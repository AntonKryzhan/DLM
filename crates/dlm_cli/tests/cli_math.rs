use std::process::Command;
#[test]
fn homology_reports_examples_and_rejects_invalid_inputs() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../examples/math");
    for (name, expected) in [
        ("circle.simp", "H_1: dimension 1"),
        ("sphere.simp", "H_2: dimension 1"),
        ("disk.simp", "H_1: dimension 0"),
    ] {
        let out = Command::new(env!("CARGO_BIN_EXE_dlm"))
            .arg("homology")
            .arg(root.join(name))
            .output()
            .unwrap();
        assert!(
            out.status.success(),
            "{}",
            String::from_utf8_lossy(&out.stderr)
        );
        let text = String::from_utf8(out.stdout).unwrap();
        assert!(text.contains(expected));
        assert!(text.contains("runtime-checked"));
    }
    let file = std::env::temp_dir().join(format!("dlm-bad-complex-{}.simp", std::process::id()));
    for bad in ["0 0".to_string(), "0 nope".into(), " ".repeat(65537)] {
        std::fs::write(&file, bad).unwrap();
        let out = Command::new(env!("CARGO_BIN_EXE_dlm"))
            .arg("homology")
            .arg(&file)
            .output()
            .unwrap();
        assert!(!out.status.success());
        assert!(String::from_utf8_lossy(&out.stderr).contains("homology error:"));
    }
    std::fs::remove_file(file).unwrap();
}
#[test]
fn f2_library_checks_and_executes() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let out = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["run", "--lib"])
        .arg(root.join("lib"))
        .arg(root.join("examples/valid/f2_arithmetic.dlm"))
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
        "false\ntrue\nfalse\ntrue"
    );
}

#[test]
fn homology_witness_exports_replays_without_source_and_binds_external_claim() {
    let dir = std::env::temp_dir().join(format!("dlm-homology-replay-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let source = dir.join("circle.simp");
    let proof = dir.join("circle.f2proof");
    std::fs::write(&source, "0 1\n1 2\n0 2\n").unwrap();
    let cli = env!("CARGO_BIN_EXE_dlm");
    let out = Command::new(cli)
        .arg("homology-export")
        .arg(&source)
        .arg(&proof)
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let out = Command::new(cli)
        .arg("homology-check")
        .arg(&proof)
        .arg(&source)
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    assert!(String::from_utf8_lossy(&out.stdout).contains("matches supplied complex"));
    std::fs::write(&source, "0 1 2\n").unwrap();
    let out = Command::new(cli)
        .arg("homology-check")
        .arg(&proof)
        .arg(&source)
        .output()
        .unwrap();
    assert!(!out.status.success());
    assert!(String::from_utf8_lossy(&out.stderr).contains("different chain complex"));
    std::fs::remove_file(&source).unwrap();
    let out = Command::new(cli)
        .arg("homology-check")
        .arg(&proof)
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let text = String::from_utf8(out.stdout).unwrap();
    assert!(text.contains("Betti numbers: [1, 1]"));
    assert!(text.contains("embedded boundary matrices"));
    assert!(text.contains("StaticProof: no"));
    let good = std::fs::read_to_string(&proof).unwrap();
    let bad = good.replace("matrix 3 1 111", "matrix 3 1 100");
    assert_ne!(good, bad);
    std::fs::write(&proof, bad).unwrap();
    let out = Command::new(cli)
        .arg("homology-check")
        .arg(&proof)
        .output()
        .unwrap();
    assert!(!out.status.success());
    assert!(String::from_utf8_lossy(&out.stderr).contains("not a cycle"));
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn homology_check_rejects_wrong_format_and_trailing_data() {
    let path = std::env::temp_dir().join(format!(
        "dlm-homology-invalid-{}.f2proof",
        std::process::id()
    ));
    for text in [
        "DLM-KERNEL-PROOF v1\n",
        "DLM-F2-HOMOLOGY v9\n",
        "DLM-F2-HOMOLOGY v1\nboundaries 0\ngroups 0\nend extra\n",
    ] {
        std::fs::write(&path, text).unwrap();
        let out = Command::new(env!("CARGO_BIN_EXE_dlm"))
            .arg("homology-check")
            .arg(&path)
            .output()
            .unwrap();
        assert!(!out.status.success());
        assert!(String::from_utf8_lossy(&out.stderr).contains("homology witness error:"));
    }
    std::fs::remove_file(path).unwrap();
}
