module examples.native.network_transport_quic
import std.net_transport

native {
    fn connected(endpoint: &mut QuicEndpoint) -> U64 {
        let mut id: U64=0;let mut event: QuicEvent=quic_next(endpoint);
        while network_ok(&event.error) {
            assert(event.kind!=3 && event.kind!=5);if event.kind==1 { id=event.stream.connection.id; }
            let ack: NetworkTransfer=quic_ack(endpoint,event.id);assert(network_ok(&ack.error));event=quic_next(endpoint);
        }
        assert(network_would_block(&event.error));return id;
    }
    fn exchange(profile: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> Unit {
        let bounds: TransportBounds=TransportBounds(1024,8,8,8388608);let req: TransportRequirements=TransportRequirements(transport_profile_properties(profile),0,0);
        let local: SocketAddress=socket_address_v4(127,0,0,1,0);let config: QuicConfig=transport_quic_config(bounds);
        let mut raw_server: QuicEndpoint=quic_server(local,transport_alpn(profile),"examples/native/fixtures/quic-test-server.pem","examples/native/fixtures/quic-test-key.pem",config);
        let opened: NetworkError=quic_error(&raw_server);assert(network_ok(&opened));let destination: SocketAddressResult=quic_local(&raw_server);assert(network_ok(&destination.error));
        let choice: TransportChoice=transport_select(req,bounds,transport_candidates(U32(1)<<(profile-1)));assert(choice.status==0 && choice.profile==profile);
        let mut client: TransportSession=transport_open(choice,req,bounds,transport_target(destination.address,"localhost","examples/native/fixtures/quic-test-ca.pem",""),2000,cancel);let error: NetworkError=transport_error(&client);assert(network_ok(&error));
        let mut server_id: U64=0;let mut client_stats: TransportStatistics=transport_statistics(&client);let deadline: U64=network_clock_ms()+5000;
        while server_id==0 || client_stats.state!=1 {
            assert(network_clock_ms()<deadline);let a: NetworkTransfer=transport_poll(&mut client,1,cancel);let b: NetworkTransfer=quic_poll(&mut raw_server,1,cancel);assert(a.error.kind<=3 && b.error.kind<=3);
            let id: U64=connected(&mut raw_server);if id!=0 { server_id=id; }client_stats=transport_statistics(&client);
        }
        let mut server: TransportSession=transport_adopt_quic(move raw_server,QuicConnection(server_id),false,profile,req,bounds);let server_error: NetworkError=transport_error(&server);assert(network_ok(&server_error));
        let mut channel: U32=0;if profile!=6 { let opened_channel: NetworkTransfer=transport_open_channel(&mut client);assert(network_ok(&opened_channel.error));channel=cast<U32>(opened_channel.count); }
        let mut payload: Buffer<U8>=buffer<U8>(4);payload[0]=0;payload[1]=255;payload[2]=128;payload[3]=42;
        for size in 0..2 {
            let count: USize=cast<USize>(size)*4;let sent: NetworkTransfer=transport_send(&mut client,channel,slice(&payload,0,count));assert(network_ok(&sent.error));
            let mut message: TransportMessage=transport_peek(&server);
            while network_would_block(&message.error) { assert(network_clock_ms()<deadline);let a: NetworkTransfer=transport_poll(&mut client,1,cancel);let b: NetworkTransfer=transport_poll(&mut server,1,cancel);assert(a.error.kind<=3 && b.error.kind<=3);message=transport_peek(&server); }
            assert(network_ok(&message.error) && message.channel==channel && message.count==count);
            let mut output: Buffer<U8>=buffer<U8>(8);let received: TransportDelivery=transport_receive(&mut server,&mut output);assert(network_ok(&received.error) && received.copied==count);if count!=0 { assert(output[0]==0 && output[1]==255 && output[2]==128 && output[3]==42); }
            let reply: NetworkTransfer=transport_send(&mut server,channel,slice(&output,0,count));assert(network_ok(&reply.error));message=transport_peek(&client);
            while network_would_block(&message.error) { assert(network_clock_ms()<deadline);let a: NetworkTransfer=transport_poll(&mut client,1,cancel);let b: NetworkTransfer=transport_poll(&mut server,1,cancel);assert(a.error.kind<=3 && b.error.kind<=3);message=transport_peek(&client); }
            let echoed: TransportDelivery=transport_receive(&mut client,&mut output);assert(network_ok(&echoed.error) && echoed.channel==channel && echoed.copied==count);
        }
        let stats: TransportStatistics=transport_statistics(&client);assert(stats.used<=stats.limit && stats.profile==profile && stats.properties==req.required);
        transport_close(move client);transport_close(move server);
    }
    fn main() -> Unit {
        assert(quic_available());let cancel: Resource<OpaqueHandle,net_cancel_dispose>=network_cancel();assert(network_cancel_valid(&cancel));
        for profile in 4..7 { exchange(cast<U32>(profile),&cancel); }resource_close(move cancel);
        unsafe { let q: NetIo=quic_provider_shutdown();let n: NetIo=network_provider_shutdown();assert(q.status==0 && n.status==0); }
        print("transport QUIC ordered unordered datagram messages and channels OK");
    }
}
