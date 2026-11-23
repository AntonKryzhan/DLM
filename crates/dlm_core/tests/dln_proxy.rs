use dlm_core::{load_program_with_paths, lower_program, CheckPolicy};
use std::path::{Path, PathBuf};
fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}
#[test]
fn proxy_ffi_fixture_requires_explicit_unsafe_and_preserves_owned_resources() {
    let p = load_program_with_paths(
        &root().join("crates/dlm_core/tests/native_hosts/gateway_proxy_boundary.dlm"),
        &[root().join("lib")],
    )
    .unwrap();
    assert!(lower_program(&p, CheckPolicy::default()).is_err());
    lower_program(&p, CheckPolicy::allow_unsafe()).unwrap();
}
#[test]
fn proxy_tokens_remain_owned_and_network_operations_remain_effectful() {
    let path = std::env::temp_dir().join(format!("dlm-proxy-invalid-{}.dlm", std::process::id()));
    for(body,expected)in[
("fn bad(p:GatewayProxy)->Unit{let cloned:GatewayProxy=p;}","explicit move"),
("fn bad(p:GatewayProxyCancel)->Unit{gateway_proxy_cancel_close(move p);let e:NetworkError=gateway_proxy_cancel(&p);}","use after move"),
("fn bad(p:&GatewayProxy)->Unit{let raw:&NetOpened=resource_get(&p.inner);let forged:GatewayProxy=GatewayProxy(resource(*raw));}","unsafe"),
("fn bad(p:&mut GatewayProxy,b:&mut Buffer<U8>)->Unit guarantees(NoIO){let r:GatewayTransfer=gateway_proxy_read(p,65535,b);}","NoIO"),
("fn bad(p:&mut GatewayProxy)->Unit guarantees(NoBlocking){let token:GatewayProxyCancel=gateway_proxy_cancellation(p);}","NoBlocking"),
]{std::fs::write(&path,format!("module test.proxy.invalid\nimport std.dln_proxy\nnative{{{body}}}\n")).unwrap();let p=load_program_with_paths(&path,&[root().join("lib")]).unwrap();let errors=match lower_program(&p,CheckPolicy::allow_unsafe()){Ok(_)=>panic!("unexpected acceptance: {body}"),Err(errors)=>errors};assert!(errors.iter().any(|e|e.message.to_lowercase().contains(&expected.to_lowercase())),"{body}: {errors:?}");}
    std::fs::remove_file(path).unwrap();
}
#[test]
fn cancellation_and_grant_queries_are_nonblocking_memory_operations() {
    let path = std::env::temp_dir().join(format!("dlm-proxy-pure-{}.dlm", std::process::id()));
    std::fs::write(&path,"module test.proxy.pure\nimport std.dln_proxy\nnative{fn stop(t:&GatewayProxyCancel)->NetworkError guarantees(NoAlloc,NoBlocking,NoIO){return gateway_proxy_cancel(t);}fn grants(p:&GatewayProxy,b:&mut Buffer<GatewayIpAssignment>)->NetworkTransfer guarantees(NoAlloc,NoBlocking,NoIO){return gateway_proxy_assignments(p,b);}}\n").unwrap();
    let p = load_program_with_paths(&path, &[root().join("lib")]).unwrap();
    lower_program(&p, CheckPolicy::allow_unsafe()).unwrap();
    std::fs::remove_file(path).unwrap();
}
