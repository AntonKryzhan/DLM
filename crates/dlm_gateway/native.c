#if defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif
#include "../dlm_core/src/native/network.c"
#include "../dlm_core/src/native/nodes.c"
#include "../dlm_core/src/native/adaptive.c"
#include "../dlm_core/src/native/tun.c"
#include <signal.h>
static volatile sig_atomic_t dgn_signal=0;
static void dgn_signal_handler(int value) { (void)value;dgn_signal=1; }
void dgn_signal_start(void) { dgn_signal=0;signal(SIGINT,dgn_signal_handler);signal(SIGTERM,dgn_signal_handler); }
int dgn_interrupted(void) { return (int)dgn_signal; }

/* Reuse the installed DLN cryptographic provider and signed DLR1 locators. */
void dgn_wipe(uint8_t *bytes,size_t count) { if(bytes) dnc_wipe(bytes,count); }
dnn_io dgn_random(uint8_t *out,size_t count) { return dlm_net_random(out,count); }
dnn_io dgn_hash(const uint8_t *data,size_t count,uint8_t *out) { return dlm_net_crypto_hash(NULL,data,count,out,32); }
dnn_open dgn_signer(const uint8_t *root,const uint8_t *current,uint64_t epoch,uint64_t first,uint64_t last) {
    dnn_open a=dlm_node_key(root,32,false),b=dlm_node_key(current,32,false);
    if(a.status || b.status) { dlm_node_key_close(a.handle);dlm_node_key_close(b.handle);return dnn_open_error(a.status?a.status:b.status,0); }
    dnn_open result=dlm_node_signer(a.handle,b.handle,epoch,first,last);dlm_node_key_close(a.handle);return result;
}
void dgn_signer_close(void *p) { dlm_node_signer_close(p); }
dnn_io dgn_sign(void *p,const uint8_t *data,size_t count,uint8_t *out) {
    dnp_signer *s=p;dnp_crypto api;if(!s || s->magic!=DNP_SIGNER_MAGIC || !dnp_crypto_load(&api)) return dnn_result(0,DNN_INVALID,0);
    api.sign(s->key.bytes,s->seed,count,data,out);return dnn_result(64,0,0);
}
int dgn_verify(const uint8_t *key,const uint8_t *data,size_t count,const uint8_t *signature) { return dnp_verify(key,data,count,signature)?0:12; }
dnn_io dgn_record(void *p,dnn_addr address,uint32_t profiles,uint32_t ttl,uint8_t *out,size_t capacity) {
    dnp_locator locator={address,profiles,0};dnp_record_result result=dlm_node_record(p,&locator,1,dlm_node_wall_seconds(),ttl);
    return result.status?dnn_result(0,result.status,result.system):dlm_node_record_encode(result.record,out,capacity);
}
/* The verified public record is returned in its original canonical encoding. */
int dgn_record_verify(const uint8_t *bytes,size_t count) { return dnp_record_decode(bytes,count,dlm_node_wall_seconds()).status; }
dnn_open dgn_ephemeral(uint8_t *public_key) { dnn_open r=dlm_net_crypto_secret(NULL,32,2,true);if(!r.status) { dnn_io p=dlm_net_crypto_public(r.handle,public_key,32);if(p.status) { dlm_net_crypto_close(r.handle);return dnn_open_error(p.status,p.system); } }return r; }
void dgn_secret_close(void *p) { dlm_net_crypto_close(p); }
dnn_open dgn_channel(void *p,const uint8_t *peer,const uint8_t *binding,const uint8_t *session,uint32_t role) {
    dnn_open shared=dlm_net_crypto_agree((dnn_open){p,0,0},peer,32);if(shared.status) return shared;
    return dlm_net_crypto_channel(shared,binding,32,session,32,role,1048576,UINT64_C(1073741824),3600000);
}
dnn_io dgn_seal(void *p,const uint8_t *data,size_t count,const uint8_t *aad,size_t aad_count,uint8_t *out,size_t capacity) { return dlm_net_crypto_seal(p,data,count,aad,aad_count,out,capacity); }
dnn_io dgn_open(void *p,const uint8_t *data,size_t count,const uint8_t *aad,size_t aad_count,uint8_t *out,size_t capacity) { return dlm_net_crypto_open(p,data,count,aad,aad_count,out,capacity); }
void dgn_frame(uint8_t *out,uint32_t count) { dnt_header(out,0,count); }
int dgn_frame_size(const uint8_t *header,uint32_t maximum) { if(memcmp(header,"DLT1",4) || dnt_read32(header+8) || dnt_read32(header+12)) return -1;uint32_t n=dnt_read32(header+4);return n<=maximum?(int)n:-1; }

