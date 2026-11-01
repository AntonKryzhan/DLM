//! v0.112 — stage E: type classes. `class Name<T> { fn m(x: T) -> R }`, concrete and
//! generic instances (`instance<A: Show> Show<List<A>>`), class bounds on templates,
//! method resolution by inferred argument types; interpreter and native C agree;
//! incomplete, mistyped, duplicate, overlapping and missing instances are rejected.
use dlm_core::load_program_with_paths;
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, lower_program, parse_module, CheckPolicy,
    Checker, Program, Runtime,
};
use std::path::Path;
use std::process::Command;

fn program(body: &str) -> Result<Program, String> {
    parse_module(&format!("module test.classes\ntheory App {{\n{body}\n}}\n"))
        .map(Program::single)
        .map_err(|d| format!("{d:?}"))
}

const BODY: &str = r#"    record P(x: Nat, tag: Text)
    class Show<T> {
        fn show(x: T) -> Text
    }
    class Size<T> {
        fn size(x: T) -> Nat
        fn empty(x: T) -> Bool
    }
    instance Show<Nat> {
        fn show(x: Nat) -> Text = nat_to_text(x)
    }
    instance Show<Text> {
        fn show(x: Text) -> Text = concat("'", concat(x, "'"))
    }
    instance Show<P> {
        fn show(p: P) -> Text = concat("P(", concat(show(field(p, x)), concat(",", concat(show(field(p, tag)), ")"))))
    }
    instance<A: Show> Show<List<A>> {
        fn show(xs: List<A>) -> Text = concat("[", concat(join(map(xs, fun(x: A, show(x))), ","), "]"))
    }
    instance<A: Show> Show<Option<A>> {
        fn show(o: Option<A>) -> Text = match o { none => "none", some(v) => concat("some ", show(v)) }
    }
    instance Size<Text> {
        fn size(x: Text) -> Nat = text_length(x)
        fn empty(x: Text) -> Bool = nat_eq(text_length(x), 0)
    }
    instance<A> Size<List<A>> {
        fn size(x: List<A>) -> Nat = length(x)
        fn empty(x: List<A>) -> Bool = is_empty(x)
    }
    fn describe<T: Show + Size>(x: T) -> Text = concat(show(x), concat(" size ", nat_to_text(size(x))))
    print_line(show(5))
    print_line(show("hi"))
    print_line(show(list(1, 2, 3)))
    print_line(show(list(list("a"), empty_list(Text))))
    print_line(show(P(1, "one")))
    print_line(show(list(some(P(2, "two")), none(P))))
    print_line(describe("abc"))
    print_line(describe(list(4, 5)))
    print_line(if_then_else(empty(empty_list(Nat)), "empty", "full"))
"#;

