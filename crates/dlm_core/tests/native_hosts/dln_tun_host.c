#include <assert.h>
#include "../../src/native/edge_headers.h"
#include "../../src/native/network.c"
#include "../../src/native/nodes.c"
#include "../../src/native/adaptive.c"
#include "../../src/native/tun.c"
#define main dln_adaptive_dependency_main
#include "dln_adaptive_host.c"
#undef main
static dnu_config configuration(unsigned kind) { dnu_config c={0};c.kind=kind;c.mode=1;c.mtu=1500;c.max_packet=65571;c.memory=1048576;c.ring_capacity=131072;return c; }
static void validators(void) {
    uint8_t ip[64]={0};ip[0]=0x45;ip[3]=20;dnu_packet p=dlm_tun_packet(1,ip,20);assert(!p.status && p.family==4);ip[3]=21;assert(dlm_tun_packet(1,ip,20).status==DNN_PROTOCOL);memset(ip,0,sizeof ip);ip[0]=0x60;p=dlm_tun_packet(1,ip,40);assert(!p.status && p.family==6);ip[5]=1;assert(dlm_tun_packet(1,ip,40).status==DNN_PROTOCOL);
    memset(ip,0,sizeof ip);ip[12]=8;ip[14]=0x45;ip[17]=20;p=dlm_tun_packet(2,ip,60);assert(!p.status && p.family==4 && p.offset==14);ip[12]=0x81;ip[13]=0;ip[16]=0x88;ip[17]=0xa8;ip[20]=8;ip[21]=0;ip[22]=0x45;ip[25]=20;p=dlm_tun_packet(2,ip,64);assert(!p.status && p.offset==22 && p.family==4);assert(dlm_tun_packet(1,NULL,0).status==DNN_INVALID);
    dnn_open permission=dlm_tun_permission(3);assert(!permission.status);dnn_text invalid={(const uint8_t*)"../bad",6};dnn_open opened=dlm_tun_open(permission,invalid,(dnn_text){0},configuration(1));
assert(opened.status==DNN_INVALID);
}
#if !defined(_WIN32)
static void bridge_roundtrip(void) {
    dnn_open a,b,x,y;links(&a,&b,1);raw_pair(&x,&y,true);assert(!dlm_adaptive_add(a.handle,x,1).status && !dlm_adaptive_add(b.handle,y,1).status);int fa[2],fb[2];assert(!socketpair(AF_UNIX,SOCK_DGRAM,0,fa) && !socketpair(AF_UNIX,SOCK_DGRAM,0,fb));dnu_config cfg=configuration(1);cfg.max_packet=4096;dnn_open da=dlm_tun_adopt(fa[0],cfg),db=dlm_tun_adopt(fb[0],cfg);assert(!da.status && !db.status);dnn_open ba=dlm_tun_bridge_new(da,a,1),bb=dlm_tun_bridge_new(db,b,1);assert(!ba.status && !bb.status);uint8_t ip[64]={0},out[64];ip[0]=0x45;ip[3]=20;assert(write(fa[1],ip,20)==20);dnn_io first=dlm_tun_bridge_poll(ba.handle,0,NULL);assert(first.status==DNN_WOULD_BLOCK && dlm_tun_stats(((dnu_bridge*)ba.handle)->device).received==0);uint64_t end=dlm_net_clock_ms()+3000;ssize_t count=-1;
    while(count<0) { dnn_io left=dlm_tun_bridge_poll(ba.handle,0,NULL),right=dlm_tun_bridge_poll(bb.handle,0,NULL);assert((!left.status || left.status==DNN_WOULD_BLOCK) && (!right.status || right.status==DNN_WOULD_BLOCK));count=recv(fb[1],out,sizeof out,MSG_DONTWAIT);assert(dlm_net_clock_ms()<end); }assert(count==20 && !memcmp(ip,out,20));ip[3]=21;assert(write(fa[1],ip,20)==20);for(unsigned i=0;i<50;i++) { assert(!dlm_tun_bridge_poll(ba.handle,0,NULL).status);assert(!dlm_tun_bridge_poll(bb.handle,0,NULL).status); }assert(dlm_tun_bridge_stats(ba.handle).rejected==1 && recv(fb[1],out,sizeof out,MSG_DONTWAIT)<0);dnu_bridge_stats stats=dlm_tun_bridge_stats(bb.handle);assert(stats.incoming==1 && stats.authenticated==1);dlm_tun_bridge_close(ba.handle);dlm_tun_bridge_close(bb.handle);close(fa[1]);close(fb[1]);assert(!atomic_load(&dnc_live) && !atomic_load(&dnn_live));
}
static void retained_packets_and_consumed_fds(void) {
    int fds[2];assert(!socketpair(AF_UNIX,SOCK_DGRAM,0,fds));dnn_open d=dlm_tun_adopt(fds[0],configuration(1));assert(!d.status);uint8_t packet[64]={0},out[65];packet[0]=0x45;packet[3]=20;assert(write(fds[1],packet,20)==20);assert(!dlm_tun_wait(d.handle,1000,NULL).status);dnu_packet peek=dlm_tun_peek(d.handle);assert(!peek.status && peek.required==20);memset(out,0xa5,sizeof out);dnu_packet read=dlm_tun_receive(d.handle,out,19);assert(read.status==DNN_TRUNCATED && out[0]==0xa5 && dlm_tun_peek(d.handle).required==20);read=dlm_tun_receive(d.handle,out,sizeof out);assert(!read.status && read.copied==20 && !memcmp(out,packet,20) && out[20]==0xa5);assert(!dlm_tun_send(d.handle,packet,20).status && recv(fds[1],out,sizeof out,0)==20 && !memcmp(out,packet,20));packet[3]=21;assert(write(fds[1],packet,20)==20 && dlm_tun_receive(d.handle,out,sizeof out).status==DNN_PROTOCOL);assert(!dlm_tun_discard(d.handle).status);assert(dlm_tun_wait(d.handle,1,NULL).status==DNN_TIMEOUT);void *cancel=dlm_net_cancel_new();dlm_net_cancel_signal(cancel);assert(dlm_tun_wait(d.handle,100,cancel).status==DNN_CANCELLED);dlm_net_cancel_close(cancel);dnu_stats stats=dlm_tun_stats(d.handle);assert(stats.backend==4 && stats.received==2 && stats.sent==1 && stats.dropped==1 && stats.used<=stats.limit);dlm_tun_close(d.handle);assert(fcntl(fds[0],F_GETFD)==-1 && errno==EBADF);close(fds[1]);
    assert(!socketpair(AF_UNIX,SOCK_STREAM,0,fds));d=dlm_tun_adopt(fds[0],configuration(1));assert(d.status==DNN_INVALID && fcntl(fds[0],F_GETFD)==-1);close(fds[1]);
}
#endif
int main(void) { validators();
#if !defined(_WIN32)
    retained_packets_and_consumed_fds();bridge_roundtrip();
#endif
    assert(!dlm_net_crypto_shutdown().status && !dlm_net_provider_shutdown().status);puts("TUN TAP IP VLAN validation retained buffers authority and packet-fd ownership OK");return 0; }
