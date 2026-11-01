//! v0.119: a CDCL SAT solver, integral simplicial homology (Smith normal
//! form over i128) and Vietoris–Rips persistent homology over F_2.
//!
//! * SAT: literal `2v` is `x_v`, `2v + 1` is `¬x_v`; clause `i` is
//!   `lits[ends[i-1]..ends[i]]`. Watched literals, first-UIP learning,
//!   non-chronological backjumping, VSIDS (bump 1, decay 1/0.95, rescale at
//!   1e100; decision = highest activity, lowest index on ties) with phase
//!   saving (initial phase false), no restarts, at most 2·10^6 conflicts.
//! * Homology: simplices are vertex sets (`faces[ends[i-1]..ends[i]]`), the
//!   complex is their closure; output `[top + 1, (betti_d, k_d, t_1..t_k)...]`
//!   with the torsion coefficients `t > 1` of `H_d(X; Z)`.
//! * Persistence: points `n × dim`, Rips simplices up to dimension `maxdim + 1`
//!   with diameter `<= radius`, ordered by (value, dimension, vertices);
//!   output `(dimension, birth, death)` triples sorted, `inf` for essential
//!   classes, zero-length bars dropped.
//! The native runtime (`dvc_*`) follows the same definitions.
pub const SAT_CONFLICTS: u64 = 2_000_000;
pub const SIMPLEX_MAX: usize = 1 << 20;

// ---------------- SAT ----------------

/// `(status, model)`: 0 satisfiable, 1 unsatisfiable, 2 malformed, 3 conflict limit.
pub fn sat(lits: &[u128], ends: &[u128], nvars: u128) -> (u128, Vec<u128>) {
    let (s, m, _) = sat_full(lits, ends, nvars);
    (s, m)
}

/// `(status, model, learnt clauses in order)`.
pub fn sat_full(lits: &[u128], ends: &[u128], nvars: u128) -> (u128, Vec<u128>, Vec<Vec<usize>>) {
    let Ok(nv) = usize::try_from(nvars) else {
        return (2, Vec::new(), Vec::new());
    };
    if nv > 1 << 22 || lits.iter().any(|l| *l >= 2 * nv as u128) {
        return (2, Vec::new(), Vec::new());
    }
    let mut prev = 0u128;
    let mut raw: Vec<Vec<usize>> = Vec::with_capacity(ends.len());
    for e in ends {
        if *e < prev || *e > lits.len() as u128 {
            return (2, Vec::new(), Vec::new());
        }
        raw.push(
            lits[prev as usize..*e as usize]
                .iter()
                .map(|l| *l as usize)
                .collect(),
        );
        prev = *e;
    }
    if prev != lits.len() as u128 {
        return (2, Vec::new(), Vec::new());
    }
    let mut s = Solver::new(nv);
    let mut units = Vec::new();
    for mut c in raw {
        c.sort_unstable();
        c.dedup();
        if c.windows(2).any(|w| w[0] ^ 1 == w[1]) {
            continue;
        }
        match c.len() {
            0 => return (1, Vec::new(), Vec::new()),
            1 => units.push(c[0]),
            _ => s.add_clause(c),
        }
    }
    for u in units {
        match s.value(u) {
            0 => return (1, Vec::new(), Vec::new()),
            1 => {}
            _ => s.enqueue(u, NONE),
        }
    }
    let status = s.solve();
    let proof = std::mem::take(&mut s.proof);
    match status {
        0 => (
            0,
            s.assign.iter().map(|a| u128::from(*a == 1)).collect(),
            proof,
        ),
        k => (k, Vec::new(), proof),
    }
}

const NONE: usize = usize::MAX;

struct Solver {
    clauses: Vec<Vec<usize>>,
    watches: Vec<Vec<usize>>,
    assign: Vec<i8>,
    level: Vec<usize>,
    reason: Vec<usize>,
    phase: Vec<i8>,
    activity: Vec<f64>,
    inc: f64,
    trail: Vec<usize>,
    lim: Vec<usize>,
    qhead: usize,
    seen: Vec<bool>,
    proof: Vec<Vec<usize>>,
}

