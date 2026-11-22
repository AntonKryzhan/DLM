module examples.network_reactor
import std.net_reactor

native {
    fn main() -> Unit {
        let cancel: Resource<OpaqueHandle,net_cancel_dispose>=network_cancel();
        assert(network_cancel_valid(&cancel));
        let mut group: NetworkReactor=reactor_open(32,64,65536);
        let opened: NetworkError=reactor_error(&group);assert(network_ok(&opened));
        let server: TcpListener=tcp_listen(socket_address_v4(127,0,0,1,0),16,false);
        let address: SocketAddressResult=listener_local(&server);assert(network_ok(&address.error));
        let listening: ReactorListenerResult=reactor_adopt_listener(&mut group,move server);
        assert(network_ok(&listening.error));
        let mut clients: Array<ReactorTcp,8>=zero<Array<ReactorTcp,8>>();
        let mut peers: Array<ReactorTcp,8>=zero<Array<ReactorTcp,8>>();
        let first: NetworkOperation=reactor_accept(&mut group,listening.channel,2000);assert(network_ok(&first.error));
        for i in 0..8 {
            let connection: ReactorConnect=reactor_connect(&mut group,address.address,2000);
            assert(network_ok(&connection.operation.error));clients[i]=connection.channel;
        }
        let mut connected: USize=0;let mut accepted: USize=0;let mut turns: USize=0;
        while connected<8 || accepted<8 {
            assert(turns<100);turns=turns+1;
            let wait: NetworkTransfer=reactor_poll(&mut group,2000,&cancel);assert(network_ok(&wait.error));
            let result: ReactorCompletion=reactor_next(&group);
            assert(network_ok(&result.transfer.error));
            if result.kind==8 { connected=connected+1; }
            if result.kind==3 {
                peers[accepted]=result.accepted;accepted=accepted+1;
                if accepted<8 { let next: NetworkOperation=reactor_accept(&mut group,listening.channel,2000);assert(network_ok(&next.error)); }
            }
            let done: NetworkTransfer=reactor_ack(&mut group,result.operation);assert(network_ok(&done.error));
        }
        let mut bytes: Buffer<U8>=buffer<U8>(4);bytes[1]=0;bytes[2]=255;bytes[3]=42;
        for i in 0..8 {
            let reading: NetworkOperation=reactor_read(&mut group,peers[i],4,true,2000);assert(network_ok(&reading.error));
            bytes[0]=cast<U8>(i);
            let writing: NetworkOperation=reactor_write(&mut group,clients[i],slice(&bytes,0,4),2000);assert(network_ok(&writing.error));
        }
        bytes[0]=99;
        let mut reads: USize=0;let mut writes: USize=0;
        while reads<8 || writes<8 {
            let wait: NetworkTransfer=reactor_poll(&mut group,2000,&cancel);assert(network_ok(&wait.error));
            let result: ReactorCompletion=reactor_next(&group);assert(network_ok(&result.transfer.error));assert(result.transfer.count==4);
            if result.kind==1 {
                let copied: NetworkTransfer=reactor_copy(&group,result.operation,0,&mut bytes);assert(network_ok(&copied.error));
                assert(copied.count==4);assert(bytes[0]<8);assert(bytes[1]==0);assert(bytes[2]==255);assert(bytes[3]==42);reads=reads+1;
            }
            if result.kind==2 { writes=writes+1; }
            let done: NetworkTransfer=reactor_ack(&mut group,result.operation);assert(network_ok(&done.error));
        }
        let paused: NetworkOperation=reactor_read(&mut group,peers[0],8,true,2000);assert(network_ok(&paused.error));
        let cancelled: NetworkTransfer=reactor_cancel(&mut group,paused.id);assert(cancelled.count==1);
        let partial: ReactorCompletion=reactor_result(&group,paused.id);assert(network_was_cancelled(&partial.transfer.error));
        let ack: NetworkTransfer=reactor_ack(&mut group,paused.id);assert(network_ok(&ack.error));
        let timer: NetworkOperation=reactor_timer(&mut group,0);assert(network_ok(&timer.error));
        let event: NetworkOperation=reactor_event(&mut group,1000);assert(network_ok(&event.error));
        let signalled: NetworkTransfer=reactor_signal(&mut group,event.id);assert(network_ok(&signalled.error));
        let t: NetworkTransfer=reactor_ack(&mut group,timer.id);assert(network_ok(&t.error));
        let e: NetworkTransfer=reactor_ack(&mut group,event.id);assert(network_ok(&e.error));
        let stats: ReactorStatistics=reactor_statistics(&group);assert(stats.pending==0);assert(stats.completed==0);assert(stats.buffered_bytes==0);
        reactor_close(move group);
        resource_close(move cancel);
        unsafe { let shutdown: NetIo=network_provider_shutdown();assert(shutdown.status==0); }
        print("reactor TCP operations OK");
    }
}
