#!/usr/bin/env python3
import ctypes as C
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import socket
import struct
import subprocess
import tempfile
import time
import unittest
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value
base=module('nodes_base',HERE/'network.py')
compat=module('nodes_compat',ROOT/'tools/check_dln_compatibility.py')
class Id(C.Structure):_fields_=[('bytes',C.c_uint8*32)]
class Locator(C.Structure):_fields_=[('address',base.Address),('profiles',C.c_uint32),('priority',C.c_uint32)]
class Record(C.Structure):
    _fields_=[('identity',Id),('root',Id),('key',Id)]+[(n,C.c_uint64) for n in ('epoch','sequence','not_before','key_expires','issued','expires')]+[('locator_count',C.c_uint32),('locators',Locator*8),('delegation',C.c_uint8*64),('signature',C.c_uint8*64)]
class RecordResult(C.Structure):_fields_=[('record',Record),('status',C.c_int32),('system',C.c_int32)]
class IdResult(C.Structure):_fields_=[('value',Id),('status',C.c_int32),('system',C.c_int32)]
class Info(C.Structure):_fields_=[('id',Id),('size',C.c_uint64),('leaves',C.c_uint32),('kind',C.c_uint32),('status',C.c_int32),('system',C.c_int32)]
class Child(C.Structure):_fields_=[('id',Id),('size',C.c_uint64),('leaves',C.c_uint32),('reserved',C.c_uint32)]
class Proof(C.Structure):_fields_=[('root',Id),('leaf',Id),('total_size',C.c_uint64),('leaf_size',C.c_uint64)]+[(n,C.c_uint32) for n in ('index','leaf_count','depth','reserved')]+[('siblings',Child*32),('sides',C.c_uint32*32)]
class ProofResult(C.Structure):_fields_=[('proof',Proof),('status',C.c_int32),('system',C.c_int32)]
class Stats(C.Structure):_fields_=[('used',C.c_size_t),('limit',C.c_size_t),('generation',C.c_uint64),('entries',C.c_uint32),('present',C.c_uint32),('status',C.c_int32),('system',C.c_int32)]
class DiscoveryStats(C.Structure):_fields_=[(n,C.c_uint64) for n in ('sent','received','rejected','timeouts')]+[('used',C.c_size_t),('limit',C.c_size_t),('peers',C.c_uint32),('pins',C.c_uint32),('status',C.c_int32),('system',C.c_int32)]
def ident(value):return Id((C.c_uint8*32).from_buffer_copy(value))
def address(port):
    value=base.Address(4,port,0);value.bytes[:4]=[127,0,0,1];return value

def public(key):return key.public_key().public_bytes(serialization.Encoding.Raw,serialization.PublicFormat.Raw)
def record_wire(root,key,port,epoch=1,sequence=1,issued=None):
    now=int(time.time()) if issued is None else issued;root_public=public(root);current=public(key);identity=hashlib.sha256(b'DLN-ID1'+root_public).digest();first=now-10;last=now+3600
    delegation=root.sign(b'DLN-KEY1'+identity+struct.pack('>Q',epoch)+current+struct.pack('>QQ',first,last))
    prefix=b'DLR1'+struct.pack('>I',1)+identity+root_public+current+struct.pack('>6QI',epoch,sequence,first,last,now,now+300,1)+delegation
    locator=struct.pack('>III',4,port,0)+socket.inet_aton('127.0.0.1')+bytes(12)+struct.pack('>II',4,0);unsigned=prefix+locator
    return unsigned+key.sign(unsigned)
def content_id(raw):return hashlib.sha256(b'DLN-CONTENT1'+raw).digest()
def block(data):return b'DCB1'+struct.pack('>IQII',1,len(data),0,0)+data
def tree_id(data,chunk):
    level=[(content_id(block(data[i:i+chunk])),len(data[i:i+chunk]),1) for i in range(0,len(data),chunk)] or [(content_id(block(b'')),0,1)]
    while len(level)>1:
        out=[]
        for i in range(0,len(level),2):
            if i+1==len(level):out.append(level[i]);continue
            a,b=level[i:i+2];raw=b'DCB1'+struct.pack('>IQII',2,a[1]+b[1],2,0)+b''.join(x[0]+struct.pack('>QII',x[1],x[2],0) for x in (a,b));out.append((content_id(raw),a[1]+b[1],a[2]+b[2]))
        level=out
    return level[0][0]

class NodesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='dln-nodes-');path=Path(cls.temp.name);source=path/'nodes.c';source.write_text('#include "'+str(ROOT/'crates/dlm_core/src/native/network.c')+'"\n#include "'+str(ROOT/'crates/dlm_core/src/native/nodes.c')+'"\n');library=path/('nodes.dll' if os.name=='nt' else 'nodes.so')
        command=[os.environ.get('CC','cc'),'-std=gnu11','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-shared',str(source),'-o',str(library)]+(['-lws2_32'] if os.name=='nt' else ['-fPIC','-ldl','-pthread']);subprocess.run(command,check=True,capture_output=True);cls.api=C.CDLL(str(library))
        signatures={
          'key_export':(base.Io,[C.c_void_p,C.c_void_p,base.U8P,C.c_size_t]),'key_restore':(base.Opened,[C.c_void_p,base.U8P,C.c_size_t]),'key':(base.Opened,[base.U8P,C.c_size_t,C.c_bool]),'key_close':(None,[C.c_void_p]),'public':(IdResult,[C.c_void_p]),'sign':(base.Io,[C.c_void_p,base.U8P,C.c_size_t,base.U8P,C.c_size_t]),'verify':(base.Io,[Id,base.U8P,C.c_size_t,base.U8P,C.c_size_t]),
          'signer':(base.Opened,[C.c_void_p,C.c_void_p,C.c_uint64,C.c_uint64,C.c_uint64]),'signer_close':(None,[C.c_void_p]),'record_decode':(RecordResult,[base.U8P,C.c_size_t,C.c_uint64]),'record_encode':(base.Io,[Record,base.U8P,C.c_size_t]),'record':(RecordResult,[C.c_void_p,C.POINTER(Locator),C.c_size_t,C.c_uint64,C.c_uint32]),
          'trust_new':(base.Opened,[C.c_uint32,C.c_size_t]),'trust_close':(None,[C.c_void_p]),'trust_pin':(base.Io,[C.c_void_p,Id]),'trust_accept':(base.Io,[C.c_void_p,Record,C.c_uint64]),'trust_stats':(Stats,[C.c_void_p]),
          'content_new':(base.Opened,[C.c_uint32,C.c_size_t]),'content_close':(None,[C.c_void_p]),'content_tree':(Info,[C.c_void_p,base.U8P,C.c_size_t,C.c_uint32]),'content_proof':(ProofResult,[C.c_void_p,Id,C.c_uint32]),'content_verify':(base.Io,[Id,Proof,base.U8P,C.c_size_t]),'content_import':(Info,[C.c_void_p,Id,base.U8P,C.c_size_t]),'content_stats':(Stats,[C.c_void_p]),
          'discovery_new':(base.Opened,[base.Opened]*4+[base.Address,Id]+[C.c_uint32]*5+[C.c_size_t]),'discovery_close':(None,[C.c_void_p]),'discovery_record':(RecordResult,[C.c_void_p]),'discovery_bootstrap':(base.Io,[C.c_void_p,Record]),'discovery_poll':(base.Io,[C.c_void_p,C.c_uint32,C.c_void_p]),'discovery_publish':(Info,[C.c_void_p,base.U8P,C.c_size_t,C.c_uint32]),'discovery_stats':(DiscoveryStats,[C.c_void_p]),
        }
        for name,(result,args) in signatures.items():
            fn=getattr(cls.api,'dlm_node_'+name);fn.restype,fn.argtypes=result,args;setattr(cls,name,staticmethod(fn))
        cls.api.dlm_net_crypto_secret.restype=base.Opened;cls.api.dlm_net_crypto_secret.argtypes=[base.U8P,C.c_size_t,C.c_uint32,C.c_bool];cls.api.dlm_net_crypto_close.argtypes=[C.c_void_p];cls.api.dlm_net_crypto_shutdown.restype=base.Io;cls.api.dlm_net_provider_shutdown.restype=base.Io
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def setUp(self):self.owners=[]
    def tearDown(self):
        for close,handle in reversed(self.owners):close(handle)
        self.assertEqual(self.api.dlm_net_crypto_shutdown().status,0);self.assertEqual(self.api.dlm_net_provider_shutdown().status,0)
    def own(self,opened,close):
        self.assertEqual(opened.status,0);self.assertTrue(opened.handle);self.owners.append((close,opened.handle));return opened
    def make_key(self,seed):return self.own(self.key(base.octets(seed),32,False),self.key_close)
    def test_openssl_ed25519_differential(self):
        rng=random.Random(144)
        for n in (0,1,31,32,63,64,127,1024,65537):
            seed=rng.randbytes(32);message=rng.randbytes(n);k=self.make_key(seed);p=self.public(k.handle);oracle=Ed25519PrivateKey.from_private_bytes(seed);self.assertEqual(bytes(p.value.bytes),public(oracle));out=(C.c_uint8*64)();self.assertEqual(self.sign(k.handle,base.octets(message),n,out,64).status,0);self.assertEqual(bytes(out),oracle.sign(message));self.assertEqual(self.verify(p.value,base.octets(message),n,base.octets(oracle.sign(message)),64).status,0)
    def test_key_backup_aead_matches_independent_oracle(self):
        seed=bytes(range(32));root=self.make_key(seed);master=b'M'*32;secret=self.own(self.api.dlm_net_crypto_secret(base.octets(master),32,1,False),self.api.dlm_net_crypto_close);output=(C.c_uint8*136)();saved=self.key_export(root.handle,secret.handle,output,136);self.assertEqual(saved.status,0);wire=bytes(output);key=HKDF(algorithm=hashes.SHA256(),length=32,salt=wire[8:40],info=b'DLN-KEYSTORE1').derive(master);self.assertEqual(AESGCM(key).decrypt(wire[40:52],wire[88:],wire[:88]),seed);restored=self.own(self.key_restore(secret.handle,output,136),self.key_close);self.assertEqual(bytes(self.public(restored.handle).value.bytes),bytes(self.public(root.handle).value.bytes))
        for i in range(len(wire)):
            bad=bytearray(wire);bad[i]^=1;self.assertNotEqual(self.key_restore(secret.handle,base.octets(bad),len(bad)).status,0)
        second=(C.c_uint8*136)();self.assertEqual(self.key_export(root.handle,secret.handle,second,136).status,0);self.assertNotEqual(bytes(output),bytes(second));guard=(C.c_uint8*136)(*([165]*136));self.assertEqual(self.key_export(root.handle,secret.handle,guard,135).status,10);self.assertEqual(bytes(guard),bytes([165]*136))
    def test_independently_signed_locator_chain(self):
        root=Ed25519PrivateKey.from_private_bytes(bytes(range(32)));key=Ed25519PrivateKey.from_private_bytes(bytes(range(32,64)));wire=record_wire(root,key,19009);decoded=self.record_decode(base.octets(wire),len(wire),int(time.time()));self.assertEqual(decoded.status,0);out=(C.c_uint8*572)();r=self.record_encode(decoded.record,out,572);self.assertEqual(bytes(out[:r.count]),wire);self.assertEqual(bytes(decoded.record.identity.bytes),hashlib.sha256(b'DLN-ID1'+public(root)).digest())
        store=self.own(self.trust_new(8,16384),self.trust_close);self.assertEqual(self.trust_accept(store.handle,decoded.record,int(time.time())).status,12);self.assertEqual(self.trust_pin(store.handle,ident(public(root))).status,0);self.assertEqual(self.trust_accept(store.handle,decoded.record,int(time.time())).status,0)
        for i in range(len(wire)):
            changed=bytearray(wire);changed[i]^=1;self.assertNotEqual(self.record_decode(base.octets(changed),len(changed),int(time.time())).status,0)
    def test_noncanonical_scalar_and_small_order_keys(self):
        seed=bytes(range(32));k=self.make_key(seed);p=self.public(k.handle);message=b'canonical signatures';signature=bytearray(Ed25519PrivateKey.from_private_bytes(seed).sign(message));order=2**252+27742317777372353535851937790883648493;signature[32:]=(int.from_bytes(signature[32:],'little')+order).to_bytes(32,'little');self.assertEqual(self.verify(p.value,base.octets(message),len(message),base.octets(signature),64).status,12)
        for point in (bytes(32),b'\1'+bytes(31),(2**255-19).to_bytes(32,'little')):
            self.assertEqual(self.verify(ident(point),None,0,base.octets(bytes(64)),64).status,12)
    def test_merkle_roots_and_each_leaf_against_hashlib(self):
        cache=self.own(self.content_new(512,2097152),self.content_close);rng=random.Random(1314)
        for n in (0,1,255,256,257,511,512,513,2049,16003):
            data=rng.randbytes(n)
            for chunk in (256,512,1024):
                result=self.content_tree(cache.handle,base.octets(data),n,chunk);self.assertEqual(result.status,0);expected=tree_id(data,chunk);self.assertEqual(bytes(result.id.bytes),expected)
                for i in range(result.leaves):
                    proof=self.content_proof(cache.handle,result.id,i);self.assertEqual(proof.status,0);leaf=data[i*chunk:(i+1)*chunk];self.assertEqual(self.content_verify(ident(expected),proof.proof,base.octets(leaf),len(leaf)).status,0);bad=bytearray(expected);bad[0]^=1;self.assertNotEqual(self.content_verify(ident(bad),proof.proof,base.octets(leaf),len(leaf)).status,0)
        stats=self.content_stats(cache.handle);self.assertLessEqual(stats.used,stats.limit)
    def test_hostile_content_has_no_cache_side_effects(self):
        cache=self.own(self.content_new(8,32768),self.content_close);rng=random.Random(135)
        for n in range(400):
            data=rng.randbytes(n);result=self.content_import(cache.handle,ident(hashlib.sha256(data).digest()),base.octets(data),n);self.assertNotEqual(result.status,0);self.assertEqual(self.content_stats(cache.handle).entries,0)
    def test_encrypted_signed_dht_against_independent_udp_peer(self):
        root=self.make_key(bytes(range(32)));current=self.make_key(bytes(range(32,64)));now=int(time.time());signer=self.signer(root.handle,current.handle,1,now-1,now+3600);self.owners.remove((self.key_close,current.handle));self.own(signer,self.signer_close)
        peer_root=Ed25519PrivateKey.from_private_bytes(b'R'*32);peer_key=Ed25519PrivateKey.from_private_bytes(b'K'*32);trust=self.own(self.trust_new(8,16384),self.trust_close);self.assertEqual(self.trust_pin(trust.handle,ident(public(peer_root))).status,0);cache=self.own(self.content_new(128,1048576),self.content_close);group=b'G'*32;secret=self.own(self.api.dlm_net_crypto_secret(base.octets(group),32,1,False),self.api.dlm_net_crypto_close);network=b'N'*32
        node=self.discovery_new(signer,trust,cache,secret,address(0),ident(network),300,8,8,3,256,8388608)
        for owned in (signer,trust,cache,secret):self.owners=[x for x in self.owners if x[1]!=owned.handle]
        self.own(node,self.discovery_close);own=self.discovery_record(node.handle);self.assertEqual(own.status,0);endpoint=('127.0.0.1',own.record.locators[0].address.port)
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as peer:
            peer.bind(('127.0.0.1',0));peer.settimeout(.2);record=record_wire(peer_root,peer_key,peer.getsockname()[1]);decoded=self.record_decode(base.octets(record),len(record),now);self.assertEqual(decoded.status,0);self.assertEqual(self.discovery_bootstrap(node.handle,decoded.record).status,0);sender=bytes(decoded.record.identity.bytes);salt=b'S'*32;key=HKDF(algorithm=hashes.SHA256(),length=32,salt=salt,info=b'DLN-DHT1').derive(group);counter=0
            def query(kind,target,offset=0):
                nonlocal counter
                counter+=1;nonce=counter.to_bytes(16,'big');inner=b'DND1'+struct.pack('>III',1,kind,0)+sender+nonce+target+struct.pack('>QIIQQ',int(time.time()),len(record),0,offset,0)+record;outer=b'DNE1'+struct.pack('>I',1)+network+salt+struct.pack('>QII',counter,len(inner)+64,0);signature=peer_key.sign(outer+inner);wire=outer+AESGCM(key).encrypt(b'DNE1'+counter.to_bytes(8,'big'),inner+signature,outer);peer.sendto(wire,endpoint);self.assertEqual(self.discovery_poll(node.handle,20,None).status,0);response,_=peer.recvfrom(9000);self.assertEqual(response[:8],b'DNE1\0\0\0\1');self.assertEqual(response[8:40],network);received_counter=int.from_bytes(response[72:80],'big');derived=HKDF(algorithm=hashes.SHA256(),length=32,salt=response[40:72],info=b'DLN-DHT1').derive(group);plain=AESGCM(derived).decrypt(b'DNE1'+received_counter.to_bytes(8,'big'),response[88:],response[:88]);self.assertEqual(plain[48:64],nonce);self.assertEqual(plain[64:96],target);n=int.from_bytes(plain[104:108],'big');remote=plain[128:128+n];Ed25519PublicKey.from_public_bytes(remote[72:104]).verify(plain[-64:],response[:88]+plain[:-64]);return plain,wire
            response,captured=query(1,bytes(own.record.identity.bytes));self.assertEqual(int.from_bytes(response[8:12],'big'),2);peer.sendto(captured,endpoint);self.discovery_poll(node.handle,0,None)
            with self.assertRaises(socket.timeout):peer.recvfrom(9000)
            self.assertGreaterEqual(self.discovery_stats(node.handle).rejected,1)
            data=random.Random(150).randbytes(16003);published=self.discovery_publish(node.handle,base.octets(data),len(data),16384);self.assertEqual(published.status,0);received=bytearray()
            while len(received)<len(data)+24:
                response,_=query(3,bytes(published.id.bytes),len(received));self.assertEqual(int.from_bytes(response[12:16],'big'),0);self.assertEqual(int.from_bytes(response[120:128],'big'),len(data)+24);n=int.from_bytes(response[104:108],'big');received.extend(response[128+n:-64])
            self.assertEqual(received,block(data));self.assertEqual(content_id(received),bytes(published.id.bytes));self.assertLessEqual(self.discovery_stats(node.handle).used,self.discovery_stats(node.handle).limit)

