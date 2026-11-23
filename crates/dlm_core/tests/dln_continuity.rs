use dlm_core::{load_program_with_paths,lower_program,CheckPolicy};
use std::path::{Path,PathBuf};
fn root()->PathBuf{Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")}
#[test]fn continuity_stdlib_and_c_abi_fixture_lower_with_explicit_unsafe_ffi(){let p=load_program_with_paths(&root().join("crates/dlm_core/tests/native_hosts/gateway_continuity_boundary.dlm"),&[root().join("lib")]).unwrap();assert!(lower_program(&p,CheckPolicy::default()).is_err());lower_program(&p,CheckPolicy::allow_unsafe()).unwrap();}
#[test]fn pools_and_outboxes_remain_owned_and_effectful(){let path=std::env::temp_dir().join(format!("dlm-continuity-invalid-{}.dlm",std::process::id()));for(body,expected)in[
("fn bad(p:GatewayPool)->Unit{let cloned:GatewayPool=p;}","explicit move"),
("fn bad(p:GatewayOutbox)->Unit{gateway_outbox_close(move p);let e:NetworkError=gateway_outbox_error(&p);}","use after move"),
("fn bad(p:&GatewayPool)->Unit{let raw:&NetOpened=resource_get(&p.inner);let forged:GatewayPool=GatewayPool(resource(*raw));}","unsafe"),
("fn bad(p:&mut GatewayPool)->Unit guarantees(NoIO){let r:NetworkTransfer=gateway_pool_refresh(p);}","NoIO"),
("fn bad(p:&mut GatewayPool,d:GatewayDeliveryDescriptor)->Unit guarantees(NoBlocking){let r:GatewayDeliveryReceipt=gateway_pool_delivery_status(p,d);}","NoBlocking"),
]{std::fs::write(&path,format!("module test.continuity.invalid\nimport std.dln_continuity\nnative{{{body}}}\n")).unwrap();let p=load_program_with_paths(&path,&[root().join("lib")]).unwrap();let errors=match lower_program(&p,CheckPolicy::allow_unsafe()){Ok(_)=>panic!("unexpected acceptance: {body}"),Err(errors)=>errors};assert!(errors.iter().any(|e|e.message.to_lowercase().contains(&expected.to_lowercase())),"{body}: {errors:?}");}std::fs::remove_file(path).unwrap();}
