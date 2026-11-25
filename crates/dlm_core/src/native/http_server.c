/* The owner drives sockets through the existing network provider. Handler data
 * crosses the executor boundary through ordinary DLM channels, never socket handles. */
#define DNH_FIELDS 64
#define DNH_CONNECTIONS 256
#define DNH_MEMORY ((size_t)134217728)
enum { DNH_HEAD, DNH_READY, DNH_ACTIVE, DNH_WS, DNH_HANDSHAKE, DNH_TLS_CLOSE };
typedef struct { size_t start, length; } dnh_span;
typedef struct { dnh_span name,value; } dnh_header;
typedef struct {
    size_t headers, body, chunk, output;
    uint32_t connections, requests, deadline, idle;
} dnh_limits;
typedef struct {
    uint64_t id;
    size_t method, target, headers, body;
    bool keep_alive, chunked, websocket;
    int32_t status;
} dnh_request;
typedef struct dnh_tls dnh_tls;
typedef struct {
    dnn_socket *socket;
    dnh_tls *tls;
    void *session;
    uint8_t *input,*output;
    size_t rx_len, scan, tx_pos, tx_len, send_retry;
    uint64_t id,deadline;
    uint32_t phase,requests,wait_read,wait_write;
    dnh_span method,target;
    dnh_header field[DNH_FIELDS];
    size_t fields,head,content_length,remaining,total;
    uint32_t chunk_state,trailers;
    bool keep_alive,chunked,body_done,head_taken,old_http,expect,upgradable;
    bool response,response_done,response_chunked,no_body,close_after,peer_eof,waiting_for_head;
    uint64_t response_remaining;
    uint8_t ws_key[24];
    uint64_t ws_remaining,ws_offset,ws_message;
    uint8_t ws_mask[4],ws_opcode,ws_fragment,ws_out_fragment;
    uint32_t ws_utf8,ws_codepoint,ws_minimum;
    uint32_t ws_out_utf8,ws_out_codepoint,ws_out_minimum;
    bool ws_fin,ws_frame,ws_close;
} dnh_conn;
typedef struct {
    dnn_socket *listener;
    dnh_tls *tls;
    dnh_conn *conn;
    dnh_limits limits;
    size_t input_capacity;
    uint64_t owner,sequence,stop_at;
    uint32_t next;
    bool closing;
} dnh_server;

