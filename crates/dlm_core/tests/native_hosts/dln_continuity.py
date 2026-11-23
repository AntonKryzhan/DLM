#!/usr/bin/env python3
import ctypes as C
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import socket
import ssl
import struct
import subprocess
import threading
import time
import unittest
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('continuity_gateway_base',HERE/'dln_gateway.py');base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
P,N,U,Q=base.P,base.N,base.U,base.Q
class Descriptor(C.Structure):_fields_=[('id',C.c_uint8*16),('stream',Q),('length',Q),('expires',Q),('digest',C.c_uint8*32)]
class DescriptorResult(C.Structure):_fields_=[('descriptor',Descriptor),('status',C.c_int32),('system',C.c_int32)]
class Receipt(C.Structure):_fields_=[('descriptor',Descriptor),('committed',Q),('state',U),('credit',U),('status',C.c_int32),('system',C.c_int32)]
class PoolStats(C.Structure):_fields_=[(n,Q)for n in('migrations','committed_bytes','retransmitted','fec_shards','probes','active','goodput_bps')]+[(n,U)for n in('ready_paths','configured_paths','state','declared_access_domains','independent_pairs','reserved')]
class Metric(C.Structure):_fields_=[(n,Q)for n in('id','rtt_us','jitter_us','handshake_us','goodput_bps','samples','failures','probes','bytes_committed')]+[(n,U)for n in('loss_ppm','profile','ready')]+[('last_error',C.c_int32)]
class FrameRelay:
 def __init__(self,case,backend):
  self.listener=socket.socket();self.listener.bind(('127.0.0.1',0));self.listener.listen(16);self.listener.settimeout(.05);self.address='127.0.0.1:%d'%self.listener.getsockname()[1];host,port=backend.rsplit(':',1);self.backend=(host,int(port));self.stop=threading.Event();self.drop_reply=threading.Event();self.dropped=threading.Event();self.workers=[];self.sockets=[]
  server=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);server.minimum_version=ssl.TLSVersion.TLSv1_3;server.load_cert_chain(case.fixtures/'quic-test-server.pem',case.fixtures/'quic-test-key.pem');client=ssl.create_default_context(cafile=str(case.fixtures/'quic-test-ca.pem'));client.minimum_version=ssl.TLSVersion.TLSv1_3
  def pump(source,target,reply):
   try:
    while not self.stop.is_set():
     header=base.exact(source,16);n=struct.unpack('>I',header[4:8])[0]
     if n>65500:return
     body=base.exact(source,n)
     if reply and body[:4]==struct.pack('>I',6) and self.drop_reply.is_set():
      self.drop_reply.clear();self.dropped.set()
      try:source.shutdown(socket.SHUT_RDWR);target.shutdown(socket.SHUT_RDWR)
      except OSError:pass
      return
     target.sendall(header+body)
   except (OSError,EOFError):pass
  def connection(raw):
   try:
    raw.settimeout(3);front=server.wrap_socket(raw,server_side=True);back=client.wrap_socket(socket.create_connection(self.backend,timeout=3),server_hostname='localhost');self.sockets.extend([front,back]);front.settimeout(3);back.settimeout(3);a=threading.Thread(target=pump,args=(front,back,False),daemon=True);b=threading.Thread(target=pump,args=(back,front,True),daemon=True);a.start();b.start();self.workers.extend([a,b])
   except OSError:raw.close()
  def accept():
   while not self.stop.is_set():
    try:
     raw,_=self.listener.accept();worker=threading.Thread(target=connection,args=(raw,),daemon=True);worker.start();self.workers.append(worker)
    except socket.timeout:pass
    except OSError:return
  self.worker=threading.Thread(target=accept,daemon=True);self.worker.start()
 def close(self):
  self.stop.set();self.listener.close()
  for s in self.sockets:
   try:s.shutdown(socket.SHUT_RDWR);s.close()
   except OSError:pass
   finally:s.close()
  self.worker.join(2)
  for worker in self.workers:worker.join(2)
