module std.net_session_machine
import std.net_spec_compat
import std.net_codec_runtime
native {
    record ProtocolState repr(C, version=1) { id: U32; phase: U32; resources: U32; authenticated: Bool; terminal: Bool; }
    record ProtocolTransition repr(C, version=1) { source: U32; event: U32; message: U32; roles: U32; target: U32; acquire: U32; release: U32; require_auth: Bool; }
    record ProtocolMachine repr(C, version=1) { initial: U32; state_count: U32; transition_count: U32; states: Array<ProtocolState,32>; transitions: Array<ProtocolTransition,256>; }
    record ProtocolCheck repr(C) { status: I32; state: U32; transition: U32; reachable: U32; model_properties: U32; pending_proofs: U32; }
    record ProtocolControl repr(C) { state: U32; role: U32; resources: U32; authenticated: Bool; sent: U64; received: U64; created: U64; activity: U64; observed: U64; status: I32; }
    record ProtocolStep repr(C) { state: U32; resources: U32; authenticated: Bool; status: I32; }
    // Events: start, connected, authenticate, send, receive, close, peer-close,
    // timeout, failure, cancel, drained. Roles describe the local endpoint.
    fn protocol_state(machine: &ProtocolMachine, id: U32) -> USize guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if machine.state_count>32 { return 32; }for index in 0..cast<USize>(machine.state_count) { if machine.states[index].id==id { return index; } }return cast<USize>(machine.state_count);
    }
    fn protocol_machine() -> ProtocolMachine guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) { return zero<ProtocolMachine>(); }
    fn protocol_add_state(machine: &mut ProtocolMachine, state: ProtocolState) -> I32 guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if machine.state_count>=32 { return 7; }if state.id==0 || protocol_state(machine,state.id)!=cast<USize>(machine.state_count) { return 4; }machine.states[cast<USize>(machine.state_count)]=state;machine.state_count=machine.state_count+1;return 0;
    }
    fn protocol_add_transition(machine: &mut ProtocolMachine, transition: ProtocolTransition) -> I32 guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if machine.transition_count>=256 { return 7; }machine.transitions[cast<USize>(machine.transition_count)]=transition;machine.transition_count=machine.transition_count+1;return 0;
    }
    fn protocol_failure(status: I32, state: U32, transition: U32) -> ProtocolCheck guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) { return ProtocolCheck(status,state,transition,0,0,0); }
    fn protocol_machine_check(spec: &NetworkSpec, machine: &ProtocolMachine, minor: U32) -> ProtocolCheck guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let schema: NetworkSpecCheck=network_spec_check(spec);if schema.status!=0 { return protocol_failure(schema.status,0,0); }
        if minor<spec.version.minimum_minor || minor>spec.version.minor || machine.state_count==0 || machine.state_count>32 || machine.transition_count==0 || machine.transition_count>256 { return protocol_failure(4,0,0); }
        let initial: USize=protocol_state(machine,machine.initial);if initial==cast<USize>(machine.state_count) { return protocol_failure(4,machine.initial,0); }
        if machine.states[initial].phase!=0 || machine.states[initial].resources!=0 || machine.states[initial].authenticated || machine.states[initial].terminal { return protocol_failure(4,machine.initial,0); }
        let mut reached: U32=U32(1)<<cast<U32>(initial);let mut terminals: U32=0;
        for index in 0..cast<USize>(machine.state_count) {
            let state: ProtocolState=machine.states[index];if state.id==0 || state.phase>6 || state.resources>1 || protocol_state(machine,state.id)!=index { return protocol_failure(4,state.id,0); }
            if !state.terminal {
                if (state.phase==0 && (state.id!=machine.initial || state.resources!=0 || state.authenticated)) || (state.phase>0 && state.resources!=1) || (state.phase==1 && state.authenticated) || (state.phase>=3 && !state.authenticated) { return protocol_failure(4,state.id,0); }
                let mut required: U32=1;if state.phase==1 { required=2; }if state.phase==2 { required=3; }if state.phase==3 { required=6; }if state.phase==4 { required=11; }
                for role in 1..3 { for kind in 0..2 { let mut event: U32=required;if kind==1 { event=7; }let mut found: U32=0;for edge in 0..cast<USize>(machine.transition_count) { let t: ProtocolTransition=machine.transitions[edge];if t.source==state.id && t.event==event && t.message==0 && (t.roles&cast<U32>(role))!=0 { found=found+1; } }if found!=1 { return protocol_failure(4,state.id,0); } } }
            }
            if state.terminal { if state.resources!=0 || (state.phase!=5 && state.phase!=6) || state.authenticated { return protocol_failure(4,state.id,0); }terminals=terminals|(U32(1)<<cast<U32>(index)); }
            else { if state.phase>=5 { return protocol_failure(4,state.id,0); }for event in 8..11 { for role in 1..3 { let mut found: U32=0;for edge in 0..cast<USize>(machine.transition_count) { let t: ProtocolTransition=machine.transitions[edge];if t.source==state.id && t.event==cast<U32>(event) && t.message==0 && (t.roles&cast<U32>(role))!=0 { let target: USize=protocol_state(machine,t.target);if target<cast<USize>(machine.state_count) { if machine.states[target].terminal { found=found+1; } } } }if found!=1 { return protocol_failure(4,state.id,0); } } } }
        }
        if terminals==0 { return protocol_failure(4,0,0); }
        for edge in 0..cast<USize>(machine.transition_count) {
            let t: ProtocolTransition=machine.transitions[edge];let from: USize=protocol_state(machine,t.source);let to: USize=protocol_state(machine,t.target);
            if from==cast<USize>(machine.state_count) || to==cast<USize>(machine.state_count) || t.event==0 || t.event>11 || t.roles==0 || t.roles>3 || t.acquire>65535 || t.release>65535 { return protocol_failure(4,t.source,cast<U32>(edge)); }
            let a: ProtocolState=machine.states[from];let b: ProtocolState=machine.states[to];if a.terminal || (t.acquire&a.resources)!=0 || (t.acquire&t.release)!=0 || (t.release&(U32(65535)^a.resources))!=0 || ((a.resources|t.acquire)&(U32(65535)^t.release))!=b.resources { return protocol_failure(4,t.source,cast<U32>(edge)); }
            if (!b.terminal && b.phase<a.phase) || (t.event==1 && (a.phase!=0 || b.phase!=1)) || (t.event==2 && (a.phase!=1 || b.phase!=2)) || (t.event==3 && (a.phase!=2 || (b.phase!=2 && b.phase!=3) || !b.authenticated)) || (t.event==6 && ((a.phase!=3 && a.phase!=4) || b.phase!=4)) || (t.event==7 && (!b.terminal || b.phase!=5)) || (t.event>=8 && t.event<=10 && (!b.terminal || b.phase!=6)) || (t.event==11 && (a.phase!=4 || !b.terminal || b.phase!=5)) || (t.event==4 && b.terminal) { return protocol_failure(4,t.source,cast<U32>(edge)); }
            if t.require_auth && !a.authenticated { return protocol_failure(4,t.source,cast<U32>(edge)); }
            if b.authenticated && !a.authenticated && t.event!=2 && t.event!=3 { return protocol_failure(4,t.source,cast<U32>(edge)); }
            if !b.terminal && a.authenticated && !b.authenticated { return protocol_failure(4,t.source,cast<U32>(edge)); }
            if t.event==4 || t.event==5 {
                let found: USize=network_find_message(spec,t.message,minor);if found==cast<USize>(spec.message_count) { return protocol_failure(4,t.source,cast<U32>(edge)); }let message: &MessageSchema=&spec.messages[found];let mut senders: U32=message.senders;if t.event==5 { senders=((senders&1)<<1)|((senders&2)>>1); }
                if (t.roles&(U32(3)^senders))!=0 || (message.phase==1 && a.phase!=2) || (message.phase==2 && a.phase!=3) || (message.phase==3 && a.phase!=3 && a.phase!=4) { return protocol_failure(4,t.source,cast<U32>(edge)); }
                let mut secure: Bool=spec.session.authentication_before_data && message.phase==2;
                for field in 0..cast<USize>(message.field_count) { if network_minor_active(message.fields[field].first_minor,message.fields[field].last_minor,minor) && (message.fields[field].flags&1)!=0 { secure=true; } }
                if secure && (!a.authenticated || !t.require_auth) { return protocol_failure(4,t.source,cast<U32>(edge)); }
            }else { if t.message!=0 { return protocol_failure(4,t.source,cast<U32>(edge)); } }
            for prior in 0..edge { let previous: ProtocolTransition=machine.transitions[prior];if previous.source==t.source && previous.event==t.event && previous.message==t.message && (previous.roles&t.roles)!=0 { return protocol_failure(4,t.source,cast<U32>(edge)); } }
        }
        for round in 0..cast<USize>(machine.state_count) { for edge in 0..cast<USize>(machine.transition_count) { let t: ProtocolTransition=machine.transitions[edge];let from: USize=protocol_state(machine,t.source);let to: USize=protocol_state(machine,t.target);if (reached&(U32(1)<<cast<U32>(from)))!=0 { reached=reached|(U32(1)<<cast<U32>(to)); } } }
        let mut all: U32=4294967295;if machine.state_count<32 { all=(U32(1)<<machine.state_count)-1; }if reached!=all { return protocol_failure(4,0,0); }
        let mut properties: U32=416;if spec.session.authentication_before_data { properties=properties|64; }return ProtocolCheck(0,0,0,reached,properties,schema.pending_proofs);
    }
    fn protocol_default(spec: &NetworkSpec, minor: U32) -> ProtocolMachine guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let checked: NetworkSpecCheck=network_spec_check(spec);let mut machine: ProtocolMachine=protocol_machine();if checked.status!=0 || minor<spec.version.minimum_minor || minor>spec.version.minor { return machine; }
        machine.initial=1;machine.state_count=7;machine.states[0]=ProtocolState(1,0,0,false,false);machine.states[1]=ProtocolState(2,1,1,false,false);machine.states[2]=ProtocolState(3,2,1,true,false);machine.states[3]=ProtocolState(4,3,1,true,false);machine.states[4]=ProtocolState(5,4,1,true,false);machine.states[5]=ProtocolState(6,5,0,false,true);machine.states[6]=ProtocolState(7,6,0,false,true);
        let a: I32=protocol_add_transition(&mut machine,ProtocolTransition(1,1,0,3,2,1,0,false));let b: I32=protocol_add_transition(&mut machine,ProtocolTransition(2,2,0,3,3,0,0,false));let c: I32=protocol_add_transition(&mut machine,ProtocolTransition(3,3,0,3,4,0,0,false));assert(a==0 && b==0 && c==0);
        for index in 0..5 { let state: ProtocolState=machine.states[index];for event in 8..11 { let added: I32=protocol_add_transition(&mut machine,ProtocolTransition(state.id,cast<U32>(event),0,3,7,0,state.resources,false));assert(added==0); }let remote: I32=protocol_add_transition(&mut machine,ProtocolTransition(state.id,7,0,3,6,0,state.resources,false));assert(remote==0); }
        let close: I32=protocol_add_transition(&mut machine,ProtocolTransition(4,6,0,3,5,0,0,true));let drained: I32=protocol_add_transition(&mut machine,ProtocolTransition(5,11,0,3,6,0,1,true));assert(close==0 && drained==0);
        for index in 0..cast<USize>(spec.message_count) {
            let message: &MessageSchema=&spec.messages[index];if !network_minor_active(message.first_minor,message.last_minor,minor) { continue; }let mut from: U32=4;let mut target: U32=4;let mut auth: Bool=true;if message.phase==1 { from=3;target=3;auth=false; }if message.phase==3 { target=5; }
            let send: I32=protocol_add_transition(&mut machine,ProtocolTransition(from,4,message.id,message.senders,target,0,0,auth));let roles: U32=((message.senders&1)<<1)|((message.senders&2)>>1);let receive: I32=protocol_add_transition(&mut machine,ProtocolTransition(from,5,message.id,roles,target,0,0,auth));assert(send==0 && receive==0);
        }return machine;
    }
    fn protocol_transition(machine: &ProtocolMachine, state: U32, event: U32, message: U32, role: U32) -> ProtocolStep guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if machine.state_count>32 || machine.transition_count>256 || (role!=1 && role!=2) { return ProtocolStep(state,0,false,4); }let source: USize=protocol_state(machine,state);if source==cast<USize>(machine.state_count) { return ProtocolStep(state,0,false,4); }
        let mut found: USize=256;for index in 0..cast<USize>(machine.transition_count) { let t: ProtocolTransition=machine.transitions[index];if t.source==state && t.event==event && t.message==message && (t.roles&role)!=0 { if found!=256 { return ProtocolStep(state,0,false,4); }found=index; } }
        if found==256 { return ProtocolStep(state,machine.states[source].resources,machine.states[source].authenticated,4); }let t: ProtocolTransition=machine.transitions[found];let target: USize=protocol_state(machine,t.target);if target==cast<USize>(machine.state_count) { return ProtocolStep(state,0,false,4); }return ProtocolStep(t.target,machine.states[target].resources,machine.states[target].authenticated,0);
    }
    fn protocol_payload_plan(spec: &NetworkSpec, minor: U32, id: U32) -> CodecPlan guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut plan: CodecPlan=zero<CodecPlan>();let checked: NetworkSpecCheck=network_spec_check(spec);if checked.status!=0 { return plan; }let found: USize=network_find_message(spec,id,minor);if found==cast<USize>(spec.message_count) || spec.message_count>16 { return plan; }let message: &MessageSchema=&spec.messages[found];if message.field_count>32 { return plan; }plan.message=id;plan.wire=spec.wire;plan.minor=minor;plan.maximum_wire=4;if spec.wire.kind==2 { plan.maximum_wire=12; }
        for index in 0..cast<USize>(message.field_count) { let field: MessageField=message.fields[index];if network_minor_active(field.first_minor,field.last_minor,minor) { plan.fields[cast<USize>(plan.field_count)]=field;plan.field_count=plan.field_count+1;plan.maximum_values=plan.maximum_values+field.maximum_count;let measured: SchemaMeasure=network_field_measure(&field,spec.wire);plan.maximum_wire=plan.maximum_wire+cast<USize>(measured.maximum); } }
        plan.maximum_unknown_fields=spec.resources.maximum_unknown_fields;plan.maximum_unknown_bytes=cast<USize>(spec.resources.maximum_unknown_bytes);plan.maximum_wire=plan.maximum_wire+plan.maximum_unknown_bytes;plan.storage_budget=spec.resources.transport.memory;return plan;
    }
    fn protocol_clock(control: &ProtocolControl, contract: SessionContract, now: U64, phase: U32) -> I32 guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        if control.status!=0 { return control.status; }if now<control.observed || now<control.created || now<control.activity { return 4; }
        if now-control.created>=cast<U64>(contract.maximum_duration_ms) { return 3; }if phase<=2 && now-control.created>=cast<U64>(contract.handshake_timeout_ms) { return 3; }if now-control.activity>=cast<U64>(contract.idle_timeout_ms) { return 3; }return 0;
    }
    fn protocol_counter(control: &ProtocolControl, maximum: U64) -> Bool guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) { return control.sent<=maximum && control.received<maximum-control.sent; }
    fn protocol_state_words(state: &ProtocolState) -> Array<U32,5> guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut auth: U32=0;let mut terminal: U32=0;if state.authenticated { auth=1; }if state.terminal { terminal=1; }let mut result: Array<U32,5>=zero<Array<U32,5>>();result[0]=state.id;result[1]=state.phase;result[2]=state.resources;result[3]=auth;result[4]=terminal;return result;
    }
    fn protocol_transition_words(edge: &ProtocolTransition) -> Array<U32,8> guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut auth: U32=0;if edge.require_auth { auth=1; }let mut result: Array<U32,8>=zero<Array<U32,8>>();result[0]=edge.source;result[1]=edge.event;result[2]=edge.message;result[3]=edge.roles;result[4]=edge.target;result[5]=edge.acquire;result[6]=edge.release;result[7]=auth;return result;
    }
}
