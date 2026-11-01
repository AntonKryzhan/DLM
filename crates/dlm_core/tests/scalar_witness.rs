//! v0.106 — independently checkable scalar witnesses (N15): host
//! `math::scalar_witness` and source `std.scalar_witness` produce identical
//! lines, accept them with the same trust level, and refuse tampered,
//! resealed-false, malformed and foreign-version lines with the same messages
//! (interpreter and native C).
use dlm_core::math::contract::ErrorCategory;
use dlm_core::math::convert::RoundMode;
use dlm_core::math::creal::CReal;
use dlm_core::math::exact::{ExactBudget, Integer, Rational};
use dlm_core::math::interval_q::IntervalQ;
use dlm_core::math::portable::{self, PortableNumber};
use dlm_core::math::scalar_witness::{self as sw, Trust};
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use std::path::{Path, PathBuf};
use std::process::Command;

fn b() -> ExactBudget {
    ExactBudget::new(256, 1_000_000_000_000_000).unwrap()
}
fn q(t: &str) -> Rational {
    Rational::parse(t, &mut b()).unwrap()
}
fn check(t: &str) -> String {
    match sw::check(t, &mut b()) {
        Ok(c) => format!("{} {}", c.trust.name(), c.kind),
        Err(e) => e.0,
    }
}
fn fields(t: &str) -> Vec<String> {
    let mut f: Vec<String> = t.split('|').map(str::to_string).collect();
    f.pop();
    f
}
fn reseal(t: &str, i: usize, v: String) -> String {
    let mut f = fields(t);
    f[i] = v;
    sw::seal(&f)
}
fn rat_text(x: Rational) -> String {
    portable::format(&PortableNumber::Rat(x))
}

const MODES: [(&str, RoundMode, usize); 4] = [
    ("floor", RoundMode::Floor, 0),
    ("ceil", RoundMode::Ceil, 1),
    ("trunc", RoundMode::Trunc, 2),
    ("half_even", RoundMode::HalfEven, 3),
];
const QUOTIENTS: [(&str, &str); 3] = [("22/7", "-3"), ("0", "5"), ("-1/1000", "7/3")];
const ROUNDS: [&str; 6] = ["5/2", "-5/2", "7/2", "-1/3", "22/7", "-7/2"];
const DECIMALS: [(&str, u32, usize); 4] = [
    ("22/7", 5, 3),
    ("-1/8", 2, 3),
    ("2/3", 0, 0),
    ("-1/1000", 2, 2),
];
const SQRTS: [(&str, u32); 3] = [("2", 20), ("1/4", 10), ("0", 8)];
const ENCLOSURES: [(&str, u32); 2] = [("c 2 ; sqrt 0", 16), ("pi", 8)];

fn host_witnesses() -> Vec<String> {
    let bb = &mut b();
    let mut out = Vec::new();
    for (a, c) in QUOTIENTS {
        out.push(sw::produce_quotient(&q(a), &q(c), bb).unwrap());
    }
    for x in ROUNDS {
        for (_, m, _) in MODES {
            out.push(sw::produce_round(&q(x), m, bb).unwrap());
        }
    }
    for (x, d, m) in DECIMALS {
        out.push(sw::produce_decimal(&q(x), d, MODES[m].1, bb).unwrap());
    }
    for (x, k) in SQRTS {
        out.push(sw::produce_sqrt(&q(x), k, bb).unwrap());
    }
    for (x, n) in ENCLOSURES {
        out.push(sw::produce_enclosure(&CReal::parse(x, bb).unwrap(), n, bb).unwrap());
    }
    out
}

