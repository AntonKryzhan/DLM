//! v0.106 — uniform error and budget contracts of the scalar numeric APIs
//! (N13): one category per refusal in host `math::contract` and source
//! `std.numeric`, a catalogue covering every refusal literal, and the
//! sequential/threshold budget contract in both (interpreter and native C).
use dlm_core::math::contract::{
    self, chain, check_threshold, classify, ErrorCategory, MathResultExt, MESSAGES,
};
use dlm_core::math::convert::{self, RoundMode};
use dlm_core::math::creal::CReal;
use dlm_core::math::elementary;
use dlm_core::math::exact::{ExactBudget, Natural, Rational};
use dlm_core::math::interval_q::IntervalQ;
use dlm_core::math::portable;
use dlm_core::math::{MathError, MathResult};
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use std::collections::BTreeSet;
use std::path::{Path, PathBuf};
use std::process::Command;

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn literals(text: &str) -> Vec<String> {
    let mut out = Vec::new();
    let mut chars = text.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '/' && chars.peek() == Some(&'/') {
            for c in chars.by_ref() {
                if c == '\n' {
                    break;
                }
            }
        } else if c == '\'' {
            // char literal or lifetime: skip a short char literal
            let mut look = chars.clone();
            if let (Some(a), Some(b)) = (look.next(), look.next()) {
                if b == '\'' || (a == '\\' && look.next() == Some('\'')) {
                    chars = look;
                }
            }
        } else if c == '"' {
            let mut s = String::new();
            while let Some(c) = chars.next() {
                match c {
                    '\\' => {
                        if let Some(n) = chars.next() {
                            s.push(n);
                        }
                    }
                    '"' => break,
                    _ => s.push(c),
                }
            }
            out.push(s);
        }
    }
    out
}

