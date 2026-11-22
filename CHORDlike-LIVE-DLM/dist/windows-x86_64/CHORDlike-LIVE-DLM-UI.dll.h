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
typedef struct dn_r_Editor dn_r_Editor;
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
void * live_ui_create(size_t v_parent);
size_t live_ui_handle(void * v_h);
void live_ui_resize(void * v_h,size_t v_width,size_t v_height);
void live_ui_draw(void * v_h,double * v_values,size_t v_count);
int32_t live_ui_hit(size_t v_x,size_t v_y,size_t v_width,size_t v_height);
double live_ui_slider_value(size_t v_x,size_t v_width,size_t v_index);
void live_ui_destroy(void * v_h);
size_t dlm_native_sizeof_Editor(void);
size_t dlm_native_alignof_Editor(void);
uint32_t dlm_native_version_Editor(void);
size_t dlm_native_offset_Editor_window(void);
size_t dlm_native_offset_Editor_width(void);
size_t dlm_native_offset_Editor_height(void);
#ifdef __cplusplus
}
#endif
#undef DN_HEADER_ATOMIC
#undef DN_HEADER_ASSERT
#endif
