//! v0.102: the computable-real and byte-buffer examples run and build with
//! identical output; ownership errors are reported by `dlm check`.
use std::path::{Path, PathBuf};
use std::process::Command;

fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

const CREAL: &str = "[1554944255987/1099511627776,388736063997/274877906944]\n[6949403065/4294967296,13898806131/8589934592]\n1\n2\n0\ncomputable real division by zero\ncomputable real square root of a negative value\nUnknownWithinBudget: source precision exceeded";
const BUFFERS: &str = "25\n168\n 68 76 77\n107";
const ELEMENTARY: &str = "pi [205887/65536,3217/1024]\ne [178145/65536,89073/32768]\nlog2 [22713/32768,45427/65536]\nsin1 [27573/32768,55147/65536]\ncos(pi) [-1/1,-32767/32768]\nlog(-1) computable real logarithm of a non-positive value";
const CCOMPLEX: &str = "[-1/1,-1/1] | [0/1,0/1]\n[342273570831115633/162129586585337856,1369094290157713657/648518346341351424] | [-1/805306368,1/805306368]\n[-1/5,-1/5] | [2/5,2/5]\n[321629967803877006253/221360928884514619392,61752956284053005390921/42501298345826806923264]\n0\ncomputable real division by zero";
const CONVERT: &str = "round -5/2: -3 -2 -2 -2\n22/7 to 5 digits: 3.14286\n-1/8 exactly: -0.125\n1/3 exactly: rational is not a finite decimal\nparse -2.50e1: -25/1\npi to 10 digits: 3.1415926536\nfloor(e): 2\nfloor(sqrt2 - sqrt2): UnknownWithinBudget: rounding undecided at precision 64\ndlm1:rat:-3/4\ndlm1:rat:6/8 -> noncanonical\ndlm2:int:1 -> version";

fn run_and_build(example: &str, expected: &str) {
    let source = root().join("examples/valid").join(example);
    let output = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["run", "--trusted-only", "--lib"])
        .arg(root().join("lib"))
        .arg(&source)
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let text = String::from_utf8(output.stdout).unwrap();
    assert_eq!(
        text.split("program output:\n").nth(1).unwrap().trim_end(),
        expected
    );

    let dir = std::env::temp_dir().join(format!("dlm-cli-v102-{}-{example}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let exe = dir.join(if cfg!(windows) { "main.exe" } else { "main" });
    let output = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["build", "--trusted-only", "--lib"])
        .arg(root().join("lib"))
        .arg("-o")
        .arg(&exe)
        .arg(&source)
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let native = Command::new(exe)
        .env("DLM_OWNERSHIP_TRACE", "1")
        .output()
        .unwrap();
    assert!(
        native.status.success(),
        "{}",
        String::from_utf8_lossy(&native.stderr)
    );
    assert_eq!(
        String::from_utf8(native.stdout).unwrap().trim_end(),
        expected
    );
    let trace = String::from_utf8(native.stderr).unwrap();
    assert!(
        trace.ends_with(" released at exit\n") && trace.contains(", 0 released at exit"),
        "{trace}"
    );
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn cli_runs_and_builds_computable_real_example() {
    run_and_build("creal_demo.dlm", CREAL);
}

#[test]
fn cli_runs_and_builds_byte_buffer_example() {
    run_and_build("byte_buffer_demo.dlm", BUFFERS);
}

#[test]
fn cli_runs_and_builds_computable_complex_example() {
    run_and_build("ccomplex_demo.dlm", CCOMPLEX);
}

#[test]
fn cli_check_reports_ownership_errors() {
    let dir = std::env::temp_dir().join(format!("dlm-cli-v102-own-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("bad.dlm");
    std::fs::write(&path, "module bad\ntheory App {\n    fn f(b: ByteBuf) -> Nat =\n        buf_free(b) + buf_len(b)\n}\n").unwrap();
    let output = Command::new(env!("CARGO_BIN_EXE_dlm"))
        .args(["check", "--trusted-only"])
        .arg(&path)
        .output()
        .unwrap();
    assert!(!output.status.success());
    let text = format!(
        "{}{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
    assert!(
        text.contains("E0943 OwnershipError")
            && text.contains("bad.dlm:4:31")
            && text.contains("borrowed after it was moved at line 4"),
        "{text}"
    );
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn cli_runs_and_builds_elementary_function_example() {
    run_and_build("elementary_demo.dlm", ELEMENTARY);
}

#[test]
fn cli_runs_and_builds_conversion_example() {
    run_and_build("convert_demo.dlm", CONVERT);
}
