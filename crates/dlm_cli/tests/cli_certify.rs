use std::process::Command;

#[test]
fn certify_prints_checked_and_axiom_certificates_with_evidence() {
    let example = concat!(env!("CARGO_MANIFEST_DIR"), "/../../examples/valid/ordinary_theorems.dlm");
    let output = Command::new(env!("CARGO_BIN_EXE_dlm")).args(["certify", example]).output().expect("run dlm certify");
    assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
    let stdout = String::from_utf8(output.stdout).expect("utf8");
    assert!(stdout.contains("certificates: 6"), "{stdout}");
    assert_eq!(stdout.matches("DLM-PROOF-CERTIFICATE v1").count(), 6);
    assert!(stdout.contains("theorem: gauss_below_200"));
    assert!(stdout.contains("evidence: proof:exhaustion:below=200:cases=200:theorem=gauss_below_200"));
    assert!(stdout.contains("status: AxiomAdmitted"));
    assert!(stdout.contains("axiom_tainted: true"));
}

#[test]
fn certify_is_blocked_by_policy() {
    let example = concat!(env!("CARGO_MANIFEST_DIR"), "/../../examples/valid/ordinary_theorems.dlm");
    let output = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["certify", "--trusted-only", example])
        .output()
        .expect("run dlm certify");
    assert!(!output.status.success(), "an axiom must block certification under --trusted-only");
}
