#!/usr/bin/env python3
"""Generated codecs against independent Python wire/value oracles; stdlib only."""
import ctypes as C
import importlib.util
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[4];HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('codec_checker',ROOT/'tools/check_network_codec.py');checker=importlib.util.module_from_spec(spec);spec.loader.exec_module(checker)
U32,U64,SZ,I64=C.c_uint32,C.c_uint64,C.c_size_t,C.c_int64;PTR=C.POINTER(C.c_uint8)
MAX=(1<<64)-1
class Error(C.Structure):_fields_=[('kind',C.c_int32),('field',U32),('offset',SZ)]
class Io(C.Structure):_fields_=[('count',SZ),('next',SZ),('error',Error)]
class Scalar(C.Structure):_fields_=[('value',U64),('error',Error)]
class Probe(C.Structure):_fields_=[('error',Error),('canonical_count',SZ),('scalar',U64),('signed_value',I64),('values',SZ),('unknown',SZ),('roundtrip',C.c_bool)]
class Invalid(ValueError):pass

def need(condition):
    if not condition:raise Invalid()
def uleb(n):
    out=bytearray()
    while n>=128:out.append((n&127)|128);n>>=7
    out.append(n);return bytes(out)
def read_uleb(data,position,end):
    value=0;start=position
    for index in range(10):
        need(position<end);byte=data[position];position+=1;need(index!=9 or byte<=1);value|=(byte&127)<<(index*7)
        if byte<128:need(uleb(value)==data[start:position]);return value,position
    raise Invalid()
def zigzag(n):return ((n<<1)^(n>>63))&MAX
def signed(n):return (n>>1)^-(n&1)
def payload(kind,value,endian):
    if kind in (6,7):return value
    if kind in (8,9):return uleb(zigzag(value) if kind==9 else value)
    width={1:1,2:2,3:4,4:8,5:1}[kind];return int(value).to_bytes(width,endian)
def tlv(identifier,data):return struct.pack('<II',identifier,len(data))+data

def describe(format):
    name=('codec_echo','codec_pos','codec_le')[format];path=ROOT/f'examples/native/generated/{name}.dlm';w,messages=checker.parse_descriptor(Path(str(path)+'.dns').read_bytes());minor=1 if format==0 else 0
    fields=[f for f in messages[0][1] if f[5]<=minor<=f[6]];maximum=(12 if w[4]==2 else 4)+sum(checker.measure(f,w[4])[1] for f in fields)+w[17]
    return w,fields,maximum

def encode(values,unknown,format):
    w,fields,_=describe(format);endian='big' if w[5]==1 else 'little';out=bytearray(struct.pack('!I',1))
    if w[4]==2:out+=b'DLMB'+struct.pack('<HH',w[6],0)
    for f in fields:
        fid,kind,minimum,maximum,length,*_=f;items=values.get(fid,[])
        if w[4]==1 and (minimum,maximum)!=(1,1):out+=len(items).to_bytes(4,endian)
        for item in items:
            body=payload(kind,item,endian)
            if w[4]==2:out+=tlv(fid,body)
            else:
                if kind in (6,7):out+=len(body).to_bytes(4,endian)
                out+=body
    for fid,body in unknown:out+=tlv(fid,body)
    return bytes(out)

def decode(data,format):
    w,fields,maximum=describe(format);need(len(data)<=maximum and len(data)>=4 and struct.unpack_from('!I',data)[0]==1);position=4;endian='big' if w[5]==1 else 'little';known={f[0]:f for f in fields};values={f[0]:[] for f in fields};unknown=[]
    def value(f,start,end,exact):
        kind=f[1]
        if kind in (6,7):
            if not exact:need(start+4<=end);size=int.from_bytes(data[start:start+4],endian);start+=4;need(size<=f[4] and start+size<=end);finish=start+size
            else:finish=end;need(finish-start<=f[4])
            body=data[start:finish]
            if kind==7:
                try:body.decode('utf-8','strict')
                except UnicodeError:raise Invalid()
            return body,finish
        if kind in (8,9):
            n,finish=read_uleb(data,start,end);need(not exact or finish==end);return signed(n) if kind==9 else n,finish
        width={1:1,2:2,3:4,4:8,5:1}[kind];need(start+width<=end and (not exact or start+width==end));n=int.from_bytes(data[start:start+width],endian);need(kind!=5 or n<=1);return n,start+width
    if w[4]==1:
        for f in fields:
            n=1
            if f[2:4]!=[1,1]:need(position+4<=len(data));n=int.from_bytes(data[position:position+4],endian);position+=4
            need(f[2]<=n<=f[3])
            for _ in range(n):item,position=value(f,position,len(data),False);values[f[0]].append(item)
    else:
        need(position+8<=len(data) and data[position:position+4]==b'DLMB' and struct.unpack_from('<HH',data,position+4)==(w[6],0));position+=8
        while position<len(data):
            need(position+8<=len(data));fid,count=struct.unpack_from('<II',data,position);position+=8;need(position+count<=len(data));end=position+count
            if fid in known:item,_=value(known[fid],position,end,True);values[fid].append(item);need(len(values[fid])<=known[fid][3])
            else:need(w[7]==1);unknown.append((fid,data[position:end]))
            position=end
    need(position==len(data));need(all(f[2]<=len(values[f[0]])<=f[3] for f in fields));need(len(unknown)<=w[16] and sum(8+len(body) for _,body in unknown)<=w[17]);return values,unknown

