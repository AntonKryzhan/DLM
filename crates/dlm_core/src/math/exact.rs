//! Limb-based exact integers/rationals with explicit precision and work budgets.
//! This runtime API does not change the source Nat/Int machine representation.
use super::{require, MathError, MathResult};
use std::cmp::Ordering;
pub const MAX_BITS: usize = 16384;
#[derive(Clone, Debug)]
pub struct ExactBudget {
    max_bits: usize,
    initial_work: u64,
    remaining_work: u64,
}
impl ExactBudget {
    pub fn new(max_bits: usize, work: u64) -> MathResult<Self> {
        require(
            (1..=MAX_BITS).contains(&max_bits),
            "exact precision must be 1..16384 bits",
        )?;
        Ok(Self {
            max_bits,
            initial_work: work,
            remaining_work: work,
        })
    }
    pub fn max_bits(&self) -> usize {
        self.max_bits
    }
    pub fn remaining_work(&self) -> u64 {
        self.remaining_work
    }
    pub fn work_used(&self) -> u64 {
        self.initial_work - self.remaining_work
    }
    pub(crate) fn charge(&mut self, work: usize) -> MathResult<()> {
        let work = work as u64;
        if work > self.remaining_work {
            self.remaining_work = 0;
            return Err(MathError(
                "UnknownWithinBudget: exact arithmetic work exhausted".into(),
            ));
        }
        self.remaining_work -= work;
        Ok(())
    }
    fn value(&self, n: &Natural) -> MathResult<()> {
        require(
            n.bit_len() <= self.max_bits,
            "UnknownWithinBudget: exact precision exceeded",
        )
    }
    fn temporary(&self, n: &Natural) -> MathResult<()> {
        require(
            n.bit_len() <= self.max_bits * 2 + 2,
            "UnknownWithinBudget: exact intermediate precision exceeded",
        )
    }
}
impl Default for ExactBudget {
    fn default() -> Self {
        Self::new(4096, 50_000_000).unwrap()
    }
}
#[derive(Clone, Debug, Default, PartialEq, Eq, Hash)]
pub struct Natural {
    limbs: Vec<u32>,
}
impl Natural {
    fn normalized(mut limbs: Vec<u32>) -> Self {
        while limbs.last() == Some(&0) {
            limbs.pop();
        }
        Self { limbs }
    }
    pub fn zero() -> Self {
        Self::default()
    }
    pub fn from_u128(mut n: u128) -> Self {
        let mut v = Vec::new();
        while n != 0 {
            v.push(n as u32);
            n >>= 32;
        }
        Self { limbs: v }
    }
    pub fn is_zero(&self) -> bool {
        self.limbs.is_empty()
    }
    pub fn bit_len(&self) -> usize {
        self.limbs.last().map_or(0, |v| {
            (self.limbs.len() - 1) * 32 + (32 - v.leading_zeros()) as usize
        })
    }
    pub fn to_u128(&self) -> Option<u128> {
        if self.limbs.len() > 4 {
            return None;
        }
        Some(
            self.limbs
                .iter()
                .rev()
                .fold(0, |a, &b| (a << 32) | b as u128),
        )
    }
    pub fn parse(text: &str, budget: &mut ExactBudget) -> MathResult<Self> {
        // Cap input before traversing or allocating by its length.
        require(
            text.len() <= budget.max_bits + 1,
            "UnknownWithinBudget: exact decimal input exceeded",
        )?;
        require(
            !text.is_empty() && text.bytes().all(|c| c.is_ascii_digit()),
            "invalid exact natural decimal",
        )?;
        budget.charge(text.len())?;
        let mut out = Self::zero();
        for d in text.bytes() {
            budget.charge(out.limbs.len() + 1)?;
            let mut carry = (d - b'0') as u64;
            for word in &mut out.limbs {
                let n = *word as u64 * 10 + carry;
                *word = n as u32;
                carry = n >> 32;
            }
            if carry != 0 {
                out.limbs.push(carry as u32);
            }
            budget.value(&out)?;
        }
        Ok(out)
    }
    fn sum(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        let n = self.limbs.len().max(b.limbs.len());
        budget.charge(n + 1)?;
        let mut out = Vec::with_capacity(n + 1);
        let mut carry = 0u64;
        for i in 0..n {
            let v = self.limbs.get(i).copied().unwrap_or(0) as u64
                + b.limbs.get(i).copied().unwrap_or(0) as u64
                + carry;
            out.push(v as u32);
            carry = v >> 32;
        }
        if carry != 0 {
            out.push(carry as u32);
        }
        let out = Self::normalized(out);
        budget.temporary(&out)?;
        Ok(out)
    }
    fn difference(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        debug_assert!(self >= b);
        budget.charge(self.limbs.len() + 1)?;
        let mut out = Vec::with_capacity(self.limbs.len());
        let mut borrow = 0i64;
        for (i, &a) in self.limbs.iter().enumerate() {
            let v = a as i64 - b.limbs.get(i).copied().unwrap_or(0) as i64 - borrow;
            borrow = i64::from(v < 0);
            out.push((v + borrow * (1i64 << 32)) as u32);
        }
        debug_assert_eq!(borrow, 0);
        Ok(Self::normalized(out))
    }
    fn product(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        budget.charge(self.limbs.len() * b.limbs.len() + 1)?;
        if self.is_zero() || b.is_zero() {
            return Ok(Self::zero());
        }
        let mut out = vec![0u32; self.limbs.len() + b.limbs.len()];
        for (i, &a) in self.limbs.iter().enumerate() {
            let mut carry = 0u64;
            for (j, &v) in b.limbs.iter().enumerate() {
                let n = a as u64 * v as u64 + out[i + j] as u64 + carry;
                out[i + j] = n as u32;
                carry = n >> 32;
            }
            out[i + b.limbs.len()] = carry as u32;
        }
        let out = Self::normalized(out);
        budget.temporary(&out)?;
        Ok(out)
    }
    fn division(&self, d: &Self, budget: &mut ExactBudget) -> MathResult<(Self, Self)> {
        require(!d.is_zero(), "exact division by zero")?;
        budget.charge(self.limbs.len() + d.limbs.len() + 1)?;
        if self < d {
            return Ok((Self::zero(), self.clone()));
        }
        if d.limbs == [1] {
            return Ok((self.clone(), Self::zero()));
        }
        if self == d {
            return Ok((Self::from_u128(1), Self::zero()));
        }
        let mut q = vec![0u32; self.limbs.len()];
        let mut rem = Self::zero();
        for bit in (0..self.bit_len()).rev() {
            budget.charge(rem.limbs.len() + d.limbs.len() + 1)?;
            let mut carry = (self.limbs[bit / 32] >> (bit % 32)) & 1;
            for word in &mut rem.limbs {
                let next = *word >> 31;
                *word = (*word << 1) | carry;
                carry = next;
            }
            if carry != 0 {
                rem.limbs.push(carry);
            }
            if &rem >= d {
                rem = rem.difference(d, budget)?;
                q[bit / 32] |= 1u32 << (bit % 32);
            }
        }
        Ok((Self::normalized(q), rem))
    }
    fn quotient(&self, d: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        let (q, r) = self.division(d, budget)?;
        require(r.is_zero(), "internal nonexact division")?;
        Ok(q)
    }
    fn gcd(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        let mut a = self.clone();
        let mut b = b.clone();
        while !b.is_zero() {
            budget.charge(1)?;
            let r = a.division(&b, budget)?.1;
            a = b;
            b = r;
        }
        Ok(a)
    }
    pub fn checked_add(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        budget.value(self)?;
        budget.value(b)?;
        let out = self.sum(b, budget)?;
        budget.value(&out)?;
        Ok(out)
    }
    pub fn checked_sub(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        budget.value(self)?;
        budget.value(b)?;
        require(self >= b, "negative natural subtraction")?;
        self.difference(b, budget)
    }
    pub fn checked_mul(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        budget.value(self)?;
        budget.value(b)?;
        let out = self.product(b, budget)?;
        budget.value(&out)?;
        Ok(out)
    }
    pub fn div_rem(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<(Self, Self)> {
        budget.value(self)?;
        budget.value(b)?;
        self.division(b, budget)
    }
    pub fn checked_gcd(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        budget.value(self)?;
        budget.value(b)?;
        self.gcd(b, budget)
    }
    /// Nonnegative exponent; 0^0 is the empty product, 1.
    pub fn checked_pow(&self, mut exponent: u128, budget: &mut ExactBudget) -> MathResult<Self> {
        budget.value(self)?;
        budget.charge(1)?;
        let mut base = self.clone();
        let mut out = Self::from_u128(1);
        while exponent != 0 {
            budget.charge(1)?;
            if exponent & 1 != 0 {
                out = out.checked_mul(&base, budget)?;
            }
            exponent >>= 1;
            if exponent != 0 {
                base = base.checked_mul(&base, budget)?;
            }
        }
        Ok(out)
    }
    /// lcm(0, b) = 0; divide by gcd before multiplying.
    pub fn checked_lcm(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        budget.value(self)?;
        budget.value(b)?;
        budget.charge(1)?;
        if self.is_zero() || b.is_zero() {
            return Ok(Self::zero());
        }
        let g = self.gcd(b, budget)?;
        self.quotient(&g, budget)?.checked_mul(b, budget)
    }
    /// Return (s, r) with self = s*s + r and 0 <= r < 2*s + 1.
    pub fn checked_sqrt_rem(&self, budget: &mut ExactBudget) -> MathResult<(Self, Self)> {
        budget.value(self)?;
        budget.charge(1)?;
        let mut root = Self::zero();
        let mut remainder = Self::zero();
        let two = Self::from_u128(2);
        let four = Self::from_u128(4);
        let one = Self::from_u128(1);
        for pair in (0..self.bit_len().div_ceil(2)).rev() {
            let bit = pair * 2;
            let digit = (self.limbs[bit / 32] >> (bit % 32)) & 3;
            budget.charge(1)?;
            remainder = remainder
                .product(&four, budget)?
                .sum(&Self::from_u128(digit as u128), budget)?;
            let trial = root.product(&four, budget)?.sum(&one, budget)?;
            budget.charge(remainder.limbs.len() + trial.limbs.len() + 1)?;
            let take = remainder >= trial;
            root = root.product(&two, budget)?;
            if take {
                remainder = remainder.difference(&trial, budget)?;
                root = root.sum(&one, budget)?;
            }
        }
        budget.value(&root)?;
        budget.value(&remainder)?;
        Ok((root, remainder))
    }
}
impl Ord for Natural {
    fn cmp(&self, b: &Self) -> Ordering {
        self.limbs
            .len()
            .cmp(&b.limbs.len())
            .then_with(|| self.limbs.iter().rev().cmp(b.limbs.iter().rev()))
    }
}
impl PartialOrd for Natural {
    fn partial_cmp(&self, b: &Self) -> Option<Ordering> {
        Some(self.cmp(b))
    }
}
impl std::fmt::Display for Natural {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if self.is_zero() {
            return f.write_str("0");
        }
        let mut words = self.limbs.clone();
        let mut chunks = Vec::new();
        while !words.is_empty() {
            let mut rem = 0u64;
            for word in words.iter_mut().rev() {
                let v = (rem << 32) | *word as u64;
                *word = (v / 1_000_000_000) as u32;
                rem = v % 1_000_000_000;
            }
            chunks.push(rem as u32);
            while words.last() == Some(&0) {
                words.pop();
            }
        }
        write!(f, "{}", chunks.pop().unwrap())?;
        for chunk in chunks.iter().rev() {
            write!(f, "{chunk:09}")?;
        }
        Ok(())
    }
}
fn signed_sum(
    an: bool,
    a: &Natural,
    bn: bool,
    b: &Natural,
    budget: &mut ExactBudget,
) -> MathResult<(bool, Natural)> {
    if an == bn {
        Ok((an, a.sum(b, budget)?))
    } else if a >= b {
        Ok((an, a.difference(b, budget)?))
    } else {
        Ok((bn, b.difference(a, budget)?))
    }
}
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct Integer {
    negative: bool,
    magnitude: Natural,
}
impl Integer {
    fn canonical(negative: bool, magnitude: Natural) -> Self {
        Self {
            negative: negative && !magnitude.is_zero(),
            magnitude,
        }
    }
    pub fn from_i128(n: i128) -> Self {
        Self::canonical(n < 0, Natural::from_u128(n.unsigned_abs()))
    }
    /// v0.105: sign and magnitude (a zero magnitude is never negative).
    pub fn from_parts(negative: bool, magnitude: Natural) -> Self {
        Self::canonical(negative, magnitude)
    }
    pub fn parse(text: &str, budget: &mut ExactBudget) -> MathResult<Self> {
        let (negative, digits) = match text.strip_prefix('-') {
            Some(t) => (true, t),
            None => (false, text),
        };
        Ok(Self::canonical(negative, Natural::parse(digits, budget)?))
    }
    pub fn negative(&self) -> bool {
        self.negative
    }
    pub fn magnitude(&self) -> &Natural {
        &self.magnitude
    }
    pub fn negated(&self) -> Self {
        Self::canonical(!self.negative, self.magnitude.clone())
    }
    pub fn checked_cmp(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Ordering> {
        budget.value(&self.magnitude)?;
        budget.value(&b.magnitude)?;
        budget.charge(self.magnitude.limbs.len() + b.magnitude.limbs.len() + 1)?;
        if self.negative != b.negative {
            return Ok(if self.negative {
                Ordering::Less
            } else {
                Ordering::Greater
            });
        }
        let order = self.magnitude.cmp(&b.magnitude);
        Ok(if self.negative {
            order.reverse()
        } else {
            order
        })
    }
    pub fn checked_add(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        budget.value(&self.magnitude)?;
        budget.value(&b.magnitude)?;
        let (n, v) = signed_sum(
            self.negative,
            &self.magnitude,
            b.negative,
            &b.magnitude,
            budget,
        )?;
        budget.value(&v)?;
        Ok(Self::canonical(n, v))
    }
    pub fn checked_sub(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.checked_add(&b.negated(), budget)
    }
    pub fn checked_mul(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        Ok(Self::canonical(
            self.negative != b.negative,
            self.magnitude.checked_mul(&b.magnitude, budget)?,
        ))
    }
    /// Euclidean remainder: 0 <= r < abs(divisor), including negative divisors.
    pub fn div_rem(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<(Self, Natural)> {
        let (mut q, mut r) = self.magnitude.div_rem(&b.magnitude, budget)?;
        if self.negative && !r.is_zero() {
            q = q.sum(&Natural::from_u128(1), budget)?;
            r = b.magnitude.difference(&r, budget)?;
        }
        budget.value(&q)?;
        Ok((Self::canonical(self.negative != b.negative, q), r))
    }
    pub fn checked_pow(&self, exponent: u128, budget: &mut ExactBudget) -> MathResult<Self> {
        Ok(Self::canonical(
            self.negative && exponent & 1 != 0,
            self.magnitude.checked_pow(exponent, budget)?,
        ))
    }
    pub fn checked_lcm(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        Ok(Self::canonical(
            false,
            self.magnitude.checked_lcm(&b.magnitude, budget)?,
        ))
    }
    pub fn checked_sqrt_rem(&self, budget: &mut ExactBudget) -> MathResult<(Self, Natural)> {
        budget.value(&self.magnitude)?;
        require(!self.negative, "exact square root of negative integer")?;
        let (root, remainder) = self.magnitude.checked_sqrt_rem(budget)?;
        Ok((Self::canonical(false, root), remainder))
    }
}
impl std::fmt::Display for Integer {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if self.negative {
            f.write_str("-")?;
        }
        write!(f, "{}", self.magnitude)
    }
}
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct Rational {
    negative: bool,
    numerator: Natural,
    denominator: Natural,
}
impl Rational {
    pub fn zero() -> Self {
        Self {
            negative: false,
            numerator: Natural::zero(),
            denominator: Natural::from_u128(1),
        }
    }
    pub fn one() -> Self {
        Self::from_integer(1)
    }
    pub fn from_integer(n: i128) -> Self {
        Self {
            negative: n < 0,
            numerator: Natural::from_u128(n.unsigned_abs()),
            denominator: Natural::from_u128(1),
        }
    }
    fn valid_in(&self, budget: &ExactBudget) -> MathResult<()> {
        budget.value(&self.numerator)?;
        budget.value(&self.denominator)
    }
    pub fn negative(&self) -> bool {
        self.negative
    }
    pub fn numerator(&self) -> &Natural {
        &self.numerator
    }
    pub fn denominator(&self) -> &Natural {
        &self.denominator
    }
    pub fn is_zero(&self) -> bool {
        self.numerator.is_zero()
    }
    fn finish(
        negative: bool,
        n: Natural,
        d: Natural,
        budget: &mut ExactBudget,
    ) -> MathResult<Self> {
        require(!d.is_zero(), "exact rational denominator is zero")?;
        if n.is_zero() {
            return Ok(Self::zero());
        }
        let g = n.gcd(&d, budget)?;
        let numerator = n.quotient(&g, budget)?;
        let denominator = d.quotient(&g, budget)?;
        budget.value(&numerator)?;
        budget.value(&denominator)?;
        Ok(Self {
            negative,
            numerator,
            denominator,
        })
    }
    pub fn new(
        negative: bool,
        n: Natural,
        d: Natural,
        budget: &mut ExactBudget,
    ) -> MathResult<Self> {
        budget.value(&n)?;
        budget.value(&d)?;
        Self::finish(negative, n, d, budget)
    }
    pub fn parse(text: &str, budget: &mut ExactBudget) -> MathResult<Self> {
        require(
            text.len() <= 2 * budget.max_bits + 3,
            "UnknownWithinBudget: exact rational input exceeded",
        )?;
        let (negative, text) = match text.strip_prefix('-') {
            Some(t) => (true, t),
            None => (false, text),
        };
        let (n, d) = text.split_once('/').unwrap_or((text, "1"));
        Self::new(
            negative,
            Natural::parse(n, budget)?,
            Natural::parse(d, budget)?,
            budget,
        )
    }
    pub fn negated(&self) -> Self {
        Self {
            negative: !self.negative && !self.is_zero(),
            ..self.clone()
        }
    }
    pub fn reciprocal(&self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.valid_in(budget)?;
        budget.charge(self.numerator.limbs.len() + self.denominator.limbs.len() + 1)?;
        require(!self.is_zero(), "exact rational division by zero")?;
        Ok(Self {
            negative: self.negative,
            numerator: self.denominator.clone(),
            denominator: self.numerator.clone(),
        })
    }
    pub fn checked_add(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.valid_in(budget)?;
        b.valid_in(budget)?;
        let g = self.denominator.gcd(&b.denominator, budget)?;
        let a_factor = b.denominator.quotient(&g, budget)?;
        let b_factor = self.denominator.quotient(&g, budget)?;
        let left = self.numerator.product(&a_factor, budget)?;
        let right = b.numerator.product(&b_factor, budget)?;
        let (negative, num) = signed_sum(self.negative, &left, b.negative, &right, budget)?;
        if num.is_zero() {
            return Ok(Self::zero());
        }
        let den = b_factor.product(&b.denominator, budget)?;
        Self::finish(negative, num, den, budget)
    }
    pub fn checked_sub(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.checked_add(&b.negated(), budget)
    }
    pub fn checked_mul(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.valid_in(budget)?;
        b.valid_in(budget)?;
        let g = self.numerator.gcd(&b.denominator, budget)?;
        let h = b.numerator.gcd(&self.denominator, budget)?;
        let num = self
            .numerator
            .quotient(&g, budget)?
            .product(&b.numerator.quotient(&h, budget)?, budget)?;
        let den = self
            .denominator
            .quotient(&h, budget)?
            .product(&b.denominator.quotient(&g, budget)?, budget)?;
        Self::finish(self.negative != b.negative, num, den, budget)
    }
    pub fn checked_div(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.checked_mul(&b.reciprocal(budget)?, budget)
    }
    /// Integer exponent, including i128::MIN; 0^0 = 1, negative powers require a nonzero base.
    pub fn checked_pow(&self, exponent: i128, budget: &mut ExactBudget) -> MathResult<Self> {
        self.valid_in(budget)?;
        budget.charge(1)?;
        let mut power = exponent.unsigned_abs();
        let mut base = if exponent < 0 {
            self.reciprocal(budget)?
        } else {
            self.clone()
        };
        let mut out = Self::one();
        while power != 0 {
            budget.charge(1)?;
            if power & 1 != 0 {
                out = out.checked_mul(&base, budget)?;
            }
            power >>= 1;
            if power != 0 {
                base = base.checked_mul(&base, budget)?;
            }
        }
        Ok(out)
    }
    pub fn checked_cmp(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Ordering> {
        self.valid_in(budget)?;
        b.valid_in(budget)?;
        budget.charge(1)?;
        if self.negative != b.negative {
            return Ok(if self.negative {
                Ordering::Less
            } else {
                Ordering::Greater
            });
        }
        let left = self.numerator.product(&b.denominator, budget)?;
        let right = b.numerator.product(&self.denominator, budget)?;
        budget.charge(left.limbs.len() + right.limbs.len() + 1)?;
        let order = left.cmp(&right);
        Ok(if self.negative {
            order.reverse()
        } else {
            order
        })
    }
    pub(crate) fn check_budget(&self, budget: &ExactBudget) -> MathResult<()> {
        self.valid_in(budget)
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
