use std::process::Command;

#[test]
fn imported_nominal_callbacks_keep_ids_and_match_native_execution() {
    let dir = std::env::temp_dir().join(format!("dlm_cli_decl_ids_{}", std::process::id()));
    std::fs::create_dir_all(dir.join("lib")).unwrap();
    let root = dir.join("app.dlm");
    let base = dir.join("lib/base.dlm");
    let exe = dir.join("app");
    std::fs::write(&base, "module lib.base\ntheory Base {\n    record P(n: Nat)\n    private fn helper(n: Nat) -> Nat = n + 1\n    fn make(n: Nat) -> P = P(helper(n))\n    fn step(acc: P, n: Nat) -> P = P(field(acc, n) + n)\n}\n").unwrap();
    std::fs::write(&root, "module app\nimport lib.base\ntheory App extends Base {\n    fn main() -> Nat = let p = fold(list(1, 2, 3), P(0), step) in let _ = print_decimal(field(unwrap_or(head(map(list(p), fun(x: P, make(field(x, n))))), P(0)), n)) in 0\n}\n").unwrap();
    let compiled = Command::new(env!("CARGO_BIN_EXE_dlm")).arg("build").arg(&root).args(["--emit-ir", "-o"]).arg(&exe).output().unwrap();
    let stdout = String::from_utf8_lossy(&compiled.stdout);
    assert!(compiled.status.success(), "{stdout}\n{}", String::from_utf8_lossy(&compiled.stderr));
    assert!(stdout.contains("declaration decl#") && stdout.contains("lib.base::Base.P"), "{stdout}");
    let native = Command::new(exe).output().unwrap();
    assert_eq!(native.status.code(), Some(0));
    assert_eq!(native.stdout, b"7\n");
    let interpreted = Command::new(env!("CARGO_BIN_EXE_dlm")).arg("run").arg(&root).output().unwrap();
    assert_eq!(interpreted.status.code(), Some(0));
    assert!(String::from_utf8_lossy(&interpreted.stdout).contains("\n7\n"));
    std::fs::remove_dir_all(dir).unwrap();
}
