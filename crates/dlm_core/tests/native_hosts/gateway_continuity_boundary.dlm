module fixtures.gateway.continuity_boundary
import std.dln_continuity
native {
    export "gateway_continuity_abi" fn delivery_abi(value: GatewayDeliveryDescriptor) -> GatewayDeliveryDescriptor guarantees(NoAlloc,NoBlocking,NoIO) { return value; }
    export "gateway_continuity_delivery" unsafe fn delivery(config: Ptr<U8>, config_count: USize, expires: U64, nonce: U64) -> U64 {
        let settings: ForeignBuffer<U8>=foreign(config,config_count);let mut pool: GatewayPool=gateway_pool(slice(&settings,0,config_count));if gateway_pool_error(&pool).kind!=0 { return 0; }
        let paths: NetworkTransfer=gateway_pool_refresh(&mut pool);if paths.error.kind!=0 { return 1; }
        let mut payload: Buffer<U8>=buffer<U8>(3);payload[0]=65;payload[1]=0;payload[2]=255;let mut id: Buffer<U8>=buffer<U8>(16);for i in 0..16 { id[i]=cast<U8>(i+71+cast<USize>(nonce)); }
        let descriptor: GatewayDescriptorResult=gateway_delivery(slice(&id,0,16),41+nonce,expires,slice(&payload,0,3));if descriptor.status!=0 { return 2; }
        let result: GatewayDeliveryReceipt=gateway_pool_upload(&mut pool,descriptor.descriptor,slice(&payload,0,3));if result.status!=0 || result.state!=4 || result.committed!=3 { return 3; }
        let mut small: Buffer<U8>=buffer<U8>(1);small[0]=165;let short: GatewayTransfer=gateway_pool_download(&mut pool,descriptor.descriptor,&mut small);if short.error.kind!=10 || short.required!=3 || short.copied!=0 || small[0]!=165 { return 4; }
        let mut output: Buffer<U8>=buffer<U8>(4);output[3]=165;let received: GatewayTransfer=gateway_pool_download(&mut pool,descriptor.descriptor,&mut output);if received.error.kind!=0 || received.copied!=3 || output[0]!=65 || output[1]!=0 || output[2]!=255 || output[3]!=165 { return 5; }
        let done: GatewayDeliveryReceipt=gateway_pool_consume(&mut pool,descriptor.descriptor);if done.status!=0 || done.state!=5 { return 6; }
        let mut metrics: Buffer<GatewayPathMetrics>=buffer<GatewayPathMetrics>(6);let measured: NetworkTransfer=gateway_pool_metrics(&pool,&mut metrics);if measured.error.kind!=0 || measured.count==0 { return 7; }let stats: GatewayPoolStatistics=gateway_pool_statistics(&pool);if stats.ready_paths==0 || stats.probes==0 { return 8; }
        gateway_pool_close(move pool);return 127;
    }
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
