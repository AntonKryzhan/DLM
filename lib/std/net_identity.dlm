module std.net_identity
import std.net_crypto

native {
    trust "dlm.nodes.v1";
    record LogicalIdentity repr(C, version=1) { bytes: Array<U8,32>; }
    record NodePublicKey repr(C, version=1) { bytes: Array<U8,32>; }
    record NetworkLocator repr(C, version=1) { address: NetAddress; profiles: U32; priority: U32; }
    record SignedLocatorRecord repr(C, version=1) { identity: LogicalIdentity; root: NodePublicKey; key: NodePublicKey; epoch: U64; sequence: U64; not_before: U64; key_expires: U64; issued: U64; expires: U64; locator_count: U32; locators: Array<NetworkLocator,8>; delegation: Array<U8,64>; signature: Array<U8,64>; }
    record NodeNativeRecord repr(C, version=1) { record: SignedLocatorRecord; status: I32; system: I32; }
    record NodeRecordResult repr(C) { record: SignedLocatorRecord; error: NetworkError; }
    record NodeNativePublic repr(C, version=1) { value: NodePublicKey; status: I32; system: I32; }
    record NodePublicResult repr(C) { value: NodePublicKey; error: NetworkError; }
    record NodeNativeIdentity repr(C, version=1) { value: LogicalIdentity; status: I32; system: I32; }
    record NodeIdentityResult repr(C) { value: LogicalIdentity; error: NetworkError; }
    record NodeStoreStatistics repr(C, version=1) { used: USize; limit: USize; generation: U64; entries: U32; present: U32; status: I32; system: I32; }
    record NodeKey repr(C) { inner: Resource<NetOpened,node_key_dispose>; }
    record NodeSigner repr(C) { inner: Resource<NetOpened,node_signer_dispose>; }
    record NodeTrust repr(C) { inner: Resource<NetOpened,node_trust_dispose>; }
    record NodeChallenge repr(C) { inner: Resource<NetOpened,node_challenge_dispose>; }
    record NodeNativeConnection repr(C, version=1) { local: LogicalIdentity; peer: LogicalIdentity; binding: Array<U8,32>; key: NodePublicKey; epoch: U64; sequence: U64; status: I32; system: I32; }
    record ConnectionIdentity repr(C) { local: LogicalIdentity; peer: LogicalIdentity; binding: Array<U8,32>; key: NodePublicKey; epoch: U64; sequence: U64; error: NetworkError; }

    extern "C" fn dlm_node_capabilities() -> U32 from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_wall_seconds() -> U64 from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_identity(root: NodePublicKey) -> NodeNativeIdentity from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_key(bytes: Ptr<U8>, count: USize, random: Bool) -> NetOpened from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_key_close(handle: OpaqueHandle) -> Unit from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_public(handle: OpaqueHandle) -> NodeNativePublic from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_sign(handle: OpaqueHandle, input: Ptr<U8>, count: USize, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_verify(key: NodePublicKey, input: Ptr<U8>, count: USize, signature: Ptr<U8>, size: USize) -> NetIo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_signer(root: OpaqueHandle, key: OpaqueHandle, epoch: U64, first: U64, last: U64) -> NetOpened from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_signer_close(handle: OpaqueHandle) -> Unit from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_signer_resume(handle: OpaqueHandle, previous: SignedLocatorRecord, now: U64) -> NetIo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_record(handle: OpaqueHandle, locators: Ptr<NetworkLocator>, count: USize, now: U64, ttl: U32) -> NodeNativeRecord from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_record_encode(record: SignedLocatorRecord, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_record_decode(input: Ptr<U8>, count: USize, now: U64) -> NodeNativeRecord from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_trust_new(capacity: U32, memory: USize) -> NetOpened from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_trust_close(handle: OpaqueHandle) -> Unit from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_trust_pin(handle: OpaqueHandle, root: NodePublicKey) -> NetIo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_trust_accept(handle: OpaqueHandle, record: SignedLocatorRecord, now: U64) -> NetIo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_trust_get(handle: OpaqueHandle, identity: LogicalIdentity, now: U64) -> NodeNativeRecord from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_trust_stats(handle: OpaqueHandle) -> NodeStoreStatistics from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_trust_export(handle: OpaqueHandle, secret: OpaqueHandle, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_trust_import(secret: OpaqueHandle, input: Ptr<U8>, count: USize, minimum_generation: U64, capacity: U32, memory: USize, now: U64) -> NetOpened from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_connection_challenge(local: LogicalIdentity, expected: LogicalIdentity, binding: Ptr<U8>, count: USize, timeout_ms: U32) -> NetOpened from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_connection_close(handle: OpaqueHandle) -> Unit from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_connection_write(handle: OpaqueHandle, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_connection_prove(handle: OpaqueHandle, record: SignedLocatorRecord, challenge: Ptr<U8>, count: USize, binding: Ptr<U8>, binding_count: USize, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_connection_check(challenge: NetOpened, trust: OpaqueHandle, input: Ptr<U8>, count: USize) -> NodeNativeConnection from "dlm.nodes.v1" guarantees(NoUnwind);

    extern "C" fn dlm_node_key_export(handle: OpaqueHandle, secret: OpaqueHandle, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_key_restore(secret: OpaqueHandle, input: Ptr<U8>, count: USize) -> NetOpened from "dlm.nodes.v1" guarantees(NoUnwind);

    unsafe fn node_key_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { unsafe { dlm_node_key_close(state.handle); } }
    unsafe fn node_signer_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { unsafe { dlm_node_signer_close(state.handle); } }
    unsafe fn node_trust_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { unsafe { dlm_node_trust_close(state.handle); } }
    unsafe fn node_challenge_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { unsafe { dlm_node_connection_close(state.handle); } }
    fn node_available() -> Bool { unsafe { return dlm_node_capabilities()==7; } }
    fn node_wall_seconds() -> U64 { unsafe { return dlm_node_wall_seconds(); } }
    fn node_record_result(value: NodeNativeRecord) -> NodeRecordResult guarantees(NoAlloc,NoBlocking,NoIO) { return NodeRecordResult(value.record,NetworkError(value.status,value.system,100)); }
    fn node_identity(root: NodePublicKey) -> NodeIdentityResult { unsafe { let value: NodeNativeIdentity=dlm_node_identity(root);return NodeIdentityResult(value.value,NetworkError(value.status,value.system,101)); } }
    fn node_key_generate() -> NodeKey { unsafe { return NodeKey(resource(dlm_node_key(zero<Ptr<U8>>(),0,true))); } }
    unsafe fn node_key_import(seed: Slice<U8>) -> NodeKey { let bytes: NetText=net_bytes(seed);return NodeKey(resource(dlm_node_key(bytes.data,bytes.count,false))); }
    fn node_key_error(key: &NodeKey) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { let state: &NetOpened=resource_get(&key.inner);return NetworkError(state.status,state.system,102); }
    fn node_signer_error(signer: &NodeSigner) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { let state: &NetOpened=resource_get(&signer.inner);return NetworkError(state.status,state.system,103); }
    fn node_trust_error(trust: &NodeTrust) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { let state: &NetOpened=resource_get(&trust.inner);return NetworkError(state.status,state.system,104); }
    fn node_challenge_error(challenge: &NodeChallenge) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { let state: &NetOpened=resource_get(&challenge.inner);return NetworkError(state.status,state.system,105); }
    fn node_key_close(key: NodeKey) -> Unit { resource_close(move key.inner); }
    fn node_signer_close(signer: NodeSigner) -> Unit { resource_close(move signer.inner); }
    fn node_trust_close(trust: NodeTrust) -> Unit { resource_close(move trust.inner); }
    fn node_challenge_close(challenge: NodeChallenge) -> Unit { resource_close(move challenge.inner); }
    fn node_public(key: &NodeKey) -> NodePublicResult { unsafe { let state: &NetOpened=resource_get(&key.inner);let value: NodeNativePublic=dlm_node_public(state.handle);return NodePublicResult(value.value,NetworkError(value.status,value.system,106)); } }
    fn node_sign(key: &NodeKey, input: Slice<U8>, output: &mut Buffer<U8>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&key.inner);let bytes: NetText=net_bytes(input);return socket_transfer(dlm_node_sign(state.handle,bytes.data,bytes.count,net_output(output),len(output)),107); } }
    fn node_verify(key: NodePublicKey, input: Slice<U8>, signature: Slice<U8>) -> NetworkError { unsafe { let bytes: NetText=net_bytes(input);let sig: NetText=net_bytes(signature);let r: NetIo=dlm_node_verify(key,bytes.data,bytes.count,sig.data,sig.count);return NetworkError(r.status,r.system,108); } }
    fn node_signer(root: &NodeKey, key: NodeKey, epoch: U64, first: U64, last: U64) -> NodeSigner { unsafe { let state: &NetOpened=resource_get(&root.inner);let current: NetOpened=resource_into_inner(move key.inner);if state.status!=0 || current.status!=0 { dlm_node_key_close(current.handle);let mut status: I32=state.status;if status==0 { status=current.status; }return NodeSigner(resource(NetOpened(zero<OpaqueHandle>(),status,0))); }return NodeSigner(resource(dlm_node_signer(state.handle,current.handle,epoch,first,last))); } }
    fn node_signer_resume(signer: &mut NodeSigner, record: SignedLocatorRecord, now: U64) -> NetworkError { unsafe { let state: &NetOpened=resource_get(&signer.inner);let r: NetIo=dlm_node_signer_resume(state.handle,record,now);return NetworkError(r.status,r.system,109); } }
    fn node_record(signer: &mut NodeSigner, locators: Slice<NetworkLocator>, now: U64, ttl: U32) -> NodeRecordResult { unsafe { let state: &NetOpened=resource_get(&signer.inner);let mut data: Ptr<NetworkLocator>=zero<Ptr<NetworkLocator>>();if len(locators)>0 { data=as_raw(borrowed_ptr(&locators[0])); }return node_record_result(dlm_node_record(state.handle,data,len(locators),now,ttl)); } }
    fn node_record_encode(record: SignedLocatorRecord, output: &mut Buffer<U8>) -> NetworkTransfer { unsafe { return socket_transfer(dlm_node_record_encode(record,net_output(output),len(output)),110); } }
    fn node_record_decode(input: Slice<U8>, now: U64) -> NodeRecordResult { unsafe { let bytes: NetText=net_bytes(input);return node_record_result(dlm_node_record_decode(bytes.data,bytes.count,now)); } }
    fn node_trust_new(capacity: U32, memory: USize) -> NodeTrust { unsafe { return NodeTrust(resource(dlm_node_trust_new(capacity,memory))); } }
    fn node_trust_pin(trust: &mut NodeTrust, root: NodePublicKey) -> NetworkError { unsafe { let state: &NetOpened=resource_get(&trust.inner);let r: NetIo=dlm_node_trust_pin(state.handle,root);return NetworkError(r.status,r.system,111); } }
    fn node_trust_accept(trust: &mut NodeTrust, record: SignedLocatorRecord, now: U64) -> NetworkError { unsafe { let state: &NetOpened=resource_get(&trust.inner);let r: NetIo=dlm_node_trust_accept(state.handle,record,now);return NetworkError(r.status,r.system,112); } }
    fn node_trust_get(trust: &NodeTrust, identity: LogicalIdentity, now: U64) -> NodeRecordResult { unsafe { let state: &NetOpened=resource_get(&trust.inner);return node_record_result(dlm_node_trust_get(state.handle,identity,now)); } }
    fn node_trust_statistics(trust: &NodeTrust) -> NodeStoreStatistics { unsafe { let state: &NetOpened=resource_get(&trust.inner);return dlm_node_trust_stats(state.handle); } }
    fn node_trust_export(trust: &NodeTrust, secret: &CryptoSecret, output: &mut Buffer<U8>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&trust.inner);let key: &NetOpened=resource_get(&secret.inner);return socket_transfer(dlm_node_trust_export(state.handle,key.handle,net_output(output),len(output)),113); } }
    // Keep minimum_generation in a separately trusted persistent store.
    fn node_trust_import(secret: &CryptoSecret, input: Slice<U8>, minimum_generation: U64, capacity: U32, memory: USize, now: U64) -> NodeTrust { unsafe { let key: &NetOpened=resource_get(&secret.inner);let bytes: NetText=net_bytes(input);return NodeTrust(resource(dlm_node_trust_import(key.handle,bytes.data,bytes.count,minimum_generation,capacity,memory,now))); } }
    fn node_connection_challenge(local: LogicalIdentity, expected: LogicalIdentity, binding: Slice<U8>, timeout_ms: U32) -> NodeChallenge { unsafe { let bytes: NetText=net_bytes(binding);return NodeChallenge(resource(dlm_node_connection_challenge(local,expected,bytes.data,bytes.count,timeout_ms))); } }
    fn node_connection_write(challenge: &NodeChallenge, output: &mut Buffer<U8>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&challenge.inner);return socket_transfer(dlm_node_connection_write(state.handle,net_output(output),len(output)),114); } }
    fn node_connection_prove(signer: &NodeSigner, record: SignedLocatorRecord, challenge: Slice<U8>, binding: Slice<U8>, output: &mut Buffer<U8>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&signer.inner);let bytes: NetText=net_bytes(challenge);let context: NetText=net_bytes(binding);return socket_transfer(dlm_node_connection_prove(state.handle,record,bytes.data,bytes.count,context.data,context.count,net_output(output),len(output)),115); } }
    fn node_connection_check(challenge: NodeChallenge, trust: &mut NodeTrust, input: Slice<U8>) -> ConnectionIdentity { unsafe { let state: &NetOpened=resource_get(&trust.inner);let bytes: NetText=net_bytes(input);let r: NodeNativeConnection=dlm_node_connection_check(resource_into_inner(move challenge.inner),state.handle,bytes.data,bytes.count);return ConnectionIdentity(r.local,r.peer,r.binding,r.key,r.epoch,r.sequence,NetworkError(r.status,r.system,116)); } }
    fn node_key_export(key: &NodeKey, secret: &CryptoSecret, output: &mut Buffer<U8>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&key.inner);let master: &NetOpened=resource_get(&secret.inner);return socket_transfer(dlm_node_key_export(state.handle,master.handle,net_output(output),len(output)),117); } }
    fn node_key_restore(secret: &CryptoSecret, input: Slice<U8>) -> NodeKey { unsafe { let master: &NetOpened=resource_get(&secret.inner);let bytes: NetText=net_bytes(input);return NodeKey(resource(dlm_node_key_restore(master.handle,bytes.data,bytes.count))); } }
}
