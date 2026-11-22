module std.net_spec_compat
import std.net_spec

native {
    record NetworkNegotiation repr(C) { minor: U32; requirements: TransportRequirements; bounds: TransportBounds; choice: TransportChoice; session: SessionContract; maximum_session_messages: U64; pending_proofs: U32; unsupported_security: U32; status: I32; component: U32; message: U32; field: U32; }
    fn network_find_message(spec: &NetworkSpec, id: U32, minor: U32) -> USize guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if spec.message_count>16 { return 16; }for index in 0..cast<USize>(spec.message_count) { let message: &MessageSchema=&spec.messages[index];if message.id==id && network_minor_active(message.first_minor,message.last_minor,minor) { return index; } }return cast<USize>(spec.message_count);
    }
    fn network_find_field(message: &MessageSchema, id: U32, minor: U32) -> USize guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if message.field_count>32 { return 32; }for index in 0..cast<USize>(message.field_count) { let field: &MessageField=&message.fields[index];if field.id==id && network_minor_active(field.first_minor,field.last_minor,minor) { return index; } }return cast<USize>(message.field_count);
    }
    // Used after both complete descriptors pass network_spec_check. Sender
    // domains must fit the receiver, including opaque-extension budgets.
    fn network_spec_direction(sender: &NetworkSpec, receiver: &NetworkSpec, minor: U32, role: U32) -> NetworkSpecCheck guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if sender.message_count>16 || receiver.message_count>16 || (role!=1 && role!=2) { return network_spec_failure(4,6,0,0); }
        let mut result: NetworkSpecCheck=zero<NetworkSpecCheck>();
        for index in 0..cast<USize>(sender.message_count) {
            let source: &MessageSchema=&sender.messages[index];if !network_minor_active(source.first_minor,source.last_minor,minor) || (source.senders&role)==0 { continue; }
            let found: USize=network_find_message(receiver,source.id,minor);if found==cast<USize>(receiver.message_count) { return network_spec_failure(11,6,source.id,0); }
            let target: &MessageSchema=&receiver.messages[found];if source.field_count>32 || target.field_count>32 || source.phase!=target.phase || (target.senders&role)==0 { return network_spec_failure(11,6,source.id,0); }
            let measured: SchemaMeasure=network_message_measure(source,sender.wire,minor);if measured.status!=0 { return network_spec_failure(measured.status,7,source.id,0); }
            let maximum: U64=measured.maximum+cast<U64>(sender.resources.maximum_unknown_bytes);if maximum>cast<U64>(receiver.resources.transport.max_message) { return network_spec_failure(7,3,source.id,0); }
            if maximum>cast<U64>(result.maximum) { result.maximum=cast<USize>(maximum); }
            let mut unknown_fields: U64=cast<U64>(sender.resources.maximum_unknown_fields);let mut unknown_bytes: U64=cast<U64>(sender.resources.maximum_unknown_bytes);
            for field_index in 0..cast<USize>(source.field_count) {
                let field: &MessageField=&source.fields[field_index];if !network_minor_active(field.first_minor,field.last_minor,minor) { continue; }
                let match_index: USize=network_find_field(target,field.id,minor);
                if match_index==cast<USize>(target.field_count) {
                    if sender.wire.kind!=2 || receiver.wire.unknown_fields!=1 { return network_spec_failure(11,7,source.id,field.id); }
                    let size: SchemaMeasure=network_field_measure(field,sender.wire);unknown_fields=unknown_fields+cast<U64>(field.maximum_count);unknown_bytes=unknown_bytes+size.maximum;
                }else {
                    let accepted: &MessageField=&target.fields[match_index];
                    if field.kind!=accepted.kind || field.minimum_count<accepted.minimum_count || field.maximum_count>accepted.maximum_count || field.maximum_length>accepted.maximum_length { return network_spec_failure(11,7,source.id,field.id); }
                    if sender.wire.kind==1 { let a: Bool=field.minimum_count==1 && field.maximum_count==1;let b: Bool=accepted.minimum_count==1 && accepted.maximum_count==1;if a!=b { return network_spec_failure(11,7,source.id,field.id); } }
                }
            }
            for field_index in 0..cast<USize>(target.field_count) {
                let field: &MessageField=&target.fields[field_index];if !network_minor_active(field.first_minor,field.last_minor,minor) { continue; }
                if network_find_field(source,field.id,minor)==cast<USize>(source.field_count) {
                    if sender.wire.kind==1 || field.minimum_count!=0 || sender.resources.maximum_unknown_fields!=0 { return network_spec_failure(11,7,source.id,field.id); }
                }
            }
            if unknown_fields>cast<U64>(receiver.resources.maximum_unknown_fields) || unknown_bytes>cast<U64>(receiver.resources.maximum_unknown_bytes) { return network_spec_failure(7,3,source.id,0); }
        }
        return result;
    }
    fn network_negotiation_failure(status: I32, component: U32, message: U32, field: U32) -> NetworkNegotiation guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut result: NetworkNegotiation=zero<NetworkNegotiation>();result.status=status;result.choice.status=status;result.component=component;result.message=message;result.field=field;return result;
    }
    fn network_session_intersection(left: SessionContract, right: SessionContract) -> SessionContract guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut result: SessionContract=left;if right.handshake_timeout_ms<result.handshake_timeout_ms { result.handshake_timeout_ms=right.handshake_timeout_ms; }
        if right.idle_timeout_ms<result.idle_timeout_ms { result.idle_timeout_ms=right.idle_timeout_ms; }if right.maximum_duration_ms<result.maximum_duration_ms { result.maximum_duration_ms=right.maximum_duration_ms; }
        if right.maximum_in_flight<result.maximum_in_flight { result.maximum_in_flight=right.maximum_in_flight; }result.authentication_before_data=left.authentication_before_data || right.authentication_before_data;return result;
    }
    fn network_spec_negotiate(local: &NetworkSpec, remote: &NetworkSpec, initiator: Bool, candidates: Array<TransportCandidate,6>) -> NetworkNegotiation guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let a: NetworkSpecCheck=network_spec_check(local);let b: NetworkSpecCheck=network_spec_check(remote);
        if a.status!=0 { return network_negotiation_failure(a.status,a.component,a.message,a.field); }if b.status!=0 { return network_negotiation_failure(b.status,b.component,b.message,b.field); }
        if local.protocol!=remote.protocol || local.version.major!=remote.version.major { return network_negotiation_failure(11,1,0,0); }
        let mut minor: U32=local.version.minor;if remote.version.minor<minor { minor=remote.version.minor; }
        if minor<local.version.minimum_minor || minor<remote.version.minimum_minor { return network_negotiation_failure(11,1,0,0); }
        if local.wire.kind!=remote.wire.kind || local.wire.endian!=remote.wire.endian || local.wire.envelope_version!=remote.wire.envelope_version { return network_negotiation_failure(11,2,0,0); }
        let mut role: U32=1;if !initiator { role=2; }
        let outgoing: NetworkSpecCheck=network_spec_direction(local,remote,minor,role);if outgoing.status!=0 { return network_negotiation_failure(outgoing.status,outgoing.component,outgoing.message,outgoing.field); }
        let incoming: NetworkSpecCheck=network_spec_direction(remote,local,minor,U32(3)^role);if incoming.status!=0 { return network_negotiation_failure(incoming.status,incoming.component,incoming.message,incoming.field); }
        let mut result: NetworkNegotiation=zero<NetworkNegotiation>();result.minor=minor;
        let required: U32=a.required_properties|b.required_properties;let forbidden: U32=local.transport.forbidden|remote.transport.forbidden;
        if (required&forbidden)!=0 || (required&66)==66 { return network_negotiation_failure(11,3,0,0); }
        result.requirements=TransportRequirements(required,forbidden,(local.transport.preferred|remote.transport.preferred)&(U32(511)^forbidden));
        result.bounds=local.resources.transport;
        if remote.resources.transport.memory<result.bounds.memory { result.bounds.memory=remote.resources.transport.memory; }
        if remote.resources.transport.queued_messages<result.bounds.queued_messages { result.bounds.queued_messages=remote.resources.transport.queued_messages; }
        if remote.resources.transport.channels<result.bounds.channels { result.bounds.channels=remote.resources.transport.channels; }
        result.bounds.max_message=cast<U32>(outgoing.maximum);if incoming.maximum>outgoing.maximum { result.bounds.max_message=cast<U32>(incoming.maximum); }
        if !transport_bounds_valid(result.bounds) || ((required&8)!=0 && result.bounds.channels<2) { return network_negotiation_failure(7,3,0,0); }
        result.session=network_session_intersection(local.session,remote.session);result.maximum_session_messages=local.resources.maximum_session_messages;if remote.resources.maximum_session_messages<result.maximum_session_messages { result.maximum_session_messages=remote.resources.maximum_session_messages; }
        result.pending_proofs=a.pending_proofs|b.pending_proofs;result.unsupported_security=a.unsupported_security|b.unsupported_security;
        if result.unsupported_security!=0 || local.proofs.assurance==2 || remote.proofs.assurance==2 { result.status=6;result.choice.status=6;return result; }
        result.choice=transport_select(result.requirements,result.bounds,candidates);result.status=result.choice.status;return result;
    }
}
