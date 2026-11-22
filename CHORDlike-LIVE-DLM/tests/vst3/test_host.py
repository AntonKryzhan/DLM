"""Minimal synthetic VST3 host tests: ABI, processor, controller and audio."""
import ctypes as c, sys, math
from pathlib import Path
module=Path(sys.argv[1]).resolve()
lib=c.CDLL(str(module));lib.GetPluginFactory.restype=c.c_void_p
p=c.c_void_p; i=c.c_int32;u=c.c_uint32
class CI(c.Structure):
    _fields_=[('cid',c.c_char*16),('cardinality',i),('category',c.c_char*32),('name',c.c_char*64)]
class Setup(c.Structure):
    _fields_=[('mode',i),('size',i),('maxBlock',i),('rate',c.c_double)]
fptr=c.POINTER(c.c_float)
class Bus(c.Structure):
    _fields_=[('channels',i),('silence',c.c_uint64),('data',c.POINTER(fptr))]
class Data(c.Structure):
    _fields_=[('mode',i),('size',i),('frames',i),('ni',i),('no',i),('input',c.POINTER(Bus)),('output',c.POINTER(Bus)),('paramIn',p),('paramOut',p),('inEvents',p),('outEvents',p),('context',p)]
def call(obj,slot,ret,*args):
    vt=c.cast(obj,c.POINTER(p))[0]
    address=c.cast(vt,c.POINTER(p))[slot]
    return c.CFUNCTYPE(ret,p,*args)(address)
def guid(a,b,d,e):return c.create_string_buffer(b''.join(x.to_bytes(4,'big') for x in (a,b,d,e)),16)
f=lib.GetPluginFactory();assert f
assert call(f,4,i)(f)==2
classes=[]
for j in range(2):
    ci=CI();assert call(f,5,i,i,p)(f,j,c.byref(ci))==0;classes.append(ci)
unk=guid(0,0,0xC0000000,0x46)
def instantiate(n):
    out=p();cid=c.create_string_buffer(bytes(classes[n].cid),16)
    assert call(f,6,i,p,p,p)(f,c.cast(cid,p),c.cast(unk,p),c.byref(out))==0
    return out
comp=instantiate(0);ctrl=instantiate(1)
assert call(comp,3,i,p)(comp,None)==0
assert call(ctrl,3,i,p)(ctrl,None)==0
assert call(ctrl,8,i)(ctrl)==48
assert call(comp,7,i,i,i)(comp,0,0)==1
identity=guid(0x42043F99,0xB7DA453C,0xA569E79D,0x9AAEC33D)
audio=p();assert call(comp,0,i,p,p)(comp,c.cast(identity,p),c.byref(audio))==0
assert call(audio,5,i,i)(audio,0)==0
assert call(audio,6,u)(audio)==173
setup=Setup(0,0,1024,48000.0)
assert call(audio,7,i,p)(audio,c.byref(setup))==0
assert call(comp,11,i,c.c_uint8)(comp,1)==0
n=1024
left=(c.c_float*n)();right=(c.c_float*n)()
outl=(c.c_float*n)();outr=(c.c_float*n)()
left[0]=0.1;right[0]=-0.1
buffersIn=(fptr*2)(c.cast(left,fptr),c.cast(right,fptr))
buffersOut=(fptr*2)(c.cast(outl,fptr),c.cast(outr,fptr))
busIn=Bus(2,0,buffersIn);busOut=Bus(2,0,buffersOut)
data=Data(0,0,n,1,1,c.pointer(busIn),c.pointer(busOut),None,None,None,None,None)
assert call(audio,9,i,p)(audio,c.byref(data))==0
assert max(map(abs,outl))>1e-8
assert all(math.isfinite(x) for x in outl)
call(audio,2,u)(audio);call(comp,4,i)(comp);call(comp,2,u)(comp)
call(ctrl,4,i)(ctrl);call(ctrl,2,u)(ctrl);call(f,2,u)(f)
print('PASS: VST3 factory, 2 classes, 48 parameters, 173 samples, 1024-frame stereo DSP')
