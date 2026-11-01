// std.matrix_f2 — bounded reference matrices written in ordinary DLM.
// Public operations validate records; helpers are private. Axis limit: 32.
module std.matrix_f2
import std.f2

theory StdMatrixF2 extends StdF2 {
    record F2Matrix(rows: Nat, cols: Nat, entries: List<Bool>)
    record F2Reduced(matrix: F2Matrix, pivots: List<Nat>)

    private fn mf2_bits(n: Nat, i: Nat, f: Fn<Nat, Bool>) -> List<Bool> decreasing n fuel 4096 =
        if_then_else(nat_eq(n, 0), empty_list(Bool), cons(f(i), mf2_bits(pred(n), i + 1, f)))
    private fn mf2_build(r: Nat, c: Nat, f: Fn<Nat, Bool>) -> F2Matrix = F2Matrix(r, c, mf2_bits(mul(r, c), 0, f))
    private fn mf2_get(a: F2Matrix, r: Nat, c: Nat) -> Bool = unwrap_or(index(field(a, entries), mul(r, field(a, cols)) + c), false)
    private fn mf2_zero(r: Nat, c: Nat) -> F2Matrix = mf2_build(r, c, fun(i: Nat, false))
    private fn mf2_has(xs: List<Nat>, v: Nat) -> Bool decreasing xs =
        if_then_else(is_empty(xs), false, if_then_else(nat_eq(unwrap_or(head(xs), 0), v), true, mf2_has(tail(xs), v)))
    private fn mf2_pos(xs: List<Nat>, v: Nat, i: Nat) -> Option<Nat> decreasing xs =
        if_then_else(is_empty(xs), none(Nat), if_then_else(nat_eq(unwrap_or(head(xs), 0), v), some(i), mf2_pos(tail(xs), v, i + 1)))

    fn mat_valid(a: F2Matrix) -> Bool =
        if_then_else(or(field(a, rows) > 32, field(a, cols) > 32), false, nat_eq(length(field(a, entries)), mul(field(a, rows), field(a, cols))))
    fn mat_new(r: Nat, c: Nat, entries: List<Bool>) -> Result<F2Matrix, Text> =
        let a = F2Matrix(r, c, entries) in if_then_else(mat_valid(a), ok(a, Text), err("invalid F2 matrix shape or axis budget", F2Matrix))
    fn mat_zero(r: Nat, c: Nat) -> Result<F2Matrix, Text> =
        if_then_else(or(r > 32, c > 32), err("F2 matrix axis budget exceeded", F2Matrix), ok(mf2_zero(r, c), Text))
    fn mat_identity(n: Nat) -> Result<F2Matrix, Text> =
        if_then_else(n > 32, err("F2 matrix axis budget exceeded", F2Matrix), ok(mf2_build(n, n, fun(i: Nat, nat_eq(div(i, n), mod(i, n)))), Text))
    fn mat_at(a: F2Matrix, r: Nat, c: Nat) -> Result<Bool, Text> =
        if_then_else(not(mat_valid(a)), err("invalid F2 matrix", Bool), if_then_else(or(not(field(a, rows) > r), not(field(a, cols) > c)), err("F2 matrix index out of range", Bool), ok(mf2_get(a, r, c), Text)))
    private fn mf2_transpose(a: F2Matrix) -> F2Matrix =
        mf2_build(field(a, cols), field(a, rows), fun(i: Nat, mf2_get(a, mod(i, field(a, rows)), div(i, field(a, rows)))))
    fn mat_transpose(a: F2Matrix) -> Result<F2Matrix, Text> =
        if_then_else(mat_valid(a), ok(mf2_transpose(a), Text), err("invalid F2 matrix", F2Matrix))
    fn mat_add(a: F2Matrix, b: F2Matrix) -> Result<F2Matrix, Text> =
        if_then_else(not(and(mat_valid(a), mat_valid(b))), err("invalid F2 matrix", F2Matrix),
            if_then_else(not(and(nat_eq(field(a, rows), field(b, rows)), nat_eq(field(a, cols), field(b, cols)))), err("F2 addition shape mismatch", F2Matrix),
                ok(mf2_build(field(a, rows), field(a, cols), fun(i: Nat, f2_add(unwrap_or(index(field(a, entries), i), false), unwrap_or(index(field(b, entries), i), false)))), Text)))
    private fn mf2_dot(n: Nat, a: F2Matrix, b: F2Matrix, r: Nat, c: Nat, k: Nat) -> Bool decreasing n =
        if_then_else(nat_eq(n, 0), false, f2_add(f2_mul(mf2_get(a, r, k), mf2_get(b, k, c)), mf2_dot(pred(n), a, b, r, c, k + 1)))
    private fn mf2_mul(a: F2Matrix, b: F2Matrix) -> F2Matrix =
        mf2_build(field(a, rows), field(b, cols), fun(i: Nat, mf2_dot(field(a, cols), a, b, div(i, field(b, cols)), mod(i, field(b, cols)), 0)))
    fn mat_mul(a: F2Matrix, b: F2Matrix) -> Result<F2Matrix, Text> =
        if_then_else(not(and(mat_valid(a), mat_valid(b))), err("invalid F2 matrix", F2Matrix),
            if_then_else(nat_eq(field(a, cols), field(b, rows)), ok(mf2_mul(a, b), Text), err("F2 multiplication shape mismatch", F2Matrix)))
    private fn mf2_is_zero(a: F2Matrix) -> Bool = fold(field(a, entries), true, fun(acc: Bool, v: Bool, and(acc, not(v))))
    fn mat_is_zero(a: F2Matrix) -> Bool = if_then_else(mat_valid(a), mf2_is_zero(a), false)
    fn mat_equal(a: F2Matrix, b: F2Matrix) -> Bool =
        if_then_else(not(and(mat_valid(a), mat_valid(b))), false,
            if_then_else(and(nat_eq(field(a, rows), field(b, rows)), nat_eq(field(a, cols), field(b, cols))),
                mf2_is_zero(mf2_build(field(a, rows), field(a, cols), fun(i: Nat, f2_add(unwrap_or(index(field(a, entries), i), false), unwrap_or(index(field(b, entries), i), false))))), false))

    private fn mf2_find(n: Nat, a: F2Matrix, r: Nat, c: Nat) -> Option<Nat> decreasing n =
        if_then_else(nat_eq(n, 0), none(Nat), if_then_else(mf2_get(a, r, c), some(r), mf2_find(pred(n), a, r + 1, c)))
    private fn mf2_swap(a: F2Matrix, r: Nat, p: Nat) -> F2Matrix =
        mf2_build(field(a, rows), field(a, cols), fun(i: Nat,
            let row = div(i, field(a, cols)) in
            mf2_get(a, if_then_else(nat_eq(row, r), p, if_then_else(nat_eq(row, p), r, row)), mod(i, field(a, cols)))))
    private fn mf2_clear(a: F2Matrix, r: Nat, c: Nat) -> F2Matrix =
        mf2_build(field(a, rows), field(a, cols), fun(i: Nat,
            let row = div(i, field(a, cols)) in let col = mod(i, field(a, cols)) in
            if_then_else(and(not(nat_eq(row, r)), mf2_get(a, row, c)), f2_add(mf2_get(a, row, col), mf2_get(a, r, col)), mf2_get(a, row, col))))
    private fn mf2_reduce(n: Nat, a: F2Matrix, r: Nat, c: Nat) -> F2Reduced decreasing n =
        if_then_else(nat_eq(n, 0), F2Reduced(a, empty_list(Nat)),
            if_then_else(nat_eq(r, field(a, rows)), F2Reduced(a, empty_list(Nat)),
                match mf2_find(monus(field(a, rows), r), a, r, c) {
                    none => mf2_reduce(pred(n), a, r, c + 1),
                    some(p) => let rest = mf2_reduce(pred(n), mf2_clear(mf2_swap(a, r, p), r, c), r + 1, c + 1) in F2Reduced(field(rest, matrix), cons(c, field(rest, pivots)))
                }))
    private fn mf2_rank(a: F2Matrix) -> Nat = length(field(mf2_reduce(field(a, cols), a, 0, 0), pivots))
    fn mat_rref(a: F2Matrix) -> Result<F2Reduced, Text> =
        if_then_else(mat_valid(a), ok(mf2_reduce(field(a, cols), a, 0, 0), Text), err("invalid F2 matrix", F2Reduced))
    fn mat_rank(a: F2Matrix) -> Result<Nat, Text> =
        if_then_else(mat_valid(a), ok(mf2_rank(a), Text), err("invalid F2 matrix", Nat))
    private fn mf2_free(n: Nat, c: Nat, pivots: List<Nat>) -> List<Nat> decreasing n =
        if_then_else(nat_eq(n, 0), empty_list(Nat),
            if_then_else(mf2_has(pivots, c), mf2_free(pred(n), c + 1, pivots), cons(c, mf2_free(pred(n), c + 1, pivots))))
    private fn mf2_kbit(reduced: F2Reduced, free: List<Nat>, r: Nat, c: Nat) -> Bool =
        let f = unwrap_or(index(free, c), 0) in
        if_then_else(nat_eq(r, f), true, match mf2_pos(field(reduced, pivots), r, 0) { none => false, some(p) => mf2_get(field(reduced, matrix), p, f) })
    private fn mf2_kernel(a: F2Matrix) -> F2Matrix =
        let reduced = mf2_reduce(field(a, cols), a, 0, 0) in
        let free = mf2_free(field(a, cols), 0, field(reduced, pivots)) in
        mf2_build(field(a, cols), length(free), fun(i: Nat, mf2_kbit(reduced, free, div(i, length(free)), mod(i, length(free)))))
    fn mat_kernel(a: F2Matrix) -> Result<F2Matrix, Text> =
        if_then_else(mat_valid(a), ok(mf2_kernel(a), Text), err("invalid F2 matrix", F2Matrix))
    private fn mf2_columns(a: F2Matrix, columns: List<Nat>) -> F2Matrix =
        mf2_build(field(a, rows), length(columns), fun(i: Nat, mf2_get(a, div(i, length(columns)), unwrap_or(index(columns, mod(i, length(columns))), 0))))
    fn mat_columns(a: F2Matrix, columns: List<Nat>) -> Result<F2Matrix, Text> =
        if_then_else(not(mat_valid(a)), err("invalid F2 matrix", F2Matrix),
            if_then_else(length(columns) > 32, err("F2 column selection axis budget", F2Matrix),
                if_then_else(fold(columns, true, fun(acc: Bool, c: Nat, and(acc, field(a, cols) > c))), ok(mf2_columns(a, columns), Text), err("F2 selected column out of range", F2Matrix))))
    private fn mf2_image(a: F2Matrix) -> F2Matrix = mf2_columns(a, field(mf2_reduce(field(a, cols), a, 0, 0), pivots))
    fn mat_image(a: F2Matrix) -> Result<F2Matrix, Text> =
        if_then_else(mat_valid(a), ok(mf2_image(a), Text), err("invalid F2 matrix", F2Matrix))
    private fn mf2_hstack(a: F2Matrix, b: F2Matrix) -> F2Matrix =
        let cols = field(a, cols) + field(b, cols) in
        mf2_build(field(a, rows), cols, fun(i: Nat,
            let r = div(i, cols) in let c = mod(i, cols) in
            if_then_else(field(a, cols) > c, mf2_get(a, r, c), mf2_get(b, r, monus(c, field(a, cols))))))
    fn mat_hstack(a: F2Matrix, b: F2Matrix) -> Result<F2Matrix, Text> =
        if_then_else(not(and(mat_valid(a), mat_valid(b))), err("invalid F2 matrix", F2Matrix),
            if_then_else(or(not(nat_eq(field(a, rows), field(b, rows))), field(a, cols) + field(b, cols) > 32), err("F2 concatenation shape or axis budget", F2Matrix), ok(mf2_hstack(a, b), Text)))
    private fn mf2_after(xs: List<Nat>, offset: Nat) -> List<Nat> decreasing xs =
        if_then_else(is_empty(xs), empty_list(Nat),
            let p = unwrap_or(head(xs), 0) in
            if_then_else(not(offset > p), cons(monus(p, offset), mf2_after(tail(xs), offset)), mf2_after(tail(xs), offset)))
    // Internal augmentation has at most 32 rows and 64 columns. Only selected
    // original candidate columns leave this operation (at most 32 columns).
    fn mat_extend_basis(span: F2Matrix, candidates: F2Matrix) -> Result<F2Matrix, Text> =
        if_then_else(not(and(mat_valid(span), mat_valid(candidates))), err("invalid F2 matrix", F2Matrix),
            if_then_else(not(nat_eq(field(span, rows), field(candidates, rows))), err("F2 basis extension shape mismatch", F2Matrix),
                let joined = mf2_hstack(span, candidates) in
                let pivots = field(mf2_reduce(field(joined, cols), joined, 0, 0), pivots) in
                ok(mf2_columns(candidates, mf2_after(pivots, field(span, cols))), Text)))
    fn mat_tensor(a: F2Matrix, b: F2Matrix) -> Result<F2Matrix, Text> =
        if_then_else(not(and(mat_valid(a), mat_valid(b))), err("invalid F2 matrix", F2Matrix),
            let rows = mul(field(a, rows), field(b, rows)) in let cols = mul(field(a, cols), field(b, cols)) in
            if_then_else(or(rows > 32, cols > 32), err("F2 tensor axis budget exceeded", F2Matrix),
                ok(mf2_build(rows, cols, fun(i: Nat,
                    let r = div(i, cols) in let c = mod(i, cols) in
                    f2_mul(mf2_get(a, div(r, field(b, rows)), div(c, field(b, cols))), mf2_get(b, mod(r, field(b, rows)), mod(c, field(b, cols)))))), Text)))
    // Closed computational specifications; legacy finite evaluation certificates.
    theorem empty_matrix_rank: nat_eq(unwrap_or(mat_rank(F2Matrix(0, 0, empty_list(Bool))), 999), 0)
    theorem identity_two_rank: nat_eq(unwrap_or(mat_rank(F2Matrix(2, 2, list(true, false, false, true))), 999), 2)
    theorem reject_oversized_matrix: not(mat_valid(F2Matrix(33, 0, empty_list(Bool))))

}
