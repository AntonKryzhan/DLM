static bool dvm_tolerance(double tolerance) { return isfinite(tolerance) && tolerance >= 0.0; }

static DV *dvm_checked(DV *vector, double error, bool kind) {
    return dv_some(dv_pair(vector, dv_pair(dvf_float(error), dv_bool(kind))));
}

static DV *dvm_strict(DV *option) {
    DV *vector = option->u.opt.v;
    if (!vector || !dvf_matmul_finite(vector->u.fv.d, vector->u.fv.n)) return DV_NONE;
    return dvm_checked(vector, 0.0, false);
}

static DV *dvm_gpu(const DmFloatJob *job) {
    double *out = malloc(job->output_count * sizeof(double));
    if (!out) return NULL;
    double error;
    if (!dlm_math_float_gpu(job, out, &error)) { free(out); return NULL; }
    size_t len = job->output_count;
    if (job->kind == DM_CONVOLUTION) {
        len /= 2;
        for (size_t i = 0; i < len; i++) out[i] = out[2 * i];
    }
    return dvm_checked(dvf_vec_take(out, len), error, true);
}

static DV *dvb_fv_matmul_bounded(DV *av, DV *bv, DV *nv, DV *mv, DV *kv, DV *tv) {
    const char *op = "fv_matmul_bounded";
    DV *a = dvf_vec_of(av, op), *b = dvf_vec_of(bv, op);
    nat_t n = dv_nat_of(nv, op), m = dv_nat_of(mv, op), k = dv_nat_of(kv, op);
    double tolerance = dvf_of(tv, op);
    size_t na = a->u.fv.n, nb = b->u.fv.n;
    if ((m && n > (nat_t)na / m) || n * m != (nat_t)na
        || (k && m > (nat_t)nb / k) || m * k != (nat_t)nb
        || (k && n > DVF_VEC_MAX / k)
        || !dvm_tolerance(tolerance) || !dvf_matmul_finite(a->u.fv.d, na) || !dvf_matmul_finite(b->u.fv.d, nb))
        return DV_NONE;
    if (n && m && k && tolerance > 0 && dlm_math_should_try((uint64_t)(n * m * k))) {
        DmFloatJob job = {
            .kind = DM_MATRIX, .n = (uint32_t)n, .inner = (uint32_t)m, .columns = (uint32_t)k,
            .left = a->u.fv.d, .right = b->u.fv.d, .left_count = na, .right_count = nb,
            .output_count = (size_t)(n * k), .tolerance = tolerance
        };
        DV *result = dvm_gpu(&job);
        if (result) return result;
    }
    return dvm_strict(dvb_fv_matmul(av, bv, nv, mv, kv));
}

static DV *dvb_fft_bounded(DV *xv, DV *iv, DV *tv) {
    const char *op = "fft_bounded";
    DV *x = dvf_vec_of(xv, op);
    size_t len = x->u.fv.n, n = len / 2;
    bool inverse = dv_truth(iv);
    double tolerance = dvf_of(tv, op);
    if ((len & 1) || !dvm_tolerance(tolerance) || !dvf_matmul_finite(x->u.fv.d, len)) return DV_NONE;
    if (tolerance > 0 && n > 1 && !(n & (n - 1)) && dlm_math_should_try(n * (uint64_t)dm_bits((uint32_t)n))) {
        DmFloatJob job = {
            .kind = DM_FFT, .n = (uint32_t)n, .inverse = inverse, .left = x->u.fv.d,
            .left_count = len, .output_count = len, .tolerance = tolerance
        };
        DV *result = dvm_gpu(&job);
        if (result) return result;
    }
    return dvm_strict(dva_fft_dv(x, inverse, op));
}

static double *dvm_interleave(const double *x, size_t n) {
    double *out = calloc(n * 2, sizeof(double));
    if (out) for (size_t i = 0; i < n; i++) out[2 * i] = x[i];
    return out;
}

static DV *dvb_fft_convolve_bounded(DV *av, DV *bv, DV *tv) {
    const char *op = "fft_convolve_bounded";
    DV *a = dvf_vec_of(av, op), *b = dvf_vec_of(bv, op);
    size_t na = a->u.fv.n, nb = b->u.fv.n;
    double tolerance = dvf_of(tv, op);
    if (!dvm_tolerance(tolerance) || !dvf_matmul_finite(a->u.fv.d, na) || !dvf_matmul_finite(b->u.fv.d, nb))
        return DV_NONE;
    if (!na || !nb) return dvm_checked(dvf_vec(NULL, 0), 0.0, false);
    size_t len = na + nb - 1, n = 1;
    while (n < len) n *= 2;
    if (n > DVF_VEC_MAX / 2) return DV_NONE;
    if (tolerance > 0 && n > 1 && dlm_math_should_try(3 * n * (uint64_t)dm_bits((uint32_t)n))) {
        double *left = dvm_interleave(a->u.fv.d, na), *right = dvm_interleave(b->u.fv.d, nb);
        DV *result = NULL;
        if (left && right) {
            DmFloatJob job = {
                .kind = DM_CONVOLUTION, .n = (uint32_t)n, .left = left, .right = right,
                .left_count = 2 * na, .right_count = 2 * nb, .output_count = 2 * len, .tolerance = tolerance
            };
            result = dvm_gpu(&job);
        }
        free(left);
        free(right);
        if (result) return result;
    }
    return dvm_strict(dvb_fft_convolve(av, bv));
}

