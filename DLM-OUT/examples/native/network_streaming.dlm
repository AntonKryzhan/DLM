module examples.native.network_streaming
import std.net_codec

native {
    fn main() -> Unit {
        // A 1 MiB payload is consumed in 256-byte portions. The decoder owns
        // only its 20-byte state and never accumulates the message body.
        let mut state: Frame32Decoder=frame32_decoder(1048576);
        let mut chunk: Buffer<U8>=buffer<U8>(256);
        let header: I32=frame32_write_header(&mut chunk,0,1048576,1048576);assert(header == I32(0));
        for i in 0..4 {
            let step: Frame32Step=frame32_feed(&mut state,slice(&chunk,i,1),0);
            assert(step.consumed == USize(1) && step.payload_count == USize(0));
        }
        let mut total: USize=0;let mut finished: Bool=false;
        for i in 0..4096 {
            let step: Frame32Step=frame32_feed(&mut state,slice(&chunk,0,256),0);
            assert(step.status == I32(0) && step.consumed == USize(256));
            total=total+step.payload_count;finished=step.complete;
        }
        assert(total == USize(1048576) && finished && frame32_finish(&state) == I32(0));
        let mut integer: Buffer<U8>=buffer<U8>(10);
        let encoded: ProtocolValue=uleb128_write(&mut integer,0,U64(18446744073709551615));
        assert(encoded.status == I32(0) && encoded.next == USize(10));
        let decoded: ProtocolValue=uleb128_read(slice(&integer,0,10),0,true);
        assert(decoded.status == I32(0) && decoded.value == U64(18446744073709551615));
        assert(zigzag_decode(zigzag_encode(I64(-9223372036854775808))) == I64(-9223372036854775808));
        print("streaming codec OK");
    }
}
