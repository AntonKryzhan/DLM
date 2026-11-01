use std::process::Command;

#[test]
fn version_flag_reports_workspace_version() {
    for flag in ["--version", "-V"] {
        let output = Command::new(env!("CARGO_BIN_EXE_dlm"))
            .arg(flag)
            .output()
            .expect("run dlm");
        assert!(output.status.success(), "dlm {flag} must succeed");
        let stdout = String::from_utf8(output.stdout).expect("utf8 stdout");
        assert_eq!(stdout.trim(), format!("dlm {}", env!("CARGO_PKG_VERSION")));
    }
}

#[test]
fn check_and_run_gpu_checkpoint_example() {
    let example = concat!(env!("CARGO_MANIFEST_DIR"), "/../../examples/valid/gpu_checkpoint_restore_memory.dlm");

    let check = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["check", example])
        .output()
        .expect("run dlm check");
    assert!(check.status.success(), "dlm check failed: {}", String::from_utf8_lossy(&check.stderr));

    let run = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["run", example])
        .output()
        .expect("run dlm run");
    assert!(run.status.success(), "dlm run failed: {}", String::from_utf8_lossy(&run.stderr));
    let stdout = String::from_utf8(run.stdout).expect("utf8 stdout");
    assert!(stdout.contains("gpu_memory_checkpoint<memory_mib=8192>"), "unexpected output: {stdout}");
}
