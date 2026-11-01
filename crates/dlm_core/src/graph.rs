//! v0.115: dense natural-number vectors (`NatVec`) and graph algorithms over
//! edge lists (`gr_*`). A graph is `n` vertices `0..n` and parallel edge
//! vectors `src`, `dst` (edge `i` is `src[i] -> dst[i]`); weights are a
//! `FloatVec`, capacities a `NatVec` of the same length. Adjacency is visited
//! in edge order. Every function is total: invalid input (lengths, endpoints,
//! NaN or negative weights where forbidden) gives `None`. The native runtime
//! (`dvg_*` in `backend_c_runtime.c`) implements the same definitions.
use std::cmp::Reverse;
use std::collections::{BinaryHeap, VecDeque};

pub const VEC_MAX: usize = 1 << 24;

// ---------- NatVec ----------

pub fn range(start: u128, len: u128) -> Option<Vec<u128>> {
    if len > VEC_MAX as u128 {
        return None;
    }
    if len != 0 {
        start.checked_add(len - 1)?;
    }
    Some((0..len).map(|i| start + i).collect())
}

pub fn sum(v: &[u128]) -> Option<u128> {
    v.iter().try_fold(0u128, |a, x| a.checked_add(*x))
}

// ---------- graphs ----------

type Adj = Vec<Vec<(usize, usize)>>;

fn idx(x: u128, n: usize) -> Option<usize> {
    usize::try_from(x).ok().filter(|v| *v < n)
}

fn check(n: u128, src: &[u128], dst: &[u128]) -> Option<usize> {
    if n > VEC_MAX as u128 || src.len() != dst.len() {
        return None;
    }
    let n = n as usize;
    for (a, b) in src.iter().zip(dst) {
        idx(*a, n)?;
        idx(*b, n)?;
    }
    Some(n)
}

fn adjacency(n: usize, src: &[u128], dst: &[u128], undirected: bool) -> Adj {
    let mut adj = vec![Vec::new(); n];
    for (e, (a, b)) in src.iter().zip(dst).enumerate() {
        adj[*a as usize].push((*b as usize, e));
        if undirected {
            adj[*b as usize].push((*a as usize, e));
        }
    }
    adj
}

fn weights_ok(w: &[f64], edges: usize, nonnegative: bool) -> bool {
    w.len() == edges && w.iter().all(|x| !x.is_nan() && (!nonnegative || *x >= 0.0))
}

/// Unweighted distances from `s`; unreachable vertices get `n`.
pub fn bfs(n: u128, src: &[u128], dst: &[u128], s: u128) -> Option<Vec<u128>> {
    let n = check(n, src, dst)?;
    let s = idx(s, n)?;
    let adj = adjacency(n, src, dst, false);
    let mut d = vec![n as u128; n];
    let mut q = VecDeque::from([s]);
    d[s] = 0;
    while let Some(u) = q.pop_front() {
        for &(v, _) in &adj[u] {
            if d[v] == n as u128 {
                d[v] = d[u] + 1;
                q.push_back(v);
            }
        }
    }
    Some(d)
}

/// Depth-first preorder from `s` (neighbours in edge order).
pub fn dfs(n: u128, src: &[u128], dst: &[u128], s: u128) -> Option<Vec<u128>> {
    let n = check(n, src, dst)?;
    let s = idx(s, n)?;
    let adj = adjacency(n, src, dst, false);
    let mut seen = vec![false; n];
    let mut out = vec![s as u128];
    let mut stack = vec![(s, 0usize)];
    seen[s] = true;
    while let Some(top) = stack.last_mut() {
        let (u, i) = *top;
        if i == adj[u].len() {
            stack.pop();
            continue;
        }
        top.1 += 1;
        let v = adj[u][i].0;
        if !seen[v] {
            seen[v] = true;
            out.push(v as u128);
            stack.push((v, 0));
        }
    }
    Some(out)
}

/// Kahn's algorithm taking the smallest ready vertex first; `None` on a cycle.
pub fn toposort(n: u128, src: &[u128], dst: &[u128]) -> Option<Vec<u128>> {
    let n = check(n, src, dst)?;
    let adj = adjacency(n, src, dst, false);
    let mut indeg = vec![0usize; n];
    for b in dst {
        indeg[*b as usize] += 1;
    }
    let mut ready: BinaryHeap<Reverse<usize>> =
        (0..n).filter(|v| indeg[*v] == 0).map(Reverse).collect();
    let mut out = Vec::with_capacity(n);
    while let Some(Reverse(u)) = ready.pop() {
        out.push(u as u128);
        for &(v, _) in &adj[u] {
            indeg[v] -= 1;
            if indeg[v] == 0 {
                ready.push(Reverse(v));
            }
        }
    }
    (out.len() == n).then_some(out)
}

fn find(p: &mut [usize], mut x: usize) -> usize {
    while p[x] != x {
        p[x] = p[p[x]];
        x = p[x];
    }
    x
}

