#!/usr/bin/env python3
import concurrent.futures
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import queue
import shutil
import socket
import ssl
import struct
import subprocess
import tempfile
import threading
import time
import unittest
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
P=C.c_void_p;N=C.c_size_t;U=C.c_uint32;Q=C.c_uint64
class Opened(C.Structure):_fields_=[('handle',P),('status',C.c_int32),('system',C.c_int32)]
class Io(C.Structure):_fields_=[('count',N),('status',C.c_int32),('system',C.c_int32)]
class Transfer(C.Structure):_fields_=[('required',N),('copied',N),('value',Q),('outcome',U),('status',C.c_int32),('system',C.c_int32)]
class Stats(C.Structure):_fields_=[(n,Q) for n in ('accepted','authenticated','denied','failed','requests','forwarded','returned','active','handshakes')]
def raw_public(key):return key.public_key().public_bytes(serialization.Encoding.Raw,serialization.PublicFormat.Raw)
def wire(kind,body):return struct.pack('>II',kind,len(body))+body
def exact(sock,count):
    out=bytearray()
    while len(out)<count:
        block=sock.recv(count-len(out))
        if not block:raise EOFError('gateway closed the carrier')
        out.extend(block)
    return bytes(out)
def send(sock,data):sock.sendall(b'DLT1'+struct.pack('>III',len(data),0,0)+data)
def receive(sock):
    header=exact(sock,16)
    if header[:4]!=b'DLT1' or header[8:]!=bytes(8):raise ValueError('noncanonical carrier')
    n=struct.unpack('>I',header[4:8])[0]
    if n>65500:raise ValueError('unbounded carrier')
    return exact(sock,n)
def record(identity,port):
    root=Ed25519PrivateKey.from_private_bytes(identity[28:60]);key=Ed25519PrivateKey.from_private_bytes(identity[60:92]);public=raw_public(root);current=raw_public(key);node=hashlib.sha256(b'DLN-ID1'+public).digest();epoch,first,last=struct.unpack('>QQQ',identity[4:28]);now=int(time.time())
    delegated=root.sign(b'DLN-KEY1'+node+struct.pack('>Q',epoch)+current+struct.pack('>QQ',first,last))
    out=b'DLR1'+struct.pack('>I',1)+node+public+current+struct.pack('>6QI',epoch,1,first,last,now,now+300,1)+delegated
    out+=struct.pack('>III',4,port,0)+socket.inet_aton('127.0.0.1')+bytes(12)+struct.pack('>II',2,0)
    return out+key.sign(out)
