module dpi.adaptive
import dpi.tcp

native {
    // Wilson upper confidence bound, Beta(1,1)-smoothed evidence. Selection is
    // per destination/interface and fixed for the lifetime of a TCP connection.
    record ArmEvidence repr(C) { trials: U32; successes: U32; failures: U32; inflight: U32; latency_ms: U32; }
    record StrategyEvidence repr(C) { key: Endpoint; arms: Array<ArmEvidence,8>; touched: U64; used: Bool; }
    fn arm_name(arm: U32) -> Utf8 {
        if arm >= U32(8) { return "legacy-manual"; }if arm == U32(0) { return "control-pass"; }if arm == U32(1) { return "semantic-disorder"; }
        if arm == U32(2) { return "overlap-disorder"; }if arm == U32(3) { return "cover-badsum"; }
        if arm == U32(4) { return "cover-md5-badseq"; }if arm == U32(5) { return "server-window-disorder"; }
        if arm == U32(6) { return "server-window-cover"; }return "ipv4-fragments";
    }
    fn arm_score(a: &ArmEvidence) -> F64 guarantees(NoAlloc,NoBlocking,NoIO) {
        let n: F64=cast<F64>(a.successes)+cast<F64>(a.failures)+2.0;
        let p: F64=(cast<F64>(a.successes)+1.0)/n;let z: F64=1.2815515655446004;let z2: F64=z*z;
        let upper: F64=(p+z2/(2.0*n)+z*sqrt(p*(1.0-p)/n+z2/(4.0*n*n)))/(1.0+z2/n);
        return upper-0.04*cast<F64>(a.inflight)-0.000002*cast<F64>(a.latency_ms);
    }
    fn evidence_slot(items: &mut Buffer<StrategyEvidence>, key: &Endpoint, now: U64) -> USize {
        let base: USize=cast<USize>(endpoint_hash(key) & U64(1023))*4;let mut choice: USize=base;let mut oldest: U64=18446744073709551615;
        for way in 0..4 {
            let i: USize=base+way;if items[i].used && endpoint_equal(&items[i].key,key) { items[i].touched=now;return i; }
            if !items[i].used || items[i].touched < oldest { oldest=items[i].touched;choice=i; }
        }
        items[choice]=zero<StrategyEvidence>();items[choice].used=true;items[choice].key=*key;items[choice].touched=now;return choice;
    }
    fn choose_arm(e: &mut StrategyEvidence, version: U8, allow_window: Bool, forced: U32) -> U32 {
        if forced < U32(8) { return forced; }
        let order: Array<U32,8>=array<Array<U32,8>>(1,2,3,4,5,6,7,0);
        let mut best: U32=1;let mut score: F64=-100.0;
        for i in 0..8 {
            let arm: U32=order[i];if (arm == U32(7) && version != U8(4)) || ((arm == U32(5) || arm == U32(6)) && !allow_window) { continue; }
            let a: ArmEvidence=e.arms[cast<USize>(arm)];
            if a.trials == U32(0) && a.inflight == U32(0) { return arm; }
            let candidate: F64=arm_score(&a);if candidate > score { score=candidate;best=arm; }
        }return best;
    }
    fn evidence_begin(items: &mut Buffer<StrategyEvidence>, key: &Endpoint, now: U64, version: U8, allow_window: Bool, forced: U32) -> U32 {
        let i: USize=evidence_slot(items,key,now);let arm: U32=choose_arm(&mut items[i],version,allow_window,forced);let at: USize=cast<USize>(arm);
        items[i].arms[at].trials=saturating_add(items[i].arms[at].trials,U32(1));items[i].arms[at].inflight=saturating_add(items[i].arms[at].inflight,U32(1));return arm;
    }
    fn evidence_end(items: &mut Buffer<StrategyEvidence>, key: &Endpoint, arm: U32, result: U32, latency: U32, now: U64) -> Unit {
        if arm >= U32(8) { return; }let i: USize=evidence_slot(items,key,now);let at: USize=cast<USize>(arm);
        items[i].arms[at].inflight=saturating_sub(items[i].arms[at].inflight,U32(1));
        if result == U32(1) { items[i].arms[at].successes=saturating_add(items[i].arms[at].successes,U32(1)); }
        if result == U32(2) { items[i].arms[at].failures=saturating_add(items[i].arms[at].failures,U32(1)); }
        if latency > U32(0) {
            if items[i].arms[at].latency_ms == U32(0) { items[i].arms[at].latency_ms=latency; }
            else { items[i].arms[at].latency_ms=cast<U32>((cast<U64>(items[i].arms[at].latency_ms)*U64(7)+cast<U64>(latency))/U64(8)); }
        }
        let a: ArmEvidence=items[i].arms[at];if a.successes+a.failures >= U32(64) {
            items[i].arms[at].successes=a.successes/U32(2);items[i].arms[at].failures=a.failures/U32(2);items[i].arms[at].trials=(a.trials/U32(2))+a.inflight;
        }
    }
    fn tcp_option(b: Slice<U8>, v: &Packet, kind: U8) -> USize {
        if v.protocol != U8(6) || v.transport_header < 20 { return len(b); }
        let mut p: USize=v.transport+20;let end: USize=v.transport+v.transport_header;
        while p < end {
            let code: U8=b[p];if code == U8(0) { if kind == U8(0) { return p; }return len(b); }if code == U8(1) { p=p+1;continue; }
            if p+2 > end || b[p+1] < U8(2) || cast<USize>(b[p+1]) > end-p { return len(b); }
            if code == kind { return p; }p=p+cast<USize>(b[p+1]);
        }return len(b);
    }
    // Deliberately leave negotiated window scale untouched. Original advertised
    // window values are restored verbatim after the brief ServerHello phase.
    fn pace_server(b: &mut Buffer<U8>, v: &Packet, active: Bool) -> Bool {
        if !active || v.protocol != U8(6) || v.transport_header < 20 || v.fragmented || (v.flags & U8(5)) != U8(0) { return false; }
        {
            let raw: Slice<U8>=slice(b,0,len(b));if tcp_option(raw,v,19) < len(raw) || tcp_option(raw,v,29) < len(raw) || tcp_option(raw,v,30) < len(raw) { return false; }
        }
        let old: U16=read_be<U16>(b,v.transport+14);if old == U16(0) { return false; }
        let mut target: U16=1;if (v.flags & U8(2)) != U8(0) { target=64; }
        if old <= target { return false; }write_be<U16>(b,v.transport+14,target);return checksums(b);
    }
    fn overlap_segments(b: Slice<U8>, v: &Packet, p: &Plan, reverse: Bool, overlap: USize) -> Batch {
        let mut out: Batch=zero<Batch>();if !v.transformable || v.protocol != U8(6) || p.count == 0 { return move out; }
        let mut plan: Plan=zero<Plan>();for i in 0..p.count { add_cut(&mut plan,p.offsets[i],v.payload_length); }sort_plan(&mut plan);if plan.count == 0 { return move out; }
        let mut bounds: Array<USize,6>=zero<Array<USize,6>>();for i in 0..plan.count { bounds[i+1]=cast<USize>(plan.offsets[i]); }let count: USize=plan.count+1;bounds[count]=v.payload_length;
        for part in 0..count {
            let mut i: USize=part;if reverse { i=count-part-1; }let mut begin: USize=bounds[i];let end: USize=bounds[i+1];
            if i > 0 { begin=begin-min_size(begin,overlap); }
            let mut packet: Buffer<U8>=buffer<U8>(v.payload+end-begin);for j in 0..v.payload { packet[j]=b[j]; }
            for j in begin..end { packet[v.payload+j-begin]=b[v.payload+j]; }packet_length(&mut packet,v);
            write_be<U32>(&mut packet,v.transport+4,wrapping_add(v.sequence,cast<U32>(begin)));
            if v.version == U8(4) { write_be<U16>(&mut packet,4,wrapping_add(be16(b,4),cast<U16>(i))); }
            if i+1 < count { packet[v.transport+13]=packet[v.transport+13] & U8(0xf6); }if i > 0 { packet[v.transport+13]=packet[v.transport+13] & U8(0x7f); }
            if !checksums(&mut packet) { return zero<Batch>(); }batch_add(&mut out,slice(&packet,0,len(&packet)));
        }return move out;
    }
    fn random_step(seed: &mut U64) -> U8 {
        let mut x: U64=*seed;x=x ^ (x << U64(13));x=x ^ (x >> U64(7));x=x ^ (x << U64(17));*seed=x;return cast<U8>(x & U64(255));
    }
    fn cover_hello_for(original: Slice<U8>, seed_value: U64, template_: U32) -> Buffer<U8> {
        let mut sid: USize=0;if len(original) > 44 && original[0] == U8(22) && original[5] == U8(1) { sid=cast<USize>(original[43]);if sid > 32 || sid > len(original)-44 { sid=0; } }
        let mut hostname: Utf8="www.microsoft.com";if template_ == U32(1) { hostname="www.wikipedia.org"; }if template_ == U32(2) { hostname="www.example.com"; }let host: Slice<U8>=utf8_bytes(hostname);let mut b: Buffer<U8>=buffer<U8>(44+sid);let mut seed: U64=seed_value | U64(1);
        b[0]=22;b[1]=3;b[2]=1;b[5]=1;b[9]=3;b[10]=3;for i in 11..43 { b[i]=random_step(&mut seed); }b[43]=cast<U8>(sid);for i in 0..sid { b[44+i]=original[44+i]; }
        let cipher: Array<U8,12>=array<Array<U8,12>>(0,8,19,1,19,2,19,3,192,47,1,0);bytes_append(&mut b,slice(&cipher,0,12));
        let extension: USize=len(&b);resize(&mut b,extension+2);
        let sni: USize=len(&b);resize(&mut b,sni+9+len(host));write_be<U16>(&mut b,sni,0);write_be<U16>(&mut b,sni+2,cast<U16>(len(host)+5));
        write_be<U16>(&mut b,sni+4,cast<U16>(len(host)+3));write_be<U16>(&mut b,sni+7,cast<U16>(len(host)));for i in 0..len(host) { b[sni+9+i]=host[i]; }
        let versions: Array<U8,7>=array<Array<U8,7>>(0,43,0,3,2,3,4);bytes_append(&mut b,slice(&versions,0,7));
        let signatures: Array<U8,12>=array<Array<U8,12>>(0,13,0,8,0,6,4,3,8,4,8,7);bytes_append(&mut b,slice(&signatures,0,12));
        let groups: Array<U8,8>=array<Array<U8,8>>(0,10,0,4,0,2,0,29);bytes_append(&mut b,slice(&groups,0,8));
        let share: USize=len(&b);resize(&mut b,share+42);write_be<U16>(&mut b,share,51);write_be<U16>(&mut b,share+2,38);write_be<U16>(&mut b,share+4,36);write_be<U16>(&mut b,share+6,29);write_be<U16>(&mut b,share+8,32);
        for i in 0..32 { b[share+10+i]=random_step(&mut seed); }
        let alpn: Array<U8,18>=array<Array<U8,18>>(0,16,0,14,0,12,2,104,50,8,104,116,116,112,47,49,46,49);bytes_append(&mut b,slice(&alpn,0,18));
        let extensions_length: U16=cast<U16>(len(&b)-extension-2);write_be<U16>(&mut b,extension,extensions_length);
        let handshake: U32=cast<U32>(len(&b)-9);b[6]=cast<U8>((handshake >> U32(16)) & U32(255));b[7]=cast<U8>((handshake >> U32(8)) & U32(255));b[8]=cast<U8>(handshake & U32(255));
        let record_length: U16=cast<U16>(len(&b)-5);write_be<U16>(&mut b,3,record_length);return move b;
    }
    fn cover_hello(original: Slice<U8>, seed_value: U64) -> Buffer<U8> { return cover_hello_for(original,seed_value,0); }
    fn guarded_cover(b: Slice<U8>, v: &Packet, guard: U32, tls: Bool, seed_value: U64) -> Batch { return guarded_cover_for(b,v,guard,tls,seed_value,1048576,0); }
    fn guarded_cover_for(b: Slice<U8>, v: &Packet, guard: U32, tls: Bool, seed_value: U64, gap: U32, template_: U32) -> Batch {
        let mut out: Batch=zero<Batch>();if v.protocol != U8(6) || !v.transformable || guard == U32(0) { return move out; }
        let mut body: Buffer<U8>=buffer<U8>(0);if tls { body=cover_hello_for(slice(&b,v.payload,v.payload_length),seed_value,template_); }
        else { body=bytes_copy(utf8_bytes("GET / HTTP/1.1\r\nHost: www.microsoft.com\r\nConnection: close\r\n\r\n")); }
        let mut header: USize=v.payload;let md5: Bool=guard == U32(2);if md5 && v.transport_header > 40 { return move out; }if md5 { header=header+20; }
        let mut packet: Buffer<U8>=buffer<U8>(header+len(&body));for i in 0..v.payload { packet[i]=b[i]; }
        if md5 {
            let old_options_end: USize=tcp_option(b,v,0);if old_options_end < v.payload { for i in old_options_end..v.payload { packet[i]=1; } }
            packet[v.transport+12]=(cast<U8>((v.transport_header+20)/4)*U8(16)) | (b[v.transport+12] & U8(15));let at: USize=v.payload;packet[at]=19;packet[at+1]=18;
            let mut seed: U64=seed_value | U64(1);for i in 0..16 { packet[at+2+i]=random_step(&mut seed); }packet[at+18]=1;packet[at+19]=1;
        }
        for i in 0..len(&body) { packet[header+i]=body[i]; }packet_length(&mut packet,v);packet[v.transport+13]=24;
        // Every cover has an out-of-window sequence. The badsum variant adds
        // an independently invalid TCP checksum; MD5 is never the sole guard.
        write_be<U32>(&mut packet,v.transport+4,wrapping_sub(v.sequence,gap));
        if !checksums(&mut packet) { return move out; }
        if guard == U32(1) { let sum: U16=read_be<U16>(&packet,v.transport+16);write_be<U16>(&mut packet,v.transport+16,sum ^ U16(0xffff)); }
        batch_add(&mut out,slice(&packet,0,len(&packet)));return move out;
    }
    fn udp_signature(b: Slice<U8>) -> U32 {
        if len(b) >= 20 && (b[0] & U8(192)) == U8(0) && be32(b,4) == U32(0x2112a442) && cast<USize>(be16(b,2))+20 == len(b) { return 1; }
        if len(b) == 74 && (be16(b,0) == U16(1) || be16(b,0) == U16(2)) && be16(b,2) == U16(70) { return 2; }return 0;
    }
    fn guarded_udp(b: Slice<U8>, v: &Packet, seed_value: U64) -> Batch {
        let mut out: Batch=zero<Batch>();if v.protocol != U8(17) || !v.transformable || udp_signature(slice(&b,v.payload,v.payload_length)) == U32(0) { return move out; }
        let mut packet: Buffer<U8>=bytes_copy(b);let mut seed: U64=seed_value | U64(1);
        for i in v.payload+8..v.length { packet[i]=random_step(&mut seed); }
        if !checksums(&mut packet) { return move out; }let mut sum: U16=read_be<U16>(&packet,v.transport+6) ^ U16(0xffff);if sum == U16(0) { sum=1; }
        write_be<U16>(&mut packet,v.transport+6,sum);batch_add(&mut out,slice(&packet,0,len(&packet)));return move out;
    }
}
