//! v0.107: dense natural numbers (`BigNat`, numeric criterion N14).
//!
//! Canonical little-endian 32-bit limbs without trailing zero limbs, at most
//! [`MAX_BITS`] bits. The interpreter uses these functions; the native C
//! runtime (`backend_c_runtime.c`, `dvn_*`) implements the same algorithms
//! (schoolbook multiplication, Knuth algorithm D, Euclid, Newton square root)
//! so both executors agree on every result and every `none`.
use std::cmp::Ordering;

pub const MAX_BITS: usize = 1 << 20;
pub const MAX_LIMBS: usize = MAX_BITS / 32;
/// Longest decimal text accepted by `bn_from_text`.
pub const MAX_DECIMAL: usize = 315_653;

pub type Limbs = Vec<u32>;

pub fn trim(mut x: Limbs) -> Limbs {
    while x.last() == Some(&0) {
        x.pop();
    }
    x
}
fn fits(x: Limbs) -> Option<Limbs> {
    let x = trim(x);
    (x.len() <= MAX_LIMBS).then_some(x)
}
pub fn from_u128(mut n: u128) -> Limbs {
    let mut out = Vec::new();
    while n > 0 {
        out.push(n as u32);
        n >>= 32;
    }
    out
}
pub fn to_u128(x: &[u32]) -> Option<u128> {
    if x.len() > 4 {
        return None;
    }
    Some(
        x.iter()
            .rev()
            .fold(0u128, |acc, &l| (acc << 32) | u128::from(l)),
    )
}
/// Base-65536 words (std.exact representation) to limbs.
pub fn from_words(words: &[u128]) -> Option<Limbs> {
    if words.len() > 2 * MAX_LIMBS || words.iter().any(|&w| w > 0xffff) {
        return None;
    }
    let mut out = vec![0u32; words.len().div_ceil(2)];
    for (i, &w) in words.iter().enumerate() {
        out[i / 2] |= (w as u32) << (16 * (i % 2));
    }
    fits(out)
}
pub fn to_words(x: &[u32]) -> Vec<u128> {
    let mut out = Vec::with_capacity(x.len() * 2);
    for &l in x {
        out.push(u128::from(l & 0xffff));
        out.push(u128::from(l >> 16));
    }
    while out.last() == Some(&0) {
        out.pop();
    }
    out
}
pub fn bits(x: &[u32]) -> usize {
    match x.last() {
        None => 0,
        Some(&top) => 32 * (x.len() - 1) + (32 - top.leading_zeros() as usize),
    }
}
pub fn cmp(a: &[u32], b: &[u32]) -> Ordering {
    a.len()
        .cmp(&b.len())
        .then_with(|| a.iter().rev().cmp(b.iter().rev()))
}
pub fn add(a: &[u32], b: &[u32]) -> Option<Limbs> {
    let (a, b) = if a.len() >= b.len() { (a, b) } else { (b, a) };
    let mut out = Vec::with_capacity(a.len() + 1);
    let mut carry = 0u64;
    for (i, &x) in a.iter().enumerate() {
        let s = u64::from(x) + u64::from(*b.get(i).unwrap_or(&0)) + carry;
        out.push(s as u32);
        carry = s >> 32;
    }
    out.push(carry as u32);
    fits(out)
}
/// `max(a - b, 0)`.
pub fn monus(a: &[u32], b: &[u32]) -> Limbs {
    if cmp(a, b) != Ordering::Greater {
        return Vec::new();
    }
    let mut out = Vec::with_capacity(a.len());
    let mut borrow = 0i64;
    for (i, &x) in a.iter().enumerate() {
        let d = i64::from(x) - i64::from(*b.get(i).unwrap_or(&0)) - borrow;
        out.push(d.rem_euclid(1 << 32) as u32);
        borrow = i64::from(d < 0);
    }
    trim(out)
}
pub fn mul(a: &[u32], b: &[u32]) -> Option<Limbs> {
    if a.is_empty() || b.is_empty() {
        return Some(Vec::new());
    }
    if bits(a) + bits(b) > MAX_BITS + 1 {
        return None;
    }
    let mut out = vec![0u32; a.len() + b.len()];
    for (i, &x) in a.iter().enumerate() {
        let mut carry = 0u64;
        for (j, &y) in b.iter().enumerate() {
            let t = u64::from(x) * u64::from(y) + u64::from(out[i + j]) + carry;
            out[i + j] = t as u32;
            carry = t >> 32;
        }
        out[i + b.len()] = carry as u32;
    }
    fits(out)
}
pub fn shl(a: &[u32], k: u128) -> Option<Limbs> {
    if a.is_empty() {
        return Some(Vec::new());
    }
    if k > MAX_BITS as u128 || bits(a) + k as usize > MAX_BITS {
        return None;
    }
    let (w, s) = ((k / 32) as usize, (k % 32) as u32);
    let mut out = vec![0u32; w];
    let mut carry = 0u32;
    for &l in a {
        out.push(if s == 0 { l } else { (l << s) | carry });
        carry = if s == 0 { 0 } else { l >> (32 - s) };
    }
    out.push(carry);
    fits(out)
}
pub fn shr(a: &[u32], k: u128) -> Limbs {
    let w = (k / 32) as usize;
    if k >= (a.len() as u128) * 32 {
        return Vec::new();
    }
    let s = (k % 32) as u32;
    let mut out = Vec::with_capacity(a.len() - w);
    for i in w..a.len() {
        let lo = a[i] >> s;
        let hi = if s == 0 {
            0
        } else {
            a.get(i + 1).map_or(0, |&n| n << (32 - s))
        };
        out.push(lo | hi);
    }
    trim(out)
}
fn divrem_small(a: &[u32], d: u32) -> (Limbs, u32) {
    let mut q = vec![0u32; a.len()];
    let mut r = 0u64;
    for i in (0..a.len()).rev() {
        let cur = (r << 32) | u64::from(a[i]);
        q[i] = (cur / u64::from(d)) as u32;
        r = cur % u64::from(d);
    }
    (trim(q), r as u32)
}
/// Quotient and remainder (Knuth, TAOCP 4.3.1, algorithm D); `None` for a zero divisor.
pub fn divrem(a: &[u32], b: &[u32]) -> Option<(Limbs, Limbs)> {
    if b.is_empty() {
        return None;
    }
    if cmp(a, b) == Ordering::Less {
        return Some((Vec::new(), a.to_vec()));
    }
    if b.len() == 1 {
        let (q, r) = divrem_small(a, b[0]);
        return Some((q, trim(vec![r])));
    }
    let s = b[b.len() - 1].leading_zeros();
    let v = shl_raw(b, s);
    let mut u = shl_raw(a, s);
    u.push(0);
    if u.len() < a.len() + 1 {
        u.resize(a.len() + 1, 0);
    }
    let n = v.len();
    let m = u.len() - n - 1;
    let mut q = vec![0u32; m + 1];
    let base = 1u64 << 32;
    for j in (0..=m).rev() {
        let num = (u64::from(u[j + n]) << 32) | u64::from(u[j + n - 1]);
        let mut qhat = num / u64::from(v[n - 1]);
        let mut rhat = num % u64::from(v[n - 1]);
        while qhat >= base
            || u128::from(qhat) * u128::from(v[n - 2])
                > ((u128::from(rhat) << 32) | u128::from(u[j + n - 2]))
        {
            qhat -= 1;
            rhat += u64::from(v[n - 1]);
            if rhat >= base {
                break;
            }
        }
        let mut borrow = 0i128;
        let mut carry = 0u128;
        for i in 0..n {
            let p = u128::from(qhat) * u128::from(v[i]) + carry;
            carry = p >> 32;
            let t = i128::from(u[i + j]) - borrow - (p & 0xffff_ffff) as i128;
            u[i + j] = t.rem_euclid(1 << 32) as u32;
            borrow = i128::from(t < 0);
        }
        let t = i128::from(u[j + n]) - borrow - carry as i128;
        u[j + n] = t.rem_euclid(1 << 32) as u32;
        if t < 0 {
            qhat -= 1;
            let mut c = 0u64;
            for i in 0..n {
                let s2 = u64::from(u[i + j]) + u64::from(v[i]) + c;
                u[i + j] = s2 as u32;
                c = s2 >> 32;
            }
            u[j + n] = u[j + n].wrapping_add(c as u32);
        }
        q[j] = qhat as u32;
    }
    u.truncate(n);
    let r = shr(&trim(u), u128::from(s));
    Some((trim(q), r))
}
fn shl_raw(a: &[u32], s: u32) -> Limbs {
    if s == 0 {
        return a.to_vec();
    }
    let mut out = Vec::with_capacity(a.len() + 1);
    let mut carry = 0u32;
    for &l in a {
        out.push((l << s) | carry);
        carry = l >> (32 - s);
    }
    if carry != 0 {
        out.push(carry);
    }
    out
}
pub fn gcd(a: &[u32], b: &[u32]) -> Limbs {
    let (mut a, mut b) = (a.to_vec(), b.to_vec());
    while !b.is_empty() {
        let r = divrem(&a, &b).unwrap().1;
        a = b;
        b = r;
    }
    a
}
/// `floor(sqrt(a))` by Newton's iteration from `2^ceil(bits/2)`.
pub fn sqrt(a: &[u32]) -> Limbs {
    if a.is_empty() {
        return Vec::new();
    }
    let mut x = shl(&[1], bits(a).div_ceil(2) as u128).unwrap();
    loop {
        let (q, _) = divrem(a, &x).unwrap();
        let y = shr(&add(&x, &q).unwrap(), 1);
        if cmp(&y, &x) != Ordering::Less {
            return x;
        }
        x = y;
    }
}
pub fn to_decimal(x: &[u32]) -> String {
    if x.is_empty() {
        return "0".into();
    }
    let mut chunks = Vec::new();
    let mut cur = x.to_vec();
    while !cur.is_empty() {
        let (q, r) = divrem_small(&cur, 1_000_000_000);
        chunks.push(r);
        cur = q;
    }
    let mut s = chunks.pop().unwrap().to_string();
    for c in chunks.iter().rev() {
        s.push_str(&format!("{c:09}"));
    }
    s
}
pub fn from_decimal(t: &str) -> Option<Limbs> {
    if t.is_empty() || t.len() > MAX_DECIMAL || !t.bytes().all(|c| c.is_ascii_digit()) {
        return None;
    }
    let mut x: Limbs = Vec::new();
    for chunk in t.as_bytes().chunks(9) {
        let v: u32 = std::str::from_utf8(chunk).unwrap().parse().unwrap();
        let scale = 10u32.pow(chunk.len() as u32);
        let mut carry = u64::from(v);
        for l in x.iter_mut() {
            let p = u64::from(*l) * u64::from(scale) + carry;
            *l = p as u32;
            carry = p >> 32;
        }
        if carry != 0 {
            x.push(carry as u32);
        }
    }
    fits(x)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn small_values_match_u128() {
        let vals: [u128; 8] = [
            0,
            1,
            7,
            1 << 32,
            (1 << 64) + 5,
            u64::MAX as u128 * 3,
            123_456_789_012_345_678_901_234,
            u128::MAX >> 3,
        ];
        for &a in &vals {
            for &b in &vals {
                let (x, y) = (from_u128(a), from_u128(b));
                assert_eq!(to_u128(&add(&x, &y).unwrap()), a.checked_add(b));
                assert_eq!(to_u128(&monus(&x, &y)), Some(a.saturating_sub(b)));
                if let Some(p) = a.checked_mul(b) {
                    assert_eq!(to_u128(&mul(&x, &y).unwrap()), Some(p));
                }
                if b != 0 {
                    let (q, r) = divrem(&x, &y).unwrap();
                    assert_eq!((to_u128(&q), to_u128(&r)), (Some(a / b), Some(a % b)));
                }
                assert_eq!(cmp(&x, &y), a.cmp(&b));
            }
            assert_eq!(to_decimal(&from_u128(a)), a.to_string());
            assert_eq!(from_decimal(&a.to_string()).unwrap(), from_u128(a));
            assert_eq!(from_words(&to_words(&from_u128(a))).unwrap(), from_u128(a));
            let s = to_u128(&sqrt(&from_u128(a))).unwrap();
            assert!(s * s <= a && (s + 1).checked_mul(s + 1).is_none_or(|t| t > a));
        }
    }
    #[test]
    fn large_division_identity() {
        let a = from_decimal(&"98765432109876543210".repeat(20)).unwrap();
        let b = from_decimal(&"1234567890123".repeat(7)).unwrap();
        let (q, r) = divrem(&a, &b).unwrap();
        assert_eq!(add(&mul(&q, &b).unwrap(), &r).unwrap(), a);
        assert_eq!(cmp(&r, &b), Ordering::Less);
        assert_eq!(shr(&shl(&a, 77).unwrap(), 77), a);
        assert!(shl(&[1], MAX_BITS as u128).is_none());
        assert_eq!(bits(&shl(&[1], (MAX_BITS - 1) as u128).unwrap()), MAX_BITS);
        let g = gcd(&mul(&a, &b).unwrap(), &mul(&b, &b).unwrap());
        assert_eq!(divrem(&g, &b).unwrap().1, Vec::<u32>::new());
    }
}
