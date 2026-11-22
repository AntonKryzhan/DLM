module fixtures.dln.quic
import std.net_quic

native {
    record QuicOwners repr(C) { first: QuicEndpoint; second: QuicEndpoint; }
    export fn quic_fixture_probe() -> I32 {
        let endpoint: QuicEndpoint=quic_client(socket_address_v4(127,0,0,1,0),"dlm-cleanup/1","examples/native/fixtures/quic-test-ca.pem",quic_config());
        let error: NetworkError=quic_error(&endpoint);return error.kind;
    }
    export fn quic_abort_with_partial_move() -> Unit {
        let address: SocketAddress=socket_address_v4(127,0,0,1,0);let config: QuicConfig=quic_config();
        let mut owners: QuicOwners=QuicOwners(quic_client(address,"dlm-cleanup/1","examples/native/fixtures/quic-test-ca.pem",config),quic_client(address,"dlm-cleanup/1","examples/native/fixtures/quic-test-ca.pem",config));
        let a: NetworkError=quic_error(&owners.first);let b: NetworkError=quic_error(&owners.second);assert(network_ok(&a) && network_ok(&b));
        quic_close(move owners.first);assert(false);
    }
    export unsafe fn quic_shutdown() -> NetIo { return quic_provider_shutdown(); }
    export unsafe fn quic_network_shutdown() -> NetIo { return network_provider_shutdown(); }
}
