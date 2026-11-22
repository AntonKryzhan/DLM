module fixtures.dln.codec
import std.net_codec

native {
    export unsafe fn codec_read_integer(data: Ptr<U8>, count: USize, offset: USize, width: USize, little: Bool) -> ProtocolValue guarantees(NoAlloc,NoBlocking,NoIO) {
        let bytes: ForeignBuffer<U8>=foreign(data,count);
        if little { return net_read_le(slice(&bytes,0,count),offset,width); }
        return net_read_be(slice(&bytes,0,count),offset,width);
    }
    export unsafe fn codec_write_integer(data: Ptr<U8>, count: USize, offset: USize, width: USize, value: U64, little: Bool) -> I32 {
        if count > USize(64) { return 7; }
        let mut out: Buffer<U8>=buffer<U8>(count);let mut target: ForeignBuffer<U8>=foreign(data,count);
        for i in 0..count { out[i]=target[i]; }
        let mut status: I32=0;
        if little { status=net_write_le(&mut out,offset,width,value); }
        else { status=net_write_be(&mut out,offset,width,value); }
        for i in 0..count { target[i]=out[i]; }
        return status;
    }
    export unsafe fn codec_read_varint(data: Ptr<U8>, count: USize, offset: USize, canonical: Bool) -> ProtocolValue guarantees(NoAlloc,NoBlocking,NoIO) {
        let bytes: ForeignBuffer<U8>=foreign(data,count);return uleb128_read(slice(&bytes,0,count),offset,canonical);
    }
    export unsafe fn codec_write_varint(data: Ptr<U8>, count: USize, offset: USize, value: U64) -> ProtocolValue {
        if count > USize(64) { return ProtocolValue(0,offset,7); }
        let mut out: Buffer<U8>=buffer<U8>(count);let mut target: ForeignBuffer<U8>=foreign(data,count);
        for i in 0..count { out[i]=target[i]; }
        let result: ProtocolValue=uleb128_write(&mut out,offset,value);
        for i in 0..count { target[i]=out[i]; }
        return result;
    }
    export fn codec_zigzag_encode(value: I64) -> U64 guarantees(NoAlloc,NoBlocking,NoIO) { return zigzag_encode(value); }
    export fn codec_zigzag_decode(value: U64) -> I64 guarantees(NoAlloc,NoBlocking,NoIO) { return zigzag_decode(value); }
    export fn codec_frame_new(maximum: U32) -> Frame32Decoder guarantees(NoAlloc,NoBlocking,NoIO) { return frame32_decoder(maximum); }
    export unsafe fn codec_frame_feed(state: Ptr<Frame32Decoder>, data: Ptr<U8>, count: USize, offset: USize) -> Frame32Step guarantees(NoAlloc,NoBlocking,NoIO) {
        let bytes: ForeignBuffer<U8>=foreign(data,count);return frame32_feed(&mut *state,slice(&bytes,0,count),offset);
    }
    export unsafe fn codec_frame_finish(state: Ptr<Frame32Decoder>) -> I32 guarantees(NoAlloc,NoBlocking,NoIO) { return frame32_finish(&*state); }
    export unsafe fn codec_frame_reset(state: Ptr<Frame32Decoder>) -> Unit guarantees(NoAlloc,NoBlocking,NoIO) { frame32_reset(&mut *state); }
}
