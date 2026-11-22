module dpi.packet
import dpi.bytes

native {
    record Packet repr(C) {
        source: Array<U8,16>; destination: Array<U8,16>;
        length: USize; ip_header: USize; transport: USize; transport_header: USize;
        payload: USize; payload_length: USize;
        sequence: U32; acknowledgement: U32; source_port: U16; destination_port: U16;
        version: U8; protocol: U8; flags: U8; ttl: U8;
        fragmented: Bool; transformable: Bool; valid: Bool;
    }
    fn parse_packet(b: Slice<U8>) -> Packet guarantees(NoAlloc, NoBlocking, NoIO) {
        let mut v: Packet=zero<Packet>();if len(b) < 20 { return v; }
        v.version=b[0] >> U8(4);let mut offset: USize=0;let mut protected_: Bool=false;
        if v.version == U8(4) {
            v.ip_header=cast<USize>(b[0] & U8(15))*4;v.length=cast<USize>(be16(b,2));
            if v.ip_header < 20 || v.length < v.ip_header || v.length > len(b) { return v; }
            for i in 0..4 { v.source[i]=b[12+i];v.destination[i]=b[16+i]; }
            v.protocol=b[9];v.ttl=b[8];v.fragmented=(be16(b,6) & U16(0x3fff)) != U16(0);offset=v.ip_header;
        } else if v.version == U8(6) {
            if len(b) < 40 { return v; }v.ip_header=40;v.length=40+cast<USize>(be16(b,4));
            if v.length > len(b) || v.length == 40 { return v; }
            for i in 0..16 { v.source[i]=b[8+i];v.destination[i]=b[24+i]; }
            v.protocol=b[6];v.ttl=b[7];offset=40;let mut extensions: USize=0;
            while v.protocol == U8(0) || v.protocol == U8(60) || v.protocol == U8(43) || v.protocol == U8(44) || v.protocol == U8(51) {
                extensions=extensions+1;if extensions > 8 || offset+2 > v.length { return v; }
                let kind: U8=v.protocol;let mut size: USize=(cast<USize>(b[offset+1])+1)*8;
                if kind == U8(44) { size=8;v.fragmented=true; }
                if kind == U8(51) { size=(cast<USize>(b[offset+1])+2)*4; }
                if size > v.length-offset { return v; }
                if kind == U8(43) || kind == U8(51) { protected_=true; }v.protocol=b[offset];offset=offset+size;
            }
        } else { return v; }
        v.transport=offset;
        if v.fragmented { v.valid=true;return v; }
        if v.protocol == U8(6) {
            if v.length-offset < 20 { return v; }v.transport_header=cast<USize>(b[offset+12] >> U8(4))*4;
            if v.transport_header < 20 || v.transport_header > v.length-offset { return v; }
            v.sequence=be32(b,offset+4);v.acknowledgement=be32(b,offset+8);v.flags=b[offset+13];
            let mut p: USize=offset+20;let end: USize=offset+v.transport_header;
            while p < end {
                let kind: U8=b[p];if kind == U8(0) { break; }if kind == U8(1) { p=p+1;continue; }
                if p+2 > end || b[p+1] < U8(2) || cast<USize>(b[p+1]) > end-p { return v; }
                if kind == U8(19) || kind == U8(29) || kind == U8(30) { protected_=true; }p=p+cast<USize>(b[p+1]);
            }
            v.transformable=!protected_ && (v.flags & U8(0x26)) == U8(0);
        } else if v.protocol == U8(17) {
            if v.length-offset < 8 { return v; }let size: USize=cast<USize>(be16(b,offset+4));
            if size < 8 || size != v.length-offset { return v; }v.transport_header=8;v.transformable=!protected_;
        }
        if v.transport_header > 0 {
            v.source_port=be16(b,offset);v.destination_port=be16(b,offset+2);
            v.payload=offset+v.transport_header;v.payload_length=v.length-v.payload;
        }
        v.valid=true;return v;
    }
    fn checksum_sum(b: Slice<U8>, seed: U32) -> U32 {
        let mut sum: U32=seed;let mut i: USize=0;
        while i+1 < len(b) { sum=sum+cast<U32>(be16(b,i));i=i+2; }
        if i < len(b) { sum=sum+(cast<U32>(b[i]) << U32(8)); }return sum;
    }
    fn checksum_finish(seed: U32) -> U16 {
        let mut sum: U32=seed;while (sum >> U32(16)) != U32(0) { sum=(sum & U32(65535))+(sum >> U32(16)); }
        return cast<U16>((~sum) & U32(65535));
    }
    fn checksum(b: Slice<U8>) -> U16 { return checksum_finish(checksum_sum(b,0)); }
    fn checksums(b: &mut Buffer<U8>) -> Bool {
        let v: Packet=parse_packet(slice(b,0,len(b)));if !v.valid { return false; }
        if v.version == U8(4) { write_be<U16>(b,10,0);let sum: U16=checksum(slice(b,0,v.ip_header));write_be<U16>(b,10,sum); }
        if v.fragmented { return true; }
        let n: USize=v.length-v.transport;let mut field: USize=0;
        if v.protocol == U8(6) && v.transport_header > 0 { field=v.transport+16; }
        else if v.protocol == U8(17) && v.transport_header > 0 { field=v.transport+6; }
        else if (v.protocol == U8(1) || v.protocol == U8(58)) && n >= 8 { field=v.transport+2; }else { return true; }
        write_be<U16>(b,field,0);let mut sum: U32=0;
        if v.protocol != U8(1) {
            let mut count: USize=16;if v.version == U8(4) { count=4; }
            sum=checksum_sum(slice(&v.source,0,count),0);sum=checksum_sum(slice(&v.destination,0,count),sum);
            sum=sum+cast<U32>(n)+cast<U32>(v.protocol);
        }
        let mut value: U16=checksum_finish(checksum_sum(slice(b,v.transport,n),sum));
        if v.protocol == U8(17) && value == U16(0) { value=65535; }write_be<U16>(b,field,value);return true;
    }
    record Plan repr(C) { offsets: Array<U32,4>; count: USize; }
    fn add_cut(p: &mut Plan, cut: U32, length: USize) -> Unit {
        if cut == U32(0) || cast<USize>(cut) >= length || p.count >= 4 { return; }
        for i in 0..p.count { if p.offsets[i] == cut { return; } }p.offsets[p.count]=cut;p.count=p.count+1;
    }
    fn sort_plan(p: &mut Plan) -> Unit {
        for i in 1..p.count {
            let mut j: USize=i;while j > 0 && p.offsets[j] < p.offsets[j-1] {
                let x: U32=p.offsets[j];p.offsets[j]=p.offsets[j-1];p.offsets[j-1]=x;j=j-1;
            }
        }
    }
    record Batch repr(C) { bytes: Buffer<U8>; lengths: Array<U32,5>; count: USize; }
    fn batch_add(batch: &mut Batch, b: Slice<U8>) -> Unit {
        assert(batch.count < 5);batch.lengths[batch.count]=cast<U32>(len(b));batch.count=batch.count+1;bytes_append(&mut batch.bytes,b);
    }
    fn packet_length(b: &mut Buffer<U8>, v: &Packet) -> Unit {
        let mut field: USize=2;let mut n: USize=len(b);if v.version == U8(6) { field=4;n=n-40; }write_be<U16>(b,field,cast<U16>(n));
    }
    fn tcp_segments(b: Slice<U8>, v: &Packet, plan: &Plan, reverse: Bool) -> Batch {
        let mut out: Batch=zero<Batch>();
        if v.protocol != U8(6) || !v.transformable || plan.count == 0 || plan.count > 4 { return move out; }
        let mut p: Plan=zero<Plan>();for i in 0..plan.count { add_cut(&mut p,plan.offsets[i],v.payload_length); }sort_plan(&mut p);
        if p.count == 0 { return move out; }
        let mut bounds: Array<USize,6>=zero<Array<USize,6>>();for i in 0..p.count { bounds[i+1]=cast<USize>(p.offsets[i]); }
        let count: USize=p.count+1;bounds[count]=v.payload_length;
        for part in 0..count {
            let mut i: USize=part;if reverse { i=count-part-1; }let first: USize=bounds[i];let last: USize=bounds[i+1];
            let mut segment: Buffer<U8>=buffer<U8>(v.payload+last-first);
            for j in 0..v.payload { segment[j]=b[j]; }for j in first..last { segment[v.payload+j-first]=b[v.payload+j]; }
            packet_length(&mut segment,v);
            if v.version == U8(4) { write_be<U16>(&mut segment,4,wrapping_add(be16(b,4),cast<U16>(i))); }
            write_be<U32>(&mut segment,v.transport+4,wrapping_add(v.sequence,cast<U32>(first)));
            if i+1 < count { segment[v.transport+13]=segment[v.transport+13] & U8(0xf6); }
            if i > 0 { segment[v.transport+13]=segment[v.transport+13] & U8(0x7f); }
            if !checksums(&mut segment) { return zero<Batch>(); }batch_add(&mut out,slice(&segment,0,len(&segment)));
        }
        return move out;
    }
    fn ip_fragments(b: Slice<U8>, v: &Packet, a: U32, c: U32, reverse: Bool, id: U16) -> Batch {
        let mut out: Batch=zero<Batch>();
        if v.version != U8(4) || v.ip_header != 20 || v.fragmented || !v.transformable { return move out; }
        let n: USize=v.length-20;let first: USize=cast<USize>(a & U32(0xfffffff8));let second: USize=cast<USize>(c & U32(0xfffffff8));
        if first == 0 || second <= first || second >= n { return move out; }
        let mut source: Buffer<U8>=bytes_copy(b);if !checksums(&mut source) { return move out; }
        let bounds: Array<USize,4>=array<Array<USize,4>>(0,first,second,n);
        for part in 0..3 {
            let mut i: USize=part;if reverse { i=2-part; }let begin: USize=bounds[i];let end: USize=bounds[i+1];
            let mut fragment: Buffer<U8>=buffer<U8>(20+end-begin);
            for j in 0..20 { fragment[j]=source[j]; }for j in begin..end { fragment[20+j-begin]=source[20+j]; }
            let fragment_size: U16=cast<U16>(len(&fragment));write_be<U16>(&mut fragment,2,fragment_size);write_be<U16>(&mut fragment,4,id);
            let mut flags: U16=cast<U16>(begin/8);if i < 2 { flags=flags | U16(0x2000); }write_be<U16>(&mut fragment,6,flags);
            write_be<U16>(&mut fragment,10,0);let sum: U16=checksum(slice(&fragment,0,20));write_be<U16>(&mut fragment,10,sum);
            batch_add(&mut out,slice(&fragment,0,len(&fragment)));
        }return move out;
    }
    fn fake_hello() -> Buffer<U8> {
        let host: Slice<U8>=utf8_bytes("www.example.com");let mut b: Buffer<U8>=buffer<U8>(61+len(host));
        b[0]=22;b[1]=3;b[2]=1;b[5]=1;b[9]=3;b[10]=3;for i in 11..43 { b[i]=66; }
        write_be<U16>(&mut b,44,2);b[46]=192;b[47]=47;b[48]=1;
        let extension_size: U16=cast<U16>(len(&b)-52);write_be<U16>(&mut b,50,extension_size);write_be<U16>(&mut b,54,cast<U16>(len(host)+5));
        write_be<U16>(&mut b,56,cast<U16>(len(host)+3));write_be<U16>(&mut b,59,cast<U16>(len(host)));
        for i in 0..len(host) { b[61+i]=host[i]; }let handshake: U32=cast<U32>(len(&b)-9);
        b[6]=cast<U8>((handshake >> U32(16)) & U32(255));b[7]=cast<U8>((handshake >> U32(8)) & U32(255));b[8]=cast<U8>(handshake & U32(255));
        let record_size: U16=cast<U16>(len(&b)-5);write_be<U16>(&mut b,3,record_size);return move b;
    }
    fn fake_tcp(b: Slice<U8>, v: &Packet, badseq: Bool, ttl: U8, tls: Bool) -> Batch {
        let mut out: Batch=zero<Batch>();if v.protocol != U8(6) || !v.transformable || (!badseq && ttl == U8(0)) { return move out; }
        let mut body: Buffer<U8>=buffer<U8>(0);
        if badseq { if tls { body=fake_hello(); }else { body=bytes_copy(utf8_bytes("GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n")); } }
        let mut packet: Buffer<U8>=bytes_copy(slice(&b,0,v.payload));bytes_append(&mut packet,slice(&body,0,len(&body)));packet_length(&mut packet,v);
        let mut flags: U8=20;if badseq { flags=24; }packet[v.transport+13]=flags;
        write_be<U32>(&mut packet,v.transport+4,wrapping_add(v.sequence,U32(0x80000000)));
        if ttl != U8(0) { let mut offset: USize=8;if v.version == U8(6) { offset=7; }packet[offset]=ttl; }
        if checksums(&mut packet) { batch_add(&mut out,slice(&packet,0,len(&packet))); }return move out;
    }
    fn udp_unreachable(b: Slice<U8>, v: &Packet) -> Batch {
        let mut out: Batch=zero<Batch>();if v.protocol != U8(17) || !v.transformable { return move out; }
        let mut header: USize=40;let mut quoted: USize=min_size(v.length,1232);
        if v.version == U8(4) { header=20;quoted=min_size(v.length,548); }
        let mut packet: Buffer<U8>=buffer<U8>(header+8+quoted);
        if v.version == U8(4) {
            packet[0]=69;packet[8]=64;packet[9]=1;let packet_size: U16=cast<U16>(len(&packet));write_be<U16>(&mut packet,2,packet_size);
            for i in 0..4 { packet[12+i]=v.destination[i];packet[16+i]=v.source[i]; }packet[20]=3;packet[21]=3;
        }else {
            packet[0]=96;packet[6]=58;packet[7]=64;let packet_size: U16=cast<U16>(len(&packet)-40);write_be<U16>(&mut packet,4,packet_size);
            for i in 0..16 { packet[8+i]=v.destination[i];packet[24+i]=v.source[i]; }packet[40]=1;packet[41]=4;
        }
        for i in 0..quoted { packet[header+8+i]=b[i]; }if checksums(&mut packet) { batch_add(&mut out,slice(&packet,0,len(&packet))); }return move out;
    }
    fn fake_ttl(server: U8, margin: U32, spread: U32, maximum: U32, minimum: U32, random: U32) -> U8 {
        if server == U8(0) { return 0; }let mut base: U32=255;
        if server <= U8(32) { base=32; }else if server <= U8(64) { base=64; }else if server <= U8(128) { base=128; }
        let hops: U32=base-cast<U32>(server);let mut delta: U32=margin;if spread > U32(0) { delta=delta+random%spread; }if delta == U32(0) { delta=1; }
        if hops < minimum || hops <= delta { return 0; }let mut ttl: U32=hops-delta;if maximum > U32(0) && ttl > maximum { ttl=maximum; }return cast<U8>(ttl);
    }
}
