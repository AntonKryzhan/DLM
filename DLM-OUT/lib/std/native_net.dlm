module std.native_net

native {
    trust "dlm.net.v1";

    // Stable provider ABI. Status: 0 OK, 1 would-block, 2 EOF, 3 timeout,
    // 4 invalid, 5 OS/provider error, 6 unavailable, 7 limit, 8 TLS error,
    // 9 cancelled, 10 truncated datagram, 11 protocol mismatch.
    record NetText repr(C, version=1) { data: Ptr<U8>; count: USize; }
    record NetIo repr(C, version=1) { count: USize; status: I32; system: I32; }
    record NetOpened repr(C, version=1) { handle: OpaqueHandle; status: I32; system: I32; }
    record NetAddress repr(C, version=1) { family: U32; port: U32; scope: U32; bytes: Array<U8,16>; }
    record NetResolved repr(C, version=1) { address: NetAddress; status: I32; system: I32; }
    record NetDatagram repr(C, version=1) { io: NetIo; peer: NetAddress; }
    record NetWebSocketIo repr(C, version=1) { io: NetIo; offset: U64; remaining: U64; flags: U32; }
    record NetPoll repr(C, version=1) { socket: OpaqueHandle; events: U32; ready: U32; status: I32; system: I32; }

    extern "C" fn dlm_net_capabilities() -> U32 from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_curl_version() -> U32 from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_provider_shutdown() -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_clock_ms() -> U64 from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_tcp(host: NetText, port: U16, timeout_ms: U32) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_listen(host: NetText, port: U16, backlog: U32) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_udp(host: NetText, port: U16) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_accept(socket: OpaqueHandle) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_close(socket: OpaqueHandle) -> Unit from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_local(socket: OpaqueHandle) -> NetResolved from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_resolve(host: NetText, port: U16, index: U32) -> NetResolved from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_nodelay(socket: OpaqueHandle, enabled: Bool) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_send(socket: OpaqueHandle, bytes: Ptr<U8>, count: USize) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_recv(socket: OpaqueHandle, bytes: Ptr<U8>, capacity: USize) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_transfer(socket: OpaqueHandle, bytes: Ptr<U8>, count: USize, timeout_ms: U32, cancel: OpaqueHandle, sending: Bool) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_shutdown(socket: OpaqueHandle) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_udp_send(socket: OpaqueHandle, peer: NetAddress, bytes: Ptr<U8>, count: USize) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_udp_recv(socket: OpaqueHandle, bytes: Ptr<U8>, capacity: USize) -> NetDatagram from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_wait(socket: OpaqueHandle, events: U32, timeout_ms: U32, cancel: OpaqueHandle) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_poll(items: Ptr<NetPoll>, count: USize, timeout_ms: U32, cancel: OpaqueHandle) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_cancel_new() -> OpaqueHandle from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_cancel_close(cancel: OpaqueHandle) -> Unit from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_cancel_signal(cancel: OpaqueHandle) -> Unit from "dlm.net.v1" guarantees(NoUnwind,ThreadSafe);
    extern "C" fn dlm_net_cancel_reset(cancel: OpaqueHandle) -> Unit from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_tls(url: NetText, timeout_ms: U32, ca_file: NetText, pin: NetText) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_http(url: NetText, timeout_ms: U32, maximum: USize, ca_file: NetText, pin: NetText) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_http_version(request: OpaqueHandle, version: U32) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_http_header(request: OpaqueHandle, header: NetText) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_http_body(request: OpaqueHandle, bytes: Ptr<U8>, count: USize) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_http_method(request: OpaqueHandle, method: NetText) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_http_ech(request: OpaqueHandle, config: NetText) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_http_perform(request: OpaqueHandle, cancel: OpaqueHandle) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_http_info(request: OpaqueHandle, field: U32) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_http_read(request: OpaqueHandle, headers: Bool, offset: USize, bytes: Ptr<U8>, capacity: USize) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_websocket(url: NetText, timeout_ms: U32, ca_file: NetText, pin: NetText) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_ws_start(socket: OpaqueHandle) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_ws_send(socket: OpaqueHandle, bytes: Ptr<U8>, count: USize, flags: U32, timeout_ms: U32, cancel: OpaqueHandle) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_ws_recv(socket: OpaqueHandle, bytes: Ptr<U8>, capacity: USize) -> NetWebSocketIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_random(bytes: Ptr<U8>, count: USize) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);

    unsafe fn net_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { dlm_net_close(state.handle); }
    unsafe fn net_cancel_dispose(state: &mut OpaqueHandle) -> Unit guarantees(NoUnwind) { dlm_net_cancel_close(*state); }
    // These raw pointers never escape a synchronous provider call in wrappers.
    unsafe fn net_input(bytes: Slice<U8>) -> Ptr<U8> {
        if len(bytes) == 0 { return null<U8>(); }
        return as_raw(borrowed_ptr(&bytes[0]));
    }
    unsafe fn net_output(bytes: &mut Buffer<U8>) -> Ptr<U8> {
        if len(bytes) == 0 { return null<U8>(); }
        return as_raw(borrowed_ptr(&mut bytes[0]));
    }
    unsafe fn net_text(text: Utf8) -> NetText {
        let bytes: Slice<U8>=utf8_bytes(text);return NetText(net_input(bytes),len(bytes));
    }
    unsafe fn net_bytes(bytes: Slice<U8>) -> NetText { return NetText(net_input(bytes),len(bytes)); }
    // Buffer/slice variants accept dynamically constructed names and URLs;
    // Utf8 variants below are convenient for literal-backed native strings.
    fn network_resolve_bytes(host: Slice<U8>, port: U16, index: U32) -> NetResolved {
        unsafe { return dlm_net_resolve(net_bytes(host),port,index); }
    }
    fn socket_tcp_bytes(host: Slice<U8>, port: U16, timeout_ms: U32) -> Resource<NetOpened,net_dispose> {
        unsafe { return resource(dlm_net_tcp(net_bytes(host),port,timeout_ms)); }
    }
    fn socket_listen_bytes(host: Slice<U8>, port: U16, backlog: U32) -> Resource<NetOpened,net_dispose> {
        unsafe { return resource(dlm_net_listen(net_bytes(host),port,backlog)); }
    }
    fn socket_udp_bytes(host: Slice<U8>, port: U16) -> Resource<NetOpened,net_dispose> {
        unsafe { return resource(dlm_net_udp(net_bytes(host),port)); }
    }
    fn socket_tls_bytes(url: Slice<U8>, timeout_ms: U32, ca_file: Slice<U8>, pin: Slice<U8>) -> Resource<NetOpened,net_dispose> {
        unsafe { return resource(dlm_net_tls(net_bytes(url),timeout_ms,net_bytes(ca_file),net_bytes(pin))); }
    }
    fn http_client_bytes(url: Slice<U8>, timeout_ms: U32, maximum: USize, ca_file: Slice<U8>, pin: Slice<U8>) -> Resource<NetOpened,net_dispose> {
        unsafe { return resource(dlm_net_http(net_bytes(url),timeout_ms,maximum,net_bytes(ca_file),net_bytes(pin))); }
    }
    fn http_header_bytes(request: &mut Resource<NetOpened,net_dispose>, header: Slice<U8>) -> NetIo {
        let state: &NetOpened=resource_get(request);unsafe { return dlm_net_http_header(state.handle,net_bytes(header)); }
    }
    fn http_method_bytes(request: &mut Resource<NetOpened,net_dispose>, method: Slice<U8>) -> NetIo {
        let state: &NetOpened=resource_get(request);unsafe { return dlm_net_http_method(state.handle,net_bytes(method)); }
    }
    fn http_ech_bytes(request: &mut Resource<NetOpened,net_dispose>, config: Slice<U8>) -> NetIo {
        let state: &NetOpened=resource_get(request);unsafe { return dlm_net_http_ech(state.handle,net_bytes(config)); }
    }
    fn websocket_client_bytes(url: Slice<U8>, timeout_ms: U32, ca_file: Slice<U8>, pin: Slice<U8>) -> Resource<NetOpened,net_dispose> {
        unsafe { return resource(dlm_net_websocket(net_bytes(url),timeout_ms,net_bytes(ca_file),net_bytes(pin))); }
    }
    fn network_capabilities() -> U32 { unsafe { return dlm_net_capabilities(); } }
    fn network_provider_version() -> U32 { unsafe { return dlm_net_curl_version(); } }
    // Stop all provider calls/threads and close every owner before unloading.
    unsafe fn network_provider_shutdown() -> NetIo { return dlm_net_provider_shutdown(); }
    fn network_clock_ms() -> U64 { unsafe { return dlm_net_clock_ms(); } }
    fn network_resolve(host: Utf8, port: U16, index: U32) -> NetResolved { unsafe { return dlm_net_resolve(net_text(host),port,index); } }
    fn socket_tcp(host: Utf8, port: U16, timeout_ms: U32) -> Resource<NetOpened,net_dispose> {
        unsafe { return resource(dlm_net_tcp(net_text(host),port,timeout_ms)); }
    }
    fn socket_listen(host: Utf8, port: U16, backlog: U32) -> Resource<NetOpened,net_dispose> {
        unsafe { return resource(dlm_net_listen(net_text(host),port,backlog)); }
    }
    fn socket_udp(host: Utf8, port: U16) -> Resource<NetOpened,net_dispose> {
        unsafe { return resource(dlm_net_udp(net_text(host),port)); }
    }
    fn socket_accept(listener: &mut Resource<NetOpened,net_dispose>) -> Resource<NetOpened,net_dispose> {
        let state: &NetOpened=resource_get(listener);
        unsafe { return resource(dlm_net_accept(state.handle)); }
    }
    fn socket_status(socket: &Resource<NetOpened,net_dispose>) -> I32 {
        let state: &NetOpened=resource_get(socket);return state.status;
    }
    fn socket_system_error(socket: &Resource<NetOpened,net_dispose>) -> I32 {
        let state: &NetOpened=resource_get(socket);return state.system;
    }
    fn socket_local(socket: &Resource<NetOpened,net_dispose>) -> NetResolved {
        let state: &NetOpened=resource_get(socket);unsafe { return dlm_net_local(state.handle); }
    }
    fn socket_nodelay(socket: &mut Resource<NetOpened,net_dispose>, enabled: Bool) -> NetIo {
        let state: &NetOpened=resource_get(socket);unsafe { return dlm_net_nodelay(state.handle,enabled); }
    }
    fn socket_write(socket: &mut Resource<NetOpened,net_dispose>, bytes: Slice<U8>) -> NetIo {
        let state: &NetOpened=resource_get(socket);unsafe { return dlm_net_send(state.handle,net_input(bytes),len(bytes)); }
    }
    fn socket_read(socket: &mut Resource<NetOpened,net_dispose>, bytes: &mut Buffer<U8>) -> NetIo {
        let state: &NetOpened=resource_get(socket);unsafe { return dlm_net_recv(state.handle,net_output(bytes),len(bytes)); }
    }
    fn socket_write_all(socket: &mut Resource<NetOpened,net_dispose>, bytes: Slice<U8>, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetIo {
        let state: &NetOpened=resource_get(socket);let token: &OpaqueHandle=resource_get(cancel);
        unsafe { return dlm_net_transfer(state.handle,net_input(bytes),len(bytes),timeout_ms,*token,true); }
    }
    fn socket_read_exact(socket: &mut Resource<NetOpened,net_dispose>, bytes: &mut Buffer<U8>, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetIo {
        let state: &NetOpened=resource_get(socket);let token: &OpaqueHandle=resource_get(cancel);
        unsafe { return dlm_net_transfer(state.handle,net_output(bytes),len(bytes),timeout_ms,*token,false); }
    }
    fn socket_shutdown_write(socket: &mut Resource<NetOpened,net_dispose>) -> NetIo {
        let state: &NetOpened=resource_get(socket);unsafe { return dlm_net_shutdown(state.handle); }
    }
    fn socket_send_datagram(socket: &mut Resource<NetOpened,net_dispose>, peer: NetAddress, bytes: Slice<U8>) -> NetIo {
        let state: &NetOpened=resource_get(socket);unsafe { return dlm_net_udp_send(state.handle,peer,net_input(bytes),len(bytes)); }
    }
    fn socket_read_datagram(socket: &mut Resource<NetOpened,net_dispose>, bytes: &mut Buffer<U8>) -> NetDatagram {
        let state: &NetOpened=resource_get(socket);unsafe { return dlm_net_udp_recv(state.handle,net_output(bytes),len(bytes)); }
    }
    fn socket_wait(socket: &mut Resource<NetOpened,net_dispose>, events: U32, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetIo {
        let state: &NetOpened=resource_get(socket);let token: &OpaqueHandle=resource_get(cancel);
        unsafe { return dlm_net_wait(state.handle,events,timeout_ms,*token); }
    }
    // Poll entries carry foreign handles. Keep every socket owner alive and do
    // not close/reenter it until socket_poll returns. This remains explicit unsafe.
    unsafe fn socket_poll_entry(socket: &Resource<NetOpened,net_dispose>, events: U32) -> NetPoll {
        let state: &NetOpened=resource_get(socket);return NetPoll(state.handle,events,0,0,0);
    }
    unsafe fn socket_poll(items: &mut Buffer<NetPoll>, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetIo {
        if len(items) == 0 { return NetIo(0,4,0); }
        let token: &OpaqueHandle=resource_get(cancel);
        return dlm_net_poll(as_raw(borrowed_ptr(&mut items[0])),len(items),timeout_ms,*token);
    }
    fn network_cancel() -> Resource<OpaqueHandle,net_cancel_dispose> { unsafe { return resource(dlm_net_cancel_new()); } }
    fn network_cancel_valid(cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> Bool {
        let token: &OpaqueHandle=resource_get(cancel);unsafe { return address(handle_ptr<U8>(*token)) != USize(0); }
    }
    fn network_cancel_signal(cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> Unit {
        let token: &OpaqueHandle=resource_get(cancel);unsafe { dlm_net_cancel_signal(*token); }
    }
    fn network_cancel_reset(cancel: &mut Resource<OpaqueHandle,net_cancel_dispose>) -> Unit {
        let token: &OpaqueHandle=resource_get(cancel);unsafe { dlm_net_cancel_reset(*token); }
    }
    fn socket_tls(url: Utf8, timeout_ms: U32, ca_file: Utf8, pin: Utf8) -> Resource<NetOpened,net_dispose> {
        unsafe { return resource(dlm_net_tls(net_text(url),timeout_ms,net_text(ca_file),net_text(pin))); }
    }
    fn http_client(url: Utf8, timeout_ms: U32, maximum: USize, ca_file: Utf8, pin: Utf8) -> Resource<NetOpened,net_dispose> {
        unsafe { return resource(dlm_net_http(net_text(url),timeout_ms,maximum,net_text(ca_file),net_text(pin))); }
    }
    // version: 0 provider default, 1 HTTP/1.1, 2 HTTP/2 only, 3 HTTP/3 only.
    fn http_version(request: &mut Resource<NetOpened,net_dispose>, version: U32) -> NetIo {
        let state: &NetOpened=resource_get(request);unsafe { return dlm_net_http_version(state.handle,version); }
    }
    fn http_header(request: &mut Resource<NetOpened,net_dispose>, header: Utf8) -> NetIo {
        let state: &NetOpened=resource_get(request);unsafe { return dlm_net_http_header(state.handle,net_text(header)); }
    }
    fn http_method(request: &mut Resource<NetOpened,net_dispose>, method: Utf8) -> NetIo {
        let state: &NetOpened=resource_get(request);unsafe { return dlm_net_http_method(state.handle,net_text(method)); }
    }
    fn http_body(request: &mut Resource<NetOpened,net_dispose>, bytes: Slice<U8>) -> NetIo {
        let state: &NetOpened=resource_get(request);unsafe { return dlm_net_http_body(state.handle,net_input(bytes),len(bytes)); }
    }
    fn http_ech(request: &mut Resource<NetOpened,net_dispose>, config: Utf8) -> NetIo {
        let state: &NetOpened=resource_get(request);unsafe { return dlm_net_http_ech(state.handle,net_text(config)); }
    }
    fn http_perform(request: &mut Resource<NetOpened,net_dispose>, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetIo {
        let state: &NetOpened=resource_get(request);let token: &OpaqueHandle=resource_get(cancel);
        unsafe { return dlm_net_http_perform(state.handle,*token); }
    }
    // field: 0 response status, 1 negotiated HTTP version (1/2/3),
    // 2 buffered body length, 3 buffered response-header length.
    fn http_info(request: &Resource<NetOpened,net_dispose>, field: U32) -> NetIo {
        let state: &NetOpened=resource_get(request);unsafe { return dlm_net_http_info(state.handle,field); }
    }
    fn http_read(request: &Resource<NetOpened,net_dispose>, headers: Bool, offset: USize, bytes: &mut Buffer<U8>) -> NetIo {
        let state: &NetOpened=resource_get(request);unsafe { return dlm_net_http_read(state.handle,headers,offset,net_output(bytes),len(bytes)); }
    }
    fn websocket_client(url: Utf8, timeout_ms: U32, ca_file: Utf8, pin: Utf8) -> Resource<NetOpened,net_dispose> {
        unsafe { return resource(dlm_net_websocket(net_text(url),timeout_ms,net_text(ca_file),net_text(pin))); }
    }
    // Set authentication/subprotocol headers with http_header before starting.
    fn websocket_start(socket: &mut Resource<NetOpened,net_dispose>) -> NetIo {
        let state: &NetOpened=resource_get(socket);unsafe { return dlm_net_ws_start(state.handle); }
    }
    // flags: text=1, binary=2, more fragments=4, close=8, ping=16, pong=64.
    fn websocket_write(socket: &mut Resource<NetOpened,net_dispose>, bytes: Slice<U8>, flags: U32, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetIo {
        let state: &NetOpened=resource_get(socket);let token: &OpaqueHandle=resource_get(cancel);
        unsafe { return dlm_net_ws_send(state.handle,net_input(bytes),len(bytes),flags,timeout_ms,*token); }
    }
    fn websocket_read(socket: &mut Resource<NetOpened,net_dispose>, bytes: &mut Buffer<U8>) -> NetWebSocketIo {
        let state: &NetOpened=resource_get(socket);unsafe { return dlm_net_ws_recv(state.handle,net_output(bytes),len(bytes)); }
    }
    fn network_random(bytes: &mut Buffer<U8>) -> NetIo { unsafe { return dlm_net_random(net_output(bytes),len(bytes)); } }
}