static bool dnh_token(uint8_t c) {
    return (c>='0'&&c<='9') || (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c && strchr("!#$%&'*+-.^_`|~",c));
}
static uint8_t dnh_lower(uint8_t c) { return c>='A'&&c<='Z'?c+32:c; }
static bool dnh_equal(const uint8_t *p,size_t n,const char *s) {
    size_t len=strlen(s);
    if (len!=n) return false;
    for (size_t i=0;i<n;i++) if (dnh_lower(p[i])!=(uint8_t)s[i]) return false;
    return true;
}
static bool dnh_ows(uint8_t c) { return c==' ' || c=='\t'; }
static bool dnh_list_has(const uint8_t *p,size_t n,const char *want) {
    size_t i=0;
    while (i<n) {
        while (i<n && (dnh_ows(p[i]) || p[i]==',')) i++;
        size_t first=i;
        while (i<n && p[i]!=',') i++;
        size_t end=i;
        while (end>first && dnh_ows(p[end-1])) end--;
        if (dnh_equal(p+first,end-first,want)) return true;
    }
    return false;
}
static size_t dnh_line(const uint8_t *bytes,size_t first,size_t end) {
    for (size_t i=first;i+1<end;i++) if (bytes[i]=='\r' && bytes[i+1]=='\n') return i;
    return SIZE_MAX;
}
static bool dnh_field(const uint8_t *bytes,size_t first,size_t end,dnh_header *out) {
    size_t i=first;
    while (i<end && dnh_token(bytes[i])) i++;
    if (i==first || i==end || bytes[i]!=':') return false;
    out->name=(dnh_span){first,i-first};
    i++;
    while (i<end && dnh_ows(bytes[i])) i++;
    size_t value=i;
    for (;i<end;i++) if ((bytes[i]<32 && bytes[i]!='\t') || bytes[i]==127) return false;
    while (end>value && dnh_ows(bytes[end-1])) end--;
    out->value=(dnh_span){value,end-value};
    return true;
}
static int dnh_b64(uint8_t c) {
    if (c>='A'&&c<='Z') return c-'A';
    if (c>='a'&&c<='z') return c-'a'+26;
    if (c>='0'&&c<='9') return c-'0'+52;
    return c=='+'?62:c=='/'?63:-1;
}
static bool dnh_ws_key(const uint8_t *p,size_t n) {
    if (n!=24 || p[22]!='=' || p[23]!='=') return false;
    for (size_t i=0;i<22;i++) if (dnh_b64(p[i])<0) return false;
    return !(dnh_b64(p[21]) & 15);
}
static unsigned dnh_parse_head(dnh_conn *c,const dnh_limits *limits) {
    uint8_t *in=c->input;
    size_t end=dnh_line(in,0,c->head);
    if (end==SIZE_MAX) return 400;
    size_t first=0;
    while (first<end && dnh_token(in[first])) first++;
    if (!first || first>32 || first==end || in[first]!=' ') return 400;
    c->method=(dnh_span){0,first};
    size_t target=first+1;
    first=target;
    while (first<end && in[first]!=' ') {
        if (in[first]<=32 || in[first]>=127 || in[first]=='#') return 400;
        first++;
    }
    if (first==target || first+9!=end) return 400;
    if (in[target]!='/' && !(first==target+1 && in[target]=='*' && target==8 && !memcmp(in,"OPTIONS",7))) return 400;
    c->target=(dnh_span){target,first-target};
    if (memcmp(in+first+1,"HTTP/1.",7) || (in[end-1]!='0' && in[end-1]!='1')) return 505;
    c->old_http=in[end-1]=='0';
    bool host=false, length=false, transfer=false, close=false, keep=false;
    bool upgrade=false, websocket=false,key=false,version=false,expect=false;
    for (size_t i=end+2;i+2<c->head;) {
        end=dnh_line(in,i,c->head);
        if (end==SIZE_MAX || c->fields==DNH_FIELDS) return 431;
        dnh_header *header=&c->field[c->fields++];
        if (!dnh_field(in,i,end,header)) return 400;
        i=end+2;
        const uint8_t *name=in+header->name.start,*value=in+header->value.start;
        size_t n=header->name.length,v=header->value.length;
        if (dnh_equal(name,n,"host")) {
            if (host || !v) return 400;
            for (size_t k=0;k<v;k++) if (value[k]<=32 || value[k]>=127 || strchr("/@,?#\\",value[k])) return 400;
            host=true;
        } else if (dnh_equal(name,n,"content-length")) {
            if (length || !v) return 400;
            length=true;
            size_t bytes=0;
            for (size_t k=0;k<v;k++) {
                if (value[k]<'0' || value[k]>'9') return 400;
                size_t digit=value[k]-'0';
                if (digit>limits->body || bytes>(limits->body-digit)/10) return 413;
                bytes=bytes*10+digit;
            }
            c->content_length=bytes;
        } else if (dnh_equal(name,n,"transfer-encoding")) {
            if (transfer || !dnh_equal(value,v,"chunked") || c->old_http) return 400;
            transfer=true;
        } else if (dnh_equal(name,n,"connection")) {
            close|=dnh_list_has(value,v,"close"); keep|=dnh_list_has(value,v,"keep-alive");
            upgrade|=dnh_list_has(value,v,"upgrade");
        } else if (dnh_equal(name,n,"expect")) {
            if (expect || c->old_http || !dnh_equal(value,v,"100-continue")) return 417;
            expect=true;
        } else if (dnh_equal(name,n,"upgrade")) {
            websocket|=dnh_list_has(value,v,"websocket");
        } else if (dnh_equal(name,n,"sec-websocket-key")) {
            if (key || !dnh_ws_key(value,v)) return 400;
            key=true; memcpy(c->ws_key,value,24);
        } else if (dnh_equal(name,n,"sec-websocket-version")) {
            if (version || !dnh_equal(value,v,"13")) return 400;
            version=true;
        }
    }
    if ((!host && !c->old_http) || (transfer && length)) return 400;
    c->chunked=transfer; c->remaining=c->content_length; c->body_done=!transfer && !c->remaining;
    c->keep_alive=!close && (!c->old_http || keep);
    c->expect=expect && !c->body_done;
    c->upgradable=websocket && upgrade && key && version && c->body_done && !c->old_http && c->method.length==3 && !memcmp(in,"GET",3);
    c->no_body=c->method.length==4 && !memcmp(in,"HEAD",4);
    return 0;
}

#define DNH_TLS_API(X) \
  X(int,global_init,(void)) \
  X(void,global_deinit,(void)) \
  X(int,certificate_allocate_credentials,(void **)) \
  X(void,certificate_free_credentials,(void *)) \
  X(int,certificate_set_x509_key_file,(void *,const char*,const char*,int)) \
  X(int,init,(void **,unsigned)) \
  X(void,deinit,(void *)) \
  X(int,priority_set_direct,(void *,const char *,const char **)) \
  X(int,credentials_set,(void *,int,void *)) \
  X(void,transport_set_ptr,(void *,void *)) \
  X(void,transport_set_errno,(void *,int)) \
  X(void,transport_set_pull_function,(void *,intptr_t(*)(void *,void *,size_t))) \
  X(void,transport_set_push_function,(void *,intptr_t(*)(void *,const void *,size_t))) \
  X(int,handshake,(void *)) \
  X(int,bye,(void *,int)) \
  X(int,record_get_direction,(void *)) \
  X(size_t,record_check_pending,(void *)) \
  X(intptr_t,record_recv,(void *,void *,size_t)) \
  X(intptr_t,record_send,(void *,const void *,size_t))
