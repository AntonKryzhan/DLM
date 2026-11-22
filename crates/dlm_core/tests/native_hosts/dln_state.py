#!/usr/bin/env python3
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import random
import shutil
import socket
import ssl
import threading
import struct
import subprocess
import tempfile
import unittest
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
COMPILER=Path(os.environ.get('DLM_NETWORK_DLM',ROOT/'target/release'/('dlm.exe' if os.name=='nt' else 'dlm')))
CHECKER=COMPILER.with_name('dlm-network-check.exe' if os.name=='nt' else 'dlm-network-check')
GENERATED=HERE/'generated/state_session.dlm'
class Check(C.Structure):_fields_=[('status',C.c_int32),('state',C.c_uint32),('transition',C.c_uint32),('reachable',C.c_uint32),('properties',C.c_uint32),('pending',C.c_uint32)]
class Step(C.Structure):_fields_=[('state',C.c_uint32),('resources',C.c_uint32),('authenticated',C.c_bool),('status',C.c_int32)]
class NativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        os.environ['NO_PROXY']='*'
        if os.environ.get('DLM_QUIC_PREBUILT') and not os.environ.get('DLM_LIBQUIC'):os.environ['DLM_LIBQUIC']=os.environ['DLM_QUIC_PREBUILT']
        cls.work=tempfile.TemporaryDirectory(prefix='dlm-state-');cls.addClassCleanup(cls.work.cleanup);out=Path(cls.work.name)/('state.dll' if os.name=='nt' else 'state.so')
        result=subprocess.run([str(COMPILER),'build',str(HERE/'dln_state.dlm'),'--lib',str(ROOT/'lib'),'--allow-unsafe','--target','sharedlib','-o',str(out)],capture_output=True,text=True)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        cls.api=C.CDLL(str(out));signatures={'state_tcp_contracts':(C.c_uint32,[]),'state_invalid_constructor':(C.c_int32,[]),'state_tls_client':(C.c_int32,[C.c_uint16,C.c_bool]),'state_quic':(C.c_uint32,[C.c_uint32]),'state_message_limit':(C.c_uint32,[]),'state_cancel_timeout':(C.c_int32,[C.c_uint32]),'state_security_refusal':(C.c_int32,[C.c_uint32]),'state_eof_or_malformed':(C.c_uint32,[C.c_bool]),'state_model_error':(Check,[C.c_uint32]),'state_step':(Step,[C.c_uint32]*4),'state_clock':(C.c_int32,[C.c_uint64]*4+[C.c_uint32]),'state_counter':(C.c_bool,[C.c_uint64]*3),'state_abort_cleanup':(None,[]),'dlm_native_last_error':(C.c_char_p,[]),'dlm_native_clear_error':(None,[]),'dlm_native_live_allocations':(C.c_uint64,[])}
        for name,(ret,args)in signatures.items():function=getattr(cls.api,name);function.restype=ret;function.argtypes=args
    def tearDown(self):self.assertIsNone(self.api.dlm_native_last_error());self.assertEqual(self.api.dlm_native_live_allocations(),0)
    def test_tcp_roles_phase_gates_retained_short_output_and_drain(self):self.assertEqual(self.api.state_tcp_contracts(),255)
    def test_accepted_messages_only_consume_the_global_session_budget(self):self.assertEqual(self.api.state_message_limit(),1)
    def test_cancellation_is_terminal_and_idempotent(self):self.assertEqual(self.api.state_cancel_timeout(0),9)
    def test_native_poll_is_capped_by_the_handshake_deadline(self):self.assertEqual(self.api.state_cancel_timeout(2),3)
    def test_secure_and_mutual_identity_never_admit_plain_tcp(self):self.assertEqual(self.api.state_security_refusal(3),6);self.assertEqual(self.api.state_security_refusal(4),6)
    def test_last_valid_message_survives_transport_eof(self):self.assertEqual(self.api.state_eof_or_malformed(False),1)
    def test_malformed_peer_packet_fails_closed_without_output_writes(self):self.assertEqual(self.api.state_eof_or_malformed(True),2)
    def test_verified_tls_gate_accepts_a_valid_server_and_rejects_wrong_identity(self):
        fixtures=ROOT/'examples/native/fixtures'
        for wrong in (False,True):
            listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(3);port=listener.getsockname()[1];errors=[]
            def peer():
                try:
                    context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.minimum_version=ssl.TLSVersion.TLSv1_3;context.load_cert_chain(fixtures/'quic-test-server.pem',fixtures/'quic-test-key.pem')
                    with listener:
                        connection,_=listener.accept();connection.settimeout(3)
                        with context.wrap_socket(connection,server_side=True) as stream:
                            if wrong:return
                            def exact(n):
                                data=b''
                                while len(data)<n:
                                    block=stream.recv(n-len(data))
                                    if not block:raise ValueError('unexpected TLS EOF')
                                    data+=block
                                return data
                            header=exact(16);count=struct.unpack_from('!I',header,4)[0];body=exact(count);stream.sendall(header+body)
                except ssl.SSLError as error:
                    if not wrong:errors.append(error)
                except Exception as error:errors.append(error)
            thread=threading.Thread(target=peer,daemon=True);thread.start();previous=os.getcwd()
            try:os.chdir(ROOT);self.assertEqual(self.api.state_tls_client(port,wrong),8 if wrong else 0)
            finally:os.chdir(previous);thread.join(4);listener.close()
            self.assertFalse(thread.is_alive());self.assertEqual(errors,[])
    def test_authenticated_quic_sessions_use_all_three_message_profiles(self):
        previous=os.getcwd()
        try:
            os.chdir(ROOT)
            for profile in (4,5,6):self.assertEqual(self.api.state_quic(profile),profile)
        finally:os.chdir(previous)
    def test_invalid_constructor_is_queryable_and_fails_closed(self):self.assertEqual(self.api.state_invalid_constructor(),4)
    def test_invalid_graphs_are_rejected(self):
        self.assertEqual(self.api.state_model_error(0).status,0)
        for mode in range(1,11):self.assertNotEqual(self.api.state_model_error(mode).status,0)
    def test_all_control_and_message_steps_match_an_independent_table_oracle(self):
        data=Path(str(GENERATED)+'.state').read_bytes();initial,nstates,nedges=struct.unpack_from('!3I',data,4);states={};edges=[];position=16
        for _ in range(nstates):row=struct.unpack_from('!5I',data,position);position+=20;states[row[0]]=row
        for _ in range(nedges):edges.append(struct.unpack_from('!8I',data,position));position+=32
        for state in range(10):
            for event in range(14):
                for message in range(6):
                    for role in (0,1,2,3,4):
                        matches=[t for t in edges if t[0]==state and t[1]==event and t[2]==message and t[3]&role] if role in (1,2) else []
                        observed=self.api.state_step(state,event,message,role)
                        if len(matches)==1:
                            target=states[matches[0][4]];self.assertEqual((observed.status,observed.state,observed.resources,observed.authenticated),(0,target[0],target[2],bool(target[3])))
                        else:self.assertEqual(observed.status,4)
    def test_monotonic_deadline_and_overflow_boundaries_match_integer_oracles(self):
        randomizer=random.Random(143);maximum=(1<<64)-1
        for _ in range(2000):
            created=randomizer.choice((0,randomizer.randrange(10000),maximum-10000));activity=created+randomizer.randrange(5000);observed=created+randomizer.randrange(5000);now=created+randomizer.randrange(10000);phase=randomizer.randrange(5)
            expected=4 if now<max(created,activity,observed) else 3 if now-created>=5000 or (phase<=2 and now-created>=1000) or now-activity>=2000 else 0
            self.assertEqual(self.api.state_clock(created,activity,observed,now,phase),expected)
            sent=randomizer.choice((0,maximum,randomizer.getrandbits(64)));received=randomizer.choice((0,maximum,randomizer.getrandbits(64)));limit=randomizer.choice((0,maximum,randomizer.getrandbits(64)));self.assertEqual(self.api.state_counter(sent,received,limit),sent+received<limit)
    def test_partial_owner_moves_unwind_without_leaks(self):
        for _ in range(24):self.api.state_abort_cleanup();self.assertIsNotNone(self.api.dlm_native_last_error());self.assertEqual(self.api.dlm_native_live_allocations(),0);self.api.dlm_native_clear_error()