impl Solver {
    fn new(n: usize) -> Self {
        Solver {
            clauses: Vec::new(),
            watches: vec![Vec::new(); 2 * n],
            assign: vec![-1; n],
            level: vec![0; n],
            reason: vec![NONE; n],
            phase: vec![0; n],
            activity: vec![0.0; n],
            inc: 1.0,
            trail: Vec::new(),
            lim: Vec::new(),
            qhead: 0,
            seen: vec![false; n],
            proof: Vec::new(),
        }
    }
    fn value(&self, l: usize) -> i8 {
        let a = self.assign[l >> 1];
        if a < 0 {
            -1
        } else {
            a ^ (l & 1) as i8
        }
    }
    fn enqueue(&mut self, l: usize, reason: usize) {
        let v = l >> 1;
        self.assign[v] = 1 - (l & 1) as i8;
        self.level[v] = self.lim.len();
        self.reason[v] = reason;
        self.trail.push(l);
    }
    fn add_clause(&mut self, c: Vec<usize>) {
        let ci = self.clauses.len();
        self.watches[c[0]].push(ci);
        self.watches[c[1]].push(ci);
        self.clauses.push(c);
    }
    /// Conflicting clause or `NONE`.
    fn propagate(&mut self) -> usize {
        while self.qhead < self.trail.len() {
            let p = self.trail[self.qhead];
            self.qhead += 1;
            let f = p ^ 1;
            let ws = std::mem::take(&mut self.watches[f]);
            let mut keep = Vec::with_capacity(ws.len());
            let mut conflict = NONE;
            let mut k = 0;
            while k < ws.len() {
                let ci = ws[k];
                k += 1;
                if conflict != NONE {
                    keep.push(ci);
                    continue;
                }
                if self.clauses[ci][0] == f {
                    self.clauses[ci].swap(0, 1);
                }
                if self.value(self.clauses[ci][0]) == 1 {
                    keep.push(ci);
                    continue;
                }
                let len = self.clauses[ci].len();
                let mut moved = false;
                for j in 2..len {
                    if self.value(self.clauses[ci][j]) != 0 {
                        self.clauses[ci].swap(1, j);
                        let w = self.clauses[ci][1];
                        self.watches[w].push(ci);
                        moved = true;
                        break;
                    }
                }
                if moved {
                    continue;
                }
                keep.push(ci);
                let first = self.clauses[ci][0];
                if self.value(first) == 0 {
                    conflict = ci;
                } else {
                    self.enqueue(first, ci);
                }
            }
            self.watches[f] = keep;
            if conflict != NONE {
                return conflict;
            }
        }
        NONE
    }
    fn bump(&mut self, v: usize) {
        self.activity[v] += self.inc;
        if self.activity[v] > 1e100 {
            for a in &mut self.activity {
                *a *= 1e-100;
            }
            self.inc *= 1e-100;
        }
    }
    fn analyze(&mut self, mut c: usize) -> (Vec<usize>, usize) {
        let current = self.lim.len();
        let mut out = vec![NONE];
        let mut counter = 0usize;
        let mut p = NONE;
        let mut idx = self.trail.len();
        loop {
            let start = usize::from(p != NONE);
            for k in start..self.clauses[c].len() {
                let q = self.clauses[c][k];
                let v = q >> 1;
                if !self.seen[v] && self.level[v] > 0 {
                    self.seen[v] = true;
                    self.bump(v);
                    if self.level[v] >= current {
                        counter += 1;
                    } else {
                        out.push(q);
                    }
                }
            }
            loop {
                idx -= 1;
                if self.seen[self.trail[idx] >> 1] {
                    break;
                }
            }
            p = self.trail[idx];
            self.seen[p >> 1] = false;
            counter -= 1;
            if counter == 0 {
                break;
            }
            c = self.reason[p >> 1];
        }
        out[0] = p ^ 1;
        let mut back = 0;
        if out.len() > 1 {
            let mut m = 1;
            for k in 2..out.len() {
                if self.level[out[k] >> 1] > self.level[out[m] >> 1] {
                    m = k;
                }
            }
            out.swap(1, m);
            back = self.level[out[1] >> 1];
        }
        for q in &out[1..] {
            self.seen[q >> 1] = false;
        }
        (out, back)
    }
    fn backtrack(&mut self, to: usize) {
        if self.lim.len() <= to {
            return;
        }
        let start = self.lim[to];
        for k in (start..self.trail.len()).rev() {
            let v = self.trail[k] >> 1;
            self.phase[v] = self.assign[v];
            self.assign[v] = -1;
            self.reason[v] = NONE;
        }
        self.trail.truncate(start);
        self.lim.truncate(to);
        self.qhead = self.trail.len();
    }
    fn solve(&mut self) -> u128 {
        if self.propagate() != NONE {
            return 1;
        }
        let mut conflicts = 0u64;
        loop {
            let c = self.propagate();
            if c != NONE {
                conflicts += 1;
                if self.lim.is_empty() {
                    return 1;
                }
                if conflicts > SAT_CONFLICTS {
                    return 3;
                }
                let (learnt, back) = self.analyze(c);
                self.backtrack(back);
                self.proof.push(learnt.clone());
                if learnt.len() == 1 {
                    self.enqueue(learnt[0], NONE);
                } else {
                    let ci = self.clauses.len();
                    self.watches[learnt[0]].push(ci);
                    self.watches[learnt[1]].push(ci);
                    let first = learnt[0];
                    self.clauses.push(learnt);
                    self.enqueue(first, ci);
                }
                self.inc *= 1.0 / 0.95;
                continue;
            }
            let mut best = NONE;
            for v in 0..self.assign.len() {
                if self.assign[v] < 0 && (best == NONE || self.activity[v] > self.activity[best]) {
                    best = v;
                }
            }
            if best == NONE {
                return 0;
            }
            self.lim.push(self.trail.len());
            self.enqueue(2 * best + usize::from(self.phase[best] != 1), NONE);
        }
    }
}

