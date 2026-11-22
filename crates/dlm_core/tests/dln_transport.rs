use dlm_core::{load_program_with_paths, lower_program, CheckPolicy};
use dlm_core::native;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicUsize, Ordering};
static SERIAL: AtomicUsize = AtomicUsize::new(0);
fn repo() -> PathBuf { Path::new(env!("CARGO_MANIFEST_DIR")).join("../..") }
fn temp() -> PathBuf {
    let p=std::env::temp_dir().join(format!("dlm-transport-{}-{}",std::process::id(),SERIAL.fetch_add(1,Ordering::Relaxed)));
    std::fs::create_dir_all(&p).unwrap();p
}
fn reject(body: &str, expected: &str) {
    let directory=temp();let path=directory.join("bad.dlm");
    std::fs::write(&path,format!("module test.transport\nimport std.net_transport\nnative {{ {body} }}\n")).unwrap();
    let program=load_program_with_paths(&path,&[repo().join("lib")]).unwrap();
    let errors=lower_program(&program,CheckPolicy::allow_unsafe()).unwrap_err();
    assert!(errors.iter().any(|e|e.message.to_lowercase().contains(&expected.to_lowercase())),"expected {expected}: {errors:?}");
    std::fs::remove_dir_all(directory).unwrap();
}
#[test]
fn selection_is_pure_and_matches_vm_without_a_network_provider() {
    let program=load_program_with_paths(&repo().join("examples/native/network_transport_policy.dlm"),&[repo().join("lib")]).unwrap();
    let lowered=lower_program(&program,CheckPolicy::default()).unwrap();
    assert!(!native::emit::emit(&lowered.native,true).contains("DLM_NATIVE_NET_PROVIDER"));
    let mut vm=native::vm::Vm::new(&lowered.native);vm.run().unwrap();assert!(vm.output.is_empty());
    let directory=temp();let executable=directory.join(if cfg!(windows) {"policy.exe"} else {"policy"});
    native::build::build(&lowered.native,&executable,&Default::default(),false).unwrap();
    let result=Command::new(&executable).output().unwrap();assert!(result.status.success(),"{}",String::from_utf8_lossy(&result.stderr));assert!(result.stdout.is_empty());std::fs::remove_dir_all(directory).unwrap();
}
#[test]
fn message_contract_runs_for_tcp_and_udp_in_compiled_dlm() {
    let program=load_program_with_paths(&repo().join("examples/native/network_transport.dlm"),&[repo().join("lib")]).unwrap();
    assert!(lower_program(&program,CheckPolicy::default()).is_err());let lowered=lower_program(&program,CheckPolicy::allow_unsafe()).unwrap();
    let directory=temp();let executable=directory.join(if cfg!(windows) {"transport.exe"} else {"transport"});
    native::build::build(&lowered.native,&executable,&Default::default(),false).unwrap();
    let result=Command::new(&executable).output().unwrap();assert!(result.status.success(),"{}",String::from_utf8_lossy(&result.stderr));
    assert_eq!(String::from_utf8(result.stdout).unwrap().replace("\r\n","\n"),"transport TCP UDP messages policy retention and backpressure OK\n");std::fs::remove_dir_all(directory).unwrap();
}
#[test]
fn generic_quic_sample_compiles_for_all_three_message_profiles() {
    let program=load_program_with_paths(&repo().join("examples/native/network_transport_quic.dlm"),&[repo().join("lib")]).unwrap();
    let lowered=lower_program(&program,CheckPolicy::allow_unsafe()).unwrap();let directory=temp();let executable=directory.join(if cfg!(windows) {"quic.exe"} else {"quic"});
    native::build::build(&lowered.native,&executable,&Default::default(),false).unwrap();assert!(executable.is_file());std::fs::remove_dir_all(directory).unwrap();
    // Runtime uses the pinned provider: dln_transport.py / Build-Network.ps1.
}
#[test]
fn sessions_cannot_be_copied_fabricated_or_used_after_consumption() {
    reject("fn main()->Unit{let s:TransportSession=zero<TransportSession>();}","zero");
    reject("fn bad(s:TransportSession)->Unit{let copy:TransportSession=s;}","explicit move");
    reject("fn bad(s:TransportSession)->Unit{transport_close(move s);let e:NetworkError=transport_error(&s);}","use after move");
    reject("fn bad(s:UdpSocket)->Unit{let t:TransportSession=transport_adopt_tcp(move s,TransportRequirements(16,0,0),transport_bounds());}","TcpSocket");
    reject("fn bad(s:TcpSocket)->Unit{let t:TransportSession=transport_adopt_tcp(move s,TransportRequirements(16,0,0),transport_bounds());let e:NetworkError=tcp_error(&s);}","use after move");
    reject("fn bad(s:TransportSession)->Unit{let mut bytes:Buffer<U8>=buffer<U8>(8);let x:TransportDelivery=transport_receive(&s,&mut bytes);}","mut");
}
#[test]
fn io_and_pure_contracts_remain_separate() {
    reject("fn main()->Unit guarantees(NoIO){let a:U32=transport_available();}","NoIO");
    reject("fn bad(s:&mut TransportSession,c:&Resource<OpaqueHandle,net_cancel_dispose>)->Unit guarantees(NoBlocking){let r:NetworkTransfer=transport_poll(s,1000,c);}","NoBlocking");
}
