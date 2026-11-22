module examples.native.network_transport
import std.net_transport

native {
    fn exchange(client: &mut TransportSession, server: &mut TransportSession, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> Unit {
        let mut payload: Buffer<U8>=buffer<U8>(4);payload[0]=0;payload[1]=255;payload[2]=128;payload[3]=42;
        let first: NetworkTransfer=transport_send(client,0,slice(&payload,0,4));let full: NetworkTransfer=transport_send(client,0,slice(&payload,0,4));assert(network_ok(&first.error) && network_would_block(&full.error));
        let write: NetworkTransfer=transport_poll(client,0,cancel);assert(network_ok(&write.error));let read: NetworkTransfer=transport_poll(server,1000,cancel);assert(network_ok(&read.error));
        let message: TransportMessage=transport_peek(server);assert(network_ok(&message.error) && message.channel==0 && message.count==4);
        let mut small: Buffer<U8>=buffer<U8>(2);small[0]=165;small[1]=165;let truncated: TransportDelivery=transport_receive(server,&mut small);assert(network_truncated(&truncated.error) && truncated.required==4 && truncated.copied==0 && small[0]==165 && small[1]==165);
        let retained: TransportMessage=transport_peek(server);assert(retained.id==message.id);
        let mut output: Buffer<U8>=buffer<U8>(8);let received: TransportDelivery=transport_receive(server,&mut output);assert(network_ok(&received.error) && received.copied==4 && output[0]==0 && output[1]==255 && output[2]==128 && output[3]==42);
        let reply: NetworkTransfer=transport_send(server,0,slice(&payload,0,0));assert(network_ok(&reply.error));let a: NetworkTransfer=transport_poll(server,0,cancel);let b: NetworkTransfer=transport_poll(client,1000,cancel);assert(network_ok(&a.error) && network_ok(&b.error));
        let empty: TransportDelivery=transport_receive(client,&mut output);assert(network_ok(&empty.error) && empty.copied==0 && empty.required==0);
        let stats: TransportStatistics=transport_statistics(client);assert(stats.used<=stats.limit && stats.state==1);
    }
    fn main() -> Unit {
        let cancel: Resource<OpaqueHandle,net_cancel_dispose>=network_cancel();assert(network_cancel_valid(&cancel));
        let bounds: TransportBounds=TransportBounds(1024,1,4,8388608);let req: TransportRequirements=TransportRequirements(19,0,0);
        let mut listener: TcpListener=tcp_listen(socket_address_v4(127,0,0,1,0),8,false);let local: SocketAddressResult=listener_local(&listener);assert(network_ok(&local.error));
        let choice: TransportChoice=transport_select(req,bounds,transport_candidates(1));assert(choice.profile==1);
        let mut client: TransportSession=transport_open(choice,req,bounds,transport_target(local.address,"","",""),1000,&cancel);let ce: NetworkError=transport_error(&client);assert(network_ok(&ce));
        let accepted: NetworkReady=listener_wait(&mut listener,1000,&cancel);assert(network_ok(&accepted.error));let raw: TcpSocket=tcp_accept(&mut listener);
        let mut server: TransportSession=transport_adopt_tcp(move raw,req,bounds);let se: NetworkError=transport_error(&server);assert(network_ok(&se));exchange(&mut client,&mut server,&cancel);
        transport_close(move client);transport_close(move server);listener_close(move listener);
        let left: UdpSocket=udp_bind(socket_address_v4(127,0,0,1,0),false);let right: UdpSocket=udp_bind(socket_address_v4(127,0,0,1,0),false);let la: SocketAddressResult=udp_local(&left);let ra: SocketAddressResult=udp_local(&right);assert(network_ok(&la.error) && network_ok(&ra.error));
        let datagrams: TransportRequirements=TransportRequirements(80,0,0);let mut uc: TransportSession=transport_adopt_udp(move left,ra.address,datagrams,bounds);let mut us: TransportSession=transport_adopt_udp(move right,la.address,datagrams,bounds);exchange(&mut uc,&mut us,&cancel);
        transport_close(move uc);transport_close(move us);resource_close(move cancel);
        unsafe { let stopped: NetIo=network_provider_shutdown();assert(stopped.status==0); }
        print("transport TCP UDP messages policy retention and backpressure OK");
    }
}
