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
typedef struct dn_r_TransportRequirements dn_r_TransportRequirements;
typedef struct dn_r_TransportBounds dn_r_TransportBounds;
typedef struct dn_r_TransportCandidate dn_r_TransportCandidate;
typedef struct dn_r_TransportCheck dn_r_TransportCheck;
typedef struct dn_r_TransportChoice dn_r_TransportChoice;
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
typedef struct dn_r_QuicEndpoint dn_r_QuicEndpoint;
typedef struct dn_r_QuicConnection dn_r_QuicConnection;
typedef struct dn_r_QuicStream dn_r_QuicStream;
typedef struct dn_r_QuicConnectionResult dn_r_QuicConnectionResult;
typedef struct dn_r_QuicStreamResult dn_r_QuicStreamResult;
typedef struct dn_r_QuicConfig dn_r_QuicConfig;
typedef struct dn_r_QuicNativeReference dn_r_QuicNativeReference;
typedef struct dn_r_QuicNativeEvent dn_r_QuicNativeEvent;
typedef struct dn_r_QuicEvent dn_r_QuicEvent;
typedef struct dn_r_QuicNativeStatistics dn_r_QuicNativeStatistics;
typedef struct dn_r_QuicStatistics dn_r_QuicStatistics;
typedef struct dn_r_TransportSession dn_r_TransportSession;
typedef struct dn_r_TransportTarget dn_r_TransportTarget;
typedef struct dn_r_TransportNativeTarget dn_r_TransportNativeTarget;
typedef struct dn_r_TransportNativeMessage dn_r_TransportNativeMessage;
typedef struct dn_r_TransportNativeDelivery dn_r_TransportNativeDelivery;
typedef struct dn_r_TransportNativeStatistics dn_r_TransportNativeStatistics;
typedef struct dn_r_TransportMessage dn_r_TransportMessage;
typedef struct dn_r_TransportDelivery dn_r_TransportDelivery;
typedef struct dn_r_TransportStatistics dn_r_TransportStatistics;
typedef struct dn_t_resource_r9_NetOpened_net_dispose dn_t_resource_r9_NetOpened_net_dispose;
typedef struct dn_t_resource_r9_NetOpened_quic_dispose dn_t_resource_r9_NetOpened_quic_dispose;
typedef struct dn_t_resource_r9_NetOpened_transport_dispose dn_t_resource_r9_NetOpened_transport_dispose;
typedef struct dn_t_resource_h_net_cancel_dispose dn_t_resource_h_net_cancel_dispose;
struct dn_r_NetOpened {
    void * f_handle;
    int32_t f_status;
    int32_t f_system;
};
struct dn_t_resource_r9_NetOpened_net_dispose { dn_r_NetOpened value;bool active; };
struct dn_t_resource_r9_NetOpened_quic_dispose { dn_r_NetOpened value;bool active; };
struct dn_t_resource_r9_NetOpened_transport_dispose { dn_r_NetOpened value;bool active; };
struct dn_t_resource_h_net_cancel_dispose { void * value;bool active; };
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
struct dn_r_NetworkReady {
    uint32_t f_events;
    dn_r_NetworkError f_error;
};
struct dn_r_NetworkTransfer {
    size_t f_count;
    dn_r_NetworkError f_error;
};
struct dn_r_QuicConfig {
    uint32_t f_connections;
    uint32_t f_streams;
    uint32_t f_events;
    uint32_t f_idle_ms;
    uint32_t f_handshake_ms;
    uint32_t f_congestion;
    size_t f_memory;
    size_t f_window;
};
struct dn_r_QuicConnection {
    uint64_t f_id;
};
struct dn_r_QuicConnectionResult {
    dn_r_QuicConnection f_connection;
    dn_r_NetworkError f_error;
};
struct dn_r_QuicEndpoint {
    dn_t_resource_r9_NetOpened_quic_dispose * f_inner;
};
struct dn_r_QuicStream {
    dn_r_QuicConnection f_connection;
    int64_t f_id;
};
struct dn_r_QuicEvent {
    uint64_t f_id;
    dn_r_QuicStream f_stream;
    uint64_t f_offset;
    uint64_t f_code;
    size_t f_count;
    uint32_t f_kind;
    uint32_t f_flags;
    dn_r_NetworkError f_error;
};
struct dn_r_QuicNativeEvent {
    uint64_t f_id;
    uint64_t f_connection;
    int64_t f_stream;
    uint64_t f_offset;
    uint64_t f_code;
    size_t f_count;
    uint32_t f_kind;
    uint32_t f_flags;
    int32_t f_status;
    int32_t f_system;
};
struct dn_r_QuicNativeReference {
    uint64_t f_connection;
    int64_t f_stream;
    int32_t f_status;
    int32_t f_system;
};
struct dn_r_QuicNativeStatistics {
    size_t f_used;
    size_t f_limit;
    uint64_t f_sent;
    uint64_t f_received;
    uint64_t f_lost;
    uint64_t f_cwnd;
    uint64_t f_in_flight;
    uint64_t f_rtt_ns;
    uint32_t f_connections;
    uint32_t f_streams;
    uint32_t f_events;
    uint32_t f_state;
    uint32_t f_version;
    int32_t f_status;
    int32_t f_system;
};
struct dn_r_QuicStatistics {
    size_t f_used;
    size_t f_limit;
    uint64_t f_sent;
    uint64_t f_received;
    uint64_t f_lost;
    uint64_t f_cwnd;
    uint64_t f_in_flight;
    uint64_t f_rtt_ns;
    uint32_t f_connections;
    uint32_t f_streams;
    uint32_t f_events;
    uint32_t f_state;
    uint32_t f_version;
    dn_r_NetworkError f_error;
};
struct dn_r_QuicStreamResult {
    dn_r_QuicStream f_stream;
    dn_r_NetworkError f_error;
};
struct dn_r_SocketAddress {
    dn_r_NetAddress f_value;
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
struct dn_r_TransportBounds {
    uint32_t f_max_message;
    uint32_t f_queued_messages;
    uint32_t f_channels;
    size_t f_memory;
};
struct dn_r_TransportCandidate {
    uint32_t f_profile;
    bool f_available;
    uint32_t f_cost;
};
struct dn_r_TransportCheck {
    uint32_t f_properties;
    uint32_t f_missing;
    uint32_t f_forbidden;
    int32_t f_status;
};
struct dn_r_TransportChoice {
    uint32_t f_profile;
    uint32_t f_properties;
    uint32_t f_missing;
    uint32_t f_forbidden;
    uint32_t f_preference_misses;
    uint32_t f_cost;
    int32_t f_status;
};
struct dn_r_TransportDelivery {
    uint64_t f_id;
    uint32_t f_channel;
    size_t f_copied;
    size_t f_required;
    dn_r_NetworkError f_error;
};
struct dn_r_TransportMessage {
    uint64_t f_id;
    uint32_t f_channel;
    size_t f_count;
    dn_r_NetworkError f_error;
};
struct dn_r_TransportNativeDelivery {
    uint64_t f_id;
    uint32_t f_channel;
    size_t f_copied;
    size_t f_required;
    int32_t f_status;
    int32_t f_system;
};
struct dn_r_TransportNativeMessage {
    uint64_t f_id;
    uint32_t f_channel;
    size_t f_count;
    int32_t f_status;
    int32_t f_system;
};
struct dn_r_TransportNativeStatistics {
    size_t f_used;
    size_t f_limit;
    size_t f_adapter_limit;
    size_t f_backend_limit;
    uint32_t f_profile;
    uint32_t f_properties;
    uint32_t f_state;
    uint32_t f_send_queued;
    uint32_t f_receive_queued;
    uint32_t f_channels;
    int32_t f_status;
    int32_t f_system;
};
struct dn_r_TransportNativeTarget {
    dn_r_NetAddress f_local;
    dn_r_NetAddress f_peer;
    dn_r_NetText f_identity;
    dn_r_NetText f_ca;
    dn_r_NetText f_pin;
    dn_r_NetText f_alpn;
};
struct dn_r_TransportRequirements {
    uint32_t f_required;
    uint32_t f_forbidden;
    uint32_t f_preferred;
};
struct dn_r_TransportSession {
    dn_t_resource_r9_NetOpened_transport_dispose * f_inner;
};
struct dn_r_TransportStatistics {
    size_t f_used;
    size_t f_limit;
    size_t f_adapter_limit;
    size_t f_backend_limit;
    uint32_t f_profile;
    uint32_t f_properties;
    uint32_t f_state;
    uint32_t f_send_queued;
    uint32_t f_receive_queued;
    uint32_t f_channels;
    dn_r_NetworkError f_error;
};
struct dn_r_TransportTarget {
    dn_r_SocketAddress f_local;
    dn_r_SocketAddress f_peer;
    dn_utf8 f_identity;
    dn_utf8 f_ca_file;
    dn_utf8 f_pin;
};
struct dn_r_UdpSocket {
    dn_t_resource_r9_NetOpened_net_dispose * f_inner;
};
typedef struct dn_t_arr6_r18_TransportCandidate { dn_r_TransportCandidate data[6]; } dn_t_arr6_r18_TransportCandidate;
typedef struct dn_t_buf_U8 { uint8_t *data;size_t len,cap; } dn_t_buf_U8;
typedef struct dn_t_buf_r7_NetPoll { dn_r_NetPoll *data;size_t len,cap; } dn_t_buf_r7_NetPoll;
typedef struct dn_t_slice0_U8 { uint8_t const *data;size_t len; } dn_t_slice0_U8;
_Static_assert(sizeof(dn_r_TransportRequirements)==12,"TransportRequirements ABI size");
_Static_assert(_Alignof(dn_r_TransportRequirements)==4,"TransportRequirements ABI alignment");
_Static_assert(offsetof(dn_r_TransportRequirements,f_required)==0,"TransportRequirements.required ABI offset");
_Static_assert(offsetof(dn_r_TransportRequirements,f_forbidden)==4,"TransportRequirements.forbidden ABI offset");
_Static_assert(offsetof(dn_r_TransportRequirements,f_preferred)==8,"TransportRequirements.preferred ABI offset");
_Static_assert(sizeof(dn_r_TransportBounds)==24,"TransportBounds ABI size");
_Static_assert(_Alignof(dn_r_TransportBounds)==8,"TransportBounds ABI alignment");
_Static_assert(offsetof(dn_r_TransportBounds,f_max_message)==0,"TransportBounds.max_message ABI offset");
_Static_assert(offsetof(dn_r_TransportBounds,f_queued_messages)==4,"TransportBounds.queued_messages ABI offset");
_Static_assert(offsetof(dn_r_TransportBounds,f_channels)==8,"TransportBounds.channels ABI offset");
_Static_assert(offsetof(dn_r_TransportBounds,f_memory)==16,"TransportBounds.memory ABI offset");
_Static_assert(sizeof(dn_r_TransportCandidate)==12,"TransportCandidate ABI size");
_Static_assert(_Alignof(dn_r_TransportCandidate)==4,"TransportCandidate ABI alignment");
_Static_assert(offsetof(dn_r_TransportCandidate,f_profile)==0,"TransportCandidate.profile ABI offset");
_Static_assert(offsetof(dn_r_TransportCandidate,f_available)==4,"TransportCandidate.available ABI offset");
_Static_assert(offsetof(dn_r_TransportCandidate,f_cost)==8,"TransportCandidate.cost ABI offset");
_Static_assert(sizeof(dn_r_TransportCheck)==16,"TransportCheck ABI size");
_Static_assert(_Alignof(dn_r_TransportCheck)==4,"TransportCheck ABI alignment");
_Static_assert(offsetof(dn_r_TransportCheck,f_properties)==0,"TransportCheck.properties ABI offset");
_Static_assert(offsetof(dn_r_TransportCheck,f_missing)==4,"TransportCheck.missing ABI offset");
_Static_assert(offsetof(dn_r_TransportCheck,f_forbidden)==8,"TransportCheck.forbidden ABI offset");
_Static_assert(offsetof(dn_r_TransportCheck,f_status)==12,"TransportCheck.status ABI offset");
_Static_assert(sizeof(dn_r_TransportChoice)==28,"TransportChoice ABI size");
_Static_assert(_Alignof(dn_r_TransportChoice)==4,"TransportChoice ABI alignment");
_Static_assert(offsetof(dn_r_TransportChoice,f_profile)==0,"TransportChoice.profile ABI offset");
_Static_assert(offsetof(dn_r_TransportChoice,f_properties)==4,"TransportChoice.properties ABI offset");
_Static_assert(offsetof(dn_r_TransportChoice,f_missing)==8,"TransportChoice.missing ABI offset");
_Static_assert(offsetof(dn_r_TransportChoice,f_forbidden)==12,"TransportChoice.forbidden ABI offset");
_Static_assert(offsetof(dn_r_TransportChoice,f_preference_misses)==16,"TransportChoice.preference_misses ABI offset");
_Static_assert(offsetof(dn_r_TransportChoice,f_cost)==20,"TransportChoice.cost ABI offset");
_Static_assert(offsetof(dn_r_TransportChoice,f_status)==24,"TransportChoice.status ABI offset");
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
_Static_assert(sizeof(dn_r_QuicEndpoint)==8,"QuicEndpoint ABI size");
_Static_assert(_Alignof(dn_r_QuicEndpoint)==8,"QuicEndpoint ABI alignment");
_Static_assert(offsetof(dn_r_QuicEndpoint,f_inner)==0,"QuicEndpoint.inner ABI offset");
_Static_assert(sizeof(dn_r_QuicConnection)==8,"QuicConnection ABI size");
_Static_assert(_Alignof(dn_r_QuicConnection)==8,"QuicConnection ABI alignment");
_Static_assert(offsetof(dn_r_QuicConnection,f_id)==0,"QuicConnection.id ABI offset");
_Static_assert(sizeof(dn_r_QuicStream)==16,"QuicStream ABI size");
_Static_assert(_Alignof(dn_r_QuicStream)==8,"QuicStream ABI alignment");
_Static_assert(offsetof(dn_r_QuicStream,f_connection)==0,"QuicStream.connection ABI offset");
_Static_assert(offsetof(dn_r_QuicStream,f_id)==8,"QuicStream.id ABI offset");
_Static_assert(sizeof(dn_r_QuicConnectionResult)==24,"QuicConnectionResult ABI size");
_Static_assert(_Alignof(dn_r_QuicConnectionResult)==8,"QuicConnectionResult ABI alignment");
_Static_assert(offsetof(dn_r_QuicConnectionResult,f_connection)==0,"QuicConnectionResult.connection ABI offset");
_Static_assert(offsetof(dn_r_QuicConnectionResult,f_error)==8,"QuicConnectionResult.error ABI offset");
_Static_assert(sizeof(dn_r_QuicStreamResult)==32,"QuicStreamResult ABI size");
_Static_assert(_Alignof(dn_r_QuicStreamResult)==8,"QuicStreamResult ABI alignment");
_Static_assert(offsetof(dn_r_QuicStreamResult,f_stream)==0,"QuicStreamResult.stream ABI offset");
_Static_assert(offsetof(dn_r_QuicStreamResult,f_error)==16,"QuicStreamResult.error ABI offset");
_Static_assert(sizeof(dn_r_QuicConfig)==40,"QuicConfig ABI size");
_Static_assert(_Alignof(dn_r_QuicConfig)==8,"QuicConfig ABI alignment");
_Static_assert(offsetof(dn_r_QuicConfig,f_connections)==0,"QuicConfig.connections ABI offset");
_Static_assert(offsetof(dn_r_QuicConfig,f_streams)==4,"QuicConfig.streams ABI offset");
_Static_assert(offsetof(dn_r_QuicConfig,f_events)==8,"QuicConfig.events ABI offset");
_Static_assert(offsetof(dn_r_QuicConfig,f_idle_ms)==12,"QuicConfig.idle_ms ABI offset");
_Static_assert(offsetof(dn_r_QuicConfig,f_handshake_ms)==16,"QuicConfig.handshake_ms ABI offset");
_Static_assert(offsetof(dn_r_QuicConfig,f_congestion)==20,"QuicConfig.congestion ABI offset");
_Static_assert(offsetof(dn_r_QuicConfig,f_memory)==24,"QuicConfig.memory ABI offset");
_Static_assert(offsetof(dn_r_QuicConfig,f_window)==32,"QuicConfig.window ABI offset");
_Static_assert(sizeof(dn_r_QuicNativeReference)==24,"QuicNativeReference ABI size");
_Static_assert(_Alignof(dn_r_QuicNativeReference)==8,"QuicNativeReference ABI alignment");
_Static_assert(offsetof(dn_r_QuicNativeReference,f_connection)==0,"QuicNativeReference.connection ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeReference,f_stream)==8,"QuicNativeReference.stream ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeReference,f_status)==16,"QuicNativeReference.status ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeReference,f_system)==20,"QuicNativeReference.system ABI offset");
_Static_assert(sizeof(dn_r_QuicNativeEvent)==64,"QuicNativeEvent ABI size");
_Static_assert(_Alignof(dn_r_QuicNativeEvent)==8,"QuicNativeEvent ABI alignment");
_Static_assert(offsetof(dn_r_QuicNativeEvent,f_id)==0,"QuicNativeEvent.id ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeEvent,f_connection)==8,"QuicNativeEvent.connection ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeEvent,f_stream)==16,"QuicNativeEvent.stream ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeEvent,f_offset)==24,"QuicNativeEvent.offset ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeEvent,f_code)==32,"QuicNativeEvent.code ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeEvent,f_count)==40,"QuicNativeEvent.count ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeEvent,f_kind)==48,"QuicNativeEvent.kind ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeEvent,f_flags)==52,"QuicNativeEvent.flags ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeEvent,f_status)==56,"QuicNativeEvent.status ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeEvent,f_system)==60,"QuicNativeEvent.system ABI offset");
_Static_assert(sizeof(dn_r_QuicEvent)==72,"QuicEvent ABI size");
_Static_assert(_Alignof(dn_r_QuicEvent)==8,"QuicEvent ABI alignment");
_Static_assert(offsetof(dn_r_QuicEvent,f_id)==0,"QuicEvent.id ABI offset");
_Static_assert(offsetof(dn_r_QuicEvent,f_stream)==8,"QuicEvent.stream ABI offset");
_Static_assert(offsetof(dn_r_QuicEvent,f_offset)==24,"QuicEvent.offset ABI offset");
_Static_assert(offsetof(dn_r_QuicEvent,f_code)==32,"QuicEvent.code ABI offset");
_Static_assert(offsetof(dn_r_QuicEvent,f_count)==40,"QuicEvent.count ABI offset");
_Static_assert(offsetof(dn_r_QuicEvent,f_kind)==48,"QuicEvent.kind ABI offset");
_Static_assert(offsetof(dn_r_QuicEvent,f_flags)==52,"QuicEvent.flags ABI offset");
_Static_assert(offsetof(dn_r_QuicEvent,f_error)==56,"QuicEvent.error ABI offset");
_Static_assert(sizeof(dn_r_QuicNativeStatistics)==96,"QuicNativeStatistics ABI size");
_Static_assert(_Alignof(dn_r_QuicNativeStatistics)==8,"QuicNativeStatistics ABI alignment");
_Static_assert(offsetof(dn_r_QuicNativeStatistics,f_used)==0,"QuicNativeStatistics.used ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeStatistics,f_limit)==8,"QuicNativeStatistics.limit ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeStatistics,f_sent)==16,"QuicNativeStatistics.sent ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeStatistics,f_received)==24,"QuicNativeStatistics.received ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeStatistics,f_lost)==32,"QuicNativeStatistics.lost ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeStatistics,f_cwnd)==40,"QuicNativeStatistics.cwnd ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeStatistics,f_in_flight)==48,"QuicNativeStatistics.in_flight ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeStatistics,f_rtt_ns)==56,"QuicNativeStatistics.rtt_ns ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeStatistics,f_connections)==64,"QuicNativeStatistics.connections ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeStatistics,f_streams)==68,"QuicNativeStatistics.streams ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeStatistics,f_events)==72,"QuicNativeStatistics.events ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeStatistics,f_state)==76,"QuicNativeStatistics.state ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeStatistics,f_version)==80,"QuicNativeStatistics.version ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeStatistics,f_status)==84,"QuicNativeStatistics.status ABI offset");
_Static_assert(offsetof(dn_r_QuicNativeStatistics,f_system)==88,"QuicNativeStatistics.system ABI offset");
_Static_assert(sizeof(dn_r_QuicStatistics)==96,"QuicStatistics ABI size");
_Static_assert(_Alignof(dn_r_QuicStatistics)==8,"QuicStatistics ABI alignment");
_Static_assert(offsetof(dn_r_QuicStatistics,f_used)==0,"QuicStatistics.used ABI offset");
_Static_assert(offsetof(dn_r_QuicStatistics,f_limit)==8,"QuicStatistics.limit ABI offset");
_Static_assert(offsetof(dn_r_QuicStatistics,f_sent)==16,"QuicStatistics.sent ABI offset");
_Static_assert(offsetof(dn_r_QuicStatistics,f_received)==24,"QuicStatistics.received ABI offset");
_Static_assert(offsetof(dn_r_QuicStatistics,f_lost)==32,"QuicStatistics.lost ABI offset");
_Static_assert(offsetof(dn_r_QuicStatistics,f_cwnd)==40,"QuicStatistics.cwnd ABI offset");
_Static_assert(offsetof(dn_r_QuicStatistics,f_in_flight)==48,"QuicStatistics.in_flight ABI offset");
_Static_assert(offsetof(dn_r_QuicStatistics,f_rtt_ns)==56,"QuicStatistics.rtt_ns ABI offset");
_Static_assert(offsetof(dn_r_QuicStatistics,f_connections)==64,"QuicStatistics.connections ABI offset");
_Static_assert(offsetof(dn_r_QuicStatistics,f_streams)==68,"QuicStatistics.streams ABI offset");
_Static_assert(offsetof(dn_r_QuicStatistics,f_events)==72,"QuicStatistics.events ABI offset");
_Static_assert(offsetof(dn_r_QuicStatistics,f_state)==76,"QuicStatistics.state ABI offset");
_Static_assert(offsetof(dn_r_QuicStatistics,f_version)==80,"QuicStatistics.version ABI offset");
_Static_assert(offsetof(dn_r_QuicStatistics,f_error)==84,"QuicStatistics.error ABI offset");
_Static_assert(sizeof(dn_r_TransportSession)==8,"TransportSession ABI size");
_Static_assert(_Alignof(dn_r_TransportSession)==8,"TransportSession ABI alignment");
_Static_assert(offsetof(dn_r_TransportSession,f_inner)==0,"TransportSession.inner ABI offset");
_Static_assert(sizeof(dn_r_TransportTarget)==104,"TransportTarget ABI size");
_Static_assert(_Alignof(dn_r_TransportTarget)==8,"TransportTarget ABI alignment");
_Static_assert(offsetof(dn_r_TransportTarget,f_local)==0,"TransportTarget.local ABI offset");
_Static_assert(offsetof(dn_r_TransportTarget,f_peer)==28,"TransportTarget.peer ABI offset");
_Static_assert(offsetof(dn_r_TransportTarget,f_identity)==56,"TransportTarget.identity ABI offset");
_Static_assert(offsetof(dn_r_TransportTarget,f_ca_file)==72,"TransportTarget.ca_file ABI offset");
_Static_assert(offsetof(dn_r_TransportTarget,f_pin)==88,"TransportTarget.pin ABI offset");
_Static_assert(sizeof(dn_r_TransportNativeTarget)==120,"TransportNativeTarget ABI size");
_Static_assert(_Alignof(dn_r_TransportNativeTarget)==8,"TransportNativeTarget ABI alignment");
_Static_assert(offsetof(dn_r_TransportNativeTarget,f_local)==0,"TransportNativeTarget.local ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeTarget,f_peer)==28,"TransportNativeTarget.peer ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeTarget,f_identity)==56,"TransportNativeTarget.identity ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeTarget,f_ca)==72,"TransportNativeTarget.ca ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeTarget,f_pin)==88,"TransportNativeTarget.pin ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeTarget,f_alpn)==104,"TransportNativeTarget.alpn ABI offset");
_Static_assert(sizeof(dn_r_TransportNativeMessage)==32,"TransportNativeMessage ABI size");
_Static_assert(_Alignof(dn_r_TransportNativeMessage)==8,"TransportNativeMessage ABI alignment");
_Static_assert(offsetof(dn_r_TransportNativeMessage,f_id)==0,"TransportNativeMessage.id ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeMessage,f_channel)==8,"TransportNativeMessage.channel ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeMessage,f_count)==16,"TransportNativeMessage.count ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeMessage,f_status)==24,"TransportNativeMessage.status ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeMessage,f_system)==28,"TransportNativeMessage.system ABI offset");
_Static_assert(sizeof(dn_r_TransportNativeDelivery)==40,"TransportNativeDelivery ABI size");
_Static_assert(_Alignof(dn_r_TransportNativeDelivery)==8,"TransportNativeDelivery ABI alignment");
_Static_assert(offsetof(dn_r_TransportNativeDelivery,f_id)==0,"TransportNativeDelivery.id ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeDelivery,f_channel)==8,"TransportNativeDelivery.channel ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeDelivery,f_copied)==16,"TransportNativeDelivery.copied ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeDelivery,f_required)==24,"TransportNativeDelivery.required ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeDelivery,f_status)==32,"TransportNativeDelivery.status ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeDelivery,f_system)==36,"TransportNativeDelivery.system ABI offset");
_Static_assert(sizeof(dn_r_TransportNativeStatistics)==64,"TransportNativeStatistics ABI size");
_Static_assert(_Alignof(dn_r_TransportNativeStatistics)==8,"TransportNativeStatistics ABI alignment");
_Static_assert(offsetof(dn_r_TransportNativeStatistics,f_used)==0,"TransportNativeStatistics.used ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeStatistics,f_limit)==8,"TransportNativeStatistics.limit ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeStatistics,f_adapter_limit)==16,"TransportNativeStatistics.adapter_limit ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeStatistics,f_backend_limit)==24,"TransportNativeStatistics.backend_limit ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeStatistics,f_profile)==32,"TransportNativeStatistics.profile ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeStatistics,f_properties)==36,"TransportNativeStatistics.properties ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeStatistics,f_state)==40,"TransportNativeStatistics.state ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeStatistics,f_send_queued)==44,"TransportNativeStatistics.send_queued ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeStatistics,f_receive_queued)==48,"TransportNativeStatistics.receive_queued ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeStatistics,f_channels)==52,"TransportNativeStatistics.channels ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeStatistics,f_status)==56,"TransportNativeStatistics.status ABI offset");
_Static_assert(offsetof(dn_r_TransportNativeStatistics,f_system)==60,"TransportNativeStatistics.system ABI offset");
_Static_assert(sizeof(dn_r_TransportMessage)==40,"TransportMessage ABI size");
_Static_assert(_Alignof(dn_r_TransportMessage)==8,"TransportMessage ABI alignment");
_Static_assert(offsetof(dn_r_TransportMessage,f_id)==0,"TransportMessage.id ABI offset");
_Static_assert(offsetof(dn_r_TransportMessage,f_channel)==8,"TransportMessage.channel ABI offset");
_Static_assert(offsetof(dn_r_TransportMessage,f_count)==16,"TransportMessage.count ABI offset");
_Static_assert(offsetof(dn_r_TransportMessage,f_error)==24,"TransportMessage.error ABI offset");
_Static_assert(sizeof(dn_r_TransportDelivery)==48,"TransportDelivery ABI size");
_Static_assert(_Alignof(dn_r_TransportDelivery)==8,"TransportDelivery ABI alignment");
_Static_assert(offsetof(dn_r_TransportDelivery,f_id)==0,"TransportDelivery.id ABI offset");
_Static_assert(offsetof(dn_r_TransportDelivery,f_channel)==8,"TransportDelivery.channel ABI offset");
_Static_assert(offsetof(dn_r_TransportDelivery,f_copied)==16,"TransportDelivery.copied ABI offset");
_Static_assert(offsetof(dn_r_TransportDelivery,f_required)==24,"TransportDelivery.required ABI offset");
_Static_assert(offsetof(dn_r_TransportDelivery,f_error)==32,"TransportDelivery.error ABI offset");
_Static_assert(sizeof(dn_r_TransportStatistics)==72,"TransportStatistics ABI size");
_Static_assert(_Alignof(dn_r_TransportStatistics)==8,"TransportStatistics ABI alignment");
_Static_assert(offsetof(dn_r_TransportStatistics,f_used)==0,"TransportStatistics.used ABI offset");
_Static_assert(offsetof(dn_r_TransportStatistics,f_limit)==8,"TransportStatistics.limit ABI offset");
_Static_assert(offsetof(dn_r_TransportStatistics,f_adapter_limit)==16,"TransportStatistics.adapter_limit ABI offset");
_Static_assert(offsetof(dn_r_TransportStatistics,f_backend_limit)==24,"TransportStatistics.backend_limit ABI offset");
_Static_assert(offsetof(dn_r_TransportStatistics,f_profile)==32,"TransportStatistics.profile ABI offset");
_Static_assert(offsetof(dn_r_TransportStatistics,f_properties)==36,"TransportStatistics.properties ABI offset");
_Static_assert(offsetof(dn_r_TransportStatistics,f_state)==40,"TransportStatistics.state ABI offset");
_Static_assert(offsetof(dn_r_TransportStatistics,f_send_queued)==44,"TransportStatistics.send_queued ABI offset");
_Static_assert(offsetof(dn_r_TransportStatistics,f_receive_queued)==48,"TransportStatistics.receive_queued ABI offset");
_Static_assert(offsetof(dn_r_TransportStatistics,f_channels)==52,"TransportStatistics.channels ABI offset");
_Static_assert(offsetof(dn_r_TransportStatistics,f_error)==56,"TransportStatistics.error ABI offset");
static void dn_drop_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * volatile*);static void dn_retain_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * volatile*);
static void dn_drop_resource_r9_NetOpened_quic_dispose(dn_t_resource_r9_NetOpened_quic_dispose * volatile*);static void dn_retain_resource_r9_NetOpened_quic_dispose(dn_t_resource_r9_NetOpened_quic_dispose * volatile*);
static void dn_drop_resource_r9_NetOpened_transport_dispose(dn_t_resource_r9_NetOpened_transport_dispose * volatile*);static void dn_retain_resource_r9_NetOpened_transport_dispose(dn_t_resource_r9_NetOpened_transport_dispose * volatile*);
static void dn_drop_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * volatile*);static void dn_retain_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * volatile*);
static void dn_drop_r12_QuicEndpoint(dn_r_QuicEndpoint volatile*);static void dn_retain_r12_QuicEndpoint(dn_r_QuicEndpoint volatile*);
static void dn_drop_r11_TcpListener(dn_r_TcpListener volatile*);static void dn_retain_r11_TcpListener(dn_r_TcpListener volatile*);
static void dn_drop_r9_TcpSocket(dn_r_TcpSocket volatile*);static void dn_retain_r9_TcpSocket(dn_r_TcpSocket volatile*);
static void dn_drop_r9_TlsSocket(dn_r_TlsSocket volatile*);static void dn_retain_r9_TlsSocket(dn_r_TlsSocket volatile*);
static void dn_drop_r16_TransportSession(dn_r_TransportSession volatile*);static void dn_retain_r16_TransportSession(dn_r_TransportSession volatile*);
static void dn_drop_r9_UdpSocket(dn_r_UdpSocket volatile*);static void dn_retain_r9_UdpSocket(dn_r_UdpSocket volatile*);
static void dn_drop_buf_U8(dn_t_buf_U8 volatile*);static void dn_retain_buf_U8(dn_t_buf_U8 volatile*);
static void dn_drop_buf_r7_NetPoll(dn_t_buf_r7_NetPoll volatile*);static void dn_retain_buf_r7_NetPoll(dn_t_buf_r7_NetPoll volatile*);
static void dn_adopt_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * volatile*);
static void dn_adopt_resource_r9_NetOpened_quic_dispose(dn_t_resource_r9_NetOpened_quic_dispose * volatile*);
static void dn_adopt_resource_r9_NetOpened_transport_dispose(dn_t_resource_r9_NetOpened_transport_dispose * volatile*);
static void dn_adopt_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * volatile*);
static void dn_adopt_r12_QuicEndpoint(dn_r_QuicEndpoint volatile*);
static void dn_adopt_r11_TcpListener(dn_r_TcpListener volatile*);
static void dn_adopt_r9_TcpSocket(dn_r_TcpSocket volatile*);
static void dn_adopt_r9_TlsSocket(dn_r_TlsSocket volatile*);
static void dn_adopt_r16_TransportSession(dn_r_TransportSession volatile*);
static void dn_adopt_r9_UdpSocket(dn_r_UdpSocket volatile*);
static void dn_adopt_buf_U8(dn_t_buf_U8 volatile*);
static void dn_adopt_buf_r7_NetPoll(dn_t_buf_r7_NetPoll volatile*);
static uint32_t dn_f_transport_reliable(void);
static uint32_t dn_f_transport_ordered(void);
static uint32_t dn_f_transport_secure(void);
static uint32_t dn_f_transport_streams(void);
static uint32_t dn_f_transport_messages(void);
static uint32_t dn_f_transport_congestion_control(void);
static uint32_t dn_f_transport_unordered(void);
static uint32_t dn_f_transport_bounded_storage(void);
static uint32_t dn_f_transport_total_memory_bound(void);
static dn_r_TransportBounds dn_f_transport_bounds(void);
static dn_r_TransportRequirements dn_f_transport_requirements(uint32_t v_required,uint32_t v_forbidden,uint32_t v_preferred);
static uint32_t dn_f_transport_profile_properties(uint32_t v_profile);
static bool dn_f_transport_requirements_valid(dn_r_TransportRequirements v_requirements);
static bool dn_f_transport_bounds_valid(dn_r_TransportBounds v_bounds);
static dn_r_TransportCheck dn_f_transport_check(uint32_t v_profile,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds);
static uint32_t dn_f_transport_preference_misses(uint32_t v_properties,uint32_t v_preferred);
static dn_r_TransportChoice dn_f_transport_select(dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds,dn_t_arr6_r18_TransportCandidate v_candidates);
static dn_t_arr6_r18_TransportCandidate dn_f_transport_candidates(uint32_t v_available);
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
static uint8_t dn_f_quic_dispose(dn_r_NetOpened * v_state);
static void * dn_f_quic_handle(dn_r_QuicEndpoint const * v_endpoint);
static dn_r_QuicConfig dn_f_quic_config(void);
static bool dn_f_quic_available(void);
static dn_r_QuicEndpoint dn_f_quic_client(dn_r_SocketAddress v_address,dn_utf8 v_alpn,dn_utf8 v_ca_file,dn_r_QuicConfig v_config);
static dn_r_QuicEndpoint dn_f_quic_server(dn_r_SocketAddress v_address,dn_utf8 v_alpn,dn_utf8 v_certificate,dn_utf8 v_key,dn_r_QuicConfig v_config);
static dn_r_NetworkError dn_f_quic_error(dn_r_QuicEndpoint const * v_endpoint);
static uint8_t dn_f_quic_close(dn_r_QuicEndpoint v_endpoint);
static dn_r_SocketAddressResult dn_f_quic_local(dn_r_QuicEndpoint const * v_endpoint);
static dn_r_QuicConnectionResult dn_f_quic_connect(dn_r_QuicEndpoint * v_endpoint,dn_r_SocketAddress v_address,dn_utf8 v_hostname);
static dn_r_NetworkTransfer dn_f_quic_poll(dn_r_QuicEndpoint * v_endpoint,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_QuicEvent dn_f_quic_next(dn_r_QuicEndpoint const * v_endpoint);
static dn_r_NetworkTransfer dn_f_quic_copy(dn_r_QuicEndpoint const * v_endpoint,uint64_t v_event,size_t v_offset,dn_t_buf_U8 * v_output);
static dn_r_NetworkTransfer dn_f_quic_ack(dn_r_QuicEndpoint * v_endpoint,uint64_t v_event);
static dn_r_QuicStreamResult dn_f_quic_open_stream(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection,bool v_unidirectional);
static dn_r_NetworkTransfer dn_f_quic_write(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicStream v_stream,dn_t_slice0_U8 v_bytes,bool v_fin);
static dn_r_NetworkTransfer dn_f_quic_datagram(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection,dn_t_slice0_U8 v_bytes);
static dn_r_NetworkTransfer dn_f_quic_reset(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicStream v_stream,uint64_t v_code);
static dn_r_NetworkTransfer dn_f_quic_disconnect(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection,uint64_t v_code);
static dn_r_NetworkTransfer dn_f_quic_forget(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection);
static dn_r_NetworkTransfer dn_f_quic_migrate(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection,dn_r_SocketAddress v_address);
static dn_r_QuicStatistics dn_f_quic_statistics(dn_r_QuicEndpoint const * v_endpoint,dn_r_QuicConnection v_connection);
static bool dn_f_quic_event_fin(dn_r_QuicEvent const * v_event);
static dn_r_NetIo dn_f_quic_provider_shutdown(void);
static uint8_t dn_f_transport_dispose(dn_r_NetOpened * v_state);
static void * dn_f_transport_handle(dn_r_TransportSession const * v_session);
static dn_r_NetworkError dn_f_transport_error(dn_r_TransportSession const * v_session);
static uint8_t dn_f_transport_close(dn_r_TransportSession v_session);
static uint32_t dn_f_transport_available(void);
static dn_r_TransportTarget dn_f_transport_target(dn_r_SocketAddress v_peer,dn_utf8 v_identity,dn_utf8 v_ca_file,dn_utf8 v_pin);
static dn_utf8 dn_f_transport_alpn(uint32_t v_profile);
static dn_r_QuicConfig dn_f_transport_quic_config(dn_r_TransportBounds v_bounds);
static dn_r_TransportSession dn_f_transport_open(dn_r_TransportChoice v_choice,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds,dn_r_TransportTarget v_target,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_TransportSession dn_f_transport_adopt_tcp(dn_r_TcpSocket v_socket,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds);
static dn_r_TransportSession dn_f_transport_adopt_tls(dn_r_TlsSocket v_socket,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds);
static dn_r_TransportSession dn_f_transport_adopt_udp(dn_r_UdpSocket v_socket,dn_r_SocketAddress v_peer,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds);
static dn_r_TransportSession dn_f_transport_adopt_quic(dn_r_QuicEndpoint v_endpoint,dn_r_QuicConnection v_connection,bool v_initiator,uint32_t v_profile,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds);
static dn_r_SocketAddressResult dn_f_transport_local(dn_r_TransportSession const * v_session);
static dn_r_NetworkTransfer dn_f_transport_open_channel(dn_r_TransportSession * v_session);
static dn_r_NetworkTransfer dn_f_transport_send(dn_r_TransportSession * v_session,uint32_t v_channel,dn_t_slice0_U8 v_bytes);
static dn_r_NetworkTransfer dn_f_transport_poll(dn_r_TransportSession * v_session,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_TransportMessage dn_f_transport_peek(dn_r_TransportSession const * v_session);
static dn_r_NetworkTransfer dn_f_transport_copy(dn_r_TransportSession const * v_session,uint64_t v_message,size_t v_offset,dn_t_buf_U8 * v_output);
static dn_r_NetworkTransfer dn_f_transport_ack(dn_r_TransportSession * v_session,uint64_t v_message);
static dn_r_TransportDelivery dn_f_transport_receive(dn_r_TransportSession * v_session,dn_t_buf_U8 * v_output);
static dn_r_TransportStatistics dn_f_transport_statistics(dn_r_TransportSession const * v_session);
static uint8_t dn_f_exchange(dn_r_TransportSession * v_client,dn_r_TransportSession * v_server,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static uint8_t dn_f_main(void);
static void dn_drop_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * volatile *x); static void dn_retain_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * volatile *x);
static void dn_final_resource_r9_NetOpened_net_dispose(void *raw){dn_t_resource_r9_NetOpened_net_dispose *p=raw;if(p->active){p->active=false;(void)dn_f_net_dispose(&p->value);}}
static dn_t_resource_r9_NetOpened_net_dispose * dn_begin_resource_r9_NetOpened_net_dispose(void){dn_t_resource_r9_NetOpened_net_dispose *p=dn_allocate_aligned(sizeof *p,_Alignof(dn_t_resource_r9_NetOpened_net_dispose));((dn_mem*)p-1)->finalize=dn_final_resource_r9_NetOpened_net_dispose;return p;}
static dn_t_resource_r9_NetOpened_net_dispose * dn_commit_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * p,dn_r_NetOpened v){p->value=v;p->active=true;return p;}
static void dn_drop_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * volatile *x){if(*x){dn_t_resource_r9_NetOpened_net_dispose *p=*x;*x=NULL;((dn_mem*)p-1)->finalize=NULL;dn_final_resource_r9_NetOpened_net_dispose(p);dn_deallocate(p);}}
static void dn_retain_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * volatile *x){if(*x)dn_detach(*x);}
static dn_r_NetOpened dn_take_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * p){dn_nonnull(p);dn_r_NetOpened v=p->value;p->active=false;dn_deallocate(p);return v;}
static void dn_adopt_resource_r9_NetOpened_net_dispose(dn_t_resource_r9_NetOpened_net_dispose * volatile*x){dn_adopt(*x);}
static void dn_drop_resource_r9_NetOpened_quic_dispose(dn_t_resource_r9_NetOpened_quic_dispose * volatile *x); static void dn_retain_resource_r9_NetOpened_quic_dispose(dn_t_resource_r9_NetOpened_quic_dispose * volatile *x);
static void dn_final_resource_r9_NetOpened_quic_dispose(void *raw){dn_t_resource_r9_NetOpened_quic_dispose *p=raw;if(p->active){p->active=false;(void)dn_f_quic_dispose(&p->value);}}
static dn_t_resource_r9_NetOpened_quic_dispose * dn_begin_resource_r9_NetOpened_quic_dispose(void){dn_t_resource_r9_NetOpened_quic_dispose *p=dn_allocate_aligned(sizeof *p,_Alignof(dn_t_resource_r9_NetOpened_quic_dispose));((dn_mem*)p-1)->finalize=dn_final_resource_r9_NetOpened_quic_dispose;return p;}
static dn_t_resource_r9_NetOpened_quic_dispose * dn_commit_resource_r9_NetOpened_quic_dispose(dn_t_resource_r9_NetOpened_quic_dispose * p,dn_r_NetOpened v){p->value=v;p->active=true;return p;}
static void dn_drop_resource_r9_NetOpened_quic_dispose(dn_t_resource_r9_NetOpened_quic_dispose * volatile *x){if(*x){dn_t_resource_r9_NetOpened_quic_dispose *p=*x;*x=NULL;((dn_mem*)p-1)->finalize=NULL;dn_final_resource_r9_NetOpened_quic_dispose(p);dn_deallocate(p);}}
static void dn_retain_resource_r9_NetOpened_quic_dispose(dn_t_resource_r9_NetOpened_quic_dispose * volatile *x){if(*x)dn_detach(*x);}
static dn_r_NetOpened dn_take_resource_r9_NetOpened_quic_dispose(dn_t_resource_r9_NetOpened_quic_dispose * p){dn_nonnull(p);dn_r_NetOpened v=p->value;p->active=false;dn_deallocate(p);return v;}
static void dn_adopt_resource_r9_NetOpened_quic_dispose(dn_t_resource_r9_NetOpened_quic_dispose * volatile*x){dn_adopt(*x);}
static void dn_drop_resource_r9_NetOpened_transport_dispose(dn_t_resource_r9_NetOpened_transport_dispose * volatile *x); static void dn_retain_resource_r9_NetOpened_transport_dispose(dn_t_resource_r9_NetOpened_transport_dispose * volatile *x);
static void dn_final_resource_r9_NetOpened_transport_dispose(void *raw){dn_t_resource_r9_NetOpened_transport_dispose *p=raw;if(p->active){p->active=false;(void)dn_f_transport_dispose(&p->value);}}
static dn_t_resource_r9_NetOpened_transport_dispose * dn_begin_resource_r9_NetOpened_transport_dispose(void){dn_t_resource_r9_NetOpened_transport_dispose *p=dn_allocate_aligned(sizeof *p,_Alignof(dn_t_resource_r9_NetOpened_transport_dispose));((dn_mem*)p-1)->finalize=dn_final_resource_r9_NetOpened_transport_dispose;return p;}
static dn_t_resource_r9_NetOpened_transport_dispose * dn_commit_resource_r9_NetOpened_transport_dispose(dn_t_resource_r9_NetOpened_transport_dispose * p,dn_r_NetOpened v){p->value=v;p->active=true;return p;}
static void dn_drop_resource_r9_NetOpened_transport_dispose(dn_t_resource_r9_NetOpened_transport_dispose * volatile *x){if(*x){dn_t_resource_r9_NetOpened_transport_dispose *p=*x;*x=NULL;((dn_mem*)p-1)->finalize=NULL;dn_final_resource_r9_NetOpened_transport_dispose(p);dn_deallocate(p);}}
static void dn_retain_resource_r9_NetOpened_transport_dispose(dn_t_resource_r9_NetOpened_transport_dispose * volatile *x){if(*x)dn_detach(*x);}
static dn_r_NetOpened dn_take_resource_r9_NetOpened_transport_dispose(dn_t_resource_r9_NetOpened_transport_dispose * p){dn_nonnull(p);dn_r_NetOpened v=p->value;p->active=false;dn_deallocate(p);return v;}
static void dn_adopt_resource_r9_NetOpened_transport_dispose(dn_t_resource_r9_NetOpened_transport_dispose * volatile*x){dn_adopt(*x);}
static void dn_drop_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * volatile *x); static void dn_retain_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * volatile *x);
static void dn_final_resource_h_net_cancel_dispose(void *raw){dn_t_resource_h_net_cancel_dispose *p=raw;if(p->active){p->active=false;(void)dn_f_net_cancel_dispose(&p->value);}}
static dn_t_resource_h_net_cancel_dispose * dn_begin_resource_h_net_cancel_dispose(void){dn_t_resource_h_net_cancel_dispose *p=dn_allocate_aligned(sizeof *p,_Alignof(dn_t_resource_h_net_cancel_dispose));((dn_mem*)p-1)->finalize=dn_final_resource_h_net_cancel_dispose;return p;}
static dn_t_resource_h_net_cancel_dispose * dn_commit_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * p,void * v){p->value=v;p->active=true;return p;}
static void dn_drop_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * volatile *x){if(*x){dn_t_resource_h_net_cancel_dispose *p=*x;*x=NULL;((dn_mem*)p-1)->finalize=NULL;dn_final_resource_h_net_cancel_dispose(p);dn_deallocate(p);}}
static void dn_retain_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * volatile *x){if(*x)dn_detach(*x);}
static void * dn_take_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * p){dn_nonnull(p);void * v=p->value;p->active=false;dn_deallocate(p);return v;}
static void dn_adopt_resource_h_net_cancel_dispose(dn_t_resource_h_net_cancel_dispose * volatile*x){dn_adopt(*x);}
static void dn_drop_r12_QuicEndpoint(dn_r_QuicEndpoint volatile *x); static void dn_retain_r12_QuicEndpoint(dn_r_QuicEndpoint volatile *x);
static void dn_drop_r12_QuicEndpoint(dn_r_QuicEndpoint volatile *x){dn_drop_resource_r9_NetOpened_quic_dispose(&x->f_inner);}
static void dn_retain_r12_QuicEndpoint(dn_r_QuicEndpoint volatile *x){dn_retain_resource_r9_NetOpened_quic_dispose(&x->f_inner);}
static void dn_adopt_r12_QuicEndpoint(dn_r_QuicEndpoint volatile*x){dn_adopt_resource_r9_NetOpened_quic_dispose(&x->f_inner);}
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
static void dn_drop_r16_TransportSession(dn_r_TransportSession volatile *x); static void dn_retain_r16_TransportSession(dn_r_TransportSession volatile *x);
static void dn_drop_r16_TransportSession(dn_r_TransportSession volatile *x){dn_drop_resource_r9_NetOpened_transport_dispose(&x->f_inner);}
static void dn_retain_r16_TransportSession(dn_r_TransportSession volatile *x){dn_retain_resource_r9_NetOpened_transport_dispose(&x->f_inner);}
static void dn_adopt_r16_TransportSession(dn_r_TransportSession volatile*x){dn_adopt_resource_r9_NetOpened_transport_dispose(&x->f_inner);}
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
static uint32_t dn_cb_transport_reliable(void);
static uint32_t dn_cb_transport_ordered(void);
static uint32_t dn_cb_transport_secure(void);
static uint32_t dn_cb_transport_streams(void);
static uint32_t dn_cb_transport_messages(void);
static uint32_t dn_cb_transport_congestion_control(void);
static uint32_t dn_cb_transport_unordered(void);
static uint32_t dn_cb_transport_bounded_storage(void);
static uint32_t dn_cb_transport_total_memory_bound(void);
static dn_r_TransportBounds dn_cb_transport_bounds(void);
static dn_r_TransportRequirements dn_cb_transport_requirements(uint32_t v_required,uint32_t v_forbidden,uint32_t v_preferred);
static uint32_t dn_cb_transport_profile_properties(uint32_t v_profile);
static bool dn_cb_transport_requirements_valid(dn_r_TransportRequirements v_requirements);
static bool dn_cb_transport_bounds_valid(dn_r_TransportBounds v_bounds);
static dn_r_TransportCheck dn_cb_transport_check(uint32_t v_profile,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds);
static uint32_t dn_cb_transport_preference_misses(uint32_t v_properties,uint32_t v_preferred);
static dn_r_TransportChoice dn_cb_transport_select(dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds,dn_t_arr6_r18_TransportCandidate v_candidates);
static dn_t_arr6_r18_TransportCandidate dn_cb_transport_candidates(uint32_t v_available);
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
extern uint32_t dn_ext_dlm_net_quic_version(void) __asm__("dlm_net_quic_version");
extern dn_r_NetIo dn_ext_dlm_net_quic_shutdown(void) __asm__("dlm_net_quic_shutdown");
extern dn_r_NetOpened dn_ext_dlm_net_quic_open(dn_r_NetAddress v_address,bool v_server,dn_r_NetText v_alpn,dn_r_NetText v_certificate,dn_r_NetText v_key,dn_r_QuicConfig v_config) __asm__("dlm_net_quic_open");
extern void dn_ext_dlm_net_quic_close(void * v_endpoint) __asm__("dlm_net_quic_close");
extern dn_r_NetResolved dn_ext_dlm_net_quic_local(void * v_endpoint) __asm__("dlm_net_quic_local");
extern dn_r_QuicNativeReference dn_ext_dlm_net_quic_connect(void * v_endpoint,dn_r_NetAddress v_address,dn_r_NetText v_name) __asm__("dlm_net_quic_connect");
extern dn_r_NetIo dn_ext_dlm_net_quic_poll(void * v_endpoint,uint32_t v_timeout_ms,void * v_cancel) __asm__("dlm_net_quic_poll");
extern dn_r_QuicNativeEvent dn_ext_dlm_net_quic_next(void * v_endpoint) __asm__("dlm_net_quic_next");
extern dn_r_NetIo dn_ext_dlm_net_quic_copy(void * v_endpoint,uint64_t v_event,size_t v_offset,uint8_t * v_output,size_t v_capacity) __asm__("dlm_net_quic_copy");
extern dn_r_NetIo dn_ext_dlm_net_quic_ack(void * v_endpoint,uint64_t v_event) __asm__("dlm_net_quic_ack");
extern dn_r_QuicNativeReference dn_ext_dlm_net_quic_stream(void * v_endpoint,uint64_t v_connection,bool v_unidirectional) __asm__("dlm_net_quic_stream");
extern dn_r_NetIo dn_ext_dlm_net_quic_write(void * v_endpoint,uint64_t v_connection,int64_t v_stream,uint8_t * v_bytes,size_t v_count,bool v_fin) __asm__("dlm_net_quic_write");
extern dn_r_NetIo dn_ext_dlm_net_quic_datagram(void * v_endpoint,uint64_t v_connection,uint8_t * v_bytes,size_t v_count) __asm__("dlm_net_quic_datagram");
extern dn_r_NetIo dn_ext_dlm_net_quic_reset(void * v_endpoint,uint64_t v_connection,int64_t v_stream,uint64_t v_code) __asm__("dlm_net_quic_reset");
extern dn_r_NetIo dn_ext_dlm_net_quic_disconnect(void * v_endpoint,uint64_t v_connection,uint64_t v_code) __asm__("dlm_net_quic_disconnect");
extern dn_r_NetIo dn_ext_dlm_net_quic_forget(void * v_endpoint,uint64_t v_connection) __asm__("dlm_net_quic_forget");
extern dn_r_NetIo dn_ext_dlm_net_quic_migrate(void * v_endpoint,uint64_t v_connection,dn_r_NetAddress v_address) __asm__("dlm_net_quic_migrate");
extern dn_r_QuicNativeStatistics dn_ext_dlm_net_quic_stats(void * v_endpoint,uint64_t v_connection) __asm__("dlm_net_quic_stats");
static void dn_cb_quic_dispose(dn_r_NetOpened * v_state);
static void * dn_cb_quic_handle(dn_r_QuicEndpoint const * v_endpoint);
static dn_r_QuicConfig dn_cb_quic_config(void);
static bool dn_cb_quic_available(void);
static dn_r_QuicEndpoint dn_cb_quic_client(dn_r_SocketAddress v_address,dn_utf8 v_alpn,dn_utf8 v_ca_file,dn_r_QuicConfig v_config);
static dn_r_QuicEndpoint dn_cb_quic_server(dn_r_SocketAddress v_address,dn_utf8 v_alpn,dn_utf8 v_certificate,dn_utf8 v_key,dn_r_QuicConfig v_config);
static dn_r_NetworkError dn_cb_quic_error(dn_r_QuicEndpoint const * v_endpoint);
static void dn_cb_quic_close(dn_r_QuicEndpoint v_endpoint);
static dn_r_SocketAddressResult dn_cb_quic_local(dn_r_QuicEndpoint const * v_endpoint);
static dn_r_QuicConnectionResult dn_cb_quic_connect(dn_r_QuicEndpoint * v_endpoint,dn_r_SocketAddress v_address,dn_utf8 v_hostname);
static dn_r_NetworkTransfer dn_cb_quic_poll(dn_r_QuicEndpoint * v_endpoint,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_QuicEvent dn_cb_quic_next(dn_r_QuicEndpoint const * v_endpoint);
static dn_r_NetworkTransfer dn_cb_quic_copy(dn_r_QuicEndpoint const * v_endpoint,uint64_t v_event,size_t v_offset,dn_t_buf_U8 * v_output);
static dn_r_NetworkTransfer dn_cb_quic_ack(dn_r_QuicEndpoint * v_endpoint,uint64_t v_event);
static dn_r_QuicStreamResult dn_cb_quic_open_stream(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection,bool v_unidirectional);
static dn_r_NetworkTransfer dn_cb_quic_write(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicStream v_stream,dn_t_slice0_U8 v_bytes,bool v_fin);
static dn_r_NetworkTransfer dn_cb_quic_datagram(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection,dn_t_slice0_U8 v_bytes);
static dn_r_NetworkTransfer dn_cb_quic_reset(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicStream v_stream,uint64_t v_code);
static dn_r_NetworkTransfer dn_cb_quic_disconnect(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection,uint64_t v_code);
static dn_r_NetworkTransfer dn_cb_quic_forget(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection);
static dn_r_NetworkTransfer dn_cb_quic_migrate(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection,dn_r_SocketAddress v_address);
static dn_r_QuicStatistics dn_cb_quic_statistics(dn_r_QuicEndpoint const * v_endpoint,dn_r_QuicConnection v_connection);
static bool dn_cb_quic_event_fin(dn_r_QuicEvent const * v_event);
static dn_r_NetIo dn_cb_quic_provider_shutdown(void);
extern void dn_ext_dlm_net_transport_close(void * v_session) __asm__("dlm_net_transport_close");
extern uint32_t dn_ext_dlm_net_transport_available(void) __asm__("dlm_net_transport_available");
extern dn_r_NetOpened dn_ext_dlm_net_transport_open(uint32_t v_profile,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds,dn_r_TransportNativeTarget v_target,uint32_t v_timeout_ms,void * v_cancel) __asm__("dlm_net_transport_open");
extern dn_r_NetOpened dn_ext_dlm_net_transport_adopt(dn_r_NetOpened v_opened,uint32_t v_profile,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds,dn_r_NetAddress v_peer,uint64_t v_connection,bool v_initiator) __asm__("dlm_net_transport_adopt");
extern dn_r_NetIo dn_ext_dlm_net_transport_channel(void * v_session) __asm__("dlm_net_transport_channel");
extern dn_r_NetIo dn_ext_dlm_net_transport_send(void * v_session,uint32_t v_channel,uint8_t * v_bytes,size_t v_count) __asm__("dlm_net_transport_send");
extern dn_r_NetIo dn_ext_dlm_net_transport_poll(void * v_session,uint32_t v_timeout_ms,void * v_cancel) __asm__("dlm_net_transport_poll");
extern dn_r_TransportNativeMessage dn_ext_dlm_net_transport_peek(void * v_session) __asm__("dlm_net_transport_peek");
extern dn_r_NetIo dn_ext_dlm_net_transport_copy(void * v_session,uint64_t v_message,size_t v_offset,uint8_t * v_output,size_t v_capacity) __asm__("dlm_net_transport_copy");
extern dn_r_NetIo dn_ext_dlm_net_transport_ack(void * v_session,uint64_t v_message) __asm__("dlm_net_transport_ack");
extern dn_r_TransportNativeDelivery dn_ext_dlm_net_transport_receive(void * v_session,uint8_t * v_output,size_t v_capacity) __asm__("dlm_net_transport_receive");
extern dn_r_TransportNativeStatistics dn_ext_dlm_net_transport_stats(void * v_session) __asm__("dlm_net_transport_stats");
extern dn_r_NetResolved dn_ext_dlm_net_transport_local(void * v_session) __asm__("dlm_net_transport_local");
static void dn_cb_transport_dispose(dn_r_NetOpened * v_state);
static void * dn_cb_transport_handle(dn_r_TransportSession const * v_session);
static dn_r_NetworkError dn_cb_transport_error(dn_r_TransportSession const * v_session);
static void dn_cb_transport_close(dn_r_TransportSession v_session);
static uint32_t dn_cb_transport_available(void);
static dn_r_TransportTarget dn_cb_transport_target(dn_r_SocketAddress v_peer,dn_utf8 v_identity,dn_utf8 v_ca_file,dn_utf8 v_pin);
static dn_utf8 dn_cb_transport_alpn(uint32_t v_profile);
static dn_r_QuicConfig dn_cb_transport_quic_config(dn_r_TransportBounds v_bounds);
static dn_r_TransportSession dn_cb_transport_open(dn_r_TransportChoice v_choice,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds,dn_r_TransportTarget v_target,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_TransportSession dn_cb_transport_adopt_tcp(dn_r_TcpSocket v_socket,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds);
static dn_r_TransportSession dn_cb_transport_adopt_tls(dn_r_TlsSocket v_socket,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds);
static dn_r_TransportSession dn_cb_transport_adopt_udp(dn_r_UdpSocket v_socket,dn_r_SocketAddress v_peer,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds);
static dn_r_TransportSession dn_cb_transport_adopt_quic(dn_r_QuicEndpoint v_endpoint,dn_r_QuicConnection v_connection,bool v_initiator,uint32_t v_profile,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds);
static dn_r_SocketAddressResult dn_cb_transport_local(dn_r_TransportSession const * v_session);
static dn_r_NetworkTransfer dn_cb_transport_open_channel(dn_r_TransportSession * v_session);
static dn_r_NetworkTransfer dn_cb_transport_send(dn_r_TransportSession * v_session,uint32_t v_channel,dn_t_slice0_U8 v_bytes);
static dn_r_NetworkTransfer dn_cb_transport_poll(dn_r_TransportSession * v_session,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static dn_r_TransportMessage dn_cb_transport_peek(dn_r_TransportSession const * v_session);
static dn_r_NetworkTransfer dn_cb_transport_copy(dn_r_TransportSession const * v_session,uint64_t v_message,size_t v_offset,dn_t_buf_U8 * v_output);
static dn_r_NetworkTransfer dn_cb_transport_ack(dn_r_TransportSession * v_session,uint64_t v_message);
static dn_r_TransportDelivery dn_cb_transport_receive(dn_r_TransportSession * v_session,dn_t_buf_U8 * v_output);
static dn_r_TransportStatistics dn_cb_transport_statistics(dn_r_TransportSession const * v_session);
static void dn_cb_exchange(dn_r_TransportSession * v_client,dn_r_TransportSession * v_server,dn_t_resource_h_net_cancel_dispose * const * v_cancel);
static void dn_cb_main(void);
static uint32_t dn_f_transport_reliable(void) {
{
{ uint32_t dn_tmp_0=((uint32_t)(1ULL));return dn_tmp_0; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_transport_reliable(void) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_transport_reliable();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_transport_ordered(void) {
{
{ uint32_t dn_tmp_0=((uint32_t)(2ULL));return dn_tmp_0; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_transport_ordered(void) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_transport_ordered();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_transport_secure(void) {
{
{ uint32_t dn_tmp_0=((uint32_t)(4ULL));return dn_tmp_0; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_transport_secure(void) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_transport_secure();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_transport_streams(void) {
{
{ uint32_t dn_tmp_0=((uint32_t)(8ULL));return dn_tmp_0; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_transport_streams(void) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_transport_streams();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_transport_messages(void) {
{
{ uint32_t dn_tmp_0=((uint32_t)(16ULL));return dn_tmp_0; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_transport_messages(void) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_transport_messages();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_transport_congestion_control(void) {
{
{ uint32_t dn_tmp_0=((uint32_t)(32ULL));return dn_tmp_0; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_transport_congestion_control(void) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_transport_congestion_control();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_transport_unordered(void) {
{
{ uint32_t dn_tmp_0=((uint32_t)(64ULL));return dn_tmp_0; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_transport_unordered(void) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_transport_unordered();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_transport_bounded_storage(void) {
{
{ uint32_t dn_tmp_0=((uint32_t)(128ULL));return dn_tmp_0; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_transport_bounded_storage(void) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_transport_bounded_storage();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_transport_total_memory_bound(void) {
{
{ uint32_t dn_tmp_0=((uint32_t)(256ULL));return dn_tmp_0; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_transport_total_memory_bound(void) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_transport_total_memory_bound();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TransportBounds dn_f_transport_bounds(void) {
{
{ dn_r_TransportBounds dn_tmp_4=({ uint32_t dn_tmp_0=((uint32_t)(65536ULL));
uint32_t dn_tmp_1=((uint32_t)(32ULL));
uint32_t dn_tmp_2=((uint32_t)(16ULL));
size_t dn_tmp_3=((size_t)(8388608ULL));
((dn_r_TransportBounds){dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3}); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_TransportBounds dn_cb_transport_bounds(void) {
 if(!dn_enter()){return (dn_r_TransportBounds){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TransportBounds){0};}
 dn_r_TransportBounds dn_result=dn_f_transport_bounds();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TransportRequirements dn_f_transport_requirements(uint32_t v_required,uint32_t v_forbidden,uint32_t v_preferred) {
{
{ dn_r_TransportRequirements dn_tmp_3=({ uint32_t dn_tmp_0=v_required;
uint32_t dn_tmp_1=v_forbidden;
uint32_t dn_tmp_2=v_preferred;
((dn_r_TransportRequirements){dn_tmp_0,dn_tmp_1,dn_tmp_2}); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static dn_r_TransportRequirements dn_cb_transport_requirements(uint32_t v_required,uint32_t v_forbidden,uint32_t v_preferred) {
 if(!dn_enter()){return (dn_r_TransportRequirements){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TransportRequirements){0};}
 dn_r_TransportRequirements dn_result=dn_f_transport_requirements(v_required,v_forbidden,v_preferred);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_transport_profile_properties(uint32_t v_profile) {
{
if(({ uint32_t dn_tmp_0=v_profile;
uint32_t dn_tmp_1=((uint32_t)(1ULL));
(dn_tmp_0 == dn_tmp_1); })){
{ uint32_t dn_tmp_2=((uint32_t)(179ULL));return dn_tmp_2; }
}
else{
}
if(({ uint32_t dn_tmp_3=v_profile;
uint32_t dn_tmp_4=((uint32_t)(2ULL));
(dn_tmp_3 == dn_tmp_4); })){
{ uint32_t dn_tmp_5=((uint32_t)(183ULL));return dn_tmp_5; }
}
else{
}
if(({ uint32_t dn_tmp_6=v_profile;
uint32_t dn_tmp_7=((uint32_t)(3ULL));
(dn_tmp_6 == dn_tmp_7); })){
{ uint32_t dn_tmp_8=((uint32_t)(208ULL));return dn_tmp_8; }
}
else{
}
if(({ uint32_t dn_tmp_9=v_profile;
uint32_t dn_tmp_10=((uint32_t)(4ULL));
(dn_tmp_9 == dn_tmp_10); })){
{ uint32_t dn_tmp_11=((uint32_t)(191ULL));return dn_tmp_11; }
}
else{
}
if(({ uint32_t dn_tmp_12=v_profile;
uint32_t dn_tmp_13=((uint32_t)(5ULL));
(dn_tmp_12 == dn_tmp_13); })){
{ uint32_t dn_tmp_14=((uint32_t)(253ULL));return dn_tmp_14; }
}
else{
}
if(({ uint32_t dn_tmp_15=v_profile;
uint32_t dn_tmp_16=((uint32_t)(6ULL));
(dn_tmp_15 == dn_tmp_16); })){
{ uint32_t dn_tmp_17=((uint32_t)(244ULL));return dn_tmp_17; }
}
else{
}
{ uint32_t dn_tmp_18=((uint32_t)(0ULL));return dn_tmp_18; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_transport_profile_properties(uint32_t v_profile) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_transport_profile_properties(v_profile);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_transport_requirements_valid(dn_r_TransportRequirements v_requirements) {
{
if(({ uint32_t dn_tmp_0=({ uint32_t dn_tmp_1=({ uint32_t dn_tmp_2=({ uint32_t dn_tmp_3=(v_requirements).f_required;
uint32_t dn_tmp_4=(v_requirements).f_forbidden;
dn_U32_bits((uint64_t)dn_tmp_3 | (uint64_t)dn_tmp_4); });
uint32_t dn_tmp_5=(v_requirements).f_preferred;
dn_U32_bits((uint64_t)dn_tmp_2 | (uint64_t)dn_tmp_5); });
uint32_t dn_tmp_6=({ uint32_t dn_tmp_7=((uint32_t)(4294966784ULL));
if((unsigned __int128)dn_tmp_7>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_7; });
dn_U32_bits((uint64_t)dn_tmp_1 & (uint64_t)dn_tmp_6); });
uint32_t dn_tmp_8=((uint32_t)(0ULL));
(dn_tmp_0 != dn_tmp_8); })){
{ bool dn_tmp_9=0;return dn_tmp_9; }
}
else{
}
if((({ uint32_t dn_tmp_10=({ uint32_t dn_tmp_11=(v_requirements).f_required;
uint32_t dn_tmp_12=(v_requirements).f_forbidden;
dn_U32_bits((uint64_t)dn_tmp_11 & (uint64_t)dn_tmp_12); });
uint32_t dn_tmp_13=((uint32_t)(0ULL));
(dn_tmp_10 != dn_tmp_13); }) || ({ uint32_t dn_tmp_14=({ uint32_t dn_tmp_15=(v_requirements).f_preferred;
uint32_t dn_tmp_16=(v_requirements).f_forbidden;
dn_U32_bits((uint64_t)dn_tmp_15 & (uint64_t)dn_tmp_16); });
uint32_t dn_tmp_17=((uint32_t)(0ULL));
(dn_tmp_14 != dn_tmp_17); }))){
{ bool dn_tmp_18=0;return dn_tmp_18; }
}
else{
}
{ bool dn_tmp_25=({ uint32_t dn_tmp_19=({ uint32_t dn_tmp_20=(v_requirements).f_required;
uint32_t dn_tmp_21=({ uint32_t dn_tmp_22=((uint32_t)(66ULL));
if((unsigned __int128)dn_tmp_22>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_22; });
dn_U32_bits((uint64_t)dn_tmp_20 & (uint64_t)dn_tmp_21); });
uint32_t dn_tmp_23=({ uint32_t dn_tmp_24=((uint32_t)(66ULL));
if((unsigned __int128)dn_tmp_24>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_24; });
(dn_tmp_19 != dn_tmp_23); });return dn_tmp_25; }
}

dn_fail("missing native return");
}
static bool dn_cb_transport_requirements_valid(dn_r_TransportRequirements v_requirements) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_transport_requirements_valid(v_requirements);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_transport_bounds_valid(dn_r_TransportBounds v_bounds) {
{
if(((((({ uint32_t dn_tmp_0=(v_bounds).f_max_message;
uint32_t dn_tmp_1=((uint32_t)(16777216ULL));
(dn_tmp_0 > dn_tmp_1); }) || ({ uint32_t dn_tmp_2=(v_bounds).f_queued_messages;
uint32_t dn_tmp_3=((uint32_t)(0ULL));
(dn_tmp_2 == dn_tmp_3); })) || ({ uint32_t dn_tmp_4=(v_bounds).f_queued_messages;
uint32_t dn_tmp_5=((uint32_t)(4096ULL));
(dn_tmp_4 > dn_tmp_5); })) || ({ uint32_t dn_tmp_6=(v_bounds).f_channels;
uint32_t dn_tmp_7=((uint32_t)(0ULL));
(dn_tmp_6 == dn_tmp_7); })) || ({ uint32_t dn_tmp_8=(v_bounds).f_channels;
uint32_t dn_tmp_9=((uint32_t)(256ULL));
(dn_tmp_8 > dn_tmp_9); }))){
{ bool dn_tmp_10=0;return dn_tmp_10; }
}
else{
}
if((({ size_t dn_tmp_11=(v_bounds).f_memory;
size_t dn_tmp_12=((size_t)(1024ULL));
(dn_tmp_11 < dn_tmp_12); }) || ({ size_t dn_tmp_13=(v_bounds).f_memory;
size_t dn_tmp_14=((size_t)(67108864ULL));
(dn_tmp_13 > dn_tmp_14); }))){
{ bool dn_tmp_15=0;return dn_tmp_15; }
}
else{
}
size_t v_reserve=({ size_t dn_tmp_16=({ size_t dn_tmp_17=((size_t)(1024ULL));
size_t dn_tmp_18=({ size_t dn_tmp_19=({ uint32_t dn_tmp_20=(v_bounds).f_channels;
if((unsigned __int128)dn_tmp_20>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_20; });
size_t dn_tmp_21=((size_t)(256ULL));
dn_USize_mul(dn_tmp_19,dn_tmp_21); });
dn_USize_add(dn_tmp_17,dn_tmp_18); });
size_t dn_tmp_22=({ size_t dn_tmp_23=((size_t)(2ULL));
size_t dn_tmp_24=({ size_t dn_tmp_25=({ uint32_t dn_tmp_26=(v_bounds).f_max_message;
if((unsigned __int128)dn_tmp_26>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_26; });
size_t dn_tmp_27=((size_t)(128ULL));
dn_USize_add(dn_tmp_25,dn_tmp_27); });
dn_USize_mul(dn_tmp_23,dn_tmp_24); });
dn_USize_add(dn_tmp_16,dn_tmp_22); });
{ bool dn_tmp_30=({ size_t dn_tmp_28=(v_bounds).f_memory;
size_t dn_tmp_29=v_reserve;
(dn_tmp_28 >= dn_tmp_29); });return dn_tmp_30; }
}

dn_fail("missing native return");
}
static bool dn_cb_transport_bounds_valid(dn_r_TransportBounds v_bounds) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_transport_bounds_valid(v_bounds);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TransportCheck dn_f_transport_check(uint32_t v_profile,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds) {
{
uint32_t v_properties=({ uint32_t dn_tmp_0=v_profile;
dn_f_transport_profile_properties(dn_tmp_0); });
if((((!(({ dn_r_TransportRequirements dn_tmp_1=v_requirements;
dn_f_transport_requirements_valid(dn_tmp_1); }))) || (!(({ dn_r_TransportBounds dn_tmp_2=v_bounds;
dn_f_transport_bounds_valid(dn_tmp_2); })))) || ({ uint32_t dn_tmp_3=v_properties;
uint32_t dn_tmp_4=((uint32_t)(0ULL));
(dn_tmp_3 == dn_tmp_4); }))){
{ dn_r_TransportCheck dn_tmp_9=({ uint32_t dn_tmp_5=v_properties;
uint32_t dn_tmp_6=((uint32_t)(0ULL));
uint32_t dn_tmp_7=((uint32_t)(0ULL));
int32_t dn_tmp_8=((int32_t)(4ULL));
((dn_r_TransportCheck){dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8}); });return dn_tmp_9; }
}
else{
}
uint32_t v_missing=({ uint32_t dn_tmp_10=(v_requirements).f_required;
uint32_t dn_tmp_11=({ uint32_t dn_tmp_12=({ uint32_t dn_tmp_13=((uint32_t)(511ULL));
if((unsigned __int128)dn_tmp_13>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_13; });
uint32_t dn_tmp_14=v_properties;
dn_U32_bits((uint64_t)dn_tmp_12 ^ (uint64_t)dn_tmp_14); });
dn_U32_bits((uint64_t)dn_tmp_10 & (uint64_t)dn_tmp_11); });
uint32_t v_forbidden=({ uint32_t dn_tmp_15=(v_requirements).f_forbidden;
uint32_t dn_tmp_16=v_properties;
dn_U32_bits((uint64_t)dn_tmp_15 & (uint64_t)dn_tmp_16); });
if((({ uint32_t dn_tmp_17=v_missing;
uint32_t dn_tmp_18=((uint32_t)(0ULL));
(dn_tmp_17 != dn_tmp_18); }) || ({ uint32_t dn_tmp_19=v_forbidden;
uint32_t dn_tmp_20=((uint32_t)(0ULL));
(dn_tmp_19 != dn_tmp_20); }))){
{ dn_r_TransportCheck dn_tmp_25=({ uint32_t dn_tmp_21=v_properties;
uint32_t dn_tmp_22=v_missing;
uint32_t dn_tmp_23=v_forbidden;
int32_t dn_tmp_24=((int32_t)(6ULL));
((dn_r_TransportCheck){dn_tmp_21,dn_tmp_22,dn_tmp_23,dn_tmp_24}); });return dn_tmp_25; }
}
else{
}
if((({ uint32_t dn_tmp_26=v_profile;
uint32_t dn_tmp_27=((uint32_t)(3ULL));
(dn_tmp_26 == dn_tmp_27); }) && ({ uint32_t dn_tmp_28=(v_bounds).f_max_message;
uint32_t dn_tmp_29=((uint32_t)(65491ULL));
(dn_tmp_28 > dn_tmp_29); }))){
{ dn_r_TransportCheck dn_tmp_34=({ uint32_t dn_tmp_30=v_properties;
uint32_t dn_tmp_31=((uint32_t)(0ULL));
uint32_t dn_tmp_32=((uint32_t)(0ULL));
int32_t dn_tmp_33=((int32_t)(7ULL));
((dn_r_TransportCheck){dn_tmp_30,dn_tmp_31,dn_tmp_32,dn_tmp_33}); });return dn_tmp_34; }
}
else{
}
if((({ uint32_t dn_tmp_35=v_profile;
uint32_t dn_tmp_36=((uint32_t)(6ULL));
(dn_tmp_35 == dn_tmp_36); }) && ({ uint32_t dn_tmp_37=(v_bounds).f_max_message;
uint32_t dn_tmp_38=((uint32_t)(1084ULL));
(dn_tmp_37 > dn_tmp_38); }))){
{ dn_r_TransportCheck dn_tmp_43=({ uint32_t dn_tmp_39=v_properties;
uint32_t dn_tmp_40=((uint32_t)(0ULL));
uint32_t dn_tmp_41=((uint32_t)(0ULL));
int32_t dn_tmp_42=((int32_t)(7ULL));
((dn_r_TransportCheck){dn_tmp_39,dn_tmp_40,dn_tmp_41,dn_tmp_42}); });return dn_tmp_43; }
}
else{
}
if(({ uint32_t dn_tmp_44=v_profile;
uint32_t dn_tmp_45=((uint32_t)(4ULL));
(dn_tmp_44 >= dn_tmp_45); })){
if((({ size_t dn_tmp_46=(v_bounds).f_memory;
size_t dn_tmp_47=((size_t)(2097152ULL));
(dn_tmp_46 < dn_tmp_47); }) || ({ size_t dn_tmp_48=({ size_t dn_tmp_49=({ uint32_t dn_tmp_50=(v_bounds).f_max_message;
if((unsigned __int128)dn_tmp_50>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_50; });
size_t dn_tmp_51=((size_t)(16ULL));
dn_USize_add(dn_tmp_49,dn_tmp_51); });
size_t dn_tmp_52=({ size_t dn_tmp_53=(v_bounds).f_memory;
size_t dn_tmp_54=((size_t)(8ULL));
dn_USize_div(dn_tmp_53,dn_tmp_54); });
(dn_tmp_48 > dn_tmp_52); }))){
{ dn_r_TransportCheck dn_tmp_59=({ uint32_t dn_tmp_55=v_properties;
uint32_t dn_tmp_56=((uint32_t)(0ULL));
uint32_t dn_tmp_57=((uint32_t)(0ULL));
int32_t dn_tmp_58=((int32_t)(7ULL));
((dn_r_TransportCheck){dn_tmp_55,dn_tmp_56,dn_tmp_57,dn_tmp_58}); });return dn_tmp_59; }
}
else{
}
size_t v_reserve=({ size_t dn_tmp_60=({ size_t dn_tmp_61=((size_t)(1024ULL));
size_t dn_tmp_62=({ size_t dn_tmp_63=({ uint32_t dn_tmp_64=(v_bounds).f_channels;
if((unsigned __int128)dn_tmp_64>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_64; });
size_t dn_tmp_65=((size_t)(256ULL));
dn_USize_mul(dn_tmp_63,dn_tmp_65); });
dn_USize_add(dn_tmp_61,dn_tmp_62); });
size_t dn_tmp_66=({ size_t dn_tmp_67=((size_t)(2ULL));
size_t dn_tmp_68=({ size_t dn_tmp_69=({ uint32_t dn_tmp_70=(v_bounds).f_max_message;
if((unsigned __int128)dn_tmp_70>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_70; });
size_t dn_tmp_71=((size_t)(128ULL));
dn_USize_add(dn_tmp_69,dn_tmp_71); });
dn_USize_mul(dn_tmp_67,dn_tmp_68); });
dn_USize_add(dn_tmp_60,dn_tmp_66); });
if(({ size_t dn_tmp_72=({ size_t dn_tmp_73=(v_bounds).f_memory;
size_t dn_tmp_74=((size_t)(2ULL));
dn_USize_div(dn_tmp_73,dn_tmp_74); });
size_t dn_tmp_75=v_reserve;
(dn_tmp_72 < dn_tmp_75); })){
{ dn_r_TransportCheck dn_tmp_80=({ uint32_t dn_tmp_76=v_properties;
uint32_t dn_tmp_77=((uint32_t)(0ULL));
uint32_t dn_tmp_78=((uint32_t)(0ULL));
int32_t dn_tmp_79=((int32_t)(7ULL));
((dn_r_TransportCheck){dn_tmp_76,dn_tmp_77,dn_tmp_78,dn_tmp_79}); });return dn_tmp_80; }
}
else{
}
}
else{
}
{ dn_r_TransportCheck dn_tmp_85=({ uint32_t dn_tmp_81=v_properties;
uint32_t dn_tmp_82=((uint32_t)(0ULL));
uint32_t dn_tmp_83=((uint32_t)(0ULL));
int32_t dn_tmp_84=((int32_t)(0ULL));
((dn_r_TransportCheck){dn_tmp_81,dn_tmp_82,dn_tmp_83,dn_tmp_84}); });return dn_tmp_85; }
}

dn_fail("missing native return");
}
static dn_r_TransportCheck dn_cb_transport_check(uint32_t v_profile,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds) {
 if(!dn_enter()){return (dn_r_TransportCheck){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TransportCheck){0};}
 dn_r_TransportCheck dn_result=dn_f_transport_check(v_profile,v_requirements,v_bounds);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_transport_preference_misses(uint32_t v_properties,uint32_t v_preferred) {
{
uint32_t v_bits=({ uint32_t dn_tmp_0=v_preferred;
uint32_t dn_tmp_1=({ uint32_t dn_tmp_2=({ uint32_t dn_tmp_3=((uint32_t)(511ULL));
if((unsigned __int128)dn_tmp_3>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_3; });
uint32_t dn_tmp_4=v_properties;
dn_U32_bits((uint64_t)dn_tmp_2 ^ (uint64_t)dn_tmp_4); });
dn_U32_bits((uint64_t)dn_tmp_0 & (uint64_t)dn_tmp_1); });
uint32_t v_count=((uint32_t)(0ULL));
{ size_t dn_tmp_6=((size_t)(0ULL));size_t dn_tmp_5=((size_t)(9ULL));for(size_t v_index=dn_tmp_6;v_index<dn_tmp_5;v_index++){
if(({ uint32_t dn_tmp_7=({ uint32_t dn_tmp_8=v_bits;
uint32_t dn_tmp_9=({ uint32_t dn_tmp_10=({ uint32_t dn_tmp_11=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_11; });
uint32_t dn_tmp_12=({ size_t dn_tmp_13=v_index;
if((unsigned __int128)dn_tmp_13>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_13; });
dn_U32_shl(dn_tmp_10,dn_tmp_12); });
dn_U32_bits((uint64_t)dn_tmp_8 & (uint64_t)dn_tmp_9); });
uint32_t dn_tmp_14=((uint32_t)(0ULL));
(dn_tmp_7 != dn_tmp_14); })){
{ uint32_t dn_tmp_18=({ uint32_t dn_tmp_15=v_count;
uint32_t dn_tmp_16=((uint32_t)(1ULL));
dn_U32_add(dn_tmp_15,dn_tmp_16); });uint32_t *dn_tmp_17=&(v_count);*dn_tmp_17=dn_tmp_18; }
}
else{
}
}
 }
{ uint32_t dn_tmp_19=v_count;return dn_tmp_19; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_transport_preference_misses(uint32_t v_properties,uint32_t v_preferred) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_transport_preference_misses(v_properties,v_preferred);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TransportChoice dn_f_transport_select(dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds,dn_t_arr6_r18_TransportCandidate v_candidates) {
{
if(((!(({ dn_r_TransportRequirements dn_tmp_0=v_requirements;
dn_f_transport_requirements_valid(dn_tmp_0); }))) || (!(({ dn_r_TransportBounds dn_tmp_1=v_bounds;
dn_f_transport_bounds_valid(dn_tmp_1); }))))){
{ dn_r_TransportChoice dn_tmp_9=({ uint32_t dn_tmp_2=((uint32_t)(0ULL));
uint32_t dn_tmp_3=((uint32_t)(0ULL));
uint32_t dn_tmp_4=((uint32_t)(0ULL));
uint32_t dn_tmp_5=((uint32_t)(0ULL));
uint32_t dn_tmp_6=((uint32_t)(0ULL));
uint32_t dn_tmp_7=((uint32_t)(0ULL));
int32_t dn_tmp_8=((int32_t)(4ULL));
((dn_r_TransportChoice){dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8}); });return dn_tmp_9; }
}
else{
}
dn_r_TransportChoice v_best=({ uint32_t dn_tmp_10=((uint32_t)(0ULL));
uint32_t dn_tmp_11=((uint32_t)(0ULL));
uint32_t dn_tmp_12=((uint32_t)(0ULL));
uint32_t dn_tmp_13=((uint32_t)(0ULL));
uint32_t dn_tmp_14=((uint32_t)(0ULL));
uint32_t dn_tmp_15=((uint32_t)(0ULL));
int32_t dn_tmp_16=((int32_t)(6ULL));
((dn_r_TransportChoice){dn_tmp_10,dn_tmp_11,dn_tmp_12,dn_tmp_13,dn_tmp_14,dn_tmp_15,dn_tmp_16}); });
uint32_t v_missing=((uint32_t)(0ULL));
uint32_t v_forbidden=((uint32_t)(0ULL));
{ size_t dn_tmp_18=((size_t)(0ULL));size_t dn_tmp_17=((size_t)(6ULL));for(size_t v_index=dn_tmp_18;v_index<dn_tmp_17;v_index++){
dn_r_TransportCandidate v_candidate=(*({ dn_t_arr6_r18_TransportCandidate *dn_tmp_19=&(v_candidates);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_index(dn_tmp_20,6)]; }));
if((v_candidate).f_available){
dn_r_TransportCheck v_checked=({ uint32_t dn_tmp_21=(v_candidate).f_profile;
dn_r_TransportRequirements dn_tmp_22=v_requirements;
dn_r_TransportBounds dn_tmp_23=v_bounds;
dn_f_transport_check(dn_tmp_21,dn_tmp_22,dn_tmp_23); });
{ uint32_t dn_tmp_27=({ uint32_t dn_tmp_24=v_missing;
uint32_t dn_tmp_25=(v_checked).f_missing;
dn_U32_bits((uint64_t)dn_tmp_24 | (uint64_t)dn_tmp_25); });uint32_t *dn_tmp_26=&(v_missing);*dn_tmp_26=dn_tmp_27; }
{ uint32_t dn_tmp_31=({ uint32_t dn_tmp_28=v_forbidden;
uint32_t dn_tmp_29=(v_checked).f_forbidden;
dn_U32_bits((uint64_t)dn_tmp_28 | (uint64_t)dn_tmp_29); });uint32_t *dn_tmp_30=&(v_forbidden);*dn_tmp_30=dn_tmp_31; }
if(({ int32_t dn_tmp_32=(v_checked).f_status;
int32_t dn_tmp_33=((int32_t)(0ULL));
(dn_tmp_32 == dn_tmp_33); })){
uint32_t v_misses=({ uint32_t dn_tmp_34=(v_checked).f_properties;
uint32_t dn_tmp_35=(v_requirements).f_preferred;
dn_f_transport_preference_misses(dn_tmp_34,dn_tmp_35); });
if((((({ uint32_t dn_tmp_36=(v_best).f_profile;
uint32_t dn_tmp_37=((uint32_t)(0ULL));
(dn_tmp_36 == dn_tmp_37); }) || ({ uint32_t dn_tmp_38=v_misses;
uint32_t dn_tmp_39=(v_best).f_preference_misses;
(dn_tmp_38 < dn_tmp_39); })) || (({ uint32_t dn_tmp_40=v_misses;
uint32_t dn_tmp_41=(v_best).f_preference_misses;
(dn_tmp_40 == dn_tmp_41); }) && ({ uint32_t dn_tmp_42=(v_candidate).f_cost;
uint32_t dn_tmp_43=(v_best).f_cost;
(dn_tmp_42 < dn_tmp_43); }))) || ((({ uint32_t dn_tmp_44=v_misses;
uint32_t dn_tmp_45=(v_best).f_preference_misses;
(dn_tmp_44 == dn_tmp_45); }) && ({ uint32_t dn_tmp_46=(v_candidate).f_cost;
uint32_t dn_tmp_47=(v_best).f_cost;
(dn_tmp_46 == dn_tmp_47); })) && ({ uint32_t dn_tmp_48=(v_candidate).f_profile;
uint32_t dn_tmp_49=(v_best).f_profile;
(dn_tmp_48 < dn_tmp_49); })))){
{ dn_r_TransportChoice dn_tmp_58=({ uint32_t dn_tmp_50=(v_candidate).f_profile;
uint32_t dn_tmp_51=(v_checked).f_properties;
uint32_t dn_tmp_52=((uint32_t)(0ULL));
uint32_t dn_tmp_53=((uint32_t)(0ULL));
uint32_t dn_tmp_54=v_misses;
uint32_t dn_tmp_55=(v_candidate).f_cost;
int32_t dn_tmp_56=((int32_t)(0ULL));
((dn_r_TransportChoice){dn_tmp_50,dn_tmp_51,dn_tmp_52,dn_tmp_53,dn_tmp_54,dn_tmp_55,dn_tmp_56}); });dn_r_TransportChoice *dn_tmp_57=&(v_best);*dn_tmp_57=dn_tmp_58; }
}
else{
}
}
else{
}
}
else{
}
}
 }
if(({ uint32_t dn_tmp_59=(v_best).f_profile;
uint32_t dn_tmp_60=((uint32_t)(0ULL));
(dn_tmp_59 == dn_tmp_60); })){
{ uint32_t dn_tmp_62=v_missing;uint32_t *dn_tmp_61=&((v_best).f_missing);*dn_tmp_61=dn_tmp_62; }
{ uint32_t dn_tmp_64=v_forbidden;uint32_t *dn_tmp_63=&((v_best).f_forbidden);*dn_tmp_63=dn_tmp_64; }
}
else{
}
{ dn_r_TransportChoice dn_tmp_65=v_best;return dn_tmp_65; }
}

dn_fail("missing native return");
}
static dn_r_TransportChoice dn_cb_transport_select(dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds,dn_t_arr6_r18_TransportCandidate v_candidates) {
 if(!dn_enter()){return (dn_r_TransportChoice){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TransportChoice){0};}
 dn_r_TransportChoice dn_result=dn_f_transport_select(v_requirements,v_bounds,v_candidates);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_arr6_r18_TransportCandidate dn_f_transport_candidates(uint32_t v_available) {
{
dn_t_arr6_r18_TransportCandidate v_candidates=({ ((dn_t_arr6_r18_TransportCandidate){0}); });
{ dn_r_TransportCandidate dn_tmp_10=({ uint32_t dn_tmp_2=((uint32_t)(1ULL));
bool dn_tmp_3=({ uint32_t dn_tmp_4=({ uint32_t dn_tmp_5=v_available;
uint32_t dn_tmp_6=((uint32_t)(1ULL));
dn_U32_bits((uint64_t)dn_tmp_5 & (uint64_t)dn_tmp_6); });
uint32_t dn_tmp_7=((uint32_t)(0ULL));
(dn_tmp_4 != dn_tmp_7); });
uint32_t dn_tmp_8=((uint32_t)(10ULL));
((dn_r_TransportCandidate){dn_tmp_2,dn_tmp_3,dn_tmp_8}); });dn_r_TransportCandidate *dn_tmp_9=&((*({ dn_t_arr6_r18_TransportCandidate *dn_tmp_0=&(v_candidates);size_t dn_tmp_1=((size_t)(0ULL));&dn_tmp_0->data[dn_index(dn_tmp_1,6)]; })));*dn_tmp_9=dn_tmp_10; }
{ dn_r_TransportCandidate dn_tmp_21=({ uint32_t dn_tmp_13=((uint32_t)(2ULL));
bool dn_tmp_14=({ uint32_t dn_tmp_15=({ uint32_t dn_tmp_16=v_available;
uint32_t dn_tmp_17=((uint32_t)(2ULL));
dn_U32_bits((uint64_t)dn_tmp_16 & (uint64_t)dn_tmp_17); });
uint32_t dn_tmp_18=((uint32_t)(0ULL));
(dn_tmp_15 != dn_tmp_18); });
uint32_t dn_tmp_19=((uint32_t)(30ULL));
((dn_r_TransportCandidate){dn_tmp_13,dn_tmp_14,dn_tmp_19}); });dn_r_TransportCandidate *dn_tmp_20=&((*({ dn_t_arr6_r18_TransportCandidate *dn_tmp_11=&(v_candidates);size_t dn_tmp_12=((size_t)(1ULL));&dn_tmp_11->data[dn_index(dn_tmp_12,6)]; })));*dn_tmp_20=dn_tmp_21; }
{ dn_r_TransportCandidate dn_tmp_32=({ uint32_t dn_tmp_24=((uint32_t)(3ULL));
bool dn_tmp_25=({ uint32_t dn_tmp_26=({ uint32_t dn_tmp_27=v_available;
uint32_t dn_tmp_28=((uint32_t)(4ULL));
dn_U32_bits((uint64_t)dn_tmp_27 & (uint64_t)dn_tmp_28); });
uint32_t dn_tmp_29=((uint32_t)(0ULL));
(dn_tmp_26 != dn_tmp_29); });
uint32_t dn_tmp_30=((uint32_t)(1ULL));
((dn_r_TransportCandidate){dn_tmp_24,dn_tmp_25,dn_tmp_30}); });dn_r_TransportCandidate *dn_tmp_31=&((*({ dn_t_arr6_r18_TransportCandidate *dn_tmp_22=&(v_candidates);size_t dn_tmp_23=((size_t)(2ULL));&dn_tmp_22->data[dn_index(dn_tmp_23,6)]; })));*dn_tmp_31=dn_tmp_32; }
{ dn_r_TransportCandidate dn_tmp_43=({ uint32_t dn_tmp_35=((uint32_t)(4ULL));
bool dn_tmp_36=({ uint32_t dn_tmp_37=({ uint32_t dn_tmp_38=v_available;
uint32_t dn_tmp_39=((uint32_t)(8ULL));
dn_U32_bits((uint64_t)dn_tmp_38 & (uint64_t)dn_tmp_39); });
uint32_t dn_tmp_40=((uint32_t)(0ULL));
(dn_tmp_37 != dn_tmp_40); });
uint32_t dn_tmp_41=((uint32_t)(20ULL));
((dn_r_TransportCandidate){dn_tmp_35,dn_tmp_36,dn_tmp_41}); });dn_r_TransportCandidate *dn_tmp_42=&((*({ dn_t_arr6_r18_TransportCandidate *dn_tmp_33=&(v_candidates);size_t dn_tmp_34=((size_t)(3ULL));&dn_tmp_33->data[dn_index(dn_tmp_34,6)]; })));*dn_tmp_42=dn_tmp_43; }
{ dn_r_TransportCandidate dn_tmp_54=({ uint32_t dn_tmp_46=((uint32_t)(5ULL));
bool dn_tmp_47=({ uint32_t dn_tmp_48=({ uint32_t dn_tmp_49=v_available;
uint32_t dn_tmp_50=((uint32_t)(16ULL));
dn_U32_bits((uint64_t)dn_tmp_49 & (uint64_t)dn_tmp_50); });
uint32_t dn_tmp_51=((uint32_t)(0ULL));
(dn_tmp_48 != dn_tmp_51); });
uint32_t dn_tmp_52=((uint32_t)(20ULL));
((dn_r_TransportCandidate){dn_tmp_46,dn_tmp_47,dn_tmp_52}); });dn_r_TransportCandidate *dn_tmp_53=&((*({ dn_t_arr6_r18_TransportCandidate *dn_tmp_44=&(v_candidates);size_t dn_tmp_45=((size_t)(4ULL));&dn_tmp_44->data[dn_index(dn_tmp_45,6)]; })));*dn_tmp_53=dn_tmp_54; }
{ dn_r_TransportCandidate dn_tmp_65=({ uint32_t dn_tmp_57=((uint32_t)(6ULL));
bool dn_tmp_58=({ uint32_t dn_tmp_59=({ uint32_t dn_tmp_60=v_available;
uint32_t dn_tmp_61=((uint32_t)(32ULL));
dn_U32_bits((uint64_t)dn_tmp_60 & (uint64_t)dn_tmp_61); });
uint32_t dn_tmp_62=((uint32_t)(0ULL));
(dn_tmp_59 != dn_tmp_62); });
uint32_t dn_tmp_63=((uint32_t)(5ULL));
((dn_r_TransportCandidate){dn_tmp_57,dn_tmp_58,dn_tmp_63}); });dn_r_TransportCandidate *dn_tmp_64=&((*({ dn_t_arr6_r18_TransportCandidate *dn_tmp_55=&(v_candidates);size_t dn_tmp_56=((size_t)(5ULL));&dn_tmp_55->data[dn_index(dn_tmp_56,6)]; })));*dn_tmp_64=dn_tmp_65; }
{ dn_t_arr6_r18_TransportCandidate dn_tmp_66=v_candidates;return dn_tmp_66; }
}

dn_fail("missing native return");
}
static dn_t_arr6_r18_TransportCandidate dn_cb_transport_candidates(uint32_t v_available) {
 if(!dn_enter()){return (dn_t_arr6_r18_TransportCandidate){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_arr6_r18_TransportCandidate){0};}
 dn_t_arr6_r18_TransportCandidate dn_result=dn_f_transport_candidates(v_available);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
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
static uint8_t dn_f_quic_dispose(dn_r_NetOpened * v_state) {
{
{
(void)(({ void * dn_tmp_0=(v_state)->f_handle;
({ dn_ext_dlm_net_quic_close(dn_tmp_0);dn_fp_default();0; }); }));
}
}

return 0;
}
static void dn_cb_quic_dispose(dn_r_NetOpened * v_state) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_quic_dispose(v_state);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static void * dn_f_quic_handle(dn_r_QuicEndpoint const * v_endpoint) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_quic_dispose * const * dn_tmp_0=(&((v_endpoint)->f_inner));
(&((*dn_tmp_0)->value)); });
{ void * dn_tmp_1=(v_state)->f_handle;return dn_tmp_1; }
}

dn_fail("missing native return");
}
static void * dn_cb_quic_handle(dn_r_QuicEndpoint const * v_endpoint) {
 if(!dn_enter()){return (void *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (void *){0};}
 void * dn_result=dn_f_quic_handle(v_endpoint);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_QuicConfig dn_f_quic_config(void) {
{
{ dn_r_QuicConfig dn_tmp_8=({ uint32_t dn_tmp_0=((uint32_t)(16ULL));
uint32_t dn_tmp_1=((uint32_t)(64ULL));
uint32_t dn_tmp_2=((uint32_t)(1024ULL));
uint32_t dn_tmp_3=((uint32_t)(30000ULL));
uint32_t dn_tmp_4=((uint32_t)(10000ULL));
uint32_t dn_tmp_5=((uint32_t)(2ULL));
size_t dn_tmp_6=((size_t)(16777216ULL));
size_t dn_tmp_7=((size_t)(65536ULL));
((dn_r_QuicConfig){dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7}); });return dn_tmp_8; }
}

dn_fail("missing native return");
}
static dn_r_QuicConfig dn_cb_quic_config(void) {
 if(!dn_enter()){return (dn_r_QuicConfig){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_QuicConfig){0};}
 dn_r_QuicConfig dn_result=dn_f_quic_config();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_quic_available(void) {
{
{
{ bool dn_tmp_2=({ uint32_t dn_tmp_0=({ ({ uint32_t dn_foreign=dn_ext_dlm_net_quic_version();dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_1=((uint32_t)(0ULL));
(dn_tmp_0 != dn_tmp_1); });return dn_tmp_2; }
}
}

dn_fail("missing native return");
}
static bool dn_cb_quic_available(void) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_quic_available();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_QuicEndpoint dn_f_quic_client(dn_r_SocketAddress v_address,dn_utf8 v_alpn,dn_utf8 v_ca_file,dn_r_QuicConfig v_config) {
{
{
{ dn_r_QuicEndpoint dn_tmp_11=({ dn_t_resource_r9_NetOpened_quic_dispose * dn_tmp_0=({ dn_t_resource_r9_NetOpened_quic_dispose * dn_tmp_1=dn_begin_resource_r9_NetOpened_quic_dispose();dn_commit_resource_r9_NetOpened_quic_dispose(dn_tmp_1,({ dn_r_NetAddress dn_tmp_2=(v_address).f_value;
bool dn_tmp_3=0;
dn_r_NetText dn_tmp_4=({ dn_utf8 dn_tmp_5=v_alpn;
dn_f_net_text(dn_tmp_5); });
dn_r_NetText dn_tmp_6=({ dn_utf8 dn_tmp_7=v_ca_file;
dn_f_net_text(dn_tmp_7); });
dn_r_NetText dn_tmp_8=({ dn_utf8 dn_tmp_9=((dn_utf8){(const uint8_t*)"", 0});
dn_f_net_text(dn_tmp_9); });
dn_r_QuicConfig dn_tmp_10=v_config;
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_quic_open(dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_6,dn_tmp_8,dn_tmp_10);dn_fp_default();dn_foreign; }); })); });
dn_adopt_resource_r9_NetOpened_quic_dispose(&dn_tmp_0);
((dn_r_QuicEndpoint){dn_tmp_0}); });dn_adopt_r12_QuicEndpoint(&dn_tmp_11);return dn_tmp_11; }
}
}

dn_fail("missing native return");
}
static dn_r_QuicEndpoint dn_cb_quic_client(dn_r_SocketAddress v_address,dn_utf8 v_alpn,dn_utf8 v_ca_file,dn_r_QuicConfig v_config) {
 if(!dn_enter()){return (dn_r_QuicEndpoint){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_QuicEndpoint){0};}
 dn_r_QuicEndpoint dn_result=dn_f_quic_client(v_address,v_alpn,v_ca_file,v_config);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_QuicEndpoint dn_f_quic_server(dn_r_SocketAddress v_address,dn_utf8 v_alpn,dn_utf8 v_certificate,dn_utf8 v_key,dn_r_QuicConfig v_config) {
{
{
{ dn_r_QuicEndpoint dn_tmp_11=({ dn_t_resource_r9_NetOpened_quic_dispose * dn_tmp_0=({ dn_t_resource_r9_NetOpened_quic_dispose * dn_tmp_1=dn_begin_resource_r9_NetOpened_quic_dispose();dn_commit_resource_r9_NetOpened_quic_dispose(dn_tmp_1,({ dn_r_NetAddress dn_tmp_2=(v_address).f_value;
bool dn_tmp_3=1;
dn_r_NetText dn_tmp_4=({ dn_utf8 dn_tmp_5=v_alpn;
dn_f_net_text(dn_tmp_5); });
dn_r_NetText dn_tmp_6=({ dn_utf8 dn_tmp_7=v_certificate;
dn_f_net_text(dn_tmp_7); });
dn_r_NetText dn_tmp_8=({ dn_utf8 dn_tmp_9=v_key;
dn_f_net_text(dn_tmp_9); });
dn_r_QuicConfig dn_tmp_10=v_config;
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_quic_open(dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_6,dn_tmp_8,dn_tmp_10);dn_fp_default();dn_foreign; }); })); });
dn_adopt_resource_r9_NetOpened_quic_dispose(&dn_tmp_0);
((dn_r_QuicEndpoint){dn_tmp_0}); });dn_adopt_r12_QuicEndpoint(&dn_tmp_11);return dn_tmp_11; }
}
}

dn_fail("missing native return");
}
static dn_r_QuicEndpoint dn_cb_quic_server(dn_r_SocketAddress v_address,dn_utf8 v_alpn,dn_utf8 v_certificate,dn_utf8 v_key,dn_r_QuicConfig v_config) {
 if(!dn_enter()){return (dn_r_QuicEndpoint){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_QuicEndpoint){0};}
 dn_r_QuicEndpoint dn_result=dn_f_quic_server(v_address,v_alpn,v_certificate,v_key,v_config);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkError dn_f_quic_error(dn_r_QuicEndpoint const * v_endpoint) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_quic_dispose * const * dn_tmp_0=(&((v_endpoint)->f_inner));
(&((*dn_tmp_0)->value)); });
{ dn_r_NetworkError dn_tmp_4=({ int32_t dn_tmp_1=(v_state)->f_status;
int32_t dn_tmp_2=(v_state)->f_system;
uint32_t dn_tmp_3=((uint32_t)(70ULL));
((dn_r_NetworkError){dn_tmp_1,dn_tmp_2,dn_tmp_3}); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_NetworkError dn_cb_quic_error(dn_r_QuicEndpoint const * v_endpoint) {
 if(!dn_enter()){return (dn_r_NetworkError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkError){0};}
 dn_r_NetworkError dn_result=dn_f_quic_error(v_endpoint);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_quic_close(dn_r_QuicEndpoint v_endpoint) {
dn_r_QuicEndpoint volatile dn_param_endpoint=v_endpoint;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r12_QuicEndpoint(&dn_param_endpoint);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
(void)(({ dn_t_resource_r9_NetOpened_quic_dispose * dn_tmp_2=({ dn_t_resource_r9_NetOpened_quic_dispose * dn_tmp_3=(dn_param_endpoint).f_inner;(dn_param_endpoint).f_inner=(dn_t_resource_r9_NetOpened_quic_dispose *){0};dn_adopt_resource_r9_NetOpened_quic_dispose(&dn_tmp_3);dn_tmp_3; });
dn_adopt_resource_r9_NetOpened_quic_dispose(&dn_tmp_2);
({ dn_drop_resource_r9_NetOpened_quic_dispose(&dn_tmp_2);0; }); }));
}

dn_drop_r12_QuicEndpoint(&dn_param_endpoint);
dn_boundary_end(&dn_tmp_0,0);
return 0;
}
static void dn_cb_quic_close(dn_r_QuicEndpoint v_endpoint) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_quic_close(v_endpoint);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static dn_r_SocketAddressResult dn_f_quic_local(dn_r_QuicEndpoint const * v_endpoint) {
{
{
{ dn_r_SocketAddressResult dn_tmp_4=({ dn_r_NetResolved dn_tmp_0=({ void * dn_tmp_1=({ dn_r_QuicEndpoint const * dn_tmp_2=v_endpoint;
dn_f_quic_handle(dn_tmp_2); });
({ dn_r_NetResolved dn_foreign=dn_ext_dlm_net_quic_local(dn_tmp_1);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_3=((uint32_t)(71ULL));
dn_f_socket_address_result(dn_tmp_0,dn_tmp_3); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_SocketAddressResult dn_cb_quic_local(dn_r_QuicEndpoint const * v_endpoint) {
 if(!dn_enter()){return (dn_r_SocketAddressResult){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_SocketAddressResult){0};}
 dn_r_SocketAddressResult dn_result=dn_f_quic_local(v_endpoint);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_QuicConnectionResult dn_f_quic_connect(dn_r_QuicEndpoint * v_endpoint,dn_r_SocketAddress v_address,dn_utf8 v_hostname) {
{
{
dn_r_QuicNativeReference v_r=({ void * dn_tmp_0=({ dn_r_QuicEndpoint * dn_tmp_1=v_endpoint;
dn_f_quic_handle(dn_tmp_1); });
dn_r_NetAddress dn_tmp_2=(v_address).f_value;
dn_r_NetText dn_tmp_3=({ dn_utf8 dn_tmp_4=v_hostname;
dn_f_net_text(dn_tmp_4); });
({ dn_r_QuicNativeReference dn_foreign=dn_ext_dlm_net_quic_connect(dn_tmp_0,dn_tmp_2,dn_tmp_3);dn_fp_default();dn_foreign; }); });
{ dn_r_QuicConnectionResult dn_tmp_11=({ dn_r_QuicConnection dn_tmp_5=({ uint64_t dn_tmp_6=(v_r).f_connection;
((dn_r_QuicConnection){dn_tmp_6}); });
dn_r_NetworkError dn_tmp_7=({ int32_t dn_tmp_8=(v_r).f_status;
int32_t dn_tmp_9=(v_r).f_system;
uint32_t dn_tmp_10=((uint32_t)(72ULL));
((dn_r_NetworkError){dn_tmp_8,dn_tmp_9,dn_tmp_10}); });
((dn_r_QuicConnectionResult){dn_tmp_5,dn_tmp_7}); });return dn_tmp_11; }
}
}

dn_fail("missing native return");
}
static dn_r_QuicConnectionResult dn_cb_quic_connect(dn_r_QuicEndpoint * v_endpoint,dn_r_SocketAddress v_address,dn_utf8 v_hostname) {
 if(!dn_enter()){return (dn_r_QuicConnectionResult){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_QuicConnectionResult){0};}
 dn_r_QuicConnectionResult dn_result=dn_f_quic_connect(v_endpoint,v_address,v_hostname);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_quic_poll(dn_r_QuicEndpoint * v_endpoint,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
{
{ dn_r_NetworkTransfer dn_tmp_7=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_QuicEndpoint * dn_tmp_2=v_endpoint;
dn_f_quic_handle(dn_tmp_2); });
uint32_t dn_tmp_3=v_timeout_ms;
void * dn_tmp_4=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_net_cancel_dispose * const * dn_tmp_5=v_cancel;
(&((*dn_tmp_5)->value)); })))));
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_quic_poll(dn_tmp_1,dn_tmp_3,dn_tmp_4);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_6=((uint32_t)(73ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_6); });return dn_tmp_7; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_quic_poll(dn_r_QuicEndpoint * v_endpoint,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_quic_poll(v_endpoint,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_QuicEvent dn_f_quic_next(dn_r_QuicEndpoint const * v_endpoint) {
{
{
dn_r_QuicNativeEvent v_r=({ void * dn_tmp_0=({ dn_r_QuicEndpoint const * dn_tmp_1=v_endpoint;
dn_f_quic_handle(dn_tmp_1); });
({ dn_r_QuicNativeEvent dn_foreign=dn_ext_dlm_net_quic_next(dn_tmp_0);dn_fp_default();dn_foreign; }); });
{ dn_r_QuicEvent dn_tmp_16=({ uint64_t dn_tmp_2=(v_r).f_id;
dn_r_QuicStream dn_tmp_3=({ dn_r_QuicConnection dn_tmp_4=({ uint64_t dn_tmp_5=(v_r).f_connection;
((dn_r_QuicConnection){dn_tmp_5}); });
int64_t dn_tmp_6=(v_r).f_stream;
((dn_r_QuicStream){dn_tmp_4,dn_tmp_6}); });
uint64_t dn_tmp_7=(v_r).f_offset;
uint64_t dn_tmp_8=(v_r).f_code;
size_t dn_tmp_9=(v_r).f_count;
uint32_t dn_tmp_10=(v_r).f_kind;
uint32_t dn_tmp_11=(v_r).f_flags;
dn_r_NetworkError dn_tmp_12=({ int32_t dn_tmp_13=(v_r).f_status;
int32_t dn_tmp_14=(v_r).f_system;
uint32_t dn_tmp_15=((uint32_t)(74ULL));
((dn_r_NetworkError){dn_tmp_13,dn_tmp_14,dn_tmp_15}); });
((dn_r_QuicEvent){dn_tmp_2,dn_tmp_3,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_11,dn_tmp_12}); });return dn_tmp_16; }
}
}

dn_fail("missing native return");
}
static dn_r_QuicEvent dn_cb_quic_next(dn_r_QuicEndpoint const * v_endpoint) {
 if(!dn_enter()){return (dn_r_QuicEvent){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_QuicEvent){0};}
 dn_r_QuicEvent dn_result=dn_f_quic_next(v_endpoint);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_quic_copy(dn_r_QuicEndpoint const * v_endpoint,uint64_t v_event,size_t v_offset,dn_t_buf_U8 * v_output) {
{
{
{ dn_r_NetworkTransfer dn_tmp_10=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_QuicEndpoint const * dn_tmp_2=v_endpoint;
dn_f_quic_handle(dn_tmp_2); });
uint64_t dn_tmp_3=v_event;
size_t dn_tmp_4=v_offset;
uint8_t * dn_tmp_5=({ dn_t_buf_U8 * dn_tmp_6=v_output;
dn_f_net_output(dn_tmp_6); });
size_t dn_tmp_7=({ dn_t_buf_U8 * dn_tmp_8=v_output;
(*(dn_tmp_8)).len; });
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_quic_copy(dn_tmp_1,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_7);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_9=((uint32_t)(75ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_9); });return dn_tmp_10; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_quic_copy(dn_r_QuicEndpoint const * v_endpoint,uint64_t v_event,size_t v_offset,dn_t_buf_U8 * v_output) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_quic_copy(v_endpoint,v_event,v_offset,v_output);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_quic_ack(dn_r_QuicEndpoint * v_endpoint,uint64_t v_event) {
{
{
{ dn_r_NetworkTransfer dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_QuicEndpoint * dn_tmp_2=v_endpoint;
dn_f_quic_handle(dn_tmp_2); });
uint64_t dn_tmp_3=v_event;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_quic_ack(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_4=((uint32_t)(76ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_4); });return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_quic_ack(dn_r_QuicEndpoint * v_endpoint,uint64_t v_event) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_quic_ack(v_endpoint,v_event);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_QuicStreamResult dn_f_quic_open_stream(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection,bool v_unidirectional) {
{
{
dn_r_QuicNativeReference v_r=({ void * dn_tmp_0=({ dn_r_QuicEndpoint * dn_tmp_1=v_endpoint;
dn_f_quic_handle(dn_tmp_1); });
uint64_t dn_tmp_2=(v_connection).f_id;
bool dn_tmp_3=v_unidirectional;
({ dn_r_QuicNativeReference dn_foreign=dn_ext_dlm_net_quic_stream(dn_tmp_0,dn_tmp_2,dn_tmp_3);dn_fp_default();dn_foreign; }); });
{ dn_r_QuicStreamResult dn_tmp_12=({ dn_r_QuicStream dn_tmp_4=({ dn_r_QuicConnection dn_tmp_5=({ uint64_t dn_tmp_6=(v_r).f_connection;
((dn_r_QuicConnection){dn_tmp_6}); });
int64_t dn_tmp_7=(v_r).f_stream;
((dn_r_QuicStream){dn_tmp_5,dn_tmp_7}); });
dn_r_NetworkError dn_tmp_8=({ int32_t dn_tmp_9=(v_r).f_status;
int32_t dn_tmp_10=(v_r).f_system;
uint32_t dn_tmp_11=((uint32_t)(77ULL));
((dn_r_NetworkError){dn_tmp_9,dn_tmp_10,dn_tmp_11}); });
((dn_r_QuicStreamResult){dn_tmp_4,dn_tmp_8}); });return dn_tmp_12; }
}
}

dn_fail("missing native return");
}
static dn_r_QuicStreamResult dn_cb_quic_open_stream(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection,bool v_unidirectional) {
 if(!dn_enter()){return (dn_r_QuicStreamResult){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_QuicStreamResult){0};}
 dn_r_QuicStreamResult dn_result=dn_f_quic_open_stream(v_endpoint,v_connection,v_unidirectional);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_quic_write(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicStream v_stream,dn_t_slice0_U8 v_bytes,bool v_fin) {
{
{
dn_r_NetText v_data=({ dn_t_slice0_U8 dn_tmp_0=v_bytes;
dn_f_net_bytes(dn_tmp_0); });
{ dn_r_NetworkTransfer dn_tmp_10=({ dn_r_NetIo dn_tmp_1=({ void * dn_tmp_2=({ dn_r_QuicEndpoint * dn_tmp_3=v_endpoint;
dn_f_quic_handle(dn_tmp_3); });
uint64_t dn_tmp_4=((v_stream).f_connection).f_id;
int64_t dn_tmp_5=(v_stream).f_id;
uint8_t * dn_tmp_6=(v_data).f_data;
size_t dn_tmp_7=(v_data).f_count;
bool dn_tmp_8=v_fin;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_quic_write(dn_tmp_2,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_9=((uint32_t)(78ULL));
dn_f_socket_transfer(dn_tmp_1,dn_tmp_9); });return dn_tmp_10; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_quic_write(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicStream v_stream,dn_t_slice0_U8 v_bytes,bool v_fin) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_quic_write(v_endpoint,v_stream,v_bytes,v_fin);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_quic_datagram(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection,dn_t_slice0_U8 v_bytes) {
{
{
dn_r_NetText v_data=({ dn_t_slice0_U8 dn_tmp_0=v_bytes;
dn_f_net_bytes(dn_tmp_0); });
{ dn_r_NetworkTransfer dn_tmp_8=({ dn_r_NetIo dn_tmp_1=({ void * dn_tmp_2=({ dn_r_QuicEndpoint * dn_tmp_3=v_endpoint;
dn_f_quic_handle(dn_tmp_3); });
uint64_t dn_tmp_4=(v_connection).f_id;
uint8_t * dn_tmp_5=(v_data).f_data;
size_t dn_tmp_6=(v_data).f_count;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_quic_datagram(dn_tmp_2,dn_tmp_4,dn_tmp_5,dn_tmp_6);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_7=((uint32_t)(79ULL));
dn_f_socket_transfer(dn_tmp_1,dn_tmp_7); });return dn_tmp_8; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_quic_datagram(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection,dn_t_slice0_U8 v_bytes) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_quic_datagram(v_endpoint,v_connection,v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_quic_reset(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicStream v_stream,uint64_t v_code) {
{
{
{ dn_r_NetworkTransfer dn_tmp_7=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_QuicEndpoint * dn_tmp_2=v_endpoint;
dn_f_quic_handle(dn_tmp_2); });
uint64_t dn_tmp_3=((v_stream).f_connection).f_id;
int64_t dn_tmp_4=(v_stream).f_id;
uint64_t dn_tmp_5=v_code;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_quic_reset(dn_tmp_1,dn_tmp_3,dn_tmp_4,dn_tmp_5);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_6=((uint32_t)(80ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_6); });return dn_tmp_7; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_quic_reset(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicStream v_stream,uint64_t v_code) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_quic_reset(v_endpoint,v_stream,v_code);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_quic_disconnect(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection,uint64_t v_code) {
{
{
{ dn_r_NetworkTransfer dn_tmp_6=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_QuicEndpoint * dn_tmp_2=v_endpoint;
dn_f_quic_handle(dn_tmp_2); });
uint64_t dn_tmp_3=(v_connection).f_id;
uint64_t dn_tmp_4=v_code;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_quic_disconnect(dn_tmp_1,dn_tmp_3,dn_tmp_4);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_5=((uint32_t)(81ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_5); });return dn_tmp_6; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_quic_disconnect(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection,uint64_t v_code) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_quic_disconnect(v_endpoint,v_connection,v_code);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_quic_forget(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection) {
{
{
{ dn_r_NetworkTransfer dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_QuicEndpoint * dn_tmp_2=v_endpoint;
dn_f_quic_handle(dn_tmp_2); });
uint64_t dn_tmp_3=(v_connection).f_id;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_quic_forget(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_4=((uint32_t)(82ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_4); });return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_quic_forget(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_quic_forget(v_endpoint,v_connection);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_quic_migrate(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection,dn_r_SocketAddress v_address) {
{
{
{ dn_r_NetworkTransfer dn_tmp_6=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_QuicEndpoint * dn_tmp_2=v_endpoint;
dn_f_quic_handle(dn_tmp_2); });
uint64_t dn_tmp_3=(v_connection).f_id;
dn_r_NetAddress dn_tmp_4=(v_address).f_value;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_quic_migrate(dn_tmp_1,dn_tmp_3,dn_tmp_4);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_5=((uint32_t)(83ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_5); });return dn_tmp_6; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_quic_migrate(dn_r_QuicEndpoint * v_endpoint,dn_r_QuicConnection v_connection,dn_r_SocketAddress v_address) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_quic_migrate(v_endpoint,v_connection,v_address);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_QuicStatistics dn_f_quic_statistics(dn_r_QuicEndpoint const * v_endpoint,dn_r_QuicConnection v_connection) {
{
{
dn_r_QuicNativeStatistics v_r=({ void * dn_tmp_0=({ dn_r_QuicEndpoint const * dn_tmp_1=v_endpoint;
dn_f_quic_handle(dn_tmp_1); });
uint64_t dn_tmp_2=(v_connection).f_id;
({ dn_r_QuicNativeStatistics dn_foreign=dn_ext_dlm_net_quic_stats(dn_tmp_0,dn_tmp_2);dn_fp_default();dn_foreign; }); });
{ dn_r_QuicStatistics dn_tmp_20=({ size_t dn_tmp_3=(v_r).f_used;
size_t dn_tmp_4=(v_r).f_limit;
uint64_t dn_tmp_5=(v_r).f_sent;
uint64_t dn_tmp_6=(v_r).f_received;
uint64_t dn_tmp_7=(v_r).f_lost;
uint64_t dn_tmp_8=(v_r).f_cwnd;
uint64_t dn_tmp_9=(v_r).f_in_flight;
uint64_t dn_tmp_10=(v_r).f_rtt_ns;
uint32_t dn_tmp_11=(v_r).f_connections;
uint32_t dn_tmp_12=(v_r).f_streams;
uint32_t dn_tmp_13=(v_r).f_events;
uint32_t dn_tmp_14=(v_r).f_state;
uint32_t dn_tmp_15=(v_r).f_version;
dn_r_NetworkError dn_tmp_16=({ int32_t dn_tmp_17=(v_r).f_status;
int32_t dn_tmp_18=(v_r).f_system;
uint32_t dn_tmp_19=((uint32_t)(84ULL));
((dn_r_NetworkError){dn_tmp_17,dn_tmp_18,dn_tmp_19}); });
((dn_r_QuicStatistics){dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_11,dn_tmp_12,dn_tmp_13,dn_tmp_14,dn_tmp_15,dn_tmp_16}); });return dn_tmp_20; }
}
}

dn_fail("missing native return");
}
static dn_r_QuicStatistics dn_cb_quic_statistics(dn_r_QuicEndpoint const * v_endpoint,dn_r_QuicConnection v_connection) {
 if(!dn_enter()){return (dn_r_QuicStatistics){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_QuicStatistics){0};}
 dn_r_QuicStatistics dn_result=dn_f_quic_statistics(v_endpoint,v_connection);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_quic_event_fin(dn_r_QuicEvent const * v_event) {
{
{ bool dn_tmp_6=(({ uint32_t dn_tmp_0=(v_event)->f_kind;
uint32_t dn_tmp_1=((uint32_t)(3ULL));
(dn_tmp_0 == dn_tmp_1); }) && ({ uint32_t dn_tmp_2=({ uint32_t dn_tmp_3=(v_event)->f_flags;
uint32_t dn_tmp_4=((uint32_t)(1ULL));
dn_U32_bits((uint64_t)dn_tmp_3 & (uint64_t)dn_tmp_4); });
uint32_t dn_tmp_5=((uint32_t)(0ULL));
(dn_tmp_2 != dn_tmp_5); }));return dn_tmp_6; }
}

dn_fail("missing native return");
}
static bool dn_cb_quic_event_fin(dn_r_QuicEvent const * v_event) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_quic_event_fin(v_event);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetIo dn_f_quic_provider_shutdown(void) {
{
{ dn_r_NetIo dn_tmp_0=({ ({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_quic_shutdown();dn_fp_default();dn_foreign; }); });return dn_tmp_0; }
}

dn_fail("missing native return");
}
static dn_r_NetIo dn_cb_quic_provider_shutdown(void) {
 if(!dn_enter()){return (dn_r_NetIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetIo){0};}
 dn_r_NetIo dn_result=dn_f_quic_provider_shutdown();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_transport_dispose(dn_r_NetOpened * v_state) {
{
{
(void)(({ void * dn_tmp_0=(v_state)->f_handle;
({ dn_ext_dlm_net_transport_close(dn_tmp_0);dn_fp_default();0; }); }));
}
}

return 0;
}
static void dn_cb_transport_dispose(dn_r_NetOpened * v_state) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_transport_dispose(v_state);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static void * dn_f_transport_handle(dn_r_TransportSession const * v_session) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_transport_dispose * const * dn_tmp_0=(&((v_session)->f_inner));
(&((*dn_tmp_0)->value)); });
{ void * dn_tmp_1=(v_state)->f_handle;return dn_tmp_1; }
}

dn_fail("missing native return");
}
static void * dn_cb_transport_handle(dn_r_TransportSession const * v_session) {
 if(!dn_enter()){return (void *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (void *){0};}
 void * dn_result=dn_f_transport_handle(v_session);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkError dn_f_transport_error(dn_r_TransportSession const * v_session) {
{
dn_r_NetOpened const * v_state=({ dn_t_resource_r9_NetOpened_transport_dispose * const * dn_tmp_0=(&((v_session)->f_inner));
(&((*dn_tmp_0)->value)); });
{ dn_r_NetworkError dn_tmp_4=({ int32_t dn_tmp_1=(v_state)->f_status;
int32_t dn_tmp_2=(v_state)->f_system;
uint32_t dn_tmp_3=((uint32_t)(90ULL));
((dn_r_NetworkError){dn_tmp_1,dn_tmp_2,dn_tmp_3}); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_NetworkError dn_cb_transport_error(dn_r_TransportSession const * v_session) {
 if(!dn_enter()){return (dn_r_NetworkError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkError){0};}
 dn_r_NetworkError dn_result=dn_f_transport_error(v_session);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_transport_close(dn_r_TransportSession v_session) {
dn_r_TransportSession volatile dn_param_session=v_session;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r16_TransportSession(&dn_param_session);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
(void)(({ dn_t_resource_r9_NetOpened_transport_dispose * dn_tmp_2=({ dn_t_resource_r9_NetOpened_transport_dispose * dn_tmp_3=(dn_param_session).f_inner;(dn_param_session).f_inner=(dn_t_resource_r9_NetOpened_transport_dispose *){0};dn_adopt_resource_r9_NetOpened_transport_dispose(&dn_tmp_3);dn_tmp_3; });
dn_adopt_resource_r9_NetOpened_transport_dispose(&dn_tmp_2);
({ dn_drop_resource_r9_NetOpened_transport_dispose(&dn_tmp_2);0; }); }));
}

dn_drop_r16_TransportSession(&dn_param_session);
dn_boundary_end(&dn_tmp_0,0);
return 0;
}
static void dn_cb_transport_close(dn_r_TransportSession v_session) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_transport_close(v_session);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint32_t dn_f_transport_available(void) {
{
{
{ uint32_t dn_tmp_0=({ ({ uint32_t dn_foreign=dn_ext_dlm_net_transport_available();dn_fp_default();dn_foreign; }); });return dn_tmp_0; }
}
}

dn_fail("missing native return");
}
static uint32_t dn_cb_transport_available(void) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_transport_available();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TransportTarget dn_f_transport_target(dn_r_SocketAddress v_peer,dn_utf8 v_identity,dn_utf8 v_ca_file,dn_utf8 v_pin) {
{
{ dn_r_TransportTarget dn_tmp_6=({ dn_r_SocketAddress dn_tmp_0=({ dn_r_NetAddress dn_tmp_1=({ ((dn_r_NetAddress){0}); });
((dn_r_SocketAddress){dn_tmp_1}); });
dn_r_SocketAddress dn_tmp_2=v_peer;
dn_utf8 dn_tmp_3=v_identity;
dn_utf8 dn_tmp_4=v_ca_file;
dn_utf8 dn_tmp_5=v_pin;
((dn_r_TransportTarget){dn_tmp_0,dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5}); });return dn_tmp_6; }
}

dn_fail("missing native return");
}
static dn_r_TransportTarget dn_cb_transport_target(dn_r_SocketAddress v_peer,dn_utf8 v_identity,dn_utf8 v_ca_file,dn_utf8 v_pin) {
 if(!dn_enter()){return (dn_r_TransportTarget){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TransportTarget){0};}
 dn_r_TransportTarget dn_result=dn_f_transport_target(v_peer,v_identity,v_ca_file,v_pin);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_utf8 dn_f_transport_alpn(uint32_t v_profile) {
{
if(({ uint32_t dn_tmp_0=v_profile;
uint32_t dn_tmp_1=((uint32_t)(4ULL));
(dn_tmp_0 == dn_tmp_1); })){
{ dn_utf8 dn_tmp_2=((dn_utf8){(const uint8_t*)"dlm-transport/1/ordered", 23});return dn_tmp_2; }
}
else{
}
if(({ uint32_t dn_tmp_3=v_profile;
uint32_t dn_tmp_4=((uint32_t)(5ULL));
(dn_tmp_3 == dn_tmp_4); })){
{ dn_utf8 dn_tmp_5=((dn_utf8){(const uint8_t*)"dlm-transport/1/unordered", 25});return dn_tmp_5; }
}
else{
}
if(({ uint32_t dn_tmp_6=v_profile;
uint32_t dn_tmp_7=((uint32_t)(6ULL));
(dn_tmp_6 == dn_tmp_7); })){
{ dn_utf8 dn_tmp_8=((dn_utf8){(const uint8_t*)"dlm-transport/1/datagram", 24});return dn_tmp_8; }
}
else{
}
{ dn_utf8 dn_tmp_9=((dn_utf8){(const uint8_t*)"", 0});return dn_tmp_9; }
}

dn_fail("missing native return");
}
static dn_utf8 dn_cb_transport_alpn(uint32_t v_profile) {
 if(!dn_enter()){return (dn_utf8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_utf8){0};}
 dn_utf8 dn_result=dn_f_transport_alpn(v_profile);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_QuicConfig dn_f_transport_quic_config(dn_r_TransportBounds v_bounds) {
{
if((!(({ dn_r_TransportBounds dn_tmp_0=v_bounds;
dn_f_transport_bounds_valid(dn_tmp_0); })))){
{ dn_r_QuicConfig dn_tmp_9=({ uint32_t dn_tmp_1=((uint32_t)(0ULL));
uint32_t dn_tmp_2=((uint32_t)(0ULL));
uint32_t dn_tmp_3=((uint32_t)(0ULL));
uint32_t dn_tmp_4=((uint32_t)(0ULL));
uint32_t dn_tmp_5=((uint32_t)(0ULL));
uint32_t dn_tmp_6=((uint32_t)(0ULL));
size_t dn_tmp_7=((size_t)(0ULL));
size_t dn_tmp_8=((size_t)(0ULL));
((dn_r_QuicConfig){dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8}); });return dn_tmp_9; }
}
else{
}
size_t v_window=({ size_t dn_tmp_10=({ uint32_t dn_tmp_11=(v_bounds).f_max_message;
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_11; });
size_t dn_tmp_12=((size_t)(16ULL));
dn_USize_add(dn_tmp_10,dn_tmp_12); });
if(({ size_t dn_tmp_13=v_window;
size_t dn_tmp_14=((size_t)(1024ULL));
(dn_tmp_13 < dn_tmp_14); })){
{ size_t dn_tmp_16=((size_t)(1024ULL));size_t *dn_tmp_15=&(v_window);*dn_tmp_15=dn_tmp_16; }
}
else{
}
{ dn_r_QuicConfig dn_tmp_33=({ uint32_t dn_tmp_17=((uint32_t)(1ULL));
uint32_t dn_tmp_18=({ uint32_t dn_tmp_19=(v_bounds).f_channels;
uint32_t dn_tmp_20=((uint32_t)(4ULL));
dn_U32_mul(dn_tmp_19,dn_tmp_20); });
uint32_t dn_tmp_21=({ uint32_t dn_tmp_22=({ uint32_t dn_tmp_23=(v_bounds).f_queued_messages;
uint32_t dn_tmp_24=((uint32_t)(2ULL));
dn_U32_mul(dn_tmp_23,dn_tmp_24); });
uint32_t dn_tmp_25=((uint32_t)(16ULL));
dn_U32_add(dn_tmp_22,dn_tmp_25); });
uint32_t dn_tmp_26=((uint32_t)(30000ULL));
uint32_t dn_tmp_27=((uint32_t)(10000ULL));
uint32_t dn_tmp_28=((uint32_t)(2ULL));
size_t dn_tmp_29=({ size_t dn_tmp_30=(v_bounds).f_memory;
size_t dn_tmp_31=((size_t)(2ULL));
dn_USize_div(dn_tmp_30,dn_tmp_31); });
size_t dn_tmp_32=v_window;
((dn_r_QuicConfig){dn_tmp_17,dn_tmp_18,dn_tmp_21,dn_tmp_26,dn_tmp_27,dn_tmp_28,dn_tmp_29,dn_tmp_32}); });return dn_tmp_33; }
}

dn_fail("missing native return");
}
static dn_r_QuicConfig dn_cb_transport_quic_config(dn_r_TransportBounds v_bounds) {
 if(!dn_enter()){return (dn_r_QuicConfig){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_QuicConfig){0};}
 dn_r_QuicConfig dn_result=dn_f_transport_quic_config(v_bounds);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TransportSession dn_f_transport_open(dn_r_TransportChoice v_choice,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds,dn_r_TransportTarget v_target,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
if(({ int32_t dn_tmp_0=(v_choice).f_status;
int32_t dn_tmp_1=({ int32_t dn_tmp_2=((int32_t)(0ULL));
if((__int128)dn_tmp_2<(-((__int128)1<<31))||(__int128)dn_tmp_2>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_2; });
(dn_tmp_0 != dn_tmp_1); })){
{ dn_r_TransportSession dn_tmp_8=({ dn_t_resource_r9_NetOpened_transport_dispose * dn_tmp_3=({ dn_t_resource_r9_NetOpened_transport_dispose * dn_tmp_4=dn_begin_resource_r9_NetOpened_transport_dispose();dn_commit_resource_r9_NetOpened_transport_dispose(dn_tmp_4,({ void * dn_tmp_5=({ ((void *){0}); });
int32_t dn_tmp_6=(v_choice).f_status;
int32_t dn_tmp_7=((int32_t)(0ULL));
((dn_r_NetOpened){dn_tmp_5,dn_tmp_6,dn_tmp_7}); })); });
dn_adopt_resource_r9_NetOpened_transport_dispose(&dn_tmp_3);
((dn_r_TransportSession){dn_tmp_3}); });dn_adopt_r16_TransportSession(&dn_tmp_8);return dn_tmp_8; }
}
else{
}
{
dn_r_TransportNativeTarget v_native_target=({ dn_r_NetAddress dn_tmp_9=((v_target).f_local).f_value;
dn_r_NetAddress dn_tmp_10=((v_target).f_peer).f_value;
dn_r_NetText dn_tmp_11=({ dn_utf8 dn_tmp_12=(v_target).f_identity;
dn_f_net_text(dn_tmp_12); });
dn_r_NetText dn_tmp_13=({ dn_utf8 dn_tmp_14=(v_target).f_ca_file;
dn_f_net_text(dn_tmp_14); });
dn_r_NetText dn_tmp_15=({ dn_utf8 dn_tmp_16=(v_target).f_pin;
dn_f_net_text(dn_tmp_16); });
dn_r_NetText dn_tmp_17=({ dn_utf8 dn_tmp_18=({ uint32_t dn_tmp_19=(v_choice).f_profile;
dn_f_transport_alpn(dn_tmp_19); });
dn_f_net_text(dn_tmp_18); });
((dn_r_TransportNativeTarget){dn_tmp_9,dn_tmp_10,dn_tmp_11,dn_tmp_13,dn_tmp_15,dn_tmp_17}); });
{ dn_r_TransportSession dn_tmp_29=({ dn_t_resource_r9_NetOpened_transport_dispose * dn_tmp_20=({ dn_t_resource_r9_NetOpened_transport_dispose * dn_tmp_21=dn_begin_resource_r9_NetOpened_transport_dispose();dn_commit_resource_r9_NetOpened_transport_dispose(dn_tmp_21,({ uint32_t dn_tmp_22=(v_choice).f_profile;
dn_r_TransportRequirements dn_tmp_23=v_requirements;
dn_r_TransportBounds dn_tmp_24=v_bounds;
dn_r_TransportNativeTarget dn_tmp_25=v_native_target;
uint32_t dn_tmp_26=v_timeout_ms;
void * dn_tmp_27=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_net_cancel_dispose * const * dn_tmp_28=v_cancel;
(&((*dn_tmp_28)->value)); })))));
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_transport_open(dn_tmp_22,dn_tmp_23,dn_tmp_24,dn_tmp_25,dn_tmp_26,dn_tmp_27);dn_fp_default();dn_foreign; }); })); });
dn_adopt_resource_r9_NetOpened_transport_dispose(&dn_tmp_20);
((dn_r_TransportSession){dn_tmp_20}); });dn_adopt_r16_TransportSession(&dn_tmp_29);return dn_tmp_29; }
}
}

dn_fail("missing native return");
}
static dn_r_TransportSession dn_cb_transport_open(dn_r_TransportChoice v_choice,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds,dn_r_TransportTarget v_target,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_TransportSession){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TransportSession){0};}
 dn_r_TransportSession dn_result=dn_f_transport_open(v_choice,v_requirements,v_bounds,v_target,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TransportSession dn_f_transport_adopt_tcp(dn_r_TcpSocket v_socket,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds) {
dn_r_TcpSocket volatile dn_param_socket=v_socket;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r9_TcpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
{
{ dn_r_TransportSession dn_tmp_13=({ dn_t_resource_r9_NetOpened_transport_dispose * dn_tmp_2=({ dn_t_resource_r9_NetOpened_transport_dispose * dn_tmp_3=dn_begin_resource_r9_NetOpened_transport_dispose();dn_commit_resource_r9_NetOpened_transport_dispose(dn_tmp_3,({ dn_r_NetOpened dn_tmp_4=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_5=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_6=(dn_param_socket).f_inner;(dn_param_socket).f_inner=(dn_t_resource_r9_NetOpened_net_dispose *){0};dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_6);dn_tmp_6; });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_5);
dn_take_resource_r9_NetOpened_net_dispose(dn_tmp_5); });
uint32_t dn_tmp_7=((uint32_t)(1ULL));
dn_r_TransportRequirements dn_tmp_8=v_requirements;
dn_r_TransportBounds dn_tmp_9=v_bounds;
dn_r_NetAddress dn_tmp_10=({ ((dn_r_NetAddress){0}); });
uint64_t dn_tmp_11=((uint64_t)(0ULL));
bool dn_tmp_12=1;
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_transport_adopt(dn_tmp_4,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_11,dn_tmp_12);dn_fp_default();dn_foreign; }); })); });
dn_adopt_resource_r9_NetOpened_transport_dispose(&dn_tmp_2);
((dn_r_TransportSession){dn_tmp_2}); });dn_adopt_r16_TransportSession(&dn_tmp_13);dn_drop_r9_TcpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_13; }
}
}

dn_drop_r9_TcpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
dn_fail("missing native return");
}
static dn_r_TransportSession dn_cb_transport_adopt_tcp(dn_r_TcpSocket v_socket,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds) {
 if(!dn_enter()){return (dn_r_TransportSession){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TransportSession){0};}
 dn_r_TransportSession dn_result=dn_f_transport_adopt_tcp(v_socket,v_requirements,v_bounds);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TransportSession dn_f_transport_adopt_tls(dn_r_TlsSocket v_socket,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds) {
dn_r_TlsSocket volatile dn_param_socket=v_socket;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r9_TlsSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
{
{ dn_r_TransportSession dn_tmp_13=({ dn_t_resource_r9_NetOpened_transport_dispose * dn_tmp_2=({ dn_t_resource_r9_NetOpened_transport_dispose * dn_tmp_3=dn_begin_resource_r9_NetOpened_transport_dispose();dn_commit_resource_r9_NetOpened_transport_dispose(dn_tmp_3,({ dn_r_NetOpened dn_tmp_4=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_5=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_6=(dn_param_socket).f_inner;(dn_param_socket).f_inner=(dn_t_resource_r9_NetOpened_net_dispose *){0};dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_6);dn_tmp_6; });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_5);
dn_take_resource_r9_NetOpened_net_dispose(dn_tmp_5); });
uint32_t dn_tmp_7=((uint32_t)(2ULL));
dn_r_TransportRequirements dn_tmp_8=v_requirements;
dn_r_TransportBounds dn_tmp_9=v_bounds;
dn_r_NetAddress dn_tmp_10=({ ((dn_r_NetAddress){0}); });
uint64_t dn_tmp_11=((uint64_t)(0ULL));
bool dn_tmp_12=1;
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_transport_adopt(dn_tmp_4,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_11,dn_tmp_12);dn_fp_default();dn_foreign; }); })); });
dn_adopt_resource_r9_NetOpened_transport_dispose(&dn_tmp_2);
((dn_r_TransportSession){dn_tmp_2}); });dn_adopt_r16_TransportSession(&dn_tmp_13);dn_drop_r9_TlsSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_13; }
}
}

dn_drop_r9_TlsSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
dn_fail("missing native return");
}
static dn_r_TransportSession dn_cb_transport_adopt_tls(dn_r_TlsSocket v_socket,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds) {
 if(!dn_enter()){return (dn_r_TransportSession){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TransportSession){0};}
 dn_r_TransportSession dn_result=dn_f_transport_adopt_tls(v_socket,v_requirements,v_bounds);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TransportSession dn_f_transport_adopt_udp(dn_r_UdpSocket v_socket,dn_r_SocketAddress v_peer,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds) {
dn_r_UdpSocket volatile dn_param_socket=v_socket;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r9_UdpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
{
{ dn_r_TransportSession dn_tmp_13=({ dn_t_resource_r9_NetOpened_transport_dispose * dn_tmp_2=({ dn_t_resource_r9_NetOpened_transport_dispose * dn_tmp_3=dn_begin_resource_r9_NetOpened_transport_dispose();dn_commit_resource_r9_NetOpened_transport_dispose(dn_tmp_3,({ dn_r_NetOpened dn_tmp_4=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_5=({ dn_t_resource_r9_NetOpened_net_dispose * dn_tmp_6=(dn_param_socket).f_inner;(dn_param_socket).f_inner=(dn_t_resource_r9_NetOpened_net_dispose *){0};dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_6);dn_tmp_6; });
dn_adopt_resource_r9_NetOpened_net_dispose(&dn_tmp_5);
dn_take_resource_r9_NetOpened_net_dispose(dn_tmp_5); });
uint32_t dn_tmp_7=((uint32_t)(3ULL));
dn_r_TransportRequirements dn_tmp_8=v_requirements;
dn_r_TransportBounds dn_tmp_9=v_bounds;
dn_r_NetAddress dn_tmp_10=(v_peer).f_value;
uint64_t dn_tmp_11=((uint64_t)(0ULL));
bool dn_tmp_12=1;
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_transport_adopt(dn_tmp_4,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_11,dn_tmp_12);dn_fp_default();dn_foreign; }); })); });
dn_adopt_resource_r9_NetOpened_transport_dispose(&dn_tmp_2);
((dn_r_TransportSession){dn_tmp_2}); });dn_adopt_r16_TransportSession(&dn_tmp_13);dn_drop_r9_UdpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_13; }
}
}

dn_drop_r9_UdpSocket(&dn_param_socket);
dn_boundary_end(&dn_tmp_0,0);
dn_fail("missing native return");
}
static dn_r_TransportSession dn_cb_transport_adopt_udp(dn_r_UdpSocket v_socket,dn_r_SocketAddress v_peer,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds) {
 if(!dn_enter()){return (dn_r_TransportSession){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TransportSession){0};}
 dn_r_TransportSession dn_result=dn_f_transport_adopt_udp(v_socket,v_peer,v_requirements,v_bounds);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TransportSession dn_f_transport_adopt_quic(dn_r_QuicEndpoint v_endpoint,dn_r_QuicConnection v_connection,bool v_initiator,uint32_t v_profile,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds) {
dn_r_QuicEndpoint volatile dn_param_endpoint=v_endpoint;
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r12_QuicEndpoint(&dn_param_endpoint);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
{
if((({ uint32_t dn_tmp_2=v_profile;
uint32_t dn_tmp_3=((uint32_t)(4ULL));
(dn_tmp_2 < dn_tmp_3); }) || ({ uint32_t dn_tmp_4=v_profile;
uint32_t dn_tmp_5=((uint32_t)(6ULL));
(dn_tmp_4 > dn_tmp_5); }))){
(void)(({ dn_r_QuicEndpoint dn_tmp_6=({ dn_r_QuicEndpoint dn_tmp_7=dn_param_endpoint;dn_param_endpoint=(dn_r_QuicEndpoint){0};dn_adopt_r12_QuicEndpoint(&dn_tmp_7);dn_tmp_7; });
dn_adopt_r12_QuicEndpoint(&dn_tmp_6);
dn_f_quic_close(dn_tmp_6); }));
{ dn_r_TransportSession dn_tmp_13=({ dn_t_resource_r9_NetOpened_transport_dispose * dn_tmp_8=({ dn_t_resource_r9_NetOpened_transport_dispose * dn_tmp_9=dn_begin_resource_r9_NetOpened_transport_dispose();dn_commit_resource_r9_NetOpened_transport_dispose(dn_tmp_9,({ void * dn_tmp_10=({ ((void *){0}); });
int32_t dn_tmp_11=((int32_t)(4ULL));
int32_t dn_tmp_12=((int32_t)(0ULL));
((dn_r_NetOpened){dn_tmp_10,dn_tmp_11,dn_tmp_12}); })); });
dn_adopt_resource_r9_NetOpened_transport_dispose(&dn_tmp_8);
((dn_r_TransportSession){dn_tmp_8}); });dn_adopt_r16_TransportSession(&dn_tmp_13);dn_drop_r12_QuicEndpoint(&dn_param_endpoint);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_13; }
}
else{
}
{
{ dn_r_TransportSession dn_tmp_25=({ dn_t_resource_r9_NetOpened_transport_dispose * dn_tmp_14=({ dn_t_resource_r9_NetOpened_transport_dispose * dn_tmp_15=dn_begin_resource_r9_NetOpened_transport_dispose();dn_commit_resource_r9_NetOpened_transport_dispose(dn_tmp_15,({ dn_r_NetOpened dn_tmp_16=({ dn_t_resource_r9_NetOpened_quic_dispose * dn_tmp_17=({ dn_t_resource_r9_NetOpened_quic_dispose * dn_tmp_18=(dn_param_endpoint).f_inner;(dn_param_endpoint).f_inner=(dn_t_resource_r9_NetOpened_quic_dispose *){0};dn_adopt_resource_r9_NetOpened_quic_dispose(&dn_tmp_18);dn_tmp_18; });
dn_adopt_resource_r9_NetOpened_quic_dispose(&dn_tmp_17);
dn_take_resource_r9_NetOpened_quic_dispose(dn_tmp_17); });
uint32_t dn_tmp_19=v_profile;
dn_r_TransportRequirements dn_tmp_20=v_requirements;
dn_r_TransportBounds dn_tmp_21=v_bounds;
dn_r_NetAddress dn_tmp_22=({ ((dn_r_NetAddress){0}); });
uint64_t dn_tmp_23=(v_connection).f_id;
bool dn_tmp_24=v_initiator;
({ dn_r_NetOpened dn_foreign=dn_ext_dlm_net_transport_adopt(dn_tmp_16,dn_tmp_19,dn_tmp_20,dn_tmp_21,dn_tmp_22,dn_tmp_23,dn_tmp_24);dn_fp_default();dn_foreign; }); })); });
dn_adopt_resource_r9_NetOpened_transport_dispose(&dn_tmp_14);
((dn_r_TransportSession){dn_tmp_14}); });dn_adopt_r16_TransportSession(&dn_tmp_25);dn_drop_r12_QuicEndpoint(&dn_param_endpoint);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_25; }
}
}

dn_drop_r12_QuicEndpoint(&dn_param_endpoint);
dn_boundary_end(&dn_tmp_0,0);
dn_fail("missing native return");
}
static dn_r_TransportSession dn_cb_transport_adopt_quic(dn_r_QuicEndpoint v_endpoint,dn_r_QuicConnection v_connection,bool v_initiator,uint32_t v_profile,dn_r_TransportRequirements v_requirements,dn_r_TransportBounds v_bounds) {
 if(!dn_enter()){return (dn_r_TransportSession){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TransportSession){0};}
 dn_r_TransportSession dn_result=dn_f_transport_adopt_quic(v_endpoint,v_connection,v_initiator,v_profile,v_requirements,v_bounds);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_SocketAddressResult dn_f_transport_local(dn_r_TransportSession const * v_session) {
{
{
{ dn_r_SocketAddressResult dn_tmp_4=({ dn_r_NetResolved dn_tmp_0=({ void * dn_tmp_1=({ dn_r_TransportSession const * dn_tmp_2=v_session;
dn_f_transport_handle(dn_tmp_2); });
({ dn_r_NetResolved dn_foreign=dn_ext_dlm_net_transport_local(dn_tmp_1);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_3=((uint32_t)(91ULL));
dn_f_socket_address_result(dn_tmp_0,dn_tmp_3); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_SocketAddressResult dn_cb_transport_local(dn_r_TransportSession const * v_session) {
 if(!dn_enter()){return (dn_r_SocketAddressResult){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_SocketAddressResult){0};}
 dn_r_SocketAddressResult dn_result=dn_f_transport_local(v_session);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_transport_open_channel(dn_r_TransportSession * v_session) {
{
{
{ dn_r_NetworkTransfer dn_tmp_4=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_TransportSession * dn_tmp_2=v_session;
dn_f_transport_handle(dn_tmp_2); });
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_transport_channel(dn_tmp_1);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_3=((uint32_t)(92ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_3); });return dn_tmp_4; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_transport_open_channel(dn_r_TransportSession * v_session) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_transport_open_channel(v_session);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_transport_send(dn_r_TransportSession * v_session,uint32_t v_channel,dn_t_slice0_U8 v_bytes) {
{
{
dn_r_NetText v_data=({ dn_t_slice0_U8 dn_tmp_0=v_bytes;
dn_f_net_bytes(dn_tmp_0); });
{ dn_r_NetworkTransfer dn_tmp_8=({ dn_r_NetIo dn_tmp_1=({ void * dn_tmp_2=({ dn_r_TransportSession * dn_tmp_3=v_session;
dn_f_transport_handle(dn_tmp_3); });
uint32_t dn_tmp_4=v_channel;
uint8_t * dn_tmp_5=(v_data).f_data;
size_t dn_tmp_6=(v_data).f_count;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_transport_send(dn_tmp_2,dn_tmp_4,dn_tmp_5,dn_tmp_6);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_7=((uint32_t)(93ULL));
dn_f_socket_transfer(dn_tmp_1,dn_tmp_7); });return dn_tmp_8; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_transport_send(dn_r_TransportSession * v_session,uint32_t v_channel,dn_t_slice0_U8 v_bytes) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_transport_send(v_session,v_channel,v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_transport_poll(dn_r_TransportSession * v_session,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
{
{ dn_r_NetworkTransfer dn_tmp_7=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_TransportSession * dn_tmp_2=v_session;
dn_f_transport_handle(dn_tmp_2); });
uint32_t dn_tmp_3=v_timeout_ms;
void * dn_tmp_4=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_net_cancel_dispose * const * dn_tmp_5=v_cancel;
(&((*dn_tmp_5)->value)); })))));
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_transport_poll(dn_tmp_1,dn_tmp_3,dn_tmp_4);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_6=((uint32_t)(94ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_6); });return dn_tmp_7; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_transport_poll(dn_r_TransportSession * v_session,uint32_t v_timeout_ms,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_transport_poll(v_session,v_timeout_ms,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TransportMessage dn_f_transport_peek(dn_r_TransportSession const * v_session) {
{
{
dn_r_TransportNativeMessage v_r=({ void * dn_tmp_0=({ dn_r_TransportSession const * dn_tmp_1=v_session;
dn_f_transport_handle(dn_tmp_1); });
({ dn_r_TransportNativeMessage dn_foreign=dn_ext_dlm_net_transport_peek(dn_tmp_0);dn_fp_default();dn_foreign; }); });
{ dn_r_TransportMessage dn_tmp_9=({ uint64_t dn_tmp_2=(v_r).f_id;
uint32_t dn_tmp_3=(v_r).f_channel;
size_t dn_tmp_4=(v_r).f_count;
dn_r_NetworkError dn_tmp_5=({ int32_t dn_tmp_6=(v_r).f_status;
int32_t dn_tmp_7=(v_r).f_system;
uint32_t dn_tmp_8=((uint32_t)(95ULL));
((dn_r_NetworkError){dn_tmp_6,dn_tmp_7,dn_tmp_8}); });
((dn_r_TransportMessage){dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5}); });return dn_tmp_9; }
}
}

dn_fail("missing native return");
}
static dn_r_TransportMessage dn_cb_transport_peek(dn_r_TransportSession const * v_session) {
 if(!dn_enter()){return (dn_r_TransportMessage){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TransportMessage){0};}
 dn_r_TransportMessage dn_result=dn_f_transport_peek(v_session);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_transport_copy(dn_r_TransportSession const * v_session,uint64_t v_message,size_t v_offset,dn_t_buf_U8 * v_output) {
{
{
{ dn_r_NetworkTransfer dn_tmp_10=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_TransportSession const * dn_tmp_2=v_session;
dn_f_transport_handle(dn_tmp_2); });
uint64_t dn_tmp_3=v_message;
size_t dn_tmp_4=v_offset;
uint8_t * dn_tmp_5=({ dn_t_buf_U8 * dn_tmp_6=v_output;
dn_f_net_output(dn_tmp_6); });
size_t dn_tmp_7=({ dn_t_buf_U8 * dn_tmp_8=v_output;
(*(dn_tmp_8)).len; });
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_transport_copy(dn_tmp_1,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_7);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_9=((uint32_t)(96ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_9); });return dn_tmp_10; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_transport_copy(dn_r_TransportSession const * v_session,uint64_t v_message,size_t v_offset,dn_t_buf_U8 * v_output) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_transport_copy(v_session,v_message,v_offset,v_output);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkTransfer dn_f_transport_ack(dn_r_TransportSession * v_session,uint64_t v_message) {
{
{
{ dn_r_NetworkTransfer dn_tmp_5=({ dn_r_NetIo dn_tmp_0=({ void * dn_tmp_1=({ dn_r_TransportSession * dn_tmp_2=v_session;
dn_f_transport_handle(dn_tmp_2); });
uint64_t dn_tmp_3=v_message;
({ dn_r_NetIo dn_foreign=dn_ext_dlm_net_transport_ack(dn_tmp_1,dn_tmp_3);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_4=((uint32_t)(97ULL));
dn_f_socket_transfer(dn_tmp_0,dn_tmp_4); });return dn_tmp_5; }
}
}

dn_fail("missing native return");
}
static dn_r_NetworkTransfer dn_cb_transport_ack(dn_r_TransportSession * v_session,uint64_t v_message) {
 if(!dn_enter()){return (dn_r_NetworkTransfer){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkTransfer){0};}
 dn_r_NetworkTransfer dn_result=dn_f_transport_ack(v_session,v_message);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TransportDelivery dn_f_transport_receive(dn_r_TransportSession * v_session,dn_t_buf_U8 * v_output) {
{
{
dn_r_TransportNativeDelivery v_r=({ void * dn_tmp_0=({ dn_r_TransportSession * dn_tmp_1=v_session;
dn_f_transport_handle(dn_tmp_1); });
uint8_t * dn_tmp_2=({ dn_t_buf_U8 * dn_tmp_3=v_output;
dn_f_net_output(dn_tmp_3); });
size_t dn_tmp_4=({ dn_t_buf_U8 * dn_tmp_5=v_output;
(*(dn_tmp_5)).len; });
({ dn_r_TransportNativeDelivery dn_foreign=dn_ext_dlm_net_transport_receive(dn_tmp_0,dn_tmp_2,dn_tmp_4);dn_fp_default();dn_foreign; }); });
{ dn_r_TransportDelivery dn_tmp_14=({ uint64_t dn_tmp_6=(v_r).f_id;
uint32_t dn_tmp_7=(v_r).f_channel;
size_t dn_tmp_8=(v_r).f_copied;
size_t dn_tmp_9=(v_r).f_required;
dn_r_NetworkError dn_tmp_10=({ int32_t dn_tmp_11=(v_r).f_status;
int32_t dn_tmp_12=(v_r).f_system;
uint32_t dn_tmp_13=((uint32_t)(98ULL));
((dn_r_NetworkError){dn_tmp_11,dn_tmp_12,dn_tmp_13}); });
((dn_r_TransportDelivery){dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10}); });return dn_tmp_14; }
}
}

dn_fail("missing native return");
}
static dn_r_TransportDelivery dn_cb_transport_receive(dn_r_TransportSession * v_session,dn_t_buf_U8 * v_output) {
 if(!dn_enter()){return (dn_r_TransportDelivery){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TransportDelivery){0};}
 dn_r_TransportDelivery dn_result=dn_f_transport_receive(v_session,v_output);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TransportStatistics dn_f_transport_statistics(dn_r_TransportSession const * v_session) {
{
{
dn_r_TransportNativeStatistics v_r=({ void * dn_tmp_0=({ dn_r_TransportSession const * dn_tmp_1=v_session;
dn_f_transport_handle(dn_tmp_1); });
({ dn_r_TransportNativeStatistics dn_foreign=dn_ext_dlm_net_transport_stats(dn_tmp_0);dn_fp_default();dn_foreign; }); });
{ dn_r_TransportStatistics dn_tmp_16=({ size_t dn_tmp_2=(v_r).f_used;
size_t dn_tmp_3=(v_r).f_limit;
size_t dn_tmp_4=(v_r).f_adapter_limit;
size_t dn_tmp_5=(v_r).f_backend_limit;
uint32_t dn_tmp_6=(v_r).f_profile;
uint32_t dn_tmp_7=(v_r).f_properties;
uint32_t dn_tmp_8=(v_r).f_state;
uint32_t dn_tmp_9=(v_r).f_send_queued;
uint32_t dn_tmp_10=(v_r).f_receive_queued;
uint32_t dn_tmp_11=(v_r).f_channels;
dn_r_NetworkError dn_tmp_12=({ int32_t dn_tmp_13=(v_r).f_status;
int32_t dn_tmp_14=(v_r).f_system;
uint32_t dn_tmp_15=((uint32_t)(99ULL));
((dn_r_NetworkError){dn_tmp_13,dn_tmp_14,dn_tmp_15}); });
((dn_r_TransportStatistics){dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_11,dn_tmp_12}); });return dn_tmp_16; }
}
}

dn_fail("missing native return");
}
static dn_r_TransportStatistics dn_cb_transport_statistics(dn_r_TransportSession const * v_session) {
 if(!dn_enter()){return (dn_r_TransportStatistics){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TransportStatistics){0};}
 dn_r_TransportStatistics dn_result=dn_f_transport_statistics(v_session);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_exchange(dn_r_TransportSession * v_client,dn_r_TransportSession * v_server,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
{
dn_t_buf_U8 volatile v_payload={0};
dn_t_buf_U8 volatile v_small={0};
dn_t_buf_U8 volatile v_output={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_output);
dn_drop_buf_U8(&v_small);
dn_drop_buf_U8(&v_payload);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_payload=({ size_t dn_tmp_2=((size_t)(4ULL));
dn_new_buf_U8(dn_tmp_2); });
{ uint8_t dn_tmp_6=((uint8_t)(0ULL));uint8_t *dn_tmp_5=&((*({ dn_t_buf_U8 volatile *dn_tmp_3=&(v_payload);size_t dn_tmp_4=((size_t)(0ULL));&dn_tmp_3->data[dn_index(dn_tmp_4,dn_tmp_3->len)]; })));*dn_tmp_5=dn_tmp_6; }
{ uint8_t dn_tmp_10=((uint8_t)(255ULL));uint8_t *dn_tmp_9=&((*({ dn_t_buf_U8 volatile *dn_tmp_7=&(v_payload);size_t dn_tmp_8=((size_t)(1ULL));&dn_tmp_7->data[dn_index(dn_tmp_8,dn_tmp_7->len)]; })));*dn_tmp_9=dn_tmp_10; }
{ uint8_t dn_tmp_14=((uint8_t)(128ULL));uint8_t *dn_tmp_13=&((*({ dn_t_buf_U8 volatile *dn_tmp_11=&(v_payload);size_t dn_tmp_12=((size_t)(2ULL));&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_13=dn_tmp_14; }
{ uint8_t dn_tmp_18=((uint8_t)(42ULL));uint8_t *dn_tmp_17=&((*({ dn_t_buf_U8 volatile *dn_tmp_15=&(v_payload);size_t dn_tmp_16=((size_t)(3ULL));&dn_tmp_15->data[dn_index(dn_tmp_16,dn_tmp_15->len)]; })));*dn_tmp_17=dn_tmp_18; }
dn_r_NetworkTransfer v_first=({ dn_r_TransportSession * dn_tmp_19=v_client;
uint32_t dn_tmp_20=((uint32_t)(0ULL));
dn_t_slice0_U8 dn_tmp_21=({ dn_t_buf_U8 const * dn_tmp_22=(&(v_payload));
size_t dn_tmp_23=((size_t)(0ULL));
size_t dn_tmp_24=((size_t)(4ULL));
({ dn_range(dn_tmp_23,dn_tmp_24,(*(dn_tmp_22)).len);(dn_t_slice0_U8){(*(dn_tmp_22)).data?(*(dn_tmp_22)).data+dn_tmp_23:NULL,dn_tmp_24}; }); });
dn_f_transport_send(dn_tmp_19,dn_tmp_20,dn_tmp_21); });
dn_r_NetworkTransfer v_full=({ dn_r_TransportSession * dn_tmp_25=v_client;
uint32_t dn_tmp_26=((uint32_t)(0ULL));
dn_t_slice0_U8 dn_tmp_27=({ dn_t_buf_U8 const * dn_tmp_28=(&(v_payload));
size_t dn_tmp_29=((size_t)(0ULL));
size_t dn_tmp_30=((size_t)(4ULL));
({ dn_range(dn_tmp_29,dn_tmp_30,(*(dn_tmp_28)).len);(dn_t_slice0_U8){(*(dn_tmp_28)).data?(*(dn_tmp_28)).data+dn_tmp_29:NULL,dn_tmp_30}; }); });
dn_f_transport_send(dn_tmp_25,dn_tmp_26,dn_tmp_27); });
(void)(({ bool dn_tmp_31=(({ dn_r_NetworkError const * dn_tmp_32=(&((v_first).f_error));
dn_f_network_ok(dn_tmp_32); }) && ({ dn_r_NetworkError const * dn_tmp_33=(&((v_full).f_error));
dn_f_network_would_block(dn_tmp_33); }));
(dn_assert(dn_tmp_31),0); }));
dn_r_NetworkTransfer v_write=({ dn_r_TransportSession * dn_tmp_34=v_client;
uint32_t dn_tmp_35=((uint32_t)(0ULL));
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_36=v_cancel;
dn_f_transport_poll(dn_tmp_34,dn_tmp_35,dn_tmp_36); });
(void)(({ bool dn_tmp_37=({ dn_r_NetworkError const * dn_tmp_38=(&((v_write).f_error));
dn_f_network_ok(dn_tmp_38); });
(dn_assert(dn_tmp_37),0); }));
dn_r_NetworkTransfer v_read=({ dn_r_TransportSession * dn_tmp_39=v_server;
uint32_t dn_tmp_40=((uint32_t)(1000ULL));
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_41=v_cancel;
dn_f_transport_poll(dn_tmp_39,dn_tmp_40,dn_tmp_41); });
(void)(({ bool dn_tmp_42=({ dn_r_NetworkError const * dn_tmp_43=(&((v_read).f_error));
dn_f_network_ok(dn_tmp_43); });
(dn_assert(dn_tmp_42),0); }));
dn_r_TransportMessage v_message=({ dn_r_TransportSession * dn_tmp_44=v_server;
dn_f_transport_peek(dn_tmp_44); });
(void)(({ bool dn_tmp_45=((({ dn_r_NetworkError const * dn_tmp_46=(&((v_message).f_error));
dn_f_network_ok(dn_tmp_46); }) && ({ uint32_t dn_tmp_47=(v_message).f_channel;
uint32_t dn_tmp_48=((uint32_t)(0ULL));
(dn_tmp_47 == dn_tmp_48); })) && ({ size_t dn_tmp_49=(v_message).f_count;
size_t dn_tmp_50=((size_t)(4ULL));
(dn_tmp_49 == dn_tmp_50); }));
(dn_assert(dn_tmp_45),0); }));
v_small=({ size_t dn_tmp_51=((size_t)(2ULL));
dn_new_buf_U8(dn_tmp_51); });
{ uint8_t dn_tmp_55=((uint8_t)(165ULL));uint8_t *dn_tmp_54=&((*({ dn_t_buf_U8 volatile *dn_tmp_52=&(v_small);size_t dn_tmp_53=((size_t)(0ULL));&dn_tmp_52->data[dn_index(dn_tmp_53,dn_tmp_52->len)]; })));*dn_tmp_54=dn_tmp_55; }
{ uint8_t dn_tmp_59=((uint8_t)(165ULL));uint8_t *dn_tmp_58=&((*({ dn_t_buf_U8 volatile *dn_tmp_56=&(v_small);size_t dn_tmp_57=((size_t)(1ULL));&dn_tmp_56->data[dn_index(dn_tmp_57,dn_tmp_56->len)]; })));*dn_tmp_58=dn_tmp_59; }
dn_r_TransportDelivery v_truncated=({ dn_r_TransportSession * dn_tmp_60=v_server;
dn_t_buf_U8 * dn_tmp_61=(&(v_small));
dn_f_transport_receive(dn_tmp_60,dn_tmp_61); });
(void)(({ bool dn_tmp_62=((((({ dn_r_NetworkError const * dn_tmp_63=(&((v_truncated).f_error));
dn_f_network_truncated(dn_tmp_63); }) && ({ size_t dn_tmp_64=(v_truncated).f_required;
size_t dn_tmp_65=((size_t)(4ULL));
(dn_tmp_64 == dn_tmp_65); })) && ({ size_t dn_tmp_66=(v_truncated).f_copied;
size_t dn_tmp_67=((size_t)(0ULL));
(dn_tmp_66 == dn_tmp_67); })) && ({ uint8_t dn_tmp_68=(*({ dn_t_buf_U8 volatile *dn_tmp_69=&(v_small);size_t dn_tmp_70=((size_t)(0ULL));&dn_tmp_69->data[dn_index(dn_tmp_70,dn_tmp_69->len)]; }));
uint8_t dn_tmp_71=((uint8_t)(165ULL));
(dn_tmp_68 == dn_tmp_71); })) && ({ uint8_t dn_tmp_72=(*({ dn_t_buf_U8 volatile *dn_tmp_73=&(v_small);size_t dn_tmp_74=((size_t)(1ULL));&dn_tmp_73->data[dn_index(dn_tmp_74,dn_tmp_73->len)]; }));
uint8_t dn_tmp_75=((uint8_t)(165ULL));
(dn_tmp_72 == dn_tmp_75); }));
(dn_assert(dn_tmp_62),0); }));
dn_r_TransportMessage v_retained=({ dn_r_TransportSession * dn_tmp_76=v_server;
dn_f_transport_peek(dn_tmp_76); });
(void)(({ bool dn_tmp_77=({ uint64_t dn_tmp_78=(v_retained).f_id;
uint64_t dn_tmp_79=(v_message).f_id;
(dn_tmp_78 == dn_tmp_79); });
(dn_assert(dn_tmp_77),0); }));
v_output=({ size_t dn_tmp_80=((size_t)(8ULL));
dn_new_buf_U8(dn_tmp_80); });
dn_r_TransportDelivery v_received=({ dn_r_TransportSession * dn_tmp_81=v_server;
dn_t_buf_U8 * dn_tmp_82=(&(v_output));
dn_f_transport_receive(dn_tmp_81,dn_tmp_82); });
(void)(({ bool dn_tmp_83=(((((({ dn_r_NetworkError const * dn_tmp_84=(&((v_received).f_error));
dn_f_network_ok(dn_tmp_84); }) && ({ size_t dn_tmp_85=(v_received).f_copied;
size_t dn_tmp_86=((size_t)(4ULL));
(dn_tmp_85 == dn_tmp_86); })) && ({ uint8_t dn_tmp_87=(*({ dn_t_buf_U8 volatile *dn_tmp_88=&(v_output);size_t dn_tmp_89=((size_t)(0ULL));&dn_tmp_88->data[dn_index(dn_tmp_89,dn_tmp_88->len)]; }));
uint8_t dn_tmp_90=((uint8_t)(0ULL));
(dn_tmp_87 == dn_tmp_90); })) && ({ uint8_t dn_tmp_91=(*({ dn_t_buf_U8 volatile *dn_tmp_92=&(v_output);size_t dn_tmp_93=((size_t)(1ULL));&dn_tmp_92->data[dn_index(dn_tmp_93,dn_tmp_92->len)]; }));
uint8_t dn_tmp_94=((uint8_t)(255ULL));
(dn_tmp_91 == dn_tmp_94); })) && ({ uint8_t dn_tmp_95=(*({ dn_t_buf_U8 volatile *dn_tmp_96=&(v_output);size_t dn_tmp_97=((size_t)(2ULL));&dn_tmp_96->data[dn_index(dn_tmp_97,dn_tmp_96->len)]; }));
uint8_t dn_tmp_98=((uint8_t)(128ULL));
(dn_tmp_95 == dn_tmp_98); })) && ({ uint8_t dn_tmp_99=(*({ dn_t_buf_U8 volatile *dn_tmp_100=&(v_output);size_t dn_tmp_101=((size_t)(3ULL));&dn_tmp_100->data[dn_index(dn_tmp_101,dn_tmp_100->len)]; }));
uint8_t dn_tmp_102=((uint8_t)(42ULL));
(dn_tmp_99 == dn_tmp_102); }));
(dn_assert(dn_tmp_83),0); }));
dn_r_NetworkTransfer v_reply=({ dn_r_TransportSession * dn_tmp_103=v_server;
uint32_t dn_tmp_104=((uint32_t)(0ULL));
dn_t_slice0_U8 dn_tmp_105=({ dn_t_buf_U8 const * dn_tmp_106=(&(v_payload));
size_t dn_tmp_107=((size_t)(0ULL));
size_t dn_tmp_108=((size_t)(0ULL));
({ dn_range(dn_tmp_107,dn_tmp_108,(*(dn_tmp_106)).len);(dn_t_slice0_U8){(*(dn_tmp_106)).data?(*(dn_tmp_106)).data+dn_tmp_107:NULL,dn_tmp_108}; }); });
dn_f_transport_send(dn_tmp_103,dn_tmp_104,dn_tmp_105); });
(void)(({ bool dn_tmp_109=({ dn_r_NetworkError const * dn_tmp_110=(&((v_reply).f_error));
dn_f_network_ok(dn_tmp_110); });
(dn_assert(dn_tmp_109),0); }));
dn_r_NetworkTransfer v_a=({ dn_r_TransportSession * dn_tmp_111=v_server;
uint32_t dn_tmp_112=((uint32_t)(0ULL));
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_113=v_cancel;
dn_f_transport_poll(dn_tmp_111,dn_tmp_112,dn_tmp_113); });
dn_r_NetworkTransfer v_b=({ dn_r_TransportSession * dn_tmp_114=v_client;
uint32_t dn_tmp_115=((uint32_t)(1000ULL));
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_116=v_cancel;
dn_f_transport_poll(dn_tmp_114,dn_tmp_115,dn_tmp_116); });
(void)(({ bool dn_tmp_117=(({ dn_r_NetworkError const * dn_tmp_118=(&((v_a).f_error));
dn_f_network_ok(dn_tmp_118); }) && ({ dn_r_NetworkError const * dn_tmp_119=(&((v_b).f_error));
dn_f_network_ok(dn_tmp_119); }));
(dn_assert(dn_tmp_117),0); }));
dn_r_TransportDelivery v_empty=({ dn_r_TransportSession * dn_tmp_120=v_client;
dn_t_buf_U8 * dn_tmp_121=(&(v_output));
dn_f_transport_receive(dn_tmp_120,dn_tmp_121); });
(void)(({ bool dn_tmp_122=((({ dn_r_NetworkError const * dn_tmp_123=(&((v_empty).f_error));
dn_f_network_ok(dn_tmp_123); }) && ({ size_t dn_tmp_124=(v_empty).f_copied;
size_t dn_tmp_125=((size_t)(0ULL));
(dn_tmp_124 == dn_tmp_125); })) && ({ size_t dn_tmp_126=(v_empty).f_required;
size_t dn_tmp_127=((size_t)(0ULL));
(dn_tmp_126 == dn_tmp_127); }));
(dn_assert(dn_tmp_122),0); }));
dn_r_TransportStatistics v_stats=({ dn_r_TransportSession * dn_tmp_128=v_client;
dn_f_transport_statistics(dn_tmp_128); });
(void)(({ bool dn_tmp_129=(({ size_t dn_tmp_130=(v_stats).f_used;
size_t dn_tmp_131=(v_stats).f_limit;
(dn_tmp_130 <= dn_tmp_131); }) && ({ uint32_t dn_tmp_132=(v_stats).f_state;
uint32_t dn_tmp_133=((uint32_t)(1ULL));
(dn_tmp_132 == dn_tmp_133); }));
(dn_assert(dn_tmp_129),0); }));
dn_drop_buf_U8(&v_output);
dn_drop_buf_U8(&v_small);
dn_drop_buf_U8(&v_payload);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_exchange(dn_r_TransportSession * v_client,dn_r_TransportSession * v_server,dn_t_resource_h_net_cancel_dispose * const * v_cancel) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_exchange(v_client,v_server,v_cancel);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_main(void) {
{
dn_t_resource_h_net_cancel_dispose * volatile v_cancel={0};
dn_r_TcpListener volatile v_listener={0};
dn_r_TransportSession volatile v_client={0};
dn_r_TcpSocket volatile v_raw={0};
dn_r_TransportSession volatile v_server={0};
dn_r_UdpSocket volatile v_left={0};
dn_r_UdpSocket volatile v_right={0};
dn_r_TransportSession volatile v_uc={0};
dn_r_TransportSession volatile v_us={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r16_TransportSession(&v_us);
dn_drop_r16_TransportSession(&v_uc);
dn_drop_r9_UdpSocket(&v_right);
dn_drop_r9_UdpSocket(&v_left);
dn_drop_r16_TransportSession(&v_server);
dn_drop_r9_TcpSocket(&v_raw);
dn_drop_r16_TransportSession(&v_client);
dn_drop_r11_TcpListener(&v_listener);
dn_drop_resource_h_net_cancel_dispose(&v_cancel);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_cancel=({ dn_f_network_cancel(); });
(void)(({ bool dn_tmp_2=({ dn_t_resource_h_net_cancel_dispose * const * dn_tmp_3=(&(v_cancel));
dn_f_network_cancel_valid(dn_tmp_3); });
(dn_assert(dn_tmp_2),0); }));
dn_r_TransportBounds v_bounds=({ uint32_t dn_tmp_4=((uint32_t)(1024ULL));
uint32_t dn_tmp_5=((uint32_t)(1ULL));
uint32_t dn_tmp_6=((uint32_t)(4ULL));
size_t dn_tmp_7=((size_t)(8388608ULL));
((dn_r_TransportBounds){dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7}); });
dn_r_TransportRequirements v_req=({ uint32_t dn_tmp_8=((uint32_t)(19ULL));
uint32_t dn_tmp_9=((uint32_t)(0ULL));
uint32_t dn_tmp_10=((uint32_t)(0ULL));
((dn_r_TransportRequirements){dn_tmp_8,dn_tmp_9,dn_tmp_10}); });
v_listener=({ dn_r_SocketAddress dn_tmp_11=({ uint8_t dn_tmp_12=((uint8_t)(127ULL));
uint8_t dn_tmp_13=((uint8_t)(0ULL));
uint8_t dn_tmp_14=((uint8_t)(0ULL));
uint8_t dn_tmp_15=((uint8_t)(1ULL));
uint16_t dn_tmp_16=((uint16_t)(0ULL));
dn_f_socket_address_v4(dn_tmp_12,dn_tmp_13,dn_tmp_14,dn_tmp_15,dn_tmp_16); });
uint32_t dn_tmp_17=((uint32_t)(8ULL));
bool dn_tmp_18=0;
dn_f_tcp_listen(dn_tmp_11,dn_tmp_17,dn_tmp_18); });
dn_r_SocketAddressResult v_local=({ dn_r_TcpListener const * dn_tmp_19=(&(v_listener));
dn_f_listener_local(dn_tmp_19); });
(void)(({ bool dn_tmp_20=({ dn_r_NetworkError const * dn_tmp_21=(&((v_local).f_error));
dn_f_network_ok(dn_tmp_21); });
(dn_assert(dn_tmp_20),0); }));
dn_r_TransportChoice v_choice=({ dn_r_TransportRequirements dn_tmp_22=v_req;
dn_r_TransportBounds dn_tmp_23=v_bounds;
dn_t_arr6_r18_TransportCandidate dn_tmp_24=({ uint32_t dn_tmp_25=((uint32_t)(1ULL));
dn_f_transport_candidates(dn_tmp_25); });
dn_f_transport_select(dn_tmp_22,dn_tmp_23,dn_tmp_24); });
(void)(({ bool dn_tmp_26=({ uint32_t dn_tmp_27=(v_choice).f_profile;
uint32_t dn_tmp_28=((uint32_t)(1ULL));
(dn_tmp_27 == dn_tmp_28); });
(dn_assert(dn_tmp_26),0); }));
v_client=({ dn_r_TransportChoice dn_tmp_29=v_choice;
dn_r_TransportRequirements dn_tmp_30=v_req;
dn_r_TransportBounds dn_tmp_31=v_bounds;
dn_r_TransportTarget dn_tmp_32=({ dn_r_SocketAddress dn_tmp_33=(v_local).f_address;
dn_utf8 dn_tmp_34=((dn_utf8){(const uint8_t*)"", 0});
dn_utf8 dn_tmp_35=((dn_utf8){(const uint8_t*)"", 0});
dn_utf8 dn_tmp_36=((dn_utf8){(const uint8_t*)"", 0});
dn_f_transport_target(dn_tmp_33,dn_tmp_34,dn_tmp_35,dn_tmp_36); });
uint32_t dn_tmp_37=((uint32_t)(1000ULL));
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_38=(&(v_cancel));
dn_f_transport_open(dn_tmp_29,dn_tmp_30,dn_tmp_31,dn_tmp_32,dn_tmp_37,dn_tmp_38); });
dn_r_NetworkError v_ce=({ dn_r_TransportSession const * dn_tmp_39=(&(v_client));
dn_f_transport_error(dn_tmp_39); });
(void)(({ bool dn_tmp_40=({ dn_r_NetworkError const * dn_tmp_41=(&(v_ce));
dn_f_network_ok(dn_tmp_41); });
(dn_assert(dn_tmp_40),0); }));
dn_r_NetworkReady v_accepted=({ dn_r_TcpListener * dn_tmp_42=(&(v_listener));
uint32_t dn_tmp_43=((uint32_t)(1000ULL));
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_44=(&(v_cancel));
dn_f_listener_wait(dn_tmp_42,dn_tmp_43,dn_tmp_44); });
(void)(({ bool dn_tmp_45=({ dn_r_NetworkError const * dn_tmp_46=(&((v_accepted).f_error));
dn_f_network_ok(dn_tmp_46); });
(dn_assert(dn_tmp_45),0); }));
v_raw=({ dn_r_TcpListener * dn_tmp_47=(&(v_listener));
dn_f_tcp_accept(dn_tmp_47); });
v_server=({ dn_r_TcpSocket dn_tmp_48=({ dn_r_TcpSocket dn_tmp_49=v_raw;v_raw=(dn_r_TcpSocket){0};dn_adopt_r9_TcpSocket(&dn_tmp_49);dn_tmp_49; });
dn_adopt_r9_TcpSocket(&dn_tmp_48);
dn_r_TransportRequirements dn_tmp_50=v_req;
dn_r_TransportBounds dn_tmp_51=v_bounds;
dn_f_transport_adopt_tcp(dn_tmp_48,dn_tmp_50,dn_tmp_51); });
dn_r_NetworkError v_se=({ dn_r_TransportSession const * dn_tmp_52=(&(v_server));
dn_f_transport_error(dn_tmp_52); });
(void)(({ bool dn_tmp_53=({ dn_r_NetworkError const * dn_tmp_54=(&(v_se));
dn_f_network_ok(dn_tmp_54); });
(dn_assert(dn_tmp_53),0); }));
(void)(({ dn_r_TransportSession * dn_tmp_55=(&(v_client));
dn_r_TransportSession * dn_tmp_56=(&(v_server));
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_57=(&(v_cancel));
dn_f_exchange(dn_tmp_55,dn_tmp_56,dn_tmp_57); }));
(void)(({ dn_r_TransportSession dn_tmp_58=({ dn_r_TransportSession dn_tmp_59=v_client;v_client=(dn_r_TransportSession){0};dn_adopt_r16_TransportSession(&dn_tmp_59);dn_tmp_59; });
dn_adopt_r16_TransportSession(&dn_tmp_58);
dn_f_transport_close(dn_tmp_58); }));
(void)(({ dn_r_TransportSession dn_tmp_60=({ dn_r_TransportSession dn_tmp_61=v_server;v_server=(dn_r_TransportSession){0};dn_adopt_r16_TransportSession(&dn_tmp_61);dn_tmp_61; });
dn_adopt_r16_TransportSession(&dn_tmp_60);
dn_f_transport_close(dn_tmp_60); }));
(void)(({ dn_r_TcpListener dn_tmp_62=({ dn_r_TcpListener dn_tmp_63=v_listener;v_listener=(dn_r_TcpListener){0};dn_adopt_r11_TcpListener(&dn_tmp_63);dn_tmp_63; });
dn_adopt_r11_TcpListener(&dn_tmp_62);
dn_f_listener_close(dn_tmp_62); }));
v_left=({ dn_r_SocketAddress dn_tmp_64=({ uint8_t dn_tmp_65=((uint8_t)(127ULL));
uint8_t dn_tmp_66=((uint8_t)(0ULL));
uint8_t dn_tmp_67=((uint8_t)(0ULL));
uint8_t dn_tmp_68=((uint8_t)(1ULL));
uint16_t dn_tmp_69=((uint16_t)(0ULL));
dn_f_socket_address_v4(dn_tmp_65,dn_tmp_66,dn_tmp_67,dn_tmp_68,dn_tmp_69); });
bool dn_tmp_70=0;
dn_f_udp_bind(dn_tmp_64,dn_tmp_70); });
v_right=({ dn_r_SocketAddress dn_tmp_71=({ uint8_t dn_tmp_72=((uint8_t)(127ULL));
uint8_t dn_tmp_73=((uint8_t)(0ULL));
uint8_t dn_tmp_74=((uint8_t)(0ULL));
uint8_t dn_tmp_75=((uint8_t)(1ULL));
uint16_t dn_tmp_76=((uint16_t)(0ULL));
dn_f_socket_address_v4(dn_tmp_72,dn_tmp_73,dn_tmp_74,dn_tmp_75,dn_tmp_76); });
bool dn_tmp_77=0;
dn_f_udp_bind(dn_tmp_71,dn_tmp_77); });
dn_r_SocketAddressResult v_la=({ dn_r_UdpSocket const * dn_tmp_78=(&(v_left));
dn_f_udp_local(dn_tmp_78); });
dn_r_SocketAddressResult v_ra=({ dn_r_UdpSocket const * dn_tmp_79=(&(v_right));
dn_f_udp_local(dn_tmp_79); });
(void)(({ bool dn_tmp_80=(({ dn_r_NetworkError const * dn_tmp_81=(&((v_la).f_error));
dn_f_network_ok(dn_tmp_81); }) && ({ dn_r_NetworkError const * dn_tmp_82=(&((v_ra).f_error));
dn_f_network_ok(dn_tmp_82); }));
(dn_assert(dn_tmp_80),0); }));
dn_r_TransportRequirements v_datagrams=({ uint32_t dn_tmp_83=((uint32_t)(80ULL));
uint32_t dn_tmp_84=((uint32_t)(0ULL));
uint32_t dn_tmp_85=((uint32_t)(0ULL));
((dn_r_TransportRequirements){dn_tmp_83,dn_tmp_84,dn_tmp_85}); });
v_uc=({ dn_r_UdpSocket dn_tmp_86=({ dn_r_UdpSocket dn_tmp_87=v_left;v_left=(dn_r_UdpSocket){0};dn_adopt_r9_UdpSocket(&dn_tmp_87);dn_tmp_87; });
dn_adopt_r9_UdpSocket(&dn_tmp_86);
dn_r_SocketAddress dn_tmp_88=(v_ra).f_address;
dn_r_TransportRequirements dn_tmp_89=v_datagrams;
dn_r_TransportBounds dn_tmp_90=v_bounds;
dn_f_transport_adopt_udp(dn_tmp_86,dn_tmp_88,dn_tmp_89,dn_tmp_90); });
v_us=({ dn_r_UdpSocket dn_tmp_91=({ dn_r_UdpSocket dn_tmp_92=v_right;v_right=(dn_r_UdpSocket){0};dn_adopt_r9_UdpSocket(&dn_tmp_92);dn_tmp_92; });
dn_adopt_r9_UdpSocket(&dn_tmp_91);
dn_r_SocketAddress dn_tmp_93=(v_la).f_address;
dn_r_TransportRequirements dn_tmp_94=v_datagrams;
dn_r_TransportBounds dn_tmp_95=v_bounds;
dn_f_transport_adopt_udp(dn_tmp_91,dn_tmp_93,dn_tmp_94,dn_tmp_95); });
(void)(({ dn_r_TransportSession * dn_tmp_96=(&(v_uc));
dn_r_TransportSession * dn_tmp_97=(&(v_us));
dn_t_resource_h_net_cancel_dispose * const * dn_tmp_98=(&(v_cancel));
dn_f_exchange(dn_tmp_96,dn_tmp_97,dn_tmp_98); }));
(void)(({ dn_r_TransportSession dn_tmp_99=({ dn_r_TransportSession dn_tmp_100=v_uc;v_uc=(dn_r_TransportSession){0};dn_adopt_r16_TransportSession(&dn_tmp_100);dn_tmp_100; });
dn_adopt_r16_TransportSession(&dn_tmp_99);
dn_f_transport_close(dn_tmp_99); }));
(void)(({ dn_r_TransportSession dn_tmp_101=({ dn_r_TransportSession dn_tmp_102=v_us;v_us=(dn_r_TransportSession){0};dn_adopt_r16_TransportSession(&dn_tmp_102);dn_tmp_102; });
dn_adopt_r16_TransportSession(&dn_tmp_101);
dn_f_transport_close(dn_tmp_101); }));
(void)(({ dn_t_resource_h_net_cancel_dispose * dn_tmp_103=({ dn_t_resource_h_net_cancel_dispose * dn_tmp_104=v_cancel;v_cancel=(dn_t_resource_h_net_cancel_dispose *){0};dn_adopt_resource_h_net_cancel_dispose(&dn_tmp_104);dn_tmp_104; });
dn_adopt_resource_h_net_cancel_dispose(&dn_tmp_103);
({ dn_drop_resource_h_net_cancel_dispose(&dn_tmp_103);0; }); }));
{
dn_r_NetIo v_stopped=({ dn_f_network_provider_shutdown(); });
(void)(({ bool dn_tmp_105=({ int32_t dn_tmp_106=(v_stopped).f_status;
int32_t dn_tmp_107=((int32_t)(0ULL));
(dn_tmp_106 == dn_tmp_107); });
(dn_assert(dn_tmp_105),0); }));
}
(void)(({ dn_utf8 dn_tmp_108=((dn_utf8){(const uint8_t*)"transport TCP UDP messages policy retention and backpressure OK", 63});
(fwrite(dn_tmp_108.data,1,dn_tmp_108.len,stdout),fputc('\n',stdout),0); }));
dn_drop_r16_TransportSession(&v_us);
dn_drop_r16_TransportSession(&v_uc);
dn_drop_r9_UdpSocket(&v_right);
dn_drop_r9_UdpSocket(&v_left);
dn_drop_r16_TransportSession(&v_server);
dn_drop_r9_TcpSocket(&v_raw);
dn_drop_r16_TransportSession(&v_client);
dn_drop_r11_TcpListener(&v_listener);
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
DN_EXPORT size_t dlm_native_sizeof_TransportRequirements(void){return 12;}
DN_EXPORT size_t dlm_native_alignof_TransportRequirements(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_TransportRequirements(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TransportRequirements_required(void){return 0;}
DN_EXPORT size_t dlm_native_offset_TransportRequirements_forbidden(void){return 4;}
DN_EXPORT size_t dlm_native_offset_TransportRequirements_preferred(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_TransportBounds(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_TransportBounds(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_TransportBounds(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TransportBounds_max_message(void){return 0;}
DN_EXPORT size_t dlm_native_offset_TransportBounds_queued_messages(void){return 4;}
DN_EXPORT size_t dlm_native_offset_TransportBounds_channels(void){return 8;}
DN_EXPORT size_t dlm_native_offset_TransportBounds_memory(void){return 16;}
DN_EXPORT size_t dlm_native_sizeof_TransportCandidate(void){return 12;}
DN_EXPORT size_t dlm_native_alignof_TransportCandidate(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_TransportCandidate(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TransportCandidate_profile(void){return 0;}
DN_EXPORT size_t dlm_native_offset_TransportCandidate_available(void){return 4;}
DN_EXPORT size_t dlm_native_offset_TransportCandidate_cost(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_TransportCheck(void){return 16;}
DN_EXPORT size_t dlm_native_alignof_TransportCheck(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_TransportCheck(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TransportCheck_properties(void){return 0;}
DN_EXPORT size_t dlm_native_offset_TransportCheck_missing(void){return 4;}
DN_EXPORT size_t dlm_native_offset_TransportCheck_forbidden(void){return 8;}
DN_EXPORT size_t dlm_native_offset_TransportCheck_status(void){return 12;}
DN_EXPORT size_t dlm_native_sizeof_TransportChoice(void){return 28;}
DN_EXPORT size_t dlm_native_alignof_TransportChoice(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_TransportChoice(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TransportChoice_profile(void){return 0;}
DN_EXPORT size_t dlm_native_offset_TransportChoice_properties(void){return 4;}
DN_EXPORT size_t dlm_native_offset_TransportChoice_missing(void){return 8;}
DN_EXPORT size_t dlm_native_offset_TransportChoice_forbidden(void){return 12;}
DN_EXPORT size_t dlm_native_offset_TransportChoice_preference_misses(void){return 16;}
DN_EXPORT size_t dlm_native_offset_TransportChoice_cost(void){return 20;}
DN_EXPORT size_t dlm_native_offset_TransportChoice_status(void){return 24;}
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
DN_EXPORT size_t dlm_native_sizeof_QuicEndpoint(void){return 8;}
DN_EXPORT size_t dlm_native_alignof_QuicEndpoint(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_QuicEndpoint(void){return 1;}
DN_EXPORT size_t dlm_native_offset_QuicEndpoint_inner(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_QuicConnection(void){return 8;}
DN_EXPORT size_t dlm_native_alignof_QuicConnection(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_QuicConnection(void){return 1;}
DN_EXPORT size_t dlm_native_offset_QuicConnection_id(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_QuicStream(void){return 16;}
DN_EXPORT size_t dlm_native_alignof_QuicStream(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_QuicStream(void){return 1;}
DN_EXPORT size_t dlm_native_offset_QuicStream_connection(void){return 0;}
DN_EXPORT size_t dlm_native_offset_QuicStream_id(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_QuicConnectionResult(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_QuicConnectionResult(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_QuicConnectionResult(void){return 1;}
DN_EXPORT size_t dlm_native_offset_QuicConnectionResult_connection(void){return 0;}
DN_EXPORT size_t dlm_native_offset_QuicConnectionResult_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_QuicStreamResult(void){return 32;}
DN_EXPORT size_t dlm_native_alignof_QuicStreamResult(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_QuicStreamResult(void){return 1;}
DN_EXPORT size_t dlm_native_offset_QuicStreamResult_stream(void){return 0;}
DN_EXPORT size_t dlm_native_offset_QuicStreamResult_error(void){return 16;}
DN_EXPORT size_t dlm_native_sizeof_QuicConfig(void){return 40;}
DN_EXPORT size_t dlm_native_alignof_QuicConfig(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_QuicConfig(void){return 1;}
DN_EXPORT size_t dlm_native_offset_QuicConfig_connections(void){return 0;}
DN_EXPORT size_t dlm_native_offset_QuicConfig_streams(void){return 4;}
DN_EXPORT size_t dlm_native_offset_QuicConfig_events(void){return 8;}
DN_EXPORT size_t dlm_native_offset_QuicConfig_idle_ms(void){return 12;}
DN_EXPORT size_t dlm_native_offset_QuicConfig_handshake_ms(void){return 16;}
DN_EXPORT size_t dlm_native_offset_QuicConfig_congestion(void){return 20;}
DN_EXPORT size_t dlm_native_offset_QuicConfig_memory(void){return 24;}
DN_EXPORT size_t dlm_native_offset_QuicConfig_window(void){return 32;}
DN_EXPORT size_t dlm_native_sizeof_QuicNativeReference(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_QuicNativeReference(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_QuicNativeReference(void){return 1;}
DN_EXPORT size_t dlm_native_offset_QuicNativeReference_connection(void){return 0;}
DN_EXPORT size_t dlm_native_offset_QuicNativeReference_stream(void){return 8;}
DN_EXPORT size_t dlm_native_offset_QuicNativeReference_status(void){return 16;}
DN_EXPORT size_t dlm_native_offset_QuicNativeReference_system(void){return 20;}
DN_EXPORT size_t dlm_native_sizeof_QuicNativeEvent(void){return 64;}
DN_EXPORT size_t dlm_native_alignof_QuicNativeEvent(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_QuicNativeEvent(void){return 1;}
DN_EXPORT size_t dlm_native_offset_QuicNativeEvent_id(void){return 0;}
DN_EXPORT size_t dlm_native_offset_QuicNativeEvent_connection(void){return 8;}
DN_EXPORT size_t dlm_native_offset_QuicNativeEvent_stream(void){return 16;}
DN_EXPORT size_t dlm_native_offset_QuicNativeEvent_offset(void){return 24;}
DN_EXPORT size_t dlm_native_offset_QuicNativeEvent_code(void){return 32;}
DN_EXPORT size_t dlm_native_offset_QuicNativeEvent_count(void){return 40;}
DN_EXPORT size_t dlm_native_offset_QuicNativeEvent_kind(void){return 48;}
DN_EXPORT size_t dlm_native_offset_QuicNativeEvent_flags(void){return 52;}
DN_EXPORT size_t dlm_native_offset_QuicNativeEvent_status(void){return 56;}
DN_EXPORT size_t dlm_native_offset_QuicNativeEvent_system(void){return 60;}
DN_EXPORT size_t dlm_native_sizeof_QuicEvent(void){return 72;}
DN_EXPORT size_t dlm_native_alignof_QuicEvent(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_QuicEvent(void){return 1;}
DN_EXPORT size_t dlm_native_offset_QuicEvent_id(void){return 0;}
DN_EXPORT size_t dlm_native_offset_QuicEvent_stream(void){return 8;}
DN_EXPORT size_t dlm_native_offset_QuicEvent_offset(void){return 24;}
DN_EXPORT size_t dlm_native_offset_QuicEvent_code(void){return 32;}
DN_EXPORT size_t dlm_native_offset_QuicEvent_count(void){return 40;}
DN_EXPORT size_t dlm_native_offset_QuicEvent_kind(void){return 48;}
DN_EXPORT size_t dlm_native_offset_QuicEvent_flags(void){return 52;}
DN_EXPORT size_t dlm_native_offset_QuicEvent_error(void){return 56;}
DN_EXPORT size_t dlm_native_sizeof_QuicNativeStatistics(void){return 96;}
DN_EXPORT size_t dlm_native_alignof_QuicNativeStatistics(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_QuicNativeStatistics(void){return 1;}
DN_EXPORT size_t dlm_native_offset_QuicNativeStatistics_used(void){return 0;}
DN_EXPORT size_t dlm_native_offset_QuicNativeStatistics_limit(void){return 8;}
DN_EXPORT size_t dlm_native_offset_QuicNativeStatistics_sent(void){return 16;}
DN_EXPORT size_t dlm_native_offset_QuicNativeStatistics_received(void){return 24;}
DN_EXPORT size_t dlm_native_offset_QuicNativeStatistics_lost(void){return 32;}
DN_EXPORT size_t dlm_native_offset_QuicNativeStatistics_cwnd(void){return 40;}
DN_EXPORT size_t dlm_native_offset_QuicNativeStatistics_in_flight(void){return 48;}
DN_EXPORT size_t dlm_native_offset_QuicNativeStatistics_rtt_ns(void){return 56;}
DN_EXPORT size_t dlm_native_offset_QuicNativeStatistics_connections(void){return 64;}
DN_EXPORT size_t dlm_native_offset_QuicNativeStatistics_streams(void){return 68;}
DN_EXPORT size_t dlm_native_offset_QuicNativeStatistics_events(void){return 72;}
DN_EXPORT size_t dlm_native_offset_QuicNativeStatistics_state(void){return 76;}
DN_EXPORT size_t dlm_native_offset_QuicNativeStatistics_version(void){return 80;}
DN_EXPORT size_t dlm_native_offset_QuicNativeStatistics_status(void){return 84;}
DN_EXPORT size_t dlm_native_offset_QuicNativeStatistics_system(void){return 88;}
DN_EXPORT size_t dlm_native_sizeof_QuicStatistics(void){return 96;}
DN_EXPORT size_t dlm_native_alignof_QuicStatistics(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_QuicStatistics(void){return 1;}
DN_EXPORT size_t dlm_native_offset_QuicStatistics_used(void){return 0;}
DN_EXPORT size_t dlm_native_offset_QuicStatistics_limit(void){return 8;}
DN_EXPORT size_t dlm_native_offset_QuicStatistics_sent(void){return 16;}
DN_EXPORT size_t dlm_native_offset_QuicStatistics_received(void){return 24;}
DN_EXPORT size_t dlm_native_offset_QuicStatistics_lost(void){return 32;}
DN_EXPORT size_t dlm_native_offset_QuicStatistics_cwnd(void){return 40;}
DN_EXPORT size_t dlm_native_offset_QuicStatistics_in_flight(void){return 48;}
DN_EXPORT size_t dlm_native_offset_QuicStatistics_rtt_ns(void){return 56;}
DN_EXPORT size_t dlm_native_offset_QuicStatistics_connections(void){return 64;}
DN_EXPORT size_t dlm_native_offset_QuicStatistics_streams(void){return 68;}
DN_EXPORT size_t dlm_native_offset_QuicStatistics_events(void){return 72;}
DN_EXPORT size_t dlm_native_offset_QuicStatistics_state(void){return 76;}
DN_EXPORT size_t dlm_native_offset_QuicStatistics_version(void){return 80;}
DN_EXPORT size_t dlm_native_offset_QuicStatistics_error(void){return 84;}
DN_EXPORT size_t dlm_native_sizeof_TransportSession(void){return 8;}
DN_EXPORT size_t dlm_native_alignof_TransportSession(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_TransportSession(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TransportSession_inner(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_TransportTarget(void){return 104;}
DN_EXPORT size_t dlm_native_alignof_TransportTarget(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_TransportTarget(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TransportTarget_local(void){return 0;}
DN_EXPORT size_t dlm_native_offset_TransportTarget_peer(void){return 28;}
DN_EXPORT size_t dlm_native_offset_TransportTarget_identity(void){return 56;}
DN_EXPORT size_t dlm_native_offset_TransportTarget_ca_file(void){return 72;}
DN_EXPORT size_t dlm_native_offset_TransportTarget_pin(void){return 88;}
DN_EXPORT size_t dlm_native_sizeof_TransportNativeTarget(void){return 120;}
DN_EXPORT size_t dlm_native_alignof_TransportNativeTarget(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_TransportNativeTarget(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TransportNativeTarget_local(void){return 0;}
DN_EXPORT size_t dlm_native_offset_TransportNativeTarget_peer(void){return 28;}
DN_EXPORT size_t dlm_native_offset_TransportNativeTarget_identity(void){return 56;}
DN_EXPORT size_t dlm_native_offset_TransportNativeTarget_ca(void){return 72;}
DN_EXPORT size_t dlm_native_offset_TransportNativeTarget_pin(void){return 88;}
DN_EXPORT size_t dlm_native_offset_TransportNativeTarget_alpn(void){return 104;}
DN_EXPORT size_t dlm_native_sizeof_TransportNativeMessage(void){return 32;}
DN_EXPORT size_t dlm_native_alignof_TransportNativeMessage(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_TransportNativeMessage(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TransportNativeMessage_id(void){return 0;}
DN_EXPORT size_t dlm_native_offset_TransportNativeMessage_channel(void){return 8;}
DN_EXPORT size_t dlm_native_offset_TransportNativeMessage_count(void){return 16;}
DN_EXPORT size_t dlm_native_offset_TransportNativeMessage_status(void){return 24;}
DN_EXPORT size_t dlm_native_offset_TransportNativeMessage_system(void){return 28;}
DN_EXPORT size_t dlm_native_sizeof_TransportNativeDelivery(void){return 40;}
DN_EXPORT size_t dlm_native_alignof_TransportNativeDelivery(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_TransportNativeDelivery(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TransportNativeDelivery_id(void){return 0;}
DN_EXPORT size_t dlm_native_offset_TransportNativeDelivery_channel(void){return 8;}
DN_EXPORT size_t dlm_native_offset_TransportNativeDelivery_copied(void){return 16;}
DN_EXPORT size_t dlm_native_offset_TransportNativeDelivery_required(void){return 24;}
DN_EXPORT size_t dlm_native_offset_TransportNativeDelivery_status(void){return 32;}
DN_EXPORT size_t dlm_native_offset_TransportNativeDelivery_system(void){return 36;}
DN_EXPORT size_t dlm_native_sizeof_TransportNativeStatistics(void){return 64;}
DN_EXPORT size_t dlm_native_alignof_TransportNativeStatistics(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_TransportNativeStatistics(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TransportNativeStatistics_used(void){return 0;}
DN_EXPORT size_t dlm_native_offset_TransportNativeStatistics_limit(void){return 8;}
DN_EXPORT size_t dlm_native_offset_TransportNativeStatistics_adapter_limit(void){return 16;}
DN_EXPORT size_t dlm_native_offset_TransportNativeStatistics_backend_limit(void){return 24;}
DN_EXPORT size_t dlm_native_offset_TransportNativeStatistics_profile(void){return 32;}
DN_EXPORT size_t dlm_native_offset_TransportNativeStatistics_properties(void){return 36;}
DN_EXPORT size_t dlm_native_offset_TransportNativeStatistics_state(void){return 40;}
DN_EXPORT size_t dlm_native_offset_TransportNativeStatistics_send_queued(void){return 44;}
DN_EXPORT size_t dlm_native_offset_TransportNativeStatistics_receive_queued(void){return 48;}
DN_EXPORT size_t dlm_native_offset_TransportNativeStatistics_channels(void){return 52;}
DN_EXPORT size_t dlm_native_offset_TransportNativeStatistics_status(void){return 56;}
DN_EXPORT size_t dlm_native_offset_TransportNativeStatistics_system(void){return 60;}
DN_EXPORT size_t dlm_native_sizeof_TransportMessage(void){return 40;}
DN_EXPORT size_t dlm_native_alignof_TransportMessage(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_TransportMessage(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TransportMessage_id(void){return 0;}
DN_EXPORT size_t dlm_native_offset_TransportMessage_channel(void){return 8;}
DN_EXPORT size_t dlm_native_offset_TransportMessage_count(void){return 16;}
DN_EXPORT size_t dlm_native_offset_TransportMessage_error(void){return 24;}
DN_EXPORT size_t dlm_native_sizeof_TransportDelivery(void){return 48;}
DN_EXPORT size_t dlm_native_alignof_TransportDelivery(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_TransportDelivery(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TransportDelivery_id(void){return 0;}
DN_EXPORT size_t dlm_native_offset_TransportDelivery_channel(void){return 8;}
DN_EXPORT size_t dlm_native_offset_TransportDelivery_copied(void){return 16;}
DN_EXPORT size_t dlm_native_offset_TransportDelivery_required(void){return 24;}
DN_EXPORT size_t dlm_native_offset_TransportDelivery_error(void){return 32;}
DN_EXPORT size_t dlm_native_sizeof_TransportStatistics(void){return 72;}
DN_EXPORT size_t dlm_native_alignof_TransportStatistics(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_TransportStatistics(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TransportStatistics_used(void){return 0;}
DN_EXPORT size_t dlm_native_offset_TransportStatistics_limit(void){return 8;}
DN_EXPORT size_t dlm_native_offset_TransportStatistics_adapter_limit(void){return 16;}
DN_EXPORT size_t dlm_native_offset_TransportStatistics_backend_limit(void){return 24;}
DN_EXPORT size_t dlm_native_offset_TransportStatistics_profile(void){return 32;}
DN_EXPORT size_t dlm_native_offset_TransportStatistics_properties(void){return 36;}
DN_EXPORT size_t dlm_native_offset_TransportStatistics_state(void){return 40;}
DN_EXPORT size_t dlm_native_offset_TransportStatistics_send_queued(void){return 44;}
DN_EXPORT size_t dlm_native_offset_TransportStatistics_receive_queued(void){return 48;}
DN_EXPORT size_t dlm_native_offset_TransportStatistics_channels(void){return 52;}
DN_EXPORT size_t dlm_native_offset_TransportStatistics_error(void){return 56;}
int main(void){(void)dlm_native_thread_init();dn_boundary b={0};jmp_buf dn_jump;b.jump=&dn_jump;b.foreign_marker=dn_foreign_context;dn_current=&b;dn_fp_begin(&b);if(setjmp(dn_jump)){dn_boundary_end(&b,1);fprintf(stderr,"DLM native error: %s\n",dn_error);return 1;}(void)dn_f_main();dn_boundary_end(&b,0);(void)dlm_native_runtime_shutdown();return 0;}