class CertificateTests(unittest.TestCase):
    def setUp(self):
        self.work=tempfile.TemporaryDirectory(prefix='dlm-network-proof-');self.addCleanup(self.work.cleanup);self.directory=Path(self.work.name);self.source=self.directory/GENERATED.name
        for suffix in ('','.dns','.state','.nproof'):shutil.copyfile(Path(str(GENERATED)+suffix),Path(str(self.source)+suffix))
        self.proof=Path(str(self.source)+'.nproof');self.library=ROOT/'lib'
    def replace(self,key,value):
        lines=self.proof.read_text().splitlines();self.proof.write_text('\n'.join(key+' '+str(value) if line.startswith(key+' ') else line for line in lines)+'\n')
    def audit(self,extra=(),success=True):
        result=subprocess.run([str(CHECKER),str(self.proof),'--lib',str(self.library),*map(str,extra)],capture_output=True,text=True)
        self.assertEqual(result.returncode==0,success,result.stdout+result.stderr);return result
    def generate(self,input=None,extra=()):
        output=self.directory/'new.dlm';return subprocess.run([str(COMPILER),'network-protocol',str(input or HERE/'dln_state_schema.dlm'),'--lib',str(ROOT/'lib'),'--module','generated.new','--prefix','New','-o',str(output),*map(str,extra)],capture_output=True,text=True)
    def test_standalone_checker_and_compiler_kernel_agree(self):
        report=json.loads(self.audit().stdout);self.assertEqual(report['model_verified'],416);self.assertEqual(report['runtime_pending'],431);self.assertFalse(report['native_correctness_proved']);self.assertTrue(any('GnuTLS' in text for text in report['assumptions']))
        result=subprocess.run([str(COMPILER),'proof-check',str(self.proof),'--lib',str(self.library)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr);self.assertEqual(json.loads(result.stdout),report)
    def test_rebound_malicious_source_body_is_still_rejected(self):
        data=self.source.read_bytes().replace(b'protocol_adopt(move transport,State_spec(),State_machine(),1,initiator)',b'protocol_adopt(move transport,State_spec(),State_machine(),0,initiator)');self.assertNotEqual(data,self.source.read_bytes());self.source.write_bytes(data);self.replace('source',self.source.name.encode().hex()+' '+hashlib.sha256(data).hexdigest());self.assertIn('admitted executable rules',self.audit(success=False).stderr)
    def test_state_properties_and_reachability_cannot_be_self_asserted(self):
        original=self.proof.read_text()
        for key,value in [('properties',2047),('reachable',0),('ranks','0 0 0 0 0 0 0')]:
            self.proof.write_text(original);self.replace(key,value);self.audit(success=False)
    def test_runtime_changes_are_rejected_even_with_updated_certificate_hashes(self):
        self.library=self.directory/'lib'
        for line in self.proof.read_text().splitlines():
            if line.startswith('dependency '):
                _,name,_=line.split();target=self.library/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/'lib'/name,target)
        changed=self.library/'std/net_session.dlm';changed.write_bytes(changed.read_bytes()+b'\n// changed\n');self.replace('dependency std/net_session.dlm',hashlib.sha256(changed.read_bytes()).hexdigest());self.assertIn('pinned runtime closure',self.audit(success=False).stderr)
    def test_terminal_resources_and_undefined_edges_fail_before_source_binding(self):
        original=self.proof.read_text();binary=Path(str(self.source)+'.state').read_bytes()
        for offset,value in [(16+6*20+8,1),(16+7*20+32+16,999)]:
            self.proof.write_text(original);data=bytearray(binary);struct.pack_into('!I',data,offset,value);self.replace('executable',data.hex());self.audit(success=False)
    def test_sidecar_tampering_and_duplicate_headers_fail(self):
        sidecar=Path(str(self.source)+'.dns');data=sidecar.read_bytes();sidecar.write_bytes(data+b'\0');self.audit(success=False);sidecar.write_bytes(data);self.proof.write_text(self.proof.read_text()+'minor 1\n');self.assertIn('duplicate',self.audit(success=False).stderr)
    def test_zero_work_is_unknown_not_a_successful_proof(self):
        result=self.audit(extra=('--work','0'),success=False);self.assertEqual(result.returncode,3);self.assertIn('UnknownWithinBudget',result.stderr);self.assertEqual(result.stdout,'')
    def test_executable_identity_binding_is_verified_without_claiming_native_correctness(self):
        binary=self.directory/'program.bin';binary.write_bytes(b'bound file identity, not a machine-code proof')
        result=subprocess.run([str(COMPILER),'network-bind',str(self.proof),'--lib',str(self.library),'--binary',str(binary)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr);report=json.loads(self.audit(extra=('--binary',binary)).stdout);self.assertEqual(report['binary_sha256'],hashlib.sha256(binary.read_bytes()).hexdigest());self.assertFalse(report['native_correctness_proved']);self.audit(success=False);binary.write_bytes(b'changed');self.assertIn('binary binding',self.audit(extra=('--binary',binary),success=False).stderr)
    def test_custom_factory_and_binary_machine_ir_generate_equivalent_checked_code(self):
        schema=(HERE/'dln_state_schema.dlm').read_text();end=schema.rfind('}');factory='''    fn network_machine()->ProtocolMachine guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork){let spec:NetworkSpec=network_schema();let mut machine:ProtocolMachine=protocol_default(&spec,1);machine.states[3].id=40;for i in 0..cast<USize>(machine.transition_count){if machine.transitions[i].source==4{machine.transitions[i].source=40;}if machine.transitions[i].target==4{machine.transitions[i].target=40;}}return machine;}\n''';input=self.directory/'custom.dlm';input.write_text(schema[:end]+factory+schema[end:]);result=self.generate(input);self.assertEqual(result.returncode,0,result.stderr);new=self.directory/'new.dlm';self.assertIn('ProtocolState(40,3,1,true,false)',new.read_text());original=new.read_bytes();result=self.generate(Path(str(new)+'.dns'),extra=('--machine',Path(str(new)+'.state'),'--check'));self.assertEqual(result.returncode,0,result.stderr);self.assertEqual(new.read_bytes(),original)
    def test_failed_regeneration_and_budget_exhaustion_leave_all_files_unchanged(self):
        result=self.generate();self.assertEqual(result.returncode,0,result.stderr);source=self.directory/'new.dlm';source.write_bytes(source.read_bytes()+b'\n// edited\n');paths=[Path(str(source)+suffix)for suffix in ('','.dns','.state','.nproof')];before=[p.read_bytes()for p in paths]
        for extra in [('--check',),(),('--work','0','--force')]:result=self.generate(extra=extra);self.assertNotEqual(result.returncode,0);self.assertEqual([p.read_bytes()for p in paths],before)
    def test_path_escape_and_unknown_certificate_headers_are_rejected(self):
        original=self.proof.read_text();self.replace('source','../outside.dlm'.encode().hex()+' '+'0'*64);self.audit(success=False);self.proof.write_text(original+'trusted true\n');self.audit(success=False)
    def test_mutated_ir_never_panics_or_bypasses_the_dlm_validator(self):
        randomizer=random.Random(143);original=Path(str(self.source)+'.state').read_bytes();input=self.directory/'bad.state'
        for _ in range(80):
            data=bytearray(original);position=randomizer.randrange(len(data));data[position]^=1<<randomizer.randrange(8);input.write_bytes(data);result=self.generate(extra=('--machine',input,'--force'))
            self.assertIn(result.returncode,(0,1,3),result.stderr);self.assertNotIn('panicked',result.stderr);self.assertNotIn('generated machine failed the DLM validator',result.stderr)

if __name__=='__main__':unittest.main(verbosity=2)