/// Learnt-clause certificate `[k, len_1, lits_1..., len_2, ...]` of an unsatisfiable formula.
pub fn sat_proof(lits: &[u128], ends: &[u128], nvars: u128) -> Option<Vec<u128>> {
    let (s, _, proof) = sat_full(lits, ends, nvars);
    if s != 1 {
        return None;
    }
    let mut out = vec![proof.len() as u128];
    for c in proof {
        out.push(c.len() as u128);
        out.extend(c.iter().map(|l| *l as u128));
        if out.len() > crate::graph::VEC_MAX {
            return None;
        }
    }
    Some(out)
}

/// Unit propagation over `db` from `assign`; true on conflict.
fn rup_conflict(db: &[Vec<usize>], assign: &mut [i8], work: &mut u64) -> Option<bool> {
    loop {
        let mut changed = false;
        for c in db {
            *work = work.checked_sub(1 + c.len() as u64)?;
            let mut unassigned = NONE;
            let mut count = 0;
            let mut sat = false;
            for &l in c {
                let a = assign[l >> 1];
                if a < 0 {
                    if l != unassigned {
                        unassigned = l;
                        count += 1;
                    }
                } else if a ^ (l & 1) as i8 == 1 {
                    sat = true;
                    break;
                }
            }
            if sat {
                continue;
            }
            if count == 0 {
                return Some(true);
            }
            if count == 1 {
                assign[unassigned >> 1] = 1 - (unassigned & 1) as i8;
                changed = true;
            }
        }
        if !changed {
            return Some(false);
        }
    }
}

