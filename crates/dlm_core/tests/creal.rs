//! v0.102 — computable reals (numeric criterion N08): programs over closed
//! rational intervals with a fixed refinement schedule; host API, ordinary DLM
//! `std.creal`, interpreter and native C, against independent Python
//! Fraction/isqrt/Decimal fixtures (`tools/generate_creal102_oracle.py`).
use dlm_core::math::creal::{CNode, CReal, CRealOp, MAX_NODES};
use dlm_core::math::exact::{ExactBudget, Rational};
use dlm_core::math::interval_q::IntervalQ;
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use std::cmp::Ordering;
use std::path::{Path, PathBuf};
use std::process::Command;

const FIXTURES: &str = include_str!("fixtures/creal102.tsv");

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn wide() -> ExactBudget {
    ExactBudget::new(16384, u64::MAX / 4).unwrap()
}

fn budget(bits: usize) -> ExactBudget {
    ExactBudget::new(bits, u64::MAX / 4).unwrap()
}

fn program(text: &str) -> CReal {
    CReal::parse(text, &mut wide()).unwrap_or_else(|e| panic!("{text}: {e}"))
}

fn rational(text: &str) -> Rational {
    Rational::parse(text, &mut wide()).unwrap()
}

struct Row<'a> {
    kind: &'a str,
    bits: Option<usize>,
    n: u32,
    x: &'a str,
    y: &'a str,
    value: &'a str,
    source: u8,
}

fn rows() -> Vec<Row<'static>> {
    FIXTURES
        .lines()
        .map(|line| {
            let f: Vec<&str> = line.split('\t').collect();
            assert_eq!(f.len(), 7, "{line}");
            Row {
                kind: f[0],
                bits: f[1].parse().ok(),
                n: f[2].parse().unwrap(),
                x: f[3],
                y: f[4],
                value: f[5],
                source: f[6].parse().unwrap(),
            }
        })
        .collect()
}

fn order_code(o: Option<Ordering>) -> &'static str {
    match o {
        Some(Ordering::Less) => "-1",
        Some(Ordering::Greater) => "1",
        Some(Ordering::Equal) => "0",
        None => "2",
    }
}

fn host(row: &Row, budget: &mut ExactBudget) -> String {
    let parse = |t: &str, b: &mut ExactBudget| CReal::parse(t, b);
    let result = (|| -> Result<String, dlm_core::math::MathError> {
        let x = parse(row.x, budget)?;
        Ok(match row.kind {
            "approx" => x.approx(row.n, budget)?.to_string(),
            "compare" => {
                let y = parse(row.y, budget)?;
                order_code(x.compare(&y, row.n, budget)?).to_string()
            }
            other => panic!("unknown kind {other}"),
        })
    })();
    result.unwrap_or_else(|e| e.0)
}

fn expected_host(value: &str) -> &str {
    match value {
        "ERR:zero" => "computable real division by zero",
        "ERR:negative" => "computable real square root of a negative value",
        v => v,
    }
}

#[test]
fn host_matches_independent_oracle_with_exact_precision_boundaries() {
    let (mut total, mut boundaries, mut unknown, mut domain, mut undecided) = (0, 0, 0, 0, 0);
    for row in rows() {
        total += 1;
        match row.bits {
            None => {
                assert_eq!(row.value, "UNKNOWN");
                let got = host(&row, &mut wide());
                assert!(got.starts_with("UnknownWithinBudget"), "{}: {got}", row.x);
                unknown += 1;
            }
            Some(bits) => {
                let got = host(&row, &mut budget(bits));
                assert_eq!(got, expected_host(row.value), "{} at n={}", row.x, row.n);
                if row.value.starts_with("ERR:") {
                    domain += 1;
                }
                if row.value == "2" {
                    undecided += 1;
                }
                if bits > 1 {
                    let narrow = host(&row, &mut budget(bits - 1));
                    assert!(
                        narrow.starts_with("UnknownWithinBudget") && narrow.contains("precision"),
                        "{} at n={} bits={}: {narrow}",
                        row.x,
                        row.n,
                        bits - 1
                    );
                    boundaries += 1;
                }
            }
        }
    }
    assert_eq!(
        (total, boundaries, unknown, domain, undecided),
        (343, 340, 3, 63, 2)
    );
}

