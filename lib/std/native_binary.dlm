module std.native_binary

native {
    // DLM binary envelope: "DLMB", U16 schema version, U16 flags;
    // then U32 field id, U32 payload length, payload bytes (little endian).
    // Unknown fields and their order survive wire_set and wire_upgrade.
    fn wire_new(version: U16) -> Buffer<U8> {
        assert(version == U16(1) || version == U16(2));
        let mut bytes: Buffer<U8> = buffer<U8>(8);
        write_le<U32>(&mut bytes, 0, 0x424D4C44);
        write_le<U16>(&mut bytes, 4, version);
        write_le<U16>(&mut bytes, 6, 0);
        return move bytes;
    }

    fn wire_valid(bytes: &Buffer<U8>) -> Bool guarantees(NoAlloc, NoBlocking, NoIO) {
        if len(bytes) < 8 { return false; }
        if read_le<U32>(bytes, 0) != U32(0x424D4C44) { return false; }
        let version: U16 = read_le<U16>(bytes, 4);
        if version != U16(1) && version != U16(2) { return false; }
        let mut position: USize = 8;
        while position < len(bytes) {
            if len(bytes) - position < 8 { return false; }
            let size: USize = cast<USize>(read_le<U32>(bytes, position + 4));
            if size > len(bytes) - position - 8 { return false; }
            position = position + 8 + size;
        }
        return true;
    }

    // Returns the field header offset, or len(bytes) if the id is absent.
    fn wire_find(bytes: &Buffer<U8>, id: U32) -> USize guarantees(NoAlloc, NoBlocking, NoIO) {
        assert(wire_valid(bytes));
        let mut position: USize = 8;
        while position < len(bytes) {
            if read_le<U32>(bytes, position) == id { return position; }
            position = position + 8 + cast<USize>(read_le<U32>(bytes, position + 4));
        }
        return len(bytes);
    }

    fn wire_field(bytes: &Buffer<U8>, id: U32) -> Slice<U8> borrows(bytes) guarantees(NoAlloc, NoBlocking, NoIO) {
        let offset: USize = wire_find(bytes, id);
        if offset == len(bytes) { return slice(bytes, len(bytes), 0); }
        let size: USize = cast<USize>(read_le<U32>(bytes, offset + 4));
        return slice(bytes, offset + 8, size);
    }

    fn wire_set(bytes: &mut Buffer<U8>, id: U32, payload: Slice<U8>) -> Unit {
        assert(wire_valid(bytes));
        assert(len(payload) <= USize(4294967295));
        let offset: USize = wire_find(bytes, id);
        let old_length: USize = len(bytes);
        let mut old_size: USize = 0;
        let mut tail: USize = old_length;
        if offset < old_length {
            old_size = 8 + cast<USize>(read_le<U32>(bytes, offset + 4));
            tail = offset + old_size;
        }
        let new_size: USize = 8 + len(payload);
        let new_length: USize = old_length - old_size + new_size;
        if new_length > old_length {
            resize(bytes, new_length);
            let mut position: USize = old_length;
            while position > tail {
                position = position - 1;
                bytes[position - old_size + new_size] = bytes[position];
            }
        } else {
            for position in tail..old_length {
                bytes[position - old_size + new_size] = bytes[position];
            }
            resize(bytes, new_length);
        }
        write_le<U32>(bytes, offset, id);
        write_le<U32>(bytes, offset + 4, cast<U32>(len(payload)));
        for index in 0..len(payload) { bytes[offset + 8 + index] = payload[index]; }
    }

    fn wire_upgrade(bytes: &mut Buffer<U8>) -> Unit guarantees(NoAlloc, NoBlocking, NoIO) {
        assert(wire_valid(bytes));
        write_le<U16>(bytes, 4, 2);
    }

    record Utf8Step repr(C) {
        codepoint: U32;
        next: USize;
        valid: Bool;
    }

    // Strict Unicode scalar decoding for text parsers: rejects overlong forms,
    // isolated continuation bytes, surrogates and values above U+10FFFF.
    fn utf8_next(bytes: Slice<U8>, offset: USize) -> Utf8Step guarantees(NoAlloc, NoBlocking, NoIO) {
        if offset >= len(bytes) { return Utf8Step(0, offset, false); }
        let first: U32 = cast<U32>(bytes[offset]);
        if first < U32(128) { return Utf8Step(first, offset + 1, true); }
        let mut count: USize = 0;
        let mut value: U32 = 0;
        let mut minimum: U32 = 0;
        if first >= U32(194) && first <= U32(223) {
            count = 2; value = first & U32(31); minimum = 128;
        } else {
            if first >= U32(224) && first <= U32(239) {
                count = 3; value = first & U32(15); minimum = 2048;
            } else {
                if first >= U32(240) && first <= U32(244) {
                    count = 4; value = first & U32(7); minimum = 65536;
                } else { return Utf8Step(0, offset, false); }
            }
        }
        if count > len(bytes) - offset { return Utf8Step(0, offset, false); }
        for index in 1..count {
            let next: U32 = cast<U32>(bytes[offset + index]);
            if next < U32(128) || next > U32(191) { return Utf8Step(0, offset, false); }
            value = (value << U32(6)) | (next & U32(63));
        }
        if value < minimum || value > U32(1114111) || (value >= U32(55296) && value <= U32(57343)) {
            return Utf8Step(0, offset, false);
        }
        return Utf8Step(value, offset + count, true);
    }
}
