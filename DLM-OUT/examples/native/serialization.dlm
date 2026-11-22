module native.serialization
import std.native_binary

native {
    fn main() -> Unit {
        let mut bytes: Buffer<U8> = wire_new(1);
        let mut known: Buffer<U8> = buffer<U8>(8);
        write_le<F64>(&mut known, 0, 0.625);
        wire_set(&mut bytes, 1, slice(&known, 0, 8));
        let unknown: Slice<U8> = utf8_bytes("future field");
        wire_set(&mut bytes, 900, unknown);
        write_le<F64>(&mut known, 0, 0.75);
        wire_set(&mut bytes, 1, slice(&known, 0, 8));
        wire_upgrade(&mut bytes);
        assert(wire_valid(&bytes));
        {
            let value: Slice<U8> = wire_field(&bytes, 1);
            assert(read_le<F64>(&value, 0) == 0.75);
            let future: Slice<U8> = wire_field(&bytes, 900);
            assert(len(future) == 12);
            assert(future[0] == U8(102));
        }
        let unicode: Slice<U8> = utf8_bytes("Антон");
        let first: Utf8Step = utf8_next(unicode, 0);
        assert(first.valid);
        assert(first.codepoint == U32(1040));
        print("serialization OK");
    }
}
