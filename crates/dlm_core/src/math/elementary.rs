//! Elementary functions of rational intervals with guaranteed enclosures
//! (numeric criterion N10; algorithm in `docs/ELEMENTARY_FUNCTIONS.md`).
//!
//! Every function takes a resolution `w` and returns a closed interval with
//! dyadic endpoints (denominators `2^w`) that contains the exact image. All
//! intermediate intervals are rounded outward to dyadic numbers, so sizes stay
//! bounded; correctness never depends on how many terms are summed, only on
//! the tail bounds stated at each series. The computable-real refinement loop
//! (`creal`) doubles `w` until the requested width is reached.
use super::exact::{ExactBudget, Natural, Rational};
use super::interval_q::IntervalQ;
use super::MathResult;
use std::cmp::Ordering;

type B<'a> = &'a mut ExactBudget;

fn nat(n: u128) -> Natural {
    Natural::from_u128(n)
}

fn pow2(p: u32, b: B) -> MathResult<Natural> {
    nat(2).checked_pow(u128::from(p), b)
}

fn ratio(neg: bool, n: Natural, d: Natural, b: B) -> MathResult<Rational> {
    Rational::new(neg, n, d, b)
}

fn int(n: i128, b: B) -> MathResult<Rational> {
    ratio(n < 0, nat(n.unsigned_abs()), nat(1), b)
}

fn point(x: Rational, b: B) -> MathResult<IntervalQ> {
    IntervalQ::point(x, b)
}

fn bitlen(n: &Natural) -> i64 {
    n.bit_len() as i64
}

/// `floor(x·2^p)/2^p` or the ceiling.
fn rnd(x: &Rational, p: u32, up: bool, b: B) -> MathResult<Rational> {
    let scale = pow2(p, b)?;
    let num = x.numerator().checked_mul(&scale, b)?;
    let (q, r) = num.div_rem(x.denominator(), b)?;
    let round_away = !r.is_zero() && (up != x.negative());
    let mag = if round_away {
        q.checked_add(&nat(1), b)?
    } else {
        q
    };
    ratio(x.negative(), mag, scale, b)
}

fn out(x: &IntervalQ, p: u32, b: B) -> MathResult<IntervalQ> {
    let lo = rnd(x.lo(), p, false, b)?;
    let hi = rnd(x.hi(), p, true, b)?;
    IntervalQ::new(lo, hi, b)
}

fn scale(x: &IntervalQ, q: &Rational, b: B) -> MathResult<IntervalQ> {
    let q = point(q.clone(), b)?;
    x.checked_mul(&q, b)
}

fn widen(x: &IntervalQ, lo: &Rational, hi: &Rational, b: B) -> MathResult<IntervalQ> {
    let lo = x.lo().checked_sub(lo, b)?;
    let hi = x.hi().checked_add(hi, b)?;
    IntervalQ::new(lo, hi, b)
}

fn le(x: &Rational, y: &Rational, b: B) -> MathResult<bool> {
    Ok(x.checked_cmp(y, b)? != Ordering::Greater)
}

fn eps(p: u32, b: B) -> MathResult<Rational> {
    let d = pow2(p, b)?;
    ratio(false, nat(1), d, b)
}

/// `max(|lo|, |hi|)`.
fn mag(x: &IntervalQ, b: B) -> MathResult<Rational> {
    let lo = x.lo().negated();
    let lo = if x.lo().negative() {
        lo
    } else {
        x.lo().clone()
    };
    let hi = if x.hi().negative() {
        x.hi().negated()
    } else {
        x.hi().clone()
    };
    Ok(if le(&lo, &hi, b)? { hi } else { lo })
}

/// `floor(q)` as a rational integer.
fn floor(q: &Rational, b: B) -> MathResult<Rational> {
    let (n, r) = q.numerator().div_rem(q.denominator(), b)?;
    let n = if q.negative() && !r.is_zero() {
        n.checked_add(&nat(1), b)?
    } else {
        n
    };
    ratio(q.negative(), n, nat(1), b)
}

