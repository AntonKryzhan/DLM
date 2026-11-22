use dlm_core::{load_program_with_paths, lower_program, CheckPolicy};
use dlm_core::native;
use dlm_core::proof_kernel::{check_kernel_proof, KernelBudget, KernelContext, KernelProof};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicUsize, Ordering};
static SERIAL: AtomicUsize = AtomicUsize::new(0);
fn repo() -> PathBuf { Path::new(env!("CARGO_MANIFEST_DIR")).join("../..") }
fn temp() -> PathBuf {
    let p=std::env::temp_dir().join(format!("dlm-spec-{}-{}",std::process::id(),SERIAL.fetch_add(1,Ordering::Relaxed)));std::fs::create_dir_all(&p).unwrap();p
}
fn sample(name: &str, expected: &str, unsafe_allowed: bool) {
    let program=load_program_with_paths(&repo().join(format!("examples/native/{name}.dlm")),&[repo().join("lib")]).unwrap();
    let policy=if unsafe_allowed { CheckPolicy::allow_unsafe() } else { CheckPolicy::default() };
    let lowered=lower_program(&program,policy).unwrap();
    if !unsafe_allowed {
        assert!(!native::emit::emit(&lowered.native,true).contains("DLM_NATIVE_NET_PROVIDER"));let mut vm=native::vm::Vm::new(&lowered.native);vm.run().unwrap();assert_eq!(vm.output.join("\n"),expected);
    }else { assert!(lower_program(&program,CheckPolicy::default()).is_err()); }
    let directory=temp();let executable=directory.join(if cfg!(windows) {"spec.exe"} else {"spec"});native::build::build(&lowered.native,&executable,&Default::default(),false).unwrap();
    let result=Command::new(&executable).output().unwrap();assert!(result.status.success(),"{}",String::from_utf8_lossy(&result.stderr));assert_eq!(String::from_utf8(result.stdout).unwrap().replace("\r\n","\n"),format!("{expected}\n"));std::fs::remove_dir_all(directory).unwrap();
}
#[test]
fn schemas_metadata_assurance_and_bounds_match_vm_without_network_foreign_calls() {
    sample("network_spec","NetworkSpec schemas bounds metadata negotiation and proof gates OK",false);
}
#[test]
fn version_and_directional_compatibility_match_vm_and_native() {
    sample("network_spec_versions","NetworkSpec version windows directional fields extensions and budgets OK",false);
}
#[test]
fn admitted_session_uses_existing_tcp_and_unchanged_dlmb_wire() {
    sample("network_spec_session","NetworkSpec admitted TCP session with unchanged DLMB payload OK",true);
}
#[test]
fn arithmetic_certificates_replay_in_the_existing_independent_kernel() {
    for name in ["repeated","zero","padding"] {
        let text=std::fs::read_to_string(repo().join(format!("examples/native/fixtures/network-spec-{name}.kproof"))).unwrap();
        let budget=KernelBudget::default();let proof=KernelProof::from_text(&text,budget).unwrap();check_kernel_proof(&proof,&KernelContext::default(),budget).unwrap();
    }
}
#[test]
fn preflight_and_metadata_functions_keep_pure_bounded_no_allocation_contracts() {
    let directory=temp();let path=directory.join("pure.dlm");
    std::fs::write(&path,r#"module test.spec.contracts
import std.net_spec_wire
import std.net_spec_compat
native {
 fn validate(s:&NetworkSpec)->NetworkSpecCheck guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork){return network_spec_check(s);}
 fn decode(b:Slice<U8>)->NetworkSpecDecoded guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork){return network_spec_decode(b);}
 fn encode(s:&NetworkSpec,b:&mut Buffer<U8>)->NetworkSpecIo guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork){return network_spec_encode(s,b,0);}
 fn select(s:&NetworkSpec,c:Array<TransportCandidate,6>)->NetworkAdmission guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork){return network_spec_select(s,c);}
 fn agree(a:&NetworkSpec,b:&NetworkSpec,c:Array<TransportCandidate,6>)->NetworkNegotiation guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork){return network_spec_negotiate(a,b,true,c);}
}
"#).unwrap();
    let program=load_program_with_paths(&path,&[repo().join("lib")]).unwrap();let lowered=lower_program(&program,CheckPolicy::default()).unwrap();assert!(!native::emit::emit(&lowered.native,true).contains("DLM_NATIVE_NET_PROVIDER"));std::fs::remove_dir_all(directory).unwrap();
}
