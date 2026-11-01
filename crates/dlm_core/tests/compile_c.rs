//! v0.84 — compiler: IR lowering and the C backend, checked differentially
//! against `dlm run` on the example corpus and on generated programs.

use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, link_program, load_program_with_paths, lower_program, parse_module,
    CheckPolicy, DiagnosticKind, IrOp, Program, Runtime,
};

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn scratch(name: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("dlm_compile_{}_{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

fn have_cc() -> bool {
    Command::new(default_c_compiler()).arg("--version").stdout(Stdio::null()).stderr(Stdio::null()).status().is_ok_and(|s| s.success())
}

/// Compile and run `program`; (stdout as `dlm run` output lines, exit status).
/// A printed Text may itself contain newlines, so lines are compared as the
/// interpreter's output joined with `\n`.
fn compile_and_run(program: &Program, dir: &Path, name: &str, stdin: &str, args: &[&str]) -> (Vec<String>, i32) {
    let ir = lower_program(program, CheckPolicy::research()).unwrap_or_else(|d| panic!("{name}: {d:?}"));
    let exe = dir.join(name);
    let exe = build_c_executable(&emit_c(&ir), &dir.join(format!("{name}.c")), &exe, &default_c_compiler())
        .unwrap_or_else(|d| panic!("{name}: {d}"));
    // Programs may finish without reading stdin. A file avoids a race with
    // the child closing a pipe before the parent writes the test input.
    let input = dir.join(format!("{name}.stdin"));
    std::fs::write(&input, stdin).unwrap();
    let child = Command::new(&exe).args(args).stdin(std::fs::File::open(input).unwrap()).stdout(Stdio::piped()).stderr(Stdio::piped()).spawn().unwrap();
    let output = child.wait_with_output().unwrap();
    let stdout = String::from_utf8_lossy(&output.stdout);
    let text = stdout.strip_suffix('\n').unwrap_or(&stdout).to_string();
    (if stdout.is_empty() { Vec::new() } else { vec![text] }, output.status.code().unwrap_or(-1))
}

fn joined(lines: &[String]) -> Vec<String> {
    if lines.is_empty() { Vec::new() } else { vec![lines.join("\n")] }
}

fn interpret(program: &Program, stdin: &str, args: &[&str]) -> (Vec<String>, i32) {
    let report = Runtime::with_stdin(stdin).with_args(args.iter().map(|a| a.to_string()).collect()).run_program(program).expect("dlm run");
    let code = report.exit_code();
    (joined(&report.output), code)
}

#[test]
fn compiled_examples_agree_with_the_interpreter() {
    assert!(have_cc(), "a C compiler is required for the compiler tests");
    let dir = scratch("examples");
    let mut compiled = Vec::new();
    let mut unsupported = Vec::new();
    let mut entries: Vec<_> = std::fs::read_dir(repo().join("examples/valid")).unwrap().filter_map(|e| e.ok().map(|e| e.path())).collect();
    entries.sort();
    for path in entries.into_iter().filter(|p| p.extension().is_some_and(|e| e == "dlm")) {
        let name = path.file_stem().unwrap().to_string_lossy().into_owned();
        let program = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
        // Only programs that run under the interpreter are compared.
        let Ok(report) = Runtime::with_stdin("5").run_program(&program) else { continue };
        match lower_program(&program, CheckPolicy::research()) {
            Ok(_) => {
                let (lines, code) = compile_and_run(&program, &dir, &name, "5", &[]);
                assert_eq!(lines, joined(&report.output), "{name}: compiled output differs from dlm run");
                assert_eq!(code, report.exit_code(), "{name}: exit status differs");
                compiled.push(name);
            }
            Err(diagnostics) => {
                assert!(diagnostics.iter().all(|d| d.kind == DiagnosticKind::CompileError), "{name}: {diagnostics:?}");
                unsupported.push(name);
            }
        }
    }
    eprintln!("compiled {} examples: {compiled:?}\nnot compilable: {unsupported:?}", compiled.len());
    for expected in ["patterns_and_let", "text_and_io", "word_count", "mutual_recursion", "std_library_usage", "ordinary_higher_order", "integers", "record_schemas", "equational_replay"] {
        assert!(compiled.iter().any(|c| c == expected), "{expected} must compile");
    }
    assert!(compiled.len() >= 12, "{compiled:?}");
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn programs_with_input_arguments_and_exit_status() {
    assert!(have_cc());
    let dir = scratch("io");
    let program = load_program_with_paths(&repo().join("examples/valid/word_count.dlm"), &[]).unwrap();
    for (stdin, args) in [("one two\nthree", vec!["Anton"]), ("", vec![]), ("  a\r\n\nb  c\n", vec!["x", "y"]), ("héllo wörld", vec!["ü"])] {
        assert_eq!(compile_and_run(&program, &dir, "wc", stdin, &args), interpret(&program, stdin, &args), "stdin {stdin:?}");
    }
    let source = "module m\ntheory T {\n    fn show(o: Option<Text>) -> Text = match o { some(t) => t, none => \"<eof>\" }\n    fn main() -> Nat =\n        let a = read_line() in\n        let b = read_line() in\n        let _ = print_line(concat(show(a), concat(\"|\", show(b)))) in\n        let _ = print_line(read_all()) in\n        let _ = print_symbolic(list(some(1), none(Nat))) in\n        let _ = print_symbolic(pair(\"t\", ok(3, Text))) in\n        text_length(show(a)) + 300\n}\n";
    let program = Program::single(parse_module(source).unwrap());
    for stdin in ["first\r\nsecond\nrest\nof it", "only", ""] {
        let compiled = compile_and_run(&program, &dir, "lines", stdin, &[]);
        assert_eq!(compiled, interpret(&program, stdin, &[]), "stdin {stdin:?}");
        assert_eq!(compiled.1, 255, "exit status saturates");
    }
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn runtime_errors_and_deep_recursion() {
    assert!(have_cc());
    let dir = scratch("errors");
    let overflow = Program::single(parse_module("module m\ntheory T {\n    fn big(n: Nat) -> Nat decreasing n = match n { 0 => 1, succ(k) => mul(big(k), 2) }\n    print_decimal(big(130))\n}\n").unwrap());
    let ir = lower_program(&overflow, CheckPolicy::research()).unwrap();
    let exe = dir.join("overflow");
    let exe = build_c_executable(&emit_c(&ir), &dir.join("overflow.c"), &exe, &default_c_compiler()).unwrap();
    let output = Command::new(&exe).stdin(Stdio::null()).output().unwrap();
    assert_eq!(output.status.code(), Some(1));
    assert!(String::from_utf8_lossy(&output.stderr).contains("dlm runtime error: multiplication result overflows"));
    assert!(Runtime::new().run_program(&overflow).is_err(), "the interpreter fails too");

    // Termination is proved, so fuel is erased: the compiled program recurses
    // far deeper than the interpreter's default fuel of 10000.
    let deep = Program::single(parse_module("module m\ntheory T {\n    fn count(n: Nat) -> Nat decreasing n = match n { 0 => 0, succ(k) => count(k) + 1 }\n    print_decimal(count(200000))\n}\n").unwrap());
    let (lines, code) = compile_and_run(&deep, &dir, "deep", "", &[]);
    assert_eq!((lines, code), (vec!["200000".to_string()], 0));
    assert!(Runtime::new().run_program(&deep).is_err(), "the interpreter enforces fuel");
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn lowering_erases_statements_and_reports_unsupported_constructs() {
    let program = Program::single(parse_module("module m\ntheory T {\n    fn d(n: Nat) -> Nat = n + n\n    theorem t(n: Nat): nat_eq(d(n), mul(2, n)) by normalize\n    axiom a(n: Nat): nat_eq(n, n)\n    fn main() -> Nat = d(3)\n}\n").unwrap());
    let ir = lower_program(&program, CheckPolicy::research()).unwrap();
    assert_eq!(ir.erased_statements, 2);
    assert!(ir.main.is_some());
    let text = ir.render();
    assert!(text.starts_with("DLM-IR v3\nmodule: m\nerased statements: 2\n"), "{text}");
    assert!(text.contains("fn f0 T.d(n) captures=0 slots=1\n  (%0 + %0)"), "{text}");
    // Under --trusted-only the axiom blocks the build like any check.
    assert!(lower_program(&program, CheckPolicy::trusted_only()).is_err());

    for (source, needle) in [
        ("    let a = axiom_true()", "'axiom_true' is not supported by the compiler yet"),
        ("    fn d(n: Nat) -> Nat = n\n    theorem t(n: Nat): nat_eq(d(n), n) by normalize\n    let x = t", "statements are erased"),
    ] {
        let program = Program::single(parse_module(&format!("module m\ntheory T {{\n{source}\n}}\n")).unwrap());
        let errors = lower_program(&program, CheckPolicy::research()).unwrap_err();
        assert!(errors.iter().any(|d| d.kind == DiagnosticKind::CompileError && d.message.contains(needle)), "{needle}: {errors:?}");
    }
    // Lambdas capture exactly the free locals they use.
    let program = Program::single(parse_module("module m\ntheory T {\n    fn f(a: Nat, b: Nat) -> Nat = let c = a + 1 in fold(list(1, 2), 0, fun(acc: Nat, x: Nat, acc + x + c))\n}\n").unwrap());
    let ir = lower_program(&program, CheckPolicy::research()).unwrap();
    let lambda = ir.functions.iter().find(|f| f.is_lambda).unwrap();
    assert_eq!((lambda.captures, lambda.params.len()), (1, 2));
    assert!(ir.functions.iter().any(|f| matches!(&f.body.op, IrOp::Let { .. })));
}

/// Random programs over Nat, Bool, lists, options, text, lambdas and `let`:
/// the compiled program must print exactly what the interpreter prints.
#[test]
fn generated_programs_agree_with_the_interpreter() {
    assert!(have_cc());
    let dir = scratch("fuzz");
    struct Rng(u64);
    impl Rng {
        fn below(&mut self, n: u64) -> u64 {
            self.0 ^= self.0 << 13;
            self.0 ^= self.0 >> 7;
            self.0 ^= self.0 << 17;
            self.0 % n
        }
    }
    fn nat(rng: &mut Rng, depth: u32) -> String {
        if depth == 0 || rng.below(4) == 0 {
            return match rng.below(5) {
                0 => "x".to_string(),
                1 => "y".to_string(),
                _ => rng.below(20).to_string(),
            };
        }
        match rng.below(12) {
            0 => format!("{} + {}", nat(rng, depth - 1), nat(rng, depth - 1)),
            1 => format!("mul({}, {})", nat(rng, depth - 1), nat(rng, depth - 1)),
            2 => format!("monus({}, {})", nat(rng, depth - 1), nat(rng, depth - 1)),
            3 => format!("pred({})", nat(rng, depth - 1)),
            4 => format!("if_then_else({} > {}, {}, {})", nat(rng, depth - 1), nat(rng, depth - 1), nat(rng, depth - 1), nat(rng, depth - 1)),
            5 => format!("(let z = {} in z + {})", nat(rng, depth - 1), nat(rng, depth - 1)),
            6 => format!("length({})", list(rng, depth - 1)),
            7 => format!("fold({}, {}, fun(acc: Nat, e: Nat, acc + e + y))", list(rng, depth - 1), nat(rng, depth - 1)),
            8 => format!("unwrap_or(head({}), {})", list(rng, depth - 1), nat(rng, depth - 1)),
            9 => format!("text_length({})", text(rng, depth - 1)),
            10 => format!("(match {} {{ 0 => {}, succ(k) => k + {} }})", nat(rng, depth - 1), nat(rng, depth - 1), nat(rng, depth - 1)),
            _ => format!("sumto({})", rng.below(30)),
        }
    }
    fn list(rng: &mut Rng, depth: u32) -> String {
        match rng.below(4) {
            0 => format!("list({}, {})", nat(rng, depth), nat(rng, depth)),
            1 => format!("map({}, fun(e: Nat, e + x))", list_leaf(rng)),
            2 => format!("cons({}, {})", nat(rng, depth), list_leaf(rng)),
            _ => format!("tail({})", list_leaf(rng)),
        }
    }
    fn list_leaf(rng: &mut Rng) -> String {
        format!("list({}, {}, {})", rng.below(9), rng.below(9), rng.below(9))
    }
    fn text(rng: &mut Rng, depth: u32) -> String {
        match rng.below(4) {
            0 => format!("nat_to_text({})", nat(rng, depth)),
            1 => format!("concat(\"ab\", nat_to_text({}))", nat(rng, depth)),
            2 => "join(words(\" a  bc d \"), \"-\")".to_string(),
            _ => format!("substring(\"héllo wörld\", {}, {})", rng.below(12), rng.below(12)),
        }
    }
    let mut rng = Rng(0x0BAD_C0DE_1234_5678);
    let mut agreed = 0usize;
    for round in 0..12 {
        let mut body = String::from("module m\ntheory T {\n    fn sumto(n: Nat) -> Nat decreasing n = match n { 0 => 0, succ(k) => n + sumto(k) }\n");
        for i in 0..10 {
            body.push_str(&format!("    fn g{i}(x: Nat, y: Nat) -> Nat = {}\n", nat(&mut rng, 4)));
        }
        for i in 0..6 {
            body.push_str(&format!("    fn t{i}(x: Nat, y: Nat) -> Text = {}\n", text(&mut rng, 2)));
            body.push_str(&format!("    fn l{i}(x: Nat, y: Nat) -> List<Nat> = {}\n", list(&mut rng, 2)));
        }
        for i in 0..10 {
            let (a, b) = (rng.below(50), rng.below(50));
            body.push_str(&format!("    print_decimal(g{i}({a}, {b}))\n"));
            body.push_str(&format!("    print_line(t{}({a}, {b}))\n", i % 6));
            body.push_str(&format!("    print_symbolic(l{}({b}, {a}))\n", i % 6));
        }
        body.push_str("}\n");
        let program = Program::single(parse_module(&body).unwrap_or_else(|d| panic!("{d:?}\n{body}")));
        let interpreted = match Runtime::new().run_program(&program) {
            Ok(report) => joined(&report.output),
            Err(err) => {
                eprintln!("round {round}: interpreter error {}", err.message);
                continue; // e.g. overflow: both fail, compared elsewhere
            }
        };
        let (compiled, code) = compile_and_run(&program, &dir, &format!("p{round}"), "", &[]);
        assert_eq!(code, 0);
        assert_eq!(compiled, interpreted, "program:\n{body}");
        agreed += 1;
    }
    assert!(agreed >= 8, "{agreed}");
    std::fs::remove_dir_all(&dir).ok();
    let _ = link_program;
}

/// v0.85: Int arithmetic, Euclidean division, conversions and overflow agree
/// between the C backend and the interpreter.
#[test]
fn int_programs_agree_with_the_interpreter() {
    assert!(have_cc());
    let dir = scratch("ints");
    struct Rng(u64);
    impl Rng {
        fn below(&mut self, n: u64) -> u64 {
            self.0 ^= self.0 << 13;
            self.0 ^= self.0 >> 7;
            self.0 ^= self.0 << 17;
            self.0 % n
        }
    }
    fn int(rng: &mut Rng, depth: u32) -> String {
        if depth == 0 || rng.below(3) == 0 {
            return match rng.below(5) {
                0 => "x".to_string(),
                1 => "y".to_string(),
                2 => format!("int({})", rng.below(9)),
                3 => format!("-{}", 1 + rng.below(9)),
                _ => "int(abs(x))".to_string(),
            };
        }
        let (a, b) = (int(rng, depth - 1), int(rng, depth - 1));
        match rng.below(9) {
            0 | 1 => format!("{a} + {b}"),
            2 | 3 => format!("{a} - {b}"),
            4 => format!("mul({a}, {b})"),
            5 => format!("-({a})"),
            6 => format!("div({a}, {b})"),
            7 => format!("mod({a}, {b})"),
            _ => format!("if_then_else({a} > {b}, {a}, unwrap_or(text_to_int(int_to_text({b})), int(0)))"),
        }
    }
    let mut rng = Rng(0x1234_5678_9ABC_DEF1);
    for round in 0..6 {
        let mut body = String::from("module m\ntheory T {\n");
        for i in 0..12 {
            body.push_str(&format!("    fn h{i}(x: Int, y: Int) -> Int = {}\n", int(&mut rng, 4)));
        }
        for i in 0..12 {
            let lit = |v: i64| if v < 0 { v.to_string() } else { format!("int({v})") };
            let (a, b) = (lit(rng.below(41) as i64 - 20), lit(rng.below(41) as i64 - 20));
            body.push_str(&format!("    print_decimal(h{i}({a}, {b}))\n"));
            body.push_str(&format!("    print_symbolic(list(abs(h{i}({b}, {a})), abs(h{i}({a}, {a}))))\n"));
            body.push_str(&format!("    print_line(int_to_text(h{i}({b}, {b})))\n"));
        }
        body.push_str("}\n");
        let program = Program::single(parse_module(&body).unwrap_or_else(|d| panic!("{d:?}\n{body}")));
        let (compiled, code) = compile_and_run(&program, &dir, &format!("i{round}"), "", &[]);
        assert_eq!(code, 0);
        assert_eq!(compiled, interpret(&program, "", &[]).0, "program:\n{body}");
    }
    // Overflow fails in both, with the same message.
    let min = "-170141183460469231731687303715884105728";
    for (expr, message) in [
        (format!("{min} - int(1)"), "Int subtraction overflows the exact Int range"),
        (format!("-({min})"), "Int negation overflows the exact Int range"),
        (format!("mul({min}, int(2))"), "Int multiplication overflows the exact Int range"),
    ] {
        let program = Program::single(parse_module(&format!("module m\ntheory T {{\n    print_decimal({expr})\n}}\n")).unwrap());
        let err = Runtime::new().run_program(&program).expect_err("interpreter overflow");
        assert!(err.message.contains(message), "{}", err.message);
        let ir = lower_program(&program, CheckPolicy::research()).unwrap();
        let exe = dir.join("ovf");
        let exe = build_c_executable(&emit_c(&ir), &dir.join("ovf.c"), &exe, &default_c_compiler()).unwrap();
        let output = Command::new(&exe).stdin(Stdio::null()).output().unwrap();
        assert_eq!(output.status.code(), Some(1));
        assert!(String::from_utf8_lossy(&output.stderr).contains(message), "{expr}");
    }
    // Negative constants are folded into IR constants.
    let program = Program::single(parse_module("module m\ntheory T {\n    fn f(i: Int) -> Int = i - -3\n}\n").unwrap());
    let ir = lower_program(&program, CheckPolicy::research()).unwrap();
    assert!(ir.render().contains("int(-3)"), "{}", ir.render());
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn tagged_numbers_preserve_word_and_128_bit_boundaries() {
    let dir = scratch("tagged");
    // Includes both sides of the immediate/heap boundary on 64-bit targets.
    let values = ["0", "255", "256", "4611686018427387903", "4611686018427387904", "18446744073709551615", "340282366920938463463374607431768211455"];
    let mut body = String::from("module m\ntheory T {\n    fn round(n: Nat) -> Bool = nat_eq(n, unwrap_or(text_to_nat(nat_to_text(n)), 0))\n");
    for n in values {
        body.push_str(&format!("    print_symbolic(pair({n}, some({n})))\n    print_symbolic({n} + 0)\n    print_symbolic(round({n}))\n"));
    }
    for i in ["int(0)", "int(4611686018427387903)", "int(4611686018427387904)", "-4611686018427387904", "-4611686018427387905", "-170141183460469231731687303715884105728", "int(170141183460469231731687303715884105727)"] {
        body.push_str(&format!("    print_decimal({i} + int(0))\n    print_symbolic(pair({i}, text_to_int(int_to_text({i}))))\n"));
    }
    body.push_str("}\n");
    let p = Program::single(parse_module(&body).unwrap());
    assert_eq!(compile_and_run(&p, &dir, "numbers", "", &[]), interpret(&p, "", &[]));
    std::fs::remove_dir_all(dir).ok();
}

#[test]
fn regions_preserve_escaped_graphs_and_shared_values() {
    let dir = scratch("escape");
    let p = Program::single(parse_module(r#"module m
 theory T {
    record Box(value: List<Option<Text>>)
    fn make(t: Text) -> Box = Box(list(some(concat(t, "!")), none(Text)))
    fn capture(t: Text) -> Fn<Nat, Text> = fun(n: Nat, concat(t, nat_to_text(n)))
    fn alias(t: Text) -> Product<Text, Text> = pair(t, t)
    fn indirect(t: Text) -> Text = unwrap_or(head(list(t)), "bad")
    let g = make("global")
    let f = capture("captured ")
    print_symbolic(g)
    print_symbolic(make("local"))
    print_line(f(4))
    print_symbolic(alias(indirect("shared")))
    print_symbolic(map(list(1, 2, 3), fun(n: Nat, capture(nat_to_text(n)))))
    print_line(unwrap_or(read_line(), "eof"))
    print_line(read_all())
 }
 "#).unwrap());
    assert_eq!(compile_and_run(&p, &dir, "escape", "a\nb\nc", &[]), interpret(&p, "a\nb\nc", &[]));
    std::fs::remove_dir_all(dir).ok();
}

#[test]
fn tail_calls_reuse_regions_for_long_running_programs() {
    let dir = scratch("tail");
    let p = Program::single(parse_module(r#"module m
 theory T {
    fn walk(n: Nat, acc: Text) -> Text decreasing n = match n {
        0 => acc,
        succ(k) => let discarded = words("one two three") in walk(k, nat_to_text(n))
    }
    print_line(walk(500000, "start"))
 }
 "#).unwrap());
    let ir = lower_program(&p, CheckPolicy::research()).unwrap();
    let c = emit_c(&ir);
    assert!(c.contains("continue;"));
    let (output, status) = compile_and_run(&p, &dir, "tail", "", &[]);
    assert_eq!((output, status), (vec!["1".to_string()], 0));
    std::fs::remove_dir_all(dir).ok();
}

#[test]
fn runtime_regions_release_allocations_and_fold_accumulators() {
    let dir = scratch("regions");
    // A C harness can inspect ownership without adding a DLM builtin or
    // making correctness depend on allocator/RSS implementation details.
    let harness = r#"
#undef main
#include <assert.h>
static void dlm_program(void) {}
static size_t blocks(dv_region *r) { return dv_region_count(r); }
static DV *step(DV **env, DV **args) {
    (void)env;
    dv_region region;
    dv_region_enter(&region);
    dv_text("discard", 7);
    assert(blocks(region.parent) < 20);
    return dv_region_leave(&region, dvb_add(args[0], dv_nat_lit(1, 0)));
}
int main(void) {
    for (unsigned i = 0; i < 100000; i++) {
        assert(dv_nat_of(dvb_add(dv_nat(i), dv_nat(1)), "+") == i + 1);
        assert(dv_int_of(dvb___sub(dv_int(-(int_t)i), dv_int(1)), "sub") == -(int_t)i - 1);
    }
    assert(blocks(&dv_root) == 0);
    dv_region outer;
    dv_region_enter(&outer);
    DV *old = dv_text("old", 3);
    dv_region inner;
    dv_region_enter(&inner);
    DV *shared = dv_text("shared", 6);
    DV *captured[] = { shared };
    DV *f = dv_closure(step, 2, "fn:step", 1, captured);
    DV *fields[] = { dv_pair(shared, shared), dv_some(f), dv_result(old, 0), dv_inject(shared, 1) };
    const char *const names[] = { "pair", "fn", "result", "sum" };
    DV *r = dv_record("R", 4, names, fields);
    dv_text("unreachable", 11);
    r = dv_region_leave(&inner, r);
    assert(inner.blocks == 0);
    assert(dvb_fst(dv_field(r, "pair")) == shared);
    assert(dvb_snd(dv_field(r, "pair")) == shared);
    assert(memcmp(shared->u.text.s, "shared", 6) == 0);
    assert(dv_inner(dv_field(r, "fn"))->u.clos.env[0] == shared);
    assert(dv_field(r, "result")->u.res.v == old);
    dv_region_leave(&outer, 0);
    assert(blocks(&dv_root) == 0);
    // Every fold step returns a heap-sized Nat; discarded accumulators must
    // be freed on each iteration, while the new accumulator survives.
    size_t count = 50000;
    DV **items = dv_alloc(sizeof(DV *) * count);
    for (size_t i = 0; i < count; i++) items[i] = dv_nat(0);
    DV *list = dv_list(items, count, 0);
    DV *fold = dvb_fold(list, dv_nat(0), dv_closure(step, 2, "step", 0, 0));
    assert(dv_nat_of(fold, "fold") == (nat_t)count << 64);
    assert(blocks(&dv_root) < 20);
    dv_cleanup();
    assert(dv_current == 0 && dv_root.blocks == 0);
    return 0;
}
"#;
    let c = format!("#define main dlm_runtime_main\n{}\n{harness}", dlm_core::C_RUNTIME);
    let exe = build_c_executable(&c, &dir.join("regions.c"), &dir.join("regions"), &default_c_compiler()).unwrap();
    let output = Command::new(exe).output().unwrap();
    assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
    std::fs::remove_dir_all(dir).ok();
}

#[test]
fn scalar_functions_use_native_128_bit_values_and_preserve_effect_order() {
    let dir = scratch("scalar");
    let p = Program::single(parse_module(r#"module m
 theory T {
    fn wide(n: Nat, x: Int) -> Int decreasing n = if_then_else(nat_eq(n, 0), x, wide(pred(n), x + int(1)))
    fn signed(x: Int) -> Int = mod(div(x - -3, -2), int(7))
    fn choose(b: Bool, x: Nat) -> Nat = if_then_else(not(b), 0, x)
    fn constant(unused: Int) -> Nat = 3
    fn say(n: Nat) -> Nat = let _ = print_line(nat_to_text(n)) in n
    fn ordered() -> Nat = say(1) + say(2)
    print_decimal(wide(100000, int(1267650600228229401496703205376)))
    print_decimal(signed(-100))
    print_decimal(choose(true, 9))
    print_decimal(constant(-9))
    print_decimal(ordered())
 }
 "#).unwrap());
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let c = emit_c(&ir);
    assert!(c.contains("/* scalar T.wide */") && c.contains("static int_t n0(nat_t a0, int_t a1)"));
    assert!(c.contains("/* scalar T.signed */"));
    assert!(!c.contains("/* scalar T.say */") && !c.contains("/* scalar T.ordered */"));
    let (lines, code) = compile_and_run(&p, &dir, "scalar", "", &[]);
    assert_eq!(code, 0);
    assert_eq!(lines, ["1267650600228229401496703305376\n0\n9\n3\n1\n2\n3"]);
    std::fs::remove_dir_all(dir).ok();
}

/// v0.86.1: a group of survivors whose roots are all reachable again is kept
/// whole, so a graph returned through many calls is not traced at every level.
#[test]
fn returned_graphs_are_not_retraced_at_every_level() {
    let dir = scratch("groups");
    let harness = r#"
#undef main
#include <assert.h>
static void dlm_program(void) {}
static DV *id(DV **env, DV **args) { (void)env; return args[0]; }
/* Like `chain(n) = let f = chain(n - 1) in fun(y, f(y) + 1)`: every level
 * returns a closure that captures the closure of the level below. */
static DV *chain(int n, DV *shared) {
    dv_region region;
    dv_region_enter(&region);
    DV *r;
    if (n == 0) {
        DV *captured[] = { shared };
        r = dv_closure(id, 1, "fn:id", 1, captured);
    } else {
        DV *below = chain(n - 1, shared);
        dv_text("garbage", 7);
        DV *captured[] = { below, shared };
        r = dv_closure(id, 1, "fn:level", 2, captured);
    }
    return dv_region_leave(&region, r);
}
static size_t depth(DV *f) {
    size_t n = 0;
    while (f->u.clos.captures == 2) { f = f->u.clos.env[0]; n++; }
    return n;
}
int main(void) {
    dv_region outer;
    dv_region_enter(&outer);
    DV *shared = dv_text("shared", 6);
    size_t levels = 20000;
    dv_scanned = 0;
    DV *f = chain((int)levels, shared);
    /* Linear, not quadratic (levels * levels / 2 = 2e8 before v0.86.1). */
    assert(dv_scanned < 8 * levels);
    assert(depth(f) == levels);
    assert(f->u.clos.env[1] == shared);
    assert(memcmp(shared->u.text.s, "shared", 6) == 0);
    /* Garbage was freed: closures and environments only (2 per level) plus a
     * bounded number of outer blocks. */
    assert(dv_region_count(&outer) <= 2 * (levels + 1) + 4);
    /* Partly reachable groups are split precisely: keeping one level below
     * the top frees everything above it. */
    DV *inner = f->u.clos.env[0];
    for (int i = 0; i < 100; i++) inner = inner->u.clos.env[0];
    dv_region_finish(&outer, inner, 0);
    assert(depth(inner) == levels - 101);
    assert(dv_region_count(&outer) <= 2 * (levels - 100) + 4);
    dv_region_leave(&outer, 0);
    assert(dv_root.blocks == 0);
    dv_cleanup();
    return 0;
}
"#;
    let c = format!("#define main dlm_runtime_main\n{}\n{harness}", dlm_core::C_RUNTIME);
    let exe = build_c_executable(&c, &dir.join("groups.c"), &dir.join("groups"), &default_c_compiler()).unwrap();
    let output = Command::new(exe).output().unwrap();
    assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
    std::fs::remove_dir_all(dir).ok();
}

/// v0.86.1: generated programs that return, split, share and capture parts of
/// results across recursion, tail calls and `fold` agree with `dlm run`.
#[test]
fn generated_sharing_programs_agree_with_the_interpreter() {
    assert!(have_cc());
    let dir = scratch("sharing");
    struct Rng(u64);
    impl Rng {
        fn below(&mut self, n: u64) -> u64 {
            self.0 ^= self.0 << 13;
            self.0 ^= self.0 >> 7;
            self.0 ^= self.0 << 17;
            self.0 % n
        }
        fn pick<'a>(&mut self, items: &[&'a str]) -> &'a str {
            items[self.below(items.len() as u64) as usize]
        }
    }
    fn text(rng: &mut Rng, d: u32, p: bool, f: bool) -> String {
        let mut opts = vec!["a".to_string(), "\"x\"".to_string(), "nat_to_text(n)".to_string(), "unwrap_or(head(xs), \"h\")".to_string()];
        if p {
            opts.push("fst(p)".to_string());
        }
        if f {
            opts.push("f(n)".to_string());
        }
        if d > 0 {
            let c = rng.pick(&["a", "b", "c"]);
            opts.push(format!("concat({}, \"{c}\")", text(rng, d - 1, p, f)));
            opts.push(format!("concat(\"{c}\", {})", text(rng, d - 1, p, f)));
        }
        let i = rng.below(opts.len() as u64) as usize;
        opts.swap_remove(i)
    }
    fn list(rng: &mut Rng, d: u32, p: bool, f: bool) -> String {
        let mut opts = vec!["xs".to_string(), format!("list({}, {})", text(rng, 0, p, f), text(rng, 0, p, f))];
        if p {
            opts.push("snd(p)".to_string());
        }
        if d > 0 {
            opts.push(format!("cons({}, {})", text(rng, d - 1, p, f), list(rng, d - 1, p, f)));
            opts.push(format!("tail({})", list(rng, d - 1, p, f)));
            opts.push(format!("map({}, fun(s: Text, concat(s, {})))", list(rng, d - 1, p, f), text(rng, 0, p, f)));
        }
        let i = rng.below(opts.len() as u64) as usize;
        opts.swap_remove(i)
    }
    fn function(rng: &mut Rng) -> String {
        match rng.below(4) {
            0 => "f".to_string(),
            1 => format!("fun(m: Nat, concat({}, nat_to_text(m)))", text(rng, 0, false, true)),
            2 => "fun(m: Nat, concat(f(m), \"+\"))".to_string(),
            _ => format!("fun(m: Nat, unwrap_or(head({}), f(m)))", list(rng, 0, false, true)),
        }
    }
    let mut rng = Rng(0x5DEE_CE66_D1CE_B00C);
    for round in 0..10 {
        let mut body = String::from("module m\ntheory T {\n");
        for i in 0..4 {
            let callee = format!("r{}", rng.below(i + 1));
            let result = match rng.below(4) {
                0 => format!("pair({}, {})", text(&mut rng, 2, true, true), list(&mut rng, 2, true, true)),
                1 => "p".to_string(),
                2 => format!("pair(fst(p), {})", list(&mut rng, 2, true, true)),
                _ => format!("pair({}, snd(p))", text(&mut rng, 2, true, true)),
            };
            let (t, l, f) = (text(&mut rng, 1, false, true), list(&mut rng, 1, false, true), function(&mut rng));
            body.push_str(&format!(
                "    fn r{i}(n: Nat, a: Text, xs: List<Text>, f: Fn<Nat, Text>) -> Product<Text, List<Text>> decreasing n = match n {{ 0 => pair(a, xs), succ(k) => let p = {callee}(k, {t}, {l}, {f}) in {result} }}\n"
            ));
        }
        for i in 0..3 {
            let (t, l, f) = (text(&mut rng, 2, false, true), list(&mut rng, 2, false, true), function(&mut rng));
            body.push_str(&format!(
                "    fn t{i}(n: Nat, a: Text, xs: List<Text>, f: Fn<Nat, Text>) -> Text decreasing n = match n {{ 0 => concat(a, f(length(xs))), succ(k) => t{i}(k, {t}, {l}, {f}) }}\n"
            ));
        }
        body.push_str("    fn show(p: Product<Text, List<Text>>) -> Text = concat(fst(p), concat(\"|\", join(snd(p), \",\")))\n");
        body.push_str("    fn chain(xs: List<Text>) -> Fn<Nat, Text> = fold(xs, fun(m: Nat, \"e\"), fun(g: Fn<Nat, Text>, s: Text, fun(m: Nat, concat(s, g(m)))))\n");
        for i in 0..4 {
            body.push_str(&format!("    print_line(show(r{i}({}, \"s\", list(\"u\", \"v\"), fun(m: Nat, nat_to_text(m)))))\n", 5 + rng.below(20)));
        }
        for i in 0..3 {
            body.push_str(&format!("    print_line(t{i}({}, \"s\", list(\"w\"), fun(m: Nat, \"z\")))\n", 5 + rng.below(20)));
        }
        body.push_str("    let ch = chain(list(\"1\", \"2\", \"3\"))\n    print_line(ch(7))\n");
        body.push_str("    print_line(fold(list(\"1\", \"2\", \"3\"), \"acc\", fun(acc: Text, s: Text, concat(s, acc))))\n}\n");
        let program = Program::single(parse_module(&body).unwrap_or_else(|d| panic!("{d:?}\n{body}")));
        let (compiled, code) = compile_and_run(&program, &dir, &format!("s{round}"), "", &[]);
        assert_eq!(code, 0, "program:\n{body}");
        assert_eq!(compiled, interpret(&program, "", &[]).0, "program:\n{body}");
    }
    std::fs::remove_dir_all(&dir).ok();
}
