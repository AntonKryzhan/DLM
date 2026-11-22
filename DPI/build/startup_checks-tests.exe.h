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
typedef struct dn_r_StartupView dn_r_StartupView;
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
size_t dlm_native_sizeof_StartupView(void);
size_t dlm_native_alignof_StartupView(void);
uint32_t dlm_native_version_StartupView(void);
size_t dlm_native_offset_StartupView_found(void);
size_t dlm_native_offset_StartupView_ready(void);
size_t dlm_native_offset_StartupView_driver(void);
size_t dlm_native_offset_StartupView_error(void);
#ifdef __cplusplus
}
#endif
#undef DN_HEADER_ATOMIC
#undef DN_HEADER_ASSERT
#endif
