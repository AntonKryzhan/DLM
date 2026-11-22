module std.net_tun
import std.net_adaptive

native {
    trust "dlm.edge.v1";
    record TunConfig repr(C, version=1) { kind: U32; mode: U32; mtu: U32; max_packet: U32; ring_capacity: U32; memory: USize; configure: Bool; ipv4: NetAddress; ipv6: NetAddress; ipv4_prefix: U32; ipv6_prefix: U32; }
    record TunNativePacket repr(C, version=1) { required: USize; copied: USize; family: U32; ether_type: U32; offset: U32; status: I32; system: I32; }
    record TunPacket repr(C) { required: USize; copied: USize; family: U32; ether_type: U32; offset: U32; error: NetworkError; }
    record TunStatistics repr(C, version=1) { received: U64; sent: U64; dropped: U64; malformed: U64; used: USize; limit: USize; pending: USize; backend: U32; kind: U32; mtu: U32; index: U32; status: I32; system: I32; }
    record TunBridgeStatistics repr(C, version=1) { outgoing: U64; incoming: U64; rejected: U64; blocked: U64; pending: USize; used: USize; authenticated: U32; status: I32; system: I32; }
    record TunAuthority repr(C) { inner: Resource<NetOpened,tun_authority_dispose>; }
    record TunDevice repr(C) { inner: Resource<NetOpened,tun_dispose>; }
    record TunBridge repr(C) { inner: Resource<NetOpened,tun_bridge_dispose>; }
    extern "C" fn dlm_tun_permission(flags: U32) -> NetOpened from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_tun_permission_close(handle: OpaqueHandle) -> Unit from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_tun_open(authority: NetOpened, name: NetText, library: NetText, config: TunConfig) -> NetOpened from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_tun_adopt(descriptor: I64, config: TunConfig) -> NetOpened from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_tun_close(handle: OpaqueHandle) -> Unit from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_tun_peek(handle: OpaqueHandle) -> TunNativePacket from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_tun_receive(handle: OpaqueHandle, output: Ptr<U8>, capacity: USize) -> TunNativePacket from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_tun_send(handle: OpaqueHandle, input: Ptr<U8>, count: USize) -> NetIo from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_tun_discard(handle: OpaqueHandle) -> NetIo from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_tun_wait(handle: OpaqueHandle, timeout_ms: U32, cancel: OpaqueHandle) -> NetIo from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_tun_stats(handle: OpaqueHandle) -> TunStatistics from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_tun_name(handle: OpaqueHandle, output: Ptr<U8>, capacity: USize) -> NetIo from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_tun_packet(kind: U32, input: Ptr<U8>, count: USize) -> TunNativePacket from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_tun_bridge_new(device: NetOpened, adaptive: NetOpened, channel: U32) -> NetOpened from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_tun_bridge_close(handle: OpaqueHandle) -> Unit from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_tun_bridge_poll(handle: OpaqueHandle, timeout_ms: U32, cancel: OpaqueHandle) -> NetIo from "dlm.edge.v1" guarantees(NoUnwind);
    extern "C" fn dlm_tun_bridge_stats(handle: OpaqueHandle) -> TunBridgeStatistics from "dlm.edge.v1" guarantees(NoUnwind);
    unsafe fn tun_authority_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { dlm_tun_permission_close(state.handle); }
    unsafe fn tun_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { dlm_tun_close(state.handle); }
    unsafe fn tun_bridge_dispose(state: &mut NetOpened) -> Unit guarantees(NoUnwind) { dlm_tun_bridge_close(state.handle); }
    fn tun_config() -> TunConfig guarantees(NoAlloc,NoBlocking,NoIO) { return TunConfig(1,1,1500,65535,4194304,1048576,false,zero<NetAddress>(),zero<NetAddress>(),0,0); }
    unsafe fn tun_authorize(configure: Bool) -> TunAuthority { let mut flags: U32=1;if configure { flags=3; }return TunAuthority(resource(dlm_tun_permission(flags))); }
    fn tun_open(authority: TunAuthority, name: Slice<U8>, config: TunConfig) -> TunDevice { unsafe { return TunDevice(resource(dlm_tun_open(resource_into_inner(move authority.inner),net_bytes(name),zero<NetText>(),config))); } }
    unsafe fn tun_open_with_library(authority: TunAuthority, name: Slice<U8>, library: Slice<U8>, config: TunConfig) -> TunDevice { return TunDevice(resource(dlm_tun_open(resource_into_inner(move authority.inner),net_bytes(name),net_bytes(library),config))); }
    unsafe fn tun_adopt_packet_fd(descriptor: I64, config: TunConfig) -> TunDevice { return TunDevice(resource(dlm_tun_adopt(descriptor,config))); }
    fn tun_error(device: &TunDevice) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { let state: &NetOpened=resource_get(&device.inner);return NetworkError(state.status,state.system,150); }
    fn tun_close(device: TunDevice) -> Unit { resource_close(move device.inner); }
    fn tun_packet_result(r: TunNativePacket) -> TunPacket guarantees(NoAlloc,NoBlocking,NoIO) { return TunPacket(r.required,r.copied,r.family,r.ether_type,r.offset,NetworkError(r.status,r.system,151)); }
    fn tun_peek(device: &mut TunDevice) -> TunPacket { unsafe { let state: &NetOpened=resource_get(&device.inner);return tun_packet_result(dlm_tun_peek(state.handle)); } }
    fn tun_receive(device: &mut TunDevice, output: &mut Buffer<U8>) -> TunPacket { unsafe { let state: &NetOpened=resource_get(&device.inner);return tun_packet_result(dlm_tun_receive(state.handle,net_output(output),len(output))); } }
    fn tun_send(device: &mut TunDevice, input: Slice<U8>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&device.inner);let bytes: NetText=net_bytes(input);return socket_transfer(dlm_tun_send(state.handle,bytes.data,bytes.count),152); } }
    fn tun_discard(device: &mut TunDevice) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&device.inner);return socket_transfer(dlm_tun_discard(state.handle),153); } }
    fn tun_wait(device: &mut TunDevice, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&device.inner);return socket_transfer(dlm_tun_wait(state.handle,timeout_ms,*resource_get(cancel)),154); } }
    fn tun_statistics(device: &TunDevice) -> TunStatistics { unsafe { let state: &NetOpened=resource_get(&device.inner);return dlm_tun_stats(state.handle); } }
    fn tun_name(device: &TunDevice, output: &mut Buffer<U8>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&device.inner);return socket_transfer(dlm_tun_name(state.handle,net_output(output),len(output)),155); } }
    fn tun_validate(kind: U32, input: Slice<U8>) -> TunPacket { unsafe { let bytes: NetText=net_bytes(input);return tun_packet_result(dlm_tun_packet(kind,bytes.data,bytes.count)); } }
    fn tun_bridge(device: TunDevice, adaptive: AdaptiveSession, channel: U32) -> TunBridge { unsafe { return TunBridge(resource(dlm_tun_bridge_new(resource_into_inner(move device.inner),resource_into_inner(move adaptive.inner),channel))); } }
    fn tun_bridge_close(bridge: TunBridge) -> Unit { resource_close(move bridge.inner); }
    fn tun_bridge_error(bridge: &TunBridge) -> NetworkError guarantees(NoAlloc,NoBlocking,NoIO) { let state: &NetOpened=resource_get(&bridge.inner);return NetworkError(state.status,state.system,156); }
    fn tun_bridge_poll(bridge: &mut TunBridge, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetworkTransfer { unsafe { let state: &NetOpened=resource_get(&bridge.inner);return socket_transfer(dlm_tun_bridge_poll(state.handle,timeout_ms,*resource_get(cancel)),157); } }
    fn tun_bridge_statistics(bridge: &TunBridge) -> TunBridgeStatistics { unsafe { let state: &NetOpened=resource_get(&bridge.inner);return dlm_tun_bridge_stats(state.handle); } }
}
