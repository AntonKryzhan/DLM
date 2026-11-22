use std::path::{Path, PathBuf};
use std::process::{Command, Output};
use std::sync::atomic::{AtomicUsize, Ordering};
static SERIAL: AtomicUsize=AtomicUsize::new(0);
fn root() -> PathBuf { Path::new(env!("CARGO_MANIFEST_DIR")).join("../..") }
struct Work(PathBuf);
impl Work {
    fn new() -> Self { let path=std::env::temp_dir().join(format!("dlm-codegen-{}-{}",std::process::id(),SERIAL.fetch_add(1,Ordering::Relaxed)));std::fs::create_dir_all(&path).unwrap();Self(path) }
    fn path(&self,name: &str) -> PathBuf { self.0.join(name) }
}
impl Drop for Work { fn drop(&mut self) { let _=std::fs::remove_dir_all(&self.0); } }
fn run(input: &Path, output: &Path, extra: &[&str]) -> Output {
    Command::new(env!("CARGO_BIN_EXE_dlm")).arg("network-codegen").arg(input).args(["--module","generated.test","--prefix","Test","--lib"]).arg(root().join("lib")).arg("-o").arg(output).args(extra).env_remove("DLM_PATH").output().unwrap()
}
fn success(result: Output) { assert!(result.status.success(),"{}",String::from_utf8_lossy(&result.stderr)); }
fn failure(result: Output, expected: &str) { assert!(!result.status.success());let error=String::from_utf8_lossy(&result.stderr);assert!(error.contains(expected),"{error}"); }
fn schema() -> PathBuf { root().join("examples/native/network_codec_schema.dlm") }
fn sibling(path: &Path, extension: &str) -> PathBuf { PathBuf::from(format!("{}.{extension}",path.display())) }
fn contents(path: &Path) -> Vec<Vec<u8>> { [path.to_owned(),sibling(path,"dns"),sibling(path,"codec")].iter().map(|p|std::fs::read(p).unwrap()).collect() }
#[test]
fn factory_and_descriptor_produce_identical_reproducible_bundles() {
    let work=Work::new();let a=work.path("a.dlm");let b=work.path("b.dlm");success(run(&schema(),&a,&["--minor","0"]));success(run(&sibling(&a,"dns"),&b,&["--minor","0"]));assert_eq!(contents(&a),contents(&b));success(run(&schema(),&a,&["--minor","0","--check"]));
    let old=String::from_utf8(std::fs::read(&a).unwrap()).unwrap();assert!(old.contains("TestMessage1_field_11_add") && !old.contains("TestMessage1_field_10_add"));success(run(&schema(),&a,&["--minor","1","--force"]));let new=std::fs::read_to_string(&a).unwrap();assert!(new.contains("TestMessage1_field_10_add") && !new.contains("TestMessage1_field_11_add"));
}
#[test]
fn checks_and_failed_regeneration_leave_all_existing_outputs_untouched() {
    let work=Work::new();let out=work.path("codec.dlm");success(run(&schema(),&out,&[]));let mut bytes=std::fs::read(&out).unwrap();bytes.extend_from_slice(b"\n// intentional user edit\n");std::fs::write(&out,bytes).unwrap();let before=contents(&out);
    failure(run(&schema(),&out,&["--check"]),"differs");assert_eq!(before,contents(&out));failure(run(&schema(),&out,&[]),"output exists and differs");assert_eq!(before,contents(&out));success(run(&schema(),&out,&["--force"]));success(run(&schema(),&out,&["--check"]));
    std::fs::remove_file(sibling(&out,"dns")).unwrap();let source=std::fs::read(&out).unwrap();failure(run(&schema(),&out,&["--check"]),"differs");assert_eq!(source,std::fs::read(&out).unwrap());assert!(!sibling(&out,"dns").exists());
}
#[test]
fn malformed_schemas_and_unproved_required_assurance_never_generate_outputs() {
    let work=Work::new();let input=work.path("bad.dns");let out=work.path("codec.dlm");let valid=std::fs::read(root().join("examples/native/generated/codec_echo.dlm.dns")).unwrap();
    for (index,value,expected) in [(31,2u32,"proof-required"),(0,0,"invalid DNS1"),(32,17,"invalid DNS1"),(11,1,"invalid DNS1")] {
        let mut bytes=valid.clone();bytes[4+index*4..8+index*4].copy_from_slice(&value.to_be_bytes());std::fs::write(&input,bytes).unwrap();failure(run(&input,&out,&[]),expected);assert!(!out.exists() && !sibling(&out,"dns").exists() && !sibling(&out,"codec").exists());
    }
    std::fs::write(&input,&valid[..valid.len()-1]).unwrap();failure(run(&input,&out,&[]),"invalid DNS1");std::fs::write(&input,vec![0;16905]).unwrap();failure(run(&input,&out,&[]),"exceeds");
    let source=work.path("bad.dlm");std::fs::write(&source,"module test.impure\nimport std.net_spec\nnative { fn network_schema()->NetworkSpec guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork){let bytes:Buffer<U8>=buffer<U8>(1);return network_spec(1,1,0,0);} }\n").unwrap();failure(run(&source,&out,&[]),"NoAlloc");assert!(!out.exists());
    std::fs::write(&source,"module test.contract\nimport std.net_spec\nnative { fn network_schema()->NetworkSpec{return network_spec(1,1,0,0);} }\n").unwrap();failure(run(&source,&out,&[]),"must declare");assert!(!out.exists());
}
#[test]
fn invalid_options_storage_and_versions_are_rejected_before_output_installation() {
    let work=Work::new();let out=work.path("codec.dlm");
    for (extra,expected) in [
        (vec!["--minor","2"],"version window"),(vec!["--storage","255"],"usage:"),(vec!["--storage","67108865"],"usage:"),(vec!["--storage","256"],"plan failed"),(vec!["--module","generated.bad;"],"usage:"),(vec!["--prefix","bad-name"],"usage:"),(vec!["--force","--check"],"usage:"),(vec!["--factory","absent"],"was not found"),
    ] { failure(run(&schema(),&out,&extra),expected);assert!(!out.exists());assert!(!work.path(".codec.dlm.codegen-lock").exists()); }
}
#[test]
fn output_collisions_symlinks_and_concurrent_writer_locks_are_refused() {
    let work=Work::new();let out=work.path("codec.dlm");std::fs::create_dir(sibling(&out,"dns")).unwrap();failure(run(&schema(),&out,&["--force"]),"non-regular output");assert!(!out.exists());std::fs::remove_dir(sibling(&out,"dns")).unwrap();
    let lock=work.path(".codec.dlm.codegen-lock");std::fs::write(&lock,[]).unwrap();failure(run(&schema(),&out,&[]),"generation lock");failure(run(&schema(),&out,&["--check"]),"generation is in progress");assert!(!out.exists());std::fs::remove_file(&lock).unwrap();
    let input=work.path("input.dlm");std::fs::copy(&schema(),&input).unwrap();let before=std::fs::read(&input).unwrap();let common=root().join("examples/native");failure(run(&input,&input,&["--lib",common.to_str().unwrap(),"--force"]),"cannot replace");assert_eq!(before,std::fs::read(&input).unwrap());
    #[cfg(unix)] { std::os::unix::fs::symlink(&input,&out).unwrap();failure(run(&schema(),&out,&["--force"]),"non-regular output");assert_eq!(before,std::fs::read(input).unwrap()); }
}
#[test]
fn generated_batch_api_handles_the_maximum_repetition_count_in_native_code() {
    let work=Work::new();let input=work.path("schema.dlm");let generated=work.path("generated/test.dlm");
    std::fs::write(&input,"module test.repeat\nimport std.net_spec\nnative {fn network_schema()->NetworkSpec guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork){let mut spec:NetworkSpec=network_spec(1,1,0,0);let mut message:MessageSchema=message_schema(1,2,3);let f:I32=message_add_field(&mut message,network_field(1,3,4096,4096,0));let m:I32=network_add_message(&mut spec,message);assert(f==0 && m==0);return spec;}}\n").unwrap();success(run(&input,&generated,&[]));
    let source=work.path("main.dlm");std::fs::write(&source,"module test.repeat.main\nimport generated.test\nnative {fn main()->Unit{let mut message:TestMessage1=TestMessage1_new();let mut values:Buffer<U32>=buffer<U32>(4096);for i in 0..4096 {values[i]=cast<U32>(i);}values[4095]=4294967295;let added:CodecError=TestMessage1_field_1_add_many(&mut message,slice(&values,0,4096));assert(added.kind==0);let size:CodecIo=TestMessage1_measure(&message);assert(size.error.kind==0 && size.count==49164);let mut bytes:Buffer<U8>=buffer<U8>(size.count);let encoded:CodecIo=TestMessage1_encode(&message,&mut bytes,0);assert(encoded.error.kind==0);let decoded:TestMessage1Decoded=TestMessage1_decode(slice(&bytes,0,size.count));assert(decoded.error.kind==0 && TestMessage1_equal(&message,&decoded.value));let last:TestMessage1Field1=TestMessage1_field_1_get(&decoded.value,4095);assert(last.error.kind==0 && last.value==U32(4294967295));print(\"maximum repeated batch codec OK\");}}\n").unwrap();
    let executable=work.path(if cfg!(windows) {"repeat.exe"} else {"repeat"});let built=Command::new(env!("CARGO_BIN_EXE_dlm")).arg("build").arg(&source).arg("--lib").arg(root().join("lib")).arg("--lib").arg(&work.0).arg("-o").arg(&executable).env_remove("DLM_PATH").output().unwrap();success(built);let result=Command::new(executable).output().unwrap();assert!(result.status.success(),"{}",String::from_utf8_lossy(&result.stderr));assert_eq!(String::from_utf8_lossy(&result.stdout).replace("\r\n","\n"),"maximum repeated batch codec OK\n");
}