#define DNH_TLS_FIELD(ret,name,args) ret (*name) args;
struct dnh_tls { void *library,*credentials; bool global; DNH_TLS_API(DNH_TLS_FIELD) };
#undef DNH_TLS_FIELD
static void dnh_tls_clear(dnh_tls *tls) {
    if (!tls) return;
    if (tls->credentials) tls->certificate_free_credentials(tls->credentials);
    if (tls->global) tls->global_deinit();
#if defined(_WIN32)
    if (tls->library) FreeLibrary((HMODULE)tls->library);
#else
    if (tls->library) dlclose(tls->library);
#endif
    free(tls);
}
static char *dnh_path(dnn_text path) {
    if (!path.data || !path.len || path.len>16384 || memchr(path.data,0,path.len)) return NULL;
    char *result=malloc(path.len+1);
    if (result) { memcpy(result,path.data,path.len); result[path.len]=0; }
    return result;
}
static dnh_tls *dnh_tls_open(dnn_text certificate,dnn_text key,int *error) {
    dnh_tls *tls=calloc(1,sizeof *tls);
    if (!tls) { *error=DNN_LIMIT; return NULL; }
#if defined(_WIN32)
    const char *names[]={"libgnutls-30.dll","gnutls-30.dll",NULL};
#elif defined(__APPLE__)
    const char *names[]={"libgnutls.30.dylib",NULL};
#else
    const char *names[]={"libgnutls.so.30",NULL};
#endif
    tls->library=dnc_library("DLM_LIBGNUTLS",L"DLM_LIBGNUTLS",names);
    if (!tls->library) { *error=DNN_UNSUPPORTED; dnh_tls_clear(tls); return NULL; }
    bool complete=true;
#define DNH_LOAD(ret,name,args) do { *(void **)(&tls->name)=dnn_symbol(tls->library,"gnutls_" #name); if (!tls->name) complete=false; } while (0);
    DNH_TLS_API(DNH_LOAD)
#undef DNH_LOAD
    char *crt=dnh_path(certificate),*private_key=dnh_path(key);
    if (complete && crt && private_key && !tls->global_init()) {
        tls->global=true;
        if (!tls->certificate_allocate_credentials(&tls->credentials) && !tls->certificate_set_x509_key_file(tls->credentials,crt,private_key,1)) {
            free(crt); free(private_key); return tls;
        }
    }
    *error=DNN_TLS;
    free(crt); free(private_key);
    dnh_tls_clear(tls);
    return NULL;
}
static void dnh_tls_errno(dnh_conn *c) {
    int e=dnn_socket_error();
    c->tls->transport_set_errno(c->session,DNN_AGAIN(e)?EAGAIN:DNN_INTR(e)?EINTR:EIO);
}
static intptr_t dnh_tls_pull(void *raw,void *out,size_t len) {
    dnh_conn *c=raw;
    int n=(int)recv(c->socket->fd,(char*)out,(int)(len>INT_MAX?INT_MAX:len),0);
    if (n<0) dnh_tls_errno(c);
    return n;
}
static intptr_t dnh_tls_push(void *raw,const void *in,size_t len) {
    dnh_conn *c=raw;
    int n=(int)send(c->socket->fd,(const char*)in,(int)(len>INT_MAX?INT_MAX:len),MSG_NOSIGNAL);
    if (n<0) dnh_tls_errno(c);
    return n;
}
static bool dnh_tls_session(dnh_conn *c,dnh_tls *tls) {
    c->tls=tls;
    if (tls->init(&c->session,1 | 8 | 64) || tls->priority_set_direct(c->session,"NORMAL:-VERS-ALL:+VERS-TLS1.3:+VERS-TLS1.2",NULL)
        || tls->credentials_set(c->session,1,tls->credentials)) return false;
    tls->transport_set_ptr(c->session,c);
    tls->transport_set_pull_function(c->session,dnh_tls_pull);
    tls->transport_set_push_function(c->session,dnh_tls_push);
    c->phase=DNH_HANDSHAKE;
    return true;
}
static bool dnh_handshake(dnh_conn *c) {
    int r=c->tls->handshake(c->session);
    if (!r) { c->phase=DNH_HEAD; c->wait_read=1; return true; }
    if (r!=-28 && r!=-52) return false;
    c->wait_read=c->tls->record_get_direction(c->session)?2:1;
    return true;
}
static bool dnh_tls_bye(dnh_conn *c) {
    int rc=c->tls->bye(c->session,1);
    if (rc!=-28 && rc!=-52) return false;
    c->wait_write=c->tls->record_get_direction(c->session)?2:1;
    return true;
}

