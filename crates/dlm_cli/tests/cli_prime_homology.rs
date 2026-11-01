use std::process::Command;
fn root() -> std::path::PathBuf {
    std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}
#[test]
fn explicit_prime_changes_projective_plane_and_preserves_oriented_examples() {
    for (name, p, expected) in [
        ("projective_plane", 2, "H_1: dimension 1"),
        ("projective_plane", 3, "H_1: dimension 0"),
        ("sphere", 5, "H_2: dimension 1"),
        ("torus", 251, "H_1: dimension 2"),
    ] {
        let out = Command::new(env!("CARGO_BIN_EXE_dlm"))
            .args(["homology", "--prime", &p.to_string()])
            .arg(root().join(format!("examples/math/{name}.simp")))
            .output()
            .unwrap();
        assert!(
            out.status.success(),
            "{}",
            String::from_utf8_lossy(&out.stderr)
        );
        let text = String::from_utf8(out.stdout).unwrap();
        assert!(text.contains(expected));
        assert!(text.contains(&format!("coefficients: F{p}")));
        assert!(text.contains("runtime-checked"));
        assert!(text.contains("StaticProof: no"));
    }
}
#[test]
fn explicit_prime_rejects_invalid_composite_and_out_of_profile_moduli() {
    for p in ["0", "1", "4", "9", "252", "257", "65536", "-3", "nope"] {
        let out = Command::new(env!("CARGO_BIN_EXE_dlm"))
            .args(["homology", "--prime", p])
            .arg(root().join("examples/math/circle.simp"))
            .output()
            .unwrap();
        assert!(!out.status.success());
        assert!(String::from_utf8_lossy(&out.stderr).contains("homology error:"));
    }
}
