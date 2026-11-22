module std.net_content
import std.net_identity

native {
    trust "dlm.nodes.v1";
    record ContentId repr(C, version=1) { bytes: Array<U8,32>; }
    record ContentChild repr(C, version=1) { id: ContentId; size: U64; leaves: U32; reserved: U32; }
    record MerkleProof repr(C, version=1) { root: ContentId; leaf: ContentId; total_size: U64; leaf_size: U64; index: U32; leaf_count: U32; depth: U32; reserved: U32; siblings: Array<ContentChild,32>; sides: Array<U32,32>; }
    record ContentNativeProof repr(C, version=1) { proof: MerkleProof; status: I32; system: I32; }
    record ContentProofResult repr(C) { proof: MerkleProof; error: NetworkError; }
    record ContentNativeInfo repr(C, version=1) { id: ContentId; size: U64; leaves: U32; kind: U32; status: I32; system: I32; }
    record ContentResult repr(C) { id: ContentId; size: U64; leaves: U32; kind: U32; error: NetworkError; }
    record ContentCache repr(C) { inner: Resource<NetOpened,content_dispose>; }
    extern "C" fn dlm_node_content_new(entries: U32, memory: USize) -> NetOpened from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_content_close(handle: OpaqueHandle) -> Unit from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_content_import(handle: OpaqueHandle, expected: ContentId, input: Ptr<U8>, count: USize) -> ContentNativeInfo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_content_put(handle: OpaqueHandle, input: Ptr<U8>, count: USize) -> ContentNativeInfo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_content_tree(handle: OpaqueHandle, input: Ptr<U8>, count: USize, chunk: U32) -> ContentNativeInfo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_content_info(handle: OpaqueHandle, id: ContentId) -> ContentNativeInfo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_content_export(handle: OpaqueHandle, id: ContentId, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_content_read(handle: OpaqueHandle, id: ContentId, offset: U64, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_content_proof(handle: OpaqueHandle, root: ContentId, index: U32) -> ContentNativeProof from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_content_verify(root: ContentId, proof: MerkleProof, input: Ptr<U8>, count: USize) -> NetIo from "dlm.nodes.v1" guarantees(NoUnwind);
    extern "C" fn dlm_node_content_stats(handle: OpaqueHandle) -> NodeStoreStatistics from "dlm.nodes.v1" guarantees(NoUnwind);
    unsafe fn content_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { unsafe { dlm_node_content_close(state.handle); } }
    fn content_result(value: ContentNativeInfo) -> ContentResult guarantees(NoAlloc,NoBlocking,NoIO) { return ContentResult(value.id,value.size,value.leaves,value.kind,NetworkError(value.status,value.system,120)); }
    fn content_cache(entries: U32, memory: USize) -> ContentCache { unsafe { return ContentCache(resource(dlm_node_content_new(entries,memory))); } }
    fn content_error(cache: &ContentCache) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { let state: &NetOpened=resource_get(&cache.inner);return NetworkError(state.status,state.system,121); }
    fn content_close(cache: ContentCache) -> Unit { resource_close(move cache.inner); }
    fn content_put(cache: &mut ContentCache, input: Slice<U8>) -> ContentResult { unsafe { let state: &NetOpened=resource_get(&cache.inner);let bytes: NetText=net_bytes(input);return content_result(dlm_node_content_put(state.handle,bytes.data,bytes.count)); } }
    fn content_tree(cache: &mut ContentCache, input: Slice<U8>, chunk: U32) -> ContentResult { unsafe { let state: &NetOpened=resource_get(&cache.inner);let bytes: NetText=net_bytes(input);return content_result(dlm_node_content_tree(state.handle,bytes.data,bytes.count,chunk)); } }
    fn content_import(cache: &mut ContentCache, expected: ContentId, object: Slice<U8>) -> ContentResult { unsafe { let state: &NetOpened=resource_get(&cache.inner);let bytes: NetText=net_bytes(object);return content_result(dlm_node_content_import(state.handle,expected,bytes.data,bytes.count)); } }
    fn content_info(cache: &mut ContentCache, id: ContentId) -> ContentResult { unsafe { let state: &NetOpened=resource_get(&cache.inner);return content_result(dlm_node_content_info(state.handle,id)); } }
    fn content_export(cache: &mut ContentCache, id: ContentId, output: &mut Buffer<U8>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&cache.inner);return socket_transfer(dlm_node_content_export(state.handle,id,net_output(output),len(output)),122); } }
    fn content_read(cache: &mut ContentCache, id: ContentId, offset: U64, output: &mut Buffer<U8>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&cache.inner);return socket_transfer(dlm_node_content_read(state.handle,id,offset,net_output(output),len(output)),123); } }
    fn content_proof(cache: &mut ContentCache, root: ContentId, index: U32) -> ContentProofResult { unsafe { let state: &NetOpened=resource_get(&cache.inner);let r: ContentNativeProof=dlm_node_content_proof(state.handle,root,index);return ContentProofResult(r.proof,NetworkError(r.status,r.system,124)); } }
    fn content_verify(root: ContentId, proof: MerkleProof, input: Slice<U8>) -> NetworkError { unsafe { let bytes: NetText=net_bytes(input);let r: NetIo=dlm_node_content_verify(root,proof,bytes.data,bytes.count);return NetworkError(r.status,r.system,125); } }
    fn content_statistics(cache: &ContentCache) -> NodeStoreStatistics { unsafe { let state: &NetOpened=resource_get(&cache.inner);return dlm_node_content_stats(state.handle); } }
}