/// Independent reverse-unit-propagation check of a certificate: every learnt
/// clause follows from the formula and earlier clauses by unit propagation, and
/// so does the empty clause at the end. `None` on malformed input or work limit.
pub fn sat_check(lits: &[u128], ends: &[u128], nvars: u128, proof: &[u128]) -> Option<bool> {
    let nv = usize::try_from(nvars).ok().filter(|n| *n <= 1 << 22)?;
    let mut db: Vec<Vec<usize>> = Vec::new();
    let mut prev = 0u128;
    for e in ends {
        if *e < prev || *e > lits.len() as u128 {
            return None;
        }
        let c: Vec<usize> = lits[prev as usize..*e as usize]
            .iter()
            .map(|l| (*l < 2 * nv as u128).then_some(*l as usize))
            .collect::<Option<_>>()?;
        db.push(c);
        prev = *e;
    }
    if prev != lits.len() as u128 {
        return None;
    }
    let k = usize::try_from(*proof.first()?).ok()?;
    let mut i = 1usize;
    let mut work: u64 = 1 << 32;
    for _ in 0..k {
        let len = usize::try_from(*proof.get(i)?).ok()?;
        let start = i.checked_add(1)?;
        let end = start.checked_add(len)?;
        let c: Vec<usize> = proof
            .get(start..end)?
            .iter()
            .map(|l| (*l < 2 * nv as u128).then_some(*l as usize))
            .collect::<Option<_>>()?;
        i = end;
        let mut assign = vec![-1i8; nv];
        let mut contradictory = false;
        for &l in &c {
            let want = (l & 1) as i8; // value making l false
            if assign[l >> 1] >= 0 && assign[l >> 1] != want {
                contradictory = true;
            }
            assign[l >> 1] = want;
        }
        if !contradictory && !rup_conflict(&db, &mut assign, &mut work)? {
            return Some(false);
        }
        db.push(c);
    }
    if i != proof.len() {
        return None;
    }
    let mut assign = vec![-1i8; nv];
    rup_conflict(&db, &mut assign, &mut work)
}

const COUNT_WORK: u64 = 1 << 26;

fn count_search(db: &[Vec<usize>], nv: usize) -> Option<u128> {
    let mut stack = vec![vec![-1i8; nv]];
    let mut nodes = 1_000_000u64;
    let mut work = COUNT_WORK;
    let mut total = 0u128;
    while let Some(mut a) = stack.pop() {
        nodes = nodes.checked_sub(1)?;
        work = work.checked_sub(1 + nv as u64)?;
        if rup_conflict(db, &mut a, &mut work)? {
            continue;
        }
        let mut pick = NONE;
        for c in db {
            let mut sat = false;
            let mut first = NONE;
            for &l in c {
                let v = a[l >> 1];
                if v < 0 {
                    if first == NONE {
                        first = l >> 1;
                    }
                } else if v ^ (l & 1) as i8 == 1 {
                    sat = true;
                    break;
                }
            }
            if !sat && first != NONE {
                pick = first;
                break;
            }
        }
        if pick == NONE {
            let free = a.iter().filter(|v| **v < 0).count();
            if free >= 128 {
                return None;
            }
            total = total.checked_add(1u128 << free)?;
        } else {
            let mut right = a.clone();
            right[pick] = 1;
            a[pick] = 0;
            stack.push(right);
            stack.push(a);
        }
    }
    Some(total)
}

/// Exact number of satisfying assignments of all `nvars` variables (DPLL with unit propagation, at most 10^6 nodes).
pub fn sat_count(lits: &[u128], ends: &[u128], nvars: u128) -> Option<u128> {
    let nv = usize::try_from(nvars).ok().filter(|n| *n <= 1 << 22)?;
    let mut db: Vec<Vec<usize>> = Vec::new();
    let mut prev = 0u128;
    for e in ends {
        if *e < prev || *e > lits.len() as u128 {
            return None;
        }
        db.push(
            lits[prev as usize..*e as usize]
                .iter()
                .map(|l| (*l < 2 * nv as u128).then_some(*l as usize))
                .collect::<Option<_>>()?,
        );
        prev = *e;
    }
    if prev != lits.len() as u128 {
        return None;
    }
    count_search(&db, nv)
}

// ---------------- integral homology ----------------

type Faces = Vec<Vec<Vec<usize>>>;

