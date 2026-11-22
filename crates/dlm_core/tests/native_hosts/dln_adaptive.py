#!/usr/bin/env python3
import ctypes as C
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import unittest
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
COMPILER=Path(os.environ.get('DLM_NETWORK_DLM',ROOT/'target/release'/('dlm.exe' if os.name=='nt' else 'dlm')))
CHECKER=COMPILER.with_name('dlm-transport-check.exe' if os.name=='nt' else 'dlm-transport-check')
class Requirements(C.Structure):_fields_=[('required',C.c_uint32),('forbidden',C.c_uint32),('preferred',C.c_uint32)]
class Policy(C.Structure):_fields_=[(n,C.c_uint32) for n in ('delivery','latency','loss','throughput','cost','max_age','min_samples','min_hold','hysteresis')]
class Witness(C.Structure):_fields_=[(n,C.c_uint64) for n in ('id','at','rtt','goodput','samples')]+[(n,C.c_uint32) for n in ('loss','profile','properties','cost')]+[('ready',C.c_bool)]
class Witnesses(C.Structure):_fields_=[('data',Witness*6)]
class Choice(C.Structure):_fields_=[('id',C.c_uint64),('score',C.c_uint64)]+[(n,C.c_uint32) for n in ('profile','properties','index')]+[('status',C.c_int32)]
class Certificate(C.Structure):_fields_=[('data',C.c_uint8*456)]
class Error(C.Structure):_fields_=[('kind',C.c_int32),('system',C.c_int32),('operation',C.c_uint32)]
class Packet(C.Structure):_fields_=[('required',C.c_size_t),('copied',C.c_size_t),('family',C.c_uint32),('ether_type',C.c_uint32),('offset',C.c_uint32),('error',Error)]
def properties(profile,delivery):
    if not 1<=profile<=6 or delivery>2:return 0
    raw=(179,183,208,191,253,244)[profile-1]
    return raw if not delivery else (raw&~71)|5|(2 if delivery==1 else 64)
