module examples.native.network_transport_policy
import std.net_transport_policy

native {
    fn main() -> Unit guarantees(NoAlloc,NoBlocking,NoIO) {
        let bounds: TransportBounds=TransportBounds(1024,8,8,8388608);
        let candidates: Array<TransportCandidate,6>=transport_candidates(63);
        let ordered_secure: TransportChoice=transport_select(TransportRequirements(23,0,8),bounds,candidates);
        assert(ordered_secure.status==0 && ordered_secure.profile==4);
        let reliable_unordered: TransportChoice=transport_select(TransportRequirements(85,2,0),bounds,candidates);
        assert(reliable_unordered.status==0 && reliable_unordered.profile==5);
        let datagram: TransportChoice=transport_select(TransportRequirements(84,3,0),bounds,candidates);
        assert(datagram.status==0 && datagram.profile==6);
        let impossible: TransportChoice=transport_select(TransportRequirements(256,0,0),bounds,candidates);assert(impossible.status==6 && impossible.profile==0);
        let contradictory: TransportChoice=transport_select(TransportRequirements(66,0,0),bounds,candidates);assert(contradictory.status==4);
        for mask in 0..512 {
            let req: TransportRequirements=TransportRequirements(cast<U32>(mask),0,0);
            let result: TransportChoice=transport_select(req,bounds,candidates);
            if result.status==0 { assert((result.properties&req.required)==req.required);assert(transport_check(result.profile,req,bounds).status==0); }
        }
    }
}
