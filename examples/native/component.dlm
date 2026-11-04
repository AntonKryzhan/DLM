module native.component

native {
    record Instance repr(C, version=1) {
        gain: AtomicF32;
    }

    export fn component_create(gain: F32) -> OpaqueHandle {
        let instance: Box<Instance> = box<Instance>(Instance(atomic<F32>(gain)));
        return handle(into_raw(move instance));
    }

    export unsafe fn component_destroy(instance: OpaqueHandle) -> Unit {
        let owned: Box<Instance> = from_raw(handle_ptr<Instance>(instance));
    }

    export unsafe fn component_set_gain(instance: OpaqueHandle, gain: F32) -> Unit guarantees(NoAlloc, NoBlocking, NoIO, ThreadSafe) {
        let state: Ptr<Instance> = handle_ptr<Instance>(instance);
        atomic_store(&(*state).gain, gain, Release);
    }

    // The host lends an exclusively writable array for this call.
    export unsafe fn component_process(instance: OpaqueHandle, data: Ptr<F32>, count: USize) -> Unit guarantees(NoAlloc, NoBlocking, NoIO, BoundedWork, ThreadSafe) {
        let state: Ptr<Instance> = handle_ptr<Instance>(instance);
        let gain: F32 = atomic_load(&(*state).gain, Acquire);
        let mut values: ForeignBuffer<F32> = foreign(data, count);
        for index in 0..len(&values) { values[index] = values[index] * gain; }
    }
    record ComponentApi repr(C,version=1) {
        version: U32;
        size: USize;
        create: FnPtr<F32,OpaqueHandle>;
        destroy: FnPtr<OpaqueHandle,Unit>;
        set_gain: FnPtr<OpaqueHandle,F32,Unit>;
        process: FnPtr<OpaqueHandle,Ptr<F32>,USize,Unit>;
    }
    export fn component_api() -> ComponentApi {
        return ComponentApi(U32(1),size_of<ComponentApi>(),fn_ptr(component_create),fn_ptr(component_destroy),fn_ptr(component_set_gain),fn_ptr(component_process));
    }

}