fn run_both(name: &str, source: &str) -> Vec<String> {
    let dir = std::env::temp_dir().join(format!("dlm-{name}-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let interpreted = Runtime::new().run_program(&p).unwrap().output;
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(
        &emit_c(&ir),
        &dir.join("main.c"),
        &dir.join(if cfg!(windows) { "main.exe" } else { "main" }),
        &default_c_compiler(),
    )
    .unwrap();
    let output = Command::new(exe).output().unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let native: Vec<String> = String::from_utf8(output.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect();
    assert_eq!(interpreted, native);
    std::fs::remove_dir_all(dir).unwrap();
    native
}

#[test]
fn catalogue_classifies_and_covers_every_refusal_literal() {
    for (m, c, side) in MESSAGES {
        assert_eq!(classify(m), Some(*c), "{m}");
        assert!(matches!(*side, "h" | "s" | "hs"), "{m}");
    }
    let used: BTreeSet<_> = MESSAGES.iter().map(|(_, c, _)| *c).collect();
    assert_eq!(used.len(), ErrorCategory::ALL.len());
    for c in ErrorCategory::ALL {
        assert_eq!(ErrorCategory::parse(c.name()), Some(c));
        assert_eq!(ErrorCategory::ALL[c.code() as usize], c);
    }
    assert!(
        ErrorCategory::Budget.retryable()
            && ErrorCategory::Undecided.retryable()
            && !ErrorCategory::Domain.retryable()
    );
    assert_eq!(classify("internal nonexact division"), None);
    assert_eq!(
        MathError("exact division by zero".into()).category(),
        Some(ErrorCategory::Domain)
    );
    // every source refusal is catalogued (source side)
    let mut missing = Vec::new();
    for module in [
        "exact",
        "interval_q",
        "complex_q",
        "creal",
        "ccomplex",
        "convert",
        "numeric",
        "scalar_witness",
        "dense",
    ] {
        let text = std::fs::read_to_string(repo().join(format!("lib/std/{module}.dlm"))).unwrap();
        for part in text.split("err(\"").skip(1) {
            let m = &part[..part.find('"').unwrap()];
            if !MESSAGES.iter().any(|(x, _, s)| *x == m && s.contains('s')) {
                missing.push(format!("{module}: {m}"));
            }
        }
    }
    // every host refusal literal is catalogued (host side); format arguments match by prefix
    const NOT_REFUSALS: &[&str] = &[
        "internal nonexact division",
        "constants are embedded",
        "nonempty program",
    ];
    for module in [
        "exact",
        "interval_q",
        "complex_q",
        "complex",
        "creal",
        "ccomplex",
        "elementary",
        "convert",
        "portable",
        "scalar_witness",
        "contract",
    ] {
        if module == "contract" {
            continue;
        }
        let text =
            std::fs::read_to_string(repo().join(format!("crates/dlm_core/src/math/{module}.rs")))
                .unwrap();
        for lit in literals(&text) {
            let words = lit.split(' ').count();
            let first = lit.chars().next().unwrap_or(' ');
            if words < 3
                || !(first.is_ascii_lowercase() || lit.starts_with("UnknownWithinBudget"))
                || NOT_REFUSALS.contains(&lit.as_str())
            {
                continue;
            }
            let stem = lit.split('{').next().unwrap();
            if !MESSAGES
                .iter()
                .any(|(x, _, s)| x.starts_with(stem) && s.contains('h'))
            {
                missing.push(format!("{module}.rs: {lit}"));
            }
        }
    }
    assert!(
        missing.is_empty(),
        "uncatalogued refusals:\n{}",
        missing.join("\n")
    );
}

#[test]
fn source_classification_matches_host() {
    let mut source = String::from(
        "module test.numeric_classes\nimport std.numeric\ntheory App extends StdNumeric {\n",
    );
    let mut expected = Vec::new();
    for (m, c, _) in MESSAGES {
        source += &format!(" print_line(nm_category(\"{m}\"))\n");
        expected.push(c.name().to_string());
    }
    for (m, want) in [("something else", "unclassified"), ("", "unclassified")] {
        source += &format!(" print_line(nm_category(\"{m}\"))\n");
        expected.push(want.to_string());
    }
    // Names in malformed input are data, not an error category.
    for name in [
        "division by zero",
        "rational is not an integer",
        "decimal digit count exceeds 4096",
    ] {
        let error =
            portable::parse(&format!("dlm1:{name}:1"), &mut ExactBudget::default()).unwrap_err();
        assert_eq!(
            error.category(),
            Some(ErrorCategory::InvalidInput),
            "{error}"
        );
        source += &format!(" print_line(nm_category(\"{}\"))\n", error.0);
        expected.push("invalid_input".to_string());
        let error = RoundMode::parse(name).unwrap_err();
        assert_eq!(
            error.category(),
            Some(ErrorCategory::InvalidInput),
            "{error}"
        );
        source += &format!(" print_line(nm_category(\"{}\"))\n", error.0);
        expected.push("invalid_input".to_string());
    }
    source += " print_line(if_then_else(nm_retryable(\"UnknownWithinBudget: rounding undecided at precision 8\"), \"retry\", \"final\"))\n";
    source += " print_line(if_then_else(nm_retryable(\"invalid decimal number\"), \"retry\", \"final\"))\n}\n";
    expected.extend(["retry".to_string(), "final".to_string()]);
    assert_eq!(run_both("numeric-classes", &source), expected);
}

type Op<'a> = Box<dyn Fn(&mut ExactBudget) -> MathResult<String> + 'a>;

fn host_ops() -> Vec<(&'static str, Op<'static>)> {
    fn q(t: &str, b: &mut ExactBudget) -> MathResult<Rational> {
        Rational::parse(t, b)
    }
    vec![
        (
            "pow",
            Box::new(|b| {
                Natural::from_u128(3)
                    .checked_pow(100, b)
                    .map(|v| v.to_string())
            }),
        ),
        (
            "rat",
            Box::new(|b| {
                q("22/7", b)?
                    .checked_div(&q("-355/113", b)?, b)
                    .map(|v| v.to_string())
            }),
        ),
        (
            "div0",
            Box::new(|b| {
                q("22/7", b)?
                    .checked_div(&q("0", b)?, b)
                    .map(|v| v.to_string())
            }),
        ),
        (
            "ivq_sqrt",
            Box::new(|b| {
                IntervalQ::parse("[2/1,3/1]", b)?
                    .checked_sqrt(40, b)
                    .map(|v| v.to_string())
            }),
        ),
        (
            "creal",
            Box::new(|b| {
                CReal::parse("c 2 ; sqrt 0 ; mul 1 1", b)?
                    .approx(32, b)
                    .map(|v| v.to_string())
            }),
        ),
        (
            "pi",
            Box::new(|b| elementary::pi(48, b).map(|v| v.to_string())),
        ),
        (
            "log0",
            Box::new(|b| {
                CReal::parse("c 0 ; log 0", b)?
                    .approx(16, b)
                    .map(|v| v.to_string())
            }),
        ),
        (
            "decimal",
            Box::new(|b| convert::to_decimal(&q("22/7", b)?, 30, RoundMode::HalfEven, b)),
        ),
        (
            "parse_decimal",
            Box::new(|b| convert::parse_decimal("-2.50e-7", b).map(|v| v.to_string())),
        ),
        (
            "creal_decimal",
            Box::new(|b| {
                convert::creal_decimal(&CReal::parse("pi", b)?, 6, RoundMode::Floor, 64, b)
            }),
        ),
        (
            "undecided",
            Box::new(|b| {
                convert::creal_round(
                    &CReal::parse("c 2 ; sqrt 0 ; sub 1 1", b)?,
                    RoundMode::Floor,
                    32,
                    b,
                )
                .map(|v| v.to_string())
            }),
        ),
        (
            "portable",
            Box::new(|b| portable::parse("dlm1:ivq:[-1/2,3/4]", b).map(|v| portable::format(&v))),
        ),
    ]
}

#[test]
fn host_budget_thresholds_and_sequential_chains() {
    let mut total = 0;
    for (name, op) in host_ops() {
        let w = check_threshold(256, &op).unwrap_or_else(|e| panic!("{name}: {e}"));
        assert!(w > 0, "{name} uses no work");
        total += w;
    }
    // a chain uses the sum of its steps and leaves the rest
    let mut b = ExactBudget::new(256, total + 7).unwrap();
    let ops = host_ops();
    let steps = ops
        .iter()
        .map(|(_, op)| {
            Box::new(move |b: &mut ExactBudget| {
                op(b).or_else(|e| {
                    if e.category() == Some(ErrorCategory::Domain)
                        || e.category() == Some(ErrorCategory::Undecided)
                    {
                        Ok(String::new())
                    } else {
                        Err(e)
                    }
                })
            }) as Box<dyn FnOnce(&mut ExactBudget) -> MathResult<String>>
        })
        .collect();
    let done = chain(&mut b, steps);
    assert_eq!(done.len(), ops.len());
    assert_eq!(done.iter().map(|m| m.work).sum::<u64>(), total);
    assert_eq!(b.remaining_work(), 7);
    // the first refusal stops the chain; an exhausted budget is left empty
    let mut b = ExactBudget::new(256, total / 2).unwrap();
    let steps = ops
        .iter()
        .map(|(_, op)| {
            Box::new(move |b: &mut ExactBudget| {
                op(b).or_else(|e| {
                    if e.category() == Some(ErrorCategory::Budget) {
                        Err(e)
                    } else {
                        Ok(String::new())
                    }
                })
            }) as Box<dyn FnOnce(&mut ExactBudget) -> MathResult<String>>
        })
        .collect();
    let done = chain(&mut b, steps);
    assert!(done.len() < ops.len());
    assert_eq!(done.last().unwrap().category(), Some(ErrorCategory::Budget));
    assert!(done[..done.len() - 1].iter().all(|m| m.value.is_ok()));
    assert_eq!(b.remaining_work(), 0);
    // refusals keep their category under any sufficient budget
    let cats: Vec<_> = host_ops()
        .iter()
        .map(|(n, op)| (*n, contract::required_work(256, op).unwrap().category()))
        .collect();
    assert!(cats.contains(&("div0", Some(ErrorCategory::Domain))));
    assert!(cats.contains(&("log0", Some(ErrorCategory::Domain))));
    assert!(cats.contains(&("undecided", Some(ErrorCategory::Undecided))));
    assert_eq!(cats.iter().filter(|(_, c)| c.is_some()).count(), 3);
    assert_eq!(
        MathResult::<u8>::Err(MathError("decimal exponent out of range".into())).category(),
        Some(ErrorCategory::Limit)
    );
}

#[test]
fn source_budget_thresholds_and_sequential_chains() {
    let ops = [
        ("pow", "r(big_pow(b, BigInt(false, list(3)), 100))"),
        ("rat", "match bigrat_parse(b, \"22/7\") { err(e) => err(e, ExactBudget), ok(x) => match bigrat_parse(field(x, budget), \"-355/113\") { err(e) => err(e, ExactBudget), ok(y) => q(bigrat_div(field(y, budget), field(x, value), field(y, value))) } }"),
        ("div0", "match bigrat_parse(b, \"22/7\") { err(e) => err(e, ExactBudget), ok(x) => q(bigrat_div(field(x, budget), field(x, value), BigRational(false, empty_list(Nat), list(1)))) }"),
        ("ivq_sqrt", "match ivq_parse(b, \"[2/1,3/1]\") { err(e) => err(e, ExactBudget), ok(x) => iv(ivq_sqrt(field(x, budget), field(x, value), 40)) }"),
        ("creal", "ap(b, \"c 2 ; sqrt 0 ; mul 1 1\", 32)"),
        ("log0", "ap(b, \"c 0 ; log 0\", 16)"),
        ("decimal", "match bigrat_parse(b, \"22/7\") { err(e) => err(e, ExactBudget), ok(x) => t(cv_decimal(field(x, budget), field(x, value), 30, 3)) }"),
        ("parse_decimal", "q(cv_parse_decimal(b, \"-2.50e-7\"))"),
        ("undecided", "match creal_parse(b, \"c 2 ; sqrt 0 ; sub 1 1\") { err(e) => err(e, ExactBudget), ok(x) => r(cv_creal_round(field(x, budget), field(x, value), 0, 32)) }"),
        ("portable", "t(pn_check(b, \"dlm1:ivq:[-1/2,3/4]\"))"),
    ];
    let mut source = String::from(
        r#"module test.numeric_budget
import std.numeric
theory App extends StdNumeric {
 let big = ExactBudget(256, 1000000000000000)
 fn r(x: Result<BigIntCalc, Text>) -> Result<ExactBudget, Text> = match x { err(e) => err(e, ExactBudget), ok(v) => ok(field(v, budget), Text) }
 fn q(x: Result<BigRationalCalc, Text>) -> Result<ExactBudget, Text> = match x { err(e) => err(e, ExactBudget), ok(v) => ok(field(v, budget), Text) }
 fn t(x: Result<BigTextCalc, Text>) -> Result<ExactBudget, Text> = match x { err(e) => err(e, ExactBudget), ok(v) => ok(field(v, budget), Text) }
 fn iv(x: Result<IntervalQCalc, Text>) -> Result<ExactBudget, Text> = match x { err(e) => err(e, ExactBudget), ok(v) => ok(field(v, budget), Text) }
 fn ap(b: ExactBudget, s: Text, n: Nat) -> Result<ExactBudget, Text> = match creal_parse(b, s) { err(e) => err(e, ExactBudget), ok(x) => iv(creal_approx(field(x, budget), field(x, value), n)) }
 fn used(x: Result<ExactBudget, Text>) -> Nat = match x { err(e) => 0, ok(v) => nm_work(big, v) }
 fn cat(x: Result<ExactBudget, Text>) -> Text = match x { err(e) => nm_category(e), ok(v) => concat("ok ", nat_to_text(field(v, work))) }
 fn soft(x: Result<ExactBudget, Text>, b: ExactBudget) -> Result<ExactBudget, Text> = match x { err(e) => if_then_else(nm_retryable(e), if_then_else(nat_eq(nm_code(e), 4), err(e, ExactBudget), ok(b, Text)), ok(b, Text)), ok(v) => ok(v, Text) }
"#,
    );
    // ops that refuse with domain/undecided still report their work through the full-budget run
    source += " fn full(x: Result<ExactBudget, Text>) -> Text = match x { err(e) => nm_category(e), ok(v) => nat_to_text(nm_work(big, v)) }\n";
    for (i, (_, body)) in ops.iter().enumerate() {
        source += &format!(" fn op{i}(b: ExactBudget) -> Result<ExactBudget, Text> = {body}\n");
    }
    // work of refusing ops: measured as the least work under which the category stops being `budget`
    for i in 0..ops.len() {
        source += &format!(" print_line(full(op{i}(big)))\n");
    }
    source += "}\n";
    let first = run_both("numeric-budget-1", &source);
    assert_eq!(first.len(), ops.len());
    let refusing: Vec<_> = first
        .iter()
        .enumerate()
        .filter(|(_, v)| v.parse::<u64>().is_err())
        .map(|(i, v)| (ops[i].0, v.as_str()))
        .collect();
    assert_eq!(
        refusing,
        vec![
            ("div0", "domain"),
            ("log0", "domain"),
            ("undecided", "undecided")
        ]
    );
    // second program: thresholds for every op and a chain
    let mut second = source.trim_end().trim_end_matches('}').to_string();
    second = second
        .lines()
        .filter(|l| !l.starts_with(" print_line(full("))
        .collect::<Vec<_>>()
        .join("\n")
        + "\n";
    let mut dispatch =
        String::from(" fn run(f: Nat, b: ExactBudget) -> Result<ExactBudget, Text> = ");
    for i in 0..ops.len() {
        dispatch += &format!("if_then_else(nat_eq(f, {i}), op{i}(b), ");
    }
    dispatch += "err(\"no op\", ExactBudget)";
    dispatch += &")".repeat(ops.len());
    second += &dispatch;
    second += "\n";
    second += " fn need(n: Nat, w: Nat, step: Nat, f: Nat) -> Nat decreasing n fuel 128 = if_then_else(nat_eq(n, 0), w, if_then_else(nat_eq(step, 0), w, if_then_else(text_eq(cat(run(f, ExactBudget(256, monus(w, step)))), \"budget\"), need(pred(n), w, div(step, 2), f), need(pred(n), monus(w, step), step, f))))\n";
    for (i, v) in first.iter().enumerate() {
        let w = match v.parse::<u64>() {
            Ok(w) => format!("{w}"),
            // least work for a non-budget answer, by bisection from 2^40
            Err(_) => format!("need(120, 1099511627776, 549755813888, {i})"),
        };
        second += &format!(" print_line(concat(nat_to_text({w}), concat(\" \", concat(cat(op{i}(ExactBudget(256, monus({w}, 1)))), concat(\" | \", cat(op{i}(ExactBudget(256, {w}))))))))\n");
    }
    second += " print_line(nat_to_text(used(soft(op1(match soft(op0(big), big) { err(e) => big, ok(v) => v }), big))))\n";
    second += " print_line(cat(op4(ExactBudget(256, 3))))\n}\n";
    let lines = run_both("numeric-budget-2", &second);
    assert_eq!(lines.len(), ops.len() + 2, "{lines:?}");
    for (i, line) in lines[..ops.len()].iter().enumerate() {
        let (w, rest) = line.split_once(' ').unwrap();
        let (short, exact) = rest.split_once(" | ").unwrap();
        assert!(w.parse::<u64>().unwrap() > 0, "{}: {line}", ops[i].0);
        assert_eq!(short, "budget", "{}: {line}", ops[i].0);
        match first[i].parse::<u64>() {
            Ok(_) => assert_eq!(exact, "ok 0", "{}: {line}", ops[i].0),
            Err(_) => assert_eq!(exact, first[i], "{}: {line}", ops[i].0),
        }
    }
    let w0: u64 = first[0].parse().unwrap();
    let w1: u64 = first[1].parse().unwrap();
    assert_eq!(lines[ops.len()].parse::<u64>().unwrap(), w0 + w1);
    assert_eq!(lines[ops.len() + 1], "budget");
}
