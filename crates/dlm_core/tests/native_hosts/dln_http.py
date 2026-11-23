#!/usr/bin/env python3
import ctypes as C
import email.parser
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import threading
import time
import unittest
import urllib.parse
from websockets.sync.client import connect as websocket_connect
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('http_continuity',HERE/'dln_continuity.py');continuity=importlib.util.module_from_spec(spec);spec.loader.exec_module(continuity);base=continuity.base
P,N,U,Q=base.P,base.N,base.U,base.Q
class Stats(C.Structure):_fields_=[(n,Q)for n in('accepted','authenticated','denied','active','incoming','outgoing','udp','ip','failed')]
def vint(n):
 size,tag=(1,0)if n<64 else(2,64)if n<16384 else(4,128)if n<2**30 else(8,192);b=bytearray(n.to_bytes(size,'big'));b[0]|=tag;return bytes(b)
def capsule(kind,body):return vint(kind)+vint(len(body))+body
def read_vint(s):
 first=base.exact(s,1);n=1<<(first[0]>>6);return int.from_bytes(bytes([first[0]&63])+base.exact(s,n-1),'big')
def read_capsule(s):kind=read_vint(s);n=read_vint(s);assert n<=65536;return kind,base.exact(s,n)
def read_head(s):
 out=bytearray()
 while not out.endswith(b'\r\n\r\n'):out.extend(base.exact(s,1));assert len(out)<=8192
 return bytes(out)
class WebSocketSocket:
 def __init__(self,connection):self.ws=connection;self.input=bytearray();self.timeout=2
 def settimeout(self,n):self.timeout=n
 def getsockname(self):return self.ws.socket.getsockname()
 def sendall(self,data):self.ws.send([data[:7],data[7:20],data[20:]])
 def recv(self,n):
  while not self.input:
   try:message=self.ws.recv(timeout=self.timeout)
   except Exception as e:raise OSError(str(e))from e
   if not isinstance(message,bytes):raise ValueError('nonbinary WebSocket message')
   self.input.extend(message)
  data=bytes(self.input[:n]);del self.input[:n];return data
 def close(self):self.ws.close()
class IndependentMasque:
 def __init__(self,case,target):
  self.listener=socket.socket();self.listener.bind(('127.0.0.1',0));self.listener.listen(8);self.listener.settimeout(.05);self.address='127.0.0.1:%d'%self.listener.getsockname()[1];self.stop=threading.Event();self.workers=[];self.connections=[];self.target=target;self.secret='Bearer '+case.credential().read_bytes().hex();self.errors=[];context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.minimum_version=ssl.TLSVersion.TLSv1_3;context.set_alpn_protocols(['http/1.1']);context.load_cert_chain(case.fixtures/'quic-test-server.pem',case.fixtures/'quic-test-key.pem')
  def serve(raw):
   try:
    raw.settimeout(3)
    with context.wrap_socket(raw,server_side=True)as peer:
     self.connections.append(peer);head=read_head(peer);start,fields=head.split(b'\r\n',1);headers=email.parser.BytesParser().parsebytes(fields);assert start.startswith(b'GET ')and start.endswith(b' HTTP/1.1');assert headers['Proxy-Authorization']==self.secret;assert headers['Upgrade'].lower()=='connect-udp';assert headers['Capsule-Protocol']=='?1';parts=urllib.parse.urlsplit(start.decode().split(' ')[1]).path.split('/');assert parts[:4]==['','.well-known','masque','udp'];target=(urllib.parse.unquote(parts[4]),int(parts[5]));assert '%s:%d'%target==self.target
     with socket.socket(socket.AF_INET,socket.SOCK_DGRAM)as udp:
      udp.connect(target);udp.setblocking(False);peer.sendall(b'HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: connect-udp\r\nCapsule-Protocol: ?1\r\n\r\n');peer.setblocking(False);incoming=bytearray();outgoing=bytearray()
      while not self.stop.is_set():
       progress=False
       try:
        block=peer.recv(65536)
        if not block:return
        incoming.extend(block);assert len(incoming)<=131072;progress=True
       except(ssl.SSLWantReadError,ssl.SSLWantWriteError,BlockingIOError):pass
       while incoming:
        a=1<<(incoming[0]>>6)
        if len(incoming)<a:break
        kind=int.from_bytes(bytes([incoming[0]&63])+incoming[1:a],'big')
        if len(incoming)<=a:break
        b=1<<(incoming[a]>>6)
        if len(incoming)<a+b:break
        n=int.from_bytes(bytes([incoming[a]&63])+incoming[a+1:a+b],'big');assert n<=65536
        if len(incoming)<a+b+n:break
        body=bytes(incoming[a+b:a+b+n]);del incoming[:a+b+n]
        if kind==0 and body[:1]==b'\0':udp.send(body[1:])
       for _ in range(16):
        try:data=udp.recv(65535);outgoing.extend(capsule(0,b'\0'+data));assert len(outgoing)<=131072;progress=True
        except BlockingIOError:break
       if outgoing:
        try:n=peer.send(outgoing);del outgoing[:n];progress=True
        except(ssl.SSLWantReadError,ssl.SSLWantWriteError,BlockingIOError):pass
       if not progress:time.sleep(.001)
   except(OSError,EOFError):pass
   except Exception as e:self.errors.append(str(e))
  def accept():
   while not self.stop.is_set():
    try:raw,_=self.listener.accept();worker=threading.Thread(target=serve,args=(raw,),daemon=True);worker.start();self.workers.append(worker)
    except socket.timeout:pass
    except OSError:return
  self.worker=threading.Thread(target=accept,daemon=True);self.worker.start()
 def close(self):
  self.stop.set();self.listener.close();self.worker.join(3)
  for peer in self.connections:
   try:peer.shutdown(socket.SHUT_RDWR)
   except OSError:pass
   peer.close()
  for worker in self.workers:worker.join(4)
