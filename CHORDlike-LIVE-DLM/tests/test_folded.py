#!/usr/bin/env python3
"""Compare original liveOptimization path with DLM folded FIR; verify no allocations."""
import argparse,ctypes as C,math,struct,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--reference-dir',type=Path,required=True);p.add_argument('--library',type=Path,default=ROOT/'dist/linux-x86_64/libchordlive.so');a=p.parse_args()
source=a.reference_dir.resolve()/'Source/DSP';ref=ROOT/'tests/reference_fast'
subprocess.run(['g++','-std=c++17','-O2','-I',str(source),str(ROOT/'tests/reference_fast.cpp'),
                *[str(source/n) for n in ('LiveAudioEngine.cpp','DacModel.cpp','TubeStage.cpp','ThreeBandTube.cpp','OutputStage.cpp')],'-lm','-o',str(ref)],check=True)
l=C.CDLL(str(a.library.resolve()));l.dlm_native_thread_init()
l.live_create_ex.argtypes=[C.c_double,C.c_uint32,C.c_uint32,C.c_uint32,C.c_float];l.live_create_ex.restype=C.c_void_p
l.live_destroy.argtypes=[C.c_void_p]
l.live_fast_mode.argtypes=[C.c_void_p,C.c_bool];l.live_fast_mode.restype=C.c_uint32
l.live_process_final.argtypes=[C.c_void_p,C.POINTER(C.c_float),C.POINTER(C.c_float),C.c_size_t,C.c_float,C.c_float,C.c_float,C.c_uint32,
                                 C.c_uint32,C.c_float,C.c_float,C.c_float,C.c_float,C.c_float,C.c_bool,C.c_bool,C.c_float];l.live_process_final.restype=C.c_uint32
l.dlm_native_live_allocations.restype=C.c_uint64;l.dlm_native_allocations.restype=C.c_uint64;l.dlm_native_last_error.restype=C.c_char_p
for depth,block in [(1,43),(3,43),(5,43)]:
    rate=48000.;cross=.18;mix=.89;gain=-1.5;filter=3;count=1600;auto=0;limiter=0;ceiling=-1;dac=0
    args=[rate,depth,cross,mix,gain,filter,count,auto,limiter,ceiling,block,dac]
    data=subprocess.check_output([str(ref),*[str(z) for z in args]])
    reference=struct.unpack('<'+'f'*(2*count),data)
    L=(C.c_float*count)();R=(C.c_float*count)()
    for i in range(count):
        L[i]=C.c_float(.38*math.sin(.023*i)+.22*math.cos(.007*i)).value
        R[i]=C.c_float(.3*math.sin(.013*i+.4)).value
    t0=time.perf_counter();h=l.live_create_ex(rate,depth,0,dac,.25);prepare=time.perf_counter()-t0
    assert h
    assert l.live_fast_mode(h,True)==1
    before=l.dlm_native_allocations(); t0=time.perf_counter()
    for k in range(0,count,block):
        m=min(block,count-k)
        ll=C.cast(C.byref(L,4*k),C.POINTER(C.c_float));rr=C.cast(C.byref(R,4*k),C.POINTER(C.c_float))
        assert l.live_process_final(h,ll,rr,m,cross,mix,gain,filter,0,0,0,0,0,0,False,False,ceiling)==1,l.dlm_native_last_error()
    duration=time.perf_counter()-t0
    assert l.dlm_native_allocations()==before
    err=max([abs(L[i]-reference[i]) for i in range(count)]+[abs(R[i]-reference[count+i]) for i in range(count)])
    print(f'depth={depth} prep={prepare:.3f}s process={duration:.3f}s max-diff={err:.8g}')
    assert err<2e-5,(depth,err)
    l.live_destroy(h);assert l.dlm_native_live_allocations()==0
print('PASS: folded fast path vs original LIVE reference')
