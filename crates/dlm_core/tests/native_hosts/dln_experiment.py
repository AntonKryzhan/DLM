#!/usr/bin/env python3
import ctypes as C
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import threading
import time
import unittest

ROOT=Path(__file__).resolve().parents[4]
HERE=Path(__file__).resolve().parent
MODES=('absolute','interval','presence','count','protocol','order','size','hybrid')
P,N,U,Q=C.c_void_p,C.c_size_t,C.c_uint32,C.c_uint64
class Opened(C.Structure):_fields_=[('handle',P),('status',C.c_int32),('reserved',C.c_int32)]
class Io(C.Structure):_fields_=[('count',Q),('status',C.c_int32),('reserved',C.c_int32)]
class Transfer(C.Structure):_fields_=[('required',Q),('copied',Q),('status',C.c_int32),('delivery',U)]
class Summary(C.Structure):_fields_=[('delivery',U),('mode',U)]+[(n,Q)for n in('symbols','events','payload_bytes','decoded_bytes','corrected_words','duration_us')]
class Event(C.Structure):_fields_=[('time_us',Q),('size',U),('kind',U)]
def crc32c(data):
 crc=0xffffffff
 for b in data:
  crc^=b
  for _ in range(8):crc=(crc>>1)^(0x82f63b78 if crc&1 else 0)
 return crc^0xffffffff

