// Numerical linear algebra (v0.116, docs/LINALG.md): dense row-major matrices over machine floats
// with native factorizations (la_* builtins) and CSR sparse matrices with conjugate gradients.
module std.linalg
import std.graphs

// on top of std.graphs, std.geometry, std.probability and std.numerics
theory StdLinalg extends StdGraphs {
    record Matrix(rows: Nat, cols: Nat, data: FloatVec)
    record Eigen(values: FloatVec, vectors: Matrix)
    record QR(q: Matrix, r: Matrix)
    record LU(packed: Matrix, perm: NatVec)
    record Csr(rows: Nat, cols: Nat, rowptr: NatVec, index: NatVec, values: FloatVec)
    record Triplet(row: Nat, col: Nat, value: Float)

    private fn matrix_size(rows: Nat, cols: Nat) -> Option<Nat> =
        if_then_else(or(rows > 16777216, cols > 16777216), none(Nat),
            let size = mul(rows, cols) in if_then_else(size > 16777216, none(Nat), some(size)))
    private fn valid_matrix(a: Matrix) -> Bool = match matrix_size(field(a, rows), field(a, cols)) {
        none => false, some(size) => nat_eq(size, fv_len(field(a, data))) }
    fn matrix(rows: Nat, cols: Nat, xs: List<Float>) -> Option<Matrix> =
        match matrix_size(rows, cols) { none => none(Matrix), some(size) =>
            match fv_from_list(xs) { none => none(Matrix), some(d) => if_then_else(nat_eq(fv_len(d), size), some(Matrix(rows, cols, d)), none(Matrix)) } }
    fn zeros(rows: Nat, cols: Nat) -> Option<Matrix> = match matrix_size(rows, cols) {
        none => none(Matrix), some(size) => match fv_new(size, 0.0) { none => none(Matrix), some(d) => some(Matrix(rows, cols, d)) } }
    fn identity(n: Nat) -> Option<Matrix> = match la_identity(n) { none => none(Matrix), some(d) => some(Matrix(n, n, d)) }
    fn column(xs: FloatVec) -> Matrix = Matrix(fv_len(xs), 1, xs)
    fn is_square(a: Matrix) -> Bool = nat_eq(field(a, rows), field(a, cols))
    fn get(a: Matrix, i: Nat, j: Nat) -> Option<Float> = if_then_else(and(valid_matrix(a), and(field(a, rows) > i, field(a, cols) > j)), fv_get(field(a, data), mul(i, field(a, cols)) + j), none(Float))
    fn set(a: Matrix, i: Nat, j: Nat, x: Float) -> Option<Matrix> =
        if_then_else(and(valid_matrix(a), and(field(a, rows) > i, field(a, cols) > j)), match fv_set(field(a, data), mul(i, field(a, cols)) + j, x) { none => none(Matrix), some(d) => some(Matrix(field(a, rows), field(a, cols), d)) }, none(Matrix))
    fn row(a: Matrix, i: Nat) -> Option<FloatVec> = if_then_else(and(valid_matrix(a), field(a, rows) > i), fv_slice(field(a, data), mul(i, field(a, cols)), field(a, cols)), none(FloatVec))
    fn col(a: Matrix, j: Nat) -> Option<FloatVec> = if_then_else(and(valid_matrix(a), field(a, cols) > j), match fv_transpose(field(a, data), field(a, rows), field(a, cols)) { none => none(FloatVec), some(t) => fv_slice(t, mul(j, field(a, rows)), field(a, rows)) }, none(FloatVec))
    fn transpose(a: Matrix) -> Matrix = Matrix(field(a, cols), field(a, rows), unwrap_or(fv_transpose(field(a, data), field(a, rows), field(a, cols)), field(a, data)))
    private fn same_shape(a: Matrix, b: Matrix) -> Bool = and(nat_eq(field(a, rows), field(b, rows)), nat_eq(field(a, cols), field(b, cols)))
    fn add(a: Matrix, b: Matrix) -> Option<Matrix> =
        if_then_else(same_shape(a, b), match fv_add(field(a, data), field(b, data)) { none => none(Matrix), some(d) => some(Matrix(field(a, rows), field(a, cols), d)) }, none(Matrix))
    fn sub(a: Matrix, b: Matrix) -> Option<Matrix> =
        if_then_else(same_shape(a, b), match fv_sub(field(a, data), field(b, data)) { none => none(Matrix), some(d) => some(Matrix(field(a, rows), field(a, cols), d)) }, none(Matrix))
    fn scale(a: Matrix, k: Float) -> Matrix = Matrix(field(a, rows), field(a, cols), fv_scale(field(a, data), k))
    fn matmul(a: Matrix, b: Matrix) -> Option<Matrix> =
        if_then_else(and(and(valid_matrix(a), valid_matrix(b)), nat_eq(field(a, cols), field(b, rows))),
            match fv_matmul(field(a, data), field(b, data), field(a, rows), field(a, cols), field(b, cols)) { none => none(Matrix), some(d) => some(Matrix(field(a, rows), field(b, cols), d)) }, none(Matrix))
    fn matvec(a: Matrix, x: FloatVec) -> Option<FloatVec> = fv_matmul(field(a, data), x, field(a, rows), field(a, cols), 1)
    fn frobenius(a: Matrix) -> Float = fv_norm(field(a, data))
    fn max_abs_diff(a: Matrix, b: Matrix) -> Option<Float> = match sub(a, b) { none => none(Float), some(d) => fv_max(unwrap_or(fv_from_list(map(fv_to_list(field(d, data)), fun(x: Float, fl_abs(x)))), fv_empty())) }
    fn trace(a: Matrix) -> Option<Float> = if_then_else(is_square(a), la_trace(field(a, data), field(a, rows)), none(Float))

    fn det(a: Matrix) -> Option<Float> = if_then_else(is_square(a), la_det(field(a, data), field(a, rows)), none(Float))
    fn inverse(a: Matrix) -> Option<Matrix> = if_then_else(is_square(a), match la_inverse(field(a, data), field(a, rows)) { none => none(Matrix), some(d) => some(Matrix(field(a, rows), field(a, rows), d)) }, none(Matrix))
    fn solve(a: Matrix, b: FloatVec) -> Option<FloatVec> = if_then_else(is_square(a), fv_solve(field(a, data), b, field(a, rows)), none(FloatVec))
    fn lu(a: Matrix) -> Option<LU> =
        if_then_else(is_square(a), match la_lu(field(a, data), field(a, rows)) { none => none(LU), some(d) => match la_lu_perm(field(a, data), field(a, rows)) { none => none(LU), some(p) => some(LU(Matrix(field(a, rows), field(a, rows), d), p)) } }, none(LU))
    fn cholesky(a: Matrix) -> Option<Matrix> = if_then_else(is_square(a), match la_cholesky(field(a, data), field(a, rows)) { none => none(Matrix), some(d) => some(Matrix(field(a, rows), field(a, rows), d)) }, none(Matrix))
    fn qr(a: Matrix) -> Option<QR> =
        match la_qr_q(field(a, data), field(a, rows), field(a, cols)) { none => none(QR), some(q) => match la_qr_r(field(a, data), field(a, rows), field(a, cols)) { none => none(QR), some(r) =>
            some(QR(Matrix(field(a, rows), field(a, cols), q), Matrix(field(a, cols), field(a, cols), r))) } }
    fn least_squares(a: Matrix, b: FloatVec) -> Option<FloatVec> = la_lstsq(field(a, data), b, field(a, rows), field(a, cols))
    fn eigh(a: Matrix) -> Option<Eigen> =
        if_then_else(is_square(a), match la_eigh_values(field(a, data), field(a, rows)) { none => none(Eigen), some(v) => match la_eigh_vectors(field(a, data), field(a, rows)) { none => none(Eigen), some(w) =>
            some(Eigen(v, Matrix(field(a, rows), field(a, rows), w))) } }, none(Eigen))
    fn singular_values(a: Matrix) -> Option<FloatVec> = la_svd_values(field(a, data), field(a, rows), field(a, cols))
    fn rank(a: Matrix, tol: Float) -> Option<Nat> = la_rank(field(a, data), field(a, rows), field(a, cols), tol)
    // 2-norm condition number σmax / σmin (inf when singular)
    fn condition(a: Matrix) -> Option<Float> =
        match singular_values(a) { none => none(Float), some(s) => match fv_max(s) { none => none(Float), some(hi) => match fv_min(s) { none => none(Float), some(lo) => some(fl_div(hi, lo)) } } }
    fn spectral_norm(a: Matrix) -> Option<Float> = match singular_values(a) { none => none(Float), some(s) => fv_max(s) }
    private fn pow_loop(k: Nat, a: Matrix, acc: Matrix) -> Option<Matrix> decreasing k =
        if_then_else(nat_eq(k, 0), some(acc), match matmul(acc, a) { none => none(Matrix), some(m) => pow_loop(pred(k), a, m) })
    fn matrix_power(a: Matrix, k: Nat) -> Option<Matrix> = match identity(field(a, rows)) { none => none(Matrix), some(i) => if_then_else(is_square(a), pow_loop(k, a, i), none(Matrix)) }
    private fn power_loop(k: Nat, a: Matrix, x: FloatVec, lambda: Float) -> Float decreasing k =
        if_then_else(nat_eq(k, 0), lambda, match matvec(a, x) { none => lambda, some(y) => let l = fv_norm(y) in
            if_then_else(fl_eq(l, 0.0), 0.0, power_loop(pred(k), a, fv_scale(y, fl_div(1.0, l)), unwrap_or(fv_dot(x, y), lambda))) })
    // dominant eigenvalue estimate by power iteration (Rayleigh quotient)
    fn dominant_eigenvalue(a: Matrix, iterations: Nat) -> Option<Float> =
        if_then_else(and(is_square(a), field(a, rows) > 0), some(power_loop(iterations, a, fv_scale(unwrap_or(fv_new(field(a, rows), 1.0), fv_empty()), fl_div(1.0, fl_sqrt(fl_of_nat(field(a, rows))))), 0.0)), none(Float))

    // ---- sparse CSR ----
    private fn row_entries(ts: List<Triplet>, r: Nat) -> List<Triplet> = fold(ts, empty_list(Triplet), fun(acc: List<Triplet>, t: Triplet, if_then_else(nat_eq(field(t, row), r), cons(t, acc), acc)))
    private fn insert_col(ts: List<Triplet>, t: Triplet) -> List<Triplet> decreasing ts =
        if_then_else(is_empty(ts), list(t), match head(ts) { none => list(t), some(h) => if_then_else(field(t, col) > field(h, col), cons(h, insert_col(tail(ts), t)), cons(t, ts)) })
    private fn csr_rows(k: Nat, r: Nat, ts: List<Triplet>, ptr: NatVec, idx: NatVec, vals: FloatVec) -> Csr decreasing k =
        if_then_else(nat_eq(k, 0), Csr(r, 0, ptr, idx, vals),
            let es = fold(row_entries(ts, r), empty_list(Triplet), insert_col) in
            let idx2 = fold(es, idx, fun(acc: NatVec, t: Triplet, unwrap_or(nv_push(acc, field(t, col)), acc))) in
            let vals2 = fold(es, vals, fun(acc: FloatVec, t: Triplet, unwrap_or(fv_concat(acc, unwrap_or(fv_new(1, field(t, value)), fv_empty())), acc))) in
            csr_rows(pred(k), r + 1, ts, unwrap_or(nv_push(ptr, nv_len(idx2)), ptr), idx2, vals2))
    // CSR from (row, col, value) triplets; duplicates are kept as separate entries (they add up in products)
    fn csr(rows: Nat, cols: Nat, ts: List<Triplet>) -> Option<Csr> =
        if_then_else(and(and(16777216 > rows, not(cols > 16777216)), and(not(length(ts) > 16777216), is_empty(fold(ts, empty_list(Triplet), fun(acc: List<Triplet>, t: Triplet, if_then_else(not(and(rows > field(t, row), cols > field(t, col))), cons(t, acc), acc)))))),
            let c = csr_rows(rows, 0, ts, unwrap_or(nv_from_list(list(0)), nv_empty()), nv_empty(), fv_empty()) in
            some(Csr(rows, cols, field(c, rowptr), field(c, index), field(c, values))), none(Csr))
    fn csr_nnz(a: Csr) -> Nat = nv_len(field(a, index))
    fn csr_matvec(a: Csr, x: FloatVec) -> Option<FloatVec> =
        if_then_else(nat_eq(fv_len(x), field(a, cols)), la_csr_matvec(field(a, rowptr), field(a, index), field(a, values), x), none(FloatVec))
    fn conjugate_gradient(a: Csr, b: FloatVec, tol: Float, iterations: Nat) -> Option<FloatVec> =
        if_then_else(nat_eq(field(a, rows), field(a, cols)), la_cg(field(a, rowptr), field(a, index), field(a, values), b, tol, iterations), none(FloatVec))
    // 1-D Laplacian tridiag(-1, 2, -1) of size n (a standard SPD test matrix)
    private fn lap_loop(k: Nat, n: Nat, i: Nat, acc: List<Triplet>) -> List<Triplet> decreasing k =
        if_then_else(nat_eq(k, 0), acc,
            let a1 = cons(Triplet(i, i, 2.0), acc) in
            let a2 = if_then_else(i > 0, cons(Triplet(i, monus(i, 1), -1.0), a1), a1) in
            let a3 = if_then_else(n > i + 1, cons(Triplet(i, i + 1, -1.0), a2), a2) in
            lap_loop(pred(k), n, i + 1, a3))
    fn laplacian_1d(n: Nat) -> Option<Csr> = csr(n, n, lap_loop(n, n, 0, empty_list(Triplet)))

    // ---- v0.123: Krylov solvers on CSR ----
    fn preconditioned_cg(a: Csr, b: FloatVec, tol: Float, iterations: Nat) -> Option<FloatVec> =
        if_then_else(nat_eq(field(a, rows), field(a, cols)), la_pcg(field(a, rowptr), field(a, index), field(a, values), b, tol, iterations), none(FloatVec))
    fn bicgstab(a: Csr, b: FloatVec, tol: Float, iterations: Nat) -> Option<FloatVec> =
        if_then_else(nat_eq(field(a, rows), field(a, cols)), la_bicgstab(field(a, rowptr), field(a, index), field(a, values), b, tol, iterations), none(FloatVec))
    fn gmres(a: Csr, b: FloatVec, tol: Float, restart: Nat, iterations: Nat) -> Option<FloatVec> =
        if_then_else(nat_eq(field(a, rows), field(a, cols)), la_gmres(field(a, rowptr), field(a, index), field(a, values), b, tol, restart, iterations), none(FloatVec))
    // residual ‖A x − b‖
    fn csr_residual(a: Csr, x: FloatVec, b: FloatVec) -> Option<Float> =
        match csr_matvec(a, x) { none => none(Float), some(ax) => match fv_sub(ax, b) { none => none(Float), some(r) => some(fv_norm(r)) } }
    // 2-D Laplacian on an m × m grid (5-point stencil, Dirichlet), size m²
    private fn lap2_loop(k: Nat, m: Nat, i: Nat, acc: List<Triplet>) -> List<Triplet> decreasing k =
        if_then_else(nat_eq(k, 0), acc,
            let r = div(i, m) in let c = mod(i, m) in
            let a1 = cons(Triplet(i, i, 4.0), acc) in
            let a2 = if_then_else(r > 0, cons(Triplet(i, monus(i, m), -1.0), a1), a1) in
            let a3 = if_then_else(m > r + 1, cons(Triplet(i, i + m, -1.0), a2), a2) in
            let a4 = if_then_else(c > 0, cons(Triplet(i, monus(i, 1), -1.0), a3), a3) in
            let a5 = if_then_else(m > c + 1, cons(Triplet(i, i + 1, -1.0), a4), a4) in
            lap2_loop(pred(k), m, i + 1, a5))
    fn laplacian_2d(m: Nat) -> Option<Csr> = csr(mul(m, m), mul(m, m), lap2_loop(mul(m, m), m, 0, empty_list(Triplet)))
    // row equilibration: D A x = D b with D = 1 / max |a_ij| per row
    fn equilibrate(a: Csr, b: FloatVec) -> ProductType<Csr*FloatVec> =
        let scales = map(nv_to_list(unwrap_or(nv_range(0, field(a, rows)), nv_empty())), fun(i: Nat,
            let s = unwrap_or(nv_get(field(a, rowptr), i), 0) in let e = unwrap_or(nv_get(field(a, rowptr), i + 1), 0) in
            let m = fold(nv_to_list(unwrap_or(nv_range(s, monus(e, s)), nv_empty())), 0.0, fun(acc: Float, k: Nat, fl_max(acc, fl_abs(unwrap_or(fv_get(field(a, values), k), 0.0))))) in
            if_then_else(fl_eq(m, 0.0), 1.0, fl_div(1.0, m)))) in
        let sv = unwrap_or(fv_from_list(scales), fv_empty()) in
        let vals = map(nv_to_list(unwrap_or(nv_range(0, nv_len(field(a, index))), nv_empty())), fun(k: Nat,
            let row = fold(nv_to_list(unwrap_or(nv_range(0, field(a, rows)), nv_empty())), 0, fun(acc: Nat, i: Nat, if_then_else(k + 1 > unwrap_or(nv_get(field(a, rowptr), i), 0), i, acc))) in
            fl_mul(unwrap_or(fv_get(field(a, values), k), 0.0), unwrap_or(fv_get(sv, row), 1.0)))) in
        pair(Csr(field(a, rows), field(a, cols), field(a, rowptr), field(a, index), unwrap_or(fv_from_list(vals), field(a, values))), unwrap_or(fv_mul(b, sv), b))

    theorem identity_has_unit_determinant: fl_eq(unwrap_or(la_det(unwrap_or(la_identity(3), fv_empty()), 3), 0.0), 1.0)
}