class UdpRelay:
 def __init__(self,backend,seed):
  self.socket=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);self.socket.bind(('127.0.0.1',0));self.socket.settimeout(.005);self.address='127.0.0.1:%d'%self.socket.getsockname()[1];host,port=backend.rsplit(':',1);self.backend=(host,int(port));self.front=None;self.loss=0.;self.reorder=0.;self.delay=0.;self.stop=threading.Event();self.received=0;self.dropped=0;self.moved=0;self.random=random.Random(seed);self.pending=[]
  def run():
   while not self.stop.is_set():
    now=time.monotonic();ready=[v for v in self.pending if v[0]<=now];self.pending=[v for v in self.pending if v[0]>now]
    for _,data,peer in ready:
     try:self.socket.sendto(data,peer)
     except OSError:return
    try:
     data,peer=self.socket.recvfrom(65536);self.received+=1
     if peer==self.backend:target=self.front
     else:self.front=peer;target=self.backend
     if target is None:continue
     if self.random.random()<self.loss:self.dropped+=1;continue
     extra=.015 if self.random.random()<self.reorder else 0.
     if extra:self.moved+=1
     if self.delay or extra:self.pending.append((time.monotonic()+self.delay+extra,data,target))
     else:self.socket.sendto(data,target)
     if len(self.pending)>1024:raise RuntimeError('relay delay queue exceeded bound')
    except socket.timeout:pass
    except OSError:return
  self.worker=threading.Thread(target=run,daemon=True);self.worker.start()
 def close(self):self.stop.set();self.worker.join(2);self.socket.close()
