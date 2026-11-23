use dlm_core::{load_program_with_paths, lower_program, CheckPolicy};
use std::path::{Path, PathBuf};
fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}
#[test]
fn laboratory_fixture_checks_ownership_and_explicit_unsafe() {
    let p = load_program_with_paths(
        &root().join("crates/dlm_core/tests/native_hosts/channel_lab_boundary.dlm"),
        &[root().join("lib")],
    )
    .unwrap();
    assert!(lower_program(&p, CheckPolicy::default()).is_err());
    lower_program(&p, CheckPolicy::allow_unsafe()).unwrap();
}
#[test]
fn simulation_remains_pure_and_loopback_remains_effectful() {
    let path = std::env::temp_dir().join(format!("dlm-lab-{}.dlm", std::process::id()));
    for(body,expected)in[
("fn bad(x:ChannelLab)->Unit{let y:ChannelLab=x;}","explicit move"),
("fn bad(x:ChannelLabCancel)->Unit{channel_lab_cancel_close(move x);let r:ChannelLabIo=channel_lab_cancel(&x);}","use after move"),
("fn bad(x:&mut ChannelLab,b:Slice<U8>)->Unit guarantees(NoIO){let r:ChannelLabIo=channel_lab_loopback(x,b);}","NoIO"),
("fn bad(x:&mut ChannelLab,b:Slice<U8>)->Unit guarantees(NoAlloc){let r:ChannelLabIo=channel_lab_simulate(x,b);}","NoAlloc"),
("fn bad(x:&ChannelLab)->Unit{let s:&ChannelLabOpened=resource_get(&x.inner);let forged:ChannelLab=ChannelLab(resource(*s));}","unsafe"),
]{std::fs::write(&path,format!("module test.lab.invalid\nimport std.dln_experiment\nnative{{{body}}}\n")).unwrap();let p=load_program_with_paths(&path,&[root().join("lib")]).unwrap();let e=lower_program(&p,CheckPolicy::allow_unsafe()).unwrap_err();assert!(e.iter().any(|e|e.message.to_lowercase().contains(&expected.to_lowercase())),"{body}: {e:?}");}
    std::fs::remove_file(path).unwrap();
}
#[test]
fn local_codec_operations_allow_noio_but_preserve_allocation_effects() {
    let path = std::env::temp_dir().join(format!("dlm-lab-pure-{}.dlm", std::process::id()));
    std::fs::write(&path,"module test.lab.pure\nimport std.dln_experiment\nnative{fn run(x:&mut ChannelLab,b:Slice<U8>)->ChannelLabIo guarantees(NoBlocking,NoIO){return channel_lab_simulate(x,b);}fn stop(t:&ChannelLabCancel)->ChannelLabIo guarantees(NoAlloc,NoBlocking,NoIO){return channel_lab_cancel(t);}fn shape(x:&ChannelLab)->ChannelLabSummary guarantees(NoAlloc,NoBlocking,NoIO){return channel_lab_summary(x);}}\n").unwrap();
    let p = load_program_with_paths(&path, &[root().join("lib")]).unwrap();
    lower_program(&p, CheckPolicy::allow_unsafe()).unwrap();
    std::fs::remove_file(path).unwrap();
}