class CompatibilityTests(unittest.TestCase):
    def test_complete_previous_release_is_preserved(self):
        result=compat.check(ROOT,ROOT/'docs/DLN_BASELINE_0.143.0.json');self.assertEqual(result['status'],'passed',result['failures']);self.assertGreaterEqual(result['protected_files'],158)
    def test_emitter_extension_exception_cannot_mask_old_body_changes(self):
        current=(ROOT/'crates/dlm_core/src/native/emit.rs').read_bytes();bridge=b'    if m.unit.functions.iter().any(|f| f.library.as_deref() == Some("dlm.nodes.v1")) {\n        out.push_str(include_str!("nodes.c"));\n    }\n';extended=b'    if m.unit.functions.iter().any(|f| matches!(f.library.as_deref(),Some("dlm.net.v1" | "dlm.nodes.v1"))) {\n';original=b'    if m.unit.functions.iter().any(|f| f.library.as_deref() == Some("dlm.net.v1")) {\n';old=current.replace(bridge,b'').replace(extended,original)
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);path=root/'crates/dlm_core/src/native/emit.rs';path.parent.mkdir(parents=True);(root/'network.c').write_text('int dlm_net_stable(void){return 1;}\n');snapshot=root/'baseline.json';snapshot.write_text(json.dumps({'format':'DLM-DLN-COMPAT-v1','baseline_version':'0.143.0','protected_files':{'crates/dlm_core/src/native/emit.rs':compat.digest(old)},'network_c_source':'network.c','network_c_abi':compat.c_functions((root/'network.c').read_text())}))
            for value,expected in ((old,'passed'),(current,'passed'),(current+b'// changed old compiler\n','failed'),(current+bridge,'failed'),(current.replace(b'include_str!("nodes.c")',b'include_str!("other.c")'),'failed')):
                path.write_bytes(value);self.assertEqual(compat.check(root,snapshot)['status'],expected)

if __name__=='__main__':unittest.main(verbosity=2)
