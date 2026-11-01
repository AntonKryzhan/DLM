//! v0.125 — `loop` functions (tail loops in constant stack), `enum` declarations with
//! exhaustive variant matches, file positioning / buffering / path and directory
//! operations, `std.files`. Interpreter (with stdin) and native C agree.
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program, parse_module,
    CheckPolicy, Checker, Program, Runtime,
};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

/// Runs `source` (with `{D}` replaced by a fresh work directory) in the
/// interpreter and natively, each from a clean work directory, and returns the
/// native output lines with the directory written back as `{D}`.
fn run_both(name: &str, source: &str, stdin: &str) -> Vec<String> {
    let dir = std::env::temp_dir().join(format!("dlm-v125-{}-{name}", std::process::id()));
    let work = dir.join("work");
    let d = work.display().to_string();
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source.replace("{D}", &d)).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    std::fs::remove_dir_all(&work).ok();
    let interpreted = Runtime::with_stdin(stdin).run_program(&p).unwrap().output;
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(&emit_c(&ir), &dir.join("main.c"), &dir.join(if cfg!(windows) { "main.exe" } else { "main" }), &default_c_compiler()).unwrap();
    std::fs::remove_dir_all(&work).ok();
    let mut child = Command::new(exe).stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::piped()).spawn().unwrap();
    child.stdin.take().unwrap().write_all(stdin.as_bytes()).unwrap();
    let out = child.wait_with_output().unwrap();
    assert!(out.status.success(), "{}", String::from_utf8_lossy(&out.stderr));
    let native: Vec<String> = String::from_utf8(out.stdout).unwrap().lines().map(|l| l.replace(&d, "{D}")).collect();
    let interpreted: Vec<String> = interpreted.join("\n").lines().map(|l| l.replace(&d, "{D}")).collect();
    assert_eq!(interpreted, native);
    std::fs::remove_dir_all(dir).ok();
    native
}

const FILES: &str = r#"    fn rn(r: Result<Nat, Text>) -> Text = match r { ok(n) => nat_to_text(n), err(e) => e }
    fn rl(r: Result<List<Text>, Text>) -> Text = match r { ok(xs) => join(xs, ","), err(e) => e }
    fn bt(b: Bool) -> Text = if_then_else(b, "true", "false")
    // long-running loop: count to n with constant stack
    fn count(i: Nat, n: Nat, acc: Nat) -> Nat loop = if_then_else(nat_eq(i, n), acc, count(i + 1, n, acc + i))
    // read commands until `quit` or end of input
    fn repl(seen: Nat) -> Nat loop = match read_line() { none => seen, some(l) =>
        if_then_else(text_eq(l, "quit"), seen, let u = print_line(join(list("echo", l), " ")) in repl(seen + 1)) }
    fn copy_lines(src: File, dst: File, n: Nat) -> Nat loop = match file_read_line(src) { none => let a = file_close(src) in let b = file_close(dst) in n,
        some(l) => let w = file_write(dst, join(list(l, ""), "\n")) in copy_lines(src, dst, n + 1) }
    fn rd(f: &File) -> Text = match file_read_line(f) { none => "none", some(l) => l }
    fn t_read() -> Text = let f = file_open("{D}/t125dir/a.txt", "r") in let s1 = file_seek(f, 6) in let a = rd(f) in let p = file_tell(f) in let z = file_size(f) in
        let s2 = file_seek(f, 0) in let b = rd(f) in let t = bt(file_truncate(f, 2)) in let e = file_error(f) in let c = file_close(f) in
        join(list(a, nat_to_text(p), nat_to_text(z), b, t, e, rn(c)), "|")
    fn t_append() -> Text = let g = file_open("{D}/t125dir/out.txt", "a") in let p0 = file_tell(g) in let b1 = file_set_buffered(g, true) in let w1 = file_write(g, "delta\n") in
        let p1 = file_tell(g) in let z1 = file_size(g) in let t = bt(file_truncate(g, 6)) in let z2 = file_size(g) in let c = file_close(g) in
        join(list(nat_to_text(p0), nat_to_text(p1), nat_to_text(z1), t, nat_to_text(z2), rn(c)), "|")
    fn t_all() -> Text = let h = file_open("{D}/t125dir/out.txt", "r") in let x = file_read_all(h) in let c = file_close(h) in x
    print_line(nat_to_text(count(0, 300000, 0)))
    print_line(nat_to_text(repl(0)))
    print_line(rn(file_make_dir("{D}/t125dir/sub/deep")))
    print_line(rn(file_write_atomic("{D}/t125dir/a.txt", "alpha\nbeta\ngamma\n")))
    print_line(rn(file_write_atomic("{D}/t125dir/sub/b.txt", "bee")))
    print_line(rn(file_copy("{D}/t125dir/a.txt", "{D}/t125dir/sub/deep/c.txt")))
    print_line(rl(file_list_dir("{D}/t125dir")))
    print_line(rl(file_walk("{D}/t125dir")))
    print_line(bt(file_exists("{D}/t125dir/a.txt")))
    print_line(bt(file_is_dir("{D}/t125dir/sub")))
    print_line(bt(file_is_dir("{D}/t125dir/a.txt")))
    print_line(rn(file_size_of("{D}/t125dir/a.txt")))
    print_line(rn(file_size_of("{D}/t125dir/missing")))
    print_line(rl(file_list_dir("{D}/t125dir/missing")))
    print_line(rn(file_rename("{D}/t125dir/sub/b.txt", "{D}/t125dir/b2.txt")))
    print_line(rn(file_remove_dir("{D}/t125dir/sub")))
    print_line(nat_to_text(copy_lines(file_open("{D}/t125dir/a.txt", "r"), file_open("{D}/t125dir/out.txt", "w"), 0)))
    print_line(t_read())
    print_line(t_append())
    print_line(rn(file_size_of("{D}/t125dir/out.txt")))
    print_line(t_all())
    print_line(match file_mtime("{D}/t125dir/a.txt") { ok(t) => bt(t > 1700000000), err(e) => e })
    print_line(rn(file_remove_dir("{D}/t125dir/sub/deep")))
