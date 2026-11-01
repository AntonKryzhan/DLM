#![allow(dead_code)]
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    parse_module, verify_ir, CheckPolicy, Checker, Program, Runtime,
};
use std::path::{Path, PathBuf};
use std::process::Command;

pub fn scratch(name: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("dlm-effects-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    dir
}
pub fn program(body: &str) -> Program {
    let source = format!("module test.effects\ntheory App {{\n{body}\n}}\n");
    Program::single(parse_module(&source).unwrap_or_else(|d| panic!("{d:?}\n{source}")))
}
pub fn library(module: &str, theory: &str, body: &str, dir: &Path) -> Program {
    let source = format!(
        "module test.effects\nimport {module}\ntheory App extends {theory} {{\n{body}\n}}\n"
    );
    let path = dir.join("main.dlm");
    std::fs::write(&path, &source).unwrap();
    let lib = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../lib");
    load_program_with_paths(&path, &[lib]).unwrap_or_else(|d| panic!("{d:?}\n{source}"))
}
pub fn check(p: &Program) {
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(p);
    assert!(report.ok(), "{:?}", report.diagnostics);
}
pub fn build(p: &Program, dir: &Path) -> PathBuf {
    check(p);
    let ir = lower_program(p, CheckPolicy::trusted_only()).unwrap();
    assert!(verify_ir(&ir).is_empty());
    build_c_executable(
        &emit_c(&ir),
        &dir.join("main.c"),
        &dir.join("main"),
        &default_c_compiler(),
    )
    .unwrap()
}
pub fn run_both(p: &Program, dir: &Path) -> Vec<String> {
    let exe = build(p, dir);
    let interpreted = Runtime::new().run_program(p).unwrap().output;
    let out = Command::new(exe).output().unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let native: Vec<String> = String::from_utf8(out.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect();
    assert_eq!(native, interpreted);
    interpreted
}
pub fn rejects(body: &str) {
    let source = format!("module test.effects\ntheory App {{\n{body}\n}}\n");
    let Ok(module) = parse_module(&source) else {
        return;
    };
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "accepted invalid program:\n{source}");
}
