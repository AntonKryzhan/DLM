module dpi.engine
import dpi.telegram
import dpi.cloud

native {
    record Statistics repr(C) { received: U64; sent: U64; batches: U64; transformed: U64; escalations: U64; learned: U64; quic_fallbacks: U64; quic_drops: U64; send_errors: U64; receive_errors: U64; }
    record QuicFlow repr(C) { key: FlowKey; used: Bool; touched: U64; initial: QuicInitial; started: U64; attempts: U8; replied: Bool; rx_bytes: U64; last_report: U64; }
    record QuicFailure repr(C) { key: Endpoint; used: Bool; touched: U64; rejected_until: U64; }
    record TraceEvent repr(C) { kind: U32; time: U64; key: FlowKey; host: Host; arm: U32; reason: U32; a: U64; b: U64; c: U64; }
    record Engine repr(C) {
        options: Options; policy: Policy; flows: Buffer<Flow>; strategies: Buffer<Strategy>;
        quic: Buffer<QuicFlow>; failures: Buffer<QuicFailure>; statistics: Statistics;
        memory: USize; peak: USize; expiration_cursor: USize; identification: U16;
        evidence: Buffer<StrategyEvidence>; events: Buffer<TraceEvent>; dropped_events: U64; sweep_cursor: USize; last_sweep: U64; automatic_hosts: Buffer<AutomaticHost>;
    }
    fn engine_new(options: Options, policy: Policy) -> Engine {
        let mut e: Engine=Engine(move options,move policy,buffer<Flow>(16384),buffer<Strategy>(4096),buffer<QuicFlow>(4096),buffer<QuicFailure>(1024),zero<Statistics>(),0,0,0,1,buffer<StrategyEvidence>(4096),buffer<TraceEvent>(4096),0,0,0,buffer<AutomaticHost>(1024));
        resize(&mut e.events,0);return move e;
    }
    fn trace_event(e: &mut Engine, kind: U32, now: U64, key: FlowKey, host: Host, arm: U32, reason: U32, a: U64, b: U64, c: U64) -> Unit {
        if !e.options.diagnostic { return; }if len(&e.events) >= 4096 { e.dropped_events=e.dropped_events+U64(1);return; }
        push(&mut e.events,TraceEvent(kind,now,key,host,arm,reason,a,b,c));
    }
    fn trace_flow(e: &mut Engine, index: USize, kind: U32, now: U64, reason: U32) -> Unit {
        let key: FlowKey=e.flows[index].key;let host: Host=e.flows[index].host;let arm: U32=e.flows[index].arm;
        let incoming: U64=e.flows[index].inbound_bytes;let highest: U64=cast<U64>(e.flows[index].highest);let remaining: U64=cast<U64>(e.flows[index].record_remaining);
        trace_event(e,kind,now,key,host,arm,reason,incoming,highest,remaining);
    }
    fn trial_endpoint(e: &Engine, index: USize) -> Endpoint {
        let mut host: U64=0;if e.options.safe_automatic { host=host_hash(&e.flows[index].host); }return endpoint(&e.flows[index].key,host);
    }
    // result 1 = observed transport progress, 2 = observed failure,
    // 3 = inconclusive. None of these is a claim that a video has played.
    fn finish_trial(e: &mut Engine, index: USize, result: U32, reason: U32, now: U64) -> Unit {
        if !e.flows[index].trial_pending || e.flows[index].trial_finished { return; }
        let key: Endpoint=trial_endpoint(e,index);let delay: U64=now-e.flows[index].trial_time;
        evidence_end(&mut e.evidence,&key,e.flows[index].arm,result,cast<U32>(min_size(cast<USize>(delay),30000)),now);
        e.flows[index].trial_finished=true;e.flows[index].trial_result=result;trace_flow(e,index,11,now,reason);
        if e.options.safe_automatic { automatic_feedback(&mut e.automatic_hosts,&e.flows[index].host,e.flows[index].arm,result,reason,now); }
    }
    fn open_trial(e: &mut Engine, index: USize, now: U64, syn: Bool) -> Unit {
        if !e.options.auto_strategy { e.flows[index].arm=9;return; }if e.flows[index].arm_chosen || (e.options.safe_automatic && syn) { return; }
        let key: Endpoint=trial_endpoint(e,index);
        let lists: Bool=len(&e.policy.hosts.domains) > 0 || len(&e.policy.excluded_hosts.domains) > 0;
        let allowed_ip: Bool=policy_udp(&e.policy,&key.address_,key.version,e.options.whitelist);
        let allow_window: Bool=!e.options.safe_automatic && syn && e.options.server_window && !lists && allowed_ip && key.port == U16(443);
        let mut forced: U32=e.options.forced_arm;let mut candidate_version: U8=key.version;
        if e.options.safe_automatic {
            // Automatic operation uses only TCP candidates; fragment/window/UDP
            // experiments remain available through the explicit console interface.
            candidate_version=6;if flow_gene(&e.options,&e.flows[index]) { forced=1; }
            else if e.flows[index].cloud_scope { let slot: USize=evidence_slot(&mut e.evidence,&key,now);forced=cloud_arm(&e.evidence[slot]);if e.flows[index].host.length > 0 { let host_slot: USize=automatic_slot(&mut e.automatic_hosts,&e.flows[index].host,now);if e.automatic_hosts[host_slot].pause_until > now { forced=0; } } }
            else if !automatic_candidate(&mut e.automatic_hosts,&e.flows[index].host,&e.policy,now) { forced=0; }
        }
        e.flows[index].arm=evidence_begin(&mut e.evidence,&key,now,candidate_version,allow_window,forced);
        e.flows[index].arm_chosen=true;e.flows[index].trial_pending=true;e.flows[index].trial_time=now;
        e.flows[index].record_certain=true;
        if allow_window && (e.flows[index].arm == U32(5) || e.flows[index].arm == U32(6)) { e.flows[index].window_until=now+U64(2000); }
        trace_flow(e,index,1,now,0);
    }
    fn engine_create_flow(e: &mut Engine, key: &FlowKey, now: U64) -> USize {
        let base: USize=cast<USize>(key_hash(key) & U64(4095))*4;let mut candidate: USize=base;let mut oldest: U64=18446744073709551615;
        for way in 0..4 { let i: USize=base+way;if !e.flows[i].used || now-e.flows[i].touched > U64(180000) { candidate=i;break; }if e.flows[i].touched < oldest { candidate=i;oldest=e.flows[i].touched; } }
        if e.flows[candidate].used { finish_trial(e,candidate,3,10,now); }
        return flow_create(&mut e.flows,key,now,&mut e.memory);
    }
    fn engine_expire(e: &mut Engine, now: U64, budget: USize) -> Unit {
        for count in 0..min_size(budget,len(&e.flows)) {
            let i: USize=e.expiration_cursor;e.expiration_cursor=(i+1)%len(&e.flows);
            if e.flows[i].used && now-e.flows[i].touched > U64(180000) { finish_trial(e,i,3,10,now);discard_stream(&mut e.flows[i],&mut e.memory);e.flows[i]=zero<Flow>(); }
        }
    }
    fn observe_transport(f: &mut Flow, v: &Packet, b: Slice<U8>, now: U64) -> Unit {
        if (v.flags & U8(18)) == U8(18) { f.syn_reply=true;f.inbound_end=wrapping_add(v.sequence,U32(1));f.inbound_known=true; }
        if v.payload_length == 0 { return; }let mut skip: USize=0;
        if !f.inbound_known { f.inbound_end=v.sequence;f.inbound_known=true; }
        if sequence_before(v.sequence,f.inbound_end) { skip=min_size(v.payload_length,cast<USize>(wrapping_sub(f.inbound_end,v.sequence))); }
        else if v.sequence != f.inbound_end { f.record_certain=false; }
        let fresh: USize=v.payload_length-skip;if fresh == 0 { return; }
        f.inbound_bytes=f.inbound_bytes+cast<U64>(fresh);f.inbound_end=wrapping_add(v.sequence,cast<U32>(v.payload_length));f.last_rx=now;
        if !f.tls || !f.record_certain { return; }let mut p: USize=v.payload+skip;
        while p < v.length {
            if f.record_remaining > 0 { let n: USize=min_size(f.record_remaining,v.length-p);f.record_remaining=f.record_remaining-n;p=p+n;continue; }
            while f.record_have < 5 && p < v.length { f.record_header[f.record_have]=b[p];f.record_have=f.record_have+1;p=p+1; }
            if f.record_have < 5 { break; }
            let kind: U8=f.record_header[0];let length: USize=cast<USize>(read_be<U16>(&f.record_header,3));
            if kind < U8(20) || kind > U8(23) || f.record_header[1] != U8(3) || length > 18432 { f.record_certain=false;return; }
            if kind == U8(23) { f.tls_app=true; }f.record_remaining=length;f.record_have=0;
        }
    }
    fn engine_tick(e: &mut Engine, now: U64, budget: USize) -> Unit {
        if now-e.last_sweep < U64(100) { return; }e.last_sweep=now;
        for count in 0..min_size(budget,len(&e.flows)) {
            let i: USize=e.sweep_cursor;e.sweep_cursor=(i+1)%len(&e.flows);if !e.flows[i].used { continue; }
            if e.options.safe_automatic && !e.flows[i].trial_pending && e.flows[i].syn && !e.flows[i].syn_reply && now-e.flows[i].started >= cast<U64>(e.options.handshake_timeout) && (e.flows[i].trace_mask & U32(256)) == U32(0) {
                e.flows[i].trace_mask=e.flows[i].trace_mask | U32(256);trace_flow(e,i,8,now,2);
            }
            if !e.flows[i].trial_pending { continue; }
            let age: U64=now-e.flows[i].trial_time;
            if !e.flows[i].trial_finished && age >= cast<U64>(e.options.handshake_timeout) {
                if e.flows[i].trial_started && (!e.flows[i].progress || (e.flows[i].tls && !e.flows[i].client_after)) { let mut reason: U32=1;if e.flows[i].progress { reason=13; }trace_flow(e,i,8,now,reason);finish_trial(e,i,2,reason,now); }
                else if !e.flows[i].syn_reply && e.flows[i].syn { trace_flow(e,i,8,now,2);finish_trial(e,i,3,2,now); }
                else if !e.flows[i].trial_started { finish_trial(e,i,3,3,now); }
            }
            if e.flows[i].trial_started && e.flows[i].inbound_bytes >= U64(16384) && e.flows[i].tls_app && e.flows[i].client_after && !e.flows[i].trial_finished {
                finish_trial(e,i,1,4,now);
            }
            if !e.flows[i].trial_finished && e.flows[i].progress && e.flows[i].inbound_bytes > U64(0) && (!e.flows[i].tls || (e.flows[i].client_after && e.flows[i].tls_app && e.flows[i].record_remaining == 0 && now-e.flows[i].last_rx >= U64(500))) { finish_trial(e,i,1,4,now); }
            if e.flows[i].progress && e.flows[i].inbound_bytes >= U64(1024) && e.flows[i].record_certain && e.flows[i].record_remaining > 0 && now-e.flows[i].last_rx >= U64(5000) && (e.flows[i].trace_mask & U32(128)) == U32(0) {
                e.flows[i].trace_mask=e.flows[i].trace_mask | U32(128);trace_flow(e,i,9,now,5);
                if !e.flows[i].trial_finished { finish_trial(e,i,2,5,now); }
                else if e.flows[i].trial_result == U32(1) {
                    let key: Endpoint=trial_endpoint(e,i);let slot: USize=evidence_slot(&mut e.evidence,&key,now);let arm: USize=cast<USize>(e.flows[i].arm);
                    e.evidence[slot].arms[arm].successes=saturating_sub(e.evidence[slot].arms[arm].successes,U32(1));e.evidence[slot].arms[arm].failures=saturating_add(e.evidence[slot].arms[arm].failures,U32(1));
                    e.flows[i].trial_result=2;trace_flow(e,i,11,now,5);
                }
            }
            if !e.flows[i].trial_finished && age >= U64(30000) { finish_trial(e,i,3,6,now); }
        }
    }
    fn quic_slot(items: &mut Buffer<QuicFlow>, key: &FlowKey, now: U64, create: Bool) -> USize {
        let base: USize=cast<USize>(key_hash(key) & U64(1023))*4;let mut choice: USize=base;let mut oldest: U64=18446744073709551615;
        for way in 0..4 {
            let i: USize=base+way;
            if items[i].used && key_equal(&items[i].key,key) && now-items[i].touched <= U64(30000) { items[i].touched=now;return i; }
            if !items[i].used || items[i].touched < oldest { oldest=items[i].touched;choice=i; }
        }
        if !create { return len(items); }items[choice]=zero<QuicFlow>();items[choice].key=*key;items[choice].used=true;items[choice].touched=now;return choice;
    }
    fn failure_slot(items: &mut Buffer<QuicFailure>, key: &Endpoint, now: U64, create: Bool) -> USize {
        let base: USize=cast<USize>(endpoint_hash(key) & U64(255))*4;let mut choice: USize=base;let mut oldest: U64=18446744073709551615;
        for way in 0..4 {
            let i: USize=base+way;if items[i].used && endpoint_equal(&items[i].key,key) && now-items[i].touched <= U64(30000) { items[i].touched=now;return i; }
            if !items[i].used || items[i].touched < oldest { oldest=items[i].touched;choice=i; }
        }
        if !create { return len(items); }items[choice]=QuicFailure(*key,true,now,0);return choice;
    }
    fn quic_same(a: &QuicInitial, b: &QuicInitial) -> Bool {
        if a.version != b.version || a.dcid_length != b.dcid_length || a.scid_length != b.scid_length { return false; }
        for i in 0..20 { if a.dcid[i] != b.dcid[i] || a.scid[i] != b.scid[i] { return false; } }return true;
    }
    fn quic_reply(f: &QuicFlow, b: Slice<U8>) -> Bool {
        if f.attempts == U8(0) || len(b) < 7 || (b[0] & U8(128)) == U8(0) || (be32(b,1) != f.initial.version && be32(b,1) != U32(0)) ||
            cast<USize>(b[5]) != f.initial.scid_length || b[5] > U8(20) || len(b) < cast<USize>(b[5])+7 { return false; }
        let n: USize=cast<USize>(b[5]);for i in 0..n { if f.initial.scid[i] != b[6+i] { return false; } }
        let count: USize=cast<USize>(b[6+n]);return count <= 20 && count <= len(b)-7-n;
    }
    record Dispatch repr(C) { packets: Batch; fake: Batch; consumed: Bool; inbound: Bool; modified: Bool; delay: U64; generation: U32; }
    fn evaluate_quic(e: &mut Engine, v: &Packet, key: &FlowKey, b: Slice<U8>, outbound: Bool, now: U64) -> Dispatch {
        let mut d: Dispatch=zero<Dispatch>();if e.options.quic == U32(0) || e.options.game || v.transport_header == 0 { return move d; }
        let payload: Slice<U8>=slice(&b,v.payload,v.payload_length);
        if !outbound {
            if e.options.quic == U32(1) {
                let reverse: FlowKey=reverse_key(key);let slot: USize=quic_slot(&mut e.quic,&reverse,now,false);
                if slot < len(&e.quic) && quic_reply(&e.quic[slot],payload) {
                    e.quic[slot].replied=true;let endpoint_: Endpoint=endpoint(&reverse,0);let failed: USize=failure_slot(&mut e.failures,&endpoint_,now,false);
                    if failed < len(&e.failures) { e.failures[failed].rejected_until=0; }
                }
            }return move d;
        }
        if !v.transformable || v.destination_port != U16(443) || !policy_udp(&e.policy,&v.destination,v.version,e.options.whitelist) { return move d; }
        let initial: QuicInitial=inspect_quic(payload,true);if !initial.valid { return move d; }
        if e.options.quic == U32(2) { e.statistics.quic_drops=e.statistics.quic_drops+U64(1);d.consumed=true;return move d; }
        if e.options.quic == U32(3) {
            d.packets=ip_fragments(b,v,328,968,e.options.reverse,e.identification);e.identification=wrapping_add(e.identification,U16(1));
            if d.packets.count > 0 { d.consumed=true;e.statistics.transformed=e.statistics.transformed+U64(1); }return move d;
        }
        let slot: USize=quic_slot(&mut e.quic,key,now,true);
        if e.quic[slot].attempts == U8(0) || !quic_same(&e.quic[slot].initial,&initial) {
            e.quic[slot].initial=initial;e.quic[slot].started=now;e.quic[slot].attempts=0;e.quic[slot].replied=false;
        }e.quic[slot].attempts=saturating_add(e.quic[slot].attempts,U8(1));
        let endpoint_: Endpoint=endpoint(key,0);let failed: USize=failure_slot(&mut e.failures,&endpoint_,now,false);let mut cached: Bool=false;
        if failed < len(&e.failures) { cached=e.failures[failed].rejected_until > now; }
        if !e.quic[slot].replied && (cached || (e.quic[slot].attempts >= U8(2) && now-e.quic[slot].started >= cast<U64>(e.options.probe_ms))) {
            d.packets=udp_unreachable(b,v);if d.packets.count > 0 {
                let at: USize=failure_slot(&mut e.failures,&endpoint_,now,true);e.failures[at].rejected_until=now+U64(30000);
                d.consumed=true;d.inbound=true;e.statistics.quic_fallbacks=e.statistics.quic_fallbacks+U64(1);
            }
        }return move d;
    }
    fn transform_packet(b: Slice<U8>, v: &Packet, p: &Plan, o: &Options, reverse: Bool, id: U16) -> Batch {
        let mut result: Batch=zero<Batch>();if p.count == 0 { return move result; }
        if o.split_mode != U32(1) || v.version == U8(6) { result=tcp_segments(b,v,p,reverse);if result.count > 0 { return move result; } }
        if v.version == U8(4) && o.split_mode != U32(0) {
            let first: U32=(cast<U32>(v.transport_header)+p.offsets[0]+U32(7)) & U32(0xfffffff8);
            let mut second: U32=cast<U32>(v.transport_header+v.payload_length/2);if p.count > 1 { second=cast<U32>(v.transport_header)+p.offsets[1]; }
            second=second & U32(0xfffffff8);if second <= first { second=first+U32(8); }return ip_fragments(b,v,first,second,reverse,id);
        }return move result;
    }
    fn adaptive_transform(b: Slice<U8>, v: &Packet, plan: &Plan, f: &Flow, o: &Options, id: U16) -> Batch {
        if flow_gene(o,f) {
            let mut chosen: Plan=*plan;if o.gene.cut1 > U32(0) || o.gene.cut2 > U32(0) { chosen=zero<Plan>();add_cut(&mut chosen,o.gene.cut1,v.payload_length);add_cut(&mut chosen,o.gene.cut2,v.payload_length);if chosen.count == 0 { chosen=*plan; }sort_plan(&mut chosen); }
            if o.gene.overlap > U32(0) { return overlap_segments(b,v,&chosen,o.gene.reverse != U32(0),cast<USize>(o.gene.overlap)); }return tcp_segments(b,v,&chosen,o.gene.reverse != U32(0));
        }
        let mut p: Plan=*plan;
        if f.arm == U32(1) || f.arm == U32(2) || f.arm == U32(5) || f.arm == U32(6) { p=flow_strong_plan(f,v.sequence,v.payload_length);if p.count == 0 { p=*plan; } }
        if f.arm == U32(7) && v.version == U8(4) {
            let first: U32=(cast<U32>(v.transport_header)+p.offsets[0]+U32(7)) & U32(0xfffffff8);let mut second: U32=cast<U32>(v.transport_header+v.payload_length/2) & U32(0xfffffff8);if second <= first { second=first+U32(8); }
            let result: Batch=ip_fragments(b,v,first,second,true,id);if result.count > 0 { return move result; }
        }
        if f.arm == U32(2) || f.arm == U32(6) { return overlap_segments(b,v,&p,true,8); }
        let reverse: Bool=f.arm == U32(1) || f.arm == U32(5) || f.arm == U32(4) || o.reverse || f.level >= U8(2);
        return tcp_segments(b,v,&p,reverse);
    }
    fn flow_strong_plan(f: &Flow, sequence: U32, length: USize) -> Plan {
        let mut p: Plan=zero<Plan>();let at: U32=wrapping_sub(sequence,f.base);if at >= U32(65536) { return p; }
        for i in 0..f.strong.count { let cut: U32=f.strong.offsets[i];if cut > at { add_cut(&mut p,cut-at,length); } }return p;
    }
    fn evaluate_telegram(e: &mut Engine, index: USize, b: &Buffer<U8>, v: &Packet, now: U64, transport: U32) -> Dispatch {
        let mut d: Dispatch=zero<Dispatch>();if e.flows[index].stopped { return move d; }
        if !e.flows[index].telegram {
            if e.flows[index].trial_pending { finish_trial(e,index,3,9,now);e.flows[index].trial_finished=false; }
            e.flows[index].telegram=true;e.flows[index].telegram_transport=transport;discard_stream(&mut e.flows[index],&mut e.memory);e.flows[index].recognized=true;e.flows[index].decided=true;e.flows[index].apply=true;
            let key: Endpoint=trial_endpoint(e,index);let slot: USize=evidence_slot(&mut e.evidence,&key,now);let mut forced: U32=telegram_arm(&e.evidence[slot]);
            if e.options.gene_mode && target_telegram(e.options.gene_target) { forced=1; }
            e.flows[index].arm=evidence_begin(&mut e.evidence,&key,now,6,false,forced);e.flows[index].arm_chosen=true;e.flows[index].trial_pending=true;e.flows[index].trial_time=now;e.flows[index].trial_started=true;
            trace_flow(e,index,1,now,0);trace_flow(e,index,27,now,transport);
        }
        let at: U32=wrapping_sub(v.sequence,e.flows[index].base);
        if !telegram_startup(&e.flows[index],at,v.payload_length,now) { e.flows[index].stopped=true;return move d; }
        note_payload(&mut e.flows[index],v.sequence,v.payload_length);note_send(&mut e.flows[index],v.sequence,v.payload_length,now,false);
        if e.flows[index].arm == U32(0) { return move d; }
        let p: Plan=telegram_plan(&e.options,&e.flows[index],at,v.payload_length);
        let gene: Bool=e.options.gene_mode && target_telegram(e.options.gene_target);let mut reverse: Bool=true;let mut overlap: USize=0;
        if e.flows[index].arm == U32(2) { overlap=8; }if gene { reverse=e.options.gene.reverse != U32(0);overlap=cast<USize>(e.options.gene.overlap); }
        if overlap > 0 { d.packets=overlap_segments(slice(b,0,len(b)),v,&p,reverse,overlap); }else { d.packets=tcp_segments(slice(b,0,len(b)),v,&p,reverse); }
        if d.packets.count > 1 {
            d.consumed=true;e.statistics.transformed=e.statistics.transformed+U64(1);
            let key: FlowKey=e.flows[index].key;let arm: U32=e.flows[index].arm;trace_event(e,26,now,key,zero<Host>(),arm,0,cast<U64>(d.packets.count),cast<U64>(overlap),cast<U64>(v.payload_length));
        }return move d;
    }
    fn evaluate(e: &mut Engine, b: &mut Buffer<U8>, interface_: U32, subinterface: U32, outbound: Bool, bypass: Bool, now: U64, delayed: Bool) -> Dispatch {
        let mut d: Dispatch=zero<Dispatch>();let v: Packet=parse_packet(slice(b,0,len(b)));if !v.valid || bypass { return move d; }
        if e.options.safe_automatic {
            if outbound && !automatic_public(&v.destination,v.version) { return move d; }
            if !outbound && !automatic_public(&v.source,v.version) { return move d; }
        }
        let key: FlowKey=FlowKey(v.source,v.destination,interface_,subinterface,v.source_port,v.destination_port,v.version,v.protocol);
        if v.protocol == U8(17) {
            if e.options.safe_automatic { let mut address_: Array<U8,16>=v.destination;if !outbound { address_=v.source; }if !ip_contains(&e.policy.quic_targets,&address_,v.version) {
                if outbound { let initial: QuicInitial=inspect_quic(slice(b,v.payload,v.payload_length),true);if initial.valid { let slot: USize=quic_slot(&mut e.quic,&key,now,true);if e.quic[slot].attempts == U8(0) { trace_event(e,25,now,key,zero<Host>(),0,0,0,0,0);e.quic[slot].attempts=1; } } }return move d;
            } }
            d=evaluate_quic(e,&v,&key,slice(b,0,len(b)),outbound,now);
            if e.options.udp_obfuscate && outbound && !d.consumed && policy_udp(&e.policy,&v.destination,v.version,e.options.whitelist) && udp_signature(slice(b,v.payload,v.payload_length)) > U32(0) {
                let at: USize=quic_slot(&mut e.quic,&key,now,true);if e.quic[at].attempts < U8(4) { d.fake=guarded_udp(slice(b,0,len(b)),&v,key_hash(&key) ^ now);e.quic[at].attempts=e.quic[at].attempts+U8(1); }
                trace_event(e,18,now,key,zero<Host>(),0,0,cast<U64>(d.fake.count),cast<U64>(v.payload_length),0);
            }
            if e.options.udp_obfuscate && !outbound {
                let reverse: FlowKey=reverse_key(&key);let at: USize=quic_slot(&mut e.quic,&reverse,now,true);e.quic[at].rx_bytes=e.quic[at].rx_bytes+cast<U64>(v.payload_length);
                if e.quic[at].last_report == U64(0) || now-e.quic[at].last_report >= U64(5000) { let received: U64=e.quic[at].rx_bytes;trace_event(e,23,now,reverse,zero<Host>(),0,0,received,0,0);e.quic[at].last_report=now; }
            }
            if d.consumed { let mode: U32=e.options.quic;trace_event(e,12,now,key,zero<Host>(),0,mode,cast<U64>(d.packets.count),0,0); }return move d;
        }
        if v.protocol != U8(6) || v.transport_header == 0 { return move d; }
        if !outbound {
            let reversed: FlowKey=reverse_key(&key);let index: USize=flow_find(&mut e.flows,&reversed,now);
            if index < len(&e.flows) {
                let previous_bytes: U64=e.flows[index].inbound_bytes;observe_transport(&mut e.flows[index],&v,slice(b,0,len(b)),now);
                if (v.flags & U8(18)) == U8(18) { trace_flow(e,index,2,now,0); }
                if e.flows[index].inbound_bytes > previous_bytes && (previous_bytes == U64(0) || (e.flows[index].inbound_bytes-e.flows[index].last_trace_bytes >= U64(65536) && now-e.flows[index].last_trace_ms >= U64(500))) {
                    trace_flow(e,index,5,now,0);e.flows[index].last_trace_bytes=e.flows[index].inbound_bytes;e.flows[index].last_trace_ms=now;
                }
                if e.flows[index].telegram && !e.flows[index].progress && e.flows[index].inbound_bytes > previous_bytes {
                    e.flows[index].progress=true;e.flows[index].stopped=true;trace_flow(e,index,28,now,4);
                }
                if (v.flags & U8(4)) != U8(0) { trace_flow(e,index,7,now,0);if e.flows[index].trial_started && !e.flows[index].trial_finished && (e.flows[index].inbound_bytes < U64(16384) || !e.flows[index].client_after) { finish_trial(e,index,2,7,now); } }
                if (v.flags & U8(1)) != U8(0) { trace_flow(e,index,10,now,0);if e.flows[index].progress && e.flows[index].inbound_bytes > U64(0) { finish_trial(e,index,1,8,now); }else { finish_trial(e,index,3,8,now); } }
                if observe_reply(&mut e.flows[index],&e.options,&v,slice(b,0,len(b)),now,&mut e.strategies,&mut e.memory) { e.statistics.learned=e.statistics.learned+U64(1); }
                if e.flows[index].progress && e.flows[index].window_until != U64(0) { e.flows[index].window_until=0;trace_flow(e,index,16,now,0); }
            }return move d;
        }
        let mut index: USize=flow_find(&mut e.flows,&key,now);
        if (v.flags & U8(2)) != U8(0) {
            if index == len(&e.flows) { index=engine_create_flow(e,&key,now); }
            if !e.flows[index].syn || e.flows[index].base != wrapping_add(v.sequence,U32(1)) {
                finish_trial(e,index,3,9,now);discard_stream(&mut e.flows[index],&mut e.memory);e.flows[index]=zero<Flow>();e.flows[index].key=key;e.flows[index].used=true;e.flows[index].touched=now;
                e.flows[index].syn=true;e.flows[index].base=wrapping_add(v.sequence,U32(1));e.flows[index].started=now;if e.options.strategy == U32(1) { e.flows[index].level=1; }
            }
            open_trial(e,index,now,true);
            if (e.flows[index].trace_mask & U32(16)) == U32(0) {
                let mut scale: U64=255;let mut mss: U64=0;let raw: Slice<U8>=slice(b,0,len(b));let scale_pos: USize=tcp_option(raw,&v,3);let mss_pos: USize=tcp_option(raw,&v,2);
                if scale_pos < len(raw) && raw[scale_pos+1] == U8(3) { scale=cast<U64>(raw[scale_pos+2]); }if mss_pos < len(raw) && raw[mss_pos+1] == U8(4) { mss=cast<U64>(be16(raw,mss_pos+2)); }
                let host: Host=e.flows[index].host;let arm: U32=e.flows[index].arm;trace_event(e,19,now,key,host,arm,0,cast<U64>(be16(raw,v.transport+14)),scale,mss);e.flows[index].trace_mask=e.flows[index].trace_mask | U32(16);
            }
            d.modified=pace_server(b,&v,e.flows[index].window_until > now);return move d;
        }
        if index < len(&e.flows) {
            d.modified=pace_server(b,&v,e.flows[index].window_until > now && !e.flows[index].progress);
            if e.flows[index].complete && v.payload_length > 0 && cast<USize>(wrapping_sub(v.sequence,e.flows[index].base)) >= e.flows[index].wire { e.flows[index].client_after=true; }
        }
        if v.payload_length == 0 || !v.transformable {
            if index < len(&e.flows) && (v.flags & U8(5)) != U8(0) { trace_flow(e,index,22,now,14);finish_trial(e,index,3,14,now);e.flows[index].stopped=true;discard_stream(&mut e.flows[index],&mut e.memory); }return move d;
        }
        let mut telegram_kind: U32=0;
        if index < len(&e.flows) {
            if e.flows[index].telegram { telegram_kind=e.flows[index].telegram_transport; }
            else if !e.flows[index].recognized && !e.flows[index].stopped && telegram_scope(&e.options,&e.policy,&v) && e.flows[index].syn { telegram_kind=telegram_flow_transport(&e.flows[index],&v,slice(b,0,len(b))); }
        }
        if telegram_kind != U32(0) { return evaluate_telegram(e,index,b,&v,now,telegram_kind); }
        if index == len(&e.flows) {
            let first: U8=b[v.payload];if e.options.strategy != U32(2) && first != U8(22) && !(first >= U8(65) && first <= U8(90)) { return move d; }
            index=engine_create_flow(e,&key,now);e.flows[index].base=v.sequence;e.flows[index].started=now;if e.options.strategy == U32(1) { e.flows[index].level=1; }
        }
        if !e.options.safe_automatic { open_trial(e,index,now,false); }d.generation=e.flows[index].base;if !delayed { note_payload(&mut e.flows[index],v.sequence,v.payload_length); }
        if !delayed && e.flows[index].jitter_until > now { d.delay=e.flows[index].jitter_until;return move d; }
        if e.flows[index].stopped { return move d; }
        // Expire stale streams before enforcing the 16 MiB aggregate bound.
        let required: USize=(cast<USize>(wrapping_sub(v.sequence,e.flows[index].base))+v.payload_length)*2;
        if required > 16777216-min_size(e.memory,16777216)+len(&e.flows[index].stream.bytes)*2 { engine_expire(e,now,16384); }
        inspect_flow(&mut e.flows[index],&e.options,&e.policy,&v,slice(b,v.payload,v.payload_length),now,&mut e.memory,&mut e.peak);
        if e.options.safe_automatic { e.flows[index].cloud_provider=cloud_provider(&e.policy,&e.flows[index].host,&v.destination,v.version);e.flows[index].cloud_scope=cloud_scope(&e.options,&e.policy,&e.flows[index],&v); }
        if !e.flows[index].decided || !e.flows[index].apply || e.flows[index].stopped {
            if e.flows[index].decided && !e.flows[index].apply && (e.flows[index].trace_mask & U32(2)) == U32(0) { e.flows[index].trace_mask=e.flows[index].trace_mask | U32(2);trace_flow(e,index,13,now,0);finish_trial(e,index,3,11,now); }
            return move d;
        }
        if e.options.safe_automatic && !e.flows[index].arm_chosen {
            // A large ClientHello can reveal SNI only in a later TCP segment.
            // Forward the current bytes while retaining bounded reassembly.
            if e.flows[index].host.length == 0 && !e.flows[index].complete { return move d; }
            if automatic_protected(&e.flows[index].host) || cloud_protected_ip(&v.destination,v.version) || (e.flows[index].host.length == 0 && !(e.flows[index].tls && e.flows[index].cloud_scope)) {
                trace_flow(e,index,24,now,1);e.flows[index].apply=false;e.flows[index].stopped=true;discard_stream(&mut e.flows[index],&mut e.memory);return move d;
            }
            open_trial(e,index,now,false);
            if e.flows[index].cloud_scope { let h: Host=e.flows[index].host;let a: U32=e.flows[index].arm;let p: U32=e.flows[index].cloud_provider;let pid: U32=e.flows[index].mwo_pid;trace_event(e,30,now,key,h,a,0,cast<U64>(p),cast<U64>(pid),0); }
            let mut scope: U32=3;if e.flows[index].arm == U32(0) { scope=2; }trace_flow(e,index,24,now,scope);
        }
        if e.options.safe_automatic && automatic_known(&e.flows[index].host) && e.policy.quic_targets.count < USize(4096) && !ip_contains(&e.policy.quic_targets,&v.destination,v.version) {
            let mut target: Prefix=zero<Prefix>();target.address=v.destination;target.version=v.version;target.bits=32;if v.version == U8(6) { target.bits=128; }target.valid=true;ip_add(&mut e.policy.quic_targets,&target);
        }
        if !e.flows[index].trial_started { e.flows[index].trial_started=true;e.flows[index].trial_time=now;trace_flow(e,index,3,now,0); }
        if e.options.auto_strategy && e.flows[index].arm == U32(0) { note_send(&mut e.flows[index],v.sequence,v.payload_length,now,false);return move d; }
        if e.options.learn && !e.options.auto_strategy {
            let endpoint_: Endpoint=endpoint(&key,host_hash(&e.flows[index].host));let at: USize=strategy_slot(&mut e.strategies,&endpoint_,now,false);
            if at < len(&e.strategies) && e.strategies[at].expires > now && e.strategies[at].level > e.flows[index].level { e.flows[index].level=e.strategies[at].level; }
        }
        if !delayed && e.options.jitter_ms > U32(0) && !e.flows[index].jittered && e.flows[index].recognized {
            e.flows[index].jittered=true;e.flows[index].jitter_until=now+key_hash(&key)%cast<U64>(e.options.jitter_ms+U32(1));
            if e.flows[index].jitter_until > now { d.delay=e.flows[index].jitter_until;return move d; }
        }
        let offset: U32=wrapping_sub(v.sequence,e.flows[index].base);let handshake: Bool=offset < U32(65536) && (!e.flows[index].complete || cast<USize>(offset) < e.flows[index].wire);
        let retry: Bool=e.options.adaptive || (e.options.safe_automatic && e.options.gene_mode && !flow_gene(&e.options,&e.flows[index]));
        if handshake && note_send(&mut e.flows[index],v.sequence,v.payload_length,now,retry) { e.statistics.escalations=e.statistics.escalations+U64(1); }
        if e.options.http && !e.flows[index].tls && e.flows[index].recognized && handshake { d.modified=http_case(b,&v);if d.modified { d.modified=checksums(b); } }
        let mut plan: Plan=flow_plan(&e.flows[index],v.sequence,v.payload_length);
        let legacy: Bool=e.options.strategy == U32(2) && (e.options.first_packets == U32(0) || e.flows[index].packets <= e.options.first_packets);
        if legacy && v.payload_length > 2 {
            plan=zero<Plan>();add_cut(&mut plan,cast<U32>(min_size(8,v.payload_length-1)),v.payload_length);add_cut(&mut plan,cast<U32>(v.payload_length/2),v.payload_length);sort_plan(&mut plan);
        }
        if e.options.aggressive && (handshake || legacy) && plan.count > 0 {
            if e.options.auto_strategy { d.packets=adaptive_transform(slice(b,0,len(b)),&v,&plan,&e.flows[index],&e.options,e.identification); }
            else { d.packets=transform_packet(slice(b,0,len(b)),&v,&plan,&e.options,e.options.reverse || e.flows[index].level >= U8(2),e.identification); }
            e.identification=wrapping_add(e.identification,U16(1));
            if d.packets.count > 0 {
                if e.options.auto_strategy && !e.flows[index].cloud_scope && e.flows[index].fake_count < U8(2) {
                    let arm: U32=e.flows[index].arm;let mut guard: U32=0;if arm == U32(3) || arm == U32(6) { guard=1; }if arm == U32(4) { guard=2; }
                    if e.options.gene_mode && gene_applies(&e.flows[index].host,e.options.gene_target) {
                        for repeat in 0..cast<USize>(e.options.gene.repeats) { let cover: Batch=guarded_cover_for(slice(b,0,len(b)),&v,e.options.gene.guard,e.flows[index].tls,key_hash(&key) ^ now ^ cast<U64>(repeat),e.options.gene.gap,e.options.gene.template_);if cover.count > 0 { batch_add(&mut d.fake,slice(&cover.bytes,0,len(&cover.bytes))); } }
                    }else { d.fake=guarded_cover(slice(b,0,len(b)),&v,guard,e.flows[index].tls,key_hash(&key) ^ now); }
                    if d.fake.count > 0 { e.flows[index].fake_count=e.flows[index].fake_count+U8(1); }
                }else if !e.options.auto_strategy && e.options.fake && e.flows[index].fake_count < U8(3) && (e.flows[index].tls || e.flows[index].host.length > 0) {
                    let badseq: Bool=e.options.fake_mode == U32(0);let mut ttl: U8=0;
                    if !badseq { ttl=fake_ttl(e.flows[index].server_ttl,e.options.ttl_margin,e.options.ttl_spread,e.options.ttl_max,e.options.min_hops,cast<U32>(wrapping_add(key_hash(&key),cast<U64>(e.flows[index].fake_count)) & U64(0xffffffff))); }
                    d.fake=fake_tcp(slice(b,0,len(b)),&v,badseq,ttl,e.flows[index].tls);if d.fake.count > 0 { e.flows[index].fake_count=e.flows[index].fake_count+U8(1); }
                }d.consumed=true;e.statistics.transformed=e.statistics.transformed+U64(1);
                if (e.flows[index].trace_mask & U32(4)) == U32(0) || e.options.debug > U32(1) { let host: Host=e.flows[index].host;let arm: U32=e.flows[index].arm;trace_event(e,4,now,key,host,arm,0,cast<U64>(d.packets.count),cast<U64>(d.fake.count),cast<U64>(v.payload_length));e.flows[index].trace_mask=e.flows[index].trace_mask | U32(4); }
            }
        }return move d;
    }
}
