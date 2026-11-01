//! Canonical, exact signed rationals in a bounded u128 magnitude profile.
//! Overflow is checked after cancellation; these are not arbitrary-precision Q.
use super::{require, MathError, MathResult};
use std::cmp::Ordering;

fn gcd(mut a: u128, mut b: u128) -> u128 {
    while b != 0 {
        (a, b) = (b, a % b);
    }
    a
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord)]
struct Wide {
    hi: u128,
    lo: u128,
}
impl Wide {
    fn product(a: u128, b: u128) -> Self {
        let aa = [a as u64, (a >> 64) as u64];
        let bb = [b as u64, (b >> 64) as u64];
        let mut v = [0u64; 4];
        for i in 0..2 {
            let mut carry = 0u128;
            for j in 0..2 {
                let p = aa[i] as u128 * bb[j] as u128 + v[i + j] as u128 + carry;
                v[i + j] = p as u64;
                carry = p >> 64;
            }
            v[i + 2] = carry as u64;
        }
        Self {
            hi: v[2] as u128 | ((v[3] as u128) << 64),
            lo: v[0] as u128 | ((v[1] as u128) << 64),
        }
    }
    fn add(self, rhs: Self) -> Option<Self> {
        let (lo, carry) = self.lo.overflowing_add(rhs.lo);
        Some(Self {
            hi: self.hi.checked_add(rhs.hi)?.checked_add(carry as u128)?,
            lo,
        })
    }
    fn sub(self, rhs: Self) -> Self {
        debug_assert!(self >= rhs);
        let (lo, borrow) = self.lo.overflowing_sub(rhs.lo);
        Self {
            hi: self.hi - rhs.hi - borrow as u128,
            lo,
        }
    }
    /// Exactly 256 steps. The carry represents the 129th remainder bit.
    fn div_rem(self, d: u128) -> (Self, u128) {
        debug_assert!(d != 0);
        let mut q = Self::default();
        let mut rem = 0u128;
        for bit in (0..256).rev() {
            let input = if bit >= 128 {
                (self.hi >> (bit - 128)) & 1
            } else {
                (self.lo >> bit) & 1
            };
            let carry = rem >> 127 != 0;
            rem = (rem << 1) | input;
            if carry || rem >= d {
                // With the carry this is exact subtraction in 129 bits.
                rem = rem.wrapping_sub(d);
                if bit >= 128 {
                    q.hi |= 1 << (bit - 128);
                } else {
                    q.lo |= 1 << bit;
                }
            }
        }
        (q, rem)
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Rational {
    negative: bool,
    numerator: u128,
    denominator: u128,
}
impl Rational {
    pub fn new(negative: bool, numerator: u128, denominator: u128) -> MathResult<Self> {
        require(denominator != 0, "rational denominator is zero")?;
        if numerator == 0 {
            return Ok(Self::zero());
        }
        let g = gcd(numerator, denominator);
        Ok(Self {
            negative,
            numerator: numerator / g,
            denominator: denominator / g,
        })
    }
    pub const fn zero() -> Self {
        Self {
            negative: false,
            numerator: 0,
            denominator: 1,
        }
    }
    pub fn from_integer(value: i128) -> Self {
        Self {
            negative: value < 0,
            numerator: value.unsigned_abs(),
            denominator: 1,
        }
    }
    pub fn negative(self) -> bool {
        self.negative
    }
    pub fn numerator(self) -> u128 {
        self.numerator
    }
    pub fn denominator(self) -> u128 {
        self.denominator
    }
    pub fn negated(self) -> Self {
        Self {
            negative: self.numerator != 0 && !self.negative,
            ..self
        }
    }
    pub fn checked_add(self, rhs: Self) -> MathResult<Self> {
        let g = gcd(self.denominator, rhs.denominator);
        let left = Wide::product(self.numerator, rhs.denominator / g);
        let right = Wide::product(rhs.numerator, self.denominator / g);
        let (negative, sum) = if self.negative == rhs.negative {
            // If g>1, the sum fits 256 bits. If g=1 and it overflows,
            // coprimality implies the reduced denominator also exceeds u128.
            (
                self.negative,
                left.add(right)
                    .ok_or_else(|| MathError("rational profile overflow".into()))?,
            )
        } else if left >= right {
            (self.negative, left.sub(right))
        } else {
            (rhs.negative, right.sub(left))
        };
        if sum == Wide::default() {
            return Ok(Self::zero());
        }
        let h = gcd(sum.div_rem(g).1, g);
        let num = sum.div_rem(h).0;
        let den = (self.denominator / g)
            .checked_mul(rhs.denominator / h)
            .ok_or_else(|| MathError("rational profile overflow".into()))?;
        require(num.hi == 0, "rational profile overflow")?;
        Self::new(negative, num.lo, den)
    }
    pub fn checked_sub(self, rhs: Self) -> MathResult<Self> {
        self.checked_add(rhs.negated())
    }
    pub fn checked_mul(self, rhs: Self) -> MathResult<Self> {
        let g = gcd(self.numerator, rhs.denominator);
        let h = gcd(rhs.numerator, self.denominator);
        let num = (self.numerator / g)
            .checked_mul(rhs.numerator / h)
            .ok_or_else(|| MathError("rational profile overflow".into()))?;
        let den = (self.denominator / h)
            .checked_mul(rhs.denominator / g)
            .ok_or_else(|| MathError("rational profile overflow".into()))?;
        Self::new(self.negative != rhs.negative, num, den)
    }
    pub fn reciprocal(self) -> MathResult<Self> {
        require(self.numerator != 0, "rational division by zero")?;
        Self::new(self.negative, self.denominator, self.numerator)
    }
    pub fn checked_div(self, rhs: Self) -> MathResult<Self> {
        self.checked_mul(rhs.reciprocal()?)
    }
}
impl Ord for Rational {
    fn cmp(&self, rhs: &Self) -> Ordering {
        if self.negative != rhs.negative {
            return if self.negative {
                Ordering::Less
            } else {
                Ordering::Greater
            };
        }
        let c = Wide::product(self.numerator, rhs.denominator)
            .cmp(&Wide::product(rhs.numerator, self.denominator));
        if self.negative {
            c.reverse()
        } else {
            c
        }
    }
}
impl PartialOrd for Rational {
    fn partial_cmp(&self, rhs: &Self) -> Option<Ordering> {
        Some(self.cmp(rhs))
    }
}
impl std::fmt::Display for Rational {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if self.negative {
            f.write_str("-")?;
        }
        write!(f, "{}/{}", self.numerator, self.denominator)
    }
}
