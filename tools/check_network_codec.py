#!/usr/bin/env python3
"""Independent DLN codec plan/wrapper checker. No generator or DLM VM invocation.
This checks schema/plan/source binding and a pinned runtime dependency closure;
protocol and machine proof obligations remain pending. Python standard library.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

LIBRARY_SHA256 = {'std/net_codec_runtime.dlm': 'fe14ea66586316b3be92715e14cb5e08270b982a3b6d3b1822ed8492c2405440', 'std/net_spec.dlm': '08da27fa1e4b12ef5df8a76e30ecde98594c9ea23385409153bdd9bced9fcb0a', 'std/net_spec_wire.dlm': '455f292d14526320e3d871da206f1fb6ec3dad0d524ce46d3bbd641f52751fc7', 'std/net_transport_policy.dlm': 'a5edaa7c9a3cb3cae21e303ba27fef8e2f1c903be937ecbff9ccd9913d1f4934', 'std/net_bytes.dlm': '0af4057e3bc77671db7177e13765069052830e21607554440281c807243ab49b', 'std/native_protocol.dlm': '7c7e619848c65279ad9a6eceadee4d73db5074f88836d336e603fb417c746e2f', 'std/native_binary.dlm': 'e09d329ec7c02ed80c6f6f095024f4481452746a9c5254c75d707136a7df4219'}
MAX=(1<<32)-1
PURE=' guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork)'
TOKEN=re.compile(r'[A-Za-z_][A-Za-z_0-9]*|[0-9]+|->|<=|>=|==|!=|&&|\|\||\.\.|[^\s]')

def require(condition,message):
    if not condition:raise ValueError(message)
def fnv(data):
    value=14695981039346656037
    for byte in data:value=((value^byte)*1099511628211)&((1<<64)-1)
    return f'{value:016x}'
def read(path,maximum):
    with path.open('rb') as stream:data=stream.read(maximum+1)
    require(len(data)<=maximum,f'{path}: input limit exceeded');return data
def parse_descriptor(data):
    require(136<=len(data)<=16904 and data[:4]==b'DNS1','invalid DNS1 framing')
    w=list(struct.unpack_from('!33I',data,4));require(w[14]==0 and 1<=w[32]<=16 and all(w[i] in (0,1) for i in (25,27,28)),'noncanonical descriptor header')
    messages=[];position=136
    for _ in range(w[32]):
        require(position+24<=len(data),'truncated message declaration');h=list(struct.unpack_from('!6I',data,position));position+=24;require(h[5]<=32 and position+h[5]*32<=len(data),'invalid field count')
        fields=[]
        for _ in range(h[5]):fields.append(list(struct.unpack_from('!8I',data,position)));position+=32
        messages.append((h,fields))
    require(position==len(data),'trailing descriptor data');validate_descriptor(w,messages);return w,messages

def measure(field,wire):
    _,kind,minimum,maximum,length,*_=field
    low,high={1:(1,1),2:(2,2),3:(4,4),4:(8,8),5:(1,1),6:(0,length),7:(0,length),8:(1,10),9:(1,10)}[kind]
    if wire==2:return minimum*(low+8),maximum*(high+8)
    if kind in (6,7):low+=4;high+=4
    prefix=0 if minimum==maximum==1 else 4
    return prefix+minimum*low,prefix+maximum*high

def validate_descriptor(w,messages):
    require(w[0]!=0 and 1<=w[1]<=65535 and w[3]<=w[2]<=65535,'invalid protocol/version')
    require((w[4]==1 and w[5] in (1,2) and w[6:8]==[0,0]) or (w[4]==2 and w[5]==2 and w[6] in (1,2) and w[7] in (0,1)),'invalid wire profile')
    required,forbidden,preferred=w[8:11];require(not ((required|forbidden|preferred)&~511) and not required&forbidden and not preferred&forbidden and required&66!=66,'contradictory transport properties')
    maximum,queue,channels,memory=w[11],w[12],w[13],w[15]
    require(maximum<=16777216 and 1<=queue<=4096 and 1<=channels<=256 and 1024<=memory<=67108864 and 1024+channels*256+2*(maximum+128)<=memory,'invalid transport resource bounds')
    require(w[16]<=4096 and w[17]<=maximum and (w[18] or w[19]) and 1<=w[20]<=529,'invalid schema/session budgets')
    require((w[7]==0 and w[16:18]==[0,0]) or (w[7]==1 and w[16]>=1 and w[17]>=8),'invalid unknown-field policy')
    require(0<w[21]<=w[22]<=w[23]<=2147483647 and 1<=w[24]<=queue,'invalid session contract')
    require(w[26]<=2 and w[29]<=2 and (not w[25] or w[26]!=0),'invalid security contract')
    require(w[30]&~2047==0 and w[31]==1,'engineering profile required; no proof claim may be supplied')
    nodes=1;last_id=0;windows=[];peak=0;sensitive=False
    for h,fields in messages:
        identifier,phase,senders,first,last,count=h
        require(last_id<identifier and phase in (1,2,3) and senders in (1,2,3) and first<=last and first<=w[2] and last>=w[3],'invalid message declaration');last_id=identifier;nodes+=1;windows.append((max(first,w[3]),min(last,w[2])))
        prior=0;points={max(first,w[3])}
        for f in fields:
            fid,kind,minimum,maximum_count,length,since,until,flags=f
            require(prior<fid and 1<=kind<=9 and minimum<=maximum_count and 1<=maximum_count<=4096 and length<=16777216 and since<=until and flags in (0,1),'invalid field declaration');prior=fid
            require((kind in (6,7) or length==0) and first<=since<=w[2] and until<=last and until>=w[3],'field interval/length mismatch')
            points|={since,until+1};nodes+=1;sensitive|=bool(flags)
        for version in points:
            if max(first,w[3])<=version<=min(last,w[2]):peak=max(peak,(12 if w[4]==2 else 4)+sum(measure(f,w[4])[1] for f in fields if f[5]<=version<=f[6]))
    cursor=w[3]
    for start,end in sorted(windows):
        require(start<=cursor,'uncovered minor version');cursor=max(cursor,end+1)
    require(cursor>w[2] and nodes<=w[20] and peak+w[17]<=w[11],'uncovered version or size/node limit exceeded')
    effective=required|16|(4 if sensitive or any(w[26:30]) else 0)
    require(not effective&forbidden and (not effective&8 or channels>=2),'effective properties conflict')

def pending(w,messages):
    secure=bool(w[8]&4 or any(w[26:30]) or any(f[7] for _,fields in messages for f in fields))
    return 431|w[30]|(16 if w[7] else 0)|(64 if w[25] else 0)|(1024 if secure else 0)

def tokens(source):
    # Generated declarations contain no string/character literals. Reject these
    # outside comments so comment-like text cannot hide executable statements.
    result=[];position=0
    while position<len(source):
        if source.startswith('//',position):
            end=source.find('\n',position);position=len(source) if end<0 else end+1;continue
        if source.startswith('/*',position):
            end=source.find('*/',position+2);require(end>=0,'unterminated comment');position=end+2;continue
        c=source[position]
        if c.isspace():position+=1;continue
        require(c not in ('"',"'"),'literal is outside the generated wrapper grammar')
        match=TOKEN.match(source,position);require(match is not None,'unrecognized source token');result.append(match.group());position=match.end()
    return result

def expected_source(w,messages,module,prefix,minor,storage):
    # Independent wrapper rules. Only constants, checked builders, and calls to
    # the pinned runtime are admitted; an equivalent-looking arbitrary body is
    # not accepted as a proof. The native compiler remains a separate gate.
    out=[f'module {module}\nimport std.net_codec_runtime\nnative {{']
    add=out.append;csv=lambda xs:','.join(map(str,xs));boolean=lambda x:'true' if x else 'false'
    add(f'fn {prefix}_spec() -> NetworkSpec{PURE} {{ let mut spec: NetworkSpec=network_spec({csv(w[:4])});')
    add(f'spec.wire=WireFormat({csv(w[4:8])});spec.transport=TransportRequirements({csv(w[8:11])});')
    add(f'spec.resources=ResourceBounds(TransportBounds({csv(w[11:14])},{w[15]}),{w[16]},{w[17]},U64({(w[18]<<32)|w[19]}),{w[20]});')
    add(f'spec.session=SessionContract({csv(w[21:25])},{boolean(w[25])});spec.security=SecurityRequirements({w[26]},{boolean(w[27])},{boolean(w[28])},{w[29]});spec.proofs=ProofObligations({w[30]},{w[31]});spec.message_count={len(messages)};')
    for i,(h,fields) in enumerate(messages):
        add(f'spec.messages[{i}]=message_schema({csv(h[:3])});spec.messages[{i}].first_minor={h[3]};spec.messages[{i}].last_minor={h[4]};spec.messages[{i}].field_count={len(fields)};')
        for j,f in enumerate(fields):add(f'spec.messages[{i}].fields[{j}]=MessageField({csv(f)});')
    add('return spec; }');plans=[]
    for h,declared in messages:
        if not h[3]<=minor<=h[4]:continue
        fields=[f for f in declared if f[5]<=minor<=f[6]];identifier=h[0];name=f'{prefix}Message{identifier}';base=12 if w[4]==2 else 4
        minimum=base+sum(measure(f,w[4])[0] for f in fields);maximum=base+sum(measure(f,w[4])[1] for f in fields)+w[17];values=sum(f[3] for f in fields)
        require(256+32*sum(f[2] for f in fields)<=storage,'storage cannot hold a minimum valid message')
        plans.append(([identifier,minimum,maximum,values,len(fields)],fields))
        add(f'record {name} repr(C) {{ inner: CodecMessage; }} record {name}Decoded repr(C) {{ value: {name}; error: CodecError; }}')
        add(f'fn {prefix}_plan_{identifier}() -> CodecPlan{PURE} {{ let mut plan: CodecPlan=zero<CodecPlan>();plan.message={identifier};plan.wire=WireFormat({csv(w[4:8])});plan.minor={minor};plan.field_count={len(fields)};')
        for i,f in enumerate(fields):add(f'plan.fields[{i}]=MessageField({csv(f)});')
        add(f'plan.maximum_values={values};plan.maximum_wire={maximum};plan.maximum_unknown_fields={w[16]};plan.maximum_unknown_bytes={w[17]};plan.storage_budget={storage};return plan; }}')
        add(f'fn {name}_new() -> {name} {{ return {name}(codec_message({identifier})); }}')
        add(f'fn {name}_decode(input: Slice<U8>) -> {name}Decoded {{ let plan: CodecPlan={prefix}_plan_{identifier}();let decoded: CodecDecoded=codec_decode(&plan,input);return {name}Decoded({name}(move decoded.value),decoded.error); }}')
        add(f'fn {name}_measure(message: &{name}) -> CodecIo{PURE} {{ let plan: CodecPlan={prefix}_plan_{identifier}();return codec_measure(&plan,&message.inner); }}')
        add(f'fn {name}_encode(message: &{name}, output: &mut Buffer<U8>, offset: USize) -> CodecIo{PURE} {{ let plan: CodecPlan={prefix}_plan_{identifier}();return codec_encode(&plan,&message.inner,output,offset); }}')
        add(f'fn {name}_equal(left: &{name}, right: &{name}) -> Bool{PURE} {{ let plan: CodecPlan={prefix}_plan_{identifier}();return codec_equal(&plan,&left.inner,&right.inner); }}')
        for f in fields:
            fid,kind=f[:2];label=f'{name}_field_{fid}'
            if kind in (6,7):
                add(f'fn {label}_add(message: &mut {name}, value: Slice<U8>) -> CodecError {{ let plan: CodecPlan={prefix}_plan_{identifier}();return codec_add_bytes(&plan,&mut message.inner,{fid},value); }}')
                add(f'fn {label}_span(message: &{name}, occurrence: USize) -> CodecSpan{PURE} {{ let plan: CodecPlan={prefix}_plan_{identifier}();return codec_get_span(&plan,&message.inner,{fid},occurrence); }}')
                add(f'fn {label}_view(message: &{name}, occurrence: USize) -> Slice<U8> borrows(message){PURE} {{ let span: CodecSpan={label}_span(message,occurrence);return codec_view(&message.inner,span); }}')
            else:
                ty={1:'U8',2:'U16',3:'U32',4:'U64',5:'Bool',8:'U64',9:'I64'}[kind];result=f'{name}Field{fid}';add(f'record {result} repr(C) {{ value: {ty}; error: CodecError; }}')
                setup='let mut scalar: U64=0;if value { scalar=1; }' if kind==5 else '';scalar='scalar' if kind==5 else 'zigzag_encode(value)' if kind==9 else 'cast<U64>(value)'
                add(f'fn {label}_add(message: &mut {name}, value: {ty}) -> CodecError {{ let plan: CodecPlan={prefix}_plan_{identifier}();{setup}return codec_add_scalar(&plan,&mut message.inner,{fid},{scalar}); }}')
                value='value.value!=0' if kind==5 else 'zigzag_decode(value.value)' if kind==9 else f'cast<{ty}>(value.value)'
                add(f'fn {label}_get(message: &{name}, occurrence: USize) -> {result}{PURE} {{ let plan: CodecPlan={prefix}_plan_{identifier}();let value: CodecScalar=codec_get_scalar(&plan,&message.inner,{fid},occurrence);return {result}({value},value.error); }}')
                setup='let mut scalar: U64=0;if values[index] { scalar=1; }' if kind==5 else '';value='scalar' if kind==5 else 'zigzag_encode(values[index])' if kind==9 else 'cast<U64>(values[index])'
                add(f'fn {label}_add_many(message: &mut {name}, values: Slice<{ty}>) -> CodecError {{ let plan: CodecPlan={prefix}_plan_{identifier}();let reserved: CodecReservation=codec_reserve_scalars(&plan,&mut message.inner,{fid},len(values));if reserved.error.kind!=0 {{ return reserved.error; }}for index in 0..len(values) {{ {setup}message.inner.values[reserved.start+index]=CodecValue({fid},{kind},{value},0,0); }}return CodecError(0,0,0); }}')
        if w[7]:add(f'fn {name}_add_unknown(message: &mut {name}, value: Slice<U8>) -> CodecError {{ let plan: CodecPlan={prefix}_plan_{identifier}();return codec_add_unknown(&plan,&mut message.inner,value); }}')
    add(f'fn {prefix}_decode(input: Slice<U8>) -> CodecDecoded {{ let id: ProtocolValue=net_read_be(input,0,4);if id.status!=0 {{ return codec_decode_error(id.status,0,0); }}')
    for h,_ in plans:add(f'if id.value==U64({h[0]}) {{ let plan: CodecPlan={prefix}_plan_{h[0]}();return codec_decode(&plan,input); }}')
    add('return codec_decode_error(11,0,0); } }');return '\n'.join(out),plans

def check(source_path,library):
    source_bytes=read(source_path,2*1024*1024);descriptor=read(Path(str(source_path)+'.dns'),16904);text=read(Path(str(source_path)+'.codec'),128*1024).decode('ascii');lines=text.splitlines();require(lines and lines[0]=='DLM-NET-CODEC-PLAN v1','invalid plan format')
    headers={};dependencies={};plans=[];position=1
    while position<len(lines):
        parts=lines[position].split();position+=1;require(len(parts)>=2,'malformed plan line');key=parts[0]
        if key=='dependency':
            require(len(parts)==3 and parts[1] not in dependencies,'duplicate dependency');dependencies[parts[1]]=parts[2]
        elif key=='message':
            require(len(parts)==6,'malformed message plan');h=list(map(int,parts[1:]));fields=[]
            for _ in range(h[4]):
                require(position<len(lines),'truncated field plan');f=lines[position].split();position+=1;require(len(f)==9 and f[0]=='field','malformed field plan');fields.append(list(map(int,f[1:])))
            plans.append((h,fields))
        else:
            require(len(parts)==2 and key not in headers,'duplicate/malformed header');headers[key]=parts[1]
    require(set(headers)=={'assurance','module','prefix','minor','storage','descriptor','descriptor_fnv64','source_fnv64','runtime_fnv64','pending','messages'},'unknown or missing plan headers')
    require(headers['assurance']=='engineering' and headers['descriptor']==descriptor.hex() and headers['descriptor_fnv64']==fnv(descriptor) and headers['source_fnv64']==fnv(source_bytes),'descriptor/source binding mismatch')
    require(set(dependencies)==set(LIBRARY_SHA256),'runtime dependency closure mismatch');hashes={}
    for name,expected in LIBRARY_SHA256.items():
        data=read(library/name,1024*1024);digest=hashlib.sha256(data).hexdigest();require(digest==expected and dependencies[name]==fnv(data),f'changed runtime dependency {name}');hashes[name]=digest
    require(headers['runtime_fnv64']==dependencies['std/net_codec_runtime.dlm'],'runtime binding mismatch')
    module,prefix=headers['module'],headers['prefix'];valid=lambda x:re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]{0,127}',x) is not None
    require(all(valid(s) for s in module.split('.')) and valid(prefix) and len(prefix)<=48,'invalid generated namespace')
    minor,storage=int(headers['minor']),int(headers['storage']);w,messages=parse_descriptor(descriptor);require(w[3]<=minor<=w[2] and 256<=storage<=67108864,'invalid selected version/storage');require(int(headers['pending'])==pending(w,messages),'forged pending-proof mask')
    expected,expected_plans=expected_source(w,messages,module,prefix,minor,storage);require(plans==expected_plans and int(headers['messages'])==len(plans),'active plan/table mismatch')
    require(tokens(source_bytes.decode('utf-8'))==tokens(expected),'generated wrapper body is outside the independently checked rules')
    return dict(format='DLM-NET-CODEC-CHECK-v1',status='passed',assurance='engineering',minor=minor,messages=len(plans),pending_proofs=pending(w,messages),unsupported_security=(1 if w[26]==2 else 0)|(2 if w[29]==2 else 0),descriptor_sha256=hashlib.sha256(descriptor).hexdigest(),source_sha256=hashlib.sha256(source_bytes).hexdigest(),dependencies=hashes,checked=['descriptor consistency','active field/bound plan','typed wrapper rules','runtime/source binding'],formal_protocol_certificate=False)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('source',type=Path);parser.add_argument('--lib',type=Path,default=Path(__file__).resolve().parents[1]/'lib');parser.add_argument('--report',type=Path);args=parser.parse_args()
    try:report=check(args.source,args.lib)
    except (OSError,ValueError,UnicodeError,struct.error) as error:report=dict(status='failed',error=str(error))
    output=json.dumps(report,indent=2,sort_keys=True)+'\n'
    if args.report:args.report.write_text(output,encoding='utf-8')
    print(output,end='');return 0 if report['status']=='passed' else 1
if __name__=='__main__':sys.exit(main())
