"""Windows x64 VST3 module smoke: factory, controller, DSP latency, F32 processing.
Not a replacement for Steinberg's validator or an actual DAW.
"""
import ctypes as c
import math
import os
from pathlib import Path
import sys

if len(sys.argv)!=2:
    sys.exit('usage: validate_vst3_windows.py <path-to-VST3-module>')
module=Path(sys.argv[1]).resolve()
if not module.is_file():
    sys.exit(f'VST3 binary missing: {module}')
if os.name=='nt':
    dll_directory = os.add_dll_directory(str(module.parent))
lib=c.CDLL(str(module))
lib.GetPluginFactory.restype=c.c_void_p
p=c.c_void_p; i=c.c_int32; u=c.c_uint32
class ClassInfo(c.Structure):
    _fields_=[('cid',c.c_char*16),('cardinality',i),('category',c.c_char*32),('name',c.c_char*64)]
class Setup(c.Structure):
    _fields_=[('mode',i),('size',i),('maxBlock',i),('rate',c.c_double)]
fptr=c.POINTER(c.c_float)
class Bus(c.Structure):
    _fields_=[('channels',i),('silence',c.c_uint64),('data',c.POINTER(fptr))]
class Data(c.Structure):
    _fields_=[('mode',i),('size',i),('frames',i),('ni',i),('no',i),('input',c.POINTER(Bus)),('output',c.POINTER(Bus)),('paramIn',p),('paramOut',p),('inEvents',p),('outEvents',p),('context',p)]

def method(obj,index,ret,*args):
    vt=c.cast(obj,c.POINTER(p))[0]
    address=c.cast(vt,c.POINTER(p))[index]
    return c.CFUNCTYPE(ret,p,*args)(address)

def uid(a,b,d,e):
    return c.create_string_buffer(a.to_bytes(4,'little')+bytes([b>>16&255,b>>24&255,b&255,b>>8&255])+d.to_bytes(4,'big')+e.to_bytes(4,'big'),16)

factory=lib.GetPluginFactory()
assert factory and method(factory,4,i)(factory)==2,'VST3 factory/classes not found'
classes=[]
for j in range(2):
    cl=ClassInfo()
    assert method(factory,5,i,i,p)(factory,j,c.byref(cl))==0
    classes.append(cl)
unknown=uid(0,0,0xC0000000,0x46)
def create(index):
    obj=p();cid=c.create_string_buffer(bytes(classes[index].cid),16)
    assert method(factory,6,i,p,p,p)(factory,c.cast(cid,p),c.cast(unknown,p),c.byref(obj))==0
    return obj
comp=create(0);ctrl=create(1)
assert method(comp,3,i,p)(comp,None)==0
assert method(ctrl,3,i,p)(ctrl,None)==0
assert method(ctrl,8,i)(ctrl)==48

# IEditController::createView must return a real Steinberg IPlugView.
view=method(ctrl,17,p,c.c_char_p)(ctrl,b'editor')
assert view, 'DLM custom editor missing'
assert method(view,3,i,c.c_char_p)(view,b'HWND')==0
class ViewRect(c.Structure):
    _fields_=[('left',i),('top',i),('right',i),('bottom',i)]
size=ViewRect()
assert method(view,9,i,p)(view,c.byref(size))==0
assert (size.right-size.left,size.bottom-size.top)==(1024,736)
view_iid=uid(0x5BC32507,0xD06049EA,0xA6151B52,0x2B755B29)
queried=p()
assert method(view,0,i,p,p)(view,c.cast(view_iid,p),c.byref(queried))==0 and queried.value==view
method(queried,2,u)(queried)
assert method(view,13,i)(view)==0  # supports resize and size constraints
method(view,2,u)(view)
processor=p();audio_uid=uid(0x42043F99,0xB7DA453C,0xA569E79D,0x9AAEC33D)
assert method(comp,0,i,p,p)(comp,c.cast(audio_uid,p),c.byref(processor))==0
assert method(processor,6,u)(processor)==173
assert method(processor,7,i,p)(processor,c.byref(Setup(0,0,1024,48000)))==0
assert method(comp,11,i,c.c_uint8)(comp,1)==0
n=1024
L=(c.c_float*n)();R=(c.c_float*n)();OL=(c.c_float*n)();OR=(c.c_float*n)()
L[0]=0.1;R[0]=-0.1
ins=(fptr*2)(c.cast(L,fptr),c.cast(R,fptr));outs=(fptr*2)(c.cast(OL,fptr),c.cast(OR,fptr))
bi=Bus(2,0,ins);bo=Bus(2,0,outs)
data=Data(0,0,n,1,1,c.pointer(bi),c.pointer(bo),None,None,None,None,None)
assert method(processor,9,i,p)(processor,c.byref(data))==0
assert max(map(abs,OL))>1e-9 and all(math.isfinite(x) for x in OL)
method(processor,2,u)(processor)
method(comp,4,i)(comp);method(comp,2,u)(comp)
method(ctrl,4,i)(ctrl);method(ctrl,2,u)(ctrl)
method(factory,2,u)(factory)
print('PASS: Windows VST3 loaded; IPlugView vector editor, 48 parameters, latency 173, stereo F32 processing')
