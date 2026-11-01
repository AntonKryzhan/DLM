//! Independently checkable scalar witnesses (numeric criterion N15).
//!
//! A witness is one line `dlmw1|<kind>|<field>|...|<digest>`: the claim's
//! inputs and output as canonical portable texts (`dlm1:...`), parameters as
//! canonical decimal numbers, and a transport digest (polynomial hash mod
//! 2^61 - 1, base 257, over all bytes before the digest). The digest detects
//! accidental or naive edits only; it is not an authentication.
//!
//! Trust levels: `checked` claims are verified by inverse identities and
//! inequalities that do not call the producing algorithm (quotient, round,
//! decimal, sqrt); `replayed` claims are recomputed by the producing
//! algorithm and compared (enclosure of a computable real).
use super::contract::ErrorCategory;
use super::convert::{self, RoundMode};
use super::creal::CReal;
use super::exact::{ExactBudget, Integer, Natural, Rational};
use super::interval_q::IntervalQ;
use super::portable::{self, PortableNumber};
use super::{MathError, MathResult};
use std::cmp::Ordering;

pub const WITNESS_VERSION: &str = "dlmw1";
pub const MAX_WITNESS_CHARS: usize = 4096;
const MODULUS: u128 = (1 << 61) - 1;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Trust {
    Checked,
    Replayed,
}
impl Trust {
    pub fn name(self) -> &'static str {
        match self {
            Trust::Checked => "checked",
            Trust::Replayed => "replayed",
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CheckedWitness {
    pub kind: &'static str,
    pub trust: Trust,
}

fn invalid() -> MathError {
    MathError("invalid scalar witness".into())
}
fn rejected(kind: &str) -> MathError {
    MathError(format!("scalar witness claim rejected: {kind}"))
}

pub fn digest(text: &str) -> u128 {
    text.bytes()
        .fold(0u128, |h, c| (h * 257 + u128::from(c)) % MODULUS)
}

/// Joins the fields and appends their digest.
pub fn seal(fields: &[String]) -> String {
    let body = format!("{}|", fields.join("|"));
    let d = digest(&body);
    format!("{body}{d}")
}

fn seal_checked(fields: &[String]) -> MathResult<String> {
    let text = seal(fields);
    if text.len() > MAX_WITNESS_CHARS {
        Err(invalid())
    } else {
        Ok(text)
    }
}

fn pn(x: PortableNumber) -> String {
    portable::format(&x)
}

pub fn produce_quotient(a: &Rational, c: &Rational, b: &mut ExactBudget) -> MathResult<String> {
    let q = a.checked_div(c, b)?;
    seal_checked(&[
        "dlmw1".into(),
        "quotient".into(),
        pn(PortableNumber::Rat(a.clone())),
        pn(PortableNumber::Rat(c.clone())),
        pn(PortableNumber::Rat(q)),
    ])
}
pub fn produce_round(q: &Rational, mode: RoundMode, b: &mut ExactBudget) -> MathResult<String> {
    let n = convert::round_rational(q, mode, b)?;
    seal_checked(&[
        "dlmw1".into(),
        "round".into(),
        pn(PortableNumber::Rat(q.clone())),
        mode.name().into(),
        pn(PortableNumber::Int(n)),
    ])
}
pub fn produce_decimal(
    q: &Rational,
    digits: u32,
    mode: RoundMode,
    b: &mut ExactBudget,
) -> MathResult<String> {
    let t = convert::to_decimal(q, digits, mode, b)?;
    seal_checked(&[
        "dlmw1".into(),
        "decimal".into(),
        pn(PortableNumber::Rat(q.clone())),
        digits.to_string(),
        mode.name().into(),
        t,
    ])
}
pub fn produce_sqrt(x: &Rational, k: u32, b: &mut ExactBudget) -> MathResult<String> {
    let r = IntervalQ::point(x.clone(), b)?.checked_sqrt(k, b)?;
    seal_checked(&[
        "dlmw1".into(),
        "sqrt".into(),
        pn(PortableNumber::Rat(x.clone())),
        k.to_string(),
        pn(PortableNumber::Interval(r)),
    ])
}
pub fn produce_enclosure(x: &CReal, n: u32, b: &mut ExactBudget) -> MathResult<String> {
    let v = x.approx(n, b)?;
    seal_checked(&[
        "dlmw1".into(),
        "enclosure".into(),
        pn(PortableNumber::CReal(x.clone())),
        n.to_string(),
        pn(PortableNumber::Interval(v)),
    ])
}

fn small(t: &str, max: u32) -> MathResult<u32> {
    let ok = !t.is_empty()
        && t.len() <= 5
        && t.bytes().all(|c| c.is_ascii_digit())
        && (t == "0" || !t.starts_with('0'));
    let v: u32 = if ok {
        t.parse().map_err(|_| invalid())?
    } else {
        return Err(invalid());
    };
    if v > max {
        return Err(invalid());
    }
    Ok(v)
}
fn payload(t: &str, family: &str, b: &mut ExactBudget) -> MathResult<PortableNumber> {
    portable::parse_as(t, family, b).map_err(|e| {
        if e.category() == Some(ErrorCategory::Budget) {
            e
        } else {
            invalid()
        }
    })
}
fn rat(t: &str, b: &mut ExactBudget) -> MathResult<Rational> {
    match payload(t, "rat", b)? {
        PortableNumber::Rat(q) => Ok(q),
        _ => Err(invalid()),
    }
}
fn ivq(t: &str, b: &mut ExactBudget) -> MathResult<IntervalQ> {
    match payload(t, "ivq", b)? {
        PortableNumber::Interval(q) => Ok(q),
        _ => Err(invalid()),
    }
}
fn mode(t: &str) -> MathResult<RoundMode> {
    RoundMode::parse(t).map_err(|_| invalid())
}
fn cmp(a: &Rational, c: &Rational, b: &mut ExactBudget) -> MathResult<Ordering> {
    a.checked_cmp(c, b)
}
fn int_rat(i: &Integer, b: &mut ExactBudget) -> MathResult<Rational> {
    convert::integer_to_rational(i, b)
}

/// `n = round(q)` by mode, by inequalities on `d = q - n`.
fn round_holds(
    q: &Rational,
    mode: RoundMode,
    n: &Integer,
    b: &mut ExactBudget,
) -> MathResult<bool> {
    let d = q.checked_sub(&int_rat(n, b)?, b)?;
    let zero = Rational::zero();
    let one = Rational::one();
    let half = Rational::new(false, Natural::from_u128(1), Natural::from_u128(2), b)?;
    let floor = |d: &Rational, b: &mut ExactBudget| -> MathResult<bool> {
        Ok(cmp(d, &zero, b)? != Ordering::Less && cmp(d, &one, b)? == Ordering::Less)
    };
    let ceil = |d: &Rational, b: &mut ExactBudget| -> MathResult<bool> {
        Ok(cmp(d, &zero, b)? != Ordering::Greater
            && cmp(d, &one.negated(), b)? == Ordering::Greater)
    };
    Ok(match mode {
        RoundMode::Floor => floor(&d, b)?,
        RoundMode::Ceil => ceil(&d, b)?,
        RoundMode::Trunc => {
            if q.negative() {
                ceil(&d, b)?
            } else {
                floor(&d, b)?
            }
        }
        RoundMode::HalfEven => {
            let hi = cmp(&d, &half, b)?;
            let lo = cmp(&d, &half.negated(), b)?;
            hi != Ordering::Greater
                && lo != Ordering::Less
                && (hi == Ordering::Less && lo == Ordering::Greater
                    || n.magnitude()
                        .to_u128()
                        .map(|v| v % 2 == 0)
                        .unwrap_or_else(|| {
                            n.magnitude()
                                .to_string()
                                .ends_with(['0', '2', '4', '6', '8'])
                        }))
        }
    })
}

fn pow2(k: u32, b: &mut ExactBudget) -> MathResult<Rational> {
    let p = Natural::from_u128(2).checked_pow(u128::from(k), b)?;
    Rational::new(false, p, Natural::from_u128(1), b)
}

/// Checks a witness line; returns its kind and trust level.
pub fn check(text: &str, b: &mut ExactBudget) -> MathResult<CheckedWitness> {
    if text.len() > MAX_WITNESS_CHARS {
        return Err(invalid());
    }
    let fields: Vec<&str> = text.split('|').collect();
    let version = fields[0];
    if version != WITNESS_VERSION {
        let numbered = version.len() > 4
            && version.starts_with("dlmw")
            && version[4..].bytes().all(|c| c.is_ascii_digit());
        return Err(if numbered {
            MathError("unsupported scalar witness version".into())
        } else {
            invalid()
        });
    }
    if !text
        .bytes()
        .all(|c| (32..=126).contains(&c) && c != b'"' && c != b'\\')
        || fields.len() < 3
    {
        return Err(invalid());
    }
    let (body, sealed) = text.rsplit_once('|').unwrap();
    let ok_digest = !sealed.is_empty()
        && sealed.len() <= 19
        && sealed.bytes().all(|c| c.is_ascii_digit())
        && (sealed == "0" || !sealed.starts_with('0'));
    if !ok_digest {
        return Err(invalid());
    }
    if sealed.parse::<u128>().ok() != Some(digest(&format!("{body}|"))) {
        return Err(MathError("scalar witness digest mismatch".into()));
    }
    let kind = fields[1];
    let arity = match kind {
        "quotient" | "round" | "sqrt" | "enclosure" => 6,
        "decimal" => 7,
        _ => return Err(MathError(format!("unknown scalar witness kind `{kind}`"))),
    };
    if fields.len() != arity {
        return Err(invalid());
    }
    let f = &fields[2..arity - 1];
    let (name, trust, holds) = match kind {
        "quotient" => {
            let (a, c, q) = (rat(f[0], b)?, rat(f[1], b)?, rat(f[2], b)?);
            (
                "quotient",
                Trust::Checked,
                !c.is_zero() && cmp(&q.checked_mul(&c, b)?, &a, b)? == Ordering::Equal,
            )
        }
        "round" => {
            let (q, m) = (rat(f[0], b)?, mode(f[1])?);
            let PortableNumber::Int(n) = payload(f[2], "int", b)? else {
                return Err(invalid());
            };
            ("round", Trust::Checked, round_holds(&q, m, &n, b)?)
        }
        "decimal" => {
            let (q, digits, m) = (rat(f[0], b)?, small(f[1], 4096)?, mode(f[2])?);
            let d = convert::parse_decimal(f[3], b).map_err(|e| {
                if e.category() == Some(ErrorCategory::Budget) {
                    e
                } else {
                    invalid()
                }
            })?;
            if convert::to_decimal(&d, digits, RoundMode::Trunc, b)? != f[3] {
                return Err(invalid());
            }
            let scale = pow10(digits, b)?;
            let n = convert::to_integer_exact(&d.checked_mul(&scale, b)?, b)?;
            (
                "decimal",
                Trust::Checked,
                round_holds(&q.checked_mul(&scale, b)?, m, &n, b)?,
            )
        }
        "sqrt" => {
            let (x, k, r) = (rat(f[0], b)?, small(f[1], 16384)?, ivq(f[2], b)?);
            let zero = Rational::zero();
            let lower = cmp(r.lo(), &zero, b)? != Ordering::Greater
                || cmp(&r.lo().checked_mul(r.lo(), b)?, &x, b)? != Ordering::Greater;
            let upper = cmp(r.hi(), &zero, b)? != Ordering::Less
                && cmp(&r.hi().checked_mul(r.hi(), b)?, &x, b)? != Ordering::Less;
            let width = r
                .hi()
                .checked_sub(r.lo(), b)?
                .checked_mul(&pow2(k, b)?, b)?;
            let narrow = cmp(&width, &Rational::from_integer(2), b)? != Ordering::Greater;
            (
                "sqrt",
                Trust::Checked,
                !x.negative() && lower && upper && narrow,
            )
        }
        _ => {
            let PortableNumber::CReal(x) = payload(f[0], "creal", b)? else {
                return Err(invalid());
            };
            let (n, r) = (small(f[1], 4096)?, ivq(f[2], b)?);
            ("enclosure", Trust::Replayed, x.approx(n, b)? == r)
        }
    };
    if holds {
        Ok(CheckedWitness { kind: name, trust })
    } else {
        Err(rejected(name))
    }
}

fn pow10(digits: u32, b: &mut ExactBudget) -> MathResult<Rational> {
    let p = Natural::from_u128(10).checked_pow(u128::from(digits), b)?;
    Rational::new(false, p, Natural::from_u128(1), b)
}
