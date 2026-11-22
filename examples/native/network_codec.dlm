module examples.native.network_codec
import generated.codec_echo
import generated.codec_pos
import generated.codec_le
native {
    fn make_echo() -> EchoMessage1 {
        let mut message: EchoMessage1=EchoMessage1_new();let a: CodecError=EchoMessage1_field_1_add(&mut message,255);assert(a.kind==0);
        let mut counts: Buffer<U32>=buffer<U32>(3);counts[0]=0;counts[1]=4294967295;counts[2]=42;
        let b: CodecError=EchoMessage1_field_3_add_many(&mut message,slice(&counts,0,3));let c: CodecError=EchoMessage1_field_4_add(&mut message,U64(18446744073709551615));let d: CodecError=EchoMessage1_field_5_add(&mut message,true);assert(b.kind==0 && c.kind==0 && d.kind==0);
        let mut bytes: Buffer<U8>=buffer<U8>(4);bytes[0]=0;bytes[1]=255;bytes[2]=128;bytes[3]=42;
        let e: CodecError=EchoMessage1_field_6_add(&mut message,slice(&bytes,0,4));let f: CodecError=EchoMessage1_field_6_add(&mut message,slice(&bytes,0,0));let g: CodecError=EchoMessage1_field_7_add(&mut message,utf8_bytes("Привет, DLM"));assert(e.kind==0 && f.kind==0 && g.kind==0);
        let mut unsigned: Buffer<U64>=buffer<U64>(4);unsigned[0]=0;unsigned[1]=127;unsigned[2]=128;unsigned[3]=18446744073709551615;let mut signed: Buffer<I64>=buffer<I64>(2);signed[0]=I64(-9223372036854775808);signed[1]=I64(9223372036854775807);
        let h: CodecError=EchoMessage1_field_8_add_many(&mut message,slice(&unsigned,0,4));let i: CodecError=EchoMessage1_field_9_add_many(&mut message,slice(&signed,0,2));assert(h.kind==0 && i.kind==0);
        let newer: CodecError=EchoMessage1_field_10_add(&mut message,65535);assert(newer.kind==0);let mut unknown: Buffer<U8>=buffer<U8>(11);write_le<U32>(&mut unknown,0,99);write_le<U32>(&mut unknown,4,3);unknown[8]=0;unknown[9]=255;unknown[10]=128;let preserved: CodecError=EchoMessage1_add_unknown(&mut message,slice(&unknown,0,11));assert(preserved.kind==0);
        return move message;
    }
    fn make_pos() -> PosMessage1 {
        let mut message: PosMessage1=PosMessage1_new();let a: CodecError=PosMessage1_field_1_add(&mut message,255);assert(a.kind==0);
        let mut counts: Buffer<U32>=buffer<U32>(3);counts[0]=0;counts[1]=4294967295;counts[2]=42;
        let b: CodecError=PosMessage1_field_3_add_many(&mut message,slice(&counts,0,3));let c: CodecError=PosMessage1_field_4_add(&mut message,U64(18446744073709551615));let d: CodecError=PosMessage1_field_5_add(&mut message,true);assert(b.kind==0 && c.kind==0 && d.kind==0);
        let mut bytes: Buffer<U8>=buffer<U8>(4);bytes[0]=0;bytes[1]=255;bytes[2]=128;bytes[3]=42;
        let e: CodecError=PosMessage1_field_6_add(&mut message,slice(&bytes,0,4));let f: CodecError=PosMessage1_field_6_add(&mut message,slice(&bytes,0,0));let g: CodecError=PosMessage1_field_7_add(&mut message,utf8_bytes("Привет, DLM"));assert(e.kind==0 && f.kind==0 && g.kind==0);
        let mut unsigned: Buffer<U64>=buffer<U64>(4);unsigned[0]=0;unsigned[1]=127;unsigned[2]=128;unsigned[3]=18446744073709551615;let mut signed: Buffer<I64>=buffer<I64>(2);signed[0]=I64(-9223372036854775808);signed[1]=I64(9223372036854775807);
        let h: CodecError=PosMessage1_field_8_add_many(&mut message,slice(&unsigned,0,4));let i: CodecError=PosMessage1_field_9_add_many(&mut message,slice(&signed,0,2));assert(h.kind==0 && i.kind==0);
        let retired: CodecError=PosMessage1_field_11_add(&mut message,utf8_bytes("old"));assert(retired.kind==0);
        return move message;
    }
    fn make_little() -> LittleMessage1 {
        let mut message: LittleMessage1=LittleMessage1_new();let a: CodecError=LittleMessage1_field_1_add(&mut message,255);assert(a.kind==0);
        let mut counts: Buffer<U32>=buffer<U32>(3);counts[0]=0;counts[1]=4294967295;counts[2]=42;
        let b: CodecError=LittleMessage1_field_3_add_many(&mut message,slice(&counts,0,3));let c: CodecError=LittleMessage1_field_4_add(&mut message,U64(18446744073709551615));let d: CodecError=LittleMessage1_field_5_add(&mut message,true);assert(b.kind==0 && c.kind==0 && d.kind==0);
        let mut bytes: Buffer<U8>=buffer<U8>(4);bytes[0]=0;bytes[1]=255;bytes[2]=128;bytes[3]=42;
        let e: CodecError=LittleMessage1_field_6_add(&mut message,slice(&bytes,0,4));let f: CodecError=LittleMessage1_field_6_add(&mut message,slice(&bytes,0,0));let g: CodecError=LittleMessage1_field_7_add(&mut message,utf8_bytes("Привет, DLM"));assert(e.kind==0 && f.kind==0 && g.kind==0);
        let mut unsigned: Buffer<U64>=buffer<U64>(4);unsigned[0]=0;unsigned[1]=127;unsigned[2]=128;unsigned[3]=18446744073709551615;let mut signed: Buffer<I64>=buffer<I64>(2);signed[0]=I64(-9223372036854775808);signed[1]=I64(9223372036854775807);
        let h: CodecError=LittleMessage1_field_8_add_many(&mut message,slice(&unsigned,0,4));let i: CodecError=LittleMessage1_field_9_add_many(&mut message,slice(&signed,0,2));assert(h.kind==0 && i.kind==0);
        let retired: CodecError=LittleMessage1_field_11_add(&mut message,utf8_bytes("old"));assert(retired.kind==0);
        return move message;
    }
    fn main() -> Unit {
        let original: EchoMessage1=make_echo();let measured: CodecIo=EchoMessage1_measure(&original);assert(measured.error.kind==0);let mut bytes: Buffer<U8>=buffer<U8>(1024);for index in 0..len(&bytes) { bytes[index]=165; }
        let encoded: CodecIo=EchoMessage1_encode(&original,&mut bytes,5);assert(encoded.error.kind==0 && encoded.count==measured.count && bytes[4]==165 && bytes[encoded.next]==165);let decoded: EchoMessage1Decoded=EchoMessage1_decode(slice(&bytes,5,encoded.count));assert(decoded.error.kind==0 && EchoMessage1_equal(&original,&decoded.value));
        let number: EchoMessage1Field4=EchoMessage1_field_4_get(&decoded.value,0);let signed: EchoMessage1Field9=EchoMessage1_field_9_get(&decoded.value,0);let missing: EchoMessage1Field2=EchoMessage1_field_2_get(&decoded.value,0);assert(number.error.kind==0 && number.value==U64(18446744073709551615) && signed.value==I64(-9223372036854775808) && missing.error.kind==2);
        let view: Slice<U8>=EchoMessage1_field_7_view(&decoded.value,0);assert(len(view)==len(utf8_bytes("Привет, DLM")));let empty: CodecSpan=EchoMessage1_field_6_span(&decoded.value,1);assert(empty.error.kind==0 && empty.count==0);
        let pos: PosMessage1=make_pos();let p: CodecIo=PosMessage1_encode(&pos,&mut bytes,0);assert(p.error.kind==0);let pd: PosMessage1Decoded=PosMessage1_decode(slice(&bytes,0,p.count));assert(pd.error.kind==0 && PosMessage1_equal(&pos,&pd.value));
        let little: LittleMessage1=make_little();let l: CodecIo=LittleMessage1_encode(&little,&mut bytes,0);assert(l.error.kind==0);let ld: LittleMessage1Decoded=LittleMessage1_decode(slice(&bytes,0,l.count));assert(ld.error.kind==0 && LittleMessage1_equal(&little,&ld.value));
        let close: EchoMessage2=EchoMessage2_new();let packet: CodecIo=EchoMessage2_encode(&close,&mut bytes,0);assert(packet.error.kind==0 && packet.count==12);let dispatch: CodecDecoded=Echo_decode(slice(&bytes,0,packet.count));assert(dispatch.error.kind==0 && dispatch.value.message==2);
        print("generated codecs typed fields UTF8 integers unknowns and all wire profiles OK");
    }
}
