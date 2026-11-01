//! v0.101 — closed rational intervals (numeric criterion N07): host API,
//! ordinary DLM `std.interval_q`, interpreter and native C, against independent
//! Python Fraction/isqrt fixtures (`tools/generate_interval_q101_oracle.py`).
use dlm_core::math::exact::{ExactBudget, Natural, Rational};
use dlm_core::math::interval_q::{IntervalOrder, IntervalQ};
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use std::path::{Path, PathBuf};
use std::process::Command;

const FIXTURES: &str = include_str!("fixtures/interval_q101.tsv");

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn wide() -> ExactBudget {
    ExactBudget::new(16384, u64::MAX / 4).unwrap()
}

fn interval(text: &str) -> IntervalQ {
    IntervalQ::parse(text, &mut wide()).unwrap_or_else(|e| panic!("{text}: {e}"))
}

fn rational(text: &str) -> Rational {
    Rational::parse(text, &mut wide()).unwrap()
}

struct Row<'a> {
    op: &'a str,
    bits: usize,
    x: &'a str,
    y: &'a str,
    value: &'a str,
    source: u8,
}

fn rows() -> Vec<Row<'static>> {
    FIXTURES
        .lines()
        .filter(|l| !l.starts_with('#'))
        .map(|l| {
            let f: Vec<&str> = l.split('\t').collect();
            Row {
                op: f[0],
                bits: f[1].parse().unwrap(),
                x: f[2],
                y: f[3],
                value: f[4],
                source: f[5].parse().unwrap(),
            }
        })
        .collect()
}

fn order_code(o: IntervalOrder) -> &'static str {
    match o {
        IntervalOrder::Less => "-1",
        IntervalOrder::Greater => "1",
        IntervalOrder::Equal => "0",
        IntervalOrder::Overlap => "2",
    }
}

/// Host result as fixture text, or `ERR:<message>`.
fn host(row: &Row, budget: &mut ExactBudget) -> String {
    let x = interval(row.x);
    let y = || interval(row.y);
    let shown = |r: Result<IntervalQ, dlm_core::math::MathError>| match r {
        Ok(v) => v.to_string(),
        Err(e) => format!("ERR:{e}"),
    };
    let text =
        |r: Result<String, dlm_core::math::MathError>| r.unwrap_or_else(|e| format!("ERR:{e}"));
    match row.op {
        "add" => shown(x.checked_add(&y(), budget)),
        "sub" => shown(x.checked_sub(&y(), budget)),
        "mul" => shown(x.checked_mul(&y(), budget)),
        "div" => shown(x.checked_div(&y(), budget)),
        "neg" => shown(x.checked_neg(budget)),
        "abs" => shown(x.checked_abs(budget)),
        "square" => shown(x.checked_square(budget)),
        "inv" => shown(x.checked_inv(budget)),
        "sqrt" => shown(x.checked_sqrt(row.y.parse().unwrap(), budget)),
        "hull" => shown(x.hull(&y(), budget)),
        "intersect" => text(
            x.intersect(&y(), budget)
                .map(|m| m.map_or("empty".to_string(), |v| v.to_string())),
        ),
        "contains" => text(x.contains(&rational(row.y), budget).map(|v| v.to_string())),
        "subset" => text(x.subset(&y(), budget).map(|v| v.to_string())),
        "compare" => text(x.compare(&y(), budget).map(|o| order_code(o).to_string())),
        "width" => text(x.width(budget).map(|v| v.to_string())),
        "mid" => text(x.midpoint(budget).map(|v| v.to_string())),
        other => panic!("unknown operation {other}"),
    }
}

fn expected_host(value: &str) -> &str {
    match value {
        "ERR:zero" => "ERR:interval division by an interval containing zero",
        "ERR:negative" => "ERR:interval square root of negative values",
        v => v,
    }
}

