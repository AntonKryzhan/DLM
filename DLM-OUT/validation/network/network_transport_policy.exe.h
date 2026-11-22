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
typedef struct dn_r_TransportRequirements dn_r_TransportRequirements;
typedef struct dn_r_TransportBounds dn_r_TransportBounds;
typedef struct dn_r_TransportCandidate dn_r_TransportCandidate;
typedef struct dn_r_TransportCheck dn_r_TransportCheck;
typedef struct dn_r_TransportChoice dn_r_TransportChoice;
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
size_t dlm_native_sizeof_TransportRequirements(void);
size_t dlm_native_alignof_TransportRequirements(void);
uint32_t dlm_native_version_TransportRequirements(void);
size_t dlm_native_offset_TransportRequirements_required(void);
size_t dlm_native_offset_TransportRequirements_forbidden(void);
size_t dlm_native_offset_TransportRequirements_preferred(void);
size_t dlm_native_sizeof_TransportBounds(void);
size_t dlm_native_alignof_TransportBounds(void);
uint32_t dlm_native_version_TransportBounds(void);
size_t dlm_native_offset_TransportBounds_max_message(void);
size_t dlm_native_offset_TransportBounds_queued_messages(void);
size_t dlm_native_offset_TransportBounds_channels(void);
size_t dlm_native_offset_TransportBounds_memory(void);
size_t dlm_native_sizeof_TransportCandidate(void);
size_t dlm_native_alignof_TransportCandidate(void);
uint32_t dlm_native_version_TransportCandidate(void);
size_t dlm_native_offset_TransportCandidate_profile(void);
size_t dlm_native_offset_TransportCandidate_available(void);
size_t dlm_native_offset_TransportCandidate_cost(void);
size_t dlm_native_sizeof_TransportCheck(void);
size_t dlm_native_alignof_TransportCheck(void);
uint32_t dlm_native_version_TransportCheck(void);
size_t dlm_native_offset_TransportCheck_properties(void);
size_t dlm_native_offset_TransportCheck_missing(void);
size_t dlm_native_offset_TransportCheck_forbidden(void);
size_t dlm_native_offset_TransportCheck_status(void);
size_t dlm_native_sizeof_TransportChoice(void);
size_t dlm_native_alignof_TransportChoice(void);
uint32_t dlm_native_version_TransportChoice(void);
size_t dlm_native_offset_TransportChoice_profile(void);
size_t dlm_native_offset_TransportChoice_properties(void);
size_t dlm_native_offset_TransportChoice_missing(void);
size_t dlm_native_offset_TransportChoice_forbidden(void);
size_t dlm_native_offset_TransportChoice_preference_misses(void);
size_t dlm_native_offset_TransportChoice_cost(void);
size_t dlm_native_offset_TransportChoice_status(void);
#ifdef __cplusplus
}
#endif
#undef DN_HEADER_ATOMIC
#undef DN_HEADER_ASSERT
#endif
