module fixtures.dln.reactor
import std.net_reactor

native {
    record ReactorOwners repr(C) { first: NetworkReactor; second: NetworkReactor; }
    export fn reactor_abort_with_pending_buffers() -> Unit {
        let mut owners: ReactorOwners=ReactorOwners(reactor_open(2,4,65536),reactor_open(2,4,65536));
        let socket: UdpSocket=udp_bind(socket_address_v4(127,0,0,1,0),false);
        let adopted: ReactorUdpResult=reactor_adopt_udp(&mut owners.second,move socket);assert(network_ok(&adopted.error));
        let reading: NetworkOperation=reactor_receive(&mut owners.second,adopted.channel,65536,10000);assert(network_ok(&reading.error));
        let ready: NetworkOperation=reactor_timer(&mut owners.second,0);assert(network_ok(&ready.error));
        reactor_close(move owners.first);
        assert(false);
    }
    export fn reactor_udp_roundtrip() -> Unit {
        let cancel: Resource<OpaqueHandle,net_cancel_dispose>=network_cancel();assert(network_cancel_valid(&cancel));
        let mut group: NetworkReactor=reactor_open(2,4,64);
        let socket: UdpSocket=udp_bind(socket_address_v4(127,0,0,1,0),false);
        let local: SocketAddressResult=udp_local(&socket);assert(network_ok(&local.error));
        let adopted: ReactorUdpResult=reactor_adopt_udp(&mut group,move socket);assert(network_ok(&adopted.error));
        let reading: NetworkOperation=reactor_receive(&mut group,adopted.channel,8,1000);assert(network_ok(&reading.error));
        let mut bytes: Buffer<U8>=buffer<U8>(3);bytes[0]=0;bytes[1]=255;bytes[2]=128;
        let writing: NetworkOperation=reactor_send(&mut group,adopted.channel,local.address,slice(&bytes,0,3),1000);assert(network_ok(&writing.error));
        let sent: ReactorCompletion=reactor_result(&group,writing.id);assert(network_ok(&sent.transfer.error));
        let freed: NetworkTransfer=reactor_ack(&mut group,writing.id);assert(network_ok(&freed.error));
        let wait: NetworkTransfer=reactor_poll(&mut group,1000,&cancel);assert(network_ok(&wait.error));
        let result: ReactorCompletion=reactor_result(&group,reading.id);assert(network_ok(&result.transfer.error));assert(result.transfer.count==3);
        bytes[0]=1;bytes[1]=1;bytes[2]=1;
        let copied: NetworkTransfer=reactor_copy(&group,reading.id,0,&mut bytes);assert(network_ok(&copied.error));
        assert(bytes[0]==0 && bytes[1]==255 && bytes[2]==128);
        let done: NetworkTransfer=reactor_ack(&mut group,reading.id);assert(network_ok(&done.error));
        let restored: UdpSocket=reactor_release_udp(&mut group,adopted.channel);
        let released: NetworkError=udp_error(&restored);assert(network_ok(&released));udp_close(move restored);
        reactor_close(move group);resource_close(move cancel);
    }
    export unsafe fn reactor_shutdown() -> NetIo { return network_provider_shutdown(); }
}
