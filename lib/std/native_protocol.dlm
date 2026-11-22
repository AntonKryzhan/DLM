module std.native_protocol

native {
    // Parse status: 0 complete, 1 more input required, 4 malformed, 7 limit.
    record ProtocolValue repr(C) { value: U64; next: USize; status: I32; }
    record ProtocolFrame repr(C) { payload: USize; count: USize; next: USize; status: I32; }

    fn protocol_read_be(bytes: Slice<U8>, offset: USize, width: USize) -> ProtocolValue guarantees(NoAlloc,NoBlocking,NoIO) {
        if width == 0 || width > 8 || offset > len(bytes) { return ProtocolValue(0,offset,4); }
        if width > len(bytes) - offset { return ProtocolValue(0,offset,1); }
        let mut value: U64=0;
        for index in 0..width { value=(value << U64(8)) | cast<U64>(bytes[offset+index]); }
        return ProtocolValue(value,offset+width,0);
    }
    fn protocol_write_be(bytes: &mut Buffer<U8>, offset: USize, width: USize, value: U64) -> I32 guarantees(NoAlloc,NoBlocking,NoIO) {
        if width == 0 || width > 8 || offset > len(bytes) { return 4; }
        if width > len(bytes) - offset { return 1; }
        if width < 8 { if (value >> cast<U64>(width*8)) != U64(0) { return 7; } }
        for index in 0..width { bytes[offset+index]=cast<U8>((value >> cast<U64>((width-index-1)*8)) & U64(255)); }
        return 0;
    }
    // RFC 9000 section 16. Non-minimal encodings are legal in QUIC.
    fn quic_varint_read(bytes: Slice<U8>, offset: USize) -> ProtocolValue guarantees(NoAlloc,NoBlocking,NoIO) {
        if offset > len(bytes) { return ProtocolValue(0,offset,4); }
        if offset == len(bytes) { return ProtocolValue(0,offset,1); }
        let width: USize=USize(1) << cast<USize>(bytes[offset] >> U8(6));
        if width > len(bytes) - offset { return ProtocolValue(0,offset,1); }
        let mut value: U64=cast<U64>(bytes[offset] & U8(63));
        for index in 1..width { value=(value << U64(8)) | cast<U64>(bytes[offset+index]); }
        return ProtocolValue(value,offset+width,0);
    }
    fn quic_varint_size(value: U64) -> USize guarantees(NoAlloc,NoBlocking,NoIO) {
        if value < U64(64) { return 1; }
        if value < U64(16384) { return 2; }
        if value < U64(1073741824) { return 4; }
        if value < U64(4611686018427387904) { return 8; }
        return 0;
    }
    fn quic_varint_write(bytes: &mut Buffer<U8>, offset: USize, value: U64) -> ProtocolValue guarantees(NoAlloc,NoBlocking,NoIO) {
        let width: USize=quic_varint_size(value);
        if width == 0 { return ProtocolValue(0,offset,7); }
        if offset > len(bytes) { return ProtocolValue(0,offset,4); }
        if width > len(bytes) - offset { return ProtocolValue(0,offset,1); }
        let status: I32=protocol_write_be(bytes,offset,width,value);
        if status != I32(0) { return ProtocolValue(0,offset,status); }
        let mut tag: U8=0;
        if width == 2 { tag=64; }
        if width == 4 { tag=128; }
        if width == 8 { tag=192; }
        bytes[offset]=bytes[offset] | tag;
        return ProtocolValue(value,offset+width,0);
    }
    // Incremental U32 big-endian length prefix. Never allocates from a remote
    // length and reports an oversized claim before waiting for its payload.
    fn protocol_frame32(bytes: Slice<U8>, offset: USize, maximum: USize) -> ProtocolFrame guarantees(NoAlloc,NoBlocking,NoIO) {
        let header: ProtocolValue=protocol_read_be(bytes,offset,4);
        if header.status != I32(0) { return ProtocolFrame(offset,0,offset,header.status); }
        let count: USize=cast<USize>(header.value);
        if count > maximum { return ProtocolFrame(header.next,count,offset,7); }
        if count > len(bytes) - header.next { return ProtocolFrame(header.next,count,offset,1); }
        return ProtocolFrame(header.next,count,header.next+count,0);
    }
}
