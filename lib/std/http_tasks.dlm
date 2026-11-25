module std.http_tasks
import std.http_server

native {
    record HttpExchange repr(C) { request: HttpRequest; input: Receiver<HttpBody>; output: Sender<HttpEvent>; completion: Receiver<Bool>; }
    record HttpEvent repr(C) {
        action: U32; status: U32; length: U64; chunked: Bool; close: Bool; final: Bool; bytes: Buffer<U8>;
    }
    record HttpPump repr(C) {
        ticket: HttpTicket; input: Sender<HttpBody>; output: Receiver<HttpEvent>;
        completion: Sender<Bool>; event: HttpEvent; offset: USize;
        pending: Bool; input_done: Bool; websocket: Bool; discard: Bool; close_sent: Bool;
    }
    record HttpDispatch repr(C) { pumps: Buffer<Box<HttpPump>>; requests: Sender<HttpExchange>; capacity: USize; chunk: USize; }
    record HttpSocketMessage repr(C) { bytes: Buffer<U8>; opcode: U32; first: Bool; final: Bool; status: I32; }
    record HttpRoute repr(C) { method: Buffer<U8>; path: Buffer<U8>; id: U32; }

    fn http_route(routes: Slice<HttpRoute>, request: &HttpRequest) -> U32 {
        let mut fallback: U32 = 0;
        for i in 0..len(routes) {
            let route: &HttpRoute = &routes[i];
            if http_path_is(request,slice(&route.path,0,len(&route.path))) {
                let method: Slice<U8> = slice(&route.method,0,len(&route.method));
                if http_method_is(request,method) { return route.id; }
                if http_is_head(request) && len(method) == 3 && method[0] == U8(71) && method[1] == U8(69) && method[2] == U8(84) { fallback = route.id; }
            }
        }
        return fallback;
    }
    fn http_dispatch(requests: Sender<HttpExchange>, capacity: USize, chunk: USize) -> HttpDispatch {
        assert(capacity > USize(0) && capacity <= USize(256));
        assert(chunk >= USize(256) && chunk <= USize(65536));
        return HttpDispatch(buffer<Box<HttpPump>>(0),move requests,capacity,chunk);
    }
    fn http_dispatch_request(dispatch: &mut HttpDispatch, request: HttpRequest) -> Bool {
        if len(&dispatch.pumps) == dispatch.capacity || !sender_ready(&dispatch.requests) { return false; }
        let ticket: HttpTicket = request.ticket;
        let input: Sender<HttpBody> = channel<HttpBody>(1);
        let reader: Receiver<HttpBody> = channel_receiver(&input);
        let output: Sender<HttpEvent> = channel<HttpEvent>(1);
        let events: Receiver<HttpEvent> = channel_receiver(&output);
        let done: Sender<Bool> = channel<Bool>(1);
        let completion: Receiver<Bool> = channel_receiver(&done);
        if !try_send(&dispatch.requests,HttpExchange(move request,move reader,move output,move completion)) { return false; }
        push(&mut dispatch.pumps,box<HttpPump>(HttpPump(ticket,move input,move events,move done,zero<HttpEvent>(),0,false,false,false,false,false)));
        return true;
    }
    fn http_pump_event(server: &mut HttpServer, pump: &mut HttpPump) -> I32 {
        if !pump.pending {
            if !try_receive(&pump.output,&mut pump.event) {
                if receiver_connected(&pump.output) { return 1; }
                if pump.websocket {
                    if pump.close_sent { return 1; }
                    let closed: NetIo = http_websocket_write(server,pump.ticket.id,8,true,utf8_bytes(""));
                    if closed.status != 0 { return closed.status; }
                    pump.close_sent = true;
                    return 1;
                }
                return 2;
            }
            pump.pending=true; pump.offset=0;
        }
        let id: U64 = pump.ticket.id;
        let action: U32 = pump.event.action;
        let mut result: NetIo = zero<NetIo>();
        {
            let bytes: Slice<U8> = slice(&pump.event.bytes,pump.offset,len(&pump.event.bytes)-pump.offset);
            if action == 1 {
                result = http_response_start(server,id,pump.event.status,bytes,pump.event.length,pump.event.chunked,pump.event.close);
            } else { if action == 2 { result = http_response_write(server,id,bytes); }
            else { if action == 3 { result = http_response_end(server,id); }
            else { if action == 4 { result = http_websocket_upgrade(server,id); }
            else { if action == 5 { result = http_websocket_write(server,id,pump.event.status,pump.event.final,bytes); }
            else { return 4; } } } } }
        }
        if result.status != 0 { return result.status; }
        if action == 2 {
            pump.offset = pump.offset+result.count;
            if pump.offset < len(&pump.event.bytes) { return 1; }
        }
        if action == 5 && pump.event.status == 8 { pump.close_sent = true; }
        pump.pending=false;
        pump.event = zero<HttpEvent>();
        if action == 3 { return 3; }
        if action == 4 { pump.websocket=true; pump.input_done=false; pump.discard=false; }
        return 0;
    }
    fn http_pump_input(server: &mut HttpServer, pump: &mut HttpPump, chunk: USize) -> I32 {
        if pump.input_done { return 0; }
        if !sender_connected(&pump.input) { pump.discard = true; }
        if !pump.discard && !sender_ready(&pump.input) { return 1; }
        let mut bytes: Buffer<U8> = buffer<U8>(chunk);
        if pump.websocket {
            let part: HttpWebSocketPart = http_websocket_read(server,pump.ticket.id,&mut bytes);
            if part.status != 0 {
                if part.status != 1 && !pump.discard {
                    let sent: Bool = try_send(&pump.input,HttpBody(buffer<U8>(0),part.status));
                }
                return part.status;
            }
            resize(&mut bytes,part.count);
            let mut code: I32 = 100+cast<I32>(part.opcode);
            if part.first { code=code+16; }
            if part.final { code=code+32; }
            if !pump.discard { let sent: Bool = try_send(&pump.input,HttpBody(move bytes,code)); }
            return 0;
        }
        let result: NetIo = http_body_read(server,pump.ticket.id,&mut bytes);
        if result.status == 1 { return 1; }
        if result.status == 2 && pump.ticket.websocket { pump.input_done = true; return 0; }
        if result.status == 0 { resize(&mut bytes,result.count); }
        else { resize(&mut bytes,0); pump.input_done = true; }
        if !pump.discard { let sent: Bool = try_send(&pump.input,HttpBody(move bytes,result.status)); }
        if result.status != I32(0) && result.status != I32(2) { return -1; }
        return 0;
    }
    fn http_pump(server: &mut HttpServer, pump: &mut HttpPump, chunk: USize) -> U32 {
        let id: U64 = pump.ticket.id;
        if !http_exchange_alive(server,id) { return 2; }
        let output: I32 = http_pump_event(server,pump);
        if output == 3 { return 1; }
        if output != 0 && output != 1 {
            let closed: NetIo = http_exchange_close(server,id); return 2;
        }
        let input: I32 = http_pump_input(server,pump,chunk);
        if pump.websocket && input == I32(2) { return 1; }
        if input == -1 || (pump.websocket && input == I32(11)) { return 2; }
        if input != 0 && input != 1 {
            let closed: NetIo = http_exchange_close(server,id); return 2;
        }
        return 0;
    }
    fn http_dispatch_poll(server: &mut HttpServer, dispatch: &mut HttpDispatch, cancel: &Resource<OpaqueHandle,net_cancel_dispose>, wait: U32) -> NetIo {
        let tick: NetIo = http_server_poll(server,wait,cancel);
        if tick.status != 0 { return tick; }
        let mut chunk: USize = dispatch.chunk;
        if chunk > server.limits.chunk { chunk = server.limits.chunk; }
        let mut i: USize = 0;
        while i < len(&dispatch.pumps) {
            let state: U32 = http_pump(server,&mut *dispatch.pumps[i],chunk);
            if state == 0 { i = i+1; }
            else {
                let delivered: Bool = try_send(&(*dispatch.pumps[i]).completion,state == U32(2));
                let last: USize = len(&dispatch.pumps)-1;
                if i < last {
                    let mut tail: Box<HttpPump> = zero<Box<HttpPump>>();
                    {
                        let slot: &mut Box<HttpPump> = &mut dispatch.pumps[last];
                        tail = move *slot;
                    }
                    dispatch.pumps[i] = move tail;
                }
                resize(&mut dispatch.pumps,last);
            }
        }
        let mut ready: Bool = true;
        while ready && len(&dispatch.pumps) < dispatch.capacity && sender_ready(&dispatch.requests) {
            let request: HttpRequest = http_next_request(server);
            if request.ticket.status != 0 { ready=false; }
            else {
                let id: U64 = request.ticket.id;
                if !http_dispatch_request(dispatch,move request) { let closed: NetIo = http_exchange_close(server,id); ready=false; }
            }
        }
        return tick;
    }
    fn http_dispatch_shutdown(server: &mut HttpServer, dispatch: &mut HttpDispatch, grace_ms: U32) -> NetIo {
        let stopped: NetIo = http_server_stop(server,grace_ms);
        if stopped.status != 0 { return stopped; }
        let cancel: Resource<OpaqueHandle,net_cancel_dispose> = network_cancel();
        while true {
            let tick: NetIo = http_dispatch_poll(server,dispatch,&cancel,10);
            if tick.status != 0 || tick.count == USize(0) { return tick; }
        }
    }
    async fn http_send_response(output: Sender<HttpEvent>, response: HttpResponse, head: Bool) -> Bool {
        let bytes: U64 = cast<U64>(len(&response.body));
        let started: Bool = await send_async(sender_clone(&output),HttpEvent(1,response.status,bytes,false,response.close,false,move response.headers));
        if !started { return false; }
        if !head && response.status != 204 && response.status != 205 && response.status != 304 && bytes != U64(0) {
            let wrote: Bool = await send_async(sender_clone(&output),HttpEvent(2,0,0,false,false,false,move response.body));
            if !wrote { return false; }
        }
        return await send_async(move output,HttpEvent(3,0,0,false,false,false,buffer<U8>(0)));
    }
    async fn http_stream_start(output: Sender<HttpEvent>, status: U32, headers: Buffer<U8>, close: Bool) -> Bool {
        return await send_async(move output,HttpEvent(1,status,0,true,close,false,move headers));
    }
    async fn http_stream_write(output: Sender<HttpEvent>, bytes: Buffer<U8>) -> Bool {
        return await send_async(move output,HttpEvent(2,0,0,false,false,false,move bytes));
    }
    async fn http_stream_end(output: Sender<HttpEvent>) -> Bool {
        return await send_async(move output,HttpEvent(3,0,0,false,false,false,buffer<U8>(0)));
    }
    async fn http_upgrade(output: Sender<HttpEvent>) -> Bool {
        return await send_async(move output,HttpEvent(4,0,0,false,false,false,buffer<U8>(0)));
    }
    async fn http_socket_receive(input: Receiver<HttpBody>) -> HttpSocketMessage {
        let event: HttpBody = await receive_async(move input);
        if event.status < I32(100) { return HttpSocketMessage(move event.bytes,0,false,false,event.status); }
        let code: U32 = cast<U32>(event.status-I32(100));
        return HttpSocketMessage(move event.bytes,code & U32(15),(code & U32(16)) != U32(0),(code & U32(32)) != U32(0),0);
    }
    async fn http_socket_send(output: Sender<HttpEvent>, opcode: U32, final: Bool, bytes: Buffer<U8>) -> Bool {
        return await send_async(move output,HttpEvent(5,opcode,0,false,false,final,move bytes));
    }
}
