use std::process::Command;
fn root() -> std::path::PathBuf {
    std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}
#[test]
fn rational_cli_reports_exact_inverse_and_solution() {
    let out = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["matrix-q", "--inverse", "--solve"])
        .arg(root().join("examples/math/rational_rhs.qmat"))
        .arg(root().join("examples/math/rational_system.qmat"))
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let text = String::from_utf8(out.stdout).unwrap();
    for expected in [
        "coefficients: Q",
        "StaticProof: no",
        "rank: 2",
        "determinant: 5/6",
        "6/5 -3/5",
        "-2/5 6/5",
        "particular solution:\n2 1\n0/1\n2/1",
    ] {
        assert!(text.contains(expected), "missing {expected}: {text}");
    }
}
#[test]
fn rational_cli_handles_wide_values_and_rejects_resource_and_input_errors() {
    let path = std::env::temp_dir().join(format!("dlm-qmatrix-{}.qmat", std::process::id()));
    std::fs::write(&path, "1 1\n340282366920938463463374607431768211456").unwrap();
    let good = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .arg("matrix-q")
        .arg(&path)
        .output()
        .unwrap();
    assert!(good.status.success());
    assert!(String::from_utf8_lossy(&good.stdout)
        .contains("determinant: 340282366920938463463374607431768211456/1"));
    for args in [
        vec!["--bits", "128"],
        vec!["--bits", "0"],
        vec!["--bits", "16385"],
        vec!["--work", "0"],
        vec!["--work", "nope"],
        vec!["--what"],
    ] {
        let out = Command::new(env!("CARGO_BIN_EXE_dlm"))
            .arg("matrix-q")
            .args(args)
            .arg(&path)
            .output()
            .unwrap();
        assert!(!out.status.success());
        assert!(out.stdout.is_empty());
        assert!(String::from_utf8_lossy(&out.stderr).contains("rational matrix error:"));
    }
    for text in ["2 2\n1", "1 1\n1/0", "1 1\n1 2"] {
        std::fs::write(&path, text).unwrap();
        let out = Command::new(env!("CARGO_BIN_EXE_dlm"))
            .arg("matrix-q")
            .arg(&path)
            .output()
            .unwrap();
        assert!(!out.status.success());
        assert!(out.stdout.is_empty());
    }
    std::fs::remove_file(path).unwrap();
}
