//! v0.80 — the CLI loads imported modules (`--lib`, `DLM_PATH`).

use std::process::Command;

const ROOT: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");

fn dlm(args: &[&str], dlm_path: Option<&str>) -> (bool, String, String) {
    let mut command = Command::new(env!("CARGO_BIN_EXE_dlm"));
    command.args(args).env_remove("DLM_PATH");
    if let Some(path) = dlm_path {
        command.env("DLM_PATH", path);
    }
    let output = command.output().expect("run dlm");
    (output.status.success(), String::from_utf8_lossy(&output.stdout).into_owned(), String::from_utf8_lossy(&output.stderr).into_owned())
}

#[test]
fn imports_are_found_with_lib_or_dlm_path() {
    let example = format!("{ROOT}/examples/valid/std_library_usage.dlm");
    let lib = format!("{ROOT}/lib");
    let (ok, _, stderr) = dlm(&["check", &example], None);
    assert!(!ok, "without a search path std.list is not found");
    assert!(stderr.contains("E0917 ModuleImportError") && stderr.contains("module 'std.list'"), "{stderr}");

    let (ok, stdout, stderr) = dlm(&["certify", "--lib", &lib, &example], None);
    assert!(ok, "{stderr}");
    assert!(stdout.contains("certificates: 3"), "{stdout}");
    assert!(stdout.contains("lemma: inc_all_sum"), "{stdout}");

    let (ok, stdout, stderr) = dlm(&["run", &example], Some(&lib));
    assert!(ok, "{stderr}");
    assert!(stdout.contains("module: examples.std_library_usage"), "{stdout}");
}

#[test]
fn private_names_and_cycles_are_reported() {
    let lib = format!("{ROOT}/lib");
    let private = format!("{ROOT}/examples/invalid/private_declaration_not_visible.dlm");
    let (ok, _, stderr) = dlm(&["check", "--lib", &lib, &private], None);
    assert!(!ok);
    assert!(stderr.contains("'add_acc' is private to module 'std.nat'"), "{stderr}");
    let cycle = format!("{ROOT}/examples/invalid/import_cycle_a.dlm");
    let (ok, _, stderr) = dlm(&["check", &cycle], None);
    assert!(!ok);
    assert!(stderr.contains("import cycle: examples.import_cycle_a -> examples.import_cycle_b -> examples.import_cycle_a"), "{stderr}");
    let (ok, _, stderr) = dlm(&["check", "--lib"], None);
    assert!(!ok && stderr.contains("--lib expects a directory"), "{stderr}");
}
