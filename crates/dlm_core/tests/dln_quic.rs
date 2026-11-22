use dlm_core::{load_program_with_paths, lower_program, CheckPolicy};
use dlm_core::native;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};
static SERIAL: AtomicUsize = AtomicUsize::new(0);
fn repo() -> PathBuf { Path::new(env!("CARGO_MANIFEST_DIR")).join("../..") }
fn temp() -> PathBuf {
    let path=std::env::temp_dir().join(format!("dlm-quic-{}-{}",std::process::id(),SERIAL.fetch_add(1,Ordering::Relaxed)));
    std::fs::create_dir_all(&path).unwrap();path
}
fn reject(body: &str, expected: &str) {
    let directory=temp();let path=directory.join("bad.dlm");
    std::fs::write(&path,format!("module test.quic\nimport std.net_quic\nnative {{ {body} }}\n")).unwrap();
    let program=load_program_with_paths(&path,&[repo().join("lib")]).unwrap();
    let errors=lower_program(&program,CheckPolicy::allow_unsafe()).unwrap_err();
    assert!(errors.iter().any(|e|e.message.to_lowercase().contains(&expected.to_lowercase())),"expected {expected}: {errors:?}");
    std::fs::remove_dir_all(directory).unwrap();
}
#[test]
fn quic_sample_compiles_with_explicit_native_admission() {
    let program=load_program_with_paths(&repo().join("examples/native/network_quic.dlm"),&[repo().join("lib")]).unwrap();
    assert!(lower_program(&program,CheckPolicy::default()).is_err());let lowered=lower_program(&program,CheckPolicy::allow_unsafe()).unwrap();
    let directory=temp();let executable=directory.join(if cfg!(windows) {"quic.exe"} else {"quic"});
    native::build::build(&lowered.native,&executable,&Default::default(),false).unwrap();
    // Runtime interoperability is checked by dln_quic.py and Build-Network.ps1.
    assert!(executable.is_file());std::fs::remove_dir_all(directory).unwrap();
}
#[test]
fn quic_endpoint_ownership_and_socket_kinds_are_checked() {
    reject("fn main()->Unit{let e:QuicEndpoint=quic_client(socket_address_v4(127,0,0,1,0),\"p\",\"\",quic_config());let copy:QuicEndpoint=e;}","explicit move");
    reject("fn main()->Unit{let e:QuicEndpoint=quic_client(socket_address_v4(127,0,0,1,0),\"p\",\"\",quic_config());quic_close(move e);let r:NetworkError=quic_error(&e);}","use after move");
    reject("fn main()->Unit{let e:QuicEndpoint=zero<QuicEndpoint>();}","zero");
    reject("fn main()->Unit{let s:UdpSocket=udp_bind(socket_address_v4(127,0,0,1,0),false);let e:NetworkError=quic_error(&s);}","QuicEndpoint");
}
#[test]
fn quic_effects_and_provider_lifecycle_are_explicit() {
    reject("fn main()->Unit guarantees(NoIO){let b:Bool=quic_available();}","NoIO");
    reject("fn main()->Unit{let r:NetIo=quic_provider_shutdown();}","unsafe");
}
