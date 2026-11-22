module std.net_adaptive
import std.net_identity
import std.net_optimizer
import std.net_transport

native {
    trust "dlm.edge.v1";
    record AdaptiveConfig repr(C, version=1) { bounds: TransportBounds; requirements: TransportRequirements; policy: OptimizerPolicy; handshake_ms: U32; idle_ms: U32; duration_ms: U32; probe_ms: U32; automatic: Bool; }
    record AdaptivePath repr(C) { id: U64; }
    record AdaptivePathResult repr(C) { path: AdaptivePath; error: NetworkError; }
    record AdaptiveNativeMessage repr(C, version=1) { id: U64; sequence: U64; channel: U32; count: USize; status: I32; system: I32; }
    record AdaptiveMessage repr(C) { id: U64; sequence: U64; channel: U32; count: USize; error: NetworkError; }
    record AdaptiveNativeDelivery repr(C, version=1) { id: U64; sequence: U64; channel: U32; copied: USize; required: USize; status: I32; system: I32; }
    record AdaptiveDelivery repr(C) { id: U64; sequence: U64; channel: U32; copied: USize; required: USize; error: NetworkError; }
    record AdaptiveStatistics repr(C, version=1) { sent: U64; received: U64; acked: U64; retransmitted: U64; duplicates: U64; migrations: U64; active: U64; age_ms: U64; used: USize; limit: USize; backend_used: USize; backend_limit: USize; queued: U32; received_queued: U32; paths: U32; authenticated: U32; status: I32; system: I32; }
    record AdaptiveSession repr(C) { inner: Resource<NetOpened,adaptive_dispose>; }
    extern "C" fn dlm_adaptive_new(signer: NetOpened, trust: NetOpened, record: SignedLocatorRecord, expected: LogicalIdentity, session: Array<U8,32>, config: AdaptiveConfig) -> NetOpened from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_adaptive_close(handle: OpaqueHandle) -> Unit from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_adaptive_add(handle: OpaqueHandle, transport: NetOpened, cost: U32) -> NetIo from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_adaptive_promote(handle: OpaqueHandle, path: U64) -> NetIo from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_adaptive_remove(handle: OpaqueHandle, path: U64) -> NetIo from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_adaptive_poll(handle: OpaqueHandle, timeout_ms: U32, cancel: OpaqueHandle) -> NetIo from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_adaptive_send(handle: OpaqueHandle, channel: U32, input: Ptr<U8>, count: USize) -> AdaptiveNativeMessage from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_adaptive_peek(handle: OpaqueHandle) -> AdaptiveNativeMessage from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_adaptive_receive(handle: OpaqueHandle, output: Ptr<U8>, capacity: USize) -> AdaptiveNativeDelivery from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_adaptive_stats(handle: OpaqueHandle) -> AdaptiveStatistics from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_adaptive_witness(handle: OpaqueHandle, path: U64) -> RuntimeWitness from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_adaptive_config(handle: OpaqueHandle) -> AdaptiveConfig from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_adaptive_rotate(handle: OpaqueHandle, signer: NetOpened, record: SignedLocatorRecord) -> NetIo from "dlm.edge.v1" guarantees(NoUnwind);
    unsafe fn adaptive_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { dlm_adaptive_close(state.handle); }
    fn adaptive_config() -> AdaptiveConfig guarantees(NoAlloc,NoBlocking,NoIO) { return AdaptiveConfig(TransportBounds(65535,32,8,8388608),TransportRequirements(1|4|16,0,0),optimizer_policy(),10000,30000,300000,250,true); }
    fn adaptive_transport_bounds(config: AdaptiveConfig) -> TransportBounds guarantees(NoAlloc,NoBlocking,NoIO) { if config.bounds.max_message>65571 || config.bounds.channels==0 || config.bounds.channels>64 || config.bounds.queued_messages==0 || config.bounds.queued_messages>256 || config.bounds.memory>67108864 { return TransportBounds(0,0,0,0); }let mut maximum: U32=config.bounds.max_message+176;if maximum<1024 { maximum=1024; }let mut messages: U32=config.bounds.queued_messages;if messages<2 { messages=2; }let mut memory: USize=config.bounds.memory;if memory<2097152 { memory=2097152; }return TransportBounds(maximum,messages,config.bounds.channels*2+1,memory); }
    fn adaptive_transport_bounds_for(config: AdaptiveConfig, profile: U32) -> TransportBounds guarantees(NoAlloc,NoBlocking,NoIO) { let mut bounds: TransportBounds=adaptive_transport_bounds(config);if bounds.channels==0 { return bounds; }if profile==3 { bounds.max_message=1200; }if profile==6 { bounds.max_message=1084; }return bounds; }
    fn adaptive_open(signer: NodeSigner, trust: NodeTrust, record: SignedLocatorRecord, expected: LogicalIdentity, session: Array<U8,32>, config: AdaptiveConfig) -> AdaptiveSession { unsafe { return AdaptiveSession(resource(dlm_adaptive_new(resource_into_inner(move signer.inner),resource_into_inner(move trust.inner),record,expected,session,config))); } }
    fn adaptive_error(session: &AdaptiveSession) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { let state: &NetOpened=resource_get(&session.inner);return NetworkError(state.status,state.system,140); }
    fn adaptive_close(session: AdaptiveSession) -> Unit { resource_close(move session.inner); }
    fn adaptive_add(session: &mut AdaptiveSession, transport: TransportSession, cost: U32) -> AdaptivePathResult { unsafe { let state: &NetOpened=resource_get(&session.inner);let r: NetIo=dlm_adaptive_add(state.handle,resource_into_inner(move transport.inner),cost);return AdaptivePathResult(AdaptivePath(cast<U64>(r.count)),NetworkError(r.status,r.system,141)); } }
    fn adaptive_promote(session: &mut AdaptiveSession, path: AdaptivePath) -> NetworkError { unsafe { let state: &NetOpened=resource_get(&session.inner);let r: NetIo=dlm_adaptive_promote(state.handle,path.id);return NetworkError(r.status,r.system,142); } }
    fn adaptive_remove(session: &mut AdaptiveSession, path: AdaptivePath) -> NetworkError { unsafe { let state: &NetOpened=resource_get(&session.inner);let r: NetIo=dlm_adaptive_remove(state.handle,path.id);return NetworkError(r.status,r.system,143); } }
    fn adaptive_poll(session: &mut AdaptiveSession, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&session.inner);return socket_transfer(dlm_adaptive_poll(state.handle,timeout_ms,*resource_get(cancel)),144); } }
    fn adaptive_message(r: AdaptiveNativeMessage) -> AdaptiveMessage guarantees(NoAlloc,NoBlocking,NoIO) { return AdaptiveMessage(r.id,r.sequence,r.channel,r.count,NetworkError(r.status,r.system,145)); }
    fn adaptive_send(session: &mut AdaptiveSession, channel: U32, input: Slice<U8>) -> AdaptiveMessage { unsafe { let state: &NetOpened=resource_get(&session.inner);let bytes: NetText=net_bytes(input);return adaptive_message(dlm_adaptive_send(state.handle,channel,bytes.data,bytes.count)); } }
    fn adaptive_peek(session: &AdaptiveSession) -> AdaptiveMessage { unsafe { let state: &NetOpened=resource_get(&session.inner);return adaptive_message(dlm_adaptive_peek(state.handle)); } }
    fn adaptive_receive(session: &mut AdaptiveSession, output: &mut Buffer<U8>) -> AdaptiveDelivery { unsafe { let state: &NetOpened=resource_get(&session.inner);let r: AdaptiveNativeDelivery=dlm_adaptive_receive(state.handle,net_output(output),len(output));return AdaptiveDelivery(r.id,r.sequence,r.channel,r.copied,r.required,NetworkError(r.status,r.system,146)); } }
    fn adaptive_statistics(session: &AdaptiveSession) -> AdaptiveStatistics { unsafe { let state: &NetOpened=resource_get(&session.inner);return dlm_adaptive_stats(state.handle); } }
    fn adaptive_witness(session: &AdaptiveSession, path: AdaptivePath) -> RuntimeWitness { unsafe { let state: &NetOpened=resource_get(&session.inner);return dlm_adaptive_witness(state.handle,path.id); } }
    fn adaptive_configuration(session: &AdaptiveSession) -> AdaptiveConfig { unsafe { let state: &NetOpened=resource_get(&session.inner);return dlm_adaptive_config(state.handle); } }
    fn adaptive_rotate(session: &mut AdaptiveSession, signer: NodeSigner, record: SignedLocatorRecord) -> NetworkError { unsafe { let state: &NetOpened=resource_get(&session.inner);let result: NetIo=dlm_adaptive_rotate(state.handle,resource_into_inner(move signer.inner),record);return NetworkError(result.status,result.system,147); } }
    fn adaptive_connect(session: &mut AdaptiveSession, profile: U32, target: TransportTarget, cost: U32, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> AdaptivePathResult {
        let config: AdaptiveConfig=adaptive_configuration(session);let caps: U32=optimizer_properties(profile,config.policy.delivery);if profile==0 || profile>6 || caps==0 || (caps&config.requirements.required)!=config.requirements.required || (caps&config.requirements.forbidden)!=0 { return AdaptivePathResult(AdaptivePath(0),NetworkError(6,0,148)); }let native: TransportRequirements=TransportRequirements(16|(config.requirements.required&40),0,0);let bounds: TransportBounds=adaptive_transport_bounds_for(config,profile);let mut candidates: Array<TransportCandidate,6>=transport_candidates(0);candidates[0]=TransportCandidate(profile,true,cost);let choice: TransportChoice=transport_select(native,bounds,candidates);let transport: TransportSession=transport_open(choice,native,bounds,target,timeout_ms,cancel);return adaptive_add(session,move transport,cost);
    }
}