/* TLS listener support is additive; the existing libcurl TLS client is reused. */
typedef struct {
    int (*init)(void**,unsigned);void (*deinit)(void*);int (*priority)(void*,const char*,const char**);
    int (*credentials_new)(void**);void (*credentials_free)(void*);int (*credentials_file)(void*,const char*,const char*,int);int (*credentials_set)(void*,int,void*);
    void (*transport)(void*,void*);void (*pull)(void*,ptrdiff_t(*)(void*,void*,size_t));void (*push)(void*,ptrdiff_t(*)(void*,const void*,size_t));void (*set_errno)(void*,int);
    int (*handshake)(void*);ptrdiff_t (*read)(void*,void*,size_t);ptrdiff_t (*write)(void*,const void*,size_t);int (*bye)(void*,int);
} dgn_tls_api;
typedef struct { void *socket,*session,*credentials;dgn_tls_api api;bool client,ready; } dgn_tls;
static bool dgn_tls_load(dgn_tls_api *a) {
    if(!dnc_load()) return false;memset(a,0,sizeof *a);void *symbol;
#define DGN_LOAD(member,name) do { symbol=dnn_symbol(dnc.gnutls,name);if(!symbol) return false;memcpy(&a->member,&symbol,sizeof symbol); } while(0)
    DGN_LOAD(init,"gnutls_init");DGN_LOAD(deinit,"gnutls_deinit");DGN_LOAD(priority,"gnutls_priority_set_direct");DGN_LOAD(credentials_new,"gnutls_certificate_allocate_credentials");DGN_LOAD(credentials_free,"gnutls_certificate_free_credentials");DGN_LOAD(credentials_file,"gnutls_certificate_set_x509_key_file");DGN_LOAD(credentials_set,"gnutls_credentials_set");DGN_LOAD(transport,"gnutls_transport_set_ptr");DGN_LOAD(pull,"gnutls_transport_set_pull_function");DGN_LOAD(push,"gnutls_transport_set_push_function");DGN_LOAD(set_errno,"gnutls_transport_set_errno");DGN_LOAD(handshake,"gnutls_handshake");DGN_LOAD(read,"gnutls_record_recv");DGN_LOAD(write,"gnutls_record_send");DGN_LOAD(bye,"gnutls_bye");
#undef DGN_LOAD
    return true;
}
dnn_io dgn_tls_validate(const char *certificate,const char *key) {
    dgn_tls_api api;if(!dgn_tls_load(&api)) return dnn_result(0,DNN_TLS,0);
    void *credentials=NULL;int error=api.credentials_new(&credentials);
    if(!error) error=api.credentials_file(credentials,certificate,key,1);
    if(credentials) api.credentials_free(credentials);
    return dnn_result(0,error?DNN_TLS:0,error);
}
static ptrdiff_t dgn_pull(void *handle,void *bytes,size_t count) { dgn_tls *t=handle;dnn_io r=dlm_net_recv(t->socket,bytes,count);if(r.status==DNN_WOULD_BLOCK) { t->api.set_errno(t->session,EAGAIN);return -1; }if(r.status && r.status!=DNN_EOF) { t->api.set_errno(t->session,EIO);return -1; }return (ptrdiff_t)r.count; }
static ptrdiff_t dgn_push(void *handle,const void *bytes,size_t count) { dgn_tls *t=handle;dnn_io r=dlm_net_send(t->socket,bytes,count);if(r.status==DNN_WOULD_BLOCK) { t->api.set_errno(t->session,EAGAIN);return -1; }if(r.status) { t->api.set_errno(t->session,EIO);return -1; }return (ptrdiff_t)r.count; }
void dgn_tls_close(void *handle) { dgn_tls *t=handle;if(!t) return;if(t->session) { if(t->ready) (void)t->api.bye(t->session,1);t->api.deinit(t->session); }if(t->credentials) t->api.credentials_free(t->credentials);dlm_net_close(t->socket);free(t); }
dnn_open dgn_listen(dnn_addr address) {
    struct sockaddr_storage target;dnn_socklen length=dnn_to_sockaddr(&address,&target);
    if(!length) return dnn_open_error(DNN_INVALID,0);int error=dnn_os_init();if(error) return dnn_open_error(DNN_SYSTEM,error);
    dnn_fd fd=socket(address.family==4?AF_INET:AF_INET6,SOCK_STREAM,0);if(fd==DNN_BAD) return dnn_open_error(DNN_SYSTEM,dnn_socket_error());
#if !defined(_WIN32)
    int reuse=1;if(setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,(const char*)&reuse,sizeof reuse)) { error=dnn_socket_error();dnn_close(fd);return dnn_open_error(DNN_SYSTEM,error); }
