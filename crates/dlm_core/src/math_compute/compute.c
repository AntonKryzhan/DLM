#include <float.h>
#include <math.h>

#if DLM_MATH_NTT_MODULUS != 998244353u
#error Regenerate the NTT shader for the new modulus
#endif

#ifdef DLM_MATH_STATIC
#define DM_API
#else
#define DM_API static
#endif

enum { DM_MATRIX = 1, DM_FFT, DM_CONVOLUTION, DM_CSR };
typedef struct {
    uint32_t kind, n, inner, columns, inverse;
    const double *left, *right, *factors;
    const uint32_t *layout;
    size_t left_count, right_count, output_count;
    double tolerance;
} DmFloatJob;

typedef struct { double magnitude, error; } DmBound;
typedef struct { double epsilon, tiny, limit; size_t bytes; } DmPrecision;
static const DmPrecision dm_fp64 = { DBL_EPSILON, DBL_MIN, DBL_MAX / 2.0, sizeof(double) };
static const DmPrecision dm_fp32 = { FLT_EPSILON, FLT_MIN, FLT_MAX / 2.0, sizeof(float) };

static double dm_up(double x) { return isfinite(x) ? nextafter(x, INFINITY) : INFINITY; }
static double dm_add_up(double a, double b) { return a == 0 ? b : b == 0 ? a : dm_up(a + b); }
static double dm_mul_up(double a, double b) { return a == 0 || b == 0 ? 0 : dm_up(a * b); }

static double dm_maxabs(const double *v, size_t n) {
    double out = 0;
    for (size_t i = 0; i < n; i++) {
        if (!isfinite(v[i])) return INFINITY;
        out = fmax(out, fabs(v[i]));
    }
    return out;
}

static DmBound dm_input(double a, DmPrecision p) {
    /* A shader may discard subnormals. Allow one ULP for normal arithmetic
     * and a full minimum normal at every input and intermediate. */
    return (DmBound){ a, dm_add_up(p.bytes == sizeof(float) ? dm_mul_up(a, p.epsilon) : 0, p.tiny) };
}
static DmBound dm_rounded(double magnitude, double drift, DmPrecision p) {
    double total = dm_add_up(magnitude, drift);
    if (!(total < p.limit)) return (DmBound){ magnitude, INFINITY };
    return (DmBound){ magnitude, dm_add_up(drift, dm_add_up(dm_mul_up(total, p.epsilon), p.tiny)) };
}
static DmBound dm_add(DmBound a, DmBound b, DmPrecision p) {
    return dm_rounded(dm_add_up(a.magnitude, b.magnitude), dm_add_up(a.error, b.error), p);
}
static DmBound dm_mul(DmBound a, DmBound b, DmPrecision p) {
    double cross = dm_add_up(dm_mul_up(a.magnitude, b.error), dm_mul_up(b.magnitude, a.error));
    double drift = dm_add_up(cross, dm_mul_up(a.error, b.error));
    return dm_rounded(dm_mul_up(a.magnitude, b.magnitude), drift, p);
}
static DmBound dm_dot_bound(DmBound a, DmBound b, uint32_t n, DmPrecision p) {
    if (!n) return (DmBound){0, 0};
    DmBound term = dm_mul(a, b, p);
    double magnitude = dm_mul_up(n, term.magnitude);
    double r = dm_mul_up(n, p.epsilon);
    if (!(r < 0.5)) return (DmBound){ magnitude, INFINITY };
    /* (1+epsilon)^n is bounded by 1/(1-n*epsilon). Include every
     * additive underflow allowance in the accumulated error. */
    double denom = nextafter(1.0 - r, 0);
    double gamma = dm_up(r / denom);
    double total = dm_mul_up(n, dm_add_up(term.magnitude, term.error));
    double drift = dm_add_up(dm_mul_up(n, term.error), dm_mul_up(total, gamma));
    drift = dm_add_up(drift, dm_up(dm_mul_up(n, p.tiny) / denom));
    if (!(dm_add_up(magnitude, drift) < p.limit)) drift = INFINITY;
    return (DmBound){ magnitude, drift };
}
static DmBound dm_complex_product(DmBound a, DmBound b, DmPrecision p) {
    DmBound product = dm_mul(a, b, p);
    return dm_add(product, product, p);
}
static DmBound dm_transform_bound(DmBound input, DmBound factor, uint32_t n, bool inverse, DmPrecision p) {
    for (uint32_t step = 2; step <= n; step *= 2)
        input = dm_add(input, dm_complex_product(input, factor, p), p);
    if (inverse) input = dm_mul(input, dm_input(1.0 / n, p), p);
    return input;
}
static DmBound dm_job_bound(const DmFloatJob *job, double a, double b, double f, DmPrecision p) {
    DmBound left = dm_input(a, p), right = dm_input(b, p), factor = dm_input(f, p);
    if (job->kind == DM_MATRIX || job->kind == DM_CSR) return dm_dot_bound(left, right, job->inner, p);
    left = dm_transform_bound(left, factor, job->n, job->inverse != 0, p);
    if (job->kind != DM_CONVOLUTION) return left;
    right = dm_transform_bound(right, factor, job->n, false, p);
    return dm_transform_bound(dm_complex_product(left, right, p), factor, job->n, true, p);
}