#[test]
fn classes_and_instances_agree_between_interpreter_and_native() {
    let p = program(BODY).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let interpreted = Runtime::new().run_program(&p).unwrap().output;
    let dir = std::env::temp_dir().join(format!("dlm-classes-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(
        &emit_c(&ir),
        &dir.join("main.c"),
        &dir.join("main"),
        &default_c_compiler(),
    )
    .unwrap();
    let out = Command::new(exe).output().unwrap();
    let native: Vec<String> = String::from_utf8(out.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect();
    assert_eq!(interpreted, native);
    assert_eq!(
        native,
        [
            "5",
            "'hi'",
            "[1,2,3]",
            "[['a'],[]]",
            "P(1,'one')",
            "[some P(2,'two'),none]",
            "'abc' size 3",
            "[4,5] size 2",
            "empty"
        ]
    );
    std::fs::remove_dir_all(dir).ok();
}

const SHOW: &str = "    class Show<T> {\n        fn show(x: T) -> Text\n    }\n    instance Show<Nat> {\n        fn show(x: Nat) -> Text = nat_to_text(x)\n    }\n";

fn errors(extra: &str) -> String {
    match program(&format!("{SHOW}{extra}")) {
        Err(e) => e,
        Ok(p) => {
            let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
            assert!(!report.ok(), "{extra}");
            report
                .diagnostics
                .iter()
                .map(|d| d.message.clone())
                .collect::<Vec<_>>()
                .join("\n")
        }
    }
}

#[test]
fn malformed_classes_and_instances_are_rejected() {
    let cases = [
        ("    print_line(show(\"a\"))", "no instance `Show<Text>` provides method 'show'"),
        ("    instance Show<Text> {\n        fn shw(x: Text) -> Text = x\n    }", "does not define method 'show'"),
        ("    instance Show<Text> {\n        fn shw(x: Text) -> Text = x\n    }", "'shw' is not a method of class `Show`"),
        ("    instance Show<Bool> {\n        fn show(x: Bool) -> Nat = 0\n    }", "must have type (Bool) -> Text"),
        ("    instance Show<Nat> {\n        fn show(x: Nat) -> Text = \"dup\"\n    }", "duplicate instance `Show<Nat>`"),
        ("    fn d<T: Show>(x: T) -> Text = show(x)\n    print_line(d(true))", "type `Bool` does not satisfy `Show`"),
        ("    instance Eqq<Nat> {\n        fn f(x: Nat) -> Nat = x\n    }", "instance of unknown class `Eqq`"),
        ("    class Bad<T>: {\n        fn m(x: T) -> T = x\n    }", "invalid superclass"),
        ("    class Two<A, B> {\n        fn m(x: A) -> B\n    }", "expected: class Name<T>"),
        ("    instance<A> Show<List<A>> {\n        fn show(x: List<A>) -> Text = \"l\"\n    }\n    instance<B> Show<List<B>> {\n        fn show(x: List<B>) -> Text = \"m\"\n    }\n    print_line(show(list(1)))", "overlapping instances of `Show` for `List<Nat>`"),
        ("    instance Show<Text> {\n        fn show(x: Text) -> Text = x\n        print_line(\"no\")\n    }", "only fn declarations may appear in a class or instance block"),
    ];
    for (extra, message) in cases {
        let e = errors(extra);
        assert!(e.contains(message), "{extra}\n{e}");
    }
}

const STD_CLASSES: &str = r#"module t
import std.classes
theory App extends StdClasses {
    record P(x: Nat, tag: Text)
    instance Show<P> {
        fn show(p: P) -> Text = concat("P ", concat(show(field(p, x)), concat(" ", show(field(p, tag)))))
    }
    print_line(show(list(1, 2)))
    print_line(show(list(some(P(1, "a")), none(P))))
    print_line(show(int(0) - int(3)))
    print_line(nat_to_text(mconcat(list(1, 2, 3), 0)))
    print_line(mconcat(list("a", "b"), ""))
    print_line(show(mconcat(list(list(1), list(2, 3)), empty_list(Nat))))
    print_line(show_all(list(true, false)))
    print_line(concat(mtimes("ab", 3), nat_to_text(mtimes(5, 4))))
}
"#;

#[test]
fn std_classes_with_user_instances_agree_between_interpreter_and_native() {
    let dir = std::env::temp_dir().join(format!("dlm-std-classes-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(dir.join("main.dlm"), STD_CLASSES).unwrap();
    let p = load_program_with_paths(
        &dir.join("main.dlm"),
        &[Path::new(env!("CARGO_MANIFEST_DIR")).join("../../lib")],
    )
    .unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let interpreted: Vec<String> = Runtime::new()
        .run_program(&p)
        .unwrap()
        .output
        .join("\n")
        .lines()
        .map(str::to_string)
        .collect();
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(
        &emit_c(&ir),
        &dir.join("main.c"),
        &dir.join("main"),
        &default_c_compiler(),
    )
    .unwrap();
    let native: Vec<String> = String::from_utf8(Command::new(exe).output().unwrap().stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect();
    assert_eq!(interpreted, native);
    assert_eq!(
        native,
        [
            "[1, 2]",
            "[some(P 1 \"a\"), none]",
            "-3",
            "6",
            "ab",
            "[1, 2, 3]",
            "true",
            "false",
            "ababab20"
        ]
    );
    std::fs::remove_dir_all(dir).ok();
}

const HIERARCHY: &str = r#"    record P(x: Nat, tag: Text)
    class Same<T> {
        fn same(a: T, b: T) -> Bool
        fn differ(a: T, b: T) -> Bool = not(same(a, b))
    }
    class Before<T>: Same {
        fn before(a: T, b: T) -> Bool
        fn max_of(a: T, b: T) -> T = if_then_else(before(a, b), b, a)
        fn between(a: T, x: T, b: T) -> Bool = and(or(before(a, x), same(a, x)), or(before(x, b), same(x, b)))
    }
    instance Same<Nat> {
        fn same(a: Nat, b: Nat) -> Bool = nat_eq(a, b)
    }
    instance Before<Nat> {
        fn before(a: Nat, b: Nat) -> Bool = b > a
    }
    instance Same<P> {
        fn same(a: P, b: P) -> Bool = nat_eq(field(a, x), field(b, x))
        fn differ(a: P, b: P) -> Bool = text_eq(field(a, tag), "always")
    }
    instance<A: Same> Same<List<A>> {
        fn same(a: List<A>, b: List<A>) -> Bool = and(nat_eq(length(a), length(b)), fold(a, true, fun(acc: Bool, x: A, and(acc, fold(b, false, fun(f: Bool, y: A, or(f, same(x, y))))))))
    }
    fn largest<T: Before>(xs: List<T>, d: T) -> T = fold(xs, d, fun(m: T, x: T, max_of(m, x)))
    print_line(if_then_else(differ(1, 2), "differ", "same"))
    print_line(if_then_else(differ(P(1, "a"), P(1, "always")), "always", "no"))
    print_line(nat_to_text(max_of(3, 9)))
    print_line(if_then_else(between(1, 5, 9), "in", "out"))
    print_line(nat_to_text(largest(list(4, 11, 7), 0)))
    print_line(if_then_else(differ(list(1, 2), list(2, 1)), "differ", "same"))
"#;

#[test]
fn superclasses_and_default_methods_agree_between_interpreter_and_native() {
    let p = program(HIERARCHY).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let interpreted = Runtime::new().run_program(&p).unwrap().output;
    let dir = std::env::temp_dir().join(format!("dlm-classes-h-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(
        &emit_c(&ir),
        &dir.join("main.c"),
        &dir.join("main"),
        &default_c_compiler(),
    )
    .unwrap();
    let native: Vec<String> = String::from_utf8(Command::new(exe).output().unwrap().stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect();
    assert_eq!(interpreted, native);
    assert_eq!(native, ["differ", "no", "9", "in", "11", "same"]);
    std::fs::remove_dir_all(dir).ok();
}

#[test]
fn superclass_requirements_are_checked() {
    let base = "    class Same<T> {\n        fn same(a: T, b: T) -> Bool\n        fn differ(a: T, b: T) -> Bool = not(same(a, b))\n    }\n    class Before<T>: Same {\n        fn before(a: T, b: T) -> Bool\n    }\n";
    let cases = [
        ("    instance Before<Nat> {\n        fn before(a: Nat, b: Nat) -> Bool = b > a\n    }", "instance `Before<Nat>` requires an instance `Same<Nat>` (superclass)"),
        ("    class Loop<T>: Loop {\n        fn l(x: T) -> T\n    }", "cannot be its own superclass"),
        ("    class Up<T>: Nope {\n        fn u(x: T) -> T\n    }", "superclass `Nope` of class `Up` is not a class"),
        ("    instance<A> Before<List<A>> {\n        fn before(a: List<A>, b: List<A>) -> Bool = false\n    }", "requires an instance `Same<List<Record<A>>>`"),
    ];
    for (extra, message) in cases {
        let text = match program(&format!("{base}{extra}")) {
            Err(e) => e,
            Ok(p) => Checker::with_policy(CheckPolicy::trusted_only())
                .check_program(&p)
                .diagnostics
                .iter()
                .map(|d| d.message.clone())
                .collect::<Vec<_>>()
                .join("\n"),
        };
        assert!(text.contains(message), "{extra}\n{text}");
    }
    // a generic instance satisfies its superclass through another generic instance
    let ok = format!("{base}    instance<A> Same<List<A>> {{\n        fn same(a: List<A>, b: List<A>) -> Bool = nat_eq(length(a), length(b))\n    }}\n    instance<A> Before<List<A>> {{\n        fn before(a: List<A>, b: List<A>) -> Bool = length(b) > length(a)\n    }}\n    print_line(if_then_else(differ(list(1), list(2, 3)), \"d\", \"s\"))");
    let p = program(&ok).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(Runtime::new().run_program(&p).unwrap().output, ["d"]);
}

fn checked_both(body: &str, name: &str) -> Vec<String> {
    let p = program(body).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{name}: {:?}", report.diagnostics);
    let interpreted = Runtime::new().run_program(&p).unwrap().output;
    let dir = std::env::temp_dir().join(format!("dlm-classes-reg-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let exe = build_c_executable(
        &emit_c(&lower_program(&p, CheckPolicy::trusted_only()).unwrap()),
        &dir.join("main.c"),
        &dir.join("main"),
        &default_c_compiler(),
    )
    .unwrap();
    let out = Command::new(exe).output().unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let native: Vec<String> = String::from_utf8(out.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect();
    assert_eq!(interpreted, native, "{name}");
    std::fs::remove_dir_all(dir).unwrap();
    native
}

const BOUNDED_SHOW: &str =
    "instance<A: Show> Show<List<A>> {\n fn show(x: List<A>) -> Text = \"list\"\n}\n";

#[test]
fn instance_bounds_and_superclass_bounds_are_checked_transitively() {
    let missing = format!("{BOUNDED_SHOW}fn ignore<T: Show>(x: T) -> Nat = 0\nprint_line(nat_to_text(ignore(list(true))))");
    assert!(errors(&missing).contains("does not satisfy `Show`"));
    let other = "class Other<T>: Show {\n fn other(x: T) -> Nat\n}\n";
    let missing = format!("{BOUNDED_SHOW}{other}instance Other<List<Bool>> {{\n fn other(x: List<Bool>) -> Nat = 0\n}}\nprint_line(nat_to_text(other(list(true))))");
    assert!(errors(&missing).contains("requires an instance `Show<List<Bool>>`"));
    let valid = format!("{SHOW}{BOUNDED_SHOW}{other}instance<A: Show> Other<List<A>> {{\n fn other(x: List<A>) -> Nat = 0\n}}\nfn ignore<T: Show>(x: T) -> Nat = 0\nprint_line(nat_to_text(ignore(list(1))))\nprint_line(nat_to_text(other(list(1))))");
    assert_eq!(checked_both(&valid, "bounds"), ["0", "0"]);
    // A matching instance whose own requirement loops is not evidence of itself.
    let missing = "class C<T> {\n fn c(x: T) -> Nat\n}\ninstance<A: C> C<A> {\n fn c(x: A) -> Nat = 0\n}\nfn ignore<T: C>(x: T) -> Nat = 0\nprint_line(nat_to_text(ignore(true)))";
    assert!(errors(missing).contains("does not satisfy `C`"));
    let mut nested = "1".to_string();
    for _ in 0..24 {
        nested = format!("list({nested})");
    }
    let repeated = format!("class C<T> {{\n fn c(x: T) -> Nat\n}}\ninstance C<Nat> {{\n fn c(x: Nat) -> Nat = 0\n}}\ninstance<A: C + C> C<List<A>> {{\n fn c(x: List<A>) -> Nat = 0\n}}\nfn ignore<T: C>(x: T) -> Nat = 0\nprint_line(nat_to_text(ignore({nested})))");
    assert_eq!(checked_both(&repeated, "bounds-repeated"), ["0"]);
}

#[test]
fn superclass_cycles_and_concrete_generic_overlaps_are_rejected() {
    let cycle = "class A<T>: B {\n fn a(x: T) -> Nat\n}\nclass B<T>: A {\n fn b(x: T) -> Nat\n}\ninstance A<Nat> {\n fn a(x: Nat) -> Nat = 1\n}\ninstance B<Nat> {\n fn b(x: Nat) -> Nat = 2\n}\nprint_line(nat_to_text(a(1)))";
    assert!(errors(cycle).contains("cyclic superclass requirements"));
    let overlap = "instance<A> Show<A> {\n fn show(x: A) -> Text = \"any\"\n}\nprint_line(show(1))";
    assert!(errors(overlap).contains("overlapping concrete and generic instances"));
}

#[test]
fn defaults_have_distinct_templates_and_dependencies_precede_callers() {
    let body = r#"class C<T> {
 fn id(x: T) -> T
 fn num(x: T) -> Nat = 1
 fn second(x: T) -> Text = "ok"
 fn first(x: T) -> Text = second(x)
 fn same_value(T: T) -> T = T
}
instance<A> C<List<A>> {
 fn id(x: List<A>) -> List<A> = x
}
instance<A> C<Option<A>> {
 fn id(x: Option<A>) -> Option<A> = x
}
instance C<Nat> {
 fn id(x: Nat) -> Nat = x
}
instance C<Text> {
 fn id(x: Text) -> Text = x
 fn first(x: Text) -> Text = "override"
}
print_line(nat_to_text(num(list(1))))
print_line(nat_to_text(num(some(1))))
print_line(first(1))
print_line(first("x"))
print_line(nat_to_text(length(same_value(list(1, 2)))))
"#;
    assert_eq!(
        checked_both(body, "defaults"),
        ["1", "1", "ok", "override", "2"]
    );
}

#[test]
fn eq_checks_the_complete_record_graph() {
    let make = |leaf: &str, value: &str| {
        let mut body = format!("record R0(value: {leaf})\n");
        let mut v = format!("R0({value})");
        for i in 1..=36 {
            body += &format!("record R{i}(value: R{})\n", i - 1);
            v = format!("R{i}({v})");
        }
        body += &format!("print_line(if_then_else(equal({v}, {v}), \"yes\", \"no\"))\n");
        body
    };
    assert!(errors(&make("Fn<Nat, Nat>", "fun(x: Nat, x)")).contains("equal needs a `Eq` type"));
    assert_eq!(checked_both(&make("Nat", "7"), "deep-eq"), ["yes"]);
}

#[test]
fn inference_uses_builtin_results_and_named_function_signatures() {
    let body = r#"fn accept<T>(x: T) -> Nat = 0
fn function_arg<T>(f: Fn<T, T>) -> Nat = 1
fn inc(x: Nat) -> Nat = x + 1
print_line(nat_to_text(accept(args())))
print_line(nat_to_text(accept(read_line())))
print_line(nat_to_text(accept(inl(1, Text))))
print_line(nat_to_text(accept(inr("x", Nat))))
print_line(nat_to_text(function_arg(inc)))
"#;
    assert_eq!(checked_both(body, "inference"), ["0", "0", "0", "0", "1"]);
}
