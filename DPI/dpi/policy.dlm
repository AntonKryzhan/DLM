module dpi.policy
import dpi.inspect

native {
    record DomainIndex repr(C) { domains: Buffer<Host>; slots: Buffer<U32>; }
    fn domain_finalize(index: &mut DomainIndex) -> Unit {
        let mut count: USize=8;while count < len(&index.domains)*2 { count=count*2; }index.slots=buffer<U32>(count);
        for i in 0..len(&index.domains) {
            let mut at: USize=cast<USize>(host_hash(&index.domains[i])) & (count-1);
            while index.slots[at] != U32(0) { at=(at+1) & (count-1); }index.slots[at]=cast<U32>(i+1);
        }
    }
    fn domain_exact(index: &DomainIndex, h: &Host, offset: USize) -> Bool {
        if len(&index.slots) == 0 { return false; }let mut part: Host=zero<Host>();part.length=h.length-offset;
        for i in 0..part.length { part.bytes[i]=h.bytes[offset+i]; }
        let mut at: USize=cast<USize>(host_hash(&part)) & (len(&index.slots)-1);
        for i in 0..len(&index.slots) {
            let slot: U32=index.slots[at];if slot == U32(0) { return false; }
            if host_equal(&index.domains[cast<USize>(slot)-1],&part) { return true; }at=(at+1) & (len(&index.slots)-1);
        }return false;
    }
    fn domain_matches(index: &DomainIndex, h: &Host) -> Bool {
        if h.length == 0 { return false; }if domain_exact(index,h,0) { return true; }
        for i in 0..h.length { if h.bytes[i] == U8(46) && domain_exact(index,h,i+1) { return true; } }return false;
    }
    record Prefix repr(C) { address: Array<U8,16>; bits: USize; version: U8; valid: Bool; }
    fn parse_ipv4(text: Slice<U8>) -> Prefix {
        let mut p: Prefix=zero<Prefix>();let mut start: USize=0;
        for i in 0..4 {
            let end: USize=byte_find(text,start,U8(46));if (i < 3 && end == len(text)) || (i == 3 && end != len(text)) { return p; }
            let mut n: U32=0;let part: Slice<U8>=slice(&text,start,end-start);
            if !decimal(part,255,&mut n) || (len(part) > 1 && part[0] == U8(48)) { return p; }p.address[i]=cast<U8>(n);start=end+1;
        }p.bits=32;p.version=4;p.valid=true;return p;
    }
    fn parse_ipv6(text: Slice<U8>) -> Prefix {
        let mut result: Prefix=zero<Prefix>();let mut words: Array<U16,8>=zero<Array<U16,8>>();let mut count: USize=0;
        let mut compressed: USize=9;let mut p: USize=0;
        if bytes_at(text,0,utf8_bytes("::")) { compressed=0;p=2; }else if len(text) == 0 || text[0] == U8(58) { return result; }
        while p < len(text) {
            if count >= 8 { return result; }let end: USize=byte_find(text,p,U8(58));let part: Slice<U8>=slice(&text,p,end-p);
            if byte_find(part,0,U8(46)) != len(part) {
                let v4: Prefix=parse_ipv4(part);if end != len(text) || count > 6 || !v4.valid { return result; }
                words[count]=(cast<U16>(v4.address[0]) << U16(8)) | cast<U16>(v4.address[1]);
                words[count+1]=(cast<U16>(v4.address[2]) << U16(8)) | cast<U16>(v4.address[3]);count=count+2;break;
            }
            if len(part) == 0 || len(part) > 4 { return result; }let mut n: U16=0;
            for i in 0..len(part) {
                let c: U8=ascii_lower(part[i]);let mut d: U16=99;
                if c >= U8(48) && c <= U8(57) { d=cast<U16>(c)-U16(48); }
                else if c >= U8(97) && c <= U8(102) { d=cast<U16>(c)-U16(87); }else { return result; }n=(n << U16(4)) | d;
            }words[count]=n;count=count+1;if end == len(text) { break; }p=end+1;
            if p < len(text) && text[p] == U8(58) { if compressed != 9 { return result; }compressed=count;p=p+1; }
            else if p == len(text) { return result; }
        }
        if compressed == 9 && count != 8 { return result; }
        if compressed != 9 {
            if count >= 8 { return result; }let missing: USize=8-count;let mut i: USize=count;
            while i > compressed { i=i-1;words[i+missing]=words[i]; }for j in compressed..compressed+missing { words[j]=0; }
        }
        for i in 0..8 { result.address[i*2]=cast<U8>(words[i] >> U16(8));result.address[i*2+1]=cast<U8>(words[i] & U16(255)); }
        result.valid=true;result.version=6;result.bits=128;return result;
    }
    fn parse_prefix(text: Slice<U8>) -> Prefix {
        let slash: USize=byte_find(text,0,U8(47));let a: Slice<U8>=slice(&text,0,slash);let mut p: Prefix=zero<Prefix>();
        if byte_find(a,0,U8(58)) == len(a) { p=parse_ipv4(a); }else { p=parse_ipv6(a); }
        if !p.valid { return p; }if slash < len(text) {
            let mut n: U32=0;if !decimal(slice(&text,slash+1,len(text)-slash-1),cast<U32>(p.bits),&mut n) { p.valid=false;return p; }p.bits=cast<USize>(n);
        }return p;
    }
    // A compact radix index makes inclusion O(32) / O(128), independent of
    // the number of AWS prefixes. Child zero is absent; roots are 0 and 1.
    record RadixNode repr(C) { left: U32; right: U32; terminal: Bool; }
    record IpIndex repr(C) { nodes: Buffer<RadixNode>; count: USize; }
    fn ip_add(index: &mut IpIndex, p: &Prefix) -> Unit {
        if !p.valid { return; }if len(&index.nodes) == 0 { index.nodes=buffer<RadixNode>(2); }
        let mut at: USize=0;if p.version == U8(6) { at=1; }
        for i in 0..p.bits {
            if index.nodes[at].terminal { return; }let bit: U8=(p.address[i/8] >> cast<U8>(7-i%8)) & U8(1);
            let mut next: U32=index.nodes[at].left;if bit == U8(1) { next=index.nodes[at].right; }
            if next == U32(0) {
                next=cast<U32>(len(&index.nodes));push(&mut index.nodes,zero<RadixNode>());
                if bit == U8(0) { index.nodes[at].left=next; }else { index.nodes[at].right=next; }
            }at=cast<USize>(next);
        }if !index.nodes[at].terminal { index.count=index.count+1; }index.nodes[at].terminal=true;
    }
    fn ip_contains(index: &IpIndex, address_: &Array<U8,16>, version: U8) -> Bool {
        if len(&index.nodes) == 0 { return false; }let mut at: USize=0;let mut bits: USize=32;
        if version == U8(6) { at=1;bits=128; }else if version != U8(4) { return false; }
        for i in 0..bits {
            if index.nodes[at].terminal { return true; }let bit: U8=(address_[i/8] >> cast<U8>(7-i%8)) & U8(1);
            let mut next: U32=index.nodes[at].left;if bit == U8(1) { next=index.nodes[at].right; }if next == U32(0) { return false; }at=cast<USize>(next);
        }return index.nodes[at].terminal;
    }
    record Policy repr(C) { hosts: DomainIndex; excluded_hosts: DomainIndex; ips: IpIndex; excluded_ips: IpIndex; aws: IpIndex; quic_targets: IpIndex; telegram_ips: IpIndex; cloudflare_ips: IpIndex; cloudfront_ips: IpIndex; azure_frontdoor_ips: IpIndex; azure_cloud_ips: IpIndex; }
    fn policy_allows(p: &Policy, host: &Host, address_: &Array<U8,16>, version: U8, ech: Bool, apply_ech: Bool, whitelist: Bool) -> Bool {
        if domain_matches(&p.excluded_hosts,host) || ip_contains(&p.excluded_ips,address_,version) { return false; }
        let aws: Bool=ip_contains(&p.aws,address_,version);if whitelist && !aws { return false; }
        if ech && apply_ech { return p.ips.count == 0 || ip_contains(&p.ips,address_,version); }
        if len(&p.hosts.domains) == 0 && p.ips.count == 0 && (whitelist || p.aws.count == 0) { return true; }
        return domain_matches(&p.hosts,host) || ip_contains(&p.ips,address_,version) || (!whitelist && aws);
    }
    fn policy_udp(p: &Policy, address_: &Array<U8,16>, version: U8, whitelist: Bool) -> Bool {
        if ip_contains(&p.excluded_ips,address_,version) || (whitelist && !ip_contains(&p.aws,address_,version)) { return false; }
        return p.ips.count == 0 || ip_contains(&p.ips,address_,version);
    }
    fn rule_line(b: Slice<U8>) -> Slice<U8> borrows(b) {
        let mut n: USize=len(b);for i in 0..len(b) { if b[i] == U8(35) || b[i] == U8(59) { n=i;break; } }
        let mut p: USize=0;if bytes_at(b,0,utf8_bytes("﻿")) { p=3; }let part: Slice<U8>=slice(&b,p,n-p);return trimmed(part);
    }
    fn host_rules(index: &mut DomainIndex, b: Slice<U8>) -> Bool {
        let mut p: USize=0;while p < len(b) {
            let end: USize=byte_find(b,p,U8(10));let raw: Slice<U8>=slice(&b,p,end-p);let line: Slice<U8>=rule_line(raw);p=end+1;
            if len(line) == 0 { continue; }
            // Hosts files may prefix one or more names with an IPv4/IPv6 address.
            let mut first: USize=0;while first < len(line) && !space(line[first]) { first=first+1; }
            let prefix: Prefix=parse_prefix(slice(&line,0,first));let mut start: USize=0;if prefix.valid && first < len(line) { start=first; }
            while start < len(line) {
                while start < len(line) && space(line[start]) { start=start+1; }let mut stop: USize=start;
                while stop < len(line) && !space(line[stop]) { stop=stop+1; }let h: Host=domain(slice(&line,start,stop-start),true);
                if h.length > 0 { push(&mut index.domains,h); }start=stop;
            }
        }domain_finalize(index);return len(&index.domains) > 0;
    }
    fn ip_rules(index: &mut IpIndex, b: Slice<U8>) -> Bool {
        let mut p: USize=0;while p < len(b) { let end: USize=byte_find(b,p,U8(10));let raw: Slice<U8>=slice(&b,p,end-p);let line: Slice<U8>=rule_line(raw);let prefix: Prefix=parse_prefix(line);ip_add(index,&prefix);p=end+1; }
        return index.count > 0;
    }
    fn service_allows(filter: Slice<U8>, name: Slice<U8>) -> Bool {
        if len(filter) == 0 { return true; }let mut p: USize=0;
        while p < len(filter) {
            while p < len(filter) && (space(filter[p]) || filter[p] == U8(44)) { p=p+1; }let start: USize=p;
            while p < len(filter) && !space(filter[p]) && filter[p] != U8(44) { p=p+1; }
            if p-start == len(name) && ascii_at(filter,start,name) { return true; }
        }return false;
    }
    fn json_value(b: Slice<U8>, name: Slice<U8>) -> Slice<U8> borrows(b) {
        let mut p: USize=0;
        while p < len(b) {
            if b[p] != U8(34) { p=p+1;continue; }let a: USize=p+1;let z: USize=byte_find(b,a,U8(34));if z == len(b) { break; }
            p=z+1;while p < len(b) && space(b[p]) { p=p+1; }
            if p >= len(b) || b[p] != U8(58) { continue; }p=p+1;while p < len(b) && space(b[p]) { p=p+1; }
            if p >= len(b) || b[p] != U8(34) { continue; }let value: USize=p+1;let end: USize=byte_find(b,value,U8(34));
            if end == len(b) { break; }p=end+1;if z-a == len(name) && ascii_at(b,a,name) { return slice(&b,value,end-value); }
        }return slice(&b,0,0);
    }
    fn aws_rules(index: &mut IpIndex, b: Slice<U8>, services: Slice<U8>) -> Bool {
        let mut p: USize=0;let mut object: USize=0;let mut quoted: Bool=false;let mut escaped: Bool=false;
        while p < len(b) {
            let c: U8=b[p];if quoted {
                if escaped { escaped=false; }else if c == U8(92) { escaped=true; }else if c == U8(34) { quoted=false; }
            }else if c == U8(34) { quoted=true; }else if c == U8(123) { object=p+1; }
            else if c == U8(125) && object <= p {
                let obj: Slice<U8>=slice(&b,object,p-object);let value: Slice<U8>=json_value(obj,utf8_bytes("ip_prefix"));
                let v6: Slice<U8>=json_value(obj,utf8_bytes("ipv6_prefix"));let service: Slice<U8>=json_value(obj,utf8_bytes("service"));
                if len(service) == 0 || service_allows(services,service) { let mut prefix: Prefix=parse_prefix(value);if len(value) == 0 { prefix=parse_prefix(v6); }ip_add(index,&prefix); }object=p+1;
            }p=p+1;
        }return index.count > 0;
    }
}
