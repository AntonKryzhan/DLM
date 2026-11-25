use dlm_core::{lower_program, native, parse_module, CheckPolicy, Program};
use native::abi::{export_fingerprint, AbiProfile, LayoutVersion};
use native::{Mir, Scalar, Ty};
use std::{
    path::PathBuf,
    process::Command,
    sync::atomic::{AtomicU32, Ordering},
};

fn compile(source: &str) -> Mir {
    let source = format!("module test.abi_layout\n{source}");
    let program = Program::single(parse_module(&source).unwrap());
    lower_program(&program, CheckPolicy::default())
        .unwrap()
        .native
}

fn directory() -> PathBuf {
    static SERIAL: AtomicU32 = AtomicU32::new(0);
    let path = std::env::temp_dir().join(format!(
        "dlm-abi-contract-{}-{}",
        std::process::id(),
        SERIAL.fetch_add(1, Ordering::Relaxed)
    ));
    std::fs::create_dir_all(&path).unwrap();
    path
}

#[test]
fn changing_a_nested_layout_changes_the_abi_without_changing_size() {
    let original = compile("native { record Inner repr(C,version=2){value:U64;} record Packet repr(C){inner:Inner;data:Array<U32,2>;} export fn value(p:Packet)->U64{return p.inner.value;} }");
    let changed = compile("native { record Inner repr(C,version=3){value:U64;} record Packet repr(C){inner:Inner;data:Array<U32,2>;} export fn value(p:Packet)->U64{return p.inner.value;} }");
    let unrelated = compile("native { record Spare repr(C){x:F32;} record Packet repr(C){inner:Inner;data:Array<U32,2>;} record Inner repr(C,version=2){value:U64;} export fn value(p:Packet)->U64{return p.inner.value;} }");
    let packet = Ty::Record("Packet".to_string());
    let a = original.layout_contract(&packet).unwrap();
    let b = changed.layout_contract(&packet).unwrap();
    assert_eq!(
        (a.size, a.alignment, &a.field_offsets, a.version),
        (16, 8, &vec![0, 8], LayoutVersion(1))
    );
    assert_eq!((a.size, a.alignment), (b.size, b.alignment));
    assert_ne!(a.fingerprint, b.fingerprint);
    assert_eq!(
        a.fingerprint,
        unrelated.layout_contract(&packet).unwrap().fingerprint
    );
    assert!(a.c_abi && a.send && a.sync && !a.owned);
    let buffer = Ty::Buffer(Box::new(packet.clone()));
    let storage = original.layout_contract(&buffer).unwrap();
    assert_eq!(storage.element_stride, Some(16));
    assert!(storage.owned && !storage.c_abi && storage.send);
    assert_ne!(
        storage.fingerprint,
        changed.layout_contract(&buffer).unwrap().fingerprint
    );
    let layouts = original.type_layouts().unwrap();
    assert!(layouts.iter().any(|entry| entry.contract == a));
    assert!(original
        .layout_contract(&Ty::Array(Box::new(Ty::Scalar(Scalar::U8)), 0))
        .is_err());
    let f = original
        .unit
        .functions
        .iter()
        .find(|f| f.export.is_some())
        .unwrap();
    assert_ne!(
        export_fingerprint(&original, f, AbiProfile::SystemV64).unwrap(),
        export_fingerprint(&original, f, AbiProfile::Win64).unwrap()
    );
    let f2 = changed
        .unit
        .functions
        .iter()
        .find(|f| f.export.is_some())
        .unwrap();
    assert_ne!(
        export_fingerprint(&original, f, AbiProfile::SystemV64).unwrap(),
        export_fingerprint(&changed, f2, AbiProfile::SystemV64).unwrap()
    );
}