class HttpTests(unittest.TestCase):
 command=base.GatewayTests.command;server_text=base.GatewayTests.server_text;client_text=base.GatewayTests.client_text;start=base.GatewayTests.start;start_process=base.GatewayTests.start_process;setUp=base.GatewayTests.setUp
 pool_text=continuity.ContinuityTests.pool_text;pool=continuity.ContinuityTests.pool;listeners=continuity.ContinuityTests.listeners;make_descriptor=continuity.ContinuityTests.make_descriptor;stream=continuity.ContinuityTests.stream;read_all=continuity.ContinuityTests.read_all
 @classmethod
 def setUpClass(cls):
  continuity.ContinuityTests.setUpClass.__func__(cls)
  signatures={'proxy_open':(base.Opened,[P,N,U,P,N]),'proxy_close':(None,[P]),'proxy_write':(base.Io,[P,P,N]),'proxy_read':(base.Transfer,[P,N,P,N]),'proxy_take':(base.Transfer,[P,P,N]),'proxy_cancel_open':(base.Opened,[P]),'proxy_cancel_request':(base.Io,[P]),'proxy_cancel_close':(None,[P]),'proxy_server_open':(base.Opened,[P,N]),'proxy_server_close':(None,[P]),'proxy_server_address':(base.Io,[P,P,N]),'proxy_server_statistics':(Stats,[P]),'carriers':(base.Transfer,[P,N])}
  for name,(result,args)in signatures.items():f=getattr(cls.api,'dln_gateway_'+name);f.restype=result;f.argtypes=args;setattr(cls,name,staticmethod(f))
  library=Path(cls.build.name)/('proxy.dll'if os.name=='nt'else'proxy.so');args=[str(cls.compiler),'build',str(HERE/'gateway_proxy_boundary.dlm'),'--lib',str(base.ROOT/'lib'),'--allow-unsafe','--target','sharedlib','--library','dlm_gateway','--library-dir',str(cls.profile),'-o',str(library)]
  if os.name!='nt':args+=['--cflag','-Wl,-rpath,'+str(cls.profile)]
  r=subprocess.run(args,capture_output=True);assert r.returncode==0,r.stderr.decode();cls.proxy_boundary=C.CDLL(str(library));cls.proxy_boundary.gateway_proxy_dlm.restype=Q;cls.proxy_boundary.gateway_proxy_dlm.argtypes=[P,N,P,N,U];cls.proxy_boundary.gateway_proxy_abi.restype=Stats;cls.proxy_boundary.gateway_proxy_abi.argtypes=[Stats]
  fixture=Path(cls.build.name)/'proxy.a';subprocess.run([str(cls.compiler),'build',str(HERE/'gateway_proxy_boundary.dlm'),'--lib',str(base.ROOT/'lib'),'--allow-unsafe','--target','staticlib','-o',str(fixture)],check=True,capture_output=True)
  cls.proxy_host=Path(cls.build.name)/('proxy-host.exe'if os.name=='nt'else'proxy-host');command=[os.environ.get('CC','gcc'),'-std=gnu11','-O1','-Wall','-Wextra','-Werror','-UNDEBUG',str(HERE/'dln_proxy_host.c'),str(fixture),'-L'+str(cls.profile),'-ldlm_gateway'];command+=['-lws2_32','-lshell32','-lgdi32','-luser32']if os.name=='nt'else['-pthread','-ldl','-lm','-Wl,-rpath,'+str(cls.profile)];subprocess.run(command+['-o',str(cls.proxy_host)],check=True,capture_output=True)
 @classmethod
 def tearDownClass(cls):continuity.ContinuityTests.tearDownClass.__func__(cls)
 def credential(self):
  p=self.path/'proxy.key'
  if not p.exists():self.command('journal-keygen','--output',p)
  return p
 def proxy_text(self,udp=None,extra=''):
  text=f'listen=127.0.0.1:0\ncertificate={self.fixtures/"quic-test-server.pem"}\nkey={self.fixtures/"quic-test-key.pem"}\ntoken={self.credential()}\nallow_private=true\nallow_target=tcp,127.0.0.0/8,{self.echo.address.rsplit(":",1)[1]}\n'
  if udp:text+=f'allow_target=udp,127.0.0.0/8,{udp.rsplit(":",1)[1]}\n'
  return text+extra
 def proxy_server(self,text):
  data=text.encode();r=self.proxy_server_open(data,len(data));self.assertEqual(r.status,0);self.addCleanup(self.proxy_server_close,r.handle);out=C.create_string_buffer(128);n=self.proxy_server_address(r.handle,out,128);self.assertEqual(n.status,0);return r.handle,out.raw[:n.count].decode()
 def settings(self,address,extra=''):return f'proxy_address={address}\nproxy_hostname=localhost\nproxy_ca={self.fixtures/"quic-test-ca.pem"}\nproxy_token={self.credential()}\nproxy_timeout_ms=3000\n'+extra
 def tunnel(self,config,mode,target):
  config=config.encode();target=target.encode();r=self.proxy_open(config,len(config),mode,target,len(target));self.assertEqual(r.status,0)
  if not hasattr(self,'owned_tunnels'):self.owned_tunnels=set()
  self.owned_tunnels.add(r.handle)
  def close():
   if r.handle in self.owned_tunnels:self.proxy_close(r.handle);self.owned_tunnels.remove(r.handle)
  self.addCleanup(close);return r.handle
 def raw_tls(self,address):
  h,p=address.rsplit(':',1);ctx=ssl.create_default_context(cafile=str(self.fixtures/'quic-test-ca.pem'));ctx.minimum_version=ssl.TLSVersion.TLSv1_3;ctx.set_alpn_protocols(['http/1.1']);s=ctx.wrap_socket(socket.create_connection((h,int(p)),timeout=3),server_hostname='localhost');s.settimeout(3);self.addCleanup(s.close);return s
 def raw_masque(self,address,target,token=None,path=None,upgrade='connect-udp'):
  s=self.raw_tls(address);port=target.rsplit(':',1)[1];host=target.rsplit(':',1)[0];path=path or f'/.well-known/masque/udp/{host}/{port}/';auth=token or self.credential().read_bytes().hex();s.sendall(f'GET https://localhost:{address.rsplit(":",1)[1]}{path} HTTP/1.1\r\nHost: localhost:{address.rsplit(":",1)[1]}\r\nConnection: Upgrade\r\nUpgrade: {upgrade}\r\nCapsule-Protocol: ?1\r\nProxy-Authorization: Bearer {auth}\r\n\r\n'.encode());return s,read_head(s)
 def test_websocket_and_connect_binary_dlm_static_host_and_half_close(self):
  for kind in('websocket','https-connect'):
   with self.subTest(kind=kind):
    server=self.path/(kind+'.server');client=self.path/(kind+'.client');server.write_text(self.server_text(kind=kind).replace(self.echo.address.rsplit(':',1)[1],'__ECHO_PORT__'));client.write_text(self.client_text('__GATEWAY__',kind));r=subprocess.run([str(self.host),str(server),str(client)],capture_output=True,timeout=20);self.assertEqual(r.returncode,0,r.stderr.decode());self.assertEqual(json.loads(r.stdout)['status'],'PASS');_,address=self.start(self.server_text(kind=kind));cfg=self.path/(kind+'.cfg');cfg.write_text(self.client_text(address,kind));data=bytes(range(256))*257+b'\0\xff';(self.path/'input').write_bytes(data);r=self.command('exchange','--config',cfg,'--protocol','tcp','--target',self.echo.address,'--input',self.path/'input','--output',self.path/'output');self.assertEqual(r['received'],len(data));self.assertEqual((self.path/'output').read_bytes(),data)
 def test_independent_websocket_library_verifies_signed_gateway_and_binary_exchange(self):
  handle,address=self.start(self.server_text(kind='websocket'));port=address.rsplit(':',1)[1];ctx=ssl.create_default_context(cafile=str(self.fixtures/'quic-test-ca.pem'));ws=websocket_connect(f'wss://localhost:{port}/dln/v1',ssl=ctx,subprotocols=['dln.gateway.v1'],proxy=None,open_timeout=3);channel=WebSocketSocket(ws);self.addCleanup(channel.close);peer=base.Oracle(self,address,channel=channel,profile=7);stream=peer.open(self.echo.address);payload=b'websocket\0\xff';status,_,count,_=peer.request(3,struct.pack('>II',stream,len(payload))+payload);self.assertEqual((status,count),(0,len(payload)));self.assertEqual(peer.request(4,struct.pack('>II',stream,len(payload)))[3],payload);self.assertTrue(ws.ping(b'probe').wait(2));self.assertEqual(self.server_statistics(handle).forwarded,len(payload))
 def test_independent_connect_client_verifies_application_identity(self):
  _,address=self.start(self.server_text(kind='https-connect'));s=self.raw_tls(address);s.sendall(f'CONNECT localhost:{address.rsplit(":",1)[1]} HTTP/1.1\r\nHost: localhost:{address.rsplit(":",1)[1]}\r\n\r\n'.encode());self.assertIn(b'200',read_head(s));peer=base.Oracle(self,address,channel=s,profile=8);stream=peer.open(self.echo.address);self.assertEqual(peer.request(3,struct.pack('>II',stream,3)+b'A\0\xff')[0],0);self.assertEqual(peer.request(4,struct.pack('>II',stream,3))[3],b'A\0\xff')
 def test_http_smuggling_and_websocket_frame_faults_never_authenticate(self):
  handle,address=self.start(self.server_text(kind='websocket'));faults=[b'\x82\x01x',b'\xc2\x80'+bytes(4),b'\x89\xfe\x00\x7e'+bytes(4),b'\x80\x80'+bytes(4),b'\x82\xfe\x00\x01'+bytes(4)+b'x',b'\x82\xff'+(2**63).to_bytes(8,'big')+bytes(4),b'\x88\x81'+bytes(4)+b'x',b'\x88\x83'+bytes(4)+b'\x03\xe8\xff']
  for frame in faults:
   s=self.raw_tls(address);s.sendall(f'GET /dln/v1 HTTP/1.1\r\nHost: localhost:{address.rsplit(":",1)[1]}\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Protocol: dln.gateway.v1\r\n\r\n'.encode());self.assertIn(b'101',read_head(s));s.sendall(frame)
   try:self.assertEqual(s.recv(256),b'')
   except ssl.SSLError:pass
   s.close()
  for extra in('Host: duplicate\r\n','Content-Length: 0\r\n','Transfer-Encoding: chunked\r\n',' X: folded\r\n'):
   s=self.raw_tls(address);s.sendall(f'GET /dln/v1 HTTP/1.1\r\nHost: localhost:443\r\n{extra}\r\n'.encode());self.assertIn(b'400',read_head(s));s.close()
  self.assertEqual((self.server_statistics(handle).authenticated,self.server_statistics(handle).forwarded),(0,0))
 def test_masque_udp_empty_binary_mtu_and_retained_short_output(self):
  udp=base.Echo(udp=True);self.addCleanup(udp.close);handle,address=self.proxy_server(self.proxy_text(udp.address));t=self.tunnel(self.settings(address),2,udp.address);self.assertEqual(self.proxy_read(t,65535,None,1).status,4)
  for size in(0,1,1200,8192,65507):
   data=bytes((i*17)&255 for i in range(size));self.assertEqual(self.proxy_write(t,data,len(data)).status,0);out=C.create_string_buffer(max(1,size));r=self.proxy_read(t,65535,out,size);self.assertEqual((r.status,r.copied),(0,size));self.assertEqual(out.raw[:size],data)
  data=b'A\0\xff';self.assertEqual(self.proxy_write(t,data,3).status,0);small=C.create_string_buffer(b'\xa5',1);r=self.proxy_read(t,65535,small,1);self.assertEqual((r.status,r.required,small.raw),(10,3,b'\xa5'));out=C.create_string_buffer(4);r=self.proxy_take(t,out,4);self.assertEqual((r.status,r.copied,out.raw[:3]),(0,3,data));self.assertEqual(self.proxy_server_statistics(handle).udp,1)
 def test_masque_independent_http_capsules_unknown_context_and_zero_udp(self):
  udp=base.Echo(udp=True);self.addCleanup(udp.close);_,address=self.proxy_server(self.proxy_text(udp.address));s,head=self.raw_masque(address,udp.address);self.assertIn(b'101',head);self.assertIn(b'Capsule-Protocol: ?1',head);s.sendall(capsule(499,bytes(65536))+capsule(0,b'\x01ignored')+capsule(0,b'\0A\0\xff'));self.assertEqual(read_capsule(s),(0,b'\0A\0\xff'));self.assertEqual(udp.received,3);s.sendall(capsule(0,b'\0'));self.assertEqual(read_capsule(s),(0,b'\0'))
 def test_proxy_auth_acl_ip_mode_and_http_version_are_distinct(self):
  udp=base.Echo(udp=True);self.addCleanup(udp.close);handle,address=self.proxy_server(self.proxy_text(udp.address));settings=self.settings(address);wrong=self.path/'wrong.key';wrong.write_bytes(bytes(32));bad=settings.replace(str(self.credential()),str(wrong)).encode();target=udp.address.encode();self.assertEqual(self.proxy_open(bad,len(bad),2,target,len(target)).status,16);text=settings.encode();self.assertEqual(self.proxy_open(text,len(text),3,b'*,*',3).status,6);self.assertEqual(self.proxy_open(text,len(text),2,b'127.0.0.1:1',11).status,16);accepted=self.proxy_server_statistics(handle).accepted
  for version in(2,3):
   cfg=(settings+f'proxy_http_version={version}\n').encode();self.assertEqual(self.proxy_open(cfg,len(cfg),2,target,len(target)).status,6)
  self.assertEqual(self.proxy_server_statistics(handle).accepted,accepted);self.assertEqual(udp.received,0)
 def test_proxy_cancellation_is_independent_of_blocking_read_ownership(self):
  udp=base.Echo(udp=True);self.addCleanup(udp.close);_,address=self.proxy_server(self.proxy_text(udp.address));t=self.tunnel(self.settings(address),2,udp.address);token=self.proxy_cancel_open(t);self.assertEqual(token.status,0);self.addCleanup(self.proxy_cancel_close,token.handle);reply=[];entered=threading.Event()
  def reader():entered.set();out=C.create_string_buffer(32);reply.append(self.proxy_read(t,65535,out,32))
  worker=threading.Thread(target=reader);worker.start();self.assertTrue(entered.wait(1));time.sleep(.02);self.assertEqual(self.proxy_cancel_request(token.handle).status,0);worker.join(2);self.assertFalse(worker.is_alive());self.assertEqual(reply[0].status,9)
 def test_masque_quic_gateway_pool_and_durable_delivery(self):
  group,_=self.start(self.server_text(spool=True)+'max_client_sessions=8\nlisten_alternate=quic-datagram,127.0.0.1:0\nlisten_alternate=websocket,127.0.0.1:0\nlisten_alternate=https-connect,127.0.0.1:0\n');paths=self.listeners(group);quic=next(a for k,a in paths if k=='quic-datagram');_,proxy=self.proxy_server(self.proxy_text(quic));paths.append(('quic-datagram',quic));text=self.pool_text(paths)+self.settings(proxy)+'proxy_paths=4\n';pool=self.pool(text);self.assertEqual(self.pool_refresh(pool).status,0);self.assertEqual(self.pool_statistics(pool).ready_paths,5);data=os.urandom(131089);desc=self.make_descriptor(data);r=self.pool_upload(pool,desc,data,len(data));self.assertEqual((r.status,r.state),(0,4));self.assertEqual(self.pool_disconnect(pool,0).status,0);out=C.create_string_buffer(len(data));r=self.pool_download(pool,desc,out,len(data));self.assertEqual(r.status,0);self.assertEqual(hashlib.sha256(out.raw[:r.copied]).digest(),bytes(desc.digest));self.assertEqual(self.pool_statistics(pool).independent_pairs,0)
 def test_dlm_proxy_resource_and_c_layout(self):
  udp=base.Echo(udp=True);self.addCleanup(udp.close);_,address=self.proxy_server(self.proxy_text(udp.address));cfg=self.settings(address).encode()
  for mode,target in((1,self.echo.address),(2,udp.address)):
   target=target.encode();self.assertEqual(self.proxy_boundary.gateway_proxy_dlm(cfg,len(cfg),target,len(target),mode),31)
  stats=Stats(*[2**63+i for i in range(9)]);self.assertEqual(C.sizeof(stats),72);self.assertEqual(bytes(self.proxy_boundary.gateway_proxy_abi(stats)),bytes(stats))
 def test_sdk_proxy_static_library_host(self):
  server=self.path/'proxy.server';client=self.path/'proxy.client';text=self.proxy_text('127.0.0.1:__ECHO_PORT__').replace('allow_target=tcp,127.0.0.0/8,'+self.echo.address.rsplit(':',1)[1]+'\n','');server.write_text(text);client.write_text(self.settings('__PROXY__'));r=subprocess.run([str(self.proxy_host),str(server),str(client)],capture_output=True,timeout=20);self.assertEqual(r.returncode,0,r.stderr.decode());self.assertEqual(json.loads(r.stdout)['status'],'PASS')
 def test_remote_acceptance_runner_preserves_local_scope_and_verifies_sha(self):
  _,address=self.start(self.server_text(kind='websocket',spool=True));cfg=self.path/'remote.cfg';cfg.write_text(self.client_text(address,'websocket'));out=self.path/'remote-results';r=subprocess.run([os.sys.executable,str(base.ROOT/'tools/dln_remote_acceptance.py'),'--binary',str(self.cli),'--config',str(cfg),'--output',str(out),'--bytes','65553'],capture_output=True,timeout=30);self.assertEqual(r.returncode,0,r.stderr.decode()+r.stdout.decode());v=json.loads((out/'report.json').read_text());self.assertEqual((v['status'],v['cycles'],v['bytes_verified']),('PASS',1,65553));self.assertEqual(v['network_scope'],'local');self.assertEqual(v['24_hour_soak'],'NOT_RUN');self.assertEqual(v['two_host_attestation'],'NOT_PROVIDED')
 def test_capabilities_do_not_claim_http3_or_native_proofs(self):
  out=C.create_string_buffer(4096);r=self.carriers(out,4096);self.assertEqual(r.status,0);v=json.loads(out.raw[:r.copied]);self.assertEqual(v['proxy']['http_versions'],[1]);self.assertFalse(v['proxy']['http3']);self.assertFalse(v['native_correctness_proved']);self.assertIn('websocket',v['direct']);small=C.create_string_buffer(b'\xa5',1);r=self.carriers(small,1);self.assertEqual((r.status,small.raw),(10,b'\xa5'))
 def test_native_client_interoperates_with_independent_masque_udp_proxy(self):
  udp=base.Echo(udp=True);self.addCleanup(udp.close);proxy=IndependentMasque(self,udp.address);self.addCleanup(proxy.close);t=self.tunnel(self.settings(proxy.address),2,udp.address);data=bytes(range(256))+b'\0\xff';self.assertEqual(self.proxy_write(t,data,len(data)).status,0);out=C.create_string_buffer(65535);r=self.proxy_read(t,65535,out,65535);self.assertEqual((r.status,out.raw[:r.copied]),(0,data));self.assertFalse(proxy.errors)
 def test_quic_identity_and_endpoint_through_independent_masque_proxy(self):
  _,address=self.start(self.server_text(kind='quic-datagram'));proxy=IndependentMasque(self,address);self.addCleanup(proxy.close);cfg=self.path/'independent.cfg';cfg.write_text(self.client_text(address,'quic-datagram')+self.settings(proxy.address));data=bytes(range(256))*33+b'\0\xff';(self.path/'input').write_bytes(data);self.command('exchange','--config',cfg,'--protocol','tcp','--target',self.echo.address,'--input',self.path/'input','--output',self.path/'output');self.assertEqual((self.path/'output').read_bytes(),data);self.assertFalse(proxy.errors)
 def test_proxy_reconnect_resources_remain_bounded(self):
  udp=base.Echo(udp=True);self.addCleanup(udp.close);handle,address=self.proxy_server(self.proxy_text(udp.address));cfg=self.settings(address).encode();target=udp.address.encode();baseline=None;peak=0
  for cycle in range(64):
   opened=self.proxy_open(cfg,len(cfg),2,target,len(target));self.assertEqual(opened.status,0)
   try:
    data=b'reconnect\0\xff';self.assertEqual(self.proxy_write(opened.handle,data,len(data)).status,0);out=C.create_string_buffer(64);r=self.proxy_read(opened.handle,65535,out,64);self.assertEqual((r.status,out.raw[:r.copied]),(0,data));peak=max(peak,self.proxy_server_statistics(handle).active)
   finally:self.proxy_close(opened.handle)
   deadline=time.monotonic()+2
   while self.proxy_server_statistics(handle).active and time.monotonic()<deadline:time.sleep(.002)
   self.assertEqual(self.proxy_server_statistics(handle).active,0)
   if os.sys.platform.startswith('linux'):
    metrics=(len(list(Path('/proc/self/fd').iterdir())),int(Path('/proc/self/statm').read_text().split()[1])*os.sysconf('SC_PAGE_SIZE'))
    if cycle==15:baseline=metrics
  if baseline:fd_delta=metrics[0]-baseline[0];rss_delta=metrics[1]-baseline[1];self.assertLessEqual(fd_delta,2);self.assertLessEqual(rss_delta,8*1024*1024)
  else:fd_delta=None;rss_delta=None
  print(json.dumps({'case':'proxy_reconnect_resource_bounds','status':'PASS','cycles':64,'peak_sessions':peak,'final_sessions':0,'fd_delta':fd_delta,'rss_delta_bytes':rss_delta,'24_hour_soak':'NOT_RUN'}),flush=True)
if __name__=='__main__':unittest.main()
