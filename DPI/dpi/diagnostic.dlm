module dpi.diagnostic
import dpi.engine
import dpi.win32

native {
    record SystemTime repr(C) { year: U16; month: U16; weekday: U16; day: U16; hour: U16; minute: U16; second: U16; millisecond: U16; }
    extern "C" fn GetSystemTime(time: Ptr<SystemTime>) -> Unit from "Win32";
    extern "C" fn CreateDirectoryW(path: Ptr<U16>, security: Ptr<U8>) -> I32 from "Win32";
    extern "C" fn GetActiveProcessorCount(group: U16) -> U32 from "Win32";
    record DiagnosticLog repr(C) { owner: Resource<OpaqueHandle,close_handle>; buffer_: Buffer<U8>; bytes: U64; last_flush: U64; failed: Bool; limited: Bool; path: Buffer<U8>; }
    fn json_text(b: &mut Buffer<U8>, text: Slice<U8>) -> Unit {
        push(b,U8(34));for i in 0..len(text) { let c: U8=text[i];if c == U8(34) || c == U8(92) { push(b,U8(92));push(b,c); }
            else if c >= U8(32) { push(b,c); }else { push(b,U8(32)); } }push(b,U8(34));
    }
    fn json_uint(b: &mut Buffer<U8>, name: Slice<U8>, value: U64) -> Unit { push(b,U8(44));json_text(b,name);push(b,U8(58));append_number(b,value); }
    fn json_bool(b: &mut Buffer<U8>, name: Slice<U8>, value: Bool) -> Unit { push(b,U8(44));json_text(b,name);push(b,U8(58));if value { bytes_append(b,utf8_bytes("true")); }else { bytes_append(b,utf8_bytes("false")); } }
    fn hex_word(b: &mut Buffer<U8>, word: U16) -> Unit {
        let digits: Slice<U8>=utf8_bytes("0123456789abcdef");let mut started: Bool=false;
        for i in 0..4 { let shift: U16=cast<U16>((3-i)*4);let d: USize=cast<USize>((word >> shift) & U16(15));if d != 0 || started || i == 3 { push(b,digits[d]);started=true; } }
    }
    fn ip_text(address_: &Array<U8,16>, version: U8) -> Buffer<U8> {
        let mut b: Buffer<U8>=buffer<U8>(0);if version == U8(4) { for i in 0..4 { if i > 0 { push(&mut b,U8(46)); }append_number(&mut b,cast<U64>(address_[i])); } }
        else { for i in 0..8 { if i > 0 { push(&mut b,U8(58)); }hex_word(&mut b,read_be<U16>(address_,i*2)); } }return move b;
    }
    fn event_name(kind: U32) -> Utf8 {
        if kind == U32(1) { return "connection_strategy"; }if kind == U32(2) { return "syn_ack"; }if kind == U32(3) { return "client_hello_policy_allowed"; }
        if kind == U32(4) { return "packet_transform"; }if kind == U32(5) { return "server_bytes"; }if kind == U32(7) { return "peer_reset"; }
        if kind == U32(8) { return "progress_timeout"; }if kind == U32(9) { return "tls_midrecord_stall_suspected"; }if kind == U32(10) { return "peer_fin"; }
        if kind == U32(11) { return "trial_outcome"; }if kind == U32(12) { return "quic_action"; }if kind == U32(13) { return "policy_excluded"; }
        if kind == U32(16) { return "server_window_released"; }if kind == U32(18) { return "udp_signature_cover"; }if kind == U32(19) { return "tcp_options"; }if kind == U32(22) { return "client_closed"; }if kind == U32(23) { return "udp_peer_progress"; }if kind == U32(24) { return "automatic_scope"; }if kind == U32(25) { return "quic_unknown_target_pass"; }if kind == U32(26) { return "telegram_tcp_transform"; }if kind == U32(27) { return "telegram_transport"; }if kind == U32(28) { return "telegram_server_data"; }if kind == U32(29) { return "mwo_process_scope"; }if kind == U32(30) { return "cloud_cdn_scope"; }return "event";
    }
    fn reason_name(reason: U32) -> Utf8 {
        if reason == U32(1) { return "no_server_hello"; }if reason == U32(2) { return "no_syn_ack_inconclusive"; }if reason == U32(3) { return "no_inspectable_request"; }
        if reason == U32(4) { return "transport_progress_not_application_success"; }if reason == U32(5) { return "midrecord_stall_observed"; }
        if reason == U32(6) { return "observation_expired_inconclusive"; }if reason == U32(7) { return "early_reset"; }if reason == U32(8) { return "peer_closed"; }
        if reason == U32(9) { return "new_syn_generation"; }if reason == U32(10) { return "cache_evicted"; }if reason == U32(11) { return "policy_skipped"; }if reason == U32(13) { return "no_post_hello_progress"; }if reason == U32(14) { return "client_close_inconclusive"; }return "none";
    }
    fn trace_json(t: &TraceEvent) -> Buffer<U8> {
        let mut b: Buffer<U8>=bytes_copy(utf8_bytes("{\"v\":2,\"event\":"));json_text(&mut b,utf8_bytes(event_name(t.kind)));
        json_uint(&mut b,utf8_bytes("mono_ms"),t.time);json_uint(&mut b,utf8_bytes("flow"),key_hash(&t.key));
        let ip: Buffer<U8>=ip_text(&t.key.destination,t.key.version);bytes_append(&mut b,utf8_bytes(",\"peer_ip\":"));json_text(&mut b,slice(&ip,0,len(&ip)));
        json_uint(&mut b,utf8_bytes("ip_protocol"),cast<U64>(t.key.protocol));json_uint(&mut b,utf8_bytes("peer_port"),cast<U64>(t.key.destination_port));json_uint(&mut b,utf8_bytes("local_port"),cast<U64>(t.key.source_port));
        json_uint(&mut b,utf8_bytes("interface"),cast<U64>(t.key.interface_));json_uint(&mut b,utf8_bytes("subinterface"),cast<U64>(t.key.subinterface));json_uint(&mut b,utf8_bytes("ip_version"),cast<U64>(t.key.version));
        bytes_append(&mut b,utf8_bytes(",\"host\":"));json_text(&mut b,slice(&t.host.bytes,0,t.host.length));json_uint(&mut b,utf8_bytes("arm"),cast<U64>(t.arm));
        bytes_append(&mut b,utf8_bytes(",\"strategy\":"));json_text(&mut b,utf8_bytes(arm_name(t.arm)));bytes_append(&mut b,utf8_bytes(",\"reason\":"));if t.kind == U32(24) { if t.reason == U32(1) { json_text(&mut b,utf8_bytes("protected_dns_or_unknown_name_pass")); }else if t.reason == U32(2) { json_text(&mut b,utf8_bytes("direct_observation_or_circuit_breaker_pass")); }else { json_text(&mut b,utf8_bytes("targeted_tcp_candidate")); } }
        else if t.kind == U32(29) { json_text(&mut b,utf8_bytes("windows_tcp_owner_confirmed")); }else if t.kind == U32(30) { json_text(&mut b,utf8_bytes("initial_tcp_only_application_success_unverified")); }
        else if t.kind == U32(27) { json_text(&mut b,utf8_bytes("ip_scoped_startup")); }
        else if t.kind == U32(12) { if t.reason == U32(1) { json_text(&mut b,utf8_bytes("quic_fallback")); }else if t.reason == U32(2) { json_text(&mut b,utf8_bytes("quic_initial_drop")); }else { json_text(&mut b,utf8_bytes("quic_ip_fragments")); } }
        else { json_text(&mut b,utf8_bytes(reason_name(t.reason))); }
        if t.kind == U32(4) || t.kind == U32(26) { json_uint(&mut b,utf8_bytes("segments"),t.a);if t.kind == U32(26) { json_uint(&mut b,utf8_bytes("identical_overlap_bytes"),t.b); }else { json_uint(&mut b,utf8_bytes("covers"),t.b); }json_uint(&mut b,utf8_bytes("payload_bytes"),t.c); }
        else if t.kind == U32(27) { bytes_append(&mut b,utf8_bytes(",\"transport\":"));json_text(&mut b,utf8_bytes(telegram_transport_name(t.reason))); }
        else if t.kind == U32(29) { json_uint(&mut b,utf8_bytes("process_id"),t.a);json_uint(&mut b,utf8_bytes("process_kind_1_client_2_portal"),t.b); }
        else if t.kind == U32(30) { bytes_append(&mut b,utf8_bytes(",\"provider\":"));json_text(&mut b,utf8_bytes(cloud_provider_name(cast<U32>(t.a))));json_uint(&mut b,utf8_bytes("mwo_process_id_0_domain_or_ip_scope"),t.b); }
        else if t.kind == U32(18) { json_uint(&mut b,utf8_bytes("covers"),t.a);json_uint(&mut b,utf8_bytes("payload_bytes"),t.b); }
        else if t.kind == U32(12) { json_uint(&mut b,utf8_bytes("quic_mode"),cast<U64>(t.reason));json_uint(&mut b,utf8_bytes("segments"),t.a); }
        else if t.kind == U32(19) { json_uint(&mut b,utf8_bytes("window"),t.a);json_uint(&mut b,utf8_bytes("window_scale_255_absent"),t.b);json_uint(&mut b,utf8_bytes("mss"),t.c); }
        else { json_uint(&mut b,utf8_bytes("rx_bytes"),t.a);json_uint(&mut b,utf8_bytes("client_hello_sent_bytes"),t.b);json_uint(&mut b,utf8_bytes("tls_record_remaining"),t.c); }
        bytes_append(&mut b,utf8_bytes("}\n"));return move b;
    }
    unsafe fn diagnostic_open(o: &Options) -> DiagnosticLog {
        let mut d: DiagnosticLog=DiagnosticLog(resource(zero<OpaqueHandle>()),buffer<U8>(65536),0,GetTickCount64(),false,false,buffer<U8>(0));resize(&mut d.buffer_,0);
        if !o.diagnostic { return move d; }let dir: Buffer<U16>=wide(utf8_bytes("logs"));CreateDirectoryW(data(&dir),null<U8>());
        let mut time: SystemTime=zero<SystemTime>();GetSystemTime(as_raw(borrowed_ptr(&mut time)));
        d.path=bytes_copy(utf8_bytes("logs/engine-"));append_number(&mut d.path,cast<U64>(time.year));push(&mut d.path,U8(45));append_number(&mut d.path,cast<U64>(time.month));push(&mut d.path,U8(45));append_number(&mut d.path,cast<U64>(time.day));push(&mut d.path,U8(45));append_number(&mut d.path,cast<U64>(time.hour));push(&mut d.path,U8(45));append_number(&mut d.path,cast<U64>(time.minute));push(&mut d.path,U8(45));append_number(&mut d.path,cast<U64>(time.second));push(&mut d.path,U8(45));append_number(&mut d.path,cast<U64>(GetCurrentProcessId()));bytes_append(&mut d.path,utf8_bytes(".jsonl"));
        let name: Buffer<U16>=wide(slice(&d.path,0,len(&d.path)));d.owner=resource(CreateFileW(data(&name),0x40000000,7,null<U8>(),2,0x80,zero<OpaqueHandle>()));
        if !handle_valid(*resource_get(&d.owner)) { d.failed=true;return move d; }
        let mut b: Buffer<U8>=bytes_copy(utf8_bytes("{\"v\":2,\"event\":\"session_start\",\"build\":\"DPI-DLM-2.4-20261119\",\"dlm\":\"0.133.0\",\"clock\":\"UTC\""));
        json_uint(&mut b,utf8_bytes("year"),cast<U64>(time.year));json_uint(&mut b,utf8_bytes("month"),cast<U64>(time.month));json_uint(&mut b,utf8_bytes("day"),cast<U64>(time.day));json_uint(&mut b,utf8_bytes("hour"),cast<U64>(time.hour));json_uint(&mut b,utf8_bytes("minute"),cast<U64>(time.minute));json_uint(&mut b,utf8_bytes("second"),cast<U64>(time.second));
        json_uint(&mut b,utf8_bytes("pid"),cast<U64>(GetCurrentProcessId()));json_uint(&mut b,utf8_bytes("cpus"),cast<U64>(GetActiveProcessorCount(65535)));json_uint(&mut b,utf8_bytes("mono_ms"),GetTickCount64());
        json_bool(&mut b,utf8_bytes("cloud_cdn_enabled"),o.cloud_cdn);json_bool(&mut b,utf8_bytes("telegram_enabled"),o.telegram);json_bool(&mut b,utf8_bytes("adaptive"),o.auto_strategy);json_bool(&mut b,utf8_bytes("safe_automatic"),o.safe_automatic);json_bool(&mut b,utf8_bytes("gene_mode"),o.gene_mode);json_uint(&mut b,utf8_bytes("gene_target"),cast<U64>(o.gene_target));bytes_append(&mut b,utf8_bytes(",\"trial_tag\":"));json_text(&mut b,slice(&o.trial_tag,0,len(&o.trial_tag)));if o.gene_mode { let gene: Buffer<U8>=gene_text(&o.gene);bytes_append(&mut b,utf8_bytes(",\"gene\":"));json_text(&mut b,slice(&gene,0,len(&gene))); }json_uint(&mut b,utf8_bytes("forced_arm_8_auto"),cast<U64>(o.forced_arm));json_bool(&mut b,utf8_bytes("window_candidates"),o.server_window);
        json_uint(&mut b,utf8_bytes("quic_mode"),cast<U64>(o.quic));json_bool(&mut b,utf8_bytes("stun_covers"),o.udp_obfuscate);json_bool(&mut b,utf8_bytes("ech"),o.ech);
        json_uint(&mut b,utf8_bytes("timeout_ms"),cast<U64>(o.handshake_timeout));json_bool(&mut b,utf8_bytes("host_include_configured"),len(&o.paths[0]) > 0);json_bool(&mut b,utf8_bytes("ip_include_configured"),len(&o.paths[2]) > 0);
        bytes_append(&mut b,utf8_bytes(",\"application_success\":\"requires_probe_and_manual_media_voice_test\"}\n"));bytes_append(&mut d.buffer_,slice(&b,0,len(&b)));return move d;
    }
    unsafe fn diagnostic_flush(d: &mut DiagnosticLog, force: Bool) -> Unit {
        if d.failed || !handle_valid(*resource_get(&d.owner)) { return; }let now: U64=GetTickCount64();
        if !force && len(&d.buffer_) < 65536 && now-d.last_flush < U64(250) { return; }
        if len(&d.buffer_) > 0 {
            if !write_all(*resource_get(&d.owner),slice(&d.buffer_,0,len(&d.buffer_))) { d.failed=true;return; }d.bytes=d.bytes+cast<U64>(len(&d.buffer_));resize(&mut d.buffer_,0);
        }d.last_flush=now;
    }
    unsafe fn diagnostic_drain(d: &mut DiagnosticLog, e: &mut Engine, force: Bool) -> Unit {
        if !d.failed && handle_valid(*resource_get(&d.owner)) && !d.limited {
            for i in 0..len(&e.events) {
                let b: Buffer<U8>=trace_json(&e.events[i]);bytes_append(&mut d.buffer_,slice(&b,0,len(&b)));
                if d.bytes+cast<U64>(len(&d.buffer_)) >= U64(8388608) { bytes_append(&mut d.buffer_,utf8_bytes("{\"event\":\"log_size_limit\",\"limit_bytes\":8388608}\n"));d.limited=true;break; }
            }
        }resize(&mut e.events,0);diagnostic_flush(d,force);
    }
    unsafe fn diagnostic_end(d: &mut DiagnosticLog, e: &mut Engine, code: U32) -> Unit {
        diagnostic_drain(d,e,true);let mut b: Buffer<U8>=bytes_copy(utf8_bytes("{\"v\":2,\"event\":\"session_end\""));
        json_uint(&mut b,utf8_bytes("exit_code"),cast<U64>(code));json_uint(&mut b,utf8_bytes("received"),e.statistics.received);json_uint(&mut b,utf8_bytes("sent"),e.statistics.sent);json_uint(&mut b,utf8_bytes("transformed"),e.statistics.transformed);
        json_uint(&mut b,utf8_bytes("send_errors"),e.statistics.send_errors);json_uint(&mut b,utf8_bytes("receive_errors"),e.statistics.receive_errors);json_uint(&mut b,utf8_bytes("dropped_events"),e.dropped_events);json_bool(&mut b,utf8_bytes("log_limited"),d.limited);bytes_append(&mut b,utf8_bytes("}\n"));bytes_append(&mut d.buffer_,slice(&b,0,len(&b)));diagnostic_flush(d,true);
    }
}