static bool dnh_owner(dnh_server *s) { return s && s->owner==dn_thread_id(); }
static dnh_conn *dnh_get(dnh_server *s,uint64_t id) {
    if (!dnh_owner(s) || !id) return NULL;
    for (uint32_t i=0;i<s->limits.connections;i++) if (s->conn[i].socket && s->conn[i].id==id) return &s->conn[i];
    return NULL;
}
static void dnh_close(dnh_conn *c) {
    if (c->session) c->tls->deinit(c->session);
    if (c->socket) dlm_net_close(c->socket);
    free(c->input); free(c->output);
    memset(c,0,sizeof *c);
}
static size_t dnh_room(dnh_server *s,dnh_conn *c) {
    if (!c->send_retry && c->tx_pos) {
        c->tx_len-=c->tx_pos;
        memmove(c->output,c->output+c->tx_pos,c->tx_len);
        c->tx_pos=0;
    }
    return s->limits.output-c->tx_len;
}
static void dnh_append(dnh_conn *c,const void *bytes,size_t n) {
    if (n) memcpy(c->output+c->tx_len,bytes,n);
    c->tx_len+=n;
}
static void dnh_consume(dnh_conn *c,size_t n) {
    c->rx_len-=n;
    if (c->rx_len) memmove(c->input,c->input+n,c->rx_len);
    c->scan=0;
}
static const char *dnh_phrase(unsigned status) {
    switch (status) {
        case 200:return "OK"; case 201:return "Created"; case 202:return "Accepted"; case 204:return "No Content";
        case 206:return "Partial Content"; case 301:return "Moved Permanently"; case 302:return "Found";
        case 303:return "See Other"; case 304:return "Not Modified"; case 307:return "Temporary Redirect";
        case 308:return "Permanent Redirect"; case 400:return "Bad Request"; case 401:return "Unauthorized";
        case 403:return "Forbidden"; case 404:return "Not Found"; case 405:return "Method Not Allowed";
        case 408:return "Request Timeout"; case 409:return "Conflict"; case 411:return "Length Required";
        case 413:return "Content Too Large"; case 414:return "URI Too Long"; case 415:return "Unsupported Media Type";
        case 417:return "Expectation Failed"; case 422:return "Unprocessable Content"; case 429:return "Too Many Requests";
        case 431:return "Request Header Fields Too Large"; case 500:return "Internal Server Error";
        case 502:return "Bad Gateway"; case 503:return "Service Unavailable"; case 504:return "Gateway Timeout";
        case 505:return "HTTP Version Not Supported"; default:return "";
    }
}
static void dnh_reject(dnh_server *s,dnh_conn *c,unsigned status) {
    if (c->response || c->phase==DNH_WS) { c->close_after=true; c->response_done=true; return; }
    char message[192];
    int n=snprintf(message,sizeof message,"HTTP/1.1 %u %s\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",status,dnh_phrase(status));
    if (n>0 && (size_t)n<=dnh_room(s,c)) dnh_append(c,message,(size_t)n);
    c->response=true; c->close_after=true; c->response_done=true;
    c->deadline=dnc_now()+s->limits.idle;
}
static void dnh_read_head(dnh_server *s,dnh_conn *c) {
    size_t bound=c->rx_len<s->limits.headers?c->rx_len:s->limits.headers;
    for (size_t i=c->scan;i+3<bound;i++) {
        if (memcmp(c->input+i,"\r\n\r\n",4)) continue;
        c->head=i+4;
        unsigned status=dnh_parse_head(c,&s->limits);
        if (status) dnh_reject(s,c,status);
        else c->phase=DNH_READY;
        return;
    }
    c->scan=bound>=3?bound-3:0;
    if (bound==s->limits.headers) dnh_reject(s,c,431);
}
static bool dnh_can_read(dnh_server *s,dnh_conn *c) {
    return !c->peer_eof && !c->close_after && c->rx_len<s->input_capacity
        && (c->phase==DNH_HEAD || c->phase==DNH_ACTIVE || c->phase==DNH_WS);
}
static bool dnh_receive(dnh_server *s,dnh_conn *c) {
    size_t room=s->input_capacity-c->rx_len;
    size_t count=room<s->limits.chunk?room:s->limits.chunk;
    if (!count) return true;
    uint8_t *input=c->input+c->rx_len;
    size_t n=0;
    if (c->session) {
        intptr_t r=c->tls->record_recv(c->session,input,count);
        if (r==-28 || r==-52) { c->wait_read=c->tls->record_get_direction(c->session)?2:1; return true; }
        if (r<0) return false;
        n=(size_t)r;
    } else {
        dnn_io r=dlm_net_recv(c->socket,input,count);
        if (r.status==DNN_WOULD_BLOCK) return true;
        if (r.status!=DNN_OK && r.status!=DNN_EOF) return false;
        n=r.count;
    }
    c->wait_read=1;
    if (!n) {
        c->peer_eof=true; c->keep_alive=false;
        if (c->phase==DNH_WS) return false;
        if (c->phase==DNH_HEAD && !c->rx_len) return false;
    }
    c->rx_len+=n;
    if (c->phase==DNH_HEAD) {
        if (n && c->waiting_for_head) { c->waiting_for_head=false; c->deadline=dnc_now()+s->limits.deadline; }
        dnh_read_head(s,c);
        if (c->phase==DNH_HEAD && c->peer_eof) return false;
    }
    return true;
}
static bool dnh_send(dnh_conn *c) {
    size_t count=c->send_retry?c->send_retry:c->tx_len-c->tx_pos;
    const uint8_t *bytes=c->output+c->tx_pos;
    if (!count) return true;
    size_t n;
    if (c->session) {
        intptr_t r=c->tls->record_send(c->session,bytes,count);
        if (r==-28 || r==-52) { c->send_retry=count; c->wait_write=c->tls->record_get_direction(c->session)?2:1; return true; }
        if (r<=0) return false;
        c->send_retry=0; n=(size_t)r;
    } else {
        dnn_io r=dlm_net_send(c->socket,bytes,count);
        if (r.status==DNN_WOULD_BLOCK) return true;
        if (r.status!=DNN_OK || !r.count) return false;
        n=r.count;
    }
    c->wait_write=2;
    c->tx_pos+=n;
    if (c->tx_pos==c->tx_len) c->tx_pos=c->tx_len=0;
    return true;
}
static void dnh_next(dnh_server *s,dnh_conn *c) {
    if (c->close_after || !c->body_done || c->peer_eof || s->closing || c->requests>=s->limits.requests || s->sequence==UINT64_MAX) {
        if (c->session) {
            c->response_done=false; c->id=0; c->phase=DNH_TLS_CLOSE; c->deadline=dnc_now()+250;
            if (dnh_tls_bye(c)) return;
        }
        dnh_close(c); return;
    }
    dnh_conn next={0};
    next.socket=c->socket; next.tls=c->tls; next.session=c->session;
    next.input=c->input; next.output=c->output; next.rx_len=c->rx_len;
    next.requests=c->requests; next.id=++s->sequence;
    next.wait_read=1; next.wait_write=2;
    next.waiting_for_head=!next.rx_len;
    next.deadline=dnc_now()+(next.rx_len?s->limits.deadline:s->limits.idle);
    *c=next;
    dnh_read_head(s,c);
}
static void dnh_accept(dnh_server *s,dnh_conn *c) {
    dnn_open accepted=dlm_net_accept(s->listener);
    if (accepted.status!=DNN_OK) return;
    c->socket=accepted.handle;
    c->input=malloc(s->input_capacity);
    c->output=malloc(s->limits.output);
    c->id=++s->sequence; c->deadline=dnc_now()+s->limits.deadline;
    c->wait_read=1; c->wait_write=2;
    if (!c->input || !c->output || (s->tls && !dnh_tls_session(c,s->tls))) dnh_close(c);
}

