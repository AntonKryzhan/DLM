/* General QUIC v1/v2: ngtcp2 owns transport, GnuTLS owns TLS 1.3 and peer
 * verification. No session tickets or 0-RTT. One driver per endpoint; only
 * cancellation is cross-thread. Application/ngtcp2 allocations share a budget;
 * GnuTLS and kernel internals are outside that budget. */
#include "quic_api.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_gnutls.h>
#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET qfd;
typedef int qlen;
#define Q_BAD INVALID_SOCKET
#define q_close closesocket
#define q_errno() WSAGetLastError()
#define Q_AGAIN(x) ((x)==WSAEWOULDBLOCK)
#define Q_INTR(x) ((x)==WSAEINTR)
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
typedef int qfd;
typedef socklen_t qlen;
#define Q_BAD (-1)
#define q_close close
#define q_errno() errno
#define Q_AGAIN(x) ((x)==EAGAIN || (x)==EWOULDBLOCK)
#define Q_INTR(x) ((x)==EINTR)
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
enum { Q_OK,Q_AGAIN_STATUS,Q_EOF,Q_TIMEOUT,Q_INVALID,Q_SYSTEM,Q_UNSUPPORTED,Q_LIMIT,Q_TLS,Q_CANCELLED,Q_TRUNCATED,Q_PROTOCOL };
enum { Q_HANDSHAKE=1,Q_READY,Q_CLOSING,Q_DRAINING,Q_CLOSED };
enum { Q_CONNECTED=1,Q_STREAM,Q_DATA,Q_RESET,Q_DATAGRAM,Q_DISCONNECTED,Q_PATH,Q_STREAM_CLOSED };
#define Q_MAGIC UINT64_C(0x444c4d5155494331)
#define Q_PACKET 1500
#define Q_CID 8
#define Q_MAX_DGRAM 1100
#define Q_APP_MAX UINT64_C(0x3fffffffffffffff)
typedef struct qe qe;
typedef struct qc qc;
typedef struct qs qs;
typedef struct chunk { struct chunk *next; size_t length,sent; uint64_t offset; bool fin; uint8_t data[]; } chunk;
typedef struct event { struct event *next; dq_event value; uint64_t credit; uint8_t data[]; } event;
struct qs { qs *next; int64_t id; chunk *head,*tail; uint64_t offset,received; size_t queued; bool fin,reset_seen; };
struct qc {
    qc *next; qe *ep; uint64_t id; ngtcp2_conn *conn; gnutls_session_t tls;
    ngtcp2_crypto_conn_ref ref; ngtcp2_mem mem; qs *streams,*cursor; uint32_t stream_count,local_stream_count;
    ngtcp2_cid original; ngtcp2_ccerr error; char hostname[256];
    uint32_t state; int status,system; uint64_t created,retire,random_seed;
    chunk *datagrams,*datagram_tail; uint32_t datagram_count; bool terminal_event;
};
struct qe {
    uint64_t magic; dq_config config; size_t used; qfd fd,migration;
    struct sockaddr_storage local,migration_local; qlen local_len,migration_len;
    gnutls_certificate_credentials_t cred; bool server,wsa; uint8_t alpn[255]; size_t alpn_len;
    qc *connections; uint32_t connection_count,event_count; event *head,*tail; uint64_t event_id;
    uint8_t pending[Q_PACKET]; size_t pending_len; struct sockaddr_storage pending_peer;
    qlen pending_peer_len; qfd pending_fd; uint64_t pending_connection;
};
typedef union allocation { struct { size_t size; qe *ep; } value; max_align_t alignment; } allocation;
static _Atomic uint64_t q_serial=1;
static dq_io qi(size_t n,int s,int e) { dq_io r={n,s,e};return r; }
static dq_open qo(void *p,int s,int e) { dq_open r={p,s,e};return r; }
static dq_ref qr(uint64_t id,int64_t stream,int s,int e) { dq_ref r={id,stream,s,e};return r; }
static uint64_t q_now(void) {
#if defined(_WIN32)
    return GetTickCount64()*NGTCP2_MILLISECONDS;
#else
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t)) return 0;return (uint64_t)t.tv_sec*NGTCP2_SECONDS+(uint64_t)t.tv_nsec;
#endif
}
static qe *q_endpoint(void *p) { qe *e=p;return e && e->magic==Q_MAGIC?e:NULL; }
static uint64_t q_identifier(void) {
    uint64_t id=atomic_load_explicit(&q_serial,memory_order_relaxed);
    while(id && id<UINT64_MAX) if(atomic_compare_exchange_weak_explicit(&q_serial,&id,id+1,memory_order_relaxed,memory_order_relaxed)) return id;
    return 0;
}
static void *qa(size_t n,void *opaque) {
    qe *e=opaque;if(n>SIZE_MAX-sizeof(allocation) || n+sizeof(allocation)>e->config.memory-e->used) return NULL;
    allocation *p=malloc(sizeof *p+n);if(!p) return NULL;p->value.size=n+sizeof *p;p->value.ep=e;e->used+=p->value.size;return p+1;
}
static void qf(void *p,void *opaque) { (void)opaque;if(p) { allocation *a=(allocation*)p-1;a->value.ep->used-=a->value.size;free(a); } }
static void *qcalloc(size_t n,size_t size,void *opaque) { if(n && size>SIZE_MAX/n) return NULL;void *p=qa(n*size,opaque);if(p) memset(p,0,n*size);return p; }
static void *qrealloc(void *p,size_t n,void *opaque) {
    if(!p) return qa(n,opaque);if(!n) { qf(p,opaque);return NULL; }
    allocation *a=(allocation*)p-1;size_t old=a->value.size-sizeof *a;void *next=qa(n,opaque);
    if(next) { memcpy(next,p,n<old?n:old);qf(p,opaque); }return next;
}
static qc *q_connection(qe *e,uint64_t id) { for(qc *c=e->connections;c;c=c->next) if(c->id==id) return c;return NULL; }
static qs *q_stream(qc *c,int64_t id) { for(qs *s=c->streams;s;s=s->next) if(s->id==id) return s;return NULL; }
static bool q_input(const void *p,size_t n,size_t limit) { return n<=limit && (p || !n); }
static bool q_string(dq_text text,char *out,size_t capacity,bool empty) {
    if(!q_input(text.data,text.len,capacity-1) || (!text.len && !empty) || (text.len && memchr(text.data,0,text.len))) return false;
    if(text.len) memcpy(out,text.data,text.len);out[text.len]=0;return true;
}
static int q_sockaddr(dq_addr a,struct sockaddr_storage *out,qlen *n) {
    memset(out,0,sizeof *out);if(a.port>65535) return Q_INVALID;
    if(a.family==4) { struct sockaddr_in *s=(void*)out;s->sin_family=AF_INET;s->sin_port=htons((uint16_t)a.port);memcpy(&s->sin_addr,a.bytes,4);*n=sizeof *s;return Q_OK; }
    if(a.family==6) { struct sockaddr_in6 *s=(void*)out;s->sin6_family=AF_INET6;s->sin6_port=htons((uint16_t)a.port);s->sin6_scope_id=a.scope;memcpy(&s->sin6_addr,a.bytes,16);*n=sizeof *s;return Q_OK; }return Q_INVALID;
}
static dq_addr q_address(const struct sockaddr_storage *p) {
    dq_addr r={0};if(p->ss_family==AF_INET) { const struct sockaddr_in *s=(const void*)p;r.family=4;r.port=ntohs(s->sin_port);memcpy(r.bytes,&s->sin_addr,4); }
    else if(p->ss_family==AF_INET6) { const struct sockaddr_in6 *s=(const void*)p;r.family=6;r.port=ntohs(s->sin6_port);r.scope=s->sin6_scope_id;memcpy(r.bytes,&s->sin6_addr,16); }return r;
}
static bool q_specific(dq_addr a) { uint8_t v=0;size_t n=a.family==4?4:16;for(size_t i=0;i<n;i++) v|=a.bytes[i];return v!=0; }
static int q_bind(dq_addr a,qfd *fd,struct sockaddr_storage *local,qlen *length) {
    if(q_sockaddr(a,local,length) || !q_specific(a)) return Q_INVALID;
    *fd=socket(local->ss_family,SOCK_DGRAM,IPPROTO_UDP);if(*fd==Q_BAD) return Q_SYSTEM;
    if(local->ss_family==AF_INET6) { int yes=1;if(setsockopt(*fd,IPPROTO_IPV6,IPV6_V6ONLY,(const char*)&yes,sizeof yes)) goto bad; }
#if defined(_WIN32)
    u_long yes=1;if(ioctlsocket(*fd,FIONBIO,&yes)) goto bad;
#else
    int flags=fcntl(*fd,F_GETFL,0);if(flags<0 || fcntl(*fd,F_SETFL,flags|O_NONBLOCK)<0) goto bad;
    flags=fcntl(*fd,F_GETFD,0);if(flags<0 || fcntl(*fd,F_SETFD,flags|FD_CLOEXEC)<0) goto bad;
#endif
    if(bind(*fd,(struct sockaddr*)local,*length) || getsockname(*fd,(struct sockaddr*)local,length)) goto bad;return Q_OK;
bad:q_close(*fd);*fd=Q_BAD;return Q_SYSTEM;
}
static int q_event_push(qc *c,uint32_t kind,uint32_t flags,int64_t stream,uint64_t offset,uint64_t code,const uint8_t *data,size_t n) {
    qe *e=c->ep;if(e->event_count>=e->config.events || e->event_id==UINT64_MAX) return NGTCP2_ERR_CALLBACK_FAILURE;
    event *v=qa(sizeof *v+n,e);if(!v) return NGTCP2_ERR_CALLBACK_FAILURE;memset(v,0,sizeof *v);
    v->value=(dq_event){++e->event_id,c->id,stream,offset,code,n,kind,flags,Q_OK,0};if(n) memcpy(v->data,data,n);
    if(e->tail) e->tail->next=v;else e->head=v;e->tail=v;e->event_count++;return 0;
}
static void q_chunks_free(chunk *p,qe *e) { while(p) { chunk *next=p->next;qf(p,e);p=next; } }
static void q_stream_free(qc *c,qs *s) { q_chunks_free(s->head,c->ep);qf(s,c->ep); }
static qs *q_stream_new(qc *c,int64_t id) {
    qs *s=q_stream(c,id);if(s) return s;if(c->stream_count>=c->ep->config.streams) return NULL;
    s=qcalloc(1,sizeof *s,c->ep);if(!s) return NULL;s->id=id;s->next=c->streams;c->streams=s;c->stream_count++;
    if((id&1)==(c->ep->server?1:0)) c->local_stream_count++;return s;
}
static ngtcp2_conn *q_get_conn(ngtcp2_crypto_conn_ref *ref) { return ((qc*)ref->user_data)->conn; }
static void q_random(uint8_t *out,size_t n,const ngtcp2_rand_ctx *ctx) {
    /* ngtcp2 uses this callback for non-security scheduling/greasing only.
     * Key material, connection IDs, reset tokens use fallible OS-backed RNG. */
    uint64_t *state=ctx->native_handle;for(size_t i=0;i<n;i++) { *state^=*state<<13;*state^=*state>>7;*state^=*state<<17;out[i]=(uint8_t)*state; }
}
static int q_new_cid(ngtcp2_conn *conn,ngtcp2_cid *cid,ngtcp2_stateless_reset_token *token,size_t n,void *opaque) {
    (void)conn;(void)opaque;if(n!=Q_CID || gnutls_rnd(GNUTLS_RND_RANDOM,cid->data,n) || gnutls_rnd(GNUTLS_RND_RANDOM,token->data,sizeof token->data)) return NGTCP2_ERR_CALLBACK_FAILURE;cid->datalen=n;return 0;
}
static int q_handshake(ngtcp2_conn *conn,void *opaque) {
    (void)conn;qc *c=opaque;gnutls_datum_t selected;unsigned status=0;
    if(gnutls_alpn_get_selected_protocol(c->tls,&selected) || selected.size!=c->ep->alpn_len || memcmp(selected.data,c->ep->alpn,selected.size)) return NGTCP2_ERR_CALLBACK_FAILURE;
    if(!c->ep->server && (gnutls_certificate_verify_peers3(c->tls,c->hostname,&status)<0 || status)) { c->status=Q_TLS;return NGTCP2_ERR_CALLBACK_FAILURE; }
    c->state=Q_READY;return q_event_push(c,Q_CONNECTED,0,-1,0,0,NULL,0);
}
static int q_remote_stream(ngtcp2_conn *conn,int64_t id,void *opaque) { (void)conn;qc *c=opaque;if(!q_stream_new(c,id)) return NGTCP2_ERR_CALLBACK_FAILURE;return q_event_push(c,Q_STREAM,0,id,0,0,NULL,0); }
static int q_receive(ngtcp2_conn *conn,uint32_t flags,int64_t id,uint64_t offset,const uint8_t *data,size_t n,void *opaque,void *stream_data) {
    (void)conn;(void)stream_data;qc *c=opaque;if(c->state!=Q_READY || (flags&NGTCP2_STREAM_DATA_FLAG_0RTT)) return NGTCP2_ERR_CALLBACK_FAILURE;
    qs *s=q_stream_new(c,id);if(!s) return NGTCP2_ERR_CALLBACK_FAILURE;s->received=offset+n;return q_event_push(c,Q_DATA,(flags&NGTCP2_STREAM_DATA_FLAG_FIN)?1:0,id,offset,0,data,n);
}
static int q_acked(ngtcp2_conn *conn,int64_t id,uint64_t offset,uint64_t n,void *opaque,void *stream_data) {
    (void)conn;(void)stream_data;qc *c=opaque;qs *s=q_stream(c,id);if(!s) return 0;
    while(s->head && s->head->sent==s->head->length && s->head->offset+s->head->length<=offset+n) { chunk *p=s->head;s->head=p->next;s->queued-=p->length;qf(p,c->ep); }
    if(!s->head) s->tail=NULL;return 0;
}
static int q_stream_closed(ngtcp2_conn *conn,uint32_t flags,int64_t id,uint64_t code,void *opaque,void *stream_data) {
    (void)stream_data;qc *c=opaque;qs **p=&c->streams;while(*p && (*p)->id!=id) p=&(*p)->next;
    if(*p) { qs *s=*p;*p=s->next;if(c->cursor==s) c->cursor=s->next;q_stream_free(c,s);c->stream_count--;if((id&1)==(c->ep->server?1:0)) c->local_stream_count--; }
    if((id&1)==(c->ep->server?0:1)) { if(id&2) ngtcp2_conn_extend_max_streams_uni(conn,1);else ngtcp2_conn_extend_max_streams_bidi(conn,1); }
    return q_event_push(c,Q_STREAM_CLOSED,flags,id,0,code,NULL,0);
}
static int q_stream_reset(ngtcp2_conn *conn,int64_t id,uint64_t final,uint64_t code,void *opaque,void *stream_data) {
    (void)conn;(void)stream_data;qc *c=opaque;qs *s=q_stream_new(c,id);if(!s || final<s->received) return NGTCP2_ERR_CALLBACK_FAILURE;
    int r=q_event_push(c,Q_RESET,0,id,final,code,NULL,0);if(!r && !s->reset_seen) { c->ep->tail->credit=final-s->received;s->reset_seen=true; }return r;
}
static int q_datagram_received(ngtcp2_conn *conn,uint32_t flags,const uint8_t *data,size_t n,void *opaque) {
    (void)conn;(void)flags;qc *c=opaque;if(c->state!=Q_READY) return NGTCP2_ERR_CALLBACK_FAILURE;
    /* An exhausted unreliable receive queue drops this DATAGRAM. */
    (void)q_event_push(c,Q_DATAGRAM,0,-1,0,0,data,n);return 0;
}
static bool q_same_local(const ngtcp2_addr *a,const struct sockaddr_storage *b,qlen n) { return a->addrlen==(ngtcp2_socklen)n && !memcmp(a->addr,b,(size_t)n); }
static int q_path(ngtcp2_conn *conn,uint32_t flags,const ngtcp2_path *path,const ngtcp2_path *fallback,ngtcp2_path_validation_result result,void *opaque) {
    (void)conn;(void)flags;(void)fallback;qc *c=opaque;qe *e=c->ep;
    if(e->migration!=Q_BAD && q_same_local(&path->local,&e->migration_local,e->migration_len)) {
        if(result==NGTCP2_PATH_VALIDATION_RESULT_SUCCESS) { q_close(e->fd);e->fd=e->migration;e->local=e->migration_local;e->local_len=e->migration_len; }
        else q_close(e->migration);e->migration=Q_BAD;
    }
    return q_event_push(c,Q_PATH,result==NGTCP2_PATH_VALIDATION_RESULT_SUCCESS?1:0,-1,0,0,NULL,0);
}
static const ngtcp2_callbacks q_callbacks={
    .client_initial=ngtcp2_crypto_client_initial_cb,.recv_client_initial=ngtcp2_crypto_recv_client_initial_cb,
    .recv_crypto_data=ngtcp2_crypto_recv_crypto_data_cb,.handshake_completed=q_handshake,
    .encrypt=ngtcp2_crypto_encrypt_cb,.decrypt=ngtcp2_crypto_decrypt_cb,.hp_mask=ngtcp2_crypto_hp_mask_cb,
    .recv_retry=ngtcp2_crypto_recv_retry_cb,.rand=q_random,.get_new_connection_id2=q_new_cid,
    .update_key=ngtcp2_crypto_update_key_cb,.delete_crypto_aead_ctx=ngtcp2_crypto_delete_crypto_aead_ctx_cb,
    .delete_crypto_cipher_ctx=ngtcp2_crypto_delete_crypto_cipher_ctx_cb,.version_negotiation=ngtcp2_crypto_version_negotiation_cb,
    .get_path_challenge_data2=ngtcp2_crypto_get_path_challenge_data2_cb,.stream_open=q_remote_stream,
    .recv_stream_data=q_receive,.acked_stream_data_offset=q_acked,.stream_close=q_stream_closed,
    .stream_reset=q_stream_reset,.recv_datagram=q_datagram_received,.path_validation=q_path
};
static void q_fail(qc *c,int status,int system) {
    if(c->state>=Q_CLOSING) return;c->status=status;c->system=system;c->state=Q_CLOSING;
    if(system<0) { if(system==NGTCP2_ERR_CRYPTO) ngtcp2_ccerr_set_tls_alert(&c->error,ngtcp2_conn_get_tls_alert2(c->conn),NULL,0);else ngtcp2_ccerr_set_liberr(&c->error,system,NULL,0); }
    else ngtcp2_ccerr_set_application_error(&c->error,(uint64_t)status,NULL,0);
    uint64_t pto=ngtcp2_conn_get_pto2(c->conn);if(pto>10*NGTCP2_SECONDS) pto=10*NGTCP2_SECONDS;c->retire=q_now()+3*pto;
}
static void q_connection_free(qc *c) {
    qe *e=c->ep;if(c->tls) gnutls_session_set_ptr(c->tls,NULL);if(c->conn) ngtcp2_conn_del(c->conn);if(c->tls) gnutls_deinit(c->tls);
    while(c->streams) { qs *next=c->streams->next;q_stream_free(c,c->streams);c->streams=next; }q_chunks_free(c->datagrams,e);qf(c,e);
}
static qc *q_connection_new(qe *e,const ngtcp2_path *path,const ngtcp2_pkt_hd *hd,const char *hostname,uint32_t version,int *error) {
    *error=0;if(e->connection_count>=e->config.connections) { *error=Q_LIMIT;return NULL; }
    qc *c=qcalloc(1,sizeof *c,e);if(!c) { *error=Q_LIMIT;return NULL; }c->ep=e;c->state=Q_HANDSHAKE;c->created=q_now();
    c->id=q_identifier();if(!c->id) { *error=Q_LIMIT;goto bad; }c->mem=(ngtcp2_mem){e,qa,qf,qcalloc,qrealloc};c->ref=(ngtcp2_crypto_conn_ref){q_get_conn,c};ngtcp2_ccerr_default(&c->error);
    ngtcp2_cid dcid,scid;scid.datalen=Q_CID;if(gnutls_rnd(GNUTLS_RND_RANDOM,scid.data,scid.datalen)) { *error=Q_TLS;goto bad; }
    if(hd) { dcid=hd->scid;c->original=hd->dcid; }else { dcid.datalen=NGTCP2_MIN_INITIAL_DCIDLEN;if(gnutls_rnd(GNUTLS_RND_RANDOM,dcid.data,dcid.datalen)) { *error=Q_TLS;goto bad; } }
    ngtcp2_settings settings;ngtcp2_settings_default(&settings);settings.initial_ts=c->created;settings.cc_algo=e->config.congestion==2?NGTCP2_CC_ALGO_CUBIC:NGTCP2_CC_ALGO_RENO;settings.max_tx_udp_payload_size=1200;settings.no_pmtud=1;
    if(gnutls_rnd(GNUTLS_RND_RANDOM,&c->random_seed,sizeof c->random_seed)) { *error=Q_TLS;goto bad; }if(!c->random_seed) c->random_seed=1;settings.rand_ctx.native_handle=&c->random_seed;
    ngtcp2_transport_params params;ngtcp2_transport_params_default(&params);
    params.initial_max_streams_bidi=e->config.streams/4;params.initial_max_streams_uni=e->config.streams/4;
    params.initial_max_stream_data_bidi_local=e->config.window;params.initial_max_stream_data_bidi_remote=e->config.window;params.initial_max_stream_data_uni=e->config.window;
    params.initial_max_data=e->config.window;params.max_idle_timeout=(uint64_t)e->config.idle_ms*NGTCP2_MILLISECONDS;params.active_connection_id_limit=4;params.max_datagram_frame_size=1200;
    if(hd) { params.original_dcid=hd->dcid;params.original_dcid_present=1;params.stateless_reset_token_present=1;if(gnutls_rnd(GNUTLS_RND_RANDOM,params.stateless_reset_token,sizeof params.stateless_reset_token)) { *error=Q_TLS;goto bad; } }
    int r=hd?ngtcp2_conn_server_new(&c->conn,&dcid,&scid,path,version,&q_callbacks,&settings,&params,&c->mem,c):ngtcp2_conn_client_new(&c->conn,&dcid,&scid,path,version,&q_callbacks,&settings,&params,&c->mem,c);if(r) { *error=r;goto bad; }
    r=gnutls_init(&c->tls,(e->server?GNUTLS_SERVER:GNUTLS_CLIENT)|GNUTLS_NO_TICKETS|GNUTLS_NO_AUTO_SEND_TICKET|GNUTLS_NO_END_OF_EARLY_DATA);if(r) { *error=r;goto bad; }
    if((e->server?ngtcp2_crypto_gnutls_configure_server_session(c->tls):ngtcp2_crypto_gnutls_configure_client_session(c->tls)) ||
       gnutls_priority_set_direct(c->tls,"NORMAL:-VERS-ALL:+VERS-TLS1.3:%DISABLE_TLS13_COMPAT_MODE",NULL) || gnutls_credentials_set(c->tls,GNUTLS_CRD_CERTIFICATE,e->cred)) { *error=Q_TLS;goto bad; }
    gnutls_handshake_set_max_packet_length(c->tls,65536);gnutls_session_set_ptr(c->tls,&c->ref);ngtcp2_conn_set_tls_native_handle(c->conn,c->tls);
    gnutls_datum_t alpn={e->alpn,(unsigned)e->alpn_len};if(gnutls_alpn_set_protocols(c->tls,&alpn,1,GNUTLS_ALPN_MANDATORY|(e->server?GNUTLS_ALPN_SERVER_PRECEDENCE:0))) { *error=Q_TLS;goto bad; }
    if(!e->server) { strcpy(c->hostname,hostname);gnutls_session_set_verify_cert(c->tls,c->hostname,0);uint8_t numeric[16];
        if(inet_pton(AF_INET,hostname,numeric)!=1 && inet_pton(AF_INET6,hostname,numeric)!=1 && gnutls_server_name_set(c->tls,GNUTLS_NAME_DNS,hostname,strlen(hostname))) { *error=Q_TLS;goto bad; } }
    c->next=e->connections;e->connections=c;e->connection_count++;return c;
bad:q_connection_free(c);return NULL;
}
static int q_send_pending(qe *e) {
    if(!e->pending_len) return Q_OK;
    int r=(int)sendto(e->pending_fd,(const char*)e->pending,(int)e->pending_len,MSG_NOSIGNAL,(struct sockaddr*)&e->pending_peer,e->pending_peer_len);
    if(r<0) { int error=q_errno();return Q_AGAIN(error)||Q_INTR(error)?Q_AGAIN_STATUS:Q_SYSTEM; }
    if((size_t)r!=e->pending_len) return Q_SYSTEM;e->pending_len=0;return Q_OK;
}
static int q_send(qc *c,const ngtcp2_path *path,const uint8_t *packet,size_t n) {
    qe *e=c->ep;if(e->pending_len || n>Q_PACKET || path->remote.addrlen>sizeof e->pending_peer) return Q_INVALID;
    memcpy(e->pending,packet,n);e->pending_len=n;e->pending_peer_len=(qlen)path->remote.addrlen;memcpy(&e->pending_peer,path->remote.addr,path->remote.addrlen);
    e->pending_fd=e->migration!=Q_BAD && q_same_local(&path->local,&e->migration_local,e->migration_len)?e->migration:e->fd;e->pending_connection=c->id;return q_send_pending(e);
}
static int q_flush(qc *c,uint64_t now) {
    if(c->state==Q_CLOSED || c->state==Q_DRAINING) return Q_OK;uint8_t packet[Q_PACKET];ngtcp2_path_storage ps;ngtcp2_path_storage_zero(&ps);ngtcp2_pkt_info pi={0};
    if(c->state==Q_CLOSING) { ngtcp2_ssize n=ngtcp2_conn_write_connection_close(c->conn,&ps.path,&pi,packet,sizeof packet,&c->error,now);if(n>0) return q_send(c,&ps.path,packet,(size_t)n);return Q_OK; }
    for(unsigned i=0;i<32;i++) {
        if(c->ep->pending_len) return Q_AGAIN_STATUS;ngtcp2_ssize n=0,used=-1;qs *chosen=NULL;chunk *part=NULL;
        qs *start=c->cursor?c->cursor:c->streams;qs *scan=c->datagrams && i%4==0?NULL:start;
        if(scan) do { for(chunk *p=scan->head;p;p=p->next) if(p->sent<p->length || (p->fin && !p->sent && !p->length)) { chosen=scan;part=p;break; }
            scan=scan->next?scan->next:c->streams;if(part) break; } while(scan!=start);
        if(chosen) c->cursor=chosen->next?chosen->next:c->streams;bool blocked=false;
        if(part) { ngtcp2_vec v={part->data+part->sent,part->length-part->sent};uint32_t flags=part->fin?NGTCP2_WRITE_STREAM_FLAG_FIN:0;
            n=ngtcp2_conn_writev_stream(c->conn,&ps.path,&pi,packet,sizeof packet,&used,flags,chosen->id,&v,1,now);
            if(used>=0) { part->sent+=(size_t)used;if(part->fin && !part->length) part->sent=1; }
            if(n==NGTCP2_ERR_STREAM_DATA_BLOCKED || n==NGTCP2_ERR_STREAM_SHUT_WR || n==NGTCP2_ERR_STREAM_NOT_FOUND) { blocked=true;n=0; }
        }
        if((!part || blocked) && c->datagrams) { chunk *p=c->datagrams;ngtcp2_vec v={p->data,p->length};int accepted=0;
            n=ngtcp2_conn_writev_datagram(c->conn,&ps.path,&pi,packet,sizeof packet,&accepted,0,0,&v,1,now);
            if(accepted) { c->datagrams=p->next;c->datagram_count--;if(!c->datagrams) c->datagram_tail=NULL;qf(p,c->ep); }
        }else if(!part || blocked) n=ngtcp2_conn_write_pkt(c->conn,&ps.path,&pi,packet,sizeof packet,now);
        if(n<0) { q_fail(c,n==NGTCP2_ERR_CRYPTO?Q_TLS:Q_PROTOCOL,(int)n);return Q_OK; }
        if(!n) { if(blocked && c->stream_count>1) continue;break; }
        int r=q_send(c,&ps.path,packet,(size_t)n);ngtcp2_conn_update_pkt_tx_time(c->conn,now);if(r!=Q_OK) return r;
    }return Q_OK;
}
static qc *q_route(qe *e,const ngtcp2_version_cid *v) {
    for(qc *c=e->connections;c;c=c->next) { if(c->state==Q_CLOSED) continue;
        if(c->original.datalen==v->dcidlen && !memcmp(c->original.data,v->dcid,v->dcidlen)) return c;
        size_t n=ngtcp2_conn_get_scid2(c->conn,NULL);if(n>16) continue;ngtcp2_cid ids[16];ngtcp2_conn_get_scid2(c->conn,ids);
        for(size_t i=0;i<n;i++) if(ids[i].datalen==v->dcidlen && !memcmp(ids[i].data,v->dcid,v->dcidlen)) return c;
    }return NULL;
}
static void q_read(qe *e,qfd fd,struct sockaddr_storage *local,qlen length,size_t *progress) {
    for(unsigned i=0;i<64 && !e->pending_len;i++) {
        uint8_t packet[65536];struct sockaddr_storage peer;qlen peerlen=sizeof peer;
        int n=(int)recvfrom(fd,(char*)packet,sizeof packet,0,(struct sockaddr*)&peer,&peerlen);
        if(n<0) { int error=q_errno();if(Q_INTR(error)) continue;return; }if(!n) continue;(*progress)++;
        ngtcp2_version_cid v;int r=ngtcp2_pkt_decode_version_cid(&v,packet,(size_t)n,Q_CID);if(r && r!=NGTCP2_ERR_VERSION_NEGOTIATION) continue;
        ngtcp2_path path={{(struct sockaddr*)local,(ngtcp2_socklen)length},{(struct sockaddr*)&peer,(ngtcp2_socklen)peerlen},NULL};qc *c=q_route(e,&v);
        if(!c) { if(!e->server || n<1200) continue;
            if(r==NGTCP2_ERR_VERSION_NEGOTIATION) { uint32_t versions[]={NGTCP2_PROTO_VER_V1,NGTCP2_PROTO_VER_V2};uint8_t random=0,reply[1500];if(gnutls_rnd(GNUTLS_RND_RANDOM,&random,1)) continue;
                ngtcp2_ssize wrote=ngtcp2_pkt_write_version_negotiation(reply,sizeof reply,random,v.scid,v.scidlen,v.dcid,v.dcidlen,versions,2);if(wrote>0) (void)sendto(fd,(char*)reply,(int)wrote,MSG_NOSIGNAL,(struct sockaddr*)&peer,peerlen);continue; }
            ngtcp2_pkt_hd hd;if(ngtcp2_accept(&hd,packet,(size_t)n)) continue;int error=0;c=q_connection_new(e,&path,&hd,NULL,v.version,&error);if(!c) continue;
        }
        if(c->state==Q_CLOSING || c->state==Q_DRAINING) continue;
        ngtcp2_pkt_info pi={0};r=ngtcp2_conn_read_pkt(c->conn,&path,&pi,packet,(size_t)n,q_now());
        if(r==NGTCP2_ERR_DRAINING) { c->error=*ngtcp2_conn_get_ccerr2(c->conn);c->status=c->error.error_code?(c->error.error_code>=0x100 && c->error.error_code<0x200?Q_TLS:Q_PROTOCOL):Q_OK;c->state=Q_DRAINING;c->retire=q_now()+3*ngtcp2_conn_get_pto2(c->conn); }
        else if(r) q_fail(c,r==NGTCP2_ERR_CRYPTO?Q_TLS:(r==NGTCP2_ERR_NOMEM || r==NGTCP2_ERR_CALLBACK_FAILURE?Q_LIMIT:Q_PROTOCOL),r);
    }
}
static int q_wait(qe *e,int ms) {
#if defined(_WIN32)
    WSAPOLLFD p[2]={{e->fd,POLLRDNORM,0},{e->migration,POLLRDNORM,0}};return WSAPoll(p,e->migration==Q_BAD?1:2,ms);
#else
    struct pollfd p[2]={{e->fd,POLLIN,0},{e->migration,POLLIN,0}};return poll(p,e->migration==Q_BAD?1:2,ms);
#endif
}
static dq_io q_poll(void *handle,uint32_t ms,const void *cancel) {
    qe *e=q_endpoint(handle);if(!e) return qi(0,Q_INVALID,0);uint64_t deadline=q_now()+(uint64_t)ms*NGTCP2_MILLISECONDS;size_t progress=0;
    for(;;) {
        if(cancel && atomic_load_explicit((const _Atomic bool*)cancel,memory_order_acquire)) return qi(progress,Q_CANCELLED,0);
        if(q_send_pending(e)==Q_SYSTEM) return qi(progress,Q_SYSTEM,q_errno());uint64_t now=q_now(),expiry=deadline;
        for(qc *c=e->connections;c;c=c->next) {
            if(c->state==Q_HANDSHAKE && now-c->created>=(uint64_t)e->config.handshake_ms*NGTCP2_MILLISECONDS) q_fail(c,Q_TIMEOUT,0);
            if(c->state>=Q_CLOSING && c->state<Q_CLOSED && now>=c->retire) { c->state=Q_CLOSED;if(e->migration!=Q_BAD) { q_close(e->migration);e->migration=Q_BAD; } }
            if(c->state==Q_CLOSED) { if(!c->terminal_event && !q_event_push(c,Q_DISCONNECTED,0,-1,0,c->error.error_code,NULL,0)) c->terminal_event=true;continue; }
            uint64_t next=ngtcp2_conn_get_expiry2(c->conn);
            if(c->state<Q_CLOSING && now>=next) { int r=ngtcp2_conn_handle_expiry(c->conn,now);if(r) q_fail(c,r==NGTCP2_ERR_IDLE_CLOSE?Q_TIMEOUT:Q_PROTOCOL,r); }
            if(!e->pending_len && q_flush(c,now)==Q_SYSTEM) return qi(progress,Q_SYSTEM,q_errno());
            next=c->state>=Q_CLOSING?c->retire:ngtcp2_conn_get_expiry2(c->conn);if(next<expiry) expiry=next;
            if(c->state==Q_HANDSHAKE && c->created+(uint64_t)e->config.handshake_ms*NGTCP2_MILLISECONDS<expiry) expiry=c->created+(uint64_t)e->config.handshake_ms*NGTCP2_MILLISECONDS;
        }
        q_read(e,e->fd,&e->local,e->local_len,&progress);if(e->migration!=Q_BAD) q_read(e,e->migration,&e->migration_local,e->migration_len,&progress);
        for(qc *c=e->connections;c && !e->pending_len;c=c->next) (void)q_flush(c,q_now());
        if(e->head || progress) return qi(progress,Q_OK,0);now=q_now();if(now>=deadline) return qi(0,ms?Q_TIMEOUT:Q_AGAIN_STATUS,0);
        uint64_t wait=expiry>now?(expiry-now+NGTCP2_MILLISECONDS-1)/NGTCP2_MILLISECONDS:0;if(wait>10) wait=10;
        if(q_wait(e,(int)wait)<0 && !Q_INTR(q_errno())) return qi(0,Q_SYSTEM,q_errno());
    }
}
static dq_event q_next(void *handle) { qe *e=q_endpoint(handle);dq_event r={0};if(!e) r.status=Q_INVALID;else if(e->head) r=e->head->value;else r.status=Q_AGAIN_STATUS;return r; }
static dq_io q_copy(void *handle,uint64_t id,size_t offset,uint8_t *out,size_t capacity) {
    qe *e=q_endpoint(handle);if(!e || !e->head || e->head->value.id!=id || !q_input(out,capacity,67108864)) return qi(0,Q_INVALID,0);
    event *v=e->head;if(offset>v->value.count) return qi(0,Q_INVALID,0);size_t n=v->value.count-offset;if(n>capacity) n=capacity;if(n) memcpy(out,v->data+offset,n);return qi(n,Q_OK,0);
}
static dq_io q_ack(void *handle,uint64_t id) {
    qe *e=q_endpoint(handle);if(!e || !e->head || e->head->value.id!=id) return qi(0,Q_INVALID,0);event *v=e->head;qc *c=q_connection(e,v->value.connection);
    if(c && c->state<Q_CLOSING && v->value.kind==Q_DATA) { (void)ngtcp2_conn_extend_max_stream_offset(c->conn,v->value.stream,v->value.count);ngtcp2_conn_extend_max_offset(c->conn,v->value.count); }
    if(c && c->state<Q_CLOSING && v->credit) ngtcp2_conn_extend_max_offset(c->conn,v->credit);
    e->head=v->next;if(!e->head) e->tail=NULL;e->event_count--;qf(v,e);return qi(1,Q_OK,0);
}
static dq_ref q_connect(void *handle,dq_addr address,dq_text name) {
    qe *e=q_endpoint(handle);char hostname[256];if(!e || e->server || !address.port || !q_string(name,hostname,sizeof hostname,false)) return qr(0,-1,Q_INVALID,0);
    struct sockaddr_storage peer;qlen length;if(q_sockaddr(address,&peer,&length) || peer.ss_family!=e->local.ss_family) return qr(0,-1,Q_INVALID,0);
    ngtcp2_path path={{(struct sockaddr*)&e->local,(ngtcp2_socklen)e->local_len},{(struct sockaddr*)&peer,(ngtcp2_socklen)length},NULL};int error=0;
    qc *c=q_connection_new(e,&path,NULL,hostname,NGTCP2_PROTO_VER_V1,&error);return c?qr(c->id,-1,Q_OK,0):qr(0,-1,error<0?Q_TLS:error,error);
}
static dq_ref q_open_stream(void *handle,uint64_t id,bool uni) {
    qe *e=q_endpoint(handle);qc *c=e?q_connection(e,id):NULL;if(!c) return qr(0,-1,Q_INVALID,0);if(c->state!=Q_READY) return qr(id,-1,c->state>=Q_CLOSING?Q_EOF:Q_AGAIN_STATUS,0);
    if(c->local_stream_count>=e->config.streams/2) return qr(id,-1,Q_LIMIT,0);int64_t stream=-1;int r=uni?ngtcp2_conn_open_uni_stream(c->conn,&stream,NULL):ngtcp2_conn_open_bidi_stream(c->conn,&stream,NULL);
    if(r) return qr(id,-1,r==NGTCP2_ERR_STREAM_ID_BLOCKED?Q_AGAIN_STATUS:Q_PROTOCOL,r);
    if(!q_stream_new(c,stream)) { (void)ngtcp2_conn_shutdown_stream(c->conn,0,stream,Q_LIMIT);return qr(id,-1,Q_LIMIT,0); }return qr(id,stream,Q_OK,0);
}
static dq_io q_write(void *handle,uint64_t id,int64_t stream,const uint8_t *data,size_t n,bool fin) {
    qe *e=q_endpoint(handle);qc *c=e?q_connection(e,id):NULL;qs *s=c?q_stream(c,stream):NULL;
    if(!s || !q_input(data,n,e->config.window) || ((stream&2) && (stream&1)!=(e->server?1:0))) return qi(0,Q_INVALID,0);
    if(c->state!=Q_READY || s->fin) return qi(0,Q_EOF,0);if(n>e->config.window-s->queued) return qi(0,Q_AGAIN_STATUS,0);if(!n && !fin) return qi(0,Q_OK,0);
    if(s->offset>Q_APP_MAX-n) return qi(0,Q_LIMIT,0);chunk *p=qa(sizeof *p+n,e);if(!p) return qi(0,Q_LIMIT,0);*p=(chunk){NULL,n,0,s->offset,fin};if(n) memcpy(p->data,data,n);
    s->offset+=n;s->queued+=n;s->fin=fin;if(s->tail) s->tail->next=p;else s->head=p;s->tail=p;return qi(n,Q_OK,0);
}
static dq_io q_datagram(void *handle,uint64_t id,const uint8_t *data,size_t n) {
    qe *e=q_endpoint(handle);qc *c=e?q_connection(e,id):NULL;if(!c || !q_input(data,n,Q_MAX_DGRAM)) return qi(0,Q_INVALID,0);if(c->state!=Q_READY) return qi(0,Q_AGAIN_STATUS,0);
    const ngtcp2_transport_params *params=ngtcp2_conn_get_remote_transport_params2(c->conn);if(!params || params->max_datagram_frame_size<n+3) return qi(0,Q_UNSUPPORTED,0);if(c->datagram_count>=e->config.events) return qi(0,Q_AGAIN_STATUS,0);
    chunk *p=qa(sizeof *p+n,e);if(!p) return qi(0,Q_LIMIT,0);*p=(chunk){NULL,n,0,0,false};if(n) memcpy(p->data,data,n);
    if(c->datagram_tail) c->datagram_tail->next=p;else c->datagrams=p;c->datagram_tail=p;c->datagram_count++;return qi(n,Q_OK,0);
}
static dq_io q_reset(void *handle,uint64_t id,int64_t stream,uint64_t code) {
    qe *e=q_endpoint(handle);qc *c=e?q_connection(e,id):NULL;if(!c || !q_stream(c,stream) || code>Q_APP_MAX || c->state!=Q_READY) return qi(0,Q_INVALID,0);
    int r=ngtcp2_conn_shutdown_stream(c->conn,0,stream,code);qs *s=q_stream(c,stream);if(!r && s) s->fin=true;return qi(0,r?Q_PROTOCOL:Q_OK,r);
}
static dq_io q_disconnect(void *handle,uint64_t id,uint64_t code) {
    qe *e=q_endpoint(handle);qc *c=e?q_connection(e,id):NULL;if(!c || code>Q_APP_MAX) return qi(0,Q_INVALID,0);if(c->state>=Q_CLOSING) return qi(0,Q_OK,0);
    q_fail(c,Q_OK,0);ngtcp2_ccerr_set_application_error(&c->error,code,NULL,0);return qi(0,Q_OK,0);
}
static dq_io q_forget(void *handle,uint64_t id) {
    qe *e=q_endpoint(handle);if(!e) return qi(0,Q_INVALID,0);qc **p=&e->connections;while(*p && (*p)->id!=id) p=&(*p)->next;
    if(!*p) return qi(0,Q_INVALID,0);if((*p)->state!=Q_CLOSED) return qi(0,Q_AGAIN_STATUS,0);for(event *v=e->head;v;v=v->next) if(v->value.connection==id) return qi(0,Q_AGAIN_STATUS,0);
    if(e->pending_len && e->pending_connection==id) e->pending_len=0;qc *c=*p;*p=c->next;q_connection_free(c);e->connection_count--;return qi(1,Q_OK,0);
}
static dq_io q_migrate(void *handle,uint64_t id,dq_addr bind_address) {
    qe *e=q_endpoint(handle);qc *c=e?q_connection(e,id):NULL;if(!c || e->server || e->connection_count!=1 || e->migration!=Q_BAD || e->pending_len || c->state!=Q_READY) return qi(0,Q_INVALID,0);
    if(bind_address.family!=q_address(&e->local).family) return qi(0,Q_UNSUPPORTED,0);
    int r=q_bind(bind_address,&e->migration,&e->migration_local,&e->migration_len);if(r) return qi(0,r,q_errno());
    const ngtcp2_path *old=ngtcp2_conn_get_path2(c->conn);ngtcp2_path path={{(struct sockaddr*)&e->migration_local,(ngtcp2_socklen)e->migration_len},old->remote,NULL};
    r=ngtcp2_conn_initiate_migration(c->conn,&path,q_now());if(r) { q_close(e->migration);e->migration=Q_BAD;return qi(0,r==NGTCP2_ERR_INVALID_STATE?Q_UNSUPPORTED:Q_AGAIN_STATUS,r); }return qi(0,Q_OK,0);
}
static dq_stats q_stats(void *handle,uint64_t id) {
    qe *e=q_endpoint(handle);dq_stats r={0};if(!e) { r.status=Q_INVALID;return r; }r.used=e->used;r.limit=e->config.memory;r.connections=e->connection_count;r.events=e->event_count;
    qc *one=id?q_connection(e,id):NULL;if(id && !one) { r.status=Q_INVALID;return r; }
    for(qc *c=one?one:e->connections;c;c=one?NULL:c->next) { ngtcp2_conn_info i;ngtcp2_conn_get_conn_info2(c->conn,&i);r.sent+=i.bytes_sent;r.received+=i.bytes_recv;r.lost+=i.pkt_lost;r.cwnd+=i.cwnd;r.in_flight+=i.bytes_in_flight;r.rtt_ns=i.smoothed_rtt;r.streams+=c->stream_count;
        if(one) { r.state=c->state;r.status=c->status;r.system=c->system;r.version=ngtcp2_conn_get_negotiated_version2(c->conn); } }return r;
}
static dq_resolved q_local(void *handle) { dq_resolved r={0};qe *e=q_endpoint(handle);if(!e) r.status=Q_INVALID;else r.address=q_address(&e->local);return r; }
static void q_destroy(void *handle) {
    qe *e=q_endpoint(handle);if(!e) return;e->magic=0;if(e->fd!=Q_BAD) q_close(e->fd);if(e->migration!=Q_BAD) q_close(e->migration);
    while(e->connections) { qc *next=e->connections->next;q_connection_free(e->connections);e->connections=next; }
    while(e->head) { event *next=e->head->next;qf(e->head,e);e->head=next; }
    if(e->cred) gnutls_certificate_free_credentials(e->cred);gnutls_global_deinit();
#if defined(_WIN32)
    if(e->wsa) WSACleanup();
#endif
    free(e);
}
static int q_read_file(qe *e,const char *path,gnutls_datum_t *out) {
    FILE *f=NULL;
#if defined(_WIN32)
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,NULL,0);if(n<=0 || n>32768) return Q_INVALID;
    wchar_t *wide=qa((size_t)n*sizeof *wide,e);if(!wide) return Q_LIMIT;if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,n)) f=_wfopen(wide,L"rb");qf(wide,e);
