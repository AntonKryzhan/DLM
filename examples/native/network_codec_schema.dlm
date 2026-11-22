module network_codec_schema
import codec_schema_common
native {
    fn network_schema() -> NetworkSpec guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) { return codec_example_schema(); }
}
