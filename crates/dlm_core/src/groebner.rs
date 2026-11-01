//! v0.120: multivariate polynomials over `F_p` and Gröbner bases.
//!
//! A polynomial in `n` variables is a `NatVec` of terms `[c, e_1, ..., e_n]`
//! (any order, repeated monomials allowed on input); outputs are normalized:
//! no zero coefficients, distinct monomials, decreasing in the monomial order
//! (`0` lex, `1` graded lex, `2` graded reverse lex). A system is the
//! concatenation of polynomials with `ends` offsets. `gb_basis` returns the
//! reduced Gröbner basis (monic, sorted by decreasing leading monomial)
//! encoded `[k, len_1, ..., len_k, terms...]`. `p` is a prime `< 2^63`,
//! `n <= 32`, exponents `< 2^31`. Work is bounded; exceeding it gives `None`.
//! The native runtime (`dvg2_*`) implements the same definitions.
use std::cmp::Ordering;

pub const MAX_VARS: usize = 32;
pub const MAX_BASIS: usize = 2000;
pub const WORK: u64 = 20_000_000;

#[derive(Clone, Copy)]
pub struct Ctx {
    pub n: usize,
    pub p: u64,
    pub order: u8,
}

pub type Term = (Vec<u32>, u64);
pub type Poly = Vec<Term>;

