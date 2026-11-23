#ifndef DLM_GATEWAY_H
#define DLM_GATEWAY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { void *handle; int32_t status,system; } dln_gateway_opened;
typedef struct { size_t count; int32_t status,system; } dln_gateway_io;
typedef struct { size_t required,copied; uint64_t value; uint32_t outcome; int32_t status,system; } dln_gateway_transfer;
typedef struct { uint64_t accepted,authenticated,denied,failed,requests,forwarded,returned,active,handshakes; } dln_gateway_statistics;
uint32_t dln_gateway_abi(void);
dln_gateway_opened dln_gateway_client_open(const uint8_t*,size_t);
void dln_gateway_client_close(void*);
dln_gateway_opened dln_gateway_server_open(const uint8_t*,size_t);
void dln_gateway_server_close(void*);
dln_gateway_io dln_gateway_stream_open(void*,uint32_t,const uint8_t*,size_t);
dln_gateway_io dln_gateway_stream_write(void*,uint32_t,const uint8_t*,size_t);
dln_gateway_transfer dln_gateway_stream_read(void*,uint32_t,size_t,uint8_t*,size_t);
dln_gateway_transfer dln_gateway_datagram(void*,uint32_t,const uint8_t*,size_t,uint8_t*,size_t);
dln_gateway_transfer dln_gateway_take(void*,uint8_t*,size_t);
dln_gateway_io dln_gateway_stream_close(void*,uint32_t,bool);
dln_gateway_io dln_gateway_store(void*,const uint8_t*,uint64_t,const uint8_t*,size_t);
dln_gateway_transfer dln_gateway_fetch(void*,const uint8_t*,uint8_t*,size_t);
dln_gateway_io dln_gateway_consume(void*,const uint8_t*);
dln_gateway_statistics dln_gateway_server_statistics(void*);
dln_gateway_io dln_gateway_server_address(void*,uint8_t*,size_t);
typedef struct { uint8_t id[16]; uint64_t stream,length,expires; uint8_t digest[32]; } dln_gateway_descriptor;
typedef struct { dln_gateway_descriptor descriptor; int32_t status,system; } dln_gateway_descriptor_result;
typedef struct { dln_gateway_descriptor descriptor; uint64_t committed; uint32_t state,credit; int32_t status,system; } dln_gateway_delivery_receipt;
typedef struct { uint64_t id,rtt_us,jitter_us,handshake_us,goodput_bps,samples,failures,probes,bytes_committed; uint32_t loss_ppm,profile,ready; int32_t last_error; } dln_gateway_path_metrics;
typedef struct { uint64_t migrations,committed_bytes,retransmitted,fec_shards,probes,active,goodput_bps; uint32_t ready_paths,configured_paths,state,declared_access_domains,independent_pairs,reserved; } dln_gateway_pool_stats;
dln_gateway_opened dln_gateway_pool_open(const uint8_t*,size_t);
void dln_gateway_pool_close(void*);
dln_gateway_io dln_gateway_pool_refresh(void*);
dln_gateway_pool_stats dln_gateway_pool_statistics(void*);
dln_gateway_io dln_gateway_pool_metrics(void*,dln_gateway_path_metrics*,size_t);
dln_gateway_io dln_gateway_pool_disconnect(void*,size_t);
dln_gateway_io dln_gateway_pool_stream_open(void*,uint32_t,const uint8_t*,size_t);
dln_gateway_io dln_gateway_pool_stream_write(void*,uint32_t,const uint8_t*,size_t);
dln_gateway_transfer dln_gateway_pool_stream_read(void*,uint32_t,size_t,uint8_t*,size_t);
dln_gateway_transfer dln_gateway_pool_datagram(void*,uint32_t,const uint8_t*,size_t,uint8_t*,size_t);
dln_gateway_transfer dln_gateway_pool_take(void*,uint8_t*,size_t);
dln_gateway_io dln_gateway_pool_stream_close(void*,uint32_t,bool);
dln_gateway_descriptor_result dln_gateway_delivery_make(const uint8_t*,uint64_t,uint64_t,const uint8_t*,size_t);
dln_gateway_delivery_receipt dln_gateway_pool_upload(void*,dln_gateway_descriptor,const uint8_t*,size_t);
dln_gateway_delivery_receipt dln_gateway_pool_delivery_status(void*,dln_gateway_descriptor);
dln_gateway_delivery_receipt dln_gateway_pool_consume(void*,dln_gateway_descriptor);
dln_gateway_transfer dln_gateway_pool_download(void*,dln_gateway_descriptor,uint8_t*,size_t);
dln_gateway_opened dln_gateway_outbox_open(const uint8_t*,size_t);
void dln_gateway_outbox_close(void*);
dln_gateway_delivery_receipt dln_gateway_outbox_enqueue(void*,dln_gateway_descriptor,const uint8_t*,size_t);
dln_gateway_io dln_gateway_pool_flush(void*,void*);
dln_gateway_transfer dln_gateway_server_listeners(void*,uint8_t*,size_t);
typedef struct { uint64_t accepted,authenticated,denied,active,incoming,outgoing,udp,ip,failed; } dln_gateway_proxy_statistics;
typedef struct { uint32_t family,prefix;uint8_t address[16]; } dln_gateway_ip_assignment;
typedef struct { uint32_t family,protocol;uint8_t first[16],last[16]; } dln_gateway_ip_route;
dln_gateway_opened dln_gateway_proxy_open(const uint8_t*,size_t,uint32_t,const uint8_t*,size_t);
void dln_gateway_proxy_close(void*);
dln_gateway_io dln_gateway_proxy_write(void*,const uint8_t*,size_t);
dln_gateway_transfer dln_gateway_proxy_read(void*,size_t,uint8_t*,size_t);
dln_gateway_transfer dln_gateway_proxy_take(void*,uint8_t*,size_t);
dln_gateway_io dln_gateway_proxy_assignments(void*,dln_gateway_ip_assignment*,size_t);
dln_gateway_io dln_gateway_proxy_routes(void*,dln_gateway_ip_route*,size_t);
dln_gateway_opened dln_gateway_proxy_cancel_open(void*);
dln_gateway_io dln_gateway_proxy_cancel_request(void*);
void dln_gateway_proxy_cancel_close(void*);
dln_gateway_opened dln_gateway_proxy_server_open(const uint8_t*,size_t);
void dln_gateway_proxy_server_close(void*);
dln_gateway_io dln_gateway_proxy_server_stop(void*);
dln_gateway_proxy_statistics dln_gateway_proxy_server_statistics(void*);
dln_gateway_io dln_gateway_proxy_server_address(void*,uint8_t*,size_t);
dln_gateway_transfer dln_gateway_carriers(uint8_t*,size_t);
#ifdef __cplusplus
}
#endif
#endif
