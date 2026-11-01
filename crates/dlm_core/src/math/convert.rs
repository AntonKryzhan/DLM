//! Explicit conversions between numeric families (numeric criterion N11).
//!
//! Exact conversions fail when the value is not representable (`to_integer_exact`,
//! `to_decimal_exact`, `to_u128`, `to_i128`); every lossy conversion names its
//! rounding mode. Conversions of computable reals are decided by their
//! enclosures: all four modes are monotone, so equal roundings of both bounds
//! are the rounding of the number; otherwise the precision is raised up to the
//! requested limit and the result is UnknownWithinBudget, never a guess.
use super::creal::CReal;
use super::exact::{ExactBudget, Integer, Natural, Rational};
use super::{require, MathError, MathResult};
use std::cmp::Ordering;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RoundMode {
    Floor,
    Ceil,
    /// Toward zero.
    Trunc,
    /// Nearest, ties to even.
    HalfEven,
}

impl RoundMode {
    pub const ALL: [RoundMode; 4] = [
        RoundMode::Floor,
        RoundMode::Ceil,
        RoundMode::Trunc,
        RoundMode::HalfEven,
    ];
    pub fn name(self) -> &'static str {
        match self {
            RoundMode::Floor => "floor",
            RoundMode::Ceil => "ceil",
            RoundMode::Trunc => "trunc",
            RoundMode::HalfEven => "half_even",
        }
    }
    pub fn parse(name: &str) -> MathResult<Self> {
        Self::ALL
            .into_iter()
            .find(|m| m.name() == name)
            .ok_or_else(|| MathError(format!("unknown rounding mode `{name}`")))
    }
}

/// Largest decimal digit count and exponent accepted.
pub const MAX_DECIMAL_DIGITS: u32 = 4096;

fn nat(n: u128) -> Natural {
    Natural::from_u128(n)
}

/// `round(q)` to an integer by `mode` (exact).
pub fn round_rational(q: &Rational, mode: RoundMode, b: &mut ExactBudget) -> MathResult<Integer> {
    let (f, r) = q.numerator().div_rem(q.denominator(), b)?;
    let exact = r.is_zero();
    // |q| = f + r/d; away = add one to the magnitude
    let away = !exact
        && match mode {
            RoundMode::Floor => q.negative(),
            RoundMode::Ceil => !q.negative(),
            RoundMode::Trunc => false,
            RoundMode::HalfEven => {
                let twice = r.checked_mul(&nat(2), b)?;
                match twice.cmp(q.denominator()) {
                    Ordering::Greater => true,
                    Ordering::Less => false,
                    Ordering::Equal => f.div_rem(&nat(2), b)?.1 != Natural::zero(),
                }
            }
        };
    let m = if away { f.checked_add(&nat(1), b)? } else { f };
    Ok(Integer::from_parts(q.negative(), m))
}

/// The integer `q`, or an error when `q` is not an integer.
pub fn to_integer_exact(q: &Rational, b: &mut ExactBudget) -> MathResult<Integer> {
    require(q.denominator() == &nat(1), "rational is not an integer")?;
    round_rational(q, RoundMode::Trunc, b)
}

pub fn integer_to_rational(i: &Integer, b: &mut ExactBudget) -> MathResult<Rational> {
    Rational::new(i.negative(), i.magnitude().clone(), nat(1), b)
}

/// `round(q·2^p)/2^p`.
pub fn to_dyadic(
    q: &Rational,
    p: u32,
    mode: RoundMode,
    b: &mut ExactBudget,
) -> MathResult<Rational> {
    let scale = nat(2).checked_pow(u128::from(p), b)?;
    let scaled = Rational::new(
        q.negative(),
        q.numerator().checked_mul(&scale, b)?,
        q.denominator().clone(),
        b,
    )?;
    let i = round_rational(&scaled, mode, b)?;
    Rational::new(i.negative(), i.magnitude().clone(), scale, b)
}

fn fixed_text(i: &Integer, digits: u32) -> String {
    let mut m = i.magnitude().to_string();
    let sign = if i.negative() { "-" } else { "" };
    if digits == 0 {
        return format!("{sign}{m}");
    }
    let d = digits as usize;
    if m.len() <= d {
        m = "0".repeat(d + 1 - m.len()) + &m;
    }
    let (int, frac) = m.split_at(m.len() - d);
    format!("{sign}{int}.{frac}")
}

/// `q` rounded by `mode` to exactly `digits` fractional decimal digits.
pub fn to_decimal(
    q: &Rational,
    digits: u32,
    mode: RoundMode,
    b: &mut ExactBudget,
) -> MathResult<String> {
    require(
        digits <= MAX_DECIMAL_DIGITS,
        "decimal digit count exceeds 4096",
    )?;
    let scale = nat(10).checked_pow(u128::from(digits), b)?;
    let scaled = Rational::new(
        q.negative(),
        q.numerator().checked_mul(&scale, b)?,
        q.denominator().clone(),
        b,
    )?;
    Ok(fixed_text(&round_rational(&scaled, mode, b)?, digits))
}

