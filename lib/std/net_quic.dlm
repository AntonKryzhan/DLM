module std.net_quic
import std.net_socket

native {
    trust "dlm.net.v1";
    record QuicEndpoint repr(C) { inner: Resource<NetOpened,quic_dispose>; }
    record QuicConnection repr(C) { id: U64; }
    record QuicStream repr(C) { connection: QuicConnection; id: I64; }
    record QuicConnectionResult repr(C) { connection: QuicConnection; error: NetworkError; }
    record QuicStreamResult repr(C) { stream: QuicStream; error: NetworkError; }
    record QuicConfig repr(C, version=1) { connections: U32; streams: U32; events: U32; idle_ms: U32; handshake_ms: U32; congestion: U32; memory: USize; window: USize; }
    record QuicNativeReference repr(C, version=1) { connection: U64; stream: I64; status: I32; system: I32; }
    record QuicNativeEvent repr(C, version=1) { id: U64; connection: U64; stream: I64; offset: U64; code: U64; count: USize; kind: U32; flags: U32; status: I32; system: I32; }
    record QuicEvent repr(C) { id: U64; stream: QuicStream; offset: U64; code: U64; count: USize; kind: U32; flags: U32; error: NetworkError; }
    record QuicNativeStatistics repr(C, version=1) { used: USize; limit: USize; sent: U64; received: U64; lost: U64; cwnd: U64; in_flight: U64; rtt_ns: U64; connections: U32; streams: U32; events: U32; state: U32; version: U32; status: I32; system: I32; }
    record QuicStatistics repr(C) { used: USize; limit: USize; sent: U64; received: U64; lost: U64; cwnd: U64; in_flight: U64; rtt_ns: U64; connections: U32; streams: U32; events: U32; state: U32; version: U32; error: NetworkError; }

    extern "C" fn dlm_net_quic_version() -> U32 from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_quic_shutdown() -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_quic_open(address: NetAddress, server: Bool, alpn: NetText, certificate: NetText, key: NetText, config: QuicConfig) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_quic_close(endpoint: OpaqueHandle) -> Unit from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_quic_local(endpoint: OpaqueHandle) -> NetResolved from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_quic_connect(endpoint: OpaqueHandle, address: NetAddress, name: NetText) -> QuicNativeReference from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_quic_poll(endpoint: OpaqueHandle, timeout_ms: U32, cancel: OpaqueHandle) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_quic_next(endpoint: OpaqueHandle) -> QuicNativeEvent from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_quic_copy(endpoint: OpaqueHandle, event: U64, offset: USize, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_quic_ack(endpoint: OpaqueHandle, event: U64) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_quic_stream(endpoint: OpaqueHandle, connection: U64, unidirectional: Bool) -> QuicNativeReference from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_quic_write(endpoint: OpaqueHandle, connection: U64, stream: I64, bytes: Ptr<U8>, count: USize, fin: Bool) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_quic_datagram(endpoint: OpaqueHandle, connection: U64, bytes: Ptr<U8>, count: USize) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_quic_reset(endpoint: OpaqueHandle, connection: U64, stream: I64, code: U64) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_quic_disconnect(endpoint: OpaqueHandle, connection: U64, code: U64) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_quic_forget(endpoint: OpaqueHandle, connection: U64) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_quic_migrate(endpoint: OpaqueHandle, connection: U64, address: NetAddress) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_quic_stats(endpoint: OpaqueHandle, connection: U64) -> QuicNativeStatistics from "dlm.net.v1" guarantees(NoUnwind);

    fn quic_dispose(state: &mut NetOpened) -> Unit { unsafe { dlm_net_quic_close(state.handle); } }
    fn quic_handle(endpoint: &QuicEndpoint) -> OpaqueHandle guarantees(NoAlloc,NoBlocking,NoIO) { let state: &NetOpened=resource_get(&endpoint.inner);return state.handle; }
    fn quic_config() -> QuicConfig guarantees(NoAlloc,NoBlocking,NoIO) { return QuicConfig(16,64,1024,30000,10000,2,16777216,65536); }
    fn quic_available() -> Bool { unsafe { return dlm_net_quic_version()!=0; } }
    fn quic_client(address: SocketAddress, alpn: Utf8, ca_file: Utf8, config: QuicConfig) -> QuicEndpoint {
        unsafe { return QuicEndpoint(resource(dlm_net_quic_open(address.value,false,net_text(alpn),net_text(ca_file),net_text(""),config))); }
    }
    fn quic_server(address: SocketAddress, alpn: Utf8, certificate: Utf8, key: Utf8, config: QuicConfig) -> QuicEndpoint {
        unsafe { return QuicEndpoint(resource(dlm_net_quic_open(address.value,true,net_text(alpn),net_text(certificate),net_text(key),config))); }
    }
    fn quic_error(endpoint: &QuicEndpoint) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { let state: &NetOpened=resource_get(&endpoint.inner);return NetworkError(state.status,state.system,70); }
    fn quic_close(endpoint: QuicEndpoint) -> Unit { resource_close(move endpoint.inner); }
    fn quic_local(endpoint: &QuicEndpoint) -> SocketAddressResult { unsafe { return socket_address_result(dlm_net_quic_local(quic_handle(endpoint)),71); } }
    fn quic_connect(endpoint: &mut QuicEndpoint, address: SocketAddress, hostname: Utf8) -> QuicConnectionResult {
        unsafe { let r: QuicNativeReference=dlm_net_quic_connect(quic_handle(endpoint),address.value,net_text(hostname));return QuicConnectionResult(QuicConnection(r.connection),NetworkError(r.status,r.system,72)); }
    }
    fn quic_poll(endpoint: &mut QuicEndpoint, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_quic_poll(quic_handle(endpoint),timeout_ms,*resource_get(cancel)),73); }
    }
    fn quic_next(endpoint: &QuicEndpoint) -> QuicEvent {
        unsafe { let r: QuicNativeEvent=dlm_net_quic_next(quic_handle(endpoint));return QuicEvent(r.id,QuicStream(QuicConnection(r.connection),r.stream),r.offset,r.code,r.count,r.kind,r.flags,NetworkError(r.status,r.system,74)); }
    }
    fn quic_copy(endpoint: &QuicEndpoint, event: U64, offset: USize, output: &mut Buffer<U8>) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_quic_copy(quic_handle(endpoint),event,offset,net_output(output),len(output)),75); }
    }
    fn quic_ack(endpoint: &mut QuicEndpoint, event: U64) -> NetworkTransfer { unsafe { return socket_transfer(dlm_net_quic_ack(quic_handle(endpoint),event),76); } }
    fn quic_open_stream(endpoint: &mut QuicEndpoint, connection: QuicConnection, unidirectional: Bool) -> QuicStreamResult {
        unsafe { let r: QuicNativeReference=dlm_net_quic_stream(quic_handle(endpoint),connection.id,unidirectional);return QuicStreamResult(QuicStream(QuicConnection(r.connection),r.stream),NetworkError(r.status,r.system,77)); }
    }
    fn quic_write(endpoint: &mut QuicEndpoint, stream: QuicStream, bytes: Slice<U8>, fin: Bool) -> NetworkTransfer {
        unsafe { let data: NetText=net_bytes(bytes);return socket_transfer(dlm_net_quic_write(quic_handle(endpoint),stream.connection.id,stream.id,data.data,data.count,fin),78); }
    }
    fn quic_datagram(endpoint: &mut QuicEndpoint, connection: QuicConnection, bytes: Slice<U8>) -> NetworkTransfer {
        unsafe { let data: NetText=net_bytes(bytes);return socket_transfer(dlm_net_quic_datagram(quic_handle(endpoint),connection.id,data.data,data.count),79); }
    }
    fn quic_reset(endpoint: &mut QuicEndpoint, stream: QuicStream, code: U64) -> NetworkTransfer { unsafe { return socket_transfer(dlm_net_quic_reset(quic_handle(endpoint),stream.connection.id,stream.id,code),80); } }
    fn quic_disconnect(endpoint: &mut QuicEndpoint, connection: QuicConnection, code: U64) -> NetworkTransfer { unsafe { return socket_transfer(dlm_net_quic_disconnect(quic_handle(endpoint),connection.id,code),81); } }
    fn quic_forget(endpoint: &mut QuicEndpoint, connection: QuicConnection) -> NetworkTransfer { unsafe { return socket_transfer(dlm_net_quic_forget(quic_handle(endpoint),connection.id),82); } }
    fn quic_migrate(endpoint: &mut QuicEndpoint, connection: QuicConnection, address: SocketAddress) -> NetworkTransfer { unsafe { return socket_transfer(dlm_net_quic_migrate(quic_handle(endpoint),connection.id,address.value),83); } }
    fn quic_statistics(endpoint: &QuicEndpoint, connection: QuicConnection) -> QuicStatistics {
        unsafe { let r: QuicNativeStatistics=dlm_net_quic_stats(quic_handle(endpoint),connection.id);return QuicStatistics(r.used,r.limit,r.sent,r.received,r.lost,r.cwnd,r.in_flight,r.rtt_ns,r.connections,r.streams,r.events,r.state,r.version,NetworkError(r.status,r.system,84)); }
    }
    fn quic_event_fin(event: &QuicEvent) -> Bool guarantees(NoAlloc,NoBlocking,NoIO) { return event.kind==3 && (event.flags&1)!=0; }
    // Host shutdown requires quiescence and no live endpoints.
    unsafe fn quic_provider_shutdown() -> NetIo { return dlm_net_quic_shutdown(); }
}