"#;

const ENUMS: &str = r#"    enum Shape {
        Circle(r: Nat),
        Rect(w: Nat, h: Nat),
        Empty
    }
    enum Cmd { Push(n: Nat), Pop, Quit }
    fn area(s: Shape) -> Nat = match s { Circle(r) => mul(3, mul(r, r)), Rect(w, h) => mul(w, h), Empty => 0 }
    fn name(s: Shape) -> Text = match s { Circle(_) => "circle", _ => "other" }
    fn parse(t: Text) -> Cmd = if_then_else(text_eq(t, "pop"), Pop(), if_then_else(text_eq(t, "quit"), Quit(), Push(unwrap_or(text_to_nat(t), 0))))
    // stack machine driven by stdin until Quit or end of input
    fn run(stack: List<Nat>) -> Nat loop = match read_line() { none => length(stack), some(l) => match parse(l) {
        Push(n) => run(cons(n, stack)),
        Pop => let u = print_line(match stack { [] => "empty", cons(h, t) => nat_to_text(h) }) in run(if_then_else(is_empty(stack), stack, tail(stack))),
        Quit => length(stack) } }
    print_line(nat_to_text(area(Circle(2))))
    print_line(nat_to_text(area(Rect(3, 4))))
    print_line(nat_to_text(area(Empty())))
    print_line(name(Rect(1, 1)))
    print_line(name(Circle(1)))
    print_line(nat_to_text(run(empty_list(Nat))))
"#;

const LIBRARY: &str = r#"    fn rn(r: Result<Nat, Text>) -> Text = match r { ok(n) => nat_to_text(n), err(e) => e }
    fn rl(r: Result<List<Text>, Text>) -> Text = match r { ok(xs) => join(xs, ","), err(e) => e }
    print_line(rn(file_make_dir("{D}/lt/src/a/b")))
    print_line(rn(write_lines("{D}/lt/src/x.txt", list("one", "two"))))
    print_line(rn(write_text("{D}/lt/src/a/b/y.dlm", "fn")))
    print_line(rn(append_line("{D}/lt/src/x.txt", "three")))
    print_line(rl(read_lines("{D}/lt/src/x.txt")))
    print_line(rn(copy_tree("{D}/lt/src", "{D}/lt/dst")))
    print_line(rl(file_walk("{D}/lt/dst")))
    print_line(rn(tree_size("{D}/lt/dst")))
    print_line(rl(find_files("{D}/lt", ".dlm")))
    print_line(parent_dir("a/b/c.txt"))
    print_line(base_name("a/b/c.txt"))
    print_line(join(stdin_lines(), "+"))
    print_line(rl(read_lines("{D}/lt/missing")))
"#;

