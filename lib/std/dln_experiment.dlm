module std.dln_experiment

native {
    trust "dlm.channel_lab.v1";
    record ChannelLabOpened repr(C, version=1) { handle: OpaqueHandle; status: I32; reserved: I32; }
    record ChannelLabIo repr(C, version=1) { count: U64; status: I32; reserved: I32; }
    record ChannelLabTransfer repr(C, version=1) { required: U64; copied: U64; status: I32; delivery: U32; }
    record ChannelLabSummary repr(C, version=1) { delivery: U32; mode: U32; symbols: U64; events: U64; payload_bytes: U64; decoded_bytes: U64; corrected_words: U64; duration_us: U64; }
    record ChannelLabEvent repr(C, version=1) { time_us: U64; size: U32; kind: U32; }
    record ChannelLab repr(C) { inner: Resource<ChannelLabOpened,channel_lab_dispose>; }
    record ChannelLabCancel repr(C) { inner: Resource<ChannelLabOpened,channel_lab_cancel_dispose>; }
    extern "C" fn dlm_channel_lab_abi() -> U32 from "dlm.channel_lab.v1" guarantees(NoUnwind,NoAlloc,NoBlocking,NoIO);
    extern "C" fn dlm_channel_lab_open(input: Ptr<U8>, count: USize) -> ChannelLabOpened from "dlm.channel_lab.v1" guarantees(NoUnwind,NoBlocking,NoIO);
    extern "C" fn dlm_channel_lab_simulate(handle: OpaqueHandle, input: Ptr<U8>, count: USize) -> ChannelLabIo from "dlm.channel_lab.v1" guarantees(NoUnwind,NoBlocking,NoIO);
    extern "C" fn dlm_channel_lab_loopback(handle: OpaqueHandle, input: Ptr<U8>, count: USize) -> ChannelLabIo from "dlm.channel_lab.v1" guarantees(NoUnwind);
    extern "C" fn dlm_channel_lab_decode(input: Ptr<U8>, count: USize) -> ChannelLabOpened from "dlm.channel_lab.v1" guarantees(NoUnwind,NoBlocking,NoIO);
    extern "C" fn dlm_channel_lab_read(handle: OpaqueHandle, item: U32, output: Ptr<U8>, capacity: USize) -> ChannelLabTransfer from "dlm.channel_lab.v1" guarantees(NoUnwind,NoAlloc,NoBlocking,NoIO);
    extern "C" fn dlm_channel_lab_summary(handle: OpaqueHandle) -> ChannelLabSummary from "dlm.channel_lab.v1" guarantees(NoUnwind,NoAlloc,NoBlocking,NoIO);
    extern "C" fn dlm_channel_lab_event(handle: OpaqueHandle, index: USize, output: Ptr<ChannelLabEvent>) -> ChannelLabIo from "dlm.channel_lab.v1" guarantees(NoUnwind,NoAlloc,NoBlocking,NoIO);
    extern "C" fn dlm_channel_lab_close(handle: OpaqueHandle) -> Unit from "dlm.channel_lab.v1" guarantees(NoUnwind,NoAlloc,NoBlocking,NoIO);
    extern "C" fn dlm_channel_lab_cancellation(handle: OpaqueHandle) -> ChannelLabOpened from "dlm.channel_lab.v1" guarantees(NoUnwind,NoBlocking,NoIO);
    extern "C" fn dlm_channel_lab_cancel(handle: OpaqueHandle) -> ChannelLabIo from "dlm.channel_lab.v1" guarantees(NoUnwind,NoAlloc,NoBlocking,NoIO);
    extern "C" fn dlm_channel_lab_cancel_close(handle: OpaqueHandle) -> Unit from "dlm.channel_lab.v1" guarantees(NoUnwind,NoAlloc,NoBlocking,NoIO);
    unsafe fn channel_lab_input(bytes: Slice<U8>) -> Ptr<U8> guarantees(NoAlloc,NoBlocking,NoIO) { if len(bytes)==0 { return null<U8>(); }return as_raw(borrowed_ptr(&bytes[0])); }
    unsafe fn channel_lab_output(bytes: &mut Buffer<U8>) -> Ptr<U8> guarantees(NoAlloc,NoBlocking,NoIO) { if len(bytes)==0 { return null<U8>(); }return as_raw(borrowed_ptr(&mut bytes[0])); }
    unsafe fn channel_lab_dispose(state: &mut ChannelLabOpened) -> Unit guarantees(NoUnwind,NoAlloc,NoBlocking,NoIO) { dlm_channel_lab_close(state.handle); }
    unsafe fn channel_lab_cancel_dispose(state: &mut ChannelLabOpened) -> Unit guarantees(NoUnwind,NoAlloc,NoBlocking,NoIO) { dlm_channel_lab_cancel_close(state.handle); }
    fn channel_lab(config: Slice<U8>) -> ChannelLab guarantees(NoIO) { unsafe { return ChannelLab(resource(dlm_channel_lab_open(channel_lab_input(config),len(config)))); } }
    fn channel_lab_decode(capture: Slice<U8>) -> ChannelLab guarantees(NoIO) { unsafe { return ChannelLab(resource(dlm_channel_lab_decode(channel_lab_input(capture),len(capture)))); } }
    fn channel_lab_error(lab: &ChannelLab) -> I32 guarantees(NoAlloc,NoBlocking,NoIO) { let state: &ChannelLabOpened=resource_get(&lab.inner);return state.status; }
    fn channel_lab_close(lab: ChannelLab) -> Unit guarantees(NoAlloc,NoIO) { resource_close(move lab.inner); }
    fn channel_lab_simulate(lab: &mut ChannelLab, payload: Slice<U8>) -> ChannelLabIo guarantees(NoBlocking,NoIO) { unsafe { let state: &ChannelLabOpened=resource_get(&lab.inner);return dlm_channel_lab_simulate(state.handle,channel_lab_input(payload),len(payload)); } }
    fn channel_lab_loopback(lab: &mut ChannelLab, payload: Slice<U8>) -> ChannelLabIo { unsafe { let state: &ChannelLabOpened=resource_get(&lab.inner);return dlm_channel_lab_loopback(state.handle,channel_lab_input(payload),len(payload)); } }
    fn channel_lab_read(lab: &ChannelLab, item: U32, output: &mut Buffer<U8>) -> ChannelLabTransfer guarantees(NoAlloc,NoBlocking,NoIO) { unsafe { let state: &ChannelLabOpened=resource_get(&lab.inner);return dlm_channel_lab_read(state.handle,item,channel_lab_output(output),len(output)); } }
    fn channel_lab_summary(lab: &ChannelLab) -> ChannelLabSummary guarantees(NoAlloc,NoBlocking,NoIO) { unsafe { let state: &ChannelLabOpened=resource_get(&lab.inner);return dlm_channel_lab_summary(state.handle); } }
    fn channel_lab_event(lab: &ChannelLab, index: USize, output: &mut Buffer<ChannelLabEvent>) -> ChannelLabIo guarantees(NoAlloc,NoBlocking,NoIO) { unsafe { let state: &ChannelLabOpened=resource_get(&lab.inner);if len(output)==0 { return dlm_channel_lab_event(state.handle,index,null<ChannelLabEvent>()); }return dlm_channel_lab_event(state.handle,index,as_raw(borrowed_ptr(&mut output[0]))); } }
    fn channel_lab_cancellation(lab: &ChannelLab) -> ChannelLabCancel guarantees(NoIO) { unsafe { let state: &ChannelLabOpened=resource_get(&lab.inner);return ChannelLabCancel(resource(dlm_channel_lab_cancellation(state.handle))); } }
    fn channel_lab_cancel_error(token: &ChannelLabCancel) -> I32 guarantees(NoAlloc,NoBlocking,NoIO) { let state: &ChannelLabOpened=resource_get(&token.inner);return state.status; }
    fn channel_lab_cancel(token: &ChannelLabCancel) -> ChannelLabIo guarantees(NoAlloc,NoBlocking,NoIO) { unsafe { let state: &ChannelLabOpened=resource_get(&token.inner);return dlm_channel_lab_cancel(state.handle); } }
    fn channel_lab_cancel_close(token: ChannelLabCancel) -> Unit guarantees(NoAlloc,NoIO) { resource_close(move token.inner); }
}
