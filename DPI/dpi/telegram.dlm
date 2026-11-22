module dpi.telegram
import dpi.automatic

native {
    fn telegram_scope(o: &Options, p: &Policy, v: &Packet) -> Bool {
        return o.telegram && o.aggressive && automatic_public(&v.destination,v.version) && ip_contains(&p.telegram_ips,&v.destination,v.version) &&
            policy_udp(p,&v.destination,v.version,o.whitelist) && len(&p.hosts.domains) == 0 && len(&p.excluded_hosts.domains) == 0;
    }
    // Classification is metadata only. Obfuscated traffic is never decrypted.
    fn telegram_transport(b: Slice<U8>) -> U32 {
        if len(b) == 0 { return 0; }
        if b[0] == U8(22) && (len(b) < 3 || (b[1] == U8(3) && b[2] <= U8(4))) { return 0; }
        if bytes_at(b,0,utf8_bytes("GET ")) || bytes_at(b,0,utf8_bytes("POST")) || bytes_at(b,0,utf8_bytes("HEAD")) || bytes_at(b,0,utf8_bytes("OPTI")) || bytes_at(b,0,utf8_bytes("CONN")) { return 0; }
        if b[0] == U8(239) { return 1; }
        if len(b) >= 4 {
            let n: U32=read_le<U32>(&b,0);if n == U32(0xeeeeeeee) { return 2; }if n == U32(0xdddddddd) { return 3; }
        }
        if len(b) < 8 { return 0; }return 4;
    }
    fn telegram_flow_transport(f: &Flow, v: &Packet, b: Slice<U8>) -> U32 {
        let at: USize=cast<USize>(wrapping_sub(v.sequence,f.base));let payload: Slice<U8>=slice(&b,v.payload,v.payload_length);
        if at == 0 { return telegram_transport(payload); }if at >= 8 || f.stream.contiguous != at { return 0; }
        let mut prefix: Array<U8,8>=zero<Array<U8,8>>();for i in 0..at { prefix[i]=f.stream.bytes[i]; }
        let count: USize=min_size(len(payload),8-at);for i in 0..count { prefix[at+i]=payload[i]; }return telegram_transport(slice(&prefix,0,at+count));
    }
    fn telegram_transport_name(kind: U32) -> Utf8 {
        if kind == U32(1) { return "abridged"; }if kind == U32(2) { return "intermediate"; }if kind == U32(3) { return "padded_intermediate"; }return "obfuscated_candidate";
    }
    fn telegram_startup(f: &Flow, at: U32, length: USize, now: U64) -> Bool {
        return f.syn && !f.progress && f.inbound_bytes == U64(0) && at < U32(512) && length > 0 && length <= 4096 && f.packets < U32(6) && now-f.started <= U64(3000);
    }
    fn telegram_arm(e: &StrategyEvidence) -> U32 {
        let order: Array<U32,3>=array<Array<U32,3>>(1,2,0);let mut best: U32=1;let mut score: F64=-100.0;
        for i in 0..3 { let arm: U32=order[i];let a: ArmEvidence=e.arms[cast<USize>(arm)];if a.trials == U32(0) && a.inflight == U32(0) { return arm; }
            let candidate: F64=arm_score(&a);if candidate > score { score=candidate;best=arm; } }return best;
    }
    fn telegram_plan(o: &Options, f: &Flow, at: U32, length: USize) -> Plan {
        let mut p: Plan=zero<Plan>();let mut cuts: Array<U32,4>=array<Array<U32,4>>(1,4,32,64);
        if o.gene_mode && target_telegram(o.gene_target) && (o.gene.cut1 > U32(0) || o.gene.cut2 > U32(0)) { cuts=array<Array<U32,4>>(o.gene.cut1,o.gene.cut2,0,0); }
        for i in 0..4 { if cuts[i] > at && cuts[i] <= U32(512) { add_cut(&mut p,cuts[i]-at,length); } }sort_plan(&mut p);
        if p.count == 0 && at < U32(64) { add_cut(&mut p,1,length); }return p;
    }
}
