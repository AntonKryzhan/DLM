module examples.native.network_spec_versions
import std.net_spec_compat

native {
    fn sample(minor: U32) -> NetworkSpec guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut spec: NetworkSpec=network_spec(0x444c4e32,1,minor,0);let mut message: MessageSchema=message_schema(1,2,3);let a: I32=message_add_field(&mut message,network_field(1,3,1,1,0));assert(a==0);
        if minor!=0 { let mut added: MessageField=network_field(2,6,0,1,128);added.first_minor=1;let b: I32=message_add_field(&mut message,added);assert(b==0); }
        let m: I32=network_add_message(&mut spec,message);assert(m==0);return spec;
    }
    fn main() -> Unit {
        let older: NetworkSpec=sample(0);let mut newer: NetworkSpec=sample(1);let candidates: Array<TransportCandidate,6>=transport_candidates(63);
        let shared: NetworkNegotiation=network_spec_negotiate(&newer,&older,true,candidates);assert(shared.status==0 && shared.minor==0 && shared.bounds.max_message==24);
        newer.version.minimum_minor=1;let excluded: NetworkNegotiation=network_spec_negotiate(&newer,&older,true,candidates);assert(excluded.status==11 && excluded.component==1);newer.version.minimum_minor=0;
        newer.messages[0].fields[0].kind=4;let changed_type: NetworkNegotiation=network_spec_negotiate(&newer,&older,true,candidates);assert(changed_type.status==11 && changed_type.message==1 && changed_type.field==1);newer.messages[0].fields[0].kind=3;
        newer.messages[0].fields[1].first_minor=0;let unknown: NetworkNegotiation=network_spec_negotiate(&newer,&older,true,candidates);assert(unknown.status==11);
        let mut receiver: NetworkSpec=sample(0);receiver.wire.unknown_fields=1;receiver.resources.maximum_unknown_fields=2;receiver.resources.maximum_unknown_bytes=256;
        newer.messages[0].senders=1;receiver.messages[0].senders=1;
        let extension: NetworkNegotiation=network_spec_negotiate(&newer,&receiver,true,candidates);assert(extension.status==0 && extension.minor==0 && (extension.pending_proofs&16)!=0);
        receiver.resources.maximum_unknown_bytes=8;let limited: NetworkNegotiation=network_spec_negotiate(&newer,&receiver,true,candidates);assert(limited.status==7 && limited.component==3);
        newer=sample(1);receiver=sample(0);receiver.transport.forbidden=1;newer.transport.required=17;let contradictory: NetworkNegotiation=network_spec_negotiate(&newer,&receiver,true,candidates);assert(contradictory.status==11);
        print("NetworkSpec version windows directional fields extensions and budgets OK");
    }
}