#[test]
fn host_operations_match_independent_oracles_with_exact_precision_boundaries() {
    let rows = rows();
    assert_eq!(rows.len(), 5055);
    let mut boundaries = 0;
    for row in &rows {
        let mut budget = ExactBudget::new(row.bits, u64::MAX / 4).unwrap();
        let got = host(row, &mut budget);
        assert_eq!(
            got,
            expected_host(row.value),
            "{} {} {} at {} bits",
            row.op,
            row.x,
            row.y,
            row.bits
        );
        if row.bits > 1 && !row.value.starts_with("ERR:") {
            let mut narrow = ExactBudget::new(row.bits - 1, u64::MAX / 4).unwrap();
            let got = host(row, &mut narrow);
            assert!(
                got.starts_with("ERR:UnknownWithinBudget") && got.contains("precision"),
                "{} {} {} at {} bits: {got}",
                row.op,
                row.x,
                row.y,
                row.bits - 1
            );
            boundaries += 1;
        }
    }
    assert_eq!(boundaries, 4559);
    assert!(rows.iter().any(|r| r.bits > 13000));
}

/// Inclusion: for sample points of the operands, the exact result lies in the
/// result interval; for intervals with rational endpoints the endpoints of
/// add/sub/mul/square/inv/div are attained (tightness).
#[test]
fn host_results_enclose_every_sampled_exact_result_and_are_tight() {
    let ends = ["-3", "-1", "-1/2", "0", "1/3", "1", "5/2"];
    let b = &mut wide();
    let mut intervals = Vec::new();
    for (i, lo) in ends.iter().enumerate() {
        for hi in &ends[i..] {
            intervals.push(interval(&format!("[{lo},{hi}]")));
        }
    }
    let samples = |x: &IntervalQ, b: &mut ExactBudget| -> Vec<Rational> {
        let mid = x.midpoint(b).unwrap();
        vec![x.lo().clone(), mid, x.hi().clone()]
    };
    let attained =
        |r: &IntervalQ, values: &[Rational]| values.contains(r.lo()) && values.contains(r.hi());
    for x in &intervals {
        let xs = samples(x, b);
        let square = x.checked_square(b).unwrap();
        for p in &xs {
            assert!(square.contains(&p.checked_mul(p, b).unwrap(), b).unwrap());
        }
        assert!(
            square.subset(&x.checked_mul(x, b).unwrap(), b).unwrap(),
            "square is at least as tight as X*X"
        );
        let abs = x.checked_abs(b).unwrap();
        for p in &xs {
            let a = if p.negative() { p.negated() } else { p.clone() };
            assert!(abs.contains(&a, b).unwrap());
        }
        if let Ok(inv) = x.checked_inv(b) {
            let values: Vec<Rational> = xs.iter().map(|p| p.reciprocal(b).unwrap()).collect();
            for v in &values {
                assert!(inv.contains(v, b).unwrap());
            }
            assert!(attained(&inv, &values));
        } else {
            assert!(x.contains_zero(b).unwrap());
        }
        for y in &intervals {
            let ys = samples(y, b);
            let (sum, difference, product) = (
                x.checked_add(y, b).unwrap(),
                x.checked_sub(y, b).unwrap(),
                x.checked_mul(y, b).unwrap(),
            );
            let mut products = Vec::new();
            for p in &xs {
                for q in &ys {
                    assert!(sum.contains(&p.checked_add(q, b).unwrap(), b).unwrap());
                    assert!(difference
                        .contains(&p.checked_sub(q, b).unwrap(), b)
                        .unwrap());
                    let pq = p.checked_mul(q, b).unwrap();
                    assert!(product.contains(&pq, b).unwrap());
                    products.push(pq);
                    if !q.is_zero() {
                        if let Ok(quotient) = x.checked_div(y, b) {
                            assert!(quotient.contains(&p.checked_div(q, b).unwrap(), b).unwrap());
                        }
                    }
                }
            }
            assert!(
                attained(&product, &products),
                "product endpoints are attained"
            );
            let hull = x.hull(y, b).unwrap();
            assert!(x.subset(&hull, b).unwrap() && y.subset(&hull, b).unwrap());
            match x.intersect(y, b).unwrap() {
                Some(m) => assert!(m.subset(x, b).unwrap() && m.subset(y, b).unwrap()),
                None => assert!(matches!(
                    x.compare(y, b).unwrap(),
                    IntervalOrder::Less | IntervalOrder::Greater
                )),
            }
        }
    }
}

