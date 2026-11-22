// BigRational matrices in ordinary DLM. Public axes <=4, private solve <=4x8.
// Every fallible scalar operation and validation threads its returned budget.
module std.matrix_bigq
import std.exact

theory StdMatrixBigQ extends StdExact {
    record BigMatrix(rows: Nat, cols: Nat, entries: List<BigRational>)
    record BigMatrixCalc(value: BigMatrix, budget: ExactBudget)
    record BigMatrixCells(entries: List<BigRational>, budget: ExactBudget)
    record BigMatrixReduced(matrix: BigMatrix, pivots: List<Nat>, scales: List<BigRational>, odd_swaps: Bool, budget: ExactBudget)
    record BigMatrixRank(rank: Nat, budget: ExactBudget)
    record BigMatrixFlag(value: Bool, budget: ExactBudget)
    record BigMatrixSolution(particular: BigMatrix, kernel: BigMatrix, budget: ExactBudget)
    private fn bm_zero() -> BigRational = BigRational(false, empty_list(Nat), list(1))
    private fn bm_one() -> BigRational = BigRational(false, list(1), list(1))
    private fn bm_get(a: BigMatrix, r: Nat, c: Nat) -> BigRational = unwrap_or(index(field(a, entries), mul(r, field(a, cols)) + c), bm_zero())
    private fn bm_shape(a: BigMatrix) -> Bool =
        if_then_else(or(field(a, rows) > 4, field(a, cols) > 4), false, nat_eq(length(field(a, entries)), mul(field(a, rows), field(a, cols))))
    private fn bm_validate(xs: List<BigRational>, b: ExactBudget) -> Result<ExactBudget, Text> decreasing xs fuel 128 =
        if_then_else(is_empty(xs), ok(b, Text), match bigrat_check(b, unwrap_or(head(xs), bm_zero())) { err(e) => err(e, ExactBudget), ok(v) => bm_validate(tail(xs), field(v, budget)) })
    fn bmat_check(b: ExactBudget, a: BigMatrix) -> Result<BigMatrixCalc, Text> =
        match exact_budget(field(b, bits), field(b, work)) {
            err(e) => err(e, BigMatrixCalc), ok(valid) => if_then_else(not(bm_shape(a)), err("invalid BigMatrix shape or axis budget", BigMatrixCalc),
                match bm_validate(field(a, entries), valid) { err(e) => err(e, BigMatrixCalc), ok(left) => ok(BigMatrixCalc(a, left), Text) })
        }
    fn bmat_new(b: ExactBudget, rows: Nat, cols: Nat, entries: List<BigRational>) -> Result<BigMatrixCalc, Text> = bmat_check(b, BigMatrix(rows, cols, entries))
    private fn bm_cells(n: Nat, i: Nat, b: ExactBudget, f: Fn<ExactBudget, Nat, Result<BigRationalCalc, Text>>) -> Result<BigMatrixCells, Text> decreasing n fuel 128 =
        if_then_else(nat_eq(n, 0), ok(BigMatrixCells(empty_list(BigRational), b), Text),
            match f(b, i) { err(e) => err(e, BigMatrixCells), ok(v) => match bm_cells(pred(n), i + 1, field(v, budget), f) {
                err(e) => err(e, BigMatrixCells), ok(rest) => ok(BigMatrixCells(cons(field(v, value), field(rest, entries)), field(rest, budget)), Text)
            } })
    private fn bm_build(rows: Nat, cols: Nat, b: ExactBudget, f: Fn<ExactBudget, Nat, Result<BigRationalCalc, Text>>) -> Result<BigMatrixCalc, Text> =
        match bm_cells(mul(rows, cols), 0, b, f) { err(e) => err(e, BigMatrixCalc), ok(v) => ok(BigMatrixCalc(BigMatrix(rows, cols, field(v, entries)), field(v, budget)), Text) }
    fn bmat_zero(b: ExactBudget, rows: Nat, cols: Nat) -> Result<BigMatrixCalc, Text> =
        if_then_else(or(rows > 4, cols > 4), err("BigMatrix axis budget exceeded", BigMatrixCalc),
            match exact_budget(field(b, bits), field(b, work)) { err(e) => err(e, BigMatrixCalc), ok(valid) => bm_build(rows, cols, valid, fun(left: ExactBudget, i: Nat, ok(BigRationalCalc(bm_zero(), left), Text))) })
    fn bmat_identity(b: ExactBudget, n: Nat) -> Result<BigMatrixCalc, Text> =
        if_then_else(n > 4, err("BigMatrix axis budget exceeded", BigMatrixCalc),
            match exact_budget(field(b, bits), field(b, work)) { err(e) => err(e, BigMatrixCalc), ok(valid) => bm_build(n, n, valid, fun(left: ExactBudget, i: Nat, ok(BigRationalCalc(if_then_else(nat_eq(div(i, n), mod(i, n)), bm_one(), bm_zero()), left), Text))) })
    fn bmat_at(b: ExactBudget, a: BigMatrix, r: Nat, c: Nat) -> Result<BigRationalCalc, Text> =
        match bmat_check(b, a) { err(e) => err(e, BigRationalCalc), ok(checked) =>
            if_then_else(or(not(field(a, rows) > r), not(field(a, cols) > c)), err("BigMatrix index out of range", BigRationalCalc), ok(BigRationalCalc(bm_get(a, r, c), field(checked, budget)), Text)) }
    fn bmat_transpose(b: ExactBudget, a: BigMatrix) -> Result<BigMatrixCalc, Text> =
        match bmat_check(b, a) { err(e) => err(e, BigMatrixCalc), ok(checked) => bm_build(field(a, cols), field(a, rows), field(checked, budget), fun(left: ExactBudget, i: Nat, ok(BigRationalCalc(bm_get(a, mod(i, field(a, rows)), div(i, field(a, rows))), left), Text))) }
    fn bmat_add(b: ExactBudget, a: BigMatrix, c: BigMatrix) -> Result<BigMatrixCalc, Text> =
        match bmat_check(b, a) { err(e) => err(e, BigMatrixCalc), ok(ca) => match bmat_check(field(ca, budget), c) {
            err(e) => err(e, BigMatrixCalc), ok(cc) => if_then_else(not(and(nat_eq(field(a, rows), field(c, rows)), nat_eq(field(a, cols), field(c, cols)))), err("BigMatrix addition shape mismatch", BigMatrixCalc),
                bm_build(field(a, rows), field(a, cols), field(cc, budget), fun(left: ExactBudget, i: Nat, bigrat_add(left, unwrap_or(index(field(a, entries), i), bm_zero()), unwrap_or(index(field(c, entries), i), bm_zero())))))
        } }
    private fn bm_dot(n: Nat, a: BigMatrix, c: BigMatrix, row: Nat, col: Nat, k: Nat, acc: BigRational, b: ExactBudget) -> Result<BigRationalCalc, Text> decreasing n fuel 128 =
        if_then_else(nat_eq(n, 0), ok(BigRationalCalc(acc, b), Text),
            match bigrat_mul(b, bm_get(a, row, k), bm_get(c, k, col)) { err(e) => err(e, BigRationalCalc), ok(product) =>
                match bigrat_add(field(product, budget), acc, field(product, value)) { err(e) => err(e, BigRationalCalc), ok(sum) => bm_dot(pred(n), a, c, row, col, k + 1, field(sum, value), field(sum, budget)) }
            })
    fn bmat_mul(b: ExactBudget, a: BigMatrix, c: BigMatrix) -> Result<BigMatrixCalc, Text> =
        match bmat_check(b, a) { err(e) => err(e, BigMatrixCalc), ok(ca) => match bmat_check(field(ca, budget), c) {
            err(e) => err(e, BigMatrixCalc), ok(cc) => if_then_else(not(nat_eq(field(a, cols), field(c, rows))), err("BigMatrix multiplication shape mismatch", BigMatrixCalc),
                bm_build(field(a, rows), field(c, cols), field(cc, budget), fun(left: ExactBudget, i: Nat, bm_dot(field(a, cols), a, c, div(i, field(c, cols)), mod(i, field(c, cols)), 0, bm_zero(), left))))
        } }
    fn bmat_is_zero(b: ExactBudget, a: BigMatrix) -> Result<BigMatrixFlag, Text> =
        match bmat_check(b, a) { err(e) => err(e, BigMatrixFlag), ok(v) => ok(BigMatrixFlag(fold(field(a, entries), true, fun(acc: Bool, q: BigRational, and(acc, is_empty(field(q, numerator))))), field(v, budget)), Text) }
    private fn bm_has(xs: List<Nat>, value: Nat) -> Bool decreasing xs fuel 128 =
        if_then_else(is_empty(xs), false, if_then_else(nat_eq(unwrap_or(head(xs), 0), value), true, bm_has(tail(xs), value)))
    private fn bm_pos(xs: List<Nat>, value: Nat, i: Nat) -> Option<Nat> decreasing xs fuel 128 =
        if_then_else(is_empty(xs), none(Nat), if_then_else(nat_eq(unwrap_or(head(xs), 0), value), some(i), bm_pos(tail(xs), value, i + 1)))
    private fn bm_find(n: Nat, a: BigMatrix, r: Nat, c: Nat) -> Option<Nat> decreasing n fuel 128 =
        if_then_else(nat_eq(n, 0), none(Nat), if_then_else(not(is_empty(field(bm_get(a, r, c), numerator))), some(r), bm_find(pred(n), a, r + 1, c)))
    private fn bm_swap(b: ExactBudget, a: BigMatrix, r: Nat, p: Nat) -> Result<BigMatrixCalc, Text> =
        bm_build(field(a, rows), field(a, cols), b, fun(left: ExactBudget, i: Nat, let row = div(i, field(a, cols)) in
            ok(BigRationalCalc(bm_get(a, if_then_else(nat_eq(row, r), p, if_then_else(nat_eq(row, p), r, row)), mod(i, field(a, cols))), left), Text)))
    private fn bm_normalize(b: ExactBudget, a: BigMatrix, r: Nat, c: Nat) -> Result<BigMatrixCalc, Text> =
        match bigrat_inv(b, bm_get(a, r, c)) { err(e) => err(e, BigMatrixCalc), ok(inverse) =>
            bm_build(field(a, rows), field(a, cols), field(inverse, budget), fun(left: ExactBudget, i: Nat, let q = unwrap_or(index(field(a, entries), i), bm_zero()) in
                if_then_else(nat_eq(div(i, field(a, cols)), r), bigrat_mul(left, q, field(inverse, value)), ok(BigRationalCalc(q, left), Text)))) }
    private fn bm_clear_cell(b: ExactBudget, a: BigMatrix, r: Nat, c: Nat, i: Nat) -> Result<BigRationalCalc, Text> =
        let row = div(i, field(a, cols)) in let col = mod(i, field(a, cols)) in let q = bm_get(a, row, col) in
        if_then_else(or(nat_eq(row, r), is_empty(field(bm_get(a, row, c), numerator))), ok(BigRationalCalc(q, b), Text),
            match bigrat_mul(b, bm_get(a, row, c), bm_get(a, r, col)) { err(e) => err(e, BigRationalCalc), ok(product) => bigrat_sub(field(product, budget), q, field(product, value)) })
    private fn bm_eliminate(b: ExactBudget, a: BigMatrix, r: Nat, p: Nat, c: Nat) -> Result<BigMatrixCalc, Text> =
        match bm_swap(b, a, r, p) { err(e) => err(e, BigMatrixCalc), ok(swapped) => match bm_normalize(field(swapped, budget), field(swapped, value), r, c) {
            err(e) => err(e, BigMatrixCalc), ok(normalized) => bm_build(field(a, rows), field(a, cols), field(normalized, budget), fun(left: ExactBudget, i: Nat, bm_clear_cell(left, field(normalized, value), r, c, i)))
        } }
    private fn bm_reduce(n: Nat, a: BigMatrix, r: Nat, c: Nat, b: ExactBudget) -> Result<BigMatrixReduced, Text> decreasing n fuel 128 =
        if_then_else(nat_eq(n, 0), ok(BigMatrixReduced(a, empty_list(Nat), empty_list(BigRational), false, b), Text),
            if_then_else(nat_eq(r, field(a, rows)), ok(BigMatrixReduced(a, empty_list(Nat), empty_list(BigRational), false, b), Text),
                match bm_find(monus(field(a, rows), r), a, r, c) {
                    none => bm_reduce(pred(n), a, r, c + 1, b),
                    some(p) => match bm_eliminate(b, a, r, p, c) { err(e) => err(e, BigMatrixReduced), ok(cleared) =>
                        match bm_reduce(pred(n), field(cleared, value), r + 1, c + 1, field(cleared, budget)) {
                            err(e) => err(e, BigMatrixReduced), ok(rest) => ok(BigMatrixReduced(field(rest, matrix), cons(c, field(rest, pivots)), cons(bm_get(a, p, c), field(rest, scales)), if_then_else(nat_eq(r, p), field(rest, odd_swaps), not(field(rest, odd_swaps))), field(rest, budget)), Text)
                        }
                    }
                }))
    fn bmat_rref(b: ExactBudget, a: BigMatrix) -> Result<BigMatrixReduced, Text> =
        match bmat_check(b, a) { err(e) => err(e, BigMatrixReduced), ok(v) => bm_reduce(field(a, cols), a, 0, 0, field(v, budget)) }
    fn bmat_rank(b: ExactBudget, a: BigMatrix) -> Result<BigMatrixRank, Text> =
        match bmat_rref(b, a) { err(e) => err(e, BigMatrixRank), ok(v) => ok(BigMatrixRank(length(field(v, pivots)), field(v, budget)), Text) }
    private fn bm_free(n: Nat, c: Nat, pivots: List<Nat>) -> List<Nat> decreasing n fuel 128 =
        if_then_else(nat_eq(n, 0), empty_list(Nat), if_then_else(bm_has(pivots, c), bm_free(pred(n), c + 1, pivots), cons(c, bm_free(pred(n), c + 1, pivots))))
    private fn bm_kernel_cell(b: ExactBudget, reduced: BigMatrixReduced, free: List<Nat>, row: Nat, col: Nat) -> Result<BigRationalCalc, Text> =
        let f = unwrap_or(index(free, col), 0) in
        if_then_else(nat_eq(row, f), ok(BigRationalCalc(bm_one(), b), Text), match bm_pos(field(reduced, pivots), row, 0) {
            none => ok(BigRationalCalc(bm_zero(), b), Text), some(p) => bigrat_neg(b, bm_get(field(reduced, matrix), p, f)) })
    private fn bm_kernel(reduced: BigMatrixReduced, columns: Nat) -> Result<BigMatrixCalc, Text> =
        let free = bm_free(columns, 0, field(reduced, pivots)) in
        bm_build(columns, length(free), field(reduced, budget), fun(left: ExactBudget, i: Nat, bm_kernel_cell(left, reduced, free, div(i, length(free)), mod(i, length(free)))))
    fn bmat_kernel(b: ExactBudget, a: BigMatrix) -> Result<BigMatrixCalc, Text> =
        match bmat_rref(b, a) { err(e) => err(e, BigMatrixCalc), ok(v) => bm_kernel(v, field(a, cols)) }
    private fn bm_columns(b: ExactBudget, a: BigMatrix, columns: List<Nat>) -> Result<BigMatrixCalc, Text> =
        bm_build(field(a, rows), length(columns), b, fun(left: ExactBudget, i: Nat, ok(BigRationalCalc(bm_get(a, div(i, length(columns)), unwrap_or(index(columns, mod(i, length(columns))), 0)), left), Text)))
    fn bmat_columns(b: ExactBudget, a: BigMatrix, columns: List<Nat>) -> Result<BigMatrixCalc, Text> =
        match bmat_check(b, a) { err(e) => err(e, BigMatrixCalc), ok(v) => if_then_else(length(columns) > 4, err("BigMatrix column axis budget", BigMatrixCalc),
            if_then_else(fold(columns, true, fun(acc: Bool, c: Nat, and(acc, field(a, cols) > c))), bm_columns(field(v, budget), a, columns), err("BigMatrix selected column out of range", BigMatrixCalc))) }
    fn bmat_image(b: ExactBudget, a: BigMatrix) -> Result<BigMatrixCalc, Text> =
        match bmat_rref(b, a) { err(e) => err(e, BigMatrixCalc), ok(v) => bm_columns(field(v, budget), a, field(v, pivots)) }
    private fn bm_product(xs: List<BigRational>, acc: BigRational, b: ExactBudget) -> Result<BigRationalCalc, Text> decreasing xs fuel 128 =
        if_then_else(is_empty(xs), ok(BigRationalCalc(acc, b), Text), match bigrat_mul(b, acc, unwrap_or(head(xs), bm_one())) {
            err(e) => err(e, BigRationalCalc), ok(v) => bm_product(tail(xs), field(v, value), field(v, budget)) })
    fn bmat_det(b: ExactBudget, a: BigMatrix) -> Result<BigRationalCalc, Text> =
        if_then_else(not(nat_eq(field(a, rows), field(a, cols))), err("BigMatrix determinant requires square shape", BigRationalCalc),
            match bmat_rref(b, a) { err(e) => err(e, BigRationalCalc), ok(v) =>
                if_then_else(not(nat_eq(length(field(v, pivots)), field(a, rows))), ok(BigRationalCalc(bm_zero(), field(v, budget)), Text),
                    match bm_product(field(v, scales), bm_one(), field(v, budget)) { err(e) => err(e, BigRationalCalc), ok(product) =>
                        if_then_else(field(v, odd_swaps), bigrat_neg(field(product, budget), field(product, value)), ok(product, Text)) }) })
    private fn bm_join(b: ExactBudget, a: BigMatrix, c: BigMatrix) -> Result<BigMatrixCalc, Text> =
        let cols = field(a, cols) + field(c, cols) in
        bm_build(field(a, rows), cols, b, fun(left: ExactBudget, i: Nat, let row = div(i, cols) in let col = mod(i, cols) in
            ok(BigRationalCalc(if_then_else(field(a, cols) > col, bm_get(a, row, col), bm_get(c, row, monus(col, field(a, cols)))), left), Text)))
    private fn bm_consistent(n: Nat, reduced: BigMatrixReduced, columns: Nat, rhs: Nat, first: Nat, i: Nat) -> Bool decreasing n fuel 128 =
        if_then_else(nat_eq(n, 0), true,
            if_then_else(is_empty(field(bm_get(field(reduced, matrix), first + div(i, rhs), columns + mod(i, rhs)), numerator)), bm_consistent(pred(n), reduced, columns, rhs, first, i + 1), false))
    private fn bm_particular(b: ExactBudget, reduced: BigMatrixReduced, columns: Nat, rhs: Nat) -> Result<BigMatrixCalc, Text> =
        bm_build(columns, rhs, b, fun(left: ExactBudget, i: Nat, match bm_pos(field(reduced, pivots), div(i, rhs), 0) {
            none => ok(BigRationalCalc(bm_zero(), left), Text), some(p) => ok(BigRationalCalc(bm_get(field(reduced, matrix), p, columns + mod(i, rhs)), left), Text) }))
    fn bmat_solve(b: ExactBudget, a: BigMatrix, c: BigMatrix) -> Result<BigMatrixSolution, Text> =
        match bmat_check(b, a) { err(e) => err(e, BigMatrixSolution), ok(ca) => match bmat_check(field(ca, budget), c) {
            err(e) => err(e, BigMatrixSolution), ok(cc) => if_then_else(not(nat_eq(field(a, rows), field(c, rows))), err("BigMatrix solve RHS shape mismatch", BigMatrixSolution),
                match bm_join(field(cc, budget), a, c) { err(e) => err(e, BigMatrixSolution), ok(joined) =>
                    match bm_reduce(field(a, cols), field(joined, value), 0, 0, field(joined, budget)) {
                        err(e) => err(e, BigMatrixSolution), ok(reduced) =>
                            if_then_else(not(bm_consistent(mul(monus(field(a, rows), length(field(reduced, pivots))), field(c, cols)), reduced, field(a, cols), field(c, cols), length(field(reduced, pivots)), 0)), err("inconsistent BigMatrix linear system", BigMatrixSolution),
                                match bm_particular(field(reduced, budget), reduced, field(a, cols), field(c, cols)) { err(e) => err(e, BigMatrixSolution), ok(particular) =>
                                    let next = BigMatrixReduced(field(reduced, matrix), field(reduced, pivots), field(reduced, scales), field(reduced, odd_swaps), field(particular, budget)) in
                                    match bm_kernel(next, field(a, cols)) { err(e) => err(e, BigMatrixSolution), ok(kernel) => ok(BigMatrixSolution(field(particular, value), field(kernel, value), field(kernel, budget)), Text) }
                                })
                    }
                })
        } }
    fn bmat_inverse(b: ExactBudget, a: BigMatrix) -> Result<BigMatrixCalc, Text> =
        if_then_else(not(nat_eq(field(a, rows), field(a, cols))), err("BigMatrix inverse requires square shape", BigMatrixCalc),
            match bmat_identity(b, field(a, rows)) { err(e) => err(e, BigMatrixCalc), ok(id) => match bmat_solve(field(id, budget), a, field(id, value)) {
                err(e) => err(e, BigMatrixCalc), ok(solution) => if_then_else(nat_eq(field(field(solution, kernel), cols), 0), ok(BigMatrixCalc(field(solution, particular), field(solution, budget)), Text), err("singular BigMatrix", BigMatrixCalc))
            } })


    private fn bm_text_loop(n: Nat, xs: List<BigRational>, cols: Nat, i: Nat, text: Text, b: ExactBudget) -> Result<BigTextCalc, Text> decreasing n fuel 128 =
        if_then_else(nat_eq(n, 0), ok(BigTextCalc(text, b), Text), match bigrat_text(b, unwrap_or(head(xs), bm_zero())) {
            err(e) => err(e, BigTextCalc), ok(part) => bm_text_loop(pred(n), tail(xs), cols, i + 1, concat(text, concat(field(part, value), if_then_else(nat_eq(mod(i + 1, cols), 0), "\n", " "))), field(part, budget))
        })
    fn bmat_text(b: ExactBudget, a: BigMatrix) -> Result<BigTextCalc, Text> =
        match bmat_check(b, a) { err(e) => err(e, BigTextCalc), ok(v) => bm_text_loop(length(field(a, entries)), field(a, entries), field(a, cols), 0, concat(nat_to_text(field(a, rows)), concat(" ", concat(nat_to_text(field(a, cols)), "\n"))), field(v, budget)) }

    // Closed finite evaluations retain legacy source certificate assurance.
    theorem empty_rank: nat_eq(field(unwrap_or(bmat_rank(exact_default_budget(), BigMatrix(0, 0, empty_list(BigRational))), BigMatrixRank(999, exact_default_budget())), rank), 0)
    theorem bad_shape_rejected: is_err(bmat_new(exact_default_budget(), 5, 0, empty_list(BigRational)))
}