dnn_open dlm_http_server_open(dnn_addr address,dnh_limits limits,dnn_text certificate,dnn_text key) {
    if (limits.headers<256 || limits.headers>65536 || !limits.body || limits.body>DNN_MAX
        || limits.chunk<256 || limits.chunk>65536 || limits.output<1024 || limits.output>1048576
        || !limits.connections || limits.connections>DNH_CONNECTIONS || !limits.requests || !limits.deadline || !limits.idle
        || limits.deadline>3600000 || limits.idle>3600000 || (limits.headers+limits.chunk+limits.output)>DNH_MEMORY/limits.connections
        || !!certificate.len!=!!key.len) return dnn_open_error(DNN_INVALID,0);
    dnh_tls *tls=NULL;
    if (certificate.len) {
        int error=0;
        tls=dnh_tls_open(certificate,key,&error);
        if (!tls) return dnn_open_error(error,0);
    }
    dnn_open listener=dlm_net_bind_address(address,DNN_LISTENER,limits.connections,false);
    if (listener.status!=DNN_OK) { dnh_tls_clear(tls); return listener; }
    dnh_server *s=calloc(1,sizeof *s);
    dnh_conn *c=calloc(limits.connections,sizeof *c);
    if (!s || !c) { free(s); free(c); dlm_net_close(listener.handle); dnh_tls_clear(tls); return dnn_open_error(DNN_LIMIT,0); }
    *s=(dnh_server){.listener=listener.handle,.tls=tls,.conn=c,.limits=limits,.input_capacity=limits.headers+limits.chunk,.owner=dn_thread_id()};
    return (dnn_open){s,DNN_OK,0};
}
void dlm_http_server_close(void *raw) {
    dnh_server *s=raw;
    if (!dnh_owner(s)) return;
    for (uint32_t i=0;i<s->limits.connections;i++) if (s->conn[i].socket) dnh_close(&s->conn[i]);
    if (s->listener) dlm_net_close(s->listener);
    dnh_tls_clear(s->tls);
    free(s->conn); free(s);
}
dnn_resolved dlm_http_server_local(void *raw) {
    dnh_server *s=raw;
    return dlm_net_local(dnh_owner(s)?s->listener:NULL);
}
dnn_io dlm_http_server_stop(void *raw,uint32_t grace_ms) {
    dnh_server *s=raw;
    if (!dnh_owner(s) || grace_ms>3600000) return dnn_result(0,DNN_INVALID,0);
    if (!s->closing) {
        s->closing=true; s->stop_at=dnc_now()+grace_ms;
        dlm_net_close(s->listener); s->listener=NULL;
    }
    return dnn_result(0,DNN_OK,0);
}
dnn_io dlm_http_server_poll(void *raw,uint32_t milliseconds,void *cancel) {
    dnh_server *s=raw;
    if (!dnh_owner(s) || milliseconds>1000) return dnn_result(0,DNN_INVALID,0);
    if (dnn_cancelled(cancel)) return dnn_result(0,DNN_CANCELLED,0);
    uint64_t now=dnc_now(),deadline=now+milliseconds;
    dnn_poll polls[DNH_CONNECTIONS+1];
    dnh_conn *entries[DNH_CONNECTIONS+1];
    size_t count=0,live=0;
    dnh_conn *available=NULL;
    for (uint32_t i=0;i<s->limits.connections;i++) {
        dnh_conn *c=&s->conn[i];
        if (c->socket && ((s->closing && (now>=s->stop_at || c->phase==DNH_HEAD || c->phase==DNH_HANDSHAKE)) || now>=c->deadline)) {
            if (!s->closing && c->phase==DNH_HEAD && c->rx_len && !c->close_after) dnh_reject(s,c,408);
            else dnh_close(c);
        }
        if (c->socket && c->response_done && c->tx_pos==c->tx_len) dnh_next(s,c);
        if (!c->socket) { if (!available) available=c; continue; }
        live++;
        if (c->deadline<deadline) deadline=c->deadline;
        if (c->session && dnh_can_read(s,c) && c->wait_read==1 && c->tls->record_check_pending(c->session)) {
            if (!dnh_receive(s,c)) { dnh_close(c); live--; continue; }
            deadline=now;
        }
        uint32_t events=0;
        if (c->phase==DNH_HANDSHAKE || dnh_can_read(s,c)) events|=c->wait_read;
        if (c->phase==DNH_TLS_CLOSE || c->tx_pos<c->tx_len) events|=c->wait_write;
        if (events) { polls[count]=(dnn_poll){.socket=c->socket,.events=events}; entries[count++]=c; }
    }
    if (available && s->listener && s->sequence<UINT64_MAX) {
        polls[count]=(dnn_poll){.socket=s->listener,.events=1}; entries[count++]=NULL;
    }
    if (s->closing && s->stop_at<deadline) deadline=s->stop_at;
    if (!count) return dnn_result(live,DNN_OK,0);
    uint32_t wait=deadline>now?(uint32_t)(deadline-now):0;
    dnn_io result=dlm_net_poll(polls,count,wait,cancel);
    if (result.status!=DNN_OK && result.status!=DNN_TIMEOUT) return result;
    for (size_t i=0;i<count;i++) {
        uint32_t ready=polls[i].ready;
        if (!ready) continue;
        dnh_conn *c=entries[i];
        if (!c) { dnh_accept(s,available); if (available->socket) live++; continue; }
        if (ready&4) { dnh_close(c); live--; }
        else if (c->phase==DNH_TLS_CLOSE) {
            if (!dnh_tls_bye(c)) { dnh_close(c); live--; }
        } else if (c->phase==DNH_HANDSHAKE) {
            if (!dnh_handshake(c)) { dnh_close(c); live--; }
        } else {
            if ((c->tx_pos<c->tx_len && (ready & (c->wait_write|12)) && !dnh_send(c))
                || (dnh_can_read(s,c) && (ready & (c->wait_read|12)) && !dnh_receive(s,c))) { dnh_close(c); live--; }
        }
    }
    return dnn_result(live,DNN_OK,0);
}

