use std::io::Write;
use std::process::{Command, Stdio};

#[test]
fn interactive_loop_streams_only_program_output() {
    let dir = std::env::temp_dir().join(format!("dlm-interactive125-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, "module live\ntheory T {\nfn echo() -> Nat loop = match read_line() { none => 0, some(s) => let u = print_line(s) in echo() }\nlet main = echo()\n}\n").unwrap();
    let mut child = Command::new(env!("CARGO_BIN_EXE_dlm")).args(["run", "--interactive"]).arg(&path)
        .stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::piped()).spawn().unwrap();
    child.stdin.take().unwrap().write_all("один\nдва\n".as_bytes()).unwrap();
    let output = child.wait_with_output().unwrap();
    assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
    assert_eq!(String::from_utf8(output.stdout).unwrap(), "один\nдва\n");
    std::fs::remove_dir_all(dir).unwrap();
}
