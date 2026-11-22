#!/usr/bin/env python3
"""ThreeBandTube vs original C++ LIVE in a two-channel hosted-style chain."""
import ctypes as C, math, struct, subprocess, argparse
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--reference-dir',type=Path,required=True);p.add_argument('--library',type=Path,default=ROOT/'dist/linux-x86_64/libchordlive.so');a=p.parse_args()
source=a.reference_dir.resolve()/'Source/DSP';ref=ROOT/'tests/reference_threeband'
subprocess.run(['g++','-std=c++17','-O2','-I',str(source),str(ROOT/'tests/reference_threeband.cpp'),
                *[str(source/name) for name in ('LiveAudioEngine.cpp','DacModel.cpp','TubeStage.cpp','ThreeBandTube.cpp','OutputStage.cpp')],'-lm','-o',str(ref)],check=True)
lib=C.CDLL(str(a.library.resolve()));lib.dlm_native_thread_init()
lib.live_create_ex.argtypes=[C.c_double,C.c_uint32,C.c_uint32,C.c_uint32,C.c_float];lib.live_create_ex.restype=C.c_void_p
lib.live_destroy.argtypes=[C.c_void_p]
lib.live_band_config.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.c_float,C.c_float,C.c_float,C.c_float];lib.live_band_config.restype=C.c_uint32
lib.live_band_frequency.argtypes=[C.c_void_p,C.c_float,C.c_float];lib.live_band_frequency.restype=C.c_uint32
lib.live_tube_routing.argtypes=[C.c_void_p,C.c_uint32];lib.live_tube_routing.restype=C.c_uint32
lib.live_process_final.argtypes=[C.c_void_p,C.POINTER(C.c_float),C.POINTER(C.c_float),C.c_size_t,C.c_float,C.c_float,C.c_float,C.c_uint32,
                                 C.c_uint32,C.c_float,C.c_float,C.c_float,C.c_float,C.c_float,C.c_bool,C.c_bool,C.c_float];lib.live_process_final.restype=C.c_uint32
lib.dlm_native_live_allocations.restype=C.c_uint64;lib.dlm_native_allocations.restype=C.c_uint64;lib.dlm_native_last_error.restype=C.c_char_p
for depth,mode,low,high,block in [(0,0,440,3232,43),(1,0,440,3232,43),(2,0,320,2800,43),(1,1,440,3232,51)]:
    rate=48000.;cross=.18;mix=.89;gain=-1.5;filter=3;count=1024;auto=0;limiter=0;ceiling=-1;dac=0
    args=[rate,depth,cross,mix,gain,filter,count,auto,limiter,ceiling,block,dac,low,high,mode]
    data=subprocess.check_output([str(ref),*[str(z) for z in args]])
    samples=struct.unpack('<'+'f'*(2*count),data)
    L=(C.c_float*count)();R=(C.c_float*count)()
    for i in range(count):
        L[i]=C.c_float(0.38*math.sin(0.023*i)+0.22*math.cos(0.007*i)).value
        R[i]=C.c_float(0.3*math.sin(0.013*i+0.4)).value
    h=lib.live_create_ex(rate,depth,0,dac,.25);assert h
    assert lib.live_tube_routing(h,1)==1
    assert lib.live_band_frequency(h,low,high)==1
    settings=[(2,0,14.1,.408,0,.696),(1,0,14.8,.368,0,.616),(3,0,27.6,.44,0,1)]
    if mode==1:settings=[(0,0,14.1,.408,0,.696),(0,0,14.8,.368,0,.616),(0,0,27.6,.44,0,1)]
    before=lib.dlm_native_allocations()
    for start in range(0,count,block):
        n=min(block,count-start)
        for idx,x in enumerate(settings):
            assert lib.live_band_config(h,idx,*x)==1
        ll=C.cast(C.byref(L,4*start),C.POINTER(C.c_float));rr=C.cast(C.byref(R,4*start),C.POINTER(C.c_float))
        assert lib.live_process_final(h,ll,rr,n,cross,mix,gain,filter,0,0,0,0,0,0,False,False,ceiling)==1,lib.dlm_native_last_error()
    assert lib.dlm_native_allocations()==before
    err=max([abs(L[i]-samples[i]) for i in range(count)]+[abs(R[i]-samples[count+i]) for i in range(count)])
    print(f'depth={depth} mode={mode} cutoff={low}-{high} max-diff={err:.8g}')
    assert err<2e-5,(depth,err)
    lib.live_destroy(h);assert lib.dlm_native_live_allocations()==0
print('PASS: ThreeBandTube and output stage vs C++')