class ContinuityTests(unittest.TestCase):
 command=base.GatewayTests.command;server_text=base.GatewayTests.server_text;client_text=base.GatewayTests.client_text;start=base.GatewayTests.start;start_process=base.GatewayTests.start_process;setUp=base.GatewayTests.setUp
 @classmethod
 def setUpClass(cls):
  base.GatewayTests.setUpClass.__func__(cls)
  signatures={'pool_open':(base.Opened,[P,N]),'pool_close':(None,[P]),'pool_refresh':(base.Io,[P]),'pool_statistics':(PoolStats,[P]),'pool_metrics':(base.Io,[P,C.POINTER(Metric),N]),'pool_disconnect':(base.Io,[P,N]),'pool_stream_open':(base.Io,[P,U,P,N]),'pool_stream_write':(base.Io,[P,U,P,N]),'pool_stream_read':(base.Transfer,[P,U,N,P,N]),'pool_stream_close':(base.Io,[P,U,C.c_bool]),'pool_take':(base.Transfer,[P,P,N]),'delivery_make':(DescriptorResult,[P,Q,Q,P,N]),'pool_upload':(Receipt,[P,Descriptor,P,N]),'pool_delivery_status':(Receipt,[P,Descriptor]),'pool_consume':(Receipt,[P,Descriptor]),'pool_download':(base.Transfer,[P,Descriptor,P,N]),'outbox_open':(base.Opened,[P,N]),'outbox_close':(None,[P]),'outbox_enqueue':(Receipt,[P,Descriptor,P,N]),'pool_flush':(base.Io,[P,P]),'server_listeners':(base.Transfer,[P,P,N])}
  for name,(result,args)in signatures.items():fn=getattr(cls.api,'dln_gateway_'+name);fn.restype=result;fn.argtypes=args;setattr(cls,name,staticmethod(fn))
  library=Path(cls.build.name)/('continuity.dll'if os.name=='nt'else'continuity.so');args=[str(cls.compiler),'build',str(HERE/'gateway_continuity_boundary.dlm'),'--lib',str(base.ROOT/'lib'),'--allow-unsafe','--target','sharedlib','--library','dlm_gateway','--library-dir',str(cls.profile),'-o',str(library)]
  if os.name!='nt':args+=['--cflag','-Wl,-rpath,'+str(cls.profile)]
  result=subprocess.run(args,capture_output=True);assert result.returncode==0,result.stderr.decode();cls.boundary=C.CDLL(str(library));cls.boundary.gateway_continuity_delivery.restype=Q;cls.boundary.gateway_continuity_delivery.argtypes=[P,N,Q,Q];cls.boundary.gateway_continuity_abi.restype=Descriptor;cls.boundary.gateway_continuity_abi.argtypes=[Descriptor]
  fixture=Path(cls.build.name)/'continuity.a';subprocess.run([str(cls.compiler),'build',str(HERE/'gateway_continuity_boundary.dlm'),'--lib',str(base.ROOT/'lib'),'--allow-unsafe','--target','staticlib','-o',str(fixture)],check=True,capture_output=True)
  cls.continuity_host=Path(cls.build.name)/('continuity-host.exe'if os.name=='nt'else'continuity-host');command=[os.environ.get('CC','gcc'),'-std=gnu11','-O1','-Wall','-Wextra','-Werror','-UNDEBUG','-DDLN_CONTINUITY_ACCEPTANCE',str(HERE/'dln_gateway_host.c'),str(fixture),'-L'+str(cls.profile),'-ldlm_gateway']
  command+=['-lws2_32','-lshell32','-lgdi32','-luser32']if os.name=='nt'else['-pthread','-ldl','-lm','-Wl,-rpath,'+str(cls.profile)];subprocess.run(command+['-o',str(cls.continuity_host)],check=True,capture_output=True)
 @classmethod
 def tearDownClass(cls):base.GatewayTests.tearDownClass.__func__(cls)
 def listeners(self,handle):
  output=C.create_string_buffer(2048);r=self.server_listeners(handle,output,2048);self.assertEqual(r.status,0);return[tuple(v.split(',',1))for v in output.raw[:r.copied].decode().splitlines()]
 def pool_text(self,paths,source='*',costs=None):
  text=f'identity={self.client_identity}\nhostname=localhost\nca={self.fixtures/"quic-test-ca.pem"}\npeer={self.server_root}\nmin_hold_ms=0\nprobe_ms=60000\npath_max_age_ms=60000\ntimeout_ms=3000\n'
  for i,(kind,address)in enumerate(paths):text+=f'path={kind},{address},{source},loopback,local,local,local,{costs[i]if costs else i*100}\n'
  return text
 def pool(self,text):
  value=self.pool_open(text.encode(),len(text.encode()));self.assertEqual(value.status,0)
  if not hasattr(self,'owned_pools'):self.owned_pools=set()
  self.owned_pools.add(value.handle)
  def cleanup():
   if value.handle in self.owned_pools:self.pool_close(value.handle);self.owned_pools.remove(value.handle)
  self.addCleanup(cleanup);return value.handle
 def make_descriptor(self,data,ident=None,stream=1,expiry=None):
  r=self.delivery_make(ident or os.urandom(16),stream,expiry or int(time.time())+3600,data,len(data));self.assertEqual(r.status,0);self.assertEqual(bytes(r.descriptor.digest),hashlib.sha256(data).digest());return r.descriptor
 def stream(self,pool):
  r=self.pool_stream_open(pool,1,self.echo.address.encode(),len(self.echo.address));self.assertEqual(r.status,0);return r.count
 def read_all(self,pool,stream,count):
  data=bytearray()
  while len(data)<count:
   output=C.create_string_buffer(640);r=self.pool_stream_read(pool,stream,min(640,count-len(data)),output,640);self.assertEqual(r.status,0);self.assertGreater(r.copied,0);data.extend(output.raw[:r.copied])
  return bytes(data)
 def test_actual_dlm_delivery_and_ffi_layout_on_all_carriers(self):
  self.assertEqual((C.sizeof(Descriptor),C.sizeof(Receipt),C.sizeof(Metric),C.sizeof(PoolStats)),(72,96,88,80));handle,_=self.start(self.server_text(spool=True)+'listen_alternate=quic-stream,127.0.0.1:0\nlisten_alternate=quic-datagram,127.0.0.1:0\n')
  for nonce,(kind,address)in enumerate(self.listeners(handle),1):
   with self.subTest(kind=kind):text=self.pool_text([(kind,address)]).encode();self.assertEqual(self.boundary.gateway_continuity_delivery(text,len(text),int(time.time())+3600,nonce),127)
  d=Descriptor((C.c_uint8*16)(*range(16)),2**63+7,2**63+9,2**63+11,(C.c_uint8*32)(*range(32)));self.assertEqual(bytes(self.boundary.gateway_continuity_abi(d)),bytes(d))
 def test_sdk_c_host_and_dlm_static_library_on_all_carriers(self):
  for kind in ('tls','quic-stream','quic-datagram'):
   with self.subTest(kind=kind):
    server=self.path/('sdk-'+kind+'.server');client=self.path/('sdk-'+kind+'.client');text=self.server_text(kind=kind,spool=True).replace('spool='+str(self.path/'inbox'),'spool='+str(self.path/('sdk-inbox-'+kind)));text=text.replace('allow_target=tcp,127.0.0.0/8,'+self.echo.address.rsplit(':',1)[1],'allow_target=tcp,127.0.0.0/8,__ECHO_PORT__');server.write_text(text,encoding='utf-8');client.write_text(self.client_text('__GATEWAY__',kind),encoding='utf-8');r=subprocess.run([str(self.continuity_host),str(server),str(client)],capture_output=True,timeout=20);self.assertEqual(r.returncode,0,r.stderr.decode());result=json.loads(r.stdout);self.assertEqual((result['status'],result['durable_bytes']),('PASS',3))
 def test_lost_write_response_does_not_forward_twice(self):
  handle,address=self.start(self.server_text());relay=FrameRelay(self,address);self.addCleanup(relay.close);pool=self.pool(self.pool_text([('tls',relay.address),('tls',address)],costs=[0,1000000]));stream=self.stream(pool);self.assertEqual(self.pool_statistics(pool).active,1);data=b'write-committed-before-reply\0\xff';relay.drop_reply.set();r=self.pool_stream_write(pool,stream,data,len(data));self.assertEqual(r.status,0);self.assertTrue(relay.dropped.wait(1));self.assertEqual(self.read_all(pool,stream,len(data)),data);self.assertEqual(self.echo.received,len(data));self.assertEqual(self.server_statistics(handle).forwarded,len(data));self.assertGreater(self.pool_statistics(pool).migrations,0)
 def test_lost_read_response_preserves_endpoint_bytes(self):
  handle,address=self.start(self.server_text());relay=FrameRelay(self,address);self.addCleanup(relay.close);pool=self.pool(self.pool_text([('tls',relay.address),('tls',address)],costs=[0,1000000]));stream=self.stream(pool);data=bytes(range(128));self.assertEqual(self.pool_stream_write(pool,stream,data,len(data)).status,0);relay.drop_reply.set();self.assertEqual(self.read_all(pool,stream,len(data)),data);self.assertTrue(relay.dropped.wait(1));self.assertEqual(self.server_statistics(handle).returned,len(data))
 def test_bound_source_and_shared_uplink_do_not_imply_independence(self):
  _,address=self.start(self.server_text());pool=self.pool(self.pool_text([('tls',address)],source='127.0.0.2:0'));self.assertEqual(self.pool_refresh(pool).status,0);metrics=(Metric*6)();r=self.pool_metrics(pool,metrics,6);self.assertEqual((r.status,r.count,metrics[0].ready),(0,1,1));self.assertGreater(metrics[0].samples,0);self.assertEqual(self.pool_statistics(pool).independent_pairs,0);guard=(Metric*1)();C.memset(C.addressof(guard),165,C.sizeof(guard));self.assertEqual(self.pool_metrics(pool,guard,0).status,10);self.assertEqual(bytes(guard),bytes([165])*C.sizeof(guard))
 def test_delivery_commit_survives_gateway_sigkill_and_duplicate_resume(self):
  text=self.server_text(spool=True);process,address=self.start_process(text);pool=self.pool(self.pool_text([('tls',address)]));data=os.urandom(131089);desc=self.make_descriptor(data);r=self.pool_upload(pool,desc,data,len(data));self.assertEqual((r.status,r.state,r.committed),(0,4,len(data)));process.kill();process.wait(5);_,again=self.start_process(text.replace('listen=127.0.0.1:0','listen='+address));self.assertEqual(again,address);other=self.pool(self.pool_text([('tls',address)]));r=self.pool_upload(other,desc,data,len(data));self.assertEqual((r.status,r.state),(0,4));self.assertEqual(self.pool_statistics(other).committed_bytes,0);output=C.create_string_buffer(len(data));r=self.pool_download(other,desc,output,len(data));self.assertEqual(r.status,0);self.assertEqual(output.raw[:r.copied],data)
 def test_cli_offline_queue_flush_and_verified_large_download(self):
  _,address=self.start(self.server_text(spool=True));key=self.path/'queue.key';self.command('journal-keygen','--output',key);queue=self.path/'queue.cfg';queue.write_text(f'directory={self.path/"queued"}\nkey={key}\npeer={self.server_root}\n');cfg=self.path/'pool.cfg';cfg.write_text(self.pool_text([('tls',address)]));data=os.urandom(1048593);source=self.path/'large.bin';source.write_bytes(data);desc=self.path/'large.dld';self.assertEqual(self.command('pool-queue','--config',queue,'--input',source,'--descriptor',desc,'--message-id','17'*16)['status'],'accepted_local');self.assertEqual(self.command('pool-flush','--config',cfg,'--outbox-config',queue)['messages'],1);self.assertEqual(self.command('pool-flush','--config',cfg,'--outbox-config',queue)['messages'],0);output=self.path/'large.out';self.command('pool-download','--config',cfg,'--descriptor',desc,'--output',output);self.assertEqual(hashlib.sha256(output.read_bytes()).digest(),hashlib.sha256(data).digest())
 def test_gateway_restart_does_not_pretend_to_restore_external_tcp(self):
  text=self.server_text(spool=True);process,address=self.start_process(text);pool=self.pool(self.pool_text([('tls',address)]));stream=self.stream(pool);data=b'before-restart';self.assertEqual(self.pool_stream_write(pool,stream,data,len(data)).status,0);self.assertEqual(self.read_all(pool,stream,len(data)),data);process.kill();process.wait(5);self.start_process(text.replace('listen=127.0.0.1:0','listen='+address));self.assertNotEqual(self.pool_stream_write(pool,stream,b'unknown',7).status,0);time.sleep(.5);self.assertEqual(self.pool_refresh(pool).status,0);self.assertEqual(self.pool_stream_write(pool,stream,b'unknown',7).status,19)
 def test_expired_and_corrupted_descriptors_are_not_acknowledged(self):
  _,address=self.start(self.server_text(spool=True));pool=self.pool(self.pool_text([('tls',address)]));data=b'authenticated';desc=self.make_descriptor(data);bad=Descriptor.from_buffer_copy(bytes(desc));bad.digest[0]^=1;self.assertEqual(self.pool_upload(pool,bad,data,len(data)).status,12);desc.expires=int(time.time())-1;self.assertEqual(self.pool_upload(pool,desc,data,len(data)).status,4);self.assertEqual(self.pool_statistics(pool).committed_bytes,0)
 def test_models_are_checked_and_runtime_proofs_remain_pending(self):
  r=self.command('continuity-model','--output',self.path/'model.json');self.assertEqual(r['status'],'PASS');self.assertEqual(r['false_committed_acks'],0);self.assertEqual(r['ownership_kernel']['states'],11);self.assertFalse(r['runtime_equivalence_proved']);self.assertGreater(r['delivery_states'],0)
 def test_real_quic_loss_reorder_delay_matrix(self):
  if not os.environ.get('DLM_LIBQUIC'):self.fail('pinned QUIC provider is required')
  handle,_=self.start(self.server_text(kind='quic-datagram',spool=True));backend=self.listeners(handle)[0][1]
  for i,(loss,reorder,delay)in enumerate(((0,0,.01),(.01,.05,.01),(.05,.2,.01),(.1,.2,.01),(.3,.2,.01))):
   with self.subTest(loss=loss,reorder=reorder):
    relay=UdpRelay(backend,14800+i);self.addCleanup(relay.close);pool=self.pool(self.pool_text([('quic-datagram',relay.address)])+'fec_threshold_ppm=0\n');self.assertEqual(self.pool_refresh(pool).status,0);relay.loss=loss;relay.reorder=reorder;relay.delay=delay;data=bytes((n*29+i)%256 for n in range(32771));desc=self.make_descriptor(data);r=self.pool_upload(pool,desc,data,len(data));self.assertEqual((r.status,r.state),(0,4),f'loss={loss} dropped={relay.dropped} status={r.status}');self.assertGreater(relay.received,0);stats=self.pool_statistics(pool);self.assertEqual(stats.independent_pairs,0);relay.loss=relay.reorder=relay.delay=0;output=C.create_string_buffer(len(data));r=self.pool_download(pool,desc,output,len(data));self.assertEqual(r.status,0);self.assertEqual(output.raw[:r.copied],data);print(json.dumps({'case':'quic_packet_impairment','status':'PASS','seed':14800+i,'loss':loss,'reorder':reorder,'one_way_delay_ms':int(delay*1000),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'network_packets':relay.received,'dropped_packets':relay.dropped,'reordered_packets':relay.moved,'fec_shards':stats.fec_shards,'retransmitted':stats.retransmitted}),flush=True);self.pool_close(pool);self.owned_pools.remove(pool)
 def test_corrupt_transfer_journal_prevents_ready_after_restart(self):
  text=self.server_text(spool=True);process,address=self.start_process(text);pool=self.pool(self.pool_text([('tls',address)]));data=b'server commit';desc=self.make_descriptor(data);self.assertEqual(self.pool_upload(pool,desc,data,len(data)).status,0);process.kill();process.wait(5);files=list((self.path/'inbox/delivery-v2').glob('*.dgw'));self.assertTrue(files);content=bytearray(files[0].read_bytes());content[-1]^=1;files[0].write_bytes(content);cfg=self.path/'bad.cfg';cfg.write_text(text.replace('listen=127.0.0.1:0','listen='+address));r=subprocess.run([str(self.cli),'serve','--config',str(cfg),'--duration','1'],capture_output=True,timeout=5);self.assertNotEqual(r.returncode,0);self.assertNotIn(b'READY',r.stdout)
if __name__=='__main__':unittest.main()
