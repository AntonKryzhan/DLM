module std.net_reactor
import std.net_socket

native {
    trust "dlm.net.v1";
    record NetworkReactor repr(C) { inner: Resource<NetOpened,reactor_dispose>; }
    // Keys identify resources owned by the reactor, not independent owners.
    record ReactorTcp repr(C) { id: U64; }
    record ReactorListener repr(C) { id: U64; }
    record ReactorUdp repr(C) { id: U64; }
    record ReactorTcpResult repr(C) { channel: ReactorTcp; error: NetworkError; }
    record ReactorListenerResult repr(C) { channel: ReactorListener; error: NetworkError; }
    record ReactorUdpResult repr(C) { channel: ReactorUdp; error: NetworkError; }
    record NetworkOperation repr(C) { id: U64; error: NetworkError; }
    record ReactorConnect repr(C) { channel: ReactorTcp; operation: NetworkOperation; }
    record ReactorCompletion repr(C) { operation: U64; channel: U64; accepted: ReactorTcp; transfer: NetworkTransfer; kind: U32; peer: SocketAddress; }
    record ReactorStatistics repr(C) { buffered_bytes: USize; byte_limit: USize; channels: U32; pending: U32; completed: U32; channel_limit: U32; operation_limit: U32; backend: U32; error: NetworkError; }
    record ReactorNativeRequest repr(C, version=1) { id: U64; channel: U64; status: I32; system: I32; }
    record ReactorNativeCompletion repr(C, version=1) { id: U64; channel: U64; accepted: U64; count: USize; status: I32; system: I32; kind: U32; peer: NetAddress; }
    record ReactorNativeStats repr(C, version=1) { buffered_bytes: USize; byte_limit: USize; channels: U32; pending: U32; completed: U32; channel_limit: U32; operation_limit: U32; backend: U32; status: I32; system: I32; }

    extern "C" fn dlm_net_reactor_new(channels: U32, operations: U32, byte_limit: USize) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_reactor_close(reactor: OpaqueHandle) -> Unit from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_reactor_adopt(reactor: OpaqueHandle, socket: NetOpened, kind: U32) -> ReactorNativeRequest from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_reactor_submit(reactor: OpaqueHandle, channel: U64, kind: U32, bytes: Ptr<U8>, count: USize, exact: Bool, timeout_ms: U32, peer: NetAddress) -> ReactorNativeRequest from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_reactor_connect(reactor: OpaqueHandle, address: NetAddress, timeout_ms: U32) -> ReactorNativeRequest from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_reactor_timer(reactor: OpaqueHandle, timeout_ms: U32, event: Bool) -> ReactorNativeRequest from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_reactor_signal(reactor: OpaqueHandle, operation: U64) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_reactor_cancel(reactor: OpaqueHandle, operation: U64) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_reactor_poll(reactor: OpaqueHandle, timeout_ms: U32, cancel: OpaqueHandle) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_reactor_result(reactor: OpaqueHandle, operation: U64) -> ReactorNativeCompletion from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_reactor_copy(reactor: OpaqueHandle, operation: U64, offset: USize, bytes: Ptr<U8>, capacity: USize) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_reactor_ack(reactor: OpaqueHandle, operation: U64) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_reactor_channel_close(reactor: OpaqueHandle, channel: U64) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_reactor_release(reactor: OpaqueHandle, channel: U64, kind: U32) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_reactor_stats(reactor: OpaqueHandle) -> ReactorNativeStats from "dlm.net.v1" guarantees(NoUnwind);

    fn reactor_dispose(state: &mut NetOpened) -> Unit { unsafe { dlm_net_reactor_close(state.handle); } }
    fn reactor_handle(reactor: &NetworkReactor) -> OpaqueHandle guarantees(NoAlloc,NoBlocking,NoIO) {
        let state: &NetOpened=resource_get(&reactor.inner);return state.handle;
    }
    fn reactor_operation(result: ReactorNativeRequest) -> NetworkOperation guarantees(NoAlloc,NoBlocking,NoIO) {
        return NetworkOperation(result.id,NetworkError(result.status,result.system,21));
    }
    fn reactor_completion(result: ReactorNativeCompletion) -> ReactorCompletion guarantees(NoAlloc,NoBlocking,NoIO) {
        return ReactorCompletion(result.id,result.channel,ReactorTcp(result.accepted),NetworkTransfer(result.count,NetworkError(result.status,result.system,28)),result.kind,SocketAddress(result.peer));
    }
    fn reactor_open(channels: U32, operations: U32, byte_limit: USize) -> NetworkReactor {
        unsafe { return NetworkReactor(resource(dlm_net_reactor_new(channels,operations,byte_limit))); }
    }
    fn reactor_error(reactor: &NetworkReactor) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) {
        let state: &NetOpened=resource_get(&reactor.inner);return NetworkError(state.status,state.system,20);
    }
    fn reactor_close(reactor: NetworkReactor) -> Unit { resource_close(move reactor.inner); }
    fn reactor_adopt_tcp(reactor: &mut NetworkReactor, socket: TcpSocket) -> ReactorTcpResult {
        unsafe { let result: ReactorNativeRequest=dlm_net_reactor_adopt(reactor_handle(reactor),resource_into_inner(move socket.inner),1);return ReactorTcpResult(ReactorTcp(result.channel),NetworkError(result.status,result.system,21)); }
    }
    fn reactor_adopt_listener(reactor: &mut NetworkReactor, socket: TcpListener) -> ReactorListenerResult {
        unsafe { let result: ReactorNativeRequest=dlm_net_reactor_adopt(reactor_handle(reactor),resource_into_inner(move socket.inner),2);return ReactorListenerResult(ReactorListener(result.channel),NetworkError(result.status,result.system,21)); }
    }
    fn reactor_adopt_udp(reactor: &mut NetworkReactor, socket: UdpSocket) -> ReactorUdpResult {
        unsafe { let result: ReactorNativeRequest=dlm_net_reactor_adopt(reactor_handle(reactor),resource_into_inner(move socket.inner),3);return ReactorUdpResult(ReactorUdp(result.channel),NetworkError(result.status,result.system,21)); }
    }
    fn reactor_connect(reactor: &mut NetworkReactor, address: SocketAddress, timeout_ms: U32) -> ReactorConnect {
        unsafe { let result: ReactorNativeRequest=dlm_net_reactor_connect(reactor_handle(reactor),address.value,timeout_ms);return ReactorConnect(ReactorTcp(result.channel),reactor_operation(result)); }
    }
    fn reactor_read(reactor: &mut NetworkReactor, channel: ReactorTcp, count: USize, exact: Bool, timeout_ms: U32) -> NetworkOperation {
        unsafe { return reactor_operation(dlm_net_reactor_submit(reactor_handle(reactor),channel.id,1,zero<Ptr<U8>>(),count,exact,timeout_ms,zero<NetAddress>())); }
    }
    fn reactor_write(reactor: &mut NetworkReactor, channel: ReactorTcp, bytes: Slice<U8>, timeout_ms: U32) -> NetworkOperation {
        unsafe { let input: NetText=net_bytes(bytes);return reactor_operation(dlm_net_reactor_submit(reactor_handle(reactor),channel.id,2,input.data,input.count,true,timeout_ms,zero<NetAddress>())); }
    }
    fn reactor_accept(reactor: &mut NetworkReactor, channel: ReactorListener, timeout_ms: U32) -> NetworkOperation {
        unsafe { return reactor_operation(dlm_net_reactor_submit(reactor_handle(reactor),channel.id,3,zero<Ptr<U8>>(),0,false,timeout_ms,zero<NetAddress>())); }
    }
    fn reactor_receive(reactor: &mut NetworkReactor, channel: ReactorUdp, capacity: USize, timeout_ms: U32) -> NetworkOperation {
        unsafe { return reactor_operation(dlm_net_reactor_submit(reactor_handle(reactor),channel.id,5,zero<Ptr<U8>>(),capacity,false,timeout_ms,zero<NetAddress>())); }
    }
    fn reactor_send(reactor: &mut NetworkReactor, channel: ReactorUdp, peer: SocketAddress, bytes: Slice<U8>, timeout_ms: U32) -> NetworkOperation {
        unsafe { let input: NetText=net_bytes(bytes);return reactor_operation(dlm_net_reactor_submit(reactor_handle(reactor),channel.id,6,input.data,input.count,false,timeout_ms,peer.value)); }
    }
    fn reactor_timer(reactor: &mut NetworkReactor, delay_ms: U32) -> NetworkOperation {
        unsafe { return reactor_operation(dlm_net_reactor_timer(reactor_handle(reactor),delay_ms,false)); }
    }
    fn reactor_event(reactor: &mut NetworkReactor, timeout_ms: U32) -> NetworkOperation {
        unsafe { return reactor_operation(dlm_net_reactor_timer(reactor_handle(reactor),timeout_ms,true)); }
    }
    fn reactor_signal(reactor: &mut NetworkReactor, operation: U64) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_reactor_signal(reactor_handle(reactor),operation),37); }
    }
    fn reactor_cancel(reactor: &mut NetworkReactor, operation: U64) -> NetworkTransfer {
        if operation==0 { return NetworkTransfer(0,NetworkError(4,0,26)); }
        unsafe { return socket_transfer(dlm_net_reactor_cancel(reactor_handle(reactor),operation),26); }
    }
    fn reactor_cancel_all(reactor: &mut NetworkReactor) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_reactor_cancel(reactor_handle(reactor),0),26); }
    }
    fn reactor_poll(reactor: &mut NetworkReactor, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_reactor_poll(reactor_handle(reactor),timeout_ms,*resource_get(cancel)),27); }
    }
    fn reactor_result(reactor: &NetworkReactor, operation: U64) -> ReactorCompletion {
        unsafe { return reactor_completion(dlm_net_reactor_result(reactor_handle(reactor),operation)); }
    }
    fn reactor_next(reactor: &NetworkReactor) -> ReactorCompletion {
        unsafe { return reactor_completion(dlm_net_reactor_result(reactor_handle(reactor),0)); }
    }
    fn reactor_copy(reactor: &NetworkReactor, operation: U64, offset: USize, bytes: &mut Buffer<U8>) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_reactor_copy(reactor_handle(reactor),operation,offset,net_output(bytes),len(bytes)),29); }
    }
    fn reactor_ack(reactor: &mut NetworkReactor, operation: U64) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_reactor_ack(reactor_handle(reactor),operation),30); }
    }
    fn reactor_tcp_close(reactor: &mut NetworkReactor, channel: ReactorTcp) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_reactor_channel_close(reactor_handle(reactor),channel.id),31); }
    }
    fn reactor_listener_close(reactor: &mut NetworkReactor, channel: ReactorListener) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_reactor_channel_close(reactor_handle(reactor),channel.id),31); }
    }
    fn reactor_udp_close(reactor: &mut NetworkReactor, channel: ReactorUdp) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_reactor_channel_close(reactor_handle(reactor),channel.id),31); }
    }
    fn reactor_release_tcp(reactor: &mut NetworkReactor, channel: ReactorTcp) -> TcpSocket {
        unsafe { return TcpSocket(resource(dlm_net_reactor_release(reactor_handle(reactor),channel.id,1)),32); }
    }
    fn reactor_release_listener(reactor: &mut NetworkReactor, channel: ReactorListener) -> TcpListener {
        unsafe { return TcpListener(resource(dlm_net_reactor_release(reactor_handle(reactor),channel.id,2))); }
    }
    fn reactor_release_udp(reactor: &mut NetworkReactor, channel: ReactorUdp) -> UdpSocket {
        unsafe { return UdpSocket(resource(dlm_net_reactor_release(reactor_handle(reactor),channel.id,3))); }
    }
    fn reactor_statistics(reactor: &NetworkReactor) -> ReactorStatistics {
        unsafe { let result: ReactorNativeStats=dlm_net_reactor_stats(reactor_handle(reactor));return ReactorStatistics(result.buffered_bytes,result.byte_limit,result.channels,result.pending,result.completed,result.channel_limit,result.operation_limit,result.backend,NetworkError(result.status,result.system,33)); }
    }
}
