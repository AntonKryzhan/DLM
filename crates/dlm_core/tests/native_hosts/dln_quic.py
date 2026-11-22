#!/usr/bin/env python3
"""DLM/ngtcp2 interoperability with independent aioquic==1.8.0 loopback peers.
Needs a GNU C compiler, GnuTLS runtime/dev linker, and Python cryptography.
DLM_QUIC_PREBUILT optionally selects the already built pinned source provider.
"""
import ctypes as C
import importlib.util
import os
from pathlib import Path
import random
import socket
import subprocess
import tempfile
import threading
import time
import unittest
from aioquic.buffer import Buffer
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.connection import QuicConnection
from aioquic.quic.events import ConnectionTerminated,DatagramFrameReceived,HandshakeCompleted,StreamDataReceived,StreamReset
from aioquic.quic.packet import pull_quic_header,QuicProtocolVersion
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
FIXTURES=ROOT/'examples/native/fixtures'
spec=importlib.util.spec_from_file_location('quic_network_base',HERE/'network.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
U64,I64=C.c_uint64,C.c_int64
class Config(C.Structure):
    _fields_=[(n,C.c_uint32) for n in ('connections','streams','events','idle_ms','handshake_ms','congestion')]+[('memory',C.c_size_t),('window',C.c_size_t)]
    @classmethod
    def default(cls,**kwargs):
        v=dict(connections=4,streams=32,events=1024,idle_ms=10000,handshake_ms=2000,congestion=2,memory=16<<20,window=65536);v.update(kwargs);return cls(**v)
class Reference(C.Structure):
    _fields_=[('connection',U64),('stream',I64),('status',C.c_int32),('system',C.c_int32)]
class Event(C.Structure):
    _fields_=[('id',U64),('connection',U64),('stream',I64),('offset',U64),('code',U64),('count',C.c_size_t),('kind',C.c_uint32),('flags',C.c_uint32),('status',C.c_int32),('system',C.c_int32)]
class Statistics(C.Structure):
    _fields_=[('used',C.c_size_t),('limit',C.c_size_t)]+[(n,U64) for n in ('sent','received','lost','cwnd','in_flight','rtt_ns')]+[(n,C.c_uint32) for n in ('connections','streams','events','state','version')]+[('status',C.c_int32),('system',C.c_int32)]
def address(port=0,ipv6=False):
    raw=socket.inet_pton(socket.AF_INET6 if ipv6 else socket.AF_INET,'::1' if ipv6 else '127.0.0.1').ljust(16,b'\0')
    return base.Address(6 if ipv6 else 4,port,0,(C.c_uint8*16).from_buffer_copy(raw))
def api_load(path):
    api=C.CDLL(str(path))
    signatures={
        'version':(C.c_uint32,[]),'shutdown':(base.Io,[]),'open':(base.Opened,[base.Address,C.c_bool,base.Text,base.Text,base.Text,Config]),
        'close':(None,[C.c_void_p]),'local':(base.Resolved,[C.c_void_p]),'connect':(Reference,[C.c_void_p,base.Address,base.Text]),
        'poll':(base.Io,[C.c_void_p,C.c_uint32,C.c_void_p]),'next':(Event,[C.c_void_p]),
        'copy':(base.Io,[C.c_void_p,U64,C.c_size_t,base.U8P,C.c_size_t]),'ack':(base.Io,[C.c_void_p,U64]),
        'stream':(Reference,[C.c_void_p,U64,C.c_bool]),'write':(base.Io,[C.c_void_p,U64,I64,base.U8P,C.c_size_t,C.c_bool]),
        'datagram':(base.Io,[C.c_void_p,U64,base.U8P,C.c_size_t]),'reset':(base.Io,[C.c_void_p,U64,I64,U64]),
        'disconnect':(base.Io,[C.c_void_p,U64,U64]),'forget':(base.Io,[C.c_void_p,U64]),'migrate':(base.Io,[C.c_void_p,U64,base.Address]),'stats':(Statistics,[C.c_void_p,U64])}
    for name,(result,args) in signatures.items():
        f=getattr(api,'dlm_net_quic_'+name);f.restype,f.argtypes=result,args;setattr(api,name,f)
    return api
class Pair:
    def __init__(self,test,dlm_server=False,hostname='localhost',trust=None,alpn='dlm-test/1',peer_alpn=None,ipv6=False,loss=0,reorder=False,peer_version=None,**limits):
        self.test,self.api,self.server,self.ipv6=test,test.api,dlm_server,ipv6
        self.loss,self.reorder,self.random=loss,reorder,random.Random(43)
        self.dlm_events,self.peer_events,self.held,self.peer=[],[],[],None
        self.sock=socket.socket(socket.AF_INET6 if ipv6 else socket.AF_INET,socket.SOCK_DGRAM);self.sock.bind(('::1' if ipv6 else '127.0.0.1',0));self.sock.setblocking(False)
        self.config=Config.default(**limits)
        cert=str(FIXTURES/'quic-test-server.pem') if dlm_server else (str(FIXTURES/'quic-test-ca.pem') if trust is None else trust)
        key=str(FIXTURES/'quic-test-key.pem') if dlm_server else ''
        opened=self.api.open(address(ipv6=ipv6),dlm_server,base.Text.of(alpn),base.Text.of(cert),base.Text.of(key),self.config)
        test.assertEqual(opened.status,0,opened.system);self.handle=opened.handle;test.owners.append(self)
        port=self.api.local(self.handle).address.port;self.target=('::1',port,0,0) if ipv6 else ('127.0.0.1',port)
        self.peer_config=QuicConfiguration(is_client=dlm_server,alpn_protocols=[peer_alpn or alpn],max_datagram_frame_size=1200,max_data=1<<20,max_stream_data=1<<20,idle_timeout=10,server_name=hostname)
        if peer_version is not None:self.peer_config.supported_versions=[peer_version]
        if dlm_server:
            self.peer_config.load_verify_locations(cafile=str(FIXTURES/'quic-test-ca.pem'));self.peer=QuicConnection(configuration=self.peer_config);self.peer.connect(self.target,now=time.monotonic());self.connection=0
        else:
            self.peer_config.load_cert_chain(str(FIXTURES/'quic-test-server.pem'),str(FIXTURES/'quic-test-key.pem'))
            r=self.api.connect(self.handle,address(self.sock.getsockname()[1],ipv6),base.Text.of(hostname));test.assertEqual(r.status,0,r.system);self.connection=r.connection
    def close(self):
        if self.handle:self.api.close(self.handle);self.handle=None
        self.sock.close()
    def events(self,ack=True):
        result=[]
        while True:
            e=self.api.next(self.handle)
            if e.status:self.test.assertEqual(e.status,1);break
            out=base.octets(b'\xa5'*(e.count+8));r=self.api.copy(self.handle,e.id,0,out,e.count+8)
            self.test.assertEqual((r.status,r.count),(0,e.count));self.test.assertEqual(bytes(out)[e.count:],b'\xa5'*8);result.append((e,bytes(out)[:e.count]))
            if e.kind==1:self.connection=e.connection
            if not ack:break
            self.test.assertEqual(self.api.ack(self.handle,e.id).status,0)
        return result
    def step(self,ack=True):
        r=self.api.poll(self.handle,0,None);self.test.assertIn(r.status,(0,1),(r.status,r.system));now=time.monotonic()
        for _ in range(64):
            try:data,source=self.sock.recvfrom(65536)
            except BlockingIOError:break
            if self.random.random()<self.loss:continue
            if self.peer is None:
                header=pull_quic_header(Buffer(data=data),host_cid_length=8);self.peer=QuicConnection(configuration=self.peer_config,original_destination_connection_id=header.destination_cid)
            self.peer.receive_datagram(data,source,now=now)
        if self.peer:
            timer=self.peer.get_timer()
            if timer is not None and timer<=now:self.peer.handle_timer(now=now)
            while True:
                e=self.peer.next_event()
                if e is None:break
                self.peer_events.append(e)
            for data,target in self.peer.datagrams_to_send(now=now):
                if self.random.random()>=self.loss:
                    if self.reorder:self.held.append((now+self.random.uniform(0,0.012),data,target))
                    else:self.sock.sendto(data,target)
            for entry in list(reversed(self.held)):
                release,data,target=entry
                if release<=now:
                    self.sock.sendto(data,target)
                    if self.random.random()<0.25:self.sock.sendto(data,target)
                    self.held.remove(entry)
        if ack:self.dlm_events.extend(self.events())
        return r
    def until(self,predicate,timeout=5,ack=True):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            self.step(ack)
            if predicate():return
            time.sleep(0.001)
        s=self.api.stats(self.handle,self.connection);self.test.fail(f'QUIC wait expired: state={s.state}, status={s.status}, system={s.system}, events={[(e.kind,e.code) for e,_ in self.dlm_events]}, peer={self.peer_events[-5:]}')
    def ready(self):
        self.until(lambda:any(e.kind==1 for e,_ in self.dlm_events) and any(isinstance(e,HandshakeCompleted) for e in self.peer_events));return self
    def write(self,stream,data,fin=True):return self.api.write(self.handle,self.connection,stream,base.octets(data),len(data),fin)
    def datagram(self,data):return self.api.datagram(self.handle,self.connection,base.octets(data),len(data))
class QuicTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory(prefix='dln-quic-');d=Path(cls.directory.name)
        provider=os.environ.get('DLM_QUIC_PREBUILT')
        if not provider:
            provider=str(d/'libdlm-quic-1.so');subprocess.run(['python3',str(ROOT/'native/quic/build.py'),provider],check=True,capture_output=True,text=True)
        os.environ['DLM_LIBQUIC']=str(Path(provider).resolve());cls.network_path=d/('network.dll' if os.name=='nt' else 'network.so')
        cmd=[os.environ.get('CC','cc'),'-std=gnu11','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-shared',str(ROOT/'crates/dlm_core/src/native/network.c'),'-o',str(cls.network_path)]+(['-lws2_32'] if os.name=='nt' else ['-fPIC','-ldl'])
        subprocess.run(cmd,check=True,capture_output=True,text=True);cls.api=api_load(cls.network_path);assert cls.api.version()==0x011a00
    @classmethod
    def tearDownClass(cls):cls.directory.cleanup()
    def setUp(self):self.owners=[]
    def tearDown(self):
        for p in self.owners:p.close()
        self.assertEqual(self.api.shutdown().status,0);self.assertEqual(self.api.version(),0x011a00)
    def test_client_and_server_binary_streams_and_datagrams(self):
        for server in (False,True):
            p=Pair(self,dlm_server=server).ready();streams=[]
            for uni in (False,True,False):
                r=self.api.stream(p.handle,p.connection,uni);self.assertEqual(r.status,0,r.system);streams.append(r.stream)
                self.assertEqual(p.write(r.stream,bytes(range(256))*13+bytes([len(streams)])).status,0)
            p.until(lambda:len({e.stream_id for e in p.peer_events if isinstance(e,StreamDataReceived) and e.end_stream})==3)
            for stream in streams:self.assertEqual(b''.join(e.data for e in p.peer_events if isinstance(e,StreamDataReceived) and e.stream_id==stream),bytes(range(256))*13+bytes([streams.index(stream)+1]))
            stream=p.peer.get_next_available_stream_id();p.peer.send_stream_data(stream,b'peer\0\xffdata',end_stream=True);p.peer.send_datagram_frame(b'unreliable\0\xff');self.assertEqual(p.datagram(b'DLM\0datagram').status,0)
            p.until(lambda:any(e.kind==3 and d==b'peer\0\xffdata' and e.flags&1 for e,d in p.dlm_events) and any(e.kind==5 for e,_ in p.dlm_events) and any(isinstance(e,DatagramFrameReceived) for e in p.peer_events))
            self.assertEqual([d for e,d in p.dlm_events if e.kind==5],[b'unreliable\0\xff']);self.assertEqual([e.data for e in p.peer_events if isinstance(e,DatagramFrameReceived)],[b'DLM\0datagram'])
            s=self.api.stats(p.handle,p.connection);self.assertEqual((s.state,s.version,s.status),(2,1,0));self.assertGreater(s.cwnd,0);self.assertLessEqual(s.used,s.limit);p.close()
    def test_hostname_ca_and_alpn_fail_closed(self):
        for params in ({'hostname':'wrong.invalid'},{'trust':str(FIXTURES/'quic-test-untrusted-ca.pem')},{'peer_alpn':'different/1'}):
            p=Pair(self,**params);p.until(lambda:self.api.stats(p.handle,p.connection).state>=3)
            self.assertFalse(any(e.kind in (1,3,5) for e,_ in p.dlm_events));self.assertNotEqual(self.api.stats(p.handle,p.connection).status,0);p.close()
    def test_flow_control_retained_event_credit(self):
        p=Pair(self,window=1024).ready();stream=p.peer.get_next_available_stream_id();data=bytes(range(256))*16;p.peer.send_stream_data(stream,data,end_stream=True)
        p.until(lambda:self.api.stats(p.handle,0).events>0,ack=False);first=p.events(False)[0]
        for _ in range(50):p.step(False)
        self.assertEqual(self.api.next(p.handle).id,first[0].id)
        p.until(lambda:sum(len(d) for e,d in p.dlm_events if e.kind==3)==len(data));self.assertEqual(b''.join(d for e,d in p.dlm_events if e.kind==3),data)
    def test_loss_retransmits_both_roles(self):
        for server in (False,True):
            p=Pair(self,dlm_server=server,loss=0.13,window=262144).ready();s=self.api.stream(p.handle,p.connection,False);data=random.Random(7).randbytes(90000);self.assertEqual(p.write(s.stream,data).status,0)
            p.until(lambda:any(isinstance(e,StreamDataReceived) and e.end_stream for e in p.peer_events),12)
            self.assertEqual(b''.join(e.data for e in p.peer_events if isinstance(e,StreamDataReceived)),data);self.assertGreater(self.api.stats(p.handle,p.connection).lost,0);p.close()
    def test_client_migration_keeps_connection(self):
        p=Pair(self).ready()
        for _ in range(30):p.step()
        self.assertEqual(self.api.migrate(p.handle,p.connection,address(ipv6=True)).status,6)
        before=self.api.local(p.handle).address.port;r=self.api.migrate(p.handle,p.connection,address());self.assertEqual(r.status,0,r.system)
        p.until(lambda:any(e.kind==7 and e.flags&1 for e,_ in p.dlm_events));self.assertNotEqual(before,self.api.local(p.handle).address.port)
        s=self.api.stream(p.handle,p.connection,False);self.assertEqual(p.write(s.stream,b'after migration').status,0);p.until(lambda:any(isinstance(e,StreamDataReceived) and e.data==b'after migration' for e in p.peer_events))
    def test_limits_cross_endpoint_and_cancel(self):
        p=Pair(self,connections=1,streams=4,window=1024).ready();self.assertEqual(self.api.connect(p.handle,address(9),base.Text.of('localhost')).status,7)
        s=self.api.stream(p.handle,p.connection,False);self.assertEqual(p.write(s.stream,b'a'*1024,False).status,0);self.assertEqual(p.write(s.stream,b'b',False).status,1);self.assertEqual(p.datagram(b'a'*1101).status,4)
        other=Pair(self).ready();self.assertEqual(self.api.write(other.handle,p.connection,s.stream,base.octets(b'x'),1,False).status,4)
        stop=C.c_bool(True);self.assertEqual(self.api.poll(p.handle,10000,C.byref(stop)).status,9);self.assertEqual(self.api.shutdown().status,4);self.assertEqual(self.api.forget(p.handle,p.connection).status,1)
    def test_ipv6_both_roles(self):
        for server in (False,True):
            p=Pair(self,dlm_server=server,ipv6=True).ready();s=self.api.stream(p.handle,p.connection,False);self.assertEqual(p.write(s.stream,b'ipv6\0\xff').status,0)
            p.until(lambda:any(isinstance(e,StreamDataReceived) and e.end_stream for e in p.peer_events));self.assertEqual(b''.join(e.data for e in p.peer_events if isinstance(e,StreamDataReceived)),b'ipv6\0\xff');p.close()
    def test_quic_v2_independent_client(self):
        p=Pair(self,dlm_server=True,peer_version=QuicProtocolVersion.VERSION_2).ready();self.assertEqual(self.api.stats(p.handle,p.connection).version,int(QuicProtocolVersion.VERSION_2))
        s=p.peer.get_next_available_stream_id();p.peer.send_stream_data(s,b'v2',end_stream=True);p.until(lambda:any(e.kind==3 and d==b'v2' for e,d in p.dlm_events))
    def test_reorder_duplicates_empty_stream(self):
        p=Pair(self,dlm_server=True,reorder=True,window=262144).ready();data=random.Random(19).randbytes(120000);s=p.peer.get_next_available_stream_id();p.peer.send_stream_data(s,data,end_stream=True)
        p.until(lambda:any(e.kind==3 and e.flags&1 for e,_ in p.dlm_events),10);self.assertEqual(b''.join(d for e,d in p.dlm_events if e.kind==3),data)
        s=self.api.stream(p.handle,p.connection,True);self.assertEqual(p.write(s.stream,b'').status,0);p.until(lambda:any(isinstance(e,StreamDataReceived) and e.stream_id==s.stream and e.end_stream and not e.data for e in p.peer_events))
    def test_receive_queue_limit_is_observable(self):
        p=Pair(self,events=4).ready()
        for _ in range(8):s=p.peer.get_next_available_stream_id();p.peer.send_stream_data(s,b'small',end_stream=True)
        p.until(lambda:self.api.stats(p.handle,p.connection).state>=3,ack=False);s=self.api.stats(p.handle,p.connection);self.assertEqual(s.status,7);self.assertLessEqual(s.events,4);self.assertLessEqual(s.used,s.limit)
    def test_reset_preserves_other_stream(self):
        p=Pair(self).ready();a=self.api.stream(p.handle,p.connection,False);b=self.api.stream(p.handle,p.connection,False);self.assertEqual(p.write(a.stream,b'first',False).status,0)
        p.until(lambda:any(isinstance(e,StreamDataReceived) and e.stream_id==a.stream for e in p.peer_events));self.assertEqual(self.api.reset(p.handle,p.connection,a.stream,123).status,0);self.assertEqual(p.write(a.stream,b'rejected',False).status,2);self.assertEqual(p.write(b.stream,b'still independent').status,0)
        p.until(lambda:any(isinstance(e,StreamReset) and e.error_code==123 for e in p.peer_events) and any(isinstance(e,StreamDataReceived) and e.stream_id==b.stream and e.end_stream for e in p.peer_events))
    def test_event_copy_offsets_and_bad_ack(self):
        p=Pair(self).ready();s=p.peer.get_next_available_stream_id();p.peer.send_stream_data(s,b'0123456789',end_stream=True);p.until(lambda:self.api.stats(p.handle,0).events>0,ack=False)
        while self.api.next(p.handle).kind!=3:self.assertEqual(self.api.ack(p.handle,self.api.next(p.handle).id).status,0)
        e=self.api.next(p.handle);out=base.octets(b'\xa5'*8);self.assertEqual(self.api.copy(p.handle,e.id,e.count+1,out,8).status,4);self.assertEqual(bytes(out),b'\xa5'*8)
        r=self.api.copy(p.handle,e.id,3,out,4);self.assertEqual((r.status,r.count,bytes(out)),(0,4,b'3456'+b'\xa5'*4));self.assertEqual(self.api.ack(p.handle,e.id+1).status,4);self.assertEqual(self.api.next(p.handle).id,e.id)
    def test_graceful_close_and_forget(self):
        for server in (False,True):
            p=Pair(self,dlm_server=server).ready();self.assertEqual(self.api.disconnect(p.handle,p.connection,321).status,0)
            p.until(lambda:self.api.stats(p.handle,p.connection).state==5 and any(isinstance(e,ConnectionTerminated) for e in p.peer_events))
            self.assertTrue(any(isinstance(e,ConnectionTerminated) and e.error_code==321 for e in p.peer_events));self.assertTrue(any(e.kind==6 for e,_ in p.dlm_events))
            self.assertEqual(self.api.forget(p.handle,p.connection).status,0);self.assertEqual(self.api.stats(p.handle,p.connection).status,4);self.assertEqual(self.api.stats(p.handle,0).connections,0);p.close()
    def test_cross_thread_atomic_cancellation(self):
        p=Pair(self).ready()
        for _ in range(20):p.step()
        create,signal,close=self.api.dlm_net_cancel_new,self.api.dlm_net_cancel_signal,self.api.dlm_net_cancel_close;create.restype,create.argtypes=C.c_void_p,[];signal.argtypes=close.argtypes=[C.c_void_p];flag=create();self.assertTrue(flag);result=[]
        worker=threading.Thread(target=lambda:result.append(self.api.poll(p.handle,5000,flag).status));worker.start();time.sleep(0.03);signal(flag);worker.join(0.5);self.assertFalse(worker.is_alive());self.assertEqual(result,[9]);close(flag)
    def test_invalid_configuration_missing_credentials(self):
        for config in (Config.default(connections=0),Config.default(streams=1),Config.default(events=1),Config.default(memory=1024),Config.default(window=32),Config.default(handshake_ms=11000)):
            r=self.api.open(address(),False,base.Text.of('p'),base.Text.of(''),base.Text.of(''),config);self.assertEqual((r.status,r.handle),(4,None))
        r=self.api.open(address(),True,base.Text.of('p'),base.Text.of('missing-cert.pem'),base.Text.of('missing-key.pem'),Config.default());self.assertEqual((r.status,r.handle),(5,None))
    def test_loader_override_no_fallback(self):
        code='import ctypes as c,sys;x=c.CDLL(sys.argv[1]);assert x.dlm_net_quic_version()==0'
        subprocess.run(['python3','-c',code,str(self.network_path)],env=dict(os.environ,DLM_LIBQUIC='relative-library.so'),check=True)
    def test_reset_final_size_returns_connection_credit(self):
        p=Pair(self,dlm_server=True,window=1024).ready();stream=p.peer.get_next_available_stream_id()
        p.peer.send_stream_data(stream,b'lost bytes'*102+b'1234',end_stream=False)
        # Deliberately drop the generated STREAM datagram, then deliver RESET.
        dropped=p.peer.datagrams_to_send(now=time.monotonic());self.assertTrue(dropped)
        p.peer.reset_stream(stream,55)
        p.until(lambda:any(e.kind==4 and e.code==55 for e,_ in p.dlm_events))
        next_stream=p.peer.get_next_available_stream_id();message=b'after reset'*93
        p.peer.send_stream_data(next_stream,message,end_stream=True)
        p.until(lambda:any(e.kind==3 and e.stream==next_stream and e.flags&1 for e,_ in p.dlm_events))
        self.assertEqual(b''.join(d for e,d in p.dlm_events if e.kind==3 and e.stream==next_stream),message)
    def test_stateless_version_negotiation(self):
        p=Pair(self,dlm_server=True)
        data,target=p.peer.datagrams_to_send(now=time.monotonic())[0];packet=bytearray(data);packet[1:5]=bytes.fromhex('1a2a3a4a');p.sock.sendto(packet,target)
        for _ in range(50):
            self.api.poll(p.handle,1,None)
            try:reply,_=p.sock.recvfrom(65536)
            except BlockingIOError:continue
            header=pull_quic_header(Buffer(data=reply),host_cid_length=8)
            self.assertEqual(header.version,0);self.assertIn(1,header.supported_versions);self.assertIn(int(QuicProtocolVersion.VERSION_2),header.supported_versions)
            self.assertEqual(self.api.stats(p.handle,0).connections,0);return
        self.fail('No stateless version-negotiation response')
class DlmQuicTests(unittest.TestCase):
    def test_runtime_failure_closes_partial_move_owners(self):
        compiler=os.environ.get('DLM_NETWORK_DLM',str(ROOT/'target/release'/('dlm.exe' if os.name=='nt' else 'dlm')))
        with tempfile.TemporaryDirectory(prefix='dln-quic-owners-') as directory:
            provider=os.environ.get('DLM_QUIC_PREBUILT') or os.environ.get('DLM_LIBQUIC')
            if not provider:
                provider=str(Path(directory)/'libdlm-quic-1.so')
                subprocess.run(['python3',str(ROOT/'native/quic/build.py'),provider],check=True,capture_output=True,text=True)
            os.environ['DLM_LIBQUIC']=str(Path(provider).resolve())
            output=Path(directory)/('owners.dll' if os.name=='nt' else 'owners.so')
            cmd=[compiler,'build',str(HERE/'dln_quic.dlm'),'--lib',str(ROOT/'lib'),'--allow-unsafe','--target','sharedlib','-o',str(output)]
            r=subprocess.run(cmd,capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            api=C.CDLL(str(output));api.quic_fixture_probe.restype=C.c_int32;api.quic_abort_with_partial_move.restype=None;api.quic_shutdown.restype=base.Io;api.quic_network_shutdown.restype=base.Io;api.dlm_native_last_error.restype=C.c_char_p;api.dlm_native_live_allocations.restype=U64
            previous=os.getcwd()
            try:
                os.chdir(ROOT)
                for _ in range(16):
                    self.assertEqual(api.quic_fixture_probe(),0);api.quic_abort_with_partial_move();self.assertTrue(api.dlm_native_last_error());self.assertEqual(api.dlm_native_live_allocations(),0);self.assertEqual(api.quic_shutdown().status,0);self.assertEqual(api.quic_network_shutdown().status,0)
            finally:os.chdir(previous)
if __name__=='__main__':unittest.main(verbosity=2)