#[test]
fn host_results_contain_exact_values_and_meet_the_width() {
    let mut b = wide();
    // rational programs are exact points at every precision
    let x = program("c 1/3 ; c 1/6 ; add 0 1 ; c 3 ; mul 2 3 ; inv 4 ; neg 5 ; abs 6 ; square 7");
    for n in [0, 1, 100, 4096] {
        let v = x.approx(n, &mut b).unwrap();
        assert!(v.is_point());
        assert_eq!(v.lo(), &rational("4/9"));
    }
    // sqrt bounds square to the radicand's sides at every precision
    for n in [0u32, 1, 7, 31, 64, 200, 777] {
        let v = program("c 2 ; sqrt 0").approx(n, &mut b).unwrap();
        let two = rational("2");
        assert!(
            v.lo()
                .checked_mul(v.lo(), &mut b)
                .unwrap()
                .checked_cmp(&two, &mut b)
                .unwrap()
                != Ordering::Greater
        );
        assert!(
            v.hi()
                .checked_mul(v.hi(), &mut b)
                .unwrap()
                .checked_cmp(&two, &mut b)
                .unwrap()
                != Ordering::Less
        );
        let tolerance = Rational::new(
            false,
            dlm_core::math::exact::Natural::from_u128(1),
            dlm_core::math::exact::Natural::from_u128(2)
                .checked_pow(u128::from(n), &mut b)
                .unwrap(),
            &mut b,
        )
        .unwrap();
        assert_ne!(
            v.width(&mut b)
                .unwrap()
                .checked_cmp(&tolerance, &mut b)
                .unwrap(),
            Ordering::Greater
        );
        // nested refinement: the interval at n+1 lies in the closure of the one at n widened by 2^-n
        let finer = program("c 2 ; sqrt 0").approx(n + 1, &mut b).unwrap();
        assert!(finer.contains(v.lo(), &mut b).unwrap() || v.contains(finer.lo(), &mut b).unwrap());
    }
    // golden ratio satisfies phi^2 = phi + 1 within the bounds
    let phi = program("c 5 ; sqrt 0 ; c 1 ; add 1 2 ; c 2 ; div 3 4");
    let v = phi.approx(120, &mut b).unwrap();
    let sq = v.checked_square(&mut b).unwrap();
    let plus = v
        .checked_add(&IntervalQ::point(rational("1"), &mut b).unwrap(), &mut b)
        .unwrap();
    assert!(sq.intersect(&plus, &mut b).unwrap().is_some());
    assert!(v.contains(&rational("1618033988749894848204586834365638117720309/1000000000000000000000000000000000000000000"), &mut b).unwrap());
}

