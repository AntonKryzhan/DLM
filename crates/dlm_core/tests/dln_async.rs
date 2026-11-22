use dlm_core::{load_program_with_paths, lower_program, CheckPolicy};
use dlm_core::native;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicUsize, Ordering};

static SERIAL: AtomicUsize = AtomicUsize::new(0);
fn repo() -> PathBuf { Path::new(env!("CARGO_MANIFEST_DIR")).join("../..") }
fn temp() -> PathBuf {
    let path=std::env::temp_dir().join(format!("dlm-async-{}-{}",std::process::id(),SERIAL.fetch_add(1,Ordering::Relaxed)));
    std::fs::create_dir_all(&path).unwrap();path
}
fn reject(body: &str, expected: &str) {
    let directory=temp();let path=directory.join("bad.dlm");
    std::fs::write(&path,format!("module test.scope\nimport std.net_async\nnative {{ {body} }}\n")).unwrap();
    let program=load_program_with_paths(&path,&[repo().join("lib")]).unwrap();
    let errors=lower_program(&program,CheckPolicy::allow_unsafe()).unwrap_err();
    assert!(errors.iter().any(|error|error.message.to_lowercase().contains(&expected.to_lowercase())),"expected {expected}: {errors:?}");
    std::fs::remove_dir_all(directory).unwrap();
}
fn execute(name: &str, expected: &str) {
    let program=load_program_with_paths(&repo().join(format!("examples/native/{name}.dlm")),&[repo().join("lib")]).unwrap();
    let lowered=lower_program(&program,CheckPolicy::allow_unsafe()).unwrap();
    let directory=temp();let executable=directory.join(if cfg!(windows) {"scope.exe"} else {"scope"});
    native::build::build(&lowered.native,&executable,&Default::default(),false).unwrap();
    let result=Command::new(executable).output().unwrap();
    assert!(result.status.success(),"{}",String::from_utf8_lossy(&result.stderr));
    assert_eq!(String::from_utf8(result.stdout).unwrap().replace("\r\n","\n"),format!("{expected}\n"));
    std::fs::remove_dir_all(directory).unwrap();
}
#[test]
fn scope_runs_concurrent_tcp_udp_and_retains_join_results() { execute("network_scope","scoped TCP UDP and join OK"); }
#[test]
fn scope_runs_bounded_signals_cancel_and_tokens_after_close() { execute("network_scope_control","scoped wake cancellation and backpressure OK"); }
#[test]
fn scope_and_waker_owners_cannot_be_duplicated_or_reused() {
    reject("fn main()->Unit{let g:NetworkScope=scope_open(1,1,0,1);let copy:NetworkScope=g;}","explicit move");
    reject("fn main()->Unit{let g:NetworkScope=scope_open(1,1,0,1);scope_close(move g);let e:NetworkError=scope_error(&g);}","use after move");
    reject("fn main()->Unit{let g:NetworkScope=zero<NetworkScope>();}","zero");
    reject("fn main()->Unit{let g:NetworkScope=scope_open(1,1,0,1);let w:ScopeCanceller=scope_canceller(&g);let copy:ScopeCanceller=w;}","explicit move");
    reject("fn main()->Unit{let g:NetworkScope=scope_open(1,1,0,1);let w:ScopeCanceller=scope_canceller(&g);scope_canceller_close(move w);let r:NetworkTransfer=scope_request_cancel(&w);}","use after move");
}
#[test]
fn scope_key_and_waker_types_are_distinct() {
    reject("fn main()->Unit{let mut g:NetworkScope=scope_open(1,1,8,1);let c:ReactorUdp=ReactorUdp(1);let r:NetworkOperation=scope_read(&mut g,c,8,true,10);}","ReactorTcp");
    reject("fn main()->Unit{let g:NetworkScope=scope_open(1,1,0,1);let w:ScopeCanceller=scope_canceller(&g);let r:NetworkTransfer=event_wake(&w);}","EventWaker");
    reject("fn main()->Unit{let g:NetworkScope=scope_open(1,1,0,1);let e:NetworkError=reactor_error(&g);}","NetworkReactor");
}
#[test]
fn scope_ffi_and_effects_remain_explicit() {
    let program=load_program_with_paths(&repo().join("examples/native/network_scope.dlm"),&[repo().join("lib")]).unwrap();
    assert!(lower_program(&program,CheckPolicy::default()).is_err());
    reject("fn main()->Unit guarantees(NoAlloc){let g:NetworkScope=scope_open(1,1,0,1);}","NoAlloc");
    reject("fn main()->Unit guarantees(NoIO){let g:NetworkScope=scope_open(1,1,0,1);}","NoIO");
    reject("fn poll(g:&mut NetworkScope,c:&Resource<OpaqueHandle,net_cancel_dispose>)->Unit guarantees(NoBlocking){let r:NetworkTransfer=scope_join(g,10,c);}","NoBlocking");
}
