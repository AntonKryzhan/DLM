#!/usr/bin/env python3
"""DLN-07 message contracts against Python TCP/TLS/UDP and aioquic peers.
Run with aioquic==1.8.0 and the dependencies of dln_quic.py. No public servers.
DLM_QUIC_PREBUILT may select the pinned provider already built from source.
"""
import ctypes as C
import importlib.util
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import tempfile
import threading
import time
import unittest

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('transport_quic_peer',HERE/'dln_quic.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
b=q.base;ROOT=q.ROOT;FIXTURES=q.FIXTURES
U32,U64,SZ=C.c_uint32,C.c_uint64,C.c_size_t
class SocketOptions(C.Structure):
    _fields_=[(n,U32) for n in ('no_delay','keep_alive','send_buffer','receive_buffer')]
class Requirements(C.Structure):
    _fields_=[(n,U32) for n in ('required','forbidden','preferred')]
class Bounds(C.Structure):
    _fields_=[(n,U32) for n in ('max_message','queued_messages','channels')]+[('memory',SZ)]
    @classmethod
    def default(cls,**changes):
        values=dict(max_message=1024,queued_messages=8,channels=8,memory=8<<20);values.update(changes);return cls(**values)
class Target(C.Structure):
    _fields_=[('local',b.Address),('peer',b.Address)]+[(n,b.Text) for n in ('identity','ca','pin','alpn')]
class Message(C.Structure):
    _fields_=[('id',U64),('channel',U32),('count',SZ),('status',C.c_int32),('system',C.c_int32)]
class Delivery(C.Structure):
    _fields_=[('id',U64),('channel',U32),('copied',SZ),('required',SZ),('status',C.c_int32),('system',C.c_int32)]
class Statistics(C.Structure):
    _fields_=[(n,SZ) for n in ('used','limit','adapter_limit','backend_limit')]+[(n,U32) for n in ('profile','properties','state','send_queued','receive_queued','channels')]+[('status',C.c_int32),('system',C.c_int32)]
class Choice(C.Structure):
    _fields_=[(n,U32) for n in ('profile','properties','missing','forbidden','preference_misses','cost')]+[('status',C.c_int32)]
def alpn(profile):return {4:'dlm-transport/1/ordered',5:'dlm-transport/1/unordered',6:'dlm-transport/1/datagram'}.get(profile,'')
def frame(data=b'',channel=0):return b'DLT1'+struct.pack('!III',len(data),channel,0)+data
def load(path):
    api=q.api_load(path)
    signatures={
        'open':(b.Opened,[U32,Requirements,Bounds,Target,U32,C.c_void_p]),'adopt':(b.Opened,[b.Opened,U32,Requirements,Bounds,b.Address,U64,C.c_bool]),
        'available':(U32,[]),'close':(None,[C.c_void_p]),'channel':(b.Io,[C.c_void_p]),'send':(b.Io,[C.c_void_p,U32,b.U8P,SZ]),
        'poll':(b.Io,[C.c_void_p,U32,C.c_void_p]),'peek':(Message,[C.c_void_p]),'copy':(b.Io,[C.c_void_p,U64,SZ,b.U8P,SZ]),'ack':(b.Io,[C.c_void_p,U64]),
        'receive':(Delivery,[C.c_void_p,b.U8P,SZ]),'stats':(Statistics,[C.c_void_p]),'local':(b.Resolved,[C.c_void_p])}
    for name,(result,args) in signatures.items():
        f=getattr(api,'dlm_net_transport_'+name);f.restype,f.argtypes=result,args;setattr(api,'t'+name,f)
    for name,result,args in [('provider_shutdown',b.Io,[]),('cancel_new',C.c_void_p,[]),('cancel_signal',None,[C.c_void_p]),('cancel_close',None,[C.c_void_p]),('tcp',b.Opened,[b.Text,C.c_uint16,U32]),('configure',b.Io,[C.c_void_p,SocketOptions])]:
        f=getattr(api,'dlm_net_'+name);f.restype,f.argtypes=result,args;setattr(api,'n'+name,f)
    return api

class TransportPair(q.Pair):
    def __init__(self,test,profile,server=False,**limits):
        bounds=Bounds.default(**limits);self.transport=None;self.profile=profile
        super().__init__(test,dlm_server=server,alpn=alpn(profile),connections=1,streams=bounds.channels*4,events=bounds.queued_messages*2+16,memory=bounds.memory//2,window=max(1024,bounds.max_message+16))
        self.ready()
        opened=self.api.tadopt(b.Opened(self.handle,0,0),profile,Requirements(16,0,0),bounds,b.Address(),self.connection,not server)
        test.assertEqual(opened.status,0,opened.system);self.transport=opened.handle
    def close(self):
        if self.transport:self.api.tclose(self.transport);self.transport=None;self.handle=None
        super().close()
    def step(self,ack=True):
        if not self.transport:return super().step(ack)
        r=self.api.tpoll(self.transport,0,None);self.test.assertIn(r.status,(0,1),(r.status,r.system))
        self.pump();return r
    def pump(self):
        now=time.monotonic()
        for _ in range(64):
            try:data,source=self.sock.recvfrom(65536)
            except BlockingIOError:break
            self.peer.receive_datagram(data,source,now=now)
        timer=self.peer.get_timer()
        if timer is not None and timer<=now:self.peer.handle_timer(now=now)
        while True:
            event=self.peer.next_event()
            if event is None:break
            self.peer_events.append(event)
        for data,target in self.peer.datagrams_to_send(now=now):self.sock.sendto(data,target)
    def wait(self,predicate,timeout=5):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            self.step()
            if predicate():return
            time.sleep(0.001)
        self.test.fail(f'transport wait expired: profile={self.profile}, state={self.api.tstats(self.transport).state}, peer={self.peer_events[-5:]}')

class TransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory(prefix='dln-transport-');directory=Path(cls.directory.name)
        provider=os.environ.get('DLM_QUIC_PREBUILT')
        if not provider:
            provider=str(directory/'libdlm-quic-1.so');subprocess.run(['python3',str(ROOT/'native/quic/build.py'),provider],check=True,capture_output=True,text=True)
        os.environ['DLM_LIBQUIC']=str(Path(provider).resolve());cls.path=directory/('transport.dll' if os.name=='nt' else 'transport.so')
        cmd=[os.environ.get('CC','cc'),'-std=gnu11','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-shared',str(ROOT/'crates/dlm_core/src/native/network.c'),'-o',str(cls.path)]+(['-lws2_32'] if os.name=='nt' else ['-fPIC','-ldl'])
        subprocess.run(cmd,check=True,capture_output=True,text=True);cls.api=load(cls.path)
    @classmethod
    def tearDownClass(cls):cls.directory.cleanup()
    def setUp(self):self.owners=[];self.handles=[];self.sockets=[]
    def tearDown(self):
        for owner in self.owners:owner.close()
        for handle in self.handles:self.api.tclose(handle)
        for sock in self.sockets:sock.close()
        self.assertEqual(self.api.shutdown().status,0);self.assertEqual(self.api.nprovider_shutdown().status,0)
    def opened(self,profile,port,req=None,bounds=None,identity='',ca='',pin='',cancel=None):
        target=Target(b.Address(),q.address(port),b.Text.of(identity),b.Text.of(ca),b.Text.of(pin),b.Text.of(alpn(profile)))
        return self.api.topen(profile,req or Requirements(16,0,0),bounds or Bounds.default(),target,2000,cancel)
    def tcp(self,bounds=None):
        listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();self.sockets.append(listener)
        opened=self.opened(1,listener.getsockname()[1],bounds=bounds);self.assertEqual(opened.status,0,opened.system);self.handles.append(opened.handle)
        peer,_=listener.accept();peer.settimeout(2);self.sockets.append(peer);return opened.handle,peer
    def until(self,handle,predicate,timeout=2):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            r=self.api.tpoll(handle,1,None);self.assertIn(r.status,(0,1,3),(r.status,r.system))
            if predicate():return
        self.fail('TCP/UDP message wait expired')
    def receive(self,handle,payload,channel=0):
        out=b.octets(b'\xa5'*(len(payload)+8));r=self.api.treceive(handle,out,len(out))
        self.assertEqual((r.status,r.channel,r.copied,r.required),(0,channel,len(payload),len(payload)));self.assertEqual(bytes(out)[:len(payload)],payload);self.assertEqual(bytes(out)[len(payload):],b'\xa5'*8);return r
    def test_tcp_fragmentation_empty_binary_and_retention(self):
        h,peer=self.tcp();payload=b'\0\xff\x80hello';wire=frame(payload)
        for byte in wire:
            peer.sendall(bytes([byte]));self.api.tpoll(h,0,None)
        self.until(h,lambda:self.api.tpeek(h).status==0)
        event=self.api.tpeek(h);small=b.octets(b'\xa5'*2);short=self.api.treceive(h,small,2)
        self.assertEqual((short.status,short.copied,short.required),(10,0,len(payload)));self.assertEqual(bytes(small),b'\xa5'*2);self.assertEqual(self.api.tpeek(h).id,event.id)
        out=b.octets(b'\xa5'*5);r=self.api.tcopy(h,event.id,2,out,3);self.assertEqual((r.status,r.count,bytes(out)),(0,3,payload[2:5]+b'\xa5'*2))
        self.receive(h,payload);self.assertEqual(self.api.tack(h,event.id).status,4)
        peer.sendall(frame()+frame(b'x'));self.until(h,lambda:self.api.tstats(h).receive_queued==2);self.receive(h,b'');self.receive(h,b'x')
    def test_tcp_queue_backpressure_and_bounded_memory(self):
        h,peer=self.tcp(Bounds.default(queued_messages=1));peer.sendall(frame(b'first')+frame(b'second'))
        self.until(h,lambda:self.api.tpeek(h).status==0)
        for _ in range(80):self.assertEqual(self.api.tpoll(h,0,None).status,0)
        stats=self.api.tstats(h);self.assertEqual(stats.receive_queued,1);self.assertLessEqual(stats.used,stats.limit);self.receive(h,b'first');self.until(h,lambda:self.api.tpeek(h).status==0);self.receive(h,b'second')
        self.assertEqual(self.api.tsend(h,0,b.octets(b'a'),1).status,0);self.assertEqual(self.api.tsend(h,0,b.octets(b'b'),1).status,1)
        self.api.tpoll(h,0,None);self.assertEqual(peer.recv(17),frame(b'a'))
    def test_tcp_partial_writes_preserve_copied_payload(self):
        listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();self.sockets.append(listener)
        raw=self.api.ntcp(b.Text.of('127.0.0.1'),listener.getsockname()[1],1000);self.assertEqual(raw.status,0);self.assertEqual(self.api.nconfigure(raw.handle,SocketOptions(1,0,4096,0)).status,0)
        peer,_=listener.accept();peer.setblocking(False);self.sockets.append(peer)
        opened=self.api.tadopt(raw,1,Requirements(19,0,0),Bounds.default(max_message=2<<20,memory=16<<20),b.Address(),0,True);self.assertEqual(opened.status,0);h=opened.handle;self.handles.append(h)
        payload=bytes(range(256))*8192;source=b.octets(payload);self.assertEqual(self.api.tsend(h,0,source,len(source)).status,0);C.memset(source,0,len(source))
        self.api.tpoll(h,0,None);self.assertEqual(self.api.tstats(h).send_queued,1,'Small send buffer must leave a partial frame queued')
        received=bytearray();end=time.monotonic()+6
        while len(received)<len(payload)+16 and time.monotonic()<end:
            r=self.api.tpoll(h,0,None);self.assertIn(r.status,(0,1))
            try:received.extend(peer.recv(65536))
            except BlockingIOError:time.sleep(.001)
        self.assertEqual(received,frame(payload));self.assertEqual(self.api.tstats(h).send_queued,0)
    def test_timeout_cancel_and_partial_message_survive(self):
        h,peer=self.tcp();peer.sendall(frame(b'ab')[:-1]);self.api.tpoll(h,0,None)
        self.assertEqual(self.api.tpoll(h,25,None).status,3)
        token=self.api.ncancel_new();thread=threading.Thread(target=lambda:(time.sleep(.02),self.api.ncancel_signal(token)));thread.start()
        try:self.assertEqual(self.api.tpoll(h,1000,token).status,9)
        finally:thread.join();self.api.ncancel_close(token)
        peer.sendall(b'b');self.until(h,lambda:self.api.tpeek(h).status==0);self.receive(h,b'ab')
    def test_eof_between_and_inside_messages(self):
        for wire,expected in [(frame(b'good'),2),(frame(b'good')+frame(b'bad')[:-1],10)]:
            h,peer=self.tcp();peer.sendall(wire);peer.shutdown(socket.SHUT_WR)
            for _ in range(100):
                self.api.tpoll(h,1,None)
                if self.api.tstats(h).state in (2,3):break
            self.receive(h,b'good');self.assertEqual(self.api.tpeek(h).status,expected)
    def test_reject_malformed_reserved_channel_and_oversize(self):
        for wire,expected in [(b'BAD!'+frame()[4:],11),(b'DLT1'+struct.pack('!III',0,0,1),11),(frame(b'x',7),11),(b'DLT1'+struct.pack('!III',1025,0,0),7)]:
            h,peer=self.tcp();peer.sendall(wire)
            for _ in range(50):
                r=self.api.tpoll(h,1,None)
                if r.status not in (0,1,3):break
            self.assertEqual(r.status,expected);self.assertEqual(self.api.tpoll(h,0,None).status,expected)
    def test_hard_requirements_rejected_before_connect_and_no_downgrade(self):
        self.assertEqual(self.opened(1,1,Requirements(4,0,0)).status,6)
        self.assertEqual(self.opened(1,1,Requirements(256,0,0)).status,6)
        self.assertEqual(self.opened(3,1,Requirements(1,0,0)).status,6)
        self.assertEqual(self.opened(1,1,Requirements(1,1,0)).status,4)
        self.assertEqual(self.opened(6,1,bounds=Bounds.default(max_message=1085)).status,7)
        self.assertEqual(self.opened(4,1,pin='sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=').status,6)
        self.assertEqual(self.opened(4,1,bounds=Bounds.default(memory=1<<20)).status,7)
        token=self.api.ncancel_new();self.api.ncancel_signal(token)
        try:self.assertEqual(self.opened(1,1,cancel=token).status,9)
        finally:self.api.ncancel_close(token)
    def test_failed_adoption_consumes_owner(self):
        listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();self.sockets.append(listener)
        raw=self.api.ntcp(b.Text.of('127.0.0.1'),listener.getsockname()[1],1000);self.assertEqual(raw.status,0);peer,_=listener.accept();self.sockets.append(peer);peer.settimeout(1)
        opened=self.api.tadopt(raw,1,Requirements(4,0,0),Bounds.default(),b.Address(),0,True);self.assertEqual((opened.status,opened.handle),(6,None));self.assertEqual(peer.recv(1),b'')
    def test_udp_peer_pin_empty_and_binary(self):
        peer=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);peer.bind(('127.0.0.1',0));peer.settimeout(2);self.sockets.append(peer)
        opened=self.opened(3,peer.getsockname()[1]);self.assertEqual(opened.status,0);h=opened.handle;self.handles.append(h);target=('127.0.0.1',self.api.tlocal(h).address.port)
        stranger=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);self.sockets.append(stranger);stranger.sendto(b'bad',target);self.api.tpoll(h,0,None);self.assertEqual(self.api.tpeek(h).status,1)
        for payload in (b'',b'\0\xff\x80datagram'):
            peer.sendto(frame(payload),target);self.until(h,lambda:self.api.tpeek(h).status==0);self.receive(h,payload)
            self.assertEqual(self.api.tsend(h,0,b.octets(payload),len(payload)).status,0);self.api.tpoll(h,0,None);self.assertEqual(peer.recvfrom(65536)[0],frame(payload))
    def tls(self,identity='localhost',ca=None,pin='',old=False):
        listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(2);self.sockets.append(listener);results=[];ready=threading.Event()
        context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.minimum_version=ssl.TLSVersion.TLSv1_2 if old else ssl.TLSVersion.TLSv1_3
        if old:context.maximum_version=ssl.TLSVersion.TLSv1_2
        context.set_alpn_protocols(['h2','http/1.1']);context.load_cert_chain(str(FIXTURES/'quic-test-server.pem'),str(FIXTURES/'quic-test-key.pem'))
        def serve():
            sock=None
            try:
                sock,_=listener.accept();sock=context.wrap_socket(sock,server_side=True);sock.settimeout(2);results.append(sock.selected_alpn_protocol());ready.set()
                data=b''
                while len(data)<19:
                    part=sock.recv(19-len(data))
                    if not part:break
                    data+=part
                if data:results.append(data);sock.sendall(frame(b'tls\0\xff'));sock.recv(1)
                sock.close()
            except (OSError,ssl.SSLError) as error:results.append(error);ready.set()
            finally:
                if sock is not None:sock.close()
        thread=threading.Thread(target=serve);thread.start()
        opened=self.opened(2,listener.getsockname()[1],Requirements(4|1|16,0,0),identity=identity,ca=ca or str(FIXTURES/'quic-test-ca.pem'),pin=pin)
        return opened,thread,results
    def test_tls_verified_identity_numeric_route_and_no_http_alpn(self):
        self.assertTrue(self.api.tavailable()&2,'Configured job needs libcurl >=8.10 with TLS')
        opened,thread,results=self.tls();self.assertEqual(opened.status,0,opened.system);h=opened.handle;self.handles.append(h)
        try:
            self.assertEqual(self.api.tsend(h,0,b.octets(b'abc'),3).status,0);self.until(h,lambda:self.api.tpeek(h).status==0);self.receive(h,b'tls\0\xff')
        finally:self.api.tclose(h);self.handles.remove(h);thread.join(3)
        self.assertFalse(thread.is_alive());self.assertEqual(results,[None,frame(b'abc')])
    def test_tls_bad_identity_ca_pin_and_old_version_fail_closed(self):
        for args in [dict(identity='wrong.invalid'),dict(ca=str(FIXTURES/'quic-test-untrusted-ca.pem')),dict(pin='sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA='),dict(old=True)]:
            opened,thread,_=self.tls(**args);thread.join(3);self.assertFalse(thread.is_alive());self.assertNotEqual(opened.status,0);self.assertIsNone(opened.handle)
    def test_quic_all_profiles_both_roles_use_same_message_contract(self):
        for profile in (4,5,6):
            for server in (False,True):
                p=TransportPair(self,profile,server);h=p.transport
                self.assertEqual(self.api.tstats(h).properties,{4:191,5:253,6:244}[profile]);peer_stream=None
                for payload in (b'',b'\0\xff\x80QUIC'):
                    self.assertEqual(self.api.tsend(h,0,b.octets(payload),len(payload)).status,0)
                    if profile==6:p.wait(lambda:any(isinstance(e,q.DatagramFrameReceived) and e.data==frame(payload) for e in p.peer_events))
                    else:p.wait(lambda:frame(payload) in b''.join(e.data for e in p.peer_events if isinstance(e,q.StreamDataReceived)))
                    if profile==6:p.peer.send_datagram_frame(frame(payload))
                    else:
                        if profile==5 or peer_stream is None:peer_stream=p.peer.get_next_available_stream_id(is_unidirectional=True)
                        p.peer.send_stream_data(peer_stream,frame(payload),end_stream=profile==5)
                    p.wait(lambda:self.api.tpeek(h).status==0);self.receive(h,payload)
                p.close()
    def test_quic_independent_channels_and_replies(self):
        for profile in (4,5):
            p=TransportPair(self,profile);h=p.transport;opened=self.api.tchannel(h);self.assertEqual((opened.status,opened.count),(0,1))
            self.assertEqual(self.api.tsend(h,1,b.octets(b'local'),5).status,0)
            p.wait(lambda:any(isinstance(e,q.StreamDataReceived) for e in p.peer_events))
            a=p.peer.get_next_available_stream_id(is_unidirectional=True);p.peer.send_stream_data(a,frame(b'partial',2)[:-1],end_stream=False)
            z=p.peer.get_next_available_stream_id(is_unidirectional=True);p.peer.send_stream_data(z,frame(b'independent',4),end_stream=profile==5)
            p.wait(lambda:self.api.tpeek(h).status==0);self.receive(h,b'independent',4)
            p.peer.send_stream_data(a,b'l',end_stream=profile==5);p.wait(lambda:self.api.tpeek(h).status==0);self.receive(h,b'partial',2)
            self.assertEqual(self.api.tsend(h,4,b.octets(b'reply'),5).status,0);p.wait(lambda:any(isinstance(e,q.StreamDataReceived) and frame(b'reply',4) in e.data for e in p.peer_events));p.close()
    def test_quic_unordered_requires_fin_and_rejects_two_messages(self):
        p=TransportPair(self,5);h=p.transport;stream=p.peer.get_next_available_stream_id(is_unidirectional=True);p.peer.send_stream_data(stream,frame(b'not final'),end_stream=False)
        for _ in range(40):p.step()
        self.assertEqual(self.api.tpeek(h).status,1)
        p.peer.send_stream_data(stream,b'',end_stream=True);p.wait(lambda:self.api.tpeek(h).status==0);self.receive(h,b'not final');p.close()
        p=TransportPair(self,5);stream=p.peer.get_next_available_stream_id(is_unidirectional=True);p.peer.send_stream_data(stream,frame(b'a')+frame(b'b'),end_stream=True)
        for _ in range(100):
            r=self.api.tpoll(p.transport,0,None);p.pump()
            if r.status==11:break
            time.sleep(.001)
        self.assertEqual(r.status,11)
    def test_quic_bounded_receive_backpressure_and_channel_limit(self):
        p=TransportPair(self,5,queued_messages=1,channels=2);h=p.transport
        stream=p.peer.get_next_available_stream_id(is_unidirectional=True);p.peer.send_stream_data(stream,frame(b'one'),end_stream=True);p.wait(lambda:self.api.tpeek(h).status==0)
        for number in range(2,7):
            stream=p.peer.get_next_available_stream_id(is_unidirectional=True);p.peer.send_stream_data(stream,frame(str(number).encode()),end_stream=True)
        for _ in range(200):p.step()
        self.assertLessEqual(self.api.tstats(h).used,self.api.tstats(h).limit);self.assertEqual(self.api.tstats(h).state,1);self.receive(h,b'one')
        for number in range(2,7):p.wait(lambda:self.api.tpeek(h).status==0);self.receive(h,str(number).encode())
        self.assertEqual(self.api.tchannel(h).status,0);self.assertEqual(self.api.tchannel(h).status,7)
    def test_quic_ordered_coalesced_large_frames_resume_after_retention(self):
        p=TransportPair(self,4,queued_messages=1,max_message=16384);h=p.transport;payload=bytes(range(256))*64
        stream=p.peer.get_next_available_stream_id(is_unidirectional=True);p.peer.send_stream_data(stream,frame()+frame(payload)+frame(b'last'),end_stream=False)
        for expected in (b'',payload,b'last'):
            p.wait(lambda:self.api.tpeek(h).status==0);event=self.api.tpeek(h)
            for _ in range(30):p.step()
            self.assertEqual(self.api.tpeek(h).id,event.id);self.receive(h,expected)
    def test_quic_partial_frames_exhausting_receive_budget_fail_explicitly(self):
        p=TransportPair(self,4,channels=4,max_message=200000,memory=2<<20)
        for channel in (0,2,4):
            stream=p.peer.get_next_available_stream_id(is_unidirectional=True);p.peer.send_stream_data(stream,b'DLT1'+struct.pack('!III',200000,channel,0)+b'x',end_stream=False)
        end=time.monotonic()+3
        while time.monotonic()<end:
            r=self.api.tpoll(p.transport,0,None);p.pump()
            if r.status==7:break
            time.sleep(.001)
        self.assertEqual(r.status,7);stats=self.api.tstats(p.transport);self.assertLessEqual(stats.used,stats.limit);self.assertEqual(stats.receive_queued,0)
    def test_quic_truncated_fin_reset_and_bidirectional_stream_fail(self):
        for mode,expected in (('short',10),('reset',11),('bidi',11)):
            p=TransportPair(self,5);stream=p.peer.get_next_available_stream_id(is_unidirectional=mode!='bidi')
            if mode=='reset':p.peer.send_stream_data(stream,frame(b'x')[:-1],end_stream=False);p.peer.reset_stream(stream,17)
            else:p.peer.send_stream_data(stream,frame(b'x')[:-1] if mode=='short' else frame(b'x'),end_stream=True)
            end=time.monotonic()+3
            while time.monotonic()<end:
                r=self.api.tpoll(p.transport,0,None);p.pump()
                if r.status==expected:break
                time.sleep(.001)
            self.assertEqual(r.status,expected);p.close()
    def test_quic_bad_profile_alpn_and_authentication_do_not_fallback(self):
        peer=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);peer.bind(('127.0.0.1',0));peer.setblocking(False);self.sockets.append(peer)
        opened=self.opened(4,peer.getsockname()[1],Requirements(4|1,0,0),identity='wrong.invalid',ca=str(FIXTURES/'quic-test-ca.pem'))
        self.assertEqual(opened.status,0);self.handles.append(opened.handle)
        config=q.QuicConfiguration(is_client=False,alpn_protocols=[alpn(4)]);config.load_cert_chain(str(FIXTURES/'quic-test-server.pem'),str(FIXTURES/'quic-test-key.pem'));server=None;end=time.monotonic()+3
        while time.monotonic()<end:
            r=self.api.tpoll(opened.handle,0,None);now=time.monotonic()
            try:data,source=peer.recvfrom(65536)
            except BlockingIOError:data=None
            if data:
                if server is None:
                    header=q.pull_quic_header(q.Buffer(data=data),host_cid_length=8);server=q.QuicConnection(configuration=config,original_destination_connection_id=header.destination_cid)
                server.receive_datagram(data,source,now=now)
            if server:
                for data,target in server.datagrams_to_send(now=now):peer.sendto(data,target)
            if self.api.tstats(opened.handle).state==2:break
            time.sleep(.001)
        self.assertEqual(self.api.tstats(opened.handle).profile,4);self.assertNotEqual(self.api.tstats(opened.handle).status,0);self.assertEqual(self.api.tpeek(opened.handle).status,self.api.tstats(opened.handle).status)

class DlmTransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory(prefix='dln-transport-dlm-');directory=Path(cls.directory.name)
        compiler=os.environ.get('DLM_NETWORK_DLM',str(ROOT/'target/release'/('dlm.exe' if os.name=='nt' else 'dlm')))
        output=directory/('owners.dll' if os.name=='nt' else 'owners.so')
        result=subprocess.run([compiler,'build',str(HERE/'dln_transport.dlm'),'--lib',str(ROOT/'lib'),'--allow-unsafe','--target','sharedlib','-o',str(output)],capture_output=True,text=True)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        cls.api=C.CDLL(str(output));cls.api.transport_policy_probe.restype=Choice;cls.api.transport_policy_probe.argtypes=[U32,U32,U32,U32,U32,SZ]
        cls.api.transport_abort_with_partial_move.restype=None;cls.api.transport_fixture_shutdown.restype=b.Io;cls.api.transport_fixture_quic_shutdown.restype=b.Io
        cls.api.transport_invalid_quic_profile.restype=C.c_int32;cls.api.dlm_native_last_error.restype=C.c_char_p;cls.api.dlm_native_live_allocations.restype=U64
    @classmethod
    def tearDownClass(cls):cls.directory.cleanup()
    def test_selection_matches_independent_property_oracle(self):
        # The reference uses sets and sorting, independently of the DLM loop.
        properties={1:{0,1,4,5,7},2:{0,1,2,4,5,7},3:{4,6,7},4:{0,1,2,3,4,5,7},5:{0,2,3,4,5,6,7},6:{2,4,5,6,7}}
        costs={1:10,2:30,3:1,4:20,5:20,6:5}
        def flags(value):return {bit for bit in range(9) if value&(1<<bit)}
        for required in range(512):
            for forbidden in (0,1,2,4,64,256):
                for preferred in (0,4,8,32):
                    for available in (0,1,7,63):
                        got=self.api.transport_policy_probe(required,forbidden,preferred,available,1024,8<<20)
                        invalid=bool(required&forbidden or preferred&forbidden or required&66==66)
                        if invalid:self.assertEqual(got.status,4);continue
                        candidates=[p for p,caps in properties.items() if available&(1<<(p-1)) and flags(required)<=caps and not flags(forbidden)&caps]
                        if not candidates:self.assertEqual((got.profile,got.status),(0,6));continue
                        best=min(candidates,key=lambda p:(len(flags(preferred)-properties[p]),costs[p],p))
                        self.assertEqual((got.profile,got.status,got.properties),(best,0,sum(1<<bit for bit in properties[best])))
        for size,memory,expected in ((1085,8<<20,0),(1024,1<<20,0),(1024,8<<20,6)):
            got=self.api.transport_policy_probe(84,3,0,32,size,memory);self.assertEqual(got.profile,expected)
    def test_runtime_abort_closes_moved_sessions_and_queued_messages(self):
        for _ in range(24):
            self.api.transport_abort_with_partial_move();self.assertTrue(self.api.dlm_native_last_error());self.assertEqual(self.api.dlm_native_live_allocations(),0)
            self.assertEqual(self.api.transport_fixture_shutdown().status,0)
    def test_bad_quic_profile_consumes_typed_endpoint(self):
        provider=os.environ.get('DLM_QUIC_PREBUILT')
        if not provider:self.skipTest('Set DLM_QUIC_PREBUILT for the optional typed QUIC owner check')
        os.environ['DLM_LIBQUIC']=str(Path(provider).resolve());previous=os.getcwd()
        try:
            os.chdir(ROOT)
            for _ in range(16):
                self.assertEqual(self.api.transport_invalid_quic_profile(),4);self.assertEqual(self.api.dlm_native_live_allocations(),0);self.assertEqual(self.api.transport_fixture_quic_shutdown().status,0);self.assertEqual(self.api.transport_fixture_shutdown().status,0)
        finally:os.chdir(previous)

if __name__=='__main__':unittest.main(verbosity=2)