#[test]
fn host_square_roots_are_guaranteed_and_within_the_resolution() {
    let b = &mut wide();
    for text in [
        "[0,2]",
        "[1/3,7/5]",
        "[2,2]",
        "[1/4,9/4]",
        "[5/7,100]",
        "[0,0]",
    ] {
        let x = interval(text);
        for k in [0u32, 1, 2, 7, 30, 100, 300] {
            let r = x.checked_sqrt(k, b).unwrap();
            let lo2 = r.lo().checked_mul(r.lo(), b).unwrap();
            let hi2 = r.hi().checked_mul(r.hi(), b).unwrap();
            assert!(
                lo2.checked_cmp(x.lo(), b).unwrap().is_le()
                    && hi2.checked_cmp(x.hi(), b).unwrap().is_ge(),
                "{text} k={k}"
            );
            // each bound is within 2^-k of the true root: (lo + 2^-k)^2 > x.lo
            let step = Rational::new(
                false,
                Natural::from_u128(1),
                Natural::from_u128(2).checked_pow(u128::from(k), b).unwrap(),
                b,
            )
            .unwrap();
            let up = r.lo().checked_add(&step, b).unwrap();
            assert!(
                up.checked_mul(&up, b)
                    .unwrap()
                    .checked_cmp(x.lo(), b)
                    .unwrap()
                    .is_gt(),
                "{text} k={k}"
            );
            if !r.hi().is_zero() {
                let down = r.hi().checked_sub(&step, b).unwrap();
                assert!(
                    down.negative()
                        || down
                            .checked_mul(&down, b)
                            .unwrap()
                            .checked_cmp(x.hi(), b)
                            .unwrap()
                            .is_lt(),
                    "{text} k={k}"
                );
            }
        }
    }
    assert_eq!(
        interval("[1/4,9/4]")
            .checked_sqrt(0, b)
            .unwrap()
            .to_string(),
        "[1/2,3/2]"
    );
    assert_eq!(
        interval("[2,3]").checked_sqrt(0, b).unwrap().to_string(),
        "[1/1,2/1]"
    );
    assert_eq!(
        interval("[2,3]").checked_sqrt(2, b).unwrap().to_string(),
        "[5/4,7/4]"
    );
    assert_eq!(
        interval("[1/4,9/4]")
            .checked_sqrt(1, b)
            .unwrap()
            .to_string(),
        "[1/2,3/2]"
    );
    assert!(interval("[-1,4]")
        .checked_sqrt(3, b)
        .unwrap_err()
        .0
        .contains("negative"));
    assert!(interval("[0,1]")
        .checked_sqrt(16385, b)
        .unwrap_err()
        .0
        .contains("resolution"));
}

