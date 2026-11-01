// Optimization (v0.117, docs/ANALYSIS.md): Nelder–Mead, BFGS with Armijo backtracking,
// Levenberg–Marquardt least squares, linear programming (native two-phase simplex).
module std.optimize
import std.analysis

theory StdOptimize extends StdAnalysis {
    record Vertex(x: FloatVec, fx: Float)
    record Minimum(x: FloatVec, value: Float, iterations: Nat)
    record LpResult(status: Nat, x: FloatVec, value: Float)

    private fn ov_add(a: FloatVec, b: FloatVec) -> FloatVec = unwrap_or(fv_add(a, b), a)
    private fn ov_sub(a: FloatVec, b: FloatVec) -> FloatVec = unwrap_or(fv_sub(a, b), a)
    private fn ov_dot(a: FloatVec, b: FloatVec) -> Float = unwrap_or(fv_dot(a, b), 0.0)

    // ---- Nelder–Mead ----
    private fn insert_vertex(vs: List<Vertex>, v: Vertex) -> List<Vertex> decreasing vs =
        if_then_else(is_empty(vs), list(v), match head(vs) { none => list(v), some(h) => if_then_else(fl_lt(field(v, fx), field(h, fx)), cons(v, vs), cons(h, insert_vertex(tail(vs), v))) })
    private fn sort_vertices(vs: List<Vertex>) -> List<Vertex> = fold(vs, empty_list(Vertex), insert_vertex)
    private fn initial_simplex(k: Nat, f: Fn<FloatVec, Float>, x0: FloatVec, step: Float, i: Nat, acc: List<Vertex>) -> List<Vertex> decreasing k =
        if_then_else(nat_eq(k, 0), acc,
            let x = unwrap_or(fv_set(x0, i, fl_add(unwrap_or(fv_get(x0, i), 0.0), step)), x0) in initial_simplex(pred(k), f, x0, step, i + 1, cons(Vertex(x, f(x)), acc)))
    private fn drop_last(vs: List<Vertex>) -> List<Vertex> decreasing vs =
        if_then_else(is_empty(vs), vs, if_then_else(is_empty(tail(vs)), empty_list(Vertex), match head(vs) { none => vs, some(h) => cons(h, drop_last(tail(vs))) }))
    private fn centroid(vs: List<Vertex>, n: Nat) -> FloatVec =
        fv_scale(fold(vs, unwrap_or(fv_new(n, 0.0), fv_empty()), fun(acc: FloatVec, v: Vertex, ov_add(acc, field(v, x)))), fl_div(1.0, fl_of_nat(length(vs))))
    private fn shrink(vs: List<Vertex>, best: Vertex, f: Fn<FloatVec, Float>) -> List<Vertex> =
        map(vs, fun(v: Vertex, let x = ov_add(field(best, x), fv_scale(ov_sub(field(v, x), field(best, x)), 0.5)) in Vertex(x, f(x))))
    private fn nm_result(vs: List<Vertex>, used: Nat) -> Minimum =
        let best = unwrap_or(head(sort_vertices(vs)), Vertex(fv_empty(), 0.0)) in Minimum(field(best, x), field(best, fx), used)
    private fn nm_loop(k: Nat, f: Fn<FloatVec, Float>, vs: List<Vertex>, n: Nat, tol: Float, used: Nat) -> Minimum decreasing k =
        if_then_else(nat_eq(k, 0), nm_result(vs, used),
        let s = sort_vertices(vs) in
        let best = unwrap_or(head(s), Vertex(fv_empty(), 0.0)) in
        let worst = unwrap_or(index(s, n), best) in
        let second = unwrap_or(index(s, monus(n, 1)), best) in
        if_then_else(fl_le(fl_sub(field(worst, fx), field(best, fx)), tol), Minimum(field(best, x), field(best, fx), used),
            let rest = drop_last(s) in let c = centroid(rest, n) in
            let xr = ov_add(c, ov_sub(c, field(worst, x))) in let fr = f(xr) in
            if_then_else(fl_lt(fr, field(best, fx)),
                let xe = ov_add(c, fv_scale(ov_sub(c, field(worst, x)), 2.0)) in let fe = f(xe) in
                nm_loop(pred(k), f, cons(if_then_else(fl_lt(fe, fr), Vertex(xe, fe), Vertex(xr, fr)), rest), n, tol, used + 1),
                if_then_else(fl_lt(fr, field(second, fx)), nm_loop(pred(k), f, cons(Vertex(xr, fr), rest), n, tol, used + 1),
                    let xc = ov_add(c, fv_scale(ov_sub(field(worst, x), c), 0.5)) in let fc = f(xc) in
                    if_then_else(fl_lt(fc, field(worst, fx)), nm_loop(pred(k), f, cons(Vertex(xc, fc), rest), n, tol, used + 1),
                        nm_loop(pred(k), f, shrink(s, best, f), n, tol, used + 1))))))
    // derivative-free minimization from x0; stops when the simplex values differ by <= tol
    fn nelder_mead(f: Fn<FloatVec, Float>, x0: FloatVec, step: Float, tol: Float, iterations: Nat) -> Minimum =
        nm_loop(iterations, f, cons(Vertex(x0, f(x0)), initial_simplex(fv_len(x0), f, x0, step, 0, empty_list(Vertex))), fv_len(x0), tol, 0)

    // ---- BFGS ----
    private fn backtrack(k: Nat, f: Fn<FloatVec, Float>, x: FloatVec, fx: Float, p: FloatVec, slope: Float, a: Float) -> Float decreasing k =
        if_then_else(nat_eq(k, 0), a, if_then_else(fl_le(f(ov_add(x, fv_scale(p, a))), fl_add(fx, fl_mul(fl_mul(0.0001, a), slope))), a, backtrack(pred(k), f, x, fx, p, slope, fl_mul(0.5, a))))
    private fn outer(a: FloatVec, b: FloatVec) -> FloatVec = unwrap_or(fv_matmul(a, b, fv_len(a), 1, fv_len(b)), fv_empty())
    private fn bfgs_update(h: FloatVec, s: FloatVec, y: FloatVec, n: Nat) -> FloatVec =
        let sy = ov_dot(s, y) in
        if_then_else(fl_le(sy, 0.0), h,
            let rho = fl_div(1.0, sy) in let id = unwrap_or(la_identity(n), h) in
            let l = ov_sub(id, fv_scale(outer(s, y), rho)) in let r = ov_sub(id, fv_scale(outer(y, s), rho)) in
            ov_add(unwrap_or(fv_matmul(unwrap_or(fv_matmul(l, h, n, n, n), h), r, n, n, n), h), fv_scale(outer(s, s), rho)))
    private fn bfgs_loop(k: Nat, f: Fn<FloatVec, Float>, g: Fn<FloatVec, FloatVec>, x: FloatVec, h: FloatVec, n: Nat, tol: Float, used: Nat) -> Minimum decreasing k =
        if_then_else(nat_eq(k, 0), Minimum(x, f(x), used),
        let gx = g(x) in let fx = f(x) in
        if_then_else(fl_le(fv_norm(gx), tol), Minimum(x, fx, used),
            let p = fv_scale(unwrap_or(fv_matmul(h, gx, n, n, 1), gx), -1.0) in let slope = ov_dot(gx, p) in
            let p2 = if_then_else(fl_lt(slope, 0.0), p, fv_scale(gx, -1.0)) in let slope2 = ov_dot(gx, p2) in
            let a = backtrack(60, f, x, fx, p2, slope2, 1.0) in
            let s = fv_scale(p2, a) in let x2 = ov_add(x, s) in
            bfgs_loop(pred(k), f, g, x2, bfgs_update(if_then_else(fl_lt(slope, 0.0), h, unwrap_or(la_identity(n), h)), s, ov_sub(g(x2), gx), n), n, tol, used + 1)))
    // quasi-Newton minimization with a supplied gradient
    fn bfgs(f: Fn<FloatVec, Float>, grad: Fn<FloatVec, FloatVec>, x0: FloatVec, tol: Float, iterations: Nat) -> Minimum =
        bfgs_loop(iterations, f, grad, x0, unwrap_or(la_identity(fv_len(x0)), fv_empty()), fv_len(x0), tol, 0)
    // BFGS with central-difference gradients (step h)
    fn bfgs_numeric(f: Fn<FloatVec, Float>, x0: FloatVec, h: Float, tol: Float, iterations: Nat) -> Minimum =
        bfgs(f, fun(x: FloatVec, gradient(f, x, h)), x0, tol, iterations)

    // ---- Levenberg–Marquardt: minimize |r(p)|^2 ----
    private fn damped(jtj: FloatVec, n: Nat, lambda: Float) -> FloatVec =
        ov_add(jtj, fv_scale(unwrap_or(fv_mul(unwrap_or(la_identity(n), jtj), fv_scale(jtj, 1.0)), jtj), lambda))
    private fn lm_loop(k: Nat, r: Fn<FloatVec, FloatVec>, p: FloatVec, lambda: Float, h: Float, tol: Float, used: Nat) -> Minimum decreasing k =
        if_then_else(nat_eq(k, 0), Minimum(p, unwrap_or(fv_dot(r(p), r(p)), 0.0), used),
        let res = r(p) in let c = unwrap_or(fv_dot(res, res), 0.0) in
        if_then_else(fl_le(c, tol), Minimum(p, c, used),
            let j = jacobian(r, p, h) in let n = fv_len(p) in let m = field(j, rows) in
            let jt = unwrap_or(fv_transpose(field(j, data), m, n), fv_empty()) in
            let jtj = unwrap_or(fv_matmul(jt, field(j, data), n, m, n), fv_empty()) in
            let jtr = fv_scale(unwrap_or(fv_matmul(jt, res, n, m, 1), fv_empty()), -1.0) in
            match fv_solve(damped(jtj, n, lambda), jtr, n) { none => Minimum(p, c, used), some(delta) =>
                let p2 = ov_add(p, delta) in let c2 = unwrap_or(fv_dot(r(p2), r(p2)), c) in
                if_then_else(fl_lt(c2, c),
                    if_then_else(fl_le(fl_sub(c, c2), fl_mul(tol, c)), Minimum(p2, c2, used + 1), lm_loop(pred(k), r, p2, fl_mul(lambda, 0.3), h, tol, used + 1)),
                    lm_loop(pred(k), r, p, fl_mul(lambda, 10.0), h, tol, used + 1)) }))
    // nonlinear least squares from p0 with numerical Jacobians
    fn levenberg_marquardt(residuals: Fn<FloatVec, FloatVec>, p0: FloatVec, tol: Float, iterations: Nat) -> Minimum =
        lm_loop(iterations, residuals, p0, 0.001, fl_lit("1e-6"), tol, 0)

    // ---- linear programming: maximize c·x subject to A x <= b, x >= 0 ----
    fn linear_program(c: FloatVec, a: Matrix, b: FloatVec) -> LpResult =
        let s = lp_status(c, field(a, data), b, field(a, rows), field(a, cols)) in
        match lp_solve(c, field(a, data), b, field(a, rows), field(a, cols)) { none => LpResult(s, fv_empty(), fl_lit("nan")), some(x) => LpResult(s, x, ov_dot(c, x)) }
    fn lp_optimal(r: LpResult) -> Bool = nat_eq(field(r, status), 0)
    fn lp_infeasible(r: LpResult) -> Bool = nat_eq(field(r, status), 1)
    fn lp_unbounded(r: LpResult) -> Bool = nat_eq(field(r, status), 2)

    // ---- integer programming (v0.121): maximize c·x, A x <= b, x >= 0, x_j integral where integer[j] = 1 ----
    fn integer_program(c: FloatVec, a: Matrix, b: FloatVec, integer: NatVec) -> LpResult =
        let s = ilp_status(c, field(a, data), b, field(a, rows), field(a, cols), integer) in
        match ilp_solve(c, field(a, data), b, field(a, rows), field(a, cols), integer) { none => LpResult(s, fv_empty(), fl_lit("nan")), some(x) => LpResult(s, x, ov_dot(c, x)) }
    fn all_integer(n: Nat) -> NatVec = unwrap_or(nv_new(n, 1), nv_empty())
    // 0/1 knapsack: maximize values·x with weights·x <= capacity
    fn knapsack(values: FloatVec, weights: FloatVec, capacity: Float) -> LpResult =
        let n = fv_len(values) in
        let id = unwrap_or(la_identity(n), fv_empty()) in
        integer_program(values, Matrix(n + 1, n, unwrap_or(fv_concat(weights, id), weights)), unwrap_or(fv_concat(unwrap_or(fv_new(1, capacity), fv_empty()), unwrap_or(fv_new(n, 1.0), fv_empty())), fv_empty()), all_integer(n))

    theorem rosenbrock_minimum_is_zero: fl_eq(fl_add(fl_mul(100.0, fl_mul(fl_sub(1.0, fl_mul(1.0, 1.0)), fl_sub(1.0, fl_mul(1.0, 1.0)))), fl_mul(fl_sub(1.0, 1.0), fl_sub(1.0, 1.0))), 0.0)
}
