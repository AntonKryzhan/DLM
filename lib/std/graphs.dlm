// Graphs over edge lists (v0.115, docs/GRAPHS.md): vertices 0..n, edge i = src[i] -> dst[i] with
// weight w[i]. Algorithms run natively (gr_* builtins); this module adds construction, queries,
// path reconstruction and summaries. Invalid graphs and absent results are `none`.
module std.graphs
import std.geometry

// on top of std.geometry, std.probability and std.numerics
theory StdGraphs extends StdGeometry {
    record Graph(n: Nat, src: NatVec, dst: NatVec, w: FloatVec)
    record Route(vertices: List<Nat>)

    fn graph(n: Nat) -> Graph = Graph(n, nv_empty(), nv_empty(), fv_empty())
    fn edge_count(g: Graph) -> Nat = nv_len(field(g, src))
    fn add_edge(g: Graph, a: Nat, b: Nat, weight: Float) -> Graph =
        Graph(field(g, n), unwrap_or(nv_push(field(g, src), a), field(g, src)), unwrap_or(nv_push(field(g, dst), b), field(g, dst)),
            unwrap_or(fv_concat(field(g, w), unwrap_or(fv_new(1, weight), fv_empty())), field(g, w)))
    fn add_undirected(g: Graph, a: Nat, b: Nat, weight: Float) -> Graph = add_edge(add_edge(g, a, b, weight), b, a, weight)
    private fn edges_loop(g: Graph, xs: List<Nat>, ys: List<Nat>) -> Graph decreasing xs =
        if_then_else(is_empty(xs), g, match head(xs) { none => g, some(a) => match head(ys) { none => g, some(b) => edges_loop(add_edge(g, a, b, 1.0), tail(xs), tail(ys)) } })
    // unit-weight directed graph from parallel endpoint lists
    fn from_edges(n: Nat, xs: List<Nat>, ys: List<Nat>) -> Graph = edges_loop(graph(n), xs, ys)
    fn is_valid(g: Graph) -> Bool = and(nat_eq(edge_count(g), fv_len(field(g, w))),
        and(fold(fv_to_list(field(g, w)), true, fun(ok: Bool, x: Float, and(ok, not(fl_is_nan(x))))), is_some(gr_components(field(g, n), field(g, src), field(g, dst)))))

    private fn nbr_loop(k: Nat, g: Graph, v: Nat, i: Nat, acc: List<Nat>) -> List<Nat> decreasing k =
        if_then_else(nat_eq(k, 0), acc,
            nbr_loop(pred(k), g, v, i + 1, if_then_else(nat_eq(unwrap_or(nv_get(field(g, src), i), field(g, n)), v), cons(unwrap_or(nv_get(field(g, dst), i), 0), acc), acc)))
    // successors of v in reverse edge order
    fn neighbors(g: Graph, v: Nat) -> List<Nat> = nbr_loop(edge_count(g), g, v, 0, empty_list(Nat))
    fn out_degree(g: Graph, v: Nat) -> Nat = length(neighbors(g, v))
    private fn indeg_loop(k: Nat, g: Graph, v: Nat, i: Nat, acc: Nat) -> Nat decreasing k =
        if_then_else(nat_eq(k, 0), acc, indeg_loop(pred(k), g, v, i + 1, if_then_else(nat_eq(unwrap_or(nv_get(field(g, dst), i), field(g, n)), v), acc + 1, acc)))
    fn in_degree(g: Graph, v: Nat) -> Nat = indeg_loop(edge_count(g), g, v, 0, 0)

    fn bfs(g: Graph, s: Nat) -> Option<NatVec> = gr_bfs(field(g, n), field(g, src), field(g, dst), s)
    fn dfs(g: Graph, s: Nat) -> Option<NatVec> = gr_dfs(field(g, n), field(g, src), field(g, dst), s)
    fn reachable(g: Graph, s: Nat, t: Nat) -> Bool = match bfs(g, s) { none => false, some(d) => match nv_get(d, t) { none => false, some(x) => field(g, n) > x } }
    fn hops(g: Graph, s: Nat, t: Nat) -> Option<Nat> = match bfs(g, s) { none => none(Nat), some(d) => match nv_get(d, t) { none => none(Nat), some(x) => if_then_else(field(g, n) > x, some(x), none(Nat)) } }
    fn toposort(g: Graph) -> Option<NatVec> = gr_toposort(field(g, n), field(g, src), field(g, dst))
    fn is_dag(g: Graph) -> Bool = is_some(toposort(g))
    fn components(g: Graph) -> Option<NatVec> = gr_components(field(g, n), field(g, src), field(g, dst))
    fn component_count(g: Graph) -> Nat = match components(g) { none => 0, some(c) => match nv_max(c) { none => 0, some(m) => m + 1 } }
    fn is_connected(g: Graph) -> Bool = nat_eq(component_count(g), 1)
    fn scc(g: Graph) -> Option<NatVec> = gr_scc(field(g, n), field(g, src), field(g, dst))
    fn scc_count(g: Graph) -> Nat = match scc(g) { none => 0, some(c) => match nv_max(c) { none => 0, some(m) => m + 1 } }
    fn bipartition(g: Graph) -> Option<NatVec> = gr_bipartite(field(g, n), field(g, src), field(g, dst))
    fn is_bipartite(g: Graph) -> Bool = is_some(bipartition(g))
    fn distances(g: Graph, s: Nat) -> Option<FloatVec> = gr_dijkstra(field(g, n), field(g, src), field(g, dst), field(g, w), s)
    fn distances_signed(g: Graph, s: Nat) -> Option<FloatVec> = gr_bellman_ford(field(g, n), field(g, src), field(g, dst), field(g, w), s)
    fn all_pairs(g: Graph) -> Option<FloatVec> = gr_floyd(field(g, n), field(g, src), field(g, dst), field(g, w))
    fn has_negative_cycle(g: Graph, s: Nat) -> Bool = and(and(is_valid(g), field(g, n) > s), is_none(distances_signed(g, s)))
    fn mst_edges(g: Graph) -> Option<NatVec> = gr_mst(field(g, n), field(g, src), field(g, dst), field(g, w))
    fn mst_weight(g: Graph) -> Option<Float> =
        match mst_edges(g) { none => none(Float), some(es) => some(fold(nv_to_list(es), 0.0, fun(acc: Float, e: Nat, fl_add(acc, unwrap_or(fv_get(field(g, w), e), 0.0))))) }
    // maximum flow, capacities per edge
    fn max_flow(g: Graph, capacity: NatVec, s: Nat, t: Nat) -> Option<Nat> = gr_maxflow(field(g, n), field(g, src), field(g, dst), capacity, s, t)

    // Grow an acyclic parent tree on shortest-path edges. Assign each vertex once,
    // so zero-weight cycles cannot trap reconstruction.
    private fn tree_pass(k: Nat, g: Graph, d: FloatVec, i: Nat, parents: NatVec) -> NatVec decreasing k =
        if_then_else(nat_eq(k, 0), parents,
            let a = unwrap_or(nv_get(field(g, src), i), field(g, n)) in
            let b = unwrap_or(nv_get(field(g, dst), i), field(g, n)) in
            let next = if_then_else(and(field(g, n) > unwrap_or(nv_get(parents, a), field(g, n)),
                and(nat_eq(unwrap_or(nv_get(parents, b), field(g, n)), field(g, n)),
                    fl_eq(fl_add(unwrap_or(fv_get(d, a), fl_lit("inf")), unwrap_or(fv_get(field(g, w), i), fl_lit("nan"))), unwrap_or(fv_get(d, b), fl_lit("nan"))))),
                unwrap_or(nv_set(parents, b, a), parents), parents) in
            tree_pass(pred(k), g, d, i + 1, next))
    private fn tree_fill(k: Nat, g: Graph, d: FloatVec, t: Nat, parents: NatVec) -> NatVec decreasing k =
        if_then_else(nat_eq(k, 0), parents,
            if_then_else(field(g, n) > unwrap_or(nv_get(parents, t), field(g, n)), parents,
                tree_fill(pred(k), g, d, t, tree_pass(edge_count(g), g, d, 0, parents))))
    private fn walk_back(k: Nat, parents: NatVec, s: Nat, v: Nat, acc: List<Nat>) -> Option<Route> decreasing k =
        if_then_else(nat_eq(v, s), some(Route(cons(s, acc))),
            if_then_else(nat_eq(k, 0), none(Route),
                match nv_get(parents, v) { none => none(Route), some(u) => walk_back(pred(k), parents, s, u, cons(v, acc)) }))
    // a shortest path s ... t by non-negative weights
    fn shortest_path(g: Graph, s: Nat, t: Nat) -> Option<Route> =
        match distances(g, s) { none => none(Route), some(d) =>
            if_then_else(fl_is_finite(unwrap_or(fv_get(d, t), fl_lit("inf"))),
                match nv_new(field(g, n), field(g, n)) { none => none(Route), some(ps) =>
                    let parents = tree_fill(field(g, n), g, d, t, unwrap_or(nv_set(ps, s, s), ps)) in
                    walk_back(field(g, n), parents, s, t, empty_list(Nat)) }, none(Route)) }
    fn path_weight(g: Graph, s: Nat, t: Nat) -> Option<Float> =
        match distances(g, s) { none => none(Float), some(d) => match fv_get(d, t) { none => none(Float), some(x) => if_then_else(fl_is_finite(x), some(x), none(Float)) } }

    theorem two_cycle_is_not_a_dag: is_none(gr_toposort(2, unwrap_or(nv_from_list(list(0, 1)), nv_empty()), unwrap_or(nv_from_list(list(1, 0)), nv_empty())))
}
