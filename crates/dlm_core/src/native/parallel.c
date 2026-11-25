typedef struct {
    _Atomic size_t next;
    size_t end, grain;
    void (*visit)(void *, size_t);
    void *context;
    _Atomic bool failed;
} dnp_range;

static void dnp_dispose(void *raw) { (void)raw; }

static void dnp_worker(void *raw) {
    dnp_range *range = *(dnp_range **)raw;
    size_t first = atomic_load_explicit(&range->next, memory_order_relaxed);
    while (first < range->end && !dn_task_cancelled()) {
        size_t remaining = range->end - first;
        size_t end = first + (remaining < range->grain ? remaining : range->grain);
        if (!atomic_compare_exchange_weak_explicit(&range->next, &first, end,
                memory_order_relaxed, memory_order_relaxed)) continue;
        for (size_t i = first; i < end && !dn_task_cancelled(); ++i)
            range->visit(range->context, i);
        first = atomic_load_explicit(&range->next, memory_order_relaxed);
    }
}

static unsigned dn_parallel(dn_pool *pool, size_t first, size_t end, size_t grain,
                           void (*visit)(void *, size_t), void *context) {
    if (first >= end) return 2;
    if (dn_task_cancelled()) return 3;
    size_t count = 1 + (end - first - 1) / grain;
    if (count > pool->workers) count = pool->workers;
    if (count > pool->capacity) count = pool->capacity;
    dnp_range range = { .end = end, .grain = grain, .visit = visit, .context = context };
    atomic_init(&range.next, first);
    atomic_init(&range.failed, false);
    /* Prepare every lane before publishing any pointer to the stack captures. */
    dn_task *lanes[256];
    for (size_t i = 0; i < count; ++i) {
        lanes[i] = dn_pool_prepare(pool, dnp_worker, dnp_dispose,
                                  sizeof(dnp_range *), _Alignof(dnp_range *));
        *(dnp_range **)lanes[i]->data = &range;
        lanes[i]->cancel_parent = dnc_current_task;
        lanes[i]->cancel_group = &range.failed;
    }

    dnc_lock(&pool->sync);
    size_t used = pool->queued + pool->waiting_count + pool->polling;
    size_t available = pool->closing || used >= pool->capacity ? 0 : pool->capacity - used;
    size_t started = available < count ? available : count;
    for (size_t i = 0; i < started; ++i) dnc_enqueue(pool, lanes[i]);
    dnc_signal(&pool->sync);
    dnc_unlock(&pool->sync);

    unsigned status = started ? 2 : 5;
    for (size_t i = 0; i < count; ++i) {
        dn_task *lane = lanes[i];
        if (i < started) {
            unsigned result = dn_task_wait(lane, UINT32_MAX);
            if (result > status) status = result;
        }
        ((dn_mem *)lane - 1)->finalize = NULL;
        dn_task_finalize(lane);
        dn_deallocate(lane);
    }
    if (status == 2 && dn_task_cancelled()) return 3;
    return status;
}

static void dn_parallel_check(unsigned status) {
    if (status == 3) dn_fail("parallel operation cancelled");
    if (status == 5) dn_fail("executor queue full or closed");
    if (status != 2) dn_fail("parallel callback failed");
}
