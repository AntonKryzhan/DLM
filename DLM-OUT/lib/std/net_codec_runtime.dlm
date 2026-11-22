module std.net_codec_runtime
import std.net_spec
import std.net_bytes
import std.native_binary

native {
    record CodecPlan repr(C, version=1) { message: U32; wire: WireFormat; minor: U32; field_count: U32; fields: Array<MessageField,32>; maximum_values: U32; maximum_wire: USize; maximum_unknown_fields: U32; maximum_unknown_bytes: USize; storage_budget: USize; }
    record CodecError repr(C) { kind: I32; field: U32; offset: USize; }
    record CodecIo repr(C) { count: USize; next: USize; error: CodecError; }
    record CodecValue repr(C) { field: U32; kind: U32; scalar: U64; offset: USize; count: USize; }
    record CodecMessage repr(C) { message: U32; values: Buffer<CodecValue>; bytes: Buffer<U8>; unknown: Buffer<U8>; }
    record CodecDecoded repr(C) { value: CodecMessage; error: CodecError; }
    record CodecScalar repr(C) { value: U64; error: CodecError; }
    record CodecSpan repr(C) { offset: USize; count: USize; error: CodecError; }
    record CodecRead repr(C) { scalar: U64; offset: USize; count: USize; next: USize; error: CodecError; }
    record CodecShape repr(C) { counts: Array<U32,32>; lengths: Array<USize,32>; values: USize; bytes: USize; unknown: USize; unknown_fields: U32; wire: USize; error: CodecError; }
    record CodecReservation repr(C) { start: USize; count: USize; error: CodecError; }

    fn codec_error(kind: I32, field: U32, offset: USize) -> CodecError guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) { return CodecError(kind,field,offset); }
    fn codec_shape_error(kind: I32, field: U32, offset: USize) -> CodecShape guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) { let mut result: CodecShape=zero<CodecShape>();result.error=CodecError(kind,field,offset);return result; }
    fn codec_plan_check(plan: &CodecPlan) -> CodecError guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if plan.message==0 || plan.minor>65535 || plan.field_count>32 || network_wire_check(plan.wire)!=0 || plan.storage_budget<256 || plan.storage_budget>67108864 || plan.maximum_unknown_fields>4096 || plan.maximum_unknown_bytes>16777216 { return CodecError(4,0,0); }
        if plan.wire.unknown_fields==0 { if plan.maximum_unknown_fields!=0 || plan.maximum_unknown_bytes!=0 { return CodecError(4,0,0); } }
        else { if plan.maximum_unknown_fields==0 || plan.maximum_unknown_bytes<8 { return CodecError(4,0,0); } }
        let mut size: U64=4;if plan.wire.kind==2 { size=12; }let mut values: U32=0;let mut minimum_values: U32=0;let mut prior: U32=0;
        for index in 0..cast<USize>(plan.field_count) {
            let field: &MessageField=&plan.fields[index];if field.id<=prior || network_field_check(field)!=0 || !network_minor_active(field.first_minor,field.last_minor,plan.minor) { return CodecError(4,field.id,0); }
            prior=field.id;let measured: SchemaMeasure=network_field_measure(field,plan.wire);size=size+measured.maximum;values=values+field.maximum_count;minimum_values=minimum_values+field.minimum_count;
        }
        size=size+cast<U64>(plan.maximum_unknown_bytes);if size>16777216 || size!=cast<U64>(plan.maximum_wire) || values!=plan.maximum_values || 256+cast<USize>(minimum_values)*32>plan.storage_budget { return CodecError(4,0,0); }return CodecError(0,0,0);
    }
    fn codec_find(plan: &CodecPlan, field: U32) -> USize guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if plan.field_count>32 { return 32; }for index in 0..cast<USize>(plan.field_count) { if plan.fields[index].id==field { return index; } }return cast<USize>(plan.field_count);
    }
    // This is requested logical value storage, not allocator capacity or OS RAM.
    fn codec_storage(plan: &CodecPlan, values: USize, bytes: USize, unknown: USize) -> Bool guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if plan.storage_budget<256 || values>cast<USize>(plan.maximum_values) || bytes>plan.maximum_wire || unknown>plan.maximum_unknown_bytes { return false; }
        let available: USize=plan.storage_budget-256;if values>available/32 { return false; }let used: USize=values*32;
        if bytes>available-used { return false; }return unknown<=available-used-bytes;
    }
    fn codec_scalar_valid(kind: U32, value: U64) -> Bool guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if kind==1 { return value<=255; }if kind==2 { return value<=65535; }if kind==3 { return value<=U64(4294967295); }if kind==5 { return value<=1; }return kind==4 || kind==8 || kind==9;
    }
    fn codec_scalar_size(kind: U32, value: U64) -> USize guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if kind==1 || kind==5 { return 1; }if kind==2 { return 2; }if kind==3 { return 4; }if kind==4 { return 8; }if kind==8 || kind==9 { return uleb128_size(value); }return 0;
    }
    fn codec_utf8(input: Slice<U8>, offset: USize, count: USize) -> Bool guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if offset>len(input) || count>len(input)-offset { return false; }let end: USize=offset+count;let mut position: USize=offset;
        for index in 0..count { if position==end { return true; }let step: Utf8Step=utf8_next(input,position);if !step.valid || step.next>end { return false; }position=step.next; }return position==end;
    }
    fn codec_read_error(kind: I32, field: U32, offset: USize) -> CodecRead guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) { return CodecRead(0,0,0,offset,CodecError(kind,field,offset)); }
    fn codec_read_integer(plan: &CodecPlan, input: Slice<U8>, offset: USize, width: USize) -> ProtocolValue guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if plan.wire.endian==1 { return net_read_be(input,offset,width); }return net_read_le(input,offset,width);
    }
    fn codec_read_value(plan: &CodecPlan, field: &MessageField, input: Slice<U8>, offset: USize, end: USize) -> CodecRead guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if offset>end || end>len(input) { return codec_read_error(4,field.id,offset); }
        let mut position: USize=offset;
        if field.kind==6 || field.kind==7 {
            let mut count: USize=end-position;
            if plan.wire.kind==1 {
                if end-position<4 { return codec_read_error(1,field.id,position); }let size: ProtocolValue=codec_read_integer(plan,input,position,4);position=position+4;count=cast<USize>(size.value);
                if count>cast<USize>(field.maximum_length) { return codec_read_error(7,field.id,offset); }if count>end-position { return codec_read_error(1,field.id,offset); }
            }
            if count>cast<USize>(field.maximum_length) { return codec_read_error(7,field.id,offset); }
            if field.kind==7 && !codec_utf8(input,position,count) { return codec_read_error(4,field.id,position); }
            return CodecRead(0,position,count,position+count,CodecError(0,0,0));
        }
        if field.kind==8 || field.kind==9 {
            if plan.wire.kind==2 && (end-position==0 || end-position>10) { return codec_read_error(4,field.id,offset); }
            let value: ProtocolValue=uleb128_read(input,position,true);
            if value.status!=0 { let mut error: I32=value.status;if plan.wire.kind==2 { error=4; }return codec_read_error(error,field.id,offset); }
            if value.next>end || (plan.wire.kind==2 && value.next!=end) { return codec_read_error(4,field.id,offset); }
            return CodecRead(value.value,0,0,value.next,CodecError(0,0,0));
        }
        let width: USize=codec_scalar_size(field.kind,0);if width==0 { return codec_read_error(4,field.id,offset); }
        if plan.wire.kind==2 && end-position!=width { return codec_read_error(4,field.id,offset); }if width>end-position { return codec_read_error(1,field.id,offset); }
        let value: ProtocolValue=codec_read_integer(plan,input,position,width);if !codec_scalar_valid(field.kind,value.value) { return codec_read_error(4,field.id,offset); }return CodecRead(value.value,0,0,position+width,CodecError(0,0,0));
    }
    fn codec_unknown_shape(plan: &CodecPlan, input: Slice<U8>) -> CodecShape guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut result: CodecShape=zero<CodecShape>();if len(input)==0 { return result; }
        if plan.wire.kind!=2 || plan.wire.unknown_fields!=1 { return codec_shape_error(4,0,0); }if len(input)>plan.maximum_unknown_bytes { return codec_shape_error(7,0,0); }
        let mut position: USize=0;
        for item in 0..(len(input)/8+1) {
            if position==len(input) { break; }if len(input)-position<8 { return codec_shape_error(4,0,position); }
            let id: ProtocolValue=net_read_le(input,position,4);let size: ProtocolValue=net_read_le(input,position+4,4);let field: U32=cast<U32>(id.value);let count: USize=cast<USize>(size.value);
            if codec_find(plan,field)!=cast<USize>(plan.field_count) || count>len(input)-position-8 { return codec_shape_error(4,field,position); }
            result.unknown_fields=result.unknown_fields+1;if result.unknown_fields>plan.maximum_unknown_fields { return codec_shape_error(7,field,position); }position=position+8+count;
        }
        result.unknown=len(input);return result;
    }
    fn codec_shape_complete(plan: &CodecPlan, shape: &CodecShape) -> CodecError guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        for index in 0..cast<USize>(plan.field_count) { if shape.counts[index]<plan.fields[index].minimum_count { return CodecError(4,plan.fields[index].id,shape.wire); } }
        if !codec_storage(plan,shape.values,shape.bytes,shape.unknown) { return CodecError(7,0,shape.wire); }return CodecError(0,0,0);
    }
    fn codec_scan(plan: &CodecPlan, input: Slice<U8>) -> CodecShape guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let checked: CodecError=codec_plan_check(plan);if checked.kind!=0 { return codec_shape_error(checked.kind,checked.field,checked.offset); }
        if len(input)>plan.maximum_wire { return codec_shape_error(7,0,0); }if len(input)<4 { return codec_shape_error(1,0,0); }
        let id: ProtocolValue=net_read_be(input,0,4);if id.value!=cast<U64>(plan.message) { return codec_shape_error(11,0,0); }
        let mut result: CodecShape=zero<CodecShape>();result.wire=len(input);let mut position: USize=4;
        if plan.wire.kind==1 {
            for index in 0..cast<USize>(plan.field_count) {
                let field: &MessageField=&plan.fields[index];let mut count: U32=1;
                if field.minimum_count!=1 || field.maximum_count!=1 { if len(input)-position<4 { return codec_shape_error(1,field.id,position); }let n: ProtocolValue=codec_read_integer(plan,input,position,4);count=cast<U32>(n.value);position=position+4; }
                if count<field.minimum_count { return codec_shape_error(4,field.id,position); }if count>field.maximum_count { return codec_shape_error(7,field.id,position); }
                result.counts[index]=count;result.values=result.values+cast<USize>(count);
                for occurrence in 0..cast<USize>(count) { let value: CodecRead=codec_read_value(plan,field,input,position,len(input));if value.error.kind!=0 { return codec_shape_error(value.error.kind,value.error.field,value.error.offset); }position=value.next;result.lengths[index]=result.lengths[index]+value.count;result.bytes=result.bytes+value.count; }
            }
        }else {
            if len(input)-position<8 { return codec_shape_error(1,0,position); }
            let magic: ProtocolValue=net_read_le(input,position,4);let version: ProtocolValue=net_read_le(input,position+4,2);let flags: ProtocolValue=net_read_le(input,position+6,2);
            if magic.value!=U64(0x424d4c44) || version.value!=cast<U64>(plan.wire.envelope_version) || flags.value!=0 { return codec_shape_error(4,0,position); }position=position+8;
            for item in 0..(len(input)/8+1) {
                if position==len(input) { break; }if len(input)-position<8 { return codec_shape_error(1,0,position); }
                let tag: ProtocolValue=net_read_le(input,position,4);let size: ProtocolValue=net_read_le(input,position+4,4);let field_id: U32=cast<U32>(tag.value);let count: USize=cast<USize>(size.value);let begin: USize=position;
                if count>plan.maximum_wire { return codec_shape_error(7,field_id,begin); }let index: USize=codec_find(plan,field_id);
                if index==cast<USize>(plan.field_count) {
                    if plan.wire.unknown_fields!=1 { return codec_shape_error(4,field_id,begin); }
                    if 8+count>plan.maximum_unknown_bytes-result.unknown { return codec_shape_error(7,field_id,begin); }
                }else { let field: &MessageField=&plan.fields[index];if (field.kind==6 || field.kind==7) && count>cast<USize>(field.maximum_length) { return codec_shape_error(7,field_id,begin); } }
                if count>len(input)-position-8 { return codec_shape_error(1,field_id,position); }position=position+8;let end: USize=position+count;
                if index==cast<USize>(plan.field_count) {
                    if plan.wire.unknown_fields!=1 { return codec_shape_error(4,field_id,begin); }result.unknown_fields=result.unknown_fields+1;result.unknown=result.unknown+8+count;
                    if result.unknown_fields>plan.maximum_unknown_fields || result.unknown>plan.maximum_unknown_bytes { return codec_shape_error(7,field_id,begin); }
                }else {
                    let field: &MessageField=&plan.fields[index];result.counts[index]=result.counts[index]+1;if result.counts[index]>field.maximum_count { return codec_shape_error(7,field_id,begin); }
                    let value: CodecRead=codec_read_value(plan,field,input,position,end);if value.error.kind!=0 { return codec_shape_error(value.error.kind,value.error.field,value.error.offset); }
                    result.values=result.values+1;result.lengths[index]=result.lengths[index]+value.count;result.bytes=result.bytes+value.count;
                }
                position=end;
            }
        }
        if position!=len(input) { return codec_shape_error(4,0,position); }let complete: CodecError=codec_shape_complete(plan,&result);if complete.kind!=0 { return codec_shape_error(complete.kind,complete.field,complete.offset); }return result;
    }
    fn codec_message(id: U32) -> CodecMessage { return CodecMessage(id,buffer<CodecValue>(0),buffer<U8>(0),buffer<U8>(0)); }
    fn codec_decode_error(kind: I32, field: U32, offset: USize) -> CodecDecoded { return CodecDecoded(codec_message(0),CodecError(kind,field,offset)); }
    fn codec_decode(plan: &CodecPlan, input: Slice<U8>) -> CodecDecoded {
        let shape: CodecShape=codec_scan(plan,input);if shape.error.kind!=0 { return codec_decode_error(shape.error.kind,shape.error.field,shape.error.offset); }
        let mut result: CodecMessage=CodecMessage(plan.message,buffer<CodecValue>(shape.values),buffer<U8>(shape.bytes),buffer<U8>(shape.unknown));
        let mut value_start: Array<USize,32>=zero<Array<USize,32>>();let mut byte_start: Array<USize,32>=zero<Array<USize,32>>();let mut used: Array<U32,32>=zero<Array<U32,32>>();let mut used_bytes: Array<USize,32>=zero<Array<USize,32>>();let mut values: USize=0;let mut bytes: USize=0;
        for index in 0..cast<USize>(plan.field_count) { value_start[index]=values;byte_start[index]=bytes;values=values+cast<USize>(shape.counts[index]);bytes=bytes+shape.lengths[index]; }
        let mut position: USize=4;let mut unknown: USize=0;if plan.wire.kind==2 { position=12; }
        for item in 0..(shape.values+cast<USize>(shape.unknown_fields)+cast<USize>(plan.field_count)+1) {
            if plan.wire.kind==1 {
                if item>=cast<USize>(plan.field_count) { break; }let index: USize=item;let field: &MessageField=&plan.fields[index];if field.minimum_count!=1 || field.maximum_count!=1 { position=position+4; }
                for occurrence in 0..cast<USize>(shape.counts[index]) {
                    let value: CodecRead=codec_read_value(plan,field,input,position,len(input));let offset: USize=byte_start[index]+used_bytes[index];
                    result.values[value_start[index]+occurrence]=CodecValue(field.id,field.kind,value.scalar,0,0);
                    if field.kind==6 || field.kind==7 { result.values[value_start[index]+occurrence].offset=offset;result.values[value_start[index]+occurrence].count=value.count;for b in 0..value.count { result.bytes[offset+b]=input[value.offset+b]; }used_bytes[index]=used_bytes[index]+value.count; }position=value.next;
                }
            }else {
                if position==len(input) { break; }let begin: USize=position;let tag: ProtocolValue=net_read_le(input,position,4);let size: ProtocolValue=net_read_le(input,position+4,4);let count: USize=cast<USize>(size.value);position=position+8;let end: USize=position+count;let index: USize=codec_find(plan,cast<U32>(tag.value));
                if index==cast<USize>(plan.field_count) { for b in 0..(8+count) { result.unknown[unknown+b]=input[begin+b]; }unknown=unknown+8+count; }
                else {
                    let field: &MessageField=&plan.fields[index];let value: CodecRead=codec_read_value(plan,field,input,position,end);let slot: USize=value_start[index]+cast<USize>(used[index]);let offset: USize=byte_start[index]+used_bytes[index];
                    result.values[slot]=CodecValue(field.id,field.kind,value.scalar,0,0);
                    if field.kind==6 || field.kind==7 { result.values[slot].offset=offset;result.values[slot].count=value.count;for b in 0..value.count { result.bytes[offset+b]=input[value.offset+b]; }used_bytes[index]=used_bytes[index]+value.count; }used[index]=used[index]+1;
                }position=end;
            }
        }
        return CodecDecoded(move result,CodecError(0,0,0));
    }
    fn codec_message_inspect(plan: &CodecPlan, message: &CodecMessage, complete: Bool) -> CodecShape guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let checked: CodecError=codec_plan_check(plan);if checked.kind!=0 { return codec_shape_error(checked.kind,checked.field,checked.offset); }
        if message.message!=plan.message { return codec_shape_error(11,0,0); }if !codec_storage(plan,len(&message.values),len(&message.bytes),len(&message.unknown)) { return codec_shape_error(7,0,0); }
        let mut result: CodecShape=zero<CodecShape>();result.values=len(&message.values);result.bytes=len(&message.bytes);result.wire=4;if plan.wire.kind==2 { result.wire=12; }let mut prior: U32=0;let mut position: USize=0;
        for slot in 0..len(&message.values) {
            let value: &CodecValue=&message.values[slot];let index: USize=codec_find(plan,value.field);if index==cast<USize>(plan.field_count) || value.field<prior { return codec_shape_error(4,value.field,slot); }prior=value.field;let field: &MessageField=&plan.fields[index];if value.kind!=field.kind { return codec_shape_error(4,value.field,slot); }
            result.counts[index]=result.counts[index]+1;if result.counts[index]>field.maximum_count { return codec_shape_error(7,value.field,slot); }let mut size: USize=0;
            if field.kind==6 || field.kind==7 {
                if value.scalar!=0 || value.offset!=position || value.count>cast<USize>(field.maximum_length) || value.count>len(&message.bytes)-position { return codec_shape_error(4,value.field,slot); }
                if field.kind==7 && !codec_utf8(slice(&message.bytes,0,len(&message.bytes)),position,value.count) { return codec_shape_error(4,value.field,slot); }position=position+value.count;result.lengths[index]=result.lengths[index]+value.count;size=value.count;if plan.wire.kind==1 { size=size+4; }
            }else { if value.offset!=0 || value.count!=0 || !codec_scalar_valid(field.kind,value.scalar) { return codec_shape_error(4,value.field,slot); }size=codec_scalar_size(field.kind,value.scalar); }
            if plan.wire.kind==2 { size=size+8; }result.wire=result.wire+size;
        }
        if position!=len(&message.bytes) { return codec_shape_error(4,0,position); }
        if plan.wire.kind==1 { for index in 0..cast<USize>(plan.field_count) { if plan.fields[index].minimum_count!=1 || plan.fields[index].maximum_count!=1 { result.wire=result.wire+4; } } }
        let unknown: CodecShape=codec_unknown_shape(plan,slice(&message.unknown,0,len(&message.unknown)));if unknown.error.kind!=0 { return unknown; }result.unknown=unknown.unknown;result.unknown_fields=unknown.unknown_fields;result.wire=result.wire+result.unknown;
        if result.wire>plan.maximum_wire { return codec_shape_error(7,0,0); }if complete { let required: CodecError=codec_shape_complete(plan,&result);if required.kind!=0 { return codec_shape_error(required.kind,required.field,required.offset); } }return result;
    }
    fn codec_measure(plan: &CodecPlan, message: &CodecMessage) -> CodecIo guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let shape: CodecShape=codec_message_inspect(plan,message,true);if shape.error.kind!=0 { return CodecIo(0,0,shape.error); }return CodecIo(shape.wire,shape.wire,CodecError(0,0,0));
    }
    fn codec_reserve_scalars(plan: &CodecPlan, message: &mut CodecMessage, field_id: U32, count: USize) -> CodecReservation {
        let shape: CodecShape=codec_message_inspect(plan,message,false);if shape.error.kind!=0 { return CodecReservation(0,0,shape.error); }let index: USize=codec_find(plan,field_id);
        if index==cast<USize>(plan.field_count) { return CodecReservation(0,0,CodecError(4,field_id,0)); }let field: &MessageField=&plan.fields[index];if field.kind==6 || field.kind==7 { return CodecReservation(0,0,CodecError(4,field_id,0)); }
        let start: USize=len(&message.values);if start!=0 { if message.values[start-1].field>field_id { return CodecReservation(0,0,CodecError(4,field_id,start)); } }
        if count>cast<USize>(field.maximum_count-shape.counts[index]) || !codec_storage(plan,start+count,len(&message.bytes),len(&message.unknown)) { return CodecReservation(0,0,CodecError(7,field_id,start)); }
        resize(&mut message.values,start+count);return CodecReservation(start,count,CodecError(0,0,0));
    }
    fn codec_add_scalar(plan: &CodecPlan, message: &mut CodecMessage, field_id: U32, scalar: U64) -> CodecError {
        let index: USize=codec_find(plan,field_id);if index==cast<USize>(plan.field_count) || plan.field_count>32 { return CodecError(4,field_id,0); }let kind: U32=plan.fields[index].kind;if !codec_scalar_valid(kind,scalar) { return CodecError(4,field_id,0); }
        let reserved: CodecReservation=codec_reserve_scalars(plan,message,field_id,1);if reserved.error.kind!=0 { return reserved.error; }message.values[reserved.start]=CodecValue(field_id,kind,scalar,0,0);return CodecError(0,0,0);
    }
    fn codec_add_bytes(plan: &CodecPlan, message: &mut CodecMessage, field_id: U32, input: Slice<U8>) -> CodecError {
        let shape: CodecShape=codec_message_inspect(plan,message,false);if shape.error.kind!=0 { return shape.error; }let index: USize=codec_find(plan,field_id);if index==cast<USize>(plan.field_count) { return CodecError(4,field_id,0); }let field: &MessageField=&plan.fields[index];
        if (field.kind!=6 && field.kind!=7) || len(input)>cast<USize>(field.maximum_length) { return CodecError(4,field_id,0); }if field.kind==7 && !codec_utf8(input,0,len(input)) { return CodecError(4,field_id,0); }
        let start: USize=len(&message.values);let offset: USize=len(&message.bytes);if start!=0 { if message.values[start-1].field>field_id { return CodecError(4,field_id,start); } }
        if shape.counts[index]==field.maximum_count || !codec_storage(plan,start+1,offset+len(input),len(&message.unknown)) { return CodecError(7,field_id,start); }
        resize(&mut message.bytes,offset+len(input));for b in 0..len(input) { message.bytes[offset+b]=input[b]; }resize(&mut message.values,start+1);message.values[start]=CodecValue(field_id,field.kind,0,offset,len(input));return CodecError(0,0,0);
    }
    fn codec_add_unknown(plan: &CodecPlan, message: &mut CodecMessage, input: Slice<U8>) -> CodecError {
        let shape: CodecShape=codec_message_inspect(plan,message,false);if shape.error.kind!=0 { return shape.error; }let extra: CodecShape=codec_unknown_shape(plan,input);if extra.error.kind!=0 { return extra.error; }
        let offset: USize=len(&message.unknown);if extra.unknown_fields>plan.maximum_unknown_fields-shape.unknown_fields || codec_unknown_over_budget(plan,offset,len(input)) { return CodecError(7,0,offset); }
        if !codec_storage(plan,len(&message.values),len(&message.bytes),offset+len(input)) { return CodecError(7,0,offset); }resize(&mut message.unknown,offset+len(input));for b in 0..len(input) { message.unknown[offset+b]=input[b]; }return CodecError(0,0,0);
    }
    fn codec_unknown_over_budget(plan: &CodecPlan, used: USize, count: USize) -> Bool guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) { return used>plan.maximum_unknown_bytes || count>plan.maximum_unknown_bytes-used; }
    fn codec_get(plan: &CodecPlan, message: &CodecMessage, field_id: U32, occurrence: USize) -> CodecValue guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut seen: USize=0;for index in 0..len(&message.values) { let value: CodecValue=message.values[index];if value.field==field_id { if seen==occurrence { return value; }seen=seen+1; } }return zero<CodecValue>();
    }
    fn codec_get_scalar(plan: &CodecPlan, message: &CodecMessage, field_id: U32, occurrence: USize) -> CodecScalar guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let checked: CodecShape=codec_message_inspect(plan,message,false);if checked.error.kind!=0 { return CodecScalar(0,checked.error); }let value: CodecValue=codec_get(plan,message,field_id,occurrence);if value.field==0 { return CodecScalar(0,CodecError(2,field_id,0)); }if value.kind==6 || value.kind==7 { return CodecScalar(0,CodecError(4,field_id,0)); }return CodecScalar(value.scalar,CodecError(0,0,0));
    }
    fn codec_get_span(plan: &CodecPlan, message: &CodecMessage, field_id: U32, occurrence: USize) -> CodecSpan guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let checked: CodecShape=codec_message_inspect(plan,message,false);if checked.error.kind!=0 { return CodecSpan(0,0,checked.error); }let value: CodecValue=codec_get(plan,message,field_id,occurrence);if value.field==0 { return CodecSpan(0,0,CodecError(2,field_id,0)); }if value.kind!=6 && value.kind!=7 { return CodecSpan(0,0,CodecError(4,field_id,0)); }return CodecSpan(value.offset,value.count,CodecError(0,0,0));
    }
    fn codec_view(message: &CodecMessage, span: CodecSpan) -> Slice<U8> borrows(message) guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if span.error.kind!=0 || span.offset>len(&message.bytes) || span.count>len(&message.bytes)-span.offset { return slice(&message.bytes,0,0); }return slice(&message.bytes,span.offset,span.count);
    }
    fn codec_write_integer(plan: &CodecPlan, output: &mut Buffer<U8>, offset: USize, width: USize, value: U64) -> I32 guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if plan.wire.endian==1 { return net_write_be(output,offset,width,value); }return net_write_le(output,offset,width,value);
    }
    fn codec_encode(plan: &CodecPlan, message: &CodecMessage, output: &mut Buffer<U8>, offset: USize) -> CodecIo guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let shape: CodecShape=codec_message_inspect(plan,message,true);if shape.error.kind!=0 { return CodecIo(0,offset,shape.error); }
        if offset>len(output) { return CodecIo(0,offset,CodecError(4,0,offset)); }if shape.wire>len(output)-offset { return CodecIo(0,offset,CodecError(1,0,offset)); }
        let first: I32=net_write_be(output,offset,4,cast<U64>(plan.message));let mut position: USize=offset+4;
        if plan.wire.kind==2 { let a: I32=net_write_le(output,position,4,0x424d4c44);let b: I32=net_write_le(output,position+4,2,cast<U64>(plan.wire.envelope_version));let c: I32=net_write_le(output,position+6,2,0);position=position+8; }
        let mut slot: USize=0;
        for index in 0..cast<USize>(plan.field_count) {
            let field: &MessageField=&plan.fields[index];if plan.wire.kind==1 && (field.minimum_count!=1 || field.maximum_count!=1) { let count: I32=codec_write_integer(plan,output,position,4,cast<U64>(shape.counts[index]));position=position+4; }
            for occurrence in 0..cast<USize>(shape.counts[index]) {
                let value: CodecValue=message.values[slot];slot=slot+1;let mut count: USize=codec_scalar_size(field.kind,value.scalar);if field.kind==6 || field.kind==7 { count=value.count; }
                if plan.wire.kind==2 { let tag: I32=net_write_le(output,position,4,cast<U64>(field.id));let size: I32=net_write_le(output,position+4,4,cast<U64>(count));position=position+8; }
                if field.kind==6 || field.kind==7 { if plan.wire.kind==1 { let size: I32=codec_write_integer(plan,output,position,4,cast<U64>(count));position=position+4; }for b in 0..count { output[position+b]=message.bytes[value.offset+b]; }position=position+count; }
                else if field.kind==8 || field.kind==9 { let written: ProtocolValue=uleb128_write(output,position,value.scalar);position=written.next; }
                else { let written: I32=codec_write_integer(plan,output,position,count,value.scalar);position=position+count; }
            }
        }
        for b in 0..len(&message.unknown) { output[position+b]=message.unknown[b]; }position=position+len(&message.unknown);return CodecIo(shape.wire,position,CodecError(0,0,0));
    }
    fn codec_equal(plan: &CodecPlan, left: &CodecMessage, right: &CodecMessage) -> Bool guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let a: CodecShape=codec_message_inspect(plan,left,true);let b: CodecShape=codec_message_inspect(plan,right,true);if a.error.kind!=0 || b.error.kind!=0 || len(&left.values)!=len(&right.values) || len(&left.bytes)!=len(&right.bytes) || len(&left.unknown)!=len(&right.unknown) { return false; }
        for index in 0..len(&left.values) { let x: CodecValue=left.values[index];let y: CodecValue=right.values[index];if x.field!=y.field || x.kind!=y.kind || x.scalar!=y.scalar || x.offset!=y.offset || x.count!=y.count { return false; } }
        for index in 0..len(&left.bytes) { if left.bytes[index]!=right.bytes[index] { return false; } }for index in 0..len(&left.unknown) { if left.unknown[index]!=right.unknown[index] { return false; } }return true;
    }
}
