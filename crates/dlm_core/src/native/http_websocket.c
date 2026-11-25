typedef struct { size_t count; uint32_t opcode; bool first, final; int32_t status; } dnh_ws_part;

static uint32_t dnh_rotate(uint32_t x,unsigned n) { return (x<<n)|(x>>(32-n)); }
static void dnh_ws_accept_key(const uint8_t key[24],char out[28]) {
    uint8_t input[128]={0};
    memcpy(input,key,24);
    memcpy(input+24,"258EAFA5-E914-47DA-95CA-C5AB0DC85B11",36);
    input[60]=128; input[126]=1; input[127]=224;
    uint32_t h[]={0x67452301u,0xefcdab89u,0x98badcfeu,0x10325476u,0xc3d2e1f0u};
    for (size_t block=0;block<128;block+=64) {
        uint32_t w[80];
        for (size_t i=0;i<16;i++) w[i]=(uint32_t)dn_binary_read(input,128,block+4*i,4,1);
        for (size_t i=16;i<80;i++) w[i]=dnh_rotate(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
        for (size_t i=0;i<80;i++) {
            uint32_t f,k;
            if (i<20) { f=(b&c)|(~b&d); k=0x5a827999u; }
            else if (i<40) { f=b^c^d; k=0x6ed9eba1u; }
            else if (i<60) { f=(b&c)|(b&d)|(c&d); k=0x8f1bbcdcu; }
            else { f=b^c^d; k=0xca62c1d6u; }
            uint32_t t=dnh_rotate(a,5)+f+e+k+w[i];
            e=d; d=c; c=dnh_rotate(b,30); b=a; a=t;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
    }
    uint8_t digest[20];
    for (size_t i=0;i<20;i++) digest[i]=(uint8_t)(h[i/4]>>(24-8*(i%4)));
    static const char digits[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i=0,j=0;i<20;i+=3,j+=4) {
        uint32_t bits=(uint32_t)digest[i]<<16 | (uint32_t)digest[i+1]<<8 | (i+2<20?digest[i+2]:0);
        out[j]=digits[(bits>>18)&63]; out[j+1]=digits[(bits>>12)&63]; out[j+2]=digits[(bits>>6)&63];
        out[j+3]=i+2<20?digits[bits&63]:'=';
    }
}
static bool dnh_utf8(uint32_t *need,uint32_t *scalar,uint32_t *minimum,uint8_t byte) {
    if (*need) {
        if ((byte&192)!=128) return false;
        *scalar=(*scalar<<6)|(byte&63);
        --*need;
        return *need || (*scalar>=*minimum && *scalar<=0x10ffff && (*scalar<0xd800 || *scalar>0xdfff));
    }
    if (byte<128) return true;
    if (byte>=194 && byte<=223) { *need=1; *scalar=byte&31; *minimum=128; }
    else if (byte>=224 && byte<=239) { *need=2; *scalar=byte&15; *minimum=2048; }
    else if (byte>=240 && byte<=244) { *need=3; *scalar=byte&7; *minimum=65536; }
    else return false;
    return true;
}
static bool dnh_close_payload(const uint8_t *bytes,size_t n) {
    if (!n) return true;
    if (n<2) return false;
    unsigned code=(unsigned)bytes[0]*256+bytes[1];
    if (code<1000 || code>=5000 || code==1004 || code==1005 || code==1006 || (code>=1015 && code<3000)) return false;
    uint32_t need=0,scalar=0,minimum=0;
    for (size_t i=2;i<n;i++) if (!dnh_utf8(&need,&scalar,&minimum,bytes[i])) return false;
    return need==0;
}
static dnn_io dnh_frame_write(dnh_server *s,dnh_conn *c,uint8_t opcode,bool final,const uint8_t *bytes,size_t n) {
    uint8_t h[10]={(uint8_t)(opcode|(final?128:0)),0};
    size_t size=2;
    if (n<126) h[1]=(uint8_t)n;
    else {
        unsigned width=n<=65535?2:8;
        h[1]=width==2?126:127;
        for (unsigned i=0;i<width;i++) h[size+i]=(uint8_t)((uint64_t)n>>(8*(width-1-i)));
        size+=width;
    }
    if (n>s->limits.output-size) return dnn_result(0,DNN_LIMIT,0);
    if (n+size>dnh_room(s,c)) return dnn_result(0,DNN_WOULD_BLOCK,0);
    dnh_append(c,h,size); dnh_append(c,bytes,n);
    return dnn_result(n,DNN_OK,0);
}
static dnh_ws_part dnh_ws_fail(dnh_server *s,dnh_conn *c,uint16_t code) {
    uint8_t close[2]={(uint8_t)(code>>8),(uint8_t)code};
    if (dnh_frame_write(s,c,8,true,close,2).status!=DNN_OK) dnh_close(c);
    else { c->close_after=true; c->response_done=true; }
    return (dnh_ws_part){.status=DNN_PROTOCOL};
}

dnn_io dlm_http_websocket_upgrade(void *raw,uint64_t id) {
    dnh_server *s=raw;
    dnh_conn *c=dnh_get(s,id);
    if (!c || c->phase!=DNH_ACTIVE || !c->upgradable || !c->head_taken || c->response || s->closing) return dnn_result(0,DNN_INVALID,0);
    static const char response[]="HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ";
    if (dnh_room(s,c)<sizeof response-1+32) return dnn_result(0,DNN_WOULD_BLOCK,0);
    char key[28]; dnh_ws_accept_key(c->ws_key,key);
    dnh_append(c,response,sizeof response-1); dnh_append(c,key,28); dnh_append(c,"\r\n\r\n",4);
    c->phase=DNH_WS; c->deadline=dnc_now()+s->limits.idle;
    return dnn_result(0,DNN_OK,0);
}
dnh_ws_part dlm_http_websocket_read(void *raw,uint64_t id,uint8_t *out,size_t cap) {
    dnh_server *s=raw;
    dnh_conn *c=dnh_get(s,id);
    if (!c || c->phase!=DNH_WS || !out || !cap || cap>s->limits.chunk) return (dnh_ws_part){.status=DNN_INVALID};
    if (c->response_done) return (dnh_ws_part){.status=DNN_EOF};
    if (!c->ws_frame) {
        if (c->rx_len<2) return (dnh_ws_part){.status=DNN_WOULD_BLOCK};
        const uint8_t *p=c->input;
        bool final=(p[0]&128)!=0;
        uint8_t opcode=p[0]&15;
        bool control=opcode>=8;
        uint64_t n=p[1]&127;
        if ((p[0]&112) || !(p[1]&128) || (opcode>2 && (opcode<8 || opcode>10)) || (control && (!final || n>125))) return dnh_ws_fail(s,c,1002);
        size_t header=n<126?2:n==126?4:10;
        if (c->rx_len<header+4) return (dnh_ws_part){.status=DNN_WOULD_BLOCK};
        if (n>=126) {
            n=dn_binary_read(p,c->rx_len,2,header-2,1);
            if ((header==4 && n<126) || (header==10 && (n<65536 || n>>63))) return dnh_ws_fail(s,c,1002);
        }
        if (control) {
            if (c->rx_len<header+4+(size_t)n) return (dnh_ws_part){.status=DNN_WOULD_BLOCK};
            uint8_t payload[125];
            for (size_t i=0;i<n;i++) payload[i]=p[header+4+i]^p[header+i%4];
            if (opcode==8 && !dnh_close_payload(payload,(size_t)n)) return dnh_ws_fail(s,c,1002);
            if (opcode==9 || (opcode==8 && !c->ws_close)) {
                dnn_io wrote=dnh_frame_write(s,c,opcode==9?10:8,true,payload,(size_t)n);
                if (wrote.status!=DNN_OK) return (dnh_ws_part){.status=wrote.status};
            }
            dnh_consume(c,header+4+(size_t)n);
            c->deadline=dnc_now()+s->limits.idle;
            if (opcode==8) { c->response_done=true; c->close_after=true; return (dnh_ws_part){.status=DNN_EOF}; }
            return (dnh_ws_part){.status=DNN_WOULD_BLOCK};
        }
        if ((opcode==0 && !c->ws_fragment) || (opcode && c->ws_fragment)) return dnh_ws_fail(s,c,1002);
        if (opcode) c->ws_message=0;
        if (n>s->limits.body-c->ws_message) return dnh_ws_fail(s,c,1009);
        c->ws_message+=n;
        memcpy(c->ws_mask,p+header,4);
        dnh_consume(c,header+4);
        c->ws_remaining=n; c->ws_offset=0; c->ws_opcode=opcode; c->ws_fin=final; c->ws_frame=true;
    }
    size_t count=c->rx_len<cap?c->rx_len:cap;
    if (c->ws_remaining<count) count=(size_t)c->ws_remaining;
    if (!count && c->ws_remaining) return (dnh_ws_part){.status=DNN_WOULD_BLOCK};
    uint8_t opcode=c->ws_opcode?c->ws_opcode:c->ws_fragment;
    bool first=c->ws_opcode && !c->ws_offset;
    for (size_t i=0;i<count;i++) {
        uint8_t b=c->input[i]^c->ws_mask[(c->ws_offset+i)&3];
        if (opcode==1 && !dnh_utf8(&c->ws_utf8,&c->ws_codepoint,&c->ws_minimum,b)) return dnh_ws_fail(s,c,1007);
        out[i]=b;
    }
    dnh_consume(c,count); c->ws_offset+=count; c->ws_remaining-=count;
    bool final=c->ws_fin && !c->ws_remaining;
    if (!c->ws_remaining) {
        if (final && c->ws_utf8) return dnh_ws_fail(s,c,1007);
        c->ws_frame=false;
        c->ws_fragment=final?0:opcode;
    }
    c->deadline=dnc_now()+s->limits.idle;
    return (dnh_ws_part){count,opcode,first,final,DNN_OK};
}
dnn_io dlm_http_websocket_write(void *raw,uint64_t id,uint32_t opcode,bool final,dnn_text bytes) {
    dnh_server *s=raw;
    dnh_conn *c=dnh_get(s,id);
    if (!c || c->phase!=DNH_WS || c->response_done || c->ws_close || !dnn_buffer(bytes.data,bytes.len)
        || (opcode>2 && (opcode<8 || opcode>10)) || (opcode>=8 && (!final || bytes.len>125))) return dnn_result(0,DNN_INVALID,0);
    if (opcode==8 && !dnh_close_payload(bytes.data,bytes.len)) return dnn_result(0,DNN_INVALID,0);
    if (opcode<8 && ((opcode==0 && !c->ws_out_fragment) || (opcode && c->ws_out_fragment))) return dnn_result(0,DNN_INVALID,0);
    uint32_t need=c->ws_out_utf8,scalar=c->ws_out_codepoint,minimum=c->ws_out_minimum;
    uint8_t kind=opcode?opcode:c->ws_out_fragment;
    if (kind==1) {
        for (size_t i=0;i<bytes.len;i++) if (!dnh_utf8(&need,&scalar,&minimum,bytes.data[i])) return dnn_result(0,DNN_INVALID,0);
        if (final && need) return dnn_result(0,DNN_INVALID,0);
    }
    dnn_io result=dnh_frame_write(s,c,(uint8_t)opcode,final,bytes.data,bytes.len);
    if (!result.status) {
        if (opcode<8) { c->ws_out_fragment=final?0:kind; c->ws_out_utf8=need; c->ws_out_codepoint=scalar; c->ws_out_minimum=minimum; }
        if (opcode==8) c->ws_close=true;
    }
    return result;
}
