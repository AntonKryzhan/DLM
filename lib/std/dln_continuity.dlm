module std.dln_continuity
import std.dln_gateway

native {
    trust "dlm.gateway.v1";
    record GatewayPool repr(C) { inner: Resource<NetOpened,gateway_pool_dispose>; }
    record GatewayOutbox repr(C) { inner: Resource<NetOpened,gateway_outbox_dispose>; }
    record GatewayDeliveryDescriptor repr(C, version=1) { id: Array<U8,16>; stream: U64; length: U64; expires: U64; digest: Array<U8,32>; }
    record GatewayDescriptorResult repr(C, version=1) { descriptor: GatewayDeliveryDescriptor; status: I32; system: I32; }
    record GatewayDeliveryReceipt repr(C, version=1) { descriptor: GatewayDeliveryDescriptor; committed: U64; state: U32; credit: U32; status: I32; system: I32; }
    record GatewayPathMetrics repr(C, version=1) { id: U64; rtt_us: U64; jitter_us: U64; handshake_us: U64; goodput_bps: U64; samples: U64; failures: U64; probes: U64; bytes_committed: U64; loss_ppm: U32; profile: U32; ready: U32; last_error: I32; }
    record GatewayPoolStatistics repr(C, version=1) { migrations: U64; committed_bytes: U64; retransmitted: U64; fec_shards: U64; probes: U64; active: U64; goodput_bps: U64; ready_paths: U32; configured_paths: U32; state: U32; declared_access_domains: U32; independent_pairs: U32; reserved: U32; }
    extern "C" fn dln_gateway_pool_open(a0: Ptr<U8>, a1: USize) -> NetOpened from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_pool_close(a0: OpaqueHandle) -> Unit from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_pool_refresh(a0: OpaqueHandle) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_pool_statistics(a0: OpaqueHandle) -> GatewayPoolStatistics from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_pool_metrics(a0: OpaqueHandle, a1: Ptr<GatewayPathMetrics>, a2: USize) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_pool_disconnect(a0: OpaqueHandle, a1: USize) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_pool_stream_open(a0: OpaqueHandle, a1: U32, a2: Ptr<U8>, a3: USize) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_pool_stream_write(a0: OpaqueHandle, a1: U32, a2: Ptr<U8>, a3: USize) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_pool_stream_read(a0: OpaqueHandle, a1: U32, a2: USize, a3: Ptr<U8>, a4: USize) -> GatewayNativeTransfer from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_pool_datagram(a0: OpaqueHandle, a1: U32, a2: Ptr<U8>, a3: USize, a4: Ptr<U8>, a5: USize) -> GatewayNativeTransfer from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_pool_take(a0: OpaqueHandle, a1: Ptr<U8>, a2: USize) -> GatewayNativeTransfer from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_pool_stream_close(a0: OpaqueHandle, a1: U32, a2: Bool) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_delivery_make(a0: Ptr<U8>, a1: U64, a2: U64, a3: Ptr<U8>, a4: USize) -> GatewayDescriptorResult from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_pool_upload(a0: OpaqueHandle, a1: GatewayDeliveryDescriptor, a2: Ptr<U8>, a3: USize) -> GatewayDeliveryReceipt from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_pool_delivery_status(a0: OpaqueHandle, a1: GatewayDeliveryDescriptor) -> GatewayDeliveryReceipt from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_pool_consume(a0: OpaqueHandle, a1: GatewayDeliveryDescriptor) -> GatewayDeliveryReceipt from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_pool_download(a0: OpaqueHandle, a1: GatewayDeliveryDescriptor, a2: Ptr<U8>, a3: USize) -> GatewayNativeTransfer from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_outbox_open(a0: Ptr<U8>, a1: USize) -> NetOpened from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_outbox_close(a0: OpaqueHandle) -> Unit from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_outbox_enqueue(a0: OpaqueHandle, a1: GatewayDeliveryDescriptor, a2: Ptr<U8>, a3: USize) -> GatewayDeliveryReceipt from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_pool_flush(a0: OpaqueHandle, a1: OpaqueHandle) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_server_listeners(a0: OpaqueHandle, a1: Ptr<U8>, a2: USize) -> GatewayNativeTransfer from "dlm.gateway.v1" guarantees(NoUnwind);
    unsafe fn gateway_pool_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { dln_gateway_pool_close(state.handle); }
    unsafe fn gateway_outbox_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { dln_gateway_outbox_close(state.handle); }
    fn gateway_pool(config: Slice<U8>) -> GatewayPool { unsafe { let text: NetText=net_bytes(config);return GatewayPool(resource(dln_gateway_pool_open(text.data,text.count))); } }
    fn gateway_outbox(config: Slice<U8>) -> GatewayOutbox { unsafe { let text: NetText=net_bytes(config);return GatewayOutbox(resource(dln_gateway_outbox_open(text.data,text.count))); } }
    fn gateway_pool_error(pool: &GatewayPool) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { let state: &NetOpened=resource_get(&pool.inner);return NetworkError(state.status,state.system,180); }
    fn gateway_outbox_error(queue: &GatewayOutbox) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { let state: &NetOpened=resource_get(&queue.inner);return NetworkError(state.status,state.system,181); }
    fn gateway_pool_close(pool: GatewayPool) -> Unit { resource_close(move pool.inner); }
    fn gateway_outbox_close(queue: GatewayOutbox) -> Unit { resource_close(move queue.inner); }
    fn gateway_pool_refresh(pool: &mut GatewayPool) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&pool.inner);return socket_transfer(dln_gateway_pool_refresh(state.handle),182); } }
    fn gateway_pool_statistics(pool: &GatewayPool) -> GatewayPoolStatistics { unsafe { let state: &NetOpened=resource_get(&pool.inner);return dln_gateway_pool_statistics(state.handle); } }
    fn gateway_pool_metrics(pool: &GatewayPool, output: &mut Buffer<GatewayPathMetrics>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&pool.inner);if len(output)==0 { return socket_transfer(dln_gateway_pool_metrics(state.handle,null<GatewayPathMetrics>(),0),183); }return socket_transfer(dln_gateway_pool_metrics(state.handle,as_raw(borrowed_ptr(&mut output[0])),len(output)),183); } }
    fn gateway_pool_disconnect(pool: &mut GatewayPool, index: USize) -> NetworkError { unsafe { let state: &NetOpened=resource_get(&pool.inner);let result: NetIo=dln_gateway_pool_disconnect(state.handle,index);return NetworkError(result.status,result.system,184); } }
    fn gateway_pool_stream(pool: &mut GatewayPool, protocol: U32, target: Slice<U8>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&pool.inner);let text: NetText=net_bytes(target);return socket_transfer(dln_gateway_pool_stream_open(state.handle,protocol,text.data,text.count),185); } }
    fn gateway_pool_write(pool: &mut GatewayPool, stream: U32, input: Slice<U8>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&pool.inner);let text: NetText=net_bytes(input);return socket_transfer(dln_gateway_pool_stream_write(state.handle,stream,text.data,text.count),186); } }
    fn gateway_pool_read(pool: &mut GatewayPool, stream: U32, maximum: USize, output: &mut Buffer<U8>) -> GatewayTransfer { unsafe { let state: &NetOpened=resource_get(&pool.inner);return gateway_transfer(dln_gateway_pool_stream_read(state.handle,stream,maximum,net_output(output),len(output))); } }
    fn gateway_pool_datagram(pool: &mut GatewayPool, stream: U32, input: Slice<U8>, output: &mut Buffer<U8>) -> GatewayTransfer { unsafe { let state: &NetOpened=resource_get(&pool.inner);let text: NetText=net_bytes(input);return gateway_transfer(dln_gateway_pool_datagram(state.handle,stream,text.data,text.count,net_output(output),len(output))); } }
    fn gateway_pool_take(pool: &mut GatewayPool, output: &mut Buffer<U8>) -> GatewayTransfer { unsafe { let state: &NetOpened=resource_get(&pool.inner);return gateway_transfer(dln_gateway_pool_take(state.handle,net_output(output),len(output))); } }
    fn gateway_pool_stream_close(pool: &mut GatewayPool, stream: U32, half: Bool) -> NetworkError { unsafe { let state: &NetOpened=resource_get(&pool.inner);let result: NetIo=dln_gateway_pool_stream_close(state.handle,stream,half);return NetworkError(result.status,result.system,187); } }
    fn gateway_delivery(id: Slice<U8>, stream: U64, expires: U64, input: Slice<U8>) -> GatewayDescriptorResult { if len(id)!=16 { return GatewayDescriptorResult(zero<GatewayDeliveryDescriptor>(),4,0); }unsafe { let key: NetText=net_bytes(id);let text: NetText=net_bytes(input);return dln_gateway_delivery_make(key.data,stream,expires,text.data,text.count); } }
    fn gateway_pool_upload(pool: &mut GatewayPool, descriptor: GatewayDeliveryDescriptor, input: Slice<U8>) -> GatewayDeliveryReceipt { unsafe { let state: &NetOpened=resource_get(&pool.inner);let text: NetText=net_bytes(input);return dln_gateway_pool_upload(state.handle,descriptor,text.data,text.count); } }
    fn gateway_pool_delivery_status(pool: &mut GatewayPool, descriptor: GatewayDeliveryDescriptor) -> GatewayDeliveryReceipt { unsafe { let state: &NetOpened=resource_get(&pool.inner);return dln_gateway_pool_delivery_status(state.handle,descriptor); } }
    fn gateway_pool_consume(pool: &mut GatewayPool, descriptor: GatewayDeliveryDescriptor) -> GatewayDeliveryReceipt { unsafe { let state: &NetOpened=resource_get(&pool.inner);return dln_gateway_pool_consume(state.handle,descriptor); } }
    fn gateway_pool_download(pool: &mut GatewayPool, descriptor: GatewayDeliveryDescriptor, output: &mut Buffer<U8>) -> GatewayTransfer { unsafe { let state: &NetOpened=resource_get(&pool.inner);return gateway_transfer(dln_gateway_pool_download(state.handle,descriptor,net_output(output),len(output))); } }
    fn gateway_outbox_enqueue(queue: &mut GatewayOutbox, descriptor: GatewayDeliveryDescriptor, input: Slice<U8>) -> GatewayDeliveryReceipt { unsafe { let state: &NetOpened=resource_get(&queue.inner);let text: NetText=net_bytes(input);return dln_gateway_outbox_enqueue(state.handle,descriptor,text.data,text.count); } }
    fn gateway_pool_flush(pool: &mut GatewayPool, queue: &mut GatewayOutbox) -> NetworkTransfer { unsafe { let client: &NetOpened=resource_get(&pool.inner);let journal: &NetOpened=resource_get(&queue.inner);return socket_transfer(dln_gateway_pool_flush(client.handle,journal.handle),188); } }
    fn gateway_listeners(server: &GatewayServer, output: &mut Buffer<U8>) -> GatewayTransfer { unsafe { let state: &NetOpened=resource_get(&server.inner);return gateway_transfer(dln_gateway_server_listeners(state.handle,net_output(output),len(output))); } }
    fn gateway_delivery_accepted_local() -> U32 guarantees(NoAlloc,NoBlocking,NoIO) { return 1; }
    fn gateway_delivery_accepted_remote() -> U32 guarantees(NoAlloc,NoBlocking,NoIO) { return 2; }
    fn gateway_delivery_committed() -> U32 guarantees(NoAlloc,NoBlocking,NoIO) { return 3; }
    fn gateway_delivery_completed() -> U32 guarantees(NoAlloc,NoBlocking,NoIO) { return 4; }
    fn gateway_delivery_consumed() -> U32 guarantees(NoAlloc,NoBlocking,NoIO) { return 5; }
}
