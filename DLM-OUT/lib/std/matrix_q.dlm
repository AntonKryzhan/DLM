// Ordinary rational matrices: axes <=8, canonical std.rational 32-bit profile.
// Every scalar error propagates; this is separate from host limb arithmetic.
module std.matrix_q
import std.rational

theory StdMatrixQ extends StdRational {
    record QMatrix(rows: Nat, cols: Nat, entries: List<Rational>)
    record QReduced(matrix: QMatrix, pivots: List<Nat>)
    private fn mq_cells(n: Nat, i: Nat, f: Fn<Nat, Result<Rational, Text>>) -> Result<List<Rational>, Text> decreasing n fuel 256 =
        if_then_else(nat_eq(n, 0), ok(empty_list(Rational), Text),
            match f(i) { err(e) => err(e, List(Rational)), ok(a) => match mq_cells(pred(n), i + 1, f) { err(e) => err(e, List(Rational)), ok(rest) => ok(cons(a, rest), Text) } })
    private fn mq_build(r: Nat, c: Nat, f: Fn<Nat, Result<Rational, Text>>) -> Result<QMatrix, Text> =
        match mq_cells(mul(r, c), 0, f) { err(e) => err(e, QMatrix), ok(values) => ok(QMatrix(r, c, values), Text) }
    private fn mq_get(a: QMatrix, r: Nat, c: Nat) -> Rational = unwrap_or(index(field(a, entries), mul(r, field(a, cols)) + c), Rational(false, 0, 1))
    fn qmat_valid(a: QMatrix) -> Bool =
        if_then_else(or(field(a, rows) > 8, field(a, cols) > 8), false,
            and(nat_eq(length(field(a, entries)), mul(field(a, rows), field(a, cols))), fold(field(a, entries), true, fun(acc: Bool, q: Rational, and(acc, rat_valid(q))))))
    private fn mq_pair(a: QMatrix, b: QMatrix) -> Bool = and(qmat_valid(a), qmat_valid(b))
    fn qmat_new(r: Nat, c: Nat, entries: List<Rational>) -> Result<QMatrix, Text> =
        let a = QMatrix(r, c, entries) in if_then_else(qmat_valid(a), ok(a, Text), err("invalid rational matrix or axis budget", QMatrix))
    fn qmat_zero(r: Nat, c: Nat) -> Result<QMatrix, Text> =
        if_then_else(or(r > 8, c > 8), err("rational matrix axis budget exceeded", QMatrix), mq_build(r, c, fun(i: Nat, ok(Rational(false, 0, 1), Text))))
    fn qmat_identity(n: Nat) -> Result<QMatrix, Text> =
        if_then_else(n > 8, err("rational matrix axis budget exceeded", QMatrix), mq_build(n, n, fun(i: Nat, ok(Rational(false, if_then_else(nat_eq(div(i, n), mod(i, n)), 1, 0), 1), Text))))
    fn qmat_at(a: QMatrix, r: Nat, c: Nat) -> Result<Rational, Text> =
        if_then_else(not(qmat_valid(a)), err("invalid rational matrix", Rational), if_then_else(or(not(field(a, rows) > r), not(field(a, cols) > c)), err("rational matrix index out of range", Rational), ok(mq_get(a, r, c), Text)))
    fn qmat_transpose(a: QMatrix) -> Result<QMatrix, Text> =
        if_then_else(qmat_valid(a), mq_build(field(a, cols), field(a, rows), fun(i: Nat, ok(mq_get(a, mod(i, field(a, rows)), div(i, field(a, rows))), Text))), err("invalid rational matrix", QMatrix))
    fn qmat_add(a: QMatrix, b: QMatrix) -> Result<QMatrix, Text> =
        if_then_else(not(mq_pair(a, b)), err("invalid rational matrix", QMatrix),
            if_then_else(and(nat_eq(field(a, rows), field(b, rows)), nat_eq(field(a, cols), field(b, cols))), mq_build(field(a, rows), field(a, cols), fun(i: Nat, rat_add(unwrap_or(index(field(a, entries), i), Rational(false, 0, 1)), unwrap_or(index(field(b, entries), i), Rational(false, 0, 1))))), err("rational matrix addition shape mismatch", QMatrix)))
    private fn mq_dot(n: Nat, a: QMatrix, b: QMatrix, r: Nat, c: Nat, k: Nat) -> Result<Rational, Text> decreasing n =
        if_then_else(nat_eq(n, 0), ok(Rational(false, 0, 1), Text),
            match rat_mul(mq_get(a, r, k), mq_get(b, k, c)) {
                err(e) => err(e, Rational), ok(product) => match mq_dot(pred(n), a, b, r, c, k + 1) { err(e) => err(e, Rational), ok(rest) => rat_add(product, rest) }
            })
    fn qmat_mul(a: QMatrix, b: QMatrix) -> Result<QMatrix, Text> =
        if_then_else(not(mq_pair(a, b)), err("invalid rational matrix", QMatrix),
            if_then_else(nat_eq(field(a, cols), field(b, rows)), mq_build(field(a, rows), field(b, cols), fun(i: Nat, mq_dot(field(a, cols), a, b, div(i, field(b, cols)), mod(i, field(b, cols)), 0))), err("rational matrix multiplication shape mismatch", QMatrix)))
    fn qmat_is_zero(a: QMatrix) -> Bool = if_then_else(qmat_valid(a), fold(field(a, entries), true, fun(acc: Bool, q: Rational, and(acc, nat_eq(field(q, numerator), 0)))), false)
    private fn mq_has(xs: List<Nat>, value: Nat) -> Bool decreasing xs =
        if_then_else(is_empty(xs), false, if_then_else(nat_eq(unwrap_or(head(xs), 0), value), true, mq_has(tail(xs), value)))
    private fn mq_pos(xs: List<Nat>, value: Nat, i: Nat) -> Option<Nat> decreasing xs =
        if_then_else(is_empty(xs), none(Nat), if_then_else(nat_eq(unwrap_or(head(xs), 0), value), some(i), mq_pos(tail(xs), value, i + 1)))
    private fn mq_find(n: Nat, a: QMatrix, r: Nat, c: Nat) -> Option<Nat> decreasing n =
        if_then_else(nat_eq(n, 0), none(Nat), if_then_else(not(nat_eq(field(mq_get(a, r, c), numerator), 0)), some(r), mq_find(pred(n), a, r + 1, c)))
    private fn mq_swap(a: QMatrix, r: Nat, p: Nat) -> Result<QMatrix, Text> =
        mq_build(field(a, rows), field(a, cols), fun(i: Nat,
            let row = div(i, field(a, cols)) in ok(mq_get(a, if_then_else(nat_eq(row, r), p, if_then_else(nat_eq(row, p), r, row)), mod(i, field(a, cols))), Text)))
    private fn mq_normalize(a: QMatrix, r: Nat, c: Nat) -> Result<QMatrix, Text> =
        match rat_inv(mq_get(a, r, c)) {
            err(e) => err(e, QMatrix), ok(inverse) => mq_build(field(a, rows), field(a, cols), fun(i: Nat,
                let value = unwrap_or(index(field(a, entries), i), Rational(false, 0, 1)) in
                if_then_else(nat_eq(div(i, field(a, cols)), r), rat_mul(value, inverse), ok(value, Text))))
        }
    private fn mq_clear_cell(a: QMatrix, r: Nat, c: Nat, i: Nat) -> Result<Rational, Text> =
        let row = div(i, field(a, cols)) in let col = mod(i, field(a, cols)) in
        if_then_else(nat_eq(row, r), ok(mq_get(a, row, col), Text),
            match rat_mul(mq_get(a, row, c), mq_get(a, r, col)) { err(e) => err(e, Rational), ok(product) => rat_sub(mq_get(a, row, col), product) })
    private fn mq_eliminate(a: QMatrix, r: Nat, p: Nat, c: Nat) -> Result<QMatrix, Text> =
        match mq_swap(a, r, p) { err(e) => err(e, QMatrix), ok(swapped) => match mq_normalize(swapped, r, c) {
            err(e) => err(e, QMatrix), ok(normalized) => mq_build(field(a, rows), field(a, cols), fun(i: Nat, mq_clear_cell(normalized, r, c, i)))
        } }
    private fn mq_reduce(n: Nat, a: QMatrix, r: Nat, c: Nat) -> Result<QReduced, Text> decreasing n =
        if_then_else(nat_eq(n, 0), ok(QReduced(a, empty_list(Nat)), Text),
            if_then_else(nat_eq(r, field(a, rows)), ok(QReduced(a, empty_list(Nat)), Text),
            match mq_find(monus(field(a, rows), r), a, r, c) {
                none => mq_reduce(pred(n), a, r, c + 1),
                some(p) => match mq_eliminate(a, r, p, c) {
                    err(e) => err(e, QReduced), ok(cleared) => match mq_reduce(pred(n), cleared, r + 1, c + 1) {
                        err(e) => err(e, QReduced), ok(rest) => ok(QReduced(field(rest, matrix), cons(c, field(rest, pivots))), Text)
                    }
                }
            }))
    fn qmat_rref(a: QMatrix) -> Result<QReduced, Text> = if_then_else(qmat_valid(a), mq_reduce(field(a, cols), a, 0, 0), err("invalid rational matrix", QReduced))
    fn qmat_rank(a: QMatrix) -> Result<Nat, Text> = match qmat_rref(a) { err(e) => err(e, Nat), ok(reduced) => ok(length(field(reduced, pivots)), Text) }
    private fn mq_free(n: Nat, c: Nat, pivots: List<Nat>) -> List<Nat> decreasing n =
        if_then_else(nat_eq(n, 0), empty_list(Nat), if_then_else(mq_has(pivots, c), mq_free(pred(n), c + 1, pivots), cons(c, mq_free(pred(n), c + 1, pivots))))
    private fn mq_kernel_cell(reduced: QReduced, free: List<Nat>, r: Nat, c: Nat) -> Result<Rational, Text> =
        let f = unwrap_or(index(free, c), 0) in
        if_then_else(nat_eq(r, f), ok(Rational(false, 1, 1), Text), match mq_pos(field(reduced, pivots), r, 0) { none => ok(Rational(false, 0, 1), Text), some(p) => rat_neg(mq_get(field(reduced, matrix), p, f)) })
    fn qmat_kernel(a: QMatrix) -> Result<QMatrix, Text> =
        match qmat_rref(a) { err(e) => err(e, QMatrix), ok(reduced) =>
            let free = mq_free(field(a, cols), 0, field(reduced, pivots)) in
            mq_build(field(a, cols), length(free), fun(i: Nat, mq_kernel_cell(reduced, free, div(i, length(free)), mod(i, length(free)))))
        }
    private fn mq_columns(a: QMatrix, columns: List<Nat>) -> Result<QMatrix, Text> =
        mq_build(field(a, rows), length(columns), fun(i: Nat, ok(mq_get(a, div(i, length(columns)), unwrap_or(index(columns, mod(i, length(columns))), 0)), Text)))
    fn qmat_columns(a: QMatrix, columns: List<Nat>) -> Result<QMatrix, Text> =
        if_then_else(not(qmat_valid(a)), err("invalid rational matrix", QMatrix),
            if_then_else(length(columns) > 8, err("rational column axis budget", QMatrix),
                if_then_else(fold(columns, true, fun(acc: Bool, c: Nat, and(acc, field(a, cols) > c))), mq_columns(a, columns), err("rational selected column out of range", QMatrix))))
    fn qmat_image(a: QMatrix) -> Result<QMatrix, Text> = match qmat_rref(a) { err(e) => err(e, QMatrix), ok(reduced) => mq_columns(a, field(reduced, pivots)) }
    // Closed finite computations, not independent proof-kernel algorithm proofs.
    theorem rational_identity_rank: nat_eq(unwrap_or(qmat_rank(QMatrix(2, 2, list(Rational(false, 1, 1), Rational(false, 0, 1), Rational(false, 0, 1), Rational(false, 1, 1)))), 999), 2)
    theorem reject_noncanonical_entry: not(qmat_valid(QMatrix(1, 1, list(Rational(false, 2, 4)))))
}
