module std.net_transport
import std.net_transport_policy
import std.net_quic

native {
    trust "dlm.net.v1";
    record TransportSession repr(C) { inner: Resource<NetOpened,transport_dispose>; }
    record TransportTarget repr(C) { local: SocketAddress; peer: SocketAddress; identity: Utf8; ca_file: Utf8; pin: Utf8; }
    record TransportNativeTarget repr(C, version=1) { local: NetAddress; peer: NetAddress; identity: NetText; ca: NetText; pin: NetText; alpn: NetText; }
    record TransportNativeMessage repr(C, version=1) { id: U64; channel: U32; count: USize; status: I32; system: I32; }
    record TransportNativeDelivery repr(C, version=1) { id: U64; channel: U32; copied: USize; required: USize; status: I32; system: I32; }
    record TransportNativeStatistics repr(C, version=1) { used: USize; limit: USize; adapter_limit: USize; backend_limit: USize; profile: U32; properties: U32; state: U32; send_queued: U32; receive_queued: U32; channels: U32; status: I32; system: I32; }
    record TransportMessage repr(C) { id: U64; channel: U32; count: USize; error: NetworkError; }
    record TransportDelivery repr(C) { id: U64; channel: U32; copied: USize; required: USize; error: NetworkError; }
    record TransportStatistics repr(C) { used: USize; limit: USize; adapter_limit: USize; backend_limit: USize; profile: U32; properties: U32; state: U32; send_queued: U32; receive_queued: U32; channels: U32; error: NetworkError; }

    extern "C" fn dlm_net_transport_close(session: OpaqueHandle) -> Unit from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_transport_available() -> U32 from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_transport_open(profile: U32, requirements: TransportRequirements, bounds: TransportBounds, target: TransportNativeTarget, timeout_ms: U32, cancel: OpaqueHandle) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_transport_adopt(opened: NetOpened, profile: U32, requirements: TransportRequirements, bounds: TransportBounds, peer: NetAddress, connection: U64, initiator: Bool) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_transport_channel(session: OpaqueHandle) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_transport_send(session: OpaqueHandle, channel: U32, bytes: Ptr<U8>, count: USize) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_transport_poll(session: OpaqueHandle, timeout_ms: U32, cancel: OpaqueHandle) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_transport_peek(session: OpaqueHandle) -> TransportNativeMessage from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_transport_copy(session: OpaqueHandle, message: U64, offset: USize, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_transport_ack(session: OpaqueHandle, message: U64) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_transport_receive(session: OpaqueHandle, output: Ptr<U8>, capacity: USize) -> TransportNativeDelivery from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_transport_stats(session: OpaqueHandle) -> TransportNativeStatistics from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_transport_local(session: OpaqueHandle) -> NetResolved from "dlm.net.v1" guarantees(NoUnwind);

    fn transport_dispose(state: &mut NetOpened) -> Unit { unsafe { dlm_net_transport_close(state.handle); } }
    fn transport_handle(session: &TransportSession) -> OpaqueHandle guarantees(NoAlloc,NoBlocking,NoIO) { let state: &NetOpened=resource_get(&session.inner);return state.handle; }
    fn transport_error(session: &TransportSession) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { let state: &NetOpened=resource_get(&session.inner);return NetworkError(state.status,state.system,90); }
    fn transport_close(session: TransportSession) -> Unit { resource_close(move session.inner); }
    fn transport_available() -> U32 { unsafe { return dlm_net_transport_available(); } }
    fn transport_target(peer: SocketAddress, identity: Utf8, ca_file: Utf8, pin: Utf8) -> TransportTarget guarantees(NoAlloc,NoBlocking,NoIO) {
        return TransportTarget(SocketAddress(zero<NetAddress>()),peer,identity,ca_file,pin);
    }
    fn transport_alpn(profile: U32) -> Utf8 guarantees(NoAlloc,NoBlocking,NoIO) {
        if profile==4 { return "dlm-transport/1/ordered"; }if profile==5 { return "dlm-transport/1/unordered"; }if profile==6 { return "dlm-transport/1/datagram"; }return "";
    }
    fn transport_quic_config(bounds: TransportBounds) -> QuicConfig guarantees(NoAlloc,NoBlocking,NoIO) {
        if !transport_bounds_valid(bounds) { return QuicConfig(0,0,0,0,0,0,0,0); }
        let mut window: USize=cast<USize>(bounds.max_message)+16;if window<1024 { window=1024; }
        return QuicConfig(1,bounds.channels*4,bounds.queued_messages*2+16,30000,10000,2,bounds.memory/2,window);
    }
    // Availability must also be intersected with the application's remote offer.
    fn transport_open(choice: TransportChoice, requirements: TransportRequirements, bounds: TransportBounds, target: TransportTarget, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> TransportSession {
        if choice.status!=I32(0) { return TransportSession(resource(NetOpened(zero<OpaqueHandle>(),choice.status,0))); }
        unsafe {
            let native_target: TransportNativeTarget=TransportNativeTarget(target.local.value,target.peer.value,net_text(target.identity),net_text(target.ca_file),net_text(target.pin),net_text(transport_alpn(choice.profile)));
            return TransportSession(resource(dlm_net_transport_open(choice.profile,requirements,bounds,native_target,timeout_ms,*resource_get(cancel))));
        }
    }
    fn transport_adopt_tcp(socket: TcpSocket, requirements: TransportRequirements, bounds: TransportBounds) -> TransportSession {
        unsafe { return TransportSession(resource(dlm_net_transport_adopt(resource_into_inner(move socket.inner),1,requirements,bounds,zero<NetAddress>(),0,true))); }
    }
    fn transport_adopt_tls(socket: TlsSocket, requirements: TransportRequirements, bounds: TransportBounds) -> TransportSession {
        unsafe { return TransportSession(resource(dlm_net_transport_adopt(resource_into_inner(move socket.inner),2,requirements,bounds,zero<NetAddress>(),0,true))); }
    }
    fn transport_adopt_udp(socket: UdpSocket, peer: SocketAddress, requirements: TransportRequirements, bounds: TransportBounds) -> TransportSession {
        unsafe { return TransportSession(resource(dlm_net_transport_adopt(resource_into_inner(move socket.inner),3,requirements,bounds,peer.value,0,true))); }
    }
    // The established endpoint must contain exactly one connection and use the
    // matching profile ALPN. Ownership transfers even when validation fails.
    fn transport_adopt_quic(endpoint: QuicEndpoint, connection: QuicConnection, initiator: Bool, profile: U32, requirements: TransportRequirements, bounds: TransportBounds) -> TransportSession {
        if profile<4 || profile>6 { quic_close(move endpoint);return TransportSession(resource(NetOpened(zero<OpaqueHandle>(),4,0))); }
        unsafe { return TransportSession(resource(dlm_net_transport_adopt(resource_into_inner(move endpoint.inner),profile,requirements,bounds,zero<NetAddress>(),connection.id,initiator))); }
    }
    fn transport_local(session: &TransportSession) -> SocketAddressResult { unsafe { return socket_address_result(dlm_net_transport_local(transport_handle(session)),91); } }
    fn transport_open_channel(session: &mut TransportSession) -> NetworkTransfer { unsafe { return socket_transfer(dlm_net_transport_channel(transport_handle(session)),92); } }
    fn transport_send(session: &mut TransportSession, channel: U32, bytes: Slice<U8>) -> NetworkTransfer {
        unsafe { let data: NetText=net_bytes(bytes);return socket_transfer(dlm_net_transport_send(transport_handle(session),channel,data.data,data.count),93); }
    }
    fn transport_poll(session: &mut TransportSession, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_transport_poll(transport_handle(session),timeout_ms,*resource_get(cancel)),94); }
    }
    fn transport_peek(session: &TransportSession) -> TransportMessage {
        unsafe { let r: TransportNativeMessage=dlm_net_transport_peek(transport_handle(session));return TransportMessage(r.id,r.channel,r.count,NetworkError(r.status,r.system,95)); }
    }
    fn transport_copy(session: &TransportSession, message: U64, offset: USize, output: &mut Buffer<U8>) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_transport_copy(transport_handle(session),message,offset,net_output(output),len(output)),96); }
    }
    fn transport_ack(session: &mut TransportSession, message: U64) -> NetworkTransfer { unsafe { return socket_transfer(dlm_net_transport_ack(transport_handle(session),message),97); } }
    fn transport_receive(session: &mut TransportSession, output: &mut Buffer<U8>) -> TransportDelivery {
        unsafe { let r: TransportNativeDelivery=dlm_net_transport_receive(transport_handle(session),net_output(output),len(output));return TransportDelivery(r.id,r.channel,r.copied,r.required,NetworkError(r.status,r.system,98)); }
    }
    fn transport_statistics(session: &TransportSession) -> TransportStatistics {
        unsafe { let r: TransportNativeStatistics=dlm_net_transport_stats(transport_handle(session));return TransportStatistics(r.used,r.limit,r.adapter_limit,r.backend_limit,r.profile,r.properties,r.state,r.send_queued,r.receive_queued,r.channels,NetworkError(r.status,r.system,99)); }
    }
}
