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
typedef struct dn_r_Stereo dn_r_Stereo;
typedef struct dn_r_Fir dn_r_Fir;
typedef struct dn_r_Biquad dn_r_Biquad;
typedef struct dn_r_Allpass dn_r_Allpass;
typedef struct dn_r_Creative dn_r_Creative;
typedef struct dn_r_Pulse dn_r_Pulse;
typedef struct dn_r_Dac dn_r_Dac;
typedef struct dn_r_OutputStage dn_r_OutputStage;
typedef struct dn_r_TubeDef dn_r_TubeDef;
typedef struct dn_r_ValveChannel dn_r_ValveChannel;
typedef struct dn_r_Tube dn_r_Tube;
typedef struct dn_r_BandChannel dn_r_BandChannel;
typedef struct dn_r_ThreeBand dn_r_ThreeBand;
typedef struct dn_r_Folded dn_r_Folded;
typedef struct dn_r_Engine dn_r_Engine;
struct dn_r_Allpass {
    double f_input;
    double f_output;
};
struct dn_r_Biquad {
    double f_b0;
    double f_b1;
    double f_b2;
    double f_a1;
    double f_a2;
    double f_z1;
    double f_z2;
};
typedef struct dn_t_arr8_r6_Biquad { dn_r_Biquad data[8]; } dn_t_arr8_r6_Biquad;
struct dn_r_BandChannel {
    dn_t_arr8_r6_Biquad f_filters;
    double f_currentLower;
    double f_currentUpper;
    int32_t f_coefficientCounter;
};
typedef struct dn_t_arr3_F64 { double data[3]; } dn_t_arr3_F64;
typedef struct dn_t_arr2_F64 { double data[2]; } dn_t_arr2_F64;
typedef struct dn_t_arr4_r7_Allpass { dn_r_Allpass data[4]; } dn_t_arr4_r7_Allpass;
struct dn_r_Creative {
    dn_t_arr3_F64 f_amounts;
    dn_t_arr2_F64 f_focusState;
    dn_t_arr2_F64 f_warmState;
    dn_t_arr2_F64 f_hfState;
    dn_t_arr4_r7_Allpass f_allpass;
    double f_smoothing;
    double f_focusPole;
    double f_warmPole;
    double f_hfPole;
    double f_ap0;
    double f_ap1;
};
typedef struct dn_t_arr12_F64 { double data[12]; } dn_t_arr12_F64;
typedef struct dn_t_arr11_F64 { double data[11]; } dn_t_arr11_F64;
typedef struct dn_t_arr110_F64 { double data[110]; } dn_t_arr110_F64;
typedef struct dn_t_arr100_F64 { double data[100]; } dn_t_arr100_F64;
struct dn_r_Dac {
    dn_t_arr12_F64 f_ntf;
    dn_t_arr11_F64 f_errors;
    dn_t_arr110_F64 f_prefixes;
    dn_t_arr100_F64 f_gains;
    size_t f_order;
    size_t f_mode;
    size_t f_rotation;
    uint32_t f_rng;
    uint32_t f_seed;
};
typedef struct dn_t_buf_r3_Fir { dn_r_Fir *data;size_t len,cap; } dn_t_buf_r3_Fir;
typedef struct dn_t_arr157_F64 { double data[157]; } dn_t_arr157_F64;
typedef struct dn_t_arr63_F64 { double data[63]; } dn_t_arr63_F64;
typedef struct dn_t_arr64_F64 { double data[64]; } dn_t_arr64_F64;
struct dn_r_Pulse {
    dn_t_arr12_F64 f_ntf;
    dn_t_arr11_F64 f_errors;
    dn_t_arr110_F64 f_prefixes;
    dn_t_arr100_F64 f_gains;
    size_t f_order;
    size_t f_elements;
    size_t f_position;
    uint32_t f_rng;
    uint32_t f_seed;
    double f_totalGain;
};
typedef struct dn_t_arr2_r5_Pulse { dn_r_Pulse data[2]; } dn_t_arr2_r5_Pulse;
typedef struct dn_t_arr2_r3_Dac { dn_r_Dac data[2]; } dn_t_arr2_r3_Dac;
typedef struct dn_t_arr256_F64 { double data[256]; } dn_t_arr256_F64;
typedef struct dn_t_arr128_F64 { double data[128]; } dn_t_arr128_F64;
typedef struct dn_t_arr68_F64 { double data[68]; } dn_t_arr68_F64;
struct dn_r_OutputStage {
    dn_t_arr256_F64 f_signal;
    dn_t_arr128_F64 f_peakHistory;
    dn_t_arr68_F64 f_kernels;
    size_t f_index;
    bool f_limiterPreviouslyEnabled;
    double f_inputPower;
    double f_processedPower;
    double f_autoGain;
    double f_limiterGain;
    double f_powerAlpha;
    double f_matchAlpha;
    double f_releaseAlpha;
    double f_estimatedPeak;
    double f_reductionDb;
    double f_autoGainDb;
};
typedef struct dn_t_buf_F32 { float *data;size_t len,cap; } dn_t_buf_F32;
struct dn_r_ValveChannel {
    size_t f_oldType;
    size_t f_newType;
    size_t f_oldTopology;
    size_t f_newTopology;
    double f_transition;
    double f_drive;
    double f_bias;
    double f_sag;
    double f_wet;
    double f_envelope;
};
typedef struct dn_t_arr2_r12_ValveChannel { dn_r_ValveChannel data[2]; } dn_t_arr2_r12_ValveChannel;
struct dn_r_Tube {
    dn_t_buf_F32 f_curves;
    dn_t_arr2_r12_ValveChannel f_channels;
    size_t f_desiredType;
    size_t f_desiredTopology;
    double f_desiredDrive;
    double f_desiredBias;
    double f_desiredSag;
    double f_desiredWet;
    double f_smoothing;
    double f_envelopeAttack;
    double f_envelopeRelease;
    double f_transitionStep;
};
typedef struct dn_t_arr3_r4_Tube { dn_r_Tube data[3]; } dn_t_arr3_r4_Tube;
typedef struct dn_t_arr2_r11_BandChannel { dn_r_BandChannel data[2]; } dn_t_arr2_r11_BandChannel;
typedef struct dn_t_arr2_USize { size_t data[2]; } dn_t_arr2_USize;
typedef struct dn_t_arr3_USize { size_t data[3]; } dn_t_arr3_USize;
struct dn_r_ThreeBand {
    dn_t_arr3_r4_Tube f_stages;
    dn_t_arr2_r11_BandChannel f_channels;
    double f_rate;
    double f_targetLower;
    double f_targetUpper;
    double f_frequencySmoothing;
    bool f_anyActive;
    dn_t_arr2_USize f_releaseFrames;
    dn_t_arr3_USize f_types;
    dn_t_arr3_F64 f_wets;
};
typedef struct dn_t_arr2304_F64 { double data[2304]; } dn_t_arr2304_F64;
typedef struct dn_t_arr6_USize { size_t data[6]; } dn_t_arr6_USize;
typedef struct dn_t_arr1536_F64 { double data[1536]; } dn_t_arr1536_F64;
struct dn_r_Folded {
    dn_t_arr2304_F64 f_coefficients;
    dn_t_arr6_USize f_lengths;
    dn_t_arr1536_F64 f_history;
    size_t f_index;
};
struct dn_r_Engine {
    dn_t_buf_r3_Fir f_up;
    dn_t_buf_r3_Fir f_down;
    dn_t_arr157_F64 f_dryL;
    dn_t_arr157_F64 f_dryR;
    dn_t_arr63_F64 f_alignedL;
    dn_t_arr63_F64 f_alignedR;
    dn_t_arr64_F64 f_delayL;
    dn_t_arr64_F64 f_delayR;
    size_t f_dryIndex;
    size_t f_alignIndex;
    size_t f_crossIndex;
    double f_crossLowL;
    double f_crossLowR;
    double f_crossAlpha;
    size_t f_crossSamples;
    double f_crossCurrent;
    double f_mixCurrent;
    double f_gainCurrent;
    double f_controlAlpha;
    double f_warmLowL;
    double f_warmLowR;
    double f_warmAlpha;
    double f_warmMix;
    double f_hfMix;
    bool f_hfAvailable;
    dn_r_Biquad f_hfL;
    dn_r_Biquad f_hfR;
    dn_r_Creative f_creative;
    dn_t_arr2_r5_Pulse f_pulses;
    dn_t_arr2_r3_Dac f_dacs;
    size_t f_pulseMode;
    size_t f_dacMode;
    double f_mismatch;
    double f_characterCurrent;
    double f_pulseColorCurrent;
    size_t f_depth;
    double f_sampleRate;
    dn_r_OutputStage f_output;
    dn_r_Tube f_tube;
    bool f_tubeActive;
    dn_r_ThreeBand f_threeBand;
    size_t f_tubeRouting;
    dn_t_arr2_F64 f_tubeRouteBlend;
    double f_tubeRouteStep;
    size_t f_pendingDepth;
    size_t f_pendingPulse;
    size_t f_pendingDac;
    size_t f_transitionCounter;
    bool f_transitioning;
    dn_r_Folded f_folded;
    bool f_fastEnabled;
    bool f_foldedHistoryActive;
    size_t f_foldedPriming;
    size_t f_legacyPriming;
    bool f_legacyRunning;
    double f_fastWeight;
    size_t f_foldedDepth;
    size_t f_previousFoldedDepth;
    size_t f_foldedFade;
    size_t f_tubeCpuMode;
};
typedef struct dn_t_buf_F64 { double *data;size_t len,cap; } dn_t_buf_F64;
struct dn_r_Fir {
    dn_t_buf_F64 f_taps;
    dn_t_buf_F64 f_history;
    size_t f_index;
    size_t f_size;
};
struct dn_r_Stereo {
    double f_left;
    double f_right;
};
struct dn_r_TubeDef {
    double f_mu;
    double f_ex;
    double f_kg1;
    double f_kp;
    double f_kvb;
    double f_plate;
    double f_grid;
    double f_load;
    double f_screen;
    double f_swing;
    bool f_pentode;
};
typedef struct dn_t_arr384_F64 { double data[384]; } dn_t_arr384_F64;
typedef struct dn_t_foreign_F32 { float *data;size_t len; } dn_t_foreign_F32;
_Static_assert(sizeof(dn_r_Stereo)==16,"Stereo ABI size");
_Static_assert(_Alignof(dn_r_Stereo)==8,"Stereo ABI alignment");
_Static_assert(offsetof(dn_r_Stereo,f_left)==0,"Stereo.left ABI offset");
_Static_assert(offsetof(dn_r_Stereo,f_right)==8,"Stereo.right ABI offset");
_Static_assert(sizeof(dn_r_Fir)==64,"Fir ABI size");
_Static_assert(_Alignof(dn_r_Fir)==8,"Fir ABI alignment");
_Static_assert(offsetof(dn_r_Fir,f_taps)==0,"Fir.taps ABI offset");
_Static_assert(offsetof(dn_r_Fir,f_history)==24,"Fir.history ABI offset");
_Static_assert(offsetof(dn_r_Fir,f_index)==48,"Fir.index ABI offset");
_Static_assert(offsetof(dn_r_Fir,f_size)==56,"Fir.size ABI offset");
_Static_assert(sizeof(dn_r_Biquad)==56,"Biquad ABI size");
_Static_assert(_Alignof(dn_r_Biquad)==8,"Biquad ABI alignment");
_Static_assert(offsetof(dn_r_Biquad,f_b0)==0,"Biquad.b0 ABI offset");
_Static_assert(offsetof(dn_r_Biquad,f_b1)==8,"Biquad.b1 ABI offset");
_Static_assert(offsetof(dn_r_Biquad,f_b2)==16,"Biquad.b2 ABI offset");
_Static_assert(offsetof(dn_r_Biquad,f_a1)==24,"Biquad.a1 ABI offset");
_Static_assert(offsetof(dn_r_Biquad,f_a2)==32,"Biquad.a2 ABI offset");
_Static_assert(offsetof(dn_r_Biquad,f_z1)==40,"Biquad.z1 ABI offset");
_Static_assert(offsetof(dn_r_Biquad,f_z2)==48,"Biquad.z2 ABI offset");
_Static_assert(sizeof(dn_r_Allpass)==16,"Allpass ABI size");
_Static_assert(_Alignof(dn_r_Allpass)==8,"Allpass ABI alignment");
_Static_assert(offsetof(dn_r_Allpass,f_input)==0,"Allpass.input ABI offset");
_Static_assert(offsetof(dn_r_Allpass,f_output)==8,"Allpass.output ABI offset");
_Static_assert(sizeof(dn_r_Creative)==184,"Creative ABI size");
_Static_assert(_Alignof(dn_r_Creative)==8,"Creative ABI alignment");
_Static_assert(offsetof(dn_r_Creative,f_amounts)==0,"Creative.amounts ABI offset");
_Static_assert(offsetof(dn_r_Creative,f_focusState)==24,"Creative.focusState ABI offset");
_Static_assert(offsetof(dn_r_Creative,f_warmState)==40,"Creative.warmState ABI offset");
_Static_assert(offsetof(dn_r_Creative,f_hfState)==56,"Creative.hfState ABI offset");
_Static_assert(offsetof(dn_r_Creative,f_allpass)==72,"Creative.allpass ABI offset");
_Static_assert(offsetof(dn_r_Creative,f_smoothing)==136,"Creative.smoothing ABI offset");
_Static_assert(offsetof(dn_r_Creative,f_focusPole)==144,"Creative.focusPole ABI offset");
_Static_assert(offsetof(dn_r_Creative,f_warmPole)==152,"Creative.warmPole ABI offset");
_Static_assert(offsetof(dn_r_Creative,f_hfPole)==160,"Creative.hfPole ABI offset");
_Static_assert(offsetof(dn_r_Creative,f_ap0)==168,"Creative.ap0 ABI offset");
_Static_assert(offsetof(dn_r_Creative,f_ap1)==176,"Creative.ap1 ABI offset");
_Static_assert(sizeof(dn_r_Pulse)==1904,"Pulse ABI size");
_Static_assert(_Alignof(dn_r_Pulse)==8,"Pulse ABI alignment");
_Static_assert(offsetof(dn_r_Pulse,f_ntf)==0,"Pulse.ntf ABI offset");
_Static_assert(offsetof(dn_r_Pulse,f_errors)==96,"Pulse.errors ABI offset");
_Static_assert(offsetof(dn_r_Pulse,f_prefixes)==184,"Pulse.prefixes ABI offset");
_Static_assert(offsetof(dn_r_Pulse,f_gains)==1064,"Pulse.gains ABI offset");
_Static_assert(offsetof(dn_r_Pulse,f_order)==1864,"Pulse.order ABI offset");
_Static_assert(offsetof(dn_r_Pulse,f_elements)==1872,"Pulse.elements ABI offset");
_Static_assert(offsetof(dn_r_Pulse,f_position)==1880,"Pulse.position ABI offset");
_Static_assert(offsetof(dn_r_Pulse,f_rng)==1888,"Pulse.rng ABI offset");
_Static_assert(offsetof(dn_r_Pulse,f_seed)==1892,"Pulse.seed ABI offset");
_Static_assert(offsetof(dn_r_Pulse,f_totalGain)==1896,"Pulse.totalGain ABI offset");
_Static_assert(sizeof(dn_r_Dac)==1896,"Dac ABI size");
_Static_assert(_Alignof(dn_r_Dac)==8,"Dac ABI alignment");
_Static_assert(offsetof(dn_r_Dac,f_ntf)==0,"Dac.ntf ABI offset");
_Static_assert(offsetof(dn_r_Dac,f_errors)==96,"Dac.errors ABI offset");
_Static_assert(offsetof(dn_r_Dac,f_prefixes)==184,"Dac.prefixes ABI offset");
_Static_assert(offsetof(dn_r_Dac,f_gains)==1064,"Dac.gains ABI offset");
_Static_assert(offsetof(dn_r_Dac,f_order)==1864,"Dac.order ABI offset");
_Static_assert(offsetof(dn_r_Dac,f_mode)==1872,"Dac.mode ABI offset");
_Static_assert(offsetof(dn_r_Dac,f_rotation)==1880,"Dac.rotation ABI offset");
_Static_assert(offsetof(dn_r_Dac,f_rng)==1888,"Dac.rng ABI offset");
_Static_assert(offsetof(dn_r_Dac,f_seed)==1892,"Dac.seed ABI offset");
_Static_assert(sizeof(dn_r_OutputStage)==3712,"OutputStage ABI size");
_Static_assert(_Alignof(dn_r_OutputStage)==8,"OutputStage ABI alignment");
_Static_assert(offsetof(dn_r_OutputStage,f_signal)==0,"OutputStage.signal ABI offset");
_Static_assert(offsetof(dn_r_OutputStage,f_peakHistory)==2048,"OutputStage.peakHistory ABI offset");
_Static_assert(offsetof(dn_r_OutputStage,f_kernels)==3072,"OutputStage.kernels ABI offset");
_Static_assert(offsetof(dn_r_OutputStage,f_index)==3616,"OutputStage.index ABI offset");
_Static_assert(offsetof(dn_r_OutputStage,f_limiterPreviouslyEnabled)==3624,"OutputStage.limiterPreviouslyEnabled ABI offset");
_Static_assert(offsetof(dn_r_OutputStage,f_inputPower)==3632,"OutputStage.inputPower ABI offset");
_Static_assert(offsetof(dn_r_OutputStage,f_processedPower)==3640,"OutputStage.processedPower ABI offset");
_Static_assert(offsetof(dn_r_OutputStage,f_autoGain)==3648,"OutputStage.autoGain ABI offset");
_Static_assert(offsetof(dn_r_OutputStage,f_limiterGain)==3656,"OutputStage.limiterGain ABI offset");
_Static_assert(offsetof(dn_r_OutputStage,f_powerAlpha)==3664,"OutputStage.powerAlpha ABI offset");
_Static_assert(offsetof(dn_r_OutputStage,f_matchAlpha)==3672,"OutputStage.matchAlpha ABI offset");
_Static_assert(offsetof(dn_r_OutputStage,f_releaseAlpha)==3680,"OutputStage.releaseAlpha ABI offset");
_Static_assert(offsetof(dn_r_OutputStage,f_estimatedPeak)==3688,"OutputStage.estimatedPeak ABI offset");
_Static_assert(offsetof(dn_r_OutputStage,f_reductionDb)==3696,"OutputStage.reductionDb ABI offset");
_Static_assert(offsetof(dn_r_OutputStage,f_autoGainDb)==3704,"OutputStage.autoGainDb ABI offset");
_Static_assert(sizeof(dn_r_TubeDef)==88,"TubeDef ABI size");
_Static_assert(_Alignof(dn_r_TubeDef)==8,"TubeDef ABI alignment");
_Static_assert(offsetof(dn_r_TubeDef,f_mu)==0,"TubeDef.mu ABI offset");
_Static_assert(offsetof(dn_r_TubeDef,f_ex)==8,"TubeDef.ex ABI offset");
_Static_assert(offsetof(dn_r_TubeDef,f_kg1)==16,"TubeDef.kg1 ABI offset");
_Static_assert(offsetof(dn_r_TubeDef,f_kp)==24,"TubeDef.kp ABI offset");
_Static_assert(offsetof(dn_r_TubeDef,f_kvb)==32,"TubeDef.kvb ABI offset");
_Static_assert(offsetof(dn_r_TubeDef,f_plate)==40,"TubeDef.plate ABI offset");
_Static_assert(offsetof(dn_r_TubeDef,f_grid)==48,"TubeDef.grid ABI offset");
_Static_assert(offsetof(dn_r_TubeDef,f_load)==56,"TubeDef.load ABI offset");
_Static_assert(offsetof(dn_r_TubeDef,f_screen)==64,"TubeDef.screen ABI offset");
_Static_assert(offsetof(dn_r_TubeDef,f_swing)==72,"TubeDef.swing ABI offset");
_Static_assert(offsetof(dn_r_TubeDef,f_pentode)==80,"TubeDef.pentode ABI offset");
_Static_assert(sizeof(dn_r_ValveChannel)==80,"ValveChannel ABI size");
_Static_assert(_Alignof(dn_r_ValveChannel)==8,"ValveChannel ABI alignment");
_Static_assert(offsetof(dn_r_ValveChannel,f_oldType)==0,"ValveChannel.oldType ABI offset");
_Static_assert(offsetof(dn_r_ValveChannel,f_newType)==8,"ValveChannel.newType ABI offset");
_Static_assert(offsetof(dn_r_ValveChannel,f_oldTopology)==16,"ValveChannel.oldTopology ABI offset");
_Static_assert(offsetof(dn_r_ValveChannel,f_newTopology)==24,"ValveChannel.newTopology ABI offset");
_Static_assert(offsetof(dn_r_ValveChannel,f_transition)==32,"ValveChannel.transition ABI offset");
_Static_assert(offsetof(dn_r_ValveChannel,f_drive)==40,"ValveChannel.drive ABI offset");
_Static_assert(offsetof(dn_r_ValveChannel,f_bias)==48,"ValveChannel.bias ABI offset");
_Static_assert(offsetof(dn_r_ValveChannel,f_sag)==56,"ValveChannel.sag ABI offset");
_Static_assert(offsetof(dn_r_ValveChannel,f_wet)==64,"ValveChannel.wet ABI offset");
_Static_assert(offsetof(dn_r_ValveChannel,f_envelope)==72,"ValveChannel.envelope ABI offset");
_Static_assert(sizeof(dn_r_Tube)==264,"Tube ABI size");
_Static_assert(_Alignof(dn_r_Tube)==8,"Tube ABI alignment");
_Static_assert(offsetof(dn_r_Tube,f_curves)==0,"Tube.curves ABI offset");
_Static_assert(offsetof(dn_r_Tube,f_channels)==24,"Tube.channels ABI offset");
_Static_assert(offsetof(dn_r_Tube,f_desiredType)==184,"Tube.desiredType ABI offset");
_Static_assert(offsetof(dn_r_Tube,f_desiredTopology)==192,"Tube.desiredTopology ABI offset");
_Static_assert(offsetof(dn_r_Tube,f_desiredDrive)==200,"Tube.desiredDrive ABI offset");
_Static_assert(offsetof(dn_r_Tube,f_desiredBias)==208,"Tube.desiredBias ABI offset");
_Static_assert(offsetof(dn_r_Tube,f_desiredSag)==216,"Tube.desiredSag ABI offset");
_Static_assert(offsetof(dn_r_Tube,f_desiredWet)==224,"Tube.desiredWet ABI offset");
_Static_assert(offsetof(dn_r_Tube,f_smoothing)==232,"Tube.smoothing ABI offset");
_Static_assert(offsetof(dn_r_Tube,f_envelopeAttack)==240,"Tube.envelopeAttack ABI offset");
_Static_assert(offsetof(dn_r_Tube,f_envelopeRelease)==248,"Tube.envelopeRelease ABI offset");
_Static_assert(offsetof(dn_r_Tube,f_transitionStep)==256,"Tube.transitionStep ABI offset");
_Static_assert(sizeof(dn_r_BandChannel)==472,"BandChannel ABI size");
_Static_assert(_Alignof(dn_r_BandChannel)==8,"BandChannel ABI alignment");
_Static_assert(offsetof(dn_r_BandChannel,f_filters)==0,"BandChannel.filters ABI offset");
_Static_assert(offsetof(dn_r_BandChannel,f_currentLower)==448,"BandChannel.currentLower ABI offset");
_Static_assert(offsetof(dn_r_BandChannel,f_currentUpper)==456,"BandChannel.currentUpper ABI offset");
_Static_assert(offsetof(dn_r_BandChannel,f_coefficientCounter)==464,"BandChannel.coefficientCounter ABI offset");
_Static_assert(sizeof(dn_r_ThreeBand)==1840,"ThreeBand ABI size");
_Static_assert(_Alignof(dn_r_ThreeBand)==8,"ThreeBand ABI alignment");
_Static_assert(offsetof(dn_r_ThreeBand,f_stages)==0,"ThreeBand.stages ABI offset");
_Static_assert(offsetof(dn_r_ThreeBand,f_channels)==792,"ThreeBand.channels ABI offset");
_Static_assert(offsetof(dn_r_ThreeBand,f_rate)==1736,"ThreeBand.rate ABI offset");
_Static_assert(offsetof(dn_r_ThreeBand,f_targetLower)==1744,"ThreeBand.targetLower ABI offset");
_Static_assert(offsetof(dn_r_ThreeBand,f_targetUpper)==1752,"ThreeBand.targetUpper ABI offset");
_Static_assert(offsetof(dn_r_ThreeBand,f_frequencySmoothing)==1760,"ThreeBand.frequencySmoothing ABI offset");
_Static_assert(offsetof(dn_r_ThreeBand,f_anyActive)==1768,"ThreeBand.anyActive ABI offset");
_Static_assert(offsetof(dn_r_ThreeBand,f_releaseFrames)==1776,"ThreeBand.releaseFrames ABI offset");
_Static_assert(offsetof(dn_r_ThreeBand,f_types)==1792,"ThreeBand.types ABI offset");
_Static_assert(offsetof(dn_r_ThreeBand,f_wets)==1816,"ThreeBand.wets ABI offset");
_Static_assert(sizeof(dn_r_Folded)==30776,"Folded ABI size");
_Static_assert(_Alignof(dn_r_Folded)==8,"Folded ABI alignment");
_Static_assert(offsetof(dn_r_Folded,f_coefficients)==0,"Folded.coefficients ABI offset");
_Static_assert(offsetof(dn_r_Folded,f_lengths)==18432,"Folded.lengths ABI offset");
_Static_assert(offsetof(dn_r_Folded,f_history)==18480,"Folded.history ABI offset");
_Static_assert(offsetof(dn_r_Folded,f_index)==30768,"Folded.index ABI offset");
_Static_assert(sizeof(dn_r_Engine)==49424,"Engine ABI size");
_Static_assert(_Alignof(dn_r_Engine)==8,"Engine ABI alignment");
_Static_assert(offsetof(dn_r_Engine,f_up)==0,"Engine.up ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_down)==24,"Engine.down ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_dryL)==48,"Engine.dryL ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_dryR)==1304,"Engine.dryR ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_alignedL)==2560,"Engine.alignedL ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_alignedR)==3064,"Engine.alignedR ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_delayL)==3568,"Engine.delayL ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_delayR)==4080,"Engine.delayR ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_dryIndex)==4592,"Engine.dryIndex ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_alignIndex)==4600,"Engine.alignIndex ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_crossIndex)==4608,"Engine.crossIndex ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_crossLowL)==4616,"Engine.crossLowL ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_crossLowR)==4624,"Engine.crossLowR ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_crossAlpha)==4632,"Engine.crossAlpha ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_crossSamples)==4640,"Engine.crossSamples ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_crossCurrent)==4648,"Engine.crossCurrent ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_mixCurrent)==4656,"Engine.mixCurrent ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_gainCurrent)==4664,"Engine.gainCurrent ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_controlAlpha)==4672,"Engine.controlAlpha ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_warmLowL)==4680,"Engine.warmLowL ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_warmLowR)==4688,"Engine.warmLowR ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_warmAlpha)==4696,"Engine.warmAlpha ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_warmMix)==4704,"Engine.warmMix ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_hfMix)==4712,"Engine.hfMix ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_hfAvailable)==4720,"Engine.hfAvailable ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_hfL)==4728,"Engine.hfL ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_hfR)==4784,"Engine.hfR ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_creative)==4840,"Engine.creative ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_pulses)==5024,"Engine.pulses ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_dacs)==8832,"Engine.dacs ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_pulseMode)==12624,"Engine.pulseMode ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_dacMode)==12632,"Engine.dacMode ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_mismatch)==12640,"Engine.mismatch ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_characterCurrent)==12648,"Engine.characterCurrent ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_pulseColorCurrent)==12656,"Engine.pulseColorCurrent ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_depth)==12664,"Engine.depth ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_sampleRate)==12672,"Engine.sampleRate ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_output)==12680,"Engine.output ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_tube)==16392,"Engine.tube ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_tubeActive)==16656,"Engine.tubeActive ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_threeBand)==16664,"Engine.threeBand ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_tubeRouting)==18504,"Engine.tubeRouting ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_tubeRouteBlend)==18512,"Engine.tubeRouteBlend ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_tubeRouteStep)==18528,"Engine.tubeRouteStep ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_pendingDepth)==18536,"Engine.pendingDepth ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_pendingPulse)==18544,"Engine.pendingPulse ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_pendingDac)==18552,"Engine.pendingDac ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_transitionCounter)==18560,"Engine.transitionCounter ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_transitioning)==18568,"Engine.transitioning ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_folded)==18576,"Engine.folded ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_fastEnabled)==49352,"Engine.fastEnabled ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_foldedHistoryActive)==49353,"Engine.foldedHistoryActive ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_foldedPriming)==49360,"Engine.foldedPriming ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_legacyPriming)==49368,"Engine.legacyPriming ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_legacyRunning)==49376,"Engine.legacyRunning ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_fastWeight)==49384,"Engine.fastWeight ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_foldedDepth)==49392,"Engine.foldedDepth ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_previousFoldedDepth)==49400,"Engine.previousFoldedDepth ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_foldedFade)==49408,"Engine.foldedFade ABI offset");
_Static_assert(offsetof(dn_r_Engine,f_tubeCpuMode)==49416,"Engine.tubeCpuMode ABI offset");
static void dn_drop_r6_Engine(dn_r_Engine volatile*);static void dn_retain_r6_Engine(dn_r_Engine volatile*);
static void dn_drop_r3_Fir(dn_r_Fir volatile*);static void dn_retain_r3_Fir(dn_r_Fir volatile*);
static void dn_drop_r9_ThreeBand(dn_r_ThreeBand volatile*);static void dn_retain_r9_ThreeBand(dn_r_ThreeBand volatile*);
static void dn_drop_r4_Tube(dn_r_Tube volatile*);static void dn_retain_r4_Tube(dn_r_Tube volatile*);
static void dn_drop_arr3_r4_Tube(dn_t_arr3_r4_Tube volatile*);static void dn_retain_arr3_r4_Tube(dn_t_arr3_r4_Tube volatile*);
static void dn_drop_buf_F32(dn_t_buf_F32 volatile*);static void dn_retain_buf_F32(dn_t_buf_F32 volatile*);
static void dn_drop_buf_F64(dn_t_buf_F64 volatile*);static void dn_retain_buf_F64(dn_t_buf_F64 volatile*);
static void dn_drop_buf_r3_Fir(dn_t_buf_r3_Fir volatile*);static void dn_retain_buf_r3_Fir(dn_t_buf_r3_Fir volatile*);
static void dn_drop_box_r6_Engine(dn_r_Engine * volatile*);static void dn_retain_box_r6_Engine(dn_r_Engine * volatile*);
static void dn_adopt_r6_Engine(dn_r_Engine volatile*);
static void dn_adopt_r3_Fir(dn_r_Fir volatile*);
static void dn_adopt_r9_ThreeBand(dn_r_ThreeBand volatile*);
static void dn_adopt_r4_Tube(dn_r_Tube volatile*);
static void dn_adopt_arr3_r4_Tube(dn_t_arr3_r4_Tube volatile*);
static void dn_adopt_buf_F32(dn_t_buf_F32 volatile*);
static void dn_adopt_buf_F64(dn_t_buf_F64 volatile*);
static void dn_adopt_buf_r3_Fir(dn_t_buf_r3_Fir volatile*);
static void dn_adopt_box_r6_Engine(dn_r_Engine * volatile*);
static double dn_f_min_f(double v_x,double v_lo,double v_hi);
static double dn_f_pi(void);
static dn_r_TubeDef dn_f_tube_definition(size_t v_index);
static double dn_f_tube_softplus(double v_x);
static double dn_f_tube_atan(double v_x);
static double dn_f_tube_plate_current(dn_r_TubeDef const * v_t,double v_vp0,double v_vg);
static double dn_f_tube_plate_voltage(dn_r_TubeDef const * v_t,double v_supply,double v_vg);
static dn_r_ValveChannel dn_f_valve_blank(void);
static uint8_t dn_f_tube_set_rate(dn_r_Tube * v_t,double v_sr);
static dn_r_Tube dn_f_tube_create(double v_sr);
static uint8_t dn_f_tube_reset(dn_r_Tube * v_t);
static uint8_t dn_f_tube_config(dn_r_Tube * v_t,size_t v_typ,size_t v_topology,double v_driveDb,double v_bias,double v_sag,double v_wet);
static double dn_f_tube_lookup(dn_r_Tube const * v_t,size_t v_typ,double v_x,double v_bias);
static double dn_f_tube_evaluate(dn_r_Tube const * v_t,size_t v_typ,size_t v_topology,double v_x,double v_bias);
static double dn_f_tube_process(dn_r_Tube * v_t,double v_x,size_t v_ch);
static dn_r_Biquad dn_f_band_blank_filter(void);
static dn_r_BandChannel dn_f_band_blank_channel(void);
static uint8_t dn_f_band_coefficients(dn_r_Biquad * v_b,double v_freq,double v_rate,bool v_hp);
static uint8_t dn_f_band_update_coeff(dn_r_ThreeBand * v_t,size_t v_ch);
static uint8_t dn_f_band_reset(dn_r_ThreeBand * v_t);
static dn_r_ThreeBand dn_f_band_create(double v_sr);
static uint8_t dn_f_band_set_rate(dn_r_ThreeBand * v_t,double v_sr);
static uint8_t dn_f_band_frequency(dn_r_ThreeBand * v_t,double v_lowHz,double v_highHz);
static uint8_t dn_f_band_update_enabled(dn_r_ThreeBand * v_t);
static uint8_t dn_f_band_config(dn_r_ThreeBand * v_t,size_t v_band,size_t v_typ,size_t v_topology,double v_driveDb,double v_bias,double v_sag,double v_wet);
static double dn_f_band_process(dn_r_ThreeBand * v_t,double v_x,size_t v_channel);
static double dn_f_apply_tube(dn_r_Engine * v_e,double v_signal,size_t v_channel);
static dn_r_OutputStage dn_f_output_prepare(double v_rate);
static uint8_t dn_f_output_reset(dn_r_OutputStage * v_o);
static double dn_f_output_estimate(dn_r_OutputStage const * v_o,size_t v_center);
static uint8_t dn_f_output_process(dn_r_OutputStage * v_o,float * v_left,float * v_right,size_t v_frames,double v_inputMeanSquare,bool v_autoMatch,bool v_limiter,double v_ceilingDb);
static dn_r_Fir dn_f_fir_create(size_t v_n,bool v_upsampler);
static uint8_t dn_f_fir_reset(dn_r_Fir * v_f);
static uint8_t dn_f_fir_push(dn_r_Fir * v_f,double v_x);
static dn_r_Stereo dn_f_fir_upsample(dn_r_Fir * v_f,double v_x);
static double dn_f_fir_downsample(dn_r_Fir * v_f,double v_a,double v_b);
static size_t dn_f_folded_compensation(size_t v_depth);
static uint8_t dn_f_folded_reset(dn_r_Folded * v_f);
static dn_r_Folded dn_f_folded_prepare(void);
static uint8_t dn_f_folded_push(dn_r_Folded * v_f,double v_l,double v_r);
static double dn_f_folded_eval(dn_r_Folded const * v_f,size_t v_ch,size_t v_depth);
static size_t dn_f_if_model(size_t v_dac,size_t v_pulse);
static double dn_f_channel_sample(dn_r_Engine * v_e,double v_x,size_t v_ch);
static dn_r_Biquad dn_f_biquad_create(double v_sr);
static double dn_f_biquad_process(dn_r_Biquad * v_b,double v_x);
static dn_r_Allpass dn_f_allpass_create(void);
static double dn_f_ap_coeff(double v_hz,double v_rate);
static dn_r_Creative dn_f_creative_create(double v_sr);
static double dn_f_allpass_process(dn_r_Allpass * v_s,double v_x,double v_a);
static double dn_f_creative_channel(dn_r_Creative * v_c,double v_x,size_t v_ch,uint32_t v_phaseMode,uint32_t v_filterMode);
static double dn_f_round_away(double v_x);
static double dn_f_next_rand(uint32_t * v_state);
static dn_r_Pulse dn_f_pulse_create(size_t v_mode,size_t v_channel);
static uint8_t dn_f_pulse_reset(dn_r_Pulse * v_p);
static double dn_f_pulse_process(dn_r_Pulse * v_p,double v_signal);
static dn_r_Dac dn_f_dac_create(size_t v_mode,size_t v_channel);
static uint8_t dn_f_dac_reset(dn_r_Dac * v_d);
static double dn_f_dac_shaped(dn_r_Dac * v_d,double v_signal,double v_quantum);
static double dn_f_dac_process(dn_r_Dac * v_d,double v_signal,double v_amount);
static double dn_f_color_sample(double v_x,double v_amount);
static double dn_f_pulse_color(double v_x,double v_strength,size_t v_model);
static dn_r_Engine dn_f_create_engine(double v_sr,size_t v_depth,size_t v_pulseMode,size_t v_dacMode,double v_mismatch);
static uint32_t dn_f_live_latency_final(void);
static uint32_t dn_f_live_latency(void);
static void * dn_f_live_create(double v_rate,uint32_t v_depth);
static void * dn_f_live_create_ex(double v_rate,uint32_t v_depth,uint32_t v_pulseMode,uint32_t v_dacMode,float v_mismatch);
static uint8_t dn_f_live_destroy(void * v_h);
static uint8_t dn_f_live_reset(void * v_h);
static uint32_t dn_f_live_process(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode);
static uint32_t dn_f_live_process_ex(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode,uint32_t v_phaseMode,float v_focus,float v_phase,float v_tone);
static uint32_t dn_f_live_tube_cpu_mode(void * v_h,uint32_t v_mode);
static uint32_t dn_f_live_fast_mode(void * v_h,bool v_enabled);
static uint32_t dn_f_live_request_mode(void * v_h,uint32_t v_depth,uint32_t v_pulseMode,uint32_t v_dacMode,float v_mismatch);
static uint32_t dn_f_live_process_full(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode,uint32_t v_phaseMode,float v_focus,float v_phase,float v_tone,float v_character,float v_pulseIntensity);
static uint32_t dn_f_live_process_final(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode,uint32_t v_phaseMode,float v_focus,float v_phase,float v_tone,float v_character,float v_pulseIntensity,bool v_autoMatch,bool v_limiter,float v_ceilingDb);
static uint32_t dn_f_live_tube_config(void * v_h,uint32_t v_typ,uint32_t v_topology,float v_driveDb,float v_bias,float v_sag,float v_wet);
static uint32_t dn_f_live_band_config(void * v_h,uint32_t v_band,uint32_t v_typ,uint32_t v_topology,float v_driveDb,float v_bias,float v_sag,float v_wet);
static uint32_t dn_f_live_band_frequency(void * v_h,float v_lower,float v_upper);
static uint32_t dn_f_live_tube_routing(void * v_h,uint32_t v_routing);
static void dn_drop_r6_Engine(dn_r_Engine volatile *x); static void dn_retain_r6_Engine(dn_r_Engine volatile *x);
static void dn_drop_r6_Engine(dn_r_Engine volatile *x){dn_drop_r9_ThreeBand(&x->f_threeBand);dn_drop_r4_Tube(&x->f_tube);dn_drop_buf_r3_Fir(&x->f_down);dn_drop_buf_r3_Fir(&x->f_up);}
static void dn_retain_r6_Engine(dn_r_Engine volatile *x){dn_retain_buf_r3_Fir(&x->f_up);dn_retain_buf_r3_Fir(&x->f_down);dn_retain_r4_Tube(&x->f_tube);dn_retain_r9_ThreeBand(&x->f_threeBand);}
static void dn_adopt_r6_Engine(dn_r_Engine volatile*x){dn_adopt_buf_r3_Fir(&x->f_up);dn_adopt_buf_r3_Fir(&x->f_down);dn_adopt_r4_Tube(&x->f_tube);dn_adopt_r9_ThreeBand(&x->f_threeBand);}
static void dn_drop_r3_Fir(dn_r_Fir volatile *x); static void dn_retain_r3_Fir(dn_r_Fir volatile *x);
static void dn_drop_r3_Fir(dn_r_Fir volatile *x){dn_drop_buf_F64(&x->f_history);dn_drop_buf_F64(&x->f_taps);}
static void dn_retain_r3_Fir(dn_r_Fir volatile *x){dn_retain_buf_F64(&x->f_taps);dn_retain_buf_F64(&x->f_history);}
static void dn_adopt_r3_Fir(dn_r_Fir volatile*x){dn_adopt_buf_F64(&x->f_taps);dn_adopt_buf_F64(&x->f_history);}
static void dn_drop_r9_ThreeBand(dn_r_ThreeBand volatile *x); static void dn_retain_r9_ThreeBand(dn_r_ThreeBand volatile *x);
static void dn_drop_r9_ThreeBand(dn_r_ThreeBand volatile *x){dn_drop_arr3_r4_Tube(&x->f_stages);}
static void dn_retain_r9_ThreeBand(dn_r_ThreeBand volatile *x){dn_retain_arr3_r4_Tube(&x->f_stages);}
static void dn_adopt_r9_ThreeBand(dn_r_ThreeBand volatile*x){dn_adopt_arr3_r4_Tube(&x->f_stages);}
static void dn_drop_r4_Tube(dn_r_Tube volatile *x); static void dn_retain_r4_Tube(dn_r_Tube volatile *x);
static void dn_drop_r4_Tube(dn_r_Tube volatile *x){dn_drop_buf_F32(&x->f_curves);}
static void dn_retain_r4_Tube(dn_r_Tube volatile *x){dn_retain_buf_F32(&x->f_curves);}
static void dn_adopt_r4_Tube(dn_r_Tube volatile*x){dn_adopt_buf_F32(&x->f_curves);}
static void dn_drop_arr3_r4_Tube(dn_t_arr3_r4_Tube volatile *x); static void dn_retain_arr3_r4_Tube(dn_t_arr3_r4_Tube volatile *x);
static void dn_drop_arr3_r4_Tube(dn_t_arr3_r4_Tube volatile *x){for(size_t i=0;i<3;i++)dn_drop_r4_Tube(&x->data[i]);}
static void dn_retain_arr3_r4_Tube(dn_t_arr3_r4_Tube volatile *x){for(size_t i=0;i<3;i++)dn_retain_r4_Tube(&x->data[i]);}
static void dn_adopt_arr3_r4_Tube(dn_t_arr3_r4_Tube volatile*x){for(size_t i=0;i<3;i++)dn_adopt_r4_Tube(&x->data[i]);}
static void dn_drop_buf_F32(dn_t_buf_F32 volatile *x); static void dn_retain_buf_F32(dn_t_buf_F32 volatile *x);
static void dn_drop_buf_F32(dn_t_buf_F32 volatile *x){dn_deallocate(x->data);*x=(dn_t_buf_F32){0};}
static void dn_retain_buf_F32(dn_t_buf_F32 volatile *x){dn_detach(x->data);}
static dn_t_buf_F32 dn_new_buf_F32(size_t n){dn_t_buf_F32 x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(float)),_Alignof(float));x.len=x.cap=n;return x;}
static dn_t_buf_F32 dn_aligned_buf_F32(size_t n,size_t a){if(a<_Alignof(float)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_F32 x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(float)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_F32(dn_t_buf_F32 *x,float v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;float *p=dn_allocate_aligned(dn_size(cap,sizeof(float)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(float));if(x->len)memcpy(p,x->data,x->len*sizeof(float));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;x->len++;return 0;}
static uint8_t dn_resize_buf_F32(dn_t_buf_F32 *x,size_t n){if(n<x->len){}if(n>x->cap){size_t cap=n;float *p=dn_allocate_aligned(dn_size(cap,sizeof(float)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(float));if(x->len)memcpy(p,x->data,x->len*sizeof(float));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(float));x->len=n;return 0;}
static void dn_adopt_buf_F32(dn_t_buf_F32 volatile*x){dn_adopt(x->data);}
static void dn_drop_buf_F64(dn_t_buf_F64 volatile *x); static void dn_retain_buf_F64(dn_t_buf_F64 volatile *x);
static void dn_drop_buf_F64(dn_t_buf_F64 volatile *x){dn_deallocate(x->data);*x=(dn_t_buf_F64){0};}
static void dn_retain_buf_F64(dn_t_buf_F64 volatile *x){dn_detach(x->data);}
static dn_t_buf_F64 dn_new_buf_F64(size_t n){dn_t_buf_F64 x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(double)),_Alignof(double));x.len=x.cap=n;return x;}
static dn_t_buf_F64 dn_aligned_buf_F64(size_t n,size_t a){if(a<_Alignof(double)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_F64 x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(double)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_F64(dn_t_buf_F64 *x,double v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;double *p=dn_allocate_aligned(dn_size(cap,sizeof(double)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(double));if(x->len)memcpy(p,x->data,x->len*sizeof(double));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;x->len++;return 0;}
static uint8_t dn_resize_buf_F64(dn_t_buf_F64 *x,size_t n){if(n<x->len){}if(n>x->cap){size_t cap=n;double *p=dn_allocate_aligned(dn_size(cap,sizeof(double)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(double));if(x->len)memcpy(p,x->data,x->len*sizeof(double));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(double));x->len=n;return 0;}
static void dn_adopt_buf_F64(dn_t_buf_F64 volatile*x){dn_adopt(x->data);}
static void dn_drop_buf_r3_Fir(dn_t_buf_r3_Fir volatile *x); static void dn_retain_buf_r3_Fir(dn_t_buf_r3_Fir volatile *x);
static void dn_drop_buf_r3_Fir(dn_t_buf_r3_Fir volatile *x){for(size_t i=0;i<x->len;i++)dn_drop_r3_Fir(&x->data[i]);dn_deallocate(x->data);*x=(dn_t_buf_r3_Fir){0};}
static void dn_retain_buf_r3_Fir(dn_t_buf_r3_Fir volatile *x){for(size_t i=0;i<x->len;i++)dn_retain_r3_Fir(&x->data[i]);dn_detach(x->data);}
static dn_t_buf_r3_Fir dn_new_buf_r3_Fir(size_t n){dn_t_buf_r3_Fir x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(dn_r_Fir)),_Alignof(dn_r_Fir));x.len=x.cap=n;return x;}
static dn_t_buf_r3_Fir dn_aligned_buf_r3_Fir(size_t n,size_t a){if(a<_Alignof(dn_r_Fir)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_r3_Fir x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(dn_r_Fir)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_r3_Fir(dn_t_buf_r3_Fir *x,dn_r_Fir v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;dn_r_Fir *p=dn_allocate_aligned(dn_size(cap,sizeof(dn_r_Fir)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(dn_r_Fir));if(x->len)memcpy(p,x->data,x->len*sizeof(dn_r_Fir));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;dn_retain_r3_Fir(&x->data[x->len]);x->len++;return 0;}
static uint8_t dn_resize_buf_r3_Fir(dn_t_buf_r3_Fir *x,size_t n){if(n<x->len){for(size_t i=n;i<x->len;i++)dn_drop_r3_Fir(&x->data[i]);}if(n>x->cap){size_t cap=n;dn_r_Fir *p=dn_allocate_aligned(dn_size(cap,sizeof(dn_r_Fir)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(dn_r_Fir));if(x->len)memcpy(p,x->data,x->len*sizeof(dn_r_Fir));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(dn_r_Fir));x->len=n;return 0;}
static void dn_adopt_buf_r3_Fir(dn_t_buf_r3_Fir volatile*x){for(size_t i=0;i<x->len;i++)dn_adopt_r3_Fir(&x->data[i]);dn_adopt(x->data);}
static void dn_drop_box_r6_Engine(dn_r_Engine * volatile *x); static void dn_retain_box_r6_Engine(dn_r_Engine * volatile *x);
static dn_r_Engine * dn_new_box_r6_Engine(dn_r_Engine v){dn_r_Engine * p=dn_allocate_aligned(sizeof(dn_r_Engine),_Alignof(dn_r_Engine));*p=v;return p;}
static void dn_drop_box_r6_Engine(dn_r_Engine * volatile *x){if(*x){dn_drop_r6_Engine(*x);dn_deallocate(*x);*x=0;}}
static void dn_retain_box_r6_Engine(dn_r_Engine * volatile *x){if(*x){dn_retain_r6_Engine(*x);dn_detach(*x);}}
static void dn_adopt_box_r6_Engine(dn_r_Engine * volatile*x){if(*x){dn_adopt_r6_Engine(*x);dn_adopt(*x);}}
static double dn_cb_min_f(double v_x,double v_lo,double v_hi);
static double dn_cb_pi(void);
static dn_r_TubeDef dn_cb_tube_definition(size_t v_index);
static double dn_cb_tube_softplus(double v_x);
static double dn_cb_tube_atan(double v_x);
static double dn_cb_tube_plate_current(dn_r_TubeDef const * v_t,double v_vp0,double v_vg);
static double dn_cb_tube_plate_voltage(dn_r_TubeDef const * v_t,double v_supply,double v_vg);
static dn_r_ValveChannel dn_cb_valve_blank(void);
static void dn_cb_tube_set_rate(dn_r_Tube * v_t,double v_sr);
static dn_r_Tube dn_cb_tube_create(double v_sr);
static void dn_cb_tube_reset(dn_r_Tube * v_t);
static void dn_cb_tube_config(dn_r_Tube * v_t,size_t v_typ,size_t v_topology,double v_driveDb,double v_bias,double v_sag,double v_wet);
static double dn_cb_tube_lookup(dn_r_Tube const * v_t,size_t v_typ,double v_x,double v_bias);
static double dn_cb_tube_evaluate(dn_r_Tube const * v_t,size_t v_typ,size_t v_topology,double v_x,double v_bias);
static double dn_cb_tube_process(dn_r_Tube * v_t,double v_x,size_t v_ch);
static dn_r_Biquad dn_cb_band_blank_filter(void);
static dn_r_BandChannel dn_cb_band_blank_channel(void);
static void dn_cb_band_coefficients(dn_r_Biquad * v_b,double v_freq,double v_rate,bool v_hp);
static void dn_cb_band_update_coeff(dn_r_ThreeBand * v_t,size_t v_ch);
static void dn_cb_band_reset(dn_r_ThreeBand * v_t);
static dn_r_ThreeBand dn_cb_band_create(double v_sr);
static void dn_cb_band_set_rate(dn_r_ThreeBand * v_t,double v_sr);
static void dn_cb_band_frequency(dn_r_ThreeBand * v_t,double v_lowHz,double v_highHz);
static void dn_cb_band_update_enabled(dn_r_ThreeBand * v_t);
static void dn_cb_band_config(dn_r_ThreeBand * v_t,size_t v_band,size_t v_typ,size_t v_topology,double v_driveDb,double v_bias,double v_sag,double v_wet);
static double dn_cb_band_process(dn_r_ThreeBand * v_t,double v_x,size_t v_channel);
static double dn_cb_apply_tube(dn_r_Engine * v_e,double v_signal,size_t v_channel);
static dn_r_OutputStage dn_cb_output_prepare(double v_rate);
static void dn_cb_output_reset(dn_r_OutputStage * v_o);
static double dn_cb_output_estimate(dn_r_OutputStage const * v_o,size_t v_center);
static void dn_cb_output_process(dn_r_OutputStage * v_o,float * v_left,float * v_right,size_t v_frames,double v_inputMeanSquare,bool v_autoMatch,bool v_limiter,double v_ceilingDb);
static dn_r_Fir dn_cb_fir_create(size_t v_n,bool v_upsampler);
static void dn_cb_fir_reset(dn_r_Fir * v_f);
static void dn_cb_fir_push(dn_r_Fir * v_f,double v_x);
static dn_r_Stereo dn_cb_fir_upsample(dn_r_Fir * v_f,double v_x);
static double dn_cb_fir_downsample(dn_r_Fir * v_f,double v_a,double v_b);
static size_t dn_cb_folded_compensation(size_t v_depth);
static void dn_cb_folded_reset(dn_r_Folded * v_f);
static dn_r_Folded dn_cb_folded_prepare(void);
static void dn_cb_folded_push(dn_r_Folded * v_f,double v_l,double v_r);
static double dn_cb_folded_eval(dn_r_Folded const * v_f,size_t v_ch,size_t v_depth);
static size_t dn_cb_if_model(size_t v_dac,size_t v_pulse);
static double dn_cb_channel_sample(dn_r_Engine * v_e,double v_x,size_t v_ch);
static dn_r_Biquad dn_cb_biquad_create(double v_sr);
static double dn_cb_biquad_process(dn_r_Biquad * v_b,double v_x);
static dn_r_Allpass dn_cb_allpass_create(void);
static double dn_cb_ap_coeff(double v_hz,double v_rate);
static dn_r_Creative dn_cb_creative_create(double v_sr);
static double dn_cb_allpass_process(dn_r_Allpass * v_s,double v_x,double v_a);
static double dn_cb_creative_channel(dn_r_Creative * v_c,double v_x,size_t v_ch,uint32_t v_phaseMode,uint32_t v_filterMode);
static double dn_cb_round_away(double v_x);
static double dn_cb_next_rand(uint32_t * v_state);
static dn_r_Pulse dn_cb_pulse_create(size_t v_mode,size_t v_channel);
static void dn_cb_pulse_reset(dn_r_Pulse * v_p);
static double dn_cb_pulse_process(dn_r_Pulse * v_p,double v_signal);
static dn_r_Dac dn_cb_dac_create(size_t v_mode,size_t v_channel);
static void dn_cb_dac_reset(dn_r_Dac * v_d);
static double dn_cb_dac_shaped(dn_r_Dac * v_d,double v_signal,double v_quantum);
static double dn_cb_dac_process(dn_r_Dac * v_d,double v_signal,double v_amount);
static double dn_cb_color_sample(double v_x,double v_amount);
static double dn_cb_pulse_color(double v_x,double v_strength,size_t v_model);
static dn_r_Engine dn_cb_create_engine(double v_sr,size_t v_depth,size_t v_pulseMode,size_t v_dacMode,double v_mismatch);
static uint32_t dn_cb_live_latency_final(void);
DN_EXPORT uint32_t live_latency_final(void);
static uint32_t dn_cb_live_latency(void);
DN_EXPORT uint32_t live_latency(void);
static void * dn_cb_live_create(double v_rate,uint32_t v_depth);
DN_EXPORT void * live_create(double v_rate,uint32_t v_depth);
static void * dn_cb_live_create_ex(double v_rate,uint32_t v_depth,uint32_t v_pulseMode,uint32_t v_dacMode,float v_mismatch);
DN_EXPORT void * live_create_ex(double v_rate,uint32_t v_depth,uint32_t v_pulseMode,uint32_t v_dacMode,float v_mismatch);
static void dn_cb_live_destroy(void * v_h);
DN_EXPORT void live_destroy(void * v_h);
static void dn_cb_live_reset(void * v_h);
DN_EXPORT void live_reset(void * v_h);
static uint32_t dn_cb_live_process(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode);
DN_EXPORT uint32_t live_process(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode);
static uint32_t dn_cb_live_process_ex(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode,uint32_t v_phaseMode,float v_focus,float v_phase,float v_tone);
DN_EXPORT uint32_t live_process_ex(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode,uint32_t v_phaseMode,float v_focus,float v_phase,float v_tone);
static uint32_t dn_cb_live_tube_cpu_mode(void * v_h,uint32_t v_mode);
DN_EXPORT uint32_t live_tube_cpu_mode(void * v_h,uint32_t v_mode);
static uint32_t dn_cb_live_fast_mode(void * v_h,bool v_enabled);
DN_EXPORT uint32_t live_fast_mode(void * v_h,bool v_enabled);
static uint32_t dn_cb_live_request_mode(void * v_h,uint32_t v_depth,uint32_t v_pulseMode,uint32_t v_dacMode,float v_mismatch);
DN_EXPORT uint32_t live_request_mode(void * v_h,uint32_t v_depth,uint32_t v_pulseMode,uint32_t v_dacMode,float v_mismatch);
static uint32_t dn_cb_live_process_full(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode,uint32_t v_phaseMode,float v_focus,float v_phase,float v_tone,float v_character,float v_pulseIntensity);
DN_EXPORT uint32_t live_process_full(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode,uint32_t v_phaseMode,float v_focus,float v_phase,float v_tone,float v_character,float v_pulseIntensity);
static uint32_t dn_cb_live_process_final(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode,uint32_t v_phaseMode,float v_focus,float v_phase,float v_tone,float v_character,float v_pulseIntensity,bool v_autoMatch,bool v_limiter,float v_ceilingDb);
DN_EXPORT uint32_t live_process_final(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode,uint32_t v_phaseMode,float v_focus,float v_phase,float v_tone,float v_character,float v_pulseIntensity,bool v_autoMatch,bool v_limiter,float v_ceilingDb);
static uint32_t dn_cb_live_tube_config(void * v_h,uint32_t v_typ,uint32_t v_topology,float v_driveDb,float v_bias,float v_sag,float v_wet);
DN_EXPORT uint32_t live_tube_config(void * v_h,uint32_t v_typ,uint32_t v_topology,float v_driveDb,float v_bias,float v_sag,float v_wet);
static uint32_t dn_cb_live_band_config(void * v_h,uint32_t v_band,uint32_t v_typ,uint32_t v_topology,float v_driveDb,float v_bias,float v_sag,float v_wet);
DN_EXPORT uint32_t live_band_config(void * v_h,uint32_t v_band,uint32_t v_typ,uint32_t v_topology,float v_driveDb,float v_bias,float v_sag,float v_wet);
static uint32_t dn_cb_live_band_frequency(void * v_h,float v_lower,float v_upper);
DN_EXPORT uint32_t live_band_frequency(void * v_h,float v_lower,float v_upper);
static uint32_t dn_cb_live_tube_routing(void * v_h,uint32_t v_routing);
DN_EXPORT uint32_t live_tube_routing(void * v_h,uint32_t v_routing);
static double dn_f_min_f(double v_x,double v_lo,double v_hi) {
{
if(({ double dn_tmp_0=v_x;
double dn_tmp_1=v_lo;
(dn_tmp_0 < dn_tmp_1); })){
{ double dn_tmp_2=v_lo;return dn_tmp_2; }
}
else{
}
if(({ double dn_tmp_3=v_x;
double dn_tmp_4=v_hi;
(dn_tmp_3 > dn_tmp_4); })){
{ double dn_tmp_5=v_hi;return dn_tmp_5; }
}
else{
}
{ double dn_tmp_6=v_x;return dn_tmp_6; }
}

dn_fail("missing native return");
}
static double dn_cb_min_f(double v_x,double v_lo,double v_hi) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_min_f(v_x,v_lo,v_hi);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_pi(void) {
{
{ double dn_tmp_0=((double)(3.1415926535897932384626433832795));return dn_tmp_0; }
}

dn_fail("missing native return");
}
static double dn_cb_pi(void) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_pi();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_TubeDef dn_f_tube_definition(size_t v_index) {
{
if(({ size_t dn_tmp_0=v_index;
size_t dn_tmp_1=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); })){
{ dn_r_TubeDef dn_tmp_13=({ double dn_tmp_2=((double)(100.0));
double dn_tmp_3=((double)(1.40));
double dn_tmp_4=((double)(1060.0));
double dn_tmp_5=((double)(600.0));
double dn_tmp_6=((double)(300.0));
double dn_tmp_7=((double)(180.0));
double dn_tmp_8=(-(((double)(1.5))));
double dn_tmp_9=((double)(100000.0));
double dn_tmp_10=((double)(0.0));
double dn_tmp_11=((double)(1.3));
bool dn_tmp_12=0;
((dn_r_TubeDef){dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_11,dn_tmp_12}); });return dn_tmp_13; }
}
else{
}
if(({ size_t dn_tmp_14=v_index;
size_t dn_tmp_15=((size_t)(1ULL));
(dn_tmp_14 == dn_tmp_15); })){
{ dn_r_TubeDef dn_tmp_27=({ double dn_tmp_16=((double)(21.5));
double dn_tmp_17=((double)(1.30));
double dn_tmp_18=((double)(1180.0));
double dn_tmp_19=((double)(84.0));
double dn_tmp_20=((double)(300.0));
double dn_tmp_21=((double)(160.0));
double dn_tmp_22=(-(((double)(8.0))));
double dn_tmp_23=((double)(22000.0));
double dn_tmp_24=((double)(0.0));
double dn_tmp_25=((double)(7.0));
bool dn_tmp_26=0;
((dn_r_TubeDef){dn_tmp_16,dn_tmp_17,dn_tmp_18,dn_tmp_19,dn_tmp_20,dn_tmp_21,dn_tmp_22,dn_tmp_23,dn_tmp_24,dn_tmp_25,dn_tmp_26}); });return dn_tmp_27; }
}
else{
}
if(({ size_t dn_tmp_28=v_index;
size_t dn_tmp_29=((size_t)(2ULL));
(dn_tmp_28 == dn_tmp_29); })){
{ dn_r_TubeDef dn_tmp_41=({ double dn_tmp_30=((double)(28.0));
double dn_tmp_31=((double)(1.30));
double dn_tmp_32=((double)(330.0));
double dn_tmp_33=((double)(320.0));
double dn_tmp_34=((double)(300.0));
double dn_tmp_35=((double)(90.0));
double dn_tmp_36=(-(((double)(2.0))));
double dn_tmp_37=((double)(15000.0));
double dn_tmp_38=((double)(0.0));
double dn_tmp_39=((double)(1.7));
bool dn_tmp_40=0;
((dn_r_TubeDef){dn_tmp_30,dn_tmp_31,dn_tmp_32,dn_tmp_33,dn_tmp_34,dn_tmp_35,dn_tmp_36,dn_tmp_37,dn_tmp_38,dn_tmp_39,dn_tmp_40}); });return dn_tmp_41; }
}
else{
}
if(({ size_t dn_tmp_42=v_index;
size_t dn_tmp_43=((size_t)(3ULL));
(dn_tmp_42 == dn_tmp_43); })){
{ dn_r_TubeDef dn_tmp_55=({ double dn_tmp_44=((double)(8.7));
double dn_tmp_45=((double)(1.35));
double dn_tmp_46=((double)(1460.0));
double dn_tmp_47=((double)(48.0));
double dn_tmp_48=((double)(12.0));
double dn_tmp_49=((double)(300.0));
double dn_tmp_50=(-(((double)(20.0))));
double dn_tmp_51=((double)(2500.0));
double dn_tmp_52=((double)(250.0));
double dn_tmp_53=((double)(16.0));
bool dn_tmp_54=1;
((dn_r_TubeDef){dn_tmp_44,dn_tmp_45,dn_tmp_46,dn_tmp_47,dn_tmp_48,dn_tmp_49,dn_tmp_50,dn_tmp_51,dn_tmp_52,dn_tmp_53,dn_tmp_54}); });return dn_tmp_55; }
}
else{
}
if(({ size_t dn_tmp_56=v_index;
size_t dn_tmp_57=((size_t)(4ULL));
(dn_tmp_56 == dn_tmp_57); })){
{ dn_r_TubeDef dn_tmp_69=({ double dn_tmp_58=((double)(7.9));
double dn_tmp_59=((double)(1.35));
double dn_tmp_60=((double)(890.0));
double dn_tmp_61=((double)(60.0));
double dn_tmp_62=((double)(24.0));
double dn_tmp_63=((double)(360.0));
double dn_tmp_64=(-(((double)(37.0))));
double dn_tmp_65=((double)(2500.0));
double dn_tmp_66=((double)(350.0));
double dn_tmp_67=((double)(30.0));
bool dn_tmp_68=1;
((dn_r_TubeDef){dn_tmp_58,dn_tmp_59,dn_tmp_60,dn_tmp_61,dn_tmp_62,dn_tmp_63,dn_tmp_64,dn_tmp_65,dn_tmp_66,dn_tmp_67,dn_tmp_68}); });return dn_tmp_69; }
}
else{
}
{ dn_r_TubeDef dn_tmp_81=({ double dn_tmp_70=((double)(8.8));
double dn_tmp_71=((double)(1.35));
double dn_tmp_72=((double)(730.0));
double dn_tmp_73=((double)(32.0));
double dn_tmp_74=((double)(16.0));
double dn_tmp_75=((double)(350.0));
double dn_tmp_76=(-(((double)(40.0))));
double dn_tmp_77=((double)(2300.0));
double dn_tmp_78=((double)(400.0));
double dn_tmp_79=((double)(34.0));
bool dn_tmp_80=1;
((dn_r_TubeDef){dn_tmp_70,dn_tmp_71,dn_tmp_72,dn_tmp_73,dn_tmp_74,dn_tmp_75,dn_tmp_76,dn_tmp_77,dn_tmp_78,dn_tmp_79,dn_tmp_80}); });return dn_tmp_81; }
}

dn_fail("missing native return");
}
static dn_r_TubeDef dn_cb_tube_definition(size_t v_index) {
 if(!dn_enter()){return (dn_r_TubeDef){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_TubeDef){0};}
 dn_r_TubeDef dn_result=dn_f_tube_definition(v_index);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_tube_softplus(double v_x) {
{
if(({ double dn_tmp_0=v_x;
double dn_tmp_1=((double)(40.0));
(dn_tmp_0 > dn_tmp_1); })){
{ double dn_tmp_2=v_x;return dn_tmp_2; }
}
else{
}
if(({ double dn_tmp_3=v_x;
double dn_tmp_4=(-(((double)(40.0))));
(dn_tmp_3 < dn_tmp_4); })){
{ double dn_tmp_6=({ double dn_tmp_5=v_x;
exp(dn_tmp_5); });return dn_tmp_6; }
}
else{
}
{ double dn_tmp_11=({ double dn_tmp_7=({ double dn_tmp_8=((double)(1.0));
double dn_tmp_9=({ double dn_tmp_10=v_x;
exp(dn_tmp_10); });
(dn_tmp_8 + dn_tmp_9); });
log(dn_tmp_7); });return dn_tmp_11; }
}

dn_fail("missing native return");
}
static double dn_cb_tube_softplus(double v_x) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_tube_softplus(v_x);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_tube_atan(double v_x) {
{
double v_ax=({ double dn_tmp_0=v_x;
fabs(dn_tmp_0); });
double v_z=v_ax;
bool v_invert=({ double dn_tmp_1=v_ax;
double dn_tmp_2=((double)(1.0));
(dn_tmp_1 > dn_tmp_2); });
if(v_invert){
{ double dn_tmp_6=({ double dn_tmp_3=((double)(1.0));
double dn_tmp_4=v_ax;
(dn_tmp_3 / dn_tmp_4); });double *dn_tmp_5=&(v_z);*dn_tmp_5=dn_tmp_6; }
}
else{
}
double v_a=({ double dn_tmp_7=v_z;
double dn_tmp_8=({ double dn_tmp_9=((double)(1.0));
double dn_tmp_10=({ double dn_tmp_11=({ double dn_tmp_12=((double)(1.0));
double dn_tmp_13=({ double dn_tmp_14=v_z;
double dn_tmp_15=v_z;
(dn_tmp_14 * dn_tmp_15); });
(dn_tmp_12 + dn_tmp_13); });
sqrt(dn_tmp_11); });
(dn_tmp_9 + dn_tmp_10); });
(dn_tmp_7 / dn_tmp_8); });
double v_square=({ double dn_tmp_16=v_a;
double dn_tmp_17=v_a;
(dn_tmp_16 * dn_tmp_17); });
double v_power=v_a;
double v_sum=((double)(0.0));
{ size_t dn_tmp_19=((size_t)(0ULL));size_t dn_tmp_18=((size_t)(26ULL));for(size_t v_i=dn_tmp_19;v_i<dn_tmp_18;v_i++){
double v_v=({ double dn_tmp_20=v_power;
double dn_tmp_21=({ size_t dn_tmp_22=({ size_t dn_tmp_23=({ size_t dn_tmp_24=((size_t)(2ULL));
size_t dn_tmp_25=v_i;
dn_USize_mul(dn_tmp_24,dn_tmp_25); });
size_t dn_tmp_26=((size_t)(1ULL));
dn_USize_add(dn_tmp_23,dn_tmp_26); });
(double)dn_tmp_22; });
(dn_tmp_20 / dn_tmp_21); });
if(({ size_t dn_tmp_27=({ size_t dn_tmp_28=v_i;
size_t dn_tmp_29=((size_t)(2ULL));
dn_USize_mod(dn_tmp_28,dn_tmp_29); });
size_t dn_tmp_30=((size_t)(0ULL));
(dn_tmp_27 == dn_tmp_30); })){
{ double dn_tmp_34=({ double dn_tmp_31=v_sum;
double dn_tmp_32=v_v;
(dn_tmp_31 + dn_tmp_32); });double *dn_tmp_33=&(v_sum);*dn_tmp_33=dn_tmp_34; }
}
else{
{ double dn_tmp_38=({ double dn_tmp_35=v_sum;
double dn_tmp_36=v_v;
(dn_tmp_35 - dn_tmp_36); });double *dn_tmp_37=&(v_sum);*dn_tmp_37=dn_tmp_38; }
}
{ double dn_tmp_42=({ double dn_tmp_39=v_power;
double dn_tmp_40=v_square;
(dn_tmp_39 * dn_tmp_40); });double *dn_tmp_41=&(v_power);*dn_tmp_41=dn_tmp_42; }
}
 }
double v_result=({ double dn_tmp_43=((double)(2.0));
double dn_tmp_44=v_sum;
(dn_tmp_43 * dn_tmp_44); });
if(v_invert){
{ double dn_tmp_50=({ double dn_tmp_45=({ double dn_tmp_46=((double)(0.5));
double dn_tmp_47=({ dn_f_pi(); });
(dn_tmp_46 * dn_tmp_47); });
double dn_tmp_48=v_result;
(dn_tmp_45 - dn_tmp_48); });double *dn_tmp_49=&(v_result);*dn_tmp_49=dn_tmp_50; }
}
else{
}
if(({ double dn_tmp_51=v_x;
double dn_tmp_52=((double)(0.0));
(dn_tmp_51 < dn_tmp_52); })){
{ double dn_tmp_54=(-(v_result));double *dn_tmp_53=&(v_result);*dn_tmp_53=dn_tmp_54; }
}
else{
}
{ double dn_tmp_55=v_result;return dn_tmp_55; }
}

dn_fail("missing native return");
}
static double dn_cb_tube_atan(double v_x) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_tube_atan(v_x);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_tube_plate_current(dn_r_TubeDef const * v_t,double v_vp0,double v_vg) {
{
double v_vp=({ double dn_tmp_0=v_vp0;
double dn_tmp_1=((double)(0.0));
double dn_tmp_2=((double)(1.0e9));
dn_f_min_f(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
if((v_t)->f_pentode){
double v_e1=({ double dn_tmp_3=({ double dn_tmp_4=(v_t)->f_screen;
double dn_tmp_5=(v_t)->f_kp;
(dn_tmp_4 / dn_tmp_5); });
double dn_tmp_6=({ double dn_tmp_7=({ double dn_tmp_8=(v_t)->f_kp;
double dn_tmp_9=({ double dn_tmp_10=({ double dn_tmp_11=((double)(1.0));
double dn_tmp_12=(v_t)->f_mu;
(dn_tmp_11 / dn_tmp_12); });
double dn_tmp_13=({ double dn_tmp_14=v_vg;
double dn_tmp_15=(v_t)->f_screen;
(dn_tmp_14 / dn_tmp_15); });
(dn_tmp_10 + dn_tmp_13); });
(dn_tmp_8 * dn_tmp_9); });
dn_f_tube_softplus(dn_tmp_7); });
(dn_tmp_3 * dn_tmp_6); });
{ double dn_tmp_32=({ double dn_tmp_16=({ double dn_tmp_17=({ double dn_tmp_18=((double)(2.0));
double dn_tmp_19=({ double dn_tmp_20=({ double dn_tmp_21=(v_t)->f_ex;
double dn_tmp_22=({ double dn_tmp_23=({ double dn_tmp_24=v_e1;
double dn_tmp_25=((double)(1.0e-200));
double dn_tmp_26=((double)(1.0e200));
dn_f_min_f(dn_tmp_24,dn_tmp_25,dn_tmp_26); });
log(dn_tmp_23); });
(dn_tmp_21 * dn_tmp_22); });
exp(dn_tmp_20); });
(dn_tmp_18 * dn_tmp_19); });
double dn_tmp_27=(v_t)->f_kg1;
(dn_tmp_17 / dn_tmp_27); });
double dn_tmp_28=({ double dn_tmp_29=({ double dn_tmp_30=v_vp;
double dn_tmp_31=(v_t)->f_kvb;
(dn_tmp_30 / dn_tmp_31); });
dn_f_tube_atan(dn_tmp_29); });
(dn_tmp_16 * dn_tmp_28); });return dn_tmp_32; }
}
else{
}
double v_e1=({ double dn_tmp_33=({ double dn_tmp_34=v_vp;
double dn_tmp_35=(v_t)->f_kp;
(dn_tmp_34 / dn_tmp_35); });
double dn_tmp_36=({ double dn_tmp_37=({ double dn_tmp_38=(v_t)->f_kp;
double dn_tmp_39=({ double dn_tmp_40=({ double dn_tmp_41=((double)(1.0));
double dn_tmp_42=(v_t)->f_mu;
(dn_tmp_41 / dn_tmp_42); });
double dn_tmp_43=({ double dn_tmp_44=v_vg;
double dn_tmp_45=({ double dn_tmp_46=({ double dn_tmp_47=(v_t)->f_kvb;
double dn_tmp_48=({ double dn_tmp_49=v_vp;
double dn_tmp_50=v_vp;
(dn_tmp_49 * dn_tmp_50); });
(dn_tmp_47 + dn_tmp_48); });
sqrt(dn_tmp_46); });
(dn_tmp_44 / dn_tmp_45); });
(dn_tmp_40 + dn_tmp_43); });
(dn_tmp_38 * dn_tmp_39); });
dn_f_tube_softplus(dn_tmp_37); });
(dn_tmp_33 * dn_tmp_36); });
if(({ double dn_tmp_51=v_e1;
double dn_tmp_52=((double)(0.0));
(dn_tmp_51 <= dn_tmp_52); })){
{ double dn_tmp_53=((double)(0.0));return dn_tmp_53; }
}
else{
}
{ double dn_tmp_62=({ double dn_tmp_54=({ double dn_tmp_55=((double)(2.0));
double dn_tmp_56=({ double dn_tmp_57=({ double dn_tmp_58=(v_t)->f_ex;
double dn_tmp_59=({ double dn_tmp_60=v_e1;
log(dn_tmp_60); });
(dn_tmp_58 * dn_tmp_59); });
exp(dn_tmp_57); });
(dn_tmp_55 * dn_tmp_56); });
double dn_tmp_61=(v_t)->f_kg1;
(dn_tmp_54 / dn_tmp_61); });return dn_tmp_62; }
}

dn_fail("missing native return");
}
static double dn_cb_tube_plate_current(dn_r_TubeDef const * v_t,double v_vp0,double v_vg) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_tube_plate_current(v_t,v_vp0,v_vg);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_tube_plate_voltage(dn_r_TubeDef const * v_t,double v_supply,double v_vg) {
{
double v_lo=((double)(0.0));
double v_hi=v_supply;
{ size_t dn_tmp_1=((size_t)(0ULL));size_t dn_tmp_0=((size_t)(36ULL));for(size_t v_i=dn_tmp_1;v_i<dn_tmp_0;v_i++){
double v_mid=({ double dn_tmp_2=({ double dn_tmp_3=v_lo;
double dn_tmp_4=v_hi;
(dn_tmp_3 + dn_tmp_4); });
double dn_tmp_5=((double)(0.5));
(dn_tmp_2 * dn_tmp_5); });
if(({ double dn_tmp_6=({ double dn_tmp_7=v_mid;
double dn_tmp_8=({ double dn_tmp_9=(v_t)->f_load;
double dn_tmp_10=({ dn_r_TubeDef const * dn_tmp_11=v_t;
double dn_tmp_12=v_mid;
double dn_tmp_13=v_vg;
dn_f_tube_plate_current(dn_tmp_11,dn_tmp_12,dn_tmp_13); });
(dn_tmp_9 * dn_tmp_10); });
(dn_tmp_7 + dn_tmp_8); });
double dn_tmp_14=v_supply;
(dn_tmp_6 > dn_tmp_14); })){
{ double dn_tmp_16=v_mid;double *dn_tmp_15=&(v_hi);*dn_tmp_15=dn_tmp_16; }
}
else{
{ double dn_tmp_18=v_mid;double *dn_tmp_17=&(v_lo);*dn_tmp_17=dn_tmp_18; }
}
}
 }
{ double dn_tmp_23=({ double dn_tmp_19=({ double dn_tmp_20=v_lo;
double dn_tmp_21=v_hi;
(dn_tmp_20 + dn_tmp_21); });
double dn_tmp_22=((double)(0.5));
(dn_tmp_19 * dn_tmp_22); });return dn_tmp_23; }
}

dn_fail("missing native return");
}
static double dn_cb_tube_plate_voltage(dn_r_TubeDef const * v_t,double v_supply,double v_vg) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_tube_plate_voltage(v_t,v_supply,v_vg);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ValveChannel dn_f_valve_blank(void) {
{
{ dn_r_ValveChannel dn_tmp_10=({ size_t dn_tmp_0=((size_t)(0ULL));
size_t dn_tmp_1=((size_t)(0ULL));
size_t dn_tmp_2=((size_t)(0ULL));
size_t dn_tmp_3=((size_t)(0ULL));
double dn_tmp_4=((double)(1.0));
double dn_tmp_5=((double)(1.0));
double dn_tmp_6=((double)(0.0));
double dn_tmp_7=((double)(0.0));
double dn_tmp_8=((double)(0.0));
double dn_tmp_9=((double)(0.0));
((dn_r_ValveChannel){dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9}); });return dn_tmp_10; }
}

dn_fail("missing native return");
}
static dn_r_ValveChannel dn_cb_valve_blank(void) {
 if(!dn_enter()){return (dn_r_ValveChannel){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ValveChannel){0};}
 dn_r_ValveChannel dn_result=dn_f_valve_blank();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_tube_set_rate(dn_r_Tube * v_t,double v_sr) {
{
double v_rate=({ double dn_tmp_0=v_sr;
double dn_tmp_1=((double)(16000.0));
double dn_tmp_2=((double)(6144000.0));
dn_f_min_f(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
{ double dn_tmp_11=({ double dn_tmp_3=((double)(1.0));
double dn_tmp_4=({ double dn_tmp_5=({ double dn_tmp_6=(-(((double)(1.0))));
double dn_tmp_7=({ double dn_tmp_8=((double)(0.015));
double dn_tmp_9=v_rate;
(dn_tmp_8 * dn_tmp_9); });
(dn_tmp_6 / dn_tmp_7); });
exp(dn_tmp_5); });
(dn_tmp_3 - dn_tmp_4); });double *dn_tmp_10=&((v_t)->f_smoothing);*dn_tmp_10=dn_tmp_11; }
{ double dn_tmp_20=({ double dn_tmp_12=((double)(1.0));
double dn_tmp_13=({ double dn_tmp_14=({ double dn_tmp_15=(-(((double)(1.0))));
double dn_tmp_16=({ double dn_tmp_17=((double)(0.004));
double dn_tmp_18=v_rate;
(dn_tmp_17 * dn_tmp_18); });
(dn_tmp_15 / dn_tmp_16); });
exp(dn_tmp_14); });
(dn_tmp_12 - dn_tmp_13); });double *dn_tmp_19=&((v_t)->f_envelopeAttack);*dn_tmp_19=dn_tmp_20; }
{ double dn_tmp_29=({ double dn_tmp_21=((double)(1.0));
double dn_tmp_22=({ double dn_tmp_23=({ double dn_tmp_24=(-(((double)(1.0))));
double dn_tmp_25=({ double dn_tmp_26=((double)(0.080));
double dn_tmp_27=v_rate;
(dn_tmp_26 * dn_tmp_27); });
(dn_tmp_24 / dn_tmp_25); });
exp(dn_tmp_23); });
(dn_tmp_21 - dn_tmp_22); });double *dn_tmp_28=&((v_t)->f_envelopeRelease);*dn_tmp_28=dn_tmp_29; }
{ double dn_tmp_35=({ double dn_tmp_30=((double)(1.0));
double dn_tmp_31=({ double dn_tmp_32=((double)(0.015));
double dn_tmp_33=v_rate;
(dn_tmp_32 * dn_tmp_33); });
(dn_tmp_30 / dn_tmp_31); });double *dn_tmp_34=&((v_t)->f_transitionStep);*dn_tmp_34=dn_tmp_35; }
}

return 0;
}
static void dn_cb_tube_set_rate(dn_r_Tube * v_t,double v_sr) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_tube_set_rate(v_t,v_sr);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static dn_r_Tube dn_f_tube_create(double v_sr) {
{
dn_t_buf_F32 volatile v_curves={0};
dn_r_Tube volatile v_stage={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r4_Tube(&v_stage);
dn_drop_buf_F32(&v_curves);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_curves=({ size_t dn_tmp_2=((size_t)(36882ULL));
dn_new_buf_F32(dn_tmp_2); });
{ size_t dn_tmp_4=((size_t)(0ULL));size_t dn_tmp_3=((size_t)(6ULL));for(size_t v_tube=dn_tmp_4;v_tube<dn_tmp_3;v_tube++){
dn_r_TubeDef v_t=({ size_t dn_tmp_5=v_tube;
dn_f_tube_definition(dn_tmp_5); });
{ size_t dn_tmp_7=((size_t)(0ULL));size_t dn_tmp_6=((size_t)(3ULL));for(size_t v_b=dn_tmp_7;v_b<dn_tmp_6;v_b++){
double v_gridScale=((double)(0.55));
if(({ size_t dn_tmp_8=v_b;
size_t dn_tmp_9=((size_t)(1ULL));
(dn_tmp_8 == dn_tmp_9); })){
{ double dn_tmp_11=((double)(1.0));double *dn_tmp_10=&(v_gridScale);*dn_tmp_10=dn_tmp_11; }
}
else{
}
if(({ size_t dn_tmp_12=v_b;
size_t dn_tmp_13=((size_t)(2ULL));
(dn_tmp_12 == dn_tmp_13); })){
{ double dn_tmp_15=((double)(1.45));double *dn_tmp_14=&(v_gridScale);*dn_tmp_14=dn_tmp_15; }
}
else{
}
double v_gridBias=({ double dn_tmp_16=(v_t).f_grid;
double dn_tmp_17=v_gridScale;
(dn_tmp_16 * dn_tmp_17); });
double v_supply=({ double dn_tmp_18=(v_t).f_plate;
double dn_tmp_19=({ double dn_tmp_20=(v_t).f_load;
double dn_tmp_21=({ dn_r_TubeDef const * dn_tmp_22=(&(v_t));
double dn_tmp_23=(v_t).f_plate;
double dn_tmp_24=v_gridBias;
dn_f_tube_plate_current(dn_tmp_22,dn_tmp_23,dn_tmp_24); });
(dn_tmp_20 * dn_tmp_21); });
(dn_tmp_18 + dn_tmp_19); });
double v_base=({ dn_r_TubeDef const * dn_tmp_25=(&(v_t));
double dn_tmp_26=v_supply;
double dn_tmp_27=v_gridBias;
dn_f_tube_plate_voltage(dn_tmp_25,dn_tmp_26,dn_tmp_27); });
double v_probe=({ double dn_tmp_28=((double)(0.0005));
double dn_tmp_29=(v_t).f_swing;
(dn_tmp_28 * dn_tmp_29); });
double v_slope=({ double dn_tmp_30=({ double dn_tmp_31=({ dn_r_TubeDef const * dn_tmp_32=(&(v_t));
double dn_tmp_33=v_supply;
double dn_tmp_34=({ double dn_tmp_35=v_gridBias;
double dn_tmp_36=v_probe;
(dn_tmp_35 - dn_tmp_36); });
dn_f_tube_plate_voltage(dn_tmp_32,dn_tmp_33,dn_tmp_34); });
double dn_tmp_37=({ dn_r_TubeDef const * dn_tmp_38=(&(v_t));
double dn_tmp_39=v_supply;
double dn_tmp_40=({ double dn_tmp_41=v_gridBias;
double dn_tmp_42=v_probe;
(dn_tmp_41 + dn_tmp_42); });
dn_f_tube_plate_voltage(dn_tmp_38,dn_tmp_39,dn_tmp_40); });
(dn_tmp_31 - dn_tmp_37); });
double dn_tmp_43=({ double dn_tmp_44=((double)(2.0));
double dn_tmp_45=v_probe;
(dn_tmp_44 * dn_tmp_45); });
(dn_tmp_30 / dn_tmp_43); });
double v_norm=({ double dn_tmp_46=({ double dn_tmp_47=v_slope;
double dn_tmp_48=(v_t).f_swing;
(dn_tmp_47 * dn_tmp_48); });
double dn_tmp_49=((double)(1.0e-6));
double dn_tmp_50=((double)(1.0e300));
dn_f_min_f(dn_tmp_46,dn_tmp_49,dn_tmp_50); });
{ size_t dn_tmp_52=((size_t)(0ULL));size_t dn_tmp_51=((size_t)(2049ULL));for(size_t v_i=dn_tmp_52;v_i<dn_tmp_51;v_i++){
double v_normalized=({ double dn_tmp_53=({ double dn_tmp_54=({ size_t dn_tmp_55=v_i;
(double)dn_tmp_55; });
double dn_tmp_56=({ double dn_tmp_57=((double)(5.0));
double dn_tmp_58=((double)(2048.0));
(dn_tmp_57 / dn_tmp_58); });
(dn_tmp_54 * dn_tmp_56); });
double dn_tmp_59=((double)(2.5));
(dn_tmp_53 - dn_tmp_59); });
double v_plate=({ dn_r_TubeDef const * dn_tmp_60=(&(v_t));
double dn_tmp_61=v_supply;
double dn_tmp_62=({ double dn_tmp_63=v_gridBias;
double dn_tmp_64=({ double dn_tmp_65=v_normalized;
double dn_tmp_66=(v_t).f_swing;
(dn_tmp_65 * dn_tmp_66); });
(dn_tmp_63 + dn_tmp_64); });
dn_f_tube_plate_voltage(dn_tmp_60,dn_tmp_61,dn_tmp_62); });
{ float dn_tmp_83=({ double dn_tmp_77=({ double dn_tmp_78=({ double dn_tmp_79=v_base;
double dn_tmp_80=v_plate;
(dn_tmp_79 - dn_tmp_80); });
double dn_tmp_81=v_norm;
(dn_tmp_78 / dn_tmp_81); });
(float)dn_tmp_77; });float *dn_tmp_82=&((*({ dn_t_buf_F32 volatile *dn_tmp_67=&(v_curves);size_t dn_tmp_68=({ size_t dn_tmp_69=({ size_t dn_tmp_70=({ size_t dn_tmp_71=({ size_t dn_tmp_72=v_tube;
size_t dn_tmp_73=((size_t)(3ULL));
dn_USize_mul(dn_tmp_72,dn_tmp_73); });
size_t dn_tmp_74=v_b;
dn_USize_add(dn_tmp_71,dn_tmp_74); });
size_t dn_tmp_75=((size_t)(2049ULL));
dn_USize_mul(dn_tmp_70,dn_tmp_75); });
size_t dn_tmp_76=v_i;
dn_USize_add(dn_tmp_69,dn_tmp_76); });&dn_tmp_67->data[dn_index(dn_tmp_68,dn_tmp_67->len)]; })));*dn_tmp_82=dn_tmp_83; }
}
 }
}
 }
}
 }
v_stage=({ dn_t_buf_F32 dn_tmp_84=({ dn_t_buf_F32 dn_tmp_85=v_curves;v_curves=(dn_t_buf_F32){0};dn_adopt_buf_F32(&dn_tmp_85);dn_tmp_85; });
dn_adopt_buf_F32(&dn_tmp_84);
dn_t_arr2_r12_ValveChannel dn_tmp_86=({ dn_r_ValveChannel dn_tmp_87=({ dn_f_valve_blank(); });
dn_r_ValveChannel dn_tmp_88=({ dn_f_valve_blank(); });
((dn_t_arr2_r12_ValveChannel){{dn_tmp_87,dn_tmp_88}}); });
size_t dn_tmp_89=((size_t)(0ULL));
size_t dn_tmp_90=((size_t)(0ULL));
double dn_tmp_91=((double)(1.0));
double dn_tmp_92=((double)(0.0));
double dn_tmp_93=((double)(0.0));
double dn_tmp_94=((double)(1.0));
double dn_tmp_95=((double)(0.001));
double dn_tmp_96=((double)(0.001));
double dn_tmp_97=((double)(0.0001));
double dn_tmp_98=((double)(0.01));
((dn_r_Tube){dn_tmp_84,dn_tmp_86,dn_tmp_89,dn_tmp_90,dn_tmp_91,dn_tmp_92,dn_tmp_93,dn_tmp_94,dn_tmp_95,dn_tmp_96,dn_tmp_97,dn_tmp_98}); });
(void)(({ dn_r_Tube * dn_tmp_99=(&(v_stage));
double dn_tmp_100=v_sr;
dn_f_tube_set_rate(dn_tmp_99,dn_tmp_100); }));
{ dn_r_Tube dn_tmp_102=({ dn_r_Tube dn_tmp_101=v_stage;v_stage=(dn_r_Tube){0};dn_adopt_r4_Tube(&dn_tmp_101);dn_tmp_101; });dn_adopt_r4_Tube(&dn_tmp_102);dn_drop_r4_Tube(&v_stage);
dn_drop_buf_F32(&v_curves);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_102; }
dn_drop_r4_Tube(&v_stage);
dn_drop_buf_F32(&v_curves);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_r_Tube dn_cb_tube_create(double v_sr) {
 if(!dn_enter()){return (dn_r_Tube){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_Tube){0};}
 dn_r_Tube dn_result=dn_f_tube_create(v_sr);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_tube_reset(dn_r_Tube * v_t) {
{
{ size_t dn_tmp_1=((size_t)(0ULL));size_t dn_tmp_0=((size_t)(2ULL));for(size_t v_c=dn_tmp_1;v_c<dn_tmp_0;v_c++){
{ dn_r_ValveChannel dn_tmp_15=({ size_t dn_tmp_4=(v_t)->f_desiredType;
size_t dn_tmp_5=(v_t)->f_desiredType;
size_t dn_tmp_6=(v_t)->f_desiredTopology;
size_t dn_tmp_7=(v_t)->f_desiredTopology;
double dn_tmp_8=((double)(1.0));
double dn_tmp_9=(v_t)->f_desiredDrive;
double dn_tmp_10=(v_t)->f_desiredBias;
double dn_tmp_11=(v_t)->f_desiredSag;
double dn_tmp_12=((double)(0.0));
double dn_tmp_13=((double)(0.0));
((dn_r_ValveChannel){dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_11,dn_tmp_12,dn_tmp_13}); });dn_r_ValveChannel *dn_tmp_14=&((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_2=&((v_t)->f_channels);size_t dn_tmp_3=v_c;&dn_tmp_2->data[dn_index(dn_tmp_3,2)]; })));*dn_tmp_14=dn_tmp_15; }
if(({ size_t dn_tmp_16=(v_t)->f_desiredType;
size_t dn_tmp_17=((size_t)(0ULL));
(dn_tmp_16 > dn_tmp_17); })){
{ double dn_tmp_21=(v_t)->f_desiredWet;double *dn_tmp_20=&(((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_18=&((v_t)->f_channels);size_t dn_tmp_19=v_c;&dn_tmp_18->data[dn_index(dn_tmp_19,2)]; }))).f_wet);*dn_tmp_20=dn_tmp_21; }
}
else{
}
}
 }
}

return 0;
}
static void dn_cb_tube_reset(dn_r_Tube * v_t) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_tube_reset(v_t);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_tube_config(dn_r_Tube * v_t,size_t v_typ,size_t v_topology,double v_driveDb,double v_bias,double v_sag,double v_wet) {
{
{ size_t dn_tmp_1=v_typ;size_t *dn_tmp_0=&((v_t)->f_desiredType);*dn_tmp_0=dn_tmp_1; }
if(({ size_t dn_tmp_2=(v_t)->f_desiredType;
size_t dn_tmp_3=((size_t)(6ULL));
(dn_tmp_2 > dn_tmp_3); })){
{ size_t dn_tmp_5=((size_t)(6ULL));size_t *dn_tmp_4=&((v_t)->f_desiredType);*dn_tmp_4=dn_tmp_5; }
}
else{
}
{ size_t dn_tmp_7=v_topology;size_t *dn_tmp_6=&((v_t)->f_desiredTopology);*dn_tmp_6=dn_tmp_7; }
if(({ size_t dn_tmp_8=(v_t)->f_desiredTopology;
size_t dn_tmp_9=((size_t)(1ULL));
(dn_tmp_8 > dn_tmp_9); })){
{ size_t dn_tmp_11=((size_t)(1ULL));size_t *dn_tmp_10=&((v_t)->f_desiredTopology);*dn_tmp_10=dn_tmp_11; }
}
else{
}
{ double dn_tmp_22=({ double dn_tmp_12=({ double dn_tmp_13=({ double dn_tmp_14=({ double dn_tmp_15=((double)(10.0));
log(dn_tmp_15); });
double dn_tmp_16=({ double dn_tmp_17=v_driveDb;
double dn_tmp_18=((double)(0.0));
double dn_tmp_19=((double)(36.0));
dn_f_min_f(dn_tmp_17,dn_tmp_18,dn_tmp_19); });
(dn_tmp_14 * dn_tmp_16); });
double dn_tmp_20=((double)(20.0));
(dn_tmp_13 / dn_tmp_20); });
exp(dn_tmp_12); });double *dn_tmp_21=&((v_t)->f_desiredDrive);*dn_tmp_21=dn_tmp_22; }
{ double dn_tmp_27=({ double dn_tmp_23=v_bias;
double dn_tmp_24=(-(((double)(1.0))));
double dn_tmp_25=((double)(1.0));
dn_f_min_f(dn_tmp_23,dn_tmp_24,dn_tmp_25); });double *dn_tmp_26=&((v_t)->f_desiredBias);*dn_tmp_26=dn_tmp_27; }
{ double dn_tmp_32=({ double dn_tmp_28=v_sag;
double dn_tmp_29=((double)(0.0));
double dn_tmp_30=((double)(1.0));
dn_f_min_f(dn_tmp_28,dn_tmp_29,dn_tmp_30); });double *dn_tmp_31=&((v_t)->f_desiredSag);*dn_tmp_31=dn_tmp_32; }
{ double dn_tmp_37=({ double dn_tmp_33=v_wet;
double dn_tmp_34=((double)(0.0));
double dn_tmp_35=((double)(1.0));
dn_f_min_f(dn_tmp_33,dn_tmp_34,dn_tmp_35); });double *dn_tmp_36=&((v_t)->f_desiredWet);*dn_tmp_36=dn_tmp_37; }
{ size_t dn_tmp_39=((size_t)(0ULL));size_t dn_tmp_38=((size_t)(2ULL));for(size_t v_i=dn_tmp_39;v_i<dn_tmp_38;v_i++){
if((({ size_t dn_tmp_40=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_41=&((v_t)->f_channels);size_t dn_tmp_42=v_i;&dn_tmp_41->data[dn_index(dn_tmp_42,2)]; }))).f_newType;
size_t dn_tmp_43=(v_t)->f_desiredType;
(dn_tmp_40 != dn_tmp_43); }) || ({ size_t dn_tmp_44=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_45=&((v_t)->f_channels);size_t dn_tmp_46=v_i;&dn_tmp_45->data[dn_index(dn_tmp_46,2)]; }))).f_newTopology;
size_t dn_tmp_47=(v_t)->f_desiredTopology;
(dn_tmp_44 != dn_tmp_47); }))){
{ size_t dn_tmp_53=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_50=&((v_t)->f_channels);size_t dn_tmp_51=v_i;&dn_tmp_50->data[dn_index(dn_tmp_51,2)]; }))).f_newType;size_t *dn_tmp_52=&(((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_48=&((v_t)->f_channels);size_t dn_tmp_49=v_i;&dn_tmp_48->data[dn_index(dn_tmp_49,2)]; }))).f_oldType);*dn_tmp_52=dn_tmp_53; }
{ size_t dn_tmp_59=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_56=&((v_t)->f_channels);size_t dn_tmp_57=v_i;&dn_tmp_56->data[dn_index(dn_tmp_57,2)]; }))).f_newTopology;size_t *dn_tmp_58=&(((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_54=&((v_t)->f_channels);size_t dn_tmp_55=v_i;&dn_tmp_54->data[dn_index(dn_tmp_55,2)]; }))).f_oldTopology);*dn_tmp_58=dn_tmp_59; }
{ size_t dn_tmp_63=(v_t)->f_desiredType;size_t *dn_tmp_62=&(((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_60=&((v_t)->f_channels);size_t dn_tmp_61=v_i;&dn_tmp_60->data[dn_index(dn_tmp_61,2)]; }))).f_newType);*dn_tmp_62=dn_tmp_63; }
{ size_t dn_tmp_67=(v_t)->f_desiredTopology;size_t *dn_tmp_66=&(((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_64=&((v_t)->f_channels);size_t dn_tmp_65=v_i;&dn_tmp_64->data[dn_index(dn_tmp_65,2)]; }))).f_newTopology);*dn_tmp_66=dn_tmp_67; }
{ double dn_tmp_71=((double)(0.0));double *dn_tmp_70=&(((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_68=&((v_t)->f_channels);size_t dn_tmp_69=v_i;&dn_tmp_68->data[dn_index(dn_tmp_69,2)]; }))).f_transition);*dn_tmp_70=dn_tmp_71; }
}
else{
}
}
 }
}

return 0;
}
static void dn_cb_tube_config(dn_r_Tube * v_t,size_t v_typ,size_t v_topology,double v_driveDb,double v_bias,double v_sag,double v_wet) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_tube_config(v_t,v_typ,v_topology,v_driveDb,v_bias,v_sag,v_wet);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static double dn_f_tube_lookup(dn_r_Tube const * v_t,size_t v_typ,double v_x,double v_bias) {
{
if(({ size_t dn_tmp_0=v_typ;
size_t dn_tmp_1=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); })){
{ double dn_tmp_2=v_x;return dn_tmp_2; }
}
else{
}
double v_pos=({ double dn_tmp_3=({ double dn_tmp_4=({ double dn_tmp_5=v_x;
double dn_tmp_6=(-(((double)(2.5))));
double dn_tmp_7=((double)(2.5));
dn_f_min_f(dn_tmp_5,dn_tmp_6,dn_tmp_7); });
double dn_tmp_8=({ double dn_tmp_9=((double)(2048.0));
double dn_tmp_10=((double)(5.0));
(dn_tmp_9 / dn_tmp_10); });
(dn_tmp_4 * dn_tmp_8); });
double dn_tmp_11=((double)(1024.0));
(dn_tmp_3 + dn_tmp_11); });
size_t v_index=({ double dn_tmp_12=({ double dn_tmp_13=({ double dn_tmp_14=v_pos;
double dn_tmp_15=((double)(0.0));
double dn_tmp_16=((double)(2047.0));
dn_f_min_f(dn_tmp_14,dn_tmp_15,dn_tmp_16); });
trunc(dn_tmp_13); });
long double dn_n=(long double)dn_tmp_12;if(!isfinite(dn_tmp_12)||truncl(dn_n)!=dn_n||dn_n<0.0L||dn_n>=ldexpl(1.0L,64))dn_fail("numeric conversion out of range");(size_t)dn_tmp_12; });
double v_fractional=({ double dn_tmp_17=v_pos;
double dn_tmp_18=({ size_t dn_tmp_19=v_index;
(double)dn_tmp_19; });
(dn_tmp_17 - dn_tmp_18); });
double v_biasPos=({ double dn_tmp_20=({ double dn_tmp_21=v_bias;
double dn_tmp_22=(-(((double)(1.0))));
double dn_tmp_23=((double)(1.0));
dn_f_min_f(dn_tmp_21,dn_tmp_22,dn_tmp_23); });
double dn_tmp_24=((double)(1.0));
(dn_tmp_20 + dn_tmp_24); });
size_t v_biasIndex=({ double dn_tmp_25=({ double dn_tmp_26=({ double dn_tmp_27=v_biasPos;
double dn_tmp_28=((double)(0.0));
double dn_tmp_29=((double)(1.0));
dn_f_min_f(dn_tmp_27,dn_tmp_28,dn_tmp_29); });
trunc(dn_tmp_26); });
long double dn_n=(long double)dn_tmp_25;if(!isfinite(dn_tmp_25)||truncl(dn_n)!=dn_n||dn_n<0.0L||dn_n>=ldexpl(1.0L,64))dn_fail("numeric conversion out of range");(size_t)dn_tmp_25; });
double v_biasFraction=({ double dn_tmp_30=v_biasPos;
double dn_tmp_31=({ size_t dn_tmp_32=v_biasIndex;
(double)dn_tmp_32; });
(dn_tmp_30 - dn_tmp_31); });
size_t v_row=({ size_t dn_tmp_33=({ size_t dn_tmp_34=({ size_t dn_tmp_35=({ size_t dn_tmp_36=v_typ;
size_t dn_tmp_37=((size_t)(1ULL));
dn_USize_sub(dn_tmp_36,dn_tmp_37); });
size_t dn_tmp_38=((size_t)(6147ULL));
dn_USize_mul(dn_tmp_35,dn_tmp_38); });
size_t dn_tmp_39=({ size_t dn_tmp_40=v_biasIndex;
size_t dn_tmp_41=((size_t)(2049ULL));
dn_USize_mul(dn_tmp_40,dn_tmp_41); });
dn_USize_add(dn_tmp_34,dn_tmp_39); });
size_t dn_tmp_42=v_index;
dn_USize_add(dn_tmp_33,dn_tmp_42); });
size_t v_row2=({ size_t dn_tmp_43=v_row;
size_t dn_tmp_44=((size_t)(2049ULL));
dn_USize_add(dn_tmp_43,dn_tmp_44); });
double v_u=({ double dn_tmp_45=({ double dn_tmp_46=({ float dn_tmp_47=(*({ dn_t_buf_F32 volatile *dn_tmp_48=&((v_t)->f_curves);size_t dn_tmp_49=v_row;&dn_tmp_48->data[dn_index(dn_tmp_49,dn_tmp_48->len)]; }));
(double)dn_tmp_47; });
double dn_tmp_50=({ double dn_tmp_51=((double)(1.0));
double dn_tmp_52=v_fractional;
(dn_tmp_51 - dn_tmp_52); });
(dn_tmp_46 * dn_tmp_50); });
double dn_tmp_53=({ double dn_tmp_54=({ float dn_tmp_55=(*({ dn_t_buf_F32 volatile *dn_tmp_56=&((v_t)->f_curves);size_t dn_tmp_57=({ size_t dn_tmp_58=v_row;
size_t dn_tmp_59=((size_t)(1ULL));
dn_USize_add(dn_tmp_58,dn_tmp_59); });&dn_tmp_56->data[dn_index(dn_tmp_57,dn_tmp_56->len)]; }));
(double)dn_tmp_55; });
double dn_tmp_60=v_fractional;
(dn_tmp_54 * dn_tmp_60); });
(dn_tmp_45 + dn_tmp_53); });
double v_v=({ double dn_tmp_61=({ double dn_tmp_62=({ float dn_tmp_63=(*({ dn_t_buf_F32 volatile *dn_tmp_64=&((v_t)->f_curves);size_t dn_tmp_65=v_row2;&dn_tmp_64->data[dn_index(dn_tmp_65,dn_tmp_64->len)]; }));
(double)dn_tmp_63; });
double dn_tmp_66=({ double dn_tmp_67=((double)(1.0));
double dn_tmp_68=v_fractional;
(dn_tmp_67 - dn_tmp_68); });
(dn_tmp_62 * dn_tmp_66); });
double dn_tmp_69=({ double dn_tmp_70=({ float dn_tmp_71=(*({ dn_t_buf_F32 volatile *dn_tmp_72=&((v_t)->f_curves);size_t dn_tmp_73=({ size_t dn_tmp_74=v_row2;
size_t dn_tmp_75=((size_t)(1ULL));
dn_USize_add(dn_tmp_74,dn_tmp_75); });&dn_tmp_72->data[dn_index(dn_tmp_73,dn_tmp_72->len)]; }));
(double)dn_tmp_71; });
double dn_tmp_76=v_fractional;
(dn_tmp_70 * dn_tmp_76); });
(dn_tmp_61 + dn_tmp_69); });
{ double dn_tmp_83=({ double dn_tmp_77=v_u;
double dn_tmp_78=({ double dn_tmp_79=({ double dn_tmp_80=v_v;
double dn_tmp_81=v_u;
(dn_tmp_80 - dn_tmp_81); });
double dn_tmp_82=v_biasFraction;
(dn_tmp_79 * dn_tmp_82); });
(dn_tmp_77 + dn_tmp_78); });return dn_tmp_83; }
}

dn_fail("missing native return");
}
static double dn_cb_tube_lookup(dn_r_Tube const * v_t,size_t v_typ,double v_x,double v_bias) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_tube_lookup(v_t,v_typ,v_x,v_bias);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_tube_evaluate(dn_r_Tube const * v_t,size_t v_typ,size_t v_topology,double v_x,double v_bias) {
{
if(({ size_t dn_tmp_0=v_typ;
size_t dn_tmp_1=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); })){
{ double dn_tmp_2=v_x;return dn_tmp_2; }
}
else{
}
if(({ size_t dn_tmp_3=v_topology;
size_t dn_tmp_4=((size_t)(0ULL));
(dn_tmp_3 == dn_tmp_4); })){
{ double dn_tmp_9=({ dn_r_Tube const * dn_tmp_5=v_t;
size_t dn_tmp_6=v_typ;
double dn_tmp_7=v_x;
double dn_tmp_8=v_bias;
dn_f_tube_lookup(dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8); });return dn_tmp_9; }
}
else{
}
{ double dn_tmp_22=({ double dn_tmp_10=((double)(0.5));
double dn_tmp_11=({ double dn_tmp_12=({ dn_r_Tube const * dn_tmp_13=v_t;
size_t dn_tmp_14=v_typ;
double dn_tmp_15=v_x;
double dn_tmp_16=v_bias;
dn_f_tube_lookup(dn_tmp_13,dn_tmp_14,dn_tmp_15,dn_tmp_16); });
double dn_tmp_17=({ dn_r_Tube const * dn_tmp_18=v_t;
size_t dn_tmp_19=v_typ;
double dn_tmp_20=(-(v_x));
double dn_tmp_21=v_bias;
dn_f_tube_lookup(dn_tmp_18,dn_tmp_19,dn_tmp_20,dn_tmp_21); });
(dn_tmp_12 - dn_tmp_17); });
(dn_tmp_10 * dn_tmp_11); });return dn_tmp_22; }
}

dn_fail("missing native return");
}
static double dn_cb_tube_evaluate(dn_r_Tube const * v_t,size_t v_typ,size_t v_topology,double v_x,double v_bias) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_tube_evaluate(v_t,v_typ,v_topology,v_x,v_bias);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_tube_process(dn_r_Tube * v_t,double v_x,size_t v_ch) {
{
if((!(({ double dn_tmp_0=v_x;
isfinite(dn_tmp_0); })))){
{ double dn_tmp_1=((double)(0.0));return dn_tmp_1; }
}
else{
}
if(((({ size_t dn_tmp_2=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_3=&((v_t)->f_channels);size_t dn_tmp_4=v_ch;&dn_tmp_3->data[dn_index(dn_tmp_4,2)]; }))).f_newType;
size_t dn_tmp_5=((size_t)(0ULL));
(dn_tmp_2 == dn_tmp_5); }) && ({ size_t dn_tmp_6=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_7=&((v_t)->f_channels);size_t dn_tmp_8=v_ch;&dn_tmp_7->data[dn_index(dn_tmp_8,2)]; }))).f_oldType;
size_t dn_tmp_9=((size_t)(0ULL));
(dn_tmp_6 == dn_tmp_9); })) && ({ double dn_tmp_10=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_11=&((v_t)->f_channels);size_t dn_tmp_12=v_ch;&dn_tmp_11->data[dn_index(dn_tmp_12,2)]; }))).f_wet;
double dn_tmp_13=((double)(1.0e-10));
(dn_tmp_10 < dn_tmp_13); }))){
{ double dn_tmp_14=v_x;return dn_tmp_14; }
}
else{
}
{ double dn_tmp_28=({ double dn_tmp_17=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_18=&((v_t)->f_channels);size_t dn_tmp_19=v_ch;&dn_tmp_18->data[dn_index(dn_tmp_19,2)]; }))).f_drive;
double dn_tmp_20=({ double dn_tmp_21=(v_t)->f_smoothing;
double dn_tmp_22=({ double dn_tmp_23=(v_t)->f_desiredDrive;
double dn_tmp_24=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_25=&((v_t)->f_channels);size_t dn_tmp_26=v_ch;&dn_tmp_25->data[dn_index(dn_tmp_26,2)]; }))).f_drive;
(dn_tmp_23 - dn_tmp_24); });
(dn_tmp_21 * dn_tmp_22); });
(dn_tmp_17 + dn_tmp_20); });double *dn_tmp_27=&(((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_15=&((v_t)->f_channels);size_t dn_tmp_16=v_ch;&dn_tmp_15->data[dn_index(dn_tmp_16,2)]; }))).f_drive);*dn_tmp_27=dn_tmp_28; }
{ double dn_tmp_42=({ double dn_tmp_31=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_32=&((v_t)->f_channels);size_t dn_tmp_33=v_ch;&dn_tmp_32->data[dn_index(dn_tmp_33,2)]; }))).f_bias;
double dn_tmp_34=({ double dn_tmp_35=(v_t)->f_smoothing;
double dn_tmp_36=({ double dn_tmp_37=(v_t)->f_desiredBias;
double dn_tmp_38=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_39=&((v_t)->f_channels);size_t dn_tmp_40=v_ch;&dn_tmp_39->data[dn_index(dn_tmp_40,2)]; }))).f_bias;
(dn_tmp_37 - dn_tmp_38); });
(dn_tmp_35 * dn_tmp_36); });
(dn_tmp_31 + dn_tmp_34); });double *dn_tmp_41=&(((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_29=&((v_t)->f_channels);size_t dn_tmp_30=v_ch;&dn_tmp_29->data[dn_index(dn_tmp_30,2)]; }))).f_bias);*dn_tmp_41=dn_tmp_42; }
{ double dn_tmp_56=({ double dn_tmp_45=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_46=&((v_t)->f_channels);size_t dn_tmp_47=v_ch;&dn_tmp_46->data[dn_index(dn_tmp_47,2)]; }))).f_sag;
double dn_tmp_48=({ double dn_tmp_49=(v_t)->f_smoothing;
double dn_tmp_50=({ double dn_tmp_51=(v_t)->f_desiredSag;
double dn_tmp_52=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_53=&((v_t)->f_channels);size_t dn_tmp_54=v_ch;&dn_tmp_53->data[dn_index(dn_tmp_54,2)]; }))).f_sag;
(dn_tmp_51 - dn_tmp_52); });
(dn_tmp_49 * dn_tmp_50); });
(dn_tmp_45 + dn_tmp_48); });double *dn_tmp_55=&(((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_43=&((v_t)->f_channels);size_t dn_tmp_44=v_ch;&dn_tmp_43->data[dn_index(dn_tmp_44,2)]; }))).f_sag);*dn_tmp_55=dn_tmp_56; }
double v_targetWet=((double)(0.0));
if(({ size_t dn_tmp_57=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_58=&((v_t)->f_channels);size_t dn_tmp_59=v_ch;&dn_tmp_58->data[dn_index(dn_tmp_59,2)]; }))).f_newType;
size_t dn_tmp_60=((size_t)(0ULL));
(dn_tmp_57 > dn_tmp_60); })){
{ double dn_tmp_62=(v_t)->f_desiredWet;double *dn_tmp_61=&(v_targetWet);*dn_tmp_61=dn_tmp_62; }
}
else{
}
{ double dn_tmp_76=({ double dn_tmp_65=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_66=&((v_t)->f_channels);size_t dn_tmp_67=v_ch;&dn_tmp_66->data[dn_index(dn_tmp_67,2)]; }))).f_wet;
double dn_tmp_68=({ double dn_tmp_69=(v_t)->f_smoothing;
double dn_tmp_70=({ double dn_tmp_71=v_targetWet;
double dn_tmp_72=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_73=&((v_t)->f_channels);size_t dn_tmp_74=v_ch;&dn_tmp_73->data[dn_index(dn_tmp_74,2)]; }))).f_wet;
(dn_tmp_71 - dn_tmp_72); });
(dn_tmp_69 * dn_tmp_70); });
(dn_tmp_65 + dn_tmp_68); });double *dn_tmp_75=&(((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_63=&((v_t)->f_channels);size_t dn_tmp_64=v_ch;&dn_tmp_63->data[dn_index(dn_tmp_64,2)]; }))).f_wet);*dn_tmp_75=dn_tmp_76; }
double v_level=({ double dn_tmp_77=({ double dn_tmp_78=({ double dn_tmp_79=v_x;
double dn_tmp_80=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_81=&((v_t)->f_channels);size_t dn_tmp_82=v_ch;&dn_tmp_81->data[dn_index(dn_tmp_82,2)]; }))).f_drive;
(dn_tmp_79 * dn_tmp_80); });
fabs(dn_tmp_78); });
double dn_tmp_83=((double)(0.0));
double dn_tmp_84=((double)(4.0));
dn_f_min_f(dn_tmp_77,dn_tmp_83,dn_tmp_84); });
double v_alpha=(v_t)->f_envelopeRelease;
if(({ double dn_tmp_85=v_level;
double dn_tmp_86=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_87=&((v_t)->f_channels);size_t dn_tmp_88=v_ch;&dn_tmp_87->data[dn_index(dn_tmp_88,2)]; }))).f_envelope;
(dn_tmp_85 > dn_tmp_86); })){
{ double dn_tmp_90=(v_t)->f_envelopeAttack;double *dn_tmp_89=&(v_alpha);*dn_tmp_89=dn_tmp_90; }
}
else{
}
{ double dn_tmp_104=({ double dn_tmp_93=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_94=&((v_t)->f_channels);size_t dn_tmp_95=v_ch;&dn_tmp_94->data[dn_index(dn_tmp_95,2)]; }))).f_envelope;
double dn_tmp_96=({ double dn_tmp_97=v_alpha;
double dn_tmp_98=({ double dn_tmp_99=v_level;
double dn_tmp_100=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_101=&((v_t)->f_channels);size_t dn_tmp_102=v_ch;&dn_tmp_101->data[dn_index(dn_tmp_102,2)]; }))).f_envelope;
(dn_tmp_99 - dn_tmp_100); });
(dn_tmp_97 * dn_tmp_98); });
(dn_tmp_93 + dn_tmp_96); });double *dn_tmp_103=&(((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_91=&((v_t)->f_channels);size_t dn_tmp_92=v_ch;&dn_tmp_91->data[dn_index(dn_tmp_92,2)]; }))).f_envelope);*dn_tmp_103=dn_tmp_104; }
double v_adjBias=({ double dn_tmp_105=({ double dn_tmp_106=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_107=&((v_t)->f_channels);size_t dn_tmp_108=v_ch;&dn_tmp_107->data[dn_index(dn_tmp_108,2)]; }))).f_bias;
double dn_tmp_109=({ double dn_tmp_110=({ double dn_tmp_111=((double)(0.27));
double dn_tmp_112=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_113=&((v_t)->f_channels);size_t dn_tmp_114=v_ch;&dn_tmp_113->data[dn_index(dn_tmp_114,2)]; }))).f_sag;
(dn_tmp_111 * dn_tmp_112); });
double dn_tmp_115=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_116=&((v_t)->f_channels);size_t dn_tmp_117=v_ch;&dn_tmp_116->data[dn_index(dn_tmp_117,2)]; }))).f_envelope;
(dn_tmp_110 * dn_tmp_115); });
(dn_tmp_106 - dn_tmp_109); });
double dn_tmp_118=(-(((double)(1.0))));
double dn_tmp_119=((double)(1.0));
dn_f_min_f(dn_tmp_105,dn_tmp_118,dn_tmp_119); });
double v_driven=({ double dn_tmp_120=({ double dn_tmp_121=v_x;
double dn_tmp_122=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_123=&((v_t)->f_channels);size_t dn_tmp_124=v_ch;&dn_tmp_123->data[dn_index(dn_tmp_124,2)]; }))).f_drive;
(dn_tmp_121 * dn_tmp_122); });
double dn_tmp_125=(-(((double)(8.0))));
double dn_tmp_126=((double)(8.0));
dn_f_min_f(dn_tmp_120,dn_tmp_125,dn_tmp_126); });
double v_newValue=({ dn_r_Tube * dn_tmp_127=v_t;
size_t dn_tmp_128=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_129=&((v_t)->f_channels);size_t dn_tmp_130=v_ch;&dn_tmp_129->data[dn_index(dn_tmp_130,2)]; }))).f_newType;
size_t dn_tmp_131=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_132=&((v_t)->f_channels);size_t dn_tmp_133=v_ch;&dn_tmp_132->data[dn_index(dn_tmp_133,2)]; }))).f_newTopology;
double dn_tmp_134=v_driven;
double dn_tmp_135=v_adjBias;
dn_f_tube_evaluate(dn_tmp_127,dn_tmp_128,dn_tmp_131,dn_tmp_134,dn_tmp_135); });
double v_processed=v_newValue;
if(({ double dn_tmp_136=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_137=&((v_t)->f_channels);size_t dn_tmp_138=v_ch;&dn_tmp_137->data[dn_index(dn_tmp_138,2)]; }))).f_transition;
double dn_tmp_139=((double)(1.0));
(dn_tmp_136 < dn_tmp_139); })){
double v_oldValue=({ dn_r_Tube * dn_tmp_140=v_t;
size_t dn_tmp_141=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_142=&((v_t)->f_channels);size_t dn_tmp_143=v_ch;&dn_tmp_142->data[dn_index(dn_tmp_143,2)]; }))).f_oldType;
size_t dn_tmp_144=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_145=&((v_t)->f_channels);size_t dn_tmp_146=v_ch;&dn_tmp_145->data[dn_index(dn_tmp_146,2)]; }))).f_oldTopology;
double dn_tmp_147=v_driven;
double dn_tmp_148=v_adjBias;
dn_f_tube_evaluate(dn_tmp_140,dn_tmp_141,dn_tmp_144,dn_tmp_147,dn_tmp_148); });
{ double dn_tmp_158=({ double dn_tmp_149=v_oldValue;
double dn_tmp_150=({ double dn_tmp_151=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_152=&((v_t)->f_channels);size_t dn_tmp_153=v_ch;&dn_tmp_152->data[dn_index(dn_tmp_153,2)]; }))).f_transition;
double dn_tmp_154=({ double dn_tmp_155=v_newValue;
double dn_tmp_156=v_oldValue;
(dn_tmp_155 - dn_tmp_156); });
(dn_tmp_151 * dn_tmp_154); });
(dn_tmp_149 + dn_tmp_150); });double *dn_tmp_157=&(v_processed);*dn_tmp_157=dn_tmp_158; }
{ double dn_tmp_169=({ double dn_tmp_161=({ double dn_tmp_162=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_163=&((v_t)->f_channels);size_t dn_tmp_164=v_ch;&dn_tmp_163->data[dn_index(dn_tmp_164,2)]; }))).f_transition;
double dn_tmp_165=(v_t)->f_transitionStep;
(dn_tmp_162 + dn_tmp_165); });
double dn_tmp_166=((double)(0.0));
double dn_tmp_167=((double)(1.0));
dn_f_min_f(dn_tmp_161,dn_tmp_166,dn_tmp_167); });double *dn_tmp_168=&(((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_159=&((v_t)->f_channels);size_t dn_tmp_160=v_ch;&dn_tmp_159->data[dn_index(dn_tmp_160,2)]; }))).f_transition);*dn_tmp_168=dn_tmp_169; }
if(({ double dn_tmp_170=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_171=&((v_t)->f_channels);size_t dn_tmp_172=v_ch;&dn_tmp_171->data[dn_index(dn_tmp_172,2)]; }))).f_transition;
double dn_tmp_173=((double)(1.0));
(dn_tmp_170 == dn_tmp_173); })){
{ size_t dn_tmp_179=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_176=&((v_t)->f_channels);size_t dn_tmp_177=v_ch;&dn_tmp_176->data[dn_index(dn_tmp_177,2)]; }))).f_newType;size_t *dn_tmp_178=&(((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_174=&((v_t)->f_channels);size_t dn_tmp_175=v_ch;&dn_tmp_174->data[dn_index(dn_tmp_175,2)]; }))).f_oldType);*dn_tmp_178=dn_tmp_179; }
{ size_t dn_tmp_185=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_182=&((v_t)->f_channels);size_t dn_tmp_183=v_ch;&dn_tmp_182->data[dn_index(dn_tmp_183,2)]; }))).f_newTopology;size_t *dn_tmp_184=&(((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_180=&((v_t)->f_channels);size_t dn_tmp_181=v_ch;&dn_tmp_180->data[dn_index(dn_tmp_181,2)]; }))).f_oldTopology);*dn_tmp_184=dn_tmp_185; }
}
else{
}
}
else{
}
double v_compensated=({ double dn_tmp_186=v_processed;
double dn_tmp_187=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_188=&((v_t)->f_channels);size_t dn_tmp_189=v_ch;&dn_tmp_188->data[dn_index(dn_tmp_189,2)]; }))).f_drive;
(dn_tmp_186 / dn_tmp_187); });
double v_result=({ double dn_tmp_190=v_x;
double dn_tmp_191=({ double dn_tmp_192=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_193=&((v_t)->f_channels);size_t dn_tmp_194=v_ch;&dn_tmp_193->data[dn_index(dn_tmp_194,2)]; }))).f_wet;
double dn_tmp_195=({ double dn_tmp_196=v_compensated;
double dn_tmp_197=v_x;
(dn_tmp_196 - dn_tmp_197); });
(dn_tmp_192 * dn_tmp_195); });
(dn_tmp_190 + dn_tmp_191); });
if((!(({ double dn_tmp_198=v_result;
isfinite(dn_tmp_198); })))){
{ double dn_tmp_199=((double)(0.0));return dn_tmp_199; }
}
else{
}
{ double dn_tmp_203=({ double dn_tmp_200=v_result;
double dn_tmp_201=(-(((double)(32.0))));
double dn_tmp_202=((double)(32.0));
dn_f_min_f(dn_tmp_200,dn_tmp_201,dn_tmp_202); });return dn_tmp_203; }
}

dn_fail("missing native return");
}
static double dn_cb_tube_process(dn_r_Tube * v_t,double v_x,size_t v_ch) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_tube_process(v_t,v_x,v_ch);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_Biquad dn_f_band_blank_filter(void) {
{
{ dn_r_Biquad dn_tmp_7=({ double dn_tmp_0=((double)(1.0));
double dn_tmp_1=((double)(0.0));
double dn_tmp_2=((double)(0.0));
double dn_tmp_3=((double)(0.0));
double dn_tmp_4=((double)(0.0));
double dn_tmp_5=((double)(0.0));
double dn_tmp_6=((double)(0.0));
((dn_r_Biquad){dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_Biquad dn_cb_band_blank_filter(void) {
 if(!dn_enter()){return (dn_r_Biquad){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_Biquad){0};}
 dn_r_Biquad dn_result=dn_f_band_blank_filter();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_BandChannel dn_f_band_blank_channel(void) {
{
{ dn_r_BandChannel dn_tmp_13=({ dn_t_arr8_r6_Biquad dn_tmp_0=({ dn_r_Biquad dn_tmp_1=({ dn_f_band_blank_filter(); });
dn_r_Biquad dn_tmp_2=({ dn_f_band_blank_filter(); });
dn_r_Biquad dn_tmp_3=({ dn_f_band_blank_filter(); });
dn_r_Biquad dn_tmp_4=({ dn_f_band_blank_filter(); });
dn_r_Biquad dn_tmp_5=({ dn_f_band_blank_filter(); });
dn_r_Biquad dn_tmp_6=({ dn_f_band_blank_filter(); });
dn_r_Biquad dn_tmp_7=({ dn_f_band_blank_filter(); });
dn_r_Biquad dn_tmp_8=({ dn_f_band_blank_filter(); });
((dn_t_arr8_r6_Biquad){{dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8}}); });
double dn_tmp_9=((double)(250.0));
double dn_tmp_10=((double)(3000.0));
int32_t dn_tmp_11=({ int32_t dn_tmp_12=((int32_t)(0ULL));
if((__int128)dn_tmp_12<(-((__int128)1<<31))||(__int128)dn_tmp_12>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_12; });
((dn_r_BandChannel){dn_tmp_0,dn_tmp_9,dn_tmp_10,dn_tmp_11}); });return dn_tmp_13; }
}

dn_fail("missing native return");
}
static dn_r_BandChannel dn_cb_band_blank_channel(void) {
 if(!dn_enter()){return (dn_r_BandChannel){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_BandChannel){0};}
 dn_r_BandChannel dn_result=dn_f_band_blank_channel();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_band_coefficients(dn_r_Biquad * v_b,double v_freq,double v_rate,bool v_hp) {
{
double v_angle=({ double dn_tmp_0=({ double dn_tmp_1=({ double dn_tmp_2=((double)(2.0));
double dn_tmp_3=({ dn_f_pi(); });
(dn_tmp_2 * dn_tmp_3); });
double dn_tmp_4=({ double dn_tmp_5=v_freq;
double dn_tmp_6=((double)(10.0));
double dn_tmp_7=({ double dn_tmp_8=((double)(0.45));
double dn_tmp_9=v_rate;
(dn_tmp_8 * dn_tmp_9); });
dn_f_min_f(dn_tmp_5,dn_tmp_6,dn_tmp_7); });
(dn_tmp_1 * dn_tmp_4); });
double dn_tmp_10=v_rate;
(dn_tmp_0 / dn_tmp_10); });
double v_c=({ double dn_tmp_11=v_angle;
cos(dn_tmp_11); });
double v_sn=({ double dn_tmp_12=v_angle;
sin(dn_tmp_12); });
double v_alpha=({ double dn_tmp_13=v_sn;
double dn_tmp_14=({ double dn_tmp_15=((double)(2.0));
sqrt(dn_tmp_15); });
(dn_tmp_13 / dn_tmp_14); });
double v_inv=({ double dn_tmp_16=((double)(1.0));
double dn_tmp_17=({ double dn_tmp_18=((double)(1.0));
double dn_tmp_19=v_alpha;
(dn_tmp_18 + dn_tmp_19); });
(dn_tmp_16 / dn_tmp_17); });
if(v_hp){
{ double dn_tmp_27=({ double dn_tmp_20=({ double dn_tmp_21=({ double dn_tmp_22=((double)(1.0));
double dn_tmp_23=v_c;
(dn_tmp_22 + dn_tmp_23); });
double dn_tmp_24=((double)(0.5));
(dn_tmp_21 * dn_tmp_24); });
double dn_tmp_25=v_inv;
(dn_tmp_20 * dn_tmp_25); });double *dn_tmp_26=&((v_b)->f_b0);*dn_tmp_26=dn_tmp_27; }
{ double dn_tmp_33=({ double dn_tmp_28=(-(({ double dn_tmp_29=((double)(1.0));
double dn_tmp_30=v_c;
(dn_tmp_29 + dn_tmp_30); })));
double dn_tmp_31=v_inv;
(dn_tmp_28 * dn_tmp_31); });double *dn_tmp_32=&((v_b)->f_b1);*dn_tmp_32=dn_tmp_33; }
}
else{
{ double dn_tmp_41=({ double dn_tmp_34=({ double dn_tmp_35=({ double dn_tmp_36=((double)(1.0));
double dn_tmp_37=v_c;
(dn_tmp_36 - dn_tmp_37); });
double dn_tmp_38=((double)(0.5));
(dn_tmp_35 * dn_tmp_38); });
double dn_tmp_39=v_inv;
(dn_tmp_34 * dn_tmp_39); });double *dn_tmp_40=&((v_b)->f_b0);*dn_tmp_40=dn_tmp_41; }
{ double dn_tmp_47=({ double dn_tmp_42=({ double dn_tmp_43=((double)(1.0));
double dn_tmp_44=v_c;
(dn_tmp_43 - dn_tmp_44); });
double dn_tmp_45=v_inv;
(dn_tmp_42 * dn_tmp_45); });double *dn_tmp_46=&((v_b)->f_b1);*dn_tmp_46=dn_tmp_47; }
}
{ double dn_tmp_49=(v_b)->f_b0;double *dn_tmp_48=&((v_b)->f_b2);*dn_tmp_48=dn_tmp_49; }
{ double dn_tmp_55=({ double dn_tmp_50=({ double dn_tmp_51=(-(((double)(2.0))));
double dn_tmp_52=v_c;
(dn_tmp_51 * dn_tmp_52); });
double dn_tmp_53=v_inv;
(dn_tmp_50 * dn_tmp_53); });double *dn_tmp_54=&((v_b)->f_a1);*dn_tmp_54=dn_tmp_55; }
{ double dn_tmp_61=({ double dn_tmp_56=({ double dn_tmp_57=((double)(1.0));
double dn_tmp_58=v_alpha;
(dn_tmp_57 - dn_tmp_58); });
double dn_tmp_59=v_inv;
(dn_tmp_56 * dn_tmp_59); });double *dn_tmp_60=&((v_b)->f_a2);*dn_tmp_60=dn_tmp_61; }
}

return 0;
}
static void dn_cb_band_coefficients(dn_r_Biquad * v_b,double v_freq,double v_rate,bool v_hp) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_band_coefficients(v_b,v_freq,v_rate,v_hp);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_band_update_coeff(dn_r_ThreeBand * v_t,size_t v_ch) {
{
{ size_t dn_tmp_1=((size_t)(0ULL));size_t dn_tmp_0=((size_t)(2ULL));for(size_t v_i=dn_tmp_1;v_i<dn_tmp_0;v_i++){
(void)(({ dn_r_Biquad * dn_tmp_2=(&((*({ dn_t_arr8_r6_Biquad *dn_tmp_3=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_5=&((v_t)->f_channels);size_t dn_tmp_6=v_ch;&dn_tmp_5->data[dn_index(dn_tmp_6,2)]; }))).f_filters);size_t dn_tmp_4=v_i;&dn_tmp_3->data[dn_index(dn_tmp_4,8)]; }))));
double dn_tmp_7=((*({ dn_t_arr2_r11_BandChannel *dn_tmp_8=&((v_t)->f_channels);size_t dn_tmp_9=v_ch;&dn_tmp_8->data[dn_index(dn_tmp_9,2)]; }))).f_currentLower;
double dn_tmp_10=(v_t)->f_rate;
bool dn_tmp_11=0;
dn_f_band_coefficients(dn_tmp_2,dn_tmp_7,dn_tmp_10,dn_tmp_11); }));
(void)(({ dn_r_Biquad * dn_tmp_12=(&((*({ dn_t_arr8_r6_Biquad *dn_tmp_13=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_15=&((v_t)->f_channels);size_t dn_tmp_16=v_ch;&dn_tmp_15->data[dn_index(dn_tmp_16,2)]; }))).f_filters);size_t dn_tmp_14=({ size_t dn_tmp_17=v_i;
size_t dn_tmp_18=((size_t)(2ULL));
dn_USize_add(dn_tmp_17,dn_tmp_18); });&dn_tmp_13->data[dn_index(dn_tmp_14,8)]; }))));
double dn_tmp_19=((*({ dn_t_arr2_r11_BandChannel *dn_tmp_20=&((v_t)->f_channels);size_t dn_tmp_21=v_ch;&dn_tmp_20->data[dn_index(dn_tmp_21,2)]; }))).f_currentLower;
double dn_tmp_22=(v_t)->f_rate;
bool dn_tmp_23=1;
dn_f_band_coefficients(dn_tmp_12,dn_tmp_19,dn_tmp_22,dn_tmp_23); }));
(void)(({ dn_r_Biquad * dn_tmp_24=(&((*({ dn_t_arr8_r6_Biquad *dn_tmp_25=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_27=&((v_t)->f_channels);size_t dn_tmp_28=v_ch;&dn_tmp_27->data[dn_index(dn_tmp_28,2)]; }))).f_filters);size_t dn_tmp_26=({ size_t dn_tmp_29=v_i;
size_t dn_tmp_30=((size_t)(4ULL));
dn_USize_add(dn_tmp_29,dn_tmp_30); });&dn_tmp_25->data[dn_index(dn_tmp_26,8)]; }))));
double dn_tmp_31=((*({ dn_t_arr2_r11_BandChannel *dn_tmp_32=&((v_t)->f_channels);size_t dn_tmp_33=v_ch;&dn_tmp_32->data[dn_index(dn_tmp_33,2)]; }))).f_currentUpper;
double dn_tmp_34=(v_t)->f_rate;
bool dn_tmp_35=0;
dn_f_band_coefficients(dn_tmp_24,dn_tmp_31,dn_tmp_34,dn_tmp_35); }));
(void)(({ dn_r_Biquad * dn_tmp_36=(&((*({ dn_t_arr8_r6_Biquad *dn_tmp_37=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_39=&((v_t)->f_channels);size_t dn_tmp_40=v_ch;&dn_tmp_39->data[dn_index(dn_tmp_40,2)]; }))).f_filters);size_t dn_tmp_38=({ size_t dn_tmp_41=v_i;
size_t dn_tmp_42=((size_t)(6ULL));
dn_USize_add(dn_tmp_41,dn_tmp_42); });&dn_tmp_37->data[dn_index(dn_tmp_38,8)]; }))));
double dn_tmp_43=((*({ dn_t_arr2_r11_BandChannel *dn_tmp_44=&((v_t)->f_channels);size_t dn_tmp_45=v_ch;&dn_tmp_44->data[dn_index(dn_tmp_45,2)]; }))).f_currentUpper;
double dn_tmp_46=(v_t)->f_rate;
bool dn_tmp_47=1;
dn_f_band_coefficients(dn_tmp_36,dn_tmp_43,dn_tmp_46,dn_tmp_47); }));
}
 }
}

return 0;
}
static void dn_cb_band_update_coeff(dn_r_ThreeBand * v_t,size_t v_ch) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_band_update_coeff(v_t,v_ch);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_band_reset(dn_r_ThreeBand * v_t) {
{
{ size_t dn_tmp_1=((size_t)(0ULL));size_t dn_tmp_0=((size_t)(3ULL));for(size_t v_i=dn_tmp_1;v_i<dn_tmp_0;v_i++){
(void)(({ dn_r_Tube * dn_tmp_2=(&((*({ dn_t_arr3_r4_Tube volatile *dn_tmp_3=&((v_t)->f_stages);size_t dn_tmp_4=v_i;&dn_tmp_3->data[dn_index(dn_tmp_4,3)]; }))));
dn_f_tube_reset(dn_tmp_2); }));
}
 }
{ size_t dn_tmp_6=((size_t)(0ULL));size_t dn_tmp_5=((size_t)(2ULL));for(size_t v_ch=dn_tmp_6;v_ch<dn_tmp_5;v_ch++){
{ size_t dn_tmp_10=((size_t)(0ULL));size_t *dn_tmp_9=&((*({ dn_t_arr2_USize *dn_tmp_7=&((v_t)->f_releaseFrames);size_t dn_tmp_8=v_ch;&dn_tmp_7->data[dn_index(dn_tmp_8,2)]; })));*dn_tmp_9=dn_tmp_10; }
{ double dn_tmp_14=(v_t)->f_targetLower;double *dn_tmp_13=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_11=&((v_t)->f_channels);size_t dn_tmp_12=v_ch;&dn_tmp_11->data[dn_index(dn_tmp_12,2)]; }))).f_currentLower);*dn_tmp_13=dn_tmp_14; }
{ double dn_tmp_18=(v_t)->f_targetUpper;double *dn_tmp_17=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_15=&((v_t)->f_channels);size_t dn_tmp_16=v_ch;&dn_tmp_15->data[dn_index(dn_tmp_16,2)]; }))).f_currentUpper);*dn_tmp_17=dn_tmp_18; }
{ int32_t dn_tmp_23=({ int32_t dn_tmp_21=((int32_t)(0ULL));
if((__int128)dn_tmp_21<(-((__int128)1<<31))||(__int128)dn_tmp_21>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_21; });int32_t *dn_tmp_22=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_19=&((v_t)->f_channels);size_t dn_tmp_20=v_ch;&dn_tmp_19->data[dn_index(dn_tmp_20,2)]; }))).f_coefficientCounter);*dn_tmp_22=dn_tmp_23; }
{ size_t dn_tmp_25=((size_t)(0ULL));size_t dn_tmp_24=((size_t)(8ULL));for(size_t v_i=dn_tmp_25;v_i<dn_tmp_24;v_i++){
{ double dn_tmp_31=((double)(0.0));double *dn_tmp_30=&(((*({ dn_t_arr8_r6_Biquad *dn_tmp_26=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_28=&((v_t)->f_channels);size_t dn_tmp_29=v_ch;&dn_tmp_28->data[dn_index(dn_tmp_29,2)]; }))).f_filters);size_t dn_tmp_27=v_i;&dn_tmp_26->data[dn_index(dn_tmp_27,8)]; }))).f_z1);*dn_tmp_30=dn_tmp_31; }
{ double dn_tmp_37=((double)(0.0));double *dn_tmp_36=&(((*({ dn_t_arr8_r6_Biquad *dn_tmp_32=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_34=&((v_t)->f_channels);size_t dn_tmp_35=v_ch;&dn_tmp_34->data[dn_index(dn_tmp_35,2)]; }))).f_filters);size_t dn_tmp_33=v_i;&dn_tmp_32->data[dn_index(dn_tmp_33,8)]; }))).f_z2);*dn_tmp_36=dn_tmp_37; }
}
 }
(void)(({ dn_r_ThreeBand * dn_tmp_38=v_t;
size_t dn_tmp_39=v_ch;
dn_f_band_update_coeff(dn_tmp_38,dn_tmp_39); }));
}
 }
}

return 0;
}
static void dn_cb_band_reset(dn_r_ThreeBand * v_t) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_band_reset(v_t);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static dn_r_ThreeBand dn_f_band_create(double v_sr) {
{
dn_r_ThreeBand volatile v_out={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r9_ThreeBand(&v_out);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_out=({ dn_t_arr3_r4_Tube dn_tmp_2=({ dn_r_Tube dn_tmp_3=({ double dn_tmp_4=v_sr;
dn_f_tube_create(dn_tmp_4); });
dn_adopt_r4_Tube(&dn_tmp_3);
dn_r_Tube dn_tmp_5=({ double dn_tmp_6=v_sr;
dn_f_tube_create(dn_tmp_6); });
dn_adopt_r4_Tube(&dn_tmp_5);
dn_r_Tube dn_tmp_7=({ double dn_tmp_8=v_sr;
dn_f_tube_create(dn_tmp_8); });
dn_adopt_r4_Tube(&dn_tmp_7);
((dn_t_arr3_r4_Tube){{dn_tmp_3,dn_tmp_5,dn_tmp_7}}); });
dn_adopt_arr3_r4_Tube(&dn_tmp_2);
dn_t_arr2_r11_BandChannel dn_tmp_9=({ dn_r_BandChannel dn_tmp_10=({ dn_f_band_blank_channel(); });
dn_r_BandChannel dn_tmp_11=({ dn_f_band_blank_channel(); });
((dn_t_arr2_r11_BandChannel){{dn_tmp_10,dn_tmp_11}}); });
double dn_tmp_12=((double)(88200.0));
double dn_tmp_13=((double)(250.0));
double dn_tmp_14=((double)(3000.0));
double dn_tmp_15=((double)(1.0));
bool dn_tmp_16=0;
dn_t_arr2_USize dn_tmp_17=({ ((dn_t_arr2_USize){0}); });
dn_t_arr3_USize dn_tmp_18=({ ((dn_t_arr3_USize){0}); });
dn_t_arr3_F64 dn_tmp_19=({ ((dn_t_arr3_F64){0}); });
((dn_r_ThreeBand){dn_tmp_2,dn_tmp_9,dn_tmp_12,dn_tmp_13,dn_tmp_14,dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_18,dn_tmp_19}); });
(void)(({ dn_r_ThreeBand * dn_tmp_20=(&(v_out));
double dn_tmp_21=v_sr;
dn_f_band_set_rate(dn_tmp_20,dn_tmp_21); }));
(void)(({ dn_r_ThreeBand * dn_tmp_22=(&(v_out));
dn_f_band_reset(dn_tmp_22); }));
{ dn_r_ThreeBand dn_tmp_24=({ dn_r_ThreeBand dn_tmp_23=v_out;v_out=(dn_r_ThreeBand){0};dn_adopt_r9_ThreeBand(&dn_tmp_23);dn_tmp_23; });dn_adopt_r9_ThreeBand(&dn_tmp_24);dn_drop_r9_ThreeBand(&v_out);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_24; }
dn_drop_r9_ThreeBand(&v_out);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_r_ThreeBand dn_cb_band_create(double v_sr) {
 if(!dn_enter()){return (dn_r_ThreeBand){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ThreeBand){0};}
 dn_r_ThreeBand dn_result=dn_f_band_create(v_sr);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_band_set_rate(dn_r_ThreeBand * v_t,double v_sr) {
{
double v_rate=({ double dn_tmp_0=v_sr;
double dn_tmp_1=((double)(16000.0));
double dn_tmp_2=((double)(6144000.0));
dn_f_min_f(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
if(({ double dn_tmp_3=v_rate;
double dn_tmp_4=(v_t)->f_rate;
(dn_tmp_3 == dn_tmp_4); })){
{ uint8_t dn_tmp_5=0;return dn_tmp_5; }
}
else{
}
{ double dn_tmp_7=v_rate;double *dn_tmp_6=&((v_t)->f_rate);*dn_tmp_6=dn_tmp_7; }
{ double dn_tmp_16=({ double dn_tmp_8=((double)(1.0));
double dn_tmp_9=({ double dn_tmp_10=({ double dn_tmp_11=(-(((double)(64.0))));
double dn_tmp_12=({ double dn_tmp_13=((double)(0.025));
double dn_tmp_14=v_rate;
(dn_tmp_13 * dn_tmp_14); });
(dn_tmp_11 / dn_tmp_12); });
exp(dn_tmp_10); });
(dn_tmp_8 - dn_tmp_9); });double *dn_tmp_15=&((v_t)->f_frequencySmoothing);*dn_tmp_15=dn_tmp_16; }
{ size_t dn_tmp_18=((size_t)(0ULL));size_t dn_tmp_17=((size_t)(3ULL));for(size_t v_i=dn_tmp_18;v_i<dn_tmp_17;v_i++){
(void)(({ dn_r_Tube * dn_tmp_19=(&((*({ dn_t_arr3_r4_Tube volatile *dn_tmp_20=&((v_t)->f_stages);size_t dn_tmp_21=v_i;&dn_tmp_20->data[dn_index(dn_tmp_21,3)]; }))));
double dn_tmp_22=v_rate;
dn_f_tube_set_rate(dn_tmp_19,dn_tmp_22); }));
}
 }
{ size_t dn_tmp_24=((size_t)(0ULL));size_t dn_tmp_23=((size_t)(2ULL));for(size_t v_ch=dn_tmp_24;v_ch<dn_tmp_23;v_ch++){
{ int32_t dn_tmp_29=({ int32_t dn_tmp_27=((int32_t)(0ULL));
if((__int128)dn_tmp_27<(-((__int128)1<<31))||(__int128)dn_tmp_27>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_27; });int32_t *dn_tmp_28=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_25=&((v_t)->f_channels);size_t dn_tmp_26=v_ch;&dn_tmp_25->data[dn_index(dn_tmp_26,2)]; }))).f_coefficientCounter);*dn_tmp_28=dn_tmp_29; }
(void)(({ dn_r_ThreeBand * dn_tmp_30=v_t;
size_t dn_tmp_31=v_ch;
dn_f_band_update_coeff(dn_tmp_30,dn_tmp_31); }));
}
 }
}

return 0;
}
static void dn_cb_band_set_rate(dn_r_ThreeBand * v_t,double v_sr) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_band_set_rate(v_t,v_sr);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_band_frequency(dn_r_ThreeBand * v_t,double v_lowHz,double v_highHz) {
{
double v_ceiling=({ double dn_tmp_0=(v_t)->f_rate;
double dn_tmp_1=((double)(0.45));
(dn_tmp_0 * dn_tmp_1); });
if(({ double dn_tmp_2=v_ceiling;
double dn_tmp_3=((double)(12000.0));
(dn_tmp_2 > dn_tmp_3); })){
{ double dn_tmp_5=((double)(12000.0));double *dn_tmp_4=&(v_ceiling);*dn_tmp_4=dn_tmp_5; }
}
else{
}
{ double dn_tmp_15=({ double dn_tmp_6=v_lowHz;
double dn_tmp_7=((double)(40.0));
double dn_tmp_8=({ double dn_tmp_9=((double)(1400.0));
double dn_tmp_10=((double)(0.0));
double dn_tmp_11=({ double dn_tmp_12=v_ceiling;
double dn_tmp_13=((double)(0.5));
(dn_tmp_12 * dn_tmp_13); });
dn_f_min_f(dn_tmp_9,dn_tmp_10,dn_tmp_11); });
dn_f_min_f(dn_tmp_6,dn_tmp_7,dn_tmp_8); });double *dn_tmp_14=&((v_t)->f_targetLower);*dn_tmp_14=dn_tmp_15; }
{ double dn_tmp_22=({ double dn_tmp_16=v_highHz;
double dn_tmp_17=({ double dn_tmp_18=(v_t)->f_targetLower;
double dn_tmp_19=((double)(1.5));
(dn_tmp_18 * dn_tmp_19); });
double dn_tmp_20=v_ceiling;
dn_f_min_f(dn_tmp_16,dn_tmp_17,dn_tmp_20); });double *dn_tmp_21=&((v_t)->f_targetUpper);*dn_tmp_21=dn_tmp_22; }
}

return 0;
}
static void dn_cb_band_frequency(dn_r_ThreeBand * v_t,double v_lowHz,double v_highHz) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_band_frequency(v_t,v_lowHz,v_highHz);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_band_update_enabled(dn_r_ThreeBand * v_t) {
{
bool v_wasActive=(v_t)->f_anyActive;
{ bool dn_tmp_1=0;bool *dn_tmp_0=&((v_t)->f_anyActive);*dn_tmp_0=dn_tmp_1; }
{ size_t dn_tmp_3=((size_t)(0ULL));size_t dn_tmp_2=((size_t)(3ULL));for(size_t v_b=dn_tmp_3;v_b<dn_tmp_2;v_b++){
if((({ size_t dn_tmp_4=(*({ dn_t_arr3_USize *dn_tmp_5=&((v_t)->f_types);size_t dn_tmp_6=v_b;&dn_tmp_5->data[dn_index(dn_tmp_6,3)]; }));
size_t dn_tmp_7=((size_t)(0ULL));
(dn_tmp_4 > dn_tmp_7); }) && ({ double dn_tmp_8=(*({ dn_t_arr3_F64 *dn_tmp_9=&((v_t)->f_wets);size_t dn_tmp_10=v_b;&dn_tmp_9->data[dn_index(dn_tmp_10,3)]; }));
double dn_tmp_11=((double)(0.0));
(dn_tmp_8 > dn_tmp_11); }))){
{ bool dn_tmp_13=1;bool *dn_tmp_12=&((v_t)->f_anyActive);*dn_tmp_12=dn_tmp_13; }
}
else{
}
}
 }
if((v_wasActive && (!((v_t)->f_anyActive)))){
{ size_t dn_tmp_15=((size_t)(0ULL));size_t dn_tmp_14=((size_t)(2ULL));for(size_t v_ch=dn_tmp_15;v_ch<dn_tmp_14;v_ch++){
{ size_t dn_tmp_23=({ double dn_tmp_18=({ double dn_tmp_19=({ double dn_tmp_20=((double)(0.10));
double dn_tmp_21=(v_t)->f_rate;
(dn_tmp_20 * dn_tmp_21); });
trunc(dn_tmp_19); });
long double dn_n=(long double)dn_tmp_18;if(!isfinite(dn_tmp_18)||truncl(dn_n)!=dn_n||dn_n<0.0L||dn_n>=ldexpl(1.0L,64))dn_fail("numeric conversion out of range");(size_t)dn_tmp_18; });size_t *dn_tmp_22=&((*({ dn_t_arr2_USize *dn_tmp_16=&((v_t)->f_releaseFrames);size_t dn_tmp_17=v_ch;&dn_tmp_16->data[dn_index(dn_tmp_17,2)]; })));*dn_tmp_22=dn_tmp_23; }
}
 }
}
else{
}
}

return 0;
}
static void dn_cb_band_update_enabled(dn_r_ThreeBand * v_t) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_band_update_enabled(v_t);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_band_config(dn_r_ThreeBand * v_t,size_t v_band,size_t v_typ,size_t v_topology,double v_driveDb,double v_bias,double v_sag,double v_wet) {
{
(void)(({ dn_r_Tube * dn_tmp_0=(&((*({ dn_t_arr3_r4_Tube volatile *dn_tmp_1=&((v_t)->f_stages);size_t dn_tmp_2=v_band;&dn_tmp_1->data[dn_index(dn_tmp_2,3)]; }))));
size_t dn_tmp_3=v_typ;
size_t dn_tmp_4=v_topology;
double dn_tmp_5=v_driveDb;
double dn_tmp_6=v_bias;
double dn_tmp_7=v_sag;
double dn_tmp_8=v_wet;
dn_f_tube_config(dn_tmp_0,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8); }));
{ size_t dn_tmp_12=v_typ;size_t *dn_tmp_11=&((*({ dn_t_arr3_USize *dn_tmp_9=&((v_t)->f_types);size_t dn_tmp_10=v_band;&dn_tmp_9->data[dn_index(dn_tmp_10,3)]; })));*dn_tmp_11=dn_tmp_12; }
{ double dn_tmp_19=({ double dn_tmp_15=v_wet;
double dn_tmp_16=((double)(0.0));
double dn_tmp_17=((double)(1.0));
dn_f_min_f(dn_tmp_15,dn_tmp_16,dn_tmp_17); });double *dn_tmp_18=&((*({ dn_t_arr3_F64 *dn_tmp_13=&((v_t)->f_wets);size_t dn_tmp_14=v_band;&dn_tmp_13->data[dn_index(dn_tmp_14,3)]; })));*dn_tmp_18=dn_tmp_19; }
(void)(({ dn_r_ThreeBand * dn_tmp_20=v_t;
dn_f_band_update_enabled(dn_tmp_20); }));
}

return 0;
}
static void dn_cb_band_config(dn_r_ThreeBand * v_t,size_t v_band,size_t v_typ,size_t v_topology,double v_driveDb,double v_bias,double v_sag,double v_wet) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_band_config(v_t,v_band,v_typ,v_topology,v_driveDb,v_bias,v_sag,v_wet);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static double dn_f_band_process(dn_r_ThreeBand * v_t,double v_x,size_t v_channel) {
{
if((!(({ double dn_tmp_0=v_x;
isfinite(dn_tmp_0); })))){
{ double dn_tmp_1=((double)(0.0));return dn_tmp_1; }
}
else{
}
{ int32_t dn_tmp_10=({ int32_t dn_tmp_4=((*({ dn_t_arr2_r11_BandChannel *dn_tmp_5=&((v_t)->f_channels);size_t dn_tmp_6=v_channel;&dn_tmp_5->data[dn_index(dn_tmp_6,2)]; }))).f_coefficientCounter;
int32_t dn_tmp_7=({ int32_t dn_tmp_8=((int32_t)(1ULL));
if((__int128)dn_tmp_8<(-((__int128)1<<31))||(__int128)dn_tmp_8>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_8; });
dn_I32_sub(dn_tmp_4,dn_tmp_7); });int32_t *dn_tmp_9=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_2=&((v_t)->f_channels);size_t dn_tmp_3=v_channel;&dn_tmp_2->data[dn_index(dn_tmp_3,2)]; }))).f_coefficientCounter);*dn_tmp_9=dn_tmp_10; }
if(({ int32_t dn_tmp_11=((*({ dn_t_arr2_r11_BandChannel *dn_tmp_12=&((v_t)->f_channels);size_t dn_tmp_13=v_channel;&dn_tmp_12->data[dn_index(dn_tmp_13,2)]; }))).f_coefficientCounter;
int32_t dn_tmp_14=({ int32_t dn_tmp_15=((int32_t)(0ULL));
if((__int128)dn_tmp_15<(-((__int128)1<<31))||(__int128)dn_tmp_15>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_15; });
(dn_tmp_11 < dn_tmp_14); })){
{ int32_t dn_tmp_20=({ int32_t dn_tmp_18=((int32_t)(63ULL));
if((__int128)dn_tmp_18<(-((__int128)1<<31))||(__int128)dn_tmp_18>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_18; });int32_t *dn_tmp_19=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_16=&((v_t)->f_channels);size_t dn_tmp_17=v_channel;&dn_tmp_16->data[dn_index(dn_tmp_17,2)]; }))).f_coefficientCounter);*dn_tmp_19=dn_tmp_20; }
if((({ double dn_tmp_21=((*({ dn_t_arr2_r11_BandChannel *dn_tmp_22=&((v_t)->f_channels);size_t dn_tmp_23=v_channel;&dn_tmp_22->data[dn_index(dn_tmp_23,2)]; }))).f_currentLower;
double dn_tmp_24=(v_t)->f_targetLower;
(dn_tmp_21 != dn_tmp_24); }) || ({ double dn_tmp_25=((*({ dn_t_arr2_r11_BandChannel *dn_tmp_26=&((v_t)->f_channels);size_t dn_tmp_27=v_channel;&dn_tmp_26->data[dn_index(dn_tmp_27,2)]; }))).f_currentUpper;
double dn_tmp_28=(v_t)->f_targetUpper;
(dn_tmp_25 != dn_tmp_28); }))){
{ double dn_tmp_42=({ double dn_tmp_31=((*({ dn_t_arr2_r11_BandChannel *dn_tmp_32=&((v_t)->f_channels);size_t dn_tmp_33=v_channel;&dn_tmp_32->data[dn_index(dn_tmp_33,2)]; }))).f_currentLower;
double dn_tmp_34=({ double dn_tmp_35=(v_t)->f_frequencySmoothing;
double dn_tmp_36=({ double dn_tmp_37=(v_t)->f_targetLower;
double dn_tmp_38=((*({ dn_t_arr2_r11_BandChannel *dn_tmp_39=&((v_t)->f_channels);size_t dn_tmp_40=v_channel;&dn_tmp_39->data[dn_index(dn_tmp_40,2)]; }))).f_currentLower;
(dn_tmp_37 - dn_tmp_38); });
(dn_tmp_35 * dn_tmp_36); });
(dn_tmp_31 + dn_tmp_34); });double *dn_tmp_41=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_29=&((v_t)->f_channels);size_t dn_tmp_30=v_channel;&dn_tmp_29->data[dn_index(dn_tmp_30,2)]; }))).f_currentLower);*dn_tmp_41=dn_tmp_42; }
{ double dn_tmp_56=({ double dn_tmp_45=((*({ dn_t_arr2_r11_BandChannel *dn_tmp_46=&((v_t)->f_channels);size_t dn_tmp_47=v_channel;&dn_tmp_46->data[dn_index(dn_tmp_47,2)]; }))).f_currentUpper;
double dn_tmp_48=({ double dn_tmp_49=(v_t)->f_frequencySmoothing;
double dn_tmp_50=({ double dn_tmp_51=(v_t)->f_targetUpper;
double dn_tmp_52=((*({ dn_t_arr2_r11_BandChannel *dn_tmp_53=&((v_t)->f_channels);size_t dn_tmp_54=v_channel;&dn_tmp_53->data[dn_index(dn_tmp_54,2)]; }))).f_currentUpper;
(dn_tmp_51 - dn_tmp_52); });
(dn_tmp_49 * dn_tmp_50); });
(dn_tmp_45 + dn_tmp_48); });double *dn_tmp_55=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_43=&((v_t)->f_channels);size_t dn_tmp_44=v_channel;&dn_tmp_43->data[dn_index(dn_tmp_44,2)]; }))).f_currentUpper);*dn_tmp_55=dn_tmp_56; }
if(({ double dn_tmp_57=({ double dn_tmp_58=({ double dn_tmp_59=((*({ dn_t_arr2_r11_BandChannel *dn_tmp_60=&((v_t)->f_channels);size_t dn_tmp_61=v_channel;&dn_tmp_60->data[dn_index(dn_tmp_61,2)]; }))).f_currentLower;
double dn_tmp_62=(v_t)->f_targetLower;
(dn_tmp_59 - dn_tmp_62); });
fabs(dn_tmp_58); });
double dn_tmp_63=((double)(1.0e-7));
(dn_tmp_57 < dn_tmp_63); })){
{ double dn_tmp_67=(v_t)->f_targetLower;double *dn_tmp_66=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_64=&((v_t)->f_channels);size_t dn_tmp_65=v_channel;&dn_tmp_64->data[dn_index(dn_tmp_65,2)]; }))).f_currentLower);*dn_tmp_66=dn_tmp_67; }
}
else{
}
if(({ double dn_tmp_68=({ double dn_tmp_69=({ double dn_tmp_70=((*({ dn_t_arr2_r11_BandChannel *dn_tmp_71=&((v_t)->f_channels);size_t dn_tmp_72=v_channel;&dn_tmp_71->data[dn_index(dn_tmp_72,2)]; }))).f_currentUpper;
double dn_tmp_73=(v_t)->f_targetUpper;
(dn_tmp_70 - dn_tmp_73); });
fabs(dn_tmp_69); });
double dn_tmp_74=((double)(1.0e-7));
(dn_tmp_68 < dn_tmp_74); })){
{ double dn_tmp_78=(v_t)->f_targetUpper;double *dn_tmp_77=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_75=&((v_t)->f_channels);size_t dn_tmp_76=v_channel;&dn_tmp_75->data[dn_index(dn_tmp_76,2)]; }))).f_currentUpper);*dn_tmp_77=dn_tmp_78; }
}
else{
}
(void)(({ dn_r_ThreeBand * dn_tmp_79=v_t;
size_t dn_tmp_80=v_channel;
dn_f_band_update_coeff(dn_tmp_79,dn_tmp_80); }));
}
else{
}
}
else{
}
double v_low=v_x;
double v_rest=v_x;
{ size_t dn_tmp_82=((size_t)(0ULL));size_t dn_tmp_81=((size_t)(2ULL));for(size_t v_i=dn_tmp_82;v_i<dn_tmp_81;v_i++){
{ double dn_tmp_90=({ dn_r_Biquad * dn_tmp_83=(&((*({ dn_t_arr8_r6_Biquad *dn_tmp_84=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_86=&((v_t)->f_channels);size_t dn_tmp_87=v_channel;&dn_tmp_86->data[dn_index(dn_tmp_87,2)]; }))).f_filters);size_t dn_tmp_85=v_i;&dn_tmp_84->data[dn_index(dn_tmp_85,8)]; }))));
double dn_tmp_88=v_low;
dn_f_biquad_process(dn_tmp_83,dn_tmp_88); });double *dn_tmp_89=&(v_low);*dn_tmp_89=dn_tmp_90; }
}
 }
{ size_t dn_tmp_92=((size_t)(2ULL));size_t dn_tmp_91=((size_t)(4ULL));for(size_t v_i=dn_tmp_92;v_i<dn_tmp_91;v_i++){
{ double dn_tmp_100=({ dn_r_Biquad * dn_tmp_93=(&((*({ dn_t_arr8_r6_Biquad *dn_tmp_94=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_96=&((v_t)->f_channels);size_t dn_tmp_97=v_channel;&dn_tmp_96->data[dn_index(dn_tmp_97,2)]; }))).f_filters);size_t dn_tmp_95=v_i;&dn_tmp_94->data[dn_index(dn_tmp_95,8)]; }))));
double dn_tmp_98=v_rest;
dn_f_biquad_process(dn_tmp_93,dn_tmp_98); });double *dn_tmp_99=&(v_rest);*dn_tmp_99=dn_tmp_100; }
}
 }
double v_mid=v_rest;
double v_high=v_rest;
{ size_t dn_tmp_102=((size_t)(4ULL));size_t dn_tmp_101=((size_t)(6ULL));for(size_t v_i=dn_tmp_102;v_i<dn_tmp_101;v_i++){
{ double dn_tmp_110=({ dn_r_Biquad * dn_tmp_103=(&((*({ dn_t_arr8_r6_Biquad *dn_tmp_104=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_106=&((v_t)->f_channels);size_t dn_tmp_107=v_channel;&dn_tmp_106->data[dn_index(dn_tmp_107,2)]; }))).f_filters);size_t dn_tmp_105=v_i;&dn_tmp_104->data[dn_index(dn_tmp_105,8)]; }))));
double dn_tmp_108=v_mid;
dn_f_biquad_process(dn_tmp_103,dn_tmp_108); });double *dn_tmp_109=&(v_mid);*dn_tmp_109=dn_tmp_110; }
}
 }
{ size_t dn_tmp_112=((size_t)(6ULL));size_t dn_tmp_111=((size_t)(8ULL));for(size_t v_i=dn_tmp_112;v_i<dn_tmp_111;v_i++){
{ double dn_tmp_120=({ dn_r_Biquad * dn_tmp_113=(&((*({ dn_t_arr8_r6_Biquad *dn_tmp_114=&(((*({ dn_t_arr2_r11_BandChannel *dn_tmp_116=&((v_t)->f_channels);size_t dn_tmp_117=v_channel;&dn_tmp_116->data[dn_index(dn_tmp_117,2)]; }))).f_filters);size_t dn_tmp_115=v_i;&dn_tmp_114->data[dn_index(dn_tmp_115,8)]; }))));
double dn_tmp_118=v_high;
dn_f_biquad_process(dn_tmp_113,dn_tmp_118); });double *dn_tmp_119=&(v_high);*dn_tmp_119=dn_tmp_120; }
}
 }
if(((!((v_t)->f_anyActive)) && ({ size_t dn_tmp_121=(*({ dn_t_arr2_USize *dn_tmp_122=&((v_t)->f_releaseFrames);size_t dn_tmp_123=v_channel;&dn_tmp_122->data[dn_index(dn_tmp_123,2)]; }));
size_t dn_tmp_124=((size_t)(0ULL));
(dn_tmp_121 == dn_tmp_124); }))){
{ double dn_tmp_125=v_x;return dn_tmp_125; }
}
else{
}
if((!((v_t)->f_anyActive))){
{ size_t dn_tmp_133=({ size_t dn_tmp_128=(*({ dn_t_arr2_USize *dn_tmp_129=&((v_t)->f_releaseFrames);size_t dn_tmp_130=v_channel;&dn_tmp_129->data[dn_index(dn_tmp_130,2)]; }));
size_t dn_tmp_131=((size_t)(1ULL));
dn_USize_sub(dn_tmp_128,dn_tmp_131); });size_t *dn_tmp_132=&((*({ dn_t_arr2_USize *dn_tmp_126=&((v_t)->f_releaseFrames);size_t dn_tmp_127=v_channel;&dn_tmp_126->data[dn_index(dn_tmp_127,2)]; })));*dn_tmp_132=dn_tmp_133; }
}
else{
}
double v_lowWet=({ dn_r_Tube * dn_tmp_134=(&((*({ dn_t_arr3_r4_Tube volatile *dn_tmp_135=&((v_t)->f_stages);size_t dn_tmp_136=((size_t)(0ULL));&dn_tmp_135->data[dn_index(dn_tmp_136,3)]; }))));
double dn_tmp_137=v_low;
size_t dn_tmp_138=v_channel;
dn_f_tube_process(dn_tmp_134,dn_tmp_137,dn_tmp_138); });
double v_midWet=({ dn_r_Tube * dn_tmp_139=(&((*({ dn_t_arr3_r4_Tube volatile *dn_tmp_140=&((v_t)->f_stages);size_t dn_tmp_141=((size_t)(1ULL));&dn_tmp_140->data[dn_index(dn_tmp_141,3)]; }))));
double dn_tmp_142=v_mid;
size_t dn_tmp_143=v_channel;
dn_f_tube_process(dn_tmp_139,dn_tmp_142,dn_tmp_143); });
double v_highWet=({ dn_r_Tube * dn_tmp_144=(&((*({ dn_t_arr3_r4_Tube volatile *dn_tmp_145=&((v_t)->f_stages);size_t dn_tmp_146=((size_t)(2ULL));&dn_tmp_145->data[dn_index(dn_tmp_146,3)]; }))));
double dn_tmp_147=v_high;
size_t dn_tmp_148=v_channel;
dn_f_tube_process(dn_tmp_144,dn_tmp_147,dn_tmp_148); });
double v_y=({ double dn_tmp_149=({ double dn_tmp_150=({ double dn_tmp_151=v_x;
double dn_tmp_152=({ double dn_tmp_153=v_lowWet;
double dn_tmp_154=v_low;
(dn_tmp_153 - dn_tmp_154); });
(dn_tmp_151 + dn_tmp_152); });
double dn_tmp_155=({ double dn_tmp_156=v_midWet;
double dn_tmp_157=v_mid;
(dn_tmp_156 - dn_tmp_157); });
(dn_tmp_150 + dn_tmp_155); });
double dn_tmp_158=({ double dn_tmp_159=v_highWet;
double dn_tmp_160=v_high;
(dn_tmp_159 - dn_tmp_160); });
(dn_tmp_149 + dn_tmp_158); });
if((!(({ double dn_tmp_161=v_y;
isfinite(dn_tmp_161); })))){
{ double dn_tmp_162=((double)(0.0));return dn_tmp_162; }
}
else{
}
{ double dn_tmp_166=({ double dn_tmp_163=v_y;
double dn_tmp_164=(-(((double)(32.0))));
double dn_tmp_165=((double)(32.0));
dn_f_min_f(dn_tmp_163,dn_tmp_164,dn_tmp_165); });return dn_tmp_166; }
}

dn_fail("missing native return");
}
static double dn_cb_band_process(dn_r_ThreeBand * v_t,double v_x,size_t v_channel) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_band_process(v_t,v_x,v_channel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_apply_tube(dn_r_Engine * v_e,double v_signal,size_t v_channel) {
{
double v_weight=(*({ dn_t_arr2_F64 *dn_tmp_0=&((v_e)->f_tubeRouteBlend);size_t dn_tmp_1=v_channel;&dn_tmp_0->data[dn_index(dn_tmp_1,2)]; }));
double v_target=({ size_t dn_tmp_2=(v_e)->f_tubeRouting;
(double)dn_tmp_2; });
if(({ double dn_tmp_3=v_weight;
double dn_tmp_4=v_target;
(dn_tmp_3 < dn_tmp_4); })){
{ double dn_tmp_13=({ double dn_tmp_7=({ double dn_tmp_8=v_weight;
double dn_tmp_9=(v_e)->f_tubeRouteStep;
(dn_tmp_8 + dn_tmp_9); });
double dn_tmp_10=((double)(0.0));
double dn_tmp_11=v_target;
dn_f_min_f(dn_tmp_7,dn_tmp_10,dn_tmp_11); });double *dn_tmp_12=&((*({ dn_t_arr2_F64 *dn_tmp_5=&((v_e)->f_tubeRouteBlend);size_t dn_tmp_6=v_channel;&dn_tmp_5->data[dn_index(dn_tmp_6,2)]; })));*dn_tmp_12=dn_tmp_13; }
}
else{
if(({ double dn_tmp_14=v_weight;
double dn_tmp_15=v_target;
(dn_tmp_14 > dn_tmp_15); })){
{ double dn_tmp_24=({ double dn_tmp_18=({ double dn_tmp_19=v_weight;
double dn_tmp_20=(v_e)->f_tubeRouteStep;
(dn_tmp_19 - dn_tmp_20); });
double dn_tmp_21=v_target;
double dn_tmp_22=((double)(1.0));
dn_f_min_f(dn_tmp_18,dn_tmp_21,dn_tmp_22); });double *dn_tmp_23=&((*({ dn_t_arr2_F64 *dn_tmp_16=&((v_e)->f_tubeRouteBlend);size_t dn_tmp_17=v_channel;&dn_tmp_16->data[dn_index(dn_tmp_17,2)]; })));*dn_tmp_23=dn_tmp_24; }
}
else{
}
}
if(({ double dn_tmp_25=(*({ dn_t_arr2_F64 *dn_tmp_26=&((v_e)->f_tubeRouteBlend);size_t dn_tmp_27=v_channel;&dn_tmp_26->data[dn_index(dn_tmp_27,2)]; }));
double dn_tmp_28=((double)(0.0));
(dn_tmp_25 <= dn_tmp_28); })){
{ double dn_tmp_32=({ dn_r_Tube * dn_tmp_29=(&((v_e)->f_tube));
double dn_tmp_30=v_signal;
size_t dn_tmp_31=v_channel;
dn_f_tube_process(dn_tmp_29,dn_tmp_30,dn_tmp_31); });return dn_tmp_32; }
}
else{
}
if(({ double dn_tmp_33=(*({ dn_t_arr2_F64 *dn_tmp_34=&((v_e)->f_tubeRouteBlend);size_t dn_tmp_35=v_channel;&dn_tmp_34->data[dn_index(dn_tmp_35,2)]; }));
double dn_tmp_36=((double)(1.0));
(dn_tmp_33 >= dn_tmp_36); })){
{ double dn_tmp_40=({ dn_r_ThreeBand * dn_tmp_37=(&((v_e)->f_threeBand));
double dn_tmp_38=v_signal;
size_t dn_tmp_39=v_channel;
dn_f_band_process(dn_tmp_37,dn_tmp_38,dn_tmp_39); });return dn_tmp_40; }
}
else{
}
double v_one=({ dn_r_Tube * dn_tmp_41=(&((v_e)->f_tube));
double dn_tmp_42=v_signal;
size_t dn_tmp_43=v_channel;
dn_f_tube_process(dn_tmp_41,dn_tmp_42,dn_tmp_43); });
double v_three=({ dn_r_ThreeBand * dn_tmp_44=(&((v_e)->f_threeBand));
double dn_tmp_45=v_signal;
size_t dn_tmp_46=v_channel;
dn_f_band_process(dn_tmp_44,dn_tmp_45,dn_tmp_46); });
{ double dn_tmp_55=({ double dn_tmp_47=v_one;
double dn_tmp_48=({ double dn_tmp_49=(*({ dn_t_arr2_F64 *dn_tmp_50=&((v_e)->f_tubeRouteBlend);size_t dn_tmp_51=v_channel;&dn_tmp_50->data[dn_index(dn_tmp_51,2)]; }));
double dn_tmp_52=({ double dn_tmp_53=v_three;
double dn_tmp_54=v_one;
(dn_tmp_53 - dn_tmp_54); });
(dn_tmp_49 * dn_tmp_52); });
(dn_tmp_47 + dn_tmp_48); });return dn_tmp_55; }
}

dn_fail("missing native return");
}
static double dn_cb_apply_tube(dn_r_Engine * v_e,double v_signal,size_t v_channel) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_apply_tube(v_e,v_signal,v_channel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_OutputStage dn_f_output_prepare(double v_rate) {
{
double v_sr=({ double dn_tmp_0=v_rate;
double dn_tmp_1=((double)(8000.0));
double dn_tmp_2=((double)(384000.0));
dn_f_min_f(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
dn_r_OutputStage v_o=({ dn_t_arr256_F64 dn_tmp_3=({ ((dn_t_arr256_F64){0}); });
dn_t_arr128_F64 dn_tmp_4=({ ((dn_t_arr128_F64){0}); });
dn_t_arr68_F64 dn_tmp_5=({ ((dn_t_arr68_F64){0}); });
size_t dn_tmp_6=((size_t)(0ULL));
bool dn_tmp_7=0;
double dn_tmp_8=((double)(0.0));
double dn_tmp_9=((double)(0.0));
double dn_tmp_10=((double)(1.0));
double dn_tmp_11=((double)(1.0));
double dn_tmp_12=({ double dn_tmp_13=((double)(1.0));
double dn_tmp_14=({ double dn_tmp_15=({ double dn_tmp_16=(-(((double)(1.0))));
double dn_tmp_17=({ double dn_tmp_18=((double)(0.50));
double dn_tmp_19=v_sr;
(dn_tmp_18 * dn_tmp_19); });
(dn_tmp_16 / dn_tmp_17); });
exp(dn_tmp_15); });
(dn_tmp_13 - dn_tmp_14); });
double dn_tmp_20=({ double dn_tmp_21=((double)(1.0));
double dn_tmp_22=({ double dn_tmp_23=({ double dn_tmp_24=(-(((double)(1.0))));
double dn_tmp_25=({ double dn_tmp_26=((double)(0.25));
double dn_tmp_27=v_sr;
(dn_tmp_26 * dn_tmp_27); });
(dn_tmp_24 / dn_tmp_25); });
exp(dn_tmp_23); });
(dn_tmp_21 - dn_tmp_22); });
double dn_tmp_28=({ double dn_tmp_29=((double)(1.0));
double dn_tmp_30=({ double dn_tmp_31=({ double dn_tmp_32=(-(((double)(1.0))));
double dn_tmp_33=({ double dn_tmp_34=((double)(0.12));
double dn_tmp_35=v_sr;
(dn_tmp_34 * dn_tmp_35); });
(dn_tmp_32 / dn_tmp_33); });
exp(dn_tmp_31); });
(dn_tmp_29 - dn_tmp_30); });
double dn_tmp_36=((double)(0.0));
double dn_tmp_37=((double)(0.0));
double dn_tmp_38=((double)(0.0));
((dn_r_OutputStage){dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_11,dn_tmp_12,dn_tmp_20,dn_tmp_28,dn_tmp_36,dn_tmp_37,dn_tmp_38}); });
{ size_t dn_tmp_40=((size_t)(0ULL));size_t dn_tmp_39=((size_t)(4ULL));for(size_t v_phase=dn_tmp_40;v_phase<dn_tmp_39;v_phase++){
double v_sum=((double)(0.0));
{ size_t dn_tmp_42=((size_t)(0ULL));size_t dn_tmp_41=((size_t)(17ULL));for(size_t v_tap=dn_tmp_42;v_tap<dn_tmp_41;v_tap++){
double v_j=({ double dn_tmp_43=({ size_t dn_tmp_44=v_tap;
(double)dn_tmp_44; });
double dn_tmp_45=((double)(8.0));
(dn_tmp_43 - dn_tmp_45); });
double v_x=({ double dn_tmp_46=v_j;
double dn_tmp_47=({ double dn_tmp_48=({ size_t dn_tmp_49=v_phase;
(double)dn_tmp_49; });
double dn_tmp_50=((double)(0.25));
(dn_tmp_48 * dn_tmp_50); });
(dn_tmp_46 - dn_tmp_47); });
double v_w=({ double dn_tmp_51=({ double dn_tmp_52=((double)(0.42));
double dn_tmp_53=({ double dn_tmp_54=((double)(0.5));
double dn_tmp_55=({ double dn_tmp_56=({ double dn_tmp_57=({ double dn_tmp_58=({ dn_f_pi(); });
double dn_tmp_59=v_j;
(dn_tmp_58 * dn_tmp_59); });
double dn_tmp_60=((double)(8.0));
(dn_tmp_57 / dn_tmp_60); });
cos(dn_tmp_56); });
(dn_tmp_54 * dn_tmp_55); });
(dn_tmp_52 + dn_tmp_53); });
double dn_tmp_61=({ double dn_tmp_62=((double)(0.08));
double dn_tmp_63=({ double dn_tmp_64=({ double dn_tmp_65=({ double dn_tmp_66=({ double dn_tmp_67=((double)(2.0));
double dn_tmp_68=({ dn_f_pi(); });
(dn_tmp_67 * dn_tmp_68); });
double dn_tmp_69=v_j;
(dn_tmp_66 * dn_tmp_69); });
double dn_tmp_70=((double)(8.0));
(dn_tmp_65 / dn_tmp_70); });
cos(dn_tmp_64); });
(dn_tmp_62 * dn_tmp_63); });
(dn_tmp_51 + dn_tmp_61); });
double v_sinc=((double)(1.0));
if(({ double dn_tmp_71=({ double dn_tmp_72=v_x;
fabs(dn_tmp_72); });
double dn_tmp_73=((double)(1.0e-12));
(dn_tmp_71 >= dn_tmp_73); })){
{ double dn_tmp_82=({ double dn_tmp_74=({ double dn_tmp_75=({ double dn_tmp_76=({ dn_f_pi(); });
double dn_tmp_77=v_x;
(dn_tmp_76 * dn_tmp_77); });
sin(dn_tmp_75); });
double dn_tmp_78=({ double dn_tmp_79=({ dn_f_pi(); });
double dn_tmp_80=v_x;
(dn_tmp_79 * dn_tmp_80); });
(dn_tmp_74 / dn_tmp_78); });double *dn_tmp_81=&(v_sinc);*dn_tmp_81=dn_tmp_82; }
}
else{
}
double v_c=({ double dn_tmp_83=v_sinc;
double dn_tmp_84=v_w;
(dn_tmp_83 * dn_tmp_84); });
{ double dn_tmp_92=v_c;double *dn_tmp_91=&((*({ dn_t_arr68_F64 *dn_tmp_85=&((v_o).f_kernels);size_t dn_tmp_86=({ size_t dn_tmp_87=({ size_t dn_tmp_88=v_phase;
size_t dn_tmp_89=((size_t)(17ULL));
dn_USize_mul(dn_tmp_88,dn_tmp_89); });
size_t dn_tmp_90=v_tap;
dn_USize_add(dn_tmp_87,dn_tmp_90); });&dn_tmp_85->data[dn_index(dn_tmp_86,68)]; })));*dn_tmp_91=dn_tmp_92; }
{ double dn_tmp_96=({ double dn_tmp_93=v_sum;
double dn_tmp_94=v_c;
(dn_tmp_93 + dn_tmp_94); });double *dn_tmp_95=&(v_sum);*dn_tmp_95=dn_tmp_96; }
}
 }
{ size_t dn_tmp_98=((size_t)(0ULL));size_t dn_tmp_97=((size_t)(17ULL));for(size_t v_tap=dn_tmp_98;v_tap<dn_tmp_97;v_tap++){
{ double dn_tmp_114=({ double dn_tmp_105=(*({ dn_t_arr68_F64 *dn_tmp_106=&((v_o).f_kernels);size_t dn_tmp_107=({ size_t dn_tmp_108=({ size_t dn_tmp_109=v_phase;
size_t dn_tmp_110=((size_t)(17ULL));
dn_USize_mul(dn_tmp_109,dn_tmp_110); });
size_t dn_tmp_111=v_tap;
dn_USize_add(dn_tmp_108,dn_tmp_111); });&dn_tmp_106->data[dn_index(dn_tmp_107,68)]; }));
double dn_tmp_112=v_sum;
(dn_tmp_105 / dn_tmp_112); });double *dn_tmp_113=&((*({ dn_t_arr68_F64 *dn_tmp_99=&((v_o).f_kernels);size_t dn_tmp_100=({ size_t dn_tmp_101=({ size_t dn_tmp_102=v_phase;
size_t dn_tmp_103=((size_t)(17ULL));
dn_USize_mul(dn_tmp_102,dn_tmp_103); });
size_t dn_tmp_104=v_tap;
dn_USize_add(dn_tmp_101,dn_tmp_104); });&dn_tmp_99->data[dn_index(dn_tmp_100,68)]; })));*dn_tmp_113=dn_tmp_114; }
}
 }
}
 }
{ dn_r_OutputStage dn_tmp_115=v_o;return dn_tmp_115; }
}

dn_fail("missing native return");
}
static dn_r_OutputStage dn_cb_output_prepare(double v_rate) {
 if(!dn_enter()){return (dn_r_OutputStage){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_OutputStage){0};}
 dn_r_OutputStage dn_result=dn_f_output_prepare(v_rate);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_output_reset(dn_r_OutputStage * v_o) {
{
{ size_t dn_tmp_1=((size_t)(0ULL));size_t dn_tmp_0=((size_t)(256ULL));for(size_t v_i=dn_tmp_1;v_i<dn_tmp_0;v_i++){
{ double dn_tmp_5=((double)(0.0));double *dn_tmp_4=&((*({ dn_t_arr256_F64 *dn_tmp_2=&((v_o)->f_signal);size_t dn_tmp_3=v_i;&dn_tmp_2->data[dn_index(dn_tmp_3,256)]; })));*dn_tmp_4=dn_tmp_5; }
}
 }
{ size_t dn_tmp_7=((size_t)(0ULL));size_t dn_tmp_6=((size_t)(128ULL));for(size_t v_i=dn_tmp_7;v_i<dn_tmp_6;v_i++){
{ double dn_tmp_11=((double)(0.0));double *dn_tmp_10=&((*({ dn_t_arr128_F64 *dn_tmp_8=&((v_o)->f_peakHistory);size_t dn_tmp_9=v_i;&dn_tmp_8->data[dn_index(dn_tmp_9,128)]; })));*dn_tmp_10=dn_tmp_11; }
}
 }
{ size_t dn_tmp_13=((size_t)(0ULL));size_t *dn_tmp_12=&((v_o)->f_index);*dn_tmp_12=dn_tmp_13; }
{ bool dn_tmp_15=0;bool *dn_tmp_14=&((v_o)->f_limiterPreviouslyEnabled);*dn_tmp_14=dn_tmp_15; }
{ double dn_tmp_17=((double)(0.0));double *dn_tmp_16=&((v_o)->f_inputPower);*dn_tmp_16=dn_tmp_17; }
{ double dn_tmp_19=((double)(0.0));double *dn_tmp_18=&((v_o)->f_processedPower);*dn_tmp_18=dn_tmp_19; }
{ double dn_tmp_21=((double)(1.0));double *dn_tmp_20=&((v_o)->f_autoGain);*dn_tmp_20=dn_tmp_21; }
{ double dn_tmp_23=((double)(1.0));double *dn_tmp_22=&((v_o)->f_limiterGain);*dn_tmp_22=dn_tmp_23; }
{ double dn_tmp_25=((double)(0.0));double *dn_tmp_24=&((v_o)->f_estimatedPeak);*dn_tmp_24=dn_tmp_25; }
{ double dn_tmp_27=((double)(0.0));double *dn_tmp_26=&((v_o)->f_reductionDb);*dn_tmp_26=dn_tmp_27; }
{ double dn_tmp_29=((double)(0.0));double *dn_tmp_28=&((v_o)->f_autoGainDb);*dn_tmp_28=dn_tmp_29; }
}

return 0;
}
static void dn_cb_output_reset(dn_r_OutputStage * v_o) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_output_reset(v_o);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static double dn_f_output_estimate(dn_r_OutputStage const * v_o,size_t v_center) {
{
double v_peak=((double)(0.0));
{ size_t dn_tmp_1=((size_t)(0ULL));size_t dn_tmp_0=((size_t)(4ULL));for(size_t v_phase=dn_tmp_1;v_phase<dn_tmp_0;v_phase++){
{ size_t dn_tmp_3=((size_t)(0ULL));size_t dn_tmp_2=((size_t)(2ULL));for(size_t v_channel=dn_tmp_3;v_channel<dn_tmp_2;v_channel++){
double v_sample=((double)(0.0));
{ size_t dn_tmp_5=((size_t)(0ULL));size_t dn_tmp_4=((size_t)(17ULL));for(size_t v_tap=dn_tmp_5;v_tap<dn_tmp_4;v_tap++){
size_t v_position=({ size_t dn_tmp_6=({ size_t dn_tmp_7=({ size_t dn_tmp_8=v_center;
size_t dn_tmp_9=v_tap;
dn_USize_add(dn_tmp_8,dn_tmp_9); });
size_t dn_tmp_10=((size_t)(120ULL));
dn_USize_add(dn_tmp_7,dn_tmp_10); });
size_t dn_tmp_11=((size_t)(128ULL));
dn_USize_mod(dn_tmp_6,dn_tmp_11); });
{ double dn_tmp_29=({ double dn_tmp_12=v_sample;
double dn_tmp_13=({ double dn_tmp_14=(*({ dn_t_arr256_F64 *dn_tmp_15=&((v_o)->f_signal);size_t dn_tmp_16=({ size_t dn_tmp_17=({ size_t dn_tmp_18=v_channel;
size_t dn_tmp_19=((size_t)(128ULL));
dn_USize_mul(dn_tmp_18,dn_tmp_19); });
size_t dn_tmp_20=v_position;
dn_USize_add(dn_tmp_17,dn_tmp_20); });&dn_tmp_15->data[dn_index(dn_tmp_16,256)]; }));
double dn_tmp_21=(*({ dn_t_arr68_F64 *dn_tmp_22=&((v_o)->f_kernels);size_t dn_tmp_23=({ size_t dn_tmp_24=({ size_t dn_tmp_25=v_phase;
size_t dn_tmp_26=((size_t)(17ULL));
dn_USize_mul(dn_tmp_25,dn_tmp_26); });
size_t dn_tmp_27=v_tap;
dn_USize_add(dn_tmp_24,dn_tmp_27); });&dn_tmp_22->data[dn_index(dn_tmp_23,68)]; }));
(dn_tmp_14 * dn_tmp_21); });
(dn_tmp_12 + dn_tmp_13); });double *dn_tmp_28=&(v_sample);*dn_tmp_28=dn_tmp_29; }
}
 }
if(({ double dn_tmp_30=({ double dn_tmp_31=v_sample;
fabs(dn_tmp_31); });
double dn_tmp_32=v_peak;
(dn_tmp_30 > dn_tmp_32); })){
{ double dn_tmp_35=({ double dn_tmp_33=v_sample;
fabs(dn_tmp_33); });double *dn_tmp_34=&(v_peak);*dn_tmp_34=dn_tmp_35; }
}
else{
}
}
 }
}
 }
{ double dn_tmp_36=v_peak;return dn_tmp_36; }
}

dn_fail("missing native return");
}
static double dn_cb_output_estimate(dn_r_OutputStage const * v_o,size_t v_center) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_output_estimate(v_o,v_center);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_output_process(dn_r_OutputStage * v_o,float * v_left,float * v_right,size_t v_frames,double v_inputMeanSquare,bool v_autoMatch,bool v_limiter,double v_ceilingDb) {
{
if(({ size_t dn_tmp_0=v_frames;
size_t dn_tmp_1=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); })){
{ uint8_t dn_tmp_2=0;return dn_tmp_2; }
}
else{
}
dn_t_foreign_F32 v_lb=({ float * dn_tmp_3=v_left;
size_t dn_tmp_4=v_frames;
({ if(dn_tmp_4&&!dn_tmp_3)dn_fail("null foreign buffer");dn_size(dn_tmp_4,sizeof(*dn_tmp_3));(dn_t_foreign_F32){dn_tmp_3,dn_tmp_4}; }); });
dn_t_foreign_F32 v_rb=({ float * dn_tmp_5=v_right;
size_t dn_tmp_6=v_frames;
({ if(dn_tmp_6&&!dn_tmp_5)dn_fail("null foreign buffer");dn_size(dn_tmp_6,sizeof(*dn_tmp_5));(dn_t_foreign_F32){dn_tmp_5,dn_tmp_6}; }); });
double v_power=({ double dn_tmp_7=v_inputMeanSquare;
double dn_tmp_8=((double)(0.0));
double dn_tmp_9=((double)(1024.0));
dn_f_min_f(dn_tmp_7,dn_tmp_8,dn_tmp_9); });
double v_ceiling=({ double dn_tmp_10=({ double dn_tmp_11=({ double dn_tmp_12=({ double dn_tmp_13=((double)(10.0));
log(dn_tmp_13); });
double dn_tmp_14=({ double dn_tmp_15=v_ceilingDb;
double dn_tmp_16=(-(((double)(12.0))));
double dn_tmp_17=(-(((double)(0.1))));
dn_f_min_f(dn_tmp_15,dn_tmp_16,dn_tmp_17); });
(dn_tmp_12 * dn_tmp_14); });
double dn_tmp_18=((double)(20.0));
(dn_tmp_11 / dn_tmp_18); });
exp(dn_tmp_10); });
bool v_wasLimiter=(v_o)->f_limiterPreviouslyEnabled;
{ bool dn_tmp_20=v_limiter;bool *dn_tmp_19=&((v_o)->f_limiterPreviouslyEnabled);*dn_tmp_19=dn_tmp_20; }
double v_blockPeak=((double)(0.0));
double v_blockReduction=((double)(0.0));
{ size_t dn_tmp_22=((size_t)(0ULL));size_t dn_tmp_21=v_frames;for(size_t v_i=dn_tmp_22;v_i<dn_tmp_21;v_i++){
double v_l=({ float dn_tmp_23=(*({ dn_t_foreign_F32 *dn_tmp_24=&(v_lb);size_t dn_tmp_25=v_i;&dn_tmp_24->data[dn_index(dn_tmp_25,dn_tmp_24->len)]; }));
(double)dn_tmp_23; });
double v_r=({ float dn_tmp_26=(*({ dn_t_foreign_F32 *dn_tmp_27=&(v_rb);size_t dn_tmp_28=v_i;&dn_tmp_27->data[dn_index(dn_tmp_28,dn_tmp_27->len)]; }));
(double)dn_tmp_26; });
if((!(({ double dn_tmp_29=v_l;
isfinite(dn_tmp_29); })))){
{ double dn_tmp_31=((double)(0.0));double *dn_tmp_30=&(v_l);*dn_tmp_30=dn_tmp_31; }
}
else{
}
if((!(({ double dn_tmp_32=v_r;
isfinite(dn_tmp_32); })))){
{ double dn_tmp_34=((double)(0.0));double *dn_tmp_33=&(v_r);*dn_tmp_33=dn_tmp_34; }
}
else{
}
{ double dn_tmp_42=({ double dn_tmp_35=(v_o)->f_inputPower;
double dn_tmp_36=({ double dn_tmp_37=(v_o)->f_powerAlpha;
double dn_tmp_38=({ double dn_tmp_39=v_power;
double dn_tmp_40=(v_o)->f_inputPower;
(dn_tmp_39 - dn_tmp_40); });
(dn_tmp_37 * dn_tmp_38); });
(dn_tmp_35 + dn_tmp_36); });double *dn_tmp_41=&((v_o)->f_inputPower);*dn_tmp_41=dn_tmp_42; }
{ double dn_tmp_58=({ double dn_tmp_43=(v_o)->f_processedPower;
double dn_tmp_44=({ double dn_tmp_45=(v_o)->f_powerAlpha;
double dn_tmp_46=({ double dn_tmp_47=({ double dn_tmp_48=((double)(0.5));
double dn_tmp_49=({ double dn_tmp_50=({ double dn_tmp_51=v_l;
double dn_tmp_52=v_l;
(dn_tmp_51 * dn_tmp_52); });
double dn_tmp_53=({ double dn_tmp_54=v_r;
double dn_tmp_55=v_r;
(dn_tmp_54 * dn_tmp_55); });
(dn_tmp_50 + dn_tmp_53); });
(dn_tmp_48 * dn_tmp_49); });
double dn_tmp_56=(v_o)->f_processedPower;
(dn_tmp_47 - dn_tmp_56); });
(dn_tmp_45 * dn_tmp_46); });
(dn_tmp_43 + dn_tmp_44); });double *dn_tmp_57=&((v_o)->f_processedPower);*dn_tmp_57=dn_tmp_58; }
double v_target=((double)(1.0));
if(((v_autoMatch && ({ double dn_tmp_59=(v_o)->f_inputPower;
double dn_tmp_60=((double)(1.0e-8));
(dn_tmp_59 > dn_tmp_60); })) && ({ double dn_tmp_61=(v_o)->f_processedPower;
double dn_tmp_62=((double)(1.0e-8));
(dn_tmp_61 > dn_tmp_62); }))){
{ double dn_tmp_70=({ double dn_tmp_63=({ double dn_tmp_64=({ double dn_tmp_65=(v_o)->f_inputPower;
double dn_tmp_66=(v_o)->f_processedPower;
(dn_tmp_65 / dn_tmp_66); });
sqrt(dn_tmp_64); });
double dn_tmp_67=((double)(0.125));
double dn_tmp_68=((double)(2.0));
dn_f_min_f(dn_tmp_63,dn_tmp_67,dn_tmp_68); });double *dn_tmp_69=&(v_target);*dn_tmp_69=dn_tmp_70; }
}
else{
}
{ double dn_tmp_78=({ double dn_tmp_71=(v_o)->f_autoGain;
double dn_tmp_72=({ double dn_tmp_73=(v_o)->f_matchAlpha;
double dn_tmp_74=({ double dn_tmp_75=v_target;
double dn_tmp_76=(v_o)->f_autoGain;
(dn_tmp_75 - dn_tmp_76); });
(dn_tmp_73 * dn_tmp_74); });
(dn_tmp_71 + dn_tmp_72); });double *dn_tmp_77=&((v_o)->f_autoGain);*dn_tmp_77=dn_tmp_78; }
{ double dn_tmp_89=({ double dn_tmp_79=({ double dn_tmp_80=((double)(20.0));
double dn_tmp_81=({ double dn_tmp_82=({ double dn_tmp_83=(v_o)->f_autoGain;
double dn_tmp_84=((double)(1.0e-8));
double dn_tmp_85=((double)(1.0e12));
dn_f_min_f(dn_tmp_83,dn_tmp_84,dn_tmp_85); });
log(dn_tmp_82); });
(dn_tmp_80 * dn_tmp_81); });
double dn_tmp_86=({ double dn_tmp_87=((double)(10.0));
log(dn_tmp_87); });
(dn_tmp_79 / dn_tmp_86); });double *dn_tmp_88=&((v_o)->f_autoGainDb);*dn_tmp_88=dn_tmp_89; }
size_t v_ring=({ size_t dn_tmp_90=(v_o)->f_index;
size_t dn_tmp_91=((size_t)(128ULL));
dn_USize_mod(dn_tmp_90,dn_tmp_91); });
{ double dn_tmp_97=({ double dn_tmp_94=v_l;
double dn_tmp_95=(v_o)->f_autoGain;
(dn_tmp_94 * dn_tmp_95); });double *dn_tmp_96=&((*({ dn_t_arr256_F64 *dn_tmp_92=&((v_o)->f_signal);size_t dn_tmp_93=v_ring;&dn_tmp_92->data[dn_index(dn_tmp_93,256)]; })));*dn_tmp_96=dn_tmp_97; }
{ double dn_tmp_105=({ double dn_tmp_102=v_r;
double dn_tmp_103=(v_o)->f_autoGain;
(dn_tmp_102 * dn_tmp_103); });double *dn_tmp_104=&((*({ dn_t_arr256_F64 *dn_tmp_98=&((v_o)->f_signal);size_t dn_tmp_99=({ size_t dn_tmp_100=((size_t)(128ULL));
size_t dn_tmp_101=v_ring;
dn_USize_add(dn_tmp_100,dn_tmp_101); });&dn_tmp_98->data[dn_index(dn_tmp_99,256)]; })));*dn_tmp_104=dn_tmp_105; }
if((v_limiter && ({ size_t dn_tmp_106=(v_o)->f_index;
size_t dn_tmp_107=((size_t)(8ULL));
(dn_tmp_106 >= dn_tmp_107); }))){
size_t v_center=({ size_t dn_tmp_108=(v_o)->f_index;
size_t dn_tmp_109=((size_t)(8ULL));
dn_USize_sub(dn_tmp_108,dn_tmp_109); });
if((((!(v_wasLimiter)) && ({ size_t dn_tmp_110=v_i;
size_t dn_tmp_111=((size_t)(0ULL));
(dn_tmp_110 == dn_tmp_111); })) && ({ size_t dn_tmp_112=(v_o)->f_index;
size_t dn_tmp_113=((size_t)(16ULL));
(dn_tmp_112 >= dn_tmp_113); }))){
{ size_t dn_tmp_117=({ size_t dn_tmp_114=(v_o)->f_index;
size_t dn_tmp_115=((size_t)(16ULL));
dn_USize_sub(dn_tmp_114,dn_tmp_115); });size_t dn_tmp_116=v_center;for(size_t v_k=dn_tmp_117;v_k<dn_tmp_116;v_k++){
{ double dn_tmp_125=({ dn_r_OutputStage * dn_tmp_122=v_o;
size_t dn_tmp_123=v_k;
dn_f_output_estimate(dn_tmp_122,dn_tmp_123); });double *dn_tmp_124=&((*({ dn_t_arr128_F64 *dn_tmp_118=&((v_o)->f_peakHistory);size_t dn_tmp_119=({ size_t dn_tmp_120=v_k;
size_t dn_tmp_121=((size_t)(128ULL));
dn_USize_mod(dn_tmp_120,dn_tmp_121); });&dn_tmp_118->data[dn_index(dn_tmp_119,128)]; })));*dn_tmp_124=dn_tmp_125; }
}
 }
}
else{
}
{ double dn_tmp_133=({ dn_r_OutputStage * dn_tmp_130=v_o;
size_t dn_tmp_131=v_center;
dn_f_output_estimate(dn_tmp_130,dn_tmp_131); });double *dn_tmp_132=&((*({ dn_t_arr128_F64 *dn_tmp_126=&((v_o)->f_peakHistory);size_t dn_tmp_127=({ size_t dn_tmp_128=v_center;
size_t dn_tmp_129=((size_t)(128ULL));
dn_USize_mod(dn_tmp_128,dn_tmp_129); });&dn_tmp_126->data[dn_index(dn_tmp_127,128)]; })));*dn_tmp_132=dn_tmp_133; }
}
else{
}
if(({ size_t dn_tmp_134=(v_o)->f_index;
size_t dn_tmp_135=((size_t)(16ULL));
(dn_tmp_134 < dn_tmp_135); })){
{ float dn_tmp_140=({ float dn_tmp_138=((float)(0.0f));
(float)dn_tmp_138; });float *dn_tmp_139=&((*({ dn_t_foreign_F32 *dn_tmp_136=&(v_lb);size_t dn_tmp_137=v_i;&dn_tmp_136->data[dn_index(dn_tmp_137,dn_tmp_136->len)]; })));*dn_tmp_139=dn_tmp_140; }
{ float dn_tmp_145=({ float dn_tmp_143=((float)(0.0f));
(float)dn_tmp_143; });float *dn_tmp_144=&((*({ dn_t_foreign_F32 *dn_tmp_141=&(v_rb);size_t dn_tmp_142=v_i;&dn_tmp_141->data[dn_index(dn_tmp_142,dn_tmp_141->len)]; })));*dn_tmp_144=dn_tmp_145; }
{ size_t dn_tmp_149=({ size_t dn_tmp_146=(v_o)->f_index;
size_t dn_tmp_147=((size_t)(1ULL));
dn_USize_add(dn_tmp_146,dn_tmp_147); });size_t *dn_tmp_148=&((v_o)->f_index);*dn_tmp_148=dn_tmp_149; }
continue;
}
else{
}
size_t v_outIndex=({ size_t dn_tmp_150=(v_o)->f_index;
size_t dn_tmp_151=((size_t)(16ULL));
dn_USize_sub(dn_tmp_150,dn_tmp_151); });
double v_futurePeak=((double)(0.0));
if(v_limiter){
{ size_t dn_tmp_155=v_outIndex;size_t dn_tmp_154=({ size_t dn_tmp_152=(v_o)->f_index;
size_t dn_tmp_153=((size_t)(7ULL));
dn_USize_sub(dn_tmp_152,dn_tmp_153); });for(size_t v_k=dn_tmp_155;v_k<dn_tmp_154;v_k++){
if(({ double dn_tmp_156=(*({ dn_t_arr128_F64 *dn_tmp_157=&((v_o)->f_peakHistory);size_t dn_tmp_158=({ size_t dn_tmp_159=v_k;
size_t dn_tmp_160=((size_t)(128ULL));
dn_USize_mod(dn_tmp_159,dn_tmp_160); });&dn_tmp_157->data[dn_index(dn_tmp_158,128)]; }));
double dn_tmp_161=v_futurePeak;
(dn_tmp_156 > dn_tmp_161); })){
{ double dn_tmp_167=(*({ dn_t_arr128_F64 *dn_tmp_162=&((v_o)->f_peakHistory);size_t dn_tmp_163=({ size_t dn_tmp_164=v_k;
size_t dn_tmp_165=((size_t)(128ULL));
dn_USize_mod(dn_tmp_164,dn_tmp_165); });&dn_tmp_162->data[dn_index(dn_tmp_163,128)]; }));double *dn_tmp_166=&(v_futurePeak);*dn_tmp_166=dn_tmp_167; }
}
else{
}
}
 }
}
else{
}
if(({ double dn_tmp_168=v_futurePeak;
double dn_tmp_169=v_blockPeak;
(dn_tmp_168 > dn_tmp_169); })){
{ double dn_tmp_171=v_futurePeak;double *dn_tmp_170=&(v_blockPeak);*dn_tmp_170=dn_tmp_171; }
}
else{
}
double v_requested=((double)(1.0));
if((v_limiter && ({ double dn_tmp_172=v_futurePeak;
double dn_tmp_173=((double)(0.0));
(dn_tmp_172 > dn_tmp_173); }))){
{ double dn_tmp_182=({ double dn_tmp_174=({ double dn_tmp_175=v_ceiling;
double dn_tmp_176=({ double dn_tmp_177=v_futurePeak;
double dn_tmp_178=((double)(1.005));
(dn_tmp_177 * dn_tmp_178); });
(dn_tmp_175 / dn_tmp_176); });
double dn_tmp_179=((double)(0.0));
double dn_tmp_180=((double)(1.0));
dn_f_min_f(dn_tmp_174,dn_tmp_179,dn_tmp_180); });double *dn_tmp_181=&(v_requested);*dn_tmp_181=dn_tmp_182; }
}
else{
}
if(({ double dn_tmp_183=v_requested;
double dn_tmp_184=(v_o)->f_limiterGain;
(dn_tmp_183 < dn_tmp_184); })){
{ double dn_tmp_186=v_requested;double *dn_tmp_185=&((v_o)->f_limiterGain);*dn_tmp_185=dn_tmp_186; }
}
else{
{ double dn_tmp_194=({ double dn_tmp_187=(v_o)->f_limiterGain;
double dn_tmp_188=({ double dn_tmp_189=(v_o)->f_releaseAlpha;
double dn_tmp_190=({ double dn_tmp_191=v_requested;
double dn_tmp_192=(v_o)->f_limiterGain;
(dn_tmp_191 - dn_tmp_192); });
(dn_tmp_189 * dn_tmp_190); });
(dn_tmp_187 + dn_tmp_188); });double *dn_tmp_193=&((v_o)->f_limiterGain);*dn_tmp_193=dn_tmp_194; }
}
double v_outL=({ double dn_tmp_195=(*({ dn_t_arr256_F64 *dn_tmp_196=&((v_o)->f_signal);size_t dn_tmp_197=({ size_t dn_tmp_198=v_outIndex;
size_t dn_tmp_199=((size_t)(128ULL));
dn_USize_mod(dn_tmp_198,dn_tmp_199); });&dn_tmp_196->data[dn_index(dn_tmp_197,256)]; }));
double dn_tmp_200=(v_o)->f_limiterGain;
(dn_tmp_195 * dn_tmp_200); });
double v_outR=({ double dn_tmp_201=(*({ dn_t_arr256_F64 *dn_tmp_202=&((v_o)->f_signal);size_t dn_tmp_203=({ size_t dn_tmp_204=((size_t)(128ULL));
size_t dn_tmp_205=({ size_t dn_tmp_206=v_outIndex;
size_t dn_tmp_207=((size_t)(128ULL));
dn_USize_mod(dn_tmp_206,dn_tmp_207); });
dn_USize_add(dn_tmp_204,dn_tmp_205); });&dn_tmp_202->data[dn_index(dn_tmp_203,256)]; }));
double dn_tmp_208=(v_o)->f_limiterGain;
(dn_tmp_201 * dn_tmp_208); });
if(v_limiter){
{ double dn_tmp_213=({ double dn_tmp_209=v_outL;
double dn_tmp_210=(-(v_ceiling));
double dn_tmp_211=v_ceiling;
dn_f_min_f(dn_tmp_209,dn_tmp_210,dn_tmp_211); });double *dn_tmp_212=&(v_outL);*dn_tmp_212=dn_tmp_213; }
{ double dn_tmp_218=({ double dn_tmp_214=v_outR;
double dn_tmp_215=(-(v_ceiling));
double dn_tmp_216=v_ceiling;
dn_f_min_f(dn_tmp_214,dn_tmp_215,dn_tmp_216); });double *dn_tmp_217=&(v_outR);*dn_tmp_217=dn_tmp_218; }
}
else{
}
{ float dn_tmp_223=({ double dn_tmp_221=v_outL;
(float)dn_tmp_221; });float *dn_tmp_222=&((*({ dn_t_foreign_F32 *dn_tmp_219=&(v_lb);size_t dn_tmp_220=v_i;&dn_tmp_219->data[dn_index(dn_tmp_220,dn_tmp_219->len)]; })));*dn_tmp_222=dn_tmp_223; }
{ float dn_tmp_228=({ double dn_tmp_226=v_outR;
(float)dn_tmp_226; });float *dn_tmp_227=&((*({ dn_t_foreign_F32 *dn_tmp_224=&(v_rb);size_t dn_tmp_225=v_i;&dn_tmp_224->data[dn_index(dn_tmp_225,dn_tmp_224->len)]; })));*dn_tmp_227=dn_tmp_228; }
double v_reduction=({ double dn_tmp_229=({ double dn_tmp_230=((double)(20.0));
double dn_tmp_231=({ double dn_tmp_232=({ double dn_tmp_233=(v_o)->f_limiterGain;
double dn_tmp_234=((double)(1.0e-8));
double dn_tmp_235=((double)(1.0e12));
dn_f_min_f(dn_tmp_233,dn_tmp_234,dn_tmp_235); });
log(dn_tmp_232); });
(dn_tmp_230 * dn_tmp_231); });
double dn_tmp_236=({ double dn_tmp_237=((double)(10.0));
log(dn_tmp_237); });
(dn_tmp_229 / dn_tmp_236); });
if(({ double dn_tmp_238=v_reduction;
double dn_tmp_239=v_blockReduction;
(dn_tmp_238 < dn_tmp_239); })){
{ double dn_tmp_241=v_reduction;double *dn_tmp_240=&(v_blockReduction);*dn_tmp_240=dn_tmp_241; }
}
else{
}
{ size_t dn_tmp_245=({ size_t dn_tmp_242=(v_o)->f_index;
size_t dn_tmp_243=((size_t)(1ULL));
dn_USize_add(dn_tmp_242,dn_tmp_243); });size_t *dn_tmp_244=&((v_o)->f_index);*dn_tmp_244=dn_tmp_245; }
}
 }
{ double dn_tmp_247=v_blockPeak;double *dn_tmp_246=&((v_o)->f_estimatedPeak);*dn_tmp_246=dn_tmp_247; }
{ double dn_tmp_249=v_blockReduction;double *dn_tmp_248=&((v_o)->f_reductionDb);*dn_tmp_248=dn_tmp_249; }
}

return 0;
}
static void dn_cb_output_process(dn_r_OutputStage * v_o,float * v_left,float * v_right,size_t v_frames,double v_inputMeanSquare,bool v_autoMatch,bool v_limiter,double v_ceilingDb) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_output_process(v_o,v_left,v_right,v_frames,v_inputMeanSquare,v_autoMatch,v_limiter,v_ceilingDb);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static dn_r_Fir dn_f_fir_create(size_t v_n,bool v_upsampler) {
{
dn_t_buf_F64 volatile v_taps={0};
dn_t_buf_F64 volatile v_history={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_F64(&v_history);
dn_drop_buf_F64(&v_taps);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_taps=({ size_t dn_tmp_2=v_n;
dn_new_buf_F64(dn_tmp_2); });
v_history=({ size_t dn_tmp_3=({ size_t dn_tmp_4=((size_t)(2ULL));
size_t dn_tmp_5=v_n;
dn_USize_mul(dn_tmp_4,dn_tmp_5); });
dn_new_buf_F64(dn_tmp_3); });
double v_middle=({ double dn_tmp_6=({ double dn_tmp_7=({ size_t dn_tmp_8=v_n;
(double)dn_tmp_8; });
double dn_tmp_9=((double)(1.0));
(dn_tmp_7 - dn_tmp_9); });
double dn_tmp_10=((double)(0.5));
(dn_tmp_6 * dn_tmp_10); });
double v_total=((double)(0.0));
{ size_t dn_tmp_12=((size_t)(0ULL));size_t dn_tmp_11=v_n;for(size_t v_i=dn_tmp_12;v_i<dn_tmp_11;v_i++){
double v_x=({ double dn_tmp_13=({ double dn_tmp_14=((double)(2.0));
double dn_tmp_15=((double)(0.245));
(dn_tmp_14 * dn_tmp_15); });
double dn_tmp_16=({ double dn_tmp_17=({ size_t dn_tmp_18=v_i;
(double)dn_tmp_18; });
double dn_tmp_19=v_middle;
(dn_tmp_17 - dn_tmp_19); });
(dn_tmp_13 * dn_tmp_16); });
double v_si=((double)(1.0));
if(({ double dn_tmp_20=({ double dn_tmp_21=v_x;
fabs(dn_tmp_21); });
double dn_tmp_22=((double)(1.0e-12));
(dn_tmp_20 >= dn_tmp_22); })){
{ double dn_tmp_31=({ double dn_tmp_23=({ double dn_tmp_24=({ double dn_tmp_25=({ dn_f_pi(); });
double dn_tmp_26=v_x;
(dn_tmp_25 * dn_tmp_26); });
sin(dn_tmp_24); });
double dn_tmp_27=({ double dn_tmp_28=({ dn_f_pi(); });
double dn_tmp_29=v_x;
(dn_tmp_28 * dn_tmp_29); });
(dn_tmp_23 / dn_tmp_27); });double *dn_tmp_30=&(v_si);*dn_tmp_30=dn_tmp_31; }
}
else{
}
double v_a=({ double dn_tmp_32=({ double dn_tmp_33=({ double dn_tmp_34=((double)(2.0));
double dn_tmp_35=({ dn_f_pi(); });
(dn_tmp_34 * dn_tmp_35); });
double dn_tmp_36=({ size_t dn_tmp_37=v_i;
(double)dn_tmp_37; });
(dn_tmp_33 * dn_tmp_36); });
double dn_tmp_38=({ size_t dn_tmp_39=({ size_t dn_tmp_40=v_n;
size_t dn_tmp_41=((size_t)(1ULL));
dn_USize_sub(dn_tmp_40,dn_tmp_41); });
(double)dn_tmp_39; });
(dn_tmp_32 / dn_tmp_38); });
double v_window=({ double dn_tmp_42=({ double dn_tmp_43=({ double dn_tmp_44=((double)(0.35875));
double dn_tmp_45=({ double dn_tmp_46=((double)(0.48829));
double dn_tmp_47=({ double dn_tmp_48=v_a;
cos(dn_tmp_48); });
(dn_tmp_46 * dn_tmp_47); });
(dn_tmp_44 - dn_tmp_45); });
double dn_tmp_49=({ double dn_tmp_50=((double)(0.14128));
double dn_tmp_51=({ double dn_tmp_52=({ double dn_tmp_53=((double)(2.0));
double dn_tmp_54=v_a;
(dn_tmp_53 * dn_tmp_54); });
cos(dn_tmp_52); });
(dn_tmp_50 * dn_tmp_51); });
(dn_tmp_43 + dn_tmp_49); });
double dn_tmp_55=({ double dn_tmp_56=((double)(0.01168));
double dn_tmp_57=({ double dn_tmp_58=({ double dn_tmp_59=((double)(3.0));
double dn_tmp_60=v_a;
(dn_tmp_59 * dn_tmp_60); });
cos(dn_tmp_58); });
(dn_tmp_56 * dn_tmp_57); });
(dn_tmp_42 - dn_tmp_55); });
double v_v=({ double dn_tmp_61=({ double dn_tmp_62=({ double dn_tmp_63=((double)(2.0));
double dn_tmp_64=((double)(0.245));
(dn_tmp_63 * dn_tmp_64); });
double dn_tmp_65=v_si;
(dn_tmp_62 * dn_tmp_65); });
double dn_tmp_66=v_window;
(dn_tmp_61 * dn_tmp_66); });
{ double dn_tmp_70=v_v;double *dn_tmp_69=&((*({ dn_t_buf_F64 volatile *dn_tmp_67=&(v_taps);size_t dn_tmp_68=v_i;&dn_tmp_67->data[dn_index(dn_tmp_68,dn_tmp_67->len)]; })));*dn_tmp_69=dn_tmp_70; }
{ double dn_tmp_74=({ double dn_tmp_71=v_total;
double dn_tmp_72=v_v;
(dn_tmp_71 + dn_tmp_72); });double *dn_tmp_73=&(v_total);*dn_tmp_73=dn_tmp_74; }
}
 }
double v_factor=({ double dn_tmp_75=((double)(1.0));
double dn_tmp_76=v_total;
(dn_tmp_75 / dn_tmp_76); });
if(v_upsampler){
{ double dn_tmp_80=({ double dn_tmp_77=((double)(2.0));
double dn_tmp_78=v_total;
(dn_tmp_77 / dn_tmp_78); });double *dn_tmp_79=&(v_factor);*dn_tmp_79=dn_tmp_80; }
}
else{
}
{ size_t dn_tmp_82=((size_t)(0ULL));size_t dn_tmp_81=v_n;for(size_t v_i=dn_tmp_82;v_i<dn_tmp_81;v_i++){
{ double dn_tmp_90=({ double dn_tmp_85=(*({ dn_t_buf_F64 volatile *dn_tmp_86=&(v_taps);size_t dn_tmp_87=v_i;&dn_tmp_86->data[dn_index(dn_tmp_87,dn_tmp_86->len)]; }));
double dn_tmp_88=v_factor;
(dn_tmp_85 * dn_tmp_88); });double *dn_tmp_89=&((*({ dn_t_buf_F64 volatile *dn_tmp_83=&(v_taps);size_t dn_tmp_84=v_i;&dn_tmp_83->data[dn_index(dn_tmp_84,dn_tmp_83->len)]; })));*dn_tmp_89=dn_tmp_90; }
}
 }
{ dn_r_Fir dn_tmp_97=({ dn_t_buf_F64 dn_tmp_91=({ dn_t_buf_F64 dn_tmp_92=v_taps;v_taps=(dn_t_buf_F64){0};dn_adopt_buf_F64(&dn_tmp_92);dn_tmp_92; });
dn_adopt_buf_F64(&dn_tmp_91);
dn_t_buf_F64 dn_tmp_93=({ dn_t_buf_F64 dn_tmp_94=v_history;v_history=(dn_t_buf_F64){0};dn_adopt_buf_F64(&dn_tmp_94);dn_tmp_94; });
dn_adopt_buf_F64(&dn_tmp_93);
size_t dn_tmp_95=((size_t)(0ULL));
size_t dn_tmp_96=v_n;
((dn_r_Fir){dn_tmp_91,dn_tmp_93,dn_tmp_95,dn_tmp_96}); });dn_adopt_r3_Fir(&dn_tmp_97);dn_drop_buf_F64(&v_history);
dn_drop_buf_F64(&v_taps);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_97; }
dn_drop_buf_F64(&v_history);
dn_drop_buf_F64(&v_taps);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_r_Fir dn_cb_fir_create(size_t v_n,bool v_upsampler) {
 if(!dn_enter()){return (dn_r_Fir){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_Fir){0};}
 dn_r_Fir dn_result=dn_f_fir_create(v_n,v_upsampler);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_fir_reset(dn_r_Fir * v_f) {
{
{ size_t dn_tmp_2=((size_t)(0ULL));size_t dn_tmp_1=({ dn_t_buf_F64 const * dn_tmp_0=(&((v_f)->f_history));
(*(dn_tmp_0)).len; });for(size_t v_i=dn_tmp_2;v_i<dn_tmp_1;v_i++){
{ double dn_tmp_6=((double)(0.0));double *dn_tmp_5=&((*({ dn_t_buf_F64 volatile *dn_tmp_3=&((v_f)->f_history);size_t dn_tmp_4=v_i;&dn_tmp_3->data[dn_tmp_4]; })));*dn_tmp_5=dn_tmp_6; }
}
 }
{ size_t dn_tmp_8=((size_t)(0ULL));size_t *dn_tmp_7=&((v_f)->f_index);*dn_tmp_7=dn_tmp_8; }
}

return 0;
}
static void dn_cb_fir_reset(dn_r_Fir * v_f) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_fir_reset(v_f);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_fir_push(dn_r_Fir * v_f,double v_x) {
{
size_t v_i=(v_f)->f_index;
{ double dn_tmp_3=v_x;double *dn_tmp_2=&((*({ dn_t_buf_F64 volatile *dn_tmp_0=&((v_f)->f_history);size_t dn_tmp_1=v_i;&dn_tmp_0->data[dn_index(dn_tmp_1,dn_tmp_0->len)]; })));*dn_tmp_2=dn_tmp_3; }
{ double dn_tmp_9=v_x;double *dn_tmp_8=&((*({ dn_t_buf_F64 volatile *dn_tmp_4=&((v_f)->f_history);size_t dn_tmp_5=({ size_t dn_tmp_6=v_i;
size_t dn_tmp_7=(v_f)->f_size;
dn_USize_add(dn_tmp_6,dn_tmp_7); });&dn_tmp_4->data[dn_index(dn_tmp_5,dn_tmp_4->len)]; })));*dn_tmp_8=dn_tmp_9; }
{ size_t dn_tmp_13=({ size_t dn_tmp_10=v_i;
size_t dn_tmp_11=((size_t)(1ULL));
dn_USize_add(dn_tmp_10,dn_tmp_11); });size_t *dn_tmp_12=&((v_f)->f_index);*dn_tmp_12=dn_tmp_13; }
if(({ size_t dn_tmp_14=(v_f)->f_index;
size_t dn_tmp_15=(v_f)->f_size;
(dn_tmp_14 == dn_tmp_15); })){
{ size_t dn_tmp_17=((size_t)(0ULL));size_t *dn_tmp_16=&((v_f)->f_index);*dn_tmp_16=dn_tmp_17; }
}
else{
}
}

return 0;
}
static void dn_cb_fir_push(dn_r_Fir * v_f,double v_x) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_fir_push(v_f,v_x);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static dn_r_Stereo dn_f_fir_upsample(dn_r_Fir * v_f,double v_x) {
{
(void)(({ dn_r_Fir * dn_tmp_0=v_f;
double dn_tmp_1=v_x;
dn_f_fir_push(dn_tmp_0,dn_tmp_1); }));
size_t v_newest=({ size_t dn_tmp_2=({ size_t dn_tmp_3=(v_f)->f_index;
size_t dn_tmp_4=(v_f)->f_size;
dn_USize_add(dn_tmp_3,dn_tmp_4); });
size_t dn_tmp_5=((size_t)(1ULL));
dn_USize_sub(dn_tmp_2,dn_tmp_5); });
size_t v_ec=({ size_t dn_tmp_6=({ size_t dn_tmp_7=(v_f)->f_size;
size_t dn_tmp_8=((size_t)(1ULL));
dn_USize_add(dn_tmp_7,dn_tmp_8); });
size_t dn_tmp_9=((size_t)(2ULL));
dn_USize_div(dn_tmp_6,dn_tmp_9); });
size_t v_oc=({ size_t dn_tmp_10=(v_f)->f_size;
size_t dn_tmp_11=((size_t)(2ULL));
dn_USize_div(dn_tmp_10,dn_tmp_11); });
double v_even=((double)(0.0));
double v_odd=((double)(0.0));
{ size_t dn_tmp_15=((size_t)(0ULL));size_t dn_tmp_14=({ size_t dn_tmp_12=v_ec;
size_t dn_tmp_13=((size_t)(2ULL));
dn_USize_div(dn_tmp_12,dn_tmp_13); });for(size_t v_k=dn_tmp_15;v_k<dn_tmp_14;v_k++){
{ double dn_tmp_39=({ double dn_tmp_16=v_even;
double dn_tmp_17=({ double dn_tmp_18=(*({ dn_t_buf_F64 volatile *dn_tmp_19=&((v_f)->f_taps);size_t dn_tmp_20=({ size_t dn_tmp_21=((size_t)(2ULL));
size_t dn_tmp_22=v_k;
dn_USize_mul(dn_tmp_21,dn_tmp_22); });&dn_tmp_19->data[dn_index(dn_tmp_20,dn_tmp_19->len)]; }));
double dn_tmp_23=({ double dn_tmp_24=(*({ dn_t_buf_F64 volatile *dn_tmp_25=&((v_f)->f_history);size_t dn_tmp_26=({ size_t dn_tmp_27=v_newest;
size_t dn_tmp_28=v_k;
dn_USize_sub(dn_tmp_27,dn_tmp_28); });&dn_tmp_25->data[dn_index(dn_tmp_26,dn_tmp_25->len)]; }));
double dn_tmp_29=(*({ dn_t_buf_F64 volatile *dn_tmp_30=&((v_f)->f_history);size_t dn_tmp_31=({ size_t dn_tmp_32=v_newest;
size_t dn_tmp_33=({ size_t dn_tmp_34=({ size_t dn_tmp_35=v_ec;
size_t dn_tmp_36=((size_t)(1ULL));
dn_USize_sub(dn_tmp_35,dn_tmp_36); });
size_t dn_tmp_37=v_k;
dn_USize_sub(dn_tmp_34,dn_tmp_37); });
dn_USize_sub(dn_tmp_32,dn_tmp_33); });&dn_tmp_30->data[dn_index(dn_tmp_31,dn_tmp_30->len)]; }));
(dn_tmp_24 + dn_tmp_29); });
(dn_tmp_18 * dn_tmp_23); });
(dn_tmp_16 + dn_tmp_17); });double *dn_tmp_38=&(v_even);*dn_tmp_38=dn_tmp_39; }
}
 }
if(({ size_t dn_tmp_40=({ size_t dn_tmp_41=v_ec;
size_t dn_tmp_42=((size_t)(2ULL));
dn_USize_mod(dn_tmp_41,dn_tmp_42); });
size_t dn_tmp_43=((size_t)(0ULL));
(dn_tmp_40 != dn_tmp_43); })){
{ double dn_tmp_61=({ double dn_tmp_44=v_even;
double dn_tmp_45=({ double dn_tmp_46=(*({ dn_t_buf_F64 volatile *dn_tmp_47=&((v_f)->f_taps);size_t dn_tmp_48=({ size_t dn_tmp_49=((size_t)(2ULL));
size_t dn_tmp_50=({ size_t dn_tmp_51=v_ec;
size_t dn_tmp_52=((size_t)(2ULL));
dn_USize_div(dn_tmp_51,dn_tmp_52); });
dn_USize_mul(dn_tmp_49,dn_tmp_50); });&dn_tmp_47->data[dn_index(dn_tmp_48,dn_tmp_47->len)]; }));
double dn_tmp_53=(*({ dn_t_buf_F64 volatile *dn_tmp_54=&((v_f)->f_history);size_t dn_tmp_55=({ size_t dn_tmp_56=v_newest;
size_t dn_tmp_57=({ size_t dn_tmp_58=v_ec;
size_t dn_tmp_59=((size_t)(2ULL));
dn_USize_div(dn_tmp_58,dn_tmp_59); });
dn_USize_sub(dn_tmp_56,dn_tmp_57); });&dn_tmp_54->data[dn_index(dn_tmp_55,dn_tmp_54->len)]; }));
(dn_tmp_46 * dn_tmp_53); });
(dn_tmp_44 + dn_tmp_45); });double *dn_tmp_60=&(v_even);*dn_tmp_60=dn_tmp_61; }
}
else{
}
{ size_t dn_tmp_65=((size_t)(0ULL));size_t dn_tmp_64=({ size_t dn_tmp_62=v_oc;
size_t dn_tmp_63=((size_t)(2ULL));
dn_USize_div(dn_tmp_62,dn_tmp_63); });for(size_t v_k=dn_tmp_65;v_k<dn_tmp_64;v_k++){
{ double dn_tmp_91=({ double dn_tmp_66=v_odd;
double dn_tmp_67=({ double dn_tmp_68=(*({ dn_t_buf_F64 volatile *dn_tmp_69=&((v_f)->f_taps);size_t dn_tmp_70=({ size_t dn_tmp_71=({ size_t dn_tmp_72=((size_t)(2ULL));
size_t dn_tmp_73=v_k;
dn_USize_mul(dn_tmp_72,dn_tmp_73); });
size_t dn_tmp_74=((size_t)(1ULL));
dn_USize_add(dn_tmp_71,dn_tmp_74); });&dn_tmp_69->data[dn_index(dn_tmp_70,dn_tmp_69->len)]; }));
double dn_tmp_75=({ double dn_tmp_76=(*({ dn_t_buf_F64 volatile *dn_tmp_77=&((v_f)->f_history);size_t dn_tmp_78=({ size_t dn_tmp_79=v_newest;
size_t dn_tmp_80=v_k;
dn_USize_sub(dn_tmp_79,dn_tmp_80); });&dn_tmp_77->data[dn_index(dn_tmp_78,dn_tmp_77->len)]; }));
double dn_tmp_81=(*({ dn_t_buf_F64 volatile *dn_tmp_82=&((v_f)->f_history);size_t dn_tmp_83=({ size_t dn_tmp_84=v_newest;
size_t dn_tmp_85=({ size_t dn_tmp_86=({ size_t dn_tmp_87=v_oc;
size_t dn_tmp_88=((size_t)(1ULL));
dn_USize_sub(dn_tmp_87,dn_tmp_88); });
size_t dn_tmp_89=v_k;
dn_USize_sub(dn_tmp_86,dn_tmp_89); });
dn_USize_sub(dn_tmp_84,dn_tmp_85); });&dn_tmp_82->data[dn_index(dn_tmp_83,dn_tmp_82->len)]; }));
(dn_tmp_76 + dn_tmp_81); });
(dn_tmp_68 * dn_tmp_75); });
(dn_tmp_66 + dn_tmp_67); });double *dn_tmp_90=&(v_odd);*dn_tmp_90=dn_tmp_91; }
}
 }
if(({ size_t dn_tmp_92=({ size_t dn_tmp_93=v_oc;
size_t dn_tmp_94=((size_t)(2ULL));
dn_USize_mod(dn_tmp_93,dn_tmp_94); });
size_t dn_tmp_95=((size_t)(0ULL));
(dn_tmp_92 != dn_tmp_95); })){
{ double dn_tmp_115=({ double dn_tmp_96=v_odd;
double dn_tmp_97=({ double dn_tmp_98=(*({ dn_t_buf_F64 volatile *dn_tmp_99=&((v_f)->f_taps);size_t dn_tmp_100=({ size_t dn_tmp_101=({ size_t dn_tmp_102=((size_t)(2ULL));
size_t dn_tmp_103=({ size_t dn_tmp_104=v_oc;
size_t dn_tmp_105=((size_t)(2ULL));
dn_USize_div(dn_tmp_104,dn_tmp_105); });
dn_USize_mul(dn_tmp_102,dn_tmp_103); });
size_t dn_tmp_106=((size_t)(1ULL));
dn_USize_add(dn_tmp_101,dn_tmp_106); });&dn_tmp_99->data[dn_index(dn_tmp_100,dn_tmp_99->len)]; }));
double dn_tmp_107=(*({ dn_t_buf_F64 volatile *dn_tmp_108=&((v_f)->f_history);size_t dn_tmp_109=({ size_t dn_tmp_110=v_newest;
size_t dn_tmp_111=({ size_t dn_tmp_112=v_oc;
size_t dn_tmp_113=((size_t)(2ULL));
dn_USize_div(dn_tmp_112,dn_tmp_113); });
dn_USize_sub(dn_tmp_110,dn_tmp_111); });&dn_tmp_108->data[dn_index(dn_tmp_109,dn_tmp_108->len)]; }));
(dn_tmp_98 * dn_tmp_107); });
(dn_tmp_96 + dn_tmp_97); });double *dn_tmp_114=&(v_odd);*dn_tmp_114=dn_tmp_115; }
}
else{
}
{ dn_r_Stereo dn_tmp_118=({ double dn_tmp_116=v_even;
double dn_tmp_117=v_odd;
((dn_r_Stereo){dn_tmp_116,dn_tmp_117}); });return dn_tmp_118; }
}

dn_fail("missing native return");
}
static dn_r_Stereo dn_cb_fir_upsample(dn_r_Fir * v_f,double v_x) {
 if(!dn_enter()){return (dn_r_Stereo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_Stereo){0};}
 dn_r_Stereo dn_result=dn_f_fir_upsample(v_f,v_x);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_fir_downsample(dn_r_Fir * v_f,double v_a,double v_b) {
{
(void)(({ dn_r_Fir * dn_tmp_0=v_f;
double dn_tmp_1=v_a;
dn_f_fir_push(dn_tmp_0,dn_tmp_1); }));
(void)(({ dn_r_Fir * dn_tmp_2=v_f;
double dn_tmp_3=v_b;
dn_f_fir_push(dn_tmp_2,dn_tmp_3); }));
size_t v_newest=({ size_t dn_tmp_4=({ size_t dn_tmp_5=(v_f)->f_index;
size_t dn_tmp_6=(v_f)->f_size;
dn_USize_add(dn_tmp_5,dn_tmp_6); });
size_t dn_tmp_7=((size_t)(1ULL));
dn_USize_sub(dn_tmp_4,dn_tmp_7); });
size_t v_half=({ size_t dn_tmp_8=(v_f)->f_size;
size_t dn_tmp_9=((size_t)(2ULL));
dn_USize_div(dn_tmp_8,dn_tmp_9); });
double v_s=({ double dn_tmp_10=(*({ dn_t_buf_F64 volatile *dn_tmp_11=&((v_f)->f_taps);size_t dn_tmp_12=v_half;&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; }));
double dn_tmp_13=(*({ dn_t_buf_F64 volatile *dn_tmp_14=&((v_f)->f_history);size_t dn_tmp_15=({ size_t dn_tmp_16=v_newest;
size_t dn_tmp_17=v_half;
dn_USize_sub(dn_tmp_16,dn_tmp_17); });&dn_tmp_14->data[dn_index(dn_tmp_15,dn_tmp_14->len)]; }));
(dn_tmp_10 * dn_tmp_13); });
{ size_t dn_tmp_19=((size_t)(0ULL));size_t dn_tmp_18=v_half;for(size_t v_k=dn_tmp_19;v_k<dn_tmp_18;v_k++){
{ double dn_tmp_41=({ double dn_tmp_20=v_s;
double dn_tmp_21=({ double dn_tmp_22=(*({ dn_t_buf_F64 volatile *dn_tmp_23=&((v_f)->f_taps);size_t dn_tmp_24=v_k;&dn_tmp_23->data[dn_index(dn_tmp_24,dn_tmp_23->len)]; }));
double dn_tmp_25=({ double dn_tmp_26=(*({ dn_t_buf_F64 volatile *dn_tmp_27=&((v_f)->f_history);size_t dn_tmp_28=({ size_t dn_tmp_29=v_newest;
size_t dn_tmp_30=v_k;
dn_USize_sub(dn_tmp_29,dn_tmp_30); });&dn_tmp_27->data[dn_index(dn_tmp_28,dn_tmp_27->len)]; }));
double dn_tmp_31=(*({ dn_t_buf_F64 volatile *dn_tmp_32=&((v_f)->f_history);size_t dn_tmp_33=({ size_t dn_tmp_34=v_newest;
size_t dn_tmp_35=({ size_t dn_tmp_36=({ size_t dn_tmp_37=(v_f)->f_size;
size_t dn_tmp_38=((size_t)(1ULL));
dn_USize_sub(dn_tmp_37,dn_tmp_38); });
size_t dn_tmp_39=v_k;
dn_USize_sub(dn_tmp_36,dn_tmp_39); });
dn_USize_sub(dn_tmp_34,dn_tmp_35); });&dn_tmp_32->data[dn_index(dn_tmp_33,dn_tmp_32->len)]; }));
(dn_tmp_26 + dn_tmp_31); });
(dn_tmp_22 * dn_tmp_25); });
(dn_tmp_20 + dn_tmp_21); });double *dn_tmp_40=&(v_s);*dn_tmp_40=dn_tmp_41; }
}
 }
{ double dn_tmp_42=v_s;return dn_tmp_42; }
}

dn_fail("missing native return");
}
static double dn_cb_fir_downsample(dn_r_Fir * v_f,double v_a,double v_b) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_fir_downsample(v_f,v_a,v_b);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static size_t dn_f_folded_compensation(size_t v_depth) {
{
if(({ size_t dn_tmp_0=v_depth;
size_t dn_tmp_1=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); })){
{ size_t dn_tmp_2=((size_t)(62ULL));return dn_tmp_2; }
}
else{
}
if(({ size_t dn_tmp_3=v_depth;
size_t dn_tmp_4=((size_t)(1ULL));
(dn_tmp_3 == dn_tmp_4); })){
{ size_t dn_tmp_5=((size_t)(31ULL));return dn_tmp_5; }
}
else{
}
if(({ size_t dn_tmp_6=v_depth;
size_t dn_tmp_7=((size_t)(2ULL));
(dn_tmp_6 == dn_tmp_7); })){
{ size_t dn_tmp_8=((size_t)(16ULL));return dn_tmp_8; }
}
else{
}
if(({ size_t dn_tmp_9=v_depth;
size_t dn_tmp_10=((size_t)(3ULL));
(dn_tmp_9 == dn_tmp_10); })){
{ size_t dn_tmp_11=((size_t)(8ULL));return dn_tmp_11; }
}
else{
}
if(({ size_t dn_tmp_12=v_depth;
size_t dn_tmp_13=((size_t)(4ULL));
(dn_tmp_12 == dn_tmp_13); })){
{ size_t dn_tmp_14=((size_t)(4ULL));return dn_tmp_14; }
}
else{
}
{ size_t dn_tmp_15=((size_t)(2ULL));return dn_tmp_15; }
}

dn_fail("missing native return");
}
static size_t dn_cb_folded_compensation(size_t v_depth) {
 if(!dn_enter()){return (size_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (size_t){0};}
 size_t dn_result=dn_f_folded_compensation(v_depth);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_folded_reset(dn_r_Folded * v_f) {
{
{ size_t dn_tmp_1=((size_t)(0ULL));size_t dn_tmp_0=((size_t)(1536ULL));for(size_t v_i=dn_tmp_1;v_i<dn_tmp_0;v_i++){
{ double dn_tmp_5=((double)(0.0));double *dn_tmp_4=&((*({ dn_t_arr1536_F64 *dn_tmp_2=&((v_f)->f_history);size_t dn_tmp_3=v_i;&dn_tmp_2->data[dn_index(dn_tmp_3,1536)]; })));*dn_tmp_4=dn_tmp_5; }
}
 }
{ size_t dn_tmp_7=((size_t)(0ULL));size_t *dn_tmp_6=&((v_f)->f_index);*dn_tmp_6=dn_tmp_7; }
}

return 0;
}
static void dn_cb_folded_reset(dn_r_Folded * v_f) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_folded_reset(v_f);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static dn_r_Folded dn_f_folded_prepare(void) {
{
dn_r_Folded v_f=({ dn_t_arr2304_F64 dn_tmp_0=({ ((dn_t_arr2304_F64){0}); });
dn_t_arr6_USize dn_tmp_1=({ ((dn_t_arr6_USize){0}); });
dn_t_arr1536_F64 dn_tmp_2=({ ((dn_t_arr1536_F64){0}); });
size_t dn_tmp_3=((size_t)(0ULL));
((dn_r_Folded){dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3}); });
{ size_t dn_tmp_5=((size_t)(0ULL));size_t dn_tmp_4=((size_t)(6ULL));for(size_t v_depth=dn_tmp_5;v_depth<dn_tmp_4;v_depth++){
dn_t_buf_r3_Fir volatile v_ups={0};
dn_t_buf_r3_Fir volatile v_downs={0};
dn_boundary dn_tmp_6={0};jmp_buf dn_tmp_7;dn_tmp_6.jump=&dn_tmp_7;dn_tmp_6.previous=dn_current;dn_tmp_6.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_6;if(setjmp(dn_tmp_7)){dn_current=dn_tmp_6.previous;dn_drop_buf_r3_Fir(&v_downs);
dn_drop_buf_r3_Fir(&v_ups);
dn_boundary_end(&dn_tmp_6,1);dn_raise();}
v_ups=({ size_t dn_tmp_8=((size_t)(0ULL));
dn_new_buf_r3_Fir(dn_tmp_8); });
v_downs=({ size_t dn_tmp_9=((size_t)(0ULL));
dn_new_buf_r3_Fir(dn_tmp_9); });
(void)(({ dn_t_buf_r3_Fir * dn_tmp_10=(&(v_ups));
dn_r_Fir dn_tmp_11=({ size_t dn_tmp_12=((size_t)(191ULL));
bool dn_tmp_13=1;
dn_f_fir_create(dn_tmp_12,dn_tmp_13); });
dn_adopt_r3_Fir(&dn_tmp_11);
dn_push_buf_r3_Fir(dn_tmp_10,dn_tmp_11); }));
(void)(({ dn_t_buf_r3_Fir * dn_tmp_14=(&(v_downs));
dn_r_Fir dn_tmp_15=({ size_t dn_tmp_16=((size_t)(191ULL));
bool dn_tmp_17=0;
dn_f_fir_create(dn_tmp_16,dn_tmp_17); });
dn_adopt_r3_Fir(&dn_tmp_15);
dn_push_buf_r3_Fir(dn_tmp_14,dn_tmp_15); }));
{ size_t dn_tmp_19=((size_t)(0ULL));size_t dn_tmp_18=((size_t)(5ULL));for(size_t v_st=dn_tmp_19;v_st<dn_tmp_18;v_st++){
(void)(({ dn_t_buf_r3_Fir * dn_tmp_20=(&(v_ups));
dn_r_Fir dn_tmp_21=({ size_t dn_tmp_22=((size_t)(127ULL));
bool dn_tmp_23=1;
dn_f_fir_create(dn_tmp_22,dn_tmp_23); });
dn_adopt_r3_Fir(&dn_tmp_21);
dn_push_buf_r3_Fir(dn_tmp_20,dn_tmp_21); }));
(void)(({ dn_t_buf_r3_Fir * dn_tmp_24=(&(v_downs));
dn_r_Fir dn_tmp_25=({ size_t dn_tmp_26=((size_t)(127ULL));
bool dn_tmp_27=0;
dn_f_fir_create(dn_tmp_26,dn_tmp_27); });
dn_adopt_r3_Fir(&dn_tmp_25);
dn_push_buf_r3_Fir(dn_tmp_24,dn_tmp_25); }));
}
 }
dn_t_arr384_F64 v_impulse=({ ((dn_t_arr384_F64){0}); });
{ size_t dn_tmp_29=((size_t)(0ULL));size_t dn_tmp_28=((size_t)(384ULL));for(size_t v_n=dn_tmp_29;v_n<dn_tmp_28;v_n++){
dn_t_arr64_F64 v_high=({ ((dn_t_arr64_F64){0}); });
double v_value=((double)(0.0));
if(({ size_t dn_tmp_30=v_n;
size_t dn_tmp_31=((size_t)(0ULL));
(dn_tmp_30 == dn_tmp_31); })){
{ double dn_tmp_33=((double)(1.0));double *dn_tmp_32=&(v_value);*dn_tmp_32=dn_tmp_33; }
}
else{
}
dn_r_Stereo v_first=({ dn_r_Fir * dn_tmp_34=(&((*({ dn_t_buf_r3_Fir volatile *dn_tmp_35=&(v_ups);size_t dn_tmp_36=((size_t)(0ULL));&dn_tmp_35->data[dn_index(dn_tmp_36,dn_tmp_35->len)]; }))));
double dn_tmp_37=v_value;
dn_f_fir_upsample(dn_tmp_34,dn_tmp_37); });
{ double dn_tmp_41=(v_first).f_left;double *dn_tmp_40=&((*({ dn_t_arr64_F64 *dn_tmp_38=&(v_high);size_t dn_tmp_39=((size_t)(0ULL));&dn_tmp_38->data[dn_index(dn_tmp_39,64)]; })));*dn_tmp_40=dn_tmp_41; }
{ double dn_tmp_45=(v_first).f_right;double *dn_tmp_44=&((*({ dn_t_arr64_F64 *dn_tmp_42=&(v_high);size_t dn_tmp_43=((size_t)(1ULL));&dn_tmp_42->data[dn_index(dn_tmp_43,64)]; })));*dn_tmp_44=dn_tmp_45; }
size_t v_count=((size_t)(2ULL));
{ size_t dn_tmp_47=((size_t)(0ULL));size_t dn_tmp_46=v_depth;for(size_t v_stage=dn_tmp_47;v_stage<dn_tmp_46;v_stage++){
dn_t_arr64_F64 v_next=({ ((dn_t_arr64_F64){0}); });
{ size_t dn_tmp_49=((size_t)(0ULL));size_t dn_tmp_48=v_count;for(size_t v_i=dn_tmp_49;v_i<dn_tmp_48;v_i++){
dn_r_Stereo v_a=({ dn_r_Fir * dn_tmp_50=(&((*({ dn_t_buf_r3_Fir volatile *dn_tmp_51=&(v_ups);size_t dn_tmp_52=({ size_t dn_tmp_53=v_stage;
size_t dn_tmp_54=((size_t)(1ULL));
dn_USize_add(dn_tmp_53,dn_tmp_54); });&dn_tmp_51->data[dn_index(dn_tmp_52,dn_tmp_51->len)]; }))));
double dn_tmp_55=(*({ dn_t_arr64_F64 *dn_tmp_56=&(v_high);size_t dn_tmp_57=v_i;&dn_tmp_56->data[dn_index(dn_tmp_57,64)]; }));
dn_f_fir_upsample(dn_tmp_50,dn_tmp_55); });
{ double dn_tmp_63=(v_a).f_left;double *dn_tmp_62=&((*({ dn_t_arr64_F64 *dn_tmp_58=&(v_next);size_t dn_tmp_59=({ size_t dn_tmp_60=((size_t)(2ULL));
size_t dn_tmp_61=v_i;
dn_USize_mul(dn_tmp_60,dn_tmp_61); });&dn_tmp_58->data[dn_index(dn_tmp_59,64)]; })));*dn_tmp_62=dn_tmp_63; }
{ double dn_tmp_71=(v_a).f_right;double *dn_tmp_70=&((*({ dn_t_arr64_F64 *dn_tmp_64=&(v_next);size_t dn_tmp_65=({ size_t dn_tmp_66=({ size_t dn_tmp_67=((size_t)(2ULL));
size_t dn_tmp_68=v_i;
dn_USize_mul(dn_tmp_67,dn_tmp_68); });
size_t dn_tmp_69=((size_t)(1ULL));
dn_USize_add(dn_tmp_66,dn_tmp_69); });&dn_tmp_64->data[dn_index(dn_tmp_65,64)]; })));*dn_tmp_70=dn_tmp_71; }
}
 }
{ size_t dn_tmp_75=((size_t)(0ULL));size_t dn_tmp_74=({ size_t dn_tmp_72=v_count;
size_t dn_tmp_73=((size_t)(2ULL));
dn_USize_mul(dn_tmp_72,dn_tmp_73); });for(size_t v_i=dn_tmp_75;v_i<dn_tmp_74;v_i++){
{ double dn_tmp_81=(*({ dn_t_arr64_F64 *dn_tmp_78=&(v_next);size_t dn_tmp_79=v_i;&dn_tmp_78->data[dn_index(dn_tmp_79,64)]; }));double *dn_tmp_80=&((*({ dn_t_arr64_F64 *dn_tmp_76=&(v_high);size_t dn_tmp_77=v_i;&dn_tmp_76->data[dn_index(dn_tmp_77,64)]; })));*dn_tmp_80=dn_tmp_81; }
}
 }
{ size_t dn_tmp_85=({ size_t dn_tmp_82=v_count;
size_t dn_tmp_83=((size_t)(2ULL));
dn_USize_mul(dn_tmp_82,dn_tmp_83); });size_t *dn_tmp_84=&(v_count);*dn_tmp_84=dn_tmp_85; }
}
 }
size_t v_stage=v_depth;
while(({ size_t dn_tmp_86=v_stage;
size_t dn_tmp_87=((size_t)(0ULL));
(dn_tmp_86 > dn_tmp_87); })){
{ size_t dn_tmp_91=({ size_t dn_tmp_88=v_stage;
size_t dn_tmp_89=((size_t)(1ULL));
dn_USize_sub(dn_tmp_88,dn_tmp_89); });size_t *dn_tmp_90=&(v_stage);*dn_tmp_90=dn_tmp_91; }
{ size_t dn_tmp_95=((size_t)(0ULL));size_t dn_tmp_94=({ size_t dn_tmp_92=v_count;
size_t dn_tmp_93=((size_t)(2ULL));
dn_USize_div(dn_tmp_92,dn_tmp_93); });for(size_t v_i=dn_tmp_95;v_i<dn_tmp_94;v_i++){
{ double dn_tmp_116=({ dn_r_Fir * dn_tmp_98=(&((*({ dn_t_buf_r3_Fir volatile *dn_tmp_99=&(v_downs);size_t dn_tmp_100=({ size_t dn_tmp_101=v_stage;
size_t dn_tmp_102=((size_t)(1ULL));
dn_USize_add(dn_tmp_101,dn_tmp_102); });&dn_tmp_99->data[dn_index(dn_tmp_100,dn_tmp_99->len)]; }))));
double dn_tmp_103=(*({ dn_t_arr64_F64 *dn_tmp_104=&(v_high);size_t dn_tmp_105=({ size_t dn_tmp_106=((size_t)(2ULL));
size_t dn_tmp_107=v_i;
dn_USize_mul(dn_tmp_106,dn_tmp_107); });&dn_tmp_104->data[dn_index(dn_tmp_105,64)]; }));
double dn_tmp_108=(*({ dn_t_arr64_F64 *dn_tmp_109=&(v_high);size_t dn_tmp_110=({ size_t dn_tmp_111=({ size_t dn_tmp_112=((size_t)(2ULL));
size_t dn_tmp_113=v_i;
dn_USize_mul(dn_tmp_112,dn_tmp_113); });
size_t dn_tmp_114=((size_t)(1ULL));
dn_USize_add(dn_tmp_111,dn_tmp_114); });&dn_tmp_109->data[dn_index(dn_tmp_110,64)]; }));
dn_f_fir_downsample(dn_tmp_98,dn_tmp_103,dn_tmp_108); });double *dn_tmp_115=&((*({ dn_t_arr64_F64 *dn_tmp_96=&(v_high);size_t dn_tmp_97=v_i;&dn_tmp_96->data[dn_index(dn_tmp_97,64)]; })));*dn_tmp_115=dn_tmp_116; }
}
 }
{ size_t dn_tmp_120=({ size_t dn_tmp_117=v_count;
size_t dn_tmp_118=((size_t)(2ULL));
dn_USize_div(dn_tmp_117,dn_tmp_118); });size_t *dn_tmp_119=&(v_count);*dn_tmp_119=dn_tmp_120; }
}
double v_output=({ dn_r_Fir * dn_tmp_121=(&((*({ dn_t_buf_r3_Fir volatile *dn_tmp_122=&(v_downs);size_t dn_tmp_123=((size_t)(0ULL));&dn_tmp_122->data[dn_index(dn_tmp_123,dn_tmp_122->len)]; }))));
double dn_tmp_124=(*({ dn_t_arr64_F64 *dn_tmp_125=&(v_high);size_t dn_tmp_126=((size_t)(0ULL));&dn_tmp_125->data[dn_index(dn_tmp_126,64)]; }));
double dn_tmp_127=(*({ dn_t_arr64_F64 *dn_tmp_128=&(v_high);size_t dn_tmp_129=((size_t)(1ULL));&dn_tmp_128->data[dn_index(dn_tmp_129,64)]; }));
dn_f_fir_downsample(dn_tmp_121,dn_tmp_124,dn_tmp_127); });
size_t v_index=({ size_t dn_tmp_130=v_n;
size_t dn_tmp_131=({ size_t dn_tmp_132=v_depth;
dn_f_folded_compensation(dn_tmp_132); });
dn_USize_add(dn_tmp_130,dn_tmp_131); });
if(({ size_t dn_tmp_133=v_index;
size_t dn_tmp_134=((size_t)(384ULL));
(dn_tmp_133 < dn_tmp_134); })){
{ double dn_tmp_138=v_output;double *dn_tmp_137=&((*({ dn_t_arr384_F64 *dn_tmp_135=&(v_impulse);size_t dn_tmp_136=v_index;&dn_tmp_135->data[dn_index(dn_tmp_136,384)]; })));*dn_tmp_137=dn_tmp_138; }
}
else{
}
}
 }
size_t v_used=((size_t)(384ULL));
while((({ size_t dn_tmp_139=v_used;
size_t dn_tmp_140=((size_t)(1ULL));
(dn_tmp_139 > dn_tmp_140); }) && ({ double dn_tmp_141=({ double dn_tmp_142=(*({ dn_t_arr384_F64 *dn_tmp_143=&(v_impulse);size_t dn_tmp_144=({ size_t dn_tmp_145=v_used;
size_t dn_tmp_146=((size_t)(1ULL));
dn_USize_sub(dn_tmp_145,dn_tmp_146); });&dn_tmp_143->data[dn_index(dn_tmp_144,384)]; }));
fabs(dn_tmp_142); });
double dn_tmp_147=((double)(1.0e-17));
(dn_tmp_141 < dn_tmp_147); }))){
{ size_t dn_tmp_151=({ size_t dn_tmp_148=v_used;
size_t dn_tmp_149=((size_t)(1ULL));
dn_USize_sub(dn_tmp_148,dn_tmp_149); });size_t *dn_tmp_150=&(v_used);*dn_tmp_150=dn_tmp_151; }
}
{ size_t dn_tmp_155=v_used;size_t *dn_tmp_154=&((*({ dn_t_arr6_USize *dn_tmp_152=&((v_f).f_lengths);size_t dn_tmp_153=v_depth;&dn_tmp_152->data[dn_index(dn_tmp_153,6)]; })));*dn_tmp_154=dn_tmp_155; }
{ size_t dn_tmp_157=((size_t)(0ULL));size_t dn_tmp_156=v_used;for(size_t v_k=dn_tmp_157;v_k<dn_tmp_156;v_k++){
{ double dn_tmp_171=(*({ dn_t_arr384_F64 *dn_tmp_164=&(v_impulse);size_t dn_tmp_165=({ size_t dn_tmp_166=({ size_t dn_tmp_167=v_used;
size_t dn_tmp_168=((size_t)(1ULL));
dn_USize_sub(dn_tmp_167,dn_tmp_168); });
size_t dn_tmp_169=v_k;
dn_USize_sub(dn_tmp_166,dn_tmp_169); });&dn_tmp_164->data[dn_index(dn_tmp_165,384)]; }));double *dn_tmp_170=&((*({ dn_t_arr2304_F64 *dn_tmp_158=&((v_f).f_coefficients);size_t dn_tmp_159=({ size_t dn_tmp_160=({ size_t dn_tmp_161=v_depth;
size_t dn_tmp_162=((size_t)(384ULL));
dn_USize_mul(dn_tmp_161,dn_tmp_162); });
size_t dn_tmp_163=v_k;
dn_USize_add(dn_tmp_160,dn_tmp_163); });&dn_tmp_158->data[dn_index(dn_tmp_159,2304)]; })));*dn_tmp_170=dn_tmp_171; }
}
 }
dn_drop_buf_r3_Fir(&v_downs);
dn_drop_buf_r3_Fir(&v_ups);
dn_boundary_end(&dn_tmp_6,0);
}
 }
{ dn_r_Folded dn_tmp_172=v_f;return dn_tmp_172; }
}

dn_fail("missing native return");
}
static dn_r_Folded dn_cb_folded_prepare(void) {
 if(!dn_enter()){return (dn_r_Folded){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_Folded){0};}
 dn_r_Folded dn_result=dn_f_folded_prepare();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_folded_push(dn_r_Folded * v_f,double v_l,double v_r) {
{
{ double dn_tmp_3=v_l;double *dn_tmp_2=&((*({ dn_t_arr1536_F64 *dn_tmp_0=&((v_f)->f_history);size_t dn_tmp_1=(v_f)->f_index;&dn_tmp_0->data[dn_index(dn_tmp_1,1536)]; })));*dn_tmp_2=dn_tmp_3; }
{ double dn_tmp_9=v_l;double *dn_tmp_8=&((*({ dn_t_arr1536_F64 *dn_tmp_4=&((v_f)->f_history);size_t dn_tmp_5=({ size_t dn_tmp_6=(v_f)->f_index;
size_t dn_tmp_7=((size_t)(384ULL));
dn_USize_add(dn_tmp_6,dn_tmp_7); });&dn_tmp_4->data[dn_index(dn_tmp_5,1536)]; })));*dn_tmp_8=dn_tmp_9; }
{ double dn_tmp_15=v_r;double *dn_tmp_14=&((*({ dn_t_arr1536_F64 *dn_tmp_10=&((v_f)->f_history);size_t dn_tmp_11=({ size_t dn_tmp_12=(v_f)->f_index;
size_t dn_tmp_13=((size_t)(768ULL));
dn_USize_add(dn_tmp_12,dn_tmp_13); });&dn_tmp_10->data[dn_index(dn_tmp_11,1536)]; })));*dn_tmp_14=dn_tmp_15; }
{ double dn_tmp_21=v_r;double *dn_tmp_20=&((*({ dn_t_arr1536_F64 *dn_tmp_16=&((v_f)->f_history);size_t dn_tmp_17=({ size_t dn_tmp_18=(v_f)->f_index;
size_t dn_tmp_19=((size_t)(1152ULL));
dn_USize_add(dn_tmp_18,dn_tmp_19); });&dn_tmp_16->data[dn_index(dn_tmp_17,1536)]; })));*dn_tmp_20=dn_tmp_21; }
{ size_t dn_tmp_25=({ size_t dn_tmp_22=(v_f)->f_index;
size_t dn_tmp_23=((size_t)(1ULL));
dn_USize_add(dn_tmp_22,dn_tmp_23); });size_t *dn_tmp_24=&((v_f)->f_index);*dn_tmp_24=dn_tmp_25; }
if(({ size_t dn_tmp_26=(v_f)->f_index;
size_t dn_tmp_27=((size_t)(384ULL));
(dn_tmp_26 == dn_tmp_27); })){
{ size_t dn_tmp_29=((size_t)(0ULL));size_t *dn_tmp_28=&((v_f)->f_index);*dn_tmp_28=dn_tmp_29; }
}
else{
}
}

return 0;
}
static void dn_cb_folded_push(dn_r_Folded * v_f,double v_l,double v_r) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_folded_push(v_f,v_l,v_r);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static double dn_f_folded_eval(dn_r_Folded const * v_f,size_t v_ch,size_t v_depth) {
{
size_t v_n=(*({ dn_t_arr6_USize *dn_tmp_0=&((v_f)->f_lengths);size_t dn_tmp_1=v_depth;&dn_tmp_0->data[dn_index(dn_tmp_1,6)]; }));
size_t v_start=({ size_t dn_tmp_2=({ size_t dn_tmp_3=({ size_t dn_tmp_4=({ size_t dn_tmp_5=v_ch;
size_t dn_tmp_6=((size_t)(768ULL));
dn_USize_mul(dn_tmp_5,dn_tmp_6); });
size_t dn_tmp_7=(v_f)->f_index;
dn_USize_add(dn_tmp_4,dn_tmp_7); });
size_t dn_tmp_8=((size_t)(384ULL));
dn_USize_add(dn_tmp_3,dn_tmp_8); });
size_t dn_tmp_9=v_n;
dn_USize_sub(dn_tmp_2,dn_tmp_9); });
double v_result=((double)(0.0));
{ size_t dn_tmp_11=((size_t)(0ULL));size_t dn_tmp_10=v_n;for(size_t v_k=dn_tmp_11;v_k<dn_tmp_10;v_k++){
{ double dn_tmp_27=({ double dn_tmp_12=v_result;
double dn_tmp_13=({ double dn_tmp_14=(*({ dn_t_arr1536_F64 *dn_tmp_15=&((v_f)->f_history);size_t dn_tmp_16=({ size_t dn_tmp_17=v_start;
size_t dn_tmp_18=v_k;
dn_USize_add(dn_tmp_17,dn_tmp_18); });&dn_tmp_15->data[dn_index(dn_tmp_16,1536)]; }));
double dn_tmp_19=(*({ dn_t_arr2304_F64 *dn_tmp_20=&((v_f)->f_coefficients);size_t dn_tmp_21=({ size_t dn_tmp_22=({ size_t dn_tmp_23=v_depth;
size_t dn_tmp_24=((size_t)(384ULL));
dn_USize_mul(dn_tmp_23,dn_tmp_24); });
size_t dn_tmp_25=v_k;
dn_USize_add(dn_tmp_22,dn_tmp_25); });&dn_tmp_20->data[dn_index(dn_tmp_21,2304)]; }));
(dn_tmp_14 * dn_tmp_19); });
(dn_tmp_12 + dn_tmp_13); });double *dn_tmp_26=&(v_result);*dn_tmp_26=dn_tmp_27; }
}
 }
{ double dn_tmp_28=v_result;return dn_tmp_28; }
}

dn_fail("missing native return");
}
static double dn_cb_folded_eval(dn_r_Folded const * v_f,size_t v_ch,size_t v_depth) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_folded_eval(v_f,v_ch,v_depth);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static size_t dn_f_if_model(size_t v_dac,size_t v_pulse) {
{
if(({ size_t dn_tmp_0=v_dac;
size_t dn_tmp_1=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); })){
{ size_t dn_tmp_2=v_pulse;return dn_tmp_2; }
}
else{
}
if(({ size_t dn_tmp_3=v_dac;
size_t dn_tmp_4=((size_t)(3ULL));
(dn_tmp_3 == dn_tmp_4); })){
{ size_t dn_tmp_5=((size_t)(2ULL));return dn_tmp_5; }
}
else{
}
{ size_t dn_tmp_6=((size_t)(0ULL));return dn_tmp_6; }
}

dn_fail("missing native return");
}
static size_t dn_cb_if_model(size_t v_dac,size_t v_pulse) {
 if(!dn_enter()){return (size_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (size_t){0};}
 size_t dn_result=dn_f_if_model(v_dac,v_pulse);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_channel_sample(dn_r_Engine * v_e,double v_x,size_t v_ch) {
{
bool v_fastTubes=(({ size_t dn_tmp_0=(v_e)->f_tubeCpuMode;
size_t dn_tmp_1=((size_t)(1ULL));
(dn_tmp_0 == dn_tmp_1); }) && ({ size_t dn_tmp_2=(v_e)->f_depth;
size_t dn_tmp_3=((size_t)(4ULL));
(dn_tmp_2 >= dn_tmp_3); }));
dn_t_arr64_F64 v_high=({ ((dn_t_arr64_F64){0}); });
dn_r_Stereo v_first=({ dn_r_Fir * dn_tmp_4=(&((*({ dn_t_buf_r3_Fir volatile *dn_tmp_5=&((v_e)->f_up);size_t dn_tmp_6=v_ch;&dn_tmp_5->data[dn_index(dn_tmp_6,dn_tmp_5->len)]; }))));
double dn_tmp_7=v_x;
dn_f_fir_upsample(dn_tmp_4,dn_tmp_7); });
{ double dn_tmp_11=(v_first).f_left;double *dn_tmp_10=&((*({ dn_t_arr64_F64 *dn_tmp_8=&(v_high);size_t dn_tmp_9=((size_t)(0ULL));&dn_tmp_8->data[dn_index(dn_tmp_9,64)]; })));*dn_tmp_10=dn_tmp_11; }
{ double dn_tmp_15=(v_first).f_right;double *dn_tmp_14=&((*({ dn_t_arr64_F64 *dn_tmp_12=&(v_high);size_t dn_tmp_13=((size_t)(1ULL));&dn_tmp_12->data[dn_index(dn_tmp_13,64)]; })));*dn_tmp_14=dn_tmp_15; }
size_t v_count=((size_t)(2ULL));
{ size_t dn_tmp_17=((size_t)(0ULL));size_t dn_tmp_16=(v_e)->f_depth;for(size_t v_stage=dn_tmp_17;v_stage<dn_tmp_16;v_stage++){
dn_t_arr64_F64 v_next=({ ((dn_t_arr64_F64){0}); });
{ size_t dn_tmp_19=((size_t)(0ULL));size_t dn_tmp_18=v_count;for(size_t v_i=dn_tmp_19;v_i<dn_tmp_18;v_i++){
dn_r_Stereo v_result=({ dn_r_Fir * dn_tmp_20=(&((*({ dn_t_buf_r3_Fir volatile *dn_tmp_21=&((v_e)->f_up);size_t dn_tmp_22=({ size_t dn_tmp_23=({ size_t dn_tmp_24=((size_t)(2ULL));
size_t dn_tmp_25=({ size_t dn_tmp_26=v_stage;
size_t dn_tmp_27=((size_t)(2ULL));
dn_USize_mul(dn_tmp_26,dn_tmp_27); });
dn_USize_add(dn_tmp_24,dn_tmp_25); });
size_t dn_tmp_28=v_ch;
dn_USize_add(dn_tmp_23,dn_tmp_28); });&dn_tmp_21->data[dn_index(dn_tmp_22,dn_tmp_21->len)]; }))));
double dn_tmp_29=(*({ dn_t_arr64_F64 *dn_tmp_30=&(v_high);size_t dn_tmp_31=v_i;&dn_tmp_30->data[dn_index(dn_tmp_31,64)]; }));
dn_f_fir_upsample(dn_tmp_20,dn_tmp_29); });
{ double dn_tmp_37=(v_result).f_left;double *dn_tmp_36=&((*({ dn_t_arr64_F64 *dn_tmp_32=&(v_next);size_t dn_tmp_33=({ size_t dn_tmp_34=v_i;
size_t dn_tmp_35=((size_t)(2ULL));
dn_USize_mul(dn_tmp_34,dn_tmp_35); });&dn_tmp_32->data[dn_index(dn_tmp_33,64)]; })));*dn_tmp_36=dn_tmp_37; }
{ double dn_tmp_45=(v_result).f_right;double *dn_tmp_44=&((*({ dn_t_arr64_F64 *dn_tmp_38=&(v_next);size_t dn_tmp_39=({ size_t dn_tmp_40=({ size_t dn_tmp_41=v_i;
size_t dn_tmp_42=((size_t)(2ULL));
dn_USize_mul(dn_tmp_41,dn_tmp_42); });
size_t dn_tmp_43=((size_t)(1ULL));
dn_USize_add(dn_tmp_40,dn_tmp_43); });&dn_tmp_38->data[dn_index(dn_tmp_39,64)]; })));*dn_tmp_44=dn_tmp_45; }
}
 }
{ size_t dn_tmp_49=((size_t)(0ULL));size_t dn_tmp_48=({ size_t dn_tmp_46=v_count;
size_t dn_tmp_47=((size_t)(2ULL));
dn_USize_mul(dn_tmp_46,dn_tmp_47); });for(size_t v_i=dn_tmp_49;v_i<dn_tmp_48;v_i++){
{ double dn_tmp_55=(*({ dn_t_arr64_F64 *dn_tmp_52=&(v_next);size_t dn_tmp_53=v_i;&dn_tmp_52->data[dn_index(dn_tmp_53,64)]; }));double *dn_tmp_54=&((*({ dn_t_arr64_F64 *dn_tmp_50=&(v_high);size_t dn_tmp_51=v_i;&dn_tmp_50->data[dn_index(dn_tmp_51,64)]; })));*dn_tmp_54=dn_tmp_55; }
}
 }
{ size_t dn_tmp_59=({ size_t dn_tmp_56=v_count;
size_t dn_tmp_57=((size_t)(2ULL));
dn_USize_mul(dn_tmp_56,dn_tmp_57); });size_t *dn_tmp_58=&(v_count);*dn_tmp_58=dn_tmp_59; }
if(((v_fastTubes && ({ size_t dn_tmp_60=v_count;
size_t dn_tmp_61=((size_t)(16ULL));
(dn_tmp_60 == dn_tmp_61); })) && (v_e)->f_tubeActive)){
{ size_t dn_tmp_63=((size_t)(0ULL));size_t dn_tmp_62=v_count;for(size_t v_i=dn_tmp_63;v_i<dn_tmp_62;v_i++){
{ double dn_tmp_72=({ dn_r_Engine * dn_tmp_66=v_e;
double dn_tmp_67=(*({ dn_t_arr64_F64 *dn_tmp_68=&(v_high);size_t dn_tmp_69=v_i;&dn_tmp_68->data[dn_index(dn_tmp_69,64)]; }));
size_t dn_tmp_70=v_ch;
dn_f_apply_tube(dn_tmp_66,dn_tmp_67,dn_tmp_70); });double *dn_tmp_71=&((*({ dn_t_arr64_F64 *dn_tmp_64=&(v_high);size_t dn_tmp_65=v_i;&dn_tmp_64->data[dn_index(dn_tmp_65,64)]; })));*dn_tmp_71=dn_tmp_72; }
}
 }
}
else{
}
}
 }
{ size_t dn_tmp_74=((size_t)(0ULL));size_t dn_tmp_73=v_count;for(size_t v_i=dn_tmp_74;v_i<dn_tmp_73;v_i++){
double v_tubeInput=(*({ dn_t_arr64_F64 *dn_tmp_75=&(v_high);size_t dn_tmp_76=v_i;&dn_tmp_75->data[dn_index(dn_tmp_76,64)]; }));
if(((v_e)->f_tubeActive && (!(v_fastTubes)))){
{ double dn_tmp_81=({ dn_r_Engine * dn_tmp_77=v_e;
double dn_tmp_78=v_tubeInput;
size_t dn_tmp_79=v_ch;
dn_f_apply_tube(dn_tmp_77,dn_tmp_78,dn_tmp_79); });double *dn_tmp_80=&(v_tubeInput);*dn_tmp_80=dn_tmp_81; }
}
else{
}
double v_colored=({ double dn_tmp_82=({ double dn_tmp_83=v_tubeInput;
double dn_tmp_84=(v_e)->f_characterCurrent;
dn_f_color_sample(dn_tmp_83,dn_tmp_84); });
double dn_tmp_85=(v_e)->f_pulseColorCurrent;
size_t dn_tmp_86=({ size_t dn_tmp_87=(v_e)->f_dacMode;
size_t dn_tmp_88=(v_e)->f_pulseMode;
dn_f_if_model(dn_tmp_87,dn_tmp_88); });
dn_f_pulse_color(dn_tmp_82,dn_tmp_85,dn_tmp_86); });
if(({ size_t dn_tmp_89=(v_e)->f_dacMode;
size_t dn_tmp_90=((size_t)(0ULL));
(dn_tmp_89 == dn_tmp_90); })){
{ double dn_tmp_98=({ dn_r_Pulse * dn_tmp_93=(&((*({ dn_t_arr2_r5_Pulse *dn_tmp_94=&((v_e)->f_pulses);size_t dn_tmp_95=v_ch;&dn_tmp_94->data[dn_index(dn_tmp_95,2)]; }))));
double dn_tmp_96=v_colored;
dn_f_pulse_process(dn_tmp_93,dn_tmp_96); });double *dn_tmp_97=&((*({ dn_t_arr64_F64 *dn_tmp_91=&(v_high);size_t dn_tmp_92=v_i;&dn_tmp_91->data[dn_index(dn_tmp_92,64)]; })));*dn_tmp_97=dn_tmp_98; }
}
else{
{ double dn_tmp_107=({ dn_r_Dac * dn_tmp_101=(&((*({ dn_t_arr2_r3_Dac *dn_tmp_102=&((v_e)->f_dacs);size_t dn_tmp_103=v_ch;&dn_tmp_102->data[dn_index(dn_tmp_103,2)]; }))));
double dn_tmp_104=v_colored;
double dn_tmp_105=(v_e)->f_mismatch;
dn_f_dac_process(dn_tmp_101,dn_tmp_104,dn_tmp_105); });double *dn_tmp_106=&((*({ dn_t_arr64_F64 *dn_tmp_99=&(v_high);size_t dn_tmp_100=v_i;&dn_tmp_99->data[dn_index(dn_tmp_100,64)]; })));*dn_tmp_106=dn_tmp_107; }
}
}
 }
size_t v_stage=(v_e)->f_depth;
while(({ size_t dn_tmp_108=v_stage;
size_t dn_tmp_109=((size_t)(0ULL));
(dn_tmp_108 > dn_tmp_109); })){
{ size_t dn_tmp_113=({ size_t dn_tmp_110=v_stage;
size_t dn_tmp_111=((size_t)(1ULL));
dn_USize_sub(dn_tmp_110,dn_tmp_111); });size_t *dn_tmp_112=&(v_stage);*dn_tmp_112=dn_tmp_113; }
{ size_t dn_tmp_117=((size_t)(0ULL));size_t dn_tmp_116=({ size_t dn_tmp_114=v_count;
size_t dn_tmp_115=((size_t)(2ULL));
dn_USize_div(dn_tmp_114,dn_tmp_115); });for(size_t v_i=dn_tmp_117;v_i<dn_tmp_116;v_i++){
{ double dn_tmp_142=({ dn_r_Fir * dn_tmp_120=(&((*({ dn_t_buf_r3_Fir volatile *dn_tmp_121=&((v_e)->f_down);size_t dn_tmp_122=({ size_t dn_tmp_123=({ size_t dn_tmp_124=((size_t)(2ULL));
size_t dn_tmp_125=({ size_t dn_tmp_126=v_stage;
size_t dn_tmp_127=((size_t)(2ULL));
dn_USize_mul(dn_tmp_126,dn_tmp_127); });
dn_USize_add(dn_tmp_124,dn_tmp_125); });
size_t dn_tmp_128=v_ch;
dn_USize_add(dn_tmp_123,dn_tmp_128); });&dn_tmp_121->data[dn_index(dn_tmp_122,dn_tmp_121->len)]; }))));
double dn_tmp_129=(*({ dn_t_arr64_F64 *dn_tmp_130=&(v_high);size_t dn_tmp_131=({ size_t dn_tmp_132=((size_t)(2ULL));
size_t dn_tmp_133=v_i;
dn_USize_mul(dn_tmp_132,dn_tmp_133); });&dn_tmp_130->data[dn_index(dn_tmp_131,64)]; }));
double dn_tmp_134=(*({ dn_t_arr64_F64 *dn_tmp_135=&(v_high);size_t dn_tmp_136=({ size_t dn_tmp_137=({ size_t dn_tmp_138=((size_t)(2ULL));
size_t dn_tmp_139=v_i;
dn_USize_mul(dn_tmp_138,dn_tmp_139); });
size_t dn_tmp_140=((size_t)(1ULL));
dn_USize_add(dn_tmp_137,dn_tmp_140); });&dn_tmp_135->data[dn_index(dn_tmp_136,64)]; }));
dn_f_fir_downsample(dn_tmp_120,dn_tmp_129,dn_tmp_134); });double *dn_tmp_141=&((*({ dn_t_arr64_F64 *dn_tmp_118=&(v_high);size_t dn_tmp_119=v_i;&dn_tmp_118->data[dn_index(dn_tmp_119,64)]; })));*dn_tmp_141=dn_tmp_142; }
}
 }
{ size_t dn_tmp_146=({ size_t dn_tmp_143=v_count;
size_t dn_tmp_144=((size_t)(2ULL));
dn_USize_div(dn_tmp_143,dn_tmp_144); });size_t *dn_tmp_145=&(v_count);*dn_tmp_145=dn_tmp_146; }
}
{ double dn_tmp_156=({ dn_r_Fir * dn_tmp_147=(&((*({ dn_t_buf_r3_Fir volatile *dn_tmp_148=&((v_e)->f_down);size_t dn_tmp_149=v_ch;&dn_tmp_148->data[dn_index(dn_tmp_149,dn_tmp_148->len)]; }))));
double dn_tmp_150=(*({ dn_t_arr64_F64 *dn_tmp_151=&(v_high);size_t dn_tmp_152=((size_t)(0ULL));&dn_tmp_151->data[dn_index(dn_tmp_152,64)]; }));
double dn_tmp_153=(*({ dn_t_arr64_F64 *dn_tmp_154=&(v_high);size_t dn_tmp_155=((size_t)(1ULL));&dn_tmp_154->data[dn_index(dn_tmp_155,64)]; }));
dn_f_fir_downsample(dn_tmp_147,dn_tmp_150,dn_tmp_153); });return dn_tmp_156; }
}

dn_fail("missing native return");
}
static double dn_cb_channel_sample(dn_r_Engine * v_e,double v_x,size_t v_ch) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_channel_sample(v_e,v_x,v_ch);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_Biquad dn_f_biquad_create(double v_sr) {
{
if(({ double dn_tmp_0=v_sr;
double dn_tmp_1=((double)(88000.0));
(dn_tmp_0 < dn_tmp_1); })){
{ dn_r_Biquad dn_tmp_9=({ double dn_tmp_2=((double)(1.0));
double dn_tmp_3=((double)(0.0));
double dn_tmp_4=((double)(0.0));
double dn_tmp_5=((double)(0.0));
double dn_tmp_6=((double)(0.0));
double dn_tmp_7=((double)(0.0));
double dn_tmp_8=((double)(0.0));
((dn_r_Biquad){dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8}); });return dn_tmp_9; }
}
else{
}
double v_w=({ double dn_tmp_10=({ double dn_tmp_11=({ double dn_tmp_12=((double)(2.0));
double dn_tmp_13=({ dn_f_pi(); });
(dn_tmp_12 * dn_tmp_13); });
double dn_tmp_14=((double)(40000.0));
(dn_tmp_11 * dn_tmp_14); });
double dn_tmp_15=v_sr;
(dn_tmp_10 / dn_tmp_15); });
double v_cs=({ double dn_tmp_16=v_w;
cos(dn_tmp_16); });
double v_alpha=({ double dn_tmp_17=({ double dn_tmp_18=v_w;
sin(dn_tmp_18); });
double dn_tmp_19=({ double dn_tmp_20=((double)(2.0));
sqrt(dn_tmp_20); });
(dn_tmp_17 / dn_tmp_19); });
double v_a0=({ double dn_tmp_21=((double)(1.0));
double dn_tmp_22=v_alpha;
(dn_tmp_21 + dn_tmp_22); });
{ dn_r_Biquad dn_tmp_54=({ double dn_tmp_23=({ double dn_tmp_24=({ double dn_tmp_25=({ double dn_tmp_26=((double)(1.0));
double dn_tmp_27=v_cs;
(dn_tmp_26 - dn_tmp_27); });
double dn_tmp_28=((double)(0.5));
(dn_tmp_25 * dn_tmp_28); });
double dn_tmp_29=v_a0;
(dn_tmp_24 / dn_tmp_29); });
double dn_tmp_30=({ double dn_tmp_31=({ double dn_tmp_32=((double)(1.0));
double dn_tmp_33=v_cs;
(dn_tmp_32 - dn_tmp_33); });
double dn_tmp_34=v_a0;
(dn_tmp_31 / dn_tmp_34); });
double dn_tmp_35=({ double dn_tmp_36=({ double dn_tmp_37=({ double dn_tmp_38=((double)(1.0));
double dn_tmp_39=v_cs;
(dn_tmp_38 - dn_tmp_39); });
double dn_tmp_40=((double)(0.5));
(dn_tmp_37 * dn_tmp_40); });
double dn_tmp_41=v_a0;
(dn_tmp_36 / dn_tmp_41); });
double dn_tmp_42=({ double dn_tmp_43=({ double dn_tmp_44=(-(((double)(2.0))));
double dn_tmp_45=v_cs;
(dn_tmp_44 * dn_tmp_45); });
double dn_tmp_46=v_a0;
(dn_tmp_43 / dn_tmp_46); });
double dn_tmp_47=({ double dn_tmp_48=({ double dn_tmp_49=((double)(1.0));
double dn_tmp_50=v_alpha;
(dn_tmp_49 - dn_tmp_50); });
double dn_tmp_51=v_a0;
(dn_tmp_48 / dn_tmp_51); });
double dn_tmp_52=((double)(0.0));
double dn_tmp_53=((double)(0.0));
((dn_r_Biquad){dn_tmp_23,dn_tmp_30,dn_tmp_35,dn_tmp_42,dn_tmp_47,dn_tmp_52,dn_tmp_53}); });return dn_tmp_54; }
}

dn_fail("missing native return");
}
static dn_r_Biquad dn_cb_biquad_create(double v_sr) {
 if(!dn_enter()){return (dn_r_Biquad){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_Biquad){0};}
 dn_r_Biquad dn_result=dn_f_biquad_create(v_sr);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_biquad_process(dn_r_Biquad * v_b,double v_x) {
{
double v_y=({ double dn_tmp_0=({ double dn_tmp_1=(v_b)->f_b0;
double dn_tmp_2=v_x;
(dn_tmp_1 * dn_tmp_2); });
double dn_tmp_3=(v_b)->f_z1;
(dn_tmp_0 + dn_tmp_3); });
{ double dn_tmp_13=({ double dn_tmp_4=({ double dn_tmp_5=({ double dn_tmp_6=(v_b)->f_b1;
double dn_tmp_7=v_x;
(dn_tmp_6 * dn_tmp_7); });
double dn_tmp_8=({ double dn_tmp_9=(v_b)->f_a1;
double dn_tmp_10=v_y;
(dn_tmp_9 * dn_tmp_10); });
(dn_tmp_5 - dn_tmp_8); });
double dn_tmp_11=(v_b)->f_z2;
(dn_tmp_4 + dn_tmp_11); });double *dn_tmp_12=&((v_b)->f_z1);*dn_tmp_12=dn_tmp_13; }
{ double dn_tmp_21=({ double dn_tmp_14=({ double dn_tmp_15=(v_b)->f_b2;
double dn_tmp_16=v_x;
(dn_tmp_15 * dn_tmp_16); });
double dn_tmp_17=({ double dn_tmp_18=(v_b)->f_a2;
double dn_tmp_19=v_y;
(dn_tmp_18 * dn_tmp_19); });
(dn_tmp_14 - dn_tmp_17); });double *dn_tmp_20=&((v_b)->f_z2);*dn_tmp_20=dn_tmp_21; }
{ double dn_tmp_22=v_y;return dn_tmp_22; }
}

dn_fail("missing native return");
}
static double dn_cb_biquad_process(dn_r_Biquad * v_b,double v_x) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_biquad_process(v_b,v_x);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_Allpass dn_f_allpass_create(void) {
{
{ dn_r_Allpass dn_tmp_2=({ double dn_tmp_0=((double)(0.0));
double dn_tmp_1=((double)(0.0));
((dn_r_Allpass){dn_tmp_0,dn_tmp_1}); });return dn_tmp_2; }
}

dn_fail("missing native return");
}
static dn_r_Allpass dn_cb_allpass_create(void) {
 if(!dn_enter()){return (dn_r_Allpass){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_Allpass){0};}
 dn_r_Allpass dn_result=dn_f_allpass_create();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_ap_coeff(double v_hz,double v_rate) {
{
double v_angle=({ double dn_tmp_0=({ double dn_tmp_1=({ dn_f_pi(); });
double dn_tmp_2=({ double dn_tmp_3=v_hz;
double dn_tmp_4=((double)(0.0));
double dn_tmp_5=({ double dn_tmp_6=v_rate;
double dn_tmp_7=((double)(0.35));
(dn_tmp_6 * dn_tmp_7); });
dn_f_min_f(dn_tmp_3,dn_tmp_4,dn_tmp_5); });
(dn_tmp_1 * dn_tmp_2); });
double dn_tmp_8=v_rate;
(dn_tmp_0 / dn_tmp_8); });
double v_tangent=({ double dn_tmp_9=({ double dn_tmp_10=v_angle;
sin(dn_tmp_10); });
double dn_tmp_11=({ double dn_tmp_12=v_angle;
cos(dn_tmp_12); });
(dn_tmp_9 / dn_tmp_11); });
{ double dn_tmp_19=({ double dn_tmp_13=({ double dn_tmp_14=v_tangent;
double dn_tmp_15=((double)(1.0));
(dn_tmp_14 - dn_tmp_15); });
double dn_tmp_16=({ double dn_tmp_17=v_tangent;
double dn_tmp_18=((double)(1.0));
(dn_tmp_17 + dn_tmp_18); });
(dn_tmp_13 / dn_tmp_16); });return dn_tmp_19; }
}

dn_fail("missing native return");
}
static double dn_cb_ap_coeff(double v_hz,double v_rate) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_ap_coeff(v_hz,v_rate);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_Creative dn_f_creative_create(double v_sr) {
{
double v_rate=({ double dn_tmp_0=v_sr;
double dn_tmp_1=((double)(8000.0));
double dn_tmp_2=((double)(384000.0));
dn_f_min_f(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
{ dn_r_Creative dn_tmp_56=({ dn_t_arr3_F64 dn_tmp_3=({ ((dn_t_arr3_F64){0}); });
dn_t_arr2_F64 dn_tmp_4=({ ((dn_t_arr2_F64){0}); });
dn_t_arr2_F64 dn_tmp_5=({ ((dn_t_arr2_F64){0}); });
dn_t_arr2_F64 dn_tmp_6=({ ((dn_t_arr2_F64){0}); });
dn_t_arr4_r7_Allpass dn_tmp_7=({ dn_r_Allpass dn_tmp_8=({ dn_f_allpass_create(); });
dn_r_Allpass dn_tmp_9=({ dn_f_allpass_create(); });
dn_r_Allpass dn_tmp_10=({ dn_f_allpass_create(); });
dn_r_Allpass dn_tmp_11=({ dn_f_allpass_create(); });
((dn_t_arr4_r7_Allpass){{dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_11}}); });
double dn_tmp_12=({ double dn_tmp_13=((double)(1.0));
double dn_tmp_14=({ double dn_tmp_15=({ double dn_tmp_16=(-(((double)(1.0))));
double dn_tmp_17=({ double dn_tmp_18=((double)(0.020));
double dn_tmp_19=v_rate;
(dn_tmp_18 * dn_tmp_19); });
(dn_tmp_16 / dn_tmp_17); });
exp(dn_tmp_15); });
(dn_tmp_13 - dn_tmp_14); });
double dn_tmp_20=({ double dn_tmp_21=((double)(1.0));
double dn_tmp_22=({ double dn_tmp_23=({ double dn_tmp_24=({ double dn_tmp_25=({ double dn_tmp_26=(-(((double)(2.0))));
double dn_tmp_27=({ dn_f_pi(); });
(dn_tmp_26 * dn_tmp_27); });
double dn_tmp_28=((double)(1750.0));
(dn_tmp_25 * dn_tmp_28); });
double dn_tmp_29=v_rate;
(dn_tmp_24 / dn_tmp_29); });
exp(dn_tmp_23); });
(dn_tmp_21 - dn_tmp_22); });
double dn_tmp_30=({ double dn_tmp_31=((double)(1.0));
double dn_tmp_32=({ double dn_tmp_33=({ double dn_tmp_34=({ double dn_tmp_35=({ double dn_tmp_36=(-(((double)(2.0))));
double dn_tmp_37=({ dn_f_pi(); });
(dn_tmp_36 * dn_tmp_37); });
double dn_tmp_38=((double)(3200.0));
(dn_tmp_35 * dn_tmp_38); });
double dn_tmp_39=v_rate;
(dn_tmp_34 / dn_tmp_39); });
exp(dn_tmp_33); });
(dn_tmp_31 - dn_tmp_32); });
double dn_tmp_40=({ double dn_tmp_41=((double)(1.0));
double dn_tmp_42=({ double dn_tmp_43=({ double dn_tmp_44=({ double dn_tmp_45=({ double dn_tmp_46=(-(((double)(2.0))));
double dn_tmp_47=({ dn_f_pi(); });
(dn_tmp_46 * dn_tmp_47); });
double dn_tmp_48=((double)(7200.0));
(dn_tmp_45 * dn_tmp_48); });
double dn_tmp_49=v_rate;
(dn_tmp_44 / dn_tmp_49); });
exp(dn_tmp_43); });
(dn_tmp_41 - dn_tmp_42); });
double dn_tmp_50=({ double dn_tmp_51=((double)(900.0));
double dn_tmp_52=v_rate;
dn_f_ap_coeff(dn_tmp_51,dn_tmp_52); });
double dn_tmp_53=({ double dn_tmp_54=((double)(3200.0));
double dn_tmp_55=v_rate;
dn_f_ap_coeff(dn_tmp_54,dn_tmp_55); });
((dn_r_Creative){dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_12,dn_tmp_20,dn_tmp_30,dn_tmp_40,dn_tmp_50,dn_tmp_53}); });return dn_tmp_56; }
}

dn_fail("missing native return");
}
static dn_r_Creative dn_cb_creative_create(double v_sr) {
 if(!dn_enter()){return (dn_r_Creative){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_Creative){0};}
 dn_r_Creative dn_result=dn_f_creative_create(v_sr);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_allpass_process(dn_r_Allpass * v_s,double v_x,double v_a) {
{
double v_y=({ double dn_tmp_0=({ double dn_tmp_1=({ double dn_tmp_2=v_a;
double dn_tmp_3=v_x;
(dn_tmp_2 * dn_tmp_3); });
double dn_tmp_4=(v_s)->f_input;
(dn_tmp_1 + dn_tmp_4); });
double dn_tmp_5=({ double dn_tmp_6=v_a;
double dn_tmp_7=(v_s)->f_output;
(dn_tmp_6 * dn_tmp_7); });
(dn_tmp_0 - dn_tmp_5); });
{ double dn_tmp_9=v_x;double *dn_tmp_8=&((v_s)->f_input);*dn_tmp_8=dn_tmp_9; }
{ double dn_tmp_11=v_y;double *dn_tmp_10=&((v_s)->f_output);*dn_tmp_10=dn_tmp_11; }
{ double dn_tmp_12=v_y;return dn_tmp_12; }
}

dn_fail("missing native return");
}
static double dn_cb_allpass_process(dn_r_Allpass * v_s,double v_x,double v_a) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_allpass_process(v_s,v_x,v_a);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_creative_channel(dn_r_Creative * v_c,double v_x,size_t v_ch,uint32_t v_phaseMode,uint32_t v_filterMode) {
{
{ double dn_tmp_13=({ double dn_tmp_2=(*({ dn_t_arr2_F64 *dn_tmp_3=&((v_c)->f_focusState);size_t dn_tmp_4=v_ch;&dn_tmp_3->data[dn_index(dn_tmp_4,2)]; }));
double dn_tmp_5=({ double dn_tmp_6=(v_c)->f_focusPole;
double dn_tmp_7=({ double dn_tmp_8=v_x;
double dn_tmp_9=(*({ dn_t_arr2_F64 *dn_tmp_10=&((v_c)->f_focusState);size_t dn_tmp_11=v_ch;&dn_tmp_10->data[dn_index(dn_tmp_11,2)]; }));
(dn_tmp_8 - dn_tmp_9); });
(dn_tmp_6 * dn_tmp_7); });
(dn_tmp_2 + dn_tmp_5); });double *dn_tmp_12=&((*({ dn_t_arr2_F64 *dn_tmp_0=&((v_c)->f_focusState);size_t dn_tmp_1=v_ch;&dn_tmp_0->data[dn_index(dn_tmp_1,2)]; })));*dn_tmp_12=dn_tmp_13; }
{ double dn_tmp_27=({ double dn_tmp_16=(*({ dn_t_arr2_F64 *dn_tmp_17=&((v_c)->f_warmState);size_t dn_tmp_18=v_ch;&dn_tmp_17->data[dn_index(dn_tmp_18,2)]; }));
double dn_tmp_19=({ double dn_tmp_20=(v_c)->f_warmPole;
double dn_tmp_21=({ double dn_tmp_22=v_x;
double dn_tmp_23=(*({ dn_t_arr2_F64 *dn_tmp_24=&((v_c)->f_warmState);size_t dn_tmp_25=v_ch;&dn_tmp_24->data[dn_index(dn_tmp_25,2)]; }));
(dn_tmp_22 - dn_tmp_23); });
(dn_tmp_20 * dn_tmp_21); });
(dn_tmp_16 + dn_tmp_19); });double *dn_tmp_26=&((*({ dn_t_arr2_F64 *dn_tmp_14=&((v_c)->f_warmState);size_t dn_tmp_15=v_ch;&dn_tmp_14->data[dn_index(dn_tmp_15,2)]; })));*dn_tmp_26=dn_tmp_27; }
{ double dn_tmp_41=({ double dn_tmp_30=(*({ dn_t_arr2_F64 *dn_tmp_31=&((v_c)->f_hfState);size_t dn_tmp_32=v_ch;&dn_tmp_31->data[dn_index(dn_tmp_32,2)]; }));
double dn_tmp_33=({ double dn_tmp_34=(v_c)->f_hfPole;
double dn_tmp_35=({ double dn_tmp_36=v_x;
double dn_tmp_37=(*({ dn_t_arr2_F64 *dn_tmp_38=&((v_c)->f_hfState);size_t dn_tmp_39=v_ch;&dn_tmp_38->data[dn_index(dn_tmp_39,2)]; }));
(dn_tmp_36 - dn_tmp_37); });
(dn_tmp_34 * dn_tmp_35); });
(dn_tmp_30 + dn_tmp_33); });double *dn_tmp_40=&((*({ dn_t_arr2_F64 *dn_tmp_28=&((v_c)->f_hfState);size_t dn_tmp_29=v_ch;&dn_tmp_28->data[dn_index(dn_tmp_29,2)]; })));*dn_tmp_40=dn_tmp_41; }
double v_y=({ double dn_tmp_42=v_x;
double dn_tmp_43=({ double dn_tmp_44=({ double dn_tmp_45=((double)(0.95));
double dn_tmp_46=(*({ dn_t_arr3_F64 *dn_tmp_47=&((v_c)->f_amounts);size_t dn_tmp_48=((size_t)(0ULL));&dn_tmp_47->data[dn_index(dn_tmp_48,3)]; }));
(dn_tmp_45 * dn_tmp_46); });
double dn_tmp_49=({ double dn_tmp_50=v_x;
double dn_tmp_51=(*({ dn_t_arr2_F64 *dn_tmp_52=&((v_c)->f_focusState);size_t dn_tmp_53=v_ch;&dn_tmp_52->data[dn_index(dn_tmp_53,2)]; }));
(dn_tmp_50 - dn_tmp_51); });
(dn_tmp_44 * dn_tmp_49); });
(dn_tmp_42 + dn_tmp_43); });
double v_ap1=({ dn_r_Allpass * dn_tmp_54=(&((*({ dn_t_arr4_r7_Allpass *dn_tmp_55=&((v_c)->f_allpass);size_t dn_tmp_56=v_ch;&dn_tmp_55->data[dn_index(dn_tmp_56,4)]; }))));
double dn_tmp_57=v_y;
double dn_tmp_58=(v_c)->f_ap0;
dn_f_allpass_process(dn_tmp_54,dn_tmp_57,dn_tmp_58); });
double v_ap2=({ dn_r_Allpass * dn_tmp_59=(&((*({ dn_t_arr4_r7_Allpass *dn_tmp_60=&((v_c)->f_allpass);size_t dn_tmp_61=({ size_t dn_tmp_62=((size_t)(2ULL));
size_t dn_tmp_63=v_ch;
dn_USize_add(dn_tmp_62,dn_tmp_63); });&dn_tmp_60->data[dn_index(dn_tmp_61,4)]; }))));
double dn_tmp_64=v_ap1;
double dn_tmp_65=(v_c)->f_ap1;
dn_f_allpass_process(dn_tmp_59,dn_tmp_64,dn_tmp_65); });
if(({ uint32_t dn_tmp_66=v_phaseMode;
uint32_t dn_tmp_67=({ uint32_t dn_tmp_68=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_68>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_68; });
(dn_tmp_66 == dn_tmp_67); })){
{ double dn_tmp_80=({ double dn_tmp_69=v_y;
double dn_tmp_70=({ double dn_tmp_71=({ double dn_tmp_72=((double)(0.43));
double dn_tmp_73=(*({ dn_t_arr3_F64 *dn_tmp_74=&((v_c)->f_amounts);size_t dn_tmp_75=((size_t)(1ULL));&dn_tmp_74->data[dn_index(dn_tmp_75,3)]; }));
(dn_tmp_72 * dn_tmp_73); });
double dn_tmp_76=({ double dn_tmp_77=v_ap2;
double dn_tmp_78=v_y;
(dn_tmp_77 - dn_tmp_78); });
(dn_tmp_71 * dn_tmp_76); });
(dn_tmp_69 + dn_tmp_70); });double *dn_tmp_79=&(v_y);*dn_tmp_79=dn_tmp_80; }
}
else{
if(({ uint32_t dn_tmp_81=v_phaseMode;
uint32_t dn_tmp_82=({ uint32_t dn_tmp_83=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_83>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_83; });
(dn_tmp_81 == dn_tmp_82); })){
{ double dn_tmp_95=({ double dn_tmp_84=v_y;
double dn_tmp_85=({ double dn_tmp_86=({ double dn_tmp_87=((double)(0.30));
double dn_tmp_88=(*({ dn_t_arr3_F64 *dn_tmp_89=&((v_c)->f_amounts);size_t dn_tmp_90=((size_t)(1ULL));&dn_tmp_89->data[dn_index(dn_tmp_90,3)]; }));
(dn_tmp_87 * dn_tmp_88); });
double dn_tmp_91=({ double dn_tmp_92=v_ap1;
double dn_tmp_93=v_y;
(dn_tmp_92 - dn_tmp_93); });
(dn_tmp_86 * dn_tmp_91); });
(dn_tmp_84 + dn_tmp_85); });double *dn_tmp_94=&(v_y);*dn_tmp_94=dn_tmp_95; }
}
else{
}
}
if(({ uint32_t dn_tmp_96=v_filterMode;
uint32_t dn_tmp_97=({ uint32_t dn_tmp_98=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_98>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_98; });
(dn_tmp_96 != dn_tmp_97); })){
double v_low=(*({ dn_t_arr2_F64 *dn_tmp_99=&((v_c)->f_hfState);size_t dn_tmp_100=v_ch;&dn_tmp_99->data[dn_index(dn_tmp_100,2)]; }));
if(({ uint32_t dn_tmp_101=v_filterMode;
uint32_t dn_tmp_102=({ uint32_t dn_tmp_103=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_103>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_103; });
(dn_tmp_101 >= dn_tmp_102); })){
{ double dn_tmp_107=(*({ dn_t_arr2_F64 *dn_tmp_104=&((v_c)->f_warmState);size_t dn_tmp_105=v_ch;&dn_tmp_104->data[dn_index(dn_tmp_105,2)]; }));double *dn_tmp_106=&(v_low);*dn_tmp_106=dn_tmp_107; }
}
else{
}
double v_depth=((double)(0.72));
if(({ uint32_t dn_tmp_108=v_filterMode;
uint32_t dn_tmp_109=({ uint32_t dn_tmp_110=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_110>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_110; });
(dn_tmp_108 == dn_tmp_109); })){
{ double dn_tmp_112=((double)(0.78));double *dn_tmp_111=&(v_depth);*dn_tmp_111=dn_tmp_112; }
}
else{
}
if(({ uint32_t dn_tmp_113=v_filterMode;
uint32_t dn_tmp_114=({ uint32_t dn_tmp_115=((uint32_t)(3ULL));
if((unsigned __int128)dn_tmp_115>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_115; });
(dn_tmp_113 == dn_tmp_114); })){
{ double dn_tmp_117=((double)(0.90));double *dn_tmp_116=&(v_depth);*dn_tmp_116=dn_tmp_117; }
}
else{
}
{ double dn_tmp_129=({ double dn_tmp_118=v_y;
double dn_tmp_119=({ double dn_tmp_120=({ double dn_tmp_121=v_depth;
double dn_tmp_122=(*({ dn_t_arr3_F64 *dn_tmp_123=&((v_c)->f_amounts);size_t dn_tmp_124=((size_t)(2ULL));&dn_tmp_123->data[dn_index(dn_tmp_124,3)]; }));
(dn_tmp_121 * dn_tmp_122); });
double dn_tmp_125=({ double dn_tmp_126=v_low;
double dn_tmp_127=v_x;
(dn_tmp_126 - dn_tmp_127); });
(dn_tmp_120 * dn_tmp_125); });
(dn_tmp_118 + dn_tmp_119); });double *dn_tmp_128=&(v_y);*dn_tmp_128=dn_tmp_129; }
}
else{
}
{ double dn_tmp_130=v_y;return dn_tmp_130; }
}

dn_fail("missing native return");
}
static double dn_cb_creative_channel(dn_r_Creative * v_c,double v_x,size_t v_ch,uint32_t v_phaseMode,uint32_t v_filterMode) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_creative_channel(v_c,v_x,v_ch,v_phaseMode,v_filterMode);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_round_away(double v_x) {
{
if(({ double dn_tmp_0=v_x;
double dn_tmp_1=((double)(0.0));
(dn_tmp_0 >= dn_tmp_1); })){
{ double dn_tmp_5=({ double dn_tmp_2=({ double dn_tmp_3=v_x;
double dn_tmp_4=((double)(0.5));
(dn_tmp_3 + dn_tmp_4); });
floor(dn_tmp_2); });return dn_tmp_5; }
}
else{
}
{ double dn_tmp_9=({ double dn_tmp_6=({ double dn_tmp_7=v_x;
double dn_tmp_8=((double)(0.5));
(dn_tmp_7 - dn_tmp_8); });
ceil(dn_tmp_6); });return dn_tmp_9; }
}

dn_fail("missing native return");
}
static double dn_cb_round_away(double v_x) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_round_away(v_x);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_next_rand(uint32_t * v_state) {
{
uint32_t v_r=(*((uint32_t *)dn_nonnull((void*)(v_state))));
{ uint32_t dn_tmp_5=({ uint32_t dn_tmp_0=v_r;
uint32_t dn_tmp_1=({ uint32_t dn_tmp_2=v_r;
uint32_t dn_tmp_3=((uint32_t)(13ULL));
dn_U32_shl(dn_tmp_2,dn_tmp_3); });
dn_U32_bits((uint64_t)dn_tmp_0 ^ (uint64_t)dn_tmp_1); });uint32_t *dn_tmp_4=&(v_r);*dn_tmp_4=dn_tmp_5; }
{ uint32_t dn_tmp_11=({ uint32_t dn_tmp_6=v_r;
uint32_t dn_tmp_7=({ uint32_t dn_tmp_8=v_r;
uint32_t dn_tmp_9=((uint32_t)(17ULL));
dn_U32_shr(dn_tmp_8,dn_tmp_9); });
dn_U32_bits((uint64_t)dn_tmp_6 ^ (uint64_t)dn_tmp_7); });uint32_t *dn_tmp_10=&(v_r);*dn_tmp_10=dn_tmp_11; }
{ uint32_t dn_tmp_17=({ uint32_t dn_tmp_12=v_r;
uint32_t dn_tmp_13=({ uint32_t dn_tmp_14=v_r;
uint32_t dn_tmp_15=((uint32_t)(5ULL));
dn_U32_shl(dn_tmp_14,dn_tmp_15); });
dn_U32_bits((uint64_t)dn_tmp_12 ^ (uint64_t)dn_tmp_13); });uint32_t *dn_tmp_16=&(v_r);*dn_tmp_16=dn_tmp_17; }
{ uint32_t dn_tmp_19=v_r;uint32_t *dn_tmp_18=&((*((uint32_t *)dn_nonnull((void*)(v_state)))));*dn_tmp_18=dn_tmp_19; }
{ double dn_tmp_25=({ double dn_tmp_20=({ uint32_t dn_tmp_21=v_r;
(double)dn_tmp_21; });
double dn_tmp_22=({ double dn_tmp_23=((double)(1.0));
double dn_tmp_24=((double)(4294967296.0));
(dn_tmp_23 / dn_tmp_24); });
(dn_tmp_20 * dn_tmp_22); });return dn_tmp_25; }
}

dn_fail("missing native return");
}
static double dn_cb_next_rand(uint32_t * v_state) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_next_rand(v_state);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_Pulse dn_f_pulse_create(size_t v_mode,size_t v_channel) {
{
dn_r_Pulse v_p=({ dn_t_arr12_F64 dn_tmp_0=({ ((dn_t_arr12_F64){0}); });
dn_t_arr11_F64 dn_tmp_1=({ ((dn_t_arr11_F64){0}); });
dn_t_arr110_F64 dn_tmp_2=({ ((dn_t_arr110_F64){0}); });
dn_t_arr100_F64 dn_tmp_3=({ ((dn_t_arr100_F64){0}); });
size_t dn_tmp_4=((size_t)(0ULL));
size_t dn_tmp_5=((size_t)(0ULL));
size_t dn_tmp_6=((size_t)(0ULL));
uint32_t dn_tmp_7=({ uint32_t dn_tmp_8=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_8>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_8; });
uint32_t dn_tmp_9=({ uint32_t dn_tmp_10=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_10>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_10; });
double dn_tmp_11=((double)(0.0));
((dn_r_Pulse){dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_9,dn_tmp_11}); });
if(({ size_t dn_tmp_12=v_mode;
size_t dn_tmp_13=((size_t)(1ULL));
(dn_tmp_12 == dn_tmp_13); })){
{ size_t dn_tmp_15=((size_t)(5ULL));size_t *dn_tmp_14=&((v_p).f_order);*dn_tmp_14=dn_tmp_15; }
{ size_t dn_tmp_17=((size_t)(4ULL));size_t *dn_tmp_16=&((v_p).f_elements);*dn_tmp_16=dn_tmp_17; }
}
else{
}
if(({ size_t dn_tmp_18=v_mode;
size_t dn_tmp_19=((size_t)(2ULL));
(dn_tmp_18 == dn_tmp_19); })){
{ size_t dn_tmp_21=((size_t)(11ULL));size_t *dn_tmp_20=&((v_p).f_order);*dn_tmp_20=dn_tmp_21; }
{ size_t dn_tmp_23=((size_t)(10ULL));size_t *dn_tmp_22=&((v_p).f_elements);*dn_tmp_22=dn_tmp_23; }
}
else{
}
{ double dn_tmp_27=((double)(1.0));double *dn_tmp_26=&((*({ dn_t_arr12_F64 *dn_tmp_24=&((v_p).f_ntf);size_t dn_tmp_25=((size_t)(0ULL));&dn_tmp_24->data[dn_index(dn_tmp_25,12)]; })));*dn_tmp_26=dn_tmp_27; }
{ size_t dn_tmp_29=((size_t)(0ULL));size_t dn_tmp_28=(v_p).f_order;for(size_t v_i=dn_tmp_29;v_i<dn_tmp_28;v_i++){
size_t v_k=({ size_t dn_tmp_30=v_i;
size_t dn_tmp_31=((size_t)(1ULL));
dn_USize_add(dn_tmp_30,dn_tmp_31); });
while(({ size_t dn_tmp_32=v_k;
size_t dn_tmp_33=((size_t)(0ULL));
(dn_tmp_32 > dn_tmp_33); })){
{ double dn_tmp_47=({ double dn_tmp_36=(*({ dn_t_arr12_F64 *dn_tmp_37=&((v_p).f_ntf);size_t dn_tmp_38=v_k;&dn_tmp_37->data[dn_index(dn_tmp_38,12)]; }));
double dn_tmp_39=({ double dn_tmp_40=((double)(0.30));
double dn_tmp_41=(*({ dn_t_arr12_F64 *dn_tmp_42=&((v_p).f_ntf);size_t dn_tmp_43=({ size_t dn_tmp_44=v_k;
size_t dn_tmp_45=((size_t)(1ULL));
dn_USize_sub(dn_tmp_44,dn_tmp_45); });&dn_tmp_42->data[dn_index(dn_tmp_43,12)]; }));
(dn_tmp_40 * dn_tmp_41); });
(dn_tmp_36 - dn_tmp_39); });double *dn_tmp_46=&((*({ dn_t_arr12_F64 *dn_tmp_34=&((v_p).f_ntf);size_t dn_tmp_35=v_k;&dn_tmp_34->data[dn_index(dn_tmp_35,12)]; })));*dn_tmp_46=dn_tmp_47; }
{ size_t dn_tmp_51=({ size_t dn_tmp_48=v_k;
size_t dn_tmp_49=((size_t)(1ULL));
dn_USize_sub(dn_tmp_48,dn_tmp_49); });size_t *dn_tmp_50=&(v_k);*dn_tmp_50=dn_tmp_51; }
}
}
 }
{ uint32_t dn_tmp_60=({ uint32_t dn_tmp_52=({ uint32_t dn_tmp_53=((uint32_t)(0x9E3779B9ULL));
if((unsigned __int128)dn_tmp_53>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_53; });
uint32_t dn_tmp_54=({ uint32_t dn_tmp_55=({ size_t dn_tmp_56=v_channel;
if((unsigned __int128)dn_tmp_56>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_56; });
uint32_t dn_tmp_57=({ uint32_t dn_tmp_58=((uint32_t)(0x85EBCA6BULL));
if((unsigned __int128)dn_tmp_58>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_58; });
dn_U32_wrap_mul(dn_tmp_55,dn_tmp_57); });
dn_U32_wrap_add(dn_tmp_52,dn_tmp_54); });uint32_t *dn_tmp_59=&((v_p).f_seed);*dn_tmp_59=dn_tmp_60; }
{ uint32_t dn_tmp_62=(v_p).f_seed;uint32_t *dn_tmp_61=&((v_p).f_rng);*dn_tmp_61=dn_tmp_62; }
if(({ size_t dn_tmp_63=(v_p).f_elements;
size_t dn_tmp_64=((size_t)(0ULL));
(dn_tmp_63 != dn_tmp_64); })){
{ size_t dn_tmp_66=((size_t)(0ULL));size_t dn_tmp_65=(v_p).f_elements;for(size_t v_origin=dn_tmp_66;v_origin<dn_tmp_65;v_origin++){
{ double dn_tmp_72=((double)(0.0));double *dn_tmp_71=&((*({ dn_t_arr110_F64 *dn_tmp_67=&((v_p).f_prefixes);size_t dn_tmp_68=({ size_t dn_tmp_69=v_origin;
size_t dn_tmp_70=((size_t)(11ULL));
dn_USize_mul(dn_tmp_69,dn_tmp_70); });&dn_tmp_67->data[dn_index(dn_tmp_68,110)]; })));*dn_tmp_71=dn_tmp_72; }
{ size_t dn_tmp_74=((size_t)(0ULL));size_t dn_tmp_73=(v_p).f_elements;for(size_t v_i=dn_tmp_74;v_i<dn_tmp_73;v_i++){
size_t v_cell=({ size_t dn_tmp_75=({ size_t dn_tmp_76=v_origin;
size_t dn_tmp_77=v_i;
dn_USize_add(dn_tmp_76,dn_tmp_77); });
size_t dn_tmp_78=(v_p).f_elements;
dn_USize_mod(dn_tmp_75,dn_tmp_78); });
double v_centered=({ double dn_tmp_79=({ double dn_tmp_80=({ double dn_tmp_81=({ double dn_tmp_82=((double)(2.0));
double dn_tmp_83=({ size_t dn_tmp_84=v_cell;
(double)dn_tmp_84; });
(dn_tmp_82 * dn_tmp_83); });
double dn_tmp_85=({ size_t dn_tmp_86=(v_p).f_elements;
(double)dn_tmp_86; });
(dn_tmp_81 - dn_tmp_85); });
double dn_tmp_87=((double)(1.0));
(dn_tmp_80 + dn_tmp_87); });
double dn_tmp_88=({ size_t dn_tmp_89=(v_p).f_elements;
(double)dn_tmp_89; });
(dn_tmp_79 / dn_tmp_88); });
double v_gain=({ double dn_tmp_90=((double)(1.0));
double dn_tmp_91=({ double dn_tmp_92=((double)(0.000001));
double dn_tmp_93=v_centered;
(dn_tmp_92 * dn_tmp_93); });
(dn_tmp_90 + dn_tmp_91); });
{ double dn_tmp_101=v_gain;double *dn_tmp_100=&((*({ dn_t_arr100_F64 *dn_tmp_94=&((v_p).f_gains);size_t dn_tmp_95=({ size_t dn_tmp_96=({ size_t dn_tmp_97=v_origin;
size_t dn_tmp_98=((size_t)(10ULL));
dn_USize_mul(dn_tmp_97,dn_tmp_98); });
size_t dn_tmp_99=v_i;
dn_USize_add(dn_tmp_96,dn_tmp_99); });&dn_tmp_94->data[dn_index(dn_tmp_95,100)]; })));*dn_tmp_100=dn_tmp_101; }
{ double dn_tmp_119=({ double dn_tmp_110=(*({ dn_t_arr110_F64 *dn_tmp_111=&((v_p).f_prefixes);size_t dn_tmp_112=({ size_t dn_tmp_113=({ size_t dn_tmp_114=v_origin;
size_t dn_tmp_115=((size_t)(11ULL));
dn_USize_mul(dn_tmp_114,dn_tmp_115); });
size_t dn_tmp_116=v_i;
dn_USize_add(dn_tmp_113,dn_tmp_116); });&dn_tmp_111->data[dn_index(dn_tmp_112,110)]; }));
double dn_tmp_117=v_gain;
(dn_tmp_110 + dn_tmp_117); });double *dn_tmp_118=&((*({ dn_t_arr110_F64 *dn_tmp_102=&((v_p).f_prefixes);size_t dn_tmp_103=({ size_t dn_tmp_104=({ size_t dn_tmp_105=({ size_t dn_tmp_106=v_origin;
size_t dn_tmp_107=((size_t)(11ULL));
dn_USize_mul(dn_tmp_106,dn_tmp_107); });
size_t dn_tmp_108=v_i;
dn_USize_add(dn_tmp_105,dn_tmp_108); });
size_t dn_tmp_109=((size_t)(1ULL));
dn_USize_add(dn_tmp_104,dn_tmp_109); });&dn_tmp_102->data[dn_index(dn_tmp_103,110)]; })));*dn_tmp_118=dn_tmp_119; }
}
 }
}
 }
{ double dn_tmp_123=(*({ dn_t_arr110_F64 *dn_tmp_120=&((v_p).f_prefixes);size_t dn_tmp_121=(v_p).f_elements;&dn_tmp_120->data[dn_index(dn_tmp_121,110)]; }));double *dn_tmp_122=&((v_p).f_totalGain);*dn_tmp_122=dn_tmp_123; }
}
else{
}
{ dn_r_Pulse dn_tmp_124=v_p;return dn_tmp_124; }
}

dn_fail("missing native return");
}
static dn_r_Pulse dn_cb_pulse_create(size_t v_mode,size_t v_channel) {
 if(!dn_enter()){return (dn_r_Pulse){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_Pulse){0};}
 dn_r_Pulse dn_result=dn_f_pulse_create(v_mode,v_channel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_pulse_reset(dn_r_Pulse * v_p) {
{
{ size_t dn_tmp_1=((size_t)(0ULL));size_t dn_tmp_0=((size_t)(11ULL));for(size_t v_i=dn_tmp_1;v_i<dn_tmp_0;v_i++){
{ double dn_tmp_5=((double)(0.0));double *dn_tmp_4=&((*({ dn_t_arr11_F64 *dn_tmp_2=&((v_p)->f_errors);size_t dn_tmp_3=v_i;&dn_tmp_2->data[dn_index(dn_tmp_3,11)]; })));*dn_tmp_4=dn_tmp_5; }
}
 }
{ size_t dn_tmp_7=((size_t)(0ULL));size_t *dn_tmp_6=&((v_p)->f_position);*dn_tmp_6=dn_tmp_7; }
{ uint32_t dn_tmp_9=(v_p)->f_seed;uint32_t *dn_tmp_8=&((v_p)->f_rng);*dn_tmp_8=dn_tmp_9; }
}

return 0;
}
static void dn_cb_pulse_reset(dn_r_Pulse * v_p) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_pulse_reset(v_p);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static double dn_f_pulse_process(dn_r_Pulse * v_p,double v_signal) {
{
if((({ size_t dn_tmp_0=(v_p)->f_order;
size_t dn_tmp_1=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); }) || (!(({ double dn_tmp_2=v_signal;
isfinite(dn_tmp_2); }))))){
{ double dn_tmp_3=v_signal;return dn_tmp_3; }
}
else{
}
if(({ double dn_tmp_4=({ double dn_tmp_5=v_signal;
fabs(dn_tmp_5); });
double dn_tmp_6=((double)(7.5));
(dn_tmp_4 > dn_tmp_6); })){
(void)(({ dn_r_Pulse * dn_tmp_7=v_p;
dn_f_pulse_reset(dn_tmp_7); }));
{ double dn_tmp_8=v_signal;return dn_tmp_8; }
}
else{
}
double v_feedback=((double)(0.0));
{ size_t dn_tmp_12=((size_t)(1ULL));size_t dn_tmp_11=({ size_t dn_tmp_9=(v_p)->f_order;
size_t dn_tmp_10=((size_t)(1ULL));
dn_USize_add(dn_tmp_9,dn_tmp_10); });for(size_t v_k=dn_tmp_12;v_k<dn_tmp_11;v_k++){
{ double dn_tmp_24=({ double dn_tmp_13=v_feedback;
double dn_tmp_14=({ double dn_tmp_15=(*({ dn_t_arr12_F64 *dn_tmp_16=&((v_p)->f_ntf);size_t dn_tmp_17=v_k;&dn_tmp_16->data[dn_index(dn_tmp_17,12)]; }));
double dn_tmp_18=(*({ dn_t_arr11_F64 *dn_tmp_19=&((v_p)->f_errors);size_t dn_tmp_20=({ size_t dn_tmp_21=v_k;
size_t dn_tmp_22=((size_t)(1ULL));
dn_USize_sub(dn_tmp_21,dn_tmp_22); });&dn_tmp_19->data[dn_index(dn_tmp_20,11)]; }));
(dn_tmp_15 * dn_tmp_18); });
(dn_tmp_13 - dn_tmp_14); });double *dn_tmp_23=&(v_feedback);*dn_tmp_23=dn_tmp_24; }
}
 }
double v_input=({ double dn_tmp_25=v_signal;
double dn_tmp_26=v_feedback;
(dn_tmp_25 + dn_tmp_26); });
double v_step=({ double dn_tmp_27=((double)(1.0));
double dn_tmp_28=((double)(1048576.0));
(dn_tmp_27 / dn_tmp_28); });
double v_tpdf=({ double dn_tmp_29=({ double dn_tmp_30=({ uint32_t * dn_tmp_31=(&((v_p)->f_rng));
dn_f_next_rand(dn_tmp_31); });
double dn_tmp_32=({ uint32_t * dn_tmp_33=(&((v_p)->f_rng));
dn_f_next_rand(dn_tmp_33); });
(dn_tmp_30 - dn_tmp_32); });
double dn_tmp_34=v_step;
(dn_tmp_29 * dn_tmp_34); });
double v_q=({ double dn_tmp_35=({ double dn_tmp_36=({ double dn_tmp_37=({ double dn_tmp_38=v_input;
double dn_tmp_39=v_tpdf;
(dn_tmp_38 + dn_tmp_39); });
double dn_tmp_40=v_step;
(dn_tmp_37 / dn_tmp_40); });
dn_f_round_away(dn_tmp_36); });
double dn_tmp_41=v_step;
(dn_tmp_35 * dn_tmp_41); });
double v_err=({ double dn_tmp_42=v_q;
double dn_tmp_43=v_input;
(dn_tmp_42 - dn_tmp_43); });
size_t v_k=(v_p)->f_order;
while(({ size_t dn_tmp_44=v_k;
size_t dn_tmp_45=((size_t)(1ULL));
(dn_tmp_44 > dn_tmp_45); })){
{ size_t dn_tmp_49=({ size_t dn_tmp_46=v_k;
size_t dn_tmp_47=((size_t)(1ULL));
dn_USize_sub(dn_tmp_46,dn_tmp_47); });size_t *dn_tmp_48=&(v_k);*dn_tmp_48=dn_tmp_49; }
{ double dn_tmp_57=(*({ dn_t_arr11_F64 *dn_tmp_52=&((v_p)->f_errors);size_t dn_tmp_53=({ size_t dn_tmp_54=v_k;
size_t dn_tmp_55=((size_t)(1ULL));
dn_USize_sub(dn_tmp_54,dn_tmp_55); });&dn_tmp_52->data[dn_index(dn_tmp_53,11)]; }));double *dn_tmp_56=&((*({ dn_t_arr11_F64 *dn_tmp_50=&((v_p)->f_errors);size_t dn_tmp_51=v_k;&dn_tmp_50->data[dn_index(dn_tmp_51,11)]; })));*dn_tmp_56=dn_tmp_57; }
}
{ double dn_tmp_61=v_err;double *dn_tmp_60=&((*({ dn_t_arr11_F64 *dn_tmp_58=&((v_p)->f_errors);size_t dn_tmp_59=((size_t)(0ULL));&dn_tmp_58->data[dn_index(dn_tmp_59,11)]; })));*dn_tmp_60=dn_tmp_61; }
if((({ double dn_tmp_62=v_q;
double dn_tmp_63=(-(((double)(1.0))));
(dn_tmp_62 <= dn_tmp_63); }) || ({ double dn_tmp_64=v_q;
double dn_tmp_65=((double)(1.0));
(dn_tmp_64 >= dn_tmp_65); }))){
{ double dn_tmp_66=v_q;return dn_tmp_66; }
}
else{
}
double v_duty=({ double dn_tmp_67=({ double dn_tmp_68=v_q;
double dn_tmp_69=((double)(1.0));
(dn_tmp_68 + dn_tmp_69); });
double dn_tmp_70=({ double dn_tmp_71=((double)(0.5));
double dn_tmp_72=({ size_t dn_tmp_73=(v_p)->f_elements;
(double)dn_tmp_73; });
(dn_tmp_71 * dn_tmp_72); });
(dn_tmp_67 * dn_tmp_70); });
size_t v_full=({ double dn_tmp_74=({ double dn_tmp_75=v_duty;
trunc(dn_tmp_75); });
long double dn_n=(long double)dn_tmp_74;if(!isfinite(dn_tmp_74)||truncl(dn_n)!=dn_n||dn_n<0.0L||dn_n>=ldexpl(1.0L,64))dn_fail("numeric conversion out of range");(size_t)dn_tmp_74; });
double v_fraction=({ double dn_tmp_76=v_duty;
double dn_tmp_77=({ size_t dn_tmp_78=v_full;
(double)dn_tmp_78; });
(dn_tmp_76 - dn_tmp_77); });
size_t v_base=(v_p)->f_position;
double v_sum=({ double dn_tmp_79=(*({ dn_t_arr110_F64 *dn_tmp_80=&((v_p)->f_prefixes);size_t dn_tmp_81=({ size_t dn_tmp_82=({ size_t dn_tmp_83=v_base;
size_t dn_tmp_84=((size_t)(11ULL));
dn_USize_mul(dn_tmp_83,dn_tmp_84); });
size_t dn_tmp_85=v_full;
dn_USize_add(dn_tmp_82,dn_tmp_85); });&dn_tmp_80->data[dn_index(dn_tmp_81,110)]; }));
double dn_tmp_86=({ double dn_tmp_87=v_fraction;
double dn_tmp_88=(*({ dn_t_arr100_F64 *dn_tmp_89=&((v_p)->f_gains);size_t dn_tmp_90=({ size_t dn_tmp_91=({ size_t dn_tmp_92=v_base;
size_t dn_tmp_93=((size_t)(10ULL));
dn_USize_mul(dn_tmp_92,dn_tmp_93); });
size_t dn_tmp_94=v_full;
dn_USize_add(dn_tmp_91,dn_tmp_94); });&dn_tmp_89->data[dn_index(dn_tmp_90,100)]; }));
(dn_tmp_87 * dn_tmp_88); });
(dn_tmp_79 + dn_tmp_86); });
{ size_t dn_tmp_100=({ size_t dn_tmp_95=({ size_t dn_tmp_96=(v_p)->f_position;
size_t dn_tmp_97=((size_t)(1ULL));
dn_USize_add(dn_tmp_96,dn_tmp_97); });
size_t dn_tmp_98=(v_p)->f_elements;
dn_USize_mod(dn_tmp_95,dn_tmp_98); });size_t *dn_tmp_99=&((v_p)->f_position);*dn_tmp_99=dn_tmp_100; }
{ double dn_tmp_107=({ double dn_tmp_101=({ double dn_tmp_102=({ double dn_tmp_103=((double)(2.0));
double dn_tmp_104=v_sum;
(dn_tmp_103 * dn_tmp_104); });
double dn_tmp_105=(v_p)->f_totalGain;
(dn_tmp_102 / dn_tmp_105); });
double dn_tmp_106=((double)(1.0));
(dn_tmp_101 - dn_tmp_106); });return dn_tmp_107; }
}

dn_fail("missing native return");
}
static double dn_cb_pulse_process(dn_r_Pulse * v_p,double v_signal) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_pulse_process(v_p,v_signal);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_Dac dn_f_dac_create(size_t v_mode,size_t v_channel) {
{
dn_r_Dac v_d=({ dn_t_arr12_F64 dn_tmp_0=({ ((dn_t_arr12_F64){0}); });
dn_t_arr11_F64 dn_tmp_1=({ ((dn_t_arr11_F64){0}); });
dn_t_arr110_F64 dn_tmp_2=({ ((dn_t_arr110_F64){0}); });
dn_t_arr100_F64 dn_tmp_3=({ ((dn_t_arr100_F64){0}); });
size_t dn_tmp_4=((size_t)(0ULL));
size_t dn_tmp_5=v_mode;
size_t dn_tmp_6=((size_t)(0ULL));
uint32_t dn_tmp_7=({ uint32_t dn_tmp_8=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_8>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_8; });
uint32_t dn_tmp_9=({ uint32_t dn_tmp_10=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_10>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_10; });
((dn_r_Dac){dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_9}); });
if(({ size_t dn_tmp_11=v_mode;
size_t dn_tmp_12=((size_t)(2ULL));
(dn_tmp_11 == dn_tmp_12); })){
{ size_t dn_tmp_14=((size_t)(5ULL));size_t *dn_tmp_13=&((v_d).f_order);*dn_tmp_13=dn_tmp_14; }
}
else{
}
if(({ size_t dn_tmp_15=v_mode;
size_t dn_tmp_16=((size_t)(3ULL));
(dn_tmp_15 == dn_tmp_16); })){
{ size_t dn_tmp_18=((size_t)(11ULL));size_t *dn_tmp_17=&((v_d).f_order);*dn_tmp_17=dn_tmp_18; }
}
else{
}
{ uint32_t dn_tmp_30=({ uint32_t dn_tmp_19=({ uint32_t dn_tmp_20=((uint32_t)(0xA341316CULL));
if((unsigned __int128)dn_tmp_20>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_20; });
uint32_t dn_tmp_21=({ uint32_t dn_tmp_22=({ uint32_t dn_tmp_23=((uint32_t)(0x9E3779B9ULL));
if((unsigned __int128)dn_tmp_23>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_23; });
uint32_t dn_tmp_24=({ uint32_t dn_tmp_25=({ size_t dn_tmp_26=v_channel;
if((unsigned __int128)dn_tmp_26>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_26; });
uint32_t dn_tmp_27=({ uint32_t dn_tmp_28=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_28>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_28; });
dn_U32_wrap_add(dn_tmp_25,dn_tmp_27); });
dn_U32_wrap_mul(dn_tmp_22,dn_tmp_24); });
dn_U32_bits((uint64_t)dn_tmp_19 ^ (uint64_t)dn_tmp_21); });uint32_t *dn_tmp_29=&((v_d).f_seed);*dn_tmp_29=dn_tmp_30; }
{ uint32_t dn_tmp_32=(v_d).f_seed;uint32_t *dn_tmp_31=&((v_d).f_rng);*dn_tmp_31=dn_tmp_32; }
{ double dn_tmp_36=((double)(1.0));double *dn_tmp_35=&((*({ dn_t_arr12_F64 *dn_tmp_33=&((v_d).f_ntf);size_t dn_tmp_34=((size_t)(0ULL));&dn_tmp_33->data[dn_index(dn_tmp_34,12)]; })));*dn_tmp_35=dn_tmp_36; }
double v_a=((double)(0.30));
if(({ size_t dn_tmp_37=v_mode;
size_t dn_tmp_38=((size_t)(2ULL));
(dn_tmp_37 == dn_tmp_38); })){
{ double dn_tmp_40=((double)(0.48));double *dn_tmp_39=&(v_a);*dn_tmp_39=dn_tmp_40; }
}
else{
}
{ size_t dn_tmp_42=((size_t)(0ULL));size_t dn_tmp_41=(v_d).f_order;for(size_t v_i=dn_tmp_42;v_i<dn_tmp_41;v_i++){
size_t v_k=({ size_t dn_tmp_43=v_i;
size_t dn_tmp_44=((size_t)(1ULL));
dn_USize_add(dn_tmp_43,dn_tmp_44); });
while(({ size_t dn_tmp_45=v_k;
size_t dn_tmp_46=((size_t)(0ULL));
(dn_tmp_45 > dn_tmp_46); })){
{ double dn_tmp_60=({ double dn_tmp_49=(*({ dn_t_arr12_F64 *dn_tmp_50=&((v_d).f_ntf);size_t dn_tmp_51=v_k;&dn_tmp_50->data[dn_index(dn_tmp_51,12)]; }));
double dn_tmp_52=({ double dn_tmp_53=v_a;
double dn_tmp_54=(*({ dn_t_arr12_F64 *dn_tmp_55=&((v_d).f_ntf);size_t dn_tmp_56=({ size_t dn_tmp_57=v_k;
size_t dn_tmp_58=((size_t)(1ULL));
dn_USize_sub(dn_tmp_57,dn_tmp_58); });&dn_tmp_55->data[dn_index(dn_tmp_56,12)]; }));
(dn_tmp_53 * dn_tmp_54); });
(dn_tmp_49 - dn_tmp_52); });double *dn_tmp_59=&((*({ dn_t_arr12_F64 *dn_tmp_47=&((v_d).f_ntf);size_t dn_tmp_48=v_k;&dn_tmp_47->data[dn_index(dn_tmp_48,12)]; })));*dn_tmp_59=dn_tmp_60; }
{ size_t dn_tmp_64=({ size_t dn_tmp_61=v_k;
size_t dn_tmp_62=((size_t)(1ULL));
dn_USize_sub(dn_tmp_61,dn_tmp_62); });size_t *dn_tmp_63=&(v_k);*dn_tmp_63=dn_tmp_64; }
}
}
 }
{ size_t dn_tmp_66=((size_t)(0ULL));size_t dn_tmp_65=((size_t)(10ULL));for(size_t v_origin=dn_tmp_66;v_origin<dn_tmp_65;v_origin++){
{ double dn_tmp_72=((double)(0.0));double *dn_tmp_71=&((*({ dn_t_arr110_F64 *dn_tmp_67=&((v_d).f_prefixes);size_t dn_tmp_68=({ size_t dn_tmp_69=v_origin;
size_t dn_tmp_70=((size_t)(11ULL));
dn_USize_mul(dn_tmp_69,dn_tmp_70); });&dn_tmp_67->data[dn_index(dn_tmp_68,110)]; })));*dn_tmp_71=dn_tmp_72; }
{ size_t dn_tmp_74=((size_t)(0ULL));size_t dn_tmp_73=((size_t)(10ULL));for(size_t v_i=dn_tmp_74;v_i<dn_tmp_73;v_i++){
size_t v_cell=({ size_t dn_tmp_75=({ size_t dn_tmp_76=v_origin;
size_t dn_tmp_77=v_i;
dn_USize_add(dn_tmp_76,dn_tmp_77); });
size_t dn_tmp_78=((size_t)(10ULL));
dn_USize_mod(dn_tmp_75,dn_tmp_78); });
double v_centered=({ double dn_tmp_79=({ double dn_tmp_80=({ double dn_tmp_81=({ double dn_tmp_82=((double)(2.0));
double dn_tmp_83=({ size_t dn_tmp_84=v_cell;
(double)dn_tmp_84; });
(dn_tmp_82 * dn_tmp_83); });
double dn_tmp_85=((double)(1.0));
(dn_tmp_81 + dn_tmp_85); });
double dn_tmp_86=((double)(10.0));
(dn_tmp_80 - dn_tmp_86); });
double dn_tmp_87=((double)(10.0));
(dn_tmp_79 / dn_tmp_87); });
{ double dn_tmp_95=v_centered;double *dn_tmp_94=&((*({ dn_t_arr100_F64 *dn_tmp_88=&((v_d).f_gains);size_t dn_tmp_89=({ size_t dn_tmp_90=({ size_t dn_tmp_91=v_origin;
size_t dn_tmp_92=((size_t)(10ULL));
dn_USize_mul(dn_tmp_91,dn_tmp_92); });
size_t dn_tmp_93=v_i;
dn_USize_add(dn_tmp_90,dn_tmp_93); });&dn_tmp_88->data[dn_index(dn_tmp_89,100)]; })));*dn_tmp_94=dn_tmp_95; }
{ double dn_tmp_113=({ double dn_tmp_104=(*({ dn_t_arr110_F64 *dn_tmp_105=&((v_d).f_prefixes);size_t dn_tmp_106=({ size_t dn_tmp_107=({ size_t dn_tmp_108=v_origin;
size_t dn_tmp_109=((size_t)(11ULL));
dn_USize_mul(dn_tmp_108,dn_tmp_109); });
size_t dn_tmp_110=v_i;
dn_USize_add(dn_tmp_107,dn_tmp_110); });&dn_tmp_105->data[dn_index(dn_tmp_106,110)]; }));
double dn_tmp_111=v_centered;
(dn_tmp_104 + dn_tmp_111); });double *dn_tmp_112=&((*({ dn_t_arr110_F64 *dn_tmp_96=&((v_d).f_prefixes);size_t dn_tmp_97=({ size_t dn_tmp_98=({ size_t dn_tmp_99=({ size_t dn_tmp_100=v_origin;
size_t dn_tmp_101=((size_t)(11ULL));
dn_USize_mul(dn_tmp_100,dn_tmp_101); });
size_t dn_tmp_102=v_i;
dn_USize_add(dn_tmp_99,dn_tmp_102); });
size_t dn_tmp_103=((size_t)(1ULL));
dn_USize_add(dn_tmp_98,dn_tmp_103); });&dn_tmp_96->data[dn_index(dn_tmp_97,110)]; })));*dn_tmp_112=dn_tmp_113; }
}
 }
}
 }
{ dn_r_Dac dn_tmp_114=v_d;return dn_tmp_114; }
}

dn_fail("missing native return");
}
static dn_r_Dac dn_cb_dac_create(size_t v_mode,size_t v_channel) {
 if(!dn_enter()){return (dn_r_Dac){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_Dac){0};}
 dn_r_Dac dn_result=dn_f_dac_create(v_mode,v_channel);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_dac_reset(dn_r_Dac * v_d) {
{
{ size_t dn_tmp_1=((size_t)(0ULL));size_t dn_tmp_0=((size_t)(11ULL));for(size_t v_i=dn_tmp_1;v_i<dn_tmp_0;v_i++){
{ double dn_tmp_5=((double)(0.0));double *dn_tmp_4=&((*({ dn_t_arr11_F64 *dn_tmp_2=&((v_d)->f_errors);size_t dn_tmp_3=v_i;&dn_tmp_2->data[dn_index(dn_tmp_3,11)]; })));*dn_tmp_4=dn_tmp_5; }
}
 }
{ size_t dn_tmp_7=((size_t)(0ULL));size_t *dn_tmp_6=&((v_d)->f_rotation);*dn_tmp_6=dn_tmp_7; }
{ uint32_t dn_tmp_9=(v_d)->f_seed;uint32_t *dn_tmp_8=&((v_d)->f_rng);*dn_tmp_8=dn_tmp_9; }
}

return 0;
}
static void dn_cb_dac_reset(dn_r_Dac * v_d) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_dac_reset(v_d);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static double dn_f_dac_shaped(dn_r_Dac * v_d,double v_signal,double v_quantum) {
{
double v_correction=((double)(0.0));
{ size_t dn_tmp_3=((size_t)(1ULL));size_t dn_tmp_2=({ size_t dn_tmp_0=(v_d)->f_order;
size_t dn_tmp_1=((size_t)(1ULL));
dn_USize_add(dn_tmp_0,dn_tmp_1); });for(size_t v_k=dn_tmp_3;v_k<dn_tmp_2;v_k++){
{ double dn_tmp_15=({ double dn_tmp_4=v_correction;
double dn_tmp_5=({ double dn_tmp_6=(*({ dn_t_arr12_F64 *dn_tmp_7=&((v_d)->f_ntf);size_t dn_tmp_8=v_k;&dn_tmp_7->data[dn_index(dn_tmp_8,12)]; }));
double dn_tmp_9=(*({ dn_t_arr11_F64 *dn_tmp_10=&((v_d)->f_errors);size_t dn_tmp_11=({ size_t dn_tmp_12=v_k;
size_t dn_tmp_13=((size_t)(1ULL));
dn_USize_sub(dn_tmp_12,dn_tmp_13); });&dn_tmp_10->data[dn_index(dn_tmp_11,11)]; }));
(dn_tmp_6 * dn_tmp_9); });
(dn_tmp_4 - dn_tmp_5); });double *dn_tmp_14=&(v_correction);*dn_tmp_14=dn_tmp_15; }
}
 }
double v_input=({ double dn_tmp_16=v_signal;
double dn_tmp_17=v_correction;
(dn_tmp_16 + dn_tmp_17); });
double v_dither=({ double dn_tmp_18=({ double dn_tmp_19=({ uint32_t * dn_tmp_20=(&((v_d)->f_rng));
dn_f_next_rand(dn_tmp_20); });
double dn_tmp_21=({ uint32_t * dn_tmp_22=(&((v_d)->f_rng));
dn_f_next_rand(dn_tmp_22); });
(dn_tmp_19 - dn_tmp_21); });
double dn_tmp_23=v_quantum;
(dn_tmp_18 * dn_tmp_23); });
double v_q=({ double dn_tmp_24=({ double dn_tmp_25=({ double dn_tmp_26=({ double dn_tmp_27=({ double dn_tmp_28=v_input;
double dn_tmp_29=v_dither;
(dn_tmp_28 + dn_tmp_29); });
double dn_tmp_30=v_quantum;
(dn_tmp_27 / dn_tmp_30); });
dn_f_round_away(dn_tmp_26); });
double dn_tmp_31=v_quantum;
(dn_tmp_25 * dn_tmp_31); });
double dn_tmp_32=v_dither;
(dn_tmp_24 - dn_tmp_32); });
double v_err=({ double dn_tmp_33=v_q;
double dn_tmp_34=v_input;
(dn_tmp_33 - dn_tmp_34); });
size_t v_k=(v_d)->f_order;
while(({ size_t dn_tmp_35=v_k;
size_t dn_tmp_36=((size_t)(1ULL));
(dn_tmp_35 > dn_tmp_36); })){
{ size_t dn_tmp_40=({ size_t dn_tmp_37=v_k;
size_t dn_tmp_38=((size_t)(1ULL));
dn_USize_sub(dn_tmp_37,dn_tmp_38); });size_t *dn_tmp_39=&(v_k);*dn_tmp_39=dn_tmp_40; }
{ double dn_tmp_48=(*({ dn_t_arr11_F64 *dn_tmp_43=&((v_d)->f_errors);size_t dn_tmp_44=({ size_t dn_tmp_45=v_k;
size_t dn_tmp_46=((size_t)(1ULL));
dn_USize_sub(dn_tmp_45,dn_tmp_46); });&dn_tmp_43->data[dn_index(dn_tmp_44,11)]; }));double *dn_tmp_47=&((*({ dn_t_arr11_F64 *dn_tmp_41=&((v_d)->f_errors);size_t dn_tmp_42=v_k;&dn_tmp_41->data[dn_index(dn_tmp_42,11)]; })));*dn_tmp_47=dn_tmp_48; }
}
if(({ size_t dn_tmp_49=(v_d)->f_order;
size_t dn_tmp_50=((size_t)(0ULL));
(dn_tmp_49 > dn_tmp_50); })){
{ double dn_tmp_54=v_err;double *dn_tmp_53=&((*({ dn_t_arr11_F64 *dn_tmp_51=&((v_d)->f_errors);size_t dn_tmp_52=((size_t)(0ULL));&dn_tmp_51->data[dn_index(dn_tmp_52,11)]; })));*dn_tmp_53=dn_tmp_54; }
}
else{
}
{ double dn_tmp_55=v_q;return dn_tmp_55; }
}

dn_fail("missing native return");
}
static double dn_cb_dac_shaped(dn_r_Dac * v_d,double v_signal,double v_quantum) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_dac_shaped(v_d,v_signal,v_quantum);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_dac_process(dn_r_Dac * v_d,double v_signal,double v_amount) {
{
if((!(({ double dn_tmp_0=v_signal;
isfinite(dn_tmp_0); })))){
(void)(({ dn_r_Dac * dn_tmp_1=v_d;
dn_f_dac_reset(dn_tmp_1); }));
{ double dn_tmp_2=((double)(0.0));return dn_tmp_2; }
}
else{
}
if(({ size_t dn_tmp_3=(v_d)->f_mode;
size_t dn_tmp_4=((size_t)(0ULL));
(dn_tmp_3 == dn_tmp_4); })){
{ double dn_tmp_5=v_signal;return dn_tmp_5; }
}
else{
}
if(({ double dn_tmp_6=({ double dn_tmp_7=v_signal;
fabs(dn_tmp_7); });
double dn_tmp_8=((double)(4.0));
(dn_tmp_6 > dn_tmp_8); })){
(void)(({ dn_r_Dac * dn_tmp_9=v_d;
dn_f_dac_reset(dn_tmp_9); }));
{ double dn_tmp_10=v_signal;return dn_tmp_10; }
}
else{
}
if(({ size_t dn_tmp_11=(v_d)->f_mode;
size_t dn_tmp_12=((size_t)(1ULL));
(dn_tmp_11 == dn_tmp_12); })){
double v_quantum=({ double dn_tmp_13=((double)(1.0));
double dn_tmp_14=((double)(262144.0));
(dn_tmp_13 / dn_tmp_14); });
double v_dither=({ double dn_tmp_15=({ double dn_tmp_16=({ uint32_t * dn_tmp_17=(&((v_d)->f_rng));
dn_f_next_rand(dn_tmp_17); });
double dn_tmp_18=({ uint32_t * dn_tmp_19=(&((v_d)->f_rng));
dn_f_next_rand(dn_tmp_19); });
(dn_tmp_16 - dn_tmp_18); });
double dn_tmp_20=v_quantum;
(dn_tmp_15 * dn_tmp_20); });
double v_q=({ double dn_tmp_21=({ double dn_tmp_22=({ double dn_tmp_23=({ double dn_tmp_24=({ double dn_tmp_25=v_signal;
double dn_tmp_26=v_dither;
(dn_tmp_25 + dn_tmp_26); });
double dn_tmp_27=v_quantum;
(dn_tmp_24 / dn_tmp_27); });
dn_f_round_away(dn_tmp_23); });
double dn_tmp_28=v_quantum;
(dn_tmp_22 * dn_tmp_28); });
double dn_tmp_29=v_dither;
(dn_tmp_21 - dn_tmp_29); });
{ double dn_tmp_42=({ double dn_tmp_30=v_q;
double dn_tmp_31=({ double dn_tmp_32=({ double dn_tmp_33=({ double dn_tmp_34=v_amount;
double dn_tmp_35=((double)(2.0e-5));
(dn_tmp_34 * dn_tmp_35); });
double dn_tmp_36=v_q;
(dn_tmp_33 * dn_tmp_36); });
double dn_tmp_37=({ double dn_tmp_38=({ double dn_tmp_39=v_q;
double dn_tmp_40=v_q;
(dn_tmp_39 * dn_tmp_40); });
double dn_tmp_41=((double)(0.25));
(dn_tmp_38 - dn_tmp_41); });
(dn_tmp_32 * dn_tmp_37); });
(dn_tmp_30 + dn_tmp_31); });return dn_tmp_42; }
}
else{
}
double v_q=({ dn_r_Dac * dn_tmp_43=v_d;
double dn_tmp_44=v_signal;
double dn_tmp_45=({ double dn_tmp_46=((double)(1.0));
double dn_tmp_47=((double)(1048576.0));
(dn_tmp_46 / dn_tmp_47); });
dn_f_dac_shaped(dn_tmp_43,dn_tmp_44,dn_tmp_45); });
if(((({ size_t dn_tmp_48=(v_d)->f_mode;
size_t dn_tmp_49=((size_t)(2ULL));
(dn_tmp_48 == dn_tmp_49); }) || ({ double dn_tmp_50=v_q;
double dn_tmp_51=(-(((double)(1.0))));
(dn_tmp_50 <= dn_tmp_51); })) || ({ double dn_tmp_52=v_q;
double dn_tmp_53=((double)(1.0));
(dn_tmp_52 >= dn_tmp_53); }))){
{ double dn_tmp_54=v_q;return dn_tmp_54; }
}
else{
}
double v_duty=({ double dn_tmp_55=({ double dn_tmp_56=v_q;
double dn_tmp_57=((double)(1.0));
(dn_tmp_56 + dn_tmp_57); });
double dn_tmp_58=((double)(5.0));
(dn_tmp_55 * dn_tmp_58); });
size_t v_full=({ double dn_tmp_59=({ double dn_tmp_60=v_duty;
trunc(dn_tmp_60); });
long double dn_n=(long double)dn_tmp_59;if(!isfinite(dn_tmp_59)||truncl(dn_n)!=dn_n||dn_n<0.0L||dn_n>=ldexpl(1.0L,64))dn_fail("numeric conversion out of range");(size_t)dn_tmp_59; });
if(({ size_t dn_tmp_61=v_full;
size_t dn_tmp_62=((size_t)(9ULL));
(dn_tmp_61 > dn_tmp_62); })){
{ size_t dn_tmp_64=((size_t)(9ULL));size_t *dn_tmp_63=&(v_full);*dn_tmp_63=dn_tmp_64; }
}
else{
}
double v_fraction=({ double dn_tmp_65=v_duty;
double dn_tmp_66=({ size_t dn_tmp_67=v_full;
(double)dn_tmp_67; });
(dn_tmp_65 - dn_tmp_66); });
size_t v_origin=(v_d)->f_rotation;
double v_gainError=({ double dn_tmp_68=(*({ dn_t_arr110_F64 *dn_tmp_69=&((v_d)->f_prefixes);size_t dn_tmp_70=({ size_t dn_tmp_71=({ size_t dn_tmp_72=v_origin;
size_t dn_tmp_73=((size_t)(11ULL));
dn_USize_mul(dn_tmp_72,dn_tmp_73); });
size_t dn_tmp_74=v_full;
dn_USize_add(dn_tmp_71,dn_tmp_74); });&dn_tmp_69->data[dn_index(dn_tmp_70,110)]; }));
double dn_tmp_75=({ double dn_tmp_76=v_fraction;
double dn_tmp_77=(*({ dn_t_arr100_F64 *dn_tmp_78=&((v_d)->f_gains);size_t dn_tmp_79=({ size_t dn_tmp_80=({ size_t dn_tmp_81=v_origin;
size_t dn_tmp_82=((size_t)(10ULL));
dn_USize_mul(dn_tmp_81,dn_tmp_82); });
size_t dn_tmp_83=v_full;
dn_USize_add(dn_tmp_80,dn_tmp_83); });&dn_tmp_78->data[dn_index(dn_tmp_79,100)]; }));
(dn_tmp_76 * dn_tmp_77); });
(dn_tmp_68 + dn_tmp_75); });
{ size_t dn_tmp_89=({ size_t dn_tmp_84=({ size_t dn_tmp_85=(v_d)->f_rotation;
size_t dn_tmp_86=((size_t)(1ULL));
dn_USize_add(dn_tmp_85,dn_tmp_86); });
size_t dn_tmp_87=((size_t)(10ULL));
dn_USize_mod(dn_tmp_84,dn_tmp_87); });size_t *dn_tmp_88=&((v_d)->f_rotation);*dn_tmp_88=dn_tmp_89; }
{ double dn_tmp_100=({ double dn_tmp_90=v_q;
double dn_tmp_91=({ double dn_tmp_92=({ double dn_tmp_93=({ double dn_tmp_94=v_amount;
double dn_tmp_95=((double)(2.0e-5));
(dn_tmp_94 * dn_tmp_95); });
double dn_tmp_96=({ double dn_tmp_97=((double)(2.0));
double dn_tmp_98=((double)(10.0));
(dn_tmp_97 / dn_tmp_98); });
(dn_tmp_93 * dn_tmp_96); });
double dn_tmp_99=v_gainError;
(dn_tmp_92 * dn_tmp_99); });
(dn_tmp_90 + dn_tmp_91); });return dn_tmp_100; }
}

dn_fail("missing native return");
}
static double dn_cb_dac_process(dn_r_Dac * v_d,double v_signal,double v_amount) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_dac_process(v_d,v_signal,v_amount);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_color_sample(double v_x,double v_amount) {
{
double v_drive=({ double dn_tmp_0=((double)(0.6));
double dn_tmp_1=v_amount;
(dn_tmp_0 * dn_tmp_1); });
if(({ double dn_tmp_2=v_drive;
double dn_tmp_3=((double)(1.0e-8));
(dn_tmp_2 <= dn_tmp_3); })){
{ double dn_tmp_4=v_x;return dn_tmp_4; }
}
else{
}
double v_z=({ double dn_tmp_5=v_drive;
double dn_tmp_6=v_x;
(dn_tmp_5 * dn_tmp_6); });
if(({ double dn_tmp_7=({ double dn_tmp_8=v_z;
fabs(dn_tmp_8); });
double dn_tmp_9=((double)(2.0));
(dn_tmp_7 > dn_tmp_9); })){
if(({ double dn_tmp_10=v_z;
double dn_tmp_11=((double)(20.0));
(dn_tmp_10 > dn_tmp_11); })){
{ double dn_tmp_14=({ double dn_tmp_12=((double)(1.0));
double dn_tmp_13=v_drive;
(dn_tmp_12 / dn_tmp_13); });return dn_tmp_14; }
}
else{
}
if(({ double dn_tmp_15=v_z;
double dn_tmp_16=(-(((double)(20.0))));
(dn_tmp_15 < dn_tmp_16); })){
{ double dn_tmp_19=({ double dn_tmp_17=(-(((double)(1.0))));
double dn_tmp_18=v_drive;
(dn_tmp_17 / dn_tmp_18); });return dn_tmp_19; }
}
else{
}
{ double dn_tmp_34=({ double dn_tmp_20=({ double dn_tmp_21=({ double dn_tmp_22=({ double dn_tmp_23=({ double dn_tmp_24=((double)(2.0));
double dn_tmp_25=v_z;
(dn_tmp_24 * dn_tmp_25); });
exp(dn_tmp_23); });
double dn_tmp_26=((double)(1.0));
(dn_tmp_22 - dn_tmp_26); });
double dn_tmp_27=({ double dn_tmp_28=({ double dn_tmp_29=({ double dn_tmp_30=((double)(2.0));
double dn_tmp_31=v_z;
(dn_tmp_30 * dn_tmp_31); });
exp(dn_tmp_29); });
double dn_tmp_32=((double)(1.0));
(dn_tmp_28 + dn_tmp_32); });
(dn_tmp_21 / dn_tmp_27); });
double dn_tmp_33=v_drive;
(dn_tmp_20 / dn_tmp_33); });return dn_tmp_34; }
}
else{
}
double v_z2=({ double dn_tmp_35=v_z;
double dn_tmp_36=v_z;
(dn_tmp_35 * dn_tmp_36); });
double v_num=({ double dn_tmp_37=((double)(135135.0));
double dn_tmp_38=({ double dn_tmp_39=v_z2;
double dn_tmp_40=({ double dn_tmp_41=((double)(17325.0));
double dn_tmp_42=({ double dn_tmp_43=v_z2;
double dn_tmp_44=({ double dn_tmp_45=((double)(378.0));
double dn_tmp_46=v_z2;
(dn_tmp_45 + dn_tmp_46); });
(dn_tmp_43 * dn_tmp_44); });
(dn_tmp_41 + dn_tmp_42); });
(dn_tmp_39 * dn_tmp_40); });
(dn_tmp_37 + dn_tmp_38); });
double v_den=({ double dn_tmp_47=((double)(135135.0));
double dn_tmp_48=({ double dn_tmp_49=v_z2;
double dn_tmp_50=({ double dn_tmp_51=((double)(62370.0));
double dn_tmp_52=({ double dn_tmp_53=v_z2;
double dn_tmp_54=({ double dn_tmp_55=((double)(3150.0));
double dn_tmp_56=({ double dn_tmp_57=((double)(28.0));
double dn_tmp_58=v_z2;
(dn_tmp_57 * dn_tmp_58); });
(dn_tmp_55 + dn_tmp_56); });
(dn_tmp_53 * dn_tmp_54); });
(dn_tmp_51 + dn_tmp_52); });
(dn_tmp_49 * dn_tmp_50); });
(dn_tmp_47 + dn_tmp_48); });
{ double dn_tmp_65=({ double dn_tmp_59=({ double dn_tmp_60=v_z;
double dn_tmp_61=v_num;
(dn_tmp_60 * dn_tmp_61); });
double dn_tmp_62=({ double dn_tmp_63=v_den;
double dn_tmp_64=v_drive;
(dn_tmp_63 * dn_tmp_64); });
(dn_tmp_59 / dn_tmp_62); });return dn_tmp_65; }
}

dn_fail("missing native return");
}
static double dn_cb_color_sample(double v_x,double v_amount) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_color_sample(v_x,v_amount);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static double dn_f_pulse_color(double v_x,double v_strength,size_t v_model) {
{
if((({ size_t dn_tmp_0=v_model;
size_t dn_tmp_1=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); }) || ({ double dn_tmp_2=v_strength;
double dn_tmp_3=((double)(0.0));
(dn_tmp_2 <= dn_tmp_3); }))){
{ double dn_tmp_4=v_x;return dn_tmp_4; }
}
else{
}
double v_s=({ double dn_tmp_5=v_strength;
double dn_tmp_6=((double)(0.0));
double dn_tmp_7=((double)(1.0));
dn_f_min_f(dn_tmp_5,dn_tmp_6,dn_tmp_7); });
double v_bounded=({ double dn_tmp_8=v_x;
double dn_tmp_9=(-(((double)(2.0))));
double dn_tmp_10=((double)(2.0));
dn_f_min_f(dn_tmp_8,dn_tmp_9,dn_tmp_10); });
double v_sq=({ double dn_tmp_11=v_bounded;
double dn_tmp_12=v_bounded;
(dn_tmp_11 * dn_tmp_12); });
double v_extra=({ double dn_tmp_13=({ double dn_tmp_14=((double)(0.08));
double dn_tmp_15=v_sq;
(dn_tmp_14 * dn_tmp_15); });
double dn_tmp_16=({ double dn_tmp_17=({ double dn_tmp_18=((double)(0.30));
double dn_tmp_19=v_sq;
(dn_tmp_18 * dn_tmp_19); });
double dn_tmp_20=v_bounded;
(dn_tmp_17 * dn_tmp_20); });
(dn_tmp_13 - dn_tmp_16); });
if(({ size_t dn_tmp_21=v_model;
size_t dn_tmp_22=((size_t)(1ULL));
(dn_tmp_21 == dn_tmp_22); })){
{ double dn_tmp_32=({ double dn_tmp_23=({ double dn_tmp_24=((double)(0.21));
double dn_tmp_25=v_sq;
(dn_tmp_24 * dn_tmp_25); });
double dn_tmp_26=({ double dn_tmp_27=({ double dn_tmp_28=((double)(0.15));
double dn_tmp_29=v_sq;
(dn_tmp_28 * dn_tmp_29); });
double dn_tmp_30=v_bounded;
(dn_tmp_27 * dn_tmp_30); });
(dn_tmp_23 - dn_tmp_26); });double *dn_tmp_31=&(v_extra);*dn_tmp_31=dn_tmp_32; }
}
else{
}
{ double dn_tmp_37=({ double dn_tmp_33=v_x;
double dn_tmp_34=({ double dn_tmp_35=v_s;
double dn_tmp_36=v_extra;
(dn_tmp_35 * dn_tmp_36); });
(dn_tmp_33 + dn_tmp_34); });return dn_tmp_37; }
}

dn_fail("missing native return");
}
static double dn_cb_pulse_color(double v_x,double v_strength,size_t v_model) {
 if(!dn_enter()){return (double){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (double){0};}
 double dn_result=dn_f_pulse_color(v_x,v_strength,v_model);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_Engine dn_f_create_engine(double v_sr,size_t v_depth,size_t v_pulseMode,size_t v_dacMode,double v_mismatch) {
{
dn_t_buf_r3_Fir volatile v_ups={0};
dn_t_buf_r3_Fir volatile v_downs={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_r3_Fir(&v_downs);
dn_drop_buf_r3_Fir(&v_ups);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
double v_rate=({ double dn_tmp_2=v_sr;
double dn_tmp_3=((double)(8000.0));
double dn_tmp_4=((double)(384000.0));
dn_f_min_f(dn_tmp_2,dn_tmp_3,dn_tmp_4); });
v_ups=({ size_t dn_tmp_5=((size_t)(0ULL));
dn_new_buf_r3_Fir(dn_tmp_5); });
v_downs=({ size_t dn_tmp_6=((size_t)(0ULL));
dn_new_buf_r3_Fir(dn_tmp_6); });
{ size_t dn_tmp_8=((size_t)(0ULL));size_t dn_tmp_7=((size_t)(2ULL));for(size_t v_c=dn_tmp_8;v_c<dn_tmp_7;v_c++){
(void)(({ dn_t_buf_r3_Fir * dn_tmp_9=(&(v_ups));
dn_r_Fir dn_tmp_10=({ size_t dn_tmp_11=((size_t)(191ULL));
bool dn_tmp_12=1;
dn_f_fir_create(dn_tmp_11,dn_tmp_12); });
dn_adopt_r3_Fir(&dn_tmp_10);
dn_push_buf_r3_Fir(dn_tmp_9,dn_tmp_10); }));
(void)(({ dn_t_buf_r3_Fir * dn_tmp_13=(&(v_downs));
dn_r_Fir dn_tmp_14=({ size_t dn_tmp_15=((size_t)(191ULL));
bool dn_tmp_16=0;
dn_f_fir_create(dn_tmp_15,dn_tmp_16); });
dn_adopt_r3_Fir(&dn_tmp_14);
dn_push_buf_r3_Fir(dn_tmp_13,dn_tmp_14); }));
}
 }
{ size_t dn_tmp_18=((size_t)(0ULL));size_t dn_tmp_17=((size_t)(5ULL));for(size_t v_s=dn_tmp_18;v_s<dn_tmp_17;v_s++){
{ size_t dn_tmp_20=((size_t)(0ULL));size_t dn_tmp_19=((size_t)(2ULL));for(size_t v_c=dn_tmp_20;v_c<dn_tmp_19;v_c++){
(void)(({ dn_t_buf_r3_Fir * dn_tmp_21=(&(v_ups));
dn_r_Fir dn_tmp_22=({ size_t dn_tmp_23=((size_t)(127ULL));
bool dn_tmp_24=1;
dn_f_fir_create(dn_tmp_23,dn_tmp_24); });
dn_adopt_r3_Fir(&dn_tmp_22);
dn_push_buf_r3_Fir(dn_tmp_21,dn_tmp_22); }));
(void)(({ dn_t_buf_r3_Fir * dn_tmp_25=(&(v_downs));
dn_r_Fir dn_tmp_26=({ size_t dn_tmp_27=((size_t)(127ULL));
bool dn_tmp_28=0;
dn_f_fir_create(dn_tmp_27,dn_tmp_28); });
dn_adopt_r3_Fir(&dn_tmp_26);
dn_push_buf_r3_Fir(dn_tmp_25,dn_tmp_26); }));
}
 }
}
 }
size_t v_samples=({ double dn_tmp_29=({ double dn_tmp_30=({ double dn_tmp_31=({ double dn_tmp_32=v_rate;
double dn_tmp_33=((double)(0.00015));
(dn_tmp_32 * dn_tmp_33); });
double dn_tmp_34=((double)(0.5));
(dn_tmp_31 + dn_tmp_34); });
floor(dn_tmp_30); });
long double dn_n=(long double)dn_tmp_29;if(!isfinite(dn_tmp_29)||truncl(dn_n)!=dn_n||dn_n<0.0L||dn_n>=ldexpl(1.0L,64))dn_fail("numeric conversion out of range");(size_t)dn_tmp_29; });
if(({ size_t dn_tmp_35=v_samples;
size_t dn_tmp_36=((size_t)(1ULL));
(dn_tmp_35 < dn_tmp_36); })){
{ size_t dn_tmp_38=((size_t)(1ULL));size_t *dn_tmp_37=&(v_samples);*dn_tmp_37=dn_tmp_38; }
}
else{
}
if(({ size_t dn_tmp_39=v_samples;
size_t dn_tmp_40=((size_t)(63ULL));
(dn_tmp_39 > dn_tmp_40); })){
{ size_t dn_tmp_42=((size_t)(63ULL));size_t *dn_tmp_41=&(v_samples);*dn_tmp_41=dn_tmp_42; }
}
else{
}
double v_smooth=({ double dn_tmp_43=((double)(1.0));
double dn_tmp_44=({ double dn_tmp_45=({ double dn_tmp_46=(-(((double)(1.0))));
double dn_tmp_47=({ double dn_tmp_48=((double)(0.020));
double dn_tmp_49=v_rate;
(dn_tmp_48 * dn_tmp_49); });
(dn_tmp_46 / dn_tmp_47); });
exp(dn_tmp_45); });
(dn_tmp_43 - dn_tmp_44); });
double v_cross=({ double dn_tmp_50=((double)(1.0));
double dn_tmp_51=({ double dn_tmp_52=({ double dn_tmp_53=({ double dn_tmp_54=({ double dn_tmp_55=(-(((double)(2.0))));
double dn_tmp_56=({ dn_f_pi(); });
(dn_tmp_55 * dn_tmp_56); });
double dn_tmp_57=((double)(700.0));
(dn_tmp_54 * dn_tmp_57); });
double dn_tmp_58=v_rate;
(dn_tmp_53 / dn_tmp_58); });
exp(dn_tmp_52); });
(dn_tmp_50 - dn_tmp_51); });
double v_warm=({ double dn_tmp_59=((double)(1.0));
double dn_tmp_60=({ double dn_tmp_61=({ double dn_tmp_62=({ double dn_tmp_63=({ double dn_tmp_64=(-(((double)(2.0))));
double dn_tmp_65=({ dn_f_pi(); });
(dn_tmp_64 * dn_tmp_65); });
double dn_tmp_66=((double)(7500.0));
(dn_tmp_63 * dn_tmp_66); });
double dn_tmp_67=v_rate;
(dn_tmp_62 / dn_tmp_67); });
exp(dn_tmp_61); });
(dn_tmp_59 - dn_tmp_60); });
{ dn_r_Engine dn_tmp_169=({ dn_t_buf_r3_Fir dn_tmp_68=({ dn_t_buf_r3_Fir dn_tmp_69=v_ups;v_ups=(dn_t_buf_r3_Fir){0};dn_adopt_buf_r3_Fir(&dn_tmp_69);dn_tmp_69; });
dn_adopt_buf_r3_Fir(&dn_tmp_68);
dn_t_buf_r3_Fir dn_tmp_70=({ dn_t_buf_r3_Fir dn_tmp_71=v_downs;v_downs=(dn_t_buf_r3_Fir){0};dn_adopt_buf_r3_Fir(&dn_tmp_71);dn_tmp_71; });
dn_adopt_buf_r3_Fir(&dn_tmp_70);
dn_t_arr157_F64 dn_tmp_72=({ ((dn_t_arr157_F64){0}); });
dn_t_arr157_F64 dn_tmp_73=({ ((dn_t_arr157_F64){0}); });
dn_t_arr63_F64 dn_tmp_74=({ ((dn_t_arr63_F64){0}); });
dn_t_arr63_F64 dn_tmp_75=({ ((dn_t_arr63_F64){0}); });
dn_t_arr64_F64 dn_tmp_76=({ ((dn_t_arr64_F64){0}); });
dn_t_arr64_F64 dn_tmp_77=({ ((dn_t_arr64_F64){0}); });
size_t dn_tmp_78=((size_t)(0ULL));
size_t dn_tmp_79=((size_t)(0ULL));
size_t dn_tmp_80=((size_t)(0ULL));
double dn_tmp_81=((double)(0.0));
double dn_tmp_82=((double)(0.0));
double dn_tmp_83=v_cross;
size_t dn_tmp_84=v_samples;
double dn_tmp_85=((double)(0.0));
double dn_tmp_86=((double)(1.0));
double dn_tmp_87=((double)(1.0));
double dn_tmp_88=v_smooth;
double dn_tmp_89=((double)(0.0));
double dn_tmp_90=((double)(0.0));
double dn_tmp_91=v_warm;
double dn_tmp_92=((double)(0.0));
double dn_tmp_93=((double)(0.0));
bool dn_tmp_94=({ double dn_tmp_95=v_rate;
double dn_tmp_96=((double)(88000.0));
(dn_tmp_95 >= dn_tmp_96); });
dn_r_Biquad dn_tmp_97=({ double dn_tmp_98=v_rate;
dn_f_biquad_create(dn_tmp_98); });
dn_r_Biquad dn_tmp_99=({ double dn_tmp_100=v_rate;
dn_f_biquad_create(dn_tmp_100); });
dn_r_Creative dn_tmp_101=({ double dn_tmp_102=v_rate;
dn_f_creative_create(dn_tmp_102); });
dn_t_arr2_r5_Pulse dn_tmp_103=({ dn_r_Pulse dn_tmp_104=({ size_t dn_tmp_105=v_pulseMode;
size_t dn_tmp_106=((size_t)(0ULL));
dn_f_pulse_create(dn_tmp_105,dn_tmp_106); });
dn_r_Pulse dn_tmp_107=({ size_t dn_tmp_108=v_pulseMode;
size_t dn_tmp_109=((size_t)(1ULL));
dn_f_pulse_create(dn_tmp_108,dn_tmp_109); });
((dn_t_arr2_r5_Pulse){{dn_tmp_104,dn_tmp_107}}); });
dn_t_arr2_r3_Dac dn_tmp_110=({ dn_r_Dac dn_tmp_111=({ size_t dn_tmp_112=v_dacMode;
size_t dn_tmp_113=((size_t)(0ULL));
dn_f_dac_create(dn_tmp_112,dn_tmp_113); });
dn_r_Dac dn_tmp_114=({ size_t dn_tmp_115=v_dacMode;
size_t dn_tmp_116=((size_t)(1ULL));
dn_f_dac_create(dn_tmp_115,dn_tmp_116); });
((dn_t_arr2_r3_Dac){{dn_tmp_111,dn_tmp_114}}); });
size_t dn_tmp_117=v_pulseMode;
size_t dn_tmp_118=v_dacMode;
double dn_tmp_119=v_mismatch;
double dn_tmp_120=((double)(0.0));
double dn_tmp_121=((double)(0.0));
size_t dn_tmp_122=v_depth;
double dn_tmp_123=v_rate;
dn_r_OutputStage dn_tmp_124=({ double dn_tmp_125=v_rate;
dn_f_output_prepare(dn_tmp_125); });
dn_r_Tube dn_tmp_126=({ double dn_tmp_127=({ double dn_tmp_128=v_rate;
double dn_tmp_129=({ size_t dn_tmp_130=({ size_t dn_tmp_131=((size_t)(2ULL));
size_t dn_tmp_132=v_depth;
dn_USize_shl(dn_tmp_131,dn_tmp_132); });
(double)dn_tmp_130; });
(dn_tmp_128 * dn_tmp_129); });
dn_f_tube_create(dn_tmp_127); });
dn_adopt_r4_Tube(&dn_tmp_126);
bool dn_tmp_133=0;
dn_r_ThreeBand dn_tmp_134=({ double dn_tmp_135=({ double dn_tmp_136=v_rate;
double dn_tmp_137=({ size_t dn_tmp_138=({ size_t dn_tmp_139=((size_t)(2ULL));
size_t dn_tmp_140=v_depth;
dn_USize_shl(dn_tmp_139,dn_tmp_140); });
(double)dn_tmp_138; });
(dn_tmp_136 * dn_tmp_137); });
dn_f_band_create(dn_tmp_135); });
dn_adopt_r9_ThreeBand(&dn_tmp_134);
size_t dn_tmp_141=((size_t)(0ULL));
dn_t_arr2_F64 dn_tmp_142=({ ((dn_t_arr2_F64){0}); });
double dn_tmp_143=({ double dn_tmp_144=((double)(1.0));
double dn_tmp_145=({ double dn_tmp_146=({ double dn_tmp_147=((double)(0.025));
double dn_tmp_148=v_rate;
(dn_tmp_147 * dn_tmp_148); });
double dn_tmp_149=({ size_t dn_tmp_150=({ size_t dn_tmp_151=((size_t)(2ULL));
size_t dn_tmp_152=v_depth;
dn_USize_shl(dn_tmp_151,dn_tmp_152); });
(double)dn_tmp_150; });
(dn_tmp_146 * dn_tmp_149); });
(dn_tmp_144 / dn_tmp_145); });
size_t dn_tmp_153=v_depth;
size_t dn_tmp_154=v_pulseMode;
size_t dn_tmp_155=v_dacMode;
size_t dn_tmp_156=((size_t)(0ULL));
bool dn_tmp_157=0;
dn_r_Folded dn_tmp_158=({ dn_f_folded_prepare(); });
bool dn_tmp_159=0;
bool dn_tmp_160=0;
size_t dn_tmp_161=((size_t)(0ULL));
size_t dn_tmp_162=((size_t)(0ULL));
bool dn_tmp_163=1;
double dn_tmp_164=((double)(0.0));
size_t dn_tmp_165=v_depth;
size_t dn_tmp_166=v_depth;
size_t dn_tmp_167=((size_t)(128ULL));
size_t dn_tmp_168=((size_t)(0ULL));
((dn_r_Engine){dn_tmp_68,dn_tmp_70,dn_tmp_72,dn_tmp_73,dn_tmp_74,dn_tmp_75,dn_tmp_76,dn_tmp_77,dn_tmp_78,dn_tmp_79,dn_tmp_80,dn_tmp_81,dn_tmp_82,dn_tmp_83,dn_tmp_84,dn_tmp_85,dn_tmp_86,dn_tmp_87,dn_tmp_88,dn_tmp_89,dn_tmp_90,dn_tmp_91,dn_tmp_92,dn_tmp_93,dn_tmp_94,dn_tmp_97,dn_tmp_99,dn_tmp_101,dn_tmp_103,dn_tmp_110,dn_tmp_117,dn_tmp_118,dn_tmp_119,dn_tmp_120,dn_tmp_121,dn_tmp_122,dn_tmp_123,dn_tmp_124,dn_tmp_126,dn_tmp_133,dn_tmp_134,dn_tmp_141,dn_tmp_142,dn_tmp_143,dn_tmp_153,dn_tmp_154,dn_tmp_155,dn_tmp_156,dn_tmp_157,dn_tmp_158,dn_tmp_159,dn_tmp_160,dn_tmp_161,dn_tmp_162,dn_tmp_163,dn_tmp_164,dn_tmp_165,dn_tmp_166,dn_tmp_167,dn_tmp_168}); });dn_adopt_r6_Engine(&dn_tmp_169);dn_drop_buf_r3_Fir(&v_downs);
dn_drop_buf_r3_Fir(&v_ups);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_169; }
dn_drop_buf_r3_Fir(&v_downs);
dn_drop_buf_r3_Fir(&v_ups);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_r_Engine dn_cb_create_engine(double v_sr,size_t v_depth,size_t v_pulseMode,size_t v_dacMode,double v_mismatch) {
 if(!dn_enter()){return (dn_r_Engine){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_Engine){0};}
 dn_r_Engine dn_result=dn_f_create_engine(v_sr,v_depth,v_pulseMode,v_dacMode,v_mismatch);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_live_latency_final(void) {
{
{ uint32_t dn_tmp_1=({ uint32_t dn_tmp_0=((uint32_t)(173ULL));
if((unsigned __int128)dn_tmp_0>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_0; });return dn_tmp_1; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_live_latency_final(void) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_latency_final();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
DN_EXPORT uint32_t live_latency_final(void) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_latency_final();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_live_latency(void) {
{
{ uint32_t dn_tmp_1=({ uint32_t dn_tmp_0=((uint32_t)(157ULL));
if((unsigned __int128)dn_tmp_0>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_0; });return dn_tmp_1; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_live_latency(void) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_latency();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
DN_EXPORT uint32_t live_latency(void) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_latency();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static void * dn_f_live_create(double v_rate,uint32_t v_depth) {
{
if((((!(({ double dn_tmp_0=v_rate;
isfinite(dn_tmp_0); }))) || ({ double dn_tmp_1=v_rate;
double dn_tmp_2=((double)(0.0));
(dn_tmp_1 <= dn_tmp_2); })) || ({ uint32_t dn_tmp_3=v_depth;
uint32_t dn_tmp_4=({ uint32_t dn_tmp_5=((uint32_t)(5ULL));
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_5; });
(dn_tmp_3 > dn_tmp_4); }))){
{ void * dn_tmp_7=({ uint8_t * dn_tmp_6=({ ((uint8_t *)NULL); });
((void*)dn_tmp_6); });return dn_tmp_7; }
}
else{
}
{ void * dn_tmp_16=({ double dn_tmp_8=v_rate;
uint32_t dn_tmp_9=v_depth;
uint32_t dn_tmp_10=({ uint32_t dn_tmp_11=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_11; });
uint32_t dn_tmp_12=({ uint32_t dn_tmp_13=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_13>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_13; });
float dn_tmp_14=({ float dn_tmp_15=((float)(0.25f));
(float)dn_tmp_15; });
dn_f_live_create_ex(dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_12,dn_tmp_14); });return dn_tmp_16; }
}

dn_fail("missing native return");
}
static void * dn_cb_live_create(double v_rate,uint32_t v_depth) {
 if(!dn_enter()){return (void *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (void *){0};}
 void * dn_result=dn_f_live_create(v_rate,v_depth);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
DN_EXPORT void * live_create(double v_rate,uint32_t v_depth) {
 if(!dn_enter()){return (void *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (void *){0};}
 void * dn_result=dn_f_live_create(v_rate,v_depth);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static void * dn_f_live_create_ex(double v_rate,uint32_t v_depth,uint32_t v_pulseMode,uint32_t v_dacMode,float v_mismatch) {
{
dn_r_Engine * volatile v_owner={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_box_r6_Engine(&v_owner);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
if(((((((!(({ double dn_tmp_2=v_rate;
isfinite(dn_tmp_2); }))) || ({ double dn_tmp_3=v_rate;
double dn_tmp_4=((double)(0.0));
(dn_tmp_3 <= dn_tmp_4); })) || ({ uint32_t dn_tmp_5=v_depth;
uint32_t dn_tmp_6=({ uint32_t dn_tmp_7=((uint32_t)(5ULL));
if((unsigned __int128)dn_tmp_7>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_7; });
(dn_tmp_5 > dn_tmp_6); })) || ({ uint32_t dn_tmp_8=v_pulseMode;
uint32_t dn_tmp_9=({ uint32_t dn_tmp_10=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_10>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_10; });
(dn_tmp_8 > dn_tmp_9); })) || ({ uint32_t dn_tmp_11=v_dacMode;
uint32_t dn_tmp_12=({ uint32_t dn_tmp_13=((uint32_t)(3ULL));
if((unsigned __int128)dn_tmp_13>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_13; });
(dn_tmp_11 > dn_tmp_12); })) || (!(({ double dn_tmp_14=({ float dn_tmp_15=v_mismatch;
(double)dn_tmp_15; });
isfinite(dn_tmp_14); }))))){
{ void * dn_tmp_17=({ uint8_t * dn_tmp_16=({ ((uint8_t *)NULL); });
((void*)dn_tmp_16); });dn_drop_box_r6_Engine(&v_owner);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_17; }
}
else{
}
v_owner=({ dn_r_Engine dn_tmp_18=({ double dn_tmp_19=v_rate;
size_t dn_tmp_20=({ uint32_t dn_tmp_21=v_depth;
if((unsigned __int128)dn_tmp_21>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_21; });
size_t dn_tmp_22=({ uint32_t dn_tmp_23=v_pulseMode;
if((unsigned __int128)dn_tmp_23>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_23; });
size_t dn_tmp_24=({ uint32_t dn_tmp_25=v_dacMode;
if((unsigned __int128)dn_tmp_25>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_25; });
double dn_tmp_26=({ double dn_tmp_27=({ float dn_tmp_28=v_mismatch;
(double)dn_tmp_28; });
double dn_tmp_29=((double)(0.0));
double dn_tmp_30=((double)(1.0));
dn_f_min_f(dn_tmp_27,dn_tmp_29,dn_tmp_30); });
dn_f_create_engine(dn_tmp_19,dn_tmp_20,dn_tmp_22,dn_tmp_24,dn_tmp_26); });
dn_adopt_r6_Engine(&dn_tmp_18);
dn_new_box_r6_Engine(dn_tmp_18); });
{ void * dn_tmp_34=({ dn_r_Engine * dn_tmp_31=({ dn_r_Engine * dn_tmp_32=({ dn_r_Engine * dn_tmp_33=v_owner;v_owner=(dn_r_Engine *){0};dn_adopt_box_r6_Engine(&dn_tmp_33);dn_tmp_33; });
dn_adopt_box_r6_Engine(&dn_tmp_32);
({ dn_retain_box_r6_Engine(&dn_tmp_32);dn_tmp_32; }); });
((void*)dn_tmp_31); });dn_drop_box_r6_Engine(&v_owner);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_34; }
dn_drop_box_r6_Engine(&v_owner);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static void * dn_cb_live_create_ex(double v_rate,uint32_t v_depth,uint32_t v_pulseMode,uint32_t v_dacMode,float v_mismatch) {
 if(!dn_enter()){return (void *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (void *){0};}
 void * dn_result=dn_f_live_create_ex(v_rate,v_depth,v_pulseMode,v_dacMode,v_mismatch);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
DN_EXPORT void * live_create_ex(double v_rate,uint32_t v_depth,uint32_t v_pulseMode,uint32_t v_dacMode,float v_mismatch) {
 if(!dn_enter()){return (void *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (void *){0};}
 void * dn_result=dn_f_live_create_ex(v_rate,v_depth,v_pulseMode,v_dacMode,v_mismatch);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_live_destroy(void * v_h) {
{
dn_r_Engine * volatile v_owner={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_box_r6_Engine(&v_owner);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_owner=({ dn_r_Engine * dn_tmp_2=({ void * dn_tmp_3=v_h;
((dn_r_Engine *)dn_tmp_3); });
((dn_r_Engine *)dn_nonnull((void*)dn_tmp_2)); });
dn_drop_box_r6_Engine(&v_owner);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_live_destroy(void * v_h) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_live_destroy(v_h);dn_boundary_end(&dn_b,0);dn_leave();return;
}
DN_EXPORT void live_destroy(void * v_h) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_live_destroy(v_h);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_live_reset(void * v_h) {
{
dn_r_Engine * v_e=({ void * dn_tmp_0=v_h;
((dn_r_Engine *)dn_tmp_0); });
{ size_t dn_tmp_3=((size_t)(0ULL));size_t dn_tmp_2=({ dn_t_buf_r3_Fir const * dn_tmp_1=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_up));
(*(dn_tmp_1)).len; });for(size_t v_i=dn_tmp_3;v_i<dn_tmp_2;v_i++){
(void)(({ dn_r_Fir * dn_tmp_4=(&((*({ dn_t_buf_r3_Fir volatile *dn_tmp_5=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_up);size_t dn_tmp_6=v_i;&dn_tmp_5->data[dn_index(dn_tmp_6,dn_tmp_5->len)]; }))));
dn_f_fir_reset(dn_tmp_4); }));
(void)(({ dn_r_Fir * dn_tmp_7=(&((*({ dn_t_buf_r3_Fir volatile *dn_tmp_8=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_down);size_t dn_tmp_9=v_i;&dn_tmp_8->data[dn_index(dn_tmp_9,dn_tmp_8->len)]; }))));
dn_f_fir_reset(dn_tmp_7); }));
}
 }
{ size_t dn_tmp_11=((size_t)(0ULL));size_t dn_tmp_10=((size_t)(157ULL));for(size_t v_i=dn_tmp_11;v_i<dn_tmp_10;v_i++){
{ double dn_tmp_15=((double)(0.0));double *dn_tmp_14=&((*({ dn_t_arr157_F64 *dn_tmp_12=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dryL);size_t dn_tmp_13=v_i;&dn_tmp_12->data[dn_index(dn_tmp_13,157)]; })));*dn_tmp_14=dn_tmp_15; }
{ double dn_tmp_19=((double)(0.0));double *dn_tmp_18=&((*({ dn_t_arr157_F64 *dn_tmp_16=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dryR);size_t dn_tmp_17=v_i;&dn_tmp_16->data[dn_index(dn_tmp_17,157)]; })));*dn_tmp_18=dn_tmp_19; }
}
 }
{ size_t dn_tmp_21=((size_t)(0ULL));size_t dn_tmp_20=((size_t)(63ULL));for(size_t v_i=dn_tmp_21;v_i<dn_tmp_20;v_i++){
{ double dn_tmp_25=((double)(0.0));double *dn_tmp_24=&((*({ dn_t_arr63_F64 *dn_tmp_22=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_alignedL);size_t dn_tmp_23=v_i;&dn_tmp_22->data[dn_index(dn_tmp_23,63)]; })));*dn_tmp_24=dn_tmp_25; }
{ double dn_tmp_29=((double)(0.0));double *dn_tmp_28=&((*({ dn_t_arr63_F64 *dn_tmp_26=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_alignedR);size_t dn_tmp_27=v_i;&dn_tmp_26->data[dn_index(dn_tmp_27,63)]; })));*dn_tmp_28=dn_tmp_29; }
}
 }
{ size_t dn_tmp_31=((size_t)(0ULL));size_t dn_tmp_30=((size_t)(64ULL));for(size_t v_i=dn_tmp_31;v_i<dn_tmp_30;v_i++){
{ double dn_tmp_35=((double)(0.0));double *dn_tmp_34=&((*({ dn_t_arr64_F64 *dn_tmp_32=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_delayL);size_t dn_tmp_33=v_i;&dn_tmp_32->data[dn_index(dn_tmp_33,64)]; })));*dn_tmp_34=dn_tmp_35; }
{ double dn_tmp_39=((double)(0.0));double *dn_tmp_38=&((*({ dn_t_arr64_F64 *dn_tmp_36=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_delayR);size_t dn_tmp_37=v_i;&dn_tmp_36->data[dn_index(dn_tmp_37,64)]; })));*dn_tmp_38=dn_tmp_39; }
}
 }
{ size_t dn_tmp_41=((size_t)(0ULL));size_t *dn_tmp_40=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dryIndex);*dn_tmp_40=dn_tmp_41; }
{ size_t dn_tmp_43=((size_t)(0ULL));size_t *dn_tmp_42=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_alignIndex);*dn_tmp_42=dn_tmp_43; }
{ size_t dn_tmp_45=((size_t)(0ULL));size_t *dn_tmp_44=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossIndex);*dn_tmp_44=dn_tmp_45; }
{ double dn_tmp_47=((double)(0.0));double *dn_tmp_46=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossLowL);*dn_tmp_46=dn_tmp_47; }
{ double dn_tmp_49=((double)(0.0));double *dn_tmp_48=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossLowR);*dn_tmp_48=dn_tmp_49; }
{ double dn_tmp_51=((double)(0.0));double *dn_tmp_50=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_warmLowL);*dn_tmp_50=dn_tmp_51; }
{ double dn_tmp_53=((double)(0.0));double *dn_tmp_52=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_warmLowR);*dn_tmp_52=dn_tmp_53; }
{ double dn_tmp_55=((double)(0.0));double *dn_tmp_54=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_warmMix);*dn_tmp_54=dn_tmp_55; }
{ double dn_tmp_57=((double)(0.0));double *dn_tmp_56=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_hfMix);*dn_tmp_56=dn_tmp_57; }
{ double dn_tmp_59=((double)(0.0));double *dn_tmp_58=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_hfL).f_z1);*dn_tmp_58=dn_tmp_59; }
{ double dn_tmp_61=((double)(0.0));double *dn_tmp_60=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_hfL).f_z2);*dn_tmp_60=dn_tmp_61; }
{ double dn_tmp_63=((double)(0.0));double *dn_tmp_62=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_hfR).f_z1);*dn_tmp_62=dn_tmp_63; }
{ double dn_tmp_65=((double)(0.0));double *dn_tmp_64=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_hfR).f_z2);*dn_tmp_64=dn_tmp_65; }
{ size_t dn_tmp_67=((size_t)(0ULL));size_t dn_tmp_66=((size_t)(3ULL));for(size_t v_i=dn_tmp_67;v_i<dn_tmp_66;v_i++){
{ double dn_tmp_71=((double)(0.0));double *dn_tmp_70=&((*({ dn_t_arr3_F64 *dn_tmp_68=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative).f_amounts);size_t dn_tmp_69=v_i;&dn_tmp_68->data[dn_index(dn_tmp_69,3)]; })));*dn_tmp_70=dn_tmp_71; }
}
 }
{ size_t dn_tmp_73=((size_t)(0ULL));size_t dn_tmp_72=((size_t)(2ULL));for(size_t v_i=dn_tmp_73;v_i<dn_tmp_72;v_i++){
{ double dn_tmp_77=((double)(0.0));double *dn_tmp_76=&((*({ dn_t_arr2_F64 *dn_tmp_74=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative).f_focusState);size_t dn_tmp_75=v_i;&dn_tmp_74->data[dn_index(dn_tmp_75,2)]; })));*dn_tmp_76=dn_tmp_77; }
{ double dn_tmp_81=((double)(0.0));double *dn_tmp_80=&((*({ dn_t_arr2_F64 *dn_tmp_78=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative).f_warmState);size_t dn_tmp_79=v_i;&dn_tmp_78->data[dn_index(dn_tmp_79,2)]; })));*dn_tmp_80=dn_tmp_81; }
{ double dn_tmp_85=((double)(0.0));double *dn_tmp_84=&((*({ dn_t_arr2_F64 *dn_tmp_82=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative).f_hfState);size_t dn_tmp_83=v_i;&dn_tmp_82->data[dn_index(dn_tmp_83,2)]; })));*dn_tmp_84=dn_tmp_85; }
}
 }
{ size_t dn_tmp_87=((size_t)(0ULL));size_t dn_tmp_86=((size_t)(4ULL));for(size_t v_i=dn_tmp_87;v_i<dn_tmp_86;v_i++){
{ double dn_tmp_91=((double)(0.0));double *dn_tmp_90=&(((*({ dn_t_arr4_r7_Allpass *dn_tmp_88=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative).f_allpass);size_t dn_tmp_89=v_i;&dn_tmp_88->data[dn_index(dn_tmp_89,4)]; }))).f_input);*dn_tmp_90=dn_tmp_91; }
{ double dn_tmp_95=((double)(0.0));double *dn_tmp_94=&(((*({ dn_t_arr4_r7_Allpass *dn_tmp_92=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative).f_allpass);size_t dn_tmp_93=v_i;&dn_tmp_92->data[dn_index(dn_tmp_93,4)]; }))).f_output);*dn_tmp_94=dn_tmp_95; }
}
 }
(void)(({ dn_r_Pulse * dn_tmp_96=(&((*({ dn_t_arr2_r5_Pulse *dn_tmp_97=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pulses);size_t dn_tmp_98=((size_t)(0ULL));&dn_tmp_97->data[dn_index(dn_tmp_98,2)]; }))));
dn_f_pulse_reset(dn_tmp_96); }));
(void)(({ dn_r_Pulse * dn_tmp_99=(&((*({ dn_t_arr2_r5_Pulse *dn_tmp_100=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pulses);size_t dn_tmp_101=((size_t)(1ULL));&dn_tmp_100->data[dn_index(dn_tmp_101,2)]; }))));
dn_f_pulse_reset(dn_tmp_99); }));
(void)(({ dn_r_Dac * dn_tmp_102=(&((*({ dn_t_arr2_r3_Dac *dn_tmp_103=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dacs);size_t dn_tmp_104=((size_t)(0ULL));&dn_tmp_103->data[dn_index(dn_tmp_104,2)]; }))));
dn_f_dac_reset(dn_tmp_102); }));
(void)(({ dn_r_Dac * dn_tmp_105=(&((*({ dn_t_arr2_r3_Dac *dn_tmp_106=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dacs);size_t dn_tmp_107=((size_t)(1ULL));&dn_tmp_106->data[dn_index(dn_tmp_107,2)]; }))));
dn_f_dac_reset(dn_tmp_105); }));
(void)(({ dn_r_OutputStage * dn_tmp_108=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_output));
dn_f_output_reset(dn_tmp_108); }));
(void)(({ dn_r_Tube * dn_tmp_109=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_tube));
dn_f_tube_reset(dn_tmp_109); }));
(void)(({ dn_r_ThreeBand * dn_tmp_110=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_threeBand));
dn_f_band_reset(dn_tmp_110); }));
{ double dn_tmp_115=({ size_t dn_tmp_113=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_tubeRouting;
(double)dn_tmp_113; });double *dn_tmp_114=&((*({ dn_t_arr2_F64 *dn_tmp_111=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_tubeRouteBlend);size_t dn_tmp_112=((size_t)(0ULL));&dn_tmp_111->data[dn_index(dn_tmp_112,2)]; })));*dn_tmp_114=dn_tmp_115; }
{ double dn_tmp_120=({ size_t dn_tmp_118=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_tubeRouting;
(double)dn_tmp_118; });double *dn_tmp_119=&((*({ dn_t_arr2_F64 *dn_tmp_116=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_tubeRouteBlend);size_t dn_tmp_117=((size_t)(1ULL));&dn_tmp_116->data[dn_index(dn_tmp_117,2)]; })));*dn_tmp_119=dn_tmp_120; }
{ size_t dn_tmp_122=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_depth;size_t *dn_tmp_121=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pendingDepth);*dn_tmp_121=dn_tmp_122; }
{ size_t dn_tmp_124=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pulseMode;size_t *dn_tmp_123=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pendingPulse);*dn_tmp_123=dn_tmp_124; }
{ size_t dn_tmp_126=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dacMode;size_t *dn_tmp_125=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pendingDac);*dn_tmp_125=dn_tmp_126; }
{ size_t dn_tmp_128=((size_t)(0ULL));size_t *dn_tmp_127=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_transitionCounter);*dn_tmp_127=dn_tmp_128; }
{ bool dn_tmp_130=0;bool *dn_tmp_129=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_transitioning);*dn_tmp_129=dn_tmp_130; }
(void)(({ dn_r_Folded * dn_tmp_131=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_folded));
dn_f_folded_reset(dn_tmp_131); }));
{ bool dn_tmp_133=0;bool *dn_tmp_132=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedHistoryActive);*dn_tmp_132=dn_tmp_133; }
{ size_t dn_tmp_135=((size_t)(0ULL));size_t *dn_tmp_134=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedPriming);*dn_tmp_134=dn_tmp_135; }
{ bool dn_tmp_137=1;bool *dn_tmp_136=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_legacyRunning);*dn_tmp_136=dn_tmp_137; }
{ size_t dn_tmp_139=((size_t)(0ULL));size_t *dn_tmp_138=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_legacyPriming);*dn_tmp_138=dn_tmp_139; }
{ double dn_tmp_141=((double)(0.0));double *dn_tmp_140=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_fastWeight);*dn_tmp_140=dn_tmp_141; }
}

return 0;
}
static void dn_cb_live_reset(void * v_h) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_live_reset(v_h);dn_boundary_end(&dn_b,0);dn_leave();return;
}
DN_EXPORT void live_reset(void * v_h) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_live_reset(v_h);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint32_t dn_f_live_process(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode) {
{
{ uint32_t dn_tmp_12=({ void * dn_tmp_0=v_h;
float * dn_tmp_1=v_left;
float * dn_tmp_2=v_right;
size_t dn_tmp_3=v_frames;
float dn_tmp_4=v_crossfeed;
float dn_tmp_5=v_mix;
float dn_tmp_6=v_gainDb;
uint32_t dn_tmp_7=v_filterMode;
uint32_t dn_tmp_8=((uint32_t)(0ULL));
float dn_tmp_9=((float)(0.0f));
float dn_tmp_10=((float)(0.0f));
float dn_tmp_11=((float)(0.0f));
dn_f_live_process_ex(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_11); });return dn_tmp_12; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_live_process(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_process(v_h,v_left,v_right,v_frames,v_crossfeed,v_mix,v_gainDb,v_filterMode);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
DN_EXPORT uint32_t live_process(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_process(v_h,v_left,v_right,v_frames,v_crossfeed,v_mix,v_gainDb,v_filterMode);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_live_process_ex(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode,uint32_t v_phaseMode,float v_focus,float v_phase,float v_tone) {
{
{ uint32_t dn_tmp_14=({ void * dn_tmp_0=v_h;
float * dn_tmp_1=v_left;
float * dn_tmp_2=v_right;
size_t dn_tmp_3=v_frames;
float dn_tmp_4=v_crossfeed;
float dn_tmp_5=v_mix;
float dn_tmp_6=v_gainDb;
uint32_t dn_tmp_7=v_filterMode;
uint32_t dn_tmp_8=v_phaseMode;
float dn_tmp_9=v_focus;
float dn_tmp_10=v_phase;
float dn_tmp_11=v_tone;
float dn_tmp_12=((float)(0.0f));
float dn_tmp_13=((float)(0.0f));
dn_f_live_process_full(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_11,dn_tmp_12,dn_tmp_13); });return dn_tmp_14; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_live_process_ex(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode,uint32_t v_phaseMode,float v_focus,float v_phase,float v_tone) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_process_ex(v_h,v_left,v_right,v_frames,v_crossfeed,v_mix,v_gainDb,v_filterMode,v_phaseMode,v_focus,v_phase,v_tone);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
DN_EXPORT uint32_t live_process_ex(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode,uint32_t v_phaseMode,float v_focus,float v_phase,float v_tone) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_process_ex(v_h,v_left,v_right,v_frames,v_crossfeed,v_mix,v_gainDb,v_filterMode,v_phaseMode,v_focus,v_phase,v_tone);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_live_tube_cpu_mode(void * v_h,uint32_t v_mode) {
{
if(({ uint32_t dn_tmp_0=v_mode;
uint32_t dn_tmp_1=({ uint32_t dn_tmp_2=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_2>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_2; });
(dn_tmp_0 > dn_tmp_1); })){
{ uint32_t dn_tmp_4=({ uint32_t dn_tmp_3=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_3>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_3; });return dn_tmp_4; }
}
else{
}
dn_r_Engine * v_e=({ void * dn_tmp_5=v_h;
((dn_r_Engine *)dn_tmp_5); });
{ size_t dn_tmp_8=({ uint32_t dn_tmp_6=v_mode;
if((unsigned __int128)dn_tmp_6>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_6; });size_t *dn_tmp_7=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_tubeCpuMode);*dn_tmp_7=dn_tmp_8; }
size_t v_depth=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_depth;
if((({ size_t dn_tmp_9=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_tubeCpuMode;
size_t dn_tmp_10=((size_t)(1ULL));
(dn_tmp_9 == dn_tmp_10); }) && ({ size_t dn_tmp_11=v_depth;
size_t dn_tmp_12=((size_t)(4ULL));
(dn_tmp_11 >= dn_tmp_12); }))){
{ size_t dn_tmp_14=((size_t)(3ULL));size_t *dn_tmp_13=&(v_depth);*dn_tmp_13=dn_tmp_14; }
}
else{
}
double v_rate=({ double dn_tmp_15=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_sampleRate;
double dn_tmp_16=({ size_t dn_tmp_17=({ size_t dn_tmp_18=((size_t)(2ULL));
size_t dn_tmp_19=v_depth;
dn_USize_shl(dn_tmp_18,dn_tmp_19); });
(double)dn_tmp_17; });
(dn_tmp_15 * dn_tmp_16); });
(void)(({ dn_r_Tube * dn_tmp_20=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_tube));
double dn_tmp_21=v_rate;
dn_f_tube_set_rate(dn_tmp_20,dn_tmp_21); }));
(void)(({ dn_r_ThreeBand * dn_tmp_22=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_threeBand));
double dn_tmp_23=v_rate;
dn_f_band_set_rate(dn_tmp_22,dn_tmp_23); }));
{ double dn_tmp_29=({ double dn_tmp_24=((double)(1.0));
double dn_tmp_25=({ double dn_tmp_26=((double)(0.025));
double dn_tmp_27=v_rate;
(dn_tmp_26 * dn_tmp_27); });
(dn_tmp_24 / dn_tmp_25); });double *dn_tmp_28=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_tubeRouteStep);*dn_tmp_28=dn_tmp_29; }
{ uint32_t dn_tmp_31=({ uint32_t dn_tmp_30=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_30>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_30; });return dn_tmp_31; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_live_tube_cpu_mode(void * v_h,uint32_t v_mode) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_tube_cpu_mode(v_h,v_mode);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
DN_EXPORT uint32_t live_tube_cpu_mode(void * v_h,uint32_t v_mode) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_tube_cpu_mode(v_h,v_mode);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_live_fast_mode(void * v_h,bool v_enabled) {
{
dn_r_Engine * v_e=({ void * dn_tmp_0=v_h;
((dn_r_Engine *)dn_tmp_0); });
{ bool dn_tmp_2=v_enabled;bool *dn_tmp_1=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_fastEnabled);*dn_tmp_1=dn_tmp_2; }
{ uint32_t dn_tmp_4=({ uint32_t dn_tmp_3=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_3>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_3; });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_live_fast_mode(void * v_h,bool v_enabled) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_fast_mode(v_h,v_enabled);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
DN_EXPORT uint32_t live_fast_mode(void * v_h,bool v_enabled) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_fast_mode(v_h,v_enabled);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_live_request_mode(void * v_h,uint32_t v_depth,uint32_t v_pulseMode,uint32_t v_dacMode,float v_mismatch) {
{
if((((({ uint32_t dn_tmp_0=v_depth;
uint32_t dn_tmp_1=({ uint32_t dn_tmp_2=((uint32_t)(5ULL));
if((unsigned __int128)dn_tmp_2>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_2; });
(dn_tmp_0 > dn_tmp_1); }) || ({ uint32_t dn_tmp_3=v_pulseMode;
uint32_t dn_tmp_4=({ uint32_t dn_tmp_5=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_5; });
(dn_tmp_3 > dn_tmp_4); })) || ({ uint32_t dn_tmp_6=v_dacMode;
uint32_t dn_tmp_7=({ uint32_t dn_tmp_8=((uint32_t)(3ULL));
if((unsigned __int128)dn_tmp_8>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_8; });
(dn_tmp_6 > dn_tmp_7); })) || (!(({ double dn_tmp_9=({ float dn_tmp_10=v_mismatch;
(double)dn_tmp_10; });
isfinite(dn_tmp_9); }))))){
{ uint32_t dn_tmp_12=({ uint32_t dn_tmp_11=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_11; });return dn_tmp_12; }
}
else{
}
dn_r_Engine * v_e=({ void * dn_tmp_13=v_h;
((dn_r_Engine *)dn_tmp_13); });
{ double dn_tmp_19=({ double dn_tmp_14=({ float dn_tmp_15=v_mismatch;
(double)dn_tmp_15; });
double dn_tmp_16=((double)(0.0));
double dn_tmp_17=((double)(1.0));
dn_f_min_f(dn_tmp_14,dn_tmp_16,dn_tmp_17); });double *dn_tmp_18=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_mismatch);*dn_tmp_18=dn_tmp_19; }
if(((!(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_transitioning)) && ((({ size_t dn_tmp_20=({ uint32_t dn_tmp_21=v_depth;
if((unsigned __int128)dn_tmp_21>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_21; });
size_t dn_tmp_22=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_depth;
(dn_tmp_20 != dn_tmp_22); }) || ({ size_t dn_tmp_23=({ uint32_t dn_tmp_24=v_pulseMode;
if((unsigned __int128)dn_tmp_24>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_24; });
size_t dn_tmp_25=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pulseMode;
(dn_tmp_23 != dn_tmp_25); })) || ({ size_t dn_tmp_26=({ uint32_t dn_tmp_27=v_dacMode;
if((unsigned __int128)dn_tmp_27>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_27; });
size_t dn_tmp_28=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dacMode;
(dn_tmp_26 != dn_tmp_28); })))){
{ size_t dn_tmp_31=({ uint32_t dn_tmp_29=v_depth;
if((unsigned __int128)dn_tmp_29>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_29; });size_t *dn_tmp_30=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pendingDepth);*dn_tmp_30=dn_tmp_31; }
{ size_t dn_tmp_34=({ uint32_t dn_tmp_32=v_pulseMode;
if((unsigned __int128)dn_tmp_32>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_32; });size_t *dn_tmp_33=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pendingPulse);*dn_tmp_33=dn_tmp_34; }
{ size_t dn_tmp_37=({ uint32_t dn_tmp_35=v_dacMode;
if((unsigned __int128)dn_tmp_35>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_35; });size_t *dn_tmp_36=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pendingDac);*dn_tmp_36=dn_tmp_37; }
{ size_t dn_tmp_39=((size_t)(0ULL));size_t *dn_tmp_38=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_transitionCounter);*dn_tmp_38=dn_tmp_39; }
{ bool dn_tmp_41=1;bool *dn_tmp_40=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_transitioning);*dn_tmp_40=dn_tmp_41; }
}
else{
}
{ uint32_t dn_tmp_43=({ uint32_t dn_tmp_42=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_42>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_42; });return dn_tmp_43; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_live_request_mode(void * v_h,uint32_t v_depth,uint32_t v_pulseMode,uint32_t v_dacMode,float v_mismatch) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_request_mode(v_h,v_depth,v_pulseMode,v_dacMode,v_mismatch);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
DN_EXPORT uint32_t live_request_mode(void * v_h,uint32_t v_depth,uint32_t v_pulseMode,uint32_t v_dacMode,float v_mismatch) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_request_mode(v_h,v_depth,v_pulseMode,v_dacMode,v_mismatch);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_live_process_full(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode,uint32_t v_phaseMode,float v_focus,float v_phase,float v_tone,float v_character,float v_pulseIntensity) {
{
dn_r_Engine * v_e=({ void * dn_tmp_0=v_h;
((dn_r_Engine *)dn_tmp_0); });
dn_t_foreign_F32 v_lbuf=({ float * dn_tmp_1=v_left;
size_t dn_tmp_2=v_frames;
({ if(dn_tmp_2&&!dn_tmp_1)dn_fail("null foreign buffer");dn_size(dn_tmp_2,sizeof(*dn_tmp_1));(dn_t_foreign_F32){dn_tmp_1,dn_tmp_2}; }); });
dn_t_foreign_F32 v_rbuf=({ float * dn_tmp_3=v_right;
size_t dn_tmp_4=v_frames;
({ if(dn_tmp_4&&!dn_tmp_3)dn_fail("null foreign buffer");dn_size(dn_tmp_4,sizeof(*dn_tmp_3));(dn_t_foreign_F32){dn_tmp_3,dn_tmp_4}; }); });
double v_xTarget=({ double dn_tmp_5=({ float dn_tmp_6=v_crossfeed;
(double)dn_tmp_6; });
double dn_tmp_7=((double)(0.0));
double dn_tmp_8=((double)(1.0));
dn_f_min_f(dn_tmp_5,dn_tmp_7,dn_tmp_8); });
double v_mTarget=({ double dn_tmp_9=({ float dn_tmp_10=v_mix;
(double)dn_tmp_10; });
double dn_tmp_11=((double)(0.0));
double dn_tmp_12=((double)(1.0));
dn_f_min_f(dn_tmp_9,dn_tmp_11,dn_tmp_12); });
double v_gainTarget=({ double dn_tmp_13=({ double dn_tmp_14=({ double dn_tmp_15=({ double dn_tmp_16=((double)(10.0));
log(dn_tmp_16); });
double dn_tmp_17=({ double dn_tmp_18=({ float dn_tmp_19=v_gainDb;
(double)dn_tmp_19; });
double dn_tmp_20=(-(((double)(24.0))));
double dn_tmp_21=((double)(12.0));
dn_f_min_f(dn_tmp_18,dn_tmp_20,dn_tmp_21); });
(dn_tmp_15 * dn_tmp_17); });
double dn_tmp_22=((double)(20.0));
(dn_tmp_14 / dn_tmp_22); });
exp(dn_tmp_13); });
uint32_t v_mode=v_filterMode;
bool v_wantFast=(((((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_fastEnabled && ({ size_t dn_tmp_23=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pulseMode;
size_t dn_tmp_24=((size_t)(0ULL));
(dn_tmp_23 == dn_tmp_24); })) && ({ size_t dn_tmp_25=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dacMode;
size_t dn_tmp_26=((size_t)(0ULL));
(dn_tmp_25 == dn_tmp_26); })) && ({ float dn_tmp_27=v_character;
float dn_tmp_28=({ float dn_tmp_29=((float)(0.0f));
(float)dn_tmp_29; });
(dn_tmp_27 == dn_tmp_28); })) && (!(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_tubeActive))) && (!(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_transitioning)));
if((v_wantFast && (!(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedHistoryActive)))){
(void)(({ dn_r_Folded * dn_tmp_30=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_folded));
dn_f_folded_reset(dn_tmp_30); }));
{ bool dn_tmp_32=1;bool *dn_tmp_31=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedHistoryActive);*dn_tmp_31=dn_tmp_32; }
{ size_t dn_tmp_34=((size_t)(0ULL));size_t *dn_tmp_33=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedPriming);*dn_tmp_33=dn_tmp_34; }
{ size_t dn_tmp_36=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_depth;size_t *dn_tmp_35=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedDepth);*dn_tmp_35=dn_tmp_36; }
{ size_t dn_tmp_38=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_depth;size_t *dn_tmp_37=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_previousFoldedDepth);*dn_tmp_37=dn_tmp_38; }
{ size_t dn_tmp_40=((size_t)(128ULL));size_t *dn_tmp_39=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedFade);*dn_tmp_39=dn_tmp_40; }
}
else{
}
if(((!(v_wantFast)) && (!(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_legacyRunning)))){
{ size_t dn_tmp_43=((size_t)(0ULL));size_t dn_tmp_42=({ dn_t_buf_r3_Fir const * dn_tmp_41=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_up));
(*(dn_tmp_41)).len; });for(size_t v_j=dn_tmp_43;v_j<dn_tmp_42;v_j++){
(void)(({ dn_r_Fir * dn_tmp_44=(&((*({ dn_t_buf_r3_Fir volatile *dn_tmp_45=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_up);size_t dn_tmp_46=v_j;&dn_tmp_45->data[dn_index(dn_tmp_46,dn_tmp_45->len)]; }))));
dn_f_fir_reset(dn_tmp_44); }));
(void)(({ dn_r_Fir * dn_tmp_47=(&((*({ dn_t_buf_r3_Fir volatile *dn_tmp_48=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_down);size_t dn_tmp_49=v_j;&dn_tmp_48->data[dn_index(dn_tmp_49,dn_tmp_48->len)]; }))));
dn_f_fir_reset(dn_tmp_47); }));
}
 }
{ size_t dn_tmp_51=((size_t)(0ULL));size_t dn_tmp_50=((size_t)(63ULL));for(size_t v_j=dn_tmp_51;v_j<dn_tmp_50;v_j++){
{ double dn_tmp_55=((double)(0.0));double *dn_tmp_54=&((*({ dn_t_arr63_F64 *dn_tmp_52=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_alignedL);size_t dn_tmp_53=v_j;&dn_tmp_52->data[dn_index(dn_tmp_53,63)]; })));*dn_tmp_54=dn_tmp_55; }
{ double dn_tmp_59=((double)(0.0));double *dn_tmp_58=&((*({ dn_t_arr63_F64 *dn_tmp_56=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_alignedR);size_t dn_tmp_57=v_j;&dn_tmp_56->data[dn_index(dn_tmp_57,63)]; })));*dn_tmp_58=dn_tmp_59; }
}
 }
{ size_t dn_tmp_61=((size_t)(0ULL));size_t *dn_tmp_60=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_alignIndex);*dn_tmp_60=dn_tmp_61; }
{ bool dn_tmp_63=1;bool *dn_tmp_62=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_legacyRunning);*dn_tmp_62=dn_tmp_63; }
{ size_t dn_tmp_65=((size_t)(0ULL));size_t *dn_tmp_64=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_legacyPriming);*dn_tmp_64=dn_tmp_65; }
}
else{
}
{ size_t dn_tmp_67=((size_t)(0ULL));size_t dn_tmp_66=v_frames;for(size_t v_i=dn_tmp_67;v_i<dn_tmp_66;v_i++){
double v_l=({ float dn_tmp_68=(*({ dn_t_foreign_F32 *dn_tmp_69=&(v_lbuf);size_t dn_tmp_70=v_i;&dn_tmp_69->data[dn_index(dn_tmp_70,dn_tmp_69->len)]; }));
(double)dn_tmp_68; });
double v_r=({ float dn_tmp_71=(*({ dn_t_foreign_F32 *dn_tmp_72=&(v_rbuf);size_t dn_tmp_73=v_i;&dn_tmp_72->data[dn_index(dn_tmp_73,dn_tmp_72->len)]; }));
(double)dn_tmp_71; });
if((!(({ double dn_tmp_74=v_l;
isfinite(dn_tmp_74); })))){
{ double dn_tmp_76=((double)(0.0));double *dn_tmp_75=&(v_l);*dn_tmp_75=dn_tmp_76; }
}
else{
}
if((!(({ double dn_tmp_77=v_r;
isfinite(dn_tmp_77); })))){
{ double dn_tmp_79=((double)(0.0));double *dn_tmp_78=&(v_r);*dn_tmp_78=dn_tmp_79; }
}
else{
}
double v_inputL=v_l;
double v_inputR=v_r;
{ double dn_tmp_87=({ double dn_tmp_80=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossCurrent;
double dn_tmp_81=({ double dn_tmp_82=({ double dn_tmp_83=v_xTarget;
double dn_tmp_84=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossCurrent;
(dn_tmp_83 - dn_tmp_84); });
double dn_tmp_85=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_controlAlpha;
(dn_tmp_82 * dn_tmp_85); });
(dn_tmp_80 + dn_tmp_81); });double *dn_tmp_86=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossCurrent);*dn_tmp_86=dn_tmp_87; }
{ double dn_tmp_95=({ double dn_tmp_88=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_mixCurrent;
double dn_tmp_89=({ double dn_tmp_90=({ double dn_tmp_91=v_mTarget;
double dn_tmp_92=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_mixCurrent;
(dn_tmp_91 - dn_tmp_92); });
double dn_tmp_93=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_controlAlpha;
(dn_tmp_90 * dn_tmp_93); });
(dn_tmp_88 + dn_tmp_89); });double *dn_tmp_94=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_mixCurrent);*dn_tmp_94=dn_tmp_95; }
{ double dn_tmp_103=({ double dn_tmp_96=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_gainCurrent;
double dn_tmp_97=({ double dn_tmp_98=({ double dn_tmp_99=v_gainTarget;
double dn_tmp_100=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_gainCurrent;
(dn_tmp_99 - dn_tmp_100); });
double dn_tmp_101=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_controlAlpha;
(dn_tmp_98 * dn_tmp_101); });
(dn_tmp_96 + dn_tmp_97); });double *dn_tmp_102=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_gainCurrent);*dn_tmp_102=dn_tmp_103; }
{ double dn_tmp_115=({ double dn_tmp_104=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_characterCurrent;
double dn_tmp_105=({ double dn_tmp_106=({ double dn_tmp_107=({ double dn_tmp_108=({ float dn_tmp_109=v_character;
(double)dn_tmp_109; });
double dn_tmp_110=((double)(0.0));
double dn_tmp_111=((double)(1.0));
dn_f_min_f(dn_tmp_108,dn_tmp_110,dn_tmp_111); });
double dn_tmp_112=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_characterCurrent;
(dn_tmp_107 - dn_tmp_112); });
double dn_tmp_113=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_controlAlpha;
(dn_tmp_106 * dn_tmp_113); });
(dn_tmp_104 + dn_tmp_105); });double *dn_tmp_114=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_characterCurrent);*dn_tmp_114=dn_tmp_115; }
{ double dn_tmp_127=({ double dn_tmp_116=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pulseColorCurrent;
double dn_tmp_117=({ double dn_tmp_118=({ double dn_tmp_119=({ double dn_tmp_120=({ float dn_tmp_121=v_pulseIntensity;
(double)dn_tmp_121; });
double dn_tmp_122=((double)(0.0));
double dn_tmp_123=((double)(1.0));
dn_f_min_f(dn_tmp_120,dn_tmp_122,dn_tmp_123); });
double dn_tmp_124=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pulseColorCurrent;
(dn_tmp_119 - dn_tmp_124); });
double dn_tmp_125=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_controlAlpha;
(dn_tmp_118 * dn_tmp_125); });
(dn_tmp_116 + dn_tmp_117); });double *dn_tmp_126=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pulseColorCurrent);*dn_tmp_126=dn_tmp_127; }
{ double dn_tmp_135=({ double dn_tmp_128=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossLowL;
double dn_tmp_129=({ double dn_tmp_130=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossAlpha;
double dn_tmp_131=({ double dn_tmp_132=v_l;
double dn_tmp_133=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossLowL;
(dn_tmp_132 - dn_tmp_133); });
(dn_tmp_130 * dn_tmp_131); });
(dn_tmp_128 + dn_tmp_129); });double *dn_tmp_134=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossLowL);*dn_tmp_134=dn_tmp_135; }
{ double dn_tmp_143=({ double dn_tmp_136=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossLowR;
double dn_tmp_137=({ double dn_tmp_138=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossAlpha;
double dn_tmp_139=({ double dn_tmp_140=v_r;
double dn_tmp_141=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossLowR;
(dn_tmp_140 - dn_tmp_141); });
(dn_tmp_138 * dn_tmp_139); });
(dn_tmp_136 + dn_tmp_137); });double *dn_tmp_142=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossLowR);*dn_tmp_142=dn_tmp_143; }
size_t v_read=({ size_t dn_tmp_144=({ size_t dn_tmp_145=({ size_t dn_tmp_146=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossIndex;
size_t dn_tmp_147=((size_t)(64ULL));
dn_USize_add(dn_tmp_146,dn_tmp_147); });
size_t dn_tmp_148=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossSamples;
dn_USize_sub(dn_tmp_145,dn_tmp_148); });
size_t dn_tmp_149=((size_t)(64ULL));
dn_USize_mod(dn_tmp_144,dn_tmp_149); });
double v_a=(*({ dn_t_arr64_F64 *dn_tmp_150=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_delayL);size_t dn_tmp_151=v_read;&dn_tmp_150->data[dn_index(dn_tmp_151,64)]; }));
double v_b=(*({ dn_t_arr64_F64 *dn_tmp_152=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_delayR);size_t dn_tmp_153=v_read;&dn_tmp_152->data[dn_index(dn_tmp_153,64)]; }));
{ double dn_tmp_157=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossLowL;double *dn_tmp_156=&((*({ dn_t_arr64_F64 *dn_tmp_154=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_delayL);size_t dn_tmp_155=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossIndex;&dn_tmp_154->data[dn_index(dn_tmp_155,64)]; })));*dn_tmp_156=dn_tmp_157; }
{ double dn_tmp_161=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossLowR;double *dn_tmp_160=&((*({ dn_t_arr64_F64 *dn_tmp_158=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_delayR);size_t dn_tmp_159=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossIndex;&dn_tmp_158->data[dn_index(dn_tmp_159,64)]; })));*dn_tmp_160=dn_tmp_161; }
{ size_t dn_tmp_167=({ size_t dn_tmp_162=({ size_t dn_tmp_163=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossIndex;
size_t dn_tmp_164=((size_t)(1ULL));
dn_USize_add(dn_tmp_163,dn_tmp_164); });
size_t dn_tmp_165=((size_t)(64ULL));
dn_USize_mod(dn_tmp_162,dn_tmp_165); });size_t *dn_tmp_166=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossIndex);*dn_tmp_166=dn_tmp_167; }
double v_feed=({ double dn_tmp_168=((double)(0.28));
double dn_tmp_169=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_crossCurrent;
(dn_tmp_168 * dn_tmp_169); });
double v_norm=({ double dn_tmp_170=((double)(1.0));
double dn_tmp_171=({ double dn_tmp_172=((double)(1.0));
double dn_tmp_173=v_feed;
(dn_tmp_172 + dn_tmp_173); });
(dn_tmp_170 / dn_tmp_171); });
{ double dn_tmp_181=({ double dn_tmp_174=({ double dn_tmp_175=v_l;
double dn_tmp_176=({ double dn_tmp_177=v_feed;
double dn_tmp_178=v_b;
(dn_tmp_177 * dn_tmp_178); });
(dn_tmp_175 + dn_tmp_176); });
double dn_tmp_179=v_norm;
(dn_tmp_174 * dn_tmp_179); });double *dn_tmp_180=&(v_l);*dn_tmp_180=dn_tmp_181; }
{ double dn_tmp_189=({ double dn_tmp_182=({ double dn_tmp_183=v_r;
double dn_tmp_184=({ double dn_tmp_185=v_feed;
double dn_tmp_186=v_a;
(dn_tmp_185 * dn_tmp_186); });
(dn_tmp_183 + dn_tmp_184); });
double dn_tmp_187=v_norm;
(dn_tmp_182 * dn_tmp_187); });double *dn_tmp_188=&(v_r);*dn_tmp_188=dn_tmp_189; }
double v_fastL=((double)(0.0));
double v_fastR=((double)(0.0));
if(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedHistoryActive){
(void)(({ dn_r_Folded * dn_tmp_190=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_folded));
double dn_tmp_191=v_l;
double dn_tmp_192=v_r;
dn_f_folded_push(dn_tmp_190,dn_tmp_191,dn_tmp_192); }));
if(({ size_t dn_tmp_193=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedPriming;
size_t dn_tmp_194=((size_t)(384ULL));
(dn_tmp_193 < dn_tmp_194); })){
{ size_t dn_tmp_198=({ size_t dn_tmp_195=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedPriming;
size_t dn_tmp_196=((size_t)(1ULL));
dn_USize_add(dn_tmp_195,dn_tmp_196); });size_t *dn_tmp_197=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedPriming);*dn_tmp_197=dn_tmp_198; }
}
else{
}
if(({ size_t dn_tmp_199=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedPriming;
size_t dn_tmp_200=((size_t)(384ULL));
(dn_tmp_199 >= dn_tmp_200); })){
{ double dn_tmp_205=({ dn_r_Folded const * dn_tmp_201=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_folded));
size_t dn_tmp_202=((size_t)(0ULL));
size_t dn_tmp_203=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedDepth;
dn_f_folded_eval(dn_tmp_201,dn_tmp_202,dn_tmp_203); });double *dn_tmp_204=&(v_fastL);*dn_tmp_204=dn_tmp_205; }
{ double dn_tmp_210=({ dn_r_Folded const * dn_tmp_206=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_folded));
size_t dn_tmp_207=((size_t)(1ULL));
size_t dn_tmp_208=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedDepth;
dn_f_folded_eval(dn_tmp_206,dn_tmp_207,dn_tmp_208); });double *dn_tmp_209=&(v_fastR);*dn_tmp_209=dn_tmp_210; }
}
else{
}
}
else{
}
double v_wl=((double)(0.0));
double v_wr=((double)(0.0));
if(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_legacyRunning){
{ double dn_tmp_215=({ dn_r_Engine * dn_tmp_211=(&((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))));
double dn_tmp_212=v_l;
size_t dn_tmp_213=((size_t)(0ULL));
dn_f_channel_sample(dn_tmp_211,dn_tmp_212,dn_tmp_213); });double *dn_tmp_214=&(v_wl);*dn_tmp_214=dn_tmp_215; }
{ double dn_tmp_220=({ dn_r_Engine * dn_tmp_216=(&((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))));
double dn_tmp_217=v_r;
size_t dn_tmp_218=((size_t)(1ULL));
dn_f_channel_sample(dn_tmp_216,dn_tmp_217,dn_tmp_218); });double *dn_tmp_219=&(v_wr);*dn_tmp_219=dn_tmp_220; }
}
else{
}
if(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_legacyRunning){
size_t v_compensation=({ size_t dn_tmp_221=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_depth;
dn_f_folded_compensation(dn_tmp_221); });
size_t v_ar=({ size_t dn_tmp_222=({ size_t dn_tmp_223=({ size_t dn_tmp_224=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_alignIndex;
size_t dn_tmp_225=((size_t)(63ULL));
dn_USize_add(dn_tmp_224,dn_tmp_225); });
size_t dn_tmp_226=v_compensation;
dn_USize_sub(dn_tmp_223,dn_tmp_226); });
size_t dn_tmp_227=((size_t)(63ULL));
dn_USize_mod(dn_tmp_222,dn_tmp_227); });
double v_alignedL=(*({ dn_t_arr63_F64 *dn_tmp_228=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_alignedL);size_t dn_tmp_229=v_ar;&dn_tmp_228->data[dn_index(dn_tmp_229,63)]; }));
double v_alignedR=(*({ dn_t_arr63_F64 *dn_tmp_230=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_alignedR);size_t dn_tmp_231=v_ar;&dn_tmp_230->data[dn_index(dn_tmp_231,63)]; }));
{ double dn_tmp_235=v_wl;double *dn_tmp_234=&((*({ dn_t_arr63_F64 *dn_tmp_232=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_alignedL);size_t dn_tmp_233=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_alignIndex;&dn_tmp_232->data[dn_index(dn_tmp_233,63)]; })));*dn_tmp_234=dn_tmp_235; }
{ double dn_tmp_239=v_wr;double *dn_tmp_238=&((*({ dn_t_arr63_F64 *dn_tmp_236=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_alignedR);size_t dn_tmp_237=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_alignIndex;&dn_tmp_236->data[dn_index(dn_tmp_237,63)]; })));*dn_tmp_238=dn_tmp_239; }
{ size_t dn_tmp_245=({ size_t dn_tmp_240=({ size_t dn_tmp_241=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_alignIndex;
size_t dn_tmp_242=((size_t)(1ULL));
dn_USize_add(dn_tmp_241,dn_tmp_242); });
size_t dn_tmp_243=((size_t)(63ULL));
dn_USize_mod(dn_tmp_240,dn_tmp_243); });size_t *dn_tmp_244=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_alignIndex);*dn_tmp_244=dn_tmp_245; }
{ double dn_tmp_247=v_alignedL;double *dn_tmp_246=&(v_wl);*dn_tmp_246=dn_tmp_247; }
{ double dn_tmp_249=v_alignedR;double *dn_tmp_248=&(v_wr);*dn_tmp_248=dn_tmp_249; }
if(({ size_t dn_tmp_250=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_legacyPriming;
size_t dn_tmp_251=((size_t)(384ULL));
(dn_tmp_250 < dn_tmp_251); })){
{ size_t dn_tmp_255=({ size_t dn_tmp_252=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_legacyPriming;
size_t dn_tmp_253=((size_t)(1ULL));
dn_USize_add(dn_tmp_252,dn_tmp_253); });size_t *dn_tmp_254=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_legacyPriming);*dn_tmp_254=dn_tmp_255; }
}
else{
}
}
else{
}
bool v_fastReady=(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedHistoryActive && ({ size_t dn_tmp_256=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedPriming;
size_t dn_tmp_257=((size_t)(384ULL));
(dn_tmp_256 >= dn_tmp_257); }));
if((v_wantFast && v_fastReady)){
{ double dn_tmp_266=({ double dn_tmp_258=({ double dn_tmp_259=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_fastWeight;
double dn_tmp_260=({ double dn_tmp_261=((double)(1.0));
double dn_tmp_262=((double)(128.0));
(dn_tmp_261 / dn_tmp_262); });
(dn_tmp_259 + dn_tmp_260); });
double dn_tmp_263=((double)(0.0));
double dn_tmp_264=((double)(1.0));
dn_f_min_f(dn_tmp_258,dn_tmp_263,dn_tmp_264); });double *dn_tmp_265=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_fastWeight);*dn_tmp_265=dn_tmp_266; }
}
else{
if(((!(v_wantFast)) && ({ size_t dn_tmp_267=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_legacyPriming;
size_t dn_tmp_268=((size_t)(384ULL));
(dn_tmp_267 >= dn_tmp_268); }))){
{ double dn_tmp_277=({ double dn_tmp_269=({ double dn_tmp_270=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_fastWeight;
double dn_tmp_271=({ double dn_tmp_272=((double)(1.0));
double dn_tmp_273=((double)(128.0));
(dn_tmp_272 / dn_tmp_273); });
(dn_tmp_270 - dn_tmp_271); });
double dn_tmp_274=((double)(0.0));
double dn_tmp_275=((double)(1.0));
dn_f_min_f(dn_tmp_269,dn_tmp_274,dn_tmp_275); });double *dn_tmp_276=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_fastWeight);*dn_tmp_276=dn_tmp_277; }
}
else{
}
}
if(({ double dn_tmp_278=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_fastWeight;
double dn_tmp_279=((double)(0.0));
(dn_tmp_278 > dn_tmp_279); })){
{ double dn_tmp_289=({ double dn_tmp_280=({ double dn_tmp_281=({ double dn_tmp_282=((double)(1.0));
double dn_tmp_283=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_fastWeight;
(dn_tmp_282 - dn_tmp_283); });
double dn_tmp_284=v_wl;
(dn_tmp_281 * dn_tmp_284); });
double dn_tmp_285=({ double dn_tmp_286=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_fastWeight;
double dn_tmp_287=v_fastL;
(dn_tmp_286 * dn_tmp_287); });
(dn_tmp_280 + dn_tmp_285); });double *dn_tmp_288=&(v_wl);*dn_tmp_288=dn_tmp_289; }
{ double dn_tmp_299=({ double dn_tmp_290=({ double dn_tmp_291=({ double dn_tmp_292=((double)(1.0));
double dn_tmp_293=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_fastWeight;
(dn_tmp_292 - dn_tmp_293); });
double dn_tmp_294=v_wr;
(dn_tmp_291 * dn_tmp_294); });
double dn_tmp_295=({ double dn_tmp_296=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_fastWeight;
double dn_tmp_297=v_fastR;
(dn_tmp_296 * dn_tmp_297); });
(dn_tmp_290 + dn_tmp_295); });double *dn_tmp_298=&(v_wr);*dn_tmp_298=dn_tmp_299; }
}
else{
}
if((v_wantFast && ({ double dn_tmp_300=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_fastWeight;
double dn_tmp_301=((double)(1.0));
(dn_tmp_300 >= dn_tmp_301); }))){
{ bool dn_tmp_303=0;bool *dn_tmp_302=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_legacyRunning);*dn_tmp_302=dn_tmp_303; }
{ size_t dn_tmp_305=((size_t)(0ULL));size_t *dn_tmp_304=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_legacyPriming);*dn_tmp_304=dn_tmp_305; }
}
else{
}
if((((!(v_wantFast)) && ({ double dn_tmp_306=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_fastWeight;
double dn_tmp_307=((double)(0.0));
(dn_tmp_306 == dn_tmp_307); })) && ((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedHistoryActive)){
{ bool dn_tmp_309=0;bool *dn_tmp_308=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedHistoryActive);*dn_tmp_308=dn_tmp_309; }
{ size_t dn_tmp_311=((size_t)(0ULL));size_t *dn_tmp_310=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_foldedPriming);*dn_tmp_310=dn_tmp_311; }
}
else{
}
uint32_t v_m=v_mode;
if(({ uint32_t dn_tmp_312=v_m;
uint32_t dn_tmp_313=({ uint32_t dn_tmp_314=((uint32_t)(3ULL));
if((unsigned __int128)dn_tmp_314>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_314; });
(dn_tmp_312 > dn_tmp_313); })){
{ uint32_t dn_tmp_317=({ uint32_t dn_tmp_315=((uint32_t)(3ULL));
if((unsigned __int128)dn_tmp_315>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_315; });uint32_t *dn_tmp_316=&(v_m);*dn_tmp_316=dn_tmp_317; }
}
else{
}
double v_warmTarget=((double)(0.0));
if(({ uint32_t dn_tmp_318=v_m;
uint32_t dn_tmp_319=({ uint32_t dn_tmp_320=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_320>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_320; });
(dn_tmp_318 >= dn_tmp_319); })){
{ double dn_tmp_322=((double)(1.0));double *dn_tmp_321=&(v_warmTarget);*dn_tmp_321=dn_tmp_322; }
}
else{
}
double v_hfTarget=((double)(0.0));
if((({ uint32_t dn_tmp_323=v_m;
uint32_t dn_tmp_324=({ uint32_t dn_tmp_325=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_325>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_325; });
(dn_tmp_323 == dn_tmp_324); }) || ({ uint32_t dn_tmp_326=v_m;
uint32_t dn_tmp_327=({ uint32_t dn_tmp_328=((uint32_t)(3ULL));
if((unsigned __int128)dn_tmp_328>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_328; });
(dn_tmp_326 == dn_tmp_327); }))){
{ double dn_tmp_330=((double)(1.0));double *dn_tmp_329=&(v_hfTarget);*dn_tmp_329=dn_tmp_330; }
}
else{
}
{ double dn_tmp_338=({ double dn_tmp_331=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_warmMix;
double dn_tmp_332=({ double dn_tmp_333=({ double dn_tmp_334=v_warmTarget;
double dn_tmp_335=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_warmMix;
(dn_tmp_334 - dn_tmp_335); });
double dn_tmp_336=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_controlAlpha;
(dn_tmp_333 * dn_tmp_336); });
(dn_tmp_331 + dn_tmp_332); });double *dn_tmp_337=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_warmMix);*dn_tmp_337=dn_tmp_338; }
{ double dn_tmp_346=({ double dn_tmp_339=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_hfMix;
double dn_tmp_340=({ double dn_tmp_341=({ double dn_tmp_342=v_hfTarget;
double dn_tmp_343=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_hfMix;
(dn_tmp_342 - dn_tmp_343); });
double dn_tmp_344=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_controlAlpha;
(dn_tmp_341 * dn_tmp_344); });
(dn_tmp_339 + dn_tmp_340); });double *dn_tmp_345=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_hfMix);*dn_tmp_345=dn_tmp_346; }
{ double dn_tmp_354=({ double dn_tmp_347=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_warmLowL;
double dn_tmp_348=({ double dn_tmp_349=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_warmAlpha;
double dn_tmp_350=({ double dn_tmp_351=v_wl;
double dn_tmp_352=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_warmLowL;
(dn_tmp_351 - dn_tmp_352); });
(dn_tmp_349 * dn_tmp_350); });
(dn_tmp_347 + dn_tmp_348); });double *dn_tmp_353=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_warmLowL);*dn_tmp_353=dn_tmp_354; }
{ double dn_tmp_362=({ double dn_tmp_355=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_warmLowR;
double dn_tmp_356=({ double dn_tmp_357=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_warmAlpha;
double dn_tmp_358=({ double dn_tmp_359=v_wr;
double dn_tmp_360=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_warmLowR;
(dn_tmp_359 - dn_tmp_360); });
(dn_tmp_357 * dn_tmp_358); });
(dn_tmp_355 + dn_tmp_356); });double *dn_tmp_361=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_warmLowR);*dn_tmp_361=dn_tmp_362; }
double v_warmL=({ double dn_tmp_363=v_wl;
double dn_tmp_364=({ double dn_tmp_365=({ double dn_tmp_366=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_warmLowL;
double dn_tmp_367=v_wl;
(dn_tmp_366 - dn_tmp_367); });
double dn_tmp_368=({ double dn_tmp_369=((double)(0.18));
double dn_tmp_370=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_warmMix;
(dn_tmp_369 * dn_tmp_370); });
(dn_tmp_365 * dn_tmp_368); });
(dn_tmp_363 + dn_tmp_364); });
double v_warmR=({ double dn_tmp_371=v_wr;
double dn_tmp_372=({ double dn_tmp_373=({ double dn_tmp_374=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_warmLowR;
double dn_tmp_375=v_wr;
(dn_tmp_374 - dn_tmp_375); });
double dn_tmp_376=({ double dn_tmp_377=((double)(0.18));
double dn_tmp_378=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_warmMix;
(dn_tmp_377 * dn_tmp_378); });
(dn_tmp_373 * dn_tmp_376); });
(dn_tmp_371 + dn_tmp_372); });
double v_outputL=v_warmL;
double v_outputR=v_warmR;
if(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_hfAvailable){
double v_filteredL=({ dn_r_Biquad * dn_tmp_379=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_hfL));
double dn_tmp_380=v_warmL;
dn_f_biquad_process(dn_tmp_379,dn_tmp_380); });
double v_filteredR=({ dn_r_Biquad * dn_tmp_381=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_hfR));
double dn_tmp_382=v_warmR;
dn_f_biquad_process(dn_tmp_381,dn_tmp_382); });
{ double dn_tmp_390=({ double dn_tmp_383=v_warmL;
double dn_tmp_384=({ double dn_tmp_385=({ double dn_tmp_386=v_filteredL;
double dn_tmp_387=v_warmL;
(dn_tmp_386 - dn_tmp_387); });
double dn_tmp_388=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_hfMix;
(dn_tmp_385 * dn_tmp_388); });
(dn_tmp_383 + dn_tmp_384); });double *dn_tmp_389=&(v_outputL);*dn_tmp_389=dn_tmp_390; }
{ double dn_tmp_398=({ double dn_tmp_391=v_warmR;
double dn_tmp_392=({ double dn_tmp_393=({ double dn_tmp_394=v_filteredR;
double dn_tmp_395=v_warmR;
(dn_tmp_394 - dn_tmp_395); });
double dn_tmp_396=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_hfMix;
(dn_tmp_393 * dn_tmp_396); });
(dn_tmp_391 + dn_tmp_392); });double *dn_tmp_397=&(v_outputR);*dn_tmp_397=dn_tmp_398; }
}
else{
}
{ double dn_tmp_416=({ double dn_tmp_401=(*({ dn_t_arr3_F64 *dn_tmp_402=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative).f_amounts);size_t dn_tmp_403=((size_t)(0ULL));&dn_tmp_402->data[dn_index(dn_tmp_403,3)]; }));
double dn_tmp_404=({ double dn_tmp_405=({ double dn_tmp_406=({ double dn_tmp_407=({ float dn_tmp_408=v_focus;
(double)dn_tmp_408; });
double dn_tmp_409=((double)(0.0));
double dn_tmp_410=((double)(1.0));
dn_f_min_f(dn_tmp_407,dn_tmp_409,dn_tmp_410); });
double dn_tmp_411=(*({ dn_t_arr3_F64 *dn_tmp_412=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative).f_amounts);size_t dn_tmp_413=((size_t)(0ULL));&dn_tmp_412->data[dn_index(dn_tmp_413,3)]; }));
(dn_tmp_406 - dn_tmp_411); });
double dn_tmp_414=(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative).f_smoothing;
(dn_tmp_405 * dn_tmp_414); });
(dn_tmp_401 + dn_tmp_404); });double *dn_tmp_415=&((*({ dn_t_arr3_F64 *dn_tmp_399=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative).f_amounts);size_t dn_tmp_400=((size_t)(0ULL));&dn_tmp_399->data[dn_index(dn_tmp_400,3)]; })));*dn_tmp_415=dn_tmp_416; }
{ double dn_tmp_434=({ double dn_tmp_419=(*({ dn_t_arr3_F64 *dn_tmp_420=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative).f_amounts);size_t dn_tmp_421=((size_t)(1ULL));&dn_tmp_420->data[dn_index(dn_tmp_421,3)]; }));
double dn_tmp_422=({ double dn_tmp_423=({ double dn_tmp_424=({ double dn_tmp_425=({ float dn_tmp_426=v_phase;
(double)dn_tmp_426; });
double dn_tmp_427=((double)(0.0));
double dn_tmp_428=((double)(1.0));
dn_f_min_f(dn_tmp_425,dn_tmp_427,dn_tmp_428); });
double dn_tmp_429=(*({ dn_t_arr3_F64 *dn_tmp_430=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative).f_amounts);size_t dn_tmp_431=((size_t)(1ULL));&dn_tmp_430->data[dn_index(dn_tmp_431,3)]; }));
(dn_tmp_424 - dn_tmp_429); });
double dn_tmp_432=(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative).f_smoothing;
(dn_tmp_423 * dn_tmp_432); });
(dn_tmp_419 + dn_tmp_422); });double *dn_tmp_433=&((*({ dn_t_arr3_F64 *dn_tmp_417=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative).f_amounts);size_t dn_tmp_418=((size_t)(1ULL));&dn_tmp_417->data[dn_index(dn_tmp_418,3)]; })));*dn_tmp_433=dn_tmp_434; }
{ double dn_tmp_452=({ double dn_tmp_437=(*({ dn_t_arr3_F64 *dn_tmp_438=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative).f_amounts);size_t dn_tmp_439=((size_t)(2ULL));&dn_tmp_438->data[dn_index(dn_tmp_439,3)]; }));
double dn_tmp_440=({ double dn_tmp_441=({ double dn_tmp_442=({ double dn_tmp_443=({ float dn_tmp_444=v_tone;
(double)dn_tmp_444; });
double dn_tmp_445=((double)(0.0));
double dn_tmp_446=((double)(1.0));
dn_f_min_f(dn_tmp_443,dn_tmp_445,dn_tmp_446); });
double dn_tmp_447=(*({ dn_t_arr3_F64 *dn_tmp_448=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative).f_amounts);size_t dn_tmp_449=((size_t)(2ULL));&dn_tmp_448->data[dn_index(dn_tmp_449,3)]; }));
(dn_tmp_442 - dn_tmp_447); });
double dn_tmp_450=(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative).f_smoothing;
(dn_tmp_441 * dn_tmp_450); });
(dn_tmp_437 + dn_tmp_440); });double *dn_tmp_451=&((*({ dn_t_arr3_F64 *dn_tmp_435=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative).f_amounts);size_t dn_tmp_436=((size_t)(2ULL));&dn_tmp_435->data[dn_index(dn_tmp_436,3)]; })));*dn_tmp_451=dn_tmp_452; }
{ double dn_tmp_459=({ dn_r_Creative * dn_tmp_453=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative));
double dn_tmp_454=v_outputL;
size_t dn_tmp_455=((size_t)(0ULL));
uint32_t dn_tmp_456=v_phaseMode;
uint32_t dn_tmp_457=v_m;
dn_f_creative_channel(dn_tmp_453,dn_tmp_454,dn_tmp_455,dn_tmp_456,dn_tmp_457); });double *dn_tmp_458=&(v_outputL);*dn_tmp_458=dn_tmp_459; }
{ double dn_tmp_466=({ dn_r_Creative * dn_tmp_460=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_creative));
double dn_tmp_461=v_outputR;
size_t dn_tmp_462=((size_t)(1ULL));
uint32_t dn_tmp_463=v_phaseMode;
uint32_t dn_tmp_464=v_m;
dn_f_creative_channel(dn_tmp_460,dn_tmp_461,dn_tmp_462,dn_tmp_463,dn_tmp_464); });double *dn_tmp_465=&(v_outputR);*dn_tmp_465=dn_tmp_466; }
double v_transitionGain=((double)(1.0));
if(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_transitioning){
size_t v_counter=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_transitionCounter;
if(({ size_t dn_tmp_467=v_counter;
size_t dn_tmp_468=((size_t)(128ULL));
(dn_tmp_467 < dn_tmp_468); })){
{ double dn_tmp_475=({ double dn_tmp_469=((double)(1.0));
double dn_tmp_470=({ double dn_tmp_471=({ size_t dn_tmp_472=v_counter;
(double)dn_tmp_472; });
double dn_tmp_473=((double)(128.0));
(dn_tmp_471 / dn_tmp_473); });
(dn_tmp_469 - dn_tmp_470); });double *dn_tmp_474=&(v_transitionGain);*dn_tmp_474=dn_tmp_475; }
}
else{
{ double dn_tmp_482=({ double dn_tmp_476=({ size_t dn_tmp_477=({ size_t dn_tmp_478=v_counter;
size_t dn_tmp_479=((size_t)(128ULL));
dn_USize_sub(dn_tmp_478,dn_tmp_479); });
(double)dn_tmp_477; });
double dn_tmp_480=((double)(128.0));
(dn_tmp_476 / dn_tmp_480); });double *dn_tmp_481=&(v_transitionGain);*dn_tmp_481=dn_tmp_482; }
}
{ size_t dn_tmp_486=({ size_t dn_tmp_483=v_counter;
size_t dn_tmp_484=((size_t)(1ULL));
dn_USize_add(dn_tmp_483,dn_tmp_484); });size_t *dn_tmp_485=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_transitionCounter);*dn_tmp_485=dn_tmp_486; }
if(({ size_t dn_tmp_487=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_transitionCounter;
size_t dn_tmp_488=((size_t)(128ULL));
(dn_tmp_487 == dn_tmp_488); })){
if(({ size_t dn_tmp_489=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_depth;
size_t dn_tmp_490=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pendingDepth;
(dn_tmp_489 != dn_tmp_490); })){
{ size_t dn_tmp_492=((size_t)(2ULL));size_t dn_tmp_491=((size_t)(12ULL));for(size_t v_j=dn_tmp_492;v_j<dn_tmp_491;v_j++){
(void)(({ dn_r_Fir * dn_tmp_493=(&((*({ dn_t_buf_r3_Fir volatile *dn_tmp_494=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_up);size_t dn_tmp_495=v_j;&dn_tmp_494->data[dn_index(dn_tmp_495,dn_tmp_494->len)]; }))));
dn_f_fir_reset(dn_tmp_493); }));
(void)(({ dn_r_Fir * dn_tmp_496=(&((*({ dn_t_buf_r3_Fir volatile *dn_tmp_497=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_down);size_t dn_tmp_498=v_j;&dn_tmp_497->data[dn_index(dn_tmp_498,dn_tmp_497->len)]; }))));
dn_f_fir_reset(dn_tmp_496); }));
}
 }
{ size_t dn_tmp_500=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pendingDepth;size_t *dn_tmp_499=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_depth);*dn_tmp_499=dn_tmp_500; }
{ size_t dn_tmp_502=((size_t)(0ULL));size_t dn_tmp_501=((size_t)(63ULL));for(size_t v_j=dn_tmp_502;v_j<dn_tmp_501;v_j++){
{ double dn_tmp_506=((double)(0.0));double *dn_tmp_505=&((*({ dn_t_arr63_F64 *dn_tmp_503=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_alignedL);size_t dn_tmp_504=v_j;&dn_tmp_503->data[dn_index(dn_tmp_504,63)]; })));*dn_tmp_505=dn_tmp_506; }
{ double dn_tmp_510=((double)(0.0));double *dn_tmp_509=&((*({ dn_t_arr63_F64 *dn_tmp_507=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_alignedR);size_t dn_tmp_508=v_j;&dn_tmp_507->data[dn_index(dn_tmp_508,63)]; })));*dn_tmp_509=dn_tmp_510; }
}
 }
{ size_t dn_tmp_512=((size_t)(0ULL));size_t *dn_tmp_511=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_alignIndex);*dn_tmp_511=dn_tmp_512; }
}
else{
}
if(({ size_t dn_tmp_513=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pulseMode;
size_t dn_tmp_514=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pendingPulse;
(dn_tmp_513 != dn_tmp_514); })){
{ dn_r_Pulse dn_tmp_520=({ size_t dn_tmp_517=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pendingPulse;
size_t dn_tmp_518=((size_t)(0ULL));
dn_f_pulse_create(dn_tmp_517,dn_tmp_518); });dn_r_Pulse *dn_tmp_519=&((*({ dn_t_arr2_r5_Pulse *dn_tmp_515=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pulses);size_t dn_tmp_516=((size_t)(0ULL));&dn_tmp_515->data[dn_index(dn_tmp_516,2)]; })));*dn_tmp_519=dn_tmp_520; }
{ dn_r_Pulse dn_tmp_526=({ size_t dn_tmp_523=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pendingPulse;
size_t dn_tmp_524=((size_t)(1ULL));
dn_f_pulse_create(dn_tmp_523,dn_tmp_524); });dn_r_Pulse *dn_tmp_525=&((*({ dn_t_arr2_r5_Pulse *dn_tmp_521=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pulses);size_t dn_tmp_522=((size_t)(1ULL));&dn_tmp_521->data[dn_index(dn_tmp_522,2)]; })));*dn_tmp_525=dn_tmp_526; }
{ size_t dn_tmp_528=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pendingPulse;size_t *dn_tmp_527=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pulseMode);*dn_tmp_527=dn_tmp_528; }
}
else{
}
if(({ size_t dn_tmp_529=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dacMode;
size_t dn_tmp_530=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pendingDac;
(dn_tmp_529 != dn_tmp_530); })){
{ dn_r_Dac dn_tmp_536=({ size_t dn_tmp_533=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pendingDac;
size_t dn_tmp_534=((size_t)(0ULL));
dn_f_dac_create(dn_tmp_533,dn_tmp_534); });dn_r_Dac *dn_tmp_535=&((*({ dn_t_arr2_r3_Dac *dn_tmp_531=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dacs);size_t dn_tmp_532=((size_t)(0ULL));&dn_tmp_531->data[dn_index(dn_tmp_532,2)]; })));*dn_tmp_535=dn_tmp_536; }
{ dn_r_Dac dn_tmp_542=({ size_t dn_tmp_539=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pendingDac;
size_t dn_tmp_540=((size_t)(1ULL));
dn_f_dac_create(dn_tmp_539,dn_tmp_540); });dn_r_Dac *dn_tmp_541=&((*({ dn_t_arr2_r3_Dac *dn_tmp_537=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dacs);size_t dn_tmp_538=((size_t)(1ULL));&dn_tmp_537->data[dn_index(dn_tmp_538,2)]; })));*dn_tmp_541=dn_tmp_542; }
{ size_t dn_tmp_544=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_pendingDac;size_t *dn_tmp_543=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dacMode);*dn_tmp_543=dn_tmp_544; }
}
else{
}
}
else{
}
if(({ size_t dn_tmp_545=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_transitionCounter;
size_t dn_tmp_546=((size_t)(256ULL));
(dn_tmp_545 >= dn_tmp_546); })){
{ bool dn_tmp_548=0;bool *dn_tmp_547=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_transitioning);*dn_tmp_547=dn_tmp_548; }
{ size_t dn_tmp_550=((size_t)(0ULL));size_t *dn_tmp_549=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_transitionCounter);*dn_tmp_549=dn_tmp_550; }
}
else{
}
}
else{
}
double v_dryL=(*({ dn_t_arr157_F64 *dn_tmp_551=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dryL);size_t dn_tmp_552=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dryIndex;&dn_tmp_551->data[dn_index(dn_tmp_552,157)]; }));
double v_dryR=(*({ dn_t_arr157_F64 *dn_tmp_553=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dryR);size_t dn_tmp_554=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dryIndex;&dn_tmp_553->data[dn_index(dn_tmp_554,157)]; }));
{ double dn_tmp_558=v_inputL;double *dn_tmp_557=&((*({ dn_t_arr157_F64 *dn_tmp_555=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dryL);size_t dn_tmp_556=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dryIndex;&dn_tmp_555->data[dn_index(dn_tmp_556,157)]; })));*dn_tmp_557=dn_tmp_558; }
{ double dn_tmp_562=v_inputR;double *dn_tmp_561=&((*({ dn_t_arr157_F64 *dn_tmp_559=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dryR);size_t dn_tmp_560=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dryIndex;&dn_tmp_559->data[dn_index(dn_tmp_560,157)]; })));*dn_tmp_561=dn_tmp_562; }
{ size_t dn_tmp_568=({ size_t dn_tmp_563=({ size_t dn_tmp_564=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dryIndex;
size_t dn_tmp_565=((size_t)(1ULL));
dn_USize_add(dn_tmp_564,dn_tmp_565); });
size_t dn_tmp_566=((size_t)(157ULL));
dn_USize_mod(dn_tmp_563,dn_tmp_566); });size_t *dn_tmp_567=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_dryIndex);*dn_tmp_567=dn_tmp_568; }
{ float dn_tmp_585=({ double dn_tmp_571=({ double dn_tmp_572=({ double dn_tmp_573=({ double dn_tmp_574=v_dryL;
double dn_tmp_575=({ double dn_tmp_576=((double)(1.0));
double dn_tmp_577=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_mixCurrent;
(dn_tmp_576 - dn_tmp_577); });
(dn_tmp_574 * dn_tmp_575); });
double dn_tmp_578=({ double dn_tmp_579=({ double dn_tmp_580=v_outputL;
double dn_tmp_581=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_mixCurrent;
(dn_tmp_580 * dn_tmp_581); });
double dn_tmp_582=v_transitionGain;
(dn_tmp_579 * dn_tmp_582); });
(dn_tmp_573 + dn_tmp_578); });
double dn_tmp_583=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_gainCurrent;
(dn_tmp_572 * dn_tmp_583); });
(float)dn_tmp_571; });float *dn_tmp_584=&((*({ dn_t_foreign_F32 *dn_tmp_569=&(v_lbuf);size_t dn_tmp_570=v_i;&dn_tmp_569->data[dn_index(dn_tmp_570,dn_tmp_569->len)]; })));*dn_tmp_584=dn_tmp_585; }
{ float dn_tmp_602=({ double dn_tmp_588=({ double dn_tmp_589=({ double dn_tmp_590=({ double dn_tmp_591=v_dryR;
double dn_tmp_592=({ double dn_tmp_593=((double)(1.0));
double dn_tmp_594=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_mixCurrent;
(dn_tmp_593 - dn_tmp_594); });
(dn_tmp_591 * dn_tmp_592); });
double dn_tmp_595=({ double dn_tmp_596=({ double dn_tmp_597=v_outputR;
double dn_tmp_598=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_mixCurrent;
(dn_tmp_597 * dn_tmp_598); });
double dn_tmp_599=v_transitionGain;
(dn_tmp_596 * dn_tmp_599); });
(dn_tmp_590 + dn_tmp_595); });
double dn_tmp_600=((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_gainCurrent;
(dn_tmp_589 * dn_tmp_600); });
(float)dn_tmp_588; });float *dn_tmp_601=&((*({ dn_t_foreign_F32 *dn_tmp_586=&(v_rbuf);size_t dn_tmp_587=v_i;&dn_tmp_586->data[dn_index(dn_tmp_587,dn_tmp_586->len)]; })));*dn_tmp_601=dn_tmp_602; }
}
 }
{ uint32_t dn_tmp_604=({ uint32_t dn_tmp_603=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_603>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_603; });return dn_tmp_604; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_live_process_full(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode,uint32_t v_phaseMode,float v_focus,float v_phase,float v_tone,float v_character,float v_pulseIntensity) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_process_full(v_h,v_left,v_right,v_frames,v_crossfeed,v_mix,v_gainDb,v_filterMode,v_phaseMode,v_focus,v_phase,v_tone,v_character,v_pulseIntensity);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
DN_EXPORT uint32_t live_process_full(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode,uint32_t v_phaseMode,float v_focus,float v_phase,float v_tone,float v_character,float v_pulseIntensity) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_process_full(v_h,v_left,v_right,v_frames,v_crossfeed,v_mix,v_gainDb,v_filterMode,v_phaseMode,v_focus,v_phase,v_tone,v_character,v_pulseIntensity);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_live_process_final(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode,uint32_t v_phaseMode,float v_focus,float v_phase,float v_tone,float v_character,float v_pulseIntensity,bool v_autoMatch,bool v_limiter,float v_ceilingDb) {
{
dn_t_foreign_F32 v_lb=({ float * dn_tmp_0=v_left;
size_t dn_tmp_1=v_frames;
({ if(dn_tmp_1&&!dn_tmp_0)dn_fail("null foreign buffer");dn_size(dn_tmp_1,sizeof(*dn_tmp_0));(dn_t_foreign_F32){dn_tmp_0,dn_tmp_1}; }); });
dn_t_foreign_F32 v_rb=({ float * dn_tmp_2=v_right;
size_t dn_tmp_3=v_frames;
({ if(dn_tmp_3&&!dn_tmp_2)dn_fail("null foreign buffer");dn_size(dn_tmp_3,sizeof(*dn_tmp_2));(dn_t_foreign_F32){dn_tmp_2,dn_tmp_3}; }); });
double v_inputSquare=((double)(0.0));
{ size_t dn_tmp_5=((size_t)(0ULL));size_t dn_tmp_4=v_frames;for(size_t v_i=dn_tmp_5;v_i<dn_tmp_4;v_i++){
double v_l=({ float dn_tmp_6=(*({ dn_t_foreign_F32 *dn_tmp_7=&(v_lb);size_t dn_tmp_8=v_i;&dn_tmp_7->data[dn_index(dn_tmp_8,dn_tmp_7->len)]; }));
(double)dn_tmp_6; });
double v_r=({ float dn_tmp_9=(*({ dn_t_foreign_F32 *dn_tmp_10=&(v_rb);size_t dn_tmp_11=v_i;&dn_tmp_10->data[dn_index(dn_tmp_11,dn_tmp_10->len)]; }));
(double)dn_tmp_9; });
{ double dn_tmp_23=({ double dn_tmp_12=v_inputSquare;
double dn_tmp_13=({ double dn_tmp_14=((double)(0.5));
double dn_tmp_15=({ double dn_tmp_16=({ double dn_tmp_17=v_l;
double dn_tmp_18=v_l;
(dn_tmp_17 * dn_tmp_18); });
double dn_tmp_19=({ double dn_tmp_20=v_r;
double dn_tmp_21=v_r;
(dn_tmp_20 * dn_tmp_21); });
(dn_tmp_16 + dn_tmp_19); });
(dn_tmp_14 * dn_tmp_15); });
(dn_tmp_12 + dn_tmp_13); });double *dn_tmp_22=&(v_inputSquare);*dn_tmp_22=dn_tmp_23; }
}
 }
if(({ size_t dn_tmp_24=v_frames;
size_t dn_tmp_25=((size_t)(0ULL));
(dn_tmp_24 > dn_tmp_25); })){
{ double dn_tmp_30=({ double dn_tmp_26=v_inputSquare;
double dn_tmp_27=({ size_t dn_tmp_28=v_frames;
(double)dn_tmp_28; });
(dn_tmp_26 / dn_tmp_27); });double *dn_tmp_29=&(v_inputSquare);*dn_tmp_29=dn_tmp_30; }
}
else{
}
uint32_t v_result=({ void * dn_tmp_31=v_h;
float * dn_tmp_32=v_left;
float * dn_tmp_33=v_right;
size_t dn_tmp_34=v_frames;
float dn_tmp_35=v_crossfeed;
float dn_tmp_36=v_mix;
float dn_tmp_37=v_gainDb;
uint32_t dn_tmp_38=v_filterMode;
uint32_t dn_tmp_39=v_phaseMode;
float dn_tmp_40=v_focus;
float dn_tmp_41=v_phase;
float dn_tmp_42=v_tone;
float dn_tmp_43=v_character;
float dn_tmp_44=v_pulseIntensity;
dn_f_live_process_full(dn_tmp_31,dn_tmp_32,dn_tmp_33,dn_tmp_34,dn_tmp_35,dn_tmp_36,dn_tmp_37,dn_tmp_38,dn_tmp_39,dn_tmp_40,dn_tmp_41,dn_tmp_42,dn_tmp_43,dn_tmp_44); });
if(({ uint32_t dn_tmp_45=v_result;
uint32_t dn_tmp_46=({ uint32_t dn_tmp_47=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_47>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_47; });
(dn_tmp_45 != dn_tmp_46); })){
{ uint32_t dn_tmp_48=v_result;return dn_tmp_48; }
}
else{
}
dn_r_Engine * v_e=({ void * dn_tmp_49=v_h;
((dn_r_Engine *)dn_tmp_49); });
(void)(({ dn_r_OutputStage * dn_tmp_50=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_output));
float * dn_tmp_51=v_left;
float * dn_tmp_52=v_right;
size_t dn_tmp_53=v_frames;
double dn_tmp_54=v_inputSquare;
bool dn_tmp_55=v_autoMatch;
bool dn_tmp_56=v_limiter;
double dn_tmp_57=({ float dn_tmp_58=v_ceilingDb;
(double)dn_tmp_58; });
dn_f_output_process(dn_tmp_50,dn_tmp_51,dn_tmp_52,dn_tmp_53,dn_tmp_54,dn_tmp_55,dn_tmp_56,dn_tmp_57); }));
{ uint32_t dn_tmp_60=({ uint32_t dn_tmp_59=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_59>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_59; });return dn_tmp_60; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_live_process_final(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode,uint32_t v_phaseMode,float v_focus,float v_phase,float v_tone,float v_character,float v_pulseIntensity,bool v_autoMatch,bool v_limiter,float v_ceilingDb) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_process_final(v_h,v_left,v_right,v_frames,v_crossfeed,v_mix,v_gainDb,v_filterMode,v_phaseMode,v_focus,v_phase,v_tone,v_character,v_pulseIntensity,v_autoMatch,v_limiter,v_ceilingDb);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
DN_EXPORT uint32_t live_process_final(void * v_h,float * v_left,float * v_right,size_t v_frames,float v_crossfeed,float v_mix,float v_gainDb,uint32_t v_filterMode,uint32_t v_phaseMode,float v_focus,float v_phase,float v_tone,float v_character,float v_pulseIntensity,bool v_autoMatch,bool v_limiter,float v_ceilingDb) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_process_final(v_h,v_left,v_right,v_frames,v_crossfeed,v_mix,v_gainDb,v_filterMode,v_phaseMode,v_focus,v_phase,v_tone,v_character,v_pulseIntensity,v_autoMatch,v_limiter,v_ceilingDb);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_live_tube_config(void * v_h,uint32_t v_typ,uint32_t v_topology,float v_driveDb,float v_bias,float v_sag,float v_wet) {
{
dn_r_Engine * v_e=({ void * dn_tmp_0=v_h;
((dn_r_Engine *)dn_tmp_0); });
(void)(({ dn_r_Tube * dn_tmp_1=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_tube));
size_t dn_tmp_2=({ uint32_t dn_tmp_3=v_typ;
if((unsigned __int128)dn_tmp_3>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_3; });
size_t dn_tmp_4=({ uint32_t dn_tmp_5=v_topology;
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_5; });
double dn_tmp_6=({ float dn_tmp_7=v_driveDb;
(double)dn_tmp_7; });
double dn_tmp_8=({ float dn_tmp_9=v_bias;
(double)dn_tmp_9; });
double dn_tmp_10=({ float dn_tmp_11=v_sag;
(double)dn_tmp_11; });
double dn_tmp_12=({ float dn_tmp_13=v_wet;
(double)dn_tmp_13; });
dn_f_tube_config(dn_tmp_1,dn_tmp_2,dn_tmp_4,dn_tmp_6,dn_tmp_8,dn_tmp_10,dn_tmp_12); }));
{ bool dn_tmp_25=((({ size_t dn_tmp_14=(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_tube).f_desiredType;
size_t dn_tmp_15=((size_t)(0ULL));
(dn_tmp_14 != dn_tmp_15); }) || ({ size_t dn_tmp_16=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_17=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_tube).f_channels);size_t dn_tmp_18=((size_t)(0ULL));&dn_tmp_17->data[dn_index(dn_tmp_18,2)]; }))).f_oldType;
size_t dn_tmp_19=((size_t)(0ULL));
(dn_tmp_16 != dn_tmp_19); })) || ({ double dn_tmp_20=((*({ dn_t_arr2_r12_ValveChannel *dn_tmp_21=&((((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_tube).f_channels);size_t dn_tmp_22=((size_t)(0ULL));&dn_tmp_21->data[dn_index(dn_tmp_22,2)]; }))).f_wet;
double dn_tmp_23=((double)(1.0e-10));
(dn_tmp_20 >= dn_tmp_23); }));bool *dn_tmp_24=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_tubeActive);*dn_tmp_24=dn_tmp_25; }
{ uint32_t dn_tmp_27=({ uint32_t dn_tmp_26=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_26>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_26; });return dn_tmp_27; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_live_tube_config(void * v_h,uint32_t v_typ,uint32_t v_topology,float v_driveDb,float v_bias,float v_sag,float v_wet) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_tube_config(v_h,v_typ,v_topology,v_driveDb,v_bias,v_sag,v_wet);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
DN_EXPORT uint32_t live_tube_config(void * v_h,uint32_t v_typ,uint32_t v_topology,float v_driveDb,float v_bias,float v_sag,float v_wet) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_tube_config(v_h,v_typ,v_topology,v_driveDb,v_bias,v_sag,v_wet);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_live_band_config(void * v_h,uint32_t v_band,uint32_t v_typ,uint32_t v_topology,float v_driveDb,float v_bias,float v_sag,float v_wet) {
{
if(({ uint32_t dn_tmp_0=v_band;
uint32_t dn_tmp_1=({ uint32_t dn_tmp_2=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_2>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_2; });
(dn_tmp_0 > dn_tmp_1); })){
{ uint32_t dn_tmp_4=({ uint32_t dn_tmp_3=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_3>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_3; });return dn_tmp_4; }
}
else{
}
dn_r_Engine * v_e=({ void * dn_tmp_5=v_h;
((dn_r_Engine *)dn_tmp_5); });
(void)(({ dn_r_ThreeBand * dn_tmp_6=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_threeBand));
size_t dn_tmp_7=({ uint32_t dn_tmp_8=v_band;
if((unsigned __int128)dn_tmp_8>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_8; });
size_t dn_tmp_9=({ uint32_t dn_tmp_10=v_typ;
if((unsigned __int128)dn_tmp_10>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_10; });
size_t dn_tmp_11=({ uint32_t dn_tmp_12=v_topology;
if((unsigned __int128)dn_tmp_12>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_12; });
double dn_tmp_13=({ float dn_tmp_14=v_driveDb;
(double)dn_tmp_14; });
double dn_tmp_15=({ float dn_tmp_16=v_bias;
(double)dn_tmp_16; });
double dn_tmp_17=({ float dn_tmp_18=v_sag;
(double)dn_tmp_18; });
double dn_tmp_19=({ float dn_tmp_20=v_wet;
(double)dn_tmp_20; });
dn_f_band_config(dn_tmp_6,dn_tmp_7,dn_tmp_9,dn_tmp_11,dn_tmp_13,dn_tmp_15,dn_tmp_17,dn_tmp_19); }));
{ uint32_t dn_tmp_22=({ uint32_t dn_tmp_21=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_21>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_21; });return dn_tmp_22; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_live_band_config(void * v_h,uint32_t v_band,uint32_t v_typ,uint32_t v_topology,float v_driveDb,float v_bias,float v_sag,float v_wet) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_band_config(v_h,v_band,v_typ,v_topology,v_driveDb,v_bias,v_sag,v_wet);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
DN_EXPORT uint32_t live_band_config(void * v_h,uint32_t v_band,uint32_t v_typ,uint32_t v_topology,float v_driveDb,float v_bias,float v_sag,float v_wet) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_band_config(v_h,v_band,v_typ,v_topology,v_driveDb,v_bias,v_sag,v_wet);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_live_band_frequency(void * v_h,float v_lower,float v_upper) {
{
dn_r_Engine * v_e=({ void * dn_tmp_0=v_h;
((dn_r_Engine *)dn_tmp_0); });
(void)(({ dn_r_ThreeBand * dn_tmp_1=(&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_threeBand));
double dn_tmp_2=({ float dn_tmp_3=v_lower;
(double)dn_tmp_3; });
double dn_tmp_4=({ float dn_tmp_5=v_upper;
(double)dn_tmp_5; });
dn_f_band_frequency(dn_tmp_1,dn_tmp_2,dn_tmp_4); }));
{ uint32_t dn_tmp_7=({ uint32_t dn_tmp_6=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_6>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_6; });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_live_band_frequency(void * v_h,float v_lower,float v_upper) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_band_frequency(v_h,v_lower,v_upper);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
DN_EXPORT uint32_t live_band_frequency(void * v_h,float v_lower,float v_upper) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_band_frequency(v_h,v_lower,v_upper);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_live_tube_routing(void * v_h,uint32_t v_routing) {
{
if(({ uint32_t dn_tmp_0=v_routing;
uint32_t dn_tmp_1=({ uint32_t dn_tmp_2=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_2>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_2; });
(dn_tmp_0 > dn_tmp_1); })){
{ uint32_t dn_tmp_4=({ uint32_t dn_tmp_3=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_3>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_3; });return dn_tmp_4; }
}
else{
}
dn_r_Engine * v_e=({ void * dn_tmp_5=v_h;
((dn_r_Engine *)dn_tmp_5); });
{ size_t dn_tmp_8=({ uint32_t dn_tmp_6=v_routing;
if((unsigned __int128)dn_tmp_6>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_6; });size_t *dn_tmp_7=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_tubeRouting);*dn_tmp_7=dn_tmp_8; }
{ bool dn_tmp_10=1;bool *dn_tmp_9=&(((*((dn_r_Engine *)dn_nonnull((void*)(v_e))))).f_tubeActive);*dn_tmp_9=dn_tmp_10; }
{ uint32_t dn_tmp_12=({ uint32_t dn_tmp_11=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_11; });return dn_tmp_12; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_live_tube_routing(void * v_h,uint32_t v_routing) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_tube_routing(v_h,v_routing);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
DN_EXPORT uint32_t live_tube_routing(void * v_h,uint32_t v_routing) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_live_tube_routing(v_h,v_routing);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
DN_EXPORT size_t dlm_native_sizeof_Stereo(void){return 16;}
DN_EXPORT size_t dlm_native_alignof_Stereo(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_Stereo(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Stereo_left(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Stereo_right(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_Fir(void){return 64;}
DN_EXPORT size_t dlm_native_alignof_Fir(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_Fir(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Fir_taps(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Fir_history(void){return 24;}
DN_EXPORT size_t dlm_native_offset_Fir_index(void){return 48;}
DN_EXPORT size_t dlm_native_offset_Fir_size(void){return 56;}
DN_EXPORT size_t dlm_native_sizeof_Biquad(void){return 56;}
DN_EXPORT size_t dlm_native_alignof_Biquad(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_Biquad(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Biquad_b0(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Biquad_b1(void){return 8;}
DN_EXPORT size_t dlm_native_offset_Biquad_b2(void){return 16;}
DN_EXPORT size_t dlm_native_offset_Biquad_a1(void){return 24;}
DN_EXPORT size_t dlm_native_offset_Biquad_a2(void){return 32;}
DN_EXPORT size_t dlm_native_offset_Biquad_z1(void){return 40;}
DN_EXPORT size_t dlm_native_offset_Biquad_z2(void){return 48;}
DN_EXPORT size_t dlm_native_sizeof_Allpass(void){return 16;}
DN_EXPORT size_t dlm_native_alignof_Allpass(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_Allpass(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Allpass_input(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Allpass_output(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_Creative(void){return 184;}
DN_EXPORT size_t dlm_native_alignof_Creative(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_Creative(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Creative_amounts(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Creative_focusState(void){return 24;}
DN_EXPORT size_t dlm_native_offset_Creative_warmState(void){return 40;}
DN_EXPORT size_t dlm_native_offset_Creative_hfState(void){return 56;}
DN_EXPORT size_t dlm_native_offset_Creative_allpass(void){return 72;}
DN_EXPORT size_t dlm_native_offset_Creative_smoothing(void){return 136;}
DN_EXPORT size_t dlm_native_offset_Creative_focusPole(void){return 144;}
DN_EXPORT size_t dlm_native_offset_Creative_warmPole(void){return 152;}
DN_EXPORT size_t dlm_native_offset_Creative_hfPole(void){return 160;}
DN_EXPORT size_t dlm_native_offset_Creative_ap0(void){return 168;}
DN_EXPORT size_t dlm_native_offset_Creative_ap1(void){return 176;}
DN_EXPORT size_t dlm_native_sizeof_Pulse(void){return 1904;}
DN_EXPORT size_t dlm_native_alignof_Pulse(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_Pulse(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Pulse_ntf(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Pulse_errors(void){return 96;}
DN_EXPORT size_t dlm_native_offset_Pulse_prefixes(void){return 184;}
DN_EXPORT size_t dlm_native_offset_Pulse_gains(void){return 1064;}
DN_EXPORT size_t dlm_native_offset_Pulse_order(void){return 1864;}
DN_EXPORT size_t dlm_native_offset_Pulse_elements(void){return 1872;}
DN_EXPORT size_t dlm_native_offset_Pulse_position(void){return 1880;}
DN_EXPORT size_t dlm_native_offset_Pulse_rng(void){return 1888;}
DN_EXPORT size_t dlm_native_offset_Pulse_seed(void){return 1892;}
DN_EXPORT size_t dlm_native_offset_Pulse_totalGain(void){return 1896;}
DN_EXPORT size_t dlm_native_sizeof_Dac(void){return 1896;}
DN_EXPORT size_t dlm_native_alignof_Dac(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_Dac(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Dac_ntf(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Dac_errors(void){return 96;}
DN_EXPORT size_t dlm_native_offset_Dac_prefixes(void){return 184;}
DN_EXPORT size_t dlm_native_offset_Dac_gains(void){return 1064;}
DN_EXPORT size_t dlm_native_offset_Dac_order(void){return 1864;}
DN_EXPORT size_t dlm_native_offset_Dac_mode(void){return 1872;}
DN_EXPORT size_t dlm_native_offset_Dac_rotation(void){return 1880;}
DN_EXPORT size_t dlm_native_offset_Dac_rng(void){return 1888;}
DN_EXPORT size_t dlm_native_offset_Dac_seed(void){return 1892;}
DN_EXPORT size_t dlm_native_sizeof_OutputStage(void){return 3712;}
DN_EXPORT size_t dlm_native_alignof_OutputStage(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_OutputStage(void){return 1;}
DN_EXPORT size_t dlm_native_offset_OutputStage_signal(void){return 0;}
DN_EXPORT size_t dlm_native_offset_OutputStage_peakHistory(void){return 2048;}
DN_EXPORT size_t dlm_native_offset_OutputStage_kernels(void){return 3072;}
DN_EXPORT size_t dlm_native_offset_OutputStage_index(void){return 3616;}
DN_EXPORT size_t dlm_native_offset_OutputStage_limiterPreviouslyEnabled(void){return 3624;}
DN_EXPORT size_t dlm_native_offset_OutputStage_inputPower(void){return 3632;}
DN_EXPORT size_t dlm_native_offset_OutputStage_processedPower(void){return 3640;}
DN_EXPORT size_t dlm_native_offset_OutputStage_autoGain(void){return 3648;}
DN_EXPORT size_t dlm_native_offset_OutputStage_limiterGain(void){return 3656;}
DN_EXPORT size_t dlm_native_offset_OutputStage_powerAlpha(void){return 3664;}
DN_EXPORT size_t dlm_native_offset_OutputStage_matchAlpha(void){return 3672;}
DN_EXPORT size_t dlm_native_offset_OutputStage_releaseAlpha(void){return 3680;}
DN_EXPORT size_t dlm_native_offset_OutputStage_estimatedPeak(void){return 3688;}
DN_EXPORT size_t dlm_native_offset_OutputStage_reductionDb(void){return 3696;}
DN_EXPORT size_t dlm_native_offset_OutputStage_autoGainDb(void){return 3704;}
DN_EXPORT size_t dlm_native_sizeof_TubeDef(void){return 88;}
DN_EXPORT size_t dlm_native_alignof_TubeDef(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_TubeDef(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TubeDef_mu(void){return 0;}
DN_EXPORT size_t dlm_native_offset_TubeDef_ex(void){return 8;}
DN_EXPORT size_t dlm_native_offset_TubeDef_kg1(void){return 16;}
DN_EXPORT size_t dlm_native_offset_TubeDef_kp(void){return 24;}
DN_EXPORT size_t dlm_native_offset_TubeDef_kvb(void){return 32;}
DN_EXPORT size_t dlm_native_offset_TubeDef_plate(void){return 40;}
DN_EXPORT size_t dlm_native_offset_TubeDef_grid(void){return 48;}
DN_EXPORT size_t dlm_native_offset_TubeDef_load(void){return 56;}
DN_EXPORT size_t dlm_native_offset_TubeDef_screen(void){return 64;}
DN_EXPORT size_t dlm_native_offset_TubeDef_swing(void){return 72;}
DN_EXPORT size_t dlm_native_offset_TubeDef_pentode(void){return 80;}
DN_EXPORT size_t dlm_native_sizeof_ValveChannel(void){return 80;}
DN_EXPORT size_t dlm_native_alignof_ValveChannel(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ValveChannel(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ValveChannel_oldType(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ValveChannel_newType(void){return 8;}
DN_EXPORT size_t dlm_native_offset_ValveChannel_oldTopology(void){return 16;}
DN_EXPORT size_t dlm_native_offset_ValveChannel_newTopology(void){return 24;}
DN_EXPORT size_t dlm_native_offset_ValveChannel_transition(void){return 32;}
DN_EXPORT size_t dlm_native_offset_ValveChannel_drive(void){return 40;}
DN_EXPORT size_t dlm_native_offset_ValveChannel_bias(void){return 48;}
DN_EXPORT size_t dlm_native_offset_ValveChannel_sag(void){return 56;}
DN_EXPORT size_t dlm_native_offset_ValveChannel_wet(void){return 64;}
DN_EXPORT size_t dlm_native_offset_ValveChannel_envelope(void){return 72;}
DN_EXPORT size_t dlm_native_sizeof_Tube(void){return 264;}
DN_EXPORT size_t dlm_native_alignof_Tube(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_Tube(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Tube_curves(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Tube_channels(void){return 24;}
DN_EXPORT size_t dlm_native_offset_Tube_desiredType(void){return 184;}
DN_EXPORT size_t dlm_native_offset_Tube_desiredTopology(void){return 192;}
DN_EXPORT size_t dlm_native_offset_Tube_desiredDrive(void){return 200;}
DN_EXPORT size_t dlm_native_offset_Tube_desiredBias(void){return 208;}
DN_EXPORT size_t dlm_native_offset_Tube_desiredSag(void){return 216;}
DN_EXPORT size_t dlm_native_offset_Tube_desiredWet(void){return 224;}
DN_EXPORT size_t dlm_native_offset_Tube_smoothing(void){return 232;}
DN_EXPORT size_t dlm_native_offset_Tube_envelopeAttack(void){return 240;}
DN_EXPORT size_t dlm_native_offset_Tube_envelopeRelease(void){return 248;}
DN_EXPORT size_t dlm_native_offset_Tube_transitionStep(void){return 256;}
DN_EXPORT size_t dlm_native_sizeof_BandChannel(void){return 472;}
DN_EXPORT size_t dlm_native_alignof_BandChannel(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_BandChannel(void){return 1;}
DN_EXPORT size_t dlm_native_offset_BandChannel_filters(void){return 0;}
DN_EXPORT size_t dlm_native_offset_BandChannel_currentLower(void){return 448;}
DN_EXPORT size_t dlm_native_offset_BandChannel_currentUpper(void){return 456;}
DN_EXPORT size_t dlm_native_offset_BandChannel_coefficientCounter(void){return 464;}
DN_EXPORT size_t dlm_native_sizeof_ThreeBand(void){return 1840;}
DN_EXPORT size_t dlm_native_alignof_ThreeBand(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ThreeBand(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ThreeBand_stages(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ThreeBand_channels(void){return 792;}
DN_EXPORT size_t dlm_native_offset_ThreeBand_rate(void){return 1736;}
DN_EXPORT size_t dlm_native_offset_ThreeBand_targetLower(void){return 1744;}
DN_EXPORT size_t dlm_native_offset_ThreeBand_targetUpper(void){return 1752;}
DN_EXPORT size_t dlm_native_offset_ThreeBand_frequencySmoothing(void){return 1760;}
DN_EXPORT size_t dlm_native_offset_ThreeBand_anyActive(void){return 1768;}
DN_EXPORT size_t dlm_native_offset_ThreeBand_releaseFrames(void){return 1776;}
DN_EXPORT size_t dlm_native_offset_ThreeBand_types(void){return 1792;}
DN_EXPORT size_t dlm_native_offset_ThreeBand_wets(void){return 1816;}
DN_EXPORT size_t dlm_native_sizeof_Folded(void){return 30776;}
DN_EXPORT size_t dlm_native_alignof_Folded(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_Folded(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Folded_coefficients(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Folded_lengths(void){return 18432;}
DN_EXPORT size_t dlm_native_offset_Folded_history(void){return 18480;}
DN_EXPORT size_t dlm_native_offset_Folded_index(void){return 30768;}
DN_EXPORT size_t dlm_native_sizeof_Engine(void){return 49424;}
DN_EXPORT size_t dlm_native_alignof_Engine(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_Engine(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Engine_up(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Engine_down(void){return 24;}
DN_EXPORT size_t dlm_native_offset_Engine_dryL(void){return 48;}
DN_EXPORT size_t dlm_native_offset_Engine_dryR(void){return 1304;}
DN_EXPORT size_t dlm_native_offset_Engine_alignedL(void){return 2560;}
DN_EXPORT size_t dlm_native_offset_Engine_alignedR(void){return 3064;}
DN_EXPORT size_t dlm_native_offset_Engine_delayL(void){return 3568;}
DN_EXPORT size_t dlm_native_offset_Engine_delayR(void){return 4080;}
DN_EXPORT size_t dlm_native_offset_Engine_dryIndex(void){return 4592;}
DN_EXPORT size_t dlm_native_offset_Engine_alignIndex(void){return 4600;}
DN_EXPORT size_t dlm_native_offset_Engine_crossIndex(void){return 4608;}
DN_EXPORT size_t dlm_native_offset_Engine_crossLowL(void){return 4616;}
DN_EXPORT size_t dlm_native_offset_Engine_crossLowR(void){return 4624;}
DN_EXPORT size_t dlm_native_offset_Engine_crossAlpha(void){return 4632;}
DN_EXPORT size_t dlm_native_offset_Engine_crossSamples(void){return 4640;}
DN_EXPORT size_t dlm_native_offset_Engine_crossCurrent(void){return 4648;}
DN_EXPORT size_t dlm_native_offset_Engine_mixCurrent(void){return 4656;}
DN_EXPORT size_t dlm_native_offset_Engine_gainCurrent(void){return 4664;}
DN_EXPORT size_t dlm_native_offset_Engine_controlAlpha(void){return 4672;}
DN_EXPORT size_t dlm_native_offset_Engine_warmLowL(void){return 4680;}
DN_EXPORT size_t dlm_native_offset_Engine_warmLowR(void){return 4688;}
DN_EXPORT size_t dlm_native_offset_Engine_warmAlpha(void){return 4696;}
DN_EXPORT size_t dlm_native_offset_Engine_warmMix(void){return 4704;}
DN_EXPORT size_t dlm_native_offset_Engine_hfMix(void){return 4712;}
DN_EXPORT size_t dlm_native_offset_Engine_hfAvailable(void){return 4720;}
DN_EXPORT size_t dlm_native_offset_Engine_hfL(void){return 4728;}
DN_EXPORT size_t dlm_native_offset_Engine_hfR(void){return 4784;}
DN_EXPORT size_t dlm_native_offset_Engine_creative(void){return 4840;}
DN_EXPORT size_t dlm_native_offset_Engine_pulses(void){return 5024;}
DN_EXPORT size_t dlm_native_offset_Engine_dacs(void){return 8832;}
DN_EXPORT size_t dlm_native_offset_Engine_pulseMode(void){return 12624;}
DN_EXPORT size_t dlm_native_offset_Engine_dacMode(void){return 12632;}
DN_EXPORT size_t dlm_native_offset_Engine_mismatch(void){return 12640;}
DN_EXPORT size_t dlm_native_offset_Engine_characterCurrent(void){return 12648;}
DN_EXPORT size_t dlm_native_offset_Engine_pulseColorCurrent(void){return 12656;}
DN_EXPORT size_t dlm_native_offset_Engine_depth(void){return 12664;}
DN_EXPORT size_t dlm_native_offset_Engine_sampleRate(void){return 12672;}
DN_EXPORT size_t dlm_native_offset_Engine_output(void){return 12680;}
DN_EXPORT size_t dlm_native_offset_Engine_tube(void){return 16392;}
DN_EXPORT size_t dlm_native_offset_Engine_tubeActive(void){return 16656;}
DN_EXPORT size_t dlm_native_offset_Engine_threeBand(void){return 16664;}
DN_EXPORT size_t dlm_native_offset_Engine_tubeRouting(void){return 18504;}
DN_EXPORT size_t dlm_native_offset_Engine_tubeRouteBlend(void){return 18512;}
DN_EXPORT size_t dlm_native_offset_Engine_tubeRouteStep(void){return 18528;}
DN_EXPORT size_t dlm_native_offset_Engine_pendingDepth(void){return 18536;}
DN_EXPORT size_t dlm_native_offset_Engine_pendingPulse(void){return 18544;}
DN_EXPORT size_t dlm_native_offset_Engine_pendingDac(void){return 18552;}
DN_EXPORT size_t dlm_native_offset_Engine_transitionCounter(void){return 18560;}
DN_EXPORT size_t dlm_native_offset_Engine_transitioning(void){return 18568;}
DN_EXPORT size_t dlm_native_offset_Engine_folded(void){return 18576;}
DN_EXPORT size_t dlm_native_offset_Engine_fastEnabled(void){return 49352;}
DN_EXPORT size_t dlm_native_offset_Engine_foldedHistoryActive(void){return 49353;}
DN_EXPORT size_t dlm_native_offset_Engine_foldedPriming(void){return 49360;}
DN_EXPORT size_t dlm_native_offset_Engine_legacyPriming(void){return 49368;}
DN_EXPORT size_t dlm_native_offset_Engine_legacyRunning(void){return 49376;}
DN_EXPORT size_t dlm_native_offset_Engine_fastWeight(void){return 49384;}
DN_EXPORT size_t dlm_native_offset_Engine_foldedDepth(void){return 49392;}
DN_EXPORT size_t dlm_native_offset_Engine_previousFoldedDepth(void){return 49400;}
DN_EXPORT size_t dlm_native_offset_Engine_foldedFade(void){return 49408;}
DN_EXPORT size_t dlm_native_offset_Engine_tubeCpuMode(void){return 49416;}
