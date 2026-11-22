#!/usr/bin/env python3
import ctypes as C, math, struct,subprocess,argparse
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--reference-dir',type=Path,required=True);p.add_argument('--library',type=Path,default=ROOT/'dist/linux-x86_64/libchordlive.so');a=p.parse_args()
source=a.reference_dir.resolve()/'Source/DSP';ref=ROOT/'tests/reference_transition'
subprocess.run(['g++','-std=c++17','-O2','-I',str(source),str(ROOT/'tests/reference_transition.cpp'),
                *[str(source/n) for n in ('LiveAudioEngine.cpp','DacModel.cpp','TubeStage.cpp','ThreeBandTube.cpp','OutputStage.cpp')],'-lm','-o',str(ref)],check=True)
l=C.CDLL(str(a.library.resolve()));l.dlm_native_thread_init()
l.live_create_ex.argtypes=[C.c_double,C.c_uint32,C.c_uint32,C.c_uint32,C.c_float];l.live_create_ex.restype=C.c_void_p
l.live_destroy.argtypes=[C.c_void_p]
l.live_request_mode.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.c_float];l.live_request_mode.restype=C.c_uint32
l.live_process_final.argtypes=[C.c_void_p,C.POINTER(C.c_float),C.POINTER(C.c_float),C.c_size_t,C.c_float,C.c_float,C.c_float,C.c_uint32,
                                 C.c_uint32,C.c_float,C.c_float,C.c_float,C.c_float,C.c_float,C.c_bool,C.c_bool,C.c_float];l.live_process_final.restype=C.c_uint32
l.dlm_native_live_allocations.restype=C.c_uint64;l.dlm_native_allocations.restype=C.c_uint64;l.dlm_native_last_error.restype=C.c_char_p
for depth0,depth1,pulse0,pulse1,dac0,dac1,block,changeBlock in [(1,3,0,0,0,0,43,12),(2,1,0,1,0,0,47,12),(2,2,0,2,0,0,43,10),(1,2,0,0,0,2,47,12)]:
    args=[depth0,depth1,pulse0,pulse1,dac0,dac1,block,changeBlock]
    data=subprocess.check_output([str(ref),*[str(z) for z in args]])
    n=1720;refdata=struct.unpack('<'+'f'*(2*n),data)
    L=(C.c_float*n)();R=(C.c_float*n)()
    for i in range(n):
        L[i]=C.c_float(.38*math.sin(.023*i)+.22*math.cos(.007*i)).value
        R[i]=C.c_float(.3*math.sin(.013*i+.4)).value
    h=l.live_create_ex(48000,depth0,pulse0,dac0,.25);assert h
    before=l.dlm_native_allocations()
    for k in range(0,n,block):
        if k//block==changeBlock: assert l.live_request_mode(h,depth1,pulse1,dac1,.25)==1
        m=min(block,n-k)
        ll=C.cast(C.byref(L,4*k),C.POINTER(C.c_float));rr=C.cast(C.byref(R,4*k),C.POINTER(C.c_float))
        assert l.live_process_final(h,ll,rr,m,.27,.9,-1,3,0,0,0,0,0,0,True,False,-1)==1,l.dlm_native_last_error()
    assert l.dlm_native_allocations()==before
    err=max([abs(L[i]-refdata[i]) for i in range(n)]+[abs(R[i]-refdata[n+i]) for i in range(n)])
    print(f'ratio {depth0}->{depth1} pulse {pulse0}->{pulse1} dac {dac0}->{dac1} err={err}')
    assert err<2e-5,err
    l.live_destroy(h);assert l.dlm_native_live_allocations()==0
print('PASS: dynamic mode transitions vs original LIVE')
