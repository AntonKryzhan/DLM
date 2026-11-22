/* Fault injection and bounded owner lifetime, without a Python dependency. */
#include <assert.h>
#include "../../src/native/network.c"
static dnn_addr loopback(void) { dnn_addr a={0};a.family=4;a.bytes[0]=127;a.bytes[3]=1;return a; }
static dnt_bounds bounds(void) { dnt_bounds b={1024,2,4,8388608};return b; }
static dnn_open pair(dnn_open *raw) {
    dnn_open listener=dlm_net_bind_address(loopback(),DNN_LISTENER,8,false);assert(!listener.status);
    dnn_resolved address=dlm_net_local(listener.handle);assert(!address.status);*raw=dlm_net_connect_address(address.address,1000);assert(!raw->status);
    assert(!dlm_net_wait(listener.handle,1,1000,NULL).status);dnn_open accepted=dlm_net_accept(listener.handle);assert(!accepted.status);dlm_net_close(listener.handle);
    dnn_open session=dlm_net_transport_adopt(accepted,1,(dnt_requirements){19,0,0},bounds(),(dnn_addr){0},0,false);assert(!session.status);return session;
}
static void ready(void *session) {
    uint64_t deadline=dlm_net_clock_ms()+2000;while(dlm_net_transport_peek(session).status==DNN_WOULD_BLOCK) { assert(dlm_net_clock_ms()<deadline);dnn_io r=dlm_net_transport_poll(session,1,NULL);assert(r.status<=DNN_TIMEOUT); }
}
static void framing(void) {
    dnn_open raw,session=pair(&raw);uint8_t wire[36];dnt_header(wire,0,4);wire[16]=0;wire[17]=255;wire[18]=128;wire[19]=42;dnt_header(wire+20,0,0);
    for(size_t i=0;i<sizeof wire;i++) { dnn_io sent=dlm_net_transfer(raw.handle,wire+i,1,1000,NULL,true);assert(!sent.status && sent.count==1);dnn_io r=dlm_net_transport_poll(session.handle,0,NULL);assert(r.status<=DNN_TIMEOUT); }
    ready(session.handle);dnt_message event=dlm_net_transport_peek(session.handle);assert(!event.status && event.count==4);
    uint8_t out[8];memset(out,0xa5,sizeof out);dnt_delivery r=dlm_net_transport_receive(session.handle,out,3);assert(r.status==DNN_TRUNCATED && !r.copied && r.required==4 && out[0]==0xa5 && out[7]==0xa5);
    assert(dlm_net_transport_peek(session.handle).id==event.id);r=dlm_net_transport_receive(session.handle,out,sizeof out);assert(!r.status && r.copied==4 && out[0]==0 && out[1]==255 && out[2]==128 && out[3]==42 && out[4]==0xa5);
    ready(session.handle);r=dlm_net_transport_receive(session.handle,out,0);assert(!r.status && !r.copied);
    void *cancel=dlm_net_cancel_new();assert(cancel);dlm_net_cancel_signal(cancel);assert(dlm_net_transport_poll(session.handle,1000,cancel).status==DNN_CANCELLED);dlm_net_cancel_close(cancel);
    assert(dlm_net_transport_poll(session.handle,10,NULL).status==DNN_TIMEOUT);assert(!dlm_net_shutdown(raw.handle).status);assert(dlm_net_transport_poll(session.handle,1000,NULL).status==DNN_EOF);
    dlm_net_transport_close(session.handle);dlm_net_close(raw.handle);
}
static void faults(void) {
    for(unsigned mode=0;mode<5;mode++) {
        dnn_open raw,session=pair(&raw);uint8_t wire[20];dnt_header(wire,0,4);size_t count=20;int expected=DNN_PROTOCOL;
        if(mode==0) wire[0]='?';if(mode==1) wire[15]=1;if(mode==2) wire[11]=1;
        if(mode==3) { dnt_write32(wire+4,1025);expected=DNN_LIMIT; }if(mode==4) { count=19;expected=DNN_TRUNCATED; }
        memset(wire+16,0,4);assert(!dlm_net_transfer(raw.handle,wire,count,1000,NULL,true).status);assert(!dlm_net_shutdown(raw.handle).status);
        uint64_t deadline=dlm_net_clock_ms()+1000;dnn_io r;
        do { r=dlm_net_transport_poll(session.handle,1,NULL);assert(dlm_net_clock_ms()<deadline); }while(r.status<=DNN_TIMEOUT);
        assert(r.status==expected && dlm_net_transport_poll(session.handle,0,NULL).status==expected);dlm_net_transport_close(session.handle);dlm_net_close(raw.handle);
    }
}
static void quota_and_consumption(void) {
    dnn_open raw=dlm_net_bind_address(loopback(),DNN_UDP,0,false);assert(!raw.status);dnt_bounds b={64,4096,1,1664};
    dnn_addr peer=loopback();peer.port=9;dnn_open session=dlm_net_transport_adopt(raw,3,(dnt_requirements){16,0,0},b,peer,0,true);assert(!session.status);
    uint8_t bytes[64]={0};unsigned queued=0;for(;;) { dnn_io r=dlm_net_transport_send(session.handle,0,bytes,sizeof bytes);if(r.status==DNN_WOULD_BLOCK) break;assert(!r.status);queued++;assert(queued<4096); }
    dnt_stats stats=dlm_net_transport_stats(session.handle);assert(queued && stats.used<=b.memory && stats.send_queued==queued);dlm_net_transport_close(session.handle);
    raw=dlm_net_bind_address(loopback(),DNN_UDP,0,false);assert(!raw.status);session=dlm_net_transport_adopt(raw,3,(dnt_requirements){1,0,0},bounds(),peer,0,true);assert(session.status==DNN_UNSUPPORTED && !session.handle);
    dnt_target target={0};target.peer=peer;session=dlm_net_transport_open(1,(dnt_requirements){4,0,0},bounds(),target,1000,NULL);assert(session.status==DNN_UNSUPPORTED && !session.handle);
}
int main(void) {
    for(unsigned i=0;i<24;i++) { framing();faults();quota_and_consumption();assert(!atomic_load(&dnn_live)); }
    assert(!dlm_net_provider_shutdown().status);puts("transport framing faults quotas cancellation and consumed owners OK");return 0;
}
