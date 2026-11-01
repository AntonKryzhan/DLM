//! v0.118: number theory on `Nat` (u128), polynomials over prime fields and
//! permutation groups. Modular products use 128-bit arithmetic below `2^64`
//! and double-and-add above; primality is Miller–Rabin with the first 20
//! prime bases (deterministic below 3.3·10^24); factorization is trial
//! division followed by Brent's Pollard rho with fixed seeds and a fixed work
//! budget (prime factors up to about 2^50 are found; otherwise `None`). Above
//! the deterministic Miller–Rabin range a strong Lucas test is added (BPSW).
//! Products modulo `m >= 2^64` use a 256-bit product and Knuth division. Polynomials
//! over `F_p` are ascending coefficient `NatVec`s without trailing zeros
//! (`p` prime, `p < 2^64`). Permutations are image vectors `p[i]`, composed
//! as `(p∘q)[i] = p[q[i]]`; group orders come from a deterministic
//! Schreier–Sims. The native runtime (`dvt_*`) implements the same results.
pub const MAX: usize = 1 << 24;
pub const PERM_MAX: usize = 256;
const BASES: [u128; 20] = [
    2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71,
];

pub fn gcd(mut a: u128, mut b: u128) -> u128 {
    while b != 0 {
        (a, b) = (b, a % b);
    }
    a
}

pub fn lcm(a: u128, b: u128) -> Option<u128> {
    if a == 0 || b == 0 {
        return Some(0);
    }
    (a / gcd(a, b)).checked_mul(b)
}

fn addmod(x: u128, y: u128, m: u128) -> u128 {
    if x >= m - y {
        x - (m - y)
    } else {
        x + y
    }
}

const M64: u128 = u64::MAX as u128;

/// 256-bit product `(hi, lo)`.
pub fn mul_wide(a: u128, b: u128) -> (u128, u128) {
    let (a1, a0, b1, b0) = (a >> 64, a & M64, b >> 64, b & M64);
    let (p00, p01, p10, p11) = (a0 * b0, a0 * b1, a1 * b0, a1 * b1);
    let mid = (p00 >> 64) + (p01 & M64) + (p10 & M64);
    ((p11 + (p01 >> 64) + (p10 >> 64) + (mid >> 64)), (p00 & M64) | (mid << 64))
}

/// `(x·2^64 + c) mod m` for `x < m`, `m >= 2^64` (one Knuth step with 64-bit digits).
fn shift_in(x: u128, c: u64, m: u128) -> u128 {
    let s = m.leading_zeros();
    let mn = m << s;
    let (v1, v0) = (mn >> 64, mn & M64);
    let nh = if s == 0 { x } else { (x << s) | ((c as u128) >> (64 - s)) };
    let nl = ((c as u128) << s) & M64;
    let (mut qhat, mut rhat) = (nh / v1, nh % v1);
    while qhat > M64 || qhat * v0 > ((rhat << 64) | nl) {
        qhat -= 1;
        rhat += v1;
        if rhat > M64 {
            break;
        }
    }
    // product qhat · mn as (ph, pl)
    let lo = qhat * v0;
    let (mut ph, mut pl) = (qhat * v1 + (lo >> 64), lo & M64);
    while ph > nh || (ph == nh && pl > nl) {
        let borrow = u128::from(pl < v0);
        pl = (pl + (1 << 64) - v0) & M64;
        ph = ph - v1 - borrow;
    }
    let borrow = u128::from(nl < pl);
    let rl = (nl + (1 << 64) - pl) & M64;
    let rh = nh - ph - borrow;
    ((rh << 64) | rl) >> s
}

pub fn mulmod(a: u128, b: u128, m: u128) -> u128 {
    let (a, b) = (a % m, b % m);
    if m <= M64 {
        return a * b % m;
    }
    let (hi, lo) = mul_wide(a, b);
    shift_in(shift_in(hi, (lo >> 64) as u64, m), lo as u64, m)
}

pub fn powmod(a: u128, mut e: u128, m: u128) -> Option<u128> {
    if m == 0 {
        return None;
    }
    let mut r = 1 % m;
    let mut b = a % m;
    while e > 0 {
        if e & 1 == 1 {
            r = mulmod(r, b, m);
        }
        b = mulmod(b, b, m);
        e >>= 1;
    }
    Some(r)
}

