//! v0.122: immutable ordered maps `NatMap` (`Nat → Nat`) and `TextMap`
//! (`Text → Nat`, keys ordered by UTF-8 bytes). Values are sorted
//! association vectors; every update returns a new map. Sizes are bounded by
//! `MAX`; growing past it gives `None`. The native runtime (`dvm_*`) uses the
//! same sorted-array representation.
pub const MAX: usize = 1 << 24;

pub fn find<K: Ord>(m: &[(K, u128)], k: &K) -> Result<usize, usize> {
    m.binary_search_by(|(x, _)| x.cmp(k))
}

pub fn insert<K: Ord + Clone>(m: &[(K, u128)], k: K, v: u128) -> Option<Vec<(K, u128)>> {
    let mut out = m.to_vec();
    match find(&out, &k) {
        Ok(i) => out[i].1 = v,
        Err(i) => {
            if out.len() >= MAX {
                return None;
            }
            out.insert(i, (k, v));
        }
    }
    Some(out)
}

pub fn remove<K: Ord + Clone>(m: &[(K, u128)], k: &K) -> Vec<(K, u128)> {
    let mut out = m.to_vec();
    if let Ok(i) = find(&out, k) {
        out.remove(i);
    }
    out
}

pub fn get<K: Ord>(m: &[(K, u128)], k: &K) -> Option<u128> {
    find(m, k).ok().map(|i| m[i].1)
}

/// `m[k] += delta` (missing keys start at 0); `None` on overflow.
pub fn add<K: Ord + Clone>(m: &[(K, u128)], k: K, delta: u128) -> Option<Vec<(K, u128)>> {
    let cur = get(m, &k).unwrap_or(0);
    insert(m, k, cur.checked_add(delta)?)
}

/// Later pairs override earlier ones.
pub fn from_pairs<K: Ord + Clone>(keys: Vec<K>, values: &[u128]) -> Option<Vec<(K, u128)>> {
    if keys.len() != values.len() || keys.len() > MAX {
        return None;
    }
    let mut pairs: Vec<(usize, K, u128)> = keys
        .into_iter()
        .zip(values.iter().copied())
        .enumerate()
        .map(|(i, (k, v))| (i, k, v))
        .collect();
    pairs.sort_by(|a, b| a.1.cmp(&b.1).then(b.0.cmp(&a.0)));
    let mut out: Vec<(K, u128)> = Vec::with_capacity(pairs.len());
    for (_, k, v) in pairs {
        if out.last().map_or(true, |(x, _)| *x != k) {
            out.push((k, v));
        }
    }
    Some(out)
}

/// Union; entries of `b` override `a`.
pub fn merge<K: Ord + Clone>(a: &[(K, u128)], b: &[(K, u128)]) -> Option<Vec<(K, u128)>> {
    let mut out = Vec::with_capacity(a.len() + b.len());
    let (mut i, mut j) = (0, 0);
    while i < a.len() || j < b.len() {
        if j == b.len() || (i < a.len() && a[i].0 < b[j].0) {
            out.push(a[i].clone());
            i += 1;
        } else if i == a.len() || b[j].0 < a[i].0 {
            out.push(b[j].clone());
            j += 1;
        } else {
            out.push(b[j].clone());
            i += 1;
            j += 1;
        }
    }
    (out.len() <= MAX).then_some(out)
}

/// Largest key `<= k` (`floor`) or smallest key `>= k` (`ceil`).
pub fn floor<K: Ord + Clone>(m: &[(K, u128)], k: &K) -> Option<K> {
    match find(m, k) {
        Ok(i) => Some(m[i].0.clone()),
        Err(0) => None,
        Err(i) => Some(m[i - 1].0.clone()),
    }
}
pub fn ceil<K: Ord + Clone>(m: &[(K, u128)], k: &K) -> Option<K> {
    match find(m, k) {
        Ok(i) => Some(m[i].0.clone()),
        Err(i) => m.get(i).map(|e| e.0.clone()),
    }
}

/// Entries with `lo <= key < hi`.
pub fn range<K: Ord + Clone>(m: &[(K, u128)], lo: &K, hi: &K) -> Vec<(K, u128)> {
    m.iter()
        .filter(|(k, _)| k >= lo && k < hi)
        .cloned()
        .collect()
}
