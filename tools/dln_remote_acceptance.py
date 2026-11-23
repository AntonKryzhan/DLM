#!/usr/bin/env python3
import argparse
from collections import deque
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile
import time

def digest(path):
 h=hashlib.sha256()
 with path.open('rb')as stream:
  for b in iter(lambda:stream.read(1048576),b''):h.update(b)
 return h.hexdigest()
def run(args):
 binary=args.binary.resolve();source=args.config.resolve();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
 if not binary.is_file():raise ValueError('dlm-net executable missing')
 text=source.read_text(encoding='utf-8-sig');settings={}
 for line in text.splitlines():
  if line.strip()and not line.lstrip().startswith('#'):k,v=line.split('=',1);settings.setdefault(k.strip(),[]).append(v.strip())
 def one(name):return settings.get(name,[''])[0]
 addresses=[v.split(',')[1]for v in settings.get('path',[])]or[one('gateway')]
 if one('proxy_address'):addresses.append(one('proxy_address'))
 public=[]
 for address in addresses:
  try:public.append(ipaddress.ip_address(address.rsplit(':',1)[0].strip('[]')).is_global)
  except ValueError:public.append(False)
 if args.scope=='public-internet'and not any(public):raise ValueError('public scope requires a configured public gateway or proxy address')
 version=subprocess.run([str(binary),'--version'],capture_output=True,text=True,timeout=10,check=True).stdout.strip();started=time.monotonic();seed=os.urandom(32);cycles=0;received=0;chain=bytes(32);sample=deque(maxlen=32);faults=deque(maxlen=16);cleanup=0;status='RUNNING';fatal=None;complete=False
 report={'format':'DLM-DLN-REMOTE-v1','version':version,'platform':platform.platform(),'binary_sha256':digest(binary),'config_sha256':digest(source),'network_scope':args.scope,'scope_basis':'operator declaration and configured address classification; physical routing and independent uplinks are not proven','two_host_attestation':'NOT_PROVIDED','native_privileged_tun':'NOT_RUN','requested_seconds':args.seconds,'payload_bytes':args.bytes,'seed':seed.hex()}
 def save():
  report.update(status=status,elapsed_seconds=round(time.monotonic()-started,3),cycles=cycles,bytes_verified=received,cycle_chain_sha256=chain.hex(),cycle_sample=list(sample),transient_errors=list(faults),cleanup_errors=cleanup)
  report['24_hour_soak']='PASS'if status=='PASS'and complete and args.seconds>=86400 else'NOT_RUN'
  if fatal:report['error']=fatal
  temp=output/'report.json.tmp';temp.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');os.replace(temp,output/'report.json')
 save()
 try:
  with tempfile.TemporaryDirectory(prefix='dln-remote-',dir=output)as temporary:
   work=Path(temporary);cfg=source
   if not settings.get('path'):
    if not one('gateway')or not one('transport'):raise ValueError('config requires path entries or gateway and transport')
    cfg=work/'pool.cfg';absolute=[]
    for line in text.splitlines():
     if '='in line and not line.lstrip().startswith('#'):
      key,value=line.split('=',1)
      if key.strip()in('identity','ca','locator','trust_cache','proxy_ca','proxy_token')and value.strip()and not Path(value.strip()).is_absolute():line=key+'='+str((source.parent/value.strip()).resolve())
     absolute.append(line)
    cfg.write_text('\n'.join(absolute)+f'\npath={one("transport")},{one("gateway")},*,unspecified,unspecified,unspecified,unspecified,0\n'+('proxy_paths=0\n'if one('proxy_address')else''),encoding='utf-8')
   incoming=work/'input.bin';download=work/'output.bin';descriptor=work/'delivery.dld'
   def command(name,*extra):
    r=subprocess.run([str(binary),name,'--config',str(cfg),*map(str,extra)],capture_output=True,text=True,timeout=180)
    if r.returncode:return r.returncode,None,r.stderr[-2048:]
    try:return 0,json.loads(r.stdout),''
    except ValueError:return -1,None,'gateway command returned non-JSON output'
   while True:
    cycle_started=time.monotonic();payload=hashlib.shake_256(seed+cycles.to_bytes(8,'big')).digest(args.bytes);expected=hashlib.sha256(payload).hexdigest();incoming.write_bytes(payload);descriptor.unlink(missing_ok=True);download.unlink(missing_ok=True);attempts=0
    while True:
     attempts+=1;code,value,error=command('pool-upload','--input',incoming,'--descriptor',descriptor,'--ttl',max(300,args.recovery_seconds*3+60))
     if not code and value.get('status')!='completed_remote':raise RuntimeError('upload did not return a remote completion receipt')
     if not code:code,value,error=command('pool-download','--descriptor',descriptor,'--output',download)
     if not code:
      if not download.is_file()or download.stat().st_size!=len(payload)or digest(download)!=expected:raise RuntimeError('independent downloaded length/SHA-256 mismatch')
      break
     faults.append({'cycle':cycles,'attempt':attempts,'status':code,'error':error})
     if code not in(2,3,5,7,17,20)or time.monotonic()-cycle_started>=args.recovery_seconds:raise RuntimeError('delivery recovery deadline exhausted: '+error)
     save();time.sleep(min(2,.1*2**min(attempts,4)))
    code,_,error=command('pool-consume','--descriptor',descriptor)
    if code:cleanup+=1;faults.append({'cycle':cycles,'cleanup_status':code,'error':error})
    chain=hashlib.sha256(chain+cycles.to_bytes(8,'big')+bytes.fromhex(expected)).digest();received+=len(payload);cycles+=1;sample.append({'cycle':cycles,'bytes':len(payload),'sha256':expected,'attempts':attempts,'elapsed_seconds':round(time.monotonic()-cycle_started,3)});save()
    if time.monotonic()-started>=args.seconds:complete=True;break
    time.sleep(args.interval_ms/1000)
  status='PASS'
 except KeyboardInterrupt:status='INCOMPLETE';fatal='interrupted before the requested duration completed'
 except(OSError,ValueError,RuntimeError,subprocess.SubprocessError)as e:status='FAIL';fatal=str(e)
 save();print(json.dumps({'status':status,'report':str(output/'report.json'),'cycles':cycles,'bytes_verified':received,'network_scope':args.scope,'24_hour_soak':report['24_hour_soak']}));return 0 if status=='PASS'else 3 if status=='INCOMPLETE'else 1

def main():
 p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--config',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--scope',choices=('local','public-internet'),default='local');p.add_argument('--seconds',type=int,default=0);p.add_argument('--bytes',type=int,default=65553);p.add_argument('--interval-ms',type=int,default=1000);p.add_argument('--recovery-seconds',type=int,default=30);args=p.parse_args()
 if not 0<=args.seconds<=259200 or not 1<=args.bytes<=16777216 or not 1<=args.interval_ms<=60000 or not 1<=args.recovery_seconds<=3600:p.error('duration, payload, interval, or recovery limit exceeded')
 try:return run(args)
 except(OSError,ValueError,subprocess.SubprocessError)as e:p.error(str(e))
if __name__=='__main__':raise SystemExit(main())
