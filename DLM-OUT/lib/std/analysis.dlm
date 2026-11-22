// Analysis on machine floats (v0.117, docs/ANALYSIS.md): forward-mode automatic differentiation,
// numerical gradients and Jacobians, Gauss–Legendre and adaptive quadrature, adaptive Runge–Kutta
// (Dormand–Prince 5(4)), natural cubic splines, polynomials and their complex roots, Fourier helpers.
module std.analysis
import std.linalg

theory StdAnalysis extends StdLinalg {
    record Dual(v: Float, d: Float)
    record Cx(re: Float, im: Float)
    record Spline(xs: FloatVec, ys: FloatVec, ms: FloatVec)

    // ---- dual numbers: value and derivative ----
    fn d_const(x: Float) -> Dual = Dual(x, 0.0)
    fn d_var(x: Float) -> Dual = Dual(x, 1.0)
    fn d_add(a: Dual, b: Dual) -> Dual = Dual(fl_add(field(a, v), field(b, v)), fl_add(field(a, d), field(b, d)))
    fn d_sub(a: Dual, b: Dual) -> Dual = Dual(fl_sub(field(a, v), field(b, v)), fl_sub(field(a, d), field(b, d)))
    fn d_mul(a: Dual, b: Dual) -> Dual = Dual(fl_mul(field(a, v), field(b, v)), fl_add(fl_mul(field(a, d), field(b, v)), fl_mul(field(a, v), field(b, d))))
    fn d_div(a: Dual, b: Dual) -> Dual =
        Dual(fl_div(field(a, v), field(b, v)), fl_div(fl_sub(fl_mul(field(a, d), field(b, v)), fl_mul(field(a, v), field(b, d))), fl_mul(field(b, v), field(b, v))))
    fn d_neg(a: Dual) -> Dual = Dual(fl_neg(field(a, v)), fl_neg(field(a, d)))
    fn d_scale(a: Dual, k: Float) -> Dual = Dual(fl_mul(field(a, v), k), fl_mul(field(a, d), k))
    fn d_sin(a: Dual) -> Dual = Dual(fl_sin(field(a, v)), fl_mul(field(a, d), fl_cos(field(a, v))))
    fn d_cos(a: Dual) -> Dual = Dual(fl_cos(field(a, v)), fl_neg(fl_mul(field(a, d), fl_sin(field(a, v)))))
    fn d_exp(a: Dual) -> Dual = let e = fl_exp(field(a, v)) in Dual(e, fl_mul(field(a, d), e))
    fn d_log(a: Dual) -> Dual = Dual(fl_log(field(a, v)), fl_div(field(a, d), field(a, v)))
    fn d_sqrt(a: Dual) -> Dual = let s = fl_sqrt(field(a, v)) in Dual(s, fl_div(field(a, d), fl_mul(2.0, s)))
    fn d_pow(a: Dual, p: Float) -> Dual = if_then_else(fl_eq(p, 0.0), Dual(1.0, 0.0), Dual(fl_pow(field(a, v), p), fl_mul(field(a, d), fl_mul(p, fl_pow(field(a, v), fl_sub(p, 1.0))))))
    fn d_atan(a: Dual) -> Dual = Dual(fl_atan(field(a, v)), fl_div(field(a, d), fl_add(1.0, fl_mul(field(a, v), field(a, v)))))
    // exact derivative of f at x by forward-mode AD
    fn ad_derivative(f: Fn<Dual, Dual>, x: Float) -> Float = field(f(d_var(x)), d)
    fn ad_value_and_derivative(f: Fn<Dual, Dual>, x: Float) -> Dual = f(d_var(x))
    // Newton's method with AD derivatives
    private fn ad_newton_loop(k: Nat, f: Fn<Dual, Dual>, x: Float, tol: Float) -> Option<Float> decreasing k =
        if_then_else(nat_eq(k, 0), none(Float), let r = f(d_var(x)) in
            if_then_else(fl_eq(field(r, d), 0.0), none(Float), let y = fl_sub(x, fl_div(field(r, v), field(r, d))) in
                if_then_else(fl_le(fl_abs(fl_sub(y, x)), tol), some(y), ad_newton_loop(pred(k), f, y, tol))))
    fn ad_newton(f: Fn<Dual, Dual>, x0: Float, tol: Float, iterations: Nat) -> Option<Float> = ad_newton_loop(iterations, f, x0, tol)

    // ---- numerical gradients ----
    private fn bump(x: FloatVec, i: Nat, h: Float) -> FloatVec = unwrap_or(fv_set(x, i, fl_add(unwrap_or(fv_get(x, i), 0.0), h)), x)
    private fn grad_loop(k: Nat, f: Fn<FloatVec, Float>, x: FloatVec, h: Float, i: Nat, acc: List<Float>) -> List<Float> decreasing k =
        if_then_else(nat_eq(k, 0), acc,
            grad_loop(pred(k), f, x, h, i + 1, cons(fl_div(fl_sub(f(bump(x, i, h)), f(bump(x, i, fl_neg(h)))), fl_mul(2.0, h)), acc)))
    private fn rev_floats(xs: List<Float>) -> List<Float> = fold(xs, empty_list(Float), fun(acc: List<Float>, x: Float, cons(x, acc)))
    // central-difference gradient
    fn gradient(f: Fn<FloatVec, Float>, x: FloatVec, h: Float) -> FloatVec = unwrap_or(fv_from_list(rev_floats(grad_loop(fv_len(x), f, x, h, 0, empty_list(Float)))), fv_empty())
    private fn jac_cols(k: Nat, f: Fn<FloatVec, FloatVec>, x: FloatVec, h: Float, i: Nat, acc: FloatVec) -> FloatVec decreasing k =
        if_then_else(nat_eq(k, 0), acc,
            jac_cols(pred(k), f, x, h, i + 1, unwrap_or(fv_concat(acc, fv_scale(unwrap_or(fv_sub(f(bump(x, i, h)), f(bump(x, i, fl_neg(h)))), fv_empty()), fl_div(1.0, fl_mul(2.0, h)))), acc)))
    // central-difference Jacobian (m × n) of f: R^n -> R^m
    fn jacobian(f: Fn<FloatVec, FloatVec>, x: FloatVec, h: Float) -> Matrix =
        let n = fv_len(x) in let m = fv_len(f(x)) in
        Matrix(m, n, unwrap_or(fv_transpose(jac_cols(n, f, x, h, 0, fv_empty()), n, m), fv_empty()))

    // ---- quadrature ----
    fn gauss_legendre(f: Fn<Float, Float>, a: Float, b: Float) -> Float =
        let c = fl_mid(a, b) in let r = fl_half(fl_sub(b, a)) in
        let node = fun(t: Float, w: Float, fl_mul(w, f(fl_add(c, fl_mul(r, t))))) in
        fl_mul(r, fl_add(fl_add(fl_add(node(0.0, 0.5688888888888889), fl_add(node(-0.5384693101056831, 0.4786286704993665), node(0.5384693101056831, 0.4786286704993665))),
            node(-0.906179845938664, 0.23692688505618908)), node(0.906179845938664, 0.23692688505618908)))
    private fn gl_loop(k: Nat, f: Fn<Float, Float>, a: Float, h: Float, i: Nat, acc: Float) -> Float decreasing k =
        if_then_else(nat_eq(k, 0), acc, let x0 = fl_add(a, fl_mul(fl_of_nat(i), h)) in gl_loop(pred(k), f, a, h, i + 1, fl_add(acc, gauss_legendre(f, x0, fl_add(x0, h)))))
    // composite 5-point Gauss–Legendre over n panels
    fn integrate(f: Fn<Float, Float>, a: Float, b: Float, n: Nat) -> Float =
        let m = if_then_else(nat_eq(n, 0), 1, n) in gl_loop(m, f, a, fl_div(fl_sub(b, a), fl_of_nat(m)), 0, 0.0)
    private fn simpson_panel(f: Fn<Float, Float>, a: Float, b: Float) -> Float = fl_mul(fl_div(fl_sub(b, a), 6.0), fl_add(fl_add(f(a), fl_mul(4.0, f(fl_mid(a, b)))), f(b)))
    private fn asr_corrected(f: Fn<Float, Float>, a: Float, b: Float, whole: Float) -> Float =
        let m = fl_mid(a, b) in let l = simpson_panel(f, a, m) in let r = simpson_panel(f, m, b) in fl_add(fl_add(l, r), fl_div(fl_sub(fl_add(l, r), whole), 15.0))
    private fn asr(depth: Nat, f: Fn<Float, Float>, a: Float, b: Float, whole: Float, tol: Float) -> Float decreasing depth =
        if_then_else(nat_eq(depth, 0), asr_corrected(f, a, b, whole),
            let m = fl_mid(a, b) in let l = simpson_panel(f, a, m) in let r = simpson_panel(f, m, b) in let delta = fl_sub(fl_add(l, r), whole) in
            if_then_else(fl_le(fl_abs(delta), fl_mul(15.0, tol)), fl_add(fl_add(l, r), fl_div(delta, 15.0)),
                fl_add(asr(pred(depth), f, a, m, l, fl_half(tol)), asr(pred(depth), f, m, b, r, fl_half(tol)))))
    // adaptive Simpson with Richardson correction, at most `depth` levels
    fn integrate_adaptive(f: Fn<Float, Float>, a: Float, b: Float, tol: Float, depth: Nat) -> Float = asr(depth, f, a, b, simpson_panel(f, a, b), tol)

    // ---- adaptive ODE: y' = f(t, y), Dormand–Prince 5(4) ----
    fn dopri_step(f: Fn<Float, Float, Float>, t: Float, y: Float, h: Float) -> ProductType<Float*Float> =
        let k1 = f(t, y) in
        let k2 = f(fl_add(t, fl_mul(0.2, h)), fl_add(y, fl_mul(h, fl_mul(0.2, k1)))) in
        let k3 = f(fl_add(t, fl_mul(0.3, h)), fl_add(y, fl_mul(h, fl_add(fl_mul(0.075, k1), fl_mul(0.225, k2))))) in
        let k4 = f(fl_add(t, fl_mul(0.8, h)), fl_add(y, fl_mul(h, fl_add(fl_sub(fl_mul(0.9777777777777777, k1), fl_mul(3.7333333333333334, k2)), fl_mul(3.5555555555555554, k3))))) in
        let k5 = f(fl_add(t, fl_mul(0.8888888888888888, h)), fl_add(y, fl_mul(h, fl_add(fl_add(fl_sub(fl_mul(2.9525986892242035, k1), fl_mul(11.595793324188385, k2)), fl_mul(9.822892851699436, k3)), fl_mul(-0.2908093278463649, k4))))) in
        let k6 = f(fl_add(t, h), fl_add(y, fl_mul(h, fl_add(fl_add(fl_add(fl_sub(fl_mul(2.8462752525252526, k1), fl_mul(10.757575757575758, k2)), fl_mul(8.906422717743473, k3)), fl_mul(0.2784090909090909, k4)), fl_mul(-0.2735313036020583, k5))))) in
        let y5 = fl_add(y, fl_mul(h, fl_add(fl_add(fl_add(fl_add(fl_mul(0.09114583333333333, k1), fl_mul(0.44923629829290207, k3)), fl_mul(0.6510416666666666, k4)), fl_mul(-0.322376179245283, k5)), fl_mul(0.13095238095238096, k6)))) in
        let k7 = f(fl_add(t, h), y5) in
        let err = fl_mul(h, fl_add(fl_add(fl_add(fl_add(fl_add(fl_mul(0.0012326388888888888, k1), fl_mul(-0.0042527702905061394, k3)), fl_mul(0.03697916666666667, k4)), fl_mul(-0.05086379716981132, k5)), fl_mul(0.0419047619047619, k6)), fl_mul(-0.025, k7))) in
        pair(y5, fl_abs(err))
    private fn ode_loop(k: Nat, f: Fn<Float, Float, Float>, t: Float, y: Float, t1: Float, h: Float, tol: Float) -> Option<Float> decreasing k =
        if_then_else(fl_eq(t1, t), some(y), if_then_else(nat_eq(k, 0), none(Float),
            let hh = if_then_else(fl_lt(t, t1), fl_min(fl_abs(h), fl_sub(t1, t)), fl_max(fl_neg(fl_abs(h)), fl_sub(t1, t))) in
            if_then_else(or(fl_eq(fl_add(t, hh), t), not(fl_is_finite(hh))), none(Float),
            let s = dopri_step(f, t, y, hh) in let e = snd(s) in
            if_then_else(not(and(fl_is_finite(e), fl_is_finite(fst(s)))), none(Float),
            let factor = if_then_else(fl_eq(e, 0.0), 5.0, fl_min(5.0, fl_max(0.2, fl_mul(0.9, fl_pow(fl_div(tol, e), 0.2))))) in
            if_then_else(fl_le(e, tol), ode_loop(pred(k), f, fl_add(t, hh), fst(s), t1, fl_mul(hh, factor), tol), ode_loop(pred(k), f, t, y, t1, fl_mul(hh, factor), tol))))))
    // y(t1) for y(t0) = y0 with per-step error tolerance; none after `steps` attempts
    fn ode_solve(f: Fn<Float, Float, Float>, t0: Float, y0: Float, t1: Float, tol: Float, steps: Nat) -> Option<Float> =
        if_then_else(and(and(fl_is_finite(t0), fl_is_finite(t1)), and(fl_is_finite(y0), and(fl_is_finite(tol), fl_lt(0.0, tol)))),
            ode_loop(steps, f, t0, y0, t1, fl_mul(0.01, fl_sub(t1, t0)), tol), none(Float))

    // ---- natural cubic splines ----
    private fn at(v: FloatVec, i: Nat) -> Float = unwrap_or(fv_get(v, i), 0.0)
    private fn spline_rows(k: Nat, n: Nat, xs: FloatVec, ys: FloatVec, i: Nat, a: FloatVec, b: FloatVec) -> ProductType<FloatVec*FloatVec> decreasing k =
        if_then_else(nat_eq(k, 0), pair(a, b),
            let base = mul(i, n) in
            if_then_else(or(nat_eq(i, 0), nat_eq(i + 1, n)),
                spline_rows(pred(k), n, xs, ys, i + 1, unwrap_or(fv_set(a, base + i, 1.0), a), unwrap_or(fv_set(b, i, 0.0), b)),
                let h0 = fl_sub(at(xs, i), at(xs, monus(i, 1))) in let h1 = fl_sub(at(xs, i + 1), at(xs, i)) in
                let a1 = unwrap_or(fv_set(a, base + monus(i, 1), h0), a) in let a2 = unwrap_or(fv_set(a1, base + i, fl_mul(2.0, fl_add(h0, h1))), a1) in
                let a3 = unwrap_or(fv_set(a2, base + i + 1, h1), a2) in
                let r = fl_mul(6.0, fl_sub(fl_div(fl_sub(at(ys, i + 1), at(ys, i)), h1), fl_div(fl_sub(at(ys, i), at(ys, monus(i, 1))), h0))) in
                spline_rows(pred(k), n, xs, ys, i + 1, a3, unwrap_or(fv_set(b, i, r), b))))
    private fn increasing_knots(k: Nat, xs: FloatVec, i: Nat) -> Bool decreasing k =
        if_then_else(nat_eq(k, 0), true, and(fl_lt(at(xs, i), at(xs, i + 1)), increasing_knots(pred(k), xs, i + 1)))
    private fn spline_data_valid(xs: FloatVec, ys: FloatVec) -> Bool =
        let n = fv_len(xs) in and(and(not(2 > n), nat_eq(n, fv_len(ys))),
            and(fold(fv_to_list(xs), true, fun(ok: Bool, x: Float, and(ok, fl_is_finite(x)))),
                and(fold(fv_to_list(ys), true, fun(ok: Bool, x: Float, and(ok, fl_is_finite(x)))), increasing_knots(monus(n, 1), xs, 0))))
    // through (xs[i], ys[i]) with increasing xs, zero second derivative at the ends
    fn natural_spline(xs: FloatVec, ys: FloatVec) -> Option<Spline> =
        let n = fv_len(xs) in
        if_then_else(not(and(not(n > 4096), spline_data_valid(xs, ys))), none(Spline),
            let sys = spline_rows(n, n, xs, ys, 0, unwrap_or(fv_new(mul(n, n), 0.0), fv_empty()), unwrap_or(fv_new(n, 0.0), fv_empty())) in
            match fv_solve(fst(sys), snd(sys), n) { none => none(Spline), some(ms) => some(Spline(xs, ys, ms)) })
    private fn find_interval(k: Nat, xs: FloatVec, x: Float, i: Nat) -> Nat decreasing k =
        if_then_else(nat_eq(k, 0), i, if_then_else(fl_le(x, at(xs, i + 1)), i, find_interval(pred(k), xs, x, i + 1)))
    fn spline_eval(s: Spline, x: Float) -> Option<Float> =
        let xs = field(s, xs) in let n = fv_len(xs) in
        if_then_else(not(and(and(spline_data_valid(xs, field(s, ys)), nat_eq(n, fv_len(field(s, ms)))),
            and(fl_is_finite(x), and(fl_le(at(xs, 0), x), fl_le(x, at(xs, monus(n, 1))))))), none(Float),
            let i = find_interval(monus(n, 2), xs, x, 0) in
            let h = fl_sub(at(xs, i + 1), at(xs, i)) in let a = fl_div(fl_sub(at(xs, i + 1), x), h) in let b = fl_div(fl_sub(x, at(xs, i)), h) in
            let m0 = at(field(s, ms), i) in let m1 = at(field(s, ms), i + 1) in
            some(fl_add(fl_add(fl_mul(a, at(field(s, ys), i)), fl_mul(b, at(field(s, ys), i + 1))),
                fl_mul(fl_div(fl_mul(h, h), 6.0), fl_add(fl_mul(fl_sub(fl_mul(fl_mul(a, a), a), a), m0), fl_mul(fl_sub(fl_mul(fl_mul(b, b), b), b), m1))))))

    // ---- polynomials: coefficients ascending, p(x) = c0 + c1·x + ... ----
    fn poly(xs: List<Float>) -> FloatVec = unwrap_or(fv_from_list(xs), fv_empty())
    fn fv_reverse_f(p: FloatVec) -> FloatVec = unwrap_or(fv_from_list(rev_floats(fv_to_list(p))), p)
    fn poly_eval(p: FloatVec, x: Float) -> Float = fold(fv_to_list(fv_reverse_f(p)), 0.0, fun(acc: Float, c: Float, fl_add(fl_mul(acc, x), c)))
    private fn padded(p: FloatVec, n: Nat) -> FloatVec = unwrap_or(fv_concat(p, unwrap_or(fv_new(monus(n, fv_len(p)), 0.0), fv_empty())), p)
    fn poly_add(p: FloatVec, q: FloatVec) -> FloatVec =
        let n = if_then_else(fv_len(p) > fv_len(q), fv_len(p), fv_len(q)) in unwrap_or(fv_add(padded(p, n), padded(q, n)), p)
    private fn conv_term(k: Nat, p: FloatVec, q: FloatVec, i: Nat, j: Nat, acc: Float) -> Float decreasing k =
        if_then_else(nat_eq(k, 0), acc, conv_term(pred(k), p, q, i + 1, j, if_then_else(i > j, acc, fl_add(acc, fl_mul(at(p, i), at(q, monus(j, i)))))))
    private fn conv_loop(k: Nat, p: FloatVec, q: FloatVec, j: Nat, acc: List<Float>) -> List<Float> decreasing k =
        if_then_else(nat_eq(k, 0), acc, conv_loop(pred(k), p, q, j + 1, cons(conv_term(fv_len(p), p, q, 0, j, 0.0), acc)))
    // exact-order product (direct convolution; fft_convolve for long inputs)
    fn poly_mul(p: FloatVec, q: FloatVec) -> FloatVec =
        if_then_else(or(nat_eq(fv_len(p), 0), nat_eq(fv_len(q), 0)), fv_empty(),
            let n = monus(fv_len(p) + fv_len(q), 1) in
            let r = conv_loop(n, padded(p, n), padded(q, n), 0, empty_list(Float)) in unwrap_or(fv_from_list(rev_floats(r)), fv_empty()))
    private fn deriv_loop(k: Nat, p: FloatVec, i: Nat, acc: List<Float>) -> List<Float> decreasing k =
        if_then_else(nat_eq(k, 0), acc, deriv_loop(pred(k), p, i + 1, cons(fl_mul(fl_of_nat(i), at(p, i)), acc)))
    fn poly_derivative(p: FloatVec) -> FloatVec = unwrap_or(fv_from_list(rev_floats(deriv_loop(monus(fv_len(p), 1), p, 1, empty_list(Float)))), fv_empty())
    private fn integ_loop(k: Nat, p: FloatVec, i: Nat, acc: List<Float>) -> List<Float> decreasing k =
        if_then_else(nat_eq(k, 0), acc, integ_loop(pred(k), p, i + 1, cons(fl_div(at(p, i), fl_of_nat(i + 1)), acc)))
    fn poly_integral(p: FloatVec) -> FloatVec = unwrap_or(fv_from_list(rev_floats(integ_loop(fv_len(p), p, 0, list(0.0)))), fv_empty())
    fn poly_degree(p: FloatVec) -> Nat = monus(fv_len(p), 1)

    // ---- complex numbers and polynomial roots (Durand–Kerner) ----
    fn cx_add(a: Cx, b: Cx) -> Cx = Cx(fl_add(field(a, re), field(b, re)), fl_add(field(a, im), field(b, im)))
    fn cx_sub(a: Cx, b: Cx) -> Cx = Cx(fl_sub(field(a, re), field(b, re)), fl_sub(field(a, im), field(b, im)))
    fn cx_mul(a: Cx, b: Cx) -> Cx = Cx(fl_sub(fl_mul(field(a, re), field(b, re)), fl_mul(field(a, im), field(b, im))), fl_add(fl_mul(field(a, re), field(b, im)), fl_mul(field(a, im), field(b, re))))
    fn cx_div(a: Cx, b: Cx) -> Cx =
        let d = fl_add(fl_mul(field(b, re), field(b, re)), fl_mul(field(b, im), field(b, im))) in
        Cx(fl_div(fl_add(fl_mul(field(a, re), field(b, re)), fl_mul(field(a, im), field(b, im))), d), fl_div(fl_sub(fl_mul(field(a, im), field(b, re)), fl_mul(field(a, re), field(b, im))), d))
    fn cx_abs(a: Cx) -> Float = fl_sqrt(fl_add(fl_mul(field(a, re), field(a, re)), fl_mul(field(a, im), field(a, im))))
    fn cx_poly_eval(p: FloatVec, z: Cx) -> Cx = fold(rev_floats(fv_to_list(p)), Cx(0.0, 0.0), fun(acc: Cx, c: Float, cx_add(cx_mul(acc, z), Cx(c, 0.0))))
    private fn dk_denominator(zs: List<Cx>, z: Cx, i: Nat, j: Nat) -> Cx decreasing zs =
        if_then_else(is_empty(zs), Cx(1.0, 0.0), match head(zs) { none => Cx(1.0, 0.0), some(w) =>
            if_then_else(nat_eq(i, j), dk_denominator(tail(zs), z, i, j + 1), cx_mul(cx_sub(z, w), dk_denominator(tail(zs), z, i, j + 1))) })
    private fn dk_update(all: List<Cx>, rest: List<Cx>, p: FloatVec, i: Nat) -> List<Cx> decreasing rest =
        if_then_else(is_empty(rest), empty_list(Cx), match head(rest) { none => empty_list(Cx), some(z) =>
            cons(cx_sub(z, cx_div(cx_poly_eval(p, z), dk_denominator(all, z, i, 0))), dk_update(all, tail(rest), p, i + 1)) })
    private fn dk_loop(k: Nat, zs: List<Cx>, p: FloatVec) -> List<Cx> decreasing k = if_then_else(nat_eq(k, 0), zs, dk_loop(pred(k), dk_update(zs, zs, p, 0), p))
    private fn pow_cx(k: Nat, z: Cx, acc: Cx) -> Cx decreasing k = if_then_else(nat_eq(k, 0), acc, pow_cx(pred(k), z, cx_mul(acc, z)))
    private fn starts(k: Nat, i: Nat, acc: List<Cx>) -> List<Cx> decreasing k =
        if_then_else(nat_eq(k, 0), acc, starts(pred(k), i + 1, cons(pow_cx(i, Cx(0.4, 0.9), Cx(1.0, 0.0)), acc)))
    // all complex roots of a polynomial with non-zero leading coefficient
    fn poly_roots(p: FloatVec, iterations: Nat) -> List<Cx> =
        let n = poly_degree(p) in let lead = at(p, n) in
        if_then_else(or(nat_eq(n, 0), fl_eq(lead, 0.0)), empty_list(Cx), dk_loop(iterations, starts(n, 0, empty_list(Cx)), fv_scale(p, fl_div(1.0, lead))))

    // ---- Fourier helpers on real signals ----
    private fn interleave_loop(k: Nat, x: FloatVec, i: Nat, acc: List<Float>) -> List<Float> decreasing k =
        if_then_else(nat_eq(k, 0), acc, interleave_loop(pred(k), x, i + 1, cons(0.0, cons(at(x, i), acc))))
    fn to_complex(x: FloatVec) -> FloatVec = unwrap_or(fv_from_list(rev_floats(interleave_loop(fv_len(x), x, 0, empty_list(Float)))), fv_empty())
    private fn power_loop2(k: Nat, z: FloatVec, i: Nat, acc: List<Float>) -> List<Float> decreasing k =
        if_then_else(nat_eq(k, 0), acc, let r = at(z, mul(2, i)) in let q = at(z, mul(2, i) + 1) in power_loop2(pred(k), z, i + 1, cons(fl_add(fl_mul(r, r), fl_mul(q, q)), acc)))
    // |X_k|^2 of the DFT of a real signal
    fn power_spectrum(x: FloatVec) -> Option<FloatVec> =
        match fft_forward(to_complex(x)) { none => none(FloatVec), some(z) => fv_from_list(rev_floats(power_loop2(fv_len(x), z, 0, empty_list(Float)))) }
    fn convolve(a: FloatVec, b: FloatVec) -> Option<FloatVec> = fft_convolve(a, b)

    theorem dual_product_rule: fl_eq(field(d_mul(d_var(3.0), d_var(3.0)), d), 6.0)
}
