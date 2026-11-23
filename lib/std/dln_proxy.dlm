module std.dln_proxy
import std.dln_gateway

native {
    trust "dlm.gateway.v1";
    record GatewayProxy repr(C) { inner: Resource<NetOpened,gateway_proxy_dispose>; }
    record GatewayProxyServer repr(C) { inner: Resource<NetOpened,gateway_proxy_server_dispose>; }
    record GatewayProxyCancel repr(C) { inner: Resource<NetOpened,gateway_proxy_cancel_dispose>; }
    record GatewayIpAssignment repr(C, version=1) { family: U32; prefix: U32; address: Array<U8,16>; }
    record GatewayIpRoute repr(C, version=1) { family: U32; protocol: U32; first: Array<U8,16>; last: Array<U8,16>; }
    record GatewayProxyStatistics repr(C, version=1) { accepted: U64; authenticated: U64; denied: U64; active: U64; incoming: U64; outgoing: U64; udp: U64; ip: U64; failed: U64; }
    extern "C" fn dln_gateway_proxy_open(a0: Ptr<U8>,a1: USize,a2: U32,a3: Ptr<U8>,a4: USize) -> NetOpened from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_proxy_close(a0: OpaqueHandle) -> Unit from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_proxy_write(a0: OpaqueHandle,a1: Ptr<U8>,a2: USize) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_proxy_read(a0: OpaqueHandle,a1: USize,a2: Ptr<U8>,a3: USize) -> GatewayNativeTransfer from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_proxy_take(a0: OpaqueHandle,a1: Ptr<U8>,a2: USize) -> GatewayNativeTransfer from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_proxy_assignments(a0: OpaqueHandle,a1: Ptr<GatewayIpAssignment>,a2: USize) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind,NoAlloc,NoBlocking,NoIO);
    extern "C" fn dln_gateway_proxy_routes(a0: OpaqueHandle,a1: Ptr<GatewayIpRoute>,a2: USize) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind,NoAlloc,NoBlocking,NoIO);
    extern "C" fn dln_gateway_proxy_cancel_open(a0: OpaqueHandle) -> NetOpened from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_proxy_cancel_request(a0: OpaqueHandle) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind,NoAlloc,NoBlocking,NoIO);
    extern "C" fn dln_gateway_proxy_cancel_close(a0: OpaqueHandle) -> Unit from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_proxy_server_open(a0: Ptr<U8>,a1: USize) -> NetOpened from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_proxy_server_close(a0: OpaqueHandle) -> Unit from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_proxy_server_stop(a0: OpaqueHandle) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind,NoAlloc,NoBlocking,NoIO);
    extern "C" fn dln_gateway_proxy_server_statistics(a0: OpaqueHandle) -> GatewayProxyStatistics from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_proxy_server_address(a0: OpaqueHandle,a1: Ptr<U8>,a2: USize) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_carriers(a0: Ptr<U8>,a1: USize) -> GatewayNativeTransfer from "dlm.gateway.v1" guarantees(NoUnwind);
    unsafe fn gateway_proxy_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { dln_gateway_proxy_close(state.handle); }
    unsafe fn gateway_proxy_server_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { dln_gateway_proxy_server_close(state.handle); }
    unsafe fn gateway_proxy_cancel_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { dln_gateway_proxy_cancel_close(state.handle); }
    fn gateway_proxy(config: Slice<U8>, mode: U32, target: Slice<U8>) -> GatewayProxy { unsafe { let c: NetText=net_bytes(config);let t: NetText=net_bytes(target);return GatewayProxy(resource(dln_gateway_proxy_open(c.data,c.count,mode,t.data,t.count))); } }
    fn gateway_proxy_server(config: Slice<U8>) -> GatewayProxyServer { unsafe { let c: NetText=net_bytes(config);return GatewayProxyServer(resource(dln_gateway_proxy_server_open(c.data,c.count))); } }
    fn gateway_proxy_error(proxy: &GatewayProxy) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { let s: &NetOpened=resource_get(&proxy.inner);return NetworkError(s.status,s.system,200); }
    fn gateway_proxy_server_error(server: &GatewayProxyServer) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { let s: &NetOpened=resource_get(&server.inner);return NetworkError(s.status,s.system,201); }
    fn gateway_proxy_close(proxy: GatewayProxy) -> Unit { resource_close(move proxy.inner); }
    fn gateway_proxy_server_close(server: GatewayProxyServer) -> Unit { resource_close(move server.inner); }
    fn gateway_proxy_write(proxy: &mut GatewayProxy, input: Slice<U8>) -> NetworkTransfer { unsafe { let s: &NetOpened=resource_get(&proxy.inner);let d: NetText=net_bytes(input);return socket_transfer(dln_gateway_proxy_write(s.handle,d.data,d.count),202); } }
    fn gateway_proxy_read(proxy: &mut GatewayProxy, maximum: USize, output: &mut Buffer<U8>) -> GatewayTransfer { unsafe { let s: &NetOpened=resource_get(&proxy.inner);return gateway_transfer(dln_gateway_proxy_read(s.handle,maximum,net_output(output),len(output))); } }
    fn gateway_proxy_take(proxy: &mut GatewayProxy, output: &mut Buffer<U8>) -> GatewayTransfer { unsafe { let s: &NetOpened=resource_get(&proxy.inner);return gateway_transfer(dln_gateway_proxy_take(s.handle,net_output(output),len(output))); } }
    fn gateway_proxy_assignments(proxy: &GatewayProxy, output: &mut Buffer<GatewayIpAssignment>) -> NetworkTransfer guarantees(NoAlloc,NoBlocking,NoIO) { unsafe { let s: &NetOpened=resource_get(&proxy.inner);if len(output)==0 { return socket_transfer(dln_gateway_proxy_assignments(s.handle,null<GatewayIpAssignment>(),0),207); }return socket_transfer(dln_gateway_proxy_assignments(s.handle,as_raw(borrowed_ptr(&mut output[0])),len(output)),207); } }
    fn gateway_proxy_routes(proxy: &GatewayProxy, output: &mut Buffer<GatewayIpRoute>) -> NetworkTransfer guarantees(NoAlloc,NoBlocking,NoIO) { unsafe { let s: &NetOpened=resource_get(&proxy.inner);if len(output)==0 { return socket_transfer(dln_gateway_proxy_routes(s.handle,null<GatewayIpRoute>(),0),208); }return socket_transfer(dln_gateway_proxy_routes(s.handle,as_raw(borrowed_ptr(&mut output[0])),len(output)),208); } }
    fn gateway_proxy_cancellation(proxy: &GatewayProxy) -> GatewayProxyCancel { unsafe { let s: &NetOpened=resource_get(&proxy.inner);return GatewayProxyCancel(resource(dln_gateway_proxy_cancel_open(s.handle))); } }
    fn gateway_proxy_cancel_error(token: &GatewayProxyCancel) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { let s: &NetOpened=resource_get(&token.inner);return NetworkError(s.status,s.system,203); }
    fn gateway_proxy_cancel(token: &GatewayProxyCancel) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { unsafe { let s: &NetOpened=resource_get(&token.inner);let r: NetIo=dln_gateway_proxy_cancel_request(s.handle);return NetworkError(r.status,r.system,204); } }
    fn gateway_proxy_cancel_close(token: GatewayProxyCancel) -> Unit { resource_close(move token.inner); }
    fn gateway_proxy_stop(server: &GatewayProxyServer) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { unsafe { let s: &NetOpened=resource_get(&server.inner);let r: NetIo=dln_gateway_proxy_server_stop(s.handle);return NetworkError(r.status,r.system,205); } }
    fn gateway_proxy_statistics(server: &GatewayProxyServer) -> GatewayProxyStatistics { unsafe { let s: &NetOpened=resource_get(&server.inner);return dln_gateway_proxy_server_statistics(s.handle); } }
    fn gateway_proxy_address(server: &GatewayProxyServer, output: &mut Buffer<U8>) -> NetworkTransfer { unsafe { let s: &NetOpened=resource_get(&server.inner);return socket_transfer(dln_gateway_proxy_server_address(s.handle,net_output(output),len(output)),206); } }
    fn gateway_carriers(output: &mut Buffer<U8>) -> GatewayTransfer { unsafe { return gateway_transfer(dln_gateway_carriers(net_output(output),len(output))); } }
    fn gateway_proxy_tcp() -> U32 guarantees(NoAlloc,NoBlocking,NoIO) { return 1; }
    fn gateway_proxy_udp() -> U32 guarantees(NoAlloc,NoBlocking,NoIO) { return 2; }
    fn gateway_proxy_ip() -> U32 guarantees(NoAlloc,NoBlocking,NoIO) { return 3; }
}