#[test]
fn host_text_order_zero_divisors_and_budgets() {
    for text in ["[0/1,0/1]", "[-1/2,3/4]", "[-7/3,-1/5]"] {
        assert_eq!(interval(text).to_string(), text);
    }
    assert_eq!(interval("[2/4,6]").to_string(), "[1/2,6/1]");
    for bad in [
        "", "[]", "[1]", "[1,2", "1,2]", "[2,1]", "[1/0,2]", "[1,,2]", "[ 1,2]", "[1,2,3]",
        "(1,2)", "[1;2]",
    ] {
        assert!(
            IntervalQ::parse(bad, &mut wide()).is_err(),
            "accepted `{bad}`"
        );
    }
    let b = &mut wide();
    assert!(IntervalQ::new(rational("1"), rational("0"), b)
        .unwrap_err()
        .0
        .contains("out of order"));
    let touching = interval("[0,1]");
    assert!(touching.contains_zero(b).unwrap());
    assert!(touching
        .checked_inv(b)
        .unwrap_err()
        .0
        .contains("containing zero"));
    assert!(interval("[1,2]")
        .checked_div(&interval("[-1,0]"), b)
        .unwrap_err()
        .0
        .contains("containing zero"));
    assert_eq!(
        interval("[1,2]")
            .checked_div(&interval("[-2,-1]"), b)
            .unwrap()
            .to_string(),
        "[-2/1,-1/2]"
    );
    assert_eq!(
        interval("[0,1]")
            .intersect(&interval("[1,3]"), b)
            .unwrap()
            .unwrap()
            .to_string(),
        "[1/1,1/1]"
    );
    assert_eq!(
        interval("[0,1]").compare(&interval("[1,3]"), b).unwrap(),
        IntervalOrder::Overlap
    );
    assert_eq!(
        interval("[1,1]").compare(&interval("[1,1]"), b).unwrap(),
        IntervalOrder::Equal
    );
    let mut tiny = ExactBudget::new(8, 1_000).unwrap();
    assert!(
        IntervalQ::parse(&format!("[{},1]", "1".repeat(40)), &mut tiny)
            .unwrap_err()
            .0
            .starts_with("UnknownWithinBudget")
    );
    // work is threaded and exhausts honestly
    let x = interval("[-123/45,678/91]");
    let y = interval("[11/13,17/19]");
    let mut budget = ExactBudget::new(256, 1_000_000).unwrap();
    x.checked_mul(&y, &mut budget).unwrap();
    let used = budget.work_used();
    assert!(used > 0);
    let mut starved = ExactBudget::new(256, used - 1).unwrap();
    assert!(x
        .checked_mul(&y, &mut starved)
        .unwrap_err()
        .0
        .starts_with("UnknownWithinBudget"));
    // operands are validated against the precision before any shortcut
    let mut narrow = ExactBudget::new(4, 1_000_000).unwrap();
    assert!(interval("[0,100]")
        .checked_abs(&mut narrow)
        .unwrap_err()
        .0
        .contains("precision"));
    assert!(interval("[0,100]")
        .checked_inv(&mut narrow)
        .unwrap_err()
        .0
        .contains("precision"));
}

// ---------- ordinary DLM std.interval_q, interpreter and native C ----------

fn words(n: &Natural) -> String {
    let mut n = n.clone();
    let mut words = Vec::new();
    while !n.is_zero() {
        let (q, r) = n.div_rem(&Natural::from_u128(65536), &mut wide()).unwrap();
        words.push(r.to_string());
        n = q;
    }
    if words.is_empty() {
        "empty_list(Nat)".into()
    } else {
        format!("list({})", words.join(", "))
    }
}

fn source_rational(r: &Rational) -> String {
    format!(
        "BigRational({}, {}, {})",
        r.negative(),
        words(r.numerator()),
        words(r.denominator())
    )
}

fn source_interval(text: &str) -> String {
    let x = interval(text);
    format!(
        "IntervalQ({}, {})",
        source_rational(x.lo()),
        source_rational(x.hi())
    )
}

fn header() -> String {
    r#"module test.interval_q
import std.interval_q
theory App extends StdIntervalQ {
 fn show(r: Result<IntervalQCalc, Text>) -> Text = match r { err(e) => e, ok(v) => match ivq_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } }
 fn qshow(r: Result<BigRationalCalc, Text>) -> Text = match r { err(e) => e, ok(v) => match bigrat_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } }
 fn flag(r: Result<IntervalQFlag, Text>) -> Text = match r { err(e) => e, ok(v) => if_then_else(field(v, value), "true", "false") }
 fn meet(r: Result<IntervalQMeet, Text>) -> Text = match r { err(e) => e, ok(v) => if_then_else(field(v, nonempty), show(ok(IntervalQCalc(field(v, value), field(v, budget)), Text)), "empty") }
 fn order(r: Result<IntervalQOrder, Text>) -> Text = match r { err(e) => e, ok(v) => int_to_text(field(v, order)) }
"#
    .into()
}

