module gateway_proxy_boundary
import std.dln_proxy
native {
    export "gateway_proxy_abi" fn statistics(value: GatewayProxyStatistics) -> GatewayProxyStatistics { return value; }
    export "gateway_proxy_assignment_abi" fn assignment(value: GatewayIpAssignment) -> GatewayIpAssignment guarantees(NoAlloc,NoBlocking,NoIO) { return value; }
    export "gateway_proxy_route_abi" fn route(value: GatewayIpRoute) -> GatewayIpRoute guarantees(NoAlloc,NoBlocking,NoIO) { return value; }
    export "gateway_proxy_dlm" unsafe fn exchange(config: Ptr<U8>, count: USize, target: Ptr<U8>, length: USize, mode: U32) -> U64 {
        let settings: ForeignBuffer<U8>=foreign(config,count);let destination: ForeignBuffer<U8>=foreign(target,length);
        let mut proxy: GatewayProxy=gateway_proxy(slice(&settings,0,count),mode,slice(&destination,0,length));
        if gateway_proxy_error(&proxy).kind!=0 { gateway_proxy_close(move proxy);return 0; }
        let mut input: Buffer<U8>=buffer<U8>(3);input[0]=65;input[1]=0;input[2]=255;
        let sent: NetworkTransfer=gateway_proxy_write(&mut proxy,slice(&input,0,3));
        if sent.error.kind!=0 || sent.count!=3 { gateway_proxy_close(move proxy);return 1; }
        let mut short: Buffer<U8>=buffer<U8>(1);short[0]=165;let a: GatewayTransfer=gateway_proxy_read(&mut proxy,65535,&mut short);
        if a.error.kind!=10 || a.required!=3 || short[0]!=165 { gateway_proxy_close(move proxy);return 2; }
        let mut output: Buffer<U8>=buffer<U8>(4);output[3]=165;let b: GatewayTransfer=gateway_proxy_take(&mut proxy,&mut output);
        if b.error.kind!=0 || b.copied!=3 || output[0]!=65 || output[1]!=0 || output[2]!=255 || output[3]!=165 { gateway_proxy_close(move proxy);return 3; }
        let token: GatewayProxyCancel=gateway_proxy_cancellation(&proxy);let error: NetworkError=gateway_proxy_cancel(&token);
        gateway_proxy_cancel_close(move token);gateway_proxy_close(move proxy);if error.kind!=0 { return 4; }return 31;
    }
}
