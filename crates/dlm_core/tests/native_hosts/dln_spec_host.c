/* Link with the C/header emitted from dln_spec.dlm by the actual compiler. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "dln_spec.a.h"
static void put(uint8_t *out,uint32_t n) { out[0]=(uint8_t)(n>>24);out[1]=(uint8_t)(n>>16);out[2]=(uint8_t)(n>>8);out[3]=(uint8_t)n; }
static size_t sample(uint8_t *out) {
    const uint32_t words[]={0x444c4e31,1,0,0,2,2,2,0,16,0,0,65536,32,16,0,8388608,0,0,0,1000000,529,10000,30000,300000,32,0,0,0,0,0,0,1,1,1,2,3,0,UINT32_MAX,1,1,3,1,1,0,0,UINT32_MAX,0};
    memcpy(out,"DNS1",4);for(size_t i=0;i<sizeof words/sizeof *words;i++) put(out+4+i*4,words[i]);return 4+sizeof words;
}
int main(void) {
    uint8_t bytes[192],mutated[192];size_t size=sample(bytes);assert(size==sizeof bytes);
    dn_r_NetworkSpecCheck check=spec_check(bytes,size);assert(!check.f_status && check.f_minimum==24 && check.f_maximum==24 && check.f_pending_proofs==431);
    assert(!spec_roundtrip(bytes,size) && spec_short_output_preserved(bytes,size) && spec_inactive_tail_is_semantically_ignored(bytes,size));
    for(size_t i=0;i<size;i++) assert(spec_check(bytes,i).f_status==1);
    dn_r_NetworkAdmission admitted=spec_admit(bytes,size,63);assert(!admitted.f_status && admitted.f_choice.f_profile==3 && admitted.f_bounds.f_max_message==24);
    put(bytes+4+27*4,1);admitted=spec_admit(bytes,size,63);assert(!admitted.f_status && admitted.f_choice.f_profile==6 && (admitted.f_requirements.f_required&4));
    put(bytes+4+31*4,2);admitted=spec_admit(bytes,size,63);assert(admitted.f_status==6 && admitted.f_choice.f_status==6 && admitted.f_pending_proofs);
    sample(bytes);dn_r_NetworkNegotiation same=spec_negotiate(bytes,size,bytes,size,true,63);assert(!same.f_status && !same.f_minor && same.f_bounds.f_max_message==24);
    uint32_t random=141;for(unsigned n=0;n<5000;n++) {
        memcpy(mutated,bytes,size);random=random*1664525u+1013904223u;size_t position=random%size;random=random*1664525u+1013904223u;mutated[position]^=(uint8_t)(1u<<(random%8));
        check=spec_check(mutated,size);assert(check.f_status==0 || check.f_status==4 || check.f_status==7);if(!check.f_status) assert(!spec_roundtrip(mutated,size));
        assert(!dlm_native_last_error() && !dlm_native_live_allocations());
    }
    for(uint32_t mode=1;mode<=11;mode++) { check=spec_model_error(mode);assert(check.f_status==4 || check.f_status==7); }
    assert(!dlm_native_last_error() && !dlm_native_live_allocations());puts("NetworkSpec descriptor bounds mutation proof gates and ABI host OK");return 0;
}
