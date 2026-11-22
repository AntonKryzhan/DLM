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
typedef struct dn_r_ProtocolValue dn_r_ProtocolValue;
typedef struct dn_r_ProtocolFrame dn_r_ProtocolFrame;
typedef struct dn_r_Frame32Decoder dn_r_Frame32Decoder;
typedef struct dn_r_Frame32Step dn_r_Frame32Step;
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
size_t dlm_native_sizeof_ProtocolValue(void);
size_t dlm_native_alignof_ProtocolValue(void);
uint32_t dlm_native_version_ProtocolValue(void);
size_t dlm_native_offset_ProtocolValue_value(void);
size_t dlm_native_offset_ProtocolValue_next(void);
size_t dlm_native_offset_ProtocolValue_status(void);
size_t dlm_native_sizeof_ProtocolFrame(void);
size_t dlm_native_alignof_ProtocolFrame(void);
uint32_t dlm_native_version_ProtocolFrame(void);
size_t dlm_native_offset_ProtocolFrame_payload(void);
size_t dlm_native_offset_ProtocolFrame_count(void);
size_t dlm_native_offset_ProtocolFrame_next(void);
size_t dlm_native_offset_ProtocolFrame_status(void);
size_t dlm_native_sizeof_Frame32Decoder(void);
size_t dlm_native_alignof_Frame32Decoder(void);
uint32_t dlm_native_version_Frame32Decoder(void);
size_t dlm_native_offset_Frame32Decoder_maximum(void);
size_t dlm_native_offset_Frame32Decoder_header(void);
size_t dlm_native_offset_Frame32Decoder_prefix_count(void);
size_t dlm_native_offset_Frame32Decoder_remaining(void);
size_t dlm_native_offset_Frame32Decoder_fault(void);
size_t dlm_native_sizeof_Frame32Step(void);
size_t dlm_native_alignof_Frame32Step(void);
uint32_t dlm_native_version_Frame32Step(void);
size_t dlm_native_offset_Frame32Step_consumed(void);
size_t dlm_native_offset_Frame32Step_payload_offset(void);
size_t dlm_native_offset_Frame32Step_payload_count(void);
size_t dlm_native_offset_Frame32Step_remaining(void);
size_t dlm_native_offset_Frame32Step_complete(void);
size_t dlm_native_offset_Frame32Step_status(void);
#ifdef __cplusplus
}
#endif
#undef DN_HEADER_ATOMIC
#undef DN_HEADER_ASSERT
#endif
