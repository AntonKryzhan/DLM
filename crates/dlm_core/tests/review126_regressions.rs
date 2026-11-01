//! Boundary regressions for recursive types and OS integration.
use dlm_core::{build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program, parse_module, CheckPolicy, Checker, Program, Runtime};
use std::io::Write;
use std::net::TcpListener;
use std::path::Path;
use std::process::Command;

fn checked(body: &str) -> bool {
    let source = format!("module boundary\ntheory T {{\n{body}\n}}\n");
    match parse_module(&source) {
        Ok(module) => Checker::with_policy(CheckPolicy::trusted_only()).check_program(&Program::single(module)).ok(),
        Err(_) => false,
    }
}

fn run_both(name: &str, body: &str) -> Vec<String> {
    let dir = std::env::temp_dir().join(format!("dlm-review126-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, format!("module boundary\nimport std.net\ntheory T extends StdNet {{\n{body}\n}}\n")).unwrap();
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let p = load_program_with_paths(&path, &[root.join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let interpreted = Runtime::new().run_program(&p).unwrap().output;
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(&emit_c(&ir), &dir.join("main.c"), &dir.join("main"), &default_c_compiler()).unwrap();
    let output = Command::new(exe).output().unwrap();
    assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
    let native: Vec<_> = String::from_utf8(output.stdout).unwrap().lines().map(str::to_string).collect();
    assert_eq!(interpreted, native);
    std::fs::remove_dir_all(dir).unwrap();
    native
}

#[test]
fn recursive_function_fields_cannot_encode_unchecked_nontermination() {
    for field in ["Fn<R,Nat>", "Option<Fn<R,Nat>>", "List<Fn<Nat,R>>"] {
        assert!(!checked(&format!("record R(f: {field})")), "accepted {field}");
    }
    assert!(checked("record R(children: List<R>, show: Fn<Nat,Text>)"));
}

#[test]
fn unguarded_enum_projections_are_partial_and_tags_match_payloads() {
    assert!(!checked("enum E { A(n: Nat), B }\nfn apply_e(f: Fn<E,Nat>, e: E) -> Nat = f(e)\nlet bad = apply_e(A_f0, B())"));
    assert!(!checked("enum E { A(n: Nat), B }\nlet bad = E(some(true),none(Nat),none(Bool))"));
    assert!(!checked("enum E { A(n: Nat), B }\nlet bad = E(none(Bool),some(3),some(true))"));
    let out = run_both("enum", r#"
    enum E { A(n: Nat), A_0(n: Nat), B }
    fn value(e: E) -> Nat = match e { A(n) => n, A_0(n) => n + 1, B => 0 }
    fn apply_e(f: Fn<E,Nat>, e: E) -> Nat = f(e)
    print_line(nat_to_text(apply_e(value,A(4))))
    print_line(nat_to_text(apply_e(value,A_0(4))))
    print_line(nat_to_text(A_f0(A(6))))
"#);
    assert_eq!(out, ["4","5","6"]);
}

#[test]
fn full_width_handle_ids_and_ports_have_exact_errors() {
    let out = run_both("ids", r#"
    fn rn(r: Result<Nat,Text>) -> Text = match r { ok(n) => nat_to_text(n), err(e) => e }
    fn rp(r: Result<Option<Nat>,Text>) -> Text = match r { ok(o) => "unexpected", err(e) => e }
    fn rw(r: Result<List<Text>,Text>) -> Text = match r { ok(xs) => "unexpected", err(e) => e }
    let n = 340282366920938463463374607431768211455
    print_line(rn(net_close(n)))
    print_line(rn(net_send(n,"")))
    print_line(rn(net_local_port(n)))
    print_line(rn(net_accept(n,1)))
    print_line(rn(net_listen("127.0.0.1",n)))
    print_line(rp(proc_poll(n)))
    print_line(rw(proc_wait(n)))
    print_line(rn(proc_kill(n)))
"#);
    let n = u128::MAX.to_string();
    assert_eq!(out, [format!("no socket {n}"),format!("no connection {n}"),format!("no socket {n}"),format!("no listener {n}"),format!("invalid port {n}"),format!("no process {n}"),format!("no process {n}"),format!("no process {n}")]);
}

#[test]
fn os_errors_preserve_embedded_nul_and_long_names() {
    let long = "x".repeat(1500);
    let body = r#"
    fn rp(r: Result<List<Text>,Text>) -> Text = match r { ok(xs) => "unexpected", err(e) => e }
    fn rn(r: Result<Nat,Text>) -> Text = match r { ok(n) => "unexpected", err(e) => e }
    print_line(rp(proc_run("bad\0name",empty_list(Text),"")))
    print_line(rn(proc_spawn("bad\0name",empty_list(Text),"")))
    print_line(rp(net_resolve("bad\0host")))
    print_line(rn(net_connect("bad\0host",1,1)))
    print_line(rn(net_listen("bad\0host",0)))
    print_line(rp(proc_run("LONG",empty_list(Text),"")))
"#.replace("LONG",&long);
    let out = run_both("names", &body);
    assert!(out[..5].iter().all(|s| s.contains('\0')));
    assert!(out[5].contains(&long));
}

#[test]
fn receive_limit_remains_enforced_after_the_first_failure() {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    let peer = std::thread::spawn(move || {
        for _ in 0..2 {
            let (mut stream,_) = listener.accept().unwrap();
            let chunk = [b'x';65536];
            for _ in 0..dlm_core::os::NET_MAX / chunk.len() { stream.write_all(&chunk).unwrap(); }
            stream.write_all(b"\n").unwrap();
        }
    });
    let body = r#"
    fn id(r: Result<Nat,Text>) -> Nat = match r { ok(n) => n, err(e) => 0 }
    fn rt(r: Result<Text,Text>) -> Text = match r { ok(t) => "unexpected", err(e) => e }
    fn rl(r: Result<Option<Text>,Text>) -> Text = match r { ok(t) => "unexpected", err(e) => e }
    let c = id(net_connect("127.0.0.1",PORT,5000))
    print_line(rt(net_recv_all(c)))
    print_line(rl(net_recv_line(c)))
    let closed = net_close(c)
"#.replace("PORT",&port.to_string());
    let out = run_both("limit", &body);
    peer.join().unwrap();
    let error = format!("connection 1 sent more than {} bytes",dlm_core::os::NET_MAX);
    assert_eq!(out, [error.clone(),error]);
}

#[cfg(unix)]
#[test]
fn independent_process_tables_do_not_overwrite_each_others_files() {
    let mut a = dlm_core::os::ProcTable::default();
    let mut b = dlm_core::os::ProcTable::default();
    let first = a.spawn("sh",&["-c".into(),"sleep 0.1; cat".into()],"first").unwrap();
    let second = b.spawn("cat",&[],"second").unwrap();
    assert_eq!(a.wait(first).unwrap(),["0","first",""]);
    assert_eq!(b.wait(second).unwrap(),["0","second",""]);
}

#[test]
fn http_helpers_reject_malformed_responses_and_incomplete_requests() {
    let out = run_both("http-errors", r#"
    fn result(r: Result<HttpResponse,Text>) -> Text = match r { ok(h) => "unexpected", err(e) => "rejected" }
    fn id(r: Result<Nat,Text>) -> Nat = match r { ok(n) => n, err(e) => 0 }
    fn page(path: Text) -> Text = "body"
    print_line(result(parse_http("garbage 200 OK\r\n\r\nbody")))
    print_line(result(parse_http("HTTP/1.1 999 invalid\r\n\r\nbody")))
    print_line(result(parse_http("HTTP/1.1 200 OK")))
    let l = id(net_listen("127.0.0.1",0))
    let c = id(net_connect("127.0.0.1",id(net_local_port(l)),1000))
    let sent = net_send(c,"GET /x HTTP/1.0\r\nX: y\r\n")
    let shut = net_shutdown(c)
    print_line(match http_serve_one(l,1000,page) { ok(t) => "unexpected", err(e) => e })
    let close_c = net_close(c)
    let close_l = net_close(l)
"#);
    assert_eq!(out,["rejected","rejected","rejected","unexpected end of HTTP headers"]);
}
