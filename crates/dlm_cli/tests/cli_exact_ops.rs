use std::path::{Path, PathBuf};
use std::process::Command;

fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn expected() -> &'static str {
    "-680564733841876926926749214863536422912\n510423550381407695195061911147652317184\n340282366920938463463374607431768211455,680564733841876926926749214863536422910\n-243/32"
}

#[test]
fn cli_runs_exact_powers_lcm_and_square_root_example() {
    let output = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["run", "--trusted-only", "--lib"])
        .arg(root().join("lib"))
        .arg(root().join("examples/valid/exact_scalar_operations.dlm"))
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
fn cli_builds_standalone_exact_scalar_operations_program() {
    let dir = std::env::temp_dir().join(format!("dlm-cli-exact-ops-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let exe = dir.join(if cfg!(windows) { "main.exe" } else { "main" });
    let output = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["build", "--trusted-only", "--lib"])
        .arg(root().join("lib"))
        .arg("-o")
        .arg(&exe)
        .arg(root().join("examples/valid/exact_scalar_operations.dlm"))
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
