module tests.telegram_checks
import tests.evolution_checks
import dpi.mtproto
import dpi.capture

native {
    fn telegram_packet(body: Slice<U8>, seq: U32, flags: U8, ipv6: Bool) -> Buffer<U8> {
        let mut b: Buffer<U8>=tcp_packet(body,seq,flags,ipv6);let mut ip: Prefix=parse_prefix(utf8_bytes("149.154.175.50"));let mut offset: USize=16;
        if ipv6 { ip=parse_prefix(utf8_bytes("2001:b28:f23d:f001::a"));offset=24; }
        let mut count: USize=4;if ipv6 { count=16; }for i in 0..count { b[offset+i]=ip.address[i]; }assert(checksums(&mut b));return move b;
    }
    fn telegram_policy() -> Policy {
        let mut p: Policy=zero<Policy>();assert(ip_rules(&mut p.telegram_ips,utf8_bytes("149.154.160.0/20\n2001:b28:f23d::/48\n")));return move p;
    }
    fn assert_telegram_bytes(d: &Dispatch, body: Slice<U8>, sequence: U32) -> Unit {
        assert(d.consumed && d.packets.count > 1 && d.fake.count == 0 && !d.modified);let mut offset: USize=0;let mut covered: Buffer<U8>=buffer<U8>(len(body));
        for i in 0..d.packets.count {
            let size: USize=cast<USize>(d.packets.lengths[i]);let raw: Slice<U8>=slice(&d.packets.bytes,offset,size);let v: Packet=parse_packet(raw);assert(v.valid && (v.flags & U8(6)) == U8(0));
            let at: USize=cast<USize>(wrapping_sub(v.sequence,sequence));assert(at <= len(body) && v.payload_length <= len(body)-at);
            for j in 0..v.payload_length { assert(raw[v.payload+j] == body[at+j]);covered[at+j]=1; }
            let mut repaired: Buffer<U8>=bytes_copy(raw);assert(checksums(&mut repaired));assert(bytes_equal(raw,slice(&repaired,0,len(&repaired))));offset=offset+size;
        }for i in 0..len(body) { assert(covered[i] == U8(1)); }
    }
    fn test_telegram_domains_profiles() -> Unit {
        let good: Host=domain(utf8_bytes("pluto-1.web.telegram.org"),false);let spoof: Host=domain(utf8_bytes("telegram.org.example.com"),false);
        let yt: Host=domain(utf8_bytes("www.youtube.com"),false);let dc: Host=domain(utf8_bytes("discord.com"),false);
        assert(telegram_host(&good) && automatic_known(&good) && !telegram_host(&spoof));
        for t in 0..5 { let n: U32=cast<U32>(t);assert(gene_applies(&yt,n) == target_youtube(n));assert(gene_applies(&dc,n) == target_discord(n));assert(gene_applies(&good,n) == target_telegram(n)); }
        let mut g: Gene=gene_default();let mut target: U32=0;assert(profile_gene(utf8_bytes("[DPIProfile]\nSchema=1\nTarget=3\nGene=1,0,0,0,0,0,1048576,0\n"),&mut g,&mut target) && target == U32(3));
        assert(!profile_gene(utf8_bytes("[DPIProfile]\nSchema=1\nTarget=7\nGene=1,0,0,0,0,0,1048576,0\n"),&mut g,&mut target));
        let mut o: Options=default_options();automatic_options(&mut o);o.gene_mode=true;o.gene_target=3;let p: Policy=zero<Policy>();let mut e: Engine=engine_new(move o,move p);
        let hello: Buffer<U8>=hello_for(utf8_bytes("www.youtube.com"));let mut b: Buffer<U8>=tcp_packet(slice(&hello,0,len(&hello)),100,24,false);let d: Dispatch=evaluate(&mut e,&mut b,1,0,true,false,1000,false);
        assert(d.consumed && d.packets.count > 1);let i: USize=automatic_index(&mut e,&b,1001);assert(e.flows[i].arm != U32(0));let again: Dispatch=evaluate(&mut e,&mut b,1,0,true,false,1500,false);assert(again.consumed && e.flows[i].level == U8(1));
        let mut view: TrainingView=zero<TrainingView>();assert(training_view(utf8_bytes("target=3\ntelegram_web_passed=4\ntelegram_web_total=6\ntelegram_desktop_passed=2\ntelegram_desktop_total=2\n"),&mut view));assert(view.target == U32(3) && view.telegram_desktop_passed == U32(2));
        assert(!training_view(utf8_bytes("TelegramWebPassed=5\nTelegramWebTotal=4\n"),&mut view));
    }
    fn test_telegram_startup_packets() -> Unit {
        let mut body: Buffer<U8>=buffer<U8>(128);for i in 0..128 { body[i]=cast<U8>((i*7+53)%256); }body[0]=42;
        for version in 0..2 { for mode in 0..5 {
            let mut o: Options=default_options();automatic_options(&mut o);o.gene_mode=mode > 0;o.gene_target=3;o.gene.cut1=1;o.gene.cut2=32;o.gene.overlap=cast<U32>(mode*8);o.gene.reverse=cast<U32>(mode%2);
            let p: Policy=telegram_policy();let mut e: Engine=engine_new(move o,move p);let mut syn: Buffer<U8>=telegram_packet(utf8_bytes(""),4294967289,2,version == 1);
            let a: Dispatch=evaluate(&mut e,&mut syn,1,0,true,false,1000,false);assert(!a.consumed && !a.modified);
            let mut b: Buffer<U8>=telegram_packet(slice(&body,0,len(&body)),4294967290,24,version == 1);let d: Dispatch=evaluate(&mut e,&mut b,1,0,true,false,1100,false);assert_telegram_bytes(&d,slice(&body,0,len(&body)),4294967290);
            let i: USize=automatic_index(&mut e,&b,1101);assert(e.flows[i].telegram && e.flows[i].telegram_transport == U32(4));assert(e.flows[i].arm <= U32(2));
            let mut reply: Buffer<U8>=telegram_packet(utf8_bytes("response"),500,24,version == 1);let mut source: USize=12;let mut destination: USize=16;let mut address_size: USize=4;let mut header: USize=20;
            if version == 1 { source=8;destination=24;address_size=16;header=40; }for j in 0..address_size { let old: U8=reply[source+j];reply[source+j]=reply[destination+j];reply[destination+j]=old; }write_be<U16>(&mut reply,header,443);write_be<U16>(&mut reply,header+2,12345);assert(checksums(&mut reply));
            let incoming: Dispatch=evaluate(&mut e,&mut reply,1,0,false,false,1200,false);assert(!incoming.consumed && e.flows[i].progress && e.flows[i].stopped);
            write_be<U32>(&mut reply,header+4,508);assert(checksums(&mut reply));let more: Dispatch=evaluate(&mut e,&mut reply,1,0,false,false,1250,false);assert(!more.consumed);let mut response_events: USize=0;for j in 0..len(&e.events) { if e.events[j].kind == U32(28) { response_events=response_events+1; } }assert(response_events == 1);
            let after: Dispatch=evaluate(&mut e,&mut b,1,0,true,false,1300,false);assert(!after.consumed && e.flows[i].stopped);
        } }
        for kind in 0..6 {
            let mut o: Options=default_options();automatic_options(&mut o);if kind == 3 { o.telegram=false; }
            let mut p: Policy=telegram_policy();if kind == 4 { assert(ip_rules(&mut p.excluded_ips,utf8_bytes("149.154.175.50/32\n"))); }if kind == 5 { assert(host_rules(&mut p.excluded_hosts,utf8_bytes("example.com\n"))); }
            let mut e: Engine=engine_new(move o,move p);let mut syn: Buffer<U8>=telegram_packet(utf8_bytes(""),99,2,false);
            if kind != 0 { let start: Dispatch=evaluate(&mut e,&mut syn,1,0,true,false,1000,false);assert(!start.consumed); }
            let mut b: Buffer<U8>=telegram_packet(slice(&body,0,len(&body)),100,24,false);
            if kind == 1 { b[16]=10;assert(checksums(&mut b)); }if kind == 2 { b[16]=8;assert(checksums(&mut b)); }
            let pass: Dispatch=evaluate(&mut e,&mut b,1,0,true,false,1100,false);assert(!pass.consumed && pass.fake.count == 0);
        }
        let mut o: Options=default_options();automatic_options(&mut o);let p: Policy=telegram_policy();let mut e: Engine=engine_new(move o,move p);
        let mut syn: Buffer<U8>=telegram_packet(utf8_bytes(""),99,2,false);let a: Dispatch=evaluate(&mut e,&mut syn,1,0,true,false,1000,false);assert(!a.consumed);
        let hello: Buffer<U8>=hello_for(utf8_bytes("pluto.web.telegram.org"));let mut tls: Buffer<U8>=telegram_packet(slice(&hello,0,len(&hello)),100,24,false);let d: Dispatch=evaluate(&mut e,&mut tls,1,0,true,false,1100,false);assert(d.consumed);let i: USize=automatic_index(&mut e,&tls,1101);assert(e.flows[i].tls && !e.flows[i].telegram);
        let mut flow: Flow=zero<Flow>();flow.syn=true;flow.started=1000;assert(telegram_startup(&flow,0,64,1001));assert(!telegram_startup(&flow,512,64,1001));assert(!telegram_startup(&flow,0,64,4001));assert(!telegram_startup(&flow,0,4097,1001));flow.packets=6;assert(!telegram_startup(&flow,0,64,1001));
    }
    fn test_telegram_split_prefix() -> Unit {
        for kind in 0..2 {
            let mut o: Options=default_options();automatic_options(&mut o);let p: Policy=telegram_policy();let mut e: Engine=engine_new(move o,move p);
            let mut syn: Buffer<U8>=telegram_packet(utf8_bytes(""),99,2,false);let start: Dispatch=evaluate(&mut e,&mut syn,1,0,true,false,1000,false);assert(!start.consumed);
            let mut body: Buffer<U8>=buffer<U8>(128);for i in 0..128 { body[i]=cast<U8>((i*7+42)%256); }let mut first_size: USize=6;
            if kind == 0 { body[0]=239;first_size=1; }
            let mut first: Buffer<U8>=telegram_packet(slice(&body,0,first_size),100,24,false);let a: Dispatch=evaluate(&mut e,&mut first,1,0,true,false,1100,false);assert(!a.consumed);
            let index: USize=automatic_index(&mut e,&first,1101);assert(!e.flows[index].stopped);
            let mut second: Buffer<U8>=telegram_packet(slice(&body,first_size,len(&body)-first_size),100+cast<U32>(first_size),24,false);let b: Dispatch=evaluate(&mut e,&mut second,1,0,true,false,1200,false);
            assert_telegram_bytes(&b,slice(&body,first_size,len(&body)-first_size),100+cast<U32>(first_size));assert(e.flows[index].telegram && e.memory == 0);
        }
    }
    fn test_mtproto_codec() -> Unit {
        let mut context: Buffer<U8>=buffer<U8>(24);for i in 0..16 { context[i]=cast<U8>(i+1); }write_le<U64>(&mut context,16,0x6500000000000000);
        let request: Buffer<U8>=mtp_request(slice(&context,0,len(&context)));assert(len(&request) == 41 && request[0] == U8(10));assert(read_le<U64>(&request,1) == U64(0) && read_le<U32>(&request,17) == U32(20) && read_le<U32>(&request,21) == U32(0xbe7e8ef1));assert(bytes_at(slice(&request,0,len(&request)),25,slice(&context,0,16)));
        let bad_context: Buffer<U8>=mtp_request(slice(&context,0,23));assert(len(&bad_context) == 0);
        let mut response: Buffer<U8>=buffer<U8>(84);write_le<U64>(&mut response,8,0x6500000000000001);write_le<U32>(&mut response,16,64);write_le<U32>(&mut response,20,0x05162463);
        for i in 0..16 { response[24+i]=context[i];response[40+i]=cast<U8>(i+9); }response[56]=8;for i in 0..8 { response[57+i]=cast<U8>(i+2); }
        write_le<U32>(&mut response,68,0x1cb5c415);write_le<U32>(&mut response,72,1);write_le<U64>(&mut response,76,123);
        assert(mtp_verify(slice(&context,0,16),slice(&response,0,len(&response))) == U32(0));
        for n in 0..len(&response) { assert(mtp_verify(slice(&context,0,16),slice(&response,0,n)) != U32(0)); }
        response[24]=99;assert(mtp_verify(slice(&context,0,16),slice(&response,0,len(&response))) == U32(2));response[24]=context[0];
        response[56]=9;assert(mtp_verify(slice(&context,0,16),slice(&response,0,len(&response))) == U32(3));response[56]=8;
        write_le<U32>(&mut response,72,33);assert(mtp_verify(slice(&context,0,16),slice(&response,0,len(&response))) == U32(3));write_le<U32>(&mut response,72,1);
        response[0]=1;assert(mtp_verify(slice(&context,0,16),slice(&response,0,len(&response))) == U32(1));response[0]=0;write_le<U32>(&mut response,16,60);assert(mtp_verify(slice(&context,0,16),slice(&response,0,len(&response))) == U32(1));
        let mut error: Buffer<U8>=buffer<U8>(4);write_le<U32>(&mut error,0,wrapping_sub(U32(0),U32(429)));assert(mtp_verify(slice(&context,0,16),slice(&error,0,4)) == U32(4));
        let mut random: U64=111;for n in 0..500 { let mut malformed: Buffer<U8>=buffer<U8>(n%128);for i in 0..len(&malformed) { malformed[i]=cast<U8>(evolution_random(&mut random)%U32(256)); }assert(mtp_verify(slice(&context,0,16),slice(&malformed,0,len(&malformed))) != U32(0)); }
    }
    fn test_telegram_capture() -> Unit {
        let mut o: Options=default_options();let enabled: Buffer<U8>=divert_filter(&o);assert(bytes_find(slice(&enabled,0,len(&enabled)),0,utf8_bytes("5222")) < len(&enabled));
        o.telegram=false;let disabled: Buffer<U8>=divert_filter(&o);assert(bytes_find(slice(&disabled,0,len(&disabled)),0,utf8_bytes("5222")) == len(&disabled));
        assert(telegram_transport(utf8_bytes("GET / HTTP/1.1\r\n")) == U32(0));let mut marker: Buffer<U8>=buffer<U8>(8);marker[0]=239;assert(telegram_transport(slice(&marker,0,8)) == U32(1));write_le<U32>(&mut marker,0,0xeeeeeeee);assert(telegram_transport(slice(&marker,0,8)) == U32(2));write_le<U32>(&mut marker,0,0xdddddddd);assert(telegram_transport(slice(&marker,0,8)) == U32(3));
    }
    fn main() -> Unit { test_telegram_domains_profiles();test_telegram_startup_packets();test_telegram_split_prefix();test_mtproto_codec();test_telegram_capture();print("DPI DLM 2.3: Telegram scope, exact TCP bytes, profile compatibility and MTProto verifier passed"); }
}
