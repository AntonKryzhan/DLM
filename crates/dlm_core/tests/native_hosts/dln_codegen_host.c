/* Independent wire vectors; link the header/archive emitted from dln_codegen.dlm. */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "dln_codegen.a.h"

static void integer(uint8_t *out,uint64_t value,size_t width,int little) {
    for(size_t i=0;i<width;i++) out[little?i:width-i-1]=(uint8_t)(value>>(8*i));
}
static size_t varint(uint8_t *out,uint64_t value) {
    size_t count=0;do { uint8_t byte=(uint8_t)(value&127);value>>=7;out[count++]=(uint8_t)(byte|(value?128:0)); } while(value);return count;
}
static size_t field(uint8_t *out,size_t offset,unsigned format,uint32_t id,unsigned kind,const uint64_t *numbers,const uint8_t *const *bytes,const size_t *lengths,size_t count,int repeated) {
    int little=format!=1;
    if(format && repeated) { integer(out+offset,count,4,little);offset+=4; }
    for(size_t i=0;i<count;i++) {
        uint8_t scalar[10];size_t width=0;const uint8_t *value=scalar;
        if(kind==6 || kind==7) { width=lengths[i];value=bytes[i]; }
        else if(kind==8 || kind==9) width=varint(scalar,numbers[i]);
        else { width=kind==1 || kind==5?1:kind==2?2:kind==3?4:8;integer(scalar,numbers[i],width,little); }
        if(!format) { integer(out+offset,id,4,1);integer(out+offset+4,width,4,1);offset+=8; }
        else if(kind==6 || kind==7) { integer(out+offset,width,4,little);offset+=4; }
        if(width) { memcpy(out+offset,value,width); }offset+=width;
    }
    return offset;
}
static size_t sample(uint8_t *out,unsigned format) {
    integer(out,1,4,0);size_t offset=4;
    if(!format) { memcpy(out+4,"DLMB\002\000\000\000",8);offset=12; }
    const uint64_t byte[]={255},word[]={65535},counts[]={0,UINT32_MAX,42},wide[]={UINT64_MAX},flag[]={1},varints[]={0,127,128,UINT64_MAX},signed_values[]={UINT64_MAX,UINT64_MAX-1};
    const uint8_t binary[]={0,255,128,42},utf8[]={0xd0,0x9f,0xd1,0x80,0xd0,0xb8,0xd0,0xb2,0xd0,0xb5,0xd1,0x82},old[]="old";
    const uint8_t *blobs[]={binary,NULL},*texts[]={utf8},*retired[]={old};const size_t blob_sizes[]={4,0},text_sizes[]={sizeof utf8},old_sizes[]={3};
    offset=field(out,offset,format,1,1,byte,NULL,NULL,1,0);offset=field(out,offset,format,2,2,word,NULL,NULL,1,1);offset=field(out,offset,format,3,3,counts,NULL,NULL,3,1);
    offset=field(out,offset,format,4,4,wide,NULL,NULL,1,0);offset=field(out,offset,format,5,5,flag,NULL,NULL,1,0);offset=field(out,offset,format,6,6,NULL,blobs,blob_sizes,2,1);offset=field(out,offset,format,7,7,NULL,texts,text_sizes,1,1);
    offset=field(out,offset,format,8,8,varints,NULL,NULL,4,1);offset=field(out,offset,format,9,9,signed_values,NULL,NULL,2,1);
    if(!format) { offset=field(out,offset,format,10,2,word,NULL,NULL,1,1);integer(out+offset,99,4,1);integer(out+offset+4,3,4,1);out[offset+8]=0;out[offset+9]=255;out[offset+10]=128;memset(out+offset+11,0,8);offset+=19; }
    else offset=field(out,offset,format,11,6,NULL,retired,old_sizes,1,1);
    return offset;
}
static void clean(void) { assert(!dlm_native_last_error());assert(!dlm_native_live_allocations()); }
int main(void) {
    uint8_t input[1024],output[1024],mutated[1024];uint32_t random=142;
    for(unsigned format=0;format<3;format++) {
        size_t size=sample(input,format);memset(output,165,sizeof output);dn_r_CodecIo encoded=codec_serialize_demo(format,output,sizeof output);assert(!encoded.f_error.f_kind && encoded.f_count==size && encoded.f_next==size && !memcmp(input,output,size) && output[size]==165);clean();
        dn_r_CodecProbe probe=codec_probe(input,size,format);assert(!probe.f_error.f_kind && probe.f_roundtrip && probe.f_scalar==UINT64_MAX && probe.f_signed_value==INT64_MIN && probe.f_values==17 && probe.f_unknown==(format?0:19));clean();
        size_t budget=256+17*32+(format?19:16)+(format?0:19);assert(!codec_probe_budget(input,size,format,budget).f_kind && codec_probe_budget(input,size,format,budget-1).f_kind==7);clean();
        for(size_t capacity=0;capacity<size;capacity++) {
            memset(output,165,sizeof output);encoded=codec_canonical(input,size,format,output,capacity);assert(encoded.f_error.f_kind==1 && !encoded.f_count);for(size_t i=0;i<sizeof output;i++) assert(output[i]==165);clean();
        }
        for(size_t prefix=0;prefix<size;prefix++) { probe=codec_probe(input,prefix,format);if(!probe.f_error.f_kind) assert(!format && probe.f_roundtrip);clean(); }
        for(unsigned n=0;n<5000;n++) {
            memcpy(mutated,input,size);random=random*1664525u+1013904223u;size_t position=random%size;random=random*1664525u+1013904223u;mutated[position]^=(uint8_t)(1u<<(random%8));probe=codec_probe(mutated,size,format);
            assert(probe.f_error.f_kind==0 || probe.f_error.f_kind==1 || probe.f_error.f_kind==4 || probe.f_error.f_kind==7 || probe.f_error.f_kind==11);if(!probe.f_error.f_kind) assert(probe.f_roundtrip);clean();
        }
        integer(input,2,4,0);size=4;if(!format) { memcpy(input+4,"DLMB\002\000\000\000",8);size=12; }dn_r_CodecScalar dispatch=codec_dispatch(input,size,format);assert(!dispatch.f_error.f_kind && dispatch.f_value==2);clean();
    }
    assert(codec_builder_failures()==63);clean();
    for(unsigned n=0;n<24;n++) { codec_abort_owners();assert(dlm_native_last_error() && !dlm_native_live_allocations());dlm_native_clear_error();clean(); }
    assert(codec_probe(NULL,0,0).f_error.f_kind==1);clean();puts("generated codec independent vectors 15000 mutations quotas and cleanup ABI host OK");return 0;
}
