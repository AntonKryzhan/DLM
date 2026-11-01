use std::path::{Path, PathBuf};
use std::process::Command;

fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}
const EXPECTED: &str = "-5/4;-4/3\n-27/148;-15/37\n1/2;-3/4\n13/16\ntrue";

#[test]
fn cli_interprets_exact_complex_q_example() {
    let out = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["run", "--trusted-only", "--lib"])
        .arg(root().join("lib"))
        .arg(root().join("examples/valid/exact_complex_q.dlm"))
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    assert_eq!(
        String::from_utf8(out.stdout)
            .unwrap()
            .split("program output:\n")
            .nth(1)
            .unwrap()
            .trim(),
        EXPECTED
    );
}

#[test]
fn cli_builds_standalone_exact_complex_q_example() {
    let dir = std::env::temp_dir().join(format!("dlm-cli-complex-q-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let exe = dir.join(if cfg!(windows) { "main.exe" } else { "main" });
    let out = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["build", "--trusted-only", "--lib"])
        .arg(root().join("lib"))
        .arg("-o")
        .arg(&exe)
        .arg(root().join("examples/valid/exact_complex_q.dlm"))
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let native = Command::new(exe).output().unwrap();
    assert!(
        native.status.success(),
        "{}",
        String::from_utf8_lossy(&native.stderr)
    );
    assert_eq!(String::from_utf8(native.stdout).unwrap().trim(), EXPECTED);
    std::fs::remove_dir_all(dir).unwrap();
}
