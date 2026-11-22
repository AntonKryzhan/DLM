module generated.state_session
import std.net_session
native {
    fn State_spec() -> NetworkSpec guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut spec: NetworkSpec=network_spec(1398030641,1,1,0);
        spec.wire=WireFormat(2,2,2,0);spec.transport=TransportRequirements(19,0,0);
        spec.resources=ResourceBounds(TransportBounds(65536,32,16,8388608),0,0,U64(4),529);
        spec.session=SessionContract(1000,2000,5000,1,false);spec.security=SecurityRequirements(0,false,false,0);spec.proofs=ProofObligations(0,1);
        spec.message_count=3;
        spec.messages[0]=message_schema(1,2,1);spec.messages[0].first_minor=0;spec.messages[0].last_minor=4294967295;spec.messages[0].field_count=1;
        spec.messages[0].fields[0]=MessageField(1,4,1,1,0,0,4294967295,0);
        spec.messages[1]=message_schema(2,1,2);spec.messages[1].first_minor=0;spec.messages[1].last_minor=4294967295;spec.messages[1].field_count=0;
        spec.messages[2]=message_schema(3,3,3);spec.messages[2].first_minor=0;spec.messages[2].last_minor=4294967295;spec.messages[2].field_count=0;
        return spec;
    }
    fn State_machine() -> ProtocolMachine guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork) {
        let mut machine: ProtocolMachine=protocol_machine();machine.initial=1;machine.state_count=7;machine.transition_count=31;
        machine.states[0]=ProtocolState(1,0,0,false,false);
        machine.states[1]=ProtocolState(2,1,1,false,false);
        machine.states[2]=ProtocolState(3,2,1,true,false);
        machine.states[3]=ProtocolState(4,3,1,true,false);
        machine.states[4]=ProtocolState(5,4,1,true,false);
        machine.states[5]=ProtocolState(6,5,0,false,true);
        machine.states[6]=ProtocolState(7,6,0,false,true);
        machine.transitions[0]=ProtocolTransition(1,1,0,3,2,1,0,false);
        machine.transitions[1]=ProtocolTransition(2,2,0,3,3,0,0,false);
        machine.transitions[2]=ProtocolTransition(3,3,0,3,4,0,0,false);
        machine.transitions[3]=ProtocolTransition(1,8,0,3,7,0,0,false);
        machine.transitions[4]=ProtocolTransition(1,9,0,3,7,0,0,false);
        machine.transitions[5]=ProtocolTransition(1,10,0,3,7,0,0,false);
        machine.transitions[6]=ProtocolTransition(1,7,0,3,6,0,0,false);
        machine.transitions[7]=ProtocolTransition(2,8,0,3,7,0,1,false);
        machine.transitions[8]=ProtocolTransition(2,9,0,3,7,0,1,false);
        machine.transitions[9]=ProtocolTransition(2,10,0,3,7,0,1,false);
        machine.transitions[10]=ProtocolTransition(2,7,0,3,6,0,1,false);
        machine.transitions[11]=ProtocolTransition(3,8,0,3,7,0,1,false);
        machine.transitions[12]=ProtocolTransition(3,9,0,3,7,0,1,false);
        machine.transitions[13]=ProtocolTransition(3,10,0,3,7,0,1,false);
        machine.transitions[14]=ProtocolTransition(3,7,0,3,6,0,1,false);
        machine.transitions[15]=ProtocolTransition(4,8,0,3,7,0,1,false);
        machine.transitions[16]=ProtocolTransition(4,9,0,3,7,0,1,false);
        machine.transitions[17]=ProtocolTransition(4,10,0,3,7,0,1,false);
        machine.transitions[18]=ProtocolTransition(4,7,0,3,6,0,1,false);
        machine.transitions[19]=ProtocolTransition(5,8,0,3,7,0,1,false);
        machine.transitions[20]=ProtocolTransition(5,9,0,3,7,0,1,false);
        machine.transitions[21]=ProtocolTransition(5,10,0,3,7,0,1,false);
        machine.transitions[22]=ProtocolTransition(5,7,0,3,6,0,1,false);
        machine.transitions[23]=ProtocolTransition(4,6,0,3,5,0,0,true);
        machine.transitions[24]=ProtocolTransition(5,11,0,3,6,0,1,true);
        machine.transitions[25]=ProtocolTransition(4,4,1,1,4,0,0,true);
        machine.transitions[26]=ProtocolTransition(4,5,1,2,4,0,0,true);
        machine.transitions[27]=ProtocolTransition(3,4,2,2,3,0,0,false);
        machine.transitions[28]=ProtocolTransition(3,5,2,1,3,0,0,false);
        machine.transitions[29]=ProtocolTransition(4,4,3,3,5,0,0,true);
        machine.transitions[30]=ProtocolTransition(4,5,3,3,5,0,0,true);
        return machine;
    }
    fn State_open(initiator: Bool, choice: TransportChoice, target: TransportTarget, cancel: &Resource<OpaqueHandle,net_cancel_dispose>) -> ProtocolSession { return protocol_open(State_spec(),State_machine(),1,initiator,choice,target,cancel); }
    fn State_adopt(transport: TransportSession, initiator: Bool) -> ProtocolSession { return protocol_adopt(move transport,State_spec(),State_machine(),1,initiator); }
}