#endif
    if(address.family==6) { int only=0;if(setsockopt(fd,IPPROTO_IPV6,IPV6_V6ONLY,(const char*)&only,sizeof only)) { error=dnn_socket_error();dnn_close(fd);return dnn_open_error(DNN_SYSTEM,error); } }
    if(dnn_nonblocking(fd) || bind(fd,(struct sockaddr*)&target,length) || listen(fd,64)) { error=dnn_socket_error();dnn_close(fd);return dnn_open_error(DNN_SYSTEM,error); }
    return dnn_adopt(fd,DNN_LISTENER);
}
dnn_resolved dgn_local(void *socket) { return dlm_net_local(socket); }
void dgn_socket_close(void *p) { dlm_net_close(p); }
dnn_open dgn_tls_accept(void *listener,const char *certificate,const char *key) {
    dnn_open socket=dlm_net_accept(listener);if(socket.status) return socket;dgn_tls *t=calloc(1,sizeof *t);if(!t) { dlm_net_close(socket.handle);return dnn_open_error(DNN_LIMIT,0); }t->socket=socket.handle;
    int error=0;if(!dgn_tls_load(&t->api)) error=-1;if(!error) error=t->api.credentials_new(&t->credentials);if(!error) error=t->api.credentials_file(t->credentials,certificate,key,1);if(!error) error=t->api.init(&t->session,1|8);if(!error) error=t->api.priority(t->session,"NORMAL:-VERS-ALL:+VERS-TLS1.3",NULL);if(!error) error=t->api.credentials_set(t->session,1,t->credentials);
    if(error) { dgn_tls_close(t);return dnn_open_error(DNN_TLS,error); }t->api.transport(t->session,t);t->api.pull(t->session,dgn_pull);t->api.push(t->session,dgn_push);return (dnn_open){t,0,0};
}
dnn_open dgn_tls_connect(dnn_addr peer,dnn_text hostname,dnn_text ca,uint32_t ms) {
    dnt_target target={0};target.peer=peer;target.identity=hostname;target.ca=ca;dnn_open socket=dnt_tls_open(target,ms,NULL);if(socket.status) return socket;dgn_tls *t=calloc(1,sizeof *t);if(!t) { dlm_net_close(socket.handle);return dnn_open_error(DNN_LIMIT,0); }t->socket=socket.handle;t->client=t->ready=true;return (dnn_open){t,0,0};
}
dnn_io dgn_tls_handshake(void *handle) { dgn_tls *t=handle;if(t->ready) return dnn_result(0,0,0);int status=t->api.handshake(t->session);if(!status) t->ready=true;return dnn_result(0,!status?0:(status==-28 || status==-52)?DNN_WOULD_BLOCK:DNN_TLS,status); }
dnn_resolved dgn_tls_local(void *handle) { return dlm_net_local(((dgn_tls*)handle)->socket); }
dnn_io dgn_tls_read(void *handle,uint8_t *out,size_t count) { dgn_tls *t=handle;if(t->client) return dlm_net_recv(t->socket,out,count);ptrdiff_t n=t->api.read(t->session,out,count);return dnn_result(n>0?(size_t)n:0,n>0?0:n==0?DNN_EOF:(n==-28 || n==-52)?DNN_WOULD_BLOCK:DNN_TLS,n<0?(int)n:0); }
dnn_io dgn_tls_write(void *handle,const uint8_t *bytes,size_t count) { dgn_tls *t=handle;if(t->client) return dlm_net_send(t->socket,bytes,count);ptrdiff_t n=t->api.write(t->session,bytes,count);return dnn_result(n>=0?(size_t)n:0,n>=0?0:(n==-28 || n==-52)?DNN_WOULD_BLOCK:DNN_TLS,n<0?(int)n:0); }

