//! v0.124: cryptographic-size number theory on `BigNat` (`dense_nat` limbs):
//! modular exponentiation, inverses, Baillie–PSW primality (trial division,
//! Miller–Rabin to the first 12 prime bases, strong Lucas with Selfridge
//! parameters), next prime, deterministic random numbers (SplitMix64) and hex
//! text. All results are exact; the native runtime (`dvb_bn_*`) implements the
//! same algorithms. No constant-time or side-channel guarantees.
use crate::dense_nat::{self as dn, Limbs};
use std::cmp::Ordering;

fn modulo(a: &[u32], m: &[u32]) -> Limbs {
    dn::divrem(a, m).unwrap().1
}

fn mulmod(a: &[u32], b: &[u32], m: &[u32]) -> Limbs {
    modulo(&dn::mul(a, b).unwrap(), m)
}

fn addmod(a: &[u32], b: &[u32], m: &[u32]) -> Limbs {
    let s = dn::add(a, b).unwrap();
    if dn::cmp(&s, m) != Ordering::Less {
        dn::monus(&s, m)
    } else {
        s
    }
}

fn submod(a: &[u32], b: &[u32], m: &[u32]) -> Limbs {
    if dn::cmp(a, b) != Ordering::Less {
        dn::monus(a, b)
    } else {
        dn::monus(m, &dn::monus(b, a))
    }
}

fn is_zero(a: &[u32]) -> bool {
    a.is_empty()
}

fn is_one(a: &[u32]) -> bool {
    a == [1]
}

fn odd(a: &[u32]) -> bool {
    a.first().map_or(false, |l| l & 1 == 1)
}

/// Products of residues must fit: `2·bits(m) <= MAX_BITS`.
fn modulus_ok(m: &[u32]) -> bool {
    !m.is_empty() && 2 * dn::bits(m) <= dn::MAX_BITS
}

pub fn powmod(a: &[u32], e: &[u32], m: &[u32]) -> Option<Limbs> {
    if !modulus_ok(m) {
        return None;
    }
    let mut r = modulo(&[1], m);
    let mut b = modulo(a, m);
    let nbits = dn::bits(e);
    for i in 0..nbits {
        if (e[i / 32] >> (i % 32)) & 1 == 1 {
            r = mulmod(&r, &b, m);
        }
        if i + 1 < nbits {
            b = mulmod(&b, &b, m);
        }
    }
    Some(r)
}

pub fn modinv(a: &[u32], m: &[u32]) -> Option<Limbs> {
    if !modulus_ok(m) {
        return None;
    }
    if is_one(m) {
        return Some(Vec::new());
    }
    let (mut r0, mut r1) = (modulo(a, m), m.to_vec());
    let (mut s0, mut s1): (Limbs, Limbs) = (vec![1], Vec::new());
    while !is_zero(&r1) {
        let (q, r) = dn::divrem(&r0, &r1).unwrap();
        r0 = std::mem::replace(&mut r1, r);
        let t = mulmod(&modulo(&q, m), &s1, m);
        let next = submod(&s0, &t, m);
        s0 = std::mem::replace(&mut s1, next);
    }
    is_one(&r0).then(|| modulo(&s0, m))
}

fn small_primes() -> Vec<u32> {
    (2u32..1000).filter(|p| (2..*p).take_while(|d| d * d <= *p).all(|d| p % d != 0)).collect()
}

fn small_mod(a: &[u32], p: u32) -> u32 {
    let mut r = 0u64;
    for &l in a.iter().rev() {
        r = ((r << 32) | u64::from(l)) % u64::from(p);
    }
    r as u32
}

fn miller_rabin(n: &[u32], base: u32) -> bool {
    let nm1 = dn::monus(n, &[1]);
    let mut s = 0u128;
    while (nm1[(s / 32) as usize] >> (s % 32)) & 1 == 0 {
        s += 1;
    }
    let d = dn::shr(&nm1, s);
    let mut x = powmod(&[base], &d, n).unwrap();
    if is_one(&x) || x == nm1 {
        return true;
    }
    for _ in 1..s {
        x = mulmod(&x, &x, n);
        if x == nm1 {
            return true;
        }
    }
    false
}

/// Jacobi symbol (a/n), n odd.
fn jacobi(a: &[u32], n: &[u32]) -> i32 {
    let (mut a, mut n) = (modulo(a, n), n.to_vec());
    let mut t = 1;
    while !is_zero(&a) {
        while !odd(&a) {
            a = dn::shr(&a, 1);
            let r = n[0] % 8;
            if r == 3 || r == 5 {
                t = -t;
            }
        }
        std::mem::swap(&mut a, &mut n);
        if a[0] % 4 == 3 && n[0] % 4 == 3 {
            t = -t;
        }
        a = modulo(&a, &n);
    }
    if is_one(&n) {
        t
    } else {
        0
    }
}

fn signed_mod(v: i64, n: &[u32]) -> Limbs {
    let m = modulo(&dn::from_u128(v.unsigned_abs() as u128), n);
    if v >= 0 || is_zero(&m) {
        m
    } else {
        dn::monus(n, &m)
    }
}

