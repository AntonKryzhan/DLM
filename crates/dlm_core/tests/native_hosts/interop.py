# Independent XML and DLMB interoperability checks; usage: python3 interop.py XML_MODULE SETTINGS_MODULE
import ctypes as c,xml.etree.ElementTree as E,random
import sys
lib=c.CDLL(sys.argv[1]);f=lib.xml_roundtrip;f.argtypes=[c.c_void_p,c.c_size_t,c.c_void_p,c.c_size_t,c.c_bool];f.restype=c.c_size_t;lib.dlm_native_last_error.restype=c.c_char_p

def roundtrip(data,replace=False):
 out=c.create_string_buffer(max(1024,len(data)*6));n=f(data,len(data),out,len(out),replace);err=lib.dlm_native_last_error();assert not err,err;return out.raw[:n]
original=b"<?xml version='1.0'?><root a='old' future='keep'><future k='x'/></root>";changed=roundtrip(original,True);doc=E.fromstring(changed);assert doc.attrib=={'a':'Антон<&\t\r\n','future':'keep'};assert doc[0].tag=='future'
rng=random.Random(133)
for i in range(250):
 root=E.Element('root',{'id':str(i),'имя':'Антон & < > '+chr(0x400+i%100)});child=E.SubElement(root,'future',{'key':'unknown'});child.text=''.join(rng.choice('abc<> &\r\n\tА') for _ in range(50));data=E.tostring(root,encoding='utf-8',xml_declaration=True);out=roundtrip(data);assert out==data;assert E.tostring(E.fromstring(out))==E.tostring(E.fromstring(data))
for encoding in ('UTF-8','utf-8','Utf-8','uTf-8'):
 data=("<?xml version='1.0' encoding='"+encoding+"'?><root/>").encode();assert roundtrip(data)==data;E.fromstring(data)
invalid=[b'<a>',b'<a/>tail',b'<a x="1" x="2"/>',b'<a>&#xD800;</a>',b'<a>&#1114112;</a>',b'<a>&#9999999999999999999;</a>',b'<a>&bad;</a>',b'<a>]]></a>',b'<a x="<"/>',b'<a>\xc0\x80</a>',b'<a>\xed\xa0\x80</a>',b'<a>\x00</a>',b'<a></b>',b'<?XML version="1.0"?><a/>']
for data in invalid:assert not roundtrip(data),data
print('XML: 250 ElementTree roundtrips, escaped Unicode edits, 14 malformed cases OK')

import struct
settings=c.CDLL(sys.argv[2]);transform_fn=settings.settings_transform;transform_fn.argtypes=[c.c_void_p,c.c_size_t,c.c_void_p,c.c_size_t,c.c_double];transform_fn.restype=c.c_size_t;settings.dlm_native_last_error.restype=c.c_char_p

def field(i,b):return struct.pack('<II',i,len(b))+b
def transform(data,gain):
 out=c.create_string_buffer(len(data)+1024);n=transform_fn(data,len(data),out,len(out),gain);assert not settings.dlm_native_last_error();return out.raw[:n]
for i in range(100):
 unknown=random.Random(i).randbytes(20);name=('Антон '+str(i)).encode();data=b'DLMB'+struct.pack('<HH',1,0x8000)+field(900,unknown)+field(1,struct.pack('<d',.25))+field(2,name);out=transform(data,.75);assert out[:8]==b'DLMB'+struct.pack('<HH',2,0x8000);p=8;fields=[]
 while p<len(out):
  id,n=struct.unpack_from('<II',out,p);fields.append((id,out[p+8:p+8+n]));p+=8+n
 assert fields==[(900,unknown),(1,struct.pack('<d',.75)),(2,name),(3,b'\x01')]
 assert transform(out,1.0)
assert not transform(b'corrupt',1)
for data in [b'DLMB'+struct.pack('<HH',1,0)+field(1,b'x'), b'DLMB'+struct.pack('<HH',1,0)+field(2,b'\xff'),b'DLMB'+struct.pack('<HH',1,0)+field(3,b'\x02')]:assert not transform(data,1)
print('Binary: 100 independent Python encoders/decoders, version migration, unknown fields and flags, corrupt inputs OK')
