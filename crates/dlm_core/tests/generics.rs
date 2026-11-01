//! v0.110 — stage E-min: generic functions and records with explicit type
//! arguments, monomorphized before checking (`crate::generics`): in-module and
//! imported templates (`std.collections`), interpreter and native C agree,
//! instance names are injective, and malformed uses are rejected.
use dlm_core::generics::{demangle, encode_generic_text, mangle};
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    parse_module, CheckPolicy, Checker, ModuleItem, Program, Runtime, TheoryItem,
};
use std::path::{Path, PathBuf};
use std::process::Command;

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn both(p: &Program, name: &str) -> Vec<String> {
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(p);
    assert!(report.ok(), "{name}: {:?}", report.diagnostics);
    let interpreted = Runtime::new().run_program(p).unwrap().output;
    let dir = std::env::temp_dir().join(format!("dlm-generics-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let ir = lower_program(p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(
        &emit_c(&ir),
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
    std::fs::remove_dir_all(dir).ok();
    native
}

fn single(body: &str) -> Program {
    let source = format!("module test.generics\ntheory App {{\n{body}\n}}\n");
    Program::single(parse_module(&source).unwrap_or_else(|d| panic!("{d:?}")))
}

const LOCAL: &str = r#"    record Box<T>(value: T)
    record Pair2<A, B>(first: A, second: B)
    fn id<T>(x: T) -> T = x
    fn swap<A, B>(p: Pair2<A, B>) -> Pair2<B, A> = Pair2<B, A>(field(p, second), field(p, first))
    fn len<T>(xs: List<T>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, 1 + len<T>(tail(xs)))
    fn first_or<T>(xs: List<T>, d: T) -> T = unwrap_or(head(xs), d)
    fn wrap_all<T>(xs: List<T>) -> List<Box<T>> = map(xs, fun(x: T, Box<T>(x)))
    fn none_of<T>(x: T) -> Option<T> = none(T)
    fn apply_twice<T>(f: Fn<T, T>, x: T) -> T = f(f(x))
    print_line(nat_to_text(id<Nat>(5)))
    print_line(id<Text>("hello"))
    print_line(field(swap<Nat, Text>(Pair2<Nat, Text>(1, "one")), first))
    print_line(nat_to_text(len<Text>(list("a", "b", "c"))))
    print_line(nat_to_text(len<Nat>(list(1, 2))))
    print_line(nat_to_text(field(first_or<Box<Nat>>(wrap_all<Nat>(list(7, 8)), Box<Nat>(0)), value)))
    print_line(if_then_else(is_none(none_of<List<Nat>>(list(1))), "none", "some"))
    print_line(nat_to_text(apply_twice<Nat>(fun(n: Nat, n + 3), 1)))
    print_line(field(field(id<Box<Box<Text>>>(Box<Box<Text>>(Box<Text>("deep"))), value), value))"#;

#[test]
fn local_templates_agree_between_interpreter_and_native() {
    let p = single(LOCAL);
    let root = p.root();
    let ModuleItem::Theory(t) = &root.items[0] else {
        panic!()
    };
    let names: Vec<String> = t
        .items
        .iter()
        .filter_map(|i| match i {
            TheoryItem::Fn(f) => Some(f.name.clone()),
            TheoryItem::Record(r) => Some(r.name.clone()),
            _ => None,
        })
        .collect();
    assert!(
        !names.iter().any(|n| n == "id" || n == "Box" || n == "len"),
        "templates removed: {names:?}"
    );
    for n in [
        "id__of__Nat",
        "id__of__Text",
        "len__of__Nat",
        "len__of__Text",
        "Box__of__Nat",
        "Box__of__Record_lBox_u_uof_u_uText_r",
    ] {
        assert!(names.iter().any(|m| m == n), "{n} in {names:?}");
    }
    assert_eq!(
        both(&p, "local"),
        ["5", "hello", "one", "3", "2", "7", "none", "7", "deep"]
    );
}

#[test]
fn imported_collections_agree_between_interpreter_and_native() {
    let source = r#"module test.collections
import std.collections
theory App extends StdCollections {
    fn nats(xs: List<Nat>) -> Text = join(map(xs, fun(x: Nat, nat_to_text(x))), ",")
    fn neq(a: Nat, b: Nat) -> Bool = nat_eq(a, b)
    fn teq(a: Text, b: Text) -> Bool = text_eq(a, b)
    fn lt(a: Nat, b: Nat) -> Bool = b > a
    print_line(nats(list_reverse<Nat>(list(1, 2, 3))))
    print_line(nats(list_append<Nat>(list(1, 2), list(3))))
    print_line(nats(list_filter<Nat>(list(1, 2, 3, 4), fun(x: Nat, nat_eq(mod(x, 2), 0)))))
    print_line(nats(list_sort_by<Nat>(list(5, 3, 9, 1, 3), lt)))
    print_line(join(list_sort_by<Text>(list("pear", "fig"), fun(a: Text, b: Text, text_length(b) > text_length(a))), ","))
    print_line(nats(list_take<Nat>(list(1, 2, 3, 4), 2)))
    print_line(nats(list_drop<Nat>(list(1, 2, 3, 4), 3)))
    print_line(nat_to_text(unwrap_or(list_max_by<Nat>(list(4, 9, 2), fun(a: Nat, b: Nat, a > b)), 0)))
    print_line(join(map(list_zip<Nat, Text>(list(1, 2, 3), list("a", "b")), fun(d: Duo<Nat, Text>, concat(nat_to_text(field(d, left)), field(d, right)))), " "))
    print_line(nat_to_text(unwrap_or(stack_peek<Nat>(stack_pop<Nat>(stack_push<Nat>(stack_push<Nat>(stack_new<Nat>(0), 1), 2))), 0)))
    print_line(nat_to_text(unwrap_or(queue_peek<Nat>(queue_pop<Nat>(queue_push<Nat>(queue_push<Nat>(queue_empty<Nat>(0), 1), 2))), 0)))
    print_line(let m = map_put<Text, Nat>(map_put<Text, Nat>(map_put<Text, Nat>(map_empty<Text, Nat>("", 0), "a", 1, teq), "b", 2, teq), "a", 3, teq) in concat(nat_to_text(unwrap_or(map_get<Text, Nat>(m, "a", teq), 0)), concat(" ", concat(nat_to_text(map_size<Text, Nat>(m)), concat(" ", join(map_keys<Text, Nat>(m), ","))))))
    print_line(if_then_else(list_contains<Nat>(list(1, 2), 2, neq), "yes", "no"))
    print_line(nat_to_text(list_count<Text>(list("a", "bb", "c"), fun(t: Text, nat_eq(text_length(t), 1)))))
}

"#;
    let dir = std::env::temp_dir().join(format!("dlm-generics-src-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    assert_eq!(
        both(&p, "collections"),
        [
            "3,2,1",
            "1,2,3",
            "2,4",
            "1,3,3,5,9",
            "fig,pear",
            "1,2",
            "4",
            "9",
            "1a 2b",
            "1",
            "2",
            "3 2 a,b",
            "yes",
            "2"
        ]
    );
    std::fs::remove_dir_all(dir).ok();
}

fn errors(body: &str) -> String {
    match parse_module(&format!(
        "module test.generics\ntheory App {{\n{body}\n}}\n"
    )) {
        Err(d) => format!("{d:?}"),
        Ok(m) => {
            let report = Checker::with_policy(CheckPolicy::trusted_only())
                .check_program(&Program::single(m));
            assert!(!report.ok(), "{body}");
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
fn malformed_generic_uses_are_rejected() {
    let cases = [
        ("    fn id<T>(x: T) -> T = x\n    print_line(nat_to_text(id<Nat, Text>(5)))", "generic 'id' expects 1 type argument(s), got 2"),
        ("    print_line(nat_to_text(nope<Nat>(5)))", "nope__of__Nat"),
        ("    fn deep<T>(x: T, n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, deep<List<T>>(list(x), pred(n)))\n    print_line(nat_to_text(deep<Nat>(1, 3)))", "polymorphic recursion"),
        ("    fn bad<T>(x: T) -> Nat = x + 1\n    print_line(nat_to_text(bad<Text>(\"a\")))", "must support Nat addition"),
        ("    fn f<t>(x: Nat) -> Nat = x", "type parameter `t` must be a capitalized name"),
        ("    fn f<T, T>(x: Nat) -> Nat = x", "type parameter `T` is declared twice"),
        ("    record R<List>(x: Nat)", "type parameter `List` must be a capitalized name"),
        ("    fn id<T>(x: T) -> T = x\n    print_line(nat_to_text(id<Nat(5)))", "unclosed type argument list"),
        ("    fn id<T>(x: T) -> T = x\n    print_line(nat_to_text(id<Nat))", "unclosed type argument list"),
        ("    fn forge(x: StaticProof<Nat>) -> Nat = 0", "unknown record type 'StaticProof__of__Nat'"),
    ];
    for (body, message) in cases {
        let e = errors(body);
        assert!(e.contains(message), "{body}\n{e}");
        assert!(!e.contains("unknown record type 'T'"), "{body}\n{e}");
    }
}

#[test]
fn instance_names_are_injective_and_decodable() {
    let cases: Vec<Vec<String>> = vec![
        vec!["Nat".into()],
        vec!["List<Nat>".into()],
        vec!["Record<Box__of__Nat>".into(), "Text".into()],
        vec!["Record<A_B>".into()],
        vec!["Record<A>".into(), "Record<B>".into()],
        vec!["Record<A,B>".into()],
        vec!["Fn<Nat,Bool>".into()],
        vec!["ProductType<Nat*Text>".into()],
        vec!["SumType<Nat+Text>".into()],
    ];
    let names: Vec<String> = cases.iter().map(|a| mangle("Box", a)).collect();
    for (name, args) in names.iter().zip(&cases) {
        assert!(
            name.chars().all(|c| c.is_ascii_alphanumeric() || c == '_'),
            "{name}"
        );
        let (head, back) = demangle(name).unwrap();
        assert_eq!(head, "Box");
        if args.len() == 1 && args[0] == "Record<A,B>" {
            continue; // a record argument is never `A,B`; the split is by top-level commas
        }
        assert_eq!(&back, args, "{name}");
    }
    let unique: std::collections::BTreeSet<_> = names.iter().collect();
    assert_eq!(unique.len(), names.len());
    assert_eq!(demangle("plain"), None);
    assert_eq!(demangle("bad__of__x__y"), None);
    assert_eq!(
        encode_generic_text("f(List<Nat>, g<Nat>(x)) > 3").unwrap(),
        "f(List<Nat>, g__of__Nat(x)) > 3"
    );
    assert_eq!(
        encode_generic_text("Box<Box<Nat>>(Box<Nat>(1))").unwrap(),
        "Box__of__Record_lBox_u_uof_u_uNat_r(Box__of__Nat(1))"
    );
}

#[test]
fn private_templates_give_private_instances() {
    let dir = std::env::temp_dir().join(format!("dlm-generics-private-{}", std::process::id()));
    std::fs::create_dir_all(dir.join("lib")).unwrap();
    std::fs::write(dir.join("lib/gen.dlm"), "module lib.gen\ntheory Gen {\n    private fn helper<T>(x: T) -> T = x\n    fn pub_id<T>(x: T) -> T = helper<T>(x)\n}\n").unwrap();
    std::fs::write(dir.join("ok.dlm"), "module ok\nimport lib.gen\ntheory App extends Gen {\n    print_line(pub_id<Text>(\"visible\"))\n}\n").unwrap();
    std::fs::write(dir.join("bad.dlm"), "module bad\nimport lib.gen\ntheory App extends Gen {\n    print_line(helper<Text>(\"hidden\"))\n}\n").unwrap();
    let ok = load_program_with_paths(&dir.join("ok.dlm"), &[dir.clone()]).unwrap();
    assert_eq!(both(&ok, "private"), ["visible"]);
    let bad = load_program_with_paths(&dir.join("bad.dlm"), &[dir.clone()]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&bad);
    assert!(!report.ok());
    std::fs::remove_dir_all(dir).ok();
}

#[test]
fn inferred_type_arguments_and_bounds_agree_between_interpreter_and_native() {
    let source = r#"module test.inference
import std.collections
theory App extends StdCollections {
    record P(x: Nat, tag: Text)
    fn nats(xs: List<Nat>) -> Text = join(map(xs, fun(x: Nat, nat_to_text(x))), ",")
    print_line(nats(list_reverse(list(1, 2, 3))))
    print_line(nats(list_sort(list(5, 3, 9, 1, 3))))
    print_line(join(list_sort(list("pear", "apple", "fig")), ","))
    print_line(nats(list_dedup(list(1, 2, 1, 3, 2))))
    print_line(nat_to_text(unwrap_or(list_index_of(list("a", "b", "c"), "c"), 99)))
    print_line(if_then_else(list_contains_eq(list(P(1, "a"), P(2, "b")), P(2, "b")), "yes", "no"))
    print_line(nat_to_text(unwrap_or(list_min(list(4, 2, 8)), 0)))
    print_line(int_to_text(unwrap_or(list_max(list(int(0) - int(4), int(2))), int(0))))
    print_line(let m = map_put_eq(map_put_eq(map_empty<Text, Nat>("", 0), "k", 1), "k", 2) in nat_to_text(unwrap_or(map_get_eq(m, "k"), 0)))
    print_line(nat_to_text(stack_size(stack_push(stack_new(0), 5))))
    print_line(nats(map(list_zip(list(1, 2), list("x", "y")), fun(d: Duo<Nat, Text>, field(d, left)))))
    print_line(if_then_else(equal(list(some(1), none(Nat)), list(some(1), none(Nat))), "eq", "ne"))
    print_line(if_then_else(or(less("ab", "a"), equal(P(1, "a"), P(1, "b"))), "bad", "ok"))
    print_line(field(Duo(7, "seven"), right))
}
"#;
    let dir = std::env::temp_dir().join(format!("dlm-generics-infer-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    assert_eq!(
        both(&p, "inference"),
        [
            "3,2,1",
            "1,3,3,5,9",
            "apple,fig,pear",
            "1,2,3",
            "2",
            "yes",
            "2",
            "2",
            "2",
            "1",
            "1,2",
            "eq",
            "ok",
            "seven"
        ]
    );
    std::fs::remove_dir_all(dir).ok();
}

#[test]
fn bounds_and_inference_failures_are_reported() {
    let cases = [
        ("    fn s<T: Ord>(x: T) -> T = x\n    print_line(nat_to_text(length(s<List<Nat>>(list(1)))))", "does not satisfy `Ord` required by `T` of 's'"),
        ("    fn s<T: Eq>(x: T) -> Nat = 0\n    print_line(nat_to_text(s(fun(n: Nat, n))))", "does not satisfy `Eq`"),
        ("    fn mk<T>(n: Nat) -> List<T> = empty_list(T)\n    print_line(nat_to_text(length(mk(1))))", "cannot infer the type arguments of `mk`"),
        ("    fn f<T: show>(x: T) -> T = x", "invalid bound `show`"),
        ("    print_line(if_then_else(equal(fun(n: Nat, n), fun(n: Nat, n)), \"a\", \"b\"))", "equal needs a `Eq` type"),
        ("    print_line(if_then_else(less(true, false), \"a\", \"b\"))", "less needs a `Ord` type"),
        ("    print_line(if_then_else(equal(1, \"a\"), \"a\", \"b\"))", "equal expects `Nat`"),
    ];
    for (body, message) in cases {
        let e = errors(body);
        assert!(e.contains(message), "{body}\n{e}");
    }
}

#[test]
fn substitution_preserves_value_bindings_and_field_names() {
    let p = single(
        r#"    record Named<T>(T: T)
    fn get<T>(x: Named<T>) -> T = field(x, T)
    fn parameter<T>(T: T) -> T = T
    fn local<T>(x: T) -> T = let T = x in T
    fn lambda<T>(x: T) -> T = let f = fun(T: T, T) in f(x)
    fn types<T>(x: T) -> Result<List<T>, T> = ok(empty_list(T), T)
    fn symbol<T>(pick__of__Record_lT_r: Fn<T, T>, x: T) -> T = pick__of__Record_lT_r(x)
    fn inc(x: Nat) -> Nat = x + 1
    print_line(nat_to_text(get<Nat>(Named<Nat>(7))))
    print_line(parameter<Text>("parameter"))
    print_line(local<Text>("local"))
    print_line(lambda<Text>("lambda"))
    print_line(nat_to_text(match types<Nat>(1) { ok(xs) => length(xs), err(x) => x }))
    print_line(nat_to_text(symbol<Nat>(inc, 7)))"#,
    );
    assert_eq!(
        both(&p, "bindings"),
        ["7", "parameter", "local", "lambda", "0", "8"]
    );
}

#[test]
fn growing_record_instantiations_fail_within_the_expansion_budget() {
    let message = errors("record Box<T>(value: T)\n fn grow<T>(x: T, n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, grow<Box<T>>(Box<T>(x), pred(n)))\n print_line(nat_to_text(grow<Nat>(1, 1)))");
    assert!(
        message.contains("generic type expansion exceeds")
            && message.contains("polymorphic recursion"),
        "{message}"
    );
}

#[test]
fn duplicate_templates_are_rejected_before_expansion() {
    for body in [
        "fn f<T>(x: T) -> T = x\n fn f<U>(x: U) -> U = x",
        "record R<T>(x: T)\n record R<U>(x: U)",
        "fn f<T>(x: T) -> T = x\n fn f(x: Nat) -> Nat = x",
        "fn f(x: Nat) -> Nat = x\n fn f<T>(x: T) -> T = x",
        "fn f<T>(x: T) -> T = x\n let f = 7",
    ] {
        let message = errors(body);
        assert!(message.contains("duplicate value"), "{body}\n{message}");
    }
}

#[test]
fn generic_argument_spans_refer_to_source_columns() {
    let source = "module test.spans\ntheory App {\n    fn id<T>(x: T) -> T = x\n    print_line(nat_to_text(id<Nat>(7) + id<Nat>(9)))\n}\n";
    let module = parse_module(source).unwrap();
    let ModuleItem::Theory(t) = &module.items[0] else {
        panic!()
    };
    let TheoryItem::Expr(e) = &t.items[1] else {
        panic!()
    };
    fn literals(e: &dlm_core::Expr, out: &mut Vec<(String, usize, usize)>) {
        match &e.kind {
            dlm_core::ExprKind::IntLiteral(n) => {
                out.push((n.clone(), e.span.start_col, e.span.end_col))
            }
            dlm_core::ExprKind::Call { args, .. } => args.iter().for_each(|a| literals(a, out)),
            dlm_core::ExprKind::Add { lhs, rhs } => {
                literals(lhs, out);
                literals(rhs, out);
            }
            _ => {}
        }
    }
    let mut spans = Vec::new();
    literals(e, &mut spans);
    let line = source.lines().nth(3).unwrap();
    assert_eq!(
        spans,
        ["7", "9"].map(|n| {
            let col = line.find(n).unwrap() + 1;
            (n.to_string(), col, col + 1)
        })
    );
}

#[test]
fn single_module_runtime_uses_the_instances_checked_by_the_module_api() {
    let source = format!("module test.generics\ntheory App {{\n{LOCAL}\n}}\n");
    let module = parse_module(&source).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_module(&module);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let direct = Runtime::new().run_module(&module).unwrap().output;
    let program = Program::single(module);
    assert_eq!(direct, Runtime::new().run_program(&program).unwrap().output);
}