pub fn mod_inv(a: u128, m: u128) -> Option<u128> {
    if m == 0 {
        return None;
    }
    if m == 1 {
        return Some(0);
    }
    let (mut r0, mut r1) = (a % m, m);
    let (mut s0, mut s1) = (1u128, 0u128);
    while r1 != 0 {
        let q = r0 / r1;
        (r0, r1) = (r1, r0 - q * r1);
        let t = mulmod(q, s1, m);
        (s0, s1) = (s1, if s0 >= t { s0 - t } else { m - (t - s0) });
    }
    (r0 == 1).then_some(s0 % m)
}

pub fn is_prime(n: u128) -> bool {
    if n < 2 {
        return false;
    }
    for p in BASES {
        if n == p {
            return true;
        }
        if n % p == 0 {
            return false;
        }
    }
    let s = (n - 1).trailing_zeros();
    let d = (n - 1) >> s;
    'bases: for a in BASES {
        let mut x = powmod(a, d, n).unwrap();
        if x == 1 || x == n - 1 {
            continue;
        }
        for _ in 1..s {
            x = mulmod(x, x, n);
            if x == n - 1 {
                continue 'bases;
            }
        }
        return false;
    }
    // beyond the deterministic Miller–Rabin range: strong Lucas test (BPSW)
    n < 3_317_044_064_679_887_385_961_981 || strong_lucas(n)
}

fn jacobi(a: u128, n: u128) -> i32 {
    let (mut a, mut n) = (a % n, n);
    let mut t = 1;
    while a != 0 {
        while a % 2 == 0 {
            a /= 2;
            if n % 8 == 3 || n % 8 == 5 {
                t = -t;
            }
        }
        std::mem::swap(&mut a, &mut n);
        if a % 4 == 3 && n % 4 == 3 {
            t = -t;
        }
        a %= n;
    }
    if n == 1 {
        t
    } else {
        0
    }
}

fn half(x: u128, n: u128) -> u128 {
    if x % 2 == 0 {
        x / 2
    } else {
        x / 2 + n / 2 + 1
    }
}

fn submod(a: u128, b: u128, n: u128) -> u128 {
    if a >= b {
        a - b
    } else {
        n - (b - a)
    }
}

/// Strong Lucas probable-prime test with Selfridge parameters (odd `n`, not a square).
fn strong_lucas(n: u128) -> bool {
    let r = isqrt(n);
    if r * r == n {
        return false;
    }
    let mut d: i128 = 5;
    loop {
        let dm = if d > 0 { d as u128 % n } else { n - ((-d) as u128 % n) };
        match jacobi(dm, n) {
            -1 => break,
            0 if d.unsigned_abs() != n => return false,
            _ => {}
        }
        d = if d > 0 { -(d + 2) } else { -d + 2 };
    }
    let dm = if d > 0 { d as u128 % n } else { n - ((-d) as u128 % n) };
    // Q = (1 - D) / 4
    let q = (1 - d) / 4;
    let qm = if q >= 0 { q as u128 % n } else { n - ((-q) as u128 % n) } % n;
    // n + 1 = dd · 2^s
    let h = n / 2 + 1;
    let s0 = h.trailing_zeros();
    let dd = h >> s0;
    let s = s0 + 1;
    let (mut u, mut v, mut qk) = (1u128, 1u128, qm);
    let top = 127 - dd.leading_zeros();
    for bit in (0..top).rev() {
        u = mulmod(u, v, n);
        v = submod(mulmod(v, v, n), addmod(qk, qk, n), n);
        qk = mulmod(qk, qk, n);
        if (dd >> bit) & 1 == 1 {
            let nu = half(addmod(u, v, n), n);
            let nv = half(addmod(mulmod(dm, u, n), v, n), n);
            u = nu;
            v = nv;
            qk = mulmod(qk, qm, n);
        }
    }
    if u == 0 || v == 0 {
        return true;
    }
    for _ in 1..s {
        v = submod(mulmod(v, v, n), addmod(qk, qk, n), n);
        qk = mulmod(qk, qk, n);
        if v == 0 {
            return true;
        }
    }
    false
}

