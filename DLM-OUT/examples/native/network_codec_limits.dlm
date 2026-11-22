module examples.native.network_codec_limits
import generated.codec_echo
native {
    fn main() -> Unit {
        let close: EchoMessage2=EchoMessage2_new();let mut bytes: Buffer<U8>=buffer<U8>(32);for index in 0..32 { bytes[index]=165; }
        for capacity in 0..12 {
            let mut short: Buffer<U8>=buffer<U8>(capacity);for index in 0..capacity { short[index]=165; }
            let refused: CodecIo=EchoMessage2_encode(&close,&mut short,0);assert(refused.error.kind==1 && refused.count==0);for index in 0..capacity { assert(short[index]==165); }
        }
        let bad_offset: CodecIo=EchoMessage2_encode(&close,&mut bytes,USize(4294967295));assert(bad_offset.error.kind==4 && bad_offset.count==0);for index in 0..32 { assert(bytes[index]==165); }
        let encoded: CodecIo=EchoMessage2_encode(&close,&mut bytes,3);assert(encoded.error.kind==0 && encoded.next==15 && bytes[2]==165 && bytes[15]==165);
        for prefix in 0..12 { let truncated: EchoMessage2Decoded=EchoMessage2_decode(slice(&bytes,3,prefix));assert(truncated.error.kind!=0 && truncated.value.inner.message==0 && len(&truncated.value.inner.values)==0); }
        let mut plan: CodecPlan=Echo_plan_2();plan.storage_budget=256;let exact: CodecShape=codec_scan(&plan,slice(&bytes,3,12));assert(exact.error.kind==0);plan.storage_budget=255;let below: CodecShape=codec_scan(&plan,slice(&bytes,3,12));assert(below.error.kind!=0);
        let mut message: EchoMessage1=EchoMessage1_new();let a: CodecError=EchoMessage1_field_1_add(&mut message,1);let b: CodecError=EchoMessage1_field_4_add(&mut message,2);let c: CodecError=EchoMessage1_field_5_add(&mut message,false);assert(a.kind==0 && b.kind==0 && c.kind==0);
        let before: USize=len(&message.inner.values);let missing: CodecIo=EchoMessage1_encode(&message,&mut bytes,0);assert(missing.error.kind==4 && bytes[2]==165);
        let mut invalid: Buffer<U8>=buffer<U8>(2);invalid[0]=192;invalid[1]=128;let utf8: CodecError=EchoMessage1_field_7_add(&mut message,slice(&invalid,0,2));assert(utf8.kind==4 && len(&message.inner.values)==before && len(&message.inner.bytes)==0);
        let complete: CodecError=EchoMessage1_field_9_add(&mut message,I64(-1));assert(complete.kind==0);let duplicate: CodecError=EchoMessage1_field_1_add(&mut message,2);assert(duplicate.kind!=0 && len(&message.inner.values)==4);
        let mut unknown: Buffer<U8>=buffer<U8>(8);write_le<U32>(&mut unknown,0,1);write_le<U32>(&mut unknown,4,0);let collision: CodecError=EchoMessage1_add_unknown(&mut message,slice(&unknown,0,8));assert(collision.kind==4 && len(&message.inner.unknown)==0);
        let mut output: Buffer<U8>=buffer<U8>(128);for index in 0..128 { output[index]=165; }message.inner.values[0].scalar=256;let forged: CodecIo=EchoMessage1_encode(&message,&mut output,0);assert(forged.error.kind==4);for index in 0..128 { assert(output[index]==165); }
        print("generated codec limits malformed inputs atomic output and builder checks OK");
    }
}
