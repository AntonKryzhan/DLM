use std::path::PathBuf;
use std::process::Command;

fn directory(case: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("dlm_cli_source_{}_{case}", std::process::id()));
    std::fs::create_dir_all(dir.join("lib")).unwrap();
    dir
}

#[test]
fn check_reports_imported_physical_location_and_root_location() {
    let dir = directory("check");
    let root = dir.join("app.dlm");
    let library = dir.join("lib/base.dlm");
    std::fs::write(&root, "module app\nimport lib.base\ntheory App {\n    let a = unknown_root\n}\n").unwrap();
    std::fs::write(&library, "module lib.base\ntheory Base {\n    fn f() -> Nat =\n        1 + unknown_library\n}\n").unwrap();
    let output = Command::new(env!("CARGO_BIN_EXE_dlm")).arg("check").arg(&root).output().unwrap();
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert_eq!(output.status.code(), Some(1));
    assert!(stderr.contains(&format!("{}:4:13:", library.display())), "{stderr}");
    assert!(stderr.contains(&format!("{}:4:13:", root.display())), "{stderr}");
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn standalone_parse_and_build_errors_include_the_exact_file_position() {
    let dir = directory("standalone");
    let root = dir.join("app.dlm");
    std::fs::write(&root, "module app\ntheory App {\n    let a = concat(\n        \"é\",\n        @@@)\n}\n").unwrap();
    let output = Command::new(env!("CARGO_BIN_EXE_dlm")).arg("check").arg(&root).output().unwrap();
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert_eq!(output.status.code(), Some(1));
    assert!(stderr.contains(&format!("{}:5:9:", root.display())), "{stderr}");
    std::fs::write(&root, "module app\ntheory App {\n    let a =\n        340282366920938463463374607431768211456\n}\n").unwrap();
    let output = Command::new(env!("CARGO_BIN_EXE_dlm")).arg("build").arg(&root).arg("--c-only").output().unwrap();
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert_eq!(output.status.code(), Some(1));
    assert!(stderr.contains("E0950 CompileError"), "{stderr}");
    assert!(stderr.contains(&format!("{}:4:9:", root.display())), "{stderr}");
    assert!(!root.with_extension("c").exists());
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn emitted_ir_keeps_source_files_and_native_behavior() {
    let dir = directory("build");
    let root = dir.join("app.dlm");
    let library = dir.join("lib/base.dlm");
    let executable = dir.join("app");
    std::fs::write(&root, "module app\nimport lib.base\ntheory App extends Base {\n    fn main() -> Nat = let _ = print_decimal(one() + 41) in 0\n}\n").unwrap();
    std::fs::write(&library, "module lib.base\ntheory Base {\n    fn one() -> Nat = 1\n}\n").unwrap();
    let output = Command::new(env!("CARGO_BIN_EXE_dlm")).arg("build").arg(&root).args(["--emit-ir", "-o"]).arg(&executable).output().unwrap();
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(output.status.success(), "{stdout}\n{}", String::from_utf8_lossy(&output.stderr));
    assert!(stdout.contains("source file#0:") && stdout.contains("source file#1:"), "{stdout}");
    assert!(stdout.contains("base.dlm") && stdout.contains("at file#1 line 3"), "{stdout}");
    let native = Command::new(executable).output().unwrap();
    assert_eq!(native.status.code(), Some(0));
    assert_eq!(native.stdout, b"42\n");
    std::fs::remove_dir_all(dir).unwrap();
}
