//! v0.102 — stage D-min: owned mutable byte buffers (`ByteBuf`). Static move /
//! borrow checking over every control path, releases inserted at the end of
//! owner scopes and on branches that do not consume an owner, interpreter /
//! native C agreement, cleanup of live buffers when a runtime error ends the
//! program, and negative lifetime scenarios (`docs/OWNERSHIP.md`).
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, lower_program, parse_module, CheckPolicy,
    Checker, DiagnosticKind, IrExpr, IrOp, Program, Runtime,
};
use std::path::PathBuf;
use std::process::{Command, Stdio};

fn program(body: &str) -> Program {
    let source = format!("module test.own\ntheory App {{\n{body}\n}}\n");
    Program::single(parse_module(&source).unwrap_or_else(|d| panic!("{d:?}\n{source}")))
}

fn scratch(name: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("dlm-ownership-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

/// Native run: (stdout, stderr, exit code) with the ownership trace enabled.
fn native(p: &Program, name: &str) -> (String, String, i32) {
    let dir = scratch(name);
    let ir =
        lower_program(p, CheckPolicy::trusted_only()).unwrap_or_else(|d| panic!("{name}: {d:?}"));
    let exe = build_c_executable(
        &emit_c(&ir),
        &dir.join("main.c"),
        &dir.join("main"),
        &default_c_compiler(),
    )
    .unwrap();
    let out = Command::new(exe)
        .env("DLM_OWNERSHIP_TRACE", "1")
        .stdin(Stdio::null())
        .output()
        .unwrap();
    std::fs::remove_dir_all(dir).ok();
    (
        String::from_utf8(out.stdout).unwrap(),
        String::from_utf8(out.stderr).unwrap(),
        out.status.code().unwrap_or(-1),
    )
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

fn count_releases(e: &IrExpr) -> usize {
    let own = usize::from(matches!(&e.op, IrOp::Builtin { name, .. } if name == "__release"));
    own + children(e).into_iter().map(count_releases).sum::<usize>()
}

fn children(e: &IrExpr) -> Vec<&IrExpr> {
    match &e.op {
        IrOp::Call { args, .. } | IrOp::Builtin { args, .. } => args.iter().collect(),
        IrOp::Apply { callee, args } => std::iter::once(&**callee).chain(args).collect(),
        IrOp::Add(a, b)
        | IrOp::Gt(a, b)
        | IrOp::Pow(a, b)
        | IrOp::And(a, b)
        | IrOp::Or(a, b)
        | IrOp::UnwrapOr(a, b) => vec![a, b],
        IrOp::If(c, a, b) | IrOp::Case(c, a, b) => vec![c, a, b],
        IrOp::Let { value, body, .. } => vec![value, body],
        IrOp::Inject { value, .. } => vec![value],
        IrOp::Field { record, .. } => vec![record],
        IrOp::Record { fields, .. } => fields.iter().map(|(_, v)| v).collect(),
        _ => Vec::new(),
    }
}

const PROGRAMS: &str = r#"
    fn fill(b: ByteBuf, i: Nat, n: Nat) -> ByteBuf decreasing n =
        if_then_else(nat_eq(n, 0), b, fill(buf_set(b, i, mod(mul(i, 7), 256)), i + 1, pred(n)))
    fn checksum(b: ByteBuf, i: Nat, n: Nat, acc: Nat) -> Nat decreasing n =
        if_then_else(nat_eq(n, 0), acc, checksum(b, i + 1, pred(n), acc + unwrap_or(buf_get(b, i), 0)))
    fn sum(b: ByteBuf) -> Nat = checksum(b, 0, buf_len(b), 0)
    fn grow(b: ByteBuf, n: Nat) -> ByteBuf decreasing n = if_then_else(nat_eq(n, 0), b, grow(buf_push(b, mod(n, 256)), pred(n)))
    fn swap01(b: ByteBuf) -> ByteBuf =
        let x = unwrap_or(buf_get(b, 0), 0) in
        buf_set(buf_set(b, 0, unwrap_or(buf_get(b, 1), 0)), 1, x)
    fn keep_or_free(b: ByteBuf, keep: Bool) -> Nat = if_then_else(keep, buf_len(b), buf_free(b))
    fn pick(a: ByteBuf, c: ByteBuf, first: Bool) -> ByteBuf = if_then_else(first, a, c)
    fn render(xs: List<Nat>) -> Text = fold(xs, "", fun(acc: Text, x: Nat, concat(acc, concat(nat_to_text(x), ","))))
    print_line(nat_to_text(let b = fill(buf_new(10, 0), 0, 10) in sum(b)))
    print_line(nat_to_text(let b = grow(buf_new(0, 0), 1000) in buf_len(b)))
    print_line(render(buf_to_list(swap01(buf_from_list(list(1, 2, 3))))))
    print_line(nat_to_text(keep_or_free(buf_new(5, 1), true) + keep_or_free(buf_new(7, 1), false)))
    print_line(nat_to_text(let p = pick(buf_new(2, 0), buf_new(3, 0), false) in buf_len(p)))
    print_line(render(let b = buf_new(3, 9) in buf_to_list(buf_set(b, 0, buf_len(b)))))
    print_line(nat_to_text(let b = buf_new(4, 0) in let n = buf_len(b) in let b = buf_push(b, 1) in n + buf_len(b)))
    print_line(nat_to_text(let b = buf_new(2, 0) in if_then_else(nat_eq(buf_len(b), 2), 1, buf_free(b))))
    print_line(nat_to_text(let b = buf_new(3, 0) in unwrap_or(buf_get(b, 7), 42)))
"#;

const EXPECTED: &[&str] = &["315", "1000", "2,1,3,", "12", "3", "3,9,9,", "9", "1", "42"];

#[test]
fn buffers_agree_between_interpreter_and_native_and_are_released_in_scope() {
    let p = program(PROGRAMS);
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(Runtime::new().run_program(&p).unwrap().output, EXPECTED);
    let (out, err, code) = native(&p, "agree");
    assert_eq!(code, 0, "{err}");
    assert_eq!(out.lines().collect::<Vec<_>>(), EXPECTED);
    // 11 buffers (one per buf_new/buf_from_list evaluation): every one
    // released in its scope, none left for exit cleanup.
    assert_eq!(
        err.trim(),
        "dlm ownership: 11 buffers created, 11 released in scope, 0 released at exit"
    );

    // the release plan: params kept by a path (sum, checksum's base case,
    // keep_or_free's borrowing branch, pick's unchosen buffer), lets kept
    // after their body
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let releases = |name: &str| {
        let f = ir
            .functions
            .iter()
            .find(|f| f.name.ends_with(&format!(".{name}")))
            .unwrap();
        count_releases(&f.body)
    };
    assert_eq!(releases("fill"), 0);
    assert_eq!(releases("checksum"), 1);
    assert_eq!(releases("sum"), 0);
    assert_eq!(releases("grow"), 0);
    assert_eq!(releases("swap01"), 0);
    assert_eq!(releases("keep_or_free"), 1);
    assert_eq!(releases("pick"), 2);
}

#[test]
fn runtime_errors_end_the_program_and_release_live_buffers() {
    let cases = [
        ("print_line(nat_to_text(let a = buf_new(3, 0) in let b = buf_new(2, 0) in let c = buf_set(b, 5, 1) in buf_len(c) + buf_len(a)))", "byte buffer index out of range", 2),
        ("print_line(nat_to_text(let a = buf_new(3, 0) in buf_free(buf_set(a, 0, 256))))", "byte value exceeds 255", 1),
        ("print_line(nat_to_text(buf_free(buf_new(268435457, 0))))", "byte buffer length exceeds 268435456", 0),
        ("print_line(nat_to_text(buf_free(buf_from_list(list(1, 300)))))", "byte value exceeds 255", 1),
        ("print_line(nat_to_text(buf_free(buf_push(buf_new(1, 0), 999))))", "byte value exceeds 255", 1),
        ("fn deep(b: ByteBuf, n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), buf_free(buf_set(b, 9, 0)), deep(buf_push(b, 1), pred(n)))\n    print_line(\"start\")\n    print_line(nat_to_text(deep(buf_new(0, 0), 5)))", "byte buffer index out of range", 1),
    ];
    for (i, (body, message, live)) in cases.iter().enumerate() {
        let p = program(body);
        let interpreted = Runtime::new().run_program(&p);
        let diagnostics = interpreted.expect_err("the interpreter fails too");
        assert!(
            format!("{diagnostics:?}").contains(message),
            "{i}: {diagnostics:?}"
        );
        let (out, err, code) = native(&p, &format!("error{i}"));
        assert_eq!(code, 1, "{i}: {err}");
        assert!(
            err.contains(&format!("dlm runtime error: {message}")),
            "{i}: {err}"
        );
        assert!(
            err.contains("released at exit\n")
                && err.contains(&format!(", {live} released at exit")),
            "{i}: {err}"
        );
        if i == 5 {
            assert_eq!(out, "start\n");
        }
    }
}

#[test]
fn negative_lifetime_scenarios_are_rejected() {
    let cases: &[(&str, &str)] = &[
        ("fn f(b: ByteBuf) -> Nat = let c = buf_push(b, 1) in buf_len(b) + buf_free(c)", "borrowed after it was moved"),
        ("fn two(x: ByteBuf, y: ByteBuf) -> Nat = buf_free(x) + buf_free(y)\n    fn f(b: ByteBuf) -> Nat = two(b, b)", "moved twice in one call"),
        ("fn two(x: ByteBuf, y: ByteBuf) -> Nat = buf_free(x) + buf_free(y)\n    fn f(b: ByteBuf) -> Nat = two(b, buf_push(b, 1))", "moved twice in one call"),
        ("fn f(b: ByteBuf) -> Nat = buf_free(b) + buf_len(b)", "borrowed after it was moved"),
        ("fn f(b: ByteBuf) -> Nat = buf_free(b) + buf_free(b)", "used after it was moved"),
        ("fn f(b: ByteBuf) -> ByteBuf = let c = b in b", "used after it was moved"),
        ("fn f(b: ByteBuf, c: Bool) -> Bool = and(c, nat_eq(buf_free(b), 0))", "lazily evaluated operand of and"),
        ("fn f(b: ByteBuf, c: Bool) -> Nat = unwrap_or(none(Nat), buf_free(b))", "lazily evaluated operand of unwrap_or"),
        ("fn keep(b: ByteBuf) -> ByteBuf = b\n    fn f(b: ByteBuf) -> List<Bool> = map(list(1), fun(x: Nat, let c = keep(b) in true))", "captured by a function value"),
        ("fn keep(b: ByteBuf) -> ByteBuf = b\n    fn f(b: ByteBuf) -> Bool = let g = fun(x: Nat, let c = keep(b) in true) in true", "captured by a function value"),
        ("fn f(n: Nat) -> Nat = let g = fun(x: ByteBuf, 0) in n", "function value parameter 'x'"),
        ("fn f(b: ByteBuf) -> List<ByteBuf> = list(b)", "result of fn 'f'"),
        ("fn f(b: ByteBuf) -> Nat = length(list(b))", "used as an ordinary value"),
        ("fn f(b: ByteBuf) -> Bool = is_some(some(b))", "used as an ordinary value"),
        ("fn f(xs: List<ByteBuf>) -> Nat = length(xs)", "parameter 'xs' of fn 'f'"),
        ("fn f(n: Nat) -> Nat = length(empty_list(ByteBuf))", "type argument"),
        ("record Holder(data: ByteBuf)", "field 'data' of record 'Holder'"),
        ("fn f(n: Nat) -> Nat = buf_len(buf_new(n, 0))", "must be a named owner"),
        ("fn mk(n: Nat) -> ByteBuf = buf_new(n, 0)\n    fn f(n: Nat) -> Nat = buf_len(mk(n))", "must be a named owner"),
        ("fn mk(n: Nat) -> ByteBuf = buf_new(n, 0)\n    fn f(n: Nat) -> Nat = let p = pair(mk(n), 0) in snd(p)", "a ByteBuf value is used as an ordinary value"),
        ("let global = buf_new(1, 0)", "theory-level value"),
        ("buf_new(1, 0)", "theory-level value"),
        ("fn mk(n: Nat) -> ByteBuf = buf_new(n, 0)\n    fn f(n: Nat) -> Nat = let b = mk(n) in pair(b, 1)", "used as an ordinary value"),
        ("theorem t: nat_eq(buf_len(buf_new(1, 0)), 1)", "cannot occur in statements"),
        ("fn f(b: ByteBuf, c: Bool) -> Nat = if_then_else(buf_free(b) > 0, buf_len(b), 0)", "borrowed after it was moved"),
        ("fn f(b: ByteBuf, c: Bool) -> ByteBuf = if_then_else(c, buf_push(b, 1), let x = buf_free(b) in b)", "used after it was moved"),
    ];
    for (body, message) in cases {
        let errors = ownership_error(body);
        assert!(errors.contains(message), "{body}\n => {errors}");
    }
    // buffer operations are effects: a function value cannot perform them
    let p = program("fn f(n: Nat) -> List<Nat> = map(list(1), fun(x: Nat, let b = buf_new(x, 0) in buf_free(b)))");
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report
        .diagnostics
        .iter()
        .any(|d| d.message.contains("lambda must be pure and total")
            && d.message.contains("runtime@byte_buffer")));
    // a rejected program is not compiled
    let p = program(cases[0].0);
    assert!(lower_program(&p, CheckPolicy::trusted_only()).is_err());
}

#[test]
fn ownership_errors_carry_code_and_position() {
    let p = program("fn f(b: ByteBuf) -> Nat =\n        buf_free(b) + buf_free(b)");
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    let d = report
        .diagnostics
        .iter()
        .find(|d| d.kind == DiagnosticKind::OwnershipError)
        .unwrap();
    let text = d.to_string();
    assert!(text.contains("E0943 OwnershipError"), "{text}");
    assert_eq!(d.span.map(|s| (s.start_line, s.start_col)), Some((4, 32)));
    assert!(text.contains("used after it was moved at line 4"), "{text}");
    assert!(text.contains("docs/OWNERSHIP.md"));
}

// ---------- v0.103: borrowed parameters, exclusive mutation, buffer API ----------

const BORROWS: &str = r#"
    fn sum(b: &ByteBuf, i: Nat, n: Nat, acc: Nat) -> Nat decreasing n =
        if_then_else(nat_eq(n, 0), acc, sum(b, i + 1, pred(n), acc + unwrap_or(buf_get(b, i), 0)))
    fn total(b: &ByteBuf) -> Nat = sum(b, 0, buf_len(b), 0)
    fn same(a: &ByteBuf, c: &ByteBuf) -> Bool = buf_eq(a, c)
    fn grow_by(a: ByteBuf, src: &ByteBuf) -> ByteBuf = buf_append(a, src)
    print_line(nat_to_text(let b = buf_from_list(list(1, 2, 3)) in total(b) + total(b) + buf_len(b)))
    print_line(if_then_else(let b = buf_new(3, 1) in same(b, b), "eq", "ne"))
    print_line(unwrap_or(let t = buf_from_text("мир") in let b = buf_append(buf_from_text("héllo "), t) in buf_to_text(b), "bad"))
    print_line(nat_to_text(let b = buf_from_text("abcdef") in let s = buf_slice(b, 2, 3) in buf_len(s) + unwrap_or(buf_get(s, 0), 0)))
    print_line(if_then_else(let b = buf_from_list(list(255, 1)) in is_none(buf_to_text(b)), "invalid", "valid"))
    print_line(nat_to_text(let a = buf_new(2, 5) in let c = buf_copy(a) in let a = buf_set(a, 0, 9) in mul(unwrap_or(buf_get(a, 0), 0), 10) + unwrap_or(buf_get(c, 0), 0)))
    print_line(nat_to_text(let s = buf_new(4, 1) in let g = grow_by(grow_by(buf_new(1, 0), s), s) in total(g) + buf_len(g)))
"#;

#[test]
fn borrowed_parameters_and_new_operations_agree_and_release_in_scope() {
    let p = program(BORROWS);
    let expected = ["15", "eq", "héllo мир", "102", "invalid", "95", "17"];
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(Runtime::new().run_program(&p).unwrap().output, expected);
    let (out, err, code) = native(&p, "borrows");
    assert_eq!(code, 0, "{err}");
    assert_eq!(out.lines().collect::<Vec<_>>(), expected);
    assert_eq!(
        err.trim(),
        "dlm ownership: 11 buffers created, 11 released in scope, 0 released at exit"
    );
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    for (name, releases, borrowed) in [
        ("sum", 0, vec![0]),
        ("total", 0, vec![0]),
        ("same", 0, vec![0, 1]),
        ("grow_by", 0, vec![1]),
    ] {
        let f = ir
            .functions
            .iter()
            .find(|f| f.name.ends_with(&format!(".{name}")))
            .unwrap();
        assert_eq!(
            (count_releases(&f.body), f.borrowed.clone()),
            (releases, borrowed),
            "{name}"
        );
    }
    // the IR verifier rejects a release of a borrowed parameter
    let mut broken = ir.clone();
    let id = broken
        .functions
        .iter()
        .position(|f| f.name.ends_with(".total"))
        .unwrap();
    let body = broken.functions[id].body.clone();
    let owner = IrExpr::new(
        IrOp::Local(0),
        Some(dlm_core::OrdinaryType::ByteBuf),
        body.span,
    );
    broken.functions[id].body = IrExpr::new(
        IrOp::Builtin {
            name: "__release".into(),
            args: vec![body.clone(), owner],
        },
        body.ty.clone(),
        body.span,
    );
    let issues = dlm_core::verify_ir(&broken);
    assert!(
        issues.iter().any(|i| i
            .to_string()
            .contains("borrowed (&ByteBuf) parameter is released")),
        "{issues:?}"
    );
}

#[test]
fn borrow_errors_aliasing_and_new_runtime_failures() {
    let cases: &[(&str, &str)] = &[
        ("fn f(b: &ByteBuf) -> Nat = buf_free(b)", "borrowed by this function"),
        ("fn f(b: &ByteBuf) -> ByteBuf = b", "borrowed by this function"),
        ("fn f(b: &ByteBuf) -> ByteBuf = buf_push(b, 1)", "borrowed by this function"),
        ("fn f(b: ByteBuf) -> ByteBuf = buf_append(b, b)", "borrowed and moved by the same call"),
        ("fn g(x: ByteBuf, y: &ByteBuf) -> Nat = buf_free(x) + buf_len(y)\n    fn f(b: ByteBuf) -> Nat = g(b, b)", "borrowed and moved by the same call"),
        ("fn g(x: &ByteBuf, y: ByteBuf) -> Nat = buf_free(y) + buf_len(x)\n    fn f(b: ByteBuf) -> Nat = g(b, b)", "borrowed and moved by the same call"),
        ("fn g(x: &ByteBuf) -> Nat = buf_len(x)\n    fn f(n: Nat) -> Nat = g(buf_new(n, 0))", "must be a named owner"),
    ];
    for (body, message) in cases {
        let errors = ownership_error(body);
        assert!(errors.contains(message), "{body}\n => {errors}");
    }
    // `&` is only for ByteBuf
    let source = "module test.own\ntheory App {\n    fn f(n: &Nat) -> Nat = n\n}\n";
    let e = format!("{:?}", parse_module(source).unwrap_err());
    assert!(
        e.contains("only ByteBuf, TextBuf and File can be borrowed"),
        "{e}"
    );
    // allowed: shared borrows of one buffer, borrow then move in nested calls
    for body in [
        "fn g(x: &ByteBuf, y: &ByteBuf) -> Bool = buf_eq(x, y)\n    fn f(b: ByteBuf) -> Bool = g(b, b)",
        "fn f(b: ByteBuf) -> ByteBuf = buf_set(b, 0, buf_len(b))",
        "fn f(b: ByteBuf) -> ByteBuf = let c = buf_copy(b) in buf_append(b, c)",
    ] {
        let p = program(body);
        let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
        assert!(report.ok(), "{body}: {:?}", report.diagnostics);
    }
    let failures = [
        ("print_line(nat_to_text(let b = buf_new(3, 0) in let s = buf_slice(b, 2, 2) in buf_len(s)))", "byte buffer range out of bounds", 1),
        ("print_line(nat_to_text(let b = buf_new(3, 0) in let s = buf_slice(b, 340282366920938463463374607431768211455, 2) in buf_len(s)))", "byte buffer range out of bounds", 1),
        ("print_line(nat_to_text(let a = buf_new(268435455, 0) in let c = buf_new(2, 0) in buf_free(buf_append(a, c))))", "byte buffer length exceeds 268435456", 2),
    ];
    for (i, (body, message, live)) in failures.iter().enumerate() {
        let p = program(body);
        let d = Runtime::new()
            .run_program(&p)
            .expect_err("interpreter fails");
        assert!(format!("{d:?}").contains(message), "{i}: {d:?}");
        let (_, err, code) = native(&p, &format!("borrow-error{i}"));
        assert_eq!(code, 1, "{err}");
        assert!(
            err.contains(&format!("dlm runtime error: {message}"))
                && err.contains(&format!(", {live} released at exit")),
            "{i}: {err}"
        );
    }
}

// ---------- v0.104: TextBuf, a second owned resource ----------

#[test]
fn text_buffers_follow_the_same_ownership_rules() {
    let body = r#"
    fn digits(t: TextBuf, i: Nat, n: Nat) -> TextBuf decreasing n =
        if_then_else(nat_eq(n, 0), t, digits(tbuf_push(t, concat(nat_to_text(i), ",")), i + 1, pred(n)))
    fn size(t: &TextBuf) -> Nat = tbuf_len(t)
    fn maybe(t: TextBuf, keep: Bool) -> Text = if_then_else(keep, tbuf_finish(t), "dropped")
    print_line(tbuf_finish(digits(tbuf_new(), 0, 12)))
    print_line(nat_to_text(let t = tbuf_push(tbuf_new(), "héllo мир") in size(t) + tbuf_len(t)))
    print_line(let t = tbuf_new() in tbuf_finish(t))
    print_line(concat(maybe(tbuf_push(tbuf_new(), "a"), true), maybe(tbuf_push(tbuf_new(), "b"), false)))
    print_line(nat_to_text(let b = buf_from_text("xyz") in let t = tbuf_push(tbuf_new(), unwrap_or(buf_to_text(b), "")) in tbuf_len(t) + buf_len(b)))
"#;
    let p = program(body);
    let expected = ["0,1,2,3,4,5,6,7,8,9,10,11,", "18", "", "adropped", "6"];
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(Runtime::new().run_program(&p).unwrap().output, expected);
    let (out, err, code) = native(&p, "textbuf");
    assert_eq!(code, 0, "{err}");
    assert_eq!(out.lines().collect::<Vec<_>>(), expected);
    assert_eq!(
        err.trim(),
        "dlm ownership: 7 buffers created, 7 released in scope, 0 released at exit"
    );
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let f = ir
        .functions
        .iter()
        .find(|f| f.name.ends_with(".maybe"))
        .unwrap();
    assert_eq!(count_releases(&f.body), 1);

    for (body, message) in [
        (
            "fn f(t: TextBuf) -> Text = let s = tbuf_finish(t) in concat(s, tbuf_finish(t))",
            "used after it was moved",
        ),
        (
            "fn f(t: TextBuf) -> Nat = length(list(t))",
            "used as an ordinary value",
        ),
        (
            "fn f(t: &TextBuf) -> Text = tbuf_finish(t)",
            "borrowed by this function",
        ),
        (
            "fn f(t: TextBuf) -> TextBuf = tbuf_push(t, nat_to_text(tbuf_len(tbuf_new())))",
            "must be a named owner",
        ),
        ("record R(t: TextBuf)", "field 't' of record 'R'"),
        ("let g = tbuf_new()", "theory-level value"),
    ] {
        let errors = ownership_error(body);
        assert!(errors.contains(message), "{body}\n => {errors}");
    }
    // a runtime error ends the program; live text and byte buffers are released at exit
    let p = program("print_line(nat_to_text(let t = tbuf_push(tbuf_new(), \"x\") in let b = buf_new(1, 0) in buf_free(buf_set(b, 3, 0)) + tbuf_len(t)))");
    let (_, err, code) = native(&p, "textbuf-error");
    assert_eq!(code, 1);
    assert!(
        err.contains("dlm runtime error: byte buffer index out of range")
            && err.contains(", 2 released at exit"),
        "{err}"
    );
}
