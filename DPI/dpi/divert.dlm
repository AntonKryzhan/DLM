module dpi.divert
import dpi.engine
import dpi.win32
import dpi.download
import dpi.settings
import dpi.diagnostic
import dpi.capture
import dpi.startup
import dpi.mwo_owner

native {
    trust "WinDivert 2.2";
    // Exact WinDivert 2.x x64 NETWORK address layout, including its 64-byte union.
    record DivertAddress repr(C) { timestamp: I64; flags: U32; reserved: U32; interface_: U32; subinterface: U32; rest: Array<U8,56>; }
    extern "C" fn WinDivertShutdown(handle_: OpaqueHandle, how: I32) -> I32 from "WinDivert 2.2";
    extern "C" fn WinDivertSetParam(handle_: OpaqueHandle, param: I32, value: U64) -> I32 from "WinDivert 2.2";
    extern "C" fn WinDivertRecvEx(handle_: OpaqueHandle, packet: Ptr<U8>, capacity: U32, received: Ptr<U32>, flags: U64, addresses: Ptr<DivertAddress>, address_length: Ptr<U32>, overlapped: Ptr<Overlapped>) -> I32 from "WinDivert 2.2";
    extern "C" fn WinDivertSend(handle_: OpaqueHandle, packet: Ptr<U8>, length: U32, sent: Ptr<U32>, address_: Ptr<DivertAddress>) -> I32 from "WinDivert 2.2";
    extern "C" fn WinDivertSendEx(handle_: OpaqueHandle, packet: Ptr<U8>, length: U32, sent: Ptr<U32>, flags: U64, addresses: Ptr<DivertAddress>, address_length: U32, overlapped: Ptr<Overlapped>) -> I32 from "WinDivert 2.2";
    unsafe fn load_cloud_ranges(index: &mut IpIndex, cached: Slice<U8>, bundled: Slice<U8>, log: OpaqueHandle) -> Unit {
        let mut b: Buffer<U8>=read_file(cached,1048576);let mut p: IpIndex=zero<IpIndex>();if cloud_rules(&mut p,slice(&b,0,len(&b))) { *index=move p;return; }b=read_file(bundled,1048576);
        if cloud_rules(&mut p,slice(&b,0,len(&b))) { *index=move p; }else { log_line(log,utf8_bytes("CDN range file unavailable; domain/process scopes remain available:"));log_line(log,bundled); }
    }
    unsafe fn prepare_policy(o: &Options, p: &mut Policy, stop: OpaqueHandle, log: OpaqueHandle) -> Bool {
        for i in 0..4 {
            if len(&o.paths[i]) == 0 { continue; }let b: Buffer<U8>=read_file(slice(&o.paths[i],0,len(&o.paths[i])),33554432);let mut ok: Bool=false;
            if i == 0 { ok=host_rules(&mut p.hosts,slice(&b,0,len(&b))); }
            if i == 1 { ok=host_rules(&mut p.excluded_hosts,slice(&b,0,len(&b))); }
            if i == 2 { ok=ip_rules(&mut p.ips,slice(&b,0,len(&b))); }
            if i == 3 { ok=ip_rules(&mut p.excluded_ips,slice(&b,0,len(&b))); }
            if !ok { log_line(log,utf8_bytes("Cannot read usable host/IP rules:"));log_line(log,slice(&o.paths[i],0,len(&o.paths[i])));return false; }
        }
        if len(&o.paths[4]) > 0 {
            let source: Slice<U8>=slice(&o.paths[4],0,len(&o.paths[4]));let mut b: Buffer<U8>=buffer<U8>(0);
            if ascii_at(source,0,utf8_bytes("http://")) || ascii_at(source,0,utf8_bytes("https://")) { b=download(source,stop); }else { b=read_file(source,67108864); }
            if !aws_rules(&mut p.aws,slice(&b,0,len(&b)),slice(&o.paths[5],0,len(&o.paths[5]))) { log_line(log,utf8_bytes("AWS rules missing, empty or invalid"));return false; }
        }
        if o.telegram {
            let ranges: Buffer<U8>=read_file(utf8_bytes("data/telegram-cidr.txt"),16384);
            if !ip_rules(&mut p.telegram_ips,slice(&ranges,0,len(&ranges))) { p.telegram_ips=zero<IpIndex>();log_line(log,utf8_bytes("Telegram TCP disabled: data/telegram-cidr.txt missing or invalid; HTTPS processing remains available.")); }
        }
        if o.cloud_cdn {
            load_cloud_ranges(&mut p.cloudflare_ips,utf8_bytes("cache/cloudflare-cidr.txt"),utf8_bytes("data/cloudflare-cidr.txt"),log);
            load_cloud_ranges(&mut p.cloudfront_ips,utf8_bytes("cache/cloudfront-cidr.txt"),utf8_bytes("data/cloudfront-cidr.txt"),log);
            load_cloud_ranges(&mut p.azure_frontdoor_ips,utf8_bytes("cache/azure-frontdoor-cidr.txt"),utf8_bytes("data/azure-frontdoor-cidr.txt"),log);
            load_cloud_ranges(&mut p.azure_cloud_ips,utf8_bytes("cache/azure-cloud-cidr.txt"),utf8_bytes("data/azure-cloud-cidr.txt"),log);
        }
        if o.whitelist && p.aws.count == 0 { log_line(log,utf8_bytes("--whitelist-only requires nonempty AWS rules"));return false; }return true;
    }
    record SendQueue repr(C) { bytes: Buffer<U8>; addresses: Buffer<DivertAddress>; lengths: Buffer<U32>; consecutive: U32; use_batch: Bool; }
    unsafe fn send_error(q: &mut SendQueue, e: &mut Engine, stop: OpaqueHandle, log: OpaqueHandle, error: U32) -> Unit {
        e.statistics.send_errors=e.statistics.send_errors+U64(1);q.consecutive=q.consecutive+U32(1);
        if q.consecutive == U32(1) { log_error(log,utf8_bytes("WinDivert send error: "),error); }
        if q.consecutive >= U32(8) { SetEvent(stop); }
    }
    unsafe fn send_one(q: &mut SendQueue, e: &mut Engine, handle_: OpaqueHandle, stop: OpaqueHandle, log: OpaqueHandle, b: Slice<U8>, address_: DivertAddress) -> Bool {
        let mut address_copy: DivertAddress=address_;let mut actual: U32=0;
        if WinDivertSend(handle_,data(&b),cast<U32>(len(b)),as_raw(borrowed_ptr(&mut actual)),as_raw(borrowed_ptr(&mut address_copy))) == I32(0) || cast<USize>(actual) != len(b) {
            let error: U32=GetLastError();send_error(q,e,stop,log,error);return false;
        }e.statistics.sent=e.statistics.sent+U64(1);q.consecutive=0;return true;
    }
    unsafe fn flush_send(q: &mut SendQueue, e: &mut Engine, handle_: OpaqueHandle, stop: OpaqueHandle, log: OpaqueHandle) -> Unit {
        if len(&q.addresses) == 0 { return; }let mut sent_batch: Bool=false;
        if q.use_batch && len(&q.addresses) > 1 {
            let mut actual: U32=0;
            if WinDivertSendEx(handle_,data(&q.bytes),cast<U32>(len(&q.bytes)),as_raw(borrowed_ptr(&mut actual)),0,data(&q.addresses),cast<U32>(len(&q.addresses)*size_of<DivertAddress>()),null<Overlapped>()) != I32(0) && cast<USize>(actual) == len(&q.bytes) {
                e.statistics.sent=e.statistics.sent+cast<U64>(len(&q.addresses));e.statistics.batches=e.statistics.batches+U64(1);q.consecutive=0;sent_batch=true;
            }else { let error: U32=GetLastError();send_error(q,e,stop,log,error);q.use_batch=false; }
        }
        if !sent_batch {
            let mut offset: USize=0;for i in 0..len(&q.addresses) {
                // Only TCP and its IPv4 fragments enter this queue. An ambiguous
                // partial SendEx may be replayed without duplicating UDP payloads.
                let n: USize=cast<USize>(q.lengths[i]);let mut address_: DivertAddress=q.addresses[i];let mut actual: U32=0;
                if WinDivertSend(handle_,ptr_offset(data(&q.bytes),offset),cast<U32>(n),as_raw(borrowed_ptr(&mut actual)),as_raw(borrowed_ptr(&mut address_))) == I32(0) || cast<USize>(actual) != n {
                    let error: U32=GetLastError();send_error(q,e,stop,log,error);
                }else { e.statistics.sent=e.statistics.sent+U64(1);q.consecutive=0; }offset=offset+n;
            }
        }resize(&mut q.bytes,0);resize(&mut q.addresses,0);resize(&mut q.lengths,0);
    }
    unsafe fn queue_send(q: &mut SendQueue, e: &mut Engine, handle_: OpaqueHandle, stop: OpaqueHandle, log: OpaqueHandle, b: Slice<U8>, address_: DivertAddress, ready: Bool, batchable: Bool) -> Bool {
        let mut copy: DivertAddress=address_;if ready { copy.flags=copy.flags | U32(0xe00000); }
        if !batchable { flush_send(q,e,handle_,stop,log);return send_one(q,e,handle_,stop,log,b,copy); }
        if len(&q.addresses) == 128 || len(&q.bytes)+len(b) > 2097152 { flush_send(q,e,handle_,stop,log); }
        bytes_append(&mut q.bytes,b);push(&mut q.addresses,copy);push(&mut q.lengths,cast<U32>(len(b)));return true;
    }
    unsafe fn queue_batch(q: &mut SendQueue, e: &mut Engine, handle_: OpaqueHandle, stop: OpaqueHandle, log: OpaqueHandle, batch: &Batch, address_: DivertAddress, batchable: Bool) -> Bool {
        let mut ok: Bool=true;let mut at: USize=0;for i in 0..batch.count { let n: USize=cast<USize>(batch.lengths[i]);ok=queue_send(q,e,handle_,stop,log,slice(&batch.bytes,at,n),address_,true,batchable) && ok;at=at+n; }return ok;
    }
    record Delayed repr(C) { bytes: Buffer<U8>; address_: DivertAddress; due: U64; order: U64; generation: U32; used: Bool; }
    record DelayQueue repr(C) { items: Buffer<Delayed>; memory: USize; order: U64; }
    fn delay_packet(q: &mut DelayQueue, b: Slice<U8>, address_: DivertAddress, due: U64, generation: U32) -> Bool {
        if len(b) > 2097152-q.memory { return false; }
        for i in 0..len(&q.items) {
            if !q.items[i].used { q.items[i]=Delayed(bytes_copy(b),address_,due,q.order,generation,true);q.memory=q.memory+len(b);q.order=wrapping_add(q.order,U64(1));return true; }
        }return false;
    }
    fn earliest(q: &DelayQueue) -> USize {
        let mut choice: USize=len(&q.items);let mut due: U64=18446744073709551615;let mut order: U64=18446744073709551615;
        for i in 0..len(&q.items) { if q.items[i].used && (q.items[i].due < due || (q.items[i].due == due && q.items[i].order < order)) { choice=i;due=q.items[i].due;order=q.items[i].order; } }return choice;
    }
    unsafe fn process_packet(e: &mut Engine, q: &mut SendQueue, delays: &mut DelayQueue, handle_: OpaqueHandle, stop: OpaqueHandle, log: OpaqueHandle, b: &mut Buffer<U8>, address_: DivertAddress, now: U64, delayed: Bool, bypass: Bool) -> Unit {
        let outbound: Bool=(address_.flags & U32(0x20000)) != U32(0);let skip: Bool=bypass || (address_.flags & U32(0xc0000)) != U32(0);
        let previous_retries: U64=e.statistics.escalations;let previous_learned: U64=e.statistics.learned;let previous_fallbacks: U64=e.statistics.quic_fallbacks;
        if !delayed && !skip && outbound { mark_mwo_owner(e,slice(b,0,len(b)),address_.interface_,address_.subinterface,now); }
        let mut d: Dispatch=evaluate(e,b,address_.interface_,address_.subinterface,outbound,skip,now,delayed);
        if d.delay > now {
            if delay_packet(delays,slice(b,0,len(b)),address_,d.delay,d.generation) { return; }
            d=evaluate(e,b,address_.interface_,address_.subinterface,outbound,skip,now,true);
        }
        if e.options.debug > U32(0) && e.statistics.escalations > previous_retries { log_line(log,utf8_bytes("Handshake retransmission: retry strategy escalated.")); }
        if e.options.debug > U32(0) && e.statistics.quic_fallbacks > previous_fallbacks { log_line(log,utf8_bytes("QUIC Initial unanswered: requesting TCP fallback.")); }
        if e.options.debug > U32(1) && e.statistics.learned > previous_learned { log_line(log,utf8_bytes("Server response confirmed: handshake strategy remembered.")); }
        let v: Packet=parse_packet(slice(b,0,len(b)));let batchable: Bool=v.valid && v.protocol == U8(6);
        queue_batch(q,e,handle_,stop,log,&d.fake,address_,batchable);
        if d.consumed {
            let mut target: DivertAddress=address_;if d.inbound { target.flags=target.flags & U32(0xfff5ffff); }
            let sent: Bool=queue_batch(q,e,handle_,stop,log,&d.packets,target,batchable);
            if !sent && !batchable {
                if d.inbound {
                    let key: FlowKey=FlowKey(v.source,v.destination,address_.interface_,address_.subinterface,v.source_port,v.destination_port,v.version,v.protocol);
                    let endpoint_: Endpoint=endpoint(&key,0);let failed: USize=failure_slot(&mut e.failures,&endpoint_,now,false);
                    if failed < len(&e.failures) { e.failures[failed].rejected_until=0; }
                    e.statistics.quic_fallbacks=saturating_sub(e.statistics.quic_fallbacks,U64(1));
                }else { e.statistics.transformed=saturating_sub(e.statistics.transformed,U64(1)); }
                queue_send(q,e,handle_,stop,log,slice(b,0,len(b)),address_,false,false);
            }
        }else { queue_send(q,e,handle_,stop,log,slice(b,0,len(b)),address_,d.modified,batchable); }
    }
    unsafe fn service_delayed(e: &mut Engine, q: &mut SendQueue, delays: &mut DelayQueue, handle_: OpaqueHandle, stop: OpaqueHandle, log: OpaqueHandle, now: U64, force: Bool, stopping: Bool) -> Unit {
        while true {
            let i: USize=earliest(delays);if i == len(&delays.items) || (!force && delays.items[i].due > now) { break; }
            let mut b: Buffer<U8>=bytes_copy(slice(&delays.items[i].bytes,0,len(&delays.items[i].bytes)));
            let address_: DivertAddress=delays.items[i].address_;let generation: U32=delays.items[i].generation;delays.memory=delays.memory-len(&b);delays.items[i]=zero<Delayed>();
            let v: Packet=parse_packet(slice(&b,0,len(&b)));let mut old_generation: Bool=true;
            if v.valid {
                let key: FlowKey=FlowKey(v.source,v.destination,address_.interface_,address_.subinterface,v.source_port,v.destination_port,v.version,v.protocol);
                let at: USize=flow_find(&mut e.flows,&key,now);if at < len(&e.flows) { old_generation=e.flows[at].base != generation; }
            }
            process_packet(e,q,delays,handle_,stop,log,&mut b,address_,now,true,stopping || old_generation);
        }flush_send(q,e,handle_,stop,log);
    }
    unsafe fn report(e: &Engine, log: OpaqueHandle) -> Unit {
        let mut line: Buffer<U8>=bytes_copy(utf8_bytes("Stats: recv="));append_number(&mut line,e.statistics.received);
        bytes_append(&mut line,utf8_bytes(" sent="));append_number(&mut line,e.statistics.sent);bytes_append(&mut line,utf8_bytes(" batches="));append_number(&mut line,e.statistics.batches);
        bytes_append(&mut line,utf8_bytes(" split="));append_number(&mut line,e.statistics.transformed);bytes_append(&mut line,utf8_bytes(" retries="));append_number(&mut line,e.statistics.escalations);
        bytes_append(&mut line,utf8_bytes(" learned="));append_number(&mut line,e.statistics.learned);bytes_append(&mut line,utf8_bytes(" QUIC-fallback="));append_number(&mut line,e.statistics.quic_fallbacks);
        bytes_append(&mut line,utf8_bytes(" QUIC-drop="));append_number(&mut line,e.statistics.quic_drops);bytes_append(&mut line,utf8_bytes(" send-errors="));append_number(&mut line,e.statistics.send_errors);
        bytes_append(&mut line,utf8_bytes(" recv-errors="));append_number(&mut line,e.statistics.receive_errors);bytes_append(&mut line,utf8_bytes(" inspect-peak="));append_number(&mut line,cast<U64>(e.peak));
        log_line(log,slice(&line,0,len(&line)));
    }
    unsafe fn receive_loop(e: &mut Engine, handle_: OpaqueHandle, stop: OpaqueHandle, receive: OpaqueHandle, parent: OpaqueHandle, log: OpaqueHandle, diagnostic: &mut DiagnosticLog) -> U32 {
        let mut q: SendQueue=zero<SendQueue>();q.use_batch=true;
        q.bytes=buffer<U8>(2097152);resize(&mut q.bytes,0);q.addresses=buffer<DivertAddress>(128);resize(&mut q.addresses,0);q.lengths=buffer<U32>(128);resize(&mut q.lengths,0);let mut delays: DelayQueue=zero<DelayQueue>();delays.items=buffer<Delayed>(512);
        let mut packets: Buffer<U8>=buffer<U8>(1048576);let mut addresses: Buffer<DivertAddress>=buffer<DivertAddress>(64);
        let mut stopping: Bool=false;let mut deadline: U64=0;let mut maintenance: U64=GetTickCount64();let mut statistics: U64=maintenance;
        let mut code: U32=0;let mut errors: U32=0;
        while true {
            let mut now: U64=GetTickCount64();
            if !stopping && WaitForSingleObject(stop,0) == U32(0) { stopping=true;WinDivertShutdown(handle_,1);deadline=now+U64(1500);service_delayed(e,&mut q,&mut delays,handle_,stop,log,now,true,true); }
            if now-maintenance >= U64(5000) { engine_expire(e,now,512);maintenance=now; }
            engine_tick(e,now,1024);diagnostic_drain(diagnostic,e,false);
            service_delayed(e,&mut q,&mut delays,handle_,stop,log,now,false,stopping);
            if e.options.debug > U32(0) && now-statistics >= U64(5000) { report(e,log);statistics=now; }
            let mut received: U32=0;let mut address_length: U32=cast<U32>(len(&addresses)*size_of<DivertAddress>());
            let mut ov: Overlapped=zero<Overlapped>();ov.event=receive;ResetEvent(receive);
            let mut ok: I32=WinDivertRecvEx(handle_,data(&mut packets),cast<U32>(len(&packets)),as_raw(borrowed_ptr(&mut received)),0,data(&mut addresses),as_raw(borrowed_ptr(&mut address_length)),as_raw(borrowed_ptr(&mut ov)));
            if ok == I32(0) && GetLastError() == U32(997) {
                while true {
                    now=GetTickCount64();let mut timeout: U64=100;
                    let first: USize=earliest(&delays);if first < len(&delays.items) { let mut delay: U64=0;if delays.items[first].due > now { delay=delays.items[first].due-now; }if delay < timeout { timeout=delay; } }
                    let mut count: U32=2;if handle_valid(parent) { count=3; }if stopping { count=1;timeout=0;if deadline > now { timeout=deadline-now; } }
                    let handles: Array<OpaqueHandle,3>=array<Array<OpaqueHandle,3>>(receive,stop,parent);
                    let wait: U32=WaitForMultipleObjects(count,data(&handles),0,cast<U32>(timeout));
                    if wait == U32(0) { ok=GetOverlappedResult(handle_,as_raw(borrowed_ptr(&mut ov)),as_raw(borrowed_ptr(&mut received)),1);break; }
                    if (wait == U32(1) || (wait == U32(2) && handle_valid(parent))) && !stopping {
                        if wait == U32(2) { log_line(log,utf8_bytes("GUI owner exited; draining interception and stopping."));SetEvent(stop); }
                        stopping=true;WinDivertShutdown(handle_,1);deadline=now+U64(1500);service_delayed(e,&mut q,&mut delays,handle_,stop,log,now,true,true);
                    }else if wait == U32(258) && !stopping {
                        service_delayed(e,&mut q,&mut delays,handle_,stop,log,now,false,false);engine_tick(e,now,1024);diagnostic_drain(diagnostic,e,false);
                        if now-maintenance >= U64(5000) { engine_expire(e,now,512);maintenance=now; }
                        if e.options.debug > U32(0) && now-statistics >= U64(5000) { report(e,log);statistics=now; }
                    }else {
                        CancelIoEx(handle_,as_raw(borrowed_ptr(&mut ov)));ok=GetOverlappedResult(handle_,as_raw(borrowed_ptr(&mut ov)),as_raw(borrowed_ptr(&mut received)),1);
                        if !stopping { code=3; }stopping=true;break;
                    }
                }
            }
            if ok == I32(0) {
                let error: U32=GetLastError();if stopping || error == U32(232) || error == U32(995) { break; }
                e.statistics.receive_errors=e.statistics.receive_errors+U64(1);log_error(log,utf8_bytes("WinDivert receive error: "),error);errors=errors+U32(1);if errors >= U32(8) { code=3;break; }continue;
            }
            errors=0;now=GetTickCount64();let count: USize=cast<USize>(address_length)/size_of<DivertAddress>();
            if cast<USize>(received) > len(&packets) || count == 0 || count > 64 || cast<USize>(address_length)%size_of<DivertAddress>() != 0 { code=3;break; }
            let mut offset: USize=0;
            for i in 0..count {
                let remaining: USize=cast<USize>(received)-offset;if remaining < 6 { code=3;break; }let version: U8=packets[offset] >> U8(4);let mut n: USize=0;
                if version == U8(4) { n=cast<USize>(read_be<U16>(&packets,offset+2)); }else if version == U8(6) { n=40+cast<USize>(read_be<U16>(&packets,offset+4)); }
                if n == 0 || n > remaining { code=3;break; }let mut b: Buffer<U8>=bytes_copy(slice(&packets,offset,n));
                e.statistics.received=e.statistics.received+U64(1);process_packet(e,&mut q,&mut delays,handle_,stop,log,&mut b,addresses[i],now,false,stopping);offset=offset+n;
            }
            flush_send(&mut q,e,handle_,stop,log);if code != U32(0) || offset != cast<USize>(received) { code=3;break; }
        }
        service_delayed(e,&mut q,&mut delays,handle_,stop,log,GetTickCount64(),true,true);flush_send(&mut q,e,handle_,stop,log);report(e,log);
        if q.consecutive >= U32(8) { code=3; }
        for i in 0..len(&e.flows) { if e.flows[i].used { finish_trial(e,i,3,6,GetTickCount64()); } }diagnostic_end(diagnostic,e,code);return code;
    }
    unsafe fn run_engine(o: Options) -> U32 {
        assert(size_of<DivertAddress>() == 80);assert(size_of<Overlapped>() == 32);SetConsoleOutputCP(65001);
        let log_name: Buffer<U16>=wide(utf8_bytes("NoDPI.log"));
        let log_owner: Resource<OpaqueHandle,close_handle>=resource(CreateFileW(data(&log_name),4,7,null<U8>(),4,0x80,zero<OpaqueHandle>()));let log: OpaqueHandle=*resource_get(&log_owner);
        startup_state(&o,"EngineStarting",0,false);
        if o.error { startup_state(&o,"Options",87,false);log_line(log,utf8_bytes("Invalid command line or settings. Use --help."));return 2; }
        if o.help {
            log_line(log,utf8_bytes("NoDPI DLM 2.4 / DLM 0.133.0\n--automatic (safe automatic operation) --no-automatic\n--driver-check --driver-report=FILE (read-only startup probe)\n--cloud-cdn|--no-cloud-cdn (Cloudflare / CloudFront / Azure)\n--telegram|--no-telegram (initial MTProto TCP to official Telegram networks)\n--hostlist=FILE --hostlist-exclude=FILE --ipset=FILE --ipset-exclude=FILE\n--split-mode=tcp|ip|both --split-strategy=semantic|semantic-multi|legacy\n--adaptive-retry|--no-adaptive-retry --learn-strategy|--no-learn-strategy\n--quic-mode=pass|auto|drop|ipfrag --quic-probe-ms=200..5000\n--fake-packets --fake-mode=tls-badseq|rst-ttl --auto-ttl=2,4,16\n--frag-by-sni|--no-frag-by-sni --apply-ech|--no-apply-ech\n--aggressive-frag|--no-aggressive-frag --reverse-frag --http-manip\n--first-packets=0..1024 --handshake-jitter-ms=0..250 --min-ttl=0..255\n--all-ports --game-mode --debug --debug-verbose\n--aws-json=FILE-OR-URL --aws-services=CSV --whitelist-only --blacklist=FILE\n--adaptive-strategies|--no-adaptive-strategies --strategy-profile=auto|0..7\n--server-window|--no-server-window --udp-obfuscate --diagnostic-log\n--handshake-timeout-ms=1000..30000\n--config=FILE (load [NoDPI] INI before command-line overrides)"));return 0;
        }
        if o.driver_check { return driver_health_check(&o,log); }
        let mutex_name: Buffer<U16>=wide(utf8_bytes("Local\\NoDPI.Engine.v5"));SetLastError(0);
        let mutex_owner: Resource<OpaqueHandle,close_handle>=resource(CreateMutexW(null<U8>(),0,data(&mutex_name)));let mutex_error: U32=GetLastError();
        if !handle_valid(*resource_get(&mutex_owner)) || mutex_error == U32(183) { startup_state(&o,"EngineInstanceLock",mutex_error,false);log_line(log,utf8_bytes("Another NoDPI engine is running or the instance lock is unavailable."));return 2; }
        let name: Buffer<U8>=stop_name(GetCurrentProcessId());let stop_w: Buffer<U16>=wide(slice(&name,0,len(&name)));
        let stop_owner: Resource<OpaqueHandle,close_handle>=resource(CreateEventW(null<U8>(),1,0,data(&stop_w)));let stop: OpaqueHandle=*resource_get(&stop_owner);
        let receive_owner: Resource<OpaqueHandle,close_handle>=resource(CreateEventW(null<U8>(),1,0,null<U16>()));let receive: OpaqueHandle=*resource_get(&receive_owner);
        if !handle_valid(stop) || !handle_valid(receive) { log_error(log,utf8_bytes("CreateEvent error: "),GetLastError());return 2; }
        let mut diagnostic: DiagnosticLog=diagnostic_open(&o);
        if diagnostic.failed { log_line(log,utf8_bytes("Diagnostic log could not be opened; check write access to logs/.")); }
        else if o.diagnostic { log_line(log,utf8_bytes("Detailed session log:"));log_line(log,slice(&diagnostic.path,0,len(&diagnostic.path))); }
        diagnostic_flush(&mut diagnostic,true);
        let mut p: Policy=zero<Policy>();if !prepare_policy(&o,&mut p,stop,log) { startup_failure(&o,&mut diagnostic,"PolicyConfiguration",87);return 2; }
        let mut parent_owner: Resource<OpaqueHandle,close_handle>=resource(zero<OpaqueHandle>());
        if o.parent_pid != U32(0) { parent_owner=resource(OpenProcess(0x100000,0,o.parent_pid));if !handle_valid(*resource_get(&parent_owner)) { let error: U32=GetLastError();startup_failure(&o,&mut diagnostic,"GuiOwnerWatch",error);log_error(log,utf8_bytes("Cannot watch GUI owner: "),error);return 2; } }
        let parent: OpaqueHandle=*resource_get(&parent_owner);
        if o.safe_automatic {
            let targets: Buffer<U8>=read_file(utf8_bytes("cache/auto-targets.txt"),65536);if len(&targets) > 0 { ip_rules(&mut p.quic_targets,slice(&targets,0,len(&targets))); }
            log_error(log,utf8_bytes("Target-scoped QUIC IP rules: "),cast<U32>(p.quic_targets.count));
            log_error(log,utf8_bytes("Telegram TCP CIDR rules: "),cast<U32>(p.telegram_ips.count));
        }
        let filter: Buffer<U8>=divert_filter(&o);
        let handle_: OpaqueHandle=WinDivertOpen(data(&filter),0,0,0);let open_error: U32=GetLastError();let divert_owner: Resource<OpaqueHandle,close_divert>=resource(handle_);
        if !handle_valid(handle_) { startup_failure(&o,&mut diagnostic,"WinDivertOpen",open_error);log_error(log,utf8_bytes("WinDivertOpen error: "),open_error);return 2; }
        if WinDivertSetParam(handle_,0,16384) == I32(0) || WinDivertSetParam(handle_,1,1000) == I32(0) || WinDivertSetParam(handle_,2,16777216) == I32(0) { let error: U32=GetLastError();startup_failure(&o,&mut diagnostic,"WinDivertQueue",error);log_error(log,utf8_bytes("WinDivert queue setup error: "),error);return 2; }
        log_line(log,utf8_bytes("NoDPI DLM 2.4: adaptive strategies and detailed diagnostics; engine started; 64-packet async receive / 128-packet TCP send batches."));
        let mut e: Engine=engine_new(move o,move p);
        let mut ready_name: Buffer<U8>=bytes_copy(utf8_bytes("Local\\NoDPI.Ready."));append_number(&mut ready_name,cast<U64>(GetCurrentProcessId()));let ready_w: Buffer<U16>=wide(slice(&ready_name,0,len(&ready_name)));
        let ready_owner: Resource<OpaqueHandle,close_handle>=resource(CreateEventW(null<U8>(),1,1,data(&ready_w)));if !handle_valid(*resource_get(&ready_owner)) { let error: U32=GetLastError();startup_failure(&e.options,&mut diagnostic,"ReadyEvent",error);return 2; }startup_state(&e.options,"EngineReady",0,true);SetConsoleCtrlHandler(fn_ptr(console_control),1);
        let code: U32=receive_loop(&mut e,handle_,stop,receive,parent,log,&mut diagnostic);SetConsoleCtrlHandler(fn_ptr(console_control),0);log_line(log,utf8_bytes("NoDPI DLM: engine stopped."));return code;
    }
    unsafe fn engine_main() -> Unit {
        if !set_application_directory() { print("Cannot select application directory");ExitProcess(2);return; }
        let args: Buffer<Buffer<U8>>=command_arguments();let mut o: Options=default_options();
        for i in 1..len(&args) {
            let a: Slice<U8>=slice(&args[i],0,len(&args[i]));if bytes_at(a,0,utf8_bytes("--config=")) {
                let b: Buffer<U8>=read_file(slice(&a,9,len(a)-9),1048576);if len(&b) == 0 || !load_settings(&mut o,slice(&b,0,len(&b))) { o.error=true; }
            }
        }
        for i in 1..len(&args) { let a: Slice<U8>=slice(&args[i],0,len(&args[i]));if !bytes_at(a,0,utf8_bytes("--config=")) && !option(&mut o,a) { o.error=true;print("Invalid command-line argument"); } }
        if len(&o.paths[7]) > 0 { let text: Buffer<U8>=read_file(slice(&o.paths[7],0,len(&o.paths[7])),65536);if !profile_gene(slice(&text,0,len(&text)),&mut o.gene,&mut o.gene_target) { print("Invalid strategy file; no interception started");ExitProcess(2);return; }o.gene_mode=true; }
        finish_options(&mut o);let code: U32=run_engine(move o);ExitProcess(code);
    }
}
