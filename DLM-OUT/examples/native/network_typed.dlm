module examples.native.network_typed
import std.net_socket
import std.net_codec

native {
    fn main() -> Unit {
        {
            let cancel: Resource<OpaqueHandle,net_cancel_dispose>=network_cancel();assert(network_cancel_valid(&cancel));
            let mut listener: TcpListener=tcp_listen(socket_address_v4(127,0,0,1,0),16,false);
            let opened: NetworkError=listener_error(&listener);assert(network_ok(&opened));
            let local: SocketAddressResult=listener_local(&listener);assert(network_ok(&local.error));
            let mut client: TcpSocket=tcp_connect_address(local.address,1000);
            let connected: NetworkError=tcp_error(&client);assert(network_ok(&connected));
            let ready: NetworkReady=listener_wait(&mut listener,1000,&cancel);assert(network_ok(&ready.error));
            let mut server: TcpSocket=tcp_accept(&mut listener);let accepted: NetworkError=tcp_error(&server);assert(network_ok(&accepted));
            let configured: NetworkError=tcp_configure(&mut client,socket_options(true,true,16384,16384));assert(network_ok(&configured));
            let options: SocketOptionsResult=tcp_options(&client);assert(network_ok(&options.error) && options.options.no_delay == U32(1));
            let peer: SocketAddressResult=tcp_peer(&client);assert(network_ok(&peer.error) && peer.address.value.port == local.address.value.port);
            let mut packet: Buffer<U8>=buffer<U8>(7);
            let header: I32=frame32_write_header(&mut packet,0,3,1024);assert(header == I32(0));packet[4]=0;packet[5]=255;packet[6]=128;
            let sent: NetworkTransfer=tcp_write_all(&mut client,slice(&packet,0,len(&packet)),1000,&cancel);assert(network_ok(&sent.error) && sent.count == USize(7));
            let mut received: Buffer<U8>=buffer<U8>(7);
            let read: NetworkTransfer=tcp_read_exact(&mut server,&mut received,1000,&cancel);assert(network_ok(&read.error));
            let mut decoder: Frame32Decoder=frame32_decoder(1024);
            let step: Frame32Step=frame32_feed(&mut decoder,slice(&received,0,read.count),0);
            assert(step.complete && step.payload_count == USize(3));
            {
                let body: Slice<U8>=frame32_view(&received,0,step);assert(body[0] == U8(0) && body[1] == U8(255));
            }
            tcp_close(move client);tcp_close(move server);listener_close(move listener);
            let mut udp: UdpSocket=udp_bind(socket_address_v4(127,0,0,1,0),false);
            let udp_opened: NetworkError=udp_error(&udp);assert(network_ok(&udp_opened));
            let udp_address: SocketAddressResult=udp_local(&udp);assert(network_ok(&udp_address.error));
            let udp_sent: NetworkTransfer=udp_send(&mut udp,udp_address.address,slice(&packet,4,3));assert(network_ok(&udp_sent.error));
            let udp_ready: NetworkReady=udp_wait(&mut udp,1,1000,&cancel);assert(network_ok(&udp_ready.error));
            let datagram: SocketDatagram=udp_receive(&mut udp,&mut received);assert(network_ok(&datagram.transfer.error) && datagram.transfer.count == USize(3));
            udp_close(move udp);
        }
        unsafe { let stopped: NetIo=network_provider_shutdown();assert(stopped.status == I32(0)); }
        print("typed TCP and UDP OK");
    }
}
