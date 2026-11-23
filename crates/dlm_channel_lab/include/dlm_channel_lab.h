#ifndef DLM_CHANNEL_LAB_H
#define DLM_CHANNEL_LAB_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { void *handle; int32_t status; int32_t reserved; } dlm_lab_opened;
typedef struct { uint64_t count; int32_t status; int32_t reserved; } dlm_lab_io;
typedef struct { uint64_t required; uint64_t copied; int32_t status; uint32_t delivery; } dlm_lab_transfer;
typedef struct { uint32_t delivery; uint32_t mode; uint64_t symbols; uint64_t events; uint64_t payload_bytes; uint64_t decoded_bytes; uint64_t corrected_words; uint64_t duration_us; } dlm_lab_summary;
typedef struct { uint64_t time_us; uint32_t size; uint32_t kind; } dlm_lab_event;
uint32_t dlm_channel_lab_abi(void);
dlm_lab_opened dlm_channel_lab_open(const uint8_t*,size_t);
dlm_lab_io dlm_channel_lab_simulate(void*,const uint8_t*,size_t);
dlm_lab_io dlm_channel_lab_loopback(void*,const uint8_t*,size_t);
dlm_lab_opened dlm_channel_lab_decode(const uint8_t*,size_t);
dlm_lab_transfer dlm_channel_lab_read(void*,uint32_t,uint8_t*,size_t);
dlm_lab_summary dlm_channel_lab_summary(void*);
dlm_lab_io dlm_channel_lab_event(void*,size_t,dlm_lab_event*);
void dlm_channel_lab_close(void*);
dlm_lab_opened dlm_channel_lab_cancellation(void*);
dlm_lab_io dlm_channel_lab_cancel(void*);
void dlm_channel_lab_cancel_close(void*);
#ifdef __cplusplus
}
#endif
#endif