fn closure(faces: &[u128], ends: &[u128]) -> Option<Faces> {
    let mut by_dim: Faces = Vec::new();
    let mut prev = 0u128;
    for e in ends {
        if *e < prev || *e > faces.len() as u128 {
            return None;
        }
        let mut s: Vec<usize> = faces[prev as usize..*e as usize]
            .iter()
            .map(|x| usize::try_from(*x).ok())
            .collect::<Option<_>>()?;
        prev = *e;
        s.sort_unstable();
        s.dedup();
        if s.is_empty() || s.len() > 11 {
            return None;
        }
        let k = s.len();
        for mask in 1u32..(1 << k) {
            let sub: Vec<usize> = (0..k)
                .filter(|i| mask >> i & 1 == 1)
                .map(|i| s[i])
                .collect();
            let d = sub.len() - 1;
            if by_dim.len() <= d {
                by_dim.resize(d + 1, Vec::new());
            }
            by_dim[d].push(sub);
        }
        if by_dim.iter().map(Vec::len).sum::<usize>() > 4 * SIMPLEX_MAX {
            return None;
        }
    }
    if prev != faces.len() as u128 {
        return None;
    }
    for level in &mut by_dim {
        level.sort_unstable();
        level.dedup();
        if level.len() > SIMPLEX_MAX {
            return None;
        }
    }
    Some(by_dim)
}

/// Invariant factors (absolute, non-zero) of an integer matrix; `None` on overflow.
pub fn smith(mut a: Vec<i128>, rows: usize, cols: usize) -> Option<Vec<i128>> {
    if rows.checked_mul(cols)? != a.len() || a.len() > 1 << 22 {
        return None;
    }
    let mut work = 20_000_000u64;
    let mut diag = Vec::new();
    let at = |i: usize, j: usize| i * cols + j;
    let mut t = 0;
    while t < rows.min(cols) {
        work = work.checked_sub(((rows - t) * (cols - t)) as u64)?;
        // smallest non-zero |entry| in the remaining block
        let mut best: Option<(usize, usize)> = None;
        for i in t..rows {
            for j in t..cols {
                let v = a[at(i, j)];
                if v != 0
                    && best.map_or(true, |(bi, bj)| {
                        v.unsigned_abs() < a[at(bi, bj)].unsigned_abs()
                    })
                {
                    best = Some((i, j));
                }
            }
        }
        let Some((bi, bj)) = best else { break };
        for j in 0..cols {
            a.swap(at(t, j), at(bi, j));
        }
        for i in 0..rows {
            a.swap(at(i, t), at(i, bj));
        }
        loop {
            work = work.checked_sub((3 * (rows - t) * (cols - t) + 2 * (rows + cols)) as u64)?;
            let p = a[at(t, t)];
            let mut again = false;
            for i in t + 1..rows {
                let q = a[at(i, t)].checked_div(p)?;
                if q != 0 {
                    for j in t..cols {
                        a[at(i, j)] = a[at(i, j)].checked_sub(q.checked_mul(a[at(t, j)])?)?;
                    }
                }
                if a[at(i, t)] != 0 {
                    again = true;
                }
            }
            for j in t + 1..cols {
                let q = a[at(t, j)].checked_div(p)?;
                if q != 0 {
                    for i in t..rows {
                        a[at(i, j)] = a[at(i, j)].checked_sub(q.checked_mul(a[at(i, t)])?)?;
                    }
                }
                if a[at(t, j)] != 0 {
                    again = true;
                }
            }
            if !again {
                // divisibility of the rest by the pivot
                let mut bad = None;
                'scan: for i in t + 1..rows {
                    for j in t + 1..cols {
                        if a[at(i, j)].checked_rem(p)? != 0 {
                            bad = Some(i);
                            break 'scan;
                        }
                    }
                }
                match bad {
                    None => break,
                    Some(i) => {
                        for j in t..cols {
                            a[at(t, j)] = a[at(t, j)].checked_add(a[at(i, j)])?;
                        }
                    }
                }
            }
            // move the smallest non-zero |entry| of row t / column t to the pivot
            let mut bi = t;
            let mut bj = t;
            for i in t..rows {
                let v = a[at(i, t)];
                if v != 0 && (a[at(bi, bj)] == 0 || v.unsigned_abs() < a[at(bi, bj)].unsigned_abs())
                {
                    bi = i;
                    bj = t;
                }
            }
            for j in t..cols {
                let v = a[at(t, j)];
                if v != 0 && (a[at(bi, bj)] == 0 || v.unsigned_abs() < a[at(bi, bj)].unsigned_abs())
                {
                    bi = t;
                    bj = j;
                }
            }
            for j in 0..cols {
                a.swap(at(t, j), at(bi, j));
            }
            for i in 0..rows {
                a.swap(at(i, t), at(i, bj));
            }
        }
        diag.push(a[at(t, t)].checked_abs()?);
        t += 1;
    }
    Some(diag)
}