class Oracle:
    def __init__(self,case,address,minimum=0,maximum=3,mutate=None,finish=True,channel=None,profile=2):
        context=ssl.create_default_context(cafile=str(case.fixtures/'quic-test-ca.pem'));context.minimum_version=ssl.TLSVersion.TLSv1_3
        self.socket=channel or context.wrap_socket(socket.create_connection(tuple(address.rsplit(':',1)[:1])+ (int(address.rsplit(':',1)[1]),),timeout=2),server_hostname='localhost');self.socket.settimeout(2)
        case.addCleanup(self.socket.close)
        identity=case.client_identity.read_bytes();key=Ed25519PrivateKey.from_private_bytes(identity[60:92]);ephemeral=X25519PrivateKey.generate();session=os.urandom(32)
        hello=b'DGN1'+session+os.urandom(32)+raw_public(ephemeral)+bytes.fromhex(case.server_root)+case.schema+struct.pack('>IHH',profile,minimum,maximum)
        locator=record(identity,self.socket.getsockname()[1]);hello+=struct.pack('>I',len(locator))+locator
        if mutate:hello=mutate(hello)
        hello=wire(1,hello+key.sign(b'DLN-GATEWAY-CLIENT1'+hello));send(self.socket,hello);response=receive(self.socket)
        assert response[:4]==struct.pack('>I',2)
        body=response[8:];assert body[:4]==b'DGS1' and body[4:36]==hashlib.sha256(hello).digest() and body[100:132]==session and body[132:164]==case.schema
        self.minor=struct.unpack('>H',body[164:166])[0];assert minimum<=self.minor<=maximum and body[166:170]==struct.pack('>I',profile)
        n=struct.unpack('>I',body[202:206])[0];locator=body[206:206+n];assert len(locator)==320 and locator[40:72]==bytes.fromhex(case.server_root)
        Ed25519PublicKey.from_public_bytes(locator[40:72]).verify(locator[156:220],b'DLN-KEY1'+locator[8:40]+locator[104:112]+locator[72:104]+locator[120:136])
        Ed25519PublicKey.from_public_bytes(locator[72:104]).verify(locator[-64:],locator[:-64]);Ed25519PublicKey.from_public_bytes(locator[72:104]).verify(body[-64:],b'DLN-GATEWAY-SERVER1'+body[:-64])
        self.binding=hashlib.sha256(hello+response).digest();shared=ephemeral.exchange(X25519PublicKey.from_public_bytes(body[68:100]));self.keys=[HKDF(algorithm=hashes.SHA256(),length=44,salt=self.binding,info=label+session).derive(shared) for label in (b'DLM-CRYPTO-v1/c2s|',b'DLM-CRYPTO-v1/s2c|')];self.sequence=0;self.request_id=1
        if finish:
            send(self.socket,self.encrypt(3,self.binding));assert self.decrypt(receive(self.socket),4)==self.binding
    def aad(self,kind):return b'DLN-GATEWAY-AAD1'+self.binding+struct.pack('>HI',self.minor,kind)
    def encrypt(self,kind,plain):
        sequence=self.sequence;self.sequence+=1;key=self.keys[0];nonce=bytes(a^b for a,b in zip(key[32:],sequence.to_bytes(12,'big')));header=b'DLC1'+struct.pack('>QI',sequence,len(plain));return wire(kind,header+AESGCM(key[:32]).encrypt(nonce,plain,header+self.aad(kind)))
    def decrypt(self,value,kind):
        assert struct.unpack('>II',value[:8])==(kind,len(value)-8);value=value[8:];sequence,n=struct.unpack('>QI',value[4:16]);assert value[:4]==b'DLC1' and len(value)==n+32;key=self.keys[1];nonce=bytes(a^b for a,b in zip(key[32:],sequence.to_bytes(12,'big')));return AESGCM(key[:32]).decrypt(nonce,value[16:],value[:16]+self.aad(kind))
    def request(self,op,body=b'',duplicate=False):
        value=self.encrypt(5,struct.pack('>QI',self.request_id,op)+body);send(self.socket,value);reply=receive(self.socket)
        if duplicate:send(self.socket,value);assert receive(self.socket)==reply
        plain=self.decrypt(reply,6);ident,status,outcome,result,n=struct.unpack('>QIIQI',plain[:28]);assert ident==self.request_id and n==len(plain)-28;self.request_id+=1;return status,outcome,result,plain[28:]
    def open(self,target,udp=False):
        target=target.encode();status,outcome,value,data=self.request(2 if udp else 1,struct.pack('>H',len(target))+target);assert status==0 and outcome==1 and not data;return value
class Echo:
    def __init__(self,udp=False):
        self.socket=socket.socket(socket.AF_INET,socket.SOCK_DGRAM if udp else socket.SOCK_STREAM);self.socket.bind(('127.0.0.1',0));self.socket.settimeout(.05);self.address='127.0.0.1:%s'%self.socket.getsockname()[1];self.stop=threading.Event();self.received=0;self.workers=[]
        if not udp:self.socket.listen(32)
        def client(peer):
            with peer:
                peer.settimeout(.05)
                while not self.stop.is_set():
                    try:
                        data=peer.recv(65536)
                        if not data:return
                        self.received+=len(data);peer.sendall(data)
                    except socket.timeout:continue
                    except OSError:return
        def run():
            while not self.stop.is_set():
                try:
                    if udp:data,peer=self.socket.recvfrom(65536);self.received+=len(data);self.socket.sendto(data,peer)
                    else:
                        peer,_=self.socket.accept();worker=threading.Thread(target=client,args=(peer,),daemon=True);self.workers.append(worker);worker.start()
                except socket.timeout:continue
                except OSError:return
        self.worker=threading.Thread(target=run,daemon=True);self.worker.start()
    def close(self):
        self.stop.set();self.worker.join(2);self.socket.close()
        for worker in self.workers:worker.join(2)

class GatewayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler=Path(os.environ.get('DLM_NETWORK_DLM',ROOT/'target/debug/dlm')).resolve();cls.profile=cls.compiler.parent;cls.cli=cls.profile/('dlm-net.exe' if os.name=='nt' else 'dlm-net');library=cls.profile/('dlm_gateway.dll' if os.name=='nt' else 'libdlm_gateway.so');cls.api=C.CDLL(str(library));cls.build=tempfile.TemporaryDirectory(prefix='dln-gateway-build-');build=Path(cls.build.name)
        signatures={'abi':(U,[]),'server_open':(Opened,[P,N]),'server_close':(None,[P]),'client_open':(Opened,[P,N]),'client_close':(None,[P]),'stream_open':(Io,[P,U,P,N]),'stream_write':(Io,[P,U,P,N]),'stream_read':(Transfer,[P,U,N,P,N]),'stream_close':(Io,[P,U,C.c_bool]),'take':(Transfer,[P,P,N]),'datagram':(Transfer,[P,U,P,N,P,N]),'store':(Io,[P,P,Q,P,N]),'fetch':(Transfer,[P,P,P,N]),'server_statistics':(Stats,[P]),'server_address':(Io,[P,P,N])}
        for name,(result,args) in signatures.items():fn=getattr(cls.api,'dln_gateway_'+name);fn.restype=result;fn.argtypes=args;setattr(cls,name,staticmethod(fn))
        cls.fixture=build/'gateway.a';subprocess.run([str(cls.compiler),'build',str(HERE/'gateway_boundary.dlm'),'--lib',str(ROOT/'lib'),'--allow-unsafe','--target','staticlib','-o',str(cls.fixture)],check=True,capture_output=True)
        cls.host=build/('gateway-host.exe' if os.name=='nt' else 'gateway-host');command=[os.environ.get('CC','gcc'),'-std=gnu11','-O1','-Wall','-Wextra','-Werror','-UNDEBUG',str(HERE/'dln_gateway_host.c'),str(cls.fixture),'-L'+str(cls.profile),'-ldlm_gateway']
        command+=['-lws2_32','-lshell32','-lgdi32','-luser32'] if os.name=='nt' else ['-pthread','-ldl','-lm','-Wl,-rpath,'+str(cls.profile)]
        subprocess.run(command+['-o',str(cls.host)],check=True,capture_output=True)
        cls.descriptor=build/'gateway.dns';subprocess.run([str(cls.cli),'descriptor','--output',str(cls.descriptor)],check=True,capture_output=True);cls.schema=hashlib.sha256(cls.descriptor.read_bytes()).digest();cls.fixtures=ROOT/'examples/native/fixtures'
    @classmethod
    def tearDownClass(cls):cls.build.cleanup()
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='dln-gateway-test-');self.addCleanup(self.temp.cleanup);self.path=Path(self.temp.name);self.server_identity=self.path/'server.identity';self.client_identity=self.path/'client.identity';self.server_root=self.command('keygen','--identity',self.server_identity)['root'];self.client_root=self.command('keygen','--identity',self.client_identity)['root'];self.echo=Echo();self.addCleanup(self.echo.close)
    def command(self,*args,success=True):
        result=subprocess.run([str(self.cli),*map(str,args)],capture_output=True,timeout=15)
        if success:self.assertEqual(result.returncode,0,result.stderr.decode());return json.loads(result.stdout)
        self.assertNotEqual(result.returncode,0);return result
    def server_text(self,kind='tls',private=True,spool=False):
        text=f'listen=127.0.0.1:0\ntransport={kind}\nidentity={self.server_identity}\ncertificate={self.fixtures/"quic-test-server.pem"}\nkey={self.fixtures/"quic-test-key.pem"}\nallow_client={self.client_root}\nallow_target=tcp,127.0.0.0/8,{self.echo.address.rsplit(":",1)[1]}\nallow_private={str(private).lower()}\n'
        if spool:
            key=self.path/'spool.key'
            if not key.exists():self.command('journal-keygen','--output',key)
            text+=f'spool={self.path/"inbox"}\nspool_key={key}\n'
        return text
    def client_text(self,address,kind='tls'):
        return f'gateway={address}\nhostname=localhost\nca={self.fixtures/"quic-test-ca.pem"}\ntransport={kind}\nidentity={self.client_identity}\npeer={self.server_root}\ntimeout_ms=3000\n'
    def start(self,text=None):
        text=(text or self.server_text()).encode();opened=self.server_open(text,len(text));self.assertEqual(opened.status,0);self.addCleanup(self.server_close,opened.handle);out=C.create_string_buffer(128);r=self.server_address(opened.handle,out,128);self.assertEqual(r.status,0);return opened.handle,out.raw[:r.count].decode()
    def connect(self,address,text=None):
        text=(text or self.client_text(address)).encode();opened=self.client_open(text,len(text));self.assertEqual(opened.status,0);self.addCleanup(self.client_close,opened.handle);return opened.handle
    def start_process(self,text):
        config=self.path/'server.cfg';config.write_text(text,encoding='utf-8');log=self.path/('server-'+str(time.monotonic_ns())+'.log');stream=log.open('w');process=subprocess.Popen([str(self.cli),'serve','--config',str(config)],stdout=stream,stderr=stream)
        self.addCleanup(stream.close)
        def stop():
            if process.poll() is None:process.terminate()
            try:process.wait(5)
            except subprocess.TimeoutExpired:process.kill();process.wait(5)
        self.addCleanup(stop)
        end=time.monotonic()+5
        while time.monotonic()<end:
            lines=log.read_text().splitlines()
            if lines and lines[0].startswith('{'):return process,json.loads(lines[0])['address']
            if process.poll() is not None:self.fail(log.read_text())
            time.sleep(.01)
        self.fail('gateway READY deadline')
    def test_c_and_dlm_clients_forward_binary_and_retain_short_outputs(self):
        server=self.path/'server.cfg';client=self.path/'client.cfg';server.write_text(self.server_text().replace(self.echo.address.rsplit(':',1)[1],'__ECHO_PORT__'));client.write_text(self.client_text('__GATEWAY__'));result=subprocess.run([str(self.host),str(server),str(client)],capture_output=True,timeout=15);self.assertEqual(result.returncode,0,result.stderr.decode());self.assertEqual(json.loads(result.stdout)['status'],'PASS')
    def test_independent_tls_handshake_signature_hkdf_aead_and_duplicate_receipt(self):
        handle,address=self.start();peer=Oracle(self,address,minimum=1,maximum=2);self.assertEqual(peer.minor,2);stream=peer.open(self.echo.address);data=bytes(range(256))*31+b'\0\xff';status,outcome,value,_=peer.request(3,struct.pack('>II',stream,len(data))+data,duplicate=True);self.assertEqual((status,outcome,value),(0,2,len(data)));out=bytearray()
        while len(out)<len(data):status,outcome,n,block=peer.request(4,struct.pack('>II',stream,len(data)-len(out)));self.assertEqual((status,outcome),(0,3));out.extend(block)
        self.assertEqual(out,data);self.assertEqual(self.echo.received,len(data));self.assertEqual(self.server_statistics(handle).forwarded,len(data))
    def test_unconfirmed_hello_cannot_forward_application_data(self):
        handle,address=self.start();peer=Oracle(self,address,finish=False);bad=peer.encrypt(5,struct.pack('>QI',1,1)+struct.pack('>H',len(self.echo.address))+self.echo.address.encode());send(peer.socket,bad)
        with self.assertRaises((EOFError,OSError)):receive(peer.socket)
        self.assertEqual(self.server_statistics(handle).authenticated,0);self.assertEqual(self.server_statistics(handle).forwarded,0)
    def test_schema_carrier_audience_and_minor_substitution_rejected(self):
        handle,address=self.start()
        for offset in (100,132,164):
            def mutate(body,offset=offset):return body[:offset]+bytes([body[offset]^1])+body[offset+1:]
            with self.assertRaises((EOFError,OSError,AssertionError)):Oracle(self,address,mutate=mutate)
        with self.assertRaises((EOFError,OSError,AssertionError)):Oracle(self,address,minimum=4,maximum=4)
        self.assertEqual(self.server_statistics(handle).forwarded,0)
    def test_bad_aead_and_cross_session_replay_never_reach_destination(self):
        handle,address=self.start();first=Oracle(self,address);stream=first.open(self.echo.address);packet=first.encrypt(5,struct.pack('>QI',2,3)+struct.pack('>II',stream,3)+b'abc');second=Oracle(self,address);send(second.socket,packet)
        with self.assertRaises((EOFError,OSError)):receive(second.socket)
        corrupt=bytearray(packet);corrupt[-1]^=1;send(first.socket,corrupt)
        with self.assertRaises((EOFError,OSError)):receive(first.socket)
        self.assertEqual(self.echo.received,0);self.assertEqual(self.server_statistics(handle).forwarded,0)
    def test_private_dns_mapped_ipv6_and_wrong_port_denied(self):
        _,address=self.start(self.server_text(private=False));peer=Oracle(self,address)
        for target in (self.echo.address,'localhost:'+self.echo.address.rsplit(':',1)[1],'[::ffff:127.0.0.1]:'+self.echo.address.rsplit(':',1)[1],'127.0.0.1:1'):
            target=target.encode();status,_,_,_=peer.request(1,struct.pack('>H',len(target))+target);self.assertEqual(status,16)
        self.assertEqual(self.echo.received,0)
    def test_large_cross_process_binary_sha256(self):
        _,address=self.start();config=self.path/'client.cfg';config.write_text(self.client_text(address));data=bytes(range(256))*4096+b'\xff\0';source=self.path/'input.bin';output=self.path/'output.bin';source.write_bytes(data);r=self.command('exchange','--config',config,'--protocol','tcp','--target',self.echo.address,'--input',source,'--output',output);self.assertEqual(r['received'],len(data));self.assertEqual(hashlib.sha256(output.read_bytes()).digest(),hashlib.sha256(data).digest())
    def test_cli_udp_zero_and_binary_boundary(self):
        udp=Echo(True);self.addCleanup(udp.close);text=self.server_text()+f'allow_target=udp,127.0.0.0/8,{udp.address.rsplit(":",1)[1]}\n';_,address=self.start(text);config=self.path/'client.cfg';config.write_text(self.client_text(address))
        for n in (0,1,767,768,32768):
            source=self.path/'udp.bin';output=self.path/'udp.out';data=bytes((i*37+n)%256 for i in range(n));source.write_bytes(data);self.command('exchange','--config',config,'--protocol','udp','--target',udp.address,'--input',source,'--output',output);self.assertEqual(output.read_bytes(),data)
    def test_gateway_crash_after_commit_recovers_encrypted_deduplicated_mail(self):
        text=self.server_text(spool=True);process,address=self.start_process(text);config=self.path/'client.cfg';config.write_text(self.client_text(address));client=self.connect(address);ident=b'M'*16;payload=b'CRASH-RECOVERY\0'+bytes(range(256));expires=int(time.time())+3600;self.assertEqual(self.store(client,ident,expires,payload,len(payload)).status,0);process.kill();process.wait(5);text=text.replace('listen=127.0.0.1:0','listen='+address);_,second=self.start_process(text);self.assertEqual(second,address);client=self.connect(address);self.assertEqual(self.store(client,ident,expires,payload,len(payload)).status,0);self.assertEqual(self.store(client,ident,expires,b'bad',3).status,18);output=C.create_string_buffer(1024);got=self.fetch(client,ident,output,1024);self.assertEqual((got.status,output.raw[:got.copied]),(0,payload));entry=(self.path/'inbox'/(ident.hex()+'.dgw')).read_bytes();self.assertNotIn(payload,entry);salt=entry[20:52];nonce=entry[52:64];key=HKDF(algorithm=hashes.SHA256(),length=32,salt=salt,info=b'DLN-GATEWAY-WAL1').derive((self.path/'spool.key').read_bytes());plain=AESGCM(key).decrypt(nonce,entry[64:],entry[:20]);self.assertEqual(plain[77:],payload);self.assertEqual(plain[0],2)
    def test_offline_outbox_durable_remote_ack_and_idempotent_flush(self):
        source=self.path/'offline.bin';data=b'offline\0'+os.urandom(2048);source.write_bytes(data);key=self.path/'outbox.key';self.command('journal-keygen','--output',key);ident='12'*16;outbox=self.path/'outbox';r=self.command('enqueue','--outbox',outbox,'--key',key,'--peer',self.server_root,'--input',source,'--message-id',ident);self.assertEqual(r['status'],'durable_local');_,address=self.start(self.server_text(spool=True));config=self.path/'client.cfg';config.write_text(self.client_text(address));r=self.command('flush','--config',config,'--outbox',outbox,'--key',key);self.assertEqual(r['messages'],1);self.assertEqual(self.command('flush','--config',config,'--outbox',outbox,'--key',key)['messages'],0);output=self.path/'fetched.bin';self.command('fetch','--config',config,'--message-id',ident,'--output',output);self.assertEqual(output.read_bytes(),data);self.command('consume','--config',config,'--message-id',ident);self.assertTrue((outbox/(ident+'.dgw')).is_file())
    def test_rotation_preserves_root_and_revokes_older_active_session(self):
        old=self.path/'old.identity';shutil.copyfile(self.client_identity,old);_,address=self.start();old_peer=Oracle(self,address);rotated=self.command('rotate','--identity',self.client_identity);self.assertEqual(rotated['root'],self.client_root);self.assertEqual(rotated['epoch'],2);new_peer=Oracle(self,address);new_peer.open(self.echo.address)
        with self.assertRaises((EOFError,OSError)):old_peer.open(self.echo.address)
        old_text=self.client_text(address).replace(str(self.client_identity),str(old));opened=self.client_open(old_text.encode(),len(old_text.encode()));self.assertNotEqual(opened.status,0)
    def test_untrusted_connection_flood_is_bounded_and_service_recovers(self):
        text=self.server_text()+'max_sessions=8\nmax_client_sessions=2\nmax_handshakes=4\nhandshake_ms=100\n';handle,address=self.start(text);host,port=address.rsplit(':',1);peak=0
        def attempt(_):
            try:
                with socket.create_connection((host,int(port)),timeout=.05) as channel:channel.sendall(b'not-a-TLS-or-DLN-handshake')
            except OSError:pass
        with concurrent.futures.ThreadPoolExecutor(max_workers=24) as workers:
            future=workers.map(attempt,range(1000))
            for _ in future:peak=max(peak,self.server_statistics(handle).handshakes)
        self.assertLessEqual(peak,4);time.sleep(1.1);client=self.connect(address);opened=self.stream_open(client,1,self.echo.address.encode(),len(self.echo.address));self.assertEqual(opened.status,0);self.assertLessEqual(self.server_statistics(handle).active,2)
    def test_invalid_certificate_duplicate_policy_and_oversized_config_fail_before_ready(self):
        bad=self.path/'bad.pem';bad.write_bytes(b'not a certificate');text=self.server_text().replace(str(self.fixtures/'quic-test-server.pem'),str(bad));opened=self.server_open(text.encode(),len(text));self.assertNotEqual(opened.status,0)
        for text in (self.server_text()+f'allow_client={self.client_root}@2\n',self.server_text()+'listen=127.0.0.1:1\n',self.server_text()+'unknown=x\n','x'*65537):opened=self.server_open(text.encode(),len(text));self.assertNotEqual(opened.status,0)
if __name__=='__main__':unittest.main()
