#!/usr/bin/env python3
"""Compare DLM Koren valve transfer tables against original LIVE reference."""
import ctypes as C, math, struct, subprocess, argparse
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--reference-dir',type=Path,required=True);p.add_argument('--library',type=Path,default=ROOT/'dist/linux-x86_64/libchordlive.so');a=p.parse_args()
source=a.reference_dir.resolve()/'Source/DSP';ref=ROOT/'tests/reference_tube'
subprocess.run(['g++','-std=c++17','-O2','-I',str(source),str(ROOT/'tests/reference_tube.cpp'),
                *[str(source/name) for name in ('LiveAudioEngine.cpp','DacModel.cpp','TubeStage.cpp','ThreeBandTube.cpp','OutputStage.cpp')],'-lm','-o',str(ref)],check=True)
lib=C.CDLL(str(a.library.resolve()))
lib.dlm_native_thread_init()
lib.live_create_ex.argtypes=[C.c_double,C.c_uint32,C.c_uint32,C.c_uint32,C.c_float];lib.live_create_ex.restype=C.c_void_p
lib.live_destroy.argtypes=[C.c_void_p]
lib.live_tube_config.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_float,C.c_float,C.c_float,C.c_float];lib.live_tube_config.restype=C.c_uint32
lib.live_process_final.argtypes=[C.c_void_p,C.POINTER(C.c_float),C.POINTER(C.c_float),C.c_size_t,C.c_float,C.c_float,C.c_float,C.c_uint32,
                                 C.c_uint32,C.c_float,C.c_float,C.c_float,C.c_float,C.c_float,C.c_bool,C.c_bool,C.c_float];lib.live_process_final.restype=C.c_uint32
lib.dlm_native_live_allocations.restype=C.c_uint64
lib.dlm_native_allocations.restype=C.c_uint64
lib.dlm_native_last_error.restype=C.c_char_p
for type,topology,driveDb,bias,sag,wet,depth in [(1,0,8.0,0.2,0.2,0.8,1),(2,0,14.1,0.408,0,0.696,1),(3,1,22.0,-0.15,0.4,0.9,1),(4,0,11.0,0.0,0.3,0.8,2),(5,1,19.0,0.2,0.5,0.7,1),(6,0,24.0,-0.4,0.5,0.4,0)]:
    rate=48000.0;cross=.18;mix=.89;gain=-1.5;filter=3;count=1800;auto=0;limiter=0;ceiling=-1;block=47;dac=0
    args=[rate,depth,cross,mix,gain,filter,count,auto,limiter,ceiling,block,dac,type,topology,driveDb,bias,sag,wet]
    data=subprocess.check_output([str(ref),*[str(z) for z in args]])
    samples=struct.unpack('<'+'f'*(2*count),data)
    L=(C.c_float*count)();R=(C.c_float*count)()
    for i in range(count):
        L[i]=C.c_float(0.38*math.sin(0.023*i)+0.22*math.cos(0.007*i)).value
        R[i]=C.c_float(0.3*math.sin(0.013*i+0.4)).value
    h=lib.live_create_ex(rate,depth,0,dac,.25);assert h
    before=lib.dlm_native_allocations()
    for start in range(0,count,block):
        n=min(block,count-start)
        assert lib.live_tube_config(h,type,topology,driveDb,bias,sag,wet)==1
        ll=C.cast(C.byref(L,4*start),C.POINTER(C.c_float));rr=C.cast(C.byref(R,4*start),C.POINTER(C.c_float))
        assert lib.live_process_final(h,ll,rr,n,cross,mix,gain,filter,0,0,0,0,0,0,False,False,ceiling)==1,lib.dlm_native_last_error()
    assert lib.dlm_native_allocations()==before
    err=max([abs(L[i]-samples[i]) for i in range(count)]+[abs(R[i]-samples[count+i]) for i in range(count)])
    print(f'valve type={type} topo={topology} depth={depth} error={err:.8g}')
    assert err<2e-5,(type,err)
    lib.live_destroy(h);assert lib.dlm_native_live_allocations()==0
print('PASS: six valve models and output stage vs C++')
