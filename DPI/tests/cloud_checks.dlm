module tests.cloud_checks
import tests.automatic_checks
import dpi.training_view
native {
    fn test_cloud_ranges_scope() -> Unit {
        let mut p: Policy=zero<Policy>();assert(cloud_rules(&mut p.cloudflare_ips,utf8_bytes("104.16.0.0/13\n2606:4700::/32\n")));let saved: USize=p.cloudflare_ips.count;
        let bad: Array<Utf8,7>=array<Array<Utf8,7>>("0.0.0.0/0","10.0.0.0/8","104.16.0.1/13","::/0","100::/64","bad","104.16.0.1");
        for i in 0..7 { assert(!cloud_rules(&mut p.cloudflare_ips,utf8_bytes(bad[i])));assert(p.cloudflare_ips.count == saved); }
        assert(cloud_rules(&mut p.azure_cloud_ips,utf8_bytes("20.0.0.0/8")));
        let h: Host=domain(utf8_bytes("unlisted.example"),false);let cf: Prefix=parse_prefix(utf8_bytes("104.16.1.2"));let azure: Prefix=parse_prefix(utf8_bytes("20.1.2.3"));let mut f: Flow=zero<Flow>();f.host=h;let mut v: Packet=zero<Packet>();v.destination=cf.address;v.version=4;
        let mut o: Options=default_options();automatic_options(&mut o);f.cloud_provider=cloud_provider(&p,&h,&v.destination,4);assert(f.cloud_provider == U32(1) && cloud_scope(&o,&p,&f,&v));
        v.destination=azure.address;f.cloud_provider=cloud_provider(&p,&h,&v.destination,4);assert(f.cloud_provider == U32(4) && !cloud_scope(&o,&p,&f,&v));f.mwo_owned=true;assert(cloud_scope(&o,&p,&f,&v));f.mwo_owned=false;
        f.host=domain(utf8_bytes("x.azurewebsites.net"),false);assert(cloud_scope(&o,&p,&f,&v));o.cloud_cdn=false;assert(!cloud_scope(&o,&p,&f,&v));f.host=domain(utf8_bytes("ac.mwomercs.com"),false);assert(cloud_scope(&o,&p,&f,&v));o.cloud_cdn=true;
        let protected_: Array<Utf8,4>=array<Array<Utf8,4>>("1.1.1.1","1.0.0.1","2606:4700:4700::1111","::ffff:1.1.1.1");for i in 0..4 { let ip: Prefix=parse_prefix(utf8_bytes(protected_[i]));assert(ip.valid && cloud_protected_ip(&ip.address,ip.version));v.destination=ip.address;v.version=ip.version;assert(!cloud_scope(&o,&p,&f,&v)); }
        v.destination=cf.address;v.version=4;f.host=domain(utf8_bytes("www.youtube.com"),false);assert(!cloud_scope(&o,&p,&f,&v));f.host=domain(utf8_bytes("chrome.cloudflare-dns.com"),false);assert(!cloud_scope(&o,&p,&f,&v));
        f.host=zero<Host>();f.cloud_provider=1;assert(cloud_scope(&o,&p,&f,&v));assert(host_rules(&mut p.excluded_hosts,utf8_bytes("example.com")));assert(!cloud_scope(&o,&p,&f,&v));
    }
    fn test_cloud_target_contract() -> Unit {
        let mwo: Host=domain(utf8_bytes("ac.mwomercs.com"),false);let fake: Host=domain(utf8_bytes("ac.mwomercs.com.evil.net"),false);let front: Host=domain(utf8_bytes("d123.cloudfront.net"),false);let spoof: Host=domain(utf8_bytes("evilcloudfront.net"),false);
        assert(mwo_host(&mwo) && !mwo_host(&fake));assert(cloud_host_family(&front) == U32(2) && cloud_host_family(&spoof) == U32(0));for i in 0..5 { assert(!gene_applies(&mwo,cast<U32>(i))); }assert(gene_applies(&mwo,5) && gene_applies(&front,6));assert(!target_telegram(5) && !target_youtube(5) && !target_discord(5));assert(target_telegram(4) && !target_mwo(4) && target_mwo(6));
        let mut o: Options=default_options();assert(o.cloud_cdn);assert(option(&mut o,utf8_bytes("--no-cloud-cdn")));assert(!o.cloud_cdn);let b: Buffer<U8>=save_settings(&o);let mut loaded: Options=default_options();assert(load_settings(&mut loaded,slice(&b,0,len(&b))) && !loaded.cloud_cdn);assert(option(&mut loaded,utf8_bytes("--gene-target=6")) && !option(&mut loaded,utf8_bytes("--gene-target=7")));
    }
    fn cloud_request(name: Slice<U8>, destination: Slice<U8>, ipv6: Bool) -> Buffer<U8> {
        let body: Buffer<U8>=hello_for(name);let mut b: Buffer<U8>=tcp_packet(slice(&body,0,len(&body)),4294967290,24,ipv6);let ip: Prefix=parse_prefix(destination);let mut at: USize=16;let mut n: USize=4;if ipv6 { at=24;n=16; }for i in 0..n { b[at+i]=ip.address[i]; }assert(checksums(&mut b));return move b;
    }
    fn test_cloud_packet_invariants() -> Unit {
        for version in 0..2 { for variant in 0..12 {
            let mut o: Options=default_options();automatic_options(&mut o);o.gene_mode=true;o.gene_target=5;o.gene=Gene(cast<U32>(variant%2),1,5,cast<U32>(variant*2),1,3,65536,2);finish_options(&mut o);
            let mut p: Policy=zero<Policy>();assert(cloud_rules(&mut p.cloudflare_ips,utf8_bytes("104.16.0.0/13\n2606:4700::/32")));let mut e: Engine=engine_new(move o,move p);let mut ip: Utf8="104.16.1.2";if version == 1 { ip="2606:4700::1234"; }
            let mut b: Buffer<U8>=cloud_request(utf8_bytes("unlisted.example"),utf8_bytes(ip),version == 1);let original: Buffer<U8>=bytes_copy(slice(&b,0,len(&b)));let v: Packet=parse_packet(slice(&b,0,len(&b)));let result: Dispatch=evaluate(&mut e,&mut b,1,0,true,false,1000,false);
            assert(result.consumed && !result.modified && result.fake.count == 0 && result.packets.count > 1);let at: USize=automatic_index(&mut e,&b,1001);assert(at < len(&e.flows) && e.flows[at].cloud_scope);let mut covered: Buffer<U8>=buffer<U8>(v.payload_length);let mut offset: USize=0;
            for i in 0..result.packets.count { let n: USize=cast<USize>(result.packets.lengths[i]);let raw: Slice<U8>=slice(&result.packets.bytes,offset,n);let part: Packet=parse_packet(raw);assert(part.valid);let start: USize=cast<USize>(wrapping_sub(part.sequence,v.sequence));assert(start+part.payload_length <= v.payload_length);for j in 0..part.payload_length { assert(raw[part.payload+j] == original[v.payload+start+j]);covered[start+j]=1; }offset=offset+n; }
            for i in 0..v.payload_length { assert(covered[i] == U8(1)); }
        } }
        let mut o: Options=default_options();automatic_options(&mut o);let mut p: Policy=zero<Policy>();assert(host_rules(&mut p.excluded_hosts,utf8_bytes("mwomercs.com")));let mut e: Engine=engine_new(move o,move p);let mut b: Buffer<U8>=cloud_request(utf8_bytes("ac.mwomercs.com"),utf8_bytes("104.16.1.2"),false);let r: Dispatch=evaluate(&mut e,&mut b,1,0,true,false,1000,false);assert(!r.consumed && !r.modified);
        let mut o2: Options=default_options();automatic_options(&mut o2);let p2: Policy=zero<Policy>();let mut e2: Engine=engine_new(move o2,move p2);let mut private_: Buffer<U8>=cloud_request(utf8_bytes("ac.mwomercs.com"),utf8_bytes("10.0.0.2"),false);let untouched: Dispatch=evaluate(&mut e2,&mut private_,1,0,true,false,1000,false);assert(!untouched.consumed && !untouched.modified);
    }
    fn test_cloud_no_sni_and_legacy() -> Unit {
        let mut hello: Buffer<U8>=fake_hello();write_be<U16>(&mut hello,52,0x1234);let h: Hello=inspect_tls(slice(&hello,0,len(&hello)));assert(h.complete && !h.malformed && h.host.length == 0);
        for kind in 0..3 { let mut o: Options=default_options();automatic_options(&mut o);let mut p: Policy=zero<Policy>();if kind != 1 { assert(cloud_rules(&mut p.cloudflare_ips,utf8_bytes("192.0.2.0/24"))); }if kind == 2 { assert(host_rules(&mut p.excluded_hosts,utf8_bytes("example.com"))); }
            let mut e: Engine=engine_new(move o,move p);let mut b: Buffer<U8>=tcp_packet(slice(&hello,0,len(&hello)),100,24,false);let r: Dispatch=evaluate(&mut e,&mut b,1,0,true,false,1000,false);assert(r.consumed == (kind == 0) && r.fake.count == 0); }
        let mut o: Options=default_options();automatic_options(&mut o);o.gene_mode=true;o.gene_target=0;o.gene=Gene(1,1,5,8,1,2,65536,0);finish_options(&mut o);let mut p: Policy=zero<Policy>();assert(cloud_rules(&mut p.cloudflare_ips,utf8_bytes("192.0.2.0/24")));let mut e: Engine=engine_new(move o,move p);let body: Buffer<U8>=hello_for(utf8_bytes("www.youtube.com"));let mut b: Buffer<U8>=tcp_packet(slice(&body,0,len(&body)),100,24,false);let r: Dispatch=evaluate(&mut e,&mut b,1,0,true,false,1000,false);assert(r.consumed && r.fake.count == 2);let at: USize=automatic_index(&mut e,&b,1001);assert(at < len(&e.flows) && !e.flows[at].cloud_scope);
    }
    fn test_mwo_owner_table() -> Unit {
        assert(mwo_process_kind(utf8_bytes("C:\\MWO\\Bin64\\MWOClient.exe")) == U32(1));assert(mwo_process_kind(utf8_bytes("c:/MWO/mwoportal.EXE")) == U32(2));assert(mwo_process_kind(utf8_bytes("NotMWOClient.exe")) == U32(0));
        for version in 0..2 { let mut stride: USize=24;let mut state: USize=0;let mut lp: USize=8;let mut rp: USize=16;let mut remote: USize=12;let mut pid: USize=20;let mut n: USize=4;let mut local: USize=4;let mut family: U8=4;
            if version == 1 { stride=56;state=48;lp=20;rp=44;remote=24;pid=52;n=16;local=0;family=6; }
            let mut table: Buffer<U8>=buffer<U8>(4+stride);write_le<U32>(&mut table,0,1);write_le<U32>(&mut table,4+state,5);write_be<U16>(&mut table,4+lp,40000);write_be<U16>(&mut table,4+rp,443);write_le<U32>(&mut table,4+pid,8765);let mut key: FlowKey=zero<FlowKey>();key.version=family;key.protocol=6;key.source_port=40000;key.destination_port=443;
            for i in 0..n { key.source[i]=cast<U8>(i+1);key.destination[i]=cast<U8>(i+31);table[4+local+i]=key.source[i];table[4+remote+i]=key.destination[i]; }
            assert(mwo_socket_pid(slice(&table,0,len(&table)),&key) == U32(8765));key.source_port=40001;assert(mwo_socket_pid(slice(&table,0,len(&table)),&key) == U32(0));key.source_port=40000;assert(mwo_socket_pid(slice(&table,0,len(&table)-1),&key) == U32(0));write_le<U32>(&mut table,4+state,2);assert(mwo_socket_pid(slice(&table,0,len(&table)),&key) == U32(0));write_le<U32>(&mut table,0,4294967295);assert(mwo_socket_pid(slice(&table,0,len(&table)),&key) == U32(0));
        }
    }
    fn test_mwo_probe_semantics() -> Unit {
        assert(mwo_redirect(301,utf8_bytes("https://mwomercs.com/login")));assert(!mwo_redirect(301,utf8_bytes("https://mwomercs.com/login.evil")) && !mwo_redirect(403,utf8_bytes("https://mwomercs.com/login")));
        assert(mwo_content(utf8_bytes("mwo_login"),utf8_bytes("<title>MWO: Log In</title><input name=\"email\"><input name=\"password\">")));assert(!mwo_content(utf8_bytes("mwo_login"),utf8_bytes("<title>MWO:</title>Just a moment...")));
        let mut css: Buffer<U8>=buffer<U8>(32768);css[0]=123;css[1]=125;assert(mwo_content(utf8_bytes("mwo_public_css"),slice(&css,0,len(&css))));assert(!mwo_content(utf8_bytes("mwo_public_css"),slice(&css,0,16384)));
        let mut view: TrainingView=zero<TrainingView>();assert(training_view(utf8_bytes("target=5\npassed=8\ntotal=8\nattributable=1\nmwo_passed=8\nmwo_total=8\n"),&mut view));let clock: Buffer<U8>=training_clock(&view);assert(bytes_find(slice(&clock,0,len(&clock)),0,utf8_bytes("вход в игре вручную")) < len(&clock));assert(bytes_find(slice(&clock,0,len(&clock)),0,utf8_bytes("видео-CDN")) == len(&clock));
    }
    fn main() -> Unit { test_cloud_ranges_scope();test_cloud_target_contract();test_cloud_packet_invariants();test_cloud_no_sni_and_legacy();test_mwo_owner_table();test_mwo_probe_semantics();print("DPI 2.4 cloud scope, byte preservation, MWO ownership and probe semantics passed"); }
}
