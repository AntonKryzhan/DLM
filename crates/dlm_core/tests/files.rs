//! v0.109 — stage D: external file handles. `File` is a linear resource:
//! closed with `file_close` (which reports every failure), moved or returned
//! on every path; failures are sticky values, never runtime errors; the
//! interpreter and native C agree on contents, messages and results; open
//! handles are closed at exit after a runtime error (`docs/FILES.md`).
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, lower_program, parse_module, CheckPolicy,
    Checker, DiagnosticKind, Program, Runtime,
};
use std::path::PathBuf;
use std::process::{Command, Stdio};

fn program(body: &str) -> Program {
    let source = format!("module test.files\ntheory App {{\n{body}\n}}\n");
    Program::single(parse_module(&source).unwrap_or_else(|d| panic!("{d:?}\n{source}")))
}

fn scratch(name: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("dlm-files-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

fn native(p: &Program, dir: &std::path::Path) -> (String, String, i32) {
    let ir = lower_program(p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(
        &emit_c(&ir),
        &dir.join("main.c"),
        &dir.join("main"),
        &default_c_compiler(),
    )
    .unwrap();
    let out = Command::new(exe)
        .env("DLM_FILE_TRACE", "1")
        .stdin(Stdio::null())
        .output()
        .unwrap();
    (
        String::from_utf8(out.stdout).unwrap(),
        String::from_utf8(out.stderr).unwrap(),
        out.status.code().unwrap_or(-1),
    )
}

const HELPERS: &str = r#"    fn status(r: Result<Nat, Text>) -> Text = match r { ok(n) => concat("ok ", nat_to_text(n)), err(e) => concat("err ", e) }
    fn write_lines(f: &File, n: Nat) -> Nat decreasing n fuel 100 =
        if_then_else(nat_eq(n, 0), 0, let w = file_write(f, concat("line ", concat(nat_to_text(n), "\n"))) in 1 + write_lines(f, pred(n)))
    fn count(f: &File, n: Nat) -> Nat decreasing n fuel 100 =
        if_then_else(nat_eq(n, 0), 0, if_then_else(is_some(file_read_line(f)), 1 + count(f, pred(n)), 0))
    fn make(path: Text) -> File = let f = file_open(path, "w") in let k = write_lines(f, 5) in f
    fn pass(f: File) -> Result<Nat, Text> = file_close(f)
"#;

fn body(dir: &str) -> String {
    let p = format!("{dir}/a.txt");
    let q = format!("{dir}/b.bin");
    format!(
        r#"{HELPERS}    print_line(status(file_close(make("{p}"))))
    print_line(let f = file_open("{p}", "r") in let a = file_read_line(f) in let n = count(f, 50) in concat(unwrap_or(a, "?"), concat(" + ", concat(nat_to_text(n), concat(" ", status(file_close(f)))))))
    print_line(let f = file_open("{p}", "a") in let w = file_write(f, "tail") in let r = file_read_all(f) in let w2 = file_write(f, "more") in concat(file_error(f), concat(" | ", status(pass(f)))))
    print_line(let f = file_open("{p}", "r") in let all = file_read_all(f) in let again = file_read_line(f) in concat(nat_to_text(text_length(all)), concat(" ", concat(if_then_else(is_none(again), "eof", "?"), concat(" ", status(file_close(f)))))))
    print_line(let f = file_open("{p}", "r") in let b = file_read_bytes(f, 7) in let n = buf_len(b) in let z = buf_free(b) in let l = file_read_line(f) in concat(nat_to_text(n), concat(" [", concat(unwrap_or(l, "?"), concat("] ", status(file_close(f)))))))
    print_line(let f = file_open("{q}", "w") in let b = buf_from_list(list(0, 255, 10, 192)) in let w = file_write_bytes(f, b) in let z = buf_free(b) in let fl = file_flush(f) in status(file_close(f)))
    print_line(let f = file_open("{q}", "r") in let t = file_read_all(f) in concat(file_error(f), concat(" | ", status(file_close(f)))))
    print_line(let f = file_open("{q}", "r") in let b = file_read_bytes(f, 100) in let s = join(map(buf_to_list(b), fun(x: Nat, nat_to_text(x))), ",") in concat(s, concat(" ", status(file_close(f)))))
    print_line(let f = file_open("{dir}/missing/x", "w") in let w = file_write(f, "x") in concat(if_then_else(file_ok(f), "ok", "failed"), concat(" ", status(file_close(f)))))
    print_line(let f = file_open("{p}", "rw") in status(file_close(f)))
    print_line(let f = file_open("{p}", "r") in let w = file_write(f, "x") in let a = file_read_all(f) in concat(nat_to_text(text_length(a)), concat(" ", status(file_close(f)))))
    print_line(let f = file_open("{p}", "r") in let b = file_read_bytes(f, 268435457) in let n = buf_free(b) in status(file_close(f)))
    print_line(status(file_remove("{p}")))
    print_line(status(file_remove("{p}")))
    print_line(status(file_remove("{q}")))"#
    )
}

#[test]
fn files_agree_between_interpreter_and_native_and_are_closed_in_scope() {
    let run_dir = scratch("interp");
    let p = program(&body(run_dir.to_str().unwrap()));
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let interpreted = Runtime::new().run_program(&p).unwrap().output;
    let native_dir = scratch("native");
    let (out, err, code) = native(&program(&body(native_dir.to_str().unwrap())), &native_dir);
    assert_eq!(code, 0, "{err}");
    let fix = |lines: Vec<String>, dir: &PathBuf| {
        lines
            .into_iter()
            .map(|l| l.replace(dir.to_str().unwrap(), "<dir>"))
            .collect::<Vec<_>>()
    };
    let interpreted = fix(interpreted, &run_dir);
    let native_lines = fix(out.lines().map(str::to_string).collect(), &native_dir);
    assert_eq!(interpreted, native_lines);
    assert_eq!(
        interpreted,
        [
            "ok 35",
            "line 5 + 4 ok 0",
            "file `<dir>/a.txt` is not open for reading | err file `<dir>/a.txt` is not open for reading",
            "39 eof ok 0",
            "7 [line 4] ok 0",
            "ok 4",
            "file `<dir>/b.bin` is not valid UTF-8 | err file `<dir>/b.bin` is not valid UTF-8",
            "0,255,10,192 ok 0",
            "failed err cannot open file `<dir>/missing/x`",
            "err invalid file mode `rw`",
            "0 err file `<dir>/a.txt` is not open for writing",
            "err byte buffer length exceeds 268435456",
            "ok 0",
            "err cannot remove file `<dir>/a.txt`",
            "ok 0",
        ]
    );
    assert!(
        err.contains("dlm files: 12 opened, 12 closed, 0 closed at exit"),
        "{err}"
    );
    std::fs::remove_dir_all(run_dir).ok();
    std::fs::remove_dir_all(native_dir).ok();
}

#[test]
fn runtime_errors_close_open_files_at_exit_and_keep_written_data() {
    let dir = scratch("error");
    let path = dir.join("log.txt");
    let src = format!(
        "    print_line(let f = file_open(\"{}\", \"w\") in let w = file_write(f, \"before\") in let b = buf_set(buf_new(1, 0), 5, 1) in let n = buf_free(b) in match file_close(f) {{ ok(k) => \"closed\", err(e) => e }})",
        path.display()
    );
    let p = program(&src);
    let interpreted = Runtime::new().run_program(&p).expect_err("runtime error");
    assert!(format!("{interpreted:?}").contains("byte buffer index out of range"));
    assert_eq!(std::fs::read_to_string(&path).unwrap(), "before");
    std::fs::remove_file(&path).unwrap();
    let (out, err, code) = native(&p, &dir);
    assert_eq!((out.as_str(), code), ("", 1), "{err}");
    assert!(
        err.contains("dlm runtime error: byte buffer index out of range")
            && err.contains("1 opened, 0 closed, 1 closed at exit"),
        "{err}"
    );
    assert_eq!(std::fs::read_to_string(&path).unwrap(), "before");
    std::fs::remove_dir_all(dir).ok();
}

fn ownership_error(body: &str) -> String {
    let p = program(body);
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    let errors: Vec<String> = report
        .diagnostics
        .iter()
        .filter(|d| d.kind == DiagnosticKind::OwnershipError)
        .map(|d| d.message.clone())
        .collect();
    assert!(
        !errors.is_empty(),
        "expected an ownership error for:\n{body}\n{:?}",
        report.diagnostics
    );
    errors.join("\n")
}

#[test]
fn files_are_linear() {
    let cases = [
        ("fn f() -> Bool = let h = file_open(\"x\", \"r\") in file_ok(h)", "File 'h' is not closed"),
        ("fn f(c: Bool) -> Nat = let h = file_open(\"x\", \"r\") in if_then_else(c, match file_close(h) { ok(n) => n, err(e) => 0 }, 0)", "File 'h' is consumed on one branch only"),
        ("fn f() -> Bool = let h = file_open(\"x\", \"r\") in let r = file_close(h) in file_ok(h)", "File 'h' is borrowed after it was moved"),
        ("fn f() -> Nat = let h = file_open(\"x\", \"r\") in let r = file_close(h) in let s = file_close(h) in 0", "File 'h' is used after it was moved"),
        ("fn f(h: File) -> Bool = file_ok(h)", "File 'h' is not closed"),
        ("fn f(h: &File) -> Bool = is_ok(file_close(h))", "cannot be moved"),
        ("fn f() -> List<File> = empty_list(File)", "cannot be stored in a container"),
        ("fn f() -> Nat = let h = file_open(\"x\", \"r\") in let g = h in 0", "File 'g' is not closed"),
        ("fn mk() -> File = file_open(\"x\", \"r\")\n    fn f() -> Bool = let h = mk() in file_ok(h)", "File 'h' is not closed"),
        ("fn f() -> Nat = let n = file_ok(file_open(\"x\", \"r\")) in 0", "must be a named owner"),
    ];
    for (body, message) in cases {
        let e = ownership_error(body);
        assert!(e.contains(message), "{body}\n{e}");
    }
    // accepted: closed on both branches, returned, moved into a File parameter, borrowed by &File
    for ok in [
        "fn f(c: Bool) -> Nat = let h = file_open(\"x\", \"r\") in if_then_else(c, match file_close(h) { ok(n) => n, err(e) => 0 }, match file_close(h) { ok(n) => 1, err(e) => 2 })",
        "fn f() -> File = let h = file_open(\"x\", \"r\") in h",
        "fn g(h: File) -> Nat = match file_close(h) { ok(n) => n, err(e) => 0 }\n    fn f() -> Nat = let h = file_open(\"x\", \"r\") in g(h)",
        "fn g(h: &File) -> Bool = file_ok(h)\n    fn f() -> Bool = let h = file_open(\"x\", \"r\") in let v = g(h) in let c = file_close(h) in v",
    ] {
        let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&program(ok));
        assert!(report.ok(), "{ok}\n{:?}", report.diagnostics);
    }
}

#[test]
fn file_remove_rejects_directories_and_errors_preserve_embedded_nul() {
    let dir = scratch("paths");
    let empty = dir.join("empty");
    std::fs::create_dir_all(&empty).unwrap();
    let body = format!(
        r#"{HELPERS}
    print_line(status(file_remove("{}")))
    print_line(let f = file_open("{}\0suffix", "r") in status(file_close(f)))
    print_line(let f = file_open("{}", "r\0w") in status(file_close(f)))
    print_line(status(file_remove("{}\0suffix")))"#,
        empty.display(),
        empty.display(),
        empty.display(),
        empty.display()
    );
    let p = program(&body);
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let interpreted = Runtime::new().run_program(&p).unwrap().output;
    assert!(interpreted.iter().all(|s| s.starts_with("err ")));
    assert!(interpreted[1..].iter().all(|s| s.contains('\0')));
    let (out, err, code) = native(&p, &dir);
    assert_eq!(code, 0, "{err}");
    assert_eq!(out.lines().collect::<Vec<_>>(), interpreted);
    assert!(empty.is_dir(), "file_remove must not remove directories");
    std::fs::remove_dir_all(dir).unwrap();
}