/// Labels `0, 1, ...` numbered by first appearance in vertex order.
fn relabel(raw: &[usize]) -> Vec<u128> {
    let mut map = vec![usize::MAX; raw.len()];
    let mut next = 0usize;
    raw.iter()
        .map(|r| {
            if map[*r] == usize::MAX {
                map[*r] = next;
                next += 1;
            }
            map[*r] as u128
        })
        .collect()
}

/// Connected components of the underlying undirected graph.
pub fn components(n: u128, src: &[u128], dst: &[u128]) -> Option<Vec<u128>> {
    let n = check(n, src, dst)?;
    let mut p: Vec<usize> = (0..n).collect();
    for (a, b) in src.iter().zip(dst) {
        let (x, y) = (find(&mut p, *a as usize), find(&mut p, *b as usize));
        if x != y {
            p[x.max(y)] = x.min(y);
        }
    }
    let raw: Vec<usize> = (0..n).map(|v| find(&mut p, v)).collect();
    Some(relabel(&raw))
}

/// Strongly connected components (Kosaraju), labels by first appearance.
pub fn scc(n: u128, src: &[u128], dst: &[u128]) -> Option<Vec<u128>> {
    let n = check(n, src, dst)?;
    let adj = adjacency(n, src, dst, false);
    let radj = adjacency(n, dst, src, false);
    let mut seen = vec![false; n];
    let mut order = Vec::with_capacity(n);
    for r in 0..n {
        if seen[r] {
            continue;
        }
        seen[r] = true;
        let mut stack = vec![(r, 0usize)];
        while let Some(top) = stack.last_mut() {
            let (u, i) = *top;
            if i == adj[u].len() {
                order.push(u);
                stack.pop();
                continue;
            }
            top.1 += 1;
            let v = adj[u][i].0;
            if !seen[v] {
                seen[v] = true;
                stack.push((v, 0));
            }
        }
    }
    let mut comp = vec![usize::MAX; n];
    let mut c = 0usize;
    for &r in order.iter().rev() {
        if comp[r] != usize::MAX {
            continue;
        }
        comp[r] = c;
        let mut stack = vec![r];
        while let Some(u) = stack.pop() {
            for &(v, _) in &radj[u] {
                if comp[v] == usize::MAX {
                    comp[v] = c;
                    stack.push(v);
                }
            }
        }
        c += 1;
    }
    Some(relabel(&comp))
}

/// Two-colouring of the underlying undirected graph; `None` if it has an odd cycle.
pub fn bipartite(n: u128, src: &[u128], dst: &[u128]) -> Option<Vec<u128>> {
    let n = check(n, src, dst)?;
    let adj = adjacency(n, src, dst, true);
    let mut col = vec![2u128; n];
    for r in 0..n {
        if col[r] != 2 {
            continue;
        }
        col[r] = 0;
        let mut q = VecDeque::from([r]);
        while let Some(u) = q.pop_front() {
            for &(v, _) in &adj[u] {
                if col[v] == 2 {
                    col[v] = 1 - col[u];
                    q.push_back(v);
                } else if col[v] == col[u] {
                    return None;
                }
            }
        }
    }
    Some(col)
}

/// Single-source shortest paths, non-negative weights; unreachable = `inf`.
pub fn dijkstra(n: u128, src: &[u128], dst: &[u128], w: &[f64], s: u128) -> Option<Vec<f64>> {
    let n = check(n, src, dst)?;
    let s = idx(s, n)?;
    if !weights_ok(w, src.len(), true) {
        return None;
    }
    let adj = adjacency(n, src, dst, false);
    let mut d = vec![f64::INFINITY; n];
    let mut done = vec![false; n];
    let mut heap = BinaryHeap::new();
    d[s] = 0.0;
    heap.push(Reverse((0u64, s)));
    while let Some(Reverse((_, u))) = heap.pop() {
        if done[u] {
            continue;
        }
        done[u] = true;
        for &(v, e) in &adj[u] {
            let nd = d[u] + w[e];
            if nd < d[v] {
                d[v] = nd;
                heap.push(Reverse((nd.to_bits(), v)));
            }
        }
    }
    Some(d)
}

/// Bellman–Ford; `None` when a negative cycle is reachable from `s`.
pub fn bellman_ford(n: u128, src: &[u128], dst: &[u128], w: &[f64], s: u128) -> Option<Vec<f64>> {
    let n = check(n, src, dst)?;
    let s = idx(s, n)?;
    if !weights_ok(w, src.len(), false) {
        return None;
    }
    let mut d = vec![f64::INFINITY; n];
    d[s] = 0.0;
    let relax = |d: &mut Vec<f64>| {
        let mut changed = false;
        for e in 0..src.len() {
            let (a, b) = (src[e] as usize, dst[e] as usize);
            if d[a] != f64::INFINITY {
                let nd = d[a] + w[e];
                if nd < d[b] {
                    d[b] = nd;
                    changed = true;
                }
            }
        }
        changed
    };
    for _ in 1..n {
        if !relax(&mut d) {
            return Some(d);
        }
    }
    if relax(&mut d) {
        return None;
    }
    Some(d)
}

