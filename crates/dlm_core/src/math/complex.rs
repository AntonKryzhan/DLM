//! Exact Gaussian rationals Q(i): `re + im*i` with canonical exact rational
//! components (`math::exact::Rational`). Every operation validates its
//! operands against the precision of the budget, threads the same work
//! budget through its component operations and checks every intermediate
//! rational value (products, sums, the squared modulus) against the
//! precision, exactly in the order documented in `docs/EXACT_COMPLEX.md`.
//! Results are runtime values, never `StaticProof`.
use super::exact::{ExactBudget, Rational};
use super::{require, MathError, MathResult};

#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct ComplexRational {
    re: Rational,
    im: Rational,
}

impl ComplexRational {
    pub fn zero() -> Self {
        Self {
            re: Rational::zero(),
            im: Rational::zero(),
        }
    }
    pub fn one() -> Self {
        Self {
            re: Rational::one(),
            im: Rational::zero(),
        }
    }
    /// The imaginary unit `0 + 1i`.
    pub fn i() -> Self {
        Self {
            re: Rational::zero(),
            im: Rational::one(),
        }
    }
    /// Both components must fit the budget's precision.
    pub fn new(re: Rational, im: Rational, budget: &mut ExactBudget) -> MathResult<Self> {
        re.check_budget(budget)?;
        im.check_budget(budget)?;
        budget.charge(1)?;
        Ok(Self { re, im })
    }
    pub fn from_rational(re: Rational, budget: &mut ExactBudget) -> MathResult<Self> {
        Self::new(re, Rational::zero(), budget)
    }
    pub fn re(&self) -> &Rational {
        &self.re
    }
    pub fn im(&self) -> &Rational {
        &self.im
    }
    pub fn is_zero(&self) -> bool {
        self.re.is_zero() && self.im.is_zero()
    }
    fn valid_in(&self, budget: &ExactBudget) -> MathResult<()> {
        self.re.check_budget(budget)?;
        self.im.check_budget(budget)
    }
    /// Strict text form `<re>(+|-)<|im|>i`, components as accepted by
    /// `Rational::parse` (`7`, `-1/2`, `2/4` is normalized). No spaces, no
    /// leading `+`, the imaginary magnitude carries no sign of its own.
    pub fn parse(text: &str, budget: &mut ExactBudget) -> MathResult<Self> {
        require(
            text.len() <= 4 * budget.max_bits() + 8,
            "UnknownWithinBudget: exact complex input exceeded",
        )?;
        let malformed =
            || MathError("invalid exact complex text: expected <re>+<im>i or <re>-<im>i".into());
        let body = text.strip_suffix('i').ok_or_else(malformed)?;
        let split = body
            .char_indices()
            .rev()
            .find(|(i, c)| *i > 0 && (*c == '+' || *c == '-'))
            .map(|(i, _)| i)
            .ok_or_else(malformed)?;
        let (re_text, rest) = body.split_at(split);
        let (sign, magnitude) = rest.split_at(1);
        require(
            !magnitude.is_empty() && magnitude.bytes().next().is_some_and(|c| c.is_ascii_digit()),
            "invalid exact complex text: expected <re>+<im>i or <re>-<im>i",
        )?;
        let re = Rational::parse(re_text, budget)?;
        let im = Rational::parse(magnitude, budget)?;
        let im = if sign == "-" { im.negated() } else { im };
        Self::new(re, im, budget)
    }
    /// Equality of validated canonical values.
    pub fn checked_eq(&self, other: &Self, budget: &mut ExactBudget) -> MathResult<bool> {
        self.valid_in(budget)?;
        other.valid_in(budget)?;
        budget.charge(1)?;
        Ok(self == other)
    }
    pub fn checked_add(&self, w: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        let re = self.re.checked_add(&w.re, budget)?;
        let im = self.im.checked_add(&w.im, budget)?;
        Ok(Self { re, im })
    }
    pub fn checked_sub(&self, w: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        let re = self.re.checked_sub(&w.re, budget)?;
        let im = self.im.checked_sub(&w.im, budget)?;
        Ok(Self { re, im })
    }
    pub fn checked_neg(&self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.valid_in(budget)?;
        budget.charge(1)?;
        Ok(Self {
            re: self.re.negated(),
            im: self.im.negated(),
        })
    }
    pub fn checked_conj(&self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.valid_in(budget)?;
        budget.charge(1)?;
        Ok(Self {
            re: self.re.clone(),
            im: self.im.negated(),
        })
    }
    /// `(a+bi)(c+di)`: `ac`, `bd`, `ad`, `bc`, then `ac-bd`, `ad+bc`.
    pub fn checked_mul(&self, w: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        let ac = self.re.checked_mul(&w.re, budget)?;
        let bd = self.im.checked_mul(&w.im, budget)?;
        let ad = self.re.checked_mul(&w.im, budget)?;
        let bc = self.im.checked_mul(&w.re, budget)?;
        let re = ac.checked_sub(&bd, budget)?;
        let im = ad.checked_add(&bc, budget)?;
        Ok(Self { re, im })
    }
    /// Squared modulus `a*a + b*b` (`aa`, `bb`, sum), a nonnegative rational.
    pub fn checked_norm(&self, budget: &mut ExactBudget) -> MathResult<Rational> {
        let aa = self.re.checked_mul(&self.re, budget)?;
        let bb = self.im.checked_mul(&self.im, budget)?;
        aa.checked_add(&bb, budget)
    }
    /// `(a+bi)/(c+di)`: operands validated, zero divisor refused, then
    /// `n = cc+dd`, `s = ac+bd`, `t = bc-ad`, result `s/n + (t/n)i`.
    pub fn checked_div(&self, w: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.valid_in(budget)?;
        w.valid_in(budget)?;
        require(!w.is_zero(), "exact complex division by zero")?;
        let n = w.checked_norm(budget)?;
        let ac = self.re.checked_mul(&w.re, budget)?;
        let bd = self.im.checked_mul(&w.im, budget)?;
        let s = ac.checked_add(&bd, budget)?;
        let bc = self.im.checked_mul(&w.re, budget)?;
        let ad = self.re.checked_mul(&w.im, budget)?;
        let t = bc.checked_sub(&ad, budget)?;
        let re = s.checked_div(&n, budget)?;
        let im = t.checked_div(&n, budget)?;
        Ok(Self { re, im })
    }
    /// `1/(c+di) = c/n - (d/n)i` with `n = cc+dd`; zero refused.
    pub fn checked_inv(&self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.valid_in(budget)?;
        require(!self.is_zero(), "exact complex division by zero")?;
        let n = self.checked_norm(budget)?;
        let re = self.re.checked_div(&n, budget)?;
        let q = self.im.checked_div(&n, budget)?;
        Ok(Self {
            re,
            im: q.negated(),
        })
    }
}

impl std::fmt::Display for ComplexRational {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if self.im.negative() {
            write!(f, "{}-{}i", self.re, self.im.negated())
        } else {
            write!(f, "{}+{}i", self.re, self.im)
        }
    }
}
