use dlm_core::{load_program_with_paths, lower_program, CheckPolicy};
use dlm_core::native;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicUsize, Ordering};

static SERIAL: AtomicUsize = AtomicUsize::new(0);
fn repo() -> PathBuf { Path::new(env!("CARGO_MANIFEST_DIR")).join("../..") }
fn temp() -> PathBuf {
    let path=std::env::temp_dir().join(format!("dlm-crypto-{}-{}",std::process::id(),SERIAL.fetch_add(1,Ordering::Relaxed)));
    std::fs::create_dir_all(&path).unwrap();path
}
fn reject(body: &str, expected: &str) {
    let directory=temp();let path=directory.join("bad.dlm");
    std::fs::write(&path,format!("module test.crypto\nimport std.net_crypto\nnative {{ {body} }}\n")).unwrap();
    let program=load_program_with_paths(&path,&[repo().join("lib")]).unwrap();
    let errors=lower_program(&program,CheckPolicy::allow_unsafe()).unwrap_err();
    assert!(errors.iter().any(|error|error.message.to_lowercase().contains(&expected.to_lowercase())),"expected {expected}: {errors:?}");
    std::fs::remove_dir_all(directory).unwrap();
}
fn execute(name: &str, expected: &str) {
    let program=load_program_with_paths(&repo().join(format!("examples/native/{name}.dlm")),&[repo().join("lib")]).unwrap();
    let lowered=lower_program(&program,CheckPolicy::allow_unsafe()).unwrap();
    let directory=temp();let executable=directory.join(if cfg!(windows) {"crypto.exe"} else {"crypto"});
    native::build::build(&lowered.native,&executable,&Default::default(),false).unwrap();
    let result=Command::new(executable).output().unwrap();
    assert!(result.status.success(),"{}",String::from_utf8_lossy(&result.stderr));
    assert_eq!(String::from_utf8(result.stdout).unwrap().replace("\r\n","\n"),format!("{expected}\n"));
    std::fs::remove_dir_all(directory).unwrap();
}
#[test]
fn crypto_agreement_encrypted_udp_authentication_and_replay() { execute("network_crypto","X25519 HKDF AES-GCM encrypted UDP and replay checks OK"); }
#[test]
fn crypto_record_byte_and_capacity_budgets() { execute("network_crypto_limits","crypto key budgets capacity and ownership OK"); }
#[test]
fn crypto_owners_are_linear_and_private_keys_are_consumed() {
    reject("fn main()->Unit{let k:CryptoPrivate=crypto_private_generate();let copy:CryptoPrivate=k;}","explicit move");
    reject("fn main()->Unit{let k:CryptoPrivate=crypto_private_generate();crypto_private_close(move k);let e:NetworkError=crypto_private_error(&k);}","use after move");
    reject("fn main()->Unit{let c:CryptoChannel=zero<CryptoChannel>();}","zero");
    reject("fn main()->Unit{let k:CryptoPrivate=crypto_private_generate();let b:Buffer<U8>=buffer<U8>(32);let s:CryptoSecret=crypto_agree(move k,slice(&b,0,32));crypto_private_close(move k);}","use after move");
}
#[test]
fn crypto_key_kinds_and_explicit_raw_import_are_checked() {
    reject("fn main()->Unit{let k:CryptoPrivate=crypto_private_generate();let b:Buffer<U8>=buffer<U8>(1);let mut out:Buffer<U8>=buffer<U8>(32);let h:NetworkTransfer=crypto_hmac(&k,slice(&b,0,1),&mut out);}","CryptoSecret");
    reject("fn main()->Unit{let b:Buffer<U8>=buffer<U8>(32);let k:CryptoSecret=crypto_secret_import(slice(&b,0,32));}","unsafe");
}
#[test]
fn crypto_ffi_cannot_claim_purity_or_ignore_admission() {
    let program=load_program_with_paths(&repo().join("examples/native/network_crypto.dlm"),&[repo().join("lib")]).unwrap();
    assert!(lower_program(&program,CheckPolicy::default()).is_err());
    reject("fn main()->Unit guarantees(NoIO){let k:CryptoPrivate=crypto_private_generate();}","NoIO");
    reject("fn main()->Unit guarantees(NoAlloc){let k:CryptoPrivate=crypto_private_generate();}","NoAlloc");
}
