//! Closed rational intervals `[lo, hi]` with exact endpoints (`lo <= hi`).
//!
//! Every operation returns an interval that contains the exact result for
//! every choice of operands in the operand intervals (inclusion property).
//! Arithmetic on rational endpoints is exact, so `add`, `sub`, `neg`, `mul`,
//! `square`, `inv`, `div`, `abs`, `hull` and `intersect` return the tightest
//! such interval. `sqrt` returns a guaranteed enclosure of `[sqrt(lo),
//! sqrt(hi)]` whose bounds are within `2^-k` of the true values; it is exact
//! when the endpoints are rational squares at that resolution. Intervals that
//! contain zero are refused explicitly by `inv` and `div`; `sqrt` refuses
//! negative values. Every operand and every endpoint/intermediate is checked
//! against the budget's precision and charged to its work counter.
//! Results are runtime values, never `StaticProof`.
use super::exact::{ExactBudget, Natural, Rational};
use super::{require, MathError, MathResult};
use std::cmp::Ordering;

/// Largest sqrt resolution exponent `k` (bounds within `2^-k`).
pub const MAX_SQRT_BITS: u32 = 16384;

#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct IntervalQ {
    lo: Rational,
    hi: Rational,
}

/// Certain order of two intervals, or `Overlap` when their values may coincide.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum IntervalOrder {
    /// Every value of the first is below every value of the second.
    Less,
    /// Every value of the first is above every value of the second.
    Greater,
    /// Both are the same single point.
    Equal,
    /// Not decided by the bounds (the intervals intersect, not both points).
    Overlap,
}

fn min_max(values: &[Rational], budget: &mut ExactBudget) -> MathResult<(Rational, Rational)> {
    let mut lo = values[0].clone();
    let mut hi = values[0].clone();
    for v in &values[1..] {
        if v.checked_cmp(&lo, budget)? == Ordering::Less {
            lo = v.clone();
        }
        if v.checked_cmp(&hi, budget)? == Ordering::Greater {
            hi = v.clone();
        }
    }
    Ok((lo, hi))
}

/// One bound of `sqrt(x)` for a nonnegative canonical `x = p/q`:
/// `s = isqrt(p*q*4^k)`, lower `s/(q*2^k)`, upper the same when
/// `s*s = p*q*4^k`, otherwise `(s+1)/(q*2^k)`. So `lower <= sqrt(x) <= upper`
/// and `upper - lower <= 1/(q*2^k) <= 2^-k`.
fn sqrt_bound(x: &Rational, k: u32, upper: bool, budget: &mut ExactBudget) -> MathResult<Rational> {
    require(!x.negative(), "interval square root of negative values")?;
    if x.is_zero() {
        return Ok(Rational::zero());
    }
    let scale = Natural::from_u128(2).checked_pow(u128::from(k), budget)?;
    let square_scale = scale.checked_mul(&scale, budget)?;
    let pq = x.numerator().checked_mul(x.denominator(), budget)?;
    let radicand = pq.checked_mul(&square_scale, budget)?;
    let (s, r) = radicand.checked_sqrt_rem(budget)?;
    let denominator = x.denominator().checked_mul(&scale, budget)?;
    let numerator = if upper && !r.is_zero() {
        s.checked_add(&Natural::from_u128(1), budget)?
    } else {
        s
    };
    Rational::new(false, numerator, denominator, budget)
}

