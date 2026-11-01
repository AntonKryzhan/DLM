use std::process::Command;
fn root() -> std::path::PathBuf {
    std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}
fn expected() -> String {
    "-1/1\n\"2 2\n0/1 1/1\n1/1 -340282366920938463463374607431768211456/1\n\"\n2\n\"2 1\n0/1\n2/1\n\"".into()
}
#[test]
fn cli_interprets_big_rational_matrix_inverse_determinant_and_solve() {
    let out = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["run", "--trusted-only", "--lib"])
        .arg(root().join("lib"))
        .arg(root().join("examples/valid/bigrational_matrix_demo.dlm"))
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
        expected()
    );
}
#[test]
fn cli_builds_native_big_rational_matrix_program() {
    let dir = std::env::temp_dir().join(format!("dlm-cli-bigmatrix-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let exe = dir.join(if cfg!(windows) { "main.exe" } else { "main" });
    let out = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["build", "--trusted-only", "--lib"])
        .arg(root().join("lib"))
        .arg("-o")
        .arg(&exe)
        .arg(root().join("examples/valid/bigrational_matrix_demo.dlm"))
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let native = Command::new(&exe).output().unwrap();
    assert!(native.status.success());
    assert_eq!(String::from_utf8(native.stdout).unwrap().trim(), expected());
    std::fs::remove_dir_all(dir).unwrap();
}
#[test]
fn q_witness_export_replay_without_source_external_binding_and_forgery() {
    let dir = std::env::temp_dir().join(format!("dlm-cli-qwitness-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let input = dir.join("input.qmat");
    let proof = dir.join("rank.qproof");
    std::fs::write(&input, "2 3\n1 2 3\n2 4 6").unwrap();
    let cli = env!("CARGO_BIN_EXE_dlm");
    let out = Command::new(cli)
        .arg("matrix-q-export")
        .arg(&input)
        .arg(&proof)
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    assert!(proof.is_file());
    let out = Command::new(cli)
        .arg("matrix-q-check")
        .arg(&proof)
        .arg(&input)
        .output()
        .unwrap();
    assert!(out.status.success());
    let text = String::from_utf8(out.stdout).unwrap();
    assert!(text.contains("matches supplied matrix"));
    assert!(text.contains("rank: 1; nullity: 2"));
    assert!(text.contains("StaticProof: no"));
    std::fs::write(&input, "2 3\n1 2 3\n2 4 7").unwrap();
    let out = Command::new(cli)
        .arg("matrix-q-check")
        .arg(&proof)
        .arg(&input)
        .output()
        .unwrap();
    assert!(!out.status.success());
    assert!(out.stdout.is_empty());
    std::fs::remove_file(&input).unwrap();
    let out = Command::new(cli)
        .arg("matrix-q-check")
        .arg(&proof)
        .output()
        .unwrap();
    assert!(out.status.success());
    assert!(String::from_utf8_lossy(&out.stdout).contains("embedded matrix"));
    let good = std::fs::read_to_string(&proof).unwrap();
    let bad = good.replace("matrix input 2 3 1/1", "matrix input 2 3 2/1");
    assert_ne!(bad, good);
    std::fs::write(&proof, bad).unwrap();
    let out = Command::new(cli)
        .arg("matrix-q-check")
        .arg(&proof)
        .output()
        .unwrap();
    assert!(!out.status.success());
    assert!(String::from_utf8_lossy(&out.stderr).contains("Q witness error:"));
    std::fs::remove_dir_all(dir).unwrap();
}
#[test]
fn q_witness_fixed_fixture_and_strict_format_errors() {
    let cli = env!("CARGO_BIN_EXE_dlm");
    let out = Command::new(cli)
        .arg("matrix-q-check")
        .arg(root().join("examples/math/rational_rank.qproof"))
        .arg(root().join("examples/math/rational_rank.qmat"))
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let path = std::env::temp_dir().join(format!("dlm-bad-qproof-{}.txt", std::process::id()));
    let good = std::fs::read_to_string(root().join("examples/math/rational_rank.qproof")).unwrap();
    for text in [
        "DLM-KERNEL-PROOF v1".into(),
        good.replace("v1", "v9"),
        format!("{good}extra"),
        " ".repeat((1 << 20) + 1),
    ] {
        std::fs::write(&path, text).unwrap();
        let out = Command::new(cli)
            .arg("matrix-q-check")
            .arg(&path)
            .output()
            .unwrap();
        assert!(!out.status.success());
        assert!(out.stdout.is_empty());
        assert!(String::from_utf8_lossy(&out.stderr).contains("Q witness error:"));
    }
    std::fs::remove_file(path).unwrap();
}