/// Bounds of `exp(x)` for a rational point: halving to `y = x/2^s <= 1/2`,
/// Taylor series whose tail after the last term `t_i` is at most `t_i`, then
/// `s` squarings; `exp(-x) = 1/exp(x)`.
pub fn exp_point(x: &Rational, w: u32, b: B) -> MathResult<IntervalQ> {
    if x.is_zero() {
        return point(Rational::one(), b);
    }
    if x.negative() {
        let e = exp_point(&x.negated(), w, b)?;
        let inv = e.checked_inv(b)?;
        return out(&inv, w, b);
    }
    let s = (bitlen(x.numerator()) - bitlen(x.denominator()) + 2).max(0) as u32;
    let wp = w + s + 8;
    let eps = eps(wp, b)?;
    let den = x.denominator().checked_mul(&pow2(s, b)?, b)?;
    let y = out(
        &point(ratio(false, x.numerator().clone(), den, b)?, b)?,
        wp,
        b,
    )?;
    let mut t = point(Rational::one(), b)?;
    let mut total = t.clone();
    let mut i: u128 = 0;
    loop {
        i += 1;
        let step = ratio(false, nat(1), nat(i), b)?;
        t = out(&scale(&t.checked_mul(&y, b)?, &step, b)?, wp, b)?;
        total = total.checked_add(&t, b)?;
        if le(t.hi(), &eps, b)? {
            break;
        }
    }
    let hi = total.hi().checked_add(t.hi(), b)?;
    total = IntervalQ::new(total.lo().clone(), hi, b)?;
    for _ in 0..s {
        total = out(&total.checked_mul(&total, b)?, wp, b)?;
    }
    out(&total, w, b)
}

/// `atanh(z)` for `0 <= z <= 1/3`; the tail after a term is below it.
fn atanh_series(z: &Rational, wp: u32, b: B) -> MathResult<IntervalQ> {
    let zero = point(Rational::zero(), b)?;
    if z.is_zero() {
        return Ok(zero);
    }
    let eps = eps(wp, b)?;
    let mut power = out(&point(z.clone(), b)?, wp, b)?;
    let zz = power.checked_mul(&power, b)?;
    let mut total = zero;
    let mut k: i128 = 0;
    loop {
        let inv = ratio(false, nat(1), nat((2 * k + 1) as u128), b)?;
        let term = out(&scale(&power, &inv, b)?, wp, b)?;
        total = total.checked_add(&term, b)?;
        if le(term.hi(), &eps, b)? {
            let hi = total.hi().checked_add(term.hi(), b)?;
            return IntervalQ::new(total.lo().clone(), hi, b);
        }
        power = out(&power.checked_mul(&zz, b)?, wp, b)?;
        k += 1;
    }
}

/// Bounds of `log(x)` for a rational `x > 0`: `x = 2^m·y` with `1 <= y < 2`,
/// `log y = 2 atanh((y-1)/(y+1))`, `log 2 = 2 atanh(1/3)`.
pub fn log_point(x: &Rational, w: u32, b: B) -> MathResult<IntervalQ> {
    let mut m = bitlen(x.numerator()) - bitlen(x.denominator());
    let shift = pow2(m.unsigned_abs() as u32, b)?;
    let mut y = if m >= 0 {
        ratio(
            false,
            x.numerator().clone(),
            x.denominator().checked_mul(&shift, b)?,
            b,
        )?
    } else {
        ratio(
            false,
            x.numerator().checked_mul(&shift, b)?,
            x.denominator().clone(),
            b,
        )?
    };
    if !le(&Rational::one(), &y, b)? {
        m -= 1;
        y = y.checked_mul(&int(2, b)?, b)?;
    }
    let wp = w
        + (m.unsigned_abs() as u128)
            .checked_ilog2()
            .map_or(0, |l| l + 1)
        + 8;
    let one = Rational::one();
    let z = y
        .checked_sub(&one, b)?
        .checked_div(&y.checked_add(&one, b)?, b)?;
    let a = atanh_series(&z, wp, b)?;
    let third = ratio(false, nat(1), nat(3), b)?;
    let half_ln2 = atanh_series(&third, wp, b)?;
    let two = int(2, b)?;
    let sum = scale(&a, &two, b)?.checked_add(&scale(&half_ln2, &int(2 * m as i128, b)?, b)?, b)?;
    out(&sum, w, b)
}

/// `atan(1/q)`, alternating series with remainder below the last term.
fn atan_recip(q: u128, wp: u32, b: B) -> MathResult<IntervalQ> {
    let eps = eps(wp, b)?;
    let z = ratio(false, nat(1), nat(q), b)?;
    let zz = z.checked_mul(&z, b)?;
    let mut power = point(z, b)?;
    let mut total = point(Rational::zero(), b)?;
    let mut k: u128 = 0;
    loop {
        let inv = ratio(false, nat(1), nat(2 * k + 1), b)?;
        let term = out(&scale(&power, &inv, b)?, wp, b)?;
        total = if k.is_multiple_of(2) {
            total.checked_add(&term, b)?
        } else {
            total.checked_sub(&term, b)?
        };
        if le(term.hi(), &eps, b)? {
            return widen(&total, term.hi(), term.hi(), b);
        }
        power = out(&scale(&power, &zz, b)?, wp, b)?;
        k += 1;
    }
}

