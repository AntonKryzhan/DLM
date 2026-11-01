use std::process::Command;

#[test]
fn export_and_replay_work_without_rechecking_the_source() {
    let dir = std::env::temp_dir().join(format!("dlm-proof-replay-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let source = dir.join("proof.dlm");
    std::fs::write(&source, "module m\ntheory T {\n    theorem identity(n: Nat): nat_eq(mul(1, n + 0), n) by normalize\n    fn f(n: Nat) -> Nat = n\n    theorem legacy(n: Nat): nat_eq(f(n), n) by normalize\n}\n").unwrap();
    let cli = env!("CARGO_BIN_EXE_dlm");
    let output = Command::new(cli)
        .arg("proof-export")
        .arg("--trusted-only")
        .arg(&source)
        .arg("T.identity")
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert!(output.stdout.starts_with(b"DLM-KERNEL-PROOF v1\n"));
    let artifact = dir.join("identity.kproof");
    std::fs::write(&artifact, &output.stdout).unwrap();
    let legacy = Command::new(cli)
        .arg("proof-export")
        .arg(&source)
        .arg("T.legacy")
        .output()
        .unwrap();
    assert!(!legacy.status.success());
    assert!(String::from_utf8_lossy(&legacy.stderr).contains("no independently replayable proof"));
    std::fs::remove_file(source).unwrap();
    let replay = Command::new(cli)
        .arg("proof-check")
        .arg(&artifact)
        .output()
        .unwrap();
    assert!(
        replay.status.success(),
        "{}",
        String::from_utf8_lossy(&replay.stderr)
    );
    assert!(String::from_utf8_lossy(&replay.stdout).contains("kernel proof: checked"));
    let mut proof = dlm_core::proof_kernel::KernelProof::from_text(
        &String::from_utf8(output.stdout).unwrap(),
        Default::default(),
    )
    .unwrap();
    proof.steps[proof.root] = dlm_core::proof_kernel::KernelRule::Refl(proof.goal.0);
    std::fs::write(&artifact, proof.to_text()).unwrap();
    let replay = Command::new(cli)
        .arg("proof-check")
        .arg(&artifact)
        .output()
        .unwrap();
    assert!(!replay.status.success());
    assert!(String::from_utf8_lossy(&replay.stderr).contains("Invalid"));
    std::fs::remove_dir_all(dir).unwrap();
}