#[test]
fn host_domain_errors_undecided_and_refinement_limits() {
    let mut b = wide();
    let err = |t: &str, n: u32| program(t).approx(n, &mut wide()).unwrap_err().0;
    assert_eq!(err("c 0 ; inv 0", 8), "computable real division by zero");
    assert_eq!(
        err("c 1 ; c 2 ; c 2 ; sub 1 2 ; div 0 3", 8),
        "computable real division by zero"
    );
    assert_eq!(
        err("c -1/9 ; sqrt 0", 8),
        "computable real square root of a negative value"
    );
    // a divisor that is zero but not syntactically: never decided, never refuted
    let e = err("c 2 ; sqrt 0 ; sub 1 1 ; inv 2", 8);
    assert!(e.starts_with("UnknownWithinBudget"), "{e}");
    let e = err("c 3 ; sqrt 0 ; mul 1 1 ; c 3 ; sub 2 3 ; sqrt 4", 4);
    assert!(e.starts_with("UnknownWithinBudget"), "{e}");
    // equal reals that are not exact points stay undecided at every precision
    let x = program("c 2 ; sqrt 0 ; mul 1 1");
    let two = CReal::constant(rational("2"), &mut b).unwrap();
    for n in [0, 8, 64, 512] {
        assert_eq!(x.compare(&two, n, &mut b).unwrap(), None);
    }
    assert_eq!(
        program("c 9 ; sqrt 0")
            .compare(&program("c 3"), 0, &mut b)
            .unwrap(),
        Some(Ordering::Equal)
    );
    let close = program("c 1414213562373095/1000000000000000");
    let root = program("c 2 ; sqrt 0");
    assert_eq!(root.compare(&close, 20, &mut b).unwrap(), None);
    assert_eq!(
        root.compare(&close, 80, &mut b).unwrap(),
        Some(Ordering::Greater)
    );
    assert_eq!(
        close.compare(&root, 80, &mut b).unwrap(),
        Some(Ordering::Less)
    );
    // a retry is needed and taken: the divisor's sign is decided at a finer resolution
    let v = program("c 2 ; sqrt 0 ; c 1414213/1000000 ; sub 1 2 ; inv 3")
        .approx(0, &mut b)
        .unwrap();
    assert_eq!(
        v.lo().checked_cmp(&rational("1778000"), &mut b).unwrap(),
        Ordering::Greater
    );
    assert_eq!(
        v.hi().checked_cmp(&rational("1778500"), &mut b).unwrap(),
        Ordering::Less
    );
    // precision request limit and work exhaustion
    assert_eq!(
        root.approx(4097, &mut b).unwrap_err().0,
        "computable real precision exceeds 4096 bits"
    );
    let mut tiny = ExactBudget::new(16384, 50).unwrap();
    let e = root.approx(64, &mut tiny).unwrap_err().0;
    assert!(
        e.starts_with("UnknownWithinBudget") && e.contains("work"),
        "{e}"
    );
    // the precision bounds the resolution: a 4000-bit request needs about 8000 bits
    let e = root.approx(4000, &mut budget(4096)).unwrap_err().0;
    assert!(
        e.starts_with("UnknownWithinBudget") && e.contains("precision"),
        "{e}"
    );
}

