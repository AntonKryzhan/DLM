module std.net_codec
import std.net_bytes

native {
    // Constant-size incremental state. Payload storage stays with the caller.
    record Frame32Decoder repr(C, version=1) {
        maximum: U32;
        header: U32;
        prefix_count: U32;
        remaining: U32;
        fault: I32;
    }
    record Frame32Step repr(C, version=1) {
        consumed: USize;
        payload_offset: USize;
        payload_count: USize;
        remaining: U32;
        complete: Bool;
        status: I32;
    }
    fn frame32_decoder(maximum: U32) -> Frame32Decoder guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        return Frame32Decoder(maximum,0,0,0,0);
    }
    fn frame32_reset(state: &mut Frame32Decoder) -> Unit guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        state.header=0;state.prefix_count=0;state.remaining=0;state.fault=0;
    }
    fn frame32_valid(state: &Frame32Decoder) -> Bool guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if state.prefix_count > U32(4) { return false; }
        if state.fault == I32(7) { return state.prefix_count == U32(4) && state.header > state.maximum && state.remaining == U32(0); }
        if state.fault != I32(0) { return false; }
        if state.prefix_count < U32(4) {
            return state.remaining == U32(0) && (state.header >> (state.prefix_count*U32(8))) == U32(0);
        }
        return state.header <= state.maximum && state.remaining > U32(0) && state.remaining <= state.header;
    }
    // Consumes only one frame, or one available portion of it. The caller
    // advances by consumed and may immediately feed a following frame.
    // An oversized header is sticky until an explicit reset/reconnection.
    fn frame32_feed(state: &mut Frame32Decoder, input: Slice<U8>, offset: USize) -> Frame32Step guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if offset > len(input) { return Frame32Step(0,offset,0,0,false,4); }
        if !frame32_valid(state) { return Frame32Step(0,offset,0,0,false,4); }
        if state.fault != I32(0) { return Frame32Step(0,offset,0,state.remaining,false,state.fault); }
        let mut position: USize=offset;
        for index in 0..4 {
            if state.prefix_count == U32(4) || position == len(input) { break; }
            state.header=(state.header << U32(8)) | cast<U32>(input[position]);
            state.prefix_count=state.prefix_count+U32(1);position=position+1;
            if state.prefix_count == U32(4) {
                if state.header > state.maximum {
                    state.fault=7;return Frame32Step(position-offset,position,0,0,false,7);
                }
                state.remaining=state.header;
            }
        }
        if state.prefix_count < U32(4) { return Frame32Step(position-offset,position,0,0,false,1); }
        let available: USize=len(input)-position;let mut count: USize=cast<USize>(state.remaining);
        if count > available { count=available; }
        state.remaining=state.remaining-cast<U32>(count);
        let left: U32=state.remaining;let complete: Bool=left == U32(0);
        if complete { frame32_reset(state); }
        let mut status: I32=0;
        if count == 0 && !complete { status=1; }
        return Frame32Step(position-offset+count,position,count,left,complete,status);
    }
    // Checks that EOF falls exactly between frames. An empty input chunk
    // alone is not EOF; call this function only when the transport ends.
    fn frame32_finish(state: &Frame32Decoder) -> I32 guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if !frame32_valid(state) { return 4; }
        if state.fault != I32(0) { return state.fault; }
        if state.prefix_count == U32(0) && state.remaining == U32(0) { return 0; }
        return 1;
    }
    fn frame32_view(input: &Buffer<U8>, base_offset: USize, step: Frame32Step) -> Slice<U8> borrows(input) guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        // Keep the slice bounds check even for a caller-constructed step.
        assert(base_offset <= len(input));assert(step.payload_offset <= len(input)-base_offset);
        return slice(input,base_offset+step.payload_offset,step.payload_count);
    }
    fn frame32_write_header(output: &mut Buffer<U8>, offset: USize, count: U32, maximum: U32) -> I32 guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if count > maximum { return 7; }
        return protocol_write_be(output,offset,4,cast<U64>(count));
    }
}
