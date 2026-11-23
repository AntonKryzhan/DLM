use dlm_core::{load_program_with_paths,lower_program,CheckPolicy,native};
use std::path::{Path,PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicUsize,Ordering};
#[path = "support/dln_runtime.rs"]
mod dln_runtime;
static SERIAL:AtomicUsize=AtomicUsize::new(0);
fn root()->PathBuf{Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")}
struct Work(PathBuf);
impl Work{fn new()->Self{let p=std::env::temp_dir().join(format!("dlm-nodes-{}-{}",std::process::id(),SERIAL.fetch_add(1,Ordering::Relaxed)));std::fs::create_dir_all(&p).unwrap();Self(p)}}
impl Drop for Work{fn drop(&mut self){let _=std::fs::remove_dir_all(&self.0);}}
fn link_run(source:&Path,work:&Path,library:Option<&Path>,provider:bool){
 let out=work.join(if cfg!(windows){"host.exe"}else{"host"});let mut command=Command::new(dlm_core::default_c_compiler());command.args(["-std=gnu11","-O1","-Wall","-Wextra","-Werror","-UNDEBUG"]);if provider{command.arg("-Wno-misleading-indentation");}command.arg(source).arg(format!("-I{}",work.display()));if let Some(lib)=library{command.arg(lib);}if cfg!(windows){command.args(["-lshell32","-lws2_32","-lgdi32","-luser32"]);}else{command.args(["-pthread","-lm","-ldl"]);}let built=command.arg("-o").arg(&out).output().unwrap();assert!(built.status.success(),"{}",String::from_utf8_lossy(&built.stderr));let mut run=Command::new(out);dln_runtime::configure(&mut run);let ran=run.output().unwrap();assert!(ran.status.success(),"{}",String::from_utf8_lossy(&ran.stderr));assert!(String::from_utf8_lossy(&ran.stdout).contains(" OK"));
}
#[test]
fn identity_discovery_content_vectors_and_real_three_peer_exchange(){let w=Work::new();link_run(&root().join("crates/dlm_core/tests/native_hosts/dln_nodes_host.c"),&w.0,None,true);}
#[test]
fn actual_dlm_ownership_records_and_foreign_abi_agree(){
 let w=Work::new();let hosts=root().join("crates/dlm_core/tests/native_hosts");let program=load_program_with_paths(&hosts.join("dln_nodes.dlm"),&[root().join("lib")]).unwrap();assert!(lower_program(&program,CheckPolicy::default()).is_err());let lowered=lower_program(&program,CheckPolicy::allow_unsafe()).unwrap();let library=w.0.join("dln_nodes.a");native::build::build(&lowered.native,&library,&native::build::Options{artifact:native::build::Artifact::Staticlib,..Default::default()},false).unwrap();link_run(&hosts.join("dln_nodes_abi_host.c"),&w.0,Some(&library),false);
}
#[test]
fn node_handles_require_moves_and_mutation_borrows_and_io_effects(){
 let w=Work::new();let source=w.0.join("bad.dlm");for(body,expected)in[
 ("fn bad(k:NodeKey)->Unit{let copy:NodeKey=k;}","explicit move"),
 ("fn bad(k:&NodeKey)->Unit{let raw:&NetOpened=resource_get(&k.inner);let duplicate:NodeKey=NodeKey(resource(*raw));}","unsafe"),
 ("fn bad(t:&NodeTrust)->Unit{let raw:&NetOpened=resource_get(&t.inner);let duplicate:NodeTrust=NodeTrust(resource(*raw));}","unsafe"),
 ("fn bad(d:&NodeDiscovery)->Unit{let raw:&NetOpened=resource_get(&d.inner);let duplicate:NodeDiscovery=NodeDiscovery(resource(*raw));}","unsafe"),
 ("fn bad(k:NodeKey)->Unit{node_key_close(move k);let p:NodePublicResult=node_public(&k);}","use after move"),
 ("fn bad(s:&NodeSigner, xs:Slice<NetworkLocator>)->Unit{let r:NodeRecordResult=node_record(s,xs,1000,60);}","mut"),
 ("fn bad(s:&mut NodeTrust, k:NodePublicKey)->Unit guarantees(NoIO){let r:NetworkError=node_trust_pin(s,k);}","NoIO"),
 ("fn bad(c:ContentCache)->Unit{let copy:ContentCache=c;}","explicit move"),
 ("fn bad(d:&NodeDiscovery, c:&Resource<OpaqueHandle,net_cancel_dispose>)->Unit{let r:NetworkTransfer=discovery_poll(d,0,c);}","mut"),
 ("fn bad(c:NodeChallenge,t:&mut NodeTrust,b:Slice<U8>)->Unit{let first:ConnectionIdentity=node_connection_check(move c,t,b);let second:ConnectionIdentity=node_connection_check(move c,t,b);}","use after move"),
 ("fn bad(k:&mut NodeKey)->Unit{let state:&mut NetOpened=resource_get_mut(&mut k.inner);}","unsafe"),
 ("fn bad(d:&mut NodeDiscovery,c:&Resource<OpaqueHandle,net_cancel_dispose>)->Unit guarantees(NoBlocking){let r:NetworkTransfer=discovery_poll(d,100,c);}","NoBlocking"),
 ]{std::fs::write(&source,format!("module test.nodes.bad\nimport std.net_discovery\nnative{{ {body} }}\n")).unwrap();let program=load_program_with_paths(&source,&[root().join("lib")]).unwrap();let errors=match lower_program(&program,CheckPolicy::allow_unsafe()){Ok(_)=>panic!("unexpectedly accepted: {body}"),Err(errors)=>errors};assert!(errors.iter().any(|e|e.message.to_lowercase().contains(&expected.to_lowercase())),"expected {expected}: {errors:?}");}
}
#[test]
fn old_programs_do_not_include_the_new_provider(){
 let w=Work::new();let path=w.0.join("old.dlm");for source in["module old.pure\nnative{fn main()->Unit{assert(1==1);}}\n","module old.socket\nimport std.net_socket\nnative{fn main()->Unit{let s:UdpSocket=udp_bind(socket_address_v4(127,0,0,1,0),false);let e:NetworkError=udp_error(&s);assert(e.kind==0);}}\n"]{std::fs::write(&path,source).unwrap();let p=load_program_with_paths(&path,&[root().join("lib")]).unwrap();let n=lower_program(&p,CheckPolicy::allow_unsafe()).unwrap();let c=native::emit::emit(&n.native,true);assert!(!c.contains("DLM_NATIVE_NODES_PROVIDER"));}
}
