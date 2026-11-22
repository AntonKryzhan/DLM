use dlm_core::{load_program_with_paths, lower_program, CheckPolicy};
use dlm_core::native;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicUsize, Ordering};

static SERIAL: AtomicUsize = AtomicUsize::new(0);
fn repo() -> PathBuf { Path::new(env!("CARGO_MANIFEST_DIR")).join("../..") }
fn temp() -> PathBuf {
    let p=std::env::temp_dir().join(format!("dlm-reactor-{}-{}",std::process::id(),SERIAL.fetch_add(1,Ordering::Relaxed)));
    std::fs::create_dir_all(&p).unwrap();p
}
fn reject(body: &str, expected: &str) {
    let directory=temp();let path=directory.join("bad.dlm");
    std::fs::write(&path,format!("module test.reactor\nimport std.net_reactor\nnative {{ {body} }}\n")).unwrap();
    let program=load_program_with_paths(&path,&[repo().join("lib")]).unwrap();
    let errors=lower_program(&program,CheckPolicy::allow_unsafe()).unwrap_err();
    assert!(errors.iter().any(|e|e.message.to_lowercase().contains(&expected.to_lowercase())),"expected {expected}: {errors:?}");
    std::fs::remove_dir_all(directory).unwrap();
}
#[test]
fn concurrent_tcp_operations_run_with_owned_reactor() {
    let program=load_program_with_paths(&repo().join("examples/native/network_reactor.dlm"),&[repo().join("lib")]).unwrap();
    let lowered=lower_program(&program,CheckPolicy::allow_unsafe()).unwrap();
    let directory=temp();let executable=directory.join(if cfg!(windows) {"reactor.exe"} else {"reactor"});
    native::build::build(&lowered.native,&executable,&Default::default(),false).unwrap();
    let result=Command::new(executable).output().unwrap();
    assert!(result.status.success(),"{}",String::from_utf8_lossy(&result.stderr));
    assert_eq!(String::from_utf8(result.stdout).unwrap().replace("\r\n","\n"),"reactor TCP operations OK\n");
    std::fs::remove_dir_all(directory).unwrap();
}
#[test]
fn reactor_owner_and_adopted_socket_cannot_be_duplicated_or_reused() {
    reject("fn main()->Unit{let g:NetworkReactor=reactor_open(1,1,8);let other:NetworkReactor=g;}","explicit move");
    reject("fn main()->Unit{let g:NetworkReactor=reactor_open(1,1,8);reactor_close(move g);let e:NetworkError=reactor_error(&g);}","use after move");
    reject("fn main()->Unit{let mut g:NetworkReactor=reactor_open(1,1,8);let s:UdpSocket=udp_bind(socket_address_v4(127,0,0,1,0),false);let c:ReactorUdpResult=reactor_adopt_udp(&mut g,move s);udp_close(move s);}","use after move");
    reject("fn main()->Unit{let g:NetworkReactor=zero<NetworkReactor>();}","zero");
}
#[test]
fn reactor_channels_keep_tcp_udp_and_listener_types_distinct() {
    reject("fn main()->Unit{let mut g:NetworkReactor=reactor_open(1,1,8);let c:ReactorUdp=ReactorUdp(1);let op:NetworkOperation=reactor_read(&mut g,c,8,true,10);}","ReactorTcp");
    reject("fn main()->Unit{let mut g:NetworkReactor=reactor_open(1,1,8);let c:ReactorTcp=ReactorTcp(1);let op:NetworkOperation=reactor_accept(&mut g,c,10);}","ReactorListener");
    reject("fn main()->Unit{let mut g:NetworkReactor=reactor_open(1,1,8);let c:ReactorTcp=ReactorTcp(1);let op:NetworkOperation=reactor_receive(&mut g,c,8,10);}","ReactorUdp");
}
#[test]
fn reactor_ffi_requires_admission_and_cannot_claim_pure_effects() {
    let program=load_program_with_paths(&repo().join("examples/native/network_reactor.dlm"),&[repo().join("lib")]).unwrap();
    assert!(lower_program(&program,CheckPolicy::default()).is_err());
    reject("fn main()->Unit guarantees(NoAlloc){let g:NetworkReactor=reactor_open(1,1,8);}","NoAlloc");
    reject("fn main()->Unit guarantees(NoIO){let g:NetworkReactor=reactor_open(1,1,8);}","NoIO");
}
