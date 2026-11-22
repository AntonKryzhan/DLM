/* Public test fixtures are passed explicitly; no production credentials. */
#include <assert.h>
#include "../../src/native/network.c"
static dq_addr loopback(void) { dq_addr a={0};a.family=4;a.bytes[0]=127;a.bytes[3]=1;return a; }
static dq_text text(const char *s) { dq_text t={(const uint8_t*)s,strlen(s)};return t; }
static dq_config config(void) { dq_config c={2,16,128,5000,2000,2,4194304,16384};return c; }
static uint64_t consume(void *endpoint,size_t *data,size_t *datagrams) {
    uint64_t connected=0;dq_event e;
    while((e=dlm_net_quic_next(endpoint)).status==0) {
        if(e.kind==1) connected=e.connection;
        if(e.kind==3 || e.kind==5) {
            uint8_t bytes[16];memset(bytes,0xa5,sizeof bytes);dq_io r=dlm_net_quic_copy(endpoint,e.id,0,bytes,sizeof bytes);
            assert(r.status==0 && r.count==e.count && r.count<sizeof bytes && bytes[r.count]==0xa5);
            if(e.kind==3) { assert(r.count==4 && bytes[0]==0 && bytes[1]==255 && bytes[2]==128 && bytes[3]==42 && (e.flags&1));(*data)++; }
            else { assert(r.count==3 && bytes[0]==0 && bytes[1]==255 && bytes[2]==128);(*datagrams)++; }
        }
        assert(dlm_net_quic_ack(endpoint,e.id).status==0);
    }assert(e.status==DNN_WOULD_BLOCK);return connected;
}
static void tick(void *a,void *b) { dq_io x=dlm_net_quic_poll(a,1,NULL),y=dlm_net_quic_poll(b,1,NULL);assert(x.status<=DNN_TIMEOUT && y.status<=DNN_TIMEOUT); }
static void test_exchange(const char *ca,const char *cert,const char *key) {
    dq_open server=dlm_net_quic_open(loopback(),true,text("dlm-host/1"),text(cert),text(key),config());assert(!server.status);
    dq_open client=dlm_net_quic_open(loopback(),false,text("dlm-host/1"),text(ca),text(""),config());assert(!client.status);
    assert(dlm_net_quic_shutdown().status==DNN_INVALID);dq_ref ref=dlm_net_quic_connect(client.handle,dlm_net_quic_local(server.handle).address,text("localhost"));assert(!ref.status);
    size_t data=0,datagrams=0;uint64_t ci=0,si=0,deadline=dlm_net_clock_ms()+5000;
    while(!ci || !si) { assert(dlm_net_clock_ms()<deadline);tick(client.handle,server.handle);uint64_t c=consume(client.handle,&data,&datagrams),s=consume(server.handle,&data,&datagrams);if(c) ci=c;if(s) si=s; }
    assert(ci==ref.connection);dq_stats initial=dlm_net_quic_stats(client.handle,ci);assert(initial.state==2 && initial.used<=initial.limit);
    dq_ref stream=dlm_net_quic_stream(client.handle,ci,false);assert(!stream.status);uint8_t bytes[]={0,255,128,42};
    assert(dlm_net_quic_write(client.handle,ci,stream.stream,bytes,4,true).status==0);assert(dlm_net_quic_datagram(client.handle,ci,bytes,3).status==0);
    assert(dlm_net_quic_write(server.handle,ci,stream.stream,bytes,4,true).status==DNN_INVALID);
    while(!data || !datagrams) { assert(dlm_net_clock_ms()<deadline);tick(client.handle,server.handle);(void)consume(client.handle,&data,&datagrams);(void)consume(server.handle,&data,&datagrams); }
    assert(data==1 && datagrams==1);assert(dlm_net_quic_disconnect(client.handle,ci,0).status==0);
    while(dlm_net_quic_stats(client.handle,ci).state!=5 || dlm_net_quic_stats(server.handle,si).state!=5) {
        assert(dlm_net_clock_ms()<deadline);tick(client.handle,server.handle);(void)consume(client.handle,&data,&datagrams);(void)consume(server.handle,&data,&datagrams);
    }
    assert(dlm_net_quic_forget(client.handle,ci).status==0 && dlm_net_quic_forget(server.handle,si).status==0);
    dq_stats empty=dlm_net_quic_stats(client.handle,0);assert(empty.connections==0 && empty.used<initial.used);dlm_net_quic_close(client.handle);dlm_net_quic_close(server.handle);
}
int main(int argc,char **argv) {
    if(argc!=4) { fprintf(stderr,"Usage: quic-host CA.pem certificate.pem key.pem\n");return 2; }
    if(dlm_net_quic_version()!=0x011a00) { fprintf(stderr,"QUIC provider unavailable. Rebuild DLM 0.139 and check DLM_LIBQUIC / bin/dlm-quic-1.dll.\n");return 1; }
    for(unsigned i=0;i<12;i++) test_exchange(argv[1],argv[2],argv[3]);assert(dlm_net_quic_shutdown().status==0 && dlm_net_provider_shutdown().status==0);
    puts("QUIC TLS streams datagrams bounded lifecycle and cleanup OK");return 0;
}
