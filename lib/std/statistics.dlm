// Multivariate statistics, Bayesian updates, MCMC and time series (v0.123, docs/KERNELS.md).
// Data matrices are row-major Matrix values (rows = observations).
module std.statistics
import std.signal

theory StdStatistics extends StdSignal {
    record Pca(values: FloatVec, components: Matrix, mean: FloatVec)
    record BetaPosterior(alpha: Float, beta: Float)
    record NormalPosterior(mean: Float, variance: Float)
    record Chain(samples: Matrix, accepted: Nat, state: Nat)
    record ArModel(coefficients: FloatVec, noise: Float)

    private fn st_ix(n: Nat) -> List<Nat> = nv_to_list(unwrap_or(nv_range(0, n), nv_empty()))
    private fn tabf(n: Nat, f: Fn<Nat, Float>) -> FloatVec = unwrap_or(fv_from_list(map(st_ix(n), f)), fv_empty())
    private fn mget(m: Matrix, i: Nat, j: Nat) -> Float = unwrap_or(fv_get(field(m, data), mul(i, field(m, cols)) + j), 0.0)

    // ---- multivariate ----
    fn column_means(x: Matrix) -> FloatVec =
        let n = field(x, rows) in tabf(field(x, cols), fun(j: Nat, fl_div(fold(st_ix(n), 0.0, fun(acc: Float, i: Nat, fl_add(acc, mget(x, i, j)))), fl_of_nat(n))))
    fn center(x: Matrix) -> Matrix =
        let mu = column_means(x) in Matrix(field(x, rows), field(x, cols), tabf(mul(field(x, rows), field(x, cols)), fun(k: Nat,
            fl_sub(unwrap_or(fv_get(field(x, data), k), 0.0), unwrap_or(fv_get(mu, mod(k, field(x, cols))), 0.0)))))
    // sample covariance (d × d), n - 1 denominator
    fn covariance_matrix(x: Matrix) -> Option<Matrix> =
        if_then_else(2 > field(x, rows), none(Matrix),
            let c = center(x) in match matmul(transpose(c), c) { none => none(Matrix), some(s) => some(scale(s, fl_div(1.0, fl_of_nat(monus(field(x, rows), 1))))) })
    fn correlation_matrix(x: Matrix) -> Option<Matrix> =
        match covariance_matrix(x) { none => none(Matrix), some(s) => let d = field(s, cols) in
            some(Matrix(d, d, tabf(mul(d, d), fun(k: Nat, let i = div(k, d) in let j = mod(k, d) in
                fl_div(mget(s, i, j), fl_sqrt(fl_mul(mget(s, i, i), mget(s, j, j)))))))) }
    // principal components: eigenvalues descending, components as columns
    fn pca(x: Matrix) -> Option<Pca> =
        match covariance_matrix(x) { none => none(Pca), some(s) => match eigh(s) { none => none(Pca), some(e) =>
            let d = field(s, cols) in
            some(Pca(tabf(d, fun(j: Nat, unwrap_or(fv_get(field(e, values), monus(d, j + 1)), 0.0))),
                Matrix(d, d, tabf(mul(d, d), fun(k: Nat, mget(field(e, vectors), div(k, d), monus(d, mod(k, d) + 1))))), column_means(x))) } }
    fn explained_variance(p: Pca) -> FloatVec = let t = fv_sum(field(p, values)) in fv_scale(field(p, values), fl_div(1.0, t))
    // project centred rows onto the first k components
    fn project(p: Pca, x: Matrix, k: Nat) -> Option<Matrix> =
        let d = field(field(p, components), rows) in
        let w = Matrix(d, k, tabf(mul(d, k), fun(t: Nat, mget(field(p, components), div(t, k), mod(t, k))))) in
        matmul(center(x), w)
    // n draws from N(mu, Σ) via the Cholesky factor
    fn mvn_samples(state: Nat, n: Nat, mu: FloatVec, sigma: Matrix) -> Option<Matrix> =
        match cholesky(sigma) { none => none(Matrix), some(l) => let d = fv_len(mu) in
            let z = normal_samples(state, mul(n, d), 0.0, 1.0) in
            match matmul(Matrix(n, d, z), transpose(l)) { none => none(Matrix), some(y) =>
                some(Matrix(n, d, tabf(mul(n, d), fun(k: Nat, fl_add(unwrap_or(fv_get(field(y, data), k), 0.0), unwrap_or(fv_get(mu, mod(k, d)), 0.0)))))) } }
    // squared Mahalanobis distance
    fn mahalanobis2(x: FloatVec, mu: FloatVec, sigma: Matrix) -> Option<Float> =
        match fv_sub(x, mu) { none => none(Float), some(dx) => match solve(sigma, dx) { none => none(Float), some(y) => fv_dot(dx, y) } }

    // ---- Bayesian conjugate updates ----
    fn beta_update(prior: BetaPosterior, successes: Nat, failures: Nat) -> BetaPosterior =
        BetaPosterior(fl_add(field(prior, alpha), fl_of_nat(successes)), fl_add(field(prior, beta), fl_of_nat(failures)))
    fn beta_mean(p: BetaPosterior) -> Float = fl_div(field(p, alpha), fl_add(field(p, alpha), field(p, beta)))
    fn beta_variance(p: BetaPosterior) -> Float =
        let s = fl_add(field(p, alpha), field(p, beta)) in fl_div(fl_mul(field(p, alpha), field(p, beta)), fl_mul(fl_mul(s, s), fl_add(s, 1.0)))
    // P(θ <= x) for the Beta posterior
    fn beta_cdf(p: BetaPosterior, x: Float) -> Float = beta_inc(field(p, alpha), field(p, beta), x)
    // normal mean with known observation variance
    fn normal_update(prior: NormalPosterior, xs: FloatVec, noise_variance: Float) -> NormalPosterior =
        let n = fl_of_nat(fv_len(xs)) in
        let precision = fl_add(fl_div(1.0, field(prior, variance)), fl_div(n, noise_variance)) in
        let m = fl_div(fl_add(fl_div(field(prior, mean), field(prior, variance)), fl_div(fv_sum(xs), noise_variance)), precision) in
        NormalPosterior(m, fl_div(1.0, precision))
    fn credible_interval(p: NormalPosterior, level: Float) -> Option<ProductType<Float*Float>> =
        match normal_quantile(fl_mul(0.5, fl_add(1.0, level)), 0.0, 1.0) { none => none(Product(Float, Float)), some(z) =>
            let h = fl_mul(z, fl_sqrt(field(p, variance))) in some(pair(fl_sub(field(p, mean), h), fl_add(field(p, mean), h))) }

    // ---- Metropolis–Hastings (Gaussian random walk) ----
    private fn mh_loop(k: Nat, logp: Fn<FloatVec, Float>, x: FloatVec, lx: Float, step: Float, state: Nat, acc: FloatVec, accepted: Nat) -> Chain decreasing k =
        if_then_else(nat_eq(k, 0), Chain(Matrix(div(fv_len(acc), if_then_else(nat_eq(fv_len(x), 0), 1, fv_len(x))), fv_len(x), acc), accepted, state),
            let z = normal_samples(state, fv_len(x), 0.0, step) in
            let s1 = fold(st_ix(fv_len(x)), state, fun(acc: Nat, i: Nat, rng_next(rng_next(acc)))) in
            let y = unwrap_or(fv_add(x, z), x) in let ly = logp(y) in
            let u = rng_uniform(s1) in
            let take = fl_lt(fl_log(field(u, value)), fl_sub(ly, lx)) in
            let x2 = if_then_else(take, y, x) in
            mh_loop(pred(k), logp, x2, if_then_else(take, ly, lx), step, field(u, state), unwrap_or(fv_concat(acc, x2), acc), if_then_else(take, accepted + 1, accepted)))
    // `n` samples (rows) of the density exp(logp) after `burn` discarded steps
    fn metropolis(logp: Fn<FloatVec, Float>, x0: FloatVec, step: Float, n: Nat, burn: Nat, seed: Nat) -> Chain =
        let warm = mh_loop(burn, logp, x0, logp(x0), step, seed, fv_empty(), 0) in
        let d = fv_len(x0) in
        let last = if_then_else(nat_eq(burn, 0), x0, unwrap_or(fv_slice(field(field(warm, samples), data), mul(monus(burn, 1), d), d), x0)) in
        mh_loop(n, logp, last, logp(last), step, field(warm, state), fv_empty(), 0)
    fn chain_mean(c: Chain) -> FloatVec = column_means(field(c, samples))
    fn acceptance_rate(c: Chain) -> Float = fl_div(fl_of_nat(field(c, accepted)), fl_of_nat(field(field(c, samples), rows)))

    // ---- time series ----
    fn autocovariance(x: FloatVec, lag: Nat) -> Option<Float> =
        match mean(x) { none => none(Float), some(m) => let n = fv_len(x) in if_then_else(not(n > lag), none(Float),
            some(fl_div(fold(st_ix(monus(n, lag)), 0.0, fun(acc: Float, t: Nat, fl_add(acc, fl_mul(fl_sub(unwrap_or(fv_get(x, t), 0.0), m), fl_sub(unwrap_or(fv_get(x, t + lag), 0.0), m))))), fl_of_nat(n)))) }
    fn autocorrelation(x: FloatVec, lag: Nat) -> Option<Float> =
        match autocovariance(x, 0) { none => none(Float), some(c0) => match autocovariance(x, lag) { none => none(Float), some(c) => some(fl_div(c, c0)) } }
    fn acf(x: FloatVec, max_lag: Nat) -> FloatVec = tabf(max_lag + 1, fun(k: Nat, unwrap_or(autocorrelation(x, k), 0.0)))
    // Yule–Walker AR(p) fit (Toeplitz system solved densely)
    fn ar_fit(x: FloatVec, p: Nat) -> Option<ArModel> =
        let r = tabf(p + 1, fun(k: Nat, unwrap_or(autocovariance(x, k), 0.0))) in
        let toeplitz = tabf(mul(p, p), fun(t: Nat, let i = div(t, p) in let j = mod(t, p) in unwrap_or(fv_get(r, if_then_else(i > j, monus(i, j), monus(j, i))), 0.0))) in
        match fv_solve(toeplitz, unwrap_or(fv_slice(r, 1, p), fv_empty()), p) { none => none(ArModel), some(phi) =>
            some(ArModel(phi, fl_sub(unwrap_or(fv_get(r, 0), 0.0), unwrap_or(fv_dot(phi, unwrap_or(fv_slice(r, 1, p), fv_empty())), 0.0)))) }
    // h-step forecast from the end of x
    fn ar_forecast(m: ArModel, x: FloatVec, h: Nat) -> FloatVec =
        let p = fv_len(field(m, coefficients)) in
        let mu = unwrap_or(mean(x), 0.0) in
        let ext = fold(st_ix(h), x, fun(acc: FloatVec, s: Nat, let n = fv_len(acc) in
            let v = fl_add(mu, fold(st_ix(p), 0.0, fun(a2: Float, k: Nat, fl_add(a2, fl_mul(unwrap_or(fv_get(field(m, coefficients), k), 0.0), fl_sub(unwrap_or(fv_get(acc, monus(n, k + 1)), mu), mu)))))) in
            unwrap_or(fv_concat(acc, unwrap_or(fv_new(1, v), fv_empty())), acc))) in
        unwrap_or(fv_slice(ext, fv_len(x), h), fv_empty())
    fn difference(x: FloatVec) -> FloatVec = tabf(monus(fv_len(x), 1), fun(t: Nat, fl_sub(unwrap_or(fv_get(x, t + 1), 0.0), unwrap_or(fv_get(x, t), 0.0))))
    fn exponential_smoothing(x: FloatVec, a: Float) -> FloatVec =
        unwrap_or(sig_iir(unwrap_or(fv_new(1, a), fv_empty()), unwrap_or(fv_from_list(list(1.0, fl_sub(a, 1.0))), fv_empty()), x), fv_empty())
    // Ljung–Box Q statistic and p-value (χ² with `lags` degrees of freedom)
    fn ljung_box(x: FloatVec, lags: Nat) -> TestResult =
        let n = fl_of_nat(fv_len(x)) in
        let q = fl_mul(fl_mul(n, fl_add(n, 2.0)), fold(nv_to_list(unwrap_or(nv_range(1, lags), nv_empty())), 0.0, fun(acc: Float, k: Nat,
            let r = unwrap_or(autocorrelation(x, k), 0.0) in fl_add(acc, fl_div(fl_mul(r, r), fl_sub(n, fl_of_nat(k))))))) in
        TestResult(q, fl_sub(1.0, chi_square_cdf(q, lags)))

    theorem beta_uniform_mean: fl_eq(beta_mean(BetaPosterior(1.0, 1.0)), 0.5)
}
