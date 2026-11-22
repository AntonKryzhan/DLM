module fixtures.dln.spec
import std.net_spec_wire
import std.net_spec_compat

native {
    export unsafe fn spec_check(data: Ptr<U8>, count: USize) -> NetworkSpecCheck {
        let source: ForeignBuffer<U8>=foreign(data,count);let input: Slice<U8>=slice(&source,0,count);
        let decoded: NetworkSpecDecoded=network_spec_decode(input);if decoded.status!=0 { return network_spec_failure(decoded.status,0,0,0); }return network_spec_check(&decoded.spec);
    }
    export unsafe fn spec_admit(data: Ptr<U8>, count: USize, available: U32) -> NetworkAdmission {
        let source: ForeignBuffer<U8>=foreign(data,count);let input: Slice<U8>=slice(&source,0,count);
        let decoded: NetworkSpecDecoded=network_spec_decode(input);if decoded.status!=0 { let mut error: NetworkAdmission=zero<NetworkAdmission>();error.status=decoded.status;error.choice.status=decoded.status;return error; }return network_spec_select(&decoded.spec,transport_candidates(available));
    }
    export unsafe fn spec_negotiate(first_data: Ptr<U8>, first_count: USize, second_data: Ptr<U8>, second_count: USize, initiator: Bool, available: U32) -> NetworkNegotiation {
        let first_source: ForeignBuffer<U8>=foreign(first_data,first_count);let second_source: ForeignBuffer<U8>=foreign(second_data,second_count);let first: Slice<U8>=slice(&first_source,0,first_count);let second: Slice<U8>=slice(&second_source,0,second_count);
        let a: NetworkSpecDecoded=network_spec_decode(first);let b: NetworkSpecDecoded=network_spec_decode(second);if a.status!=0 { return network_negotiation_failure(a.status,0,0,0); }if b.status!=0 { return network_negotiation_failure(b.status,0,0,0); }
        return network_spec_negotiate(&a.spec,&b.spec,initiator,transport_candidates(available));
    }
    export unsafe fn spec_roundtrip(data: Ptr<U8>, count: USize) -> I32 {
        let source: ForeignBuffer<U8>=foreign(data,count);let input: Slice<U8>=slice(&source,0,count);
        let decoded: NetworkSpecDecoded=network_spec_decode(input);if decoded.status!=0 { return decoded.status; }
        let mut output: Buffer<U8>=buffer<U8>(len(input));let encoded: NetworkSpecIo=network_spec_encode(&decoded.spec,&mut output,0);if encoded.status!=0 || encoded.count!=len(input) { return 11; }
        for index in 0..len(input) { if output[index]!=input[index] { return 11; } }return 0;
    }
    export unsafe fn spec_short_output_preserved(data: Ptr<U8>, count: USize) -> Bool {
        let source: ForeignBuffer<U8>=foreign(data,count);let input: Slice<U8>=slice(&source,0,count);
        let decoded: NetworkSpecDecoded=network_spec_decode(input);if decoded.status!=0 { return false; }
        let mut output: Buffer<U8>=buffer<U8>(len(input)-1);for index in 0..len(&output) { output[index]=165; }
        let encoded: NetworkSpecIo=network_spec_encode(&decoded.spec,&mut output,0);if encoded.status!=1 || encoded.count!=0 || encoded.next!=0 { return false; }
        for index in 0..len(&output) { if output[index]!=165 { return false; } }return true;
    }
    export unsafe fn spec_inactive_tail_is_semantically_ignored(data: Ptr<U8>, count: USize) -> Bool {
        let source: ForeignBuffer<U8>=foreign(data,count);let input: Slice<U8>=slice(&source,0,count);
        let decoded: NetworkSpecDecoded=network_spec_decode(input);if decoded.status!=0 { return false; }let mut changed: NetworkSpec=decoded.spec;
        if changed.message_count<16 { changed.messages[15].id=4294967295;changed.messages[15].field_count=4294967295; }
        if changed.messages[0].field_count<32 { changed.messages[0].fields[31].id=4294967295;changed.messages[0].fields[31].maximum_count=4294967295; }
        return network_spec_equal(&decoded.spec,&changed);
    }
    export fn spec_field_measure(kind: U32, minimum: U32, maximum: U32, length: U32, wire: U32) -> SchemaMeasure guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let field: MessageField=network_field(1,kind,minimum,maximum,length);if wire==1 { return network_field_measure(&field,WireFormat(1,1,0,0)); }return network_field_measure(&field,WireFormat(2,2,2,0));
    }
    export fn spec_model_error(mode: U32) -> NetworkSpecCheck guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut spec: NetworkSpec=network_spec(1,1,0,0);let mut message: MessageSchema=message_schema(1,2,3);let f: I32=message_add_field(&mut message,network_field(1,3,1,1,0));let m: I32=network_add_message(&mut spec,message);
        if mode==1 { spec.message_count=17; }if mode==2 { spec.messages[0].field_count=33; }if mode==3 { spec.messages[0].fields[0].maximum_count=4294967295; }
        if mode==4 { spec.messages[0].fields[0].id=0; }if mode==5 { spec.version.minimum_minor=1; }if mode==6 { spec.proofs.requested=2048; }
        if mode==7 { spec.transport.forbidden=16; }if mode==8 { spec.resources.maximum_schema_nodes=2; }if mode==9 { spec.resources.transport.max_message=23; }
        if mode==10 { spec.session.authentication_before_data=true; }if mode==11 { spec.messages[0].fields[0].flags=2; }
        return network_spec_check(&spec);
    }
}
