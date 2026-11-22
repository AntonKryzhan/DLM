module dpi.mwo_owner
import dpi.engine
import dpi.win32
native {
    trust "IP Helper";
    extern "C" fn GetExtendedTcpTable(table: Ptr<U8>, size: Ptr<U32>, sorted: I32, family: U32, class_: I32, reserved: U32) -> U32 from "IP Helper";
    extern "C" fn QueryFullProcessImageNameW(process: OpaqueHandle, flags: U32, name: Ptr<U16>, length: Ptr<U32>) -> I32 from "Win32";
    unsafe fn mwo_owner_pid(key: &FlowKey) -> U32 {
        let mut needed: U32=0;let mut family: U32=2;if key.version == U8(6) { family=23; }if GetExtendedTcpTable(null<U8>(),as_raw(borrowed_ptr(&mut needed)),0,family,5,0) != U32(122) || needed < U32(4) || needed > U32(8388608) { return 0; }
        let mut table: Buffer<U8>=buffer<U8>(cast<USize>(needed));if GetExtendedTcpTable(data(&mut table),as_raw(borrowed_ptr(&mut needed)),0,family,5,0) != U32(0) || cast<USize>(needed) > len(&table) { return 0; }return mwo_socket_pid(slice(&table,0,cast<USize>(needed)),key);
    }
    unsafe fn mwo_owner_kind(pid: U32) -> U32 {
        if pid == U32(0) { return 0; }let owner: Resource<OpaqueHandle,close_handle>=resource(OpenProcess(0x1000,0,pid));if !handle_valid(*resource_get(&owner)) { return 0; }let mut name: Buffer<U16>=buffer<U16>(32768);let mut n: U32=32768;
        if QueryFullProcessImageNameW(*resource_get(&owner),0,data(&mut name),as_raw(borrowed_ptr(&mut n))) == I32(0) || n >= U32(32768) { return 0; }let text: Buffer<U8>=utf8_wide(data(&name),cast<USize>(n));return mwo_process_kind(slice(&text,0,len(&text)));
    }
    unsafe fn mark_mwo_owner(e: &mut Engine, bytes: Slice<U8>, interface_: U32, subinterface: U32, now: U64) -> Unit {
        if !e.options.safe_automatic || !e.options.cloud_cdn { return; }let v: Packet=parse_packet(bytes);if !v.valid || !v.transformable || v.protocol != U8(6) || v.payload_length < 3 || bytes[v.payload] != U8(22) || bytes[v.payload+1] != U8(3) || bytes[v.payload+2] > U8(4) || !automatic_public(&v.destination,v.version) || cloud_protected_ip(&v.destination,v.version) { return; }
        let key: FlowKey=FlowKey(v.source,v.destination,interface_,subinterface,v.source_port,v.destination_port,v.version,v.protocol);let mut i: USize=flow_find(&mut e.flows,&key,now);if i < len(&e.flows) && (e.flows[i].arm_chosen || e.flows[i].complete || e.flows[i].stopped || e.flows[i].mwo_owned) { return; }
        let h: Hello=inspect_tls(slice(&bytes,v.payload,v.payload_length));if automatic_protected(&h.host) || gene_applies(&h.host,4) || mwo_host(&h.host) || cloud_host_family(&h.host) != U32(0) { return; }let p: U32=cloud_provider(&e.policy,&h.host,&v.destination,v.version);if p >= U32(1) && p <= U32(3) { return; }
        let pid: U32=mwo_owner_pid(&key);let kind: U32=mwo_owner_kind(pid);if kind == U32(0) { return; }if i == len(&e.flows) { i=engine_create_flow(e,&key,now);e.flows[i].base=v.sequence;e.flows[i].started=now; }e.flows[i].mwo_owned=true;e.flows[i].mwo_pid=pid;trace_event(e,29,now,key,h.host,0,0,cast<U64>(pid),cast<U64>(kind),0);
    }
}
