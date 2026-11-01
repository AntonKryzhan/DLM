//! Complex numbers with computable real components (numeric criterion N09).
//!
//! `z = re + i·im` with `re`, `im` computable-real programs (`math::creal`).
//! Every operation builds the component programs of the result by a fixed
//! construction (COMPUTABLE_COMPLEX.md), so the error control of N08 applies
//! to each component: `approx(n)` returns two rational intervals of width
//! `<= 2^-n` that contain the real and imaginary parts. The modulus and its
//! square are computable reals. Equality is decided by the bounds only.
use super::creal::{CReal, CRealBuilder, CRealOp};
use super::exact::{ExactBudget, Rational};
use super::interval_q::{IntervalOrder, IntervalQ};
use super::{MathError, MathResult};
use CRealOp::{Add, Div, Mul, Neg, Sqrt, Square, Sub};

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CComplex {
    re: CReal,
    im: CReal,
}

impl CComplex {
    pub fn new(re: CReal, im: CReal) -> Self {
        Self { re, im }
    }
    pub fn from_real(re: CReal, budget: &mut ExactBudget) -> MathResult<Self> {
        Ok(Self {
            re,
            im: CReal::constant(Rational::zero(), budget)?,
        })
    }
    pub fn re(&self) -> &CReal {
        &self.re
    }
    pub fn im(&self) -> &CReal {
        &self.im
    }
    /// Text `<re program> | <im program>`.
    pub fn parse(text: &str, budget: &mut ExactBudget) -> MathResult<Self> {
        let parts: Vec<&str> = text.split(" | ").collect();
        let [re, im] = parts[..] else {
            return Err(MathError("invalid computable complex text".into()));
        };
        Ok(Self {
            re: CReal::parse(re, budget)?,
            im: CReal::parse(im, budget)?,
        })
    }

    fn build(
        parts: &[&CReal],
        ops: impl FnOnce(&mut CRealBuilder, &[usize]),
        budget: &mut ExactBudget,
    ) -> MathResult<CReal> {
        let mut b = CRealBuilder::new();
        let roots: Vec<usize> = parts.iter().map(|x| b.embed(x)).collect();
        ops(&mut b, &roots);
        b.finish(budget)
    }
    fn unary(x: &CReal, op: CRealOp, budget: &mut ExactBudget) -> MathResult<CReal> {
        Self::build(
            &[x],
            |b, r| {
                b.op(op, r[0], 0);
            },
            budget,
        )
    }
    fn binary(x: &CReal, y: &CReal, op: CRealOp, budget: &mut ExactBudget) -> MathResult<CReal> {
        Self::build(
            &[x, y],
            |b, r| {
                b.op(op, r[0], r[1]);
            },
            budget,
        )
    }