fn rho(n: u128) -> Option<u128> {
    for c in 1..=32u128 {
        let f = |x: u128| addmod(mulmod(x, x, n), c % n, n);
        let (mut y, mut r, mut q, mut g) = (2 % n, 1u64, 1u128, 1u128);
        let (mut x, mut ys) = (y, y);
        while g == 1 {
            x = y;
            for _ in 0..r {
                y = f(y);
            }
            let mut k = 0u64;
            while k < r && g == 1 {
                ys = y;
                for _ in 0..128.min(r - k) {
                    y = f(y);
                    q = mulmod(q, x.abs_diff(y), n);
                }
                g = gcd(q, n);
                k += 128;
            }
            r *= 2;
            if r > 1 << 26 {
                break;
            }
        }
        if g == n || g == 0 {
            g = 1;
            for _ in 0..(1u64 << 20) {
                ys = f(ys);
                g = gcd(x.abs_diff(ys), n);
                if g > 1 {
                    break;
                }
            }
        }
        if g > 1 && g < n {
            return Some(g);
        }
    }
    None
}

/// Prime factors with multiplicity, increasing; `None` for 0.
pub fn factor(n: u128) -> Option<Vec<u128>> {
    if n == 0 {
        return None;
    }
    let mut out = Vec::new();
    let mut m = n;
    let mut p = 2u128;
    while p < 1000 && p * p <= m {
        while m % p == 0 {
            out.push(p);
            m /= p;
        }
        p += if p == 2 { 1 } else { 2 };
    }
    let mut stack = vec![m];
    while let Some(x) = stack.pop() {
        if x == 1 {
            continue;
        }
        if is_prime(x) {
            out.push(x);
            continue;
        }
        let d = rho(x)?;
        stack.push(d);
        stack.push(x / d);
    }
    out.sort_unstable();
    Some(out)
}

pub fn totient(n: u128) -> u128 {
    let Some(f) = factor(n) else { return 0 };
    let mut r = n;
    let mut last = 0;
    for p in f {
        if p != last {
            r = r / p * (p - 1);
            last = p;
        }
    }
    r
}

pub fn divisors(n: u128) -> Option<Vec<u128>> {
    let f = factor(n)?;
    let mut ds = vec![1u128];
    let mut i = 0;
    while i < f.len() {
        let p = f[i];
        let mut k = 0;
        while i < f.len() && f[i] == p {
            k += 1;
            i += 1;
        }
        let base = ds.clone();
        let mut pk = 1u128;
        for _ in 0..k {
            pk *= p;
            for d in &base {
                ds.push(d * pk);
            }
            if ds.len() > MAX {
                return None;
            }
        }
    }
    ds.sort_unstable();
    Some(ds)
}

/// Primes `<= limit` (`limit <= 2^24`).
pub fn primes(limit: u128) -> Option<Vec<u128>> {
    if limit > MAX as u128 {
        return None;
    }
    let n = limit as usize;
    let mut sieve = vec![true; n + 1];
    let mut out = Vec::new();
    for i in 2..=n {
        if sieve[i] {
            out.push(i as u128);
            let mut j = i * i;
            while j <= n {
                sieve[j] = false;
                j += i;
            }
        }
    }
    Some(out)
}

pub fn crt(rs: &[u128], ms: &[u128]) -> Option<u128> {
    if rs.len() != ms.len() {
        return None;
    }
    let (mut x, mut m) = (0u128, 1u128);
    for (r, mi) in rs.iter().zip(ms) {
        if *mi == 0 {
            return None;
        }
        let r = r % mi;
        let g = gcd(m, *mi);
        let diff = addmod(r, mi - x % mi, *mi) % mi;
        if diff % g != 0 {
            return None;
        }
        let mg = mi / g;
        let t = mulmod(diff / g, mod_inv((m / g) % mg, mg)?, mg);
        let next = (m / g).checked_mul(*mi)?;
        x = x.checked_add(m.checked_mul(t)?)?;
        m = next;
    }
    Some(x % m)
}