static unsigned dm_bits(uint32_t n) {
    unsigned bits = 0;
    while (n > 1) { n >>= 1; bits++; }
    return bits;
}
static void dm_vector_step(DmDispatch *steps, size_t *count, unsigned op, uint32_t n,
                           uint32_t step, uint32_t offset, bool paired, unsigned bits) {
    uint32_t work = op == 2 ? n / 2 : n;
    steps[(*count)++] = (DmDispatch){ .word = {op, n, 0, 0, step, offset, paired, bits}, .x = (work + 63) / 64, .y = 1 };
}
static void dm_transform(DmDispatch *steps, size_t *count, uint32_t n, uint32_t base,
                         uint32_t lanes, bool paired, bool inverse) {
    if (n > 1) dm_vector_step(steps, count, 1, n, 0, 0, paired, dm_bits(n));
    for (uint32_t step = 2; step <= n; step *= 2)
        dm_vector_step(steps, count, 2, n, step, base + lanes * (step / 2 - 1), paired, 0);
    if (inverse) dm_vector_step(steps, count, 4, n, 0, base + lanes * (n - 1), false, 0);
}

/* The packed per-stage factors use the same binary64 angles as the strict
 * transform. The error contract for the accelerated path is relative to that
 * strict transform, including its libm factors. */
static void dm_make_factors(double *out, uint32_t points, bool inverse) {
    const double sign = inverse ? 1.0 : -1.0;
    for (uint32_t step = 2; step <= points; step *= 2) {
        double angle = sign * 6.283185307179586 / (double)step;
        for (uint32_t j = 0; j < step / 2; j++) {
            double t = angle * (double)j;
            size_t i = 2 * ((size_t)step / 2 - 1 + j);
            out[i] = cos(t);
            out[i + 1] = sin(t);
        }
    }
    out[2 * (points - 1)] = 1.0 / points;
    out[2 * points - 1] = 0;
}

static float *dm_narrow(const double *values, size_t count) {
    if (!count) return NULL;
    float *out = malloc(count * sizeof(*out));
    if (!out) return NULL;
    for (size_t i = 0; i < count; i++) out[i] = (float)values[i];
    return out;
}

static bool dm_float_dispatch(const DmFloatJob *job, const DmDispatch *steps, size_t count, DmTransfer slots[4],
                              unsigned output_slot, DmPrecision p, unsigned shader, double *out, uint64_t work) {
    if (p.bytes == sizeof(double))
        return dm_vulkan(shader, steps, count, slots, output_slot, out, job->output_count * p.bytes, work);
    float *copies[3] = {0};
    float *result = malloc(job->output_count * sizeof(*result));
    if (!result) return false;
    DmTransfer narrow[4];
    memcpy(narrow, slots, sizeof(narrow));
    bool ok = true;
    for (unsigned i = 0; i < 3; i++) {
        if (slots[i].input_bytes) {
            copies[i] = dm_narrow(slots[i].input, slots[i].input_bytes / sizeof(double));
            if (!copies[i]) { ok = false; break; }
        }
        narrow[i] = (DmTransfer){ copies[i], slots[i].input_bytes / 2, slots[i].bytes / 2 };
    }
    if (ok) ok = dm_vulkan(shader, steps, count, narrow, output_slot, result, job->output_count * sizeof(*result), work);
    if (ok) for (size_t i = 0; i < job->output_count; i++) out[i] = result[i];
    for (unsigned i = 0; i < 3; i++) free(copies[i]);
    free(result);
    return ok;
}

