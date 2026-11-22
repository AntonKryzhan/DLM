use dlm_core::{load_program_with_paths,lower_program,CheckPolicy,native};
use std::path::{Path,PathBuf};
use std::sync::atomic::{AtomicUsize,Ordering};
use std::process::Command;
static SERIAL:AtomicUsize=AtomicUsize::new(0);
fn root()->PathBuf{Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")}
struct Work(PathBuf);
impl Work{fn new()->Self{let p=std::env::temp_dir().join(format!("dlm-state-{}-{}",std::process::id(),SERIAL.fetch_add(1,Ordering::Relaxed)));std::fs::create_dir_all(&p).unwrap();Self(p)}}
impl Drop for Work{fn drop(&mut self){let _=std::fs::remove_dir_all(&self.0);}}
#[test]
fn pure_model_deadlines_and_counters_match_vm_native_without_foreign_calls(){
 let w=Work::new();let source=w.0.join("main.dlm");std::fs::write(&source,r#"module test.state.pure
import dln_state_schema
native { fn main()->Unit{let spec:NetworkSpec=network_schema();let machine:ProtocolMachine=protocol_default(&spec,1);let checked:ProtocolCheck=protocol_machine_check(&spec,&machine,1);assert(checked.status==0 && checked.reachable==127 && checked.pending_proofs==431);let step:ProtocolStep=protocol_transition(&machine,1,1,0,1);assert(step.status==0 && step.state==2 && step.resources==1);let control:ProtocolControl=ProtocolControl(1,1,0,false,0,0,0,0,0,0);assert(protocol_clock(&control,spec.session,999,1)==0 && protocol_clock(&control,spec.session,1000,1)==3 && protocol_counter(&control,1));} }
"#).unwrap();let program=load_program_with_paths(&source,&[root().join("lib"),root().join("crates/dlm_core/tests/native_hosts")]).unwrap();let lowered=lower_program(&program,CheckPolicy::default()).unwrap();assert!(!native::emit::emit(&lowered.native,true).contains("DLM_NATIVE_NET_PROVIDER"));native::vm::Vm::new(&lowered.native).run().unwrap();let out=w.0.join(if cfg!(windows){"model.exe"}else{"model"});native::build::build(&lowered.native,&out,&Default::default(),false).unwrap();assert!(Command::new(out).status().unwrap().success());
}
#[test]
fn actual_state_runtime_passes_strict_c_abi_host(){
 let w=Work::new();let hosts=root().join("crates/dlm_core/tests/native_hosts");let program=load_program_with_paths(&hosts.join("dln_state.dlm"),&[root().join("lib")]).unwrap();assert!(lower_program(&program,CheckPolicy::default()).is_err());let lowered=lower_program(&program,CheckPolicy::allow_unsafe()).unwrap();let library=w.0.join("dln_state.a");native::build::build(&lowered.native,&library,&native::build::Options{artifact:native::build::Artifact::Staticlib,..Default::default()},false).unwrap();let out=w.0.join(if cfg!(windows){"host.exe"}else{"host"});let mut command=Command::new(dlm_core::default_c_compiler());command.args(["-std=gnu11","-O1","-Wall","-Wextra","-Werror","-UNDEBUG"]).arg(hosts.join("dln_state_host.c")).arg(format!("-I{}",w.0.display())).arg(library);if cfg!(windows){command.args(["-lshell32","-lws2_32","-lgdi32","-luser32"]);}else{command.args(["-pthread","-lm","-ldl"]);}let built=command.arg("-o").arg(&out).output().unwrap();assert!(built.status.success(),"{}",String::from_utf8_lossy(&built.stderr));let ran=Command::new(out).output().unwrap();assert!(ran.status.success(),"{}",String::from_utf8_lossy(&ran.stderr));assert!(String::from_utf8_lossy(&ran.stdout).contains("protocol state transitions"));
}
#[test]
fn owning_sessions_and_effects_cannot_be_bypassed_by_normal_source(){
 let w=Work::new();let source=w.0.join("bad.dlm");for(body,expected)in[
 ("fn bad(s:ProtocolSession)->Unit{let copy:ProtocolSession=s;}","explicit move"),
 ("fn bad(s:ProtocolSession)->Unit{protocol_close(move s);let stats:ProtocolStatistics=protocol_statistics(&s);}","use after move"),
 ("fn bad(s:&ProtocolSession)->Unit{let e:NetworkError=protocol_cancel(s);}","mut"),
 ("fn bad(s:&mut ProtocolSession)->Unit guarantees(NoIO){let e:NetworkError=protocol_authenticate(s);}","NoIO"),
 ("fn bad(s:&mut ProtocolSession)->Unit{let owned:&mut ProtocolOwned=resource_get_mut(&mut s.inner);}","unsafe"),
 ]{std::fs::write(&source,format!("module test.state.bad\nimport std.net_session\nnative{{ {body} }}\n")).unwrap();let program=load_program_with_paths(&source,&[root().join("lib")]).unwrap();let errors=lower_program(&program,CheckPolicy::allow_unsafe()).unwrap_err();assert!(errors.iter().any(|e|e.message.to_lowercase().contains(&expected.to_lowercase())),"expected {expected}: {errors:?}");}
}