#[test]
fn descriptors_cannot_shadow_the_selected_standard_library_or_replace_its_dependencies() {
    let work=Work::new();let input=root().join("examples/native/generated/codec_echo.dlm.dns");let out=work.path("codec.dlm");let lib=work.path("lib");std::fs::create_dir_all(lib.join("std/std")).unwrap();for entry in std::fs::read_dir(root().join("lib/std")).unwrap() {let entry=entry.unwrap();if entry.file_type().unwrap().is_file(){std::fs::copy(entry.path(),lib.join("std").join(entry.file_name())).unwrap();}}
    let fake=lib.join("std/std/net_spec.dlm");std::fs::copy(root().join("lib/std/net_spec.dlm"),&fake).unwrap();let result=Command::new(env!("CARGO_BIN_EXE_dlm")).arg("network-codegen").arg(&input).args(["--module","generated.test","--prefix","Test","--lib"]).arg(&lib).arg("-o").arg(&out).env_remove("DLM_PATH").output().unwrap();failure(result,"shadows the selected standard library");assert!(!out.exists());std::fs::remove_file(fake).unwrap();
    let target=lib.join("std/net_spec.dlm");let before=std::fs::read(&target).unwrap();let result=Command::new(env!("CARGO_BIN_EXE_dlm")).arg("network-codegen").arg(&input).args(["--module","generated.test","--prefix","Test","--lib"]).arg(lib).args(["--force","-o"]).arg(&target).env_remove("DLM_PATH").output().unwrap();failure(result,"cannot replace");assert_eq!(before,std::fs::read(&target).unwrap());
}
#[test]
fn dlmb_v1_generated_payload_uses_the_requested_envelope_version() {
    let work=Work::new();let input=work.path("v1.dns");let output=work.path("generated/test.dlm");let mut bytes=std::fs::read(root().join("examples/native/generated/codec_echo.dlm.dns")).unwrap();bytes[28..32].copy_from_slice(&1u32.to_be_bytes());std::fs::write(&input,bytes).unwrap();success(run(&input,&output,&[]));
    let source=work.path("main.dlm");std::fs::write(&source,"module test.codec.v1\nimport generated.test\nnative {fn main()->Unit{let m:TestMessage2=TestMessage2_new();let mut b:Buffer<U8>=buffer<U8>(12);let e:CodecIo=TestMessage2_encode(&m,&mut b,0);assert(e.error.kind==0 && b[8]==1);let d:TestMessage2Decoded=TestMessage2_decode(slice(&b,0,12));assert(d.error.kind==0 && TestMessage2_equal(&m,&d.value));b[8]=2;let wrong:TestMessage2Decoded=TestMessage2_decode(slice(&b,0,12));assert(wrong.error.kind==4);}}\n").unwrap();
    let executable=work.path(if cfg!(windows){"v1.exe"}else{"v1"});success(Command::new(env!("CARGO_BIN_EXE_dlm")).arg("build").arg(source).arg("--lib").arg(root().join("lib")).arg("--lib").arg(&work.0).arg("-o").arg(&executable).env_remove("DLM_PATH").output().unwrap());assert!(Command::new(executable).status().unwrap().success());
}
