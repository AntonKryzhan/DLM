//! v0.83 — running programs: stdin, arguments, `main` and exit status.

use std::process::Command;

const ROOT: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");

#[test]
fn word_count_runs_with_stdin_and_arguments() {
    let example = format!("{ROOT}/examples/valid/word_count.dlm");
    let output = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["run", &example, "--stdin", "a b\nc", "--", "Ada", "ignored"])
        .output()
        .expect("run dlm");
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(output.status.success(), "{stdout}\n{}", String::from_utf8_lossy(&output.stderr));
    for line in ["hello, Ada", "lines: 2", "words: 3", "chars: 5", "main: 0"] {
        assert!(stdout.contains(line), "missing `{line}` in {stdout}");
    }
}

#[test]
fn main_result_is_the_exit_status_and_stdin_can_come_from_a_file() {
    let dir = std::env::temp_dir().join(format!("dlm_cli_programs_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let program = dir.join("status.dlm");
    std::fs::write(&program, "module status\ntheory S {\n    fn main() -> Nat = length(lines(read_all()))\n}\n").unwrap();
    let input = dir.join("input.txt");
    std::fs::write(&input, "1\n2\n3\n").unwrap();
    let output = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["run", program.to_str().unwrap(), "--stdin-file", input.to_str().unwrap()])
        .output()
        .expect("run dlm");
    assert_eq!(output.status.code(), Some(3), "{}", String::from_utf8_lossy(&output.stdout));
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn build_produces_a_native_executable() {
    let dir = std::env::temp_dir().join(format!("dlm_cli_build_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let exe = dir.join("wc");
    let example = format!("{ROOT}/examples/valid/word_count.dlm");
    let output = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["build", &example, "-o", exe.to_str().unwrap(), "--emit-ir"])
        .output()
        .expect("run dlm build");
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(output.status.success(), "{stdout}\n{}", String::from_utf8_lossy(&output.stderr));
    assert!(stdout.contains("DLM-IR v3") && stdout.contains("DLM build:"), "{stdout}");
    assert!(dir.join("wc.c").is_file());
    let mut child = Command::new(&exe)
        .arg("Ada")
        .stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::piped())
        .spawn()
        .unwrap();
    use std::io::Write;
    child.stdin.take().unwrap().write_all(b"a b\nc").unwrap();
    let run = child.wait_with_output().unwrap();
    assert_eq!(String::from_utf8_lossy(&run.stdout), "hello, Ada\nlines: 2\nwords: 3\nchars: 5\n");
    assert_eq!(run.status.code(), Some(0));

    // Constructs without a backend are reported, not miscompiled.
    let unsupported = dir.join("u.dlm");
    std::fs::write(&unsupported, "module u\ntheory U {\n    let a = axiom_true()\n}\n").unwrap();
    let output = Command::new(env!("CARGO_BIN_EXE_dlm")).args(["build", unsupported.to_str().unwrap()]).output().unwrap();
    assert!(!output.status.success());
    assert!(String::from_utf8_lossy(&output.stderr).contains("E0950 CompileError"));
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn build_checks_machine_arithmetic_before_emission() {
    let dir = std::env::temp_dir().join(format!("dlm_cli_arithmetic_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("checked.dlm");
    let output_path = dir.join("checked");
    std::fs::write(&path, "module m\ntheory T {\n    fn inc(n: Nat) -> Nat = n + 1\n}\n").unwrap();
    let output = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["build", path.to_str().unwrap(), "-o", output_path.to_str().unwrap(), "--c-only", "--require-no-overflow"])
        .output().unwrap();
    assert!(!output.status.success());
    assert!(String::from_utf8_lossy(&output.stdout).contains("open: T.inc"));
    assert!(!dir.join("checked.c").exists());
    // Reporting alone keeps legacy builds available and runtime guards on.
    let output = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["build", path.to_str().unwrap(), "-o", output_path.to_str().unwrap(), "--c-only", "--check-overflow"])
        .output().unwrap();
    assert!(output.status.success());
    let c = std::fs::read_to_string(dir.join("checked.c")).unwrap();
    assert!(c.contains("addition result overflows"));
    std::fs::write(&path, "module m\ntheory T {\n    fn inc(n: Nat) -> Nat = if_then_else(n > 100, n, n + 1)\n}\n").unwrap();
    let output = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["build", path.to_str().unwrap(), "-o", output_path.to_str().unwrap(), "--c-only", "--trusted-only", "--require-no-overflow"])
        .output().unwrap();
    assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
    assert!(String::from_utf8_lossy(&output.stdout).contains("1/1 obligations proved"));
    std::fs::remove_dir_all(dir).ok();
}

/// v0.87: `dlm run` reports the output printed before a runtime error
/// (law 30), and `dlm contracts` prints independent per-function statuses.
#[test]
fn run_shows_output_before_errors_and_contracts_are_reported() {
    let dir = std::env::temp_dir().join(format!("dlm_cli_contracts_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("p.dlm");
    std::fs::write(
        &path,
        "module p\ntheory T {\n    fn sq(n: Nat) -> Nat = mul(n, n)\n    theorem sq_comm(n: Nat): nat_eq(sq(n), mul(n, n)) by normalize\n    print_decimal(sq(3))\n    print_decimal(sq(sq(4294967296)))\n}\n",
    )
    .unwrap();
    let output = Command::new(env!("CARGO_BIN_EXE_dlm")).args(["run", path.to_str().unwrap()]).output().unwrap();
    let (stdout, stderr) = (String::from_utf8_lossy(&output.stdout), String::from_utf8_lossy(&output.stderr));
    assert_eq!(output.status.code(), Some(1));
    assert!(stdout.contains("program output (before the runtime error):\n9\n"), "{stdout}");
    assert!(stderr.contains("multiplication result overflows"), "{stderr}");
    let output = Command::new(env!("CARGO_BIN_EXE_dlm")).args(["contracts", path.to_str().unwrap()]).output().unwrap();
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(output.status.success(), "{stdout}");
    for line in [
        "fn T.sq (Nat) -> Nat",
        "  effects: none (pure)",
        "  termination: non-recursive",
        "  machine arithmetic: open: 1 of 1 obligation(s) [mul(%0, %0)]; runtime checks stay",
        "  failures: arithmetic-overflow",
        "  specification (mathematical): theorem sq_comm",
    ] {
        assert!(stdout.contains(line), "missing `{line}` in {stdout}");
    }
    std::fs::remove_dir_all(&dir).ok();
}
