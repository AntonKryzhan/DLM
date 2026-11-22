module std.net_async
import std.net_reactor

native {
    trust "dlm.net.v1";
    record NetworkScope repr(C) { inner: Resource<NetOpened,scope_dispose>; }
    record EventWaker repr(C) { inner: Resource<NetOpened,scope_waker_dispose>; }
    record OperationCanceller repr(C) { inner: Resource<NetOpened,scope_waker_dispose>; }
    record ScopeCanceller repr(C) { inner: Resource<NetOpened,scope_waker_dispose>; }
    record ScopeNativeStatistics repr(C, version=1) { reactor: ReactorNativeStats; queued: U32; queue_limit: U32; kernel_pending: U32; }
    record ScopeStatistics repr(C) { reactor: ReactorStatistics; queued: U32; queue_limit: U32; kernel_pending: U32; }

    extern "C" fn dlm_net_async_new(channels: U32, operations: U32, byte_limit: USize, wake_limit: U32) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_async_close(scope: OpaqueHandle) -> Unit from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_async_adopt(scope: OpaqueHandle, socket: NetOpened, kind: U32) -> ReactorNativeRequest from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_async_connect(scope: OpaqueHandle, address: NetAddress, timeout_ms: U32) -> ReactorNativeRequest from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_async_waker(scope: OpaqueHandle, operation: U64, action: U32) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_async_waker_close(waker: OpaqueHandle) -> Unit from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_async_wake(waker: OpaqueHandle) -> NetIo from "dlm.net.v1" guarantees(NoUnwind,ThreadSafe);
    extern "C" fn dlm_net_async_join(scope: OpaqueHandle, timeout_ms: U32, cancel: OpaqueHandle) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_async_stats(scope: OpaqueHandle) -> ScopeNativeStatistics from "dlm.net.v1" guarantees(NoUnwind);

    fn scope_dispose(state: &mut NetOpened) -> Unit { unsafe { dlm_net_async_close(state.handle); } }
    fn scope_waker_dispose(state: &mut NetOpened) -> Unit { unsafe { dlm_net_async_waker_close(state.handle); } }
    fn scope_handle(scope: &NetworkScope) -> OpaqueHandle guarantees(NoAlloc,NoBlocking,NoIO) {
        let state: &NetOpened=resource_get(&scope.inner);return state.handle;
    }
    fn scope_open(channels: U32, operations: U32, byte_limit: USize, wake_limit: U32) -> NetworkScope {
        unsafe { return NetworkScope(resource(dlm_net_async_new(channels,operations,byte_limit,wake_limit))); }
    }
    fn scope_error(scope: &NetworkScope) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) {
        let state: &NetOpened=resource_get(&scope.inner);return NetworkError(state.status,state.system,40);
    }
    fn scope_close(scope: NetworkScope) -> Unit { resource_close(move scope.inner); }
    fn scope_adopt_tcp(scope: &mut NetworkScope, socket: TcpSocket) -> ReactorTcpResult {
        unsafe { let result: ReactorNativeRequest=dlm_net_async_adopt(scope_handle(scope),resource_into_inner(move socket.inner),1);return ReactorTcpResult(ReactorTcp(result.channel),NetworkError(result.status,result.system,41)); }
    }
    fn scope_adopt_listener(scope: &mut NetworkScope, socket: TcpListener) -> ReactorListenerResult {
        unsafe { let result: ReactorNativeRequest=dlm_net_async_adopt(scope_handle(scope),resource_into_inner(move socket.inner),2);return ReactorListenerResult(ReactorListener(result.channel),NetworkError(result.status,result.system,41)); }
    }
    fn scope_adopt_udp(scope: &mut NetworkScope, socket: UdpSocket) -> ReactorUdpResult {
        unsafe { let result: ReactorNativeRequest=dlm_net_async_adopt(scope_handle(scope),resource_into_inner(move socket.inner),3);return ReactorUdpResult(ReactorUdp(result.channel),NetworkError(result.status,result.system,41)); }
    }
    fn scope_connect(scope: &mut NetworkScope, address: SocketAddress, timeout_ms: U32) -> ReactorConnect {
        unsafe { let result: ReactorNativeRequest=dlm_net_async_connect(scope_handle(scope),address.value,timeout_ms);return ReactorConnect(ReactorTcp(result.channel),reactor_operation(result)); }
    }
    fn scope_read(scope: &mut NetworkScope, channel: ReactorTcp, count: USize, exact: Bool, timeout_ms: U32) -> NetworkOperation {
        unsafe { return reactor_operation(dlm_net_reactor_submit(scope_handle(scope),channel.id,1,zero<Ptr<U8>>(),count,exact,timeout_ms,zero<NetAddress>())); }
    }
    fn scope_write(scope: &mut NetworkScope, channel: ReactorTcp, bytes: Slice<U8>, timeout_ms: U32) -> NetworkOperation {
        unsafe { let input: NetText=net_bytes(bytes);return reactor_operation(dlm_net_reactor_submit(scope_handle(scope),channel.id,2,input.data,input.count,true,timeout_ms,zero<NetAddress>())); }
    }
    fn scope_accept(scope: &mut NetworkScope, channel: ReactorListener, timeout_ms: U32) -> NetworkOperation {
        unsafe { return reactor_operation(dlm_net_reactor_submit(scope_handle(scope),channel.id,3,zero<Ptr<U8>>(),0,false,timeout_ms,zero<NetAddress>())); }
    }
    fn scope_receive(scope: &mut NetworkScope, channel: ReactorUdp, capacity: USize, timeout_ms: U32) -> NetworkOperation {
        unsafe { return reactor_operation(dlm_net_reactor_submit(scope_handle(scope),channel.id,5,zero<Ptr<U8>>(),capacity,false,timeout_ms,zero<NetAddress>())); }
    }
    fn scope_send(scope: &mut NetworkScope, channel: ReactorUdp, peer: SocketAddress, bytes: Slice<U8>, timeout_ms: U32) -> NetworkOperation {
        unsafe { let input: NetText=net_bytes(bytes);return reactor_operation(dlm_net_reactor_submit(scope_handle(scope),channel.id,6,input.data,input.count,false,timeout_ms,peer.value)); }
    }
    fn scope_timer(scope: &mut NetworkScope, delay_ms: U32) -> NetworkOperation {
        unsafe { return reactor_operation(dlm_net_reactor_timer(scope_handle(scope),delay_ms,false)); }
    }
    fn scope_event(scope: &mut NetworkScope, timeout_ms: U32) -> NetworkOperation {
        unsafe { return reactor_operation(dlm_net_reactor_timer(scope_handle(scope),timeout_ms,true)); }
    }
    fn scope_poll(scope: &mut NetworkScope, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_reactor_poll(scope_handle(scope),timeout_ms,*resource_get(cancel)),42); }
    }
    // Join advances pending work even when earlier results have not been acked.
    fn scope_join(scope: &mut NetworkScope, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_async_join(scope_handle(scope),timeout_ms,*resource_get(cancel)),43); }
    }
    fn scope_result(scope: &NetworkScope, operation: U64) -> ReactorCompletion {
        unsafe { return reactor_completion(dlm_net_reactor_result(scope_handle(scope),operation)); }
    }
    fn scope_next(scope: &NetworkScope) -> ReactorCompletion {
        unsafe { return reactor_completion(dlm_net_reactor_result(scope_handle(scope),0)); }
    }
    fn scope_copy(scope: &NetworkScope, operation: U64, offset: USize, bytes: &mut Buffer<U8>) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_reactor_copy(scope_handle(scope),operation,offset,net_output(bytes),len(bytes)),44); }
    }
    fn scope_ack(scope: &mut NetworkScope, operation: U64) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_reactor_ack(scope_handle(scope),operation),45); }
    }
    fn scope_cancel(scope: &mut NetworkScope, operation: U64) -> NetworkTransfer {
        if operation==0 { return NetworkTransfer(0,NetworkError(4,0,46)); }
        unsafe { return socket_transfer(dlm_net_reactor_cancel(scope_handle(scope),operation),46); }
    }
    fn scope_cancel_all(scope: &mut NetworkScope) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_reactor_cancel(scope_handle(scope),0),46); }
    }
    fn scope_tcp_close(scope: &mut NetworkScope, channel: ReactorTcp) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_reactor_channel_close(scope_handle(scope),channel.id),47); }
    }
    fn scope_listener_close(scope: &mut NetworkScope, channel: ReactorListener) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_reactor_channel_close(scope_handle(scope),channel.id),47); }
    }
    fn scope_udp_close(scope: &mut NetworkScope, channel: ReactorUdp) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_reactor_channel_close(scope_handle(scope),channel.id),47); }
    }
    fn scope_event_waker(scope: &NetworkScope, operation: U64) -> EventWaker {
        unsafe { return EventWaker(resource(dlm_net_async_waker(scope_handle(scope),operation,1))); }
    }
    fn scope_operation_canceller(scope: &NetworkScope, operation: U64) -> OperationCanceller {
        unsafe { return OperationCanceller(resource(dlm_net_async_waker(scope_handle(scope),operation,2))); }
    }
    fn scope_canceller(scope: &NetworkScope) -> ScopeCanceller {
        unsafe { return ScopeCanceller(resource(dlm_net_async_waker(scope_handle(scope),0,3))); }
    }
    fn event_waker_error(waker: &EventWaker) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) {
        let state: &NetOpened=resource_get(&waker.inner);return NetworkError(state.status,state.system,48);
    }
    fn operation_canceller_error(waker: &OperationCanceller) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) {
        let state: &NetOpened=resource_get(&waker.inner);return NetworkError(state.status,state.system,48);
    }
    fn scope_canceller_error(waker: &ScopeCanceller) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) {
        let state: &NetOpened=resource_get(&waker.inner);return NetworkError(state.status,state.system,48);
    }
    fn event_wake(waker: &EventWaker) -> NetworkTransfer guarantees(ThreadSafe) {
        unsafe { let state: &NetOpened=resource_get(&waker.inner);return socket_transfer(dlm_net_async_wake(state.handle),49); }
    }
    fn operation_cancel(waker: &OperationCanceller) -> NetworkTransfer guarantees(ThreadSafe) {
        unsafe { let state: &NetOpened=resource_get(&waker.inner);return socket_transfer(dlm_net_async_wake(state.handle),49); }
    }
    fn scope_request_cancel(waker: &ScopeCanceller) -> NetworkTransfer guarantees(ThreadSafe) {
        unsafe { let state: &NetOpened=resource_get(&waker.inner);return socket_transfer(dlm_net_async_wake(state.handle),49); }
    }
    fn event_waker_close(waker: EventWaker) -> Unit { resource_close(move waker.inner); }
    fn operation_canceller_close(waker: OperationCanceller) -> Unit { resource_close(move waker.inner); }
    fn scope_canceller_close(waker: ScopeCanceller) -> Unit { resource_close(move waker.inner); }
    fn scope_statistics(scope: &NetworkScope) -> ScopeStatistics {
        unsafe {
            let result: ScopeNativeStatistics=dlm_net_async_stats(scope_handle(scope));
            let raw: ReactorNativeStats=result.reactor;
            let reactor: ReactorStatistics=ReactorStatistics(raw.buffered_bytes,raw.byte_limit,raw.channels,raw.pending,raw.completed,raw.channel_limit,raw.operation_limit,raw.backend,NetworkError(raw.status,raw.system,50));
            return ScopeStatistics(reactor,result.queued,result.queue_limit,result.kernel_pending);
        }
    }
}