#else
    f=fopen(path,"rb");
#endif
    if(!f) return Q_SYSTEM;if(fseek(f,0,SEEK_END)) { fclose(f);return Q_SYSTEM; }long length=ftell(f);
    if(length<=0 || length>4*1024*1024 || fseek(f,0,SEEK_SET)) { fclose(f);return Q_LIMIT; }out->data=qa((size_t)length,e);if(!out->data) { fclose(f);return Q_LIMIT; }out->size=(unsigned)length;
    bool ok=fread(out->data,1,out->size,f)==out->size;fclose(f);if(!ok) { volatile uint8_t *bytes=out->data;for(unsigned i=0;i<out->size;i++) bytes[i]=0;qf(out->data,e);out->data=NULL;return Q_SYSTEM; }return Q_OK;
}
static dq_open q_create(dq_addr local,bool server,dq_text alpn,dq_text certificate,dq_text key,dq_config config) {
    if(config.connections<1 || config.connections>128 || config.streams<4 || config.streams>1024 || config.events<4 || config.events>65536 || !config.idle_ms || !config.handshake_ms || config.handshake_ms>config.idle_ms ||
       config.congestion<1 || config.congestion>2 || config.memory<1048576 || config.memory>268435456 || config.window<1024 || config.window>16777216 || config.window>config.memory/4 || !q_input(alpn.data,alpn.len,255) || !alpn.len) return qo(NULL,Q_INVALID,0);
    char certpath[32768],keypath[32768];if(!q_string(certificate,certpath,sizeof certpath,!server) || !q_string(key,keypath,sizeof keypath,!server)) return qo(NULL,Q_INVALID,0);
    if(!gnutls_check_version("3.7.5") || gnutls_global_init()) return qo(NULL,Q_UNSUPPORTED,0);qe *e=calloc(1,sizeof *e);if(!e) { gnutls_global_deinit();return qo(NULL,Q_LIMIT,0); }
    e->magic=Q_MAGIC;e->config=config;e->used=sizeof *e;e->fd=e->migration=Q_BAD;e->server=server;memcpy(e->alpn,alpn.data,alpn.len);e->alpn_len=alpn.len;
#if defined(_WIN32)
    WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa)) { q_destroy(e);return qo(NULL,Q_SYSTEM,0); }e->wsa=true;