dnh_request dlm_http_next_request(void *raw) {
    dnh_server *s=raw;
    dnh_request result={.status=DNN_WOULD_BLOCK};
    if (!dnh_owner(s)) { result.status=DNN_INVALID; return result; }
    for (uint32_t i=0;i<s->limits.connections;i++) {
        uint32_t idx=(s->next+i)%s->limits.connections;
        dnh_conn *c=&s->conn[idx];
        if (!c->socket || c->phase!=DNH_READY || c->response) continue;
        if (c->expect) {
            static const char message[]="HTTP/1.1 100 Continue\r\n\r\n";
            if (dnh_room(s,c)<sizeof message-1) continue;
            dnh_append(c,message,sizeof message-1);
        }
        c->phase=DNH_ACTIVE; c->requests++;
        s->next=(idx+1)%s->limits.connections;
        return (dnh_request){c->id,c->method.length,c->target.length,c->fields,c->content_length,c->keep_alive,c->chunked,c->upgradable,DNN_OK};
    }
    return result;
}
dnn_io dlm_http_request_part(void *raw,uint64_t id,uint32_t part,size_t index,uint8_t *out,size_t cap) {
    dnh_conn *c=dnh_get(raw,id);
    if (!c || c->phase!=DNH_ACTIVE || c->head_taken || part>3 || (part>=2 && index>=c->fields)) return dnn_result(0,DNN_INVALID,0);
    dnh_span text=part==0?c->method:part==1?c->target:part==2?c->field[index].name:c->field[index].value;
    if (out && cap<text.length) return dnn_result(text.length,DNN_LIMIT,0);
    if (out && text.length) memcpy(out,c->input+text.start,text.length);
    return dnn_result(text.length,DNN_OK,0);
}
dnn_io dlm_http_request_taken(void *raw,uint64_t id) {
    dnh_conn *c=dnh_get(raw,id);
    if (!c || c->phase!=DNH_ACTIVE || c->head_taken) return dnn_result(0,DNN_INVALID,0);
    dnh_consume(c,c->head); c->head_taken=true;
    return dnn_result(0,DNN_OK,0);
}