DM_API int dlm_math_should_try(uint64_t work) {
    const char *selection = getenv("DLM_MATH_BACKEND");
    if (selection && strcmp(selection, "cpu") == 0) return false;
    return work >= (1u << 20) || (selection && strcmp(selection, "vulkan") == 0);
}

/* Return 1 only when the GPU variant fits the requested bound. The caller
 * runs the normal CPU algorithm on failure, preserving its original limits. */
DM_API int dlm_math_float_gpu(const DmFloatJob *job, double *out, double *error) {
    if (!(job->tolerance > 0) || !isfinite(job->tolerance) || !job->output_count || !job->n) return 0;
    uint64_t work;
    if (job->kind == DM_MATRIX) work = (uint64_t)job->n * job->inner * job->columns;
    else if (job->kind == DM_CSR) work = job->left_count;
    else work = (uint64_t)job->n * dm_bits(job->n) * (job->kind == DM_CONVOLUTION ? 3 : 1);
    if (!dlm_math_should_try(work)) return 0;
    double a = dm_maxabs(job->left, job->left_count), b = dm_maxabs(job->right, job->right_count);
    if (!isfinite(a) || !isfinite(b)) return 0;

    DmTransfer slots[4] = {
        {job->left, job->left_count * sizeof(double), job->left_count * sizeof(double)},
        {job->right, job->right_count * sizeof(double), job->right_count * sizeof(double)},
        {NULL, 0, job->output_count * sizeof(double)},
        {NULL, 0, 0}
    };
    DmDispatch steps[2 * (25 + 2) + 1];
    size_t count = 0;
    unsigned output_slot = 2;
    const double *factors = job->factors;
    double *owned_factors = NULL;
    double f = 1.0;
    if (job->kind == DM_MATRIX) {
        steps[count++] = (DmDispatch){
            .word = {0, job->n, job->inner, job->columns, 0, 0, 0, 0},
            .x = (job->columns + 7) / 8, .y = (job->n + 7) / 8
        };
    } else if (job->kind == DM_CSR) {
        size_t bytes = (job->n + 1 + job->left_count) * sizeof(uint32_t);
        slots[3] = (DmTransfer){job->layout, bytes, bytes};
        dm_vector_step(steps, &count, 0, job->n, 0, 0, false, 0);
    } else {
        uint32_t n = job->n;
        bool convolution = job->kind == DM_CONVOLUTION;
        if (n & (n - 1)) return 0;
        size_t elements = (convolution ? 4u : 2u) * n;
        if (!factors) {
            owned_factors = malloc(elements * sizeof(double));
            if (!owned_factors) return 0;
            dm_make_factors(owned_factors, n, job->inverse != 0);
            if (convolution) dm_make_factors(owned_factors + 2u * n, n, true);
            factors = owned_factors;
        }
        f = dm_maxabs(factors, elements);
        slots[0].bytes = 2u * n * sizeof(double);
        if (convolution) slots[1].bytes = slots[0].bytes;
        slots[2] = (DmTransfer){factors, elements * sizeof(double), elements * sizeof(double)};
        output_slot = 0;
        dm_transform(steps, &count, n, 0, 2, convolution, job->inverse != 0);
        if (convolution) {
            dm_vector_step(steps, &count, 3, n, 0, 0, false, 0);
            dm_transform(steps, &count, n, 2u * n, 2, false, true);
        }
    }
    bool ok = false;
    DmBound cpu = dm_job_bound(job, a, b, f, dm_fp64);
    if (count && isfinite(cpu.error)) {
        for (unsigned i = 0; i < 2; i++) {
            DmPrecision p = i ? dm_fp64 : dm_fp32;
            DmBound gpu = dm_job_bound(job, a, b, f, p);
            double difference = dm_add_up(cpu.error, gpu.error);
            if (difference > job->tolerance || a >= p.limit || b >= p.limit || f >= p.limit || !isfinite(f)) continue;
            unsigned shader = (job->kind == DM_MATRIX ? DM_F32_MATRIX : DM_F32_VECTOR) + i;
            if (dm_float_dispatch(job, steps, count, slots, output_slot, p, shader, out, work)
                && dm_maxabs(out, job->output_count) <= dm_add_up(gpu.magnitude, gpu.error)) {
                *error = difference;
                ok = true;
                break;
            }
        }
    }
    free(owned_factors);
    return ok;
}

