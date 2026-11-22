module dln_state_schema
import std.net_session_machine
native {
    fn network_schema() -> NetworkSpec guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut spec: NetworkSpec=network_spec(0x53544131,1,1,0);spec.transport=TransportRequirements(19,0,0);spec.resources.maximum_session_messages=4;spec.session=SessionContract(1000,2000,5000,1,false);
        let mut data: MessageSchema=message_schema(1,2,1);let field: I32=message_add_field(&mut data,network_field(1,4,1,1,0));assert(field==0);let a: I32=network_add_message(&mut spec,data);let b: I32=network_add_message(&mut spec,message_schema(2,1,2));let c: I32=network_add_message(&mut spec,message_schema(3,3,3));assert(a==0 && b==0 && c==0);return spec;
    }
}
