module std.net_socket
import std.native_net
import std.net_error

native {
    trust "dlm.net.v1";
    // Distinct owning records. Passing a UdpSocket to tcp_read is a type error.
    // The inner v0.134 owner remains responsible for native error cleanup.
    record TcpSocket repr(C) { inner: Resource<NetOpened,net_dispose>; opened_by: U32; }
    record TcpListener repr(C) { inner: Resource<NetOpened,net_dispose>; }
    record UdpSocket repr(C) { inner: Resource<NetOpened,net_dispose>; }
    record TlsSocket repr(C) { inner: Resource<NetOpened,net_dispose>; }
    record SocketAddress repr(C, version=1) { value: NetAddress; }
    record SocketAddressResult repr(C) { address: SocketAddress; error: NetworkError; }
    record SocketDatagram repr(C) { transfer: NetworkTransfer; peer: SocketAddress; }
    record SocketOptions repr(C, version=1) { no_delay: U32; keep_alive: U32; send_buffer: U32; receive_buffer: U32; }
    record NativeSocketOptions repr(C, version=1) { options: SocketOptions; status: I32; system: I32; }
    record SocketOptionsResult repr(C) { options: SocketOptions; error: NetworkError; }
    record NetworkReady repr(C) { events: U32; error: NetworkError; }

    extern "C" fn dlm_net_connect_address(address: NetAddress, timeout_ms: U32) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_bind_address(address: NetAddress, kind: U32, backlog: U32, dual_stack: Bool) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_resolve_family(host: NetText, port: U16, family: U32, index: U32) -> NetResolved from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_peer(socket: OpaqueHandle) -> NetResolved from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_configure(socket: OpaqueHandle, options: SocketOptions) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_get_options(socket: OpaqueHandle) -> NativeSocketOptions from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_address_text(address: NetAddress, bytes: Ptr<U8>, capacity: USize, with_port: Bool) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);

    fn socket_transfer(result: NetIo, operation: U32) -> NetworkTransfer guarantees(NoAlloc,NoBlocking,NoIO) {
        return NetworkTransfer(result.count,NetworkError(result.status,result.system,operation));
    }
    fn socket_ready(result: NetIo) -> NetworkReady guarantees(NoAlloc,NoBlocking,NoIO) {
        return NetworkReady(cast<U32>(result.count),NetworkError(result.status,result.system,8));
    }
    fn socket_address_result(result: NetResolved, operation: U32) -> SocketAddressResult guarantees(NoAlloc,NoBlocking,NoIO) {
        return SocketAddressResult(SocketAddress(result.address),NetworkError(result.status,result.system,operation));
    }
    fn socket_address_v4(a: U8, b: U8, c: U8, d: U8, port: U16) -> SocketAddress guarantees(NoAlloc,NoBlocking,NoIO) {
        let mut address: NetAddress=zero<NetAddress>();address.family=4;address.port=cast<U32>(port);
        address.bytes[0]=a;address.bytes[1]=b;address.bytes[2]=c;address.bytes[3]=d;return SocketAddress(address);
    }
    fn socket_address_v6(bytes: Array<U8,16>, port: U16, scope: U32) -> SocketAddress guarantees(NoAlloc,NoBlocking,NoIO) {
        return SocketAddress(NetAddress(6,cast<U32>(port),scope,bytes));
    }
    fn socket_address_resolve(host: Slice<U8>, port: U16, family: U32, index: U32) -> SocketAddressResult {
        unsafe { return socket_address_result(dlm_net_resolve_family(net_bytes(host),port,family,index),12); }
    }
    fn socket_address_write(address: SocketAddress, bytes: &mut Buffer<U8>, with_port: Bool) -> NetworkTransfer {
        unsafe { return socket_transfer(dlm_net_address_text(address.value,net_output(bytes),len(bytes),with_port),17); }
    }
    fn socket_options(no_delay: Bool, keep_alive: Bool, send_buffer: U32, receive_buffer: U32) -> SocketOptions guarantees(NoAlloc,NoBlocking,NoIO) {
        let mut n: U32=0;let mut k: U32=0;if no_delay { n=1; }if keep_alive { k=1; }
        return SocketOptions(n,k,send_buffer,receive_buffer);
    }
    fn tcp_connect(host: Slice<U8>, port: U16, timeout_ms: U32) -> TcpSocket {
        return TcpSocket(socket_tcp_bytes(host,port,timeout_ms),1);
    }
    fn tcp_connect_address(address: SocketAddress, timeout_ms: U32) -> TcpSocket {
        unsafe { return TcpSocket(resource(dlm_net_connect_address(address.value,timeout_ms)),1); }
    }
    fn tcp_listen(address: SocketAddress, backlog: U32, dual_stack: Bool) -> TcpListener {
        unsafe { return TcpListener(resource(dlm_net_bind_address(address.value,2,backlog,dual_stack))); }
    }
    fn tcp_accept(listener: &mut TcpListener) -> TcpSocket { return TcpSocket(socket_accept(&mut listener.inner),3); }
    fn udp_bind(address: SocketAddress, dual_stack: Bool) -> UdpSocket {
        unsafe { return UdpSocket(resource(dlm_net_bind_address(address.value,3,0,dual_stack))); }
    }
    fn tls_connect(url: Slice<U8>, timeout_ms: U32, ca_file: Slice<U8>, pin: Slice<U8>) -> TlsSocket {
        return TlsSocket(socket_tls_bytes(url,timeout_ms,ca_file,pin));
    }
    fn tcp_error(socket: &TcpSocket) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) {
        return NetworkError(socket_status(&socket.inner),socket_system_error(&socket.inner),socket.opened_by);
    }
    fn listener_error(socket: &TcpListener) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) {
        return NetworkError(socket_status(&socket.inner),socket_system_error(&socket.inner),2);
    }
    fn udp_error(socket: &UdpSocket) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) {
        return NetworkError(socket_status(&socket.inner),socket_system_error(&socket.inner),4);
    }
    fn tls_error(socket: &TlsSocket) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) {
        return NetworkError(socket_status(&socket.inner),socket_system_error(&socket.inner),5);
    }
    fn tcp_close(socket: TcpSocket) -> Unit { resource_close(move socket.inner); }
    fn listener_close(socket: TcpListener) -> Unit { resource_close(move socket.inner); }
    fn udp_close(socket: UdpSocket) -> Unit { resource_close(move socket.inner); }
    fn tls_close(socket: TlsSocket) -> Unit { resource_close(move socket.inner); }
    fn tcp_read(socket: &mut TcpSocket, bytes: &mut Buffer<U8>) -> NetworkTransfer { return socket_transfer(socket_read(&mut socket.inner,bytes),6); }
    fn tcp_write(socket: &mut TcpSocket, bytes: Slice<U8>) -> NetworkTransfer { return socket_transfer(socket_write(&mut socket.inner,bytes),7); }
    fn tcp_read_exact(socket: &mut TcpSocket, bytes: &mut Buffer<U8>, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetworkTransfer {
        return socket_transfer(socket_read_exact(&mut socket.inner,bytes,timeout_ms,cancel),6);
    }
    fn tcp_write_all(socket: &mut TcpSocket, bytes: Slice<U8>, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetworkTransfer {
        return socket_transfer(socket_write_all(&mut socket.inner,bytes,timeout_ms,cancel),7);
    }
    fn tcp_wait(socket: &mut TcpSocket, events: U32, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetworkReady {
        return socket_ready(socket_wait(&mut socket.inner,events,timeout_ms,cancel));
    }
    fn listener_wait(socket: &mut TcpListener, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetworkReady {
        return socket_ready(socket_wait(&mut socket.inner,1,timeout_ms,cancel));
    }
    fn udp_wait(socket: &mut UdpSocket, events: U32, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetworkReady {
        return socket_ready(socket_wait(&mut socket.inner,events,timeout_ms,cancel));
    }
    fn tls_wait(socket: &mut TlsSocket, events: U32, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetworkReady {
        return socket_ready(socket_wait(&mut socket.inner,events,timeout_ms,cancel));
    }
    fn tcp_shutdown_write(socket: &mut TcpSocket) -> NetworkError {
        let result: NetIo=socket_shutdown_write(&mut socket.inner);return NetworkError(result.status,result.system,9);
    }
    fn tcp_local(socket: &TcpSocket) -> SocketAddressResult { return socket_address_result(socket_local(&socket.inner),10); }
    fn listener_local(socket: &TcpListener) -> SocketAddressResult { return socket_address_result(socket_local(&socket.inner),10); }
    fn udp_local(socket: &UdpSocket) -> SocketAddressResult { return socket_address_result(socket_local(&socket.inner),10); }
    fn tcp_peer(socket: &TcpSocket) -> SocketAddressResult {
        let state: &NetOpened=resource_get(&socket.inner);unsafe { return socket_address_result(dlm_net_peer(state.handle),11); }
    }
    fn tls_peer(socket: &TlsSocket) -> SocketAddressResult {
        let state: &NetOpened=resource_get(&socket.inner);unsafe { return socket_address_result(dlm_net_peer(state.handle),11); }
    }
    fn tcp_configure(socket: &mut TcpSocket, options: SocketOptions) -> NetworkError {
        let state: &NetOpened=resource_get(&socket.inner);unsafe { let result: NetIo=dlm_net_configure(state.handle,options);return NetworkError(result.status,result.system,13); }
    }
    fn udp_configure(socket: &mut UdpSocket, send_buffer: U32, receive_buffer: U32) -> NetworkError {
        let state: &NetOpened=resource_get(&socket.inner);unsafe { let result: NetIo=dlm_net_configure(state.handle,SocketOptions(0,0,send_buffer,receive_buffer));return NetworkError(result.status,result.system,13); }
    }
    fn tcp_options(socket: &TcpSocket) -> SocketOptionsResult {
        let state: &NetOpened=resource_get(&socket.inner);unsafe { let result: NativeSocketOptions=dlm_net_get_options(state.handle);return SocketOptionsResult(result.options,NetworkError(result.status,result.system,14)); }
    }
    fn udp_send(socket: &mut UdpSocket, peer: SocketAddress, bytes: Slice<U8>) -> NetworkTransfer {
        return socket_transfer(socket_send_datagram(&mut socket.inner,peer.value,bytes),15);
    }
    fn udp_receive(socket: &mut UdpSocket, bytes: &mut Buffer<U8>) -> SocketDatagram {
        let result: NetDatagram=socket_read_datagram(&mut socket.inner,bytes);return SocketDatagram(socket_transfer(result.io,16),SocketAddress(result.peer));
    }
    fn tls_read(socket: &mut TlsSocket, bytes: &mut Buffer<U8>) -> NetworkTransfer { return socket_transfer(socket_read(&mut socket.inner,bytes),6); }
    fn tls_write(socket: &mut TlsSocket, bytes: Slice<U8>) -> NetworkTransfer { return socket_transfer(socket_write(&mut socket.inner,bytes),7); }
    fn tls_read_exact(socket: &mut TlsSocket, bytes: &mut Buffer<U8>, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetworkTransfer {
        return socket_transfer(socket_read_exact(&mut socket.inner,bytes,timeout_ms,cancel),6);
    }
    fn tls_write_all(socket: &mut TlsSocket, bytes: Slice<U8>, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetworkTransfer {
        return socket_transfer(socket_write_all(&mut socket.inner,bytes,timeout_ms,cancel),7);
    }
}
