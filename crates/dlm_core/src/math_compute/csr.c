#if defined(__SIZEOF_INT128__)
typedef unsigned __int128 DmNat;

#if defined(_WIN32) || defined(__unix__)
#ifdef _WIN32
typedef HANDLE DmThread;
#else
#include <pthread.h>
#include <unistd.h>
typedef pthread_t DmThread;
#endif

typedef struct {
    const DmNat *row, *column;
    const double *value, *vector;
    double *out;
    size_t first, end;
} DmCsrRange;

static void dm_csr_range(const DmCsrRange *range) {
    for (size_t i = range->first; i < range->end; i++) {
        double sum = 0.0;
        for (size_t j = (size_t)range->row[i]; j < (size_t)range->row[i + 1]; j++) {
            double product = range->value[j] * range->vector[(size_t)range->column[j]];
            sum = sum + product;
        }
        range->out[i] = sum;
    }
}

#ifdef _WIN32
static DWORD WINAPI dm_csr_worker(void *arg) { dm_csr_range(arg); return 0; }
static bool dm_start_thread(DmThread *thread, DmCsrRange *range) {
    *thread = CreateThread(NULL, 0, dm_csr_worker, range, 0, NULL);
    return *thread != NULL;
}
static void dm_join_thread(DmThread thread) {
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
}
static long dm_cpu_count(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors;
}
#else
static void *dm_csr_worker(void *arg) { dm_csr_range(arg); return NULL; }
static bool dm_start_thread(DmThread *thread, DmCsrRange *range) {
    return pthread_create(thread, NULL, dm_csr_worker, range) == 0;
}
static void dm_join_thread(DmThread thread) { pthread_join(thread, NULL); }
static long dm_cpu_count(void) { return sysconf(_SC_NPROCESSORS_ONLN); }
#endif

static size_t dm_row_boundary(const DmNat *row, size_t lo, size_t hi, size_t target) {
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (row[mid] < target) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}
#endif

/* Parallelize independent rows and retain their original accumulation order.
 * The caller validates CSR indices and uses this only on finite operands. */
DM_API int dlm_math_csr_parallel(const DmNat *row, const DmNat *column, const double *values,
                                const double *vector, size_t rows, double *out) {
#if (defined(_WIN32) || defined(__unix__)) && DN_CPU_PROFILE != 1
    enum { MAX_THREADS = 8 };
    if (rows < DLM_MATH_CSR_PARALLEL_ROWS || row[rows] < DLM_MATH_CSR_PARALLEL_NNZ) return 0;
    size_t nnz = (size_t)row[rows];
    long cores = dm_cpu_count();
    if (cores < 2) return 0;
    size_t workers = (size_t)cores;
    if (workers > MAX_THREADS) workers = MAX_THREADS;
    size_t useful_workers = nnz / (DLM_MATH_CSR_PARALLEL_NNZ / 2);
    if (workers > useful_workers) workers = useful_workers;
    DmThread threads[MAX_THREADS - 1];
    DmCsrRange ranges[MAX_THREADS];
    bool started[MAX_THREADS - 1] = {0};
    size_t first = 0;
    for (size_t i = 0; i < workers; i++) {
        size_t end = i + 1 == workers ? rows : dm_row_boundary(row, first, rows, (i + 1) * nnz / workers);
        ranges[i] = (DmCsrRange){row, column, values, vector, out, first, end};
        first = end;
    }
    for (size_t i = 0; i + 1 < workers; i++) {
        if (ranges[i].first != ranges[i].end) started[i] = dm_start_thread(&threads[i], &ranges[i]);
    }
    for (size_t i = 0; i + 1 < workers; i++) if (!started[i]) dm_csr_range(&ranges[i]);
    dm_csr_range(&ranges[workers - 1]);
    for (size_t i = 0; i + 1 < workers; i++) if (started[i]) dm_join_thread(threads[i]);
    return 1;
#else
    (void)row; (void)column; (void)values; (void)vector; (void)rows; (void)out;
    return 0;
#endif
}
#endif
