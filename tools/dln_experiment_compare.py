#!/usr/bin/env python3
import argparse
import ctypes as C
import datetime
import hashlib
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import tempfile
import threading
import time

ROOT=Path(__file__).resolve().parents[1]
P,N,U,Q=C.c_void_p,C.c_size_t,C.c_uint32,C.c_uint64
class Opened(C.Structure):_fields_=[('handle',P),('status',C.c_int32),('system',C.c_int32)]
class Io(C.Structure):_fields_=[('count',N),('status',C.c_int32),('system',C.c_int32)]
class Transfer(C.Structure):_fields_=[('required',N),('copied',N),('value',Q),('outcome',U),('status',C.c_int32),('system',C.c_int32)]
class ServerStatistics(C.Structure):_fields_=[(n,Q)for n in('accepted','authenticated','denied','failed','requests','forwarded','returned','active','handshakes')]
class Echo:
 def __init__(self):
  self.listener=socket.socket();self.listener.bind(('127.0.0.1',0));self.listener.listen(8);self.listener.settimeout(.05);self.address='%s:%d'%self.listener.getsockname();self.stop=threading.Event();self.workers=[]
  def client(peer):
   with peer:
    peer.settimeout(.05)
    while not self.stop.is_set():
     try:
      data=peer.recv(65536)
      if not data:return
      peer.sendall(data)
     except socket.timeout:continue
     except OSError:return
  def accept():
   while not self.stop.is_set():
    try:peer,_=self.listener.accept();t=threading.Thread(target=client,args=(peer,),daemon=True);t.start();self.workers.append(t)
    except socket.timeout:pass
    except OSError:return
  self.worker=threading.Thread(target=accept,daemon=True);self.worker.start()
 def close(self):
  self.stop.set();self.listener.close();self.worker.join(2)
  for worker in self.workers:worker.join(2)
class CounterRelay:
 def __init__(self,target,udp):
  h,p=target.rsplit(':',1);self.backend=(h,int(p));self.udp=udp;self.socket=socket.socket(socket.AF_INET,socket.SOCK_DGRAM if udp else socket.SOCK_STREAM);self.socket.bind(('127.0.0.1',0));self.address='%s:%d'%self.socket.getsockname();self.stop=threading.Event();self.incoming=0;self.outgoing=0;self.errors=[];self.connections=[]
  if udp:
   self.socket.settimeout(.01)
   def run():
    front=None
    while not self.stop.is_set():
     try:
      data,peer=self.socket.recvfrom(65536)
      if peer==self.backend:
       if front is not None:self.outgoing+=len(data);self.socket.sendto(data,front)
      elif front is None or peer==front:front=peer;self.incoming+=len(data);self.socket.sendto(data,self.backend)
     except socket.timeout:pass
     except OSError:return
  else:
   self.socket.listen(1);self.socket.settimeout(.05)
   def run():
    front=None
    while front is None and not self.stop.is_set():
     try:front,_=self.socket.accept()
     except socket.timeout:pass
     except OSError:return
    if front is None:return
    try:
     back=socket.create_connection(self.backend,timeout=2);self.connections=[front,back]
     for s in self.connections:s.setblocking(False)
     pending={front:bytearray(),back:bytearray()};readable={front,back};half=set()
     while not self.stop.is_set():
      reads,writes,_=select.select(list(readable),[s for s in self.connections if pending[s]],[],.01)
      for s in reads:
       other=back if s is front else front
       try:data=s.recv(65536)
       except BlockingIOError:continue
       if data:
        if s is front:self.incoming+=len(data)
        else:self.outgoing+=len(data)
        pending[other].extend(data)
        if len(pending[other])>1048576:raise RuntimeError('counter relay queue quota')
       else:readable.discard(s);half.add(other)
      for s in writes:
       try:n=s.send(pending[s]);del pending[s][:n]
       except BlockingIOError:pass
      for s in tuple(half):
       if not pending[s]:
        try:s.shutdown(socket.SHUT_WR)
        except OSError:pass
        half.remove(s)
      if not readable and not any(pending.values()):return
    except OSError:
     if not self.stop.is_set():self.errors.append('relay I/O failed')
    except Exception as e:self.errors.append(str(e))
    finally:
     for s in self.connections:s.close()
  self.worker=threading.Thread(target=run,daemon=True);self.worker.start()
 def close(self):self.stop.set();self.socket.close();self.worker.join(3)