/* QUIC retains its pinned provider ABI; Rust serializes endpoint access. */
dq_open dgn_quic_new(dq_addr address,bool server,dq_text certificate,dq_text key,uint32_t connections) {
    dq_config config={connections,8,2048,30000,5000,2,16777216,65536};dq_text alpn={(const uint8_t*)"dlm-gateway/1",13};return dlm_net_quic_open(address,server,alpn,certificate,key,config);
}
dq_resolved dgn_quic_local(void *p) { return dlm_net_quic_local(p); }
dq_ref dgn_quic_connect(void *p,dq_addr peer,dq_text hostname) { return dlm_net_quic_connect(p,peer,hostname); }
dq_io dgn_quic_poll(void *p) { return dlm_net_quic_poll(p,0,NULL); }
dq_event dgn_quic_next(void *p) { return dlm_net_quic_next(p); }
dq_io dgn_quic_copy(void *p,uint64_t id,uint8_t *out,size_t count) { return dlm_net_quic_copy(p,id,0,out,count); }
dq_io dgn_quic_ack(void *p,uint64_t id) { return dlm_net_quic_ack(p,id); }
dq_ref dgn_quic_stream(void *p,uint64_t id) { return dlm_net_quic_stream(p,id,false); }
dq_io dgn_quic_write(void *p,uint64_t id,int64_t stream,const uint8_t *bytes,size_t count) { return dlm_net_quic_write(p,id,stream,bytes,count,false); }
dq_io dgn_quic_datagram(void *p,uint64_t id,const uint8_t *bytes,size_t count) { return dlm_net_quic_datagram(p,id,bytes,count); }
dq_io dgn_quic_disconnect(void *p,uint64_t id) { return dlm_net_quic_disconnect(p,id,0); }
dq_io dgn_quic_forget(void *p,uint64_t id) { return dlm_net_quic_forget(p,id); }
dq_stats dgn_quic_stats(void *p,uint64_t id) { return dlm_net_quic_stats(p,id); }
void dgn_quic_close(void *p) { dlm_net_quic_close(p); }

