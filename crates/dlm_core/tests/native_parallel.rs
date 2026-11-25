use dlm_core::{lower_program, native, parse_module, CheckPolicy, Program};
use std::{
    path::PathBuf,
    process::Command,
    sync::atomic::{AtomicU32, Ordering},
};

fn fixture(source: &str) -> Program {
    Program::single(parse_module(&format!("module test.parallel\nnative {{ {source} }}")).unwrap())
}

fn directory() -> PathBuf {
    static SERIAL: AtomicU32 = AtomicU32::new(0);
    let path = std::env::temp_dir().join(format!(
        "dlm-parallel-{}-{}",
        std::process::id(),
        SERIAL.fetch_add(1, Ordering::Relaxed)
    ));
    std::fs::create_dir_all(&path).unwrap();
    path
}

#[test]
fn parallel_ranges_preserve_result_order_and_share_only_synchronized_state() {
    let m = lower_program(&fixture(r#"
record ResultItem repr(C) { index: USize; data: Box<U64>; }
fn transform(i: USize, input: &Buffer<U64>, bias: U64) -> ResultItem guarantees(ThreadSafe) {
    return ResultItem(i, box<U64>(input[i] + bias));
}
fn count(i: USize, total: &AtomicU64) -> Unit guarantees(ThreadSafe) {
    let mut add: Bool = false;
    while !add {
        let old: U64 = atomic_load(total, Acquire);
        add = atomic_compare_exchange(total, old, old + cast<U64>(i), AcqRel, Acquire);
    }
}
fn value(i: USize) -> USize guarantees(ThreadSafe) { return i; }
fn main() -> Unit {
    let pool: Executor = executor(4, 2);
    let mut input: Buffer<U64> = buffer<U64>(701);
    for i in 0..len(&input) { input[i] = cast<U64>(i) * 2; }
    let items: Buffer<ResultItem> = parallel_map(&pool, 3, 701, 23, transform, &input, U64(9));
    assert(len(&items) == USize(698));
    for i in 0..len(&items) {
        assert(items[i].index == i + USize(3));
        assert(*items[i].data == cast<U64>(i) * 2 + U64(15));
    }
    let total: AtomicU64 = atomic<U64>(0);
    parallel_for(&pool, 0, 701, 9, count, &total);
    assert(atomic_load(&total, SeqCst) == U64(245350));
    let empty: Buffer<USize> = parallel_map(&pool, 8, 4, 1, value);
    assert(len(&empty) == USize(0));
    let edge: Buffer<USize> = parallel_map(&pool, USize(18446744073709551614), USize(18446744073709551615), USize(18446744073709551615), value);
    assert(edge[0] == USize(18446744073709551614));
    print("parallel ranges OK");
}
"#), CheckPolicy::default()).unwrap().native;
    for deterministic in [false, true] {
        let mut vm = native::vm::Vm::new(&m);
        if deterministic {
            vm = vm.with_deterministic_scheduler();
        }
        vm.run().unwrap();
        assert_eq!(vm.output, ["parallel ranges OK"]);
    }
    let d = directory();
    let executable =
        native::build::build(&m, &d.join("parallel"), &Default::default(), false).unwrap();
    let output = Command::new(executable).output().unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(
        String::from_utf8_lossy(&output.stdout).trim(),
        "parallel ranges OK"
    );
    std::fs::remove_dir_all(d).unwrap();
}

#[test]
fn parallel_callbacks_cannot_move_or_write_unsynchronized_captures() {
    for (param, capture, setup) in [
        ("&mut U64", "&mut value", "let mut value:U64=1;"),
        (
            "Buffer<U64>",
            "move value",
            "let value:Buffer<U64>=buffer<U64>(2);",
        ),
        (
            "&ThreadBound<U64>",
            "&value",
            "let value:ThreadBound<U64>=thread_bind(U64(1));",
        ),
        ("&Ptr<U64>", "&value", "let value:Ptr<U64>=null<U64>();"),
    ] {
        let p = fixture(&format!("fn callback(i:USize,x:{param})->Unit guarantees(ThreadSafe){{}}\nfn main()->Unit{{let p:Executor=executor(1,2);{setup}parallel_for(&p,0,2,1,callback,{capture});}}"));
        let errors = native::check_program(&p).unwrap_err();
        assert!(
            errors
                .iter()
                .any(|d| d.message.contains("shared Sync references")),
            "{errors:?}"
        );
    }
}

#[test]
fn parallel_failure_joins_lanes_and_drops_partial_owned_results() {
    let m = lower_program(
        &fixture(
            r#"
fn owned(i: USize) -> Buffer<U64> guarantees(ThreadSafe) {
    let value: Buffer<U64> = buffer<U64>(8);
    assert(i != USize(17));
    return move value;
}
export fn failed() -> U64 {
    let pool: Executor = executor(4, 3);
    let values: Buffer<Buffer<U64>> = parallel_map(&pool, 0, 600, 4, owned);
    return cast<U64>(len(&values));
}
export fn invalid_grain() -> U64 {
    let pool: Executor = executor(2, 1);
    let values: Buffer<Buffer<U64>> = parallel_map(&pool, 9, 9, 0, owned);
    return cast<U64>(len(&values));
}
"#,
        ),
        CheckPolicy::default(),
    )
    .unwrap()
    .native;
    let mut vm = native::vm::Vm::new(&m);
    assert!(vm
        .call("failed", vec![])
        .unwrap_err()
        .contains("parallel callback failed"));
    assert!(vm
        .call("invalid_grain", vec![])
        .unwrap_err()
        .contains("grain"));
    let d = directory();
    let source = d.join("parallel.c");
    let executable = d.join(if cfg!(windows) {
        "parallel.exe"
    } else {
        "parallel"
    });
    let mut c = native::emit::emit(&m, false);
    c.push_str(r#"
int main(void) {
    for (int i=0; i<30; ++i) {
        if (failed()!=0 || !dlm_native_last_error() || dlm_native_live_allocations()) return 1;
        dlm_native_clear_error();
        if (invalid_grain()!=0 || !dlm_native_last_error() || dlm_native_live_allocations()) return 2;
        dlm_native_clear_error();
    }
    return dlm_native_runtime_shutdown() ? 0 : 3;
}
"#);
    std::fs::write(&source, c).unwrap();
    let output = Command::new(dlm_core::default_c_compiler())
        .args(["-std=gnu11", "-O2"])
        .arg(&source)
        .args(["-pthread", "-lm", "-o"])
        .arg(&executable)
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert!(Command::new(executable).status().unwrap().success());
    std::fs::remove_dir_all(d).unwrap();
}
