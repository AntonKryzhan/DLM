module dpi.inspect
import dpi.packet

native {
    record Hello repr(C) {
        recognized: Bool; complete: Bool; malformed: Bool; tls: Bool; ech: Bool;
        wire_length: USize; host: Host; host_offset: USize; host_length: USize;
        host_split: USize; ech_split: USize;
    }
    record Span repr(C) { logical: USize; wire: USize; length: USize; }
    record Handshake repr(C) { spans: Array<Span,128>; count: USize; length: USize; required: USize; }
    fn tls_wire(v: &Handshake, offset: USize) -> USize {
        for i in 0..v.count { let s: Span=v.spans[i];if offset >= s.logical && offset-s.logical < s.length { return s.wire+offset-s.logical; } }return 0;
    }
    fn tls_byte(b: Slice<U8>, v: &Handshake, at: USize) -> U8 { return b[tls_wire(v,at)]; }
    fn tls_word(b: Slice<U8>, v: &Handshake, at: USize) -> U16 {
        return (cast<U16>(tls_byte(b,v,at)) << U16(8)) | cast<U16>(tls_byte(b,v,at+1));
    }
    // Missing input is incomplete; an impossible position in the declared
    // handshake is malformed. This distinction preserves segmented hellos.
    fn tls_need(v: &Handshake, h: &mut Hello, offset: USize, length: USize) -> Bool {
        if offset > v.required || length > v.required-offset { h.malformed=true;return false; }
        let available: USize=min_size(v.required,v.length);return offset <= available && length <= available-offset;
    }
    fn inspect_tls(b: Slice<U8>) -> Hello {
        let mut h: Hello=zero<Hello>();if len(b) < 6 || b[0] != U8(22) || b[1] != U8(3) || b[5] != U8(1) { return h; }
        h.recognized=true;h.tls=true;let mut v: Handshake=zero<Handshake>();let mut p: USize=0;
        while p < len(b) {
            if len(b)-p < 5 { break; }let n: USize=cast<USize>(be16(b,p+3));
            if b[p] != U8(22) || b[p+1] != U8(3) || b[p+2] > U8(3) || n == 0 || n > 16384 || v.count == 128 { h.malformed=true;return h; }
            let present: USize=min_size(n,len(b)-p-5);v.spans[v.count]=Span(v.length,p+5,present);v.count=v.count+1;v.length=v.length+present;
            if v.required == 0 && v.length >= 4 {
                v.required=4+(cast<USize>(tls_byte(b,&v,1)) << USize(16))+(cast<USize>(tls_byte(b,&v,2)) << USize(8))+cast<USize>(tls_byte(b,&v,3));
                if v.required < 45 || v.required > 65536 { h.malformed=true;return h; }
            }
            if v.required > 0 && v.length >= v.required { break; }if present != n { break; }p=p+5+n;
        }
        if v.required == 0 { return h; }p=4;
        if !tls_need(&v,&mut h,p,34) { return h; }
        if tls_byte(b,&v,p) != U8(3) || tls_byte(b,&v,p+1) > U8(3) { h.malformed=true;return h; }p=p+34;
        if !tls_need(&v,&mut h,p,1) { return h; }let session: USize=cast<USize>(tls_byte(b,&v,p));p=p+1;
        if session > 32 { h.malformed=true;return h; }if !tls_need(&v,&mut h,p,session+2) { return h; }p=p+session;
        let cipher: USize=cast<USize>(tls_word(b,&v,p));p=p+2;
        if cipher < 2 || cipher%2 != 0 { h.malformed=true;return h; }if !tls_need(&v,&mut h,p,cipher+1) { return h; }p=p+cipher;
        let compression: USize=cast<USize>(tls_byte(b,&v,p));p=p+1;if compression == 0 { h.malformed=true;return h; }
        if !tls_need(&v,&mut h,p,compression) { return h; }p=p+compression;
        if p == v.required { h.complete=true;h.wire_length=tls_wire(&v,v.required-1)+1;return h; }
        if !tls_need(&v,&mut h,p,2) { return h; }let extensions: USize=cast<USize>(tls_word(b,&v,p));p=p+2;
        if extensions != v.required-p { h.malformed=true;return h; }let mut sni_seen: Bool=false;let mut ech_seen: Bool=false;
        while p < v.required {
            if !tls_need(&v,&mut h,p,4) { return h; }let kind: U16=tls_word(b,&v,p);let n: USize=cast<USize>(tls_word(b,&v,p+2));p=p+4;
            if n > v.required-p { h.malformed=true;return h; }
            if kind == U16(0) {
                if sni_seen || n < 5 { h.malformed=true;return h; }sni_seen=true;
                if !tls_need(&v,&mut h,p,2) { return h; }if cast<USize>(tls_word(b,&v,p)) != n-2 { h.malformed=true;return h; }
                let mut name: USize=p+2;let end: USize=p+n;
                while name < end {
                    if end-name < 3 { h.malformed=true;return h; }if !tls_need(&v,&mut h,name,3) { return h; }
                    let name_kind: U8=tls_byte(b,&v,name);let count: USize=cast<USize>(tls_word(b,&v,name+1));name=name+3;
                    if count == 0 || count > end-name { h.malformed=true;return h; }if !tls_need(&v,&mut h,name,count) { return h; }
                    if name_kind == U8(0) {
                        if h.host.length > 0 || count > 253 { h.malformed=true;return h; }
                        let mut raw: Array<U8,254>=zero<Array<U8,254>>();for i in 0..count { raw[i]=tls_byte(b,&v,name+i); }
                        h.host=domain(slice(&raw,0,count),false);if h.host.length == 0 { h.malformed=true;return h; }
                        h.host_offset=tls_wire(&v,name);h.host_length=count;h.host_split=tls_wire(&v,name+middle_label(&h.host));
                    }name=name+count;
                }
            }else if kind == U16(0xfe0d) {
                if ech_seen || n == 0 { h.malformed=true;return h; }ech_seen=true;h.ech=true;
                if n > 1 && tls_need(&v,&mut h,p,n) { h.ech_split=tls_wire(&v,p+n/2); }
            }
            if !tls_need(&v,&mut h,p,n) { return h; }p=p+n;
        }
        h.complete=v.length >= v.required;if h.complete { h.wire_length=tls_wire(&v,v.required-1)+1; }return h;
    }
    fn inspect_http(b: Slice<U8>) -> Hello {
        let mut h: Hello=zero<Hello>();if len(b) < 8 { return h; }let end: USize=bytes_find(b,0,utf8_bytes("\r\n"));
        if end == len(b) { return h; }let method: USize=byte_find(b,0,U8(32));let mut version: USize=end;
        for i in 0..end { if b[i] == U8(32) { version=i; } }
        if method == 0 || method >= version || version >= end { return h; }
        for i in 0..method { if b[i] < U8(65) || b[i] > U8(90) { return h; } }
        let v: Slice<U8>=slice(&b,version+1,end-version-1);
        if !bytes_equal(v,utf8_bytes("HTTP/1.0")) && !bytes_equal(v,utf8_bytes("HTTP/1.1")) { return h; }
        h.recognized=true;let mut p: USize=end+2;let mut seen: Bool=false;
        while p < len(b) {
            let z: USize=bytes_find(b,p,utf8_bytes("\r\n"));if z == len(b) { return h; }
            if z == p { h.complete=true;h.wire_length=z+2;return h; }
            let colon: USize=byte_find(b,p,U8(58));if colon >= z || colon == p || b[p] == U8(32) || b[p] == U8(9) { h.malformed=true;return h; }
            if colon-p == 4 && ascii_at(b,p,utf8_bytes("host")) {
                if seen { h.malformed=true;h.host=zero<Host>();return h; }seen=true;
                let mut a: USize=colon+1;let mut c: USize=z;
                while a < c && (b[a] == U8(32) || b[a] == U8(9)) { a=a+1; }
                while c > a && (b[c-1] == U8(32) || b[c-1] == U8(9)) { c=c-1; }
                if a == c { h.malformed=true;return h; }
                if b[a] != U8(91) {
                    let port: USize=byte_find(b,a,U8(58));if port < c {
                        if port+1 == c { h.malformed=true;return h; }for i in port+1..c { if b[i] < U8(48) || b[i] > U8(57) { h.malformed=true;return h; } }c=port;
                    }
                    h.host=domain(slice(&b,a,c-a),false);if h.host.length == 0 { h.malformed=true;return h; }h.host_offset=a;h.host_length=c-a;
                }
            }p=z+2;
        }return h;
    }
    fn hello_plan(h: &Hello, length: USize, multiple: Bool) -> Plan {
        let mut p: Plan=zero<Plan>();if !h.recognized || h.malformed { return p; }
        let mut host: USize=0;if h.host.length > 0 { host=h.host_offset+middle_label(&h.host); }if h.host_split > 0 { host=h.host_split; }
        if h.tls {
            if multiple { add_cut(&mut p,1,length);add_cut(&mut p,cast<U32>(host),length);add_cut(&mut p,cast<U32>(h.ech_split),length);
                if h.host_length > 2 { add_cut(&mut p,cast<U32>(h.host_offset+1),length); }
            }else { let mut cut: USize=host;if h.ech_split > 0 { cut=h.ech_split; }add_cut(&mut p,cast<U32>(cut),length); }
            if p.count == 0 { add_cut(&mut p,1,length); }
        }else {
            if multiple { add_cut(&mut p,2,length); }add_cut(&mut p,cast<U32>(host),length);
            if multiple && h.host.length > 0 { add_cut(&mut p,cast<U32>(h.host_offset+1),length); }if p.count == 0 { add_cut(&mut p,2,length); }
        }sort_plan(&mut p);return p;
    }
    record Varint repr(C) { value: U64; next: USize; valid: Bool; }
    fn quic_varint(b: Slice<U8>, p: USize) -> Varint {
        if p >= len(b) { return zero<Varint>(); }let count: USize=USize(1) << cast<USize>(b[p] >> U8(6));
        if count > len(b)-p { return zero<Varint>(); }let mut value: U64=cast<U64>(b[p] & U8(63));
        for i in 1..count { value=(value << U64(8)) | cast<U64>(b[p+i]); }return Varint(value,p+count,true);
    }
    record QuicInitial repr(C) { version: U32; dcid: Array<U8,20>; scid: Array<U8,20>; dcid_length: USize; scid_length: USize; valid: Bool; }
    fn inspect_quic(b: Slice<U8>, minimum: Bool) -> QuicInitial {
        let mut q: QuicInitial=zero<QuicInitial>();if len(b) < 7 || (minimum && len(b) < 1200) || (b[0] & U8(192)) != U8(192) { return q; }
        q.version=be32(b,1);let kind: U8=b[0] & U8(48);
        if !((q.version == U32(1) && kind == U8(0)) || (q.version == U32(0x6b3343cf) && kind == U8(16))) { return q; }
        let mut p: USize=6;q.dcid_length=cast<USize>(b[5]);if q.dcid_length > 20 || q.dcid_length > len(b)-p { return q; }
        for i in 0..q.dcid_length { q.dcid[i]=b[p+i]; }p=p+q.dcid_length;if p >= len(b) { return q; }
        q.scid_length=cast<USize>(b[p]);p=p+1;if q.scid_length > 20 || q.scid_length > len(b)-p { return q; }
        for i in 0..q.scid_length { q.scid[i]=b[p+i]; }p=p+q.scid_length;
        let token: Varint=quic_varint(b,p);if !token.valid || token.value > cast<U64>(len(b)-token.next) { return q; }p=token.next+cast<USize>(token.value);
        let protected_: Varint=quic_varint(b,p);if !protected_.valid || protected_.value < U64(17) || protected_.value > cast<U64>(len(b)-protected_.next) { return q; }
        q.valid=true;return q;
    }
    fn http_case(b: &mut Buffer<U8>, v: &Packet) -> Bool {
        if v.payload_length < 5 { return false; }
        for i in v.payload..v.length-4 {
            if i > v.payload && (i < v.payload+2 || b[i-2] != U8(13) || b[i-1] != U8(10)) { continue; }
            if i >= v.payload+2 && b[i] == U8(13) && b[i+1] == U8(10) { return false; }
            if ascii_at(slice(b,0,v.length),i,utf8_bytes("host:")) { b[i]=104;b[i+1]=79;b[i+2]=115;b[i+3]=84;return true; }
        }return false;
    }
}
