module std.http_server
import std.net_socket
import std.json

native {
    trust "dlm.http.v1";
    record HttpLimits repr(C, version=1) {
        headers: USize; body: USize; chunk: USize; output: USize;
        connections: U32; requests: U32; deadline: U32; idle: U32;
    }
    record HttpTicket repr(C, version=1) {
        id: U64; method: USize; target: USize; headers: USize; body: USize;
        keep_alive: Bool; chunked: Bool; websocket: Bool; status: I32;
    }
    record HttpHeader repr(C) { name: Buffer<U8>; value: Buffer<U8>; }
    record HttpRequest repr(C) { ticket: HttpTicket; method: Buffer<U8>; target: Buffer<U8>; headers: Buffer<HttpHeader>; }
    record HttpResponse repr(C) { status: U32; headers: Buffer<U8>; body: Buffer<U8>; close: Bool; }
    record HttpBody repr(C) { bytes: Buffer<U8>; status: I32; }
    record HttpWebSocketPart repr(C, version=1) { count: USize; opcode: U32; first: Bool; final: Bool; status: I32; }
    record HttpServer repr(C) { inner: Resource<NetOpened,http_server_dispose>; limits: HttpLimits; }

    extern "C" fn dlm_http_server_open(address: NetAddress, limits: HttpLimits, certificate: NetText, key: NetText) -> NetOpened from "dlm.http.v1" guarantees(NoUnwind);
    extern "C" fn dlm_http_server_close(server: OpaqueHandle) -> Unit from "dlm.http.v1" guarantees(NoUnwind);
    extern "C" fn dlm_http_server_local(server: OpaqueHandle) -> NetResolved from "dlm.http.v1" guarantees(NoUnwind);
    extern "C" fn dlm_http_server_stop(server: OpaqueHandle, grace_ms: U32) -> NetIo from "dlm.http.v1" guarantees(NoUnwind);
    extern "C" fn dlm_http_server_poll(server: OpaqueHandle, milliseconds: U32, cancel: OpaqueHandle) -> NetIo from "dlm.http.v1" guarantees(NoUnwind);
    extern "C" fn dlm_http_next_request(server: OpaqueHandle) -> HttpTicket from "dlm.http.v1" guarantees(NoUnwind);
    extern "C" fn dlm_http_request_part(server: OpaqueHandle, id: U64, part: U32, index: USize, out: Ptr<U8>, capacity: USize) -> NetIo from "dlm.http.v1" guarantees(NoUnwind);
    extern "C" fn dlm_http_request_taken(server: OpaqueHandle, id: U64) -> NetIo from "dlm.http.v1" guarantees(NoUnwind);
    extern "C" fn dlm_http_body(server: OpaqueHandle, id: U64, out: Ptr<U8>, capacity: USize) -> NetIo from "dlm.http.v1" guarantees(NoUnwind);
    extern "C" fn dlm_http_response_start(server: OpaqueHandle, id: U64, status: U32, headers: NetText, length: U64, chunked: Bool, close: Bool) -> NetIo from "dlm.http.v1" guarantees(NoUnwind);
    extern "C" fn dlm_http_response_write(server: OpaqueHandle, id: U64, bytes: NetText) -> NetIo from "dlm.http.v1" guarantees(NoUnwind);
    extern "C" fn dlm_http_response_end(server: OpaqueHandle, id: U64) -> NetIo from "dlm.http.v1" guarantees(NoUnwind);
    extern "C" fn dlm_http_exchange_alive(server: OpaqueHandle, id: U64) -> Bool from "dlm.http.v1" guarantees(NoUnwind);
    extern "C" fn dlm_http_exchange_close(server: OpaqueHandle, id: U64) -> NetIo from "dlm.http.v1" guarantees(NoUnwind);
    extern "C" fn dlm_http_websocket_upgrade(server: OpaqueHandle, id: U64) -> NetIo from "dlm.http.v1" guarantees(NoUnwind);
    extern "C" fn dlm_http_websocket_read(server: OpaqueHandle, id: U64, out: Ptr<U8>, capacity: USize) -> HttpWebSocketPart from "dlm.http.v1" guarantees(NoUnwind);
    extern "C" fn dlm_http_websocket_write(server: OpaqueHandle, id: U64, opcode: U32, final: Bool, bytes: NetText) -> NetIo from "dlm.http.v1" guarantees(NoUnwind);

    fn http_limits() -> HttpLimits { return HttpLimits(16384,1048576,4096,32768,64,100,15000,10000); }
    fn http_server_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { unsafe { dlm_http_server_close(state.handle); } }
    fn http_server_open(address: SocketAddress, limits: HttpLimits) -> HttpServer {
        unsafe { return HttpServer(resource(dlm_http_server_open(address.value,limits,net_text(""),net_text(""))),limits); }
    }
    fn http_server_tls(address: SocketAddress, limits: HttpLimits, certificate: Slice<U8>, key: Slice<U8>) -> HttpServer {
        unsafe { return HttpServer(resource(dlm_http_server_open(address.value,limits,net_bytes(certificate),net_bytes(key))),limits); }
    }
    fn http_server_error(server: &HttpServer) -> NetworkError {
        let owner: &NetOpened = resource_get(&server.inner);
        return NetworkError(owner.status,owner.system,50);
    }
    unsafe fn http_server_handle(server: &HttpServer) -> OpaqueHandle { return resource_get(&server.inner).handle; }
    fn http_server_address(server: &HttpServer) -> SocketAddressResult {
        unsafe { return socket_address_result(dlm_http_server_local(http_server_handle(server)),50); }
    }
    fn http_server_close(server: HttpServer) -> Unit { resource_close(move server.inner); }
    fn http_server_stop(server: &mut HttpServer, grace_ms: U32) -> NetIo {
        unsafe { return dlm_http_server_stop(http_server_handle(server),grace_ms); }
    }
    fn http_server_poll(server: &mut HttpServer, ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetIo {
        unsafe { return dlm_http_server_poll(http_server_handle(server),ms,*resource_get(cancel)); }
    }
    unsafe fn http_request_part(server: OpaqueHandle, id: U64, part: U32, index: USize) -> Buffer<U8> {
        let size: NetIo = dlm_http_request_part(server,id,part,index,zero<Ptr<U8>>(),0);
        if size.status != 0 { return buffer<U8>(0); }
        let mut bytes: Buffer<U8> = buffer<U8>(size.count);
        let result: NetIo = dlm_http_request_part(server,id,part,index,net_output(&mut bytes),size.count);
        if result.status != 0 { return buffer<U8>(0); }
        return move bytes;
    }
    fn http_next_request(server: &mut HttpServer) -> HttpRequest {
        unsafe {
            let handle: OpaqueHandle = http_server_handle(server);
            let ticket: HttpTicket = dlm_http_next_request(handle);
            if ticket.status != 0 { return HttpRequest(ticket,buffer<U8>(0),buffer<U8>(0),buffer<HttpHeader>(0)); }
            let method: Buffer<U8> = http_request_part(handle,ticket.id,0,0);
            let target: Buffer<U8> = http_request_part(handle,ticket.id,1,0);
            let mut headers: Buffer<HttpHeader> = buffer<HttpHeader>(0);
            for i in 0..ticket.headers {
                push(&mut headers,HttpHeader(http_request_part(handle,ticket.id,2,i),http_request_part(handle,ticket.id,3,i)));
            }
            let taken: NetIo = dlm_http_request_taken(handle,ticket.id);
            assert(taken.status == I32(0));
            return HttpRequest(ticket,move method,move target,move headers);
        }
    }
    fn http_body_read(server: &mut HttpServer, id: U64, bytes: &mut Buffer<U8>) -> NetIo {
        unsafe { return dlm_http_body(http_server_handle(server),id,net_output(bytes),len(bytes)); }
    }
    fn http_response_start(server: &mut HttpServer, id: U64, status: U32, headers: Slice<U8>, length: U64, chunked: Bool, close: Bool) -> NetIo {
        unsafe { return dlm_http_response_start(http_server_handle(server),id,status,net_bytes(headers),length,chunked,close); }
    }
    fn http_response_write(server: &mut HttpServer, id: U64, bytes: Slice<U8>) -> NetIo {
        unsafe { return dlm_http_response_write(http_server_handle(server),id,net_bytes(bytes)); }
    }
    fn http_response_end(server: &mut HttpServer, id: U64) -> NetIo {
        unsafe { return dlm_http_response_end(http_server_handle(server),id); }
    }
    fn http_exchange_alive(server: &HttpServer, id: U64) -> Bool {
        unsafe { return dlm_http_exchange_alive(http_server_handle(server),id); }
    }
    fn http_exchange_close(server: &mut HttpServer, id: U64) -> NetIo {
        unsafe { return dlm_http_exchange_close(http_server_handle(server),id); }
    }
    fn http_websocket_upgrade(server: &mut HttpServer, id: U64) -> NetIo {
        unsafe { return dlm_http_websocket_upgrade(http_server_handle(server),id); }
    }
    fn http_websocket_read(server: &mut HttpServer, id: U64, bytes: &mut Buffer<U8>) -> HttpWebSocketPart {
        unsafe { return dlm_http_websocket_read(http_server_handle(server),id,net_output(bytes),len(bytes)); }
    }
    fn http_websocket_write(server: &mut HttpServer, id: U64, opcode: U32, final: Bool, bytes: Slice<U8>) -> NetIo {
        unsafe { return dlm_http_websocket_write(http_server_handle(server),id,opcode,final,net_bytes(bytes)); }
    }
    fn http_copy(input: Slice<U8>) -> Buffer<U8> {
        let mut bytes: Buffer<U8> = buffer<U8>(len(input));
        for i in 0..len(input) { bytes[i] = input[i]; }
        return move bytes;
    }
    fn http_method_is(request: &HttpRequest, method: Slice<U8>) -> Bool {
        if len(&request.method) != len(method) { return false; }
        for i in 0..len(method) { if request.method[i] != method[i] { return false; } }
        return true;
    }
    fn http_path_is(request: &HttpRequest, path: Slice<U8>) -> Bool {
        if len(&request.target) < len(path) { return false; }
        for i in 0..len(path) { if request.target[i] != path[i] { return false; } }
        return len(&request.target) == len(path) || request.target[len(path)] == U8(63);
    }
    fn http_header_value(request: &HttpRequest, name: Slice<U8>, start: USize) -> USize {
        for i in start..len(&request.headers) {
            if len(&request.headers[i].name) == len(name) {
                let mut equal: Bool = true;
                for j in 0..len(name) {
                    let mut a: U8 = request.headers[i].name[j]; let mut b: U8 = name[j];
                    if a >= 65 && a <= 90 { a = a+32; }
                    if b >= 65 && b <= 90 { b = b+32; }
                    if a != b { equal=false; break; }
                }
                if equal { return i; }
            }
        }
        return len(&request.headers);
    }
    fn http_is_head(request: &HttpRequest) -> Bool { return http_method_is(request,utf8_bytes("HEAD")); }
    fn http_response(status: U32, headers: Slice<U8>, body: Buffer<U8>, close: Bool) -> HttpResponse {
        return HttpResponse(status,http_copy(headers),move body,close);
    }
    fn http_json_response(status: U32, document: &JsonDocument, maximum: USize) -> HttpResponse {
        let result: JsonEncoded = json_encode(document,maximum);
        if result.error.code != 0 { return http_response(500,utf8_bytes(""),buffer<U8>(0),true); }
        return http_response(status,utf8_bytes("Content-Type: application/json\r\n"),move result.bytes,false);
    }
    async fn http_read_json(input: Receiver<HttpBody>, limits: JsonLimits) -> JsonDocument {
        let mut parser: JsonParser = json_parser(limits);
        while true {
            let chunk: HttpBody = await receive_async(receiver_clone(&input));
            if chunk.status == 2 { break; }
            if chunk.status != 0 { parser.error = JsonError(5,parser.seen); break; }
            let status: JsonError = json_feed(&mut parser,slice(&chunk.bytes,0,len(&chunk.bytes)));
            if status.code != 0 { break; }
        }
        return json_finish(move parser);
    }
}
