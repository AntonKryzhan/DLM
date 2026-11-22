module examples.native.network_scope
import std.net_async

native {
    fn main() -> Unit {
        let cancel: Resource<OpaqueHandle,net_cancel_dispose>=network_cancel();assert(network_cancel_valid(&cancel));
        let mut group: NetworkScope=scope_open(24,32,65536,16);
        let opened: NetworkError=scope_error(&group);assert(network_ok(&opened));
        let listener: TcpListener=tcp_listen(socket_address_v4(127,0,0,1,0),16,false);
        let address: SocketAddressResult=listener_local(&listener);assert(network_ok(&address.error));
        let adopted: ReactorListenerResult=scope_adopt_listener(&mut group,move listener);assert(network_ok(&adopted.error));
        let first: NetworkOperation=scope_accept(&mut group,adopted.channel,3000);assert(network_ok(&first.error));
        let mut clients: Array<ReactorTcp,8>=zero<Array<ReactorTcp,8>>();
        let mut peers: Array<ReactorTcp,8>=zero<Array<ReactorTcp,8>>();
        for i in 0..8 {
            let connection: ReactorConnect=scope_connect(&mut group,address.address,3000);
            assert(network_ok(&connection.operation.error));clients[i]=connection.channel;
        }
        let mut connected: USize=0;let mut accepted: USize=0;
        while connected<8 || accepted<8 {
            let wait: NetworkTransfer=scope_poll(&mut group,3000,&cancel);assert(network_ok(&wait.error));
            let result: ReactorCompletion=scope_next(&group);assert(network_ok(&result.transfer.error));
            if result.kind==8 { connected=connected+1; }
            if result.kind==3 {
                peers[accepted]=result.accepted;accepted=accepted+1;
                if accepted<8 { let next: NetworkOperation=scope_accept(&mut group,adopted.channel,3000);assert(network_ok(&next.error)); }
            }
            let ack: NetworkTransfer=scope_ack(&mut group,result.operation);assert(network_ok(&ack.error));
        }
        let mut bytes: Buffer<U8>=buffer<U8>(4);bytes[1]=0;bytes[2]=255;bytes[3]=42;
        for i in 0..8 {
            let reading: NetworkOperation=scope_read(&mut group,peers[i],4,true,3000);assert(network_ok(&reading.error));
            bytes[0]=cast<U8>(i);
            let writing: NetworkOperation=scope_write(&mut group,clients[i],slice(&bytes,0,4),3000);assert(network_ok(&writing.error));
        }
        bytes[0]=99;
        let joined: NetworkTransfer=scope_join(&mut group,3000,&cancel);assert(network_ok(&joined.error));assert(joined.count==16);
        let mut reads: USize=0;let mut writes: USize=0;
        for i in 0..16 {
            let result: ReactorCompletion=scope_next(&group);assert(network_ok(&result.transfer.error));assert(result.transfer.count==4);
            if result.kind==1 {
                let copied: NetworkTransfer=scope_copy(&group,result.operation,0,&mut bytes);assert(network_ok(&copied.error));
                assert(bytes[0]<8 && bytes[1]==0 && bytes[2]==255 && bytes[3]==42);reads=reads+1;
            }
            if result.kind==2 { writes=writes+1; }
            let ack: NetworkTransfer=scope_ack(&mut group,result.operation);assert(network_ok(&ack.error));
        }
        assert(reads==8 && writes==8);
        let udp: UdpSocket=udp_bind(socket_address_v4(127,0,0,1,0),false);
        let local: SocketAddressResult=udp_local(&udp);assert(network_ok(&local.error));
        let datagrams: ReactorUdpResult=scope_adopt_udp(&mut group,move udp);assert(network_ok(&datagrams.error));
        let reading: NetworkOperation=scope_receive(&mut group,datagrams.channel,0,3000);assert(network_ok(&reading.error));
        let writing: NetworkOperation=scope_send(&mut group,datagrams.channel,local.address,slice(&bytes,0,0),3000);assert(network_ok(&writing.error));
        let complete: NetworkTransfer=scope_join(&mut group,3000,&cancel);assert(network_ok(&complete.error));assert(complete.count==2);
        let empty: ReactorCompletion=scope_result(&group,reading.id);assert(network_ok(&empty.transfer.error));assert(empty.transfer.count==0);
        let stats: ScopeStatistics=scope_statistics(&group);assert(stats.reactor.pending==0 && stats.kernel_pending==0);
        scope_close(move group);resource_close(move cancel);
        unsafe { let shutdown: NetIo=network_provider_shutdown();assert(shutdown.status==0); }
        print("scoped TCP UDP and join OK");
    }
}
