module std.dln_gateway
import std.net_socket

native {
    trust "dlm.gateway.v1";
    record GatewayClient repr(C) { inner: Resource<NetOpened,gateway_client_dispose>; }
    record GatewayServer repr(C) { inner: Resource<NetOpened,gateway_server_dispose>; }
    record GatewayNativeTransfer repr(C, version=1) { required: USize; copied: USize; value: U64; outcome: U32; status: I32; system: I32; }
    record GatewayTransfer repr(C) { required: USize; copied: USize; value: U64; outcome: U32; error: NetworkError; }
    record GatewayStatistics repr(C, version=1) { accepted: U64; authenticated: U64; denied: U64; failed: U64; requests: U64; forwarded: U64; returned: U64; active: U64; handshakes: U64; }
    extern "C" fn dln_gateway_abi() -> U32 from "dlm.gateway.v1" guarantees(NoUnwind,NoAlloc,NoBlocking,NoIO);
    extern "C" fn dln_gateway_client_open(input: Ptr<U8>, count: USize) -> NetOpened from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_client_close(handle: OpaqueHandle) -> Unit from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_server_open(input: Ptr<U8>, count: USize) -> NetOpened from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_server_close(handle: OpaqueHandle) -> Unit from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_stream_open(handle: OpaqueHandle, protocol: U32, input: Ptr<U8>, count: USize) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_stream_write(handle: OpaqueHandle, stream: U32, input: Ptr<U8>, count: USize) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_stream_read(handle: OpaqueHandle, stream: U32, maximum: USize, output: Ptr<U8>, capacity: USize) -> GatewayNativeTransfer from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_datagram(handle: OpaqueHandle, stream: U32, input: Ptr<U8>, count: USize, output: Ptr<U8>, capacity: USize) -> GatewayNativeTransfer from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_take(handle: OpaqueHandle, output: Ptr<U8>, capacity: USize) -> GatewayNativeTransfer from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_stream_close(handle: OpaqueHandle, stream: U32, half: Bool) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_store(handle: OpaqueHandle, id: Ptr<U8>, expires: U64, input: Ptr<U8>, count: USize) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_fetch(handle: OpaqueHandle, id: Ptr<U8>, output: Ptr<U8>, capacity: USize) -> GatewayNativeTransfer from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_consume(handle: OpaqueHandle, id: Ptr<U8>) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_server_statistics(handle: OpaqueHandle) -> GatewayStatistics from "dlm.gateway.v1" guarantees(NoUnwind);
    extern "C" fn dln_gateway_server_address(handle: OpaqueHandle, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.gateway.v1" guarantees(NoUnwind);
    unsafe fn gateway_client_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { dln_gateway_client_close(state.handle); }
    unsafe fn gateway_server_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { dln_gateway_server_close(state.handle); }
    fn gateway_connect(config: Slice<U8>) -> GatewayClient { unsafe { let text: NetText=net_bytes(config);return GatewayClient(resource(dln_gateway_client_open(text.data,text.count))); } }
    fn gateway_serve(config: Slice<U8>) -> GatewayServer { unsafe { let text: NetText=net_bytes(config);return GatewayServer(resource(dln_gateway_server_open(text.data,text.count))); } }
    fn gateway_error(client: &GatewayClient) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { let state: &NetOpened=resource_get(&client.inner);return NetworkError(state.status,state.system,170); }
    fn gateway_server_error(server: &GatewayServer) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { let state: &NetOpened=resource_get(&server.inner);return NetworkError(state.status,state.system,171); }
    fn gateway_close(client: GatewayClient) -> Unit { resource_close(move client.inner); }
    fn gateway_server_close(server: GatewayServer) -> Unit { resource_close(move server.inner); }
    fn gateway_stream(client: &mut GatewayClient, protocol: U32, target: Slice<U8>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&client.inner);let text: NetText=net_bytes(target);return socket_transfer(dln_gateway_stream_open(state.handle,protocol,text.data,text.count),172); } }
    fn gateway_write(client: &mut GatewayClient, stream: U32, input: Slice<U8>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&client.inner);let text: NetText=net_bytes(input);return socket_transfer(dln_gateway_stream_write(state.handle,stream,text.data,text.count),173); } }
    fn gateway_transfer(r: GatewayNativeTransfer) -> GatewayTransfer guarantees(NoAlloc,NoBlocking,NoIO) { return GatewayTransfer(r.required,r.copied,r.value,r.outcome,NetworkError(r.status,r.system,174)); }
    fn gateway_read(client: &mut GatewayClient, stream: U32, maximum: USize, output: &mut Buffer<U8>) -> GatewayTransfer { unsafe { let state: &NetOpened=resource_get(&client.inner);return gateway_transfer(dln_gateway_stream_read(state.handle,stream,maximum,net_output(output),len(output))); } }
    fn gateway_datagram(client: &mut GatewayClient, stream: U32, input: Slice<U8>, output: &mut Buffer<U8>) -> GatewayTransfer { unsafe { let state: &NetOpened=resource_get(&client.inner);let text: NetText=net_bytes(input);return gateway_transfer(dln_gateway_datagram(state.handle,stream,text.data,text.count,net_output(output),len(output))); } }
    fn gateway_take(client: &mut GatewayClient, output: &mut Buffer<U8>) -> GatewayTransfer { unsafe { let state: &NetOpened=resource_get(&client.inner);return gateway_transfer(dln_gateway_take(state.handle,net_output(output),len(output))); } }
    fn gateway_stream_close(client: &mut GatewayClient, stream: U32, half: Bool) -> NetworkError { unsafe { let state: &NetOpened=resource_get(&client.inner);let r: NetIo=dln_gateway_stream_close(state.handle,stream,half);return NetworkError(r.status,r.system,175); } }
    fn gateway_store(client: &mut GatewayClient, id: Slice<U8>, expires: U64, input: Slice<U8>) -> NetworkTransfer { if len(id)!=16 { return NetworkTransfer(0,NetworkError(4,0,176)); }unsafe { let state: &NetOpened=resource_get(&client.inner);let key: NetText=net_bytes(id);let text: NetText=net_bytes(input);return socket_transfer(dln_gateway_store(state.handle,key.data,expires,text.data,text.count),176); } }
    fn gateway_fetch(client: &mut GatewayClient, id: Slice<U8>, output: &mut Buffer<U8>) -> GatewayTransfer { if len(id)!=16 { return GatewayTransfer(0,0,0,0,NetworkError(4,0,177)); }unsafe { let state: &NetOpened=resource_get(&client.inner);let key: NetText=net_bytes(id);return gateway_transfer(dln_gateway_fetch(state.handle,key.data,net_output(output),len(output))); } }
    fn gateway_consume(client: &mut GatewayClient, id: Slice<U8>) -> NetworkError { if len(id)!=16 { return NetworkError(4,0,179); }unsafe { let state: &NetOpened=resource_get(&client.inner);let key: NetText=net_bytes(id);let r: NetIo=dln_gateway_consume(state.handle,key.data);return NetworkError(r.status,r.system,179); } }
    fn gateway_statistics(server: &GatewayServer) -> GatewayStatistics { unsafe { let state: &NetOpened=resource_get(&server.inner);return dln_gateway_server_statistics(state.handle); } }
    fn gateway_address(server: &GatewayServer, output: &mut Buffer<U8>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&server.inner);return socket_transfer(dln_gateway_server_address(state.handle,net_output(output),len(output)),178); } }
}
