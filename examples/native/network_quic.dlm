module examples.native.network_quic
import std.net_quic

native {
    fn connected(endpoint: &mut QuicEndpoint) -> U64 {
        let mut id: U64=0;let mut event: QuicEvent=quic_next(endpoint);
        while network_ok(&event.error) {
            if event.kind==1 { id=event.stream.connection.id; }
            let ack: NetworkTransfer=quic_ack(endpoint,event.id);assert(network_ok(&ack.error));event=quic_next(endpoint);
        }
        assert(network_would_block(&event.error));return id;
    }
    fn receive(endpoint: &mut QuicEndpoint) -> U32 {
        let mut flags: U32=0;let mut output: Buffer<U8>=buffer<U8>(8);let mut event: QuicEvent=quic_next(endpoint);
        while network_ok(&event.error) {
            if event.kind==3 && event.count==4 {
                let r: NetworkTransfer=quic_copy(endpoint,event.id,0,&mut output);assert(network_ok(&r.error) && r.count==4 && output[0]==0 && output[1]==255 && output[2]==128 && output[3]==42 && quic_event_fin(&event));flags=flags|1;
            }
            if event.kind==3 && event.count==0 { assert(quic_event_fin(&event));flags=flags|2; }
            if event.kind==5 { let r: NetworkTransfer=quic_copy(endpoint,event.id,0,&mut output);assert(network_ok(&r.error) && r.count==3 && output[1]==255 && output[2]==128);flags=flags|4; }
            let ack: NetworkTransfer=quic_ack(endpoint,event.id);assert(network_ok(&ack.error));event=quic_next(endpoint);
        }
        assert(network_would_block(&event.error));return flags;
    }
    fn main() -> Unit {
        assert(quic_available());let cancel: Resource<OpaqueHandle,net_cancel_dispose>=network_cancel();assert(network_cancel_valid(&cancel));
        let config: QuicConfig=quic_config();let local: SocketAddress=socket_address_v4(127,0,0,1,0);
        let mut server: QuicEndpoint=quic_server(local,"dlm-example/1","examples/native/fixtures/quic-test-server.pem","examples/native/fixtures/quic-test-key.pem",config);
        let mut client: QuicEndpoint=quic_client(local,"dlm-example/1","examples/native/fixtures/quic-test-ca.pem",config);
        let se: NetworkError=quic_error(&server);let ce: NetworkError=quic_error(&client);assert(network_ok(&se) && network_ok(&ce));
        let destination: SocketAddressResult=quic_local(&server);assert(network_ok(&destination.error));
        let connection: QuicConnectionResult=quic_connect(&mut client,destination.address,"localhost");assert(network_ok(&connection.error));
        let mut ci: U64=0;let mut si: U64=0;let deadline: U64=network_clock_ms()+5000;
        while ci==0 || si==0 {
            assert(network_clock_ms()<deadline);let a: NetworkTransfer=quic_poll(&mut client,1,&cancel);let b: NetworkTransfer=quic_poll(&mut server,1,&cancel);assert(a.error.kind<=3 && b.error.kind<=3);
            let c: U64=connected(&mut client);let s: U64=connected(&mut server);if c!=0 { ci=c; }if s!=0 { si=s; }
        }
        assert(ci==connection.connection.id);
        let bidi: QuicStreamResult=quic_open_stream(&mut client,connection.connection,false);let uni: QuicStreamResult=quic_open_stream(&mut client,connection.connection,true);assert(network_ok(&bidi.error) && network_ok(&uni.error));
        let mut bytes: Buffer<U8>=buffer<U8>(4);bytes[0]=0;bytes[1]=255;bytes[2]=128;bytes[3]=42;
        let send_a: NetworkTransfer=quic_write(&mut client,bidi.stream,slice(&bytes,0,4),true);let send_b: NetworkTransfer=quic_write(&mut client,uni.stream,slice(&bytes,0,0),true);let d: NetworkTransfer=quic_datagram(&mut client,connection.connection,slice(&bytes,0,3));assert(network_ok(&send_a.error) && network_ok(&send_b.error) && network_ok(&d.error));
        let mut received: U32=0;
        while received!=7 {
            assert(network_clock_ms()<deadline);let a: NetworkTransfer=quic_poll(&mut client,1,&cancel);let b: NetworkTransfer=quic_poll(&mut server,1,&cancel);assert(a.error.kind<=3 && b.error.kind<=3);
            received=received|receive(&mut server);let ignored: U64=connected(&mut client);
        }
        let stats: QuicStatistics=quic_statistics(&client,connection.connection);assert(stats.version==1 && stats.used<=stats.limit && stats.cwnd>0);
        let close: NetworkTransfer=quic_disconnect(&mut client,connection.connection,0);assert(network_ok(&close.error));
        let mut c: QuicStatistics=quic_statistics(&client,connection.connection);let mut s: QuicStatistics=quic_statistics(&server,QuicConnection(si));
        while c.state!=5 || s.state!=5 {
            assert(network_clock_ms()<deadline);let a: NetworkTransfer=quic_poll(&mut client,1,&cancel);let b: NetworkTransfer=quic_poll(&mut server,1,&cancel);assert(a.error.kind<=3 && b.error.kind<=3);
            let x: U64=connected(&mut client);let y: U64=connected(&mut server);c=quic_statistics(&client,connection.connection);s=quic_statistics(&server,QuicConnection(si));
        }
        let cf: NetworkTransfer=quic_forget(&mut client,connection.connection);let sf: NetworkTransfer=quic_forget(&mut server,QuicConnection(si));assert(network_ok(&cf.error) && network_ok(&sf.error));
        quic_close(move client);quic_close(move server);resource_close(move cancel);
        unsafe { let q: NetIo=quic_provider_shutdown();let n: NetIo=network_provider_shutdown();assert(q.status==0 && n.status==0); }
        print("QUIC verified TLS streams datagrams flow control and close OK");
    }
}