/// Integral homology of the closure of the given simplices.
pub fn homology(faces: &[u128], ends: &[u128]) -> Option<Vec<u128>> {
    let c = closure(faces, ends)?;
    let top = c.len();
    let mut factors: Vec<Vec<i128>> = vec![Vec::new(); top + 1];
    // factors[d] = invariant factors of the boundary C_d -> C_{d-1}
    for d in 1..top {
        let (rows, cols) = (c[d - 1].len(), c[d].len());
        if rows.checked_mul(cols)? > 1 << 22 {
            return None;
        }
        let mut a = vec![0i128; rows * cols];
        for (j, s) in c[d].iter().enumerate() {
            for skip in 0..s.len() {
                let face: Vec<usize> = s
                    .iter()
                    .enumerate()
                    .filter(|(k, _)| *k != skip)
                    .map(|(_, x)| *x)
                    .collect();
                let i = c[d - 1].binary_search(&face).ok()?;
                a[i * cols + j] = if skip % 2 == 0 { 1 } else { -1 };
            }
        }
        factors[d] = smith(a, rows, cols)?;
    }
    let mut out = vec![top as u128];
    for d in 0..top {
        let rank_d = factors[d].len();
        let rank_up = factors[d + 1].len();
        out.push((c[d].len() - rank_d - rank_up) as u128);
        let tors: Vec<u128> = factors[d + 1]
            .iter()
            .filter(|x| **x > 1)
            .map(|x| *x as u128)
            .collect();
        out.push(tors.len() as u128);
        out.extend(tors);
    }
    Some(out)
}

// ---------------- Rips persistence ----------------

