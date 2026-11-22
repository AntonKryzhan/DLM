module dpi.tcp
import dpi.policy
import dpi.options

native {
    fn sequence_before(a: U32, b: U32) -> Bool { return a != b && wrapping_sub(a,b) > U32(0x80000000); }
    record FlowKey repr(C) {
        source: Array<U8,16>; destination: Array<U8,16>; interface_: U32; subinterface: U32;
        source_port: U16; destination_port: U16; version: U8; protocol: U8;
    }
    fn same_address(a: &Array<U8,16>, b: &Array<U8,16>) -> Bool {
        for i in 0..16 { if a[i] != b[i] { return false; } }return true;
    }
    fn key_equal(a: &FlowKey, b: &FlowKey) -> Bool {
        return a.interface_ == b.interface_ && a.subinterface == b.subinterface && a.source_port == b.source_port && a.destination_port == b.destination_port &&
            a.version == b.version && a.protocol == b.protocol && same_address(&a.source,&b.source) && same_address(&a.destination,&b.destination);
    }
    fn mix64(n: U64) -> U64 {
        let mut x: U64=n;x=x ^ (x >> U64(30));x=wrapping_mul(x,U64(0xbf58476d1ce4e5b9));
        x=x ^ (x >> U64(27));x=wrapping_mul(x,U64(0x94d049bb133111eb));return x ^ (x >> U64(31));
    }
    fn address_hash(a: &Array<U8,16>) -> U64 {
        return mix64(read_be<U64>(a,0)) ^ mix64(read_be<U64>(a,8));
    }
    fn key_hash(k: &FlowKey) -> U64 {
        return mix64(address_hash(&k.source) ^ (address_hash(&k.destination) << U64(1)) ^ (cast<U64>(k.source_port) << U64(48)) ^
            (cast<U64>(k.destination_port) << U64(32)) ^ (cast<U64>(k.interface_) << U64(16)) ^ cast<U64>(k.subinterface) ^ (cast<U64>(k.version) << U64(8)) ^ cast<U64>(k.protocol));
    }
    fn reverse_key(k: &FlowKey) -> FlowKey { return FlowKey(k.destination,k.source,k.interface_,k.subinterface,k.destination_port,k.source_port,k.version,k.protocol); }
    record Stream repr(C) { bytes: Buffer<U8>; present: Buffer<U8>; contiguous: USize; }
    record AppendResult repr(C) { accepted: Bool; repeated: Bool; conflict: Bool; changed: Bool; }
    fn stream_append(s: &mut Stream, base: U32, seq: U32, b: Slice<U8>) -> AppendResult {
        let mut r: AppendResult=zero<AppendResult>();let at: USize=cast<USize>(wrapping_sub(seq,base));
        if len(b) == 0 || at >= 65536 || len(b) > 65536-at { return r; }let needed: USize=at+len(b);
        if needed > len(&s.bytes) { resize(&mut s.bytes,needed);resize(&mut s.present,needed); }
        for i in 0..len(b) { if s.present[at+i] != U8(0) { r.repeated=true;if s.bytes[at+i] != b[i] { r.conflict=true;return r; } } }
        for i in 0..len(b) { if s.present[at+i] == U8(0) { s.bytes[at+i]=b[i];s.present[at+i]=1;r.changed=true; } }
        while s.contiguous < len(&s.present) && s.present[s.contiguous] != U8(0) { s.contiguous=s.contiguous+1; }r.accepted=true;return r;
    }
    record Flow repr(C) {
        key: FlowKey; used: Bool; touched: U64; stream: Stream; fast: Plan; strong: Plan; host: Host;
        base: U32; wire: USize; highest: U32; acknowledgement: U32; packets: U32; counted_until: U32;
        started: U64; escalation: U64; jitter_until: U64; level: U8; server_ttl: U8; fake_count: U8;
        tls: Bool; recognized: Bool; complete: Bool; decided: Bool; apply: Bool; stopped: Bool;
        acknowledged: Bool; progress: Bool; learned: Bool; jittered: Bool; syn: Bool; counted: Bool;
        arm: U32; arm_chosen: Bool; trial_started: Bool; trial_finished: Bool; trial_pending: Bool; trial_result: U32; trial_time: U64;
        syn_reply: Bool; client_after: Bool; inbound_bytes: U64; inbound_end: U32; inbound_known: Bool; last_rx: U64;
        record_header: Array<U8,5>; record_have: USize; record_remaining: USize; tls_app: Bool; record_certain: Bool;
        telegram: Bool; telegram_transport: U32; cloud_scope: Bool; cloud_provider: U32; mwo_owned: Bool; mwo_pid: U32;
        window_until: U64; trace_mask: U32; last_trace_bytes: U64; last_trace_ms: U64; udp_fakes: U32;
    }
    fn discard_stream(f: &mut Flow, memory: &mut USize) -> Unit {
        let previous: USize=len(&f.stream.bytes)+len(&f.stream.present);*memory=*memory-min_size(*memory,previous);f.stream=zero<Stream>();
    }
    fn flow_find(flows: &mut Buffer<Flow>, key: &FlowKey, now: U64) -> USize {
        let base: USize=cast<USize>(key_hash(key) & U64(4095))*4;
        for way in 0..4 { let i: USize=base+way;if flows[i].used && now-flows[i].touched <= U64(180000) && key_equal(&flows[i].key,key) { flows[i].touched=now;return i; } }
        return len(flows);
    }
    fn flow_create(flows: &mut Buffer<Flow>, key: &FlowKey, now: U64, memory: &mut USize) -> USize {
        let base: USize=cast<USize>(key_hash(key) & U64(4095))*4;let mut choice: USize=base;let mut oldest: U64=18446744073709551615;
        for way in 0..4 {
            let i: USize=base+way;if !flows[i].used || now-flows[i].touched > U64(180000) { choice=i;break; }
            if flows[i].touched < oldest { oldest=flows[i].touched;choice=i; }
        }
        discard_stream(&mut flows[choice],memory);flows[choice]=zero<Flow>();flows[choice].key=*key;flows[choice].used=true;flows[choice].touched=now;return choice;
    }
    fn note_payload(f: &mut Flow, sequence: U32, length: USize) -> Unit {
        if length == 0 { return; }let end: U32=wrapping_add(sequence,cast<U32>(length));
        if !f.counted || sequence_before(f.counted_until,end) { f.counted_until=end;f.counted=true;f.packets=saturating_add(f.packets,U32(1)); }
    }
    fn note_send(f: &mut Flow, seq: U32, size: USize, now: U64, adaptive: Bool) -> Bool {
        let at: U32=wrapping_sub(seq,f.base);if at >= U32(65536) || size > 65536-cast<USize>(at) { return false; }
        let repeated: Bool=at < f.highest;let pending: Bool=!f.acknowledged || sequence_before(f.acknowledgement,wrapping_add(seq,cast<U32>(size)));
        let escalation: Bool=adaptive && repeated && pending && !f.progress && f.level < U8(2) && now-f.escalation >= U64(200);
        if escalation { f.level=f.level+U8(1);f.escalation=now; }let end: U32=at+cast<U32>(size);if end > f.highest { f.highest=end; }return escalation;
    }
    fn flow_plan(f: &Flow, seq: U32, size: USize) -> Plan {
        let mut out: Plan=zero<Plan>();let mut plan: Plan=f.fast;if f.level > U8(0) { plan=f.strong; }
        let at: U32=wrapping_sub(seq,f.base);if at >= U32(65536) { return out; }
        for i in 0..plan.count { let cut: U32=plan.offsets[i];if cut > at && cast<USize>(cut-at) < size { add_cut(&mut out,cut-at,size); } }return out;
    }
    fn inspect_flow(f: &mut Flow, o: &Options, p: &Policy, v: &Packet, bytes: Slice<U8>, now: U64, memory: &mut USize, peak: &mut USize) -> Unit {
        if f.stopped || f.complete { return; }let at: USize=cast<USize>(wrapping_sub(v.sequence,f.base));let old: USize=len(&f.stream.bytes)*2;
        if at >= 65536 || len(bytes) > 65536-at || (at+len(bytes))*2 > 16777216-min_size(*memory,16777216)+old {
            f.stopped=true;discard_stream(f,memory);return;
        }
        let r: AppendResult=stream_append(&mut f.stream,f.base,v.sequence,bytes);let current: USize=len(&f.stream.bytes)*2;
        *memory=*memory+current-old;if *memory > *peak { *peak=*memory; }
        if !r.accepted || r.conflict || *memory > 16777216 { f.stopped=true;discard_stream(f,memory);return; }if !r.changed || f.stream.contiguous == 0 { return; }
        let mut h: Hello=zero<Hello>();{
            let available: Slice<U8>=slice(&f.stream.bytes,0,f.stream.contiguous);h=inspect_tls(available);if !h.recognized { h=inspect_http(available); }
        }
        if h.malformed { f.stopped=true;f.apply=false;discard_stream(f,memory);return; }
        if h.recognized {
            f.recognized=true;f.tls=h.tls;f.fast=hello_plan(&h,f.stream.contiguous,false);f.strong=hello_plan(&h,f.stream.contiguous,true);
            if h.tls && !o.sni && f.stream.contiguous > 1 { f.fast=zero<Plan>();f.fast.offsets[0]=1;f.fast.count=1; }
        }
        if h.host.length > 0 { f.host=h.host; }
        let host_rules: Bool=len(&p.hosts.domains) > 0 || len(&p.excluded_hosts.domains) > 0;
        let ready: Bool=f.recognized && (!host_rules || f.host.length > 0 || h.complete || (h.ech && o.ech && len(&p.excluded_hosts.domains) == 0));
        if ready || (o.strategy == U32(2) && !host_rules) { f.apply=policy_allows(p,&f.host,&v.destination,v.version,h.ech,o.ech,o.whitelist);f.decided=true; }
        f.complete=h.complete;if h.complete { f.wire=h.wire_length;discard_stream(f,memory); }
        else {
            let mut limit: U32=o.first_packets;if limit == U32(0) { limit=8; }
            let mut unknown: Bool=f.packets >= limit;if o.strategy == U32(2) { unknown=o.first_packets != U32(0) && f.packets > o.first_packets; }
            if (!f.recognized && unknown) || now-f.started > U64(5000) || f.packets > U32(128) { f.stopped=true;discard_stream(f,memory); }
        }
    }
    record Endpoint repr(C) { address_: Array<U8,16>; host: U64; interface_: U32; subinterface: U32; port: U16; version: U8; }
    fn endpoint(k: &FlowKey, host: U64) -> Endpoint { return Endpoint(k.destination,host,k.interface_,k.subinterface,k.destination_port,k.version); }
    fn endpoint_equal(a: &Endpoint, b: &Endpoint) -> Bool {
        return a.host == b.host && a.interface_ == b.interface_ && a.subinterface == b.subinterface && a.port == b.port && a.version == b.version && same_address(&a.address_,&b.address_);
    }
    fn endpoint_hash(e: &Endpoint) -> U64 {
        return mix64(address_hash(&e.address_) ^ e.host ^ (cast<U64>(e.interface_) << U64(32)) ^ (cast<U64>(e.subinterface) << U64(16)) ^ (cast<U64>(e.port) << U64(8)) ^ cast<U64>(e.version));
    }
    record Strategy repr(C) { key: Endpoint; used: Bool; touched: U64; expires: U64; level: U8; confirmed: Bool; }
    fn strategy_slot(items: &mut Buffer<Strategy>, key: &Endpoint, now: U64, create: Bool) -> USize {
        let base: USize=cast<USize>(endpoint_hash(key) & U64(1023))*4;let mut choice: USize=base;let mut oldest: U64=18446744073709551615;
        for way in 0..4 {
            let i: USize=base+way;if items[i].used && endpoint_equal(&items[i].key,key) && now-items[i].touched <= U64(600000) { items[i].touched=now;return i; }
            if !items[i].used || items[i].touched < oldest { oldest=items[i].touched;choice=i; }
        }
        if !create { return len(items); }items[choice]=Strategy(*key,true,now,0,0,false);return choice;
    }
    fn observe_reply(f: &mut Flow, o: &Options, v: &Packet, b: Slice<U8>, now: U64, strategies: &mut Buffer<Strategy>, memory: &mut USize) -> Bool {
        if (v.flags & U8(18)) == U8(18) { f.server_ttl=v.ttl; }
        if (v.flags & U8(16)) != U8(0) {
            let at: U32=wrapping_sub(v.acknowledgement,f.base);
            if at <= f.highest+U32(1) && at <= U32(65536) && (!f.acknowledged || sequence_before(f.acknowledgement,v.acknowledgement)) { f.acknowledgement=v.acknowledgement;f.acknowledged=true; }
        }
        let tls: Bool=f.tls && v.payload_length >= 9 && b[v.payload] == U8(22) && b[v.payload+1] == U8(3) && b[v.payload+5] == U8(2) && be16(b,v.payload+3) >= U16(4);
        let http: Bool=!f.tls && v.payload_length >= 12 && bytes_at(b,v.payload,utf8_bytes("HTTP/1.")) && b[v.payload+8] == U8(32) && (b[v.payload+9] == U8(50) || b[v.payload+9] == U8(51));
        let mut learned: Bool=false;
        if f.apply && f.recognized && f.highest > U32(0) && (tls || http) {
            f.progress=true;f.stopped=true;discard_stream(f,memory);
            if o.learn && !f.learned {
                let e: Endpoint=endpoint(&f.key,host_hash(&f.host));let at: USize=strategy_slot(strategies,&e,now,true);let mut level: U8=f.level;if o.reverse { level=2; }
                if !strategies[at].confirmed || strategies[at].level != level || strategies[at].expires <= now { strategies[at].expires=now+U64(600000); }
                strategies[at].level=level;strategies[at].confirmed=true;f.learned=true;learned=true;
            }
        }
        if (v.flags & U8(5)) != U8(0) {
            if (v.flags & U8(4)) != U8(0) && o.adaptive && o.learn && f.apply && f.recognized && f.highest > U32(0) && !f.progress && now-f.started <= U64(5000) {
                let e: Endpoint=endpoint(&f.key,host_hash(&f.host));let at: USize=strategy_slot(strategies,&e,now,true);let mut level: U8=f.level+U8(1);if level > U8(2) { level=2; }
                strategies[at].level=level;strategies[at].expires=now+U64(30000);strategies[at].confirmed=false;
            }f.stopped=true;discard_stream(f,memory);
        }return learned;
    }
}