/// `pi = 16 atan(1/5) - 4 atan(1/239)`.
pub fn pi(w: u32, b: B) -> MathResult<IntervalQ> {
    let wp = w + 8;
    let a = atan_recip(5, wp, b)?;
    let c = atan_recip(239, wp, b)?;
    let v = scale(&a, &int(16, b)?, b)?.checked_sub(&scale(&c, &int(4, b)?, b)?, b)?;
    out(&v, w, b)
}

/// `sin(m)` or `cos(m)` for a rational `|m| <= 4`: alternating Taylor series;
/// from the second term on the magnitudes decrease, so the remainder is at
/// most the last term.
fn taylor(m: &Rational, w: u32, cosine: bool, b: B) -> MathResult<IntervalQ> {
    let wp = w + 8;
    let eps = eps(wp, b)?;
    let mi = out(&point(m.clone(), b)?, wp, b)?;
    let mm = mi.checked_mul(&mi, b)?;
    let mut t = if cosine {
        point(Rational::one(), b)?
    } else {
        mi
    };
    let mut total = t.clone();
    let mut k: i128 = 0;
    loop {
        k += 1;
        let d = if cosine {
            (2 * k - 1) * (2 * k)
        } else {
            (2 * k) * (2 * k + 1)
        };
        let step = ratio(true, nat(1), nat(d as u128), b)?;
        t = out(&scale(&t.checked_mul(&mm, b)?, &step, b)?, wp, b)?;
        total = total.checked_add(&t, b)?;
        if k >= 2 && le(&mag(&t, b)?, &eps, b)? {
            break;
        }
    }
    let bound = mag(&t, b)?;
    let v = widen(&total, &bound, &bound, b)?;
    out(&v, w, b)
}

fn trig_point(c: &Rational, w: u32, cosine: bool, b: B) -> MathResult<IntervalQ> {
    let abs = if c.negative() { c.negated() } else { c.clone() };
    if le(&abs, &int(4, b)?, b)? {
        return taylor(c, w, cosine, b);
    }
    let e = (bitlen(c.numerator()) - bitlen(c.denominator()) + 1).max(0) as u32;
    let wr = w + e + 8;
    let p = pi(wr, b)?;
    let two_pi_lo = p.lo().checked_mul(&int(2, b)?, b)?;
    let q = c
        .checked_div(&two_pi_lo, b)?
        .checked_add(&ratio(false, nat(1), nat(2), b)?, b)?;
    let j = floor(&q, b)?;
    let shift = scale(&p, &j.checked_mul(&int(2, b)?, b)?, b)?;
    let reduced = out(&point(c.clone(), b)?.checked_sub(&shift, b)?, wr, b)?;
    trig(&reduced, w, cosine, b)
}

/// Enclosure of `sin(X)` (`cosine = false`) or `cos(X)`: the mean value form
/// `f(mid) ± radius` (both have Lipschitz constant 1), clipped to `[-1, 1]`.
pub fn trig(x: &IntervalQ, w: u32, cosine: bool, b: B) -> MathResult<IntervalQ> {
    if x.is_point() {
        return trig_point(x.lo(), w, cosine, b);
    }
    let mid = rnd(&x.midpoint(b)?, w + 8, false, b)?;
    let up = x.hi().checked_sub(&mid, b)?;
    let down = mid.checked_sub(x.lo(), b)?;
    let r = if le(&up, &down, b)? { down } else { up };
    let s = trig_point(&mid, w, cosine, b)?;
    let one = Rational::one();
    let minus_one = one.negated();
    let lo = s.lo().checked_sub(&r, b)?;
    let hi = s.hi().checked_add(&r, b)?;
    let lo = if le(&lo, &minus_one, b)? {
        minus_one
    } else {
        lo
    };
    let hi = if le(&one, &hi, b)? { one } else { hi };
    out(&IntervalQ::new(lo, hi, b)?, w, b)
}

/// `exp(X) = [exp(lo), exp(hi)]` (increasing).
pub fn exp(x: &IntervalQ, w: u32, b: B) -> MathResult<IntervalQ> {
    let lo = exp_point(x.lo(), w, b)?;
    let hi = exp_point(x.hi(), w, b)?;
    IntervalQ::new(lo.lo().clone(), hi.hi().clone(), b)
}

/// `log(X) = [log(lo), log(hi)]` for `X > 0` (increasing).
pub fn log(x: &IntervalQ, w: u32, b: B) -> MathResult<IntervalQ> {
    let lo = log_point(x.lo(), w, b)?;
    let hi = log_point(x.hi(), w, b)?;
    IntervalQ::new(lo.lo().clone(), hi.hi().clone(), b)
}
