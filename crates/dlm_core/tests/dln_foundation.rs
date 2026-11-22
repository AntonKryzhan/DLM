use dlm_core::{load_program_with_paths, lower_program, CheckPolicy};
use dlm_core::native::{self, Mir};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicUsize, Ordering};

static SERIAL: AtomicUsize = AtomicUsize::new(0);
fn repo() -> PathBuf { Path::new(env!("CARGO_MANIFEST_DIR")).join("../..") }
fn temp() -> PathBuf {
    let p=std::env::temp_dir().join(format!("dlm-dln-{}-{}",std::process::id(),SERIAL.fetch_add(1,Ordering::Relaxed)));
    std::fs::create_dir_all(&p).unwrap();p
}
fn load(path: &Path) -> Mir {
    let program=load_program_with_paths(path,&[repo().join("lib")]).unwrap();
    lower_program(&program,CheckPolicy::allow_unsafe()).unwrap().native
}
fn compiled(m: &Mir) -> String {
    let d=temp();let output=d.join(if cfg!(windows) {"test.exe"} else {"test"});
    native::build::build(m,&output,&Default::default(),false).unwrap();
    let result=Command::new(&output).output().unwrap();
    assert!(result.status.success(),"{}",String::from_utf8_lossy(&result.stderr));
    std::fs::remove_dir_all(d).unwrap();String::from_utf8(result.stdout).unwrap().replace("\r\n","\n")
}
fn reject(body: &str, expected: &str) {
    let directory=temp();let path=directory.join("bad.dlm");
    std::fs::write(&path,format!("module test.dln\nimport std.net_socket\nimport std.net_codec\nnative {{ {body} }}\n")).unwrap();
    let program=load_program_with_paths(&path,&[repo().join("lib")]).unwrap();
    let errors=lower_program(&program,CheckPolicy::allow_unsafe()).unwrap_err();
    assert!(errors.iter().any(|e|e.message.to_lowercase().contains(&expected.to_lowercase())),"expected {expected}: {errors:?}");
    std::fs::remove_dir_all(directory).unwrap();
}
#[test]
fn typed_tcp_udp_close_and_socket_options_work_in_compiled_dlm() {
    let m=load(&repo().join("examples/native/network_typed.dlm"));
    assert_eq!(compiled(&m),"typed TCP and UDP OK\n");
}
#[test]
fn large_streaming_message_matches_vm_without_network_provider() {
    let m=load(&repo().join("examples/native/network_streaming.dlm"));
    assert!(!native::emit::emit(&m,true).contains("DLM_NATIVE_NET_PROVIDER"));
    let mut vm=native::vm::Vm::new(&m);vm.run().unwrap();
    assert_eq!(compiled(&m),format!("{}\n",vm.output.join("\n")));
}
#[test]
fn socket_kinds_cannot_be_substituted() {
    reject("fn main()->Unit{let mut u:UdpSocket=udp_bind(socket_address_v4(127,0,0,1,0),false);let mut b:Buffer<U8>=buffer<U8>(8);let r:NetworkTransfer=tcp_read(&mut u,&mut b);}","TcpSocket");
    reject("fn main()->Unit{let mut t:TcpSocket=tcp_connect(utf8_bytes(\"127.0.0.1\"),1,10);let b:Buffer<U8>=buffer<U8>(1);let r:NetworkTransfer=udp_send(&mut t,socket_address_v4(127,0,0,1,1),slice(&b,0,1));}","UdpSocket");
    reject("fn main()->Unit{let mut t:TcpSocket=tcp_connect(utf8_bytes(\"127.0.0.1\"),1,10);let a:TcpSocket=tcp_accept(&mut t);}","TcpListener");
    reject("fn main()->Unit{let mut t:TlsSocket=tls_connect(utf8_bytes(\"https://localhost/\"),10,utf8_bytes(\"\"),utf8_bytes(\"\"));let r:NetworkError=tcp_shutdown_write(&mut t);}","TcpSocket");
}
#[test]
fn sockets_cannot_be_copied_reused_after_close_or_zero_fabricated() {
    reject("fn main()->Unit{let t:UdpSocket=udp_bind(socket_address_v4(127,0,0,1,0),false);let other:UdpSocket=t;}","explicit move");
    reject("fn main()->Unit{let t:UdpSocket=udp_bind(socket_address_v4(127,0,0,1,0),false);udp_close(move t);let e:NetworkError=udp_error(&t);}","use after move");
    reject("fn main()->Unit{let t:UdpSocket=udp_bind(socket_address_v4(127,0,0,1,0),false);udp_close(move t);udp_close(move t);}","use after move");
    reject("fn main()->Unit{let t:TcpSocket=zero<TcpSocket>();}","zero");
}
#[test]
fn zero_copy_payload_borrows_do_not_escape_or_allow_mutation() {
    reject("fn invalid(input:&Buffer<U8>)->Slice<U8> borrows(input){let local:Buffer<U8>=buffer<U8>(8);return frame32_view(&local,0,Frame32Step(0,0,1,0,true,0));}","borrow");
    reject("fn main()->Unit{let mut b:Buffer<U8>=buffer<U8>(8);let view:Slice<U8>=frame32_view(&b,0,Frame32Step(0,0,1,0,true,0));resize(&mut b,16);print(view[0]);}","borrow");
}
#[test]
fn network_operations_do_not_acquire_false_pure_contracts() {
    reject("fn main()->Unit guarantees(NoIO){let t:UdpSocket=udp_bind(socket_address_v4(127,0,0,1,0),false);}","NoIO");
    reject("fn main()->Unit guarantees(NoAlloc){let t:TcpSocket=tcp_connect(utf8_bytes(\"127.0.0.1\"),1,10);}","NoAlloc");
}