static DV *dvb_la_csr_matvec_bounded(DV *rp, DV *ix, DV *vl, DV *xv, DV *tv) {
    const char *op = "la_csr_matvec_bounded";
    DV *x = dvf_vec_of(xv, op);
    double tolerance = dvf_of(tv, op);
    size_t rows;
    if (!dvm_tolerance(tolerance) || !dvl_csr(rp, ix, vl, x->u.fv.n, &rows, op)) return DV_NONE;
    size_t nnz = vl->u.fv.n;
    if (!dvf_matmul_finite(x->u.fv.d, x->u.fv.n) || !dvf_matmul_finite(vl->u.fv.d, nnz)) return DV_NONE;
    if (tolerance > 0 && rows && nnz && dlm_math_should_try(nnz)) {
        uint32_t *layout = malloc((rows + 1 + nnz) * sizeof(uint32_t));
        if (layout) {
            uint32_t width = 0;
            for (size_t i = 0; i <= rows; i++) {
                layout[i] = (uint32_t)rp->u.nv.d[i];
                if (i && layout[i] - layout[i - 1] > width) width = layout[i] - layout[i - 1];
            }
            for (size_t i = 0; i < nnz; i++) layout[rows + 1 + i] = (uint32_t)ix->u.nv.d[i];
            DmFloatJob job = {
                .kind = DM_CSR, .n = (uint32_t)rows, .inner = width, .columns = (uint32_t)x->u.fv.n,
                .left = vl->u.fv.d, .right = x->u.fv.d, .layout = layout,
                .left_count = nnz, .right_count = x->u.fv.n, .output_count = rows, .tolerance = tolerance
            };
            DV *result = dvm_gpu(&job);
            free(layout);
            if (result) return result;
        }
    }
    return dvm_strict(dvb_la_csr_matvec(rp, ix, vl, xv));
}

static DV *dvb_ntt_convolve_accel(DV *av, DV *bv) {
    const char *op = "ntt_convolve_accel";
    DV *a = dvv_of(av, op), *b = dvv_of(bv, op);
    size_t na = a->u.nv.n, nb = b->u.nv.n;
    if (dlm_math_should_try_ntt(na, nb)) {
        size_t len = na + nb - 1;
        uint32_t *left = malloc(na * sizeof(uint32_t)), *right = malloc(nb * sizeof(uint32_t));
        uint32_t *out = malloc(len * sizeof(uint32_t));
        bool ok = false;
        if (left && right && out) {
            for (size_t i = 0; i < na; i++) left[i] = (uint32_t)(a->u.nv.d[i] % DLM_MATH_NTT_MODULUS);
            for (size_t i = 0; i < nb; i++) right[i] = (uint32_t)(b->u.nv.d[i] % DLM_MATH_NTT_MODULUS);
            ok = dlm_math_ntt_gpu(left, right, na, nb, out);
        }
        free(left);
        free(right);
        if (ok) {
            nat_t *values = dvv_scratch(len * sizeof(nat_t));
            for (size_t i = 0; i < len; i++) values[i] = out[i];
            free(out);
            DV *vector = dvv_vec(values, len);
            free(values);
            return dv_some(dv_pair(vector, dv_bool(true)));
        }
        free(out);
    }
    DV *strict = dvb_ntt_convolve(av, bv);
    return strict->u.opt.v ? dv_some(dv_pair(strict->u.opt.v, dv_bool(false))) : DV_NONE;
}

static int64_t dvm_integer_max(const double *values, size_t len) {
    int64_t bound = 0;
    for (size_t i = 0; i < len; i++) {
        double x = fabs(values[i]);
        if (!(x <= DLM_MATH_NTT_MODULUS / 2) || x != trunc(x)) return -1;
        if ((int64_t)x > bound) bound = (int64_t)x;
    }
    return bound;
}

static DV *dvm_encode_integers(const double *values, size_t len) {
    nat_t *residues = dvv_scratch(len * sizeof(nat_t));
    for (size_t i = 0; i < len; i++) {
        int64_t x = (int64_t)values[i];
        residues[i] = (nat_t)(x < 0 ? x + DLM_MATH_NTT_MODULUS : x);
    }
    DV *result = dvv_vec(residues, len);
    free(residues);
    return result;
}

static DV *dvb_fft_convolve_integer(DV *av, DV *bv) {
    const char *op = "fft_convolve_integer";
    DV *a = dvf_vec_of(av, op), *b = dvf_vec_of(bv, op);
    size_t na = a->u.fv.n, nb = b->u.fv.n;
    if (!na || !nb) return dv_some(dvf_vec(NULL, 0));
    if (na > DLM_MATH_NTT_MAX || nb - 1 > DLM_MATH_NTT_MAX - na) return DV_NONE;
    int64_t ma = dvm_integer_max(a->u.fv.d, na), mb = dvm_integer_max(b->u.fv.d, nb);
    if (ma < 0 || mb < 0 || (nat_t)(na < nb ? na : nb) * (nat_t)ma * (nat_t)mb > DLM_MATH_NTT_MODULUS / 2)
        return DV_NONE;
    DV *left = dvm_encode_integers(a->u.fv.d, na), *right = dvm_encode_integers(b->u.fv.d, nb);
    DV *conv = dvb_ntt_convolve(left, right)->u.opt.v;
    size_t len = conv->u.nv.n;
    double *out = dvf_scratch(len);
    for (size_t i = 0; i < len; i++) {
        int64_t x = (int64_t)conv->u.nv.d[i];
        out[i] = (double)(x > DLM_MATH_NTT_MODULUS / 2 ? x - DLM_MATH_NTT_MODULUS : x);
    }
    return dv_some(dvf_vec_take(out, len));
}
