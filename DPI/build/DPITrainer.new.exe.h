#ifndef DLM_NATIVE_APPLICATION_H
#define DLM_NATIVE_APPLICATION_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
#include <atomic>
#define DN_HEADER_ATOMIC(T) std::atomic<T>
#define DN_HEADER_ASSERT static_assert
#else
#include <stdatomic.h>
#define DN_HEADER_ATOMIC(T) _Atomic(T)
#define DN_HEADER_ASSERT _Static_assert
#endif
typedef struct dn_utf8 {const uint8_t *data;size_t len;} dn_utf8;
typedef struct dn_window dn_window;
typedef struct dn_r_Host dn_r_Host;
typedef struct dn_r_Gene dn_r_Gene;
typedef struct dn_r_Individual dn_r_Individual;
typedef struct dn_r_Overlapped dn_r_Overlapped;
#ifdef __cplusplus
extern "C" {
#endif
const char *dlm_native_last_error(void);
void dlm_native_clear_error(void);
uint32_t dlm_native_abi_version(void);
uint64_t dlm_native_thread_init(void);
uint64_t dlm_native_allocations(void);
uint64_t dlm_native_frees(void);
int dlm_native_cpu_profile(void);
uint64_t dlm_native_live_allocations(void);
bool dlm_native_runtime_init(void);
bool dlm_native_runtime_shutdown(void);
size_t dlm_native_sizeof_Host(void);
size_t dlm_native_alignof_Host(void);
uint32_t dlm_native_version_Host(void);
size_t dlm_native_offset_Host_bytes(void);
size_t dlm_native_offset_Host_length(void);
size_t dlm_native_sizeof_Gene(void);
size_t dlm_native_alignof_Gene(void);
uint32_t dlm_native_version_Gene(void);
size_t dlm_native_offset_Gene_reverse(void);
size_t dlm_native_offset_Gene_cut1(void);
size_t dlm_native_offset_Gene_cut2(void);
size_t dlm_native_offset_Gene_overlap(void);
size_t dlm_native_offset_Gene_guard(void);
size_t dlm_native_offset_Gene_repeats(void);
size_t dlm_native_offset_Gene_gap(void);
size_t dlm_native_offset_Gene_template_(void);
size_t dlm_native_sizeof_Individual(void);
size_t dlm_native_alignof_Individual(void);
uint32_t dlm_native_version_Individual(void);
size_t dlm_native_offset_Individual_gene(void);
size_t dlm_native_offset_Individual_fitness(void);
size_t dlm_native_offset_Individual_trials(void);
size_t dlm_native_sizeof_Overlapped(void);
size_t dlm_native_alignof_Overlapped(void);
uint32_t dlm_native_version_Overlapped(void);
size_t dlm_native_offset_Overlapped_internal(void);
size_t dlm_native_offset_Overlapped_internal_high(void);
size_t dlm_native_offset_Overlapped_offset(void);
size_t dlm_native_offset_Overlapped_event(void);
#ifdef __cplusplus
}
#endif
#undef DN_HEADER_ATOMIC
#undef DN_HEADER_ASSERT
#endif
