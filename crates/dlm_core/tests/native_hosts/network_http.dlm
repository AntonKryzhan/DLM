module fixtures.native.network_http
import std.native_net

native {
    export unsafe fn network_fixture_fetch(url: Ptr<U8>, url_size: USize, ca: Ptr<U8>, ca_size: USize, target: Ptr<U8>, capacity: USize, version: U32) -> NetIo {
        let url_bytes: ForeignBuffer<U8>=foreign(url,url_size);
        let ca_bytes: ForeignBuffer<U8>=foreign(ca,ca_size);
        let cancel: Resource<OpaqueHandle,net_cancel_dispose>=network_cancel();
        if !network_cancel_valid(&cancel) { return NetIo(0,7,0); }
        let mut request: Resource<NetOpened,net_dispose>=http_client_bytes(slice(&url_bytes,0,url_size),2000,65536,slice(&ca_bytes,0,ca_size),utf8_bytes(""));
        if socket_status(&request) != I32(0) { return NetIo(0,socket_status(&request),socket_system_error(&request)); }
        let option: NetIo=http_version(&mut request,version);
        if option.status != I32(0) { return option; }
        let performed: NetIo=http_perform(&mut request,&cancel);
        if performed.status != I32(0) { return performed; }
        let mut result: Buffer<U8>=buffer<U8>(capacity);
        let read: NetIo=http_read(&request,false,0,&mut result);
        if read.status != I32(0) { return read; }
        let mut destination: ForeignBuffer<U8>=foreign(target,capacity);
        for i in 0..read.count { destination[i]=result[i]; }
        return read;
    }
    export unsafe fn network_fixture_shutdown() -> NetIo { return network_provider_shutdown(); }
}
