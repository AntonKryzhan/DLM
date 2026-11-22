module native.managed_callbacks
native {
    trust "host.callbacks";
    record Context repr(C) { count: AtomicU64; gain: AtomicF32; }
    record Subscription repr(C) { token: OpaqueHandle; context: Box<Context>; }
    extern "C" fn host_subscribe(callback: FnPtr<OpaqueHandle,U64,U64>, context: OpaqueHandle) -> OpaqueHandle from "host.callbacks" guarantees(NoIO,NoUnwind,ThreadSafe);
    // Contract: cancellation waits until every invocation has returned, and
    // prevents future calls. A null token is accepted and has no effect.
    extern "C" fn host_unsubscribe(token: OpaqueHandle) -> Unit from "host.callbacks" guarantees(NoIO,NoUnwind,ThreadSafe);
    unsafe fn unsubscribe(subscription: &mut Subscription) -> Unit guarantees(NoUnwind) { host_unsubscribe(subscription.token); }
    unsafe fn increment(userdata: OpaqueHandle, amount: U64) -> U64 guarantees(NoAlloc,NoBlocking,NoIO,ThreadSafe) {
        let context: Ptr<Context>=handle_ptr<Context>(userdata);
        let mut old: U64=atomic_load(&(*context).count,Relaxed);
        while true {
            let next: U64=wrapping_add(old,amount);
            if atomic_compare_exchange(&(*context).count,old,next,AcqRel,Acquire) { return next; }
            old=atomic_load(&(*context).count,Relaxed);
        }
    }
    unsafe fn subscribe(seed: U64, gain: F32) -> Resource<Subscription,unsubscribe> {
        let mut owner: Resource<Subscription,unsubscribe>=resource(Subscription(handle(null<U8>()),box<Context>(Context(atomic<U64>(seed),atomic<F32>(gain)))));
        {
            let state: &mut Subscription=resource_get_mut(&mut owner);
            state.token=host_subscribe(fn_ptr(increment),handle(as_raw(borrowed_ptr(&mut *state.context))));
            assert(address(handle_ptr<U8>(state.token)) != USize(0));
        }
        return move owner;
    }
    export unsafe fn callback_create(seed: U64, gain: F32) -> OpaqueHandle {
        let owner: Box<Resource<Subscription,unsubscribe>>=box<Resource<Subscription,unsubscribe>>(subscribe(seed,gain));
        return handle(into_raw(move owner));
    }
    export unsafe fn callback_count(handle_: OpaqueHandle) -> U64 {
        let owner: Ptr<Resource<Subscription,unsubscribe>>=handle_ptr<Resource<Subscription,unsubscribe>>(handle_);
        let subscription: &Subscription=resource_get(&*owner);
        return atomic_load(&subscription.context.count,Acquire);
    }
    export unsafe fn callback_process(handle_: OpaqueHandle, samples: Ptr<F32>, count: USize) -> Unit guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork,ThreadSafe) {
        let owner: Ptr<Resource<Subscription,unsubscribe>>=handle_ptr<Resource<Subscription,unsubscribe>>(handle_);
        let subscription: &Subscription=resource_get(&*owner);
        let gain: F32=atomic_load(&subscription.context.gain,Relaxed);
        let mut buffer_: ForeignBuffer<F32>=foreign(samples,count);
        for i in 0..len(&buffer_) { buffer_[i]=buffer_[i]*gain; }
    }
    export unsafe fn callback_destroy(handle_: OpaqueHandle) -> Unit {
        let owner: Box<Resource<Subscription,unsubscribe>>=from_raw(handle_ptr<Resource<Subscription,unsubscribe>>(handle_));
    }
    export unsafe fn callback_fail() -> Unit { let owner: Resource<Subscription,unsubscribe>=subscribe(U64(0),F32(1));assert(false); }
}
