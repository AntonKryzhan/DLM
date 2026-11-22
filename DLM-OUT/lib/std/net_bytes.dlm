module std.net_bytes
import std.native_protocol

native {
    // Compatible with ProtocolValue: 0 success, 1 incomplete, 4 invalid,
    // 7 value/length limit. Invalid writes leave the destination unchanged.
    fn net_read_le(bytes: Slice<U8>, offset: USize, width: USize) -> ProtocolValue guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if width == 0 || width > 8 || offset > len(bytes) { return ProtocolValue(0,offset,4); }
        if width > len(bytes)-offset { return ProtocolValue(0,offset,1); }
        let mut value: U64=0;
        for index in 0..width { value=value | (cast<U64>(bytes[offset+index]) << cast<U64>(index*8)); }
        return ProtocolValue(value,offset+width,0);
    }
    fn net_write_le(bytes: &mut Buffer<U8>, offset: USize, width: USize, value: U64) -> I32 guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if width == 0 || width > 8 || offset > len(bytes) { return 4; }
        if width > len(bytes)-offset { return 1; }
        if width < 8 { if (value >> cast<U64>(width*8)) != U64(0) { return 7; } }
        for index in 0..width { bytes[offset+index]=cast<U8>((value >> cast<U64>(index*8)) & U64(255)); }
        return 0;
    }
    fn net_read_be(bytes: Slice<U8>, offset: USize, width: USize) -> ProtocolValue guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        return protocol_read_be(bytes,offset,width);
    }
    fn net_write_be(bytes: &mut Buffer<U8>, offset: USize, width: USize, value: U64) -> I32 guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        return protocol_write_be(bytes,offset,width,value);
    }
    fn uleb128_size(value: U64) -> USize guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut remaining: U64=value;
        for index in 0..10 {
            if remaining < U64(128) { return index+1; }
            remaining=remaining >> U64(7);
        }
        return 10;
    }
    fn uleb128_read(bytes: Slice<U8>, offset: USize, canonical: Bool) -> ProtocolValue guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if offset > len(bytes) { return ProtocolValue(0,offset,4); }
        let mut value: U64=0;
        for index in 0..10 {
            if index >= len(bytes)-offset { return ProtocolValue(0,offset,1); }
            let byte: U8=bytes[offset+index];let payload: U64=cast<U64>(byte & U8(127));
            if index == 9 && (payload > U64(1) || (byte & U8(128)) != U8(0)) { return ProtocolValue(0,offset,7); }
            value=value | (payload << cast<U64>(index*7));
            if (byte & U8(128)) == U8(0) {
                if canonical && index > 0 && payload == U64(0) { return ProtocolValue(0,offset,4); }
                return ProtocolValue(value,offset+index+1,0);
            }
        }
        return ProtocolValue(0,offset,7);
    }
    fn uleb128_write(bytes: &mut Buffer<U8>, offset: USize, value: U64) -> ProtocolValue guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if offset > len(bytes) { return ProtocolValue(0,offset,4); }
        let count: USize=uleb128_size(value);
        if count > len(bytes)-offset { return ProtocolValue(0,offset,1); }
        let mut remaining: U64=value;
        for index in 0..count {
            let mut byte: U8=cast<U8>(remaining & U64(127));remaining=remaining >> U64(7);
            if index+1 < count { byte=byte | U8(128); }
            bytes[offset+index]=byte;
        }
        return ProtocolValue(value,offset+count,0);
    }
    // ZigZag covers every I64, including MIN, without signed overflow.
    fn zigzag_encode(value: I64) -> U64 guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if value >= I64(0) { return cast<U64>(value) << U64(1); }
        return (cast<U64>(-(value+I64(1))) << U64(1)) | U64(1);
    }
    fn zigzag_decode(value: U64) -> I64 guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let half: I64=cast<I64>(value >> U64(1));
        if (value & U64(1)) == U64(0) { return half; }
        return -half-I64(1);
    }
}
