/* Native application runtime: values have no per-element dynamic headers. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <float.h>
#include <stdatomic.h>
#include <setjmp.h>
#include <inttypes.h>
#include <locale.h>
#include <fenv.h>
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif
#if defined(_WIN32)
#define DN_EXPORT __declspec(dllexport)
#define DN_TLS _Thread_local
#else
#define DN_EXPORT __attribute__((visibility("default")))
#define DN_TLS _Thread_local
#endif
_Static_assert(sizeof(void*)==8,"native profile requires a 64-bit target");
_Static_assert(CHAR_BIT==8 && sizeof(float)==4 && sizeof(double)==8,"native IEEE scalar ABI");
_Static_assert(FLT_RADIX==2 && FLT_MANT_DIG==24 && DBL_MANT_DIG==53,"native IEEE scalar format");
_Static_assert(ATOMIC_BOOL_LOCK_FREE==2 && ATOMIC_INT_LOCK_FREE==2 && ATOMIC_LLONG_LOCK_FREE==2,"native x64 atomic profile requires lock-free 32/64-bit integers");
_Static_assert(INT64_MIN==(-INT64_MAX-1),"native two's complement profile");
typedef struct dn_utf8 { const uint8_t *data; size_t len; } dn_utf8;
typedef struct dn_mem dn_mem;
typedef volatile struct dn_boundary dn_boundary;
struct dn_mem {dn_mem *prev,*next;dn_boundary *scope;size_t bytes,alignment;void *raw;void(*finalize)(void*);max_align_t align;};
struct dn_boundary {jmp_buf *jump;dn_boundary *previous;dn_mem *allocations;void *foreign_marker;fenv_t fp;int fp_saved;};
static DN_TLS dn_boundary *dn_current;
static DN_TLS void *dn_foreign_context;
static DN_TLS void (*dn_foreign_unwind)(void*);
static DN_TLS const char *dn_error;
static DN_TLS uint64_t dn_allocations,dn_frees;
static _Atomic uint64_t dn_live_allocations,dn_active_calls;
static _Atomic unsigned dn_runtime_state=1;
#ifndef DN_RUNTIME_CLOSE
#define DN_RUNTIME_CLOSE 0
#endif
static void (*dn_runtime_close)(void)=DN_RUNTIME_CLOSE;
DN_EXPORT uint64_t dlm_native_live_allocations(void){return atomic_load_explicit(&dn_live_allocations,memory_order_acquire);}
DN_EXPORT bool dlm_native_runtime_init(void){unsigned expected=0;if(atomic_compare_exchange_strong_explicit(&dn_runtime_state,&expected,1,memory_order_acq_rel,memory_order_acquire)||expected==1){dn_error=NULL;return true;}dn_error="runtime is shutting down";return false;}
DN_EXPORT bool dlm_native_runtime_shutdown(void){
 if(atomic_load_explicit(&dn_active_calls,memory_order_acquire)||atomic_load_explicit(&dn_live_allocations,memory_order_acquire)){dn_error="runtime still has active calls or live resources";return false;}
 unsigned expected=1;if(!atomic_compare_exchange_strong_explicit(&dn_runtime_state,&expected,2,memory_order_acq_rel,memory_order_acquire)){if(expected==0){dn_error=NULL;return true;}dn_error="runtime is shutting down";return false;}
 if(atomic_load_explicit(&dn_active_calls,memory_order_acquire)||atomic_load_explicit(&dn_live_allocations,memory_order_acquire)){atomic_store_explicit(&dn_runtime_state,1,memory_order_release);dn_error="runtime still has active calls or live resources";return false;}
 if(dn_runtime_close)dn_runtime_close();atomic_store_explicit(&dn_runtime_state,0,memory_order_release);dn_error=NULL;return true;
}
static bool dn_enter(void){atomic_fetch_add_explicit(&dn_active_calls,1,memory_order_acq_rel);if(atomic_load_explicit(&dn_runtime_state,memory_order_acquire)==1)return true;atomic_fetch_sub_explicit(&dn_active_calls,1,memory_order_release);dn_error="runtime is closed";return false;}
static void dn_leave(void){atomic_fetch_sub_explicit(&dn_active_calls,1,memory_order_release);}
DN_EXPORT const char *dlm_native_last_error(void){return dn_error;}
DN_EXPORT void dlm_native_clear_error(void){dn_error=NULL;}
DN_EXPORT uint32_t dlm_native_abi_version(void){return 1;}
DN_EXPORT uint64_t dlm_native_allocations(void){return dn_allocations;}
DN_EXPORT uint64_t dlm_native_frees(void){return dn_frees;}
static void dn_fp_default(void){fesetenv(FE_DFL_ENV);
#if defined(__x86_64__) || defined(_M_X64)
_mm_setcsr((_mm_getcsr()&~0xe040U)|0x1f80U);
#endif
}
static void dn_fp_begin(dn_boundary *b){fegetenv((fenv_t*)(void*)&b->fp);b->fp_saved=1;dn_fp_default();}
static void dn_unlink(dn_mem *m){if(!m->scope)return;if(m->prev)m->prev->next=m->next;else m->scope->allocations=m->next;if(m->next)m->next->prev=m->prev;m->scope=NULL;m->prev=m->next=NULL;}
static void dn_link(dn_mem *m,dn_boundary *b){m->scope=b;m->prev=NULL;m->next=b?b->allocations:NULL;if(m->next)m->next->prev=m;if(b)b->allocations=m;}
static void dn_boundary_end(dn_boundary *b,int failed){while(b->allocations){dn_mem *m=b->allocations;dn_unlink(m);if(failed){if(m->finalize)m->finalize(m+1);free(m->raw);dn_frees++;atomic_fetch_sub_explicit(&dn_live_allocations,1,memory_order_release);}else dn_link(m,b->previous);}dn_current=b->previous;if(b->fp_saved)fesetenv((const fenv_t*)(const void*)&b->fp);}
static _Noreturn void dn_raise(void){if(dn_foreign_unwind)dn_foreign_unwind(dn_current?dn_current->foreign_marker:NULL);if(dn_current)longjmp(*dn_current->jump,1);fprintf(stderr,"DLM native error: %s\n",dn_error?dn_error:"unknown failure");abort();}
static _Noreturn void dn_fail(const char *s){dn_error=s;dn_raise();}
static size_t dn_size(size_t n,size_t s){if(s&&n>SIZE_MAX/s)dn_fail("allocation size overflow");return n*s;}
static void *dn_allocate_aligned(size_t n,size_t a){if(a<_Alignof(dn_mem))a=_Alignof(dn_mem);if(n>SIZE_MAX-sizeof(dn_mem)-(a-1))dn_fail("allocation size overflow");void *raw=calloc(1,sizeof(dn_mem)+n+a-1);if(!raw)dn_fail("out of memory");uintptr_t p=((uintptr_t)raw+sizeof(dn_mem)+a-1)&~(uintptr_t)(a-1);dn_mem *m=(dn_mem*)p-1;m->raw=raw;m->bytes=n;m->alignment=a;dn_link(m,dn_current);dn_allocations++;atomic_fetch_add_explicit(&dn_live_allocations,1,memory_order_relaxed);return (void*)p;}
static void *dn_allocate(size_t n){return dn_allocate_aligned(n,_Alignof(max_align_t));}
static void dn_deallocate(void *p){if(p){dn_mem *m=(dn_mem*)p-1;dn_unlink(m);free(m->raw);dn_frees++;atomic_fetch_sub_explicit(&dn_live_allocations,1,memory_order_release);}}
static void dn_detach(void *p){if(p)dn_unlink((dn_mem*)p-1);}
static void dn_adopt(void *p){if(p&&dn_current){dn_mem *m=(dn_mem*)p-1;dn_unlink(m);dn_link(m,dn_current);}}
static void *dn_nonnull(void *p){if(!p)dn_fail("null pointer");return p;}
static void *dn_offset(void *p,size_t count,size_t stride){uintptr_t a=(uintptr_t)p;size_t n=dn_size(count,stride);if(!p||n>UINTPTR_MAX-a)dn_fail("pointer offset overflow");return (void*)(a+n);}
static size_t dn_index(size_t i,size_t n){if(i>=n)dn_fail("index out of bounds");return i;}
static void dn_range(size_t off,size_t count,size_t n){if(off>n||count>n-off)dn_fail("range out of bounds");}
static void dn_aligned(const void *p,size_t a){if((uintptr_t)p&(a-1))dn_fail("misaligned vector access");}
static uint32_t dn_f32_bits(float x){uint32_t b;memcpy(&b,&x,4);return b;}
static uint64_t dn_f64_bits(double x){uint64_t b;memcpy(&b,&x,8);return b;}
static float dn_f32_from_bits(uint32_t b){float x;memcpy(&x,&b,4);return x;}
static double dn_f64_from_bits(uint64_t b){double x;memcpy(&x,&b,8);return x;}
static uint64_t dn_binary_read(const uint8_t *p,size_t len,size_t off,size_t size,int big){dn_range(off,size,len);uint64_t n=0;for(size_t i=0;i<size;i++)n|=(uint64_t)p[off+i]<<(8*(big?size-1-i:i));return n;}
static void dn_binary_write(uint8_t *p,size_t len,size_t off,size_t size,uint64_t n,int big){dn_range(off,size,len);for(size_t i=0;i<size;i++)p[off+i]=(uint8_t)(n>>(8*(big?size-1-i:i)));}
static void dn_assert(bool c){if(!c)dn_fail("assertion failed");}
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define DN_X86 1
#else
#define DN_X86 0
#endif
#ifndef DN_CPU_PROFILE
#define DN_CPU_PROFILE 0
#endif
static int dn_cpu_profile(void){
#if DN_X86 && (defined(__GNUC__) || defined(__clang__))
    if(DN_CPU_PROFILE==1)return 1;
    if(DN_CPU_PROFILE==2)return 2;
    __builtin_cpu_init();
    if(DN_CPU_PROFILE!=3 && __builtin_cpu_supports("avx512f"))return 4;
    if(__builtin_cpu_supports("avx2"))return 3;
    return 2;
#else
    return 1;
#endif
}
DN_EXPORT int dlm_native_cpu_profile(void){return dn_cpu_profile();}
#if DN_X86 && (defined(__GNUC__) || defined(__clang__))
__attribute__((target("sse2"))) static void dn_simd_f32_sse(float *o,const float*a,const float*b,size_t n,int op){size_t i=0;for(;i+4<=n;i+=4){__m128 x=_mm_loadu_ps(a+i),y=_mm_loadu_ps(b+i);_mm_storeu_ps(o+i,op==0?_mm_add_ps(x,y):_mm_mul_ps(x,y));}for(;i<n;i++)o[i]=op==0?a[i]+b[i]:a[i]*b[i];}
__attribute__((target("sse2"))) static void dn_simd_f64_sse(double *o,const double*a,const double*b,size_t n,int op){size_t i=0;for(;i+2<=n;i+=2){__m128d x=_mm_loadu_pd(a+i),y=_mm_loadu_pd(b+i);_mm_storeu_pd(o+i,op==0?_mm_add_pd(x,y):_mm_mul_pd(x,y));}for(;i<n;i++)o[i]=op==0?a[i]+b[i]:a[i]*b[i];}
__attribute__((target("avx2"))) static void dn_simd_f32_avx(float *o,const float*a,const float*b,size_t n,int op){size_t i=0;for(;i+8<=n;i+=8){__m256 x=_mm256_loadu_ps(a+i),y=_mm256_loadu_ps(b+i);_mm256_storeu_ps(o+i,op==0?_mm256_add_ps(x,y):_mm256_mul_ps(x,y));}dn_simd_f32_sse(o+i,a+i,b+i,n-i,op);}
__attribute__((target("avx2"))) static void dn_simd_f64_avx(double *o,const double*a,const double*b,size_t n,int op){size_t i=0;for(;i+4<=n;i+=4){__m256d x=_mm256_loadu_pd(a+i),y=_mm256_loadu_pd(b+i);_mm256_storeu_pd(o+i,op==0?_mm256_add_pd(x,y):_mm256_mul_pd(x,y));}dn_simd_f64_sse(o+i,a+i,b+i,n-i,op);}
__attribute__((target("avx512f"))) static void dn_simd_f32_avx512(float *o,const float*a,const float*b,size_t n,int op){size_t i=0;for(;i+16<=n;i+=16){__m512 x=_mm512_loadu_ps(a+i),y=_mm512_loadu_ps(b+i);_mm512_storeu_ps(o+i,op==0?_mm512_add_ps(x,y):_mm512_mul_ps(x,y));}dn_simd_f32_avx(o+i,a+i,b+i,n-i,op);}
__attribute__((target("avx512f"))) static void dn_simd_f64_avx512(double *o,const double*a,const double*b,size_t n,int op){size_t i=0;for(;i+8<=n;i+=8){__m512d x=_mm512_loadu_pd(a+i),y=_mm512_loadu_pd(b+i);_mm512_storeu_pd(o+i,op==0?_mm512_add_pd(x,y):_mm512_mul_pd(x,y));}dn_simd_f64_avx(o+i,a+i,b+i,n-i,op);}
#endif
static void dn_simd_f32(float *o,const float*a,const float*b,size_t n,int op){
#if DN_X86 && (defined(__GNUC__) || defined(__clang__))
    int p=dn_cpu_profile();if(p==4){dn_simd_f32_avx512(o,a,b,n,op);return;}if(p==3){dn_simd_f32_avx(o,a,b,n,op);return;}if(p==2){dn_simd_f32_sse(o,a,b,n,op);return;}
#endif
    for(size_t i=0;i<n;i++)o[i]=op==0?a[i]+b[i]:a[i]*b[i];
}
static void dn_simd_f64(double *o,const double*a,const double*b,size_t n,int op){
#if DN_X86 && (defined(__GNUC__) || defined(__clang__))
    int p=dn_cpu_profile();if(p==4){dn_simd_f64_avx512(o,a,b,n,op);return;}if(p==3){dn_simd_f64_avx(o,a,b,n,op);return;}if(p==2){dn_simd_f64_sse(o,a,b,n,op);return;}
#endif
    for(size_t i=0;i<n;i++)o[i]=op==0?a[i]+b[i]:a[i]*b[i];
}

static _Atomic uint64_t dn_next_thread=1;
static DN_TLS uint64_t dn_thread;
static uint64_t dn_thread_id(void){if(!dn_thread){dn_thread=atomic_fetch_add_explicit(&dn_next_thread,1,memory_order_relaxed);if(!dn_thread)dn_fail("thread identity exhausted");}return dn_thread;}
static void dn_check_thread(uint64_t owner){if(owner!=dn_thread_id())dn_fail("resource belongs to another thread");}

static __attribute__((noinline)) void dn_tls_touch(void *p){__asm__ __volatile__(""::"r"(p):"memory");}
DN_EXPORT uint64_t dlm_native_thread_init(void){dn_tls_touch(&dn_current);dn_tls_touch(&dn_foreign_context);dn_tls_touch(&dn_foreign_unwind);dn_tls_touch(&dn_error);dn_tls_touch(&dn_allocations);dn_tls_touch(&dn_frees);dn_tls_touch(&dn_thread);fenv_t fp;fegetenv(&fp);dn_fp_default();(void)dn_cpu_profile();uint64_t id=dn_thread_id();fesetenv(&fp);dn_error=NULL;return id;}

static uint8_t dn_print_f64(double x){char text[128];if(isnan(x))strcpy(text,"nan");else{snprintf(text,sizeof text,"%.17g",x);const char *point=localeconv()->decimal_point;if(point&&strcmp(point,".")!=0&&*point){char *p=strstr(text,point);if(p){size_t n=strlen(point);*p='.';memmove(p+1,p+n,strlen(p+n)+1);}}}if(puts(text)<0)dn_fail("output failed");return 0;}
/* DLM native networking provider, ABI v1. Embedded only for std.native_net.
 * Socket instances own their OS/library resources. Foreign calls return
 * explicit status values; no error unwinds across the C ABI.
 */
#ifndef DLM_NATIVE_NET_PROVIDER
#define DLM_NATIVE_NET_PROVIDER
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdatomic.h>
#include <errno.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <bcrypt.h>
typedef SOCKET dnn_fd;
typedef int dnn_socklen;
#define DNN_BAD INVALID_SOCKET
#define dnn_socket_error() WSAGetLastError()
#define dnn_close closesocket
#define DNN_AGAIN(e) ((e)==WSAEWOULDBLOCK || (e)==WSAEINPROGRESS)
#define DNN_INTR(e) ((e)==WSAEINTR)
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <dlfcn.h>
typedef int dnn_fd;
typedef socklen_t dnn_socklen;
#define DNN_BAD (-1)
#define dnn_socket_error() errno
#define dnn_close close
#define DNN_AGAIN(e) ((e)==EAGAIN || (e)==EWOULDBLOCK || (e)==EINPROGRESS)
#define DNN_INTR(e) ((e)==EINTR)
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#define DNN_MAX ((size_t)67108864)
enum { DNN_OK, DNN_WOULD_BLOCK, DNN_EOF, DNN_TIMEOUT, DNN_INVALID,
       DNN_SYSTEM, DNN_UNSUPPORTED, DNN_LIMIT, DNN_TLS, DNN_CANCELLED,
       DNN_TRUNCATED, DNN_PROTOCOL };
enum { DNN_TCP=1, DNN_LISTENER, DNN_UDP, DNN_SECURE, DNN_WS, DNN_HTTP };
typedef struct { const uint8_t *data; size_t len; } dnn_text;
typedef struct { size_t count; int32_t status, system; } dnn_io;
typedef struct { void *handle; int32_t status, system; } dnn_open;
typedef struct { uint32_t family, port, scope; uint8_t bytes[16]; } dnn_addr;
typedef struct { dnn_addr address; int32_t status, system; } dnn_resolved;
typedef struct { dnn_io io; dnn_addr peer; } dnn_datagram;
typedef struct { dnn_io io; uint64_t offset, remaining; uint32_t flags; } dnn_ws_io;
typedef struct { void *socket; uint32_t events, ready; int32_t status, system; } dnn_poll;
typedef struct { _Atomic bool signalled; } dnn_cancel;
typedef struct { uint8_t *data; size_t len, cap, limit; bool exceeded, oom; } dnn_bytes;
typedef struct {
    dnn_fd fd;
    int kind;
    void *curl, *headers;
    dnn_bytes body, response_headers;
    uint32_t requested_version;
    bool failed;
} dnn_socket;
static _Atomic size_t dnn_live=0;
#if defined(_WIN32)
static atomic_int dnn_wsa_state=0;
#endif
static dnn_io dnn_result(size_t n, int s, int e) { dnn_io r={n,s,e}; return r; }
static dnn_open dnn_open_error(int s, int e) { dnn_open r={NULL,s,e}; return r; }
static bool dnn_buffer(const void *p,size_t n) { return n<=DNN_MAX && (p || !n); }

uint64_t dlm_net_clock_ms(void) {
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)) return 0;
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
#endif
}
static uint64_t dnn_deadline(uint32_t ms) { return dlm_net_clock_ms()+(uint64_t)ms; }
static int dnn_remaining(uint64_t deadline) {
    uint64_t now=dlm_net_clock_ms();
    return now>=deadline ? 0 : (deadline-now>INT_MAX ? INT_MAX : (int)(deadline-now));
}
static bool dnn_cancelled(const void *p) {
    return p && atomic_load_explicit(&((const dnn_cancel*)p)->signalled,memory_order_acquire);
}
void *dlm_net_cancel_new(void) {
    dnn_cancel *p=(dnn_cancel*)malloc(sizeof *p);
    if(p) { atomic_init(&p->signalled,false);atomic_fetch_add(&dnn_live,1); }
    return p;
}
void dlm_net_cancel_close(void *p) { if(p) { free(p);atomic_fetch_sub(&dnn_live,1); } }
void dlm_net_cancel_signal(void *p) { if(p) atomic_store_explicit(&((dnn_cancel*)p)->signalled,true,memory_order_release); }
void dlm_net_cancel_reset(void *p) { if(p) atomic_store_explicit(&((dnn_cancel*)p)->signalled,false,memory_order_release); }
static int dnn_nonblocking(dnn_fd fd) {
#if defined(_WIN32)
    u_long one=1;
    return ioctlsocket(fd,FIONBIO,&one);
#else
    int flags=fcntl(fd,F_GETFL,0);
    if(flags<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0) return -1;
    flags=fcntl(fd,F_GETFD,0);
    if(flags<0 || fcntl(fd,F_SETFD,flags|FD_CLOEXEC)<0) return -1;
#if defined(SO_NOSIGPIPE)
    int one=1;
    if(setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one)) return -1;
#endif
    return 0;
#endif
}
static int dnn_os_init(void) {
#if defined(_WIN32)
    int expected=0;
    if(atomic_compare_exchange_strong(&dnn_wsa_state,&expected,1)) {
        WSADATA data;
        int e=WSAStartup(MAKEWORD(2,2),&data);
        atomic_store_explicit(&dnn_wsa_state,e ? -e : 2,memory_order_release);
    }
    int s;
    while((s=atomic_load_explicit(&dnn_wsa_state,memory_order_acquire))==1) SwitchToThread();
    return s<0 ? -s : 0;
#else
    return 0;
#endif
}
static char *dnn_string(dnn_text t,size_t maximum) {
    if(t.len>maximum || (!t.data && t.len) || (t.len && memchr(t.data,0,t.len))) return NULL;
    char *s=(char*)malloc(t.len+1);
    if(s) { if(t.len) memcpy(s,t.data,t.len); s[t.len]=0; }
    return s;
}
static dnn_open dnn_adopt(dnn_fd fd,int kind) {
    dnn_socket *s=(dnn_socket*)calloc(1,sizeof *s);
    if(!s) { dnn_close(fd); return dnn_open_error(DNN_LIMIT,0); }
    s->fd=fd; s->kind=kind;
    atomic_fetch_add(&dnn_live,1);
    dnn_open r={s,DNN_OK,0}; return r;
}
static void dnn_from_sockaddr(dnn_addr *out,const struct sockaddr *sa) {
    memset(out,0,sizeof *out);
    if(sa->sa_family==AF_INET) {
        const struct sockaddr_in *a=(const struct sockaddr_in*)sa;
        out->family=4; out->port=ntohs(a->sin_port); memcpy(out->bytes,&a->sin_addr,4);
    } else if(sa->sa_family==AF_INET6) {
        const struct sockaddr_in6 *a=(const struct sockaddr_in6*)sa;
        out->family=6; out->port=ntohs(a->sin6_port); out->scope=a->sin6_scope_id;
        memcpy(out->bytes,&a->sin6_addr,16);
    }
}
static dnn_socklen dnn_to_sockaddr(const dnn_addr *a,struct sockaddr_storage *out) {
    memset(out,0,sizeof *out);
    if(a->port>65535) return 0;
    if(a->family==4) {
        struct sockaddr_in *s=(struct sockaddr_in*)out;
        s->sin_family=AF_INET; s->sin_port=htons((uint16_t)a->port); memcpy(&s->sin_addr,a->bytes,4);
        return sizeof *s;
    }
    if(a->family==6) {
        struct sockaddr_in6 *s=(struct sockaddr_in6*)out;
        s->sin6_family=AF_INET6; s->sin6_port=htons((uint16_t)a->port); s->sin6_scope_id=a->scope;
        memcpy(&s->sin6_addr,a->bytes,16); return sizeof *s;
    }
    return 0;
}
dnn_resolved dlm_net_resolve(dnn_text host,uint16_t port,uint32_t index) {
    dnn_resolved r; memset(&r,0,sizeof r);
    int e=dnn_os_init(); if(e) { r.status=DNN_SYSTEM;r.system=e;return r; }
    char *name=dnn_string(host,1024);
    if(!name || !host.len) { free(name);r.status=DNN_INVALID;return r; }
    char service[8]; snprintf(service,sizeof service,"%u",(unsigned)port);
    struct addrinfo hints,*addresses=NULL;
    memset(&hints,0,sizeof hints); hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_DGRAM;
    e=getaddrinfo(name,service,&hints,&addresses);free(name);
    if(e) { r.status=DNN_SYSTEM;r.system=e;return r; }
    struct addrinfo *a=addresses;
    while(a && index) { a=a->ai_next;index--; }
    if(a) dnn_from_sockaddr(&r.address,a->ai_addr); else r.status=DNN_EOF;
    freeaddrinfo(addresses);return r;
}
/* Poll uses bounded scratch storage and a single monotonic deadline. A token
 * bounds cancellation latency to 50 ms without changing socket blocking mode.
 * Poll entries borrow their sockets: owners must outlive this foreign call.
 */
dnn_io dlm_net_poll(dnn_poll *items,size_t count,uint32_t ms,void *cancel) {
    if(!items || !count || count>4096) return dnn_result(0,DNN_INVALID,0);
#if defined(_WIN32)
    WSAPOLLFD fds[4096];
#else
    struct pollfd fds[4096];
#endif
    for(size_t i=0;i<count;i++) {
        items[i].ready=0;items[i].status=DNN_OK;items[i].system=0;
        dnn_socket *s=(dnn_socket*)items[i].socket;
        if(!s || s->fd==DNN_BAD || s->failed || !(items[i].events&3) || (items[i].events&~3u))
            return dnn_result(0,DNN_INVALID,0);
        fds[i].fd=s->fd;fds[i].events=0;fds[i].revents=0;
        if(items[i].events&1) fds[i].events|=POLLIN;
        if(items[i].events&2) fds[i].events|=POLLOUT;
    }
    uint64_t end=dnn_deadline(ms);
    for(;;) {
        if(dnn_cancelled(cancel)) return dnn_result(0,DNN_CANCELLED,0);
        int wait=dnn_remaining(end);if(cancel && wait>50) wait=50;
#if defined(_WIN32)
        int n=WSAPoll(fds,(ULONG)count,wait);
#else
        int n=poll(fds,(nfds_t)count,wait);
#endif
        if(n<0) { int e=dnn_socket_error();if(DNN_INTR(e)) { if(!dnn_remaining(end)) return dnn_result(0,DNN_TIMEOUT,0);continue; }return dnn_result(0,DNN_SYSTEM,e); }
        if(n>0) {
            size_t ready=0;
            for(size_t i=0;i<count;i++) {
                short events=fds[i].revents;
                if(events&POLLIN) items[i].ready|=1;
                if(events&POLLOUT) items[i].ready|=2;
                if(events&(POLLERR|POLLNVAL)) items[i].ready|=4;
                if(events&POLLHUP) items[i].ready|=8;
                if(events&POLLNVAL) items[i].status=DNN_INVALID;
                if(items[i].ready) ready++;
            }
            return dnn_result(ready,DNN_OK,0);
        }
        if(!dnn_remaining(end)) return dnn_result(0,DNN_TIMEOUT,0);
    }
}
dnn_io dlm_net_wait(void *handle,uint32_t events,uint32_t ms,void *cancel) {
    dnn_poll p={handle,events,0,0,0};
    dnn_io r=dlm_net_poll(&p,1,ms,cancel);
    if(r.status==DNN_OK) r.count=p.ready;
    return r;
}
dnn_open dlm_net_tcp(dnn_text host,uint16_t port,uint32_t ms) {
    if(!port || !ms) return dnn_open_error(DNN_INVALID,0);
    int e=dnn_os_init();if(e) return dnn_open_error(DNN_SYSTEM,e);
    char *name=dnn_string(host,1024);
    if(!name || !host.len) { free(name);return dnn_open_error(DNN_INVALID,0); }
    char service[8];snprintf(service,sizeof service,"%u",(unsigned)port);
    struct addrinfo hints,*addresses=NULL;
    memset(&hints,0,sizeof hints);hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;
    e=getaddrinfo(name,service,&hints,&addresses);free(name);
    if(e) return dnn_open_error(DNN_SYSTEM,e);
    uint64_t end=dnn_deadline(ms);int last=0;bool expired=false;
    dnn_open result=dnn_open_error(DNN_SYSTEM,0);
    for(struct addrinfo *a=addresses;a;a=a->ai_next) {
        if(!dnn_remaining(end)) { expired=true;break; }
        dnn_fd fd=socket(a->ai_family,a->ai_socktype,a->ai_protocol);
        if(fd==DNN_BAD) { last=dnn_socket_error();continue; }
        if(dnn_nonblocking(fd)) { last=dnn_socket_error();dnn_close(fd);continue; }
        int connected=connect(fd,a->ai_addr,(dnn_socklen)a->ai_addrlen);
        if(connected) {
            last=dnn_socket_error();
            if(!DNN_AGAIN(last)) { dnn_close(fd);continue; }
            dnn_socket temp;memset(&temp,0,sizeof temp);temp.fd=fd;temp.kind=DNN_TCP;
            dnn_io wait=dlm_net_wait(&temp,2,(uint32_t)dnn_remaining(end),NULL);
            if(wait.status==DNN_TIMEOUT) { dnn_close(fd);expired=true;break; }
            if(wait.status) { last=wait.system;dnn_close(fd);continue; }
            dnn_socklen size=sizeof last;
            if(getsockopt(fd,SOL_SOCKET,SO_ERROR,(char*)&last,&size) || last) {
                if(!last) last=dnn_socket_error();dnn_close(fd);continue;
            }
        }
        result=dnn_adopt(fd,DNN_TCP);break;
    }
    freeaddrinfo(addresses);
    if(!result.handle) result=dnn_open_error(expired?DNN_TIMEOUT:result.status,last);
    return result;
}
static dnn_open dnn_bind(dnn_text host,uint16_t port,int kind,uint32_t backlog) {
    int e=dnn_os_init();if(e) return dnn_open_error(DNN_SYSTEM,e);
    char *name=dnn_string(host,1024);if(!name || !host.len) { free(name);return dnn_open_error(DNN_INVALID,0); }
    char service[8];snprintf(service,sizeof service,"%u",(unsigned)port);
    struct addrinfo hints,*addresses=NULL;memset(&hints,0,sizeof hints);
    hints.ai_family=AF_UNSPEC;hints.ai_socktype=kind==DNN_UDP?SOCK_DGRAM:SOCK_STREAM;
    e=getaddrinfo(name,service,&hints,&addresses);free(name);
    if(e) return dnn_open_error(DNN_SYSTEM,e);
    dnn_open r=dnn_open_error(DNN_SYSTEM,0);
    for(struct addrinfo *a=addresses;a;a=a->ai_next) {
        dnn_fd fd=socket(a->ai_family,a->ai_socktype,a->ai_protocol);
        if(fd==DNN_BAD) { r.system=dnn_socket_error();continue; }
        if(dnn_nonblocking(fd) || bind(fd,a->ai_addr,(dnn_socklen)a->ai_addrlen) ||
           (kind==DNN_LISTENER && listen(fd,(int)backlog))) {
            r.system=dnn_socket_error();dnn_close(fd);continue;
        }
        r=dnn_adopt(fd,kind);break;
    }
    freeaddrinfo(addresses);return r;
}
dnn_open dlm_net_listen(dnn_text host,uint16_t port,uint32_t backlog) {
    if(!backlog || backlog>65535) return dnn_open_error(DNN_INVALID,0);
    return dnn_bind(host,port,DNN_LISTENER,backlog);
}
dnn_open dlm_net_udp(dnn_text host,uint16_t port) { return dnn_bind(host,port,DNN_UDP,0); }
dnn_open dlm_net_accept(void *handle) {
    dnn_socket *s=(dnn_socket*)handle;
    if(!s || s->kind!=DNN_LISTENER) return dnn_open_error(DNN_INVALID,0);
    dnn_fd fd=accept(s->fd,NULL,NULL);
    if(fd==DNN_BAD) { int e=dnn_socket_error();return dnn_open_error(DNN_AGAIN(e)?DNN_WOULD_BLOCK:DNN_SYSTEM,e); }
    if(dnn_nonblocking(fd)) { int e=dnn_socket_error();dnn_close(fd);return dnn_open_error(DNN_SYSTEM,e); }
    return dnn_adopt(fd,DNN_TCP);
}
dnn_resolved dlm_net_local(void *handle) {
    dnn_resolved r;memset(&r,0,sizeof r);
    dnn_socket *s=(dnn_socket*)handle;
    if(!s || s->fd==DNN_BAD) { r.status=DNN_INVALID;return r; }
    struct sockaddr_storage address;dnn_socklen n=sizeof address;
    if(getsockname(s->fd,(struct sockaddr*)&address,&n)) { r.status=DNN_SYSTEM;r.system=dnn_socket_error(); }
    else dnn_from_sockaddr(&r.address,(struct sockaddr*)&address);
    return r;
}
dnn_io dlm_net_nodelay(void *handle,bool enabled) {
    dnn_socket *s=(dnn_socket*)handle;
    if(!s || s->kind!=DNN_TCP) return dnn_result(0,DNN_INVALID,0);
    int value=enabled?1:0;
    if(setsockopt(s->fd,IPPROTO_TCP,TCP_NODELAY,(const char*)&value,sizeof value)) return dnn_result(0,DNN_SYSTEM,dnn_socket_error());
    return dnn_result(0,DNN_OK,0);
}
/* v0.135 additions. Existing ABI v1 entry points retain their signatures and
 * behavior. These numeric-address operations never invoke a DNS resolver. */
typedef struct { uint32_t no_delay,keep_alive,send_buffer,receive_buffer; } dnn_options;
typedef struct { dnn_options options;int32_t status,system; } dnn_options_result;
dnn_open dlm_net_connect_address(dnn_addr address,uint32_t ms) {
    struct sockaddr_storage target;dnn_socklen length=dnn_to_sockaddr(&address,&target);
    if(!length || !address.port || !ms) return dnn_open_error(DNN_INVALID,0);
    int e=dnn_os_init();if(e) return dnn_open_error(DNN_SYSTEM,e);
    uint64_t end=dnn_deadline(ms);
    dnn_fd fd=socket(address.family==4?AF_INET:AF_INET6,SOCK_STREAM,IPPROTO_TCP);
    if(fd==DNN_BAD) return dnn_open_error(DNN_SYSTEM,dnn_socket_error());
    if(dnn_nonblocking(fd)) { e=dnn_socket_error();dnn_close(fd);return dnn_open_error(DNN_SYSTEM,e); }
    if(connect(fd,(struct sockaddr*)&target,length)) {
        e=dnn_socket_error();
        if(!DNN_AGAIN(e)) { dnn_close(fd);return dnn_open_error(DNN_SYSTEM,e); }
        dnn_socket temporary;memset(&temporary,0,sizeof temporary);temporary.fd=fd;temporary.kind=DNN_TCP;
        dnn_io ready=dlm_net_wait(&temporary,2,(uint32_t)dnn_remaining(end),NULL);
        if(ready.status) { dnn_close(fd);return dnn_open_error(ready.status,ready.system); }
        e=0;dnn_socklen size=sizeof e;
        if(getsockopt(fd,SOL_SOCKET,SO_ERROR,(char*)&e,&size) || e) {
            if(!e) e=dnn_socket_error();dnn_close(fd);return dnn_open_error(DNN_SYSTEM,e);
        }
    }
    return dnn_adopt(fd,DNN_TCP);
}
dnn_open dlm_net_bind_address(dnn_addr address,uint32_t kind,uint32_t backlog,bool dual_stack) {
    struct sockaddr_storage target;dnn_socklen length=dnn_to_sockaddr(&address,&target);
    if(!length || (kind!=DNN_LISTENER && kind!=DNN_UDP) || (address.family==4 && dual_stack) ||
       (kind==DNN_LISTENER && (!backlog || backlog>65535))) return dnn_open_error(DNN_INVALID,0);
    int e=dnn_os_init();if(e) return dnn_open_error(DNN_SYSTEM,e);
    dnn_fd fd=socket(address.family==4?AF_INET:AF_INET6,kind==DNN_UDP?SOCK_DGRAM:SOCK_STREAM,0);
    if(fd==DNN_BAD) return dnn_open_error(DNN_SYSTEM,dnn_socket_error());
    if(address.family==6) {
        int only=dual_stack?0:1;
        if(setsockopt(fd,IPPROTO_IPV6,IPV6_V6ONLY,(const char*)&only,sizeof only)) {
            e=dnn_socket_error();dnn_close(fd);return dnn_open_error(DNN_SYSTEM,e);
        }
    }
    if(dnn_nonblocking(fd) || bind(fd,(struct sockaddr*)&target,length) ||
       (kind==DNN_LISTENER && listen(fd,(int)backlog))) {
        e=dnn_socket_error();dnn_close(fd);return dnn_open_error(DNN_SYSTEM,e);
    }
    return dnn_adopt(fd,(int)kind);
}
dnn_resolved dlm_net_resolve_family(dnn_text host,uint16_t port,uint32_t family,uint32_t index) {
    dnn_resolved result;memset(&result,0,sizeof result);
    if(family!=0 && family!=4 && family!=6) { result.status=DNN_INVALID;return result; }
    int e=dnn_os_init();if(e) { result.status=DNN_SYSTEM;result.system=e;return result; }
    char *name=dnn_string(host,1024);
    if(!name || !host.len) { free(name);result.status=DNN_INVALID;return result; }
    char service[8];snprintf(service,sizeof service,"%u",(unsigned)port);
    struct addrinfo hints,*addresses=NULL;memset(&hints,0,sizeof hints);
    hints.ai_family=family==4?AF_INET:family==6?AF_INET6:AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;
    e=getaddrinfo(name,service,&hints,&addresses);free(name);
    if(e) { result.status=DNN_SYSTEM;result.system=e;return result; }
    struct addrinfo *a=addresses;
    while(a && index) { a=a->ai_next;index--; }
    if(a) dnn_from_sockaddr(&result.address,a->ai_addr);else result.status=DNN_EOF;
    freeaddrinfo(addresses);return result;
}
dnn_resolved dlm_net_peer(void *handle) {
    dnn_resolved result;memset(&result,0,sizeof result);dnn_socket *s=(dnn_socket*)handle;
    if(!s || s->failed || s->fd==DNN_BAD || (s->kind!=DNN_TCP && s->kind!=DNN_SECURE && s->kind!=DNN_WS)) {
        result.status=DNN_INVALID;return result;
    }
    struct sockaddr_storage address;dnn_socklen length=sizeof address;
    if(getpeername(s->fd,(struct sockaddr*)&address,&length)) { result.status=DNN_SYSTEM;result.system=dnn_socket_error(); }
    else dnn_from_sockaddr(&result.address,(struct sockaddr*)&address);
    return result;
}
dnn_io dlm_net_configure(void *handle,dnn_options options) {
    dnn_socket *s=(dnn_socket*)handle;
    if(!s || s->failed || (s->kind!=DNN_TCP && s->kind!=DNN_SECURE && s->kind!=DNN_UDP) ||
       options.no_delay>1 || options.keep_alive>1 || options.send_buffer>INT_MAX || options.receive_buffer>INT_MAX ||
       (s->kind==DNN_UDP && (options.no_delay || options.keep_alive))) return dnn_result(0,DNN_INVALID,0);
    int value;
    if(s->kind!=DNN_UDP) {
        value=(int)options.no_delay;
        if(setsockopt(s->fd,IPPROTO_TCP,TCP_NODELAY,(const char*)&value,sizeof value)) return dnn_result(0,DNN_SYSTEM,dnn_socket_error());
        value=(int)options.keep_alive;
        if(setsockopt(s->fd,SOL_SOCKET,SO_KEEPALIVE,(const char*)&value,sizeof value)) return dnn_result(0,DNN_SYSTEM,dnn_socket_error());
    }
    if(options.send_buffer) {
        value=(int)options.send_buffer;
        if(setsockopt(s->fd,SOL_SOCKET,SO_SNDBUF,(const char*)&value,sizeof value)) return dnn_result(0,DNN_SYSTEM,dnn_socket_error());
    }
    if(options.receive_buffer) {
        value=(int)options.receive_buffer;
        if(setsockopt(s->fd,SOL_SOCKET,SO_RCVBUF,(const char*)&value,sizeof value)) return dnn_result(0,DNN_SYSTEM,dnn_socket_error());
    }
    return dnn_result(0,DNN_OK,0);
}
dnn_options_result dlm_net_get_options(void *handle) {
    dnn_options_result result;memset(&result,0,sizeof result);dnn_socket *s=(dnn_socket*)handle;
    if(!s || s->failed || (s->kind!=DNN_TCP && s->kind!=DNN_SECURE && s->kind!=DNN_UDP)) { result.status=DNN_INVALID;return result; }
    int value=0;dnn_socklen length=sizeof value;
#define DNN_GET(level,name,field) do { length=sizeof value;if(getsockopt(s->fd,level,name,(char*)&value,&length)) { result.status=DNN_SYSTEM;result.system=dnn_socket_error();return result; }result.options.field=(uint32_t)value; } while(0)
    if(s->kind!=DNN_UDP) { DNN_GET(IPPROTO_TCP,TCP_NODELAY,no_delay);DNN_GET(SOL_SOCKET,SO_KEEPALIVE,keep_alive); }
    DNN_GET(SOL_SOCKET,SO_SNDBUF,send_buffer);DNN_GET(SOL_SOCKET,SO_RCVBUF,receive_buffer);
#undef DNN_GET
    return result;
}
dnn_io dlm_net_address_text(dnn_addr address,uint8_t *target,size_t capacity,bool with_port) {
    if((address.family!=4 && address.family!=6) || address.port>65535 || !dnn_buffer(target,capacity)) return dnn_result(0,DNN_INVALID,0);
    int e=dnn_os_init();if(e) return dnn_result(0,DNN_SYSTEM,e);
    char host[64],scoped[80],text[96];
    if(!inet_ntop(address.family==4?AF_INET:AF_INET6,address.bytes,host,sizeof host)) return dnn_result(0,DNN_SYSTEM,dnn_socket_error());
    int count;
    if(address.family==6 && address.scope) count=snprintf(scoped,sizeof scoped,"%s%%%u",host,(unsigned)address.scope);
    else count=snprintf(scoped,sizeof scoped,"%s",host);
    if(count<0 || (size_t)count>=sizeof scoped) return dnn_result(0,DNN_LIMIT,0);
    if(with_port) count=snprintf(text,sizeof text,address.family==6?"[%s]:%u":"%s:%u",scoped,(unsigned)address.port);
    else count=snprintf(text,sizeof text,"%s",scoped);
    if(count<0 || (size_t)count>=sizeof text || (size_t)count>capacity) return dnn_result(0,DNN_LIMIT,0);
    if(count) memcpy(target,text,(size_t)count);return dnn_result((size_t)count,DNN_OK,0);
}
dnn_io dlm_net_udp_send(void *handle,dnn_addr peer,const uint8_t *data,size_t count) {
    dnn_socket *s=(dnn_socket*)handle;struct sockaddr_storage address;
    dnn_socklen n=dnn_to_sockaddr(&peer,&address);
    if(!s || s->kind!=DNN_UDP || !n || !peer.port || !dnn_buffer(data,count)) return dnn_result(0,DNN_INVALID,0);
    if(count>65507) return dnn_result(0,DNN_LIMIT,0);
    int sent=(int)sendto(s->fd,(const char*)(data?data:(const uint8_t*)""),(int)count,MSG_NOSIGNAL,(struct sockaddr*)&address,n);
    if(sent<0) { int e=dnn_socket_error();return dnn_result(0,DNN_AGAIN(e)?DNN_WOULD_BLOCK:DNN_SYSTEM,e); }
    return dnn_result((size_t)sent,(size_t)sent==count?DNN_OK:DNN_PROTOCOL,0);
}
dnn_datagram dlm_net_udp_recv(void *handle,uint8_t *data,size_t capacity) {
    dnn_datagram r;memset(&r,0,sizeof r);dnn_socket *s=(dnn_socket*)handle;
    if(!s || s->kind!=DNN_UDP || !dnn_buffer(data,capacity)) { r.io.status=DNN_INVALID;return r; }
    struct sockaddr_storage peer;memset(&peer,0,sizeof peer);
#if defined(_WIN32)
    int plen=sizeof peer;
    char empty_buffer=0;
    int n=recvfrom(s->fd,capacity?(char*)data:&empty_buffer,(int)capacity,0,(struct sockaddr*)&peer,&plen);
    if(n<0) {
        int e=dnn_socket_error();
        if(e==WSAEMSGSIZE) { r.io=dnn_result(capacity,DNN_TRUNCATED,e);dnn_from_sockaddr(&r.peer,(struct sockaddr*)&peer);return r; }
        r.io=dnn_result(0,DNN_AGAIN(e)?DNN_WOULD_BLOCK:DNN_SYSTEM,e);return r;
    }
    r.io=dnn_result((size_t)n,DNN_OK,0);
#else
    struct iovec vec={data,capacity};struct msghdr message;memset(&message,0,sizeof message);
    message.msg_name=&peer;message.msg_namelen=sizeof peer;message.msg_iov=&vec;message.msg_iovlen=1;
    ssize_t n=recvmsg(s->fd,&message,0);
    if(n<0) { int e=errno;r.io=dnn_result(0,DNN_AGAIN(e)?DNN_WOULD_BLOCK:DNN_SYSTEM,e);return r; }
    r.io=dnn_result((size_t)n,(message.msg_flags&MSG_TRUNC)?DNN_TRUNCATED:DNN_OK,0);
#endif
    dnn_from_sockaddr(&r.peer,(struct sockaddr*)&peer);return r;
}
dnn_io dlm_net_shutdown(void *handle) {
    dnn_socket *s=(dnn_socket*)handle;
    if(!s || s->kind!=DNN_TCP) return dnn_result(0,DNN_INVALID,0);
#if defined(_WIN32)
    int e=shutdown(s->fd,SD_SEND);
#else
    int e=shutdown(s->fd,SHUT_WR);
#endif
    return dnn_result(0,e?DNN_SYSTEM:DNN_OK,e?dnn_socket_error():0);
}
/* libcurl's stable public ABI, not a private TLS implementation. Keeping this
 * small ABI declaration local permits SDK builds without curl headers or an
 * import library. Values/layouts are cross-checked against curl.h in tests.
 */
typedef int64_t dnn_curl_off;
typedef struct { int age;int flags;dnn_curl_off offset,bytesleft;size_t len; } dnn_curl_frame;
typedef struct {
    int age;const char *version;unsigned int version_num;const char *host;int features;
    const char *ssl_version;long ssl_version_num;const char *libz_version;const char *const *protocols;
} dnn_curl_version;
typedef struct {
    void *module;
    void *(*init)(void);
    void (*cleanup)(void*);
    int (*setopt)(void*,int,...);
    int (*getinfo)(void*,int,...);
    int (*perform)(void*);
    int (*send)(void*,const void*,size_t,size_t*);
    int (*recv)(void*,void*,size_t,size_t*);
    int (*ws_send)(void*,const void*,size_t,size_t*,dnn_curl_off,unsigned int);
    int (*ws_recv)(void*,void*,size_t,size_t*,const dnn_curl_frame**);
    void *(*list_append)(void*,const char*);
    void (*list_free)(void*);
    int (*global_init)(long);
    void (*global_cleanup)(void);
    const dnn_curl_version *(*version)(int);
} dnn_curl_api;
static dnn_curl_api dnn_curl;
static atomic_int dnn_curl_state=0;
static void *dnn_library_open(const char *override_name,const char *name) {
#if defined(_WIN32)
    int n=override_name?(int)GetEnvironmentVariableW(L"DLM_LIBCURL",NULL,0):MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name,-1,NULL,0);
    if(n<=0 || n>32768) return NULL;
    wchar_t *wide=(wchar_t*)malloc((size_t)n*sizeof *wide);if(!wide) return NULL;
    if(override_name) {
        DWORD got=GetEnvironmentVariableW(L"DLM_LIBCURL",wide,(DWORD)n);
        if(!got || got>=(DWORD)n) { free(wide);return NULL; }
    } else if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name,-1,wide,n)) { free(wide);return NULL; }
    DWORD flags=LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
    if(override_name) {
        if(!((n>3 && wide[1]==L':' && (wide[2]==L'\\' || wide[2]==L'/')) || (n>2 && wide[0]==L'\\' && wide[1]==L'\\'))) { free(wide);return NULL; }
        flags|=LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR;
    }
    HMODULE m=LoadLibraryExW(wide,NULL,flags);free(wide);return (void*)m;
#else
    if(override_name && override_name[0]!='/') return NULL;
    return dlopen(override_name?override_name:name,RTLD_NOW|RTLD_LOCAL);
#endif
}
static void *dnn_symbol(void *module,const char *name) {
#if defined(_WIN32)
    return (void*)GetProcAddress((HMODULE)module,name);
#else
    return dlsym(module,name);
#endif
}
static bool dnn_load_curl(void) {
    int state=atomic_load_explicit(&dnn_curl_state,memory_order_acquire);
    if(state==2) return true;if(state==-1) return false;
    int expected=0;
    if(atomic_compare_exchange_strong(&dnn_curl_state,&expected,1)) {
#if defined(_WIN32)
        const char *override_name=GetEnvironmentVariableW(L"DLM_LIBCURL",NULL,0)?"wide override":NULL;
        const char *names[]={"libcurl-4.dll","libcurl.dll","curl.dll",NULL};
#elif defined(__APPLE__)
        const char *override_name=getenv("DLM_LIBCURL");
        if(override_name && !*override_name) override_name=NULL;
        const char *names[]={"/usr/lib/libcurl.4.dylib",NULL};
#else
        const char *override_name=getenv("DLM_LIBCURL");
        if(override_name && !*override_name) override_name=NULL;
        const char *names[]={"libcurl.so.4","libcurl.so",NULL};
#endif
        for(size_t i=0;names[i];i++) {
            dnn_curl.module=dnn_library_open(override_name,names[i]);
            if(dnn_curl.module || override_name) break;
        }
        bool ok=dnn_curl.module!=NULL;
#define DNN_CURL(field,name) do { *(void**)(&dnn_curl.field)=ok?dnn_symbol(dnn_curl.module,name):NULL;if(!dnn_curl.field) ok=false; } while(0)
        DNN_CURL(init,"curl_easy_init");DNN_CURL(cleanup,"curl_easy_cleanup");
        DNN_CURL(setopt,"curl_easy_setopt");DNN_CURL(getinfo,"curl_easy_getinfo");DNN_CURL(perform,"curl_easy_perform");
        DNN_CURL(send,"curl_easy_send");DNN_CURL(recv,"curl_easy_recv");DNN_CURL(list_append,"curl_slist_append");
        DNN_CURL(list_free,"curl_slist_free_all");DNN_CURL(global_init,"curl_global_init");DNN_CURL(version,"curl_version_info");
        DNN_CURL(global_cleanup,"curl_global_cleanup");
#undef DNN_CURL
        if(ok) {
            *(void**)(&dnn_curl.ws_send)=dnn_symbol(dnn_curl.module,"curl_ws_send");
            *(void**)(&dnn_curl.ws_recv)=dnn_symbol(dnn_curl.module,"curl_ws_recv");
            const dnn_curl_version *v=dnn_curl.version(0);
            ok=v && v->version_num>=0x080a00 && dnn_curl.global_init(3L)==0;
        }
        atomic_store_explicit(&dnn_curl_state,ok?2:-1,memory_order_release);return ok;
    }
    while((state=atomic_load_explicit(&dnn_curl_state,memory_order_acquire))==1) {
#if defined(_WIN32)
        SwitchToThread();
#else
        struct timespec t={0,1000000};nanosleep(&t,NULL);
#endif
    }
    return state==2;
}
uint32_t dlm_net_capabilities(void) {
    uint32_t caps=1|2|4; /* TCP, UDP, OS random */
    if(dnn_load_curl()) {
        const dnn_curl_version *v=dnn_curl.version(0);
        if(v->features&(1<<2)) caps|=8;
        caps|=16; /* HTTP/1.1 */
        if(v->features&(1<<16)) caps|=32;
        if(v->features&(1<<25)) caps|=64;
        if(dnn_curl.ws_send && dnn_curl.ws_recv && v->protocols) {
            for(const char *const *p=v->protocols;*p;p++) if(!strcmp(*p,"ws")) caps|=128;
        }
    }
    return caps;
}
uint32_t dlm_net_curl_version(void) { return dnn_load_curl()?dnn_curl.version(0)->version_num:0; }
/* Host lifecycle operation: all provider calls/foreign producer threads must
 * have stopped. Refuse to unload libraries while an owned instance is alive.
 * Not a process-exit or DLL-loader-lock callback.
 */
dnn_io dlm_net_provider_shutdown(void) {
    if(atomic_load(&dnn_live) || atomic_load(&dnn_curl_state)==1) return dnn_result(0,DNN_INVALID,0);
    if(atomic_load(&dnn_curl_state)==2) dnn_curl.global_cleanup();
    if(dnn_curl.module) {
#if defined(_WIN32)
        FreeLibrary((HMODULE)dnn_curl.module);
#else
        dlclose(dnn_curl.module);
#endif
    }
    memset(&dnn_curl,0,sizeof dnn_curl);atomic_store(&dnn_curl_state,0);
#if defined(_WIN32)
    if(atomic_load(&dnn_wsa_state)==2) WSACleanup();
    atomic_store(&dnn_wsa_state,0);
#endif
    return dnn_result(0,DNN_OK,0);
}
static int dnn_curl_status(int code) {
    if(!code) return DNN_OK;
    if(code==81) return DNN_WOULD_BLOCK;
    if(code==28) return DNN_TIMEOUT;
    if(code==42) return DNN_CANCELLED;
    if(code==1 || code==4 || code==48) return DNN_UNSUPPORTED;
    if(code==35 || code==51 || code==58 || code==59 || code==60 || code==66 || code==77 || code==80 || code==82 || code==83 || code==90 || code==91 || code==98) return DNN_TLS;
    return DNN_SYSTEM;
}
void dlm_net_close(void *handle) {
    dnn_socket *s=(dnn_socket*)handle;if(!s) return;
    if(s->curl) { dnn_curl.cleanup(s->curl);if(s->headers) dnn_curl.list_free(s->headers); }
    else if(s->fd!=DNN_BAD) dnn_close(s->fd);
    free(s->body.data);free(s->response_headers.data);free(s);atomic_fetch_sub(&dnn_live,1);
}
static bool dnn_scheme(dnn_text url,const char *prefix) {
    size_t n=strlen(prefix);return url.data && url.len>=n && !memcmp(url.data,prefix,n);
}
static dnn_open dnn_curl_new(dnn_text url,uint32_t ms,int kind,dnn_text ca,dnn_text pin) {
    bool allowed=kind==DNN_WS ? (dnn_scheme(url,"ws://") || dnn_scheme(url,"wss://")) :
        kind==DNN_SECURE ? dnn_scheme(url,"https://") : (dnn_scheme(url,"http://") || dnn_scheme(url,"https://"));
    if(!allowed || !ms || ms>INT_MAX) return dnn_open_error(DNN_INVALID,0);
    char *u=dnn_string(url,16384),*cert=dnn_string(ca,32768),*key=dnn_string(pin,8192);
    if(!u || !cert || !key) { free(u);free(cert);free(key);return dnn_open_error(DNN_INVALID,0); }
    if(!dnn_load_curl()) { free(u);free(cert);free(key);return dnn_open_error(DNN_UNSUPPORTED,0); }
    dnn_socket *s=(dnn_socket*)calloc(1,sizeof *s);
    if(!s) { free(u);free(cert);free(key);return dnn_open_error(DNN_LIMIT,0); }
    s->fd=DNN_BAD;s->kind=kind;s->curl=dnn_curl.init();
    if(!s->curl) { free(u);free(cert);free(key);free(s);return dnn_open_error(DNN_LIMIT,0); }
    atomic_fetch_add(&dnn_live,1);
    int error=0;
#define DNN_SET(o,v) do { if(!error) error=dnn_curl.setopt(s->curl,o,v); } while(0)
    DNN_SET(10002,u);DNN_SET(155,(long)ms);DNN_SET(156,(long)ms);
    DNN_SET(99,1L);DNN_SET(10004,"");DNN_SET(52,0L);DNN_SET(64,1L);DNN_SET(81,2L);
    DNN_SET(32,7L); /* TLS >=1.3; never disable certificate or hostname verification. */
    DNN_SET(10318,kind==DNN_WS?"ws,wss":"http,https");
    if(*cert) DNN_SET(10065,cert);
    if(*key) DNN_SET(10230,key);
#undef DNN_SET
    free(u);free(cert);free(key);
    if(error) { dlm_net_close(s);return dnn_open_error(dnn_curl_status(error),error); }
    dnn_open r={s,DNN_OK,0};return r;
}
dnn_open dlm_net_tls(dnn_text url,uint32_t ms,dnn_text ca,dnn_text pin) {
    dnn_open r=dnn_curl_new(url,ms,DNN_SECURE,ca,pin);if(!r.handle) return r;
    dnn_socket *s=(dnn_socket*)r.handle;
    int e=dnn_curl.setopt(s->curl,141,1L);
    if(!e) e=dnn_curl.setopt(s->curl,84,2L); /* Raw TLS stream, HTTP/1.1 ALPN. */
    if(!e) e=dnn_curl.perform(s->curl);
    if(!e) e=dnn_curl.getinfo(s->curl,0x500000+44,&s->fd);
    if(e) { dlm_net_close(s);return dnn_open_error(dnn_curl_status(e),e); }
    return r;
}
dnn_io dlm_net_send(void *handle,const uint8_t *data,size_t count) {
    dnn_socket *s=(dnn_socket*)handle;
    if(!s || s->failed || (s->kind!=DNN_TCP && s->kind!=DNN_SECURE) || !dnn_buffer(data,count)) return dnn_result(0,DNN_INVALID,0);
    if(!count) return dnn_result(0,DNN_OK,0);
    if(s->kind==DNN_SECURE) { size_t n=0;int e=dnn_curl.send(s->curl,data,count,&n);return dnn_result(n,dnn_curl_status(e),e); }
    int n=(int)send(s->fd,(const char*)data,(int)count,MSG_NOSIGNAL);
    if(n<0) { int e=dnn_socket_error();return dnn_result(0,DNN_AGAIN(e)?DNN_WOULD_BLOCK:DNN_SYSTEM,e); }
    return dnn_result((size_t)n,DNN_OK,0);
}
dnn_io dlm_net_recv(void *handle,uint8_t *data,size_t capacity) {
    dnn_socket *s=(dnn_socket*)handle;
    if(!s || s->failed || (s->kind!=DNN_TCP && s->kind!=DNN_SECURE) || !capacity || !dnn_buffer(data,capacity)) return dnn_result(0,DNN_INVALID,0);
    if(s->kind==DNN_SECURE) {
        size_t n=0;int e=dnn_curl.recv(s->curl,data,capacity,&n);
        return dnn_result(n,e?dnn_curl_status(e):(n?DNN_OK:DNN_EOF),e);
    }
    int n=(int)recv(s->fd,(char*)data,(int)capacity,0);
    if(n<0) { int e=dnn_socket_error();return dnn_result(0,DNN_AGAIN(e)?DNN_WOULD_BLOCK:DNN_SYSTEM,e); }
    return dnn_result((size_t)n,n?DNN_OK:DNN_EOF,0);
}
dnn_io dlm_net_transfer(void *handle,uint8_t *data,size_t count,uint32_t ms,void *cancel,bool sending) {
    dnn_socket *s=(dnn_socket*)handle;
    if(!s || s->failed || (s->kind!=DNN_TCP && s->kind!=DNN_SECURE) || !dnn_buffer(data,count)) return dnn_result(0,DNN_INVALID,0);
    uint64_t end=dnn_deadline(ms);size_t done=0;
    while(done<count) {
        if(dnn_cancelled(cancel)) return dnn_result(done,DNN_CANCELLED,0);
        dnn_io r=sending?dlm_net_send(handle,data+done,count-done):dlm_net_recv(handle,data+done,count-done);
        done+=r.count;
        if(r.status==DNN_WOULD_BLOCK) {
            dnn_io wait=dlm_net_wait(handle,sending?2:1,(uint32_t)dnn_remaining(end),cancel);
            if(wait.status) return dnn_result(done,wait.status,wait.system);
        } else if(r.status) return dnn_result(done,r.status,r.system);
        else if(!r.count) return dnn_result(done,DNN_PROTOCOL,0);
        if(done<count && !dnn_remaining(end)) return dnn_result(done,DNN_TIMEOUT,0);
    }
    return dnn_result(done,DNN_OK,0);
}
static size_t dnn_collect(char *data,size_t size,size_t count,void *context) {
    dnn_bytes *b=(dnn_bytes*)context;
    if(size && count>SIZE_MAX/size) { b->exceeded=true;return 0; }
    size_t n=size*count;
    if(n>b->limit-b->len) { b->exceeded=true;return 0; }
    if(b->len+n>b->cap) {
        size_t cap=b->cap?b->cap:1024;
        while(cap<b->len+n) { if(cap>b->limit/2) { cap=b->limit;break; }cap*=2; }
        if(cap>b->limit) cap=b->limit;
        void *p=realloc(b->data,cap);
        if(!p) { b->oom=true;return 0; }b->data=(uint8_t*)p;b->cap=cap;
    }
    if(n) memcpy(b->data+b->len,data,n);b->len+=n;return n;
}
dnn_open dlm_net_http(dnn_text url,uint32_t ms,size_t maximum,dnn_text ca,dnn_text pin) {
    if(!maximum || maximum>DNN_MAX) return dnn_open_error(DNN_LIMIT,0);
    dnn_open r=dnn_curl_new(url,ms,DNN_HTTP,ca,pin);if(!r.handle) return r;
    dnn_socket *s=(dnn_socket*)r.handle;s->body.limit=maximum;s->response_headers.limit=65536;
    int e=dnn_curl.setopt(s->curl,20011,dnn_collect);
    if(!e) e=dnn_curl.setopt(s->curl,10001,&s->body);
    if(!e) e=dnn_curl.setopt(s->curl,20079,dnn_collect);
    if(!e) e=dnn_curl.setopt(s->curl,10029,&s->response_headers);
    if(e) { dlm_net_close(s);return dnn_open_error(dnn_curl_status(e),e); }return r;
}
dnn_io dlm_net_http_version(void *handle,uint32_t version) {
    dnn_socket *s=(dnn_socket*)handle;if(!s || s->kind!=DNN_HTTP) return dnn_result(0,DNN_INVALID,0);
    uint32_t caps=dlm_net_capabilities();long setting=0;
    if(version==1) setting=2;
    else if(version==2) { if(!(caps&32)) return dnn_result(0,DNN_UNSUPPORTED,0);setting=5; }
    else if(version==3) { if(!(caps&64)) return dnn_result(0,DNN_UNSUPPORTED,0);setting=31; }
    else if(version!=0) return dnn_result(0,DNN_INVALID,0);
    int e=dnn_curl.setopt(s->curl,84,setting);if(!e) s->requested_version=version;
    return dnn_result(0,dnn_curl_status(e),e);
}
dnn_io dlm_net_http_header(void *handle,dnn_text header) {
    dnn_socket *s=(dnn_socket*)handle;
    if(!s || (s->kind!=DNN_HTTP && s->kind!=DNN_WS) || !header.len || header.len>8192 || !header.data || memchr(header.data,'\r',header.len) || memchr(header.data,'\n',header.len)) return dnn_result(0,DNN_INVALID,0);
    char *h=dnn_string(header,8192);if(!h) return dnn_result(0,DNN_INVALID,0);
    void *next=dnn_curl.list_append(s->headers,h);free(h);
    if(!next) return dnn_result(0,DNN_LIMIT,0);
    s->headers=next;int e=dnn_curl.setopt(s->curl,10023,s->headers);
    return dnn_result(0,dnn_curl_status(e),e);
}
dnn_io dlm_net_http_body(void *handle,const uint8_t *data,size_t count) {
    dnn_socket *s=(dnn_socket*)handle;
    if(!s || s->kind!=DNN_HTTP || !dnn_buffer(data,count)) return dnn_result(0,DNN_INVALID,0);
    int e=dnn_curl.setopt(s->curl,30120,(dnn_curl_off)count);
    if(!e) e=dnn_curl.setopt(s->curl,10165,data?(const char*)data:"");
    return dnn_result(0,dnn_curl_status(e),e);
}
dnn_io dlm_net_http_method(void *handle,dnn_text method) {
    dnn_socket *s=(dnn_socket*)handle;if(!s || s->kind!=DNN_HTTP || !method.len || method.len>32 || !method.data) return dnn_result(0,DNN_INVALID,0);
    for(size_t i=0;i<method.len;i++) {
        unsigned char c=method.data[i];
        if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || (c && strchr("!#$%&'*+-.^_`|~",c))))
            return dnn_result(0,DNN_INVALID,0);
    }
    char *m=dnn_string(method,32);if(!m) return dnn_result(0,DNN_INVALID,0);
    int e=dnn_curl.setopt(s->curl,10036,m);free(m);
    if(!e) e=dnn_curl.setopt(s->curl,44,method.len==4 && !memcmp(method.data,"HEAD",4)?1L:0L);
    return dnn_result(0,dnn_curl_status(e),e);
}
dnn_io dlm_net_http_ech(void *handle,dnn_text config) {
    dnn_socket *s=(dnn_socket*)handle;
    if(!s || s->kind!=DNN_HTTP) return dnn_result(0,DNN_INVALID,0);
    char *value=dnn_string(config,16384);if(!value) return dnn_result(0,DNN_INVALID,0);
    int e=dnn_curl.setopt(s->curl,10325,value);free(value);
    return dnn_result(0,dnn_curl_status(e),e);
}
static int dnn_progress(void *token,dnn_curl_off a,dnn_curl_off b,dnn_curl_off c,dnn_curl_off d) {
    (void)a;(void)b;(void)c;(void)d;return dnn_cancelled(token)?1:0;
}
dnn_io dlm_net_http_perform(void *handle,void *cancel) {
    dnn_socket *s=(dnn_socket*)handle;if(!s || s->kind!=DNN_HTTP) return dnn_result(0,DNN_INVALID,0);
    if(dnn_cancelled(cancel)) return dnn_result(0,DNN_CANCELLED,0);
    s->body.len=s->response_headers.len=0;s->body.exceeded=s->response_headers.exceeded=false;s->body.oom=s->response_headers.oom=false;
    int e=dnn_curl.setopt(s->curl,20219,dnn_progress);
    if(!e) e=dnn_curl.setopt(s->curl,10057,cancel);
    if(!e) e=dnn_curl.setopt(s->curl,43,0L);
    if(!e) e=dnn_curl.perform(s->curl);
    int status=(s->body.exceeded || s->response_headers.exceeded || s->body.oom || s->response_headers.oom)?DNN_LIMIT:dnn_curl_status(e);
    long actual=0;
    if(!status) {
        e=dnn_curl.getinfo(s->curl,0x200000+46,&actual);
        if(e) status=dnn_curl_status(e);
        else if((s->requested_version==1 && actual!=2) || (s->requested_version==2 && actual!=3) || (s->requested_version==3 && actual!=30)) status=DNN_PROTOCOL;
    }
    return dnn_result(s->body.len,status,e);
}
dnn_io dlm_net_http_info(void *handle,uint32_t field) {
    dnn_socket *s=(dnn_socket*)handle;if(!s || s->kind!=DNN_HTTP) return dnn_result(0,DNN_INVALID,0);
    if(field==2) return dnn_result(s->body.len,DNN_OK,0);
    if(field==3) return dnn_result(s->response_headers.len,DNN_OK,0);
    if(field>3) return dnn_result(0,DNN_INVALID,0);
    long value=0;int e=dnn_curl.getinfo(s->curl,field==0?0x200000+2:0x200000+46,&value);
    if(field==1) value=value==2?1:value==3?2:value==30?3:0;
    return dnn_result((size_t)value,dnn_curl_status(e),e);
}
dnn_io dlm_net_http_read(void *handle,bool headers,size_t offset,uint8_t *target,size_t capacity) {
    dnn_socket *s=(dnn_socket*)handle;
    if(!s || s->kind!=DNN_HTTP || !dnn_buffer(target,capacity)) return dnn_result(0,DNN_INVALID,0);
    dnn_bytes *b=headers?&s->response_headers:&s->body;
    if(offset>b->len) return dnn_result(0,DNN_INVALID,0);
    size_t n=b->len-offset;if(n>capacity) n=capacity;
    if(n) memcpy(target,b->data+offset,n);return dnn_result(n,DNN_OK,0);
}
dnn_open dlm_net_websocket(dnn_text url,uint32_t ms,dnn_text ca,dnn_text pin) {
    if(!(dlm_net_capabilities()&128)) return dnn_open_error(DNN_UNSUPPORTED,0);
    /* Delayed handshake permits authentication headers before ws_start. */
    return dnn_curl_new(url,ms,DNN_WS,ca,pin);
}
dnn_io dlm_net_ws_start(void *handle) {
    dnn_socket *s=(dnn_socket*)handle;if(!s || s->kind!=DNN_WS || s->fd!=DNN_BAD || s->failed) return dnn_result(0,DNN_INVALID,0);
    int e=dnn_curl.setopt(s->curl,141,2L);
    if(!e) e=dnn_curl.perform(s->curl);
    if(!e) e=dnn_curl.getinfo(s->curl,0x500000+44,&s->fd);
    if(e) s->failed=true;
    return dnn_result(0,dnn_curl_status(e),e);
}
dnn_io dlm_net_ws_send(void *handle,const uint8_t *data,size_t count,uint32_t flags,uint32_t ms,void *cancel) {
    dnn_socket *s=(dnn_socket*)handle;
    uint32_t type=flags&~4u;
    if(!s || s->kind!=DNN_WS || s->fd==DNN_BAD || s->failed || !dnn_buffer(data,count) ||
       (type!=1 && type!=2 && type!=8 && type!=16 && type!=64) || ((type==8 || type==16 || type==64) && (count>125 || (flags&4)))) return dnn_result(0,DNN_INVALID,0);
    uint64_t end=dnn_deadline(ms);size_t done=0;
    for(;;) {
        if(dnn_cancelled(cancel)) { if(done) s->failed=true;return dnn_result(done,DNN_CANCELLED,0); }
        size_t n=0;
        int e=dnn_curl.ws_send(s->curl,data?data+done:(const uint8_t*)"",count-done,&n,0,flags);
        done+=n;
        if(e && e!=81) { s->failed=true;return dnn_result(done,dnn_curl_status(e),e); }
        if(!e && done==count) return dnn_result(done,DNN_OK,0);
        dnn_io wait=dlm_net_wait(s,2,(uint32_t)dnn_remaining(end),cancel);
        if(wait.status) { s->failed=true;return dnn_result(done,wait.status,wait.system); }
        if(!dnn_remaining(end)) { s->failed=true;return dnn_result(done,DNN_TIMEOUT,0); }
    }
}
dnn_ws_io dlm_net_ws_recv(void *handle,uint8_t *data,size_t capacity) {
    dnn_ws_io r;memset(&r,0,sizeof r);dnn_socket *s=(dnn_socket*)handle;
    if(!s || s->kind!=DNN_WS || s->fd==DNN_BAD || s->failed || !capacity || !dnn_buffer(data,capacity)) { r.io.status=DNN_INVALID;return r; }
    const dnn_curl_frame *meta=NULL;size_t n=0;int e=dnn_curl.ws_recv(s->curl,data,capacity,&n,&meta);
    r.io=dnn_result(n,e==52?DNN_EOF:dnn_curl_status(e),e);
    if(!e && meta) { r.offset=(uint64_t)meta->offset;r.remaining=(uint64_t)meta->bytesleft;r.flags=(uint32_t)meta->flags; }
    return r;
}
dnn_io dlm_net_random(uint8_t *target,size_t count) {
    if(!dnn_buffer(target,count)) return dnn_result(0,DNN_INVALID,0);
    if(!count) return dnn_result(0,DNN_OK,0);
#if defined(_WIN32)
    typedef LONG (WINAPI *random_fn)(void*,unsigned char*,ULONG,ULONG);
    HMODULE module=LoadLibraryExW(L"bcrypt.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module) return dnn_result(0,DNN_SYSTEM,(int)GetLastError());
    random_fn generate=(random_fn)(void*)GetProcAddress(module,"BCryptGenRandom");
    LONG status=generate?generate(NULL,target,(ULONG)count,2):-1;FreeLibrary(module);
    if(status<0) return dnn_result(0,DNN_SYSTEM,(int)status);
#else
    int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);if(fd<0) return dnn_result(0,DNN_SYSTEM,errno);
    size_t done=0;
    while(done<count) {
        ssize_t n=read(fd,target+done,count-done);
        if(n<0 && errno==EINTR) continue;
        if(n<=0) { int e=errno;close(fd);return dnn_result(done,DNN_SYSTEM,e); }done+=(size_t)n;
    }
    close(fd);
#endif
    return dnn_result(count,DNN_OK,0);
}
/* DLN reactor v1. The reactor owns adopted sockets and all pending buffers.
 * No OS operation keeps a user buffer or a pointer into DLM memory. Calls on
 * a reactor are serialized by its owner; only legacy cancel signalling is
 * cross-thread. Readiness backends do not leave overlapped I/O in flight.
 */
#if defined(__linux__) && !defined(DLM_REACTOR_FORCE_POLL)
#include <sys/epoll.h>
#define DNR_BACKEND 1
#elif (defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)) && !defined(DLM_REACTOR_FORCE_POLL)
#include <sys/event.h>
#define DNR_BACKEND 2
#elif defined(_WIN32)
#define DNR_BACKEND 3
#else
#define DNR_BACKEND 4
#endif
enum { DNR_READ=1, DNR_WRITE, DNR_ACCEPT, DNR_TIMER, DNR_RECEIVE, DNR_SEND, DNR_EVENT, DNR_CONNECT };
typedef struct { uint64_t id, channel; int32_t status, system; } dnr_request;
typedef struct {
    uint64_t id, channel, accepted;
    size_t count;
    int32_t status, system;
    uint32_t kind;
    dnn_addr peer;
} dnr_completion;
typedef struct {
    size_t buffered_bytes, byte_limit;
    uint32_t channels, pending, completed, channel_limit, operation_limit, backend;
    int32_t status, system;
} dnr_stats;
typedef struct {
    uint64_t id;
    dnn_socket *socket;
    uint32_t reader, writer, registered;
    bool connecting;
} dnr_channel;
typedef struct {
    uint64_t id, channel, accepted, deadline;
    uint8_t *data;
    size_t capacity, count;
    uint32_t state, kind, previous, next;
    int32_t status, system;
    bool exact;
    dnn_addr peer;
#if defined(_WIN32)
    OVERLAPPED overlapped;
    WSABUF buffer;
    DWORD flags;
    struct sockaddr_storage address;
    int address_length;
    SOCKET accepting;
    uint8_t accept_addresses[2*(sizeof(struct sockaddr_storage)+16)];
    bool in_flight, stopping, reserved;
    int stop_status, stop_system;
#endif
} dnr_operation;
typedef struct dnx_control dnx_control;
typedef struct {
    uint32_t channel_limit, operation_limit, channels, pending, completed;
    uint32_t head, tail, cursor;
    size_t buffered_bytes, byte_limit;
    int fault;
    dnr_channel *slots;
    dnr_operation *operations;
    uint32_t *ready;
    dnx_control *control;
#if defined(_WIN32)
    HANDLE completion_port;
    uint32_t kernel_pending, accepting;
    bool closing;
#endif
#if DNR_BACKEND == 1
    int kernel;
    struct epoll_event *events;
#elif DNR_BACKEND == 2
    int kernel;
    struct kevent *events;
#else
#if defined(_WIN32)
    WSAPOLLFD *events;
#else
    struct pollfd *events;
#endif
    uint32_t *indices;
#endif
} dnr_reactor;
static void dnx_commands(dnr_reactor *r);
#if defined(_WIN32)
static void dnx_step(dnr_reactor *r,dnr_operation *o,bool immediate);
static void dnx_stop(dnr_reactor *r,dnr_operation *o,int status,int system);
static int dnx_wait(dnr_reactor *r,DWORD ms);
#elif DNR_BACKEND == 4
static int dnx_wake_fd(dnr_reactor *r);
#endif
static _Atomic uint64_t dnr_sequence=1;
static uint64_t dnr_identity(uint32_t index) {
    uint64_t value=atomic_load(&dnr_sequence);
    do { if(value>UINT64_MAX/4096) return 0; }
    while(!atomic_compare_exchange_weak(&dnr_sequence,&value,value+1));
    return value*4096+index;
}
static dnr_request dnr_ticket(uint64_t id,uint64_t channel,int status,int system) {
    dnr_request result={id,channel,status,system};return result;
}
static dnr_channel *dnr_slot(dnr_reactor *r,uint64_t id) {
    uint32_t index=(uint32_t)(id&4095);
    return r && id && index<r->channel_limit && r->slots[index].id==id?&r->slots[index]:NULL;
}
static dnr_operation *dnr_task(dnr_reactor *r,uint64_t id) {
    uint32_t index=(uint32_t)(id&4095);
    return r && id && index<r->operation_limit && r->operations[index].id==id?&r->operations[index]:NULL;
}
static uint32_t dnr_free_slot(dnr_reactor *r) {
#if defined(_WIN32)
    if(r->channels+r->accepting>=r->channel_limit) return 0;
#endif
    for(uint32_t i=0;i<r->channel_limit;i++) if(!r->slots[i].id) return i+1;
    return 0;
}
static void dnr_interest(dnr_reactor *r,dnr_channel *s) {
    uint32_t mask=(s->reader?1u:0u)|(s->writer?2u:0u);
    if(mask==s->registered || r->fault) return;
#if DNR_BACKEND == 1
    struct epoll_event event;memset(&event,0,sizeof event);event.data.u64=s->id;
    if(mask&1) event.events|=EPOLLIN|EPOLLRDHUP;
    if(mask&2) event.events|=EPOLLOUT;
    int command=!mask?EPOLL_CTL_DEL:!s->registered?EPOLL_CTL_ADD:EPOLL_CTL_MOD;
    if(epoll_ctl(r->kernel,command,s->socket->fd,&event)) { r->fault=errno;return; }
#elif DNR_BACKEND == 2
    for(uint32_t bit=1;bit<=2;bit*=2) if((mask^s->registered)&bit) {
        struct kevent event;
        EV_SET(&event,(uintptr_t)s->socket->fd,bit==1?EVFILT_READ:EVFILT_WRITE,
               (mask&bit)?EV_ADD:EV_DELETE,0,0,(void*)(uintptr_t)s->id);
        if(kevent(r->kernel,&event,1,NULL,0,NULL)<0) { r->fault=errno;return; }
    }
#endif
    s->registered=mask;
}
static void dnr_finish(dnr_reactor *r,dnr_operation *o,int status,int system) {
    if(o->state!=1) return;
#if defined(_WIN32)
    if(o->in_flight) { dnx_stop(r,o,status,system);return; }
    if(o->reserved) { r->accepting--;o->reserved=false; }
    if(o->kind==DNR_ACCEPT && o->accepting!=DNN_BAD) { dnn_close(o->accepting);o->accepting=DNN_BAD; }
#endif
    uint32_t index=(uint32_t)(o-r->operations)+1;
    dnr_channel *s=dnr_slot(r,o->channel);
    if(s) {
        if(s->reader==index) s->reader=0;
        if(s->writer==index) s->writer=0;
        dnr_interest(r,s);
        if(o->kind==DNR_CONNECT) {
            s->connecting=false;
            if(status) { dlm_net_close(s->socket);memset(s,0,sizeof *s);r->channels--; }
        }
    }
    o->state=2;o->status=status;o->system=system;
    o->previous=r->tail;o->next=0;
    if(r->tail) r->operations[r->tail-1].next=index;else r->head=index;
    r->tail=index;r->pending--;r->completed++;
}
static size_t dnr_finish_all(dnr_reactor *r,int status,int system) {
    size_t count=0;
    for(uint32_t i=0;i<r->operation_limit;i++) if(r->operations[i].state==1) {
        dnr_finish(r,&r->operations[i],status,system);count++;
    }
    return count;
}
static void dnr_step(dnr_reactor *r,dnr_operation *o,bool immediate) {
    if(o->state!=1) return;
    if(!immediate && dlm_net_clock_ms()>=o->deadline) { dnr_finish(r,o,DNN_TIMEOUT,0);return; }
#if defined(_WIN32)
    if(r->completion_port) { dnx_step(r,o,immediate);return; }
#endif
    dnr_channel *s=dnr_slot(r,o->channel);
    if(!s) { dnr_finish(r,o,DNN_INVALID,0);return; }
    dnn_io io=dnn_result(0,DNN_OK,0);
    if(o->kind==DNR_CONNECT) {
        int error=0;dnn_socklen length=sizeof error;
        if(getsockopt(s->socket->fd,SOL_SOCKET,SO_ERROR,(char*)&error,&length)) error=dnn_socket_error();
        if(DNN_AGAIN(error)) return;
        dnr_finish(r,o,error?DNN_SYSTEM:DNN_OK,error);return;
    }
    if(o->kind==DNR_ACCEPT) {
        uint32_t empty=dnr_free_slot(r);
        if(!empty) { dnr_finish(r,o,DNN_LIMIT,0);return; }
        dnn_open accepted=dlm_net_accept(s->socket);
        if(accepted.status==DNN_WOULD_BLOCK) return;
        if(accepted.status) { dnr_finish(r,o,accepted.status,accepted.system);return; }
        uint64_t id=dnr_identity(empty-1);
        if(!id) { dlm_net_close(accepted.handle);dnr_finish(r,o,DNN_LIMIT,0);return; }
        dnr_channel *child=&r->slots[empty-1];child->id=id;child->socket=(dnn_socket*)accepted.handle;
        r->channels++;o->accepted=id;dnr_finish(r,o,DNN_OK,0);return;
    }
    if(o->kind==DNR_RECEIVE) {
        dnn_datagram result=dlm_net_udp_recv(s->socket,o->data,o->capacity);
        io=result.io;o->peer=result.peer;
    } else if(o->kind==DNR_SEND) io=dlm_net_udp_send(s->socket,o->peer,o->data,o->capacity);
    else if(o->kind==DNR_READ) {
        if(o->count==o->capacity) { dnr_finish(r,o,DNN_OK,0);return; }
        io=dlm_net_recv(s->socket,o->data+o->count,o->capacity-o->count);
    } else if(o->kind==DNR_WRITE) {
        if(o->count==o->capacity) { dnr_finish(r,o,DNN_OK,0);return; }
        io=dlm_net_send(s->socket,o->data+o->count,o->capacity-o->count);
    }
    o->count+=io.count;
    if(io.status==DNN_WOULD_BLOCK || (io.status==DNN_SYSTEM && DNN_INTR(io.system))) return;
    if(io.status) { dnr_finish(r,o,io.status,io.system);return; }
    if(o->kind==DNR_RECEIVE || o->kind==DNR_SEND || o->count==o->capacity ||
       (o->kind==DNR_READ && !o->exact && o->count)) dnr_finish(r,o,DNN_OK,0);
}
static void dnr_expire(dnr_reactor *r) {
    if(r->control) dnx_commands(r);
    uint64_t now=dlm_net_clock_ms();
    for(uint32_t i=0;i<r->operation_limit;i++) {
        dnr_operation *o=&r->operations[i];
        if(o->state==1 && now>=o->deadline) dnr_finish(r,o,o->kind==DNR_TIMER?DNN_OK:DNN_TIMEOUT,0);
    }
}
static dnr_operation *dnr_allocate(dnr_reactor *r,uint32_t kind,uint64_t channel,size_t count,uint32_t ms) {
    if(r->pending+r->completed==r->operation_limit || count>r->byte_limit-r->buffered_bytes) return NULL;
    uint32_t i=0;while(i<r->operation_limit && r->operations[i].state) i++;
    if(i==r->operation_limit) return NULL;
    uint8_t *data=count?(uint8_t*)malloc(count):NULL;
    if(count && !data) return NULL;
    uint64_t id=dnr_identity(i);if(!id) { free(data);return NULL; }
    dnr_operation *o=&r->operations[i];memset(o,0,sizeof *o);
#if defined(_WIN32)
    o->accepting=DNN_BAD;
#endif
    o->id=id;o->channel=channel;o->kind=kind;o->capacity=count;o->data=data;
    o->deadline=dnn_deadline(ms);o->state=1;r->pending++;r->buffered_bytes+=count;return o;
}
void dlm_net_reactor_close(void *handle) {
    dnr_reactor *r=(dnr_reactor*)handle;if(!r) return;
#if DNR_BACKEND == 1 || DNR_BACKEND == 2
    if(r->kernel>=0) close(r->kernel);
#else
    free(r->indices);
#endif
    if(r->operations) for(uint32_t i=0;i<r->operation_limit;i++) free(r->operations[i].data);
    if(r->slots) for(uint32_t i=0;i<r->channel_limit;i++) if(r->slots[i].socket) dlm_net_close(r->slots[i].socket);
    free(r->events);free(r->ready);free(r->operations);free(r->slots);free(r);atomic_fetch_sub(&dnn_live,1);
}
dnn_open dlm_net_reactor_new(uint32_t channels,uint32_t operations,size_t byte_limit) {
    if(!channels || channels>4096 || !operations || operations>4096 || byte_limit>DNN_MAX) return dnn_open_error(DNN_INVALID,0);
    int error=dnn_os_init();if(error) return dnn_open_error(DNN_SYSTEM,error);
    dnr_reactor *r=(dnr_reactor*)calloc(1,sizeof *r);if(!r) return dnn_open_error(DNN_LIMIT,0);
    atomic_fetch_add(&dnn_live,1);r->channel_limit=channels;r->operation_limit=operations;r->byte_limit=byte_limit;
#if DNR_BACKEND == 1 || DNR_BACKEND == 2
    r->kernel=-1;
#endif
    r->slots=(dnr_channel*)calloc(channels,sizeof *r->slots);
    r->operations=(dnr_operation*)calloc(operations,sizeof *r->operations);
    r->ready=(uint32_t*)calloc(channels,sizeof *r->ready);
    r->events=calloc(channels*2,sizeof *r->events);
#if DNR_BACKEND == 3 || DNR_BACKEND == 4
    r->indices=(uint32_t*)calloc(channels,sizeof *r->indices);
#endif
    if(!r->slots || !r->operations || !r->ready || !r->events
#if DNR_BACKEND == 3 || DNR_BACKEND == 4
       || !r->indices
#endif
    ) { dlm_net_reactor_close(r);return dnn_open_error(DNN_LIMIT,0); }
#if DNR_BACKEND == 1
    r->kernel=epoll_create1(EPOLL_CLOEXEC);
#elif DNR_BACKEND == 2
    r->kernel=kqueue();
    if(r->kernel>=0 && fcntl(r->kernel,F_SETFD,FD_CLOEXEC)) { error=errno;dlm_net_reactor_close(r);return dnn_open_error(DNN_SYSTEM,error); }
#endif
#if DNR_BACKEND == 1 || DNR_BACKEND == 2
    if(r->kernel<0) { error=errno;dlm_net_reactor_close(r);return dnn_open_error(DNN_SYSTEM,error); }
#endif
    dnn_open result={r,DNN_OK,0};return result;
}
dnr_request dlm_net_reactor_adopt(void *handle,dnn_open socket,uint32_t kind) {
    dnr_reactor *r=(dnr_reactor*)handle;dnn_socket *s=(dnn_socket*)socket.handle;
    int status=socket.status,system=socket.system;
    if(!status && (!r || !s || s->failed || s->fd==DNN_BAD || s->kind!=(int)kind || kind<1 || kind>3)) status=DNN_INVALID;
    if(!status && r->fault) { status=DNN_SYSTEM;system=r->fault; }
    uint32_t empty=status?0:dnr_free_slot(r);if(!status && !empty) status=DNN_LIMIT;
    uint64_t id=status?0:dnr_identity(empty-1);if(!status && !id) status=DNN_LIMIT;
    if(status) { dlm_net_close(s);return dnr_ticket(0,0,status,system); }
    r->slots[empty-1].id=id;r->slots[empty-1].socket=s;r->channels++;return dnr_ticket(id,id,DNN_OK,0);
}
dnr_request dlm_net_reactor_submit(void *handle,uint64_t channel,uint32_t kind,const uint8_t *bytes,size_t count,bool exact,uint32_t ms,dnn_addr peer) {
    dnr_reactor *r=(dnr_reactor*)handle;dnr_channel *s=dnr_slot(r,channel);
    bool writing=kind==DNR_WRITE || kind==DNR_SEND;
    int required=(kind==DNR_READ || kind==DNR_WRITE)?DNN_TCP:kind==DNR_ACCEPT?DNN_LISTENER:DNN_UDP;
    if(!s || s->connecting || (kind!=DNR_READ && kind!=DNR_WRITE && kind!=DNR_ACCEPT && kind!=DNR_RECEIVE && kind!=DNR_SEND) ||
       s->socket->kind!=required || count>DNN_MAX || (kind==DNR_READ && !count) || (writing && !dnn_buffer(bytes,count)) ||
       (kind==DNR_ACCEPT && count)) return dnr_ticket(0,channel,DNN_INVALID,0);
    if(kind==DNR_SEND) {
        struct sockaddr_storage address;
        if(!peer.port || !dnn_to_sockaddr(&peer,&address)) return dnr_ticket(0,channel,DNN_INVALID,0);
        if(count>65507) return dnr_ticket(0,channel,DNN_LIMIT,0);
    }
    if(r->fault) return dnr_ticket(0,channel,DNN_SYSTEM,r->fault);
    if(writing?s->writer:s->reader) return dnr_ticket(0,channel,DNN_WOULD_BLOCK,0);
    if(kind==DNR_ACCEPT && !dnr_free_slot(r)) return dnr_ticket(0,channel,DNN_LIMIT,0);
    dnr_operation *o=dnr_allocate(r,kind,channel,count,ms);if(!o) return dnr_ticket(0,channel,DNN_LIMIT,0);
    o->exact=exact;o->peer=peer;if(writing && count) memcpy(o->data,bytes,count);
    uint32_t index=(uint32_t)(o-r->operations)+1;if(writing) s->writer=index;else s->reader=index;
    dnr_step(r,o,true);
    if(o->state==1 && !ms) dnr_finish(r,o,DNN_TIMEOUT,0);
    if(o->state==1) { dnr_interest(r,s);if(r->fault) dnr_finish(r,o,DNN_SYSTEM,r->fault); }
    return dnr_ticket(o->id,channel,DNN_OK,0);
}
dnr_request dlm_net_reactor_connect(void *handle,dnn_addr address,uint32_t ms) {
    dnr_reactor *r=(dnr_reactor*)handle;struct sockaddr_storage target;dnn_socklen length=dnn_to_sockaddr(&address,&target);
    if(!r || !length || !address.port) return dnr_ticket(0,0,DNN_INVALID,0);
    if(r->fault) return dnr_ticket(0,0,DNN_SYSTEM,r->fault);
    uint32_t empty=dnr_free_slot(r);
    if(!empty || r->pending+r->completed==r->operation_limit) return dnr_ticket(0,0,DNN_LIMIT,0);
    dnn_fd fd=socket(address.family==4?AF_INET:AF_INET6,SOCK_STREAM,IPPROTO_TCP);
    if(fd==DNN_BAD) return dnr_ticket(0,0,DNN_SYSTEM,dnn_socket_error());
    if(dnn_nonblocking(fd)) { int error=dnn_socket_error();dnn_close(fd);return dnr_ticket(0,0,DNN_SYSTEM,error); }
    dnn_open opened=dnn_adopt(fd,DNN_TCP);if(opened.status) return dnr_ticket(0,0,opened.status,opened.system);
    dnr_request adopted=dlm_net_reactor_adopt(r,opened,DNN_TCP);if(adopted.status) return adopted;
    dnr_channel *s=dnr_slot(r,adopted.channel);
    dnr_operation *o=dnr_allocate(r,DNR_CONNECT,s->id,0,ms);
    if(!o) { dlm_net_close(s->socket);memset(s,0,sizeof *s);r->channels--;return dnr_ticket(0,0,DNN_LIMIT,0); }
    s->writer=(uint32_t)(o-r->operations)+1;s->connecting=true;
    int result=connect(fd,(struct sockaddr*)&target,length),error=result?dnn_socket_error():0;
    if(!result) dnr_finish(r,o,DNN_OK,0);
    else if(!DNN_AGAIN(error)) dnr_finish(r,o,DNN_SYSTEM,error);
    else if(!ms) dnr_finish(r,o,DNN_TIMEOUT,0);
    else { dnr_interest(r,s);if(r->fault) dnr_finish(r,o,DNN_SYSTEM,r->fault); }
    return dnr_ticket(o->id,o->channel,DNN_OK,0);
}
dnr_request dlm_net_reactor_timer(void *handle,uint32_t ms,bool event) {
    dnr_reactor *r=(dnr_reactor*)handle;if(!r) return dnr_ticket(0,0,DNN_INVALID,0);
    if(r->fault) return dnr_ticket(0,0,DNN_SYSTEM,r->fault);
    dnr_operation *o=dnr_allocate(r,event?DNR_EVENT:DNR_TIMER,0,0,ms);
    if(!o) return dnr_ticket(0,0,DNN_LIMIT,0);
    if(!ms) dnr_finish(r,o,event?DNN_TIMEOUT:DNN_OK,0);
    return dnr_ticket(o->id,0,DNN_OK,0);
}
dnn_io dlm_net_reactor_signal(void *handle,uint64_t operation) {
    dnr_reactor *r=(dnr_reactor*)handle;dnr_operation *o=dnr_task(r,operation);
    if(!o || o->kind!=DNR_EVENT || o->state!=1) return dnn_result(0,DNN_INVALID,0);
    if(dlm_net_clock_ms()>=o->deadline) { dnr_finish(r,o,DNN_TIMEOUT,0);return dnn_result(0,DNN_TIMEOUT,0); }
    dnr_finish(r,o,DNN_OK,0);return dnn_result(0,DNN_OK,0);
}
dnn_io dlm_net_reactor_cancel(void *handle,uint64_t operation) {
    dnr_reactor *r=(dnr_reactor*)handle;if(!r) return dnn_result(0,DNN_INVALID,0);
    if(!operation) return dnn_result(dnr_finish_all(r,DNN_CANCELLED,0),DNN_OK,0);
    dnr_operation *o=dnr_task(r,operation);if(!o) return dnn_result(0,DNN_INVALID,0);
    if(o->state==1) { dnr_finish(r,o,DNN_CANCELLED,0);return dnn_result(1,DNN_OK,0); }
    return dnn_result(0,DNN_OK,0);
}
static int dnr_wait(dnr_reactor *r,int ms) {
    memset(r->ready,0,r->channel_limit*sizeof *r->ready);
#if defined(_WIN32)
    if(r->completion_port) return dnx_wait(r,(DWORD)ms);
#endif
#if DNR_BACKEND == 1
    int count=epoll_wait(r->kernel,r->events,(int)r->channel_limit,ms);
    if(count<0) return -errno;
    for(int i=0;i<count;i++) {
        dnr_channel *s=dnr_slot(r,r->events[i].data.u64);if(!s) continue;
        uint32_t events=r->events[i].events,mask=0;
        if(events&(EPOLLIN|EPOLLRDHUP)) mask|=1;
        if(events&EPOLLOUT) mask|=2;
        if(events&(EPOLLERR|EPOLLHUP)) mask|=3;
        r->ready[s-r->slots]|=mask;
    }
#elif DNR_BACKEND == 2
    struct timespec timeout={ms/1000,(long)(ms%1000)*1000000};
    int count=kevent(r->kernel,NULL,0,r->events,(int)r->channel_limit*2,&timeout);
    if(count<0) return -errno;
    for(int i=0;i<count;i++) {
        dnr_channel *s=dnr_slot(r,(uint64_t)(uintptr_t)r->events[i].udata);if(!s) continue;
        r->ready[s-r->slots]|=(r->events[i].filter==EVFILT_READ?1u:2u);
        if(r->events[i].flags&(EV_ERROR|EV_EOF)) r->ready[s-r->slots]|=3;
    }
#else
    uint32_t used=0;
#if !defined(_WIN32)
    if(r->control) {
        r->indices[used]=UINT32_MAX;r->events[used].fd=dnx_wake_fd(r);
        r->events[used].events=POLLIN;r->events[used].revents=0;used++;
    }
#endif
    for(uint32_t i=0;i<r->channel_limit;i++) if(r->slots[i].id && r->slots[i].registered) {
        r->indices[used]=i;r->events[used].fd=r->slots[i].socket->fd;
        r->events[used].events=0;r->events[used].revents=0;
        if(r->slots[i].registered&1) r->events[used].events|=POLLIN;
        if(r->slots[i].registered&2) r->events[used].events|=POLLOUT;
        used++;
    }
#if defined(_WIN32)
    if(!used) { Sleep((DWORD)ms);return 0; }
    int count=WSAPoll(r->events,(ULONG)used,ms);
#else
    int count=poll(r->events,(nfds_t)used,ms);
#endif
    if(count<0) return -dnn_socket_error();
    for(uint32_t i=0;i<used;i++) {
        if(r->indices[i]==UINT32_MAX) continue;
        short events=r->events[i].revents;uint32_t mask=0;
        if(events&POLLIN) mask|=1;if(events&POLLOUT) mask|=2;
        if(events&(POLLERR|POLLHUP|POLLNVAL)) mask|=3;
        r->ready[r->indices[i]]=mask;
    }
#endif
    return count;
}
dnn_io dlm_net_reactor_poll(void *handle,uint32_t ms,void *cancel) {
    dnr_reactor *r=(dnr_reactor*)handle;if(!r) return dnn_result(0,DNN_INVALID,0);
    uint64_t end=dnn_deadline(ms);
    for(;;) {
        if(dnn_cancelled(cancel)) { dnr_finish_all(r,DNN_CANCELLED,0);return dnn_result(r->completed,DNN_CANCELLED,0); }
        if(r->fault) { dnr_finish_all(r,DNN_SYSTEM,r->fault);return dnn_result(r->completed,DNN_SYSTEM,r->fault); }
        dnr_expire(r);
        if(r->completed) return dnn_result(r->completed,DNN_OK,0);
        if(!r->pending) return dnn_result(0,DNN_WOULD_BLOCK,0);
        uint64_t deadline=end;
        for(uint32_t i=0;i<r->operation_limit;i++) if(r->operations[i].state==1 && r->operations[i].deadline<deadline) deadline=r->operations[i].deadline;
        int wait=dnn_remaining(deadline);if(cancel && wait>50) wait=50;
        int count=dnr_wait(r,wait);
        if(count<0) {
            int error=-count;if(!DNN_INTR(error)) { r->fault=error;continue; }
        }
        if(dnn_cancelled(cancel)) { dnr_finish_all(r,DNN_CANCELLED,0);return dnn_result(r->completed,DNN_CANCELLED,0); }
        dnr_expire(r);
        uint32_t start=r->cursor;
        for(uint32_t offset=0;offset<r->channel_limit;offset++) {
            uint32_t i=(start+offset)%r->channel_limit;dnr_channel *s=&r->slots[i];uint32_t mask=r->ready[i];
            uint32_t reader=s->reader,writer=s->writer;
            if((mask&1) && reader) dnr_step(r,&r->operations[reader-1],false);
            if((mask&2) && writer) dnr_step(r,&r->operations[writer-1],false);
            if(mask) r->cursor=(i+1)%r->channel_limit;
        }
        if(r->completed) return dnn_result(r->completed,DNN_OK,0);
        if(!dnn_remaining(end)) return dnn_result(0,DNN_TIMEOUT,0);
    }
}
dnr_completion dlm_net_reactor_result(void *handle,uint64_t operation) {
    dnr_reactor *r=(dnr_reactor*)handle;dnr_completion result;memset(&result,0,sizeof result);
    if(!r) { result.status=DNN_INVALID;return result; }
    dnr_operation *o=operation?dnr_task(r,operation):r && r->head?&r->operations[r->head-1]:NULL;
    if(!o) { result.status=operation?DNN_INVALID:DNN_WOULD_BLOCK;return result; }
    result.id=o->id;result.channel=o->channel;result.accepted=o->accepted;result.count=o->count;
    result.status=o->state==1?DNN_WOULD_BLOCK:o->status;result.system=o->system;result.kind=o->kind;result.peer=o->peer;return result;
}
dnn_io dlm_net_reactor_copy(void *handle,uint64_t operation,size_t offset,uint8_t *target,size_t capacity) {
    dnr_reactor *r=(dnr_reactor*)handle;dnr_operation *o=dnr_task(r,operation);
    if(!o || o->state!=2 || (o->kind!=DNR_READ && o->kind!=DNR_RECEIVE) || offset>o->count || !dnn_buffer(target,capacity)) return dnn_result(0,DNN_INVALID,0);
    size_t count=o->count-offset;if(count>capacity) count=capacity;
    if(count) memcpy(target,o->data+offset,count);return dnn_result(count,DNN_OK,0);
}
dnn_io dlm_net_reactor_ack(void *handle,uint64_t operation) {
    dnr_reactor *r=(dnr_reactor*)handle;dnr_operation *o=dnr_task(r,operation);
    if(!o || o->state!=2) return dnn_result(0,DNN_INVALID,0);
    if(o->previous) r->operations[o->previous-1].next=o->next;else r->head=o->next;
    if(o->next) r->operations[o->next-1].previous=o->previous;else r->tail=o->previous;
    r->buffered_bytes-=o->capacity;r->completed--;free(o->data);memset(o,0,sizeof *o);return dnn_result(0,DNN_OK,0);
}
dnn_io dlm_net_reactor_channel_close(void *handle,uint64_t channel) {
    dnr_reactor *r=(dnr_reactor*)handle;dnr_channel *s=dnr_slot(r,channel);
    if(!s) return dnn_result(0,DNN_INVALID,0);
    if(s->reader) dnr_finish(r,&r->operations[s->reader-1],DNN_CANCELLED,0);
    if(s->writer) dnr_finish(r,&r->operations[s->writer-1],DNN_CANCELLED,0);
    if(s->id) { dlm_net_close(s->socket);memset(s,0,sizeof *s);r->channels--; }
    return dnn_result(0,DNN_OK,0);
}
dnn_open dlm_net_reactor_release(void *handle,uint64_t channel,uint32_t kind) {
    dnr_reactor *r=(dnr_reactor*)handle;dnr_channel *s=dnr_slot(r,channel);
    if(!s || s->connecting || s->socket->kind!=(int)kind) return dnn_open_error(DNN_INVALID,0);
    if(r->fault) return dnn_open_error(DNN_SYSTEM,r->fault);
    for(uint32_t i=0;i<r->operation_limit;i++) if(r->operations[i].state && r->operations[i].channel==channel) return dnn_open_error(DNN_WOULD_BLOCK,0);
    dnn_open result={s->socket,DNN_OK,0};memset(s,0,sizeof *s);r->channels--;return result;
}
dnr_stats dlm_net_reactor_stats(void *handle) {
    dnr_stats result;memset(&result,0,sizeof result);dnr_reactor *r=(dnr_reactor*)handle;
    if(!r) { result.status=DNN_INVALID;return result; }
    result.buffered_bytes=r->buffered_bytes;result.byte_limit=r->byte_limit;result.channels=r->channels;
    result.pending=r->pending;result.completed=r->completed;result.channel_limit=r->channel_limit;
    result.operation_limit=r->operation_limit;result.backend=DNR_BACKEND;result.status=r->fault?DNN_SYSTEM:DNN_OK;result.system=r->fault;return result;
}

/* DLN scoped operations. Legacy reactor owners keep their readiness contract.
 * Scope owners add a bounded cross-thread command queue and, on Windows, an
 * IOCP driver. A scope never releases a socket from its completion port.
 */
typedef struct { uint64_t operation, posted; uint32_t action; } dnx_command;
struct dnx_control {
    atomic_flag lock;
    _Atomic uint32_t references;
    bool open, notified;
    uint32_t capacity, head, count;
#if defined(_WIN32)
    HANDLE port;
#else
    int reader, writer;
#endif
    dnx_command commands[];
};
typedef struct { dnx_control *control; uint64_t operation; uint32_t action; } dnx_waker;
typedef struct { dnr_stats reactor; uint32_t queued, queue_limit, kernel_pending; } dnx_stats;
static void dnx_lock(dnx_control *c) {
    while(atomic_flag_test_and_set_explicit(&c->lock,memory_order_acquire)) {
#if defined(_WIN32)
        SwitchToThread();
#endif
    }
}
static void dnx_unlock(dnx_control *c) { atomic_flag_clear_explicit(&c->lock,memory_order_release); }
static void dnx_unref(dnx_control *c) {
    if(atomic_fetch_sub_explicit(&c->references,1,memory_order_acq_rel)==1) free(c);
}
static int dnx_notify(dnx_control *c) {
    if(c->notified) return 0;
#if defined(_WIN32)
    if(!PostQueuedCompletionStatus(c->port,0,0,NULL)) return (int)GetLastError();
#else
    uint8_t byte=1;ssize_t written;
    do { written=write(c->writer,&byte,1); } while(written<0 && errno==EINTR);
    if(written<0 && errno!=EAGAIN && errno!=EWOULDBLOCK) return errno;
#endif
    c->notified=true;return 0;
}
#if DNR_BACKEND == 4
static int dnx_wake_fd(dnr_reactor *r) { return r->control->reader; }
#endif
static void dnx_commands(dnr_reactor *r) {
    dnx_control *c=r->control;if(!c) return;
#if !defined(_WIN32)
    uint8_t bytes[64];
    dnx_lock(c);
    ssize_t received;
    do { received=read(c->reader,bytes,sizeof bytes); } while(received>0 || (received<0 && errno==EINTR));
    if(received<0 && errno!=EAGAIN && errno!=EWOULDBLOCK) r->fault=errno;
    c->notified=false;dnx_unlock(c);
#endif
    for(uint32_t i=0;i<c->capacity;i++) {
        dnx_lock(c);
        if(!c->count) { dnx_unlock(c);break; }
        dnx_command command=c->commands[c->head];c->head=(c->head+1)%c->capacity;c->count--;
        dnx_unlock(c);
        if(command.action==3) { dnr_finish_all(r,DNN_CANCELLED,0);continue; }
        dnr_operation *o=dnr_task(r,command.operation);
        if(!o || o->state!=1) continue;
        if(command.action==2) dnr_finish(r,o,DNN_CANCELLED,0);
        else if(o->kind==DNR_EVENT) dnr_finish(r,o,command.posted>=o->deadline?DNN_TIMEOUT:DNN_OK,0);
    }
    dnx_lock(c);
    if(c->count && c->open) { int error=dnx_notify(c);if(error) r->fault=error; }
    dnx_unlock(c);
}

#if defined(_WIN32)
static bool dnx_associate(dnr_reactor *r,dnr_channel *s) {
    if(CreateIoCompletionPort((HANDLE)s->socket->fd,r->completion_port,(ULONG_PTR)s->id,1)) return true;
    r->fault=(int)GetLastError();return false;
}
static bool dnx_extension(SOCKET socket,GUID id,void *function,DWORD length,int *error) {
    DWORD bytes=0;
    if(!WSAIoctl(socket,SIO_GET_EXTENSION_FUNCTION_POINTER,&id,sizeof id,function,length,&bytes,NULL,NULL)) return true;
    *error=WSAGetLastError();return false;
}
static void dnx_start(dnr_reactor *r,dnr_operation *o) {
    if(o->state!=1 || o->in_flight) return;
    if(r->closing) { dnr_finish(r,o,DNN_CANCELLED,0);return; }
    dnr_channel *s=dnr_slot(r,o->channel);
    if(!s) { dnr_finish(r,o,DNN_INVALID,0);return; }
    if((o->kind==DNR_WRITE && o->count==o->capacity) || (o->kind==DNR_READ && o->count==o->capacity)) {
        dnr_finish(r,o,DNN_OK,0);return;
    }
    memset(&o->overlapped,0,sizeof o->overlapped);o->flags=0;
    o->buffer.buf=o->capacity?(char*)o->data+o->count:(char*)o->accept_addresses;
    o->buffer.len=(ULONG)(o->capacity-o->count);
    DWORD bytes=0;int result=SOCKET_ERROR,error=0;
    o->in_flight=true;r->kernel_pending++;
    if(o->kind==DNR_READ) result=WSARecv(s->socket->fd,&o->buffer,1,NULL,&o->flags,&o->overlapped,NULL);
    else if(o->kind==DNR_WRITE) result=WSASend(s->socket->fd,&o->buffer,1,NULL,0,&o->overlapped,NULL);
    else if(o->kind==DNR_RECEIVE) {
        o->address_length=sizeof o->address;memset(&o->address,0,sizeof o->address);
        result=WSARecvFrom(s->socket->fd,&o->buffer,1,NULL,&o->flags,(struct sockaddr*)&o->address,&o->address_length,&o->overlapped,NULL);
    } else if(o->kind==DNR_SEND) {
        o->address_length=dnn_to_sockaddr(&o->peer,&o->address);
        result=WSASendTo(s->socket->fd,&o->buffer,1,NULL,0,(struct sockaddr*)&o->address,o->address_length,&o->overlapped,NULL);
    } else if(o->kind==DNR_ACCEPT) {
        LPFN_ACCEPTEX accept_function=NULL;GUID id=WSAID_ACCEPTEX;
        struct sockaddr_storage local;int length=sizeof local;
        if(r->channels+r->accepting>=r->channel_limit) error=WSAENOBUFS;
        else if(getsockname(s->socket->fd,(struct sockaddr*)&local,&length)) error=WSAGetLastError();
        else if(dnx_extension(s->socket->fd,id,&accept_function,sizeof accept_function,&error)) {
            o->accepting=WSASocketW(local.ss_family,SOCK_STREAM,IPPROTO_TCP,NULL,0,WSA_FLAG_OVERLAPPED);
            if(o->accepting==DNN_BAD) error=WSAGetLastError();
            else {
                r->accepting++;o->reserved=true;
                if(accept_function(s->socket->fd,o->accepting,o->accept_addresses,0,
                                   sizeof(struct sockaddr_storage)+16,sizeof(struct sockaddr_storage)+16,&bytes,&o->overlapped)) result=0;
            }
        }
    } else if(o->kind==DNR_CONNECT) {
        LPFN_CONNECTEX connect_function=NULL;GUID id=WSAID_CONNECTEX;
        struct sockaddr_storage local;memset(&local,0,sizeof local);local.ss_family=o->address.ss_family;
        int length=local.ss_family==AF_INET?(int)sizeof(struct sockaddr_in):(int)sizeof(struct sockaddr_in6);
        if(bind(s->socket->fd,(struct sockaddr*)&local,length)) error=WSAGetLastError();
        else if(dnx_extension(s->socket->fd,id,&connect_function,sizeof connect_function,&error)) {
            if(connect_function(s->socket->fd,(struct sockaddr*)&o->address,o->address_length,NULL,0,&bytes,&o->overlapped)) result=0;
        }
    } else error=WSAEINVAL;
    if(!result) return; /* Synchronous success still queues one completion. */
    if(!error) error=WSAGetLastError();
    if(error==WSA_IO_PENDING) return;
    o->in_flight=false;r->kernel_pending--;
    if(o->kind==DNR_RECEIVE && error==WSAEMSGSIZE) {
        o->count=o->capacity;dnn_from_sockaddr(&o->peer,(struct sockaddr*)&o->address);
        dnr_finish(r,o,DNN_TRUNCATED,error);
    } else dnr_finish(r,o,error==WSAENOBUFS?DNN_LIMIT:DNN_SYSTEM,error);
}
static void dnx_complete(dnr_reactor *r,dnr_operation *o,DWORD bytes,int error) {
    o->in_flight=false;r->kernel_pending--;
    dnr_channel *s=dnr_slot(r,o->channel);
    if(o->kind==DNR_READ || o->kind==DNR_WRITE || o->kind==DNR_RECEIVE || o->kind==DNR_SEND) {
        size_t remaining=o->capacity-o->count;
        bool truncated=o->kind==DNR_RECEIVE && (error==WSAEMSGSIZE || error==ERROR_MORE_DATA);
        if(bytes>remaining && !truncated) { dnr_finish(r,o,DNN_PROTOCOL,0);return; }
        o->count+=(size_t)bytes>remaining?remaining:(size_t)bytes;
    }
    if(o->kind==DNR_RECEIVE) dnn_from_sockaddr(&o->peer,(struct sockaddr*)&o->address);
    if(o->stopping) { dnr_finish(r,o,o->stop_status,o->stop_system);return; }
    if(r->closing) { dnr_finish(r,o,DNN_CANCELLED,0);return; }
    if(error) {
        int status=(o->kind==DNR_RECEIVE && (error==WSAEMSGSIZE || error==ERROR_MORE_DATA))?DNN_TRUNCATED:
                   error==ERROR_OPERATION_ABORTED?DNN_CANCELLED:DNN_SYSTEM;
        if(status==DNN_TRUNCATED) o->count=o->capacity;
        dnr_finish(r,o,status,error);return;
    }
    if(!s) { dnr_finish(r,o,DNN_INVALID,0);return; }
    if(o->kind==DNR_ACCEPT) {
        SOCKET accepted=o->accepting;
        if(setsockopt(accepted,SOL_SOCKET,SO_UPDATE_ACCEPT_CONTEXT,(const char*)&s->socket->fd,sizeof s->socket->fd) || dnn_nonblocking(accepted)) {
            dnr_finish(r,o,DNN_SYSTEM,WSAGetLastError());return;
        }
        if(o->reserved) { r->accepting--;o->reserved=false; }
        uint32_t empty=dnr_free_slot(r);uint64_t id=empty?dnr_identity(empty-1):0;
        if(!id) { dnr_finish(r,o,DNN_LIMIT,0);return; }
        o->accepting=DNN_BAD;
        dnn_open opened=dnn_adopt(accepted,DNN_TCP);
        if(opened.status) { dnr_finish(r,o,opened.status,opened.system);return; }
        dnr_channel *child=&r->slots[empty-1];child->id=id;child->socket=(dnn_socket*)opened.handle;r->channels++;
        if(!dnx_associate(r,child)) {
            int failure=r->fault;dlm_net_close(child->socket);memset(child,0,sizeof *child);r->channels--;
            dnr_finish(r,o,DNN_SYSTEM,failure);return;
        }
        o->accepted=id;dnr_finish(r,o,DNN_OK,0);return;
    }
    if(o->kind==DNR_CONNECT) {
        int failure=setsockopt(s->socket->fd,SOL_SOCKET,SO_UPDATE_CONNECT_CONTEXT,NULL,0)?WSAGetLastError():0;
        dnr_finish(r,o,failure?DNN_SYSTEM:DNN_OK,failure);return;
    }
    if(o->kind==DNR_READ && !bytes) { dnr_finish(r,o,DNN_EOF,0);return; }
    if(o->kind==DNR_SEND && o->count!=o->capacity) { dnr_finish(r,o,DNN_PROTOCOL,0);return; }
    if(o->kind==DNR_RECEIVE || o->kind==DNR_SEND || o->count==o->capacity || (o->kind==DNR_READ && !o->exact)) {
        dnr_finish(r,o,DNN_OK,0);return;
    }
    if(dlm_net_clock_ms()>=o->deadline) dnr_finish(r,o,DNN_TIMEOUT,0);
    else dnx_start(r,o);
}
static int dnx_wait(dnr_reactor *r,DWORD ms) {
    int count=0;
    for(uint32_t turn=0;turn<r->operation_limit;turn++) {
        DWORD bytes=0;ULONG_PTR key=0;OVERLAPPED *overlapped=NULL;
        BOOL success=GetQueuedCompletionStatus(r->completion_port,&bytes,&key,&overlapped,turn?0:ms);
        int error=success?0:(int)GetLastError();
        if(!overlapped) {
            if(!success) return error==WAIT_TIMEOUT?count:-error;
            if(r->control) { dnx_lock(r->control);r->control->notified=false;dnx_unlock(r->control); }
            count++;continue; /* Bounded command queue notification. */
        }
        uintptr_t address=(uintptr_t)overlapped,first=(uintptr_t)r->operations+offsetof(dnr_operation,overlapped);
        if(address<first || (address-first)%sizeof(dnr_operation) || (address-first)/sizeof(dnr_operation)>=r->operation_limit) return -ERROR_INVALID_DATA;
        dnr_operation *o=&r->operations[(address-first)/sizeof(dnr_operation)];
        if(!o->in_flight || o->state!=1 || key!=(ULONG_PTR)o->channel) return -ERROR_INVALID_DATA;
        dnx_complete(r,o,bytes,error);count++;
    }
    return count;
}
static void dnx_step(dnr_reactor *r,dnr_operation *o,bool immediate) {
    dnx_start(r,o);
    if(immediate) { int result=dnx_wait(r,0);if(result<0) r->fault=-result; }
}
static void dnx_stop(dnr_reactor *r,dnr_operation *o,int status,int system) {
    o->stopping=true;o->stop_status=status;o->stop_system=system;
    dnr_channel *s=dnr_slot(r,o->channel);
    if(s) CancelIoEx((HANDLE)s->socket->fd,&o->overlapped);
    /* CancelIoEx does not retire the buffer. Dequeue the completion before
     * publishing a terminal result, reusing OVERLAPPED or releasing memory. */
    while(o->in_flight) {
        int result=dnx_wait(r,INFINITE);
        if(result<0) r->fault=-result;
    }
}
#endif

void dlm_net_async_close(void *handle) {
    dnr_reactor *r=(dnr_reactor*)handle;if(!r) return;
    dnx_control *c=r->control;
    if(c) {
        dnx_lock(c);c->open=false;c->count=0;
#if defined(_WIN32)
        c->port=NULL;
#else
        if(c->reader>=0) close(c->reader);
        if(c->writer>=0) close(c->writer);
        c->reader=c->writer=-1;
#endif
        dnx_unlock(c);r->control=NULL;
    }
#if defined(_WIN32)
    r->closing=true;
#endif
    dnr_finish_all(r,DNN_CANCELLED,0);
#if defined(_WIN32)
    if(r->completion_port) { CloseHandle(r->completion_port);r->completion_port=NULL; }
#endif
    dlm_net_reactor_close(r);if(c) dnx_unref(c);
}
dnn_open dlm_net_async_new(uint32_t channels,uint32_t operations,size_t byte_limit,uint32_t wake_limit) {
    if(!wake_limit || wake_limit>4096) return dnn_open_error(DNN_INVALID,0);
    dnn_open opened=dlm_net_reactor_new(channels,operations,byte_limit);if(opened.status) return opened;
    dnr_reactor *r=(dnr_reactor*)opened.handle;
    dnx_control *c=(dnx_control*)calloc(1,sizeof *c+(size_t)wake_limit*sizeof(dnx_command));
    if(!c) { dlm_net_reactor_close(r);return dnn_open_error(DNN_LIMIT,0); }
    atomic_flag_clear(&c->lock);atomic_init(&c->references,1);c->capacity=wake_limit;c->open=true;r->control=c;
    int error=0;
#if defined(_WIN32)
    r->completion_port=CreateIoCompletionPort(INVALID_HANDLE_VALUE,NULL,0,1);c->port=r->completion_port;
    if(!r->completion_port) error=(int)GetLastError();
#else
    c->reader=c->writer=-1;int descriptors[2];
    if(pipe(descriptors)) error=errno;
    else {
        c->reader=descriptors[0];c->writer=descriptors[1];
        for(int i=0;i<2;i++) {
            int flags=fcntl(descriptors[i],F_GETFL,0);
            if(flags<0 || fcntl(descriptors[i],F_SETFL,flags|O_NONBLOCK)<0 || fcntl(descriptors[i],F_SETFD,FD_CLOEXEC)<0) { error=errno;break; }
        }
    }
#if DNR_BACKEND == 1
    if(!error) {
        struct epoll_event event;memset(&event,0,sizeof event);event.events=EPOLLIN;
        if(epoll_ctl(r->kernel,EPOLL_CTL_ADD,c->reader,&event)) error=errno;
    }
#elif DNR_BACKEND == 2
    if(!error) {
        struct kevent event;EV_SET(&event,(uintptr_t)c->reader,EVFILT_READ,EV_ADD,0,0,NULL);
        if(kevent(r->kernel,&event,1,NULL,0,NULL)<0) error=errno;
    }
#else
    if(!error) {
        void *events=realloc(r->events,(channels+1)*sizeof *r->events);
        if(!events) error=ENOMEM;else r->events=events;
        void *indices=realloc(r->indices,(channels+1)*sizeof *r->indices);
        if(!indices) error=ENOMEM;else r->indices=(uint32_t*)indices;
    }
#endif
#endif
    if(error) { dlm_net_async_close(r);return dnn_open_error(DNN_SYSTEM,error); }
    return opened;
}
dnr_request dlm_net_async_adopt(void *handle,dnn_open socket,uint32_t kind) {
    dnr_reactor *r=(dnr_reactor*)handle;
    if(!r || !r->control) { dlm_net_close(socket.handle);return dnr_ticket(0,0,DNN_INVALID,0); }
    dnr_request result=dlm_net_reactor_adopt(r,socket,kind);
#if defined(_WIN32)
    if(!result.status && !dnx_associate(r,dnr_slot(r,result.channel))) {
        dlm_net_reactor_channel_close(r,result.channel);return dnr_ticket(0,0,DNN_SYSTEM,r->fault);
    }
#endif
    return result;
}
dnr_request dlm_net_async_connect(void *handle,dnn_addr address,uint32_t ms) {
    dnr_reactor *r=(dnr_reactor*)handle;
    if(!r || !r->control) return dnr_ticket(0,0,DNN_INVALID,0);
#if defined(_WIN32)
    struct sockaddr_storage target;dnn_socklen length=dnn_to_sockaddr(&address,&target);
    if(!length || !address.port) return dnr_ticket(0,0,DNN_INVALID,0);
    if(r->fault) return dnr_ticket(0,0,DNN_SYSTEM,r->fault);
    if(!dnr_free_slot(r) || r->pending+r->completed==r->operation_limit) return dnr_ticket(0,0,DNN_LIMIT,0);
    SOCKET fd=WSASocketW(address.family==4?AF_INET:AF_INET6,SOCK_STREAM,IPPROTO_TCP,NULL,0,WSA_FLAG_OVERLAPPED);
    if(fd==DNN_BAD) return dnr_ticket(0,0,DNN_SYSTEM,WSAGetLastError());
    if(dnn_nonblocking(fd)) { int error=WSAGetLastError();dnn_close(fd);return dnr_ticket(0,0,DNN_SYSTEM,error); }
    dnn_open socket=dnn_adopt(fd,DNN_TCP);
    if(socket.status) return dnr_ticket(0,0,socket.status,socket.system);
    dnr_request adopted=dlm_net_async_adopt(r,socket,DNN_TCP);if(adopted.status) return adopted;
    dnr_channel *s=dnr_slot(r,adopted.channel);dnr_operation *o=dnr_allocate(r,DNR_CONNECT,s->id,0,ms);
    if(!o) { dlm_net_reactor_channel_close(r,s->id);return dnr_ticket(0,0,DNN_LIMIT,0); }
    s->writer=(uint32_t)(o-r->operations)+1;s->connecting=true;o->address=target;o->address_length=length;
    dnx_step(r,o,true);if(o->state==1 && !ms) dnr_finish(r,o,DNN_TIMEOUT,0);
    return dnr_ticket(o->id,o->channel,DNN_OK,0);
#else
    return dlm_net_reactor_connect(r,address,ms);
#endif
}
dnn_open dlm_net_async_waker(void *handle,uint64_t operation,uint32_t action) {
    dnr_reactor *r=(dnr_reactor*)handle;dnr_operation *o=dnr_task(r,operation);
    if(!r || !r->control || action<1 || action>3 || (action==3?operation!=0:!o || o->state!=1 || (action==1 && o->kind!=DNR_EVENT))) return dnn_open_error(DNN_INVALID,0);
    dnx_waker *w=(dnx_waker*)malloc(sizeof *w);if(!w) return dnn_open_error(DNN_LIMIT,0);
    dnx_control *c=r->control;uint32_t references=atomic_load(&c->references);
    do { if(references==UINT32_MAX) { free(w);return dnn_open_error(DNN_LIMIT,0); } }
    while(!atomic_compare_exchange_weak(&c->references,&references,references+1));
    w->control=c;w->operation=operation;w->action=action;atomic_fetch_add(&dnn_live,1);
    dnn_open result={w,DNN_OK,0};return result;
}
void dlm_net_async_waker_close(void *handle) {
    dnx_waker *w=(dnx_waker*)handle;if(!w) return;
    dnx_control *c=w->control;free(w);dnx_unref(c);atomic_fetch_sub(&dnn_live,1);
}
dnn_io dlm_net_async_wake(void *handle) {
    dnx_waker *w=(dnx_waker*)handle;if(!w) return dnn_result(0,DNN_INVALID,0);
    dnx_control *c=w->control;dnx_lock(c);
    if(!c->open) { dnx_unlock(c);return dnn_result(0,DNN_CANCELLED,0); }
    for(uint32_t i=0;i<c->count;i++) {
        dnx_command *command=&c->commands[(c->head+i)%c->capacity];
        if(command->action==w->action && command->operation==w->operation) { dnx_unlock(c);return dnn_result(0,DNN_OK,0); }
    }
    if(c->count==c->capacity) { dnx_unlock(c);return dnn_result(0,DNN_LIMIT,0); }
    uint32_t tail=(c->head+c->count)%c->capacity;
    c->commands[tail]=(dnx_command){w->operation,dlm_net_clock_ms(),w->action};
    int error=dnx_notify(c);
    if(!error) c->count++;
    dnx_unlock(c);return dnn_result(error?0:1,error?DNN_SYSTEM:DNN_OK,error);
}
dnn_io dlm_net_async_join(void *handle,uint32_t ms,void *cancel) {
    dnr_reactor *r=(dnr_reactor*)handle;if(!r || !r->control) return dnn_result(0,DNN_INVALID,0);
    uint64_t end=dnn_deadline(ms);
    for(;;) {
        if(dnn_cancelled(cancel)) { dnr_finish_all(r,DNN_CANCELLED,0);return dnn_result(r->completed,DNN_CANCELLED,0); }
        if(r->fault) { dnr_finish_all(r,DNN_SYSTEM,r->fault);return dnn_result(r->completed,DNN_SYSTEM,r->fault); }
        dnr_expire(r);if(!r->pending) return dnn_result(r->completed,DNN_OK,0);
        uint64_t deadline=end;
        for(uint32_t i=0;i<r->operation_limit;i++) if(r->operations[i].state==1 && r->operations[i].deadline<deadline) deadline=r->operations[i].deadline;
        int wait=dnn_remaining(deadline);if(cancel && wait>50) wait=50;
        int result=dnr_wait(r,wait);
        if(result<0 && !DNN_INTR(-result)) { r->fault=-result;continue; }
        if(dnn_cancelled(cancel)) continue;
        dnr_expire(r);uint32_t start=r->cursor;
        for(uint32_t offset=0;offset<r->channel_limit;offset++) {
            uint32_t index=(start+offset)%r->channel_limit;dnr_channel *s=&r->slots[index];uint32_t mask=r->ready[index];
            uint32_t reader=s->reader,writer=s->writer;
            if((mask&1) && reader) dnr_step(r,&r->operations[reader-1],false);
            if((mask&2) && writer) dnr_step(r,&r->operations[writer-1],false);
            if(mask) r->cursor=(index+1)%r->channel_limit;
        }
        if(!r->pending) return dnn_result(r->completed,DNN_OK,0);
        if(!dnn_remaining(end)) return dnn_result(r->completed,DNN_TIMEOUT,0);
    }
}
dnx_stats dlm_net_async_stats(void *handle) {
    dnx_stats result;memset(&result,0,sizeof result);result.reactor=dlm_net_reactor_stats(handle);
    dnr_reactor *r=(dnr_reactor*)handle;if(!r || !r->control) { result.reactor.status=DNN_INVALID;return result; }
    dnx_control *c=r->control;dnx_lock(c);result.queued=c->count;result.queue_limit=c->capacity;dnx_unlock(c);
#if defined(_WIN32)
    result.reactor.backend=5;result.kernel_pending=r->kernel_pending;
#endif
    return result;
}

/* DLN crypto v1. Primitive operations are supplied by GnuTLS and Nettle.
 * This adapter owns secrets, directional record keys, limits and replay state;
 * it does not implement a cipher, hash, MAC or elliptic-curve arithmetic. */
enum { DNC_AUTH=12,DNC_REPLAY,DNC_EXPIRED,DNC_EXHAUSTED };
typedef struct { uint8_t *data; unsigned int size; } dnc_datum;
typedef struct {
    void *gnutls,*hogweed;
    int (*init)(void);void (*deinit)(void);const char *(*version)(const char*);
    int (*aead_new)(void**,int,const dnc_datum*);void (*aead_close)(void*);
    int (*encrypt)(void*,const void*,size_t,const void*,size_t,size_t,const void*,size_t,void*,size_t*);
    int (*decrypt)(void*,const void*,size_t,const void*,size_t,size_t,const void*,size_t,void*,size_t*);
    int (*extract)(int,const dnc_datum*,const dnc_datum*,void*);
    int (*expand)(int,const dnc_datum*,const dnc_datum*,void*,size_t);
    int (*hash)(int,const void*,size_t,void*);
    int (*hmac)(int,const void*,size_t,const void*,size_t,void*);
    void (*xpublic)(uint8_t*,const uint8_t*);
    void (*xshared)(uint8_t*,const uint8_t*,const uint8_t*);
} dnc_api;
static dnc_api dnc;
static atomic_int dnc_state=0;
static _Atomic size_t dnc_live=0;
typedef struct { uint32_t kind; } dnc_owner;
typedef struct { dnc_owner owner;size_t length;uint8_t bytes[]; } dnc_secret;
typedef struct {
    dnc_owner owner;
    void *sender,*receiver;
    uint8_t send_iv[12],receive_iv[12];
    uint64_t sent,received,send_bytes,receive_bytes,highest,window,record_limit,byte_limit,deadline;
    uint32_t failed_auth;
    bool seen,send_fault;
} dnc_channel;
typedef struct {
    uint64_t sent,received,send_bytes,receive_bytes,record_limit,byte_limit,deadline;
    uint32_t failed_auth;
    int32_t status,system;
} dnc_stats;
static void dnc_wipe(void *memory,size_t count) {
    volatile uint8_t *bytes=(volatile uint8_t*)memory;while(count--) *bytes++=0;
}
static void dnc_unload(void *module) {
    if(!module) return;
#if defined(_WIN32)
    FreeLibrary((HMODULE)module);
#else
    dlclose(module);
#endif
}
static void *dnc_library(const char *variable,const wchar_t *wide_variable,const char *const *names) {
#if defined(_WIN32)
    (void)variable;DWORD count=GetEnvironmentVariableW(wide_variable,NULL,0);
    if(count) {
        if(count>32768) return NULL;
        wchar_t *path=(wchar_t*)malloc((size_t)count*sizeof *path);if(!path) return NULL;
        DWORD got=GetEnvironmentVariableW(wide_variable,path,count);
        bool absolute=got && got<count && ((got>2 && path[1]==L':' && (path[2]==L'\\' || path[2]==L'/')) || (got>1 && path[0]==L'\\' && path[1]==L'\\'));
        HMODULE module=absolute?LoadLibraryExW(path,NULL,LOAD_LIBRARY_SEARCH_DEFAULT_DIRS|LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR):NULL;
        free(path);return (void*)module;
    }
    for(size_t i=0;names[i];i++) {
        wchar_t path[128];int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,names[i],-1,path,128);
        if(!length) continue;
        HMODULE module=LoadLibraryExW(path,NULL,LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(module) return (void*)module;
    }
    return NULL;
#else
    (void)wide_variable;const char *override=getenv(variable);
    if(override && *override) return override[0]=='/'?dlopen(override,RTLD_NOW|RTLD_LOCAL):NULL;
    for(size_t i=0;names[i];i++) { void *module=dlopen(names[i],RTLD_NOW|RTLD_LOCAL);if(module) return module; }
    return NULL;
#endif
}
static bool dnc_load(void) {
    int state=atomic_load_explicit(&dnc_state,memory_order_acquire);if(state==2) return true;if(state==-1) return false;
    int expected=0;
    if(atomic_compare_exchange_strong(&dnc_state,&expected,1)) {
#if defined(_WIN32)
        const char *gnutls[]={"libgnutls-30.dll",NULL},*hogweed[]={"libhogweed-6.dll",NULL};
#elif defined(__APPLE__)
        const char *gnutls[]={"libgnutls.30.dylib",NULL},*hogweed[]={"libhogweed.6.dylib",NULL};
#else
        const char *gnutls[]={"libgnutls.so.30",NULL},*hogweed[]={"libhogweed.so.6",NULL};
#endif
        dnc.gnutls=dnc_library("DLM_LIBGNUTLS",L"DLM_LIBGNUTLS",gnutls);
        dnc.hogweed=dnc_library("DLM_LIBHOGWEED",L"DLM_LIBHOGWEED",hogweed);
        bool ok=dnc.gnutls && dnc.hogweed;
#define DNC_LOAD(field,module,name) do { *(void**)(&dnc.field)=ok?dnn_symbol(module,name):NULL;if(!dnc.field) ok=false; } while(0)
        DNC_LOAD(init,dnc.gnutls,"gnutls_global_init");DNC_LOAD(deinit,dnc.gnutls,"gnutls_global_deinit");DNC_LOAD(version,dnc.gnutls,"gnutls_check_version");
        DNC_LOAD(aead_new,dnc.gnutls,"gnutls_aead_cipher_init");DNC_LOAD(aead_close,dnc.gnutls,"gnutls_aead_cipher_deinit");
        DNC_LOAD(encrypt,dnc.gnutls,"gnutls_aead_cipher_encrypt");DNC_LOAD(decrypt,dnc.gnutls,"gnutls_aead_cipher_decrypt");
        DNC_LOAD(extract,dnc.gnutls,"gnutls_hkdf_extract");DNC_LOAD(expand,dnc.gnutls,"gnutls_hkdf_expand");
        DNC_LOAD(hash,dnc.gnutls,"gnutls_hash_fast");DNC_LOAD(hmac,dnc.gnutls,"gnutls_hmac_fast");
        DNC_LOAD(xpublic,dnc.hogweed,"nettle_curve25519_mul_g");DNC_LOAD(xshared,dnc.hogweed,"nettle_curve25519_mul");
#undef DNC_LOAD
        if(ok) ok=dnc.version("3.6.13") && dnc.init()==0;
        if(!ok) { dnc_unload(dnc.hogweed);dnc_unload(dnc.gnutls);memset(&dnc,0,sizeof dnc); }
        atomic_store_explicit(&dnc_state,ok?2:-1,memory_order_release);return ok;
    }
    while((state=atomic_load_explicit(&dnc_state,memory_order_acquire))==1) {
#if defined(_WIN32)
        SwitchToThread();
#else
        struct timespec delay={0,1000000};nanosleep(&delay,NULL);
#endif
    }
    return state==2;
}
static bool dnc_kind(const void *handle,uint32_t kind) { return handle && ((const dnc_owner*)handle)->kind==kind; }
static dnc_secret *dnc_allocate(size_t length,uint32_t kind) {
    dnc_secret *s=(dnc_secret*)calloc(1,sizeof *s+length);if(!s) return NULL;
    s->owner.kind=kind;s->length=length;atomic_fetch_add(&dnc_live,1);atomic_fetch_add(&dnn_live,1);return s;
}
void dlm_net_crypto_close(void *handle) {
    if(!handle) return;
    if(dnc_kind(handle,3)) {
        dnc_channel *c=(dnc_channel*)handle;if(c->sender) dnc.aead_close(c->sender);if(c->receiver) dnc.aead_close(c->receiver);
        dnc_wipe(c,sizeof *c);
    } else {
        dnc_secret *s=(dnc_secret*)handle;size_t count=sizeof *s+s->length;dnc_wipe(s,count);
    }
    free(handle);atomic_fetch_sub(&dnc_live,1);atomic_fetch_sub(&dnn_live,1);
}
uint32_t dlm_net_crypto_capabilities(void) { return dnc_load()?15u:0u; }
dnn_io dlm_net_crypto_shutdown(void) {
    if(atomic_load(&dnn_live) || atomic_load(&dnc_live) || atomic_load(&dnc_state)==1) return dnn_result(0,DNN_INVALID,0);
    if(atomic_load(&dnc_state)==2) dnc.deinit();
    dnc_unload(dnc.hogweed);dnc_unload(dnc.gnutls);memset(&dnc,0,sizeof dnc);atomic_store(&dnc_state,0);
    return dnn_result(0,DNN_OK,0);
}
dnn_open dlm_net_crypto_secret(const uint8_t *bytes,size_t count,uint32_t kind,bool random) {
    if((kind!=1 && kind!=2) || (kind==2 && count!=32) || (random && count<32) || count>65536 || (!random && !dnn_buffer(bytes,count))) return dnn_open_error(DNN_INVALID,0);
    if(!dnc_load()) return dnn_open_error(DNN_UNSUPPORTED,0);
    dnc_secret *s=dnc_allocate(count,kind);if(!s) return dnn_open_error(DNN_LIMIT,0);
    if(random) {
        dnn_io result=dlm_net_random(s->bytes,count);
        if(result.status) { dlm_net_crypto_close(s);return dnn_open_error(result.status,result.system); }
    } else if(count) memcpy(s->bytes,bytes,count);
    dnn_open result={s,DNN_OK,0};return result;
}
dnn_io dlm_net_crypto_hash(void *key,const uint8_t *input,size_t count,uint8_t *output,size_t capacity) {
    if(!dnn_buffer(input,count) || !dnn_buffer(output,capacity) || capacity<32 || (key && !dnc_kind(key,1))) return dnn_result(0,DNN_INVALID,0);
    if(!dnc_load()) return dnn_result(0,DNN_UNSUPPORTED,0);
    uint8_t digest[32];dnc_secret *secret=(dnc_secret*)key;
    int error=key?dnc.hmac(6,secret->bytes,secret->length,input,count,digest):dnc.hash(6,input,count,digest);
    if(!error) memcpy(output,digest,32);dnc_wipe(digest,sizeof digest);
    return dnn_result(error?0:32,error?DNN_SYSTEM:DNN_OK,error);
}
dnn_io dlm_net_crypto_hmac(void *key,const uint8_t *input,size_t count,uint8_t *output,size_t capacity) {
    if(!dnc_kind(key,1)) return dnn_result(0,DNN_INVALID,0);
    return dlm_net_crypto_hash(key,input,count,output,capacity);
}
dnn_io dlm_net_crypto_public(void *key,uint8_t *output,size_t capacity) {
    if(!dnc_kind(key,2) || !dnn_buffer(output,capacity) || capacity<32) return dnn_result(0,DNN_INVALID,0);
    uint8_t public_key[32];dnc.xpublic(public_key,((dnc_secret*)key)->bytes);memcpy(output,public_key,32);
    return dnn_result(32,DNN_OK,0);
}
dnn_open dlm_net_crypto_agree(dnn_open opened,const uint8_t *peer,size_t count) {
    dnc_secret *s=(dnc_secret*)opened.handle;int status=opened.status,system=opened.system;
    if(!status && (!dnc_kind(s,2) || count!=32 || !peer)) status=DNN_INVALID;
    uint8_t shared[32]={0},point[32];
    if(!status) {
        memcpy(point,peer,32);point[31]&=127;dnc.xshared(shared,s->bytes,point);
        uint8_t any=0;for(size_t i=0;i<32;i++) any|=shared[i];if(!any) status=DNC_AUTH;
    }
    dlm_net_crypto_close(s);
    dnc_secret *result=status?NULL:dnc_allocate(32,1);if(!status && !result) status=DNN_LIMIT;
    if(result) memcpy(result->bytes,shared,32);dnc_wipe(shared,32);dnc_wipe(point,32);
    dnn_open value={result,status,system};return value;
}
static int dnc_hkdf_bytes(const dnc_secret *s,const uint8_t *salt,size_t salt_len,const uint8_t *info,size_t info_len,uint8_t *output,size_t length) {
    uint8_t prk[32];dnc_datum key={(uint8_t*)s->bytes,(unsigned int)s->length},salt_value={(uint8_t*)salt,(unsigned int)salt_len};
    int error=dnc.extract(6,&key,&salt_value,prk);
    if(!error) { dnc_datum expanded={prk,32},context={(uint8_t*)info,(unsigned int)info_len};error=dnc.expand(6,&expanded,&context,output,length); }
    dnc_wipe(prk,32);return error;
}
dnn_open dlm_net_crypto_hkdf(dnn_open opened,const uint8_t *salt,size_t salt_len,const uint8_t *info,size_t info_len,size_t count) {
    dnc_secret *s=(dnc_secret*)opened.handle;int status=opened.status,system=opened.system;
    if(!status && (!dnc_kind(s,1) || salt_len>65536 || info_len>65536 || count<32 || count>8160 || !dnn_buffer(salt,salt_len) || !dnn_buffer(info,info_len))) status=DNN_INVALID;
    dnc_secret *result=status?NULL:dnc_allocate(count,1);if(!status && !result) status=DNN_LIMIT;
    if(!status) { system=dnc_hkdf_bytes(s,salt,salt_len,info,info_len,result->bytes,count);if(system) status=DNN_SYSTEM; }
    dlm_net_crypto_close(s);if(status && result) { dlm_net_crypto_close(result);result=NULL; }
    dnn_open value={result,status,system};return value;
}
dnn_open dlm_net_crypto_channel(dnn_open opened,const uint8_t *salt,size_t salt_len,const uint8_t *context,size_t context_len,uint32_t role,uint64_t records,uint64_t bytes,uint32_t lifetime_ms) {
    dnc_secret *s=(dnc_secret*)opened.handle;int status=opened.status,system=opened.system;
    if(!status && (!dnc_kind(s,1) || s->length<32 || salt_len>65536 || context_len>65536 || !dnn_buffer(salt,salt_len) || !dnn_buffer(context,context_len) ||
                   (role!=1 && role!=2) || !records || records>1048576 || !bytes || bytes>1073741824 || !lifetime_ms)) status=DNN_INVALID;
    dnc_channel *c=status?NULL:(dnc_channel*)calloc(1,sizeof *c);
    uint8_t *info=status?NULL:(uint8_t*)malloc(context_len+32);uint8_t keys[2][44];memset(keys,0,sizeof keys);
    if(!status && (!c || !info)) status=DNN_LIMIT;
    for(unsigned int direction=0;!status && direction<2;direction++) {
        const char *label=direction?"DLM-CRYPTO-v1/s2c|":"DLM-CRYPTO-v1/c2s|";size_t prefix=strlen(label);
        memcpy(info,label,prefix);if(context_len) memcpy(info+prefix,context,context_len);
        system=dnc_hkdf_bytes(s,salt,salt_len,info,prefix+context_len,keys[direction],44);if(system) status=DNN_SYSTEM;
    }
    if(!status) {
        unsigned int send=role==1?0:1,receive=1-send;dnc_datum key={keys[send],32};void *candidate=NULL;
        system=dnc.aead_new(&candidate,11,&key);
        if(!system) {
            c->sender=candidate;candidate=NULL;key.data=keys[receive];system=dnc.aead_new(&candidate,11,&key);
            if(!system) c->receiver=candidate;
        }
        if(system) status=DNN_SYSTEM;
        else {
            c->owner.kind=3;memcpy(c->send_iv,keys[send]+32,12);memcpy(c->receive_iv,keys[receive]+32,12);
            c->record_limit=records;c->byte_limit=bytes;c->deadline=dnn_deadline(lifetime_ms);
            atomic_fetch_add(&dnc_live,1);atomic_fetch_add(&dnn_live,1);
        }
    }
    dlm_net_crypto_close(s);dnc_wipe(keys,sizeof keys);free(info);
    if(status && c) { if(c->sender) dnc.aead_close(c->sender);if(c->receiver) dnc.aead_close(c->receiver);dnc_wipe(c,sizeof *c);free(c);c=NULL; }
    dnn_open result={c,status,system};return result;
}
static void dnc_put64(uint8_t *bytes,uint64_t value) { for(unsigned int i=0;i<8;i++) bytes[7-i]=(uint8_t)(value>>(i*8)); }
static uint64_t dnc_get64(const uint8_t *bytes) { uint64_t value=0;for(unsigned int i=0;i<8;i++) value=(value<<8)|bytes[i];return value; }
static void dnc_nonce(uint8_t *nonce,const uint8_t *iv,uint64_t sequence) {
    memcpy(nonce,iv,12);for(unsigned int i=0;i<8;i++) nonce[11-i]^=(uint8_t)(sequence>>(i*8));
}
dnn_io dlm_net_crypto_seal(void *handle,const uint8_t *input,size_t count,const uint8_t *aad,size_t aad_len,uint8_t *output,size_t capacity) {
    if(!dnc_kind(handle,3) || !dnn_buffer(input,count) || !dnn_buffer(aad,aad_len) || aad_len>65536 || !dnn_buffer(output,capacity) || count>DNN_MAX-32) return dnn_result(0,DNN_INVALID,0);
    dnc_channel *c=(dnc_channel*)handle;
    if(dlm_net_clock_ms()>=c->deadline) return dnn_result(0,DNC_EXPIRED,0);
    if(c->send_fault || c->sent>=c->record_limit || count>c->byte_limit-c->send_bytes) return dnn_result(0,DNC_EXHAUSTED,0);
    if(capacity<count+32) return dnn_result(0,DNN_LIMIT,0);
    uint8_t *record=(uint8_t*)malloc(count+32),*auth=(uint8_t*)malloc(aad_len+16);uint8_t nonce[12];
    if(!record || !auth) { free(record);free(auth);return dnn_result(0,DNN_LIMIT,0); }
    memcpy(record,"DLC1",4);dnc_put64(record+4,c->sent);
    for(unsigned int i=0;i<4;i++) record[15-i]=(uint8_t)(count>>(8*i));
    memcpy(auth,record,16);if(aad_len) memcpy(auth+16,aad,aad_len);dnc_nonce(nonce,c->send_iv,c->sent);
    c->sent++;c->send_bytes+=count;size_t length=count+16;
    int error=dnc.encrypt(c->sender,nonce,12,auth,aad_len+16,16,input,count,record+16,&length);
    int status=error?DNN_SYSTEM:length!=count+16?DNN_PROTOCOL:DNN_OK;
    if(status) c->send_fault=true;else memcpy(output,record,count+32);
    dnc_wipe(record,count+32);free(record);free(auth);dnc_wipe(nonce,12);
    return dnn_result(status?0:count+32,status,error);
}
dnn_io dlm_net_crypto_open(void *handle,const uint8_t *record,size_t count,const uint8_t *aad,size_t aad_len,uint8_t *output,size_t capacity) {
    if(!dnc_kind(handle,3) || !dnn_buffer(record,count) || count<32 || !dnn_buffer(aad,aad_len) || aad_len>65536 || !dnn_buffer(output,capacity)) return dnn_result(0,DNN_INVALID,0);
    dnc_channel *c=(dnc_channel*)handle;
    if(dlm_net_clock_ms()>=c->deadline) return dnn_result(0,DNC_EXPIRED,0);
    if(c->received>=c->record_limit || c->failed_auth>=64) return dnn_result(0,DNC_EXHAUSTED,0);
    size_t length=0;for(unsigned int i=12;i<16;i++) length=(length<<8)|record[i];
    if(memcmp(record,"DLC1",4) || length!=count-32) return dnn_result(0,DNN_PROTOCOL,0);
    uint64_t sequence=dnc_get64(record+4);
    if(sequence>=c->record_limit) return dnn_result(0,DNC_EXHAUSTED,0);
    if(c->seen && sequence<=c->highest && (c->highest-sequence>=64 || (c->window&(UINT64_C(1)<<(c->highest-sequence))))) return dnn_result(0,DNC_REPLAY,0);
    if(length>c->byte_limit-c->receive_bytes) return dnn_result(0,DNC_EXHAUSTED,0);
    if(capacity<length) return dnn_result(0,DNN_LIMIT,0);
    uint8_t *plain=(uint8_t*)malloc(length?length:1),*auth=(uint8_t*)malloc(aad_len+16),nonce[12];
    if(!plain || !auth) { free(plain);free(auth);return dnn_result(0,DNN_LIMIT,0); }
    memcpy(auth,record,16);if(aad_len) memcpy(auth+16,aad,aad_len);dnc_nonce(nonce,c->receive_iv,sequence);size_t produced=length;
    int error=dnc.decrypt(c->receiver,nonce,12,auth,aad_len+16,16,record+16,count-16,plain,&produced);
    int status=error==-24?DNC_AUTH:error?DNN_SYSTEM:produced!=length?DNN_PROTOCOL:DNN_OK;
    if(status==DNC_AUTH) c->failed_auth++;
    if(!status) {
        if(!c->seen) { c->highest=sequence;c->window=1;c->seen=true; }
        else if(sequence>c->highest) { uint64_t shift=sequence-c->highest;c->window=shift>=64?1:(c->window<<shift)|1;c->highest=sequence; }
        else c->window|=UINT64_C(1)<<(c->highest-sequence);
        c->received++;c->receive_bytes+=length;if(length) memcpy(output,plain,length);
    }
    dnc_wipe(plain,length?length:1);free(plain);free(auth);dnc_wipe(nonce,12);
    return dnn_result(status?0:length,status,error);
}
dnc_stats dlm_net_crypto_stats(void *handle) {
    dnc_stats result;memset(&result,0,sizeof result);if(!dnc_kind(handle,3)) { result.status=DNN_INVALID;return result; }
    dnc_channel *c=(dnc_channel*)handle;result.sent=c->sent;result.received=c->received;result.send_bytes=c->send_bytes;result.receive_bytes=c->receive_bytes;
    result.record_limit=c->record_limit;result.byte_limit=c->byte_limit;result.deadline=c->deadline;result.failed_auth=c->failed_auth;
    result.status=dlm_net_clock_ms()>=c->deadline?DNC_EXPIRED:c->send_fault?DNN_SYSTEM:DNN_OK;return result;
}
/* BEGIN DLM QUIC ABI 1 */
/* DLM QUIC provider ABI 1. Retain the endpoint during every call. */
#ifndef DLM_QUIC_API_H
#define DLM_QUIC_API_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef struct { const uint8_t *data; size_t len; } dq_text;
typedef struct { size_t count; int32_t status, system; } dq_io;
typedef struct { void *handle; int32_t status, system; } dq_open;
typedef struct { uint32_t family, port, scope; uint8_t bytes[16]; } dq_addr;
typedef struct { dq_addr address; int32_t status, system; } dq_resolved;
typedef struct { uint32_t connections, streams, events, idle_ms, handshake_ms, congestion; size_t memory, window; } dq_config;
typedef struct { uint64_t connection; int64_t stream; int32_t status, system; } dq_ref;
typedef struct { uint64_t id, connection; int64_t stream; uint64_t offset, code; size_t count; uint32_t kind, flags; int32_t status, system; } dq_event;
typedef struct { size_t used, limit; uint64_t sent, received, lost, cwnd, in_flight, rtt_ns; uint32_t connections, streams, events, state, version; int32_t status, system; } dq_stats;
typedef struct {
    uint32_t abi, size, ngtcp2_version;
    dq_open (*open)(dq_addr,bool,dq_text,dq_text,dq_text,dq_config);
    void (*close)(void*);
    dq_resolved (*local)(void*);
    dq_ref (*connect)(void*,dq_addr,dq_text);
    dq_io (*poll)(void*,uint32_t,const void*);
    dq_event (*next)(void*);
    dq_io (*copy)(void*,uint64_t,size_t,uint8_t*,size_t);
    dq_io (*ack)(void*,uint64_t);
    dq_ref (*stream)(void*,uint64_t,bool);
    dq_io (*write)(void*,uint64_t,int64_t,const uint8_t*,size_t,bool);
    dq_io (*datagram)(void*,uint64_t,const uint8_t*,size_t);
    dq_io (*reset)(void*,uint64_t,int64_t,uint64_t);
    dq_io (*disconnect)(void*,uint64_t,uint64_t);
    dq_io (*forget)(void*,uint64_t);
    dq_io (*migrate)(void*,uint64_t,dq_addr);
    dq_stats (*stats)(void*,uint64_t);
} dq_api;
#endif
/* END DLM QUIC ABI 1 */
/* DLN general QUIC loader: one pinned ABI; no fallback after an explicit path. */
static const dq_api *dnq;
static void *dnq_module;
static atomic_int dnq_state=0;
static bool dnq_load(void) {
    int state=atomic_load_explicit(&dnq_state,memory_order_acquire);if(state==2) return true;if(state==-1) return false;
    int expected=0;if(atomic_compare_exchange_strong(&dnq_state,&expected,1)) {
#if defined(_WIN32)
        const char *names[]={"dlm-quic-1.dll",NULL};
#elif defined(__APPLE__)
        const char *names[]={"libdlm-quic-1.dylib",NULL};
#else
        const char *names[]={"libdlm-quic-1.so",NULL};
#endif
        dnq_module=dnc_library("DLM_LIBQUIC",L"DLM_LIBQUIC",names);const dq_api *(*get)(uint32_t)=NULL;
        if(dnq_module) {
#if defined(_WIN32)
            FARPROC symbol=GetProcAddress((HMODULE)dnq_module,"dlm_quic_get_api");
            _Static_assert(sizeof get==sizeof symbol,"QUIC provider function pointer size");
            memcpy(&get,&symbol,sizeof get);
#else
            *(void**)(&get)=dlsym(dnq_module,"dlm_quic_get_api");
#endif
        }
        dnq=get?get(1):NULL;bool ok=dnq && dnq->abi==1 && dnq->size==sizeof(dq_api) && dnq->ngtcp2_version==0x011a00;
        if(!ok) { dnq=NULL;dnc_unload(dnq_module);dnq_module=NULL; }atomic_store_explicit(&dnq_state,ok?2:-1,memory_order_release);return ok;
    }
    while((state=atomic_load_explicit(&dnq_state,memory_order_acquire))==1) {
#if defined(_WIN32)
        SwitchToThread();
#else
        struct timespec delay={0,1000000};nanosleep(&delay,NULL);
#endif
    }return state==2;
}
uint32_t dlm_net_quic_version(void) { return dnq_load()?dnq->ngtcp2_version:0; }
dnn_io dlm_net_quic_shutdown(void) {
    if(atomic_load(&dnn_live) || atomic_load(&dnq_state)==1) return dnn_result(0,DNN_INVALID,0);
    dnc_unload(dnq_module);dnq_module=NULL;dnq=NULL;atomic_store(&dnq_state,0);return dnn_result(0,DNN_OK,0);
}
dq_open dlm_net_quic_open(dq_addr address,bool server,dq_text alpn,dq_text certificate,dq_text key,dq_config config) {
    if(!dnq_load()) { dq_open r={NULL,DNN_UNSUPPORTED,0};return r; }dq_open r=dnq->open(address,server,alpn,certificate,key,config);if(r.handle) atomic_fetch_add(&dnn_live,1);return r;
}
void dlm_net_quic_close(void *handle) { if(handle && dnq) { dnq->close(handle);atomic_fetch_sub(&dnn_live,1); } }
dq_resolved dlm_net_quic_local(void *handle) { if(!dnq) { dq_resolved r={0};r.status=DNN_INVALID;return r; }return dnq->local(handle); }
dq_ref dlm_net_quic_connect(void *handle,dq_addr address,dq_text name) { if(!dnq) { dq_ref r={0,-1,DNN_INVALID,0};return r; }return dnq->connect(handle,address,name); }
dq_io dlm_net_quic_poll(void *handle,uint32_t timeout,const void *cancel) { if(!dnq) { dq_io r={0,DNN_INVALID,0};return r; }return dnq->poll(handle,timeout,cancel); }
dq_event dlm_net_quic_next(void *handle) { if(!dnq) { dq_event r={0};r.status=DNN_INVALID;return r; }return dnq->next(handle); }
dq_io dlm_net_quic_copy(void *handle,uint64_t event,size_t offset,uint8_t *out,size_t capacity) { if(!dnq) { dq_io r={0,DNN_INVALID,0};return r; }return dnq->copy(handle,event,offset,out,capacity); }
dq_io dlm_net_quic_ack(void *handle,uint64_t event) { if(!dnq) { dq_io r={0,DNN_INVALID,0};return r; }return dnq->ack(handle,event); }
dq_ref dlm_net_quic_stream(void *handle,uint64_t connection,bool uni) { if(!dnq) { dq_ref r={0,-1,DNN_INVALID,0};return r; }return dnq->stream(handle,connection,uni); }
dq_io dlm_net_quic_write(void *handle,uint64_t connection,int64_t stream,const uint8_t *data,size_t count,bool fin) { if(!dnq) { dq_io r={0,DNN_INVALID,0};return r; }return dnq->write(handle,connection,stream,data,count,fin); }
dq_io dlm_net_quic_datagram(void *handle,uint64_t connection,const uint8_t *data,size_t count) { if(!dnq) { dq_io r={0,DNN_INVALID,0};return r; }return dnq->datagram(handle,connection,data,count); }
dq_io dlm_net_quic_reset(void *handle,uint64_t connection,int64_t stream,uint64_t code) { if(!dnq) { dq_io r={0,DNN_INVALID,0};return r; }return dnq->reset(handle,connection,stream,code); }
dq_io dlm_net_quic_disconnect(void *handle,uint64_t connection,uint64_t code) { if(!dnq) { dq_io r={0,DNN_INVALID,0};return r; }return dnq->disconnect(handle,connection,code); }
dq_io dlm_net_quic_forget(void *handle,uint64_t connection) { if(!dnq) { dq_io r={0,DNN_INVALID,0};return r; }return dnq->forget(handle,connection); }
dq_io dlm_net_quic_migrate(void *handle,uint64_t connection,dq_addr address) { if(!dnq) { dq_io r={0,DNN_INVALID,0};return r; }return dnq->migrate(handle,connection,address); }
dq_stats dlm_net_quic_stats(void *handle,uint64_t connection) { if(!dnq) { dq_stats r={0};r.status=DNN_INVALID;return r; }return dnq->stats(handle,connection); }

/* DLN-07 message services. Protocol choice is explicit and validated against
 * immutable capabilities before any new I/O. Existing socket and QUIC APIs are
 * unchanged. The quota covers this adapter and the QUIC controlled allocator;
 * TLS/OS/dependency internals and stacks are explicitly outside that scope. */
#define DNT_MAGIC UINT64_C(0x444c4d5452414e31)
#define DNT_HEADER 16u
#define DNT_MAX_CHANNELS 256u
typedef struct { uint32_t required,forbidden,preferred; } dnt_requirements;
typedef struct { uint32_t max_message,queued_messages,channels;size_t memory; } dnt_bounds;
typedef struct { dnn_addr local,peer;dnn_text identity,ca,pin,alpn; } dnt_target;
typedef struct { uint64_t id;uint32_t channel;size_t count;int32_t status,system; } dnt_message;
typedef struct { uint64_t id;uint32_t channel;size_t copied,required;int32_t status,system; } dnt_delivery;
typedef struct { size_t used,limit,adapter_limit,backend_limit;uint32_t profile,properties,state,send_queued,receive_queued,channels;int32_t status,system; } dnt_stats;
typedef struct dnt_node { struct dnt_node *next;uint64_t id;uint32_t channel;size_t size,position;int64_t stream;bool stream_set,outgoing;uint8_t bytes[]; } dnt_node;
typedef struct { uint32_t id;int64_t output,input;bool output_set,input_set; } dnt_channel;
typedef struct { bool active,header_ready,complete;int64_t stream;uint64_t offset;uint32_t channel;size_t header_count,body_count;uint8_t header[DNT_HEADER];dnt_node *body; } dnt_input;
typedef struct {
    uint64_t magic,connection,next_message;uint32_t profile,state,status;int system;bool initiator;
    dnt_bounds bounds;dnt_requirements requirements;size_t used,adapter_limit,backend_limit,tx_used,rx_used,tx_limit,rx_limit;
    void *backend;dnn_addr peer;dnt_node *tx,*tx_tail,*rx,*rx_tail;uint32_t tx_count,rx_count,channel_count,next_channel;
    uint64_t q_event;size_t q_offset;dnt_channel *channels;dnt_input *inputs;
} dnt_session;
_Static_assert(sizeof(dnt_session)<=1024,"transport fixed storage bound");
_Static_assert(sizeof(dnt_channel)+sizeof(dnt_input)<=256,"transport channel storage bound");
_Static_assert(sizeof(dnt_node)<=128,"transport message overhead bound");
static uint32_t dnt_properties(uint32_t p) { const uint32_t values[]={0,179,183,208,191,253,244};return p<=6?values[p]:0; }
static int dnt_check(uint32_t p,dnt_requirements r,dnt_bounds b) {
    uint32_t caps=dnt_properties(p);
    if(!caps || ((r.required|r.forbidden|r.preferred)&~511u) || (r.required&r.forbidden) || (r.preferred&r.forbidden) || (r.required&66u)==66u) return DNN_INVALID;
    if(b.max_message>16777216 || !b.queued_messages || b.queued_messages>4096 || !b.channels || b.channels>DNT_MAX_CHANNELS || b.memory<1024 || b.memory>DNN_MAX) return DNN_INVALID;
    size_t reserve=1024+(size_t)b.channels*256+2*((size_t)b.max_message+128);
    if(b.memory<reserve) return DNN_INVALID;
    if((r.required&caps)!=r.required || (r.forbidden&caps)) return DNN_UNSUPPORTED;
    if((p==3 && b.max_message>65491) || (p==6 && b.max_message>1084)) return DNN_LIMIT;
    if(p>=4 && (b.memory<2097152 || (size_t)b.max_message+DNT_HEADER>b.memory/8 || b.memory/2<reserve)) return DNN_LIMIT;
    return DNN_OK;
}
static dnt_session *dnt_get(void *p) { dnt_session *s=p;return s && s->magic==DNT_MAGIC?s:NULL; }
static uint32_t dnt_read32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static void dnt_write32(uint8_t *p,uint32_t n) { p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n; }
static void dnt_header(uint8_t *p,uint32_t channel,uint32_t size) { memcpy(p,"DLT1",4);dnt_write32(p+4,size);dnt_write32(p+8,channel);memset(p+12,0,4); }
static dnt_node *dnt_alloc(dnt_session *s,size_t payload,bool outgoing) {
    if(payload>SIZE_MAX-sizeof(dnt_node)-DNT_HEADER) return NULL;size_t n=sizeof(dnt_node)+DNT_HEADER+payload;
    size_t available=outgoing?s->tx_limit-s->tx_used:s->rx_limit-s->rx_used;
    if(n>available) return NULL;dnt_node *p=calloc(1,n);if(p) { p->size=DNT_HEADER+payload;p->outgoing=outgoing;s->used+=n;if(outgoing) s->tx_used+=n;else s->rx_used+=n; }return p;
}
static void dnt_free(dnt_session *s,dnt_node *p) { if(p) { size_t n=sizeof *p+p->size;s->used-=n;if(p->outgoing) s->tx_used-=n;else s->rx_used-=n;free(p); } }
static void dnt_fault(dnt_session *s,int status,int system) { if(s->state!=2) { s->state=2;s->status=(uint32_t)status;s->system=system; } }
static bool dnt_address_equal(dnn_addr a,dnn_addr b) { return a.family==b.family && a.port==b.port && a.scope==b.scope && !memcmp(a.bytes,b.bytes,a.family==4?4:16); }
static dnt_channel *dnt_channel_find(dnt_session *s,uint32_t id) { for(uint32_t i=0;i<s->channel_count;i++) if(s->channels[i].id==id) return &s->channels[i];return NULL; }
static dnt_channel *dnt_channel_receive(dnt_session *s,uint32_t id) {
    dnt_channel *c=dnt_channel_find(s,id);if(c) return c;
    if(s->profile<4 || s->profile==6 || !id || (id&1u)==(s->initiator?1u:0u) || s->channel_count==s->bounds.channels) return NULL;
    c=&s->channels[s->channel_count++];c->id=id;return c;
}
static int dnt_publish(dnt_session *s,dnt_input *input) {
    if(s->rx_count==s->bounds.queued_messages) return DNN_WOULD_BLOCK;if(s->next_message==UINT64_MAX) return DNN_LIMIT;
    dnt_node *p=input->body;p->id=++s->next_message;p->channel=input->channel;p->position=0;
    if(s->rx_tail) s->rx_tail->next=p;else s->rx=p;s->rx_tail=p;s->rx_count++;
    input->body=NULL;input->header_count=input->body_count=0;input->header_ready=input->complete=false;return DNN_OK;
}
static int dnt_parse_header(dnt_session *s,dnt_input *input) {
    uint8_t *h=input->header;if(memcmp(h,"DLT1",4) || dnt_read32(h+12)) return DNN_PROTOCOL;
    uint32_t n=dnt_read32(h+4),id=dnt_read32(h+8);if(n>s->bounds.max_message) return DNN_LIMIT;
    dnt_channel *c=dnt_channel_receive(s,id);if(!c) return DNN_PROTOCOL;
    if(s->profile==4) { if(c->input_set && c->input!=input->stream) return DNN_PROTOCOL;c->input=input->stream;c->input_set=true; }
    input->channel=id;input->header_ready=true;return DNN_OK;
}
/* One input belongs to one native stream. No bytes after an unacknowledged
 * complete message are consumed until queue storage is available. */
static int dnt_feed(dnt_session *s,dnt_input *in,const uint8_t *bytes,size_t count,size_t *consumed) {
    *consumed=0;
    for(;;) {
        if(in->complete) { if(s->profile==5) return count?DNN_PROTOCOL:DNN_OK;int r=dnt_publish(s,in);if(r) return r; }
        if(s->rx_count==s->bounds.queued_messages) return DNN_WOULD_BLOCK;
        if(in->header_count<DNT_HEADER) { size_t n=DNT_HEADER-in->header_count;if(n>count) n=count;if(n) memcpy(in->header+in->header_count,bytes,n);in->header_count+=n;bytes+=n;count-=n;*consumed+=n;if(in->header_count<DNT_HEADER) return DNN_OK; }
        if(!in->header_ready) { int r=dnt_parse_header(s,in);if(r) return r; }
        if(!in->body) { in->body=dnt_alloc(s,dnt_read32(in->header+4),false);if(!in->body) return s->rx_count?DNN_WOULD_BLOCK:DNN_LIMIT;memcpy(in->body->bytes,in->header,DNT_HEADER); }
        size_t remaining=in->body->size-DNT_HEADER-in->body_count,n=remaining;if(n>count) n=count;
        if(n) memcpy(in->body->bytes+DNT_HEADER+in->body_count,bytes,n);in->body_count+=n;bytes+=n;count-=n;*consumed+=n;
        if(in->body_count==in->body->size-DNT_HEADER) { in->complete=true;continue; }return DNN_OK;
    }
}
static dnt_input *dnt_input_for(dnt_session *s,int64_t stream) {
    for(uint32_t i=0;i<s->bounds.channels;i++) if(s->inputs[i].active && s->inputs[i].stream==stream) return &s->inputs[i];
    for(uint32_t i=0;i<s->bounds.channels;i++) if(!s->inputs[i].active) { dnt_input *p=&s->inputs[i];memset(p,0,sizeof *p);p->active=true;p->stream=stream;return p; }return NULL;
}
static void dnt_backend_close(void *p,uint32_t profile) { if(profile>=4) dlm_net_quic_close(p);else dlm_net_close(p); }
void dlm_net_transport_close(void *handle) {
    dnt_session *s=dnt_get(handle);if(!s) return;s->magic=0;dnt_backend_close(s->backend,s->profile);
    while(s->tx) { dnt_node *n=s->tx->next;dnt_free(s,s->tx);s->tx=n; }while(s->rx) { dnt_node *n=s->rx->next;dnt_free(s,s->rx);s->rx=n; }
    for(uint32_t i=0;i<s->bounds.channels;i++) dnt_free(s,s->inputs[i].body);free(s);atomic_fetch_sub(&dnn_live,1);
}
static dnn_open dnt_adopt(dnn_open opened,uint32_t profile,dnt_requirements req,dnt_bounds bounds,dnn_addr peer,uint64_t connection,bool initiator,bool connecting) {
    int error=dnt_check(profile,req,bounds);if(opened.status) error=opened.status;if(!opened.handle && !error) error=DNN_INVALID;
    size_t backend_limit=0;
    if(!error && profile>=4) { if(!dnq) error=DNN_UNSUPPORTED;else { dq_stats stats=dnq->stats(opened.handle,connection);backend_limit=stats.limit;
        if(stats.connections!=1 || stats.status || (!connecting && stats.state!=2)) error=stats.status?stats.status:DNN_INVALID;
        if(backend_limit>bounds.memory/2) error=DNN_LIMIT;
    }}else if(!error) { dnn_socket *b=opened.handle;int expected=profile==1?DNN_TCP:profile==2?DNN_SECURE:DNN_UDP;if(b->kind!=expected) error=DNN_INVALID;
        if(profile==3 && (!peer.port || (peer.family!=4 && peer.family!=6))) error=DNN_INVALID;
    }
    if(error) { dnt_backend_close(opened.handle,profile);return dnn_open_error(error,opened.system); }
    size_t n=sizeof(dnt_session)+(size_t)bounds.channels*(sizeof(dnt_channel)+sizeof(dnt_input));dnt_session *s=calloc(1,n);
    if(!s) { dnt_backend_close(opened.handle,profile);return dnn_open_error(DNN_LIMIT,0); }
    s->magic=DNT_MAGIC;s->backend=opened.handle;s->profile=profile;s->bounds=bounds;s->requirements=req;s->used=n;s->adapter_limit=bounds.memory-backend_limit;s->backend_limit=backend_limit;s->peer=peer;s->connection=connection;s->initiator=initiator;s->state=connecting?0:1;s->next_channel=initiator?1:2;
    s->tx_limit=(s->adapter_limit-n)/2;s->rx_limit=s->adapter_limit-n-s->tx_limit;
    s->channels=(dnt_channel*)(s+1);s->inputs=(dnt_input*)(s->channels+bounds.channels);s->channels[0].id=0;s->channel_count=1;
    atomic_fetch_add(&dnn_live,1);dnn_open result={s,DNN_OK,0};return result;
}
dnn_open dlm_net_transport_adopt(dnn_open opened,uint32_t profile,dnt_requirements req,dnt_bounds bounds,dnn_addr peer,uint64_t connection,bool initiator) { return dnt_adopt(opened,profile,req,bounds,peer,connection,initiator,false); }
uint32_t dlm_net_transport_available(void) { uint32_t bits=1|4;if(dlm_net_capabilities()&8) bits|=2;if(dlm_net_quic_version()) bits|=8|16|32;return bits; }
static dq_config dnt_quic_config(dnt_bounds b) { dq_config c={1,b.channels*4,b.queued_messages*2+16,30000,10000,2,b.memory/2,(size_t)b.max_message+DNT_HEADER};if(c.window<1024) c.window=1024;return c; }
static dnn_open dnt_tls_open(dnt_target t,uint32_t ms,void *cancel) {
    char identity[256],peer[96],url[320],route[512];if(!t.identity.len || t.identity.len>=sizeof identity || !t.identity.data || memchr(t.identity.data,0,t.identity.len)) return dnn_open_error(DNN_INVALID,0);
    memcpy(identity,t.identity.data,t.identity.len);identity[t.identity.len]=0;uint8_t numeric[16];bool v6=inet_pton(AF_INET6,identity,numeric)==1;
    if(!v6) for(size_t i=0;i<t.identity.len;i++) { unsigned char c=(unsigned char)identity[i];if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='.' || c=='-')) return dnn_open_error(DNN_INVALID,0); }
    dnn_io formatted=dlm_net_address_text(t.peer,(uint8_t*)peer,sizeof peer-1,false);if(formatted.status) return dnn_open_error(formatted.status,formatted.system);peer[formatted.count]=0;
    int n=snprintf(url,sizeof url,v6?"https://[%s]:%u":"https://%s:%u",identity,(unsigned)t.peer.port);if(n<0 || (size_t)n>=sizeof url) return dnn_open_error(DNN_INVALID,0);
    /* Build the source host separately so an IPv6 identity closes its bracket. */
    char source[260];n=snprintf(source,sizeof source,v6?"[%s]":"%s",identity);if(n<0 || (size_t)n>=sizeof source) return dnn_open_error(DNN_INVALID,0);
    n=snprintf(route,sizeof route,t.peer.family==6?"%s:%u:[%s]:%u":"%s:%u:%s:%u",source,(unsigned)t.peer.port,peer,(unsigned)t.peer.port);if(n<0 || (size_t)n>=sizeof route) return dnn_open_error(DNN_INVALID,0);
    dnn_text u={(const uint8_t*)url,strlen(url)};dnn_open result=dnn_curl_new(u,ms,DNN_SECURE,t.ca,t.pin);if(result.status) return result;
    dnn_socket *s=result.handle;s->headers=dnn_curl.list_append(NULL,route);int e=s->headers?0:27;
#define DNT_SET(option,value) do { if(!e) e=dnn_curl.setopt(s->curl,option,value); } while(0)
    DNT_SET(10243,s->headers);DNT_SET(141,1L);DNT_SET(84,2L);DNT_SET(226,0L);DNT_SET(20219,dnn_progress);DNT_SET(10057,cancel);DNT_SET(43,0L);
    if(!e) e=dnn_curl.perform(s->curl);if(!e) e=dnn_curl.getinfo(s->curl,0x500000+44,&s->fd);
    if(!e) { DNT_SET(43,1L);DNT_SET(10057,(void*)NULL); }
#undef DNT_SET
    if(e) { dlm_net_close(s);return dnn_open_error(dnn_curl_status(e),e); }if(dnn_cancelled(cancel)) { dlm_net_close(s);return dnn_open_error(DNN_CANCELLED,0); }return result;
}
/* Select a concrete local route without sending an application datagram.
 * The QUIC provider intentionally requires a specific source address. */
static dnn_resolved dnt_route_local(dnn_addr peer) {
    dnn_resolved r={0};struct sockaddr_storage address;dnn_socklen length=dnn_to_sockaddr(&peer,&address);
    if(!length) { r.status=DNN_INVALID;return r; }int e=dnn_os_init();if(e) { r.status=DNN_SYSTEM;r.system=e;return r; }
    dnn_fd fd=socket(peer.family==4?AF_INET:AF_INET6,SOCK_DGRAM,0);if(fd==DNN_BAD) { r.status=DNN_SYSTEM;r.system=dnn_socket_error();return r; }
    if(connect(fd,(struct sockaddr*)&address,length) || getsockname(fd,(struct sockaddr*)&address,&length)) { r.status=DNN_SYSTEM;r.system=dnn_socket_error(); }
    else { dnn_from_sockaddr(&r.address,(struct sockaddr*)&address);r.address.port=0; }dnn_close(fd);return r;
}
dnn_open dlm_net_transport_open(uint32_t profile,dnt_requirements req,dnt_bounds bounds,dnt_target target,uint32_t ms,void *cancel) {
    int error=dnt_check(profile,req,bounds);if(error) return dnn_open_error(error,0);if(!ms || ms>INT_MAX || !target.peer.port || (target.peer.family!=4 && target.peer.family!=6)) return dnn_open_error(DNN_INVALID,0);
    if(profile!=2 && target.pin.len) return dnn_open_error(DNN_UNSUPPORTED,0);
    if(dnn_cancelled(cancel)) return dnn_open_error(DNN_CANCELLED,0);dnn_open backend={NULL,DNN_INVALID,0};uint64_t connection=0;
    if(profile<3 && target.local.family) return dnn_open_error(DNN_UNSUPPORTED,0);
    if(profile==3 && !target.local.family) target.local.family=target.peer.family;
    if(profile>=4 && !target.local.family) { dnn_resolved route=dnt_route_local(target.peer);if(route.status) return dnn_open_error(route.status,route.system);target.local=route.address; }
    if(profile==1) backend=dlm_net_connect_address(target.peer,ms);
    else if(profile==2) backend=dnt_tls_open(target,ms,cancel);
    else if(profile==3) backend=dlm_net_bind_address(target.local,3,0,false);
    else {
        dq_config config=dnt_quic_config(bounds);config.handshake_ms=ms;config.idle_ms=ms>30000?ms:30000;
        dq_addr local,peer;dq_text alpn,ca,identity;memcpy(&local,&target.local,sizeof local);memcpy(&peer,&target.peer,sizeof peer);memcpy(&alpn,&target.alpn,sizeof alpn);memcpy(&ca,&target.ca,sizeof ca);memcpy(&identity,&target.identity,sizeof identity);
        dq_open q=dlm_net_quic_open(local,false,alpn,ca,(dq_text){NULL,0},config);
        backend=(dnn_open){q.handle,q.status,q.system};if(!q.status) { dq_ref r=dlm_net_quic_connect(q.handle,peer,identity);if(r.status) { dlm_net_quic_close(q.handle);return dnn_open_error(r.status,r.system); }connection=r.connection; }
    }
    if(dnn_cancelled(cancel)) { dnt_backend_close(backend.handle,profile);return dnn_open_error(DNN_CANCELLED,0); }
    return dnt_adopt(backend,profile,req,bounds,target.peer,connection,true,profile>=4);
}
dnn_io dlm_net_transport_channel(void *handle) {
    dnt_session *s=dnt_get(handle);if(!s) return dnn_result(0,DNN_INVALID,0);if(s->state==2) return dnn_result(0,s->status,s->system);if(s->state==3) return dnn_result(0,DNN_EOF,0);
    if(s->profile<4 || s->profile==6) return dnn_result(0,DNN_UNSUPPORTED,0);if(s->channel_count==s->bounds.channels || s->next_channel>UINT32_MAX-2) return dnn_result(0,DNN_LIMIT,0);
    uint32_t id=s->next_channel;s->next_channel+=2;s->channels[s->channel_count++].id=id;return dnn_result(id,DNN_OK,0);
}
dnn_io dlm_net_transport_send(void *handle,uint32_t channel,const uint8_t *bytes,size_t count) {
    dnt_session *s=dnt_get(handle);if(!s || !dnn_buffer(bytes,count)) return dnn_result(0,DNN_INVALID,0);if(s->state==2) return dnn_result(0,s->status,s->system);if(s->state!=1) return dnn_result(0,s->state==0?DNN_WOULD_BLOCK:DNN_EOF,0);
    if(!dnt_channel_find(s,channel)) return dnn_result(0,DNN_INVALID,0);if(count>s->bounds.max_message) return dnn_result(0,DNN_LIMIT,0);if(s->tx_count==s->bounds.queued_messages) return dnn_result(0,DNN_WOULD_BLOCK,0);
    dnt_node *p=dnt_alloc(s,count,true);if(!p) return dnn_result(0,DNN_WOULD_BLOCK,0);p->channel=channel;dnt_header(p->bytes,channel,(uint32_t)count);if(count) memcpy(p->bytes+DNT_HEADER,bytes,count);
    if(s->tx_tail) s->tx_tail->next=p;else s->tx=p;s->tx_tail=p;s->tx_count++;return dnn_result(count,DNN_OK,0);
}
static int dnt_flush(dnt_session *s,size_t *progress) {
    bool blocked[DNT_MAX_CHANNELS]={0};dnt_node **link=&s->tx;unsigned work=0;
    while(*link && work++<64) {
        dnt_node *p=*link;dnt_channel *channel=dnt_channel_find(s,p->channel);size_t index=(size_t)(channel-s->channels);dnn_io sent={0,DNN_WOULD_BLOCK,0};
        if(s->profile<3) sent=dlm_net_send(s->backend,p->bytes+p->position,p->size-p->position);
        else if(s->profile==3) sent=dlm_net_udp_send(s->backend,s->peer,p->bytes,p->size);
        else if(s->profile==6) { dq_io r=dnq->datagram(s->backend,s->connection,p->bytes,p->size);sent=(dnn_io){r.count,r.status,r.system}; }
        else {
            if(s->profile==4 && blocked[index]) { link=&p->next;continue; }
            if(!p->stream_set) {
                if(s->profile==4 && channel->output_set) { p->stream=channel->output;p->stream_set=true; }
                else { dq_ref r=dnq->stream(s->backend,s->connection,true);if(r.status==DNN_WOULD_BLOCK || r.status==DNN_LIMIT) { blocked[index]=true;link=&p->next;continue; }if(r.status) return r.status;
                    p->stream=r.stream;p->stream_set=true;if(s->profile==4) { channel->output=r.stream;channel->output_set=true; }
                }
            }
            dq_io r=dnq->write(s->backend,s->connection,p->stream,p->bytes,p->size,s->profile==5);sent=(dnn_io){r.count,r.status,r.system};if(sent.status==DNN_LIMIT) sent.status=DNN_WOULD_BLOCK;
        }
        if(sent.status==DNN_WOULD_BLOCK) { if(s->profile<4 || s->profile==6) break;blocked[index]=true;link=&p->next;continue; }
        if(sent.status) { s->system=sent.system;return sent.status; }if(!sent.count) return DNN_PROTOCOL;
        p->position+=sent.count;(*progress)++;if(p->position<p->size) { if(s->profile>=3) return DNN_PROTOCOL;continue; }
        *link=p->next;if(s->tx_tail==p) s->tx_tail=NULL;s->tx_count--;dnt_free(s,p);
    }
    if(s->tx && !s->tx_tail) { s->tx_tail=s->tx;while(s->tx_tail->next) s->tx_tail=s->tx_tail->next; }return DNN_OK;
}
static int dnt_read_stream(dnt_session *s,size_t *progress) {
    dnt_input *in=&s->inputs[0];in->active=true;
    for(unsigned work=0;work<64;work++) {
        size_t consumed=0;int r=dnt_feed(s,in,(const uint8_t*)"",0,&consumed);if(r) return r;
        if(s->rx_count==s->bounds.queued_messages) return DNN_WOULD_BLOCK;
        uint8_t *out;size_t count;
        if(in->header_count<DNT_HEADER) { out=in->header+in->header_count;count=DNT_HEADER-in->header_count; }
        else if(!in->body) return DNN_WOULD_BLOCK;
        else { out=in->body->bytes+DNT_HEADER+in->body_count;count=in->body->size-DNT_HEADER-in->body_count; }
        if(!count) continue;dnn_io received=dlm_net_recv(s->backend,out,count);
        if(received.status==DNN_EOF) { if(in->header_count || in->body) return DNN_TRUNCATED;s->state=3;return DNN_OK; }
        if(received.status) { s->system=received.system;return received.status; }if(!received.count) return DNN_PROTOCOL;(*progress)++;
        if(in->header_count<DNT_HEADER) in->header_count+=received.count;
        else { in->body_count+=received.count;if(in->body_count==in->body->size-DNT_HEADER) in->complete=true; }
    }return DNN_OK;
}
static int dnt_datagram(dnt_session *s,const uint8_t *data,size_t count) {
    if(count<DNT_HEADER || memcmp(data,"DLT1",4) || dnt_read32(data+8) || dnt_read32(data+12) || dnt_read32(data+4)!=count-DNT_HEADER) return DNN_PROTOCOL;
    if(count-DNT_HEADER>s->bounds.max_message) return DNN_LIMIT;if(s->rx_count==s->bounds.queued_messages) return DNN_WOULD_BLOCK;
    dnt_input temporary={0};temporary.body=dnt_alloc(s,count-DNT_HEADER,false);if(!temporary.body) return s->rx_count?DNN_WOULD_BLOCK:DNN_LIMIT;memcpy(temporary.body->bytes,data,count);
    int r=dnt_publish(s,&temporary);if(r) dnt_free(s,temporary.body);return r;
}
static int dnt_read_udp(dnt_session *s,size_t *progress) {
    for(unsigned work=0;work<64 && s->rx_count<s->bounds.queued_messages;work++) {
        uint8_t bytes[65536];dnn_datagram r=dlm_net_udp_recv(s->backend,bytes,sizeof bytes);
        if(r.io.status) { s->system=r.io.system;return r.io.status; }(*progress)++;if(!dnt_address_equal(r.peer,s->peer)) continue;
        int e=dnt_datagram(s,bytes,r.io.count);if(e) return e;
    }return DNN_OK;
}
static int dnt_read_quic(dnt_session *s,size_t *progress) {
    if(s->profile==4) for(uint32_t i=0;i<s->bounds.channels;i++) if(s->inputs[i].active && s->inputs[i].complete) { int r=dnt_publish(s,&s->inputs[i]);if(r) return r; }
    for(unsigned work=0;work<128;work++) {
        dq_event event=dnq->next(s->backend);if(event.status) return event.status;
        if(event.connection!=s->connection) return DNN_PROTOCOL;
        if(s->q_event && s->q_event!=event.id) return DNN_PROTOCOL;s->q_event=event.id;
        if(event.kind==1) s->state=1;
        else if(event.kind==4) return DNN_PROTOCOL;
        else if(event.kind==6) { dq_stats stats=dnq->stats(s->backend,s->connection);s->system=stats.system;return stats.status?stats.status:DNN_EOF; }
        else if(event.kind==5) {
            if(s->profile!=6) return DNN_PROTOCOL;if(event.count>1100) return DNN_PROTOCOL;
            uint8_t data[1100];dq_io copy=dnq->copy(s->backend,event.id,0,data,sizeof data);if(copy.status || copy.count!=event.count) return DNN_PROTOCOL;
            int r=dnt_datagram(s,data,event.count);if(r) return r;
        }else if(event.kind==3) {
            if((s->profile!=4 && s->profile!=5) || !(event.stream&2)) return DNN_PROTOCOL;
            dnt_input *in=dnt_input_for(s,event.stream);if(!in) return DNN_LIMIT;
            if(event.offset+s->q_offset!=in->offset) return DNN_PROTOCOL;
            while(s->q_offset<event.count) {
                uint8_t data[4096];size_t n=event.count-s->q_offset;if(n>sizeof data) n=sizeof data;
                dq_io copy=dnq->copy(s->backend,event.id,s->q_offset,data,n);if(copy.status || copy.count!=n) return DNN_PROTOCOL;
                size_t consumed=0;int r=dnt_feed(s,in,data,n,&consumed);s->q_offset+=consumed;in->offset+=consumed;if(r) return r;
                if(!consumed) return DNN_WOULD_BLOCK;
            }
            size_t consumed=0;int fed=dnt_feed(s,in,(const uint8_t*)"",0,&consumed);if(fed) return fed;
            if(event.flags&1) {
                if(s->profile==5) { if(!in->complete) return DNN_TRUNCATED;int r=dnt_publish(s,in);if(r) return r; }
                else if(in->header_count || in->body) return DNN_TRUNCATED;
                memset(in,0,sizeof *in);
            }
        }
        dq_io ack=dnq->ack(s->backend,event.id);if(ack.status) return ack.status;s->q_event=0;s->q_offset=0;(*progress)++;
    }return DNN_OK;
}
dnn_io dlm_net_transport_poll(void *handle,uint32_t ms,void *cancel) {
    dnt_session *s=dnt_get(handle);if(!s) return dnn_result(0,DNN_INVALID,0);if(s->state==2) return dnn_result(0,s->status,s->system);
    uint64_t deadline=dnn_deadline(ms);size_t progress=0;
    for(;;) {
        if(dnn_cancelled(cancel)) return dnn_result(progress,DNN_CANCELLED,0);
        if(s->profile>=4 && s->rx_count<s->bounds.queued_messages) { dq_io r=dnq->poll(s->backend,0,cancel);if(r.status!=DNN_OK && r.status!=DNN_WOULD_BLOCK) { dnt_fault(s,r.status,r.system);return dnn_result(progress,r.status,r.system); }
            dq_stats stats=dnq->stats(s->backend,s->connection);if(stats.state==2) s->state=1;else if(stats.state>=3 && stats.status) dnt_fault(s,stats.status,stats.system);
        }
        int error=s->state==1?dnt_flush(s,&progress):DNN_OK;
        if(!error && s->state!=3) error=s->profile<3?dnt_read_stream(s,&progress):s->profile==3?dnt_read_udp(s,&progress):dnt_read_quic(s,&progress);
        if(error && error!=DNN_WOULD_BLOCK && error!=DNN_EOF) { dnt_fault(s,error,s->system);return dnn_result(progress,error,s->system); }
        if(error==DNN_EOF) s->state=3;
        if(s->state==2) return dnn_result(progress,s->status,s->system);
        if(s->rx || progress) return dnn_result(progress,DNN_OK,0);if(s->state==3) return dnn_result(0,DNN_EOF,0);
        int remaining=dnn_remaining(deadline);if(!remaining) return dnn_result(0,ms?DNN_TIMEOUT:DNN_WOULD_BLOCK,0);
        if(remaining>10) remaining=10;
#if defined(_WIN32)
        Sleep((DWORD)remaining);
#else
        struct timespec delay={0,(long)remaining*1000000};nanosleep(&delay,NULL);
#endif
    }
}
dnt_message dlm_net_transport_peek(void *handle) {
    dnt_message r={0};dnt_session *s=dnt_get(handle);if(!s) r.status=DNN_INVALID;else if(s->rx) { r.id=s->rx->id;r.channel=s->rx->channel;r.count=s->rx->size-DNT_HEADER; }
    else { r.status=s->state==2?(int)s->status:s->state==3?DNN_EOF:DNN_WOULD_BLOCK;r.system=s->system; }return r;
}
dnn_io dlm_net_transport_copy(void *handle,uint64_t id,size_t offset,uint8_t *out,size_t capacity) {
    dnt_session *s=dnt_get(handle);if(!s || !s->rx || s->rx->id!=id || !dnn_buffer(out,capacity) || offset>s->rx->size-DNT_HEADER) return dnn_result(0,DNN_INVALID,0);
    size_t n=s->rx->size-DNT_HEADER-offset;if(n>capacity) n=capacity;if(n) memcpy(out,s->rx->bytes+DNT_HEADER+offset,n);return dnn_result(n,DNN_OK,0);
}
dnn_io dlm_net_transport_ack(void *handle,uint64_t id) {
    dnt_session *s=dnt_get(handle);if(!s || !s->rx || s->rx->id!=id) return dnn_result(0,DNN_INVALID,0);dnt_node *p=s->rx;s->rx=p->next;if(!s->rx) s->rx_tail=NULL;s->rx_count--;dnt_free(s,p);return dnn_result(1,DNN_OK,0);
}
dnt_delivery dlm_net_transport_receive(void *handle,uint8_t *out,size_t capacity) {
    dnt_message p=dlm_net_transport_peek(handle);dnt_delivery r={p.id,p.channel,0,p.count,p.status,p.system};if(p.status) return r;
    if(!dnn_buffer(out,capacity)) { r.status=DNN_INVALID;return r; }if(capacity<p.count) { r.status=DNN_TRUNCATED;return r; }
    dnn_io copied=dlm_net_transport_copy(handle,p.id,0,out,capacity);r.status=copied.status;r.system=copied.system;r.copied=copied.count;if(!copied.status) (void)dlm_net_transport_ack(handle,p.id);return r;
}
dnt_stats dlm_net_transport_stats(void *handle) {
    dnt_stats r={0};dnt_session *s=dnt_get(handle);if(!s) { r.status=DNN_INVALID;return r; }
    r.used=s->used;r.limit=s->bounds.memory;r.adapter_limit=s->adapter_limit;r.backend_limit=s->backend_limit;r.profile=s->profile;r.properties=dnt_properties(s->profile);r.state=s->state;r.send_queued=s->tx_count;r.receive_queued=s->rx_count;r.channels=s->channel_count;r.status=(int)s->status;r.system=s->system;
    if(s->profile>=4) { dq_stats q=dnq->stats(s->backend,s->connection);r.used+=q.used; }return r;
}
dnn_resolved dlm_net_transport_local(void *handle) {
    dnt_session *s=dnt_get(handle);dnn_resolved r;memset(&r,0,sizeof r);if(!s) r.status=DNN_INVALID;
    else if(s->profile>=4) { dq_resolved q=dnq->local(s->backend);memcpy(&r,&q,sizeof r); }else r=dlm_net_local(s->backend);return r;
}

#endif
static uint8_t dn_U8_bits(uint64_t x){ uint8_t r; uint8_t b=(uint8_t)x;memcpy(&r,&b,sizeof r);return r;}
static uint8_t dn_U8_add(uint8_t a,uint8_t b){unsigned __int128 v=(unsigned __int128)a+(unsigned __int128)b;if(v<0 || v>(((unsigned __int128)1<<8)-1))dn_fail("integer overflow");return (uint8_t)v;}
static uint8_t dn_U8_wrap_add(uint8_t a,uint8_t b){uint64_t x=(uint64_t)a+(uint64_t)b;return dn_U8_bits(x);}
static uint8_t dn_U8_sat_add(uint8_t a,uint8_t b){unsigned __int128 v=(unsigned __int128)a+(unsigned __int128)b;if(v<0)return (uint8_t)0;if(v>(((unsigned __int128)1<<8)-1))return (uint8_t)(((unsigned __int128)1<<8)-1);return (uint8_t)v;}
static uint8_t dn_U8_sub(uint8_t a,uint8_t b){unsigned __int128 v=(unsigned __int128)a-(unsigned __int128)b;if(a<b)dn_fail("integer overflow");return (uint8_t)v;}
static uint8_t dn_U8_wrap_sub(uint8_t a,uint8_t b){uint64_t x=(uint64_t)a-(uint64_t)b;return dn_U8_bits(x);}
static uint8_t dn_U8_sat_sub(uint8_t a,uint8_t b){if(a<b)return 0;unsigned __int128 v=(unsigned __int128)a-(unsigned __int128)b;if(v<0)return (uint8_t)0;if(v>(((unsigned __int128)1<<8)-1))return (uint8_t)(((unsigned __int128)1<<8)-1);return (uint8_t)v;}
static uint8_t dn_U8_mul(uint8_t a,uint8_t b){unsigned __int128 v=(unsigned __int128)a*(unsigned __int128)b;if(v<0 || v>(((unsigned __int128)1<<8)-1))dn_fail("integer overflow");return (uint8_t)v;}
static uint8_t dn_U8_wrap_mul(uint8_t a,uint8_t b){uint64_t x=(uint64_t)a*(uint64_t)b;return dn_U8_bits(x);}
static uint8_t dn_U8_sat_mul(uint8_t a,uint8_t b){unsigned __int128 v=(unsigned __int128)a*(unsigned __int128)b;if(v<0)return (uint8_t)0;if(v>(((unsigned __int128)1<<8)-1))return (uint8_t)(((unsigned __int128)1<<8)-1);return (uint8_t)v;}
static uint8_t dn_U8_div(uint8_t a,uint8_t b){if(!b)dn_fail("division by zero");return a/b;}
static uint8_t dn_U8_mod(uint8_t a,uint8_t b){if(!b)dn_fail("division by zero");return a%b;}
static uint8_t dn_U8_shl(uint8_t a,uint8_t b){if(b>=8)dn_fail("shift count out of range");return dn_U8_bits((uint64_t)a<<b);}
static uint8_t dn_U8_shr(uint8_t a,uint8_t b){if(b>=8)dn_fail("shift count out of range");return (uint8_t)(a>>b);}
static uint16_t dn_U16_bits(uint64_t x){ uint16_t r; uint16_t b=(uint16_t)x;memcpy(&r,&b,sizeof r);return r;}
static uint16_t dn_U16_add(uint16_t a,uint16_t b){unsigned __int128 v=(unsigned __int128)a+(unsigned __int128)b;if(v<0 || v>(((unsigned __int128)1<<16)-1))dn_fail("integer overflow");return (uint16_t)v;}
static uint16_t dn_U16_wrap_add(uint16_t a,uint16_t b){uint64_t x=(uint64_t)a+(uint64_t)b;return dn_U16_bits(x);}
static uint16_t dn_U16_sat_add(uint16_t a,uint16_t b){unsigned __int128 v=(unsigned __int128)a+(unsigned __int128)b;if(v<0)return (uint16_t)0;if(v>(((unsigned __int128)1<<16)-1))return (uint16_t)(((unsigned __int128)1<<16)-1);return (uint16_t)v;}
static uint16_t dn_U16_sub(uint16_t a,uint16_t b){unsigned __int128 v=(unsigned __int128)a-(unsigned __int128)b;if(a<b)dn_fail("integer overflow");return (uint16_t)v;}
static uint16_t dn_U16_wrap_sub(uint16_t a,uint16_t b){uint64_t x=(uint64_t)a-(uint64_t)b;return dn_U16_bits(x);}
static uint16_t dn_U16_sat_sub(uint16_t a,uint16_t b){if(a<b)return 0;unsigned __int128 v=(unsigned __int128)a-(unsigned __int128)b;if(v<0)return (uint16_t)0;if(v>(((unsigned __int128)1<<16)-1))return (uint16_t)(((unsigned __int128)1<<16)-1);return (uint16_t)v;}
static uint16_t dn_U16_mul(uint16_t a,uint16_t b){unsigned __int128 v=(unsigned __int128)a*(unsigned __int128)b;if(v<0 || v>(((unsigned __int128)1<<16)-1))dn_fail("integer overflow");return (uint16_t)v;}
static uint16_t dn_U16_wrap_mul(uint16_t a,uint16_t b){uint64_t x=(uint64_t)a*(uint64_t)b;return dn_U16_bits(x);}
static uint16_t dn_U16_sat_mul(uint16_t a,uint16_t b){unsigned __int128 v=(unsigned __int128)a*(unsigned __int128)b;if(v<0)return (uint16_t)0;if(v>(((unsigned __int128)1<<16)-1))return (uint16_t)(((unsigned __int128)1<<16)-1);return (uint16_t)v;}
static uint16_t dn_U16_div(uint16_t a,uint16_t b){if(!b)dn_fail("division by zero");return a/b;}
static uint16_t dn_U16_mod(uint16_t a,uint16_t b){if(!b)dn_fail("division by zero");return a%b;}
static uint16_t dn_U16_shl(uint16_t a,uint16_t b){if(b>=16)dn_fail("shift count out of range");return dn_U16_bits((uint64_t)a<<b);}
static uint16_t dn_U16_shr(uint16_t a,uint16_t b){if(b>=16)dn_fail("shift count out of range");return (uint16_t)(a>>b);}
static uint32_t dn_U32_bits(uint64_t x){ uint32_t r; uint32_t b=(uint32_t)x;memcpy(&r,&b,sizeof r);return r;}
static uint32_t dn_U32_add(uint32_t a,uint32_t b){unsigned __int128 v=(unsigned __int128)a+(unsigned __int128)b;if(v<0 || v>(((unsigned __int128)1<<32)-1))dn_fail("integer overflow");return (uint32_t)v;}
static uint32_t dn_U32_wrap_add(uint32_t a,uint32_t b){uint64_t x=(uint64_t)a+(uint64_t)b;return dn_U32_bits(x);}
static uint32_t dn_U32_sat_add(uint32_t a,uint32_t b){unsigned __int128 v=(unsigned __int128)a+(unsigned __int128)b;if(v<0)return (uint32_t)0;if(v>(((unsigned __int128)1<<32)-1))return (uint32_t)(((unsigned __int128)1<<32)-1);return (uint32_t)v;}
static uint32_t dn_U32_sub(uint32_t a,uint32_t b){unsigned __int128 v=(unsigned __int128)a-(unsigned __int128)b;if(a<b)dn_fail("integer overflow");return (uint32_t)v;}
static uint32_t dn_U32_wrap_sub(uint32_t a,uint32_t b){uint64_t x=(uint64_t)a-(uint64_t)b;return dn_U32_bits(x);}
static uint32_t dn_U32_sat_sub(uint32_t a,uint32_t b){if(a<b)return 0;unsigned __int128 v=(unsigned __int128)a-(unsigned __int128)b;if(v<0)return (uint32_t)0;if(v>(((unsigned __int128)1<<32)-1))return (uint32_t)(((unsigned __int128)1<<32)-1);return (uint32_t)v;}
static uint32_t dn_U32_mul(uint32_t a,uint32_t b){unsigned __int128 v=(unsigned __int128)a*(unsigned __int128)b;if(v<0 || v>(((unsigned __int128)1<<32)-1))dn_fail("integer overflow");return (uint32_t)v;}
static uint32_t dn_U32_wrap_mul(uint32_t a,uint32_t b){uint64_t x=(uint64_t)a*(uint64_t)b;return dn_U32_bits(x);}
static uint32_t dn_U32_sat_mul(uint32_t a,uint32_t b){unsigned __int128 v=(unsigned __int128)a*(unsigned __int128)b;if(v<0)return (uint32_t)0;if(v>(((unsigned __int128)1<<32)-1))return (uint32_t)(((unsigned __int128)1<<32)-1);return (uint32_t)v;}
static uint32_t dn_U32_div(uint32_t a,uint32_t b){if(!b)dn_fail("division by zero");return a/b;}
static uint32_t dn_U32_mod(uint32_t a,uint32_t b){if(!b)dn_fail("division by zero");return a%b;}
static uint32_t dn_U32_shl(uint32_t a,uint32_t b){if(b>=32)dn_fail("shift count out of range");return dn_U32_bits((uint64_t)a<<b);}
static uint32_t dn_U32_shr(uint32_t a,uint32_t b){if(b>=32)dn_fail("shift count out of range");return (uint32_t)(a>>b);}
static uint64_t dn_U64_bits(uint64_t x){ uint64_t r; uint64_t b=(uint64_t)x;memcpy(&r,&b,sizeof r);return r;}
static uint64_t dn_U64_add(uint64_t a,uint64_t b){unsigned __int128 v=(unsigned __int128)a+(unsigned __int128)b;if(v<0 || v>(((unsigned __int128)1<<64)-1))dn_fail("integer overflow");return (uint64_t)v;}
static uint64_t dn_U64_wrap_add(uint64_t a,uint64_t b){uint64_t x=(uint64_t)a+(uint64_t)b;return dn_U64_bits(x);}
static uint64_t dn_U64_sat_add(uint64_t a,uint64_t b){unsigned __int128 v=(unsigned __int128)a+(unsigned __int128)b;if(v<0)return (uint64_t)0;if(v>(((unsigned __int128)1<<64)-1))return (uint64_t)(((unsigned __int128)1<<64)-1);return (uint64_t)v;}
static uint64_t dn_U64_sub(uint64_t a,uint64_t b){unsigned __int128 v=(unsigned __int128)a-(unsigned __int128)b;if(a<b)dn_fail("integer overflow");return (uint64_t)v;}
static uint64_t dn_U64_wrap_sub(uint64_t a,uint64_t b){uint64_t x=(uint64_t)a-(uint64_t)b;return dn_U64_bits(x);}
static uint64_t dn_U64_sat_sub(uint64_t a,uint64_t b){if(a<b)return 0;unsigned __int128 v=(unsigned __int128)a-(unsigned __int128)b;if(v<0)return (uint64_t)0;if(v>(((unsigned __int128)1<<64)-1))return (uint64_t)(((unsigned __int128)1<<64)-1);return (uint64_t)v;}
static uint64_t dn_U64_mul(uint64_t a,uint64_t b){unsigned __int128 v=(unsigned __int128)a*(unsigned __int128)b;if(v<0 || v>(((unsigned __int128)1<<64)-1))dn_fail("integer overflow");return (uint64_t)v;}
static uint64_t dn_U64_wrap_mul(uint64_t a,uint64_t b){uint64_t x=(uint64_t)a*(uint64_t)b;return dn_U64_bits(x);}
static uint64_t dn_U64_sat_mul(uint64_t a,uint64_t b){unsigned __int128 v=(unsigned __int128)a*(unsigned __int128)b;if(v<0)return (uint64_t)0;if(v>(((unsigned __int128)1<<64)-1))return (uint64_t)(((unsigned __int128)1<<64)-1);return (uint64_t)v;}
static uint64_t dn_U64_div(uint64_t a,uint64_t b){if(!b)dn_fail("division by zero");return a/b;}
static uint64_t dn_U64_mod(uint64_t a,uint64_t b){if(!b)dn_fail("division by zero");return a%b;}
static uint64_t dn_U64_shl(uint64_t a,uint64_t b){if(b>=64)dn_fail("shift count out of range");return dn_U64_bits((uint64_t)a<<b);}
static uint64_t dn_U64_shr(uint64_t a,uint64_t b){if(b>=64)dn_fail("shift count out of range");return (uint64_t)(a>>b);}
static size_t dn_USize_bits(uint64_t x){ size_t r; uint64_t b=(uint64_t)x;memcpy(&r,&b,sizeof r);return r;}
static size_t dn_USize_add(size_t a,size_t b){unsigned __int128 v=(unsigned __int128)a+(unsigned __int128)b;if(v<0 || v>(((unsigned __int128)1<<64)-1))dn_fail("integer overflow");return (size_t)v;}
static size_t dn_USize_wrap_add(size_t a,size_t b){uint64_t x=(uint64_t)a+(uint64_t)b;return dn_USize_bits(x);}
static size_t dn_USize_sat_add(size_t a,size_t b){unsigned __int128 v=(unsigned __int128)a+(unsigned __int128)b;if(v<0)return (size_t)0;if(v>(((unsigned __int128)1<<64)-1))return (size_t)(((unsigned __int128)1<<64)-1);return (size_t)v;}
static size_t dn_USize_sub(size_t a,size_t b){unsigned __int128 v=(unsigned __int128)a-(unsigned __int128)b;if(a<b)dn_fail("integer overflow");return (size_t)v;}
static size_t dn_USize_wrap_sub(size_t a,size_t b){uint64_t x=(uint64_t)a-(uint64_t)b;return dn_USize_bits(x);}
static size_t dn_USize_sat_sub(size_t a,size_t b){if(a<b)return 0;unsigned __int128 v=(unsigned __int128)a-(unsigned __int128)b;if(v<0)return (size_t)0;if(v>(((unsigned __int128)1<<64)-1))return (size_t)(((unsigned __int128)1<<64)-1);return (size_t)v;}
static size_t dn_USize_mul(size_t a,size_t b){unsigned __int128 v=(unsigned __int128)a*(unsigned __int128)b;if(v<0 || v>(((unsigned __int128)1<<64)-1))dn_fail("integer overflow");return (size_t)v;}
static size_t dn_USize_wrap_mul(size_t a,size_t b){uint64_t x=(uint64_t)a*(uint64_t)b;return dn_USize_bits(x);}
static size_t dn_USize_sat_mul(size_t a,size_t b){unsigned __int128 v=(unsigned __int128)a*(unsigned __int128)b;if(v<0)return (size_t)0;if(v>(((unsigned __int128)1<<64)-1))return (size_t)(((unsigned __int128)1<<64)-1);return (size_t)v;}
static size_t dn_USize_div(size_t a,size_t b){if(!b)dn_fail("division by zero");return a/b;}
static size_t dn_USize_mod(size_t a,size_t b){if(!b)dn_fail("division by zero");return a%b;}
static size_t dn_USize_shl(size_t a,size_t b){if(b>=64)dn_fail("shift count out of range");return dn_USize_bits((uint64_t)a<<b);}
static size_t dn_USize_shr(size_t a,size_t b){if(b>=64)dn_fail("shift count out of range");return (size_t)(a>>b);}
static int8_t dn_I8_bits(uint64_t x){ int8_t r; uint8_t b=(uint8_t)x;memcpy(&r,&b,sizeof r);return r;}
static int8_t dn_I8_add(int8_t a,int8_t b){__int128 v=(__int128)a+(__int128)b;if(v<(-((__int128)1<<7) ) || v>(((__int128)1<<7)-1))dn_fail("integer overflow");return (int8_t)v;}
static int8_t dn_I8_wrap_add(int8_t a,int8_t b){uint64_t x=(uint64_t)a+(uint64_t)b;return dn_I8_bits(x);}
static int8_t dn_I8_sat_add(int8_t a,int8_t b){__int128 v=(__int128)a+(__int128)b;if(v<(-((__int128)1<<7) ))return (int8_t)(-((__int128)1<<7) );if(v>(((__int128)1<<7)-1))return (int8_t)(((__int128)1<<7)-1);return (int8_t)v;}
static int8_t dn_I8_sub(int8_t a,int8_t b){__int128 v=(__int128)a-(__int128)b;if(v<(-((__int128)1<<7) ) || v>(((__int128)1<<7)-1))dn_fail("integer overflow");return (int8_t)v;}
static int8_t dn_I8_wrap_sub(int8_t a,int8_t b){uint64_t x=(uint64_t)a-(uint64_t)b;return dn_I8_bits(x);}
static int8_t dn_I8_sat_sub(int8_t a,int8_t b){__int128 v=(__int128)a-(__int128)b;if(v<(-((__int128)1<<7) ))return (int8_t)(-((__int128)1<<7) );if(v>(((__int128)1<<7)-1))return (int8_t)(((__int128)1<<7)-1);return (int8_t)v;}
static int8_t dn_I8_mul(int8_t a,int8_t b){__int128 v=(__int128)a*(__int128)b;if(v<(-((__int128)1<<7) ) || v>(((__int128)1<<7)-1))dn_fail("integer overflow");return (int8_t)v;}
static int8_t dn_I8_wrap_mul(int8_t a,int8_t b){uint64_t x=(uint64_t)a*(uint64_t)b;return dn_I8_bits(x);}
static int8_t dn_I8_sat_mul(int8_t a,int8_t b){__int128 v=(__int128)a*(__int128)b;if(v<(-((__int128)1<<7) ))return (int8_t)(-((__int128)1<<7) );if(v>(((__int128)1<<7)-1))return (int8_t)(((__int128)1<<7)-1);return (int8_t)v;}
static int8_t dn_I8_div(int8_t a,int8_t b){if(!b)dn_fail("division by zero");if(a==(int8_t)(-((__int128)1<<7) ) && b==-1)dn_fail("integer overflow");return a/b;}
static int8_t dn_I8_mod(int8_t a,int8_t b){if(!b)dn_fail("division by zero");if(a==(int8_t)(-((__int128)1<<7) ) && b==-1)dn_fail("integer overflow");return a%b;}
static int8_t dn_I8_shl(int8_t a,int8_t b){if(b<0 || b>=8)dn_fail("shift count out of range");return dn_I8_bits((uint64_t)a<<b);}
static int8_t dn_I8_shr(int8_t a,int8_t b){if(b<0 || b>=8)dn_fail("shift count out of range");uint64_t u=(uint64_t)a; if(a<0 && b>0)u=(u>>(unsigned)b)|(UINT64_MAX<<(8-(unsigned)b));else u>>=(unsigned)b;return dn_I8_bits(u);}
static int8_t dn_I8_neg(int8_t a){if(a==(int8_t)(-((__int128)1<<7) ))dn_fail("integer overflow");return -a;}
static int16_t dn_I16_bits(uint64_t x){ int16_t r; uint16_t b=(uint16_t)x;memcpy(&r,&b,sizeof r);return r;}
static int16_t dn_I16_add(int16_t a,int16_t b){__int128 v=(__int128)a+(__int128)b;if(v<(-((__int128)1<<15) ) || v>(((__int128)1<<15)-1))dn_fail("integer overflow");return (int16_t)v;}
static int16_t dn_I16_wrap_add(int16_t a,int16_t b){uint64_t x=(uint64_t)a+(uint64_t)b;return dn_I16_bits(x);}
static int16_t dn_I16_sat_add(int16_t a,int16_t b){__int128 v=(__int128)a+(__int128)b;if(v<(-((__int128)1<<15) ))return (int16_t)(-((__int128)1<<15) );if(v>(((__int128)1<<15)-1))return (int16_t)(((__int128)1<<15)-1);return (int16_t)v;}
static int16_t dn_I16_sub(int16_t a,int16_t b){__int128 v=(__int128)a-(__int128)b;if(v<(-((__int128)1<<15) ) || v>(((__int128)1<<15)-1))dn_fail("integer overflow");return (int16_t)v;}
static int16_t dn_I16_wrap_sub(int16_t a,int16_t b){uint64_t x=(uint64_t)a-(uint64_t)b;return dn_I16_bits(x);}
static int16_t dn_I16_sat_sub(int16_t a,int16_t b){__int128 v=(__int128)a-(__int128)b;if(v<(-((__int128)1<<15) ))return (int16_t)(-((__int128)1<<15) );if(v>(((__int128)1<<15)-1))return (int16_t)(((__int128)1<<15)-1);return (int16_t)v;}
static int16_t dn_I16_mul(int16_t a,int16_t b){__int128 v=(__int128)a*(__int128)b;if(v<(-((__int128)1<<15) ) || v>(((__int128)1<<15)-1))dn_fail("integer overflow");return (int16_t)v;}
static int16_t dn_I16_wrap_mul(int16_t a,int16_t b){uint64_t x=(uint64_t)a*(uint64_t)b;return dn_I16_bits(x);}
static int16_t dn_I16_sat_mul(int16_t a,int16_t b){__int128 v=(__int128)a*(__int128)b;if(v<(-((__int128)1<<15) ))return (int16_t)(-((__int128)1<<15) );if(v>(((__int128)1<<15)-1))return (int16_t)(((__int128)1<<15)-1);return (int16_t)v;}
static int16_t dn_I16_div(int16_t a,int16_t b){if(!b)dn_fail("division by zero");if(a==(int16_t)(-((__int128)1<<15) ) && b==-1)dn_fail("integer overflow");return a/b;}
static int16_t dn_I16_mod(int16_t a,int16_t b){if(!b)dn_fail("division by zero");if(a==(int16_t)(-((__int128)1<<15) ) && b==-1)dn_fail("integer overflow");return a%b;}
static int16_t dn_I16_shl(int16_t a,int16_t b){if(b<0 || b>=16)dn_fail("shift count out of range");return dn_I16_bits((uint64_t)a<<b);}
static int16_t dn_I16_shr(int16_t a,int16_t b){if(b<0 || b>=16)dn_fail("shift count out of range");uint64_t u=(uint64_t)a; if(a<0 && b>0)u=(u>>(unsigned)b)|(UINT64_MAX<<(16-(unsigned)b));else u>>=(unsigned)b;return dn_I16_bits(u);}
static int16_t dn_I16_neg(int16_t a){if(a==(int16_t)(-((__int128)1<<15) ))dn_fail("integer overflow");return -a;}
static int32_t dn_I32_bits(uint64_t x){ int32_t r; uint32_t b=(uint32_t)x;memcpy(&r,&b,sizeof r);return r;}
static int32_t dn_I32_add(int32_t a,int32_t b){__int128 v=(__int128)a+(__int128)b;if(v<(-((__int128)1<<31) ) || v>(((__int128)1<<31)-1))dn_fail("integer overflow");return (int32_t)v;}
static int32_t dn_I32_wrap_add(int32_t a,int32_t b){uint64_t x=(uint64_t)a+(uint64_t)b;return dn_I32_bits(x);}
static int32_t dn_I32_sat_add(int32_t a,int32_t b){__int128 v=(__int128)a+(__int128)b;if(v<(-((__int128)1<<31) ))return (int32_t)(-((__int128)1<<31) );if(v>(((__int128)1<<31)-1))return (int32_t)(((__int128)1<<31)-1);return (int32_t)v;}
static int32_t dn_I32_sub(int32_t a,int32_t b){__int128 v=(__int128)a-(__int128)b;if(v<(-((__int128)1<<31) ) || v>(((__int128)1<<31)-1))dn_fail("integer overflow");return (int32_t)v;}
static int32_t dn_I32_wrap_sub(int32_t a,int32_t b){uint64_t x=(uint64_t)a-(uint64_t)b;return dn_I32_bits(x);}
static int32_t dn_I32_sat_sub(int32_t a,int32_t b){__int128 v=(__int128)a-(__int128)b;if(v<(-((__int128)1<<31) ))return (int32_t)(-((__int128)1<<31) );if(v>(((__int128)1<<31)-1))return (int32_t)(((__int128)1<<31)-1);return (int32_t)v;}
static int32_t dn_I32_mul(int32_t a,int32_t b){__int128 v=(__int128)a*(__int128)b;if(v<(-((__int128)1<<31) ) || v>(((__int128)1<<31)-1))dn_fail("integer overflow");return (int32_t)v;}
static int32_t dn_I32_wrap_mul(int32_t a,int32_t b){uint64_t x=(uint64_t)a*(uint64_t)b;return dn_I32_bits(x);}
static int32_t dn_I32_sat_mul(int32_t a,int32_t b){__int128 v=(__int128)a*(__int128)b;if(v<(-((__int128)1<<31) ))return (int32_t)(-((__int128)1<<31) );if(v>(((__int128)1<<31)-1))return (int32_t)(((__int128)1<<31)-1);return (int32_t)v;}
static int32_t dn_I32_div(int32_t a,int32_t b){if(!b)dn_fail("division by zero");if(a==(int32_t)(-((__int128)1<<31) ) && b==-1)dn_fail("integer overflow");return a/b;}
static int32_t dn_I32_mod(int32_t a,int32_t b){if(!b)dn_fail("division by zero");if(a==(int32_t)(-((__int128)1<<31) ) && b==-1)dn_fail("integer overflow");return a%b;}
static int32_t dn_I32_shl(int32_t a,int32_t b){if(b<0 || b>=32)dn_fail("shift count out of range");return dn_I32_bits((uint64_t)a<<b);}
static int32_t dn_I32_shr(int32_t a,int32_t b){if(b<0 || b>=32)dn_fail("shift count out of range");uint64_t u=(uint64_t)a; if(a<0 && b>0)u=(u>>(unsigned)b)|(UINT64_MAX<<(32-(unsigned)b));else u>>=(unsigned)b;return dn_I32_bits(u);}
static int32_t dn_I32_neg(int32_t a){if(a==(int32_t)(-((__int128)1<<31) ))dn_fail("integer overflow");return -a;}
static int64_t dn_I64_bits(uint64_t x){ int64_t r; uint64_t b=(uint64_t)x;memcpy(&r,&b,sizeof r);return r;}
static int64_t dn_I64_add(int64_t a,int64_t b){__int128 v=(__int128)a+(__int128)b;if(v<(-((__int128)1<<63) ) || v>(((__int128)1<<63)-1))dn_fail("integer overflow");return (int64_t)v;}
static int64_t dn_I64_wrap_add(int64_t a,int64_t b){uint64_t x=(uint64_t)a+(uint64_t)b;return dn_I64_bits(x);}
static int64_t dn_I64_sat_add(int64_t a,int64_t b){__int128 v=(__int128)a+(__int128)b;if(v<(-((__int128)1<<63) ))return (int64_t)(-((__int128)1<<63) );if(v>(((__int128)1<<63)-1))return (int64_t)(((__int128)1<<63)-1);return (int64_t)v;}
static int64_t dn_I64_sub(int64_t a,int64_t b){__int128 v=(__int128)a-(__int128)b;if(v<(-((__int128)1<<63) ) || v>(((__int128)1<<63)-1))dn_fail("integer overflow");return (int64_t)v;}
static int64_t dn_I64_wrap_sub(int64_t a,int64_t b){uint64_t x=(uint64_t)a-(uint64_t)b;return dn_I64_bits(x);}
static int64_t dn_I64_sat_sub(int64_t a,int64_t b){__int128 v=(__int128)a-(__int128)b;if(v<(-((__int128)1<<63) ))return (int64_t)(-((__int128)1<<63) );if(v>(((__int128)1<<63)-1))return (int64_t)(((__int128)1<<63)-1);return (int64_t)v;}
static int64_t dn_I64_mul(int64_t a,int64_t b){__int128 v=(__int128)a*(__int128)b;if(v<(-((__int128)1<<63) ) || v>(((__int128)1<<63)-1))dn_fail("integer overflow");return (int64_t)v;}
static int64_t dn_I64_wrap_mul(int64_t a,int64_t b){uint64_t x=(uint64_t)a*(uint64_t)b;return dn_I64_bits(x);}
static int64_t dn_I64_sat_mul(int64_t a,int64_t b){__int128 v=(__int128)a*(__int128)b;if(v<(-((__int128)1<<63) ))return (int64_t)(-((__int128)1<<63) );if(v>(((__int128)1<<63)-1))return (int64_t)(((__int128)1<<63)-1);return (int64_t)v;}
static int64_t dn_I64_div(int64_t a,int64_t b){if(!b)dn_fail("division by zero");if(a==(int64_t)(-((__int128)1<<63) ) && b==-1)dn_fail("integer overflow");return a/b;}
static int64_t dn_I64_mod(int64_t a,int64_t b){if(!b)dn_fail("division by zero");if(a==(int64_t)(-((__int128)1<<63) ) && b==-1)dn_fail("integer overflow");return a%b;}
static int64_t dn_I64_shl(int64_t a,int64_t b){if(b<0 || b>=64)dn_fail("shift count out of range");return dn_I64_bits((uint64_t)a<<b);}
static int64_t dn_I64_shr(int64_t a,int64_t b){if(b<0 || b>=64)dn_fail("shift count out of range");uint64_t u=(uint64_t)a; if(a<0 && b>0)u=(u>>(unsigned)b)|(UINT64_MAX<<(64-(unsigned)b));else u>>=(unsigned)b;return dn_I64_bits(u);}
static int64_t dn_I64_neg(int64_t a){if(a==(int64_t)(-((__int128)1<<63) ))dn_fail("integer overflow");return -a;}
static intptr_t dn_ISize_bits(uint64_t x){ intptr_t r; uint64_t b=(uint64_t)x;memcpy(&r,&b,sizeof r);return r;}
static intptr_t dn_ISize_add(intptr_t a,intptr_t b){__int128 v=(__int128)a+(__int128)b;if(v<(-((__int128)1<<63) ) || v>(((__int128)1<<63)-1))dn_fail("integer overflow");return (intptr_t)v;}
static intptr_t dn_ISize_wrap_add(intptr_t a,intptr_t b){uint64_t x=(uint64_t)a+(uint64_t)b;return dn_ISize_bits(x);}
static intptr_t dn_ISize_sat_add(intptr_t a,intptr_t b){__int128 v=(__int128)a+(__int128)b;if(v<(-((__int128)1<<63) ))return (intptr_t)(-((__int128)1<<63) );if(v>(((__int128)1<<63)-1))return (intptr_t)(((__int128)1<<63)-1);return (intptr_t)v;}
static intptr_t dn_ISize_sub(intptr_t a,intptr_t b){__int128 v=(__int128)a-(__int128)b;if(v<(-((__int128)1<<63) ) || v>(((__int128)1<<63)-1))dn_fail("integer overflow");return (intptr_t)v;}
static intptr_t dn_ISize_wrap_sub(intptr_t a,intptr_t b){uint64_t x=(uint64_t)a-(uint64_t)b;return dn_ISize_bits(x);}
static intptr_t dn_ISize_sat_sub(intptr_t a,intptr_t b){__int128 v=(__int128)a-(__int128)b;if(v<(-((__int128)1<<63) ))return (intptr_t)(-((__int128)1<<63) );if(v>(((__int128)1<<63)-1))return (intptr_t)(((__int128)1<<63)-1);return (intptr_t)v;}
static intptr_t dn_ISize_mul(intptr_t a,intptr_t b){__int128 v=(__int128)a*(__int128)b;if(v<(-((__int128)1<<63) ) || v>(((__int128)1<<63)-1))dn_fail("integer overflow");return (intptr_t)v;}
static intptr_t dn_ISize_wrap_mul(intptr_t a,intptr_t b){uint64_t x=(uint64_t)a*(uint64_t)b;return dn_ISize_bits(x);}
static intptr_t dn_ISize_sat_mul(intptr_t a,intptr_t b){__int128 v=(__int128)a*(__int128)b;if(v<(-((__int128)1<<63) ))return (intptr_t)(-((__int128)1<<63) );if(v>(((__int128)1<<63)-1))return (intptr_t)(((__int128)1<<63)-1);return (intptr_t)v;}
static intptr_t dn_ISize_div(intptr_t a,intptr_t b){if(!b)dn_fail("division by zero");if(a==(intptr_t)(-((__int128)1<<63) ) && b==-1)dn_fail("integer overflow");return a/b;}
static intptr_t dn_ISize_mod(intptr_t a,intptr_t b){if(!b)dn_fail("division by zero");if(a==(intptr_t)(-((__int128)1<<63) ) && b==-1)dn_fail("integer overflow");return a%b;}
static intptr_t dn_ISize_shl(intptr_t a,intptr_t b){if(b<0 || b>=64)dn_fail("shift count out of range");return dn_ISize_bits((uint64_t)a<<b);}
static intptr_t dn_ISize_shr(intptr_t a,intptr_t b){if(b<0 || b>=64)dn_fail("shift count out of range");uint64_t u=(uint64_t)a; if(a<0 && b>0)u=(u>>(unsigned)b)|(UINT64_MAX<<(64-(unsigned)b));else u>>=(unsigned)b;return dn_ISize_bits(u);}
static intptr_t dn_ISize_neg(intptr_t a){if(a==(intptr_t)(-((__int128)1<<63) ))dn_fail("integer overflow");return -a;}
typedef struct dn_r_NetText dn_r_NetText;
typedef struct dn_r_NetIo dn_r_NetIo;
typedef struct dn_r_NetOpened dn_r_NetOpened;
typedef struct dn_r_NetAddress dn_r_NetAddress;
typedef struct dn_r_NetResolved dn_r_NetResolved;
typedef struct dn_r_NetDatagram dn_r_NetDatagram;
typedef struct dn_r_NetWebSocketIo dn_r_NetWebSocketIo;
typedef struct dn_r_NetPoll dn_r_NetPoll;
typedef struct dn_r_NetworkError dn_r_NetworkError;
typedef struct dn_r_NetworkTransfer dn_r_NetworkTransfer;
typedef struct dn_r_TcpSocket dn_r_TcpSocket;
typedef struct dn_r_TcpListener dn_r_TcpListener;
typedef struct dn_r_UdpSocket dn_r_UdpSocket;
typedef struct dn_r_TlsSocket dn_r_TlsSocket;
typedef struct dn_r_SocketAddress dn_r_SocketAddress;
typedef struct dn_r_SocketAddressResult dn_r_SocketAddressResult;
typedef struct dn_r_SocketDatagram dn_r_SocketDatagram;
typedef struct dn_r_SocketOptions dn_r_SocketOptions;
typedef struct dn_r_NativeSocketOptions dn_r_NativeSocketOptions;
typedef struct dn_r_SocketOptionsResult dn_r_SocketOptionsResult;
typedef struct dn_r_NetworkReady dn_r_NetworkReady;
typedef struct dn_r_NetworkReactor dn_r_NetworkReactor;
typedef struct dn_r_ReactorTcp dn_r_ReactorTcp;
typedef struct dn_r_ReactorListener dn_r_ReactorListener;
typedef struct dn_r_ReactorUdp dn_r_ReactorUdp;
typedef struct dn_r_ReactorTcpResult dn_r_ReactorTcpResult;
typedef struct dn_r_ReactorListenerResult dn_r_ReactorListenerResult;
typedef struct dn_r_ReactorUdpResult dn_r_ReactorUdpResult;
typedef struct dn_r_NetworkOperation dn_r_NetworkOperation;
typedef struct dn_r_ReactorConnect dn_r_ReactorConnect;
typedef struct dn_r_ReactorCompletion dn_r_ReactorCompletion;
typedef struct dn_r_ReactorStatistics dn_r_ReactorStatistics;
typedef struct dn_r_ReactorNativeRequest dn_r_ReactorNativeRequest;
typedef struct dn_r_ReactorNativeCompletion dn_r_ReactorNativeCompletion;
typedef struct dn_r_ReactorNativeStats dn_r_ReactorNativeStats;
typedef struct dn_r_NetworkScope dn_r_NetworkScope;
typedef struct dn_r_EventWaker dn_r_EventWaker;
typedef struct dn_r_OperationCanceller dn_r_OperationCanceller;
typedef struct dn_r_ScopeCanceller dn_r_ScopeCanceller;
typedef struct dn_r_ScopeNativeStatistics dn_r_ScopeNativeStatistics;
typedef struct dn_r_ScopeStatistics dn_r_ScopeStatistics;
typedef struct dn_t_resource_r9_NetOpened_net_dispose dn_t_resource_r9_NetOpened_net_dispose;
typedef struct dn_t_resource_r9_NetOpened_reactor_dispose dn_t_resource_r9_NetOpened_reactor_dispose;
typedef struct dn_t_resource_r9_NetOpened_scope_dispose dn_t_resource_r9_NetOpened_scope_dispose;
typedef struct dn_t_resource_r9_NetOpened_scope_waker_dispose dn_t_resource_r9_NetOpened_scope_waker_dispose;
typedef struct dn_t_resource_h_net_cancel_dispose dn_t_resource_h_net_cancel_dispose;
struct dn_r_NetOpened {
    void * f_handle;
    int32_t f_status;
    int32_t f_system;
};
struct dn_t_resource_r9_NetOpened_net_dispose { dn_r_NetOpened value;bool active; };
struct dn_t_resource_r9_NetOpened_reactor_dispose { dn_r_NetOpened value;bool active; };
struct dn_t_resource_r9_NetOpened_scope_dispose { dn_r_NetOpened value;bool active; };
struct dn_t_resource_r9_NetOpened_scope_waker_dispose { dn_r_NetOpened value;bool active; };
struct dn_t_resource_h_net_cancel_dispose { void * value;bool active; };
struct dn_r_EventWaker {
    dn_t_resource_r9_NetOpened_scope_waker_dispose * f_inner;
};
struct dn_r_SocketOptions {
    uint32_t f_no_delay;
    uint32_t f_keep_alive;
    uint32_t f_send_buffer;
    uint32_t f_receive_buffer;
};
struct dn_r_NativeSocketOptions {
    dn_r_SocketOptions f_options;
    int32_t f_status;
    int32_t f_system;
};
typedef struct dn_t_arr16_U8 { uint8_t data[16]; } dn_t_arr16_U8;
struct dn_r_NetAddress {
    uint32_t f_family;
    uint32_t f_port;
    uint32_t f_scope;
    dn_t_arr16_U8 f_bytes;
};
struct dn_r_NetIo {
    size_t f_count;
    int32_t f_status;
    int32_t f_system;
};
struct dn_r_NetDatagram {
    dn_r_NetIo f_io;
    dn_r_NetAddress f_peer;
};
struct dn_r_NetPoll {
    void * f_socket;
    uint32_t f_events;
    uint32_t f_ready;
    int32_t f_status;
    int32_t f_system;
};
struct dn_r_NetResolved {
    dn_r_NetAddress f_address;
    int32_t f_status;
    int32_t f_system;
};
struct dn_r_NetText {
    uint8_t * f_data;
    size_t f_count;
};
struct dn_r_NetWebSocketIo {
    dn_r_NetIo f_io;
    uint64_t f_offset;
    uint64_t f_remaining;
    uint32_t f_flags;
};
struct dn_r_NetworkError {
    int32_t f_kind;
    int32_t f_system;
    uint32_t f_operation;
};
struct dn_r_NetworkOperation {
    uint64_t f_id;
    dn_r_NetworkError f_error;
};
struct dn_r_NetworkReactor {
    dn_t_resource_r9_NetOpened_reactor_dispose * f_inner;
};
struct dn_r_NetworkReady {
    uint32_t f_events;
    dn_r_NetworkError f_error;
};
struct dn_r_NetworkScope {
    dn_t_resource_r9_NetOpened_scope_dispose * f_inner;
};
struct dn_r_NetworkTransfer {
    size_t f_count;
    dn_r_NetworkError f_error;
};
struct dn_r_OperationCanceller {
    dn_t_resource_r9_NetOpened_scope_waker_dispose * f_inner;
};
struct dn_r_ReactorTcp {
    uint64_t f_id;
};
struct dn_r_SocketAddress {
    dn_r_NetAddress f_value;
};
struct dn_r_ReactorCompletion {
    uint64_t f_operation;
    uint64_t f_channel;
    dn_r_ReactorTcp f_accepted;
    dn_r_NetworkTransfer f_transfer;
    uint32_t f_kind;
    dn_r_SocketAddress f_peer;
};
struct dn_r_ReactorConnect {
    dn_r_ReactorTcp f_channel;
    dn_r_NetworkOperation f_operation;
};
struct dn_r_ReactorListener {
    uint64_t f_id;
};
struct dn_r_ReactorListenerResult {
    dn_r_ReactorListener f_channel;
    dn_r_NetworkError f_error;
};
struct dn_r_ReactorNativeCompletion {
    uint64_t f_id;
    uint64_t f_channel;
    uint64_t f_accepted;
    size_t f_count;
    int32_t f_status;
    int32_t f_system;
    uint32_t f_kind;
    dn_r_NetAddress f_peer;
};
struct dn_r_ReactorNativeRequest {
    uint64_t f_id;
    uint64_t f_channel;
    int32_t f_status;
    int32_t f_system;
};
struct dn_r_ReactorNativeStats {
    size_t f_buffered_bytes;
    size_t f_byte_limit;
    uint32_t f_channels;
    uint32_t f_pending;
    uint32_t f_completed;
    uint32_t f_channel_limit;
    uint32_t f_operation_limit;
    uint32_t f_backend;
    int32_t f_status;
    int32_t f_system;
};
struct dn_r_ReactorStatistics {
    size_t f_buffered_bytes;
    size_t f_byte_limit;
    uint32_t f_channels;
    uint32_t f_pending;
    uint32_t f_completed;
    uint32_t f_channel_limit;
    uint32_t f_operation_limit;
    uint32_t f_backend;
    dn_r_NetworkError f_error;
};
struct dn_r_ReactorTcpResult {
    dn_r_ReactorTcp f_channel;
    dn_r_NetworkError f_error;
};
struct dn_r_ReactorUdp {
    uint64_t f_id;
};
struct dn_r_ReactorUdpResult {
    dn_r_ReactorUdp f_channel;
    dn_r_NetworkError f_error;
};
struct dn_r_ScopeCanceller {
    dn_t_resource_r9_NetOpened_scope_waker_dispose * f_inner;
};
struct dn_r_ScopeNativeStatistics {
    dn_r_ReactorNativeStats f_reactor;
    uint32_t f_queued;
    uint32_t f_queue_limit;
    uint32_t f_kernel_pending;
};
struct dn_r_ScopeStatistics {
    dn_r_ReactorStatistics f_reactor;
    uint32_t f_queued;
    uint32_t f_queue_limit;
    uint32_t f_kernel_pending;
};
struct dn_r_SocketAddressResult {
    dn_r_SocketAddress f_address;
    dn_r_NetworkError f_error;
};
struct dn_r_SocketDatagram {
    dn_r_NetworkTransfer f_transfer;
    dn_r_SocketAddress f_peer;
};
struct dn_r_SocketOptionsResult {
    dn_r_SocketOptions f_options;
    dn_r_NetworkError f_error;
};
struct dn_r_TcpListener {
    dn_t_resource_r9_NetOpened_net_dispose * f_inner;
};
struct dn_r_TcpSocket {
    dn_t_resource_r9_NetOpened_net_dispose * f_inner;
    uint32_t f_opened_by;
};
struct dn_r_TlsSocket {
    dn_t_resource_r9_NetOpened_net_dispose * f_inner;
};
struct dn_r_UdpSocket {
    dn_t_resource_r9_NetOpened_net_dispose * f_inner;
};
typedef struct dn_t_buf_U8 { uint8_t *data;size_t len,cap; } dn_t_buf_U8;
typedef struct dn_t_buf_r7_NetPoll { dn_r_NetPoll *data;size_t len,cap; } dn_t_buf_r7_NetPoll;
typedef struct dn_t_slice0_U8 { uint8_t const *data;size_t len; } dn_t_slice0_U8;
_Static_assert(sizeof(dn_r_NetText)==16,"NetText ABI size");
_Static_assert(_Alignof(dn_r_NetText)==8,"NetText ABI alignment");
_Static_assert(offsetof(dn_r_NetText,f_data)==0,"NetText.data ABI offset");
_Static_assert(offsetof(dn_r_NetText,f_count)==8,"NetText.count ABI offset");
_Static_assert(sizeof(dn_r_NetIo)==16,"NetIo ABI size");
_Static_assert(_Alignof(dn_r_NetIo)==8,"NetIo ABI alignment");
_Static_assert(offsetof(dn_r_NetIo,f_count)==0,"NetIo.count ABI offset");
_Static_assert(offsetof(dn_r_NetIo,f_status)==8,"NetIo.status ABI offset");
_Static_assert(offsetof(dn_r_NetIo,f_system)==12,"NetIo.system ABI offset");
_Static_assert(sizeof(dn_r_NetOpened)==16,"NetOpened ABI size");
_Static_assert(_Alignof(dn_r_NetOpened)==8,"NetOpened ABI alignment");
_Static_assert(offsetof(dn_r_NetOpened,f_handle)==0,"NetOpened.handle ABI offset");
_Static_assert(offsetof(dn_r_NetOpened,f_status)==8,"NetOpened.status ABI offset");
_Static_assert(offsetof(dn_r_NetOpened,f_system)==12,"NetOpened.system ABI offset");
_Static_assert(sizeof(dn_r_NetAddress)==28,"NetAddress ABI size");
_Static_assert(_Alignof(dn_r_NetAddress)==4,"NetAddress ABI alignment");
_Static_assert(offsetof(dn_r_NetAddress,f_family)==0,"NetAddress.family ABI offset");
_Static_assert(offsetof(dn_r_NetAddress,f_port)==4,"NetAddress.port ABI offset");
_Static_assert(offsetof(dn_r_NetAddress,f_scope)==8,"NetAddress.scope ABI offset");
_Static_assert(offsetof(dn_r_NetAddress,f_bytes)==12,"NetAddress.bytes ABI offset");
_Static_assert(sizeof(dn_r_NetResolved)==36,"NetResolved ABI size");
_Static_assert(_Alignof(dn_r_NetResolved)==4,"NetResolved ABI alignment");
_Static_assert(offsetof(dn_r_NetResolved,f_address)==0,"NetResolved.address ABI offset");
_Static_assert(offsetof(dn_r_NetResolved,f_status)==28,"NetResolved.status ABI offset");
_Static_assert(offsetof(dn_r_NetResolved,f_system)==32,"NetResolved.system ABI offset");
_Static_assert(sizeof(dn_r_NetDatagram)==48,"NetDatagram ABI size");
_Static_assert(_Alignof(dn_r_NetDatagram)==8,"NetDatagram ABI alignment");
_Static_assert(offsetof(dn_r_NetDatagram,f_io)==0,"NetDatagram.io ABI offset");
_Static_assert(offsetof(dn_r_NetDatagram,f_peer)==16,"NetDatagram.peer ABI offset");
_Static_assert(sizeof(dn_r_NetWebSocketIo)==40,"NetWebSocketIo ABI size");
_Static_assert(_Alignof(dn_r_NetWebSocketIo)==8,"NetWebSocketIo ABI alignment");
_Static_assert(offsetof(dn_r_NetWebSocketIo,f_io)==0,"NetWebSocketIo.io ABI offset");
_Static_assert(offsetof(dn_r_NetWebSocketIo,f_offset)==16,"NetWebSocketIo.offset ABI offset");
_Static_assert(offsetof(dn_r_NetWebSocketIo,f_remaining)==24,"NetWebSocketIo.remaining ABI offset");
_Static_assert(offsetof(dn_r_NetWebSocketIo,f_flags)==32,"NetWebSocketIo.flags ABI offset");
_Static_assert(sizeof(dn_r_NetPoll)==24,"NetPoll ABI size");
_Static_assert(_Alignof(dn_r_NetPoll)==8,"NetPoll ABI alignment");
_Static_assert(offsetof(dn_r_NetPoll,f_socket)==0,"NetPoll.socket ABI offset");
_Static_assert(offsetof(dn_r_NetPoll,f_events)==8,"NetPoll.events ABI offset");
_Static_assert(offsetof(dn_r_NetPoll,f_ready)==12,"NetPoll.ready ABI offset");
_Static_assert(offsetof(dn_r_NetPoll,f_status)==16,"NetPoll.status ABI offset");
_Static_assert(offsetof(dn_r_NetPoll,f_system)==20,"NetPoll.system ABI offset");
_Static_assert(sizeof(dn_r_NetworkError)==12,"NetworkError ABI size");
_Static_assert(_Alignof(dn_r_NetworkError)==4,"NetworkError ABI alignment");
_Static_assert(offsetof(dn_r_NetworkError,f_kind)==0,"NetworkError.kind ABI offset");
_Static_assert(offsetof(dn_r_NetworkError,f_system)==4,"NetworkError.system ABI offset");
_Static_assert(offsetof(dn_r_NetworkError,f_operation)==8,"NetworkError.operation ABI offset");
_Static_assert(sizeof(dn_r_NetworkTransfer)==24,"NetworkTransfer ABI size");
_Static_assert(_Alignof(dn_r_NetworkTransfer)==8,"NetworkTransfer ABI alignment");
_Static_assert(offsetof(dn_r_NetworkTransfer,f_count)==0,"NetworkTransfer.count ABI offset");
_Static_assert(offsetof(dn_r_NetworkTransfer,f_error)==8,"NetworkTransfer.error ABI offset");
_Static_assert(sizeof(dn_r_TcpSocket)==16,"TcpSocket ABI size");
_Static_assert(_Alignof(dn_r_TcpSocket)==8,"TcpSocket ABI alignment");
_Static_assert(offsetof(dn_r_TcpSocket,f_inner)==0,"TcpSocket.inner ABI offset");
_Static_assert(offsetof(dn_r_TcpSocket,f_opened_by)==8,"TcpSocket.opened_by ABI offset");
_Static_assert(sizeof(dn_r_TcpListener)==8,"TcpListener ABI size");
_Static_assert(_Alignof(dn_r_TcpListener)==8,"TcpListener ABI alignment");
_Static_assert(offsetof(dn_r_TcpListener,f_inner)==0,"TcpListener.inner ABI offset");
_Static_assert(sizeof(dn_r_UdpSocket)==8,"UdpSocket ABI size");
_Static_assert(_Alignof(dn_r_UdpSocket)==8,"UdpSocket ABI alignment");
_Static_assert(offsetof(dn_r_UdpSocket,f_inner)==0,"UdpSocket.inner ABI offset");
_Static_assert(sizeof(dn_r_TlsSocket)==8,"TlsSocket ABI size");
_Static_assert(_Alignof(dn_r_TlsSocket)==8,"TlsSocket ABI alignment");
_Static_assert(offsetof(dn_r_TlsSocket,f_inner)==0,"TlsSocket.inner ABI offset");
_Static_assert(sizeof(dn_r_SocketAddress)==28,"SocketAddress ABI size");
_Static_assert(_Alignof(dn_r_SocketAddress)==4,"SocketAddress ABI alignment");
_Static_assert(offsetof(dn_r_SocketAddress,f_value)==0,"SocketAddress.value ABI offset");
_Static_assert(sizeof(dn_r_SocketAddressResult)==40,"SocketAddressResult ABI size");
_Static_assert(_Alignof(dn_r_SocketAddressResult)==4,"SocketAddressResult ABI alignment");
_Static_assert(offsetof(dn_r_SocketAddressResult,f_address)==0,"SocketAddressResult.address ABI offset");
_Static_assert(offsetof(dn_r_SocketAddressResult,f_error)==28,"SocketAddressResult.error ABI offset");
_Static_assert(sizeof(dn_r_SocketDatagram)==56,"SocketDatagram ABI size");
_Static_assert(_Alignof(dn_r_SocketDatagram)==8,"SocketDatagram ABI alignment");
_Static_assert(offsetof(dn_r_SocketDatagram,f_transfer)==0,"SocketDatagram.transfer ABI offset");
_Static_assert(offsetof(dn_r_SocketDatagram,f_peer)==24,"SocketDatagram.peer ABI offset");
_Static_assert(sizeof(dn_r_SocketOptions)==16,"SocketOptions ABI size");
_Static_assert(_Alignof(dn_r_SocketOptions)==4,"SocketOptions ABI alignment");
_Static_assert(offsetof(dn_r_SocketOptions,f_no_delay)==0,"SocketOptions.no_delay ABI offset");
_Static_assert(offsetof(dn_r_SocketOptions,f_keep_alive)==4,"SocketOptions.keep_alive ABI offset");
_Static_assert(offsetof(dn_r_SocketOptions,f_send_buffer)==8,"SocketOptions.send_buffer ABI offset");
_Static_assert(offsetof(dn_r_SocketOptions,f_receive_buffer)==12,"SocketOptions.receive_buffer ABI offset");
_Static_assert(sizeof(dn_r_NativeSocketOptions)==24,"NativeSocketOptions ABI size");
_Static_assert(_Alignof(dn_r_NativeSocketOptions)==4,"NativeSocketOptions ABI alignment");
_Static_assert(offsetof(dn_r_NativeSocketOptions,f_options)==0,"NativeSocketOptions.options ABI offset");
_Static_assert(offsetof(dn_r_NativeSocketOptions,f_status)==16,"NativeSocketOptions.status ABI offset");
_Static_assert(offsetof(dn_r_NativeSocketOptions,f_system)==20,"NativeSocketOptions.system ABI offset");
_Static_assert(sizeof(dn_r_SocketOptionsResult)==28,"SocketOptionsResult ABI size");
_Static_assert(_Alignof(dn_r_SocketOptionsResult)==4,"SocketOptionsResult ABI alignment");
_Static_assert(offsetof(dn_r_SocketOptionsResult,f_options)==0,"SocketOptionsResult.options ABI offset");
_Static_assert(offsetof(dn_r_SocketOptionsResult,f_error)==16,"SocketOptionsResult.error ABI offset");
_Static_assert(sizeof(dn_r_NetworkReady)==16,"NetworkReady ABI size");
_Static_assert(_Alignof(dn_r_NetworkReady)==4,"NetworkReady ABI alignment");
_Static_assert(offsetof(dn_r_NetworkReady,f_events)==0,"NetworkReady.events ABI offset");
_Static_assert(offsetof(dn_r_NetworkReady,f_error)==4,"NetworkReady.error ABI offset");
_Static_assert(sizeof(dn_r_NetworkReactor)==8,"NetworkReactor ABI size");
_Static_assert(_Alignof(dn_r_NetworkReactor)==8,"NetworkReactor ABI alignment");
_Static_assert(offsetof(dn_r_NetworkReactor,f_inner)==0,"NetworkReactor.inner ABI offset");
_Static_assert(sizeof(dn_r_ReactorTcp)==8,"ReactorTcp ABI size");
_Static_assert(_Alignof(dn_r_ReactorTcp)==8,"ReactorTcp ABI alignment");
_Static_assert(offsetof(dn_r_ReactorTcp,f_id)==0,"ReactorTcp.id ABI offset");
_Static_assert(sizeof(dn_r_ReactorListener)==8,"ReactorListener ABI size");
_Static_assert(_Alignof(dn_r_ReactorListener)==8,"ReactorListener ABI alignment");
_Static_assert(offsetof(dn_r_ReactorListener,f_id)==0,"ReactorListener.id ABI offset");
_Static_assert(sizeof(dn_r_ReactorUdp)==8,"ReactorUdp ABI size");
_Static_assert(_Alignof(dn_r_ReactorUdp)==8,"ReactorUdp ABI alignment");
_Static_assert(offsetof(dn_r_ReactorUdp,f_id)==0,"ReactorUdp.id ABI offset");
_Static_assert(sizeof(dn_r_ReactorTcpResult)==24,"ReactorTcpResult ABI size");
_Static_assert(_Alignof(dn_r_ReactorTcpResult)==8,"ReactorTcpResult ABI alignment");
_Static_assert(offsetof(dn_r_ReactorTcpResult,f_channel)==0,"ReactorTcpResult.channel ABI offset");
_Static_assert(offsetof(dn_r_ReactorTcpResult,f_error)==8,"ReactorTcpResult.error ABI offset");
_Static_assert(sizeof(dn_r_ReactorListenerResult)==24,"ReactorListenerResult ABI size");
_Static_assert(_Alignof(dn_r_ReactorListenerResult)==8,"ReactorListenerResult ABI alignment");
_Static_assert(offsetof(dn_r_ReactorListenerResult,f_channel)==0,"ReactorListenerResult.channel ABI offset");
_Static_assert(offsetof(dn_r_ReactorListenerResult,f_error)==8,"ReactorListenerResult.error ABI offset");
_Static_assert(sizeof(dn_r_ReactorUdpResult)==24,"ReactorUdpResult ABI size");
_Static_assert(_Alignof(dn_r_ReactorUdpResult)==8,"ReactorUdpResult ABI alignment");
_Static_assert(offsetof(dn_r_ReactorUdpResult,f_channel)==0,"ReactorUdpResult.channel ABI offset");
_Static_assert(offsetof(dn_r_ReactorUdpResult,f_error)==8,"ReactorUdpResult.error ABI offset");
_Static_assert(sizeof(dn_r_NetworkOperation)==24,"NetworkOperation ABI size");
_Static_assert(_Alignof(dn_r_NetworkOperation)==8,"NetworkOperation ABI alignment");
_Static_assert(offsetof(dn_r_NetworkOperation,f_id)==0,"NetworkOperation.id ABI offset");
_Static_assert(offsetof(dn_r_NetworkOperation,f_error)==8,"NetworkOperation.error ABI offset");
_Static_assert(sizeof(dn_r_ReactorConnect)==32,"ReactorConnect ABI size");
_Static_assert(_Alignof(dn_r_ReactorConnect)==8,"ReactorConnect ABI alignment");
_Static_assert(offsetof(dn_r_ReactorConnect,f_channel)==0,"ReactorConnect.channel ABI offset");
_Static_assert(offsetof(dn_r_ReactorConnect,f_operation)==8,"ReactorConnect.operation ABI offset");
_Static_assert(sizeof(dn_r_ReactorCompletion)==80,"ReactorCompletion ABI size");
_Static_assert(_Alignof(dn_r_ReactorCompletion)==8,"ReactorCompletion ABI alignment");
_Static_assert(offsetof(dn_r_ReactorCompletion,f_operation)==0,"ReactorCompletion.operation ABI offset");
_Static_assert(offsetof(dn_r_ReactorCompletion,f_channel)==8,"ReactorCompletion.channel ABI offset");
_Static_assert(offsetof(dn_r_ReactorCompletion,f_accepted)==16,"ReactorCompletion.accepted ABI offset");
_Static_assert(offsetof(dn_r_ReactorCompletion,f_transfer)==24,"ReactorCompletion.transfer ABI offset");
_Static_assert(offsetof(dn_r_ReactorCompletion,f_kind)==48,"ReactorCompletion.kind ABI offset");
_Static_assert(offsetof(dn_r_ReactorCompletion,f_peer)==52,"ReactorCompletion.peer ABI offset");
_Static_assert(sizeof(dn_r_ReactorStatistics)==56,"ReactorStatistics ABI size");
_Static_assert(_Alignof(dn_r_ReactorStatistics)==8,"ReactorStatistics ABI alignment");
_Static_assert(offsetof(dn_r_ReactorStatistics,f_buffered_bytes)==0,"ReactorStatistics.buffered_bytes ABI offset");
_Static_assert(offsetof(dn_r_ReactorStatistics,f_byte_limit)==8,"ReactorStatistics.byte_limit ABI offset");
_Static_assert(offsetof(dn_r_ReactorStatistics,f_channels)==16,"ReactorStatistics.channels ABI offset");
_Static_assert(offsetof(dn_r_ReactorStatistics,f_pending)==20,"ReactorStatistics.pending ABI offset");
_Static_assert(offsetof(dn_r_ReactorStatistics,f_completed)==24,"ReactorStatistics.completed ABI offset");
_Static_assert(offsetof(dn_r_ReactorStatistics,f_channel_limit)==28,"ReactorStatistics.channel_limit ABI offset");
_Static_assert(offsetof(dn_r_ReactorStatistics,f_operation_limit)==32,"ReactorStatistics.operation_limit ABI offset");
_Static_assert(offsetof(dn_r_ReactorStatistics,f_backend)==36,"ReactorStatistics.backend ABI offset");
_Static_assert(offsetof(dn_r_ReactorStatistics,f_error)==40,"ReactorStatistics.error ABI offset");
_Static_assert(sizeof(dn_r_ReactorNativeRequest)==24,"ReactorNativeRequest ABI size");
_Static_assert(_Alignof(dn_r_ReactorNativeRequest)==8,"ReactorNativeRequest ABI alignment");
_Static_assert(offsetof(dn_r_ReactorNativeRequest,f_id)==0,"ReactorNativeRequest.id ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeRequest,f_channel)==8,"ReactorNativeRequest.channel ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeRequest,f_status)==16,"ReactorNativeRequest.status ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeRequest,f_system)==20,"ReactorNativeRequest.system ABI offset");
_Static_assert(sizeof(dn_r_ReactorNativeCompletion)==72,"ReactorNativeCompletion ABI size");
_Static_assert(_Alignof(dn_r_ReactorNativeCompletion)==8,"ReactorNativeCompletion ABI alignment");
_Static_assert(offsetof(dn_r_ReactorNativeCompletion,f_id)==0,"ReactorNativeCompletion.id ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeCompletion,f_channel)==8,"ReactorNativeCompletion.channel ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeCompletion,f_accepted)==16,"ReactorNativeCompletion.accepted ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeCompletion,f_count)==24,"ReactorNativeCompletion.count ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeCompletion,f_status)==32,"ReactorNativeCompletion.status ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeCompletion,f_system)==36,"ReactorNativeCompletion.system ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeCompletion,f_kind)==40,"ReactorNativeCompletion.kind ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeCompletion,f_peer)==44,"ReactorNativeCompletion.peer ABI offset");
_Static_assert(sizeof(dn_r_ReactorNativeStats)==48,"ReactorNativeStats ABI size");
_Static_assert(_Alignof(dn_r_ReactorNativeStats)==8,"ReactorNativeStats ABI alignment");
_Static_assert(offsetof(dn_r_ReactorNativeStats,f_buffered_bytes)==0,"ReactorNativeStats.buffered_bytes ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeStats,f_byte_limit)==8,"ReactorNativeStats.byte_limit ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeStats,f_channels)==16,"ReactorNativeStats.channels ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeStats,f_pending)==20,"ReactorNativeStats.pending ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeStats,f_completed)==24,"ReactorNativeStats.completed ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeStats,f_channel_limit)==28,"ReactorNativeStats.channel_limit ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeStats,f_operation_limit)==32,"ReactorNativeStats.operation_limit ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeStats,f_backend)==36,"ReactorNativeStats.backend ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeStats,f_status)==40,"ReactorNativeStats.status ABI offset");
_Static_assert(offsetof(dn_r_ReactorNativeStats,f_system)==44,"ReactorNativeStats.system ABI offset");
_Static_assert(sizeof(dn_r_NetworkScope)==8,"NetworkScope ABI size");
_Static_assert(_Alignof(dn_r_NetworkScope)==8,"NetworkScope ABI alignment");
_Static_assert(offsetof(dn_r_NetworkScope,f_inner)==0,"NetworkScope.inner ABI offset");
_Static_assert(sizeof(dn_r_EventWaker)==8,"EventWaker ABI size");
_Static_assert(_Alignof(dn_r_EventWaker)==8,"EventWaker ABI alignment");
_Static_assert(offsetof(dn_r_EventWaker,f_inner)==0,"EventWaker.inner ABI offset");
_Static_assert(sizeof(dn_r_OperationCanceller)==8,"OperationCanceller ABI size");
_Static_assert(_Alignof(dn_r_OperationCanceller)==8,"OperationCanceller ABI alignment");
_Static_assert(offsetof(dn_r_OperationCanceller,f_inner)==0,"OperationCanceller.inner ABI offset");
_Static_assert(sizeof(dn_r_ScopeCanceller)==8,"ScopeCanceller ABI size");
_Static_assert(_Alignof(dn_r_ScopeCanceller)==8,"ScopeCanceller ABI alignment");
_Static_assert(offsetof(dn_r_ScopeCanceller,f_inner)==0,"ScopeCanceller.inner ABI offset");
_Static_assert(sizeof(dn_r_ScopeNativeStatistics)==64,"ScopeNativeStatistics ABI size");
_Static_assert(_Alignof(dn_r_ScopeNativeStatistics)==8,"ScopeNativeStatistics ABI alignment");
_Static_assert(offsetof(dn_r_ScopeNativeStatistics,f_reactor)==0,"ScopeNativeStatistics.reactor ABI offset");
_Static_assert(offsetof(dn_r_ScopeNativeStatistics,f_queued)==48,"ScopeNativeStatistics.queued ABI offset");
_Static_assert(offsetof(dn_r_ScopeNativeStatistics,f_queue_limit)==52,"ScopeNativeStatistics.queue_limit ABI offset");
_Static_assert(offsetof(dn_r_ScopeNativeStatistics,f_kernel_pending)==56,"ScopeNativeStatistics.kernel_pending ABI offset");
_Static_assert(sizeof(dn_r_ScopeStatistics)==72,"ScopeStatistics ABI size");
_Static_assert(_Alignof(dn_r_ScopeStatistics)==8,"ScopeStatistics ABI alignment");
_Static_assert(offsetof(dn_r_ScopeStatistics,f_reactor)==0,"ScopeStatistics.reactor ABI offset");
_Static_assert(offsetof(dn_r_ScopeStatistics,f_queued)==56,"ScopeStatistics.queued ABI offset");
_Static_assert(offsetof(dn_r_ScopeStatistics,f_queue_limit)==60,"ScopeStatistics.queue_limit ABI offset");
_Static_assert(offsetof(dn_r_ScopeStatistics,f_kernel_pending)==64,"ScopeStatistics.kernel_pending ABI offset");
static void dn_drop_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * volatile*);static void dn_retain_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * volatile*);
static void dn_drop_resource_r9_NetOpened_reactor_dispose(dn_t_resource_r9_NetOpened_reactor_dispose * volatile*);static void dn_retain_resource_r9_NetOpened_reactor_dispose(dn_t_resource_r9_NetOpened_reactor_dispose * volatile*);
static void dn_drop_resource_r9_NetOpened_scope_dispose(dn_t_resource_r9_NetOpened_scope_dispose * volatile*);static void dn_retain_resource_r9_NetOpened_scope_dispose(dn_t_resource_r9_NetOpened_scope_dispose * volatile*);
static void dn_drop_resource_r9_NetOpened_scope_waker_dispose(dn_t_resource_r9_NetOpened_scope_waker_dispose * volatile*);static void dn_retain_resource_r9_NetOpened_scope_waker_dispose(dn_t_resource_r9_NetOpened_scope_waker_dispose * volatile*);
static void dn_drop_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * volatile*);static void dn_retain_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * volatile*);
static void dn_drop_r10_EventWaker(dn_r_EventWaker volatile*);static void dn_retain_r10_EventWaker(dn_r_EventWaker volatile*);
static void dn_drop_r14_NetworkReactor(dn_r_NetworkReactor volatile*);static void dn_retain_r14_NetworkReactor(dn_r_NetworkReactor volatile*);
static void dn_drop_r12_NetworkScope(dn_r_NetworkScope volatile*);static void dn_retain_r12_NetworkScope(dn_r_NetworkScope volatile*);
static void dn_drop_r18_OperationCanceller(dn_r_OperationCanceller volatile*);static void dn_retain_r18_OperationCanceller(dn_r_OperationCanceller volatile*);
static void dn_drop_r14_ScopeCanceller(dn_r_ScopeCanceller volatile*);static void dn_retain_r14_ScopeCanceller(dn_r_ScopeCanceller volatile*);
static void dn_drop_r11_TcpListener(dn_r_TcpListener volatile*);static void dn_retain_r11_TcpListener(dn_r_TcpListener volatile*);
static void dn_drop_r9_TcpSocket(dn_r_TcpSocket volatile*);static void dn_retain_r9_TcpSocket(dn_r_TcpSocket volatile*);
static void dn_drop_r9_TlsSocket(dn_r_TlsSocket volatile*);static void dn_retain_r9_TlsSocket(dn_r_TlsSocket volatile*);
static void dn_drop_r9_UdpSocket(dn_r_UdpSocket volatile*);static void dn_retain_r9_UdpSocket(dn_r_UdpSocket volatile*);
static void dn_drop_buf_U8(dn_t_buf_U8 volatile*);static void dn_retain_buf_U8(dn_t_buf_U8 volatile*);
static void dn_drop_buf_r7_NetPoll(dn_t_buf_r7_NetPoll volatile*);static void dn_retain_buf_r7_NetPoll(dn_t_buf_r7_NetPoll volatile*);
static void dn_adopt_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * volatile*);
static void dn_adopt_resource_r9_NetOpened_reactor_dispose(dn_t_resource_r9_NetOpened_reactor_dispose * volatile*);
static void dn_adopt_resource_r9_NetOpened_scope_dispose(dn_t_resource_r9_NetOpened_scope_dispose * volatile*);
static void dn_adopt_resource_r9_NetOpened_scope_waker_dispose(dn_t_resource_r9_NetOpened_scope_waker_dispose * volatile*);
static void dn_adopt_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * volatile*);
static void dn_adopt_r10_EventWaker(dn_r_EventWaker volatile*);
static void dn_adopt_r14_NetworkReactor(dn_r_NetworkReactor volatile*);
static void dn_adopt_r12_NetworkScope(dn_r_NetworkScope volatile*);
static void dn_adopt_r18_OperationCanceller(dn_r_OperationCanceller volatile*);
static void dn_adopt_r14_ScopeCanceller(dn_r_ScopeCanceller volatile*);
static void dn_adopt_r11_TcpListener(dn_r_TcpListener volatile*);
static void dn_adopt_r9_TcpSocket(dn_r_TcpSocket volatile*);
static void dn_adopt_r9_TlsSocket(dn_r_TlsSocket volatile*);
static void dn_adopt_r9_UdpSocket(dn_r_UdpSocket volatile*);
static void dn_adopt_buf_U8(dn_t_buf_U8 volatile*);
static void dn_adopt_buf_r7_NetPoll(dn_t_buf_r7_NetPoll volatile*);
static uint8_t dn_f_net_dispose(dn_r_NetOpened * v_state);
static uint8_t dn_f_net_cancel_dispose(void * * v_state);
static uint8_t * dn_f_net_input(dn_t_slice0_U8 v_bytes);
static uint8_t * dn_f_net_output(dn_t_buf_U8 * v_bytes);
static dn_r_NetText dn_f_net_text(dn_utf8 v_text);
static dn_r_NetText dn_f_net_bytes(dn_t_slice0_U8 v_bytes);
static dn_r_NetResolved dn_f_network_resolve_bytes(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_index);
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_socket_tcp_bytes(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_timeout_ms);
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_socket_listen_bytes(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_backlog);
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_socket_udp_bytes(dn_t_slice0_U8 v_host,uint16_t v_port);
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_socket_tls_bytes(dn_t_slice0_U8 v_url,uint32_t v_timeout_ms,dn_t_slice0_U8 v_ca_file,dn_t_slice0_U8 v_pin);
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_http_client_bytes(dn_t_slice0_U8 v_url,uint32_t v_timeout_ms,size_t v_maximum,dn_t_slice0_U8 v_ca_file,dn_t_slice0_U8 v_pin);
static dn_r_NetIo dn_f_http_header_bytes(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_slice0_U8 v_header);
static dn_r_NetIo dn_f_http_method_bytes(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_slice0_U8 v_method);
static dn_r_NetIo dn_f_http_ech_bytes(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_slice0_U8 v_config);
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_websocket_client_bytes(dn_t_slice0_U8 v_url,uint32_t v_timeout_ms,dn_t_slice0_U8 v_ca_file,dn_t_slice0_U8 v_pin);
static uint32_t dn_f_network_capabilities(void);
static uint32_t dn_f_network_provider_version(void);
static dn_r_NetIo dn_f_network_provider_shutdown(void);
static uint64_t dn_f_network_clock_ms(void);
static dn_r_NetResolved dn_f_network_resolve(dn_utf8 v_host,uint16_t v_port,uint32_t v_index);
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_socket_tcp(dn_utf8 v_host,uint16_t v_port,uint32_t v_timeout_ms);
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_socket_listen(dn_utf8 v_host,uint16_t v_port,uint32_t v_backlog);
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_socket_udp(dn_utf8 v_host,uint16_t v_port);
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_socket_accept(dn_t_resource_r9_NetOpened_net_dispose * * v_listener);
static int32_t dn_f_socket_status(dn_t_resource_r9_NetOpened_net_dispose * const * v_socket);
static int32_t dn_f_socket_system_error(dn_t_resource_r9_NetOpened_net_dispose * const * v_socket);
static dn_r_NetResolved dn_f_socket_local(dn_t_resource_r9_NetOpened_net_dispose * const * v_socket);
static dn_r_NetIo dn_f_socket_nodelay(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,bool v_enabled);
static dn_r_NetIo dn_f_socket_write(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_slice0_U8 v_bytes);
static dn_r_NetIo dn_f_socket_read(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_buf_U8 * v_bytes);
static dn_r_NetIo dn_f_socket_write_all(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetIo dn_f_socket_read_exact(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_buf_U8 * v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetIo dn_f_socket_shutdown_write(dn_t_resource_r9_NetOpened_net_dispose * * v_socket);
static dn_r_NetIo dn_f_socket_send_datagram(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_r_NetAddress v_peer,dn_t_slice0_U8 v_bytes);
static dn_r_NetDatagram dn_f_socket_read_datagram(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_buf_U8 * v_bytes);
static dn_r_NetIo dn_f_socket_wait(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,uint32_t v_events,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetPoll dn_f_socket_poll_entry(dn_t_resource_r9_NetOpened_net_dispose * const * v_socket,uint32_t v_events);
static dn_r_NetIo dn_f_socket_poll(dn_t_buf_r7_NetPoll * v_items,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_t_resource_h_net_cancel_dispose * dn_f_network_cancel(void);
static bool dn_f_network_cancel_valid(dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static uint8_t dn_f_network_cancel_signal(dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static uint8_t dn_f_network_cancel_reset(dn_t_resource_h_net_cancel_dispose * * v_cancel);
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_socket_tls(dn_utf8 v_url,uint32_t v_timeout_ms,dn_utf8 v_ca_file,dn_utf8 v_pin);
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_http_client(dn_utf8 v_url,uint32_t v_timeout_ms,size_t v_maximum,dn_utf8 v_ca_file,dn_utf8 v_pin);
static dn_r_NetIo dn_f_http_version(dn_t_resource_r9_NetOpened_net_dispose * * v_request,uint32_t v_version);
static dn_r_NetIo dn_f_http_header(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_utf8 v_header);
static dn_r_NetIo dn_f_http_method(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_utf8 v_method);
static dn_r_NetIo dn_f_http_body(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_slice0_U8 v_bytes);
static dn_r_NetIo dn_f_http_ech(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_utf8 v_config);
static dn_r_NetIo dn_f_http_perform(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetIo dn_f_http_info(dn_t_resource_r9_NetOpened_net_dispose * const * v_request,uint32_t v_field);
static dn_r_NetIo dn_f_http_read(dn_t_resource_r9_NetOpened_net_dispose * const * v_request,bool v_headers,size_t v_offset,dn_t_buf_U8 * v_bytes);
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_websocket_client(dn_utf8 v_url,uint32_t v_timeout_ms,dn_utf8 v_ca_file,dn_utf8 v_pin);
static dn_r_NetIo dn_f_websocket_start(dn_t_resource_r9_NetOpened_net_dispose * * v_socket);
static dn_r_NetIo dn_f_websocket_write(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_slice0_U8 v_bytes,uint32_t v_flags,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetWebSocketIo dn_f_websocket_read(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_buf_U8 * v_bytes);
static dn_r_NetIo dn_f_network_random(dn_t_buf_U8 * v_bytes);
static bool dn_f_network_ok(dn_r_NetworkError const * v_error);
static bool dn_f_network_would_block(dn_r_NetworkError const * v_error);
static bool dn_f_network_eof(dn_r_NetworkError const * v_error);
static bool dn_f_network_timed_out(dn_r_NetworkError const * v_error);
static bool dn_f_network_was_cancelled(dn_r_NetworkError const * v_error);
static bool dn_f_network_truncated(dn_r_NetworkError const * v_error);
static dn_utf8 dn_f_network_error_name(dn_r_NetworkError const * v_error);
static dn_r_NetworkTransfer dn_f_socket_transfer(dn_r_NetIo v_result,uint32_t v_operation);
static dn_r_NetworkReady dn_f_socket_ready(dn_r_NetIo v_result);
static dn_r_SocketAddressResult dn_f_socket_address_result(dn_r_NetResolved v_result,uint32_t v_operation);
static dn_r_SocketAddress dn_f_socket_address_v4(uint8_t v_a,uint8_t v_b,uint8_t v_c,uint8_t v_d,uint16_t v_port);
static dn_r_SocketAddress dn_f_socket_address_v6(dn_t_arr16_U8 v_bytes,uint16_t v_port,uint32_t v_scope);
static dn_r_SocketAddressResult dn_f_socket_address_resolve(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_family,uint32_t v_index);
static dn_r_NetworkTransfer dn_f_socket_address_write(dn_r_SocketAddress v_address,dn_t_buf_U8 * v_bytes,bool v_with_port);
static dn_r_SocketOptions dn_f_socket_options(bool v_no_delay,bool v_keep_alive,uint32_t v_send_buffer,uint32_t v_receive_buffer);
static dn_r_TcpSocket dn_f_tcp_connect(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_timeout_ms);
static dn_r_TcpSocket dn_f_tcp_connect_address(dn_r_SocketAddress v_address,uint32_t v_timeout_ms);
static dn_r_TcpListener dn_f_tcp_listen(dn_r_SocketAddress v_address,uint32_t v_backlog,bool v_dual_stack);
static dn_r_TcpSocket dn_f_tcp_accept(dn_r_TcpListener * v_listener);
static dn_r_UdpSocket dn_f_udp_bind(dn_r_SocketAddress v_address,bool v_dual_stack);
static dn_r_TlsSocket dn_f_tls_connect(dn_t_slice0_U8 v_url,uint32_t v_timeout_ms,dn_t_slice0_U8 v_ca_file,dn_t_slice0_U8 v_pin);
static dn_r_NetworkError dn_f_tcp_error(dn_r_TcpSocket const * v_socket);
static dn_r_NetworkError dn_f_listener_error(dn_r_TcpListener const * v_socket);
static dn_r_NetworkError dn_f_udp_error(dn_r_UdpSocket const * v_socket);
static dn_r_NetworkError dn_f_tls_error(dn_r_TlsSocket const * v_socket);
static uint8_t dn_f_tcp_close(dn_r_TcpSocket v_socket);
static uint8_t dn_f_listener_close(dn_r_TcpListener v_socket);
static uint8_t dn_f_udp_close(dn_r_UdpSocket v_socket);
static uint8_t dn_f_tls_close(dn_r_TlsSocket v_socket);
static dn_r_NetworkTransfer dn_f_tcp_read(dn_r_TcpSocket * v_socket,dn_t_buf_U8 * v_bytes);
static dn_r_NetworkTransfer dn_f_tcp_write(dn_r_TcpSocket * v_socket,dn_t_slice0_U8 v_bytes);
static dn_r_NetworkTransfer dn_f_tcp_read_exact(dn_r_TcpSocket * v_socket,dn_t_buf_U8 * v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetworkTransfer dn_f_tcp_write_all(dn_r_TcpSocket * v_socket,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetworkReady dn_f_tcp_wait(dn_r_TcpSocket * v_socket,uint32_t v_events,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetworkReady dn_f_listener_wait(dn_r_TcpListener * v_socket,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetworkReady dn_f_udp_wait(dn_r_UdpSocket * v_socket,uint32_t v_events,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetworkReady dn_f_tls_wait(dn_r_TlsSocket * v_socket,uint32_t v_events,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetworkError dn_f_tcp_shutdown_write(dn_r_TcpSocket * v_socket);
static dn_r_SocketAddressResult dn_f_tcp_local(dn_r_TcpSocket const * v_socket);
static dn_r_SocketAddressResult dn_f_listener_local(dn_r_TcpListener const * v_socket);
static dn_r_SocketAddressResult dn_f_udp_local(dn_r_UdpSocket const * v_socket);
static dn_r_SocketAddressResult dn_f_tcp_peer(dn_r_TcpSocket const * v_socket);
static dn_r_SocketAddressResult dn_f_tls_peer(dn_r_TlsSocket const * v_socket);
static dn_r_NetworkError dn_f_tcp_configure(dn_r_TcpSocket * v_socket,dn_r_SocketOptions v_options);
static dn_r_NetworkError dn_f_udp_configure(dn_r_UdpSocket * v_socket,uint32_t v_send_buffer,uint32_t v_receive_buffer);
static dn_r_SocketOptionsResult dn_f_tcp_options(dn_r_TcpSocket const * v_socket);
static dn_r_NetworkTransfer dn_f_udp_send(dn_r_UdpSocket * v_socket,dn_r_SocketAddress v_peer,dn_t_slice0_U8 v_bytes);
static dn_r_SocketDatagram dn_f_udp_receive(dn_r_UdpSocket * v_socket,dn_t_buf_U8 * v_bytes);
static dn_r_NetworkTransfer dn_f_tls_read(dn_r_TlsSocket * v_socket,dn_t_buf_U8 * v_bytes);
static dn_r_NetworkTransfer dn_f_tls_write(dn_r_TlsSocket * v_socket,dn_t_slice0_U8 v_bytes);
static dn_r_NetworkTransfer dn_f_tls_read_exact(dn_r_TlsSocket * v_socket,dn_t_buf_U8 * v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetworkTransfer dn_f_tls_write_all(dn_r_TlsSocket * v_socket,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static uint8_t dn_f_reactor_dispose(dn_r_NetOpened * v_state);
static void * dn_f_reactor_handle(dn_r_NetworkReactor const * v_reactor);
static dn_r_NetworkOperation dn_f_reactor_operation(dn_r_ReactorNativeRequest v_result);
static dn_r_ReactorCompletion dn_f_reactor_completion(dn_r_ReactorNativeCompletion v_result);
static dn_r_NetworkReactor dn_f_reactor_open(uint32_t v_channels,uint32_t v_operations,size_t v_byte_limit);
static dn_r_NetworkError dn_f_reactor_error(dn_r_NetworkReactor const * v_reactor);
static uint8_t dn_f_reactor_close(dn_r_NetworkReactor v_reactor);
static dn_r_ReactorTcpResult dn_f_reactor_adopt_tcp(dn_r_NetworkReactor * v_reactor,dn_r_TcpSocket v_socket);
static dn_r_ReactorListenerResult dn_f_reactor_adopt_listener(dn_r_NetworkReactor * v_reactor,dn_r_TcpListener v_socket);
static dn_r_ReactorUdpResult dn_f_reactor_adopt_udp(dn_r_NetworkReactor * v_reactor,dn_r_UdpSocket v_socket);
static dn_r_ReactorConnect dn_f_reactor_connect(dn_r_NetworkReactor * v_reactor,dn_r_SocketAddress v_address,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_f_reactor_read(dn_r_NetworkReactor * v_reactor,dn_r_ReactorTcp v_channel,size_t v_count,bool v_exact,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_f_reactor_write(dn_r_NetworkReactor * v_reactor,dn_r_ReactorTcp v_channel,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_f_reactor_accept(dn_r_NetworkReactor * v_reactor,dn_r_ReactorListener v_channel,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_f_reactor_receive(dn_r_NetworkReactor * v_reactor,dn_r_ReactorUdp v_channel,size_t v_capacity,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_f_reactor_send(dn_r_NetworkReactor * v_reactor,dn_r_ReactorUdp v_channel,dn_r_SocketAddress v_peer,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_f_reactor_timer(dn_r_NetworkReactor * v_reactor,uint32_t v_delay_ms);
static dn_r_NetworkOperation dn_f_reactor_event(dn_r_NetworkReactor * v_reactor,uint32_t v_timeout_ms);
static dn_r_NetworkTransfer dn_f_reactor_signal(dn_r_NetworkReactor * v_reactor,uint64_t v_operation);
static dn_r_NetworkTransfer dn_f_reactor_cancel(dn_r_NetworkReactor * v_reactor,uint64_t v_operation);
static dn_r_NetworkTransfer dn_f_reactor_cancel_all(dn_r_NetworkReactor * v_reactor);
static dn_r_NetworkTransfer dn_f_reactor_poll(dn_r_NetworkReactor * v_reactor,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_ReactorCompletion dn_f_reactor_result(dn_r_NetworkReactor const * v_reactor,uint64_t v_operation);
static dn_r_ReactorCompletion dn_f_reactor_next(dn_r_NetworkReactor const * v_reactor);
static dn_r_NetworkTransfer dn_f_reactor_copy(dn_r_NetworkReactor const * v_reactor,uint64_t v_operation,size_t v_offset,dn_t_buf_U8 * v_bytes);
static dn_r_NetworkTransfer dn_f_reactor_ack(dn_r_NetworkReactor * v_reactor,uint64_t v_operation);
static dn_r_NetworkTransfer dn_f_reactor_tcp_close(dn_r_NetworkReactor * v_reactor,dn_r_ReactorTcp v_channel);
static dn_r_NetworkTransfer dn_f_reactor_listener_close(dn_r_NetworkReactor * v_reactor,dn_r_ReactorListener v_channel);
static dn_r_NetworkTransfer dn_f_reactor_udp_close(dn_r_NetworkReactor * v_reactor,dn_r_ReactorUdp v_channel);
static dn_r_TcpSocket dn_f_reactor_release_tcp(dn_r_NetworkReactor * v_reactor,dn_r_ReactorTcp v_channel);
static dn_r_TcpListener dn_f_reactor_release_listener(dn_r_NetworkReactor * v_reactor,dn_r_ReactorListener v_channel);
static dn_r_UdpSocket dn_f_reactor_release_udp(dn_r_NetworkReactor * v_reactor,dn_r_ReactorUdp v_channel);
static dn_r_ReactorStatistics dn_f_reactor_statistics(dn_r_NetworkReactor const * v_reactor);
static uint8_t dn_f_scope_dispose(dn_r_NetOpened * v_state);
static uint8_t dn_f_scope_waker_dispose(dn_r_NetOpened * v_state);
static void * dn_f_scope_handle(dn_r_NetworkScope const * v_scope);
static dn_r_NetworkScope dn_f_scope_open(uint32_t v_channels,uint32_t v_operations,size_t v_byte_limit,uint32_t v_wake_limit);
static dn_r_NetworkError dn_f_scope_error(dn_r_NetworkScope const * v_scope);
static uint8_t dn_f_scope_close(dn_r_NetworkScope v_scope);
static dn_r_ReactorTcpResult dn_f_scope_adopt_tcp(dn_r_NetworkScope * v_scope,dn_r_TcpSocket v_socket);
static dn_r_ReactorListenerResult dn_f_scope_adopt_listener(dn_r_NetworkScope * v_scope,dn_r_TcpListener v_socket);
static dn_r_ReactorUdpResult dn_f_scope_adopt_udp(dn_r_NetworkScope * v_scope,dn_r_UdpSocket v_socket);
static dn_r_ReactorConnect dn_f_scope_connect(dn_r_NetworkScope * v_scope,dn_r_SocketAddress v_address,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_f_scope_read(dn_r_NetworkScope * v_scope,dn_r_ReactorTcp v_channel,size_t v_count,bool v_exact,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_f_scope_write(dn_r_NetworkScope * v_scope,dn_r_ReactorTcp v_channel,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_f_scope_accept(dn_r_NetworkScope * v_scope,dn_r_ReactorListener v_channel,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_f_scope_receive(dn_r_NetworkScope * v_scope,dn_r_ReactorUdp v_channel,size_t v_capacity,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_f_scope_send(dn_r_NetworkScope * v_scope,dn_r_ReactorUdp v_channel,dn_r_SocketAddress v_peer,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_f_scope_timer(dn_r_NetworkScope * v_scope,uint32_t v_delay_ms);
static dn_r_NetworkOperation dn_f_scope_event(dn_r_NetworkScope * v_scope,uint32_t v_timeout_ms);
static dn_r_NetworkTransfer dn_f_scope_poll(dn_r_NetworkScope * v_scope,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetworkTransfer dn_f_scope_join(dn_r_NetworkScope * v_scope,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_ReactorCompletion dn_f_scope_result(dn_r_NetworkScope const * v_scope,uint64_t v_operation);
static dn_r_ReactorCompletion dn_f_scope_next(dn_r_NetworkScope const * v_scope);
static dn_r_NetworkTransfer dn_f_scope_copy(dn_r_NetworkScope const * v_scope,uint64_t v_operation,size_t v_offset,dn_t_buf_U8 * v_bytes);
static dn_r_NetworkTransfer dn_f_scope_ack(dn_r_NetworkScope * v_scope,uint64_t v_operation);
static dn_r_NetworkTransfer dn_f_scope_cancel(dn_r_NetworkScope * v_scope,uint64_t v_operation);
static dn_r_NetworkTransfer dn_f_scope_cancel_all(dn_r_NetworkScope * v_scope);
static dn_r_NetworkTransfer dn_f_scope_tcp_close(dn_r_NetworkScope * v_scope,dn_r_ReactorTcp v_channel);
static dn_r_NetworkTransfer dn_f_scope_listener_close(dn_r_NetworkScope * v_scope,dn_r_ReactorListener v_channel);
static dn_r_NetworkTransfer dn_f_scope_udp_close(dn_r_NetworkScope * v_scope,dn_r_ReactorUdp v_channel);
static dn_r_EventWaker dn_f_scope_event_waker(dn_r_NetworkScope const * v_scope,uint64_t v_operation);
static dn_r_OperationCanceller dn_f_scope_operation_canceller(dn_r_NetworkScope const * v_scope,uint64_t v_operation);
static dn_r_ScopeCanceller dn_f_scope_canceller(dn_r_NetworkScope const * v_scope);
static dn_r_NetworkError dn_f_event_waker_error(dn_r_EventWaker const * v_waker);
static dn_r_NetworkError dn_f_operation_canceller_error(dn_r_OperationCanceller const * v_waker);
static dn_r_NetworkError dn_f_scope_canceller_error(dn_r_ScopeCanceller const * v_waker);
static dn_r_NetworkTransfer dn_f_event_wake(dn_r_EventWaker const * v_waker);
static dn_r_NetworkTransfer dn_f_operation_cancel(dn_r_OperationCanceller const * v_waker);
static dn_r_NetworkTransfer dn_f_scope_request_cancel(dn_r_ScopeCanceller const * v_waker);
static uint8_t dn_f_event_waker_close(dn_r_EventWaker v_waker);
static uint8_t dn_f_operation_canceller_close(dn_r_OperationCanceller v_waker);
static uint8_t dn_f_scope_canceller_close(dn_r_ScopeCanceller v_waker);
static dn_r_ScopeStatistics dn_f_scope_statistics(dn_r_NetworkScope const * v_scope);
static uint8_t dn_f_main(void);
static void dn_drop_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * volatile *x); static void dn_retain_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * volatile *x);
static void dn_final_resource_r9_NetOpened_net_dispose(void *raw){dn_t_resource_r9_NetOpened_net_dispose *p=raw;if(p->active){p->active=false;(void)dn_f_net_dispose(&p->value);}}
static dn_t_resource_r9_NetOpened_net_dispose * dn_begin_resource_r9_NetOpened_net_dispose(void){dn_t_resource_r9_NetOpened_net_dispose *p=dn_allocate_aligned(sizeof *p,_Alignof(dn_t_resource_r9_NetOpened_net_dispose));((dn_mem*)p-1)->finalize=dn_final_resource_r9_NetOpened_net_dispose;return p;}
static dn_t_resource_r9_NetOpened_net_dispose * dn_commit_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * p,dn_r_NetOpened v){p->value=v;p->active=true;return p;}
static void dn_drop_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * volatile *x){if(*x){dn_t_resource_r9_NetOpened_net_dispose *p=*x;*x=NULL;((dn_mem*)p-1)->finalize=NULL;dn_final_resource_r9_NetOpened_net_dispose(p);dn_deallocate(p);}}
static void dn_retain_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * volatile *x){if(*x)dn_detach(*x);}
static dn_r_NetOpened dn_take_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * p){dn_nonnull(p);dn_r_NetOpened v=p->value;p->active=false;dn_deallocate(p);return v;}
static void dn_adopt_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * volatile*x){dn_adopt(*x);}
static void dn_drop_resource_r9_NetOpened_reactor_dispose(dn_t_resource_r9_NetOpened_reactor_dispose * volatile *x); static void dn_retain_resource_r9_NetOpened_reactor_dispose(dn_t_resource_r9_NetOpened_reactor_dispose * volatile *x);
static void dn_final_resource_r9_NetOpened_reactor_dispose(void *raw){dn_t_resource_r9_NetOpened_reactor_dispose *p=raw;if(p->active){p->active=false;(void)dn_f_reactor_dispose(&p->value);}}
static dn_t_resource_r9_NetOpened_reactor_dispose * dn_begin_resource_r9_NetOpened_reactor_dispose(void){dn_t_resource_r9_NetOpened_reactor_dispose *p=dn_allocate_aligned(sizeof *p,_Alignof(dn_t_resource_r9_NetOpened_reactor_dispose));((dn_mem*)p-1)->finalize=dn_final_resource_r9_NetOpened_reactor_dispose;return p;}
static dn_t_resource_r9_NetOpened_reactor_dispose * dn_commit_resource_r9_NetOpened_reactor_dispose(dn_t_resource_r9_NetOpened_reactor_dispose * p,dn_r_NetOpened v){p->value=v;p->active=true;return p;}
static void dn_drop_resource_r9_NetOpened_reactor_dispose(dn_t_resource_r9_NetOpened_reactor_dispose * volatile *x){if(*x){dn_t_resource_r9_NetOpened_reactor_dispose *p=*x;*x=NULL;((dn_mem*)p-1)->finalize=NULL;dn_final_resource_r9_NetOpened_reactor_dispose(p);dn_deallocate(p);}}
static void dn_retain_resource_r9_NetOpened_reactor_dispose(dn_t_resource_r9_NetOpened_reactor_dispose * volatile *x){if(*x)dn_detach(*x);}
static dn_r_NetOpened dn_take_resource_r9_NetOpened_reactor_dispose(dn_t_resource_r9_NetOpened_reactor_dispose * p){dn_nonnull(p);dn_r_NetOpened v=p->value;p->active=false;dn_deallocate(p);return v;}
static void dn_adopt_resource_r9_NetOpened_reactor_dispose(dn_t_resource_r9_NetOpened_reactor_dispose * volatile*x){dn_adopt(*x);}
static void dn_drop_resource_r9_NetOpened_scope_dispose(dn_t_resource_r9_NetOpened_scope_dispose * volatile *x); static void dn_retain_resource_r9_NetOpened_scope_dispose(dn_t_resource_r9_NetOpened_scope_dispose * volatile *x);
static void dn_final_resource_r9_NetOpened_scope_dispose(void *raw){dn_t_resource_r9_NetOpened_scope_dispose *p=raw;if(p->active){p->active=false;(void)dn_f_scope_dispose(&p->value);}}
static dn_t_resource_r9_NetOpened_scope_dispose * dn_begin_resource_r9_NetOpened_scope_dispose(void){dn_t_resource_r9_NetOpened_scope_dispose *p=dn_allocate_aligned(sizeof *p,_Alignof(dn_t_resource_r9_NetOpened_scope_dispose));((dn_mem*)p-1)->finalize=dn_final_resource_r9_NetOpened_scope_dispose;return p;}
static dn_t_resource_r9_NetOpened_scope_dispose * dn_commit_resource_r9_NetOpened_scope_dispose(dn_t_resource_r9_NetOpened_scope_dispose * p,dn_r_NetOpened v){p->value=v;p->active=true;return p;}
static void dn_drop_resource_r9_NetOpened_scope_dispose(dn_t_resource_r9_NetOpened_scope_dispose * volatile *x){if(*x){dn_t_resource_r9_NetOpened_scope_dispose *p=*x;*x=NULL;((dn_mem*)p-1)->finalize=NULL;dn_final_resource_r9_NetOpened_scope_dispose(p);dn_deallocate(p);}}
static void dn_retain_resource_r9_NetOpened_scope_dispose(dn_t_resource_r9_NetOpened_scope_dispose * volatile *x){if(*x)dn_detach(*x);}
static dn_r_NetOpened dn_take_resource_r9_NetOpened_scope_dispose(dn_t_resource_r9_NetOpened_scope_dispose * p){dn_nonnull(p);dn_r_NetOpened v=p->value;p->active=false;dn_deallocate(p);return v;}
static void dn_adopt_resource_r9_NetOpened_scope_dispose(dn_t_resource_r9_NetOpened_scope_dispose * volatile*x){dn_adopt(*x);}
static void dn_drop_resource_r9_NetOpened_scope_waker_dispose(dn_t_resource_r9_NetOpened_scope_waker_dispose * volatile *x); static void dn_retain_resource_r9_NetOpened_scope_waker_dispose(dn_t_resource_r9_NetOpened_scope_waker_dispose * volatile *x);
static void dn_final_resource_r9_NetOpened_scope_waker_dispose(void *raw){dn_t_resource_r9_NetOpened_scope_waker_dispose *p=raw;if(p->active){p->active=false;(void)dn_f_scope_waker_dispose(&p->value);}}
static dn_t_resource_r9_NetOpened_scope_waker_dispose * dn_begin_resource_r9_NetOpened_scope_waker_dispose(void){dn_t_resource_r9_NetOpened_scope_waker_dispose *p=dn_allocate_aligned(sizeof *p,_Alignof(dn_t_resource_r9_NetOpened_scope_waker_dispose));((dn_mem*)p-1)->finalize=dn_final_resource_r9_NetOpened_scope_waker_dispose;return p;}
static dn_t_resource_r9_NetOpened_scope_waker_dispose * dn_commit_resource_r9_NetOpened_scope_waker_dispose(dn_t_resource_r9_NetOpened_scope_waker_dispose * p,dn_r_NetOpened v){p->value=v;p->active=true;return p;}
static void dn_drop_resource_r9_NetOpened_scope_waker_dispose(dn_t_resource_r9_NetOpened_scope_waker_dispose * volatile *x){if(*x){dn_t_resource_r9_NetOpened_scope_waker_dispose *p=*x;*x=NULL;((dn_mem*)p-1)->finalize=NULL;dn_final_resource_r9_NetOpened_scope_waker_dispose(p);dn_deallocate(p);}}
static void dn_retain_resource_r9_NetOpened_scope_waker_dispose(dn_t_resource_r9_NetOpened_scope_waker_dispose * volatile *x){if(*x)dn_detach(*x);}
static dn_r_NetOpened dn_take_resource_r9_NetOpened_scope_waker_dispose(dn_t_resource_r9_NetOpened_scope_waker_dispose * p){dn_nonnull(p);dn_r_NetOpened v=p->value;p->active=false;dn_deallocate(p);return v;}
static void dn_adopt_resource_r9_NetOpened_scope_waker_dispose(dn_t_resource_r9_NetOpened_scope_waker_dispose * volatile*x){dn_adopt(*x);}
static void dn_drop_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * volatile *x); static void dn_retain_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * volatile *x);
static void dn_final_resource_h_net_cancel_dispose(void *raw){dn_t_resource_h_net_cancel_dispose *p=raw;if(p->active){p->active=false;(void)dn_f_net_cancel_dispose(&p->value);}}
static dn_t_resource_h_net_cancel_dispose * dn_begin_resource_h_net_cancel_dispose(void){dn_t_resource_h_net_cancel_dispose *p=dn_allocate_aligned(sizeof *p,_Alignof(dn_t_resource_h_net_cancel_dispose));((dn_mem*)p-1)->finalize=dn_final_resource_h_net_cancel_dispose;return p;}
static dn_t_resource_h_net_cancel_dispose * dn_commit_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * p,void * v){p->value=v;p->active=true;return p;}
static void dn_drop_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * volatile *x){if(*x){dn_t_resource_h_net_cancel_dispose *p=*x;*x=NULL;((dn_mem*)p-1)->finalize=NULL;dn_final_resource_h_net_cancel_dispose(p);dn_deallocate(p);}}
static void dn_retain_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * volatile *x){if(*x)dn_detach(*x);}
static void * dn_take_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * p){dn_nonnull(p);void * v=p->value;p->active=false;dn_deallocate(p);return v;}
static void dn_adopt_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * volatile*x){dn_adopt(*x);}
static void dn_drop_r10_EventWaker(dn_r_EventWaker volatile *x); static void dn_retain_r10_EventWaker(dn_r_EventWaker volatile *x);
static void dn_drop_r10_EventWaker(dn_r_EventWaker volatile *x){dn_drop_resource_r9_NetOpened_scope_waker_dispose(&x->f_inner);}
static void dn_retain_r10_EventWaker(dn_r_EventWaker volatile *x){dn_retain_resource_r9_NetOpened_scope_waker_dispose(&x->f_inner);}
static void dn_adopt_r10_EventWaker(dn_r_EventWaker volatile*x){dn_adopt_resource_r9_NetOpened_scope_waker_dispose(&x->f_inner);}
static void dn_drop_r14_NetworkReactor(dn_r_NetworkReactor volatile *x); static void dn_retain_r14_NetworkReactor(dn_r_NetworkReactor volatile *x);
static void dn_drop_r14_NetworkReactor(dn_r_NetworkReactor volatile *x){dn_drop_resource_r9_NetOpened_reactor_dispose(&x->f_inner);}
static void dn_retain_r14_NetworkReactor(dn_r_NetworkReactor volatile *x){dn_retain_resource_r9_NetOpened_reactor_dispose(&x->f_inner);}
static void dn_adopt_r14_NetworkReactor(dn_r_NetworkReactor volatile*x){dn_adopt_resource_r9_NetOpened_reactor_dispose(&x->f_inner);}
static void dn_drop_r12_NetworkScope(dn_r_NetworkScope volatile *x); static void dn_retain_r12_NetworkScope(dn_r_NetworkScope volatile *x);
static void dn_drop_r12_NetworkScope(dn_r_NetworkScope volatile *x){dn_drop_resource_r9_NetOpened_scope_dispose(&x->f_inner);}
static void dn_retain_r12_NetworkScope(dn_r_NetworkScope volatile *x){dn_retain_resource_r9_NetOpened_scope_dispose(&x->f_inner);}
static void dn_adopt_r12_NetworkScope(dn_r_NetworkScope volatile*x){dn_adopt_resource_r9_NetOpened_scope_dispose(&x->f_inner);}
static void dn_drop_r18_OperationCanceller(dn_r_OperationCanceller volatile *x); static void dn_retain_r18_OperationCanceller(dn_r_OperationCanceller volatile *x);
static void dn_drop_r18_OperationCanceller(dn_r_OperationCanceller volatile *x){dn_drop_resource_r9_NetOpened_scope_waker_dispose(&x->f_inner);}
static void dn_retain_r18_OperationCanceller(dn_r_OperationCanceller volatile *x){dn_retain_resource_r9_NetOpened_scope_waker_dispose(&x->f_inner);}
static void dn_adopt_r18_OperationCanceller(dn_r_OperationCanceller volatile*x){dn_adopt_resource_r9_NetOpened_scope_waker_dispose(&x->f_inner);}
static void dn_drop_r14_ScopeCanceller(dn_r_ScopeCanceller volatile *x); static void dn_retain_r14_ScopeCanceller(dn_r_ScopeCanceller volatile *x);
static void dn_drop_r14_ScopeCanceller(dn_r_ScopeCanceller volatile *x){dn_drop_resource_r9_NetOpened_scope_waker_dispose(&x->f_inner);}
static void dn_retain_r14_ScopeCanceller(dn_r_ScopeCanceller volatile *x){dn_retain_resource_r9_NetOpened_scope_waker_dispose(&x->f_inner);}
static void dn_adopt_r14_ScopeCanceller(dn_r_ScopeCanceller volatile*x){dn_adopt_resource_r9_NetOpened_scope_waker_dispose(&x->f_inner);}
static void dn_drop_r11_TcpListener(dn_r_TcpListener volatile *x); static void dn_retain_r11_TcpListener(dn_r_TcpListener volatile *x);
static void dn_drop_r11_TcpListener(dn_r_TcpListener volatile *x){dn_drop_resource_r9_NetOpened_net_dispose(&x->f_inner);}
static void dn_retain_r11_TcpListener(dn_r_TcpListener volatile *x){dn_retain_resource_r9_NetOpened_net_dispose(&x->f_inner);}
static void dn_adopt_r11_TcpListener(dn_r_TcpListener volatile*x){dn_adopt_resource_r9_NetOpened_net_dispose(&x->f_inner);}
static void dn_drop_r9_TcpSocket(dn_r_TcpSocket volatile *x); static void dn_retain_r9_TcpSocket(dn_r_TcpSocket volatile *x);
static void dn_drop_r9_TcpSocket(dn_r_TcpSocket volatile *x){dn_drop_resource_r9_NetOpened_net_dispose(&x->f_inner);}
static void dn_retain_r9_TcpSocket(dn_r_TcpSocket volatile *x){dn_retain_resource_r9_NetOpened_net_dispose(&x->f_inner);}
static void dn_adopt_r9_TcpSocket(dn_r_TcpSocket volatile*x){dn_adopt_resource_r9_NetOpened_net_dispose(&x->f_inner);}
static void dn_drop_r9_TlsSocket(dn_r_TlsSocket volatile *x); static void dn_retain_r9_TlsSocket(dn_r_TlsSocket volatile *x);
static void dn_drop_r9_TlsSocket(dn_r_TlsSocket volatile *x){dn_drop_resource_r9_NetOpened_net_dispose(&x->f_inner);}
static void dn_retain_r9_TlsSocket(dn_r_TlsSocket volatile *x){dn_retain_resource_r9_NetOpened_net_dispose(&x->f_inner);}
static void dn_adopt_r9_TlsSocket(dn_r_TlsSocket volatile*x){dn_adopt_resource_r9_NetOpened_net_dispose(&x->f_inner);}
static void dn_drop_r9_UdpSocket(dn_r_UdpSocket volatile *x); static void dn_retain_r9_UdpSocket(dn_r_UdpSocket volatile *x);
static void dn_drop_r9_UdpSocket(dn_r_UdpSocket volatile *x){dn_drop_resource_r9_NetOpened_net_dispose(&x->f_inner);}
static void dn_retain_r9_UdpSocket(dn_r_UdpSocket volatile *x){dn_retain_resource_r9_NetOpened_net_dispose(&x->f_inner);}
static void dn_adopt_r9_UdpSocket(dn_r_UdpSocket volatile*x){dn_adopt_resource_r9_NetOpened_net_dispose(&x->f_inner);}
static void dn_drop_buf_U8(dn_t_buf_U8 volatile *x); static void dn_retain_buf_U8(dn_t_buf_U8 volatile *x);
static void dn_drop_buf_U8(dn_t_buf_U8 volatile *x){dn_deallocate(x->data);*x=(dn_t_buf_U8){0};}
static void dn_retain_buf_U8(dn_t_buf_U8 volatile *x){dn_detach(x->data);}
static dn_t_buf_U8 dn_new_buf_U8(size_t n){dn_t_buf_U8 x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(uint8_t)),_Alignof(uint8_t));x.len=x.cap=n;return x;}
static dn_t_buf_U8 dn_aligned_buf_U8(size_t n,size_t a){if(a<_Alignof(uint8_t)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_U8 x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(uint8_t)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_U8(dn_t_buf_U8 *x,uint8_t v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;uint8_t *p=dn_allocate_aligned(dn_size(cap,sizeof(uint8_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(uint8_t));if(x->len)memcpy(p,x->data,x->len*sizeof(uint8_t));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;x->len++;return 0;}
static uint8_t dn_resize_buf_U8(dn_t_buf_U8 *x,size_t n){if(n<x->len){}if(n>x->cap){size_t cap=n;uint8_t *p=dn_allocate_aligned(dn_size(cap,sizeof(uint8_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(uint8_t));if(x->len)memcpy(p,x->data,x->len*sizeof(uint8_t));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(uint8_t));x->len=n;return 0;}
static void dn_adopt_buf_U8(dn_t_buf_U8 volatile*x){dn_adopt(x->data);}
static void dn_drop_buf_r7_NetPoll(dn_t_buf_r7_NetPoll volatile *x); static void dn_retain_buf_r7_NetPoll(dn_t_buf_r7_NetPoll volatile *x);
static void dn_drop_buf_r7_NetPoll(dn_t_buf_r7_NetPoll volatile *x){dn_deallocate(x->data);*x=(dn_t_buf_r7_NetPoll){0};}
static void dn_retain_buf_r7_NetPoll(dn_t_buf_r7_NetPoll volatile *x){dn_detach(x->data);}
static dn_t_buf_r7_NetPoll dn_new_buf_r7_NetPoll(size_t n){dn_t_buf_r7_NetPoll x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(dn_r_NetPoll)),_Alignof(dn_r_NetPoll));x.len=x.cap=n;return x;}
static dn_t_buf_r7_NetPoll dn_aligned_buf_r7_NetPoll(size_t n,size_t a){if(a<_Alignof(dn_r_NetPoll)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_r7_NetPoll x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(dn_r_NetPoll)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_r7_NetPoll(dn_t_buf_r7_NetPoll *x,dn_r_NetPoll v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;dn_r_NetPoll *p=dn_allocate_aligned(dn_size(cap,sizeof(dn_r_NetPoll)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(dn_r_NetPoll));if(x->len)memcpy(p,x->data,x->len*sizeof(dn_r_NetPoll));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;x->len++;return 0;}
static uint8_t dn_resize_buf_r7_NetPoll(dn_t_buf_r7_NetPoll *x,size_t n){if(n<x->len){}if(n>x->cap){size_t cap=n;dn_r_NetPoll *p=dn_allocate_aligned(dn_size(cap,sizeof(dn_r_NetPoll)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(dn_r_NetPoll));if(x->len)memcpy(p,x->data,x->len*sizeof(dn_r_NetPoll));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(dn_r_NetPoll));x->len=n;return 0;}
static void dn_adopt_buf_r7_NetPoll(dn_t_buf_r7_NetPoll volatile*x){dn_adopt(x->data);}
extern uint32_t dn_ext_dlm_net_capabilities(void) __asm__("dlm_net_capabilities");
extern uint32_t dn_ext_dlm_net_curl_version(void) __asm__("dlm_net_curl_version");
extern dn_r_NetIo dn_ext_dlm_net_provider_shutdown(void) __asm__("dlm_net_provider_shutdown");
extern uint64_t dn_ext_dlm_net_clock_ms(void) __asm__("dlm_net_clock_ms");
extern dn_r_NetOpened dn_ext_dlm_net_tcp(dn_r_NetText v_host,uint16_t v_port,uint32_t v_timeout_ms) __asm__("dlm_net_tcp");
extern dn_r_NetOpened dn_ext_dlm_net_listen(dn_r_NetText v_host,uint16_t v_port,uint32_t v_backlog) __asm__("dlm_net_listen");
extern dn_r_NetOpened dn_ext_dlm_net_udp(dn_r_NetText v_host,uint16_t v_port) __asm__("dlm_net_udp");
extern dn_r_NetOpened dn_ext_dlm_net_accept(void * v_socket) __asm__("dlm_net_accept");
extern void dn_ext_dlm_net_close(void * v_socket) __asm__("dlm_net_close");
extern dn_r_NetResolved dn_ext_dlm_net_local(void * v_socket) __asm__("dlm_net_local");
extern dn_r_NetResolved dn_ext_dlm_net_resolve(dn_r_NetText v_host,uint16_t v_port,uint32_t v_index) __asm__("dlm_net_resolve");
extern dn_r_NetIo dn_ext_dlm_net_nodelay(void * v_socket,bool v_enabled) __asm__("dlm_net_nodelay");
extern dn_r_NetIo dn_ext_dlm_net_send(void * v_socket,uint8_t * v_bytes,size_t v_count) __asm__("dlm_net_send");
extern dn_r_NetIo dn_ext_dlm_net_recv(void * v_socket,uint8_t * v_bytes,size_t v_capacity) __asm__("dlm_net_recv");
extern dn_r_NetIo dn_ext_dlm_net_transfer(void * v_socket,uint8_t * v_bytes,size_t v_count,uint32_t v_timeout_ms,void * v_cancel,bool v_sending) __asm__("dlm_net_transfer");
extern dn_r_NetIo dn_ext_dlm_net_shutdown(void * v_socket) __asm__("dlm_net_shutdown");
extern dn_r_NetIo dn_ext_dlm_net_udp_send(void * v_socket,dn_r_NetAddress v_peer,uint8_t * v_bytes,size_t v_count) __asm__("dlm_net_udp_send");
extern dn_r_NetDatagram dn_ext_dlm_net_udp_recv(void * v_socket,uint8_t * v_bytes,size_t v_capacity) __asm__("dlm_net_udp_recv");
extern dn_r_NetIo dn_ext_dlm_net_wait(void * v_socket,uint32_t v_events,uint32_t v_timeout_ms,void * v_cancel) __asm__("dlm_net_wait");
extern dn_r_NetIo dn_ext_dlm_net_poll(dn_r_NetPoll * v_items,size_t v_count,uint32_t v_timeout_ms,void * v_cancel) __asm__("dlm_net_poll");
extern void * dn_ext_dlm_net_cancel_new(void) __asm__("dlm_net_cancel_new");
extern void dn_ext_dlm_net_cancel_close(void * v_cancel) __asm__("dlm_net_cancel_close");
extern void dn_ext_dlm_net_cancel_signal(void * v_cancel) __asm__("dlm_net_cancel_signal");
extern void dn_ext_dlm_net_cancel_reset(void * v_cancel) __asm__("dlm_net_cancel_reset");
extern dn_r_NetOpened dn_ext_dlm_net_tls(dn_r_NetText v_url,uint32_t v_timeout_ms,dn_r_NetText v_ca_file,dn_r_NetText v_pin) __asm__("dlm_net_tls");
extern dn_r_NetOpened dn_ext_dlm_net_http(dn_r_NetText v_url,uint32_t v_timeout_ms,size_t v_maximum,dn_r_NetText v_ca_file,dn_r_NetText v_pin) __asm__("dlm_net_http");
extern dn_r_NetIo dn_ext_dlm_net_http_version(void * v_request,uint32_t v_version) __asm__("dlm_net_http_version");
extern dn_r_NetIo dn_ext_dlm_net_http_header(void * v_request,dn_r_NetText v_header) __asm__("dlm_net_http_header");
extern dn_r_NetIo dn_ext_dlm_net_http_body(void * v_request,uint8_t * v_bytes,size_t v_count) __asm__("dlm_net_http_body");
extern dn_r_NetIo dn_ext_dlm_net_http_method(void * v_request,dn_r_NetText v_method) __asm__("dlm_net_http_method");
extern dn_r_NetIo dn_ext_dlm_net_http_ech(void * v_request,dn_r_NetText v_config) __asm__("dlm_net_http_ech");
extern dn_r_NetIo dn_ext_dlm_net_http_perform(void * v_request,void * v_cancel) __asm__("dlm_net_http_perform");
extern dn_r_NetIo dn_ext_dlm_net_http_info(void * v_request,uint32_t v_field) __asm__("dlm_net_http_info");
extern dn_r_NetIo dn_ext_dlm_net_http_read(void * v_request,bool v_headers,size_t v_offset,uint8_t * v_bytes,size_t v_capacity) __asm__("dlm_net_http_read");
extern dn_r_NetOpened dn_ext_dlm_net_websocket(dn_r_NetText v_url,uint32_t v_timeout_ms,dn_r_NetText v_ca_file,dn_r_NetText v_pin) __asm__("dlm_net_websocket");
extern dn_r_NetIo dn_ext_dlm_net_ws_start(void * v_socket) __asm__("dlm_net_ws_start");
extern dn_r_NetIo dn_ext_dlm_net_ws_send(void * v_socket,uint8_t * v_bytes,size_t v_count,uint32_t v_flags,uint32_t v_timeout_ms,void * v_cancel) __asm__("dlm_net_ws_send");
extern dn_r_NetWebSocketIo dn_ext_dlm_net_ws_recv(void * v_socket,uint8_t * v_bytes,size_t v_capacity) __asm__("dlm_net_ws_recv");
extern dn_r_NetIo dn_ext_dlm_net_random(uint8_t * v_bytes,size_t v_count) __asm__("dlm_net_random");
static void dn_cb_net_dispose(dn_r_NetOpened * v_state);
static void dn_cb_net_cancel_dispose(void * * v_state);
static uint8_t * dn_cb_net_input(dn_t_slice0_U8 v_bytes);
static uint8_t * dn_cb_net_output(dn_t_buf_U8 * v_bytes);
static dn_r_NetText dn_cb_net_text(dn_utf8 v_text);
static dn_r_NetText dn_cb_net_bytes(dn_t_slice0_U8 v_bytes);
static dn_r_NetResolved dn_cb_network_resolve_bytes(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_index);
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_socket_tcp_bytes(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_timeout_ms);
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_socket_listen_bytes(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_backlog);
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_socket_udp_bytes(dn_t_slice0_U8 v_host,uint16_t v_port);
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_socket_tls_bytes(dn_t_slice0_U8 v_url,uint32_t v_timeout_ms,dn_t_slice0_U8 v_ca_file,dn_t_slice0_U8 v_pin);
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_http_client_bytes(dn_t_slice0_U8 v_url,uint32_t v_timeout_ms,size_t v_maximum,dn_t_slice0_U8 v_ca_file,dn_t_slice0_U8 v_pin);
static dn_r_NetIo dn_cb_http_header_bytes(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_slice0_U8 v_header);
static dn_r_NetIo dn_cb_http_method_bytes(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_slice0_U8 v_method);
static dn_r_NetIo dn_cb_http_ech_bytes(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_slice0_U8 v_config);
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_websocket_client_bytes(dn_t_slice0_U8 v_url,uint32_t v_timeout_ms,dn_t_slice0_U8 v_ca_file,dn_t_slice0_U8 v_pin);
static uint32_t dn_cb_network_capabilities(void);
static uint32_t dn_cb_network_provider_version(void);
static dn_r_NetIo dn_cb_network_provider_shutdown(void);
static uint64_t dn_cb_network_clock_ms(void);
static dn_r_NetResolved dn_cb_network_resolve(dn_utf8 v_host,uint16_t v_port,uint32_t v_index);
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_socket_tcp(dn_utf8 v_host,uint16_t v_port,uint32_t v_timeout_ms);
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_socket_listen(dn_utf8 v_host,uint16_t v_port,uint32_t v_backlog);
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_socket_udp(dn_utf8 v_host,uint16_t v_port);
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_socket_accept(dn_t_resource_r9_NetOpened_net_dispose * * v_listener);
static int32_t dn_cb_socket_status(dn_t_resource_r9_NetOpened_net_dispose * const * v_socket);
static int32_t dn_cb_socket_system_error(dn_t_resource_r9_NetOpened_net_dispose * const * v_socket);
static dn_r_NetResolved dn_cb_socket_local(dn_t_resource_r9_NetOpened_net_dispose * const * v_socket);
static dn_r_NetIo dn_cb_socket_nodelay(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,bool v_enabled);
static dn_r_NetIo dn_cb_socket_write(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_slice0_U8 v_bytes);
static dn_r_NetIo dn_cb_socket_read(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_buf_U8 * v_bytes);
static dn_r_NetIo dn_cb_socket_write_all(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetIo dn_cb_socket_read_exact(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_buf_U8 * v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetIo dn_cb_socket_shutdown_write(dn_t_resource_r9_NetOpened_net_dispose * * v_socket);
static dn_r_NetIo dn_cb_socket_send_datagram(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_r_NetAddress v_peer,dn_t_slice0_U8 v_bytes);
static dn_r_NetDatagram dn_cb_socket_read_datagram(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_buf_U8 * v_bytes);
static dn_r_NetIo dn_cb_socket_wait(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,uint32_t v_events,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetPoll dn_cb_socket_poll_entry(dn_t_resource_r9_NetOpened_net_dispose * const * v_socket,uint32_t v_events);
static dn_r_NetIo dn_cb_socket_poll(dn_t_buf_r7_NetPoll * v_items,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_t_resource_h_net_cancel_dispose * dn_cb_network_cancel(void);
static bool dn_cb_network_cancel_valid(dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static void dn_cb_network_cancel_signal(dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static void dn_cb_network_cancel_reset(dn_t_resource_h_net_cancel_dispose * * v_cancel);
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_socket_tls(dn_utf8 v_url,uint32_t v_timeout_ms,dn_utf8 v_ca_file,dn_utf8 v_pin);
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_http_client(dn_utf8 v_url,uint32_t v_timeout_ms,size_t v_maximum,dn_utf8 v_ca_file,dn_utf8 v_pin);
static dn_r_NetIo dn_cb_http_version(dn_t_resource_r9_NetOpened_net_dispose * * v_request,uint32_t v_version);
static dn_r_NetIo dn_cb_http_header(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_utf8 v_header);
static dn_r_NetIo dn_cb_http_method(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_utf8 v_method);
static dn_r_NetIo dn_cb_http_body(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_slice0_U8 v_bytes);
static dn_r_NetIo dn_cb_http_ech(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_utf8 v_config);
static dn_r_NetIo dn_cb_http_perform(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetIo dn_cb_http_info(dn_t_resource_r9_NetOpened_net_dispose * const * v_request,uint32_t v_field);
static dn_r_NetIo dn_cb_http_read(dn_t_resource_r9_NetOpened_net_dispose * const * v_request,bool v_headers,size_t v_offset,dn_t_buf_U8 * v_bytes);
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_websocket_client(dn_utf8 v_url,uint32_t v_timeout_ms,dn_utf8 v_ca_file,dn_utf8 v_pin);
static dn_r_NetIo dn_cb_websocket_start(dn_t_resource_r9_NetOpened_net_dispose * * v_socket);
static dn_r_NetIo dn_cb_websocket_write(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_slice0_U8 v_bytes,uint32_t v_flags,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetWebSocketIo dn_cb_websocket_read(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_buf_U8 * v_bytes);
static dn_r_NetIo dn_cb_network_random(dn_t_buf_U8 * v_bytes);
static bool dn_cb_network_ok(dn_r_NetworkError const * v_error);
static bool dn_cb_network_would_block(dn_r_NetworkError const * v_error);
static bool dn_cb_network_eof(dn_r_NetworkError const * v_error);
static bool dn_cb_network_timed_out(dn_r_NetworkError const * v_error);
static bool dn_cb_network_was_cancelled(dn_r_NetworkError const * v_error);
static bool dn_cb_network_truncated(dn_r_NetworkError const * v_error);
static dn_utf8 dn_cb_network_error_name(dn_r_NetworkError const * v_error);
extern dn_r_NetOpened dn_ext_dlm_net_connect_address(dn_r_NetAddress v_address,uint32_t v_timeout_ms) __asm__("dlm_net_connect_address");
extern dn_r_NetOpened dn_ext_dlm_net_bind_address(dn_r_NetAddress v_address,uint32_t v_kind,uint32_t v_backlog,bool v_dual_stack) __asm__("dlm_net_bind_address");
extern dn_r_NetResolved dn_ext_dlm_net_resolve_family(dn_r_NetText v_host,uint16_t v_port,uint32_t v_family,uint32_t v_index) __asm__("dlm_net_resolve_family");
extern dn_r_NetResolved dn_ext_dlm_net_peer(void * v_socket) __asm__("dlm_net_peer");
extern dn_r_NetIo dn_ext_dlm_net_configure(void * v_socket,dn_r_SocketOptions v_options) __asm__("dlm_net_configure");
extern dn_r_NativeSocketOptions dn_ext_dlm_net_get_options(void * v_socket) __asm__("dlm_net_get_options");
extern dn_r_NetIo dn_ext_dlm_net_address_text(dn_r_NetAddress v_address,uint8_t * v_bytes,size_t v_capacity,bool v_with_port) __asm__("dlm_net_address_text");
static dn_r_NetworkTransfer dn_cb_socket_transfer(dn_r_NetIo v_result,uint32_t v_operation);
static dn_r_NetworkReady dn_cb_socket_ready(dn_r_NetIo v_result);
static dn_r_SocketAddressResult dn_cb_socket_address_result(dn_r_NetResolved v_result,uint32_t v_operation);
static dn_r_SocketAddress dn_cb_socket_address_v4(uint8_t v_a,uint8_t v_b,uint8_t v_c,uint8_t v_d,uint16_t v_port);
static dn_r_SocketAddress dn_cb_socket_address_v6(dn_t_arr16_U8 v_bytes,uint16_t v_port,uint32_t v_scope);
static dn_r_SocketAddressResult dn_cb_socket_address_resolve(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_family,uint32_t v_index);
static dn_r_NetworkTransfer dn_cb_socket_address_write(dn_r_SocketAddress v_address,dn_t_buf_U8 * v_bytes,bool v_with_port);
static dn_r_SocketOptions dn_cb_socket_options(bool v_no_delay,bool v_keep_alive,uint32_t v_send_buffer,uint32_t v_receive_buffer);
static dn_r_TcpSocket dn_cb_tcp_connect(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_timeout_ms);
static dn_r_TcpSocket dn_cb_tcp_connect_address(dn_r_SocketAddress v_address,uint32_t v_timeout_ms);
static dn_r_TcpListener dn_cb_tcp_listen(dn_r_SocketAddress v_address,uint32_t v_backlog,bool v_dual_stack);
static dn_r_TcpSocket dn_cb_tcp_accept(dn_r_TcpListener * v_listener);
static dn_r_UdpSocket dn_cb_udp_bind(dn_r_SocketAddress v_address,bool v_dual_stack);
static dn_r_TlsSocket dn_cb_tls_connect(dn_t_slice0_U8 v_url,uint32_t v_timeout_ms,dn_t_slice0_U8 v_ca_file,dn_t_slice0_U8 v_pin);
static dn_r_NetworkError dn_cb_tcp_error(dn_r_TcpSocket const * v_socket);
static dn_r_NetworkError dn_cb_listener_error(dn_r_TcpListener const * v_socket);
static dn_r_NetworkError dn_cb_udp_error(dn_r_UdpSocket const * v_socket);
static dn_r_NetworkError dn_cb_tls_error(dn_r_TlsSocket const * v_socket);
static void dn_cb_tcp_close(dn_r_TcpSocket v_socket);
static void dn_cb_listener_close(dn_r_TcpListener v_socket);
static void dn_cb_udp_close(dn_r_UdpSocket v_socket);
static void dn_cb_tls_close(dn_r_TlsSocket v_socket);
static dn_r_NetworkTransfer dn_cb_tcp_read(dn_r_TcpSocket * v_socket,dn_t_buf_U8 * v_bytes);
static dn_r_NetworkTransfer dn_cb_tcp_write(dn_r_TcpSocket * v_socket,dn_t_slice0_U8 v_bytes);
static dn_r_NetworkTransfer dn_cb_tcp_read_exact(dn_r_TcpSocket * v_socket,dn_t_buf_U8 * v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetworkTransfer dn_cb_tcp_write_all(dn_r_TcpSocket * v_socket,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetworkReady dn_cb_tcp_wait(dn_r_TcpSocket * v_socket,uint32_t v_events,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetworkReady dn_cb_listener_wait(dn_r_TcpListener * v_socket,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetworkReady dn_cb_udp_wait(dn_r_UdpSocket * v_socket,uint32_t v_events,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetworkReady dn_cb_tls_wait(dn_r_TlsSocket * v_socket,uint32_t v_events,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetworkError dn_cb_tcp_shutdown_write(dn_r_TcpSocket * v_socket);
static dn_r_SocketAddressResult dn_cb_tcp_local(dn_r_TcpSocket const * v_socket);
static dn_r_SocketAddressResult dn_cb_listener_local(dn_r_TcpListener const * v_socket);
static dn_r_SocketAddressResult dn_cb_udp_local(dn_r_UdpSocket const * v_socket);
static dn_r_SocketAddressResult dn_cb_tcp_peer(dn_r_TcpSocket const * v_socket);
static dn_r_SocketAddressResult dn_cb_tls_peer(dn_r_TlsSocket const * v_socket);
static dn_r_NetworkError dn_cb_tcp_configure(dn_r_TcpSocket * v_socket,dn_r_SocketOptions v_options);
static dn_r_NetworkError dn_cb_udp_configure(dn_r_UdpSocket * v_socket,uint32_t v_send_buffer,uint32_t v_receive_buffer);
static dn_r_SocketOptionsResult dn_cb_tcp_options(dn_r_TcpSocket const * v_socket);
static dn_r_NetworkTransfer dn_cb_udp_send(dn_r_UdpSocket * v_socket,dn_r_SocketAddress v_peer,dn_t_slice0_U8 v_bytes);
static dn_r_SocketDatagram dn_cb_udp_receive(dn_r_UdpSocket * v_socket,dn_t_buf_U8 * v_bytes);
static dn_r_NetworkTransfer dn_cb_tls_read(dn_r_TlsSocket * v_socket,dn_t_buf_U8 * v_bytes);
static dn_r_NetworkTransfer dn_cb_tls_write(dn_r_TlsSocket * v_socket,dn_t_slice0_U8 v_bytes);
static dn_r_NetworkTransfer dn_cb_tls_read_exact(dn_r_TlsSocket * v_socket,dn_t_buf_U8 * v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetworkTransfer dn_cb_tls_write_all(dn_r_TlsSocket * v_socket,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
extern dn_r_NetOpened dn_ext_dlm_net_reactor_new(uint32_t v_channels,uint32_t v_operations,size_t v_byte_limit) __asm__("dlm_net_reactor_new");
extern void dn_ext_dlm_net_reactor_close(void * v_reactor) __asm__("dlm_net_reactor_close");
extern dn_r_ReactorNativeRequest dn_ext_dlm_net_reactor_adopt(void * v_reactor,dn_r_NetOpened v_socket,uint32_t v_kind) __asm__("dlm_net_reactor_adopt");
extern dn_r_ReactorNativeRequest dn_ext_dlm_net_reactor_submit(void * v_reactor,uint64_t v_channel,uint32_t v_kind,uint8_t * v_bytes,size_t v_count,bool v_exact,uint32_t v_timeout_ms,dn_r_NetAddress v_peer) __asm__("dlm_net_reactor_submit");
extern dn_r_ReactorNativeRequest dn_ext_dlm_net_reactor_connect(void * v_reactor,dn_r_NetAddress v_address,uint32_t v_timeout_ms) __asm__("dlm_net_reactor_connect");
extern dn_r_ReactorNativeRequest dn_ext_dlm_net_reactor_timer(void * v_reactor,uint32_t v_timeout_ms,bool v_event) __asm__("dlm_net_reactor_timer");
extern dn_r_NetIo dn_ext_dlm_net_reactor_signal(void * v_reactor,uint64_t v_operation) __asm__("dlm_net_reactor_signal");
extern dn_r_NetIo dn_ext_dlm_net_reactor_cancel(void * v_reactor,uint64_t v_operation) __asm__("dlm_net_reactor_cancel");
extern dn_r_NetIo dn_ext_dlm_net_reactor_poll(void * v_reactor,uint32_t v_timeout_ms,void * v_cancel) __asm__("dlm_net_reactor_poll");
extern dn_r_ReactorNativeCompletion dn_ext_dlm_net_reactor_result(void * v_reactor,uint64_t v_operation) __asm__("dlm_net_reactor_result");
extern dn_r_NetIo dn_ext_dlm_net_reactor_copy(void * v_reactor,uint64_t v_operation,size_t v_offset,uint8_t * v_bytes,size_t v_capacity) __asm__("dlm_net_reactor_copy");
extern dn_r_NetIo dn_ext_dlm_net_reactor_ack(void * v_reactor,uint64_t v_operation) __asm__("dlm_net_reactor_ack");
extern dn_r_NetIo dn_ext_dlm_net_reactor_channel_close(void * v_reactor,uint64_t v_channel) __asm__("dlm_net_reactor_channel_close");
extern dn_r_NetOpened dn_ext_dlm_net_reactor_release(void * v_reactor,uint64_t v_channel,uint32_t v_kind) __asm__("dlm_net_reactor_release");
extern dn_r_ReactorNativeStats dn_ext_dlm_net_reactor_stats(void * v_reactor) __asm__("dlm_net_reactor_stats");
static void dn_cb_reactor_dispose(dn_r_NetOpened * v_state);
static void * dn_cb_reactor_handle(dn_r_NetworkReactor const * v_reactor);
static dn_r_NetworkOperation dn_cb_reactor_operation(dn_r_ReactorNativeRequest v_result);
static dn_r_ReactorCompletion dn_cb_reactor_completion(dn_r_ReactorNativeCompletion v_result);
static dn_r_NetworkReactor dn_cb_reactor_open(uint32_t v_channels,uint32_t v_operations,size_t v_byte_limit);
static dn_r_NetworkError dn_cb_reactor_error(dn_r_NetworkReactor const * v_reactor);
static void dn_cb_reactor_close(dn_r_NetworkReactor v_reactor);
static dn_r_ReactorTcpResult dn_cb_reactor_adopt_tcp(dn_r_NetworkReactor * v_reactor,dn_r_TcpSocket v_socket);
static dn_r_ReactorListenerResult dn_cb_reactor_adopt_listener(dn_r_NetworkReactor * v_reactor,dn_r_TcpListener v_socket);
static dn_r_ReactorUdpResult dn_cb_reactor_adopt_udp(dn_r_NetworkReactor * v_reactor,dn_r_UdpSocket v_socket);
static dn_r_ReactorConnect dn_cb_reactor_connect(dn_r_NetworkReactor * v_reactor,dn_r_SocketAddress v_address,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_cb_reactor_read(dn_r_NetworkReactor * v_reactor,dn_r_ReactorTcp v_channel,size_t v_count,bool v_exact,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_cb_reactor_write(dn_r_NetworkReactor * v_reactor,dn_r_ReactorTcp v_channel,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_cb_reactor_accept(dn_r_NetworkReactor * v_reactor,dn_r_ReactorListener v_channel,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_cb_reactor_receive(dn_r_NetworkReactor * v_reactor,dn_r_ReactorUdp v_channel,size_t v_capacity,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_cb_reactor_send(dn_r_NetworkReactor * v_reactor,dn_r_ReactorUdp v_channel,dn_r_SocketAddress v_peer,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_cb_reactor_timer(dn_r_NetworkReactor * v_reactor,uint32_t v_delay_ms);
static dn_r_NetworkOperation dn_cb_reactor_event(dn_r_NetworkReactor * v_reactor,uint32_t v_timeout_ms);
static dn_r_NetworkTransfer dn_cb_reactor_signal(dn_r_NetworkReactor * v_reactor,uint64_t v_operation);
static dn_r_NetworkTransfer dn_cb_reactor_cancel(dn_r_NetworkReactor * v_reactor,uint64_t v_operation);
static dn_r_NetworkTransfer dn_cb_reactor_cancel_all(dn_r_NetworkReactor * v_reactor);
static dn_r_NetworkTransfer dn_cb_reactor_poll(dn_r_NetworkReactor * v_reactor,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_ReactorCompletion dn_cb_reactor_result(dn_r_NetworkReactor const * v_reactor,uint64_t v_operation);
static dn_r_ReactorCompletion dn_cb_reactor_next(dn_r_NetworkReactor const * v_reactor);
static dn_r_NetworkTransfer dn_cb_reactor_copy(dn_r_NetworkReactor const * v_reactor,uint64_t v_operation,size_t v_offset,dn_t_buf_U8 * v_bytes);
static dn_r_NetworkTransfer dn_cb_reactor_ack(dn_r_NetworkReactor * v_reactor,uint64_t v_operation);
static dn_r_NetworkTransfer dn_cb_reactor_tcp_close(dn_r_NetworkReactor * v_reactor,dn_r_ReactorTcp v_channel);
static dn_r_NetworkTransfer dn_cb_reactor_listener_close(dn_r_NetworkReactor * v_reactor,dn_r_ReactorListener v_channel);
static dn_r_NetworkTransfer dn_cb_reactor_udp_close(dn_r_NetworkReactor * v_reactor,dn_r_ReactorUdp v_channel);
static dn_r_TcpSocket dn_cb_reactor_release_tcp(dn_r_NetworkReactor * v_reactor,dn_r_ReactorTcp v_channel);
static dn_r_TcpListener dn_cb_reactor_release_listener(dn_r_NetworkReactor * v_reactor,dn_r_ReactorListener v_channel);
static dn_r_UdpSocket dn_cb_reactor_release_udp(dn_r_NetworkReactor * v_reactor,dn_r_ReactorUdp v_channel);
static dn_r_ReactorStatistics dn_cb_reactor_statistics(dn_r_NetworkReactor const * v_reactor);
extern dn_r_NetOpened dn_ext_dlm_net_async_new(uint32_t v_channels,uint32_t v_operations,size_t v_byte_limit,uint32_t v_wake_limit) __asm__("dlm_net_async_new");
extern void dn_ext_dlm_net_async_close(void * v_scope) __asm__("dlm_net_async_close");
extern dn_r_ReactorNativeRequest dn_ext_dlm_net_async_adopt(void * v_scope,dn_r_NetOpened v_socket,uint32_t v_kind) __asm__("dlm_net_async_adopt");
extern dn_r_ReactorNativeRequest dn_ext_dlm_net_async_connect(void * v_scope,dn_r_NetAddress v_address,uint32_t v_timeout_ms) __asm__("dlm_net_async_connect");
extern dn_r_NetOpened dn_ext_dlm_net_async_waker(void * v_scope,uint64_t v_operation,uint32_t v_action) __asm__("dlm_net_async_waker");
extern void dn_ext_dlm_net_async_waker_close(void * v_waker) __asm__("dlm_net_async_waker_close");
extern dn_r_NetIo dn_ext_dlm_net_async_wake(void * v_waker) __asm__("dlm_net_async_wake");
extern dn_r_NetIo dn_ext_dlm_net_async_join(void * v_scope,uint32_t v_timeout_ms,void * v_cancel) __asm__("dlm_net_async_join");
extern dn_r_ScopeNativeStatistics dn_ext_dlm_net_async_stats(void * v_scope) __asm__("dlm_net_async_stats");
static void dn_cb_scope_dispose(dn_r_NetOpened * v_state);
static void dn_cb_scope_waker_dispose(dn_r_NetOpened * v_state);
static void * dn_cb_scope_handle(dn_r_NetworkScope const * v_scope);
static dn_r_NetworkScope dn_cb_scope_open(uint32_t v_channels,uint32_t v_operations,size_t v_byte_limit,uint32_t v_wake_limit);
static dn_r_NetworkError dn_cb_scope_error(dn_r_NetworkScope const * v_scope);
static void dn_cb_scope_close(dn_r_NetworkScope v_scope);
static dn_r_ReactorTcpResult dn_cb_scope_adopt_tcp(dn_r_NetworkScope * v_scope,dn_r_TcpSocket v_socket);
static dn_r_ReactorListenerResult dn_cb_scope_adopt_listener(dn_r_NetworkScope * v_scope,dn_r_TcpListener v_socket);
static dn_r_ReactorUdpResult dn_cb_scope_adopt_udp(dn_r_NetworkScope * v_scope,dn_r_UdpSocket v_socket);
static dn_r_ReactorConnect dn_cb_scope_connect(dn_r_NetworkScope * v_scope,dn_r_SocketAddress v_address,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_cb_scope_read(dn_r_NetworkScope * v_scope,dn_r_ReactorTcp v_channel,size_t v_count,bool v_exact,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_cb_scope_write(dn_r_NetworkScope * v_scope,dn_r_ReactorTcp v_channel,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_cb_scope_accept(dn_r_NetworkScope * v_scope,dn_r_ReactorListener v_channel,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_cb_scope_receive(dn_r_NetworkScope * v_scope,dn_r_ReactorUdp v_channel,size_t v_capacity,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_cb_scope_send(dn_r_NetworkScope * v_scope,dn_r_ReactorUdp v_channel,dn_r_SocketAddress v_peer,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms);
static dn_r_NetworkOperation dn_cb_scope_timer(dn_r_NetworkScope * v_scope,uint32_t v_delay_ms);
static dn_r_NetworkOperation dn_cb_scope_event(dn_r_NetworkScope * v_scope,uint32_t v_timeout_ms);
static dn_r_NetworkTransfer dn_cb_scope_poll(dn_r_NetworkScope * v_scope,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_NetworkTransfer dn_cb_scope_join(dn_r_NetworkScope * v_scope,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_ReactorCompletion dn_cb_scope_result(dn_r_NetworkScope const * v_scope,uint64_t v_operation);
static dn_r_ReactorCompletion dn_cb_scope_next(dn_r_NetworkScope const * v_scope);
static dn_r_NetworkTransfer dn_cb_scope_copy(dn_r_NetworkScope const * v_scope,uint64_t v_operation,size_t v_offset,dn_t_buf_U8 * v_bytes);
static dn_r_NetworkTransfer dn_cb_scope_ack(dn_r_NetworkScope * v_scope,uint64_t v_operation);
static dn_r_NetworkTransfer dn_cb_scope_cancel(dn_r_NetworkScope * v_scope,uint64_t v_operation);
static dn_r_NetworkTransfer dn_cb_scope_cancel_all(dn_r_NetworkScope * v_scope);
static dn_r_NetworkTransfer dn_cb_scope_tcp_close(dn_r_NetworkScope * v_scope,dn_r_ReactorTcp v_channel);
static dn_r_NetworkTransfer dn_cb_scope_listener_close(dn_r_NetworkScope * v_scope,dn_r_ReactorListener v_channel);
static dn_r_NetworkTransfer dn_cb_scope_udp_close(dn_r_NetworkScope * v_scope,dn_r_ReactorUdp v_channel);
static dn_r_EventWaker dn_cb_scope_event_waker(dn_r_NetworkScope const * v_scope,uint64_t v_operation);
static dn_r_OperationCanceller dn_cb_scope_operation_canceller(dn_r_NetworkScope const * v_scope,uint64_t v_operation);
static dn_r_ScopeCanceller dn_cb_scope_canceller(dn_r_NetworkScope const * v_scope);
static dn_r_NetworkError dn_cb_event_waker_error(dn_r_EventWaker const * v_waker);
static dn_r_NetworkError dn_cb_operation_canceller_error(dn_r_OperationCanceller const * v_waker);
static dn_r_NetworkError dn_cb_scope_canceller_error(dn_r_ScopeCanceller const * v_waker);
static dn_r_NetworkTransfer dn_cb_event_wake(dn_r_EventWaker const * v_waker);
static dn_r_NetworkTransfer dn_cb_operation_cancel(dn_r_OperationCanceller const * v_waker);
static dn_r_NetworkTransfer dn_cb_scope_request_cancel(dn_r_ScopeCanceller const * v_waker);
static void dn_cb_event_waker_close(dn_r_EventWaker v_waker);
static void dn_cb_operation_canceller_close(dn_r_OperationCanceller v_waker);
static void dn_cb_scope_canceller_close(dn_r_ScopeCanceller v_waker);
static dn_r_ScopeStatistics dn_cb_scope_statistics(dn_r_NetworkScope const * v_scope);
static void dn_cb_main(void);
static uint8_t dn_f_net_dispose(dn_r_NetOpened * v_state) {
{
(void)(({ void * dn_tmp_0=(v_state)->f_handle;
({ dn_ext_dlm_net_close(dn_tmp_0);dn_fp_default();0; }); }));
}

return 0;
}
static void dn_cb_net_dispose(dn_r_NetOpened * v_state) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_net_dispose(v_state);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_net_cancel_dispose(void * * v_state) {
{
(void)(({ void * dn_tmp_0=(*((void * *)dn_nonnull((void*)(v_state))));
({ dn_ext_dlm_net_cancel_close(dn_tmp_0);dn_fp_default();0; }); }));
}

return 0;
}
static void dn_cb_net_cancel_dispose(void * * v_state) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_net_cancel_dispose(v_state);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t * dn_f_net_input(dn_t_slice0_U8 v_bytes) {
{
if(({ size_t dn_tmp_0=({ dn_t_slice0_U8 dn_tmp_1=v_bytes;
(dn_tmp_1).len; });
size_t dn_tmp_2=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_2); })){
{ uint8_t * dn_tmp_3=({ ((uint8_t *)NULL); });return dn_tmp_3; }
}
else{
}
{ uint8_t * dn_tmp_8=({ uint8_t * dn_tmp_4=({ uint8_t const * dn_tmp_5=(&((*({ dn_t_slice0_U8 *dn_tmp_6=&(v_bytes);size_t dn_tmp_7=((size_t)(0ULL));&dn_tmp_6->data[dn_index(dn_tmp_7,dn_tmp_6->len)]; }))));
((uint8_t *)dn_tmp_5); });
((uint8_t *)dn_tmp_4); });return dn_tmp_8; }
}

dn_fail("missing native return");
}
static uint8_t * dn_cb_net_input(dn_t_slice0_U8 v_bytes) {
 if(!dn_enter()){return (uint8_t *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint8_t *){0};}
 uint8_t * dn_result=dn_f_net_input(v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t * dn_f_net_output(dn_t_buf_U8 * v_bytes) {
{
if(({ size_t dn_tmp_0=({ dn_t_buf_U8 * dn_tmp_1=v_bytes;
(*(dn_tmp_1)).len; });
size_t dn_tmp_2=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_2); })){
{ uint8_t * dn_tmp_3=({ ((uint8_t *)NULL); });return dn_tmp_3; }
}
else{
}
{ uint8_t * dn_tmp_8=({ uint8_t * dn_tmp_4=({ uint8_t * dn_tmp_5=(&((*({ dn_t_buf_U8 * dn_tmp_6=v_bytes;size_t dn_tmp_7=((size_t)(0ULL));&dn_tmp_6->data[dn_index(dn_tmp_7,dn_tmp_6->len)]; }))));
((uint8_t *)dn_tmp_5); });
((uint8_t *)dn_tmp_4); });return dn_tmp_8; }
}

dn_fail("missing native return");
}
static uint8_t * dn_cb_net_output(dn_t_buf_U8 * v_bytes) {
 if(!dn_enter()){return (uint8_t *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint8_t *){0};}
 uint8_t * dn_result=dn_f_net_output(v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetText dn_f_net_text(dn_utf8 v_text) {
{
dn_t_slice0_U8 v_bytes=({ dn_utf8 dn_tmp_0=v_text;
((dn_t_slice0_U8){dn_tmp_0.data,dn_tmp_0.len}); });
{ dn_r_NetText dn_tmp_5=({ uint8_t * dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_bytes;
dn_f_net_input(dn_tmp_2); });
size_t dn_tmp_3=({ dn_t_slice0_U8 dn_tmp_4=v_bytes;
(dn_tmp_4).len; });
((dn_r_NetText){dn_tmp_1,dn_tmp_3}); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_NetText dn_cb_net_text(dn_utf8 v_text) {
 if(!dn_enter()){return (dn_r_NetText){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetText){0};}
 dn_r_NetText dn_result=dn_f_net_text(v_text);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetText dn_f_net_bytes(dn_t_slice0_U8 v_bytes) {
{
{ dn_r_NetText dn_tmp_4=({ uint8_t * dn_tmp_0=({ dn_t_slice0_U8 dn_tmp_1=v_bytes;
dn_f_net_input(dn_tmp_1); });
size_t dn_tmp_2=({ dn_t_slice0_U8 dn_tmp_3=v_bytes;
(dn_tmp_3).len; });
((dn_r_NetText){dn_tmp_0,dn_tmp_2}); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_NetText dn_cb_net_bytes(dn_t_slice0_U8 v_bytes) {
 if(!dn_enter()){return (dn_r_NetText){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetText){0};}
 dn_r_NetText dn_result=dn_f_net_bytes(v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetResolved dn_f_network_resolve_bytes(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_index) {
{
{
{ dn_r_NetResolved dn_tmp_4=({ dn_r_NetText dn_tmp_0=({ dn_t_slice0_U8 dn_tmp_1=v_host;
dn_f_net_bytes(dn_tmp_1); });
uint16_t dn_tmp_2=v_port;
uint32_t dn_tmp_3=v_index;
({ dn_r_NetResolved dn_foreign=dn_ext_dlm_net_resolve(dn_tmp_0,dn_tmp_2,dn_tmp_3);dn_fp_default();dn_foreign; }); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_NetResolved dn_cb_network_resolve_bytes(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_index) {
 if(!dn_enter()){return (dn_r_NetResolved){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetResolved){0};}
 dn_r_NetResolved dn_result=dn_f_network_resolve_bytes(v_host,v_port,v_index);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_socket_tcp_bytes(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_timeout_ms) {
{
{
{ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_5=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_0,({ dn_r_NetText dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_host;
dn_f_net_bytes(dn_tmp_2); });
uint16_t dn_tmp_3=v_port;
uint32_t dn_tmp_4=v_timeout_ms;
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_tcp(dn_tmp_1,dn_tmp_3,dn_tmp_4);dn_fp_default();dn_foreign; }); })); });dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_5);return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_socket_tcp_bytes(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_timeout_ms) {
 if(!dn_enter()){return (dn_t_resource_r9_NetOpened_net_dispose *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_resource_r9_NetOpened_net_dispose *){0};}
 dn_t_resource_r9_NetOpened_net_dispose * dn_result=dn_f_socket_tcp_bytes(v_host,v_port,v_timeout_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_socket_listen_bytes(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_backlog) {
{
{
{ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_5=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_0,({ dn_r_NetText dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_host;
dn_f_net_bytes(dn_tmp_2); });
uint16_t dn_tmp_3=v_port;
uint32_t dn_tmp_4=v_backlog;
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_listen(dn_tmp_1,dn_tmp_3,dn_tmp_4);dn_fp_default();dn_foreign; }); })); });dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_5);return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_socket_listen_bytes(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_backlog) {
 if(!dn_enter()){return (dn_t_resource_r9_NetOpened_net_dispose *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_resource_r9_NetOpened_net_dispose *){0};}
 dn_t_resource_r9_NetOpened_net_dispose * dn_result=dn_f_socket_listen_bytes(v_host,v_port,v_backlog);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_socket_udp_bytes(dn_t_slice0_U8 v_host,uint16_t v_port) {
{
{
{ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_4=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_0,({ dn_r_NetText dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_host;
dn_f_net_bytes(dn_tmp_2); });
uint16_t dn_tmp_3=v_port;
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_udp(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); })); });dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_4);return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_socket_udp_bytes(dn_t_slice0_U8 v_host,uint16_t v_port) {
 if(!dn_enter()){return (dn_t_resource_r9_NetOpened_net_dispose *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_resource_r9_NetOpened_net_dispose *){0};}
 dn_t_resource_r9_NetOpened_net_dispose * dn_result=dn_f_socket_udp_bytes(v_host,v_port);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_socket_tls_bytes(dn_t_slice0_U8 v_url,uint32_t v_timeout_ms,dn_t_slice0_U8 v_ca_file,dn_t_slice0_U8 v_pin) {
{
{
{ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_8=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_0,({ dn_r_NetText dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_url;
dn_f_net_bytes(dn_tmp_2); });
uint32_t dn_tmp_3=v_timeout_ms;
dn_r_NetText dn_tmp_4=({ dn_t_slice0_U8 dn_tmp_5=v_ca_file;
dn_f_net_bytes(dn_tmp_5); });
dn_r_NetText dn_tmp_6=({ dn_t_slice0_U8 dn_tmp_7=v_pin;
dn_f_net_bytes(dn_tmp_7); });
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_tls(dn_tmp_1,dn_tmp_3,dn_tmp_4,dn_tmp_6);dn_fp_default();dn_foreign; }); })); });dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_8);return dn_tmp_8; }
}
}

dn_fail("missing native return");
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_socket_tls_bytes(dn_t_slice0_U8 v_url,uint32_t v_timeout_ms,dn_t_slice0_U8 v_ca_file,dn_t_slice0_U8 v_pin) {
 if(!dn_enter()){return (dn_t_resource_r9_NetOpened_net_dispose *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_resource_r9_NetOpened_net_dispose *){0};}
 dn_t_resource_r9_NetOpened_net_dispose * dn_result=dn_f_socket_tls_bytes(v_url,v_timeout_ms,v_ca_file,v_pin);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_http_client_bytes(dn_t_slice0_U8 v_url,uint32_t v_timeout_ms,size_t v_maximum,dn_t_slice0_U8 v_ca_file,dn_t_slice0_U8 v_pin) {
{
{
{ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_9=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_0,({ dn_r_NetText dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_url;
dn_f_net_bytes(dn_tmp_2); });
uint32_t dn_tmp_3=v_timeout_ms;
size_t dn_tmp_4=v_maximum;
dn_r_NetText dn_tmp_5=({ dn_t_slice0_U8 dn_tmp_6=v_ca_file;
dn_f_net_bytes(dn_tmp_6); });
dn_r_NetText dn_tmp_7=({ dn_t_slice0_U8 dn_tmp_8=v_pin;
dn_f_net_bytes(dn_tmp_8); });
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_http(dn_tmp_1,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_7);dn_fp_default();dn_foreign; }); })); });dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_9);return dn_tmp_9; }
}
}

dn_fail("missing native return");
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_http_client_bytes(dn_t_slice0_U8 v_url,uint32_t v_timeout_ms,size_t v_maximum,dn_t_slice0_U8 v_ca_file,dn_t_slice0_U8 v_pin) {
 if(!dn_enter()){return (dn_t_resource_r9_NetOpened_net_dispose *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_resource_r9_NetOpened_net_dispose *){0};}
 dn_t_resource_r9_NetOpened_net_dispose * dn_result=dn_f_http_client_bytes(v_url,v_timeout_ms,v_maximum,v_ca_file,v_pin);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_http_header_bytes(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_slice0_U8 v_header) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_request;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetIo dn_tmp_4=({ void * dn_tmp_1=(v_state)->f_handle;
dn_r_NetText dn_tmp_2=({ dn_t_slice0_U8 dn_tmp_3=v_header;
dn_f_net_bytes(dn_tmp_3); });
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_http_header(dn_tmp_1,dn_tmp_2);dn_fp_default();dn_foreign; }); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_http_header_bytes(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_slice0_U8 v_header) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_http_header_bytes(v_request,v_header);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_http_method_bytes(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_slice0_U8 v_method) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_request;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetIo dn_tmp_4=({ void * dn_tmp_1=(v_state)->f_handle;
dn_r_NetText dn_tmp_2=({ dn_t_slice0_U8 dn_tmp_3=v_method;
dn_f_net_bytes(dn_tmp_3); });
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_http_method(dn_tmp_1,dn_tmp_2);dn_fp_default();dn_foreign; }); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_http_method_bytes(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_slice0_U8 v_method) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_http_method_bytes(v_request,v_method);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_http_ech_bytes(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_slice0_U8 v_config) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_request;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetIo dn_tmp_4=({ void * dn_tmp_1=(v_state)->f_handle;
dn_r_NetText dn_tmp_2=({ dn_t_slice0_U8 dn_tmp_3=v_config;
dn_f_net_bytes(dn_tmp_3); });
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_http_ech(dn_tmp_1,dn_tmp_2);dn_fp_default();dn_foreign; }); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_http_ech_bytes(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_slice0_U8 v_config) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_http_ech_bytes(v_request,v_config);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_websocket_client_bytes(dn_t_slice0_U8 v_url,uint32_t v_timeout_ms,dn_t_slice0_U8 v_ca_file,dn_t_slice0_U8 v_pin) {
{
{
{ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_8=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_0,({ dn_r_NetText dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_url;
dn_f_net_bytes(dn_tmp_2); });
uint32_t dn_tmp_3=v_timeout_ms;
dn_r_NetText dn_tmp_4=({ dn_t_slice0_U8 dn_tmp_5=v_ca_file;
dn_f_net_bytes(dn_tmp_5); });
dn_r_NetText dn_tmp_6=({ dn_t_slice0_U8 dn_tmp_7=v_pin;
dn_f_net_bytes(dn_tmp_7); });
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_websocket(dn_tmp_1,dn_tmp_3,dn_tmp_4,dn_tmp_6);dn_fp_default();dn_foreign; }); })); });dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_8);return dn_tmp_8; }
}
}

dn_fail("missing native return");
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_websocket_client_bytes(dn_t_slice0_U8 v_url,uint32_t v_timeout_ms,dn_t_slice0_U8 v_ca_file,dn_t_slice0_U8 v_pin) {
 if(!dn_enter()){return (dn_t_resource_r9_NetOpened_net_dispose *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_resource_r9_NetOpened_net_dispose *){0};}
 dn_t_resource_r9_NetOpened_net_dispose * dn_result=dn_f_websocket_client_bytes(v_url,v_timeout_ms,v_ca_file,v_pin);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_network_capabilities(void) {
{
{
{ uint32_t dn_tmp_0=({ ({ uint32_t dn_foreign=dn_ext_dlm_net_capabilities();dn_fp_default();dn_foreign; }); });return dn_tmp_0; }
}
}

dn_fail("missing native return");
}
static uint32_t dn_cb_network_capabilities(void) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_network_capabilities();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_network_provider_version(void) {
{
{
{ uint32_t dn_tmp_0=({ ({ uint32_t dn_foreign=dn_ext_dlm_net_curl_version();dn_fp_default();dn_foreign; }); });return dn_tmp_0; }
}
}

dn_fail("missing native return");
}
static uint32_t dn_cb_network_provider_version(void) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_network_provider_version();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_network_provider_shutdown(void) {
{
{ dn_r_NetIo dn_tmp_0=({ ({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_provider_shutdown();dn_fp_default();dn_foreign; }); });return dn_tmp_0; }
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_network_provider_shutdown(void) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_network_provider_shutdown();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint64_t dn_f_network_clock_ms(void) {
{
{
{ uint64_t dn_tmp_0=({ ({ uint64_t dn_foreign=dn_ext_dlm_net_clock_ms();dn_fp_default();dn_foreign; }); });return dn_tmp_0; }
}
}

dn_fail("missing native return");
}
static uint64_t dn_cb_network_clock_ms(void) {
 if(!dn_enter()){return (uint64_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint64_t){0};}
 uint64_t dn_result=dn_f_network_clock_ms();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetResolved dn_f_network_resolve(dn_utf8 v_host,uint16_t v_port,uint32_t v_index) {
{
{
{ dn_r_NetResolved dn_tmp_4=({ dn_r_NetText dn_tmp_0=({ dn_utf8 dn_tmp_1=v_host;
dn_f_net_text(dn_tmp_1); });
uint16_t dn_tmp_2=v_port;
uint32_t dn_tmp_3=v_index;
({ dn_r_NetResolved dn_foreign=dn_ext_dlm_net_resolve(dn_tmp_0,dn_tmp_2,dn_tmp_3);dn_fp_default();dn_foreign; }); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_NetResolved dn_cb_network_resolve(dn_utf8 v_host,uint16_t v_port,uint32_t v_index) {
 if(!dn_enter()){return (dn_r_NetResolved){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetResolved){0};}
 dn_r_NetResolved dn_result=dn_f_network_resolve(v_host,v_port,v_index);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_socket_tcp(dn_utf8 v_host,uint16_t v_port,uint32_t v_timeout_ms) {
{
{
{ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_5=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_0,({ dn_r_NetText dn_tmp_1=({ dn_utf8 dn_tmp_2=v_host;
dn_f_net_text(dn_tmp_2); });
uint16_t dn_tmp_3=v_port;
uint32_t dn_tmp_4=v_timeout_ms;
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_tcp(dn_tmp_1,dn_tmp_3,dn_tmp_4);dn_fp_default();dn_foreign; }); })); });dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_5);return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_socket_tcp(dn_utf8 v_host,uint16_t v_port,uint32_t v_timeout_ms) {
 if(!dn_enter()){return (dn_t_resource_r9_NetOpened_net_dispose *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_resource_r9_NetOpened_net_dispose *){0};}
 dn_t_resource_r9_NetOpened_net_dispose * dn_result=dn_f_socket_tcp(v_host,v_port,v_timeout_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_socket_listen(dn_utf8 v_host,uint16_t v_port,uint32_t v_backlog) {
{
{
{ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_5=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_0,({ dn_r_NetText dn_tmp_1=({ dn_utf8 dn_tmp_2=v_host;
dn_f_net_text(dn_tmp_2); });
uint16_t dn_tmp_3=v_port;
uint32_t dn_tmp_4=v_backlog;
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_listen(dn_tmp_1,dn_tmp_3,dn_tmp_4);dn_fp_default();dn_foreign; }); })); });dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_5);return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_socket_listen(dn_utf8 v_host,uint16_t v_port,uint32_t v_backlog) {
 if(!dn_enter()){return (dn_t_resource_r9_NetOpened_net_dispose *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_resource_r9_NetOpened_net_dispose *){0};}
 dn_t_resource_r9_NetOpened_net_dispose * dn_result=dn_f_socket_listen(v_host,v_port,v_backlog);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_socket_udp(dn_utf8 v_host,uint16_t v_port) {
{
{
{ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_4=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_0,({ dn_r_NetText dn_tmp_1=({ dn_utf8 dn_tmp_2=v_host;
dn_f_net_text(dn_tmp_2); });
uint16_t dn_tmp_3=v_port;
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_udp(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); })); });dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_4);return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_socket_udp(dn_utf8 v_host,uint16_t v_port) {
 if(!dn_enter()){return (dn_t_resource_r9_NetOpened_net_dispose *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_resource_r9_NetOpened_net_dispose *){0};}
 dn_t_resource_r9_NetOpened_net_dispose * dn_result=dn_f_socket_udp(v_host,v_port);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_socket_accept(dn_t_resource_r9_NetOpened_net_dispose * * v_listener) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_listener;
(&((*dn_tmp_0)->value)); });
{
{ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_3=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_1=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_1,({ void * dn_tmp_2=(v_state)->f_handle;
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_accept(dn_tmp_2);dn_fp_default();dn_foreign; }); })); });dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_3);return dn_tmp_3; }
}
}

dn_fail("missing native return");
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_socket_accept(dn_t_resource_r9_NetOpened_net_dispose * * v_listener) {
 if(!dn_enter()){return (dn_t_resource_r9_NetOpened_net_dispose *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_resource_r9_NetOpened_net_dispose *){0};}
 dn_t_resource_r9_NetOpened_net_dispose * dn_result=dn_f_socket_accept(v_listener);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static int32_t dn_f_socket_status(dn_t_resource_r9_NetOpened_net_dispose * const * v_socket) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_0=v_socket;
(&((*dn_tmp_0)->value)); });
{ int32_t dn_tmp_1=(v_state)->f_status;return dn_tmp_1; }
}

dn_fail("missing native return");
}
static int32_t dn_cb_socket_status(dn_t_resource_r9_NetOpened_net_dispose * const * v_socket) {
 if(!dn_enter()){return (int32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (int32_t){0};}
 int32_t dn_result=dn_f_socket_status(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static int32_t dn_f_socket_system_error(dn_t_resource_r9_NetOpened_net_dispose * const * v_socket) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_0=v_socket;
(&((*dn_tmp_0)->value)); });
{ int32_t dn_tmp_1=(v_state)->f_system;return dn_tmp_1; }
}

dn_fail("missing native return");
}
static int32_t dn_cb_socket_system_error(dn_t_resource_r9_NetOpened_net_dispose * const * v_socket) {
 if(!dn_enter()){return (int32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (int32_t){0};}
 int32_t dn_result=dn_f_socket_system_error(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetResolved dn_f_socket_local(dn_t_resource_r9_NetOpened_net_dispose * const * v_socket) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_0=v_socket;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetResolved dn_tmp_2=({ void * dn_tmp_1=(v_state)->f_handle;
({ dn_r_NetResolved dn_foreign=dn_ext_dlm_net_local(dn_tmp_1);dn_fp_default();dn_foreign; }); });return dn_tmp_2; }
}
}

dn_fail("missing native return");
}
static dn_r_NetResolved dn_cb_socket_local(dn_t_resource_r9_NetOpened_net_dispose * const * v_socket) {
 if(!dn_enter()){return (dn_r_NetResolved){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetResolved){0};}
 dn_r_NetResolved dn_result=dn_f_socket_local(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_socket_nodelay(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,bool v_enabled) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_socket;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetIo dn_tmp_3=({ void * dn_tmp_1=(v_state)->f_handle;
bool dn_tmp_2=v_enabled;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_nodelay(dn_tmp_1,dn_tmp_2);dn_fp_default();dn_foreign; }); });return dn_tmp_3; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_socket_nodelay(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,bool v_enabled) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_socket_nodelay(v_socket,v_enabled);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_socket_write(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_slice0_U8 v_bytes) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_socket;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetIo dn_tmp_6=({ void * dn_tmp_1=(v_state)->f_handle;
uint8_t * dn_tmp_2=({ dn_t_slice0_U8 dn_tmp_3=v_bytes;
dn_f_net_input(dn_tmp_3); });
size_t dn_tmp_4=({ dn_t_slice0_U8 dn_tmp_5=v_bytes;
(dn_tmp_5).len; });
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_send(dn_tmp_1,dn_tmp_2,dn_tmp_4);dn_fp_default();dn_foreign; }); });return dn_tmp_6; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_socket_write(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_slice0_U8 v_bytes) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_socket_write(v_socket,v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_socket_read(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_buf_U8 * v_bytes) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_socket;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetIo dn_tmp_6=({ void * dn_tmp_1=(v_state)->f_handle;
uint8_t * dn_tmp_2=({ dn_t_buf_U8 * dn_tmp_3=v_bytes;
dn_f_net_output(dn_tmp_3); });
size_t dn_tmp_4=({ dn_t_buf_U8 * dn_tmp_5=v_bytes;
(*(dn_tmp_5)).len; });
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_recv(dn_tmp_1,dn_tmp_2,dn_tmp_4);dn_fp_default();dn_foreign; }); });return dn_tmp_6; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_socket_read(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_buf_U8 * v_bytes) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_socket_read(v_socket,v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_socket_write_all(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_socket;
(&((*dn_tmp_0)->value)); });
void * const * v_token=({ dn_t_resource_h_net_cancel_dispose * const * dn_tmp_1=v_cancel;
(&((*dn_tmp_1)->value)); });
{
{ dn_r_NetIo dn_tmp_10=({ void * dn_tmp_2=(v_state)->f_handle;
uint8_t * dn_tmp_3=({ dn_t_slice0_U8 dn_tmp_4=v_bytes;
dn_f_net_input(dn_tmp_4); });
size_t dn_tmp_5=({ dn_t_slice0_U8 dn_tmp_6=v_bytes;
(dn_tmp_6).len; });
uint32_t dn_tmp_7=v_timeout_ms;
void * dn_tmp_8=(*((void * const *)dn_nonnull((void*)(v_token))));
bool dn_tmp_9=1;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_transfer(dn_tmp_2,dn_tmp_3,dn_tmp_5,dn_tmp_7,dn_tmp_8,dn_tmp_9);dn_fp_default();dn_foreign; }); });return dn_tmp_10; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_socket_write_all(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_socket_write_all(v_socket,v_bytes,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_socket_read_exact(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_buf_U8 * v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_socket;
(&((*dn_tmp_0)->value)); });
void * const * v_token=({ dn_t_resource_h_net_cancel_dispose * const * dn_tmp_1=v_cancel;
(&((*dn_tmp_1)->value)); });
{
{ dn_r_NetIo dn_tmp_10=({ void * dn_tmp_2=(v_state)->f_handle;
uint8_t * dn_tmp_3=({ dn_t_buf_U8 * dn_tmp_4=v_bytes;
dn_f_net_output(dn_tmp_4); });
size_t dn_tmp_5=({ dn_t_buf_U8 * dn_tmp_6=v_bytes;
(*(dn_tmp_6)).len; });
uint32_t dn_tmp_7=v_timeout_ms;
void * dn_tmp_8=(*((void * const *)dn_nonnull((void*)(v_token))));
bool dn_tmp_9=0;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_transfer(dn_tmp_2,dn_tmp_3,dn_tmp_5,dn_tmp_7,dn_tmp_8,dn_tmp_9);dn_fp_default();dn_foreign; }); });return dn_tmp_10; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_socket_read_exact(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_buf_U8 * v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_socket_read_exact(v_socket,v_bytes,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_socket_shutdown_write(dn_t_resource_r9_NetOpened_net_dispose * * v_socket) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_socket;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetIo dn_tmp_2=({ void * dn_tmp_1=(v_state)->f_handle;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_shutdown(dn_tmp_1);dn_fp_default();dn_foreign; }); });return dn_tmp_2; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_socket_shutdown_write(dn_t_resource_r9_NetOpened_net_dispose * * v_socket) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_socket_shutdown_write(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_socket_send_datagram(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_r_NetAddress v_peer,dn_t_slice0_U8 v_bytes) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_socket;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetIo dn_tmp_7=({ void * dn_tmp_1=(v_state)->f_handle;
dn_r_NetAddress dn_tmp_2=v_peer;
uint8_t * dn_tmp_3=({ dn_t_slice0_U8 dn_tmp_4=v_bytes;
dn_f_net_input(dn_tmp_4); });
size_t dn_tmp_5=({ dn_t_slice0_U8 dn_tmp_6=v_bytes;
(dn_tmp_6).len; });
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_udp_send(dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_5);dn_fp_default();dn_foreign; }); });return dn_tmp_7; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_socket_send_datagram(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_r_NetAddress v_peer,dn_t_slice0_U8 v_bytes) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_socket_send_datagram(v_socket,v_peer,v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetDatagram dn_f_socket_read_datagram(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_buf_U8 * v_bytes) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_socket;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetDatagram dn_tmp_6=({ void * dn_tmp_1=(v_state)->f_handle;
uint8_t * dn_tmp_2=({ dn_t_buf_U8 * dn_tmp_3=v_bytes;
dn_f_net_output(dn_tmp_3); });
size_t dn_tmp_4=({ dn_t_buf_U8 * dn_tmp_5=v_bytes;
(*(dn_tmp_5)).len; });
({ dn_r_NetDatagram dn_foreign=dn_ext_dlm_net_udp_recv(dn_tmp_1,dn_tmp_2,dn_tmp_4);dn_fp_default();dn_foreign; }); });return dn_tmp_6; }
}
}

dn_fail("missing native return");
}
static dn_r_NetDatagram dn_cb_socket_read_datagram(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_buf_U8 * v_bytes) {
 if(!dn_enter()){return (dn_r_NetDatagram){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetDatagram){0};}
 dn_r_NetDatagram dn_result=dn_f_socket_read_datagram(v_socket,v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_socket_wait(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,uint32_t v_events,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_socket;
(&((*dn_tmp_0)->value)); });
void * const * v_token=({ dn_t_resource_h_net_cancel_dispose * const * dn_tmp_1=v_cancel;
(&((*dn_tmp_1)->value)); });
{
{ dn_r_NetIo dn_tmp_6=({ void * dn_tmp_2=(v_state)->f_handle;
uint32_t dn_tmp_3=v_events;
uint32_t dn_tmp_4=v_timeout_ms;
void * dn_tmp_5=(*((void * const *)dn_nonnull((void*)(v_token))));
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_wait(dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5);dn_fp_default();dn_foreign; }); });return dn_tmp_6; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_socket_wait(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,uint32_t v_events,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_socket_wait(v_socket,v_events,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetPoll dn_f_socket_poll_entry(dn_t_resource_r9_NetOpened_net_dispose * const * v_socket,uint32_t v_events) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_0=v_socket;
(&((*dn_tmp_0)->value)); });
{ dn_r_NetPoll dn_tmp_6=({ void * dn_tmp_1=(v_state)->f_handle;
uint32_t dn_tmp_2=v_events;
uint32_t dn_tmp_3=((uint32_t)(0ULL));
int32_t dn_tmp_4=((int32_t)(0ULL));
int32_t dn_tmp_5=((int32_t)(0ULL));
((dn_r_NetPoll){dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5}); });return dn_tmp_6; }
}

dn_fail("missing native return");
}
static dn_r_NetPoll dn_cb_socket_poll_entry(dn_t_resource_r9_NetOpened_net_dispose * const * v_socket,uint32_t v_events) {
 if(!dn_enter()){return (dn_r_NetPoll){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetPoll){0};}
 dn_r_NetPoll dn_result=dn_f_socket_poll_entry(v_socket,v_events);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_socket_poll(dn_t_buf_r7_NetPoll * v_items,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
if(({ size_t dn_tmp_0=({ dn_t_buf_r7_NetPoll * dn_tmp_1=v_items;
(*(dn_tmp_1)).len; });
size_t dn_tmp_2=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_2); })){
{ dn_r_NetIo dn_tmp_6=({ size_t dn_tmp_3=((size_t)(0ULL));
int32_t dn_tmp_4=((int32_t)(4ULL));
int32_t dn_tmp_5=((int32_t)(0ULL));
((dn_r_NetIo){dn_tmp_3,dn_tmp_4,dn_tmp_5}); });return dn_tmp_6; }
}
else{
}
void * const * v_token=({ dn_t_resource_h_net_cancel_dispose * const * dn_tmp_7=v_cancel;
(&((*dn_tmp_7)->value)); });
{ dn_r_NetIo dn_tmp_17=({ dn_r_NetPoll * dn_tmp_8=({ dn_r_NetPoll * dn_tmp_9=({ dn_r_NetPoll * dn_tmp_10=(&((*({ dn_t_buf_r7_NetPoll * dn_tmp_11=v_items;size_t dn_tmp_12=((size_t)(0ULL));&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; }))));
((dn_r_NetPoll *)dn_tmp_10); });
((dn_r_NetPoll *)dn_tmp_9); });
size_t dn_tmp_13=({ dn_t_buf_r7_NetPoll * dn_tmp_14=v_items;
(*(dn_tmp_14)).len; });
uint32_t dn_tmp_15=v_timeout_ms;
void * dn_tmp_16=(*((void * const *)dn_nonnull((void*)(v_token))));
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_poll(dn_tmp_8,dn_tmp_13,dn_tmp_15,dn_tmp_16);dn_fp_default();dn_foreign; }); });return dn_tmp_17; }
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_socket_poll(dn_t_buf_r7_NetPoll * v_items,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_socket_poll(v_items,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_resource_h_net_cancel_dispose * dn_f_network_cancel(void) {
{
{
{ dn_t_resource_h_net_cancel_dispose * dn_tmp_1=({ dn_t_resource_h_net_cancel_dispose * dn_tmp_0=dn_begin_resource_h_net_cancel_dispose();dn_commit_resource_h_net_cancel_dispose(dn_tmp_0,({ ({ void * dn_foreign=dn_ext_dlm_net_cancel_new();dn_fp_default();dn_foreign; }); })); });dn_adopt_resource_h_net_cancel_dispose(&dn_tmp_1);return dn_tmp_1; }
}
}

dn_fail("missing native return");
}
static dn_t_resource_h_net_cancel_dispose * dn_cb_network_cancel(void) {
 if(!dn_enter()){return (dn_t_resource_h_net_cancel_dispose *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_resource_h_net_cancel_dispose *){0};}
 dn_t_resource_h_net_cancel_dispose * dn_result=dn_f_network_cancel();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_network_cancel_valid(dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
void * const * v_token=({ dn_t_resource_h_net_cancel_dispose * const * dn_tmp_0=v_cancel;
(&((*dn_tmp_0)->value)); });
{
{ bool dn_tmp_6=({ size_t dn_tmp_1=({ uint8_t * dn_tmp_2=({ void * dn_tmp_3=(*((void * const *)dn_nonnull((void*)(v_token))));
((uint8_t *)dn_tmp_3); });
((size_t)(uintptr_t)dn_tmp_2); });
size_t dn_tmp_4=({ size_t dn_tmp_5=((size_t)(0ULL));
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_5; });
(dn_tmp_1 != dn_tmp_4); });return dn_tmp_6; }
}
}

dn_fail("missing native return");
}
static bool dn_cb_network_cancel_valid(dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_network_cancel_valid(v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_network_cancel_signal(dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
void * const * v_token=({ dn_t_resource_h_net_cancel_dispose * const * dn_tmp_0=v_cancel;
(&((*dn_tmp_0)->value)); });
{
(void)(({ void * dn_tmp_1=(*((void * const *)dn_nonnull((void*)(v_token))));
({ dn_ext_dlm_net_cancel_signal(dn_tmp_1);dn_fp_default();0; }); }));
}
}

return 0;
}
static void dn_cb_network_cancel_signal(dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_network_cancel_signal(v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_network_cancel_reset(dn_t_resource_h_net_cancel_dispose * * v_cancel) {
{
void * const * v_token=({ dn_t_resource_h_net_cancel_dispose * * dn_tmp_0=v_cancel;
(&((*dn_tmp_0)->value)); });
{
(void)(({ void * dn_tmp_1=(*((void * const *)dn_nonnull((void*)(v_token))));
({ dn_ext_dlm_net_cancel_reset(dn_tmp_1);dn_fp_default();0; }); }));
}
}

return 0;
}
static void dn_cb_network_cancel_reset(dn_t_resource_h_net_cancel_dispose * * v_cancel) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_network_cancel_reset(v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_socket_tls(dn_utf8 v_url,uint32_t v_timeout_ms,dn_utf8 v_ca_file,dn_utf8 v_pin) {
{
{
{ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_8=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_0,({ dn_r_NetText dn_tmp_1=({ dn_utf8 dn_tmp_2=v_url;
dn_f_net_text(dn_tmp_2); });
uint32_t dn_tmp_3=v_timeout_ms;
dn_r_NetText dn_tmp_4=({ dn_utf8 dn_tmp_5=v_ca_file;
dn_f_net_text(dn_tmp_5); });
dn_r_NetText dn_tmp_6=({ dn_utf8 dn_tmp_7=v_pin;
dn_f_net_text(dn_tmp_7); });
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_tls(dn_tmp_1,dn_tmp_3,dn_tmp_4,dn_tmp_6);dn_fp_default();dn_foreign; }); })); });dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_8);return dn_tmp_8; }
}
}

dn_fail("missing native return");
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_socket_tls(dn_utf8 v_url,uint32_t v_timeout_ms,dn_utf8 v_ca_file,dn_utf8 v_pin) {
 if(!dn_enter()){return (dn_t_resource_r9_NetOpened_net_dispose *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_resource_r9_NetOpened_net_dispose *){0};}
 dn_t_resource_r9_NetOpened_net_dispose * dn_result=dn_f_socket_tls(v_url,v_timeout_ms,v_ca_file,v_pin);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_http_client(dn_utf8 v_url,uint32_t v_timeout_ms,size_t v_maximum,dn_utf8 v_ca_file,dn_utf8 v_pin) {
{
{
{ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_9=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_0,({ dn_r_NetText dn_tmp_1=({ dn_utf8 dn_tmp_2=v_url;
dn_f_net_text(dn_tmp_2); });
uint32_t dn_tmp_3=v_timeout_ms;
size_t dn_tmp_4=v_maximum;
dn_r_NetText dn_tmp_5=({ dn_utf8 dn_tmp_6=v_ca_file;
dn_f_net_text(dn_tmp_6); });
dn_r_NetText dn_tmp_7=({ dn_utf8 dn_tmp_8=v_pin;
dn_f_net_text(dn_tmp_8); });
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_http(dn_tmp_1,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_7);dn_fp_default();dn_foreign; }); })); });dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_9);return dn_tmp_9; }
}
}

dn_fail("missing native return");
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_http_client(dn_utf8 v_url,uint32_t v_timeout_ms,size_t v_maximum,dn_utf8 v_ca_file,dn_utf8 v_pin) {
 if(!dn_enter()){return (dn_t_resource_r9_NetOpened_net_dispose *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_resource_r9_NetOpened_net_dispose *){0};}
 dn_t_resource_r9_NetOpened_net_dispose * dn_result=dn_f_http_client(v_url,v_timeout_ms,v_maximum,v_ca_file,v_pin);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_http_version(dn_t_resource_r9_NetOpened_net_dispose * * v_request,uint32_t v_version) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_request;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetIo dn_tmp_3=({ void * dn_tmp_1=(v_state)->f_handle;
uint32_t dn_tmp_2=v_version;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_http_version(dn_tmp_1,dn_tmp_2);dn_fp_default();dn_foreign; }); });return dn_tmp_3; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_http_version(dn_t_resource_r9_NetOpened_net_dispose * * v_request,uint32_t v_version) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_http_version(v_request,v_version);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_http_header(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_utf8 v_header) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_request;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetIo dn_tmp_4=({ void * dn_tmp_1=(v_state)->f_handle;
dn_r_NetText dn_tmp_2=({ dn_utf8 dn_tmp_3=v_header;
dn_f_net_text(dn_tmp_3); });
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_http_header(dn_tmp_1,dn_tmp_2);dn_fp_default();dn_foreign; }); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_http_header(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_utf8 v_header) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_http_header(v_request,v_header);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_http_method(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_utf8 v_method) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_request;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetIo dn_tmp_4=({ void * dn_tmp_1=(v_state)->f_handle;
dn_r_NetText dn_tmp_2=({ dn_utf8 dn_tmp_3=v_method;
dn_f_net_text(dn_tmp_3); });
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_http_method(dn_tmp_1,dn_tmp_2);dn_fp_default();dn_foreign; }); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_http_method(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_utf8 v_method) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_http_method(v_request,v_method);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_http_body(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_slice0_U8 v_bytes) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_request;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetIo dn_tmp_6=({ void * dn_tmp_1=(v_state)->f_handle;
uint8_t * dn_tmp_2=({ dn_t_slice0_U8 dn_tmp_3=v_bytes;
dn_f_net_input(dn_tmp_3); });
size_t dn_tmp_4=({ dn_t_slice0_U8 dn_tmp_5=v_bytes;
(dn_tmp_5).len; });
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_http_body(dn_tmp_1,dn_tmp_2,dn_tmp_4);dn_fp_default();dn_foreign; }); });return dn_tmp_6; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_http_body(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_slice0_U8 v_bytes) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_http_body(v_request,v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_http_ech(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_utf8 v_config) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_request;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetIo dn_tmp_4=({ void * dn_tmp_1=(v_state)->f_handle;
dn_r_NetText dn_tmp_2=({ dn_utf8 dn_tmp_3=v_config;
dn_f_net_text(dn_tmp_3); });
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_http_ech(dn_tmp_1,dn_tmp_2);dn_fp_default();dn_foreign; }); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_http_ech(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_utf8 v_config) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_http_ech(v_request,v_config);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_http_perform(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_request;
(&((*dn_tmp_0)->value)); });
void * const * v_token=({ dn_t_resource_h_net_cancel_dispose * const * dn_tmp_1=v_cancel;
(&((*dn_tmp_1)->value)); });
{
{ dn_r_NetIo dn_tmp_4=({ void * dn_tmp_2=(v_state)->f_handle;
void * dn_tmp_3=(*((void * const *)dn_nonnull((void*)(v_token))));
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_http_perform(dn_tmp_2,dn_tmp_3);dn_fp_default();dn_foreign; }); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_http_perform(dn_t_resource_r9_NetOpened_net_dispose * * v_request,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_http_perform(v_request,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_http_info(dn_t_resource_r9_NetOpened_net_dispose * const * v_request,uint32_t v_field) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_0=v_request;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetIo dn_tmp_3=({ void * dn_tmp_1=(v_state)->f_handle;
uint32_t dn_tmp_2=v_field;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_http_info(dn_tmp_1,dn_tmp_2);dn_fp_default();dn_foreign; }); });return dn_tmp_3; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_http_info(dn_t_resource_r9_NetOpened_net_dispose * const * v_request,uint32_t v_field) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_http_info(v_request,v_field);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_http_read(dn_t_resource_r9_NetOpened_net_dispose * const * v_request,bool v_headers,size_t v_offset,dn_t_buf_U8 * v_bytes) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_0=v_request;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetIo dn_tmp_8=({ void * dn_tmp_1=(v_state)->f_handle;
bool dn_tmp_2=v_headers;
size_t dn_tmp_3=v_offset;
uint8_t * dn_tmp_4=({ dn_t_buf_U8 * dn_tmp_5=v_bytes;
dn_f_net_output(dn_tmp_5); });
size_t dn_tmp_6=({ dn_t_buf_U8 * dn_tmp_7=v_bytes;
(*(dn_tmp_7)).len; });
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_http_read(dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_6);dn_fp_default();dn_foreign; }); });return dn_tmp_8; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_http_read(dn_t_resource_r9_NetOpened_net_dispose * const * v_request,bool v_headers,size_t v_offset,dn_t_buf_U8 * v_bytes) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_http_read(v_request,v_headers,v_offset,v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_f_websocket_client(dn_utf8 v_url,uint32_t v_timeout_ms,dn_utf8 v_ca_file,dn_utf8 v_pin) {
{
{
{ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_8=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_0,({ dn_r_NetText dn_tmp_1=({ dn_utf8 dn_tmp_2=v_url;
dn_f_net_text(dn_tmp_2); });
uint32_t dn_tmp_3=v_timeout_ms;
dn_r_NetText dn_tmp_4=({ dn_utf8 dn_tmp_5=v_ca_file;
dn_f_net_text(dn_tmp_5); });
dn_r_NetText dn_tmp_6=({ dn_utf8 dn_tmp_7=v_pin;
dn_f_net_text(dn_tmp_7); });
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_websocket(dn_tmp_1,dn_tmp_3,dn_tmp_4,dn_tmp_6);dn_fp_default();dn_foreign; }); })); });dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_8);return dn_tmp_8; }
}
}

dn_fail("missing native return");
}
static dn_t_resource_r9_NetOpened_net_dispose * dn_cb_websocket_client(dn_utf8 v_url,uint32_t v_timeout_ms,dn_utf8 v_ca_file,dn_utf8 v_pin) {
 if(!dn_enter()){return (dn_t_resource_r9_NetOpened_net_dispose *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_resource_r9_NetOpened_net_dispose *){0};}
 dn_t_resource_r9_NetOpened_net_dispose * dn_result=dn_f_websocket_client(v_url,v_timeout_ms,v_ca_file,v_pin);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_websocket_start(dn_t_resource_r9_NetOpened_net_dispose * * v_socket) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_socket;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetIo dn_tmp_2=({ void * dn_tmp_1=(v_state)->f_handle;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_ws_start(dn_tmp_1);dn_fp_default();dn_foreign; }); });return dn_tmp_2; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_websocket_start(dn_t_resource_r9_NetOpened_net_dispose * * v_socket) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_websocket_start(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_websocket_write(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_slice0_U8 v_bytes,uint32_t v_flags,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_socket;
(&((*dn_tmp_0)->value)); });
void * const * v_token=({ dn_t_resource_h_net_cancel_dispose * const * dn_tmp_1=v_cancel;
(&((*dn_tmp_1)->value)); });
{
{ dn_r_NetIo dn_tmp_10=({ void * dn_tmp_2=(v_state)->f_handle;
uint8_t * dn_tmp_3=({ dn_t_slice0_U8 dn_tmp_4=v_bytes;
dn_f_net_input(dn_tmp_4); });
size_t dn_tmp_5=({ dn_t_slice0_U8 dn_tmp_6=v_bytes;
(dn_tmp_6).len; });
uint32_t dn_tmp_7=v_flags;
uint32_t dn_tmp_8=v_timeout_ms;
void * dn_tmp_9=(*((void * const *)dn_nonnull((void*)(v_token))));
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_ws_send(dn_tmp_2,dn_tmp_3,dn_tmp_5,dn_tmp_7,dn_tmp_8,dn_tmp_9);dn_fp_default();dn_foreign; }); });return dn_tmp_10; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_websocket_write(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_slice0_U8 v_bytes,uint32_t v_flags,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_websocket_write(v_socket,v_bytes,v_flags,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetWebSocketIo dn_f_websocket_read(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_buf_U8 * v_bytes) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=v_socket;
(&((*dn_tmp_0)->value)); });
{
{ dn_r_NetWebSocketIo dn_tmp_6=({ void * dn_tmp_1=(v_state)->f_handle;
uint8_t * dn_tmp_2=({ dn_t_buf_U8 * dn_tmp_3=v_bytes;
dn_f_net_output(dn_tmp_3); });
size_t dn_tmp_4=({ dn_t_buf_U8 * dn_tmp_5=v_bytes;
(*(dn_tmp_5)).len; });
({ dn_r_NetWebSocketIo dn_foreign=dn_ext_dlm_net_ws_recv(dn_tmp_1,dn_tmp_2,dn_tmp_4);dn_fp_default();dn_foreign; }); });return dn_tmp_6; }
}
}

dn_fail("missing native return");
}
static dn_r_NetWebSocketIo dn_cb_websocket_read(dn_t_resource_r9_NetOpened_net_dispose * * v_socket,dn_t_buf_U8 * v_bytes) {
 if(!dn_enter()){return (dn_r_NetWebSocketIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetWebSocketIo){0};}
 dn_r_NetWebSocketIo dn_result=dn_f_websocket_read(v_socket,v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_network_random(dn_t_buf_U8 * v_bytes) {
{
{
{ dn_r_NetIo dn_tmp_4=({ uint8_t * dn_tmp_0=({ dn_t_buf_U8 * dn_tmp_1=v_bytes;
dn_f_net_output(dn_tmp_1); });
size_t dn_tmp_2=({ dn_t_buf_U8 * dn_tmp_3=v_bytes;
(*(dn_tmp_3)).len; });
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_random(dn_tmp_0,dn_tmp_2);dn_fp_default();dn_foreign; }); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_network_random(dn_t_buf_U8 * v_bytes) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_network_random(v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_network_ok(dn_r_NetworkError const * v_error) {
{
{ bool dn_tmp_3=({ int32_t dn_tmp_0=(v_error)->f_kind;
int32_t dn_tmp_1=({ int32_t dn_tmp_2=((int32_t)(0ULL));
if((__int128)dn_tmp_2<(-((__int128)1<<31))||(__int128)dn_tmp_2>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_2; });
(dn_tmp_0 == dn_tmp_1); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static bool dn_cb_network_ok(dn_r_NetworkError const * v_error) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_network_ok(v_error);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_network_would_block(dn_r_NetworkError const * v_error) {
{
{ bool dn_tmp_3=({ int32_t dn_tmp_0=(v_error)->f_kind;
int32_t dn_tmp_1=({ int32_t dn_tmp_2=((int32_t)(1ULL));
if((__int128)dn_tmp_2<(-((__int128)1<<31))||(__int128)dn_tmp_2>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_2; });
(dn_tmp_0 == dn_tmp_1); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static bool dn_cb_network_would_block(dn_r_NetworkError const * v_error) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_network_would_block(v_error);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_network_eof(dn_r_NetworkError const * v_error) {
{
{ bool dn_tmp_3=({ int32_t dn_tmp_0=(v_error)->f_kind;
int32_t dn_tmp_1=({ int32_t dn_tmp_2=((int32_t)(2ULL));
if((__int128)dn_tmp_2<(-((__int128)1<<31))||(__int128)dn_tmp_2>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_2; });
(dn_tmp_0 == dn_tmp_1); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static bool dn_cb_network_eof(dn_r_NetworkError const * v_error) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_network_eof(v_error);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_network_timed_out(dn_r_NetworkError const * v_error) {
{
{ bool dn_tmp_3=({ int32_t dn_tmp_0=(v_error)->f_kind;
int32_t dn_tmp_1=({ int32_t dn_tmp_2=((int32_t)(3ULL));
if((__int128)dn_tmp_2<(-((__int128)1<<31))||(__int128)dn_tmp_2>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_2; });
(dn_tmp_0 == dn_tmp_1); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static bool dn_cb_network_timed_out(dn_r_NetworkError const * v_error) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_network_timed_out(v_error);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_network_was_cancelled(dn_r_NetworkError const * v_error) {
{
{ bool dn_tmp_3=({ int32_t dn_tmp_0=(v_error)->f_kind;
int32_t dn_tmp_1=({ int32_t dn_tmp_2=((int32_t)(9ULL));
if((__int128)dn_tmp_2<(-((__int128)1<<31))||(__int128)dn_tmp_2>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_2; });
(dn_tmp_0 == dn_tmp_1); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static bool dn_cb_network_was_cancelled(dn_r_NetworkError const * v_error) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_network_was_cancelled(v_error);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_network_truncated(dn_r_NetworkError const * v_error) {
{
{ bool dn_tmp_3=({ int32_t dn_tmp_0=(v_error)->f_kind;
int32_t dn_tmp_1=({ int32_t dn_tmp_2=((int32_t)(10ULL));
if((__int128)dn_tmp_2<(-((__int128)1<<31))||(__int128)dn_tmp_2>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_2; });
(dn_tmp_0 == dn_tmp_1); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static bool dn_cb_network_truncated(dn_r_NetworkError const * v_error) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_network_truncated(v_error);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_utf8 dn_f_network_error_name(dn_r_NetworkError const * v_error) {
{
if(({ int32_t dn_tmp_0=(v_error)->f_kind;
int32_t dn_tmp_1=({ int32_t dn_tmp_2=((int32_t)(0ULL));
if((__int128)dn_tmp_2<(-((__int128)1<<31))||(__int128)dn_tmp_2>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_2; });
(dn_tmp_0 == dn_tmp_1); })){
{ dn_utf8 dn_tmp_3=((dn_utf8){(const uint8_t*)"ok", 2});return dn_tmp_3; }
}
else{
}
if(({ int32_t dn_tmp_4=(v_error)->f_kind;
int32_t dn_tmp_5=({ int32_t dn_tmp_6=((int32_t)(1ULL));
if((__int128)dn_tmp_6<(-((__int128)1<<31))||(__int128)dn_tmp_6>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_6; });
(dn_tmp_4 == dn_tmp_5); })){
{ dn_utf8 dn_tmp_7=((dn_utf8){(const uint8_t*)"would_block", 11});return dn_tmp_7; }
}
else{
}
if(({ int32_t dn_tmp_8=(v_error)->f_kind;
int32_t dn_tmp_9=({ int32_t dn_tmp_10=((int32_t)(2ULL));
if((__int128)dn_tmp_10<(-((__int128)1<<31))||(__int128)dn_tmp_10>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_10; });
(dn_tmp_8 == dn_tmp_9); })){
{ dn_utf8 dn_tmp_11=((dn_utf8){(const uint8_t*)"end_of_stream", 13});return dn_tmp_11; }
}
else{
}
if(({ int32_t dn_tmp_12=(v_error)->f_kind;
int32_t dn_tmp_13=({ int32_t dn_tmp_14=((int32_t)(3ULL));
if((__int128)dn_tmp_14<(-((__int128)1<<31))||(__int128)dn_tmp_14>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_14; });
(dn_tmp_12 == dn_tmp_13); })){
{ dn_utf8 dn_tmp_15=((dn_utf8){(const uint8_t*)"timeout", 7});return dn_tmp_15; }
}
else{
}
if(({ int32_t dn_tmp_16=(v_error)->f_kind;
int32_t dn_tmp_17=({ int32_t dn_tmp_18=((int32_t)(4ULL));
if((__int128)dn_tmp_18<(-((__int128)1<<31))||(__int128)dn_tmp_18>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_18; });
(dn_tmp_16 == dn_tmp_17); })){
{ dn_utf8 dn_tmp_19=((dn_utf8){(const uint8_t*)"invalid_argument_or_state", 25});return dn_tmp_19; }
}
else{
}
if(({ int32_t dn_tmp_20=(v_error)->f_kind;
int32_t dn_tmp_21=({ int32_t dn_tmp_22=((int32_t)(5ULL));
if((__int128)dn_tmp_22<(-((__int128)1<<31))||(__int128)dn_tmp_22>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_22; });
(dn_tmp_20 == dn_tmp_21); })){
{ dn_utf8 dn_tmp_23=((dn_utf8){(const uint8_t*)"system_or_provider_error", 24});return dn_tmp_23; }
}
else{
}
if(({ int32_t dn_tmp_24=(v_error)->f_kind;
int32_t dn_tmp_25=({ int32_t dn_tmp_26=((int32_t)(6ULL));
if((__int128)dn_tmp_26<(-((__int128)1<<31))||(__int128)dn_tmp_26>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_26; });
(dn_tmp_24 == dn_tmp_25); })){
{ dn_utf8 dn_tmp_27=((dn_utf8){(const uint8_t*)"unsupported", 11});return dn_tmp_27; }
}
else{
}
if(({ int32_t dn_tmp_28=(v_error)->f_kind;
int32_t dn_tmp_29=({ int32_t dn_tmp_30=((int32_t)(7ULL));
if((__int128)dn_tmp_30<(-((__int128)1<<31))||(__int128)dn_tmp_30>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_30; });
(dn_tmp_28 == dn_tmp_29); })){
{ dn_utf8 dn_tmp_31=((dn_utf8){(const uint8_t*)"resource_or_length_limit", 24});return dn_tmp_31; }
}
else{
}
if(({ int32_t dn_tmp_32=(v_error)->f_kind;
int32_t dn_tmp_33=({ int32_t dn_tmp_34=((int32_t)(8ULL));
if((__int128)dn_tmp_34<(-((__int128)1<<31))||(__int128)dn_tmp_34>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_34; });
(dn_tmp_32 == dn_tmp_33); })){
{ dn_utf8 dn_tmp_35=((dn_utf8){(const uint8_t*)"tls_error", 9});return dn_tmp_35; }
}
else{
}
if(({ int32_t dn_tmp_36=(v_error)->f_kind;
int32_t dn_tmp_37=({ int32_t dn_tmp_38=((int32_t)(9ULL));
if((__int128)dn_tmp_38<(-((__int128)1<<31))||(__int128)dn_tmp_38>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_38; });
(dn_tmp_36 == dn_tmp_37); })){
{ dn_utf8 dn_tmp_39=((dn_utf8){(const uint8_t*)"cancelled", 9});return dn_tmp_39; }
}
else{
}
if(({ int32_t dn_tmp_40=(v_error)->f_kind;
int32_t dn_tmp_41=({ int32_t dn_tmp_42=((int32_t)(10ULL));
if((__int128)dn_tmp_42<(-((__int128)1<<31))||(__int128)dn_tmp_42>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_42; });
(dn_tmp_40 == dn_tmp_41); })){
{ dn_utf8 dn_tmp_43=((dn_utf8){(const uint8_t*)"truncated_datagram", 18});return dn_tmp_43; }
}
else{
}
if(({ int32_t dn_tmp_44=(v_error)->f_kind;
int32_t dn_tmp_45=({ int32_t dn_tmp_46=((int32_t)(11ULL));
if((__int128)dn_tmp_46<(-((__int128)1<<31))||(__int128)dn_tmp_46>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_46; });
(dn_tmp_44 == dn_tmp_45); })){
{ dn_utf8 dn_tmp_47=((dn_utf8){(const uint8_t*)"protocol_mismatch", 17});return dn_tmp_47; }
}
else{
}
{ dn_utf8 dn_tmp_48=((dn_utf8){(const uint8_t*)"unknown_network_error", 21});return dn_tmp_48; }
}

dn_fail("missing native return");
}
static dn_utf8 dn_cb_network_error_name(dn_r_NetworkError const * v_error) {
 if(!dn_enter()){return (dn_utf8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_utf8){0};}
 dn_utf8 dn_result=dn_f_network_error_name(v_error);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_socket_transfer(dn_r_NetIo v_result,uint32_t v_operation) {
{
{ dn_r_NetworkTransfer dn_tmp_5=({ size_t dn_tmp_0=(v_result).f_count;
dn_r_NetworkError dn_tmp_1=({ int32_t dn_tmp_2=(v_result).f_status;
int32_t dn_tmp_3=(v_result).f_system;
uint32_t dn_tmp_4=v_operation;
((dn_r_NetworkError){dn_tmp_2,dn_tmp_3,dn_tmp_4}); });
((dn_r_NetworkTransfer){dn_tmp_0,dn_tmp_1}); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_socket_transfer(dn_r_NetIo v_result,uint32_t v_operation) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_socket_transfer(v_result,v_operation);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkReady dn_f_socket_ready(dn_r_NetIo v_result) {
{
{ dn_r_NetworkReady dn_tmp_6=({ uint32_t dn_tmp_0=({ size_t dn_tmp_1=(v_result).f_count;
if((unsigned __int128)dn_tmp_1>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_1; });
dn_r_NetworkError dn_tmp_2=({ int32_t dn_tmp_3=(v_result).f_status;
int32_t dn_tmp_4=(v_result).f_system;
uint32_t dn_tmp_5=((uint32_t)(8ULL));
((dn_r_NetworkError){dn_tmp_3,dn_tmp_4,dn_tmp_5}); });
((dn_r_NetworkReady){dn_tmp_0,dn_tmp_2}); });return dn_tmp_6; }
}

dn_fail("missing native return");
}
static dn_r_NetworkReady dn_cb_socket_ready(dn_r_NetIo v_result) {
 if(!dn_enter()){return (dn_r_NetworkReady){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkReady){0};}
 dn_r_NetworkReady dn_result=dn_f_socket_ready(v_result);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_SocketAddressResult dn_f_socket_address_result(dn_r_NetResolved v_result,uint32_t v_operation) {
{
{ dn_r_SocketAddressResult dn_tmp_6=({ dn_r_SocketAddress dn_tmp_0=({ dn_r_NetAddress dn_tmp_1=(v_result).f_address;
((dn_r_SocketAddress){dn_tmp_1}); });
dn_r_NetworkError dn_tmp_2=({ int32_t dn_tmp_3=(v_result).f_status;
int32_t dn_tmp_4=(v_result).f_system;
uint32_t dn_tmp_5=v_operation;
((dn_r_NetworkError){dn_tmp_3,dn_tmp_4,dn_tmp_5}); });
((dn_r_SocketAddressResult){dn_tmp_0,dn_tmp_2}); });return dn_tmp_6; }
}

dn_fail("missing native return");
}
static dn_r_SocketAddressResult dn_cb_socket_address_result(dn_r_NetResolved v_result,uint32_t v_operation) {
 if(!dn_enter()){return (dn_r_SocketAddressResult){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_SocketAddressResult){0};}
 dn_r_SocketAddressResult dn_result=dn_f_socket_address_result(v_result,v_operation);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_SocketAddress dn_f_socket_address_v4(uint8_t v_a,uint8_t v_b,uint8_t v_c,uint8_t v_d,uint16_t v_port) {
{
dn_r_NetAddress v_address=({ ((dn_r_NetAddress){0}); });
{ uint32_t dn_tmp_1=((uint32_t)(4ULL));uint32_t *dn_tmp_0=&((v_address).f_family);*dn_tmp_0=dn_tmp_1; }
{ uint32_t dn_tmp_4=({ uint16_t dn_tmp_2=v_port;
if((unsigned __int128)dn_tmp_2>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_2; });uint32_t *dn_tmp_3=&((v_address).f_port);*dn_tmp_3=dn_tmp_4; }
{ uint8_t dn_tmp_8=v_a;uint8_t *dn_tmp_7=&((*({ dn_t_arr16_U8 *dn_tmp_5=&((v_address).f_bytes);size_t dn_tmp_6=((size_t)(0ULL));&dn_tmp_5->data[dn_index(dn_tmp_6,16)]; })));*dn_tmp_7=dn_tmp_8; }
{ uint8_t dn_tmp_12=v_b;uint8_t *dn_tmp_11=&((*({ dn_t_arr16_U8 *dn_tmp_9=&((v_address).f_bytes);size_t dn_tmp_10=((size_t)(1ULL));&dn_tmp_9->data[dn_index(dn_tmp_10,16)]; })));*dn_tmp_11=dn_tmp_12; }
{ uint8_t dn_tmp_16=v_c;uint8_t *dn_tmp_15=&((*({ dn_t_arr16_U8 *dn_tmp_13=&((v_address).f_bytes);size_t dn_tmp_14=((size_t)(2ULL));&dn_tmp_13->data[dn_index(dn_tmp_14,16)]; })));*dn_tmp_15=dn_tmp_16; }
{ uint8_t dn_tmp_20=v_d;uint8_t *dn_tmp_19=&((*({ dn_t_arr16_U8 *dn_tmp_17=&((v_address).f_bytes);size_t dn_tmp_18=((size_t)(3ULL));&dn_tmp_17->data[dn_index(dn_tmp_18,16)]; })));*dn_tmp_19=dn_tmp_20; }
{ dn_r_SocketAddress dn_tmp_22=({ dn_r_NetAddress dn_tmp_21=v_address;
((dn_r_SocketAddress){dn_tmp_21}); });return dn_tmp_22; }
}

dn_fail("missing native return");
}
static dn_r_SocketAddress dn_cb_socket_address_v4(uint8_t v_a,uint8_t v_b,uint8_t v_c,uint8_t v_d,uint16_t v_port) {
 if(!dn_enter()){return (dn_r_SocketAddress){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_SocketAddress){0};}
 dn_r_SocketAddress dn_result=dn_f_socket_address_v4(v_a,v_b,v_c,v_d,v_port);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_SocketAddress dn_f_socket_address_v6(dn_t_arr16_U8 v_bytes,uint16_t v_port,uint32_t v_scope) {
{
{ dn_r_SocketAddress dn_tmp_6=({ dn_r_NetAddress dn_tmp_0=({ uint32_t dn_tmp_1=((uint32_t)(6ULL));
uint32_t dn_tmp_2=({ uint16_t dn_tmp_3=v_port;
if((unsigned __int128)dn_tmp_3>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_3; });
uint32_t dn_tmp_4=v_scope;
dn_t_arr16_U8 dn_tmp_5=v_bytes;
((dn_r_NetAddress){dn_tmp_1,dn_tmp_2,dn_tmp_4,dn_tmp_5}); });
((dn_r_SocketAddress){dn_tmp_0}); });return dn_tmp_6; }
}

dn_fail("missing native return");
}
static dn_r_SocketAddress dn_cb_socket_address_v6(dn_t_arr16_U8 v_bytes,uint16_t v_port,uint32_t v_scope) {
 if(!dn_enter()){return (dn_r_SocketAddress){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_SocketAddress){0};}
 dn_r_SocketAddress dn_result=dn_f_socket_address_v6(v_bytes,v_port,v_scope);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_SocketAddressResult dn_f_socket_address_resolve(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_family,uint32_t v_index) {
{
{
{ dn_r_SocketAddressResult dn_tmp_7=({ dn_r_NetResolved dn_tmp_0=({ dn_r_NetText dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_host;
dn_f_net_bytes(dn_tmp_2); });
uint16_t dn_tmp_3=v_port;
uint32_t dn_tmp_4=v_family;
uint32_t dn_tmp_5=v_index;
({ dn_r_NetResolved dn_foreign=dn_ext_dlm_net_resolve_family(dn_tmp_1,dn_tmp_3,dn_tmp_4,dn_tmp_5);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_6=((uint32_t)(12ULL));
dn_f_socket_address_result(dn_tmp_0,dn_tmp_6); });return dn_tmp_7; }
}
}

dn_fail("missing native return");
}
static dn_r_SocketAddressResult dn_cb_socket_address_resolve(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_family,uint32_t v_index) {
 if(!dn_enter()){return (dn_r_SocketAddressResult){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_SocketAddressResult){0};}
 dn_r_SocketAddressResult dn_result=dn_f_socket_address_resolve(v_host,v_port,v_family,v_index);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_socket_address_write(dn_r_SocketAddress v_address,dn_t_buf_U8 * v_bytes,bool v_with_port) {
{
{
{ dn_r_NetworkTransfer dn_tmp_8=({ dn_r_NetIo dn_tmp_0=({ dn_r_NetAddress dn_tmp_1=(v_address).f_value;
uint8_t * dn_tmp_2=({ dn_t_buf_U8 * dn_tmp_3=v_bytes;
dn_f_net_output(dn_tmp_3); });
size_t dn_tmp_4=({ dn_t_buf_U8 * dn_tmp_5=v_bytes;
(*(dn_tmp_5)).len; });
bool dn_tmp_6=v_with_port;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_address_text(dn_tmp_1,dn_tmp_2,dn_tmp_4,dn_tmp_6);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_7=((uint32_t)(17ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_7); });return dn_tmp_8; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_socket_address_write(dn_r_SocketAddress v_address,dn_t_buf_U8 * v_bytes,bool v_with_port) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_socket_address_write(v_address,v_bytes,v_with_port);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_SocketOptions dn_f_socket_options(bool v_no_delay,bool v_keep_alive,uint32_t v_send_buffer,uint32_t v_receive_buffer) {
{
uint32_t v_n=((uint32_t)(0ULL));
uint32_t v_k=((uint32_t)(0ULL));
if(v_no_delay){
{ uint32_t dn_tmp_1=((uint32_t)(1ULL));uint32_t *dn_tmp_0=&(v_n);*dn_tmp_0=dn_tmp_1; }
}
else{
}
if(v_keep_alive){
{ uint32_t dn_tmp_3=((uint32_t)(1ULL));uint32_t *dn_tmp_2=&(v_k);*dn_tmp_2=dn_tmp_3; }
}
else{
}
{ dn_r_SocketOptions dn_tmp_8=({ uint32_t dn_tmp_4=v_n;
uint32_t dn_tmp_5=v_k;
uint32_t dn_tmp_6=v_send_buffer;
uint32_t dn_tmp_7=v_receive_buffer;
((dn_r_SocketOptions){dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7}); });return dn_tmp_8; }
}

dn_fail("missing native return");
}
static dn_r_SocketOptions dn_cb_socket_options(bool v_no_delay,bool v_keep_alive,uint32_t v_send_buffer,uint32_t v_receive_buffer) {
 if(!dn_enter()){return (dn_r_SocketOptions){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_SocketOptions){0};}
 dn_r_SocketOptions dn_result=dn_f_socket_options(v_no_delay,v_keep_alive,v_send_buffer,v_receive_buffer);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TcpSocket dn_f_tcp_connect(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_timeout_ms) {
{
{ dn_r_TcpSocket dn_tmp_5=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=({ dn_t_slice0_U8 dn_tmp_1=v_host;
uint16_t dn_tmp_2=v_port;
uint32_t dn_tmp_3=v_timeout_ms;
dn_f_socket_tcp_bytes(dn_tmp_1,dn_tmp_2,dn_tmp_3); });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_0);
uint32_t dn_tmp_4=((uint32_t)(1ULL));
((dn_r_TcpSocket){dn_tmp_0,dn_tmp_4}); });dn_adopt_r9_TcpSocket(&dn_tmp_5);return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_TcpSocket dn_cb_tcp_connect(dn_t_slice0_U8 v_host,uint16_t v_port,uint32_t v_timeout_ms) {
 if(!dn_enter()){return (dn_r_TcpSocket){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TcpSocket){0};}
 dn_r_TcpSocket dn_result=dn_f_tcp_connect(v_host,v_port,v_timeout_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TcpSocket dn_f_tcp_connect_address(dn_r_SocketAddress v_address,uint32_t v_timeout_ms) {
{
{
{ dn_r_TcpSocket dn_tmp_5=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_1=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_1,({ dn_r_NetAddress dn_tmp_2=(v_address).f_value;
uint32_t dn_tmp_3=v_timeout_ms;
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_connect_address(dn_tmp_2,dn_tmp_3);dn_fp_default();dn_foreign; }); })); });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_0);
uint32_t dn_tmp_4=((uint32_t)(1ULL));
((dn_r_TcpSocket){dn_tmp_0,dn_tmp_4}); });dn_adopt_r9_TcpSocket(&dn_tmp_5);return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_TcpSocket dn_cb_tcp_connect_address(dn_r_SocketAddress v_address,uint32_t v_timeout_ms) {
 if(!dn_enter()){return (dn_r_TcpSocket){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TcpSocket){0};}
 dn_r_TcpSocket dn_result=dn_f_tcp_connect_address(v_address,v_timeout_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TcpListener dn_f_tcp_listen(dn_r_SocketAddress v_address,uint32_t v_backlog,bool v_dual_stack) {
{
{
{ dn_r_TcpListener dn_tmp_6=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_1=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_1,({ dn_r_NetAddress dn_tmp_2=(v_address).f_value;
uint32_t dn_tmp_3=((uint32_t)(2ULL));
uint32_t dn_tmp_4=v_backlog;
bool dn_tmp_5=v_dual_stack;
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_bind_address(dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5);dn_fp_default();dn_foreign; }); })); });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_0);
((dn_r_TcpListener){dn_tmp_0}); });dn_adopt_r11_TcpListener(&dn_tmp_6);return dn_tmp_6; }
}
}

dn_fail("missing native return");
}
static dn_r_TcpListener dn_cb_tcp_listen(dn_r_SocketAddress v_address,uint32_t v_backlog,bool v_dual_stack) {
 if(!dn_enter()){return (dn_r_TcpListener){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TcpListener){0};}
 dn_r_TcpListener dn_result=dn_f_tcp_listen(v_address,v_backlog,v_dual_stack);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TcpSocket dn_f_tcp_accept(dn_r_TcpListener * v_listener) {
{
{ dn_r_TcpSocket dn_tmp_3=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_1=(&((v_listener)->f_inner));
dn_f_socket_accept(dn_tmp_1); });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_0);
uint32_t dn_tmp_2=((uint32_t)(3ULL));
((dn_r_TcpSocket){dn_tmp_0,dn_tmp_2}); });dn_adopt_r9_TcpSocket(&dn_tmp_3);return dn_tmp_3; }
}

dn_fail("missing native return");
}
static dn_r_TcpSocket dn_cb_tcp_accept(dn_r_TcpListener * v_listener) {
 if(!dn_enter()){return (dn_r_TcpSocket){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TcpSocket){0};}
 dn_r_TcpSocket dn_result=dn_f_tcp_accept(v_listener);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_UdpSocket dn_f_udp_bind(dn_r_SocketAddress v_address,bool v_dual_stack) {
{
{
{ dn_r_UdpSocket dn_tmp_6=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_1=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_1,({ dn_r_NetAddress dn_tmp_2=(v_address).f_value;
uint32_t dn_tmp_3=((uint32_t)(3ULL));
uint32_t dn_tmp_4=((uint32_t)(0ULL));
bool dn_tmp_5=v_dual_stack;
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_bind_address(dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5);dn_fp_default();dn_foreign; }); })); });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_0);
((dn_r_UdpSocket){dn_tmp_0}); });dn_adopt_r9_UdpSocket(&dn_tmp_6);return dn_tmp_6; }
}
}

dn_fail("missing native return");
}
static dn_r_UdpSocket dn_cb_udp_bind(dn_r_SocketAddress v_address,bool v_dual_stack) {
 if(!dn_enter()){return (dn_r_UdpSocket){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_UdpSocket){0};}
 dn_r_UdpSocket dn_result=dn_f_udp_bind(v_address,v_dual_stack);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TlsSocket dn_f_tls_connect(dn_t_slice0_U8 v_url,uint32_t v_timeout_ms,dn_t_slice0_U8 v_ca_file,dn_t_slice0_U8 v_pin) {
{
{ dn_r_TlsSocket dn_tmp_5=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=({ dn_t_slice0_U8 dn_tmp_1=v_url;
uint32_t dn_tmp_2=v_timeout_ms;
dn_t_slice0_U8 dn_tmp_3=v_ca_file;
dn_t_slice0_U8 dn_tmp_4=v_pin;
dn_f_socket_tls_bytes(dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4); });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_0);
((dn_r_TlsSocket){dn_tmp_0}); });dn_adopt_r9_TlsSocket(&dn_tmp_5);return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_TlsSocket dn_cb_tls_connect(dn_t_slice0_U8 v_url,uint32_t v_timeout_ms,dn_t_slice0_U8 v_ca_file,dn_t_slice0_U8 v_pin) {
 if(!dn_enter()){return (dn_r_TlsSocket){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TlsSocket){0};}
 dn_r_TlsSocket dn_result=dn_f_tls_connect(v_url,v_timeout_ms,v_ca_file,v_pin);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkError dn_f_tcp_error(dn_r_TcpSocket const * v_socket) {
{
{ dn_r_NetworkError dn_tmp_5=({ int32_t dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_1=(&((v_socket)->f_inner));
dn_f_socket_status(dn_tmp_1); });
int32_t dn_tmp_2=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_3=(&((v_socket)->f_inner));
dn_f_socket_system_error(dn_tmp_3); });
uint32_t dn_tmp_4=(v_socket)->f_opened_by;
((dn_r_NetworkError){dn_tmp_0,dn_tmp_2,dn_tmp_4}); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_NetworkError dn_cb_tcp_error(dn_r_TcpSocket const * v_socket) {
 if(!dn_enter()){return (dn_r_NetworkError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkError){0};}
 dn_r_NetworkError dn_result=dn_f_tcp_error(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkError dn_f_listener_error(dn_r_TcpListener const * v_socket) {
{
{ dn_r_NetworkError dn_tmp_5=({ int32_t dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_1=(&((v_socket)->f_inner));
dn_f_socket_status(dn_tmp_1); });
int32_t dn_tmp_2=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_3=(&((v_socket)->f_inner));
dn_f_socket_system_error(dn_tmp_3); });
uint32_t dn_tmp_4=((uint32_t)(2ULL));
((dn_r_NetworkError){dn_tmp_0,dn_tmp_2,dn_tmp_4}); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_NetworkError dn_cb_listener_error(dn_r_TcpListener const * v_socket) {
 if(!dn_enter()){return (dn_r_NetworkError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkError){0};}
 dn_r_NetworkError dn_result=dn_f_listener_error(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkError dn_f_udp_error(dn_r_UdpSocket const * v_socket) {
{
{ dn_r_NetworkError dn_tmp_5=({ int32_t dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_1=(&((v_socket)->f_inner));
dn_f_socket_status(dn_tmp_1); });
int32_t dn_tmp_2=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_3=(&((v_socket)->f_inner));
dn_f_socket_system_error(dn_tmp_3); });
uint32_t dn_tmp_4=((uint32_t)(4ULL));
((dn_r_NetworkError){dn_tmp_0,dn_tmp_2,dn_tmp_4}); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_NetworkError dn_cb_udp_error(dn_r_UdpSocket const * v_socket) {
 if(!dn_enter()){return (dn_r_NetworkError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkError){0};}
 dn_r_NetworkError dn_result=dn_f_udp_error(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkError dn_f_tls_error(dn_r_TlsSocket const * v_socket) {
{
{ dn_r_NetworkError dn_tmp_5=({ int32_t dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_1=(&((v_socket)->f_inner));
dn_f_socket_status(dn_tmp_1); });
int32_t dn_tmp_2=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_3=(&((v_socket)->f_inner));
dn_f_socket_system_error(dn_tmp_3); });
uint32_t dn_tmp_4=((uint32_t)(5ULL));
((dn_r_NetworkError){dn_tmp_0,dn_tmp_2,dn_tmp_4}); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_NetworkError dn_cb_tls_error(dn_r_TlsSocket const * v_socket) {
 if(!dn_enter()){return (dn_r_NetworkError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkError){0};}
 dn_r_NetworkError dn_result=dn_f_tls_error(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_tcp_close(dn_r_TcpSocket v_socket) {
dn_r_TcpSocket volatile dn_param_socket=v_socket;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r9_TcpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
(void)(({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_2=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_3=(dn_param_socket).f_inner;(dn_param_socket).f_inner=(dn_t_resource_r9_NetOpened_net_dispose *){0};dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_3);dn_tmp_3; });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_2);
({ dn_drop_resource_r9_NetOpened_net_dispose(&dn_tmp_2);0; }); }));
}

dn_drop_r9_TcpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
return 0;
}
static void dn_cb_tcp_close(dn_r_TcpSocket v_socket) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_tcp_close(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_listener_close(dn_r_TcpListener v_socket) {
dn_r_TcpListener volatile dn_param_socket=v_socket;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r11_TcpListener(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
(void)(({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_2=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_3=(dn_param_socket).f_inner;(dn_param_socket).f_inner=(dn_t_resource_r9_NetOpened_net_dispose *){0};dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_3);dn_tmp_3; });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_2);
({ dn_drop_resource_r9_NetOpened_net_dispose(&dn_tmp_2);0; }); }));
}

dn_drop_r11_TcpListener(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
return 0;
}
static void dn_cb_listener_close(dn_r_TcpListener v_socket) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_listener_close(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_udp_close(dn_r_UdpSocket v_socket) {
dn_r_UdpSocket volatile dn_param_socket=v_socket;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r9_UdpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
(void)(({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_2=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_3=(dn_param_socket).f_inner;(dn_param_socket).f_inner=(dn_t_resource_r9_NetOpened_net_dispose *){0};dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_3);dn_tmp_3; });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_2);
({ dn_drop_resource_r9_NetOpened_net_dispose(&dn_tmp_2);0; }); }));
}

dn_drop_r9_UdpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
return 0;
}
static void dn_cb_udp_close(dn_r_UdpSocket v_socket) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_udp_close(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_tls_close(dn_r_TlsSocket v_socket) {
dn_r_TlsSocket volatile dn_param_socket=v_socket;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r9_TlsSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
(void)(({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_2=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_3=(dn_param_socket).f_inner;(dn_param_socket).f_inner=(dn_t_resource_r9_NetOpened_net_dispose *){0};dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_3);dn_tmp_3; });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_2);
({ dn_drop_resource_r9_NetOpened_net_dispose(&dn_tmp_2);0; }); }));
}

dn_drop_r9_TlsSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
return 0;
}
static void dn_cb_tls_close(dn_r_TlsSocket v_socket) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_tls_close(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static dn_r_NetworkTransfer dn_f_tcp_read(dn_r_TcpSocket * v_socket,dn_t_buf_U8 * v_bytes) {
{
{ dn_r_NetworkTransfer dn_tmp_4=({ dn_r_NetIo dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_1=(&((v_socket)->f_inner));
dn_t_buf_U8 * dn_tmp_2=v_bytes;
dn_f_socket_read(dn_tmp_1,dn_tmp_2); });
uint32_t dn_tmp_3=((uint32_t)(6ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_tcp_read(dn_r_TcpSocket * v_socket,dn_t_buf_U8 * v_bytes) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_tcp_read(v_socket,v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_tcp_write(dn_r_TcpSocket * v_socket,dn_t_slice0_U8 v_bytes) {
{
{ dn_r_NetworkTransfer dn_tmp_4=({ dn_r_NetIo dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_1=(&((v_socket)->f_inner));
dn_t_slice0_U8 dn_tmp_2=v_bytes;
dn_f_socket_write(dn_tmp_1,dn_tmp_2); });
uint32_t dn_tmp_3=((uint32_t)(7ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_tcp_write(dn_r_TcpSocket * v_socket,dn_t_slice0_U8 v_bytes) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_tcp_write(v_socket,v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_tcp_read_exact(dn_r_TcpSocket * v_socket,dn_t_buf_U8 * v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
{ dn_r_NetworkTransfer dn_tmp_6=({ dn_r_NetIo dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_1=(&((v_socket)->f_inner));
dn_t_buf_U8 * dn_tmp_2=v_bytes;
uint32_t dn_tmp_3=v_timeout_ms;
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_4=v_cancel;
dn_f_socket_read_exact(dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4); });
uint32_t dn_tmp_5=((uint32_t)(6ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_5); });return dn_tmp_6; }
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_tcp_read_exact(dn_r_TcpSocket * v_socket,dn_t_buf_U8 * v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_tcp_read_exact(v_socket,v_bytes,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_tcp_write_all(dn_r_TcpSocket * v_socket,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
{ dn_r_NetworkTransfer dn_tmp_6=({ dn_r_NetIo dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_1=(&((v_socket)->f_inner));
dn_t_slice0_U8 dn_tmp_2=v_bytes;
uint32_t dn_tmp_3=v_timeout_ms;
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_4=v_cancel;
dn_f_socket_write_all(dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4); });
uint32_t dn_tmp_5=((uint32_t)(7ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_5); });return dn_tmp_6; }
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_tcp_write_all(dn_r_TcpSocket * v_socket,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_tcp_write_all(v_socket,v_bytes,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkReady dn_f_tcp_wait(dn_r_TcpSocket * v_socket,uint32_t v_events,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
{ dn_r_NetworkReady dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_1=(&((v_socket)->f_inner));
uint32_t dn_tmp_2=v_events;
uint32_t dn_tmp_3=v_timeout_ms;
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_4=v_cancel;
dn_f_socket_wait(dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4); });
dn_f_socket_ready(dn_tmp_0); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_NetworkReady dn_cb_tcp_wait(dn_r_TcpSocket * v_socket,uint32_t v_events,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetworkReady){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkReady){0};}
 dn_r_NetworkReady dn_result=dn_f_tcp_wait(v_socket,v_events,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkReady dn_f_listener_wait(dn_r_TcpListener * v_socket,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
{ dn_r_NetworkReady dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_1=(&((v_socket)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(1ULL));
uint32_t dn_tmp_3=v_timeout_ms;
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_4=v_cancel;
dn_f_socket_wait(dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4); });
dn_f_socket_ready(dn_tmp_0); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_NetworkReady dn_cb_listener_wait(dn_r_TcpListener * v_socket,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetworkReady){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkReady){0};}
 dn_r_NetworkReady dn_result=dn_f_listener_wait(v_socket,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkReady dn_f_udp_wait(dn_r_UdpSocket * v_socket,uint32_t v_events,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
{ dn_r_NetworkReady dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_1=(&((v_socket)->f_inner));
uint32_t dn_tmp_2=v_events;
uint32_t dn_tmp_3=v_timeout_ms;
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_4=v_cancel;
dn_f_socket_wait(dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4); });
dn_f_socket_ready(dn_tmp_0); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_NetworkReady dn_cb_udp_wait(dn_r_UdpSocket * v_socket,uint32_t v_events,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetworkReady){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkReady){0};}
 dn_r_NetworkReady dn_result=dn_f_udp_wait(v_socket,v_events,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkReady dn_f_tls_wait(dn_r_TlsSocket * v_socket,uint32_t v_events,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
{ dn_r_NetworkReady dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_1=(&((v_socket)->f_inner));
uint32_t dn_tmp_2=v_events;
uint32_t dn_tmp_3=v_timeout_ms;
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_4=v_cancel;
dn_f_socket_wait(dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4); });
dn_f_socket_ready(dn_tmp_0); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_NetworkReady dn_cb_tls_wait(dn_r_TlsSocket * v_socket,uint32_t v_events,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetworkReady){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkReady){0};}
 dn_r_NetworkReady dn_result=dn_f_tls_wait(v_socket,v_events,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkError dn_f_tcp_shutdown_write(dn_r_TcpSocket * v_socket) {
{
dn_r_NetIo v_result=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=(&((v_socket)->f_inner));
dn_f_socket_shutdown_write(dn_tmp_0); });
{ dn_r_NetworkError dn_tmp_4=({ int32_t dn_tmp_1=(v_result).f_status;
int32_t dn_tmp_2=(v_result).f_system;
uint32_t dn_tmp_3=((uint32_t)(9ULL));
((dn_r_NetworkError){dn_tmp_1,dn_tmp_2,dn_tmp_3}); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_NetworkError dn_cb_tcp_shutdown_write(dn_r_TcpSocket * v_socket) {
 if(!dn_enter()){return (dn_r_NetworkError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkError){0};}
 dn_r_NetworkError dn_result=dn_f_tcp_shutdown_write(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_SocketAddressResult dn_f_tcp_local(dn_r_TcpSocket const * v_socket) {
{
{ dn_r_SocketAddressResult dn_tmp_3=({ dn_r_NetResolved dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_1=(&((v_socket)->f_inner));
dn_f_socket_local(dn_tmp_1); });
uint32_t dn_tmp_2=((uint32_t)(10ULL));
dn_f_socket_address_result(dn_tmp_0,dn_tmp_2); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static dn_r_SocketAddressResult dn_cb_tcp_local(dn_r_TcpSocket const * v_socket) {
 if(!dn_enter()){return (dn_r_SocketAddressResult){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_SocketAddressResult){0};}
 dn_r_SocketAddressResult dn_result=dn_f_tcp_local(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_SocketAddressResult dn_f_listener_local(dn_r_TcpListener const * v_socket) {
{
{ dn_r_SocketAddressResult dn_tmp_3=({ dn_r_NetResolved dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_1=(&((v_socket)->f_inner));
dn_f_socket_local(dn_tmp_1); });
uint32_t dn_tmp_2=((uint32_t)(10ULL));
dn_f_socket_address_result(dn_tmp_0,dn_tmp_2); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static dn_r_SocketAddressResult dn_cb_listener_local(dn_r_TcpListener const * v_socket) {
 if(!dn_enter()){return (dn_r_SocketAddressResult){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_SocketAddressResult){0};}
 dn_r_SocketAddressResult dn_result=dn_f_listener_local(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_SocketAddressResult dn_f_udp_local(dn_r_UdpSocket const * v_socket) {
{
{ dn_r_SocketAddressResult dn_tmp_3=({ dn_r_NetResolved dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_1=(&((v_socket)->f_inner));
dn_f_socket_local(dn_tmp_1); });
uint32_t dn_tmp_2=((uint32_t)(10ULL));
dn_f_socket_address_result(dn_tmp_0,dn_tmp_2); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static dn_r_SocketAddressResult dn_cb_udp_local(dn_r_UdpSocket const * v_socket) {
 if(!dn_enter()){return (dn_r_SocketAddressResult){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_SocketAddressResult){0};}
 dn_r_SocketAddressResult dn_result=dn_f_udp_local(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_SocketAddressResult dn_f_tcp_peer(dn_r_TcpSocket const * v_socket) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_0=(&((v_socket)->f_inner));
(&((*dn_tmp_0)->value)); });
{
{ dn_r_SocketAddressResult dn_tmp_4=({ dn_r_NetResolved dn_tmp_1=({ void * dn_tmp_2=(v_state)->f_handle;
({ dn_r_NetResolved dn_foreign=dn_ext_dlm_net_peer(dn_tmp_2);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_3=((uint32_t)(11ULL));
dn_f_socket_address_result(dn_tmp_1,dn_tmp_3); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_SocketAddressResult dn_cb_tcp_peer(dn_r_TcpSocket const * v_socket) {
 if(!dn_enter()){return (dn_r_SocketAddressResult){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_SocketAddressResult){0};}
 dn_r_SocketAddressResult dn_result=dn_f_tcp_peer(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_SocketAddressResult dn_f_tls_peer(dn_r_TlsSocket const * v_socket) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_0=(&((v_socket)->f_inner));
(&((*dn_tmp_0)->value)); });
{
{ dn_r_SocketAddressResult dn_tmp_4=({ dn_r_NetResolved dn_tmp_1=({ void * dn_tmp_2=(v_state)->f_handle;
({ dn_r_NetResolved dn_foreign=dn_ext_dlm_net_peer(dn_tmp_2);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_3=((uint32_t)(11ULL));
dn_f_socket_address_result(dn_tmp_1,dn_tmp_3); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_SocketAddressResult dn_cb_tls_peer(dn_r_TlsSocket const * v_socket) {
 if(!dn_enter()){return (dn_r_SocketAddressResult){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_SocketAddressResult){0};}
 dn_r_SocketAddressResult dn_result=dn_f_tls_peer(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkError dn_f_tcp_configure(dn_r_TcpSocket * v_socket,dn_r_SocketOptions v_options) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_0=(&((v_socket)->f_inner));
(&((*dn_tmp_0)->value)); });
{
dn_r_NetIo v_result=({ void * dn_tmp_1=(v_state)->f_handle;
dn_r_SocketOptions dn_tmp_2=v_options;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_configure(dn_tmp_1,dn_tmp_2);dn_fp_default();dn_foreign; }); });
{ dn_r_NetworkError dn_tmp_6=({ int32_t dn_tmp_3=(v_result).f_status;
int32_t dn_tmp_4=(v_result).f_system;
uint32_t dn_tmp_5=((uint32_t)(13ULL));
((dn_r_NetworkError){dn_tmp_3,dn_tmp_4,dn_tmp_5}); });return dn_tmp_6; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkError dn_cb_tcp_configure(dn_r_TcpSocket * v_socket,dn_r_SocketOptions v_options) {
 if(!dn_enter()){return (dn_r_NetworkError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkError){0};}
 dn_r_NetworkError dn_result=dn_f_tcp_configure(v_socket,v_options);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkError dn_f_udp_configure(dn_r_UdpSocket * v_socket,uint32_t v_send_buffer,uint32_t v_receive_buffer) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_0=(&((v_socket)->f_inner));
(&((*dn_tmp_0)->value)); });
{
dn_r_NetIo v_result=({ void * dn_tmp_1=(v_state)->f_handle;
dn_r_SocketOptions dn_tmp_2=({ uint32_t dn_tmp_3=((uint32_t)(0ULL));
uint32_t dn_tmp_4=((uint32_t)(0ULL));
uint32_t dn_tmp_5=v_send_buffer;
uint32_t dn_tmp_6=v_receive_buffer;
((dn_r_SocketOptions){dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6}); });
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_configure(dn_tmp_1,dn_tmp_2);dn_fp_default();dn_foreign; }); });
{ dn_r_NetworkError dn_tmp_10=({ int32_t dn_tmp_7=(v_result).f_status;
int32_t dn_tmp_8=(v_result).f_system;
uint32_t dn_tmp_9=((uint32_t)(13ULL));
((dn_r_NetworkError){dn_tmp_7,dn_tmp_8,dn_tmp_9}); });return dn_tmp_10; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkError dn_cb_udp_configure(dn_r_UdpSocket * v_socket,uint32_t v_send_buffer,uint32_t v_receive_buffer) {
 if(!dn_enter()){return (dn_r_NetworkError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkError){0};}
 dn_r_NetworkError dn_result=dn_f_udp_configure(v_socket,v_send_buffer,v_receive_buffer);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_SocketOptionsResult dn_f_tcp_options(dn_r_TcpSocket const * v_socket) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_net_dispose * const * dn_tmp_0=(&((v_socket)->f_inner));
(&((*dn_tmp_0)->value)); });
{
dn_r_NativeSocketOptions v_result=({ void * dn_tmp_1=(v_state)->f_handle;
({ dn_r_NativeSocketOptions dn_foreign=dn_ext_dlm_net_get_options(dn_tmp_1);dn_fp_default();dn_foreign; }); });
{ dn_r_SocketOptionsResult dn_tmp_7=({ dn_r_SocketOptions dn_tmp_2=(v_result).f_options;
dn_r_NetworkError dn_tmp_3=({ int32_t dn_tmp_4=(v_result).f_status;
int32_t dn_tmp_5=(v_result).f_system;
uint32_t dn_tmp_6=((uint32_t)(14ULL));
((dn_r_NetworkError){dn_tmp_4,dn_tmp_5,dn_tmp_6}); });
((dn_r_SocketOptionsResult){dn_tmp_2,dn_tmp_3}); });return dn_tmp_7; }
}
}

dn_fail("missing native return");
}
static dn_r_SocketOptionsResult dn_cb_tcp_options(dn_r_TcpSocket const * v_socket) {
 if(!dn_enter()){return (dn_r_SocketOptionsResult){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_SocketOptionsResult){0};}
 dn_r_SocketOptionsResult dn_result=dn_f_tcp_options(v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_udp_send(dn_r_UdpSocket * v_socket,dn_r_SocketAddress v_peer,dn_t_slice0_U8 v_bytes) {
{
{ dn_r_NetworkTransfer dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_1=(&((v_socket)->f_inner));
dn_r_NetAddress dn_tmp_2=(v_peer).f_value;
dn_t_slice0_U8 dn_tmp_3=v_bytes;
dn_f_socket_send_datagram(dn_tmp_1,dn_tmp_2,dn_tmp_3); });
uint32_t dn_tmp_4=((uint32_t)(15ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_4); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_udp_send(dn_r_UdpSocket * v_socket,dn_r_SocketAddress v_peer,dn_t_slice0_U8 v_bytes) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_udp_send(v_socket,v_peer,v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_SocketDatagram dn_f_udp_receive(dn_r_UdpSocket * v_socket,dn_t_buf_U8 * v_bytes) {
{
dn_r_NetDatagram v_result=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_0=(&((v_socket)->f_inner));
dn_t_buf_U8 * dn_tmp_1=v_bytes;
dn_f_socket_read_datagram(dn_tmp_0,dn_tmp_1); });
{ dn_r_SocketDatagram dn_tmp_7=({ dn_r_NetworkTransfer dn_tmp_2=({ dn_r_NetIo dn_tmp_3=(v_result).f_io;
uint32_t dn_tmp_4=((uint32_t)(16ULL));
dn_f_socket_transfer(dn_tmp_3,dn_tmp_4); });
dn_r_SocketAddress dn_tmp_5=({ dn_r_NetAddress dn_tmp_6=(v_result).f_peer;
((dn_r_SocketAddress){dn_tmp_6}); });
((dn_r_SocketDatagram){dn_tmp_2,dn_tmp_5}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_SocketDatagram dn_cb_udp_receive(dn_r_UdpSocket * v_socket,dn_t_buf_U8 * v_bytes) {
 if(!dn_enter()){return (dn_r_SocketDatagram){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_SocketDatagram){0};}
 dn_r_SocketDatagram dn_result=dn_f_udp_receive(v_socket,v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_tls_read(dn_r_TlsSocket * v_socket,dn_t_buf_U8 * v_bytes) {
{
{ dn_r_NetworkTransfer dn_tmp_4=({ dn_r_NetIo dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_1=(&((v_socket)->f_inner));
dn_t_buf_U8 * dn_tmp_2=v_bytes;
dn_f_socket_read(dn_tmp_1,dn_tmp_2); });
uint32_t dn_tmp_3=((uint32_t)(6ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_tls_read(dn_r_TlsSocket * v_socket,dn_t_buf_U8 * v_bytes) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_tls_read(v_socket,v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_tls_write(dn_r_TlsSocket * v_socket,dn_t_slice0_U8 v_bytes) {
{
{ dn_r_NetworkTransfer dn_tmp_4=({ dn_r_NetIo dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_1=(&((v_socket)->f_inner));
dn_t_slice0_U8 dn_tmp_2=v_bytes;
dn_f_socket_write(dn_tmp_1,dn_tmp_2); });
uint32_t dn_tmp_3=((uint32_t)(7ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_tls_write(dn_r_TlsSocket * v_socket,dn_t_slice0_U8 v_bytes) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_tls_write(v_socket,v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_tls_read_exact(dn_r_TlsSocket * v_socket,dn_t_buf_U8 * v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
{ dn_r_NetworkTransfer dn_tmp_6=({ dn_r_NetIo dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_1=(&((v_socket)->f_inner));
dn_t_buf_U8 * dn_tmp_2=v_bytes;
uint32_t dn_tmp_3=v_timeout_ms;
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_4=v_cancel;
dn_f_socket_read_exact(dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4); });
uint32_t dn_tmp_5=((uint32_t)(6ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_5); });return dn_tmp_6; }
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_tls_read_exact(dn_r_TlsSocket * v_socket,dn_t_buf_U8 * v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_tls_read_exact(v_socket,v_bytes,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_tls_write_all(dn_r_TlsSocket * v_socket,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
{ dn_r_NetworkTransfer dn_tmp_6=({ dn_r_NetIo dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * * dn_tmp_1=(&((v_socket)->f_inner));
dn_t_slice0_U8 dn_tmp_2=v_bytes;
uint32_t dn_tmp_3=v_timeout_ms;
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_4=v_cancel;
dn_f_socket_write_all(dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4); });
uint32_t dn_tmp_5=((uint32_t)(7ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_5); });return dn_tmp_6; }
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_tls_write_all(dn_r_TlsSocket * v_socket,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_tls_write_all(v_socket,v_bytes,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_reactor_dispose(dn_r_NetOpened * v_state) {
{
{
(void)(({ void * dn_tmp_0=(v_state)->f_handle;
({ dn_ext_dlm_net_reactor_close(dn_tmp_0);dn_fp_default();0; }); }));
}
}

return 0;
}
static void dn_cb_reactor_dispose(dn_r_NetOpened * v_state) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_reactor_dispose(v_state);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static void * dn_f_reactor_handle(dn_r_NetworkReactor const * v_reactor) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_reactor_dispose * const * dn_tmp_0=(&((v_reactor)->f_inner));
(&((*dn_tmp_0)->value)); });
{ void * dn_tmp_1=(v_state)->f_handle;return dn_tmp_1; }
}

dn_fail("missing native return");
}
static void * dn_cb_reactor_handle(dn_r_NetworkReactor const * v_reactor) {
 if(!dn_enter()){return (void *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (void *){0};}
 void * dn_result=dn_f_reactor_handle(v_reactor);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkOperation dn_f_reactor_operation(dn_r_ReactorNativeRequest v_result) {
{
{ dn_r_NetworkOperation dn_tmp_5=({ uint64_t dn_tmp_0=(v_result).f_id;
dn_r_NetworkError dn_tmp_1=({ int32_t dn_tmp_2=(v_result).f_status;
int32_t dn_tmp_3=(v_result).f_system;
uint32_t dn_tmp_4=((uint32_t)(21ULL));
((dn_r_NetworkError){dn_tmp_2,dn_tmp_3,dn_tmp_4}); });
((dn_r_NetworkOperation){dn_tmp_0,dn_tmp_1}); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_NetworkOperation dn_cb_reactor_operation(dn_r_ReactorNativeRequest v_result) {
 if(!dn_enter()){return (dn_r_NetworkOperation){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkOperation){0};}
 dn_r_NetworkOperation dn_result=dn_f_reactor_operation(v_result);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ReactorCompletion dn_f_reactor_completion(dn_r_ReactorNativeCompletion v_result) {
{
{ dn_r_ReactorCompletion dn_tmp_13=({ uint64_t dn_tmp_0=(v_result).f_id;
uint64_t dn_tmp_1=(v_result).f_channel;
dn_r_ReactorTcp dn_tmp_2=({ uint64_t dn_tmp_3=(v_result).f_accepted;
((dn_r_ReactorTcp){dn_tmp_3}); });
dn_r_NetworkTransfer dn_tmp_4=({ size_t dn_tmp_5=(v_result).f_count;
dn_r_NetworkError dn_tmp_6=({ int32_t dn_tmp_7=(v_result).f_status;
int32_t dn_tmp_8=(v_result).f_system;
uint32_t dn_tmp_9=((uint32_t)(28ULL));
((dn_r_NetworkError){dn_tmp_7,dn_tmp_8,dn_tmp_9}); });
((dn_r_NetworkTransfer){dn_tmp_5,dn_tmp_6}); });
uint32_t dn_tmp_10=(v_result).f_kind;
dn_r_SocketAddress dn_tmp_11=({ dn_r_NetAddress dn_tmp_12=(v_result).f_peer;
((dn_r_SocketAddress){dn_tmp_12}); });
((dn_r_ReactorCompletion){dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_4,dn_tmp_10,dn_tmp_11}); });return dn_tmp_13; }
}

dn_fail("missing native return");
}
static dn_r_ReactorCompletion dn_cb_reactor_completion(dn_r_ReactorNativeCompletion v_result) {
 if(!dn_enter()){return (dn_r_ReactorCompletion){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ReactorCompletion){0};}
 dn_r_ReactorCompletion dn_result=dn_f_reactor_completion(v_result);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkReactor dn_f_reactor_open(uint32_t v_channels,uint32_t v_operations,size_t v_byte_limit) {
{
{
{ dn_r_NetworkReactor dn_tmp_5=({ dn_t_resource_r9_NetOpened_reactor_dispose * dn_tmp_0=({ dn_t_resource_r9_NetOpened_reactor_dispose * dn_tmp_1=dn_begin_resource_r9_NetOpened_reactor_dispose();dn_commit_resource_r9_NetOpened_reactor_dispose(dn_tmp_1,({ uint32_t dn_tmp_2=v_channels;
uint32_t dn_tmp_3=v_operations;
size_t dn_tmp_4=v_byte_limit;
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_reactor_new(dn_tmp_2,dn_tmp_3,dn_tmp_4);dn_fp_default();dn_foreign; }); })); });
dn_adopt_resource_r9_NetOpened_reactor_dispose(&dn_tmp_0);
((dn_r_NetworkReactor){dn_tmp_0}); });dn_adopt_r14_NetworkReactor(&dn_tmp_5);return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkReactor dn_cb_reactor_open(uint32_t v_channels,uint32_t v_operations,size_t v_byte_limit) {
 if(!dn_enter()){return (dn_r_NetworkReactor){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkReactor){0};}
 dn_r_NetworkReactor dn_result=dn_f_reactor_open(v_channels,v_operations,v_byte_limit);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkError dn_f_reactor_error(dn_r_NetworkReactor const * v_reactor) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_reactor_dispose * const * dn_tmp_0=(&((v_reactor)->f_inner));
(&((*dn_tmp_0)->value)); });
{ dn_r_NetworkError dn_tmp_4=({ int32_t dn_tmp_1=(v_state)->f_status;
int32_t dn_tmp_2=(v_state)->f_system;
uint32_t dn_tmp_3=((uint32_t)(20ULL));
((dn_r_NetworkError){dn_tmp_1,dn_tmp_2,dn_tmp_3}); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_NetworkError dn_cb_reactor_error(dn_r_NetworkReactor const * v_reactor) {
 if(!dn_enter()){return (dn_r_NetworkError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkError){0};}
 dn_r_NetworkError dn_result=dn_f_reactor_error(v_reactor);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_reactor_close(dn_r_NetworkReactor v_reactor) {
dn_r_NetworkReactor volatile dn_param_reactor=v_reactor;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r14_NetworkReactor(&dn_param_reactor);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
(void)(({ dn_t_resource_r9_NetOpened_reactor_dispose * dn_tmp_2=({ dn_t_resource_r9_NetOpened_reactor_dispose * dn_tmp_3=(dn_param_reactor).f_inner;(dn_param_reactor).f_inner=(dn_t_resource_r9_NetOpened_reactor_dispose *){0};dn_adopt_resource_r9_NetOpened_reactor_dispose(&dn_tmp_3);dn_tmp_3; });
dn_adopt_resource_r9_NetOpened_reactor_dispose(&dn_tmp_2);
({ dn_drop_resource_r9_NetOpened_reactor_dispose(&dn_tmp_2);0; }); }));
}

dn_drop_r14_NetworkReactor(&dn_param_reactor);
dn_boundary_end(&dn_tmp_0,0);
return 0;
}
static void dn_cb_reactor_close(dn_r_NetworkReactor v_reactor) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_reactor_close(v_reactor);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static dn_r_ReactorTcpResult dn_f_reactor_adopt_tcp(dn_r_NetworkReactor * v_reactor,dn_r_TcpSocket v_socket) {
dn_r_TcpSocket volatile dn_param_socket=v_socket;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r9_TcpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
{
dn_r_ReactorNativeRequest v_result=({ void * dn_tmp_2=({ dn_r_NetworkReactor * dn_tmp_3=v_reactor;
dn_f_reactor_handle(dn_tmp_3); });
dn_r_NetOpened dn_tmp_4=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_5=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_6=(dn_param_socket).f_inner;(dn_param_socket).f_inner=(dn_t_resource_r9_NetOpened_net_dispose *){0};dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_6);dn_tmp_6; });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_5);
dn_take_resource_r9_NetOpened_net_dispose(dn_tmp_5); });
uint32_t dn_tmp_7=((uint32_t)(1ULL));
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_reactor_adopt(dn_tmp_2,dn_tmp_4,dn_tmp_7);dn_fp_default();dn_foreign; }); });
{ dn_r_ReactorTcpResult dn_tmp_14=({ dn_r_ReactorTcp dn_tmp_8=({ uint64_t dn_tmp_9=(v_result).f_channel;
((dn_r_ReactorTcp){dn_tmp_9}); });
dn_r_NetworkError dn_tmp_10=({ int32_t dn_tmp_11=(v_result).f_status;
int32_t dn_tmp_12=(v_result).f_system;
uint32_t dn_tmp_13=((uint32_t)(21ULL));
((dn_r_NetworkError){dn_tmp_11,dn_tmp_12,dn_tmp_13}); });
((dn_r_ReactorTcpResult){dn_tmp_8,dn_tmp_10}); });dn_drop_r9_TcpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_14; }
}
}

dn_drop_r9_TcpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
dn_fail("missing native return");
}
static dn_r_ReactorTcpResult dn_cb_reactor_adopt_tcp(dn_r_NetworkReactor * v_reactor,dn_r_TcpSocket v_socket) {
 if(!dn_enter()){return (dn_r_ReactorTcpResult){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ReactorTcpResult){0};}
 dn_r_ReactorTcpResult dn_result=dn_f_reactor_adopt_tcp(v_reactor,v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ReactorListenerResult dn_f_reactor_adopt_listener(dn_r_NetworkReactor * v_reactor,dn_r_TcpListener v_socket) {
dn_r_TcpListener volatile dn_param_socket=v_socket;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r11_TcpListener(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
{
dn_r_ReactorNativeRequest v_result=({ void * dn_tmp_2=({ dn_r_NetworkReactor * dn_tmp_3=v_reactor;
dn_f_reactor_handle(dn_tmp_3); });
dn_r_NetOpened dn_tmp_4=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_5=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_6=(dn_param_socket).f_inner;(dn_param_socket).f_inner=(dn_t_resource_r9_NetOpened_net_dispose *){0};dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_6);dn_tmp_6; });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_5);
dn_take_resource_r9_NetOpened_net_dispose(dn_tmp_5); });
uint32_t dn_tmp_7=((uint32_t)(2ULL));
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_reactor_adopt(dn_tmp_2,dn_tmp_4,dn_tmp_7);dn_fp_default();dn_foreign; }); });
{ dn_r_ReactorListenerResult dn_tmp_14=({ dn_r_ReactorListener dn_tmp_8=({ uint64_t dn_tmp_9=(v_result).f_channel;
((dn_r_ReactorListener){dn_tmp_9}); });
dn_r_NetworkError dn_tmp_10=({ int32_t dn_tmp_11=(v_result).f_status;
int32_t dn_tmp_12=(v_result).f_system;
uint32_t dn_tmp_13=((uint32_t)(21ULL));
((dn_r_NetworkError){dn_tmp_11,dn_tmp_12,dn_tmp_13}); });
((dn_r_ReactorListenerResult){dn_tmp_8,dn_tmp_10}); });dn_drop_r11_TcpListener(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_14; }
}
}

dn_drop_r11_TcpListener(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
dn_fail("missing native return");
}
static dn_r_ReactorListenerResult dn_cb_reactor_adopt_listener(dn_r_NetworkReactor * v_reactor,dn_r_TcpListener v_socket) {
 if(!dn_enter()){return (dn_r_ReactorListenerResult){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ReactorListenerResult){0};}
 dn_r_ReactorListenerResult dn_result=dn_f_reactor_adopt_listener(v_reactor,v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ReactorUdpResult dn_f_reactor_adopt_udp(dn_r_NetworkReactor * v_reactor,dn_r_UdpSocket v_socket) {
dn_r_UdpSocket volatile dn_param_socket=v_socket;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r9_UdpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
{
dn_r_ReactorNativeRequest v_result=({ void * dn_tmp_2=({ dn_r_NetworkReactor * dn_tmp_3=v_reactor;
dn_f_reactor_handle(dn_tmp_3); });
dn_r_NetOpened dn_tmp_4=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_5=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_6=(dn_param_socket).f_inner;(dn_param_socket).f_inner=(dn_t_resource_r9_NetOpened_net_dispose *){0};dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_6);dn_tmp_6; });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_5);
dn_take_resource_r9_NetOpened_net_dispose(dn_tmp_5); });
uint32_t dn_tmp_7=((uint32_t)(3ULL));
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_reactor_adopt(dn_tmp_2,dn_tmp_4,dn_tmp_7);dn_fp_default();dn_foreign; }); });
{ dn_r_ReactorUdpResult dn_tmp_14=({ dn_r_ReactorUdp dn_tmp_8=({ uint64_t dn_tmp_9=(v_result).f_channel;
((dn_r_ReactorUdp){dn_tmp_9}); });
dn_r_NetworkError dn_tmp_10=({ int32_t dn_tmp_11=(v_result).f_status;
int32_t dn_tmp_12=(v_result).f_system;
uint32_t dn_tmp_13=((uint32_t)(21ULL));
((dn_r_NetworkError){dn_tmp_11,dn_tmp_12,dn_tmp_13}); });
((dn_r_ReactorUdpResult){dn_tmp_8,dn_tmp_10}); });dn_drop_r9_UdpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_14; }
}
}

dn_drop_r9_UdpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
dn_fail("missing native return");
}
static dn_r_ReactorUdpResult dn_cb_reactor_adopt_udp(dn_r_NetworkReactor * v_reactor,dn_r_UdpSocket v_socket) {
 if(!dn_enter()){return (dn_r_ReactorUdpResult){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ReactorUdpResult){0};}
 dn_r_ReactorUdpResult dn_result=dn_f_reactor_adopt_udp(v_reactor,v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ReactorConnect dn_f_reactor_connect(dn_r_NetworkReactor * v_reactor,dn_r_SocketAddress v_address,uint32_t v_timeout_ms) {
{
{
dn_r_ReactorNativeRequest v_result=({ void * dn_tmp_0=({ dn_r_NetworkReactor * dn_tmp_1=v_reactor;
dn_f_reactor_handle(dn_tmp_1); });
dn_r_NetAddress dn_tmp_2=(v_address).f_value;
uint32_t dn_tmp_3=v_timeout_ms;
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_reactor_connect(dn_tmp_0,dn_tmp_2,dn_tmp_3);dn_fp_default();dn_foreign; }); });
{ dn_r_ReactorConnect dn_tmp_8=({ dn_r_ReactorTcp dn_tmp_4=({ uint64_t dn_tmp_5=(v_result).f_channel;
((dn_r_ReactorTcp){dn_tmp_5}); });
dn_r_NetworkOperation dn_tmp_6=({ dn_r_ReactorNativeRequest dn_tmp_7=v_result;
dn_f_reactor_operation(dn_tmp_7); });
((dn_r_ReactorConnect){dn_tmp_4,dn_tmp_6}); });return dn_tmp_8; }
}
}

dn_fail("missing native return");
}
static dn_r_ReactorConnect dn_cb_reactor_connect(dn_r_NetworkReactor * v_reactor,dn_r_SocketAddress v_address,uint32_t v_timeout_ms) {
 if(!dn_enter()){return (dn_r_ReactorConnect){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ReactorConnect){0};}
 dn_r_ReactorConnect dn_result=dn_f_reactor_connect(v_reactor,v_address,v_timeout_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkOperation dn_f_reactor_read(dn_r_NetworkReactor * v_reactor,dn_r_ReactorTcp v_channel,size_t v_count,bool v_exact,uint32_t v_timeout_ms) {
{
{
{ dn_r_NetworkOperation dn_tmp_10=({ dn_r_ReactorNativeRequest dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkReactor * dn_tmp_2=v_reactor;
dn_f_reactor_handle(dn_tmp_2); });
uint64_t dn_tmp_3=(v_channel).f_id;
uint32_t dn_tmp_4=((uint32_t)(1ULL));
uint8_t * dn_tmp_5=({ ((uint8_t *){0}); });
size_t dn_tmp_6=v_count;
bool dn_tmp_7=v_exact;
uint32_t dn_tmp_8=v_timeout_ms;
dn_r_NetAddress dn_tmp_9=({ ((dn_r_NetAddress){0}); });
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_reactor_submit(dn_tmp_1,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9);dn_fp_default();dn_foreign; }); });
dn_f_reactor_operation(dn_tmp_0); });return dn_tmp_10; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkOperation dn_cb_reactor_read(dn_r_NetworkReactor * v_reactor,dn_r_ReactorTcp v_channel,size_t v_count,bool v_exact,uint32_t v_timeout_ms) {
 if(!dn_enter()){return (dn_r_NetworkOperation){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkOperation){0};}
 dn_r_NetworkOperation dn_result=dn_f_reactor_read(v_reactor,v_channel,v_count,v_exact,v_timeout_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkOperation dn_f_reactor_write(dn_r_NetworkReactor * v_reactor,dn_r_ReactorTcp v_channel,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms) {
{
{
dn_r_NetText v_input=({ dn_t_slice0_U8 dn_tmp_0=v_bytes;
dn_f_net_bytes(dn_tmp_0); });
{ dn_r_NetworkOperation dn_tmp_11=({ dn_r_ReactorNativeRequest dn_tmp_1=({ void * dn_tmp_2=({ dn_r_NetworkReactor * dn_tmp_3=v_reactor;
dn_f_reactor_handle(dn_tmp_3); });
uint64_t dn_tmp_4=(v_channel).f_id;
uint32_t dn_tmp_5=((uint32_t)(2ULL));
uint8_t * dn_tmp_6=(v_input).f_data;
size_t dn_tmp_7=(v_input).f_count;
bool dn_tmp_8=1;
uint32_t dn_tmp_9=v_timeout_ms;
dn_r_NetAddress dn_tmp_10=({ ((dn_r_NetAddress){0}); });
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_reactor_submit(dn_tmp_2,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10);dn_fp_default();dn_foreign; }); });
dn_f_reactor_operation(dn_tmp_1); });return dn_tmp_11; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkOperation dn_cb_reactor_write(dn_r_NetworkReactor * v_reactor,dn_r_ReactorTcp v_channel,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms) {
 if(!dn_enter()){return (dn_r_NetworkOperation){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkOperation){0};}
 dn_r_NetworkOperation dn_result=dn_f_reactor_write(v_reactor,v_channel,v_bytes,v_timeout_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkOperation dn_f_reactor_accept(dn_r_NetworkReactor * v_reactor,dn_r_ReactorListener v_channel,uint32_t v_timeout_ms) {
{
{
{ dn_r_NetworkOperation dn_tmp_10=({ dn_r_ReactorNativeRequest dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkReactor * dn_tmp_2=v_reactor;
dn_f_reactor_handle(dn_tmp_2); });
uint64_t dn_tmp_3=(v_channel).f_id;
uint32_t dn_tmp_4=((uint32_t)(3ULL));
uint8_t * dn_tmp_5=({ ((uint8_t *){0}); });
size_t dn_tmp_6=((size_t)(0ULL));
bool dn_tmp_7=0;
uint32_t dn_tmp_8=v_timeout_ms;
dn_r_NetAddress dn_tmp_9=({ ((dn_r_NetAddress){0}); });
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_reactor_submit(dn_tmp_1,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9);dn_fp_default();dn_foreign; }); });
dn_f_reactor_operation(dn_tmp_0); });return dn_tmp_10; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkOperation dn_cb_reactor_accept(dn_r_NetworkReactor * v_reactor,dn_r_ReactorListener v_channel,uint32_t v_timeout_ms) {
 if(!dn_enter()){return (dn_r_NetworkOperation){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkOperation){0};}
 dn_r_NetworkOperation dn_result=dn_f_reactor_accept(v_reactor,v_channel,v_timeout_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkOperation dn_f_reactor_receive(dn_r_NetworkReactor * v_reactor,dn_r_ReactorUdp v_channel,size_t v_capacity,uint32_t v_timeout_ms) {
{
{
{ dn_r_NetworkOperation dn_tmp_10=({ dn_r_ReactorNativeRequest dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkReactor * dn_tmp_2=v_reactor;
dn_f_reactor_handle(dn_tmp_2); });
uint64_t dn_tmp_3=(v_channel).f_id;
uint32_t dn_tmp_4=((uint32_t)(5ULL));
uint8_t * dn_tmp_5=({ ((uint8_t *){0}); });
size_t dn_tmp_6=v_capacity;
bool dn_tmp_7=0;
uint32_t dn_tmp_8=v_timeout_ms;
dn_r_NetAddress dn_tmp_9=({ ((dn_r_NetAddress){0}); });
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_reactor_submit(dn_tmp_1,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9);dn_fp_default();dn_foreign; }); });
dn_f_reactor_operation(dn_tmp_0); });return dn_tmp_10; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkOperation dn_cb_reactor_receive(dn_r_NetworkReactor * v_reactor,dn_r_ReactorUdp v_channel,size_t v_capacity,uint32_t v_timeout_ms) {
 if(!dn_enter()){return (dn_r_NetworkOperation){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkOperation){0};}
 dn_r_NetworkOperation dn_result=dn_f_reactor_receive(v_reactor,v_channel,v_capacity,v_timeout_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkOperation dn_f_reactor_send(dn_r_NetworkReactor * v_reactor,dn_r_ReactorUdp v_channel,dn_r_SocketAddress v_peer,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms) {
{
{
dn_r_NetText v_input=({ dn_t_slice0_U8 dn_tmp_0=v_bytes;
dn_f_net_bytes(dn_tmp_0); });
{ dn_r_NetworkOperation dn_tmp_11=({ dn_r_ReactorNativeRequest dn_tmp_1=({ void * dn_tmp_2=({ dn_r_NetworkReactor * dn_tmp_3=v_reactor;
dn_f_reactor_handle(dn_tmp_3); });
uint64_t dn_tmp_4=(v_channel).f_id;
uint32_t dn_tmp_5=((uint32_t)(6ULL));
uint8_t * dn_tmp_6=(v_input).f_data;
size_t dn_tmp_7=(v_input).f_count;
bool dn_tmp_8=0;
uint32_t dn_tmp_9=v_timeout_ms;
dn_r_NetAddress dn_tmp_10=(v_peer).f_value;
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_reactor_submit(dn_tmp_2,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10);dn_fp_default();dn_foreign; }); });
dn_f_reactor_operation(dn_tmp_1); });return dn_tmp_11; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkOperation dn_cb_reactor_send(dn_r_NetworkReactor * v_reactor,dn_r_ReactorUdp v_channel,dn_r_SocketAddress v_peer,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms) {
 if(!dn_enter()){return (dn_r_NetworkOperation){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkOperation){0};}
 dn_r_NetworkOperation dn_result=dn_f_reactor_send(v_reactor,v_channel,v_peer,v_bytes,v_timeout_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkOperation dn_f_reactor_timer(dn_r_NetworkReactor * v_reactor,uint32_t v_delay_ms) {
{
{
{ dn_r_NetworkOperation dn_tmp_5=({ dn_r_ReactorNativeRequest dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkReactor * dn_tmp_2=v_reactor;
dn_f_reactor_handle(dn_tmp_2); });
uint32_t dn_tmp_3=v_delay_ms;
bool dn_tmp_4=0;
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_reactor_timer(dn_tmp_1,dn_tmp_3,dn_tmp_4);dn_fp_default();dn_foreign; }); });
dn_f_reactor_operation(dn_tmp_0); });return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkOperation dn_cb_reactor_timer(dn_r_NetworkReactor * v_reactor,uint32_t v_delay_ms) {
 if(!dn_enter()){return (dn_r_NetworkOperation){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkOperation){0};}
 dn_r_NetworkOperation dn_result=dn_f_reactor_timer(v_reactor,v_delay_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkOperation dn_f_reactor_event(dn_r_NetworkReactor * v_reactor,uint32_t v_timeout_ms) {
{
{
{ dn_r_NetworkOperation dn_tmp_5=({ dn_r_ReactorNativeRequest dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkReactor * dn_tmp_2=v_reactor;
dn_f_reactor_handle(dn_tmp_2); });
uint32_t dn_tmp_3=v_timeout_ms;
bool dn_tmp_4=1;
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_reactor_timer(dn_tmp_1,dn_tmp_3,dn_tmp_4);dn_fp_default();dn_foreign; }); });
dn_f_reactor_operation(dn_tmp_0); });return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkOperation dn_cb_reactor_event(dn_r_NetworkReactor * v_reactor,uint32_t v_timeout_ms) {
 if(!dn_enter()){return (dn_r_NetworkOperation){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkOperation){0};}
 dn_r_NetworkOperation dn_result=dn_f_reactor_event(v_reactor,v_timeout_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_reactor_signal(dn_r_NetworkReactor * v_reactor,uint64_t v_operation) {
{
{
{ dn_r_NetworkTransfer dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkReactor * dn_tmp_2=v_reactor;
dn_f_reactor_handle(dn_tmp_2); });
uint64_t dn_tmp_3=v_operation;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_reactor_signal(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_4=((uint32_t)(37ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_4); });return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_reactor_signal(dn_r_NetworkReactor * v_reactor,uint64_t v_operation) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_reactor_signal(v_reactor,v_operation);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_reactor_cancel(dn_r_NetworkReactor * v_reactor,uint64_t v_operation) {
{
if(({ uint64_t dn_tmp_0=v_operation;
uint64_t dn_tmp_1=((uint64_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); })){
{ dn_r_NetworkTransfer dn_tmp_7=({ size_t dn_tmp_2=((size_t)(0ULL));
dn_r_NetworkError dn_tmp_3=({ int32_t dn_tmp_4=((int32_t)(4ULL));
int32_t dn_tmp_5=((int32_t)(0ULL));
uint32_t dn_tmp_6=((uint32_t)(26ULL));
((dn_r_NetworkError){dn_tmp_4,dn_tmp_5,dn_tmp_6}); });
((dn_r_NetworkTransfer){dn_tmp_2,dn_tmp_3}); });return dn_tmp_7; }
}
else{
}
{
{ dn_r_NetworkTransfer dn_tmp_13=({ dn_r_NetIo dn_tmp_8=({ void * dn_tmp_9=({ dn_r_NetworkReactor * dn_tmp_10=v_reactor;
dn_f_reactor_handle(dn_tmp_10); });
uint64_t dn_tmp_11=v_operation;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_reactor_cancel(dn_tmp_9,dn_tmp_11);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_12=((uint32_t)(26ULL));
dn_f_socket_transfer(dn_tmp_8,dn_tmp_12); });return dn_tmp_13; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_reactor_cancel(dn_r_NetworkReactor * v_reactor,uint64_t v_operation) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_reactor_cancel(v_reactor,v_operation);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_reactor_cancel_all(dn_r_NetworkReactor * v_reactor) {
{
{
{ dn_r_NetworkTransfer dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkReactor * dn_tmp_2=v_reactor;
dn_f_reactor_handle(dn_tmp_2); });
uint64_t dn_tmp_3=((uint64_t)(0ULL));
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_reactor_cancel(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_4=((uint32_t)(26ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_4); });return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_reactor_cancel_all(dn_r_NetworkReactor * v_reactor) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_reactor_cancel_all(v_reactor);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_reactor_poll(dn_r_NetworkReactor * v_reactor,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
{
{ dn_r_NetworkTransfer dn_tmp_7=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkReactor * dn_tmp_2=v_reactor;
dn_f_reactor_handle(dn_tmp_2); });
uint32_t dn_tmp_3=v_timeout_ms;
void * dn_tmp_4=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_net_cancel_dispose * const * dn_tmp_5=v_cancel;
(&((*dn_tmp_5)->value)); })))));
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_reactor_poll(dn_tmp_1,dn_tmp_3,dn_tmp_4);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_6=((uint32_t)(27ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_6); });return dn_tmp_7; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_reactor_poll(dn_r_NetworkReactor * v_reactor,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_reactor_poll(v_reactor,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ReactorCompletion dn_f_reactor_result(dn_r_NetworkReactor const * v_reactor,uint64_t v_operation) {
{
{
{ dn_r_ReactorCompletion dn_tmp_4=({ dn_r_ReactorNativeCompletion dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkReactor const * dn_tmp_2=v_reactor;
dn_f_reactor_handle(dn_tmp_2); });
uint64_t dn_tmp_3=v_operation;
({ dn_r_ReactorNativeCompletion dn_foreign=dn_ext_dlm_net_reactor_result(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); });
dn_f_reactor_completion(dn_tmp_0); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_ReactorCompletion dn_cb_reactor_result(dn_r_NetworkReactor const * v_reactor,uint64_t v_operation) {
 if(!dn_enter()){return (dn_r_ReactorCompletion){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ReactorCompletion){0};}
 dn_r_ReactorCompletion dn_result=dn_f_reactor_result(v_reactor,v_operation);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ReactorCompletion dn_f_reactor_next(dn_r_NetworkReactor const * v_reactor) {
{
{
{ dn_r_ReactorCompletion dn_tmp_4=({ dn_r_ReactorNativeCompletion dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkReactor const * dn_tmp_2=v_reactor;
dn_f_reactor_handle(dn_tmp_2); });
uint64_t dn_tmp_3=((uint64_t)(0ULL));
({ dn_r_ReactorNativeCompletion dn_foreign=dn_ext_dlm_net_reactor_result(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); });
dn_f_reactor_completion(dn_tmp_0); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_ReactorCompletion dn_cb_reactor_next(dn_r_NetworkReactor const * v_reactor) {
 if(!dn_enter()){return (dn_r_ReactorCompletion){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ReactorCompletion){0};}
 dn_r_ReactorCompletion dn_result=dn_f_reactor_next(v_reactor);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_reactor_copy(dn_r_NetworkReactor const * v_reactor,uint64_t v_operation,size_t v_offset,dn_t_buf_U8 * v_bytes) {
{
{
{ dn_r_NetworkTransfer dn_tmp_10=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkReactor const * dn_tmp_2=v_reactor;
dn_f_reactor_handle(dn_tmp_2); });
uint64_t dn_tmp_3=v_operation;
size_t dn_tmp_4=v_offset;
uint8_t * dn_tmp_5=({ dn_t_buf_U8 * dn_tmp_6=v_bytes;
dn_f_net_output(dn_tmp_6); });
size_t dn_tmp_7=({ dn_t_buf_U8 * dn_tmp_8=v_bytes;
(*(dn_tmp_8)).len; });
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_reactor_copy(dn_tmp_1,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_7);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_9=((uint32_t)(29ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_9); });return dn_tmp_10; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_reactor_copy(dn_r_NetworkReactor const * v_reactor,uint64_t v_operation,size_t v_offset,dn_t_buf_U8 * v_bytes) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_reactor_copy(v_reactor,v_operation,v_offset,v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_reactor_ack(dn_r_NetworkReactor * v_reactor,uint64_t v_operation) {
{
{
{ dn_r_NetworkTransfer dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkReactor * dn_tmp_2=v_reactor;
dn_f_reactor_handle(dn_tmp_2); });
uint64_t dn_tmp_3=v_operation;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_reactor_ack(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_4=((uint32_t)(30ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_4); });return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_reactor_ack(dn_r_NetworkReactor * v_reactor,uint64_t v_operation) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_reactor_ack(v_reactor,v_operation);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_reactor_tcp_close(dn_r_NetworkReactor * v_reactor,dn_r_ReactorTcp v_channel) {
{
{
{ dn_r_NetworkTransfer dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkReactor * dn_tmp_2=v_reactor;
dn_f_reactor_handle(dn_tmp_2); });
uint64_t dn_tmp_3=(v_channel).f_id;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_reactor_channel_close(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_4=((uint32_t)(31ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_4); });return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_reactor_tcp_close(dn_r_NetworkReactor * v_reactor,dn_r_ReactorTcp v_channel) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_reactor_tcp_close(v_reactor,v_channel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_reactor_listener_close(dn_r_NetworkReactor * v_reactor,dn_r_ReactorListener v_channel) {
{
{
{ dn_r_NetworkTransfer dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkReactor * dn_tmp_2=v_reactor;
dn_f_reactor_handle(dn_tmp_2); });
uint64_t dn_tmp_3=(v_channel).f_id;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_reactor_channel_close(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_4=((uint32_t)(31ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_4); });return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_reactor_listener_close(dn_r_NetworkReactor * v_reactor,dn_r_ReactorListener v_channel) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_reactor_listener_close(v_reactor,v_channel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_reactor_udp_close(dn_r_NetworkReactor * v_reactor,dn_r_ReactorUdp v_channel) {
{
{
{ dn_r_NetworkTransfer dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkReactor * dn_tmp_2=v_reactor;
dn_f_reactor_handle(dn_tmp_2); });
uint64_t dn_tmp_3=(v_channel).f_id;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_reactor_channel_close(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_4=((uint32_t)(31ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_4); });return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_reactor_udp_close(dn_r_NetworkReactor * v_reactor,dn_r_ReactorUdp v_channel) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_reactor_udp_close(v_reactor,v_channel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TcpSocket dn_f_reactor_release_tcp(dn_r_NetworkReactor * v_reactor,dn_r_ReactorTcp v_channel) {
{
{
{ dn_r_TcpSocket dn_tmp_7=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_1=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_1,({ void * dn_tmp_2=({ dn_r_NetworkReactor * dn_tmp_3=v_reactor;
dn_f_reactor_handle(dn_tmp_3); });
uint64_t dn_tmp_4=(v_channel).f_id;
uint32_t dn_tmp_5=((uint32_t)(1ULL));
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_reactor_release(dn_tmp_2,dn_tmp_4,dn_tmp_5);dn_fp_default();dn_foreign; }); })); });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_0);
uint32_t dn_tmp_6=((uint32_t)(32ULL));
((dn_r_TcpSocket){dn_tmp_0,dn_tmp_6}); });dn_adopt_r9_TcpSocket(&dn_tmp_7);return dn_tmp_7; }
}
}

dn_fail("missing native return");
}
static dn_r_TcpSocket dn_cb_reactor_release_tcp(dn_r_NetworkReactor * v_reactor,dn_r_ReactorTcp v_channel) {
 if(!dn_enter()){return (dn_r_TcpSocket){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TcpSocket){0};}
 dn_r_TcpSocket dn_result=dn_f_reactor_release_tcp(v_reactor,v_channel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TcpListener dn_f_reactor_release_listener(dn_r_NetworkReactor * v_reactor,dn_r_ReactorListener v_channel) {
{
{
{ dn_r_TcpListener dn_tmp_6=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_1=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_1,({ void * dn_tmp_2=({ dn_r_NetworkReactor * dn_tmp_3=v_reactor;
dn_f_reactor_handle(dn_tmp_3); });
uint64_t dn_tmp_4=(v_channel).f_id;
uint32_t dn_tmp_5=((uint32_t)(2ULL));
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_reactor_release(dn_tmp_2,dn_tmp_4,dn_tmp_5);dn_fp_default();dn_foreign; }); })); });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_0);
((dn_r_TcpListener){dn_tmp_0}); });dn_adopt_r11_TcpListener(&dn_tmp_6);return dn_tmp_6; }
}
}

dn_fail("missing native return");
}
static dn_r_TcpListener dn_cb_reactor_release_listener(dn_r_NetworkReactor * v_reactor,dn_r_ReactorListener v_channel) {
 if(!dn_enter()){return (dn_r_TcpListener){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TcpListener){0};}
 dn_r_TcpListener dn_result=dn_f_reactor_release_listener(v_reactor,v_channel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_UdpSocket dn_f_reactor_release_udp(dn_r_NetworkReactor * v_reactor,dn_r_ReactorUdp v_channel) {
{
{
{ dn_r_UdpSocket dn_tmp_6=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_0=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_1=dn_begin_resource_r9_NetOpened_net_dispose();dn_commit_resource_r9_NetOpened_net_dispose(dn_tmp_1,({ void * dn_tmp_2=({ dn_r_NetworkReactor * dn_tmp_3=v_reactor;
dn_f_reactor_handle(dn_tmp_3); });
uint64_t dn_tmp_4=(v_channel).f_id;
uint32_t dn_tmp_5=((uint32_t)(3ULL));
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_reactor_release(dn_tmp_2,dn_tmp_4,dn_tmp_5);dn_fp_default();dn_foreign; }); })); });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_0);
((dn_r_UdpSocket){dn_tmp_0}); });dn_adopt_r9_UdpSocket(&dn_tmp_6);return dn_tmp_6; }
}
}

dn_fail("missing native return");
}
static dn_r_UdpSocket dn_cb_reactor_release_udp(dn_r_NetworkReactor * v_reactor,dn_r_ReactorUdp v_channel) {
 if(!dn_enter()){return (dn_r_UdpSocket){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_UdpSocket){0};}
 dn_r_UdpSocket dn_result=dn_f_reactor_release_udp(v_reactor,v_channel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ReactorStatistics dn_f_reactor_statistics(dn_r_NetworkReactor const * v_reactor) {
{
{
dn_r_ReactorNativeStats v_result=({ void * dn_tmp_0=({ dn_r_NetworkReactor const * dn_tmp_1=v_reactor;
dn_f_reactor_handle(dn_tmp_1); });
({ dn_r_ReactorNativeStats dn_foreign=dn_ext_dlm_net_reactor_stats(dn_tmp_0);dn_fp_default();dn_foreign; }); });
{ dn_r_ReactorStatistics dn_tmp_14=({ size_t dn_tmp_2=(v_result).f_buffered_bytes;
size_t dn_tmp_3=(v_result).f_byte_limit;
uint32_t dn_tmp_4=(v_result).f_channels;
uint32_t dn_tmp_5=(v_result).f_pending;
uint32_t dn_tmp_6=(v_result).f_completed;
uint32_t dn_tmp_7=(v_result).f_channel_limit;
uint32_t dn_tmp_8=(v_result).f_operation_limit;
uint32_t dn_tmp_9=(v_result).f_backend;
dn_r_NetworkError dn_tmp_10=({ int32_t dn_tmp_11=(v_result).f_status;
int32_t dn_tmp_12=(v_result).f_system;
uint32_t dn_tmp_13=((uint32_t)(33ULL));
((dn_r_NetworkError){dn_tmp_11,dn_tmp_12,dn_tmp_13}); });
((dn_r_ReactorStatistics){dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10}); });return dn_tmp_14; }
}
}

dn_fail("missing native return");
}
static dn_r_ReactorStatistics dn_cb_reactor_statistics(dn_r_NetworkReactor const * v_reactor) {
 if(!dn_enter()){return (dn_r_ReactorStatistics){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ReactorStatistics){0};}
 dn_r_ReactorStatistics dn_result=dn_f_reactor_statistics(v_reactor);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_scope_dispose(dn_r_NetOpened * v_state) {
{
{
(void)(({ void * dn_tmp_0=(v_state)->f_handle;
({ dn_ext_dlm_net_async_close(dn_tmp_0);dn_fp_default();0; }); }));
}
}

return 0;
}
static void dn_cb_scope_dispose(dn_r_NetOpened * v_state) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_scope_dispose(v_state);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_scope_waker_dispose(dn_r_NetOpened * v_state) {
{
{
(void)(({ void * dn_tmp_0=(v_state)->f_handle;
({ dn_ext_dlm_net_async_waker_close(dn_tmp_0);dn_fp_default();0; }); }));
}
}

return 0;
}
static void dn_cb_scope_waker_dispose(dn_r_NetOpened * v_state) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_scope_waker_dispose(v_state);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static void * dn_f_scope_handle(dn_r_NetworkScope const * v_scope) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_scope_dispose * const * dn_tmp_0=(&((v_scope)->f_inner));
(&((*dn_tmp_0)->value)); });
{ void * dn_tmp_1=(v_state)->f_handle;return dn_tmp_1; }
}

dn_fail("missing native return");
}
static void * dn_cb_scope_handle(dn_r_NetworkScope const * v_scope) {
 if(!dn_enter()){return (void *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (void *){0};}
 void * dn_result=dn_f_scope_handle(v_scope);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkScope dn_f_scope_open(uint32_t v_channels,uint32_t v_operations,size_t v_byte_limit,uint32_t v_wake_limit) {
{
{
{ dn_r_NetworkScope dn_tmp_6=({ dn_t_resource_r9_NetOpened_scope_dispose * dn_tmp_0=({ dn_t_resource_r9_NetOpened_scope_dispose * dn_tmp_1=dn_begin_resource_r9_NetOpened_scope_dispose();dn_commit_resource_r9_NetOpened_scope_dispose(dn_tmp_1,({ uint32_t dn_tmp_2=v_channels;
uint32_t dn_tmp_3=v_operations;
size_t dn_tmp_4=v_byte_limit;
uint32_t dn_tmp_5=v_wake_limit;
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_async_new(dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5);dn_fp_default();dn_foreign; }); })); });
dn_adopt_resource_r9_NetOpened_scope_dispose(&dn_tmp_0);
((dn_r_NetworkScope){dn_tmp_0}); });dn_adopt_r12_NetworkScope(&dn_tmp_6);return dn_tmp_6; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkScope dn_cb_scope_open(uint32_t v_channels,uint32_t v_operations,size_t v_byte_limit,uint32_t v_wake_limit) {
 if(!dn_enter()){return (dn_r_NetworkScope){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkScope){0};}
 dn_r_NetworkScope dn_result=dn_f_scope_open(v_channels,v_operations,v_byte_limit,v_wake_limit);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkError dn_f_scope_error(dn_r_NetworkScope const * v_scope) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_scope_dispose * const * dn_tmp_0=(&((v_scope)->f_inner));
(&((*dn_tmp_0)->value)); });
{ dn_r_NetworkError dn_tmp_4=({ int32_t dn_tmp_1=(v_state)->f_status;
int32_t dn_tmp_2=(v_state)->f_system;
uint32_t dn_tmp_3=((uint32_t)(40ULL));
((dn_r_NetworkError){dn_tmp_1,dn_tmp_2,dn_tmp_3}); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_NetworkError dn_cb_scope_error(dn_r_NetworkScope const * v_scope) {
 if(!dn_enter()){return (dn_r_NetworkError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkError){0};}
 dn_r_NetworkError dn_result=dn_f_scope_error(v_scope);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_scope_close(dn_r_NetworkScope v_scope) {
dn_r_NetworkScope volatile dn_param_scope=v_scope;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r12_NetworkScope(&dn_param_scope);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
(void)(({ dn_t_resource_r9_NetOpened_scope_dispose * dn_tmp_2=({ dn_t_resource_r9_NetOpened_scope_dispose * dn_tmp_3=(dn_param_scope).f_inner;(dn_param_scope).f_inner=(dn_t_resource_r9_NetOpened_scope_dispose *){0};dn_adopt_resource_r9_NetOpened_scope_dispose(&dn_tmp_3);dn_tmp_3; });
dn_adopt_resource_r9_NetOpened_scope_dispose(&dn_tmp_2);
({ dn_drop_resource_r9_NetOpened_scope_dispose(&dn_tmp_2);0; }); }));
}

dn_drop_r12_NetworkScope(&dn_param_scope);
dn_boundary_end(&dn_tmp_0,0);
return 0;
}
static void dn_cb_scope_close(dn_r_NetworkScope v_scope) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_scope_close(v_scope);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static dn_r_ReactorTcpResult dn_f_scope_adopt_tcp(dn_r_NetworkScope * v_scope,dn_r_TcpSocket v_socket) {
dn_r_TcpSocket volatile dn_param_socket=v_socket;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r9_TcpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
{
dn_r_ReactorNativeRequest v_result=({ void * dn_tmp_2=({ dn_r_NetworkScope * dn_tmp_3=v_scope;
dn_f_scope_handle(dn_tmp_3); });
dn_r_NetOpened dn_tmp_4=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_5=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_6=(dn_param_socket).f_inner;(dn_param_socket).f_inner=(dn_t_resource_r9_NetOpened_net_dispose *){0};dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_6);dn_tmp_6; });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_5);
dn_take_resource_r9_NetOpened_net_dispose(dn_tmp_5); });
uint32_t dn_tmp_7=((uint32_t)(1ULL));
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_async_adopt(dn_tmp_2,dn_tmp_4,dn_tmp_7);dn_fp_default();dn_foreign; }); });
{ dn_r_ReactorTcpResult dn_tmp_14=({ dn_r_ReactorTcp dn_tmp_8=({ uint64_t dn_tmp_9=(v_result).f_channel;
((dn_r_ReactorTcp){dn_tmp_9}); });
dn_r_NetworkError dn_tmp_10=({ int32_t dn_tmp_11=(v_result).f_status;
int32_t dn_tmp_12=(v_result).f_system;
uint32_t dn_tmp_13=((uint32_t)(41ULL));
((dn_r_NetworkError){dn_tmp_11,dn_tmp_12,dn_tmp_13}); });
((dn_r_ReactorTcpResult){dn_tmp_8,dn_tmp_10}); });dn_drop_r9_TcpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_14; }
}
}

dn_drop_r9_TcpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
dn_fail("missing native return");
}
static dn_r_ReactorTcpResult dn_cb_scope_adopt_tcp(dn_r_NetworkScope * v_scope,dn_r_TcpSocket v_socket) {
 if(!dn_enter()){return (dn_r_ReactorTcpResult){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ReactorTcpResult){0};}
 dn_r_ReactorTcpResult dn_result=dn_f_scope_adopt_tcp(v_scope,v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ReactorListenerResult dn_f_scope_adopt_listener(dn_r_NetworkScope * v_scope,dn_r_TcpListener v_socket) {
dn_r_TcpListener volatile dn_param_socket=v_socket;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r11_TcpListener(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
{
dn_r_ReactorNativeRequest v_result=({ void * dn_tmp_2=({ dn_r_NetworkScope * dn_tmp_3=v_scope;
dn_f_scope_handle(dn_tmp_3); });
dn_r_NetOpened dn_tmp_4=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_5=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_6=(dn_param_socket).f_inner;(dn_param_socket).f_inner=(dn_t_resource_r9_NetOpened_net_dispose *){0};dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_6);dn_tmp_6; });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_5);
dn_take_resource_r9_NetOpened_net_dispose(dn_tmp_5); });
uint32_t dn_tmp_7=((uint32_t)(2ULL));
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_async_adopt(dn_tmp_2,dn_tmp_4,dn_tmp_7);dn_fp_default();dn_foreign; }); });
{ dn_r_ReactorListenerResult dn_tmp_14=({ dn_r_ReactorListener dn_tmp_8=({ uint64_t dn_tmp_9=(v_result).f_channel;
((dn_r_ReactorListener){dn_tmp_9}); });
dn_r_NetworkError dn_tmp_10=({ int32_t dn_tmp_11=(v_result).f_status;
int32_t dn_tmp_12=(v_result).f_system;
uint32_t dn_tmp_13=((uint32_t)(41ULL));
((dn_r_NetworkError){dn_tmp_11,dn_tmp_12,dn_tmp_13}); });
((dn_r_ReactorListenerResult){dn_tmp_8,dn_tmp_10}); });dn_drop_r11_TcpListener(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_14; }
}
}

dn_drop_r11_TcpListener(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
dn_fail("missing native return");
}
static dn_r_ReactorListenerResult dn_cb_scope_adopt_listener(dn_r_NetworkScope * v_scope,dn_r_TcpListener v_socket) {
 if(!dn_enter()){return (dn_r_ReactorListenerResult){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ReactorListenerResult){0};}
 dn_r_ReactorListenerResult dn_result=dn_f_scope_adopt_listener(v_scope,v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ReactorUdpResult dn_f_scope_adopt_udp(dn_r_NetworkScope * v_scope,dn_r_UdpSocket v_socket) {
dn_r_UdpSocket volatile dn_param_socket=v_socket;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r9_UdpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
{
dn_r_ReactorNativeRequest v_result=({ void * dn_tmp_2=({ dn_r_NetworkScope * dn_tmp_3=v_scope;
dn_f_scope_handle(dn_tmp_3); });
dn_r_NetOpened dn_tmp_4=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_5=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_6=(dn_param_socket).f_inner;(dn_param_socket).f_inner=(dn_t_resource_r9_NetOpened_net_dispose *){0};dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_6);dn_tmp_6; });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_5);
dn_take_resource_r9_NetOpened_net_dispose(dn_tmp_5); });
uint32_t dn_tmp_7=((uint32_t)(3ULL));
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_async_adopt(dn_tmp_2,dn_tmp_4,dn_tmp_7);dn_fp_default();dn_foreign; }); });
{ dn_r_ReactorUdpResult dn_tmp_14=({ dn_r_ReactorUdp dn_tmp_8=({ uint64_t dn_tmp_9=(v_result).f_channel;
((dn_r_ReactorUdp){dn_tmp_9}); });
dn_r_NetworkError dn_tmp_10=({ int32_t dn_tmp_11=(v_result).f_status;
int32_t dn_tmp_12=(v_result).f_system;
uint32_t dn_tmp_13=((uint32_t)(41ULL));
((dn_r_NetworkError){dn_tmp_11,dn_tmp_12,dn_tmp_13}); });
((dn_r_ReactorUdpResult){dn_tmp_8,dn_tmp_10}); });dn_drop_r9_UdpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_14; }
}
}

dn_drop_r9_UdpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
dn_fail("missing native return");
}
static dn_r_ReactorUdpResult dn_cb_scope_adopt_udp(dn_r_NetworkScope * v_scope,dn_r_UdpSocket v_socket) {
 if(!dn_enter()){return (dn_r_ReactorUdpResult){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ReactorUdpResult){0};}
 dn_r_ReactorUdpResult dn_result=dn_f_scope_adopt_udp(v_scope,v_socket);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ReactorConnect dn_f_scope_connect(dn_r_NetworkScope * v_scope,dn_r_SocketAddress v_address,uint32_t v_timeout_ms) {
{
{
dn_r_ReactorNativeRequest v_result=({ void * dn_tmp_0=({ dn_r_NetworkScope * dn_tmp_1=v_scope;
dn_f_scope_handle(dn_tmp_1); });
dn_r_NetAddress dn_tmp_2=(v_address).f_value;
uint32_t dn_tmp_3=v_timeout_ms;
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_async_connect(dn_tmp_0,dn_tmp_2,dn_tmp_3);dn_fp_default();dn_foreign; }); });
{ dn_r_ReactorConnect dn_tmp_8=({ dn_r_ReactorTcp dn_tmp_4=({ uint64_t dn_tmp_5=(v_result).f_channel;
((dn_r_ReactorTcp){dn_tmp_5}); });
dn_r_NetworkOperation dn_tmp_6=({ dn_r_ReactorNativeRequest dn_tmp_7=v_result;
dn_f_reactor_operation(dn_tmp_7); });
((dn_r_ReactorConnect){dn_tmp_4,dn_tmp_6}); });return dn_tmp_8; }
}
}

dn_fail("missing native return");
}
static dn_r_ReactorConnect dn_cb_scope_connect(dn_r_NetworkScope * v_scope,dn_r_SocketAddress v_address,uint32_t v_timeout_ms) {
 if(!dn_enter()){return (dn_r_ReactorConnect){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ReactorConnect){0};}
 dn_r_ReactorConnect dn_result=dn_f_scope_connect(v_scope,v_address,v_timeout_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkOperation dn_f_scope_read(dn_r_NetworkScope * v_scope,dn_r_ReactorTcp v_channel,size_t v_count,bool v_exact,uint32_t v_timeout_ms) {
{
{
{ dn_r_NetworkOperation dn_tmp_10=({ dn_r_ReactorNativeRequest dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkScope * dn_tmp_2=v_scope;
dn_f_scope_handle(dn_tmp_2); });
uint64_t dn_tmp_3=(v_channel).f_id;
uint32_t dn_tmp_4=((uint32_t)(1ULL));
uint8_t * dn_tmp_5=({ ((uint8_t *){0}); });
size_t dn_tmp_6=v_count;
bool dn_tmp_7=v_exact;
uint32_t dn_tmp_8=v_timeout_ms;
dn_r_NetAddress dn_tmp_9=({ ((dn_r_NetAddress){0}); });
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_reactor_submit(dn_tmp_1,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9);dn_fp_default();dn_foreign; }); });
dn_f_reactor_operation(dn_tmp_0); });return dn_tmp_10; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkOperation dn_cb_scope_read(dn_r_NetworkScope * v_scope,dn_r_ReactorTcp v_channel,size_t v_count,bool v_exact,uint32_t v_timeout_ms) {
 if(!dn_enter()){return (dn_r_NetworkOperation){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkOperation){0};}
 dn_r_NetworkOperation dn_result=dn_f_scope_read(v_scope,v_channel,v_count,v_exact,v_timeout_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkOperation dn_f_scope_write(dn_r_NetworkScope * v_scope,dn_r_ReactorTcp v_channel,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms) {
{
{
dn_r_NetText v_input=({ dn_t_slice0_U8 dn_tmp_0=v_bytes;
dn_f_net_bytes(dn_tmp_0); });
{ dn_r_NetworkOperation dn_tmp_11=({ dn_r_ReactorNativeRequest dn_tmp_1=({ void * dn_tmp_2=({ dn_r_NetworkScope * dn_tmp_3=v_scope;
dn_f_scope_handle(dn_tmp_3); });
uint64_t dn_tmp_4=(v_channel).f_id;
uint32_t dn_tmp_5=((uint32_t)(2ULL));
uint8_t * dn_tmp_6=(v_input).f_data;
size_t dn_tmp_7=(v_input).f_count;
bool dn_tmp_8=1;
uint32_t dn_tmp_9=v_timeout_ms;
dn_r_NetAddress dn_tmp_10=({ ((dn_r_NetAddress){0}); });
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_reactor_submit(dn_tmp_2,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10);dn_fp_default();dn_foreign; }); });
dn_f_reactor_operation(dn_tmp_1); });return dn_tmp_11; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkOperation dn_cb_scope_write(dn_r_NetworkScope * v_scope,dn_r_ReactorTcp v_channel,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms) {
 if(!dn_enter()){return (dn_r_NetworkOperation){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkOperation){0};}
 dn_r_NetworkOperation dn_result=dn_f_scope_write(v_scope,v_channel,v_bytes,v_timeout_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkOperation dn_f_scope_accept(dn_r_NetworkScope * v_scope,dn_r_ReactorListener v_channel,uint32_t v_timeout_ms) {
{
{
{ dn_r_NetworkOperation dn_tmp_10=({ dn_r_ReactorNativeRequest dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkScope * dn_tmp_2=v_scope;
dn_f_scope_handle(dn_tmp_2); });
uint64_t dn_tmp_3=(v_channel).f_id;
uint32_t dn_tmp_4=((uint32_t)(3ULL));
uint8_t * dn_tmp_5=({ ((uint8_t *){0}); });
size_t dn_tmp_6=((size_t)(0ULL));
bool dn_tmp_7=0;
uint32_t dn_tmp_8=v_timeout_ms;
dn_r_NetAddress dn_tmp_9=({ ((dn_r_NetAddress){0}); });
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_reactor_submit(dn_tmp_1,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9);dn_fp_default();dn_foreign; }); });
dn_f_reactor_operation(dn_tmp_0); });return dn_tmp_10; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkOperation dn_cb_scope_accept(dn_r_NetworkScope * v_scope,dn_r_ReactorListener v_channel,uint32_t v_timeout_ms) {
 if(!dn_enter()){return (dn_r_NetworkOperation){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkOperation){0};}
 dn_r_NetworkOperation dn_result=dn_f_scope_accept(v_scope,v_channel,v_timeout_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkOperation dn_f_scope_receive(dn_r_NetworkScope * v_scope,dn_r_ReactorUdp v_channel,size_t v_capacity,uint32_t v_timeout_ms) {
{
{
{ dn_r_NetworkOperation dn_tmp_10=({ dn_r_ReactorNativeRequest dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkScope * dn_tmp_2=v_scope;
dn_f_scope_handle(dn_tmp_2); });
uint64_t dn_tmp_3=(v_channel).f_id;
uint32_t dn_tmp_4=((uint32_t)(5ULL));
uint8_t * dn_tmp_5=({ ((uint8_t *){0}); });
size_t dn_tmp_6=v_capacity;
bool dn_tmp_7=0;
uint32_t dn_tmp_8=v_timeout_ms;
dn_r_NetAddress dn_tmp_9=({ ((dn_r_NetAddress){0}); });
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_reactor_submit(dn_tmp_1,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9);dn_fp_default();dn_foreign; }); });
dn_f_reactor_operation(dn_tmp_0); });return dn_tmp_10; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkOperation dn_cb_scope_receive(dn_r_NetworkScope * v_scope,dn_r_ReactorUdp v_channel,size_t v_capacity,uint32_t v_timeout_ms) {
 if(!dn_enter()){return (dn_r_NetworkOperation){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkOperation){0};}
 dn_r_NetworkOperation dn_result=dn_f_scope_receive(v_scope,v_channel,v_capacity,v_timeout_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkOperation dn_f_scope_send(dn_r_NetworkScope * v_scope,dn_r_ReactorUdp v_channel,dn_r_SocketAddress v_peer,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms) {
{
{
dn_r_NetText v_input=({ dn_t_slice0_U8 dn_tmp_0=v_bytes;
dn_f_net_bytes(dn_tmp_0); });
{ dn_r_NetworkOperation dn_tmp_11=({ dn_r_ReactorNativeRequest dn_tmp_1=({ void * dn_tmp_2=({ dn_r_NetworkScope * dn_tmp_3=v_scope;
dn_f_scope_handle(dn_tmp_3); });
uint64_t dn_tmp_4=(v_channel).f_id;
uint32_t dn_tmp_5=((uint32_t)(6ULL));
uint8_t * dn_tmp_6=(v_input).f_data;
size_t dn_tmp_7=(v_input).f_count;
bool dn_tmp_8=0;
uint32_t dn_tmp_9=v_timeout_ms;
dn_r_NetAddress dn_tmp_10=(v_peer).f_value;
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_reactor_submit(dn_tmp_2,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10);dn_fp_default();dn_foreign; }); });
dn_f_reactor_operation(dn_tmp_1); });return dn_tmp_11; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkOperation dn_cb_scope_send(dn_r_NetworkScope * v_scope,dn_r_ReactorUdp v_channel,dn_r_SocketAddress v_peer,dn_t_slice0_U8 v_bytes,uint32_t v_timeout_ms) {
 if(!dn_enter()){return (dn_r_NetworkOperation){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkOperation){0};}
 dn_r_NetworkOperation dn_result=dn_f_scope_send(v_scope,v_channel,v_peer,v_bytes,v_timeout_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkOperation dn_f_scope_timer(dn_r_NetworkScope * v_scope,uint32_t v_delay_ms) {
{
{
{ dn_r_NetworkOperation dn_tmp_5=({ dn_r_ReactorNativeRequest dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkScope * dn_tmp_2=v_scope;
dn_f_scope_handle(dn_tmp_2); });
uint32_t dn_tmp_3=v_delay_ms;
bool dn_tmp_4=0;
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_reactor_timer(dn_tmp_1,dn_tmp_3,dn_tmp_4);dn_fp_default();dn_foreign; }); });
dn_f_reactor_operation(dn_tmp_0); });return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkOperation dn_cb_scope_timer(dn_r_NetworkScope * v_scope,uint32_t v_delay_ms) {
 if(!dn_enter()){return (dn_r_NetworkOperation){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkOperation){0};}
 dn_r_NetworkOperation dn_result=dn_f_scope_timer(v_scope,v_delay_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkOperation dn_f_scope_event(dn_r_NetworkScope * v_scope,uint32_t v_timeout_ms) {
{
{
{ dn_r_NetworkOperation dn_tmp_5=({ dn_r_ReactorNativeRequest dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkScope * dn_tmp_2=v_scope;
dn_f_scope_handle(dn_tmp_2); });
uint32_t dn_tmp_3=v_timeout_ms;
bool dn_tmp_4=1;
({ dn_r_ReactorNativeRequest dn_foreign=dn_ext_dlm_net_reactor_timer(dn_tmp_1,dn_tmp_3,dn_tmp_4);dn_fp_default();dn_foreign; }); });
dn_f_reactor_operation(dn_tmp_0); });return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkOperation dn_cb_scope_event(dn_r_NetworkScope * v_scope,uint32_t v_timeout_ms) {
 if(!dn_enter()){return (dn_r_NetworkOperation){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkOperation){0};}
 dn_r_NetworkOperation dn_result=dn_f_scope_event(v_scope,v_timeout_ms);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_scope_poll(dn_r_NetworkScope * v_scope,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
{
{ dn_r_NetworkTransfer dn_tmp_7=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkScope * dn_tmp_2=v_scope;
dn_f_scope_handle(dn_tmp_2); });
uint32_t dn_tmp_3=v_timeout_ms;
void * dn_tmp_4=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_net_cancel_dispose * const * dn_tmp_5=v_cancel;
(&((*dn_tmp_5)->value)); })))));
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_reactor_poll(dn_tmp_1,dn_tmp_3,dn_tmp_4);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_6=((uint32_t)(42ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_6); });return dn_tmp_7; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_scope_poll(dn_r_NetworkScope * v_scope,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_scope_poll(v_scope,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_scope_join(dn_r_NetworkScope * v_scope,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
{
{ dn_r_NetworkTransfer dn_tmp_7=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkScope * dn_tmp_2=v_scope;
dn_f_scope_handle(dn_tmp_2); });
uint32_t dn_tmp_3=v_timeout_ms;
void * dn_tmp_4=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_net_cancel_dispose * const * dn_tmp_5=v_cancel;
(&((*dn_tmp_5)->value)); })))));
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_async_join(dn_tmp_1,dn_tmp_3,dn_tmp_4);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_6=((uint32_t)(43ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_6); });return dn_tmp_7; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_scope_join(dn_r_NetworkScope * v_scope,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_scope_join(v_scope,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ReactorCompletion dn_f_scope_result(dn_r_NetworkScope const * v_scope,uint64_t v_operation) {
{
{
{ dn_r_ReactorCompletion dn_tmp_4=({ dn_r_ReactorNativeCompletion dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkScope const * dn_tmp_2=v_scope;
dn_f_scope_handle(dn_tmp_2); });
uint64_t dn_tmp_3=v_operation;
({ dn_r_ReactorNativeCompletion dn_foreign=dn_ext_dlm_net_reactor_result(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); });
dn_f_reactor_completion(dn_tmp_0); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_ReactorCompletion dn_cb_scope_result(dn_r_NetworkScope const * v_scope,uint64_t v_operation) {
 if(!dn_enter()){return (dn_r_ReactorCompletion){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ReactorCompletion){0};}
 dn_r_ReactorCompletion dn_result=dn_f_scope_result(v_scope,v_operation);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ReactorCompletion dn_f_scope_next(dn_r_NetworkScope const * v_scope) {
{
{
{ dn_r_ReactorCompletion dn_tmp_4=({ dn_r_ReactorNativeCompletion dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkScope const * dn_tmp_2=v_scope;
dn_f_scope_handle(dn_tmp_2); });
uint64_t dn_tmp_3=((uint64_t)(0ULL));
({ dn_r_ReactorNativeCompletion dn_foreign=dn_ext_dlm_net_reactor_result(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); });
dn_f_reactor_completion(dn_tmp_0); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_ReactorCompletion dn_cb_scope_next(dn_r_NetworkScope const * v_scope) {
 if(!dn_enter()){return (dn_r_ReactorCompletion){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ReactorCompletion){0};}
 dn_r_ReactorCompletion dn_result=dn_f_scope_next(v_scope);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_scope_copy(dn_r_NetworkScope const * v_scope,uint64_t v_operation,size_t v_offset,dn_t_buf_U8 * v_bytes) {
{
{
{ dn_r_NetworkTransfer dn_tmp_10=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkScope const * dn_tmp_2=v_scope;
dn_f_scope_handle(dn_tmp_2); });
uint64_t dn_tmp_3=v_operation;
size_t dn_tmp_4=v_offset;
uint8_t * dn_tmp_5=({ dn_t_buf_U8 * dn_tmp_6=v_bytes;
dn_f_net_output(dn_tmp_6); });
size_t dn_tmp_7=({ dn_t_buf_U8 * dn_tmp_8=v_bytes;
(*(dn_tmp_8)).len; });
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_reactor_copy(dn_tmp_1,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_7);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_9=((uint32_t)(44ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_9); });return dn_tmp_10; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_scope_copy(dn_r_NetworkScope const * v_scope,uint64_t v_operation,size_t v_offset,dn_t_buf_U8 * v_bytes) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_scope_copy(v_scope,v_operation,v_offset,v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_scope_ack(dn_r_NetworkScope * v_scope,uint64_t v_operation) {
{
{
{ dn_r_NetworkTransfer dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkScope * dn_tmp_2=v_scope;
dn_f_scope_handle(dn_tmp_2); });
uint64_t dn_tmp_3=v_operation;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_reactor_ack(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_4=((uint32_t)(45ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_4); });return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_scope_ack(dn_r_NetworkScope * v_scope,uint64_t v_operation) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_scope_ack(v_scope,v_operation);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_scope_cancel(dn_r_NetworkScope * v_scope,uint64_t v_operation) {
{
if(({ uint64_t dn_tmp_0=v_operation;
uint64_t dn_tmp_1=((uint64_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); })){
{ dn_r_NetworkTransfer dn_tmp_7=({ size_t dn_tmp_2=((size_t)(0ULL));
dn_r_NetworkError dn_tmp_3=({ int32_t dn_tmp_4=((int32_t)(4ULL));
int32_t dn_tmp_5=((int32_t)(0ULL));
uint32_t dn_tmp_6=((uint32_t)(46ULL));
((dn_r_NetworkError){dn_tmp_4,dn_tmp_5,dn_tmp_6}); });
((dn_r_NetworkTransfer){dn_tmp_2,dn_tmp_3}); });return dn_tmp_7; }
}
else{
}
{
{ dn_r_NetworkTransfer dn_tmp_13=({ dn_r_NetIo dn_tmp_8=({ void * dn_tmp_9=({ dn_r_NetworkScope * dn_tmp_10=v_scope;
dn_f_scope_handle(dn_tmp_10); });
uint64_t dn_tmp_11=v_operation;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_reactor_cancel(dn_tmp_9,dn_tmp_11);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_12=((uint32_t)(46ULL));
dn_f_socket_transfer(dn_tmp_8,dn_tmp_12); });return dn_tmp_13; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_scope_cancel(dn_r_NetworkScope * v_scope,uint64_t v_operation) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_scope_cancel(v_scope,v_operation);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_scope_cancel_all(dn_r_NetworkScope * v_scope) {
{
{
{ dn_r_NetworkTransfer dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkScope * dn_tmp_2=v_scope;
dn_f_scope_handle(dn_tmp_2); });
uint64_t dn_tmp_3=((uint64_t)(0ULL));
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_reactor_cancel(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_4=((uint32_t)(46ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_4); });return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_scope_cancel_all(dn_r_NetworkScope * v_scope) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_scope_cancel_all(v_scope);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_scope_tcp_close(dn_r_NetworkScope * v_scope,dn_r_ReactorTcp v_channel) {
{
{
{ dn_r_NetworkTransfer dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkScope * dn_tmp_2=v_scope;
dn_f_scope_handle(dn_tmp_2); });
uint64_t dn_tmp_3=(v_channel).f_id;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_reactor_channel_close(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_4=((uint32_t)(47ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_4); });return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_scope_tcp_close(dn_r_NetworkScope * v_scope,dn_r_ReactorTcp v_channel) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_scope_tcp_close(v_scope,v_channel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_scope_listener_close(dn_r_NetworkScope * v_scope,dn_r_ReactorListener v_channel) {
{
{
{ dn_r_NetworkTransfer dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkScope * dn_tmp_2=v_scope;
dn_f_scope_handle(dn_tmp_2); });
uint64_t dn_tmp_3=(v_channel).f_id;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_reactor_channel_close(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_4=((uint32_t)(47ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_4); });return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_scope_listener_close(dn_r_NetworkScope * v_scope,dn_r_ReactorListener v_channel) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_scope_listener_close(v_scope,v_channel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_scope_udp_close(dn_r_NetworkScope * v_scope,dn_r_ReactorUdp v_channel) {
{
{
{ dn_r_NetworkTransfer dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_NetworkScope * dn_tmp_2=v_scope;
dn_f_scope_handle(dn_tmp_2); });
uint64_t dn_tmp_3=(v_channel).f_id;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_reactor_channel_close(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_4=((uint32_t)(47ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_4); });return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_scope_udp_close(dn_r_NetworkScope * v_scope,dn_r_ReactorUdp v_channel) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_scope_udp_close(v_scope,v_channel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_EventWaker dn_f_scope_event_waker(dn_r_NetworkScope const * v_scope,uint64_t v_operation) {
{
{
{ dn_r_EventWaker dn_tmp_6=({ dn_t_resource_r9_NetOpened_scope_waker_dispose * dn_tmp_0=({ dn_t_resource_r9_NetOpened_scope_waker_dispose * dn_tmp_1=dn_begin_resource_r9_NetOpened_scope_waker_dispose();dn_commit_resource_r9_NetOpened_scope_waker_dispose(dn_tmp_1,({ void * dn_tmp_2=({ dn_r_NetworkScope const * dn_tmp_3=v_scope;
dn_f_scope_handle(dn_tmp_3); });
uint64_t dn_tmp_4=v_operation;
uint32_t dn_tmp_5=((uint32_t)(1ULL));
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_async_waker(dn_tmp_2,dn_tmp_4,dn_tmp_5);dn_fp_default();dn_foreign; }); })); });
dn_adopt_resource_r9_NetOpened_scope_waker_dispose(&dn_tmp_0);
((dn_r_EventWaker){dn_tmp_0}); });dn_adopt_r10_EventWaker(&dn_tmp_6);return dn_tmp_6; }
}
}

dn_fail("missing native return");
}
static dn_r_EventWaker dn_cb_scope_event_waker(dn_r_NetworkScope const * v_scope,uint64_t v_operation) {
 if(!dn_enter()){return (dn_r_EventWaker){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_EventWaker){0};}
 dn_r_EventWaker dn_result=dn_f_scope_event_waker(v_scope,v_operation);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_OperationCanceller dn_f_scope_operation_canceller(dn_r_NetworkScope const * v_scope,uint64_t v_operation) {
{
{
{ dn_r_OperationCanceller dn_tmp_6=({ dn_t_resource_r9_NetOpened_scope_waker_dispose * dn_tmp_0=({ dn_t_resource_r9_NetOpened_scope_waker_dispose * dn_tmp_1=dn_begin_resource_r9_NetOpened_scope_waker_dispose();dn_commit_resource_r9_NetOpened_scope_waker_dispose(dn_tmp_1,({ void * dn_tmp_2=({ dn_r_NetworkScope const * dn_tmp_3=v_scope;
dn_f_scope_handle(dn_tmp_3); });
uint64_t dn_tmp_4=v_operation;
uint32_t dn_tmp_5=((uint32_t)(2ULL));
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_async_waker(dn_tmp_2,dn_tmp_4,dn_tmp_5);dn_fp_default();dn_foreign; }); })); });
dn_adopt_resource_r9_NetOpened_scope_waker_dispose(&dn_tmp_0);
((dn_r_OperationCanceller){dn_tmp_0}); });dn_adopt_r18_OperationCanceller(&dn_tmp_6);return dn_tmp_6; }
}
}

dn_fail("missing native return");
}
static dn_r_OperationCanceller dn_cb_scope_operation_canceller(dn_r_NetworkScope const * v_scope,uint64_t v_operation) {
 if(!dn_enter()){return (dn_r_OperationCanceller){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_OperationCanceller){0};}
 dn_r_OperationCanceller dn_result=dn_f_scope_operation_canceller(v_scope,v_operation);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ScopeCanceller dn_f_scope_canceller(dn_r_NetworkScope const * v_scope) {
{
{
{ dn_r_ScopeCanceller dn_tmp_6=({ dn_t_resource_r9_NetOpened_scope_waker_dispose * dn_tmp_0=({ dn_t_resource_r9_NetOpened_scope_waker_dispose * dn_tmp_1=dn_begin_resource_r9_NetOpened_scope_waker_dispose();dn_commit_resource_r9_NetOpened_scope_waker_dispose(dn_tmp_1,({ void * dn_tmp_2=({ dn_r_NetworkScope const * dn_tmp_3=v_scope;
dn_f_scope_handle(dn_tmp_3); });
uint64_t dn_tmp_4=((uint64_t)(0ULL));
uint32_t dn_tmp_5=((uint32_t)(3ULL));
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_async_waker(dn_tmp_2,dn_tmp_4,dn_tmp_5);dn_fp_default();dn_foreign; }); })); });
dn_adopt_resource_r9_NetOpened_scope_waker_dispose(&dn_tmp_0);
((dn_r_ScopeCanceller){dn_tmp_0}); });dn_adopt_r14_ScopeCanceller(&dn_tmp_6);return dn_tmp_6; }
}
}

dn_fail("missing native return");
}
static dn_r_ScopeCanceller dn_cb_scope_canceller(dn_r_NetworkScope const * v_scope) {
 if(!dn_enter()){return (dn_r_ScopeCanceller){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ScopeCanceller){0};}
 dn_r_ScopeCanceller dn_result=dn_f_scope_canceller(v_scope);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkError dn_f_event_waker_error(dn_r_EventWaker const * v_waker) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_scope_waker_dispose * const * dn_tmp_0=(&((v_waker)->f_inner));
(&((*dn_tmp_0)->value)); });
{ dn_r_NetworkError dn_tmp_4=({ int32_t dn_tmp_1=(v_state)->f_status;
int32_t dn_tmp_2=(v_state)->f_system;
uint32_t dn_tmp_3=((uint32_t)(48ULL));
((dn_r_NetworkError){dn_tmp_1,dn_tmp_2,dn_tmp_3}); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_NetworkError dn_cb_event_waker_error(dn_r_EventWaker const * v_waker) {
 if(!dn_enter()){return (dn_r_NetworkError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkError){0};}
 dn_r_NetworkError dn_result=dn_f_event_waker_error(v_waker);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkError dn_f_operation_canceller_error(dn_r_OperationCanceller const * v_waker) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_scope_waker_dispose * const * dn_tmp_0=(&((v_waker)->f_inner));
(&((*dn_tmp_0)->value)); });
{ dn_r_NetworkError dn_tmp_4=({ int32_t dn_tmp_1=(v_state)->f_status;
int32_t dn_tmp_2=(v_state)->f_system;
uint32_t dn_tmp_3=((uint32_t)(48ULL));
((dn_r_NetworkError){dn_tmp_1,dn_tmp_2,dn_tmp_3}); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_NetworkError dn_cb_operation_canceller_error(dn_r_OperationCanceller const * v_waker) {
 if(!dn_enter()){return (dn_r_NetworkError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkError){0};}
 dn_r_NetworkError dn_result=dn_f_operation_canceller_error(v_waker);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkError dn_f_scope_canceller_error(dn_r_ScopeCanceller const * v_waker) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_scope_waker_dispose * const * dn_tmp_0=(&((v_waker)->f_inner));
(&((*dn_tmp_0)->value)); });
{ dn_r_NetworkError dn_tmp_4=({ int32_t dn_tmp_1=(v_state)->f_status;
int32_t dn_tmp_2=(v_state)->f_system;
uint32_t dn_tmp_3=((uint32_t)(48ULL));
((dn_r_NetworkError){dn_tmp_1,dn_tmp_2,dn_tmp_3}); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_NetworkError dn_cb_scope_canceller_error(dn_r_ScopeCanceller const * v_waker) {
 if(!dn_enter()){return (dn_r_NetworkError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkError){0};}
 dn_r_NetworkError dn_result=dn_f_scope_canceller_error(v_waker);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_event_wake(dn_r_EventWaker const * v_waker) {
{
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_scope_waker_dispose * const * dn_tmp_0=(&((v_waker)->f_inner));
(&((*dn_tmp_0)->value)); });
{ dn_r_NetworkTransfer dn_tmp_4=({ dn_r_NetIo dn_tmp_1=({ void * dn_tmp_2=(v_state)->f_handle;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_async_wake(dn_tmp_2);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_3=((uint32_t)(49ULL));
dn_f_socket_transfer(dn_tmp_1,dn_tmp_3); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_event_wake(dn_r_EventWaker const * v_waker) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_event_wake(v_waker);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_operation_cancel(dn_r_OperationCanceller const * v_waker) {
{
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_scope_waker_dispose * const * dn_tmp_0=(&((v_waker)->f_inner));
(&((*dn_tmp_0)->value)); });
{ dn_r_NetworkTransfer dn_tmp_4=({ dn_r_NetIo dn_tmp_1=({ void * dn_tmp_2=(v_state)->f_handle;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_async_wake(dn_tmp_2);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_3=((uint32_t)(49ULL));
dn_f_socket_transfer(dn_tmp_1,dn_tmp_3); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_operation_cancel(dn_r_OperationCanceller const * v_waker) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_operation_cancel(v_waker);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_scope_request_cancel(dn_r_ScopeCanceller const * v_waker) {
{
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_scope_waker_dispose * const * dn_tmp_0=(&((v_waker)->f_inner));
(&((*dn_tmp_0)->value)); });
{ dn_r_NetworkTransfer dn_tmp_4=({ dn_r_NetIo dn_tmp_1=({ void * dn_tmp_2=(v_state)->f_handle;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_async_wake(dn_tmp_2);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_3=((uint32_t)(49ULL));
dn_f_socket_transfer(dn_tmp_1,dn_tmp_3); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_scope_request_cancel(dn_r_ScopeCanceller const * v_waker) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_scope_request_cancel(v_waker);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_event_waker_close(dn_r_EventWaker v_waker) {
dn_r_EventWaker volatile dn_param_waker=v_waker;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r10_EventWaker(&dn_param_waker);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
(void)(({ dn_t_resource_r9_NetOpened_scope_waker_dispose * dn_tmp_2=({ dn_t_resource_r9_NetOpened_scope_waker_dispose * dn_tmp_3=(dn_param_waker).f_inner;(dn_param_waker).f_inner=(dn_t_resource_r9_NetOpened_scope_waker_dispose *){0};dn_adopt_resource_r9_NetOpened_scope_waker_dispose(&dn_tmp_3);dn_tmp_3; });
dn_adopt_resource_r9_NetOpened_scope_waker_dispose(&dn_tmp_2);
({ dn_drop_resource_r9_NetOpened_scope_waker_dispose(&dn_tmp_2);0; }); }));
}

dn_drop_r10_EventWaker(&dn_param_waker);
dn_boundary_end(&dn_tmp_0,0);
return 0;
}
static void dn_cb_event_waker_close(dn_r_EventWaker v_waker) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_event_waker_close(v_waker);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_operation_canceller_close(dn_r_OperationCanceller v_waker) {
dn_r_OperationCanceller volatile dn_param_waker=v_waker;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r18_OperationCanceller(&dn_param_waker);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
(void)(({ dn_t_resource_r9_NetOpened_scope_waker_dispose * dn_tmp_2=({ dn_t_resource_r9_NetOpened_scope_waker_dispose * dn_tmp_3=(dn_param_waker).f_inner;(dn_param_waker).f_inner=(dn_t_resource_r9_NetOpened_scope_waker_dispose *){0};dn_adopt_resource_r9_NetOpened_scope_waker_dispose(&dn_tmp_3);dn_tmp_3; });
dn_adopt_resource_r9_NetOpened_scope_waker_dispose(&dn_tmp_2);
({ dn_drop_resource_r9_NetOpened_scope_waker_dispose(&dn_tmp_2);0; }); }));
}

dn_drop_r18_OperationCanceller(&dn_param_waker);
dn_boundary_end(&dn_tmp_0,0);
return 0;
}
static void dn_cb_operation_canceller_close(dn_r_OperationCanceller v_waker) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_operation_canceller_close(v_waker);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_scope_canceller_close(dn_r_ScopeCanceller v_waker) {
dn_r_ScopeCanceller volatile dn_param_waker=v_waker;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r14_ScopeCanceller(&dn_param_waker);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
(void)(({ dn_t_resource_r9_NetOpened_scope_waker_dispose * dn_tmp_2=({ dn_t_resource_r9_NetOpened_scope_waker_dispose * dn_tmp_3=(dn_param_waker).f_inner;(dn_param_waker).f_inner=(dn_t_resource_r9_NetOpened_scope_waker_dispose *){0};dn_adopt_resource_r9_NetOpened_scope_waker_dispose(&dn_tmp_3);dn_tmp_3; });
dn_adopt_resource_r9_NetOpened_scope_waker_dispose(&dn_tmp_2);
({ dn_drop_resource_r9_NetOpened_scope_waker_dispose(&dn_tmp_2);0; }); }));
}

dn_drop_r14_ScopeCanceller(&dn_param_waker);
dn_boundary_end(&dn_tmp_0,0);
return 0;
}
static void dn_cb_scope_canceller_close(dn_r_ScopeCanceller v_waker) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_scope_canceller_close(v_waker);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static dn_r_ScopeStatistics dn_f_scope_statistics(dn_r_NetworkScope const * v_scope) {
{
{
dn_r_ScopeNativeStatistics v_result=({ void * dn_tmp_0=({ dn_r_NetworkScope const * dn_tmp_1=v_scope;
dn_f_scope_handle(dn_tmp_1); });
({ dn_r_ScopeNativeStatistics dn_foreign=dn_ext_dlm_net_async_stats(dn_tmp_0);dn_fp_default();dn_foreign; }); });
dn_r_ReactorNativeStats v_raw=(v_result).f_reactor;
dn_r_ReactorStatistics v_reactor=({ size_t dn_tmp_2=(v_raw).f_buffered_bytes;
size_t dn_tmp_3=(v_raw).f_byte_limit;
uint32_t dn_tmp_4=(v_raw).f_channels;
uint32_t dn_tmp_5=(v_raw).f_pending;
uint32_t dn_tmp_6=(v_raw).f_completed;
uint32_t dn_tmp_7=(v_raw).f_channel_limit;
uint32_t dn_tmp_8=(v_raw).f_operation_limit;
uint32_t dn_tmp_9=(v_raw).f_backend;
dn_r_NetworkError dn_tmp_10=({ int32_t dn_tmp_11=(v_raw).f_status;
int32_t dn_tmp_12=(v_raw).f_system;
uint32_t dn_tmp_13=((uint32_t)(50ULL));
((dn_r_NetworkError){dn_tmp_11,dn_tmp_12,dn_tmp_13}); });
((dn_r_ReactorStatistics){dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10}); });
{ dn_r_ScopeStatistics dn_tmp_18=({ dn_r_ReactorStatistics dn_tmp_14=v_reactor;
uint32_t dn_tmp_15=(v_result).f_queued;
uint32_t dn_tmp_16=(v_result).f_queue_limit;
uint32_t dn_tmp_17=(v_result).f_kernel_pending;
((dn_r_ScopeStatistics){dn_tmp_14,dn_tmp_15,dn_tmp_16,dn_tmp_17}); });return dn_tmp_18; }
}
}

dn_fail("missing native return");
}
static dn_r_ScopeStatistics dn_cb_scope_statistics(dn_r_NetworkScope const * v_scope) {
 if(!dn_enter()){return (dn_r_ScopeStatistics){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ScopeStatistics){0};}
 dn_r_ScopeStatistics dn_result=dn_f_scope_statistics(v_scope);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_main(void) {
{
dn_t_resource_h_net_cancel_dispose * volatile v_cancel={0};
dn_r_NetworkScope volatile v_group={0};
dn_r_EventWaker volatile v_a={0};
dn_r_EventWaker volatile v_b={0};
dn_r_OperationCanceller volatile v_stop={0};
dn_r_ScopeCanceller volatile v_all={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r14_ScopeCanceller(&v_all);
dn_drop_r18_OperationCanceller(&v_stop);
dn_drop_r10_EventWaker(&v_b);
dn_drop_r10_EventWaker(&v_a);
dn_drop_r12_NetworkScope(&v_group);
dn_drop_resource_h_net_cancel_dispose(&v_cancel);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_cancel=({ dn_f_network_cancel(); });
(void)(({ bool dn_tmp_2=({ dn_t_resource_h_net_cancel_dispose * const * dn_tmp_3=(&(v_cancel));
dn_f_network_cancel_valid(dn_tmp_3); });
(dn_assert(dn_tmp_2),0); }));
v_group=({ uint32_t dn_tmp_4=((uint32_t)(1ULL));
uint32_t dn_tmp_5=((uint32_t)(4ULL));
size_t dn_tmp_6=((size_t)(32ULL));
uint32_t dn_tmp_7=((uint32_t)(1ULL));
dn_f_scope_open(dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7); });
dn_r_NetworkOperation v_first=({ dn_r_NetworkScope * dn_tmp_8=(&(v_group));
uint32_t dn_tmp_9=((uint32_t)(3000ULL));
dn_f_scope_event(dn_tmp_8,dn_tmp_9); });
(void)(({ bool dn_tmp_10=({ dn_r_NetworkError const * dn_tmp_11=(&((v_first).f_error));
dn_f_network_ok(dn_tmp_11); });
(dn_assert(dn_tmp_10),0); }));
dn_r_NetworkOperation v_second=({ dn_r_NetworkScope * dn_tmp_12=(&(v_group));
uint32_t dn_tmp_13=((uint32_t)(3000ULL));
dn_f_scope_event(dn_tmp_12,dn_tmp_13); });
(void)(({ bool dn_tmp_14=({ dn_r_NetworkError const * dn_tmp_15=(&((v_second).f_error));
dn_f_network_ok(dn_tmp_15); });
(dn_assert(dn_tmp_14),0); }));
dn_r_NetworkOperation v_ready=({ dn_r_NetworkScope * dn_tmp_16=(&(v_group));
uint32_t dn_tmp_17=((uint32_t)(0ULL));
dn_f_scope_timer(dn_tmp_16,dn_tmp_17); });
(void)(({ bool dn_tmp_18=({ dn_r_NetworkError const * dn_tmp_19=(&((v_ready).f_error));
dn_f_network_ok(dn_tmp_19); });
(dn_assert(dn_tmp_18),0); }));
dn_r_NetworkOperation v_later=({ dn_r_NetworkScope * dn_tmp_20=(&(v_group));
uint32_t dn_tmp_21=((uint32_t)(20ULL));
dn_f_scope_timer(dn_tmp_20,dn_tmp_21); });
(void)(({ bool dn_tmp_22=({ dn_r_NetworkError const * dn_tmp_23=(&((v_later).f_error));
dn_f_network_ok(dn_tmp_23); });
(dn_assert(dn_tmp_22),0); }));
v_a=({ dn_r_NetworkScope const * dn_tmp_24=(&(v_group));
uint64_t dn_tmp_25=(v_first).f_id;
dn_f_scope_event_waker(dn_tmp_24,dn_tmp_25); });
v_b=({ dn_r_NetworkScope const * dn_tmp_26=(&(v_group));
uint64_t dn_tmp_27=(v_second).f_id;
dn_f_scope_event_waker(dn_tmp_26,dn_tmp_27); });
dn_r_NetworkError v_ea=({ dn_r_EventWaker const * dn_tmp_28=(&(v_a));
dn_f_event_waker_error(dn_tmp_28); });
dn_r_NetworkError v_eb=({ dn_r_EventWaker const * dn_tmp_29=(&(v_b));
dn_f_event_waker_error(dn_tmp_29); });
(void)(({ bool dn_tmp_30=(({ dn_r_NetworkError const * dn_tmp_31=(&(v_ea));
dn_f_network_ok(dn_tmp_31); }) && ({ dn_r_NetworkError const * dn_tmp_32=(&(v_eb));
dn_f_network_ok(dn_tmp_32); }));
(dn_assert(dn_tmp_30),0); }));
dn_r_NetworkTransfer v_signal=({ dn_r_EventWaker const * dn_tmp_33=(&(v_a));
dn_f_event_wake(dn_tmp_33); });
(void)(({ bool dn_tmp_34=(({ dn_r_NetworkError const * dn_tmp_35=(&((v_signal).f_error));
dn_f_network_ok(dn_tmp_35); }) && ({ size_t dn_tmp_36=(v_signal).f_count;
size_t dn_tmp_37=((size_t)(1ULL));
(dn_tmp_36 == dn_tmp_37); }));
(dn_assert(dn_tmp_34),0); }));
dn_r_NetworkTransfer v_duplicate=({ dn_r_EventWaker const * dn_tmp_38=(&(v_a));
dn_f_event_wake(dn_tmp_38); });
(void)(({ bool dn_tmp_39=(({ dn_r_NetworkError const * dn_tmp_40=(&((v_duplicate).f_error));
dn_f_network_ok(dn_tmp_40); }) && ({ size_t dn_tmp_41=(v_duplicate).f_count;
size_t dn_tmp_42=((size_t)(0ULL));
(dn_tmp_41 == dn_tmp_42); }));
(dn_assert(dn_tmp_39),0); }));
dn_r_NetworkTransfer v_pressure=({ dn_r_EventWaker const * dn_tmp_43=(&(v_b));
dn_f_event_wake(dn_tmp_43); });
(void)(({ bool dn_tmp_44=({ int32_t dn_tmp_45=((v_pressure).f_error).f_kind;
int32_t dn_tmp_46=((int32_t)(7ULL));
(dn_tmp_45 == dn_tmp_46); });
(dn_assert(dn_tmp_44),0); }));
dn_r_NetworkTransfer v_timeout=({ dn_r_NetworkScope * dn_tmp_47=(&(v_group));
uint32_t dn_tmp_48=((uint32_t)(1ULL));
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_49=(&(v_cancel));
dn_f_scope_join(dn_tmp_47,dn_tmp_48,dn_tmp_49); });
(void)(({ bool dn_tmp_50=({ int32_t dn_tmp_51=((v_timeout).f_error).f_kind;
int32_t dn_tmp_52=((int32_t)(3ULL));
(dn_tmp_51 == dn_tmp_52); });
(dn_assert(dn_tmp_50),0); }));
dn_r_ReactorCompletion v_result=({ dn_r_NetworkScope const * dn_tmp_53=(&(v_group));
uint64_t dn_tmp_54=(v_first).f_id;
dn_f_scope_result(dn_tmp_53,dn_tmp_54); });
(void)(({ bool dn_tmp_55=({ dn_r_NetworkError const * dn_tmp_56=(&(((v_result).f_transfer).f_error));
dn_f_network_ok(dn_tmp_56); });
(dn_assert(dn_tmp_55),0); }));
dn_r_NetworkTransfer v_next=({ dn_r_EventWaker const * dn_tmp_57=(&(v_b));
dn_f_event_wake(dn_tmp_57); });
(void)(({ bool dn_tmp_58=({ dn_r_NetworkError const * dn_tmp_59=(&((v_next).f_error));
dn_f_network_ok(dn_tmp_59); });
(dn_assert(dn_tmp_58),0); }));
dn_r_NetworkTransfer v_joined=({ dn_r_NetworkScope * dn_tmp_60=(&(v_group));
uint32_t dn_tmp_61=((uint32_t)(3000ULL));
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_62=(&(v_cancel));
dn_f_scope_join(dn_tmp_60,dn_tmp_61,dn_tmp_62); });
(void)(({ bool dn_tmp_63=(({ dn_r_NetworkError const * dn_tmp_64=(&((v_joined).f_error));
dn_f_network_ok(dn_tmp_64); }) && ({ size_t dn_tmp_65=(v_joined).f_count;
size_t dn_tmp_66=((size_t)(4ULL));
(dn_tmp_65 == dn_tmp_66); }));
(dn_assert(dn_tmp_63),0); }));
dn_r_NetworkTransfer v_a0=({ dn_r_NetworkScope * dn_tmp_67=(&(v_group));
uint64_t dn_tmp_68=(v_first).f_id;
dn_f_scope_ack(dn_tmp_67,dn_tmp_68); });
dn_r_NetworkTransfer v_a1=({ dn_r_NetworkScope * dn_tmp_69=(&(v_group));
uint64_t dn_tmp_70=(v_second).f_id;
dn_f_scope_ack(dn_tmp_69,dn_tmp_70); });
dn_r_NetworkTransfer v_a2=({ dn_r_NetworkScope * dn_tmp_71=(&(v_group));
uint64_t dn_tmp_72=(v_ready).f_id;
dn_f_scope_ack(dn_tmp_71,dn_tmp_72); });
dn_r_NetworkTransfer v_a3=({ dn_r_NetworkScope * dn_tmp_73=(&(v_group));
uint64_t dn_tmp_74=(v_later).f_id;
dn_f_scope_ack(dn_tmp_73,dn_tmp_74); });
(void)(({ bool dn_tmp_75=(((({ dn_r_NetworkError const * dn_tmp_76=(&((v_a0).f_error));
dn_f_network_ok(dn_tmp_76); }) && ({ dn_r_NetworkError const * dn_tmp_77=(&((v_a1).f_error));
dn_f_network_ok(dn_tmp_77); })) && ({ dn_r_NetworkError const * dn_tmp_78=(&((v_a2).f_error));
dn_f_network_ok(dn_tmp_78); })) && ({ dn_r_NetworkError const * dn_tmp_79=(&((v_a3).f_error));
dn_f_network_ok(dn_tmp_79); }));
(dn_assert(dn_tmp_75),0); }));
dn_r_NetworkOperation v_pending=({ dn_r_NetworkScope * dn_tmp_80=(&(v_group));
uint32_t dn_tmp_81=((uint32_t)(3000ULL));
dn_f_scope_event(dn_tmp_80,dn_tmp_81); });
(void)(({ bool dn_tmp_82=({ dn_r_NetworkError const * dn_tmp_83=(&((v_pending).f_error));
dn_f_network_ok(dn_tmp_83); });
(dn_assert(dn_tmp_82),0); }));
v_stop=({ dn_r_NetworkScope const * dn_tmp_84=(&(v_group));
uint64_t dn_tmp_85=(v_pending).f_id;
dn_f_scope_operation_canceller(dn_tmp_84,dn_tmp_85); });
dn_r_NetworkTransfer v_requested=({ dn_r_OperationCanceller const * dn_tmp_86=(&(v_stop));
dn_f_operation_cancel(dn_tmp_86); });
(void)(({ bool dn_tmp_87=({ dn_r_NetworkError const * dn_tmp_88=(&((v_requested).f_error));
dn_f_network_ok(dn_tmp_88); });
(dn_assert(dn_tmp_87),0); }));
dn_r_NetworkTransfer v_stopped=({ dn_r_NetworkScope * dn_tmp_89=(&(v_group));
uint32_t dn_tmp_90=((uint32_t)(3000ULL));
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_91=(&(v_cancel));
dn_f_scope_join(dn_tmp_89,dn_tmp_90,dn_tmp_91); });
(void)(({ bool dn_tmp_92=({ dn_r_NetworkError const * dn_tmp_93=(&((v_stopped).f_error));
dn_f_network_ok(dn_tmp_93); });
(dn_assert(dn_tmp_92),0); }));
dn_r_ReactorCompletion v_cancelled=({ dn_r_NetworkScope const * dn_tmp_94=(&(v_group));
uint64_t dn_tmp_95=(v_pending).f_id;
dn_f_scope_result(dn_tmp_94,dn_tmp_95); });
(void)(({ bool dn_tmp_96=({ dn_r_NetworkError const * dn_tmp_97=(&(((v_cancelled).f_transfer).f_error));
dn_f_network_was_cancelled(dn_tmp_97); });
(dn_assert(dn_tmp_96),0); }));
v_all=({ dn_r_NetworkScope const * dn_tmp_98=(&(v_group));
dn_f_scope_canceller(dn_tmp_98); });
(void)(({ dn_r_NetworkScope dn_tmp_99=({ dn_r_NetworkScope dn_tmp_100=v_group;v_group=(dn_r_NetworkScope){0};dn_adopt_r12_NetworkScope(&dn_tmp_100);dn_tmp_100; });
dn_adopt_r12_NetworkScope(&dn_tmp_99);
dn_f_scope_close(dn_tmp_99); }));
dn_r_NetworkTransfer v_closed=({ dn_r_EventWaker const * dn_tmp_101=(&(v_a));
dn_f_event_wake(dn_tmp_101); });
dn_r_NetworkTransfer v_stopped_again=({ dn_r_OperationCanceller const * dn_tmp_102=(&(v_stop));
dn_f_operation_cancel(dn_tmp_102); });
dn_r_NetworkTransfer v_closed_group=({ dn_r_ScopeCanceller const * dn_tmp_103=(&(v_all));
dn_f_scope_request_cancel(dn_tmp_103); });
(void)(({ bool dn_tmp_104=((({ dn_r_NetworkError const * dn_tmp_105=(&((v_closed).f_error));
dn_f_network_was_cancelled(dn_tmp_105); }) && ({ dn_r_NetworkError const * dn_tmp_106=(&((v_stopped_again).f_error));
dn_f_network_was_cancelled(dn_tmp_106); })) && ({ dn_r_NetworkError const * dn_tmp_107=(&((v_closed_group).f_error));
dn_f_network_was_cancelled(dn_tmp_107); }));
(dn_assert(dn_tmp_104),0); }));
(void)(({ dn_r_EventWaker dn_tmp_108=({ dn_r_EventWaker dn_tmp_109=v_a;v_a=(dn_r_EventWaker){0};dn_adopt_r10_EventWaker(&dn_tmp_109);dn_tmp_109; });
dn_adopt_r10_EventWaker(&dn_tmp_108);
dn_f_event_waker_close(dn_tmp_108); }));
(void)(({ dn_r_EventWaker dn_tmp_110=({ dn_r_EventWaker dn_tmp_111=v_b;v_b=(dn_r_EventWaker){0};dn_adopt_r10_EventWaker(&dn_tmp_111);dn_tmp_111; });
dn_adopt_r10_EventWaker(&dn_tmp_110);
dn_f_event_waker_close(dn_tmp_110); }));
(void)(({ dn_r_OperationCanceller dn_tmp_112=({ dn_r_OperationCanceller dn_tmp_113=v_stop;v_stop=(dn_r_OperationCanceller){0};dn_adopt_r18_OperationCanceller(&dn_tmp_113);dn_tmp_113; });
dn_adopt_r18_OperationCanceller(&dn_tmp_112);
dn_f_operation_canceller_close(dn_tmp_112); }));
(void)(({ dn_r_ScopeCanceller dn_tmp_114=({ dn_r_ScopeCanceller dn_tmp_115=v_all;v_all=(dn_r_ScopeCanceller){0};dn_adopt_r14_ScopeCanceller(&dn_tmp_115);dn_tmp_115; });
dn_adopt_r14_ScopeCanceller(&dn_tmp_114);
dn_f_scope_canceller_close(dn_tmp_114); }));
(void)(({ dn_t_resource_h_net_cancel_dispose * dn_tmp_116=({ dn_t_resource_h_net_cancel_dispose * dn_tmp_117=v_cancel;v_cancel=(dn_t_resource_h_net_cancel_dispose *){0};dn_adopt_resource_h_net_cancel_dispose(&dn_tmp_117);dn_tmp_117; });
dn_adopt_resource_h_net_cancel_dispose(&dn_tmp_116);
({ dn_drop_resource_h_net_cancel_dispose(&dn_tmp_116);0; }); }));
{
dn_r_NetIo v_shutdown=({ dn_f_network_provider_shutdown(); });
(void)(({ bool dn_tmp_118=({ int32_t dn_tmp_119=(v_shutdown).f_status;
int32_t dn_tmp_120=((int32_t)(0ULL));
(dn_tmp_119 == dn_tmp_120); });
(dn_assert(dn_tmp_118),0); }));
}
(void)(({ dn_utf8 dn_tmp_121=((dn_utf8){(const uint8_t*)"scoped wake cancellation and backpressure OK", 44});
(fwrite(dn_tmp_121.data,1,dn_tmp_121.len,stdout),fputc('\n',stdout),0); }));
dn_drop_r14_ScopeCanceller(&v_all);
dn_drop_r18_OperationCanceller(&v_stop);
dn_drop_r10_EventWaker(&v_b);
dn_drop_r10_EventWaker(&v_a);
dn_drop_r12_NetworkScope(&v_group);
dn_drop_resource_h_net_cancel_dispose(&v_cancel);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_main(void) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_main();dn_boundary_end(&dn_b,0);dn_leave();return;
}
DN_EXPORT size_t dlm_native_sizeof_NetText(void){return 16;}
DN_EXPORT size_t dlm_native_alignof_NetText(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_NetText(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NetText_data(void){return 0;}
DN_EXPORT size_t dlm_native_offset_NetText_count(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_NetIo(void){return 16;}
DN_EXPORT size_t dlm_native_alignof_NetIo(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_NetIo(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NetIo_count(void){return 0;}
DN_EXPORT size_t dlm_native_offset_NetIo_status(void){return 8;}
DN_EXPORT size_t dlm_native_offset_NetIo_system(void){return 12;}
DN_EXPORT size_t dlm_native_sizeof_NetOpened(void){return 16;}
DN_EXPORT size_t dlm_native_alignof_NetOpened(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_NetOpened(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NetOpened_handle(void){return 0;}
DN_EXPORT size_t dlm_native_offset_NetOpened_status(void){return 8;}
DN_EXPORT size_t dlm_native_offset_NetOpened_system(void){return 12;}
DN_EXPORT size_t dlm_native_sizeof_NetAddress(void){return 28;}
DN_EXPORT size_t dlm_native_alignof_NetAddress(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_NetAddress(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NetAddress_family(void){return 0;}
DN_EXPORT size_t dlm_native_offset_NetAddress_port(void){return 4;}
DN_EXPORT size_t dlm_native_offset_NetAddress_scope(void){return 8;}
DN_EXPORT size_t dlm_native_offset_NetAddress_bytes(void){return 12;}
DN_EXPORT size_t dlm_native_sizeof_NetResolved(void){return 36;}
DN_EXPORT size_t dlm_native_alignof_NetResolved(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_NetResolved(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NetResolved_address(void){return 0;}
DN_EXPORT size_t dlm_native_offset_NetResolved_status(void){return 28;}
DN_EXPORT size_t dlm_native_offset_NetResolved_system(void){return 32;}
DN_EXPORT size_t dlm_native_sizeof_NetDatagram(void){return 48;}
DN_EXPORT size_t dlm_native_alignof_NetDatagram(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_NetDatagram(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NetDatagram_io(void){return 0;}
DN_EXPORT size_t dlm_native_offset_NetDatagram_peer(void){return 16;}
DN_EXPORT size_t dlm_native_sizeof_NetWebSocketIo(void){return 40;}
DN_EXPORT size_t dlm_native_alignof_NetWebSocketIo(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_NetWebSocketIo(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NetWebSocketIo_io(void){return 0;}
DN_EXPORT size_t dlm_native_offset_NetWebSocketIo_offset(void){return 16;}
DN_EXPORT size_t dlm_native_offset_NetWebSocketIo_remaining(void){return 24;}
DN_EXPORT size_t dlm_native_offset_NetWebSocketIo_flags(void){return 32;}
DN_EXPORT size_t dlm_native_sizeof_NetPoll(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_NetPoll(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_NetPoll(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NetPoll_socket(void){return 0;}
DN_EXPORT size_t dlm_native_offset_NetPoll_events(void){return 8;}
DN_EXPORT size_t dlm_native_offset_NetPoll_ready(void){return 12;}
DN_EXPORT size_t dlm_native_offset_NetPoll_status(void){return 16;}
DN_EXPORT size_t dlm_native_offset_NetPoll_system(void){return 20;}
DN_EXPORT size_t dlm_native_sizeof_NetworkError(void){return 12;}
DN_EXPORT size_t dlm_native_alignof_NetworkError(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_NetworkError(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NetworkError_kind(void){return 0;}
DN_EXPORT size_t dlm_native_offset_NetworkError_system(void){return 4;}
DN_EXPORT size_t dlm_native_offset_NetworkError_operation(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_NetworkTransfer(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_NetworkTransfer(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_NetworkTransfer(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NetworkTransfer_count(void){return 0;}
DN_EXPORT size_t dlm_native_offset_NetworkTransfer_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_TcpSocket(void){return 16;}
DN_EXPORT size_t dlm_native_alignof_TcpSocket(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_TcpSocket(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TcpSocket_inner(void){return 0;}
DN_EXPORT size_t dlm_native_offset_TcpSocket_opened_by(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_TcpListener(void){return 8;}
DN_EXPORT size_t dlm_native_alignof_TcpListener(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_TcpListener(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TcpListener_inner(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_UdpSocket(void){return 8;}
DN_EXPORT size_t dlm_native_alignof_UdpSocket(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_UdpSocket(void){return 1;}
DN_EXPORT size_t dlm_native_offset_UdpSocket_inner(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_TlsSocket(void){return 8;}
DN_EXPORT size_t dlm_native_alignof_TlsSocket(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_TlsSocket(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TlsSocket_inner(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_SocketAddress(void){return 28;}
DN_EXPORT size_t dlm_native_alignof_SocketAddress(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_SocketAddress(void){return 1;}
DN_EXPORT size_t dlm_native_offset_SocketAddress_value(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_SocketAddressResult(void){return 40;}
DN_EXPORT size_t dlm_native_alignof_SocketAddressResult(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_SocketAddressResult(void){return 1;}
DN_EXPORT size_t dlm_native_offset_SocketAddressResult_address(void){return 0;}
DN_EXPORT size_t dlm_native_offset_SocketAddressResult_error(void){return 28;}
DN_EXPORT size_t dlm_native_sizeof_SocketDatagram(void){return 56;}
DN_EXPORT size_t dlm_native_alignof_SocketDatagram(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_SocketDatagram(void){return 1;}
DN_EXPORT size_t dlm_native_offset_SocketDatagram_transfer(void){return 0;}
DN_EXPORT size_t dlm_native_offset_SocketDatagram_peer(void){return 24;}
DN_EXPORT size_t dlm_native_sizeof_SocketOptions(void){return 16;}
DN_EXPORT size_t dlm_native_alignof_SocketOptions(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_SocketOptions(void){return 1;}
DN_EXPORT size_t dlm_native_offset_SocketOptions_no_delay(void){return 0;}
DN_EXPORT size_t dlm_native_offset_SocketOptions_keep_alive(void){return 4;}
DN_EXPORT size_t dlm_native_offset_SocketOptions_send_buffer(void){return 8;}
DN_EXPORT size_t dlm_native_offset_SocketOptions_receive_buffer(void){return 12;}
DN_EXPORT size_t dlm_native_sizeof_NativeSocketOptions(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_NativeSocketOptions(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_NativeSocketOptions(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NativeSocketOptions_options(void){return 0;}
DN_EXPORT size_t dlm_native_offset_NativeSocketOptions_status(void){return 16;}
DN_EXPORT size_t dlm_native_offset_NativeSocketOptions_system(void){return 20;}
DN_EXPORT size_t dlm_native_sizeof_SocketOptionsResult(void){return 28;}
DN_EXPORT size_t dlm_native_alignof_SocketOptionsResult(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_SocketOptionsResult(void){return 1;}
DN_EXPORT size_t dlm_native_offset_SocketOptionsResult_options(void){return 0;}
DN_EXPORT size_t dlm_native_offset_SocketOptionsResult_error(void){return 16;}
DN_EXPORT size_t dlm_native_sizeof_NetworkReady(void){return 16;}
DN_EXPORT size_t dlm_native_alignof_NetworkReady(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_NetworkReady(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NetworkReady_events(void){return 0;}
DN_EXPORT size_t dlm_native_offset_NetworkReady_error(void){return 4;}
DN_EXPORT size_t dlm_native_sizeof_NetworkReactor(void){return 8;}
DN_EXPORT size_t dlm_native_alignof_NetworkReactor(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_NetworkReactor(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NetworkReactor_inner(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_ReactorTcp(void){return 8;}
DN_EXPORT size_t dlm_native_alignof_ReactorTcp(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ReactorTcp(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ReactorTcp_id(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_ReactorListener(void){return 8;}
DN_EXPORT size_t dlm_native_alignof_ReactorListener(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ReactorListener(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ReactorListener_id(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_ReactorUdp(void){return 8;}
DN_EXPORT size_t dlm_native_alignof_ReactorUdp(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ReactorUdp(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ReactorUdp_id(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_ReactorTcpResult(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_ReactorTcpResult(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ReactorTcpResult(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ReactorTcpResult_channel(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ReactorTcpResult_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_ReactorListenerResult(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_ReactorListenerResult(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ReactorListenerResult(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ReactorListenerResult_channel(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ReactorListenerResult_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_ReactorUdpResult(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_ReactorUdpResult(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ReactorUdpResult(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ReactorUdpResult_channel(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ReactorUdpResult_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_NetworkOperation(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_NetworkOperation(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_NetworkOperation(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NetworkOperation_id(void){return 0;}
DN_EXPORT size_t dlm_native_offset_NetworkOperation_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_ReactorConnect(void){return 32;}
DN_EXPORT size_t dlm_native_alignof_ReactorConnect(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ReactorConnect(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ReactorConnect_channel(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ReactorConnect_operation(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_ReactorCompletion(void){return 80;}
DN_EXPORT size_t dlm_native_alignof_ReactorCompletion(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ReactorCompletion(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ReactorCompletion_operation(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ReactorCompletion_channel(void){return 8;}
DN_EXPORT size_t dlm_native_offset_ReactorCompletion_accepted(void){return 16;}
DN_EXPORT size_t dlm_native_offset_ReactorCompletion_transfer(void){return 24;}
DN_EXPORT size_t dlm_native_offset_ReactorCompletion_kind(void){return 48;}
DN_EXPORT size_t dlm_native_offset_ReactorCompletion_peer(void){return 52;}
DN_EXPORT size_t dlm_native_sizeof_ReactorStatistics(void){return 56;}
DN_EXPORT size_t dlm_native_alignof_ReactorStatistics(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ReactorStatistics(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ReactorStatistics_buffered_bytes(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ReactorStatistics_byte_limit(void){return 8;}
DN_EXPORT size_t dlm_native_offset_ReactorStatistics_channels(void){return 16;}
DN_EXPORT size_t dlm_native_offset_ReactorStatistics_pending(void){return 20;}
DN_EXPORT size_t dlm_native_offset_ReactorStatistics_completed(void){return 24;}
DN_EXPORT size_t dlm_native_offset_ReactorStatistics_channel_limit(void){return 28;}
DN_EXPORT size_t dlm_native_offset_ReactorStatistics_operation_limit(void){return 32;}
DN_EXPORT size_t dlm_native_offset_ReactorStatistics_backend(void){return 36;}
DN_EXPORT size_t dlm_native_offset_ReactorStatistics_error(void){return 40;}
DN_EXPORT size_t dlm_native_sizeof_ReactorNativeRequest(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_ReactorNativeRequest(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ReactorNativeRequest(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeRequest_id(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeRequest_channel(void){return 8;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeRequest_status(void){return 16;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeRequest_system(void){return 20;}
DN_EXPORT size_t dlm_native_sizeof_ReactorNativeCompletion(void){return 72;}
DN_EXPORT size_t dlm_native_alignof_ReactorNativeCompletion(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ReactorNativeCompletion(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeCompletion_id(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeCompletion_channel(void){return 8;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeCompletion_accepted(void){return 16;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeCompletion_count(void){return 24;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeCompletion_status(void){return 32;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeCompletion_system(void){return 36;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeCompletion_kind(void){return 40;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeCompletion_peer(void){return 44;}
DN_EXPORT size_t dlm_native_sizeof_ReactorNativeStats(void){return 48;}
DN_EXPORT size_t dlm_native_alignof_ReactorNativeStats(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ReactorNativeStats(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeStats_buffered_bytes(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeStats_byte_limit(void){return 8;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeStats_channels(void){return 16;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeStats_pending(void){return 20;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeStats_completed(void){return 24;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeStats_channel_limit(void){return 28;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeStats_operation_limit(void){return 32;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeStats_backend(void){return 36;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeStats_status(void){return 40;}
DN_EXPORT size_t dlm_native_offset_ReactorNativeStats_system(void){return 44;}
DN_EXPORT size_t dlm_native_sizeof_NetworkScope(void){return 8;}
DN_EXPORT size_t dlm_native_alignof_NetworkScope(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_NetworkScope(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NetworkScope_inner(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_EventWaker(void){return 8;}
DN_EXPORT size_t dlm_native_alignof_EventWaker(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_EventWaker(void){return 1;}
DN_EXPORT size_t dlm_native_offset_EventWaker_inner(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_OperationCanceller(void){return 8;}
DN_EXPORT size_t dlm_native_alignof_OperationCanceller(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_OperationCanceller(void){return 1;}
DN_EXPORT size_t dlm_native_offset_OperationCanceller_inner(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_ScopeCanceller(void){return 8;}
DN_EXPORT size_t dlm_native_alignof_ScopeCanceller(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ScopeCanceller(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ScopeCanceller_inner(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_ScopeNativeStatistics(void){return 64;}
DN_EXPORT size_t dlm_native_alignof_ScopeNativeStatistics(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ScopeNativeStatistics(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ScopeNativeStatistics_reactor(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ScopeNativeStatistics_queued(void){return 48;}
DN_EXPORT size_t dlm_native_offset_ScopeNativeStatistics_queue_limit(void){return 52;}
DN_EXPORT size_t dlm_native_offset_ScopeNativeStatistics_kernel_pending(void){return 56;}
DN_EXPORT size_t dlm_native_sizeof_ScopeStatistics(void){return 72;}
DN_EXPORT size_t dlm_native_alignof_ScopeStatistics(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ScopeStatistics(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ScopeStatistics_reactor(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ScopeStatistics_queued(void){return 56;}
DN_EXPORT size_t dlm_native_offset_ScopeStatistics_queue_limit(void){return 60;}
DN_EXPORT size_t dlm_native_offset_ScopeStatistics_kernel_pending(void){return 64;}
int main(void){(void)dlm_native_thread_init();dn_boundary b={0};jmp_buf dn_jump;b.jump=&dn_jump;b.foreign_marker=dn_foreign_context;dn_current=&b;dn_fp_begin(&b);if(setjmp(dn_jump)){dn_boundary_end(&b,1);fprintf(stderr,"DLM native error: %s\n",dn_error);return 1;}(void)dn_f_main();dn_boundary_end(&b,0);(void)dlm_native_runtime_shutdown();return 0;}