pub fn sqrt_mod(a: u128, p: u128) -> Option<u128> {
    if !is_prime(p) {
        return None;
    }
    let a = a % p;
    if a == 0 || p == 2 {
        return Some(a);
    }
    if powmod(a, (p - 1) / 2, p)? != 1 {
        return None;
    }
    let s = (p - 1).trailing_zeros();
    let q = (p - 1) >> s;
    let mut z = 2;
    while powmod(z, (p - 1) / 2, p)? != p - 1 {
        z += 1;
    }
    let (mut m, mut c, mut t, mut r) = (
        s,
        powmod(z, q, p)?,
        powmod(a, q, p)?,
        powmod(a, (q + 1) / 2, p)?,
    );
    while t != 1 {
        let mut i = 0;
        let mut tt = t;
        while tt != 1 {
            tt = mulmod(tt, tt, p);
            i += 1;
        }
        let mut b = c;
        for _ in 0..m - i - 1 {
            b = mulmod(b, b, p);
        }
        m = i;
        c = mulmod(b, b, p);
        t = mulmod(t, c, p);
        r = mulmod(r, b, p);
    }
    Some(r.min(p - r))
}

pub fn isqrt(n: u128) -> u128 {
    if n < 2 {
        return n;
    }
    let mut x = (n as f64).sqrt() as u128;
    while x.checked_mul(x).map_or(true, |v| v > n) {
        x -= 1;
    }
    while (x + 1).checked_mul(x + 1).map_or(false, |v| v <= n) {
        x += 1;
    }
    x
}

// ---------- polynomials over F_p ----------

fn field_ok(p: u128) -> bool {
    p >= 2 && p <= u64::MAX as u128 && is_prime(p)
}

fn trim(mut a: Vec<u128>) -> Vec<u128> {
    while a.last() == Some(&0) {
        a.pop();
    }
    a
}

fn poly_ok(a: &[u128], p: u128) -> bool {
    a.iter().all(|c| *c < p)
}

fn padd(a: &[u128], b: &[u128], p: u128, negate: bool) -> Vec<u128> {
    let n = a.len().max(b.len());
    trim(
        (0..n)
            .map(|i| {
                let x = a.get(i).copied().unwrap_or(0);
                let y = b.get(i).copied().unwrap_or(0);
                if negate {
                    (x + p - y) % p
                } else {
                    (x + y) % p
                }
            })
            .collect(),
    )
}

fn pmul(a: &[u128], b: &[u128], p: u128) -> Vec<u128> {
    if a.is_empty() || b.is_empty() {
        return Vec::new();
    }
    let mut c = vec![0u128; a.len() + b.len() - 1];
    for (i, x) in a.iter().enumerate() {
        for (j, y) in b.iter().enumerate() {
            c[i + j] = (c[i + j] + x * y % p) % p;
        }
    }
    trim(c)
}

fn pdivmod(a: &[u128], b: &[u128], p: u128) -> Option<(Vec<u128>, Vec<u128>)> {
    let lead = *b.last()?;
    let inv = powmod(lead, p - 2, p)?;
    let mut r = a.to_vec();
    if r.len() < b.len() {
        return Some((Vec::new(), trim(r)));
    }
    let mut q = vec![0u128; r.len() - b.len() + 1];
    for k in (0..q.len()).rev() {
        let coef = r[k + b.len() - 1] * inv % p;
        q[k] = coef;
        if coef != 0 {
            for (j, y) in b.iter().enumerate() {
                r[k + j] = (r[k + j] + p - coef * y % p) % p;
            }
        }
    }
    r.truncate(b.len() - 1);
    Some((trim(q), trim(r)))
}

fn monic(a: Vec<u128>, p: u128) -> Vec<u128> {
    match a.last() {
        None => a,
        Some(l) => {
            let inv = powmod(*l, p - 2, p).unwrap();
            a.iter().map(|c| c * inv % p).collect()
        }
    }
}

fn pgcd(a: &[u128], b: &[u128], p: u128) -> Vec<u128> {
    let (mut x, mut y) = (trim(a.to_vec()), trim(b.to_vec()));
    while !y.is_empty() {
        let (_, r) = pdivmod(&x, &y, p).unwrap();
        x = y;
        y = r;
    }
    monic(x, p)
}