/// The shortest exact decimal of `q`, or an error when `q` is not a finite decimal.
pub fn to_decimal_exact(q: &Rational, b: &mut ExactBudget) -> MathResult<String> {
    let (mut d, mut twos, mut fives) = (q.denominator().clone(), 0u32, 0u32);
    for (p, count) in [(2u128, &mut twos), (5u128, &mut fives)] {
        loop {
            let (quot, rem) = d.div_rem(&nat(p), b)?;
            if !rem.is_zero() {
                break;
            }
            d = quot;
            *count += 1;
        }
    }
    require(d == nat(1), "rational is not a finite decimal")?;
    to_decimal(q, twos.max(fives), RoundMode::Trunc, b)
}

/// Exact value of `[-]D+[.D+][e[-]D+]` (`|exponent| <= 4096`).
pub fn parse_decimal(text: &str, b: &mut ExactBudget) -> MathResult<Rational> {
    let invalid = || MathError("invalid decimal number".into());
    require(
        text.len() <= 2 * b.max_bits() + 16,
        "UnknownWithinBudget: decimal input exceeded",
    )?;
    let (negative, rest) = match text.strip_prefix('-') {
        Some(r) => (true, r),
        None => (false, text),
    };
    let (mantissa, exponent) = match rest.split_once('e') {
        Some((m, e)) => (m, Some(e)),
        None => (rest, None),
    };
    let (int, frac) = match mantissa.split_once('.') {
        Some((i, f)) => (i, f),
        None => (mantissa, ""),
    };
    let digits = |s: &str| !s.is_empty() && s.bytes().all(|c| c.is_ascii_digit());
    if !digits(int) || (mantissa.contains('.') && !digits(frac)) {
        return Err(invalid());
    }
    let exp: i64 = match exponent {
        None => 0,
        Some(e) => {
            let (neg, d) = match e.strip_prefix('-') {
                Some(d) => (true, d),
                None => (false, e),
            };
            if !digits(d) || d.len() > 5 {
                return Err(invalid());
            }
            let v: i64 = d.parse().map_err(|_| invalid())?;
            if neg {
                -v
            } else {
                v
            }
        }
    };
    require(
        exp.unsigned_abs() <= u64::from(MAX_DECIMAL_DIGITS),
        "decimal exponent out of range",
    )?;
    let n = Natural::parse(&format!("{int}{frac}"), b)?;
    let shift = exp - frac.len() as i64;
    let p = nat(10).checked_pow(u128::from(shift.unsigned_abs()), b)?;
    if shift >= 0 {
        Rational::new(negative, n.checked_mul(&p, b)?, nat(1), b)
    } else {
        Rational::new(negative, n, p, b)
    }
}

pub fn to_u128(i: &Integer) -> MathResult<u128> {
    require(!i.negative(), "integer does not fit in Nat")?;
    i.magnitude()
        .to_u128()
        .ok_or_else(|| MathError("integer does not fit in Nat".into()))
}

pub fn to_i128(i: &Integer) -> MathResult<i128> {
    let m = i
        .magnitude()
        .to_u128()
        .ok_or_else(|| MathError("integer does not fit in Int".into()))?;
    if i.negative() {
        require(m <= 1u128 << 127, "integer does not fit in Int")?;
        Ok((m as i128).wrapping_neg())
    } else {
        i128::try_from(m).map_err(|_| MathError("integer does not fit in Int".into()))
    }
}

/// Precisions tried for a decided conversion: 0, 8, 16, 32, ... below
/// `max_n`, then `max_n`.
pub fn schedule(max_n: u32) -> Vec<u32> {
    let mut out = vec![0];
    let mut n = 8;
    while n < max_n {
        out.push(n);
        n *= 2;
    }
    if max_n > 0 {
        out.push(max_n);
    }
    out
}

fn decided<T: PartialEq>(
    x: &CReal,
    max_n: u32,
    b: &mut ExactBudget,
    mut f: impl FnMut(&Rational, &mut ExactBudget) -> MathResult<T>,
) -> MathResult<T> {
    require(
        max_n <= super::creal::MAX_PRECISION,
        "computable real precision exceeds 4096 bits",
    )?;
    for n in schedule(max_n) {
        let v = x.approx(n, b)?;
        let lo = f(v.lo(), b)?;
        if lo == f(v.hi(), b)? {
            return Ok(lo);
        }
    }
    Err(MathError(format!(
        "UnknownWithinBudget: rounding undecided at precision {max_n}"
    )))
}

/// `round(x)` decided by the enclosures of `x` up to precision `max_n`.
pub fn creal_round(
    x: &CReal,
    mode: RoundMode,
    max_n: u32,
    b: &mut ExactBudget,
) -> MathResult<Integer> {
    decided(x, max_n, b, |q, b| round_rational(q, mode, b))
}

/// `x` to `digits` fractional decimal digits by `mode`, decided as `creal_round`.
pub fn creal_decimal(
    x: &CReal,
    digits: u32,
    mode: RoundMode,
    max_n: u32,
    b: &mut ExactBudget,
) -> MathResult<String> {
    decided(x, max_n, b, |q, b| to_decimal(q, digits, mode, b))
}