/* Independent random salt/nonce per durable entry, including after a crash. */
dnn_io dgn_journal_crypt(bool encrypt,const uint8_t *master,const uint8_t *aad,size_t aad_count,const uint8_t *input,size_t count,uint8_t *output,size_t capacity) {
    if(!master || !dnn_buffer(aad,aad_count) || !dnn_buffer(input,count) || !dnn_buffer(output,capacity) || count>1048576 || (!encrypt && count<60)) return dnn_result(0,DNN_INVALID,0);
    size_t needed=encrypt?count+60:count-60;if(capacity<needed) return dnn_result(0,DNN_TRUNCATED,0);uint8_t prefix[44],key[32];
    if(encrypt) { dnn_io random=dlm_net_random(prefix,sizeof prefix);if(random.status) return random; }else memcpy(prefix,input,sizeof prefix);
    dnn_open secret=dlm_net_crypto_secret(master,32,1,false);if(secret.status) return dnn_result(0,secret.status,0);
    int status=dnc_hkdf_bytes(secret.handle,prefix,32,(const uint8_t*)"DLN-GATEWAY-WAL1",16,key,32);dlm_net_crypto_close(secret.handle);if(status) return dnn_result(0,status,0);
    void *cipher=NULL;dnc_datum datum={key,32};int error=dnc.aead_new(&cipher,11,&datum);dnc_wipe(key,32);if(error) return dnn_result(0,DNN_SYSTEM,error);
    size_t produced=encrypt?count+16:count-60;uint8_t *temporary=malloc(produced?produced:1);if(!temporary) { dnc.aead_close(cipher);return dnn_result(0,DNN_LIMIT,0); }
    if(encrypt) error=dnc.encrypt(cipher,prefix+32,12,aad,aad_count,16,input,count,temporary,&produced);
    else error=dnc.decrypt(cipher,prefix+32,12,aad,aad_count,16,input+44,count-44,temporary,&produced);
    if(!error && produced==(encrypt?count+16:needed)) { if(encrypt) { memcpy(output,prefix,44);memcpy(output+44,temporary,produced); }else if(produced) memcpy(output,temporary,produced); }else if(!error) error=-1;
    dnc.aead_close(cipher);dnc_wipe(temporary,encrypt?count+16:needed);free(temporary);return dnn_result(error?0:needed,error?12:0,error);
}
dnn_io dgn_records(void *p,const dnp_locator *entries,size_t count,uint32_t ttl,uint8_t *out,size_t capacity) { dnp_record_result r=dlm_node_record(p,entries,count,dlm_node_wall_seconds(),ttl);return r.status?dnn_result(0,r.status,r.system):dlm_node_record_encode(r.record,out,capacity); }
dnn_io dgn_record_resume(void *p,const uint8_t *bytes,size_t count) { if(count<320 || count>572) return dnn_result(0,DNN_INVALID,0);dnp_record_result r=dnp_record_decode(bytes,count,dnp_read64(bytes+136));return r.status?dnn_result(0,r.status,r.system):dlm_node_signer_resume(p,r.record,dlm_node_wall_seconds()); }
dna_choice dgn_choose(dna_policy policy,const dna_observation *values,size_t count,uint64_t now) { dnt_requirements requirements={4|16|128,0,0};return dlm_adaptive_choose(requirements,policy,values,count,now); }
uint32_t dgn_path_properties(uint32_t profile) { return dna_properties(profile,1); }
dnn_open dgn_tls_connect_source(dnn_addr peer,dnn_text hostname,dnn_text ca,uint32_t ms,dnn_addr source_address) {
 if(!source_address.family) return dgn_tls_connect(peer,hostname,ca,ms);if(source_address.family!=peer.family || source_address.port) return dnn_open_error(DNN_INVALID,0);
 char identity[256],destination[96],local[96],url[320],route[512];if(!hostname.len || hostname.len>=sizeof identity || !hostname.data || memchr(hostname.data,0,hostname.len)) return dnn_open_error(DNN_INVALID,0);memcpy(identity,hostname.data,hostname.len);identity[hostname.len]=0;uint8_t numeric[16];bool v6=inet_pton(AF_INET6,identity,numeric)==1;
 if(!v6) for(size_t i=0;i<hostname.len;i++) { unsigned char c=(unsigned char)identity[i];if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='.' || c=='-')) return dnn_open_error(DNN_INVALID,0); }
 dnn_io a=dlm_net_address_text(peer,(uint8_t*)destination,sizeof destination-1,false),b=dlm_net_address_text(source_address,(uint8_t*)local,sizeof local-1,false);if(a.status || b.status) return dnn_open_error(DNN_INVALID,0);destination[a.count]=0;local[b.count]=0;
 int n=snprintf(url,sizeof url,v6?"https://[%s]:%u":"https://%s:%u",identity,(unsigned)peer.port);if(n<0 || (size_t)n>=sizeof url) return dnn_open_error(DNN_INVALID,0);char host[260];n=snprintf(host,sizeof host,v6?"[%s]":"%s",identity);if(n<0 || (size_t)n>=sizeof host) return dnn_open_error(DNN_INVALID,0);n=snprintf(route,sizeof route,peer.family==6?"%s:%u:[%s]:%u":"%s:%u:%s:%u",host,(unsigned)peer.port,destination,(unsigned)peer.port);if(n<0 || (size_t)n>=sizeof route) return dnn_open_error(DNN_INVALID,0);
 dnn_text u={(const uint8_t*)url,strlen(url)};dnn_open socket=dnn_curl_new(u,ms,DNN_SECURE,ca,(dnn_text){0});if(socket.status) return socket;dnn_socket *s=socket.handle;s->headers=dnn_curl.list_append(NULL,route);int error=s->headers?0:27;
#define DGN_SET(option,value) do { if(!error) error=dnn_curl.setopt(s->curl,option,value); } while(0)
 DGN_SET(10243,s->headers);DGN_SET(10062,local);DGN_SET(141,1L);DGN_SET(84,2L);DGN_SET(226,0L);DGN_SET(43,1L);if(!error) error=dnn_curl.perform(s->curl);if(!error) error=dnn_curl.getinfo(s->curl,0x500000+44,&s->fd);if(!error) { DGN_SET(43,1L);DGN_SET(10062,(const char*)NULL); }
#undef DGN_SET
 if(error) { dlm_net_close(s);return dnn_open_error(dnn_curl_status(error),error); }dgn_tls *t=calloc(1,sizeof*t);if(!t) { dlm_net_close(s);return dnn_open_error(DNN_LIMIT,0); }t->socket=s;t->client=t->ready=true;return (dnn_open){t,0,0};
}
int dgn_record_verify_historical(const uint8_t *bytes,size_t count) { if(count<320 || count>572) return DNN_INVALID;return dnp_record_decode(bytes,count,dnp_read64(bytes+136)).status; }

