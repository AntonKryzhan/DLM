module std.net_spec
import std.net_transport_policy

native {
    record ProtocolVersion repr(C, version=1) { major: U32; minor: U32; minimum_minor: U32; }
    record WireFormat repr(C, version=1) { kind: U32; endian: U32; envelope_version: U32; unknown_fields: U32; }
    record MessageField repr(C, version=1) { id: U32; kind: U32; minimum_count: U32; maximum_count: U32; maximum_length: U32; first_minor: U32; last_minor: U32; flags: U32; }
    record MessageSchema repr(C, version=1) { id: U32; phase: U32; senders: U32; first_minor: U32; last_minor: U32; field_count: U32; fields: Array<MessageField,32>; }
    record SessionContract repr(C, version=1) { handshake_timeout_ms: U32; idle_timeout_ms: U32; maximum_duration_ms: U32; maximum_in_flight: U32; authentication_before_data: Bool; }
    record SecurityRequirements repr(C, version=1) { authentication: U32; confidentiality: Bool; integrity: Bool; replay_scope: U32; }
    record ResourceBounds repr(C, version=1) { transport: TransportBounds; maximum_unknown_fields: U32; maximum_unknown_bytes: U32; maximum_session_messages: U64; maximum_schema_nodes: U32; }
    record ProofObligations repr(C, version=1) { requested: U32; assurance: U32; }
    record NetworkSpec repr(C, version=1) { protocol: U32; version: ProtocolVersion; wire: WireFormat; transport: TransportRequirements; security: SecurityRequirements; resources: ResourceBounds; session: SessionContract; proofs: ProofObligations; message_count: U32; messages: Array<MessageSchema,16>; }
    record SchemaMeasure repr(C) { minimum: U64; maximum: U64; status: I32; }
    record NetworkSpecCheck repr(C) { status: I32; component: U32; message: U32; field: U32; minimum: USize; maximum: USize; required_properties: U32; required_proofs: U32; pending_proofs: U32; unsupported_security: U32; schema_nodes: U32; }
    record NetworkAdmission repr(C) { requirements: TransportRequirements; bounds: TransportBounds; choice: TransportChoice; session: SessionContract; maximum_session_messages: U64; pending_proofs: U32; unsupported_security: U32; status: I32; }

    // Field kinds: unsigned 8/16/32/64, Bool, bytes, UTF-8, ULEB128,
    // ZigZag-I64. Count limits are per field; byte limits are per occurrence.
    fn network_field(id: U32, kind: U32, minimum: U32, maximum: U32, length: U32) -> MessageField guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        return MessageField(id,kind,minimum,maximum,length,0,4294967295,0);
    }
    fn message_schema(id: U32, phase: U32, senders: U32) -> MessageSchema guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut result: MessageSchema=zero<MessageSchema>();result.id=id;result.phase=phase;result.senders=senders;result.last_minor=4294967295;return result;
    }
    fn network_spec(protocol: U32, major: U32, minor: U32, minimum: U32) -> NetworkSpec guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut result: NetworkSpec=zero<NetworkSpec>();result.protocol=protocol;result.version=ProtocolVersion(major,minor,minimum);result.wire=WireFormat(2,2,2,0);
        result.transport=TransportRequirements(16,0,0);result.resources=ResourceBounds(transport_bounds(),0,0,1000000,529);result.session=SessionContract(10000,30000,300000,32,false);result.proofs=ProofObligations(0,1);return result;
    }
    fn message_add_field(message: &mut MessageSchema, field: MessageField) -> I32 guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if message.field_count>=32 { return 7; }if field.id==0 { return 4; }
        if message.field_count!=0 { if message.fields[cast<USize>(message.field_count-1)].id>=field.id { return 4; } }
        message.fields[cast<USize>(message.field_count)]=field;message.field_count=message.field_count+1;return 0;
    }
    fn network_add_message(spec: &mut NetworkSpec, message: MessageSchema) -> I32 guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if spec.message_count>=16 { return 7; }if message.id==0 { return 4; }
        if spec.message_count!=0 { if spec.messages[cast<USize>(spec.message_count-1)].id>=message.id { return 4; } }
        spec.messages[cast<USize>(spec.message_count)]=message;spec.message_count=spec.message_count+1;return 0;
    }
    fn network_minor_active(first: U32, last: U32, minor: U32) -> Bool guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) { return first<=minor && minor<=last; }
    fn network_field_check(field: &MessageField) -> I32 guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if field.id==0 || field.kind==0 || field.kind>9 || field.minimum_count>field.maximum_count || field.maximum_count==0 || field.maximum_count>4096 || field.maximum_length>16777216 || field.first_minor>field.last_minor || (field.flags&U32(4294967294))!=0 { return 4; }
        if field.kind!=6 && field.kind!=7 && field.maximum_length!=0 { return 4; }return 0;
    }
    fn network_wire_check(wire: WireFormat) -> I32 guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if wire.kind==1 { if (wire.endian==1 || wire.endian==2) && wire.envelope_version==0 && wire.unknown_fields==0 { return 0; } }
        if wire.kind==2 { if wire.endian==2 && (wire.envelope_version==1 || wire.envelope_version==2) && wire.unknown_fields<=1 { return 0; } }return 4;
    }
    fn network_field_measure(field: &MessageField, wire: WireFormat) -> SchemaMeasure guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if network_field_check(field)!=0 || network_wire_check(wire)!=0 { return SchemaMeasure(0,0,4); }
        let mut minimum: U64=1;let mut maximum: U64=1;
        if field.kind==2 { minimum=2;maximum=2; }if field.kind==3 { minimum=4;maximum=4; }if field.kind==4 { minimum=8;maximum=8; }
        if field.kind==6 || field.kind==7 { minimum=0;maximum=cast<U64>(field.maximum_length);if wire.kind==1 { minimum=4;maximum=maximum+4; } }
        if field.kind==8 || field.kind==9 { maximum=10; }
        let mut prefix: U64=0;if wire.kind==2 { minimum=minimum+8;maximum=maximum+8; }else { if field.minimum_count!=1 || field.maximum_count!=1 { prefix=4; } }
        return SchemaMeasure(prefix+minimum*cast<U64>(field.minimum_count),prefix+maximum*cast<U64>(field.maximum_count),0);
    }
    // Four-byte message ID, then positional data or the unchanged DLMB envelope.
    fn network_message_measure(message: &MessageSchema, wire: WireFormat, minor: U32) -> SchemaMeasure guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if message.field_count>32 || network_wire_check(wire)!=0 { return SchemaMeasure(0,0,4); }
        if !network_minor_active(message.first_minor,message.last_minor,minor) { return SchemaMeasure(0,0,6); }
        let mut minimum: U64=4;if wire.kind==2 { minimum=12; }let mut maximum: U64=minimum;
        for index in 0..cast<USize>(message.field_count) {
            let field: &MessageField=&message.fields[index];if network_minor_active(field.first_minor,field.last_minor,minor) {
                let measured: SchemaMeasure=network_field_measure(field,wire);if measured.status!=0 { return measured; }minimum=minimum+measured.minimum;maximum=maximum+measured.maximum;
            }
        }
        return SchemaMeasure(minimum,maximum,0);
    }
    fn network_spec_failure(status: I32, component: U32, message: U32, field: U32) -> NetworkSpecCheck guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut result: NetworkSpecCheck=zero<NetworkSpecCheck>();result.status=status;result.component=component;result.message=message;result.field=field;return result;
    }
    fn network_has_message(spec: &NetworkSpec, minor: U32) -> Bool guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if spec.message_count>16 { return false; }
        for index in 0..cast<USize>(spec.message_count) { let message: &MessageSchema=&spec.messages[index];if network_minor_active(message.first_minor,message.last_minor,minor) { return true; } }return false;
    }
    fn network_spec_check(spec: &NetworkSpec) -> NetworkSpecCheck guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if spec.protocol==0 || spec.version.major==0 || spec.version.major>65535 || spec.version.minor>65535 || spec.version.minimum_minor>spec.version.minor { return network_spec_failure(4,1,0,0); }
        if network_wire_check(spec.wire)!=0 { return network_spec_failure(4,2,0,0); }
        if !transport_requirements_valid(spec.transport) { return network_spec_failure(4,3,0,0); }
        if !transport_bounds_valid(spec.resources.transport) || spec.resources.maximum_session_messages==0 || spec.resources.maximum_schema_nodes==0 || spec.resources.maximum_schema_nodes>529 { return network_spec_failure(4,3,0,0); }
        if spec.resources.maximum_unknown_fields>4096 || spec.resources.maximum_unknown_bytes>spec.resources.transport.max_message { return network_spec_failure(7,3,0,0); }
        if spec.wire.unknown_fields==0 {
            if spec.resources.maximum_unknown_fields!=0 || spec.resources.maximum_unknown_bytes!=0 { return network_spec_failure(4,3,0,0); }
        }else { if spec.resources.maximum_unknown_fields==0 || spec.resources.maximum_unknown_bytes<8 { return network_spec_failure(4,3,0,0); } }
        let session: SessionContract=spec.session;
        if session.handshake_timeout_ms==0 || session.idle_timeout_ms<session.handshake_timeout_ms || session.maximum_duration_ms<session.idle_timeout_ms || session.maximum_duration_ms>2147483647 || session.maximum_in_flight==0 || session.maximum_in_flight>spec.resources.transport.queued_messages { return network_spec_failure(4,4,0,0); }
        if spec.security.authentication>2 || spec.security.replay_scope>2 || (session.authentication_before_data && spec.security.authentication==0) { return network_spec_failure(4,5,0,0); }
        if (spec.proofs.requested&U32(4294965248))!=0 || (spec.proofs.assurance!=1 && spec.proofs.assurance!=2) { return network_spec_failure(4,8,0,0); }
        if spec.message_count==0 || spec.message_count>16 { return network_spec_failure(4,6,0,0); }
        let mut result: NetworkSpecCheck=zero<NetworkSpecCheck>();result.required_properties=spec.transport.required|16;result.required_proofs=spec.proofs.requested|431;result.schema_nodes=1;let mut minimum: U64=18446744073709551615;let mut maximum: U64=0;let mut previous: U32=0;
        if spec.security.authentication!=0 || spec.security.confidentiality || spec.security.integrity || spec.security.replay_scope!=0 { result.required_properties=result.required_properties|4; }
        if spec.security.authentication==2 { result.unsupported_security=result.unsupported_security|1; }if spec.security.replay_scope==2 { result.unsupported_security=result.unsupported_security|2; }
        if spec.wire.unknown_fields==1 { result.required_proofs=result.required_proofs|16; }
        if session.authentication_before_data { result.required_proofs=result.required_proofs|64; }
        for index in 0..cast<USize>(spec.message_count) {
            let message: &MessageSchema=&spec.messages[index];result.schema_nodes=result.schema_nodes+1;
            if message.id<=previous || message.phase==0 || message.phase>3 || message.senders==0 || message.senders>3 || message.first_minor>message.last_minor || message.first_minor>spec.version.minor || message.last_minor<spec.version.minimum_minor || message.field_count>32 { return network_spec_failure(4,6,message.id,0); }previous=message.id;
            let mut prior: U32=0;let mut first: U32=message.first_minor;if first<spec.version.minimum_minor { first=spec.version.minimum_minor; }
            for field_index in 0..cast<USize>(message.field_count) {
                let field: &MessageField=&message.fields[field_index];result.schema_nodes=result.schema_nodes+1;
                if field.id<=prior || network_field_check(field)!=0 || field.first_minor<message.first_minor || field.last_minor>message.last_minor || field.first_minor>spec.version.minor || field.last_minor<spec.version.minimum_minor { return network_spec_failure(4,7,message.id,field.id); }prior=field.id;
                if (field.flags&1)!=0 { result.required_properties=result.required_properties|4; }
            }
            let base: SchemaMeasure=network_message_measure(message,spec.wire,first);if base.status!=0 { return network_spec_failure(base.status,7,message.id,0); }
            if base.minimum<minimum { minimum=base.minimum; }if base.maximum>maximum { maximum=base.maximum; }
            for field_index in 0..cast<USize>(message.field_count) {
                let field: &MessageField=&message.fields[field_index];let mut point: U32=field.first_minor;
                for boundary in 0..2 {
                    if boundary==1 { if field.last_minor==U32(4294967295) { continue; }point=field.last_minor+1; }
                    if point>=first && point<=spec.version.minor && network_minor_active(message.first_minor,message.last_minor,point) {
                        let measured: SchemaMeasure=network_message_measure(message,spec.wire,point);if measured.minimum<minimum { minimum=measured.minimum; }if measured.maximum>maximum { maximum=measured.maximum; }
                    }
                }
            }
            // Each possible start of a gap must still have some active message.
            if message.last_minor<spec.version.minor && message.last_minor>=spec.version.minimum_minor { if !network_has_message(spec,message.last_minor+1) { return network_spec_failure(4,1,message.id,0); } }
        }
        if !network_has_message(spec,spec.version.minimum_minor) || !network_has_message(spec,spec.version.minor) { return network_spec_failure(4,1,0,0); }
        if result.schema_nodes>spec.resources.maximum_schema_nodes { return network_spec_failure(7,3,0,0); }
        maximum=maximum+cast<U64>(spec.resources.maximum_unknown_bytes);
        if maximum>cast<U64>(spec.resources.transport.max_message) { return network_spec_failure(7,3,0,0); }
        if (result.required_properties&spec.transport.forbidden)!=0 { return network_spec_failure(4,5,0,0); }
        if (result.required_properties&8)!=0 && spec.resources.transport.channels<2 { return network_spec_failure(7,3,0,0); }
        if (result.required_properties&4)!=0 { result.required_proofs=result.required_proofs|1024; }
        result.minimum=cast<USize>(minimum);result.maximum=cast<USize>(maximum);result.pending_proofs=result.required_proofs;return result;
    }
    fn network_spec_select(spec: &NetworkSpec, candidates: Array<TransportCandidate,6>) -> NetworkAdmission guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let checked: NetworkSpecCheck=network_spec_check(spec);let mut result: NetworkAdmission=zero<NetworkAdmission>();result.status=checked.status;result.choice.status=checked.status;
        if checked.status!=0 { return result; }
        result.requirements=TransportRequirements(checked.required_properties,spec.transport.forbidden,spec.transport.preferred);result.bounds=spec.resources.transport;result.bounds.max_message=cast<U32>(checked.maximum);result.session=spec.session;result.maximum_session_messages=spec.resources.maximum_session_messages;result.pending_proofs=checked.pending_proofs;result.unsupported_security=checked.unsupported_security;
        if result.unsupported_security!=0 || spec.proofs.assurance==2 { result.status=6;result.choice.status=6;return result; }
        result.choice=transport_select(result.requirements,result.bounds,candidates);result.status=result.choice.status;return result;
    }
    fn network_obligation_name(bit: U32) -> Utf8 guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if bit==1 { return "codec_roundtrip"; }if bit==2 { return "wire_bounds"; }if bit==4 { return "machine_overflow"; }if bit==8 { return "deterministic_encoding"; }if bit==16 { return "unknown_field_preservation"; }
        if bit==32 { return "session_transitions"; }if bit==64 { return "authentication_before_data"; }if bit==128 { return "resource_cleanup"; }if bit==256 { return "termination"; }if bit==512 { return "application_invariants"; }if bit==1024 { return "security_assumptions"; }return "unknown_obligation";
    }
}
