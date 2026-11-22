module network_codec_le_schema
import codec_schema_common
native {
    fn network_schema() -> NetworkSpec guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut spec: NetworkSpec=codec_example_schema();spec.wire=WireFormat(1,2,0,0);spec.resources.maximum_unknown_fields=0;spec.resources.maximum_unknown_bytes=0;return spec;
    }
}
