module std.net_crypto
import std.net_socket

native {
    trust "dlm.net.v1";
    record CryptoSecret repr(C) { inner: Resource<NetOpened,crypto_dispose>; }
    record CryptoPrivate repr(C) { inner: Resource<NetOpened,crypto_dispose>; }
    record CryptoChannel repr(C) { inner: Resource<NetOpened,crypto_dispose>; }
    record CryptoRole repr(C) { value: U32; }
    record CryptoNativeStatistics repr(C, version=1) { sent: U64; received: U64; send_bytes: U64; receive_bytes: U64; record_limit: U64; byte_limit: U64; deadline_ms: U64; failed_auth: U32; status: I32; system: I32; }
    record CryptoStatistics repr(C) { sent: U64; received: U64; send_bytes: U64; receive_bytes: U64; record_limit: U64; byte_limit: U64; deadline_ms: U64; failed_auth: U32; error: NetworkError; }

    extern "C" fn dlm_net_crypto_capabilities() -> U32 from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_crypto_shutdown() -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_crypto_close(handle: OpaqueHandle) -> Unit from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_crypto_secret(bytes: Ptr<U8>, count: USize, kind: U32, random: Bool) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_crypto_hash(key: OpaqueHandle, input: Ptr<U8>, count: USize, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_crypto_hmac(key: OpaqueHandle, input: Ptr<U8>, count: USize, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_crypto_public(key: OpaqueHandle, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_crypto_agree(key: NetOpened, peer: Ptr<U8>, count: USize) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_crypto_hkdf(key: NetOpened, salt: Ptr<U8>, salt_count: USize, info: Ptr<U8>, info_count: USize, count: USize) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_crypto_channel(key: NetOpened, salt: Ptr<U8>, salt_count: USize, context: Ptr<U8>, context_count: USize, role: U32, records: U64, bytes: U64, lifetime_ms: U32) -> NetOpened from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_crypto_seal(channel: OpaqueHandle, input: Ptr<U8>, count: USize, aad: Ptr<U8>, aad_count: USize, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_crypto_open(channel: OpaqueHandle, record: Ptr<U8>, count: USize, aad: Ptr<U8>, aad_count: USize, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.net.v1" guarantees(NoUnwind);
    extern "C" fn dlm_net_crypto_stats(channel: OpaqueHandle) -> CryptoNativeStatistics from "dlm.net.v1" guarantees(NoUnwind);

    fn crypto_dispose(state: &mut NetOpened) -> Unit { unsafe { dlm_net_crypto_close(state.handle); } }
    fn crypto_available() -> Bool { unsafe { return dlm_net_crypto_capabilities()==15; } }
    fn crypto_initiator() -> CryptoRole guarantees(NoAlloc,NoBlocking,NoIO) { return CryptoRole(1); }
    fn crypto_responder() -> CryptoRole guarantees(NoAlloc,NoBlocking,NoIO) { return CryptoRole(2); }
    fn crypto_secret_error(secret: &CryptoSecret) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) {
        let state: &NetOpened=resource_get(&secret.inner);return NetworkError(state.status,state.system,60);
    }
    fn crypto_private_error(key: &CryptoPrivate) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) {
        let state: &NetOpened=resource_get(&key.inner);return NetworkError(state.status,state.system,61);
    }
    fn crypto_channel_error(channel: &CryptoChannel) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) {
        let state: &NetOpened=resource_get(&channel.inner);return NetworkError(state.status,state.system,62);
    }
    fn crypto_secret_close(secret: CryptoSecret) -> Unit { resource_close(move secret.inner); }
    fn crypto_private_close(key: CryptoPrivate) -> Unit { resource_close(move key.inner); }
    fn crypto_channel_close(channel: CryptoChannel) -> Unit { resource_close(move channel.inner); }
    fn crypto_private_generate() -> CryptoPrivate {
        unsafe { return CryptoPrivate(resource(dlm_net_crypto_secret(zero<Ptr<U8>>(),32,2,true))); }
    }
    fn crypto_secret_generate(count: USize) -> CryptoSecret {
        unsafe { return CryptoSecret(resource(dlm_net_crypto_secret(zero<Ptr<U8>>(),count,1,true))); }
    }
    // Explicit imports cannot establish entropy or uniqueness of caller keys.
    unsafe fn crypto_secret_import(bytes: Slice<U8>) -> CryptoSecret {
        let value: NetText=net_bytes(bytes);return CryptoSecret(resource(dlm_net_crypto_secret(value.data,value.count,1,false)));
    }
    unsafe fn crypto_private_import(bytes: Slice<U8>) -> CryptoPrivate {
        let value: NetText=net_bytes(bytes);return CryptoPrivate(resource(dlm_net_crypto_secret(value.data,value.count,2,false)));
    }
    fn crypto_public(key: &CryptoPrivate, output: &mut Buffer<U8>) -> NetworkTransfer {
        unsafe { let state: &NetOpened=resource_get(&key.inner);return socket_transfer(dlm_net_crypto_public(state.handle,net_output(output),len(output)),63); }
    }
    fn crypto_agree(key: CryptoPrivate, peer: Slice<U8>) -> CryptoSecret {
        unsafe { let value: NetText=net_bytes(peer);return CryptoSecret(resource(dlm_net_crypto_agree(resource_into_inner(move key.inner),value.data,value.count))); }
    }
    fn crypto_hkdf(secret: CryptoSecret, salt: Slice<U8>, info: Slice<U8>, count: USize) -> CryptoSecret {
        unsafe { let s: NetText=net_bytes(salt);let i: NetText=net_bytes(info);return CryptoSecret(resource(dlm_net_crypto_hkdf(resource_into_inner(move secret.inner),s.data,s.count,i.data,i.count,count))); }
    }
    fn crypto_sha256(input: Slice<U8>, output: &mut Buffer<U8>) -> NetworkTransfer {
        unsafe { let value: NetText=net_bytes(input);return socket_transfer(dlm_net_crypto_hash(zero<OpaqueHandle>(),value.data,value.count,net_output(output),len(output)),64); }
    }
    fn crypto_hmac(secret: &CryptoSecret, input: Slice<U8>, output: &mut Buffer<U8>) -> NetworkTransfer {
        unsafe { let state: &NetOpened=resource_get(&secret.inner);let value: NetText=net_bytes(input);return socket_transfer(dlm_net_crypto_hmac(state.handle,value.data,value.count,net_output(output),len(output)),65); }
    }
    fn crypto_channel_limited(secret: CryptoSecret, salt: Slice<U8>, context: Slice<U8>, role: CryptoRole, records: U64, bytes: U64, lifetime_ms: U32) -> CryptoChannel {
        unsafe {
            let s: NetText=net_bytes(salt);let c: NetText=net_bytes(context);
            return CryptoChannel(resource(dlm_net_crypto_channel(resource_into_inner(move secret.inner),s.data,s.count,c.data,c.count,role.value,records,bytes,lifetime_ms)));
        }
    }
    fn crypto_channel_open(secret: CryptoSecret, salt: Slice<U8>, context: Slice<U8>, role: CryptoRole) -> CryptoChannel {
        return crypto_channel_limited(move secret,salt,context,role,1048576,1073741824,3600000);
    }
    fn crypto_seal(channel: &mut CryptoChannel, input: Slice<U8>, aad: Slice<U8>, output: &mut Buffer<U8>) -> NetworkTransfer {
        unsafe {
            let state: &NetOpened=resource_get(&channel.inner);let bytes: NetText=net_bytes(input);let auth: NetText=net_bytes(aad);
            return socket_transfer(dlm_net_crypto_seal(state.handle,bytes.data,bytes.count,auth.data,auth.count,net_output(output),len(output)),66);
        }
    }
    fn crypto_open(channel: &mut CryptoChannel, record: Slice<U8>, aad: Slice<U8>, output: &mut Buffer<U8>) -> NetworkTransfer {
        unsafe {
            let state: &NetOpened=resource_get(&channel.inner);let bytes: NetText=net_bytes(record);let auth: NetText=net_bytes(aad);
            return socket_transfer(dlm_net_crypto_open(state.handle,bytes.data,bytes.count,auth.data,auth.count,net_output(output),len(output)),67);
        }
    }
    fn crypto_statistics(channel: &CryptoChannel) -> CryptoStatistics {
        unsafe {
            let state: &NetOpened=resource_get(&channel.inner);let value: CryptoNativeStatistics=dlm_net_crypto_stats(state.handle);
            return CryptoStatistics(value.sent,value.received,value.send_bytes,value.receive_bytes,value.record_limit,value.byte_limit,value.deadline_ms,value.failed_auth,NetworkError(value.status,value.system,68));
        }
    }
    fn crypto_auth_failed(error: &NetworkError) -> Bool guarantees(NoAlloc,NoBlocking,NoIO) { return error.kind==12; }
    fn crypto_replayed(error: &NetworkError) -> Bool guarantees(NoAlloc,NoBlocking,NoIO) { return error.kind==13; }
    fn crypto_expired(error: &NetworkError) -> Bool guarantees(NoAlloc,NoBlocking,NoIO) { return error.kind==14; }
    fn crypto_exhausted(error: &NetworkError) -> Bool guarantees(NoAlloc,NoBlocking,NoIO) { return error.kind==15; }
    fn crypto_error_name(error: &NetworkError) -> Utf8 guarantees(NoAlloc,NoBlocking,NoIO) {
        if error.kind==12 { return "authentication_failed"; }
        if error.kind==13 { return "replayed_or_too_old"; }
        if error.kind==14 { return "key_expired"; }
        if error.kind==15 { return "key_or_authentication_budget_exhausted"; }
        return network_error_name(error);
    }
    unsafe fn crypto_provider_shutdown() -> NetIo { return dlm_net_crypto_shutdown(); }
}
