#define main dln_release_dependency_main
#include "dln_adaptive_host.c"
#undef main
#include "../../src/native/tun.c"

#define RELEASE_QUEUE 512u
#define RELEASE_PACKET 1400u
typedef struct { size_t size;uint64_t due;unsigned side;uint8_t bytes[RELEASE_PACKET]; } release_packet;
typedef struct {
    dnn_open sockets[2];dnn_addr peers[2];release_packet packets[RELEASE_QUEUE];
    uint64_t received,forwarded,dropped,duplicated;uint32_t random,loss,delay,jitter,auth_dropped;bool blackout,drop_probes,drop_first_auth;
} release_relay;
static bool release_quiet;

static uint32_t release_random(uint32_t *state) { uint32_t x=*state;x^=x<<13;x^=x>>17;x^=x<<5;*state=x;return x; }
static void release_wait(void) {
#if defined(_WIN32)
    Sleep(1);
#else
    struct timespec delay={0,1000000};nanosleep(&delay,NULL);
#endif
}
static release_relay *release_pair(dnn_open *a,dnn_open *b,uint32_t seed,unsigned loss,unsigned delay,unsigned jitter) {
    release_relay *r=calloc(1,sizeof *r);assert(r);r->random=seed?seed:1;r->loss=loss;r->delay=delay;r->jitter=jitter;
    dnn_open endpoints[2];for(unsigned i=0;i<2;i++) {
        endpoints[i]=dlm_net_bind_address(localhost(),3,0,false);r->sockets[i]=dlm_net_bind_address(localhost(),3,0,false);
        assert(!endpoints[i].status && !r->sockets[i].status);r->peers[i]=dlm_net_local(endpoints[i].handle).address;
    }
    dnt_bounds bounds={1200,32,16,8388608};
    *a=dlm_net_transport_adopt(endpoints[0],3,(dnt_requirements){0},bounds,dlm_net_local(r->sockets[0].handle).address,0,true);
    *b=dlm_net_transport_adopt(endpoints[1],3,(dnt_requirements){0},bounds,dlm_net_local(r->sockets[1].handle).address,0,false);
    assert(!a->status && !b->status);return r;
}
static void release_relay_close(release_relay *r) { dlm_net_close(r->sockets[0].handle);dlm_net_close(r->sockets[1].handle);free(r); }
static void release_schedule(release_relay *r,unsigned side,const uint8_t *bytes,size_t size,uint64_t now) {
    for(unsigned i=0;i<RELEASE_QUEUE;i++) if(!r->packets[i].size) {
        release_packet *p=&r->packets[i];p->size=size;p->side=side;p->due=now+r->delay+(r->jitter?release_random(&r->random)%(r->jitter+1):0);memcpy(p->bytes,bytes,size);return;
    }
    /* The impairment queue itself has a fixed memory bound. */
    r->dropped++;
}
static void release_relay_poll(release_relay *r) {
    uint64_t now=dlm_net_clock_ms();uint8_t bytes[RELEASE_PACKET];
    for(unsigned side=0;side<2;side++) for(unsigned work=0;work<128;work++) {
        dnn_datagram packet=dlm_net_udp_recv(r->sockets[side].handle,bytes,sizeof bytes);if(packet.io.status==DNN_WOULD_BLOCK) break;
        assert(!packet.io.status && packet.io.count && packet.io.count<=sizeof bytes);r->received++;
        unsigned kind=packet.io.count>=DNT_HEADER+DNA_HEADER?dnp_read32(bytes+DNT_HEADER+8):0;
        if(r->drop_first_auth && !r->auth_dropped && side==0 && kind==2) { r->auth_dropped++;r->dropped++;continue; }
        if(r->blackout || (r->drop_probes && (kind==5 || kind==6)) || release_random(&r->random)%100<r->loss) { r->dropped++;continue; }
        release_schedule(r,side,bytes,packet.io.count,now);
        if(release_random(&r->random)%20==0) { release_schedule(r,side,bytes,packet.io.count,now+1);r->duplicated++; }
    }
    /* Reverse traversal and jitter create reordering without changing ciphertext. */
    for(unsigned i=RELEASE_QUEUE;i>0;i--) {
        release_packet *p=&r->packets[i-1];if(!p->size || p->due>now) continue;
        if(r->blackout) { p->size=0;r->dropped++;continue; }
        dnn_io sent=dlm_net_udp_send(r->sockets[1-p->side].handle,r->peers[1-p->side],p->bytes,p->size);
        if(sent.status==DNN_WOULD_BLOCK) continue;assert(!sent.status && sent.count==p->size);p->size=0;r->forwarded++;
    }
}
static void release_tick(dnn_open a,dnn_open b,release_relay *r) { if(r) release_relay_poll(r);tick(a,b);if(r) release_relay_poll(r); }
static void release_establish(dnn_open a,dnn_open b,release_relay *r,unsigned paths) {
    uint64_t end=dlm_net_clock_ms()+10000;
    while(dlm_adaptive_stats(a.handle).authenticated<paths || dlm_adaptive_stats(b.handle).authenticated<paths) {
        release_tick(a,b,r);if(dlm_net_clock_ms()>=end) { for(unsigned side=0;side<2;side++) { dna_link *link=side?b.handle:a.handle;for(unsigned i=0;i<6;i++) if(link->paths[i].id) fprintf(stderr,"release handshake side=%u path=%u state=%u status=%u auth_sent=%u auth_received=%u\n",side,i,link->paths[i].state,link->paths[i].status,link->paths[i].auth_sent,link->paths[i].auth_received); }assert(0); }release_wait();
    }
}
static void release_probe_regressions(void) {
    dnn_open a,b,x,y;links(&a,&b,1);release_relay *r=release_pair(&x,&y,14601,0,65,15);
    dnn_io pa=dlm_adaptive_add(a.handle,x,1),pb=dlm_adaptive_add(b.handle,y,1);assert(!pa.status && !pb.status);release_establish(a,b,r,1);
    uint64_t end=dlm_net_clock_ms()+4000;
    while(dlm_adaptive_witness(a.handle,pa.count).samples<3 || dlm_adaptive_witness(b.handle,pb.count).samples<3) {
        release_tick(a,b,r);assert(dlm_net_clock_ms()<end);release_wait();
    }
    dna_observation witness=dlm_adaptive_witness(a.handle,pa.count);assert(witness.ready && witness.rtt_us>=120000 && witness.loss_ppm==0);
    printf("{\"case\":\"probe_rtt\",\"status\":\"PASS\",\"samples\":%llu,\"rtt_us\":%llu}\n",(unsigned long long)witness.samples,(unsigned long long)witness.rtt_us);
    dna_link *link=a.handle;dna_path *path=dna_find(link,pa.count);dnt_session *native=dnt_get(path->transport);
    path->ping_at=0;path->last_ping=0;uint64_t cookie=path->ping_cookie;uint64_t pings=path->pings;uint32_t queued=native->tx_count;
    dnt_node *head=native->tx;native->tx=NULL;native->tx_count=native->bounds.queued_messages;assert(!dna_path_poll(link,path,dlm_net_clock_ms()));
    assert(path->ping_cookie==cookie && path->pings==pings && !path->ping_at);native->tx=head;native->tx_count=queued;
    puts("{\"case\":\"probe_backpressure\",\"status\":\"PASS\"}");
    /* A not-yet-ready underlay must still obey the outer handshake deadline. */
    native->state=0;path->state=0;path->created=dlm_net_clock_ms();assert(dna_path_poll(link,path,path->created+link->config.handshake_ms)==DNN_TIMEOUT);
    puts("{\"case\":\"handshake_deadline\",\"status\":\"PASS\"}");
    dlm_adaptive_close(a.handle);dlm_adaptive_close(b.handle);release_relay_close(r);
}
static void release_blackhole(void) {
    dnn_open a,b,x,y;links(&a,&b,1);release_relay *r=release_pair(&x,&y,14602,0,0,0);r->drop_probes=true;
    dnn_io pa=dlm_adaptive_add(a.handle,x,1),pb=dlm_adaptive_add(b.handle,y,1);assert(!pa.status && !pb.status);release_establish(a,b,r,1);
    assert(!dlm_adaptive_witness(a.handle,pa.count).samples && !dlm_adaptive_witness(b.handle,pb.count).samples);
    raw_pair(&x,&y,false);dnn_io qa=dlm_adaptive_add(a.handle,x,2),qb=dlm_adaptive_add(b.handle,y,2);assert(!qa.status && !qb.status);release_establish(a,b,r,2);
    assert(!dlm_adaptive_promote(a.handle,pa.count).status && !dlm_adaptive_promote(b.handle,pb.count).status);
    ((dna_link*)a.handle)->config.policy.max_age_ms=200;((dna_link*)b.handle)->config.policy.max_age_ms=200;r->blackout=true;
    uint8_t data[4096],output[4097];for(unsigned i=0;i<sizeof data;i++) data[i]=(uint8_t)(i*37);
    dna_message_result sent=dlm_adaptive_send(a.handle,2,data,sizeof data);assert(!sent.status);uint64_t start=dlm_net_clock_ms(),end=start+5000;
    while(dlm_adaptive_peek(b.handle).status) { release_tick(a,b,r);assert(dlm_net_clock_ms()<end);release_wait(); }
    memset(output,165,sizeof output);dna_delivery received=dlm_adaptive_receive(b.handle,output,sizeof output);
    assert(!received.status && received.id==sent.id && received.copied==sizeof data && !memcmp(data,output,sizeof data) && output[sizeof data]==165);
    while(dlm_adaptive_stats(a.handle).queued) { release_tick(a,b,r);assert(dlm_net_clock_ms()<end);release_wait(); }
    assert(dlm_adaptive_stats(a.handle).active==qa.count && dlm_adaptive_stats(b.handle).active==qb.count);
    assert(dna_find(a.handle,pa.count)->status==DNN_TIMEOUT && dna_find(b.handle,pb.count)->status==DNN_TIMEOUT);
    printf("{\"case\":\"silent_path_failover\",\"status\":\"PASS\",\"failover_ms\":%llu,\"acked\":1}\n",(unsigned long long)(dlm_net_clock_ms()-start));
    dlm_adaptive_close(a.handle);dlm_adaptive_close(b.handle);release_relay_close(r);
}
static void release_auth_loss(void) {
    dnn_open a,b,x,y;links(&a,&b,1);release_relay *r=release_pair(&x,&y,14603,0,0,0);r->drop_first_auth=true;
    assert(!dlm_adaptive_add(a.handle,x,1).status && !dlm_adaptive_add(b.handle,y,1).status);release_establish(a,b,r,1);assert(r->auth_dropped==1);
    assert(!dlm_adaptive_send(a.handle,0,(const uint8_t*)"confirmed",9).status);uint64_t end=dlm_net_clock_ms()+3000;
    while(dlm_adaptive_peek(b.handle).status || dlm_adaptive_stats(a.handle).queued) { release_tick(a,b,r);assert(dlm_net_clock_ms()<end);release_wait(); }
    uint8_t output[10]={0};dna_delivery read=dlm_adaptive_receive(b.handle,output,sizeof output);assert(!read.status && read.copied==9 && !memcmp(output,"confirmed",9));
    puts("{\"case\":\"lost_authentication_confirmation\",\"status\":\"PASS\",\"dropped_auth\":1}");
    dlm_adaptive_close(a.handle);dlm_adaptive_close(b.handle);release_relay_close(r);
}
static size_t release_payload(uint8_t *bytes,uint32_t id) {
    size_t size=id%31==0?0:id%29==0?1:id%23==0?4095:4096;
    for(size_t i=0;i<size;i++) bytes[i]=(uint8_t)(id*17+(uint32_t)i*29+((uint32_t)i>>8));
    if(size>=4) dnp_write32(bytes,id);return size;
}
static void release_hex(const uint8_t *bytes,char *out) { const char digits[]="0123456789abcdef";for(unsigned i=0;i<32;i++) { out[2*i]=digits[bytes[i]>>4];out[2*i+1]=digits[bytes[i]&15]; }out[64]=0; }
static void release_transfer(uint32_t seed,unsigned loss,unsigned mode,unsigned messages) {
    assert(messages>=8 && messages<=4096);dnn_open a,b,x,y;links(&a,&b,mode);release_relay *r=release_pair(&x,&y,seed,loss,2,7);
    dnn_io pa=dlm_adaptive_add(a.handle,x,1),pb=dlm_adaptive_add(b.handle,y,1);assert(!pa.status && !pb.status);release_establish(a,b,r,1);
    raw_pair(&x,&y,false);dnn_io qa=dlm_adaptive_add(a.handle,x,2),qb=dlm_adaptive_add(b.handle,y,2);assert(!qa.status && !qb.status);release_establish(a,b,r,2);
    uint8_t *seen=calloc(messages,1);assert(seen);uint8_t data[4096],out[4097];uint32_t queued=0,delivered=0,transitions=0;uint64_t total=0,start=dlm_net_clock_ms(),end=start+60000;
    uint8_t wanted[32],got[32];uint8_t *payloads=calloc(messages,4096);assert(payloads);size_t length=0;
    /* Hash in logical message order, independently of unordered application reads. */
    for(uint32_t i=0;i<messages;i++) { size_t size=release_payload(data,i);memcpy(payloads+length,data,size);length+=size; }
    assert(!dlm_net_crypto_hash(NULL,payloads,length,wanted,32).status);memset(payloads,0,(size_t)messages*4096);
    while(delivered<messages || dlm_adaptive_stats(a.handle).queued) {
        while(queued<messages) { size_t size=release_payload(data,queued);dna_message_result sent=dlm_adaptive_send(a.handle,queued%4,data,size);
            if(sent.status==DNN_WOULD_BLOCK) break;assert(!sent.status && sent.id==(uint64_t)queued+1);queued++; }
        release_tick(a,b,r);
        for(;;) { memset(out,165,sizeof out);dna_delivery read=dlm_adaptive_receive(b.handle,out,sizeof out);if(read.status==DNN_WOULD_BLOCK) break;
            assert(!read.status && read.id>=1 && read.id<=messages);uint32_t id=(uint32_t)read.id-1;size_t size=release_payload(data,id);
            assert(!seen[id] && read.channel==id%4 && read.sequence==(uint64_t)id/4+1 && read.copied==size && !memcmp(data,out,size) && out[size]==165);
            seen[id]=1;if(size) memcpy(payloads+(size_t)id*4096,out,size);total+=size;delivered++;
        }
        if(transitions<3 && queued>=(uint64_t)messages*(transitions+1)/4) {
            bool tcp=!(transitions&1);assert(!dlm_adaptive_promote(a.handle,tcp?qa.count:pa.count).status && !dlm_adaptive_promote(b.handle,tcp?qb.count:pb.count).status);transitions++;
        }
        dna_stats left=dlm_adaptive_stats(a.handle),right=dlm_adaptive_stats(b.handle);
        assert(left.used<=left.limit && right.used<=right.limit && left.backend_used<=left.backend_limit && right.backend_used<=right.backend_limit);
        assert(dlm_net_clock_ms()<end);release_wait();
    }
    length=0;for(uint32_t i=0;i<messages;i++) { assert(seen[i]);size_t size=release_payload(data,i);memmove(payloads+length,payloads+(size_t)i*4096,size);length+=size; }
    assert(length==total && !dlm_net_crypto_hash(NULL,payloads,length,got,32).status && !memcmp(wanted,got,32));char digest[65];release_hex(got,digest);
    dna_stats stats=dlm_adaptive_stats(a.handle);assert(stats.acked==messages && transitions==3 && dlm_adaptive_peek(b.handle).status==DNN_WOULD_BLOCK);
    if(!release_quiet) printf("{\"case\":\"impaired_transfer\",\"status\":\"PASS\",\"seed\":%u,\"loss_percent\":%u,\"delivery\":%u,\"messages\":%u,\"bytes\":%llu,\"elapsed_ms\":%llu,\"sha256\":\"%s\",\"dropped\":%llu,\"duplicated\":%llu,\"retransmitted\":%llu,\"migrations\":%llu}\n",seed,loss,mode,messages,(unsigned long long)total,(unsigned long long)(dlm_net_clock_ms()-start),digest,(unsigned long long)r->dropped,(unsigned long long)r->duplicated,(unsigned long long)stats.retransmitted,(unsigned long long)stats.migrations);
    free(payloads);free(seen);dlm_adaptive_close(a.handle);dlm_adaptive_close(b.handle);release_relay_close(r);
}
static void release_fuzz(uint32_t seed,unsigned iterations) {
    dnn_open a,b;links(&a,&b,1);dna_link *link=b.handle;dna_path path={0};path.id=1;uint8_t bytes[1024];size_t initial=link->used;
    for(unsigned i=0;i<iterations;i++) {
        size_t count=release_random(&seed)%sizeof bytes;for(size_t j=0;j<count;j++) bytes[j]=(uint8_t)release_random(&seed);
        (void)dlm_tun_packet(1,bytes,count);(void)dlm_tun_packet(2,bytes,count);
        if(count>=32) {
            dnp_write64(bytes,1);dnp_write64(bytes+8,1);dnp_write32(bytes+16,0);dnp_write32(bytes+20,(uint32_t)count-32);dnp_write32(bytes+24,0);dnp_write32(bytes+28,(uint32_t)count-32);
            switch(i%5) { case 0:dnp_write32(bytes+16,link->config.bounds.channels);break;case 1:dnp_write32(bytes+24,UINT32_MAX);break;case 2:dnp_write32(bytes+28,UINT32_MAX);break;case 3:dnp_write32(bytes+20,UINT32_MAX);break;default:dnp_write64(bytes+8,0);break; }
            assert(dna_data(link,&path,bytes,count)==DNN_PROTOCOL && link->used==initial && !link->rx_count);
        }
        dnp_record_result record=dnp_record_decode(bytes,count,dlm_node_wall_seconds());assert(record.status);
    }
    printf("{\"case\":\"hostile_input\",\"status\":\"PASS\",\"iterations\":%u,\"used\":%llu}\n",iterations,(unsigned long long)link->used);
    dlm_adaptive_close(a.handle);dlm_adaptive_close(b.handle);
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);assert(argc>=2);
    if(!strcmp(argv[1],"probes")) release_probe_regressions();
    else if(!strcmp(argv[1],"blackhole")) release_blackhole();
    else if(!strcmp(argv[1],"auth-loss")) release_auth_loss();
    else if(!strcmp(argv[1],"transfer")) { assert(argc==6);unsigned loss=(unsigned)strtoul(argv[3],NULL,10),mode=(unsigned)strtoul(argv[4],NULL,10);assert(loss<=10 && (mode==1 || mode==2));release_transfer((uint32_t)strtoul(argv[2],NULL,10),loss,mode,(unsigned)strtoul(argv[5],NULL,10)); }
    else if(!strcmp(argv[1],"fuzz")) release_fuzz(14617,20000);
    else if(!strcmp(argv[1],"soak")) {
        assert(argc==3);unsigned seconds=(unsigned)strtoul(argv[2],NULL,10);assert(seconds && seconds<=259200);uint64_t start=dlm_net_clock_ms();unsigned cycles=0;release_quiet=true;
        do { release_transfer(14700+cycles,10,cycles%2+1,128);cycles++;assert(!atomic_load(&dnc_live) && !atomic_load(&dnn_live)); }while(dlm_net_clock_ms()-start<(uint64_t)seconds*1000);
        printf("{\"case\":\"reconnect_soak\",\"status\":\"PASS\",\"requested_seconds\":%u,\"elapsed_ms\":%llu,\"cycles\":%u,\"single_process\":true,\"live_network_owners\":0,\"live_crypto_owners\":0}\n",seconds,(unsigned long long)(dlm_net_clock_ms()-start),cycles);
    }
    else assert(0);
    assert(!atomic_load(&dnc_live) && !atomic_load(&dnn_live));assert(!dlm_net_quic_shutdown().status && !dlm_net_crypto_shutdown().status && !dlm_net_provider_shutdown().status);return 0;
}