/// False claims with a correct digest, malformed lines and foreign versions.
fn refused_lines(valid: &[String]) -> Vec<(String, &'static str)> {
    let bb = &mut b();
    let mut out = Vec::new();
    for w in valid {
        let f = fields(w);
        let kind = f[1].clone();
        let false_claim = match kind.as_str() {
            "quotient" => reseal(
                w,
                4,
                rat_text(q(&f[4][9..]).checked_add(&Rational::one(), bb).unwrap()),
            ),
            "round" => {
                let PortableNumber::Int(n) = portable::parse(&f[4], bb).unwrap() else {
                    unreachable!()
                };
                reseal(
                    w,
                    4,
                    portable::format(&PortableNumber::Int(
                        n.checked_add(&Integer::from_i128(1), bb).unwrap(),
                    )),
                )
            }
            "decimal" => {
                let t = &f[5];
                let last = t.chars().last().unwrap().to_digit(10).unwrap();
                reseal(w, 5, format!("{}{}", &t[..t.len() - 1], (last + 1) % 10))
            }
            "sqrt" | "enclosure" => {
                let PortableNumber::Interval(r) = portable::parse(&f[4], bb).unwrap() else {
                    unreachable!()
                };
                let wider = IntervalQ::new(
                    r.lo().clone(),
                    r.hi().checked_add(&Rational::one(), bb).unwrap(),
                    bb,
                )
                .unwrap();
                reseal(w, 4, portable::format(&PortableNumber::Interval(wider)))
            }
            _ => unreachable!(),
        };
        out.push((false_claim, "rejected"));
        // an edit without resealing
        let mut edited = w.clone();
        edited.insert(w.find("dlm1:").unwrap() + 9, '1');
        out.push((edited, "mismatch"));
    }
    let base = &valid[0];
    out.push((reseal(base, 2, "dlm1:rat:6/8".into()), "invalid"));
    out.push((reseal(base, 2, "dlm1:int:6".into()), "invalid"));
    out.push((reseal(base, 1, "sum".into()), "unknown"));
    out.push((sw::seal(&fields(base)[..4]), "invalid"));
    out.push((
        sw::seal(&[
            "dlmw1".into(),
            "round".into(),
            "dlm1:rat:1/2".into(),
            "up".into(),
            "dlm1:int:1".into(),
        ]),
        "invalid",
    ));
    out.push((
        sw::seal(&[
            "dlmw1".into(),
            "decimal".into(),
            "dlm1:rat:1/2".into(),
            "01".into(),
            "floor".into(),
            "0.5".into(),
        ]),
        "invalid",
    ));
    out.push((
        sw::seal(&[
            "dlmw1".into(),
            "decimal".into(),
            "dlm1:rat:1/2".into(),
            "1".into(),
            "floor".into(),
            "5e-1".into(),
        ]),
        "invalid",
    ));
    out.push((
        sw::seal(&[
            "dlmw1".into(),
            "sqrt".into(),
            "dlm1:rat:-1/1".into(),
            "4".into(),
            "dlm1:ivq:[0/1,1/1]".into(),
        ]),
        "rejected",
    ));
    out.push((
        sw::seal(&[
            "dlmw1".into(),
            "quotient".into(),
            "dlm1:rat:1/1".into(),
            "dlm1:rat:0/1".into(),
            "dlm1:rat:0/1".into(),
        ]),
        "rejected",
    ));
    out.push((base.replace("dlmw1", "dlmw2"), "version"));
    out.push((base.replace("dlmw1", "DLMW1"), "invalid"));
    out.push((format!("{base}0"), "invalid"));
    out.push((
        format!(
            "{}{}",
            &base[..base.len() - 1],
            if base.ends_with('1') { '2' } else { '1' }
        ),
        "mismatch",
    ));
    let (body, d) = base.rsplit_once('|').unwrap();
    out.push((format!("{body}|0{d}"), "invalid"));
    out.push((format!("{body}é|{d}"), "invalid"));
    out.push(("".into(), "invalid"));
    out.push(("dlmw1".into(), "invalid"));
    out.push(("dlmw1|0".into(), "invalid"));
    // Reject oversized input before the source parser spends its recursion fuel.
    out.push((format!("dlmw1|{}", "x".repeat(5000)), "invalid"));
    out.push((format!("dlmw{}|x|0", "9".repeat(90)), "version"));
    out
}

#[test]
fn producers_refuse_witnesses_that_exceed_the_transport_limit() {
    let bb = &mut ExactBudget::new(16384, u64::MAX / 4).unwrap();
    let large = Rational::parse(&format!("1{}", "0".repeat(2100)), bb).unwrap();
    let real = CReal::parse(&format!("c {large}"), bb).unwrap();
    for result in [
        sw::produce_quotient(&large, &Rational::one(), bb),
        sw::produce_round(&large, RoundMode::Floor, bb),
        sw::produce_decimal(&Rational::zero(), 4096, RoundMode::Trunc, bb),
        sw::produce_sqrt(&large, 0, bb),
        sw::produce_enclosure(&real, 0, bb),
    ] {
        let error = result.unwrap_err();
        assert_eq!(error.0, "invalid scalar witness");
    }
    let witness = boundary_witness();
    assert_eq!(sw::check(&witness, bb).unwrap().trust, Trust::Checked);
}

fn boundary_witness() -> String {
    (3900..=4096)
        .map(|zeros| {
            sw::seal(&[
                "dlmw1".into(),
                "quotient".into(),
                "dlm1:rat:0/1".into(),
                format!("dlm1:rat:1{}/1", "0".repeat(zeros)),
                "dlm1:rat:0/1".into(),
            ])
        })
        .find(|text| text.len() == sw::MAX_WITNESS_CHARS)
        .expect("a valid witness at the transport limit")
}

