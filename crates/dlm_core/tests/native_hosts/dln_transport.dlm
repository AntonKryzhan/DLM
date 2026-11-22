module fixtures.dln.transport
import std.net_transport

native {
    record TransportOwners repr(C) { first: TransportSession; second: TransportSession; }
    export fn transport_policy_probe(required: U32, forbidden: U32, preferred: U32, available: U32, message: U32, memory: USize) -> TransportChoice guarantees(NoAlloc,NoBlocking,NoIO) {
        return transport_select(TransportRequirements(required,forbidden,preferred),TransportBounds(message,8,8,memory),transport_candidates(available));
    }
    fn udp_session() -> TransportSession {
        let raw: UdpSocket=udp_bind(socket_address_v4(127,0,0,1,0),false);return transport_adopt_udp(move raw,socket_address_v4(127,0,0,1,9),TransportRequirements(16,0,0),TransportBounds(1024,8,8,8388608));
    }
    export fn transport_abort_with_partial_move() -> Unit {
        let mut owners: TransportOwners=TransportOwners(udp_session(),udp_session());let a: NetworkError=transport_error(&owners.first);let b: NetworkError=transport_error(&owners.second);assert(network_ok(&a) && network_ok(&b));
        let mut bytes: Buffer<U8>=buffer<U8>(4);bytes[0]=255;let sent: NetworkTransfer=transport_send(&mut owners.second,0,slice(&bytes,0,4));assert(network_ok(&sent.error));transport_close(move owners.first);assert(false);
    }
    export fn transport_invalid_quic_profile() -> I32 {
        let q: QuicEndpoint=quic_client(socket_address_v4(127,0,0,1,0),transport_alpn(4),"examples/native/fixtures/quic-test-ca.pem",transport_quic_config(transport_bounds()));
        let session: TransportSession=transport_adopt_quic(move q,QuicConnection(0),true,1,TransportRequirements(16,0,0),transport_bounds());let e: NetworkError=transport_error(&session);return e.kind;
    }
    export unsafe fn transport_fixture_shutdown() -> NetIo { return network_provider_shutdown(); }
    export unsafe fn transport_fixture_quic_shutdown() -> NetIo { return quic_provider_shutdown(); }
}
