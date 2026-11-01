use std::path::{Path, PathBuf};
use std::process::Command;
fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}
#[test]
fn cli_interprets_16384_bit_division_and_preserves_small_profile() {
    let out = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["run", "--trusted-only", "--lib"])
        .arg(root().join("lib"))
        .arg(root().join("examples/valid/wide_exact_arithmetic.dlm"))
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
        "true\n1024:2\ntrue"
    );
}
#[test]
fn cli_builds_standalone_16384_bit_arithmetic_program() {
    let dir = std::env::temp_dir().join(format!("dlm-cli-wide-exact-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let exe = dir.join(if cfg!(windows) { "main.exe" } else { "main" });
    let out = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["build", "--trusted-only", "--lib"])
        .arg(root().join("lib"))
        .arg("-o")
        .arg(&exe)
        .arg(root().join("examples/valid/wide_exact_arithmetic.dlm"))
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
    assert_eq!(
        String::from_utf8(native.stdout).unwrap().trim(),
        "true\n1024:2\ntrue"
    );
    std::fs::remove_dir_all(dir).unwrap();
}
