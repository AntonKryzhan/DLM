module examples.native.network_loopback
import std.native_net
import std.native_protocol

native {
    fn main() -> Unit {
        {
            let cancel: Resource<OpaqueHandle,net_cancel_dispose>=network_cancel();
            assert(network_cancel_valid(&cancel));
            let mut listener: Resource<NetOpened,net_dispose>=socket_listen("127.0.0.1",0,16);
            assert(socket_status(&listener) == I32(0));
            let local: NetResolved=socket_local(&listener);
            assert(local.status == I32(0));
            let mut client: Resource<NetOpened,net_dispose>=socket_tcp("127.0.0.1",cast<U16>(local.address.port),1000);
            assert(socket_status(&client) == I32(0));
            let ready: NetIo=socket_wait(&mut listener,1,1000,&cancel);
            assert(ready.status == I32(0));
            let mut server: Resource<NetOpened,net_dispose>=socket_accept(&mut listener);
            assert(socket_status(&server) == I32(0));
            let mut sent: Buffer<U8>=buffer<U8>(7);
            let encoded: I32=protocol_write_be(&mut sent,0,4,3);
            assert(encoded == I32(0));
            sent[4]=0;sent[5]=255;sent[6]=128;
            let written: NetIo=socket_write_all(&mut client,slice(&sent,0,len(&sent)),1000,&cancel);
            assert(written.status == I32(0) && written.count == USize(7));
            let mut received: Buffer<U8>=buffer<U8>(7);
            let read: NetIo=socket_read_exact(&mut server,&mut received,1000,&cancel);
            assert(read.status == I32(0) && read.count == USize(7));
            let frame: ProtocolFrame=protocol_frame32(slice(&received,0,read.count),0,1024);
            assert(frame.status == I32(0) && frame.count == USize(3));
            assert(received[4] == U8(0) && received[5] == U8(255) && received[6] == U8(128));
            let mut udp: Resource<NetOpened,net_dispose>=socket_udp("127.0.0.1",0);
            assert(socket_status(&udp) == I32(0));
            let endpoint: NetResolved=socket_local(&udp);
            assert(endpoint.status == I32(0));
            let datagram_sent: NetIo=socket_send_datagram(&mut udp,endpoint.address,slice(&sent,4,3));
            assert(datagram_sent.status == I32(0));
            let udp_ready: NetIo=socket_wait(&mut udp,1,1000,&cancel);
            assert(udp_ready.status == I32(0));
            let datagram: NetDatagram=socket_read_datagram(&mut udp,&mut received);
            assert(datagram.io.status == I32(0) && datagram.io.count == USize(3));
            assert(received[0] == U8(0) && received[1] == U8(255));
            print("binary TCP and UDP OK");
        }
        print(network_capabilities());
        unsafe { let stopped: NetIo=network_provider_shutdown();assert(stopped.status == I32(0)); }
    }
}