#[test]
fn loops_and_file_operations_agree_between_interpreter_and_native() {
    let out = run_both("files", &format!("module test.v125
theory App {{
{FILES}}}
"), "hello
world
quit
ignored
");
    let want: &[&str] = &[
        "44999850000",
        "echo hello",
        "echo world",
        "2",
        "0",
        "17",
        "3",
        "17",
        "a.txt,sub",
        "a.txt,sub/b.txt,sub/deep/c.txt",
        "true",
        "true",
        "false",
        "17",
        "cannot stat `{D}/t125dir/missing` (os error 2)",
        "cannot list directory `{D}/t125dir/missing` (os error 2)",
        "0",
        "cannot remove directory `{D}/t125dir/sub` (os error 39)",
        "3",
        "beta|11|17|alpha|false|file `{D}/t125dir/a.txt` is not open for writing|file `{D}/t125dir/a.txt` is not open for writing",
        "17|23|23|true|6|6",
        "6",
        "alpha",
        "",
        "true",
        "cannot remove directory `{D}/t125dir/sub/deep` (os error 39)",
    ];
    assert_eq!(out, want);
}

#[test]
fn enums_and_stdin_loops_agree_between_interpreter_and_native() {
    let out = run_both("enums", &format!("module test.enums
theory App {{
{ENUMS}}}
"), "5
7
pop
pop
pop
9
quit
1
");
    let want: &[&str] = &[
        "12",
        "12",
        "0",
        "other",
        "circle",
        "7",
        "5",
        "empty",
        "1",
    ];
    assert_eq!(out, want);
}

#[test]
fn std_files_agrees_between_interpreter_and_native() {
    let out = run_both("library", &format!("module test.files_lib
import std.files
theory App extends StdFiles {{
{LIBRARY}}}
"), "p
q
");
    let want: &[&str] = &[
        "0",
        "8",
        "2",
        "6",
        "one,two,three",
        "16",
        "a/b/y.dlm,x.txt",
        "16",
        "dst/a/b/y.dlm,src/a/b/y.dlm",
        "a/b",
        "c.txt",
        "p+q",
        "cannot open file `{D}/lt/missing`",
    ];
    assert_eq!(out, want);
}

fn diagnostics(items: &str) -> String {
    let source = format!("module bad
theory T {{
{items}
}}
");
    match parse_module(&source) {
        Err(e) => format!("{e:?}"),
        Ok(module) => {
            let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&Program::single(module));
            report.diagnostics.iter().map(|d| d.to_string()).collect::<Vec<_>>().join("
")
        }
    }
}

#[test]
fn loop_and_enum_misuse_is_rejected() {
    let cases = [
        ("    fn f(n: Nat) -> Nat loop = if_then_else(nat_eq(n, 0), 0, f(n + 1) + 1)", "tail position"),
        ("    fn f(n: Nat) -> Nat loop = unwrap_or(some(f(n)), 0)", "tail position"),
        ("    fn f(n: Nat) -> Fn<Nat,Nat> loop = f", "as a value"),
        ("    fn f(n: Nat) -> Nat loop = let g = fun(k: Nat, f(k)) in 0", "lambda"),
        ("    fn f(n: Nat) -> Nat loop = f(n)
    theorem t: nat_eq(f(1), 1)", ""),
        ("    enum E { A(n: Nat), B, C }
    fn g(e: E) -> Nat = match e { A(n) => n, B => 1 }", "not exhaustive: no arm for "),
        ("    enum E { A(n: Nat), B }
    fn g(e: E) -> Nat = match e { A(n) => n, D => 1 }", "no variant "),
        ("    enum E { A(n: Nat), A }", "twice"),
        ("    enum E { a }", "invalid variant"),
    ];
    for (items, needle) in cases {
        let text = diagnostics(items);
        assert!(!text.is_empty(), "accepted: {items}");
        assert!(text.contains(needle), "{items}
{text}");
    }
    assert!(diagnostics("    enum E { A(n: Nat), B, C }
    fn g(e: E) -> Nat = match e { A(n) => n, _ => 1 }
    fn h(n: Nat) -> Nat loop = if_then_else(nat_eq(n, 0), 0, h(pred(n)))").is_empty());
    let deep = Runtime::new().run_program(&Program::single(parse_module("module deep
theory T {
    fn h(n: Nat, a: Nat) -> Nat loop = if_then_else(nat_eq(n, 0), a, h(pred(n), a + n))
    print_line(nat_to_text(h(2000000, 0)))
}
").unwrap())).unwrap();
    assert_eq!(deep.output, vec!["2000001000000".to_string()]);
}
