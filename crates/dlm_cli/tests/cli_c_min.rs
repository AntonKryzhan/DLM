//! v0.101 — C-min through the CLI: standalone replay with an empty axiom
//! context, distinct exit codes for invalid proofs (1), unreadable input (2)
//! and budget exhaustion (3).
use std::path::{Path, PathBuf};
use std::process::Command;

fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn check(path: &Path) -> (i32, String, String) {
    let output = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .arg("proof-check")
        .arg(path)
        .output()
        .unwrap();
    (
        output.status.code().unwrap_or(-1),
        String::from_utf8_lossy(&output.stdout).into_owned(),
        String::from_utf8_lossy(&output.stderr).into_owned(),
    )
}

#[test]
fn standalone_replay_exit_codes_and_empty_axiom_context() {
    let dir = std::env::temp_dir().join(format!("dlm-cli-c-min-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let sample = root().join("examples/proofs/identity.kproof");
    let (code, stdout, _) = check(&sample);
    assert_eq!(code, 0, "{stdout}");
    assert!(stdout.contains("kernel proof: checked") && stdout.contains("axioms: []"));
    assert!(stdout.contains("machine definedness is a separate obligation"));
    let text = std::fs::read_to_string(&sample).unwrap();

    // an axiom step: the standalone check has no axiom context
    let with_axiom = text.replace("goal ", "proof axiom 0\ngoal ");
    let path = dir.join("axiom.kproof");
    std::fs::write(&path, &with_axiom).unwrap();
    let (code, _, stderr) = check(&path);
    assert_eq!(code, 1, "{stderr}");
    assert!(
        stderr.contains("Invalid") && stderr.contains("undeclared axiom"),
        "{stderr}"
    );

    // a non-canonical spelling of the same proof
    let path = dir.join("spaced.kproof");
    std::fs::write(&path, text.replacen("goal ", "goal  ", 1)).unwrap();
    let (code, _, stderr) = check(&path);
    assert_eq!(code, 1, "{stderr}");
    assert!(stderr.contains("Invalid"), "{stderr}");

    // budget exhaustion is reported as unknown, with its own exit code
    let path = dir.join("huge.kproof");
    std::fs::write(&path, "x".repeat((4 << 20) + 10)).unwrap();
    let (code, _, stderr) = check(&path);
    assert_eq!(code, 3, "{stderr}");
    assert!(stderr.contains("UnknownWithinBudget"), "{stderr}");

    // unreadable input
    let (code, _, stderr) = check(&dir.join("missing.kproof"));
    assert_eq!(code, 2, "{stderr}");
    let path = dir.join("binary.kproof");
    std::fs::write(&path, [0xffu8, 0xfe, 0x00]).unwrap();
    assert_eq!(check(&path).0, 2);
    std::fs::remove_dir_all(dir).unwrap();
}