pub fn rips(points: &[f64], dim: u128, maxdim: u128, radius: f64) -> Option<Vec<f64>> {
    let dim = usize::try_from(dim).ok().filter(|d| *d > 0)?;
    if points.len() % dim != 0
        || maxdim > 2
        || radius.is_nan()
        || radius < 0.0
        || points.iter().any(|x| !x.is_finite())
    {
        return None;
    }
    let n = points.len() / dim;
    if n > 4096 {
        return None;
    }
    let mut work = 20_000_000u64;
    work = work.checked_sub(
        (n as u64)
            .checked_mul(n.saturating_sub(1) as u64)?
            .checked_mul(dim as u64)?
            .checked_mul(2)?
            / 2,
    )?;
    let mut dist = vec![0.0; n * n];
    for i in 0..n {
        for j in i + 1..n {
            let mut hi = 0.0f64;
            for k in 0..dim {
                let d = (points[i * dim + k] - points[j * dim + k]).abs();
                if !d.is_finite() {
                    return None;
                }
                hi = hi.max(d);
            }
            let scaling = if hi != 0.0 && (hi > 1e150 || hi < 1e-150) {
                hi
            } else {
                1.0
            };
            let mut s = 0.0;
            for k in 0..dim {
                let d = (points[i * dim + k] - points[j * dim + k]) / scaling;
                let p = d * d;
                s = s + p;
            }
            let r = s.sqrt() * scaling;
            if !r.is_finite() {
                return None;
            }
            dist[i * n + j] = r;
            dist[j * n + i] = r;
        }
    }
    let top = maxdim as usize + 1;
    // simplices: (value, vertices)
    let mut simplices: Vec<(f64, Vec<usize>)> = (0..n).map(|v| (0.0, vec![v])).collect();
    let mut frontier: Vec<(f64, Vec<usize>)> = simplices.clone();
    for _ in 1..=top {
        let mut next = Vec::new();
        for (val, s) in &frontier {
            let last = *s.last().unwrap();
            work = work.checked_sub(((n - last - 1) * (s.len() + 1)) as u64)?;
            for v in last + 1..n {
                let mut m = *val;
                let mut ok = true;
                for u in s {
                    let d = dist[u * n + v];
                    if d > radius {
                        ok = false;
                        break;
                    }
                    if d > m {
                        m = d;
                    }
                }
                if ok {
                    let mut t = s.clone();
                    t.push(v);
                    next.push((m, t));
                }
            }
            if simplices.len() + next.len() > SIMPLEX_MAX {
                return None;
            }
        }
        simplices.extend(next.iter().cloned());
        frontier = next;
    }
    simplices.sort_by(|a, b| {
        a.0.partial_cmp(&b.0)
            .unwrap()
            .then(a.1.len().cmp(&b.1.len()))
            .then(a.1.cmp(&b.1))
    });
    let mut index = std::collections::HashMap::new();
    for (i, (_, s)) in simplices.iter().enumerate() {
        index.insert(s.clone(), i);
    }
    let m = simplices.len();
    let mut columns: Vec<Vec<usize>> = Vec::with_capacity(m);
    for (_, s) in &simplices {
        let mut col: Vec<usize> = if s.len() == 1 {
            Vec::new()
        } else {
            (0..s.len())
                .map(|k| {
                    index[&s
                        .iter()
                        .enumerate()
                        .filter(|(i, _)| *i != k)
                        .map(|(_, x)| *x)
                        .collect::<Vec<usize>>()]
                })
                .collect()
        };
        col.sort_unstable();
        columns.push(col);
    }
    let mut owner = vec![NONE; m];
    let mut killed = vec![false; m];
    let mut out: Vec<(usize, f64, f64)> = Vec::new();
    for j in 0..m {
        let mut col = std::mem::take(&mut columns[j]);
        while let Some(&low) = col.last() {
            let o = owner[low];
            if o == NONE {
                break;
            }
            // symmetric difference of sorted lists
            let other = &columns[o];
            work = work.checked_sub(1 + col.len() as u64 + other.len() as u64)?;
            let mut merged = Vec::with_capacity(col.len() + other.len());
            let (mut a, mut b) = (0, 0);
            while a < col.len() || b < other.len() {
                if b == other.len() || (a < col.len() && col[a] < other[b]) {
                    merged.push(col[a]);
                    a += 1;
                } else if a == col.len() || other[b] < col[a] {
                    merged.push(other[b]);
                    b += 1;
                } else {
                    a += 1;
                    b += 1;
                }
            }
            col = merged;
        }
        if let Some(&low) = col.last() {
            owner[low] = j;
            killed[low] = true;
            let (birth, death) = (simplices[low].0, simplices[j].0);
            if birth < death {
                out.push((simplices[low].1.len() - 1, birth, death));
            }
        }
        columns[j] = col;
    }
    for i in 0..m {
        let d = simplices[i].1.len() - 1;
        if !killed[i] && columns[i].is_empty() && d <= maxdim as usize {
            out.push((d, simplices[i].0, f64::INFINITY));
        }
    }
    out.sort_by(|a, b| {
        a.0.cmp(&b.0)
            .then(a.1.partial_cmp(&b.1).unwrap())
            .then(a.2.partial_cmp(&b.2).unwrap())
    });
    Some(
        out.into_iter()
            .flat_map(|(d, b, e)| [d as f64, b, e])
            .collect(),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn sat_homology_rips() {
        // (x0 ∨ x1) ∧ (¬x0 ∨ x1) ∧ (¬x1 ∨ x2)
        let (s, m) = sat(&[0, 2, 1, 2, 3, 4], &[2, 4, 6], 3);
        assert_eq!((s, m[1], m[2]), (0, 1, 1));
        assert_eq!(sat(&[0, 1], &[1, 2], 1).0, 1);
        // circle: H0 = Z, H1 = Z
        assert_eq!(
            homology(&[0, 1, 1, 2, 2, 0], &[2, 4, 6]).unwrap(),
            vec![2, 1, 0, 1, 0]
        );
        let sq = [0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0];
        let r = rips(&sq, 2, 1, 2.0).unwrap();
        assert_eq!(r.len() % 3, 0);
    }
}
