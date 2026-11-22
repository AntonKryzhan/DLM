module std.net_spec_wire
import std.net_spec
import std.net_bytes

native {
    // DNS1 is canonical descriptor metadata, not generated message codecs.
    // All integers are big endian. There are no pointers, padding or proof claims.
    record NetworkSpecIo repr(C) { count: USize; next: USize; status: I32; }
    record NetworkSpecDecoded repr(C) { spec: NetworkSpec; count: USize; status: I32; }
    fn network_spec_words(spec: &NetworkSpec) -> Array<U32,33> guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut words: Array<U32,33>=zero<Array<U32,33>>();
        words[0]=spec.protocol;words[1]=spec.version.major;words[2]=spec.version.minor;words[3]=spec.version.minimum_minor;
        words[4]=spec.wire.kind;words[5]=spec.wire.endian;words[6]=spec.wire.envelope_version;words[7]=spec.wire.unknown_fields;
        words[8]=spec.transport.required;words[9]=spec.transport.forbidden;words[10]=spec.transport.preferred;
        words[11]=spec.resources.transport.max_message;words[12]=spec.resources.transport.queued_messages;words[13]=spec.resources.transport.channels;
        let memory: U64=cast<U64>(spec.resources.transport.memory);words[14]=cast<U32>(memory>>32);words[15]=cast<U32>(memory&U64(4294967295));
        words[16]=spec.resources.maximum_unknown_fields;words[17]=spec.resources.maximum_unknown_bytes;words[18]=cast<U32>(spec.resources.maximum_session_messages>>32);words[19]=cast<U32>(spec.resources.maximum_session_messages&U64(4294967295));words[20]=spec.resources.maximum_schema_nodes;
        words[21]=spec.session.handshake_timeout_ms;words[22]=spec.session.idle_timeout_ms;words[23]=spec.session.maximum_duration_ms;words[24]=spec.session.maximum_in_flight;if spec.session.authentication_before_data { words[25]=1; }
        words[26]=spec.security.authentication;if spec.security.confidentiality { words[27]=1; }if spec.security.integrity { words[28]=1; }words[29]=spec.security.replay_scope;words[30]=spec.proofs.requested;words[31]=spec.proofs.assurance;words[32]=spec.message_count;return words;
    }
    fn network_message_words(message: &MessageSchema) -> Array<U32,6> guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        return array<Array<U32,6>>(message.id,message.phase,message.senders,message.first_minor,message.last_minor,message.field_count);
    }
    fn network_field_words(field: &MessageField) -> Array<U32,8> guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        return array<Array<U32,8>>(field.id,field.kind,field.minimum_count,field.maximum_count,field.maximum_length,field.first_minor,field.last_minor,field.flags);
    }
    fn network_spec_encoded_size(spec: &NetworkSpec) -> NetworkSpecIo guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let check: NetworkSpecCheck=network_spec_check(spec);if check.status!=0 { return NetworkSpecIo(0,0,check.status); }
        let mut count: USize=136;for index in 0..cast<USize>(spec.message_count) { count=count+24+cast<USize>(spec.messages[index].field_count)*32; }return NetworkSpecIo(count,count,0);
    }
    fn network_spec_encode(spec: &NetworkSpec, output: &mut Buffer<U8>, offset: USize) -> NetworkSpecIo guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let size: NetworkSpecIo=network_spec_encoded_size(spec);if size.status!=0 { return NetworkSpecIo(0,offset,size.status); }
        if offset>len(output) { return NetworkSpecIo(0,offset,4); }if size.count>len(output)-offset { return NetworkSpecIo(0,offset,1); }
        let magic: I32=net_write_be(output,offset,4,0x444e5331);let words: Array<U32,33>=network_spec_words(spec);let mut position: USize=offset+4;
        for index in 0..33 { let written: I32=net_write_be(output,position,4,cast<U64>(words[index]));position=position+4; }
        for index in 0..cast<USize>(spec.message_count) {
            let message: &MessageSchema=&spec.messages[index];let header: Array<U32,6>=network_message_words(message);
            for word in 0..6 { let written: I32=net_write_be(output,position,4,cast<U64>(header[word]));position=position+4; }
            for field_index in 0..cast<USize>(message.field_count) {
                let field: Array<U32,8>=network_field_words(&message.fields[field_index]);for word in 0..8 { let written: I32=net_write_be(output,position,4,cast<U64>(field[word]));position=position+4; }
            }
        }
        return NetworkSpecIo(size.count,position,0);
    }
    fn network_spec_decode_failure(status: I32) -> NetworkSpecDecoded guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) { return NetworkSpecDecoded(zero<NetworkSpec>(),0,status); }
    fn network_spec_decode(input: Slice<U8>) -> NetworkSpecDecoded guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if len(input)<136 { return network_spec_decode_failure(1); }if len(input)>16904 { return network_spec_decode_failure(7); }
        let magic: ProtocolValue=net_read_be(input,0,4);if magic.value!=U64(0x444e5331) { return network_spec_decode_failure(4); }
        let mut words: Array<U32,33>=zero<Array<U32,33>>();for index in 0..33 { let value: ProtocolValue=net_read_be(input,4+index*4,4);words[index]=cast<U32>(value.value); }
        if words[14]!=0 || words[15]>67108864 { return network_spec_decode_failure(7); }
        if words[25]>1 || words[27]>1 || words[28]>1 || words[32]==0 || words[32]>16 { return network_spec_decode_failure(4); }
        let mut spec: NetworkSpec=network_spec(words[0],words[1],words[2],words[3]);spec.wire=WireFormat(words[4],words[5],words[6],words[7]);spec.transport=TransportRequirements(words[8],words[9],words[10]);
        spec.resources=ResourceBounds(TransportBounds(words[11],words[12],words[13],cast<USize>(words[15])),words[16],words[17],(cast<U64>(words[18])<<32)|cast<U64>(words[19]),words[20]);
        spec.session=SessionContract(words[21],words[22],words[23],words[24],words[25]!=0);spec.security=SecurityRequirements(words[26],words[27]!=0,words[28]!=0,words[29]);spec.proofs=ProofObligations(words[30],words[31]);spec.message_count=words[32];let mut position: USize=136;
        for index in 0..cast<USize>(spec.message_count) {
            if len(input)-position<24 { return network_spec_decode_failure(1); }
            let mut header: Array<U32,6>=zero<Array<U32,6>>();for word in 0..6 { let value: ProtocolValue=net_read_be(input,position,4);header[word]=cast<U32>(value.value);position=position+4; }
            if header[5]>32 { return network_spec_decode_failure(4); }
            let mut message: MessageSchema=message_schema(header[0],header[1],header[2]);message.first_minor=header[3];message.last_minor=header[4];message.field_count=header[5];
            if cast<USize>(message.field_count)*32>len(input)-position { return network_spec_decode_failure(1); }
            for field_index in 0..cast<USize>(message.field_count) {
                let mut field: Array<U32,8>=zero<Array<U32,8>>();for word in 0..8 { let value: ProtocolValue=net_read_be(input,position,4);field[word]=cast<U32>(value.value);position=position+4; }
                message.fields[field_index]=MessageField(field[0],field[1],field[2],field[3],field[4],field[5],field[6],field[7]);
            }
            spec.messages[index]=message;
        }
        if position!=len(input) { return network_spec_decode_failure(4); }let check: NetworkSpecCheck=network_spec_check(&spec);if check.status!=0 { return network_spec_decode_failure(check.status); }
        return NetworkSpecDecoded(spec,position,0);
    }
    fn network_spec_equal(left: &NetworkSpec, right: &NetworkSpec) -> Bool guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let a: NetworkSpecCheck=network_spec_check(left);let b: NetworkSpecCheck=network_spec_check(right);if a.status!=0 || b.status!=0 { return false; }
        let x: Array<U32,33>=network_spec_words(left);let y: Array<U32,33>=network_spec_words(right);for index in 0..33 { if x[index]!=y[index] { return false; } }
        for index in 0..cast<USize>(left.message_count) {
            let m: &MessageSchema=&left.messages[index];let n: &MessageSchema=&right.messages[index];let p: Array<U32,6>=network_message_words(m);let q: Array<U32,6>=network_message_words(n);for word in 0..6 { if p[word]!=q[word] { return false; } }
            for field_index in 0..cast<USize>(m.field_count) { let f: Array<U32,8>=network_field_words(&m.fields[field_index]);let g: Array<U32,8>=network_field_words(&n.fields[field_index]);for word in 0..8 { if f[word]!=g[word] { return false; } } }
        }
        return true;
    }
}
