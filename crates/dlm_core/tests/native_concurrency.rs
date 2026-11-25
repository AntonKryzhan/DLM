use dlm_core::{
    lower_program,
    native::{self, Mir},
    parse_module, CheckPolicy, Program,
};
use std::{
    path::PathBuf,
    process::Command,
    sync::atomic::{AtomicUsize, Ordering},
};
static SERIAL: AtomicUsize = AtomicUsize::new(0);
fn directory() -> PathBuf {
    let p = std::env::temp_dir().join(format!(
        "dlm-cpu-{}-{}",
        std::process::id(),
        SERIAL.fetch_add(1, Ordering::Relaxed)
    ));
    std::fs::create_dir_all(&p).unwrap();
    p
}
fn mir(s: &str) -> Mir {
    lower_program(
        &Program::single(parse_module(&format!("module test.cpu\nnative {{ {s} }}")).unwrap()),
        CheckPolicy::default(),
    )
    .unwrap()
    .native
}
fn reject(s: &str, why: &str) {
    let m = parse_module(&format!("module test.cpu\nnative {{ {s} }}")).unwrap();
    let errors = native::check_program(&Program::single(m)).unwrap_err();
    assert!(
        errors.iter().any(|e| e.message.contains(why)),
        "expected {why}: {errors:?}"
    );
}
fn execute(executable: &std::path::Path) -> std::process::Output {
    let mut command = if let Some(runner) = std::env::var_os("DLM_NATIVE_TEST_RUNNER") {
        let mut command = Command::new(runner);
        command.arg(executable);
        command
    } else {
        Command::new(executable)
    };
    command.output().unwrap()
}
fn both(s: &str, expected: &str) {
    let m = mir(s);
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(vm.output.join("\n"), expected);
    let d = directory();
    let exe = d.join(if cfg!(windows) { "cpu.exe" } else { "cpu" });
    let exe = native::build::build(&m, &exe, &Default::default(), false).unwrap();
    let r = execute(&exe);
    assert!(r.status.success(), "{}", String::from_utf8_lossy(&r.stderr));
    assert_eq!(
        String::from_utf8(r.stdout).unwrap().replace("\r\n", "\n"),
        format!("{expected}\n")
    );
    std::fs::remove_dir_all(d).unwrap();
}
const CHANNELS: &str = r#"
fn produce(tx:Sender<U64>)->U64 guarantees(ThreadSafe){for i in 0..1000{assert(send(&tx,cast<U64>(i)));}return 1000;}
fn consume(rx:Receiver<U64>)->U64 guarantees(ThreadSafe){let mut total:U64=0;for i in 0..1000{let mut value:U64=0;assert(receive(&rx,&mut value));total=total+value;}return total;}
fn main()->Unit{let pool:Executor=executor(2,8);let tx:Sender<U64>=channel<U64>(1);let rx:Receiver<U64>=channel_receiver(&tx);let producer:Task<U64>=spawn(&pool,produce,sender_clone(&tx));let consumer:Task<U64>=spawn(&pool,consume,move rx);assert(join(move producer)==U64(1000));assert(join(move consumer)==U64(499500));print("bounded channels OK");}
"#;
#[test]
fn bounded_channels_run_concurrently_in_native_and_vm() {
    both(CHANNELS, "bounded channels OK");
}
#[test]
fn owned_buffers_records_arrays_and_boxed_results_cross_threads() {
    both(
        r#"
record Payload repr(C){data:Buffer<U64>;count:U64;}
fn work(p:Payload,offset:U64)->Payload guarantees(ThreadSafe){let mut data:Buffer<U64>=move p.data;for i in 0..len(&data){data[i]=data[i]+offset;}return Payload(move data,p.count);}
fn boxed(x:U64)->Box<U64> guarantees(ThreadSafe){return box<U64>(x+U64(1));}
fn array_work(a:Array<U64,3>)->Array<U64,3> guarantees(ThreadSafe){return a;}
fn main()->Unit{let pool:Executor=executor(2,8);let mut data:Buffer<U64>=buffer<U64>(3);data[0]=U64(1);data[2]=U64(5);let t:Task<Payload>=spawn(&pool,work,Payload(move data,3),U64(7));let b:Task<Box<U64>>=spawn(&pool,boxed,U64(41));let a:Array<U64,3>=zero<Array<U64,3>>();let at:Task<Array<U64,3>>=spawn(&pool,array_work,a);let ar:Array<U64,3>=join(move at);assert(ar[2]==U64(0));let p:Payload=join(move t);let x:Box<U64>=join(move b);assert(p.data[0]==U64(8));assert(p.data[2]==U64(12));assert(p.count==U64(3));assert(*x==U64(42));print("owned transfer OK");}
"#,
        "owned transfer OK",
    );
}
#[test]
fn cancellation_queue_limits_and_cooperative_stop() {
    both(
        r#"
fn blocker(tx:Sender<U64>)->U64 guarantees(ThreadSafe){assert(send(&tx,U64(1)));while !task_cancelled(){let ok:Bool=task_sleep(1);}return 17;}
fn constant(input:Buffer<U8>)->U64 guarantees(ThreadSafe){return 42;}
fn main()->Unit{let pool:Executor=executor(1,1);let tx:Sender<U64>=channel<U64>(1);let rx:Receiver<U64>=channel_receiver(&tx);let running:Task<U64>=spawn(&pool,blocker,sender_clone(&tx));let mut signal:U64=0;assert(receive(&rx,&mut signal));let pending:Task<U64>=spawn(&pool,constant,buffer<U8>(256));let rejected:Task<U64>=spawn(&pool,constant,buffer<U8>(512));assert(task_status(&rejected)==U32(5));assert(task_cancel(&pending));assert(task_wait(&running,0)==U32(1));assert(task_cancel(&running));assert(join(move running)==U64(17));assert(task_wait(&pending,U32(4294967295))==U32(3));print("cancellation and limits OK");}
"#,
        "cancellation and limits OK",
    );
}
#[test]
fn task_failure_is_contained_and_does_not_stop_other_jobs() {
    both(
        r#"
fn broken()->U64 guarantees(ThreadSafe){let data:Buffer<U8>=buffer<U8>(32);assert(false);return 1;}
fn good()->U64 guarantees(ThreadSafe){return 42;}
fn main()->Unit{let pool:Executor=executor(2,8);let bad:Task<U64>=spawn(&pool,broken);let ok:Task<U64>=spawn(&pool,good);assert(task_wait(&bad,U32(4294967295))==U32(4));assert(join(move ok)==U64(42));print("failure isolation OK");}
"#,
        "failure isolation OK",
    );
}
#[test]
fn channel_close_drains_owned_messages_and_preserves_failed_receive_output() {
    both(
        r#"
fn main()->Unit{let tx:Sender<Buffer<U64>>=channel<Buffer<U64>>(1);{let rx:Receiver<Buffer<U64>>=channel_receiver(&tx);assert(send(&tx,buffer<U64>(4)));assert(!try_send(&tx,buffer<U64>(8)));}assert(!send(&tx,buffer<U64>(3)));let numbers:Sender<U64>=channel<U64>(1);let receiver:Receiver<U64>=channel_receiver(&numbers);let mut out:U64=99;assert(!try_receive(&receiver,&mut out));assert(out==U64(99));print("channel cleanup OK");}
"#,
        "channel cleanup OK",
    );
}
#[test]
fn dropping_executor_waits_for_children_and_task_handle_can_outlive_it() {
    both(
        r#"
fn slow()->U64 guarantees(ThreadSafe){let ok:Bool=task_sleep(10);return 7;}
fn launch()->Task<U64>{let pool:Executor=executor(1,2);let t:Task<U64>=spawn(&pool,slow);return move t;}
fn main()->Unit{let t:Task<U64>=launch();let status:U32=task_status(&t);assert(status==U32(2)||status==U32(3));print("structured teardown OK");}
"#,
        "structured teardown OK",
    );
}
#[test]
fn ownership_and_send_checks_reject_invalid_transfers() {
    reject("fn f(x:&U64)->U64 guarantees(ThreadSafe){return *x;}fn main()->Unit{let p:Executor=executor(1,1);let n:U64=1;let t:Task<U64>=spawn(&p,f,&n);}","Send-safe");
    reject("fn f(x:ThreadBound<U64>)->U64{return 1;}fn main()->Unit{let p:Executor=executor(1,1);let t:Task<U64>=spawn(&p,f,thread_bind(U64(1)));}","ThreadSafe");
    reject("fn f()->U64{return 1;}fn main()->Unit{let p:Executor=executor(1,1);let t:Task<U64>=spawn(&p,f);}","ThreadSafe");
    reject("fn f()->U64 guarantees(ThreadSafe){return 1;}fn main()->Unit{let p:Executor=executor(1,1);let t:Task<U64>=spawn(&p,f);let copy:Task<U64>=t;}","explicit move");
    reject("fn f()->U64 guarantees(ThreadSafe){return 1;}fn main()->Unit{let p:Executor=executor(1,1);let t:Task<U64>=spawn(&p,f);let x:U64=join(move t);let y:U32=task_status(&t);}","use after move");
    reject(
        "fn main()->Unit{let t:Task<U64>=zero<Task<U64>>();}",
        "zero",
    );
    reject("fn main()->Unit{let tx:Sender<U64>=channel<U64>(1);let mut value:U64=0;let ok:Bool=receive(&tx,&mut value);}","endpoint kind");
    reject(
        "fn f(p:&Executor)->Unit guarantees(NoAlloc){let q:Executor=executor(1,1);}",
        "NoAlloc",
    );
    reject(
        "fn f(t:Task<U64>)->U64 guarantees(NoBlocking){return join(move t);}",
        "NoBlocking",
    );
}
#[test]
fn repeated_failure_and_scope_cleanup_leave_no_live_allocations() {
    let m = mir(r#"
record Aligned repr(C,align=64){data:Buffer<U64>;value:U64;}
fn bad()->U64 guarantees(ThreadSafe){let b:Buffer<U64>=buffer<U64>(64);assert(false);return 1;}
fn bad_input(b:Buffer<U64>)->U64 guarantees(ThreadSafe){assert(false);return 1;}
fn wait()->U64 guarantees(ThreadSafe){let ok:Bool=task_sleep(100);return 2;}
fn transfer(value:Aligned)->Aligned guarantees(ThreadSafe){return move value;}
export fn exercise()->U64{let p:Executor=executor(2,8);let bad_task:Task<U64>=spawn(&p,bad);assert(task_wait(&bad_task,U32(4294967295))==U32(4));let bad_owned:Task<U64>=spawn(&p,bad_input,buffer<U64>(64));assert(task_wait(&bad_owned,U32(4294967295))==U32(4));let task:Task<Aligned>=spawn(&p,transfer,Aligned(buffer<U64>(3),7));let result:Aligned=join(move task);assert(result.value==U64(7));let tx:Sender<Aligned>=channel<Aligned>(2);{let rx:Receiver<Aligned>=channel_receiver(&tx);assert(send(&tx,move result));}return 42;}
export fn fail_scope()->U64{let p:Executor=executor(2,8);let a:Task<U64>=spawn(&p,wait);let b:Task<U64>=spawn(&p,wait);assert(false);return 1;}
export fn fail_join()->U64{let p:Executor=executor(1,1);let t:Task<U64>=spawn(&p,bad);return join(move t);}
"#);
    let d = directory();
    let source = d.join("runtime.c");
    let exe = d.join(if cfg!(windows) {
        "runtime.exe"
    } else {
        "runtime"
    });
    let mut c = native::emit::emit(&m, false);
    c.push_str(r#"
#ifdef DNC_TRACK_ALLOCATIONS
void *__real_calloc(size_t,size_t);void __real_free(void*);
static _Atomic uint64_t dnc_blocks;
void *__wrap_calloc(size_t n,size_t s){void*p=__real_calloc(n,s);if(p)atomic_fetch_add(&dnc_blocks,1);return p;}
void __wrap_free(void*p){if(p)atomic_fetch_sub(&dnc_blocks,1);__real_free(p);}
static uint64_t dnc_test_blocks(void){return atomic_load(&dnc_blocks);}
#else
static uint64_t dnc_test_blocks(void){return 0;}
#endif
int main(void){for(int i=0;i<40;i++){
 if(exercise()!=42||dlm_native_last_error()||dlm_native_live_allocations()||dnc_test_blocks())return 21;
 (void)fail_scope();if(!dlm_native_last_error()||dlm_native_live_allocations()||dnc_test_blocks())return 22;dlm_native_clear_error();
 (void)fail_join();if(!dlm_native_last_error()||dlm_native_live_allocations()||dnc_test_blocks())return 23;dlm_native_clear_error();
}if(!dlm_native_runtime_shutdown())return 24;return 0;}
"#);
    std::fs::write(&source, c).unwrap();
    let compiler = dlm_core::default_c_compiler();
    let mut command = Command::new(&compiler);
    command
        .args(["-std=gnu11", "-O1"])
        .arg(&source)
        .args(["-pthread", "-lm"])
        .arg("-o")
        .arg(&exe);
    if cfg!(target_os = "linux") && std::env::var_os("DLM_NATIVE_TEST_RUNNER").is_none() {
        command.args([
            "-DDNC_TRACK_ALLOCATIONS",
            "-Wl,--wrap=calloc",
            "-Wl,--wrap=free",
        ]);
    }
    let result = command.output().unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    let result = execute(&exe);
    assert!(
        result.status.success(),
        "exit {:?}: {}",
        result.status.code(),
        String::from_utf8_lossy(&result.stderr)
    );
    std::fs::remove_dir_all(d).unwrap();
}
#[test]
fn cancellation_releases_a_task_blocked_on_a_channel() {
    both(
        r#"
fn waiter(rx:Receiver<U64>,ready:Sender<U64>)->U64 guarantees(ThreadSafe){assert(send(&ready,U64(1)));let mut value:U64=0;assert(!receive(&rx,&mut value));assert(task_cancelled());return 9;}
fn main()->Unit{let pool:Executor=executor(1,4);let tx:Sender<U64>=channel<U64>(1);let rx:Receiver<U64>=channel_receiver(&tx);let ready:Sender<U64>=channel<U64>(1);let signal:Receiver<U64>=channel_receiver(&ready);let job:Task<U64>=spawn(&pool,waiter,move rx,sender_clone(&ready));let mut value:U64=0;assert(receive(&signal,&mut value));assert(task_cancel(&job));assert(join(move job)==U64(9));print("blocked cancellation OK");}
"#,
        "blocked cancellation OK",
    );
}
