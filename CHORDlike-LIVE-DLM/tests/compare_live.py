#!/usr/bin/env python3
import argparse
import ctypes as C
import math
from pathlib import Path
import struct
import subprocess
import sys
import os

project=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description='Compare DLM output to the original C++ LIVE engine')
parser.add_argument('--reference-dir',type=Path,help='Directory containing original Source/DSP/*.cpp and headers')
parser.add_argument('--reference-bin',type=Path,help='Already compiled C++ reference harness')
parser.add_argument('--library',type=Path,default=project/'dist/linux-x86_64/libchordlive.so')
args=parser.parse_args()
if args.reference_bin:
    ref=args.reference_bin.resolve()
elif args.reference_dir:
    source=args.reference_dir.resolve()/'Source/DSP'
    if not (source/'LiveAudioEngine.cpp').is_file():
        parser.error('--reference-dir must contain Source/DSP/LiveAudioEngine.cpp')
    ref=project/'tests/reference_live'
    compiler='g++'
    subprocess.run([compiler,'-std=c++17','-O2','-I',str(source),
                    str(project/'tests/reference_live.cpp'),
                    *[str(source/name) for name in ('LiveAudioEngine.cpp','DacModel.cpp','TubeStage.cpp','ThreeBandTube.cpp')],
                    '-o',str(ref),'-lm'],check=True)
else:
    ref=project/'tests/reference_live'
    if not ref.exists():
        parser.error('Provide --reference-dir with the unpacked original CHORDlike project')
lib=C.CDLL(str(args.library.resolve()))
lib.live_create.argtypes=[C.c_double,C.c_uint32]
lib.live_create.restype=C.c_void_p
lib.live_process.argtypes=[C.c_void_p,C.POINTER(C.c_float),C.POINTER(C.c_float),C.c_size_t,C.c_float,C.c_float,C.c_float,C.c_uint32]
lib.live_process.restype=C.c_uint32
lib.live_process_ex.argtypes=lib.live_process.argtypes+[C.c_uint32,C.c_float,C.c_float,C.c_float]
lib.live_process_ex.restype=C.c_uint32
lib.live_create_ex.argtypes=[C.c_double,C.c_uint32,C.c_uint32,C.c_uint32,C.c_float]
lib.live_create_ex.restype=C.c_void_p
lib.live_process_full.argtypes=lib.live_process_ex.argtypes+[C.c_float,C.c_float]
lib.live_process_full.restype=C.c_uint32
lib.live_reset.argtypes=[C.c_void_p]
lib.live_destroy.argtypes=[C.c_void_p]
lib.dlm_native_last_error.restype=C.c_char_p
lib.dlm_native_thread_init.restype=C.c_uint64
lib.dlm_native_live_allocations.restype=C.c_uint64
lib.dlm_native_thread_init()

def scenario(rate,depth,cross,mix,gain,mode,count=800,phaseMode=0,focus=0.0,phase=0.0,tone=0.0,pulseMode=0,dacMode=0,mismatch=0.25,character=0.0,pulseIntensity=0.0):
    args=[rate,depth,cross,mix,gain,mode,count,phaseMode,focus,phase,tone,pulseMode,dacMode,mismatch,character,pulseIntensity]
    data=subprocess.check_output([str(ref),*[str(x) for x in args]])
    samples=struct.unpack('<'+'f'*(2*count),data)
    left=(C.c_float*count)()
    right=(C.c_float*count)()
    for i in range(count):
        left[i]=C.c_float(0.38*math.sin(0.023*i)+0.22*math.cos(0.007*i)).value
        right[i]=C.c_float(0.3*math.sin(0.013*i+0.4)).value
    h=lib.live_create_ex(rate,depth,pulseMode,dacMode,mismatch)
    assert h, f'create failed {lib.dlm_native_last_error()}'
    for k in range(0,count,43):
        n=min(43,count-k)
        assert lib.live_process_full(h,C.cast(C.byref(left,4*k),C.POINTER(C.c_float)),C.cast(C.byref(right,4*k),C.POINTER(C.c_float)),n,cross,mix,gain,mode,phaseMode,focus,phase,tone,character,pulseIntensity)==1, lib.dlm_native_last_error()
    delta=max([abs(left[i]-samples[i]) for i in range(count)]+[abs(right[i]-samples[count+i]) for i in range(count)])
    rms=math.sqrt(sum((left[i]-samples[i])**2+(right[i]-samples[count+i])**2 for i in range(count))/(2*count))
    lib.live_destroy(h)
    assert not lib.dlm_native_last_error(), lib.dlm_native_last_error()
    print(f'rate={rate:6.0f} depth={depth} pulse={pulseMode} dac={dacMode} phase={phaseMode} cross={cross} mix={mix} gain={gain} mode={mode} max={delta:.3e} rms={rms:.3e}')
    return delta

if __name__=='__main__':
    worst=0.0
    for rate,depth,cross,mix,gain,mode in [(44100,0,0,1,0,0),(48000,1,0,1,0,0),(48000,2,0,1,0,0),(48000,3,0,1,0,0),(48000,4,0,1,0,0),(48000,5,0,1,0,0),(48000,1,0.6,0.7,-2,2),(96000,1,0.8,0.9,3,3)]:
        worst=max(worst,scenario(rate,depth,cross,mix,gain,mode))
    worst=max(worst,scenario(48000,1,0.5,0.8,-1,3,2400,phaseMode=1,focus=0.7,phase=0.8,tone=0.65))
    worst=max(worst,scenario(96000,2,0.3,0.9,0,2,1600,phaseMode=2,focus=1.0,phase=0.5,tone=0.95))
    for pulseMode,dacMode in [(1,0),(2,0),(0,1),(0,2),(0,3)]:
        worst=max(worst,scenario(48000,1,0.3,0.9,-1,2,800,phaseMode=1,focus=0.2,phase=0.4,tone=0.1,pulseMode=pulseMode,dacMode=dacMode,mismatch=0.37,character=0.35,pulseIntensity=0.6))
    print('worst:',worst,'live allocations:',lib.dlm_native_live_allocations())
    assert worst < 2e-5, f'regression: {worst}'
    assert lib.dlm_native_live_allocations()==0
    print('PASS: supported LIVE DSP paths match C++ reference')
