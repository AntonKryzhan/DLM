module examples.native.network_spec
import std.net_spec_wire
import std.net_spec_compat

native {
    fn example_spec() -> NetworkSpec guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut spec: NetworkSpec=network_spec(0x444c4e31,1,0,0);let mut message: MessageSchema=message_schema(1,2,3);
        let a: I32=message_add_field(&mut message,network_field(1,4,1,1,0));let b: I32=message_add_field(&mut message,network_field(2,6,0,1,64));assert(a==0 && b==0);
        let added: I32=network_add_message(&mut spec,message);assert(added==0);spec.transport=TransportRequirements(19,0,0);return spec;
    }
    fn exercise() -> Unit guarantees(NoIO,BoundedWork) {
        let mut spec: NetworkSpec=example_spec();let check: NetworkSpecCheck=network_spec_check(&spec);assert(check.status==0 && check.minimum==28 && check.maximum==100 && check.schema_nodes==4 && check.pending_proofs==431);
        let candidates: Array<TransportCandidate,6>=transport_candidates(63);let admission: NetworkAdmission=network_spec_select(&spec,candidates);assert(admission.status==0 && admission.choice.profile==1 && admission.bounds.max_message==100 && admission.pending_proofs==431);
        let size: NetworkSpecIo=network_spec_encoded_size(&spec);assert(size.status==0 && size.count==224);
        let mut bytes: Buffer<U8>=buffer<U8>(232);for index in 0..len(&bytes) { bytes[index]=165; }
        let short: NetworkSpecIo=network_spec_encode(&spec,&mut bytes,9);assert(short.status==1 && short.count==0 && bytes[9]==165);
        let encoded: NetworkSpecIo=network_spec_encode(&spec,&mut bytes,4);assert(encoded.status==0 && encoded.count==224 && encoded.next==228 && bytes[0]==165 && bytes[228]==165);
        let decoded: NetworkSpecDecoded=network_spec_decode(slice(&bytes,4,encoded.count));assert(decoded.status==0 && network_spec_equal(&spec,&decoded.spec));
        let truncated: NetworkSpecDecoded=network_spec_decode(slice(&bytes,4,encoded.count-1));assert(truncated.status==1 && truncated.count==0 && truncated.spec.protocol==0);
        let negotiated: NetworkNegotiation=network_spec_negotiate(&spec,&decoded.spec,true,candidates);assert(negotiated.status==0 && negotiated.minor==0 && negotiated.bounds.max_message==100 && negotiated.choice.profile==1);
        spec.messages[0].fields[1].flags=1;let secret: NetworkAdmission=network_spec_select(&spec,candidates);assert(secret.status==0 && (secret.requirements.required&4)!=0 && secret.choice.profile==4 && (secret.pending_proofs&1024)!=0);
        spec.proofs.assurance=2;let proof_required: NetworkAdmission=network_spec_select(&spec,candidates);assert(proof_required.status==6 && proof_required.choice.status==6 && proof_required.pending_proofs!=0);
        spec.proofs.assurance=1;spec.security.authentication=2;let mutual: NetworkAdmission=network_spec_select(&spec,candidates);assert(mutual.status==6 && mutual.unsupported_security==1);
        spec.security.authentication=0;spec.transport.forbidden=4;let conflict: NetworkSpecCheck=network_spec_check(&spec);assert(conflict.status==4 && conflict.component==5);
    }
    fn main() -> Unit { exercise();print("NetworkSpec schemas bounds metadata negotiation and proof gates OK"); }
}
