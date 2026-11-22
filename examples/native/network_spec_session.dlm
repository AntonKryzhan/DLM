module examples.native.network_spec_session
import std.net_spec_compat
import std.net_transport
import std.native_binary
import std.net_bytes

native {
    fn session_spec() -> NetworkSpec guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut spec: NetworkSpec=network_spec(0x444c4e33,1,0,0);let mut message: MessageSchema=message_schema(1,2,3);
        let f: I32=message_add_field(&mut message,network_field(1,4,1,1,0));let m: I32=network_add_message(&mut spec,message);assert(f==0 && m==0);spec.transport=TransportRequirements(19,0,0);return spec;
    }
    fn main() -> Unit {
        let local_spec: NetworkSpec=session_spec();let peer_spec: NetworkSpec=session_spec();let agreed: NetworkNegotiation=network_spec_negotiate(&local_spec,&peer_spec,true,transport_candidates(1));assert(agreed.status==0 && agreed.choice.profile==1 && agreed.bounds.max_message==28 && agreed.pending_proofs!=0);
        let cancel: Resource<OpaqueHandle,net_cancel_dispose>=network_cancel();assert(network_cancel_valid(&cancel));let mut listener: TcpListener=tcp_listen(socket_address_v4(127,0,0,1,0),8,false);
        let address: SocketAddressResult=listener_local(&listener);assert(network_ok(&address.error));let mut client: TransportSession=transport_open(agreed.choice,agreed.requirements,agreed.bounds,transport_target(address.address,"","",""),agreed.session.handshake_timeout_ms,&cancel);let error: NetworkError=transport_error(&client);assert(network_ok(&error));
        let ready: NetworkReady=listener_wait(&mut listener,1000,&cancel);assert(network_ok(&ready.error));let accepted: TcpSocket=tcp_accept(&mut listener);let mut server: TransportSession=transport_adopt_tcp(move accepted,agreed.requirements,agreed.bounds);let server_error: NetworkError=transport_error(&server);assert(network_ok(&server_error));
        // Handwritten existing DLMB API; automatic payload codecs are DLN-09.
        let mut value: Buffer<U8>=buffer<U8>(8);write_le<U64>(&mut value,0,U64(18446744073709551615));let mut body: Buffer<U8>=wire_new(2);wire_set(&mut body,1,slice(&value,0,8));assert(len(&body)==24);
        let mut packet: Buffer<U8>=buffer<U8>(28);let id: I32=net_write_be(&mut packet,0,4,1);assert(id==0);for index in 0..24 { packet[index+4]=body[index]; }
        let sent: NetworkTransfer=transport_send(&mut client,0,slice(&packet,0,28));assert(network_ok(&sent.error));let flushed: NetworkTransfer=transport_poll(&mut client,0,&cancel);let read: NetworkTransfer=transport_poll(&mut server,1000,&cancel);assert(network_ok(&flushed.error) && network_ok(&read.error));
        let mut received: Buffer<U8>=buffer<U8>(28);let delivery: TransportDelivery=transport_receive(&mut server,&mut received);assert(network_ok(&delivery.error) && delivery.copied==28);let kind: ProtocolValue=net_read_be(slice(&received,0,28),0,4);assert(kind.status==0 && kind.value==1);
        let mut decoded: Buffer<U8>=buffer<U8>(24);for index in 0..24 { decoded[index]=received[index+4]; }assert(wire_valid(&decoded));let field: Slice<U8>=wire_field(&decoded,1);let number: ProtocolValue=net_read_le(field,0,8);assert(number.status==0 && number.value==U64(18446744073709551615));
        transport_close(move client);transport_close(move server);listener_close(move listener);resource_close(move cancel);unsafe { let stopped: NetIo=network_provider_shutdown();assert(stopped.status==0); }
        print("NetworkSpec admitted TCP session with unchanged DLMB payload OK");
    }
}