#[test]
fn host_program_grammar_and_validation() {
    let mut b = wide();
    let text = "c -1/2 ; c 3/1 ; sqrt 1 ; add 0 2 ; neg 3 ; inv 4 ; div 5 1 ; square 6 ; abs 7 ; mul 7 8 ; sub 9 0";
    let x = program(text);
    assert_eq!(x.to_string(), text);
    assert_eq!(x.nodes().len(), 11);
    assert_eq!(
        x.nodes()[2],
        CNode {
            op: CRealOp::Sqrt,
            a: 1,
            b: 0,
            value: Rational::zero()
        }
    );
    for (i, op) in CRealOp::ALL.iter().enumerate() {
        assert_eq!(op.code() as usize, i);
    }
    // normalization of constants; the canonical form round-trips
    assert_eq!(program("c 4/6 ; c 2").to_string(), "c 2/3 ; c 2/1");
    let bad = [
        "",
        " ",
        "c 1 ",
        " c 1",
        "c  1",
        "c 1 ;",
        "c 1 ; ",
        "; c 1",
        "c 1 ;; c 2",
        "c 1 c 2",
        "c 1;c 2",
        "c 1 ; sqrt",
        "c 1 ; sqrt 0 0",
        "c 1 ; add 0",
        "c 1 ; sqrt 00",
        "c 1 ; sqrt +0",
        "c 1 ; sqrt -0",
        "c 1 ; SQRT 0",
        "c 1 ; root 0",
        "c\t1",
    ];
    for t in bad {
        let e = CReal::parse(t, &mut b).unwrap_err().0;
        assert!(
            e.contains("computable real") || e.contains("exact"),
            "{t:?}: {e}"
        );
    }
    assert_eq!(
        CReal::parse("c 1 ; sqrt 1", &mut b).unwrap_err().0,
        "computable real operand must refer to an earlier node"
    );
    assert_eq!(
        CReal::parse("sqrt 0", &mut b).unwrap_err().0,
        "computable real operand must refer to an earlier node"
    );
    assert_eq!(
        CReal::parse("c 1 ; sqrt 340282366920938463463374607431768211455", &mut b)
            .unwrap_err()
            .0,
        "computable real operand must refer to an earlier node"
    );
    assert_eq!(
        CReal::parse("c 1 ; sqrt 340282366920938463463374607431768211456", &mut b)
            .unwrap_err()
            .0,
        "invalid computable real text"
    );
    assert!(CReal::parse("c 1/0", &mut b).is_err());
    // node count limit
    let max = vec!["c 1"; MAX_NODES].join(" ; ");
    assert_eq!(program(&max).nodes().len(), MAX_NODES);
    let over = vec!["c 1"; MAX_NODES + 1].join(" ; ");
    assert_eq!(
        CReal::parse(&over, &mut b).unwrap_err().0,
        "computable real program must have 1..64 nodes"
    );
    assert!(CReal::new(Vec::new(), &mut b).is_err());
    // structural validation of directly built programs
    let node = |op, a, c, v: &str| CNode {
        op,
        a,
        b: c,
        value: rational(v),
    };
    let e = |nodes| CReal::new(nodes, &mut wide()).unwrap_err().0;
    assert_eq!(
        e(vec![node(CRealOp::Const, 1, 0, "1")]),
        "computable real unused operands must be 0"
    );
    assert_eq!(
        e(vec![
            node(CRealOp::Const, 0, 0, "1"),
            node(CRealOp::Neg, 0, 1, "0")
        ]),
        "computable real unused operands must be 0"
    );
    assert_eq!(
        e(vec![
            node(CRealOp::Const, 0, 0, "1"),
            node(CRealOp::Neg, 0, 0, "1")
        ]),
        "computable real non-constant node must carry 0"
    );
    assert_eq!(
        e(vec![
            node(CRealOp::Const, 0, 0, "1"),
            node(CRealOp::Add, 0, 1, "0")
        ]),
        "computable real operand must refer to an earlier node"
    );
    // constants are checked against the precision; long input is unknown, not invalid
    let big = format!("c {}", "9".repeat(40));
    assert!(CReal::parse(&big, &mut budget(64))
        .unwrap_err()
        .0
        .starts_with("UnknownWithinBudget"));
    let long = "c 1".to_string() + &" ; c 1".repeat(200) + &" ".repeat(64 * 160);
    assert!(CReal::parse(&long, &mut budget(64))
        .unwrap_err()
        .0
        .starts_with("UnknownWithinBudget"));
}

// ---------- ordinary DLM std.creal, interpreter and native C ----------

fn header() -> String {
    r#"module test.creal
import std.creal
theory App extends StdCReal {
 fn show(r: Result<IntervalQCalc, Text>) -> Text = match r { err(e) => e, ok(v) => match ivq_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } }
 fn order(r: Result<IntervalQOrder, Text>) -> Text = match r { err(e) => e, ok(v) => int_to_text(field(v, order)) }
 fn ptext(r: Result<CRealCalc, Text>) -> Text = match r { err(e) => e, ok(v) => match creal_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } }
 fn approx(b: ExactBudget, t: Text, n: Nat) -> Text = match creal_parse(b, t) { err(e) => e, ok(x) => show(creal_approx(field(x, budget), field(x, value), n)) }
 fn compare(b: ExactBudget, s: Text, t: Text, n: Nat) -> Text = match creal_parse(b, s) { err(e) => e, ok(x) => match creal_parse(field(x, budget), t) { err(e) => e, ok(y) => order(creal_compare(field(y, budget), field(x, value), field(y, value), n)) } }
