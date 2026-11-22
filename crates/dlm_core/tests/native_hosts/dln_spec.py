#!/usr/bin/env python3
"""Independent NetworkSpec metadata/property/compatibility oracle; stdlib only.
Builds the actual DLM modules as a shared library. No network/crypto dependency.
"""
import copy
import ctypes as C
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[4]
HERE=Path(__file__).resolve().parent
U32,U64,SZ,I32=C.c_uint32,C.c_uint64,C.c_size_t,C.c_int32
PTR=C.POINTER(C.c_uint8)
class Requirements(C.Structure):_fields_=[(n,U32) for n in ('required','forbidden','preferred')]
class Bounds(C.Structure):_fields_=[(n,U32) for n in ('max_message','queued_messages','channels')]+[('memory',SZ)]
class Choice(C.Structure):_fields_=[(n,U32) for n in ('profile','properties','missing','forbidden','preference_misses','cost')]+[('status',I32)]
class Check(C.Structure):_fields_=[('status',I32)]+[(n,U32) for n in ('component','message','field')]+[(n,SZ) for n in ('minimum','maximum')]+[(n,U32) for n in ('required_properties','required_proofs','pending_proofs','unsupported_security','schema_nodes')]
class Session(C.Structure):_fields_=[(n,U32) for n in ('handshake_timeout_ms','idle_timeout_ms','maximum_duration_ms','maximum_in_flight')]+[('authentication_before_data',C.c_bool)]
class Admission(C.Structure):_fields_=[('requirements',Requirements),('bounds',Bounds),('choice',Choice),('session',Session),('maximum_session_messages',U64),('pending_proofs',U32),('unsupported_security',U32),('status',I32)]
class Negotiation(C.Structure):_fields_=[('minor',U32),('requirements',Requirements),('bounds',Bounds),('choice',Choice),('session',Session),('maximum_session_messages',U64)]+[(n,U32) for n in ('pending_proofs','unsupported_security')]+[('status',I32)]+[(n,U32) for n in ('component','message','field')]
class Measure(C.Structure):_fields_=[('minimum',U64),('maximum',U64),('status',I32)]
MAX=(1<<32)-1

def field(identifier=1,kind=3,minimum=1,maximum=1,length=0,first=0,last=MAX,flags=0):return [identifier,kind,minimum,maximum,length,first,last,flags]
def message(identifier=1,fields=None,first=0,last=MAX,senders=3,phase=2):return dict(id=identifier,fields=[field()] if fields is None else fields,first=first,last=last,senders=senders,phase=phase)
def sample(fields=None,minor=0,minimum=0):
    return dict(protocol=0x444c4e31,version=[1,minor,minimum],wire=[2,2,2,0],transport=[16,0,0],bounds=[65536,32,16,8<<20],unknown=[0,0],messages_limit=1000000,nodes=529,session=[10000,30000,300000,32,0],security=[0,0,0,0],proofs=[0,1],messages=[message(fields=fields)])
def encode(spec):
    memory=spec['bounds'][3];limit=spec['messages_limit']
    words=[spec['protocol'],*spec['version'],*spec['wire'],*spec['transport'],*spec['bounds'][:3],memory>>32,memory&MAX,*spec['unknown'],limit>>32,limit&MAX,spec['nodes'],*spec['session'],*spec['security'],*spec['proofs'],len(spec['messages'])]
    assert len(words)==33
    result=b'DNS1'+struct.pack('!33I',*words)
    for msg in spec['messages']:
        result+=struct.pack('!6I',msg['id'],msg['phase'],msg['senders'],msg['first'],msg['last'],len(msg['fields']))
        for value in msg['fields']:result+=struct.pack('!8I',*value)
    return result

def oracle_measure(value,wire):
    _,kind,minimum,maximum,length,*_=value
    widths={1:(1,1),2:(2,2),3:(4,4),4:(8,8),5:(1,1),6:(0,length),7:(0,length),8:(1,10),9:(1,10)}
    low,high=widths[kind]
    if wire==2:return minimum*(8+low),maximum*(8+high)
    prefix=0 if minimum==maximum==1 else 4
    if kind in (6,7):low+=4;high+=4
    return prefix+minimum*low,prefix+maximum*high