fn call(row: &Row, budget: &str) -> String {
    let x = source_interval(row.x);
    let y = || source_interval(row.y);
    match row.op {
        "add" | "sub" | "mul" | "div" | "hull" => {
            format!("show(ivq_{}({budget}, {x}, {}))", row.op, y())
        }
        "neg" | "abs" | "square" | "inv" => format!("show(ivq_{}({budget}, {x}))", row.op),
        "sqrt" => format!("show(ivq_sqrt({budget}, {x}, {}))", row.y),
        "intersect" => format!("meet(ivq_intersect({budget}, {x}, {}))", y()),
        "contains" => format!(
            "flag(ivq_contains({budget}, {x}, {}))",
            source_rational(&rational(row.y))
        ),
        "subset" => format!("flag(ivq_subset({budget}, {x}, {}))", y()),
        "compare" => format!("order(ivq_compare({budget}, {x}, {}))", y()),
        "width" => format!("qshow(ivq_width({budget}, {x}))"),
        "mid" => format!("qshow(ivq_midpoint({budget}, {x}))"),
        other => panic!("unknown operation {other}"),
    }
}

fn run_both(source: &str, expected: &[String], name: &str) {
    let dir = std::env::temp_dir().join(format!("dlm-interval-q-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(
        Runtime::new().run_program(&p).unwrap().output,
        expected,
        "{name}: interpreter"
    );
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
    assert_eq!(
        String::from_utf8(output.stdout).unwrap().trim_end(),
        expected.join("\n"),
        "{name}: native C"
    );
    std::fs::remove_dir_all(dir).unwrap();
}

fn expected_source(value: &str) -> String {
    match value {
        "ERR:zero" => "interval division by an interval containing zero".into(),
        "ERR:negative" => "interval square root of negative values".into(),
        v => v.into(),
    }
}

#[test]
fn source_operations_match_oracles_and_native_c_with_precision_boundaries() {
    let mut source = header();
    let mut expected = Vec::new();
    let (mut vectors, mut boundaries) = (0, 0);
    for row in rows().iter().filter(|r| r.source > 0) {
        let budget = format!("ExactBudget({}, 1000000000000000)", row.bits);
        source += &format!(" print_line({})\n", call(row, &budget));
        expected.push(expected_source(row.value));
        vectors += 1;
        if row.source == 2 && row.bits > 1 && !row.value.starts_with("ERR:") {
            let narrow = format!("ExactBudget({}, 1000000000000000)", row.bits - 1);
            source += &format!(" print_line({})\n", call(row, &narrow));
            expected.push("UnknownWithinBudget: source precision exceeded".into());
            boundaries += 1;
        }
    }
    source += "}\n";
    assert_eq!(vectors, 233);
    assert_eq!(boundaries, 77);
    run_both(&source, &expected, "oracles");
}

#[test]
fn source_raw_records_budgets_and_privacy() {
    let mut source = header();
    source += r#"
 let b = ExactBudget(64, 1000000000000000)
 let zero = BigRational(false, empty_list(Nat), list(1))
 let one = BigRational(false, list(1), list(1))
 let half = BigRational(false, list(1), list(2))
 let x = IntervalQ(BigRational(true, list(1), list(2)), BigRational(false, list(3), list(4)))
 print_line(show(ivq_new(b, half, one)))
 print_line(show(ivq_new(b, one, half)))
 print_line(show(ivq_point(b, half)))
 print_line(show(ivq_check(b, IntervalQ(one, zero))))
 print_symbolic(is_err(ivq_check(b, IntervalQ(BigRational(false, list(2), list(4)), one))))
 print_symbolic(is_err(ivq_check(b, IntervalQ(BigRational(true, empty_list(Nat), list(1)), one))))
 print_symbolic(is_err(ivq_check(b, IntervalQ(zero, BigRational(false, list(1), empty_list(Nat))))))
 print_symbolic(is_err(ivq_add(b, x, IntervalQ(one, zero))))
 print_symbolic(is_err(ivq_mul(b, IntervalQ(one, zero), x)))
 print_symbolic(is_err(ivq_abs(b, IntervalQ(one, zero))))
 print_symbolic(is_err(ivq_sqrt(b, IntervalQ(one, zero), 3)))
 print_symbolic(is_err(ivq_add(ExactBudget(0, 1000), x, x)))
 print_symbolic(is_err(ivq_add(ExactBudget(64, 0), x, x)))
 print_line(show(ivq_inv(b, x)))
 print_line(show(ivq_div(b, x, x)))
 print_line(show(ivq_sqrt(b, x, 4)))
 print_line(show(ivq_sqrt(b, IntervalQ(zero, BigRational(false, list(2), list(1))), 20)))
 print_line(show(ivq_sqrt(b, IntervalQ(zero, one), 16385)))
 print_line(show(ivq_square(b, x)))
 print_line(show(ivq_abs(b, x)))
 print_line(show(ivq_neg(b, x)))
 print_line(flag(ivq_contains_zero(b, x)))
 print_line(qshow(ivq_width(b, x)))
 print_line(qshow(ivq_midpoint(b, x)))
 let start = ExactBudget(64, 1000000000)
 let product = unwrap_or(ivq_mul(start, x, x), IntervalQCalc(x, ExactBudget(64, 0)))
 print_symbolic(1000000000 > field(field(product, budget), work))
 print_decimal(field(start, work))
 print_line(show(ok(product, Text)))
 print_line(show(ivq_parse(b, "[2/4,6]")))
 print_line(show(ivq_parse(b, "[-7/3,-1/5]")))
 print_line(show(ivq_parse(b, "[2,1]")))
 print_line(show(ivq_parse(b, "[1,2")))
 print_line(show(ivq_parse(b, "[]")))
 print_line(show(ivq_parse(b, "[1;2]")))
 print_line(show(ivq_parse(b, "[1,2,3]")))
 print_line(show(ivq_parse(b, "[1/0,2]")))
 print_line(show(ivq_parse(ExactBudget(2, 1000), "[1,1000000000000000]")))
}
"#;
    let expected = [
        "[1/2,1/1]",
        "interval bounds out of order",
        "[1/2,1/2]",
        "interval bounds out of order",
        "true",
        "true",
        "true",
        "true",
        "true",
        "true",
        "true",
        "true",
        "true",
        "interval division by an interval containing zero",
        "interval division by an interval containing zero",
        "interval square root of negative values",
        "[0/1,1482911/1048576]",
        "interval square root resolution exceeds 16384 bits",
        "[0/1,9/16]",
        "[0/1,3/4]",
        "[-3/4,1/2]",
        "true",
        "5/4",
        "1/8",
        "true",
        "1000000000",
        "[-3/8,9/16]",
        "[1/2,6/1]",
        "[-7/3,-1/5]",
        "interval bounds out of order",
        "invalid exact interval text: expected [<lo>,<hi>]",
        "invalid exact interval text: expected [<lo>,<hi>]",
        "invalid exact interval text: expected [<lo>,<hi>]",
        "invalid exact decimal",
        "exact rational denominator is zero",
        "UnknownWithinBudget: exact interval input exceeded",
    ]
    .map(String::from);
    run_both(&source, &expected, "records");

    let dir = std::env::temp_dir().join(format!("dlm-interval-q-private-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    for (i, body) in [
        "let v = iv_zero()",
        "let v = iv_pick(exact_default_budget(), iv_zero(), iv_zero(), true)",
        "let v = iv_sqrt_bound(exact_default_budget(), BigRational(false, list(2), list(1)), 3, true)",
        "fn iv_is_zero(a: BigRational) -> Bool = true",
    ]
    .iter()
    .enumerate()
    {
        let path = dir.join(format!("bad{i}.dlm"));
        std::fs::write(&path, format!("module test.private_interval\nimport std.interval_q\ntheory App extends StdIntervalQ {{\n {body}\n}}\n")).unwrap();
        let rejected = match load_program_with_paths(&path, &[repo().join("lib")]) {
            Err(_) => true,
            Ok(p) => !Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p).ok(),
        };
        assert!(rejected, "private helper escaped: {body}");
    }
    std::fs::remove_dir_all(dir).unwrap();
}