"#
    .into()
}

fn call(row: &Row, budget: &str) -> String {
    match row.kind {
        "approx" => format!("approx({budget}, \"{}\", {})", row.x, row.n),
        "compare" => format!("compare({budget}, \"{}\", \"{}\", {})", row.x, row.y, row.n),
        other => panic!("unknown kind {other}"),
    }
}

/// Source budgets allow at most 10^15 work units up to 1024 bits.
fn source_budget(bits: usize) -> String {
    let work = if bits > 1024 {
        "18446744073709551615"
    } else {
        "1000000000000000"
    };
    format!("ExactBudget({bits}, {work})")
}

fn run_both(source: &str, expected: &[String], name: &str) -> Vec<String> {
    let dir = std::env::temp_dir().join(format!("dlm-creal-{}-{name}", std::process::id()));
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
    let native = String::from_utf8(output.stdout).unwrap();
    let native: Vec<String> = native.lines().map(str::to_string).collect();
    assert_eq!(interpreted, native, "{name}: interpreter vs native C");
    for (i, (got, want)) in native.iter().zip(expected).enumerate() {
        if want == "UNKNOWN" {
            assert!(got.starts_with("UnknownWithinBudget"), "{name} #{i}: {got}");
        } else {
            assert_eq!(got, want, "{name} #{i}");
        }
    }
    assert_eq!(native.len(), expected.len(), "{name}");
    std::fs::remove_dir_all(dir).unwrap();
    native
}

#[test]
fn source_matches_oracle_and_native_c_with_precision_boundaries() {
    let mut source = header();
    let mut expected = Vec::new();
    let (mut vectors, mut boundaries) = (0, 0);
    for row in rows().iter().filter(|r| r.source > 0) {
        let bits = row.bits.unwrap_or(256);
        source += &format!(" print_line({})\n", call(row, &source_budget(bits)));
        expected.push(expected_host(row.value).to_string());
        vectors += 1;
        if row.source == 2 && bits > 1 && row.bits.is_some() {
            source += &format!(" print_line({})\n", call(row, &source_budget(bits - 1)));
            expected.push("UNKNOWN".into());
            boundaries += 1;
        }
    }
    source += "}\n";
    assert_eq!((vectors, boundaries), (88, 30));
    let got = run_both(&source, &expected, "oracle");
    // the precision failures are precision failures, not refinement limits
    let mut i = 0;
    for row in rows().iter().filter(|r| r.source > 0) {
        i += 1;
        if row.source == 2 && row.bits.is_some_and(|b| b > 1) {
            assert!(
                got[i].contains("precision") || got[i].contains("input exceeded"),
                "{}: {}",
                row.x,
                got[i]
            );
            i += 1;
        }
    }
}

