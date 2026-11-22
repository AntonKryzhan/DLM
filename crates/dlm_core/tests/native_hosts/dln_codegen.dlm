module fixtures.dln.codegen
import generated.codec_echo
import generated.codec_pos
import generated.codec_le

native {
    record CodecProbe repr(C) { error: CodecError; canonical_count: USize; scalar: U64; signed_value: I64; values: USize; unknown: USize; roundtrip: Bool; }
    record CodecOwners repr(C) { first: CodecMessage; second: CodecMessage; }
    fn codec_fixture_plan(format: U32) -> CodecPlan guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if format==0 { return Echo_plan_1(); }if format==1 { return Pos_plan_1(); }if format==2 { return Little_plan_1(); }return zero<CodecPlan>();
    }
    fn codec_fixture_decode(input: Slice<U8>, format: U32) -> CodecDecoded {
        if format==0 { let decoded: EchoMessage1Decoded=EchoMessage1_decode(input);return CodecDecoded(move decoded.value.inner,decoded.error); }
        if format==1 { let decoded: PosMessage1Decoded=PosMessage1_decode(input);return CodecDecoded(move decoded.value.inner,decoded.error); }
        if format==2 { let decoded: LittleMessage1Decoded=LittleMessage1_decode(input);return CodecDecoded(move decoded.value.inner,decoded.error); }return codec_decode_error(4,0,0);
    }
    export unsafe fn codec_probe(data: Ptr<U8>, count: USize, format: U32) -> CodecProbe {
        let source: ForeignBuffer<U8>=foreign(data,count);let decoded: CodecDecoded=codec_fixture_decode(slice(&source,0,count),format);let mut result: CodecProbe=zero<CodecProbe>();result.error=decoded.error;if decoded.error.kind!=0 { return result; }
        let plan: CodecPlan=codec_fixture_plan(format);let size: CodecIo=codec_measure(&plan,&decoded.value);result.error=size.error;if size.error.kind!=0 { return result; }result.canonical_count=size.count;
        let mut bytes: Buffer<U8>=buffer<U8>(size.count);let encoded: CodecIo=codec_encode(&plan,&decoded.value,&mut bytes,0);if encoded.error.kind!=0 { result.error=encoded.error;return result; }
        let again: CodecDecoded=codec_decode(&plan,slice(&bytes,0,size.count));result.roundtrip=again.error.kind==0 && codec_equal(&plan,&decoded.value,&again.value);let scalar: CodecScalar=codec_get_scalar(&plan,&decoded.value,4,0);let signed: CodecScalar=codec_get_scalar(&plan,&decoded.value,9,0);result.scalar=scalar.value;result.signed_value=zigzag_decode(signed.value);result.values=len(&decoded.value.values);result.unknown=len(&decoded.value.unknown);return result;
    }
    export unsafe fn codec_canonical(data: Ptr<U8>, count: USize, format: U32, output: Ptr<U8>, capacity: USize) -> CodecIo {
        let source: ForeignBuffer<U8>=foreign(data,count);let decoded: CodecDecoded=codec_fixture_decode(slice(&source,0,count),format);if decoded.error.kind!=0 { return CodecIo(0,0,decoded.error); }let plan: CodecPlan=codec_fixture_plan(format);let size: CodecIo=codec_measure(&plan,&decoded.value);if size.error.kind!=0 { return size; }if capacity<size.count { return CodecIo(0,0,CodecError(1,0,0)); }
        let mut bytes: Buffer<U8>=buffer<U8>(size.count);let encoded: CodecIo=codec_encode(&plan,&decoded.value,&mut bytes,0);if encoded.error.kind!=0 { return encoded; }let mut target: ForeignBuffer<U8>=foreign(output,capacity);for index in 0..size.count { target[index]=bytes[index]; }return encoded;
    }
    fn codec_fixture_message(format: U32) -> CodecMessage {
        let plan: CodecPlan=codec_fixture_plan(format);let mut message: CodecMessage=codec_message(1);let a: CodecError=codec_add_scalar(&plan,&mut message,1,255);let b: CodecError=codec_add_scalar(&plan,&mut message,2,65535);assert(a.kind==0 && b.kind==0);
        let batch: CodecReservation=codec_reserve_scalars(&plan,&mut message,3,3);assert(batch.error.kind==0);message.values[batch.start]=CodecValue(3,3,0,0,0);message.values[batch.start+1]=CodecValue(3,3,4294967295,0,0);message.values[batch.start+2]=CodecValue(3,3,42,0,0);
        let c: CodecError=codec_add_scalar(&plan,&mut message,4,U64(18446744073709551615));let d: CodecError=codec_add_scalar(&plan,&mut message,5,1);assert(c.kind==0 && d.kind==0);
        let mut bytes: Buffer<U8>=buffer<U8>(4);bytes[0]=0;bytes[1]=255;bytes[2]=128;bytes[3]=42;let e: CodecError=codec_add_bytes(&plan,&mut message,6,slice(&bytes,0,4));let f: CodecError=codec_add_bytes(&plan,&mut message,6,slice(&bytes,0,0));let g: CodecError=codec_add_bytes(&plan,&mut message,7,utf8_bytes("Привет"));assert(e.kind==0 && f.kind==0 && g.kind==0);
        let h: CodecError=codec_add_scalar(&plan,&mut message,8,0);let i: CodecError=codec_add_scalar(&plan,&mut message,8,127);let j: CodecError=codec_add_scalar(&plan,&mut message,8,128);let k: CodecError=codec_add_scalar(&plan,&mut message,8,U64(18446744073709551615));let l: CodecError=codec_add_scalar(&plan,&mut message,9,zigzag_encode(I64(-9223372036854775808)));let m: CodecError=codec_add_scalar(&plan,&mut message,9,zigzag_encode(I64(9223372036854775807)));assert(h.kind==0 && i.kind==0 && j.kind==0 && k.kind==0 && l.kind==0 && m.kind==0);
        if format==0 {
            let n: CodecError=codec_add_scalar(&plan,&mut message,10,65535);assert(n.kind==0);let mut unknown: Buffer<U8>=buffer<U8>(19);write_le<U32>(&mut unknown,0,99);write_le<U32>(&mut unknown,4,3);unknown[8]=0;unknown[9]=255;unknown[10]=128;write_le<U32>(&mut unknown,11,0);write_le<U32>(&mut unknown,15,0);let added: CodecError=codec_add_unknown(&plan,&mut message,slice(&unknown,0,19));assert(added.kind==0);
        }else { let n: CodecError=codec_add_bytes(&plan,&mut message,11,utf8_bytes("old"));assert(n.kind==0); }return move message;
    }
    export unsafe fn codec_serialize_demo(format: U32, output: Ptr<U8>, capacity: USize) -> CodecIo {
        if format>2 { return CodecIo(0,0,CodecError(4,0,0)); }let plan: CodecPlan=codec_fixture_plan(format);let message: CodecMessage=codec_fixture_message(format);let size: CodecIo=codec_measure(&plan,&message);if size.error.kind!=0 { return size; }if capacity<size.count { return CodecIo(0,0,CodecError(1,0,0)); }
        let mut bytes: Buffer<U8>=buffer<U8>(size.count);let encoded: CodecIo=codec_encode(&plan,&message,&mut bytes,0);let mut target: ForeignBuffer<U8>=foreign(output,capacity);for index in 0..size.count { target[index]=bytes[index]; }return encoded;
    }
    export unsafe fn codec_probe_budget(data: Ptr<U8>, count: USize, format: U32, budget: USize) -> CodecError {
        let source: ForeignBuffer<U8>=foreign(data,count);let mut plan: CodecPlan=codec_fixture_plan(format);plan.storage_budget=budget;let checked: CodecShape=codec_scan(&plan,slice(&source,0,count));return checked.error;
    }
    export fn codec_builder_failures() -> U32 {
        let plan: CodecPlan=Echo_plan_1();let mut message: CodecMessage=codec_message(1);let mut flags: U32=0;let invalid: CodecError=codec_add_scalar(&plan,&mut message,1,256);if invalid.kind==4 && len(&message.values)==0 { flags=flags|1; }
        let huge: CodecReservation=codec_reserve_scalars(&plan,&mut message,3,USize(4294967295));if huge.error.kind==7 && len(&message.values)==0 { flags=flags|2; }
        let mut bad: Buffer<U8>=buffer<U8>(2);bad[0]=192;bad[1]=128;let utf8: CodecError=codec_add_bytes(&plan,&mut message,7,slice(&bad,0,2));if utf8.kind==4 && len(&message.values)==0 && len(&message.bytes)==0 { flags=flags|4; }
        let mut out: Buffer<U8>=buffer<U8>(64);for index in 0..64 { out[index]=165; }let missing: CodecIo=codec_encode(&plan,&message,&mut out,0);if missing.error.kind==4 && missing.count==0 && out[0]==165 && out[63]==165 { flags=flags|8; }
        let mut unknown: Buffer<U8>=buffer<U8>(8);write_le<U32>(&mut unknown,0,1);write_le<U32>(&mut unknown,4,0);let smuggled: CodecError=codec_add_unknown(&plan,&mut message,slice(&unknown,0,8));if smuggled.kind==4 && len(&message.unknown)==0 { flags=flags|16; }
        let mut wrong: CodecPlan=plan;wrong.field_count=33;let checked: CodecError=codec_plan_check(&wrong);if checked.kind==4 { flags=flags|32; }return flags;
    }
    export fn codec_abort_owners() -> Unit {
        let plan: CodecPlan=Echo_plan_1();let mut owners: CodecOwners=CodecOwners(codec_message(1),codec_message(1));let a: CodecError=codec_add_scalar(&plan,&mut owners.first,1,1);let b: CodecError=codec_add_scalar(&plan,&mut owners.second,1,2);assert(a.kind==0 && b.kind==0);let moved: CodecMessage=move owners.first;assert(false);
    }
    export unsafe fn codec_dispatch(data: Ptr<U8>, count: USize, format: U32) -> CodecScalar {
        let source: ForeignBuffer<U8>=foreign(data,count);if format==0 { let decoded: CodecDecoded=Echo_decode(slice(&source,0,count));return CodecScalar(cast<U64>(decoded.value.message),decoded.error); }if format==1 { let decoded: CodecDecoded=Pos_decode(slice(&source,0,count));return CodecScalar(cast<U64>(decoded.value.message),decoded.error); }if format==2 { let decoded: CodecDecoded=Little_decode(slice(&source,0,count));return CodecScalar(cast<U64>(decoded.value.message),decoded.error); }return CodecScalar(0,CodecError(4,0,0));
    }
}
