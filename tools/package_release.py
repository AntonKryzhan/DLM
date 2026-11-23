#!/usr/bin/env python3
import argparse
from collections import deque
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[1]
def command(args):
 p=subprocess.run([str(a)for a in args],cwd=ROOT,capture_output=True,text=True)
 if p.returncode:raise RuntimeError(p.stdout+p.stderr)
 return p.stdout

def package(build,output,compiler):
 windows=os.name=='nt';version=re.search(r'^version = "([^"]+)"', (ROOT/'Cargo.toml').read_text(),re.M)[1];name='DLM-v%s-%s-x86_64'%(version,'windows'if windows else'linux');stage=output/name
 if stage.exists():raise RuntimeError('staging directory already exists: '+str(stage))
 stage.mkdir(parents=True);bin_dir=stage/'bin';bin_dir.mkdir();binary_suffix='.exe'if windows else'';library_suffix='.dll'if windows else'.so'
 for executable in('dlm','dlm-net','dlm-channel-lab','dlm-network-check','dlm-transport-check'):
  source=build/(executable+binary_suffix)
  if not source.is_file():raise RuntimeError('missing built executable: '+str(source))
  shutil.copy2(source,bin_dir/source.name)
 for library in('dlm_gateway','dlm_channel_lab'):
  source=build/((library if windows else'lib'+library)+library_suffix)
  if not source.is_file():raise RuntimeError('missing built library: '+str(source))
  shutil.copy2(source,bin_dir/source.name)
  if windows:
   source=build/('lib'+library+'.dll.a');shutil.copy2(source,bin_dir/source.name)
 shutil.copytree(ROOT/'lib',stage/'lib',ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
 (stage/'include').mkdir()
 for component in('dlm_gateway','dlm_channel_lab'):
  for header in(ROOT/'crates'/component/'include').glob('*.h'):shutil.copy2(header,stage/'include'/header.name)
 if windows:
  cc=Path(shutil.which(compiler)or compiler).resolve();dll_dir=cc.parent;objdump=dll_dir/'objdump.exe';seen=set()
  for pattern in('libgnutls-*.dll','libhogweed-*.dll','libnettle-*.dll','libcurl-*.dll'):
   candidates=sorted(dll_dir.glob(pattern),reverse=True)
   if not candidates:raise RuntimeError('runtime dependency not installed: '+pattern)
   shutil.copy2(candidates[0],bin_dir/candidates[0].name)
  def dependencies():
   queue=deque(p for p in bin_dir.iterdir()if p.suffix.lower()in('.exe','.dll'))
   while queue:
    path=queue.popleft()
    if path.name.lower()in seen:continue
    seen.add(path.name.lower())
    for dep in re.findall(r'DLL Name:\s*(\S+)',command([objdump,'-p',path])):
     if (bin_dir/dep).is_file():queue.append(bin_dir/dep);continue
     source=dll_dir/dep
     if source.is_file():shutil.copy2(source,bin_dir/dep);queue.append(bin_dir/dep)
     elif dep.lower().startswith(('api-ms-win-','ext-ms-win-'))or(Path(os.environ['SystemRoot'])/'System32'/dep).exists():continue
     else:raise RuntimeError('unresolved runtime DLL: '+dep)
  dependencies()
  print(command(['pwsh','-NoProfile','-File',ROOT/'DLM-pre/Build-Quic.ps1','-SourcePath',ROOT,'-SdkPath',stage,'-CCompiler',cc]),end='');dependencies()
  licenses=dll_dir.parent/'share/licenses'
  if licenses.is_dir():shutil.copytree(licenses,stage/'licenses/msys2',dirs_exist_ok=True)
  sdk=ROOT/'DLM-pre/sdk'
  for source in sdk.iterdir():
   if source.is_dir():shutil.copytree(source,stage/source.name)
   elif source.suffix.lower()=='.cmd':
    text=source.read_text();text=text.replace('set "CC=%~dp0toolchain\\mingw64\\bin\\gcc.exe"','if exist "%~dp0toolchain\\mingw64\\bin\\gcc.exe" (set "CC=%~dp0toolchain\\mingw64\\bin\\gcc.exe") else if not defined CC set "CC=gcc"');hogweed=next(bin_dir.glob('libhogweed-*.dll')).name;text=text.replace('libhogweed-6.dll',hogweed);(stage/source.name).write_bytes(text.replace('\r\n','\n').replace('\n','\r\n').encode())
  for executable in('dlm-net','dlm-channel-lab'):
   (stage/(executable+'.cmd')).write_bytes(('@echo off\r\nsetlocal\r\nset "DLM_PATH=%~dp0lib"\r\nset "PATH=%~dp0bin;%PATH%"\r\nset "DLM_LIBQUIC=%~dp0bin\\dlm-quic-1.dll"\r\n"%~dp0bin\\'+executable+'.exe" %*\r\nexit /b %ERRORLEVEL%\r\n').encode())
 else:
  print(command([sys.executable,ROOT/'native/quic/build.py',bin_dir/'libdlm-quic-1.so']),end='')
  for executable in('dlm','dlm-net','dlm-channel-lab'):
   script=stage/executable;script.write_text('#!/bin/sh\nDLM_RUNTIME_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nexport DLM_PATH="$DLM_RUNTIME_ROOT/lib"\nexport LD_LIBRARY_PATH="$DLM_RUNTIME_ROOT/bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\nexport DLM_LIBQUIC="$DLM_RUNTIME_ROOT/bin/libdlm-quic-1.so"\nexec "$DLM_RUNTIME_ROOT/bin/'+executable+'" "$@"\n');script.chmod(0o755)
 info={'version':version,'commit':os.environ.get('GITHUB_SHA')or command(['git','rev-parse','HEAD']).strip(),'platform':'windows-x86_64'if windows else'linux-x86_64','c_compiler_bundled':False,'native_c_build_requires':'existing GCC/Clang; private SDK toolchain is reused when present','rust':command(['rustc','--version']).strip(),'c_compiler':command([compiler,'--version']).splitlines()[0]};(stage/'build-info.json').write_text(json.dumps(info,indent=2)+'\n');files=sorted(p for p in stage.rglob('*')if p.is_file());(stage/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(stage).as_posix()+'\n'for p in files));archive=output/(name+'.zip')
 with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9)as z:
  for p in sorted(stage.rglob('*')):
   if p.is_file():z.write(p,p.relative_to(stage))
 with zipfile.ZipFile(archive)as z:
  if z.testzip()is not None:raise RuntimeError('built artifact ZIP verification failed')
 print(json.dumps({'status':'PACKAGED','archive':str(archive),'sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'bytes':archive.stat().st_size}))
 return stage

def main():
 p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,required=True);p.add_argument('--output',type=Path,default=ROOT/'dist');p.add_argument('--compiler',default=os.environ.get('CC','gcc'));a=p.parse_args();package(a.build_dir.resolve(),a.output.resolve(),a.compiler)
if __name__=='__main__':main()
