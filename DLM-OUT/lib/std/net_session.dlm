module std.net_session
import std.net_session_machine
import std.net_transport
native {
    record ProtocolOwned repr(C) { transport: TransportSession; spec: NetworkSpec; machine: ProtocolMachine; minor: U32; control: ProtocolControl; system: I32; }
    record ProtocolSession repr(C) { inner: Resource<ProtocolOwned,protocol_dispose>; }
    record ProtocolStatistics repr(C) { state: U32; phase: U32; authenticated: Bool; sent: U64; received: U64; pending: U32; terminal: Bool; error: NetworkError; }
    fn protocol_release(owned: &mut ProtocolOwned) -> Unit {
        unsafe { let opened: &mut NetOpened=resource_get_mut(&mut owned.transport.inner);if opened.handle!=zero<OpaqueHandle>() { dlm_net_transport_close(opened.handle);opened.handle=zero<OpaqueHandle>();opened.status=2;opened.system=0; } }owned.control.resources=0;owned.control.authenticated=false;
    }
    fn protocol_dispose(owned: &mut ProtocolOwned) -> Unit { protocol_release(owned); }
    fn protocol_apply(owned: &mut ProtocolOwned, event: U32, message: U32) -> I32 {
        let step: ProtocolStep=protocol_transition(&owned.machine,owned.control.state,event,message,owned.control.role);if step.status!=0 { return step.status; }owned.control.state=step.state;owned.control.resources=step.resources;owned.control.authenticated=step.authenticated;let found: USize=protocol_state(&owned.machine,step.state);if found<cast<USize>(owned.machine.state_count) { if owned.machine.states[found].terminal { protocol_release(owned); } }return 0;
    }
    fn protocol_stop(owned: &mut ProtocolOwned, status: I32, system: I32) -> I32 {
        let mut event: U32=9;if status==3 { event=8; }if status==9 { event=10; }let ignored: I32=protocol_apply(owned,event,0);owned.control.status=status;owned.system=system;protocol_release(owned);return status;
    }
    fn protocol_valid_control(owned: &ProtocolOwned) -> Bool guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if owned.machine.state_count>32 || owned.machine.transition_count>256 || (owned.control.role!=1 && owned.control.role!=2) || owned.control.sent>owned.spec.resources.maximum_session_messages || owned.control.received>owned.spec.resources.maximum_session_messages-owned.control.sent { return false; }
        let found: USize=protocol_state(&owned.machine,owned.control.state);if found==cast<USize>(owned.machine.state_count) { return false; }let state: ProtocolState=owned.machine.states[found];return state.resources==owned.control.resources && state.authenticated==owned.control.authenticated;
    }
    fn protocol_guard(owned: &mut ProtocolOwned, now: U64) -> I32 {
        if owned.control.status!=0 { return owned.control.status; }if !protocol_valid_control(owned) { return protocol_stop(owned,4,0); }
        let state: ProtocolState=owned.machine.states[protocol_state(&owned.machine,owned.control.state)];if state.terminal { protocol_release(owned);return 2; }
        let checked: I32=protocol_clock(&owned.control,owned.spec.session,now,state.phase);if checked!=0 { return protocol_stop(owned,checked,0); }owned.control.observed=now;return 0;
    }
    fn protocol_adopt(transport: TransportSession, spec: NetworkSpec, machine: ProtocolMachine, minor: U32, initiator: Bool) -> ProtocolSession {
        let mut role: U32=1;if !initiator { role=2; }let now: U64=network_clock_ms();let control: ProtocolControl=ProtocolControl(machine.initial,role,0,false,0,0,now,now,now,0);let mut owned: ProtocolOwned=ProtocolOwned(move transport,spec,machine,minor,control,0);
        let check: ProtocolCheck=protocol_machine_check(&owned.spec,&owned.machine,minor);let schema: NetworkSpecCheck=network_spec_check(&owned.spec);let mut error: I32=check.status;if schema.status!=0 { error=schema.status; }
        if error==0 {
            if owned.spec.proofs.assurance!=1 || schema.unsupported_security!=0 { error=6; }
            for index in 0..cast<USize>(owned.machine.state_count) { if owned.machine.states[index].resources>1 { error=6; } }
            if error==0 { let opened: NetworkError=transport_error(&owned.transport);if opened.kind!=0 { error=opened.kind;owned.system=opened.system; }else { let stats: TransportStatistics=transport_statistics(&owned.transport);if stats.error.kind!=0 { error=stats.error.kind;owned.system=stats.error.system; }else { let compatible: TransportCheck=transport_check(stats.profile,TransportRequirements(schema.required_properties,owned.spec.transport.forbidden,0),owned.spec.resources.transport);if compatible.status!=0 { error=compatible.status; } } } }
        }
        if error==0 { error=protocol_apply(&mut owned,1,0); }if error!=0 { owned.control.status=error;protocol_release(&mut owned); }return ProtocolSession(resource(move owned));
    }
    fn protocol_open(spec: NetworkSpec, machine: ProtocolMachine, minor: U32, initiator: Bool, choice: TransportChoice, target: TransportTarget, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> ProtocolSession {
        let schema: NetworkSpecCheck=network_spec_check(&spec);let model: ProtocolCheck=protocol_machine_check(&spec,&machine,minor);let mut selected: TransportChoice=choice;
        if schema.status!=0 { selected.status=schema.status; }if model.status!=0 { selected.status=model.status; }if spec.proofs.assurance!=1 || schema.unsupported_security!=0 { selected.status=6; }
        let started: U64=network_clock_ms();let transport: TransportSession=transport_open(selected,TransportRequirements(schema.required_properties,spec.transport.forbidden,spec.transport.preferred),spec.resources.transport,target,spec.session.handshake_timeout_ms,cancel);let mut session: ProtocolSession=protocol_adopt(move transport,spec,machine,minor,initiator);
        unsafe { let owned: &mut ProtocolOwned=resource_get_mut(&mut session.inner);owned.control.created=started;owned.control.activity=started;let checked: I32=protocol_guard(owned,network_clock_ms()); }return move session;
    }
    fn protocol_statistics(session: &ProtocolSession) -> ProtocolStatistics {
        let owned: &ProtocolOwned=resource_get(&session.inner);let found: USize=protocol_state(&owned.machine,owned.control.state);let mut phase: U32=6;let mut terminal: Bool=true;if owned.machine.state_count<=32 && found<cast<USize>(owned.machine.state_count) { phase=owned.machine.states[found].phase;terminal=owned.machine.states[found].terminal; }if owned.control.status!=0 { phase=6;terminal=true; }
        let schema: NetworkSpecCheck=network_spec_check(&owned.spec);return ProtocolStatistics(owned.control.state,phase,owned.control.authenticated,owned.control.sent,owned.control.received,schema.pending_proofs,terminal,NetworkError(owned.control.status,owned.system,110));
    }
    fn protocol_authenticate(session: &mut ProtocolSession) -> NetworkError {
        unsafe { let owned: &mut ProtocolOwned=resource_get_mut(&mut session.inner);let guard: I32=protocol_guard(owned,network_clock_ms());if guard!=0 { return NetworkError(guard,0,111); }let stats: TransportStatistics=transport_statistics(&owned.transport);if stats.error.kind!=0 { let stopped: I32=protocol_stop(owned,stats.error.kind,stats.error.system);return NetworkError(stopped,stats.error.system,111); }if stats.state!=1 { return NetworkError(1,0,111); }
            let schema: NetworkSpecCheck=network_spec_check(&owned.spec);if (schema.required_properties&4)!=0 && (stats.properties&4)==0 { let stopped: I32=protocol_stop(owned,6,0);return NetworkError(stopped,0,111); }let step: I32=protocol_apply(owned,3,0);if step==0 { owned.control.activity=network_clock_ms(); }return NetworkError(step,0,111);
        }
    }
    fn protocol_poll(session: &mut ProtocolSession, timeout_ms: U32, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> NetworkTransfer {
        unsafe { let owned: &mut ProtocolOwned=resource_get_mut(&mut session.inner);let now: U64=network_clock_ms();let guard: I32=protocol_guard(owned,now);if guard!=0 { return NetworkTransfer(0,NetworkError(guard,0,112)); }if timeout_ms>2147483647 { return NetworkTransfer(0,NetworkError(4,0,112)); }
            let phase: U32=owned.machine.states[protocol_state(&owned.machine,owned.control.state)].phase;let contract: SessionContract=owned.spec.session;let mut remaining: U64=cast<U64>(contract.maximum_duration_ms)-(now-owned.control.created);let idle: U64=cast<U64>(contract.idle_timeout_ms)-(now-owned.control.activity);if idle<remaining { remaining=idle; }if phase<=2 { let handshake: U64=cast<U64>(contract.handshake_timeout_ms)-(now-owned.control.created);if handshake<remaining { remaining=handshake; } }let mut wait: U32=timeout_ms;if cast<U64>(wait)>remaining { wait=cast<U32>(remaining); }
            let progress: NetworkTransfer=transport_poll(&mut owned.transport,wait,cancel);let checked: I32=protocol_guard(owned,network_clock_ms());if checked!=0 { return NetworkTransfer(progress.count,NetworkError(checked,0,112)); }let stats: TransportStatistics=transport_statistics(&owned.transport);
            if progress.error.kind==2 {
                if stats.receive_queued!=0 { return NetworkTransfer(progress.count,NetworkError(0,0,112)); }let end: I32=protocol_apply(owned,7,0);if end!=0 { let failed: I32=protocol_stop(owned,end,0);return NetworkTransfer(progress.count,NetworkError(failed,0,112)); }return NetworkTransfer(progress.count,NetworkError(2,0,112));
            }
            if progress.error.kind!=0 && progress.error.kind!=1 && progress.error.kind!=3 { let stopped: I32=protocol_stop(owned,progress.error.kind,progress.error.system);return NetworkTransfer(progress.count,NetworkError(stopped,progress.error.system,112)); }
            if stats.error.kind!=0 && stats.error.kind!=2 { let stopped: I32=protocol_stop(owned,stats.error.kind,stats.error.system);return NetworkTransfer(progress.count,NetworkError(stopped,stats.error.system,112)); }
            if phase==1 && stats.state==1 { let connected: I32=protocol_apply(owned,2,0);if connected!=0 { let stopped: I32=protocol_stop(owned,connected,0);return NetworkTransfer(progress.count,NetworkError(stopped,0,112)); } }
            if phase==4 && stats.send_queued==0 { let drained: I32=protocol_apply(owned,11,0);if drained!=0 { let stopped: I32=protocol_stop(owned,drained,0);return NetworkTransfer(progress.count,NetworkError(stopped,0,112)); } }
            return NetworkTransfer(progress.count,NetworkError(0,0,112));
        }
    }
    fn protocol_send(session: &mut ProtocolSession, channel: U32, bytes: Slice<U8>) -> NetworkTransfer {
        unsafe { let owned: &mut ProtocolOwned=resource_get_mut(&mut session.inner);let guard: I32=protocol_guard(owned,network_clock_ms());if guard!=0 { return NetworkTransfer(0,NetworkError(guard,0,113)); }
            if !protocol_counter(&owned.control,owned.spec.resources.maximum_session_messages) { return NetworkTransfer(0,NetworkError(7,0,113)); }let id: ProtocolValue=net_read_be(bytes,0,4);if id.status!=0 { return NetworkTransfer(0,NetworkError(id.status,0,113)); }let plan: CodecPlan=protocol_payload_plan(&owned.spec,owned.minor,cast<U32>(id.value));let payload: CodecShape=codec_scan(&plan,bytes);if payload.error.kind!=0 { return NetworkTransfer(0,NetworkError(payload.error.kind,0,113)); }
            let next: ProtocolStep=protocol_transition(&owned.machine,owned.control.state,4,cast<U32>(id.value),owned.control.role);if next.status!=0 { return NetworkTransfer(0,NetworkError(next.status,0,113)); }let stats: TransportStatistics=transport_statistics(&owned.transport);if stats.error.kind!=0 { let stopped: I32=protocol_stop(owned,stats.error.kind,stats.error.system);return NetworkTransfer(0,NetworkError(stopped,stats.error.system,113)); }if stats.send_queued>=owned.spec.session.maximum_in_flight { return NetworkTransfer(0,NetworkError(1,0,113)); }
            let sent: NetworkTransfer=transport_send(&mut owned.transport,channel,bytes);if sent.error.kind==0 { let step: I32=protocol_apply(owned,4,cast<U32>(id.value));if step!=0 { let stopped: I32=protocol_stop(owned,step,0);return NetworkTransfer(0,NetworkError(stopped,0,113)); }owned.control.sent=owned.control.sent+1;owned.control.activity=network_clock_ms(); }
            else { if sent.error.kind!=1 && sent.error.kind!=7 && sent.error.kind!=4 { let stopped: I32=protocol_stop(owned,sent.error.kind,sent.error.system);return NetworkTransfer(sent.count,NetworkError(stopped,sent.error.system,113)); } }return sent;
        }
    }
    fn protocol_receive(session: &mut ProtocolSession, output: &mut Buffer<U8>) -> TransportDelivery {
        unsafe { let owned: &mut ProtocolOwned=resource_get_mut(&mut session.inner);let guard: I32=protocol_guard(owned,network_clock_ms());if guard!=0 { return TransportDelivery(0,0,0,0,NetworkError(guard,0,114)); }let pending: TransportMessage=transport_peek(&owned.transport);
            if pending.error.kind!=0 { if pending.error.kind==2 { let end: I32=protocol_apply(owned,7,0);if end!=0 { let stopped: I32=protocol_stop(owned,end,0);return TransportDelivery(0,0,0,0,NetworkError(stopped,0,114)); } }return TransportDelivery(pending.id,pending.channel,0,pending.count,pending.error); }
            if !protocol_counter(&owned.control,owned.spec.resources.maximum_session_messages) { return TransportDelivery(pending.id,pending.channel,0,pending.count,NetworkError(7,0,114)); }let schema: NetworkSpecCheck=network_spec_check(&owned.spec);if pending.count>schema.maximum || pending.count<4 { let stopped: I32=protocol_stop(owned,11,0);return TransportDelivery(pending.id,pending.channel,0,pending.count,NetworkError(stopped,0,114)); }
            if pending.count>len(output) { return TransportDelivery(pending.id,pending.channel,0,pending.count,NetworkError(10,0,114)); }let mut buffer: Buffer<U8>=buffer<U8>(pending.count);let copied: NetworkTransfer=transport_copy(&owned.transport,pending.id,0,&mut buffer);if copied.error.kind!=0 || copied.count!=pending.count { let stopped: I32=protocol_stop(owned,11,0);return TransportDelivery(pending.id,pending.channel,0,pending.count,NetworkError(stopped,0,114)); }
            let id: ProtocolValue=net_read_be(slice(&buffer,0,pending.count),0,4);let plan: CodecPlan=protocol_payload_plan(&owned.spec,owned.minor,cast<U32>(id.value));let payload: CodecShape=codec_scan(&plan,slice(&buffer,0,pending.count));let next: ProtocolStep=protocol_transition(&owned.machine,owned.control.state,5,cast<U32>(id.value),owned.control.role);
            if payload.error.kind!=0 || next.status!=0 { let stopped: I32=protocol_stop(owned,11,0);return TransportDelivery(pending.id,pending.channel,0,pending.count,NetworkError(stopped,0,114)); }let ack: NetworkTransfer=transport_ack(&mut owned.transport,pending.id);if ack.error.kind!=0 { let stopped: I32=protocol_stop(owned,ack.error.kind,ack.error.system);return TransportDelivery(pending.id,pending.channel,0,pending.count,NetworkError(stopped,ack.error.system,114)); }
            let step: I32=protocol_apply(owned,5,cast<U32>(id.value));if step!=0 { let stopped: I32=protocol_stop(owned,step,0);return TransportDelivery(pending.id,pending.channel,0,pending.count,NetworkError(stopped,0,114)); }for index in 0..pending.count { output[index]=buffer[index]; }owned.control.received=owned.control.received+1;owned.control.activity=network_clock_ms();return TransportDelivery(pending.id,pending.channel,pending.count,pending.count,NetworkError(0,0,114));
        }
    }
    fn protocol_shutdown(session: &mut ProtocolSession) -> NetworkError { unsafe { let owned: &mut ProtocolOwned=resource_get_mut(&mut session.inner);let guard: I32=protocol_guard(owned,network_clock_ms());if guard!=0 { return NetworkError(guard,0,115); }return NetworkError(protocol_apply(owned,6,0),0,115); } }
    fn protocol_cancel(session: &mut ProtocolSession) -> NetworkError { unsafe { let owned: &mut ProtocolOwned=resource_get_mut(&mut session.inner);let guard: I32=protocol_guard(owned,network_clock_ms());if guard!=0 { return NetworkError(guard,0,116); }return NetworkError(protocol_stop(owned,9,0),0,116); } }
    fn protocol_close(session: ProtocolSession) -> Unit { resource_close(move session.inner); }
}