/* SHA-1 is required only by RFC 6455's public handshake, never by DLN authentication. */
dnn_io dgn_websocket_digest(const uint8_t *bytes,size_t count,uint8_t *out) {
 if(!dnn_buffer(bytes,count) || !out || !dnc_load()) return dnn_result(0,DNN_UNSUPPORTED,0);
 int error=dnc.hash(3,bytes,count,out);return dnn_result(error?0:20,error?DNN_SYSTEM:0,error);
}
dnn_io dgn_tls_http1(void *handle) {
 dgn_tls *t=handle;if(!t || t->client) return dnn_result(0,DNN_INVALID,0);
 int (*alpn)(void*,const dnc_datum*,unsigned,unsigned)=NULL;void *symbol=dnn_symbol(dnc.gnutls,"gnutls_alpn_set_protocols");
 if(!symbol) return dnn_result(0,DNN_UNSUPPORTED,0);memcpy(&alpn,&symbol,sizeof symbol);
 dnc_datum protocol={(uint8_t*)"http/1.1",8};int error=alpn(t->session,&protocol,1,2);return dnn_result(0,error?DNN_TLS:0,error);
}
static dnu_config dgn_ip_config(void) { dnu_config c={0};c.kind=1;c.mode=2;c.mtu=1500;c.max_packet=65535;c.ring_capacity=131072;c.memory=1048576;return c; }
dnn_open dgn_ip_open(dnn_text name,dnn_text library) { return dlm_tun_open(dlm_tun_permission(1),name,library,dgn_ip_config()); }
dnn_open dgn_ip_adopt(int64_t descriptor) { return dlm_tun_adopt(descriptor,dgn_ip_config()); }
dnu_packet dgn_ip_receive(void *handle,uint8_t *out,size_t size) { return dlm_tun_receive(handle,out,size); }
dnn_io dgn_ip_send(void *handle,const uint8_t *data,size_t size) { return dlm_tun_send(handle,data,size); }
dnu_packet dgn_ip_packet(const uint8_t *data,size_t size) { return dlm_tun_packet(1,data,size); }
void dgn_ip_close(void *handle) { dlm_tun_close(handle); }
dnn_io dgn_udp_dont_fragment(int64_t socket_value,uint32_t family) {
 dnn_fd fd=(dnn_fd)socket_value;int result=0,value=1;
#if defined(__linux__)
 if(family==4) { value=IP_PMTUDISC_DO;result=setsockopt(fd,IPPROTO_IP,IP_MTU_DISCOVER,&value,sizeof value); }
 else result=setsockopt(fd,IPPROTO_IPV6,IPV6_DONTFRAG,&value,sizeof value);
#elif defined(_WIN32)
 result=setsockopt(fd,family==4?IPPROTO_IP:IPPROTO_IPV6,family==4?IP_DONTFRAGMENT:IPV6_DONTFRAG,(const char*)&value,sizeof value);
#elif defined(__FreeBSD__)
 result=setsockopt(fd,family==4?IPPROTO_IP:IPPROTO_IPV6,family==4?IP_DONTFRAG:IPV6_DONTFRAG,&value,sizeof value);
#else
 (void)fd;(void)family;(void)value;return dnn_result(0,DNN_UNSUPPORTED,0);
#endif
 return dnn_result(0,result?DNN_SYSTEM:0,result?dnn_socket_error():0);
}

dnn_io dgn_tls_hostname(void *handle,uint8_t *out,size_t capacity) {
 dgn_tls *t=handle;if(!t || t->client || !out || !capacity || capacity>256) return dnn_result(0,DNN_INVALID,0);
 int (*get_name)(void*,void*,size_t*,unsigned*,unsigned)=NULL;void *symbol=dnn_symbol(dnc.gnutls,"gnutls_server_name_get");if(!symbol) return dnn_result(0,DNN_UNSUPPORTED,0);memcpy(&get_name,&symbol,sizeof symbol);
 size_t n=capacity;unsigned type=0;int error=get_name(t->session,out,&n,&type,0);return dnn_result(!error&&type==1?n:0,error||type!=1?DNN_WOULD_BLOCK:0,error);
}
