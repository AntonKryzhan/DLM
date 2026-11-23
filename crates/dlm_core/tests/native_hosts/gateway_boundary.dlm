module tests.gateway_boundary
import std.dln_gateway
native {
    export "gateway_dlm_exchange" unsafe fn exchange(config: Ptr<U8>, config_count: USize, destination: Ptr<U8>, destination_count: USize) -> U64 {
        let configuration: ForeignBuffer<U8>=foreign(config,config_count);let target: ForeignBuffer<U8>=foreign(destination,destination_count);
        let mut client: GatewayClient=gateway_connect(slice(&configuration,0,config_count));if gateway_error(&client).kind!=0 { return 0; }
        let stream: NetworkTransfer=gateway_stream(&mut client,1,slice(&target,0,destination_count));if stream.error.kind!=0 { return 1; }
        let mut bytes: Buffer<U8>=buffer<U8>(3);bytes[0]=65;bytes[1]=0;bytes[2]=255;
        let sent: NetworkTransfer=gateway_write(&mut client,cast<U32>(stream.count),slice(&bytes,0,3));if sent.error.kind!=0 || sent.count!=3 { return 2; }
        let mut small: Buffer<U8>=buffer<U8>(1);small[0]=165;let short: GatewayTransfer=gateway_read(&mut client,cast<U32>(stream.count),3,&mut small);if short.error.kind!=10 || short.required!=3 || short.copied!=0 || small[0]!=165 { return 3; }
        let mut output: Buffer<U8>=buffer<U8>(4);output[3]=165;let received: GatewayTransfer=gateway_take(&mut client,&mut output);if received.error.kind!=0 || received.copied!=3 || received.outcome!=3 || output[0]!=65 || output[1]!=0 || output[2]!=255 || output[3]!=165 { return 4; }
        let closed: NetworkError=gateway_stream_close(&mut client,cast<U32>(stream.count),false);if closed.kind!=0 { return 5; }gateway_close(move client);return 63;
    }
}