impl Ctx {
    pub fn new(n: u128, p: u128, order: u128) -> Option<Ctx> {
        let n = usize::try_from(n).ok().filter(|n| *n <= MAX_VARS)?;
        if p < 2 || p >= 1 << 63 || order > 2 || !crate::number::is_prime(p) {
            return None;
        }
        Some(Ctx {
            n,
            p: p as u64,
            order: order as u8,
        })
    }
    pub fn cmp(&self, a: &[u32], b: &[u32]) -> Ordering {
        let deg = |m: &[u32]| m.iter().map(|x| *x as u64).sum::<u64>();
        match self.order {
            0 => a.cmp(b),
            1 => deg(a).cmp(&deg(b)).then_with(|| a.cmp(b)),
            _ => deg(a).cmp(&deg(b)).then_with(|| {
                for i in (0..a.len()).rev() {
                    if a[i] != b[i] {
                        return b[i].cmp(&a[i]);
                    }
                }
                Ordering::Equal
            }),
        }
    }
    fn mul(&self, a: u64, b: u64) -> u64 {
        ((a as u128 * b as u128) % self.p as u128) as u64
    }
    fn inv(&self, a: u64) -> u64 {
        crate::number::powmod(a as u128, self.p as u128 - 2, self.p as u128).unwrap() as u64
    }
    pub fn normalize(&self, mut t: Poly) -> Poly {
        t.sort_by(|a, b| self.cmp(&b.0, &a.0));
        let mut out: Poly = Vec::with_capacity(t.len());
        for (m, c) in t {
            match out.last_mut() {
                Some(last) if last.0 == m => last.1 = (last.1 + c) % self.p,
                _ => out.push((m, c % self.p)),
            }
        }
        out.retain(|t| t.1 != 0);
        out
    }
    /// `f - c·x^s·g` (all normalized).
    fn sub_scaled(&self, f: &Poly, c: u64, s: &[u32], g: &Poly) -> Option<Poly> {
        let mut out = Vec::with_capacity(f.len() + g.len());
        let (mut i, mut j) = (0, 0);
        while i < f.len() || j < g.len() {
            let gm: Option<Vec<u32>> = match g.get(j) {
                None => None,
                Some(t) => Some(
                    t.0.iter()
                        .zip(s)
                        .map(|(a, b)| a.checked_add(*b).filter(|v| *v < 1 << 31))
                        .collect::<Option<Vec<_>>>()?,
                ),
            };
            let ord = match (f.get(i), &gm) {
                (Some(ft), Some(m)) => self.cmp(&ft.0, m),
                (Some(_), None) => Ordering::Greater,
                _ => Ordering::Less,
            };
            match ord {
                Ordering::Greater => {
                    out.push(f[i].clone());
                    i += 1;
                }
                Ordering::Less => {
                    let v = (self.p - self.mul(c, g[j].1)) % self.p;
                    if v != 0 {
                        out.push((gm.unwrap(), v));
                    }
                    j += 1;
                }
                Ordering::Equal => {
                    let v = (f[i].1 + self.p - self.mul(c, g[j].1)) % self.p;
                    if v != 0 {
                        out.push((f[i].0.clone(), v));
                    }
                    i += 1;
                    j += 1;
                }
            }
        }
        Some(out)
    }
    fn monic(&self, f: Poly) -> Poly {
        match f.first() {
            None => f,
            Some(t) => {
                let k = self.inv(t.1);
                f.into_iter().map(|(m, c)| (m, self.mul(c, k))).collect()
            }
        }
    }
    /// Full reduction of `f` by `gs` (first divisor in list order at each step).
    pub fn reduce(&self, f: &Poly, gs: &[Poly], work: &mut u64) -> Option<Poly> {
        let mut p = f.clone();
        let mut r: Poly = Vec::new();
        while let Some((m, c)) = p.first().cloned() {
            *work = work.checked_sub(1 + p.len() as u64 + gs.len() as u64 * (self.n as u64 + 1))?;
            let div = gs
                .iter()
                .find(|g| !g.is_empty() && g[0].0.iter().zip(&m).all(|(a, b)| a <= b));
            match div {
                Some(g) => {
                    let s: Vec<u32> = m.iter().zip(&g[0].0).map(|(a, b)| a - b).collect();
                    let k = self.mul(c, self.inv(g[0].1));
                    *work = work.checked_sub(g.len() as u64 * (self.n as u64 + 1))?;
                    p = self.sub_scaled(&p, k, &s, g)?;
                }
                None => {
                    r.push((m, c));
                    p.remove(0);
                }
            }
        }
        Some(r)
    }
    fn lcm(a: &[u32], b: &[u32]) -> Vec<u32> {
        a.iter().zip(b).map(|(x, y)| *x.max(y)).collect()
    }
    pub fn basis(&self, input: Vec<Poly>) -> Option<Vec<Poly>> {
        let mut work = WORK;
        let mut g: Vec<Poly> = input
            .into_iter()
            .map(|f| self.normalize(f))
            .filter(|f| !f.is_empty())
            .map(|f| self.monic(f))
            .collect();
        if g.len() > MAX_BASIS {
            return None;
        }
        let mut pairs: Vec<(usize, usize)> = Vec::new();
        for j in 0..g.len() {
            for i in 0..j {
                work = work.checked_sub(1)?;
                pairs.push((i, j));
            }
        }
        while !pairs.is_empty() {
            work = work.checked_sub((1 + pairs.len() as u64) * (1 + self.n as u64))?;
            // smallest lcm, then smallest (i, j)
            let mut best = 0;
            for k in 1..pairs.len() {
                let (a, b) = pairs[k];
                let (c, d) = pairs[best];
                let o = self.cmp(
                    &Self::lcm(&g[a][0].0, &g[b][0].0),
                    &Self::lcm(&g[c][0].0, &g[d][0].0),
                );
                if o == Ordering::Less || (o == Ordering::Equal && (a, b) < (c, d)) {
                    best = k;
                }
            }
            let (i, j) = pairs.remove(best);
            let (mi, mj) = (&g[i][0].0, &g[j][0].0);
            let l = Self::lcm(mi, mj);
            if mi.iter().zip(mj).all(|(a, b)| *a == 0 || *b == 0) {
                continue;
            }
            let si: Vec<u32> = l.iter().zip(mi).map(|(a, b)| a - b).collect();
            let sj: Vec<u32> = l.iter().zip(mj).map(|(a, b)| a - b).collect();
            let gi: Poly = g[i]
                .iter()
                .map(|(m, c)| {
                    Some((
                        m.iter()
                            .zip(&si)
                            .map(|(a, b)| a.checked_add(*b).filter(|v| *v < 1 << 31))
                            .collect::<Option<Vec<_>>>()?,
                        *c,
                    ))
                })
                .collect::<Option<_>>()?;
            let s = self.sub_scaled(&gi, 1, &sj, &g[j])?;
            let r = self.reduce(&s, &g, &mut work)?;
            if !r.is_empty() {
                if g.len() >= MAX_BASIS {
                    return None;
                }
                let k = g.len();
                g.push(self.monic(r));
                for i in 0..k {
                    work = work.checked_sub(1)?;
                    pairs.push((i, k));
                }
            }
        }
        // minimal basis
        let mut keep: Vec<Poly> = Vec::new();
        for (i, f) in g.iter().enumerate() {
            let redundant = g.iter().enumerate().any(|(j, h)| {
                j != i
                    && h[0].0.iter().zip(&f[0].0).all(|(a, b)| a <= b)
                    && (h[0].0 != f[0].0 || j < i)
            });
            if !redundant {
                keep.push(f.clone());
            }
        }
        // interreduce
        let mut out = Vec::with_capacity(keep.len());
        for i in 0..keep.len() {
            let others: Vec<Poly> = keep
                .iter()
                .enumerate()
                .filter(|(j, _)| *j != i)
                .map(|(_, h)| h.clone())
                .collect();
            let head = vec![keep[i][0].clone()];
            let tail: Poly = keep[i][1..].to_vec();
            let r = self.reduce(&tail, &others, &mut work)?;
            let mut f = head;
            f.extend(r);
            out.push(self.monic(f));
        }
        out.sort_by(|a, b| self.cmp(&b[0].0, &a[0].0));
        Some(out)
    }
    pub fn decode(&self, v: &[u128]) -> Option<Poly> {
        let w = self.n + 1;
        if v.len() % w != 0 {
            return None;
        }
        let mut t = Vec::with_capacity(v.len() / w);
        for chunk in v.chunks(w) {
            if chunk[0] >= self.p as u128 || chunk[1..].iter().any(|e| *e >= 1 << 31) {
                return None;
            }
            t.push((
                chunk[1..].iter().map(|e| *e as u32).collect(),
                chunk[0] as u64,
            ));
        }
        Some(self.normalize(t))
    }
    pub fn encode(&self, f: &Poly) -> Vec<u128> {
        f.iter()
            .flat_map(|(m, c)| std::iter::once(*c as u128).chain(m.iter().map(|e| *e as u128)))
            .collect()
    }
    pub fn system(&self, v: &[u128], ends: &[u128]) -> Option<Vec<Poly>> {
        let mut prev = 0u128;
        let mut out = Vec::new();
        for e in ends {
            if *e < prev || *e > v.len() as u128 {
                return None;
            }
            out.push(self.decode(&v[prev as usize..*e as usize])?);
            prev = *e;
        }
        (prev == v.len() as u128).then_some(out)
    }
}

