module codec_schema_common
import std.net_spec
native {
    fn codec_example_schema() -> NetworkSpec guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut spec: NetworkSpec=network_spec(0x434f4431,1,1,0);spec.transport=TransportRequirements(19,0,0);spec.wire.unknown_fields=1;spec.resources.maximum_unknown_fields=4;spec.resources.maximum_unknown_bytes=256;
        let mut data: MessageSchema=message_schema(1,2,3);
        let a: I32=message_add_field(&mut data,network_field(1,1,1,1,0));let b: I32=message_add_field(&mut data,network_field(2,2,0,1,0));let c: I32=message_add_field(&mut data,network_field(3,3,0,3,0));
        let d: I32=message_add_field(&mut data,network_field(4,4,1,1,0));let e: I32=message_add_field(&mut data,network_field(5,5,1,1,0));let f: I32=message_add_field(&mut data,network_field(6,6,0,2,32));let g: I32=message_add_field(&mut data,network_field(7,7,0,1,64));
        let h: I32=message_add_field(&mut data,network_field(8,8,0,4,0));let i: I32=message_add_field(&mut data,network_field(9,9,1,2,0));
        let mut added: MessageField=network_field(10,2,0,1,0);added.first_minor=1;let j: I32=message_add_field(&mut data,added);
        let mut retired: MessageField=network_field(11,6,0,1,8);retired.last_minor=0;let k: I32=message_add_field(&mut data,retired);
        assert(a==0 && b==0 && c==0 && d==0 && e==0 && f==0 && g==0 && h==0 && i==0 && j==0 && k==0);
        let m: I32=network_add_message(&mut spec,data);let n: I32=network_add_message(&mut spec,message_schema(2,3,3));assert(m==0 && n==0);return spec;
    }
}
