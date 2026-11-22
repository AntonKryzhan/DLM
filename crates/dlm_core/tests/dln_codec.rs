use dlm_core::{load_program_with_paths, lower_program, CheckPolicy};
use dlm_core::native;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicUsize, Ordering};
static SERIAL: AtomicUsize = AtomicUsize::new(0);
fn repo() -> PathBuf { Path::new(env!("CARGO_MANIFEST_DIR")).join("../..") }
fn temp() -> PathBuf {
    let path=std::env::temp_dir().join(format!("dlm-codec-{}-{}",std::process::id(),SERIAL.fetch_add(1,Ordering::Relaxed)));std::fs::create_dir_all(&path).unwrap();path
}
fn sample(name: &str, expected: &str, unsafe_allowed: bool) {
    let program=load_program_with_paths(&repo().join(format!("examples/native/{name}.dlm")),&[repo().join("lib")]).unwrap();
    let lowered=lower_program(&program,if unsafe_allowed {CheckPolicy::allow_unsafe()} else {CheckPolicy::default()}).unwrap();
    if unsafe_allowed { assert!(lower_program(&program,CheckPolicy::default()).is_err()); }
    else { assert!(!native::emit::emit(&lowered.native,true).contains("DLM_NATIVE_NET_PROVIDER"));let mut vm=native::vm::Vm::new(&lowered.native);vm.run().unwrap();assert_eq!(vm.output.join("\n"),expected); }
    let directory=temp();let executable=directory.join(if cfg!(windows) {"codec.exe"} else {"codec"});native::build::build(&lowered.native,&executable,&Default::default(),false).unwrap();
    let result=Command::new(&executable).output().unwrap();assert!(result.status.success(),"{}",String::from_utf8_lossy(&result.stderr));assert_eq!(String::from_utf8(result.stdout).unwrap().replace("\r\n","\n"),format!("{expected}\n"));std::fs::remove_dir_all(directory).unwrap();
}
#[test]
fn typed_all_kind_codecs_match_vm_and_native_under_three_profiles() {
    sample("network_codec","generated codecs typed fields UTF8 integers unknowns and all wire profiles OK",false);
}
#[test]
fn malformed_and_short_output_paths_match_vm_without_network_ffi() {
    sample("network_codec_limits","generated codec limits malformed inputs atomic output and builder checks OK",false);
}
#[test]
fn generated_payload_roundtrips_in_an_admitted_tcp_request_reply() {
    sample("network_codec_session","generated codec admitted TCP request reply and typed roundtrip OK",true);
}
#[test]
fn checked_codec_preflight_measure_and_encode_keep_pure_bounded_contracts() {
    let directory=temp();let source=directory.join("pure.dlm");std::fs::write(&source,r#"module test.codec.contracts
import std.net_codec_runtime
native {
 fn inspect(p:&CodecPlan,b:Slice<U8>)->CodecShape guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork){return codec_scan(p,b);}
 fn measure(p:&CodecPlan,m:&CodecMessage)->CodecIo guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork){return codec_measure(p,m);}
 fn encode(p:&CodecPlan,m:&CodecMessage,b:&mut Buffer<U8>)->CodecIo guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork){return codec_encode(p,m,b,0);}
 fn equal(p:&CodecPlan,a:&CodecMessage,b:&CodecMessage)->Bool guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork){return codec_equal(p,a,b);}
}
"#).unwrap();let program=load_program_with_paths(&source,&[repo().join("lib")]).unwrap();let lowered=lower_program(&program,CheckPolicy::default()).unwrap();assert!(!native::emit::emit(&lowered.native,true).contains("DLM_NATIVE_NET_PROVIDER"));std::fs::remove_dir_all(directory).unwrap();
}
#[test]
fn generated_types_retain_scalar_message_ownership_and_view_borrow_checks() {
    let directory=temp();let path=directory.join("bad.dlm");
    for (body,expected) in [
        ("fn main()->Unit{let mut m:EchoMessage1=EchoMessage1_new();let n:U32=42;let e:CodecError=EchoMessage1_field_2_add(&mut m,n);}","U16"),
        ("fn main()->Unit{let m:PosMessage1=PosMessage1_new();let mut b:Buffer<U8>=buffer<U8>(64);let e:CodecIo=EchoMessage1_encode(&m,&mut b,0);}","EchoMessage1"),
        ("fn main()->Unit{let m:EchoMessage1=EchoMessage1_new();let copy:EchoMessage1=m;}","explicit move"),
        ("fn main()->Unit{let m:EchoMessage1=EchoMessage1_new();let moved:EchoMessage1=move m;let e:CodecIo=EchoMessage1_measure(&m);}","use after move"),
        ("fn main()->Unit{let mut m:EchoMessage1=EchoMessage1_new();let view:Slice<U8>=EchoMessage1_field_6_view(&m,0);let e:CodecError=EchoMessage1_field_6_add(&mut m,utf8_bytes(\"x\"));let n:USize=len(view);}","borrow"),
        ("fn bad()->Slice<U8>{let m:EchoMessage1=EchoMessage1_new();return EchoMessage1_field_6_view(&m,0);}","borrow"),
    ] {
        std::fs::write(&path,format!("module test.codec.types\nimport generated.codec_echo\nimport generated.codec_pos\nnative {{ {body} }}\n")).unwrap();let program=load_program_with_paths(&path,&[repo().join("lib"),repo().join("examples/native")]).unwrap();let errors=lower_program(&program,CheckPolicy::default()).unwrap_err();assert!(errors.iter().any(|e|e.message.to_lowercase().contains(&expected.to_lowercase())),"expected {expected}: {errors:?}");
    }
    std::fs::remove_dir_all(directory).unwrap();
}