/// All-pairs shortest paths (Floyd–Warshall), row-major `n × n`.
pub fn floyd(n: u128, src: &[u128], dst: &[u128], w: &[f64]) -> Option<Vec<f64>> {
    let n = check(n, src, dst)?;
    if n.checked_mul(n)? > VEC_MAX || !weights_ok(w, src.len(), false) {
        return None;
    }
    let mut d = vec![f64::INFINITY; n * n];
    for i in 0..n {
        d[i * n + i] = 0.0;
    }
    for e in 0..src.len() {
        let k = src[e] as usize * n + dst[e] as usize;
        if w[e] < d[k] {
            d[k] = w[e];
        }
    }
    for k in 0..n {
        for i in 0..n {
            let a = d[i * n + k];
            if a == f64::INFINITY {
                continue;
            }
            for j in 0..n {
                let nd = a + d[k * n + j];
                if nd < d[i * n + j] {
                    d[i * n + j] = nd;
                }
            }
        }
    }
    Some(d)
}

/// Kruskal minimum spanning forest of the undirected graph: chosen edge
/// indices in selection order (weight, then index).
pub fn mst(n: u128, src: &[u128], dst: &[u128], w: &[f64]) -> Option<Vec<u128>> {
    let n = check(n, src, dst)?;
    if !weights_ok(w, src.len(), false) {
        return None;
    }
    let mut order: Vec<usize> = (0..src.len()).collect();
    order.sort_by(|a, b| w[*a].partial_cmp(&w[*b]).unwrap().then(a.cmp(b)));
    let mut p: Vec<usize> = (0..n).collect();
    let mut out = Vec::new();
    for e in order {
        let (x, y) = (find(&mut p, src[e] as usize), find(&mut p, dst[e] as usize));
        if x != y {
            p[x.max(y)] = x.min(y);
            out.push(e as u128);
        }
    }
    Some(out)
}

/// Maximum `s`–`t` flow with integer capacities (Edmonds–Karp).
pub fn maxflow(
    n: u128,
    src: &[u128],
    dst: &[u128],
    cap: &[u128],
    s: u128,
    t: u128,
) -> Option<u128> {
    let n = check(n, src, dst)?;
    let (s, t) = (idx(s, n)?, idx(t, n)?);
    if s == t || cap.len() != src.len() {
        return None;
    }
    let mut out_s = 0u128;
    for e in 0..src.len() {
        if src[e] as usize == s {
            out_s = out_s.checked_add(cap[e])?;
        }
    }
    // residual edges 2e (forward) and 2e+1 (backward)
    let mut to = Vec::with_capacity(2 * src.len());
    let mut res = Vec::with_capacity(2 * src.len());
    let mut adj = vec![Vec::new(); n];
    for e in 0..src.len() {
        let (a, b) = (src[e] as usize, dst[e] as usize);
        adj[a].push(to.len());
        to.push(b);
        res.push(cap[e]);
        adj[b].push(to.len());
        to.push(a);
        res.push(0u128);
    }
    let mut flow = 0u128;
    loop {
        let mut via = vec![usize::MAX; n];
        let mut seen = vec![false; n];
        seen[s] = true;
        let mut q = VecDeque::from([s]);
        while let Some(u) = q.pop_front() {
            if u == t {
                break;
            }
            for &r in &adj[u] {
                let v = to[r];
                if !seen[v] && res[r] > 0 {
                    seen[v] = true;
                    via[v] = r;
                    q.push_back(v);
                }
            }
        }
        if !seen[t] {
            return Some(flow);
        }
        let mut aug = u128::MAX;
        let mut v = t;
        while v != s {
            let r = via[v];
            aug = aug.min(res[r]);
            v = to[r ^ 1];
        }
        let mut v = t;
        while v != s {
            let r = via[v];
            res[r] -= aug;
            res[r ^ 1] += aug;
            v = to[r ^ 1];
        }
        flow += aug;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn small_graphs() {
        let (s, d) = (vec![0, 0, 1, 2, 3], vec![1, 2, 3, 3, 4]);
        assert_eq!(bfs(6, &s, &d, 0).unwrap(), vec![0, 1, 1, 2, 3, 6]);
        assert_eq!(dfs(6, &s, &d, 0).unwrap(), vec![0, 1, 3, 4, 2]);
        assert_eq!(toposort(6, &s, &d).unwrap(), vec![0, 1, 2, 3, 4, 5]);
        assert_eq!(components(6, &s, &d).unwrap(), vec![0, 0, 0, 0, 0, 1]);
        assert_eq!(scc(3, &[0, 1, 2], &[1, 0, 0]).unwrap(), vec![0, 0, 1]);
        assert!(toposort(2, &[0, 1], &[1, 0]).is_none());
        assert_eq!(
            maxflow(
                4,
                &[0, 0, 1, 2, 1],
                &[1, 2, 3, 3, 2],
                &[3, 2, 2, 3, 1],
                0,
                3
            ),
            Some(5)
        );
    }
}
