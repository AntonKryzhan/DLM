// Machine-float numerical methods (v0.114, docs/FLOATS.md): roots, integration, ODEs, optimization,
// interpolation, statistics, random numbers, linear systems. Floats are IEEE-754 binary64; every
// method is iterative with an explicit iteration bound and reports failure as `none`.
module std.numerics

theory StdNumerics {
    record Draw(value: Float, state: Nat)
    record LinearFit(slope: Float, intercept: Float)

    fn fl_half(x: Float) -> Float = fl_mul(x, 0.5)
    fn fl_mid(a: Float, b: Float) -> Float = let s = fl_add(a, b) in if_then_else(fl_is_finite(s), fl_half(s), fl_add(fl_half(a), fl_half(b)))
    fn fl_close(a: Float, b: Float, tol: Float) -> Bool = fl_le(fl_abs(fl_sub(a, b)), tol)
    fn fl_sign_change(a: Float, b: Float) -> Bool = or(and(fl_le(a, 0.0), fl_le(0.0, b)), and(fl_le(b, 0.0), fl_le(0.0, a)))

    // ---- roots ----
    private fn bisect_loop(n: Nat, f: Fn<Float, Float>, a: Float, b: Float, fa: Float, tol: Float) -> Option<Float> decreasing n =
        if_then_else(nat_eq(n, 0), none(Float),
            let m = fl_mid(a, b) in let fm = f(m) in
            if_then_else(not(fl_is_finite(fm)), none(Float),
                if_then_else(or(fl_le(fl_sub(b, a), tol), fl_eq(fm, 0.0)), some(m),
                    if_then_else(or(fl_eq(m, a), fl_eq(m, b)), none(Float),
                        if_then_else(fl_sign_change(fa, fm), bisect_loop(pred(n), f, a, m, fa, tol), bisect_loop(pred(n), f, m, b, fm, tol))))))
    // a root of f in [a, b] with f(a)·f(b) <= 0, to width tol
    fn bisection(f: Fn<Float, Float>, a: Float, b: Float, tol: Float, iterations: Nat) -> Option<Float> =
        if_then_else(and(and(fl_is_finite(a), fl_is_finite(b)), and(fl_le(a, b), and(fl_is_finite(tol), fl_lt(0.0, tol)))),
            let fa = f(a) in let fb = f(b) in
            if_then_else(and(fl_is_finite(fa), fl_is_finite(fb)),
                if_then_else(fl_eq(fa, 0.0), some(a), if_then_else(fl_eq(fb, 0.0), some(b),
                    if_then_else(fl_sign_change(fa, fb), bisect_loop(iterations, f, a, b, fa, tol), none(Float)))), none(Float)), none(Float))
    private fn newton_loop(n: Nat, f: Fn<Float, Float>, df: Fn<Float, Float>, x: Float, tol: Float) -> Option<Float> decreasing n =
        if_then_else(nat_eq(n, 0), none(Float),
            let d = df(x) in
            if_then_else(or(fl_eq(d, 0.0), not(fl_is_finite(d))), none(Float),
                let y = fl_sub(x, fl_div(f(x), d)) in
                if_then_else(fl_close(x, y, tol), some(y), newton_loop(pred(n), f, df, y, tol))))
    fn newton(f: Fn<Float, Float>, df: Fn<Float, Float>, x0: Float, tol: Float, iterations: Nat) -> Option<Float> = newton_loop(iterations, f, df, x0, tol)
    private fn secant_loop(n: Nat, f: Fn<Float, Float>, x0: Float, x1: Float, tol: Float) -> Option<Float> decreasing n =
        if_then_else(nat_eq(n, 0), none(Float),
            let d = fl_sub(f(x1), f(x0)) in
            if_then_else(fl_eq(d, 0.0), none(Float),
                let x2 = fl_sub(x1, fl_div(fl_mul(f(x1), fl_sub(x1, x0)), d)) in
                if_then_else(fl_close(x1, x2, tol), some(x2), secant_loop(pred(n), f, x1, x2, tol))))
    fn secant(f: Fn<Float, Float>, x0: Float, x1: Float, tol: Float, iterations: Nat) -> Option<Float> = secant_loop(iterations, f, x0, x1, tol)

    // ---- calculus ----
    fn derivative(f: Fn<Float, Float>, x: Float, h: Float) -> Float = fl_div(fl_sub(f(fl_add(x, h)), f(fl_sub(x, h))), fl_mul(2.0, h))
    private fn trap_loop(k: Nat, f: Fn<Float, Float>, a: Float, h: Float, i: Nat, acc: Float) -> Float decreasing k =
        if_then_else(nat_eq(k, 0), acc, trap_loop(pred(k), f, a, h, i + 1, fl_add(acc, f(fl_add(a, fl_mul(fl_of_nat(i), h))))))
    fn trapezoid(f: Fn<Float, Float>, a: Float, b: Float, n: Nat) -> Float =
        let m = if_then_else(nat_eq(n, 0), 1, n) in let h = fl_div(fl_sub(b, a), fl_of_nat(m)) in
        fl_mul(h, fl_add(fl_half(fl_add(f(a), f(b))), trap_loop(monus(m, 1), f, a, h, 1, 0.0)))
    private fn simpson_loop(k: Nat, f: Fn<Float, Float>, a: Float, h: Float, i: Nat, acc: Float) -> Float decreasing k =
        if_then_else(nat_eq(k, 0), acc,
            simpson_loop(pred(k), f, a, h, i + 1, fl_add(acc, fl_mul(if_then_else(nat_eq(mod(i, 2), 1), 4.0, 2.0), f(fl_add(a, fl_mul(fl_of_nat(i), h)))))))
    // composite Simpson rule with n (made even) subintervals
    fn simpson(f: Fn<Float, Float>, a: Float, b: Float, n: Nat) -> Float =
        let m = if_then_else(nat_eq(mod(n, 2), 1), n + 1, if_then_else(nat_eq(n, 0), 2, n)) in
        let h = fl_div(fl_sub(b, a), fl_of_nat(m)) in
        fl_mul(fl_div(h, 3.0), fl_add(fl_add(f(a), f(b)), simpson_loop(monus(m, 1), f, a, h, 1, 0.0)))

    // ---- ODEs: y' = f(t, y) ----
    fn rk4_step(f: Fn<Float, Float, Float>, t: Float, y: Float, h: Float) -> Float =
        let k1 = f(t, y) in
        let k2 = f(fl_add(t, fl_half(h)), fl_add(y, fl_mul(fl_half(h), k1))) in
        let k3 = f(fl_add(t, fl_half(h)), fl_add(y, fl_mul(fl_half(h), k2))) in
        let k4 = f(fl_add(t, h), fl_add(y, fl_mul(h, k3))) in
        fl_add(y, fl_mul(fl_div(h, 6.0), fl_add(fl_add(k1, fl_mul(2.0, k2)), fl_add(fl_mul(2.0, k3), k4))))
    fn rk4(f: Fn<Float, Float, Float>, t0: Float, y0: Float, h: Float, steps: Nat) -> Float decreasing steps =
        if_then_else(nat_eq(steps, 0), y0, rk4(f, fl_add(t0, h), rk4_step(f, t0, y0, h), h, pred(steps)))
    private fn vadd(a: FloatVec, b: FloatVec) -> FloatVec = unwrap_or(fv_add(a, b), a)
    // systems: y' = f(t, y) with y a FloatVec
    fn rk4_vec_step(f: Fn<Float, FloatVec, FloatVec>, t: Float, y: FloatVec, h: Float) -> FloatVec =
        let k1 = f(t, y) in
        let k2 = f(fl_add(t, fl_half(h)), vadd(y, fv_scale(k1, fl_half(h)))) in
        let k3 = f(fl_add(t, fl_half(h)), vadd(y, fv_scale(k2, fl_half(h)))) in
        let k4 = f(fl_add(t, h), vadd(y, fv_scale(k3, h))) in
        vadd(y, fv_scale(vadd(vadd(k1, fv_scale(k2, 2.0)), vadd(fv_scale(k3, 2.0), k4)), fl_div(h, 6.0)))
    fn rk4_vec(f: Fn<Float, FloatVec, FloatVec>, t0: Float, y0: FloatVec, h: Float, steps: Nat) -> FloatVec decreasing steps =
        if_then_else(nat_eq(steps, 0), y0, rk4_vec(f, fl_add(t0, h), rk4_vec_step(f, t0, y0, h), h, pred(steps)))

    // ---- optimization ----
    private fn golden_loop(n: Nat, f: Fn<Float, Float>, a: Float, b: Float, tol: Float) -> Float decreasing n =
        if_then_else(nat_eq(n, 0), fl_mid(a, b),
            if_then_else(fl_le(fl_sub(b, a), tol), fl_mid(a, b),
                let r = 0.6180339887498949 in
                let c = fl_sub(b, fl_mul(r, fl_sub(b, a))) in let d = fl_add(a, fl_mul(r, fl_sub(b, a))) in
                if_then_else(fl_lt(f(c), f(d)), golden_loop(pred(n), f, a, d, tol), golden_loop(pred(n), f, c, b, tol))))
    // minimum of a unimodal f on [a, b]
    fn golden_min(f: Fn<Float, Float>, a: Float, b: Float, tol: Float, iterations: Nat) -> Float = golden_loop(iterations, f, a, b, tol)
    private fn gd_loop(n: Nat, df: Fn<Float, Float>, x: Float, rate: Float, tol: Float) -> Float decreasing n =
        if_then_else(nat_eq(n, 0), x, let y = fl_sub(x, fl_mul(rate, df(x))) in if_then_else(fl_close(x, y, tol), y, gd_loop(pred(n), df, y, rate, tol)))
    fn gradient_descent(df: Fn<Float, Float>, x0: Float, rate: Float, tol: Float, iterations: Nat) -> Float = gd_loop(iterations, df, x0, rate, tol)

    // ---- interpolation ----
    fn horner(coefficients: FloatVec, x: Float) -> Float =
        fold(fv_to_list(coefficients), 0.0, fun(acc: Float, c: Float, fl_add(fl_mul(acc, x), c)))
    private fn interp_at(k: Nat, xs: FloatVec, ys: FloatVec, x: Float, i: Nat) -> Option<Float> decreasing k =
        if_then_else(nat_eq(k, 0), none(Float),
            match fv_get(xs, i) { none => none(Float), some(x0) => match fv_get(xs, i + 1) { none => none(Float), some(x1) =>
                if_then_else(and(fl_le(x0, x), fl_le(x, x1)),
                    match fv_get(ys, i) { none => none(Float), some(y0) => match fv_get(ys, i + 1) { none => none(Float), some(y1) =>
                        some(if_then_else(fl_eq(x0, x1), y0, fl_add(y0, fl_mul(fl_sub(y1, y0), fl_div(fl_sub(x, x0), fl_sub(x1, x0)))))) } },
                    interp_at(pred(k), xs, ys, x, i + 1)) } })
    // piecewise linear interpolation over increasing xs; none outside [xs[0], xs[n-1]]
    fn interpolate(xs: FloatVec, ys: FloatVec, x: Float) -> Option<Float> =
        if_then_else(nat_eq(fv_len(xs), fv_len(ys)), interp_at(fv_len(xs), xs, ys, x, 0), none(Float))

    // ---- statistics ----
    fn mean(xs: FloatVec) -> Option<Float> = if_then_else(nat_eq(fv_len(xs), 0), none(Float), some(fl_div(fv_sum(xs), fl_of_nat(fv_len(xs)))))
    fn variance(xs: FloatVec) -> Option<Float> =
        match mean(xs) { none => none(Float), some(m) => if_then_else(2 > fv_len(xs), none(Float),
            some(fl_div(fold(fv_to_list(xs), 0.0, fun(acc: Float, x: Float, fl_add(acc, fl_mul(fl_sub(x, m), fl_sub(x, m))))), fl_of_nat(monus(fv_len(xs), 1))))) }
    fn stddev(xs: FloatVec) -> Option<Float> = match variance(xs) { none => none(Float), some(v) => some(fl_sqrt(v)) }
    fn covariance(xs: FloatVec, ys: FloatVec) -> Option<Float> =
        match mean(xs) { none => none(Float), some(mx) => match mean(ys) { none => none(Float), some(my) =>
            if_then_else(or(2 > fv_len(xs), not(nat_eq(fv_len(xs), fv_len(ys)))), none(Float),
                match fv_dot(fv_scale(unwrap_or(fv_sub(xs, unwrap_or(fv_new(fv_len(xs), mx), xs)), xs), 1.0), unwrap_or(fv_sub(ys, unwrap_or(fv_new(fv_len(ys), my), ys)), ys)) {
                    none => none(Float), some(s) => some(fl_div(s, fl_of_nat(monus(fv_len(xs), 1)))) }) } }
    fn correlation(xs: FloatVec, ys: FloatVec) -> Option<Float> =
        match covariance(xs, ys) { none => none(Float), some(c) => match stddev(xs) { none => none(Float), some(sx) => match stddev(ys) { none => none(Float), some(sy) =>
            if_then_else(fl_eq(fl_mul(sx, sy), 0.0), none(Float), some(fl_div(c, fl_mul(sx, sy)))) } } }
    // least squares y = slope·x + intercept
    fn linear_fit(xs: FloatVec, ys: FloatVec) -> Option<LinearFit> =
        match covariance(xs, ys) { none => none(LinearFit), some(c) => match variance(xs) { none => none(LinearFit), some(v) =>
            match mean(xs) { none => none(LinearFit), some(mx) => match mean(ys) { none => none(LinearFit), some(my) =>
                if_then_else(fl_eq(v, 0.0), none(LinearFit), let s = fl_div(c, v) in some(LinearFit(s, fl_sub(my, fl_mul(s, mx))))) } } } }
    private fn insert_sorted(xs: List<Float>, x: Float) -> List<Float> decreasing xs =
        if_then_else(is_empty(xs), list(x), match head(xs) { none => list(x), some(h) => if_then_else(fl_lt(h, x), cons(h, insert_sorted(tail(xs), x)), cons(x, xs)) })
    fn sorted(xs: FloatVec) -> FloatVec = unwrap_or(fv_from_list(fold(fv_to_list(xs), empty_list(Float), insert_sorted)), xs)
    fn median(xs: FloatVec) -> Option<Float> =
        let s = sorted(xs) in let n = fv_len(s) in
        if_then_else(nat_eq(n, 0), none(Float), if_then_else(nat_eq(mod(n, 2), 1), fv_get(s, div(n, 2)),
            match fv_get(s, monus(div(n, 2), 1)) { none => none(Float), some(a) => match fv_get(s, div(n, 2)) { none => none(Float), some(b) => some(fl_mid(a, b)) } }))

    // ---- random numbers: 64-bit LCG (Knuth MMIX constants), state in Nat ----
    fn rng_next(state: Nat) -> Nat = mod(mul(mod(state, 18446744073709551616), 6364136223846793005) + 1442695040888963407, 18446744073709551616)
    // uniform in [0, 1) from the top 53 bits
    fn rng_uniform(state: Nat) -> Draw = let s = rng_next(state) in Draw(fl_div(fl_of_nat(div(s, 2048)), 9007199254740992.0), s)
    fn rng_range(state: Nat, a: Float, b: Float) -> Draw = let d = rng_uniform(state) in Draw(fl_add(a, fl_mul(field(d, value), fl_sub(b, a))), field(d, state))
    // standard normal by Box–Muller, then mean + sd·z
    fn rng_normal(state: Nat, mu: Float, sd: Float) -> Draw =
        let u1 = rng_uniform(state) in let u2 = rng_uniform(field(u1, state)) in
        let r = fl_sqrt(fl_mul(-2.0, fl_log(fl_sub(1.0, field(u1, value))))) in
        Draw(fl_add(mu, fl_mul(sd, fl_mul(r, fl_cos(fl_mul(6.283185307179586, field(u2, value)))))), field(u2, state))
    private fn sample_loop(n: Nat, state: Nat, acc: List<Float>) -> List<Float> decreasing n =
        if_then_else(nat_eq(n, 0), acc, let d = rng_uniform(state) in sample_loop(pred(n), field(d, state), cons(field(d, value), acc)))
    // n uniform draws in [0, 1)
    fn rng_samples(state: Nat, n: Nat) -> FloatVec = unwrap_or(fv_from_list(sample_loop(n, state, empty_list(Float))), fv_empty())

    // ---- linear systems ----
    fn solve_linear(a: FloatVec, b: FloatVec, n: Nat) -> Option<FloatVec> = fv_solve(a, b, n)
    fn residual_norm(a: FloatVec, x: FloatVec, b: FloatVec, n: Nat) -> Option<Float> =
        match fv_matmul(a, x, n, n, 1) { none => none(Float), some(ax) => match fv_sub(ax, b) { none => none(Float), some(r) => some(fv_norm(r)) } }

    theorem midpoint_of_one_and_three: fl_eq(fl_mid(1.0, 3.0), 2.0)
}