def checked(r):
 if r.status:raise RuntimeError('native baseline status %d/%d'%(r.status,r.system))
 return r

def compare(binary_dir,quic,output,payload):
 output.mkdir(parents=True,exist_ok=True)
 if not 1<=len(payload)<=16:raise ValueError('short comparison payload must be 1..16 bytes')
 os.environ['DLM_LIBQUIC']=str(quic.resolve());cli=binary_dir/('dlm-net.exe'if os.name=='nt'else'dlm-net');lab=binary_dir/('dlm-channel-lab.exe'if os.name=='nt'else'dlm-channel-lab');library=binary_dir/('dlm_gateway.dll'if os.name=='nt'else'libdlm_gateway.so');api=C.CDLL(str(library))
 signatures={'server_open':(Opened,[P,N]),'server_close':(None,[P]),'server_address':(Io,[P,P,N]),'server_statistics':(ServerStatistics,[P]),'client_open':(Opened,[P,N]),'client_close':(None,[P]),'stream_open':(Io,[P,U,P,N]),'stream_write':(Io,[P,U,P,N]),'stream_read':(Transfer,[P,U,N,P,N])}
 for name,(restype,args)in signatures.items():f=getattr(api,'dln_gateway_'+name);f.restype=restype;f.argtypes=args;setattr(api,name,f)
 def command(binary,*args):
  p=subprocess.run([str(binary),*map(str,args)],capture_output=True,timeout=15)
  if p.returncode:raise RuntimeError(p.stderr.decode())
  return json.loads(p.stdout)
 data_sha=hashlib.sha256(payload).hexdigest();baselines=[];experiments=[];echo=Echo()
 try:
  with tempfile.TemporaryDirectory(prefix='dln-lab-comparison-')as temp:
   temp=Path(temp);server_id=temp/'server.id';client_id=temp/'client.id';server_root=command(cli,'keygen','--identity',server_id)['root'];client_root=command(cli,'keygen','--identity',client_id)['root'];fixtures=ROOT/'examples/native/fixtures'
   for carrier in('tls','quic-stream','quic-datagram'):
    server_text=f'listen=127.0.0.1:0\ntransport={carrier}\nidentity={server_id}\ncertificate={fixtures/"quic-test-server.pem"}\nkey={fixtures/"quic-test-key.pem"}\nallow_client={client_root}\nallow_private=true\nallow_target=tcp,127.0.0.0/8,{echo.address.rsplit(":",1)[1]}\n'.encode();server=checked(api.server_open(server_text,len(server_text)));relay=None;client=None
    try:
     address=C.create_string_buffer(128);bound=checked(api.server_address(server.handle,address,128));backend=address.raw[:bound.count].decode();relay=CounterRelay(backend,carrier!='tls');text=f'gateway={relay.address}\nhostname=localhost\nca={fixtures/"quic-test-ca.pem"}\ntransport={carrier}\nidentity={client_id}\npeer={server_root}\ntimeout_ms=3000\n'.encode();started=time.perf_counter_ns();client=checked(api.client_open(text,len(text)));handshake_us=(time.perf_counter_ns()-started)//1000;transfer_started=time.perf_counter_ns();target=echo.address.encode();stream=checked(api.stream_open(client.handle,1,target,len(target)));sent=checked(api.stream_write(client.handle,stream.count,payload,len(payload)));assert sent.count==len(payload);received=bytearray()
     while len(received)<len(payload):
      block=C.create_string_buffer(len(payload));r=api.stream_read(client.handle,stream.count,len(payload)-len(received),block,len(payload));assert r.status==0 and r.copied>0;received.extend(block.raw[:r.copied])
     transfer_us=max(1,(time.perf_counter_ns()-transfer_started)//1000);assert bytes(received)==payload;stats=api.server_statistics(server.handle);assert stats.authenticated==1 and stats.forwarded==stats.returned==len(payload);api.client_close(client.handle);client=None;time.sleep(.03);relay.close();assert not relay.errors,relay.errors;elapsed_us=handshake_us+transfer_us;baselines.append({'carrier':carrier,'status':'PASS','payload_sha256':data_sha,'payload_exact_match':True,'payload_bytes':len(payload),'samples':1,'clock_basis':'measured monotonic wall clock; handshake separately measured','handshake_us':handshake_us,'open_write_echo_read_us':transfer_us,'total_us':elapsed_us,'delivered_payload_bits_per_second_including_handshake':len(payload)*8*1000000/max(1,elapsed_us),'encrypted_forward_transport_payload_bytes':relay.incoming,'encrypted_reverse_transport_payload_bytes':relay.outgoing,'cost_observation_tail_us':30000,'cost_scope':'TCP stream bytes or QUIC UDP payload bytes at local forwarding relay; IP and link headers excluded'})
    finally:
     if client is not None:api.client_close(client.handle)
     if relay is not None:relay.close()
     api.server_close(server.handle)
   source=temp/'input';source.write_bytes(payload)
   for mode in('absolute','interval','presence','count','protocol','order','size','hybrid'):
    config=temp/'event.cfg';config.write_text(f'mode={mode}\nslot_us=4000\n');capture=output/(mode+'.dlct');planned=temp/'planned.dlct';command(lab,'encode','--config',config,'--input',source,'--capture',planned);wire=planned.read_bytes();sent_cost=sum(int.from_bytes(wire[i+8:i+12],'big')+(2 if wire[i+12]else 0)for i in range(76,len(wire),16));m=command(lab,'loopback','--config',config,'--input',source,'--capture',capture);experiments.append({'carrier':'experimental-'+mode,'status':'MEASURED','payload_sha256':data_sha,'payload_exact_match':m['payload_exact_match'],'payload_bytes':len(payload),'samples':1,'clock_basis':'measured monotonic wall clock of physical receiver; includes training and capture drain','total_us':m['observation_window_us'],'delivered_payload_bits_per_second':m['useful_bits_per_second'],'forward_transport_payload_bytes':sent_cost,'reverse_transport_payload_bytes':0,'cost_scope':'constant UDP bodies plus TCP event-length framing; IP and link headers excluded','acknowledgement':'none; receiver capture checked locally','measurement':m})
 finally:echo.close()
 report={'format':'DLM-EXPERIMENT-TRANSPORT-COMPARISON-v1','status':'MEASURED','execution_status':'PASS','scope':'short clean physical loopback measurements; no public Internet or long-duration acceptance','finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'input_sha256':data_sha,'input_bytes':len(payload),'same_binary_payload_for_all_carriers':True,'baseline_profiles':baselines,'experimental_profiles':experiments,'experimental_delivery_samples':sum(m['payload_exact_match']for m in experiments),'experimental_delivery_trials':len(experiments),'confidence_for_frame_delivery_rate':'not estimated from one frame per profile','security_equivalence_claimed':False,'experiment_integrity':'CRC32C only; TLS/QUIC baselines authenticate and encrypt','gateway_library_sha256':hashlib.sha256(library.read_bytes()).hexdigest(),'lab_cli_sha256':hashlib.sha256(lab.read_bytes()).hexdigest(),'quic_sha256':hashlib.sha256(quic.read_bytes()).hexdigest()};(output/'report.json').write_text(json.dumps(report,indent=2)+'\n');return report

def main():
 p=argparse.ArgumentParser();p.add_argument('--binary-dir',type=Path,default=ROOT/'target/debug');p.add_argument('--quic',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--input',type=Path);a=p.parse_args();payload=b'A\0\xff'
 if a.input:
  with a.input.open('rb')as f:payload=f.read(17)
 report=compare(a.binary_dir.resolve(),a.quic.resolve(),a.output.resolve(),payload);print(json.dumps({'status':report['status'],'baseline_profiles':len(report['baseline_profiles']),'experimental_delivery_samples':report['experimental_delivery_samples'],'experimental_delivery_trials':report['experimental_delivery_trials'],'report':str(a.output/'report.json')}));return 0
if __name__=='__main__':raise SystemExit(main())
