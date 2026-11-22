module std.net_discovery
import std.net_content

native {
    trust "dlm.nodes.v1";
    record DiscoveryNetwork repr(C, version=1) { bytes: Array<U8,32>; }
    record DiscoveryBounds repr(C, version=1) { peers: U32; bucket: U32; parallelism: U32; requests_per_second: U32; record_ttl: U32; memory: USize; }
    record DiscoveryStatistics repr(C, version=1) { sent: U64; received: U64; rejected: U64; timeouts: U64; used: USize; limit: USize; peers: U32; pins: U32; status: I32; system: I32; }
    record NodeDiscovery repr(C) { inner: Resource<NetOpened,discovery_dispose>; }
    extern "C" fn dlm_node_discovery_new(signer: NetOpened, trust: NetOpened, cache: NetOpened, group: NetOpened, local: NetAddress, network: DiscoveryNetwork, ttl: U32, peers: U32, bucket: U32, parallelism: U32, rate: U32, memory: USize) -> NetOpened from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_discovery_close(handle: OpaqueHandle) -> Unit from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_discovery_record(handle: OpaqueHandle) -> NodeNativeRecord from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_discovery_pin(handle: OpaqueHandle, root: NodePublicKey) -> NetIo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_discovery_bootstrap(handle: OpaqueHandle, record: SignedLocatorRecord) -> NetIo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_discovery_poll(handle: OpaqueHandle, timeout_ms: U32, cancel: OpaqueHandle) -> NetIo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_discovery_resolve(handle: OpaqueHandle, target: LogicalIdentity, timeout_ms: U32, cancel: OpaqueHandle) -> NodeNativeRecord from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_discovery_fetch(handle: OpaqueHandle, peer: LogicalIdentity, id: ContentId, timeout_ms: U32, cancel: OpaqueHandle) -> ContentNativeInfo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_discovery_materialize(handle: OpaqueHandle, peer: LogicalIdentity, id: ContentId, timeout_ms: U32, cancel: OpaqueHandle) -> ContentNativeInfo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_discovery_publish(handle: OpaqueHandle, input: Ptr<U8>, count: USize, chunk: U32) -> ContentNativeInfo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_discovery_read(handle: OpaqueHandle, id: ContentId, offset: U64, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_discovery_stats(handle: OpaqueHandle) -> DiscoveryStatistics from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_discovery_trust_export(handle: OpaqueHandle, secret: OpaqueHandle, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_discovery_get(handle: OpaqueHandle, id: ContentId, timeout_ms: U32, cancel: OpaqueHandle) -> ContentNativeInfo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_discovery_advertise(handle: OpaqueHandle, locators: Ptr<NetworkLocator>, count: USize) -> NodeNativeRecord from "dlm.nodes.v1" guarantees(NoUnwind);
    unsafe fn discovery_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { unsafe { dlm_node_discovery_close(state.handle); } }
    fn discovery_open(signer: NodeSigner, trust: NodeTrust, cache: ContentCache, group: CryptoSecret, local: SocketAddress, network: DiscoveryNetwork, bounds: DiscoveryBounds) -> NodeDiscovery { unsafe { return NodeDiscovery(resource(dlm_node_discovery_new(resource_into_inner(move signer.inner),resource_into_inner(move trust.inner),resource_into_inner(move cache.inner),resource_into_inner(move group.inner),local.value,network,bounds.record_ttl,bounds.peers,bounds.bucket,bounds.parallelism,bounds.requests_per_second,bounds.memory))); } }
    fn discovery_error(node: &NodeDiscovery) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { let state: &NetOpened=resource_get(&node.inner);return NetworkError(state.status,state.system,130); }
    fn discovery_close(node: NodeDiscovery) -> Unit { resource_close(move node.inner); }
    fn discovery_record(node: &mut NodeDiscovery) -> NodeRecordResult { unsafe { let state: &NetOpened=resource_get(&node.inner);return node_record_result(dlm_node_discovery_record(state.handle)); } }
    fn discovery_pin(node: &mut NodeDiscovery, root: NodePublicKey) -> NetworkError { unsafe { let state: &NetOpened=resource_get(&node.inner);let r: NetIo=dlm_node_discovery_pin(state.handle,root);return NetworkError(r.status,r.system,131); } }
    fn discovery_bootstrap(node: &mut NodeDiscovery, record: SignedLocatorRecord) -> NetworkError { unsafe { let state: &NetOpened=resource_get(&node.inner);let r: NetIo=dlm_node_discovery_bootstrap(state.handle,record);return NetworkError(r.status,r.system,132); } }
    fn discovery_poll(node: &mut NodeDiscovery, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&node.inner);let token: &OpaqueHandle=resource_get(cancel);return socket_transfer(dlm_node_discovery_poll(state.handle,timeout_ms,*token),133); } }
    fn discovery_resolve(node: &mut NodeDiscovery, target: LogicalIdentity, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NodeRecordResult { unsafe { let state: &NetOpened=resource_get(&node.inner);let token: &OpaqueHandle=resource_get(cancel);return node_record_result(dlm_node_discovery_resolve(state.handle,target,timeout_ms,*token)); } }
    fn discovery_fetch(node: &mut NodeDiscovery, peer: LogicalIdentity, id: ContentId, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> ContentResult { unsafe { let state: &NetOpened=resource_get(&node.inner);let token: &OpaqueHandle=resource_get(cancel);return content_result(dlm_node_discovery_fetch(state.handle,peer,id,timeout_ms,*token)); } }
    fn discovery_materialize(node: &mut NodeDiscovery, peer: LogicalIdentity, id: ContentId, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> ContentResult { unsafe { let state: &NetOpened=resource_get(&node.inner);let token: &OpaqueHandle=resource_get(cancel);return content_result(dlm_node_discovery_materialize(state.handle,peer,id,timeout_ms,*token)); } }
    fn discovery_publish(node: &mut NodeDiscovery, input: Slice<U8>, chunk: U32) -> ContentResult { unsafe { let state: &NetOpened=resource_get(&node.inner);let bytes: NetText=net_bytes(input);return content_result(dlm_node_discovery_publish(state.handle,bytes.data,bytes.count,chunk)); } }
    fn discovery_read(node: &mut NodeDiscovery, id: ContentId, offset: U64, output: &mut Buffer<U8>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&node.inner);return socket_transfer(dlm_node_discovery_read(state.handle,id,offset,net_output(output),len(output)),134); } }
    fn discovery_statistics(node: &NodeDiscovery) -> DiscoveryStatistics { unsafe { let state: &NetOpened=resource_get(&node.inner);return dlm_node_discovery_stats(state.handle); } }
    fn discovery_trust_export(node: &NodeDiscovery, secret: &CryptoSecret, output: &mut Buffer<U8>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&node.inner);let key: &NetOpened=resource_get(&secret.inner);return socket_transfer(dlm_node_discovery_trust_export(state.handle,key.handle,net_output(output),len(output)),135); } }
    fn discovery_get(node: &mut NodeDiscovery, id: ContentId, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> ContentResult { unsafe { let state: &NetOpened=resource_get(&node.inner);let token: &OpaqueHandle=resource_get(cancel);return content_result(dlm_node_discovery_get(state.handle,id,timeout_ms,*token)); } }
    fn discovery_advertise(node: &mut NodeDiscovery, locators: Slice<NetworkLocator>) -> NodeRecordResult { unsafe { let state: &NetOpened=resource_get(&node.inner);let mut data: Ptr<NetworkLocator>=zero<Ptr<NetworkLocator>>();if len(locators)>0 { data=as_raw(borrowed_ptr(&locators[0])); }return node_record_result(dlm_node_discovery_advertise(state.handle,data,len(locators))); } }
}