impl IntervalQ {
    /// `[lo, hi]`; refuses `lo > hi`.
    pub fn new(lo: Rational, hi: Rational, budget: &mut ExactBudget) -> MathResult<Self> {
        require(
            lo.checked_cmp(&hi, budget)? != Ordering::Greater,
            "interval bounds out of order",
        )?;
        Ok(Self { lo, hi })
    }
    /// The degenerate interval `[x, x]`.
    pub fn point(x: Rational, budget: &mut ExactBudget) -> MathResult<Self> {
        x.check_budget(budget)?;
        budget.charge(1)?;
        Ok(Self {
            lo: x.clone(),
            hi: x,
        })
    }
    pub fn lo(&self) -> &Rational {
        &self.lo
    }
    pub fn hi(&self) -> &Rational {
        &self.hi
    }
    fn valid_in(&self, budget: &ExactBudget) -> MathResult<()> {
        self.lo.check_budget(budget)?;
        self.hi.check_budget(budget)
    }
    pub fn is_point(&self) -> bool {
        self.lo == self.hi
    }
    /// Strict text `[<lo>,<hi>]`, endpoints as accepted by `Rational::parse`.
    pub fn parse(text: &str, budget: &mut ExactBudget) -> MathResult<Self> {
        require(
            text.len() <= 4 * budget.max_bits() + 8,
            "UnknownWithinBudget: exact interval input exceeded",
        )?;
        let malformed = || MathError("invalid exact interval text: expected [<lo>,<hi>]".into());
        let body = text
            .strip_prefix('[')
            .and_then(|t| t.strip_suffix(']'))
            .ok_or_else(malformed)?;
        let (lo, hi) = body.split_once(',').ok_or_else(malformed)?;
        let lo = Rational::parse(lo, budget)?;
        let hi = Rational::parse(hi, budget)?;
        Self::new(lo, hi, budget)
    }
    /// `lo <= x <= hi`.
    pub fn contains(&self, x: &Rational, budget: &mut ExactBudget) -> MathResult<bool> {
        self.valid_in(budget)?;
        Ok(self.lo.checked_cmp(x, budget)? != Ordering::Greater
            && x.checked_cmp(&self.hi, budget)? != Ordering::Greater)
    }
    pub fn contains_zero(&self, budget: &mut ExactBudget) -> MathResult<bool> {
        self.contains(&Rational::zero(), budget)
    }
    /// `self ⊆ other`.
    pub fn subset(&self, other: &Self, budget: &mut ExactBudget) -> MathResult<bool> {
        self.valid_in(budget)?;
        other.valid_in(budget)?;
        Ok(other.lo.checked_cmp(&self.lo, budget)? != Ordering::Greater
            && self.hi.checked_cmp(&other.hi, budget)? != Ordering::Greater)
    }
    /// The smallest interval containing both.
    pub fn hull(&self, other: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.valid_in(budget)?;
        other.valid_in(budget)?;
        let lo = if other.lo.checked_cmp(&self.lo, budget)? == Ordering::Less {
            &other.lo
        } else {
            &self.lo
        };
        let hi = if other.hi.checked_cmp(&self.hi, budget)? == Ordering::Greater {
            &other.hi
        } else {
            &self.hi
        };
        Ok(Self {
            lo: lo.clone(),
            hi: hi.clone(),
        })
    }
    /// The common part, `None` when the intervals are disjoint.
    pub fn intersect(&self, other: &Self, budget: &mut ExactBudget) -> MathResult<Option<Self>> {
        self.valid_in(budget)?;
        other.valid_in(budget)?;
        let lo = if other.lo.checked_cmp(&self.lo, budget)? == Ordering::Greater {
            &other.lo
        } else {
            &self.lo
        };
        let hi = if other.hi.checked_cmp(&self.hi, budget)? == Ordering::Less {
            &other.hi
        } else {
            &self.hi
        };
        if lo.checked_cmp(hi, budget)? == Ordering::Greater {
            return Ok(None);
        }
        Ok(Some(Self {
            lo: lo.clone(),
            hi: hi.clone(),
        }))
    }
    /// Certain order of the values, decided by the bounds only.
    pub fn compare(&self, other: &Self, budget: &mut ExactBudget) -> MathResult<IntervalOrder> {
        self.valid_in(budget)?;
        other.valid_in(budget)?;
        if self.hi.checked_cmp(&other.lo, budget)? == Ordering::Less {
            return Ok(IntervalOrder::Less);
        }
        if other.hi.checked_cmp(&self.lo, budget)? == Ordering::Less {
            return Ok(IntervalOrder::Greater);
        }
        if self.is_point() && other.is_point() && self.lo == other.lo {
            return Ok(IntervalOrder::Equal);
        }
        Ok(IntervalOrder::Overlap)
    }
    pub fn width(&self, budget: &mut ExactBudget) -> MathResult<Rational> {
        self.hi.checked_sub(&self.lo, budget)
    }
    pub fn midpoint(&self, budget: &mut ExactBudget) -> MathResult<Rational> {
        let sum = self.lo.checked_add(&self.hi, budget)?;
        sum.checked_div(&Rational::from_integer(2), budget)
    }
    pub fn checked_add(&self, other: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        let lo = self.lo.checked_add(&other.lo, budget)?;
        let hi = self.hi.checked_add(&other.hi, budget)?;
        Ok(Self { lo, hi })
    }
    pub fn checked_sub(&self, other: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        let lo = self.lo.checked_sub(&other.hi, budget)?;
        let hi = self.hi.checked_sub(&other.lo, budget)?;
        Ok(Self { lo, hi })
    }
    pub fn checked_neg(&self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.valid_in(budget)?;
        budget.charge(1)?;
        Ok(Self {
            lo: self.hi.negated(),
            hi: self.lo.negated(),
        })
    }
    /// Endpoint products `lo*lo'`, `lo*hi'`, `hi*lo'`, `hi*hi'`; their min and max.
    pub fn checked_mul(&self, other: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        let products = [
            self.lo.checked_mul(&other.lo, budget)?,
            self.lo.checked_mul(&other.hi, budget)?,
            self.hi.checked_mul(&other.lo, budget)?,
            self.hi.checked_mul(&other.hi, budget)?,
        ];
        let (lo, hi) = min_max(&products, budget)?;
        Ok(Self { lo, hi })
    }
    /// `{x*x : x in X}`: `[0, max(lo², hi²)]` when `0 ∈ X`, otherwise the
    /// ordered endpoint squares (tighter than `X*X`).
    pub fn checked_square(&self, budget: &mut ExactBudget) -> MathResult<Self> {
        let a = self.lo.checked_mul(&self.lo, budget)?;
        let b = self.hi.checked_mul(&self.hi, budget)?;
        let (small, large) = min_max(&[a, b], budget)?;
        if self.contains_zero(budget)? {
            Ok(Self {
                lo: Rational::zero(),
                hi: large,
            })
        } else {
            Ok(Self {
                lo: small,
                hi: large,
            })
        }
    }
    /// `{|x| : x in X}`.
    pub fn checked_abs(&self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.valid_in(budget)?;
        budget.charge(1)?;
        let (a, b) = (self.lo.negated(), self.hi.negated());
        if !self.lo.negative() {
            Ok(self.clone())
        } else if self.hi.negative() || self.hi.is_zero() {
            Ok(Self { lo: b, hi: a })
        } else {
            let hi = if a.checked_cmp(&self.hi, budget)? == Ordering::Greater {
                a
            } else {
                self.hi.clone()
            };
            Ok(Self {
                lo: Rational::zero(),
                hi,
            })
        }
    }
    /// `[1/hi, 1/lo]`; an interval that contains zero is refused.
    pub fn checked_inv(&self, budget: &mut ExactBudget) -> MathResult<Self> {
        require(
            !self.contains_zero(budget)?,
            "interval division by an interval containing zero",
        )?;
        let lo = self.hi.reciprocal(budget)?;
        let hi = self.lo.reciprocal(budget)?;
        Ok(Self { lo, hi })
    }
    /// `X * (1/Y)`; `Y` containing zero is refused.
    pub fn checked_div(&self, other: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.valid_in(budget)?;
        let inverse = other.checked_inv(budget)?;
        self.checked_mul(&inverse, budget)
    }
    /// Guaranteed enclosure of `{sqrt(x) : x in X}` for `X >= 0`, each bound
    /// within `2^-k` of the true value (see `sqrt_bound`).
    pub fn checked_sqrt(&self, k: u32, budget: &mut ExactBudget) -> MathResult<Self> {
        self.valid_in(budget)?;
        require(
            k <= MAX_SQRT_BITS,
            "interval square root resolution exceeds 16384 bits",
        )?;
        require(
            !self.lo.negative(),
            "interval square root of negative values",
        )?;
        let lo = sqrt_bound(&self.lo, k, false, budget)?;
        let hi = sqrt_bound(&self.hi, k, true, budget)?;
        Ok(Self { lo, hi })
    }
}

impl std::fmt::Display for IntervalQ {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "[{},{}]", self.lo, self.hi)
    }
}
