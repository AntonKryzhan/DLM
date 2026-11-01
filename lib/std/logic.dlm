// Propositional logic and SAT encodings (v0.119, docs/LOGIC_TOPOLOGY.md): CNF construction, cardinality
// constraints, Tseitin gates, and solvers for n-queens, sudoku and graph colouring on the native CDCL
// solver (sat_solve). Literal 2v is x_v, 2v + 1 is ¬x_v.
module std.logic
import std.number_theory

theory StdLogic extends StdNumberTheory {
    record Cnf(vars: Nat, lits: NatVec, ends: NatVec)
    record Fresh(cnf: Cnf, var: Nat)

    fn pos(v: Nat) -> Nat = mul(2, v)
    fn neg(v: Nat) -> Nat = mul(2, v) + 1
    fn negate(l: Nat) -> Nat = if_then_else(nat_eq(mod(l, 2), 0), l + 1, monus(l, 1))
    fn cnf(vars: Nat) -> Cnf = Cnf(vars, nv_empty(), nv_empty())
    fn clause_count(f: Cnf) -> Nat = nv_len(field(f, ends))
    fn add_clause(f: Cnf, ls: List<Nat>) -> Cnf =
        let lits = fold(ls, field(f, lits), fun(acc: NatVec, l: Nat, unwrap_or(nv_push(acc, l), acc))) in
        Cnf(field(f, vars), lits, unwrap_or(nv_push(field(f, ends), nv_len(lits)), field(f, ends)))
    fn add_clauses(f: Cnf, cs: List<List<Nat>>) -> Cnf = fold(cs, f, add_clause)
    // a new variable
    fn fresh(f: Cnf) -> Fresh = Fresh(Cnf(field(f, vars) + 1, field(f, lits), field(f, ends)), field(f, vars))
    fn sat_model(f: Cnf) -> Option<NatVec> = sat_solve(field(f, lits), field(f, ends), field(f, vars))
    fn status(f: Cnf) -> Nat = sat_status(field(f, lits), field(f, ends), field(f, vars))
    fn satisfiable(f: Cnf) -> Bool = is_some(sat_model(f))
    fn holds(model: NatVec, l: Nat) -> Bool = nat_eq(unwrap_or(nv_get(model, div(l, 2)), 2), if_then_else(nat_eq(mod(l, 2), 0), 1, 0))
    private fn clause_true(model: NatVec, lits: NatVec, a: Nat, b: Nat) -> Bool decreasing b =
        if_then_else(nat_eq(b, 0), false, if_then_else(not(a > monus(b, 1)), or(holds(model, unwrap_or(nv_get(lits, monus(b, 1)), 0)), clause_true(model, lits, a, monus(b, 1))), false))
    private fn check_loop(k: Nat, f: Cnf, model: NatVec, i: Nat, start: Nat) -> Bool decreasing k =
        if_then_else(nat_eq(k, 0), true, let e = unwrap_or(nv_get(field(f, ends), i), 0) in
            if_then_else(clause_true(model, field(f, lits), start, e), check_loop(pred(k), f, model, i + 1, e), false))
    private fn valid_offsets(xs: List<Nat>, prev: Nat, size: Nat) -> Bool decreasing xs =
        if_then_else(is_empty(xs), nat_eq(prev, size), match head(xs) { none => false, some(e) =>
            and(and(not(prev > e), not(e > size)), valid_offsets(tail(xs), e, size)) })
    private fn valid_cnf(f: Cnf) -> Bool =
        if_then_else(field(f, vars) > 4194304, false,
            and(valid_offsets(nv_to_list(field(f, ends)), 0, nv_len(field(f, lits))),
                fold(nv_to_list(field(f, lits)), true, fun(ok: Bool, l: Nat, and(ok, mul(2, field(f, vars)) > l)))))
    // every clause has a true literal under a complete Boolean model
    fn satisfies(f: Cnf, model: NatVec) -> Bool = and(and(valid_cnf(f), nat_eq(nv_len(model), field(f, vars))),
        and(fold(nv_to_list(model), true, fun(ok: Bool, v: Nat, and(ok, 2 > v))), check_loop(clause_count(f), f, model, 0, 0)))

    // ---- cardinality ----
    fn at_least_one(f: Cnf, ls: List<Nat>) -> Cnf = add_clause(f, ls)
    private fn pairs_with(f: Cnf, l: Nat, rest: List<Nat>) -> Cnf = fold(rest, f, fun(g: Cnf, m: Nat, add_clause(g, list(negate(l), negate(m)))))
    private fn amo_loop(f: Cnf, ls: List<Nat>) -> Cnf decreasing ls =
        if_then_else(is_empty(ls), f, match head(ls) { none => f, some(l) => amo_loop(pairs_with(f, l, tail(ls)), tail(ls)) })
    // pairwise at-most-one
    fn at_most_one(f: Cnf, ls: List<Nat>) -> Cnf = amo_loop(f, ls)
    fn exactly_one(f: Cnf, ls: List<Nat>) -> Cnf = at_most_one(at_least_one(f, ls), ls)
    fn implies(f: Cnf, a: Nat, b: Nat) -> Cnf = add_clause(f, list(negate(a), b))
    fn equivalent(f: Cnf, a: Nat, b: Nat) -> Cnf = implies(implies(f, a, b), b, a)

    // ---- Tseitin gates: return the CNF with an output variable defined from inputs ----
    fn gate_and(f: Cnf, a: Nat, b: Nat) -> Fresh =
        let g = fresh(f) in let o = pos(field(g, var)) in
        Fresh(add_clauses(field(g, cnf), list(list(negate(o), a), list(negate(o), b), list(o, negate(a), negate(b)))), field(g, var))
    fn gate_or(f: Cnf, a: Nat, b: Nat) -> Fresh =
        let g = fresh(f) in let o = pos(field(g, var)) in
        Fresh(add_clauses(field(g, cnf), list(list(o, negate(a)), list(o, negate(b)), list(negate(o), a, b))), field(g, var))
    fn gate_xor(f: Cnf, a: Nat, b: Nat) -> Fresh =
        let g = fresh(f) in let o = pos(field(g, var)) in
        Fresh(add_clauses(field(g, cnf), list(list(negate(o), a, b), list(negate(o), negate(a), negate(b)), list(o, negate(a), b), list(o, a, negate(b)))), field(g, var))

    // ---- puzzles ----
    private fn qv(n: Nat, r: Nat, c: Nat) -> Nat = pos(mul(r, n) + c)
    private fn row_lits(n: Nat, r: Nat) -> List<Nat> = map(nv_to_list(unwrap_or(nv_range(0, n), nv_empty())), fun(c: Nat, qv(n, r, c)))
    private fn col_lits(n: Nat, c: Nat) -> List<Nat> = map(nv_to_list(unwrap_or(nv_range(0, n), nv_empty())), fun(r: Nat, qv(n, r, c)))
    private fn diag_pairs(n: Nat, f: Cnf, r: Nat, c: Nat) -> Cnf =
        fold(nv_to_list(unwrap_or(nv_range(r + 1, monus(n, r + 1)), nv_empty())), f, fun(g: Cnf, r2: Nat,
            let d = monus(r2, r) in
            let g1 = if_then_else(n > c + d, add_clause(g, list(negate(qv(n, r, c)), negate(qv(n, r2, c + d)))), g) in
            if_then_else(not(d > c), add_clause(g1, list(negate(qv(n, r, c)), negate(qv(n, r2, monus(c, d))))), g1)))
    fn queens_cnf(n: Nat) -> Cnf =
        let idx = nv_to_list(unwrap_or(nv_range(0, n), nv_empty())) in
        let f1 = fold(idx, cnf(mul(n, n)), fun(f: Cnf, r: Nat, exactly_one(f, row_lits(n, r)))) in
        let f2 = fold(idx, f1, fun(f: Cnf, c: Nat, at_most_one(f, col_lits(n, c)))) in
        fold(idx, f2, fun(f: Cnf, r: Nat, fold(idx, f, fun(g: Cnf, c: Nat, diag_pairs(n, g, r, c)))))
    // column of the queen in each row
    fn queens(n: Nat) -> Option<NatVec> =
        match sat_model(queens_cnf(n)) { none => none(NatVec), some(m) =>
            nv_from_list(map(nv_to_list(unwrap_or(nv_range(0, n), nv_empty())), fun(r: Nat,
                fold(nv_to_list(unwrap_or(nv_range(0, n), nv_empty())), 0, fun(acc: Nat, c: Nat, if_then_else(holds(m, qv(n, r, c)), c, acc)))))) }
    private fn sv(r: Nat, c: Nat, d: Nat) -> Nat = pos(mul(81, 0) + mul(r, 81) + mul(c, 9) + d)
    private fn nine() -> List<Nat> = nv_to_list(unwrap_or(nv_range(0, 9), nv_empty()))
    private fn cell_rules(f: Cnf) -> Cnf = fold(nine(), f, fun(g: Cnf, r: Nat, fold(nine(), g, fun(h: Cnf, c: Nat, exactly_one(h, map(nine(), fun(d: Nat, sv(r, c, d))))))))
    private fn unit_rules(f: Cnf, cells: Fn<Nat, Nat, ProductType<Nat*Nat>>) -> Cnf =
        fold(nine(), f, fun(g: Cnf, u: Nat, fold(nine(), g, fun(h: Cnf, d: Nat, exactly_one(h, map(nine(), fun(k: Nat, let rc = cells(u, k) in sv(fst(rc), snd(rc), d))))))))
    private fn row_cell(u: Nat, k: Nat) -> ProductType<Nat*Nat> = pair(u, k)
    private fn col_cell(u: Nat, k: Nat) -> ProductType<Nat*Nat> = pair(k, u)
    private fn box_cell(u: Nat, k: Nat) -> ProductType<Nat*Nat> = pair(mul(div(u, 3), 3) + div(k, 3), mul(mod(u, 3), 3) + mod(k, 3))
    // 81 digits row by row, 0 for empty; the solved grid with digits 1..9
    fn sudoku(grid: NatVec) -> Option<NatVec> =
        if_then_else(or(not(nat_eq(nv_len(grid), 81)), fold(nv_to_list(grid), false, fun(bad: Bool, d: Nat, or(bad, d > 9)))), none(NatVec),
            let base = unit_rules(unit_rules(unit_rules(cell_rules(cnf(729)), row_cell), col_cell), box_cell) in
            let given = fold(nv_to_list(unwrap_or(nv_range(0, 81), nv_empty())), base, fun(f: Cnf, i: Nat, let d = unwrap_or(nv_get(grid, i), 0) in
                if_then_else(or(nat_eq(d, 0), d > 9), f, add_clause(f, list(sv(div(i, 9), mod(i, 9), monus(d, 1))))))) in
            match sat_model(given) { none => none(NatVec), some(m) =>
                nv_from_list(map(nv_to_list(unwrap_or(nv_range(0, 81), nv_empty())), fun(i: Nat,
                    fold(nine(), 0, fun(acc: Nat, d: Nat, if_then_else(holds(m, sv(div(i, 9), mod(i, 9), d)), d + 1, acc)))))) })
    // proper k-colouring of a graph (undirected use of its edges)
    fn colouring(g: Graph, k: Nat) -> Option<NatVec> =
        let n = field(g, n) in let ks = nv_to_list(unwrap_or(nv_range(0, k), nv_empty())) in
        let cv = fun(v: Nat, c: Nat, pos(mul(v, k) + c)) in
        let f1 = fold(nv_to_list(unwrap_or(nv_range(0, n), nv_empty())), cnf(mul(n, k)), fun(f: Cnf, v: Nat, exactly_one(f, map(ks, fun(c: Nat, cv(v, c)))))) in
        let f2 = fold(nv_to_list(unwrap_or(nv_range(0, edge_count(g)), nv_empty())), f1, fun(f: Cnf, e: Nat,
            let a = unwrap_or(nv_get(field(g, src), e), 0) in let b = unwrap_or(nv_get(field(g, dst), e), 0) in
            fold(ks, f, fun(h: Cnf, c: Nat, add_clause(h, list(negate(cv(a, c)), negate(cv(b, c)))))))) in
        match sat_model(f2) { none => none(NatVec), some(m) =>
            nv_from_list(map(nv_to_list(unwrap_or(nv_range(0, n), nv_empty())), fun(v: Nat, fold(ks, 0, fun(acc: Nat, c: Nat, if_then_else(holds(m, cv(v, c)), c, acc)))))) }
    // pigeonhole: n + 1 pigeons in n holes (unsatisfiable)
    fn pigeonhole(n: Nat) -> Cnf =
        let ps = nv_to_list(unwrap_or(nv_range(0, n + 1), nv_empty())) in let hs = nv_to_list(unwrap_or(nv_range(0, n), nv_empty())) in
        let f1 = fold(ps, cnf(mul(n + 1, n)), fun(f: Cnf, p: Nat, at_least_one(f, map(hs, fun(h: Nat, pos(mul(p, n) + h)))))) in
        fold(hs, f1, fun(f: Cnf, h: Nat, at_most_one(f, map(ps, fun(p: Nat, pos(mul(p, n) + h))))))

    // ---- certificates and counting (v0.121) ----
    // learnt-clause (RUP) certificate of unsatisfiability
    fn refutation(f: Cnf) -> Option<NatVec> = sat_proof(field(f, lits), field(f, ends), field(f, vars))
    // independent check of a certificate against the formula
    fn check_refutation(f: Cnf, proof: NatVec) -> Bool = unwrap_or(sat_check(field(f, lits), field(f, ends), field(f, vars), proof), false)
    // unsatisfiable, with the solver's certificate re-checked by reverse unit propagation
    fn verified_unsat(f: Cnf) -> Bool = match refutation(f) { none => false, some(p) => check_refutation(f, p) }
    // number of satisfying assignments of all variables
    fn model_count(f: Cnf) -> Option<Nat> = sat_count(field(f, lits), field(f, ends), field(f, vars))
    fn refutation_size(p: NatVec) -> Nat = unwrap_or(nv_get(p, 0), 0)
    // entailment f ⊨ clause: f ∧ ¬clause is unsatisfiable (verified)
    fn entails(f: Cnf, clause: List<Nat>) -> Bool = verified_unsat(fold(clause, f, fun(g: Cnf, l: Nat, add_clause(g, list(negate(l))))))

    theorem contradiction_is_unsatisfiable: nat_eq(sat_status(unwrap_or(nv_from_list(list(0, 1)), nv_empty()), unwrap_or(nv_from_list(list(1, 2)), nv_empty()), 1), 1)
}
