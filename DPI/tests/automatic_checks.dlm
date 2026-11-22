module tests.automatic_checks
import tests.adaptive

native {
    fn hello_for(name: Slice<U8>) -> Buffer<U8> {
        let mut b: Buffer<U8>=fake_hello();resize(&mut b,61+len(name));for i in 0..len(name) { b[61+i]=name[i]; }
        let total: USize=len(&b);write_be<U16>(&mut b,3,cast<U16>(total-5));let length: U32=cast<U32>(total-9);b[6]=cast<U8>(length >> U32(16));b[7]=cast<U8>((length >> U32(8)) & U32(255));b[8]=cast<U8>(length & U32(255));
        write_be<U16>(&mut b,50,cast<U16>(total-52));write_be<U16>(&mut b,54,cast<U16>(len(name)+5));write_be<U16>(&mut b,56,cast<U16>(len(name)+3));write_be<U16>(&mut b,59,cast<U16>(len(name)));return move b;
    }
    fn automatic_index(e: &mut Engine, packet: &Buffer<U8>, now: U64) -> USize {
        let v: Packet=parse_packet(slice(packet,0,len(packet)));let key: FlowKey=FlowKey(v.source,v.destination,1,0,v.source_port,v.destination_port,v.version,v.protocol);return flow_find(&mut e.flows,&key,now);
    }
    fn test_automatic_scope() -> Unit {
        let mut hosts: Buffer<AutomaticHost>=buffer<AutomaticHost>(1024);let p: Policy=zero<Policy>();
        let yt: Host=domain(utf8_bytes("r1.googlevideo.com"),false);let foreign: Host=domain(utf8_bytes("www.example.com"),false);let ru: Host=domain(utf8_bytes("ya.ru"),false);
        let dns: Host=domain(utf8_bytes("chrome.cloudflare-dns.com"),false);let fake: Host=domain(utf8_bytes("googlevideo.com.example.com"),false);
        assert(automatic_candidate(&mut hosts,&yt,&p,100));assert(!automatic_candidate(&mut hosts,&foreign,&p,100));assert(!automatic_candidate(&mut hosts,&ru,&p,100));
        assert(automatic_protected(&dns));assert(!automatic_candidate(&mut hosts,&dns,&p,100));assert(!automatic_candidate(&mut hosts,&fake,&p,100));
        automatic_feedback(&mut hosts,&foreign,0,3,2,1000);automatic_feedback(&mut hosts,&foreign,0,2,13,2000);assert(!automatic_candidate(&mut hosts,&foreign,&p,3000));
        automatic_feedback(&mut hosts,&foreign,0,2,1,4000);assert(!automatic_candidate(&mut hosts,&foreign,&p,5000));automatic_feedback(&mut hosts,&foreign,0,2,7,6000);assert(automatic_candidate(&mut hosts,&foreign,&p,7000));
        for i in 0..6 { automatic_feedback(&mut hosts,&foreign,1,2,1,8000+cast<U64>(i)); }assert(!automatic_candidate(&mut hosts,&foreign,&p,9000));assert(automatic_candidate(&mut hosts,&foreign,&p,130000));
        automatic_feedback(&mut hosts,&foreign,0,1,4,140000);assert(!automatic_candidate(&mut hosts,&foreign,&p,150000));
        for i in 0..5 { automatic_feedback(&mut hosts,&dns,0,2,1,1000+cast<U64>(i)); }assert(!automatic_candidate(&mut hosts,&dns,&p,100000));
        let mut included: Policy=zero<Policy>();assert(host_rules(&mut included.hosts,utf8_bytes("example.com\n")));assert(automatic_candidate(&mut hosts,&foreign,&included,160000));
    }
    fn test_private_addresses() -> Unit {
        let cases: Array<Utf8,11>=array<Array<Utf8,11>>("10.0.0.1","127.0.0.1","192.168.1.1","172.16.9.1","100.64.1.1","169.254.1.2","::1","fe80::1","fd00::1234","ff02::1","::ffff:192.168.1.1");
        for i in 0..11 { let p: Prefix=parse_prefix(utf8_bytes(cases[i]));assert(p.valid && !automatic_public(&p.address,p.version)); }
        let v4: Prefix=parse_prefix(utf8_bytes("1.1.1.1"));let v6: Prefix=parse_prefix(utf8_bytes("2606:4700:4700::1111"));assert(automatic_public(&v4.address,4));assert(automatic_public(&v6.address,6));
    }
    fn test_automatic_packets() -> Unit {
        let cases: Array<Utf8,4>=array<Array<Utf8,4>>("chrome.cloudflare-dns.com","ya.ru","www.example.com","www.youtube.com");
        for i in 0..4 {
            let mut o: Options=default_options();o.server_window=true;o.quic=2;o.udp_obfuscate=true;o.fake=true;o.forced_arm=6;o.safe_automatic=true;finish_options(&mut o);
            assert(!o.server_window && o.quic == U32(1) && !o.udp_obfuscate && !o.fake && o.forced_arm == U32(8));
            let p: Policy=zero<Policy>();let mut e: Engine=engine_new(move o,move p);let mut syn: Buffer<U8>=tcp_packet(utf8_bytes(""),99,2,false);write_be<U16>(&mut syn,34,65535);assert(checksums(&mut syn));let original_syn: Buffer<U8>=bytes_copy(slice(&syn,0,len(&syn)));
            let start: Dispatch=evaluate(&mut e,&mut syn,1,0,true,false,900,false);assert(!start.modified && !start.consumed && bytes_equal(slice(&syn,0,len(&syn)),slice(&original_syn,0,len(&original_syn))));
            let body: Buffer<U8>=hello_for(utf8_bytes(cases[i]));let mut request: Buffer<U8>=tcp_packet(slice(&body,0,len(&body)),100,24,false);let original: Buffer<U8>=bytes_copy(slice(&request,0,len(&request)));
            let result: Dispatch=evaluate(&mut e,&mut request,1,0,true,false,1000,false);assert(!result.modified && result.fake.count == 0);
            if i < 3 { assert(!result.consumed && bytes_equal(slice(&request,0,len(&request)),slice(&original,0,len(&original)))); }else { assert(result.consumed && result.packets.count > 1); }
            if i == 2 { let at: USize=automatic_index(&mut e,&request,1100);assert(at < len(&e.flows) && e.flows[at].arm == U32(0));let isolated: Endpoint=trial_endpoint(&e,at);assert(isolated.host == host_hash(&e.flows[at].host) && isolated.host != U64(0));engine_tick(&mut e,10000,16384);assert(e.flows[at].trial_result == U32(2)); }
        }
    }
    fn test_no_legacy_fake_fallback() -> Unit {
        let mut o: Options=default_options();o.forced_arm=3;o.fake=true;o.fake_mode=1;o.ttl_margin=2;o.ttl_spread=4;o.ttl_max=16;
        let p: Policy=zero<Policy>();let mut e: Engine=engine_new(move o,move p);let body: Buffer<U8>=fake_hello();let mut request: Buffer<U8>=tcp_packet(slice(&body,0,len(&body)),100,24,false);
        let initial: Dispatch=evaluate(&mut e,&mut request,1,0,true,false,1000,false);assert(initial.fake.count == 1);let at: USize=automatic_index(&mut e,&request,1100);assert(at < len(&e.flows));e.flows[at].fake_count=2;e.flows[at].server_ttl=64;
        let retry: Dispatch=evaluate(&mut e,&mut request,1,0,true,false,1200,false);assert(retry.consumed && retry.fake.count == 0);
    }
    fn main() -> Unit { test_automatic_scope();test_private_addresses();test_automatic_packets();test_no_legacy_fake_fallback();print("DPI DLM 2.1: automatic scope and legacy-fake regression tests passed"); }
}