def independent_decode(wire):
 assert wire[:8]==b'DLCT0001'and len(wire)>=76
 mode,fec,depth,small,large,slot,tolerance,search,frame_id,length,count,flags,checksum=struct.unpack('>5I4Q4I',wire[8:76]);assert flags==0 and checksum==crc32c(wire[:72]);assert len(wire)==76+16*count
 events=[]
 for offset in range(76,len(wire),16):
  t,size,kind=struct.unpack('>QIB',wire[offset:offset+13]);assert wire[offset+13:offset+16]==bytes(3);events.append((t,size,kind))
 scale=(1,3,2)[fec];coded=((length+28)*8*scale+depth*8-1)//(depth*8)*depth*8;width=3 if mode==8 else 1;slots=64+(coded+width-1)//width;groups=[[]for _ in range(slots)]
 for e in events:
  if e[0]//slot<slots:groups[e[0]//slot].append(e)
 symbols=[]
 for i,g in enumerate(groups):
  g.sort();phase=[e[0]-i*slot for e in g]
  if mode==1:assert len(g)==1;value=0 if phase[0]<slot/2 else 1
  elif mode==2:assert len(g)==2;value=0 if g[1][0]-g[0][0]<slot*3/8 else 1
  elif mode==3:assert len(g)in(1,2);value=len(g)-1
  elif mode==4:assert len(g)in(2,4);value=0 if len(g)==2 else 1
  elif mode==5:assert len(g)==1;value=g[0][2]
  elif mode==6:assert len(g)==2 and g[0][2]!=g[1][2];value=g[0][2]
  elif mode==7:assert len(g)==1 and g[0][1]in(small,large);value=int(g[0][1]==large)
  else:assert len(g)==1;value=(4 if phase[0]>slot/2 else 0)|(2 if g[0][1]==large else 0)|g[0][2]
  symbols.append(value)
 alphabet=1<<width;assert symbols[:64]==[(i*5+i//8)%alphabet for i in range(64)]
 bits=[(v>>i)&1 for v in symbols[64:]for i in range(width-1,-1,-1)][:coded];ordered=[]
 for start in range(0,len(bits),depth*8):
  ordered.extend(bits[start+column*depth+row]for row in range(depth)for column in range(8))
 ordered=ordered[:(length+28)*8*scale]
 if fec==1:
  ordered=[int(sum(ordered[i:i+3])>=2)for i in range(0,len(ordered),3)]
 elif fec==2:
  recovered=[]
  for i in range(0,len(ordered),8):
   b=[0]+ordered[i:i+8];assert not(sum(b[1:])%2);assert all(not(sum(b[j]for j in range(1,8)if j&p)%2)for p in(1,2,4));recovered.extend(b[j]for j in(3,5,6,7))
  ordered=recovered
 frame=bytes(sum(ordered[i+j]<<(7-j)for j in range(8))for i in range(0,len(ordered),8));assert frame[:8]==b'DLX1'+bytes([0,1,mode,fec]);assert frame[8:16]==frame_id.to_bytes(8,'big')and frame[16:20]==length.to_bytes(4,'big');fingerprint=b''.join(v.to_bytes(8,'big')for v in(mode,fec,depth,slot,tolerance,search,small,large));assert frame[20:24]==crc32c(fingerprint).to_bytes(4,'big');assert crc32c(frame[:-4])==int.from_bytes(frame[-4:],'big');return frame[24:-4]

class ExperimentTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.compiler=Path(os.environ.get('DLM_NETWORK_DLM',ROOT/'target/debug/dlm')).resolve();cls.profile=cls.compiler.parent;cls.cli=cls.profile/('dlm-channel-lab.exe'if os.name=='nt'else'dlm-channel-lab');cls.library=cls.profile/('dlm_channel_lab.dll'if os.name=='nt'else'libdlm_channel_lab.so');cls.api=C.CDLL(str(cls.library));cls.build=tempfile.TemporaryDirectory(prefix='dln-lab-build-');build=Path(cls.build.name)
  signatures={'abi':(U,[]),'open':(Opened,[P,N]),'simulate':(Io,[P,P,N]),'loopback':(Io,[P,P,N]),'decode':(Opened,[P,N]),'read':(Transfer,[P,U,P,N]),'summary':(Summary,[P]),'event':(Io,[P,N,C.POINTER(Event)]),'close':(None,[P]),'cancellation':(Opened,[P]),'cancel':(Io,[P]),'cancel_close':(None,[P])}
  for name,(restype,args)in signatures.items():f=getattr(cls.api,'dlm_channel_lab_'+name);f.restype=restype;f.argtypes=args;setattr(cls,name,staticmethod(f))
  fixture=build/'lab.a';r=subprocess.run([str(cls.compiler),'build',str(HERE/'channel_lab_boundary.dlm'),'--lib',str(ROOT/'lib'),'--allow-unsafe','--target','staticlib','-o',str(fixture)],capture_output=True);assert r.returncode==0,r.stderr.decode();cls.host=build/('lab-host.exe'if os.name=='nt'else'lab-host');command=[os.environ.get('CC','gcc'),'-std=c11','-O1','-Wall','-Wextra','-Werror','-UNDEBUG',str(HERE/'dln_experiment_host.c'),str(fixture),'-L'+str(cls.profile),'-ldlm_channel_lab'];command+=['-lws2_32','-lshell32','-lgdi32','-luser32']if os.name=='nt'else['-pthread','-ldl','-lm','-Wl,-rpath,'+str(cls.profile)];r=subprocess.run(command+['-o',str(cls.host)],capture_output=True);assert r.returncode==0,r.stderr.decode()
 @classmethod
 def tearDownClass(cls):cls.build.cleanup()
 def setUp(self):self.temp=tempfile.TemporaryDirectory(prefix='dln-lab-test-');self.addCleanup(self.temp.cleanup);self.path=Path(self.temp.name)
 def command(self,*args,success=True,timeout=30):
  p=subprocess.run([str(self.cli),*map(str,args)],capture_output=True,timeout=timeout)
  if success:self.assertEqual(p.returncode,0,p.stderr.decode());return json.loads(p.stdout)
  self.assertNotEqual(p.returncode,0);return p
 def files(self,text='',data=b'A\0\xff'):
  cfg=self.path/'config';source=self.path/'input';cfg.write_text(text);source.write_bytes(data);return cfg,source
 def context(self,text=''):
  b=text.encode();r=self.open(b,len(b));self.assertEqual(r.status,0);self.addCleanup(self.close,r.handle);return r.handle
 def result(self,handle,item):
  first=self.read(handle,item,None,0);self.assertIn(first.status,(0,10));out=C.create_string_buffer(max(1,first.required));r=self.read(handle,item,out,first.required);self.assertEqual((r.status,r.copied),(0,first.required));return out.raw[:r.copied]
 def test_sdk_native_c_and_dlm_actual_owned_ffi_and_layout(self):
  r=subprocess.run([str(self.host)],capture_output=True,timeout=30);self.assertEqual(r.returncode,0,r.stderr.decode());self.assertEqual(json.loads(r.stdout)['status'],'PASS');self.assertEqual((C.sizeof(Opened),C.sizeof(Summary),C.sizeof(Event),C.sizeof(Transfer)),(16,56,16,24))
 def test_independent_python_decodes_every_alphabet_and_error_code(self):
  data=bytes(range(256))+b'\0\xff'
  for mode in MODES:
   for fec in('none','repeat3','hamming84'):
    with self.subTest(mode=mode,fec=fec):cfg,source=self.files(f'mode={mode}\nfec={fec}\n',data);capture=self.path/(mode+fec+'.trace');self.command('encode','--config',cfg,'--input',source,'--capture',capture);self.assertEqual(independent_decode(capture.read_bytes()),data)
 def test_standalone_decoder_has_capture_only_and_retains_exact_binary(self):
  cfg,source=self.files('mode=hybrid\n',os.urandom(527));capture=self.path/'capture';output=self.path/'decoded';self.command('encode','--config',cfg,'--input',source,'--capture',capture);r=self.command('decode','--capture',capture,'--output',output);self.assertFalse(r['original_payload_available_to_decoder']);self.assertEqual(hashlib.sha256(output.read_bytes()).digest(),hashlib.sha256(source.read_bytes()).digest())
 def test_simulation_report_is_reproducible_with_full_noise_model(self):
  text='mode=interval\nseed=123456\nloss_ppm=10000\njitter_us=250\nreorder_ppm=1000\nreorder_delay_us=12000\n';cfg,source=self.files(text,bytes(range(64)));a=self.path/'a.trace';b=self.path/'b.trace';x=self.command('simulate','--config',cfg,'--input',source,'--capture',a);y=self.command('simulate','--config',cfg,'--input',source,'--capture',b);self.assertEqual(x,y);self.assertEqual(a.read_bytes(),b.read_bytes());self.assertEqual(x['noise_model']['jitter_us'],250);self.assertEqual(x['noise_model']['seed'],123456);self.assertFalse(x['production_carrier'])
 def test_full_matrix_measures_all_models_without_claiming_closed_paths_work(self):
  cfg,source=self.files('',bytes(range(16)));r=self.command('matrix','--config',cfg,'--input',source,timeout=60);self.assertEqual(len(r['cases']),96);clean=0
  for case in r['cases']:
   m=case['measurement'];self.assertEqual(m['status'],'MEASURED');self.assertTrue(math.isfinite(m['useful_bits_per_second']));self.assertFalse(m['public_internet_tested'])
   if case['case']=='clean':clean+=1;self.assertTrue(m['payload_exact_match'])
   if case['case']=='closed_channel':self.assertFalse(m['payload_exact_match']);self.assertEqual(m['statistics']['dmc_fit_capacity_lower_bits_per_symbol'],0);self.assertEqual(m['useful_bits_per_second'],0)
   if case['case']=='observable_normalization':self.assertFalse(m['payload_exact_match'])
   if case['case']=='gilbert_elliott':self.assertEqual(m['statistics']['confidence_method'],'moving_block_bootstrap_95')
  self.assertEqual(clean,8)
 def test_hybrid_reports_residual_capacity_after_one_observable_is_erased(self):
  cfg,source=self.files('mode=hybrid\nnormalize_size=64\n');r=self.command('simulate','--config',cfg,'--input',source);self.assertFalse(r['payload_exact_match']);self.assertAlmostEqual(r['statistics']['dmc_fit_capacity_lower_bits_per_symbol'],2);self.assertEqual(r['statistics']['confidence_method'],'moving_block_bootstrap_95')
 def test_closed_channel_decoding_fails_and_does_not_create_output(self):
  cfg,source=self.files('blocked=true\n',bytes(3));capture=self.path/'capture';r=self.command('simulate','--config',cfg,'--input',source,'--capture',capture);self.assertEqual(r['delivery'],'SYNC_LOST');out=self.path/'output';self.command('decode','--capture',capture,'--output',out,success=False);self.assertFalse(out.exists())
 def test_physical_loopback_uses_real_tcp_udp_and_capture_replays(self):
  cfg,source=self.files('mode=protocol\nslot_us=4000\n');capture=self.path/'loop.trace';r=self.command('loopback','--config',cfg,'--input',source,'--capture',capture,'--output',self.path/'direct',timeout=12);self.assertEqual(r['scope'],'physical_loopback_TCP_UDP_application_events');self.assertTrue(r['payload_exact_match']);self.assertEqual((self.path/'direct').read_bytes(),source.read_bytes());self.command('decode','--capture',capture,'--output',self.path/'replayed');self.assertEqual((self.path/'replayed').read_bytes(),source.read_bytes());wire=capture.read_bytes();kinds={wire[i+12]for i in range(76,len(wire),16)};self.assertEqual(kinds,{0,1})
 def test_c_api_retains_short_buffers_and_rejects_null_before_consumption(self):
  h=self.context('mode=size\n');self.assertEqual(self.simulate(h,b'A\0\xff',3).status,0);self.assertEqual(self.read(h,2,None,1).status,4);guard=C.create_string_buffer(b'\xa5',1);short=self.read(h,2,guard,1);self.assertEqual((short.status,short.required,short.copied,guard.raw),(10,3,0,b'\xa5'));self.assertEqual(self.result(h,2),b'A\0\xff');self.assertEqual(self.result(h,2),b'A\0\xff')
 def test_c_api_decode_reads_no_original_data_and_null_empty_is_distinct(self):
  h=self.context();self.assertEqual(self.simulate(h,None,0).status,0);self.assertEqual(self.read(h,2,None,0).status,0);self.assertEqual(self.summary(h).delivery,1);wire=self.result(h,1);r=self.decode(wire,len(wire));self.assertEqual(r.status,0);self.addCleanup(self.close,r.handle);self.assertEqual(self.result(r.handle,2),b'');self.assertEqual(self.simulate(h,None,1).status,4);self.assertEqual(self.read(h,2,None,0).status,19)
 def test_cancellation_token_interrupts_pure_work_and_survives_context_close(self):
  h=self.context('mode=hybrid\n');t=self.cancellation(h);self.assertEqual(t.status,0);self.addCleanup(self.cancel_close,t.handle);result=[];entered=threading.Event();data=bytes(20000)
  def work():entered.set();result.append(self.simulate(h,data,len(data)))
  worker=threading.Thread(target=work);worker.start();self.assertTrue(entered.wait(1));time.sleep(.01);self.assertEqual(self.cancel(t.handle).status,0);worker.join(3);self.assertFalse(worker.is_alive());self.assertEqual(result[0].status,9)
  b=b'';separate=self.open(b,0);token=self.cancellation(separate.handle);self.close(separate.handle);self.assertEqual(self.cancel(token.handle).status,0);self.cancel_close(token.handle)
 def test_parser_fuzz_and_exact_capture_bounds_never_unwind_across_c(self):
  h=self.context('mode=size\nfec=none\n');self.assertEqual(self.simulate(h,b'test',4).status,0);wire=self.result(h,1)
  for i in range(400):
   bad=bytearray(wire);index=(i*37)%len(bad);bad[index]^=(i%255)+1;r=self.decode(bytes(bad),len(bad))
   self.assertNotEqual(r.status,99)
   if r.status==0:self.close(r.handle)
   else:self.assertFalse(r.handle)
  for n in(0,1,75,len(wire)-1):self.assertNotEqual(self.decode(wire,n).status,0)
  self.assertEqual(self.decode(None,17).status,4);self.assertEqual(self.decode(None,76+16*1048576+1).status,4)
 def test_invalid_cli_configuration_and_unused_options_are_rejected(self):
  for text in('mode=bogus','loss_ppm=1000001','mode=size\nmode=order','normalize_count=5','slot_us=0'):
   cfg,source=self.files(text);self.command('simulate','--config',cfg,'--input',source,success=False)
  cfg,source=self.files();self.command('encode','--config',cfg,'--input',source,'--capture',self.path/'capture','--unknown','1',success=False)
 def test_repeated_owned_contexts_return_to_bounded_resources(self):
  baseline=None
  for cycle in range(64):
   b=b'mode=hybrid\n';h=self.open(b,len(b));self.assertEqual(h.status,0)
   try:self.assertEqual(self.simulate(h.handle,b'\0\xff',2).status,0);self.assertEqual(self.result(h.handle,2),b'\0\xff')
   finally:self.close(h.handle)
   if os.sys.platform.startswith('linux'):
    metrics=(len(list(Path('/proc/self/fd').iterdir())),int(Path('/proc/self/statm').read_text().split()[1])*os.sysconf('SC_PAGE_SIZE'))
    if cycle==15:baseline=metrics
  if baseline:self.assertLessEqual(metrics[0]-baseline[0],1);self.assertLessEqual(metrics[1]-baseline[1],8*1024*1024)
  print(json.dumps({'case':'experimental_resource_bounds','status':'PASS','cycles':64,'fd_delta':None if baseline is None else metrics[0]-baseline[0],'rss_delta_bytes':None if baseline is None else metrics[1]-baseline[1],'long_duration_soak':'DEFERRED_BY_USER'}),flush=True)

if __name__=='__main__':unittest.main(verbosity=2)
