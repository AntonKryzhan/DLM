//! v0.101: the rational interval example runs and builds with identical output.
use std::path::{Path, PathBuf};
use std::process::Command;

fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn expected() -> &'static str {
    "[6074000999/4294967296,759250125/536870912]\n[-5/4,5/4]\n[0/1,9/16]\n[-3/8,9/16]\n[1/4,2/3]\ninterval division by an interval containing zero\n[-1/2,3/1]"
}

#[test]
fn cli_runs_exact_interval_example() {
    let output = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["run", "--trusted-only", "--lib"])
        .arg(root().join("lib"))
        .arg(root().join("examples/valid/interval_q_demo.dlm"))
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let text = String::from_utf8(output.stdout).unwrap();
    assert_eq!(
        text.split("program output:\n").nth(1).unwrap().trim(),
        expected()
    );
}

#[test]
fn cli_builds_standalone_exact_interval_program() {
    let dir = std::env::temp_dir().join(format!("dlm-cli-interval-q-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let exe = dir.join(if cfg!(windows) { "main.exe" } else { "main" });
    let output = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["build", "--trusted-only", "--lib"])
        .arg(root().join("lib"))
        .arg("-o")
        .arg(&exe)
        .arg(root().join("examples/valid/interval_q_demo.dlm"))
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let native = Command::new(exe).output().unwrap();
    assert!(
        native.status.success(),
        "{}",
        String::from_utf8_lossy(&native.stderr)
    );
    assert_eq!(String::from_utf8(native.stdout).unwrap().trim(), expected());
    std::fs::remove_dir_all(dir).unwrap();
}
