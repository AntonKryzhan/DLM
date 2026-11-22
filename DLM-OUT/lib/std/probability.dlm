// Probability and statistics on machine floats (v0.115, docs/GRAPHS.md): special functions,
// distributions (pdf/pmf, cdf, quantile), sampling from std.numerics' generator, estimates and tests.
module std.probability
import std.numerics

theory StdProbability extends StdNumerics {
    record DrawNat(value: Nat, state: Nat)
    record TestResult(statistic: Float, p_value: Float)

    fn fl_inf() -> Float = fl_lit("inf")
    fn sqrt_pi() -> Float = 1.7724538509055159
    fn pi() -> Float = 3.141592653589793

    // ---- special functions ----
    private fn erf_series(k: Nat, x: Float, x2: Float, n: Nat, term: Float, acc: Float) -> Float decreasing k =
        if_then_else(nat_eq(k, 0), acc,
            let t = fl_div(fl_mul(fl_neg(term), x2), fl_of_nat(n + 1)) in
            erf_series(pred(k), x, x2, n + 1, t, fl_add(acc, fl_div(t, fl_of_nat(mul(2, n) + 3)))))
    private fn erfc_cf(k: Nat, x: Float, acc: Float) -> Float decreasing k =
        if_then_else(nat_eq(k, 0), acc, erfc_cf(pred(k), x, fl_add(x, fl_div(fl_mul(fl_of_nat(k), 0.5), acc))))
    // erfc for x >= 3 by the continued fraction x + (1/2)/(x + 1/(x + (3/2)/(x + ...)))
    private fn erfc_large(x: Float) -> Float = fl_div(fl_exp(fl_neg(fl_mul(x, x))), fl_mul(sqrt_pi(), erfc_cf(80, x, x)))
    private fn erf_pos(x: Float) -> Float =
        if_then_else(fl_lt(x, 3.0), fl_mul(fl_div(2.0, sqrt_pi()), erf_series(120, x, fl_mul(x, x), 0, x, x)), fl_sub(1.0, erfc_large(x)))
    fn erf(x: Float) -> Float = if_then_else(fl_is_nan(x), x, if_then_else(fl_lt(x, 0.0), fl_neg(erf_pos(fl_neg(x))), erf_pos(x)))
    fn erfc(x: Float) -> Float = if_then_else(fl_lt(x, 3.0), fl_sub(1.0, erf(x)), erfc_large(x))
    // Lanczos (g = 7, n = 9), x > 0
    private fn lanczos(x: Float) -> Float =
            let z = fl_sub(x, 1.0) in
            let s = fold(list(pair(1, 676.5203681218851), pair(2, -1259.1392167224028), pair(3, 771.3234287776531), pair(4, -176.6150291621406),
                    pair(5, 12.507343278686905), pair(6, -0.13857109526572012), pair(7, 0.000009984369578019572), pair(8, 0.00000015056327351493116)),
                0.9999999999998099, fun(acc: Float, c: ProductType<Nat*Float>, fl_add(acc, fl_div(snd(c), fl_add(z, fl_of_nat(fst(c))))))) in
            let t = fl_add(z, 7.5) in
            fl_add(fl_add(fl_mul(0.5, fl_log(fl_mul(2.0, pi()))), fl_mul(fl_add(z, 0.5), fl_log(t))), fl_sub(fl_log(s), t))
    // log |Γ(x)|, reflection below 1/2
    fn log_gamma(x: Float) -> Float =
        if_then_else(fl_lt(x, 0.5), fl_sub(fl_log(fl_div(pi(), fl_abs(fl_sin(fl_mul(pi(), x))))), lanczos(fl_sub(1.0, x))), lanczos(x))
    fn gamma(x: Float) -> Float =
        let magnitude = fl_exp(log_gamma(x)) in
        if_then_else(and(fl_lt(x, 0.5), fl_lt(fl_sin(fl_mul(pi(), x)), 0.0)), fl_neg(magnitude), magnitude)
    fn log_choose(n: Nat, k: Nat) -> Float =
        fl_sub(log_gamma(fl_of_nat(n + 1)), fl_add(log_gamma(fl_of_nat(k + 1)), log_gamma(fl_of_nat(monus(n, k) + 1))))
    // regularized lower incomplete gamma P(a, x): series for x < a + 1, continued fraction otherwise
    private fn gser(k: Nat, a: Float, x: Float, ap: Float, del: Float, sum: Float) -> Float decreasing k =
        if_then_else(nat_eq(k, 0), sum, let ap1 = fl_add(ap, 1.0) in let d = fl_div(fl_mul(del, x), ap1) in
            if_then_else(fl_lt(fl_abs(d), fl_mul(fl_abs(sum), fl_lit("1e-16"))), fl_add(sum, d), gser(pred(k), a, x, ap1, d, fl_add(sum, d))))
    private fn gcf(k: Nat, i: Nat, a: Float, b: Float, c: Float, d: Float, h: Float) -> Float decreasing k =
        if_then_else(nat_eq(k, 0), h,
            let an = fl_mul(fl_neg(fl_of_nat(i)), fl_sub(fl_of_nat(i), a)) in let b2 = fl_add(b, 2.0) in
            let d1 = fl_add(fl_mul(an, d), b2) in let d2 = if_then_else(fl_lt(fl_abs(d1), fl_lit("1e-300")), fl_lit("1e-300"), d1) in
            let c1 = fl_add(b2, fl_div(an, c)) in let c2 = if_then_else(fl_lt(fl_abs(c1), fl_lit("1e-300")), fl_lit("1e-300"), c1) in
            let dd = fl_div(1.0, d2) in let del = fl_mul(dd, c2) in
            if_then_else(fl_lt(fl_abs(fl_sub(del, 1.0)), fl_lit("1e-16")), fl_mul(h, del), gcf(pred(k), i + 1, a, b2, c2, dd, fl_mul(h, del))))
    fn gamma_p(a: Float, x: Float) -> Float =
        if_then_else(fl_le(x, 0.0), 0.0, if_then_else(fl_eq(x, fl_inf()), 1.0,
            let front = fl_exp(fl_sub(fl_sub(fl_mul(a, fl_log(x)), x), log_gamma(a))) in
            if_then_else(fl_lt(x, fl_add(a, 1.0)), fl_mul(front, fl_div(gser(500, a, x, a, fl_div(1.0, a), fl_div(1.0, a)), 1.0)),
                let b = fl_add(fl_sub(x, a), 1.0) in let d = fl_div(1.0, b) in
                fl_sub(1.0, fl_mul(front, gcf(500, 1, a, b, fl_lit("1e300"), d, d))))))
    // regularized incomplete beta I_x(a, b) (Lentz continued fraction)
    private fn tiny_fix(v: Float) -> Float = if_then_else(fl_lt(fl_abs(v), fl_lit("1e-300")), fl_lit("1e-300"), v)
    private fn betacf(k: Nat, m: Nat, a: Float, b: Float, x: Float, c: Float, d: Float, h: Float) -> Float decreasing k =
        if_then_else(nat_eq(k, 0), h,
            let fm = fl_of_nat(m) in let m2 = fl_mul(2.0, fm) in
            let aa = fl_div(fl_mul(fl_mul(fm, fl_sub(b, fm)), x), fl_mul(fl_add(fl_sub(a, 1.0), m2), fl_add(a, m2))) in
            let d1 = fl_div(1.0, tiny_fix(fl_add(1.0, fl_mul(aa, d)))) in let c1 = tiny_fix(fl_add(1.0, fl_div(aa, c))) in let h1 = fl_mul(fl_mul(h, d1), c1) in
            let ab = fl_div(fl_mul(fl_neg(fl_mul(fl_add(a, fm), fl_add(fl_add(a, b), fm))), x), fl_mul(fl_add(a, m2), fl_add(fl_add(a, 1.0), m2))) in
            let d2 = fl_div(1.0, tiny_fix(fl_add(1.0, fl_mul(ab, d1)))) in let c2 = tiny_fix(fl_add(1.0, fl_div(ab, c1))) in let del = fl_mul(d2, c2) in
            if_then_else(fl_lt(fl_abs(fl_sub(del, 1.0)), fl_lit("1e-16")), fl_mul(h1, del), betacf(pred(k), m + 1, a, b, x, c2, d2, fl_mul(h1, del))))
    private fn beta_front(a: Float, b: Float, x: Float) -> Float =
        fl_exp(fl_add(fl_sub(fl_sub(log_gamma(fl_add(a, b)), log_gamma(a)), log_gamma(b)), fl_add(fl_mul(a, fl_log(x)), fl_mul(b, fl_log(fl_sub(1.0, x))))))
    private fn beta_cf_value(a: Float, b: Float, x: Float) -> Float =
        let tiny = fl_lit("1e-300") in
        let d0 = fl_sub(1.0, fl_div(fl_mul(fl_add(a, b), x), fl_add(a, 1.0))) in
        let d = fl_div(1.0, if_then_else(fl_lt(fl_abs(d0), tiny), tiny, d0)) in
        fl_div(fl_mul(beta_front(a, b, x), betacf(300, 1, a, b, x, 1.0, d, d)), a)
    fn beta_inc(a: Float, b: Float, x: Float) -> Float =
        if_then_else(fl_le(x, 0.0), 0.0, if_then_else(fl_le(1.0, x), 1.0,
            if_then_else(fl_lt(x, fl_div(fl_add(a, 1.0), fl_add(fl_add(a, b), 2.0))), beta_cf_value(a, b, x), fl_sub(1.0, beta_cf_value(b, a, fl_sub(1.0, x))))))

    // ---- continuous distributions ----
    fn uniform_pdf(x: Float, a: Float, b: Float) -> Float = if_then_else(and(fl_le(a, x), fl_le(x, b)), fl_div(1.0, fl_sub(b, a)), 0.0)
    fn uniform_cdf(x: Float, a: Float, b: Float) -> Float = fl_max(0.0, fl_min(1.0, fl_div(fl_sub(x, a), fl_sub(b, a))))
    fn normal_pdf(x: Float, mu: Float, sd: Float) -> Float =
        let z = fl_div(fl_sub(x, mu), sd) in fl_div(fl_exp(fl_mul(-0.5, fl_mul(z, z))), fl_mul(sd, fl_mul(sqrt_pi(), 1.4142135623730951)))
    fn normal_cdf(x: Float, mu: Float, sd: Float) -> Float = fl_mul(0.5, erfc(fl_div(fl_sub(mu, x), fl_mul(sd, 1.4142135623730951))))
    private fn quantile_loop(k: Nat, p: Float, lo: Float, hi: Float) -> Float decreasing k =
        if_then_else(nat_eq(k, 0), fl_mid(lo, hi), let m = fl_mid(lo, hi) in
            if_then_else(or(fl_eq(m, lo), fl_eq(m, hi)), m,
                if_then_else(fl_lt(normal_cdf(m, 0.0, 1.0), p), quantile_loop(pred(k), p, m, hi), quantile_loop(pred(k), p, lo, m))))
    // inverse cdf by bisection on [-40, 40]; none outside (0, 1)
    fn normal_quantile(p: Float, mu: Float, sd: Float) -> Option<Float> =
        if_then_else(and(fl_lt(0.0, p), fl_lt(p, 1.0)), some(fl_add(mu, fl_mul(sd, quantile_loop(200, p, -40.0, 40.0)))), none(Float))
    fn exponential_pdf(x: Float, rate: Float) -> Float = if_then_else(fl_lt(x, 0.0), 0.0, fl_mul(rate, fl_exp(fl_neg(fl_mul(rate, x)))))
    fn exponential_cdf(x: Float, rate: Float) -> Float = if_then_else(fl_lt(x, 0.0), 0.0, fl_sub(1.0, fl_exp(fl_neg(fl_mul(rate, x)))))
    fn exponential_quantile(p: Float, rate: Float) -> Float = fl_div(fl_neg(fl_log(fl_sub(1.0, p))), rate)
    fn chi_square_cdf(x: Float, k: Nat) -> Float = gamma_p(fl_mul(0.5, fl_of_nat(k)), fl_mul(0.5, x))
    fn student_t_cdf(t: Float, nu: Float) -> Float =
        let x = fl_div(nu, fl_add(nu, fl_mul(t, t))) in let tail = fl_mul(0.5, beta_inc(fl_mul(0.5, nu), 0.5, x)) in
        if_then_else(fl_lt(0.0, t), fl_sub(1.0, tail), tail)
    fn gamma_pdf(x: Float, shape: Float, scale: Float) -> Float =
        if_then_else(fl_lt(x, 0.0), 0.0,
            if_then_else(fl_eq(x, 0.0), if_then_else(fl_lt(shape, 1.0), fl_inf(), if_then_else(fl_eq(shape, 1.0), fl_div(1.0, scale), 0.0)),
                fl_exp(fl_sub(fl_sub(fl_mul(fl_sub(shape, 1.0), fl_log(x)), fl_div(x, scale)), fl_add(log_gamma(shape), fl_mul(shape, fl_log(scale)))))))

    // ---- discrete distributions ----
    fn binomial_pmf(n: Nat, k: Nat, p: Float) -> Float =
        if_then_else(k > n, 0.0, if_then_else(fl_eq(p, 0.0), if_then_else(nat_eq(k, 0), 1.0, 0.0), if_then_else(fl_eq(p, 1.0), if_then_else(nat_eq(k, n), 1.0, 0.0),
            fl_exp(fl_add(log_choose(n, k), fl_add(fl_mul(fl_of_nat(k), fl_log(p)), fl_mul(fl_of_nat(monus(n, k)), fl_log(fl_sub(1.0, p)))))))))
    private fn binom_sum(j: Nat, n: Nat, i: Nat, p: Float, acc: Float) -> Float decreasing j =
        if_then_else(nat_eq(j, 0), acc, binom_sum(pred(j), n, i + 1, p, fl_add(acc, binomial_pmf(n, i, p))))
    fn binomial_cdf(n: Nat, k: Nat, p: Float) -> Float = fl_min(1.0, binom_sum(if_then_else(k > n, n, k) + 1, n, 0, p, 0.0))
    fn poisson_pmf(k: Nat, lambda: Float) -> Float =
        if_then_else(fl_eq(lambda, 0.0), if_then_else(nat_eq(k, 0), 1.0, 0.0),
            fl_exp(fl_sub(fl_sub(fl_mul(fl_of_nat(k), fl_log(lambda)), lambda), log_gamma(fl_of_nat(k + 1)))))
    private fn pois_sum(j: Nat, i: Nat, lambda: Float, acc: Float) -> Float decreasing j =
        if_then_else(nat_eq(j, 0), acc, pois_sum(pred(j), i + 1, lambda, fl_add(acc, poisson_pmf(i, lambda))))
    fn poisson_cdf(k: Nat, lambda: Float) -> Float = fl_min(1.0, pois_sum(k + 1, 0, lambda, 0.0))
    fn geometric_pmf(k: Nat, p: Float) -> Float = fl_mul(p, fl_pow(fl_sub(1.0, p), fl_of_nat(k)))

    // ---- sampling (state threaded through Draw/DrawNat) ----
    fn sample_exponential(state: Nat, rate: Float) -> Draw = let u = rng_uniform(state) in Draw(exponential_quantile(field(u, value), rate), field(u, state))
    fn sample_bernoulli(state: Nat, p: Float) -> DrawNat = let u = rng_uniform(state) in DrawNat(if_then_else(fl_lt(field(u, value), p), 1, 0), field(u, state))
    private fn pois_knuth(k: Nat, state: Nat, limit: Float, prod: Float, count: Nat) -> DrawNat decreasing k =
        if_then_else(nat_eq(k, 0), DrawNat(count, state),
            let u = rng_uniform(state) in let p2 = fl_mul(prod, field(u, value)) in
            if_then_else(fl_le(p2, limit), DrawNat(count, field(u, state)), pois_knuth(pred(k), field(u, state), limit, p2, count + 1)))
    // Knuth's multiplication method (intended for moderate lambda)
    fn sample_poisson(state: Nat, lambda: Float) -> DrawNat = pois_knuth(100000, state, fl_exp(fl_neg(lambda)), 1.0, 0)
    private fn pick(k: Nat, w: FloatVec, i: Nat, target: Float, acc: Float) -> Nat decreasing k =
        if_then_else(nat_eq(k, 0), monus(fv_len(w), 1),
            let a = fl_add(acc, unwrap_or(fv_get(w, i), 0.0)) in if_then_else(fl_lt(target, a), i, pick(pred(k), w, i + 1, target, a)))
    // index drawn with probability proportional to non-negative weights
    fn sample_discrete(state: Nat, weights: FloatVec) -> DrawNat =
        let u = rng_uniform(state) in DrawNat(pick(fv_len(weights), weights, 0, fl_mul(field(u, value), fv_sum(weights)), 0.0), field(u, state))
    private fn normals(n: Nat, state: Nat, mu: Float, sd: Float, acc: List<Float>) -> List<Float> decreasing n =
        if_then_else(nat_eq(n, 0), acc, let d = rng_normal(state, mu, sd) in normals(pred(n), field(d, state), mu, sd, cons(field(d, value), acc)))
    fn normal_samples(state: Nat, n: Nat, mu: Float, sd: Float) -> FloatVec = unwrap_or(fv_from_list(normals(n, state, mu, sd, empty_list(Float))), fv_empty())

    // ---- estimates and tests ----
    // linear interpolation between order statistics (type 7)
    fn quantile(xs: FloatVec, q: Float) -> Option<Float> =
        let s = sorted(xs) in let n = fv_len(s) in
        if_then_else(or(nat_eq(n, 0), or(fl_lt(q, 0.0), fl_lt(1.0, q))), none(Float),
            let h = fl_mul(q, fl_of_nat(monus(n, 1))) in let lo = fl_floor(h) in
            match fl_to_int(lo) { none => none(Float), some(i) => let k = abs(i) in
                match fv_get(s, k) { none => none(Float), some(a) => some(fl_add(a, fl_mul(fl_sub(h, lo), fl_sub(unwrap_or(fv_get(s, k + 1), a), a)))) } })
    private fn hist_loop(k: Nat, xs: FloatVec, i: Nat, lo: Float, width: Float, bins: Nat, acc: NatVec) -> NatVec decreasing k =
        if_then_else(nat_eq(k, 0), acc,
            let x = unwrap_or(fv_get(xs, i), fl_lit("nan")) in
            let b = match fl_to_int(fl_floor(fl_div(fl_sub(x, lo), width))) { none => bins, some(z) => if_then_else(int_eq(z, int(abs(z))), abs(z), bins) } in
            let b2 = if_then_else(and(nat_eq(b, bins), fl_eq(x, fl_add(lo, fl_mul(width, fl_of_nat(bins))))), monus(bins, 1), b) in
            hist_loop(pred(k), xs, i + 1, lo, width, bins, if_then_else(bins > b2, unwrap_or(nv_set(acc, b2, unwrap_or(nv_get(acc, b2), 0) + 1), acc), acc)))
    // counts in `bins` equal bins over [lo, hi]; values outside are ignored
    fn histogram(xs: FloatVec, lo: Float, hi: Float, bins: Nat) -> NatVec =
        if_then_else(or(nat_eq(bins, 0), not(fl_lt(lo, hi))), nv_empty(),
            hist_loop(fv_len(xs), xs, 0, lo, fl_div(fl_sub(hi, lo), fl_of_nat(bins)), bins, unwrap_or(nv_new(bins, 0), nv_empty())))
    fn z_score(x: Float, mu: Float, sd: Float) -> Float = fl_div(fl_sub(x, mu), sd)
    // one-sample t test against mean mu0, two-sided
    fn t_test(xs: FloatVec, mu0: Float) -> Option<TestResult> =
        match mean(xs) { none => none(TestResult), some(m) => match stddev(xs) { none => none(TestResult), some(s) =>
            let n = fl_of_nat(fv_len(xs)) in let t = fl_div(fl_sub(m, mu0), fl_div(s, fl_sqrt(n))) in
            some(TestResult(t, fl_mul(2.0, student_t_cdf(fl_neg(fl_abs(t)), fl_sub(n, 1.0))))) } }
    // Welch two-sample t test, two-sided
    fn welch_t_test(xs: FloatVec, ys: FloatVec) -> Option<TestResult> =
        match mean(xs) { none => none(TestResult), some(mx) => match mean(ys) { none => none(TestResult), some(my) =>
            match variance(xs) { none => none(TestResult), some(vx) => match variance(ys) { none => none(TestResult), some(vy) =>
                let nx = fl_of_nat(fv_len(xs)) in let ny = fl_of_nat(fv_len(ys)) in
                let a = fl_div(vx, nx) in let b = fl_div(vy, ny) in
                let t = fl_div(fl_sub(mx, my), fl_sqrt(fl_add(a, b))) in
                let df = fl_div(fl_mul(fl_add(a, b), fl_add(a, b)), fl_add(fl_div(fl_mul(a, a), fl_sub(nx, 1.0)), fl_div(fl_mul(b, b), fl_sub(ny, 1.0)))) in
                some(TestResult(t, fl_mul(2.0, student_t_cdf(fl_neg(fl_abs(t)), df)))) } } } }
    private fn chi_sum(k: Nat, d2: FloatVec, e: FloatVec, i: Nat, acc: Float) -> Float decreasing k =
        if_then_else(nat_eq(k, 0), acc, chi_sum(pred(k), d2, e, i + 1, fl_add(acc, fl_div(unwrap_or(fv_get(d2, i), 0.0), unwrap_or(fv_get(e, i), 1.0)))))
    // Pearson chi-square goodness of fit with k - 1 degrees of freedom
    fn chi_square_test(observed: FloatVec, expected: FloatVec) -> Option<TestResult> =
        if_then_else(or(2 > fv_len(observed), not(nat_eq(fv_len(observed), fv_len(expected)))), none(TestResult),
            match fv_sub(observed, expected) { none => none(TestResult), some(d) => match fv_mul(d, d) { none => none(TestResult), some(d2) =>
                let s = chi_sum(fv_len(d2), d2, expected, 0, 0.0) in
                some(TestResult(s, fl_sub(1.0, chi_square_cdf(s, monus(fv_len(observed), 1))))) } })

    theorem normal_cdf_at_mean_is_half: fl_eq(normal_cdf(0.0, 0.0, 1.0), 0.5)
}