    pub fn checked_add(&self, w: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        Ok(Self {
            re: Self::binary(&self.re, &w.re, Add, budget)?,
            im: Self::binary(&self.im, &w.im, Add, budget)?,
        })
    }
    pub fn checked_sub(&self, w: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        Ok(Self {
            re: Self::binary(&self.re, &w.re, Sub, budget)?,
            im: Self::binary(&self.im, &w.im, Sub, budget)?,
        })
    }
    pub fn checked_neg(&self, budget: &mut ExactBudget) -> MathResult<Self> {
        Ok(Self {
            re: Self::unary(&self.re, Neg, budget)?,
            im: Self::unary(&self.im, Neg, budget)?,
        })
    }
    pub fn conj(&self, budget: &mut ExactBudget) -> MathResult<Self> {
        Ok(Self {
            re: self.re.clone(),
            im: Self::unary(&self.im, Neg, budget)?,
        })
    }
    /// `(ac − bd) + i(ad + bc)`.
    pub fn checked_mul(&self, w: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        let parts = [&self.re, &self.im, &w.re, &w.im];
        let re = Self::build(
            &parts,
            |b, r| {
                let ac = b.op(Mul, r[0], r[2]);
                let bd = b.op(Mul, r[1], r[3]);
                b.op(Sub, ac, bd);
            },
            budget,
        )?;
        let im = Self::build(
            &parts,
            |b, r| {
                let ad = b.op(Mul, r[0], r[3]);
                let bc = b.op(Mul, r[1], r[2]);
                b.op(Add, ad, bc);
            },
            budget,
        )?;
        Ok(Self { re, im })
    }
    /// `a/(a² + b²) − i·b/(a² + b²)`; zero is a domain error of the approximation.
    pub fn checked_inv(&self, budget: &mut ExactBudget) -> MathResult<Self> {
        let parts = [&self.re, &self.im];
        let re = Self::build(
            &parts,
            |b, r| {
                let q = Self::modulus_sq(b, r[0], r[1]);
                b.op(Div, r[0], q);
            },
            budget,
        )?;
        let im = Self::build(
            &parts,
            |b, r| {
                let q = Self::modulus_sq(b, r[0], r[1]);
                let t = b.op(Div, r[1], q);
                b.op(Neg, t, 0);
            },
            budget,
        )?;
        Ok(Self { re, im })
    }
    /// `((ac + bd) + i(bc − ad)) / (c² + d²)`.
    pub fn checked_div(&self, w: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        let parts = [&self.re, &self.im, &w.re, &w.im];
        let re = Self::build(
            &parts,
            |b, r| {
                let ac = b.op(Mul, r[0], r[2]);
                let bd = b.op(Mul, r[1], r[3]);
                let num = b.op(Add, ac, bd);
                let q = Self::modulus_sq(b, r[2], r[3]);
                b.op(Div, num, q);
            },
            budget,
        )?;
        let im = Self::build(
            &parts,
            |b, r| {
                let bc = b.op(Mul, r[1], r[2]);
                let ad = b.op(Mul, r[0], r[3]);
                let num = b.op(Sub, bc, ad);
                let q = Self::modulus_sq(b, r[2], r[3]);
                b.op(Div, num, q);
            },
            budget,
        )?;
        Ok(Self { re, im })
    }
    fn modulus_sq(b: &mut CRealBuilder, x: usize, y: usize) -> usize {
        let xx = b.op(Square, x, 0);
        let yy = b.op(Square, y, 0);
        b.op(Add, xx, yy)
    }
    /// `|z|² = re² + im²`.
    pub fn abs_sq(&self, budget: &mut ExactBudget) -> MathResult<CReal> {
        Self::build(
            &[&self.re, &self.im],
            |b, r| {
                Self::modulus_sq(b, r[0], r[1]);
            },
            budget,
        )
    }
    /// `|z| = sqrt(re² + im²)`.
    pub fn abs(&self, budget: &mut ExactBudget) -> MathResult<CReal> {
        Self::build(
            &[&self.re, &self.im],
            |b, r| {
                let q = Self::modulus_sq(b, r[0], r[1]);
                b.op(Sqrt, q, 0);
            },
            budget,
        )
    }
    /// Intervals of width `<= 2^-n` containing the real and imaginary parts.
    pub fn approx(&self, n: u32, budget: &mut ExactBudget) -> MathResult<(IntervalQ, IntervalQ)> {
        let re = self.re.approx(n, budget)?;
        let im = self.im.approx(n, budget)?;
        Ok((re, im))
    }
    /// `Some(false)` when a component is certainly different, `Some(true)` when
    /// both components are the same exact points, `None` when undecided.
    pub fn equals(&self, w: &Self, n: u32, budget: &mut ExactBudget) -> MathResult<Option<bool>> {
        let (a, b) = self.approx(n, budget)?;
        let (c, d) = w.approx(n, budget)?;
        let re = a.compare(&c, budget)?;
        let im = b.compare(&d, budget)?;
        let apart = |o: IntervalOrder| matches!(o, IntervalOrder::Less | IntervalOrder::Greater);
        Ok(if apart(re) || apart(im) {
            Some(false)
        } else if re == IntervalOrder::Equal && im == IntervalOrder::Equal {
            Some(true)
        } else {
            None
        })
    }
}

impl std::fmt::Display for CComplex {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{} | {}", self.re, self.im)
    }
}
