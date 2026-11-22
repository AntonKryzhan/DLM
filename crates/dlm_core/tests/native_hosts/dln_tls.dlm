module fixtures.dln.tls
import std.net_socket

native {
    record SocketOwners repr(C) { left: UdpSocket; right: UdpSocket; }
    export fn typed_resource_failure() -> Unit {
        let owners: SocketOwners=SocketOwners(udp_bind(socket_address_v4(127,0,0,1,0),false),udp_bind(socket_address_v4(127,0,0,1,0),false));
        let first: NetworkError=udp_error(&owners.left);let second: NetworkError=udp_error(&owners.right);
        assert(network_ok(&first) && network_ok(&second));
        udp_close(move owners.left);
        // The export error boundary must finalize the still-owned right field.
        assert(false);
    }
    export unsafe fn typed_tls_echo(url: Ptr<U8>, url_count: USize, ca: Ptr<U8>, ca_count: USize) -> NetworkTransfer {
        let uri: ForeignBuffer<U8>=foreign(url,url_count);let certificate: ForeignBuffer<U8>=foreign(ca,ca_count);
        let cancel: Resource<OpaqueHandle,net_cancel_dispose>=network_cancel();
        if !network_cancel_valid(&cancel) { return NetworkTransfer(0,NetworkError(7,0,5)); }
        let mut socket: TlsSocket=tls_connect(slice(&uri,0,url_count),2000,slice(&certificate,0,ca_count),utf8_bytes(""));
        let opened: NetworkError=tls_error(&socket);if !network_ok(&opened) { return NetworkTransfer(0,opened); }
        let mut bytes: Buffer<U8>=buffer<U8>(3);bytes[0]=0;bytes[1]=255;bytes[2]=128;
        let sent: NetworkTransfer=tls_write_all(&mut socket,slice(&bytes,0,3),1000,&cancel);if !network_ok(&sent.error) { return sent; }
        bytes[0]=1;bytes[1]=1;bytes[2]=1;
        let read: NetworkTransfer=tls_read_exact(&mut socket,&mut bytes,1000,&cancel);if !network_ok(&read.error) { return read; }
        if bytes[0] != U8(0) || bytes[1] != U8(255) || bytes[2] != U8(128) { return NetworkTransfer(read.count,NetworkError(11,0,6)); }
        tls_close(move socket);return read;
    }
    export unsafe fn typed_tls_shutdown() -> NetIo { return network_provider_shutdown(); }
}