static dnn_io dnh_bad_body(dnh_server *s,dnh_conn *c,int code) {
    dnh_reject(s,c,code==DNN_LIMIT?413:400);
    return dnn_result(0,code,0);
}
static size_t dnh_chunk_length(const uint8_t *p,size_t n,size_t maximum) {
    size_t bytes=0,i=0;
    for (;i<n && p[i]!=';';i++) {
        uint8_t c=dnh_lower(p[i]);
        unsigned digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:16;
        if (digit==16) return SIZE_MAX;
        if (digit>maximum || bytes>(maximum-digit)/16) return SIZE_MAX-1;
        bytes=bytes*16+digit;
    }
    if (!i) return SIZE_MAX;
    for (;i<n;i++) if (p[i]<32 || p[i]>=127) return SIZE_MAX;
    return bytes;
}
dnn_io dlm_http_body(void *raw,uint64_t id,uint8_t *out,size_t cap) {
    dnh_server *s=raw;
    dnh_conn *c=dnh_get(s,id);
    if (!c || c->phase!=DNH_ACTIVE || !c->head_taken || !out || !cap || cap>s->limits.chunk) return dnn_result(0,DNN_INVALID,0);
    if (c->body_done) return dnn_result(0,DNN_EOF,0);
    for (;;) {
        if (!c->chunked || c->chunk_state==1) {
            size_t n=c->remaining<cap?c->remaining:cap;
            if (n>c->rx_len) n=c->rx_len;
            if (!n) break;
            memcpy(out,c->input,n); dnh_consume(c,n);
            c->remaining-=n;
            if (!c->remaining) { if (c->chunked) c->chunk_state=2; else c->body_done=true; }
            return dnn_result(n,DNN_OK,0);
        }
        if (c->chunk_state==2) {
            if (c->rx_len<2) break;
            if (memcmp(c->input,"\r\n",2)) return dnh_bad_body(s,c,DNN_PROTOCOL);
            dnh_consume(c,2); c->chunk_state=0;
        }
        size_t end=dnh_line(c->input,0,c->rx_len);
        if (end==SIZE_MAX) {
            if (c->rx_len>=1024) return dnh_bad_body(s,c,DNN_PROTOCOL);
            break;
        }
        if (end>=1024) return dnh_bad_body(s,c,DNN_PROTOCOL);
        if (c->chunk_state==3) {
            if (!end) { dnh_consume(c,2); c->body_done=true; return dnn_result(0,DNN_EOF,0); }
            dnh_header trailer;
            if (++c->trailers>DNH_FIELDS || !dnh_field(c->input,0,end,&trailer)) return dnh_bad_body(s,c,DNN_PROTOCOL);
            const uint8_t *name=c->input+trailer.name.start;
            size_t n=trailer.name.length;
            if (dnh_equal(name,n,"transfer-encoding") || dnh_equal(name,n,"content-length") || dnh_equal(name,n,"host")
                || dnh_equal(name,n,"connection") || dnh_equal(name,n,"authorization")) return dnh_bad_body(s,c,DNN_PROTOCOL);
        } else {
            size_t n=dnh_chunk_length(c->input,end,s->limits.body-c->total);
            if (n>=SIZE_MAX-1) return dnh_bad_body(s,c,n==SIZE_MAX-1?DNN_LIMIT:DNN_PROTOCOL);
            c->chunk_state=n?1:3; c->remaining=n; c->total+=n;
        }
        dnh_consume(c,end+2);
    }
    if (c->peer_eof) return dnh_bad_body(s,c,DNN_TRUNCATED);
    return dnn_result(0,DNN_WOULD_BLOCK,0);
}