#[test]
fn host_accepts_produced_witnesses_and_refuses_every_edit() {
    let valid = host_witnesses();
    assert_eq!(valid.len(), 3 + 24 + 4 + 3 + 2);
    for w in &valid {
        let c = sw::check(w, &mut b()).unwrap_or_else(|e| panic!("{w}: {e}"));
        assert_eq!(
            c.trust,
            if c.kind == "enclosure" {
                Trust::Replayed
            } else {
                Trust::Checked
            }
        );
        assert!(w.starts_with("dlmw1|") && w.split('|').nth(1) == Some(c.kind));
        // every single-character edit without resealing is refused
        for (i, ch) in w.char_indices() {
            for r in ['0', '7', '|', '-', 'x'] {
                if r != ch {
                    let mut t = w.clone();
                    t.replace_range(i..i + ch.len_utf8(), &r.to_string());
                    assert!(sw::check(&t, &mut b()).is_err(), "{t}");
                }
            }
        }
    }
    for (t, want) in refused_lines(&valid) {
        let e = sw::check(&t, &mut b()).unwrap_err();
        let ok = match want {
            "rejected" => e.0.starts_with("scalar witness claim rejected: "),
            "mismatch" => e.0 == "scalar witness digest mismatch",
            "invalid" => e.0 == "invalid scalar witness",
            "unknown" => e.0 == "unknown scalar witness kind `sum`",
            "version" => e.0 == "unsupported scalar witness version",
            _ => false,
        };
        assert!(ok, "{t}: {e}");
        assert_eq!(e.category(), Some(ErrorCategory::InvalidInput), "{e}");
    }
    // half-even ties need an even result; round witnesses for ties are exact
    assert_eq!(
        check(&sw::seal(&[
            "dlmw1".into(),
            "round".into(),
            "dlm1:rat:5/2".into(),
            "half_even".into(),
            "dlm1:int:3".into()
        ])),
        "scalar witness claim rejected: round"
    );
    assert_eq!(
        check(&sw::seal(&[
            "dlmw1".into(),
            "round".into(),
            "dlm1:rat:5/2".into(),
            "half_even".into(),
            "dlm1:int:2".into()
        ])),
        "checked round"
    );
    // a checker without enough work refuses with a budget error, never accepts
    let w = &valid[valid.len() - 1];
    let e = sw::check(w, &mut ExactBudget::new(256, 10).unwrap()).unwrap_err();
    assert_eq!(e.category(), Some(ErrorCategory::Budget));
    assert_eq!(sw::digest(""), 0);
}

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

#[test]
fn source_produces_identical_witnesses_and_agrees_on_every_check() {
    let valid = host_witnesses();
    let refused = refused_lines(&valid);
    let mut source = String::from(
        r#"module test.scalar_witness
import std.scalar_witness
theory App extends StdScalarWitness {
 let b = ExactBudget(256, 1000000000000000)
 fn q(t: Text) -> BigRational = match bigrat_parse(b, t) { err(e) => BigRational(false, empty_list(Nat), list(1)), ok(v) => field(v, value) }
 fn x(t: Text) -> CReal = match creal_parse(b, t) { err(e) => CReal(empty_list(CRealNode)), ok(v) => field(v, value) }
 fn tt(r: Result<BigTextCalc, Text>) -> Text = match r { err(e) => e, ok(v) => field(v, value) }
"#,
    );
    let mut expected = valid.clone();
    for (a, c) in QUOTIENTS {
        source += &format!(" print_line(tt(sw_quotient(b, q(\"{a}\"), q(\"{c}\"))))\n");
    }
    for v in ROUNDS {
        for (_, _, m) in MODES {
            source += &format!(" print_line(tt(sw_round(b, q(\"{v}\"), {m})))\n");
        }
    }
    for (v, d, m) in DECIMALS {
        source += &format!(" print_line(tt(sw_decimal(b, q(\"{v}\"), {d}, {m})))\n");
    }
    for (v, k) in SQRTS {
        source += &format!(" print_line(tt(sw_sqrt(b, q(\"{v}\"), {k})))\n");
    }
    for (v, n) in ENCLOSURES {
        source += &format!(" print_line(tt(sw_enclosure(b, x(\"{v}\"), {n})))\n");
    }
    for t in valid.iter().chain(refused.iter().map(|(t, _)| t)) {
        source += &format!(" print_line(tt(sw_check(b, \"{t}\")))\n");
        expected.push(check(t));
    }
    // All producers apply the same transport bound; an exactly 4096-character
    // witness remains valid in the host, interpreter and native C.
    source += " let wide = ExactBudget(16384, 18446744073709551615)\n";
    source += &format!(" let large = match bigrat_parse(wide, \"1{}\") {{ err(e) => BigRational(false, empty_list(Nat), list(1)), ok(v) => field(v, value) }}\n", "0".repeat(2100));
    for call in [
        "sw_quotient(wide, large, BigRational(false, list(1), list(1)))",
        "sw_round(wide, large, 0)",
        "sw_enclosure(wide, CReal(list(CRealNode(0, 0, 0, large))), 0)",
    ] {
        source += &format!(" print_line(tt({call}))\n");
        expected.push("invalid scalar witness".to_string());
    }
    source += &format!(
        " print_line(tt(sw_check(wide, \"{}\")))\n",
        boundary_witness()
    );
    expected.push("checked quotient".to_string());
    source += " print_line(tt(sw_check(ExactBudget(256, 10), \"";
    source += &valid[valid.len() - 1];
    source += "\")))\n}\n";
    let dir = std::env::temp_dir().join(format!("dlm-scalar-witness-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, &source).unwrap();
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
    let (last, lines) = native.split_last().unwrap();
    for (i, (got, want)) in lines.iter().zip(&expected).enumerate() {
        assert_eq!(got, want, "#{i}");
    }
    assert_eq!(lines.len(), expected.len());
    assert!(last.starts_with("UnknownWithinBudget: "), "{last}");
    std::fs::remove_dir_all(dir).unwrap();
}
