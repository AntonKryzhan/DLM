//! Exact Q(i), represented by two canonical, budgeted rational components.
//! Each rational intermediate must fit the caller's precision profile.
use super::exact::{ExactBudget, Rational};
use super::{require, MathResult};

#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct ComplexQ {
    real: Rational,
    imag: Rational,
}

impl ComplexQ {
    pub fn zero() -> Self {
        Self {
            real: Rational::zero(),
            imag: Rational::zero(),
        }
    }

    pub fn one() -> Self {
        Self {
            real: Rational::one(),
            imag: Rational::zero(),
        }
    }

    pub fn i() -> Self {
        Self {
            real: Rational::zero(),
            imag: Rational::one(),
        }
    }

    pub fn from_parts(
        real: Rational,
        imag: Rational,
        budget: &mut ExactBudget,
    ) -> MathResult<Self> {
        let value = Self { real, imag };
        value.check(budget)?;
        Ok(value)
    }

    pub fn from_integers(real: i128, imag: i128, budget: &mut ExactBudget) -> MathResult<Self> {
        Self::from_parts(
            Rational::from_integer(real),
            Rational::from_integer(imag),
            budget,
        )
    }

    pub fn real(&self) -> &Rational {
        &self.real
    }
    pub fn imag(&self) -> &Rational {
        &self.imag
    }
    pub fn is_zero(&self) -> bool {
        self.real.is_zero() && self.imag.is_zero()
    }

    fn check(&self, budget: &mut ExactBudget) -> MathResult<()> {
        self.real.check_budget(budget)?;
        self.imag.check_budget(budget)?;
        // Account for copying/comparing the canonical limbs, including zero.
        let work = [&self.real, &self.imag]
            .into_iter()
            .map(|q| {
                q.numerator().bit_len().div_ceil(32) + q.denominator().bit_len().div_ceil(32) + 1
            })
            .sum();
        budget.charge(work)
    }

    fn prepare(&self, other: &Self, budget: &mut ExactBudget) -> MathResult<()> {
        self.check(budget)?;
        other.check(budget)
    }

    pub fn checked_equal(&self, other: &Self, budget: &mut ExactBudget) -> MathResult<bool> {
        self.prepare(other, budget)?;
        Ok(self == other)
    }

    pub fn checked_neg(&self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.check(budget)?;
        Ok(Self {
            real: self.real.negated(),
            imag: self.imag.negated(),
        })
    }

    pub fn conjugate(&self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.check(budget)?;
        Ok(Self {
            real: self.real.clone(),
            imag: self.imag.negated(),
        })
    }

    pub fn checked_add(&self, other: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.prepare(other, budget)?;
        Ok(Self {
            real: self.real.checked_add(&other.real, budget)?,
            imag: self.imag.checked_add(&other.imag, budget)?,
        })
    }

    pub fn checked_sub(&self, other: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.prepare(other, budget)?;
        Ok(Self {
            real: self.real.checked_sub(&other.real, budget)?,
            imag: self.imag.checked_sub(&other.imag, budget)?,
        })
    }

    fn product(&self, other: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        let ac = self.real.checked_mul(&other.real, budget)?;
        let bd = self.imag.checked_mul(&other.imag, budget)?;
        let real = ac.checked_sub(&bd, budget)?;
        let ad = self.real.checked_mul(&other.imag, budget)?;
        let bc = self.imag.checked_mul(&other.real, budget)?;
        let imag = ad.checked_add(&bc, budget)?;
        Ok(Self { real, imag })
    }

    pub fn checked_mul(&self, other: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.prepare(other, budget)?;
        self.product(other, budget)
    }

    fn norm(&self, budget: &mut ExactBudget) -> MathResult<Rational> {
        let aa = self.real.checked_mul(&self.real, budget)?;
        let bb = self.imag.checked_mul(&self.imag, budget)?;
        aa.checked_add(&bb, budget)
    }

    pub fn norm_squared(&self, budget: &mut ExactBudget) -> MathResult<Rational> {
        self.check(budget)?;
        self.norm(budget)
    }

    pub fn checked_div(&self, other: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.prepare(other, budget)?;
        require(!other.is_zero(), "exact complex division by zero")?;
        let denominator = other.norm(budget)?;
        let conjugate = Self {
            real: other.real.clone(),
            imag: other.imag.negated(),
        };
        let numerator = self.product(&conjugate, budget)?;
        Ok(Self {
            real: numerator.real.checked_div(&denominator, budget)?,
            imag: numerator.imag.checked_div(&denominator, budget)?,
        })
    }

    pub fn reciprocal(&self, budget: &mut ExactBudget) -> MathResult<Self> {
        Self::one().checked_div(self, budget)
    }
}

impl std::fmt::Display for ComplexQ {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{};{}", self.real, self.imag)
    }
}
