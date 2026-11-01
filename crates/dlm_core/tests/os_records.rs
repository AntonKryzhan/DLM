//! v0.126 — recursive records and recursive / generic enums with structural
//! measures, child processes (`proc_*`), TCP (`net_*`), `std.process`,
//! `std.net`. Interpreter and native C agree.
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program, parse_module,
    CheckPolicy, Checker, Program, Runtime,
};
use std::path::{Path, PathBuf};
use std::process::Command;

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn run_both(name: &str, source: &str) -> Vec<String> {
    let dir = std::env::temp_dir().join(format!("dlm-v126-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let interpreted = Runtime::new().run_program(&p).unwrap().output;
    let interpreted: Vec<String> = interpreted.join("\n").lines().map(str::to_string).collect();
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(&emit_c(&ir), &dir.join("main.c"), &dir.join(if cfg!(windows) { "main.exe" } else { "main" }), &default_c_compiler()).unwrap();
    let out = Command::new(exe).output().unwrap();
    assert!(out.status.success(), "{}", String::from_utf8_lossy(&out.stderr));
    let native: Vec<String> = String::from_utf8(out.stdout).unwrap().lines().map(str::to_string).collect();
    assert_eq!(interpreted, native);
    std::fs::remove_dir_all(dir).ok();
    native
}

const RECURSIVE: &str = r#"    enum Tree { Leaf, Node(l: Tree, v: Nat, r: Tree) }
    enum Expr { Num(n: Nat), Add(a: Expr, b: Expr), Mul(a: Expr, b: Expr), Neg(e: Expr) }
    enum Opt<T> { Some(x: T), None }
    enum Chain<T> { End, Link(h: T, t: Chain<T>) }
    record Dir(name: Text, files: List<Text>, subdirs: List<Dir>)
    fn reverse_nat(a: List<Nat>) -> List<Nat> = fold(a, empty_list(Nat), fun(acc: List<Nat>, x: Nat, cons(x, acc)))
    fn concat_lists(a: List<Nat>, b: List<Nat>) -> List<Nat> = fold(reverse_nat(a), b, fun(acc: List<Nat>, x: Nat, cons(x, acc)))
    fn int_mul(a: Int, b: Int) -> Int = mul(a, b)
    fn insert(t: Tree, x: Nat) -> Tree decreasing t = match t { Leaf => Node(Leaf(), x, Leaf()),
        Node(l, v, r) => if_then_else(x > v, Node(l, v, insert(r, x)), Node(insert(l, x), v, r)) }
    fn size(t: Tree) -> Nat decreasing t = match t { Leaf => 0, Node(l, v, r) => size(l) + 1 + size(r) }
    fn inorder(t: Tree) -> List<Nat> decreasing t = match t { Leaf => empty_list(Nat), Node(l, v, r) => concat_lists(inorder(l), cons(v, inorder(r))) }
    fn eval(e: Expr) -> Int decreasing e = match e { Num(n) => int(n), Add(a, b) => eval(a) + eval(b), Mul(a, b) => int_mul(eval(a), eval(b)), Neg(x) => int(0) - eval(x) }
    fn get_or<T>(o: Opt<T>, d: T) -> T = match o { Some(x) => x, None => d }
    fn chain_len<T>(c: Chain<T>) -> Nat decreasing c = match c { End => 0, Link(h, t) => 1 + chain_len(t) }
    fn first_depth(d: Dir) -> Nat decreasing d = match field(d, subdirs) { [] => 0, cons(h, t) => 1 + first_depth(h) }
    print_line(nat_to_text(size(insert(insert(insert(insert(Leaf(), 5), 2), 8), 3))))
    print_symbolic(inorder(insert(insert(insert(insert(Leaf(), 5), 2), 8), 3)))
    print_line(int_to_text(eval(Add(Num(2), Mul(Num(3), Neg(Num(4)))))))
    print_line(nat_to_text(get_or(Some(7), 0)))
    print_line(nat_to_text(get_or(None<Nat>(), 9)))
    print_line(get_or(Some("t"), "d"))
    print_line(nat_to_text(chain_len(Link(1, Link(2, Link(3, End<Nat>()))))))
    print_line(nat_to_text(first_depth(Dir("r", list("a", "b"), list(Dir("s", list("c"), empty_list(Dir)), Dir("u", empty_list(Text), empty_list(Dir)))))))
    print_line(if_then_else(equal(insert(Leaf(), 1), insert(Leaf(), 1)), "eq", "ne"))
"#;

const NETWORK: &str = r#"    fn rn(r: Result<Nat, Text>) -> Text = match r { ok(n) => nat_to_text(n), err(e) => e }
    fn rt(r: Result<Text, Text>) -> Text = match r { ok(t) => t, err(e) => e }
    fn rl(r: Result<List<Text>, Text>) -> Text = match r { ok(xs) => join(xs, "|"), err(e) => e }
    fn ro(r: Result<Option<Text>, Text>) -> Text = match r { ok(o) => match o { none => "<eof>", some(t) => t }, err(e) => e }
    fn id(r: Result<Nat, Text>) -> Nat = match r { ok(n) => n, err(e) => 0 }
    fn exchange(port: Nat) -> Text =
        let l = id(net_listen("127.0.0.1", port)) in let p = id(net_local_port(l)) in
        let c = id(net_connect("127.0.0.1", p, 2000)) in let s1 = net_send(c, "GET /x HTTP/1.0\r\nHost: a\r\n\r\n") in let sh = net_shutdown(c) in
        let a = id(net_accept(l, 2000)) in let l1 = ro(net_recv_line(a)) in let l2 = ro(net_recv_line(a)) in let l3 = ro(net_recv_line(a)) in let l4 = ro(net_recv_line(a)) in
        let w = net_send(a, "HTTP/1.0 200 OK\r\n\r\nhello") in let x = net_close(a) in
        let body = rt(net_recv_all(c)) in let y = net_close(c) in let z = net_close(l) in
        join(list(l1, l2, l3, l4, body, rn(net_close(l))), ";")
    print_line(rl(proc_run("printf", list("%s-%s", "a", "b"), "")))
    print_line(rl(proc_run("cat", empty_list(Text), "piped input\nline2")))
    print_line(rl(proc_run("sh", list("-c", "echo err >&2; exit 3"), "")))
    print_line(rl(proc_run("/nonexistent/prog", empty_list(Text), "")))
    print_line(match proc_env("HOME") { none => "no home", some(h) => "home" })
    print_line(match proc_env("DLM_SURELY_UNSET_VAR") { none => "unset", some(h) => h })
    print_line(rl(net_resolve("localhost")))
    print_line(rl(net_resolve("no-such-host.invalid")))
    print_line(rn(net_connect("127.0.0.1", 1, 1000)))
    print_line(rn(net_send(99, "x")))
    print_line(rn(net_listen("127.0.0.1", 70000)))
    print_line(exchange(0))
    print_line(exchange(0))
"#;

const HTTP: &str = r#"    fn page(path: Text) -> Text = concat("you asked for ", path)
    fn rs(r: Result<HttpResponse, Text>) -> Text = match r { ok(h) => concat(concat(nat_to_text(field(h, status)), " "), field(h, body)), err(e) => e }
    fn id(r: Result<Nat, Text>) -> Nat = match r { ok(n) => n, err(e) => 0 }
    // client request is fully buffered by the kernel before the server accepts it
    fn roundtrip() -> Text =
        let l = id(net_listen("127.0.0.1", 0)) in let p = id(net_local_port(l)) in
        let c = id(net_connect("127.0.0.1", p, 2000)) in let s = net_send(c, "GET /hello HTTP/1.0\r\nHost: x\r\n\r\n") in
        let served = http_serve_one(l, 2000, page) in let resp = net_recv_all(c) in let x = net_close(c) in let y = net_close(l) in
        concat(concat(match served { ok(t) => t, err(e) => e }, " => "), rs(match resp { ok(t) => parse_http(t), err(e) => err(e, HttpResponse) }))
    print_line(roundtrip())
    print_line(rs(parse_http("HTTP/1.1 404 Not Found\r\nA: b\r\n\r\nmissing")))
    print_line(rs(http_get("127.0.0.1", 1, "/", 500)))
    print_line(match output_of(shell("echo hi; echo there")) { ok(t) => t, err(e) => e })
    print_line(match output_of(shell("exit 4")) { ok(t) => t, err(e) => e })
    print_line(if_then_else(succeeded(run("true", empty_list(Text))), "ok", "fail"))
    print_line(env_or("DLM_SURELY_UNSET_VAR", "dflt"))
"#;

const PROCESSES: &str = r#"    fn show(r: Result<List<ProcResult>, Text>) -> Text = match r { err(e) => e, ok(xs) => join(map(xs, fun(p: ProcResult, concat(concat(nat_to_text(field(p, code)), ":"), field(p, out)))), ",") }
    fn id(r: Result<Nat, Text>) -> Nat = match r { ok(n) => n, err(e) => 0 }
    fn bg() -> Text =
        let a = id(proc_spawn("sh", list("-c", "sleep 0.3; echo slow"), "")) in
        let b = id(proc_spawn("cat", empty_list(Text), "fed")) in
        let ra = is_running(a) in let k = proc_kill(a) in
        let wa = wait_for(a) in let wb = wait_for(b) in
        join(list(if_then_else(ra, "running", "done"), match wa { ok(p) => nat_to_text(field(p, code)), err(e) => e }, match wb { ok(p) => field(p, out), err(e) => e }, match proc_wait(a) { ok(x) => "?", err(e) => e }), "|")
    print_line(show(run_parallel(list("sleep 0.2; echo one", "echo two", "exit 5"))))
    print_line(bg())
    print_line(match spawn("/nonexistent/x", empty_list(Text)) { ok(i) => "?", err(e) => e })
"#;

#[test]
fn recursive_agree_between_interpreter_and_native() {
    let out = run_both("recursive", &format!("module test.rec\ntheory App {{\n{RECURSIVE}}}\n"));
    let want: &[&str] = &[
        "4",
        "[2, 3, 5, 8]",
        "-10",
        "7",
        "9",
        "t",
        "3",
        "1",
        "eq",
    ];
    assert_eq!(out, want);
}

#[test]
fn network_agree_between_interpreter_and_native() {
    let out = run_both("network", &format!("module test.net\ntheory App {{\n{NETWORK}}}\n"));
    use std::net::ToSocketAddrs;
    let mut addresses: Vec<_> = ("localhost", 0).to_socket_addrs().unwrap().map(|a| a.ip().to_string()).collect();
    addresses.sort();
    addresses.dedup();
    let addresses = addresses.join("|");
    let want: &[&str] = &[
        "0|a-b|",
        "0|piped input",
        "line2|",
        "3||err",
        "",
        "cannot run `/nonexistent/prog` (os error 2)",
        "home",
        "unset",
        addresses.as_str(),
        "cannot resolve `no-such-host.invalid`",
        "cannot connect to `127.0.0.1:1` (os error 111)",
        "no connection 99",
        "invalid port 70000",
        "GET /x HTTP/1.0;Host: a;;<eof>;HTTP/1.0 200 OK",
        "",
        "hello;no socket 1",
        "GET /x HTTP/1.0;Host: a;;<eof>;HTTP/1.0 200 OK",
        "",
        "hello;no socket 4",
    ];
    assert_eq!(out, want);
}

#[test]
fn http_agree_between_interpreter_and_native() {
    let out = run_both("http", &format!("module test.http\nimport std.net\ntheory App extends StdNet {{\n{HTTP}}}\n"));
    let want: &[&str] = &[
        "/hello => 200 you asked for /hello",
        "404 missing",
        "cannot connect to `127.0.0.1:1` (os error 111)",
        "hi",
        "there",
        "",
        "exit 4: ",
        "ok",
        "dflt",
    ];
    assert_eq!(out, want);
}

#[test]
fn processes_agree_between_interpreter_and_native() {
    let out = run_both("processes", &format!("module test.procs\nimport std.process\ntheory App extends StdProcess {{\n{PROCESSES}}}\n"));
    let want: &[&str] = &[
        "0:one",
        ",0:two",
        ",5:",
        "running|137|fed|no process 4",
        "cannot run `/nonexistent/x` (os error 2)",
    ];
    assert_eq!(out, want);
}

fn diagnostics(items: &str) -> String {
    let source = format!("module bad\ntheory T {{\n{items}\n}}\n");
    match parse_module(&source) {
        Err(e) => format!("{e:?}"),
        Ok(module) => {
            let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&Program::single(module));
            report.diagnostics.iter().map(|d| d.to_string()).collect::<Vec<_>>().join("\n")
        }
    }
}

#[test]
fn recursion_on_records_is_checked() {
    let rejected = [
        ("    record Loop(next: Loop)", "without Option/List"),
        ("    record P(a: Nat, b: ProductType<Nat*P>)", "without Option/List"),
        ("    enum T { L, N(l: T, r: T) }\n    fn f(t: T) -> Nat decreasing t = match t { L => 0, N(l, r) => f(t) }", "must pass"),
        ("    enum T { L, N(l: T, r: T) }\n    fn g(t: T) -> T = t\n    fn f(t: T) -> Nat decreasing t = match t { L => 0, N(l, r) => f(g(l)) }", "must pass"),
        ("    enum T { L, N(l: T, r: T) }\n    fn f(t: T) -> Nat = match t { L => 0, N(l, r) => f(l) }", "no measure"),
    ];
    for (items, needle) in rejected {
        let text = diagnostics(items);
        assert!(text.contains(needle), "{items}\n{text}");
    }
    let accepted = [
        "    record Node(v: Nat, kids: List<Node>, parent: Option<Node>)",
        "    enum T { L, N(l: T, r: T) }\n    fn depth(t: T) -> Nat decreasing t = match t { L => 0, N(l, r) => let a = depth(l) in let b = depth(r) in 1 + if_then_else(a > b, a, b) }",
        "    enum Opt<A> { Some(x: A), Nothing }\n    fn get<A>(o: Opt<A>, d: A) -> A = match o { Some(x) => x, Nothing => d }\n    fn probe() -> Nat = get(Some(1), 0)",
        "    record Dir(name: Text, subdirs: List<Dir>)\n    fn first(d: Dir) -> Nat decreasing d = match field(d, subdirs) { [] => 0, cons(h, t) => 1 + first(h) }",
    ];
    for items in accepted {
        let text = diagnostics(items);
        assert!(text.is_empty(), "{items}\n{text}");
    }
}
