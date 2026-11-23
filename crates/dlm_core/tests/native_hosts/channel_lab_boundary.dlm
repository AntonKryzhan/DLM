module channel_lab_boundary
import std.dln_experiment
native {
    export "channel_lab_summary_abi" fn summary(value: ChannelLabSummary) -> ChannelLabSummary guarantees(NoAlloc,NoBlocking,NoIO) { return value; }
    export "channel_lab_event_abi" fn event(value: ChannelLabEvent) -> ChannelLabEvent guarantees(NoAlloc,NoBlocking,NoIO) { return value; }
    export "channel_lab_transfer_abi" fn transfer(value: ChannelLabTransfer) -> ChannelLabTransfer guarantees(NoAlloc,NoBlocking,NoIO) { return value; }
    export "channel_lab_dlm" unsafe fn verify(config: Ptr<U8>, count: USize) -> U64 guarantees(NoIO) {
        let settings: ForeignBuffer<U8>=foreign(config,count);let mut lab: ChannelLab=channel_lab(slice(&settings,0,count));
        if channel_lab_error(&lab)!=0 { channel_lab_close(move lab);return 0; }
        let mut payload: Buffer<U8>=buffer<U8>(3);payload[0]=65;payload[1]=0;payload[2]=255;
        let run: ChannelLabIo=channel_lab_simulate(&mut lab,slice(&payload,0,3));if run.status!=0 || run.count!=1 { channel_lab_close(move lab);return 1; }
        let summary: ChannelLabSummary=channel_lab_summary(&lab);if summary.delivery!=1 || summary.payload_bytes!=3 || summary.decoded_bytes!=3 { channel_lab_close(move lab);return 2; }
        let mut short: Buffer<U8>=buffer<U8>(1);short[0]=165;let a: ChannelLabTransfer=channel_lab_read(&lab,2,&mut short);
        if a.status!=10 || a.required!=3 || short[0]!=165 { channel_lab_close(move lab);return 3; }
        let mut output: Buffer<U8>=buffer<U8>(4);output[3]=165;let b: ChannelLabTransfer=channel_lab_read(&lab,2,&mut output);
        if b.status!=0 || b.copied!=3 || output[0]!=65 || output[1]!=0 || output[2]!=255 || output[3]!=165 { channel_lab_close(move lab);return 4; }
        let token: ChannelLabCancel=channel_lab_cancellation(&lab);let stopped: ChannelLabIo=channel_lab_cancel(&token);let cancelled: ChannelLabIo=channel_lab_simulate(&mut lab,slice(&payload,0,3));
        channel_lab_cancel_close(move token);channel_lab_close(move lab);if stopped.status!=0 || cancelled.status!=9 { return 5; }return 63;
    }
}
