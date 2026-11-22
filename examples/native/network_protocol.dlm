module examples.native.network_protocol
import std.native_protocol

native {
    fn main() -> Unit {
        let mut bytes: Buffer<U8>=buffer<U8>(8);
        let encoded: ProtocolValue=quic_varint_write(&mut bytes,0,151288809941952652);
        assert(encoded.status == I32(0) && encoded.next == USize(8));
        let decoded: ProtocolValue=quic_varint_read(slice(&bytes,0,len(&bytes)),0);
        assert(decoded.value == U64(151288809941952652));
        let incomplete: ProtocolValue=quic_varint_read(slice(&bytes,0,7),0);
        assert(incomplete.status == I32(1));
        let huge: I32=protocol_write_be(&mut bytes,0,4,4294967295);
        assert(huge == I32(0));
        let rejected: ProtocolFrame=protocol_frame32(slice(&bytes,0,4),0,65536);
        assert(rejected.status == I32(7));
        print("protocol boundaries OK");
    }
}
