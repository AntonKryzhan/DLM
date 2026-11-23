use std::path::Path;
use std::process::Command;

#[path = "support/dln_runtime.rs"]
mod dln_runtime;

#[test]
fn native_release_checks_delayed_probes_silent_failover_loss_reordering_and_integrity() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let work = std::env::temp_dir().join(format!("dlm-release-{}", std::process::id()));
    std::fs::create_dir_all(&work).unwrap();
    let executable = work.join(if cfg!(windows) { "release.exe" } else { "release" });
    let mut compiler = Command::new(dlm_core::default_c_compiler());
    compiler.args(["-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation", "-UNDEBUG"])
        .arg(root.join("crates/dlm_core/tests/native_hosts/dln_release_host.c"));
    if cfg!(windows) { compiler.arg("-lws2_32"); }
    else { compiler.args(["-pthread", "-lm", "-ldl"]); }
    let built = compiler.arg("-o").arg(&executable).output().unwrap();
    assert!(built.status.success(), "{}", String::from_utf8_lossy(&built.stderr));
    for args in [
        vec!["probes"],
        vec!["blackhole"],
        vec!["auth-loss"],
        vec!["transfer", "14610", "10", "1", "320"],
        vec!["transfer", "14620", "10", "2", "320"],
        vec!["fuzz"],
    ] {
        let mut command = Command::new(&executable);
        dln_runtime::configure(&mut command);
        let ran = command.args(&args).output().unwrap();
        assert!(ran.status.success(), "{args:?}:\n{}\n{}", String::from_utf8_lossy(&ran.stdout), String::from_utf8_lossy(&ran.stderr));
        assert!(String::from_utf8_lossy(&ran.stdout).contains("\"status\":\"PASS\""));
    }
    std::fs::remove_dir_all(work).unwrap();
}
