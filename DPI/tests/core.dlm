module tests.core
import dpi.engine
import dpi.settings

native {
    fn tcp_packet(body: Slice<U8>, seq: U32, flags: U8, ipv6: Bool) -> Buffer<U8> {
        let mut header: USize=20;if ipv6 { header=40; }let mut b: Buffer<U8>=buffer<U8>(header+20+len(body));
        if ipv6 { b[0]=96;b[6]=6;b[7]=64;b[8]=32;b[9]=1;b[24]=32;b[25]=1;write_be<U16>(&mut b,4,cast<U16>(20+len(body))); }
        else { b[0]=69;b[8]=64;b[9]=6;b[12]=10;b[15]=1;b[16]=192;b[18]=2;b[19]=1;let n: U16=cast<U16>(len(&b));write_be<U16>(&mut b,2,n);write_be<U16>(&mut b,6,0x4000); }
        write_be<U16>(&mut b,header,12345);write_be<U16>(&mut b,header+2,443);write_be<U32>(&mut b,header+4,seq);b[header+12]=80;b[header+13]=flags;
        for i in 0..len(body) { b[header+20+i]=body[i]; }assert(checksums(&mut b));return move b;
    }
    fn test_domains() -> Unit {
        let h: Host=domain(utf8_bytes("*.Example.COM."),true);assert(h.length == 11);
        let invalid: Host=domain(utf8_bytes("a..com"),false);assert(invalid.length == 0);
        let literal: Host=domain(utf8_bytes("*.example.com"),false);assert(literal.length == 0);
        let mut index: DomainIndex=zero<DomainIndex>();assert(host_rules(&mut index,utf8_bytes("# rules\nexample.com\n0.0.0.0 other.example.net\n")));
        let sub: Host=domain(utf8_bytes("a.EXAMPLE.COM"),false);let bad: Host=domain(utf8_bytes("badexample.com"),false);
        assert(domain_matches(&index,&sub));assert(!domain_matches(&index,&bad));
        let a: Prefix=parse_prefix(utf8_bytes("192.0.2.123/24"));assert(a.valid && a.bits == 24);
        let b: Prefix=parse_prefix(utf8_bytes("2001:db8::1/32"));assert(b.valid && b.bits == 32);
        let c: Prefix=parse_prefix(utf8_bytes("::ffff:192.0.2.1/128"));assert(c.valid);
        let mut ips: IpIndex=zero<IpIndex>();ip_add(&mut ips,&b);assert(ip_contains(&ips,&b.address,6));assert(!ip_contains(&ips,&a.address,4));
        let all: Prefix=parse_prefix(utf8_bytes("0.0.0.0/0"));ip_add(&mut ips,&all);assert(ip_contains(&ips,&a.address,4));
        let invalid_cidr: Prefix=parse_prefix(utf8_bytes("192.0.2.1/33"));assert(!invalid_cidr.valid);
        let invalid_v6: Prefix=parse_prefix(utf8_bytes("1::2::3"));assert(!invalid_v6.valid);
        let mut policy: Policy=zero<Policy>();assert(host_rules(&mut policy.hosts,utf8_bytes("example.com")));ip_add(&mut policy.excluded_ips,&a);
        assert(!policy_allows(&policy,&sub,&a.address,4,false,true,false));
        policy.excluded_ips=zero<IpIndex>();assert(host_rules(&mut policy.excluded_hosts,utf8_bytes("a.example.com")));ip_add(&mut policy.ips,&a);
        assert(!policy_allows(&policy,&sub,&a.address,4,true,true,false));
        assert(!policy_udp(&policy,&a.address,4,true));
        let mut aws: IpIndex=zero<IpIndex>();
        assert(aws_rules(&mut aws,utf8_bytes("{\"prefixes\":[{\"ip_prefix\":\"192.0.2.0/24\",\"service\":\"EC2\"},{\"ip_prefix\":\"198.51.100.0/24\",\"service\":\"S3\"}],\"ipv6_prefixes\":[{\"ipv6_prefix\":\"2001:db8::/32\",\"service\":\"EC2\"}]}"),utf8_bytes("ec2")));
        assert(ip_contains(&aws,&a.address,4));assert(ip_contains(&aws,&b.address,6));
    }
    fn test_options() -> Unit {
        let mut o: Options=default_options();assert(option(&mut o,utf8_bytes("--split-strategy=semantic-multi")));assert(o.strategy == U32(1));
        assert(!option(&mut o,utf8_bytes("--quic-probe-ms=199")));assert(!option(&mut o,utf8_bytes("--min-ttl=-1")));assert(!option(&mut o,utf8_bytes("--first-packets=1025")));
        assert(option(&mut o,utf8_bytes("--quic-probe-ms=750")));assert(option(&mut o,utf8_bytes("--auto-ttl=2,4,16")));
        assert(option(&mut o,utf8_bytes("--hostlist=M:\\users\\Ark\\Desktop\\DLM\\DPI\\data\\список.txt")));
        assert(option(&mut o,utf8_bytes("--quic-mode=auto")));let saved: Buffer<U8>=save_settings(&o);let mut copy: Options=default_options();
        assert(load_settings(&mut copy,slice(&saved,0,len(&saved))));assert(copy.strategy == o.strategy && copy.ttl_margin == U32(2) && copy.quic == U32(1));
        assert(bytes_equal(slice(&copy.paths[0],0,len(&copy.paths[0])),slice(&o.paths[0],0,len(&o.paths[0]))));
        assert(option(&mut copy,utf8_bytes("--game-mode")));finish_options(&mut copy);assert(copy.quic == U32(0) && !copy.http);
    }
    fn test_inspection() -> Unit {
        let hello: Buffer<U8>=fake_hello();let parsed: Hello=inspect_tls(slice(&hello,0,len(&hello)));
        assert(parsed.recognized && parsed.complete && !parsed.malformed && parsed.host.length == 15);
        for n in 0..len(&hello) { let h: Hello=inspect_tls(slice(&hello,0,n));assert(!h.complete); }
        let request: Hello=inspect_http(utf8_bytes("GET / HTTP/1.1\r\nHost: Example.com:443\r\n\r\n"));assert(request.complete && request.host.length == 11);
        let duplicate: Hello=inspect_http(utf8_bytes("GET / HTTP/1.1\r\nHost: a.com\r\nHost: b.com\r\n\r\n"));assert(duplicate.malformed);
        for ipv6 in 0..2 {
            let packet: Buffer<U8>=tcp_packet(slice(&hello,0,len(&hello)),4294967294,24,ipv6 == 1);let view: Packet=parse_packet(slice(&packet,0,len(&packet)));
            assert(view.valid && view.transformable);let plan: Plan=hello_plan(&parsed,len(&hello),true);
            let parts: Batch=tcp_segments(slice(&packet,0,len(&packet)),&view,&plan,true);assert(parts.count >= 3);
            let mut offset: USize=0;let mut total: USize=0;
            for i in 0..parts.count {
                let n: USize=cast<USize>(parts.lengths[i]);let p: Packet=parse_packet(slice(&parts.bytes,offset,n));assert(p.valid);total=total+p.payload_length;
                if ipv6 == 0 { assert(checksum(slice(&parts.bytes,offset,p.ip_header)) == U16(0)); }offset=offset+n;
            }assert(total == len(&hello));
            let fake: Batch=fake_tcp(slice(&packet,0,len(&packet)),&view,true,0,true);assert(fake.count == 1);
            let f: Packet=parse_packet(slice(&fake.bytes,0,len(&fake.bytes)));assert(f.sequence == wrapping_add(view.sequence,U32(0x80000000)));
            if ipv6 == 0 { let fragments: Batch=ip_fragments(slice(&packet,0,len(&packet)),&view,24,48,true,42);assert(fragments.count == 3); }
        }
        assert(sequence_before(4294967290,4));assert(!sequence_before(4,4294967290));
        assert(fake_ttl(60,2,0,16,0,0) == U8(2));assert(fake_ttl(64,2,0,16,0,0) == U8(0));
    }
    fn test_stream() -> Unit {
        let mut s: Stream=zero<Stream>();let a: AppendResult=stream_append(&mut s,4294967293,0,utf8_bytes("def"));assert(a.accepted && s.contiguous == 0);
        let b: AppendResult=stream_append(&mut s,4294967293,4294967293,utf8_bytes("abc"));assert(b.accepted && s.contiguous == 6);
        let repeated: AppendResult=stream_append(&mut s,4294967293,0,utf8_bytes("def"));assert(repeated.repeated && !repeated.changed);
        let conflict: AppendResult=stream_append(&mut s,4294967293,0,utf8_bytes("Xef"));assert(conflict.conflict);
        let too_far: AppendResult=stream_append(&mut s,0,65535,utf8_bytes("xx"));assert(!too_far.accepted);
        let mut flow: Flow=zero<Flow>();flow.base=100;flow.started=1;assert(!note_send(&mut flow,100,80,1000,true));
        assert(note_send(&mut flow,100,80,1250,true));assert(flow.level == U8(1));assert(!note_send(&mut flow,100,80,1300,true));
        flow.progress=true;assert(!note_send(&mut flow,100,80,1600,true));
    }
    fn udp_packet(body: Slice<U8>) -> Buffer<U8> {
        let mut b: Buffer<U8>=buffer<U8>(28+len(body));b[0]=69;b[8]=64;b[9]=17;b[12]=10;b[15]=1;b[16]=192;b[18]=2;b[19]=1;
        let n: U16=cast<U16>(len(&b));write_be<U16>(&mut b,2,n);write_be<U16>(&mut b,20,12345);write_be<U16>(&mut b,22,443);
        write_be<U16>(&mut b,24,cast<U16>(len(body)+8));for i in 0..len(body) { b[28+i]=body[i]; }assert(checksums(&mut b));return move b;
    }
    fn test_quic() -> Unit {
        let mut initial: Buffer<U8>=buffer<U8>(1200);initial[0]=192;write_be<U32>(&mut initial,1,1);initial[5]=8;initial[14]=8;
        for i in 0..8 { initial[6+i]=cast<U8>(i+1);initial[15+i]=cast<U8>(i+11); }initial[24]=68;initial[25]=150;
        let parsed: QuicInitial=inspect_quic(slice(&initial,0,len(&initial)),true);assert(parsed.valid && parsed.dcid_length == 8 && parsed.scid_length == 8);
        let mut b: Buffer<U8>=udp_packet(slice(&initial,0,len(&initial)));let mut o: Options=default_options();o.auto_strategy=false;let p: Policy=zero<Policy>();let mut e: Engine=engine_new(move o,move p);
        e.options.quic=1;let first: Dispatch=evaluate(&mut e,&mut b,1,0,true,false,1000,false);assert(!first.consumed);
        let early: Dispatch=evaluate(&mut e,&mut b,1,0,true,false,1200,false);assert(!early.consumed);
        let fallback: Dispatch=evaluate(&mut e,&mut b,1,0,true,false,1800,false);assert(fallback.consumed && fallback.inbound && fallback.packets.count == 1);
        let icmp: Packet=parse_packet(slice(&fallback.packets.bytes,0,len(&fallback.packets.bytes)));assert(icmp.valid && icmp.protocol == U8(1));
        e.options.quic=2;let drop: Dispatch=evaluate(&mut e,&mut b,1,0,true,false,2000,false);assert(drop.consumed && drop.packets.count == 0);
        e.options.quic=3;let fragments: Dispatch=evaluate(&mut e,&mut b,1,0,true,false,2100,false);assert(fragments.consumed && fragments.packets.count == 3);
        e.options.quic=0;let pass: Dispatch=evaluate(&mut e,&mut b,1,0,true,false,2200,false);assert(!pass.consumed);
        initial[0]=208;write_be<U32>(&mut initial,1,0x6b3343cf);let v2: QuicInitial=inspect_quic(slice(&initial,0,len(&initial)),true);assert(v2.valid);
    }
    fn test_engine() -> Unit {
        let mut o: Options=default_options();o.auto_strategy=false;let p: Policy=zero<Policy>();let mut e: Engine=engine_new(move o,move p);let hello: Buffer<U8>=fake_hello();
        let mut syn: Buffer<U8>=tcp_packet(utf8_bytes(""),99,2,false);let s: Dispatch=evaluate(&mut e,&mut syn,1,0,true,false,1000,false);assert(!s.consumed);
        let mut packet: Buffer<U8>=tcp_packet(slice(&hello,0,len(&hello)),100,24,false);let d: Dispatch=evaluate(&mut e,&mut packet,1,0,true,false,1100,false);
        assert(d.consumed && d.packets.count == 2 && e.statistics.transformed == U64(1));assert(e.memory == 0);
        let retry: Dispatch=evaluate(&mut e,&mut packet,1,0,true,false,1400,false);assert(retry.consumed && retry.packets.count > 2 && e.statistics.escalations == U64(1));
        let mut server_hello: Buffer<U8>=buffer<U8>(9);server_hello[0]=22;server_hello[1]=3;server_hello[2]=3;server_hello[4]=4;server_hello[5]=2;
        let mut response: Buffer<U8>=tcp_packet(slice(&server_hello,0,len(&server_hello)),1,24,false);
        for i in 0..4 { let source: U8=response[12+i];response[12+i]=response[16+i];response[16+i]=source; }
        write_be<U16>(&mut response,20,443);write_be<U16>(&mut response,22,12345);write_be<U32>(&mut response,28,176);assert(checksums(&mut response));
        let reply: Dispatch=evaluate(&mut e,&mut response,1,0,false,false,1500,false);assert(!reply.consumed && e.statistics.learned == U64(1));
        let acknowledged: Dispatch=evaluate(&mut e,&mut packet,1,0,true,false,1550,false);assert(!acknowledged.consumed);
        e.options.jitter_ms=250;
        let mut second: Buffer<U8>=tcp_packet(slice(&hello,0,len(&hello)),800,24,true);let delayed: Dispatch=evaluate(&mut e,&mut second,1,0,true,false,1600,false);
        if delayed.delay > U64(1600) { let sent: Dispatch=evaluate(&mut e,&mut second,1,0,true,false,delayed.delay,true);assert(sent.consumed); }
        let mut unknown: Buffer<U8>=tcp_packet(utf8_bytes("opaque"),900,24,false);let pass: Dispatch=evaluate(&mut e,&mut unknown,1,0,true,false,1700,false);assert(!pass.consumed);
        let mut bad: Buffer<U8>=buffer<U8>(19);let bypass: Dispatch=evaluate(&mut e,&mut bad,1,0,true,false,1700,false);assert(!bypass.consumed);
        engine_expire(&mut e,200000,16384);assert(e.memory == 0);
    }
    fn main() -> Unit { test_domains();test_options();test_inspection();test_stream();test_quic();test_engine();print("DPI DLM: all core tests passed"); }
}
