use std::process::Command;
fn root() -> std::path::PathBuf {
    std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}
fn expected() -> String {
    [
        "680564733841876926926749214863536422912",
        "115792089237316195423570985008687907853269984665640564039457584007913129639936",
        "-340282366920938463463374607431768211456/3",
        "5/6",
        "true",
    ]
    .join("\n")
}
#[test]
fn cli_interprets_more_than_128_bit_integer_and_rational_arithmetic() {
    let out = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["run", "--trusted-only", "--lib"])
        .arg(root().join("lib"))
        .arg(root().join("examples/valid/exact_arithmetic.dlm"))
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let text = String::from_utf8(out.stdout).unwrap();
    assert_eq!(
        text.split("program output:\n").nth(1).unwrap().trim(),
        expected()
    );
}
#[test]
fn cli_builds_standalone_c_program_with_identical_exact_results() {
    let dir = std::env::temp_dir().join(format!("dlm-cli-exact-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let exe = dir.join(if cfg!(windows) { "exact.exe" } else { "exact" });
    let out = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["build", "--trusted-only", "--lib"])
        .arg(root().join("lib"))
        .arg("-o")
        .arg(&exe)
        .arg(root().join("examples/valid/exact_arithmetic.dlm"))
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let native = Command::new(&exe).output().unwrap();
    assert!(
        native.status.success(),
        "{}",
        String::from_utf8_lossy(&native.stderr)
    );
    assert_eq!(String::from_utf8(native.stdout).unwrap().trim(), expected());
    std::fs::remove_dir_all(dir).unwrap();
}
