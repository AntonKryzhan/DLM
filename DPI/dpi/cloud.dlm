module dpi.cloud
import dpi.automatic
native {
    fn cloud_rules(index: &mut IpIndex, text: Slice<U8>) -> Bool {
        let mut parsed: IpIndex=zero<IpIndex>();let mut at: USize=0;let mut count: USize=0;
        while at < len(text) { let end: USize=byte_find(text,at,10);let raw: Slice<U8>=slice(&text,at,end-at);let line: Slice<U8>=rule_line(raw);at=end+1;if len(line) == 0 { continue; }
            let p: Prefix=parse_prefix(line);if byte_find(line,0,47) == len(line) || !p.valid || (p.version == U8(6) && (p.address[0] & U8(0xe0)) != U8(0x20)) || (p.version == U8(4) && p.bits < 8) || (p.version == U8(6) && p.bits < 16) || !automatic_public(&p.address,p.version) { return false; }
            let mut bits: USize=32;if p.version == U8(6) { bits=128; }for i in p.bits..bits { if (p.address[i/8] & (U8(1) << cast<U8>(7-i%8))) != U8(0) { return false; } }
            count=count+1;if count > 16384 { return false; }ip_add(&mut parsed,&p);
        }if parsed.count == 0 { return false; }*index=move parsed;return true;
    }
    fn cloud_protected_ip(a: &Array<U8,16>, version: U8) -> Bool {
        if version == U8(4) { return a[0] == U8(1) && ((a[1] == U8(0) && a[2] == U8(0)) || (a[1] == U8(1) && a[2] == U8(1))); }
        if version == U8(6) { let mut mapped: Bool=true;for i in 0..10 { mapped=mapped && a[i] == U8(0); }if mapped && a[10] == U8(255) && a[11] == U8(255) { let mut v4: Array<U8,16>=zero<Array<U8,16>>();for i in 0..4 { v4[i]=a[12+i]; }return cloud_protected_ip(&v4,4); } }
        return version == U8(6) && a[0] == U8(0x26) && a[1] == U8(6) && a[2] == U8(0x47) && a[3] == U8(0) && a[4] == U8(0x47) && a[5] == U8(0);
    }
    fn cloud_provider(p: &Policy, host: &Host, address_: &Array<U8,16>, version: U8) -> U32 {
        if ip_contains(&p.cloudflare_ips,address_,version) { return 1; }if ip_contains(&p.cloudfront_ips,address_,version) { return 2; }if ip_contains(&p.azure_frontdoor_ips,address_,version) { return 3; }
        let named: U32=cloud_host_family(host);if named != U32(0) { return named; }if ip_contains(&p.azure_cloud_ips,address_,version) { return 4; }return 0;
    }
    fn cloud_provider_name(n: U32) -> Utf8 { if n == U32(1) { return "cloudflare"; }if n == U32(2) { return "cloudfront"; }if n == U32(3) { return "azure_frontdoor"; }if n == U32(4) { return "azure_cloud"; }return "mwo_or_unclassified"; }
    fn cloud_scope(o: &Options, p: &Policy, f: &Flow, v: &Packet) -> Bool {
        if automatic_protected(&f.host) || cloud_protected_ip(&v.destination,v.version) || !automatic_public(&v.destination,v.version) || !policy_udp(p,&v.destination,v.version,o.whitelist) || gene_applies(&f.host,4) { return false; }
        if f.host.length == 0 && (len(&p.hosts.domains) > 0 || len(&p.excluded_hosts.domains) > 0) { return false; }
        if mwo_host(&f.host) { return true; }if !o.cloud_cdn { return false; }return f.mwo_owned || cloud_host_family(&f.host) != U32(0) || (f.cloud_provider >= U32(1) && f.cloud_provider <= U32(3));
    }
    fn flow_gene(o: &Options, f: &Flow) -> Bool { if !o.gene_mode { return false; }if target_mwo(o.gene_target) && f.cloud_scope { return true; }if cloud_host_family(&f.host) != U32(0) && !o.cloud_cdn { return false; }return gene_applies(&f.host,o.gene_target); }
    fn cloud_arm(e: &StrategyEvidence) -> U32 {
        let order: Array<U32,3>=array<Array<U32,3>>(1,2,0);let mut best: U32=1;let mut score: F64=-100.0;
        for i in 0..3 { let arm: U32=order[i];if arm == U32(0) && (e.arms[1].failures == U32(0) || e.arms[2].failures == U32(0) || e.arms[1].successes > U32(0) || e.arms[2].successes > U32(0)) { continue; }
            let a: ArmEvidence=e.arms[cast<USize>(arm)];if a.trials == U32(0) && a.inflight == U32(0) { return arm; }let s: F64=arm_score(&a);if s > score { score=s;best=arm; }
        }return best;
    }
    fn mwo_process_kind(path: Slice<U8>) -> U32 { let mut start: USize=0;for i in 0..len(path) { if path[i] == U8(92) || path[i] == U8(47) { start=i+1; } }let name: Slice<U8>=slice(&path,start,len(path)-start);if ascii_equal(name,utf8_bytes("MWOClient.exe")) { return 1; }if ascii_equal(name,utf8_bytes("MWOPortal.exe")) { return 2; }return 0; }
    fn mwo_socket_pid(table: Slice<U8>, key: &FlowKey) -> U32 {
        if len(table) < 4 || key.protocol != U8(6) || (key.version != U8(4) && key.version != U8(6)) { return 0; }
        let mut stride: USize=24;let mut local: USize=4;let mut lp: USize=8;let mut remote: USize=12;let mut rp: USize=16;let mut state: USize=0;let mut pid: USize=20;let mut size: USize=4;
        if key.version == U8(6) { stride=56;local=0;lp=20;remote=24;rp=44;state=48;pid=52;size=16; }
        let count: USize=cast<USize>(read_le<U32>(&table,0));if count > (len(table)-4)/stride || count > 262144 { return 0; }
        for i in 0..count { let at: USize=4+i*stride;let status: U32=read_le<U32>(&table,at+state);if status < U32(3) || status > U32(10) || read_be<U16>(&table,at+lp) != key.source_port || read_be<U16>(&table,at+rp) != key.destination_port { continue; }
            let mut same: Bool=true;for j in 0..size { same=same && table[at+local+j] == key.source[j] && table[at+remote+j] == key.destination[j]; }if same { return read_le<U32>(&table,at+pid); }
        }return 0;
    }
    fn mwo_redirect(status: U32, location: Slice<U8>) -> Bool { return (status == U32(301) || status == U32(302) || status == U32(307) || status == U32(308)) && bytes_equal(location,utf8_bytes("https://mwomercs.com/login")); }
    fn mwo_content(name: Slice<U8>, body: Slice<U8>) -> Bool {
        if bytes_find(body,0,utf8_bytes("Just a moment...")) < len(body) || bytes_find(body,0,utf8_bytes("cf-chl-error")) < len(body) { return false; }
        if bytes_equal(name,utf8_bytes("mwo_public_css")) { return len(body) >= 32768 && byte_find(body,0,123) < len(body) && byte_find(body,0,125) < len(body); }
        if bytes_find(body,0,utf8_bytes("MWO:")) == len(body) && bytes_find(body,0,utf8_bytes("MechWarrior")) == len(body) { return false; }
        if bytes_equal(name,utf8_bytes("mwo_login")) || bytes_equal(name,utf8_bytes("mwo_ac_frontend")) { return bytes_find(body,0,utf8_bytes("name=\"password\"")) < len(body) && bytes_find(body,0,utf8_bytes("name=\"email\"")) < len(body); }return true;
    }
}