#endif
    int r=q_bind(local,&e->fd,&e->local,&e->local_len);if(r) { int system=q_errno();q_destroy(e);return qo(NULL,r,system); }
    r=gnutls_certificate_allocate_credentials(&e->cred);if(r<0) { q_destroy(e);return qo(NULL,Q_TLS,r); }gnutls_datum_t cert={0},private_key={0};int status=Q_OK;
    if(server) { status=q_read_file(e,certpath,&cert);if(!status) status=q_read_file(e,keypath,&private_key);if(!status) { r=gnutls_certificate_set_x509_key_mem(e->cred,&cert,&private_key,GNUTLS_X509_FMT_PEM);if(r<0) status=Q_TLS; } }
    else if(certificate.len) { status=q_read_file(e,certpath,&cert);if(!status) { r=gnutls_certificate_set_x509_trust_mem(e->cred,&cert,GNUTLS_X509_FMT_PEM);if(r<=0) status=Q_TLS; } }
    else { r=gnutls_certificate_set_x509_system_trust(e->cred);if(r<=0) status=Q_TLS; }
    if(private_key.data) { volatile uint8_t *p=private_key.data;for(unsigned i=0;i<private_key.size;i++) p[i]=0;qf(private_key.data,e); }qf(cert.data,e);if(status==Q_SYSTEM) r=errno;
    if(status) { q_destroy(e);return qo(NULL,status,r); }return qo(e,Q_OK,0);
}
static const dq_api q_api={1,sizeof(dq_api),NGTCP2_VERSION_NUM,q_create,q_destroy,q_local,q_connect,q_poll,q_next,q_copy,q_ack,q_open_stream,q_write,q_datagram,q_reset,q_disconnect,q_forget,q_migrate,q_stats};
#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
const dq_api *dlm_quic_get_api(uint32_t abi) { return abi==1?&q_api:NULL; }
