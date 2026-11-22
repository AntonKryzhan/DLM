#!/usr/bin/env python3
"""Compare DLM LIVE+OutputStage against original C++ Reference on identical blocks."""
import ctypes as C
import math
from pathlib import Path
import struct
import subprocess
import argparse

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--reference-dir',type=Path,required=True)
p.add_argument('--library',type=Path,default=ROOT/'dist/linux-x86_64/libchordlive.so')
a=p.parse_args()
source=a.reference_dir.resolve()/'Source/DSP'
ref=ROOT/'tests/reference_final'
subprocess.run(['g++','-std=c++17','-O2','-I',str(source),str(ROOT/'tests/reference_final.cpp'),
                *[str(source/name) for name in ('LiveAudioEngine.cpp','DacModel.cpp','TubeStage.cpp','ThreeBandTube.cpp','OutputStage.cpp')],'-lm','-o',str(ref)],check=True)
lib=C.CDLL(str(a.library.resolve()))
lib.dlm_native_thread_init.restype=C.c_uint64
lib.dlm_native_thread_init()
lib.live_create_ex.argtypes=[C.c_double,C.c_uint32,C.c_uint32,C.c_uint32,C.c_float]
lib.live_create_ex.restype=C.c_void_p
lib.live_destroy.argtypes=[C.c_void_p]
lib.live_process_final.argtypes=[C.c_void_p,C.POINTER(C.c_float),C.POINTER(C.c_float),C.c_size_t,C.c_float,C.c_float,C.c_float,C.c_uint32,
                                 C.c_uint32,C.c_float,C.c_float,C.c_float,C.c_float,C.c_float,C.c_bool,C.c_bool,C.c_float]
lib.live_process_final.restype=C.c_uint32
lib.live_latency_final.restype=C.c_uint32
lib.dlm_native_live_allocations.restype=C.c_uint64
lib.dlm_native_allocations.restype=C.c_uint64
lib.dlm_native_last_error.restype=C.c_char_p
assert lib.live_latency_final()==173
for depth,match,limiter,ceiling,block,dac in [(0,0,0,-1,43,0),(1,1,0,-1,43,0),(1,0,1,-3,47,0),(1,1,1,-6,71,0),(2,1,1,-0.1,200,0),(3,0,1,-1,43,2)]:
    rate=48000.0;cross=0.28;mix=0.86;gain=2.1;filter=3;count=1600
    args=[rate,depth,cross,mix,gain,filter,count,match,limiter,ceiling,block,dac]
    data=subprocess.check_output([str(ref),*[str(z) for z in args]])
    samples=struct.unpack('<'+'f'*(2*count),data)
    L=(C.c_float*count)();R=(C.c_float*count)()
    for i in range(count):
        L[i]=C.c_float(0.38*math.sin(0.023*i)+0.22*math.cos(0.007*i)).value
        R[i]=C.c_float(0.3*math.sin(0.013*i+0.4)).value
    h=lib.live_create_ex(rate,depth,0,dac,0.25)
    assert h
    before=lib.dlm_native_allocations()
    for start in range(0,count,block):
        width=min(block,count-start)
        ll=C.cast(C.byref(L,4*start),C.POINTER(C.c_float))
        rr=C.cast(C.byref(R,4*start),C.POINTER(C.c_float))
        assert lib.live_process_final(h,ll,rr,width,cross,mix,gain,filter,0,0,0,0,0,0,bool(match),bool(limiter),ceiling)==1,lib.dlm_native_last_error()
    assert lib.dlm_native_allocations()==before,'Heap allocation in process'
    err=max([abs(L[i]-samples[i]) for i in range(count)]+[abs(R[i]-samples[count+i]) for i in range(count)])
    print(f'depth={depth} auto={match} limit={limiter} ceiling={ceiling} block={block} dac={dac} max-diff={err:.3g}')
    assert err<2e-5,(depth,match,limiter,err)
    lib.live_destroy(h)
    assert lib.dlm_native_live_allocations()==0
print('PASS: OutputStage integrated, 173-sample latency, zero heap allocations in callback')
