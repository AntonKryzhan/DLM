module fixtures.dln.async
import std.net_async

native {
    record ScopedOwners repr(C) { first: NetworkScope; second: NetworkScope; signal: ScopeCanceller; }
    export fn scope_abort_with_pending_buffers_and_waker() -> Unit {
        let first: NetworkScope=scope_open(1,2,32,2);
        let second: NetworkScope=scope_open(1,2,65536,2);
        let signal: ScopeCanceller=scope_canceller(&second);
        let mut owners: ScopedOwners=ScopedOwners(move first,move second,move signal);
        let udp: UdpSocket=udp_bind(socket_address_v4(127,0,0,1,0),false);
        let adopted: ReactorUdpResult=scope_adopt_udp(&mut owners.second,move udp);assert(network_ok(&adopted.error));
        let pending: NetworkOperation=scope_receive(&mut owners.second,adopted.channel,65536,10000);assert(network_ok(&pending.error));
        scope_close(move owners.first);assert(false);
    }
    export unsafe fn scope_shutdown() -> NetIo { return network_provider_shutdown(); }
}