fn ppowmod(a: &[u128], mut e: u128, f: &[u128], p: u128) -> Option<Vec<u128>> {
    if f.is_empty() {
        return None;
    }
    let mut r = pdivmod(&[1], f, p)?.1;
    let mut b = pdivmod(a, f, p)?.1;
    while e > 0 {
        if e & 1 == 1 {
            r = pdivmod(&pmul(&r, &b, p), f, p)?.1;
        }
        b = pdivmod(&pmul(&b, &b, p), f, p)?.1;
        e >>= 1;
    }
    Some(r)
}

fn checked(a: &[u128], b: &[u128], p: u128) -> Option<(Vec<u128>, Vec<u128>)> {
    if !field_ok(p) || !poly_ok(a, p) || !poly_ok(b, p) || a.len() > 1 << 13 || b.len() > 1 << 13 {
        return None;
    }
    Some((trim(a.to_vec()), trim(b.to_vec())))
}

pub fn pf_add(a: &[u128], b: &[u128], p: u128) -> Option<Vec<u128>> {
    let (a, b) = checked(a, b, p)?;
    Some(padd(&a, &b, p, false))
}
pub fn pf_sub(a: &[u128], b: &[u128], p: u128) -> Option<Vec<u128>> {
    let (a, b) = checked(a, b, p)?;
    Some(padd(&a, &b, p, true))
}
pub fn pf_mul(a: &[u128], b: &[u128], p: u128) -> Option<Vec<u128>> {
    let (a, b) = checked(a, b, p)?;
    Some(pmul(&a, &b, p))
}
pub fn pf_div(a: &[u128], b: &[u128], p: u128) -> Option<Vec<u128>> {
    let (a, b) = checked(a, b, p)?;
    Some(pdivmod(&a, &b, p)?.0)
}
pub fn pf_rem(a: &[u128], b: &[u128], p: u128) -> Option<Vec<u128>> {
    let (a, b) = checked(a, b, p)?;
    Some(pdivmod(&a, &b, p)?.1)
}
pub fn pf_gcd(a: &[u128], b: &[u128], p: u128) -> Option<Vec<u128>> {
    let (a, b) = checked(a, b, p)?;
    Some(pgcd(&a, &b, p))
}
pub fn pf_powmod(a: &[u128], e: u128, f: &[u128], p: u128) -> Option<Vec<u128>> {
    let (a, f) = checked(a, f, p)?;
    ppowmod(&a, e, &f, p)
}
pub fn pf_eval(a: &[u128], x: u128, p: u128) -> Option<u128> {
    let (a, _) = checked(a, &[], p)?;
    let x = x % p;
    Some(a.iter().rev().fold(0, |acc, c| (acc * x % p + c) % p))
}
/// Rabin's irreducibility test (degree >= 1).
pub fn pf_irreducible(f: &[u128], p: u128) -> Option<bool> {
    let (f, _) = checked(f, &[], p)?;
    let n = f.len().checked_sub(1)?;
    if n == 0 {
        return Some(false);
    }
    let f = monic(f, p);
    let x = vec![0, 1];
    let frob = |k: usize| -> Option<Vec<u128>> {
        let mut h = pdivmod(&x, &f, p)?.1;
        for _ in 0..k {
            h = ppowmod(&h, p, &f, p)?;
        }
        Some(h)
    };
    let mut qs = factor(n as u128)?;
    qs.dedup();
    for q in qs {
        let h = frob(n / q as usize)?;
        if pgcd(&padd(&h, &x, p, true), &f, p) != vec![1] {
            return Some(false);
        }
    }
    let h = frob(n)?;
    Some(pdivmod(&padd(&h, &x, p, true), &f, p)?.1.is_empty())
}

// ---------- permutations ----------

fn perm(p: &[u128]) -> Option<Vec<usize>> {
    let n = p.len();
    let mut seen = vec![false; n];
    let mut out = Vec::with_capacity(n);
    for x in p {
        let i = usize::try_from(*x).ok().filter(|i| *i < n)?;
        if seen[i] {
            return None;
        }
        seen[i] = true;
        out.push(i);
    }
    Some(out)
}

fn back(p: &[usize]) -> Vec<u128> {
    p.iter().map(|x| *x as u128).collect()
}

fn compose(p: &[usize], q: &[usize]) -> Vec<usize> {
    q.iter().map(|i| p[*i]).collect()
}