def valid_policy(p):return p.delivery<=2 and max(p.latency,p.loss,p.throughput,p.cost)<=1000000 and bool(p.latency|p.loss|p.throughput|p.cost) and 1<=p.max_age<=3600000 and 1<=p.min_samples<=1000000 and p.min_hold<=3600000 and p.hysteresis<=100
def oracle(r,p,values,now):
    if (r.required|r.forbidden|r.preferred)&~511 or r.required&r.forbidden or r.preferred&r.forbidden or r.required&66==66 or not valid_policy(p):return (0,0,0,0,0,4)
    choices=[]
    for i,o in enumerate(values.data):
        caps=properties(o.profile,p.delivery)
        if not o.ready or not o.id or not caps or o.properties!=caps or o.cost>1000000 or o.rtt>60000000000 or o.goodput>1000000000000 or o.loss>1000000 or o.samples<p.min_samples or o.at>now or now-o.at>p.max_age or r.required&caps!=r.required or r.forbidden&caps:continue
        score=min(o.rtt//1000,1000000)*p.latency+o.loss*p.loss+min(1000000000//(o.goodput//1000+1),1000000)*p.throughput+o.cost*p.cost
        choices.append(((r.preferred&~caps).bit_count(),score,o.id,i,o.profile,caps))
    if not choices:return (0,0,0,0,0,6)
    _,score,id,i,profile,caps=min(choices)
    return (id,score,profile,caps,i,0)
def tuple_choice(c):return (c.id,c.score,c.profile,c.properties,c.index,c.status)
def packet_oracle(kind,data):
    if kind not in (1,2) or not 1<=len(data)<=65571:return 4
    offset=0
    if kind==2:
        if len(data)<14:return 11
        type=int.from_bytes(data[12:14],'big');offset=14
        for _ in range(2):
            if type not in (0x8100,0x88a8):break
            if len(data)-offset<4:return 11
            type=int.from_bytes(data[offset+2:offset+4],'big');offset+=4
        if type not in (0x800,0x86dd):return 11 if type<=1500 and type>len(data)-offset else 0
        version=4 if type==0x800 else 6
    else:version=data[0]>>4
    raw=data[offset:]
    if version==4:
        if len(raw)<20 or raw[0]>>4!=4 or raw[0]&15<5 or (raw[0]&15)*4>len(raw):return 11
        size=int.from_bytes(raw[2:4],'big')
        if size<(raw[0]&15)*4:return 11
    elif version==6:
        if len(raw)<40 or raw[0]>>4!=6:return 11
        size=40+int.from_bytes(raw[4:6],'big')
    else:return 11
    return 11 if size>len(raw) or (kind==1 and size!=len(raw)) else 0
class AdaptiveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='dln-adaptive-');cls.directory=Path(cls.temp.name);library=cls.directory/('adaptive.dll' if os.name=='nt' else 'adaptive.so');built=subprocess.run([str(COMPILER),'build',str(HERE/'dln_adaptive.dlm'),'--lib',str(ROOT/'lib'),'--allow-unsafe','--target','sharedlib','-o',str(library)],capture_output=True,text=True,timeout=120)
        if built.returncode:raise RuntimeError(built.stdout+built.stderr)
        cls.api=C.CDLL(str(library))
        for name in ('optimizer_dlm','optimizer_native'):
            f=getattr(cls.api,name);f.argtypes=[Requirements,Policy,Witnesses,C.c_uint64];f.restype=Choice
        cls.api.optimizer_dot.argtypes=[Requirements,Policy,Witnesses,C.c_uint64];cls.api.optimizer_dot.restype=Certificate
        cls.api.tun_dlm_validator.argtypes=[C.c_uint32,C.POINTER(C.c_uint8),C.c_size_t];cls.api.tun_dlm_validator.restype=Packet
        for name in ('adaptive_dlm_roundtrip','verified_dlm_contracts','verified_cross_session_replay_is_refused','tun_dlm_invalid_authority'):getattr(cls.api,name).restype=C.c_int32
        cls.api.dlm_native_last_error.restype=C.c_char_p;cls.api.dlm_native_live_allocations.restype=C.c_uint64
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def tearDown(self):self.assertIsNone(self.api.dlm_native_last_error());self.assertEqual(self.api.dlm_native_live_allocations(),0)
    def sample(self,seed):
        rng=random.Random(seed);p=Policy(rng.randrange(3),rng.randrange(1001),rng.randrange(1001),rng.randrange(101),rng.randrange(101),1000,2,100,10);r=Requirements(rng.randrange(512),rng.choice((0,0,8,32,64,256)),rng.choice((0,0,4,8,32)));values=Witnesses()
        for i in range(6):
            profile=rng.randrange(1,7);values.data[i]=Witness(i+1,rng.randrange(0,1200),rng.randrange(60000001),rng.randrange(1000000001),rng.randrange(10),rng.randrange(1000001),profile,properties(profile,p.delivery),rng.randrange(1000001),rng.randrange(10)!=0)
        return r,p,values
    def test_native_and_dlm_choices_match_independent_integer_oracle(self):
        for seed in range(1600):
            r,p,values=self.sample(seed);expected=oracle(r,p,values,1000);self.assertEqual(tuple_choice(self.api.optimizer_dlm(r,p,values,1000)),expected,seed);self.assertEqual(tuple_choice(self.api.optimizer_native(r,p,values,1000)),expected,seed)
    def test_raw_and_composed_property_contracts_never_weaken_requirements(self):
        for mode in range(3):
            p=Policy(mode,1,0,0,1,1000,1,0,0);values=Witnesses()
            for i in range(6):values.data[i]=Witness(i+1,100,1,1000000,1,0,i+1,properties(i+1,mode),i,True)
            for bits in range(512):
                r=Requirements(bits,0,0);actual=self.api.optimizer_dlm(r,p,values,100)
                self.assertEqual(tuple_choice(actual),oracle(r,p,values,100))
                if not actual.status:self.assertEqual(actual.properties&bits,bits)
    def test_invalid_policy_age_and_integer_edges_fail_closed(self):
        for value in (0,1,1000000,1000001,2**32-1):
            r,p,observations=self.sample(value);p.latency=value
            for now in (0,999,1000,2**64-1):self.assertEqual(tuple_choice(self.api.optimizer_dlm(r,p,observations,now)),oracle(r,p,observations,now))
    def test_dlm_certificates_are_checked_by_the_independent_kernel(self):
        for seed in range(40):
            r,p,values=self.sample(seed);data=bytes(self.api.optimizer_dot(r,p,values,1000).data);path=self.directory/'choice.nopt';path.write_bytes(data);run=subprocess.run([str(CHECKER),str(path)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stderr);report=json.loads(run.stdout);self.assertTrue(report['native_pending']);self.assertEqual(report['certificate_sha256'],hashlib.sha256(data).hexdigest());self.assertEqual(report['selected'],oracle(r,p,values,1000)[0])
    def test_forged_selection_noncanonical_flags_and_work_limit_are_rejected(self):
        r=Requirements(4,0,0);p=Policy(1,1000,1,0,1,1000,1,0,0);values=Witnesses()
        for i in range(6):values.data[i]=Witness(i+1,100,1000,1000000,2,0,i+1,properties(i+1,1),i,True)
        good=bytes(self.api.optimizer_dot(r,p,values,100).data);path=self.directory/'forged.nopt'
        for index in range(64,96):
            bad=bytearray(good);bad[index]^=1;path.write_bytes(bad);run=subprocess.run([str(CHECKER),str(path)],capture_output=True,text=True);self.assertNotEqual(run.returncode,0)
        path.write_bytes(good);run=subprocess.run([str(CHECKER),str(path),'--work','5'],capture_output=True,text=True);self.assertEqual(run.returncode,3)
    def test_migration_model_includes_ordered_and_unordered_delivery(self):
        for mode in (1,2):
            run=subprocess.run([str(CHECKER),'--model','4','4',str(mode)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stderr);report=json.loads(run.stdout);self.assertTrue(report['native_pending']);self.assertGreater(report['states'],1000);self.assertEqual(report['model_invariants'],31)
    def test_real_dlm_identity_gated_sessions_and_retained_short_output(self):self.assertEqual(self.api.adaptive_dlm_roundtrip(),1);self.assertEqual(self.api.verified_dlm_contracts(),2)
    def test_persistent_replay_and_unauthorized_tun_mutations_are_refused(self):self.assertEqual(self.api.verified_cross_session_replay_is_refused(),6);self.assertEqual(self.api.tun_dlm_invalid_authority(),4)
    def test_ip_ethernet_vlan_parsing_matches_an_independent_hostile_input_oracle(self):
        rng=random.Random(14516);packets=[bytes(rng.randrange(256) for _ in range(n)) for n in range(600)]
        for n in (0,1,20,39,40,41,1280,1500,65535):
            ipv4=bytearray(max(20,n));ipv4[0]=0x45;ipv4[2:4]=len(ipv4).to_bytes(2,'big');packets.append(bytes(ipv4));ipv6=bytearray(max(40,n));ipv6[0]=0x60;ipv6[4:6]=(len(ipv6)-40).to_bytes(2,'big');packets.append(bytes(ipv6))
            for p in list(packets[-2:]):packets.append(bytes(12)+b'\x08\x00'+p);packets.append(bytes(12)+b'\x81\x00\x00\x01\x86\xdd'+p)
        for kind in (1,2,3):
            for packet in packets:
                raw=(C.c_uint8*len(packet)).from_buffer_copy(packet);actual=self.api.tun_dlm_validator(kind,raw,len(packet));self.assertEqual(actual.error.kind,packet_oracle(kind,packet),(kind,len(packet),packet[:24]))
if __name__=='__main__':unittest.main(verbosity=2)
