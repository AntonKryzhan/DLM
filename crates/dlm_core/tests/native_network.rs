use dlm_core::{load_program_with_paths, lower_program, CheckPolicy};
use dlm_core::native::{self, Mir};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicUsize, Ordering};

static SERIAL: AtomicUsize = AtomicUsize::new(0);
fn repo() -> PathBuf { Path::new(env!("CARGO_MANIFEST_DIR")).join("../..") }
fn temp() -> PathBuf {
    let d=std::env::temp_dir().join(format!("dlm-network-{}-{}",std::process::id(),SERIAL.fetch_add(1,Ordering::Relaxed)));
    std::fs::create_dir_all(&d).unwrap();d
}
fn lower(path: &Path) -> Mir {
    let p=load_program_with_paths(path,&[repo().join("lib")]).unwrap();
    lower_program(&p,CheckPolicy::allow_unsafe()).unwrap().native
}
fn compiled(m: &Mir) -> String {
    let d=temp();let exe=d.join(if cfg!(windows) {"network.exe"} else {"network"});
    native::build::build(m,&exe,&Default::default(),false).unwrap();
    let result=Command::new(&exe).output().unwrap();
    assert!(result.status.success(),"{}",String::from_utf8_lossy(&result.stderr));
    std::fs::remove_dir_all(d).unwrap();
    String::from_utf8(result.stdout).unwrap().replace("\r\n","\n")
}
#[test]
fn binary_tcp_udp_loopback_owns_resources() {
    let m=lower(&repo().join("examples/native/network_loopback.dlm"));
    assert!(compiled(&m).starts_with("binary TCP and UDP OK\n"));
}
#[test]
fn protocol_codecs_match_vm_and_compile_without_network_provider() {
    let m=lower(&repo().join("examples/native/network_protocol.dlm"));
    assert!(!native::emit::emit(&m,true).contains("DLM_NATIVE_NET_PROVIDER"));
    let mut vm=native::vm::Vm::new(&m);vm.run().unwrap();
    assert_eq!(compiled(&m),format!("{}\n",vm.output.join("\n")));
}
#[test]
fn protocol_rfc_vectors_all_prefixes_and_frame_limits() {
    let d=temp();let path=d.join("vectors.dlm");
    let vectors: &[(u64,&[u8])] = &[
        (0,&[0]),(63,&[63]),(64,&[0x40,0x40]),(16383,&[0x7f,0xff]),
        (16384,&[0x80,0,0x40,0]),(1073741823,&[0xbf,0xff,0xff,0xff]),
        (1073741824,&[0xc0,0,0,0,0x40,0,0,0]),
        (4611686018427387903,&[0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff]),
        (151288809941952652,&[0xc2,0x19,0x7c,0x5e,0xff,0x14,0xe8,0x8c]),
    ];
    let mut source=String::from("module network.vectors\nimport std.native_protocol\nnative { fn main()->Unit {\nlet mut b:Buffer<U8>=buffer<U8>(8);\n");
    for (index,(value,wire)) in vectors.iter().enumerate() {
        source.push_str(&format!("{{let e:ProtocolValue=quic_varint_write(&mut b,0,U64({value}));assert(e.status==I32(0));assert(e.next==USize({}));\n",wire.len()));
        for (i,byte) in wire.iter().enumerate() { source.push_str(&format!("assert(b[{i}]==U8({byte}));\n")); }
        source.push_str(&format!("let r:ProtocolValue=quic_varint_read(slice(&b,0,{}),0);assert(r.value==U64({value}));assert(r.status==I32(0));\n",wire.len()));
        for n in 0..wire.len() { source.push_str(&format!("let p{index}_{n}:ProtocolValue=quic_varint_read(slice(&b,0,{n}),0);assert(p{index}_{n}.status==I32(1));\n")); }
        source.push_str("}\n");
    }
    source.push_str(r#"
assert(quic_varint_size(U64(4611686018427387904))==USize(0));
let invalid:ProtocolValue=quic_varint_write(&mut b,0,U64(4611686018427387904));assert(invalid.status==I32(7));
let offset:ProtocolValue=quic_varint_read(slice(&b,0,0),USize(18446744073709551615));assert(offset.status==I32(4));
assert(protocol_write_be(&mut b,0,2,U64(65536))==I32(7));
assert(protocol_write_be(&mut b,0,0,U64(0))==I32(4));
assert(protocol_write_be(&mut b,7,2,U64(1))==I32(1));
assert(protocol_write_be(&mut b,0,4,U64(0))==I32(0));
let empty:ProtocolFrame=protocol_frame32(slice(&b,0,4),0,0);assert(empty.status==I32(0)&&empty.next==USize(4));
assert(protocol_write_be(&mut b,0,4,U64(4))==I32(0));
let exact:ProtocolFrame=protocol_frame32(slice(&b,0,8),0,4);assert(exact.status==I32(0)&&exact.next==USize(8));
let missing:ProtocolFrame=protocol_frame32(slice(&b,0,7),0,4);assert(missing.status==I32(1));
let large:ProtocolFrame=protocol_frame32(slice(&b,0,4),0,3);assert(large.status==I32(7));
b[0]=64;b[1]=0;let nonminimal:ProtocolValue=quic_varint_read(slice(&b,0,2),0);assert(nonminimal.status==I32(0)&&nonminimal.value==U64(0));
print("RFC vectors OK");
} }
"#);
    std::fs::write(&path,source).unwrap();let m=lower(&path);
    let mut vm=native::vm::Vm::new(&m);vm.run().unwrap();
    assert_eq!(compiled(&m),"RFC vectors OK\n");std::fs::remove_dir_all(d).unwrap();
}
#[test]
fn networking_does_not_claim_purity_or_bypass_unsafe_policy() {
    let d=temp();let path=d.join("contracts.dlm");
    std::fs::write(&path,"module network.contracts\nimport std.native_net\nnative { fn main()->Unit guarantees(NoIO) { let s:Resource<NetOpened,net_dispose>=socket_udp(\"127.0.0.1\",0); } }\n").unwrap();
    let p=load_program_with_paths(&path,&[repo().join("lib")]).unwrap();
    let errors=lower_program(&p,CheckPolicy::allow_unsafe()).unwrap_err();
    assert!(errors.iter().any(|e|e.message.contains("NoIO")),"{errors:?}");
    std::fs::write(&path,"module network.contracts\nimport std.native_net\nnative { fn main()->Unit {print(network_capabilities());} }\n").unwrap();
    let p=load_program_with_paths(&path,&[repo().join("lib")]).unwrap();
    assert!(lower_program(&p,CheckPolicy::trusted_only()).is_err());
    let m=lower(&path);let mut vm=native::vm::Vm::new(&m);
    assert!(vm.run().is_err(),"the reference VM must not silently emulate a foreign provider");
    std::fs::remove_dir_all(d).unwrap();
}
