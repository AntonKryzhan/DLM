module tests.adaptive
import tests.core

native {
    fn test_cover() -> Unit {
        let original: Buffer<U8>=fake_hello();
        for seed in 1..40 {
            let hello: Buffer<U8>=cover_hello(slice(&original,0,len(&original)),cast<U64>(seed));let h: Hello=inspect_tls(slice(&hello,0,len(&hello)));
            assert(h.recognized && h.complete && !h.malformed && h.host.length == 17);
        }
        let b: Buffer<U8>=tcp_packet(slice(&original,0,len(&original)),4,24,false);let v: Packet=parse_packet(slice(&b,0,len(&b)));
        for guard in 1..3 {
            let cover: Batch=guarded_cover(slice(&b,0,len(&b)),&v,cast<U32>(guard),true,12345);assert(cover.count == 1);
            let f: Packet=parse_packet(slice(&cover.bytes,0,len(&cover.bytes)));assert(f.valid && sequence_before(f.sequence,v.sequence));
            let mut corrected: Buffer<U8>=bytes_copy(slice(&cover.bytes,0,len(&cover.bytes)));let before: U16=read_be<U16>(&corrected,f.transport+16);assert(checksums(&mut corrected));
            let after: U16=read_be<U16>(&corrected,f.transport+16);if guard == 1 { assert(before != after); }else { assert(before == after);assert(f.transport_header == v.transport_header+20); }
        }
    }
    fn test_overlap() -> Unit {
        let body: Slice<U8>=utf8_bytes("GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n");
        for version in 0..2 {
            for cut in 1..len(body)-1 {
                let b: Buffer<U8>=tcp_packet(body,4294967280,25,version == 1);let v: Packet=parse_packet(slice(&b,0,len(&b)));let mut p: Plan=zero<Plan>();add_cut(&mut p,cast<U32>(cut),len(body));add_cut(&mut p,cast<U32>(len(body)-1),len(body));sort_plan(&mut p);
                for reverse in 0..2 {
                    let parts: Batch=overlap_segments(slice(&b,0,len(&b)),&v,&p,reverse == 1,8);assert(parts.count >= 2);let mut covered: Buffer<U8>=buffer<U8>(len(body));let mut offset: USize=0;
                    for i in 0..parts.count {
                        let n: USize=cast<USize>(parts.lengths[i]);let raw: Slice<U8>=slice(&parts.bytes,offset,n);let part: Packet=parse_packet(raw);assert(part.valid);
                        let at: USize=cast<USize>(wrapping_sub(part.sequence,v.sequence));assert(at+part.payload_length <= len(body));
                        for j in 0..part.payload_length { assert(raw[part.payload+j] == body[at+j]);covered[at+j]=1; }
                        offset=offset+n;
                    }
                    for i in 0..len(body) { assert(covered[i] == U8(1)); }
                }
            }
        }
    }
    fn test_pacing() -> Unit {
        let mut b: Buffer<U8>=tcp_packet(utf8_bytes(""),100,2,false);write_be<U16>(&mut b,34,65535);assert(checksums(&mut b));let v: Packet=parse_packet(slice(&b,0,len(&b)));
        assert(pace_server(&mut b,&v,true));assert(read_be<U16>(&b,34) == U16(64));assert(!pace_server(&mut b,&v,false));
        b[33]=16;write_be<U16>(&mut b,34,65535);assert(checksums(&mut b));let ack: Packet=parse_packet(slice(&b,0,len(&b)));assert(pace_server(&mut b,&ack,true));assert(read_be<U16>(&b,34) == U16(1));
        write_be<U16>(&mut b,34,0);assert(!pace_server(&mut b,&ack,true));
    }
    fn test_udp_cover() -> Unit {
        let mut stun: Buffer<U8>=buffer<U8>(20);write_be<U16>(&mut stun,0,1);write_be<U32>(&mut stun,4,0x2112a442);assert(udp_signature(slice(&stun,0,len(&stun))) == U32(1));
        let b: Buffer<U8>=udp_packet(slice(&stun,0,len(&stun)));let v: Packet=parse_packet(slice(&b,0,len(&b)));let cover: Batch=guarded_udp(slice(&b,0,len(&b)),&v,98765);assert(cover.count == 1);
        let mut corrected: Buffer<U8>=bytes_copy(slice(&cover.bytes,0,len(&cover.bytes)));let before: U16=read_be<U16>(&corrected,v.transport+6);assert(before != U16(0));assert(checksums(&mut corrected));assert(before != read_be<U16>(&corrected,v.transport+6));
        let hello: Buffer<U8>=fake_hello();let mut tcp: Buffer<U8>=tcp_packet(slice(&hello,0,len(&hello)),1,24,false);write_be<U16>(&mut tcp,34,65535);assert(checksums(&mut tcp));let view: Packet=parse_packet(slice(&tcp,0,len(&tcp)));
        let mut authenticated: Batch=guarded_cover(slice(&tcp,0,len(&tcp)),&view,2,true,321);let protected_: Packet=parse_packet(slice(&authenticated.bytes,0,len(&authenticated.bytes)));assert(!pace_server(&mut authenticated.bytes,&protected_,true));
    }
    fn test_bandit() -> Unit {
        let mut good: ArmEvidence=zero<ArmEvidence>();good.successes=20;good.trials=20;let mut bad: ArmEvidence=zero<ArmEvidence>();bad.failures=20;bad.trials=20;assert(arm_score(&good) > arm_score(&bad));
        let mut e: StrategyEvidence=zero<StrategyEvidence>();assert(choose_arm(&mut e,4,true,8) == U32(1));e.arms[1].inflight=1;assert(choose_arm(&mut e,4,true,8) == U32(2));
        for i in 0..8 { e.arms[i].trials=10;e.arms[i].failures=10; }e.arms[3]=good;assert(choose_arm(&mut e,4,true,8) == U32(3));
        assert(choose_arm(&mut e,6,false,5) == U32(5));
    }
    fn test_profiles() -> Unit {
        let hello: Buffer<U8>=fake_hello();
        for arm in 0..8 {
            let mut o: Options=default_options();o.forced_arm=cast<U32>(arm);let policy: Policy=zero<Policy>();let mut e: Engine=engine_new(move o,move policy);
            let mut syn: Buffer<U8>=tcp_packet(utf8_bytes(""),99,2,false);write_be<U16>(&mut syn,34,65535);assert(checksums(&mut syn));let start: Dispatch=evaluate(&mut e,&mut syn,7,3,true,false,1000,false);
            if arm == 5 || arm == 6 { assert(start.modified && read_be<U16>(&syn,34) == U16(64)); }
            let mut b: Buffer<U8>=tcp_packet(slice(&hello,0,len(&hello)),100,24,false);let result: Dispatch=evaluate(&mut e,&mut b,7,3,true,false,1100,false);
            if arm == 0 { assert(!result.consumed); }else { assert(result.consumed && result.packets.count >= 2); }
            if arm == 3 || arm == 4 || arm == 6 { assert(result.fake.count == 1); }
            for tick in 0..16 { engine_tick(&mut e,8000+cast<U64>(tick)*U64(100),1024); }
            let view: Packet=parse_packet(slice(&b,0,len(&b)));let key: FlowKey=FlowKey(view.source,view.destination,7,3,view.source_port,view.destination_port,view.version,view.protocol);let index: USize=flow_find(&mut e.flows,&key,10000);
            assert(index < len(&e.flows));assert(e.flows[index].trial_finished && e.flows[index].trial_result == U32(2));
            let endpoint_: Endpoint=endpoint(&key,0);let slot: USize=evidence_slot(&mut e.evidence,&endpoint_,10000);assert(e.evidence[slot].arms[arm].failures == U32(1));
        }
    }
    fn test_record_tracker() -> Unit {
        let mut f: Flow=zero<Flow>();f.tls=true;f.record_certain=true;
        let mut wire: Buffer<U8>=buffer<U8>(28);wire[0]=23;wire[1]=3;wire[2]=3;wire[4]=23;
        let first: Buffer<U8>=tcp_packet(slice(&wire,0,9),4294967290,24,false);let a: Packet=parse_packet(slice(&first,0,len(&first)));observe_transport(&mut f,&a,slice(&first,0,len(&first)),1000);
        assert(f.tls_app && f.record_remaining == 19 && f.inbound_bytes == U64(9));
        observe_transport(&mut f,&a,slice(&first,0,len(&first)),1001);assert(f.inbound_bytes == U64(9));
        let last: Buffer<U8>=tcp_packet(slice(&wire,9,19),3,24,false);let b: Packet=parse_packet(slice(&last,0,len(&last)));observe_transport(&mut f,&b,slice(&last,0,len(&last)),1200);
        assert(f.inbound_bytes == U64(28) && f.record_remaining == 0 && f.record_certain);
    }
    fn test_settings_v2() -> Unit {
        let mut o: Options=default_options();assert(option(&mut o,utf8_bytes("--strategy-profile=6")));assert(option(&mut o,utf8_bytes("--udp-obfuscate")));
        let text: Buffer<U8>=save_settings(&o);let mut b: Options=default_options();assert(load_settings(&mut b,slice(&text,0,len(&text))));assert(b.forced_arm == U32(6) && b.udp_obfuscate && b.diagnostic);
        assert(!option(&mut b,utf8_bytes("--strategy-profile=9")));assert(!option(&mut b,utf8_bytes("--handshake-timeout-ms=999")));
    }
    fn server_packet(body: Slice<U8>, sequence: U32, flags: U8) -> Buffer<U8> {
        let mut b: Buffer<U8>=tcp_packet(body,sequence,flags,false);
        for i in 0..4 { let old: U8=b[12+i];b[12+i]=b[16+i];b[16+i]=old; }
        write_be<U16>(&mut b,20,443);write_be<U16>(&mut b,22,12345);write_be<U32>(&mut b,28,176);assert(checksums(&mut b));return move b;
    }
    fn test_reply_stage_and_reset() -> Unit {
        let hello: Buffer<U8>=fake_hello();
        for arm in 0..2 {
            let mut o: Options=default_options();o.forced_arm=cast<U32>(arm);let p: Policy=zero<Policy>();let mut e: Engine=engine_new(move o,move p);
            let mut request: Buffer<U8>=tcp_packet(slice(&hello,0,len(&hello)),100,24,false);let dispatch: Dispatch=evaluate(&mut e,&mut request,1,0,true,false,1000,false);
            let mut server_hello: Buffer<U8>=buffer<U8>(9);server_hello[0]=22;server_hello[1]=3;server_hello[2]=3;server_hello[4]=4;server_hello[5]=2;
            let mut reply: Buffer<U8>=server_packet(slice(&server_hello,0,9),1,24);let received: Dispatch=evaluate(&mut e,&mut reply,1,0,false,false,1100,false);
            let v: Packet=parse_packet(slice(&request,0,len(&request)));let key: FlowKey=FlowKey(v.source,v.destination,1,0,v.source_port,v.destination_port,v.version,v.protocol);let i: USize=flow_find(&mut e.flows,&key,1150);
            assert(i < len(&e.flows) && e.flows[i].progress && !e.flows[i].trial_finished);
            let mut reset: Buffer<U8>=server_packet(utf8_bytes(""),10,20);let failed: Dispatch=evaluate(&mut e,&mut reset,1,0,false,false,1200,false);
            assert(e.flows[i].trial_finished && e.flows[i].trial_result == U32(2));
        }
    }
    fn main() -> Unit { test_cover();test_overlap();test_pacing();test_udp_cover();test_bandit();test_profiles();test_record_tracker();test_settings_v2();test_reply_stage_and_reset();print("DPI DLM 2.0: adaptive tests passed"); }
}