fn half(x: &[u32], n: &[u32]) -> Limbs {
    if odd(x) {
        dn::shr(&dn::add(x, n).unwrap(), 1)
    } else {
        dn::shr(x, 1)
    }
}

fn strong_lucas(n: &[u32]) -> bool {
    let r = dn::sqrt(n);
    if dn::mul(&r, &r).unwrap() == n {
        return false;
    }
    let mut d: i64 = 5;
    loop {
        let dm = signed_mod(d, n);
        match jacobi(&dm, n) {
            -1 => break,
            0 if dn::from_u128(d.unsigned_abs() as u128) != n => return false,
            _ => {}
        }
        d = if d > 0 { -(d + 2) } else { -d + 2 };
        if d.unsigned_abs() > 1 << 40 {
            return false;
        }
    }
    let dm = signed_mod(d, n);
    let qm = signed_mod((1 - d) / 4, n);
    let np1 = dn::add(n, &[1]).unwrap();
    let mut s = 0u128;
    while (np1[(s / 32) as usize] >> (s % 32)) & 1 == 0 {
        s += 1;
    }
    let dd = dn::shr(&np1, s);
    let (mut u, mut v, mut qk): (Limbs, Limbs, Limbs) = (vec![1], vec![1], qm.clone());
    let top = dn::bits(&dd) - 1;
    for bit in (0..top).rev() {
        u = mulmod(&u, &v, n);
        v = submod(&mulmod(&v, &v, n), &addmod(&qk, &qk, n), n);
        qk = mulmod(&qk, &qk, n);
        if (dd[bit / 32] >> (bit % 32)) & 1 == 1 {
            let nu = half(&addmod(&u, &v, n), n);
            let nv = half(&addmod(&mulmod(&dm, &u, n), &v, n), n);
            u = nu;
            v = nv;
            qk = mulmod(&qk, &qm, n);
        }
    }
    if is_zero(&u) || is_zero(&v) {
        return true;
    }
    for _ in 1..s {
        v = submod(&mulmod(&v, &v, n), &addmod(&qk, &qk, n), n);
        qk = mulmod(&qk, &qk, n);
        if is_zero(&v) {
            return true;
        }
    }
    false
}

/// Baillie–PSW: no composite passing it is known.
pub fn is_prime(n: &[u32]) -> bool {
    if dn::cmp(n, &[2]) == Ordering::Less {
        return false;
    }
    if !modulus_ok(n) {
        return false;
    }
    for p in small_primes() {
        if n == [p] {
            return true;
        }
        if small_mod(n, p) == 0 {
            return false;
        }
    }
    for b in [2u32, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37] {
        if !miller_rabin(n, b) {
            return false;
        }
    }
    strong_lucas(n)
}

/// Smallest prime `> n`; `None` beyond the size limit.
pub fn next_prime(n: &[u32]) -> Option<Limbs> {
    let mut x = dn::add(n, &[1])?;
    if dn::cmp(&x, &[2]) != Ordering::Greater {
        return Some(vec![2]);
    }
    if !odd(&x) {
        x = dn::add(&x, &[1])?;
    }
    loop {
        if !modulus_ok(&x) {
            return None;
        }
        if is_prime(&x) {
            return Some(x);
        }
        x = dn::add(&x, &[2])?;
    }
}

fn splitmix(state: &mut u64) -> u64 {
    *state = state.wrapping_add(0x9e3779b97f4a7c15);
    let mut z = *state;
    z = (z ^ (z >> 30)).wrapping_mul(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)).wrapping_mul(0x94d049bb133111eb);
    z ^ (z >> 31)
}

/// A `bits`-bit number (top bit set) from the SplitMix64 stream seeded with `seed`.
pub fn random(seed: u128, bits: u128) -> Option<Limbs> {
    if bits > dn::MAX_BITS as u128 {
        return None;
    }
    let bits = bits as usize;
    let mut st = (seed as u64) ^ ((seed >> 64) as u64);
    let n = bits.div_ceil(32);
    let mut out: Limbs = (0..n).map(|_| (splitmix(&mut st) >> 32) as u32).collect();
    if bits > 0 {
        let top = (bits - 1) % 32;
        let mask = if top == 31 { u32::MAX } else { (1u32 << (top + 1)) - 1 };
        out[n - 1] = (out[n - 1] & mask) | (1 << top);
    }
    Some(dn::trim(out))
}

pub fn to_hex(a: &[u32]) -> String {
    if a.is_empty() {
        return "0".into();
    }
    let mut s = format!("{:x}", a[a.len() - 1]);
    for l in a.iter().rev().skip(1) {
        s.push_str(&format!("{l:08x}"));
    }
    s
}

pub fn from_hex(t: &str) -> Option<Limbs> {
    if t.is_empty() || t.len() > dn::MAX_BITS / 4 || !t.bytes().all(|c| c.is_ascii_hexdigit()) {
        return None;
    }
    let b = t.as_bytes();
    let mut out = Vec::with_capacity(b.len() / 8 + 1);
    let mut end = b.len();
    while end > 0 {
        let start = end.saturating_sub(8);
        out.push(u32::from_str_radix(&t[start..end], 16).ok()?);
        end = start;
    }
    Some(dn::trim(out))
}