pub fn gb_basis(v: &[u128], ends: &[u128], n: u128, p: u128, order: u128) -> Option<Vec<u128>> {
    let ctx = Ctx::new(n, p, order)?;
    let g = ctx.basis(ctx.system(v, ends)?)?;
    let enc: Vec<Vec<u128>> = g.iter().map(|f| ctx.encode(f)).collect();
    let mut out = vec![enc.len() as u128];
    out.extend(enc.iter().map(|e| e.len() as u128));
    for e in enc {
        out.extend(e);
    }
    (out.len() <= crate::graph::VEC_MAX).then_some(out)
}

pub fn gb_reduce(
    f: &[u128],
    v: &[u128],
    ends: &[u128],
    n: u128,
    p: u128,
    order: u128,
) -> Option<Vec<u128>> {
    let ctx = Ctx::new(n, p, order)?;
    let gs: Vec<Poly> = ctx
        .system(v, ends)?
        .into_iter()
        .map(|g| ctx.normalize(g))
        .collect();
    let mut work = WORK;
    Some(ctx.encode(&ctx.reduce(&ctx.decode(f)?, &gs, &mut work)?))
}

pub fn mv_binary(a: &[u128], b: &[u128], n: u128, p: u128, op: u8) -> Option<Vec<u128>> {
    let ctx = Ctx::new(n, p, 2)?;
    let (x, y) = (ctx.decode(a)?, ctx.decode(b)?);
    let r = match op {
        0 => ctx.sub_scaled(&x, ctx.p - 1, &vec![0; ctx.n], &y)?,
        1 => ctx.sub_scaled(&x, 1, &vec![0; ctx.n], &y)?,
        _ => {
            if x.len().checked_mul(y.len())? > 1 << 22 {
                return None;
            }
            let mut t = Vec::with_capacity(x.len() * y.len());
            for (m1, c1) in &x {
                for (m2, c2) in &y {
                    let m: Vec<u32> = m1
                        .iter()
                        .zip(m2)
                        .map(|(a, b)| a.checked_add(*b).filter(|v| *v < 1 << 31))
                        .collect::<Option<_>>()?;
                    t.push((m, ctx.mul(*c1, *c2)));
                }
            }
            ctx.normalize(t)
        }
    };
    let out = ctx.encode(&r);
    (out.len() <= crate::graph::VEC_MAX).then_some(out)
}

/// Normal form of a polynomial in the given order.
pub fn mv_normalize(a: &[u128], n: u128, p: u128, order: u128) -> Option<Vec<u128>> {
    let ctx = Ctx::new(n, p, order)?;
    Some(ctx.encode(&ctx.decode(a)?))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn small_bases() {
        // x^2 - y, x y - 1 over F_7, lex x > y: basis {x - y^2, y^3 - 1}
        let p = 7u128;
        let f = [1, 2, 0, p - 1, 0, 1];
        let g = [1, 1, 1, p - 1, 0, 0];
        let v: Vec<u128> = f.iter().chain(&g).copied().collect();
        let b = gb_basis(&v, &[6, 12], 2, p, 0).unwrap();
        assert_eq!(b, vec![2, 6, 6, 1, 1, 0, 6, 0, 2, 1, 0, 3, 6, 0, 0]);
    }
}
