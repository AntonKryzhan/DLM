//! v0.81 — `dlm audit` and `dlm project`.

use std::process::Command;

const ROOT: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");

fn dlm(args: &[&str]) -> (bool, String, String) {
    let output = Command::new(env!("CARGO_BIN_EXE_dlm")).args(args).env_remove("DLM_PATH").output().expect("run dlm");
    (output.status.success(), String::from_utf8_lossy(&output.stdout).into_owned(), String::from_utf8_lossy(&output.stderr).into_owned())
}

#[test]
fn audit_prints_the_trusted_base() {
    let lib = format!("{ROOT}/lib");
    let example = format!("{ROOT}/examples/valid/std_library_usage.dlm");
    let (ok, stdout, stderr) = dlm(&["audit", "--lib", &lib, &example]);
    assert!(ok, "{stderr}");
    assert!(stdout.starts_with("DLM-AUDIT v1\n"), "{stdout}");
    assert!(stdout.contains("status: closed"), "{stdout}");
    assert!(stdout.contains("imports: std.nat, std.list"), "{stdout}");
    assert!(stdout.contains("fingerprint: fnv64:"), "{stdout}");

    let theorems = format!("{ROOT}/examples/valid/ordinary_theorems.dlm");
    let (ok, stdout, _) = dlm(&["audit", &theorems]);
    assert!(ok);
    assert!(stdout.contains("status: axiom-dependent"), "{stdout}");
    assert!(stdout.contains("open goal Arithmetic.double_monus"), "{stdout}");
    // Under --trusted-only the axiom blocks the audit like any check.
    let (ok, _, stderr) = dlm(&["audit", "--trusted-only", &theorems]);
    assert!(!ok && stderr.contains("DLM audit blocked by check errors"), "{stderr}");
}

#[test]
fn project_checks_the_repository() {
    let (ok, stdout, stderr) = dlm(&["project", ROOT]);
    assert!(ok, "{stdout}\n{stderr}");
    assert!(stdout.starts_with("DLM project: DLM\n"), "{stdout}");
    assert!(stdout.contains("failed: 0"), "{stdout}");
    assert!(stdout.contains("std/list.dlm  module=std.list status=closed"), "{stdout}");
    let (ok, stdout, _) = dlm(&["project", "--audit", &format!("{ROOT}/dlm.project")]);
    assert!(ok);
    assert!(stdout.matches("DLM-AUDIT v1").count() >= 60, "one audit per root");
    let (ok, _, stderr) = dlm(&["project", "/nonexistent-dlm-project"]);
    assert!(!ok && stderr.contains("cannot read"), "{stderr}");
}