fn invert(p: &[usize]) -> Vec<usize> {
    let mut r = vec![0; p.len()];
    for (i, x) in p.iter().enumerate() {
        r[*x] = i;
    }
    r
}

fn cycle_lengths(p: &[usize]) -> Vec<usize> {
    let mut seen = vec![false; p.len()];
    let mut out = Vec::new();
    for s in 0..p.len() {
        if !seen[s] {
            let (mut k, mut x) = (0, s);
            while !seen[x] {
                seen[x] = true;
                x = p[x];
                k += 1;
            }
            out.push(k);
        }
    }
    out
}

pub fn pm_compose(p: &[u128], q: &[u128]) -> Option<Vec<u128>> {
    let (p, q) = (perm(p)?, perm(q)?);
    (p.len() == q.len()).then(|| back(&compose(&p, &q)))
}
pub fn pm_inverse(p: &[u128]) -> Option<Vec<u128>> {
    Some(back(&invert(&perm(p)?)))
}
pub fn pm_power(p: &[u128], mut k: u128) -> Option<Vec<u128>> {
    let mut b = perm(p)?;
    let mut r: Vec<usize> = (0..b.len()).collect();
    while k > 0 {
        if k & 1 == 1 {
            r = compose(&r, &b);
        }
        b = compose(&b, &b);
        k >>= 1;
    }
    Some(back(&r))
}
pub fn pm_order(p: &[u128]) -> Option<u128> {
    cycle_lengths(&perm(p)?)
        .into_iter()
        .try_fold(1u128, |a, k| lcm(a, k as u128))
}
/// 0 even, 1 odd.
pub fn pm_sign(p: &[u128]) -> Option<u128> {
    let c = cycle_lengths(&perm(p)?);
    Some((c.iter().map(|k| k - 1).sum::<usize>() % 2) as u128)
}
/// Cycle lengths, decreasing.
pub fn pm_cycle_type(p: &[u128]) -> Option<Vec<u128>> {
    let mut c = cycle_lengths(&perm(p)?);
    c.sort_unstable_by(|a, b| b.cmp(a));
    Some(c.into_iter().map(|k| k as u128).collect())
}

fn generators(gens: &[u128], n: u128) -> Option<(usize, Vec<Vec<usize>>)> {
    let n = usize::try_from(n).ok().filter(|n| *n <= PERM_MAX)?;
    if n == 0 {
        return gens.is_empty().then(|| (0, Vec::new()));
    }
    if gens.len() % n != 0 || gens.len() / n > 64 {
        return None;
    }
    let mut out = Vec::new();
    for g in gens.chunks(n) {
        out.push(perm(g)?);
    }
    Some((n, out))
}

/// Orbit of `x` under the group generated by `gens` (concatenated permutations of length `n`), increasing.
pub fn pm_orbit(gens: &[u128], n: u128, x: u128) -> Option<Vec<u128>> {
    let (n, gs) = generators(gens, n)?;
    let x = usize::try_from(x).ok().filter(|x| *x < n)?;
    let mut seen = vec![false; n];
    seen[x] = true;
    let mut stack = vec![x];
    while let Some(y) = stack.pop() {
        for g in &gs {
            if !seen[g[y]] {
                seen[g[y]] = true;
                stack.push(g[y]);
            }
        }
    }
    Some((0..n).filter(|i| seen[*i]).map(|i| i as u128).collect())
}

fn is_identity(h: &[usize]) -> bool {
    h.iter().enumerate().all(|(a, b)| a == *b)
}

fn level_of(g: &[usize], base: &[usize]) -> usize {
    (0..base.len())
        .find(|k| g[base[*k]] != base[*k])
        .unwrap_or(base.len())
}

/// Orbit of `base[k]` under the strong generators of level `>= k`, with coset representatives `u[x](base[k]) = x`.
fn orbit(
    k: usize,
    base: &[usize],
    strong: &[Vec<usize>],
    n: usize,
) -> (Vec<usize>, Vec<Option<Vec<usize>>>) {
    let mut trans = vec![None; n];
    trans[base[k]] = Some((0..n).collect::<Vec<usize>>());
    let mut queue = vec![base[k]];
    let mut q = 0;
    while q < queue.len() {
        let x = queue[q];
        q += 1;
        for s in strong.iter().filter(|s| level_of(s, base) >= k) {
            let y = s[x];
            if trans[y].is_none() {
                trans[y] = Some(compose(s, trans[x].as_ref().unwrap()));
                queue.push(y);
            }
        }
    }
    (queue, trans)
}

