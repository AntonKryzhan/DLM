#!/usr/bin/env python3
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[1]
HOSTS=ROOT/'crates/dlm_core/tests/native_hosts'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def source_files():
 paths={ROOT/'Cargo.toml',ROOT/'Cargo.lock',ROOT/'lib/std/dln_experiment.dlm',ROOT/'crates/dlm_core/tests/dln_experiment.rs',HOSTS/'channel_lab_boundary.dlm',HOSTS/'dln_experiment_host.c',HOSTS/'dln_experiment.py',ROOT/'tools/dln_experiment_compare.py',ROOT/'native/quic/build.py',ROOT/'native/quic/provider.c',ROOT/'vendor/DLN_QUIC_SOURCES.json',Path(__file__).resolve()}
 paths.update(p for p in(ROOT/'crates/dlm_channel_lab').rglob('*')if p.is_file());return{p.relative_to(ROOT).as_posix():sha(p)for p in sorted(paths)if '__pycache__'not in p.parts}
def main():
 p=argparse.ArgumentParser();p.add_argument('--profile',choices=('debug','release'),default='debug');p.add_argument('--output',type=Path,default=ROOT/'target/dln-experiment-acceptance');p.add_argument('--skip-build',action='store_true');p.add_argument('--quic',type=Path);p.add_argument('--sanitizers',action='store_true');a=p.parse_args();output=a.output.resolve();output.mkdir(parents=True,exist_ok=True);initial=source_files();env=dict(os.environ,CARGO_BUILD_JOBS=os.environ.get('CARGO_BUILD_JOBS','2'),CARGO_INCREMENTAL='0',CARGO_PROFILE_DEV_DEBUG='0',CARGO_PROFILE_TEST_DEBUG='0');profile=ROOT/'target'/a.profile;compiler=profile/('dlm.exe'if os.name=='nt'else'dlm');cli=profile/('dlm-channel-lab.exe'if os.name=='nt'else'dlm-channel-lab');library=profile/('dlm_channel_lab.dll'if os.name=='nt'else'libdlm_channel_lab.so');env['DLM_NETWORK_DLM']=str(compiler);steps=[];started=datetime.datetime.now(datetime.timezone.utc).isoformat();(output/'report.json').write_text(json.dumps({'status':'RUNNING','started_utc':started})+'\n')
 def run(name,command,timeout=180,environment=None):
  log=output/(name+'.log');now=time.monotonic();print(name,flush=True)
  try:
   with log.open('w')as stream:r=subprocess.run([str(x)for x in command],cwd=ROOT,env=environment or env,stdout=stream,stderr=subprocess.STDOUT,timeout=timeout)
   code=r.returncode
  except(OSError,subprocess.TimeoutExpired)as e:log.write_text(str(e)+'\n');code=-1
  s={'name':name,'status':'PASS'if code==0 else'FAIL','exit_code':code,'elapsed_seconds':round(time.monotonic()-now,3),'log':log.name};steps.append(s);return s
 release=['--release']if a.profile=='release'else[]
 if not a.skip_build:run('build',['cargo','build','--workspace','--locked',*release],600)
 run('rust_laboratory',['cargo','test','--locked','-p','dlm_channel_lab',*release],180)
 run('rust_dlm_effects_and_regression',['cargo','test','--locked','-p','dlm_core','--test','dln_experiment','--test','dln_gateway','--test','dln_proxy','--test','dln_continuity','--test','dln_adaptive','--test','dln_crypto',*release],180)
 run('independent_python_and_C_DLM',[sys.executable,'-m','unittest',str(HOSTS/'dln_experiment.py')],180)
 for snapshot in sorted((ROOT/'docs').glob('DLN_BASELINE_*.json')):run('compatibility_'+snapshot.stem,[sys.executable,ROOT/'tools/check_dln_compatibility.py','--snapshot',snapshot],60)
 payload=output/'matrix.input';payload.write_bytes(bytes(range(256))+b'A\0\xff');config=output/'matrix.cfg';config.write_text('seed=150\n');matrix_path=output/'matrix.json';matrix_step=run('matrix',[cli,'matrix','--config',config,'--input',payload,'--report',matrix_path],120);matrix_summary={}
 if matrix_step['status']=='PASS':
  try:
   matrix=json.loads(matrix_path.read_text());assert len(matrix['cases'])==96;clean=[c['measurement']for c in matrix['cases']if c['case']=='clean'];closed=[c['measurement']for c in matrix['cases']if c['case']=='closed_channel'];assert len(clean)==len(closed)==8;assert all(m['payload_exact_match']for m in clean);assert all(not m['payload_exact_match']and m['useful_bits_per_second']==0 and m['statistics']['dmc_fit_capacity_lower_bits_per_symbol']==0 for m in closed);matrix_summary={'cases':96,'clean_delivery_cases':8,'closed_channel_false_deliveries':0,'input_sha256':sha(payload),'matrix_sha256':sha(matrix_path)}
  except(ValueError,KeyError,AssertionError):matrix_step['status']='FAIL';matrix_step['error']='matrix outcomes inconsistent'
 quic=a.quic.resolve()if a.quic else output/('dlm-quic-1.dll'if os.name=='nt'else'libdlm-quic-1.so')
 quic_ready=quic.is_file()if a.quic else run('quic_baseline_build',[sys.executable,ROOT/'native/quic/build.py',quic],180)['status']=='PASS'
 comparison_summary={}
 if quic_ready:
  step=run('physical_TLS_QUIC_comparison',[sys.executable,ROOT/'tools/dln_experiment_compare.py','--binary-dir',profile,'--quic',quic,'--output',output/'comparison'],90)
  if step['status']=='PASS':
   try:
    comparison=json.loads((output/'comparison/report.json').read_text());assert comparison['status']=='MEASURED'and comparison['execution_status']=='PASS';assert len(comparison['baseline_profiles'])==3 and len(comparison['experimental_profiles'])==8;assert all(v['payload_exact_match']and v['payload_sha256']==comparison['input_sha256']for v in comparison['baseline_profiles']);assert all(v['payload_sha256']==comparison['input_sha256']for v in comparison['experimental_profiles']);comparison_summary={'standard_baselines':3,'experimental_profiles':8,'experimental_delivery_samples':comparison['experimental_delivery_samples'],'input_bytes':comparison['input_bytes'],'input_sha256':comparison['input_sha256'],'report_sha256':sha(output/'comparison/report.json')}
   except(ValueError,KeyError,AssertionError):step['status']='FAIL';step['error']='incomplete or inconsistent transport comparison'
 else:steps.append({'name':'physical_TLS_QUIC_comparison','status':'FAIL','error':'pinned QUIC provider unavailable'})
 if a.sanitizers:
  static=output/'lab-sanitized.a';host=output/'lab-host-sanitized';cc=os.environ.get('CC','gcc');flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'];built=run('sanitized_dlm_build',[compiler,'build',HOSTS/'channel_lab_boundary.dlm','--lib',ROOT/'lib','--allow-unsafe','--target','staticlib','--cflag',flags[0],'--cflag',flags[1],'-o',static])
  if built['status']=='PASS':
   built=run('sanitized_c_build',[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,HOSTS/'dln_experiment_host.c',static,'-L'+str(profile),'-ldlm_channel_lab','-Wl,-rpath,'+str(profile),'-pthread','-ldl','-lm','-o',host])
   if built['status']=='PASS':run('sanitized_c_dlm_host',[host],60,dict(env,ASAN_OPTIONS='detect_leaks=0:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
 if initial!=source_files():steps.append({'name':'source_identity','status':'FAIL','error':'source changed during acceptance'})
 counts={'rust_passed':0,'rust_ignored':0,'python_passed':0,'python_skipped':0}
 for s in steps:
  text=(output/s['log']).read_text()if s.get('log')else''
  if s['name'].startswith('rust_'):
   for passed,ignored in re.findall(r'test result: ok\. (\d+) passed; 0 failed; (\d+) ignored;',text):counts['rust_passed']+=int(passed);counts['rust_ignored']+=int(ignored)
  if s['name']=='independent_python_and_C_DLM':
   match=re.search(r'Ran (\d+) tests? in',text);counts['python_passed']=int(match[1])if match and s['status']=='PASS'else 0;skip=re.search(r'skipped=(\d+)',text);counts['python_skipped']=int(skip[1])if skip else 0
 status='PASS'if all(s['status']=='PASS'for s in steps)and not counts['rust_ignored']and not counts['python_skipped']and counts['python_passed']else'FAIL';report={'format':'DLM-EXPERIMENT-ACCEPTANCE-v1','status':status,'scope':'local experimental laboratory and physical loopback only','started_utc':started,'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'platform':platform.platform(),'source_files':initial,'compiler_sha256':sha(compiler)if compiler.exists()else None,'lab_cli_sha256':sha(cli)if cli.exists()else None,'lab_library_sha256':sha(library)if library.exists()else None,'test_counts':counts,'matrix':matrix_summary,'physical_comparison':comparison_summary,'gateway_library_sha256':sha(profile/('dlm_gateway.dll'if os.name=='nt'else'libdlm_gateway.so')),'quic_sha256':sha(quic)if quic.is_file()else None,'steps':steps,'sanitizer_scope':'native C host and DLM generated C; Rust is not instrumented'if a.sanitizers else'NOT_RUN','production_carrier':False,'deferred_by_user':['native Windows/FreeBSD NET-D runs','two public Internet hosts','long-duration soak']};(output/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(status+': '+str(output/'report.json'));return 0 if status=='PASS'else 1
if __name__=='__main__':sys.exit(main())
