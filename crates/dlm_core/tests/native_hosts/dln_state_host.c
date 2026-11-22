#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "dln_state.a.h"
static void clean(void) { assert(!dlm_native_last_error());assert(!dlm_native_live_allocations()); }
int main(int argc,char **argv) {
    assert(state_tcp_contracts()==255);clean();assert(state_invalid_constructor()==4);clean();assert(state_message_limit()==1);clean();assert(state_cancel_timeout(0)==9);clean();assert(state_cancel_timeout(2)==3);clean();assert(state_security_refusal(3)==6 && state_security_refusal(4)==6);clean();assert(state_eof_or_malformed(false)==1);clean();assert(state_eof_or_malformed(true)==2);clean();
    if(argc==2 && !strcmp(argv[1],"--quic")) { for(uint32_t profile=4;profile<=6;profile++) { assert(state_quic(profile)==profile);clean(); } }
    assert(!state_model_error(0).f_status);for(uint32_t mode=1;mode<=10;mode++) { assert(state_model_error(mode).f_status);clean(); }
    for(uint32_t role=1;role<=3;role++) for(uint32_t state=0;state<10;state++) for(uint32_t event=0;event<14;event++) for(uint32_t message=0;message<6;message++) {
        dn_r_ProtocolStep step=state_step(state,event,message,role);assert(step.f_status==0 || step.f_status==4);if(!step.f_status) assert(step.f_state>=1 && step.f_state<=7);clean();
    }
    assert(state_clock(0,0,0,999,1)==0 && state_clock(0,0,0,1000,1)==3 && state_clock(0,0,0,2000,3)==3 && state_clock(0,4500,4500,5000,3)==3);assert(state_clock(0,1,2,1,3)==4);assert(state_clock(UINT64_MAX-50,UINT64_MAX-50,UINT64_MAX-50,UINT64_MAX,3)==0);assert(state_counter(0,0,UINT64_MAX) && !state_counter(UINT64_MAX,0,UINT64_MAX) && !state_counter(0,UINT64_MAX,UINT64_MAX) && !state_counter(UINT64_MAX,UINT64_MAX,1));clean();
    for(unsigned index=0;index<16;index++) { state_abort_cleanup();assert(dlm_native_last_error() && !dlm_native_live_allocations());dlm_native_clear_error(); }clean();puts("protocol state transitions authentication quotas timeout EOF and cleanup host OK");return 0;
}
