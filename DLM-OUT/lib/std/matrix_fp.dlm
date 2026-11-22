// Ordinary DLM reference matrices over prime fields p<=251. Public axes <=16.
// Internal basis augmentation has <=16 rows, <=32 columns and <=512 cells.
module std.matrix_fp
import std.prime

theory StdMatrixFp extends StdPrime {
    record FpMatrix(prime: Nat, rows: Nat, cols: Nat, entries: List<Nat>)
    record FpReduced(matrix: FpMatrix, pivots: List<Nat>)
    private fn mp_cells(n: Nat, i: Nat, f: Fn<Nat, Nat>) -> List<Nat> decreasing n fuel 1024 =
        if_then_else(nat_eq(n, 0), empty_list(Nat), cons(f(i), mp_cells(pred(n), i + 1, f)))
    private fn mp_build(p: Nat, r: Nat, c: Nat, f: Fn<Nat, Nat>) -> FpMatrix = FpMatrix(p, r, c, mp_cells(mul(r, c), 0, f))
    private fn mp_get(a: FpMatrix, r: Nat, c: Nat) -> Nat = unwrap_or(index(field(a, entries), mul(r, field(a, cols)) + c), 0)
    private fn mp_zero(p: Nat, r: Nat, c: Nat) -> FpMatrix = mp_build(p, r, c, fun(i: Nat, 0))
    private fn mp_plus(p: Nat, a: Nat, b: Nat) -> Nat = mod(a + b, p)
    private fn mp_minus(p: Nat, a: Nat, b: Nat) -> Nat = mod(monus(a + p, b), p)
    private fn mp_times(p: Nat, a: Nat, b: Nat) -> Nat = mod(mul(a, b), p)
    private fn mp_power(n: Nat, p: Nat, a: Nat, acc: Nat) -> Nat decreasing n fuel 512 =
        if_then_else(nat_eq(n, 0), acc, mp_power(pred(n), p, a, mp_times(p, acc, a)))
    private fn mp_has(xs: List<Nat>, v: Nat) -> Bool decreasing xs =
        if_then_else(is_empty(xs), false, if_then_else(nat_eq(unwrap_or(head(xs), 0), v), true, mp_has(tail(xs), v)))
    private fn mp_pos(xs: List<Nat>, v: Nat, i: Nat) -> Option<Nat> decreasing xs =
        if_then_else(is_empty(xs), none(Nat), if_then_else(nat_eq(unwrap_or(head(xs), 0), v), some(i), mp_pos(tail(xs), v, i + 1)))
    fn pmat_valid(a: FpMatrix) -> Bool =
        if_then_else(or(field(a, rows) > 16, field(a, cols) > 16), false,
            if_then_else(not(fp_supported_prime(field(a, prime))), false,
                and(nat_eq(length(field(a, entries)), mul(field(a, rows), field(a, cols))), fold(field(a, entries), true, fun(acc: Bool, v: Nat, and(acc, field(a, prime) > v))))))
    private fn mp_pair(a: FpMatrix, b: FpMatrix) -> Bool = and(and(pmat_valid(a), pmat_valid(b)), nat_eq(field(a, prime), field(b, prime)))
    fn pmat_new(p: Nat, r: Nat, c: Nat, entries: List<Nat>) -> Result<FpMatrix, Text> =
        let a = FpMatrix(p, r, c, entries) in if_then_else(pmat_valid(a), ok(a, Text), err("invalid prime matrix or axis budget", FpMatrix))
    fn pmat_zero(p: Nat, r: Nat, c: Nat) -> Result<FpMatrix, Text> =
        if_then_else(or(or(r > 16, c > 16), not(fp_supported_prime(p))), err("invalid prime or matrix axis budget", FpMatrix), ok(mp_zero(p, r, c), Text))
    fn pmat_identity(p: Nat, n: Nat) -> Result<FpMatrix, Text> =
        if_then_else(or(n > 16, not(fp_supported_prime(p))), err("invalid prime or matrix axis budget", FpMatrix), ok(mp_build(p, n, n, fun(i: Nat, if_then_else(nat_eq(div(i, n), mod(i, n)), 1, 0))), Text))
    fn pmat_at(a: FpMatrix, r: Nat, c: Nat) -> Result<Nat, Text> =
        if_then_else(not(pmat_valid(a)), err("invalid prime matrix", Nat), if_then_else(or(not(field(a, rows) > r), not(field(a, cols) > c)), err("prime matrix index out of range", Nat), ok(mp_get(a, r, c), Text)))
    private fn mp_transpose(a: FpMatrix) -> FpMatrix =
        mp_build(field(a, prime), field(a, cols), field(a, rows), fun(i: Nat, mp_get(a, mod(i, field(a, rows)), div(i, field(a, rows)))))
    fn pmat_transpose(a: FpMatrix) -> Result<FpMatrix, Text> = if_then_else(pmat_valid(a), ok(mp_transpose(a), Text), err("invalid prime matrix", FpMatrix))
    fn pmat_add(a: FpMatrix, b: FpMatrix) -> Result<FpMatrix, Text> =
        if_then_else(not(mp_pair(a, b)), err("invalid matrix or coefficient field mismatch", FpMatrix),
            if_then_else(not(and(nat_eq(field(a, rows), field(b, rows)), nat_eq(field(a, cols), field(b, cols)))), err("prime addition shape mismatch", FpMatrix),
                ok(mp_build(field(a, prime), field(a, rows), field(a, cols), fun(i: Nat, mp_plus(field(a, prime), unwrap_or(index(field(a, entries), i), 0), unwrap_or(index(field(b, entries), i), 0)))), Text)))
    private fn mp_dot(n: Nat, a: FpMatrix, b: FpMatrix, r: Nat, c: Nat, k: Nat) -> Nat decreasing n =
        if_then_else(nat_eq(n, 0), 0, mp_plus(field(a, prime), mp_times(field(a, prime), mp_get(a, r, k), mp_get(b, k, c)), mp_dot(pred(n), a, b, r, c, k + 1)))
    private fn mp_mul(a: FpMatrix, b: FpMatrix) -> FpMatrix =
        mp_build(field(a, prime), field(a, rows), field(b, cols), fun(i: Nat, mp_dot(field(a, cols), a, b, div(i, field(b, cols)), mod(i, field(b, cols)), 0)))
    fn pmat_mul(a: FpMatrix, b: FpMatrix) -> Result<FpMatrix, Text> =
        if_then_else(not(mp_pair(a, b)), err("invalid matrix or coefficient field mismatch", FpMatrix), if_then_else(nat_eq(field(a, cols), field(b, rows)), ok(mp_mul(a, b), Text), err("prime multiplication shape mismatch", FpMatrix)))
    fn pmat_is_zero(a: FpMatrix) -> Bool = if_then_else(pmat_valid(a), fold(field(a, entries), true, fun(acc: Bool, v: Nat, and(acc, nat_eq(v, 0)))), false)
    private fn mp_find(n: Nat, a: FpMatrix, r: Nat, c: Nat) -> Option<Nat> decreasing n =
        if_then_else(nat_eq(n, 0), none(Nat), if_then_else(not(nat_eq(mp_get(a, r, c), 0)), some(r), mp_find(pred(n), a, r + 1, c)))
    private fn mp_swap(a: FpMatrix, r: Nat, p: Nat) -> FpMatrix =
        mp_build(field(a, prime), field(a, rows), field(a, cols), fun(i: Nat,
            let row = div(i, field(a, cols)) in mp_get(a, if_then_else(nat_eq(row, r), p, if_then_else(nat_eq(row, p), r, row)), mod(i, field(a, cols)))))
    private fn mp_normalize(a: FpMatrix, r: Nat, c: Nat) -> FpMatrix =
        let inverse = mp_power(monus(field(a, prime), 2), field(a, prime), mp_get(a, r, c), 1) in
        mp_build(field(a, prime), field(a, rows), field(a, cols), fun(i: Nat, if_then_else(nat_eq(div(i, field(a, cols)), r), mp_times(field(a, prime), unwrap_or(index(field(a, entries), i), 0), inverse), unwrap_or(index(field(a, entries), i), 0))))
    private fn mp_clear(a: FpMatrix, r: Nat, c: Nat) -> FpMatrix =
        mp_build(field(a, prime), field(a, rows), field(a, cols), fun(i: Nat,
            let row = div(i, field(a, cols)) in let col = mod(i, field(a, cols)) in
            if_then_else(nat_eq(row, r), mp_get(a, row, col), mp_minus(field(a, prime), mp_get(a, row, col), mp_times(field(a, prime), mp_get(a, row, c), mp_get(a, r, col))))))
    private fn mp_reduce(n: Nat, a: FpMatrix, r: Nat, c: Nat) -> FpReduced decreasing n =
        if_then_else(nat_eq(n, 0), FpReduced(a, empty_list(Nat)),
            if_then_else(nat_eq(r, field(a, rows)), FpReduced(a, empty_list(Nat)),
                match mp_find(monus(field(a, rows), r), a, r, c) {
                    none => mp_reduce(pred(n), a, r, c + 1),
                    some(p) => let rest = mp_reduce(pred(n), mp_clear(mp_normalize(mp_swap(a, r, p), r, c), r, c), r + 1, c + 1) in FpReduced(field(rest, matrix), cons(c, field(rest, pivots)))
                }))
    fn pmat_rref(a: FpMatrix) -> Result<FpReduced, Text> = if_then_else(pmat_valid(a), ok(mp_reduce(field(a, cols), a, 0, 0), Text), err("invalid prime matrix", FpReduced))
    fn pmat_rank(a: FpMatrix) -> Result<Nat, Text> = if_then_else(pmat_valid(a), ok(length(field(mp_reduce(field(a, cols), a, 0, 0), pivots)), Text), err("invalid prime matrix", Nat))
    private fn mp_free(n: Nat, c: Nat, pivots: List<Nat>) -> List<Nat> decreasing n =
        if_then_else(nat_eq(n, 0), empty_list(Nat), if_then_else(mp_has(pivots, c), mp_free(pred(n), c + 1, pivots), cons(c, mp_free(pred(n), c + 1, pivots))))
    private fn mp_kcell(reduced: FpReduced, free: List<Nat>, r: Nat, c: Nat) -> Nat =
        let f = unwrap_or(index(free, c), 0) in let a = field(reduced, matrix) in
        if_then_else(nat_eq(r, f), 1, match mp_pos(field(reduced, pivots), r, 0) { none => 0, some(p) => mp_minus(field(a, prime), 0, mp_get(a, p, f)) })
    private fn mp_kernel(a: FpMatrix) -> FpMatrix =
        let reduced = mp_reduce(field(a, cols), a, 0, 0) in let free = mp_free(field(a, cols), 0, field(reduced, pivots)) in
        mp_build(field(a, prime), field(a, cols), length(free), fun(i: Nat, mp_kcell(reduced, free, div(i, length(free)), mod(i, length(free)))))
    fn pmat_kernel(a: FpMatrix) -> Result<FpMatrix, Text> = if_then_else(pmat_valid(a), ok(mp_kernel(a), Text), err("invalid prime matrix", FpMatrix))
    private fn mp_columns(a: FpMatrix, columns: List<Nat>) -> FpMatrix =
        mp_build(field(a, prime), field(a, rows), length(columns), fun(i: Nat, mp_get(a, div(i, length(columns)), unwrap_or(index(columns, mod(i, length(columns))), 0))))
    fn pmat_columns(a: FpMatrix, columns: List<Nat>) -> Result<FpMatrix, Text> =
        if_then_else(not(pmat_valid(a)), err("invalid prime matrix", FpMatrix), if_then_else(length(columns) > 16, err("prime selection axis budget", FpMatrix),
            if_then_else(fold(columns, true, fun(acc: Bool, c: Nat, and(acc, field(a, cols) > c))), ok(mp_columns(a, columns), Text), err("selected column out of range", FpMatrix))))
    fn pmat_image(a: FpMatrix) -> Result<FpMatrix, Text> = if_then_else(pmat_valid(a), ok(mp_columns(a, field(mp_reduce(field(a, cols), a, 0, 0), pivots)), Text), err("invalid prime matrix", FpMatrix))
    private fn mp_hstack(a: FpMatrix, b: FpMatrix) -> FpMatrix =
        let cols = field(a, cols) + field(b, cols) in
        mp_build(field(a, prime), field(a, rows), cols, fun(i: Nat, let r = div(i, cols) in let c = mod(i, cols) in if_then_else(field(a, cols) > c, mp_get(a, r, c), mp_get(b, r, monus(c, field(a, cols))))))
    fn pmat_hstack(a: FpMatrix, b: FpMatrix) -> Result<FpMatrix, Text> =
        if_then_else(not(mp_pair(a, b)), err("invalid matrix or coefficient field mismatch", FpMatrix),
            if_then_else(or(not(nat_eq(field(a, rows), field(b, rows))), field(a, cols) + field(b, cols) > 16), err("prime concatenation shape or axis budget", FpMatrix), ok(mp_hstack(a, b), Text)))
    private fn mp_after(xs: List<Nat>, offset: Nat) -> List<Nat> decreasing xs =
        if_then_else(is_empty(xs), empty_list(Nat), let p = unwrap_or(head(xs), 0) in if_then_else(not(offset > p), cons(monus(p, offset), mp_after(tail(xs), offset)), mp_after(tail(xs), offset)))
    fn pmat_extend_basis(span: FpMatrix, candidates: FpMatrix) -> Result<FpMatrix, Text> =
        if_then_else(not(mp_pair(span, candidates)), err("invalid matrix or coefficient field mismatch", FpMatrix),
            if_then_else(not(nat_eq(field(span, rows), field(candidates, rows))), err("basis extension shape mismatch", FpMatrix),
                let joined = mp_hstack(span, candidates) in let pivots = field(mp_reduce(field(joined, cols), joined, 0, 0), pivots) in
                ok(mp_columns(candidates, mp_after(pivots, field(span, cols))), Text)))
    theorem identity_three_rank: nat_eq(unwrap_or(pmat_rank(FpMatrix(3, 2, 2, list(1, 0, 0, 1))), 999), 2)
    theorem reject_composite: not(pmat_valid(FpMatrix(4, 0, 0, empty_list(Nat))))
}
