static double dvm_add_down(double a, double b) {
    if (b == 0) return a;
    if (a == 0) return b;
    return a == -b ? 0 : nextafter(a + b, -INFINITY);
}

static double dvm_add_up(double a, double b) {
    if (b == 0) return a;
    if (a == 0) return b;
    return a == -b ? 0 : nextafter(a + b, INFINITY);
}

static double dvm_residual(const double *a, const double *x, size_t n, double rhs) {
    double lo = -rhs, hi = -rhs;
    for (size_t i = 0; i < n; i++) {
        double p = a[i] * x[i];
        if (!isfinite(p)) return INFINITY;
        bool exact = a[i] == 0 || x[i] == 0 || fabs(a[i]) == 1 || fabs(x[i]) == 1;
        lo = dvm_add_down(lo, exact ? p : nextafter(p, -INFINITY));
        hi = dvm_add_up(hi, exact ? p : nextafter(p, INFINITY));
        if (!isfinite(lo) || !isfinite(hi)) return INFINITY;
    }
    return fmax(fabs(lo), fabs(hi));
}

static double dvm_inverse_triangular_norm(const double *l, size_t n, bool transpose, double *bound) {
    double norm = 0;
    for (size_t i = 0; i < n; i++) {
        size_t row = transpose ? n - 1 - i : i;
        double upper = 1;
        for (size_t j = 0; j < i; j++) {
            size_t column = transpose ? n - 1 - j : j;
            double element = transpose ? l[column * n + row] : l[row * n + column];
            if (element != 0) upper = dvm_add_up(upper, nextafter(fabs(element) * bound[j], INFINITY));
        }
        bound[i] = nextafter(upper / l[row * n + row], INFINITY);
        norm = fmax(norm, bound[i]);
    }
    return norm;
}

static double dvm_factor_margin(const double *a, const double *l, size_t n) {
    double *bound = dvf_scratch(n);
    double left = dvm_inverse_triangular_norm(l, n, false, bound);
    double right = dvm_inverse_triangular_norm(l, n, true, bound);
    double upper = nextafter(left * right, INFINITY);
    if (!isfinite(upper) || upper <= 0) { free(bound); return 0; }
    memset(bound, 0, n * sizeof(double));
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j <= i; j++) {
            double e = dvm_residual(l + i * n, l + j * n, j + 1, a[i * n + j]);
            bound[i] = dvm_add_up(bound[i], e);
            if (i != j) bound[j] = dvm_add_up(bound[j], e);
        }
    }
    double error = 0;
    for (size_t i = 0; i < n; i++) error = fmax(error, bound[i]);
    free(bound);
    return dvm_add_down(nextafter(1 / upper, -INFINITY), -error);
}

static void dvm_triangular_solve(const double *l, const double *b, size_t n, double *x) {
    for (size_t i = 0; i < n; i++) {
        double value = b[i];
        for (size_t j = 0; j < i; j++) value -= l[i * n + j] * x[j];
        x[i] = value / l[i * n + i];
    }
    for (size_t i = n; i-- > 0;) {
        double value = x[i];
        for (size_t j = i + 1; j < n; j++) value -= l[j * n + i] * x[j];
        x[i] = value / l[i * n + i];
    }
}

static double dvm_certify(const double *a, const double *b, const double *x, size_t n, double lower) {
    if (!isfinite(lower) || lower <= 0 || !dvf_matmul_finite(x, n)) return INFINITY;
    double r = 0;
    for (size_t i = 0; i < n; i++) r = fmax(r, dvm_residual(a + i * n, x, n, b[i]));
    return r == 0 ? 0 : nextafter(r / lower, INFINITY);
}

static DV *dvb_la_solve_certified(DV *av, DV *bv, DV *nv, DV *tv) {
    const char *op = "la_solve_certified";
    size_t n;
    DV *b = dvf_vec_of(bv, op);
    double tolerance = dvf_of(tv, op);
    if (!dvm_tolerance(tolerance) || !dvl_square(av, nv, &n, op)
        || b->u.fv.n != n || !dvf_matmul_finite(b->u.fv.d, n)) return DV_NONE;
    if (!n) return dvm_checked(dvf_vec(NULL, 0), 0.0, false);
    const double *a = av->u.fv.d;
    bool symmetric = true;
    double lower = INFINITY;
    for (size_t i = 0; i < n; i++) {
        double diagonal = a[i * n + i];
        if (diagonal <= 0) symmetric = false;
        double radius = 0;
        for (size_t j = 0; j < n; j++) {
            if (i != j) radius = dvm_add_up(radius, fabs(a[i * n + j]));
            if (j < i && a[i * n + j] != a[j * n + i]) symmetric = false;
        }
        lower = fmin(lower, dvm_add_down(fabs(diagonal), -radius));
    }
    if (symmetric) {
        DV *factor = dvb_la_cholesky(av, nv)->u.opt.v;
        if (factor) {
            const double *l = factor->u.fv.d;
            if (lower <= 0) lower = dvm_factor_margin(a, l, n);
            if (lower > 0) {
                double *x = dvf_scratch(n);
                dvm_triangular_solve(l, b->u.fv.d, n, x);
                double error = dvm_certify(a, b->u.fv.d, x, n, lower);
                if (error <= tolerance) return dvm_checked(dvf_vec_take(x, n), error, true);
                free(x);
            }
        }
    }
    if (lower <= 0) return DV_NONE;
    DV *x = dvb_fv_solve(av, bv, nv)->u.opt.v;
    if (!x) return DV_NONE;
    double error = dvm_certify(a, b->u.fv.d, x->u.fv.d, n, lower);
    return error <= tolerance ? dvm_checked(x, error, false) : DV_NONE;
}