fn sift(
    base: &[usize],
    trans: &[Vec<Option<Vec<usize>>>],
    mut h: Vec<usize>,
    from: usize,
) -> (Vec<usize>, usize) {
    for k in from..base.len() {
        match &trans[k][h[base[k]]] {
            None => return (h, k),
            Some(u) => h = compose(&invert(u), &h),
        }
    }
    (h, base.len())
}

/// Order of the generated group (deterministic Schreier–Sims); `None` on overflow or work limits.
pub fn pm_group_order(gens: &[u128], n: u128) -> Option<u128> {
    let (n, gs) = generators(gens, n)?;
    let mut base: Vec<usize> = Vec::new();
    let mut strong: Vec<Vec<usize>> = Vec::new();
    for g in gs {
        if is_identity(&g) {
            continue;
        }
        if base.iter().all(|b| g[*b] == *b) {
            base.push((0..n).find(|i| g[*i] != *i).unwrap());
        }
        strong.push(g);
    }
    let mut trans: Vec<Vec<Option<Vec<usize>>>> = (0..base.len())
        .map(|k| orbit(k, &base, &strong, n).1)
        .collect();
    let mut work: u64 = 1 << 22;
    let mut i = base.len();
    'outer: while i > 0 {
        let level = i - 1;
        let (points, t) = orbit(level, &base, &strong, n);
        trans[level] = t;
        let gens: Vec<Vec<usize>> = strong
            .iter()
            .filter(|s| level_of(s, &base) >= level)
            .cloned()
            .collect();
        for x in points {
            for s in &gens {
                work = work.checked_sub(1)?;
                let ux = trans[level][x].as_ref().unwrap();
                let usx = trans[level][s[x]].as_ref().unwrap();
                let schreier = compose(&invert(usx), &compose(s, ux));
                let (h, j) = sift(&base, &trans, schreier, level + 1);
                if !is_identity(&h) {
                    if strong.len() >= 4096 {
                        return None;
                    }
                    if j == base.len() {
                        base.push((0..n).find(|p| h[*p] != *p).unwrap());
                        trans.push(Vec::new());
                    }
                    strong.push(h);
                    for k in level + 1..=j {
                        trans[k] = orbit(k, &base, &strong, n).1;
                    }
                    i = j + 1;
                    continue 'outer;
                }
            }
        }
        i -= 1;
    }
    trans.iter().try_fold(1u128, |acc, t| {
        acc.checked_mul(t.iter().filter(|u| u.is_some()).count() as u128)
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn number_theory_polys_perms() {
        assert!(
            is_prime(1_000_000_007)
                && !is_prime(561)
                && is_prime(170141183460469231731687303715884105727)
        );
        assert_eq!(factor(600851475143).unwrap(), vec![71, 839, 1471, 6857]);
        assert_eq!(
            factor(1_000_000_007u128 * 998_244_353).unwrap(),
            vec![998_244_353, 1_000_000_007]
        );
        assert_eq!(crt(&[2, 3, 2], &[3, 5, 7]), Some(23));
        assert_eq!(mod_inv(3, 11), Some(4));
        assert_eq!(sqrt_mod(10, 13), Some(6));
        assert_eq!(pf_irreducible(&[1, 1, 1], 2), Some(true));
        assert_eq!(pf_irreducible(&[1, 0, 1], 2), Some(false));
        let s5 = [1, 2, 3, 4, 0, 1, 0, 2, 3, 4];
        assert_eq!(pm_group_order(&s5, 5), Some(120));
        assert_eq!(pm_group_order(&[1, 2, 0, 3, 4, 0, 1, 3, 4, 2], 5), Some(60));
    }
}

pub fn next_prime(n: u128) -> Option<u128> {
    let mut x = n.checked_add(1)?;
    while !is_prime(x) {
        x = x.checked_add(1)?;
    }
    Some(x)
}