def demo(format):
    values={1:[255],2:[65535],3:[0,(1<<32)-1,42],4:[MAX],5:[1],6:[b'\0\xff\x80*',b''],7:['Привет'.encode()],8:[0,127,128,MAX],9:[-(1<<63),(1<<63)-1]}
    if format==0:values[10]=[65535];unknown=[(99,b'\0\xff\x80'),(0,b'')]
    else:values[11]=[b'old'];unknown=[]
    return values,unknown

class CodecTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory(prefix='dln-codec-');out=Path(cls.directory.name)/('codec.dll' if os.name=='nt' else 'codec.so');compiler=os.environ.get('DLM_NETWORK_DLM',str(ROOT/'target/release'/('dlm.exe' if os.name=='nt' else 'dlm')))
        result=subprocess.run([compiler,'build',str(HERE/'dln_codegen.dlm'),'--lib',str(ROOT/'lib'),'--lib',str(ROOT/'examples/native'),'--allow-unsafe','--target','sharedlib','-o',str(out)],capture_output=True,text=True)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        cls.api=C.CDLL(str(out));signatures={'codec_probe':(Probe,[PTR,SZ,U32]),'codec_canonical':(Io,[PTR,SZ,U32,PTR,SZ]),'codec_serialize_demo':(Io,[U32,PTR,SZ]),'codec_dispatch':(Scalar,[PTR,SZ,U32]),'codec_probe_budget':(Error,[PTR,SZ,U32,SZ]),'codec_builder_failures':(U32,[]),'codec_abort_owners':(None,[]),'dlm_native_last_error':(C.c_char_p,[]),'dlm_native_clear_error':(None,[]),'dlm_native_live_allocations':(U64,[])}
        for name,(restype,args) in signatures.items():f=getattr(cls.api,name);f.restype,f.argtypes=restype,args
    @classmethod
    def tearDownClass(cls):cls.directory.cleanup()
    def tearDown(self):self.assertIsNone(self.api.dlm_native_last_error());self.assertEqual(self.api.dlm_native_live_allocations(),0)
    def data(self,blob):return (C.c_uint8*len(blob)).from_buffer_copy(blob)
    def probe(self,blob,format):return self.api.codec_probe(self.data(blob),len(blob),format)
    def canonical(self,blob,format,capacity=1024):
        out=self.data(b'\xa5'*capacity);result=self.api.codec_canonical(self.data(blob),len(blob),format,out,capacity);return result,bytes(out)
    def valid(self,blob,format):
        expected,unknown=decode(blob,format);result=self.probe(blob,format);self.assertEqual(result.error.kind,0);self.assertTrue(result.roundtrip);self.assertEqual(result.scalar,expected[4][0]);self.assertEqual(result.signed_value,expected[9][0]);self.assertEqual(result.values,sum(map(len,expected.values())));self.assertEqual(result.unknown,sum(8+len(x) for _,x in unknown))
        canonical=encode(expected,unknown,format);result,out=self.canonical(blob,format);self.assertEqual((result.error.kind,result.count,result.next),(0,len(canonical),len(canonical)));self.assertEqual(out[:result.count],canonical);self.assertEqual(out[result.count:],b'\xa5'*(len(out)-result.count));return canonical
    def test_serializer_matches_independent_all_kind_vectors(self):
        for format in range(3):
            expected=encode(*demo(format),format);out=self.data(b'\xa5'*1024);result=self.api.codec_serialize_demo(format,out,len(out));self.assertEqual((result.error.kind,result.count),(0,len(expected)));self.assertEqual(bytes(out)[:result.count],expected);self.assertEqual(bytes(out)[result.count:],b'\xa5'*(len(out)-result.count));self.valid(expected,format)
    def test_all_short_output_capacities_preserve_the_entire_buffer(self):
        for format in range(3):
            packet=encode(*demo(format),format)
            for capacity in range(len(packet)):
                out=self.data(b'\xa5'*(len(packet)+8));result=self.api.codec_serialize_demo(format,out,capacity);self.assertEqual((result.error.kind,result.count),(1,0));self.assertEqual(bytes(out),b'\xa5'*len(out))
    def test_roundtrip_empty_optional_binary_and_utf8_values(self):
        for format in range(3):
            values={1:[0],4:[0],5:[0],9:[0],6:[b'',b''],7:[b''],8:[0]};self.valid(encode(values,[],format),format)
    def test_all_truncated_prefixes_agree_with_independent_parser(self):
        for format in range(3):
            packet=encode(*demo(format),format)
            for size in range(len(packet)):
                partial=packet[:size]
                try:decode(partial,format);valid=True
                except Invalid:valid=False
                actual=self.probe(partial,format);self.assertEqual(actual.error.kind==0,valid,(format,size,actual.error.kind))
    def test_tlv_reorders_known_fields_and_retains_unknown_byte_order(self):
        packet=encode(*demo(0),0);entries=[];position=12
        while position<len(packet):
            _,count=struct.unpack_from('<II',packet,position);entries.append(packet[position:position+8+count]);position+=8+count
        # Preserve same-field occurrence order but reverse field groups.
        groups={};unknown=[]
        for item in entries:
            fid=struct.unpack_from('<I',item)[0]
            if fid in {f[0] for f in describe(0)[1]}:groups.setdefault(fid,[]).append(item)
            else:unknown.append(item)
        mixed=packet[:12]+unknown[0]+b''.join(part for fid in sorted(groups,reverse=True) for part in groups[fid])+unknown[1]
        self.assertEqual(self.valid(mixed,0),packet)
    def test_signed_extremes_and_canonical_uleb_transitions(self):
        for format in range(3):
            for n in (0,1,127,128,16383,16384,1<<63,MAX):
                values={1:[1],4:[n],5:[1],8:[n],9:[-(1<<63),(1<<63)-1]};self.valid(encode(values,[],format),format)
    def test_utf8_rejects_overlong_surrogate_out_of_range_and_truncation(self):
        for bad in (b'\xc0\x80',b'\x80',b'\xed\xa0\x80',b'\xf4\x90\x80\x80',b'\xf0\x9f'):
            for format in range(3):
                values={1:[1],4:[1],5:[0],9:[0],7:[bad]};result=self.probe(encode(values,[],format),format);self.assertEqual(result.error.kind,4)
    def test_boolean_noncanonical_values_are_rejected(self):
        for format in range(3):
            values={1:[1],4:[1],5:[255],9:[0]};self.assertEqual(self.probe(encode(values,[],format),format).error.kind,4)
    def test_noncanonical_and_overflow_varints_are_rejected(self):
        base=encode({1:[1],4:[1],5:[1],9:[0]},[],0)
        for body in (b'\x80\0',b'\x80'*9+b'\x02',b'\x80'*10+b'\0',b'\x80',b''):
            packet=base+tlv(8,body);result=self.probe(packet,0);self.assertEqual(result.error.kind,4)
    def test_declared_oversize_is_rejected_before_waiting_or_allocating(self):
        base=encode({1:[1],4:[1],5:[1],9:[0]},[],0)
        for fid,count in ((6,33),(7,65),(99,MAX&((1<<32)-1))):
            result=self.probe(base+struct.pack('<II',fid,count),0);self.assertEqual(result.error.kind,7)
    def test_duplicate_singular_and_excess_repeated_fields_hit_count_limits(self):
        base=encode({1:[1],4:[1],5:[1],9:[0]},[],0)
        self.assertEqual(self.probe(base+tlv(1,b'\1'),0).error.kind,7)
        self.assertEqual(self.probe(base+b''.join(tlv(3,struct.pack('<I',n)) for n in range(4)),0).error.kind,7)
    def test_unknown_field_count_and_byte_limits(self):
        values={1:[1],4:[1],5:[0],9:[0]}
        self.assertEqual(self.probe(encode(values,[(99,b'')]*5,0),0).error.kind,7)
        self.assertEqual(self.probe(encode(values,[(99,b'x'*249)],0),0).error.kind,7)
    def test_missing_required_field_and_bad_envelope_are_rejected(self):
        values={1:[1],4:[1],5:[0],9:[0]};base=encode(values,[],0)
        for offset,value in ((4,ord('?')),(8,1),(10,1)):
            packet=bytearray(base);packet[offset]=value;self.assertEqual(self.probe(bytes(packet),0).error.kind,4)
        self.assertEqual(self.probe(encode({1:[1],4:[1],5:[0]},[],0),0).error.kind,4)
    def test_positional_count_prefixes_and_trailing_bytes_are_checked(self):
        for format in (1,2):
            packet=bytearray(encode({1:[1],4:[1],5:[0],9:[0]},[],format));packet[5:9]=b'\xff'*4;self.assertEqual(self.probe(bytes(packet),format).error.kind,7)
            base=encode(*demo(format),format);self.assertEqual(self.probe(base+b'\0',format).error.kind,4)
    def test_dispatcher_handles_empty_messages_wrong_ids_and_empty_input(self):
        for format in range(3):
            packet=struct.pack('!I',2)+(b'DLMB\2\0\0\0' if format==0 else b'');result=self.api.codec_dispatch(self.data(packet),len(packet),format);self.assertEqual((result.value,result.error.kind),(2,0))
            bad=struct.pack('!I',99);self.assertEqual(self.api.codec_dispatch(self.data(bad),len(bad),format).error.kind,11)
            self.assertEqual(self.api.codec_dispatch(None,0,format).error.kind,1)
    def test_requested_value_storage_budget_is_checked_before_allocation(self):
        for format in range(3):
            values,unknown=demo(format);packet=encode(values,unknown,format);payload=sum(len(v) for f in describe(format)[1] if f[1] in (6,7) for v in values.get(f[0],[]));needed=256+32*sum(map(len,values.values()))+payload+sum(8+len(v) for _,v in unknown);raw=self.data(packet)
            self.assertEqual(self.api.codec_probe_budget(raw,len(packet),format,needed).kind,0);self.assertEqual(self.api.codec_probe_budget(raw,len(packet),format,needed-1).kind,7)
    def test_builder_failures_preserve_messages_and_output(self):self.assertEqual(self.api.codec_builder_failures(),63)
    def test_abort_cleanup_releases_partially_moved_message_owners(self):
        for _ in range(24):
            self.api.codec_abort_owners();self.assertTrue(self.api.dlm_native_last_error());self.assertEqual(self.api.dlm_native_live_allocations(),0);self.api.dlm_native_clear_error()
    def test_random_valid_values_match_independent_wire_and_inverse_oracle(self):
        rng=random.Random(142)
        for format in range(3):
            fields=describe(format)[1]
            for _ in range(220):
                values={}
                for f in fields:
                    fid,kind,minimum,maximum,length,*_=f;items=[]
                    for _ in range(rng.randint(minimum,maximum)):
                        if kind==6:item=bytes(rng.randrange(256) for _ in range(rng.randrange(length+1)))
                        elif kind==7:item=rng.choice(('', 'ASCII', 'Привет', '\0', '🙂')).encode()
                        elif kind==9:item=rng.choice((-(1<<63),-1,0,1,(1<<63)-1))
                        elif kind==8:item=rng.choice((0,127,128,MAX,1<<63))
                        else:item=rng.randrange(1<<{1:8,2:16,3:32,4:64,5:1}[kind])
                        items.append(item)
                    values[fid]=items
                unknown=[(99+i,bytes(rng.randrange(256) for _ in range(rng.randrange(12)))) for i in range(rng.randrange(5))] if format==0 else []
                self.valid(encode(values,unknown,format),format)
    def test_mutated_packets_match_independent_acceptance_and_never_trap(self):
        rng=random.Random(143)
        for format in range(3):
            baseline=encode(*demo(format),format)
            for _ in range(700):
                packet=bytearray(baseline)
                for _ in range(rng.randrange(1,5)):packet[rng.randrange(len(packet))]=rng.randrange(256)
                if rng.randrange(4)==0:packet=packet[:rng.randrange(len(packet)+1)]
                packet=bytes(packet)
                try:decode(packet,format);expected=True
                except Invalid:expected=False
                result=self.probe(packet,format);self.assertEqual(result.error.kind==0,expected,(format,result.error.kind,packet.hex()))
                if expected:self.valid(packet,format)
                self.assertIsNone(self.api.dlm_native_last_error())

if __name__=='__main__':unittest.main(verbosity=2)
