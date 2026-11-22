#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "dln_adaptive.a.h"
typedef struct { size_t count;int32_t status,system; } host_io;
extern host_io dlm_net_crypto_shutdown(void);
extern host_io dlm_net_provider_shutdown(void);
static void clean(void) { const char *error=dlm_native_last_error();if(error) fprintf(stderr,"DLM error: %s\n",error);assert(!error && !dlm_native_live_allocations()); }
#define FIELD(type,field) assert(dlm_native_offset_##type##_##field()==offsetof(dn_r_##type,f_##field))
static bool same_adaptive_config(const dn_r_AdaptiveConfig *a,const dn_r_AdaptiveConfig *b) {
    return a->f_bounds.f_max_message==b->f_bounds.f_max_message && a->f_bounds.f_queued_messages==b->f_bounds.f_queued_messages && a->f_bounds.f_channels==b->f_bounds.f_channels && a->f_bounds.f_memory==b->f_bounds.f_memory &&
        a->f_requirements.f_required==b->f_requirements.f_required && a->f_requirements.f_forbidden==b->f_requirements.f_forbidden && a->f_requirements.f_preferred==b->f_requirements.f_preferred &&
        a->f_policy.f_delivery==b->f_policy.f_delivery && a->f_policy.f_latency_weight==b->f_policy.f_latency_weight && a->f_policy.f_loss_weight==b->f_policy.f_loss_weight && a->f_policy.f_throughput_weight==b->f_policy.f_throughput_weight && a->f_policy.f_cost_weight==b->f_policy.f_cost_weight &&
        a->f_policy.f_max_age_ms==b->f_policy.f_max_age_ms && a->f_policy.f_min_samples==b->f_policy.f_min_samples && a->f_policy.f_min_hold_ms==b->f_policy.f_min_hold_ms && a->f_policy.f_hysteresis_percent==b->f_policy.f_hysteresis_percent &&
        a->f_handshake_ms==b->f_handshake_ms && a->f_idle_ms==b->f_idle_ms && a->f_duration_ms==b->f_duration_ms && a->f_probe_ms==b->f_probe_ms && a->f_automatic==b->f_automatic;
}
static bool same_address(const dn_r_NetAddress *a,const dn_r_NetAddress *b) {
    return a->f_family==b->f_family && a->f_port==b->f_port && a->f_scope==b->f_scope && !memcmp(a->f_bytes.data,b->f_bytes.data,sizeof a->f_bytes.data);
}
static bool same_tun_config(const dn_r_TunConfig *a,const dn_r_TunConfig *b) {
    return a->f_kind==b->f_kind && a->f_mode==b->f_mode && a->f_mtu==b->f_mtu && a->f_max_packet==b->f_max_packet && a->f_ring_capacity==b->f_ring_capacity && a->f_memory==b->f_memory && a->f_configure==b->f_configure &&
        same_address(&a->f_ipv4,&b->f_ipv4) && same_address(&a->f_ipv6,&b->f_ipv6) && a->f_ipv4_prefix==b->f_ipv4_prefix && a->f_ipv6_prefix==b->f_ipv6_prefix;
}
static void config_abi(void) {
    assert(dlm_native_sizeof_AdaptiveConfig()==sizeof(dn_r_AdaptiveConfig) && dlm_native_alignof_AdaptiveConfig()==_Alignof(dn_r_AdaptiveConfig));
    assert(dlm_native_sizeof_TunConfig()==sizeof(dn_r_TunConfig) && dlm_native_alignof_TunConfig()==_Alignof(dn_r_TunConfig));
    FIELD(TransportBounds,max_message);FIELD(TransportBounds,queued_messages);FIELD(TransportBounds,channels);FIELD(TransportBounds,memory);
    FIELD(TransportRequirements,required);FIELD(TransportRequirements,forbidden);FIELD(TransportRequirements,preferred);
    FIELD(OptimizerPolicy,delivery);FIELD(OptimizerPolicy,latency_weight);FIELD(OptimizerPolicy,loss_weight);FIELD(OptimizerPolicy,throughput_weight);FIELD(OptimizerPolicy,cost_weight);FIELD(OptimizerPolicy,max_age_ms);FIELD(OptimizerPolicy,min_samples);FIELD(OptimizerPolicy,min_hold_ms);FIELD(OptimizerPolicy,hysteresis_percent);
    FIELD(AdaptiveConfig,bounds);FIELD(AdaptiveConfig,requirements);FIELD(AdaptiveConfig,policy);FIELD(AdaptiveConfig,handshake_ms);FIELD(AdaptiveConfig,idle_ms);FIELD(AdaptiveConfig,duration_ms);FIELD(AdaptiveConfig,probe_ms);FIELD(AdaptiveConfig,automatic);
    FIELD(NetAddress,family);FIELD(NetAddress,port);FIELD(NetAddress,scope);FIELD(NetAddress,bytes);
    FIELD(TunConfig,kind);FIELD(TunConfig,mode);FIELD(TunConfig,mtu);FIELD(TunConfig,max_packet);FIELD(TunConfig,ring_capacity);FIELD(TunConfig,memory);FIELD(TunConfig,configure);FIELD(TunConfig,ipv4);FIELD(TunConfig,ipv6);FIELD(TunConfig,ipv4_prefix);FIELD(TunConfig,ipv6_prefix);
    dn_r_AdaptiveConfig config={0};config.f_bounds=(dn_r_TransportBounds){65571,17,23,SIZE_MAX-123};config.f_requirements=(dn_r_TransportRequirements){21,64,8};config.f_policy=(dn_r_OptimizerPolicy){1,1001,2003,3007,4009,5011,6013,7019,83};config.f_handshake_ms=12347;config.f_idle_ms=23459;config.f_duration_ms=34567;config.f_probe_ms=457;
    dn_r_TunConfig tun={0};tun.f_kind=1;tun.f_mode=2;tun.f_mtu=1500;tun.f_max_packet=65535;tun.f_ring_capacity=4194304;tun.f_memory=SIZE_MAX-321;tun.f_ipv4.f_family=4;tun.f_ipv4.f_port=2347;tun.f_ipv4.f_scope=3457;tun.f_ipv6.f_family=6;tun.f_ipv6.f_port=4567;tun.f_ipv6.f_scope=5671;tun.f_ipv4_prefix=24;tun.f_ipv6_prefix=64;
    for(unsigned i=0;i<16;i++) { tun.f_ipv4.f_bytes.data[i]=(uint8_t)(i*17+1);tun.f_ipv6.f_bytes.data[i]=(uint8_t)(255-i*13); }
    for(unsigned flag=0;flag<2;flag++) {
        config.f_automatic=flag!=0;dn_r_AdaptiveConfig echoed=adaptive_config_abi(config);assert(same_adaptive_config(&config,&echoed));
        tun.f_configure=flag!=0;dn_r_TunConfig restored=tun_config_abi(tun);assert(same_tun_config(&tun,&restored));
        size_t end=offsetof(dn_r_AdaptiveConfig,f_automatic)+sizeof config.f_automatic;if(end<sizeof config) { memset((unsigned char*)&config+end,0xa5,sizeof config-end);memset((unsigned char*)&echoed+end,0x5a,sizeof echoed-end);assert(((unsigned char*)&config)[end]!=((unsigned char*)&echoed)[end] && same_adaptive_config(&config,&echoed)); }
        end=offsetof(dn_r_TunConfig,f_ipv6_prefix)+sizeof tun.f_ipv6_prefix;if(end<sizeof tun) { memset((unsigned char*)&tun+end,0xa5,sizeof tun-end);memset((unsigned char*)&restored+end,0x5a,sizeof restored-end);assert(((unsigned char*)&tun)[end]!=((unsigned char*)&restored)[end] && same_tun_config(&tun,&restored)); }
    }
    clean();
}
int main(int argc,char **argv) {
    config_abi();
    unsigned result=adaptive_dlm_roundtrip();if(result!=1) fprintf(stderr,"adaptive result %u: %s\n",result,dlm_native_last_error());assert(result==1);clean();result=verified_dlm_contracts();if(result!=2) fprintf(stderr,"verified result %u: %s\n",result,dlm_native_last_error());assert(result==2);clean();assert(verified_cross_session_replay_is_refused()==6);clean();assert(tun_dlm_invalid_authority()==4);clean();
    dn_r_TransportRequirements requirements={0};requirements.f_required=1|4|16;dn_r_OptimizerPolicy policy={0};policy.f_delivery=1;policy.f_latency_weight=1000;policy.f_cost_weight=1;policy.f_max_age_ms=1000;policy.f_min_samples=1;dn_t_arr6_r14_RuntimeWitness observations={0};for(unsigned i=0;i<6;i++) { dn_r_RuntimeWitness *o=&observations.data[i];o->f_id=i+1;o->f_at_ms=100;o->f_rtt_us=1000;o->f_goodput_bps=1000000;o->f_samples=4;o->f_profile=i+1;const unsigned properties[]={183,183,151,191,191,183};o->f_properties=properties[i];o->f_cost=6-i;o->f_ready=true; }
    dn_r_OptimizerChoice choice=optimizer_dlm(requirements,policy,observations,100);assert(!choice.f_status && choice.f_id==6);dn_t_arr456_U8 certificate=optimizer_dot(requirements,policy,observations,100);assert(!memcmp(certificate.data,"DOT1",4));if(argc>1) { FILE *file=fopen(argv[1],"wb");assert(file && fwrite(certificate.data,1,sizeof certificate.data,file)==sizeof certificate.data);assert(!fclose(file)); }clean();assert(!dlm_net_crypto_shutdown().status && !dlm_net_provider_shutdown().status);assert(dlm_native_runtime_shutdown());puts("DLM adaptive migration identity-gated protocol optimizer certificate TUN authority and ABI OK");return 0;
}
