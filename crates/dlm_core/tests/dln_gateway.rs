use dlm_core::{load_program_with_paths,lower_program,CheckPolicy};
use std::path::{Path,PathBuf};
use std::sync::atomic::{AtomicUsize,Ordering};
static SERIAL:AtomicUsize=AtomicUsize::new(0);
fn root()->PathBuf{Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")}
#[test]fn gateway_stdlib_and_boundary_lower_with_explicit_unsafe_ffi(){let p=load_program_with_paths(&root().join("crates/dlm_core/tests/native_hosts/gateway_boundary.dlm"),&[root().join("lib")]).unwrap();assert!(lower_program(&p,CheckPolicy::default()).is_err());lower_program(&p,CheckPolicy::allow_unsafe()).unwrap();}
#[test]fn gateway_resources_cannot_be_cloned_forged_or_used_after_close(){let path=std::env::temp_dir().join(format!("dlm-gateway-negative-{}-{}.dlm",std::process::id(),SERIAL.fetch_add(1,Ordering::Relaxed)));for(body,expected)in[
("fn bad(s:GatewayClient)->Unit{let other:GatewayClient=s;}","explicit move"),
("fn bad(s:&GatewayClient)->Unit{let raw:&NetOpened=resource_get(&s.inner);let other:GatewayClient=GatewayClient(resource(*raw));}","unsafe"),
("fn bad(s:GatewayClient)->Unit{gateway_close(move s);let e:NetworkError=gateway_error(&s);}","use after move"),
("fn bad(s:&mut GatewayClient)->Unit guarantees(NoIO){let r:NetworkTransfer=gateway_stream(s,1,\"127.0.0.1:80\");}","NoIO"),
("fn bad(s:&mut GatewayClient,b:&mut Buffer<U8>)->Unit guarantees(NoBlocking){let r:GatewayTransfer=gateway_read(s,1,1,b);}","NoBlocking"),
]{std::fs::write(&path,format!("module test.gateway.invalid\nimport std.dln_gateway\nnative{{ {body} }}\n")).unwrap();let p=load_program_with_paths(&path,&[root().join("lib")]).unwrap();let errors=match lower_program(&p,CheckPolicy::allow_unsafe()){Ok(_)=>panic!("unexpected acceptance: {body}"),Err(e)=>e};assert!(errors.iter().any(|e|e.message.to_lowercase().contains(&expected.to_lowercase())),"{body}: {errors:?}");}std::fs::remove_file(path).unwrap();}
