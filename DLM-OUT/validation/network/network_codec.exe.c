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
typedef struct dn_r_TransportRequirements dn_r_TransportRequirements;
typedef struct dn_r_TransportBounds dn_r_TransportBounds;
typedef struct dn_r_TransportCandidate dn_r_TransportCandidate;
typedef struct dn_r_TransportCheck dn_r_TransportCheck;
typedef struct dn_r_TransportChoice dn_r_TransportChoice;
typedef struct dn_r_ProtocolVersion dn_r_ProtocolVersion;
typedef struct dn_r_WireFormat dn_r_WireFormat;
typedef struct dn_r_MessageField dn_r_MessageField;
typedef struct dn_r_MessageSchema dn_r_MessageSchema;
typedef struct dn_r_SessionContract dn_r_SessionContract;
typedef struct dn_r_SecurityRequirements dn_r_SecurityRequirements;
typedef struct dn_r_ResourceBounds dn_r_ResourceBounds;
typedef struct dn_r_ProofObligations dn_r_ProofObligations;
typedef struct dn_r_NetworkSpec dn_r_NetworkSpec;
typedef struct dn_r_SchemaMeasure dn_r_SchemaMeasure;
typedef struct dn_r_NetworkSpecCheck dn_r_NetworkSpecCheck;
typedef struct dn_r_NetworkAdmission dn_r_NetworkAdmission;
typedef struct dn_r_ProtocolValue dn_r_ProtocolValue;
typedef struct dn_r_ProtocolFrame dn_r_ProtocolFrame;
typedef struct dn_r_Utf8Step dn_r_Utf8Step;
typedef struct dn_r_CodecPlan dn_r_CodecPlan;
typedef struct dn_r_CodecError dn_r_CodecError;
typedef struct dn_r_CodecIo dn_r_CodecIo;
typedef struct dn_r_CodecValue dn_r_CodecValue;
typedef struct dn_r_CodecMessage dn_r_CodecMessage;
typedef struct dn_r_CodecDecoded dn_r_CodecDecoded;
typedef struct dn_r_CodecScalar dn_r_CodecScalar;
typedef struct dn_r_CodecSpan dn_r_CodecSpan;
typedef struct dn_r_CodecRead dn_r_CodecRead;
typedef struct dn_r_CodecShape dn_r_CodecShape;
typedef struct dn_r_CodecReservation dn_r_CodecReservation;
typedef struct dn_r_EchoMessage1 dn_r_EchoMessage1;
typedef struct dn_r_EchoMessage1Decoded dn_r_EchoMessage1Decoded;
typedef struct dn_r_EchoMessage1Field1 dn_r_EchoMessage1Field1;
typedef struct dn_r_EchoMessage1Field2 dn_r_EchoMessage1Field2;
typedef struct dn_r_EchoMessage1Field3 dn_r_EchoMessage1Field3;
typedef struct dn_r_EchoMessage1Field4 dn_r_EchoMessage1Field4;
typedef struct dn_r_EchoMessage1Field5 dn_r_EchoMessage1Field5;
typedef struct dn_r_EchoMessage1Field8 dn_r_EchoMessage1Field8;
typedef struct dn_r_EchoMessage1Field9 dn_r_EchoMessage1Field9;
typedef struct dn_r_EchoMessage1Field10 dn_r_EchoMessage1Field10;
typedef struct dn_r_EchoMessage2 dn_r_EchoMessage2;
typedef struct dn_r_EchoMessage2Decoded dn_r_EchoMessage2Decoded;
typedef struct dn_r_PosMessage1 dn_r_PosMessage1;
typedef struct dn_r_PosMessage1Decoded dn_r_PosMessage1Decoded;
typedef struct dn_r_PosMessage1Field1 dn_r_PosMessage1Field1;
typedef struct dn_r_PosMessage1Field2 dn_r_PosMessage1Field2;
typedef struct dn_r_PosMessage1Field3 dn_r_PosMessage1Field3;
typedef struct dn_r_PosMessage1Field4 dn_r_PosMessage1Field4;
typedef struct dn_r_PosMessage1Field5 dn_r_PosMessage1Field5;
typedef struct dn_r_PosMessage1Field8 dn_r_PosMessage1Field8;
typedef struct dn_r_PosMessage1Field9 dn_r_PosMessage1Field9;
typedef struct dn_r_PosMessage2 dn_r_PosMessage2;
typedef struct dn_r_PosMessage2Decoded dn_r_PosMessage2Decoded;
typedef struct dn_r_LittleMessage1 dn_r_LittleMessage1;
typedef struct dn_r_LittleMessage1Decoded dn_r_LittleMessage1Decoded;
typedef struct dn_r_LittleMessage1Field1 dn_r_LittleMessage1Field1;
typedef struct dn_r_LittleMessage1Field2 dn_r_LittleMessage1Field2;
typedef struct dn_r_LittleMessage1Field3 dn_r_LittleMessage1Field3;
typedef struct dn_r_LittleMessage1Field4 dn_r_LittleMessage1Field4;
typedef struct dn_r_LittleMessage1Field5 dn_r_LittleMessage1Field5;
typedef struct dn_r_LittleMessage1Field8 dn_r_LittleMessage1Field8;
typedef struct dn_r_LittleMessage1Field9 dn_r_LittleMessage1Field9;
typedef struct dn_r_LittleMessage2 dn_r_LittleMessage2;
typedef struct dn_r_LittleMessage2Decoded dn_r_LittleMessage2Decoded;
typedef struct dn_t_buf_r10_CodecValue { dn_r_CodecValue *data;size_t len,cap; } dn_t_buf_r10_CodecValue;
typedef struct dn_t_buf_U8 { uint8_t *data;size_t len,cap; } dn_t_buf_U8;
struct dn_r_CodecMessage {
    uint32_t f_message;
    dn_t_buf_r10_CodecValue f_values;
    dn_t_buf_U8 f_bytes;
    dn_t_buf_U8 f_unknown;
};
struct dn_r_CodecError {
    int32_t f_kind;
    uint32_t f_field;
    size_t f_offset;
};
struct dn_r_CodecDecoded {
    dn_r_CodecMessage f_value;
    dn_r_CodecError f_error;
};
struct dn_r_CodecIo {
    size_t f_count;
    size_t f_next;
    dn_r_CodecError f_error;
};
struct dn_r_WireFormat {
    uint32_t f_kind;
    uint32_t f_endian;
    uint32_t f_envelope_version;
    uint32_t f_unknown_fields;
};
struct dn_r_MessageField {
    uint32_t f_id;
    uint32_t f_kind;
    uint32_t f_minimum_count;
    uint32_t f_maximum_count;
    uint32_t f_maximum_length;
    uint32_t f_first_minor;
    uint32_t f_last_minor;
    uint32_t f_flags;
};
typedef struct dn_t_arr32_r12_MessageField { dn_r_MessageField data[32]; } dn_t_arr32_r12_MessageField;
struct dn_r_CodecPlan {
    uint32_t f_message;
    dn_r_WireFormat f_wire;
    uint32_t f_minor;
    uint32_t f_field_count;
    dn_t_arr32_r12_MessageField f_fields;
    uint32_t f_maximum_values;
    size_t f_maximum_wire;
    uint32_t f_maximum_unknown_fields;
    size_t f_maximum_unknown_bytes;
    size_t f_storage_budget;
};
struct dn_r_CodecRead {
    uint64_t f_scalar;
    size_t f_offset;
    size_t f_count;
    size_t f_next;
    dn_r_CodecError f_error;
};
struct dn_r_CodecReservation {
    size_t f_start;
    size_t f_count;
    dn_r_CodecError f_error;
};
struct dn_r_CodecScalar {
    uint64_t f_value;
    dn_r_CodecError f_error;
};
typedef struct dn_t_arr32_U32 { uint32_t data[32]; } dn_t_arr32_U32;
typedef struct dn_t_arr32_USize { size_t data[32]; } dn_t_arr32_USize;
struct dn_r_CodecShape {
    dn_t_arr32_U32 f_counts;
    dn_t_arr32_USize f_lengths;
    size_t f_values;
    size_t f_bytes;
    size_t f_unknown;
    uint32_t f_unknown_fields;
    size_t f_wire;
    dn_r_CodecError f_error;
};
struct dn_r_CodecSpan {
    size_t f_offset;
    size_t f_count;
    dn_r_CodecError f_error;
};
struct dn_r_CodecValue {
    uint32_t f_field;
    uint32_t f_kind;
    uint64_t f_scalar;
    size_t f_offset;
    size_t f_count;
};
struct dn_r_EchoMessage1 {
    dn_r_CodecMessage f_inner;
};
struct dn_r_EchoMessage1Decoded {
    dn_r_EchoMessage1 f_value;
    dn_r_CodecError f_error;
};
struct dn_r_EchoMessage1Field1 {
    uint8_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_EchoMessage1Field10 {
    uint16_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_EchoMessage1Field2 {
    uint16_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_EchoMessage1Field3 {
    uint32_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_EchoMessage1Field4 {
    uint64_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_EchoMessage1Field5 {
    bool f_value;
    dn_r_CodecError f_error;
};
struct dn_r_EchoMessage1Field8 {
    uint64_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_EchoMessage1Field9 {
    int64_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_EchoMessage2 {
    dn_r_CodecMessage f_inner;
};
struct dn_r_EchoMessage2Decoded {
    dn_r_EchoMessage2 f_value;
    dn_r_CodecError f_error;
};
struct dn_r_LittleMessage1 {
    dn_r_CodecMessage f_inner;
};
struct dn_r_LittleMessage1Decoded {
    dn_r_LittleMessage1 f_value;
    dn_r_CodecError f_error;
};
struct dn_r_LittleMessage1Field1 {
    uint8_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_LittleMessage1Field2 {
    uint16_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_LittleMessage1Field3 {
    uint32_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_LittleMessage1Field4 {
    uint64_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_LittleMessage1Field5 {
    bool f_value;
    dn_r_CodecError f_error;
};
struct dn_r_LittleMessage1Field8 {
    uint64_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_LittleMessage1Field9 {
    int64_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_LittleMessage2 {
    dn_r_CodecMessage f_inner;
};
struct dn_r_LittleMessage2Decoded {
    dn_r_LittleMessage2 f_value;
    dn_r_CodecError f_error;
};
struct dn_r_MessageSchema {
    uint32_t f_id;
    uint32_t f_phase;
    uint32_t f_senders;
    uint32_t f_first_minor;
    uint32_t f_last_minor;
    uint32_t f_field_count;
    dn_t_arr32_r12_MessageField f_fields;
};
struct dn_r_TransportRequirements {
    uint32_t f_required;
    uint32_t f_forbidden;
    uint32_t f_preferred;
};
struct dn_r_TransportBounds {
    uint32_t f_max_message;
    uint32_t f_queued_messages;
    uint32_t f_channels;
    size_t f_memory;
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
struct dn_r_SessionContract {
    uint32_t f_handshake_timeout_ms;
    uint32_t f_idle_timeout_ms;
    uint32_t f_maximum_duration_ms;
    uint32_t f_maximum_in_flight;
    bool f_authentication_before_data;
};
struct dn_r_NetworkAdmission {
    dn_r_TransportRequirements f_requirements;
    dn_r_TransportBounds f_bounds;
    dn_r_TransportChoice f_choice;
    dn_r_SessionContract f_session;
    uint64_t f_maximum_session_messages;
    uint32_t f_pending_proofs;
    uint32_t f_unsupported_security;
    int32_t f_status;
};
struct dn_r_ProtocolVersion {
    uint32_t f_major;
    uint32_t f_minor;
    uint32_t f_minimum_minor;
};
struct dn_r_SecurityRequirements {
    uint32_t f_authentication;
    bool f_confidentiality;
    bool f_integrity;
    uint32_t f_replay_scope;
};
struct dn_r_ResourceBounds {
    dn_r_TransportBounds f_transport;
    uint32_t f_maximum_unknown_fields;
    uint32_t f_maximum_unknown_bytes;
    uint64_t f_maximum_session_messages;
    uint32_t f_maximum_schema_nodes;
};
struct dn_r_ProofObligations {
    uint32_t f_requested;
    uint32_t f_assurance;
};
typedef struct dn_t_arr16_r13_MessageSchema { dn_r_MessageSchema data[16]; } dn_t_arr16_r13_MessageSchema;
struct dn_r_NetworkSpec {
    uint32_t f_protocol;
    dn_r_ProtocolVersion f_version;
    dn_r_WireFormat f_wire;
    dn_r_TransportRequirements f_transport;
    dn_r_SecurityRequirements f_security;
    dn_r_ResourceBounds f_resources;
    dn_r_SessionContract f_session;
    dn_r_ProofObligations f_proofs;
    uint32_t f_message_count;
    dn_t_arr16_r13_MessageSchema f_messages;
};
struct dn_r_NetworkSpecCheck {
    int32_t f_status;
    uint32_t f_component;
    uint32_t f_message;
    uint32_t f_field;
    size_t f_minimum;
    size_t f_maximum;
    uint32_t f_required_properties;
    uint32_t f_required_proofs;
    uint32_t f_pending_proofs;
    uint32_t f_unsupported_security;
    uint32_t f_schema_nodes;
};
struct dn_r_PosMessage1 {
    dn_r_CodecMessage f_inner;
};
struct dn_r_PosMessage1Decoded {
    dn_r_PosMessage1 f_value;
    dn_r_CodecError f_error;
};
struct dn_r_PosMessage1Field1 {
    uint8_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_PosMessage1Field2 {
    uint16_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_PosMessage1Field3 {
    uint32_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_PosMessage1Field4 {
    uint64_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_PosMessage1Field5 {
    bool f_value;
    dn_r_CodecError f_error;
};
struct dn_r_PosMessage1Field8 {
    uint64_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_PosMessage1Field9 {
    int64_t f_value;
    dn_r_CodecError f_error;
};
struct dn_r_PosMessage2 {
    dn_r_CodecMessage f_inner;
};
struct dn_r_PosMessage2Decoded {
    dn_r_PosMessage2 f_value;
    dn_r_CodecError f_error;
};
struct dn_r_ProtocolFrame {
    size_t f_payload;
    size_t f_count;
    size_t f_next;
    int32_t f_status;
};
struct dn_r_ProtocolValue {
    uint64_t f_value;
    size_t f_next;
    int32_t f_status;
};
struct dn_r_SchemaMeasure {
    uint64_t f_minimum;
    uint64_t f_maximum;
    int32_t f_status;
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
struct dn_r_Utf8Step {
    uint32_t f_codepoint;
    size_t f_next;
    bool f_valid;
};
typedef struct dn_t_arr6_r18_TransportCandidate { dn_r_TransportCandidate data[6]; } dn_t_arr6_r18_TransportCandidate;
typedef struct dn_t_buf_U32 { uint32_t *data;size_t len,cap; } dn_t_buf_U32;
typedef struct dn_t_buf_U64 { uint64_t *data;size_t len,cap; } dn_t_buf_U64;
typedef struct dn_t_buf_I64 { int64_t *data;size_t len,cap; } dn_t_buf_I64;
typedef struct dn_t_slice0_U8 { uint8_t const *data;size_t len; } dn_t_slice0_U8;
typedef struct dn_t_slice0_U16 { uint16_t const *data;size_t len; } dn_t_slice0_U16;
typedef struct dn_t_slice0_U32 { uint32_t const *data;size_t len; } dn_t_slice0_U32;
typedef struct dn_t_slice0_U64 { uint64_t const *data;size_t len; } dn_t_slice0_U64;
typedef struct dn_t_slice0_I64 { int64_t const *data;size_t len; } dn_t_slice0_I64;
typedef struct dn_t_slice0_b { bool const *data;size_t len; } dn_t_slice0_b;
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
_Static_assert(sizeof(dn_r_ProtocolVersion)==12,"ProtocolVersion ABI size");
_Static_assert(_Alignof(dn_r_ProtocolVersion)==4,"ProtocolVersion ABI alignment");
_Static_assert(offsetof(dn_r_ProtocolVersion,f_major)==0,"ProtocolVersion.major ABI offset");
_Static_assert(offsetof(dn_r_ProtocolVersion,f_minor)==4,"ProtocolVersion.minor ABI offset");
_Static_assert(offsetof(dn_r_ProtocolVersion,f_minimum_minor)==8,"ProtocolVersion.minimum_minor ABI offset");
_Static_assert(sizeof(dn_r_WireFormat)==16,"WireFormat ABI size");
_Static_assert(_Alignof(dn_r_WireFormat)==4,"WireFormat ABI alignment");
_Static_assert(offsetof(dn_r_WireFormat,f_kind)==0,"WireFormat.kind ABI offset");
_Static_assert(offsetof(dn_r_WireFormat,f_endian)==4,"WireFormat.endian ABI offset");
_Static_assert(offsetof(dn_r_WireFormat,f_envelope_version)==8,"WireFormat.envelope_version ABI offset");
_Static_assert(offsetof(dn_r_WireFormat,f_unknown_fields)==12,"WireFormat.unknown_fields ABI offset");
_Static_assert(sizeof(dn_r_MessageField)==32,"MessageField ABI size");
_Static_assert(_Alignof(dn_r_MessageField)==4,"MessageField ABI alignment");
_Static_assert(offsetof(dn_r_MessageField,f_id)==0,"MessageField.id ABI offset");
_Static_assert(offsetof(dn_r_MessageField,f_kind)==4,"MessageField.kind ABI offset");
_Static_assert(offsetof(dn_r_MessageField,f_minimum_count)==8,"MessageField.minimum_count ABI offset");
_Static_assert(offsetof(dn_r_MessageField,f_maximum_count)==12,"MessageField.maximum_count ABI offset");
_Static_assert(offsetof(dn_r_MessageField,f_maximum_length)==16,"MessageField.maximum_length ABI offset");
_Static_assert(offsetof(dn_r_MessageField,f_first_minor)==20,"MessageField.first_minor ABI offset");
_Static_assert(offsetof(dn_r_MessageField,f_last_minor)==24,"MessageField.last_minor ABI offset");
_Static_assert(offsetof(dn_r_MessageField,f_flags)==28,"MessageField.flags ABI offset");
_Static_assert(sizeof(dn_r_MessageSchema)==1048,"MessageSchema ABI size");
_Static_assert(_Alignof(dn_r_MessageSchema)==4,"MessageSchema ABI alignment");
_Static_assert(offsetof(dn_r_MessageSchema,f_id)==0,"MessageSchema.id ABI offset");
_Static_assert(offsetof(dn_r_MessageSchema,f_phase)==4,"MessageSchema.phase ABI offset");
_Static_assert(offsetof(dn_r_MessageSchema,f_senders)==8,"MessageSchema.senders ABI offset");
_Static_assert(offsetof(dn_r_MessageSchema,f_first_minor)==12,"MessageSchema.first_minor ABI offset");
_Static_assert(offsetof(dn_r_MessageSchema,f_last_minor)==16,"MessageSchema.last_minor ABI offset");
_Static_assert(offsetof(dn_r_MessageSchema,f_field_count)==20,"MessageSchema.field_count ABI offset");
_Static_assert(offsetof(dn_r_MessageSchema,f_fields)==24,"MessageSchema.fields ABI offset");
_Static_assert(sizeof(dn_r_SessionContract)==20,"SessionContract ABI size");
_Static_assert(_Alignof(dn_r_SessionContract)==4,"SessionContract ABI alignment");
_Static_assert(offsetof(dn_r_SessionContract,f_handshake_timeout_ms)==0,"SessionContract.handshake_timeout_ms ABI offset");
_Static_assert(offsetof(dn_r_SessionContract,f_idle_timeout_ms)==4,"SessionContract.idle_timeout_ms ABI offset");
_Static_assert(offsetof(dn_r_SessionContract,f_maximum_duration_ms)==8,"SessionContract.maximum_duration_ms ABI offset");
_Static_assert(offsetof(dn_r_SessionContract,f_maximum_in_flight)==12,"SessionContract.maximum_in_flight ABI offset");
_Static_assert(offsetof(dn_r_SessionContract,f_authentication_before_data)==16,"SessionContract.authentication_before_data ABI offset");
_Static_assert(sizeof(dn_r_SecurityRequirements)==12,"SecurityRequirements ABI size");
_Static_assert(_Alignof(dn_r_SecurityRequirements)==4,"SecurityRequirements ABI alignment");
_Static_assert(offsetof(dn_r_SecurityRequirements,f_authentication)==0,"SecurityRequirements.authentication ABI offset");
_Static_assert(offsetof(dn_r_SecurityRequirements,f_confidentiality)==4,"SecurityRequirements.confidentiality ABI offset");
_Static_assert(offsetof(dn_r_SecurityRequirements,f_integrity)==5,"SecurityRequirements.integrity ABI offset");
_Static_assert(offsetof(dn_r_SecurityRequirements,f_replay_scope)==8,"SecurityRequirements.replay_scope ABI offset");
_Static_assert(sizeof(dn_r_ResourceBounds)==48,"ResourceBounds ABI size");
_Static_assert(_Alignof(dn_r_ResourceBounds)==8,"ResourceBounds ABI alignment");
_Static_assert(offsetof(dn_r_ResourceBounds,f_transport)==0,"ResourceBounds.transport ABI offset");
_Static_assert(offsetof(dn_r_ResourceBounds,f_maximum_unknown_fields)==24,"ResourceBounds.maximum_unknown_fields ABI offset");
_Static_assert(offsetof(dn_r_ResourceBounds,f_maximum_unknown_bytes)==28,"ResourceBounds.maximum_unknown_bytes ABI offset");
_Static_assert(offsetof(dn_r_ResourceBounds,f_maximum_session_messages)==32,"ResourceBounds.maximum_session_messages ABI offset");
_Static_assert(offsetof(dn_r_ResourceBounds,f_maximum_schema_nodes)==40,"ResourceBounds.maximum_schema_nodes ABI offset");
_Static_assert(sizeof(dn_r_ProofObligations)==8,"ProofObligations ABI size");
_Static_assert(_Alignof(dn_r_ProofObligations)==4,"ProofObligations ABI alignment");
_Static_assert(offsetof(dn_r_ProofObligations,f_requested)==0,"ProofObligations.requested ABI offset");
_Static_assert(offsetof(dn_r_ProofObligations,f_assurance)==4,"ProofObligations.assurance ABI offset");
_Static_assert(sizeof(dn_r_NetworkSpec)==16904,"NetworkSpec ABI size");
_Static_assert(_Alignof(dn_r_NetworkSpec)==8,"NetworkSpec ABI alignment");
_Static_assert(offsetof(dn_r_NetworkSpec,f_protocol)==0,"NetworkSpec.protocol ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpec,f_version)==4,"NetworkSpec.version ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpec,f_wire)==16,"NetworkSpec.wire ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpec,f_transport)==32,"NetworkSpec.transport ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpec,f_security)==44,"NetworkSpec.security ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpec,f_resources)==56,"NetworkSpec.resources ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpec,f_session)==104,"NetworkSpec.session ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpec,f_proofs)==124,"NetworkSpec.proofs ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpec,f_message_count)==132,"NetworkSpec.message_count ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpec,f_messages)==136,"NetworkSpec.messages ABI offset");
_Static_assert(sizeof(dn_r_SchemaMeasure)==24,"SchemaMeasure ABI size");
_Static_assert(_Alignof(dn_r_SchemaMeasure)==8,"SchemaMeasure ABI alignment");
_Static_assert(offsetof(dn_r_SchemaMeasure,f_minimum)==0,"SchemaMeasure.minimum ABI offset");
_Static_assert(offsetof(dn_r_SchemaMeasure,f_maximum)==8,"SchemaMeasure.maximum ABI offset");
_Static_assert(offsetof(dn_r_SchemaMeasure,f_status)==16,"SchemaMeasure.status ABI offset");
_Static_assert(sizeof(dn_r_NetworkSpecCheck)==56,"NetworkSpecCheck ABI size");
_Static_assert(_Alignof(dn_r_NetworkSpecCheck)==8,"NetworkSpecCheck ABI alignment");
_Static_assert(offsetof(dn_r_NetworkSpecCheck,f_status)==0,"NetworkSpecCheck.status ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpecCheck,f_component)==4,"NetworkSpecCheck.component ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpecCheck,f_message)==8,"NetworkSpecCheck.message ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpecCheck,f_field)==12,"NetworkSpecCheck.field ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpecCheck,f_minimum)==16,"NetworkSpecCheck.minimum ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpecCheck,f_maximum)==24,"NetworkSpecCheck.maximum ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpecCheck,f_required_properties)==32,"NetworkSpecCheck.required_properties ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpecCheck,f_required_proofs)==36,"NetworkSpecCheck.required_proofs ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpecCheck,f_pending_proofs)==40,"NetworkSpecCheck.pending_proofs ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpecCheck,f_unsupported_security)==44,"NetworkSpecCheck.unsupported_security ABI offset");
_Static_assert(offsetof(dn_r_NetworkSpecCheck,f_schema_nodes)==48,"NetworkSpecCheck.schema_nodes ABI offset");
_Static_assert(sizeof(dn_r_NetworkAdmission)==112,"NetworkAdmission ABI size");
_Static_assert(_Alignof(dn_r_NetworkAdmission)==8,"NetworkAdmission ABI alignment");
_Static_assert(offsetof(dn_r_NetworkAdmission,f_requirements)==0,"NetworkAdmission.requirements ABI offset");
_Static_assert(offsetof(dn_r_NetworkAdmission,f_bounds)==16,"NetworkAdmission.bounds ABI offset");
_Static_assert(offsetof(dn_r_NetworkAdmission,f_choice)==40,"NetworkAdmission.choice ABI offset");
_Static_assert(offsetof(dn_r_NetworkAdmission,f_session)==68,"NetworkAdmission.session ABI offset");
_Static_assert(offsetof(dn_r_NetworkAdmission,f_maximum_session_messages)==88,"NetworkAdmission.maximum_session_messages ABI offset");
_Static_assert(offsetof(dn_r_NetworkAdmission,f_pending_proofs)==96,"NetworkAdmission.pending_proofs ABI offset");
_Static_assert(offsetof(dn_r_NetworkAdmission,f_unsupported_security)==100,"NetworkAdmission.unsupported_security ABI offset");
_Static_assert(offsetof(dn_r_NetworkAdmission,f_status)==104,"NetworkAdmission.status ABI offset");
_Static_assert(sizeof(dn_r_ProtocolValue)==24,"ProtocolValue ABI size");
_Static_assert(_Alignof(dn_r_ProtocolValue)==8,"ProtocolValue ABI alignment");
_Static_assert(offsetof(dn_r_ProtocolValue,f_value)==0,"ProtocolValue.value ABI offset");
_Static_assert(offsetof(dn_r_ProtocolValue,f_next)==8,"ProtocolValue.next ABI offset");
_Static_assert(offsetof(dn_r_ProtocolValue,f_status)==16,"ProtocolValue.status ABI offset");
_Static_assert(sizeof(dn_r_ProtocolFrame)==32,"ProtocolFrame ABI size");
_Static_assert(_Alignof(dn_r_ProtocolFrame)==8,"ProtocolFrame ABI alignment");
_Static_assert(offsetof(dn_r_ProtocolFrame,f_payload)==0,"ProtocolFrame.payload ABI offset");
_Static_assert(offsetof(dn_r_ProtocolFrame,f_count)==8,"ProtocolFrame.count ABI offset");
_Static_assert(offsetof(dn_r_ProtocolFrame,f_next)==16,"ProtocolFrame.next ABI offset");
_Static_assert(offsetof(dn_r_ProtocolFrame,f_status)==24,"ProtocolFrame.status ABI offset");
_Static_assert(sizeof(dn_r_Utf8Step)==24,"Utf8Step ABI size");
_Static_assert(_Alignof(dn_r_Utf8Step)==8,"Utf8Step ABI alignment");
_Static_assert(offsetof(dn_r_Utf8Step,f_codepoint)==0,"Utf8Step.codepoint ABI offset");
_Static_assert(offsetof(dn_r_Utf8Step,f_next)==8,"Utf8Step.next ABI offset");
_Static_assert(offsetof(dn_r_Utf8Step,f_valid)==16,"Utf8Step.valid ABI offset");
_Static_assert(sizeof(dn_r_CodecPlan)==1088,"CodecPlan ABI size");
_Static_assert(_Alignof(dn_r_CodecPlan)==8,"CodecPlan ABI alignment");
_Static_assert(offsetof(dn_r_CodecPlan,f_message)==0,"CodecPlan.message ABI offset");
_Static_assert(offsetof(dn_r_CodecPlan,f_wire)==4,"CodecPlan.wire ABI offset");
_Static_assert(offsetof(dn_r_CodecPlan,f_minor)==20,"CodecPlan.minor ABI offset");
_Static_assert(offsetof(dn_r_CodecPlan,f_field_count)==24,"CodecPlan.field_count ABI offset");
_Static_assert(offsetof(dn_r_CodecPlan,f_fields)==28,"CodecPlan.fields ABI offset");
_Static_assert(offsetof(dn_r_CodecPlan,f_maximum_values)==1052,"CodecPlan.maximum_values ABI offset");
_Static_assert(offsetof(dn_r_CodecPlan,f_maximum_wire)==1056,"CodecPlan.maximum_wire ABI offset");
_Static_assert(offsetof(dn_r_CodecPlan,f_maximum_unknown_fields)==1064,"CodecPlan.maximum_unknown_fields ABI offset");
_Static_assert(offsetof(dn_r_CodecPlan,f_maximum_unknown_bytes)==1072,"CodecPlan.maximum_unknown_bytes ABI offset");
_Static_assert(offsetof(dn_r_CodecPlan,f_storage_budget)==1080,"CodecPlan.storage_budget ABI offset");
_Static_assert(sizeof(dn_r_CodecError)==16,"CodecError ABI size");
_Static_assert(_Alignof(dn_r_CodecError)==8,"CodecError ABI alignment");
_Static_assert(offsetof(dn_r_CodecError,f_kind)==0,"CodecError.kind ABI offset");
_Static_assert(offsetof(dn_r_CodecError,f_field)==4,"CodecError.field ABI offset");
_Static_assert(offsetof(dn_r_CodecError,f_offset)==8,"CodecError.offset ABI offset");
_Static_assert(sizeof(dn_r_CodecIo)==32,"CodecIo ABI size");
_Static_assert(_Alignof(dn_r_CodecIo)==8,"CodecIo ABI alignment");
_Static_assert(offsetof(dn_r_CodecIo,f_count)==0,"CodecIo.count ABI offset");
_Static_assert(offsetof(dn_r_CodecIo,f_next)==8,"CodecIo.next ABI offset");
_Static_assert(offsetof(dn_r_CodecIo,f_error)==16,"CodecIo.error ABI offset");
_Static_assert(sizeof(dn_r_CodecValue)==32,"CodecValue ABI size");
_Static_assert(_Alignof(dn_r_CodecValue)==8,"CodecValue ABI alignment");
_Static_assert(offsetof(dn_r_CodecValue,f_field)==0,"CodecValue.field ABI offset");
_Static_assert(offsetof(dn_r_CodecValue,f_kind)==4,"CodecValue.kind ABI offset");
_Static_assert(offsetof(dn_r_CodecValue,f_scalar)==8,"CodecValue.scalar ABI offset");
_Static_assert(offsetof(dn_r_CodecValue,f_offset)==16,"CodecValue.offset ABI offset");
_Static_assert(offsetof(dn_r_CodecValue,f_count)==24,"CodecValue.count ABI offset");
_Static_assert(sizeof(dn_r_CodecMessage)==80,"CodecMessage ABI size");
_Static_assert(_Alignof(dn_r_CodecMessage)==8,"CodecMessage ABI alignment");
_Static_assert(offsetof(dn_r_CodecMessage,f_message)==0,"CodecMessage.message ABI offset");
_Static_assert(offsetof(dn_r_CodecMessage,f_values)==8,"CodecMessage.values ABI offset");
_Static_assert(offsetof(dn_r_CodecMessage,f_bytes)==32,"CodecMessage.bytes ABI offset");
_Static_assert(offsetof(dn_r_CodecMessage,f_unknown)==56,"CodecMessage.unknown ABI offset");
_Static_assert(sizeof(dn_r_CodecDecoded)==96,"CodecDecoded ABI size");
_Static_assert(_Alignof(dn_r_CodecDecoded)==8,"CodecDecoded ABI alignment");
_Static_assert(offsetof(dn_r_CodecDecoded,f_value)==0,"CodecDecoded.value ABI offset");
_Static_assert(offsetof(dn_r_CodecDecoded,f_error)==80,"CodecDecoded.error ABI offset");
_Static_assert(sizeof(dn_r_CodecScalar)==24,"CodecScalar ABI size");
_Static_assert(_Alignof(dn_r_CodecScalar)==8,"CodecScalar ABI alignment");
_Static_assert(offsetof(dn_r_CodecScalar,f_value)==0,"CodecScalar.value ABI offset");
_Static_assert(offsetof(dn_r_CodecScalar,f_error)==8,"CodecScalar.error ABI offset");
_Static_assert(sizeof(dn_r_CodecSpan)==32,"CodecSpan ABI size");
_Static_assert(_Alignof(dn_r_CodecSpan)==8,"CodecSpan ABI alignment");
_Static_assert(offsetof(dn_r_CodecSpan,f_offset)==0,"CodecSpan.offset ABI offset");
_Static_assert(offsetof(dn_r_CodecSpan,f_count)==8,"CodecSpan.count ABI offset");
_Static_assert(offsetof(dn_r_CodecSpan,f_error)==16,"CodecSpan.error ABI offset");
_Static_assert(sizeof(dn_r_CodecRead)==48,"CodecRead ABI size");
_Static_assert(_Alignof(dn_r_CodecRead)==8,"CodecRead ABI alignment");
_Static_assert(offsetof(dn_r_CodecRead,f_scalar)==0,"CodecRead.scalar ABI offset");
_Static_assert(offsetof(dn_r_CodecRead,f_offset)==8,"CodecRead.offset ABI offset");
_Static_assert(offsetof(dn_r_CodecRead,f_count)==16,"CodecRead.count ABI offset");
_Static_assert(offsetof(dn_r_CodecRead,f_next)==24,"CodecRead.next ABI offset");
_Static_assert(offsetof(dn_r_CodecRead,f_error)==32,"CodecRead.error ABI offset");
_Static_assert(sizeof(dn_r_CodecShape)==440,"CodecShape ABI size");
_Static_assert(_Alignof(dn_r_CodecShape)==8,"CodecShape ABI alignment");
_Static_assert(offsetof(dn_r_CodecShape,f_counts)==0,"CodecShape.counts ABI offset");
_Static_assert(offsetof(dn_r_CodecShape,f_lengths)==128,"CodecShape.lengths ABI offset");
_Static_assert(offsetof(dn_r_CodecShape,f_values)==384,"CodecShape.values ABI offset");
_Static_assert(offsetof(dn_r_CodecShape,f_bytes)==392,"CodecShape.bytes ABI offset");
_Static_assert(offsetof(dn_r_CodecShape,f_unknown)==400,"CodecShape.unknown ABI offset");
_Static_assert(offsetof(dn_r_CodecShape,f_unknown_fields)==408,"CodecShape.unknown_fields ABI offset");
_Static_assert(offsetof(dn_r_CodecShape,f_wire)==416,"CodecShape.wire ABI offset");
_Static_assert(offsetof(dn_r_CodecShape,f_error)==424,"CodecShape.error ABI offset");
_Static_assert(sizeof(dn_r_CodecReservation)==32,"CodecReservation ABI size");
_Static_assert(_Alignof(dn_r_CodecReservation)==8,"CodecReservation ABI alignment");
_Static_assert(offsetof(dn_r_CodecReservation,f_start)==0,"CodecReservation.start ABI offset");
_Static_assert(offsetof(dn_r_CodecReservation,f_count)==8,"CodecReservation.count ABI offset");
_Static_assert(offsetof(dn_r_CodecReservation,f_error)==16,"CodecReservation.error ABI offset");
_Static_assert(sizeof(dn_r_EchoMessage1)==80,"EchoMessage1 ABI size");
_Static_assert(_Alignof(dn_r_EchoMessage1)==8,"EchoMessage1 ABI alignment");
_Static_assert(offsetof(dn_r_EchoMessage1,f_inner)==0,"EchoMessage1.inner ABI offset");
_Static_assert(sizeof(dn_r_EchoMessage1Decoded)==96,"EchoMessage1Decoded ABI size");
_Static_assert(_Alignof(dn_r_EchoMessage1Decoded)==8,"EchoMessage1Decoded ABI alignment");
_Static_assert(offsetof(dn_r_EchoMessage1Decoded,f_value)==0,"EchoMessage1Decoded.value ABI offset");
_Static_assert(offsetof(dn_r_EchoMessage1Decoded,f_error)==80,"EchoMessage1Decoded.error ABI offset");
_Static_assert(sizeof(dn_r_EchoMessage1Field1)==24,"EchoMessage1Field1 ABI size");
_Static_assert(_Alignof(dn_r_EchoMessage1Field1)==8,"EchoMessage1Field1 ABI alignment");
_Static_assert(offsetof(dn_r_EchoMessage1Field1,f_value)==0,"EchoMessage1Field1.value ABI offset");
_Static_assert(offsetof(dn_r_EchoMessage1Field1,f_error)==8,"EchoMessage1Field1.error ABI offset");
_Static_assert(sizeof(dn_r_EchoMessage1Field2)==24,"EchoMessage1Field2 ABI size");
_Static_assert(_Alignof(dn_r_EchoMessage1Field2)==8,"EchoMessage1Field2 ABI alignment");
_Static_assert(offsetof(dn_r_EchoMessage1Field2,f_value)==0,"EchoMessage1Field2.value ABI offset");
_Static_assert(offsetof(dn_r_EchoMessage1Field2,f_error)==8,"EchoMessage1Field2.error ABI offset");
_Static_assert(sizeof(dn_r_EchoMessage1Field3)==24,"EchoMessage1Field3 ABI size");
_Static_assert(_Alignof(dn_r_EchoMessage1Field3)==8,"EchoMessage1Field3 ABI alignment");
_Static_assert(offsetof(dn_r_EchoMessage1Field3,f_value)==0,"EchoMessage1Field3.value ABI offset");
_Static_assert(offsetof(dn_r_EchoMessage1Field3,f_error)==8,"EchoMessage1Field3.error ABI offset");
_Static_assert(sizeof(dn_r_EchoMessage1Field4)==24,"EchoMessage1Field4 ABI size");
_Static_assert(_Alignof(dn_r_EchoMessage1Field4)==8,"EchoMessage1Field4 ABI alignment");
_Static_assert(offsetof(dn_r_EchoMessage1Field4,f_value)==0,"EchoMessage1Field4.value ABI offset");
_Static_assert(offsetof(dn_r_EchoMessage1Field4,f_error)==8,"EchoMessage1Field4.error ABI offset");
_Static_assert(sizeof(dn_r_EchoMessage1Field5)==24,"EchoMessage1Field5 ABI size");
_Static_assert(_Alignof(dn_r_EchoMessage1Field5)==8,"EchoMessage1Field5 ABI alignment");
_Static_assert(offsetof(dn_r_EchoMessage1Field5,f_value)==0,"EchoMessage1Field5.value ABI offset");
_Static_assert(offsetof(dn_r_EchoMessage1Field5,f_error)==8,"EchoMessage1Field5.error ABI offset");
_Static_assert(sizeof(dn_r_EchoMessage1Field8)==24,"EchoMessage1Field8 ABI size");
_Static_assert(_Alignof(dn_r_EchoMessage1Field8)==8,"EchoMessage1Field8 ABI alignment");
_Static_assert(offsetof(dn_r_EchoMessage1Field8,f_value)==0,"EchoMessage1Field8.value ABI offset");
_Static_assert(offsetof(dn_r_EchoMessage1Field8,f_error)==8,"EchoMessage1Field8.error ABI offset");
_Static_assert(sizeof(dn_r_EchoMessage1Field9)==24,"EchoMessage1Field9 ABI size");
_Static_assert(_Alignof(dn_r_EchoMessage1Field9)==8,"EchoMessage1Field9 ABI alignment");
_Static_assert(offsetof(dn_r_EchoMessage1Field9,f_value)==0,"EchoMessage1Field9.value ABI offset");
_Static_assert(offsetof(dn_r_EchoMessage1Field9,f_error)==8,"EchoMessage1Field9.error ABI offset");
_Static_assert(sizeof(dn_r_EchoMessage1Field10)==24,"EchoMessage1Field10 ABI size");
_Static_assert(_Alignof(dn_r_EchoMessage1Field10)==8,"EchoMessage1Field10 ABI alignment");
_Static_assert(offsetof(dn_r_EchoMessage1Field10,f_value)==0,"EchoMessage1Field10.value ABI offset");
_Static_assert(offsetof(dn_r_EchoMessage1Field10,f_error)==8,"EchoMessage1Field10.error ABI offset");
_Static_assert(sizeof(dn_r_EchoMessage2)==80,"EchoMessage2 ABI size");
_Static_assert(_Alignof(dn_r_EchoMessage2)==8,"EchoMessage2 ABI alignment");
_Static_assert(offsetof(dn_r_EchoMessage2,f_inner)==0,"EchoMessage2.inner ABI offset");
_Static_assert(sizeof(dn_r_EchoMessage2Decoded)==96,"EchoMessage2Decoded ABI size");
_Static_assert(_Alignof(dn_r_EchoMessage2Decoded)==8,"EchoMessage2Decoded ABI alignment");
_Static_assert(offsetof(dn_r_EchoMessage2Decoded,f_value)==0,"EchoMessage2Decoded.value ABI offset");
_Static_assert(offsetof(dn_r_EchoMessage2Decoded,f_error)==80,"EchoMessage2Decoded.error ABI offset");
_Static_assert(sizeof(dn_r_PosMessage1)==80,"PosMessage1 ABI size");
_Static_assert(_Alignof(dn_r_PosMessage1)==8,"PosMessage1 ABI alignment");
_Static_assert(offsetof(dn_r_PosMessage1,f_inner)==0,"PosMessage1.inner ABI offset");
_Static_assert(sizeof(dn_r_PosMessage1Decoded)==96,"PosMessage1Decoded ABI size");
_Static_assert(_Alignof(dn_r_PosMessage1Decoded)==8,"PosMessage1Decoded ABI alignment");
_Static_assert(offsetof(dn_r_PosMessage1Decoded,f_value)==0,"PosMessage1Decoded.value ABI offset");
_Static_assert(offsetof(dn_r_PosMessage1Decoded,f_error)==80,"PosMessage1Decoded.error ABI offset");
_Static_assert(sizeof(dn_r_PosMessage1Field1)==24,"PosMessage1Field1 ABI size");
_Static_assert(_Alignof(dn_r_PosMessage1Field1)==8,"PosMessage1Field1 ABI alignment");
_Static_assert(offsetof(dn_r_PosMessage1Field1,f_value)==0,"PosMessage1Field1.value ABI offset");
_Static_assert(offsetof(dn_r_PosMessage1Field1,f_error)==8,"PosMessage1Field1.error ABI offset");
_Static_assert(sizeof(dn_r_PosMessage1Field2)==24,"PosMessage1Field2 ABI size");
_Static_assert(_Alignof(dn_r_PosMessage1Field2)==8,"PosMessage1Field2 ABI alignment");
_Static_assert(offsetof(dn_r_PosMessage1Field2,f_value)==0,"PosMessage1Field2.value ABI offset");
_Static_assert(offsetof(dn_r_PosMessage1Field2,f_error)==8,"PosMessage1Field2.error ABI offset");
_Static_assert(sizeof(dn_r_PosMessage1Field3)==24,"PosMessage1Field3 ABI size");
_Static_assert(_Alignof(dn_r_PosMessage1Field3)==8,"PosMessage1Field3 ABI alignment");
_Static_assert(offsetof(dn_r_PosMessage1Field3,f_value)==0,"PosMessage1Field3.value ABI offset");
_Static_assert(offsetof(dn_r_PosMessage1Field3,f_error)==8,"PosMessage1Field3.error ABI offset");
_Static_assert(sizeof(dn_r_PosMessage1Field4)==24,"PosMessage1Field4 ABI size");
_Static_assert(_Alignof(dn_r_PosMessage1Field4)==8,"PosMessage1Field4 ABI alignment");
_Static_assert(offsetof(dn_r_PosMessage1Field4,f_value)==0,"PosMessage1Field4.value ABI offset");
_Static_assert(offsetof(dn_r_PosMessage1Field4,f_error)==8,"PosMessage1Field4.error ABI offset");
_Static_assert(sizeof(dn_r_PosMessage1Field5)==24,"PosMessage1Field5 ABI size");
_Static_assert(_Alignof(dn_r_PosMessage1Field5)==8,"PosMessage1Field5 ABI alignment");
_Static_assert(offsetof(dn_r_PosMessage1Field5,f_value)==0,"PosMessage1Field5.value ABI offset");
_Static_assert(offsetof(dn_r_PosMessage1Field5,f_error)==8,"PosMessage1Field5.error ABI offset");
_Static_assert(sizeof(dn_r_PosMessage1Field8)==24,"PosMessage1Field8 ABI size");
_Static_assert(_Alignof(dn_r_PosMessage1Field8)==8,"PosMessage1Field8 ABI alignment");
_Static_assert(offsetof(dn_r_PosMessage1Field8,f_value)==0,"PosMessage1Field8.value ABI offset");
_Static_assert(offsetof(dn_r_PosMessage1Field8,f_error)==8,"PosMessage1Field8.error ABI offset");
_Static_assert(sizeof(dn_r_PosMessage1Field9)==24,"PosMessage1Field9 ABI size");
_Static_assert(_Alignof(dn_r_PosMessage1Field9)==8,"PosMessage1Field9 ABI alignment");
_Static_assert(offsetof(dn_r_PosMessage1Field9,f_value)==0,"PosMessage1Field9.value ABI offset");
_Static_assert(offsetof(dn_r_PosMessage1Field9,f_error)==8,"PosMessage1Field9.error ABI offset");
_Static_assert(sizeof(dn_r_PosMessage2)==80,"PosMessage2 ABI size");
_Static_assert(_Alignof(dn_r_PosMessage2)==8,"PosMessage2 ABI alignment");
_Static_assert(offsetof(dn_r_PosMessage2,f_inner)==0,"PosMessage2.inner ABI offset");
_Static_assert(sizeof(dn_r_PosMessage2Decoded)==96,"PosMessage2Decoded ABI size");
_Static_assert(_Alignof(dn_r_PosMessage2Decoded)==8,"PosMessage2Decoded ABI alignment");
_Static_assert(offsetof(dn_r_PosMessage2Decoded,f_value)==0,"PosMessage2Decoded.value ABI offset");
_Static_assert(offsetof(dn_r_PosMessage2Decoded,f_error)==80,"PosMessage2Decoded.error ABI offset");
_Static_assert(sizeof(dn_r_LittleMessage1)==80,"LittleMessage1 ABI size");
_Static_assert(_Alignof(dn_r_LittleMessage1)==8,"LittleMessage1 ABI alignment");
_Static_assert(offsetof(dn_r_LittleMessage1,f_inner)==0,"LittleMessage1.inner ABI offset");
_Static_assert(sizeof(dn_r_LittleMessage1Decoded)==96,"LittleMessage1Decoded ABI size");
_Static_assert(_Alignof(dn_r_LittleMessage1Decoded)==8,"LittleMessage1Decoded ABI alignment");
_Static_assert(offsetof(dn_r_LittleMessage1Decoded,f_value)==0,"LittleMessage1Decoded.value ABI offset");
_Static_assert(offsetof(dn_r_LittleMessage1Decoded,f_error)==80,"LittleMessage1Decoded.error ABI offset");
_Static_assert(sizeof(dn_r_LittleMessage1Field1)==24,"LittleMessage1Field1 ABI size");
_Static_assert(_Alignof(dn_r_LittleMessage1Field1)==8,"LittleMessage1Field1 ABI alignment");
_Static_assert(offsetof(dn_r_LittleMessage1Field1,f_value)==0,"LittleMessage1Field1.value ABI offset");
_Static_assert(offsetof(dn_r_LittleMessage1Field1,f_error)==8,"LittleMessage1Field1.error ABI offset");
_Static_assert(sizeof(dn_r_LittleMessage1Field2)==24,"LittleMessage1Field2 ABI size");
_Static_assert(_Alignof(dn_r_LittleMessage1Field2)==8,"LittleMessage1Field2 ABI alignment");
_Static_assert(offsetof(dn_r_LittleMessage1Field2,f_value)==0,"LittleMessage1Field2.value ABI offset");
_Static_assert(offsetof(dn_r_LittleMessage1Field2,f_error)==8,"LittleMessage1Field2.error ABI offset");
_Static_assert(sizeof(dn_r_LittleMessage1Field3)==24,"LittleMessage1Field3 ABI size");
_Static_assert(_Alignof(dn_r_LittleMessage1Field3)==8,"LittleMessage1Field3 ABI alignment");
_Static_assert(offsetof(dn_r_LittleMessage1Field3,f_value)==0,"LittleMessage1Field3.value ABI offset");
_Static_assert(offsetof(dn_r_LittleMessage1Field3,f_error)==8,"LittleMessage1Field3.error ABI offset");
_Static_assert(sizeof(dn_r_LittleMessage1Field4)==24,"LittleMessage1Field4 ABI size");
_Static_assert(_Alignof(dn_r_LittleMessage1Field4)==8,"LittleMessage1Field4 ABI alignment");
_Static_assert(offsetof(dn_r_LittleMessage1Field4,f_value)==0,"LittleMessage1Field4.value ABI offset");
_Static_assert(offsetof(dn_r_LittleMessage1Field4,f_error)==8,"LittleMessage1Field4.error ABI offset");
_Static_assert(sizeof(dn_r_LittleMessage1Field5)==24,"LittleMessage1Field5 ABI size");
_Static_assert(_Alignof(dn_r_LittleMessage1Field5)==8,"LittleMessage1Field5 ABI alignment");
_Static_assert(offsetof(dn_r_LittleMessage1Field5,f_value)==0,"LittleMessage1Field5.value ABI offset");
_Static_assert(offsetof(dn_r_LittleMessage1Field5,f_error)==8,"LittleMessage1Field5.error ABI offset");
_Static_assert(sizeof(dn_r_LittleMessage1Field8)==24,"LittleMessage1Field8 ABI size");
_Static_assert(_Alignof(dn_r_LittleMessage1Field8)==8,"LittleMessage1Field8 ABI alignment");
_Static_assert(offsetof(dn_r_LittleMessage1Field8,f_value)==0,"LittleMessage1Field8.value ABI offset");
_Static_assert(offsetof(dn_r_LittleMessage1Field8,f_error)==8,"LittleMessage1Field8.error ABI offset");
_Static_assert(sizeof(dn_r_LittleMessage1Field9)==24,"LittleMessage1Field9 ABI size");
_Static_assert(_Alignof(dn_r_LittleMessage1Field9)==8,"LittleMessage1Field9 ABI alignment");
_Static_assert(offsetof(dn_r_LittleMessage1Field9,f_value)==0,"LittleMessage1Field9.value ABI offset");
_Static_assert(offsetof(dn_r_LittleMessage1Field9,f_error)==8,"LittleMessage1Field9.error ABI offset");
_Static_assert(sizeof(dn_r_LittleMessage2)==80,"LittleMessage2 ABI size");
_Static_assert(_Alignof(dn_r_LittleMessage2)==8,"LittleMessage2 ABI alignment");
_Static_assert(offsetof(dn_r_LittleMessage2,f_inner)==0,"LittleMessage2.inner ABI offset");
_Static_assert(sizeof(dn_r_LittleMessage2Decoded)==96,"LittleMessage2Decoded ABI size");
_Static_assert(_Alignof(dn_r_LittleMessage2Decoded)==8,"LittleMessage2Decoded ABI alignment");
_Static_assert(offsetof(dn_r_LittleMessage2Decoded,f_value)==0,"LittleMessage2Decoded.value ABI offset");
_Static_assert(offsetof(dn_r_LittleMessage2Decoded,f_error)==80,"LittleMessage2Decoded.error ABI offset");
static void dn_drop_r12_CodecDecoded(dn_r_CodecDecoded volatile*);static void dn_retain_r12_CodecDecoded(dn_r_CodecDecoded volatile*);
static void dn_drop_r12_CodecMessage(dn_r_CodecMessage volatile*);static void dn_retain_r12_CodecMessage(dn_r_CodecMessage volatile*);
static void dn_drop_r12_EchoMessage1(dn_r_EchoMessage1 volatile*);static void dn_retain_r12_EchoMessage1(dn_r_EchoMessage1 volatile*);
static void dn_drop_r19_EchoMessage1Decoded(dn_r_EchoMessage1Decoded volatile*);static void dn_retain_r19_EchoMessage1Decoded(dn_r_EchoMessage1Decoded volatile*);
static void dn_drop_r12_EchoMessage2(dn_r_EchoMessage2 volatile*);static void dn_retain_r12_EchoMessage2(dn_r_EchoMessage2 volatile*);
static void dn_drop_r19_EchoMessage2Decoded(dn_r_EchoMessage2Decoded volatile*);static void dn_retain_r19_EchoMessage2Decoded(dn_r_EchoMessage2Decoded volatile*);
static void dn_drop_r14_LittleMessage1(dn_r_LittleMessage1 volatile*);static void dn_retain_r14_LittleMessage1(dn_r_LittleMessage1 volatile*);
static void dn_drop_r21_LittleMessage1Decoded(dn_r_LittleMessage1Decoded volatile*);static void dn_retain_r21_LittleMessage1Decoded(dn_r_LittleMessage1Decoded volatile*);
static void dn_drop_r14_LittleMessage2(dn_r_LittleMessage2 volatile*);static void dn_retain_r14_LittleMessage2(dn_r_LittleMessage2 volatile*);
static void dn_drop_r21_LittleMessage2Decoded(dn_r_LittleMessage2Decoded volatile*);static void dn_retain_r21_LittleMessage2Decoded(dn_r_LittleMessage2Decoded volatile*);
static void dn_drop_r11_PosMessage1(dn_r_PosMessage1 volatile*);static void dn_retain_r11_PosMessage1(dn_r_PosMessage1 volatile*);
static void dn_drop_r18_PosMessage1Decoded(dn_r_PosMessage1Decoded volatile*);static void dn_retain_r18_PosMessage1Decoded(dn_r_PosMessage1Decoded volatile*);
static void dn_drop_r11_PosMessage2(dn_r_PosMessage2 volatile*);static void dn_retain_r11_PosMessage2(dn_r_PosMessage2 volatile*);
static void dn_drop_r18_PosMessage2Decoded(dn_r_PosMessage2Decoded volatile*);static void dn_retain_r18_PosMessage2Decoded(dn_r_PosMessage2Decoded volatile*);
static void dn_drop_buf_U8(dn_t_buf_U8 volatile*);static void dn_retain_buf_U8(dn_t_buf_U8 volatile*);
static void dn_drop_buf_U32(dn_t_buf_U32 volatile*);static void dn_retain_buf_U32(dn_t_buf_U32 volatile*);
static void dn_drop_buf_U64(dn_t_buf_U64 volatile*);static void dn_retain_buf_U64(dn_t_buf_U64 volatile*);
static void dn_drop_buf_I64(dn_t_buf_I64 volatile*);static void dn_retain_buf_I64(dn_t_buf_I64 volatile*);
static void dn_drop_buf_r10_CodecValue(dn_t_buf_r10_CodecValue volatile*);static void dn_retain_buf_r10_CodecValue(dn_t_buf_r10_CodecValue volatile*);
static void dn_adopt_r12_CodecDecoded(dn_r_CodecDecoded volatile*);
static void dn_adopt_r12_CodecMessage(dn_r_CodecMessage volatile*);
static void dn_adopt_r12_EchoMessage1(dn_r_EchoMessage1 volatile*);
static void dn_adopt_r19_EchoMessage1Decoded(dn_r_EchoMessage1Decoded volatile*);
static void dn_adopt_r12_EchoMessage2(dn_r_EchoMessage2 volatile*);
static void dn_adopt_r19_EchoMessage2Decoded(dn_r_EchoMessage2Decoded volatile*);
static void dn_adopt_r14_LittleMessage1(dn_r_LittleMessage1 volatile*);
static void dn_adopt_r21_LittleMessage1Decoded(dn_r_LittleMessage1Decoded volatile*);
static void dn_adopt_r14_LittleMessage2(dn_r_LittleMessage2 volatile*);
static void dn_adopt_r21_LittleMessage2Decoded(dn_r_LittleMessage2Decoded volatile*);
static void dn_adopt_r11_PosMessage1(dn_r_PosMessage1 volatile*);
static void dn_adopt_r18_PosMessage1Decoded(dn_r_PosMessage1Decoded volatile*);
static void dn_adopt_r11_PosMessage2(dn_r_PosMessage2 volatile*);
static void dn_adopt_r18_PosMessage2Decoded(dn_r_PosMessage2Decoded volatile*);
static void dn_adopt_buf_U8(dn_t_buf_U8 volatile*);
static void dn_adopt_buf_U32(dn_t_buf_U32 volatile*);
static void dn_adopt_buf_U64(dn_t_buf_U64 volatile*);
static void dn_adopt_buf_I64(dn_t_buf_I64 volatile*);
static void dn_adopt_buf_r10_CodecValue(dn_t_buf_r10_CodecValue volatile*);
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
static dn_r_MessageField dn_f_network_field(uint32_t v_id,uint32_t v_kind,uint32_t v_minimum,uint32_t v_maximum,uint32_t v_length);
static dn_r_MessageSchema dn_f_message_schema(uint32_t v_id,uint32_t v_phase,uint32_t v_senders);
static dn_r_NetworkSpec dn_f_network_spec(uint32_t v_protocol,uint32_t v_major,uint32_t v_minor,uint32_t v_minimum);
static int32_t dn_f_message_add_field(dn_r_MessageSchema * v_message,dn_r_MessageField v_field);
static int32_t dn_f_network_add_message(dn_r_NetworkSpec * v_spec,dn_r_MessageSchema v_message);
static bool dn_f_network_minor_active(uint32_t v_first,uint32_t v_last,uint32_t v_minor);
static int32_t dn_f_network_field_check(dn_r_MessageField const * v_field);
static int32_t dn_f_network_wire_check(dn_r_WireFormat v_wire);
static dn_r_SchemaMeasure dn_f_network_field_measure(dn_r_MessageField const * v_field,dn_r_WireFormat v_wire);
static dn_r_SchemaMeasure dn_f_network_message_measure(dn_r_MessageSchema const * v_message,dn_r_WireFormat v_wire,uint32_t v_minor);
static dn_r_NetworkSpecCheck dn_f_network_spec_failure(int32_t v_status,uint32_t v_component,uint32_t v_message,uint32_t v_field);
static bool dn_f_network_has_message(dn_r_NetworkSpec const * v_spec,uint32_t v_minor);
static dn_r_NetworkSpecCheck dn_f_network_spec_check(dn_r_NetworkSpec const * v_spec);
static dn_r_NetworkAdmission dn_f_network_spec_select(dn_r_NetworkSpec const * v_spec,dn_t_arr6_r18_TransportCandidate v_candidates);
static dn_utf8 dn_f_network_obligation_name(uint32_t v_bit);
static dn_r_ProtocolValue dn_f_protocol_read_be(dn_t_slice0_U8 v_bytes,size_t v_offset,size_t v_width);
static int32_t dn_f_protocol_write_be(dn_t_buf_U8 * v_bytes,size_t v_offset,size_t v_width,uint64_t v_value);
static dn_r_ProtocolValue dn_f_quic_varint_read(dn_t_slice0_U8 v_bytes,size_t v_offset);
static size_t dn_f_quic_varint_size(uint64_t v_value);
static dn_r_ProtocolValue dn_f_quic_varint_write(dn_t_buf_U8 * v_bytes,size_t v_offset,uint64_t v_value);
static dn_r_ProtocolFrame dn_f_protocol_frame32(dn_t_slice0_U8 v_bytes,size_t v_offset,size_t v_maximum);
static dn_r_ProtocolValue dn_f_net_read_le(dn_t_slice0_U8 v_bytes,size_t v_offset,size_t v_width);
static int32_t dn_f_net_write_le(dn_t_buf_U8 * v_bytes,size_t v_offset,size_t v_width,uint64_t v_value);
static dn_r_ProtocolValue dn_f_net_read_be(dn_t_slice0_U8 v_bytes,size_t v_offset,size_t v_width);
static int32_t dn_f_net_write_be(dn_t_buf_U8 * v_bytes,size_t v_offset,size_t v_width,uint64_t v_value);
static size_t dn_f_uleb128_size(uint64_t v_value);
static dn_r_ProtocolValue dn_f_uleb128_read(dn_t_slice0_U8 v_bytes,size_t v_offset,bool v_canonical);
static dn_r_ProtocolValue dn_f_uleb128_write(dn_t_buf_U8 * v_bytes,size_t v_offset,uint64_t v_value);
static uint64_t dn_f_zigzag_encode(int64_t v_value);
static int64_t dn_f_zigzag_decode(uint64_t v_value);
static dn_t_buf_U8 dn_f_wire_new(uint16_t v_version);
static bool dn_f_wire_valid(dn_t_buf_U8 const * v_bytes);
static size_t dn_f_wire_find(dn_t_buf_U8 const * v_bytes,uint32_t v_id);
static dn_t_slice0_U8 dn_f_wire_field(dn_t_buf_U8 const * v_bytes,uint32_t v_id);
static uint8_t dn_f_wire_set(dn_t_buf_U8 * v_bytes,uint32_t v_id,dn_t_slice0_U8 v_payload);
static uint8_t dn_f_wire_upgrade(dn_t_buf_U8 * v_bytes);
static dn_r_Utf8Step dn_f_utf8_next(dn_t_slice0_U8 v_bytes,size_t v_offset);
static dn_r_CodecError dn_f_codec_error(int32_t v_kind,uint32_t v_field,size_t v_offset);
static dn_r_CodecShape dn_f_codec_shape_error(int32_t v_kind,uint32_t v_field,size_t v_offset);
static dn_r_CodecError dn_f_codec_plan_check(dn_r_CodecPlan const * v_plan);
static size_t dn_f_codec_find(dn_r_CodecPlan const * v_plan,uint32_t v_field);
static bool dn_f_codec_storage(dn_r_CodecPlan const * v_plan,size_t v_values,size_t v_bytes,size_t v_unknown);
static bool dn_f_codec_scalar_valid(uint32_t v_kind,uint64_t v_value);
static size_t dn_f_codec_scalar_size(uint32_t v_kind,uint64_t v_value);
static bool dn_f_codec_utf8(dn_t_slice0_U8 v_input,size_t v_offset,size_t v_count);
static dn_r_CodecRead dn_f_codec_read_error(int32_t v_kind,uint32_t v_field,size_t v_offset);
static dn_r_ProtocolValue dn_f_codec_read_integer(dn_r_CodecPlan const * v_plan,dn_t_slice0_U8 v_input,size_t v_offset,size_t v_width);
static dn_r_CodecRead dn_f_codec_read_value(dn_r_CodecPlan const * v_plan,dn_r_MessageField const * v_field,dn_t_slice0_U8 v_input,size_t v_offset,size_t v_end);
static dn_r_CodecShape dn_f_codec_unknown_shape(dn_r_CodecPlan const * v_plan,dn_t_slice0_U8 v_input);
static dn_r_CodecError dn_f_codec_shape_complete(dn_r_CodecPlan const * v_plan,dn_r_CodecShape const * v_shape);
static dn_r_CodecShape dn_f_codec_scan(dn_r_CodecPlan const * v_plan,dn_t_slice0_U8 v_input);
static dn_r_CodecMessage dn_f_codec_message(uint32_t v_id);
static dn_r_CodecDecoded dn_f_codec_decode_error(int32_t v_kind,uint32_t v_field,size_t v_offset);
static dn_r_CodecDecoded dn_f_codec_decode(dn_r_CodecPlan const * v_plan,dn_t_slice0_U8 v_input);
static dn_r_CodecShape dn_f_codec_message_inspect(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,bool v_complete);
static dn_r_CodecIo dn_f_codec_measure(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message);
static dn_r_CodecReservation dn_f_codec_reserve_scalars(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage * v_message,uint32_t v_field_id,size_t v_count);
static dn_r_CodecError dn_f_codec_add_scalar(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage * v_message,uint32_t v_field_id,uint64_t v_scalar);
static dn_r_CodecError dn_f_codec_add_bytes(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage * v_message,uint32_t v_field_id,dn_t_slice0_U8 v_input);
static dn_r_CodecError dn_f_codec_add_unknown(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage * v_message,dn_t_slice0_U8 v_input);
static bool dn_f_codec_unknown_over_budget(dn_r_CodecPlan const * v_plan,size_t v_used,size_t v_count);
static dn_r_CodecValue dn_f_codec_get(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,uint32_t v_field_id,size_t v_occurrence);
static dn_r_CodecScalar dn_f_codec_get_scalar(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,uint32_t v_field_id,size_t v_occurrence);
static dn_r_CodecSpan dn_f_codec_get_span(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,uint32_t v_field_id,size_t v_occurrence);
static dn_t_slice0_U8 dn_f_codec_view(dn_r_CodecMessage const * v_message,dn_r_CodecSpan v_span);
static int32_t dn_f_codec_write_integer(dn_r_CodecPlan const * v_plan,dn_t_buf_U8 * v_output,size_t v_offset,size_t v_width,uint64_t v_value);
static dn_r_CodecIo dn_f_codec_encode(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,dn_t_buf_U8 * v_output,size_t v_offset);
static bool dn_f_codec_equal(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_left,dn_r_CodecMessage const * v_right);
static dn_r_NetworkSpec dn_f_Echo_spec(void);
static dn_r_CodecPlan dn_f_Echo_plan_1(void);
static dn_r_EchoMessage1 dn_f_EchoMessage1_new(void);
static dn_r_EchoMessage1Decoded dn_f_EchoMessage1_decode(dn_t_slice0_U8 v_input);
static dn_r_CodecIo dn_f_EchoMessage1_measure(dn_r_EchoMessage1 const * v_message);
static dn_r_CodecIo dn_f_EchoMessage1_encode(dn_r_EchoMessage1 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset);
static bool dn_f_EchoMessage1_equal(dn_r_EchoMessage1 const * v_left,dn_r_EchoMessage1 const * v_right);
static dn_r_CodecError dn_f_EchoMessage1_field_1_add(dn_r_EchoMessage1 * v_message,uint8_t v_value);
static dn_r_EchoMessage1Field1 dn_f_EchoMessage1_field_1_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_EchoMessage1_field_1_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U8 v_values);
static dn_r_CodecError dn_f_EchoMessage1_field_2_add(dn_r_EchoMessage1 * v_message,uint16_t v_value);
static dn_r_EchoMessage1Field2 dn_f_EchoMessage1_field_2_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_EchoMessage1_field_2_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U16 v_values);
static dn_r_CodecError dn_f_EchoMessage1_field_3_add(dn_r_EchoMessage1 * v_message,uint32_t v_value);
static dn_r_EchoMessage1Field3 dn_f_EchoMessage1_field_3_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_EchoMessage1_field_3_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U32 v_values);
static dn_r_CodecError dn_f_EchoMessage1_field_4_add(dn_r_EchoMessage1 * v_message,uint64_t v_value);
static dn_r_EchoMessage1Field4 dn_f_EchoMessage1_field_4_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_EchoMessage1_field_4_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U64 v_values);
static dn_r_CodecError dn_f_EchoMessage1_field_5_add(dn_r_EchoMessage1 * v_message,bool v_value);
static dn_r_EchoMessage1Field5 dn_f_EchoMessage1_field_5_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_EchoMessage1_field_5_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_b v_values);
static dn_r_CodecError dn_f_EchoMessage1_field_6_add(dn_r_EchoMessage1 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecSpan dn_f_EchoMessage1_field_6_span(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_t_slice0_U8 dn_f_EchoMessage1_field_6_view(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_EchoMessage1_field_7_add(dn_r_EchoMessage1 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecSpan dn_f_EchoMessage1_field_7_span(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_t_slice0_U8 dn_f_EchoMessage1_field_7_view(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_EchoMessage1_field_8_add(dn_r_EchoMessage1 * v_message,uint64_t v_value);
static dn_r_EchoMessage1Field8 dn_f_EchoMessage1_field_8_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_EchoMessage1_field_8_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U64 v_values);
static dn_r_CodecError dn_f_EchoMessage1_field_9_add(dn_r_EchoMessage1 * v_message,int64_t v_value);
static dn_r_EchoMessage1Field9 dn_f_EchoMessage1_field_9_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_EchoMessage1_field_9_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_I64 v_values);
static dn_r_CodecError dn_f_EchoMessage1_field_10_add(dn_r_EchoMessage1 * v_message,uint16_t v_value);
static dn_r_EchoMessage1Field10 dn_f_EchoMessage1_field_10_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_EchoMessage1_field_10_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U16 v_values);
static dn_r_CodecError dn_f_EchoMessage1_add_unknown(dn_r_EchoMessage1 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecPlan dn_f_Echo_plan_2(void);
static dn_r_EchoMessage2 dn_f_EchoMessage2_new(void);
static dn_r_EchoMessage2Decoded dn_f_EchoMessage2_decode(dn_t_slice0_U8 v_input);
static dn_r_CodecIo dn_f_EchoMessage2_measure(dn_r_EchoMessage2 const * v_message);
static dn_r_CodecIo dn_f_EchoMessage2_encode(dn_r_EchoMessage2 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset);
static bool dn_f_EchoMessage2_equal(dn_r_EchoMessage2 const * v_left,dn_r_EchoMessage2 const * v_right);
static dn_r_CodecError dn_f_EchoMessage2_add_unknown(dn_r_EchoMessage2 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecDecoded dn_f_Echo_decode(dn_t_slice0_U8 v_input);
static dn_r_NetworkSpec dn_f_Pos_spec(void);
static dn_r_CodecPlan dn_f_Pos_plan_1(void);
static dn_r_PosMessage1 dn_f_PosMessage1_new(void);
static dn_r_PosMessage1Decoded dn_f_PosMessage1_decode(dn_t_slice0_U8 v_input);
static dn_r_CodecIo dn_f_PosMessage1_measure(dn_r_PosMessage1 const * v_message);
static dn_r_CodecIo dn_f_PosMessage1_encode(dn_r_PosMessage1 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset);
static bool dn_f_PosMessage1_equal(dn_r_PosMessage1 const * v_left,dn_r_PosMessage1 const * v_right);
static dn_r_CodecError dn_f_PosMessage1_field_1_add(dn_r_PosMessage1 * v_message,uint8_t v_value);
static dn_r_PosMessage1Field1 dn_f_PosMessage1_field_1_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_PosMessage1_field_1_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U8 v_values);
static dn_r_CodecError dn_f_PosMessage1_field_2_add(dn_r_PosMessage1 * v_message,uint16_t v_value);
static dn_r_PosMessage1Field2 dn_f_PosMessage1_field_2_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_PosMessage1_field_2_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U16 v_values);
static dn_r_CodecError dn_f_PosMessage1_field_3_add(dn_r_PosMessage1 * v_message,uint32_t v_value);
static dn_r_PosMessage1Field3 dn_f_PosMessage1_field_3_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_PosMessage1_field_3_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U32 v_values);
static dn_r_CodecError dn_f_PosMessage1_field_4_add(dn_r_PosMessage1 * v_message,uint64_t v_value);
static dn_r_PosMessage1Field4 dn_f_PosMessage1_field_4_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_PosMessage1_field_4_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U64 v_values);
static dn_r_CodecError dn_f_PosMessage1_field_5_add(dn_r_PosMessage1 * v_message,bool v_value);
static dn_r_PosMessage1Field5 dn_f_PosMessage1_field_5_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_PosMessage1_field_5_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_b v_values);
static dn_r_CodecError dn_f_PosMessage1_field_6_add(dn_r_PosMessage1 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecSpan dn_f_PosMessage1_field_6_span(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_t_slice0_U8 dn_f_PosMessage1_field_6_view(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_PosMessage1_field_7_add(dn_r_PosMessage1 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecSpan dn_f_PosMessage1_field_7_span(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_t_slice0_U8 dn_f_PosMessage1_field_7_view(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_PosMessage1_field_8_add(dn_r_PosMessage1 * v_message,uint64_t v_value);
static dn_r_PosMessage1Field8 dn_f_PosMessage1_field_8_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_PosMessage1_field_8_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U64 v_values);
static dn_r_CodecError dn_f_PosMessage1_field_9_add(dn_r_PosMessage1 * v_message,int64_t v_value);
static dn_r_PosMessage1Field9 dn_f_PosMessage1_field_9_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_PosMessage1_field_9_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_I64 v_values);
static dn_r_CodecError dn_f_PosMessage1_field_11_add(dn_r_PosMessage1 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecSpan dn_f_PosMessage1_field_11_span(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_t_slice0_U8 dn_f_PosMessage1_field_11_view(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecPlan dn_f_Pos_plan_2(void);
static dn_r_PosMessage2 dn_f_PosMessage2_new(void);
static dn_r_PosMessage2Decoded dn_f_PosMessage2_decode(dn_t_slice0_U8 v_input);
static dn_r_CodecIo dn_f_PosMessage2_measure(dn_r_PosMessage2 const * v_message);
static dn_r_CodecIo dn_f_PosMessage2_encode(dn_r_PosMessage2 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset);
static bool dn_f_PosMessage2_equal(dn_r_PosMessage2 const * v_left,dn_r_PosMessage2 const * v_right);
static dn_r_CodecDecoded dn_f_Pos_decode(dn_t_slice0_U8 v_input);
static dn_r_NetworkSpec dn_f_Little_spec(void);
static dn_r_CodecPlan dn_f_Little_plan_1(void);
static dn_r_LittleMessage1 dn_f_LittleMessage1_new(void);
static dn_r_LittleMessage1Decoded dn_f_LittleMessage1_decode(dn_t_slice0_U8 v_input);
static dn_r_CodecIo dn_f_LittleMessage1_measure(dn_r_LittleMessage1 const * v_message);
static dn_r_CodecIo dn_f_LittleMessage1_encode(dn_r_LittleMessage1 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset);
static bool dn_f_LittleMessage1_equal(dn_r_LittleMessage1 const * v_left,dn_r_LittleMessage1 const * v_right);
static dn_r_CodecError dn_f_LittleMessage1_field_1_add(dn_r_LittleMessage1 * v_message,uint8_t v_value);
static dn_r_LittleMessage1Field1 dn_f_LittleMessage1_field_1_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_LittleMessage1_field_1_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U8 v_values);
static dn_r_CodecError dn_f_LittleMessage1_field_2_add(dn_r_LittleMessage1 * v_message,uint16_t v_value);
static dn_r_LittleMessage1Field2 dn_f_LittleMessage1_field_2_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_LittleMessage1_field_2_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U16 v_values);
static dn_r_CodecError dn_f_LittleMessage1_field_3_add(dn_r_LittleMessage1 * v_message,uint32_t v_value);
static dn_r_LittleMessage1Field3 dn_f_LittleMessage1_field_3_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_LittleMessage1_field_3_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U32 v_values);
static dn_r_CodecError dn_f_LittleMessage1_field_4_add(dn_r_LittleMessage1 * v_message,uint64_t v_value);
static dn_r_LittleMessage1Field4 dn_f_LittleMessage1_field_4_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_LittleMessage1_field_4_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U64 v_values);
static dn_r_CodecError dn_f_LittleMessage1_field_5_add(dn_r_LittleMessage1 * v_message,bool v_value);
static dn_r_LittleMessage1Field5 dn_f_LittleMessage1_field_5_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_LittleMessage1_field_5_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_b v_values);
static dn_r_CodecError dn_f_LittleMessage1_field_6_add(dn_r_LittleMessage1 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecSpan dn_f_LittleMessage1_field_6_span(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_t_slice0_U8 dn_f_LittleMessage1_field_6_view(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_LittleMessage1_field_7_add(dn_r_LittleMessage1 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecSpan dn_f_LittleMessage1_field_7_span(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_t_slice0_U8 dn_f_LittleMessage1_field_7_view(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_LittleMessage1_field_8_add(dn_r_LittleMessage1 * v_message,uint64_t v_value);
static dn_r_LittleMessage1Field8 dn_f_LittleMessage1_field_8_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_LittleMessage1_field_8_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U64 v_values);
static dn_r_CodecError dn_f_LittleMessage1_field_9_add(dn_r_LittleMessage1 * v_message,int64_t v_value);
static dn_r_LittleMessage1Field9 dn_f_LittleMessage1_field_9_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_f_LittleMessage1_field_9_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_I64 v_values);
static dn_r_CodecError dn_f_LittleMessage1_field_11_add(dn_r_LittleMessage1 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecSpan dn_f_LittleMessage1_field_11_span(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_t_slice0_U8 dn_f_LittleMessage1_field_11_view(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecPlan dn_f_Little_plan_2(void);
static dn_r_LittleMessage2 dn_f_LittleMessage2_new(void);
static dn_r_LittleMessage2Decoded dn_f_LittleMessage2_decode(dn_t_slice0_U8 v_input);
static dn_r_CodecIo dn_f_LittleMessage2_measure(dn_r_LittleMessage2 const * v_message);
static dn_r_CodecIo dn_f_LittleMessage2_encode(dn_r_LittleMessage2 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset);
static bool dn_f_LittleMessage2_equal(dn_r_LittleMessage2 const * v_left,dn_r_LittleMessage2 const * v_right);
static dn_r_CodecDecoded dn_f_Little_decode(dn_t_slice0_U8 v_input);
static dn_r_EchoMessage1 dn_f_make_echo(void);
static dn_r_PosMessage1 dn_f_make_pos(void);
static dn_r_LittleMessage1 dn_f_make_little(void);
static uint8_t dn_f_main(void);
static void dn_drop_r12_CodecDecoded(dn_r_CodecDecoded volatile *x); static void dn_retain_r12_CodecDecoded(dn_r_CodecDecoded volatile *x);
static void dn_drop_r12_CodecDecoded(dn_r_CodecDecoded volatile *x){dn_drop_r12_CodecMessage(&x->f_value);}
static void dn_retain_r12_CodecDecoded(dn_r_CodecDecoded volatile *x){dn_retain_r12_CodecMessage(&x->f_value);}
static void dn_adopt_r12_CodecDecoded(dn_r_CodecDecoded volatile*x){dn_adopt_r12_CodecMessage(&x->f_value);}
static void dn_drop_r12_CodecMessage(dn_r_CodecMessage volatile *x); static void dn_retain_r12_CodecMessage(dn_r_CodecMessage volatile *x);
static void dn_drop_r12_CodecMessage(dn_r_CodecMessage volatile *x){dn_drop_buf_U8(&x->f_unknown);dn_drop_buf_U8(&x->f_bytes);dn_drop_buf_r10_CodecValue(&x->f_values);}
static void dn_retain_r12_CodecMessage(dn_r_CodecMessage volatile *x){dn_retain_buf_r10_CodecValue(&x->f_values);dn_retain_buf_U8(&x->f_bytes);dn_retain_buf_U8(&x->f_unknown);}
static void dn_adopt_r12_CodecMessage(dn_r_CodecMessage volatile*x){dn_adopt_buf_r10_CodecValue(&x->f_values);dn_adopt_buf_U8(&x->f_bytes);dn_adopt_buf_U8(&x->f_unknown);}
static void dn_drop_r12_EchoMessage1(dn_r_EchoMessage1 volatile *x); static void dn_retain_r12_EchoMessage1(dn_r_EchoMessage1 volatile *x);
static void dn_drop_r12_EchoMessage1(dn_r_EchoMessage1 volatile *x){dn_drop_r12_CodecMessage(&x->f_inner);}
static void dn_retain_r12_EchoMessage1(dn_r_EchoMessage1 volatile *x){dn_retain_r12_CodecMessage(&x->f_inner);}
static void dn_adopt_r12_EchoMessage1(dn_r_EchoMessage1 volatile*x){dn_adopt_r12_CodecMessage(&x->f_inner);}
static void dn_drop_r19_EchoMessage1Decoded(dn_r_EchoMessage1Decoded volatile *x); static void dn_retain_r19_EchoMessage1Decoded(dn_r_EchoMessage1Decoded volatile *x);
static void dn_drop_r19_EchoMessage1Decoded(dn_r_EchoMessage1Decoded volatile *x){dn_drop_r12_EchoMessage1(&x->f_value);}
static void dn_retain_r19_EchoMessage1Decoded(dn_r_EchoMessage1Decoded volatile *x){dn_retain_r12_EchoMessage1(&x->f_value);}
static void dn_adopt_r19_EchoMessage1Decoded(dn_r_EchoMessage1Decoded volatile*x){dn_adopt_r12_EchoMessage1(&x->f_value);}
static void dn_drop_r12_EchoMessage2(dn_r_EchoMessage2 volatile *x); static void dn_retain_r12_EchoMessage2(dn_r_EchoMessage2 volatile *x);
static void dn_drop_r12_EchoMessage2(dn_r_EchoMessage2 volatile *x){dn_drop_r12_CodecMessage(&x->f_inner);}
static void dn_retain_r12_EchoMessage2(dn_r_EchoMessage2 volatile *x){dn_retain_r12_CodecMessage(&x->f_inner);}
static void dn_adopt_r12_EchoMessage2(dn_r_EchoMessage2 volatile*x){dn_adopt_r12_CodecMessage(&x->f_inner);}
static void dn_drop_r19_EchoMessage2Decoded(dn_r_EchoMessage2Decoded volatile *x); static void dn_retain_r19_EchoMessage2Decoded(dn_r_EchoMessage2Decoded volatile *x);
static void dn_drop_r19_EchoMessage2Decoded(dn_r_EchoMessage2Decoded volatile *x){dn_drop_r12_EchoMessage2(&x->f_value);}
static void dn_retain_r19_EchoMessage2Decoded(dn_r_EchoMessage2Decoded volatile *x){dn_retain_r12_EchoMessage2(&x->f_value);}
static void dn_adopt_r19_EchoMessage2Decoded(dn_r_EchoMessage2Decoded volatile*x){dn_adopt_r12_EchoMessage2(&x->f_value);}
static void dn_drop_r14_LittleMessage1(dn_r_LittleMessage1 volatile *x); static void dn_retain_r14_LittleMessage1(dn_r_LittleMessage1 volatile *x);
static void dn_drop_r14_LittleMessage1(dn_r_LittleMessage1 volatile *x){dn_drop_r12_CodecMessage(&x->f_inner);}
static void dn_retain_r14_LittleMessage1(dn_r_LittleMessage1 volatile *x){dn_retain_r12_CodecMessage(&x->f_inner);}
static void dn_adopt_r14_LittleMessage1(dn_r_LittleMessage1 volatile*x){dn_adopt_r12_CodecMessage(&x->f_inner);}
static void dn_drop_r21_LittleMessage1Decoded(dn_r_LittleMessage1Decoded volatile *x); static void dn_retain_r21_LittleMessage1Decoded(dn_r_LittleMessage1Decoded volatile *x);
static void dn_drop_r21_LittleMessage1Decoded(dn_r_LittleMessage1Decoded volatile *x){dn_drop_r14_LittleMessage1(&x->f_value);}
static void dn_retain_r21_LittleMessage1Decoded(dn_r_LittleMessage1Decoded volatile *x){dn_retain_r14_LittleMessage1(&x->f_value);}
static void dn_adopt_r21_LittleMessage1Decoded(dn_r_LittleMessage1Decoded volatile*x){dn_adopt_r14_LittleMessage1(&x->f_value);}
static void dn_drop_r14_LittleMessage2(dn_r_LittleMessage2 volatile *x); static void dn_retain_r14_LittleMessage2(dn_r_LittleMessage2 volatile *x);
static void dn_drop_r14_LittleMessage2(dn_r_LittleMessage2 volatile *x){dn_drop_r12_CodecMessage(&x->f_inner);}
static void dn_retain_r14_LittleMessage2(dn_r_LittleMessage2 volatile *x){dn_retain_r12_CodecMessage(&x->f_inner);}
static void dn_adopt_r14_LittleMessage2(dn_r_LittleMessage2 volatile*x){dn_adopt_r12_CodecMessage(&x->f_inner);}
static void dn_drop_r21_LittleMessage2Decoded(dn_r_LittleMessage2Decoded volatile *x); static void dn_retain_r21_LittleMessage2Decoded(dn_r_LittleMessage2Decoded volatile *x);
static void dn_drop_r21_LittleMessage2Decoded(dn_r_LittleMessage2Decoded volatile *x){dn_drop_r14_LittleMessage2(&x->f_value);}
static void dn_retain_r21_LittleMessage2Decoded(dn_r_LittleMessage2Decoded volatile *x){dn_retain_r14_LittleMessage2(&x->f_value);}
static void dn_adopt_r21_LittleMessage2Decoded(dn_r_LittleMessage2Decoded volatile*x){dn_adopt_r14_LittleMessage2(&x->f_value);}
static void dn_drop_r11_PosMessage1(dn_r_PosMessage1 volatile *x); static void dn_retain_r11_PosMessage1(dn_r_PosMessage1 volatile *x);
static void dn_drop_r11_PosMessage1(dn_r_PosMessage1 volatile *x){dn_drop_r12_CodecMessage(&x->f_inner);}
static void dn_retain_r11_PosMessage1(dn_r_PosMessage1 volatile *x){dn_retain_r12_CodecMessage(&x->f_inner);}
static void dn_adopt_r11_PosMessage1(dn_r_PosMessage1 volatile*x){dn_adopt_r12_CodecMessage(&x->f_inner);}
static void dn_drop_r18_PosMessage1Decoded(dn_r_PosMessage1Decoded volatile *x); static void dn_retain_r18_PosMessage1Decoded(dn_r_PosMessage1Decoded volatile *x);
static void dn_drop_r18_PosMessage1Decoded(dn_r_PosMessage1Decoded volatile *x){dn_drop_r11_PosMessage1(&x->f_value);}
static void dn_retain_r18_PosMessage1Decoded(dn_r_PosMessage1Decoded volatile *x){dn_retain_r11_PosMessage1(&x->f_value);}
static void dn_adopt_r18_PosMessage1Decoded(dn_r_PosMessage1Decoded volatile*x){dn_adopt_r11_PosMessage1(&x->f_value);}
static void dn_drop_r11_PosMessage2(dn_r_PosMessage2 volatile *x); static void dn_retain_r11_PosMessage2(dn_r_PosMessage2 volatile *x);
static void dn_drop_r11_PosMessage2(dn_r_PosMessage2 volatile *x){dn_drop_r12_CodecMessage(&x->f_inner);}
static void dn_retain_r11_PosMessage2(dn_r_PosMessage2 volatile *x){dn_retain_r12_CodecMessage(&x->f_inner);}
static void dn_adopt_r11_PosMessage2(dn_r_PosMessage2 volatile*x){dn_adopt_r12_CodecMessage(&x->f_inner);}
static void dn_drop_r18_PosMessage2Decoded(dn_r_PosMessage2Decoded volatile *x); static void dn_retain_r18_PosMessage2Decoded(dn_r_PosMessage2Decoded volatile *x);
static void dn_drop_r18_PosMessage2Decoded(dn_r_PosMessage2Decoded volatile *x){dn_drop_r11_PosMessage2(&x->f_value);}
static void dn_retain_r18_PosMessage2Decoded(dn_r_PosMessage2Decoded volatile *x){dn_retain_r11_PosMessage2(&x->f_value);}
static void dn_adopt_r18_PosMessage2Decoded(dn_r_PosMessage2Decoded volatile*x){dn_adopt_r11_PosMessage2(&x->f_value);}
static void dn_drop_buf_U8(dn_t_buf_U8 volatile *x); static void dn_retain_buf_U8(dn_t_buf_U8 volatile *x);
static void dn_drop_buf_U8(dn_t_buf_U8 volatile *x){dn_deallocate(x->data);*x=(dn_t_buf_U8){0};}
static void dn_retain_buf_U8(dn_t_buf_U8 volatile *x){dn_detach(x->data);}
static dn_t_buf_U8 dn_new_buf_U8(size_t n){dn_t_buf_U8 x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(uint8_t)),_Alignof(uint8_t));x.len=x.cap=n;return x;}
static dn_t_buf_U8 dn_aligned_buf_U8(size_t n,size_t a){if(a<_Alignof(uint8_t)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_U8 x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(uint8_t)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_U8(dn_t_buf_U8 *x,uint8_t v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;uint8_t *p=dn_allocate_aligned(dn_size(cap,sizeof(uint8_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(uint8_t));if(x->len)memcpy(p,x->data,x->len*sizeof(uint8_t));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;x->len++;return 0;}
static uint8_t dn_resize_buf_U8(dn_t_buf_U8 *x,size_t n){if(n<x->len){}if(n>x->cap){size_t cap=n;uint8_t *p=dn_allocate_aligned(dn_size(cap,sizeof(uint8_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(uint8_t));if(x->len)memcpy(p,x->data,x->len*sizeof(uint8_t));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(uint8_t));x->len=n;return 0;}
static void dn_adopt_buf_U8(dn_t_buf_U8 volatile*x){dn_adopt(x->data);}
static void dn_drop_buf_U32(dn_t_buf_U32 volatile *x); static void dn_retain_buf_U32(dn_t_buf_U32 volatile *x);
static void dn_drop_buf_U32(dn_t_buf_U32 volatile *x){dn_deallocate(x->data);*x=(dn_t_buf_U32){0};}
static void dn_retain_buf_U32(dn_t_buf_U32 volatile *x){dn_detach(x->data);}
static dn_t_buf_U32 dn_new_buf_U32(size_t n){dn_t_buf_U32 x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(uint32_t)),_Alignof(uint32_t));x.len=x.cap=n;return x;}
static dn_t_buf_U32 dn_aligned_buf_U32(size_t n,size_t a){if(a<_Alignof(uint32_t)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_U32 x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(uint32_t)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_U32(dn_t_buf_U32 *x,uint32_t v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;uint32_t *p=dn_allocate_aligned(dn_size(cap,sizeof(uint32_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(uint32_t));if(x->len)memcpy(p,x->data,x->len*sizeof(uint32_t));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;x->len++;return 0;}
static uint8_t dn_resize_buf_U32(dn_t_buf_U32 *x,size_t n){if(n<x->len){}if(n>x->cap){size_t cap=n;uint32_t *p=dn_allocate_aligned(dn_size(cap,sizeof(uint32_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(uint32_t));if(x->len)memcpy(p,x->data,x->len*sizeof(uint32_t));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(uint32_t));x->len=n;return 0;}
static void dn_adopt_buf_U32(dn_t_buf_U32 volatile*x){dn_adopt(x->data);}
static void dn_drop_buf_U64(dn_t_buf_U64 volatile *x); static void dn_retain_buf_U64(dn_t_buf_U64 volatile *x);
static void dn_drop_buf_U64(dn_t_buf_U64 volatile *x){dn_deallocate(x->data);*x=(dn_t_buf_U64){0};}
static void dn_retain_buf_U64(dn_t_buf_U64 volatile *x){dn_detach(x->data);}
static dn_t_buf_U64 dn_new_buf_U64(size_t n){dn_t_buf_U64 x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(uint64_t)),_Alignof(uint64_t));x.len=x.cap=n;return x;}
static dn_t_buf_U64 dn_aligned_buf_U64(size_t n,size_t a){if(a<_Alignof(uint64_t)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_U64 x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(uint64_t)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_U64(dn_t_buf_U64 *x,uint64_t v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;uint64_t *p=dn_allocate_aligned(dn_size(cap,sizeof(uint64_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(uint64_t));if(x->len)memcpy(p,x->data,x->len*sizeof(uint64_t));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;x->len++;return 0;}
static uint8_t dn_resize_buf_U64(dn_t_buf_U64 *x,size_t n){if(n<x->len){}if(n>x->cap){size_t cap=n;uint64_t *p=dn_allocate_aligned(dn_size(cap,sizeof(uint64_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(uint64_t));if(x->len)memcpy(p,x->data,x->len*sizeof(uint64_t));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(uint64_t));x->len=n;return 0;}
static void dn_adopt_buf_U64(dn_t_buf_U64 volatile*x){dn_adopt(x->data);}
static void dn_drop_buf_I64(dn_t_buf_I64 volatile *x); static void dn_retain_buf_I64(dn_t_buf_I64 volatile *x);
static void dn_drop_buf_I64(dn_t_buf_I64 volatile *x){dn_deallocate(x->data);*x=(dn_t_buf_I64){0};}
static void dn_retain_buf_I64(dn_t_buf_I64 volatile *x){dn_detach(x->data);}
static dn_t_buf_I64 dn_new_buf_I64(size_t n){dn_t_buf_I64 x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(int64_t)),_Alignof(int64_t));x.len=x.cap=n;return x;}
static dn_t_buf_I64 dn_aligned_buf_I64(size_t n,size_t a){if(a<_Alignof(int64_t)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_I64 x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(int64_t)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_I64(dn_t_buf_I64 *x,int64_t v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;int64_t *p=dn_allocate_aligned(dn_size(cap,sizeof(int64_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(int64_t));if(x->len)memcpy(p,x->data,x->len*sizeof(int64_t));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;x->len++;return 0;}
static uint8_t dn_resize_buf_I64(dn_t_buf_I64 *x,size_t n){if(n<x->len){}if(n>x->cap){size_t cap=n;int64_t *p=dn_allocate_aligned(dn_size(cap,sizeof(int64_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(int64_t));if(x->len)memcpy(p,x->data,x->len*sizeof(int64_t));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(int64_t));x->len=n;return 0;}
static void dn_adopt_buf_I64(dn_t_buf_I64 volatile*x){dn_adopt(x->data);}
static void dn_drop_buf_r10_CodecValue(dn_t_buf_r10_CodecValue volatile *x); static void dn_retain_buf_r10_CodecValue(dn_t_buf_r10_CodecValue volatile *x);
static void dn_drop_buf_r10_CodecValue(dn_t_buf_r10_CodecValue volatile *x){dn_deallocate(x->data);*x=(dn_t_buf_r10_CodecValue){0};}
static void dn_retain_buf_r10_CodecValue(dn_t_buf_r10_CodecValue volatile *x){dn_detach(x->data);}
static dn_t_buf_r10_CodecValue dn_new_buf_r10_CodecValue(size_t n){dn_t_buf_r10_CodecValue x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(dn_r_CodecValue)),_Alignof(dn_r_CodecValue));x.len=x.cap=n;return x;}
static dn_t_buf_r10_CodecValue dn_aligned_buf_r10_CodecValue(size_t n,size_t a){if(a<_Alignof(dn_r_CodecValue)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_r10_CodecValue x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(dn_r_CodecValue)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_r10_CodecValue(dn_t_buf_r10_CodecValue *x,dn_r_CodecValue v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;dn_r_CodecValue *p=dn_allocate_aligned(dn_size(cap,sizeof(dn_r_CodecValue)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(dn_r_CodecValue));if(x->len)memcpy(p,x->data,x->len*sizeof(dn_r_CodecValue));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;x->len++;return 0;}
static uint8_t dn_resize_buf_r10_CodecValue(dn_t_buf_r10_CodecValue *x,size_t n){if(n<x->len){}if(n>x->cap){size_t cap=n;dn_r_CodecValue *p=dn_allocate_aligned(dn_size(cap,sizeof(dn_r_CodecValue)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(dn_r_CodecValue));if(x->len)memcpy(p,x->data,x->len*sizeof(dn_r_CodecValue));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(dn_r_CodecValue));x->len=n;return 0;}
static void dn_adopt_buf_r10_CodecValue(dn_t_buf_r10_CodecValue volatile*x){dn_adopt(x->data);}
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
static dn_r_MessageField dn_cb_network_field(uint32_t v_id,uint32_t v_kind,uint32_t v_minimum,uint32_t v_maximum,uint32_t v_length);
static dn_r_MessageSchema dn_cb_message_schema(uint32_t v_id,uint32_t v_phase,uint32_t v_senders);
static dn_r_NetworkSpec dn_cb_network_spec(uint32_t v_protocol,uint32_t v_major,uint32_t v_minor,uint32_t v_minimum);
static int32_t dn_cb_message_add_field(dn_r_MessageSchema * v_message,dn_r_MessageField v_field);
static int32_t dn_cb_network_add_message(dn_r_NetworkSpec * v_spec,dn_r_MessageSchema v_message);
static bool dn_cb_network_minor_active(uint32_t v_first,uint32_t v_last,uint32_t v_minor);
static int32_t dn_cb_network_field_check(dn_r_MessageField const * v_field);
static int32_t dn_cb_network_wire_check(dn_r_WireFormat v_wire);
static dn_r_SchemaMeasure dn_cb_network_field_measure(dn_r_MessageField const * v_field,dn_r_WireFormat v_wire);
static dn_r_SchemaMeasure dn_cb_network_message_measure(dn_r_MessageSchema const * v_message,dn_r_WireFormat v_wire,uint32_t v_minor);
static dn_r_NetworkSpecCheck dn_cb_network_spec_failure(int32_t v_status,uint32_t v_component,uint32_t v_message,uint32_t v_field);
static bool dn_cb_network_has_message(dn_r_NetworkSpec const * v_spec,uint32_t v_minor);
static dn_r_NetworkSpecCheck dn_cb_network_spec_check(dn_r_NetworkSpec const * v_spec);
static dn_r_NetworkAdmission dn_cb_network_spec_select(dn_r_NetworkSpec const * v_spec,dn_t_arr6_r18_TransportCandidate v_candidates);
static dn_utf8 dn_cb_network_obligation_name(uint32_t v_bit);
static dn_r_ProtocolValue dn_cb_protocol_read_be(dn_t_slice0_U8 v_bytes,size_t v_offset,size_t v_width);
static int32_t dn_cb_protocol_write_be(dn_t_buf_U8 * v_bytes,size_t v_offset,size_t v_width,uint64_t v_value);
static dn_r_ProtocolValue dn_cb_quic_varint_read(dn_t_slice0_U8 v_bytes,size_t v_offset);
static size_t dn_cb_quic_varint_size(uint64_t v_value);
static dn_r_ProtocolValue dn_cb_quic_varint_write(dn_t_buf_U8 * v_bytes,size_t v_offset,uint64_t v_value);
static dn_r_ProtocolFrame dn_cb_protocol_frame32(dn_t_slice0_U8 v_bytes,size_t v_offset,size_t v_maximum);
static dn_r_ProtocolValue dn_cb_net_read_le(dn_t_slice0_U8 v_bytes,size_t v_offset,size_t v_width);
static int32_t dn_cb_net_write_le(dn_t_buf_U8 * v_bytes,size_t v_offset,size_t v_width,uint64_t v_value);
static dn_r_ProtocolValue dn_cb_net_read_be(dn_t_slice0_U8 v_bytes,size_t v_offset,size_t v_width);
static int32_t dn_cb_net_write_be(dn_t_buf_U8 * v_bytes,size_t v_offset,size_t v_width,uint64_t v_value);
static size_t dn_cb_uleb128_size(uint64_t v_value);
static dn_r_ProtocolValue dn_cb_uleb128_read(dn_t_slice0_U8 v_bytes,size_t v_offset,bool v_canonical);
static dn_r_ProtocolValue dn_cb_uleb128_write(dn_t_buf_U8 * v_bytes,size_t v_offset,uint64_t v_value);
static uint64_t dn_cb_zigzag_encode(int64_t v_value);
static int64_t dn_cb_zigzag_decode(uint64_t v_value);
static dn_t_buf_U8 dn_cb_wire_new(uint16_t v_version);
static bool dn_cb_wire_valid(dn_t_buf_U8 const * v_bytes);
static size_t dn_cb_wire_find(dn_t_buf_U8 const * v_bytes,uint32_t v_id);
static dn_t_slice0_U8 dn_cb_wire_field(dn_t_buf_U8 const * v_bytes,uint32_t v_id);
static void dn_cb_wire_set(dn_t_buf_U8 * v_bytes,uint32_t v_id,dn_t_slice0_U8 v_payload);
static void dn_cb_wire_upgrade(dn_t_buf_U8 * v_bytes);
static dn_r_Utf8Step dn_cb_utf8_next(dn_t_slice0_U8 v_bytes,size_t v_offset);
static dn_r_CodecError dn_cb_codec_error(int32_t v_kind,uint32_t v_field,size_t v_offset);
static dn_r_CodecShape dn_cb_codec_shape_error(int32_t v_kind,uint32_t v_field,size_t v_offset);
static dn_r_CodecError dn_cb_codec_plan_check(dn_r_CodecPlan const * v_plan);
static size_t dn_cb_codec_find(dn_r_CodecPlan const * v_plan,uint32_t v_field);
static bool dn_cb_codec_storage(dn_r_CodecPlan const * v_plan,size_t v_values,size_t v_bytes,size_t v_unknown);
static bool dn_cb_codec_scalar_valid(uint32_t v_kind,uint64_t v_value);
static size_t dn_cb_codec_scalar_size(uint32_t v_kind,uint64_t v_value);
static bool dn_cb_codec_utf8(dn_t_slice0_U8 v_input,size_t v_offset,size_t v_count);
static dn_r_CodecRead dn_cb_codec_read_error(int32_t v_kind,uint32_t v_field,size_t v_offset);
static dn_r_ProtocolValue dn_cb_codec_read_integer(dn_r_CodecPlan const * v_plan,dn_t_slice0_U8 v_input,size_t v_offset,size_t v_width);
static dn_r_CodecRead dn_cb_codec_read_value(dn_r_CodecPlan const * v_plan,dn_r_MessageField const * v_field,dn_t_slice0_U8 v_input,size_t v_offset,size_t v_end);
static dn_r_CodecShape dn_cb_codec_unknown_shape(dn_r_CodecPlan const * v_plan,dn_t_slice0_U8 v_input);
static dn_r_CodecError dn_cb_codec_shape_complete(dn_r_CodecPlan const * v_plan,dn_r_CodecShape const * v_shape);
static dn_r_CodecShape dn_cb_codec_scan(dn_r_CodecPlan const * v_plan,dn_t_slice0_U8 v_input);
static dn_r_CodecMessage dn_cb_codec_message(uint32_t v_id);
static dn_r_CodecDecoded dn_cb_codec_decode_error(int32_t v_kind,uint32_t v_field,size_t v_offset);
static dn_r_CodecDecoded dn_cb_codec_decode(dn_r_CodecPlan const * v_plan,dn_t_slice0_U8 v_input);
static dn_r_CodecShape dn_cb_codec_message_inspect(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,bool v_complete);
static dn_r_CodecIo dn_cb_codec_measure(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message);
static dn_r_CodecReservation dn_cb_codec_reserve_scalars(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage * v_message,uint32_t v_field_id,size_t v_count);
static dn_r_CodecError dn_cb_codec_add_scalar(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage * v_message,uint32_t v_field_id,uint64_t v_scalar);
static dn_r_CodecError dn_cb_codec_add_bytes(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage * v_message,uint32_t v_field_id,dn_t_slice0_U8 v_input);
static dn_r_CodecError dn_cb_codec_add_unknown(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage * v_message,dn_t_slice0_U8 v_input);
static bool dn_cb_codec_unknown_over_budget(dn_r_CodecPlan const * v_plan,size_t v_used,size_t v_count);
static dn_r_CodecValue dn_cb_codec_get(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,uint32_t v_field_id,size_t v_occurrence);
static dn_r_CodecScalar dn_cb_codec_get_scalar(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,uint32_t v_field_id,size_t v_occurrence);
static dn_r_CodecSpan dn_cb_codec_get_span(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,uint32_t v_field_id,size_t v_occurrence);
static dn_t_slice0_U8 dn_cb_codec_view(dn_r_CodecMessage const * v_message,dn_r_CodecSpan v_span);
static int32_t dn_cb_codec_write_integer(dn_r_CodecPlan const * v_plan,dn_t_buf_U8 * v_output,size_t v_offset,size_t v_width,uint64_t v_value);
static dn_r_CodecIo dn_cb_codec_encode(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,dn_t_buf_U8 * v_output,size_t v_offset);
static bool dn_cb_codec_equal(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_left,dn_r_CodecMessage const * v_right);
static dn_r_NetworkSpec dn_cb_Echo_spec(void);
static dn_r_CodecPlan dn_cb_Echo_plan_1(void);
static dn_r_EchoMessage1 dn_cb_EchoMessage1_new(void);
static dn_r_EchoMessage1Decoded dn_cb_EchoMessage1_decode(dn_t_slice0_U8 v_input);
static dn_r_CodecIo dn_cb_EchoMessage1_measure(dn_r_EchoMessage1 const * v_message);
static dn_r_CodecIo dn_cb_EchoMessage1_encode(dn_r_EchoMessage1 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset);
static bool dn_cb_EchoMessage1_equal(dn_r_EchoMessage1 const * v_left,dn_r_EchoMessage1 const * v_right);
static dn_r_CodecError dn_cb_EchoMessage1_field_1_add(dn_r_EchoMessage1 * v_message,uint8_t v_value);
static dn_r_EchoMessage1Field1 dn_cb_EchoMessage1_field_1_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_EchoMessage1_field_1_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U8 v_values);
static dn_r_CodecError dn_cb_EchoMessage1_field_2_add(dn_r_EchoMessage1 * v_message,uint16_t v_value);
static dn_r_EchoMessage1Field2 dn_cb_EchoMessage1_field_2_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_EchoMessage1_field_2_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U16 v_values);
static dn_r_CodecError dn_cb_EchoMessage1_field_3_add(dn_r_EchoMessage1 * v_message,uint32_t v_value);
static dn_r_EchoMessage1Field3 dn_cb_EchoMessage1_field_3_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_EchoMessage1_field_3_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U32 v_values);
static dn_r_CodecError dn_cb_EchoMessage1_field_4_add(dn_r_EchoMessage1 * v_message,uint64_t v_value);
static dn_r_EchoMessage1Field4 dn_cb_EchoMessage1_field_4_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_EchoMessage1_field_4_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U64 v_values);
static dn_r_CodecError dn_cb_EchoMessage1_field_5_add(dn_r_EchoMessage1 * v_message,bool v_value);
static dn_r_EchoMessage1Field5 dn_cb_EchoMessage1_field_5_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_EchoMessage1_field_5_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_b v_values);
static dn_r_CodecError dn_cb_EchoMessage1_field_6_add(dn_r_EchoMessage1 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecSpan dn_cb_EchoMessage1_field_6_span(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_t_slice0_U8 dn_cb_EchoMessage1_field_6_view(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_EchoMessage1_field_7_add(dn_r_EchoMessage1 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecSpan dn_cb_EchoMessage1_field_7_span(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_t_slice0_U8 dn_cb_EchoMessage1_field_7_view(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_EchoMessage1_field_8_add(dn_r_EchoMessage1 * v_message,uint64_t v_value);
static dn_r_EchoMessage1Field8 dn_cb_EchoMessage1_field_8_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_EchoMessage1_field_8_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U64 v_values);
static dn_r_CodecError dn_cb_EchoMessage1_field_9_add(dn_r_EchoMessage1 * v_message,int64_t v_value);
static dn_r_EchoMessage1Field9 dn_cb_EchoMessage1_field_9_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_EchoMessage1_field_9_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_I64 v_values);
static dn_r_CodecError dn_cb_EchoMessage1_field_10_add(dn_r_EchoMessage1 * v_message,uint16_t v_value);
static dn_r_EchoMessage1Field10 dn_cb_EchoMessage1_field_10_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_EchoMessage1_field_10_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U16 v_values);
static dn_r_CodecError dn_cb_EchoMessage1_add_unknown(dn_r_EchoMessage1 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecPlan dn_cb_Echo_plan_2(void);
static dn_r_EchoMessage2 dn_cb_EchoMessage2_new(void);
static dn_r_EchoMessage2Decoded dn_cb_EchoMessage2_decode(dn_t_slice0_U8 v_input);
static dn_r_CodecIo dn_cb_EchoMessage2_measure(dn_r_EchoMessage2 const * v_message);
static dn_r_CodecIo dn_cb_EchoMessage2_encode(dn_r_EchoMessage2 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset);
static bool dn_cb_EchoMessage2_equal(dn_r_EchoMessage2 const * v_left,dn_r_EchoMessage2 const * v_right);
static dn_r_CodecError dn_cb_EchoMessage2_add_unknown(dn_r_EchoMessage2 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecDecoded dn_cb_Echo_decode(dn_t_slice0_U8 v_input);
static dn_r_NetworkSpec dn_cb_Pos_spec(void);
static dn_r_CodecPlan dn_cb_Pos_plan_1(void);
static dn_r_PosMessage1 dn_cb_PosMessage1_new(void);
static dn_r_PosMessage1Decoded dn_cb_PosMessage1_decode(dn_t_slice0_U8 v_input);
static dn_r_CodecIo dn_cb_PosMessage1_measure(dn_r_PosMessage1 const * v_message);
static dn_r_CodecIo dn_cb_PosMessage1_encode(dn_r_PosMessage1 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset);
static bool dn_cb_PosMessage1_equal(dn_r_PosMessage1 const * v_left,dn_r_PosMessage1 const * v_right);
static dn_r_CodecError dn_cb_PosMessage1_field_1_add(dn_r_PosMessage1 * v_message,uint8_t v_value);
static dn_r_PosMessage1Field1 dn_cb_PosMessage1_field_1_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_PosMessage1_field_1_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U8 v_values);
static dn_r_CodecError dn_cb_PosMessage1_field_2_add(dn_r_PosMessage1 * v_message,uint16_t v_value);
static dn_r_PosMessage1Field2 dn_cb_PosMessage1_field_2_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_PosMessage1_field_2_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U16 v_values);
static dn_r_CodecError dn_cb_PosMessage1_field_3_add(dn_r_PosMessage1 * v_message,uint32_t v_value);
static dn_r_PosMessage1Field3 dn_cb_PosMessage1_field_3_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_PosMessage1_field_3_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U32 v_values);
static dn_r_CodecError dn_cb_PosMessage1_field_4_add(dn_r_PosMessage1 * v_message,uint64_t v_value);
static dn_r_PosMessage1Field4 dn_cb_PosMessage1_field_4_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_PosMessage1_field_4_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U64 v_values);
static dn_r_CodecError dn_cb_PosMessage1_field_5_add(dn_r_PosMessage1 * v_message,bool v_value);
static dn_r_PosMessage1Field5 dn_cb_PosMessage1_field_5_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_PosMessage1_field_5_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_b v_values);
static dn_r_CodecError dn_cb_PosMessage1_field_6_add(dn_r_PosMessage1 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecSpan dn_cb_PosMessage1_field_6_span(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_t_slice0_U8 dn_cb_PosMessage1_field_6_view(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_PosMessage1_field_7_add(dn_r_PosMessage1 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecSpan dn_cb_PosMessage1_field_7_span(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_t_slice0_U8 dn_cb_PosMessage1_field_7_view(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_PosMessage1_field_8_add(dn_r_PosMessage1 * v_message,uint64_t v_value);
static dn_r_PosMessage1Field8 dn_cb_PosMessage1_field_8_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_PosMessage1_field_8_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U64 v_values);
static dn_r_CodecError dn_cb_PosMessage1_field_9_add(dn_r_PosMessage1 * v_message,int64_t v_value);
static dn_r_PosMessage1Field9 dn_cb_PosMessage1_field_9_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_PosMessage1_field_9_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_I64 v_values);
static dn_r_CodecError dn_cb_PosMessage1_field_11_add(dn_r_PosMessage1 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecSpan dn_cb_PosMessage1_field_11_span(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_t_slice0_U8 dn_cb_PosMessage1_field_11_view(dn_r_PosMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecPlan dn_cb_Pos_plan_2(void);
static dn_r_PosMessage2 dn_cb_PosMessage2_new(void);
static dn_r_PosMessage2Decoded dn_cb_PosMessage2_decode(dn_t_slice0_U8 v_input);
static dn_r_CodecIo dn_cb_PosMessage2_measure(dn_r_PosMessage2 const * v_message);
static dn_r_CodecIo dn_cb_PosMessage2_encode(dn_r_PosMessage2 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset);
static bool dn_cb_PosMessage2_equal(dn_r_PosMessage2 const * v_left,dn_r_PosMessage2 const * v_right);
static dn_r_CodecDecoded dn_cb_Pos_decode(dn_t_slice0_U8 v_input);
static dn_r_NetworkSpec dn_cb_Little_spec(void);
static dn_r_CodecPlan dn_cb_Little_plan_1(void);
static dn_r_LittleMessage1 dn_cb_LittleMessage1_new(void);
static dn_r_LittleMessage1Decoded dn_cb_LittleMessage1_decode(dn_t_slice0_U8 v_input);
static dn_r_CodecIo dn_cb_LittleMessage1_measure(dn_r_LittleMessage1 const * v_message);
static dn_r_CodecIo dn_cb_LittleMessage1_encode(dn_r_LittleMessage1 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset);
static bool dn_cb_LittleMessage1_equal(dn_r_LittleMessage1 const * v_left,dn_r_LittleMessage1 const * v_right);
static dn_r_CodecError dn_cb_LittleMessage1_field_1_add(dn_r_LittleMessage1 * v_message,uint8_t v_value);
static dn_r_LittleMessage1Field1 dn_cb_LittleMessage1_field_1_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_LittleMessage1_field_1_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U8 v_values);
static dn_r_CodecError dn_cb_LittleMessage1_field_2_add(dn_r_LittleMessage1 * v_message,uint16_t v_value);
static dn_r_LittleMessage1Field2 dn_cb_LittleMessage1_field_2_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_LittleMessage1_field_2_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U16 v_values);
static dn_r_CodecError dn_cb_LittleMessage1_field_3_add(dn_r_LittleMessage1 * v_message,uint32_t v_value);
static dn_r_LittleMessage1Field3 dn_cb_LittleMessage1_field_3_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_LittleMessage1_field_3_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U32 v_values);
static dn_r_CodecError dn_cb_LittleMessage1_field_4_add(dn_r_LittleMessage1 * v_message,uint64_t v_value);
static dn_r_LittleMessage1Field4 dn_cb_LittleMessage1_field_4_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_LittleMessage1_field_4_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U64 v_values);
static dn_r_CodecError dn_cb_LittleMessage1_field_5_add(dn_r_LittleMessage1 * v_message,bool v_value);
static dn_r_LittleMessage1Field5 dn_cb_LittleMessage1_field_5_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_LittleMessage1_field_5_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_b v_values);
static dn_r_CodecError dn_cb_LittleMessage1_field_6_add(dn_r_LittleMessage1 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecSpan dn_cb_LittleMessage1_field_6_span(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_t_slice0_U8 dn_cb_LittleMessage1_field_6_view(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_LittleMessage1_field_7_add(dn_r_LittleMessage1 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecSpan dn_cb_LittleMessage1_field_7_span(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_t_slice0_U8 dn_cb_LittleMessage1_field_7_view(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_LittleMessage1_field_8_add(dn_r_LittleMessage1 * v_message,uint64_t v_value);
static dn_r_LittleMessage1Field8 dn_cb_LittleMessage1_field_8_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_LittleMessage1_field_8_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U64 v_values);
static dn_r_CodecError dn_cb_LittleMessage1_field_9_add(dn_r_LittleMessage1 * v_message,int64_t v_value);
static dn_r_LittleMessage1Field9 dn_cb_LittleMessage1_field_9_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecError dn_cb_LittleMessage1_field_9_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_I64 v_values);
static dn_r_CodecError dn_cb_LittleMessage1_field_11_add(dn_r_LittleMessage1 * v_message,dn_t_slice0_U8 v_value);
static dn_r_CodecSpan dn_cb_LittleMessage1_field_11_span(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_t_slice0_U8 dn_cb_LittleMessage1_field_11_view(dn_r_LittleMessage1 const * v_message,size_t v_occurrence);
static dn_r_CodecPlan dn_cb_Little_plan_2(void);
static dn_r_LittleMessage2 dn_cb_LittleMessage2_new(void);
static dn_r_LittleMessage2Decoded dn_cb_LittleMessage2_decode(dn_t_slice0_U8 v_input);
static dn_r_CodecIo dn_cb_LittleMessage2_measure(dn_r_LittleMessage2 const * v_message);
static dn_r_CodecIo dn_cb_LittleMessage2_encode(dn_r_LittleMessage2 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset);
static bool dn_cb_LittleMessage2_equal(dn_r_LittleMessage2 const * v_left,dn_r_LittleMessage2 const * v_right);
static dn_r_CodecDecoded dn_cb_Little_decode(dn_t_slice0_U8 v_input);
static dn_r_EchoMessage1 dn_cb_make_echo(void);
static dn_r_PosMessage1 dn_cb_make_pos(void);
static dn_r_LittleMessage1 dn_cb_make_little(void);
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
static dn_r_MessageField dn_f_network_field(uint32_t v_id,uint32_t v_kind,uint32_t v_minimum,uint32_t v_maximum,uint32_t v_length) {
{
{ dn_r_MessageField dn_tmp_8=({ uint32_t dn_tmp_0=v_id;
uint32_t dn_tmp_1=v_kind;
uint32_t dn_tmp_2=v_minimum;
uint32_t dn_tmp_3=v_maximum;
uint32_t dn_tmp_4=v_length;
uint32_t dn_tmp_5=((uint32_t)(0ULL));
uint32_t dn_tmp_6=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_7=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7}); });return dn_tmp_8; }
}

dn_fail("missing native return");
}
static dn_r_MessageField dn_cb_network_field(uint32_t v_id,uint32_t v_kind,uint32_t v_minimum,uint32_t v_maximum,uint32_t v_length) {
 if(!dn_enter()){return (dn_r_MessageField){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_MessageField){0};}
 dn_r_MessageField dn_result=dn_f_network_field(v_id,v_kind,v_minimum,v_maximum,v_length);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_MessageSchema dn_f_message_schema(uint32_t v_id,uint32_t v_phase,uint32_t v_senders) {
{
dn_r_MessageSchema v_result=({ ((dn_r_MessageSchema){0}); });
{ uint32_t dn_tmp_1=v_id;uint32_t *dn_tmp_0=&((v_result).f_id);*dn_tmp_0=dn_tmp_1; }
{ uint32_t dn_tmp_3=v_phase;uint32_t *dn_tmp_2=&((v_result).f_phase);*dn_tmp_2=dn_tmp_3; }
{ uint32_t dn_tmp_5=v_senders;uint32_t *dn_tmp_4=&((v_result).f_senders);*dn_tmp_4=dn_tmp_5; }
{ uint32_t dn_tmp_7=((uint32_t)(4294967295ULL));uint32_t *dn_tmp_6=&((v_result).f_last_minor);*dn_tmp_6=dn_tmp_7; }
{ dn_r_MessageSchema dn_tmp_8=v_result;return dn_tmp_8; }
}

dn_fail("missing native return");
}
static dn_r_MessageSchema dn_cb_message_schema(uint32_t v_id,uint32_t v_phase,uint32_t v_senders) {
 if(!dn_enter()){return (dn_r_MessageSchema){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_MessageSchema){0};}
 dn_r_MessageSchema dn_result=dn_f_message_schema(v_id,v_phase,v_senders);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkSpec dn_f_network_spec(uint32_t v_protocol,uint32_t v_major,uint32_t v_minor,uint32_t v_minimum) {
{
dn_r_NetworkSpec v_result=({ ((dn_r_NetworkSpec){0}); });
{ uint32_t dn_tmp_1=v_protocol;uint32_t *dn_tmp_0=&((v_result).f_protocol);*dn_tmp_0=dn_tmp_1; }
{ dn_r_ProtocolVersion dn_tmp_6=({ uint32_t dn_tmp_2=v_major;
uint32_t dn_tmp_3=v_minor;
uint32_t dn_tmp_4=v_minimum;
((dn_r_ProtocolVersion){dn_tmp_2,dn_tmp_3,dn_tmp_4}); });dn_r_ProtocolVersion *dn_tmp_5=&((v_result).f_version);*dn_tmp_5=dn_tmp_6; }
{ dn_r_WireFormat dn_tmp_12=({ uint32_t dn_tmp_7=((uint32_t)(2ULL));
uint32_t dn_tmp_8=((uint32_t)(2ULL));
uint32_t dn_tmp_9=((uint32_t)(2ULL));
uint32_t dn_tmp_10=((uint32_t)(0ULL));
((dn_r_WireFormat){dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10}); });dn_r_WireFormat *dn_tmp_11=&((v_result).f_wire);*dn_tmp_11=dn_tmp_12; }
{ dn_r_TransportRequirements dn_tmp_17=({ uint32_t dn_tmp_13=((uint32_t)(16ULL));
uint32_t dn_tmp_14=((uint32_t)(0ULL));
uint32_t dn_tmp_15=((uint32_t)(0ULL));
((dn_r_TransportRequirements){dn_tmp_13,dn_tmp_14,dn_tmp_15}); });dn_r_TransportRequirements *dn_tmp_16=&((v_result).f_transport);*dn_tmp_16=dn_tmp_17; }
{ dn_r_ResourceBounds dn_tmp_24=({ dn_r_TransportBounds dn_tmp_18=({ dn_f_transport_bounds(); });
uint32_t dn_tmp_19=((uint32_t)(0ULL));
uint32_t dn_tmp_20=((uint32_t)(0ULL));
uint64_t dn_tmp_21=((uint64_t)(1000000ULL));
uint32_t dn_tmp_22=((uint32_t)(529ULL));
((dn_r_ResourceBounds){dn_tmp_18,dn_tmp_19,dn_tmp_20,dn_tmp_21,dn_tmp_22}); });dn_r_ResourceBounds *dn_tmp_23=&((v_result).f_resources);*dn_tmp_23=dn_tmp_24; }
{ dn_r_SessionContract dn_tmp_31=({ uint32_t dn_tmp_25=((uint32_t)(10000ULL));
uint32_t dn_tmp_26=((uint32_t)(30000ULL));
uint32_t dn_tmp_27=((uint32_t)(300000ULL));
uint32_t dn_tmp_28=((uint32_t)(32ULL));
bool dn_tmp_29=0;
((dn_r_SessionContract){dn_tmp_25,dn_tmp_26,dn_tmp_27,dn_tmp_28,dn_tmp_29}); });dn_r_SessionContract *dn_tmp_30=&((v_result).f_session);*dn_tmp_30=dn_tmp_31; }
{ dn_r_ProofObligations dn_tmp_35=({ uint32_t dn_tmp_32=((uint32_t)(0ULL));
uint32_t dn_tmp_33=((uint32_t)(1ULL));
((dn_r_ProofObligations){dn_tmp_32,dn_tmp_33}); });dn_r_ProofObligations *dn_tmp_34=&((v_result).f_proofs);*dn_tmp_34=dn_tmp_35; }
{ dn_r_NetworkSpec dn_tmp_36=v_result;return dn_tmp_36; }
}

dn_fail("missing native return");
}
static dn_r_NetworkSpec dn_cb_network_spec(uint32_t v_protocol,uint32_t v_major,uint32_t v_minor,uint32_t v_minimum) {
 if(!dn_enter()){return (dn_r_NetworkSpec){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkSpec){0};}
 dn_r_NetworkSpec dn_result=dn_f_network_spec(v_protocol,v_major,v_minor,v_minimum);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static int32_t dn_f_message_add_field(dn_r_MessageSchema * v_message,dn_r_MessageField v_field) {
{
if(({ uint32_t dn_tmp_0=(v_message)->f_field_count;
uint32_t dn_tmp_1=((uint32_t)(32ULL));
(dn_tmp_0 >= dn_tmp_1); })){
{ int32_t dn_tmp_2=((int32_t)(7ULL));return dn_tmp_2; }
}
else{
}
if(({ uint32_t dn_tmp_3=(v_field).f_id;
uint32_t dn_tmp_4=((uint32_t)(0ULL));
(dn_tmp_3 == dn_tmp_4); })){
{ int32_t dn_tmp_5=((int32_t)(4ULL));return dn_tmp_5; }
}
else{
}
if(({ uint32_t dn_tmp_6=(v_message)->f_field_count;
uint32_t dn_tmp_7=((uint32_t)(0ULL));
(dn_tmp_6 != dn_tmp_7); })){
if(({ uint32_t dn_tmp_8=((*({ dn_t_arr32_r12_MessageField *dn_tmp_9=&((v_message)->f_fields);size_t dn_tmp_10=({ uint32_t dn_tmp_11=({ uint32_t dn_tmp_12=(v_message)->f_field_count;
uint32_t dn_tmp_13=((uint32_t)(1ULL));
dn_U32_sub(dn_tmp_12,dn_tmp_13); });
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_11; });&dn_tmp_9->data[dn_index(dn_tmp_10,32)]; }))).f_id;
uint32_t dn_tmp_14=(v_field).f_id;
(dn_tmp_8 >= dn_tmp_14); })){
{ int32_t dn_tmp_15=((int32_t)(4ULL));return dn_tmp_15; }
}
else{
}
}
else{
}
{ dn_r_MessageField dn_tmp_20=v_field;dn_r_MessageField *dn_tmp_19=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_16=&((v_message)->f_fields);size_t dn_tmp_17=({ uint32_t dn_tmp_18=(v_message)->f_field_count;
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_18; });&dn_tmp_16->data[dn_index(dn_tmp_17,32)]; })));*dn_tmp_19=dn_tmp_20; }
{ uint32_t dn_tmp_24=({ uint32_t dn_tmp_21=(v_message)->f_field_count;
uint32_t dn_tmp_22=((uint32_t)(1ULL));
dn_U32_add(dn_tmp_21,dn_tmp_22); });uint32_t *dn_tmp_23=&((v_message)->f_field_count);*dn_tmp_23=dn_tmp_24; }
{ int32_t dn_tmp_25=((int32_t)(0ULL));return dn_tmp_25; }
}

dn_fail("missing native return");
}
static int32_t dn_cb_message_add_field(dn_r_MessageSchema * v_message,dn_r_MessageField v_field) {
 if(!dn_enter()){return (int32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (int32_t){0};}
 int32_t dn_result=dn_f_message_add_field(v_message,v_field);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static int32_t dn_f_network_add_message(dn_r_NetworkSpec * v_spec,dn_r_MessageSchema v_message) {
{
if(({ uint32_t dn_tmp_0=(v_spec)->f_message_count;
uint32_t dn_tmp_1=((uint32_t)(16ULL));
(dn_tmp_0 >= dn_tmp_1); })){
{ int32_t dn_tmp_2=((int32_t)(7ULL));return dn_tmp_2; }
}
else{
}
if(({ uint32_t dn_tmp_3=(v_message).f_id;
uint32_t dn_tmp_4=((uint32_t)(0ULL));
(dn_tmp_3 == dn_tmp_4); })){
{ int32_t dn_tmp_5=((int32_t)(4ULL));return dn_tmp_5; }
}
else{
}
if(({ uint32_t dn_tmp_6=(v_spec)->f_message_count;
uint32_t dn_tmp_7=((uint32_t)(0ULL));
(dn_tmp_6 != dn_tmp_7); })){
if(({ uint32_t dn_tmp_8=((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_9=&((v_spec)->f_messages);size_t dn_tmp_10=({ uint32_t dn_tmp_11=({ uint32_t dn_tmp_12=(v_spec)->f_message_count;
uint32_t dn_tmp_13=((uint32_t)(1ULL));
dn_U32_sub(dn_tmp_12,dn_tmp_13); });
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_11; });&dn_tmp_9->data[dn_index(dn_tmp_10,16)]; }))).f_id;
uint32_t dn_tmp_14=(v_message).f_id;
(dn_tmp_8 >= dn_tmp_14); })){
{ int32_t dn_tmp_15=((int32_t)(4ULL));return dn_tmp_15; }
}
else{
}
}
else{
}
{ dn_r_MessageSchema dn_tmp_20=v_message;dn_r_MessageSchema *dn_tmp_19=&((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_16=&((v_spec)->f_messages);size_t dn_tmp_17=({ uint32_t dn_tmp_18=(v_spec)->f_message_count;
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_18; });&dn_tmp_16->data[dn_index(dn_tmp_17,16)]; })));*dn_tmp_19=dn_tmp_20; }
{ uint32_t dn_tmp_24=({ uint32_t dn_tmp_21=(v_spec)->f_message_count;
uint32_t dn_tmp_22=((uint32_t)(1ULL));
dn_U32_add(dn_tmp_21,dn_tmp_22); });uint32_t *dn_tmp_23=&((v_spec)->f_message_count);*dn_tmp_23=dn_tmp_24; }
{ int32_t dn_tmp_25=((int32_t)(0ULL));return dn_tmp_25; }
}

dn_fail("missing native return");
}
static int32_t dn_cb_network_add_message(dn_r_NetworkSpec * v_spec,dn_r_MessageSchema v_message) {
 if(!dn_enter()){return (int32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (int32_t){0};}
 int32_t dn_result=dn_f_network_add_message(v_spec,v_message);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_network_minor_active(uint32_t v_first,uint32_t v_last,uint32_t v_minor) {
{
{ bool dn_tmp_4=(({ uint32_t dn_tmp_0=v_first;
uint32_t dn_tmp_1=v_minor;
(dn_tmp_0 <= dn_tmp_1); }) && ({ uint32_t dn_tmp_2=v_minor;
uint32_t dn_tmp_3=v_last;
(dn_tmp_2 <= dn_tmp_3); }));return dn_tmp_4; }
}

dn_fail("missing native return");
}
static bool dn_cb_network_minor_active(uint32_t v_first,uint32_t v_last,uint32_t v_minor) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_network_minor_active(v_first,v_last,v_minor);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static int32_t dn_f_network_field_check(dn_r_MessageField const * v_field) {
{
if(((((((((({ uint32_t dn_tmp_0=(v_field)->f_id;
uint32_t dn_tmp_1=((uint32_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); }) || ({ uint32_t dn_tmp_2=(v_field)->f_kind;
uint32_t dn_tmp_3=((uint32_t)(0ULL));
(dn_tmp_2 == dn_tmp_3); })) || ({ uint32_t dn_tmp_4=(v_field)->f_kind;
uint32_t dn_tmp_5=((uint32_t)(9ULL));
(dn_tmp_4 > dn_tmp_5); })) || ({ uint32_t dn_tmp_6=(v_field)->f_minimum_count;
uint32_t dn_tmp_7=(v_field)->f_maximum_count;
(dn_tmp_6 > dn_tmp_7); })) || ({ uint32_t dn_tmp_8=(v_field)->f_maximum_count;
uint32_t dn_tmp_9=((uint32_t)(0ULL));
(dn_tmp_8 == dn_tmp_9); })) || ({ uint32_t dn_tmp_10=(v_field)->f_maximum_count;
uint32_t dn_tmp_11=((uint32_t)(4096ULL));
(dn_tmp_10 > dn_tmp_11); })) || ({ uint32_t dn_tmp_12=(v_field)->f_maximum_length;
uint32_t dn_tmp_13=((uint32_t)(16777216ULL));
(dn_tmp_12 > dn_tmp_13); })) || ({ uint32_t dn_tmp_14=(v_field)->f_first_minor;
uint32_t dn_tmp_15=(v_field)->f_last_minor;
(dn_tmp_14 > dn_tmp_15); })) || ({ uint32_t dn_tmp_16=({ uint32_t dn_tmp_17=(v_field)->f_flags;
uint32_t dn_tmp_18=({ uint32_t dn_tmp_19=((uint32_t)(4294967294ULL));
if((unsigned __int128)dn_tmp_19>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_19; });
dn_U32_bits((uint64_t)dn_tmp_17 & (uint64_t)dn_tmp_18); });
uint32_t dn_tmp_20=((uint32_t)(0ULL));
(dn_tmp_16 != dn_tmp_20); }))){
{ int32_t dn_tmp_21=((int32_t)(4ULL));return dn_tmp_21; }
}
else{
}
if(((({ uint32_t dn_tmp_22=(v_field)->f_kind;
uint32_t dn_tmp_23=((uint32_t)(6ULL));
(dn_tmp_22 != dn_tmp_23); }) && ({ uint32_t dn_tmp_24=(v_field)->f_kind;
uint32_t dn_tmp_25=((uint32_t)(7ULL));
(dn_tmp_24 != dn_tmp_25); })) && ({ uint32_t dn_tmp_26=(v_field)->f_maximum_length;
uint32_t dn_tmp_27=((uint32_t)(0ULL));
(dn_tmp_26 != dn_tmp_27); }))){
{ int32_t dn_tmp_28=((int32_t)(4ULL));return dn_tmp_28; }
}
else{
}
{ int32_t dn_tmp_29=((int32_t)(0ULL));return dn_tmp_29; }
}

dn_fail("missing native return");
}
static int32_t dn_cb_network_field_check(dn_r_MessageField const * v_field) {
 if(!dn_enter()){return (int32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (int32_t){0};}
 int32_t dn_result=dn_f_network_field_check(v_field);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static int32_t dn_f_network_wire_check(dn_r_WireFormat v_wire) {
{
if(({ uint32_t dn_tmp_0=(v_wire).f_kind;
uint32_t dn_tmp_1=((uint32_t)(1ULL));
(dn_tmp_0 == dn_tmp_1); })){
if((((({ uint32_t dn_tmp_2=(v_wire).f_endian;
uint32_t dn_tmp_3=((uint32_t)(1ULL));
(dn_tmp_2 == dn_tmp_3); }) || ({ uint32_t dn_tmp_4=(v_wire).f_endian;
uint32_t dn_tmp_5=((uint32_t)(2ULL));
(dn_tmp_4 == dn_tmp_5); })) && ({ uint32_t dn_tmp_6=(v_wire).f_envelope_version;
uint32_t dn_tmp_7=((uint32_t)(0ULL));
(dn_tmp_6 == dn_tmp_7); })) && ({ uint32_t dn_tmp_8=(v_wire).f_unknown_fields;
uint32_t dn_tmp_9=((uint32_t)(0ULL));
(dn_tmp_8 == dn_tmp_9); }))){
{ int32_t dn_tmp_10=((int32_t)(0ULL));return dn_tmp_10; }
}
else{
}
}
else{
}
if(({ uint32_t dn_tmp_11=(v_wire).f_kind;
uint32_t dn_tmp_12=((uint32_t)(2ULL));
(dn_tmp_11 == dn_tmp_12); })){
if(((({ uint32_t dn_tmp_13=(v_wire).f_endian;
uint32_t dn_tmp_14=((uint32_t)(2ULL));
(dn_tmp_13 == dn_tmp_14); }) && (({ uint32_t dn_tmp_15=(v_wire).f_envelope_version;
uint32_t dn_tmp_16=((uint32_t)(1ULL));
(dn_tmp_15 == dn_tmp_16); }) || ({ uint32_t dn_tmp_17=(v_wire).f_envelope_version;
uint32_t dn_tmp_18=((uint32_t)(2ULL));
(dn_tmp_17 == dn_tmp_18); }))) && ({ uint32_t dn_tmp_19=(v_wire).f_unknown_fields;
uint32_t dn_tmp_20=((uint32_t)(1ULL));
(dn_tmp_19 <= dn_tmp_20); }))){
{ int32_t dn_tmp_21=((int32_t)(0ULL));return dn_tmp_21; }
}
else{
}
}
else{
}
{ int32_t dn_tmp_22=((int32_t)(4ULL));return dn_tmp_22; }
}

dn_fail("missing native return");
}
static int32_t dn_cb_network_wire_check(dn_r_WireFormat v_wire) {
 if(!dn_enter()){return (int32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (int32_t){0};}
 int32_t dn_result=dn_f_network_wire_check(v_wire);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_SchemaMeasure dn_f_network_field_measure(dn_r_MessageField const * v_field,dn_r_WireFormat v_wire) {
{
if((({ int32_t dn_tmp_0=({ dn_r_MessageField const * dn_tmp_1=v_field;
dn_f_network_field_check(dn_tmp_1); });
int32_t dn_tmp_2=((int32_t)(0ULL));
(dn_tmp_0 != dn_tmp_2); }) || ({ int32_t dn_tmp_3=({ dn_r_WireFormat dn_tmp_4=v_wire;
dn_f_network_wire_check(dn_tmp_4); });
int32_t dn_tmp_5=((int32_t)(0ULL));
(dn_tmp_3 != dn_tmp_5); }))){
{ dn_r_SchemaMeasure dn_tmp_9=({ uint64_t dn_tmp_6=((uint64_t)(0ULL));
uint64_t dn_tmp_7=((uint64_t)(0ULL));
int32_t dn_tmp_8=((int32_t)(4ULL));
((dn_r_SchemaMeasure){dn_tmp_6,dn_tmp_7,dn_tmp_8}); });return dn_tmp_9; }
}
else{
}
uint64_t v_minimum=((uint64_t)(1ULL));
uint64_t v_maximum=((uint64_t)(1ULL));
if(({ uint32_t dn_tmp_10=(v_field)->f_kind;
uint32_t dn_tmp_11=((uint32_t)(2ULL));
(dn_tmp_10 == dn_tmp_11); })){
{ uint64_t dn_tmp_13=((uint64_t)(2ULL));uint64_t *dn_tmp_12=&(v_minimum);*dn_tmp_12=dn_tmp_13; }
{ uint64_t dn_tmp_15=((uint64_t)(2ULL));uint64_t *dn_tmp_14=&(v_maximum);*dn_tmp_14=dn_tmp_15; }
}
else{
}
if(({ uint32_t dn_tmp_16=(v_field)->f_kind;
uint32_t dn_tmp_17=((uint32_t)(3ULL));
(dn_tmp_16 == dn_tmp_17); })){
{ uint64_t dn_tmp_19=((uint64_t)(4ULL));uint64_t *dn_tmp_18=&(v_minimum);*dn_tmp_18=dn_tmp_19; }
{ uint64_t dn_tmp_21=((uint64_t)(4ULL));uint64_t *dn_tmp_20=&(v_maximum);*dn_tmp_20=dn_tmp_21; }
}
else{
}
if(({ uint32_t dn_tmp_22=(v_field)->f_kind;
uint32_t dn_tmp_23=((uint32_t)(4ULL));
(dn_tmp_22 == dn_tmp_23); })){
{ uint64_t dn_tmp_25=((uint64_t)(8ULL));uint64_t *dn_tmp_24=&(v_minimum);*dn_tmp_24=dn_tmp_25; }
{ uint64_t dn_tmp_27=((uint64_t)(8ULL));uint64_t *dn_tmp_26=&(v_maximum);*dn_tmp_26=dn_tmp_27; }
}
else{
}
if((({ uint32_t dn_tmp_28=(v_field)->f_kind;
uint32_t dn_tmp_29=((uint32_t)(6ULL));
(dn_tmp_28 == dn_tmp_29); }) || ({ uint32_t dn_tmp_30=(v_field)->f_kind;
uint32_t dn_tmp_31=((uint32_t)(7ULL));
(dn_tmp_30 == dn_tmp_31); }))){
{ uint64_t dn_tmp_33=((uint64_t)(0ULL));uint64_t *dn_tmp_32=&(v_minimum);*dn_tmp_32=dn_tmp_33; }
{ uint64_t dn_tmp_36=({ uint32_t dn_tmp_34=(v_field)->f_maximum_length;
if((unsigned __int128)dn_tmp_34>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_34; });uint64_t *dn_tmp_35=&(v_maximum);*dn_tmp_35=dn_tmp_36; }
if(({ uint32_t dn_tmp_37=(v_wire).f_kind;
uint32_t dn_tmp_38=((uint32_t)(1ULL));
(dn_tmp_37 == dn_tmp_38); })){
{ uint64_t dn_tmp_40=((uint64_t)(4ULL));uint64_t *dn_tmp_39=&(v_minimum);*dn_tmp_39=dn_tmp_40; }
{ uint64_t dn_tmp_44=({ uint64_t dn_tmp_41=v_maximum;
uint64_t dn_tmp_42=((uint64_t)(4ULL));
dn_U64_add(dn_tmp_41,dn_tmp_42); });uint64_t *dn_tmp_43=&(v_maximum);*dn_tmp_43=dn_tmp_44; }
}
else{
}
}
else{
}
if((({ uint32_t dn_tmp_45=(v_field)->f_kind;
uint32_t dn_tmp_46=((uint32_t)(8ULL));
(dn_tmp_45 == dn_tmp_46); }) || ({ uint32_t dn_tmp_47=(v_field)->f_kind;
uint32_t dn_tmp_48=((uint32_t)(9ULL));
(dn_tmp_47 == dn_tmp_48); }))){
{ uint64_t dn_tmp_50=((uint64_t)(10ULL));uint64_t *dn_tmp_49=&(v_maximum);*dn_tmp_49=dn_tmp_50; }
}
else{
}
uint64_t v_prefix=((uint64_t)(0ULL));
if(({ uint32_t dn_tmp_51=(v_wire).f_kind;
uint32_t dn_tmp_52=((uint32_t)(2ULL));
(dn_tmp_51 == dn_tmp_52); })){
{ uint64_t dn_tmp_56=({ uint64_t dn_tmp_53=v_minimum;
uint64_t dn_tmp_54=((uint64_t)(8ULL));
dn_U64_add(dn_tmp_53,dn_tmp_54); });uint64_t *dn_tmp_55=&(v_minimum);*dn_tmp_55=dn_tmp_56; }
{ uint64_t dn_tmp_60=({ uint64_t dn_tmp_57=v_maximum;
uint64_t dn_tmp_58=((uint64_t)(8ULL));
dn_U64_add(dn_tmp_57,dn_tmp_58); });uint64_t *dn_tmp_59=&(v_maximum);*dn_tmp_59=dn_tmp_60; }
}
else{
if((({ uint32_t dn_tmp_61=(v_field)->f_minimum_count;
uint32_t dn_tmp_62=((uint32_t)(1ULL));
(dn_tmp_61 != dn_tmp_62); }) || ({ uint32_t dn_tmp_63=(v_field)->f_maximum_count;
uint32_t dn_tmp_64=((uint32_t)(1ULL));
(dn_tmp_63 != dn_tmp_64); }))){
{ uint64_t dn_tmp_66=((uint64_t)(4ULL));uint64_t *dn_tmp_65=&(v_prefix);*dn_tmp_65=dn_tmp_66; }
}
else{
}
}
{ dn_r_SchemaMeasure dn_tmp_80=({ uint64_t dn_tmp_67=({ uint64_t dn_tmp_68=v_prefix;
uint64_t dn_tmp_69=({ uint64_t dn_tmp_70=v_minimum;
uint64_t dn_tmp_71=({ uint32_t dn_tmp_72=(v_field)->f_minimum_count;
if((unsigned __int128)dn_tmp_72>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_72; });
dn_U64_mul(dn_tmp_70,dn_tmp_71); });
dn_U64_add(dn_tmp_68,dn_tmp_69); });
uint64_t dn_tmp_73=({ uint64_t dn_tmp_74=v_prefix;
uint64_t dn_tmp_75=({ uint64_t dn_tmp_76=v_maximum;
uint64_t dn_tmp_77=({ uint32_t dn_tmp_78=(v_field)->f_maximum_count;
if((unsigned __int128)dn_tmp_78>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_78; });
dn_U64_mul(dn_tmp_76,dn_tmp_77); });
dn_U64_add(dn_tmp_74,dn_tmp_75); });
int32_t dn_tmp_79=((int32_t)(0ULL));
((dn_r_SchemaMeasure){dn_tmp_67,dn_tmp_73,dn_tmp_79}); });return dn_tmp_80; }
}

dn_fail("missing native return");
}
static dn_r_SchemaMeasure dn_cb_network_field_measure(dn_r_MessageField const * v_field,dn_r_WireFormat v_wire) {
 if(!dn_enter()){return (dn_r_SchemaMeasure){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_SchemaMeasure){0};}
 dn_r_SchemaMeasure dn_result=dn_f_network_field_measure(v_field,v_wire);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_SchemaMeasure dn_f_network_message_measure(dn_r_MessageSchema const * v_message,dn_r_WireFormat v_wire,uint32_t v_minor) {
{
if((({ uint32_t dn_tmp_0=(v_message)->f_field_count;
uint32_t dn_tmp_1=((uint32_t)(32ULL));
(dn_tmp_0 > dn_tmp_1); }) || ({ int32_t dn_tmp_2=({ dn_r_WireFormat dn_tmp_3=v_wire;
dn_f_network_wire_check(dn_tmp_3); });
int32_t dn_tmp_4=((int32_t)(0ULL));
(dn_tmp_2 != dn_tmp_4); }))){
{ dn_r_SchemaMeasure dn_tmp_8=({ uint64_t dn_tmp_5=((uint64_t)(0ULL));
uint64_t dn_tmp_6=((uint64_t)(0ULL));
int32_t dn_tmp_7=((int32_t)(4ULL));
((dn_r_SchemaMeasure){dn_tmp_5,dn_tmp_6,dn_tmp_7}); });return dn_tmp_8; }
}
else{
}
if((!(({ uint32_t dn_tmp_9=(v_message)->f_first_minor;
uint32_t dn_tmp_10=(v_message)->f_last_minor;
uint32_t dn_tmp_11=v_minor;
dn_f_network_minor_active(dn_tmp_9,dn_tmp_10,dn_tmp_11); })))){
{ dn_r_SchemaMeasure dn_tmp_15=({ uint64_t dn_tmp_12=((uint64_t)(0ULL));
uint64_t dn_tmp_13=((uint64_t)(0ULL));
int32_t dn_tmp_14=((int32_t)(6ULL));
((dn_r_SchemaMeasure){dn_tmp_12,dn_tmp_13,dn_tmp_14}); });return dn_tmp_15; }
}
else{
}
uint64_t v_minimum=((uint64_t)(4ULL));
if(({ uint32_t dn_tmp_16=(v_wire).f_kind;
uint32_t dn_tmp_17=((uint32_t)(2ULL));
(dn_tmp_16 == dn_tmp_17); })){
{ uint64_t dn_tmp_19=((uint64_t)(12ULL));uint64_t *dn_tmp_18=&(v_minimum);*dn_tmp_18=dn_tmp_19; }
}
else{
}
uint64_t v_maximum=v_minimum;
{ size_t dn_tmp_22=((size_t)(0ULL));size_t dn_tmp_21=({ uint32_t dn_tmp_20=(v_message)->f_field_count;
if((unsigned __int128)dn_tmp_20>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_20; });for(size_t v_index=dn_tmp_22;v_index<dn_tmp_21;v_index++){
dn_r_MessageField const * v_field=(&((*({ dn_t_arr32_r12_MessageField *dn_tmp_23=&((v_message)->f_fields);size_t dn_tmp_24=v_index;&dn_tmp_23->data[dn_index(dn_tmp_24,32)]; }))));
if(({ uint32_t dn_tmp_25=(v_field)->f_first_minor;
uint32_t dn_tmp_26=(v_field)->f_last_minor;
uint32_t dn_tmp_27=v_minor;
dn_f_network_minor_active(dn_tmp_25,dn_tmp_26,dn_tmp_27); })){
dn_r_SchemaMeasure v_measured=({ dn_r_MessageField const * dn_tmp_28=v_field;
dn_r_WireFormat dn_tmp_29=v_wire;
dn_f_network_field_measure(dn_tmp_28,dn_tmp_29); });
if(({ int32_t dn_tmp_30=(v_measured).f_status;
int32_t dn_tmp_31=((int32_t)(0ULL));
(dn_tmp_30 != dn_tmp_31); })){
{ dn_r_SchemaMeasure dn_tmp_32=v_measured;return dn_tmp_32; }
}
else{
}
{ uint64_t dn_tmp_36=({ uint64_t dn_tmp_33=v_minimum;
uint64_t dn_tmp_34=(v_measured).f_minimum;
dn_U64_add(dn_tmp_33,dn_tmp_34); });uint64_t *dn_tmp_35=&(v_minimum);*dn_tmp_35=dn_tmp_36; }
{ uint64_t dn_tmp_40=({ uint64_t dn_tmp_37=v_maximum;
uint64_t dn_tmp_38=(v_measured).f_maximum;
dn_U64_add(dn_tmp_37,dn_tmp_38); });uint64_t *dn_tmp_39=&(v_maximum);*dn_tmp_39=dn_tmp_40; }
}
else{
}
}
 }
{ dn_r_SchemaMeasure dn_tmp_44=({ uint64_t dn_tmp_41=v_minimum;
uint64_t dn_tmp_42=v_maximum;
int32_t dn_tmp_43=((int32_t)(0ULL));
((dn_r_SchemaMeasure){dn_tmp_41,dn_tmp_42,dn_tmp_43}); });return dn_tmp_44; }
}

dn_fail("missing native return");
}
static dn_r_SchemaMeasure dn_cb_network_message_measure(dn_r_MessageSchema const * v_message,dn_r_WireFormat v_wire,uint32_t v_minor) {
 if(!dn_enter()){return (dn_r_SchemaMeasure){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_SchemaMeasure){0};}
 dn_r_SchemaMeasure dn_result=dn_f_network_message_measure(v_message,v_wire,v_minor);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkSpecCheck dn_f_network_spec_failure(int32_t v_status,uint32_t v_component,uint32_t v_message,uint32_t v_field) {
{
dn_r_NetworkSpecCheck v_result=({ ((dn_r_NetworkSpecCheck){0}); });
{ int32_t dn_tmp_1=v_status;int32_t *dn_tmp_0=&((v_result).f_status);*dn_tmp_0=dn_tmp_1; }
{ uint32_t dn_tmp_3=v_component;uint32_t *dn_tmp_2=&((v_result).f_component);*dn_tmp_2=dn_tmp_3; }
{ uint32_t dn_tmp_5=v_message;uint32_t *dn_tmp_4=&((v_result).f_message);*dn_tmp_4=dn_tmp_5; }
{ uint32_t dn_tmp_7=v_field;uint32_t *dn_tmp_6=&((v_result).f_field);*dn_tmp_6=dn_tmp_7; }
{ dn_r_NetworkSpecCheck dn_tmp_8=v_result;return dn_tmp_8; }
}

dn_fail("missing native return");
}
static dn_r_NetworkSpecCheck dn_cb_network_spec_failure(int32_t v_status,uint32_t v_component,uint32_t v_message,uint32_t v_field) {
 if(!dn_enter()){return (dn_r_NetworkSpecCheck){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkSpecCheck){0};}
 dn_r_NetworkSpecCheck dn_result=dn_f_network_spec_failure(v_status,v_component,v_message,v_field);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_network_has_message(dn_r_NetworkSpec const * v_spec,uint32_t v_minor) {
{
if(({ uint32_t dn_tmp_0=(v_spec)->f_message_count;
uint32_t dn_tmp_1=((uint32_t)(16ULL));
(dn_tmp_0 > dn_tmp_1); })){
{ bool dn_tmp_2=0;return dn_tmp_2; }
}
else{
}
{ size_t dn_tmp_5=((size_t)(0ULL));size_t dn_tmp_4=({ uint32_t dn_tmp_3=(v_spec)->f_message_count;
if((unsigned __int128)dn_tmp_3>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_3; });for(size_t v_index=dn_tmp_5;v_index<dn_tmp_4;v_index++){
dn_r_MessageSchema const * v_message=(&((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_6=&((v_spec)->f_messages);size_t dn_tmp_7=v_index;&dn_tmp_6->data[dn_index(dn_tmp_7,16)]; }))));
if(({ uint32_t dn_tmp_8=(v_message)->f_first_minor;
uint32_t dn_tmp_9=(v_message)->f_last_minor;
uint32_t dn_tmp_10=v_minor;
dn_f_network_minor_active(dn_tmp_8,dn_tmp_9,dn_tmp_10); })){
{ bool dn_tmp_11=1;return dn_tmp_11; }
}
else{
}
}
 }
{ bool dn_tmp_12=0;return dn_tmp_12; }
}

dn_fail("missing native return");
}
static bool dn_cb_network_has_message(dn_r_NetworkSpec const * v_spec,uint32_t v_minor) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_network_has_message(v_spec,v_minor);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkSpecCheck dn_f_network_spec_check(dn_r_NetworkSpec const * v_spec) {
{
if(((((({ uint32_t dn_tmp_0=(v_spec)->f_protocol;
uint32_t dn_tmp_1=((uint32_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); }) || ({ uint32_t dn_tmp_2=((v_spec)->f_version).f_major;
uint32_t dn_tmp_3=((uint32_t)(0ULL));
(dn_tmp_2 == dn_tmp_3); })) || ({ uint32_t dn_tmp_4=((v_spec)->f_version).f_major;
uint32_t dn_tmp_5=((uint32_t)(65535ULL));
(dn_tmp_4 > dn_tmp_5); })) || ({ uint32_t dn_tmp_6=((v_spec)->f_version).f_minor;
uint32_t dn_tmp_7=((uint32_t)(65535ULL));
(dn_tmp_6 > dn_tmp_7); })) || ({ uint32_t dn_tmp_8=((v_spec)->f_version).f_minimum_minor;
uint32_t dn_tmp_9=((v_spec)->f_version).f_minor;
(dn_tmp_8 > dn_tmp_9); }))){
{ dn_r_NetworkSpecCheck dn_tmp_14=({ int32_t dn_tmp_10=((int32_t)(4ULL));
uint32_t dn_tmp_11=((uint32_t)(1ULL));
uint32_t dn_tmp_12=((uint32_t)(0ULL));
uint32_t dn_tmp_13=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_10,dn_tmp_11,dn_tmp_12,dn_tmp_13); });return dn_tmp_14; }
}
else{
}
if(({ int32_t dn_tmp_15=({ dn_r_WireFormat dn_tmp_16=(v_spec)->f_wire;
dn_f_network_wire_check(dn_tmp_16); });
int32_t dn_tmp_17=((int32_t)(0ULL));
(dn_tmp_15 != dn_tmp_17); })){
{ dn_r_NetworkSpecCheck dn_tmp_22=({ int32_t dn_tmp_18=((int32_t)(4ULL));
uint32_t dn_tmp_19=((uint32_t)(2ULL));
uint32_t dn_tmp_20=((uint32_t)(0ULL));
uint32_t dn_tmp_21=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_18,dn_tmp_19,dn_tmp_20,dn_tmp_21); });return dn_tmp_22; }
}
else{
}
if((!(({ dn_r_TransportRequirements dn_tmp_23=(v_spec)->f_transport;
dn_f_transport_requirements_valid(dn_tmp_23); })))){
{ dn_r_NetworkSpecCheck dn_tmp_28=({ int32_t dn_tmp_24=((int32_t)(4ULL));
uint32_t dn_tmp_25=((uint32_t)(3ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
uint32_t dn_tmp_27=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_24,dn_tmp_25,dn_tmp_26,dn_tmp_27); });return dn_tmp_28; }
}
else{
}
if(((((!(({ dn_r_TransportBounds dn_tmp_29=((v_spec)->f_resources).f_transport;
dn_f_transport_bounds_valid(dn_tmp_29); }))) || ({ uint64_t dn_tmp_30=((v_spec)->f_resources).f_maximum_session_messages;
uint64_t dn_tmp_31=((uint64_t)(0ULL));
(dn_tmp_30 == dn_tmp_31); })) || ({ uint32_t dn_tmp_32=((v_spec)->f_resources).f_maximum_schema_nodes;
uint32_t dn_tmp_33=((uint32_t)(0ULL));
(dn_tmp_32 == dn_tmp_33); })) || ({ uint32_t dn_tmp_34=((v_spec)->f_resources).f_maximum_schema_nodes;
uint32_t dn_tmp_35=((uint32_t)(529ULL));
(dn_tmp_34 > dn_tmp_35); }))){
{ dn_r_NetworkSpecCheck dn_tmp_40=({ int32_t dn_tmp_36=((int32_t)(4ULL));
uint32_t dn_tmp_37=((uint32_t)(3ULL));
uint32_t dn_tmp_38=((uint32_t)(0ULL));
uint32_t dn_tmp_39=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_36,dn_tmp_37,dn_tmp_38,dn_tmp_39); });return dn_tmp_40; }
}
else{
}
if((({ uint32_t dn_tmp_41=((v_spec)->f_resources).f_maximum_unknown_fields;
uint32_t dn_tmp_42=((uint32_t)(4096ULL));
(dn_tmp_41 > dn_tmp_42); }) || ({ uint32_t dn_tmp_43=((v_spec)->f_resources).f_maximum_unknown_bytes;
uint32_t dn_tmp_44=(((v_spec)->f_resources).f_transport).f_max_message;
(dn_tmp_43 > dn_tmp_44); }))){
{ dn_r_NetworkSpecCheck dn_tmp_49=({ int32_t dn_tmp_45=((int32_t)(7ULL));
uint32_t dn_tmp_46=((uint32_t)(3ULL));
uint32_t dn_tmp_47=((uint32_t)(0ULL));
uint32_t dn_tmp_48=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_45,dn_tmp_46,dn_tmp_47,dn_tmp_48); });return dn_tmp_49; }
}
else{
}
if(({ uint32_t dn_tmp_50=((v_spec)->f_wire).f_unknown_fields;
uint32_t dn_tmp_51=((uint32_t)(0ULL));
(dn_tmp_50 == dn_tmp_51); })){
if((({ uint32_t dn_tmp_52=((v_spec)->f_resources).f_maximum_unknown_fields;
uint32_t dn_tmp_53=((uint32_t)(0ULL));
(dn_tmp_52 != dn_tmp_53); }) || ({ uint32_t dn_tmp_54=((v_spec)->f_resources).f_maximum_unknown_bytes;
uint32_t dn_tmp_55=((uint32_t)(0ULL));
(dn_tmp_54 != dn_tmp_55); }))){
{ dn_r_NetworkSpecCheck dn_tmp_60=({ int32_t dn_tmp_56=((int32_t)(4ULL));
uint32_t dn_tmp_57=((uint32_t)(3ULL));
uint32_t dn_tmp_58=((uint32_t)(0ULL));
uint32_t dn_tmp_59=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_56,dn_tmp_57,dn_tmp_58,dn_tmp_59); });return dn_tmp_60; }
}
else{
}
}
else{
if((({ uint32_t dn_tmp_61=((v_spec)->f_resources).f_maximum_unknown_fields;
uint32_t dn_tmp_62=((uint32_t)(0ULL));
(dn_tmp_61 == dn_tmp_62); }) || ({ uint32_t dn_tmp_63=((v_spec)->f_resources).f_maximum_unknown_bytes;
uint32_t dn_tmp_64=((uint32_t)(8ULL));
(dn_tmp_63 < dn_tmp_64); }))){
{ dn_r_NetworkSpecCheck dn_tmp_69=({ int32_t dn_tmp_65=((int32_t)(4ULL));
uint32_t dn_tmp_66=((uint32_t)(3ULL));
uint32_t dn_tmp_67=((uint32_t)(0ULL));
uint32_t dn_tmp_68=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_65,dn_tmp_66,dn_tmp_67,dn_tmp_68); });return dn_tmp_69; }
}
else{
}
}
dn_r_SessionContract v_session=(v_spec)->f_session;
if((((((({ uint32_t dn_tmp_70=(v_session).f_handshake_timeout_ms;
uint32_t dn_tmp_71=((uint32_t)(0ULL));
(dn_tmp_70 == dn_tmp_71); }) || ({ uint32_t dn_tmp_72=(v_session).f_idle_timeout_ms;
uint32_t dn_tmp_73=(v_session).f_handshake_timeout_ms;
(dn_tmp_72 < dn_tmp_73); })) || ({ uint32_t dn_tmp_74=(v_session).f_maximum_duration_ms;
uint32_t dn_tmp_75=(v_session).f_idle_timeout_ms;
(dn_tmp_74 < dn_tmp_75); })) || ({ uint32_t dn_tmp_76=(v_session).f_maximum_duration_ms;
uint32_t dn_tmp_77=((uint32_t)(2147483647ULL));
(dn_tmp_76 > dn_tmp_77); })) || ({ uint32_t dn_tmp_78=(v_session).f_maximum_in_flight;
uint32_t dn_tmp_79=((uint32_t)(0ULL));
(dn_tmp_78 == dn_tmp_79); })) || ({ uint32_t dn_tmp_80=(v_session).f_maximum_in_flight;
uint32_t dn_tmp_81=(((v_spec)->f_resources).f_transport).f_queued_messages;
(dn_tmp_80 > dn_tmp_81); }))){
{ dn_r_NetworkSpecCheck dn_tmp_86=({ int32_t dn_tmp_82=((int32_t)(4ULL));
uint32_t dn_tmp_83=((uint32_t)(4ULL));
uint32_t dn_tmp_84=((uint32_t)(0ULL));
uint32_t dn_tmp_85=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_82,dn_tmp_83,dn_tmp_84,dn_tmp_85); });return dn_tmp_86; }
}
else{
}
if(((({ uint32_t dn_tmp_87=((v_spec)->f_security).f_authentication;
uint32_t dn_tmp_88=((uint32_t)(2ULL));
(dn_tmp_87 > dn_tmp_88); }) || ({ uint32_t dn_tmp_89=((v_spec)->f_security).f_replay_scope;
uint32_t dn_tmp_90=((uint32_t)(2ULL));
(dn_tmp_89 > dn_tmp_90); })) || ((v_session).f_authentication_before_data && ({ uint32_t dn_tmp_91=((v_spec)->f_security).f_authentication;
uint32_t dn_tmp_92=((uint32_t)(0ULL));
(dn_tmp_91 == dn_tmp_92); })))){
{ dn_r_NetworkSpecCheck dn_tmp_97=({ int32_t dn_tmp_93=((int32_t)(4ULL));
uint32_t dn_tmp_94=((uint32_t)(5ULL));
uint32_t dn_tmp_95=((uint32_t)(0ULL));
uint32_t dn_tmp_96=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_93,dn_tmp_94,dn_tmp_95,dn_tmp_96); });return dn_tmp_97; }
}
else{
}
if((({ uint32_t dn_tmp_98=({ uint32_t dn_tmp_99=((v_spec)->f_proofs).f_requested;
uint32_t dn_tmp_100=({ uint32_t dn_tmp_101=((uint32_t)(4294965248ULL));
if((unsigned __int128)dn_tmp_101>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_101; });
dn_U32_bits((uint64_t)dn_tmp_99 & (uint64_t)dn_tmp_100); });
uint32_t dn_tmp_102=((uint32_t)(0ULL));
(dn_tmp_98 != dn_tmp_102); }) || (({ uint32_t dn_tmp_103=((v_spec)->f_proofs).f_assurance;
uint32_t dn_tmp_104=((uint32_t)(1ULL));
(dn_tmp_103 != dn_tmp_104); }) && ({ uint32_t dn_tmp_105=((v_spec)->f_proofs).f_assurance;
uint32_t dn_tmp_106=((uint32_t)(2ULL));
(dn_tmp_105 != dn_tmp_106); })))){
{ dn_r_NetworkSpecCheck dn_tmp_111=({ int32_t dn_tmp_107=((int32_t)(4ULL));
uint32_t dn_tmp_108=((uint32_t)(8ULL));
uint32_t dn_tmp_109=((uint32_t)(0ULL));
uint32_t dn_tmp_110=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_107,dn_tmp_108,dn_tmp_109,dn_tmp_110); });return dn_tmp_111; }
}
else{
}
if((({ uint32_t dn_tmp_112=(v_spec)->f_message_count;
uint32_t dn_tmp_113=((uint32_t)(0ULL));
(dn_tmp_112 == dn_tmp_113); }) || ({ uint32_t dn_tmp_114=(v_spec)->f_message_count;
uint32_t dn_tmp_115=((uint32_t)(16ULL));
(dn_tmp_114 > dn_tmp_115); }))){
{ dn_r_NetworkSpecCheck dn_tmp_120=({ int32_t dn_tmp_116=((int32_t)(4ULL));
uint32_t dn_tmp_117=((uint32_t)(6ULL));
uint32_t dn_tmp_118=((uint32_t)(0ULL));
uint32_t dn_tmp_119=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_116,dn_tmp_117,dn_tmp_118,dn_tmp_119); });return dn_tmp_120; }
}
else{
}
dn_r_NetworkSpecCheck v_result=({ ((dn_r_NetworkSpecCheck){0}); });
{ uint32_t dn_tmp_124=({ uint32_t dn_tmp_121=((v_spec)->f_transport).f_required;
uint32_t dn_tmp_122=((uint32_t)(16ULL));
dn_U32_bits((uint64_t)dn_tmp_121 | (uint64_t)dn_tmp_122); });uint32_t *dn_tmp_123=&((v_result).f_required_properties);*dn_tmp_123=dn_tmp_124; }
{ uint32_t dn_tmp_128=({ uint32_t dn_tmp_125=((v_spec)->f_proofs).f_requested;
uint32_t dn_tmp_126=((uint32_t)(431ULL));
dn_U32_bits((uint64_t)dn_tmp_125 | (uint64_t)dn_tmp_126); });uint32_t *dn_tmp_127=&((v_result).f_required_proofs);*dn_tmp_127=dn_tmp_128; }
{ uint32_t dn_tmp_130=((uint32_t)(1ULL));uint32_t *dn_tmp_129=&((v_result).f_schema_nodes);*dn_tmp_129=dn_tmp_130; }
uint64_t v_minimum=((uint64_t)(18446744073709551615ULL));
uint64_t v_maximum=((uint64_t)(0ULL));
uint32_t v_previous=((uint32_t)(0ULL));
if((((({ uint32_t dn_tmp_131=((v_spec)->f_security).f_authentication;
uint32_t dn_tmp_132=((uint32_t)(0ULL));
(dn_tmp_131 != dn_tmp_132); }) || ((v_spec)->f_security).f_confidentiality) || ((v_spec)->f_security).f_integrity) || ({ uint32_t dn_tmp_133=((v_spec)->f_security).f_replay_scope;
uint32_t dn_tmp_134=((uint32_t)(0ULL));
(dn_tmp_133 != dn_tmp_134); }))){
{ uint32_t dn_tmp_138=({ uint32_t dn_tmp_135=(v_result).f_required_properties;
uint32_t dn_tmp_136=((uint32_t)(4ULL));
dn_U32_bits((uint64_t)dn_tmp_135 | (uint64_t)dn_tmp_136); });uint32_t *dn_tmp_137=&((v_result).f_required_properties);*dn_tmp_137=dn_tmp_138; }
}
else{
}
if(({ uint32_t dn_tmp_139=((v_spec)->f_security).f_authentication;
uint32_t dn_tmp_140=((uint32_t)(2ULL));
(dn_tmp_139 == dn_tmp_140); })){
{ uint32_t dn_tmp_144=({ uint32_t dn_tmp_141=(v_result).f_unsupported_security;
uint32_t dn_tmp_142=((uint32_t)(1ULL));
dn_U32_bits((uint64_t)dn_tmp_141 | (uint64_t)dn_tmp_142); });uint32_t *dn_tmp_143=&((v_result).f_unsupported_security);*dn_tmp_143=dn_tmp_144; }
}
else{
}
if(({ uint32_t dn_tmp_145=((v_spec)->f_security).f_replay_scope;
uint32_t dn_tmp_146=((uint32_t)(2ULL));
(dn_tmp_145 == dn_tmp_146); })){
{ uint32_t dn_tmp_150=({ uint32_t dn_tmp_147=(v_result).f_unsupported_security;
uint32_t dn_tmp_148=((uint32_t)(2ULL));
dn_U32_bits((uint64_t)dn_tmp_147 | (uint64_t)dn_tmp_148); });uint32_t *dn_tmp_149=&((v_result).f_unsupported_security);*dn_tmp_149=dn_tmp_150; }
}
else{
}
if(({ uint32_t dn_tmp_151=((v_spec)->f_wire).f_unknown_fields;
uint32_t dn_tmp_152=((uint32_t)(1ULL));
(dn_tmp_151 == dn_tmp_152); })){
{ uint32_t dn_tmp_156=({ uint32_t dn_tmp_153=(v_result).f_required_proofs;
uint32_t dn_tmp_154=((uint32_t)(16ULL));
dn_U32_bits((uint64_t)dn_tmp_153 | (uint64_t)dn_tmp_154); });uint32_t *dn_tmp_155=&((v_result).f_required_proofs);*dn_tmp_155=dn_tmp_156; }
}
else{
}
if((v_session).f_authentication_before_data){
{ uint32_t dn_tmp_160=({ uint32_t dn_tmp_157=(v_result).f_required_proofs;
uint32_t dn_tmp_158=((uint32_t)(64ULL));
dn_U32_bits((uint64_t)dn_tmp_157 | (uint64_t)dn_tmp_158); });uint32_t *dn_tmp_159=&((v_result).f_required_proofs);*dn_tmp_159=dn_tmp_160; }
}
else{
}
{ size_t dn_tmp_163=((size_t)(0ULL));size_t dn_tmp_162=({ uint32_t dn_tmp_161=(v_spec)->f_message_count;
if((unsigned __int128)dn_tmp_161>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_161; });for(size_t v_index=dn_tmp_163;v_index<dn_tmp_162;v_index++){
dn_r_MessageSchema const * v_message=(&((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_164=&((v_spec)->f_messages);size_t dn_tmp_165=v_index;&dn_tmp_164->data[dn_index(dn_tmp_165,16)]; }))));
{ uint32_t dn_tmp_169=({ uint32_t dn_tmp_166=(v_result).f_schema_nodes;
uint32_t dn_tmp_167=((uint32_t)(1ULL));
dn_U32_add(dn_tmp_166,dn_tmp_167); });uint32_t *dn_tmp_168=&((v_result).f_schema_nodes);*dn_tmp_168=dn_tmp_169; }
if(((((((((({ uint32_t dn_tmp_170=(v_message)->f_id;
uint32_t dn_tmp_171=v_previous;
(dn_tmp_170 <= dn_tmp_171); }) || ({ uint32_t dn_tmp_172=(v_message)->f_phase;
uint32_t dn_tmp_173=((uint32_t)(0ULL));
(dn_tmp_172 == dn_tmp_173); })) || ({ uint32_t dn_tmp_174=(v_message)->f_phase;
uint32_t dn_tmp_175=((uint32_t)(3ULL));
(dn_tmp_174 > dn_tmp_175); })) || ({ uint32_t dn_tmp_176=(v_message)->f_senders;
uint32_t dn_tmp_177=((uint32_t)(0ULL));
(dn_tmp_176 == dn_tmp_177); })) || ({ uint32_t dn_tmp_178=(v_message)->f_senders;
uint32_t dn_tmp_179=((uint32_t)(3ULL));
(dn_tmp_178 > dn_tmp_179); })) || ({ uint32_t dn_tmp_180=(v_message)->f_first_minor;
uint32_t dn_tmp_181=(v_message)->f_last_minor;
(dn_tmp_180 > dn_tmp_181); })) || ({ uint32_t dn_tmp_182=(v_message)->f_first_minor;
uint32_t dn_tmp_183=((v_spec)->f_version).f_minor;
(dn_tmp_182 > dn_tmp_183); })) || ({ uint32_t dn_tmp_184=(v_message)->f_last_minor;
uint32_t dn_tmp_185=((v_spec)->f_version).f_minimum_minor;
(dn_tmp_184 < dn_tmp_185); })) || ({ uint32_t dn_tmp_186=(v_message)->f_field_count;
uint32_t dn_tmp_187=((uint32_t)(32ULL));
(dn_tmp_186 > dn_tmp_187); }))){
{ dn_r_NetworkSpecCheck dn_tmp_192=({ int32_t dn_tmp_188=((int32_t)(4ULL));
uint32_t dn_tmp_189=((uint32_t)(6ULL));
uint32_t dn_tmp_190=(v_message)->f_id;
uint32_t dn_tmp_191=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_188,dn_tmp_189,dn_tmp_190,dn_tmp_191); });return dn_tmp_192; }
}
else{
}
{ uint32_t dn_tmp_194=(v_message)->f_id;uint32_t *dn_tmp_193=&(v_previous);*dn_tmp_193=dn_tmp_194; }
uint32_t v_prior=((uint32_t)(0ULL));
uint32_t v_first=(v_message)->f_first_minor;
if(({ uint32_t dn_tmp_195=v_first;
uint32_t dn_tmp_196=((v_spec)->f_version).f_minimum_minor;
(dn_tmp_195 < dn_tmp_196); })){
{ uint32_t dn_tmp_198=((v_spec)->f_version).f_minimum_minor;uint32_t *dn_tmp_197=&(v_first);*dn_tmp_197=dn_tmp_198; }
}
else{
}
{ size_t dn_tmp_201=((size_t)(0ULL));size_t dn_tmp_200=({ uint32_t dn_tmp_199=(v_message)->f_field_count;
if((unsigned __int128)dn_tmp_199>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_199; });for(size_t v_field_index=dn_tmp_201;v_field_index<dn_tmp_200;v_field_index++){
dn_r_MessageField const * v_field=(&((*({ dn_t_arr32_r12_MessageField *dn_tmp_202=&((v_message)->f_fields);size_t dn_tmp_203=v_field_index;&dn_tmp_202->data[dn_index(dn_tmp_203,32)]; }))));
{ uint32_t dn_tmp_207=({ uint32_t dn_tmp_204=(v_result).f_schema_nodes;
uint32_t dn_tmp_205=((uint32_t)(1ULL));
dn_U32_add(dn_tmp_204,dn_tmp_205); });uint32_t *dn_tmp_206=&((v_result).f_schema_nodes);*dn_tmp_206=dn_tmp_207; }
if((((((({ uint32_t dn_tmp_208=(v_field)->f_id;
uint32_t dn_tmp_209=v_prior;
(dn_tmp_208 <= dn_tmp_209); }) || ({ int32_t dn_tmp_210=({ dn_r_MessageField const * dn_tmp_211=v_field;
dn_f_network_field_check(dn_tmp_211); });
int32_t dn_tmp_212=((int32_t)(0ULL));
(dn_tmp_210 != dn_tmp_212); })) || ({ uint32_t dn_tmp_213=(v_field)->f_first_minor;
uint32_t dn_tmp_214=(v_message)->f_first_minor;
(dn_tmp_213 < dn_tmp_214); })) || ({ uint32_t dn_tmp_215=(v_field)->f_last_minor;
uint32_t dn_tmp_216=(v_message)->f_last_minor;
(dn_tmp_215 > dn_tmp_216); })) || ({ uint32_t dn_tmp_217=(v_field)->f_first_minor;
uint32_t dn_tmp_218=((v_spec)->f_version).f_minor;
(dn_tmp_217 > dn_tmp_218); })) || ({ uint32_t dn_tmp_219=(v_field)->f_last_minor;
uint32_t dn_tmp_220=((v_spec)->f_version).f_minimum_minor;
(dn_tmp_219 < dn_tmp_220); }))){
{ dn_r_NetworkSpecCheck dn_tmp_225=({ int32_t dn_tmp_221=((int32_t)(4ULL));
uint32_t dn_tmp_222=((uint32_t)(7ULL));
uint32_t dn_tmp_223=(v_message)->f_id;
uint32_t dn_tmp_224=(v_field)->f_id;
dn_f_network_spec_failure(dn_tmp_221,dn_tmp_222,dn_tmp_223,dn_tmp_224); });return dn_tmp_225; }
}
else{
}
{ uint32_t dn_tmp_227=(v_field)->f_id;uint32_t *dn_tmp_226=&(v_prior);*dn_tmp_226=dn_tmp_227; }
if(({ uint32_t dn_tmp_228=({ uint32_t dn_tmp_229=(v_field)->f_flags;
uint32_t dn_tmp_230=((uint32_t)(1ULL));
dn_U32_bits((uint64_t)dn_tmp_229 & (uint64_t)dn_tmp_230); });
uint32_t dn_tmp_231=((uint32_t)(0ULL));
(dn_tmp_228 != dn_tmp_231); })){
{ uint32_t dn_tmp_235=({ uint32_t dn_tmp_232=(v_result).f_required_properties;
uint32_t dn_tmp_233=((uint32_t)(4ULL));
dn_U32_bits((uint64_t)dn_tmp_232 | (uint64_t)dn_tmp_233); });uint32_t *dn_tmp_234=&((v_result).f_required_properties);*dn_tmp_234=dn_tmp_235; }
}
else{
}
}
 }
dn_r_SchemaMeasure v_base=({ dn_r_MessageSchema const * dn_tmp_236=v_message;
dn_r_WireFormat dn_tmp_237=(v_spec)->f_wire;
uint32_t dn_tmp_238=v_first;
dn_f_network_message_measure(dn_tmp_236,dn_tmp_237,dn_tmp_238); });
if(({ int32_t dn_tmp_239=(v_base).f_status;
int32_t dn_tmp_240=((int32_t)(0ULL));
(dn_tmp_239 != dn_tmp_240); })){
{ dn_r_NetworkSpecCheck dn_tmp_245=({ int32_t dn_tmp_241=(v_base).f_status;
uint32_t dn_tmp_242=((uint32_t)(7ULL));
uint32_t dn_tmp_243=(v_message)->f_id;
uint32_t dn_tmp_244=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_241,dn_tmp_242,dn_tmp_243,dn_tmp_244); });return dn_tmp_245; }
}
else{
}
if(({ uint64_t dn_tmp_246=(v_base).f_minimum;
uint64_t dn_tmp_247=v_minimum;
(dn_tmp_246 < dn_tmp_247); })){
{ uint64_t dn_tmp_249=(v_base).f_minimum;uint64_t *dn_tmp_248=&(v_minimum);*dn_tmp_248=dn_tmp_249; }
}
else{
}
if(({ uint64_t dn_tmp_250=(v_base).f_maximum;
uint64_t dn_tmp_251=v_maximum;
(dn_tmp_250 > dn_tmp_251); })){
{ uint64_t dn_tmp_253=(v_base).f_maximum;uint64_t *dn_tmp_252=&(v_maximum);*dn_tmp_252=dn_tmp_253; }
}
else{
}
{ size_t dn_tmp_256=((size_t)(0ULL));size_t dn_tmp_255=({ uint32_t dn_tmp_254=(v_message)->f_field_count;
if((unsigned __int128)dn_tmp_254>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_254; });for(size_t v_field_index=dn_tmp_256;v_field_index<dn_tmp_255;v_field_index++){
dn_r_MessageField const * v_field=(&((*({ dn_t_arr32_r12_MessageField *dn_tmp_257=&((v_message)->f_fields);size_t dn_tmp_258=v_field_index;&dn_tmp_257->data[dn_index(dn_tmp_258,32)]; }))));
uint32_t v_point=(v_field)->f_first_minor;
{ size_t dn_tmp_260=((size_t)(0ULL));size_t dn_tmp_259=((size_t)(2ULL));for(size_t v_boundary=dn_tmp_260;v_boundary<dn_tmp_259;v_boundary++){
if(({ size_t dn_tmp_261=v_boundary;
size_t dn_tmp_262=((size_t)(1ULL));
(dn_tmp_261 == dn_tmp_262); })){
if(({ uint32_t dn_tmp_263=(v_field)->f_last_minor;
uint32_t dn_tmp_264=({ uint32_t dn_tmp_265=((uint32_t)(4294967295ULL));
if((unsigned __int128)dn_tmp_265>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_265; });
(dn_tmp_263 == dn_tmp_264); })){
continue;
}
else{
}
{ uint32_t dn_tmp_269=({ uint32_t dn_tmp_266=(v_field)->f_last_minor;
uint32_t dn_tmp_267=((uint32_t)(1ULL));
dn_U32_add(dn_tmp_266,dn_tmp_267); });uint32_t *dn_tmp_268=&(v_point);*dn_tmp_268=dn_tmp_269; }
}
else{
}
if(((({ uint32_t dn_tmp_270=v_point;
uint32_t dn_tmp_271=v_first;
(dn_tmp_270 >= dn_tmp_271); }) && ({ uint32_t dn_tmp_272=v_point;
uint32_t dn_tmp_273=((v_spec)->f_version).f_minor;
(dn_tmp_272 <= dn_tmp_273); })) && ({ uint32_t dn_tmp_274=(v_message)->f_first_minor;
uint32_t dn_tmp_275=(v_message)->f_last_minor;
uint32_t dn_tmp_276=v_point;
dn_f_network_minor_active(dn_tmp_274,dn_tmp_275,dn_tmp_276); }))){
dn_r_SchemaMeasure v_measured=({ dn_r_MessageSchema const * dn_tmp_277=v_message;
dn_r_WireFormat dn_tmp_278=(v_spec)->f_wire;
uint32_t dn_tmp_279=v_point;
dn_f_network_message_measure(dn_tmp_277,dn_tmp_278,dn_tmp_279); });
if(({ uint64_t dn_tmp_280=(v_measured).f_minimum;
uint64_t dn_tmp_281=v_minimum;
(dn_tmp_280 < dn_tmp_281); })){
{ uint64_t dn_tmp_283=(v_measured).f_minimum;uint64_t *dn_tmp_282=&(v_minimum);*dn_tmp_282=dn_tmp_283; }
}
else{
}
if(({ uint64_t dn_tmp_284=(v_measured).f_maximum;
uint64_t dn_tmp_285=v_maximum;
(dn_tmp_284 > dn_tmp_285); })){
{ uint64_t dn_tmp_287=(v_measured).f_maximum;uint64_t *dn_tmp_286=&(v_maximum);*dn_tmp_286=dn_tmp_287; }
}
else{
}
}
else{
}
}
 }
}
 }
if((({ uint32_t dn_tmp_288=(v_message)->f_last_minor;
uint32_t dn_tmp_289=((v_spec)->f_version).f_minor;
(dn_tmp_288 < dn_tmp_289); }) && ({ uint32_t dn_tmp_290=(v_message)->f_last_minor;
uint32_t dn_tmp_291=((v_spec)->f_version).f_minimum_minor;
(dn_tmp_290 >= dn_tmp_291); }))){
if((!(({ dn_r_NetworkSpec const * dn_tmp_292=v_spec;
uint32_t dn_tmp_293=({ uint32_t dn_tmp_294=(v_message)->f_last_minor;
uint32_t dn_tmp_295=((uint32_t)(1ULL));
dn_U32_add(dn_tmp_294,dn_tmp_295); });
dn_f_network_has_message(dn_tmp_292,dn_tmp_293); })))){
{ dn_r_NetworkSpecCheck dn_tmp_300=({ int32_t dn_tmp_296=((int32_t)(4ULL));
uint32_t dn_tmp_297=((uint32_t)(1ULL));
uint32_t dn_tmp_298=(v_message)->f_id;
uint32_t dn_tmp_299=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_296,dn_tmp_297,dn_tmp_298,dn_tmp_299); });return dn_tmp_300; }
}
else{
}
}
else{
}
}
 }
if(((!(({ dn_r_NetworkSpec const * dn_tmp_301=v_spec;
uint32_t dn_tmp_302=((v_spec)->f_version).f_minimum_minor;
dn_f_network_has_message(dn_tmp_301,dn_tmp_302); }))) || (!(({ dn_r_NetworkSpec const * dn_tmp_303=v_spec;
uint32_t dn_tmp_304=((v_spec)->f_version).f_minor;
dn_f_network_has_message(dn_tmp_303,dn_tmp_304); }))))){
{ dn_r_NetworkSpecCheck dn_tmp_309=({ int32_t dn_tmp_305=((int32_t)(4ULL));
uint32_t dn_tmp_306=((uint32_t)(1ULL));
uint32_t dn_tmp_307=((uint32_t)(0ULL));
uint32_t dn_tmp_308=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_305,dn_tmp_306,dn_tmp_307,dn_tmp_308); });return dn_tmp_309; }
}
else{
}
if(({ uint32_t dn_tmp_310=(v_result).f_schema_nodes;
uint32_t dn_tmp_311=((v_spec)->f_resources).f_maximum_schema_nodes;
(dn_tmp_310 > dn_tmp_311); })){
{ dn_r_NetworkSpecCheck dn_tmp_316=({ int32_t dn_tmp_312=((int32_t)(7ULL));
uint32_t dn_tmp_313=((uint32_t)(3ULL));
uint32_t dn_tmp_314=((uint32_t)(0ULL));
uint32_t dn_tmp_315=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_312,dn_tmp_313,dn_tmp_314,dn_tmp_315); });return dn_tmp_316; }
}
else{
}
{ uint64_t dn_tmp_321=({ uint64_t dn_tmp_317=v_maximum;
uint64_t dn_tmp_318=({ uint32_t dn_tmp_319=((v_spec)->f_resources).f_maximum_unknown_bytes;
if((unsigned __int128)dn_tmp_319>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_319; });
dn_U64_add(dn_tmp_317,dn_tmp_318); });uint64_t *dn_tmp_320=&(v_maximum);*dn_tmp_320=dn_tmp_321; }
if(({ uint64_t dn_tmp_322=v_maximum;
uint64_t dn_tmp_323=({ uint32_t dn_tmp_324=(((v_spec)->f_resources).f_transport).f_max_message;
if((unsigned __int128)dn_tmp_324>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_324; });
(dn_tmp_322 > dn_tmp_323); })){
{ dn_r_NetworkSpecCheck dn_tmp_329=({ int32_t dn_tmp_325=((int32_t)(7ULL));
uint32_t dn_tmp_326=((uint32_t)(3ULL));
uint32_t dn_tmp_327=((uint32_t)(0ULL));
uint32_t dn_tmp_328=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_325,dn_tmp_326,dn_tmp_327,dn_tmp_328); });return dn_tmp_329; }
}
else{
}
if(({ uint32_t dn_tmp_330=({ uint32_t dn_tmp_331=(v_result).f_required_properties;
uint32_t dn_tmp_332=((v_spec)->f_transport).f_forbidden;
dn_U32_bits((uint64_t)dn_tmp_331 & (uint64_t)dn_tmp_332); });
uint32_t dn_tmp_333=((uint32_t)(0ULL));
(dn_tmp_330 != dn_tmp_333); })){
{ dn_r_NetworkSpecCheck dn_tmp_338=({ int32_t dn_tmp_334=((int32_t)(4ULL));
uint32_t dn_tmp_335=((uint32_t)(5ULL));
uint32_t dn_tmp_336=((uint32_t)(0ULL));
uint32_t dn_tmp_337=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_334,dn_tmp_335,dn_tmp_336,dn_tmp_337); });return dn_tmp_338; }
}
else{
}
if((({ uint32_t dn_tmp_339=({ uint32_t dn_tmp_340=(v_result).f_required_properties;
uint32_t dn_tmp_341=((uint32_t)(8ULL));
dn_U32_bits((uint64_t)dn_tmp_340 & (uint64_t)dn_tmp_341); });
uint32_t dn_tmp_342=((uint32_t)(0ULL));
(dn_tmp_339 != dn_tmp_342); }) && ({ uint32_t dn_tmp_343=(((v_spec)->f_resources).f_transport).f_channels;
uint32_t dn_tmp_344=((uint32_t)(2ULL));
(dn_tmp_343 < dn_tmp_344); }))){
{ dn_r_NetworkSpecCheck dn_tmp_349=({ int32_t dn_tmp_345=((int32_t)(7ULL));
uint32_t dn_tmp_346=((uint32_t)(3ULL));
uint32_t dn_tmp_347=((uint32_t)(0ULL));
uint32_t dn_tmp_348=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_345,dn_tmp_346,dn_tmp_347,dn_tmp_348); });return dn_tmp_349; }
}
else{
}
if(({ uint32_t dn_tmp_350=({ uint32_t dn_tmp_351=(v_result).f_required_properties;
uint32_t dn_tmp_352=((uint32_t)(4ULL));
dn_U32_bits((uint64_t)dn_tmp_351 & (uint64_t)dn_tmp_352); });
uint32_t dn_tmp_353=((uint32_t)(0ULL));
(dn_tmp_350 != dn_tmp_353); })){
{ uint32_t dn_tmp_357=({ uint32_t dn_tmp_354=(v_result).f_required_proofs;
uint32_t dn_tmp_355=((uint32_t)(1024ULL));
dn_U32_bits((uint64_t)dn_tmp_354 | (uint64_t)dn_tmp_355); });uint32_t *dn_tmp_356=&((v_result).f_required_proofs);*dn_tmp_356=dn_tmp_357; }
}
else{
}
{ size_t dn_tmp_360=({ uint64_t dn_tmp_358=v_minimum;
if((unsigned __int128)dn_tmp_358>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_358; });size_t *dn_tmp_359=&((v_result).f_minimum);*dn_tmp_359=dn_tmp_360; }
{ size_t dn_tmp_363=({ uint64_t dn_tmp_361=v_maximum;
if((unsigned __int128)dn_tmp_361>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_361; });size_t *dn_tmp_362=&((v_result).f_maximum);*dn_tmp_362=dn_tmp_363; }
{ uint32_t dn_tmp_365=(v_result).f_required_proofs;uint32_t *dn_tmp_364=&((v_result).f_pending_proofs);*dn_tmp_364=dn_tmp_365; }
{ dn_r_NetworkSpecCheck dn_tmp_366=v_result;return dn_tmp_366; }
}

dn_fail("missing native return");
}
static dn_r_NetworkSpecCheck dn_cb_network_spec_check(dn_r_NetworkSpec const * v_spec) {
 if(!dn_enter()){return (dn_r_NetworkSpecCheck){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkSpecCheck){0};}
 dn_r_NetworkSpecCheck dn_result=dn_f_network_spec_check(v_spec);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkAdmission dn_f_network_spec_select(dn_r_NetworkSpec const * v_spec,dn_t_arr6_r18_TransportCandidate v_candidates) {
{
dn_r_NetworkSpecCheck v_checked=({ dn_r_NetworkSpec const * dn_tmp_0=v_spec;
dn_f_network_spec_check(dn_tmp_0); });
dn_r_NetworkAdmission v_result=({ ((dn_r_NetworkAdmission){0}); });
{ int32_t dn_tmp_2=(v_checked).f_status;int32_t *dn_tmp_1=&((v_result).f_status);*dn_tmp_1=dn_tmp_2; }
{ int32_t dn_tmp_4=(v_checked).f_status;int32_t *dn_tmp_3=&(((v_result).f_choice).f_status);*dn_tmp_3=dn_tmp_4; }
if(({ int32_t dn_tmp_5=(v_checked).f_status;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_NetworkAdmission dn_tmp_7=v_result;return dn_tmp_7; }
}
else{
}
{ dn_r_TransportRequirements dn_tmp_12=({ uint32_t dn_tmp_8=(v_checked).f_required_properties;
uint32_t dn_tmp_9=((v_spec)->f_transport).f_forbidden;
uint32_t dn_tmp_10=((v_spec)->f_transport).f_preferred;
((dn_r_TransportRequirements){dn_tmp_8,dn_tmp_9,dn_tmp_10}); });dn_r_TransportRequirements *dn_tmp_11=&((v_result).f_requirements);*dn_tmp_11=dn_tmp_12; }
{ dn_r_TransportBounds dn_tmp_14=((v_spec)->f_resources).f_transport;dn_r_TransportBounds *dn_tmp_13=&((v_result).f_bounds);*dn_tmp_13=dn_tmp_14; }
{ uint32_t dn_tmp_17=({ size_t dn_tmp_15=(v_checked).f_maximum;
if((unsigned __int128)dn_tmp_15>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_15; });uint32_t *dn_tmp_16=&(((v_result).f_bounds).f_max_message);*dn_tmp_16=dn_tmp_17; }
{ dn_r_SessionContract dn_tmp_19=(v_spec)->f_session;dn_r_SessionContract *dn_tmp_18=&((v_result).f_session);*dn_tmp_18=dn_tmp_19; }
{ uint64_t dn_tmp_21=((v_spec)->f_resources).f_maximum_session_messages;uint64_t *dn_tmp_20=&((v_result).f_maximum_session_messages);*dn_tmp_20=dn_tmp_21; }
{ uint32_t dn_tmp_23=(v_checked).f_pending_proofs;uint32_t *dn_tmp_22=&((v_result).f_pending_proofs);*dn_tmp_22=dn_tmp_23; }
{ uint32_t dn_tmp_25=(v_checked).f_unsupported_security;uint32_t *dn_tmp_24=&((v_result).f_unsupported_security);*dn_tmp_24=dn_tmp_25; }
if((({ uint32_t dn_tmp_26=(v_result).f_unsupported_security;
uint32_t dn_tmp_27=((uint32_t)(0ULL));
(dn_tmp_26 != dn_tmp_27); }) || ({ uint32_t dn_tmp_28=((v_spec)->f_proofs).f_assurance;
uint32_t dn_tmp_29=((uint32_t)(2ULL));
(dn_tmp_28 == dn_tmp_29); }))){
{ int32_t dn_tmp_31=((int32_t)(6ULL));int32_t *dn_tmp_30=&((v_result).f_status);*dn_tmp_30=dn_tmp_31; }
{ int32_t dn_tmp_33=((int32_t)(6ULL));int32_t *dn_tmp_32=&(((v_result).f_choice).f_status);*dn_tmp_32=dn_tmp_33; }
{ dn_r_NetworkAdmission dn_tmp_34=v_result;return dn_tmp_34; }
}
else{
}
{ dn_r_TransportChoice dn_tmp_39=({ dn_r_TransportRequirements dn_tmp_35=(v_result).f_requirements;
dn_r_TransportBounds dn_tmp_36=(v_result).f_bounds;
dn_t_arr6_r18_TransportCandidate dn_tmp_37=v_candidates;
dn_f_transport_select(dn_tmp_35,dn_tmp_36,dn_tmp_37); });dn_r_TransportChoice *dn_tmp_38=&((v_result).f_choice);*dn_tmp_38=dn_tmp_39; }
{ int32_t dn_tmp_41=((v_result).f_choice).f_status;int32_t *dn_tmp_40=&((v_result).f_status);*dn_tmp_40=dn_tmp_41; }
{ dn_r_NetworkAdmission dn_tmp_42=v_result;return dn_tmp_42; }
}

dn_fail("missing native return");
}
static dn_r_NetworkAdmission dn_cb_network_spec_select(dn_r_NetworkSpec const * v_spec,dn_t_arr6_r18_TransportCandidate v_candidates) {
 if(!dn_enter()){return (dn_r_NetworkAdmission){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkAdmission){0};}
 dn_r_NetworkAdmission dn_result=dn_f_network_spec_select(v_spec,v_candidates);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_utf8 dn_f_network_obligation_name(uint32_t v_bit) {
{
if(({ uint32_t dn_tmp_0=v_bit;
uint32_t dn_tmp_1=((uint32_t)(1ULL));
(dn_tmp_0 == dn_tmp_1); })){
{ dn_utf8 dn_tmp_2=((dn_utf8){(const uint8_t*)"codec_roundtrip", 15});return dn_tmp_2; }
}
else{
}
if(({ uint32_t dn_tmp_3=v_bit;
uint32_t dn_tmp_4=((uint32_t)(2ULL));
(dn_tmp_3 == dn_tmp_4); })){
{ dn_utf8 dn_tmp_5=((dn_utf8){(const uint8_t*)"wire_bounds", 11});return dn_tmp_5; }
}
else{
}
if(({ uint32_t dn_tmp_6=v_bit;
uint32_t dn_tmp_7=((uint32_t)(4ULL));
(dn_tmp_6 == dn_tmp_7); })){
{ dn_utf8 dn_tmp_8=((dn_utf8){(const uint8_t*)"machine_overflow", 16});return dn_tmp_8; }
}
else{
}
if(({ uint32_t dn_tmp_9=v_bit;
uint32_t dn_tmp_10=((uint32_t)(8ULL));
(dn_tmp_9 == dn_tmp_10); })){
{ dn_utf8 dn_tmp_11=((dn_utf8){(const uint8_t*)"deterministic_encoding", 22});return dn_tmp_11; }
}
else{
}
if(({ uint32_t dn_tmp_12=v_bit;
uint32_t dn_tmp_13=((uint32_t)(16ULL));
(dn_tmp_12 == dn_tmp_13); })){
{ dn_utf8 dn_tmp_14=((dn_utf8){(const uint8_t*)"unknown_field_preservation", 26});return dn_tmp_14; }
}
else{
}
if(({ uint32_t dn_tmp_15=v_bit;
uint32_t dn_tmp_16=((uint32_t)(32ULL));
(dn_tmp_15 == dn_tmp_16); })){
{ dn_utf8 dn_tmp_17=((dn_utf8){(const uint8_t*)"session_transitions", 19});return dn_tmp_17; }
}
else{
}
if(({ uint32_t dn_tmp_18=v_bit;
uint32_t dn_tmp_19=((uint32_t)(64ULL));
(dn_tmp_18 == dn_tmp_19); })){
{ dn_utf8 dn_tmp_20=((dn_utf8){(const uint8_t*)"authentication_before_data", 26});return dn_tmp_20; }
}
else{
}
if(({ uint32_t dn_tmp_21=v_bit;
uint32_t dn_tmp_22=((uint32_t)(128ULL));
(dn_tmp_21 == dn_tmp_22); })){
{ dn_utf8 dn_tmp_23=((dn_utf8){(const uint8_t*)"resource_cleanup", 16});return dn_tmp_23; }
}
else{
}
if(({ uint32_t dn_tmp_24=v_bit;
uint32_t dn_tmp_25=((uint32_t)(256ULL));
(dn_tmp_24 == dn_tmp_25); })){
{ dn_utf8 dn_tmp_26=((dn_utf8){(const uint8_t*)"termination", 11});return dn_tmp_26; }
}
else{
}
if(({ uint32_t dn_tmp_27=v_bit;
uint32_t dn_tmp_28=((uint32_t)(512ULL));
(dn_tmp_27 == dn_tmp_28); })){
{ dn_utf8 dn_tmp_29=((dn_utf8){(const uint8_t*)"application_invariants", 22});return dn_tmp_29; }
}
else{
}
if(({ uint32_t dn_tmp_30=v_bit;
uint32_t dn_tmp_31=((uint32_t)(1024ULL));
(dn_tmp_30 == dn_tmp_31); })){
{ dn_utf8 dn_tmp_32=((dn_utf8){(const uint8_t*)"security_assumptions", 20});return dn_tmp_32; }
}
else{
}
{ dn_utf8 dn_tmp_33=((dn_utf8){(const uint8_t*)"unknown_obligation", 18});return dn_tmp_33; }
}

dn_fail("missing native return");
}
static dn_utf8 dn_cb_network_obligation_name(uint32_t v_bit) {
 if(!dn_enter()){return (dn_utf8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_utf8){0};}
 dn_utf8 dn_result=dn_f_network_obligation_name(v_bit);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ProtocolValue dn_f_protocol_read_be(dn_t_slice0_U8 v_bytes,size_t v_offset,size_t v_width) {
{
if(((({ size_t dn_tmp_0=v_width;
size_t dn_tmp_1=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); }) || ({ size_t dn_tmp_2=v_width;
size_t dn_tmp_3=((size_t)(8ULL));
(dn_tmp_2 > dn_tmp_3); })) || ({ size_t dn_tmp_4=v_offset;
size_t dn_tmp_5=({ dn_t_slice0_U8 dn_tmp_6=v_bytes;
(dn_tmp_6).len; });
(dn_tmp_4 > dn_tmp_5); }))){
{ dn_r_ProtocolValue dn_tmp_10=({ uint64_t dn_tmp_7=((uint64_t)(0ULL));
size_t dn_tmp_8=v_offset;
int32_t dn_tmp_9=((int32_t)(4ULL));
((dn_r_ProtocolValue){dn_tmp_7,dn_tmp_8,dn_tmp_9}); });return dn_tmp_10; }
}
else{
}
if(({ size_t dn_tmp_11=v_width;
size_t dn_tmp_12=({ size_t dn_tmp_13=({ dn_t_slice0_U8 dn_tmp_14=v_bytes;
(dn_tmp_14).len; });
size_t dn_tmp_15=v_offset;
dn_USize_sub(dn_tmp_13,dn_tmp_15); });
(dn_tmp_11 > dn_tmp_12); })){
{ dn_r_ProtocolValue dn_tmp_19=({ uint64_t dn_tmp_16=((uint64_t)(0ULL));
size_t dn_tmp_17=v_offset;
int32_t dn_tmp_18=((int32_t)(1ULL));
((dn_r_ProtocolValue){dn_tmp_16,dn_tmp_17,dn_tmp_18}); });return dn_tmp_19; }
}
else{
}
uint64_t v_value=((uint64_t)(0ULL));
{ size_t dn_tmp_21=((size_t)(0ULL));size_t dn_tmp_20=v_width;for(size_t v_index=dn_tmp_21;v_index<dn_tmp_20;v_index++){
{ uint64_t dn_tmp_33=({ uint64_t dn_tmp_22=({ uint64_t dn_tmp_23=v_value;
uint64_t dn_tmp_24=({ uint64_t dn_tmp_25=((uint64_t)(8ULL));
if((unsigned __int128)dn_tmp_25>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_25; });
dn_U64_shl(dn_tmp_23,dn_tmp_24); });
uint64_t dn_tmp_26=({ uint8_t dn_tmp_27=(*({ dn_t_slice0_U8 *dn_tmp_28=&(v_bytes);size_t dn_tmp_29=({ size_t dn_tmp_30=v_offset;
size_t dn_tmp_31=v_index;
dn_USize_add(dn_tmp_30,dn_tmp_31); });&dn_tmp_28->data[dn_index(dn_tmp_29,dn_tmp_28->len)]; }));
if((unsigned __int128)dn_tmp_27>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_27; });
dn_U64_bits((uint64_t)dn_tmp_22 | (uint64_t)dn_tmp_26); });uint64_t *dn_tmp_32=&(v_value);*dn_tmp_32=dn_tmp_33; }
}
 }
{ dn_r_ProtocolValue dn_tmp_39=({ uint64_t dn_tmp_34=v_value;
size_t dn_tmp_35=({ size_t dn_tmp_36=v_offset;
size_t dn_tmp_37=v_width;
dn_USize_add(dn_tmp_36,dn_tmp_37); });
int32_t dn_tmp_38=((int32_t)(0ULL));
((dn_r_ProtocolValue){dn_tmp_34,dn_tmp_35,dn_tmp_38}); });return dn_tmp_39; }
}

dn_fail("missing native return");
}
static dn_r_ProtocolValue dn_cb_protocol_read_be(dn_t_slice0_U8 v_bytes,size_t v_offset,size_t v_width) {
 if(!dn_enter()){return (dn_r_ProtocolValue){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ProtocolValue){0};}
 dn_r_ProtocolValue dn_result=dn_f_protocol_read_be(v_bytes,v_offset,v_width);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static int32_t dn_f_protocol_write_be(dn_t_buf_U8 * v_bytes,size_t v_offset,size_t v_width,uint64_t v_value) {
{
if(((({ size_t dn_tmp_0=v_width;
size_t dn_tmp_1=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); }) || ({ size_t dn_tmp_2=v_width;
size_t dn_tmp_3=((size_t)(8ULL));
(dn_tmp_2 > dn_tmp_3); })) || ({ size_t dn_tmp_4=v_offset;
size_t dn_tmp_5=({ dn_t_buf_U8 * dn_tmp_6=v_bytes;
(*(dn_tmp_6)).len; });
(dn_tmp_4 > dn_tmp_5); }))){
{ int32_t dn_tmp_7=((int32_t)(4ULL));return dn_tmp_7; }
}
else{
}
if(({ size_t dn_tmp_8=v_width;
size_t dn_tmp_9=({ size_t dn_tmp_10=({ dn_t_buf_U8 * dn_tmp_11=v_bytes;
(*(dn_tmp_11)).len; });
size_t dn_tmp_12=v_offset;
dn_USize_sub(dn_tmp_10,dn_tmp_12); });
(dn_tmp_8 > dn_tmp_9); })){
{ int32_t dn_tmp_13=((int32_t)(1ULL));return dn_tmp_13; }
}
else{
}
if(({ size_t dn_tmp_14=v_width;
size_t dn_tmp_15=((size_t)(8ULL));
(dn_tmp_14 < dn_tmp_15); })){
if(({ uint64_t dn_tmp_16=({ uint64_t dn_tmp_17=v_value;
uint64_t dn_tmp_18=({ size_t dn_tmp_19=({ size_t dn_tmp_20=v_width;
size_t dn_tmp_21=((size_t)(8ULL));
dn_USize_mul(dn_tmp_20,dn_tmp_21); });
if((unsigned __int128)dn_tmp_19>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_19; });
dn_U64_shr(dn_tmp_17,dn_tmp_18); });
uint64_t dn_tmp_22=({ uint64_t dn_tmp_23=((uint64_t)(0ULL));
if((unsigned __int128)dn_tmp_23>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_23; });
(dn_tmp_16 != dn_tmp_22); })){
{ int32_t dn_tmp_24=((int32_t)(7ULL));return dn_tmp_24; }
}
else{
}
}
else{
}
{ size_t dn_tmp_26=((size_t)(0ULL));size_t dn_tmp_25=v_width;for(size_t v_index=dn_tmp_26;v_index<dn_tmp_25;v_index++){
{ uint8_t dn_tmp_45=({ uint64_t dn_tmp_31=({ uint64_t dn_tmp_32=({ uint64_t dn_tmp_33=v_value;
uint64_t dn_tmp_34=({ size_t dn_tmp_35=({ size_t dn_tmp_36=({ size_t dn_tmp_37=({ size_t dn_tmp_38=v_width;
size_t dn_tmp_39=v_index;
dn_USize_sub(dn_tmp_38,dn_tmp_39); });
size_t dn_tmp_40=((size_t)(1ULL));
dn_USize_sub(dn_tmp_37,dn_tmp_40); });
size_t dn_tmp_41=((size_t)(8ULL));
dn_USize_mul(dn_tmp_36,dn_tmp_41); });
if((unsigned __int128)dn_tmp_35>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_35; });
dn_U64_shr(dn_tmp_33,dn_tmp_34); });
uint64_t dn_tmp_42=({ uint64_t dn_tmp_43=((uint64_t)(255ULL));
if((unsigned __int128)dn_tmp_43>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_43; });
dn_U64_bits((uint64_t)dn_tmp_32 & (uint64_t)dn_tmp_42); });
if((unsigned __int128)dn_tmp_31>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_31; });uint8_t *dn_tmp_44=&((*({ dn_t_buf_U8 * dn_tmp_27=v_bytes;size_t dn_tmp_28=({ size_t dn_tmp_29=v_offset;
size_t dn_tmp_30=v_index;
dn_USize_add(dn_tmp_29,dn_tmp_30); });&dn_tmp_27->data[dn_index(dn_tmp_28,dn_tmp_27->len)]; })));*dn_tmp_44=dn_tmp_45; }
}
 }
{ int32_t dn_tmp_46=((int32_t)(0ULL));return dn_tmp_46; }
}

dn_fail("missing native return");
}
static int32_t dn_cb_protocol_write_be(dn_t_buf_U8 * v_bytes,size_t v_offset,size_t v_width,uint64_t v_value) {
 if(!dn_enter()){return (int32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (int32_t){0};}
 int32_t dn_result=dn_f_protocol_write_be(v_bytes,v_offset,v_width,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ProtocolValue dn_f_quic_varint_read(dn_t_slice0_U8 v_bytes,size_t v_offset) {
{
if(({ size_t dn_tmp_0=v_offset;
size_t dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_bytes;
(dn_tmp_2).len; });
(dn_tmp_0 > dn_tmp_1); })){
{ dn_r_ProtocolValue dn_tmp_6=({ uint64_t dn_tmp_3=((uint64_t)(0ULL));
size_t dn_tmp_4=v_offset;
int32_t dn_tmp_5=((int32_t)(4ULL));
((dn_r_ProtocolValue){dn_tmp_3,dn_tmp_4,dn_tmp_5}); });return dn_tmp_6; }
}
else{
}
if(({ size_t dn_tmp_7=v_offset;
size_t dn_tmp_8=({ dn_t_slice0_U8 dn_tmp_9=v_bytes;
(dn_tmp_9).len; });
(dn_tmp_7 == dn_tmp_8); })){
{ dn_r_ProtocolValue dn_tmp_13=({ uint64_t dn_tmp_10=((uint64_t)(0ULL));
size_t dn_tmp_11=v_offset;
int32_t dn_tmp_12=((int32_t)(1ULL));
((dn_r_ProtocolValue){dn_tmp_10,dn_tmp_11,dn_tmp_12}); });return dn_tmp_13; }
}
else{
}
size_t v_width=({ size_t dn_tmp_14=({ size_t dn_tmp_15=((size_t)(1ULL));
if((unsigned __int128)dn_tmp_15>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_15; });
size_t dn_tmp_16=({ uint8_t dn_tmp_17=({ uint8_t dn_tmp_18=(*({ dn_t_slice0_U8 *dn_tmp_19=&(v_bytes);size_t dn_tmp_20=v_offset;&dn_tmp_19->data[dn_index(dn_tmp_20,dn_tmp_19->len)]; }));
uint8_t dn_tmp_21=({ uint8_t dn_tmp_22=((uint8_t)(6ULL));
if((unsigned __int128)dn_tmp_22>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_22; });
dn_U8_shr(dn_tmp_18,dn_tmp_21); });
if((unsigned __int128)dn_tmp_17>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_17; });
dn_USize_shl(dn_tmp_14,dn_tmp_16); });
if(({ size_t dn_tmp_23=v_width;
size_t dn_tmp_24=({ size_t dn_tmp_25=({ dn_t_slice0_U8 dn_tmp_26=v_bytes;
(dn_tmp_26).len; });
size_t dn_tmp_27=v_offset;
dn_USize_sub(dn_tmp_25,dn_tmp_27); });
(dn_tmp_23 > dn_tmp_24); })){
{ dn_r_ProtocolValue dn_tmp_31=({ uint64_t dn_tmp_28=((uint64_t)(0ULL));
size_t dn_tmp_29=v_offset;
int32_t dn_tmp_30=((int32_t)(1ULL));
((dn_r_ProtocolValue){dn_tmp_28,dn_tmp_29,dn_tmp_30}); });return dn_tmp_31; }
}
else{
}
uint64_t v_value=({ uint8_t dn_tmp_32=({ uint8_t dn_tmp_33=(*({ dn_t_slice0_U8 *dn_tmp_34=&(v_bytes);size_t dn_tmp_35=v_offset;&dn_tmp_34->data[dn_index(dn_tmp_35,dn_tmp_34->len)]; }));
uint8_t dn_tmp_36=({ uint8_t dn_tmp_37=((uint8_t)(63ULL));
if((unsigned __int128)dn_tmp_37>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_37; });
dn_U8_bits((uint64_t)dn_tmp_33 & (uint64_t)dn_tmp_36); });
if((unsigned __int128)dn_tmp_32>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_32; });
{ size_t dn_tmp_39=((size_t)(1ULL));size_t dn_tmp_38=v_width;for(size_t v_index=dn_tmp_39;v_index<dn_tmp_38;v_index++){
{ uint64_t dn_tmp_51=({ uint64_t dn_tmp_40=({ uint64_t dn_tmp_41=v_value;
uint64_t dn_tmp_42=({ uint64_t dn_tmp_43=((uint64_t)(8ULL));
if((unsigned __int128)dn_tmp_43>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_43; });
dn_U64_shl(dn_tmp_41,dn_tmp_42); });
uint64_t dn_tmp_44=({ uint8_t dn_tmp_45=(*({ dn_t_slice0_U8 *dn_tmp_46=&(v_bytes);size_t dn_tmp_47=({ size_t dn_tmp_48=v_offset;
size_t dn_tmp_49=v_index;
dn_USize_add(dn_tmp_48,dn_tmp_49); });&dn_tmp_46->data[dn_index(dn_tmp_47,dn_tmp_46->len)]; }));
if((unsigned __int128)dn_tmp_45>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_45; });
dn_U64_bits((uint64_t)dn_tmp_40 | (uint64_t)dn_tmp_44); });uint64_t *dn_tmp_50=&(v_value);*dn_tmp_50=dn_tmp_51; }
}
 }
{ dn_r_ProtocolValue dn_tmp_57=({ uint64_t dn_tmp_52=v_value;
size_t dn_tmp_53=({ size_t dn_tmp_54=v_offset;
size_t dn_tmp_55=v_width;
dn_USize_add(dn_tmp_54,dn_tmp_55); });
int32_t dn_tmp_56=((int32_t)(0ULL));
((dn_r_ProtocolValue){dn_tmp_52,dn_tmp_53,dn_tmp_56}); });return dn_tmp_57; }
}

dn_fail("missing native return");
}
static dn_r_ProtocolValue dn_cb_quic_varint_read(dn_t_slice0_U8 v_bytes,size_t v_offset) {
 if(!dn_enter()){return (dn_r_ProtocolValue){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ProtocolValue){0};}
 dn_r_ProtocolValue dn_result=dn_f_quic_varint_read(v_bytes,v_offset);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static size_t dn_f_quic_varint_size(uint64_t v_value) {
{
if(({ uint64_t dn_tmp_0=v_value;
uint64_t dn_tmp_1=({ uint64_t dn_tmp_2=((uint64_t)(64ULL));
if((unsigned __int128)dn_tmp_2>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_2; });
(dn_tmp_0 < dn_tmp_1); })){
{ size_t dn_tmp_3=((size_t)(1ULL));return dn_tmp_3; }
}
else{
}
if(({ uint64_t dn_tmp_4=v_value;
uint64_t dn_tmp_5=({ uint64_t dn_tmp_6=((uint64_t)(16384ULL));
if((unsigned __int128)dn_tmp_6>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_6; });
(dn_tmp_4 < dn_tmp_5); })){
{ size_t dn_tmp_7=((size_t)(2ULL));return dn_tmp_7; }
}
else{
}
if(({ uint64_t dn_tmp_8=v_value;
uint64_t dn_tmp_9=({ uint64_t dn_tmp_10=((uint64_t)(1073741824ULL));
if((unsigned __int128)dn_tmp_10>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_10; });
(dn_tmp_8 < dn_tmp_9); })){
{ size_t dn_tmp_11=((size_t)(4ULL));return dn_tmp_11; }
}
else{
}
if(({ uint64_t dn_tmp_12=v_value;
uint64_t dn_tmp_13=({ uint64_t dn_tmp_14=((uint64_t)(4611686018427387904ULL));
if((unsigned __int128)dn_tmp_14>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_14; });
(dn_tmp_12 < dn_tmp_13); })){
{ size_t dn_tmp_15=((size_t)(8ULL));return dn_tmp_15; }
}
else{
}
{ size_t dn_tmp_16=((size_t)(0ULL));return dn_tmp_16; }
}

dn_fail("missing native return");
}
static size_t dn_cb_quic_varint_size(uint64_t v_value) {
 if(!dn_enter()){return (size_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (size_t){0};}
 size_t dn_result=dn_f_quic_varint_size(v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ProtocolValue dn_f_quic_varint_write(dn_t_buf_U8 * v_bytes,size_t v_offset,uint64_t v_value) {
{
size_t v_width=({ uint64_t dn_tmp_0=v_value;
dn_f_quic_varint_size(dn_tmp_0); });
if(({ size_t dn_tmp_1=v_width;
size_t dn_tmp_2=((size_t)(0ULL));
(dn_tmp_1 == dn_tmp_2); })){
{ dn_r_ProtocolValue dn_tmp_6=({ uint64_t dn_tmp_3=((uint64_t)(0ULL));
size_t dn_tmp_4=v_offset;
int32_t dn_tmp_5=((int32_t)(7ULL));
((dn_r_ProtocolValue){dn_tmp_3,dn_tmp_4,dn_tmp_5}); });return dn_tmp_6; }
}
else{
}
if(({ size_t dn_tmp_7=v_offset;
size_t dn_tmp_8=({ dn_t_buf_U8 * dn_tmp_9=v_bytes;
(*(dn_tmp_9)).len; });
(dn_tmp_7 > dn_tmp_8); })){
{ dn_r_ProtocolValue dn_tmp_13=({ uint64_t dn_tmp_10=((uint64_t)(0ULL));
size_t dn_tmp_11=v_offset;
int32_t dn_tmp_12=((int32_t)(4ULL));
((dn_r_ProtocolValue){dn_tmp_10,dn_tmp_11,dn_tmp_12}); });return dn_tmp_13; }
}
else{
}
if(({ size_t dn_tmp_14=v_width;
size_t dn_tmp_15=({ size_t dn_tmp_16=({ dn_t_buf_U8 * dn_tmp_17=v_bytes;
(*(dn_tmp_17)).len; });
size_t dn_tmp_18=v_offset;
dn_USize_sub(dn_tmp_16,dn_tmp_18); });
(dn_tmp_14 > dn_tmp_15); })){
{ dn_r_ProtocolValue dn_tmp_22=({ uint64_t dn_tmp_19=((uint64_t)(0ULL));
size_t dn_tmp_20=v_offset;
int32_t dn_tmp_21=((int32_t)(1ULL));
((dn_r_ProtocolValue){dn_tmp_19,dn_tmp_20,dn_tmp_21}); });return dn_tmp_22; }
}
else{
}
int32_t v_status=({ dn_t_buf_U8 * dn_tmp_23=v_bytes;
size_t dn_tmp_24=v_offset;
size_t dn_tmp_25=v_width;
uint64_t dn_tmp_26=v_value;
dn_f_protocol_write_be(dn_tmp_23,dn_tmp_24,dn_tmp_25,dn_tmp_26); });
if(({ int32_t dn_tmp_27=v_status;
int32_t dn_tmp_28=({ int32_t dn_tmp_29=((int32_t)(0ULL));
if((__int128)dn_tmp_29<(-((__int128)1<<31))||(__int128)dn_tmp_29>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_29; });
(dn_tmp_27 != dn_tmp_28); })){
{ dn_r_ProtocolValue dn_tmp_33=({ uint64_t dn_tmp_30=((uint64_t)(0ULL));
size_t dn_tmp_31=v_offset;
int32_t dn_tmp_32=v_status;
((dn_r_ProtocolValue){dn_tmp_30,dn_tmp_31,dn_tmp_32}); });return dn_tmp_33; }
}
else{
}
uint8_t v_tag=((uint8_t)(0ULL));
if(({ size_t dn_tmp_34=v_width;
size_t dn_tmp_35=((size_t)(2ULL));
(dn_tmp_34 == dn_tmp_35); })){
{ uint8_t dn_tmp_37=((uint8_t)(64ULL));uint8_t *dn_tmp_36=&(v_tag);*dn_tmp_36=dn_tmp_37; }
}
else{
}
if(({ size_t dn_tmp_38=v_width;
size_t dn_tmp_39=((size_t)(4ULL));
(dn_tmp_38 == dn_tmp_39); })){
{ uint8_t dn_tmp_41=((uint8_t)(128ULL));uint8_t *dn_tmp_40=&(v_tag);*dn_tmp_40=dn_tmp_41; }
}
else{
}
if(({ size_t dn_tmp_42=v_width;
size_t dn_tmp_43=((size_t)(8ULL));
(dn_tmp_42 == dn_tmp_43); })){
{ uint8_t dn_tmp_45=((uint8_t)(192ULL));uint8_t *dn_tmp_44=&(v_tag);*dn_tmp_44=dn_tmp_45; }
}
else{
}
{ uint8_t dn_tmp_53=({ uint8_t dn_tmp_48=(*({ dn_t_buf_U8 * dn_tmp_49=v_bytes;size_t dn_tmp_50=v_offset;&dn_tmp_49->data[dn_index(dn_tmp_50,dn_tmp_49->len)]; }));
uint8_t dn_tmp_51=v_tag;
dn_U8_bits((uint64_t)dn_tmp_48 | (uint64_t)dn_tmp_51); });uint8_t *dn_tmp_52=&((*({ dn_t_buf_U8 * dn_tmp_46=v_bytes;size_t dn_tmp_47=v_offset;&dn_tmp_46->data[dn_index(dn_tmp_47,dn_tmp_46->len)]; })));*dn_tmp_52=dn_tmp_53; }
{ dn_r_ProtocolValue dn_tmp_59=({ uint64_t dn_tmp_54=v_value;
size_t dn_tmp_55=({ size_t dn_tmp_56=v_offset;
size_t dn_tmp_57=v_width;
dn_USize_add(dn_tmp_56,dn_tmp_57); });
int32_t dn_tmp_58=((int32_t)(0ULL));
((dn_r_ProtocolValue){dn_tmp_54,dn_tmp_55,dn_tmp_58}); });return dn_tmp_59; }
}

dn_fail("missing native return");
}
static dn_r_ProtocolValue dn_cb_quic_varint_write(dn_t_buf_U8 * v_bytes,size_t v_offset,uint64_t v_value) {
 if(!dn_enter()){return (dn_r_ProtocolValue){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ProtocolValue){0};}
 dn_r_ProtocolValue dn_result=dn_f_quic_varint_write(v_bytes,v_offset,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ProtocolFrame dn_f_protocol_frame32(dn_t_slice0_U8 v_bytes,size_t v_offset,size_t v_maximum) {
{
dn_r_ProtocolValue v_header=({ dn_t_slice0_U8 dn_tmp_0=v_bytes;
size_t dn_tmp_1=v_offset;
size_t dn_tmp_2=((size_t)(4ULL));
dn_f_protocol_read_be(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
if(({ int32_t dn_tmp_3=(v_header).f_status;
int32_t dn_tmp_4=({ int32_t dn_tmp_5=((int32_t)(0ULL));
if((__int128)dn_tmp_5<(-((__int128)1<<31))||(__int128)dn_tmp_5>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_5; });
(dn_tmp_3 != dn_tmp_4); })){
{ dn_r_ProtocolFrame dn_tmp_10=({ size_t dn_tmp_6=v_offset;
size_t dn_tmp_7=((size_t)(0ULL));
size_t dn_tmp_8=v_offset;
int32_t dn_tmp_9=(v_header).f_status;
((dn_r_ProtocolFrame){dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9}); });return dn_tmp_10; }
}
else{
}
size_t v_count=({ uint64_t dn_tmp_11=(v_header).f_value;
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_11; });
if(({ size_t dn_tmp_12=v_count;
size_t dn_tmp_13=v_maximum;
(dn_tmp_12 > dn_tmp_13); })){
{ dn_r_ProtocolFrame dn_tmp_18=({ size_t dn_tmp_14=(v_header).f_next;
size_t dn_tmp_15=v_count;
size_t dn_tmp_16=v_offset;
int32_t dn_tmp_17=((int32_t)(7ULL));
((dn_r_ProtocolFrame){dn_tmp_14,dn_tmp_15,dn_tmp_16,dn_tmp_17}); });return dn_tmp_18; }
}
else{
}
if(({ size_t dn_tmp_19=v_count;
size_t dn_tmp_20=({ size_t dn_tmp_21=({ dn_t_slice0_U8 dn_tmp_22=v_bytes;
(dn_tmp_22).len; });
size_t dn_tmp_23=(v_header).f_next;
dn_USize_sub(dn_tmp_21,dn_tmp_23); });
(dn_tmp_19 > dn_tmp_20); })){
{ dn_r_ProtocolFrame dn_tmp_28=({ size_t dn_tmp_24=(v_header).f_next;
size_t dn_tmp_25=v_count;
size_t dn_tmp_26=v_offset;
int32_t dn_tmp_27=((int32_t)(1ULL));
((dn_r_ProtocolFrame){dn_tmp_24,dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}
else{
}
{ dn_r_ProtocolFrame dn_tmp_35=({ size_t dn_tmp_29=(v_header).f_next;
size_t dn_tmp_30=v_count;
size_t dn_tmp_31=({ size_t dn_tmp_32=(v_header).f_next;
size_t dn_tmp_33=v_count;
dn_USize_add(dn_tmp_32,dn_tmp_33); });
int32_t dn_tmp_34=((int32_t)(0ULL));
((dn_r_ProtocolFrame){dn_tmp_29,dn_tmp_30,dn_tmp_31,dn_tmp_34}); });return dn_tmp_35; }
}

dn_fail("missing native return");
}
static dn_r_ProtocolFrame dn_cb_protocol_frame32(dn_t_slice0_U8 v_bytes,size_t v_offset,size_t v_maximum) {
 if(!dn_enter()){return (dn_r_ProtocolFrame){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ProtocolFrame){0};}
 dn_r_ProtocolFrame dn_result=dn_f_protocol_frame32(v_bytes,v_offset,v_maximum);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ProtocolValue dn_f_net_read_le(dn_t_slice0_U8 v_bytes,size_t v_offset,size_t v_width) {
{
if(((({ size_t dn_tmp_0=v_width;
size_t dn_tmp_1=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); }) || ({ size_t dn_tmp_2=v_width;
size_t dn_tmp_3=((size_t)(8ULL));
(dn_tmp_2 > dn_tmp_3); })) || ({ size_t dn_tmp_4=v_offset;
size_t dn_tmp_5=({ dn_t_slice0_U8 dn_tmp_6=v_bytes;
(dn_tmp_6).len; });
(dn_tmp_4 > dn_tmp_5); }))){
{ dn_r_ProtocolValue dn_tmp_10=({ uint64_t dn_tmp_7=((uint64_t)(0ULL));
size_t dn_tmp_8=v_offset;
int32_t dn_tmp_9=((int32_t)(4ULL));
((dn_r_ProtocolValue){dn_tmp_7,dn_tmp_8,dn_tmp_9}); });return dn_tmp_10; }
}
else{
}
if(({ size_t dn_tmp_11=v_width;
size_t dn_tmp_12=({ size_t dn_tmp_13=({ dn_t_slice0_U8 dn_tmp_14=v_bytes;
(dn_tmp_14).len; });
size_t dn_tmp_15=v_offset;
dn_USize_sub(dn_tmp_13,dn_tmp_15); });
(dn_tmp_11 > dn_tmp_12); })){
{ dn_r_ProtocolValue dn_tmp_19=({ uint64_t dn_tmp_16=((uint64_t)(0ULL));
size_t dn_tmp_17=v_offset;
int32_t dn_tmp_18=((int32_t)(1ULL));
((dn_r_ProtocolValue){dn_tmp_16,dn_tmp_17,dn_tmp_18}); });return dn_tmp_19; }
}
else{
}
uint64_t v_value=((uint64_t)(0ULL));
{ size_t dn_tmp_21=((size_t)(0ULL));size_t dn_tmp_20=v_width;for(size_t v_index=dn_tmp_21;v_index<dn_tmp_20;v_index++){
{ uint64_t dn_tmp_35=({ uint64_t dn_tmp_22=v_value;
uint64_t dn_tmp_23=({ uint64_t dn_tmp_24=({ uint8_t dn_tmp_25=(*({ dn_t_slice0_U8 *dn_tmp_26=&(v_bytes);size_t dn_tmp_27=({ size_t dn_tmp_28=v_offset;
size_t dn_tmp_29=v_index;
dn_USize_add(dn_tmp_28,dn_tmp_29); });&dn_tmp_26->data[dn_index(dn_tmp_27,dn_tmp_26->len)]; }));
if((unsigned __int128)dn_tmp_25>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_25; });
uint64_t dn_tmp_30=({ size_t dn_tmp_31=({ size_t dn_tmp_32=v_index;
size_t dn_tmp_33=((size_t)(8ULL));
dn_USize_mul(dn_tmp_32,dn_tmp_33); });
if((unsigned __int128)dn_tmp_31>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_31; });
dn_U64_shl(dn_tmp_24,dn_tmp_30); });
dn_U64_bits((uint64_t)dn_tmp_22 | (uint64_t)dn_tmp_23); });uint64_t *dn_tmp_34=&(v_value);*dn_tmp_34=dn_tmp_35; }
}
 }
{ dn_r_ProtocolValue dn_tmp_41=({ uint64_t dn_tmp_36=v_value;
size_t dn_tmp_37=({ size_t dn_tmp_38=v_offset;
size_t dn_tmp_39=v_width;
dn_USize_add(dn_tmp_38,dn_tmp_39); });
int32_t dn_tmp_40=((int32_t)(0ULL));
((dn_r_ProtocolValue){dn_tmp_36,dn_tmp_37,dn_tmp_40}); });return dn_tmp_41; }
}

dn_fail("missing native return");
}
static dn_r_ProtocolValue dn_cb_net_read_le(dn_t_slice0_U8 v_bytes,size_t v_offset,size_t v_width) {
 if(!dn_enter()){return (dn_r_ProtocolValue){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ProtocolValue){0};}
 dn_r_ProtocolValue dn_result=dn_f_net_read_le(v_bytes,v_offset,v_width);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static int32_t dn_f_net_write_le(dn_t_buf_U8 * v_bytes,size_t v_offset,size_t v_width,uint64_t v_value) {
{
if(((({ size_t dn_tmp_0=v_width;
size_t dn_tmp_1=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); }) || ({ size_t dn_tmp_2=v_width;
size_t dn_tmp_3=((size_t)(8ULL));
(dn_tmp_2 > dn_tmp_3); })) || ({ size_t dn_tmp_4=v_offset;
size_t dn_tmp_5=({ dn_t_buf_U8 * dn_tmp_6=v_bytes;
(*(dn_tmp_6)).len; });
(dn_tmp_4 > dn_tmp_5); }))){
{ int32_t dn_tmp_7=((int32_t)(4ULL));return dn_tmp_7; }
}
else{
}
if(({ size_t dn_tmp_8=v_width;
size_t dn_tmp_9=({ size_t dn_tmp_10=({ dn_t_buf_U8 * dn_tmp_11=v_bytes;
(*(dn_tmp_11)).len; });
size_t dn_tmp_12=v_offset;
dn_USize_sub(dn_tmp_10,dn_tmp_12); });
(dn_tmp_8 > dn_tmp_9); })){
{ int32_t dn_tmp_13=((int32_t)(1ULL));return dn_tmp_13; }
}
else{
}
if(({ size_t dn_tmp_14=v_width;
size_t dn_tmp_15=((size_t)(8ULL));
(dn_tmp_14 < dn_tmp_15); })){
if(({ uint64_t dn_tmp_16=({ uint64_t dn_tmp_17=v_value;
uint64_t dn_tmp_18=({ size_t dn_tmp_19=({ size_t dn_tmp_20=v_width;
size_t dn_tmp_21=((size_t)(8ULL));
dn_USize_mul(dn_tmp_20,dn_tmp_21); });
if((unsigned __int128)dn_tmp_19>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_19; });
dn_U64_shr(dn_tmp_17,dn_tmp_18); });
uint64_t dn_tmp_22=({ uint64_t dn_tmp_23=((uint64_t)(0ULL));
if((unsigned __int128)dn_tmp_23>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_23; });
(dn_tmp_16 != dn_tmp_22); })){
{ int32_t dn_tmp_24=((int32_t)(7ULL));return dn_tmp_24; }
}
else{
}
}
else{
}
{ size_t dn_tmp_26=((size_t)(0ULL));size_t dn_tmp_25=v_width;for(size_t v_index=dn_tmp_26;v_index<dn_tmp_25;v_index++){
{ uint8_t dn_tmp_41=({ uint64_t dn_tmp_31=({ uint64_t dn_tmp_32=({ uint64_t dn_tmp_33=v_value;
uint64_t dn_tmp_34=({ size_t dn_tmp_35=({ size_t dn_tmp_36=v_index;
size_t dn_tmp_37=((size_t)(8ULL));
dn_USize_mul(dn_tmp_36,dn_tmp_37); });
if((unsigned __int128)dn_tmp_35>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_35; });
dn_U64_shr(dn_tmp_33,dn_tmp_34); });
uint64_t dn_tmp_38=({ uint64_t dn_tmp_39=((uint64_t)(255ULL));
if((unsigned __int128)dn_tmp_39>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_39; });
dn_U64_bits((uint64_t)dn_tmp_32 & (uint64_t)dn_tmp_38); });
if((unsigned __int128)dn_tmp_31>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_31; });uint8_t *dn_tmp_40=&((*({ dn_t_buf_U8 * dn_tmp_27=v_bytes;size_t dn_tmp_28=({ size_t dn_tmp_29=v_offset;
size_t dn_tmp_30=v_index;
dn_USize_add(dn_tmp_29,dn_tmp_30); });&dn_tmp_27->data[dn_index(dn_tmp_28,dn_tmp_27->len)]; })));*dn_tmp_40=dn_tmp_41; }
}
 }
{ int32_t dn_tmp_42=((int32_t)(0ULL));return dn_tmp_42; }
}

dn_fail("missing native return");
}
static int32_t dn_cb_net_write_le(dn_t_buf_U8 * v_bytes,size_t v_offset,size_t v_width,uint64_t v_value) {
 if(!dn_enter()){return (int32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (int32_t){0};}
 int32_t dn_result=dn_f_net_write_le(v_bytes,v_offset,v_width,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ProtocolValue dn_f_net_read_be(dn_t_slice0_U8 v_bytes,size_t v_offset,size_t v_width) {
{
{ dn_r_ProtocolValue dn_tmp_3=({ dn_t_slice0_U8 dn_tmp_0=v_bytes;
size_t dn_tmp_1=v_offset;
size_t dn_tmp_2=v_width;
dn_f_protocol_read_be(dn_tmp_0,dn_tmp_1,dn_tmp_2); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static dn_r_ProtocolValue dn_cb_net_read_be(dn_t_slice0_U8 v_bytes,size_t v_offset,size_t v_width) {
 if(!dn_enter()){return (dn_r_ProtocolValue){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ProtocolValue){0};}
 dn_r_ProtocolValue dn_result=dn_f_net_read_be(v_bytes,v_offset,v_width);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static int32_t dn_f_net_write_be(dn_t_buf_U8 * v_bytes,size_t v_offset,size_t v_width,uint64_t v_value) {
{
{ int32_t dn_tmp_4=({ dn_t_buf_U8 * dn_tmp_0=v_bytes;
size_t dn_tmp_1=v_offset;
size_t dn_tmp_2=v_width;
uint64_t dn_tmp_3=v_value;
dn_f_protocol_write_be(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static int32_t dn_cb_net_write_be(dn_t_buf_U8 * v_bytes,size_t v_offset,size_t v_width,uint64_t v_value) {
 if(!dn_enter()){return (int32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (int32_t){0};}
 int32_t dn_result=dn_f_net_write_be(v_bytes,v_offset,v_width,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static size_t dn_f_uleb128_size(uint64_t v_value) {
{
uint64_t v_remaining=v_value;
{ size_t dn_tmp_1=((size_t)(0ULL));size_t dn_tmp_0=((size_t)(10ULL));for(size_t v_index=dn_tmp_1;v_index<dn_tmp_0;v_index++){
if(({ uint64_t dn_tmp_2=v_remaining;
uint64_t dn_tmp_3=({ uint64_t dn_tmp_4=((uint64_t)(128ULL));
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_4; });
(dn_tmp_2 < dn_tmp_3); })){
{ size_t dn_tmp_7=({ size_t dn_tmp_5=v_index;
size_t dn_tmp_6=((size_t)(1ULL));
dn_USize_add(dn_tmp_5,dn_tmp_6); });return dn_tmp_7; }
}
else{
}
{ uint64_t dn_tmp_12=({ uint64_t dn_tmp_8=v_remaining;
uint64_t dn_tmp_9=({ uint64_t dn_tmp_10=((uint64_t)(7ULL));
if((unsigned __int128)dn_tmp_10>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_10; });
dn_U64_shr(dn_tmp_8,dn_tmp_9); });uint64_t *dn_tmp_11=&(v_remaining);*dn_tmp_11=dn_tmp_12; }
}
 }
{ size_t dn_tmp_13=((size_t)(10ULL));return dn_tmp_13; }
}

dn_fail("missing native return");
}
static size_t dn_cb_uleb128_size(uint64_t v_value) {
 if(!dn_enter()){return (size_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (size_t){0};}
 size_t dn_result=dn_f_uleb128_size(v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ProtocolValue dn_f_uleb128_read(dn_t_slice0_U8 v_bytes,size_t v_offset,bool v_canonical) {
{
if(({ size_t dn_tmp_0=v_offset;
size_t dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_bytes;
(dn_tmp_2).len; });
(dn_tmp_0 > dn_tmp_1); })){
{ dn_r_ProtocolValue dn_tmp_6=({ uint64_t dn_tmp_3=((uint64_t)(0ULL));
size_t dn_tmp_4=v_offset;
int32_t dn_tmp_5=((int32_t)(4ULL));
((dn_r_ProtocolValue){dn_tmp_3,dn_tmp_4,dn_tmp_5}); });return dn_tmp_6; }
}
else{
}
uint64_t v_value=((uint64_t)(0ULL));
{ size_t dn_tmp_8=((size_t)(0ULL));size_t dn_tmp_7=((size_t)(10ULL));for(size_t v_index=dn_tmp_8;v_index<dn_tmp_7;v_index++){
if(({ size_t dn_tmp_9=v_index;
size_t dn_tmp_10=({ size_t dn_tmp_11=({ dn_t_slice0_U8 dn_tmp_12=v_bytes;
(dn_tmp_12).len; });
size_t dn_tmp_13=v_offset;
dn_USize_sub(dn_tmp_11,dn_tmp_13); });
(dn_tmp_9 >= dn_tmp_10); })){
{ dn_r_ProtocolValue dn_tmp_17=({ uint64_t dn_tmp_14=((uint64_t)(0ULL));
size_t dn_tmp_15=v_offset;
int32_t dn_tmp_16=((int32_t)(1ULL));
((dn_r_ProtocolValue){dn_tmp_14,dn_tmp_15,dn_tmp_16}); });return dn_tmp_17; }
}
else{
}
uint8_t v_byte=(*({ dn_t_slice0_U8 *dn_tmp_18=&(v_bytes);size_t dn_tmp_19=({ size_t dn_tmp_20=v_offset;
size_t dn_tmp_21=v_index;
dn_USize_add(dn_tmp_20,dn_tmp_21); });&dn_tmp_18->data[dn_index(dn_tmp_19,dn_tmp_18->len)]; }));
uint64_t v_payload=({ uint8_t dn_tmp_22=({ uint8_t dn_tmp_23=v_byte;
uint8_t dn_tmp_24=({ uint8_t dn_tmp_25=((uint8_t)(127ULL));
if((unsigned __int128)dn_tmp_25>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_25; });
dn_U8_bits((uint64_t)dn_tmp_23 & (uint64_t)dn_tmp_24); });
if((unsigned __int128)dn_tmp_22>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_22; });
if((({ size_t dn_tmp_26=v_index;
size_t dn_tmp_27=((size_t)(9ULL));
(dn_tmp_26 == dn_tmp_27); }) && (({ uint64_t dn_tmp_28=v_payload;
uint64_t dn_tmp_29=({ uint64_t dn_tmp_30=((uint64_t)(1ULL));
if((unsigned __int128)dn_tmp_30>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_30; });
(dn_tmp_28 > dn_tmp_29); }) || ({ uint8_t dn_tmp_31=({ uint8_t dn_tmp_32=v_byte;
uint8_t dn_tmp_33=({ uint8_t dn_tmp_34=((uint8_t)(128ULL));
if((unsigned __int128)dn_tmp_34>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_34; });
dn_U8_bits((uint64_t)dn_tmp_32 & (uint64_t)dn_tmp_33); });
uint8_t dn_tmp_35=({ uint8_t dn_tmp_36=((uint8_t)(0ULL));
if((unsigned __int128)dn_tmp_36>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_36; });
(dn_tmp_31 != dn_tmp_35); })))){
{ dn_r_ProtocolValue dn_tmp_40=({ uint64_t dn_tmp_37=((uint64_t)(0ULL));
size_t dn_tmp_38=v_offset;
int32_t dn_tmp_39=((int32_t)(7ULL));
((dn_r_ProtocolValue){dn_tmp_37,dn_tmp_38,dn_tmp_39}); });return dn_tmp_40; }
}
else{
}
{ uint64_t dn_tmp_49=({ uint64_t dn_tmp_41=v_value;
uint64_t dn_tmp_42=({ uint64_t dn_tmp_43=v_payload;
uint64_t dn_tmp_44=({ size_t dn_tmp_45=({ size_t dn_tmp_46=v_index;
size_t dn_tmp_47=((size_t)(7ULL));
dn_USize_mul(dn_tmp_46,dn_tmp_47); });
if((unsigned __int128)dn_tmp_45>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_45; });
dn_U64_shl(dn_tmp_43,dn_tmp_44); });
dn_U64_bits((uint64_t)dn_tmp_41 | (uint64_t)dn_tmp_42); });uint64_t *dn_tmp_48=&(v_value);*dn_tmp_48=dn_tmp_49; }
if(({ uint8_t dn_tmp_50=({ uint8_t dn_tmp_51=v_byte;
uint8_t dn_tmp_52=({ uint8_t dn_tmp_53=((uint8_t)(128ULL));
if((unsigned __int128)dn_tmp_53>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_53; });
dn_U8_bits((uint64_t)dn_tmp_51 & (uint64_t)dn_tmp_52); });
uint8_t dn_tmp_54=({ uint8_t dn_tmp_55=((uint8_t)(0ULL));
if((unsigned __int128)dn_tmp_55>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_55; });
(dn_tmp_50 == dn_tmp_54); })){
if(((v_canonical && ({ size_t dn_tmp_56=v_index;
size_t dn_tmp_57=((size_t)(0ULL));
(dn_tmp_56 > dn_tmp_57); })) && ({ uint64_t dn_tmp_58=v_payload;
uint64_t dn_tmp_59=({ uint64_t dn_tmp_60=((uint64_t)(0ULL));
if((unsigned __int128)dn_tmp_60>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_60; });
(dn_tmp_58 == dn_tmp_59); }))){
{ dn_r_ProtocolValue dn_tmp_64=({ uint64_t dn_tmp_61=((uint64_t)(0ULL));
size_t dn_tmp_62=v_offset;
int32_t dn_tmp_63=((int32_t)(4ULL));
((dn_r_ProtocolValue){dn_tmp_61,dn_tmp_62,dn_tmp_63}); });return dn_tmp_64; }
}
else{
}
{ dn_r_ProtocolValue dn_tmp_72=({ uint64_t dn_tmp_65=v_value;
size_t dn_tmp_66=({ size_t dn_tmp_67=({ size_t dn_tmp_68=v_offset;
size_t dn_tmp_69=v_index;
dn_USize_add(dn_tmp_68,dn_tmp_69); });
size_t dn_tmp_70=((size_t)(1ULL));
dn_USize_add(dn_tmp_67,dn_tmp_70); });
int32_t dn_tmp_71=((int32_t)(0ULL));
((dn_r_ProtocolValue){dn_tmp_65,dn_tmp_66,dn_tmp_71}); });return dn_tmp_72; }
}
else{
}
}
 }
{ dn_r_ProtocolValue dn_tmp_76=({ uint64_t dn_tmp_73=((uint64_t)(0ULL));
size_t dn_tmp_74=v_offset;
int32_t dn_tmp_75=((int32_t)(7ULL));
((dn_r_ProtocolValue){dn_tmp_73,dn_tmp_74,dn_tmp_75}); });return dn_tmp_76; }
}

dn_fail("missing native return");
}
static dn_r_ProtocolValue dn_cb_uleb128_read(dn_t_slice0_U8 v_bytes,size_t v_offset,bool v_canonical) {
 if(!dn_enter()){return (dn_r_ProtocolValue){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ProtocolValue){0};}
 dn_r_ProtocolValue dn_result=dn_f_uleb128_read(v_bytes,v_offset,v_canonical);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ProtocolValue dn_f_uleb128_write(dn_t_buf_U8 * v_bytes,size_t v_offset,uint64_t v_value) {
{
if(({ size_t dn_tmp_0=v_offset;
size_t dn_tmp_1=({ dn_t_buf_U8 * dn_tmp_2=v_bytes;
(*(dn_tmp_2)).len; });
(dn_tmp_0 > dn_tmp_1); })){
{ dn_r_ProtocolValue dn_tmp_6=({ uint64_t dn_tmp_3=((uint64_t)(0ULL));
size_t dn_tmp_4=v_offset;
int32_t dn_tmp_5=((int32_t)(4ULL));
((dn_r_ProtocolValue){dn_tmp_3,dn_tmp_4,dn_tmp_5}); });return dn_tmp_6; }
}
else{
}
size_t v_count=({ uint64_t dn_tmp_7=v_value;
dn_f_uleb128_size(dn_tmp_7); });
if(({ size_t dn_tmp_8=v_count;
size_t dn_tmp_9=({ size_t dn_tmp_10=({ dn_t_buf_U8 * dn_tmp_11=v_bytes;
(*(dn_tmp_11)).len; });
size_t dn_tmp_12=v_offset;
dn_USize_sub(dn_tmp_10,dn_tmp_12); });
(dn_tmp_8 > dn_tmp_9); })){
{ dn_r_ProtocolValue dn_tmp_16=({ uint64_t dn_tmp_13=((uint64_t)(0ULL));
size_t dn_tmp_14=v_offset;
int32_t dn_tmp_15=((int32_t)(1ULL));
((dn_r_ProtocolValue){dn_tmp_13,dn_tmp_14,dn_tmp_15}); });return dn_tmp_16; }
}
else{
}
uint64_t v_remaining=v_value;
{ size_t dn_tmp_18=((size_t)(0ULL));size_t dn_tmp_17=v_count;for(size_t v_index=dn_tmp_18;v_index<dn_tmp_17;v_index++){
uint8_t v_byte=({ uint64_t dn_tmp_19=({ uint64_t dn_tmp_20=v_remaining;
uint64_t dn_tmp_21=({ uint64_t dn_tmp_22=((uint64_t)(127ULL));
if((unsigned __int128)dn_tmp_22>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_22; });
dn_U64_bits((uint64_t)dn_tmp_20 & (uint64_t)dn_tmp_21); });
if((unsigned __int128)dn_tmp_19>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_19; });
{ uint64_t dn_tmp_27=({ uint64_t dn_tmp_23=v_remaining;
uint64_t dn_tmp_24=({ uint64_t dn_tmp_25=((uint64_t)(7ULL));
if((unsigned __int128)dn_tmp_25>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_25; });
dn_U64_shr(dn_tmp_23,dn_tmp_24); });uint64_t *dn_tmp_26=&(v_remaining);*dn_tmp_26=dn_tmp_27; }
if(({ size_t dn_tmp_28=({ size_t dn_tmp_29=v_index;
size_t dn_tmp_30=((size_t)(1ULL));
dn_USize_add(dn_tmp_29,dn_tmp_30); });
size_t dn_tmp_31=v_count;
(dn_tmp_28 < dn_tmp_31); })){
{ uint8_t dn_tmp_36=({ uint8_t dn_tmp_32=v_byte;
uint8_t dn_tmp_33=({ uint8_t dn_tmp_34=((uint8_t)(128ULL));
if((unsigned __int128)dn_tmp_34>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_34; });
dn_U8_bits((uint64_t)dn_tmp_32 | (uint64_t)dn_tmp_33); });uint8_t *dn_tmp_35=&(v_byte);*dn_tmp_35=dn_tmp_36; }
}
else{
}
{ uint8_t dn_tmp_42=v_byte;uint8_t *dn_tmp_41=&((*({ dn_t_buf_U8 * dn_tmp_37=v_bytes;size_t dn_tmp_38=({ size_t dn_tmp_39=v_offset;
size_t dn_tmp_40=v_index;
dn_USize_add(dn_tmp_39,dn_tmp_40); });&dn_tmp_37->data[dn_index(dn_tmp_38,dn_tmp_37->len)]; })));*dn_tmp_41=dn_tmp_42; }
}
 }
{ dn_r_ProtocolValue dn_tmp_48=({ uint64_t dn_tmp_43=v_value;
size_t dn_tmp_44=({ size_t dn_tmp_45=v_offset;
size_t dn_tmp_46=v_count;
dn_USize_add(dn_tmp_45,dn_tmp_46); });
int32_t dn_tmp_47=((int32_t)(0ULL));
((dn_r_ProtocolValue){dn_tmp_43,dn_tmp_44,dn_tmp_47}); });return dn_tmp_48; }
}

dn_fail("missing native return");
}
static dn_r_ProtocolValue dn_cb_uleb128_write(dn_t_buf_U8 * v_bytes,size_t v_offset,uint64_t v_value) {
 if(!dn_enter()){return (dn_r_ProtocolValue){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ProtocolValue){0};}
 dn_r_ProtocolValue dn_result=dn_f_uleb128_write(v_bytes,v_offset,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint64_t dn_f_zigzag_encode(int64_t v_value) {
{
if(({ int64_t dn_tmp_0=v_value;
int64_t dn_tmp_1=({ int64_t dn_tmp_2=((int64_t)(0ULL));
if((__int128)dn_tmp_2<(-((__int128)1<<63))||(__int128)dn_tmp_2>(((__int128)1<<63)-1))dn_fail("numeric conversion out of range");(int64_t)dn_tmp_2; });
(dn_tmp_0 >= dn_tmp_1); })){
{ uint64_t dn_tmp_7=({ uint64_t dn_tmp_3=({ int64_t dn_tmp_4=v_value;
if((__int128)dn_tmp_4<0||(__int128)dn_tmp_4>(((__int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_4; });
uint64_t dn_tmp_5=({ uint64_t dn_tmp_6=((uint64_t)(1ULL));
if((unsigned __int128)dn_tmp_6>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_6; });
dn_U64_shl(dn_tmp_3,dn_tmp_5); });return dn_tmp_7; }
}
else{
}
{ uint64_t dn_tmp_18=({ uint64_t dn_tmp_8=({ uint64_t dn_tmp_9=({ int64_t dn_tmp_10=dn_I64_neg(({ int64_t dn_tmp_11=v_value;
int64_t dn_tmp_12=({ int64_t dn_tmp_13=((int64_t)(1ULL));
if((__int128)dn_tmp_13<(-((__int128)1<<63))||(__int128)dn_tmp_13>(((__int128)1<<63)-1))dn_fail("numeric conversion out of range");(int64_t)dn_tmp_13; });
dn_I64_add(dn_tmp_11,dn_tmp_12); }));
if((__int128)dn_tmp_10<0||(__int128)dn_tmp_10>(((__int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_10; });
uint64_t dn_tmp_14=({ uint64_t dn_tmp_15=((uint64_t)(1ULL));
if((unsigned __int128)dn_tmp_15>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_15; });
dn_U64_shl(dn_tmp_9,dn_tmp_14); });
uint64_t dn_tmp_16=({ uint64_t dn_tmp_17=((uint64_t)(1ULL));
if((unsigned __int128)dn_tmp_17>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_17; });
dn_U64_bits((uint64_t)dn_tmp_8 | (uint64_t)dn_tmp_16); });return dn_tmp_18; }
}

dn_fail("missing native return");
}
static uint64_t dn_cb_zigzag_encode(int64_t v_value) {
 if(!dn_enter()){return (uint64_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint64_t){0};}
 uint64_t dn_result=dn_f_zigzag_encode(v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static int64_t dn_f_zigzag_decode(uint64_t v_value) {
{
int64_t v_half=({ uint64_t dn_tmp_0=({ uint64_t dn_tmp_1=v_value;
uint64_t dn_tmp_2=({ uint64_t dn_tmp_3=((uint64_t)(1ULL));
if((unsigned __int128)dn_tmp_3>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_3; });
dn_U64_shr(dn_tmp_1,dn_tmp_2); });
if((unsigned __int128)dn_tmp_0>(((unsigned __int128)1<<63)-1))dn_fail("numeric conversion out of range");(int64_t)dn_tmp_0; });
if(({ uint64_t dn_tmp_4=({ uint64_t dn_tmp_5=v_value;
uint64_t dn_tmp_6=({ uint64_t dn_tmp_7=((uint64_t)(1ULL));
if((unsigned __int128)dn_tmp_7>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_7; });
dn_U64_bits((uint64_t)dn_tmp_5 & (uint64_t)dn_tmp_6); });
uint64_t dn_tmp_8=({ uint64_t dn_tmp_9=((uint64_t)(0ULL));
if((unsigned __int128)dn_tmp_9>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_9; });
(dn_tmp_4 == dn_tmp_8); })){
{ int64_t dn_tmp_10=v_half;return dn_tmp_10; }
}
else{
}
{ int64_t dn_tmp_14=({ int64_t dn_tmp_11=dn_I64_neg(v_half);
int64_t dn_tmp_12=({ int64_t dn_tmp_13=((int64_t)(1ULL));
if((__int128)dn_tmp_13<(-((__int128)1<<63))||(__int128)dn_tmp_13>(((__int128)1<<63)-1))dn_fail("numeric conversion out of range");(int64_t)dn_tmp_13; });
dn_I64_sub(dn_tmp_11,dn_tmp_12); });return dn_tmp_14; }
}

dn_fail("missing native return");
}
static int64_t dn_cb_zigzag_decode(uint64_t v_value) {
 if(!dn_enter()){return (int64_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (int64_t){0};}
 int64_t dn_result=dn_f_zigzag_decode(v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_buf_U8 dn_f_wire_new(uint16_t v_version) {
{
dn_t_buf_U8 volatile v_bytes={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_bytes);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
(void)(({ bool dn_tmp_2=(({ uint16_t dn_tmp_3=v_version;
uint16_t dn_tmp_4=({ uint16_t dn_tmp_5=((uint16_t)(1ULL));
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<16)-1))dn_fail("numeric conversion out of range");(uint16_t)dn_tmp_5; });
(dn_tmp_3 == dn_tmp_4); }) || ({ uint16_t dn_tmp_6=v_version;
uint16_t dn_tmp_7=({ uint16_t dn_tmp_8=((uint16_t)(2ULL));
if((unsigned __int128)dn_tmp_8>(((unsigned __int128)1<<16)-1))dn_fail("numeric conversion out of range");(uint16_t)dn_tmp_8; });
(dn_tmp_6 == dn_tmp_7); }));
(dn_assert(dn_tmp_2),0); }));
v_bytes=({ size_t dn_tmp_9=((size_t)(8ULL));
dn_new_buf_U8(dn_tmp_9); });
(void)(({ dn_t_buf_U8 * dn_tmp_10=(&(v_bytes));
size_t dn_tmp_11=((size_t)(0ULL));
uint32_t dn_tmp_12=((uint32_t)(0x424D4C44ULL));
(dn_binary_write((*(dn_tmp_10)).data,(*(dn_tmp_10)).len,dn_tmp_11,4,(uint64_t)dn_tmp_12,0),0); }));
(void)(({ dn_t_buf_U8 * dn_tmp_13=(&(v_bytes));
size_t dn_tmp_14=((size_t)(4ULL));
uint16_t dn_tmp_15=v_version;
(dn_binary_write((*(dn_tmp_13)).data,(*(dn_tmp_13)).len,dn_tmp_14,2,(uint64_t)dn_tmp_15,0),0); }));
(void)(({ dn_t_buf_U8 * dn_tmp_16=(&(v_bytes));
size_t dn_tmp_17=((size_t)(6ULL));
uint16_t dn_tmp_18=((uint16_t)(0ULL));
(dn_binary_write((*(dn_tmp_16)).data,(*(dn_tmp_16)).len,dn_tmp_17,2,(uint64_t)dn_tmp_18,0),0); }));
{ dn_t_buf_U8 dn_tmp_20=({ dn_t_buf_U8 dn_tmp_19=v_bytes;v_bytes=(dn_t_buf_U8){0};dn_adopt_buf_U8(&dn_tmp_19);dn_tmp_19; });dn_adopt_buf_U8(&dn_tmp_20);dn_drop_buf_U8(&v_bytes);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_20; }
dn_drop_buf_U8(&v_bytes);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_t_buf_U8 dn_cb_wire_new(uint16_t v_version) {
 if(!dn_enter()){return (dn_t_buf_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_buf_U8){0};}
 dn_t_buf_U8 dn_result=dn_f_wire_new(v_version);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_wire_valid(dn_t_buf_U8 const * v_bytes) {
{
if(({ size_t dn_tmp_0=({ dn_t_buf_U8 const * dn_tmp_1=v_bytes;
(*(dn_tmp_1)).len; });
size_t dn_tmp_2=((size_t)(8ULL));
(dn_tmp_0 < dn_tmp_2); })){
{ bool dn_tmp_3=0;return dn_tmp_3; }
}
else{
}
if(({ uint32_t dn_tmp_4=({ dn_t_buf_U8 const * dn_tmp_5=v_bytes;
size_t dn_tmp_6=((size_t)(0ULL));
dn_U32_bits(dn_binary_read((*(dn_tmp_5)).data,(*(dn_tmp_5)).len,dn_tmp_6,4,0)); });
uint32_t dn_tmp_7=({ uint32_t dn_tmp_8=((uint32_t)(0x424D4C44ULL));
if((unsigned __int128)dn_tmp_8>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_8; });
(dn_tmp_4 != dn_tmp_7); })){
{ bool dn_tmp_9=0;return dn_tmp_9; }
}
else{
}
uint16_t v_version=({ dn_t_buf_U8 const * dn_tmp_10=v_bytes;
size_t dn_tmp_11=((size_t)(4ULL));
dn_U16_bits(dn_binary_read((*(dn_tmp_10)).data,(*(dn_tmp_10)).len,dn_tmp_11,2,0)); });
if((({ uint16_t dn_tmp_12=v_version;
uint16_t dn_tmp_13=({ uint16_t dn_tmp_14=((uint16_t)(1ULL));
if((unsigned __int128)dn_tmp_14>(((unsigned __int128)1<<16)-1))dn_fail("numeric conversion out of range");(uint16_t)dn_tmp_14; });
(dn_tmp_12 != dn_tmp_13); }) && ({ uint16_t dn_tmp_15=v_version;
uint16_t dn_tmp_16=({ uint16_t dn_tmp_17=((uint16_t)(2ULL));
if((unsigned __int128)dn_tmp_17>(((unsigned __int128)1<<16)-1))dn_fail("numeric conversion out of range");(uint16_t)dn_tmp_17; });
(dn_tmp_15 != dn_tmp_16); }))){
{ bool dn_tmp_18=0;return dn_tmp_18; }
}
else{
}
size_t v_position=((size_t)(8ULL));
while(({ size_t dn_tmp_19=v_position;
size_t dn_tmp_20=({ dn_t_buf_U8 const * dn_tmp_21=v_bytes;
(*(dn_tmp_21)).len; });
(dn_tmp_19 < dn_tmp_20); })){
if(({ size_t dn_tmp_22=({ size_t dn_tmp_23=({ dn_t_buf_U8 const * dn_tmp_24=v_bytes;
(*(dn_tmp_24)).len; });
size_t dn_tmp_25=v_position;
dn_USize_sub(dn_tmp_23,dn_tmp_25); });
size_t dn_tmp_26=((size_t)(8ULL));
(dn_tmp_22 < dn_tmp_26); })){
{ bool dn_tmp_27=0;return dn_tmp_27; }
}
else{
}
size_t v_size=({ uint32_t dn_tmp_28=({ dn_t_buf_U8 const * dn_tmp_29=v_bytes;
size_t dn_tmp_30=({ size_t dn_tmp_31=v_position;
size_t dn_tmp_32=((size_t)(4ULL));
dn_USize_add(dn_tmp_31,dn_tmp_32); });
dn_U32_bits(dn_binary_read((*(dn_tmp_29)).data,(*(dn_tmp_29)).len,dn_tmp_30,4,0)); });
if((unsigned __int128)dn_tmp_28>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_28; });
if(({ size_t dn_tmp_33=v_size;
size_t dn_tmp_34=({ size_t dn_tmp_35=({ size_t dn_tmp_36=({ dn_t_buf_U8 const * dn_tmp_37=v_bytes;
(*(dn_tmp_37)).len; });
size_t dn_tmp_38=v_position;
dn_USize_sub(dn_tmp_36,dn_tmp_38); });
size_t dn_tmp_39=((size_t)(8ULL));
dn_USize_sub(dn_tmp_35,dn_tmp_39); });
(dn_tmp_33 > dn_tmp_34); })){
{ bool dn_tmp_40=0;return dn_tmp_40; }
}
else{
}
{ size_t dn_tmp_46=({ size_t dn_tmp_41=({ size_t dn_tmp_42=v_position;
size_t dn_tmp_43=((size_t)(8ULL));
dn_USize_add(dn_tmp_42,dn_tmp_43); });
size_t dn_tmp_44=v_size;
dn_USize_add(dn_tmp_41,dn_tmp_44); });size_t *dn_tmp_45=&(v_position);*dn_tmp_45=dn_tmp_46; }
}
{ bool dn_tmp_47=1;return dn_tmp_47; }
}

dn_fail("missing native return");
}
static bool dn_cb_wire_valid(dn_t_buf_U8 const * v_bytes) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_wire_valid(v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static size_t dn_f_wire_find(dn_t_buf_U8 const * v_bytes,uint32_t v_id) {
{
(void)(({ bool dn_tmp_0=({ dn_t_buf_U8 const * dn_tmp_1=v_bytes;
dn_f_wire_valid(dn_tmp_1); });
(dn_assert(dn_tmp_0),0); }));
size_t v_position=((size_t)(8ULL));
while(({ size_t dn_tmp_2=v_position;
size_t dn_tmp_3=({ dn_t_buf_U8 const * dn_tmp_4=v_bytes;
(*(dn_tmp_4)).len; });
(dn_tmp_2 < dn_tmp_3); })){
if(({ uint32_t dn_tmp_5=({ dn_t_buf_U8 const * dn_tmp_6=v_bytes;
size_t dn_tmp_7=v_position;
dn_U32_bits(dn_binary_read((*(dn_tmp_6)).data,(*(dn_tmp_6)).len,dn_tmp_7,4,0)); });
uint32_t dn_tmp_8=v_id;
(dn_tmp_5 == dn_tmp_8); })){
{ size_t dn_tmp_9=v_position;return dn_tmp_9; }
}
else{
}
{ size_t dn_tmp_20=({ size_t dn_tmp_10=({ size_t dn_tmp_11=v_position;
size_t dn_tmp_12=((size_t)(8ULL));
dn_USize_add(dn_tmp_11,dn_tmp_12); });
size_t dn_tmp_13=({ uint32_t dn_tmp_14=({ dn_t_buf_U8 const * dn_tmp_15=v_bytes;
size_t dn_tmp_16=({ size_t dn_tmp_17=v_position;
size_t dn_tmp_18=((size_t)(4ULL));
dn_USize_add(dn_tmp_17,dn_tmp_18); });
dn_U32_bits(dn_binary_read((*(dn_tmp_15)).data,(*(dn_tmp_15)).len,dn_tmp_16,4,0)); });
if((unsigned __int128)dn_tmp_14>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_14; });
dn_USize_add(dn_tmp_10,dn_tmp_13); });size_t *dn_tmp_19=&(v_position);*dn_tmp_19=dn_tmp_20; }
}
{ size_t dn_tmp_22=({ dn_t_buf_U8 const * dn_tmp_21=v_bytes;
(*(dn_tmp_21)).len; });return dn_tmp_22; }
}

dn_fail("missing native return");
}
static size_t dn_cb_wire_find(dn_t_buf_U8 const * v_bytes,uint32_t v_id) {
 if(!dn_enter()){return (size_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (size_t){0};}
 size_t dn_result=dn_f_wire_find(v_bytes,v_id);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_slice0_U8 dn_f_wire_field(dn_t_buf_U8 const * v_bytes,uint32_t v_id) {
{
size_t v_offset=({ dn_t_buf_U8 const * dn_tmp_0=v_bytes;
uint32_t dn_tmp_1=v_id;
dn_f_wire_find(dn_tmp_0,dn_tmp_1); });
if(({ size_t dn_tmp_2=v_offset;
size_t dn_tmp_3=({ dn_t_buf_U8 const * dn_tmp_4=v_bytes;
(*(dn_tmp_4)).len; });
(dn_tmp_2 == dn_tmp_3); })){
{ dn_t_slice0_U8 dn_tmp_9=({ dn_t_buf_U8 const * dn_tmp_5=v_bytes;
size_t dn_tmp_6=({ dn_t_buf_U8 const * dn_tmp_7=v_bytes;
(*(dn_tmp_7)).len; });
size_t dn_tmp_8=((size_t)(0ULL));
({ dn_range(dn_tmp_6,dn_tmp_8,(*(dn_tmp_5)).len);(dn_t_slice0_U8){(*(dn_tmp_5)).data?(*(dn_tmp_5)).data+dn_tmp_6:NULL,dn_tmp_8}; }); });return dn_tmp_9; }
}
else{
}
size_t v_size=({ uint32_t dn_tmp_10=({ dn_t_buf_U8 const * dn_tmp_11=v_bytes;
size_t dn_tmp_12=({ size_t dn_tmp_13=v_offset;
size_t dn_tmp_14=((size_t)(4ULL));
dn_USize_add(dn_tmp_13,dn_tmp_14); });
dn_U32_bits(dn_binary_read((*(dn_tmp_11)).data,(*(dn_tmp_11)).len,dn_tmp_12,4,0)); });
if((unsigned __int128)dn_tmp_10>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_10; });
{ dn_t_slice0_U8 dn_tmp_20=({ dn_t_buf_U8 const * dn_tmp_15=v_bytes;
size_t dn_tmp_16=({ size_t dn_tmp_17=v_offset;
size_t dn_tmp_18=((size_t)(8ULL));
dn_USize_add(dn_tmp_17,dn_tmp_18); });
size_t dn_tmp_19=v_size;
({ dn_range(dn_tmp_16,dn_tmp_19,(*(dn_tmp_15)).len);(dn_t_slice0_U8){(*(dn_tmp_15)).data?(*(dn_tmp_15)).data+dn_tmp_16:NULL,dn_tmp_19}; }); });return dn_tmp_20; }
}

dn_fail("missing native return");
}
static dn_t_slice0_U8 dn_cb_wire_field(dn_t_buf_U8 const * v_bytes,uint32_t v_id) {
 if(!dn_enter()){return (dn_t_slice0_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_slice0_U8){0};}
 dn_t_slice0_U8 dn_result=dn_f_wire_field(v_bytes,v_id);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_wire_set(dn_t_buf_U8 * v_bytes,uint32_t v_id,dn_t_slice0_U8 v_payload) {
{
(void)(({ bool dn_tmp_0=({ dn_t_buf_U8 * dn_tmp_1=v_bytes;
dn_f_wire_valid(dn_tmp_1); });
(dn_assert(dn_tmp_0),0); }));
(void)(({ bool dn_tmp_2=({ size_t dn_tmp_3=({ dn_t_slice0_U8 dn_tmp_4=v_payload;
(dn_tmp_4).len; });
size_t dn_tmp_5=({ size_t dn_tmp_6=((size_t)(4294967295ULL));
if((unsigned __int128)dn_tmp_6>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_6; });
(dn_tmp_3 <= dn_tmp_5); });
(dn_assert(dn_tmp_2),0); }));
size_t v_offset=({ dn_t_buf_U8 * dn_tmp_7=v_bytes;
uint32_t dn_tmp_8=v_id;
dn_f_wire_find(dn_tmp_7,dn_tmp_8); });
size_t v_old_length=({ dn_t_buf_U8 * dn_tmp_9=v_bytes;
(*(dn_tmp_9)).len; });
size_t v_old_size=((size_t)(0ULL));
size_t v_tail=v_old_length;
if(({ size_t dn_tmp_10=v_offset;
size_t dn_tmp_11=v_old_length;
(dn_tmp_10 < dn_tmp_11); })){
{ size_t dn_tmp_20=({ size_t dn_tmp_12=((size_t)(8ULL));
size_t dn_tmp_13=({ uint32_t dn_tmp_14=({ dn_t_buf_U8 * dn_tmp_15=v_bytes;
size_t dn_tmp_16=({ size_t dn_tmp_17=v_offset;
size_t dn_tmp_18=((size_t)(4ULL));
dn_USize_add(dn_tmp_17,dn_tmp_18); });
dn_U32_bits(dn_binary_read((*(dn_tmp_15)).data,(*(dn_tmp_15)).len,dn_tmp_16,4,0)); });
if((unsigned __int128)dn_tmp_14>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_14; });
dn_USize_add(dn_tmp_12,dn_tmp_13); });size_t *dn_tmp_19=&(v_old_size);*dn_tmp_19=dn_tmp_20; }
{ size_t dn_tmp_24=({ size_t dn_tmp_21=v_offset;
size_t dn_tmp_22=v_old_size;
dn_USize_add(dn_tmp_21,dn_tmp_22); });size_t *dn_tmp_23=&(v_tail);*dn_tmp_23=dn_tmp_24; }
}
else{
}
size_t v_new_size=({ size_t dn_tmp_25=((size_t)(8ULL));
size_t dn_tmp_26=({ dn_t_slice0_U8 dn_tmp_27=v_payload;
(dn_tmp_27).len; });
dn_USize_add(dn_tmp_25,dn_tmp_26); });
size_t v_new_length=({ size_t dn_tmp_28=({ size_t dn_tmp_29=v_old_length;
size_t dn_tmp_30=v_old_size;
dn_USize_sub(dn_tmp_29,dn_tmp_30); });
size_t dn_tmp_31=v_new_size;
dn_USize_add(dn_tmp_28,dn_tmp_31); });
if(({ size_t dn_tmp_32=v_new_length;
size_t dn_tmp_33=v_old_length;
(dn_tmp_32 > dn_tmp_33); })){
(void)(({ dn_t_buf_U8 * dn_tmp_34=v_bytes;
size_t dn_tmp_35=v_new_length;
dn_resize_buf_U8(dn_tmp_34,dn_tmp_35); }));
size_t v_position=v_old_length;
while(({ size_t dn_tmp_36=v_position;
size_t dn_tmp_37=v_tail;
(dn_tmp_36 > dn_tmp_37); })){
{ size_t dn_tmp_41=({ size_t dn_tmp_38=v_position;
size_t dn_tmp_39=((size_t)(1ULL));
dn_USize_sub(dn_tmp_38,dn_tmp_39); });size_t *dn_tmp_40=&(v_position);*dn_tmp_40=dn_tmp_41; }
{ uint8_t dn_tmp_51=(*({ dn_t_buf_U8 * dn_tmp_48=v_bytes;size_t dn_tmp_49=v_position;&dn_tmp_48->data[dn_index(dn_tmp_49,dn_tmp_48->len)]; }));uint8_t *dn_tmp_50=&((*({ dn_t_buf_U8 * dn_tmp_42=v_bytes;size_t dn_tmp_43=({ size_t dn_tmp_44=({ size_t dn_tmp_45=v_position;
size_t dn_tmp_46=v_old_size;
dn_USize_sub(dn_tmp_45,dn_tmp_46); });
size_t dn_tmp_47=v_new_size;
dn_USize_add(dn_tmp_44,dn_tmp_47); });&dn_tmp_42->data[dn_index(dn_tmp_43,dn_tmp_42->len)]; })));*dn_tmp_50=dn_tmp_51; }
}
}
else{
{ size_t dn_tmp_53=v_tail;size_t dn_tmp_52=v_old_length;for(size_t v_position=dn_tmp_53;v_position<dn_tmp_52;v_position++){
{ uint8_t dn_tmp_63=(*({ dn_t_buf_U8 * dn_tmp_60=v_bytes;size_t dn_tmp_61=v_position;&dn_tmp_60->data[dn_index(dn_tmp_61,dn_tmp_60->len)]; }));uint8_t *dn_tmp_62=&((*({ dn_t_buf_U8 * dn_tmp_54=v_bytes;size_t dn_tmp_55=({ size_t dn_tmp_56=({ size_t dn_tmp_57=v_position;
size_t dn_tmp_58=v_old_size;
dn_USize_sub(dn_tmp_57,dn_tmp_58); });
size_t dn_tmp_59=v_new_size;
dn_USize_add(dn_tmp_56,dn_tmp_59); });&dn_tmp_54->data[dn_index(dn_tmp_55,dn_tmp_54->len)]; })));*dn_tmp_62=dn_tmp_63; }
}
 }
(void)(({ dn_t_buf_U8 * dn_tmp_64=v_bytes;
size_t dn_tmp_65=v_new_length;
dn_resize_buf_U8(dn_tmp_64,dn_tmp_65); }));
}
(void)(({ dn_t_buf_U8 * dn_tmp_66=v_bytes;
size_t dn_tmp_67=v_offset;
uint32_t dn_tmp_68=v_id;
(dn_binary_write((*(dn_tmp_66)).data,(*(dn_tmp_66)).len,dn_tmp_67,4,(uint64_t)dn_tmp_68,0),0); }));
(void)(({ dn_t_buf_U8 * dn_tmp_69=v_bytes;
size_t dn_tmp_70=({ size_t dn_tmp_71=v_offset;
size_t dn_tmp_72=((size_t)(4ULL));
dn_USize_add(dn_tmp_71,dn_tmp_72); });
uint32_t dn_tmp_73=({ size_t dn_tmp_74=({ dn_t_slice0_U8 dn_tmp_75=v_payload;
(dn_tmp_75).len; });
if((unsigned __int128)dn_tmp_74>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_74; });
(dn_binary_write((*(dn_tmp_69)).data,(*(dn_tmp_69)).len,dn_tmp_70,4,(uint64_t)dn_tmp_73,0),0); }));
{ size_t dn_tmp_78=((size_t)(0ULL));size_t dn_tmp_77=({ dn_t_slice0_U8 dn_tmp_76=v_payload;
(dn_tmp_76).len; });for(size_t v_index=dn_tmp_78;v_index<dn_tmp_77;v_index++){
{ uint8_t dn_tmp_88=(*({ dn_t_slice0_U8 *dn_tmp_85=&(v_payload);size_t dn_tmp_86=v_index;&dn_tmp_85->data[dn_tmp_86]; }));uint8_t *dn_tmp_87=&((*({ dn_t_buf_U8 * dn_tmp_79=v_bytes;size_t dn_tmp_80=({ size_t dn_tmp_81=({ size_t dn_tmp_82=v_offset;
size_t dn_tmp_83=((size_t)(8ULL));
dn_USize_add(dn_tmp_82,dn_tmp_83); });
size_t dn_tmp_84=v_index;
dn_USize_add(dn_tmp_81,dn_tmp_84); });&dn_tmp_79->data[dn_index(dn_tmp_80,dn_tmp_79->len)]; })));*dn_tmp_87=dn_tmp_88; }
}
 }
}

return 0;
}
static void dn_cb_wire_set(dn_t_buf_U8 * v_bytes,uint32_t v_id,dn_t_slice0_U8 v_payload) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_wire_set(v_bytes,v_id,v_payload);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_wire_upgrade(dn_t_buf_U8 * v_bytes) {
{
(void)(({ bool dn_tmp_0=({ dn_t_buf_U8 * dn_tmp_1=v_bytes;
dn_f_wire_valid(dn_tmp_1); });
(dn_assert(dn_tmp_0),0); }));
(void)(({ dn_t_buf_U8 * dn_tmp_2=v_bytes;
size_t dn_tmp_3=((size_t)(4ULL));
uint16_t dn_tmp_4=((uint16_t)(2ULL));
(dn_binary_write((*(dn_tmp_2)).data,(*(dn_tmp_2)).len,dn_tmp_3,2,(uint64_t)dn_tmp_4,0),0); }));
}

return 0;
}
static void dn_cb_wire_upgrade(dn_t_buf_U8 * v_bytes) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_wire_upgrade(v_bytes);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static dn_r_Utf8Step dn_f_utf8_next(dn_t_slice0_U8 v_bytes,size_t v_offset) {
{
if(({ size_t dn_tmp_0=v_offset;
size_t dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_bytes;
(dn_tmp_2).len; });
(dn_tmp_0 >= dn_tmp_1); })){
{ dn_r_Utf8Step dn_tmp_6=({ uint32_t dn_tmp_3=((uint32_t)(0ULL));
size_t dn_tmp_4=v_offset;
bool dn_tmp_5=0;
((dn_r_Utf8Step){dn_tmp_3,dn_tmp_4,dn_tmp_5}); });return dn_tmp_6; }
}
else{
}
uint32_t v_first=({ uint8_t dn_tmp_7=(*({ dn_t_slice0_U8 *dn_tmp_8=&(v_bytes);size_t dn_tmp_9=v_offset;&dn_tmp_8->data[dn_index(dn_tmp_9,dn_tmp_8->len)]; }));
if((unsigned __int128)dn_tmp_7>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_7; });
if(({ uint32_t dn_tmp_10=v_first;
uint32_t dn_tmp_11=({ uint32_t dn_tmp_12=((uint32_t)(128ULL));
if((unsigned __int128)dn_tmp_12>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_12; });
(dn_tmp_10 < dn_tmp_11); })){
{ dn_r_Utf8Step dn_tmp_18=({ uint32_t dn_tmp_13=v_first;
size_t dn_tmp_14=({ size_t dn_tmp_15=v_offset;
size_t dn_tmp_16=((size_t)(1ULL));
dn_USize_add(dn_tmp_15,dn_tmp_16); });
bool dn_tmp_17=1;
((dn_r_Utf8Step){dn_tmp_13,dn_tmp_14,dn_tmp_17}); });return dn_tmp_18; }
}
else{
}
size_t v_count=((size_t)(0ULL));
uint32_t v_value=((uint32_t)(0ULL));
uint32_t v_minimum=((uint32_t)(0ULL));
if((({ uint32_t dn_tmp_19=v_first;
uint32_t dn_tmp_20=({ uint32_t dn_tmp_21=((uint32_t)(194ULL));
if((unsigned __int128)dn_tmp_21>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_21; });
(dn_tmp_19 >= dn_tmp_20); }) && ({ uint32_t dn_tmp_22=v_first;
uint32_t dn_tmp_23=({ uint32_t dn_tmp_24=((uint32_t)(223ULL));
if((unsigned __int128)dn_tmp_24>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_24; });
(dn_tmp_22 <= dn_tmp_23); }))){
{ size_t dn_tmp_26=((size_t)(2ULL));size_t *dn_tmp_25=&(v_count);*dn_tmp_25=dn_tmp_26; }
{ uint32_t dn_tmp_31=({ uint32_t dn_tmp_27=v_first;
uint32_t dn_tmp_28=({ uint32_t dn_tmp_29=((uint32_t)(31ULL));
if((unsigned __int128)dn_tmp_29>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_29; });
dn_U32_bits((uint64_t)dn_tmp_27 & (uint64_t)dn_tmp_28); });uint32_t *dn_tmp_30=&(v_value);*dn_tmp_30=dn_tmp_31; }
{ uint32_t dn_tmp_33=((uint32_t)(128ULL));uint32_t *dn_tmp_32=&(v_minimum);*dn_tmp_32=dn_tmp_33; }
}
else{
if((({ uint32_t dn_tmp_34=v_first;
uint32_t dn_tmp_35=({ uint32_t dn_tmp_36=((uint32_t)(224ULL));
if((unsigned __int128)dn_tmp_36>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_36; });
(dn_tmp_34 >= dn_tmp_35); }) && ({ uint32_t dn_tmp_37=v_first;
uint32_t dn_tmp_38=({ uint32_t dn_tmp_39=((uint32_t)(239ULL));
if((unsigned __int128)dn_tmp_39>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_39; });
(dn_tmp_37 <= dn_tmp_38); }))){
{ size_t dn_tmp_41=((size_t)(3ULL));size_t *dn_tmp_40=&(v_count);*dn_tmp_40=dn_tmp_41; }
{ uint32_t dn_tmp_46=({ uint32_t dn_tmp_42=v_first;
uint32_t dn_tmp_43=({ uint32_t dn_tmp_44=((uint32_t)(15ULL));
if((unsigned __int128)dn_tmp_44>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_44; });
dn_U32_bits((uint64_t)dn_tmp_42 & (uint64_t)dn_tmp_43); });uint32_t *dn_tmp_45=&(v_value);*dn_tmp_45=dn_tmp_46; }
{ uint32_t dn_tmp_48=((uint32_t)(2048ULL));uint32_t *dn_tmp_47=&(v_minimum);*dn_tmp_47=dn_tmp_48; }
}
else{
if((({ uint32_t dn_tmp_49=v_first;
uint32_t dn_tmp_50=({ uint32_t dn_tmp_51=((uint32_t)(240ULL));
if((unsigned __int128)dn_tmp_51>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_51; });
(dn_tmp_49 >= dn_tmp_50); }) && ({ uint32_t dn_tmp_52=v_first;
uint32_t dn_tmp_53=({ uint32_t dn_tmp_54=((uint32_t)(244ULL));
if((unsigned __int128)dn_tmp_54>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_54; });
(dn_tmp_52 <= dn_tmp_53); }))){
{ size_t dn_tmp_56=((size_t)(4ULL));size_t *dn_tmp_55=&(v_count);*dn_tmp_55=dn_tmp_56; }
{ uint32_t dn_tmp_61=({ uint32_t dn_tmp_57=v_first;
uint32_t dn_tmp_58=({ uint32_t dn_tmp_59=((uint32_t)(7ULL));
if((unsigned __int128)dn_tmp_59>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_59; });
dn_U32_bits((uint64_t)dn_tmp_57 & (uint64_t)dn_tmp_58); });uint32_t *dn_tmp_60=&(v_value);*dn_tmp_60=dn_tmp_61; }
{ uint32_t dn_tmp_63=((uint32_t)(65536ULL));uint32_t *dn_tmp_62=&(v_minimum);*dn_tmp_62=dn_tmp_63; }
}
else{
{ dn_r_Utf8Step dn_tmp_67=({ uint32_t dn_tmp_64=((uint32_t)(0ULL));
size_t dn_tmp_65=v_offset;
bool dn_tmp_66=0;
((dn_r_Utf8Step){dn_tmp_64,dn_tmp_65,dn_tmp_66}); });return dn_tmp_67; }
}
}
}
if(({ size_t dn_tmp_68=v_count;
size_t dn_tmp_69=({ size_t dn_tmp_70=({ dn_t_slice0_U8 dn_tmp_71=v_bytes;
(dn_tmp_71).len; });
size_t dn_tmp_72=v_offset;
dn_USize_sub(dn_tmp_70,dn_tmp_72); });
(dn_tmp_68 > dn_tmp_69); })){
{ dn_r_Utf8Step dn_tmp_76=({ uint32_t dn_tmp_73=((uint32_t)(0ULL));
size_t dn_tmp_74=v_offset;
bool dn_tmp_75=0;
((dn_r_Utf8Step){dn_tmp_73,dn_tmp_74,dn_tmp_75}); });return dn_tmp_76; }
}
else{
}
{ size_t dn_tmp_78=((size_t)(1ULL));size_t dn_tmp_77=v_count;for(size_t v_index=dn_tmp_78;v_index<dn_tmp_77;v_index++){
uint32_t v_next=({ uint8_t dn_tmp_79=(*({ dn_t_slice0_U8 *dn_tmp_80=&(v_bytes);size_t dn_tmp_81=({ size_t dn_tmp_82=v_offset;
size_t dn_tmp_83=v_index;
dn_USize_add(dn_tmp_82,dn_tmp_83); });&dn_tmp_80->data[dn_index(dn_tmp_81,dn_tmp_80->len)]; }));
if((unsigned __int128)dn_tmp_79>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_79; });
if((({ uint32_t dn_tmp_84=v_next;
uint32_t dn_tmp_85=({ uint32_t dn_tmp_86=((uint32_t)(128ULL));
if((unsigned __int128)dn_tmp_86>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_86; });
(dn_tmp_84 < dn_tmp_85); }) || ({ uint32_t dn_tmp_87=v_next;
uint32_t dn_tmp_88=({ uint32_t dn_tmp_89=((uint32_t)(191ULL));
if((unsigned __int128)dn_tmp_89>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_89; });
(dn_tmp_87 > dn_tmp_88); }))){
{ dn_r_Utf8Step dn_tmp_93=({ uint32_t dn_tmp_90=((uint32_t)(0ULL));
size_t dn_tmp_91=v_offset;
bool dn_tmp_92=0;
((dn_r_Utf8Step){dn_tmp_90,dn_tmp_91,dn_tmp_92}); });return dn_tmp_93; }
}
else{
}
{ uint32_t dn_tmp_103=({ uint32_t dn_tmp_94=({ uint32_t dn_tmp_95=v_value;
uint32_t dn_tmp_96=({ uint32_t dn_tmp_97=((uint32_t)(6ULL));
if((unsigned __int128)dn_tmp_97>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_97; });
dn_U32_shl(dn_tmp_95,dn_tmp_96); });
uint32_t dn_tmp_98=({ uint32_t dn_tmp_99=v_next;
uint32_t dn_tmp_100=({ uint32_t dn_tmp_101=((uint32_t)(63ULL));
if((unsigned __int128)dn_tmp_101>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_101; });
dn_U32_bits((uint64_t)dn_tmp_99 & (uint64_t)dn_tmp_100); });
dn_U32_bits((uint64_t)dn_tmp_94 | (uint64_t)dn_tmp_98); });uint32_t *dn_tmp_102=&(v_value);*dn_tmp_102=dn_tmp_103; }
}
 }
if(((({ uint32_t dn_tmp_104=v_value;
uint32_t dn_tmp_105=v_minimum;
(dn_tmp_104 < dn_tmp_105); }) || ({ uint32_t dn_tmp_106=v_value;
uint32_t dn_tmp_107=({ uint32_t dn_tmp_108=((uint32_t)(1114111ULL));
if((unsigned __int128)dn_tmp_108>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_108; });
(dn_tmp_106 > dn_tmp_107); })) || (({ uint32_t dn_tmp_109=v_value;
uint32_t dn_tmp_110=({ uint32_t dn_tmp_111=((uint32_t)(55296ULL));
if((unsigned __int128)dn_tmp_111>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_111; });
(dn_tmp_109 >= dn_tmp_110); }) && ({ uint32_t dn_tmp_112=v_value;
uint32_t dn_tmp_113=({ uint32_t dn_tmp_114=((uint32_t)(57343ULL));
if((unsigned __int128)dn_tmp_114>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_114; });
(dn_tmp_112 <= dn_tmp_113); })))){
{ dn_r_Utf8Step dn_tmp_118=({ uint32_t dn_tmp_115=((uint32_t)(0ULL));
size_t dn_tmp_116=v_offset;
bool dn_tmp_117=0;
((dn_r_Utf8Step){dn_tmp_115,dn_tmp_116,dn_tmp_117}); });return dn_tmp_118; }
}
else{
}
{ dn_r_Utf8Step dn_tmp_124=({ uint32_t dn_tmp_119=v_value;
size_t dn_tmp_120=({ size_t dn_tmp_121=v_offset;
size_t dn_tmp_122=v_count;
dn_USize_add(dn_tmp_121,dn_tmp_122); });
bool dn_tmp_123=1;
((dn_r_Utf8Step){dn_tmp_119,dn_tmp_120,dn_tmp_123}); });return dn_tmp_124; }
}

dn_fail("missing native return");
}
static dn_r_Utf8Step dn_cb_utf8_next(dn_t_slice0_U8 v_bytes,size_t v_offset) {
 if(!dn_enter()){return (dn_r_Utf8Step){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_Utf8Step){0};}
 dn_r_Utf8Step dn_result=dn_f_utf8_next(v_bytes,v_offset);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_codec_error(int32_t v_kind,uint32_t v_field,size_t v_offset) {
{
{ dn_r_CodecError dn_tmp_3=({ int32_t dn_tmp_0=v_kind;
uint32_t dn_tmp_1=v_field;
size_t dn_tmp_2=v_offset;
((dn_r_CodecError){dn_tmp_0,dn_tmp_1,dn_tmp_2}); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_codec_error(int32_t v_kind,uint32_t v_field,size_t v_offset) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_codec_error(v_kind,v_field,v_offset);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecShape dn_f_codec_shape_error(int32_t v_kind,uint32_t v_field,size_t v_offset) {
{
dn_r_CodecShape v_result=({ ((dn_r_CodecShape){0}); });
{ dn_r_CodecError dn_tmp_4=({ int32_t dn_tmp_0=v_kind;
uint32_t dn_tmp_1=v_field;
size_t dn_tmp_2=v_offset;
((dn_r_CodecError){dn_tmp_0,dn_tmp_1,dn_tmp_2}); });dn_r_CodecError *dn_tmp_3=&((v_result).f_error);*dn_tmp_3=dn_tmp_4; }
{ dn_r_CodecShape dn_tmp_5=v_result;return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecShape dn_cb_codec_shape_error(int32_t v_kind,uint32_t v_field,size_t v_offset) {
 if(!dn_enter()){return (dn_r_CodecShape){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecShape){0};}
 dn_r_CodecShape dn_result=dn_f_codec_shape_error(v_kind,v_field,v_offset);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_codec_plan_check(dn_r_CodecPlan const * v_plan) {
{
if((((((((({ uint32_t dn_tmp_0=(v_plan)->f_message;
uint32_t dn_tmp_1=((uint32_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); }) || ({ uint32_t dn_tmp_2=(v_plan)->f_minor;
uint32_t dn_tmp_3=((uint32_t)(65535ULL));
(dn_tmp_2 > dn_tmp_3); })) || ({ uint32_t dn_tmp_4=(v_plan)->f_field_count;
uint32_t dn_tmp_5=((uint32_t)(32ULL));
(dn_tmp_4 > dn_tmp_5); })) || ({ int32_t dn_tmp_6=({ dn_r_WireFormat dn_tmp_7=(v_plan)->f_wire;
dn_f_network_wire_check(dn_tmp_7); });
int32_t dn_tmp_8=((int32_t)(0ULL));
(dn_tmp_6 != dn_tmp_8); })) || ({ size_t dn_tmp_9=(v_plan)->f_storage_budget;
size_t dn_tmp_10=((size_t)(256ULL));
(dn_tmp_9 < dn_tmp_10); })) || ({ size_t dn_tmp_11=(v_plan)->f_storage_budget;
size_t dn_tmp_12=((size_t)(67108864ULL));
(dn_tmp_11 > dn_tmp_12); })) || ({ uint32_t dn_tmp_13=(v_plan)->f_maximum_unknown_fields;
uint32_t dn_tmp_14=((uint32_t)(4096ULL));
(dn_tmp_13 > dn_tmp_14); })) || ({ size_t dn_tmp_15=(v_plan)->f_maximum_unknown_bytes;
size_t dn_tmp_16=((size_t)(16777216ULL));
(dn_tmp_15 > dn_tmp_16); }))){
{ dn_r_CodecError dn_tmp_20=({ int32_t dn_tmp_17=((int32_t)(4ULL));
uint32_t dn_tmp_18=((uint32_t)(0ULL));
size_t dn_tmp_19=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_17,dn_tmp_18,dn_tmp_19}); });return dn_tmp_20; }
}
else{
}
if(({ uint32_t dn_tmp_21=((v_plan)->f_wire).f_unknown_fields;
uint32_t dn_tmp_22=((uint32_t)(0ULL));
(dn_tmp_21 == dn_tmp_22); })){
if((({ uint32_t dn_tmp_23=(v_plan)->f_maximum_unknown_fields;
uint32_t dn_tmp_24=((uint32_t)(0ULL));
(dn_tmp_23 != dn_tmp_24); }) || ({ size_t dn_tmp_25=(v_plan)->f_maximum_unknown_bytes;
size_t dn_tmp_26=((size_t)(0ULL));
(dn_tmp_25 != dn_tmp_26); }))){
{ dn_r_CodecError dn_tmp_30=({ int32_t dn_tmp_27=((int32_t)(4ULL));
uint32_t dn_tmp_28=((uint32_t)(0ULL));
size_t dn_tmp_29=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_27,dn_tmp_28,dn_tmp_29}); });return dn_tmp_30; }
}
else{
}
}
else{
if((({ uint32_t dn_tmp_31=(v_plan)->f_maximum_unknown_fields;
uint32_t dn_tmp_32=((uint32_t)(0ULL));
(dn_tmp_31 == dn_tmp_32); }) || ({ size_t dn_tmp_33=(v_plan)->f_maximum_unknown_bytes;
size_t dn_tmp_34=((size_t)(8ULL));
(dn_tmp_33 < dn_tmp_34); }))){
{ dn_r_CodecError dn_tmp_38=({ int32_t dn_tmp_35=((int32_t)(4ULL));
uint32_t dn_tmp_36=((uint32_t)(0ULL));
size_t dn_tmp_37=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_35,dn_tmp_36,dn_tmp_37}); });return dn_tmp_38; }
}
else{
}
}
uint64_t v_size=((uint64_t)(4ULL));
if(({ uint32_t dn_tmp_39=((v_plan)->f_wire).f_kind;
uint32_t dn_tmp_40=((uint32_t)(2ULL));
(dn_tmp_39 == dn_tmp_40); })){
{ uint64_t dn_tmp_42=((uint64_t)(12ULL));uint64_t *dn_tmp_41=&(v_size);*dn_tmp_41=dn_tmp_42; }
}
else{
}
uint32_t v_values=((uint32_t)(0ULL));
uint32_t v_minimum_values=((uint32_t)(0ULL));
uint32_t v_prior=((uint32_t)(0ULL));
{ size_t dn_tmp_45=((size_t)(0ULL));size_t dn_tmp_44=({ uint32_t dn_tmp_43=(v_plan)->f_field_count;
if((unsigned __int128)dn_tmp_43>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_43; });for(size_t v_index=dn_tmp_45;v_index<dn_tmp_44;v_index++){
dn_r_MessageField const * v_field=(&((*({ dn_t_arr32_r12_MessageField *dn_tmp_46=&((v_plan)->f_fields);size_t dn_tmp_47=v_index;&dn_tmp_46->data[dn_index(dn_tmp_47,32)]; }))));
if(((({ uint32_t dn_tmp_48=(v_field)->f_id;
uint32_t dn_tmp_49=v_prior;
(dn_tmp_48 <= dn_tmp_49); }) || ({ int32_t dn_tmp_50=({ dn_r_MessageField const * dn_tmp_51=v_field;
dn_f_network_field_check(dn_tmp_51); });
int32_t dn_tmp_52=((int32_t)(0ULL));
(dn_tmp_50 != dn_tmp_52); })) || (!(({ uint32_t dn_tmp_53=(v_field)->f_first_minor;
uint32_t dn_tmp_54=(v_field)->f_last_minor;
uint32_t dn_tmp_55=(v_plan)->f_minor;
dn_f_network_minor_active(dn_tmp_53,dn_tmp_54,dn_tmp_55); }))))){
{ dn_r_CodecError dn_tmp_59=({ int32_t dn_tmp_56=((int32_t)(4ULL));
uint32_t dn_tmp_57=(v_field)->f_id;
size_t dn_tmp_58=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_56,dn_tmp_57,dn_tmp_58}); });return dn_tmp_59; }
}
else{
}
{ uint32_t dn_tmp_61=(v_field)->f_id;uint32_t *dn_tmp_60=&(v_prior);*dn_tmp_60=dn_tmp_61; }
dn_r_SchemaMeasure v_measured=({ dn_r_MessageField const * dn_tmp_62=v_field;
dn_r_WireFormat dn_tmp_63=(v_plan)->f_wire;
dn_f_network_field_measure(dn_tmp_62,dn_tmp_63); });
{ uint64_t dn_tmp_67=({ uint64_t dn_tmp_64=v_size;
uint64_t dn_tmp_65=(v_measured).f_maximum;
dn_U64_add(dn_tmp_64,dn_tmp_65); });uint64_t *dn_tmp_66=&(v_size);*dn_tmp_66=dn_tmp_67; }
{ uint32_t dn_tmp_71=({ uint32_t dn_tmp_68=v_values;
uint32_t dn_tmp_69=(v_field)->f_maximum_count;
dn_U32_add(dn_tmp_68,dn_tmp_69); });uint32_t *dn_tmp_70=&(v_values);*dn_tmp_70=dn_tmp_71; }
{ uint32_t dn_tmp_75=({ uint32_t dn_tmp_72=v_minimum_values;
uint32_t dn_tmp_73=(v_field)->f_minimum_count;
dn_U32_add(dn_tmp_72,dn_tmp_73); });uint32_t *dn_tmp_74=&(v_minimum_values);*dn_tmp_74=dn_tmp_75; }
}
 }
{ uint64_t dn_tmp_80=({ uint64_t dn_tmp_76=v_size;
uint64_t dn_tmp_77=({ size_t dn_tmp_78=(v_plan)->f_maximum_unknown_bytes;
if((unsigned __int128)dn_tmp_78>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_78; });
dn_U64_add(dn_tmp_76,dn_tmp_77); });uint64_t *dn_tmp_79=&(v_size);*dn_tmp_79=dn_tmp_80; }
if((((({ uint64_t dn_tmp_81=v_size;
uint64_t dn_tmp_82=((uint64_t)(16777216ULL));
(dn_tmp_81 > dn_tmp_82); }) || ({ uint64_t dn_tmp_83=v_size;
uint64_t dn_tmp_84=({ size_t dn_tmp_85=(v_plan)->f_maximum_wire;
if((unsigned __int128)dn_tmp_85>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_85; });
(dn_tmp_83 != dn_tmp_84); })) || ({ uint32_t dn_tmp_86=v_values;
uint32_t dn_tmp_87=(v_plan)->f_maximum_values;
(dn_tmp_86 != dn_tmp_87); })) || ({ size_t dn_tmp_88=({ size_t dn_tmp_89=((size_t)(256ULL));
size_t dn_tmp_90=({ size_t dn_tmp_91=({ uint32_t dn_tmp_92=v_minimum_values;
if((unsigned __int128)dn_tmp_92>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_92; });
size_t dn_tmp_93=((size_t)(32ULL));
dn_USize_mul(dn_tmp_91,dn_tmp_93); });
dn_USize_add(dn_tmp_89,dn_tmp_90); });
size_t dn_tmp_94=(v_plan)->f_storage_budget;
(dn_tmp_88 > dn_tmp_94); }))){
{ dn_r_CodecError dn_tmp_98=({ int32_t dn_tmp_95=((int32_t)(4ULL));
uint32_t dn_tmp_96=((uint32_t)(0ULL));
size_t dn_tmp_97=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_95,dn_tmp_96,dn_tmp_97}); });return dn_tmp_98; }
}
else{
}
{ dn_r_CodecError dn_tmp_102=({ int32_t dn_tmp_99=((int32_t)(0ULL));
uint32_t dn_tmp_100=((uint32_t)(0ULL));
size_t dn_tmp_101=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_99,dn_tmp_100,dn_tmp_101}); });return dn_tmp_102; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_codec_plan_check(dn_r_CodecPlan const * v_plan) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_codec_plan_check(v_plan);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static size_t dn_f_codec_find(dn_r_CodecPlan const * v_plan,uint32_t v_field) {
{
if(({ uint32_t dn_tmp_0=(v_plan)->f_field_count;
uint32_t dn_tmp_1=((uint32_t)(32ULL));
(dn_tmp_0 > dn_tmp_1); })){
{ size_t dn_tmp_2=((size_t)(32ULL));return dn_tmp_2; }
}
else{
}
{ size_t dn_tmp_5=((size_t)(0ULL));size_t dn_tmp_4=({ uint32_t dn_tmp_3=(v_plan)->f_field_count;
if((unsigned __int128)dn_tmp_3>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_3; });for(size_t v_index=dn_tmp_5;v_index<dn_tmp_4;v_index++){
if(({ uint32_t dn_tmp_6=((*({ dn_t_arr32_r12_MessageField *dn_tmp_7=&((v_plan)->f_fields);size_t dn_tmp_8=v_index;&dn_tmp_7->data[dn_index(dn_tmp_8,32)]; }))).f_id;
uint32_t dn_tmp_9=v_field;
(dn_tmp_6 == dn_tmp_9); })){
{ size_t dn_tmp_10=v_index;return dn_tmp_10; }
}
else{
}
}
 }
{ size_t dn_tmp_12=({ uint32_t dn_tmp_11=(v_plan)->f_field_count;
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_11; });return dn_tmp_12; }
}

dn_fail("missing native return");
}
static size_t dn_cb_codec_find(dn_r_CodecPlan const * v_plan,uint32_t v_field) {
 if(!dn_enter()){return (size_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (size_t){0};}
 size_t dn_result=dn_f_codec_find(v_plan,v_field);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_codec_storage(dn_r_CodecPlan const * v_plan,size_t v_values,size_t v_bytes,size_t v_unknown) {
{
if((((({ size_t dn_tmp_0=(v_plan)->f_storage_budget;
size_t dn_tmp_1=((size_t)(256ULL));
(dn_tmp_0 < dn_tmp_1); }) || ({ size_t dn_tmp_2=v_values;
size_t dn_tmp_3=({ uint32_t dn_tmp_4=(v_plan)->f_maximum_values;
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_4; });
(dn_tmp_2 > dn_tmp_3); })) || ({ size_t dn_tmp_5=v_bytes;
size_t dn_tmp_6=(v_plan)->f_maximum_wire;
(dn_tmp_5 > dn_tmp_6); })) || ({ size_t dn_tmp_7=v_unknown;
size_t dn_tmp_8=(v_plan)->f_maximum_unknown_bytes;
(dn_tmp_7 > dn_tmp_8); }))){
{ bool dn_tmp_9=0;return dn_tmp_9; }
}
else{
}
size_t v_available=({ size_t dn_tmp_10=(v_plan)->f_storage_budget;
size_t dn_tmp_11=((size_t)(256ULL));
dn_USize_sub(dn_tmp_10,dn_tmp_11); });
if(({ size_t dn_tmp_12=v_values;
size_t dn_tmp_13=({ size_t dn_tmp_14=v_available;
size_t dn_tmp_15=((size_t)(32ULL));
dn_USize_div(dn_tmp_14,dn_tmp_15); });
(dn_tmp_12 > dn_tmp_13); })){
{ bool dn_tmp_16=0;return dn_tmp_16; }
}
else{
}
size_t v_used=({ size_t dn_tmp_17=v_values;
size_t dn_tmp_18=((size_t)(32ULL));
dn_USize_mul(dn_tmp_17,dn_tmp_18); });
if(({ size_t dn_tmp_19=v_bytes;
size_t dn_tmp_20=({ size_t dn_tmp_21=v_available;
size_t dn_tmp_22=v_used;
dn_USize_sub(dn_tmp_21,dn_tmp_22); });
(dn_tmp_19 > dn_tmp_20); })){
{ bool dn_tmp_23=0;return dn_tmp_23; }
}
else{
}
{ bool dn_tmp_30=({ size_t dn_tmp_24=v_unknown;
size_t dn_tmp_25=({ size_t dn_tmp_26=({ size_t dn_tmp_27=v_available;
size_t dn_tmp_28=v_used;
dn_USize_sub(dn_tmp_27,dn_tmp_28); });
size_t dn_tmp_29=v_bytes;
dn_USize_sub(dn_tmp_26,dn_tmp_29); });
(dn_tmp_24 <= dn_tmp_25); });return dn_tmp_30; }
}

dn_fail("missing native return");
}
static bool dn_cb_codec_storage(dn_r_CodecPlan const * v_plan,size_t v_values,size_t v_bytes,size_t v_unknown) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_codec_storage(v_plan,v_values,v_bytes,v_unknown);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_codec_scalar_valid(uint32_t v_kind,uint64_t v_value) {
{
if(({ uint32_t dn_tmp_0=v_kind;
uint32_t dn_tmp_1=((uint32_t)(1ULL));
(dn_tmp_0 == dn_tmp_1); })){
{ bool dn_tmp_4=({ uint64_t dn_tmp_2=v_value;
uint64_t dn_tmp_3=((uint64_t)(255ULL));
(dn_tmp_2 <= dn_tmp_3); });return dn_tmp_4; }
}
else{
}
if(({ uint32_t dn_tmp_5=v_kind;
uint32_t dn_tmp_6=((uint32_t)(2ULL));
(dn_tmp_5 == dn_tmp_6); })){
{ bool dn_tmp_9=({ uint64_t dn_tmp_7=v_value;
uint64_t dn_tmp_8=((uint64_t)(65535ULL));
(dn_tmp_7 <= dn_tmp_8); });return dn_tmp_9; }
}
else{
}
if(({ uint32_t dn_tmp_10=v_kind;
uint32_t dn_tmp_11=((uint32_t)(3ULL));
(dn_tmp_10 == dn_tmp_11); })){
{ bool dn_tmp_15=({ uint64_t dn_tmp_12=v_value;
uint64_t dn_tmp_13=({ uint64_t dn_tmp_14=((uint64_t)(4294967295ULL));
if((unsigned __int128)dn_tmp_14>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_14; });
(dn_tmp_12 <= dn_tmp_13); });return dn_tmp_15; }
}
else{
}
if(({ uint32_t dn_tmp_16=v_kind;
uint32_t dn_tmp_17=((uint32_t)(5ULL));
(dn_tmp_16 == dn_tmp_17); })){
{ bool dn_tmp_20=({ uint64_t dn_tmp_18=v_value;
uint64_t dn_tmp_19=((uint64_t)(1ULL));
(dn_tmp_18 <= dn_tmp_19); });return dn_tmp_20; }
}
else{
}
{ bool dn_tmp_27=((({ uint32_t dn_tmp_21=v_kind;
uint32_t dn_tmp_22=((uint32_t)(4ULL));
(dn_tmp_21 == dn_tmp_22); }) || ({ uint32_t dn_tmp_23=v_kind;
uint32_t dn_tmp_24=((uint32_t)(8ULL));
(dn_tmp_23 == dn_tmp_24); })) || ({ uint32_t dn_tmp_25=v_kind;
uint32_t dn_tmp_26=((uint32_t)(9ULL));
(dn_tmp_25 == dn_tmp_26); }));return dn_tmp_27; }
}

dn_fail("missing native return");
}
static bool dn_cb_codec_scalar_valid(uint32_t v_kind,uint64_t v_value) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_codec_scalar_valid(v_kind,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static size_t dn_f_codec_scalar_size(uint32_t v_kind,uint64_t v_value) {
{
if((({ uint32_t dn_tmp_0=v_kind;
uint32_t dn_tmp_1=((uint32_t)(1ULL));
(dn_tmp_0 == dn_tmp_1); }) || ({ uint32_t dn_tmp_2=v_kind;
uint32_t dn_tmp_3=((uint32_t)(5ULL));
(dn_tmp_2 == dn_tmp_3); }))){
{ size_t dn_tmp_4=((size_t)(1ULL));return dn_tmp_4; }
}
else{
}
if(({ uint32_t dn_tmp_5=v_kind;
uint32_t dn_tmp_6=((uint32_t)(2ULL));
(dn_tmp_5 == dn_tmp_6); })){
{ size_t dn_tmp_7=((size_t)(2ULL));return dn_tmp_7; }
}
else{
}
if(({ uint32_t dn_tmp_8=v_kind;
uint32_t dn_tmp_9=((uint32_t)(3ULL));
(dn_tmp_8 == dn_tmp_9); })){
{ size_t dn_tmp_10=((size_t)(4ULL));return dn_tmp_10; }
}
else{
}
if(({ uint32_t dn_tmp_11=v_kind;
uint32_t dn_tmp_12=((uint32_t)(4ULL));
(dn_tmp_11 == dn_tmp_12); })){
{ size_t dn_tmp_13=((size_t)(8ULL));return dn_tmp_13; }
}
else{
}
if((({ uint32_t dn_tmp_14=v_kind;
uint32_t dn_tmp_15=((uint32_t)(8ULL));
(dn_tmp_14 == dn_tmp_15); }) || ({ uint32_t dn_tmp_16=v_kind;
uint32_t dn_tmp_17=((uint32_t)(9ULL));
(dn_tmp_16 == dn_tmp_17); }))){
{ size_t dn_tmp_19=({ uint64_t dn_tmp_18=v_value;
dn_f_uleb128_size(dn_tmp_18); });return dn_tmp_19; }
}
else{
}
{ size_t dn_tmp_20=((size_t)(0ULL));return dn_tmp_20; }
}

dn_fail("missing native return");
}
static size_t dn_cb_codec_scalar_size(uint32_t v_kind,uint64_t v_value) {
 if(!dn_enter()){return (size_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (size_t){0};}
 size_t dn_result=dn_f_codec_scalar_size(v_kind,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_codec_utf8(dn_t_slice0_U8 v_input,size_t v_offset,size_t v_count) {
{
if((({ size_t dn_tmp_0=v_offset;
size_t dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_input;
(dn_tmp_2).len; });
(dn_tmp_0 > dn_tmp_1); }) || ({ size_t dn_tmp_3=v_count;
size_t dn_tmp_4=({ size_t dn_tmp_5=({ dn_t_slice0_U8 dn_tmp_6=v_input;
(dn_tmp_6).len; });
size_t dn_tmp_7=v_offset;
dn_USize_sub(dn_tmp_5,dn_tmp_7); });
(dn_tmp_3 > dn_tmp_4); }))){
{ bool dn_tmp_8=0;return dn_tmp_8; }
}
else{
}
size_t v_end=({ size_t dn_tmp_9=v_offset;
size_t dn_tmp_10=v_count;
dn_USize_add(dn_tmp_9,dn_tmp_10); });
size_t v_position=v_offset;
{ size_t dn_tmp_12=((size_t)(0ULL));size_t dn_tmp_11=v_count;for(size_t v_index=dn_tmp_12;v_index<dn_tmp_11;v_index++){
if(({ size_t dn_tmp_13=v_position;
size_t dn_tmp_14=v_end;
(dn_tmp_13 == dn_tmp_14); })){
{ bool dn_tmp_15=1;return dn_tmp_15; }
}
else{
}
dn_r_Utf8Step v_step=({ dn_t_slice0_U8 dn_tmp_16=v_input;
size_t dn_tmp_17=v_position;
dn_f_utf8_next(dn_tmp_16,dn_tmp_17); });
if(((!((v_step).f_valid)) || ({ size_t dn_tmp_18=(v_step).f_next;
size_t dn_tmp_19=v_end;
(dn_tmp_18 > dn_tmp_19); }))){
{ bool dn_tmp_20=0;return dn_tmp_20; }
}
else{
}
{ size_t dn_tmp_22=(v_step).f_next;size_t *dn_tmp_21=&(v_position);*dn_tmp_21=dn_tmp_22; }
}
 }
{ bool dn_tmp_25=({ size_t dn_tmp_23=v_position;
size_t dn_tmp_24=v_end;
(dn_tmp_23 == dn_tmp_24); });return dn_tmp_25; }
}

dn_fail("missing native return");
}
static bool dn_cb_codec_utf8(dn_t_slice0_U8 v_input,size_t v_offset,size_t v_count) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_codec_utf8(v_input,v_offset,v_count);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecRead dn_f_codec_read_error(int32_t v_kind,uint32_t v_field,size_t v_offset) {
{
{ dn_r_CodecRead dn_tmp_8=({ uint64_t dn_tmp_0=((uint64_t)(0ULL));
size_t dn_tmp_1=((size_t)(0ULL));
size_t dn_tmp_2=((size_t)(0ULL));
size_t dn_tmp_3=v_offset;
dn_r_CodecError dn_tmp_4=({ int32_t dn_tmp_5=v_kind;
uint32_t dn_tmp_6=v_field;
size_t dn_tmp_7=v_offset;
((dn_r_CodecError){dn_tmp_5,dn_tmp_6,dn_tmp_7}); });
((dn_r_CodecRead){dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4}); });return dn_tmp_8; }
}

dn_fail("missing native return");
}
static dn_r_CodecRead dn_cb_codec_read_error(int32_t v_kind,uint32_t v_field,size_t v_offset) {
 if(!dn_enter()){return (dn_r_CodecRead){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecRead){0};}
 dn_r_CodecRead dn_result=dn_f_codec_read_error(v_kind,v_field,v_offset);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_ProtocolValue dn_f_codec_read_integer(dn_r_CodecPlan const * v_plan,dn_t_slice0_U8 v_input,size_t v_offset,size_t v_width) {
{
if(({ uint32_t dn_tmp_0=((v_plan)->f_wire).f_endian;
uint32_t dn_tmp_1=((uint32_t)(1ULL));
(dn_tmp_0 == dn_tmp_1); })){
{ dn_r_ProtocolValue dn_tmp_5=({ dn_t_slice0_U8 dn_tmp_2=v_input;
size_t dn_tmp_3=v_offset;
size_t dn_tmp_4=v_width;
dn_f_net_read_be(dn_tmp_2,dn_tmp_3,dn_tmp_4); });return dn_tmp_5; }
}
else{
}
{ dn_r_ProtocolValue dn_tmp_9=({ dn_t_slice0_U8 dn_tmp_6=v_input;
size_t dn_tmp_7=v_offset;
size_t dn_tmp_8=v_width;
dn_f_net_read_le(dn_tmp_6,dn_tmp_7,dn_tmp_8); });return dn_tmp_9; }
}

dn_fail("missing native return");
}
static dn_r_ProtocolValue dn_cb_codec_read_integer(dn_r_CodecPlan const * v_plan,dn_t_slice0_U8 v_input,size_t v_offset,size_t v_width) {
 if(!dn_enter()){return (dn_r_ProtocolValue){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_ProtocolValue){0};}
 dn_r_ProtocolValue dn_result=dn_f_codec_read_integer(v_plan,v_input,v_offset,v_width);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecRead dn_f_codec_read_value(dn_r_CodecPlan const * v_plan,dn_r_MessageField const * v_field,dn_t_slice0_U8 v_input,size_t v_offset,size_t v_end) {
{
if((({ size_t dn_tmp_0=v_offset;
size_t dn_tmp_1=v_end;
(dn_tmp_0 > dn_tmp_1); }) || ({ size_t dn_tmp_2=v_end;
size_t dn_tmp_3=({ dn_t_slice0_U8 dn_tmp_4=v_input;
(dn_tmp_4).len; });
(dn_tmp_2 > dn_tmp_3); }))){
{ dn_r_CodecRead dn_tmp_8=({ int32_t dn_tmp_5=((int32_t)(4ULL));
uint32_t dn_tmp_6=(v_field)->f_id;
size_t dn_tmp_7=v_offset;
dn_f_codec_read_error(dn_tmp_5,dn_tmp_6,dn_tmp_7); });return dn_tmp_8; }
}
else{
}
size_t v_position=v_offset;
if((({ uint32_t dn_tmp_9=(v_field)->f_kind;
uint32_t dn_tmp_10=((uint32_t)(6ULL));
(dn_tmp_9 == dn_tmp_10); }) || ({ uint32_t dn_tmp_11=(v_field)->f_kind;
uint32_t dn_tmp_12=((uint32_t)(7ULL));
(dn_tmp_11 == dn_tmp_12); }))){
size_t v_count=({ size_t dn_tmp_13=v_end;
size_t dn_tmp_14=v_position;
dn_USize_sub(dn_tmp_13,dn_tmp_14); });
if(({ uint32_t dn_tmp_15=((v_plan)->f_wire).f_kind;
uint32_t dn_tmp_16=((uint32_t)(1ULL));
(dn_tmp_15 == dn_tmp_16); })){
if(({ size_t dn_tmp_17=({ size_t dn_tmp_18=v_end;
size_t dn_tmp_19=v_position;
dn_USize_sub(dn_tmp_18,dn_tmp_19); });
size_t dn_tmp_20=((size_t)(4ULL));
(dn_tmp_17 < dn_tmp_20); })){
{ dn_r_CodecRead dn_tmp_24=({ int32_t dn_tmp_21=((int32_t)(1ULL));
uint32_t dn_tmp_22=(v_field)->f_id;
size_t dn_tmp_23=v_position;
dn_f_codec_read_error(dn_tmp_21,dn_tmp_22,dn_tmp_23); });return dn_tmp_24; }
}
else{
}
dn_r_ProtocolValue v_size=({ dn_r_CodecPlan const * dn_tmp_25=v_plan;
dn_t_slice0_U8 dn_tmp_26=v_input;
size_t dn_tmp_27=v_position;
size_t dn_tmp_28=((size_t)(4ULL));
dn_f_codec_read_integer(dn_tmp_25,dn_tmp_26,dn_tmp_27,dn_tmp_28); });
{ size_t dn_tmp_32=({ size_t dn_tmp_29=v_position;
size_t dn_tmp_30=((size_t)(4ULL));
dn_USize_add(dn_tmp_29,dn_tmp_30); });size_t *dn_tmp_31=&(v_position);*dn_tmp_31=dn_tmp_32; }
{ size_t dn_tmp_35=({ uint64_t dn_tmp_33=(v_size).f_value;
if((unsigned __int128)dn_tmp_33>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_33; });size_t *dn_tmp_34=&(v_count);*dn_tmp_34=dn_tmp_35; }
if(({ size_t dn_tmp_36=v_count;
size_t dn_tmp_37=({ uint32_t dn_tmp_38=(v_field)->f_maximum_length;
if((unsigned __int128)dn_tmp_38>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_38; });
(dn_tmp_36 > dn_tmp_37); })){
{ dn_r_CodecRead dn_tmp_42=({ int32_t dn_tmp_39=((int32_t)(7ULL));
uint32_t dn_tmp_40=(v_field)->f_id;
size_t dn_tmp_41=v_offset;
dn_f_codec_read_error(dn_tmp_39,dn_tmp_40,dn_tmp_41); });return dn_tmp_42; }
}
else{
}
if(({ size_t dn_tmp_43=v_count;
size_t dn_tmp_44=({ size_t dn_tmp_45=v_end;
size_t dn_tmp_46=v_position;
dn_USize_sub(dn_tmp_45,dn_tmp_46); });
(dn_tmp_43 > dn_tmp_44); })){
{ dn_r_CodecRead dn_tmp_50=({ int32_t dn_tmp_47=((int32_t)(1ULL));
uint32_t dn_tmp_48=(v_field)->f_id;
size_t dn_tmp_49=v_offset;
dn_f_codec_read_error(dn_tmp_47,dn_tmp_48,dn_tmp_49); });return dn_tmp_50; }
}
else{
}
}
else{
}
if(({ size_t dn_tmp_51=v_count;
size_t dn_tmp_52=({ uint32_t dn_tmp_53=(v_field)->f_maximum_length;
if((unsigned __int128)dn_tmp_53>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_53; });
(dn_tmp_51 > dn_tmp_52); })){
{ dn_r_CodecRead dn_tmp_57=({ int32_t dn_tmp_54=((int32_t)(7ULL));
uint32_t dn_tmp_55=(v_field)->f_id;
size_t dn_tmp_56=v_offset;
dn_f_codec_read_error(dn_tmp_54,dn_tmp_55,dn_tmp_56); });return dn_tmp_57; }
}
else{
}
if((({ uint32_t dn_tmp_58=(v_field)->f_kind;
uint32_t dn_tmp_59=((uint32_t)(7ULL));
(dn_tmp_58 == dn_tmp_59); }) && (!(({ dn_t_slice0_U8 dn_tmp_60=v_input;
size_t dn_tmp_61=v_position;
size_t dn_tmp_62=v_count;
dn_f_codec_utf8(dn_tmp_60,dn_tmp_61,dn_tmp_62); }))))){
{ dn_r_CodecRead dn_tmp_66=({ int32_t dn_tmp_63=((int32_t)(4ULL));
uint32_t dn_tmp_64=(v_field)->f_id;
size_t dn_tmp_65=v_position;
dn_f_codec_read_error(dn_tmp_63,dn_tmp_64,dn_tmp_65); });return dn_tmp_66; }
}
else{
}
{ dn_r_CodecRead dn_tmp_77=({ uint64_t dn_tmp_67=((uint64_t)(0ULL));
size_t dn_tmp_68=v_position;
size_t dn_tmp_69=v_count;
size_t dn_tmp_70=({ size_t dn_tmp_71=v_position;
size_t dn_tmp_72=v_count;
dn_USize_add(dn_tmp_71,dn_tmp_72); });
dn_r_CodecError dn_tmp_73=({ int32_t dn_tmp_74=((int32_t)(0ULL));
uint32_t dn_tmp_75=((uint32_t)(0ULL));
size_t dn_tmp_76=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_74,dn_tmp_75,dn_tmp_76}); });
((dn_r_CodecRead){dn_tmp_67,dn_tmp_68,dn_tmp_69,dn_tmp_70,dn_tmp_73}); });return dn_tmp_77; }
}
else{
}
if((({ uint32_t dn_tmp_78=(v_field)->f_kind;
uint32_t dn_tmp_79=((uint32_t)(8ULL));
(dn_tmp_78 == dn_tmp_79); }) || ({ uint32_t dn_tmp_80=(v_field)->f_kind;
uint32_t dn_tmp_81=((uint32_t)(9ULL));
(dn_tmp_80 == dn_tmp_81); }))){
if((({ uint32_t dn_tmp_82=((v_plan)->f_wire).f_kind;
uint32_t dn_tmp_83=((uint32_t)(2ULL));
(dn_tmp_82 == dn_tmp_83); }) && (({ size_t dn_tmp_84=({ size_t dn_tmp_85=v_end;
size_t dn_tmp_86=v_position;
dn_USize_sub(dn_tmp_85,dn_tmp_86); });
size_t dn_tmp_87=((size_t)(0ULL));
(dn_tmp_84 == dn_tmp_87); }) || ({ size_t dn_tmp_88=({ size_t dn_tmp_89=v_end;
size_t dn_tmp_90=v_position;
dn_USize_sub(dn_tmp_89,dn_tmp_90); });
size_t dn_tmp_91=((size_t)(10ULL));
(dn_tmp_88 > dn_tmp_91); })))){
{ dn_r_CodecRead dn_tmp_95=({ int32_t dn_tmp_92=((int32_t)(4ULL));
uint32_t dn_tmp_93=(v_field)->f_id;
size_t dn_tmp_94=v_offset;
dn_f_codec_read_error(dn_tmp_92,dn_tmp_93,dn_tmp_94); });return dn_tmp_95; }
}
else{
}
dn_r_ProtocolValue v_value=({ dn_t_slice0_U8 dn_tmp_96=v_input;
size_t dn_tmp_97=v_position;
bool dn_tmp_98=1;
dn_f_uleb128_read(dn_tmp_96,dn_tmp_97,dn_tmp_98); });
if(({ int32_t dn_tmp_99=(v_value).f_status;
int32_t dn_tmp_100=((int32_t)(0ULL));
(dn_tmp_99 != dn_tmp_100); })){
int32_t v_error=(v_value).f_status;
if(({ uint32_t dn_tmp_101=((v_plan)->f_wire).f_kind;
uint32_t dn_tmp_102=((uint32_t)(2ULL));
(dn_tmp_101 == dn_tmp_102); })){
{ int32_t dn_tmp_104=((int32_t)(4ULL));int32_t *dn_tmp_103=&(v_error);*dn_tmp_103=dn_tmp_104; }
}
else{
}
{ dn_r_CodecRead dn_tmp_108=({ int32_t dn_tmp_105=v_error;
uint32_t dn_tmp_106=(v_field)->f_id;
size_t dn_tmp_107=v_offset;
dn_f_codec_read_error(dn_tmp_105,dn_tmp_106,dn_tmp_107); });return dn_tmp_108; }
}
else{
}
if((({ size_t dn_tmp_109=(v_value).f_next;
size_t dn_tmp_110=v_end;
(dn_tmp_109 > dn_tmp_110); }) || (({ uint32_t dn_tmp_111=((v_plan)->f_wire).f_kind;
uint32_t dn_tmp_112=((uint32_t)(2ULL));
(dn_tmp_111 == dn_tmp_112); }) && ({ size_t dn_tmp_113=(v_value).f_next;
size_t dn_tmp_114=v_end;
(dn_tmp_113 != dn_tmp_114); })))){
{ dn_r_CodecRead dn_tmp_118=({ int32_t dn_tmp_115=((int32_t)(4ULL));
uint32_t dn_tmp_116=(v_field)->f_id;
size_t dn_tmp_117=v_offset;
dn_f_codec_read_error(dn_tmp_115,dn_tmp_116,dn_tmp_117); });return dn_tmp_118; }
}
else{
}
{ dn_r_CodecRead dn_tmp_127=({ uint64_t dn_tmp_119=(v_value).f_value;
size_t dn_tmp_120=((size_t)(0ULL));
size_t dn_tmp_121=((size_t)(0ULL));
size_t dn_tmp_122=(v_value).f_next;
dn_r_CodecError dn_tmp_123=({ int32_t dn_tmp_124=((int32_t)(0ULL));
uint32_t dn_tmp_125=((uint32_t)(0ULL));
size_t dn_tmp_126=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_124,dn_tmp_125,dn_tmp_126}); });
((dn_r_CodecRead){dn_tmp_119,dn_tmp_120,dn_tmp_121,dn_tmp_122,dn_tmp_123}); });return dn_tmp_127; }
}
else{
}
size_t v_width=({ uint32_t dn_tmp_128=(v_field)->f_kind;
uint64_t dn_tmp_129=((uint64_t)(0ULL));
dn_f_codec_scalar_size(dn_tmp_128,dn_tmp_129); });
if(({ size_t dn_tmp_130=v_width;
size_t dn_tmp_131=((size_t)(0ULL));
(dn_tmp_130 == dn_tmp_131); })){
{ dn_r_CodecRead dn_tmp_135=({ int32_t dn_tmp_132=((int32_t)(4ULL));
uint32_t dn_tmp_133=(v_field)->f_id;
size_t dn_tmp_134=v_offset;
dn_f_codec_read_error(dn_tmp_132,dn_tmp_133,dn_tmp_134); });return dn_tmp_135; }
}
else{
}
if((({ uint32_t dn_tmp_136=((v_plan)->f_wire).f_kind;
uint32_t dn_tmp_137=((uint32_t)(2ULL));
(dn_tmp_136 == dn_tmp_137); }) && ({ size_t dn_tmp_138=({ size_t dn_tmp_139=v_end;
size_t dn_tmp_140=v_position;
dn_USize_sub(dn_tmp_139,dn_tmp_140); });
size_t dn_tmp_141=v_width;
(dn_tmp_138 != dn_tmp_141); }))){
{ dn_r_CodecRead dn_tmp_145=({ int32_t dn_tmp_142=((int32_t)(4ULL));
uint32_t dn_tmp_143=(v_field)->f_id;
size_t dn_tmp_144=v_offset;
dn_f_codec_read_error(dn_tmp_142,dn_tmp_143,dn_tmp_144); });return dn_tmp_145; }
}
else{
}
if(({ size_t dn_tmp_146=v_width;
size_t dn_tmp_147=({ size_t dn_tmp_148=v_end;
size_t dn_tmp_149=v_position;
dn_USize_sub(dn_tmp_148,dn_tmp_149); });
(dn_tmp_146 > dn_tmp_147); })){
{ dn_r_CodecRead dn_tmp_153=({ int32_t dn_tmp_150=((int32_t)(1ULL));
uint32_t dn_tmp_151=(v_field)->f_id;
size_t dn_tmp_152=v_offset;
dn_f_codec_read_error(dn_tmp_150,dn_tmp_151,dn_tmp_152); });return dn_tmp_153; }
}
else{
}
dn_r_ProtocolValue v_value=({ dn_r_CodecPlan const * dn_tmp_154=v_plan;
dn_t_slice0_U8 dn_tmp_155=v_input;
size_t dn_tmp_156=v_position;
size_t dn_tmp_157=v_width;
dn_f_codec_read_integer(dn_tmp_154,dn_tmp_155,dn_tmp_156,dn_tmp_157); });
if((!(({ uint32_t dn_tmp_158=(v_field)->f_kind;
uint64_t dn_tmp_159=(v_value).f_value;
dn_f_codec_scalar_valid(dn_tmp_158,dn_tmp_159); })))){
{ dn_r_CodecRead dn_tmp_163=({ int32_t dn_tmp_160=((int32_t)(4ULL));
uint32_t dn_tmp_161=(v_field)->f_id;
size_t dn_tmp_162=v_offset;
dn_f_codec_read_error(dn_tmp_160,dn_tmp_161,dn_tmp_162); });return dn_tmp_163; }
}
else{
}
{ dn_r_CodecRead dn_tmp_174=({ uint64_t dn_tmp_164=(v_value).f_value;
size_t dn_tmp_165=((size_t)(0ULL));
size_t dn_tmp_166=((size_t)(0ULL));
size_t dn_tmp_167=({ size_t dn_tmp_168=v_position;
size_t dn_tmp_169=v_width;
dn_USize_add(dn_tmp_168,dn_tmp_169); });
dn_r_CodecError dn_tmp_170=({ int32_t dn_tmp_171=((int32_t)(0ULL));
uint32_t dn_tmp_172=((uint32_t)(0ULL));
size_t dn_tmp_173=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_171,dn_tmp_172,dn_tmp_173}); });
((dn_r_CodecRead){dn_tmp_164,dn_tmp_165,dn_tmp_166,dn_tmp_167,dn_tmp_170}); });return dn_tmp_174; }
}

dn_fail("missing native return");
}
static dn_r_CodecRead dn_cb_codec_read_value(dn_r_CodecPlan const * v_plan,dn_r_MessageField const * v_field,dn_t_slice0_U8 v_input,size_t v_offset,size_t v_end) {
 if(!dn_enter()){return (dn_r_CodecRead){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecRead){0};}
 dn_r_CodecRead dn_result=dn_f_codec_read_value(v_plan,v_field,v_input,v_offset,v_end);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecShape dn_f_codec_unknown_shape(dn_r_CodecPlan const * v_plan,dn_t_slice0_U8 v_input) {
{
dn_r_CodecShape v_result=({ ((dn_r_CodecShape){0}); });
if(({ size_t dn_tmp_0=({ dn_t_slice0_U8 dn_tmp_1=v_input;
(dn_tmp_1).len; });
size_t dn_tmp_2=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_2); })){
{ dn_r_CodecShape dn_tmp_3=v_result;return dn_tmp_3; }
}
else{
}
if((({ uint32_t dn_tmp_4=((v_plan)->f_wire).f_kind;
uint32_t dn_tmp_5=((uint32_t)(2ULL));
(dn_tmp_4 != dn_tmp_5); }) || ({ uint32_t dn_tmp_6=((v_plan)->f_wire).f_unknown_fields;
uint32_t dn_tmp_7=((uint32_t)(1ULL));
(dn_tmp_6 != dn_tmp_7); }))){
{ dn_r_CodecShape dn_tmp_11=({ int32_t dn_tmp_8=((int32_t)(4ULL));
uint32_t dn_tmp_9=((uint32_t)(0ULL));
size_t dn_tmp_10=((size_t)(0ULL));
dn_f_codec_shape_error(dn_tmp_8,dn_tmp_9,dn_tmp_10); });return dn_tmp_11; }
}
else{
}
if(({ size_t dn_tmp_12=({ dn_t_slice0_U8 dn_tmp_13=v_input;
(dn_tmp_13).len; });
size_t dn_tmp_14=(v_plan)->f_maximum_unknown_bytes;
(dn_tmp_12 > dn_tmp_14); })){
{ dn_r_CodecShape dn_tmp_18=({ int32_t dn_tmp_15=((int32_t)(7ULL));
uint32_t dn_tmp_16=((uint32_t)(0ULL));
size_t dn_tmp_17=((size_t)(0ULL));
dn_f_codec_shape_error(dn_tmp_15,dn_tmp_16,dn_tmp_17); });return dn_tmp_18; }
}
else{
}
size_t v_position=((size_t)(0ULL));
{ size_t dn_tmp_25=((size_t)(0ULL));size_t dn_tmp_24=({ size_t dn_tmp_19=({ size_t dn_tmp_20=({ dn_t_slice0_U8 dn_tmp_21=v_input;
(dn_tmp_21).len; });
size_t dn_tmp_22=((size_t)(8ULL));
dn_USize_div(dn_tmp_20,dn_tmp_22); });
size_t dn_tmp_23=((size_t)(1ULL));
dn_USize_add(dn_tmp_19,dn_tmp_23); });for(size_t v_item=dn_tmp_25;v_item<dn_tmp_24;v_item++){
if(({ size_t dn_tmp_26=v_position;
size_t dn_tmp_27=({ dn_t_slice0_U8 dn_tmp_28=v_input;
(dn_tmp_28).len; });
(dn_tmp_26 == dn_tmp_27); })){
break;
}
else{
}
if(({ size_t dn_tmp_29=({ size_t dn_tmp_30=({ dn_t_slice0_U8 dn_tmp_31=v_input;
(dn_tmp_31).len; });
size_t dn_tmp_32=v_position;
dn_USize_sub(dn_tmp_30,dn_tmp_32); });
size_t dn_tmp_33=((size_t)(8ULL));
(dn_tmp_29 < dn_tmp_33); })){
{ dn_r_CodecShape dn_tmp_37=({ int32_t dn_tmp_34=((int32_t)(4ULL));
uint32_t dn_tmp_35=((uint32_t)(0ULL));
size_t dn_tmp_36=v_position;
dn_f_codec_shape_error(dn_tmp_34,dn_tmp_35,dn_tmp_36); });return dn_tmp_37; }
}
else{
}
dn_r_ProtocolValue v_id=({ dn_t_slice0_U8 dn_tmp_38=v_input;
size_t dn_tmp_39=v_position;
size_t dn_tmp_40=((size_t)(4ULL));
dn_f_net_read_le(dn_tmp_38,dn_tmp_39,dn_tmp_40); });
dn_r_ProtocolValue v_size=({ dn_t_slice0_U8 dn_tmp_41=v_input;
size_t dn_tmp_42=({ size_t dn_tmp_43=v_position;
size_t dn_tmp_44=((size_t)(4ULL));
dn_USize_add(dn_tmp_43,dn_tmp_44); });
size_t dn_tmp_45=((size_t)(4ULL));
dn_f_net_read_le(dn_tmp_41,dn_tmp_42,dn_tmp_45); });
uint32_t v_field=({ uint64_t dn_tmp_46=(v_id).f_value;
if((unsigned __int128)dn_tmp_46>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_46; });
size_t v_count=({ uint64_t dn_tmp_47=(v_size).f_value;
if((unsigned __int128)dn_tmp_47>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_47; });
if((({ size_t dn_tmp_48=({ dn_r_CodecPlan const * dn_tmp_49=v_plan;
uint32_t dn_tmp_50=v_field;
dn_f_codec_find(dn_tmp_49,dn_tmp_50); });
size_t dn_tmp_51=({ uint32_t dn_tmp_52=(v_plan)->f_field_count;
if((unsigned __int128)dn_tmp_52>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_52; });
(dn_tmp_48 != dn_tmp_51); }) || ({ size_t dn_tmp_53=v_count;
size_t dn_tmp_54=({ size_t dn_tmp_55=({ size_t dn_tmp_56=({ dn_t_slice0_U8 dn_tmp_57=v_input;
(dn_tmp_57).len; });
size_t dn_tmp_58=v_position;
dn_USize_sub(dn_tmp_56,dn_tmp_58); });
size_t dn_tmp_59=((size_t)(8ULL));
dn_USize_sub(dn_tmp_55,dn_tmp_59); });
(dn_tmp_53 > dn_tmp_54); }))){
{ dn_r_CodecShape dn_tmp_63=({ int32_t dn_tmp_60=((int32_t)(4ULL));
uint32_t dn_tmp_61=v_field;
size_t dn_tmp_62=v_position;
dn_f_codec_shape_error(dn_tmp_60,dn_tmp_61,dn_tmp_62); });return dn_tmp_63; }
}
else{
}
{ uint32_t dn_tmp_67=({ uint32_t dn_tmp_64=(v_result).f_unknown_fields;
uint32_t dn_tmp_65=((uint32_t)(1ULL));
dn_U32_add(dn_tmp_64,dn_tmp_65); });uint32_t *dn_tmp_66=&((v_result).f_unknown_fields);*dn_tmp_66=dn_tmp_67; }
if(({ uint32_t dn_tmp_68=(v_result).f_unknown_fields;
uint32_t dn_tmp_69=(v_plan)->f_maximum_unknown_fields;
(dn_tmp_68 > dn_tmp_69); })){
{ dn_r_CodecShape dn_tmp_73=({ int32_t dn_tmp_70=((int32_t)(7ULL));
uint32_t dn_tmp_71=v_field;
size_t dn_tmp_72=v_position;
dn_f_codec_shape_error(dn_tmp_70,dn_tmp_71,dn_tmp_72); });return dn_tmp_73; }
}
else{
}
{ size_t dn_tmp_79=({ size_t dn_tmp_74=({ size_t dn_tmp_75=v_position;
size_t dn_tmp_76=((size_t)(8ULL));
dn_USize_add(dn_tmp_75,dn_tmp_76); });
size_t dn_tmp_77=v_count;
dn_USize_add(dn_tmp_74,dn_tmp_77); });size_t *dn_tmp_78=&(v_position);*dn_tmp_78=dn_tmp_79; }
}
 }
{ size_t dn_tmp_82=({ dn_t_slice0_U8 dn_tmp_80=v_input;
(dn_tmp_80).len; });size_t *dn_tmp_81=&((v_result).f_unknown);*dn_tmp_81=dn_tmp_82; }
{ dn_r_CodecShape dn_tmp_83=v_result;return dn_tmp_83; }
}

dn_fail("missing native return");
}
static dn_r_CodecShape dn_cb_codec_unknown_shape(dn_r_CodecPlan const * v_plan,dn_t_slice0_U8 v_input) {
 if(!dn_enter()){return (dn_r_CodecShape){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecShape){0};}
 dn_r_CodecShape dn_result=dn_f_codec_unknown_shape(v_plan,v_input);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_codec_shape_complete(dn_r_CodecPlan const * v_plan,dn_r_CodecShape const * v_shape) {
{
{ size_t dn_tmp_2=((size_t)(0ULL));size_t dn_tmp_1=({ uint32_t dn_tmp_0=(v_plan)->f_field_count;
if((unsigned __int128)dn_tmp_0>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_0; });for(size_t v_index=dn_tmp_2;v_index<dn_tmp_1;v_index++){
if(({ uint32_t dn_tmp_3=(*({ dn_t_arr32_U32 *dn_tmp_4=&((v_shape)->f_counts);size_t dn_tmp_5=v_index;&dn_tmp_4->data[dn_index(dn_tmp_5,32)]; }));
uint32_t dn_tmp_6=((*({ dn_t_arr32_r12_MessageField *dn_tmp_7=&((v_plan)->f_fields);size_t dn_tmp_8=v_index;&dn_tmp_7->data[dn_index(dn_tmp_8,32)]; }))).f_minimum_count;
(dn_tmp_3 < dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_14=({ int32_t dn_tmp_9=((int32_t)(4ULL));
uint32_t dn_tmp_10=((*({ dn_t_arr32_r12_MessageField *dn_tmp_11=&((v_plan)->f_fields);size_t dn_tmp_12=v_index;&dn_tmp_11->data[dn_index(dn_tmp_12,32)]; }))).f_id;
size_t dn_tmp_13=(v_shape)->f_wire;
((dn_r_CodecError){dn_tmp_9,dn_tmp_10,dn_tmp_13}); });return dn_tmp_14; }
}
else{
}
}
 }
if((!(({ dn_r_CodecPlan const * dn_tmp_15=v_plan;
size_t dn_tmp_16=(v_shape)->f_values;
size_t dn_tmp_17=(v_shape)->f_bytes;
size_t dn_tmp_18=(v_shape)->f_unknown;
dn_f_codec_storage(dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_18); })))){
{ dn_r_CodecError dn_tmp_22=({ int32_t dn_tmp_19=((int32_t)(7ULL));
uint32_t dn_tmp_20=((uint32_t)(0ULL));
size_t dn_tmp_21=(v_shape)->f_wire;
((dn_r_CodecError){dn_tmp_19,dn_tmp_20,dn_tmp_21}); });return dn_tmp_22; }
}
else{
}
{ dn_r_CodecError dn_tmp_26=({ int32_t dn_tmp_23=((int32_t)(0ULL));
uint32_t dn_tmp_24=((uint32_t)(0ULL));
size_t dn_tmp_25=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_23,dn_tmp_24,dn_tmp_25}); });return dn_tmp_26; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_codec_shape_complete(dn_r_CodecPlan const * v_plan,dn_r_CodecShape const * v_shape) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_codec_shape_complete(v_plan,v_shape);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecShape dn_f_codec_scan(dn_r_CodecPlan const * v_plan,dn_t_slice0_U8 v_input) {
{
dn_r_CodecError v_checked=({ dn_r_CodecPlan const * dn_tmp_0=v_plan;
dn_f_codec_plan_check(dn_tmp_0); });
if(({ int32_t dn_tmp_1=(v_checked).f_kind;
int32_t dn_tmp_2=((int32_t)(0ULL));
(dn_tmp_1 != dn_tmp_2); })){
{ dn_r_CodecShape dn_tmp_6=({ int32_t dn_tmp_3=(v_checked).f_kind;
uint32_t dn_tmp_4=(v_checked).f_field;
size_t dn_tmp_5=(v_checked).f_offset;
dn_f_codec_shape_error(dn_tmp_3,dn_tmp_4,dn_tmp_5); });return dn_tmp_6; }
}
else{
}
if(({ size_t dn_tmp_7=({ dn_t_slice0_U8 dn_tmp_8=v_input;
(dn_tmp_8).len; });
size_t dn_tmp_9=(v_plan)->f_maximum_wire;
(dn_tmp_7 > dn_tmp_9); })){
{ dn_r_CodecShape dn_tmp_13=({ int32_t dn_tmp_10=((int32_t)(7ULL));
uint32_t dn_tmp_11=((uint32_t)(0ULL));
size_t dn_tmp_12=((size_t)(0ULL));
dn_f_codec_shape_error(dn_tmp_10,dn_tmp_11,dn_tmp_12); });return dn_tmp_13; }
}
else{
}
if(({ size_t dn_tmp_14=({ dn_t_slice0_U8 dn_tmp_15=v_input;
(dn_tmp_15).len; });
size_t dn_tmp_16=((size_t)(4ULL));
(dn_tmp_14 < dn_tmp_16); })){
{ dn_r_CodecShape dn_tmp_20=({ int32_t dn_tmp_17=((int32_t)(1ULL));
uint32_t dn_tmp_18=((uint32_t)(0ULL));
size_t dn_tmp_19=((size_t)(0ULL));
dn_f_codec_shape_error(dn_tmp_17,dn_tmp_18,dn_tmp_19); });return dn_tmp_20; }
}
else{
}
dn_r_ProtocolValue v_id=({ dn_t_slice0_U8 dn_tmp_21=v_input;
size_t dn_tmp_22=((size_t)(0ULL));
size_t dn_tmp_23=((size_t)(4ULL));
dn_f_net_read_be(dn_tmp_21,dn_tmp_22,dn_tmp_23); });
if(({ uint64_t dn_tmp_24=(v_id).f_value;
uint64_t dn_tmp_25=({ uint32_t dn_tmp_26=(v_plan)->f_message;
if((unsigned __int128)dn_tmp_26>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_26; });
(dn_tmp_24 != dn_tmp_25); })){
{ dn_r_CodecShape dn_tmp_30=({ int32_t dn_tmp_27=((int32_t)(11ULL));
uint32_t dn_tmp_28=((uint32_t)(0ULL));
size_t dn_tmp_29=((size_t)(0ULL));
dn_f_codec_shape_error(dn_tmp_27,dn_tmp_28,dn_tmp_29); });return dn_tmp_30; }
}
else{
}
dn_r_CodecShape v_result=({ ((dn_r_CodecShape){0}); });
{ size_t dn_tmp_33=({ dn_t_slice0_U8 dn_tmp_31=v_input;
(dn_tmp_31).len; });size_t *dn_tmp_32=&((v_result).f_wire);*dn_tmp_32=dn_tmp_33; }
size_t v_position=((size_t)(4ULL));
if(({ uint32_t dn_tmp_34=((v_plan)->f_wire).f_kind;
uint32_t dn_tmp_35=((uint32_t)(1ULL));
(dn_tmp_34 == dn_tmp_35); })){
{ size_t dn_tmp_38=((size_t)(0ULL));size_t dn_tmp_37=({ uint32_t dn_tmp_36=(v_plan)->f_field_count;
if((unsigned __int128)dn_tmp_36>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_36; });for(size_t v_index=dn_tmp_38;v_index<dn_tmp_37;v_index++){
dn_r_MessageField const * v_field=(&((*({ dn_t_arr32_r12_MessageField *dn_tmp_39=&((v_plan)->f_fields);size_t dn_tmp_40=v_index;&dn_tmp_39->data[dn_index(dn_tmp_40,32)]; }))));
uint32_t v_count=((uint32_t)(1ULL));
if((({ uint32_t dn_tmp_41=(v_field)->f_minimum_count;
uint32_t dn_tmp_42=((uint32_t)(1ULL));
(dn_tmp_41 != dn_tmp_42); }) || ({ uint32_t dn_tmp_43=(v_field)->f_maximum_count;
uint32_t dn_tmp_44=((uint32_t)(1ULL));
(dn_tmp_43 != dn_tmp_44); }))){
if(({ size_t dn_tmp_45=({ size_t dn_tmp_46=({ dn_t_slice0_U8 dn_tmp_47=v_input;
(dn_tmp_47).len; });
size_t dn_tmp_48=v_position;
dn_USize_sub(dn_tmp_46,dn_tmp_48); });
size_t dn_tmp_49=((size_t)(4ULL));
(dn_tmp_45 < dn_tmp_49); })){
{ dn_r_CodecShape dn_tmp_53=({ int32_t dn_tmp_50=((int32_t)(1ULL));
uint32_t dn_tmp_51=(v_field)->f_id;
size_t dn_tmp_52=v_position;
dn_f_codec_shape_error(dn_tmp_50,dn_tmp_51,dn_tmp_52); });return dn_tmp_53; }
}
else{
}
dn_r_ProtocolValue v_n=({ dn_r_CodecPlan const * dn_tmp_54=v_plan;
dn_t_slice0_U8 dn_tmp_55=v_input;
size_t dn_tmp_56=v_position;
size_t dn_tmp_57=((size_t)(4ULL));
dn_f_codec_read_integer(dn_tmp_54,dn_tmp_55,dn_tmp_56,dn_tmp_57); });
{ uint32_t dn_tmp_60=({ uint64_t dn_tmp_58=(v_n).f_value;
if((unsigned __int128)dn_tmp_58>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_58; });uint32_t *dn_tmp_59=&(v_count);*dn_tmp_59=dn_tmp_60; }
{ size_t dn_tmp_64=({ size_t dn_tmp_61=v_position;
size_t dn_tmp_62=((size_t)(4ULL));
dn_USize_add(dn_tmp_61,dn_tmp_62); });size_t *dn_tmp_63=&(v_position);*dn_tmp_63=dn_tmp_64; }
}
else{
}
if(({ uint32_t dn_tmp_65=v_count;
uint32_t dn_tmp_66=(v_field)->f_minimum_count;
(dn_tmp_65 < dn_tmp_66); })){
{ dn_r_CodecShape dn_tmp_70=({ int32_t dn_tmp_67=((int32_t)(4ULL));
uint32_t dn_tmp_68=(v_field)->f_id;
size_t dn_tmp_69=v_position;
dn_f_codec_shape_error(dn_tmp_67,dn_tmp_68,dn_tmp_69); });return dn_tmp_70; }
}
else{
}
if(({ uint32_t dn_tmp_71=v_count;
uint32_t dn_tmp_72=(v_field)->f_maximum_count;
(dn_tmp_71 > dn_tmp_72); })){
{ dn_r_CodecShape dn_tmp_76=({ int32_t dn_tmp_73=((int32_t)(7ULL));
uint32_t dn_tmp_74=(v_field)->f_id;
size_t dn_tmp_75=v_position;
dn_f_codec_shape_error(dn_tmp_73,dn_tmp_74,dn_tmp_75); });return dn_tmp_76; }
}
else{
}
{ uint32_t dn_tmp_80=v_count;uint32_t *dn_tmp_79=&((*({ dn_t_arr32_U32 *dn_tmp_77=&((v_result).f_counts);size_t dn_tmp_78=v_index;&dn_tmp_77->data[dn_index(dn_tmp_78,32)]; })));*dn_tmp_79=dn_tmp_80; }
{ size_t dn_tmp_85=({ size_t dn_tmp_81=(v_result).f_values;
size_t dn_tmp_82=({ uint32_t dn_tmp_83=v_count;
if((unsigned __int128)dn_tmp_83>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_83; });
dn_USize_add(dn_tmp_81,dn_tmp_82); });size_t *dn_tmp_84=&((v_result).f_values);*dn_tmp_84=dn_tmp_85; }
{ size_t dn_tmp_88=((size_t)(0ULL));size_t dn_tmp_87=({ uint32_t dn_tmp_86=v_count;
if((unsigned __int128)dn_tmp_86>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_86; });for(size_t v_occurrence=dn_tmp_88;v_occurrence<dn_tmp_87;v_occurrence++){
dn_r_CodecRead v_value=({ dn_r_CodecPlan const * dn_tmp_89=v_plan;
dn_r_MessageField const * dn_tmp_90=v_field;
dn_t_slice0_U8 dn_tmp_91=v_input;
size_t dn_tmp_92=v_position;
size_t dn_tmp_93=({ dn_t_slice0_U8 dn_tmp_94=v_input;
(dn_tmp_94).len; });
dn_f_codec_read_value(dn_tmp_89,dn_tmp_90,dn_tmp_91,dn_tmp_92,dn_tmp_93); });
if(({ int32_t dn_tmp_95=((v_value).f_error).f_kind;
int32_t dn_tmp_96=((int32_t)(0ULL));
(dn_tmp_95 != dn_tmp_96); })){
{ dn_r_CodecShape dn_tmp_100=({ int32_t dn_tmp_97=((v_value).f_error).f_kind;
uint32_t dn_tmp_98=((v_value).f_error).f_field;
size_t dn_tmp_99=((v_value).f_error).f_offset;
dn_f_codec_shape_error(dn_tmp_97,dn_tmp_98,dn_tmp_99); });return dn_tmp_100; }
}
else{
}
{ size_t dn_tmp_102=(v_value).f_next;size_t *dn_tmp_101=&(v_position);*dn_tmp_101=dn_tmp_102; }
{ size_t dn_tmp_110=({ size_t dn_tmp_105=(*({ dn_t_arr32_USize *dn_tmp_106=&((v_result).f_lengths);size_t dn_tmp_107=v_index;&dn_tmp_106->data[dn_index(dn_tmp_107,32)]; }));
size_t dn_tmp_108=(v_value).f_count;
dn_USize_add(dn_tmp_105,dn_tmp_108); });size_t *dn_tmp_109=&((*({ dn_t_arr32_USize *dn_tmp_103=&((v_result).f_lengths);size_t dn_tmp_104=v_index;&dn_tmp_103->data[dn_index(dn_tmp_104,32)]; })));*dn_tmp_109=dn_tmp_110; }
{ size_t dn_tmp_114=({ size_t dn_tmp_111=(v_result).f_bytes;
size_t dn_tmp_112=(v_value).f_count;
dn_USize_add(dn_tmp_111,dn_tmp_112); });size_t *dn_tmp_113=&((v_result).f_bytes);*dn_tmp_113=dn_tmp_114; }
}
 }
}
 }
}
else{
if(({ size_t dn_tmp_115=({ size_t dn_tmp_116=({ dn_t_slice0_U8 dn_tmp_117=v_input;
(dn_tmp_117).len; });
size_t dn_tmp_118=v_position;
dn_USize_sub(dn_tmp_116,dn_tmp_118); });
size_t dn_tmp_119=((size_t)(8ULL));
(dn_tmp_115 < dn_tmp_119); })){
{ dn_r_CodecShape dn_tmp_123=({ int32_t dn_tmp_120=((int32_t)(1ULL));
uint32_t dn_tmp_121=((uint32_t)(0ULL));
size_t dn_tmp_122=v_position;
dn_f_codec_shape_error(dn_tmp_120,dn_tmp_121,dn_tmp_122); });return dn_tmp_123; }
}
else{
}
dn_r_ProtocolValue v_magic=({ dn_t_slice0_U8 dn_tmp_124=v_input;
size_t dn_tmp_125=v_position;
size_t dn_tmp_126=((size_t)(4ULL));
dn_f_net_read_le(dn_tmp_124,dn_tmp_125,dn_tmp_126); });
dn_r_ProtocolValue v_version=({ dn_t_slice0_U8 dn_tmp_127=v_input;
size_t dn_tmp_128=({ size_t dn_tmp_129=v_position;
size_t dn_tmp_130=((size_t)(4ULL));
dn_USize_add(dn_tmp_129,dn_tmp_130); });
size_t dn_tmp_131=((size_t)(2ULL));
dn_f_net_read_le(dn_tmp_127,dn_tmp_128,dn_tmp_131); });
dn_r_ProtocolValue v_flags=({ dn_t_slice0_U8 dn_tmp_132=v_input;
size_t dn_tmp_133=({ size_t dn_tmp_134=v_position;
size_t dn_tmp_135=((size_t)(6ULL));
dn_USize_add(dn_tmp_134,dn_tmp_135); });
size_t dn_tmp_136=((size_t)(2ULL));
dn_f_net_read_le(dn_tmp_132,dn_tmp_133,dn_tmp_136); });
if(((({ uint64_t dn_tmp_137=(v_magic).f_value;
uint64_t dn_tmp_138=({ uint64_t dn_tmp_139=((uint64_t)(0x424d4c44ULL));
if((unsigned __int128)dn_tmp_139>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_139; });
(dn_tmp_137 != dn_tmp_138); }) || ({ uint64_t dn_tmp_140=(v_version).f_value;
uint64_t dn_tmp_141=({ uint32_t dn_tmp_142=((v_plan)->f_wire).f_envelope_version;
if((unsigned __int128)dn_tmp_142>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_142; });
(dn_tmp_140 != dn_tmp_141); })) || ({ uint64_t dn_tmp_143=(v_flags).f_value;
uint64_t dn_tmp_144=((uint64_t)(0ULL));
(dn_tmp_143 != dn_tmp_144); }))){
{ dn_r_CodecShape dn_tmp_148=({ int32_t dn_tmp_145=((int32_t)(4ULL));
uint32_t dn_tmp_146=((uint32_t)(0ULL));
size_t dn_tmp_147=v_position;
dn_f_codec_shape_error(dn_tmp_145,dn_tmp_146,dn_tmp_147); });return dn_tmp_148; }
}
else{
}
{ size_t dn_tmp_152=({ size_t dn_tmp_149=v_position;
size_t dn_tmp_150=((size_t)(8ULL));
dn_USize_add(dn_tmp_149,dn_tmp_150); });size_t *dn_tmp_151=&(v_position);*dn_tmp_151=dn_tmp_152; }
{ size_t dn_tmp_159=((size_t)(0ULL));size_t dn_tmp_158=({ size_t dn_tmp_153=({ size_t dn_tmp_154=({ dn_t_slice0_U8 dn_tmp_155=v_input;
(dn_tmp_155).len; });
size_t dn_tmp_156=((size_t)(8ULL));
dn_USize_div(dn_tmp_154,dn_tmp_156); });
size_t dn_tmp_157=((size_t)(1ULL));
dn_USize_add(dn_tmp_153,dn_tmp_157); });for(size_t v_item=dn_tmp_159;v_item<dn_tmp_158;v_item++){
if(({ size_t dn_tmp_160=v_position;
size_t dn_tmp_161=({ dn_t_slice0_U8 dn_tmp_162=v_input;
(dn_tmp_162).len; });
(dn_tmp_160 == dn_tmp_161); })){
break;
}
else{
}
if(({ size_t dn_tmp_163=({ size_t dn_tmp_164=({ dn_t_slice0_U8 dn_tmp_165=v_input;
(dn_tmp_165).len; });
size_t dn_tmp_166=v_position;
dn_USize_sub(dn_tmp_164,dn_tmp_166); });
size_t dn_tmp_167=((size_t)(8ULL));
(dn_tmp_163 < dn_tmp_167); })){
{ dn_r_CodecShape dn_tmp_171=({ int32_t dn_tmp_168=((int32_t)(1ULL));
uint32_t dn_tmp_169=((uint32_t)(0ULL));
size_t dn_tmp_170=v_position;
dn_f_codec_shape_error(dn_tmp_168,dn_tmp_169,dn_tmp_170); });return dn_tmp_171; }
}
else{
}
dn_r_ProtocolValue v_tag=({ dn_t_slice0_U8 dn_tmp_172=v_input;
size_t dn_tmp_173=v_position;
size_t dn_tmp_174=((size_t)(4ULL));
dn_f_net_read_le(dn_tmp_172,dn_tmp_173,dn_tmp_174); });
dn_r_ProtocolValue v_size=({ dn_t_slice0_U8 dn_tmp_175=v_input;
size_t dn_tmp_176=({ size_t dn_tmp_177=v_position;
size_t dn_tmp_178=((size_t)(4ULL));
dn_USize_add(dn_tmp_177,dn_tmp_178); });
size_t dn_tmp_179=((size_t)(4ULL));
dn_f_net_read_le(dn_tmp_175,dn_tmp_176,dn_tmp_179); });
uint32_t v_field_id=({ uint64_t dn_tmp_180=(v_tag).f_value;
if((unsigned __int128)dn_tmp_180>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_180; });
size_t v_count=({ uint64_t dn_tmp_181=(v_size).f_value;
if((unsigned __int128)dn_tmp_181>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_181; });
size_t v_begin=v_position;
if(({ size_t dn_tmp_182=v_count;
size_t dn_tmp_183=(v_plan)->f_maximum_wire;
(dn_tmp_182 > dn_tmp_183); })){
{ dn_r_CodecShape dn_tmp_187=({ int32_t dn_tmp_184=((int32_t)(7ULL));
uint32_t dn_tmp_185=v_field_id;
size_t dn_tmp_186=v_begin;
dn_f_codec_shape_error(dn_tmp_184,dn_tmp_185,dn_tmp_186); });return dn_tmp_187; }
}
else{
}
size_t v_index=({ dn_r_CodecPlan const * dn_tmp_188=v_plan;
uint32_t dn_tmp_189=v_field_id;
dn_f_codec_find(dn_tmp_188,dn_tmp_189); });
if(({ size_t dn_tmp_190=v_index;
size_t dn_tmp_191=({ uint32_t dn_tmp_192=(v_plan)->f_field_count;
if((unsigned __int128)dn_tmp_192>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_192; });
(dn_tmp_190 == dn_tmp_191); })){
if(({ uint32_t dn_tmp_193=((v_plan)->f_wire).f_unknown_fields;
uint32_t dn_tmp_194=((uint32_t)(1ULL));
(dn_tmp_193 != dn_tmp_194); })){
{ dn_r_CodecShape dn_tmp_198=({ int32_t dn_tmp_195=((int32_t)(4ULL));
uint32_t dn_tmp_196=v_field_id;
size_t dn_tmp_197=v_begin;
dn_f_codec_shape_error(dn_tmp_195,dn_tmp_196,dn_tmp_197); });return dn_tmp_198; }
}
else{
}
if(({ size_t dn_tmp_199=({ size_t dn_tmp_200=((size_t)(8ULL));
size_t dn_tmp_201=v_count;
dn_USize_add(dn_tmp_200,dn_tmp_201); });
size_t dn_tmp_202=({ size_t dn_tmp_203=(v_plan)->f_maximum_unknown_bytes;
size_t dn_tmp_204=(v_result).f_unknown;
dn_USize_sub(dn_tmp_203,dn_tmp_204); });
(dn_tmp_199 > dn_tmp_202); })){
{ dn_r_CodecShape dn_tmp_208=({ int32_t dn_tmp_205=((int32_t)(7ULL));
uint32_t dn_tmp_206=v_field_id;
size_t dn_tmp_207=v_begin;
dn_f_codec_shape_error(dn_tmp_205,dn_tmp_206,dn_tmp_207); });return dn_tmp_208; }
}
else{
}
}
else{
dn_r_MessageField const * v_field=(&((*({ dn_t_arr32_r12_MessageField *dn_tmp_209=&((v_plan)->f_fields);size_t dn_tmp_210=v_index;&dn_tmp_209->data[dn_index(dn_tmp_210,32)]; }))));
if(((({ uint32_t dn_tmp_211=(v_field)->f_kind;
uint32_t dn_tmp_212=((uint32_t)(6ULL));
(dn_tmp_211 == dn_tmp_212); }) || ({ uint32_t dn_tmp_213=(v_field)->f_kind;
uint32_t dn_tmp_214=((uint32_t)(7ULL));
(dn_tmp_213 == dn_tmp_214); })) && ({ size_t dn_tmp_215=v_count;
size_t dn_tmp_216=({ uint32_t dn_tmp_217=(v_field)->f_maximum_length;
if((unsigned __int128)dn_tmp_217>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_217; });
(dn_tmp_215 > dn_tmp_216); }))){
{ dn_r_CodecShape dn_tmp_221=({ int32_t dn_tmp_218=((int32_t)(7ULL));
uint32_t dn_tmp_219=v_field_id;
size_t dn_tmp_220=v_begin;
dn_f_codec_shape_error(dn_tmp_218,dn_tmp_219,dn_tmp_220); });return dn_tmp_221; }
}
else{
}
}
if(({ size_t dn_tmp_222=v_count;
size_t dn_tmp_223=({ size_t dn_tmp_224=({ size_t dn_tmp_225=({ dn_t_slice0_U8 dn_tmp_226=v_input;
(dn_tmp_226).len; });
size_t dn_tmp_227=v_position;
dn_USize_sub(dn_tmp_225,dn_tmp_227); });
size_t dn_tmp_228=((size_t)(8ULL));
dn_USize_sub(dn_tmp_224,dn_tmp_228); });
(dn_tmp_222 > dn_tmp_223); })){
{ dn_r_CodecShape dn_tmp_232=({ int32_t dn_tmp_229=((int32_t)(1ULL));
uint32_t dn_tmp_230=v_field_id;
size_t dn_tmp_231=v_position;
dn_f_codec_shape_error(dn_tmp_229,dn_tmp_230,dn_tmp_231); });return dn_tmp_232; }
}
else{
}
{ size_t dn_tmp_236=({ size_t dn_tmp_233=v_position;
size_t dn_tmp_234=((size_t)(8ULL));
dn_USize_add(dn_tmp_233,dn_tmp_234); });size_t *dn_tmp_235=&(v_position);*dn_tmp_235=dn_tmp_236; }
size_t v_end=({ size_t dn_tmp_237=v_position;
size_t dn_tmp_238=v_count;
dn_USize_add(dn_tmp_237,dn_tmp_238); });
if(({ size_t dn_tmp_239=v_index;
size_t dn_tmp_240=({ uint32_t dn_tmp_241=(v_plan)->f_field_count;
if((unsigned __int128)dn_tmp_241>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_241; });
(dn_tmp_239 == dn_tmp_240); })){
if(({ uint32_t dn_tmp_242=((v_plan)->f_wire).f_unknown_fields;
uint32_t dn_tmp_243=((uint32_t)(1ULL));
(dn_tmp_242 != dn_tmp_243); })){
{ dn_r_CodecShape dn_tmp_247=({ int32_t dn_tmp_244=((int32_t)(4ULL));
uint32_t dn_tmp_245=v_field_id;
size_t dn_tmp_246=v_begin;
dn_f_codec_shape_error(dn_tmp_244,dn_tmp_245,dn_tmp_246); });return dn_tmp_247; }
}
else{
}
{ uint32_t dn_tmp_251=({ uint32_t dn_tmp_248=(v_result).f_unknown_fields;
uint32_t dn_tmp_249=((uint32_t)(1ULL));
dn_U32_add(dn_tmp_248,dn_tmp_249); });uint32_t *dn_tmp_250=&((v_result).f_unknown_fields);*dn_tmp_250=dn_tmp_251; }
{ size_t dn_tmp_257=({ size_t dn_tmp_252=({ size_t dn_tmp_253=(v_result).f_unknown;
size_t dn_tmp_254=((size_t)(8ULL));
dn_USize_add(dn_tmp_253,dn_tmp_254); });
size_t dn_tmp_255=v_count;
dn_USize_add(dn_tmp_252,dn_tmp_255); });size_t *dn_tmp_256=&((v_result).f_unknown);*dn_tmp_256=dn_tmp_257; }
if((({ uint32_t dn_tmp_258=(v_result).f_unknown_fields;
uint32_t dn_tmp_259=(v_plan)->f_maximum_unknown_fields;
(dn_tmp_258 > dn_tmp_259); }) || ({ size_t dn_tmp_260=(v_result).f_unknown;
size_t dn_tmp_261=(v_plan)->f_maximum_unknown_bytes;
(dn_tmp_260 > dn_tmp_261); }))){
{ dn_r_CodecShape dn_tmp_265=({ int32_t dn_tmp_262=((int32_t)(7ULL));
uint32_t dn_tmp_263=v_field_id;
size_t dn_tmp_264=v_begin;
dn_f_codec_shape_error(dn_tmp_262,dn_tmp_263,dn_tmp_264); });return dn_tmp_265; }
}
else{
}
}
else{
dn_r_MessageField const * v_field=(&((*({ dn_t_arr32_r12_MessageField *dn_tmp_266=&((v_plan)->f_fields);size_t dn_tmp_267=v_index;&dn_tmp_266->data[dn_index(dn_tmp_267,32)]; }))));
{ uint32_t dn_tmp_275=({ uint32_t dn_tmp_270=(*({ dn_t_arr32_U32 *dn_tmp_271=&((v_result).f_counts);size_t dn_tmp_272=v_index;&dn_tmp_271->data[dn_index(dn_tmp_272,32)]; }));
uint32_t dn_tmp_273=((uint32_t)(1ULL));
dn_U32_add(dn_tmp_270,dn_tmp_273); });uint32_t *dn_tmp_274=&((*({ dn_t_arr32_U32 *dn_tmp_268=&((v_result).f_counts);size_t dn_tmp_269=v_index;&dn_tmp_268->data[dn_index(dn_tmp_269,32)]; })));*dn_tmp_274=dn_tmp_275; }
if(({ uint32_t dn_tmp_276=(*({ dn_t_arr32_U32 *dn_tmp_277=&((v_result).f_counts);size_t dn_tmp_278=v_index;&dn_tmp_277->data[dn_index(dn_tmp_278,32)]; }));
uint32_t dn_tmp_279=(v_field)->f_maximum_count;
(dn_tmp_276 > dn_tmp_279); })){
{ dn_r_CodecShape dn_tmp_283=({ int32_t dn_tmp_280=((int32_t)(7ULL));
uint32_t dn_tmp_281=v_field_id;
size_t dn_tmp_282=v_begin;
dn_f_codec_shape_error(dn_tmp_280,dn_tmp_281,dn_tmp_282); });return dn_tmp_283; }
}
else{
}
dn_r_CodecRead v_value=({ dn_r_CodecPlan const * dn_tmp_284=v_plan;
dn_r_MessageField const * dn_tmp_285=v_field;
dn_t_slice0_U8 dn_tmp_286=v_input;
size_t dn_tmp_287=v_position;
size_t dn_tmp_288=v_end;
dn_f_codec_read_value(dn_tmp_284,dn_tmp_285,dn_tmp_286,dn_tmp_287,dn_tmp_288); });
if(({ int32_t dn_tmp_289=((v_value).f_error).f_kind;
int32_t dn_tmp_290=((int32_t)(0ULL));
(dn_tmp_289 != dn_tmp_290); })){
{ dn_r_CodecShape dn_tmp_294=({ int32_t dn_tmp_291=((v_value).f_error).f_kind;
uint32_t dn_tmp_292=((v_value).f_error).f_field;
size_t dn_tmp_293=((v_value).f_error).f_offset;
dn_f_codec_shape_error(dn_tmp_291,dn_tmp_292,dn_tmp_293); });return dn_tmp_294; }
}
else{
}
{ size_t dn_tmp_298=({ size_t dn_tmp_295=(v_result).f_values;
size_t dn_tmp_296=((size_t)(1ULL));
dn_USize_add(dn_tmp_295,dn_tmp_296); });size_t *dn_tmp_297=&((v_result).f_values);*dn_tmp_297=dn_tmp_298; }
{ size_t dn_tmp_306=({ size_t dn_tmp_301=(*({ dn_t_arr32_USize *dn_tmp_302=&((v_result).f_lengths);size_t dn_tmp_303=v_index;&dn_tmp_302->data[dn_index(dn_tmp_303,32)]; }));
size_t dn_tmp_304=(v_value).f_count;
dn_USize_add(dn_tmp_301,dn_tmp_304); });size_t *dn_tmp_305=&((*({ dn_t_arr32_USize *dn_tmp_299=&((v_result).f_lengths);size_t dn_tmp_300=v_index;&dn_tmp_299->data[dn_index(dn_tmp_300,32)]; })));*dn_tmp_305=dn_tmp_306; }
{ size_t dn_tmp_310=({ size_t dn_tmp_307=(v_result).f_bytes;
size_t dn_tmp_308=(v_value).f_count;
dn_USize_add(dn_tmp_307,dn_tmp_308); });size_t *dn_tmp_309=&((v_result).f_bytes);*dn_tmp_309=dn_tmp_310; }
}
{ size_t dn_tmp_312=v_end;size_t *dn_tmp_311=&(v_position);*dn_tmp_311=dn_tmp_312; }
}
 }
}
if(({ size_t dn_tmp_313=v_position;
size_t dn_tmp_314=({ dn_t_slice0_U8 dn_tmp_315=v_input;
(dn_tmp_315).len; });
(dn_tmp_313 != dn_tmp_314); })){
{ dn_r_CodecShape dn_tmp_319=({ int32_t dn_tmp_316=((int32_t)(4ULL));
uint32_t dn_tmp_317=((uint32_t)(0ULL));
size_t dn_tmp_318=v_position;
dn_f_codec_shape_error(dn_tmp_316,dn_tmp_317,dn_tmp_318); });return dn_tmp_319; }
}
else{
}
dn_r_CodecError v_complete=({ dn_r_CodecPlan const * dn_tmp_320=v_plan;
dn_r_CodecShape const * dn_tmp_321=(&(v_result));
dn_f_codec_shape_complete(dn_tmp_320,dn_tmp_321); });
if(({ int32_t dn_tmp_322=(v_complete).f_kind;
int32_t dn_tmp_323=((int32_t)(0ULL));
(dn_tmp_322 != dn_tmp_323); })){
{ dn_r_CodecShape dn_tmp_327=({ int32_t dn_tmp_324=(v_complete).f_kind;
uint32_t dn_tmp_325=(v_complete).f_field;
size_t dn_tmp_326=(v_complete).f_offset;
dn_f_codec_shape_error(dn_tmp_324,dn_tmp_325,dn_tmp_326); });return dn_tmp_327; }
}
else{
}
{ dn_r_CodecShape dn_tmp_328=v_result;return dn_tmp_328; }
}

dn_fail("missing native return");
}
static dn_r_CodecShape dn_cb_codec_scan(dn_r_CodecPlan const * v_plan,dn_t_slice0_U8 v_input) {
 if(!dn_enter()){return (dn_r_CodecShape){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecShape){0};}
 dn_r_CodecShape dn_result=dn_f_codec_scan(v_plan,v_input);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecMessage dn_f_codec_message(uint32_t v_id) {
{
{ dn_r_CodecMessage dn_tmp_7=({ uint32_t dn_tmp_0=v_id;
dn_t_buf_r10_CodecValue dn_tmp_1=({ size_t dn_tmp_2=((size_t)(0ULL));
dn_new_buf_r10_CodecValue(dn_tmp_2); });
dn_adopt_buf_r10_CodecValue(&dn_tmp_1);
dn_t_buf_U8 dn_tmp_3=({ size_t dn_tmp_4=((size_t)(0ULL));
dn_new_buf_U8(dn_tmp_4); });
dn_adopt_buf_U8(&dn_tmp_3);
dn_t_buf_U8 dn_tmp_5=({ size_t dn_tmp_6=((size_t)(0ULL));
dn_new_buf_U8(dn_tmp_6); });
dn_adopt_buf_U8(&dn_tmp_5);
((dn_r_CodecMessage){dn_tmp_0,dn_tmp_1,dn_tmp_3,dn_tmp_5}); });dn_adopt_r12_CodecMessage(&dn_tmp_7);return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_CodecMessage dn_cb_codec_message(uint32_t v_id) {
 if(!dn_enter()){return (dn_r_CodecMessage){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecMessage){0};}
 dn_r_CodecMessage dn_result=dn_f_codec_message(v_id);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecDecoded dn_f_codec_decode_error(int32_t v_kind,uint32_t v_field,size_t v_offset) {
{
{ dn_r_CodecDecoded dn_tmp_6=({ dn_r_CodecMessage dn_tmp_0=({ uint32_t dn_tmp_1=((uint32_t)(0ULL));
dn_f_codec_message(dn_tmp_1); });
dn_adopt_r12_CodecMessage(&dn_tmp_0);
dn_r_CodecError dn_tmp_2=({ int32_t dn_tmp_3=v_kind;
uint32_t dn_tmp_4=v_field;
size_t dn_tmp_5=v_offset;
((dn_r_CodecError){dn_tmp_3,dn_tmp_4,dn_tmp_5}); });
((dn_r_CodecDecoded){dn_tmp_0,dn_tmp_2}); });dn_adopt_r12_CodecDecoded(&dn_tmp_6);return dn_tmp_6; }
}

dn_fail("missing native return");
}
static dn_r_CodecDecoded dn_cb_codec_decode_error(int32_t v_kind,uint32_t v_field,size_t v_offset) {
 if(!dn_enter()){return (dn_r_CodecDecoded){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecDecoded){0};}
 dn_r_CodecDecoded dn_result=dn_f_codec_decode_error(v_kind,v_field,v_offset);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecDecoded dn_f_codec_decode(dn_r_CodecPlan const * v_plan,dn_t_slice0_U8 v_input) {
{
dn_r_CodecMessage volatile v_result={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r12_CodecMessage(&v_result);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
dn_r_CodecShape v_shape=({ dn_r_CodecPlan const * dn_tmp_2=v_plan;
dn_t_slice0_U8 dn_tmp_3=v_input;
dn_f_codec_scan(dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_4=((v_shape).f_error).f_kind;
int32_t dn_tmp_5=((int32_t)(0ULL));
(dn_tmp_4 != dn_tmp_5); })){
{ dn_r_CodecDecoded dn_tmp_9=({ int32_t dn_tmp_6=((v_shape).f_error).f_kind;
uint32_t dn_tmp_7=((v_shape).f_error).f_field;
size_t dn_tmp_8=((v_shape).f_error).f_offset;
dn_f_codec_decode_error(dn_tmp_6,dn_tmp_7,dn_tmp_8); });dn_adopt_r12_CodecDecoded(&dn_tmp_9);dn_drop_r12_CodecMessage(&v_result);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_9; }
}
else{
}
v_result=({ uint32_t dn_tmp_10=(v_plan)->f_message;
dn_t_buf_r10_CodecValue dn_tmp_11=({ size_t dn_tmp_12=(v_shape).f_values;
dn_new_buf_r10_CodecValue(dn_tmp_12); });
dn_adopt_buf_r10_CodecValue(&dn_tmp_11);
dn_t_buf_U8 dn_tmp_13=({ size_t dn_tmp_14=(v_shape).f_bytes;
dn_new_buf_U8(dn_tmp_14); });
dn_adopt_buf_U8(&dn_tmp_13);
dn_t_buf_U8 dn_tmp_15=({ size_t dn_tmp_16=(v_shape).f_unknown;
dn_new_buf_U8(dn_tmp_16); });
dn_adopt_buf_U8(&dn_tmp_15);
((dn_r_CodecMessage){dn_tmp_10,dn_tmp_11,dn_tmp_13,dn_tmp_15}); });
dn_t_arr32_USize v_value_start=({ ((dn_t_arr32_USize){0}); });
dn_t_arr32_USize v_byte_start=({ ((dn_t_arr32_USize){0}); });
dn_t_arr32_U32 v_used=({ ((dn_t_arr32_U32){0}); });
dn_t_arr32_USize v_used_bytes=({ ((dn_t_arr32_USize){0}); });
size_t v_values=((size_t)(0ULL));
size_t v_bytes=((size_t)(0ULL));
{ size_t dn_tmp_19=((size_t)(0ULL));size_t dn_tmp_18=({ uint32_t dn_tmp_17=(v_plan)->f_field_count;
if((unsigned __int128)dn_tmp_17>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_17; });for(size_t v_index=dn_tmp_19;v_index<dn_tmp_18;v_index++){
{ size_t dn_tmp_23=v_values;size_t *dn_tmp_22=&((*({ dn_t_arr32_USize *dn_tmp_20=&(v_value_start);size_t dn_tmp_21=v_index;&dn_tmp_20->data[dn_index(dn_tmp_21,32)]; })));*dn_tmp_22=dn_tmp_23; }
{ size_t dn_tmp_27=v_bytes;size_t *dn_tmp_26=&((*({ dn_t_arr32_USize *dn_tmp_24=&(v_byte_start);size_t dn_tmp_25=v_index;&dn_tmp_24->data[dn_index(dn_tmp_25,32)]; })));*dn_tmp_26=dn_tmp_27; }
{ size_t dn_tmp_34=({ size_t dn_tmp_28=v_values;
size_t dn_tmp_29=({ uint32_t dn_tmp_30=(*({ dn_t_arr32_U32 *dn_tmp_31=&((v_shape).f_counts);size_t dn_tmp_32=v_index;&dn_tmp_31->data[dn_index(dn_tmp_32,32)]; }));
if((unsigned __int128)dn_tmp_30>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_30; });
dn_USize_add(dn_tmp_28,dn_tmp_29); });size_t *dn_tmp_33=&(v_values);*dn_tmp_33=dn_tmp_34; }
{ size_t dn_tmp_40=({ size_t dn_tmp_35=v_bytes;
size_t dn_tmp_36=(*({ dn_t_arr32_USize *dn_tmp_37=&((v_shape).f_lengths);size_t dn_tmp_38=v_index;&dn_tmp_37->data[dn_index(dn_tmp_38,32)]; }));
dn_USize_add(dn_tmp_35,dn_tmp_36); });size_t *dn_tmp_39=&(v_bytes);*dn_tmp_39=dn_tmp_40; }
}
 }
size_t v_position=((size_t)(4ULL));
size_t v_unknown=((size_t)(0ULL));
if(({ uint32_t dn_tmp_41=((v_plan)->f_wire).f_kind;
uint32_t dn_tmp_42=((uint32_t)(2ULL));
(dn_tmp_41 == dn_tmp_42); })){
{ size_t dn_tmp_44=((size_t)(12ULL));size_t *dn_tmp_43=&(v_position);*dn_tmp_43=dn_tmp_44; }
}
else{
}
{ size_t dn_tmp_54=((size_t)(0ULL));size_t dn_tmp_53=({ size_t dn_tmp_45=({ size_t dn_tmp_46=({ size_t dn_tmp_47=(v_shape).f_values;
size_t dn_tmp_48=({ uint32_t dn_tmp_49=(v_shape).f_unknown_fields;
if((unsigned __int128)dn_tmp_49>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_49; });
dn_USize_add(dn_tmp_47,dn_tmp_48); });
size_t dn_tmp_50=({ uint32_t dn_tmp_51=(v_plan)->f_field_count;
if((unsigned __int128)dn_tmp_51>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_51; });
dn_USize_add(dn_tmp_46,dn_tmp_50); });
size_t dn_tmp_52=((size_t)(1ULL));
dn_USize_add(dn_tmp_45,dn_tmp_52); });for(size_t v_item=dn_tmp_54;v_item<dn_tmp_53;v_item++){
if(({ uint32_t dn_tmp_55=((v_plan)->f_wire).f_kind;
uint32_t dn_tmp_56=((uint32_t)(1ULL));
(dn_tmp_55 == dn_tmp_56); })){
if(({ size_t dn_tmp_57=v_item;
size_t dn_tmp_58=({ uint32_t dn_tmp_59=(v_plan)->f_field_count;
if((unsigned __int128)dn_tmp_59>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_59; });
(dn_tmp_57 >= dn_tmp_58); })){
break;
}
else{
}
size_t v_index=v_item;
dn_r_MessageField const * v_field=(&((*({ dn_t_arr32_r12_MessageField *dn_tmp_60=&((v_plan)->f_fields);size_t dn_tmp_61=v_index;&dn_tmp_60->data[dn_index(dn_tmp_61,32)]; }))));
if((({ uint32_t dn_tmp_62=(v_field)->f_minimum_count;
uint32_t dn_tmp_63=((uint32_t)(1ULL));
(dn_tmp_62 != dn_tmp_63); }) || ({ uint32_t dn_tmp_64=(v_field)->f_maximum_count;
uint32_t dn_tmp_65=((uint32_t)(1ULL));
(dn_tmp_64 != dn_tmp_65); }))){
{ size_t dn_tmp_69=({ size_t dn_tmp_66=v_position;
size_t dn_tmp_67=((size_t)(4ULL));
dn_USize_add(dn_tmp_66,dn_tmp_67); });size_t *dn_tmp_68=&(v_position);*dn_tmp_68=dn_tmp_69; }
}
else{
}
{ size_t dn_tmp_74=((size_t)(0ULL));size_t dn_tmp_73=({ uint32_t dn_tmp_70=(*({ dn_t_arr32_U32 *dn_tmp_71=&((v_shape).f_counts);size_t dn_tmp_72=v_index;&dn_tmp_71->data[dn_index(dn_tmp_72,32)]; }));
if((unsigned __int128)dn_tmp_70>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_70; });for(size_t v_occurrence=dn_tmp_74;v_occurrence<dn_tmp_73;v_occurrence++){
dn_r_CodecRead v_value=({ dn_r_CodecPlan const * dn_tmp_75=v_plan;
dn_r_MessageField const * dn_tmp_76=v_field;
dn_t_slice0_U8 dn_tmp_77=v_input;
size_t dn_tmp_78=v_position;
size_t dn_tmp_79=({ dn_t_slice0_U8 dn_tmp_80=v_input;
(dn_tmp_80).len; });
dn_f_codec_read_value(dn_tmp_75,dn_tmp_76,dn_tmp_77,dn_tmp_78,dn_tmp_79); });
size_t v_offset=({ size_t dn_tmp_81=(*({ dn_t_arr32_USize *dn_tmp_82=&(v_byte_start);size_t dn_tmp_83=v_index;&dn_tmp_82->data[dn_index(dn_tmp_83,32)]; }));
size_t dn_tmp_84=(*({ dn_t_arr32_USize *dn_tmp_85=&(v_used_bytes);size_t dn_tmp_86=v_index;&dn_tmp_85->data[dn_index(dn_tmp_86,32)]; }));
dn_USize_add(dn_tmp_81,dn_tmp_84); });
{ dn_r_CodecValue dn_tmp_99=({ uint32_t dn_tmp_93=(v_field)->f_id;
uint32_t dn_tmp_94=(v_field)->f_kind;
uint64_t dn_tmp_95=(v_value).f_scalar;
size_t dn_tmp_96=((size_t)(0ULL));
size_t dn_tmp_97=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_93,dn_tmp_94,dn_tmp_95,dn_tmp_96,dn_tmp_97}); });dn_r_CodecValue *dn_tmp_98=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_87=&((v_result).f_values);size_t dn_tmp_88=({ size_t dn_tmp_89=(*({ dn_t_arr32_USize *dn_tmp_90=&(v_value_start);size_t dn_tmp_91=v_index;&dn_tmp_90->data[dn_index(dn_tmp_91,32)]; }));
size_t dn_tmp_92=v_occurrence;
dn_USize_add(dn_tmp_89,dn_tmp_92); });&dn_tmp_87->data[dn_index(dn_tmp_88,dn_tmp_87->len)]; })));*dn_tmp_98=dn_tmp_99; }
if((({ uint32_t dn_tmp_100=(v_field)->f_kind;
uint32_t dn_tmp_101=((uint32_t)(6ULL));
(dn_tmp_100 == dn_tmp_101); }) || ({ uint32_t dn_tmp_102=(v_field)->f_kind;
uint32_t dn_tmp_103=((uint32_t)(7ULL));
(dn_tmp_102 == dn_tmp_103); }))){
{ size_t dn_tmp_111=v_offset;size_t *dn_tmp_110=&(((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_104=&((v_result).f_values);size_t dn_tmp_105=({ size_t dn_tmp_106=(*({ dn_t_arr32_USize *dn_tmp_107=&(v_value_start);size_t dn_tmp_108=v_index;&dn_tmp_107->data[dn_index(dn_tmp_108,32)]; }));
size_t dn_tmp_109=v_occurrence;
dn_USize_add(dn_tmp_106,dn_tmp_109); });&dn_tmp_104->data[dn_index(dn_tmp_105,dn_tmp_104->len)]; }))).f_offset);*dn_tmp_110=dn_tmp_111; }
{ size_t dn_tmp_119=(v_value).f_count;size_t *dn_tmp_118=&(((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_112=&((v_result).f_values);size_t dn_tmp_113=({ size_t dn_tmp_114=(*({ dn_t_arr32_USize *dn_tmp_115=&(v_value_start);size_t dn_tmp_116=v_index;&dn_tmp_115->data[dn_index(dn_tmp_116,32)]; }));
size_t dn_tmp_117=v_occurrence;
dn_USize_add(dn_tmp_114,dn_tmp_117); });&dn_tmp_112->data[dn_index(dn_tmp_113,dn_tmp_112->len)]; }))).f_count);*dn_tmp_118=dn_tmp_119; }
{ size_t dn_tmp_121=((size_t)(0ULL));size_t dn_tmp_120=(v_value).f_count;for(size_t v_b=dn_tmp_121;v_b<dn_tmp_120;v_b++){
{ uint8_t dn_tmp_131=(*({ dn_t_slice0_U8 *dn_tmp_126=&(v_input);size_t dn_tmp_127=({ size_t dn_tmp_128=(v_value).f_offset;
size_t dn_tmp_129=v_b;
dn_USize_add(dn_tmp_128,dn_tmp_129); });&dn_tmp_126->data[dn_index(dn_tmp_127,dn_tmp_126->len)]; }));uint8_t *dn_tmp_130=&((*({ dn_t_buf_U8 volatile *dn_tmp_122=&((v_result).f_bytes);size_t dn_tmp_123=({ size_t dn_tmp_124=v_offset;
size_t dn_tmp_125=v_b;
dn_USize_add(dn_tmp_124,dn_tmp_125); });&dn_tmp_122->data[dn_index(dn_tmp_123,dn_tmp_122->len)]; })));*dn_tmp_130=dn_tmp_131; }
}
 }
{ size_t dn_tmp_139=({ size_t dn_tmp_134=(*({ dn_t_arr32_USize *dn_tmp_135=&(v_used_bytes);size_t dn_tmp_136=v_index;&dn_tmp_135->data[dn_index(dn_tmp_136,32)]; }));
size_t dn_tmp_137=(v_value).f_count;
dn_USize_add(dn_tmp_134,dn_tmp_137); });size_t *dn_tmp_138=&((*({ dn_t_arr32_USize *dn_tmp_132=&(v_used_bytes);size_t dn_tmp_133=v_index;&dn_tmp_132->data[dn_index(dn_tmp_133,32)]; })));*dn_tmp_138=dn_tmp_139; }
}
else{
}
{ size_t dn_tmp_141=(v_value).f_next;size_t *dn_tmp_140=&(v_position);*dn_tmp_140=dn_tmp_141; }
}
 }
}
else{
if(({ size_t dn_tmp_142=v_position;
size_t dn_tmp_143=({ dn_t_slice0_U8 dn_tmp_144=v_input;
(dn_tmp_144).len; });
(dn_tmp_142 == dn_tmp_143); })){
break;
}
else{
}
size_t v_begin=v_position;
dn_r_ProtocolValue v_tag=({ dn_t_slice0_U8 dn_tmp_145=v_input;
size_t dn_tmp_146=v_position;
size_t dn_tmp_147=((size_t)(4ULL));
dn_f_net_read_le(dn_tmp_145,dn_tmp_146,dn_tmp_147); });
dn_r_ProtocolValue v_size=({ dn_t_slice0_U8 dn_tmp_148=v_input;
size_t dn_tmp_149=({ size_t dn_tmp_150=v_position;
size_t dn_tmp_151=((size_t)(4ULL));
dn_USize_add(dn_tmp_150,dn_tmp_151); });
size_t dn_tmp_152=((size_t)(4ULL));
dn_f_net_read_le(dn_tmp_148,dn_tmp_149,dn_tmp_152); });
size_t v_count=({ uint64_t dn_tmp_153=(v_size).f_value;
if((unsigned __int128)dn_tmp_153>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_153; });
{ size_t dn_tmp_157=({ size_t dn_tmp_154=v_position;
size_t dn_tmp_155=((size_t)(8ULL));
dn_USize_add(dn_tmp_154,dn_tmp_155); });size_t *dn_tmp_156=&(v_position);*dn_tmp_156=dn_tmp_157; }
size_t v_end=({ size_t dn_tmp_158=v_position;
size_t dn_tmp_159=v_count;
dn_USize_add(dn_tmp_158,dn_tmp_159); });
size_t v_index=({ dn_r_CodecPlan const * dn_tmp_160=v_plan;
uint32_t dn_tmp_161=({ uint64_t dn_tmp_162=(v_tag).f_value;
if((unsigned __int128)dn_tmp_162>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_162; });
dn_f_codec_find(dn_tmp_160,dn_tmp_161); });
if(({ size_t dn_tmp_163=v_index;
size_t dn_tmp_164=({ uint32_t dn_tmp_165=(v_plan)->f_field_count;
if((unsigned __int128)dn_tmp_165>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_165; });
(dn_tmp_163 == dn_tmp_164); })){
{ size_t dn_tmp_169=((size_t)(0ULL));size_t dn_tmp_168=({ size_t dn_tmp_166=((size_t)(8ULL));
size_t dn_tmp_167=v_count;
dn_USize_add(dn_tmp_166,dn_tmp_167); });for(size_t v_b=dn_tmp_169;v_b<dn_tmp_168;v_b++){
{ uint8_t dn_tmp_179=(*({ dn_t_slice0_U8 *dn_tmp_174=&(v_input);size_t dn_tmp_175=({ size_t dn_tmp_176=v_begin;
size_t dn_tmp_177=v_b;
dn_USize_add(dn_tmp_176,dn_tmp_177); });&dn_tmp_174->data[dn_index(dn_tmp_175,dn_tmp_174->len)]; }));uint8_t *dn_tmp_178=&((*({ dn_t_buf_U8 volatile *dn_tmp_170=&((v_result).f_unknown);size_t dn_tmp_171=({ size_t dn_tmp_172=v_unknown;
size_t dn_tmp_173=v_b;
dn_USize_add(dn_tmp_172,dn_tmp_173); });&dn_tmp_170->data[dn_index(dn_tmp_171,dn_tmp_170->len)]; })));*dn_tmp_178=dn_tmp_179; }
}
 }
{ size_t dn_tmp_185=({ size_t dn_tmp_180=({ size_t dn_tmp_181=v_unknown;
size_t dn_tmp_182=((size_t)(8ULL));
dn_USize_add(dn_tmp_181,dn_tmp_182); });
size_t dn_tmp_183=v_count;
dn_USize_add(dn_tmp_180,dn_tmp_183); });size_t *dn_tmp_184=&(v_unknown);*dn_tmp_184=dn_tmp_185; }
}
else{
dn_r_MessageField const * v_field=(&((*({ dn_t_arr32_r12_MessageField *dn_tmp_186=&((v_plan)->f_fields);size_t dn_tmp_187=v_index;&dn_tmp_186->data[dn_index(dn_tmp_187,32)]; }))));
dn_r_CodecRead v_value=({ dn_r_CodecPlan const * dn_tmp_188=v_plan;
dn_r_MessageField const * dn_tmp_189=v_field;
dn_t_slice0_U8 dn_tmp_190=v_input;
size_t dn_tmp_191=v_position;
size_t dn_tmp_192=v_end;
dn_f_codec_read_value(dn_tmp_188,dn_tmp_189,dn_tmp_190,dn_tmp_191,dn_tmp_192); });
size_t v_slot=({ size_t dn_tmp_193=(*({ dn_t_arr32_USize *dn_tmp_194=&(v_value_start);size_t dn_tmp_195=v_index;&dn_tmp_194->data[dn_index(dn_tmp_195,32)]; }));
size_t dn_tmp_196=({ uint32_t dn_tmp_197=(*({ dn_t_arr32_U32 *dn_tmp_198=&(v_used);size_t dn_tmp_199=v_index;&dn_tmp_198->data[dn_index(dn_tmp_199,32)]; }));
if((unsigned __int128)dn_tmp_197>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_197; });
dn_USize_add(dn_tmp_193,dn_tmp_196); });
size_t v_offset=({ size_t dn_tmp_200=(*({ dn_t_arr32_USize *dn_tmp_201=&(v_byte_start);size_t dn_tmp_202=v_index;&dn_tmp_201->data[dn_index(dn_tmp_202,32)]; }));
size_t dn_tmp_203=(*({ dn_t_arr32_USize *dn_tmp_204=&(v_used_bytes);size_t dn_tmp_205=v_index;&dn_tmp_204->data[dn_index(dn_tmp_205,32)]; }));
dn_USize_add(dn_tmp_200,dn_tmp_203); });
{ dn_r_CodecValue dn_tmp_214=({ uint32_t dn_tmp_208=(v_field)->f_id;
uint32_t dn_tmp_209=(v_field)->f_kind;
uint64_t dn_tmp_210=(v_value).f_scalar;
size_t dn_tmp_211=((size_t)(0ULL));
size_t dn_tmp_212=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_208,dn_tmp_209,dn_tmp_210,dn_tmp_211,dn_tmp_212}); });dn_r_CodecValue *dn_tmp_213=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_206=&((v_result).f_values);size_t dn_tmp_207=v_slot;&dn_tmp_206->data[dn_index(dn_tmp_207,dn_tmp_206->len)]; })));*dn_tmp_213=dn_tmp_214; }
if((({ uint32_t dn_tmp_215=(v_field)->f_kind;
uint32_t dn_tmp_216=((uint32_t)(6ULL));
(dn_tmp_215 == dn_tmp_216); }) || ({ uint32_t dn_tmp_217=(v_field)->f_kind;
uint32_t dn_tmp_218=((uint32_t)(7ULL));
(dn_tmp_217 == dn_tmp_218); }))){
{ size_t dn_tmp_222=v_offset;size_t *dn_tmp_221=&(((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_219=&((v_result).f_values);size_t dn_tmp_220=v_slot;&dn_tmp_219->data[dn_index(dn_tmp_220,dn_tmp_219->len)]; }))).f_offset);*dn_tmp_221=dn_tmp_222; }
{ size_t dn_tmp_226=(v_value).f_count;size_t *dn_tmp_225=&(((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_223=&((v_result).f_values);size_t dn_tmp_224=v_slot;&dn_tmp_223->data[dn_index(dn_tmp_224,dn_tmp_223->len)]; }))).f_count);*dn_tmp_225=dn_tmp_226; }
{ size_t dn_tmp_228=((size_t)(0ULL));size_t dn_tmp_227=(v_value).f_count;for(size_t v_b=dn_tmp_228;v_b<dn_tmp_227;v_b++){
{ uint8_t dn_tmp_238=(*({ dn_t_slice0_U8 *dn_tmp_233=&(v_input);size_t dn_tmp_234=({ size_t dn_tmp_235=(v_value).f_offset;
size_t dn_tmp_236=v_b;
dn_USize_add(dn_tmp_235,dn_tmp_236); });&dn_tmp_233->data[dn_index(dn_tmp_234,dn_tmp_233->len)]; }));uint8_t *dn_tmp_237=&((*({ dn_t_buf_U8 volatile *dn_tmp_229=&((v_result).f_bytes);size_t dn_tmp_230=({ size_t dn_tmp_231=v_offset;
size_t dn_tmp_232=v_b;
dn_USize_add(dn_tmp_231,dn_tmp_232); });&dn_tmp_229->data[dn_index(dn_tmp_230,dn_tmp_229->len)]; })));*dn_tmp_237=dn_tmp_238; }
}
 }
{ size_t dn_tmp_246=({ size_t dn_tmp_241=(*({ dn_t_arr32_USize *dn_tmp_242=&(v_used_bytes);size_t dn_tmp_243=v_index;&dn_tmp_242->data[dn_index(dn_tmp_243,32)]; }));
size_t dn_tmp_244=(v_value).f_count;
dn_USize_add(dn_tmp_241,dn_tmp_244); });size_t *dn_tmp_245=&((*({ dn_t_arr32_USize *dn_tmp_239=&(v_used_bytes);size_t dn_tmp_240=v_index;&dn_tmp_239->data[dn_index(dn_tmp_240,32)]; })));*dn_tmp_245=dn_tmp_246; }
}
else{
}
{ uint32_t dn_tmp_254=({ uint32_t dn_tmp_249=(*({ dn_t_arr32_U32 *dn_tmp_250=&(v_used);size_t dn_tmp_251=v_index;&dn_tmp_250->data[dn_index(dn_tmp_251,32)]; }));
uint32_t dn_tmp_252=((uint32_t)(1ULL));
dn_U32_add(dn_tmp_249,dn_tmp_252); });uint32_t *dn_tmp_253=&((*({ dn_t_arr32_U32 *dn_tmp_247=&(v_used);size_t dn_tmp_248=v_index;&dn_tmp_247->data[dn_index(dn_tmp_248,32)]; })));*dn_tmp_253=dn_tmp_254; }
}
{ size_t dn_tmp_256=v_end;size_t *dn_tmp_255=&(v_position);*dn_tmp_255=dn_tmp_256; }
}
}
 }
{ dn_r_CodecDecoded dn_tmp_263=({ dn_r_CodecMessage dn_tmp_257=({ dn_r_CodecMessage dn_tmp_258=v_result;v_result=(dn_r_CodecMessage){0};dn_adopt_r12_CodecMessage(&dn_tmp_258);dn_tmp_258; });
dn_adopt_r12_CodecMessage(&dn_tmp_257);
dn_r_CodecError dn_tmp_259=({ int32_t dn_tmp_260=((int32_t)(0ULL));
uint32_t dn_tmp_261=((uint32_t)(0ULL));
size_t dn_tmp_262=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_260,dn_tmp_261,dn_tmp_262}); });
((dn_r_CodecDecoded){dn_tmp_257,dn_tmp_259}); });dn_adopt_r12_CodecDecoded(&dn_tmp_263);dn_drop_r12_CodecMessage(&v_result);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_263; }
dn_drop_r12_CodecMessage(&v_result);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_r_CodecDecoded dn_cb_codec_decode(dn_r_CodecPlan const * v_plan,dn_t_slice0_U8 v_input) {
 if(!dn_enter()){return (dn_r_CodecDecoded){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecDecoded){0};}
 dn_r_CodecDecoded dn_result=dn_f_codec_decode(v_plan,v_input);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecShape dn_f_codec_message_inspect(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,bool v_complete) {
{
dn_r_CodecError v_checked=({ dn_r_CodecPlan const * dn_tmp_0=v_plan;
dn_f_codec_plan_check(dn_tmp_0); });
if(({ int32_t dn_tmp_1=(v_checked).f_kind;
int32_t dn_tmp_2=((int32_t)(0ULL));
(dn_tmp_1 != dn_tmp_2); })){
{ dn_r_CodecShape dn_tmp_6=({ int32_t dn_tmp_3=(v_checked).f_kind;
uint32_t dn_tmp_4=(v_checked).f_field;
size_t dn_tmp_5=(v_checked).f_offset;
dn_f_codec_shape_error(dn_tmp_3,dn_tmp_4,dn_tmp_5); });return dn_tmp_6; }
}
else{
}
if(({ uint32_t dn_tmp_7=(v_message)->f_message;
uint32_t dn_tmp_8=(v_plan)->f_message;
(dn_tmp_7 != dn_tmp_8); })){
{ dn_r_CodecShape dn_tmp_12=({ int32_t dn_tmp_9=((int32_t)(11ULL));
uint32_t dn_tmp_10=((uint32_t)(0ULL));
size_t dn_tmp_11=((size_t)(0ULL));
dn_f_codec_shape_error(dn_tmp_9,dn_tmp_10,dn_tmp_11); });return dn_tmp_12; }
}
else{
}
if((!(({ dn_r_CodecPlan const * dn_tmp_13=v_plan;
size_t dn_tmp_14=({ dn_t_buf_r10_CodecValue const * dn_tmp_15=(&((v_message)->f_values));
(*(dn_tmp_15)).len; });
size_t dn_tmp_16=({ dn_t_buf_U8 const * dn_tmp_17=(&((v_message)->f_bytes));
(*(dn_tmp_17)).len; });
size_t dn_tmp_18=({ dn_t_buf_U8 const * dn_tmp_19=(&((v_message)->f_unknown));
(*(dn_tmp_19)).len; });
dn_f_codec_storage(dn_tmp_13,dn_tmp_14,dn_tmp_16,dn_tmp_18); })))){
{ dn_r_CodecShape dn_tmp_23=({ int32_t dn_tmp_20=((int32_t)(7ULL));
uint32_t dn_tmp_21=((uint32_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
dn_f_codec_shape_error(dn_tmp_20,dn_tmp_21,dn_tmp_22); });return dn_tmp_23; }
}
else{
}
dn_r_CodecShape v_result=({ ((dn_r_CodecShape){0}); });
{ size_t dn_tmp_26=({ dn_t_buf_r10_CodecValue const * dn_tmp_24=(&((v_message)->f_values));
(*(dn_tmp_24)).len; });size_t *dn_tmp_25=&((v_result).f_values);*dn_tmp_25=dn_tmp_26; }
{ size_t dn_tmp_29=({ dn_t_buf_U8 const * dn_tmp_27=(&((v_message)->f_bytes));
(*(dn_tmp_27)).len; });size_t *dn_tmp_28=&((v_result).f_bytes);*dn_tmp_28=dn_tmp_29; }
{ size_t dn_tmp_31=((size_t)(4ULL));size_t *dn_tmp_30=&((v_result).f_wire);*dn_tmp_30=dn_tmp_31; }
if(({ uint32_t dn_tmp_32=((v_plan)->f_wire).f_kind;
uint32_t dn_tmp_33=((uint32_t)(2ULL));
(dn_tmp_32 == dn_tmp_33); })){
{ size_t dn_tmp_35=((size_t)(12ULL));size_t *dn_tmp_34=&((v_result).f_wire);*dn_tmp_34=dn_tmp_35; }
}
else{
}
uint32_t v_prior=((uint32_t)(0ULL));
size_t v_position=((size_t)(0ULL));
{ size_t dn_tmp_38=((size_t)(0ULL));size_t dn_tmp_37=({ dn_t_buf_r10_CodecValue const * dn_tmp_36=(&((v_message)->f_values));
(*(dn_tmp_36)).len; });for(size_t v_slot=dn_tmp_38;v_slot<dn_tmp_37;v_slot++){
dn_r_CodecValue const * v_value=(&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_39=&((v_message)->f_values);size_t dn_tmp_40=v_slot;&dn_tmp_39->data[dn_tmp_40]; }))));
size_t v_index=({ dn_r_CodecPlan const * dn_tmp_41=v_plan;
uint32_t dn_tmp_42=(v_value)->f_field;
dn_f_codec_find(dn_tmp_41,dn_tmp_42); });
if((({ size_t dn_tmp_43=v_index;
size_t dn_tmp_44=({ uint32_t dn_tmp_45=(v_plan)->f_field_count;
if((unsigned __int128)dn_tmp_45>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_45; });
(dn_tmp_43 == dn_tmp_44); }) || ({ uint32_t dn_tmp_46=(v_value)->f_field;
uint32_t dn_tmp_47=v_prior;
(dn_tmp_46 < dn_tmp_47); }))){
{ dn_r_CodecShape dn_tmp_51=({ int32_t dn_tmp_48=((int32_t)(4ULL));
uint32_t dn_tmp_49=(v_value)->f_field;
size_t dn_tmp_50=v_slot;
dn_f_codec_shape_error(dn_tmp_48,dn_tmp_49,dn_tmp_50); });return dn_tmp_51; }
}
else{
}
{ uint32_t dn_tmp_53=(v_value)->f_field;uint32_t *dn_tmp_52=&(v_prior);*dn_tmp_52=dn_tmp_53; }
dn_r_MessageField const * v_field=(&((*({ dn_t_arr32_r12_MessageField *dn_tmp_54=&((v_plan)->f_fields);size_t dn_tmp_55=v_index;&dn_tmp_54->data[dn_index(dn_tmp_55,32)]; }))));
if(({ uint32_t dn_tmp_56=(v_value)->f_kind;
uint32_t dn_tmp_57=(v_field)->f_kind;
(dn_tmp_56 != dn_tmp_57); })){
{ dn_r_CodecShape dn_tmp_61=({ int32_t dn_tmp_58=((int32_t)(4ULL));
uint32_t dn_tmp_59=(v_value)->f_field;
size_t dn_tmp_60=v_slot;
dn_f_codec_shape_error(dn_tmp_58,dn_tmp_59,dn_tmp_60); });return dn_tmp_61; }
}
else{
}
{ uint32_t dn_tmp_69=({ uint32_t dn_tmp_64=(*({ dn_t_arr32_U32 *dn_tmp_65=&((v_result).f_counts);size_t dn_tmp_66=v_index;&dn_tmp_65->data[dn_index(dn_tmp_66,32)]; }));
uint32_t dn_tmp_67=((uint32_t)(1ULL));
dn_U32_add(dn_tmp_64,dn_tmp_67); });uint32_t *dn_tmp_68=&((*({ dn_t_arr32_U32 *dn_tmp_62=&((v_result).f_counts);size_t dn_tmp_63=v_index;&dn_tmp_62->data[dn_index(dn_tmp_63,32)]; })));*dn_tmp_68=dn_tmp_69; }
if(({ uint32_t dn_tmp_70=(*({ dn_t_arr32_U32 *dn_tmp_71=&((v_result).f_counts);size_t dn_tmp_72=v_index;&dn_tmp_71->data[dn_index(dn_tmp_72,32)]; }));
uint32_t dn_tmp_73=(v_field)->f_maximum_count;
(dn_tmp_70 > dn_tmp_73); })){
{ dn_r_CodecShape dn_tmp_77=({ int32_t dn_tmp_74=((int32_t)(7ULL));
uint32_t dn_tmp_75=(v_value)->f_field;
size_t dn_tmp_76=v_slot;
dn_f_codec_shape_error(dn_tmp_74,dn_tmp_75,dn_tmp_76); });return dn_tmp_77; }
}
else{
}
size_t v_size=((size_t)(0ULL));
if((({ uint32_t dn_tmp_78=(v_field)->f_kind;
uint32_t dn_tmp_79=((uint32_t)(6ULL));
(dn_tmp_78 == dn_tmp_79); }) || ({ uint32_t dn_tmp_80=(v_field)->f_kind;
uint32_t dn_tmp_81=((uint32_t)(7ULL));
(dn_tmp_80 == dn_tmp_81); }))){
if((((({ uint64_t dn_tmp_82=(v_value)->f_scalar;
uint64_t dn_tmp_83=((uint64_t)(0ULL));
(dn_tmp_82 != dn_tmp_83); }) || ({ size_t dn_tmp_84=(v_value)->f_offset;
size_t dn_tmp_85=v_position;
(dn_tmp_84 != dn_tmp_85); })) || ({ size_t dn_tmp_86=(v_value)->f_count;
size_t dn_tmp_87=({ uint32_t dn_tmp_88=(v_field)->f_maximum_length;
if((unsigned __int128)dn_tmp_88>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_88; });
(dn_tmp_86 > dn_tmp_87); })) || ({ size_t dn_tmp_89=(v_value)->f_count;
size_t dn_tmp_90=({ size_t dn_tmp_91=({ dn_t_buf_U8 const * dn_tmp_92=(&((v_message)->f_bytes));
(*(dn_tmp_92)).len; });
size_t dn_tmp_93=v_position;
dn_USize_sub(dn_tmp_91,dn_tmp_93); });
(dn_tmp_89 > dn_tmp_90); }))){
{ dn_r_CodecShape dn_tmp_97=({ int32_t dn_tmp_94=((int32_t)(4ULL));
uint32_t dn_tmp_95=(v_value)->f_field;
size_t dn_tmp_96=v_slot;
dn_f_codec_shape_error(dn_tmp_94,dn_tmp_95,dn_tmp_96); });return dn_tmp_97; }
}
else{
}
if((({ uint32_t dn_tmp_98=(v_field)->f_kind;
uint32_t dn_tmp_99=((uint32_t)(7ULL));
(dn_tmp_98 == dn_tmp_99); }) && (!(({ dn_t_slice0_U8 dn_tmp_100=({ dn_t_buf_U8 const * dn_tmp_101=(&((v_message)->f_bytes));
size_t dn_tmp_102=((size_t)(0ULL));
size_t dn_tmp_103=({ dn_t_buf_U8 const * dn_tmp_104=(&((v_message)->f_bytes));
(*(dn_tmp_104)).len; });
({ dn_range(dn_tmp_102,dn_tmp_103,(*(dn_tmp_101)).len);(dn_t_slice0_U8){(*(dn_tmp_101)).data?(*(dn_tmp_101)).data+dn_tmp_102:NULL,dn_tmp_103}; }); });
size_t dn_tmp_105=v_position;
size_t dn_tmp_106=(v_value)->f_count;
dn_f_codec_utf8(dn_tmp_100,dn_tmp_105,dn_tmp_106); }))))){
{ dn_r_CodecShape dn_tmp_110=({ int32_t dn_tmp_107=((int32_t)(4ULL));
uint32_t dn_tmp_108=(v_value)->f_field;
size_t dn_tmp_109=v_slot;
dn_f_codec_shape_error(dn_tmp_107,dn_tmp_108,dn_tmp_109); });return dn_tmp_110; }
}
else{
}
{ size_t dn_tmp_114=({ size_t dn_tmp_111=v_position;
size_t dn_tmp_112=(v_value)->f_count;
dn_USize_add(dn_tmp_111,dn_tmp_112); });size_t *dn_tmp_113=&(v_position);*dn_tmp_113=dn_tmp_114; }
{ size_t dn_tmp_122=({ size_t dn_tmp_117=(*({ dn_t_arr32_USize *dn_tmp_118=&((v_result).f_lengths);size_t dn_tmp_119=v_index;&dn_tmp_118->data[dn_index(dn_tmp_119,32)]; }));
size_t dn_tmp_120=(v_value)->f_count;
dn_USize_add(dn_tmp_117,dn_tmp_120); });size_t *dn_tmp_121=&((*({ dn_t_arr32_USize *dn_tmp_115=&((v_result).f_lengths);size_t dn_tmp_116=v_index;&dn_tmp_115->data[dn_index(dn_tmp_116,32)]; })));*dn_tmp_121=dn_tmp_122; }
{ size_t dn_tmp_124=(v_value)->f_count;size_t *dn_tmp_123=&(v_size);*dn_tmp_123=dn_tmp_124; }
if(({ uint32_t dn_tmp_125=((v_plan)->f_wire).f_kind;
uint32_t dn_tmp_126=((uint32_t)(1ULL));
(dn_tmp_125 == dn_tmp_126); })){
{ size_t dn_tmp_130=({ size_t dn_tmp_127=v_size;
size_t dn_tmp_128=((size_t)(4ULL));
dn_USize_add(dn_tmp_127,dn_tmp_128); });size_t *dn_tmp_129=&(v_size);*dn_tmp_129=dn_tmp_130; }
}
else{
}
}
else{
if(((({ size_t dn_tmp_131=(v_value)->f_offset;
size_t dn_tmp_132=((size_t)(0ULL));
(dn_tmp_131 != dn_tmp_132); }) || ({ size_t dn_tmp_133=(v_value)->f_count;
size_t dn_tmp_134=((size_t)(0ULL));
(dn_tmp_133 != dn_tmp_134); })) || (!(({ uint32_t dn_tmp_135=(v_field)->f_kind;
uint64_t dn_tmp_136=(v_value)->f_scalar;
dn_f_codec_scalar_valid(dn_tmp_135,dn_tmp_136); }))))){
{ dn_r_CodecShape dn_tmp_140=({ int32_t dn_tmp_137=((int32_t)(4ULL));
uint32_t dn_tmp_138=(v_value)->f_field;
size_t dn_tmp_139=v_slot;
dn_f_codec_shape_error(dn_tmp_137,dn_tmp_138,dn_tmp_139); });return dn_tmp_140; }
}
else{
}
{ size_t dn_tmp_144=({ uint32_t dn_tmp_141=(v_field)->f_kind;
uint64_t dn_tmp_142=(v_value)->f_scalar;
dn_f_codec_scalar_size(dn_tmp_141,dn_tmp_142); });size_t *dn_tmp_143=&(v_size);*dn_tmp_143=dn_tmp_144; }
}
if(({ uint32_t dn_tmp_145=((v_plan)->f_wire).f_kind;
uint32_t dn_tmp_146=((uint32_t)(2ULL));
(dn_tmp_145 == dn_tmp_146); })){
{ size_t dn_tmp_150=({ size_t dn_tmp_147=v_size;
size_t dn_tmp_148=((size_t)(8ULL));
dn_USize_add(dn_tmp_147,dn_tmp_148); });size_t *dn_tmp_149=&(v_size);*dn_tmp_149=dn_tmp_150; }
}
else{
}
{ size_t dn_tmp_154=({ size_t dn_tmp_151=(v_result).f_wire;
size_t dn_tmp_152=v_size;
dn_USize_add(dn_tmp_151,dn_tmp_152); });size_t *dn_tmp_153=&((v_result).f_wire);*dn_tmp_153=dn_tmp_154; }
}
 }
if(({ size_t dn_tmp_155=v_position;
size_t dn_tmp_156=({ dn_t_buf_U8 const * dn_tmp_157=(&((v_message)->f_bytes));
(*(dn_tmp_157)).len; });
(dn_tmp_155 != dn_tmp_156); })){
{ dn_r_CodecShape dn_tmp_161=({ int32_t dn_tmp_158=((int32_t)(4ULL));
uint32_t dn_tmp_159=((uint32_t)(0ULL));
size_t dn_tmp_160=v_position;
dn_f_codec_shape_error(dn_tmp_158,dn_tmp_159,dn_tmp_160); });return dn_tmp_161; }
}
else{
}
if(({ uint32_t dn_tmp_162=((v_plan)->f_wire).f_kind;
uint32_t dn_tmp_163=((uint32_t)(1ULL));
(dn_tmp_162 == dn_tmp_163); })){
{ size_t dn_tmp_166=((size_t)(0ULL));size_t dn_tmp_165=({ uint32_t dn_tmp_164=(v_plan)->f_field_count;
if((unsigned __int128)dn_tmp_164>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_164; });for(size_t v_index=dn_tmp_166;v_index<dn_tmp_165;v_index++){
if((({ uint32_t dn_tmp_167=((*({ dn_t_arr32_r12_MessageField *dn_tmp_168=&((v_plan)->f_fields);size_t dn_tmp_169=v_index;&dn_tmp_168->data[dn_index(dn_tmp_169,32)]; }))).f_minimum_count;
uint32_t dn_tmp_170=((uint32_t)(1ULL));
(dn_tmp_167 != dn_tmp_170); }) || ({ uint32_t dn_tmp_171=((*({ dn_t_arr32_r12_MessageField *dn_tmp_172=&((v_plan)->f_fields);size_t dn_tmp_173=v_index;&dn_tmp_172->data[dn_index(dn_tmp_173,32)]; }))).f_maximum_count;
uint32_t dn_tmp_174=((uint32_t)(1ULL));
(dn_tmp_171 != dn_tmp_174); }))){
{ size_t dn_tmp_178=({ size_t dn_tmp_175=(v_result).f_wire;
size_t dn_tmp_176=((size_t)(4ULL));
dn_USize_add(dn_tmp_175,dn_tmp_176); });size_t *dn_tmp_177=&((v_result).f_wire);*dn_tmp_177=dn_tmp_178; }
}
else{
}
}
 }
}
else{
}
dn_r_CodecShape v_unknown=({ dn_r_CodecPlan const * dn_tmp_179=v_plan;
dn_t_slice0_U8 dn_tmp_180=({ dn_t_buf_U8 const * dn_tmp_181=(&((v_message)->f_unknown));
size_t dn_tmp_182=((size_t)(0ULL));
size_t dn_tmp_183=({ dn_t_buf_U8 const * dn_tmp_184=(&((v_message)->f_unknown));
(*(dn_tmp_184)).len; });
({ dn_range(dn_tmp_182,dn_tmp_183,(*(dn_tmp_181)).len);(dn_t_slice0_U8){(*(dn_tmp_181)).data?(*(dn_tmp_181)).data+dn_tmp_182:NULL,dn_tmp_183}; }); });
dn_f_codec_unknown_shape(dn_tmp_179,dn_tmp_180); });
if(({ int32_t dn_tmp_185=((v_unknown).f_error).f_kind;
int32_t dn_tmp_186=((int32_t)(0ULL));
(dn_tmp_185 != dn_tmp_186); })){
{ dn_r_CodecShape dn_tmp_187=v_unknown;return dn_tmp_187; }
}
else{
}
{ size_t dn_tmp_189=(v_unknown).f_unknown;size_t *dn_tmp_188=&((v_result).f_unknown);*dn_tmp_188=dn_tmp_189; }
{ uint32_t dn_tmp_191=(v_unknown).f_unknown_fields;uint32_t *dn_tmp_190=&((v_result).f_unknown_fields);*dn_tmp_190=dn_tmp_191; }
{ size_t dn_tmp_195=({ size_t dn_tmp_192=(v_result).f_wire;
size_t dn_tmp_193=(v_result).f_unknown;
dn_USize_add(dn_tmp_192,dn_tmp_193); });size_t *dn_tmp_194=&((v_result).f_wire);*dn_tmp_194=dn_tmp_195; }
if(({ size_t dn_tmp_196=(v_result).f_wire;
size_t dn_tmp_197=(v_plan)->f_maximum_wire;
(dn_tmp_196 > dn_tmp_197); })){
{ dn_r_CodecShape dn_tmp_201=({ int32_t dn_tmp_198=((int32_t)(7ULL));
uint32_t dn_tmp_199=((uint32_t)(0ULL));
size_t dn_tmp_200=((size_t)(0ULL));
dn_f_codec_shape_error(dn_tmp_198,dn_tmp_199,dn_tmp_200); });return dn_tmp_201; }
}
else{
}
if(v_complete){
dn_r_CodecError v_required=({ dn_r_CodecPlan const * dn_tmp_202=v_plan;
dn_r_CodecShape const * dn_tmp_203=(&(v_result));
dn_f_codec_shape_complete(dn_tmp_202,dn_tmp_203); });
if(({ int32_t dn_tmp_204=(v_required).f_kind;
int32_t dn_tmp_205=((int32_t)(0ULL));
(dn_tmp_204 != dn_tmp_205); })){
{ dn_r_CodecShape dn_tmp_209=({ int32_t dn_tmp_206=(v_required).f_kind;
uint32_t dn_tmp_207=(v_required).f_field;
size_t dn_tmp_208=(v_required).f_offset;
dn_f_codec_shape_error(dn_tmp_206,dn_tmp_207,dn_tmp_208); });return dn_tmp_209; }
}
else{
}
}
else{
}
{ dn_r_CodecShape dn_tmp_210=v_result;return dn_tmp_210; }
}

dn_fail("missing native return");
}
static dn_r_CodecShape dn_cb_codec_message_inspect(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,bool v_complete) {
 if(!dn_enter()){return (dn_r_CodecShape){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecShape){0};}
 dn_r_CodecShape dn_result=dn_f_codec_message_inspect(v_plan,v_message,v_complete);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecIo dn_f_codec_measure(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message) {
{
dn_r_CodecShape v_shape=({ dn_r_CodecPlan const * dn_tmp_0=v_plan;
dn_r_CodecMessage const * dn_tmp_1=v_message;
bool dn_tmp_2=1;
dn_f_codec_message_inspect(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
if(({ int32_t dn_tmp_3=((v_shape).f_error).f_kind;
int32_t dn_tmp_4=((int32_t)(0ULL));
(dn_tmp_3 != dn_tmp_4); })){
{ dn_r_CodecIo dn_tmp_8=({ size_t dn_tmp_5=((size_t)(0ULL));
size_t dn_tmp_6=((size_t)(0ULL));
dn_r_CodecError dn_tmp_7=(v_shape).f_error;
((dn_r_CodecIo){dn_tmp_5,dn_tmp_6,dn_tmp_7}); });return dn_tmp_8; }
}
else{
}
{ dn_r_CodecIo dn_tmp_15=({ size_t dn_tmp_9=(v_shape).f_wire;
size_t dn_tmp_10=(v_shape).f_wire;
dn_r_CodecError dn_tmp_11=({ int32_t dn_tmp_12=((int32_t)(0ULL));
uint32_t dn_tmp_13=((uint32_t)(0ULL));
size_t dn_tmp_14=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_12,dn_tmp_13,dn_tmp_14}); });
((dn_r_CodecIo){dn_tmp_9,dn_tmp_10,dn_tmp_11}); });return dn_tmp_15; }
}

dn_fail("missing native return");
}
static dn_r_CodecIo dn_cb_codec_measure(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message) {
 if(!dn_enter()){return (dn_r_CodecIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecIo){0};}
 dn_r_CodecIo dn_result=dn_f_codec_measure(v_plan,v_message);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecReservation dn_f_codec_reserve_scalars(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage * v_message,uint32_t v_field_id,size_t v_count) {
{
dn_r_CodecShape v_shape=({ dn_r_CodecPlan const * dn_tmp_0=v_plan;
dn_r_CodecMessage * dn_tmp_1=v_message;
bool dn_tmp_2=0;
dn_f_codec_message_inspect(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
if(({ int32_t dn_tmp_3=((v_shape).f_error).f_kind;
int32_t dn_tmp_4=((int32_t)(0ULL));
(dn_tmp_3 != dn_tmp_4); })){
{ dn_r_CodecReservation dn_tmp_8=({ size_t dn_tmp_5=((size_t)(0ULL));
size_t dn_tmp_6=((size_t)(0ULL));
dn_r_CodecError dn_tmp_7=(v_shape).f_error;
((dn_r_CodecReservation){dn_tmp_5,dn_tmp_6,dn_tmp_7}); });return dn_tmp_8; }
}
else{
}
size_t v_index=({ dn_r_CodecPlan const * dn_tmp_9=v_plan;
uint32_t dn_tmp_10=v_field_id;
dn_f_codec_find(dn_tmp_9,dn_tmp_10); });
if(({ size_t dn_tmp_11=v_index;
size_t dn_tmp_12=({ uint32_t dn_tmp_13=(v_plan)->f_field_count;
if((unsigned __int128)dn_tmp_13>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_13; });
(dn_tmp_11 == dn_tmp_12); })){
{ dn_r_CodecReservation dn_tmp_20=({ size_t dn_tmp_14=((size_t)(0ULL));
size_t dn_tmp_15=((size_t)(0ULL));
dn_r_CodecError dn_tmp_16=({ int32_t dn_tmp_17=((int32_t)(4ULL));
uint32_t dn_tmp_18=v_field_id;
size_t dn_tmp_19=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_17,dn_tmp_18,dn_tmp_19}); });
((dn_r_CodecReservation){dn_tmp_14,dn_tmp_15,dn_tmp_16}); });return dn_tmp_20; }
}
else{
}
dn_r_MessageField const * v_field=(&((*({ dn_t_arr32_r12_MessageField *dn_tmp_21=&((v_plan)->f_fields);size_t dn_tmp_22=v_index;&dn_tmp_21->data[dn_index(dn_tmp_22,32)]; }))));
if((({ uint32_t dn_tmp_23=(v_field)->f_kind;
uint32_t dn_tmp_24=((uint32_t)(6ULL));
(dn_tmp_23 == dn_tmp_24); }) || ({ uint32_t dn_tmp_25=(v_field)->f_kind;
uint32_t dn_tmp_26=((uint32_t)(7ULL));
(dn_tmp_25 == dn_tmp_26); }))){
{ dn_r_CodecReservation dn_tmp_33=({ size_t dn_tmp_27=((size_t)(0ULL));
size_t dn_tmp_28=((size_t)(0ULL));
dn_r_CodecError dn_tmp_29=({ int32_t dn_tmp_30=((int32_t)(4ULL));
uint32_t dn_tmp_31=v_field_id;
size_t dn_tmp_32=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_30,dn_tmp_31,dn_tmp_32}); });
((dn_r_CodecReservation){dn_tmp_27,dn_tmp_28,dn_tmp_29}); });return dn_tmp_33; }
}
else{
}
size_t v_start=({ dn_t_buf_r10_CodecValue const * dn_tmp_34=(&((v_message)->f_values));
(*(dn_tmp_34)).len; });
if(({ size_t dn_tmp_35=v_start;
size_t dn_tmp_36=((size_t)(0ULL));
(dn_tmp_35 != dn_tmp_36); })){
if(({ uint32_t dn_tmp_37=((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_38=&((v_message)->f_values);size_t dn_tmp_39=({ size_t dn_tmp_40=v_start;
size_t dn_tmp_41=((size_t)(1ULL));
dn_USize_sub(dn_tmp_40,dn_tmp_41); });&dn_tmp_38->data[dn_index(dn_tmp_39,dn_tmp_38->len)]; }))).f_field;
uint32_t dn_tmp_42=v_field_id;
(dn_tmp_37 > dn_tmp_42); })){
{ dn_r_CodecReservation dn_tmp_49=({ size_t dn_tmp_43=((size_t)(0ULL));
size_t dn_tmp_44=((size_t)(0ULL));
dn_r_CodecError dn_tmp_45=({ int32_t dn_tmp_46=((int32_t)(4ULL));
uint32_t dn_tmp_47=v_field_id;
size_t dn_tmp_48=v_start;
((dn_r_CodecError){dn_tmp_46,dn_tmp_47,dn_tmp_48}); });
((dn_r_CodecReservation){dn_tmp_43,dn_tmp_44,dn_tmp_45}); });return dn_tmp_49; }
}
else{
}
}
else{
}
if((({ size_t dn_tmp_50=v_count;
size_t dn_tmp_51=({ uint32_t dn_tmp_52=({ uint32_t dn_tmp_53=(v_field)->f_maximum_count;
uint32_t dn_tmp_54=(*({ dn_t_arr32_U32 *dn_tmp_55=&((v_shape).f_counts);size_t dn_tmp_56=v_index;&dn_tmp_55->data[dn_index(dn_tmp_56,32)]; }));
dn_U32_sub(dn_tmp_53,dn_tmp_54); });
if((unsigned __int128)dn_tmp_52>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_52; });
(dn_tmp_50 > dn_tmp_51); }) || (!(({ dn_r_CodecPlan const * dn_tmp_57=v_plan;
size_t dn_tmp_58=({ size_t dn_tmp_59=v_start;
size_t dn_tmp_60=v_count;
dn_USize_add(dn_tmp_59,dn_tmp_60); });
size_t dn_tmp_61=({ dn_t_buf_U8 const * dn_tmp_62=(&((v_message)->f_bytes));
(*(dn_tmp_62)).len; });
size_t dn_tmp_63=({ dn_t_buf_U8 const * dn_tmp_64=(&((v_message)->f_unknown));
(*(dn_tmp_64)).len; });
dn_f_codec_storage(dn_tmp_57,dn_tmp_58,dn_tmp_61,dn_tmp_63); }))))){
{ dn_r_CodecReservation dn_tmp_71=({ size_t dn_tmp_65=((size_t)(0ULL));
size_t dn_tmp_66=((size_t)(0ULL));
dn_r_CodecError dn_tmp_67=({ int32_t dn_tmp_68=((int32_t)(7ULL));
uint32_t dn_tmp_69=v_field_id;
size_t dn_tmp_70=v_start;
((dn_r_CodecError){dn_tmp_68,dn_tmp_69,dn_tmp_70}); });
((dn_r_CodecReservation){dn_tmp_65,dn_tmp_66,dn_tmp_67}); });return dn_tmp_71; }
}
else{
}
(void)(({ dn_t_buf_r10_CodecValue * dn_tmp_72=(&((v_message)->f_values));
size_t dn_tmp_73=({ size_t dn_tmp_74=v_start;
size_t dn_tmp_75=v_count;
dn_USize_add(dn_tmp_74,dn_tmp_75); });
dn_resize_buf_r10_CodecValue(dn_tmp_72,dn_tmp_73); }));
{ dn_r_CodecReservation dn_tmp_82=({ size_t dn_tmp_76=v_start;
size_t dn_tmp_77=v_count;
dn_r_CodecError dn_tmp_78=({ int32_t dn_tmp_79=((int32_t)(0ULL));
uint32_t dn_tmp_80=((uint32_t)(0ULL));
size_t dn_tmp_81=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_79,dn_tmp_80,dn_tmp_81}); });
((dn_r_CodecReservation){dn_tmp_76,dn_tmp_77,dn_tmp_78}); });return dn_tmp_82; }
}

dn_fail("missing native return");
}
static dn_r_CodecReservation dn_cb_codec_reserve_scalars(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage * v_message,uint32_t v_field_id,size_t v_count) {
 if(!dn_enter()){return (dn_r_CodecReservation){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecReservation){0};}
 dn_r_CodecReservation dn_result=dn_f_codec_reserve_scalars(v_plan,v_message,v_field_id,v_count);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_codec_add_scalar(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage * v_message,uint32_t v_field_id,uint64_t v_scalar) {
{
size_t v_index=({ dn_r_CodecPlan const * dn_tmp_0=v_plan;
uint32_t dn_tmp_1=v_field_id;
dn_f_codec_find(dn_tmp_0,dn_tmp_1); });
if((({ size_t dn_tmp_2=v_index;
size_t dn_tmp_3=({ uint32_t dn_tmp_4=(v_plan)->f_field_count;
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_4; });
(dn_tmp_2 == dn_tmp_3); }) || ({ uint32_t dn_tmp_5=(v_plan)->f_field_count;
uint32_t dn_tmp_6=((uint32_t)(32ULL));
(dn_tmp_5 > dn_tmp_6); }))){
{ dn_r_CodecError dn_tmp_10=({ int32_t dn_tmp_7=((int32_t)(4ULL));
uint32_t dn_tmp_8=v_field_id;
size_t dn_tmp_9=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_7,dn_tmp_8,dn_tmp_9}); });return dn_tmp_10; }
}
else{
}
uint32_t v_kind=((*({ dn_t_arr32_r12_MessageField *dn_tmp_11=&((v_plan)->f_fields);size_t dn_tmp_12=v_index;&dn_tmp_11->data[dn_index(dn_tmp_12,32)]; }))).f_kind;
if((!(({ uint32_t dn_tmp_13=v_kind;
uint64_t dn_tmp_14=v_scalar;
dn_f_codec_scalar_valid(dn_tmp_13,dn_tmp_14); })))){
{ dn_r_CodecError dn_tmp_18=({ int32_t dn_tmp_15=((int32_t)(4ULL));
uint32_t dn_tmp_16=v_field_id;
size_t dn_tmp_17=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_15,dn_tmp_16,dn_tmp_17}); });return dn_tmp_18; }
}
else{
}
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_19=v_plan;
dn_r_CodecMessage * dn_tmp_20=v_message;
uint32_t dn_tmp_21=v_field_id;
size_t dn_tmp_22=((size_t)(1ULL));
dn_f_codec_reserve_scalars(dn_tmp_19,dn_tmp_20,dn_tmp_21,dn_tmp_22); });
if(({ int32_t dn_tmp_23=((v_reserved).f_error).f_kind;
int32_t dn_tmp_24=((int32_t)(0ULL));
(dn_tmp_23 != dn_tmp_24); })){
{ dn_r_CodecError dn_tmp_25=(v_reserved).f_error;return dn_tmp_25; }
}
else{
}
{ dn_r_CodecValue dn_tmp_34=({ uint32_t dn_tmp_28=v_field_id;
uint32_t dn_tmp_29=v_kind;
uint64_t dn_tmp_30=v_scalar;
size_t dn_tmp_31=((size_t)(0ULL));
size_t dn_tmp_32=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_28,dn_tmp_29,dn_tmp_30,dn_tmp_31,dn_tmp_32}); });dn_r_CodecValue *dn_tmp_33=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_26=&((v_message)->f_values);size_t dn_tmp_27=(v_reserved).f_start;&dn_tmp_26->data[dn_index(dn_tmp_27,dn_tmp_26->len)]; })));*dn_tmp_33=dn_tmp_34; }
{ dn_r_CodecError dn_tmp_38=({ int32_t dn_tmp_35=((int32_t)(0ULL));
uint32_t dn_tmp_36=((uint32_t)(0ULL));
size_t dn_tmp_37=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_35,dn_tmp_36,dn_tmp_37}); });return dn_tmp_38; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_codec_add_scalar(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage * v_message,uint32_t v_field_id,uint64_t v_scalar) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_codec_add_scalar(v_plan,v_message,v_field_id,v_scalar);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_codec_add_bytes(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage * v_message,uint32_t v_field_id,dn_t_slice0_U8 v_input) {
{
dn_r_CodecShape v_shape=({ dn_r_CodecPlan const * dn_tmp_0=v_plan;
dn_r_CodecMessage * dn_tmp_1=v_message;
bool dn_tmp_2=0;
dn_f_codec_message_inspect(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
if(({ int32_t dn_tmp_3=((v_shape).f_error).f_kind;
int32_t dn_tmp_4=((int32_t)(0ULL));
(dn_tmp_3 != dn_tmp_4); })){
{ dn_r_CodecError dn_tmp_5=(v_shape).f_error;return dn_tmp_5; }
}
else{
}
size_t v_index=({ dn_r_CodecPlan const * dn_tmp_6=v_plan;
uint32_t dn_tmp_7=v_field_id;
dn_f_codec_find(dn_tmp_6,dn_tmp_7); });
if(({ size_t dn_tmp_8=v_index;
size_t dn_tmp_9=({ uint32_t dn_tmp_10=(v_plan)->f_field_count;
if((unsigned __int128)dn_tmp_10>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_10; });
(dn_tmp_8 == dn_tmp_9); })){
{ dn_r_CodecError dn_tmp_14=({ int32_t dn_tmp_11=((int32_t)(4ULL));
uint32_t dn_tmp_12=v_field_id;
size_t dn_tmp_13=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_11,dn_tmp_12,dn_tmp_13}); });return dn_tmp_14; }
}
else{
}
dn_r_MessageField const * v_field=(&((*({ dn_t_arr32_r12_MessageField *dn_tmp_15=&((v_plan)->f_fields);size_t dn_tmp_16=v_index;&dn_tmp_15->data[dn_index(dn_tmp_16,32)]; }))));
if(((({ uint32_t dn_tmp_17=(v_field)->f_kind;
uint32_t dn_tmp_18=((uint32_t)(6ULL));
(dn_tmp_17 != dn_tmp_18); }) && ({ uint32_t dn_tmp_19=(v_field)->f_kind;
uint32_t dn_tmp_20=((uint32_t)(7ULL));
(dn_tmp_19 != dn_tmp_20); })) || ({ size_t dn_tmp_21=({ dn_t_slice0_U8 dn_tmp_22=v_input;
(dn_tmp_22).len; });
size_t dn_tmp_23=({ uint32_t dn_tmp_24=(v_field)->f_maximum_length;
if((unsigned __int128)dn_tmp_24>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_24; });
(dn_tmp_21 > dn_tmp_23); }))){
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(4ULL));
uint32_t dn_tmp_26=v_field_id;
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}
else{
}
if((({ uint32_t dn_tmp_29=(v_field)->f_kind;
uint32_t dn_tmp_30=((uint32_t)(7ULL));
(dn_tmp_29 == dn_tmp_30); }) && (!(({ dn_t_slice0_U8 dn_tmp_31=v_input;
size_t dn_tmp_32=((size_t)(0ULL));
size_t dn_tmp_33=({ dn_t_slice0_U8 dn_tmp_34=v_input;
(dn_tmp_34).len; });
dn_f_codec_utf8(dn_tmp_31,dn_tmp_32,dn_tmp_33); }))))){
{ dn_r_CodecError dn_tmp_38=({ int32_t dn_tmp_35=((int32_t)(4ULL));
uint32_t dn_tmp_36=v_field_id;
size_t dn_tmp_37=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_35,dn_tmp_36,dn_tmp_37}); });return dn_tmp_38; }
}
else{
}
size_t v_start=({ dn_t_buf_r10_CodecValue const * dn_tmp_39=(&((v_message)->f_values));
(*(dn_tmp_39)).len; });
size_t v_offset=({ dn_t_buf_U8 const * dn_tmp_40=(&((v_message)->f_bytes));
(*(dn_tmp_40)).len; });
if(({ size_t dn_tmp_41=v_start;
size_t dn_tmp_42=((size_t)(0ULL));
(dn_tmp_41 != dn_tmp_42); })){
if(({ uint32_t dn_tmp_43=((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_44=&((v_message)->f_values);size_t dn_tmp_45=({ size_t dn_tmp_46=v_start;
size_t dn_tmp_47=((size_t)(1ULL));
dn_USize_sub(dn_tmp_46,dn_tmp_47); });&dn_tmp_44->data[dn_index(dn_tmp_45,dn_tmp_44->len)]; }))).f_field;
uint32_t dn_tmp_48=v_field_id;
(dn_tmp_43 > dn_tmp_48); })){
{ dn_r_CodecError dn_tmp_52=({ int32_t dn_tmp_49=((int32_t)(4ULL));
uint32_t dn_tmp_50=v_field_id;
size_t dn_tmp_51=v_start;
((dn_r_CodecError){dn_tmp_49,dn_tmp_50,dn_tmp_51}); });return dn_tmp_52; }
}
else{
}
}
else{
}
if((({ uint32_t dn_tmp_53=(*({ dn_t_arr32_U32 *dn_tmp_54=&((v_shape).f_counts);size_t dn_tmp_55=v_index;&dn_tmp_54->data[dn_index(dn_tmp_55,32)]; }));
uint32_t dn_tmp_56=(v_field)->f_maximum_count;
(dn_tmp_53 == dn_tmp_56); }) || (!(({ dn_r_CodecPlan const * dn_tmp_57=v_plan;
size_t dn_tmp_58=({ size_t dn_tmp_59=v_start;
size_t dn_tmp_60=((size_t)(1ULL));
dn_USize_add(dn_tmp_59,dn_tmp_60); });
size_t dn_tmp_61=({ size_t dn_tmp_62=v_offset;
size_t dn_tmp_63=({ dn_t_slice0_U8 dn_tmp_64=v_input;
(dn_tmp_64).len; });
dn_USize_add(dn_tmp_62,dn_tmp_63); });
size_t dn_tmp_65=({ dn_t_buf_U8 const * dn_tmp_66=(&((v_message)->f_unknown));
(*(dn_tmp_66)).len; });
dn_f_codec_storage(dn_tmp_57,dn_tmp_58,dn_tmp_61,dn_tmp_65); }))))){
{ dn_r_CodecError dn_tmp_70=({ int32_t dn_tmp_67=((int32_t)(7ULL));
uint32_t dn_tmp_68=v_field_id;
size_t dn_tmp_69=v_start;
((dn_r_CodecError){dn_tmp_67,dn_tmp_68,dn_tmp_69}); });return dn_tmp_70; }
}
else{
}
(void)(({ dn_t_buf_U8 * dn_tmp_71=(&((v_message)->f_bytes));
size_t dn_tmp_72=({ size_t dn_tmp_73=v_offset;
size_t dn_tmp_74=({ dn_t_slice0_U8 dn_tmp_75=v_input;
(dn_tmp_75).len; });
dn_USize_add(dn_tmp_73,dn_tmp_74); });
dn_resize_buf_U8(dn_tmp_71,dn_tmp_72); }));
{ size_t dn_tmp_78=((size_t)(0ULL));size_t dn_tmp_77=({ dn_t_slice0_U8 dn_tmp_76=v_input;
(dn_tmp_76).len; });for(size_t v_b=dn_tmp_78;v_b<dn_tmp_77;v_b++){
{ uint8_t dn_tmp_86=(*({ dn_t_slice0_U8 *dn_tmp_83=&(v_input);size_t dn_tmp_84=v_b;&dn_tmp_83->data[dn_tmp_84]; }));uint8_t *dn_tmp_85=&((*({ dn_t_buf_U8 volatile *dn_tmp_79=&((v_message)->f_bytes);size_t dn_tmp_80=({ size_t dn_tmp_81=v_offset;
size_t dn_tmp_82=v_b;
dn_USize_add(dn_tmp_81,dn_tmp_82); });&dn_tmp_79->data[dn_index(dn_tmp_80,dn_tmp_79->len)]; })));*dn_tmp_85=dn_tmp_86; }
}
 }
(void)(({ dn_t_buf_r10_CodecValue * dn_tmp_87=(&((v_message)->f_values));
size_t dn_tmp_88=({ size_t dn_tmp_89=v_start;
size_t dn_tmp_90=((size_t)(1ULL));
dn_USize_add(dn_tmp_89,dn_tmp_90); });
dn_resize_buf_r10_CodecValue(dn_tmp_87,dn_tmp_88); }));
{ dn_r_CodecValue dn_tmp_100=({ uint32_t dn_tmp_93=v_field_id;
uint32_t dn_tmp_94=(v_field)->f_kind;
uint64_t dn_tmp_95=((uint64_t)(0ULL));
size_t dn_tmp_96=v_offset;
size_t dn_tmp_97=({ dn_t_slice0_U8 dn_tmp_98=v_input;
(dn_tmp_98).len; });
((dn_r_CodecValue){dn_tmp_93,dn_tmp_94,dn_tmp_95,dn_tmp_96,dn_tmp_97}); });dn_r_CodecValue *dn_tmp_99=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_91=&((v_message)->f_values);size_t dn_tmp_92=v_start;&dn_tmp_91->data[dn_index(dn_tmp_92,dn_tmp_91->len)]; })));*dn_tmp_99=dn_tmp_100; }
{ dn_r_CodecError dn_tmp_104=({ int32_t dn_tmp_101=((int32_t)(0ULL));
uint32_t dn_tmp_102=((uint32_t)(0ULL));
size_t dn_tmp_103=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_101,dn_tmp_102,dn_tmp_103}); });return dn_tmp_104; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_codec_add_bytes(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage * v_message,uint32_t v_field_id,dn_t_slice0_U8 v_input) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_codec_add_bytes(v_plan,v_message,v_field_id,v_input);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_codec_add_unknown(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage * v_message,dn_t_slice0_U8 v_input) {
{
dn_r_CodecShape v_shape=({ dn_r_CodecPlan const * dn_tmp_0=v_plan;
dn_r_CodecMessage * dn_tmp_1=v_message;
bool dn_tmp_2=0;
dn_f_codec_message_inspect(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
if(({ int32_t dn_tmp_3=((v_shape).f_error).f_kind;
int32_t dn_tmp_4=((int32_t)(0ULL));
(dn_tmp_3 != dn_tmp_4); })){
{ dn_r_CodecError dn_tmp_5=(v_shape).f_error;return dn_tmp_5; }
}
else{
}
dn_r_CodecShape v_extra=({ dn_r_CodecPlan const * dn_tmp_6=v_plan;
dn_t_slice0_U8 dn_tmp_7=v_input;
dn_f_codec_unknown_shape(dn_tmp_6,dn_tmp_7); });
if(({ int32_t dn_tmp_8=((v_extra).f_error).f_kind;
int32_t dn_tmp_9=((int32_t)(0ULL));
(dn_tmp_8 != dn_tmp_9); })){
{ dn_r_CodecError dn_tmp_10=(v_extra).f_error;return dn_tmp_10; }
}
else{
}
size_t v_offset=({ dn_t_buf_U8 const * dn_tmp_11=(&((v_message)->f_unknown));
(*(dn_tmp_11)).len; });
if((({ uint32_t dn_tmp_12=(v_extra).f_unknown_fields;
uint32_t dn_tmp_13=({ uint32_t dn_tmp_14=(v_plan)->f_maximum_unknown_fields;
uint32_t dn_tmp_15=(v_shape).f_unknown_fields;
dn_U32_sub(dn_tmp_14,dn_tmp_15); });
(dn_tmp_12 > dn_tmp_13); }) || ({ dn_r_CodecPlan const * dn_tmp_16=v_plan;
size_t dn_tmp_17=v_offset;
size_t dn_tmp_18=({ dn_t_slice0_U8 dn_tmp_19=v_input;
(dn_tmp_19).len; });
dn_f_codec_unknown_over_budget(dn_tmp_16,dn_tmp_17,dn_tmp_18); }))){
{ dn_r_CodecError dn_tmp_23=({ int32_t dn_tmp_20=((int32_t)(7ULL));
uint32_t dn_tmp_21=((uint32_t)(0ULL));
size_t dn_tmp_22=v_offset;
((dn_r_CodecError){dn_tmp_20,dn_tmp_21,dn_tmp_22}); });return dn_tmp_23; }
}
else{
}
if((!(({ dn_r_CodecPlan const * dn_tmp_24=v_plan;
size_t dn_tmp_25=({ dn_t_buf_r10_CodecValue const * dn_tmp_26=(&((v_message)->f_values));
(*(dn_tmp_26)).len; });
size_t dn_tmp_27=({ dn_t_buf_U8 const * dn_tmp_28=(&((v_message)->f_bytes));
(*(dn_tmp_28)).len; });
size_t dn_tmp_29=({ size_t dn_tmp_30=v_offset;
size_t dn_tmp_31=({ dn_t_slice0_U8 dn_tmp_32=v_input;
(dn_tmp_32).len; });
dn_USize_add(dn_tmp_30,dn_tmp_31); });
dn_f_codec_storage(dn_tmp_24,dn_tmp_25,dn_tmp_27,dn_tmp_29); })))){
{ dn_r_CodecError dn_tmp_36=({ int32_t dn_tmp_33=((int32_t)(7ULL));
uint32_t dn_tmp_34=((uint32_t)(0ULL));
size_t dn_tmp_35=v_offset;
((dn_r_CodecError){dn_tmp_33,dn_tmp_34,dn_tmp_35}); });return dn_tmp_36; }
}
else{
}
(void)(({ dn_t_buf_U8 * dn_tmp_37=(&((v_message)->f_unknown));
size_t dn_tmp_38=({ size_t dn_tmp_39=v_offset;
size_t dn_tmp_40=({ dn_t_slice0_U8 dn_tmp_41=v_input;
(dn_tmp_41).len; });
dn_USize_add(dn_tmp_39,dn_tmp_40); });
dn_resize_buf_U8(dn_tmp_37,dn_tmp_38); }));
{ size_t dn_tmp_44=((size_t)(0ULL));size_t dn_tmp_43=({ dn_t_slice0_U8 dn_tmp_42=v_input;
(dn_tmp_42).len; });for(size_t v_b=dn_tmp_44;v_b<dn_tmp_43;v_b++){
{ uint8_t dn_tmp_52=(*({ dn_t_slice0_U8 *dn_tmp_49=&(v_input);size_t dn_tmp_50=v_b;&dn_tmp_49->data[dn_tmp_50]; }));uint8_t *dn_tmp_51=&((*({ dn_t_buf_U8 volatile *dn_tmp_45=&((v_message)->f_unknown);size_t dn_tmp_46=({ size_t dn_tmp_47=v_offset;
size_t dn_tmp_48=v_b;
dn_USize_add(dn_tmp_47,dn_tmp_48); });&dn_tmp_45->data[dn_index(dn_tmp_46,dn_tmp_45->len)]; })));*dn_tmp_51=dn_tmp_52; }
}
 }
{ dn_r_CodecError dn_tmp_56=({ int32_t dn_tmp_53=((int32_t)(0ULL));
uint32_t dn_tmp_54=((uint32_t)(0ULL));
size_t dn_tmp_55=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_53,dn_tmp_54,dn_tmp_55}); });return dn_tmp_56; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_codec_add_unknown(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage * v_message,dn_t_slice0_U8 v_input) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_codec_add_unknown(v_plan,v_message,v_input);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_codec_unknown_over_budget(dn_r_CodecPlan const * v_plan,size_t v_used,size_t v_count) {
{
{ bool dn_tmp_6=(({ size_t dn_tmp_0=v_used;
size_t dn_tmp_1=(v_plan)->f_maximum_unknown_bytes;
(dn_tmp_0 > dn_tmp_1); }) || ({ size_t dn_tmp_2=v_count;
size_t dn_tmp_3=({ size_t dn_tmp_4=(v_plan)->f_maximum_unknown_bytes;
size_t dn_tmp_5=v_used;
dn_USize_sub(dn_tmp_4,dn_tmp_5); });
(dn_tmp_2 > dn_tmp_3); }));return dn_tmp_6; }
}

dn_fail("missing native return");
}
static bool dn_cb_codec_unknown_over_budget(dn_r_CodecPlan const * v_plan,size_t v_used,size_t v_count) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_codec_unknown_over_budget(v_plan,v_used,v_count);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecValue dn_f_codec_get(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,uint32_t v_field_id,size_t v_occurrence) {
{
size_t v_seen=((size_t)(0ULL));
{ size_t dn_tmp_2=((size_t)(0ULL));size_t dn_tmp_1=({ dn_t_buf_r10_CodecValue const * dn_tmp_0=(&((v_message)->f_values));
(*(dn_tmp_0)).len; });for(size_t v_index=dn_tmp_2;v_index<dn_tmp_1;v_index++){
dn_r_CodecValue v_value=(*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_3=&((v_message)->f_values);size_t dn_tmp_4=v_index;&dn_tmp_3->data[dn_tmp_4]; }));
if(({ uint32_t dn_tmp_5=(v_value).f_field;
uint32_t dn_tmp_6=v_field_id;
(dn_tmp_5 == dn_tmp_6); })){
if(({ size_t dn_tmp_7=v_seen;
size_t dn_tmp_8=v_occurrence;
(dn_tmp_7 == dn_tmp_8); })){
{ dn_r_CodecValue dn_tmp_9=v_value;return dn_tmp_9; }
}
else{
}
{ size_t dn_tmp_13=({ size_t dn_tmp_10=v_seen;
size_t dn_tmp_11=((size_t)(1ULL));
dn_USize_add(dn_tmp_10,dn_tmp_11); });size_t *dn_tmp_12=&(v_seen);*dn_tmp_12=dn_tmp_13; }
}
else{
}
}
 }
{ dn_r_CodecValue dn_tmp_14=({ ((dn_r_CodecValue){0}); });return dn_tmp_14; }
}

dn_fail("missing native return");
}
static dn_r_CodecValue dn_cb_codec_get(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,uint32_t v_field_id,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_CodecValue){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecValue){0};}
 dn_r_CodecValue dn_result=dn_f_codec_get(v_plan,v_message,v_field_id,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecScalar dn_f_codec_get_scalar(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,uint32_t v_field_id,size_t v_occurrence) {
{
dn_r_CodecShape v_checked=({ dn_r_CodecPlan const * dn_tmp_0=v_plan;
dn_r_CodecMessage const * dn_tmp_1=v_message;
bool dn_tmp_2=0;
dn_f_codec_message_inspect(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
if(({ int32_t dn_tmp_3=((v_checked).f_error).f_kind;
int32_t dn_tmp_4=((int32_t)(0ULL));
(dn_tmp_3 != dn_tmp_4); })){
{ dn_r_CodecScalar dn_tmp_7=({ uint64_t dn_tmp_5=((uint64_t)(0ULL));
dn_r_CodecError dn_tmp_6=(v_checked).f_error;
((dn_r_CodecScalar){dn_tmp_5,dn_tmp_6}); });return dn_tmp_7; }
}
else{
}
dn_r_CodecValue v_value=({ dn_r_CodecPlan const * dn_tmp_8=v_plan;
dn_r_CodecMessage const * dn_tmp_9=v_message;
uint32_t dn_tmp_10=v_field_id;
size_t dn_tmp_11=v_occurrence;
dn_f_codec_get(dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_11); });
if(({ uint32_t dn_tmp_12=(v_value).f_field;
uint32_t dn_tmp_13=((uint32_t)(0ULL));
(dn_tmp_12 == dn_tmp_13); })){
{ dn_r_CodecScalar dn_tmp_19=({ uint64_t dn_tmp_14=((uint64_t)(0ULL));
dn_r_CodecError dn_tmp_15=({ int32_t dn_tmp_16=((int32_t)(2ULL));
uint32_t dn_tmp_17=v_field_id;
size_t dn_tmp_18=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_16,dn_tmp_17,dn_tmp_18}); });
((dn_r_CodecScalar){dn_tmp_14,dn_tmp_15}); });return dn_tmp_19; }
}
else{
}
if((({ uint32_t dn_tmp_20=(v_value).f_kind;
uint32_t dn_tmp_21=((uint32_t)(6ULL));
(dn_tmp_20 == dn_tmp_21); }) || ({ uint32_t dn_tmp_22=(v_value).f_kind;
uint32_t dn_tmp_23=((uint32_t)(7ULL));
(dn_tmp_22 == dn_tmp_23); }))){
{ dn_r_CodecScalar dn_tmp_29=({ uint64_t dn_tmp_24=((uint64_t)(0ULL));
dn_r_CodecError dn_tmp_25=({ int32_t dn_tmp_26=((int32_t)(4ULL));
uint32_t dn_tmp_27=v_field_id;
size_t dn_tmp_28=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_26,dn_tmp_27,dn_tmp_28}); });
((dn_r_CodecScalar){dn_tmp_24,dn_tmp_25}); });return dn_tmp_29; }
}
else{
}
{ dn_r_CodecScalar dn_tmp_35=({ uint64_t dn_tmp_30=(v_value).f_scalar;
dn_r_CodecError dn_tmp_31=({ int32_t dn_tmp_32=((int32_t)(0ULL));
uint32_t dn_tmp_33=((uint32_t)(0ULL));
size_t dn_tmp_34=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_32,dn_tmp_33,dn_tmp_34}); });
((dn_r_CodecScalar){dn_tmp_30,dn_tmp_31}); });return dn_tmp_35; }
}

dn_fail("missing native return");
}
static dn_r_CodecScalar dn_cb_codec_get_scalar(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,uint32_t v_field_id,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_CodecScalar){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecScalar){0};}
 dn_r_CodecScalar dn_result=dn_f_codec_get_scalar(v_plan,v_message,v_field_id,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecSpan dn_f_codec_get_span(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,uint32_t v_field_id,size_t v_occurrence) {
{
dn_r_CodecShape v_checked=({ dn_r_CodecPlan const * dn_tmp_0=v_plan;
dn_r_CodecMessage const * dn_tmp_1=v_message;
bool dn_tmp_2=0;
dn_f_codec_message_inspect(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
if(({ int32_t dn_tmp_3=((v_checked).f_error).f_kind;
int32_t dn_tmp_4=((int32_t)(0ULL));
(dn_tmp_3 != dn_tmp_4); })){
{ dn_r_CodecSpan dn_tmp_8=({ size_t dn_tmp_5=((size_t)(0ULL));
size_t dn_tmp_6=((size_t)(0ULL));
dn_r_CodecError dn_tmp_7=(v_checked).f_error;
((dn_r_CodecSpan){dn_tmp_5,dn_tmp_6,dn_tmp_7}); });return dn_tmp_8; }
}
else{
}
dn_r_CodecValue v_value=({ dn_r_CodecPlan const * dn_tmp_9=v_plan;
dn_r_CodecMessage const * dn_tmp_10=v_message;
uint32_t dn_tmp_11=v_field_id;
size_t dn_tmp_12=v_occurrence;
dn_f_codec_get(dn_tmp_9,dn_tmp_10,dn_tmp_11,dn_tmp_12); });
if(({ uint32_t dn_tmp_13=(v_value).f_field;
uint32_t dn_tmp_14=((uint32_t)(0ULL));
(dn_tmp_13 == dn_tmp_14); })){
{ dn_r_CodecSpan dn_tmp_21=({ size_t dn_tmp_15=((size_t)(0ULL));
size_t dn_tmp_16=((size_t)(0ULL));
dn_r_CodecError dn_tmp_17=({ int32_t dn_tmp_18=((int32_t)(2ULL));
uint32_t dn_tmp_19=v_field_id;
size_t dn_tmp_20=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_18,dn_tmp_19,dn_tmp_20}); });
((dn_r_CodecSpan){dn_tmp_15,dn_tmp_16,dn_tmp_17}); });return dn_tmp_21; }
}
else{
}
if((({ uint32_t dn_tmp_22=(v_value).f_kind;
uint32_t dn_tmp_23=((uint32_t)(6ULL));
(dn_tmp_22 != dn_tmp_23); }) && ({ uint32_t dn_tmp_24=(v_value).f_kind;
uint32_t dn_tmp_25=((uint32_t)(7ULL));
(dn_tmp_24 != dn_tmp_25); }))){
{ dn_r_CodecSpan dn_tmp_32=({ size_t dn_tmp_26=((size_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_29=((int32_t)(4ULL));
uint32_t dn_tmp_30=v_field_id;
size_t dn_tmp_31=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_29,dn_tmp_30,dn_tmp_31}); });
((dn_r_CodecSpan){dn_tmp_26,dn_tmp_27,dn_tmp_28}); });return dn_tmp_32; }
}
else{
}
{ dn_r_CodecSpan dn_tmp_39=({ size_t dn_tmp_33=(v_value).f_offset;
size_t dn_tmp_34=(v_value).f_count;
dn_r_CodecError dn_tmp_35=({ int32_t dn_tmp_36=((int32_t)(0ULL));
uint32_t dn_tmp_37=((uint32_t)(0ULL));
size_t dn_tmp_38=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_36,dn_tmp_37,dn_tmp_38}); });
((dn_r_CodecSpan){dn_tmp_33,dn_tmp_34,dn_tmp_35}); });return dn_tmp_39; }
}

dn_fail("missing native return");
}
static dn_r_CodecSpan dn_cb_codec_get_span(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,uint32_t v_field_id,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_CodecSpan){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecSpan){0};}
 dn_r_CodecSpan dn_result=dn_f_codec_get_span(v_plan,v_message,v_field_id,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_slice0_U8 dn_f_codec_view(dn_r_CodecMessage const * v_message,dn_r_CodecSpan v_span) {
{
if(((({ int32_t dn_tmp_0=((v_span).f_error).f_kind;
int32_t dn_tmp_1=((int32_t)(0ULL));
(dn_tmp_0 != dn_tmp_1); }) || ({ size_t dn_tmp_2=(v_span).f_offset;
size_t dn_tmp_3=({ dn_t_buf_U8 const * dn_tmp_4=(&((v_message)->f_bytes));
(*(dn_tmp_4)).len; });
(dn_tmp_2 > dn_tmp_3); })) || ({ size_t dn_tmp_5=(v_span).f_count;
size_t dn_tmp_6=({ size_t dn_tmp_7=({ dn_t_buf_U8 const * dn_tmp_8=(&((v_message)->f_bytes));
(*(dn_tmp_8)).len; });
size_t dn_tmp_9=(v_span).f_offset;
dn_USize_sub(dn_tmp_7,dn_tmp_9); });
(dn_tmp_5 > dn_tmp_6); }))){
{ dn_t_slice0_U8 dn_tmp_13=({ dn_t_buf_U8 const * dn_tmp_10=(&((v_message)->f_bytes));
size_t dn_tmp_11=((size_t)(0ULL));
size_t dn_tmp_12=((size_t)(0ULL));
({ dn_range(dn_tmp_11,dn_tmp_12,(*(dn_tmp_10)).len);(dn_t_slice0_U8){(*(dn_tmp_10)).data?(*(dn_tmp_10)).data+dn_tmp_11:NULL,dn_tmp_12}; }); });return dn_tmp_13; }
}
else{
}
{ dn_t_slice0_U8 dn_tmp_17=({ dn_t_buf_U8 const * dn_tmp_14=(&((v_message)->f_bytes));
size_t dn_tmp_15=(v_span).f_offset;
size_t dn_tmp_16=(v_span).f_count;
({ dn_range(dn_tmp_15,dn_tmp_16,(*(dn_tmp_14)).len);(dn_t_slice0_U8){(*(dn_tmp_14)).data?(*(dn_tmp_14)).data+dn_tmp_15:NULL,dn_tmp_16}; }); });return dn_tmp_17; }
}

dn_fail("missing native return");
}
static dn_t_slice0_U8 dn_cb_codec_view(dn_r_CodecMessage const * v_message,dn_r_CodecSpan v_span) {
 if(!dn_enter()){return (dn_t_slice0_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_slice0_U8){0};}
 dn_t_slice0_U8 dn_result=dn_f_codec_view(v_message,v_span);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static int32_t dn_f_codec_write_integer(dn_r_CodecPlan const * v_plan,dn_t_buf_U8 * v_output,size_t v_offset,size_t v_width,uint64_t v_value) {
{
if(({ uint32_t dn_tmp_0=((v_plan)->f_wire).f_endian;
uint32_t dn_tmp_1=((uint32_t)(1ULL));
(dn_tmp_0 == dn_tmp_1); })){
{ int32_t dn_tmp_6=({ dn_t_buf_U8 * dn_tmp_2=v_output;
size_t dn_tmp_3=v_offset;
size_t dn_tmp_4=v_width;
uint64_t dn_tmp_5=v_value;
dn_f_net_write_be(dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5); });return dn_tmp_6; }
}
else{
}
{ int32_t dn_tmp_11=({ dn_t_buf_U8 * dn_tmp_7=v_output;
size_t dn_tmp_8=v_offset;
size_t dn_tmp_9=v_width;
uint64_t dn_tmp_10=v_value;
dn_f_net_write_le(dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10); });return dn_tmp_11; }
}

dn_fail("missing native return");
}
static int32_t dn_cb_codec_write_integer(dn_r_CodecPlan const * v_plan,dn_t_buf_U8 * v_output,size_t v_offset,size_t v_width,uint64_t v_value) {
 if(!dn_enter()){return (int32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (int32_t){0};}
 int32_t dn_result=dn_f_codec_write_integer(v_plan,v_output,v_offset,v_width,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecIo dn_f_codec_encode(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,dn_t_buf_U8 * v_output,size_t v_offset) {
{
dn_r_CodecShape v_shape=({ dn_r_CodecPlan const * dn_tmp_0=v_plan;
dn_r_CodecMessage const * dn_tmp_1=v_message;
bool dn_tmp_2=1;
dn_f_codec_message_inspect(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
if(({ int32_t dn_tmp_3=((v_shape).f_error).f_kind;
int32_t dn_tmp_4=((int32_t)(0ULL));
(dn_tmp_3 != dn_tmp_4); })){
{ dn_r_CodecIo dn_tmp_8=({ size_t dn_tmp_5=((size_t)(0ULL));
size_t dn_tmp_6=v_offset;
dn_r_CodecError dn_tmp_7=(v_shape).f_error;
((dn_r_CodecIo){dn_tmp_5,dn_tmp_6,dn_tmp_7}); });return dn_tmp_8; }
}
else{
}
if(({ size_t dn_tmp_9=v_offset;
size_t dn_tmp_10=({ dn_t_buf_U8 * dn_tmp_11=v_output;
(*(dn_tmp_11)).len; });
(dn_tmp_9 > dn_tmp_10); })){
{ dn_r_CodecIo dn_tmp_18=({ size_t dn_tmp_12=((size_t)(0ULL));
size_t dn_tmp_13=v_offset;
dn_r_CodecError dn_tmp_14=({ int32_t dn_tmp_15=((int32_t)(4ULL));
uint32_t dn_tmp_16=((uint32_t)(0ULL));
size_t dn_tmp_17=v_offset;
((dn_r_CodecError){dn_tmp_15,dn_tmp_16,dn_tmp_17}); });
((dn_r_CodecIo){dn_tmp_12,dn_tmp_13,dn_tmp_14}); });return dn_tmp_18; }
}
else{
}
if(({ size_t dn_tmp_19=(v_shape).f_wire;
size_t dn_tmp_20=({ size_t dn_tmp_21=({ dn_t_buf_U8 * dn_tmp_22=v_output;
(*(dn_tmp_22)).len; });
size_t dn_tmp_23=v_offset;
dn_USize_sub(dn_tmp_21,dn_tmp_23); });
(dn_tmp_19 > dn_tmp_20); })){
{ dn_r_CodecIo dn_tmp_30=({ size_t dn_tmp_24=((size_t)(0ULL));
size_t dn_tmp_25=v_offset;
dn_r_CodecError dn_tmp_26=({ int32_t dn_tmp_27=((int32_t)(1ULL));
uint32_t dn_tmp_28=((uint32_t)(0ULL));
size_t dn_tmp_29=v_offset;
((dn_r_CodecError){dn_tmp_27,dn_tmp_28,dn_tmp_29}); });
((dn_r_CodecIo){dn_tmp_24,dn_tmp_25,dn_tmp_26}); });return dn_tmp_30; }
}
else{
}
int32_t v_first=({ dn_t_buf_U8 * dn_tmp_31=v_output;
size_t dn_tmp_32=v_offset;
size_t dn_tmp_33=((size_t)(4ULL));
uint64_t dn_tmp_34=({ uint32_t dn_tmp_35=(v_plan)->f_message;
if((unsigned __int128)dn_tmp_35>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_35; });
dn_f_net_write_be(dn_tmp_31,dn_tmp_32,dn_tmp_33,dn_tmp_34); });
size_t v_position=({ size_t dn_tmp_36=v_offset;
size_t dn_tmp_37=((size_t)(4ULL));
dn_USize_add(dn_tmp_36,dn_tmp_37); });
if(({ uint32_t dn_tmp_38=((v_plan)->f_wire).f_kind;
uint32_t dn_tmp_39=((uint32_t)(2ULL));
(dn_tmp_38 == dn_tmp_39); })){
int32_t v_a=({ dn_t_buf_U8 * dn_tmp_40=v_output;
size_t dn_tmp_41=v_position;
size_t dn_tmp_42=((size_t)(4ULL));
uint64_t dn_tmp_43=((uint64_t)(0x424d4c44ULL));
dn_f_net_write_le(dn_tmp_40,dn_tmp_41,dn_tmp_42,dn_tmp_43); });
int32_t v_b=({ dn_t_buf_U8 * dn_tmp_44=v_output;
size_t dn_tmp_45=({ size_t dn_tmp_46=v_position;
size_t dn_tmp_47=((size_t)(4ULL));
dn_USize_add(dn_tmp_46,dn_tmp_47); });
size_t dn_tmp_48=((size_t)(2ULL));
uint64_t dn_tmp_49=({ uint32_t dn_tmp_50=((v_plan)->f_wire).f_envelope_version;
if((unsigned __int128)dn_tmp_50>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_50; });
dn_f_net_write_le(dn_tmp_44,dn_tmp_45,dn_tmp_48,dn_tmp_49); });
int32_t v_c=({ dn_t_buf_U8 * dn_tmp_51=v_output;
size_t dn_tmp_52=({ size_t dn_tmp_53=v_position;
size_t dn_tmp_54=((size_t)(6ULL));
dn_USize_add(dn_tmp_53,dn_tmp_54); });
size_t dn_tmp_55=((size_t)(2ULL));
uint64_t dn_tmp_56=((uint64_t)(0ULL));
dn_f_net_write_le(dn_tmp_51,dn_tmp_52,dn_tmp_55,dn_tmp_56); });
{ size_t dn_tmp_60=({ size_t dn_tmp_57=v_position;
size_t dn_tmp_58=((size_t)(8ULL));
dn_USize_add(dn_tmp_57,dn_tmp_58); });size_t *dn_tmp_59=&(v_position);*dn_tmp_59=dn_tmp_60; }
}
else{
}
size_t v_slot=((size_t)(0ULL));
{ size_t dn_tmp_63=((size_t)(0ULL));size_t dn_tmp_62=({ uint32_t dn_tmp_61=(v_plan)->f_field_count;
if((unsigned __int128)dn_tmp_61>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_61; });for(size_t v_index=dn_tmp_63;v_index<dn_tmp_62;v_index++){
dn_r_MessageField const * v_field=(&((*({ dn_t_arr32_r12_MessageField *dn_tmp_64=&((v_plan)->f_fields);size_t dn_tmp_65=v_index;&dn_tmp_64->data[dn_index(dn_tmp_65,32)]; }))));
if((({ uint32_t dn_tmp_66=((v_plan)->f_wire).f_kind;
uint32_t dn_tmp_67=((uint32_t)(1ULL));
(dn_tmp_66 == dn_tmp_67); }) && (({ uint32_t dn_tmp_68=(v_field)->f_minimum_count;
uint32_t dn_tmp_69=((uint32_t)(1ULL));
(dn_tmp_68 != dn_tmp_69); }) || ({ uint32_t dn_tmp_70=(v_field)->f_maximum_count;
uint32_t dn_tmp_71=((uint32_t)(1ULL));
(dn_tmp_70 != dn_tmp_71); })))){
int32_t v_count=({ dn_r_CodecPlan const * dn_tmp_72=v_plan;
dn_t_buf_U8 * dn_tmp_73=v_output;
size_t dn_tmp_74=v_position;
size_t dn_tmp_75=((size_t)(4ULL));
uint64_t dn_tmp_76=({ uint32_t dn_tmp_77=(*({ dn_t_arr32_U32 *dn_tmp_78=&((v_shape).f_counts);size_t dn_tmp_79=v_index;&dn_tmp_78->data[dn_index(dn_tmp_79,32)]; }));
if((unsigned __int128)dn_tmp_77>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_77; });
dn_f_codec_write_integer(dn_tmp_72,dn_tmp_73,dn_tmp_74,dn_tmp_75,dn_tmp_76); });
{ size_t dn_tmp_83=({ size_t dn_tmp_80=v_position;
size_t dn_tmp_81=((size_t)(4ULL));
dn_USize_add(dn_tmp_80,dn_tmp_81); });size_t *dn_tmp_82=&(v_position);*dn_tmp_82=dn_tmp_83; }
}
else{
}
{ size_t dn_tmp_88=((size_t)(0ULL));size_t dn_tmp_87=({ uint32_t dn_tmp_84=(*({ dn_t_arr32_U32 *dn_tmp_85=&((v_shape).f_counts);size_t dn_tmp_86=v_index;&dn_tmp_85->data[dn_index(dn_tmp_86,32)]; }));
if((unsigned __int128)dn_tmp_84>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_84; });for(size_t v_occurrence=dn_tmp_88;v_occurrence<dn_tmp_87;v_occurrence++){
dn_r_CodecValue v_value=(*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_89=&((v_message)->f_values);size_t dn_tmp_90=v_slot;&dn_tmp_89->data[dn_index(dn_tmp_90,dn_tmp_89->len)]; }));
{ size_t dn_tmp_94=({ size_t dn_tmp_91=v_slot;
size_t dn_tmp_92=((size_t)(1ULL));
dn_USize_add(dn_tmp_91,dn_tmp_92); });size_t *dn_tmp_93=&(v_slot);*dn_tmp_93=dn_tmp_94; }
size_t v_count=({ uint32_t dn_tmp_95=(v_field)->f_kind;
uint64_t dn_tmp_96=(v_value).f_scalar;
dn_f_codec_scalar_size(dn_tmp_95,dn_tmp_96); });
if((({ uint32_t dn_tmp_97=(v_field)->f_kind;
uint32_t dn_tmp_98=((uint32_t)(6ULL));
(dn_tmp_97 == dn_tmp_98); }) || ({ uint32_t dn_tmp_99=(v_field)->f_kind;
uint32_t dn_tmp_100=((uint32_t)(7ULL));
(dn_tmp_99 == dn_tmp_100); }))){
{ size_t dn_tmp_102=(v_value).f_count;size_t *dn_tmp_101=&(v_count);*dn_tmp_101=dn_tmp_102; }
}
else{
}
if(({ uint32_t dn_tmp_103=((v_plan)->f_wire).f_kind;
uint32_t dn_tmp_104=((uint32_t)(2ULL));
(dn_tmp_103 == dn_tmp_104); })){
int32_t v_tag=({ dn_t_buf_U8 * dn_tmp_105=v_output;
size_t dn_tmp_106=v_position;
size_t dn_tmp_107=((size_t)(4ULL));
uint64_t dn_tmp_108=({ uint32_t dn_tmp_109=(v_field)->f_id;
if((unsigned __int128)dn_tmp_109>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_109; });
dn_f_net_write_le(dn_tmp_105,dn_tmp_106,dn_tmp_107,dn_tmp_108); });
int32_t v_size=({ dn_t_buf_U8 * dn_tmp_110=v_output;
size_t dn_tmp_111=({ size_t dn_tmp_112=v_position;
size_t dn_tmp_113=((size_t)(4ULL));
dn_USize_add(dn_tmp_112,dn_tmp_113); });
size_t dn_tmp_114=((size_t)(4ULL));
uint64_t dn_tmp_115=({ size_t dn_tmp_116=v_count;
if((unsigned __int128)dn_tmp_116>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_116; });
dn_f_net_write_le(dn_tmp_110,dn_tmp_111,dn_tmp_114,dn_tmp_115); });
{ size_t dn_tmp_120=({ size_t dn_tmp_117=v_position;
size_t dn_tmp_118=((size_t)(8ULL));
dn_USize_add(dn_tmp_117,dn_tmp_118); });size_t *dn_tmp_119=&(v_position);*dn_tmp_119=dn_tmp_120; }
}
else{
}
if((({ uint32_t dn_tmp_121=(v_field)->f_kind;
uint32_t dn_tmp_122=((uint32_t)(6ULL));
(dn_tmp_121 == dn_tmp_122); }) || ({ uint32_t dn_tmp_123=(v_field)->f_kind;
uint32_t dn_tmp_124=((uint32_t)(7ULL));
(dn_tmp_123 == dn_tmp_124); }))){
if(({ uint32_t dn_tmp_125=((v_plan)->f_wire).f_kind;
uint32_t dn_tmp_126=((uint32_t)(1ULL));
(dn_tmp_125 == dn_tmp_126); })){
int32_t v_size=({ dn_r_CodecPlan const * dn_tmp_127=v_plan;
dn_t_buf_U8 * dn_tmp_128=v_output;
size_t dn_tmp_129=v_position;
size_t dn_tmp_130=((size_t)(4ULL));
uint64_t dn_tmp_131=({ size_t dn_tmp_132=v_count;
if((unsigned __int128)dn_tmp_132>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_132; });
dn_f_codec_write_integer(dn_tmp_127,dn_tmp_128,dn_tmp_129,dn_tmp_130,dn_tmp_131); });
{ size_t dn_tmp_136=({ size_t dn_tmp_133=v_position;
size_t dn_tmp_134=((size_t)(4ULL));
dn_USize_add(dn_tmp_133,dn_tmp_134); });size_t *dn_tmp_135=&(v_position);*dn_tmp_135=dn_tmp_136; }
}
else{
}
{ size_t dn_tmp_138=((size_t)(0ULL));size_t dn_tmp_137=v_count;for(size_t v_b=dn_tmp_138;v_b<dn_tmp_137;v_b++){
{ uint8_t dn_tmp_148=(*({ dn_t_buf_U8 volatile *dn_tmp_143=&((v_message)->f_bytes);size_t dn_tmp_144=({ size_t dn_tmp_145=(v_value).f_offset;
size_t dn_tmp_146=v_b;
dn_USize_add(dn_tmp_145,dn_tmp_146); });&dn_tmp_143->data[dn_index(dn_tmp_144,dn_tmp_143->len)]; }));uint8_t *dn_tmp_147=&((*({ dn_t_buf_U8 * dn_tmp_139=v_output;size_t dn_tmp_140=({ size_t dn_tmp_141=v_position;
size_t dn_tmp_142=v_b;
dn_USize_add(dn_tmp_141,dn_tmp_142); });&dn_tmp_139->data[dn_index(dn_tmp_140,dn_tmp_139->len)]; })));*dn_tmp_147=dn_tmp_148; }
}
 }
{ size_t dn_tmp_152=({ size_t dn_tmp_149=v_position;
size_t dn_tmp_150=v_count;
dn_USize_add(dn_tmp_149,dn_tmp_150); });size_t *dn_tmp_151=&(v_position);*dn_tmp_151=dn_tmp_152; }
}
else{
if((({ uint32_t dn_tmp_153=(v_field)->f_kind;
uint32_t dn_tmp_154=((uint32_t)(8ULL));
(dn_tmp_153 == dn_tmp_154); }) || ({ uint32_t dn_tmp_155=(v_field)->f_kind;
uint32_t dn_tmp_156=((uint32_t)(9ULL));
(dn_tmp_155 == dn_tmp_156); }))){
dn_r_ProtocolValue v_written=({ dn_t_buf_U8 * dn_tmp_157=v_output;
size_t dn_tmp_158=v_position;
uint64_t dn_tmp_159=(v_value).f_scalar;
dn_f_uleb128_write(dn_tmp_157,dn_tmp_158,dn_tmp_159); });
{ size_t dn_tmp_161=(v_written).f_next;size_t *dn_tmp_160=&(v_position);*dn_tmp_160=dn_tmp_161; }
}
else{
int32_t v_written=({ dn_r_CodecPlan const * dn_tmp_162=v_plan;
dn_t_buf_U8 * dn_tmp_163=v_output;
size_t dn_tmp_164=v_position;
size_t dn_tmp_165=v_count;
uint64_t dn_tmp_166=(v_value).f_scalar;
dn_f_codec_write_integer(dn_tmp_162,dn_tmp_163,dn_tmp_164,dn_tmp_165,dn_tmp_166); });
{ size_t dn_tmp_170=({ size_t dn_tmp_167=v_position;
size_t dn_tmp_168=v_count;
dn_USize_add(dn_tmp_167,dn_tmp_168); });size_t *dn_tmp_169=&(v_position);*dn_tmp_169=dn_tmp_170; }
}
}
}
 }
}
 }
{ size_t dn_tmp_173=((size_t)(0ULL));size_t dn_tmp_172=({ dn_t_buf_U8 const * dn_tmp_171=(&((v_message)->f_unknown));
(*(dn_tmp_171)).len; });for(size_t v_b=dn_tmp_173;v_b<dn_tmp_172;v_b++){
{ uint8_t dn_tmp_181=(*({ dn_t_buf_U8 volatile *dn_tmp_178=&((v_message)->f_unknown);size_t dn_tmp_179=v_b;&dn_tmp_178->data[dn_tmp_179]; }));uint8_t *dn_tmp_180=&((*({ dn_t_buf_U8 * dn_tmp_174=v_output;size_t dn_tmp_175=({ size_t dn_tmp_176=v_position;
size_t dn_tmp_177=v_b;
dn_USize_add(dn_tmp_176,dn_tmp_177); });&dn_tmp_174->data[dn_index(dn_tmp_175,dn_tmp_174->len)]; })));*dn_tmp_180=dn_tmp_181; }
}
 }
{ size_t dn_tmp_186=({ size_t dn_tmp_182=v_position;
size_t dn_tmp_183=({ dn_t_buf_U8 const * dn_tmp_184=(&((v_message)->f_unknown));
(*(dn_tmp_184)).len; });
dn_USize_add(dn_tmp_182,dn_tmp_183); });size_t *dn_tmp_185=&(v_position);*dn_tmp_185=dn_tmp_186; }
{ dn_r_CodecIo dn_tmp_193=({ size_t dn_tmp_187=(v_shape).f_wire;
size_t dn_tmp_188=v_position;
dn_r_CodecError dn_tmp_189=({ int32_t dn_tmp_190=((int32_t)(0ULL));
uint32_t dn_tmp_191=((uint32_t)(0ULL));
size_t dn_tmp_192=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_190,dn_tmp_191,dn_tmp_192}); });
((dn_r_CodecIo){dn_tmp_187,dn_tmp_188,dn_tmp_189}); });return dn_tmp_193; }
}

dn_fail("missing native return");
}
static dn_r_CodecIo dn_cb_codec_encode(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_message,dn_t_buf_U8 * v_output,size_t v_offset) {
 if(!dn_enter()){return (dn_r_CodecIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecIo){0};}
 dn_r_CodecIo dn_result=dn_f_codec_encode(v_plan,v_message,v_output,v_offset);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_codec_equal(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_left,dn_r_CodecMessage const * v_right) {
{
dn_r_CodecShape v_a=({ dn_r_CodecPlan const * dn_tmp_0=v_plan;
dn_r_CodecMessage const * dn_tmp_1=v_left;
bool dn_tmp_2=1;
dn_f_codec_message_inspect(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
dn_r_CodecShape v_b=({ dn_r_CodecPlan const * dn_tmp_3=v_plan;
dn_r_CodecMessage const * dn_tmp_4=v_right;
bool dn_tmp_5=1;
dn_f_codec_message_inspect(dn_tmp_3,dn_tmp_4,dn_tmp_5); });
if(((((({ int32_t dn_tmp_6=((v_a).f_error).f_kind;
int32_t dn_tmp_7=((int32_t)(0ULL));
(dn_tmp_6 != dn_tmp_7); }) || ({ int32_t dn_tmp_8=((v_b).f_error).f_kind;
int32_t dn_tmp_9=((int32_t)(0ULL));
(dn_tmp_8 != dn_tmp_9); })) || ({ size_t dn_tmp_10=({ dn_t_buf_r10_CodecValue const * dn_tmp_11=(&((v_left)->f_values));
(*(dn_tmp_11)).len; });
size_t dn_tmp_12=({ dn_t_buf_r10_CodecValue const * dn_tmp_13=(&((v_right)->f_values));
(*(dn_tmp_13)).len; });
(dn_tmp_10 != dn_tmp_12); })) || ({ size_t dn_tmp_14=({ dn_t_buf_U8 const * dn_tmp_15=(&((v_left)->f_bytes));
(*(dn_tmp_15)).len; });
size_t dn_tmp_16=({ dn_t_buf_U8 const * dn_tmp_17=(&((v_right)->f_bytes));
(*(dn_tmp_17)).len; });
(dn_tmp_14 != dn_tmp_16); })) || ({ size_t dn_tmp_18=({ dn_t_buf_U8 const * dn_tmp_19=(&((v_left)->f_unknown));
(*(dn_tmp_19)).len; });
size_t dn_tmp_20=({ dn_t_buf_U8 const * dn_tmp_21=(&((v_right)->f_unknown));
(*(dn_tmp_21)).len; });
(dn_tmp_18 != dn_tmp_20); }))){
{ bool dn_tmp_22=0;return dn_tmp_22; }
}
else{
}
{ size_t dn_tmp_25=((size_t)(0ULL));size_t dn_tmp_24=({ dn_t_buf_r10_CodecValue const * dn_tmp_23=(&((v_left)->f_values));
(*(dn_tmp_23)).len; });for(size_t v_index=dn_tmp_25;v_index<dn_tmp_24;v_index++){
dn_r_CodecValue v_x=(*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_26=&((v_left)->f_values);size_t dn_tmp_27=v_index;&dn_tmp_26->data[dn_tmp_27]; }));
dn_r_CodecValue v_y=(*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_28=&((v_right)->f_values);size_t dn_tmp_29=v_index;&dn_tmp_28->data[dn_index(dn_tmp_29,dn_tmp_28->len)]; }));
if(((((({ uint32_t dn_tmp_30=(v_x).f_field;
uint32_t dn_tmp_31=(v_y).f_field;
(dn_tmp_30 != dn_tmp_31); }) || ({ uint32_t dn_tmp_32=(v_x).f_kind;
uint32_t dn_tmp_33=(v_y).f_kind;
(dn_tmp_32 != dn_tmp_33); })) || ({ uint64_t dn_tmp_34=(v_x).f_scalar;
uint64_t dn_tmp_35=(v_y).f_scalar;
(dn_tmp_34 != dn_tmp_35); })) || ({ size_t dn_tmp_36=(v_x).f_offset;
size_t dn_tmp_37=(v_y).f_offset;
(dn_tmp_36 != dn_tmp_37); })) || ({ size_t dn_tmp_38=(v_x).f_count;
size_t dn_tmp_39=(v_y).f_count;
(dn_tmp_38 != dn_tmp_39); }))){
{ bool dn_tmp_40=0;return dn_tmp_40; }
}
else{
}
}
 }
{ size_t dn_tmp_43=((size_t)(0ULL));size_t dn_tmp_42=({ dn_t_buf_U8 const * dn_tmp_41=(&((v_left)->f_bytes));
(*(dn_tmp_41)).len; });for(size_t v_index=dn_tmp_43;v_index<dn_tmp_42;v_index++){
if(({ uint8_t dn_tmp_44=(*({ dn_t_buf_U8 volatile *dn_tmp_45=&((v_left)->f_bytes);size_t dn_tmp_46=v_index;&dn_tmp_45->data[dn_tmp_46]; }));
uint8_t dn_tmp_47=(*({ dn_t_buf_U8 volatile *dn_tmp_48=&((v_right)->f_bytes);size_t dn_tmp_49=v_index;&dn_tmp_48->data[dn_index(dn_tmp_49,dn_tmp_48->len)]; }));
(dn_tmp_44 != dn_tmp_47); })){
{ bool dn_tmp_50=0;return dn_tmp_50; }
}
else{
}
}
 }
{ size_t dn_tmp_53=((size_t)(0ULL));size_t dn_tmp_52=({ dn_t_buf_U8 const * dn_tmp_51=(&((v_left)->f_unknown));
(*(dn_tmp_51)).len; });for(size_t v_index=dn_tmp_53;v_index<dn_tmp_52;v_index++){
if(({ uint8_t dn_tmp_54=(*({ dn_t_buf_U8 volatile *dn_tmp_55=&((v_left)->f_unknown);size_t dn_tmp_56=v_index;&dn_tmp_55->data[dn_tmp_56]; }));
uint8_t dn_tmp_57=(*({ dn_t_buf_U8 volatile *dn_tmp_58=&((v_right)->f_unknown);size_t dn_tmp_59=v_index;&dn_tmp_58->data[dn_index(dn_tmp_59,dn_tmp_58->len)]; }));
(dn_tmp_54 != dn_tmp_57); })){
{ bool dn_tmp_60=0;return dn_tmp_60; }
}
else{
}
}
 }
{ bool dn_tmp_61=1;return dn_tmp_61; }
}

dn_fail("missing native return");
}
static bool dn_cb_codec_equal(dn_r_CodecPlan const * v_plan,dn_r_CodecMessage const * v_left,dn_r_CodecMessage const * v_right) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_codec_equal(v_plan,v_left,v_right);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkSpec dn_f_Echo_spec(void) {
{
dn_r_NetworkSpec v_spec=({ uint32_t dn_tmp_0=((uint32_t)(1129268273ULL));
uint32_t dn_tmp_1=((uint32_t)(1ULL));
uint32_t dn_tmp_2=((uint32_t)(1ULL));
uint32_t dn_tmp_3=((uint32_t)(0ULL));
dn_f_network_spec(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_WireFormat dn_tmp_9=({ uint32_t dn_tmp_4=((uint32_t)(2ULL));
uint32_t dn_tmp_5=((uint32_t)(2ULL));
uint32_t dn_tmp_6=((uint32_t)(2ULL));
uint32_t dn_tmp_7=((uint32_t)(1ULL));
((dn_r_WireFormat){dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7}); });dn_r_WireFormat *dn_tmp_8=&((v_spec).f_wire);*dn_tmp_8=dn_tmp_9; }
{ dn_r_TransportRequirements dn_tmp_14=({ uint32_t dn_tmp_10=((uint32_t)(19ULL));
uint32_t dn_tmp_11=((uint32_t)(0ULL));
uint32_t dn_tmp_12=((uint32_t)(0ULL));
((dn_r_TransportRequirements){dn_tmp_10,dn_tmp_11,dn_tmp_12}); });dn_r_TransportRequirements *dn_tmp_13=&((v_spec).f_transport);*dn_tmp_13=dn_tmp_14; }
{ dn_r_ResourceBounds dn_tmp_26=({ dn_r_TransportBounds dn_tmp_15=({ uint32_t dn_tmp_16=((uint32_t)(65536ULL));
uint32_t dn_tmp_17=((uint32_t)(32ULL));
uint32_t dn_tmp_18=((uint32_t)(16ULL));
size_t dn_tmp_19=((size_t)(8388608ULL));
((dn_r_TransportBounds){dn_tmp_16,dn_tmp_17,dn_tmp_18,dn_tmp_19}); });
uint32_t dn_tmp_20=((uint32_t)(4ULL));
uint32_t dn_tmp_21=((uint32_t)(256ULL));
uint64_t dn_tmp_22=({ uint64_t dn_tmp_23=((uint64_t)(1000000ULL));
if((unsigned __int128)dn_tmp_23>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_23; });
uint32_t dn_tmp_24=((uint32_t)(529ULL));
((dn_r_ResourceBounds){dn_tmp_15,dn_tmp_20,dn_tmp_21,dn_tmp_22,dn_tmp_24}); });dn_r_ResourceBounds *dn_tmp_25=&((v_spec).f_resources);*dn_tmp_25=dn_tmp_26; }
{ dn_r_SessionContract dn_tmp_33=({ uint32_t dn_tmp_27=((uint32_t)(10000ULL));
uint32_t dn_tmp_28=((uint32_t)(30000ULL));
uint32_t dn_tmp_29=((uint32_t)(300000ULL));
uint32_t dn_tmp_30=((uint32_t)(32ULL));
bool dn_tmp_31=0;
((dn_r_SessionContract){dn_tmp_27,dn_tmp_28,dn_tmp_29,dn_tmp_30,dn_tmp_31}); });dn_r_SessionContract *dn_tmp_32=&((v_spec).f_session);*dn_tmp_32=dn_tmp_33; }
{ dn_r_SecurityRequirements dn_tmp_39=({ uint32_t dn_tmp_34=((uint32_t)(0ULL));
bool dn_tmp_35=0;
bool dn_tmp_36=0;
uint32_t dn_tmp_37=((uint32_t)(0ULL));
((dn_r_SecurityRequirements){dn_tmp_34,dn_tmp_35,dn_tmp_36,dn_tmp_37}); });dn_r_SecurityRequirements *dn_tmp_38=&((v_spec).f_security);*dn_tmp_38=dn_tmp_39; }
{ dn_r_ProofObligations dn_tmp_43=({ uint32_t dn_tmp_40=((uint32_t)(0ULL));
uint32_t dn_tmp_41=((uint32_t)(1ULL));
((dn_r_ProofObligations){dn_tmp_40,dn_tmp_41}); });dn_r_ProofObligations *dn_tmp_42=&((v_spec).f_proofs);*dn_tmp_42=dn_tmp_43; }
{ uint32_t dn_tmp_45=((uint32_t)(2ULL));uint32_t *dn_tmp_44=&((v_spec).f_message_count);*dn_tmp_44=dn_tmp_45; }
{ dn_r_MessageSchema dn_tmp_52=({ uint32_t dn_tmp_48=((uint32_t)(1ULL));
uint32_t dn_tmp_49=((uint32_t)(2ULL));
uint32_t dn_tmp_50=((uint32_t)(3ULL));
dn_f_message_schema(dn_tmp_48,dn_tmp_49,dn_tmp_50); });dn_r_MessageSchema *dn_tmp_51=&((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_46=&((v_spec).f_messages);size_t dn_tmp_47=((size_t)(0ULL));&dn_tmp_46->data[dn_index(dn_tmp_47,16)]; })));*dn_tmp_51=dn_tmp_52; }
{ uint32_t dn_tmp_56=((uint32_t)(0ULL));uint32_t *dn_tmp_55=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_53=&((v_spec).f_messages);size_t dn_tmp_54=((size_t)(0ULL));&dn_tmp_53->data[dn_index(dn_tmp_54,16)]; }))).f_first_minor);*dn_tmp_55=dn_tmp_56; }
{ uint32_t dn_tmp_60=((uint32_t)(4294967295ULL));uint32_t *dn_tmp_59=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_57=&((v_spec).f_messages);size_t dn_tmp_58=((size_t)(0ULL));&dn_tmp_57->data[dn_index(dn_tmp_58,16)]; }))).f_last_minor);*dn_tmp_59=dn_tmp_60; }
{ uint32_t dn_tmp_64=((uint32_t)(11ULL));uint32_t *dn_tmp_63=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_61=&((v_spec).f_messages);size_t dn_tmp_62=((size_t)(0ULL));&dn_tmp_61->data[dn_index(dn_tmp_62,16)]; }))).f_field_count);*dn_tmp_63=dn_tmp_64; }
{ dn_r_MessageField dn_tmp_78=({ uint32_t dn_tmp_69=((uint32_t)(1ULL));
uint32_t dn_tmp_70=((uint32_t)(1ULL));
uint32_t dn_tmp_71=((uint32_t)(1ULL));
uint32_t dn_tmp_72=((uint32_t)(1ULL));
uint32_t dn_tmp_73=((uint32_t)(0ULL));
uint32_t dn_tmp_74=((uint32_t)(0ULL));
uint32_t dn_tmp_75=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_76=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_69,dn_tmp_70,dn_tmp_71,dn_tmp_72,dn_tmp_73,dn_tmp_74,dn_tmp_75,dn_tmp_76}); });dn_r_MessageField *dn_tmp_77=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_65=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_67=&((v_spec).f_messages);size_t dn_tmp_68=((size_t)(0ULL));&dn_tmp_67->data[dn_index(dn_tmp_68,16)]; }))).f_fields);size_t dn_tmp_66=((size_t)(0ULL));&dn_tmp_65->data[dn_index(dn_tmp_66,32)]; })));*dn_tmp_77=dn_tmp_78; }
{ dn_r_MessageField dn_tmp_92=({ uint32_t dn_tmp_83=((uint32_t)(2ULL));
uint32_t dn_tmp_84=((uint32_t)(2ULL));
uint32_t dn_tmp_85=((uint32_t)(0ULL));
uint32_t dn_tmp_86=((uint32_t)(1ULL));
uint32_t dn_tmp_87=((uint32_t)(0ULL));
uint32_t dn_tmp_88=((uint32_t)(0ULL));
uint32_t dn_tmp_89=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_90=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_83,dn_tmp_84,dn_tmp_85,dn_tmp_86,dn_tmp_87,dn_tmp_88,dn_tmp_89,dn_tmp_90}); });dn_r_MessageField *dn_tmp_91=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_79=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_81=&((v_spec).f_messages);size_t dn_tmp_82=((size_t)(0ULL));&dn_tmp_81->data[dn_index(dn_tmp_82,16)]; }))).f_fields);size_t dn_tmp_80=((size_t)(1ULL));&dn_tmp_79->data[dn_index(dn_tmp_80,32)]; })));*dn_tmp_91=dn_tmp_92; }
{ dn_r_MessageField dn_tmp_106=({ uint32_t dn_tmp_97=((uint32_t)(3ULL));
uint32_t dn_tmp_98=((uint32_t)(3ULL));
uint32_t dn_tmp_99=((uint32_t)(0ULL));
uint32_t dn_tmp_100=((uint32_t)(3ULL));
uint32_t dn_tmp_101=((uint32_t)(0ULL));
uint32_t dn_tmp_102=((uint32_t)(0ULL));
uint32_t dn_tmp_103=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_104=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_97,dn_tmp_98,dn_tmp_99,dn_tmp_100,dn_tmp_101,dn_tmp_102,dn_tmp_103,dn_tmp_104}); });dn_r_MessageField *dn_tmp_105=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_93=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_95=&((v_spec).f_messages);size_t dn_tmp_96=((size_t)(0ULL));&dn_tmp_95->data[dn_index(dn_tmp_96,16)]; }))).f_fields);size_t dn_tmp_94=((size_t)(2ULL));&dn_tmp_93->data[dn_index(dn_tmp_94,32)]; })));*dn_tmp_105=dn_tmp_106; }
{ dn_r_MessageField dn_tmp_120=({ uint32_t dn_tmp_111=((uint32_t)(4ULL));
uint32_t dn_tmp_112=((uint32_t)(4ULL));
uint32_t dn_tmp_113=((uint32_t)(1ULL));
uint32_t dn_tmp_114=((uint32_t)(1ULL));
uint32_t dn_tmp_115=((uint32_t)(0ULL));
uint32_t dn_tmp_116=((uint32_t)(0ULL));
uint32_t dn_tmp_117=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_118=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_111,dn_tmp_112,dn_tmp_113,dn_tmp_114,dn_tmp_115,dn_tmp_116,dn_tmp_117,dn_tmp_118}); });dn_r_MessageField *dn_tmp_119=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_107=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_109=&((v_spec).f_messages);size_t dn_tmp_110=((size_t)(0ULL));&dn_tmp_109->data[dn_index(dn_tmp_110,16)]; }))).f_fields);size_t dn_tmp_108=((size_t)(3ULL));&dn_tmp_107->data[dn_index(dn_tmp_108,32)]; })));*dn_tmp_119=dn_tmp_120; }
{ dn_r_MessageField dn_tmp_134=({ uint32_t dn_tmp_125=((uint32_t)(5ULL));
uint32_t dn_tmp_126=((uint32_t)(5ULL));
uint32_t dn_tmp_127=((uint32_t)(1ULL));
uint32_t dn_tmp_128=((uint32_t)(1ULL));
uint32_t dn_tmp_129=((uint32_t)(0ULL));
uint32_t dn_tmp_130=((uint32_t)(0ULL));
uint32_t dn_tmp_131=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_132=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_125,dn_tmp_126,dn_tmp_127,dn_tmp_128,dn_tmp_129,dn_tmp_130,dn_tmp_131,dn_tmp_132}); });dn_r_MessageField *dn_tmp_133=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_121=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_123=&((v_spec).f_messages);size_t dn_tmp_124=((size_t)(0ULL));&dn_tmp_123->data[dn_index(dn_tmp_124,16)]; }))).f_fields);size_t dn_tmp_122=((size_t)(4ULL));&dn_tmp_121->data[dn_index(dn_tmp_122,32)]; })));*dn_tmp_133=dn_tmp_134; }
{ dn_r_MessageField dn_tmp_148=({ uint32_t dn_tmp_139=((uint32_t)(6ULL));
uint32_t dn_tmp_140=((uint32_t)(6ULL));
uint32_t dn_tmp_141=((uint32_t)(0ULL));
uint32_t dn_tmp_142=((uint32_t)(2ULL));
uint32_t dn_tmp_143=((uint32_t)(32ULL));
uint32_t dn_tmp_144=((uint32_t)(0ULL));
uint32_t dn_tmp_145=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_146=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_139,dn_tmp_140,dn_tmp_141,dn_tmp_142,dn_tmp_143,dn_tmp_144,dn_tmp_145,dn_tmp_146}); });dn_r_MessageField *dn_tmp_147=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_135=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_137=&((v_spec).f_messages);size_t dn_tmp_138=((size_t)(0ULL));&dn_tmp_137->data[dn_index(dn_tmp_138,16)]; }))).f_fields);size_t dn_tmp_136=((size_t)(5ULL));&dn_tmp_135->data[dn_index(dn_tmp_136,32)]; })));*dn_tmp_147=dn_tmp_148; }
{ dn_r_MessageField dn_tmp_162=({ uint32_t dn_tmp_153=((uint32_t)(7ULL));
uint32_t dn_tmp_154=((uint32_t)(7ULL));
uint32_t dn_tmp_155=((uint32_t)(0ULL));
uint32_t dn_tmp_156=((uint32_t)(1ULL));
uint32_t dn_tmp_157=((uint32_t)(64ULL));
uint32_t dn_tmp_158=((uint32_t)(0ULL));
uint32_t dn_tmp_159=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_160=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_153,dn_tmp_154,dn_tmp_155,dn_tmp_156,dn_tmp_157,dn_tmp_158,dn_tmp_159,dn_tmp_160}); });dn_r_MessageField *dn_tmp_161=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_149=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_151=&((v_spec).f_messages);size_t dn_tmp_152=((size_t)(0ULL));&dn_tmp_151->data[dn_index(dn_tmp_152,16)]; }))).f_fields);size_t dn_tmp_150=((size_t)(6ULL));&dn_tmp_149->data[dn_index(dn_tmp_150,32)]; })));*dn_tmp_161=dn_tmp_162; }
{ dn_r_MessageField dn_tmp_176=({ uint32_t dn_tmp_167=((uint32_t)(8ULL));
uint32_t dn_tmp_168=((uint32_t)(8ULL));
uint32_t dn_tmp_169=((uint32_t)(0ULL));
uint32_t dn_tmp_170=((uint32_t)(4ULL));
uint32_t dn_tmp_171=((uint32_t)(0ULL));
uint32_t dn_tmp_172=((uint32_t)(0ULL));
uint32_t dn_tmp_173=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_174=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_167,dn_tmp_168,dn_tmp_169,dn_tmp_170,dn_tmp_171,dn_tmp_172,dn_tmp_173,dn_tmp_174}); });dn_r_MessageField *dn_tmp_175=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_163=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_165=&((v_spec).f_messages);size_t dn_tmp_166=((size_t)(0ULL));&dn_tmp_165->data[dn_index(dn_tmp_166,16)]; }))).f_fields);size_t dn_tmp_164=((size_t)(7ULL));&dn_tmp_163->data[dn_index(dn_tmp_164,32)]; })));*dn_tmp_175=dn_tmp_176; }
{ dn_r_MessageField dn_tmp_190=({ uint32_t dn_tmp_181=((uint32_t)(9ULL));
uint32_t dn_tmp_182=((uint32_t)(9ULL));
uint32_t dn_tmp_183=((uint32_t)(1ULL));
uint32_t dn_tmp_184=((uint32_t)(2ULL));
uint32_t dn_tmp_185=((uint32_t)(0ULL));
uint32_t dn_tmp_186=((uint32_t)(0ULL));
uint32_t dn_tmp_187=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_188=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_181,dn_tmp_182,dn_tmp_183,dn_tmp_184,dn_tmp_185,dn_tmp_186,dn_tmp_187,dn_tmp_188}); });dn_r_MessageField *dn_tmp_189=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_177=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_179=&((v_spec).f_messages);size_t dn_tmp_180=((size_t)(0ULL));&dn_tmp_179->data[dn_index(dn_tmp_180,16)]; }))).f_fields);size_t dn_tmp_178=((size_t)(8ULL));&dn_tmp_177->data[dn_index(dn_tmp_178,32)]; })));*dn_tmp_189=dn_tmp_190; }
{ dn_r_MessageField dn_tmp_204=({ uint32_t dn_tmp_195=((uint32_t)(10ULL));
uint32_t dn_tmp_196=((uint32_t)(2ULL));
uint32_t dn_tmp_197=((uint32_t)(0ULL));
uint32_t dn_tmp_198=((uint32_t)(1ULL));
uint32_t dn_tmp_199=((uint32_t)(0ULL));
uint32_t dn_tmp_200=((uint32_t)(1ULL));
uint32_t dn_tmp_201=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_202=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_195,dn_tmp_196,dn_tmp_197,dn_tmp_198,dn_tmp_199,dn_tmp_200,dn_tmp_201,dn_tmp_202}); });dn_r_MessageField *dn_tmp_203=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_191=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_193=&((v_spec).f_messages);size_t dn_tmp_194=((size_t)(0ULL));&dn_tmp_193->data[dn_index(dn_tmp_194,16)]; }))).f_fields);size_t dn_tmp_192=((size_t)(9ULL));&dn_tmp_191->data[dn_index(dn_tmp_192,32)]; })));*dn_tmp_203=dn_tmp_204; }
{ dn_r_MessageField dn_tmp_218=({ uint32_t dn_tmp_209=((uint32_t)(11ULL));
uint32_t dn_tmp_210=((uint32_t)(6ULL));
uint32_t dn_tmp_211=((uint32_t)(0ULL));
uint32_t dn_tmp_212=((uint32_t)(1ULL));
uint32_t dn_tmp_213=((uint32_t)(8ULL));
uint32_t dn_tmp_214=((uint32_t)(0ULL));
uint32_t dn_tmp_215=((uint32_t)(0ULL));
uint32_t dn_tmp_216=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_209,dn_tmp_210,dn_tmp_211,dn_tmp_212,dn_tmp_213,dn_tmp_214,dn_tmp_215,dn_tmp_216}); });dn_r_MessageField *dn_tmp_217=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_205=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_207=&((v_spec).f_messages);size_t dn_tmp_208=((size_t)(0ULL));&dn_tmp_207->data[dn_index(dn_tmp_208,16)]; }))).f_fields);size_t dn_tmp_206=((size_t)(10ULL));&dn_tmp_205->data[dn_index(dn_tmp_206,32)]; })));*dn_tmp_217=dn_tmp_218; }
{ dn_r_MessageSchema dn_tmp_225=({ uint32_t dn_tmp_221=((uint32_t)(2ULL));
uint32_t dn_tmp_222=((uint32_t)(3ULL));
uint32_t dn_tmp_223=((uint32_t)(3ULL));
dn_f_message_schema(dn_tmp_221,dn_tmp_222,dn_tmp_223); });dn_r_MessageSchema *dn_tmp_224=&((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_219=&((v_spec).f_messages);size_t dn_tmp_220=((size_t)(1ULL));&dn_tmp_219->data[dn_index(dn_tmp_220,16)]; })));*dn_tmp_224=dn_tmp_225; }
{ uint32_t dn_tmp_229=((uint32_t)(0ULL));uint32_t *dn_tmp_228=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_226=&((v_spec).f_messages);size_t dn_tmp_227=((size_t)(1ULL));&dn_tmp_226->data[dn_index(dn_tmp_227,16)]; }))).f_first_minor);*dn_tmp_228=dn_tmp_229; }
{ uint32_t dn_tmp_233=((uint32_t)(4294967295ULL));uint32_t *dn_tmp_232=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_230=&((v_spec).f_messages);size_t dn_tmp_231=((size_t)(1ULL));&dn_tmp_230->data[dn_index(dn_tmp_231,16)]; }))).f_last_minor);*dn_tmp_232=dn_tmp_233; }
{ uint32_t dn_tmp_237=((uint32_t)(0ULL));uint32_t *dn_tmp_236=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_234=&((v_spec).f_messages);size_t dn_tmp_235=((size_t)(1ULL));&dn_tmp_234->data[dn_index(dn_tmp_235,16)]; }))).f_field_count);*dn_tmp_236=dn_tmp_237; }
{ dn_r_NetworkSpec dn_tmp_238=v_spec;return dn_tmp_238; }
}

dn_fail("missing native return");
}
static dn_r_NetworkSpec dn_cb_Echo_spec(void) {
 if(!dn_enter()){return (dn_r_NetworkSpec){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkSpec){0};}
 dn_r_NetworkSpec dn_result=dn_f_Echo_spec();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecPlan dn_f_Echo_plan_1(void) {
{
dn_r_CodecPlan v_plan=({ ((dn_r_CodecPlan){0}); });
{ uint32_t dn_tmp_1=((uint32_t)(1ULL));uint32_t *dn_tmp_0=&((v_plan).f_message);*dn_tmp_0=dn_tmp_1; }
{ dn_r_WireFormat dn_tmp_7=({ uint32_t dn_tmp_2=((uint32_t)(2ULL));
uint32_t dn_tmp_3=((uint32_t)(2ULL));
uint32_t dn_tmp_4=((uint32_t)(2ULL));
uint32_t dn_tmp_5=((uint32_t)(1ULL));
((dn_r_WireFormat){dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5}); });dn_r_WireFormat *dn_tmp_6=&((v_plan).f_wire);*dn_tmp_6=dn_tmp_7; }
{ uint32_t dn_tmp_9=((uint32_t)(1ULL));uint32_t *dn_tmp_8=&((v_plan).f_minor);*dn_tmp_8=dn_tmp_9; }
{ uint32_t dn_tmp_11=((uint32_t)(10ULL));uint32_t *dn_tmp_10=&((v_plan).f_field_count);*dn_tmp_10=dn_tmp_11; }
{ dn_r_MessageField dn_tmp_23=({ uint32_t dn_tmp_14=((uint32_t)(1ULL));
uint32_t dn_tmp_15=((uint32_t)(1ULL));
uint32_t dn_tmp_16=((uint32_t)(1ULL));
uint32_t dn_tmp_17=((uint32_t)(1ULL));
uint32_t dn_tmp_18=((uint32_t)(0ULL));
uint32_t dn_tmp_19=((uint32_t)(0ULL));
uint32_t dn_tmp_20=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_21=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_14,dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_18,dn_tmp_19,dn_tmp_20,dn_tmp_21}); });dn_r_MessageField *dn_tmp_22=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_12=&((v_plan).f_fields);size_t dn_tmp_13=((size_t)(0ULL));&dn_tmp_12->data[dn_index(dn_tmp_13,32)]; })));*dn_tmp_22=dn_tmp_23; }
{ dn_r_MessageField dn_tmp_35=({ uint32_t dn_tmp_26=((uint32_t)(2ULL));
uint32_t dn_tmp_27=((uint32_t)(2ULL));
uint32_t dn_tmp_28=((uint32_t)(0ULL));
uint32_t dn_tmp_29=((uint32_t)(1ULL));
uint32_t dn_tmp_30=((uint32_t)(0ULL));
uint32_t dn_tmp_31=((uint32_t)(0ULL));
uint32_t dn_tmp_32=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_33=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_26,dn_tmp_27,dn_tmp_28,dn_tmp_29,dn_tmp_30,dn_tmp_31,dn_tmp_32,dn_tmp_33}); });dn_r_MessageField *dn_tmp_34=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_24=&((v_plan).f_fields);size_t dn_tmp_25=((size_t)(1ULL));&dn_tmp_24->data[dn_index(dn_tmp_25,32)]; })));*dn_tmp_34=dn_tmp_35; }
{ dn_r_MessageField dn_tmp_47=({ uint32_t dn_tmp_38=((uint32_t)(3ULL));
uint32_t dn_tmp_39=((uint32_t)(3ULL));
uint32_t dn_tmp_40=((uint32_t)(0ULL));
uint32_t dn_tmp_41=((uint32_t)(3ULL));
uint32_t dn_tmp_42=((uint32_t)(0ULL));
uint32_t dn_tmp_43=((uint32_t)(0ULL));
uint32_t dn_tmp_44=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_45=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_38,dn_tmp_39,dn_tmp_40,dn_tmp_41,dn_tmp_42,dn_tmp_43,dn_tmp_44,dn_tmp_45}); });dn_r_MessageField *dn_tmp_46=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_36=&((v_plan).f_fields);size_t dn_tmp_37=((size_t)(2ULL));&dn_tmp_36->data[dn_index(dn_tmp_37,32)]; })));*dn_tmp_46=dn_tmp_47; }
{ dn_r_MessageField dn_tmp_59=({ uint32_t dn_tmp_50=((uint32_t)(4ULL));
uint32_t dn_tmp_51=((uint32_t)(4ULL));
uint32_t dn_tmp_52=((uint32_t)(1ULL));
uint32_t dn_tmp_53=((uint32_t)(1ULL));
uint32_t dn_tmp_54=((uint32_t)(0ULL));
uint32_t dn_tmp_55=((uint32_t)(0ULL));
uint32_t dn_tmp_56=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_57=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_50,dn_tmp_51,dn_tmp_52,dn_tmp_53,dn_tmp_54,dn_tmp_55,dn_tmp_56,dn_tmp_57}); });dn_r_MessageField *dn_tmp_58=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_48=&((v_plan).f_fields);size_t dn_tmp_49=((size_t)(3ULL));&dn_tmp_48->data[dn_index(dn_tmp_49,32)]; })));*dn_tmp_58=dn_tmp_59; }
{ dn_r_MessageField dn_tmp_71=({ uint32_t dn_tmp_62=((uint32_t)(5ULL));
uint32_t dn_tmp_63=((uint32_t)(5ULL));
uint32_t dn_tmp_64=((uint32_t)(1ULL));
uint32_t dn_tmp_65=((uint32_t)(1ULL));
uint32_t dn_tmp_66=((uint32_t)(0ULL));
uint32_t dn_tmp_67=((uint32_t)(0ULL));
uint32_t dn_tmp_68=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_69=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_62,dn_tmp_63,dn_tmp_64,dn_tmp_65,dn_tmp_66,dn_tmp_67,dn_tmp_68,dn_tmp_69}); });dn_r_MessageField *dn_tmp_70=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_60=&((v_plan).f_fields);size_t dn_tmp_61=((size_t)(4ULL));&dn_tmp_60->data[dn_index(dn_tmp_61,32)]; })));*dn_tmp_70=dn_tmp_71; }
{ dn_r_MessageField dn_tmp_83=({ uint32_t dn_tmp_74=((uint32_t)(6ULL));
uint32_t dn_tmp_75=((uint32_t)(6ULL));
uint32_t dn_tmp_76=((uint32_t)(0ULL));
uint32_t dn_tmp_77=((uint32_t)(2ULL));
uint32_t dn_tmp_78=((uint32_t)(32ULL));
uint32_t dn_tmp_79=((uint32_t)(0ULL));
uint32_t dn_tmp_80=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_81=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_74,dn_tmp_75,dn_tmp_76,dn_tmp_77,dn_tmp_78,dn_tmp_79,dn_tmp_80,dn_tmp_81}); });dn_r_MessageField *dn_tmp_82=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_72=&((v_plan).f_fields);size_t dn_tmp_73=((size_t)(5ULL));&dn_tmp_72->data[dn_index(dn_tmp_73,32)]; })));*dn_tmp_82=dn_tmp_83; }
{ dn_r_MessageField dn_tmp_95=({ uint32_t dn_tmp_86=((uint32_t)(7ULL));
uint32_t dn_tmp_87=((uint32_t)(7ULL));
uint32_t dn_tmp_88=((uint32_t)(0ULL));
uint32_t dn_tmp_89=((uint32_t)(1ULL));
uint32_t dn_tmp_90=((uint32_t)(64ULL));
uint32_t dn_tmp_91=((uint32_t)(0ULL));
uint32_t dn_tmp_92=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_93=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_86,dn_tmp_87,dn_tmp_88,dn_tmp_89,dn_tmp_90,dn_tmp_91,dn_tmp_92,dn_tmp_93}); });dn_r_MessageField *dn_tmp_94=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_84=&((v_plan).f_fields);size_t dn_tmp_85=((size_t)(6ULL));&dn_tmp_84->data[dn_index(dn_tmp_85,32)]; })));*dn_tmp_94=dn_tmp_95; }
{ dn_r_MessageField dn_tmp_107=({ uint32_t dn_tmp_98=((uint32_t)(8ULL));
uint32_t dn_tmp_99=((uint32_t)(8ULL));
uint32_t dn_tmp_100=((uint32_t)(0ULL));
uint32_t dn_tmp_101=((uint32_t)(4ULL));
uint32_t dn_tmp_102=((uint32_t)(0ULL));
uint32_t dn_tmp_103=((uint32_t)(0ULL));
uint32_t dn_tmp_104=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_105=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_98,dn_tmp_99,dn_tmp_100,dn_tmp_101,dn_tmp_102,dn_tmp_103,dn_tmp_104,dn_tmp_105}); });dn_r_MessageField *dn_tmp_106=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_96=&((v_plan).f_fields);size_t dn_tmp_97=((size_t)(7ULL));&dn_tmp_96->data[dn_index(dn_tmp_97,32)]; })));*dn_tmp_106=dn_tmp_107; }
{ dn_r_MessageField dn_tmp_119=({ uint32_t dn_tmp_110=((uint32_t)(9ULL));
uint32_t dn_tmp_111=((uint32_t)(9ULL));
uint32_t dn_tmp_112=((uint32_t)(1ULL));
uint32_t dn_tmp_113=((uint32_t)(2ULL));
uint32_t dn_tmp_114=((uint32_t)(0ULL));
uint32_t dn_tmp_115=((uint32_t)(0ULL));
uint32_t dn_tmp_116=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_117=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_110,dn_tmp_111,dn_tmp_112,dn_tmp_113,dn_tmp_114,dn_tmp_115,dn_tmp_116,dn_tmp_117}); });dn_r_MessageField *dn_tmp_118=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_108=&((v_plan).f_fields);size_t dn_tmp_109=((size_t)(8ULL));&dn_tmp_108->data[dn_index(dn_tmp_109,32)]; })));*dn_tmp_118=dn_tmp_119; }
{ dn_r_MessageField dn_tmp_131=({ uint32_t dn_tmp_122=((uint32_t)(10ULL));
uint32_t dn_tmp_123=((uint32_t)(2ULL));
uint32_t dn_tmp_124=((uint32_t)(0ULL));
uint32_t dn_tmp_125=((uint32_t)(1ULL));
uint32_t dn_tmp_126=((uint32_t)(0ULL));
uint32_t dn_tmp_127=((uint32_t)(1ULL));
uint32_t dn_tmp_128=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_129=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_122,dn_tmp_123,dn_tmp_124,dn_tmp_125,dn_tmp_126,dn_tmp_127,dn_tmp_128,dn_tmp_129}); });dn_r_MessageField *dn_tmp_130=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_120=&((v_plan).f_fields);size_t dn_tmp_121=((size_t)(9ULL));&dn_tmp_120->data[dn_index(dn_tmp_121,32)]; })));*dn_tmp_130=dn_tmp_131; }
{ uint32_t dn_tmp_133=((uint32_t)(17ULL));uint32_t *dn_tmp_132=&((v_plan).f_maximum_values);*dn_tmp_132=dn_tmp_133; }
{ size_t dn_tmp_135=((size_t)(618ULL));size_t *dn_tmp_134=&((v_plan).f_maximum_wire);*dn_tmp_134=dn_tmp_135; }
{ uint32_t dn_tmp_137=((uint32_t)(4ULL));uint32_t *dn_tmp_136=&((v_plan).f_maximum_unknown_fields);*dn_tmp_136=dn_tmp_137; }
{ size_t dn_tmp_139=((size_t)(256ULL));size_t *dn_tmp_138=&((v_plan).f_maximum_unknown_bytes);*dn_tmp_138=dn_tmp_139; }
{ size_t dn_tmp_141=((size_t)(8388608ULL));size_t *dn_tmp_140=&((v_plan).f_storage_budget);*dn_tmp_140=dn_tmp_141; }
{ dn_r_CodecPlan dn_tmp_142=v_plan;return dn_tmp_142; }
}

dn_fail("missing native return");
}
static dn_r_CodecPlan dn_cb_Echo_plan_1(void) {
 if(!dn_enter()){return (dn_r_CodecPlan){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecPlan){0};}
 dn_r_CodecPlan dn_result=dn_f_Echo_plan_1();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_EchoMessage1 dn_f_EchoMessage1_new(void) {
{
{ dn_r_EchoMessage1 dn_tmp_2=({ dn_r_CodecMessage dn_tmp_0=({ uint32_t dn_tmp_1=((uint32_t)(1ULL));
dn_f_codec_message(dn_tmp_1); });
dn_adopt_r12_CodecMessage(&dn_tmp_0);
((dn_r_EchoMessage1){dn_tmp_0}); });dn_adopt_r12_EchoMessage1(&dn_tmp_2);return dn_tmp_2; }
}

dn_fail("missing native return");
}
static dn_r_EchoMessage1 dn_cb_EchoMessage1_new(void) {
 if(!dn_enter()){return (dn_r_EchoMessage1){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_EchoMessage1){0};}
 dn_r_EchoMessage1 dn_result=dn_f_EchoMessage1_new();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_EchoMessage1Decoded dn_f_EchoMessage1_decode(dn_t_slice0_U8 v_input) {
{
dn_r_CodecDecoded volatile v_decoded={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r12_CodecDecoded(&v_decoded);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
v_decoded=({ dn_r_CodecPlan const * dn_tmp_2=(&(v_plan));
dn_t_slice0_U8 dn_tmp_3=v_input;
dn_f_codec_decode(dn_tmp_2,dn_tmp_3); });
{ dn_r_EchoMessage1Decoded dn_tmp_8=({ dn_r_EchoMessage1 dn_tmp_4=({ dn_r_CodecMessage dn_tmp_5=({ dn_r_CodecMessage dn_tmp_6=(v_decoded).f_value;(v_decoded).f_value=(dn_r_CodecMessage){0};dn_adopt_r12_CodecMessage(&dn_tmp_6);dn_tmp_6; });
dn_adopt_r12_CodecMessage(&dn_tmp_5);
((dn_r_EchoMessage1){dn_tmp_5}); });
dn_adopt_r12_EchoMessage1(&dn_tmp_4);
dn_r_CodecError dn_tmp_7=(v_decoded).f_error;
((dn_r_EchoMessage1Decoded){dn_tmp_4,dn_tmp_7}); });dn_adopt_r19_EchoMessage1Decoded(&dn_tmp_8);dn_drop_r12_CodecDecoded(&v_decoded);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_8; }
dn_drop_r12_CodecDecoded(&v_decoded);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_r_EchoMessage1Decoded dn_cb_EchoMessage1_decode(dn_t_slice0_U8 v_input) {
 if(!dn_enter()){return (dn_r_EchoMessage1Decoded){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_EchoMessage1Decoded){0};}
 dn_r_EchoMessage1Decoded dn_result=dn_f_EchoMessage1_decode(v_input);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecIo dn_f_EchoMessage1_measure(dn_r_EchoMessage1 const * v_message) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
{ dn_r_CodecIo dn_tmp_2=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
dn_f_codec_measure(dn_tmp_0,dn_tmp_1); });return dn_tmp_2; }
}

dn_fail("missing native return");
}
static dn_r_CodecIo dn_cb_EchoMessage1_measure(dn_r_EchoMessage1 const * v_message) {
 if(!dn_enter()){return (dn_r_CodecIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecIo){0};}
 dn_r_CodecIo dn_result=dn_f_EchoMessage1_measure(v_message);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecIo dn_f_EchoMessage1_encode(dn_r_EchoMessage1 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
{ dn_r_CodecIo dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
dn_t_buf_U8 * dn_tmp_2=v_output;
size_t dn_tmp_3=v_offset;
dn_f_codec_encode(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecIo dn_cb_EchoMessage1_encode(dn_r_EchoMessage1 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset) {
 if(!dn_enter()){return (dn_r_CodecIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecIo){0};}
 dn_r_CodecIo dn_result=dn_f_EchoMessage1_encode(v_message,v_output,v_offset);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_EchoMessage1_equal(dn_r_EchoMessage1 const * v_left,dn_r_EchoMessage1 const * v_right) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
{ bool dn_tmp_3=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_left)->f_inner));
dn_r_CodecMessage const * dn_tmp_2=(&((v_right)->f_inner));
dn_f_codec_equal(dn_tmp_0,dn_tmp_1,dn_tmp_2); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static bool dn_cb_EchoMessage1_equal(dn_r_EchoMessage1 const * v_left,dn_r_EchoMessage1 const * v_right) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_EchoMessage1_equal(v_left,v_right);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_field_1_add(dn_r_EchoMessage1 * v_message,uint8_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(1ULL));
uint64_t dn_tmp_3=({ uint8_t dn_tmp_4=v_value;
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_4; });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_field_1_add(dn_r_EchoMessage1 * v_message,uint8_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_field_1_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_EchoMessage1Field1 dn_f_EchoMessage1_field_1_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(1ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_EchoMessage1Field1 dn_tmp_7=({ uint8_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_5; });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_EchoMessage1Field1){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_EchoMessage1Field1 dn_cb_EchoMessage1_field_1_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_EchoMessage1Field1){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_EchoMessage1Field1){0};}
 dn_r_EchoMessage1Field1 dn_result=dn_f_EchoMessage1_field_1_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_field_1_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U8 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(1ULL));
size_t dn_tmp_3=({ dn_t_slice0_U8 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_U8 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(1ULL));
uint32_t dn_tmp_16=((uint32_t)(1ULL));
uint64_t dn_tmp_17=({ uint8_t dn_tmp_18=(*({ dn_t_slice0_U8 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_18; });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_field_1_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U8 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_field_1_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_field_2_add(dn_r_EchoMessage1 * v_message,uint16_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(2ULL));
uint64_t dn_tmp_3=({ uint16_t dn_tmp_4=v_value;
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_4; });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_field_2_add(dn_r_EchoMessage1 * v_message,uint16_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_field_2_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_EchoMessage1Field2 dn_f_EchoMessage1_field_2_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(2ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_EchoMessage1Field2 dn_tmp_7=({ uint16_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<16)-1))dn_fail("numeric conversion out of range");(uint16_t)dn_tmp_5; });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_EchoMessage1Field2){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_EchoMessage1Field2 dn_cb_EchoMessage1_field_2_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_EchoMessage1Field2){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_EchoMessage1Field2){0};}
 dn_r_EchoMessage1Field2 dn_result=dn_f_EchoMessage1_field_2_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_field_2_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U16 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(2ULL));
size_t dn_tmp_3=({ dn_t_slice0_U16 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_U16 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(2ULL));
uint32_t dn_tmp_16=((uint32_t)(2ULL));
uint64_t dn_tmp_17=({ uint16_t dn_tmp_18=(*({ dn_t_slice0_U16 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_18; });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_field_2_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U16 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_field_2_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_field_3_add(dn_r_EchoMessage1 * v_message,uint32_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(3ULL));
uint64_t dn_tmp_3=({ uint32_t dn_tmp_4=v_value;
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_4; });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_field_3_add(dn_r_EchoMessage1 * v_message,uint32_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_field_3_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_EchoMessage1Field3 dn_f_EchoMessage1_field_3_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(3ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_EchoMessage1Field3 dn_tmp_7=({ uint32_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_5; });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_EchoMessage1Field3){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_EchoMessage1Field3 dn_cb_EchoMessage1_field_3_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_EchoMessage1Field3){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_EchoMessage1Field3){0};}
 dn_r_EchoMessage1Field3 dn_result=dn_f_EchoMessage1_field_3_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_field_3_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U32 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(3ULL));
size_t dn_tmp_3=({ dn_t_slice0_U32 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_U32 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(3ULL));
uint32_t dn_tmp_16=((uint32_t)(3ULL));
uint64_t dn_tmp_17=({ uint32_t dn_tmp_18=(*({ dn_t_slice0_U32 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_18; });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_field_3_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U32 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_field_3_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_field_4_add(dn_r_EchoMessage1 * v_message,uint64_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(4ULL));
uint64_t dn_tmp_3=({ uint64_t dn_tmp_4=v_value;
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_4; });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_field_4_add(dn_r_EchoMessage1 * v_message,uint64_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_field_4_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_EchoMessage1Field4 dn_f_EchoMessage1_field_4_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(4ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_EchoMessage1Field4 dn_tmp_7=({ uint64_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_5; });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_EchoMessage1Field4){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_EchoMessage1Field4 dn_cb_EchoMessage1_field_4_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_EchoMessage1Field4){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_EchoMessage1Field4){0};}
 dn_r_EchoMessage1Field4 dn_result=dn_f_EchoMessage1_field_4_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_field_4_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U64 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(4ULL));
size_t dn_tmp_3=({ dn_t_slice0_U64 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_U64 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(4ULL));
uint32_t dn_tmp_16=((uint32_t)(4ULL));
uint64_t dn_tmp_17=({ uint64_t dn_tmp_18=(*({ dn_t_slice0_U64 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_18; });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_field_4_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U64 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_field_4_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_field_5_add(dn_r_EchoMessage1 * v_message,bool v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
uint64_t v_scalar=((uint64_t)(0ULL));
if(v_value){
{ uint64_t dn_tmp_1=((uint64_t)(1ULL));uint64_t *dn_tmp_0=&(v_scalar);*dn_tmp_0=dn_tmp_1; }
}
else{
}
{ dn_r_CodecError dn_tmp_6=({ dn_r_CodecPlan const * dn_tmp_2=(&(v_plan));
dn_r_CodecMessage * dn_tmp_3=(&((v_message)->f_inner));
uint32_t dn_tmp_4=((uint32_t)(5ULL));
uint64_t dn_tmp_5=v_scalar;
dn_f_codec_add_scalar(dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5); });return dn_tmp_6; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_field_5_add(dn_r_EchoMessage1 * v_message,bool v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_field_5_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_EchoMessage1Field5 dn_f_EchoMessage1_field_5_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(5ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_EchoMessage1Field5 dn_tmp_8=({ bool dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
uint64_t dn_tmp_6=((uint64_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); });
dn_r_CodecError dn_tmp_7=(v_value).f_error;
((dn_r_EchoMessage1Field5){dn_tmp_4,dn_tmp_7}); });return dn_tmp_8; }
}

dn_fail("missing native return");
}
static dn_r_EchoMessage1Field5 dn_cb_EchoMessage1_field_5_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_EchoMessage1Field5){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_EchoMessage1Field5){0};}
 dn_r_EchoMessage1Field5 dn_result=dn_f_EchoMessage1_field_5_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_field_5_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_b v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(5ULL));
size_t dn_tmp_3=({ dn_t_slice0_b dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_b dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
uint64_t v_scalar=((uint64_t)(0ULL));
if((*({ dn_t_slice0_b *dn_tmp_11=&(v_values);size_t dn_tmp_12=v_index;&dn_tmp_11->data[dn_tmp_12]; }))){
{ uint64_t dn_tmp_14=((uint64_t)(1ULL));uint64_t *dn_tmp_13=&(v_scalar);*dn_tmp_13=dn_tmp_14; }
}
else{
}
{ dn_r_CodecValue dn_tmp_25=({ uint32_t dn_tmp_19=((uint32_t)(5ULL));
uint32_t dn_tmp_20=((uint32_t)(5ULL));
uint64_t dn_tmp_21=v_scalar;
size_t dn_tmp_22=((size_t)(0ULL));
size_t dn_tmp_23=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_19,dn_tmp_20,dn_tmp_21,dn_tmp_22,dn_tmp_23}); });dn_r_CodecValue *dn_tmp_24=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_15=&(((v_message)->f_inner).f_values);size_t dn_tmp_16=({ size_t dn_tmp_17=(v_reserved).f_start;
size_t dn_tmp_18=v_index;
dn_USize_add(dn_tmp_17,dn_tmp_18); });&dn_tmp_15->data[dn_index(dn_tmp_16,dn_tmp_15->len)]; })));*dn_tmp_24=dn_tmp_25; }
}
 }
{ dn_r_CodecError dn_tmp_29=({ int32_t dn_tmp_26=((int32_t)(0ULL));
uint32_t dn_tmp_27=((uint32_t)(0ULL));
size_t dn_tmp_28=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_26,dn_tmp_27,dn_tmp_28}); });return dn_tmp_29; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_field_5_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_b v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_field_5_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_field_6_add(dn_r_EchoMessage1 * v_message,dn_t_slice0_U8 v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
{ dn_r_CodecError dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(6ULL));
dn_t_slice0_U8 dn_tmp_3=v_value;
dn_f_codec_add_bytes(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_field_6_add(dn_r_EchoMessage1 * v_message,dn_t_slice0_U8 v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_field_6_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecSpan dn_f_EchoMessage1_field_6_span(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
{ dn_r_CodecSpan dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(6ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_span(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecSpan dn_cb_EchoMessage1_field_6_span(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_CodecSpan){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecSpan){0};}
 dn_r_CodecSpan dn_result=dn_f_EchoMessage1_field_6_span(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_slice0_U8 dn_f_EchoMessage1_field_6_view(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecSpan v_span=({ dn_r_EchoMessage1 const * dn_tmp_0=v_message;
size_t dn_tmp_1=v_occurrence;
dn_f_EchoMessage1_field_6_span(dn_tmp_0,dn_tmp_1); });
{ dn_t_slice0_U8 dn_tmp_4=({ dn_r_CodecMessage const * dn_tmp_2=(&((v_message)->f_inner));
dn_r_CodecSpan dn_tmp_3=v_span;
dn_f_codec_view(dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_t_slice0_U8 dn_cb_EchoMessage1_field_6_view(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_t_slice0_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_slice0_U8){0};}
 dn_t_slice0_U8 dn_result=dn_f_EchoMessage1_field_6_view(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_field_7_add(dn_r_EchoMessage1 * v_message,dn_t_slice0_U8 v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
{ dn_r_CodecError dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(7ULL));
dn_t_slice0_U8 dn_tmp_3=v_value;
dn_f_codec_add_bytes(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_field_7_add(dn_r_EchoMessage1 * v_message,dn_t_slice0_U8 v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_field_7_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecSpan dn_f_EchoMessage1_field_7_span(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
{ dn_r_CodecSpan dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(7ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_span(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecSpan dn_cb_EchoMessage1_field_7_span(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_CodecSpan){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecSpan){0};}
 dn_r_CodecSpan dn_result=dn_f_EchoMessage1_field_7_span(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_slice0_U8 dn_f_EchoMessage1_field_7_view(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecSpan v_span=({ dn_r_EchoMessage1 const * dn_tmp_0=v_message;
size_t dn_tmp_1=v_occurrence;
dn_f_EchoMessage1_field_7_span(dn_tmp_0,dn_tmp_1); });
{ dn_t_slice0_U8 dn_tmp_4=({ dn_r_CodecMessage const * dn_tmp_2=(&((v_message)->f_inner));
dn_r_CodecSpan dn_tmp_3=v_span;
dn_f_codec_view(dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_t_slice0_U8 dn_cb_EchoMessage1_field_7_view(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_t_slice0_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_slice0_U8){0};}
 dn_t_slice0_U8 dn_result=dn_f_EchoMessage1_field_7_view(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_field_8_add(dn_r_EchoMessage1 * v_message,uint64_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(8ULL));
uint64_t dn_tmp_3=({ uint64_t dn_tmp_4=v_value;
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_4; });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_field_8_add(dn_r_EchoMessage1 * v_message,uint64_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_field_8_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_EchoMessage1Field8 dn_f_EchoMessage1_field_8_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(8ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_EchoMessage1Field8 dn_tmp_7=({ uint64_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_5; });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_EchoMessage1Field8){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_EchoMessage1Field8 dn_cb_EchoMessage1_field_8_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_EchoMessage1Field8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_EchoMessage1Field8){0};}
 dn_r_EchoMessage1Field8 dn_result=dn_f_EchoMessage1_field_8_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_field_8_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U64 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(8ULL));
size_t dn_tmp_3=({ dn_t_slice0_U64 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_U64 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(8ULL));
uint32_t dn_tmp_16=((uint32_t)(8ULL));
uint64_t dn_tmp_17=({ uint64_t dn_tmp_18=(*({ dn_t_slice0_U64 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_18; });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_field_8_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U64 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_field_8_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_field_9_add(dn_r_EchoMessage1 * v_message,int64_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(9ULL));
uint64_t dn_tmp_3=({ int64_t dn_tmp_4=v_value;
dn_f_zigzag_encode(dn_tmp_4); });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_field_9_add(dn_r_EchoMessage1 * v_message,int64_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_field_9_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_EchoMessage1Field9 dn_f_EchoMessage1_field_9_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(9ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_EchoMessage1Field9 dn_tmp_7=({ int64_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
dn_f_zigzag_decode(dn_tmp_5); });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_EchoMessage1Field9){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_EchoMessage1Field9 dn_cb_EchoMessage1_field_9_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_EchoMessage1Field9){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_EchoMessage1Field9){0};}
 dn_r_EchoMessage1Field9 dn_result=dn_f_EchoMessage1_field_9_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_field_9_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_I64 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(9ULL));
size_t dn_tmp_3=({ dn_t_slice0_I64 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_I64 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(9ULL));
uint32_t dn_tmp_16=((uint32_t)(9ULL));
uint64_t dn_tmp_17=({ int64_t dn_tmp_18=(*({ dn_t_slice0_I64 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
dn_f_zigzag_encode(dn_tmp_18); });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_field_9_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_I64 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_field_9_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_field_10_add(dn_r_EchoMessage1 * v_message,uint16_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(10ULL));
uint64_t dn_tmp_3=({ uint16_t dn_tmp_4=v_value;
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_4; });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_field_10_add(dn_r_EchoMessage1 * v_message,uint16_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_field_10_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_EchoMessage1Field10 dn_f_EchoMessage1_field_10_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(10ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_EchoMessage1Field10 dn_tmp_7=({ uint16_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<16)-1))dn_fail("numeric conversion out of range");(uint16_t)dn_tmp_5; });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_EchoMessage1Field10){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_EchoMessage1Field10 dn_cb_EchoMessage1_field_10_get(dn_r_EchoMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_EchoMessage1Field10){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_EchoMessage1Field10){0};}
 dn_r_EchoMessage1Field10 dn_result=dn_f_EchoMessage1_field_10_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_field_10_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U16 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(10ULL));
size_t dn_tmp_3=({ dn_t_slice0_U16 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_U16 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(10ULL));
uint32_t dn_tmp_16=((uint32_t)(2ULL));
uint64_t dn_tmp_17=({ uint16_t dn_tmp_18=(*({ dn_t_slice0_U16 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_18; });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_field_10_add_many(dn_r_EchoMessage1 * v_message,dn_t_slice0_U16 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_field_10_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage1_add_unknown(dn_r_EchoMessage1 * v_message,dn_t_slice0_U8 v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
{ dn_r_CodecError dn_tmp_3=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
dn_t_slice0_U8 dn_tmp_2=v_value;
dn_f_codec_add_unknown(dn_tmp_0,dn_tmp_1,dn_tmp_2); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage1_add_unknown(dn_r_EchoMessage1 * v_message,dn_t_slice0_U8 v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage1_add_unknown(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecPlan dn_f_Echo_plan_2(void) {
{
dn_r_CodecPlan v_plan=({ ((dn_r_CodecPlan){0}); });
{ uint32_t dn_tmp_1=((uint32_t)(2ULL));uint32_t *dn_tmp_0=&((v_plan).f_message);*dn_tmp_0=dn_tmp_1; }
{ dn_r_WireFormat dn_tmp_7=({ uint32_t dn_tmp_2=((uint32_t)(2ULL));
uint32_t dn_tmp_3=((uint32_t)(2ULL));
uint32_t dn_tmp_4=((uint32_t)(2ULL));
uint32_t dn_tmp_5=((uint32_t)(1ULL));
((dn_r_WireFormat){dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5}); });dn_r_WireFormat *dn_tmp_6=&((v_plan).f_wire);*dn_tmp_6=dn_tmp_7; }
{ uint32_t dn_tmp_9=((uint32_t)(1ULL));uint32_t *dn_tmp_8=&((v_plan).f_minor);*dn_tmp_8=dn_tmp_9; }
{ uint32_t dn_tmp_11=((uint32_t)(0ULL));uint32_t *dn_tmp_10=&((v_plan).f_field_count);*dn_tmp_10=dn_tmp_11; }
{ uint32_t dn_tmp_13=((uint32_t)(0ULL));uint32_t *dn_tmp_12=&((v_plan).f_maximum_values);*dn_tmp_12=dn_tmp_13; }
{ size_t dn_tmp_15=((size_t)(268ULL));size_t *dn_tmp_14=&((v_plan).f_maximum_wire);*dn_tmp_14=dn_tmp_15; }
{ uint32_t dn_tmp_17=((uint32_t)(4ULL));uint32_t *dn_tmp_16=&((v_plan).f_maximum_unknown_fields);*dn_tmp_16=dn_tmp_17; }
{ size_t dn_tmp_19=((size_t)(256ULL));size_t *dn_tmp_18=&((v_plan).f_maximum_unknown_bytes);*dn_tmp_18=dn_tmp_19; }
{ size_t dn_tmp_21=((size_t)(8388608ULL));size_t *dn_tmp_20=&((v_plan).f_storage_budget);*dn_tmp_20=dn_tmp_21; }
{ dn_r_CodecPlan dn_tmp_22=v_plan;return dn_tmp_22; }
}

dn_fail("missing native return");
}
static dn_r_CodecPlan dn_cb_Echo_plan_2(void) {
 if(!dn_enter()){return (dn_r_CodecPlan){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecPlan){0};}
 dn_r_CodecPlan dn_result=dn_f_Echo_plan_2();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_EchoMessage2 dn_f_EchoMessage2_new(void) {
{
{ dn_r_EchoMessage2 dn_tmp_2=({ dn_r_CodecMessage dn_tmp_0=({ uint32_t dn_tmp_1=((uint32_t)(2ULL));
dn_f_codec_message(dn_tmp_1); });
dn_adopt_r12_CodecMessage(&dn_tmp_0);
((dn_r_EchoMessage2){dn_tmp_0}); });dn_adopt_r12_EchoMessage2(&dn_tmp_2);return dn_tmp_2; }
}

dn_fail("missing native return");
}
static dn_r_EchoMessage2 dn_cb_EchoMessage2_new(void) {
 if(!dn_enter()){return (dn_r_EchoMessage2){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_EchoMessage2){0};}
 dn_r_EchoMessage2 dn_result=dn_f_EchoMessage2_new();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_EchoMessage2Decoded dn_f_EchoMessage2_decode(dn_t_slice0_U8 v_input) {
{
dn_r_CodecDecoded volatile v_decoded={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r12_CodecDecoded(&v_decoded);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_2(); });
v_decoded=({ dn_r_CodecPlan const * dn_tmp_2=(&(v_plan));
dn_t_slice0_U8 dn_tmp_3=v_input;
dn_f_codec_decode(dn_tmp_2,dn_tmp_3); });
{ dn_r_EchoMessage2Decoded dn_tmp_8=({ dn_r_EchoMessage2 dn_tmp_4=({ dn_r_CodecMessage dn_tmp_5=({ dn_r_CodecMessage dn_tmp_6=(v_decoded).f_value;(v_decoded).f_value=(dn_r_CodecMessage){0};dn_adopt_r12_CodecMessage(&dn_tmp_6);dn_tmp_6; });
dn_adopt_r12_CodecMessage(&dn_tmp_5);
((dn_r_EchoMessage2){dn_tmp_5}); });
dn_adopt_r12_EchoMessage2(&dn_tmp_4);
dn_r_CodecError dn_tmp_7=(v_decoded).f_error;
((dn_r_EchoMessage2Decoded){dn_tmp_4,dn_tmp_7}); });dn_adopt_r19_EchoMessage2Decoded(&dn_tmp_8);dn_drop_r12_CodecDecoded(&v_decoded);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_8; }
dn_drop_r12_CodecDecoded(&v_decoded);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_r_EchoMessage2Decoded dn_cb_EchoMessage2_decode(dn_t_slice0_U8 v_input) {
 if(!dn_enter()){return (dn_r_EchoMessage2Decoded){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_EchoMessage2Decoded){0};}
 dn_r_EchoMessage2Decoded dn_result=dn_f_EchoMessage2_decode(v_input);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecIo dn_f_EchoMessage2_measure(dn_r_EchoMessage2 const * v_message) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_2(); });
{ dn_r_CodecIo dn_tmp_2=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
dn_f_codec_measure(dn_tmp_0,dn_tmp_1); });return dn_tmp_2; }
}

dn_fail("missing native return");
}
static dn_r_CodecIo dn_cb_EchoMessage2_measure(dn_r_EchoMessage2 const * v_message) {
 if(!dn_enter()){return (dn_r_CodecIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecIo){0};}
 dn_r_CodecIo dn_result=dn_f_EchoMessage2_measure(v_message);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecIo dn_f_EchoMessage2_encode(dn_r_EchoMessage2 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_2(); });
{ dn_r_CodecIo dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
dn_t_buf_U8 * dn_tmp_2=v_output;
size_t dn_tmp_3=v_offset;
dn_f_codec_encode(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecIo dn_cb_EchoMessage2_encode(dn_r_EchoMessage2 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset) {
 if(!dn_enter()){return (dn_r_CodecIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecIo){0};}
 dn_r_CodecIo dn_result=dn_f_EchoMessage2_encode(v_message,v_output,v_offset);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_EchoMessage2_equal(dn_r_EchoMessage2 const * v_left,dn_r_EchoMessage2 const * v_right) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_2(); });
{ bool dn_tmp_3=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_left)->f_inner));
dn_r_CodecMessage const * dn_tmp_2=(&((v_right)->f_inner));
dn_f_codec_equal(dn_tmp_0,dn_tmp_1,dn_tmp_2); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static bool dn_cb_EchoMessage2_equal(dn_r_EchoMessage2 const * v_left,dn_r_EchoMessage2 const * v_right) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_EchoMessage2_equal(v_left,v_right);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_EchoMessage2_add_unknown(dn_r_EchoMessage2 * v_message,dn_t_slice0_U8 v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_2(); });
{ dn_r_CodecError dn_tmp_3=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
dn_t_slice0_U8 dn_tmp_2=v_value;
dn_f_codec_add_unknown(dn_tmp_0,dn_tmp_1,dn_tmp_2); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_EchoMessage2_add_unknown(dn_r_EchoMessage2 * v_message,dn_t_slice0_U8 v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_EchoMessage2_add_unknown(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecDecoded dn_f_Echo_decode(dn_t_slice0_U8 v_input) {
{
dn_r_ProtocolValue v_id=({ dn_t_slice0_U8 dn_tmp_0=v_input;
size_t dn_tmp_1=((size_t)(0ULL));
size_t dn_tmp_2=((size_t)(4ULL));
dn_f_net_read_be(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
if(({ int32_t dn_tmp_3=(v_id).f_status;
int32_t dn_tmp_4=((int32_t)(0ULL));
(dn_tmp_3 != dn_tmp_4); })){
{ dn_r_CodecDecoded dn_tmp_8=({ int32_t dn_tmp_5=(v_id).f_status;
uint32_t dn_tmp_6=((uint32_t)(0ULL));
size_t dn_tmp_7=((size_t)(0ULL));
dn_f_codec_decode_error(dn_tmp_5,dn_tmp_6,dn_tmp_7); });dn_adopt_r12_CodecDecoded(&dn_tmp_8);return dn_tmp_8; }
}
else{
}
if(({ uint64_t dn_tmp_9=(v_id).f_value;
uint64_t dn_tmp_10=({ uint64_t dn_tmp_11=((uint64_t)(1ULL));
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_11; });
(dn_tmp_9 == dn_tmp_10); })){
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_1(); });
{ dn_r_CodecDecoded dn_tmp_14=({ dn_r_CodecPlan const * dn_tmp_12=(&(v_plan));
dn_t_slice0_U8 dn_tmp_13=v_input;
dn_f_codec_decode(dn_tmp_12,dn_tmp_13); });dn_adopt_r12_CodecDecoded(&dn_tmp_14);return dn_tmp_14; }
}
else{
}
if(({ uint64_t dn_tmp_15=(v_id).f_value;
uint64_t dn_tmp_16=({ uint64_t dn_tmp_17=((uint64_t)(2ULL));
if((unsigned __int128)dn_tmp_17>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_17; });
(dn_tmp_15 == dn_tmp_16); })){
dn_r_CodecPlan v_plan=({ dn_f_Echo_plan_2(); });
{ dn_r_CodecDecoded dn_tmp_20=({ dn_r_CodecPlan const * dn_tmp_18=(&(v_plan));
dn_t_slice0_U8 dn_tmp_19=v_input;
dn_f_codec_decode(dn_tmp_18,dn_tmp_19); });dn_adopt_r12_CodecDecoded(&dn_tmp_20);return dn_tmp_20; }
}
else{
}
{ dn_r_CodecDecoded dn_tmp_24=({ int32_t dn_tmp_21=((int32_t)(11ULL));
uint32_t dn_tmp_22=((uint32_t)(0ULL));
size_t dn_tmp_23=((size_t)(0ULL));
dn_f_codec_decode_error(dn_tmp_21,dn_tmp_22,dn_tmp_23); });dn_adopt_r12_CodecDecoded(&dn_tmp_24);return dn_tmp_24; }
}

dn_fail("missing native return");
}
static dn_r_CodecDecoded dn_cb_Echo_decode(dn_t_slice0_U8 v_input) {
 if(!dn_enter()){return (dn_r_CodecDecoded){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecDecoded){0};}
 dn_r_CodecDecoded dn_result=dn_f_Echo_decode(v_input);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkSpec dn_f_Pos_spec(void) {
{
dn_r_NetworkSpec v_spec=({ uint32_t dn_tmp_0=((uint32_t)(1129268273ULL));
uint32_t dn_tmp_1=((uint32_t)(1ULL));
uint32_t dn_tmp_2=((uint32_t)(1ULL));
uint32_t dn_tmp_3=((uint32_t)(0ULL));
dn_f_network_spec(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_WireFormat dn_tmp_9=({ uint32_t dn_tmp_4=((uint32_t)(1ULL));
uint32_t dn_tmp_5=((uint32_t)(1ULL));
uint32_t dn_tmp_6=((uint32_t)(0ULL));
uint32_t dn_tmp_7=((uint32_t)(0ULL));
((dn_r_WireFormat){dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7}); });dn_r_WireFormat *dn_tmp_8=&((v_spec).f_wire);*dn_tmp_8=dn_tmp_9; }
{ dn_r_TransportRequirements dn_tmp_14=({ uint32_t dn_tmp_10=((uint32_t)(19ULL));
uint32_t dn_tmp_11=((uint32_t)(0ULL));
uint32_t dn_tmp_12=((uint32_t)(0ULL));
((dn_r_TransportRequirements){dn_tmp_10,dn_tmp_11,dn_tmp_12}); });dn_r_TransportRequirements *dn_tmp_13=&((v_spec).f_transport);*dn_tmp_13=dn_tmp_14; }
{ dn_r_ResourceBounds dn_tmp_26=({ dn_r_TransportBounds dn_tmp_15=({ uint32_t dn_tmp_16=((uint32_t)(65536ULL));
uint32_t dn_tmp_17=((uint32_t)(32ULL));
uint32_t dn_tmp_18=((uint32_t)(16ULL));
size_t dn_tmp_19=((size_t)(8388608ULL));
((dn_r_TransportBounds){dn_tmp_16,dn_tmp_17,dn_tmp_18,dn_tmp_19}); });
uint32_t dn_tmp_20=((uint32_t)(0ULL));
uint32_t dn_tmp_21=((uint32_t)(0ULL));
uint64_t dn_tmp_22=({ uint64_t dn_tmp_23=((uint64_t)(1000000ULL));
if((unsigned __int128)dn_tmp_23>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_23; });
uint32_t dn_tmp_24=((uint32_t)(529ULL));
((dn_r_ResourceBounds){dn_tmp_15,dn_tmp_20,dn_tmp_21,dn_tmp_22,dn_tmp_24}); });dn_r_ResourceBounds *dn_tmp_25=&((v_spec).f_resources);*dn_tmp_25=dn_tmp_26; }
{ dn_r_SessionContract dn_tmp_33=({ uint32_t dn_tmp_27=((uint32_t)(10000ULL));
uint32_t dn_tmp_28=((uint32_t)(30000ULL));
uint32_t dn_tmp_29=((uint32_t)(300000ULL));
uint32_t dn_tmp_30=((uint32_t)(32ULL));
bool dn_tmp_31=0;
((dn_r_SessionContract){dn_tmp_27,dn_tmp_28,dn_tmp_29,dn_tmp_30,dn_tmp_31}); });dn_r_SessionContract *dn_tmp_32=&((v_spec).f_session);*dn_tmp_32=dn_tmp_33; }
{ dn_r_SecurityRequirements dn_tmp_39=({ uint32_t dn_tmp_34=((uint32_t)(0ULL));
bool dn_tmp_35=0;
bool dn_tmp_36=0;
uint32_t dn_tmp_37=((uint32_t)(0ULL));
((dn_r_SecurityRequirements){dn_tmp_34,dn_tmp_35,dn_tmp_36,dn_tmp_37}); });dn_r_SecurityRequirements *dn_tmp_38=&((v_spec).f_security);*dn_tmp_38=dn_tmp_39; }
{ dn_r_ProofObligations dn_tmp_43=({ uint32_t dn_tmp_40=((uint32_t)(0ULL));
uint32_t dn_tmp_41=((uint32_t)(1ULL));
((dn_r_ProofObligations){dn_tmp_40,dn_tmp_41}); });dn_r_ProofObligations *dn_tmp_42=&((v_spec).f_proofs);*dn_tmp_42=dn_tmp_43; }
{ uint32_t dn_tmp_45=((uint32_t)(2ULL));uint32_t *dn_tmp_44=&((v_spec).f_message_count);*dn_tmp_44=dn_tmp_45; }
{ dn_r_MessageSchema dn_tmp_52=({ uint32_t dn_tmp_48=((uint32_t)(1ULL));
uint32_t dn_tmp_49=((uint32_t)(2ULL));
uint32_t dn_tmp_50=((uint32_t)(3ULL));
dn_f_message_schema(dn_tmp_48,dn_tmp_49,dn_tmp_50); });dn_r_MessageSchema *dn_tmp_51=&((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_46=&((v_spec).f_messages);size_t dn_tmp_47=((size_t)(0ULL));&dn_tmp_46->data[dn_index(dn_tmp_47,16)]; })));*dn_tmp_51=dn_tmp_52; }
{ uint32_t dn_tmp_56=((uint32_t)(0ULL));uint32_t *dn_tmp_55=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_53=&((v_spec).f_messages);size_t dn_tmp_54=((size_t)(0ULL));&dn_tmp_53->data[dn_index(dn_tmp_54,16)]; }))).f_first_minor);*dn_tmp_55=dn_tmp_56; }
{ uint32_t dn_tmp_60=((uint32_t)(4294967295ULL));uint32_t *dn_tmp_59=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_57=&((v_spec).f_messages);size_t dn_tmp_58=((size_t)(0ULL));&dn_tmp_57->data[dn_index(dn_tmp_58,16)]; }))).f_last_minor);*dn_tmp_59=dn_tmp_60; }
{ uint32_t dn_tmp_64=((uint32_t)(11ULL));uint32_t *dn_tmp_63=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_61=&((v_spec).f_messages);size_t dn_tmp_62=((size_t)(0ULL));&dn_tmp_61->data[dn_index(dn_tmp_62,16)]; }))).f_field_count);*dn_tmp_63=dn_tmp_64; }
{ dn_r_MessageField dn_tmp_78=({ uint32_t dn_tmp_69=((uint32_t)(1ULL));
uint32_t dn_tmp_70=((uint32_t)(1ULL));
uint32_t dn_tmp_71=((uint32_t)(1ULL));
uint32_t dn_tmp_72=((uint32_t)(1ULL));
uint32_t dn_tmp_73=((uint32_t)(0ULL));
uint32_t dn_tmp_74=((uint32_t)(0ULL));
uint32_t dn_tmp_75=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_76=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_69,dn_tmp_70,dn_tmp_71,dn_tmp_72,dn_tmp_73,dn_tmp_74,dn_tmp_75,dn_tmp_76}); });dn_r_MessageField *dn_tmp_77=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_65=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_67=&((v_spec).f_messages);size_t dn_tmp_68=((size_t)(0ULL));&dn_tmp_67->data[dn_index(dn_tmp_68,16)]; }))).f_fields);size_t dn_tmp_66=((size_t)(0ULL));&dn_tmp_65->data[dn_index(dn_tmp_66,32)]; })));*dn_tmp_77=dn_tmp_78; }
{ dn_r_MessageField dn_tmp_92=({ uint32_t dn_tmp_83=((uint32_t)(2ULL));
uint32_t dn_tmp_84=((uint32_t)(2ULL));
uint32_t dn_tmp_85=((uint32_t)(0ULL));
uint32_t dn_tmp_86=((uint32_t)(1ULL));
uint32_t dn_tmp_87=((uint32_t)(0ULL));
uint32_t dn_tmp_88=((uint32_t)(0ULL));
uint32_t dn_tmp_89=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_90=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_83,dn_tmp_84,dn_tmp_85,dn_tmp_86,dn_tmp_87,dn_tmp_88,dn_tmp_89,dn_tmp_90}); });dn_r_MessageField *dn_tmp_91=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_79=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_81=&((v_spec).f_messages);size_t dn_tmp_82=((size_t)(0ULL));&dn_tmp_81->data[dn_index(dn_tmp_82,16)]; }))).f_fields);size_t dn_tmp_80=((size_t)(1ULL));&dn_tmp_79->data[dn_index(dn_tmp_80,32)]; })));*dn_tmp_91=dn_tmp_92; }
{ dn_r_MessageField dn_tmp_106=({ uint32_t dn_tmp_97=((uint32_t)(3ULL));
uint32_t dn_tmp_98=((uint32_t)(3ULL));
uint32_t dn_tmp_99=((uint32_t)(0ULL));
uint32_t dn_tmp_100=((uint32_t)(3ULL));
uint32_t dn_tmp_101=((uint32_t)(0ULL));
uint32_t dn_tmp_102=((uint32_t)(0ULL));
uint32_t dn_tmp_103=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_104=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_97,dn_tmp_98,dn_tmp_99,dn_tmp_100,dn_tmp_101,dn_tmp_102,dn_tmp_103,dn_tmp_104}); });dn_r_MessageField *dn_tmp_105=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_93=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_95=&((v_spec).f_messages);size_t dn_tmp_96=((size_t)(0ULL));&dn_tmp_95->data[dn_index(dn_tmp_96,16)]; }))).f_fields);size_t dn_tmp_94=((size_t)(2ULL));&dn_tmp_93->data[dn_index(dn_tmp_94,32)]; })));*dn_tmp_105=dn_tmp_106; }
{ dn_r_MessageField dn_tmp_120=({ uint32_t dn_tmp_111=((uint32_t)(4ULL));
uint32_t dn_tmp_112=((uint32_t)(4ULL));
uint32_t dn_tmp_113=((uint32_t)(1ULL));
uint32_t dn_tmp_114=((uint32_t)(1ULL));
uint32_t dn_tmp_115=((uint32_t)(0ULL));
uint32_t dn_tmp_116=((uint32_t)(0ULL));
uint32_t dn_tmp_117=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_118=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_111,dn_tmp_112,dn_tmp_113,dn_tmp_114,dn_tmp_115,dn_tmp_116,dn_tmp_117,dn_tmp_118}); });dn_r_MessageField *dn_tmp_119=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_107=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_109=&((v_spec).f_messages);size_t dn_tmp_110=((size_t)(0ULL));&dn_tmp_109->data[dn_index(dn_tmp_110,16)]; }))).f_fields);size_t dn_tmp_108=((size_t)(3ULL));&dn_tmp_107->data[dn_index(dn_tmp_108,32)]; })));*dn_tmp_119=dn_tmp_120; }
{ dn_r_MessageField dn_tmp_134=({ uint32_t dn_tmp_125=((uint32_t)(5ULL));
uint32_t dn_tmp_126=((uint32_t)(5ULL));
uint32_t dn_tmp_127=((uint32_t)(1ULL));
uint32_t dn_tmp_128=((uint32_t)(1ULL));
uint32_t dn_tmp_129=((uint32_t)(0ULL));
uint32_t dn_tmp_130=((uint32_t)(0ULL));
uint32_t dn_tmp_131=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_132=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_125,dn_tmp_126,dn_tmp_127,dn_tmp_128,dn_tmp_129,dn_tmp_130,dn_tmp_131,dn_tmp_132}); });dn_r_MessageField *dn_tmp_133=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_121=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_123=&((v_spec).f_messages);size_t dn_tmp_124=((size_t)(0ULL));&dn_tmp_123->data[dn_index(dn_tmp_124,16)]; }))).f_fields);size_t dn_tmp_122=((size_t)(4ULL));&dn_tmp_121->data[dn_index(dn_tmp_122,32)]; })));*dn_tmp_133=dn_tmp_134; }
{ dn_r_MessageField dn_tmp_148=({ uint32_t dn_tmp_139=((uint32_t)(6ULL));
uint32_t dn_tmp_140=((uint32_t)(6ULL));
uint32_t dn_tmp_141=((uint32_t)(0ULL));
uint32_t dn_tmp_142=((uint32_t)(2ULL));
uint32_t dn_tmp_143=((uint32_t)(32ULL));
uint32_t dn_tmp_144=((uint32_t)(0ULL));
uint32_t dn_tmp_145=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_146=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_139,dn_tmp_140,dn_tmp_141,dn_tmp_142,dn_tmp_143,dn_tmp_144,dn_tmp_145,dn_tmp_146}); });dn_r_MessageField *dn_tmp_147=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_135=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_137=&((v_spec).f_messages);size_t dn_tmp_138=((size_t)(0ULL));&dn_tmp_137->data[dn_index(dn_tmp_138,16)]; }))).f_fields);size_t dn_tmp_136=((size_t)(5ULL));&dn_tmp_135->data[dn_index(dn_tmp_136,32)]; })));*dn_tmp_147=dn_tmp_148; }
{ dn_r_MessageField dn_tmp_162=({ uint32_t dn_tmp_153=((uint32_t)(7ULL));
uint32_t dn_tmp_154=((uint32_t)(7ULL));
uint32_t dn_tmp_155=((uint32_t)(0ULL));
uint32_t dn_tmp_156=((uint32_t)(1ULL));
uint32_t dn_tmp_157=((uint32_t)(64ULL));
uint32_t dn_tmp_158=((uint32_t)(0ULL));
uint32_t dn_tmp_159=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_160=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_153,dn_tmp_154,dn_tmp_155,dn_tmp_156,dn_tmp_157,dn_tmp_158,dn_tmp_159,dn_tmp_160}); });dn_r_MessageField *dn_tmp_161=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_149=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_151=&((v_spec).f_messages);size_t dn_tmp_152=((size_t)(0ULL));&dn_tmp_151->data[dn_index(dn_tmp_152,16)]; }))).f_fields);size_t dn_tmp_150=((size_t)(6ULL));&dn_tmp_149->data[dn_index(dn_tmp_150,32)]; })));*dn_tmp_161=dn_tmp_162; }
{ dn_r_MessageField dn_tmp_176=({ uint32_t dn_tmp_167=((uint32_t)(8ULL));
uint32_t dn_tmp_168=((uint32_t)(8ULL));
uint32_t dn_tmp_169=((uint32_t)(0ULL));
uint32_t dn_tmp_170=((uint32_t)(4ULL));
uint32_t dn_tmp_171=((uint32_t)(0ULL));
uint32_t dn_tmp_172=((uint32_t)(0ULL));
uint32_t dn_tmp_173=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_174=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_167,dn_tmp_168,dn_tmp_169,dn_tmp_170,dn_tmp_171,dn_tmp_172,dn_tmp_173,dn_tmp_174}); });dn_r_MessageField *dn_tmp_175=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_163=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_165=&((v_spec).f_messages);size_t dn_tmp_166=((size_t)(0ULL));&dn_tmp_165->data[dn_index(dn_tmp_166,16)]; }))).f_fields);size_t dn_tmp_164=((size_t)(7ULL));&dn_tmp_163->data[dn_index(dn_tmp_164,32)]; })));*dn_tmp_175=dn_tmp_176; }
{ dn_r_MessageField dn_tmp_190=({ uint32_t dn_tmp_181=((uint32_t)(9ULL));
uint32_t dn_tmp_182=((uint32_t)(9ULL));
uint32_t dn_tmp_183=((uint32_t)(1ULL));
uint32_t dn_tmp_184=((uint32_t)(2ULL));
uint32_t dn_tmp_185=((uint32_t)(0ULL));
uint32_t dn_tmp_186=((uint32_t)(0ULL));
uint32_t dn_tmp_187=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_188=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_181,dn_tmp_182,dn_tmp_183,dn_tmp_184,dn_tmp_185,dn_tmp_186,dn_tmp_187,dn_tmp_188}); });dn_r_MessageField *dn_tmp_189=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_177=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_179=&((v_spec).f_messages);size_t dn_tmp_180=((size_t)(0ULL));&dn_tmp_179->data[dn_index(dn_tmp_180,16)]; }))).f_fields);size_t dn_tmp_178=((size_t)(8ULL));&dn_tmp_177->data[dn_index(dn_tmp_178,32)]; })));*dn_tmp_189=dn_tmp_190; }
{ dn_r_MessageField dn_tmp_204=({ uint32_t dn_tmp_195=((uint32_t)(10ULL));
uint32_t dn_tmp_196=((uint32_t)(2ULL));
uint32_t dn_tmp_197=((uint32_t)(0ULL));
uint32_t dn_tmp_198=((uint32_t)(1ULL));
uint32_t dn_tmp_199=((uint32_t)(0ULL));
uint32_t dn_tmp_200=((uint32_t)(1ULL));
uint32_t dn_tmp_201=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_202=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_195,dn_tmp_196,dn_tmp_197,dn_tmp_198,dn_tmp_199,dn_tmp_200,dn_tmp_201,dn_tmp_202}); });dn_r_MessageField *dn_tmp_203=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_191=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_193=&((v_spec).f_messages);size_t dn_tmp_194=((size_t)(0ULL));&dn_tmp_193->data[dn_index(dn_tmp_194,16)]; }))).f_fields);size_t dn_tmp_192=((size_t)(9ULL));&dn_tmp_191->data[dn_index(dn_tmp_192,32)]; })));*dn_tmp_203=dn_tmp_204; }
{ dn_r_MessageField dn_tmp_218=({ uint32_t dn_tmp_209=((uint32_t)(11ULL));
uint32_t dn_tmp_210=((uint32_t)(6ULL));
uint32_t dn_tmp_211=((uint32_t)(0ULL));
uint32_t dn_tmp_212=((uint32_t)(1ULL));
uint32_t dn_tmp_213=((uint32_t)(8ULL));
uint32_t dn_tmp_214=((uint32_t)(0ULL));
uint32_t dn_tmp_215=((uint32_t)(0ULL));
uint32_t dn_tmp_216=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_209,dn_tmp_210,dn_tmp_211,dn_tmp_212,dn_tmp_213,dn_tmp_214,dn_tmp_215,dn_tmp_216}); });dn_r_MessageField *dn_tmp_217=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_205=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_207=&((v_spec).f_messages);size_t dn_tmp_208=((size_t)(0ULL));&dn_tmp_207->data[dn_index(dn_tmp_208,16)]; }))).f_fields);size_t dn_tmp_206=((size_t)(10ULL));&dn_tmp_205->data[dn_index(dn_tmp_206,32)]; })));*dn_tmp_217=dn_tmp_218; }
{ dn_r_MessageSchema dn_tmp_225=({ uint32_t dn_tmp_221=((uint32_t)(2ULL));
uint32_t dn_tmp_222=((uint32_t)(3ULL));
uint32_t dn_tmp_223=((uint32_t)(3ULL));
dn_f_message_schema(dn_tmp_221,dn_tmp_222,dn_tmp_223); });dn_r_MessageSchema *dn_tmp_224=&((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_219=&((v_spec).f_messages);size_t dn_tmp_220=((size_t)(1ULL));&dn_tmp_219->data[dn_index(dn_tmp_220,16)]; })));*dn_tmp_224=dn_tmp_225; }
{ uint32_t dn_tmp_229=((uint32_t)(0ULL));uint32_t *dn_tmp_228=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_226=&((v_spec).f_messages);size_t dn_tmp_227=((size_t)(1ULL));&dn_tmp_226->data[dn_index(dn_tmp_227,16)]; }))).f_first_minor);*dn_tmp_228=dn_tmp_229; }
{ uint32_t dn_tmp_233=((uint32_t)(4294967295ULL));uint32_t *dn_tmp_232=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_230=&((v_spec).f_messages);size_t dn_tmp_231=((size_t)(1ULL));&dn_tmp_230->data[dn_index(dn_tmp_231,16)]; }))).f_last_minor);*dn_tmp_232=dn_tmp_233; }
{ uint32_t dn_tmp_237=((uint32_t)(0ULL));uint32_t *dn_tmp_236=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_234=&((v_spec).f_messages);size_t dn_tmp_235=((size_t)(1ULL));&dn_tmp_234->data[dn_index(dn_tmp_235,16)]; }))).f_field_count);*dn_tmp_236=dn_tmp_237; }
{ dn_r_NetworkSpec dn_tmp_238=v_spec;return dn_tmp_238; }
}

dn_fail("missing native return");
}
static dn_r_NetworkSpec dn_cb_Pos_spec(void) {
 if(!dn_enter()){return (dn_r_NetworkSpec){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkSpec){0};}
 dn_r_NetworkSpec dn_result=dn_f_Pos_spec();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecPlan dn_f_Pos_plan_1(void) {
{
dn_r_CodecPlan v_plan=({ ((dn_r_CodecPlan){0}); });
{ uint32_t dn_tmp_1=((uint32_t)(1ULL));uint32_t *dn_tmp_0=&((v_plan).f_message);*dn_tmp_0=dn_tmp_1; }
{ dn_r_WireFormat dn_tmp_7=({ uint32_t dn_tmp_2=((uint32_t)(1ULL));
uint32_t dn_tmp_3=((uint32_t)(1ULL));
uint32_t dn_tmp_4=((uint32_t)(0ULL));
uint32_t dn_tmp_5=((uint32_t)(0ULL));
((dn_r_WireFormat){dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5}); });dn_r_WireFormat *dn_tmp_6=&((v_plan).f_wire);*dn_tmp_6=dn_tmp_7; }
{ uint32_t dn_tmp_9=((uint32_t)(0ULL));uint32_t *dn_tmp_8=&((v_plan).f_minor);*dn_tmp_8=dn_tmp_9; }
{ uint32_t dn_tmp_11=((uint32_t)(10ULL));uint32_t *dn_tmp_10=&((v_plan).f_field_count);*dn_tmp_10=dn_tmp_11; }
{ dn_r_MessageField dn_tmp_23=({ uint32_t dn_tmp_14=((uint32_t)(1ULL));
uint32_t dn_tmp_15=((uint32_t)(1ULL));
uint32_t dn_tmp_16=((uint32_t)(1ULL));
uint32_t dn_tmp_17=((uint32_t)(1ULL));
uint32_t dn_tmp_18=((uint32_t)(0ULL));
uint32_t dn_tmp_19=((uint32_t)(0ULL));
uint32_t dn_tmp_20=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_21=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_14,dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_18,dn_tmp_19,dn_tmp_20,dn_tmp_21}); });dn_r_MessageField *dn_tmp_22=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_12=&((v_plan).f_fields);size_t dn_tmp_13=((size_t)(0ULL));&dn_tmp_12->data[dn_index(dn_tmp_13,32)]; })));*dn_tmp_22=dn_tmp_23; }
{ dn_r_MessageField dn_tmp_35=({ uint32_t dn_tmp_26=((uint32_t)(2ULL));
uint32_t dn_tmp_27=((uint32_t)(2ULL));
uint32_t dn_tmp_28=((uint32_t)(0ULL));
uint32_t dn_tmp_29=((uint32_t)(1ULL));
uint32_t dn_tmp_30=((uint32_t)(0ULL));
uint32_t dn_tmp_31=((uint32_t)(0ULL));
uint32_t dn_tmp_32=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_33=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_26,dn_tmp_27,dn_tmp_28,dn_tmp_29,dn_tmp_30,dn_tmp_31,dn_tmp_32,dn_tmp_33}); });dn_r_MessageField *dn_tmp_34=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_24=&((v_plan).f_fields);size_t dn_tmp_25=((size_t)(1ULL));&dn_tmp_24->data[dn_index(dn_tmp_25,32)]; })));*dn_tmp_34=dn_tmp_35; }
{ dn_r_MessageField dn_tmp_47=({ uint32_t dn_tmp_38=((uint32_t)(3ULL));
uint32_t dn_tmp_39=((uint32_t)(3ULL));
uint32_t dn_tmp_40=((uint32_t)(0ULL));
uint32_t dn_tmp_41=((uint32_t)(3ULL));
uint32_t dn_tmp_42=((uint32_t)(0ULL));
uint32_t dn_tmp_43=((uint32_t)(0ULL));
uint32_t dn_tmp_44=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_45=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_38,dn_tmp_39,dn_tmp_40,dn_tmp_41,dn_tmp_42,dn_tmp_43,dn_tmp_44,dn_tmp_45}); });dn_r_MessageField *dn_tmp_46=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_36=&((v_plan).f_fields);size_t dn_tmp_37=((size_t)(2ULL));&dn_tmp_36->data[dn_index(dn_tmp_37,32)]; })));*dn_tmp_46=dn_tmp_47; }
{ dn_r_MessageField dn_tmp_59=({ uint32_t dn_tmp_50=((uint32_t)(4ULL));
uint32_t dn_tmp_51=((uint32_t)(4ULL));
uint32_t dn_tmp_52=((uint32_t)(1ULL));
uint32_t dn_tmp_53=((uint32_t)(1ULL));
uint32_t dn_tmp_54=((uint32_t)(0ULL));
uint32_t dn_tmp_55=((uint32_t)(0ULL));
uint32_t dn_tmp_56=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_57=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_50,dn_tmp_51,dn_tmp_52,dn_tmp_53,dn_tmp_54,dn_tmp_55,dn_tmp_56,dn_tmp_57}); });dn_r_MessageField *dn_tmp_58=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_48=&((v_plan).f_fields);size_t dn_tmp_49=((size_t)(3ULL));&dn_tmp_48->data[dn_index(dn_tmp_49,32)]; })));*dn_tmp_58=dn_tmp_59; }
{ dn_r_MessageField dn_tmp_71=({ uint32_t dn_tmp_62=((uint32_t)(5ULL));
uint32_t dn_tmp_63=((uint32_t)(5ULL));
uint32_t dn_tmp_64=((uint32_t)(1ULL));
uint32_t dn_tmp_65=((uint32_t)(1ULL));
uint32_t dn_tmp_66=((uint32_t)(0ULL));
uint32_t dn_tmp_67=((uint32_t)(0ULL));
uint32_t dn_tmp_68=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_69=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_62,dn_tmp_63,dn_tmp_64,dn_tmp_65,dn_tmp_66,dn_tmp_67,dn_tmp_68,dn_tmp_69}); });dn_r_MessageField *dn_tmp_70=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_60=&((v_plan).f_fields);size_t dn_tmp_61=((size_t)(4ULL));&dn_tmp_60->data[dn_index(dn_tmp_61,32)]; })));*dn_tmp_70=dn_tmp_71; }
{ dn_r_MessageField dn_tmp_83=({ uint32_t dn_tmp_74=((uint32_t)(6ULL));
uint32_t dn_tmp_75=((uint32_t)(6ULL));
uint32_t dn_tmp_76=((uint32_t)(0ULL));
uint32_t dn_tmp_77=((uint32_t)(2ULL));
uint32_t dn_tmp_78=((uint32_t)(32ULL));
uint32_t dn_tmp_79=((uint32_t)(0ULL));
uint32_t dn_tmp_80=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_81=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_74,dn_tmp_75,dn_tmp_76,dn_tmp_77,dn_tmp_78,dn_tmp_79,dn_tmp_80,dn_tmp_81}); });dn_r_MessageField *dn_tmp_82=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_72=&((v_plan).f_fields);size_t dn_tmp_73=((size_t)(5ULL));&dn_tmp_72->data[dn_index(dn_tmp_73,32)]; })));*dn_tmp_82=dn_tmp_83; }
{ dn_r_MessageField dn_tmp_95=({ uint32_t dn_tmp_86=((uint32_t)(7ULL));
uint32_t dn_tmp_87=((uint32_t)(7ULL));
uint32_t dn_tmp_88=((uint32_t)(0ULL));
uint32_t dn_tmp_89=((uint32_t)(1ULL));
uint32_t dn_tmp_90=((uint32_t)(64ULL));
uint32_t dn_tmp_91=((uint32_t)(0ULL));
uint32_t dn_tmp_92=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_93=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_86,dn_tmp_87,dn_tmp_88,dn_tmp_89,dn_tmp_90,dn_tmp_91,dn_tmp_92,dn_tmp_93}); });dn_r_MessageField *dn_tmp_94=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_84=&((v_plan).f_fields);size_t dn_tmp_85=((size_t)(6ULL));&dn_tmp_84->data[dn_index(dn_tmp_85,32)]; })));*dn_tmp_94=dn_tmp_95; }
{ dn_r_MessageField dn_tmp_107=({ uint32_t dn_tmp_98=((uint32_t)(8ULL));
uint32_t dn_tmp_99=((uint32_t)(8ULL));
uint32_t dn_tmp_100=((uint32_t)(0ULL));
uint32_t dn_tmp_101=((uint32_t)(4ULL));
uint32_t dn_tmp_102=((uint32_t)(0ULL));
uint32_t dn_tmp_103=((uint32_t)(0ULL));
uint32_t dn_tmp_104=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_105=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_98,dn_tmp_99,dn_tmp_100,dn_tmp_101,dn_tmp_102,dn_tmp_103,dn_tmp_104,dn_tmp_105}); });dn_r_MessageField *dn_tmp_106=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_96=&((v_plan).f_fields);size_t dn_tmp_97=((size_t)(7ULL));&dn_tmp_96->data[dn_index(dn_tmp_97,32)]; })));*dn_tmp_106=dn_tmp_107; }
{ dn_r_MessageField dn_tmp_119=({ uint32_t dn_tmp_110=((uint32_t)(9ULL));
uint32_t dn_tmp_111=((uint32_t)(9ULL));
uint32_t dn_tmp_112=((uint32_t)(1ULL));
uint32_t dn_tmp_113=((uint32_t)(2ULL));
uint32_t dn_tmp_114=((uint32_t)(0ULL));
uint32_t dn_tmp_115=((uint32_t)(0ULL));
uint32_t dn_tmp_116=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_117=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_110,dn_tmp_111,dn_tmp_112,dn_tmp_113,dn_tmp_114,dn_tmp_115,dn_tmp_116,dn_tmp_117}); });dn_r_MessageField *dn_tmp_118=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_108=&((v_plan).f_fields);size_t dn_tmp_109=((size_t)(8ULL));&dn_tmp_108->data[dn_index(dn_tmp_109,32)]; })));*dn_tmp_118=dn_tmp_119; }
{ dn_r_MessageField dn_tmp_131=({ uint32_t dn_tmp_122=((uint32_t)(11ULL));
uint32_t dn_tmp_123=((uint32_t)(6ULL));
uint32_t dn_tmp_124=((uint32_t)(0ULL));
uint32_t dn_tmp_125=((uint32_t)(1ULL));
uint32_t dn_tmp_126=((uint32_t)(8ULL));
uint32_t dn_tmp_127=((uint32_t)(0ULL));
uint32_t dn_tmp_128=((uint32_t)(0ULL));
uint32_t dn_tmp_129=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_122,dn_tmp_123,dn_tmp_124,dn_tmp_125,dn_tmp_126,dn_tmp_127,dn_tmp_128,dn_tmp_129}); });dn_r_MessageField *dn_tmp_130=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_120=&((v_plan).f_fields);size_t dn_tmp_121=((size_t)(9ULL));&dn_tmp_120->data[dn_index(dn_tmp_121,32)]; })));*dn_tmp_130=dn_tmp_131; }
{ uint32_t dn_tmp_133=((uint32_t)(17ULL));uint32_t *dn_tmp_132=&((v_plan).f_maximum_values);*dn_tmp_132=dn_tmp_133; }
{ size_t dn_tmp_135=((size_t)(268ULL));size_t *dn_tmp_134=&((v_plan).f_maximum_wire);*dn_tmp_134=dn_tmp_135; }
{ uint32_t dn_tmp_137=((uint32_t)(0ULL));uint32_t *dn_tmp_136=&((v_plan).f_maximum_unknown_fields);*dn_tmp_136=dn_tmp_137; }
{ size_t dn_tmp_139=((size_t)(0ULL));size_t *dn_tmp_138=&((v_plan).f_maximum_unknown_bytes);*dn_tmp_138=dn_tmp_139; }
{ size_t dn_tmp_141=((size_t)(8388608ULL));size_t *dn_tmp_140=&((v_plan).f_storage_budget);*dn_tmp_140=dn_tmp_141; }
{ dn_r_CodecPlan dn_tmp_142=v_plan;return dn_tmp_142; }
}

dn_fail("missing native return");
}
static dn_r_CodecPlan dn_cb_Pos_plan_1(void) {
 if(!dn_enter()){return (dn_r_CodecPlan){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecPlan){0};}
 dn_r_CodecPlan dn_result=dn_f_Pos_plan_1();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_PosMessage1 dn_f_PosMessage1_new(void) {
{
{ dn_r_PosMessage1 dn_tmp_2=({ dn_r_CodecMessage dn_tmp_0=({ uint32_t dn_tmp_1=((uint32_t)(1ULL));
dn_f_codec_message(dn_tmp_1); });
dn_adopt_r12_CodecMessage(&dn_tmp_0);
((dn_r_PosMessage1){dn_tmp_0}); });dn_adopt_r11_PosMessage1(&dn_tmp_2);return dn_tmp_2; }
}

dn_fail("missing native return");
}
static dn_r_PosMessage1 dn_cb_PosMessage1_new(void) {
 if(!dn_enter()){return (dn_r_PosMessage1){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_PosMessage1){0};}
 dn_r_PosMessage1 dn_result=dn_f_PosMessage1_new();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_PosMessage1Decoded dn_f_PosMessage1_decode(dn_t_slice0_U8 v_input) {
{
dn_r_CodecDecoded volatile v_decoded={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r12_CodecDecoded(&v_decoded);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
v_decoded=({ dn_r_CodecPlan const * dn_tmp_2=(&(v_plan));
dn_t_slice0_U8 dn_tmp_3=v_input;
dn_f_codec_decode(dn_tmp_2,dn_tmp_3); });
{ dn_r_PosMessage1Decoded dn_tmp_8=({ dn_r_PosMessage1 dn_tmp_4=({ dn_r_CodecMessage dn_tmp_5=({ dn_r_CodecMessage dn_tmp_6=(v_decoded).f_value;(v_decoded).f_value=(dn_r_CodecMessage){0};dn_adopt_r12_CodecMessage(&dn_tmp_6);dn_tmp_6; });
dn_adopt_r12_CodecMessage(&dn_tmp_5);
((dn_r_PosMessage1){dn_tmp_5}); });
dn_adopt_r11_PosMessage1(&dn_tmp_4);
dn_r_CodecError dn_tmp_7=(v_decoded).f_error;
((dn_r_PosMessage1Decoded){dn_tmp_4,dn_tmp_7}); });dn_adopt_r18_PosMessage1Decoded(&dn_tmp_8);dn_drop_r12_CodecDecoded(&v_decoded);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_8; }
dn_drop_r12_CodecDecoded(&v_decoded);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_r_PosMessage1Decoded dn_cb_PosMessage1_decode(dn_t_slice0_U8 v_input) {
 if(!dn_enter()){return (dn_r_PosMessage1Decoded){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_PosMessage1Decoded){0};}
 dn_r_PosMessage1Decoded dn_result=dn_f_PosMessage1_decode(v_input);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecIo dn_f_PosMessage1_measure(dn_r_PosMessage1 const * v_message) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
{ dn_r_CodecIo dn_tmp_2=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
dn_f_codec_measure(dn_tmp_0,dn_tmp_1); });return dn_tmp_2; }
}

dn_fail("missing native return");
}
static dn_r_CodecIo dn_cb_PosMessage1_measure(dn_r_PosMessage1 const * v_message) {
 if(!dn_enter()){return (dn_r_CodecIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecIo){0};}
 dn_r_CodecIo dn_result=dn_f_PosMessage1_measure(v_message);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecIo dn_f_PosMessage1_encode(dn_r_PosMessage1 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
{ dn_r_CodecIo dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
dn_t_buf_U8 * dn_tmp_2=v_output;
size_t dn_tmp_3=v_offset;
dn_f_codec_encode(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecIo dn_cb_PosMessage1_encode(dn_r_PosMessage1 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset) {
 if(!dn_enter()){return (dn_r_CodecIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecIo){0};}
 dn_r_CodecIo dn_result=dn_f_PosMessage1_encode(v_message,v_output,v_offset);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_PosMessage1_equal(dn_r_PosMessage1 const * v_left,dn_r_PosMessage1 const * v_right) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
{ bool dn_tmp_3=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_left)->f_inner));
dn_r_CodecMessage const * dn_tmp_2=(&((v_right)->f_inner));
dn_f_codec_equal(dn_tmp_0,dn_tmp_1,dn_tmp_2); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static bool dn_cb_PosMessage1_equal(dn_r_PosMessage1 const * v_left,dn_r_PosMessage1 const * v_right) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_PosMessage1_equal(v_left,v_right);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_PosMessage1_field_1_add(dn_r_PosMessage1 * v_message,uint8_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(1ULL));
uint64_t dn_tmp_3=({ uint8_t dn_tmp_4=v_value;
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_4; });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_PosMessage1_field_1_add(dn_r_PosMessage1 * v_message,uint8_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_PosMessage1_field_1_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_PosMessage1Field1 dn_f_PosMessage1_field_1_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(1ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_PosMessage1Field1 dn_tmp_7=({ uint8_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_5; });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_PosMessage1Field1){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_PosMessage1Field1 dn_cb_PosMessage1_field_1_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_PosMessage1Field1){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_PosMessage1Field1){0};}
 dn_r_PosMessage1Field1 dn_result=dn_f_PosMessage1_field_1_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_PosMessage1_field_1_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U8 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(1ULL));
size_t dn_tmp_3=({ dn_t_slice0_U8 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_U8 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(1ULL));
uint32_t dn_tmp_16=((uint32_t)(1ULL));
uint64_t dn_tmp_17=({ uint8_t dn_tmp_18=(*({ dn_t_slice0_U8 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_18; });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_PosMessage1_field_1_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U8 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_PosMessage1_field_1_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_PosMessage1_field_2_add(dn_r_PosMessage1 * v_message,uint16_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(2ULL));
uint64_t dn_tmp_3=({ uint16_t dn_tmp_4=v_value;
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_4; });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_PosMessage1_field_2_add(dn_r_PosMessage1 * v_message,uint16_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_PosMessage1_field_2_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_PosMessage1Field2 dn_f_PosMessage1_field_2_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(2ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_PosMessage1Field2 dn_tmp_7=({ uint16_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<16)-1))dn_fail("numeric conversion out of range");(uint16_t)dn_tmp_5; });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_PosMessage1Field2){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_PosMessage1Field2 dn_cb_PosMessage1_field_2_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_PosMessage1Field2){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_PosMessage1Field2){0};}
 dn_r_PosMessage1Field2 dn_result=dn_f_PosMessage1_field_2_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_PosMessage1_field_2_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U16 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(2ULL));
size_t dn_tmp_3=({ dn_t_slice0_U16 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_U16 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(2ULL));
uint32_t dn_tmp_16=((uint32_t)(2ULL));
uint64_t dn_tmp_17=({ uint16_t dn_tmp_18=(*({ dn_t_slice0_U16 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_18; });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_PosMessage1_field_2_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U16 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_PosMessage1_field_2_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_PosMessage1_field_3_add(dn_r_PosMessage1 * v_message,uint32_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(3ULL));
uint64_t dn_tmp_3=({ uint32_t dn_tmp_4=v_value;
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_4; });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_PosMessage1_field_3_add(dn_r_PosMessage1 * v_message,uint32_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_PosMessage1_field_3_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_PosMessage1Field3 dn_f_PosMessage1_field_3_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(3ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_PosMessage1Field3 dn_tmp_7=({ uint32_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_5; });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_PosMessage1Field3){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_PosMessage1Field3 dn_cb_PosMessage1_field_3_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_PosMessage1Field3){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_PosMessage1Field3){0};}
 dn_r_PosMessage1Field3 dn_result=dn_f_PosMessage1_field_3_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_PosMessage1_field_3_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U32 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(3ULL));
size_t dn_tmp_3=({ dn_t_slice0_U32 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_U32 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(3ULL));
uint32_t dn_tmp_16=((uint32_t)(3ULL));
uint64_t dn_tmp_17=({ uint32_t dn_tmp_18=(*({ dn_t_slice0_U32 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_18; });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_PosMessage1_field_3_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U32 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_PosMessage1_field_3_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_PosMessage1_field_4_add(dn_r_PosMessage1 * v_message,uint64_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(4ULL));
uint64_t dn_tmp_3=({ uint64_t dn_tmp_4=v_value;
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_4; });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_PosMessage1_field_4_add(dn_r_PosMessage1 * v_message,uint64_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_PosMessage1_field_4_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_PosMessage1Field4 dn_f_PosMessage1_field_4_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(4ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_PosMessage1Field4 dn_tmp_7=({ uint64_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_5; });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_PosMessage1Field4){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_PosMessage1Field4 dn_cb_PosMessage1_field_4_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_PosMessage1Field4){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_PosMessage1Field4){0};}
 dn_r_PosMessage1Field4 dn_result=dn_f_PosMessage1_field_4_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_PosMessage1_field_4_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U64 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(4ULL));
size_t dn_tmp_3=({ dn_t_slice0_U64 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_U64 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(4ULL));
uint32_t dn_tmp_16=((uint32_t)(4ULL));
uint64_t dn_tmp_17=({ uint64_t dn_tmp_18=(*({ dn_t_slice0_U64 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_18; });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_PosMessage1_field_4_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U64 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_PosMessage1_field_4_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_PosMessage1_field_5_add(dn_r_PosMessage1 * v_message,bool v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
uint64_t v_scalar=((uint64_t)(0ULL));
if(v_value){
{ uint64_t dn_tmp_1=((uint64_t)(1ULL));uint64_t *dn_tmp_0=&(v_scalar);*dn_tmp_0=dn_tmp_1; }
}
else{
}
{ dn_r_CodecError dn_tmp_6=({ dn_r_CodecPlan const * dn_tmp_2=(&(v_plan));
dn_r_CodecMessage * dn_tmp_3=(&((v_message)->f_inner));
uint32_t dn_tmp_4=((uint32_t)(5ULL));
uint64_t dn_tmp_5=v_scalar;
dn_f_codec_add_scalar(dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5); });return dn_tmp_6; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_PosMessage1_field_5_add(dn_r_PosMessage1 * v_message,bool v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_PosMessage1_field_5_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_PosMessage1Field5 dn_f_PosMessage1_field_5_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(5ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_PosMessage1Field5 dn_tmp_8=({ bool dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
uint64_t dn_tmp_6=((uint64_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); });
dn_r_CodecError dn_tmp_7=(v_value).f_error;
((dn_r_PosMessage1Field5){dn_tmp_4,dn_tmp_7}); });return dn_tmp_8; }
}

dn_fail("missing native return");
}
static dn_r_PosMessage1Field5 dn_cb_PosMessage1_field_5_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_PosMessage1Field5){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_PosMessage1Field5){0};}
 dn_r_PosMessage1Field5 dn_result=dn_f_PosMessage1_field_5_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_PosMessage1_field_5_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_b v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(5ULL));
size_t dn_tmp_3=({ dn_t_slice0_b dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_b dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
uint64_t v_scalar=((uint64_t)(0ULL));
if((*({ dn_t_slice0_b *dn_tmp_11=&(v_values);size_t dn_tmp_12=v_index;&dn_tmp_11->data[dn_tmp_12]; }))){
{ uint64_t dn_tmp_14=((uint64_t)(1ULL));uint64_t *dn_tmp_13=&(v_scalar);*dn_tmp_13=dn_tmp_14; }
}
else{
}
{ dn_r_CodecValue dn_tmp_25=({ uint32_t dn_tmp_19=((uint32_t)(5ULL));
uint32_t dn_tmp_20=((uint32_t)(5ULL));
uint64_t dn_tmp_21=v_scalar;
size_t dn_tmp_22=((size_t)(0ULL));
size_t dn_tmp_23=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_19,dn_tmp_20,dn_tmp_21,dn_tmp_22,dn_tmp_23}); });dn_r_CodecValue *dn_tmp_24=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_15=&(((v_message)->f_inner).f_values);size_t dn_tmp_16=({ size_t dn_tmp_17=(v_reserved).f_start;
size_t dn_tmp_18=v_index;
dn_USize_add(dn_tmp_17,dn_tmp_18); });&dn_tmp_15->data[dn_index(dn_tmp_16,dn_tmp_15->len)]; })));*dn_tmp_24=dn_tmp_25; }
}
 }
{ dn_r_CodecError dn_tmp_29=({ int32_t dn_tmp_26=((int32_t)(0ULL));
uint32_t dn_tmp_27=((uint32_t)(0ULL));
size_t dn_tmp_28=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_26,dn_tmp_27,dn_tmp_28}); });return dn_tmp_29; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_PosMessage1_field_5_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_b v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_PosMessage1_field_5_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_PosMessage1_field_6_add(dn_r_PosMessage1 * v_message,dn_t_slice0_U8 v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
{ dn_r_CodecError dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(6ULL));
dn_t_slice0_U8 dn_tmp_3=v_value;
dn_f_codec_add_bytes(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_PosMessage1_field_6_add(dn_r_PosMessage1 * v_message,dn_t_slice0_U8 v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_PosMessage1_field_6_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecSpan dn_f_PosMessage1_field_6_span(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
{ dn_r_CodecSpan dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(6ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_span(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecSpan dn_cb_PosMessage1_field_6_span(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_CodecSpan){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecSpan){0};}
 dn_r_CodecSpan dn_result=dn_f_PosMessage1_field_6_span(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_slice0_U8 dn_f_PosMessage1_field_6_view(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecSpan v_span=({ dn_r_PosMessage1 const * dn_tmp_0=v_message;
size_t dn_tmp_1=v_occurrence;
dn_f_PosMessage1_field_6_span(dn_tmp_0,dn_tmp_1); });
{ dn_t_slice0_U8 dn_tmp_4=({ dn_r_CodecMessage const * dn_tmp_2=(&((v_message)->f_inner));
dn_r_CodecSpan dn_tmp_3=v_span;
dn_f_codec_view(dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_t_slice0_U8 dn_cb_PosMessage1_field_6_view(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_t_slice0_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_slice0_U8){0};}
 dn_t_slice0_U8 dn_result=dn_f_PosMessage1_field_6_view(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_PosMessage1_field_7_add(dn_r_PosMessage1 * v_message,dn_t_slice0_U8 v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
{ dn_r_CodecError dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(7ULL));
dn_t_slice0_U8 dn_tmp_3=v_value;
dn_f_codec_add_bytes(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_PosMessage1_field_7_add(dn_r_PosMessage1 * v_message,dn_t_slice0_U8 v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_PosMessage1_field_7_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecSpan dn_f_PosMessage1_field_7_span(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
{ dn_r_CodecSpan dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(7ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_span(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecSpan dn_cb_PosMessage1_field_7_span(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_CodecSpan){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecSpan){0};}
 dn_r_CodecSpan dn_result=dn_f_PosMessage1_field_7_span(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_slice0_U8 dn_f_PosMessage1_field_7_view(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecSpan v_span=({ dn_r_PosMessage1 const * dn_tmp_0=v_message;
size_t dn_tmp_1=v_occurrence;
dn_f_PosMessage1_field_7_span(dn_tmp_0,dn_tmp_1); });
{ dn_t_slice0_U8 dn_tmp_4=({ dn_r_CodecMessage const * dn_tmp_2=(&((v_message)->f_inner));
dn_r_CodecSpan dn_tmp_3=v_span;
dn_f_codec_view(dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_t_slice0_U8 dn_cb_PosMessage1_field_7_view(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_t_slice0_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_slice0_U8){0};}
 dn_t_slice0_U8 dn_result=dn_f_PosMessage1_field_7_view(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_PosMessage1_field_8_add(dn_r_PosMessage1 * v_message,uint64_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(8ULL));
uint64_t dn_tmp_3=({ uint64_t dn_tmp_4=v_value;
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_4; });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_PosMessage1_field_8_add(dn_r_PosMessage1 * v_message,uint64_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_PosMessage1_field_8_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_PosMessage1Field8 dn_f_PosMessage1_field_8_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(8ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_PosMessage1Field8 dn_tmp_7=({ uint64_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_5; });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_PosMessage1Field8){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_PosMessage1Field8 dn_cb_PosMessage1_field_8_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_PosMessage1Field8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_PosMessage1Field8){0};}
 dn_r_PosMessage1Field8 dn_result=dn_f_PosMessage1_field_8_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_PosMessage1_field_8_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U64 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(8ULL));
size_t dn_tmp_3=({ dn_t_slice0_U64 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_U64 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(8ULL));
uint32_t dn_tmp_16=((uint32_t)(8ULL));
uint64_t dn_tmp_17=({ uint64_t dn_tmp_18=(*({ dn_t_slice0_U64 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_18; });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_PosMessage1_field_8_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_U64 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_PosMessage1_field_8_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_PosMessage1_field_9_add(dn_r_PosMessage1 * v_message,int64_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(9ULL));
uint64_t dn_tmp_3=({ int64_t dn_tmp_4=v_value;
dn_f_zigzag_encode(dn_tmp_4); });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_PosMessage1_field_9_add(dn_r_PosMessage1 * v_message,int64_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_PosMessage1_field_9_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_PosMessage1Field9 dn_f_PosMessage1_field_9_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(9ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_PosMessage1Field9 dn_tmp_7=({ int64_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
dn_f_zigzag_decode(dn_tmp_5); });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_PosMessage1Field9){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_PosMessage1Field9 dn_cb_PosMessage1_field_9_get(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_PosMessage1Field9){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_PosMessage1Field9){0};}
 dn_r_PosMessage1Field9 dn_result=dn_f_PosMessage1_field_9_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_PosMessage1_field_9_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_I64 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(9ULL));
size_t dn_tmp_3=({ dn_t_slice0_I64 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_I64 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(9ULL));
uint32_t dn_tmp_16=((uint32_t)(9ULL));
uint64_t dn_tmp_17=({ int64_t dn_tmp_18=(*({ dn_t_slice0_I64 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
dn_f_zigzag_encode(dn_tmp_18); });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_PosMessage1_field_9_add_many(dn_r_PosMessage1 * v_message,dn_t_slice0_I64 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_PosMessage1_field_9_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_PosMessage1_field_11_add(dn_r_PosMessage1 * v_message,dn_t_slice0_U8 v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
{ dn_r_CodecError dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(11ULL));
dn_t_slice0_U8 dn_tmp_3=v_value;
dn_f_codec_add_bytes(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_PosMessage1_field_11_add(dn_r_PosMessage1 * v_message,dn_t_slice0_U8 v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_PosMessage1_field_11_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecSpan dn_f_PosMessage1_field_11_span(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
{ dn_r_CodecSpan dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(11ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_span(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecSpan dn_cb_PosMessage1_field_11_span(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_CodecSpan){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecSpan){0};}
 dn_r_CodecSpan dn_result=dn_f_PosMessage1_field_11_span(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_slice0_U8 dn_f_PosMessage1_field_11_view(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecSpan v_span=({ dn_r_PosMessage1 const * dn_tmp_0=v_message;
size_t dn_tmp_1=v_occurrence;
dn_f_PosMessage1_field_11_span(dn_tmp_0,dn_tmp_1); });
{ dn_t_slice0_U8 dn_tmp_4=({ dn_r_CodecMessage const * dn_tmp_2=(&((v_message)->f_inner));
dn_r_CodecSpan dn_tmp_3=v_span;
dn_f_codec_view(dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_t_slice0_U8 dn_cb_PosMessage1_field_11_view(dn_r_PosMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_t_slice0_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_slice0_U8){0};}
 dn_t_slice0_U8 dn_result=dn_f_PosMessage1_field_11_view(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecPlan dn_f_Pos_plan_2(void) {
{
dn_r_CodecPlan v_plan=({ ((dn_r_CodecPlan){0}); });
{ uint32_t dn_tmp_1=((uint32_t)(2ULL));uint32_t *dn_tmp_0=&((v_plan).f_message);*dn_tmp_0=dn_tmp_1; }
{ dn_r_WireFormat dn_tmp_7=({ uint32_t dn_tmp_2=((uint32_t)(1ULL));
uint32_t dn_tmp_3=((uint32_t)(1ULL));
uint32_t dn_tmp_4=((uint32_t)(0ULL));
uint32_t dn_tmp_5=((uint32_t)(0ULL));
((dn_r_WireFormat){dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5}); });dn_r_WireFormat *dn_tmp_6=&((v_plan).f_wire);*dn_tmp_6=dn_tmp_7; }
{ uint32_t dn_tmp_9=((uint32_t)(0ULL));uint32_t *dn_tmp_8=&((v_plan).f_minor);*dn_tmp_8=dn_tmp_9; }
{ uint32_t dn_tmp_11=((uint32_t)(0ULL));uint32_t *dn_tmp_10=&((v_plan).f_field_count);*dn_tmp_10=dn_tmp_11; }
{ uint32_t dn_tmp_13=((uint32_t)(0ULL));uint32_t *dn_tmp_12=&((v_plan).f_maximum_values);*dn_tmp_12=dn_tmp_13; }
{ size_t dn_tmp_15=((size_t)(4ULL));size_t *dn_tmp_14=&((v_plan).f_maximum_wire);*dn_tmp_14=dn_tmp_15; }
{ uint32_t dn_tmp_17=((uint32_t)(0ULL));uint32_t *dn_tmp_16=&((v_plan).f_maximum_unknown_fields);*dn_tmp_16=dn_tmp_17; }
{ size_t dn_tmp_19=((size_t)(0ULL));size_t *dn_tmp_18=&((v_plan).f_maximum_unknown_bytes);*dn_tmp_18=dn_tmp_19; }
{ size_t dn_tmp_21=((size_t)(8388608ULL));size_t *dn_tmp_20=&((v_plan).f_storage_budget);*dn_tmp_20=dn_tmp_21; }
{ dn_r_CodecPlan dn_tmp_22=v_plan;return dn_tmp_22; }
}

dn_fail("missing native return");
}
static dn_r_CodecPlan dn_cb_Pos_plan_2(void) {
 if(!dn_enter()){return (dn_r_CodecPlan){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecPlan){0};}
 dn_r_CodecPlan dn_result=dn_f_Pos_plan_2();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_PosMessage2 dn_f_PosMessage2_new(void) {
{
{ dn_r_PosMessage2 dn_tmp_2=({ dn_r_CodecMessage dn_tmp_0=({ uint32_t dn_tmp_1=((uint32_t)(2ULL));
dn_f_codec_message(dn_tmp_1); });
dn_adopt_r12_CodecMessage(&dn_tmp_0);
((dn_r_PosMessage2){dn_tmp_0}); });dn_adopt_r11_PosMessage2(&dn_tmp_2);return dn_tmp_2; }
}

dn_fail("missing native return");
}
static dn_r_PosMessage2 dn_cb_PosMessage2_new(void) {
 if(!dn_enter()){return (dn_r_PosMessage2){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_PosMessage2){0};}
 dn_r_PosMessage2 dn_result=dn_f_PosMessage2_new();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_PosMessage2Decoded dn_f_PosMessage2_decode(dn_t_slice0_U8 v_input) {
{
dn_r_CodecDecoded volatile v_decoded={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r12_CodecDecoded(&v_decoded);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_2(); });
v_decoded=({ dn_r_CodecPlan const * dn_tmp_2=(&(v_plan));
dn_t_slice0_U8 dn_tmp_3=v_input;
dn_f_codec_decode(dn_tmp_2,dn_tmp_3); });
{ dn_r_PosMessage2Decoded dn_tmp_8=({ dn_r_PosMessage2 dn_tmp_4=({ dn_r_CodecMessage dn_tmp_5=({ dn_r_CodecMessage dn_tmp_6=(v_decoded).f_value;(v_decoded).f_value=(dn_r_CodecMessage){0};dn_adopt_r12_CodecMessage(&dn_tmp_6);dn_tmp_6; });
dn_adopt_r12_CodecMessage(&dn_tmp_5);
((dn_r_PosMessage2){dn_tmp_5}); });
dn_adopt_r11_PosMessage2(&dn_tmp_4);
dn_r_CodecError dn_tmp_7=(v_decoded).f_error;
((dn_r_PosMessage2Decoded){dn_tmp_4,dn_tmp_7}); });dn_adopt_r18_PosMessage2Decoded(&dn_tmp_8);dn_drop_r12_CodecDecoded(&v_decoded);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_8; }
dn_drop_r12_CodecDecoded(&v_decoded);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_r_PosMessage2Decoded dn_cb_PosMessage2_decode(dn_t_slice0_U8 v_input) {
 if(!dn_enter()){return (dn_r_PosMessage2Decoded){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_PosMessage2Decoded){0};}
 dn_r_PosMessage2Decoded dn_result=dn_f_PosMessage2_decode(v_input);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecIo dn_f_PosMessage2_measure(dn_r_PosMessage2 const * v_message) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_2(); });
{ dn_r_CodecIo dn_tmp_2=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
dn_f_codec_measure(dn_tmp_0,dn_tmp_1); });return dn_tmp_2; }
}

dn_fail("missing native return");
}
static dn_r_CodecIo dn_cb_PosMessage2_measure(dn_r_PosMessage2 const * v_message) {
 if(!dn_enter()){return (dn_r_CodecIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecIo){0};}
 dn_r_CodecIo dn_result=dn_f_PosMessage2_measure(v_message);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecIo dn_f_PosMessage2_encode(dn_r_PosMessage2 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_2(); });
{ dn_r_CodecIo dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
dn_t_buf_U8 * dn_tmp_2=v_output;
size_t dn_tmp_3=v_offset;
dn_f_codec_encode(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecIo dn_cb_PosMessage2_encode(dn_r_PosMessage2 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset) {
 if(!dn_enter()){return (dn_r_CodecIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecIo){0};}
 dn_r_CodecIo dn_result=dn_f_PosMessage2_encode(v_message,v_output,v_offset);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_PosMessage2_equal(dn_r_PosMessage2 const * v_left,dn_r_PosMessage2 const * v_right) {
{
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_2(); });
{ bool dn_tmp_3=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_left)->f_inner));
dn_r_CodecMessage const * dn_tmp_2=(&((v_right)->f_inner));
dn_f_codec_equal(dn_tmp_0,dn_tmp_1,dn_tmp_2); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static bool dn_cb_PosMessage2_equal(dn_r_PosMessage2 const * v_left,dn_r_PosMessage2 const * v_right) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_PosMessage2_equal(v_left,v_right);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecDecoded dn_f_Pos_decode(dn_t_slice0_U8 v_input) {
{
dn_r_ProtocolValue v_id=({ dn_t_slice0_U8 dn_tmp_0=v_input;
size_t dn_tmp_1=((size_t)(0ULL));
size_t dn_tmp_2=((size_t)(4ULL));
dn_f_net_read_be(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
if(({ int32_t dn_tmp_3=(v_id).f_status;
int32_t dn_tmp_4=((int32_t)(0ULL));
(dn_tmp_3 != dn_tmp_4); })){
{ dn_r_CodecDecoded dn_tmp_8=({ int32_t dn_tmp_5=(v_id).f_status;
uint32_t dn_tmp_6=((uint32_t)(0ULL));
size_t dn_tmp_7=((size_t)(0ULL));
dn_f_codec_decode_error(dn_tmp_5,dn_tmp_6,dn_tmp_7); });dn_adopt_r12_CodecDecoded(&dn_tmp_8);return dn_tmp_8; }
}
else{
}
if(({ uint64_t dn_tmp_9=(v_id).f_value;
uint64_t dn_tmp_10=({ uint64_t dn_tmp_11=((uint64_t)(1ULL));
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_11; });
(dn_tmp_9 == dn_tmp_10); })){
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_1(); });
{ dn_r_CodecDecoded dn_tmp_14=({ dn_r_CodecPlan const * dn_tmp_12=(&(v_plan));
dn_t_slice0_U8 dn_tmp_13=v_input;
dn_f_codec_decode(dn_tmp_12,dn_tmp_13); });dn_adopt_r12_CodecDecoded(&dn_tmp_14);return dn_tmp_14; }
}
else{
}
if(({ uint64_t dn_tmp_15=(v_id).f_value;
uint64_t dn_tmp_16=({ uint64_t dn_tmp_17=((uint64_t)(2ULL));
if((unsigned __int128)dn_tmp_17>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_17; });
(dn_tmp_15 == dn_tmp_16); })){
dn_r_CodecPlan v_plan=({ dn_f_Pos_plan_2(); });
{ dn_r_CodecDecoded dn_tmp_20=({ dn_r_CodecPlan const * dn_tmp_18=(&(v_plan));
dn_t_slice0_U8 dn_tmp_19=v_input;
dn_f_codec_decode(dn_tmp_18,dn_tmp_19); });dn_adopt_r12_CodecDecoded(&dn_tmp_20);return dn_tmp_20; }
}
else{
}
{ dn_r_CodecDecoded dn_tmp_24=({ int32_t dn_tmp_21=((int32_t)(11ULL));
uint32_t dn_tmp_22=((uint32_t)(0ULL));
size_t dn_tmp_23=((size_t)(0ULL));
dn_f_codec_decode_error(dn_tmp_21,dn_tmp_22,dn_tmp_23); });dn_adopt_r12_CodecDecoded(&dn_tmp_24);return dn_tmp_24; }
}

dn_fail("missing native return");
}
static dn_r_CodecDecoded dn_cb_Pos_decode(dn_t_slice0_U8 v_input) {
 if(!dn_enter()){return (dn_r_CodecDecoded){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecDecoded){0};}
 dn_r_CodecDecoded dn_result=dn_f_Pos_decode(v_input);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkSpec dn_f_Little_spec(void) {
{
dn_r_NetworkSpec v_spec=({ uint32_t dn_tmp_0=((uint32_t)(1129268273ULL));
uint32_t dn_tmp_1=((uint32_t)(1ULL));
uint32_t dn_tmp_2=((uint32_t)(1ULL));
uint32_t dn_tmp_3=((uint32_t)(0ULL));
dn_f_network_spec(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_WireFormat dn_tmp_9=({ uint32_t dn_tmp_4=((uint32_t)(1ULL));
uint32_t dn_tmp_5=((uint32_t)(2ULL));
uint32_t dn_tmp_6=((uint32_t)(0ULL));
uint32_t dn_tmp_7=((uint32_t)(0ULL));
((dn_r_WireFormat){dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7}); });dn_r_WireFormat *dn_tmp_8=&((v_spec).f_wire);*dn_tmp_8=dn_tmp_9; }
{ dn_r_TransportRequirements dn_tmp_14=({ uint32_t dn_tmp_10=((uint32_t)(19ULL));
uint32_t dn_tmp_11=((uint32_t)(0ULL));
uint32_t dn_tmp_12=((uint32_t)(0ULL));
((dn_r_TransportRequirements){dn_tmp_10,dn_tmp_11,dn_tmp_12}); });dn_r_TransportRequirements *dn_tmp_13=&((v_spec).f_transport);*dn_tmp_13=dn_tmp_14; }
{ dn_r_ResourceBounds dn_tmp_26=({ dn_r_TransportBounds dn_tmp_15=({ uint32_t dn_tmp_16=((uint32_t)(65536ULL));
uint32_t dn_tmp_17=((uint32_t)(32ULL));
uint32_t dn_tmp_18=((uint32_t)(16ULL));
size_t dn_tmp_19=((size_t)(8388608ULL));
((dn_r_TransportBounds){dn_tmp_16,dn_tmp_17,dn_tmp_18,dn_tmp_19}); });
uint32_t dn_tmp_20=((uint32_t)(0ULL));
uint32_t dn_tmp_21=((uint32_t)(0ULL));
uint64_t dn_tmp_22=({ uint64_t dn_tmp_23=((uint64_t)(1000000ULL));
if((unsigned __int128)dn_tmp_23>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_23; });
uint32_t dn_tmp_24=((uint32_t)(529ULL));
((dn_r_ResourceBounds){dn_tmp_15,dn_tmp_20,dn_tmp_21,dn_tmp_22,dn_tmp_24}); });dn_r_ResourceBounds *dn_tmp_25=&((v_spec).f_resources);*dn_tmp_25=dn_tmp_26; }
{ dn_r_SessionContract dn_tmp_33=({ uint32_t dn_tmp_27=((uint32_t)(10000ULL));
uint32_t dn_tmp_28=((uint32_t)(30000ULL));
uint32_t dn_tmp_29=((uint32_t)(300000ULL));
uint32_t dn_tmp_30=((uint32_t)(32ULL));
bool dn_tmp_31=0;
((dn_r_SessionContract){dn_tmp_27,dn_tmp_28,dn_tmp_29,dn_tmp_30,dn_tmp_31}); });dn_r_SessionContract *dn_tmp_32=&((v_spec).f_session);*dn_tmp_32=dn_tmp_33; }
{ dn_r_SecurityRequirements dn_tmp_39=({ uint32_t dn_tmp_34=((uint32_t)(0ULL));
bool dn_tmp_35=0;
bool dn_tmp_36=0;
uint32_t dn_tmp_37=((uint32_t)(0ULL));
((dn_r_SecurityRequirements){dn_tmp_34,dn_tmp_35,dn_tmp_36,dn_tmp_37}); });dn_r_SecurityRequirements *dn_tmp_38=&((v_spec).f_security);*dn_tmp_38=dn_tmp_39; }
{ dn_r_ProofObligations dn_tmp_43=({ uint32_t dn_tmp_40=((uint32_t)(0ULL));
uint32_t dn_tmp_41=((uint32_t)(1ULL));
((dn_r_ProofObligations){dn_tmp_40,dn_tmp_41}); });dn_r_ProofObligations *dn_tmp_42=&((v_spec).f_proofs);*dn_tmp_42=dn_tmp_43; }
{ uint32_t dn_tmp_45=((uint32_t)(2ULL));uint32_t *dn_tmp_44=&((v_spec).f_message_count);*dn_tmp_44=dn_tmp_45; }
{ dn_r_MessageSchema dn_tmp_52=({ uint32_t dn_tmp_48=((uint32_t)(1ULL));
uint32_t dn_tmp_49=((uint32_t)(2ULL));
uint32_t dn_tmp_50=((uint32_t)(3ULL));
dn_f_message_schema(dn_tmp_48,dn_tmp_49,dn_tmp_50); });dn_r_MessageSchema *dn_tmp_51=&((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_46=&((v_spec).f_messages);size_t dn_tmp_47=((size_t)(0ULL));&dn_tmp_46->data[dn_index(dn_tmp_47,16)]; })));*dn_tmp_51=dn_tmp_52; }
{ uint32_t dn_tmp_56=((uint32_t)(0ULL));uint32_t *dn_tmp_55=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_53=&((v_spec).f_messages);size_t dn_tmp_54=((size_t)(0ULL));&dn_tmp_53->data[dn_index(dn_tmp_54,16)]; }))).f_first_minor);*dn_tmp_55=dn_tmp_56; }
{ uint32_t dn_tmp_60=((uint32_t)(4294967295ULL));uint32_t *dn_tmp_59=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_57=&((v_spec).f_messages);size_t dn_tmp_58=((size_t)(0ULL));&dn_tmp_57->data[dn_index(dn_tmp_58,16)]; }))).f_last_minor);*dn_tmp_59=dn_tmp_60; }
{ uint32_t dn_tmp_64=((uint32_t)(11ULL));uint32_t *dn_tmp_63=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_61=&((v_spec).f_messages);size_t dn_tmp_62=((size_t)(0ULL));&dn_tmp_61->data[dn_index(dn_tmp_62,16)]; }))).f_field_count);*dn_tmp_63=dn_tmp_64; }
{ dn_r_MessageField dn_tmp_78=({ uint32_t dn_tmp_69=((uint32_t)(1ULL));
uint32_t dn_tmp_70=((uint32_t)(1ULL));
uint32_t dn_tmp_71=((uint32_t)(1ULL));
uint32_t dn_tmp_72=((uint32_t)(1ULL));
uint32_t dn_tmp_73=((uint32_t)(0ULL));
uint32_t dn_tmp_74=((uint32_t)(0ULL));
uint32_t dn_tmp_75=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_76=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_69,dn_tmp_70,dn_tmp_71,dn_tmp_72,dn_tmp_73,dn_tmp_74,dn_tmp_75,dn_tmp_76}); });dn_r_MessageField *dn_tmp_77=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_65=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_67=&((v_spec).f_messages);size_t dn_tmp_68=((size_t)(0ULL));&dn_tmp_67->data[dn_index(dn_tmp_68,16)]; }))).f_fields);size_t dn_tmp_66=((size_t)(0ULL));&dn_tmp_65->data[dn_index(dn_tmp_66,32)]; })));*dn_tmp_77=dn_tmp_78; }
{ dn_r_MessageField dn_tmp_92=({ uint32_t dn_tmp_83=((uint32_t)(2ULL));
uint32_t dn_tmp_84=((uint32_t)(2ULL));
uint32_t dn_tmp_85=((uint32_t)(0ULL));
uint32_t dn_tmp_86=((uint32_t)(1ULL));
uint32_t dn_tmp_87=((uint32_t)(0ULL));
uint32_t dn_tmp_88=((uint32_t)(0ULL));
uint32_t dn_tmp_89=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_90=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_83,dn_tmp_84,dn_tmp_85,dn_tmp_86,dn_tmp_87,dn_tmp_88,dn_tmp_89,dn_tmp_90}); });dn_r_MessageField *dn_tmp_91=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_79=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_81=&((v_spec).f_messages);size_t dn_tmp_82=((size_t)(0ULL));&dn_tmp_81->data[dn_index(dn_tmp_82,16)]; }))).f_fields);size_t dn_tmp_80=((size_t)(1ULL));&dn_tmp_79->data[dn_index(dn_tmp_80,32)]; })));*dn_tmp_91=dn_tmp_92; }
{ dn_r_MessageField dn_tmp_106=({ uint32_t dn_tmp_97=((uint32_t)(3ULL));
uint32_t dn_tmp_98=((uint32_t)(3ULL));
uint32_t dn_tmp_99=((uint32_t)(0ULL));
uint32_t dn_tmp_100=((uint32_t)(3ULL));
uint32_t dn_tmp_101=((uint32_t)(0ULL));
uint32_t dn_tmp_102=((uint32_t)(0ULL));
uint32_t dn_tmp_103=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_104=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_97,dn_tmp_98,dn_tmp_99,dn_tmp_100,dn_tmp_101,dn_tmp_102,dn_tmp_103,dn_tmp_104}); });dn_r_MessageField *dn_tmp_105=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_93=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_95=&((v_spec).f_messages);size_t dn_tmp_96=((size_t)(0ULL));&dn_tmp_95->data[dn_index(dn_tmp_96,16)]; }))).f_fields);size_t dn_tmp_94=((size_t)(2ULL));&dn_tmp_93->data[dn_index(dn_tmp_94,32)]; })));*dn_tmp_105=dn_tmp_106; }
{ dn_r_MessageField dn_tmp_120=({ uint32_t dn_tmp_111=((uint32_t)(4ULL));
uint32_t dn_tmp_112=((uint32_t)(4ULL));
uint32_t dn_tmp_113=((uint32_t)(1ULL));
uint32_t dn_tmp_114=((uint32_t)(1ULL));
uint32_t dn_tmp_115=((uint32_t)(0ULL));
uint32_t dn_tmp_116=((uint32_t)(0ULL));
uint32_t dn_tmp_117=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_118=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_111,dn_tmp_112,dn_tmp_113,dn_tmp_114,dn_tmp_115,dn_tmp_116,dn_tmp_117,dn_tmp_118}); });dn_r_MessageField *dn_tmp_119=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_107=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_109=&((v_spec).f_messages);size_t dn_tmp_110=((size_t)(0ULL));&dn_tmp_109->data[dn_index(dn_tmp_110,16)]; }))).f_fields);size_t dn_tmp_108=((size_t)(3ULL));&dn_tmp_107->data[dn_index(dn_tmp_108,32)]; })));*dn_tmp_119=dn_tmp_120; }
{ dn_r_MessageField dn_tmp_134=({ uint32_t dn_tmp_125=((uint32_t)(5ULL));
uint32_t dn_tmp_126=((uint32_t)(5ULL));
uint32_t dn_tmp_127=((uint32_t)(1ULL));
uint32_t dn_tmp_128=((uint32_t)(1ULL));
uint32_t dn_tmp_129=((uint32_t)(0ULL));
uint32_t dn_tmp_130=((uint32_t)(0ULL));
uint32_t dn_tmp_131=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_132=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_125,dn_tmp_126,dn_tmp_127,dn_tmp_128,dn_tmp_129,dn_tmp_130,dn_tmp_131,dn_tmp_132}); });dn_r_MessageField *dn_tmp_133=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_121=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_123=&((v_spec).f_messages);size_t dn_tmp_124=((size_t)(0ULL));&dn_tmp_123->data[dn_index(dn_tmp_124,16)]; }))).f_fields);size_t dn_tmp_122=((size_t)(4ULL));&dn_tmp_121->data[dn_index(dn_tmp_122,32)]; })));*dn_tmp_133=dn_tmp_134; }
{ dn_r_MessageField dn_tmp_148=({ uint32_t dn_tmp_139=((uint32_t)(6ULL));
uint32_t dn_tmp_140=((uint32_t)(6ULL));
uint32_t dn_tmp_141=((uint32_t)(0ULL));
uint32_t dn_tmp_142=((uint32_t)(2ULL));
uint32_t dn_tmp_143=((uint32_t)(32ULL));
uint32_t dn_tmp_144=((uint32_t)(0ULL));
uint32_t dn_tmp_145=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_146=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_139,dn_tmp_140,dn_tmp_141,dn_tmp_142,dn_tmp_143,dn_tmp_144,dn_tmp_145,dn_tmp_146}); });dn_r_MessageField *dn_tmp_147=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_135=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_137=&((v_spec).f_messages);size_t dn_tmp_138=((size_t)(0ULL));&dn_tmp_137->data[dn_index(dn_tmp_138,16)]; }))).f_fields);size_t dn_tmp_136=((size_t)(5ULL));&dn_tmp_135->data[dn_index(dn_tmp_136,32)]; })));*dn_tmp_147=dn_tmp_148; }
{ dn_r_MessageField dn_tmp_162=({ uint32_t dn_tmp_153=((uint32_t)(7ULL));
uint32_t dn_tmp_154=((uint32_t)(7ULL));
uint32_t dn_tmp_155=((uint32_t)(0ULL));
uint32_t dn_tmp_156=((uint32_t)(1ULL));
uint32_t dn_tmp_157=((uint32_t)(64ULL));
uint32_t dn_tmp_158=((uint32_t)(0ULL));
uint32_t dn_tmp_159=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_160=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_153,dn_tmp_154,dn_tmp_155,dn_tmp_156,dn_tmp_157,dn_tmp_158,dn_tmp_159,dn_tmp_160}); });dn_r_MessageField *dn_tmp_161=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_149=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_151=&((v_spec).f_messages);size_t dn_tmp_152=((size_t)(0ULL));&dn_tmp_151->data[dn_index(dn_tmp_152,16)]; }))).f_fields);size_t dn_tmp_150=((size_t)(6ULL));&dn_tmp_149->data[dn_index(dn_tmp_150,32)]; })));*dn_tmp_161=dn_tmp_162; }
{ dn_r_MessageField dn_tmp_176=({ uint32_t dn_tmp_167=((uint32_t)(8ULL));
uint32_t dn_tmp_168=((uint32_t)(8ULL));
uint32_t dn_tmp_169=((uint32_t)(0ULL));
uint32_t dn_tmp_170=((uint32_t)(4ULL));
uint32_t dn_tmp_171=((uint32_t)(0ULL));
uint32_t dn_tmp_172=((uint32_t)(0ULL));
uint32_t dn_tmp_173=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_174=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_167,dn_tmp_168,dn_tmp_169,dn_tmp_170,dn_tmp_171,dn_tmp_172,dn_tmp_173,dn_tmp_174}); });dn_r_MessageField *dn_tmp_175=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_163=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_165=&((v_spec).f_messages);size_t dn_tmp_166=((size_t)(0ULL));&dn_tmp_165->data[dn_index(dn_tmp_166,16)]; }))).f_fields);size_t dn_tmp_164=((size_t)(7ULL));&dn_tmp_163->data[dn_index(dn_tmp_164,32)]; })));*dn_tmp_175=dn_tmp_176; }
{ dn_r_MessageField dn_tmp_190=({ uint32_t dn_tmp_181=((uint32_t)(9ULL));
uint32_t dn_tmp_182=((uint32_t)(9ULL));
uint32_t dn_tmp_183=((uint32_t)(1ULL));
uint32_t dn_tmp_184=((uint32_t)(2ULL));
uint32_t dn_tmp_185=((uint32_t)(0ULL));
uint32_t dn_tmp_186=((uint32_t)(0ULL));
uint32_t dn_tmp_187=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_188=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_181,dn_tmp_182,dn_tmp_183,dn_tmp_184,dn_tmp_185,dn_tmp_186,dn_tmp_187,dn_tmp_188}); });dn_r_MessageField *dn_tmp_189=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_177=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_179=&((v_spec).f_messages);size_t dn_tmp_180=((size_t)(0ULL));&dn_tmp_179->data[dn_index(dn_tmp_180,16)]; }))).f_fields);size_t dn_tmp_178=((size_t)(8ULL));&dn_tmp_177->data[dn_index(dn_tmp_178,32)]; })));*dn_tmp_189=dn_tmp_190; }
{ dn_r_MessageField dn_tmp_204=({ uint32_t dn_tmp_195=((uint32_t)(10ULL));
uint32_t dn_tmp_196=((uint32_t)(2ULL));
uint32_t dn_tmp_197=((uint32_t)(0ULL));
uint32_t dn_tmp_198=((uint32_t)(1ULL));
uint32_t dn_tmp_199=((uint32_t)(0ULL));
uint32_t dn_tmp_200=((uint32_t)(1ULL));
uint32_t dn_tmp_201=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_202=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_195,dn_tmp_196,dn_tmp_197,dn_tmp_198,dn_tmp_199,dn_tmp_200,dn_tmp_201,dn_tmp_202}); });dn_r_MessageField *dn_tmp_203=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_191=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_193=&((v_spec).f_messages);size_t dn_tmp_194=((size_t)(0ULL));&dn_tmp_193->data[dn_index(dn_tmp_194,16)]; }))).f_fields);size_t dn_tmp_192=((size_t)(9ULL));&dn_tmp_191->data[dn_index(dn_tmp_192,32)]; })));*dn_tmp_203=dn_tmp_204; }
{ dn_r_MessageField dn_tmp_218=({ uint32_t dn_tmp_209=((uint32_t)(11ULL));
uint32_t dn_tmp_210=((uint32_t)(6ULL));
uint32_t dn_tmp_211=((uint32_t)(0ULL));
uint32_t dn_tmp_212=((uint32_t)(1ULL));
uint32_t dn_tmp_213=((uint32_t)(8ULL));
uint32_t dn_tmp_214=((uint32_t)(0ULL));
uint32_t dn_tmp_215=((uint32_t)(0ULL));
uint32_t dn_tmp_216=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_209,dn_tmp_210,dn_tmp_211,dn_tmp_212,dn_tmp_213,dn_tmp_214,dn_tmp_215,dn_tmp_216}); });dn_r_MessageField *dn_tmp_217=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_205=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_207=&((v_spec).f_messages);size_t dn_tmp_208=((size_t)(0ULL));&dn_tmp_207->data[dn_index(dn_tmp_208,16)]; }))).f_fields);size_t dn_tmp_206=((size_t)(10ULL));&dn_tmp_205->data[dn_index(dn_tmp_206,32)]; })));*dn_tmp_217=dn_tmp_218; }
{ dn_r_MessageSchema dn_tmp_225=({ uint32_t dn_tmp_221=((uint32_t)(2ULL));
uint32_t dn_tmp_222=((uint32_t)(3ULL));
uint32_t dn_tmp_223=((uint32_t)(3ULL));
dn_f_message_schema(dn_tmp_221,dn_tmp_222,dn_tmp_223); });dn_r_MessageSchema *dn_tmp_224=&((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_219=&((v_spec).f_messages);size_t dn_tmp_220=((size_t)(1ULL));&dn_tmp_219->data[dn_index(dn_tmp_220,16)]; })));*dn_tmp_224=dn_tmp_225; }
{ uint32_t dn_tmp_229=((uint32_t)(0ULL));uint32_t *dn_tmp_228=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_226=&((v_spec).f_messages);size_t dn_tmp_227=((size_t)(1ULL));&dn_tmp_226->data[dn_index(dn_tmp_227,16)]; }))).f_first_minor);*dn_tmp_228=dn_tmp_229; }
{ uint32_t dn_tmp_233=((uint32_t)(4294967295ULL));uint32_t *dn_tmp_232=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_230=&((v_spec).f_messages);size_t dn_tmp_231=((size_t)(1ULL));&dn_tmp_230->data[dn_index(dn_tmp_231,16)]; }))).f_last_minor);*dn_tmp_232=dn_tmp_233; }
{ uint32_t dn_tmp_237=((uint32_t)(0ULL));uint32_t *dn_tmp_236=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_234=&((v_spec).f_messages);size_t dn_tmp_235=((size_t)(1ULL));&dn_tmp_234->data[dn_index(dn_tmp_235,16)]; }))).f_field_count);*dn_tmp_236=dn_tmp_237; }
{ dn_r_NetworkSpec dn_tmp_238=v_spec;return dn_tmp_238; }
}

dn_fail("missing native return");
}
static dn_r_NetworkSpec dn_cb_Little_spec(void) {
 if(!dn_enter()){return (dn_r_NetworkSpec){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkSpec){0};}
 dn_r_NetworkSpec dn_result=dn_f_Little_spec();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecPlan dn_f_Little_plan_1(void) {
{
dn_r_CodecPlan v_plan=({ ((dn_r_CodecPlan){0}); });
{ uint32_t dn_tmp_1=((uint32_t)(1ULL));uint32_t *dn_tmp_0=&((v_plan).f_message);*dn_tmp_0=dn_tmp_1; }
{ dn_r_WireFormat dn_tmp_7=({ uint32_t dn_tmp_2=((uint32_t)(1ULL));
uint32_t dn_tmp_3=((uint32_t)(2ULL));
uint32_t dn_tmp_4=((uint32_t)(0ULL));
uint32_t dn_tmp_5=((uint32_t)(0ULL));
((dn_r_WireFormat){dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5}); });dn_r_WireFormat *dn_tmp_6=&((v_plan).f_wire);*dn_tmp_6=dn_tmp_7; }
{ uint32_t dn_tmp_9=((uint32_t)(0ULL));uint32_t *dn_tmp_8=&((v_plan).f_minor);*dn_tmp_8=dn_tmp_9; }
{ uint32_t dn_tmp_11=((uint32_t)(10ULL));uint32_t *dn_tmp_10=&((v_plan).f_field_count);*dn_tmp_10=dn_tmp_11; }
{ dn_r_MessageField dn_tmp_23=({ uint32_t dn_tmp_14=((uint32_t)(1ULL));
uint32_t dn_tmp_15=((uint32_t)(1ULL));
uint32_t dn_tmp_16=((uint32_t)(1ULL));
uint32_t dn_tmp_17=((uint32_t)(1ULL));
uint32_t dn_tmp_18=((uint32_t)(0ULL));
uint32_t dn_tmp_19=((uint32_t)(0ULL));
uint32_t dn_tmp_20=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_21=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_14,dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_18,dn_tmp_19,dn_tmp_20,dn_tmp_21}); });dn_r_MessageField *dn_tmp_22=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_12=&((v_plan).f_fields);size_t dn_tmp_13=((size_t)(0ULL));&dn_tmp_12->data[dn_index(dn_tmp_13,32)]; })));*dn_tmp_22=dn_tmp_23; }
{ dn_r_MessageField dn_tmp_35=({ uint32_t dn_tmp_26=((uint32_t)(2ULL));
uint32_t dn_tmp_27=((uint32_t)(2ULL));
uint32_t dn_tmp_28=((uint32_t)(0ULL));
uint32_t dn_tmp_29=((uint32_t)(1ULL));
uint32_t dn_tmp_30=((uint32_t)(0ULL));
uint32_t dn_tmp_31=((uint32_t)(0ULL));
uint32_t dn_tmp_32=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_33=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_26,dn_tmp_27,dn_tmp_28,dn_tmp_29,dn_tmp_30,dn_tmp_31,dn_tmp_32,dn_tmp_33}); });dn_r_MessageField *dn_tmp_34=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_24=&((v_plan).f_fields);size_t dn_tmp_25=((size_t)(1ULL));&dn_tmp_24->data[dn_index(dn_tmp_25,32)]; })));*dn_tmp_34=dn_tmp_35; }
{ dn_r_MessageField dn_tmp_47=({ uint32_t dn_tmp_38=((uint32_t)(3ULL));
uint32_t dn_tmp_39=((uint32_t)(3ULL));
uint32_t dn_tmp_40=((uint32_t)(0ULL));
uint32_t dn_tmp_41=((uint32_t)(3ULL));
uint32_t dn_tmp_42=((uint32_t)(0ULL));
uint32_t dn_tmp_43=((uint32_t)(0ULL));
uint32_t dn_tmp_44=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_45=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_38,dn_tmp_39,dn_tmp_40,dn_tmp_41,dn_tmp_42,dn_tmp_43,dn_tmp_44,dn_tmp_45}); });dn_r_MessageField *dn_tmp_46=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_36=&((v_plan).f_fields);size_t dn_tmp_37=((size_t)(2ULL));&dn_tmp_36->data[dn_index(dn_tmp_37,32)]; })));*dn_tmp_46=dn_tmp_47; }
{ dn_r_MessageField dn_tmp_59=({ uint32_t dn_tmp_50=((uint32_t)(4ULL));
uint32_t dn_tmp_51=((uint32_t)(4ULL));
uint32_t dn_tmp_52=((uint32_t)(1ULL));
uint32_t dn_tmp_53=((uint32_t)(1ULL));
uint32_t dn_tmp_54=((uint32_t)(0ULL));
uint32_t dn_tmp_55=((uint32_t)(0ULL));
uint32_t dn_tmp_56=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_57=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_50,dn_tmp_51,dn_tmp_52,dn_tmp_53,dn_tmp_54,dn_tmp_55,dn_tmp_56,dn_tmp_57}); });dn_r_MessageField *dn_tmp_58=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_48=&((v_plan).f_fields);size_t dn_tmp_49=((size_t)(3ULL));&dn_tmp_48->data[dn_index(dn_tmp_49,32)]; })));*dn_tmp_58=dn_tmp_59; }
{ dn_r_MessageField dn_tmp_71=({ uint32_t dn_tmp_62=((uint32_t)(5ULL));
uint32_t dn_tmp_63=((uint32_t)(5ULL));
uint32_t dn_tmp_64=((uint32_t)(1ULL));
uint32_t dn_tmp_65=((uint32_t)(1ULL));
uint32_t dn_tmp_66=((uint32_t)(0ULL));
uint32_t dn_tmp_67=((uint32_t)(0ULL));
uint32_t dn_tmp_68=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_69=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_62,dn_tmp_63,dn_tmp_64,dn_tmp_65,dn_tmp_66,dn_tmp_67,dn_tmp_68,dn_tmp_69}); });dn_r_MessageField *dn_tmp_70=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_60=&((v_plan).f_fields);size_t dn_tmp_61=((size_t)(4ULL));&dn_tmp_60->data[dn_index(dn_tmp_61,32)]; })));*dn_tmp_70=dn_tmp_71; }
{ dn_r_MessageField dn_tmp_83=({ uint32_t dn_tmp_74=((uint32_t)(6ULL));
uint32_t dn_tmp_75=((uint32_t)(6ULL));
uint32_t dn_tmp_76=((uint32_t)(0ULL));
uint32_t dn_tmp_77=((uint32_t)(2ULL));
uint32_t dn_tmp_78=((uint32_t)(32ULL));
uint32_t dn_tmp_79=((uint32_t)(0ULL));
uint32_t dn_tmp_80=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_81=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_74,dn_tmp_75,dn_tmp_76,dn_tmp_77,dn_tmp_78,dn_tmp_79,dn_tmp_80,dn_tmp_81}); });dn_r_MessageField *dn_tmp_82=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_72=&((v_plan).f_fields);size_t dn_tmp_73=((size_t)(5ULL));&dn_tmp_72->data[dn_index(dn_tmp_73,32)]; })));*dn_tmp_82=dn_tmp_83; }
{ dn_r_MessageField dn_tmp_95=({ uint32_t dn_tmp_86=((uint32_t)(7ULL));
uint32_t dn_tmp_87=((uint32_t)(7ULL));
uint32_t dn_tmp_88=((uint32_t)(0ULL));
uint32_t dn_tmp_89=((uint32_t)(1ULL));
uint32_t dn_tmp_90=((uint32_t)(64ULL));
uint32_t dn_tmp_91=((uint32_t)(0ULL));
uint32_t dn_tmp_92=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_93=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_86,dn_tmp_87,dn_tmp_88,dn_tmp_89,dn_tmp_90,dn_tmp_91,dn_tmp_92,dn_tmp_93}); });dn_r_MessageField *dn_tmp_94=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_84=&((v_plan).f_fields);size_t dn_tmp_85=((size_t)(6ULL));&dn_tmp_84->data[dn_index(dn_tmp_85,32)]; })));*dn_tmp_94=dn_tmp_95; }
{ dn_r_MessageField dn_tmp_107=({ uint32_t dn_tmp_98=((uint32_t)(8ULL));
uint32_t dn_tmp_99=((uint32_t)(8ULL));
uint32_t dn_tmp_100=((uint32_t)(0ULL));
uint32_t dn_tmp_101=((uint32_t)(4ULL));
uint32_t dn_tmp_102=((uint32_t)(0ULL));
uint32_t dn_tmp_103=((uint32_t)(0ULL));
uint32_t dn_tmp_104=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_105=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_98,dn_tmp_99,dn_tmp_100,dn_tmp_101,dn_tmp_102,dn_tmp_103,dn_tmp_104,dn_tmp_105}); });dn_r_MessageField *dn_tmp_106=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_96=&((v_plan).f_fields);size_t dn_tmp_97=((size_t)(7ULL));&dn_tmp_96->data[dn_index(dn_tmp_97,32)]; })));*dn_tmp_106=dn_tmp_107; }
{ dn_r_MessageField dn_tmp_119=({ uint32_t dn_tmp_110=((uint32_t)(9ULL));
uint32_t dn_tmp_111=((uint32_t)(9ULL));
uint32_t dn_tmp_112=((uint32_t)(1ULL));
uint32_t dn_tmp_113=((uint32_t)(2ULL));
uint32_t dn_tmp_114=((uint32_t)(0ULL));
uint32_t dn_tmp_115=((uint32_t)(0ULL));
uint32_t dn_tmp_116=((uint32_t)(4294967295ULL));
uint32_t dn_tmp_117=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_110,dn_tmp_111,dn_tmp_112,dn_tmp_113,dn_tmp_114,dn_tmp_115,dn_tmp_116,dn_tmp_117}); });dn_r_MessageField *dn_tmp_118=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_108=&((v_plan).f_fields);size_t dn_tmp_109=((size_t)(8ULL));&dn_tmp_108->data[dn_index(dn_tmp_109,32)]; })));*dn_tmp_118=dn_tmp_119; }
{ dn_r_MessageField dn_tmp_131=({ uint32_t dn_tmp_122=((uint32_t)(11ULL));
uint32_t dn_tmp_123=((uint32_t)(6ULL));
uint32_t dn_tmp_124=((uint32_t)(0ULL));
uint32_t dn_tmp_125=((uint32_t)(1ULL));
uint32_t dn_tmp_126=((uint32_t)(8ULL));
uint32_t dn_tmp_127=((uint32_t)(0ULL));
uint32_t dn_tmp_128=((uint32_t)(0ULL));
uint32_t dn_tmp_129=((uint32_t)(0ULL));
((dn_r_MessageField){dn_tmp_122,dn_tmp_123,dn_tmp_124,dn_tmp_125,dn_tmp_126,dn_tmp_127,dn_tmp_128,dn_tmp_129}); });dn_r_MessageField *dn_tmp_130=&((*({ dn_t_arr32_r12_MessageField *dn_tmp_120=&((v_plan).f_fields);size_t dn_tmp_121=((size_t)(9ULL));&dn_tmp_120->data[dn_index(dn_tmp_121,32)]; })));*dn_tmp_130=dn_tmp_131; }
{ uint32_t dn_tmp_133=((uint32_t)(17ULL));uint32_t *dn_tmp_132=&((v_plan).f_maximum_values);*dn_tmp_132=dn_tmp_133; }
{ size_t dn_tmp_135=((size_t)(268ULL));size_t *dn_tmp_134=&((v_plan).f_maximum_wire);*dn_tmp_134=dn_tmp_135; }
{ uint32_t dn_tmp_137=((uint32_t)(0ULL));uint32_t *dn_tmp_136=&((v_plan).f_maximum_unknown_fields);*dn_tmp_136=dn_tmp_137; }
{ size_t dn_tmp_139=((size_t)(0ULL));size_t *dn_tmp_138=&((v_plan).f_maximum_unknown_bytes);*dn_tmp_138=dn_tmp_139; }
{ size_t dn_tmp_141=((size_t)(8388608ULL));size_t *dn_tmp_140=&((v_plan).f_storage_budget);*dn_tmp_140=dn_tmp_141; }
{ dn_r_CodecPlan dn_tmp_142=v_plan;return dn_tmp_142; }
}

dn_fail("missing native return");
}
static dn_r_CodecPlan dn_cb_Little_plan_1(void) {
 if(!dn_enter()){return (dn_r_CodecPlan){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecPlan){0};}
 dn_r_CodecPlan dn_result=dn_f_Little_plan_1();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_LittleMessage1 dn_f_LittleMessage1_new(void) {
{
{ dn_r_LittleMessage1 dn_tmp_2=({ dn_r_CodecMessage dn_tmp_0=({ uint32_t dn_tmp_1=((uint32_t)(1ULL));
dn_f_codec_message(dn_tmp_1); });
dn_adopt_r12_CodecMessage(&dn_tmp_0);
((dn_r_LittleMessage1){dn_tmp_0}); });dn_adopt_r14_LittleMessage1(&dn_tmp_2);return dn_tmp_2; }
}

dn_fail("missing native return");
}
static dn_r_LittleMessage1 dn_cb_LittleMessage1_new(void) {
 if(!dn_enter()){return (dn_r_LittleMessage1){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_LittleMessage1){0};}
 dn_r_LittleMessage1 dn_result=dn_f_LittleMessage1_new();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_LittleMessage1Decoded dn_f_LittleMessage1_decode(dn_t_slice0_U8 v_input) {
{
dn_r_CodecDecoded volatile v_decoded={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r12_CodecDecoded(&v_decoded);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
v_decoded=({ dn_r_CodecPlan const * dn_tmp_2=(&(v_plan));
dn_t_slice0_U8 dn_tmp_3=v_input;
dn_f_codec_decode(dn_tmp_2,dn_tmp_3); });
{ dn_r_LittleMessage1Decoded dn_tmp_8=({ dn_r_LittleMessage1 dn_tmp_4=({ dn_r_CodecMessage dn_tmp_5=({ dn_r_CodecMessage dn_tmp_6=(v_decoded).f_value;(v_decoded).f_value=(dn_r_CodecMessage){0};dn_adopt_r12_CodecMessage(&dn_tmp_6);dn_tmp_6; });
dn_adopt_r12_CodecMessage(&dn_tmp_5);
((dn_r_LittleMessage1){dn_tmp_5}); });
dn_adopt_r14_LittleMessage1(&dn_tmp_4);
dn_r_CodecError dn_tmp_7=(v_decoded).f_error;
((dn_r_LittleMessage1Decoded){dn_tmp_4,dn_tmp_7}); });dn_adopt_r21_LittleMessage1Decoded(&dn_tmp_8);dn_drop_r12_CodecDecoded(&v_decoded);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_8; }
dn_drop_r12_CodecDecoded(&v_decoded);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_r_LittleMessage1Decoded dn_cb_LittleMessage1_decode(dn_t_slice0_U8 v_input) {
 if(!dn_enter()){return (dn_r_LittleMessage1Decoded){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_LittleMessage1Decoded){0};}
 dn_r_LittleMessage1Decoded dn_result=dn_f_LittleMessage1_decode(v_input);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecIo dn_f_LittleMessage1_measure(dn_r_LittleMessage1 const * v_message) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
{ dn_r_CodecIo dn_tmp_2=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
dn_f_codec_measure(dn_tmp_0,dn_tmp_1); });return dn_tmp_2; }
}

dn_fail("missing native return");
}
static dn_r_CodecIo dn_cb_LittleMessage1_measure(dn_r_LittleMessage1 const * v_message) {
 if(!dn_enter()){return (dn_r_CodecIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecIo){0};}
 dn_r_CodecIo dn_result=dn_f_LittleMessage1_measure(v_message);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecIo dn_f_LittleMessage1_encode(dn_r_LittleMessage1 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
{ dn_r_CodecIo dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
dn_t_buf_U8 * dn_tmp_2=v_output;
size_t dn_tmp_3=v_offset;
dn_f_codec_encode(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecIo dn_cb_LittleMessage1_encode(dn_r_LittleMessage1 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset) {
 if(!dn_enter()){return (dn_r_CodecIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecIo){0};}
 dn_r_CodecIo dn_result=dn_f_LittleMessage1_encode(v_message,v_output,v_offset);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_LittleMessage1_equal(dn_r_LittleMessage1 const * v_left,dn_r_LittleMessage1 const * v_right) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
{ bool dn_tmp_3=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_left)->f_inner));
dn_r_CodecMessage const * dn_tmp_2=(&((v_right)->f_inner));
dn_f_codec_equal(dn_tmp_0,dn_tmp_1,dn_tmp_2); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static bool dn_cb_LittleMessage1_equal(dn_r_LittleMessage1 const * v_left,dn_r_LittleMessage1 const * v_right) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_LittleMessage1_equal(v_left,v_right);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_LittleMessage1_field_1_add(dn_r_LittleMessage1 * v_message,uint8_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(1ULL));
uint64_t dn_tmp_3=({ uint8_t dn_tmp_4=v_value;
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_4; });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_LittleMessage1_field_1_add(dn_r_LittleMessage1 * v_message,uint8_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_LittleMessage1_field_1_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_LittleMessage1Field1 dn_f_LittleMessage1_field_1_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(1ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_LittleMessage1Field1 dn_tmp_7=({ uint8_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_5; });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_LittleMessage1Field1){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_LittleMessage1Field1 dn_cb_LittleMessage1_field_1_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_LittleMessage1Field1){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_LittleMessage1Field1){0};}
 dn_r_LittleMessage1Field1 dn_result=dn_f_LittleMessage1_field_1_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_LittleMessage1_field_1_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U8 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(1ULL));
size_t dn_tmp_3=({ dn_t_slice0_U8 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_U8 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(1ULL));
uint32_t dn_tmp_16=((uint32_t)(1ULL));
uint64_t dn_tmp_17=({ uint8_t dn_tmp_18=(*({ dn_t_slice0_U8 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_18; });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_LittleMessage1_field_1_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U8 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_LittleMessage1_field_1_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_LittleMessage1_field_2_add(dn_r_LittleMessage1 * v_message,uint16_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(2ULL));
uint64_t dn_tmp_3=({ uint16_t dn_tmp_4=v_value;
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_4; });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_LittleMessage1_field_2_add(dn_r_LittleMessage1 * v_message,uint16_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_LittleMessage1_field_2_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_LittleMessage1Field2 dn_f_LittleMessage1_field_2_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(2ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_LittleMessage1Field2 dn_tmp_7=({ uint16_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<16)-1))dn_fail("numeric conversion out of range");(uint16_t)dn_tmp_5; });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_LittleMessage1Field2){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_LittleMessage1Field2 dn_cb_LittleMessage1_field_2_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_LittleMessage1Field2){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_LittleMessage1Field2){0};}
 dn_r_LittleMessage1Field2 dn_result=dn_f_LittleMessage1_field_2_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_LittleMessage1_field_2_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U16 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(2ULL));
size_t dn_tmp_3=({ dn_t_slice0_U16 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_U16 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(2ULL));
uint32_t dn_tmp_16=((uint32_t)(2ULL));
uint64_t dn_tmp_17=({ uint16_t dn_tmp_18=(*({ dn_t_slice0_U16 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_18; });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_LittleMessage1_field_2_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U16 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_LittleMessage1_field_2_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_LittleMessage1_field_3_add(dn_r_LittleMessage1 * v_message,uint32_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(3ULL));
uint64_t dn_tmp_3=({ uint32_t dn_tmp_4=v_value;
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_4; });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_LittleMessage1_field_3_add(dn_r_LittleMessage1 * v_message,uint32_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_LittleMessage1_field_3_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_LittleMessage1Field3 dn_f_LittleMessage1_field_3_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(3ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_LittleMessage1Field3 dn_tmp_7=({ uint32_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_5; });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_LittleMessage1Field3){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_LittleMessage1Field3 dn_cb_LittleMessage1_field_3_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_LittleMessage1Field3){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_LittleMessage1Field3){0};}
 dn_r_LittleMessage1Field3 dn_result=dn_f_LittleMessage1_field_3_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_LittleMessage1_field_3_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U32 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(3ULL));
size_t dn_tmp_3=({ dn_t_slice0_U32 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_U32 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(3ULL));
uint32_t dn_tmp_16=((uint32_t)(3ULL));
uint64_t dn_tmp_17=({ uint32_t dn_tmp_18=(*({ dn_t_slice0_U32 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_18; });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_LittleMessage1_field_3_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U32 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_LittleMessage1_field_3_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_LittleMessage1_field_4_add(dn_r_LittleMessage1 * v_message,uint64_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(4ULL));
uint64_t dn_tmp_3=({ uint64_t dn_tmp_4=v_value;
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_4; });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_LittleMessage1_field_4_add(dn_r_LittleMessage1 * v_message,uint64_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_LittleMessage1_field_4_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_LittleMessage1Field4 dn_f_LittleMessage1_field_4_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(4ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_LittleMessage1Field4 dn_tmp_7=({ uint64_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_5; });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_LittleMessage1Field4){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_LittleMessage1Field4 dn_cb_LittleMessage1_field_4_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_LittleMessage1Field4){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_LittleMessage1Field4){0};}
 dn_r_LittleMessage1Field4 dn_result=dn_f_LittleMessage1_field_4_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_LittleMessage1_field_4_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U64 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(4ULL));
size_t dn_tmp_3=({ dn_t_slice0_U64 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_U64 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(4ULL));
uint32_t dn_tmp_16=((uint32_t)(4ULL));
uint64_t dn_tmp_17=({ uint64_t dn_tmp_18=(*({ dn_t_slice0_U64 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_18; });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_LittleMessage1_field_4_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U64 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_LittleMessage1_field_4_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_LittleMessage1_field_5_add(dn_r_LittleMessage1 * v_message,bool v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
uint64_t v_scalar=((uint64_t)(0ULL));
if(v_value){
{ uint64_t dn_tmp_1=((uint64_t)(1ULL));uint64_t *dn_tmp_0=&(v_scalar);*dn_tmp_0=dn_tmp_1; }
}
else{
}
{ dn_r_CodecError dn_tmp_6=({ dn_r_CodecPlan const * dn_tmp_2=(&(v_plan));
dn_r_CodecMessage * dn_tmp_3=(&((v_message)->f_inner));
uint32_t dn_tmp_4=((uint32_t)(5ULL));
uint64_t dn_tmp_5=v_scalar;
dn_f_codec_add_scalar(dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5); });return dn_tmp_6; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_LittleMessage1_field_5_add(dn_r_LittleMessage1 * v_message,bool v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_LittleMessage1_field_5_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_LittleMessage1Field5 dn_f_LittleMessage1_field_5_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(5ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_LittleMessage1Field5 dn_tmp_8=({ bool dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
uint64_t dn_tmp_6=((uint64_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); });
dn_r_CodecError dn_tmp_7=(v_value).f_error;
((dn_r_LittleMessage1Field5){dn_tmp_4,dn_tmp_7}); });return dn_tmp_8; }
}

dn_fail("missing native return");
}
static dn_r_LittleMessage1Field5 dn_cb_LittleMessage1_field_5_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_LittleMessage1Field5){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_LittleMessage1Field5){0};}
 dn_r_LittleMessage1Field5 dn_result=dn_f_LittleMessage1_field_5_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_LittleMessage1_field_5_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_b v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(5ULL));
size_t dn_tmp_3=({ dn_t_slice0_b dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_b dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
uint64_t v_scalar=((uint64_t)(0ULL));
if((*({ dn_t_slice0_b *dn_tmp_11=&(v_values);size_t dn_tmp_12=v_index;&dn_tmp_11->data[dn_tmp_12]; }))){
{ uint64_t dn_tmp_14=((uint64_t)(1ULL));uint64_t *dn_tmp_13=&(v_scalar);*dn_tmp_13=dn_tmp_14; }
}
else{
}
{ dn_r_CodecValue dn_tmp_25=({ uint32_t dn_tmp_19=((uint32_t)(5ULL));
uint32_t dn_tmp_20=((uint32_t)(5ULL));
uint64_t dn_tmp_21=v_scalar;
size_t dn_tmp_22=((size_t)(0ULL));
size_t dn_tmp_23=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_19,dn_tmp_20,dn_tmp_21,dn_tmp_22,dn_tmp_23}); });dn_r_CodecValue *dn_tmp_24=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_15=&(((v_message)->f_inner).f_values);size_t dn_tmp_16=({ size_t dn_tmp_17=(v_reserved).f_start;
size_t dn_tmp_18=v_index;
dn_USize_add(dn_tmp_17,dn_tmp_18); });&dn_tmp_15->data[dn_index(dn_tmp_16,dn_tmp_15->len)]; })));*dn_tmp_24=dn_tmp_25; }
}
 }
{ dn_r_CodecError dn_tmp_29=({ int32_t dn_tmp_26=((int32_t)(0ULL));
uint32_t dn_tmp_27=((uint32_t)(0ULL));
size_t dn_tmp_28=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_26,dn_tmp_27,dn_tmp_28}); });return dn_tmp_29; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_LittleMessage1_field_5_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_b v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_LittleMessage1_field_5_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_LittleMessage1_field_6_add(dn_r_LittleMessage1 * v_message,dn_t_slice0_U8 v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
{ dn_r_CodecError dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(6ULL));
dn_t_slice0_U8 dn_tmp_3=v_value;
dn_f_codec_add_bytes(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_LittleMessage1_field_6_add(dn_r_LittleMessage1 * v_message,dn_t_slice0_U8 v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_LittleMessage1_field_6_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecSpan dn_f_LittleMessage1_field_6_span(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
{ dn_r_CodecSpan dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(6ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_span(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecSpan dn_cb_LittleMessage1_field_6_span(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_CodecSpan){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecSpan){0};}
 dn_r_CodecSpan dn_result=dn_f_LittleMessage1_field_6_span(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_slice0_U8 dn_f_LittleMessage1_field_6_view(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecSpan v_span=({ dn_r_LittleMessage1 const * dn_tmp_0=v_message;
size_t dn_tmp_1=v_occurrence;
dn_f_LittleMessage1_field_6_span(dn_tmp_0,dn_tmp_1); });
{ dn_t_slice0_U8 dn_tmp_4=({ dn_r_CodecMessage const * dn_tmp_2=(&((v_message)->f_inner));
dn_r_CodecSpan dn_tmp_3=v_span;
dn_f_codec_view(dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_t_slice0_U8 dn_cb_LittleMessage1_field_6_view(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_t_slice0_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_slice0_U8){0};}
 dn_t_slice0_U8 dn_result=dn_f_LittleMessage1_field_6_view(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_LittleMessage1_field_7_add(dn_r_LittleMessage1 * v_message,dn_t_slice0_U8 v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
{ dn_r_CodecError dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(7ULL));
dn_t_slice0_U8 dn_tmp_3=v_value;
dn_f_codec_add_bytes(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_LittleMessage1_field_7_add(dn_r_LittleMessage1 * v_message,dn_t_slice0_U8 v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_LittleMessage1_field_7_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecSpan dn_f_LittleMessage1_field_7_span(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
{ dn_r_CodecSpan dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(7ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_span(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecSpan dn_cb_LittleMessage1_field_7_span(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_CodecSpan){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecSpan){0};}
 dn_r_CodecSpan dn_result=dn_f_LittleMessage1_field_7_span(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_slice0_U8 dn_f_LittleMessage1_field_7_view(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecSpan v_span=({ dn_r_LittleMessage1 const * dn_tmp_0=v_message;
size_t dn_tmp_1=v_occurrence;
dn_f_LittleMessage1_field_7_span(dn_tmp_0,dn_tmp_1); });
{ dn_t_slice0_U8 dn_tmp_4=({ dn_r_CodecMessage const * dn_tmp_2=(&((v_message)->f_inner));
dn_r_CodecSpan dn_tmp_3=v_span;
dn_f_codec_view(dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_t_slice0_U8 dn_cb_LittleMessage1_field_7_view(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_t_slice0_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_slice0_U8){0};}
 dn_t_slice0_U8 dn_result=dn_f_LittleMessage1_field_7_view(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_LittleMessage1_field_8_add(dn_r_LittleMessage1 * v_message,uint64_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(8ULL));
uint64_t dn_tmp_3=({ uint64_t dn_tmp_4=v_value;
if((unsigned __int128)dn_tmp_4>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_4; });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_LittleMessage1_field_8_add(dn_r_LittleMessage1 * v_message,uint64_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_LittleMessage1_field_8_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_LittleMessage1Field8 dn_f_LittleMessage1_field_8_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(8ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_LittleMessage1Field8 dn_tmp_7=({ uint64_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_5; });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_LittleMessage1Field8){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_LittleMessage1Field8 dn_cb_LittleMessage1_field_8_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_LittleMessage1Field8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_LittleMessage1Field8){0};}
 dn_r_LittleMessage1Field8 dn_result=dn_f_LittleMessage1_field_8_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_LittleMessage1_field_8_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U64 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(8ULL));
size_t dn_tmp_3=({ dn_t_slice0_U64 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_U64 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(8ULL));
uint32_t dn_tmp_16=((uint32_t)(8ULL));
uint64_t dn_tmp_17=({ uint64_t dn_tmp_18=(*({ dn_t_slice0_U64 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_18; });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_LittleMessage1_field_8_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_U64 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_LittleMessage1_field_8_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_LittleMessage1_field_9_add(dn_r_LittleMessage1 * v_message,int64_t v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
{ dn_r_CodecError dn_tmp_5=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(9ULL));
uint64_t dn_tmp_3=({ int64_t dn_tmp_4=v_value;
dn_f_zigzag_encode(dn_tmp_4); });
dn_f_codec_add_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_LittleMessage1_field_9_add(dn_r_LittleMessage1 * v_message,int64_t v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_LittleMessage1_field_9_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_LittleMessage1Field9 dn_f_LittleMessage1_field_9_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
dn_r_CodecScalar v_value=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(9ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_scalar(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
{ dn_r_LittleMessage1Field9 dn_tmp_7=({ int64_t dn_tmp_4=({ uint64_t dn_tmp_5=(v_value).f_value;
dn_f_zigzag_decode(dn_tmp_5); });
dn_r_CodecError dn_tmp_6=(v_value).f_error;
((dn_r_LittleMessage1Field9){dn_tmp_4,dn_tmp_6}); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static dn_r_LittleMessage1Field9 dn_cb_LittleMessage1_field_9_get(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_LittleMessage1Field9){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_LittleMessage1Field9){0};}
 dn_r_LittleMessage1Field9 dn_result=dn_f_LittleMessage1_field_9_get(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_LittleMessage1_field_9_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_I64 v_values) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
dn_r_CodecReservation v_reserved=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(9ULL));
size_t dn_tmp_3=({ dn_t_slice0_I64 dn_tmp_4=v_values;
(dn_tmp_4).len; });
dn_f_codec_reserve_scalars(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
if(({ int32_t dn_tmp_5=((v_reserved).f_error).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 != dn_tmp_6); })){
{ dn_r_CodecError dn_tmp_7=(v_reserved).f_error;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_10=((size_t)(0ULL));size_t dn_tmp_9=({ dn_t_slice0_I64 dn_tmp_8=v_values;
(dn_tmp_8).len; });for(size_t v_index=dn_tmp_10;v_index<dn_tmp_9;v_index++){
{ dn_r_CodecValue dn_tmp_24=({ uint32_t dn_tmp_15=((uint32_t)(9ULL));
uint32_t dn_tmp_16=((uint32_t)(9ULL));
uint64_t dn_tmp_17=({ int64_t dn_tmp_18=(*({ dn_t_slice0_I64 *dn_tmp_19=&(v_values);size_t dn_tmp_20=v_index;&dn_tmp_19->data[dn_tmp_20]; }));
dn_f_zigzag_encode(dn_tmp_18); });
size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=((size_t)(0ULL));
((dn_r_CodecValue){dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_21,dn_tmp_22}); });dn_r_CodecValue *dn_tmp_23=&((*({ dn_t_buf_r10_CodecValue volatile *dn_tmp_11=&(((v_message)->f_inner).f_values);size_t dn_tmp_12=({ size_t dn_tmp_13=(v_reserved).f_start;
size_t dn_tmp_14=v_index;
dn_USize_add(dn_tmp_13,dn_tmp_14); });&dn_tmp_11->data[dn_index(dn_tmp_12,dn_tmp_11->len)]; })));*dn_tmp_23=dn_tmp_24; }
}
 }
{ dn_r_CodecError dn_tmp_28=({ int32_t dn_tmp_25=((int32_t)(0ULL));
uint32_t dn_tmp_26=((uint32_t)(0ULL));
size_t dn_tmp_27=((size_t)(0ULL));
((dn_r_CodecError){dn_tmp_25,dn_tmp_26,dn_tmp_27}); });return dn_tmp_28; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_LittleMessage1_field_9_add_many(dn_r_LittleMessage1 * v_message,dn_t_slice0_I64 v_values) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_LittleMessage1_field_9_add_many(v_message,v_values);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecError dn_f_LittleMessage1_field_11_add(dn_r_LittleMessage1 * v_message,dn_t_slice0_U8 v_value) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
{ dn_r_CodecError dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(11ULL));
dn_t_slice0_U8 dn_tmp_3=v_value;
dn_f_codec_add_bytes(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecError dn_cb_LittleMessage1_field_11_add(dn_r_LittleMessage1 * v_message,dn_t_slice0_U8 v_value) {
 if(!dn_enter()){return (dn_r_CodecError){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecError){0};}
 dn_r_CodecError dn_result=dn_f_LittleMessage1_field_11_add(v_message,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecSpan dn_f_LittleMessage1_field_11_span(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
{ dn_r_CodecSpan dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
uint32_t dn_tmp_2=((uint32_t)(11ULL));
size_t dn_tmp_3=v_occurrence;
dn_f_codec_get_span(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecSpan dn_cb_LittleMessage1_field_11_span(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_r_CodecSpan){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecSpan){0};}
 dn_r_CodecSpan dn_result=dn_f_LittleMessage1_field_11_span(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_slice0_U8 dn_f_LittleMessage1_field_11_view(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
{
dn_r_CodecSpan v_span=({ dn_r_LittleMessage1 const * dn_tmp_0=v_message;
size_t dn_tmp_1=v_occurrence;
dn_f_LittleMessage1_field_11_span(dn_tmp_0,dn_tmp_1); });
{ dn_t_slice0_U8 dn_tmp_4=({ dn_r_CodecMessage const * dn_tmp_2=(&((v_message)->f_inner));
dn_r_CodecSpan dn_tmp_3=v_span;
dn_f_codec_view(dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_t_slice0_U8 dn_cb_LittleMessage1_field_11_view(dn_r_LittleMessage1 const * v_message,size_t v_occurrence) {
 if(!dn_enter()){return (dn_t_slice0_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_slice0_U8){0};}
 dn_t_slice0_U8 dn_result=dn_f_LittleMessage1_field_11_view(v_message,v_occurrence);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecPlan dn_f_Little_plan_2(void) {
{
dn_r_CodecPlan v_plan=({ ((dn_r_CodecPlan){0}); });
{ uint32_t dn_tmp_1=((uint32_t)(2ULL));uint32_t *dn_tmp_0=&((v_plan).f_message);*dn_tmp_0=dn_tmp_1; }
{ dn_r_WireFormat dn_tmp_7=({ uint32_t dn_tmp_2=((uint32_t)(1ULL));
uint32_t dn_tmp_3=((uint32_t)(2ULL));
uint32_t dn_tmp_4=((uint32_t)(0ULL));
uint32_t dn_tmp_5=((uint32_t)(0ULL));
((dn_r_WireFormat){dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5}); });dn_r_WireFormat *dn_tmp_6=&((v_plan).f_wire);*dn_tmp_6=dn_tmp_7; }
{ uint32_t dn_tmp_9=((uint32_t)(0ULL));uint32_t *dn_tmp_8=&((v_plan).f_minor);*dn_tmp_8=dn_tmp_9; }
{ uint32_t dn_tmp_11=((uint32_t)(0ULL));uint32_t *dn_tmp_10=&((v_plan).f_field_count);*dn_tmp_10=dn_tmp_11; }
{ uint32_t dn_tmp_13=((uint32_t)(0ULL));uint32_t *dn_tmp_12=&((v_plan).f_maximum_values);*dn_tmp_12=dn_tmp_13; }
{ size_t dn_tmp_15=((size_t)(4ULL));size_t *dn_tmp_14=&((v_plan).f_maximum_wire);*dn_tmp_14=dn_tmp_15; }
{ uint32_t dn_tmp_17=((uint32_t)(0ULL));uint32_t *dn_tmp_16=&((v_plan).f_maximum_unknown_fields);*dn_tmp_16=dn_tmp_17; }
{ size_t dn_tmp_19=((size_t)(0ULL));size_t *dn_tmp_18=&((v_plan).f_maximum_unknown_bytes);*dn_tmp_18=dn_tmp_19; }
{ size_t dn_tmp_21=((size_t)(8388608ULL));size_t *dn_tmp_20=&((v_plan).f_storage_budget);*dn_tmp_20=dn_tmp_21; }
{ dn_r_CodecPlan dn_tmp_22=v_plan;return dn_tmp_22; }
}

dn_fail("missing native return");
}
static dn_r_CodecPlan dn_cb_Little_plan_2(void) {
 if(!dn_enter()){return (dn_r_CodecPlan){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecPlan){0};}
 dn_r_CodecPlan dn_result=dn_f_Little_plan_2();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_LittleMessage2 dn_f_LittleMessage2_new(void) {
{
{ dn_r_LittleMessage2 dn_tmp_2=({ dn_r_CodecMessage dn_tmp_0=({ uint32_t dn_tmp_1=((uint32_t)(2ULL));
dn_f_codec_message(dn_tmp_1); });
dn_adopt_r12_CodecMessage(&dn_tmp_0);
((dn_r_LittleMessage2){dn_tmp_0}); });dn_adopt_r14_LittleMessage2(&dn_tmp_2);return dn_tmp_2; }
}

dn_fail("missing native return");
}
static dn_r_LittleMessage2 dn_cb_LittleMessage2_new(void) {
 if(!dn_enter()){return (dn_r_LittleMessage2){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_LittleMessage2){0};}
 dn_r_LittleMessage2 dn_result=dn_f_LittleMessage2_new();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_LittleMessage2Decoded dn_f_LittleMessage2_decode(dn_t_slice0_U8 v_input) {
{
dn_r_CodecDecoded volatile v_decoded={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r12_CodecDecoded(&v_decoded);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_2(); });
v_decoded=({ dn_r_CodecPlan const * dn_tmp_2=(&(v_plan));
dn_t_slice0_U8 dn_tmp_3=v_input;
dn_f_codec_decode(dn_tmp_2,dn_tmp_3); });
{ dn_r_LittleMessage2Decoded dn_tmp_8=({ dn_r_LittleMessage2 dn_tmp_4=({ dn_r_CodecMessage dn_tmp_5=({ dn_r_CodecMessage dn_tmp_6=(v_decoded).f_value;(v_decoded).f_value=(dn_r_CodecMessage){0};dn_adopt_r12_CodecMessage(&dn_tmp_6);dn_tmp_6; });
dn_adopt_r12_CodecMessage(&dn_tmp_5);
((dn_r_LittleMessage2){dn_tmp_5}); });
dn_adopt_r14_LittleMessage2(&dn_tmp_4);
dn_r_CodecError dn_tmp_7=(v_decoded).f_error;
((dn_r_LittleMessage2Decoded){dn_tmp_4,dn_tmp_7}); });dn_adopt_r21_LittleMessage2Decoded(&dn_tmp_8);dn_drop_r12_CodecDecoded(&v_decoded);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_8; }
dn_drop_r12_CodecDecoded(&v_decoded);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_r_LittleMessage2Decoded dn_cb_LittleMessage2_decode(dn_t_slice0_U8 v_input) {
 if(!dn_enter()){return (dn_r_LittleMessage2Decoded){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_LittleMessage2Decoded){0};}
 dn_r_LittleMessage2Decoded dn_result=dn_f_LittleMessage2_decode(v_input);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecIo dn_f_LittleMessage2_measure(dn_r_LittleMessage2 const * v_message) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_2(); });
{ dn_r_CodecIo dn_tmp_2=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
dn_f_codec_measure(dn_tmp_0,dn_tmp_1); });return dn_tmp_2; }
}

dn_fail("missing native return");
}
static dn_r_CodecIo dn_cb_LittleMessage2_measure(dn_r_LittleMessage2 const * v_message) {
 if(!dn_enter()){return (dn_r_CodecIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecIo){0};}
 dn_r_CodecIo dn_result=dn_f_LittleMessage2_measure(v_message);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecIo dn_f_LittleMessage2_encode(dn_r_LittleMessage2 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_2(); });
{ dn_r_CodecIo dn_tmp_4=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_message)->f_inner));
dn_t_buf_U8 * dn_tmp_2=v_output;
size_t dn_tmp_3=v_offset;
dn_f_codec_encode(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });return dn_tmp_4; }
}

dn_fail("missing native return");
}
static dn_r_CodecIo dn_cb_LittleMessage2_encode(dn_r_LittleMessage2 const * v_message,dn_t_buf_U8 * v_output,size_t v_offset) {
 if(!dn_enter()){return (dn_r_CodecIo){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecIo){0};}
 dn_r_CodecIo dn_result=dn_f_LittleMessage2_encode(v_message,v_output,v_offset);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_LittleMessage2_equal(dn_r_LittleMessage2 const * v_left,dn_r_LittleMessage2 const * v_right) {
{
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_2(); });
{ bool dn_tmp_3=({ dn_r_CodecPlan const * dn_tmp_0=(&(v_plan));
dn_r_CodecMessage const * dn_tmp_1=(&((v_left)->f_inner));
dn_r_CodecMessage const * dn_tmp_2=(&((v_right)->f_inner));
dn_f_codec_equal(dn_tmp_0,dn_tmp_1,dn_tmp_2); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static bool dn_cb_LittleMessage2_equal(dn_r_LittleMessage2 const * v_left,dn_r_LittleMessage2 const * v_right) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_LittleMessage2_equal(v_left,v_right);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_CodecDecoded dn_f_Little_decode(dn_t_slice0_U8 v_input) {
{
dn_r_ProtocolValue v_id=({ dn_t_slice0_U8 dn_tmp_0=v_input;
size_t dn_tmp_1=((size_t)(0ULL));
size_t dn_tmp_2=((size_t)(4ULL));
dn_f_net_read_be(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
if(({ int32_t dn_tmp_3=(v_id).f_status;
int32_t dn_tmp_4=((int32_t)(0ULL));
(dn_tmp_3 != dn_tmp_4); })){
{ dn_r_CodecDecoded dn_tmp_8=({ int32_t dn_tmp_5=(v_id).f_status;
uint32_t dn_tmp_6=((uint32_t)(0ULL));
size_t dn_tmp_7=((size_t)(0ULL));
dn_f_codec_decode_error(dn_tmp_5,dn_tmp_6,dn_tmp_7); });dn_adopt_r12_CodecDecoded(&dn_tmp_8);return dn_tmp_8; }
}
else{
}
if(({ uint64_t dn_tmp_9=(v_id).f_value;
uint64_t dn_tmp_10=({ uint64_t dn_tmp_11=((uint64_t)(1ULL));
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_11; });
(dn_tmp_9 == dn_tmp_10); })){
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_1(); });
{ dn_r_CodecDecoded dn_tmp_14=({ dn_r_CodecPlan const * dn_tmp_12=(&(v_plan));
dn_t_slice0_U8 dn_tmp_13=v_input;
dn_f_codec_decode(dn_tmp_12,dn_tmp_13); });dn_adopt_r12_CodecDecoded(&dn_tmp_14);return dn_tmp_14; }
}
else{
}
if(({ uint64_t dn_tmp_15=(v_id).f_value;
uint64_t dn_tmp_16=({ uint64_t dn_tmp_17=((uint64_t)(2ULL));
if((unsigned __int128)dn_tmp_17>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_17; });
(dn_tmp_15 == dn_tmp_16); })){
dn_r_CodecPlan v_plan=({ dn_f_Little_plan_2(); });
{ dn_r_CodecDecoded dn_tmp_20=({ dn_r_CodecPlan const * dn_tmp_18=(&(v_plan));
dn_t_slice0_U8 dn_tmp_19=v_input;
dn_f_codec_decode(dn_tmp_18,dn_tmp_19); });dn_adopt_r12_CodecDecoded(&dn_tmp_20);return dn_tmp_20; }
}
else{
}
{ dn_r_CodecDecoded dn_tmp_24=({ int32_t dn_tmp_21=((int32_t)(11ULL));
uint32_t dn_tmp_22=((uint32_t)(0ULL));
size_t dn_tmp_23=((size_t)(0ULL));
dn_f_codec_decode_error(dn_tmp_21,dn_tmp_22,dn_tmp_23); });dn_adopt_r12_CodecDecoded(&dn_tmp_24);return dn_tmp_24; }
}

dn_fail("missing native return");
}
static dn_r_CodecDecoded dn_cb_Little_decode(dn_t_slice0_U8 v_input) {
 if(!dn_enter()){return (dn_r_CodecDecoded){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_CodecDecoded){0};}
 dn_r_CodecDecoded dn_result=dn_f_Little_decode(v_input);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_EchoMessage1 dn_f_make_echo(void) {
{
dn_r_EchoMessage1 volatile v_message={0};
dn_t_buf_U32 volatile v_counts={0};
dn_t_buf_U8 volatile v_bytes={0};
dn_t_buf_U64 volatile v_unsigned={0};
dn_t_buf_I64 volatile v_signed={0};
dn_t_buf_U8 volatile v_unknown={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_unknown);
dn_drop_buf_I64(&v_signed);
dn_drop_buf_U64(&v_unsigned);
dn_drop_buf_U8(&v_bytes);
dn_drop_buf_U32(&v_counts);
dn_drop_r12_EchoMessage1(&v_message);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_message=({ dn_f_EchoMessage1_new(); });
dn_r_CodecError v_a=({ dn_r_EchoMessage1 * dn_tmp_2=(&(v_message));
uint8_t dn_tmp_3=((uint8_t)(255ULL));
dn_f_EchoMessage1_field_1_add(dn_tmp_2,dn_tmp_3); });
(void)(({ bool dn_tmp_4=({ int32_t dn_tmp_5=(v_a).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 == dn_tmp_6); });
(dn_assert(dn_tmp_4),0); }));
v_counts=({ size_t dn_tmp_7=((size_t)(3ULL));
dn_new_buf_U32(dn_tmp_7); });
{ uint32_t dn_tmp_11=((uint32_t)(0ULL));uint32_t *dn_tmp_10=&((*({ dn_t_buf_U32 volatile *dn_tmp_8=&(v_counts);size_t dn_tmp_9=((size_t)(0ULL));&dn_tmp_8->data[dn_index(dn_tmp_9,dn_tmp_8->len)]; })));*dn_tmp_10=dn_tmp_11; }
{ uint32_t dn_tmp_15=((uint32_t)(4294967295ULL));uint32_t *dn_tmp_14=&((*({ dn_t_buf_U32 volatile *dn_tmp_12=&(v_counts);size_t dn_tmp_13=((size_t)(1ULL));&dn_tmp_12->data[dn_index(dn_tmp_13,dn_tmp_12->len)]; })));*dn_tmp_14=dn_tmp_15; }
{ uint32_t dn_tmp_19=((uint32_t)(42ULL));uint32_t *dn_tmp_18=&((*({ dn_t_buf_U32 volatile *dn_tmp_16=&(v_counts);size_t dn_tmp_17=((size_t)(2ULL));&dn_tmp_16->data[dn_index(dn_tmp_17,dn_tmp_16->len)]; })));*dn_tmp_18=dn_tmp_19; }
dn_r_CodecError v_b=({ dn_r_EchoMessage1 * dn_tmp_20=(&(v_message));
dn_t_slice0_U32 dn_tmp_21=({ dn_t_buf_U32 const * dn_tmp_22=(&(v_counts));
size_t dn_tmp_23=((size_t)(0ULL));
size_t dn_tmp_24=((size_t)(3ULL));
({ dn_range(dn_tmp_23,dn_tmp_24,(*(dn_tmp_22)).len);(dn_t_slice0_U32){(*(dn_tmp_22)).data?(*(dn_tmp_22)).data+dn_tmp_23:NULL,dn_tmp_24}; }); });
dn_f_EchoMessage1_field_3_add_many(dn_tmp_20,dn_tmp_21); });
dn_r_CodecError v_c=({ dn_r_EchoMessage1 * dn_tmp_25=(&(v_message));
uint64_t dn_tmp_26=({ uint64_t dn_tmp_27=((uint64_t)(18446744073709551615ULL));
if((unsigned __int128)dn_tmp_27>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_27; });
dn_f_EchoMessage1_field_4_add(dn_tmp_25,dn_tmp_26); });
dn_r_CodecError v_d=({ dn_r_EchoMessage1 * dn_tmp_28=(&(v_message));
bool dn_tmp_29=1;
dn_f_EchoMessage1_field_5_add(dn_tmp_28,dn_tmp_29); });
(void)(({ bool dn_tmp_30=((({ int32_t dn_tmp_31=(v_b).f_kind;
int32_t dn_tmp_32=((int32_t)(0ULL));
(dn_tmp_31 == dn_tmp_32); }) && ({ int32_t dn_tmp_33=(v_c).f_kind;
int32_t dn_tmp_34=((int32_t)(0ULL));
(dn_tmp_33 == dn_tmp_34); })) && ({ int32_t dn_tmp_35=(v_d).f_kind;
int32_t dn_tmp_36=((int32_t)(0ULL));
(dn_tmp_35 == dn_tmp_36); }));
(dn_assert(dn_tmp_30),0); }));
v_bytes=({ size_t dn_tmp_37=((size_t)(4ULL));
dn_new_buf_U8(dn_tmp_37); });
{ uint8_t dn_tmp_41=((uint8_t)(0ULL));uint8_t *dn_tmp_40=&((*({ dn_t_buf_U8 volatile *dn_tmp_38=&(v_bytes);size_t dn_tmp_39=((size_t)(0ULL));&dn_tmp_38->data[dn_index(dn_tmp_39,dn_tmp_38->len)]; })));*dn_tmp_40=dn_tmp_41; }
{ uint8_t dn_tmp_45=((uint8_t)(255ULL));uint8_t *dn_tmp_44=&((*({ dn_t_buf_U8 volatile *dn_tmp_42=&(v_bytes);size_t dn_tmp_43=((size_t)(1ULL));&dn_tmp_42->data[dn_index(dn_tmp_43,dn_tmp_42->len)]; })));*dn_tmp_44=dn_tmp_45; }
{ uint8_t dn_tmp_49=((uint8_t)(128ULL));uint8_t *dn_tmp_48=&((*({ dn_t_buf_U8 volatile *dn_tmp_46=&(v_bytes);size_t dn_tmp_47=((size_t)(2ULL));&dn_tmp_46->data[dn_index(dn_tmp_47,dn_tmp_46->len)]; })));*dn_tmp_48=dn_tmp_49; }
{ uint8_t dn_tmp_53=((uint8_t)(42ULL));uint8_t *dn_tmp_52=&((*({ dn_t_buf_U8 volatile *dn_tmp_50=&(v_bytes);size_t dn_tmp_51=((size_t)(3ULL));&dn_tmp_50->data[dn_index(dn_tmp_51,dn_tmp_50->len)]; })));*dn_tmp_52=dn_tmp_53; }
dn_r_CodecError v_e=({ dn_r_EchoMessage1 * dn_tmp_54=(&(v_message));
dn_t_slice0_U8 dn_tmp_55=({ dn_t_buf_U8 const * dn_tmp_56=(&(v_bytes));
size_t dn_tmp_57=((size_t)(0ULL));
size_t dn_tmp_58=((size_t)(4ULL));
({ dn_range(dn_tmp_57,dn_tmp_58,(*(dn_tmp_56)).len);(dn_t_slice0_U8){(*(dn_tmp_56)).data?(*(dn_tmp_56)).data+dn_tmp_57:NULL,dn_tmp_58}; }); });
dn_f_EchoMessage1_field_6_add(dn_tmp_54,dn_tmp_55); });
dn_r_CodecError v_f=({ dn_r_EchoMessage1 * dn_tmp_59=(&(v_message));
dn_t_slice0_U8 dn_tmp_60=({ dn_t_buf_U8 const * dn_tmp_61=(&(v_bytes));
size_t dn_tmp_62=((size_t)(0ULL));
size_t dn_tmp_63=((size_t)(0ULL));
({ dn_range(dn_tmp_62,dn_tmp_63,(*(dn_tmp_61)).len);(dn_t_slice0_U8){(*(dn_tmp_61)).data?(*(dn_tmp_61)).data+dn_tmp_62:NULL,dn_tmp_63}; }); });
dn_f_EchoMessage1_field_6_add(dn_tmp_59,dn_tmp_60); });
dn_r_CodecError v_g=({ dn_r_EchoMessage1 * dn_tmp_64=(&(v_message));
dn_t_slice0_U8 dn_tmp_65=({ dn_utf8 dn_tmp_66=((dn_utf8){(const uint8_t*)"\320\237\321\200\320\270\320\262\320\265\321\202, DLM", 17});
((dn_t_slice0_U8){dn_tmp_66.data,dn_tmp_66.len}); });
dn_f_EchoMessage1_field_7_add(dn_tmp_64,dn_tmp_65); });
(void)(({ bool dn_tmp_67=((({ int32_t dn_tmp_68=(v_e).f_kind;
int32_t dn_tmp_69=((int32_t)(0ULL));
(dn_tmp_68 == dn_tmp_69); }) && ({ int32_t dn_tmp_70=(v_f).f_kind;
int32_t dn_tmp_71=((int32_t)(0ULL));
(dn_tmp_70 == dn_tmp_71); })) && ({ int32_t dn_tmp_72=(v_g).f_kind;
int32_t dn_tmp_73=((int32_t)(0ULL));
(dn_tmp_72 == dn_tmp_73); }));
(dn_assert(dn_tmp_67),0); }));
v_unsigned=({ size_t dn_tmp_74=((size_t)(4ULL));
dn_new_buf_U64(dn_tmp_74); });
{ uint64_t dn_tmp_78=((uint64_t)(0ULL));uint64_t *dn_tmp_77=&((*({ dn_t_buf_U64 volatile *dn_tmp_75=&(v_unsigned);size_t dn_tmp_76=((size_t)(0ULL));&dn_tmp_75->data[dn_index(dn_tmp_76,dn_tmp_75->len)]; })));*dn_tmp_77=dn_tmp_78; }
{ uint64_t dn_tmp_82=((uint64_t)(127ULL));uint64_t *dn_tmp_81=&((*({ dn_t_buf_U64 volatile *dn_tmp_79=&(v_unsigned);size_t dn_tmp_80=((size_t)(1ULL));&dn_tmp_79->data[dn_index(dn_tmp_80,dn_tmp_79->len)]; })));*dn_tmp_81=dn_tmp_82; }
{ uint64_t dn_tmp_86=((uint64_t)(128ULL));uint64_t *dn_tmp_85=&((*({ dn_t_buf_U64 volatile *dn_tmp_83=&(v_unsigned);size_t dn_tmp_84=((size_t)(2ULL));&dn_tmp_83->data[dn_index(dn_tmp_84,dn_tmp_83->len)]; })));*dn_tmp_85=dn_tmp_86; }
{ uint64_t dn_tmp_90=((uint64_t)(18446744073709551615ULL));uint64_t *dn_tmp_89=&((*({ dn_t_buf_U64 volatile *dn_tmp_87=&(v_unsigned);size_t dn_tmp_88=((size_t)(3ULL));&dn_tmp_87->data[dn_index(dn_tmp_88,dn_tmp_87->len)]; })));*dn_tmp_89=dn_tmp_90; }
v_signed=({ size_t dn_tmp_91=((size_t)(2ULL));
dn_new_buf_I64(dn_tmp_91); });
{ int64_t dn_tmp_96=({ int64_t dn_tmp_94=((int64_t)(-INT64_C(9223372036854775807)-1));
if((__int128)dn_tmp_94<(-((__int128)1<<63))||(__int128)dn_tmp_94>(((__int128)1<<63)-1))dn_fail("numeric conversion out of range");(int64_t)dn_tmp_94; });int64_t *dn_tmp_95=&((*({ dn_t_buf_I64 volatile *dn_tmp_92=&(v_signed);size_t dn_tmp_93=((size_t)(0ULL));&dn_tmp_92->data[dn_index(dn_tmp_93,dn_tmp_92->len)]; })));*dn_tmp_95=dn_tmp_96; }
{ int64_t dn_tmp_101=({ int64_t dn_tmp_99=((int64_t)(9223372036854775807ULL));
if((__int128)dn_tmp_99<(-((__int128)1<<63))||(__int128)dn_tmp_99>(((__int128)1<<63)-1))dn_fail("numeric conversion out of range");(int64_t)dn_tmp_99; });int64_t *dn_tmp_100=&((*({ dn_t_buf_I64 volatile *dn_tmp_97=&(v_signed);size_t dn_tmp_98=((size_t)(1ULL));&dn_tmp_97->data[dn_index(dn_tmp_98,dn_tmp_97->len)]; })));*dn_tmp_100=dn_tmp_101; }
dn_r_CodecError v_h=({ dn_r_EchoMessage1 * dn_tmp_102=(&(v_message));
dn_t_slice0_U64 dn_tmp_103=({ dn_t_buf_U64 const * dn_tmp_104=(&(v_unsigned));
size_t dn_tmp_105=((size_t)(0ULL));
size_t dn_tmp_106=((size_t)(4ULL));
({ dn_range(dn_tmp_105,dn_tmp_106,(*(dn_tmp_104)).len);(dn_t_slice0_U64){(*(dn_tmp_104)).data?(*(dn_tmp_104)).data+dn_tmp_105:NULL,dn_tmp_106}; }); });
dn_f_EchoMessage1_field_8_add_many(dn_tmp_102,dn_tmp_103); });
dn_r_CodecError v_i=({ dn_r_EchoMessage1 * dn_tmp_107=(&(v_message));
dn_t_slice0_I64 dn_tmp_108=({ dn_t_buf_I64 const * dn_tmp_109=(&(v_signed));
size_t dn_tmp_110=((size_t)(0ULL));
size_t dn_tmp_111=((size_t)(2ULL));
({ dn_range(dn_tmp_110,dn_tmp_111,(*(dn_tmp_109)).len);(dn_t_slice0_I64){(*(dn_tmp_109)).data?(*(dn_tmp_109)).data+dn_tmp_110:NULL,dn_tmp_111}; }); });
dn_f_EchoMessage1_field_9_add_many(dn_tmp_107,dn_tmp_108); });
(void)(({ bool dn_tmp_112=(({ int32_t dn_tmp_113=(v_h).f_kind;
int32_t dn_tmp_114=((int32_t)(0ULL));
(dn_tmp_113 == dn_tmp_114); }) && ({ int32_t dn_tmp_115=(v_i).f_kind;
int32_t dn_tmp_116=((int32_t)(0ULL));
(dn_tmp_115 == dn_tmp_116); }));
(dn_assert(dn_tmp_112),0); }));
dn_r_CodecError v_newer=({ dn_r_EchoMessage1 * dn_tmp_117=(&(v_message));
uint16_t dn_tmp_118=((uint16_t)(65535ULL));
dn_f_EchoMessage1_field_10_add(dn_tmp_117,dn_tmp_118); });
(void)(({ bool dn_tmp_119=({ int32_t dn_tmp_120=(v_newer).f_kind;
int32_t dn_tmp_121=((int32_t)(0ULL));
(dn_tmp_120 == dn_tmp_121); });
(dn_assert(dn_tmp_119),0); }));
v_unknown=({ size_t dn_tmp_122=((size_t)(11ULL));
dn_new_buf_U8(dn_tmp_122); });
(void)(({ dn_t_buf_U8 * dn_tmp_123=(&(v_unknown));
size_t dn_tmp_124=((size_t)(0ULL));
uint32_t dn_tmp_125=((uint32_t)(99ULL));
(dn_binary_write((*(dn_tmp_123)).data,(*(dn_tmp_123)).len,dn_tmp_124,4,(uint64_t)dn_tmp_125,0),0); }));
(void)(({ dn_t_buf_U8 * dn_tmp_126=(&(v_unknown));
size_t dn_tmp_127=((size_t)(4ULL));
uint32_t dn_tmp_128=((uint32_t)(3ULL));
(dn_binary_write((*(dn_tmp_126)).data,(*(dn_tmp_126)).len,dn_tmp_127,4,(uint64_t)dn_tmp_128,0),0); }));
{ uint8_t dn_tmp_132=((uint8_t)(0ULL));uint8_t *dn_tmp_131=&((*({ dn_t_buf_U8 volatile *dn_tmp_129=&(v_unknown);size_t dn_tmp_130=((size_t)(8ULL));&dn_tmp_129->data[dn_index(dn_tmp_130,dn_tmp_129->len)]; })));*dn_tmp_131=dn_tmp_132; }
{ uint8_t dn_tmp_136=((uint8_t)(255ULL));uint8_t *dn_tmp_135=&((*({ dn_t_buf_U8 volatile *dn_tmp_133=&(v_unknown);size_t dn_tmp_134=((size_t)(9ULL));&dn_tmp_133->data[dn_index(dn_tmp_134,dn_tmp_133->len)]; })));*dn_tmp_135=dn_tmp_136; }
{ uint8_t dn_tmp_140=((uint8_t)(128ULL));uint8_t *dn_tmp_139=&((*({ dn_t_buf_U8 volatile *dn_tmp_137=&(v_unknown);size_t dn_tmp_138=((size_t)(10ULL));&dn_tmp_137->data[dn_index(dn_tmp_138,dn_tmp_137->len)]; })));*dn_tmp_139=dn_tmp_140; }
dn_r_CodecError v_preserved=({ dn_r_EchoMessage1 * dn_tmp_141=(&(v_message));
dn_t_slice0_U8 dn_tmp_142=({ dn_t_buf_U8 const * dn_tmp_143=(&(v_unknown));
size_t dn_tmp_144=((size_t)(0ULL));
size_t dn_tmp_145=((size_t)(11ULL));
({ dn_range(dn_tmp_144,dn_tmp_145,(*(dn_tmp_143)).len);(dn_t_slice0_U8){(*(dn_tmp_143)).data?(*(dn_tmp_143)).data+dn_tmp_144:NULL,dn_tmp_145}; }); });
dn_f_EchoMessage1_add_unknown(dn_tmp_141,dn_tmp_142); });
(void)(({ bool dn_tmp_146=({ int32_t dn_tmp_147=(v_preserved).f_kind;
int32_t dn_tmp_148=((int32_t)(0ULL));
(dn_tmp_147 == dn_tmp_148); });
(dn_assert(dn_tmp_146),0); }));
{ dn_r_EchoMessage1 dn_tmp_150=({ dn_r_EchoMessage1 dn_tmp_149=v_message;v_message=(dn_r_EchoMessage1){0};dn_adopt_r12_EchoMessage1(&dn_tmp_149);dn_tmp_149; });dn_adopt_r12_EchoMessage1(&dn_tmp_150);dn_drop_buf_U8(&v_unknown);
dn_drop_buf_I64(&v_signed);
dn_drop_buf_U64(&v_unsigned);
dn_drop_buf_U8(&v_bytes);
dn_drop_buf_U32(&v_counts);
dn_drop_r12_EchoMessage1(&v_message);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_150; }
dn_drop_buf_U8(&v_unknown);
dn_drop_buf_I64(&v_signed);
dn_drop_buf_U64(&v_unsigned);
dn_drop_buf_U8(&v_bytes);
dn_drop_buf_U32(&v_counts);
dn_drop_r12_EchoMessage1(&v_message);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_r_EchoMessage1 dn_cb_make_echo(void) {
 if(!dn_enter()){return (dn_r_EchoMessage1){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_EchoMessage1){0};}
 dn_r_EchoMessage1 dn_result=dn_f_make_echo();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_PosMessage1 dn_f_make_pos(void) {
{
dn_r_PosMessage1 volatile v_message={0};
dn_t_buf_U32 volatile v_counts={0};
dn_t_buf_U8 volatile v_bytes={0};
dn_t_buf_U64 volatile v_unsigned={0};
dn_t_buf_I64 volatile v_signed={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_I64(&v_signed);
dn_drop_buf_U64(&v_unsigned);
dn_drop_buf_U8(&v_bytes);
dn_drop_buf_U32(&v_counts);
dn_drop_r11_PosMessage1(&v_message);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_message=({ dn_f_PosMessage1_new(); });
dn_r_CodecError v_a=({ dn_r_PosMessage1 * dn_tmp_2=(&(v_message));
uint8_t dn_tmp_3=((uint8_t)(255ULL));
dn_f_PosMessage1_field_1_add(dn_tmp_2,dn_tmp_3); });
(void)(({ bool dn_tmp_4=({ int32_t dn_tmp_5=(v_a).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 == dn_tmp_6); });
(dn_assert(dn_tmp_4),0); }));
v_counts=({ size_t dn_tmp_7=((size_t)(3ULL));
dn_new_buf_U32(dn_tmp_7); });
{ uint32_t dn_tmp_11=((uint32_t)(0ULL));uint32_t *dn_tmp_10=&((*({ dn_t_buf_U32 volatile *dn_tmp_8=&(v_counts);size_t dn_tmp_9=((size_t)(0ULL));&dn_tmp_8->data[dn_index(dn_tmp_9,dn_tmp_8->len)]; })));*dn_tmp_10=dn_tmp_11; }
{ uint32_t dn_tmp_15=((uint32_t)(4294967295ULL));uint32_t *dn_tmp_14=&((*({ dn_t_buf_U32 volatile *dn_tmp_12=&(v_counts);size_t dn_tmp_13=((size_t)(1ULL));&dn_tmp_12->data[dn_index(dn_tmp_13,dn_tmp_12->len)]; })));*dn_tmp_14=dn_tmp_15; }
{ uint32_t dn_tmp_19=((uint32_t)(42ULL));uint32_t *dn_tmp_18=&((*({ dn_t_buf_U32 volatile *dn_tmp_16=&(v_counts);size_t dn_tmp_17=((size_t)(2ULL));&dn_tmp_16->data[dn_index(dn_tmp_17,dn_tmp_16->len)]; })));*dn_tmp_18=dn_tmp_19; }
dn_r_CodecError v_b=({ dn_r_PosMessage1 * dn_tmp_20=(&(v_message));
dn_t_slice0_U32 dn_tmp_21=({ dn_t_buf_U32 const * dn_tmp_22=(&(v_counts));
size_t dn_tmp_23=((size_t)(0ULL));
size_t dn_tmp_24=((size_t)(3ULL));
({ dn_range(dn_tmp_23,dn_tmp_24,(*(dn_tmp_22)).len);(dn_t_slice0_U32){(*(dn_tmp_22)).data?(*(dn_tmp_22)).data+dn_tmp_23:NULL,dn_tmp_24}; }); });
dn_f_PosMessage1_field_3_add_many(dn_tmp_20,dn_tmp_21); });
dn_r_CodecError v_c=({ dn_r_PosMessage1 * dn_tmp_25=(&(v_message));
uint64_t dn_tmp_26=({ uint64_t dn_tmp_27=((uint64_t)(18446744073709551615ULL));
if((unsigned __int128)dn_tmp_27>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_27; });
dn_f_PosMessage1_field_4_add(dn_tmp_25,dn_tmp_26); });
dn_r_CodecError v_d=({ dn_r_PosMessage1 * dn_tmp_28=(&(v_message));
bool dn_tmp_29=1;
dn_f_PosMessage1_field_5_add(dn_tmp_28,dn_tmp_29); });
(void)(({ bool dn_tmp_30=((({ int32_t dn_tmp_31=(v_b).f_kind;
int32_t dn_tmp_32=((int32_t)(0ULL));
(dn_tmp_31 == dn_tmp_32); }) && ({ int32_t dn_tmp_33=(v_c).f_kind;
int32_t dn_tmp_34=((int32_t)(0ULL));
(dn_tmp_33 == dn_tmp_34); })) && ({ int32_t dn_tmp_35=(v_d).f_kind;
int32_t dn_tmp_36=((int32_t)(0ULL));
(dn_tmp_35 == dn_tmp_36); }));
(dn_assert(dn_tmp_30),0); }));
v_bytes=({ size_t dn_tmp_37=((size_t)(4ULL));
dn_new_buf_U8(dn_tmp_37); });
{ uint8_t dn_tmp_41=((uint8_t)(0ULL));uint8_t *dn_tmp_40=&((*({ dn_t_buf_U8 volatile *dn_tmp_38=&(v_bytes);size_t dn_tmp_39=((size_t)(0ULL));&dn_tmp_38->data[dn_index(dn_tmp_39,dn_tmp_38->len)]; })));*dn_tmp_40=dn_tmp_41; }
{ uint8_t dn_tmp_45=((uint8_t)(255ULL));uint8_t *dn_tmp_44=&((*({ dn_t_buf_U8 volatile *dn_tmp_42=&(v_bytes);size_t dn_tmp_43=((size_t)(1ULL));&dn_tmp_42->data[dn_index(dn_tmp_43,dn_tmp_42->len)]; })));*dn_tmp_44=dn_tmp_45; }
{ uint8_t dn_tmp_49=((uint8_t)(128ULL));uint8_t *dn_tmp_48=&((*({ dn_t_buf_U8 volatile *dn_tmp_46=&(v_bytes);size_t dn_tmp_47=((size_t)(2ULL));&dn_tmp_46->data[dn_index(dn_tmp_47,dn_tmp_46->len)]; })));*dn_tmp_48=dn_tmp_49; }
{ uint8_t dn_tmp_53=((uint8_t)(42ULL));uint8_t *dn_tmp_52=&((*({ dn_t_buf_U8 volatile *dn_tmp_50=&(v_bytes);size_t dn_tmp_51=((size_t)(3ULL));&dn_tmp_50->data[dn_index(dn_tmp_51,dn_tmp_50->len)]; })));*dn_tmp_52=dn_tmp_53; }
dn_r_CodecError v_e=({ dn_r_PosMessage1 * dn_tmp_54=(&(v_message));
dn_t_slice0_U8 dn_tmp_55=({ dn_t_buf_U8 const * dn_tmp_56=(&(v_bytes));
size_t dn_tmp_57=((size_t)(0ULL));
size_t dn_tmp_58=((size_t)(4ULL));
({ dn_range(dn_tmp_57,dn_tmp_58,(*(dn_tmp_56)).len);(dn_t_slice0_U8){(*(dn_tmp_56)).data?(*(dn_tmp_56)).data+dn_tmp_57:NULL,dn_tmp_58}; }); });
dn_f_PosMessage1_field_6_add(dn_tmp_54,dn_tmp_55); });
dn_r_CodecError v_f=({ dn_r_PosMessage1 * dn_tmp_59=(&(v_message));
dn_t_slice0_U8 dn_tmp_60=({ dn_t_buf_U8 const * dn_tmp_61=(&(v_bytes));
size_t dn_tmp_62=((size_t)(0ULL));
size_t dn_tmp_63=((size_t)(0ULL));
({ dn_range(dn_tmp_62,dn_tmp_63,(*(dn_tmp_61)).len);(dn_t_slice0_U8){(*(dn_tmp_61)).data?(*(dn_tmp_61)).data+dn_tmp_62:NULL,dn_tmp_63}; }); });
dn_f_PosMessage1_field_6_add(dn_tmp_59,dn_tmp_60); });
dn_r_CodecError v_g=({ dn_r_PosMessage1 * dn_tmp_64=(&(v_message));
dn_t_slice0_U8 dn_tmp_65=({ dn_utf8 dn_tmp_66=((dn_utf8){(const uint8_t*)"\320\237\321\200\320\270\320\262\320\265\321\202, DLM", 17});
((dn_t_slice0_U8){dn_tmp_66.data,dn_tmp_66.len}); });
dn_f_PosMessage1_field_7_add(dn_tmp_64,dn_tmp_65); });
(void)(({ bool dn_tmp_67=((({ int32_t dn_tmp_68=(v_e).f_kind;
int32_t dn_tmp_69=((int32_t)(0ULL));
(dn_tmp_68 == dn_tmp_69); }) && ({ int32_t dn_tmp_70=(v_f).f_kind;
int32_t dn_tmp_71=((int32_t)(0ULL));
(dn_tmp_70 == dn_tmp_71); })) && ({ int32_t dn_tmp_72=(v_g).f_kind;
int32_t dn_tmp_73=((int32_t)(0ULL));
(dn_tmp_72 == dn_tmp_73); }));
(dn_assert(dn_tmp_67),0); }));
v_unsigned=({ size_t dn_tmp_74=((size_t)(4ULL));
dn_new_buf_U64(dn_tmp_74); });
{ uint64_t dn_tmp_78=((uint64_t)(0ULL));uint64_t *dn_tmp_77=&((*({ dn_t_buf_U64 volatile *dn_tmp_75=&(v_unsigned);size_t dn_tmp_76=((size_t)(0ULL));&dn_tmp_75->data[dn_index(dn_tmp_76,dn_tmp_75->len)]; })));*dn_tmp_77=dn_tmp_78; }
{ uint64_t dn_tmp_82=((uint64_t)(127ULL));uint64_t *dn_tmp_81=&((*({ dn_t_buf_U64 volatile *dn_tmp_79=&(v_unsigned);size_t dn_tmp_80=((size_t)(1ULL));&dn_tmp_79->data[dn_index(dn_tmp_80,dn_tmp_79->len)]; })));*dn_tmp_81=dn_tmp_82; }
{ uint64_t dn_tmp_86=((uint64_t)(128ULL));uint64_t *dn_tmp_85=&((*({ dn_t_buf_U64 volatile *dn_tmp_83=&(v_unsigned);size_t dn_tmp_84=((size_t)(2ULL));&dn_tmp_83->data[dn_index(dn_tmp_84,dn_tmp_83->len)]; })));*dn_tmp_85=dn_tmp_86; }
{ uint64_t dn_tmp_90=((uint64_t)(18446744073709551615ULL));uint64_t *dn_tmp_89=&((*({ dn_t_buf_U64 volatile *dn_tmp_87=&(v_unsigned);size_t dn_tmp_88=((size_t)(3ULL));&dn_tmp_87->data[dn_index(dn_tmp_88,dn_tmp_87->len)]; })));*dn_tmp_89=dn_tmp_90; }
v_signed=({ size_t dn_tmp_91=((size_t)(2ULL));
dn_new_buf_I64(dn_tmp_91); });
{ int64_t dn_tmp_96=({ int64_t dn_tmp_94=((int64_t)(-INT64_C(9223372036854775807)-1));
if((__int128)dn_tmp_94<(-((__int128)1<<63))||(__int128)dn_tmp_94>(((__int128)1<<63)-1))dn_fail("numeric conversion out of range");(int64_t)dn_tmp_94; });int64_t *dn_tmp_95=&((*({ dn_t_buf_I64 volatile *dn_tmp_92=&(v_signed);size_t dn_tmp_93=((size_t)(0ULL));&dn_tmp_92->data[dn_index(dn_tmp_93,dn_tmp_92->len)]; })));*dn_tmp_95=dn_tmp_96; }
{ int64_t dn_tmp_101=({ int64_t dn_tmp_99=((int64_t)(9223372036854775807ULL));
if((__int128)dn_tmp_99<(-((__int128)1<<63))||(__int128)dn_tmp_99>(((__int128)1<<63)-1))dn_fail("numeric conversion out of range");(int64_t)dn_tmp_99; });int64_t *dn_tmp_100=&((*({ dn_t_buf_I64 volatile *dn_tmp_97=&(v_signed);size_t dn_tmp_98=((size_t)(1ULL));&dn_tmp_97->data[dn_index(dn_tmp_98,dn_tmp_97->len)]; })));*dn_tmp_100=dn_tmp_101; }
dn_r_CodecError v_h=({ dn_r_PosMessage1 * dn_tmp_102=(&(v_message));
dn_t_slice0_U64 dn_tmp_103=({ dn_t_buf_U64 const * dn_tmp_104=(&(v_unsigned));
size_t dn_tmp_105=((size_t)(0ULL));
size_t dn_tmp_106=((size_t)(4ULL));
({ dn_range(dn_tmp_105,dn_tmp_106,(*(dn_tmp_104)).len);(dn_t_slice0_U64){(*(dn_tmp_104)).data?(*(dn_tmp_104)).data+dn_tmp_105:NULL,dn_tmp_106}; }); });
dn_f_PosMessage1_field_8_add_many(dn_tmp_102,dn_tmp_103); });
dn_r_CodecError v_i=({ dn_r_PosMessage1 * dn_tmp_107=(&(v_message));
dn_t_slice0_I64 dn_tmp_108=({ dn_t_buf_I64 const * dn_tmp_109=(&(v_signed));
size_t dn_tmp_110=((size_t)(0ULL));
size_t dn_tmp_111=((size_t)(2ULL));
({ dn_range(dn_tmp_110,dn_tmp_111,(*(dn_tmp_109)).len);(dn_t_slice0_I64){(*(dn_tmp_109)).data?(*(dn_tmp_109)).data+dn_tmp_110:NULL,dn_tmp_111}; }); });
dn_f_PosMessage1_field_9_add_many(dn_tmp_107,dn_tmp_108); });
(void)(({ bool dn_tmp_112=(({ int32_t dn_tmp_113=(v_h).f_kind;
int32_t dn_tmp_114=((int32_t)(0ULL));
(dn_tmp_113 == dn_tmp_114); }) && ({ int32_t dn_tmp_115=(v_i).f_kind;
int32_t dn_tmp_116=((int32_t)(0ULL));
(dn_tmp_115 == dn_tmp_116); }));
(dn_assert(dn_tmp_112),0); }));
dn_r_CodecError v_retired=({ dn_r_PosMessage1 * dn_tmp_117=(&(v_message));
dn_t_slice0_U8 dn_tmp_118=({ dn_utf8 dn_tmp_119=((dn_utf8){(const uint8_t*)"old", 3});
((dn_t_slice0_U8){dn_tmp_119.data,dn_tmp_119.len}); });
dn_f_PosMessage1_field_11_add(dn_tmp_117,dn_tmp_118); });
(void)(({ bool dn_tmp_120=({ int32_t dn_tmp_121=(v_retired).f_kind;
int32_t dn_tmp_122=((int32_t)(0ULL));
(dn_tmp_121 == dn_tmp_122); });
(dn_assert(dn_tmp_120),0); }));
{ dn_r_PosMessage1 dn_tmp_124=({ dn_r_PosMessage1 dn_tmp_123=v_message;v_message=(dn_r_PosMessage1){0};dn_adopt_r11_PosMessage1(&dn_tmp_123);dn_tmp_123; });dn_adopt_r11_PosMessage1(&dn_tmp_124);dn_drop_buf_I64(&v_signed);
dn_drop_buf_U64(&v_unsigned);
dn_drop_buf_U8(&v_bytes);
dn_drop_buf_U32(&v_counts);
dn_drop_r11_PosMessage1(&v_message);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_124; }
dn_drop_buf_I64(&v_signed);
dn_drop_buf_U64(&v_unsigned);
dn_drop_buf_U8(&v_bytes);
dn_drop_buf_U32(&v_counts);
dn_drop_r11_PosMessage1(&v_message);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_r_PosMessage1 dn_cb_make_pos(void) {
 if(!dn_enter()){return (dn_r_PosMessage1){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_PosMessage1){0};}
 dn_r_PosMessage1 dn_result=dn_f_make_pos();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_LittleMessage1 dn_f_make_little(void) {
{
dn_r_LittleMessage1 volatile v_message={0};
dn_t_buf_U32 volatile v_counts={0};
dn_t_buf_U8 volatile v_bytes={0};
dn_t_buf_U64 volatile v_unsigned={0};
dn_t_buf_I64 volatile v_signed={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_I64(&v_signed);
dn_drop_buf_U64(&v_unsigned);
dn_drop_buf_U8(&v_bytes);
dn_drop_buf_U32(&v_counts);
dn_drop_r14_LittleMessage1(&v_message);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_message=({ dn_f_LittleMessage1_new(); });
dn_r_CodecError v_a=({ dn_r_LittleMessage1 * dn_tmp_2=(&(v_message));
uint8_t dn_tmp_3=((uint8_t)(255ULL));
dn_f_LittleMessage1_field_1_add(dn_tmp_2,dn_tmp_3); });
(void)(({ bool dn_tmp_4=({ int32_t dn_tmp_5=(v_a).f_kind;
int32_t dn_tmp_6=((int32_t)(0ULL));
(dn_tmp_5 == dn_tmp_6); });
(dn_assert(dn_tmp_4),0); }));
v_counts=({ size_t dn_tmp_7=((size_t)(3ULL));
dn_new_buf_U32(dn_tmp_7); });
{ uint32_t dn_tmp_11=((uint32_t)(0ULL));uint32_t *dn_tmp_10=&((*({ dn_t_buf_U32 volatile *dn_tmp_8=&(v_counts);size_t dn_tmp_9=((size_t)(0ULL));&dn_tmp_8->data[dn_index(dn_tmp_9,dn_tmp_8->len)]; })));*dn_tmp_10=dn_tmp_11; }
{ uint32_t dn_tmp_15=((uint32_t)(4294967295ULL));uint32_t *dn_tmp_14=&((*({ dn_t_buf_U32 volatile *dn_tmp_12=&(v_counts);size_t dn_tmp_13=((size_t)(1ULL));&dn_tmp_12->data[dn_index(dn_tmp_13,dn_tmp_12->len)]; })));*dn_tmp_14=dn_tmp_15; }
{ uint32_t dn_tmp_19=((uint32_t)(42ULL));uint32_t *dn_tmp_18=&((*({ dn_t_buf_U32 volatile *dn_tmp_16=&(v_counts);size_t dn_tmp_17=((size_t)(2ULL));&dn_tmp_16->data[dn_index(dn_tmp_17,dn_tmp_16->len)]; })));*dn_tmp_18=dn_tmp_19; }
dn_r_CodecError v_b=({ dn_r_LittleMessage1 * dn_tmp_20=(&(v_message));
dn_t_slice0_U32 dn_tmp_21=({ dn_t_buf_U32 const * dn_tmp_22=(&(v_counts));
size_t dn_tmp_23=((size_t)(0ULL));
size_t dn_tmp_24=((size_t)(3ULL));
({ dn_range(dn_tmp_23,dn_tmp_24,(*(dn_tmp_22)).len);(dn_t_slice0_U32){(*(dn_tmp_22)).data?(*(dn_tmp_22)).data+dn_tmp_23:NULL,dn_tmp_24}; }); });
dn_f_LittleMessage1_field_3_add_many(dn_tmp_20,dn_tmp_21); });
dn_r_CodecError v_c=({ dn_r_LittleMessage1 * dn_tmp_25=(&(v_message));
uint64_t dn_tmp_26=({ uint64_t dn_tmp_27=((uint64_t)(18446744073709551615ULL));
if((unsigned __int128)dn_tmp_27>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_27; });
dn_f_LittleMessage1_field_4_add(dn_tmp_25,dn_tmp_26); });
dn_r_CodecError v_d=({ dn_r_LittleMessage1 * dn_tmp_28=(&(v_message));
bool dn_tmp_29=1;
dn_f_LittleMessage1_field_5_add(dn_tmp_28,dn_tmp_29); });
(void)(({ bool dn_tmp_30=((({ int32_t dn_tmp_31=(v_b).f_kind;
int32_t dn_tmp_32=((int32_t)(0ULL));
(dn_tmp_31 == dn_tmp_32); }) && ({ int32_t dn_tmp_33=(v_c).f_kind;
int32_t dn_tmp_34=((int32_t)(0ULL));
(dn_tmp_33 == dn_tmp_34); })) && ({ int32_t dn_tmp_35=(v_d).f_kind;
int32_t dn_tmp_36=((int32_t)(0ULL));
(dn_tmp_35 == dn_tmp_36); }));
(dn_assert(dn_tmp_30),0); }));
v_bytes=({ size_t dn_tmp_37=((size_t)(4ULL));
dn_new_buf_U8(dn_tmp_37); });
{ uint8_t dn_tmp_41=((uint8_t)(0ULL));uint8_t *dn_tmp_40=&((*({ dn_t_buf_U8 volatile *dn_tmp_38=&(v_bytes);size_t dn_tmp_39=((size_t)(0ULL));&dn_tmp_38->data[dn_index(dn_tmp_39,dn_tmp_38->len)]; })));*dn_tmp_40=dn_tmp_41; }
{ uint8_t dn_tmp_45=((uint8_t)(255ULL));uint8_t *dn_tmp_44=&((*({ dn_t_buf_U8 volatile *dn_tmp_42=&(v_bytes);size_t dn_tmp_43=((size_t)(1ULL));&dn_tmp_42->data[dn_index(dn_tmp_43,dn_tmp_42->len)]; })));*dn_tmp_44=dn_tmp_45; }
{ uint8_t dn_tmp_49=((uint8_t)(128ULL));uint8_t *dn_tmp_48=&((*({ dn_t_buf_U8 volatile *dn_tmp_46=&(v_bytes);size_t dn_tmp_47=((size_t)(2ULL));&dn_tmp_46->data[dn_index(dn_tmp_47,dn_tmp_46->len)]; })));*dn_tmp_48=dn_tmp_49; }
{ uint8_t dn_tmp_53=((uint8_t)(42ULL));uint8_t *dn_tmp_52=&((*({ dn_t_buf_U8 volatile *dn_tmp_50=&(v_bytes);size_t dn_tmp_51=((size_t)(3ULL));&dn_tmp_50->data[dn_index(dn_tmp_51,dn_tmp_50->len)]; })));*dn_tmp_52=dn_tmp_53; }
dn_r_CodecError v_e=({ dn_r_LittleMessage1 * dn_tmp_54=(&(v_message));
dn_t_slice0_U8 dn_tmp_55=({ dn_t_buf_U8 const * dn_tmp_56=(&(v_bytes));
size_t dn_tmp_57=((size_t)(0ULL));
size_t dn_tmp_58=((size_t)(4ULL));
({ dn_range(dn_tmp_57,dn_tmp_58,(*(dn_tmp_56)).len);(dn_t_slice0_U8){(*(dn_tmp_56)).data?(*(dn_tmp_56)).data+dn_tmp_57:NULL,dn_tmp_58}; }); });
dn_f_LittleMessage1_field_6_add(dn_tmp_54,dn_tmp_55); });
dn_r_CodecError v_f=({ dn_r_LittleMessage1 * dn_tmp_59=(&(v_message));
dn_t_slice0_U8 dn_tmp_60=({ dn_t_buf_U8 const * dn_tmp_61=(&(v_bytes));
size_t dn_tmp_62=((size_t)(0ULL));
size_t dn_tmp_63=((size_t)(0ULL));
({ dn_range(dn_tmp_62,dn_tmp_63,(*(dn_tmp_61)).len);(dn_t_slice0_U8){(*(dn_tmp_61)).data?(*(dn_tmp_61)).data+dn_tmp_62:NULL,dn_tmp_63}; }); });
dn_f_LittleMessage1_field_6_add(dn_tmp_59,dn_tmp_60); });
dn_r_CodecError v_g=({ dn_r_LittleMessage1 * dn_tmp_64=(&(v_message));
dn_t_slice0_U8 dn_tmp_65=({ dn_utf8 dn_tmp_66=((dn_utf8){(const uint8_t*)"\320\237\321\200\320\270\320\262\320\265\321\202, DLM", 17});
((dn_t_slice0_U8){dn_tmp_66.data,dn_tmp_66.len}); });
dn_f_LittleMessage1_field_7_add(dn_tmp_64,dn_tmp_65); });
(void)(({ bool dn_tmp_67=((({ int32_t dn_tmp_68=(v_e).f_kind;
int32_t dn_tmp_69=((int32_t)(0ULL));
(dn_tmp_68 == dn_tmp_69); }) && ({ int32_t dn_tmp_70=(v_f).f_kind;
int32_t dn_tmp_71=((int32_t)(0ULL));
(dn_tmp_70 == dn_tmp_71); })) && ({ int32_t dn_tmp_72=(v_g).f_kind;
int32_t dn_tmp_73=((int32_t)(0ULL));
(dn_tmp_72 == dn_tmp_73); }));
(dn_assert(dn_tmp_67),0); }));
v_unsigned=({ size_t dn_tmp_74=((size_t)(4ULL));
dn_new_buf_U64(dn_tmp_74); });
{ uint64_t dn_tmp_78=((uint64_t)(0ULL));uint64_t *dn_tmp_77=&((*({ dn_t_buf_U64 volatile *dn_tmp_75=&(v_unsigned);size_t dn_tmp_76=((size_t)(0ULL));&dn_tmp_75->data[dn_index(dn_tmp_76,dn_tmp_75->len)]; })));*dn_tmp_77=dn_tmp_78; }
{ uint64_t dn_tmp_82=((uint64_t)(127ULL));uint64_t *dn_tmp_81=&((*({ dn_t_buf_U64 volatile *dn_tmp_79=&(v_unsigned);size_t dn_tmp_80=((size_t)(1ULL));&dn_tmp_79->data[dn_index(dn_tmp_80,dn_tmp_79->len)]; })));*dn_tmp_81=dn_tmp_82; }
{ uint64_t dn_tmp_86=((uint64_t)(128ULL));uint64_t *dn_tmp_85=&((*({ dn_t_buf_U64 volatile *dn_tmp_83=&(v_unsigned);size_t dn_tmp_84=((size_t)(2ULL));&dn_tmp_83->data[dn_index(dn_tmp_84,dn_tmp_83->len)]; })));*dn_tmp_85=dn_tmp_86; }
{ uint64_t dn_tmp_90=((uint64_t)(18446744073709551615ULL));uint64_t *dn_tmp_89=&((*({ dn_t_buf_U64 volatile *dn_tmp_87=&(v_unsigned);size_t dn_tmp_88=((size_t)(3ULL));&dn_tmp_87->data[dn_index(dn_tmp_88,dn_tmp_87->len)]; })));*dn_tmp_89=dn_tmp_90; }
v_signed=({ size_t dn_tmp_91=((size_t)(2ULL));
dn_new_buf_I64(dn_tmp_91); });
{ int64_t dn_tmp_96=({ int64_t dn_tmp_94=((int64_t)(-INT64_C(9223372036854775807)-1));
if((__int128)dn_tmp_94<(-((__int128)1<<63))||(__int128)dn_tmp_94>(((__int128)1<<63)-1))dn_fail("numeric conversion out of range");(int64_t)dn_tmp_94; });int64_t *dn_tmp_95=&((*({ dn_t_buf_I64 volatile *dn_tmp_92=&(v_signed);size_t dn_tmp_93=((size_t)(0ULL));&dn_tmp_92->data[dn_index(dn_tmp_93,dn_tmp_92->len)]; })));*dn_tmp_95=dn_tmp_96; }
{ int64_t dn_tmp_101=({ int64_t dn_tmp_99=((int64_t)(9223372036854775807ULL));
if((__int128)dn_tmp_99<(-((__int128)1<<63))||(__int128)dn_tmp_99>(((__int128)1<<63)-1))dn_fail("numeric conversion out of range");(int64_t)dn_tmp_99; });int64_t *dn_tmp_100=&((*({ dn_t_buf_I64 volatile *dn_tmp_97=&(v_signed);size_t dn_tmp_98=((size_t)(1ULL));&dn_tmp_97->data[dn_index(dn_tmp_98,dn_tmp_97->len)]; })));*dn_tmp_100=dn_tmp_101; }
dn_r_CodecError v_h=({ dn_r_LittleMessage1 * dn_tmp_102=(&(v_message));
dn_t_slice0_U64 dn_tmp_103=({ dn_t_buf_U64 const * dn_tmp_104=(&(v_unsigned));
size_t dn_tmp_105=((size_t)(0ULL));
size_t dn_tmp_106=((size_t)(4ULL));
({ dn_range(dn_tmp_105,dn_tmp_106,(*(dn_tmp_104)).len);(dn_t_slice0_U64){(*(dn_tmp_104)).data?(*(dn_tmp_104)).data+dn_tmp_105:NULL,dn_tmp_106}; }); });
dn_f_LittleMessage1_field_8_add_many(dn_tmp_102,dn_tmp_103); });
dn_r_CodecError v_i=({ dn_r_LittleMessage1 * dn_tmp_107=(&(v_message));
dn_t_slice0_I64 dn_tmp_108=({ dn_t_buf_I64 const * dn_tmp_109=(&(v_signed));
size_t dn_tmp_110=((size_t)(0ULL));
size_t dn_tmp_111=((size_t)(2ULL));
({ dn_range(dn_tmp_110,dn_tmp_111,(*(dn_tmp_109)).len);(dn_t_slice0_I64){(*(dn_tmp_109)).data?(*(dn_tmp_109)).data+dn_tmp_110:NULL,dn_tmp_111}; }); });
dn_f_LittleMessage1_field_9_add_many(dn_tmp_107,dn_tmp_108); });
(void)(({ bool dn_tmp_112=(({ int32_t dn_tmp_113=(v_h).f_kind;
int32_t dn_tmp_114=((int32_t)(0ULL));
(dn_tmp_113 == dn_tmp_114); }) && ({ int32_t dn_tmp_115=(v_i).f_kind;
int32_t dn_tmp_116=((int32_t)(0ULL));
(dn_tmp_115 == dn_tmp_116); }));
(dn_assert(dn_tmp_112),0); }));
dn_r_CodecError v_retired=({ dn_r_LittleMessage1 * dn_tmp_117=(&(v_message));
dn_t_slice0_U8 dn_tmp_118=({ dn_utf8 dn_tmp_119=((dn_utf8){(const uint8_t*)"old", 3});
((dn_t_slice0_U8){dn_tmp_119.data,dn_tmp_119.len}); });
dn_f_LittleMessage1_field_11_add(dn_tmp_117,dn_tmp_118); });
(void)(({ bool dn_tmp_120=({ int32_t dn_tmp_121=(v_retired).f_kind;
int32_t dn_tmp_122=((int32_t)(0ULL));
(dn_tmp_121 == dn_tmp_122); });
(dn_assert(dn_tmp_120),0); }));
{ dn_r_LittleMessage1 dn_tmp_124=({ dn_r_LittleMessage1 dn_tmp_123=v_message;v_message=(dn_r_LittleMessage1){0};dn_adopt_r14_LittleMessage1(&dn_tmp_123);dn_tmp_123; });dn_adopt_r14_LittleMessage1(&dn_tmp_124);dn_drop_buf_I64(&v_signed);
dn_drop_buf_U64(&v_unsigned);
dn_drop_buf_U8(&v_bytes);
dn_drop_buf_U32(&v_counts);
dn_drop_r14_LittleMessage1(&v_message);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_124; }
dn_drop_buf_I64(&v_signed);
dn_drop_buf_U64(&v_unsigned);
dn_drop_buf_U8(&v_bytes);
dn_drop_buf_U32(&v_counts);
dn_drop_r14_LittleMessage1(&v_message);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_r_LittleMessage1 dn_cb_make_little(void) {
 if(!dn_enter()){return (dn_r_LittleMessage1){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_LittleMessage1){0};}
 dn_r_LittleMessage1 dn_result=dn_f_make_little();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_main(void) {
{
dn_r_EchoMessage1 volatile v_original={0};
dn_t_buf_U8 volatile v_bytes={0};
dn_r_EchoMessage1Decoded volatile v_decoded={0};
dn_r_PosMessage1 volatile v_pos={0};
dn_r_PosMessage1Decoded volatile v_pd={0};
dn_r_LittleMessage1 volatile v_little={0};
dn_r_LittleMessage1Decoded volatile v_ld={0};
dn_r_EchoMessage2 volatile v_close={0};
dn_r_CodecDecoded volatile v_dispatch={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r12_CodecDecoded(&v_dispatch);
dn_drop_r12_EchoMessage2(&v_close);
dn_drop_r21_LittleMessage1Decoded(&v_ld);
dn_drop_r14_LittleMessage1(&v_little);
dn_drop_r18_PosMessage1Decoded(&v_pd);
dn_drop_r11_PosMessage1(&v_pos);
dn_drop_r19_EchoMessage1Decoded(&v_decoded);
dn_drop_buf_U8(&v_bytes);
dn_drop_r12_EchoMessage1(&v_original);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_original=({ dn_f_make_echo(); });
dn_r_CodecIo v_measured=({ dn_r_EchoMessage1 const * dn_tmp_2=(&(v_original));
dn_f_EchoMessage1_measure(dn_tmp_2); });
(void)(({ bool dn_tmp_3=({ int32_t dn_tmp_4=((v_measured).f_error).f_kind;
int32_t dn_tmp_5=((int32_t)(0ULL));
(dn_tmp_4 == dn_tmp_5); });
(dn_assert(dn_tmp_3),0); }));
v_bytes=({ size_t dn_tmp_6=((size_t)(1024ULL));
dn_new_buf_U8(dn_tmp_6); });
{ size_t dn_tmp_9=((size_t)(0ULL));size_t dn_tmp_8=({ dn_t_buf_U8 const * dn_tmp_7=(&(v_bytes));
(*(dn_tmp_7)).len; });for(size_t v_index=dn_tmp_9;v_index<dn_tmp_8;v_index++){
{ uint8_t dn_tmp_13=((uint8_t)(165ULL));uint8_t *dn_tmp_12=&((*({ dn_t_buf_U8 volatile *dn_tmp_10=&(v_bytes);size_t dn_tmp_11=v_index;&dn_tmp_10->data[dn_tmp_11]; })));*dn_tmp_12=dn_tmp_13; }
}
 }
dn_r_CodecIo v_encoded=({ dn_r_EchoMessage1 const * dn_tmp_14=(&(v_original));
dn_t_buf_U8 * dn_tmp_15=(&(v_bytes));
size_t dn_tmp_16=((size_t)(5ULL));
dn_f_EchoMessage1_encode(dn_tmp_14,dn_tmp_15,dn_tmp_16); });
(void)(({ bool dn_tmp_17=(((({ int32_t dn_tmp_18=((v_encoded).f_error).f_kind;
int32_t dn_tmp_19=((int32_t)(0ULL));
(dn_tmp_18 == dn_tmp_19); }) && ({ size_t dn_tmp_20=(v_encoded).f_count;
size_t dn_tmp_21=(v_measured).f_count;
(dn_tmp_20 == dn_tmp_21); })) && ({ uint8_t dn_tmp_22=(*({ dn_t_buf_U8 volatile *dn_tmp_23=&(v_bytes);size_t dn_tmp_24=((size_t)(4ULL));&dn_tmp_23->data[dn_index(dn_tmp_24,dn_tmp_23->len)]; }));
uint8_t dn_tmp_25=((uint8_t)(165ULL));
(dn_tmp_22 == dn_tmp_25); })) && ({ uint8_t dn_tmp_26=(*({ dn_t_buf_U8 volatile *dn_tmp_27=&(v_bytes);size_t dn_tmp_28=(v_encoded).f_next;&dn_tmp_27->data[dn_index(dn_tmp_28,dn_tmp_27->len)]; }));
uint8_t dn_tmp_29=((uint8_t)(165ULL));
(dn_tmp_26 == dn_tmp_29); }));
(dn_assert(dn_tmp_17),0); }));
v_decoded=({ dn_t_slice0_U8 dn_tmp_30=({ dn_t_buf_U8 const * dn_tmp_31=(&(v_bytes));
size_t dn_tmp_32=((size_t)(5ULL));
size_t dn_tmp_33=(v_encoded).f_count;
({ dn_range(dn_tmp_32,dn_tmp_33,(*(dn_tmp_31)).len);(dn_t_slice0_U8){(*(dn_tmp_31)).data?(*(dn_tmp_31)).data+dn_tmp_32:NULL,dn_tmp_33}; }); });
dn_f_EchoMessage1_decode(dn_tmp_30); });
(void)(({ bool dn_tmp_34=(({ int32_t dn_tmp_35=((v_decoded).f_error).f_kind;
int32_t dn_tmp_36=((int32_t)(0ULL));
(dn_tmp_35 == dn_tmp_36); }) && ({ dn_r_EchoMessage1 const * dn_tmp_37=(&(v_original));
dn_r_EchoMessage1 const * dn_tmp_38=(&((v_decoded).f_value));
dn_f_EchoMessage1_equal(dn_tmp_37,dn_tmp_38); }));
(dn_assert(dn_tmp_34),0); }));
dn_r_EchoMessage1Field4 v_number=({ dn_r_EchoMessage1 const * dn_tmp_39=(&((v_decoded).f_value));
size_t dn_tmp_40=((size_t)(0ULL));
dn_f_EchoMessage1_field_4_get(dn_tmp_39,dn_tmp_40); });
dn_r_EchoMessage1Field9 v_signed=({ dn_r_EchoMessage1 const * dn_tmp_41=(&((v_decoded).f_value));
size_t dn_tmp_42=((size_t)(0ULL));
dn_f_EchoMessage1_field_9_get(dn_tmp_41,dn_tmp_42); });
dn_r_EchoMessage1Field2 v_missing=({ dn_r_EchoMessage1 const * dn_tmp_43=(&((v_decoded).f_value));
size_t dn_tmp_44=((size_t)(0ULL));
dn_f_EchoMessage1_field_2_get(dn_tmp_43,dn_tmp_44); });
(void)(({ bool dn_tmp_45=(((({ int32_t dn_tmp_46=((v_number).f_error).f_kind;
int32_t dn_tmp_47=((int32_t)(0ULL));
(dn_tmp_46 == dn_tmp_47); }) && ({ uint64_t dn_tmp_48=(v_number).f_value;
uint64_t dn_tmp_49=({ uint64_t dn_tmp_50=((uint64_t)(18446744073709551615ULL));
if((unsigned __int128)dn_tmp_50>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_50; });
(dn_tmp_48 == dn_tmp_49); })) && ({ int64_t dn_tmp_51=(v_signed).f_value;
int64_t dn_tmp_52=({ int64_t dn_tmp_53=((int64_t)(-INT64_C(9223372036854775807)-1));
if((__int128)dn_tmp_53<(-((__int128)1<<63))||(__int128)dn_tmp_53>(((__int128)1<<63)-1))dn_fail("numeric conversion out of range");(int64_t)dn_tmp_53; });
(dn_tmp_51 == dn_tmp_52); })) && ({ int32_t dn_tmp_54=((v_missing).f_error).f_kind;
int32_t dn_tmp_55=((int32_t)(2ULL));
(dn_tmp_54 == dn_tmp_55); }));
(dn_assert(dn_tmp_45),0); }));
dn_t_slice0_U8 v_view=({ dn_r_EchoMessage1 const * dn_tmp_56=(&((v_decoded).f_value));
size_t dn_tmp_57=((size_t)(0ULL));
dn_f_EchoMessage1_field_7_view(dn_tmp_56,dn_tmp_57); });
(void)(({ bool dn_tmp_58=({ size_t dn_tmp_59=({ dn_t_slice0_U8 dn_tmp_60=v_view;
(dn_tmp_60).len; });
size_t dn_tmp_61=({ dn_t_slice0_U8 dn_tmp_62=({ dn_utf8 dn_tmp_63=((dn_utf8){(const uint8_t*)"\320\237\321\200\320\270\320\262\320\265\321\202, DLM", 17});
((dn_t_slice0_U8){dn_tmp_63.data,dn_tmp_63.len}); });
(dn_tmp_62).len; });
(dn_tmp_59 == dn_tmp_61); });
(dn_assert(dn_tmp_58),0); }));
dn_r_CodecSpan v_empty=({ dn_r_EchoMessage1 const * dn_tmp_64=(&((v_decoded).f_value));
size_t dn_tmp_65=((size_t)(1ULL));
dn_f_EchoMessage1_field_6_span(dn_tmp_64,dn_tmp_65); });
(void)(({ bool dn_tmp_66=(({ int32_t dn_tmp_67=((v_empty).f_error).f_kind;
int32_t dn_tmp_68=((int32_t)(0ULL));
(dn_tmp_67 == dn_tmp_68); }) && ({ size_t dn_tmp_69=(v_empty).f_count;
size_t dn_tmp_70=((size_t)(0ULL));
(dn_tmp_69 == dn_tmp_70); }));
(dn_assert(dn_tmp_66),0); }));
v_pos=({ dn_f_make_pos(); });
dn_r_CodecIo v_p=({ dn_r_PosMessage1 const * dn_tmp_71=(&(v_pos));
dn_t_buf_U8 * dn_tmp_72=(&(v_bytes));
size_t dn_tmp_73=((size_t)(0ULL));
dn_f_PosMessage1_encode(dn_tmp_71,dn_tmp_72,dn_tmp_73); });
(void)(({ bool dn_tmp_74=({ int32_t dn_tmp_75=((v_p).f_error).f_kind;
int32_t dn_tmp_76=((int32_t)(0ULL));
(dn_tmp_75 == dn_tmp_76); });
(dn_assert(dn_tmp_74),0); }));
v_pd=({ dn_t_slice0_U8 dn_tmp_77=({ dn_t_buf_U8 const * dn_tmp_78=(&(v_bytes));
size_t dn_tmp_79=((size_t)(0ULL));
size_t dn_tmp_80=(v_p).f_count;
({ dn_range(dn_tmp_79,dn_tmp_80,(*(dn_tmp_78)).len);(dn_t_slice0_U8){(*(dn_tmp_78)).data?(*(dn_tmp_78)).data+dn_tmp_79:NULL,dn_tmp_80}; }); });
dn_f_PosMessage1_decode(dn_tmp_77); });
(void)(({ bool dn_tmp_81=(({ int32_t dn_tmp_82=((v_pd).f_error).f_kind;
int32_t dn_tmp_83=((int32_t)(0ULL));
(dn_tmp_82 == dn_tmp_83); }) && ({ dn_r_PosMessage1 const * dn_tmp_84=(&(v_pos));
dn_r_PosMessage1 const * dn_tmp_85=(&((v_pd).f_value));
dn_f_PosMessage1_equal(dn_tmp_84,dn_tmp_85); }));
(dn_assert(dn_tmp_81),0); }));
v_little=({ dn_f_make_little(); });
dn_r_CodecIo v_l=({ dn_r_LittleMessage1 const * dn_tmp_86=(&(v_little));
dn_t_buf_U8 * dn_tmp_87=(&(v_bytes));
size_t dn_tmp_88=((size_t)(0ULL));
dn_f_LittleMessage1_encode(dn_tmp_86,dn_tmp_87,dn_tmp_88); });
(void)(({ bool dn_tmp_89=({ int32_t dn_tmp_90=((v_l).f_error).f_kind;
int32_t dn_tmp_91=((int32_t)(0ULL));
(dn_tmp_90 == dn_tmp_91); });
(dn_assert(dn_tmp_89),0); }));
v_ld=({ dn_t_slice0_U8 dn_tmp_92=({ dn_t_buf_U8 const * dn_tmp_93=(&(v_bytes));
size_t dn_tmp_94=((size_t)(0ULL));
size_t dn_tmp_95=(v_l).f_count;
({ dn_range(dn_tmp_94,dn_tmp_95,(*(dn_tmp_93)).len);(dn_t_slice0_U8){(*(dn_tmp_93)).data?(*(dn_tmp_93)).data+dn_tmp_94:NULL,dn_tmp_95}; }); });
dn_f_LittleMessage1_decode(dn_tmp_92); });
(void)(({ bool dn_tmp_96=(({ int32_t dn_tmp_97=((v_ld).f_error).f_kind;
int32_t dn_tmp_98=((int32_t)(0ULL));
(dn_tmp_97 == dn_tmp_98); }) && ({ dn_r_LittleMessage1 const * dn_tmp_99=(&(v_little));
dn_r_LittleMessage1 const * dn_tmp_100=(&((v_ld).f_value));
dn_f_LittleMessage1_equal(dn_tmp_99,dn_tmp_100); }));
(dn_assert(dn_tmp_96),0); }));
v_close=({ dn_f_EchoMessage2_new(); });
dn_r_CodecIo v_packet=({ dn_r_EchoMessage2 const * dn_tmp_101=(&(v_close));
dn_t_buf_U8 * dn_tmp_102=(&(v_bytes));
size_t dn_tmp_103=((size_t)(0ULL));
dn_f_EchoMessage2_encode(dn_tmp_101,dn_tmp_102,dn_tmp_103); });
(void)(({ bool dn_tmp_104=(({ int32_t dn_tmp_105=((v_packet).f_error).f_kind;
int32_t dn_tmp_106=((int32_t)(0ULL));
(dn_tmp_105 == dn_tmp_106); }) && ({ size_t dn_tmp_107=(v_packet).f_count;
size_t dn_tmp_108=((size_t)(12ULL));
(dn_tmp_107 == dn_tmp_108); }));
(dn_assert(dn_tmp_104),0); }));
v_dispatch=({ dn_t_slice0_U8 dn_tmp_109=({ dn_t_buf_U8 const * dn_tmp_110=(&(v_bytes));
size_t dn_tmp_111=((size_t)(0ULL));
size_t dn_tmp_112=(v_packet).f_count;
({ dn_range(dn_tmp_111,dn_tmp_112,(*(dn_tmp_110)).len);(dn_t_slice0_U8){(*(dn_tmp_110)).data?(*(dn_tmp_110)).data+dn_tmp_111:NULL,dn_tmp_112}; }); });
dn_f_Echo_decode(dn_tmp_109); });
(void)(({ bool dn_tmp_113=(({ int32_t dn_tmp_114=((v_dispatch).f_error).f_kind;
int32_t dn_tmp_115=((int32_t)(0ULL));
(dn_tmp_114 == dn_tmp_115); }) && ({ uint32_t dn_tmp_116=((v_dispatch).f_value).f_message;
uint32_t dn_tmp_117=((uint32_t)(2ULL));
(dn_tmp_116 == dn_tmp_117); }));
(dn_assert(dn_tmp_113),0); }));
(void)(({ dn_utf8 dn_tmp_118=((dn_utf8){(const uint8_t*)"generated codecs typed fields UTF8 integers unknowns and all wire profiles OK", 77});
(fwrite(dn_tmp_118.data,1,dn_tmp_118.len,stdout),fputc('\n',stdout),0); }));
dn_drop_r12_CodecDecoded(&v_dispatch);
dn_drop_r12_EchoMessage2(&v_close);
dn_drop_r21_LittleMessage1Decoded(&v_ld);
dn_drop_r14_LittleMessage1(&v_little);
dn_drop_r18_PosMessage1Decoded(&v_pd);
dn_drop_r11_PosMessage1(&v_pos);
dn_drop_r19_EchoMessage1Decoded(&v_decoded);
dn_drop_buf_U8(&v_bytes);
dn_drop_r12_EchoMessage1(&v_original);
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
DN_EXPORT size_t dlm_native_sizeof_ProtocolVersion(void){return 12;}
DN_EXPORT size_t dlm_native_alignof_ProtocolVersion(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_ProtocolVersion(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ProtocolVersion_major(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ProtocolVersion_minor(void){return 4;}
DN_EXPORT size_t dlm_native_offset_ProtocolVersion_minimum_minor(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_WireFormat(void){return 16;}
DN_EXPORT size_t dlm_native_alignof_WireFormat(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_WireFormat(void){return 1;}
DN_EXPORT size_t dlm_native_offset_WireFormat_kind(void){return 0;}
DN_EXPORT size_t dlm_native_offset_WireFormat_endian(void){return 4;}
DN_EXPORT size_t dlm_native_offset_WireFormat_envelope_version(void){return 8;}
DN_EXPORT size_t dlm_native_offset_WireFormat_unknown_fields(void){return 12;}
DN_EXPORT size_t dlm_native_sizeof_MessageField(void){return 32;}
DN_EXPORT size_t dlm_native_alignof_MessageField(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_MessageField(void){return 1;}
DN_EXPORT size_t dlm_native_offset_MessageField_id(void){return 0;}
DN_EXPORT size_t dlm_native_offset_MessageField_kind(void){return 4;}
DN_EXPORT size_t dlm_native_offset_MessageField_minimum_count(void){return 8;}
DN_EXPORT size_t dlm_native_offset_MessageField_maximum_count(void){return 12;}
DN_EXPORT size_t dlm_native_offset_MessageField_maximum_length(void){return 16;}
DN_EXPORT size_t dlm_native_offset_MessageField_first_minor(void){return 20;}
DN_EXPORT size_t dlm_native_offset_MessageField_last_minor(void){return 24;}
DN_EXPORT size_t dlm_native_offset_MessageField_flags(void){return 28;}
DN_EXPORT size_t dlm_native_sizeof_MessageSchema(void){return 1048;}
DN_EXPORT size_t dlm_native_alignof_MessageSchema(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_MessageSchema(void){return 1;}
DN_EXPORT size_t dlm_native_offset_MessageSchema_id(void){return 0;}
DN_EXPORT size_t dlm_native_offset_MessageSchema_phase(void){return 4;}
DN_EXPORT size_t dlm_native_offset_MessageSchema_senders(void){return 8;}
DN_EXPORT size_t dlm_native_offset_MessageSchema_first_minor(void){return 12;}
DN_EXPORT size_t dlm_native_offset_MessageSchema_last_minor(void){return 16;}
DN_EXPORT size_t dlm_native_offset_MessageSchema_field_count(void){return 20;}
DN_EXPORT size_t dlm_native_offset_MessageSchema_fields(void){return 24;}
DN_EXPORT size_t dlm_native_sizeof_SessionContract(void){return 20;}
DN_EXPORT size_t dlm_native_alignof_SessionContract(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_SessionContract(void){return 1;}
DN_EXPORT size_t dlm_native_offset_SessionContract_handshake_timeout_ms(void){return 0;}
DN_EXPORT size_t dlm_native_offset_SessionContract_idle_timeout_ms(void){return 4;}
DN_EXPORT size_t dlm_native_offset_SessionContract_maximum_duration_ms(void){return 8;}
DN_EXPORT size_t dlm_native_offset_SessionContract_maximum_in_flight(void){return 12;}
DN_EXPORT size_t dlm_native_offset_SessionContract_authentication_before_data(void){return 16;}
DN_EXPORT size_t dlm_native_sizeof_SecurityRequirements(void){return 12;}
DN_EXPORT size_t dlm_native_alignof_SecurityRequirements(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_SecurityRequirements(void){return 1;}
DN_EXPORT size_t dlm_native_offset_SecurityRequirements_authentication(void){return 0;}
DN_EXPORT size_t dlm_native_offset_SecurityRequirements_confidentiality(void){return 4;}
DN_EXPORT size_t dlm_native_offset_SecurityRequirements_integrity(void){return 5;}
DN_EXPORT size_t dlm_native_offset_SecurityRequirements_replay_scope(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_ResourceBounds(void){return 48;}
DN_EXPORT size_t dlm_native_alignof_ResourceBounds(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ResourceBounds(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ResourceBounds_transport(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ResourceBounds_maximum_unknown_fields(void){return 24;}
DN_EXPORT size_t dlm_native_offset_ResourceBounds_maximum_unknown_bytes(void){return 28;}
DN_EXPORT size_t dlm_native_offset_ResourceBounds_maximum_session_messages(void){return 32;}
DN_EXPORT size_t dlm_native_offset_ResourceBounds_maximum_schema_nodes(void){return 40;}
DN_EXPORT size_t dlm_native_sizeof_ProofObligations(void){return 8;}
DN_EXPORT size_t dlm_native_alignof_ProofObligations(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_ProofObligations(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ProofObligations_requested(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ProofObligations_assurance(void){return 4;}
DN_EXPORT size_t dlm_native_sizeof_NetworkSpec(void){return 16904;}
DN_EXPORT size_t dlm_native_alignof_NetworkSpec(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_NetworkSpec(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NetworkSpec_protocol(void){return 0;}
DN_EXPORT size_t dlm_native_offset_NetworkSpec_version(void){return 4;}
DN_EXPORT size_t dlm_native_offset_NetworkSpec_wire(void){return 16;}
DN_EXPORT size_t dlm_native_offset_NetworkSpec_transport(void){return 32;}
DN_EXPORT size_t dlm_native_offset_NetworkSpec_security(void){return 44;}
DN_EXPORT size_t dlm_native_offset_NetworkSpec_resources(void){return 56;}
DN_EXPORT size_t dlm_native_offset_NetworkSpec_session(void){return 104;}
DN_EXPORT size_t dlm_native_offset_NetworkSpec_proofs(void){return 124;}
DN_EXPORT size_t dlm_native_offset_NetworkSpec_message_count(void){return 132;}
DN_EXPORT size_t dlm_native_offset_NetworkSpec_messages(void){return 136;}
DN_EXPORT size_t dlm_native_sizeof_SchemaMeasure(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_SchemaMeasure(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_SchemaMeasure(void){return 1;}
DN_EXPORT size_t dlm_native_offset_SchemaMeasure_minimum(void){return 0;}
DN_EXPORT size_t dlm_native_offset_SchemaMeasure_maximum(void){return 8;}
DN_EXPORT size_t dlm_native_offset_SchemaMeasure_status(void){return 16;}
DN_EXPORT size_t dlm_native_sizeof_NetworkSpecCheck(void){return 56;}
DN_EXPORT size_t dlm_native_alignof_NetworkSpecCheck(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_NetworkSpecCheck(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NetworkSpecCheck_status(void){return 0;}
DN_EXPORT size_t dlm_native_offset_NetworkSpecCheck_component(void){return 4;}
DN_EXPORT size_t dlm_native_offset_NetworkSpecCheck_message(void){return 8;}
DN_EXPORT size_t dlm_native_offset_NetworkSpecCheck_field(void){return 12;}
DN_EXPORT size_t dlm_native_offset_NetworkSpecCheck_minimum(void){return 16;}
DN_EXPORT size_t dlm_native_offset_NetworkSpecCheck_maximum(void){return 24;}
DN_EXPORT size_t dlm_native_offset_NetworkSpecCheck_required_properties(void){return 32;}
DN_EXPORT size_t dlm_native_offset_NetworkSpecCheck_required_proofs(void){return 36;}
DN_EXPORT size_t dlm_native_offset_NetworkSpecCheck_pending_proofs(void){return 40;}
DN_EXPORT size_t dlm_native_offset_NetworkSpecCheck_unsupported_security(void){return 44;}
DN_EXPORT size_t dlm_native_offset_NetworkSpecCheck_schema_nodes(void){return 48;}
DN_EXPORT size_t dlm_native_sizeof_NetworkAdmission(void){return 112;}
DN_EXPORT size_t dlm_native_alignof_NetworkAdmission(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_NetworkAdmission(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NetworkAdmission_requirements(void){return 0;}
DN_EXPORT size_t dlm_native_offset_NetworkAdmission_bounds(void){return 16;}
DN_EXPORT size_t dlm_native_offset_NetworkAdmission_choice(void){return 40;}
DN_EXPORT size_t dlm_native_offset_NetworkAdmission_session(void){return 68;}
DN_EXPORT size_t dlm_native_offset_NetworkAdmission_maximum_session_messages(void){return 88;}
DN_EXPORT size_t dlm_native_offset_NetworkAdmission_pending_proofs(void){return 96;}
DN_EXPORT size_t dlm_native_offset_NetworkAdmission_unsupported_security(void){return 100;}
DN_EXPORT size_t dlm_native_offset_NetworkAdmission_status(void){return 104;}
DN_EXPORT size_t dlm_native_sizeof_ProtocolValue(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_ProtocolValue(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ProtocolValue(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ProtocolValue_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ProtocolValue_next(void){return 8;}
DN_EXPORT size_t dlm_native_offset_ProtocolValue_status(void){return 16;}
DN_EXPORT size_t dlm_native_sizeof_ProtocolFrame(void){return 32;}
DN_EXPORT size_t dlm_native_alignof_ProtocolFrame(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ProtocolFrame(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ProtocolFrame_payload(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ProtocolFrame_count(void){return 8;}
DN_EXPORT size_t dlm_native_offset_ProtocolFrame_next(void){return 16;}
DN_EXPORT size_t dlm_native_offset_ProtocolFrame_status(void){return 24;}
DN_EXPORT size_t dlm_native_sizeof_Utf8Step(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_Utf8Step(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_Utf8Step(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Utf8Step_codepoint(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Utf8Step_next(void){return 8;}
DN_EXPORT size_t dlm_native_offset_Utf8Step_valid(void){return 16;}
DN_EXPORT size_t dlm_native_sizeof_CodecPlan(void){return 1088;}
DN_EXPORT size_t dlm_native_alignof_CodecPlan(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_CodecPlan(void){return 1;}
DN_EXPORT size_t dlm_native_offset_CodecPlan_message(void){return 0;}
DN_EXPORT size_t dlm_native_offset_CodecPlan_wire(void){return 4;}
DN_EXPORT size_t dlm_native_offset_CodecPlan_minor(void){return 20;}
DN_EXPORT size_t dlm_native_offset_CodecPlan_field_count(void){return 24;}
DN_EXPORT size_t dlm_native_offset_CodecPlan_fields(void){return 28;}
DN_EXPORT size_t dlm_native_offset_CodecPlan_maximum_values(void){return 1052;}
DN_EXPORT size_t dlm_native_offset_CodecPlan_maximum_wire(void){return 1056;}
DN_EXPORT size_t dlm_native_offset_CodecPlan_maximum_unknown_fields(void){return 1064;}
DN_EXPORT size_t dlm_native_offset_CodecPlan_maximum_unknown_bytes(void){return 1072;}
DN_EXPORT size_t dlm_native_offset_CodecPlan_storage_budget(void){return 1080;}
DN_EXPORT size_t dlm_native_sizeof_CodecError(void){return 16;}
DN_EXPORT size_t dlm_native_alignof_CodecError(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_CodecError(void){return 1;}
DN_EXPORT size_t dlm_native_offset_CodecError_kind(void){return 0;}
DN_EXPORT size_t dlm_native_offset_CodecError_field(void){return 4;}
DN_EXPORT size_t dlm_native_offset_CodecError_offset(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_CodecIo(void){return 32;}
DN_EXPORT size_t dlm_native_alignof_CodecIo(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_CodecIo(void){return 1;}
DN_EXPORT size_t dlm_native_offset_CodecIo_count(void){return 0;}
DN_EXPORT size_t dlm_native_offset_CodecIo_next(void){return 8;}
DN_EXPORT size_t dlm_native_offset_CodecIo_error(void){return 16;}
DN_EXPORT size_t dlm_native_sizeof_CodecValue(void){return 32;}
DN_EXPORT size_t dlm_native_alignof_CodecValue(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_CodecValue(void){return 1;}
DN_EXPORT size_t dlm_native_offset_CodecValue_field(void){return 0;}
DN_EXPORT size_t dlm_native_offset_CodecValue_kind(void){return 4;}
DN_EXPORT size_t dlm_native_offset_CodecValue_scalar(void){return 8;}
DN_EXPORT size_t dlm_native_offset_CodecValue_offset(void){return 16;}
DN_EXPORT size_t dlm_native_offset_CodecValue_count(void){return 24;}
DN_EXPORT size_t dlm_native_sizeof_CodecMessage(void){return 80;}
DN_EXPORT size_t dlm_native_alignof_CodecMessage(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_CodecMessage(void){return 1;}
DN_EXPORT size_t dlm_native_offset_CodecMessage_message(void){return 0;}
DN_EXPORT size_t dlm_native_offset_CodecMessage_values(void){return 8;}
DN_EXPORT size_t dlm_native_offset_CodecMessage_bytes(void){return 32;}
DN_EXPORT size_t dlm_native_offset_CodecMessage_unknown(void){return 56;}
DN_EXPORT size_t dlm_native_sizeof_CodecDecoded(void){return 96;}
DN_EXPORT size_t dlm_native_alignof_CodecDecoded(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_CodecDecoded(void){return 1;}
DN_EXPORT size_t dlm_native_offset_CodecDecoded_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_CodecDecoded_error(void){return 80;}
DN_EXPORT size_t dlm_native_sizeof_CodecScalar(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_CodecScalar(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_CodecScalar(void){return 1;}
DN_EXPORT size_t dlm_native_offset_CodecScalar_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_CodecScalar_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_CodecSpan(void){return 32;}
DN_EXPORT size_t dlm_native_alignof_CodecSpan(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_CodecSpan(void){return 1;}
DN_EXPORT size_t dlm_native_offset_CodecSpan_offset(void){return 0;}
DN_EXPORT size_t dlm_native_offset_CodecSpan_count(void){return 8;}
DN_EXPORT size_t dlm_native_offset_CodecSpan_error(void){return 16;}
DN_EXPORT size_t dlm_native_sizeof_CodecRead(void){return 48;}
DN_EXPORT size_t dlm_native_alignof_CodecRead(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_CodecRead(void){return 1;}
DN_EXPORT size_t dlm_native_offset_CodecRead_scalar(void){return 0;}
DN_EXPORT size_t dlm_native_offset_CodecRead_offset(void){return 8;}
DN_EXPORT size_t dlm_native_offset_CodecRead_count(void){return 16;}
DN_EXPORT size_t dlm_native_offset_CodecRead_next(void){return 24;}
DN_EXPORT size_t dlm_native_offset_CodecRead_error(void){return 32;}
DN_EXPORT size_t dlm_native_sizeof_CodecShape(void){return 440;}
DN_EXPORT size_t dlm_native_alignof_CodecShape(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_CodecShape(void){return 1;}
DN_EXPORT size_t dlm_native_offset_CodecShape_counts(void){return 0;}
DN_EXPORT size_t dlm_native_offset_CodecShape_lengths(void){return 128;}
DN_EXPORT size_t dlm_native_offset_CodecShape_values(void){return 384;}
DN_EXPORT size_t dlm_native_offset_CodecShape_bytes(void){return 392;}
DN_EXPORT size_t dlm_native_offset_CodecShape_unknown(void){return 400;}
DN_EXPORT size_t dlm_native_offset_CodecShape_unknown_fields(void){return 408;}
DN_EXPORT size_t dlm_native_offset_CodecShape_wire(void){return 416;}
DN_EXPORT size_t dlm_native_offset_CodecShape_error(void){return 424;}
DN_EXPORT size_t dlm_native_sizeof_CodecReservation(void){return 32;}
DN_EXPORT size_t dlm_native_alignof_CodecReservation(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_CodecReservation(void){return 1;}
DN_EXPORT size_t dlm_native_offset_CodecReservation_start(void){return 0;}
DN_EXPORT size_t dlm_native_offset_CodecReservation_count(void){return 8;}
DN_EXPORT size_t dlm_native_offset_CodecReservation_error(void){return 16;}
DN_EXPORT size_t dlm_native_sizeof_EchoMessage1(void){return 80;}
DN_EXPORT size_t dlm_native_alignof_EchoMessage1(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_EchoMessage1(void){return 1;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1_inner(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_EchoMessage1Decoded(void){return 96;}
DN_EXPORT size_t dlm_native_alignof_EchoMessage1Decoded(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_EchoMessage1Decoded(void){return 1;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1Decoded_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1Decoded_error(void){return 80;}
DN_EXPORT size_t dlm_native_sizeof_EchoMessage1Field1(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_EchoMessage1Field1(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_EchoMessage1Field1(void){return 1;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1Field1_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1Field1_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_EchoMessage1Field2(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_EchoMessage1Field2(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_EchoMessage1Field2(void){return 1;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1Field2_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1Field2_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_EchoMessage1Field3(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_EchoMessage1Field3(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_EchoMessage1Field3(void){return 1;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1Field3_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1Field3_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_EchoMessage1Field4(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_EchoMessage1Field4(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_EchoMessage1Field4(void){return 1;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1Field4_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1Field4_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_EchoMessage1Field5(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_EchoMessage1Field5(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_EchoMessage1Field5(void){return 1;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1Field5_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1Field5_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_EchoMessage1Field8(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_EchoMessage1Field8(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_EchoMessage1Field8(void){return 1;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1Field8_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1Field8_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_EchoMessage1Field9(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_EchoMessage1Field9(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_EchoMessage1Field9(void){return 1;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1Field9_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1Field9_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_EchoMessage1Field10(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_EchoMessage1Field10(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_EchoMessage1Field10(void){return 1;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1Field10_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_EchoMessage1Field10_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_EchoMessage2(void){return 80;}
DN_EXPORT size_t dlm_native_alignof_EchoMessage2(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_EchoMessage2(void){return 1;}
DN_EXPORT size_t dlm_native_offset_EchoMessage2_inner(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_EchoMessage2Decoded(void){return 96;}
DN_EXPORT size_t dlm_native_alignof_EchoMessage2Decoded(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_EchoMessage2Decoded(void){return 1;}
DN_EXPORT size_t dlm_native_offset_EchoMessage2Decoded_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_EchoMessage2Decoded_error(void){return 80;}
DN_EXPORT size_t dlm_native_sizeof_PosMessage1(void){return 80;}
DN_EXPORT size_t dlm_native_alignof_PosMessage1(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_PosMessage1(void){return 1;}
DN_EXPORT size_t dlm_native_offset_PosMessage1_inner(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_PosMessage1Decoded(void){return 96;}
DN_EXPORT size_t dlm_native_alignof_PosMessage1Decoded(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_PosMessage1Decoded(void){return 1;}
DN_EXPORT size_t dlm_native_offset_PosMessage1Decoded_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_PosMessage1Decoded_error(void){return 80;}
DN_EXPORT size_t dlm_native_sizeof_PosMessage1Field1(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_PosMessage1Field1(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_PosMessage1Field1(void){return 1;}
DN_EXPORT size_t dlm_native_offset_PosMessage1Field1_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_PosMessage1Field1_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_PosMessage1Field2(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_PosMessage1Field2(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_PosMessage1Field2(void){return 1;}
DN_EXPORT size_t dlm_native_offset_PosMessage1Field2_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_PosMessage1Field2_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_PosMessage1Field3(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_PosMessage1Field3(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_PosMessage1Field3(void){return 1;}
DN_EXPORT size_t dlm_native_offset_PosMessage1Field3_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_PosMessage1Field3_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_PosMessage1Field4(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_PosMessage1Field4(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_PosMessage1Field4(void){return 1;}
DN_EXPORT size_t dlm_native_offset_PosMessage1Field4_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_PosMessage1Field4_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_PosMessage1Field5(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_PosMessage1Field5(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_PosMessage1Field5(void){return 1;}
DN_EXPORT size_t dlm_native_offset_PosMessage1Field5_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_PosMessage1Field5_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_PosMessage1Field8(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_PosMessage1Field8(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_PosMessage1Field8(void){return 1;}
DN_EXPORT size_t dlm_native_offset_PosMessage1Field8_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_PosMessage1Field8_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_PosMessage1Field9(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_PosMessage1Field9(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_PosMessage1Field9(void){return 1;}
DN_EXPORT size_t dlm_native_offset_PosMessage1Field9_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_PosMessage1Field9_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_PosMessage2(void){return 80;}
DN_EXPORT size_t dlm_native_alignof_PosMessage2(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_PosMessage2(void){return 1;}
DN_EXPORT size_t dlm_native_offset_PosMessage2_inner(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_PosMessage2Decoded(void){return 96;}
DN_EXPORT size_t dlm_native_alignof_PosMessage2Decoded(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_PosMessage2Decoded(void){return 1;}
DN_EXPORT size_t dlm_native_offset_PosMessage2Decoded_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_PosMessage2Decoded_error(void){return 80;}
DN_EXPORT size_t dlm_native_sizeof_LittleMessage1(void){return 80;}
DN_EXPORT size_t dlm_native_alignof_LittleMessage1(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_LittleMessage1(void){return 1;}
DN_EXPORT size_t dlm_native_offset_LittleMessage1_inner(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_LittleMessage1Decoded(void){return 96;}
DN_EXPORT size_t dlm_native_alignof_LittleMessage1Decoded(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_LittleMessage1Decoded(void){return 1;}
DN_EXPORT size_t dlm_native_offset_LittleMessage1Decoded_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_LittleMessage1Decoded_error(void){return 80;}
DN_EXPORT size_t dlm_native_sizeof_LittleMessage1Field1(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_LittleMessage1Field1(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_LittleMessage1Field1(void){return 1;}
DN_EXPORT size_t dlm_native_offset_LittleMessage1Field1_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_LittleMessage1Field1_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_LittleMessage1Field2(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_LittleMessage1Field2(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_LittleMessage1Field2(void){return 1;}
DN_EXPORT size_t dlm_native_offset_LittleMessage1Field2_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_LittleMessage1Field2_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_LittleMessage1Field3(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_LittleMessage1Field3(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_LittleMessage1Field3(void){return 1;}
DN_EXPORT size_t dlm_native_offset_LittleMessage1Field3_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_LittleMessage1Field3_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_LittleMessage1Field4(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_LittleMessage1Field4(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_LittleMessage1Field4(void){return 1;}
DN_EXPORT size_t dlm_native_offset_LittleMessage1Field4_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_LittleMessage1Field4_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_LittleMessage1Field5(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_LittleMessage1Field5(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_LittleMessage1Field5(void){return 1;}
DN_EXPORT size_t dlm_native_offset_LittleMessage1Field5_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_LittleMessage1Field5_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_LittleMessage1Field8(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_LittleMessage1Field8(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_LittleMessage1Field8(void){return 1;}
DN_EXPORT size_t dlm_native_offset_LittleMessage1Field8_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_LittleMessage1Field8_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_LittleMessage1Field9(void){return 24;}
DN_EXPORT size_t dlm_native_alignof_LittleMessage1Field9(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_LittleMessage1Field9(void){return 1;}
DN_EXPORT size_t dlm_native_offset_LittleMessage1Field9_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_LittleMessage1Field9_error(void){return 8;}
DN_EXPORT size_t dlm_native_sizeof_LittleMessage2(void){return 80;}
DN_EXPORT size_t dlm_native_alignof_LittleMessage2(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_LittleMessage2(void){return 1;}
DN_EXPORT size_t dlm_native_offset_LittleMessage2_inner(void){return 0;}
DN_EXPORT size_t dlm_native_sizeof_LittleMessage2Decoded(void){return 96;}
DN_EXPORT size_t dlm_native_alignof_LittleMessage2Decoded(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_LittleMessage2Decoded(void){return 1;}
DN_EXPORT size_t dlm_native_offset_LittleMessage2Decoded_value(void){return 0;}
DN_EXPORT size_t dlm_native_offset_LittleMessage2Decoded_error(void){return 80;}
int main(void){(void)dlm_native_thread_init();dn_boundary b={0};jmp_buf dn_jump;b.jump=&dn_jump;b.foreign_marker=dn_foreign_context;dn_current=&b;dn_fp_begin(&b);if(setjmp(dn_jump)){dn_boundary_end(&b,1);fprintf(stderr,"DLM native error: %s\n",dn_error);return 1;}(void)dn_f_main();dn_boundary_end(&b,0);(void)dlm_native_runtime_shutdown();return 0;}