#[test]
fn source_raw_records_grammar_budgets_and_privacy() {
    let mut source = header();
    source += r#"
 let b = ExactBudget(64, 1000000000000000)
 let zero = BigRational(false, empty_list(Nat), list(1))
 let one = BigRational(false, list(1), list(1))
 print_line(ptext(creal_parse(b, "c 4/6 ; c 3 ; sqrt 1 ; add 0 2 ; div 3 1")))
 print_line(ptext(creal_check(b, CReal(empty_list(CRealNode)))))
 print_line(ptext(creal_check(b, CReal(list(CRealNode(15, 0, 0, zero))))))
 print_line(ptext(creal_check(b, CReal(list(CRealNode(4, 0, 0, zero))))))
 print_line(ptext(creal_check(b, CReal(list(CRealNode(0, 1, 0, one))))))
 print_line(ptext(creal_check(b, CReal(list(CRealNode(0, 0, 0, one), CRealNode(4, 0, 1, zero))))))
 print_line(ptext(creal_check(b, CReal(list(CRealNode(0, 0, 0, one), CRealNode(4, 0, 0, one))))))
 print_line(ptext(creal_check(b, CReal(list(CRealNode(0, 0, 0, BigRational(false, list(2), list(2))))))))
 print_line(ptext(creal_check(ExactBudget(0, 1), CReal(list(CRealNode(0, 0, 0, one))))))
 print_line(ptext(creal_const(b, one)))
 print_line(ptext(creal_parse(b, "")))
 print_line(ptext(creal_parse(b, "c 1 ;")))
 print_line(ptext(creal_parse(b, "c  1")))
 print_line(ptext(creal_parse(b, "c 1 ; sqrt 00")))
 print_line(ptext(creal_parse(b, "c 1 ; sqrt 0 0")))
 print_line(ptext(creal_parse(b, "c 1 ; add 0")))
 print_line(ptext(creal_parse(b, "c 1;c 2")))
 print_line(ptext(creal_parse(b, "c 1 ; root 0")))
 print_line(ptext(creal_parse(b, "c 1 ; sqrt 1")))
 print_line(ptext(creal_parse(b, "c 1 ; sqrt 340282366920938463463374607431768211456")))
 print_line(show(creal_approx(b, CReal(list(CRealNode(0, 0, 0, one))), 4097)))
 print_line(show(creal_approx(ExactBudget(16384, 50), CReal(list(CRealNode(0, 0, 0, BigRational(false, list(2), list(1))), CRealNode(7, 0, 0, zero))), 64)))
}
"#;
    let mut over = String::from("c 1");
    for _ in 0..64 {
        over += " ; c 1";
    }
    let mut max = String::from("c 1");
    for _ in 1..64 {
        max += " ; neg 0";
    }
    source = source.replacen(
        "\n}\n",
        &format!(
            "\n print_line(ptext(creal_parse(b, \"{over}\")))\n print_line(approx(b, \"{max}\", 4))\n}}\n"
        ),
        1,
    );
    let expected: Vec<String> = [
        "c 2/3 ; c 3/1 ; sqrt 1 ; add 0 2 ; div 3 1",
        "computable real program must have 1..64 nodes",
        "invalid computable real operation",
        "computable real operand must refer to an earlier node",
        "computable real unused operands must be 0",
        "computable real unused operands must be 0",
        "computable real non-constant node must carry 0",
        "noncanonical big rational",
        "invalid exact source budget",
        "c 1/1",
        "invalid computable real text",
        "invalid computable real text",
        "invalid computable real text",
        "invalid computable real text",
        "invalid computable real text",
        "invalid computable real text",
        "invalid exact decimal",
        "invalid computable real text",
        "computable real operand must refer to an earlier node",
        "invalid computable real text",
        "computable real precision exceeds 4096 bits",
        "UNKNOWN",
        "computable real program must have 1..64 nodes",
        "[-1/1,-1/1]",
    ]
    .iter()
    .map(|s| s.to_string())
    .collect();
    run_both(&source, &expected, "raw");

    // private helpers stay private to std.creal
    let private = header() + " fn probe() -> Nat = cr_arity(3)\n}\n";
    let dir = std::env::temp_dir().join(format!("dlm-creal-private-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, private).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(!report.ok());
    assert!(report
        .diagnostics
        .iter()
        .any(|d| format!("{d:?}").contains("cr_arity")));
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn source_and_host_agree_on_text_round_trips() {
    // every source fixture program, printed by the source library, equals the host canonical text
    let mut source = header();
    let mut expected = Vec::new();
    for row in rows().iter().filter(|r| r.source > 0) {
        source += &format!(
            " print_line(ptext(creal_parse({}, \"{}\")))\n",
            source_budget(16384),
            row.x
        );
        expected.push(program(row.x).to_string());
    }
    source += "}\n";
    run_both(&source, &expected, "text");
}