#[test]
fn ordinary_native_and_c_host_agree_on_payloads_through_parallel_storage() {
    let m = compile(
        r#"
theory Math {
    record Logical(value: Nat)
    fn offset(x: Nat) -> Logical = Logical(x + 7)
}
native {
    record Packet repr(C,version=3) { value: U64; }
    ordinary "Math.offset" fn make(x: U64) -> Packet;
    fn add(i: USize, seed: Packet) -> Packet guarantees(ThreadSafe) {
        return Packet(seed.value + cast_exact<U64>(i));
    }
    export fn compute(x: U64) -> Packet {
        let pool: Executor = executor(3, 5);
        let values: Buffer<Packet> = parallel_map(&pool, 0, 5, 1, add, make(x));
        return values[4];
    }
    fn main() -> Unit { assert(compute(U64(31)).value == U64(42));print(abi_fingerprint<Packet>()); }
}
"#,
    );
    let packet = Ty::Record("Packet".into());
    let expected = m.layout_contract(&packet).unwrap().fingerprint.to_string();
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(vm.output, [expected.clone()]);
    let d = directory();
    let source = native::build::build(
        &m,
        &d.join("library"),
        &native::build::Options {
            artifact: native::build::Artifact::Sharedlib,
            ..Default::default()
        },
        true,
    )
    .unwrap();
    let host = d.join("host.c");
    let executable = d.join(if cfg!(windows) {
        "contract.exe"
    } else {
        "contract"
    });
    let f = m
        .unit
        .functions
        .iter()
        .find(|f| f.export.as_deref() == Some("compute"))
        .unwrap();
    let abi = if cfg!(windows) {
        AbiProfile::Win64
    } else {
        AbiProfile::SystemV64
    };
    let export = export_fingerprint(&m, f, abi).unwrap();
    std::fs::write(
        &host,
        format!(
            r#"
#include <string.h>
#include "library.h"
int main(void) {{
    if (strcmp(dlm_native_abi_profile(),"{}")) return 1;
    const char *layout=dlm_native_record_fingerprint("Packet");
    const char *entry=dlm_native_export_fingerprint("compute");
    if (!layout || strcmp(layout,"{expected}") || !entry || strcmp(entry,"{export}")) return 2;
    if (dlm_native_record_fingerprint("missing") || dlm_native_export_fingerprint(NULL)) return 3;
    if (dlm_native_sizeof_Packet()!=sizeof(dn_r_Packet) || dlm_native_version_Packet()!=3) return 4;
    dn_r_Packet result=compute(31);
    return result.f_value==42 && !dlm_native_last_error() && dlm_native_runtime_shutdown() ? 0 : 5;
}}
"#,
            abi.name()
        ),
    )
    .unwrap();
    let output = Command::new(dlm_core::default_c_compiler())
        .args(["-std=gnu11", "-O2"])
        .args([&source, &host])
        .args(["-pthread", "-lm", "-o"])
        .arg(&executable)
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let status = Command::new(executable).status().unwrap();
    assert!(status.success(), "ABI host returned {status}");
    std::fs::remove_dir_all(d).unwrap();
}

#[test]
fn exact_casts_detect_integer_rounding_float_underflow_and_lost_signed_zero() {
    let m = compile(
        r#"
native {
    export fn accepted() -> U64 {
        assert(cast_exact<F32>(F64(1.25)) == F32(1.25));
        assert(cast_exact<I64>(F64(-9223372036854775808.0)) == I64(-9223372036854775808));
        return cast_exact<U64>(cast_exact<F64>(U64(9007199254740992)));
    }
    export fn rounded() -> F64 { return cast_exact<F64>(U64(9007199254740993)); }
    export fn upper() -> F64 { return cast_exact<F64>(U64(18446744073709551615)); }
    export fn underflowed() -> F32 { return cast_exact<F32>(F64(1e-100)); }
    export fn signed_zero() -> I64 { return cast_exact<I64>(F64(-0.0)); }
}
"#,
    );
    let mut vm = native::vm::Vm::new(&m);
    assert_eq!(
        vm.call("accepted", vec![]).unwrap().unsigned().unwrap(),
        9007199254740992
    );
    for name in ["rounded", "upper", "underflowed", "signed_zero"] {
        assert!(vm
            .call(name, vec![])
            .unwrap_err()
            .contains("loses precision"));
    }
    let d = directory();
    let path = d.join("casts.c");
    let exe = d.join(if cfg!(windows) { "casts.exe" } else { "casts" });
    let mut c = native::emit::emit(&m, false);
    c.push_str(
        r#"
int main(void) {
    if (accepted()!=UINT64_C(9007199254740992) || dlm_native_last_error()) return 1;
    (void)rounded(); if (!dlm_native_last_error()) return 2;
    (void)upper(); if (!dlm_native_last_error()) return 3;
    (void)underflowed(); if (!dlm_native_last_error()) return 4;
    (void)signed_zero(); if (!dlm_native_last_error()) return 5;
    return 0;
}
"#,
    );
    std::fs::write(&path, c).unwrap();
    let output = Command::new(dlm_core::default_c_compiler())
        .args(["-std=gnu11", "-O2"])
        .arg(&path)
        .args(["-pthread", "-lm", "-o"])
        .arg(&exe)
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert!(Command::new(exe).status().unwrap().success());
    std::fs::remove_dir_all(d).unwrap();
}