static bool dnh_output_headers(const uint8_t *bytes,size_t count) {
    if (!count) return true;
    if (!bytes) return false;
    size_t fields=0;
    for (size_t p=0;p<count;) {
        dnh_header header;
        size_t end=dnh_line(bytes,p,count);
        if (end==SIZE_MAX || ++fields>DNH_FIELDS || !dnh_field(bytes,p,end,&header)) return false;
        const uint8_t *name=bytes+header.name.start;
        size_t n=header.name.length;
        if (dnh_equal(name,n,"content-length") || dnh_equal(name,n,"transfer-encoding") || dnh_equal(name,n,"connection") || dnh_equal(name,n,"upgrade")) return false;
        p=end+2;
    }
    return true;
}
dnn_io dlm_http_response_start(void *raw,uint64_t id,uint32_t status,dnn_text headers,uint64_t size,bool chunked,bool close) {
    dnh_server *s=raw;
    dnh_conn *c=dnh_get(s,id);
    if (!c || c->phase!=DNH_ACTIVE || !c->head_taken || c->response || status<200 || status>599 || (chunked && c->old_http)
        || headers.len>s->limits.headers || !dnh_output_headers(headers.data,headers.len)) return dnn_result(0,DNN_INVALID,0);
    bool keep=c->keep_alive && !close && !s->closing && c->requests<s->limits.requests;
    bool nobody=c->no_body || status==204 || status==205 || status==304;
    char prefix[112],suffix[128];
    int a=snprintf(prefix,sizeof prefix,"HTTP/1.%u %u %s\r\n",c->old_http?0:1,status,dnh_phrase(status));
    int b=snprintf(suffix,sizeof suffix,"Connection: %s\r\n",keep?"keep-alive":"close");
    if (status!=204 && status!=304) {
        if (status==205) b+=snprintf(suffix+b,sizeof suffix-(size_t)b,"Content-Length: 0\r\n");
        else if (chunked && !nobody) b+=snprintf(suffix+b,sizeof suffix-(size_t)b,"Transfer-Encoding: chunked\r\n");
        else if (!chunked) b+=snprintf(suffix+b,sizeof suffix-(size_t)b,"Content-Length: %" PRIu64 "\r\n",size);
    }
    size_t needed=(size_t)(a+b)+headers.len+2;
    if (needed>s->limits.output) return dnn_result(needed,DNN_LIMIT,0);
    if (needed>dnh_room(s,c)) return dnn_result(needed,DNN_WOULD_BLOCK,0);
    dnh_append(c,prefix,(size_t)a); dnh_append(c,headers.data,headers.len); dnh_append(c,suffix,(size_t)b); dnh_append(c,"\r\n",2);
    c->response=true; c->no_body=nobody; c->response_chunked=chunked && !nobody;
    c->response_remaining=nobody?0:size;
    c->close_after=!keep && c->body_done;
    c->keep_alive=keep;
    return dnn_result(0,DNN_OK,0);
}
dnn_io dlm_http_response_write(void *raw,uint64_t id,dnn_text bytes) {
    dnh_server *s=raw;
    dnh_conn *c=dnh_get(s,id);
    if (!c || c->phase!=DNH_ACTIVE || !c->response || c->response_done || c->no_body || !dnn_buffer(bytes.data,bytes.len)
        || (!c->response_chunked && bytes.len>c->response_remaining)) return dnn_result(0,DNN_INVALID,0);
    if (!bytes.len) return dnn_result(0,DNN_OK,0);
    size_t room=dnh_room(s,c),count=bytes.len;
    if (count>s->limits.chunk) count=s->limits.chunk;
    if (c->response_chunked) {
        if (room<24) return dnn_result(0,DNN_WOULD_BLOCK,0);
        if (count>room-24) count=room-24;
        if (!count) return dnn_result(0,DNN_WOULD_BLOCK,0);
        char header[24]; int n=snprintf(header,sizeof header,"%zx\r\n",count);
        dnh_append(c,header,(size_t)n); dnh_append(c,bytes.data,count); dnh_append(c,"\r\n",2);
    } else {
        if (count>room) count=room;
        if (!count) return dnn_result(0,DNN_WOULD_BLOCK,0);
        dnh_append(c,bytes.data,count); c->response_remaining-=count;
    }
    return dnn_result(count,DNN_OK,0);
}
dnn_io dlm_http_response_end(void *raw,uint64_t id) {
    dnh_server *s=raw;
    dnh_conn *c=dnh_get(s,id);
    if (!c || c->phase!=DNH_ACTIVE || !c->response || c->response_done || (!c->response_chunked && c->response_remaining)) return dnn_result(0,DNN_INVALID,0);
    if (c->response_chunked) {
        if (dnh_room(s,c)<5) return dnn_result(0,DNN_WOULD_BLOCK,0);
        dnh_append(c,"0\r\n\r\n",5);
    }
    c->response_done=true;
    c->close_after=!c->body_done || !c->keep_alive || s->closing;
    return dnn_result(0,DNN_OK,0);
}
bool dlm_http_exchange_alive(void *raw,uint64_t id) { return dnh_get(raw,id)!=NULL; }
dnn_io dlm_http_exchange_close(void *raw,uint64_t id) {
    dnh_conn *c=dnh_get(raw,id);
    if (!c) return dnn_result(0,DNN_EOF,0);
    dnh_close(c);
    return dnn_result(0,DNN_OK,0);
}
