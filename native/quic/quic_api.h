/* DLM QUIC provider ABI 1. Retain the endpoint during every call. */
#ifndef DLM_QUIC_API_H
#define DLM_QUIC_API_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef struct { const uint8_t *data; size_t len; } dq_text;
typedef struct { size_t count; int32_t status, system; } dq_io;
typedef struct { void *handle; int32_t status, system; } dq_open;
typedef struct { uint32_t family, port, scope; uint8_t bytes[16]; } dq_addr;
typedef struct { dq_addr address; int32_t status, system; } dq_resolved;
typedef struct { uint32_t connections, streams, events, idle_ms, handshake_ms, congestion; size_t memory, window; } dq_config;
typedef struct { uint64_t connection; int64_t stream; int32_t status, system; } dq_ref;
typedef struct { uint64_t id, connection; int64_t stream; uint64_t offset, code; size_t count; uint32_t kind, flags; int32_t status, system; } dq_event;
typedef struct { size_t used, limit; uint64_t sent, received, lost, cwnd, in_flight, rtt_ns; uint32_t connections, streams, events, state, version; int32_t status, system; } dq_stats;
typedef struct {
    uint32_t abi, size, ngtcp2_version;
    dq_open (*open)(dq_addr,bool,dq_text,dq_text,dq_text,dq_config);
    void (*close)(void*);
    dq_resolved (*local)(void*);
    dq_ref (*connect)(void*,dq_addr,dq_text);
    dq_io (*poll)(void*,uint32_t,const void*);
    dq_event (*next)(void*);
    dq_io (*copy)(void*,uint64_t,size_t,uint8_t*,size_t);
    dq_io (*ack)(void*,uint64_t);
    dq_ref (*stream)(void*,uint64_t,bool);
    dq_io (*write)(void*,uint64_t,int64_t,const uint8_t*,size_t,bool);
    dq_io (*datagram)(void*,uint64_t,const uint8_t*,size_t);
    dq_io (*reset)(void*,uint64_t,int64_t,uint64_t);
    dq_io (*disconnect)(void*,uint64_t,uint64_t);
    dq_io (*forget)(void*,uint64_t);
    dq_io (*migrate)(void*,uint64_t,dq_addr);
    dq_stats (*stats)(void*,uint64_t);
} dq_api;
#endif
