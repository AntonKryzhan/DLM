module std.net_transport_policy

native {
    record TransportRequirements repr(C, version=1) { required: U32; forbidden: U32; preferred: U32; }
    record TransportBounds repr(C, version=1) { max_message: U32; queued_messages: U32; channels: U32; memory: USize; }
    record TransportCandidate repr(C, version=1) { profile: U32; available: Bool; cost: U32; }
    record TransportCheck repr(C) { properties: U32; missing: U32; forbidden: U32; status: I32; }
    record TransportChoice repr(C) { profile: U32; properties: U32; missing: U32; forbidden: U32; preference_misses: U32; cost: U32; status: I32; }

    fn transport_reliable() -> U32 guarantees(NoAlloc,NoBlocking,NoIO) { return 1; }
    fn transport_ordered() -> U32 guarantees(NoAlloc,NoBlocking,NoIO) { return 2; }
    fn transport_secure() -> U32 guarantees(NoAlloc,NoBlocking,NoIO) { return 4; }
    fn transport_streams() -> U32 guarantees(NoAlloc,NoBlocking,NoIO) { return 8; }
    fn transport_messages() -> U32 guarantees(NoAlloc,NoBlocking,NoIO) { return 16; }
    fn transport_congestion_control() -> U32 guarantees(NoAlloc,NoBlocking,NoIO) { return 32; }
    fn transport_unordered() -> U32 guarantees(NoAlloc,NoBlocking,NoIO) { return 64; }
    fn transport_bounded_storage() -> U32 guarantees(NoAlloc,NoBlocking,NoIO) { return 128; }
    // Includes TLS, OS and dependency internals: no current profile advertises it.
    fn transport_total_memory_bound() -> U32 guarantees(NoAlloc,NoBlocking,NoIO) { return 256; }
    fn transport_bounds() -> TransportBounds guarantees(NoAlloc,NoBlocking,NoIO) { return TransportBounds(65536,32,16,8388608); }
    fn transport_requirements(required: U32, forbidden: U32, preferred: U32) -> TransportRequirements guarantees(NoAlloc,NoBlocking,NoIO) { return TransportRequirements(required,forbidden,preferred); }
    fn transport_profile_properties(profile: U32) -> U32 guarantees(NoAlloc,NoBlocking,NoIO) {
        if profile==1 { return 179; }
        if profile==2 { return 183; }
        if profile==3 { return 208; }
        if profile==4 { return 191; }
        if profile==5 { return 253; }
        if profile==6 { return 244; }
        return 0;
    }
    fn transport_requirements_valid(requirements: TransportRequirements) -> Bool guarantees(NoAlloc,NoBlocking,NoIO) {
        if ((requirements.required|requirements.forbidden|requirements.preferred)&U32(4294966784))!=0 { return false; }
        if (requirements.required&requirements.forbidden)!=0 || (requirements.preferred&requirements.forbidden)!=0 { return false; }
        return (requirements.required&U32(66))!=U32(66);
    }
    fn transport_bounds_valid(bounds: TransportBounds) -> Bool guarantees(NoAlloc,NoBlocking,NoIO) {
        if bounds.max_message>16777216 || bounds.queued_messages==0 || bounds.queued_messages>4096 || bounds.channels==0 || bounds.channels>256 { return false; }
        if bounds.memory<1024 || bounds.memory>67108864 { return false; }
        let reserve: USize=1024+cast<USize>(bounds.channels)*256+2*(cast<USize>(bounds.max_message)+128);
        return bounds.memory>=reserve;
    }
    fn transport_check(profile: U32, requirements: TransportRequirements, bounds: TransportBounds) -> TransportCheck guarantees(NoAlloc,NoBlocking,NoIO) {
        let properties: U32=transport_profile_properties(profile);
        if !transport_requirements_valid(requirements) || !transport_bounds_valid(bounds) || properties==0 { return TransportCheck(properties,0,0,4); }
        let missing: U32=requirements.required&(U32(511)^properties);let forbidden: U32=requirements.forbidden&properties;
        if missing!=0 || forbidden!=0 { return TransportCheck(properties,missing,forbidden,6); }
        if profile==3 && bounds.max_message>65491 { return TransportCheck(properties,0,0,7); }
        if profile==6 && bounds.max_message>1084 { return TransportCheck(properties,0,0,7); }
        if profile>=4 {
            if bounds.memory<2097152 || cast<USize>(bounds.max_message)+16>bounds.memory/8 { return TransportCheck(properties,0,0,7); }
            let reserve: USize=1024+cast<USize>(bounds.channels)*256+2*(cast<USize>(bounds.max_message)+128);
            if bounds.memory/2<reserve { return TransportCheck(properties,0,0,7); }
        }
        return TransportCheck(properties,0,0,0);
    }
    fn transport_preference_misses(properties: U32, preferred: U32) -> U32 guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let bits: U32=preferred&(U32(511)^properties);let mut count: U32=0;
        for index in 0..9 { if (bits&(U32(1)<<cast<U32>(index)))!=0 { count=count+1; } }
        return count;
    }
    // All mandatory checks precede ranking. Cost is a caller hint, not an RTT guarantee.
    fn transport_select(requirements: TransportRequirements, bounds: TransportBounds, candidates: Array<TransportCandidate,6>) -> TransportChoice guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if !transport_requirements_valid(requirements) || !transport_bounds_valid(bounds) { return TransportChoice(0,0,0,0,0,0,4); }
        let mut best: TransportChoice=TransportChoice(0,0,0,0,0,0,6);let mut missing: U32=0;let mut forbidden: U32=0;
        for index in 0..6 {
            let candidate: TransportCandidate=candidates[index];
            if candidate.available {
                let checked: TransportCheck=transport_check(candidate.profile,requirements,bounds);missing=missing|checked.missing;forbidden=forbidden|checked.forbidden;
                if checked.status==0 {
                    let misses: U32=transport_preference_misses(checked.properties,requirements.preferred);
                    if best.profile==0 || misses<best.preference_misses || (misses==best.preference_misses && candidate.cost<best.cost) || (misses==best.preference_misses && candidate.cost==best.cost && candidate.profile<best.profile) {
                        best=TransportChoice(candidate.profile,checked.properties,0,0,misses,candidate.cost,0);
                    }
                }
            }
        }
        if best.profile==0 { best.missing=missing;best.forbidden=forbidden; }
        return best;
    }
    fn transport_candidates(available: U32) -> Array<TransportCandidate,6> guarantees(NoAlloc,NoBlocking,NoIO) {
        let mut candidates: Array<TransportCandidate,6>=zero<Array<TransportCandidate,6>>();
        candidates[0]=TransportCandidate(1,(available&1)!=0,10);candidates[1]=TransportCandidate(2,(available&2)!=0,30);candidates[2]=TransportCandidate(3,(available&4)!=0,1);
        candidates[3]=TransportCandidate(4,(available&8)!=0,20);candidates[4]=TransportCandidate(5,(available&16)!=0,20);candidates[5]=TransportCandidate(6,(available&32)!=0,5);return candidates;
    }
}