def oracle(spec):
    # Evaluate every interval change point, unlike the native nested scan.
    low,high=spec['version'][2],spec['version'][1];points={low,high}
    for msg in spec['messages']:
        points|={msg['first'],msg['last']+1}
        for value in msg['fields']:points|={value[5],value[6]+1}
    sizes=[];required=spec['transport'][0]|16;proofs=spec['proofs'][0]|431
    for version in points:
        if not low<=version<=high:continue
        for msg in spec['messages']:
            if not msg['first']<=version<=msg['last']:continue
            parts=[oracle_measure(v,spec['wire'][0]) for v in msg['fields'] if v[5]<=version<=v[6]]
            header=12 if spec['wire'][0]==2 else 4
            sizes.append((header+sum(p[0] for p in parts),header+sum(p[1] for p in parts)))
    if any(spec['security']):required|=4
    if any(v[7]&1 for msg in spec['messages'] for v in msg['fields']):required|=4
    if spec['wire'][3]:proofs|=16
    if spec['session'][4]:proofs|=64
    if required&4:proofs|=1024
    unsupported=(1 if spec['security'][0]==2 else 0)|(2 if spec['security'][3]==2 else 0)
    return min(p[0] for p in sizes),max(p[1] for p in sizes)+spec['unknown'][1],required,proofs,unsupported,1+sum(1+len(m['fields']) for m in spec['messages'])

class SpecTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory(prefix='dln-spec-');directory=Path(cls.directory.name)
        compiler=os.environ.get('DLM_NETWORK_DLM',str(ROOT/'target/release'/('dlm.exe' if os.name=='nt' else 'dlm')))
        output=directory/('spec.dll' if os.name=='nt' else 'spec.so')
        result=subprocess.run([compiler,'build',str(HERE/'dln_spec.dlm'),'--lib',str(ROOT/'lib'),'--allow-unsafe','--target','sharedlib','-o',str(output)],capture_output=True,text=True)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        cls.api=C.CDLL(str(output))
        signatures={'spec_check':(Check,[PTR,SZ]),'spec_admit':(Admission,[PTR,SZ,U32]),'spec_negotiate':(Negotiation,[PTR,SZ,PTR,SZ,C.c_bool,U32]),'spec_roundtrip':(I32,[PTR,SZ]),'spec_short_output_preserved':(C.c_bool,[PTR,SZ]),'spec_inactive_tail_is_semantically_ignored':(C.c_bool,[PTR,SZ]),'spec_field_measure':(Measure,[U32,U32,U32,U32,U32]),'spec_model_error':(Check,[U32]),'dlm_native_last_error':(C.c_char_p,[]),'dlm_native_live_allocations':(U64,[])}
        for name,(result,args) in signatures.items():function=getattr(cls.api,name);function.restype,function.argtypes=result,args
    @classmethod
    def tearDownClass(cls):cls.directory.cleanup()
    def tearDown(self):self.assertIsNone(self.api.dlm_native_last_error());self.assertEqual(self.api.dlm_native_live_allocations(),0)
    def invoke(self,name,spec,*args):
        blob=encode(spec) if isinstance(spec,dict) else spec;data=(C.c_uint8*len(blob)).from_buffer_copy(blob);return getattr(self.api,name)(data,len(blob),*args)
    def check(self,spec):return self.invoke('spec_check',spec)
    def admit(self,spec,available=63):return self.invoke('spec_admit',spec,available)
    def negotiate(self,a,b,initiator=True,available=63):
        first,second=encode(a),encode(b);x=(C.c_uint8*len(first)).from_buffer_copy(first);y=(C.c_uint8*len(second)).from_buffer_copy(second);return self.api.spec_negotiate(x,len(first),y,len(second),initiator,available)
    def valid(self,spec):
        result=self.check(spec);self.assertEqual(result.status,0)
        expected=oracle(spec);self.assertEqual((result.minimum,result.maximum,result.required_properties,result.required_proofs,result.unsupported_security,result.schema_nodes),expected);self.assertEqual(result.pending_proofs,result.required_proofs);return result
    def test_all_field_encodings_counts_and_limits(self):
        for wire in (1,2):
            for kind in range(1,10):
                for minimum,maximum in ((0,1),(1,1),(0,4096),(2,3)):
                    length=77 if kind in (6,7) else 0;value=field(kind=kind,minimum=minimum,maximum=maximum,length=length)
                    result=self.api.spec_field_measure(kind,minimum,maximum,length,wire);self.assertEqual((result.minimum,result.maximum,result.status),(*oracle_measure(value,wire),0))
        for args in ((0,1,1,0,1),(10,1,1,0,1),(1,2,1,0,1),(3,1,1,1,1),(6,0,4097,10,1)):
            self.assertEqual(self.api.spec_field_measure(*args).status,4)
    def test_canonical_metadata_roundtrip_and_atomic_short_write(self):
        spec=sample([field(1,4),field(2,7,0,3,128)]);self.valid(spec)
        self.assertEqual(self.invoke('spec_roundtrip',spec),0);self.assertTrue(self.invoke('spec_short_output_preserved',spec));self.assertTrue(self.invoke('spec_inactive_tail_is_semantically_ignored',spec))
    def test_all_truncated_prefixes_trailing_bytes_and_bad_magic(self):
        blob=encode(sample([field(1,4),field(2,6,0,1,32)]))
        self.assertEqual(self.api.spec_check(None,0).status,1)
        for size in range(len(blob)):self.assertEqual(self.check(blob[:size]).status,1)
        for blob in (blob+b'\0',b'BAD!'+blob[4:],b'1SND'+blob[4:]):self.assertEqual(self.check(blob).status,4)
        self.assertEqual(self.check(b'\0'*16905).status,7)
    def test_noncanonical_booleans_counts_and_memory_are_rejected(self):
        baseline=encode(sample())
        for word,value,expected in ((25,2,4),(27,2,4),(28,2,4),(32,0,4),(32,17,4),(14,1,7),(15,(64<<20)+1,7)):
            blob=bytearray(baseline);struct.pack_into('!I',blob,4+word*4,value);self.assertEqual(self.check(bytes(blob)).status,expected)
        blob=bytearray(baseline);struct.pack_into('!I',blob,136+20,33);self.assertEqual(self.check(bytes(blob)).status,4)
    def test_structured_diagnostics_for_in_memory_bad_models(self):
        expected={1:(4,6),2:(4,6),3:(4,7),4:(4,7),5:(4,1),6:(4,8),7:(4,3),8:(7,3),9:(7,3),10:(4,5),11:(4,7)}
        for mode,pair in expected.items():result=self.api.spec_model_error(mode);self.assertEqual((result.status,result.component),pair)
    def test_resource_overflow_and_schema_node_budgets(self):
        spec=sample([field(kind=6,minimum=4096,maximum=4096,length=16777216)]);self.assertEqual(self.check(spec).status,7)
        spec=sample();spec['nodes']=2;self.assertEqual(self.check(spec).status,7)
        spec=sample();spec['messages_limit']=0;self.assertEqual(self.check(spec).status,4)
        spec=sample();spec['bounds'][0]=23;self.assertEqual(self.check(spec).status,7)
        spec=sample();spec['messages_limit']=(1<<64)-1;self.valid(spec);self.assertEqual(self.invoke('spec_roundtrip',spec),0)
    def test_all_minor_intervals_count_toward_peak_not_just_current(self):
        spec=sample([field(1,6,0,1,4000,last=0),field(2,6,0,1,5,first=1)],minor=2);result=self.valid(spec);self.assertEqual(result.maximum,4020)
        spec['bounds'][0]=100;self.assertEqual(self.check(spec).status,7)
    def test_version_gaps_future_fields_and_parent_intervals(self):
        for messages in ([message(first=1)],[message(last=0),message(2,first=2)],[message(fields=[field(first=3)])]):
            spec=sample(minor=2);spec['messages']=messages;self.assertEqual(self.check(spec).status,4)
        spec=sample(minor=2);spec['messages']=[message(fields=[field(last=0)],last=0),message(2,fields=[field(first=1)],first=1)];self.valid(spec)
        spec=sample();spec['messages'][0]['last']=0;self.assertEqual(self.check(spec).status,4)
    def test_sensitive_fields_force_secure_transport_without_downgrade(self):
        spec=sample([field(flags=1)]);self.valid(spec);result=self.admit(spec);self.assertEqual((result.status,result.choice.profile),(0,6));self.assertTrue(result.requirements.required&4)
        self.assertEqual(self.admit(spec,5).status,6)
        spec['transport'][1]=4;self.assertEqual(self.check(spec).status,4)
    def test_unknown_memory_and_unsupported_security_are_explicit(self):
        spec=sample();spec['transport'][0]|=256;self.valid(spec);self.assertEqual(self.admit(spec).status,6)
        for authentication,replay,mask in ((2,0,1),(0,2,2),(2,2,3)):
            spec=sample();spec['security']=[authentication,1,1,replay];self.valid(spec);result=self.admit(spec);self.assertEqual((result.status,result.unsupported_security),(6,mask))
        spec=sample();spec['security']=[1,1,1,1];spec['session'][4]=1;check=self.valid(spec);self.assertTrue(check.required_proofs&64);self.assertEqual(self.admit(spec).status,0)
    def test_proof_requests_remain_pending_and_required_assurance_is_blocked(self):
        spec=sample();spec['proofs'][0]=2047;check=self.valid(spec);self.assertEqual(check.pending_proofs,2047);spec['proofs'][1]=2;result=self.admit(spec);self.assertEqual((result.status,result.choice.status,result.choice.profile,result.pending_proofs),(6,6,0,2047));self.assertEqual(self.invoke('spec_roundtrip',spec),0)
    def test_selection_uses_derived_wire_size_and_hard_properties(self):
        spec=sample();result=self.admit(spec);self.assertEqual((result.status,result.choice.profile,result.bounds.max_message),(0,3,24))
        spec['transport'][0]=19;result=self.admit(spec);self.assertEqual((result.status,result.choice.profile),(0,1))
        spec['transport'][0]=23;result=self.admit(spec);self.assertEqual((result.status,result.choice.profile),(0,4))
        spec['bounds'][2]=1;spec['transport'][0]=24;self.assertEqual(self.check(spec).status,7)
    def test_version_negotiation_uses_highest_shared_minor(self):
        a,b=sample(minor=5,minimum=1),sample(minor=3,minimum=2);self.valid(a);self.valid(b);result=self.negotiate(a,b);self.assertEqual((result.status,result.minor),(0,3))
        a['version'][2]=4;self.assertEqual(self.negotiate(a,b).status,11)
        a=sample();b=sample();b['version'][0]=2;self.assertEqual(self.negotiate(a,b).status,11);b=sample();b['protocol']+=1;self.assertEqual(self.negotiate(a,b).status,11)
    def test_new_fields_after_negotiated_minor_do_not_break_old_peers(self):
        old=sample();new=sample([field(),field(2,6,0,1,128,first=1)],minor=1)
        for initiator in (False,True):result=self.negotiate(new,old,initiator);self.assertEqual((result.status,result.minor,result.bounds.max_message),(0,0,24))
    def test_directional_cardinality_and_kind_inclusion(self):
        a=sample([field(minimum=1,maximum=2)]);b=sample([field(minimum=0,maximum=3)]);a['messages'][0]['senders']=b['messages'][0]['senders']=1
        self.assertEqual(self.negotiate(a,b,True).status,0);self.assertEqual(self.negotiate(a,b,False).status,11)
        b['messages'][0]['fields'][0][1]=4;result=self.negotiate(a,b);self.assertEqual((result.status,result.message,result.field),(11,1,1))
    def test_directional_unknown_fields_budget_and_collision(self):
        a=sample([field(),field(2,6,0,1,128)]);b=sample();a['messages'][0]['senders']=b['messages'][0]['senders']=1
        self.assertEqual(self.negotiate(a,b).status,11);b['wire'][3]=1;b['unknown']=[2,256];result=self.negotiate(a,b);self.assertEqual((result.status,result.pending_proofs&16),(0,16))
        b['unknown'][1]=8;self.assertEqual(self.negotiate(a,b).status,7)
        b['unknown']=[2,256];a['wire'][3]=1;a['unknown']=[1,8];a['messages'][0]['senders']=b['messages'][0]['senders']=3
        self.assertEqual(self.negotiate(a,b).status,11)
    def test_positional_format_rejects_optional_layout_mismatch(self):
        a=sample([field(minimum=1,maximum=1)]);b=sample([field(minimum=0,maximum=1)]);a['wire']=b['wire']=[1,1,0,0];a['messages'][0]['senders']=b['messages'][0]['senders']=1
        self.valid(a);self.valid(b);self.assertEqual(self.negotiate(a,b).status,11)
        b=copy.deepcopy(a);self.assertEqual(self.negotiate(a,b).status,0);b['wire'][1]=2;self.assertEqual(self.negotiate(a,b).status,11)
    def test_session_phase_and_sender_mismatch(self):
        a=sample();b=sample();b['messages'][0]['phase']=1;self.assertEqual(self.negotiate(a,b).status,11)
        b=sample();b['messages'][0]['senders']=1;self.assertEqual(self.negotiate(a,b,False).status,11)
        self.assertEqual(self.negotiate(a,b,True).status,0)
        spec=sample();spec['session'][1]=1;self.assertEqual(self.check(spec).status,4)
    def test_conflicting_requirements_and_optional_preferences(self):
        a=sample();b=sample();a['transport'][0]|=1;b['transport'][1]|=1;self.assertEqual(self.negotiate(a,b).status,11)
        a=sample();b=sample();a['transport'][2]=1;b['transport'][1]=1;result=self.negotiate(a,b);self.assertEqual((result.status,result.requirements.preferred),(0,0))
    def test_session_limits_are_intersected_without_widening_either_peer(self):
        a=sample();b=sample();a['session']=[2000,30000,40000,16,0];b['session']=[10000,20000,50000,8,1];b['security'][0]=1;a['messages_limit']=(1<<64)-1;b['messages_limit']=123456789012345
        result=self.negotiate(a,b);self.assertEqual(result.status,0);self.assertEqual((result.session.handshake_timeout_ms,result.session.idle_timeout_ms,result.session.maximum_duration_ms,result.session.maximum_in_flight,result.session.authentication_before_data),(2000,20000,40000,8,True));self.assertEqual(result.maximum_session_messages,b['messages_limit'])
    def test_large_canonical_descriptor_at_exact_capacity(self):
        spec=sample();spec['messages']=[message(i+1,[field(j+1,1) for j in range(32)]) for i in range(16)];blob=encode(spec);self.assertEqual(len(blob),16904);check=self.valid(spec);self.assertEqual(check.schema_nodes,529);self.assertEqual(self.invoke('spec_roundtrip',blob),0)
    def test_random_valid_descriptors_match_independent_interval_oracle(self):
        rng=random.Random(141)
        for _ in range(400):
            minor=rng.randrange(5);spec=sample(fields=[],minor=minor);spec['wire']=[1,rng.choice((1,2)),0,0] if rng.randrange(2) else [2,2,rng.choice((1,2)),0]
            values=[]
            for index in range(rng.randrange(9)):
                kind=rng.randrange(1,10);minimum=rng.randrange(2);maximum=rng.randrange(max(1,minimum),5);first=rng.randrange(minor+1);last=rng.randrange(first,minor+1) if rng.randrange(2) else MAX
                values.append(field(index+1,kind,minimum,maximum,rng.randrange(64) if kind in (6,7) else 0,first,last,rng.randrange(2)))
            spec['messages'][0]['fields']=values;self.valid(spec);self.assertEqual(self.invoke('spec_roundtrip',spec),0)
    def test_mutated_metadata_never_traps_or_leaves_allocations(self):
        rng=random.Random(142);baseline=encode(sample([field(),field(2,6,0,1,64)]))
        for _ in range(1200):
            blob=bytearray(baseline)
            for _ in range(rng.randrange(1,6)):blob[rng.randrange(len(blob))]=rng.randrange(256)
            if rng.randrange(4)==0:blob=blob[:rng.randrange(len(blob)+1)]
            result=self.check(bytes(blob));self.assertIn(result.status,(0,1,4,7))
            if result.status==0:self.assertEqual(self.invoke('spec_roundtrip',bytes(blob)),0)
            self.assertIsNone(self.api.dlm_native_last_error())

if __name__=='__main__':unittest.main(verbosity=2)