static uint32_t dm_modmul(uint32_t a, uint32_t b) {
    return (uint32_t)((uint64_t)a * b % DLM_MATH_NTT_MODULUS);
}
static uint32_t dm_modpow(uint32_t base, uint32_t exponent) {
    uint32_t out = 1;
    while (exponent) {
        if (exponent & 1u) out = dm_modmul(out, base);
        base = dm_modmul(base, base);
        exponent >>= 1;
    }
    return out;
}
static void dm_ntt_factors(uint32_t *factors, uint32_t points, bool inverse) {
    const uint32_t p = DLM_MATH_NTT_MODULUS;
    for (uint32_t step = 2; step <= points; step *= 2) {
        uint32_t root = dm_modpow(DLM_MATH_NTT_ROOT, (p - 1) / step);
        if (inverse) root = dm_modpow(root, p - 2);
        uint32_t factor = 1;
        for (uint32_t j = 0; j < step / 2; j++) {
            factors[step / 2 - 1 + j] = factor;
            factor = dm_modmul(factor, root);
        }
    }
    factors[points - 1] = inverse ? dm_modpow(points, p - 2) : 1;
}

DM_API int dlm_math_should_try_ntt(size_t left, size_t right) {
    if (!left || !right || left > DLM_MATH_NTT_MAX || right - 1 > DLM_MATH_NTT_MAX - left) return 0;
    size_t len = left + right - 1;
    uint32_t points = 1;
    while (points < len) points *= 2;
    uint64_t work = (uint64_t)points * dm_bits(points) * 3;
    if (!dlm_math_should_try(work)) return 0;
    const char *selection = getenv("DLM_MATH_BACKEND");
    return (selection && strcmp(selection, "vulkan") == 0)
        || (uint64_t)left * right > (work / 2 + points) * DLM_MATH_NTT_DIRECT_FACTOR;
}

DM_API int dlm_math_ntt_gpu(const uint32_t *a, const uint32_t *b, size_t left, size_t right, uint32_t *out) {
    if (!dlm_math_should_try_ntt(left, right)) return 0;
    size_t len = left + right - 1;
    uint32_t points = 1;
    while (points < len) points *= 2;
    uint64_t work = (uint64_t)points * dm_bits(points) * 3;
    size_t bytes = points * sizeof(uint32_t);
    uint32_t *factors = malloc(2 * bytes);
    if (!factors) return 0;
    dm_ntt_factors(factors, points, false);
    dm_ntt_factors(factors + points, points, true);
    DmTransfer slots[4] = {
        {a, left * sizeof(uint32_t), bytes}, {b, right * sizeof(uint32_t), bytes},
        {factors, 2 * bytes, 2 * bytes}, {NULL, 0, 0}
    };
    DmDispatch steps[2 * (25 + 2) + 1];
    size_t count = 0;
    dm_transform(steps, &count, points, 0, 1, true, false);
    dm_vector_step(steps, &count, 3, points, 0, 0, false, 0);
    dm_transform(steps, &count, points, points, 1, false, true);
    bool ok = dm_vulkan(DM_U32_NTT, steps, count, slots, 0, out, len * sizeof(uint32_t), work);
    free(factors);
    if (ok) for (size_t i = 0; i < len; i++) if (out[i] >= DLM_MATH_NTT_MODULUS) return 0;
    return ok;
}
