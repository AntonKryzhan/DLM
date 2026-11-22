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
typedef struct dn_r_NetworkNegotiation dn_r_NetworkNegotiation;
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
struct dn_r_NetworkNegotiation {
    uint32_t f_minor;
    dn_r_TransportRequirements f_requirements;
    dn_r_TransportBounds f_bounds;
    dn_r_TransportChoice f_choice;
    dn_r_SessionContract f_session;
    uint64_t f_maximum_session_messages;
    uint32_t f_pending_proofs;
    uint32_t f_unsupported_security;
    int32_t f_status;
    uint32_t f_component;
    uint32_t f_message;
    uint32_t f_field;
};
struct dn_r_ProtocolVersion {
    uint32_t f_major;
    uint32_t f_minor;
    uint32_t f_minimum_minor;
};
struct dn_r_WireFormat {
    uint32_t f_kind;
    uint32_t f_endian;
    uint32_t f_envelope_version;
    uint32_t f_unknown_fields;
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
typedef struct dn_t_arr6_r18_TransportCandidate { dn_r_TransportCandidate data[6]; } dn_t_arr6_r18_TransportCandidate;
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
_Static_assert(sizeof(dn_r_NetworkNegotiation)==120,"NetworkNegotiation ABI size");
_Static_assert(_Alignof(dn_r_NetworkNegotiation)==8,"NetworkNegotiation ABI alignment");
_Static_assert(offsetof(dn_r_NetworkNegotiation,f_minor)==0,"NetworkNegotiation.minor ABI offset");
_Static_assert(offsetof(dn_r_NetworkNegotiation,f_requirements)==4,"NetworkNegotiation.requirements ABI offset");
_Static_assert(offsetof(dn_r_NetworkNegotiation,f_bounds)==16,"NetworkNegotiation.bounds ABI offset");
_Static_assert(offsetof(dn_r_NetworkNegotiation,f_choice)==40,"NetworkNegotiation.choice ABI offset");
_Static_assert(offsetof(dn_r_NetworkNegotiation,f_session)==68,"NetworkNegotiation.session ABI offset");
_Static_assert(offsetof(dn_r_NetworkNegotiation,f_maximum_session_messages)==88,"NetworkNegotiation.maximum_session_messages ABI offset");
_Static_assert(offsetof(dn_r_NetworkNegotiation,f_pending_proofs)==96,"NetworkNegotiation.pending_proofs ABI offset");
_Static_assert(offsetof(dn_r_NetworkNegotiation,f_unsupported_security)==100,"NetworkNegotiation.unsupported_security ABI offset");
_Static_assert(offsetof(dn_r_NetworkNegotiation,f_status)==104,"NetworkNegotiation.status ABI offset");
_Static_assert(offsetof(dn_r_NetworkNegotiation,f_component)==108,"NetworkNegotiation.component ABI offset");
_Static_assert(offsetof(dn_r_NetworkNegotiation,f_message)==112,"NetworkNegotiation.message ABI offset");
_Static_assert(offsetof(dn_r_NetworkNegotiation,f_field)==116,"NetworkNegotiation.field ABI offset");
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
static size_t dn_f_network_find_message(dn_r_NetworkSpec const * v_spec,uint32_t v_id,uint32_t v_minor);
static size_t dn_f_network_find_field(dn_r_MessageSchema const * v_message,uint32_t v_id,uint32_t v_minor);
static dn_r_NetworkSpecCheck dn_f_network_spec_direction(dn_r_NetworkSpec const * v_sender,dn_r_NetworkSpec const * v_receiver,uint32_t v_minor,uint32_t v_role);
static dn_r_NetworkNegotiation dn_f_network_negotiation_failure(int32_t v_status,uint32_t v_component,uint32_t v_message,uint32_t v_field);
static dn_r_SessionContract dn_f_network_session_intersection(dn_r_SessionContract v_left,dn_r_SessionContract v_right);
static dn_r_NetworkNegotiation dn_f_network_spec_negotiate(dn_r_NetworkSpec const * v_local,dn_r_NetworkSpec const * v_remote,bool v_initiator,dn_t_arr6_r18_TransportCandidate v_candidates);
static dn_r_NetworkSpec dn_f_sample(uint32_t v_minor);
static uint8_t dn_f_main(void);
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
static size_t dn_cb_network_find_message(dn_r_NetworkSpec const * v_spec,uint32_t v_id,uint32_t v_minor);
static size_t dn_cb_network_find_field(dn_r_MessageSchema const * v_message,uint32_t v_id,uint32_t v_minor);
static dn_r_NetworkSpecCheck dn_cb_network_spec_direction(dn_r_NetworkSpec const * v_sender,dn_r_NetworkSpec const * v_receiver,uint32_t v_minor,uint32_t v_role);
static dn_r_NetworkNegotiation dn_cb_network_negotiation_failure(int32_t v_status,uint32_t v_component,uint32_t v_message,uint32_t v_field);
static dn_r_SessionContract dn_cb_network_session_intersection(dn_r_SessionContract v_left,dn_r_SessionContract v_right);
static dn_r_NetworkNegotiation dn_cb_network_spec_negotiate(dn_r_NetworkSpec const * v_local,dn_r_NetworkSpec const * v_remote,bool v_initiator,dn_t_arr6_r18_TransportCandidate v_candidates);
static dn_r_NetworkSpec dn_cb_sample(uint32_t v_minor);
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
static size_t dn_f_network_find_message(dn_r_NetworkSpec const * v_spec,uint32_t v_id,uint32_t v_minor) {
{
if(({ uint32_t dn_tmp_0=(v_spec)->f_message_count;
uint32_t dn_tmp_1=((uint32_t)(16ULL));
(dn_tmp_0 > dn_tmp_1); })){
{ size_t dn_tmp_2=((size_t)(16ULL));return dn_tmp_2; }
}
else{
}
{ size_t dn_tmp_5=((size_t)(0ULL));size_t dn_tmp_4=({ uint32_t dn_tmp_3=(v_spec)->f_message_count;
if((unsigned __int128)dn_tmp_3>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_3; });for(size_t v_index=dn_tmp_5;v_index<dn_tmp_4;v_index++){
dn_r_MessageSchema const * v_message=(&((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_6=&((v_spec)->f_messages);size_t dn_tmp_7=v_index;&dn_tmp_6->data[dn_index(dn_tmp_7,16)]; }))));
if((({ uint32_t dn_tmp_8=(v_message)->f_id;
uint32_t dn_tmp_9=v_id;
(dn_tmp_8 == dn_tmp_9); }) && ({ uint32_t dn_tmp_10=(v_message)->f_first_minor;
uint32_t dn_tmp_11=(v_message)->f_last_minor;
uint32_t dn_tmp_12=v_minor;
dn_f_network_minor_active(dn_tmp_10,dn_tmp_11,dn_tmp_12); }))){
{ size_t dn_tmp_13=v_index;return dn_tmp_13; }
}
else{
}
}
 }
{ size_t dn_tmp_15=({ uint32_t dn_tmp_14=(v_spec)->f_message_count;
if((unsigned __int128)dn_tmp_14>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_14; });return dn_tmp_15; }
}

dn_fail("missing native return");
}
static size_t dn_cb_network_find_message(dn_r_NetworkSpec const * v_spec,uint32_t v_id,uint32_t v_minor) {
 if(!dn_enter()){return (size_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (size_t){0};}
 size_t dn_result=dn_f_network_find_message(v_spec,v_id,v_minor);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static size_t dn_f_network_find_field(dn_r_MessageSchema const * v_message,uint32_t v_id,uint32_t v_minor) {
{
if(({ uint32_t dn_tmp_0=(v_message)->f_field_count;
uint32_t dn_tmp_1=((uint32_t)(32ULL));
(dn_tmp_0 > dn_tmp_1); })){
{ size_t dn_tmp_2=((size_t)(32ULL));return dn_tmp_2; }
}
else{
}
{ size_t dn_tmp_5=((size_t)(0ULL));size_t dn_tmp_4=({ uint32_t dn_tmp_3=(v_message)->f_field_count;
if((unsigned __int128)dn_tmp_3>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_3; });for(size_t v_index=dn_tmp_5;v_index<dn_tmp_4;v_index++){
dn_r_MessageField const * v_field=(&((*({ dn_t_arr32_r12_MessageField *dn_tmp_6=&((v_message)->f_fields);size_t dn_tmp_7=v_index;&dn_tmp_6->data[dn_index(dn_tmp_7,32)]; }))));
if((({ uint32_t dn_tmp_8=(v_field)->f_id;
uint32_t dn_tmp_9=v_id;
(dn_tmp_8 == dn_tmp_9); }) && ({ uint32_t dn_tmp_10=(v_field)->f_first_minor;
uint32_t dn_tmp_11=(v_field)->f_last_minor;
uint32_t dn_tmp_12=v_minor;
dn_f_network_minor_active(dn_tmp_10,dn_tmp_11,dn_tmp_12); }))){
{ size_t dn_tmp_13=v_index;return dn_tmp_13; }
}
else{
}
}
 }
{ size_t dn_tmp_15=({ uint32_t dn_tmp_14=(v_message)->f_field_count;
if((unsigned __int128)dn_tmp_14>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_14; });return dn_tmp_15; }
}

dn_fail("missing native return");
}
static size_t dn_cb_network_find_field(dn_r_MessageSchema const * v_message,uint32_t v_id,uint32_t v_minor) {
 if(!dn_enter()){return (size_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (size_t){0};}
 size_t dn_result=dn_f_network_find_field(v_message,v_id,v_minor);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkSpecCheck dn_f_network_spec_direction(dn_r_NetworkSpec const * v_sender,dn_r_NetworkSpec const * v_receiver,uint32_t v_minor,uint32_t v_role) {
{
if(((({ uint32_t dn_tmp_0=(v_sender)->f_message_count;
uint32_t dn_tmp_1=((uint32_t)(16ULL));
(dn_tmp_0 > dn_tmp_1); }) || ({ uint32_t dn_tmp_2=(v_receiver)->f_message_count;
uint32_t dn_tmp_3=((uint32_t)(16ULL));
(dn_tmp_2 > dn_tmp_3); })) || (({ uint32_t dn_tmp_4=v_role;
uint32_t dn_tmp_5=((uint32_t)(1ULL));
(dn_tmp_4 != dn_tmp_5); }) && ({ uint32_t dn_tmp_6=v_role;
uint32_t dn_tmp_7=((uint32_t)(2ULL));
(dn_tmp_6 != dn_tmp_7); })))){
{ dn_r_NetworkSpecCheck dn_tmp_12=({ int32_t dn_tmp_8=((int32_t)(4ULL));
uint32_t dn_tmp_9=((uint32_t)(6ULL));
uint32_t dn_tmp_10=((uint32_t)(0ULL));
uint32_t dn_tmp_11=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_11); });return dn_tmp_12; }
}
else{
}
dn_r_NetworkSpecCheck v_result=({ ((dn_r_NetworkSpecCheck){0}); });
{ size_t dn_tmp_15=((size_t)(0ULL));size_t dn_tmp_14=({ uint32_t dn_tmp_13=(v_sender)->f_message_count;
if((unsigned __int128)dn_tmp_13>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_13; });for(size_t v_index=dn_tmp_15;v_index<dn_tmp_14;v_index++){
dn_r_MessageSchema const * v_source=(&((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_16=&((v_sender)->f_messages);size_t dn_tmp_17=v_index;&dn_tmp_16->data[dn_index(dn_tmp_17,16)]; }))));
if(((!(({ uint32_t dn_tmp_18=(v_source)->f_first_minor;
uint32_t dn_tmp_19=(v_source)->f_last_minor;
uint32_t dn_tmp_20=v_minor;
dn_f_network_minor_active(dn_tmp_18,dn_tmp_19,dn_tmp_20); }))) || ({ uint32_t dn_tmp_21=({ uint32_t dn_tmp_22=(v_source)->f_senders;
uint32_t dn_tmp_23=v_role;
dn_U32_bits((uint64_t)dn_tmp_22 & (uint64_t)dn_tmp_23); });
uint32_t dn_tmp_24=((uint32_t)(0ULL));
(dn_tmp_21 == dn_tmp_24); }))){
continue;
}
else{
}
size_t v_found=({ dn_r_NetworkSpec const * dn_tmp_25=v_receiver;
uint32_t dn_tmp_26=(v_source)->f_id;
uint32_t dn_tmp_27=v_minor;
dn_f_network_find_message(dn_tmp_25,dn_tmp_26,dn_tmp_27); });
if(({ size_t dn_tmp_28=v_found;
size_t dn_tmp_29=({ uint32_t dn_tmp_30=(v_receiver)->f_message_count;
if((unsigned __int128)dn_tmp_30>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_30; });
(dn_tmp_28 == dn_tmp_29); })){
{ dn_r_NetworkSpecCheck dn_tmp_35=({ int32_t dn_tmp_31=((int32_t)(11ULL));
uint32_t dn_tmp_32=((uint32_t)(6ULL));
uint32_t dn_tmp_33=(v_source)->f_id;
uint32_t dn_tmp_34=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_31,dn_tmp_32,dn_tmp_33,dn_tmp_34); });return dn_tmp_35; }
}
else{
}
dn_r_MessageSchema const * v_target=(&((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_36=&((v_receiver)->f_messages);size_t dn_tmp_37=v_found;&dn_tmp_36->data[dn_index(dn_tmp_37,16)]; }))));
if((((({ uint32_t dn_tmp_38=(v_source)->f_field_count;
uint32_t dn_tmp_39=((uint32_t)(32ULL));
(dn_tmp_38 > dn_tmp_39); }) || ({ uint32_t dn_tmp_40=(v_target)->f_field_count;
uint32_t dn_tmp_41=((uint32_t)(32ULL));
(dn_tmp_40 > dn_tmp_41); })) || ({ uint32_t dn_tmp_42=(v_source)->f_phase;
uint32_t dn_tmp_43=(v_target)->f_phase;
(dn_tmp_42 != dn_tmp_43); })) || ({ uint32_t dn_tmp_44=({ uint32_t dn_tmp_45=(v_target)->f_senders;
uint32_t dn_tmp_46=v_role;
dn_U32_bits((uint64_t)dn_tmp_45 & (uint64_t)dn_tmp_46); });
uint32_t dn_tmp_47=((uint32_t)(0ULL));
(dn_tmp_44 == dn_tmp_47); }))){
{ dn_r_NetworkSpecCheck dn_tmp_52=({ int32_t dn_tmp_48=((int32_t)(11ULL));
uint32_t dn_tmp_49=((uint32_t)(6ULL));
uint32_t dn_tmp_50=(v_source)->f_id;
uint32_t dn_tmp_51=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_48,dn_tmp_49,dn_tmp_50,dn_tmp_51); });return dn_tmp_52; }
}
else{
}
dn_r_SchemaMeasure v_measured=({ dn_r_MessageSchema const * dn_tmp_53=v_source;
dn_r_WireFormat dn_tmp_54=(v_sender)->f_wire;
uint32_t dn_tmp_55=v_minor;
dn_f_network_message_measure(dn_tmp_53,dn_tmp_54,dn_tmp_55); });
if(({ int32_t dn_tmp_56=(v_measured).f_status;
int32_t dn_tmp_57=((int32_t)(0ULL));
(dn_tmp_56 != dn_tmp_57); })){
{ dn_r_NetworkSpecCheck dn_tmp_62=({ int32_t dn_tmp_58=(v_measured).f_status;
uint32_t dn_tmp_59=((uint32_t)(7ULL));
uint32_t dn_tmp_60=(v_source)->f_id;
uint32_t dn_tmp_61=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_58,dn_tmp_59,dn_tmp_60,dn_tmp_61); });return dn_tmp_62; }
}
else{
}
uint64_t v_maximum=({ uint64_t dn_tmp_63=(v_measured).f_maximum;
uint64_t dn_tmp_64=({ uint32_t dn_tmp_65=((v_sender)->f_resources).f_maximum_unknown_bytes;
if((unsigned __int128)dn_tmp_65>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_65; });
dn_U64_add(dn_tmp_63,dn_tmp_64); });
if(({ uint64_t dn_tmp_66=v_maximum;
uint64_t dn_tmp_67=({ uint32_t dn_tmp_68=(((v_receiver)->f_resources).f_transport).f_max_message;
if((unsigned __int128)dn_tmp_68>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_68; });
(dn_tmp_66 > dn_tmp_67); })){
{ dn_r_NetworkSpecCheck dn_tmp_73=({ int32_t dn_tmp_69=((int32_t)(7ULL));
uint32_t dn_tmp_70=((uint32_t)(3ULL));
uint32_t dn_tmp_71=(v_source)->f_id;
uint32_t dn_tmp_72=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_69,dn_tmp_70,dn_tmp_71,dn_tmp_72); });return dn_tmp_73; }
}
else{
}
if(({ uint64_t dn_tmp_74=v_maximum;
uint64_t dn_tmp_75=({ size_t dn_tmp_76=(v_result).f_maximum;
if((unsigned __int128)dn_tmp_76>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_76; });
(dn_tmp_74 > dn_tmp_75); })){
{ size_t dn_tmp_79=({ uint64_t dn_tmp_77=v_maximum;
if((unsigned __int128)dn_tmp_77>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_77; });size_t *dn_tmp_78=&((v_result).f_maximum);*dn_tmp_78=dn_tmp_79; }
}
else{
}
uint64_t v_unknown_fields=({ uint32_t dn_tmp_80=((v_sender)->f_resources).f_maximum_unknown_fields;
if((unsigned __int128)dn_tmp_80>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_80; });
uint64_t v_unknown_bytes=({ uint32_t dn_tmp_81=((v_sender)->f_resources).f_maximum_unknown_bytes;
if((unsigned __int128)dn_tmp_81>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_81; });
{ size_t dn_tmp_84=((size_t)(0ULL));size_t dn_tmp_83=({ uint32_t dn_tmp_82=(v_source)->f_field_count;
if((unsigned __int128)dn_tmp_82>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_82; });for(size_t v_field_index=dn_tmp_84;v_field_index<dn_tmp_83;v_field_index++){
dn_r_MessageField const * v_field=(&((*({ dn_t_arr32_r12_MessageField *dn_tmp_85=&((v_source)->f_fields);size_t dn_tmp_86=v_field_index;&dn_tmp_85->data[dn_index(dn_tmp_86,32)]; }))));
if((!(({ uint32_t dn_tmp_87=(v_field)->f_first_minor;
uint32_t dn_tmp_88=(v_field)->f_last_minor;
uint32_t dn_tmp_89=v_minor;
dn_f_network_minor_active(dn_tmp_87,dn_tmp_88,dn_tmp_89); })))){
continue;
}
else{
}
size_t v_match_index=({ dn_r_MessageSchema const * dn_tmp_90=v_target;
uint32_t dn_tmp_91=(v_field)->f_id;
uint32_t dn_tmp_92=v_minor;
dn_f_network_find_field(dn_tmp_90,dn_tmp_91,dn_tmp_92); });
if(({ size_t dn_tmp_93=v_match_index;
size_t dn_tmp_94=({ uint32_t dn_tmp_95=(v_target)->f_field_count;
if((unsigned __int128)dn_tmp_95>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_95; });
(dn_tmp_93 == dn_tmp_94); })){
if((({ uint32_t dn_tmp_96=((v_sender)->f_wire).f_kind;
uint32_t dn_tmp_97=((uint32_t)(2ULL));
(dn_tmp_96 != dn_tmp_97); }) || ({ uint32_t dn_tmp_98=((v_receiver)->f_wire).f_unknown_fields;
uint32_t dn_tmp_99=((uint32_t)(1ULL));
(dn_tmp_98 != dn_tmp_99); }))){
{ dn_r_NetworkSpecCheck dn_tmp_104=({ int32_t dn_tmp_100=((int32_t)(11ULL));
uint32_t dn_tmp_101=((uint32_t)(7ULL));
uint32_t dn_tmp_102=(v_source)->f_id;
uint32_t dn_tmp_103=(v_field)->f_id;
dn_f_network_spec_failure(dn_tmp_100,dn_tmp_101,dn_tmp_102,dn_tmp_103); });return dn_tmp_104; }
}
else{
}
dn_r_SchemaMeasure v_size=({ dn_r_MessageField const * dn_tmp_105=v_field;
dn_r_WireFormat dn_tmp_106=(v_sender)->f_wire;
dn_f_network_field_measure(dn_tmp_105,dn_tmp_106); });
{ uint64_t dn_tmp_111=({ uint64_t dn_tmp_107=v_unknown_fields;
uint64_t dn_tmp_108=({ uint32_t dn_tmp_109=(v_field)->f_maximum_count;
if((unsigned __int128)dn_tmp_109>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_109; });
dn_U64_add(dn_tmp_107,dn_tmp_108); });uint64_t *dn_tmp_110=&(v_unknown_fields);*dn_tmp_110=dn_tmp_111; }
{ uint64_t dn_tmp_115=({ uint64_t dn_tmp_112=v_unknown_bytes;
uint64_t dn_tmp_113=(v_size).f_maximum;
dn_U64_add(dn_tmp_112,dn_tmp_113); });uint64_t *dn_tmp_114=&(v_unknown_bytes);*dn_tmp_114=dn_tmp_115; }
}
else{
dn_r_MessageField const * v_accepted=(&((*({ dn_t_arr32_r12_MessageField *dn_tmp_116=&((v_target)->f_fields);size_t dn_tmp_117=v_match_index;&dn_tmp_116->data[dn_index(dn_tmp_117,32)]; }))));
if((((({ uint32_t dn_tmp_118=(v_field)->f_kind;
uint32_t dn_tmp_119=(v_accepted)->f_kind;
(dn_tmp_118 != dn_tmp_119); }) || ({ uint32_t dn_tmp_120=(v_field)->f_minimum_count;
uint32_t dn_tmp_121=(v_accepted)->f_minimum_count;
(dn_tmp_120 < dn_tmp_121); })) || ({ uint32_t dn_tmp_122=(v_field)->f_maximum_count;
uint32_t dn_tmp_123=(v_accepted)->f_maximum_count;
(dn_tmp_122 > dn_tmp_123); })) || ({ uint32_t dn_tmp_124=(v_field)->f_maximum_length;
uint32_t dn_tmp_125=(v_accepted)->f_maximum_length;
(dn_tmp_124 > dn_tmp_125); }))){
{ dn_r_NetworkSpecCheck dn_tmp_130=({ int32_t dn_tmp_126=((int32_t)(11ULL));
uint32_t dn_tmp_127=((uint32_t)(7ULL));
uint32_t dn_tmp_128=(v_source)->f_id;
uint32_t dn_tmp_129=(v_field)->f_id;
dn_f_network_spec_failure(dn_tmp_126,dn_tmp_127,dn_tmp_128,dn_tmp_129); });return dn_tmp_130; }
}
else{
}
if(({ uint32_t dn_tmp_131=((v_sender)->f_wire).f_kind;
uint32_t dn_tmp_132=((uint32_t)(1ULL));
(dn_tmp_131 == dn_tmp_132); })){
bool v_a=(({ uint32_t dn_tmp_133=(v_field)->f_minimum_count;
uint32_t dn_tmp_134=((uint32_t)(1ULL));
(dn_tmp_133 == dn_tmp_134); }) && ({ uint32_t dn_tmp_135=(v_field)->f_maximum_count;
uint32_t dn_tmp_136=((uint32_t)(1ULL));
(dn_tmp_135 == dn_tmp_136); }));
bool v_b=(({ uint32_t dn_tmp_137=(v_accepted)->f_minimum_count;
uint32_t dn_tmp_138=((uint32_t)(1ULL));
(dn_tmp_137 == dn_tmp_138); }) && ({ uint32_t dn_tmp_139=(v_accepted)->f_maximum_count;
uint32_t dn_tmp_140=((uint32_t)(1ULL));
(dn_tmp_139 == dn_tmp_140); }));
if(({ bool dn_tmp_141=v_a;
bool dn_tmp_142=v_b;
(dn_tmp_141 != dn_tmp_142); })){
{ dn_r_NetworkSpecCheck dn_tmp_147=({ int32_t dn_tmp_143=((int32_t)(11ULL));
uint32_t dn_tmp_144=((uint32_t)(7ULL));
uint32_t dn_tmp_145=(v_source)->f_id;
uint32_t dn_tmp_146=(v_field)->f_id;
dn_f_network_spec_failure(dn_tmp_143,dn_tmp_144,dn_tmp_145,dn_tmp_146); });return dn_tmp_147; }
}
else{
}
}
else{
}
}
}
 }
{ size_t dn_tmp_150=((size_t)(0ULL));size_t dn_tmp_149=({ uint32_t dn_tmp_148=(v_target)->f_field_count;
if((unsigned __int128)dn_tmp_148>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_148; });for(size_t v_field_index=dn_tmp_150;v_field_index<dn_tmp_149;v_field_index++){
dn_r_MessageField const * v_field=(&((*({ dn_t_arr32_r12_MessageField *dn_tmp_151=&((v_target)->f_fields);size_t dn_tmp_152=v_field_index;&dn_tmp_151->data[dn_index(dn_tmp_152,32)]; }))));
if((!(({ uint32_t dn_tmp_153=(v_field)->f_first_minor;
uint32_t dn_tmp_154=(v_field)->f_last_minor;
uint32_t dn_tmp_155=v_minor;
dn_f_network_minor_active(dn_tmp_153,dn_tmp_154,dn_tmp_155); })))){
continue;
}
else{
}
if(({ size_t dn_tmp_156=({ dn_r_MessageSchema const * dn_tmp_157=v_source;
uint32_t dn_tmp_158=(v_field)->f_id;
uint32_t dn_tmp_159=v_minor;
dn_f_network_find_field(dn_tmp_157,dn_tmp_158,dn_tmp_159); });
size_t dn_tmp_160=({ uint32_t dn_tmp_161=(v_source)->f_field_count;
if((unsigned __int128)dn_tmp_161>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_161; });
(dn_tmp_156 == dn_tmp_160); })){
if(((({ uint32_t dn_tmp_162=((v_sender)->f_wire).f_kind;
uint32_t dn_tmp_163=((uint32_t)(1ULL));
(dn_tmp_162 == dn_tmp_163); }) || ({ uint32_t dn_tmp_164=(v_field)->f_minimum_count;
uint32_t dn_tmp_165=((uint32_t)(0ULL));
(dn_tmp_164 != dn_tmp_165); })) || ({ uint32_t dn_tmp_166=((v_sender)->f_resources).f_maximum_unknown_fields;
uint32_t dn_tmp_167=((uint32_t)(0ULL));
(dn_tmp_166 != dn_tmp_167); }))){
{ dn_r_NetworkSpecCheck dn_tmp_172=({ int32_t dn_tmp_168=((int32_t)(11ULL));
uint32_t dn_tmp_169=((uint32_t)(7ULL));
uint32_t dn_tmp_170=(v_source)->f_id;
uint32_t dn_tmp_171=(v_field)->f_id;
dn_f_network_spec_failure(dn_tmp_168,dn_tmp_169,dn_tmp_170,dn_tmp_171); });return dn_tmp_172; }
}
else{
}
}
else{
}
}
 }
if((({ uint64_t dn_tmp_173=v_unknown_fields;
uint64_t dn_tmp_174=({ uint32_t dn_tmp_175=((v_receiver)->f_resources).f_maximum_unknown_fields;
if((unsigned __int128)dn_tmp_175>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_175; });
(dn_tmp_173 > dn_tmp_174); }) || ({ uint64_t dn_tmp_176=v_unknown_bytes;
uint64_t dn_tmp_177=({ uint32_t dn_tmp_178=((v_receiver)->f_resources).f_maximum_unknown_bytes;
if((unsigned __int128)dn_tmp_178>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_178; });
(dn_tmp_176 > dn_tmp_177); }))){
{ dn_r_NetworkSpecCheck dn_tmp_183=({ int32_t dn_tmp_179=((int32_t)(7ULL));
uint32_t dn_tmp_180=((uint32_t)(3ULL));
uint32_t dn_tmp_181=(v_source)->f_id;
uint32_t dn_tmp_182=((uint32_t)(0ULL));
dn_f_network_spec_failure(dn_tmp_179,dn_tmp_180,dn_tmp_181,dn_tmp_182); });return dn_tmp_183; }
}
else{
}
}
 }
{ dn_r_NetworkSpecCheck dn_tmp_184=v_result;return dn_tmp_184; }
}

dn_fail("missing native return");
}
static dn_r_NetworkSpecCheck dn_cb_network_spec_direction(dn_r_NetworkSpec const * v_sender,dn_r_NetworkSpec const * v_receiver,uint32_t v_minor,uint32_t v_role) {
 if(!dn_enter()){return (dn_r_NetworkSpecCheck){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkSpecCheck){0};}
 dn_r_NetworkSpecCheck dn_result=dn_f_network_spec_direction(v_sender,v_receiver,v_minor,v_role);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkNegotiation dn_f_network_negotiation_failure(int32_t v_status,uint32_t v_component,uint32_t v_message,uint32_t v_field) {
{
dn_r_NetworkNegotiation v_result=({ ((dn_r_NetworkNegotiation){0}); });
{ int32_t dn_tmp_1=v_status;int32_t *dn_tmp_0=&((v_result).f_status);*dn_tmp_0=dn_tmp_1; }
{ int32_t dn_tmp_3=v_status;int32_t *dn_tmp_2=&(((v_result).f_choice).f_status);*dn_tmp_2=dn_tmp_3; }
{ uint32_t dn_tmp_5=v_component;uint32_t *dn_tmp_4=&((v_result).f_component);*dn_tmp_4=dn_tmp_5; }
{ uint32_t dn_tmp_7=v_message;uint32_t *dn_tmp_6=&((v_result).f_message);*dn_tmp_6=dn_tmp_7; }
{ uint32_t dn_tmp_9=v_field;uint32_t *dn_tmp_8=&((v_result).f_field);*dn_tmp_8=dn_tmp_9; }
{ dn_r_NetworkNegotiation dn_tmp_10=v_result;return dn_tmp_10; }
}

dn_fail("missing native return");
}
static dn_r_NetworkNegotiation dn_cb_network_negotiation_failure(int32_t v_status,uint32_t v_component,uint32_t v_message,uint32_t v_field) {
 if(!dn_enter()){return (dn_r_NetworkNegotiation){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkNegotiation){0};}
 dn_r_NetworkNegotiation dn_result=dn_f_network_negotiation_failure(v_status,v_component,v_message,v_field);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_SessionContract dn_f_network_session_intersection(dn_r_SessionContract v_left,dn_r_SessionContract v_right) {
{
dn_r_SessionContract v_result=v_left;
if(({ uint32_t dn_tmp_0=(v_right).f_handshake_timeout_ms;
uint32_t dn_tmp_1=(v_result).f_handshake_timeout_ms;
(dn_tmp_0 < dn_tmp_1); })){
{ uint32_t dn_tmp_3=(v_right).f_handshake_timeout_ms;uint32_t *dn_tmp_2=&((v_result).f_handshake_timeout_ms);*dn_tmp_2=dn_tmp_3; }
}
else{
}
if(({ uint32_t dn_tmp_4=(v_right).f_idle_timeout_ms;
uint32_t dn_tmp_5=(v_result).f_idle_timeout_ms;
(dn_tmp_4 < dn_tmp_5); })){
{ uint32_t dn_tmp_7=(v_right).f_idle_timeout_ms;uint32_t *dn_tmp_6=&((v_result).f_idle_timeout_ms);*dn_tmp_6=dn_tmp_7; }
}
else{
}
if(({ uint32_t dn_tmp_8=(v_right).f_maximum_duration_ms;
uint32_t dn_tmp_9=(v_result).f_maximum_duration_ms;
(dn_tmp_8 < dn_tmp_9); })){
{ uint32_t dn_tmp_11=(v_right).f_maximum_duration_ms;uint32_t *dn_tmp_10=&((v_result).f_maximum_duration_ms);*dn_tmp_10=dn_tmp_11; }
}
else{
}
if(({ uint32_t dn_tmp_12=(v_right).f_maximum_in_flight;
uint32_t dn_tmp_13=(v_result).f_maximum_in_flight;
(dn_tmp_12 < dn_tmp_13); })){
{ uint32_t dn_tmp_15=(v_right).f_maximum_in_flight;uint32_t *dn_tmp_14=&((v_result).f_maximum_in_flight);*dn_tmp_14=dn_tmp_15; }
}
else{
}
{ bool dn_tmp_17=((v_left).f_authentication_before_data || (v_right).f_authentication_before_data);bool *dn_tmp_16=&((v_result).f_authentication_before_data);*dn_tmp_16=dn_tmp_17; }
{ dn_r_SessionContract dn_tmp_18=v_result;return dn_tmp_18; }
}

dn_fail("missing native return");
}
static dn_r_SessionContract dn_cb_network_session_intersection(dn_r_SessionContract v_left,dn_r_SessionContract v_right) {
 if(!dn_enter()){return (dn_r_SessionContract){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_SessionContract){0};}
 dn_r_SessionContract dn_result=dn_f_network_session_intersection(v_left,v_right);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkNegotiation dn_f_network_spec_negotiate(dn_r_NetworkSpec const * v_local,dn_r_NetworkSpec const * v_remote,bool v_initiator,dn_t_arr6_r18_TransportCandidate v_candidates) {
{
dn_r_NetworkSpecCheck v_a=({ dn_r_NetworkSpec const * dn_tmp_0=v_local;
dn_f_network_spec_check(dn_tmp_0); });
dn_r_NetworkSpecCheck v_b=({ dn_r_NetworkSpec const * dn_tmp_1=v_remote;
dn_f_network_spec_check(dn_tmp_1); });
if(({ int32_t dn_tmp_2=(v_a).f_status;
int32_t dn_tmp_3=((int32_t)(0ULL));
(dn_tmp_2 != dn_tmp_3); })){
{ dn_r_NetworkNegotiation dn_tmp_8=({ int32_t dn_tmp_4=(v_a).f_status;
uint32_t dn_tmp_5=(v_a).f_component;
uint32_t dn_tmp_6=(v_a).f_message;
uint32_t dn_tmp_7=(v_a).f_field;
dn_f_network_negotiation_failure(dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7); });return dn_tmp_8; }
}
else{
}
if(({ int32_t dn_tmp_9=(v_b).f_status;
int32_t dn_tmp_10=((int32_t)(0ULL));
(dn_tmp_9 != dn_tmp_10); })){
{ dn_r_NetworkNegotiation dn_tmp_15=({ int32_t dn_tmp_11=(v_b).f_status;
uint32_t dn_tmp_12=(v_b).f_component;
uint32_t dn_tmp_13=(v_b).f_message;
uint32_t dn_tmp_14=(v_b).f_field;
dn_f_network_negotiation_failure(dn_tmp_11,dn_tmp_12,dn_tmp_13,dn_tmp_14); });return dn_tmp_15; }
}
else{
}
if((({ uint32_t dn_tmp_16=(v_local)->f_protocol;
uint32_t dn_tmp_17=(v_remote)->f_protocol;
(dn_tmp_16 != dn_tmp_17); }) || ({ uint32_t dn_tmp_18=((v_local)->f_version).f_major;
uint32_t dn_tmp_19=((v_remote)->f_version).f_major;
(dn_tmp_18 != dn_tmp_19); }))){
{ dn_r_NetworkNegotiation dn_tmp_24=({ int32_t dn_tmp_20=((int32_t)(11ULL));
uint32_t dn_tmp_21=((uint32_t)(1ULL));
uint32_t dn_tmp_22=((uint32_t)(0ULL));
uint32_t dn_tmp_23=((uint32_t)(0ULL));
dn_f_network_negotiation_failure(dn_tmp_20,dn_tmp_21,dn_tmp_22,dn_tmp_23); });return dn_tmp_24; }
}
else{
}
uint32_t v_minor=((v_local)->f_version).f_minor;
if(({ uint32_t dn_tmp_25=((v_remote)->f_version).f_minor;
uint32_t dn_tmp_26=v_minor;
(dn_tmp_25 < dn_tmp_26); })){
{ uint32_t dn_tmp_28=((v_remote)->f_version).f_minor;uint32_t *dn_tmp_27=&(v_minor);*dn_tmp_27=dn_tmp_28; }
}
else{
}
if((({ uint32_t dn_tmp_29=v_minor;
uint32_t dn_tmp_30=((v_local)->f_version).f_minimum_minor;
(dn_tmp_29 < dn_tmp_30); }) || ({ uint32_t dn_tmp_31=v_minor;
uint32_t dn_tmp_32=((v_remote)->f_version).f_minimum_minor;
(dn_tmp_31 < dn_tmp_32); }))){
{ dn_r_NetworkNegotiation dn_tmp_37=({ int32_t dn_tmp_33=((int32_t)(11ULL));
uint32_t dn_tmp_34=((uint32_t)(1ULL));
uint32_t dn_tmp_35=((uint32_t)(0ULL));
uint32_t dn_tmp_36=((uint32_t)(0ULL));
dn_f_network_negotiation_failure(dn_tmp_33,dn_tmp_34,dn_tmp_35,dn_tmp_36); });return dn_tmp_37; }
}
else{
}
if(((({ uint32_t dn_tmp_38=((v_local)->f_wire).f_kind;
uint32_t dn_tmp_39=((v_remote)->f_wire).f_kind;
(dn_tmp_38 != dn_tmp_39); }) || ({ uint32_t dn_tmp_40=((v_local)->f_wire).f_endian;
uint32_t dn_tmp_41=((v_remote)->f_wire).f_endian;
(dn_tmp_40 != dn_tmp_41); })) || ({ uint32_t dn_tmp_42=((v_local)->f_wire).f_envelope_version;
uint32_t dn_tmp_43=((v_remote)->f_wire).f_envelope_version;
(dn_tmp_42 != dn_tmp_43); }))){
{ dn_r_NetworkNegotiation dn_tmp_48=({ int32_t dn_tmp_44=((int32_t)(11ULL));
uint32_t dn_tmp_45=((uint32_t)(2ULL));
uint32_t dn_tmp_46=((uint32_t)(0ULL));
uint32_t dn_tmp_47=((uint32_t)(0ULL));
dn_f_network_negotiation_failure(dn_tmp_44,dn_tmp_45,dn_tmp_46,dn_tmp_47); });return dn_tmp_48; }
}
else{
}
uint32_t v_role=((uint32_t)(1ULL));
if((!(v_initiator))){
{ uint32_t dn_tmp_50=((uint32_t)(2ULL));uint32_t *dn_tmp_49=&(v_role);*dn_tmp_49=dn_tmp_50; }
}
else{
}
dn_r_NetworkSpecCheck v_outgoing=({ dn_r_NetworkSpec const * dn_tmp_51=v_local;
dn_r_NetworkSpec const * dn_tmp_52=v_remote;
uint32_t dn_tmp_53=v_minor;
uint32_t dn_tmp_54=v_role;
dn_f_network_spec_direction(dn_tmp_51,dn_tmp_52,dn_tmp_53,dn_tmp_54); });
if(({ int32_t dn_tmp_55=(v_outgoing).f_status;
int32_t dn_tmp_56=((int32_t)(0ULL));
(dn_tmp_55 != dn_tmp_56); })){
{ dn_r_NetworkNegotiation dn_tmp_61=({ int32_t dn_tmp_57=(v_outgoing).f_status;
uint32_t dn_tmp_58=(v_outgoing).f_component;
uint32_t dn_tmp_59=(v_outgoing).f_message;
uint32_t dn_tmp_60=(v_outgoing).f_field;
dn_f_network_negotiation_failure(dn_tmp_57,dn_tmp_58,dn_tmp_59,dn_tmp_60); });return dn_tmp_61; }
}
else{
}
dn_r_NetworkSpecCheck v_incoming=({ dn_r_NetworkSpec const * dn_tmp_62=v_remote;
dn_r_NetworkSpec const * dn_tmp_63=v_local;
uint32_t dn_tmp_64=v_minor;
uint32_t dn_tmp_65=({ uint32_t dn_tmp_66=({ uint32_t dn_tmp_67=((uint32_t)(3ULL));
if((unsigned __int128)dn_tmp_67>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_67; });
uint32_t dn_tmp_68=v_role;
dn_U32_bits((uint64_t)dn_tmp_66 ^ (uint64_t)dn_tmp_68); });
dn_f_network_spec_direction(dn_tmp_62,dn_tmp_63,dn_tmp_64,dn_tmp_65); });
if(({ int32_t dn_tmp_69=(v_incoming).f_status;
int32_t dn_tmp_70=((int32_t)(0ULL));
(dn_tmp_69 != dn_tmp_70); })){
{ dn_r_NetworkNegotiation dn_tmp_75=({ int32_t dn_tmp_71=(v_incoming).f_status;
uint32_t dn_tmp_72=(v_incoming).f_component;
uint32_t dn_tmp_73=(v_incoming).f_message;
uint32_t dn_tmp_74=(v_incoming).f_field;
dn_f_network_negotiation_failure(dn_tmp_71,dn_tmp_72,dn_tmp_73,dn_tmp_74); });return dn_tmp_75; }
}
else{
}
dn_r_NetworkNegotiation v_result=({ ((dn_r_NetworkNegotiation){0}); });
{ uint32_t dn_tmp_77=v_minor;uint32_t *dn_tmp_76=&((v_result).f_minor);*dn_tmp_76=dn_tmp_77; }
uint32_t v_required=({ uint32_t dn_tmp_78=(v_a).f_required_properties;
uint32_t dn_tmp_79=(v_b).f_required_properties;
dn_U32_bits((uint64_t)dn_tmp_78 | (uint64_t)dn_tmp_79); });
uint32_t v_forbidden=({ uint32_t dn_tmp_80=((v_local)->f_transport).f_forbidden;
uint32_t dn_tmp_81=((v_remote)->f_transport).f_forbidden;
dn_U32_bits((uint64_t)dn_tmp_80 | (uint64_t)dn_tmp_81); });
if((({ uint32_t dn_tmp_82=({ uint32_t dn_tmp_83=v_required;
uint32_t dn_tmp_84=v_forbidden;
dn_U32_bits((uint64_t)dn_tmp_83 & (uint64_t)dn_tmp_84); });
uint32_t dn_tmp_85=((uint32_t)(0ULL));
(dn_tmp_82 != dn_tmp_85); }) || ({ uint32_t dn_tmp_86=({ uint32_t dn_tmp_87=v_required;
uint32_t dn_tmp_88=((uint32_t)(66ULL));
dn_U32_bits((uint64_t)dn_tmp_87 & (uint64_t)dn_tmp_88); });
uint32_t dn_tmp_89=((uint32_t)(66ULL));
(dn_tmp_86 == dn_tmp_89); }))){
{ dn_r_NetworkNegotiation dn_tmp_94=({ int32_t dn_tmp_90=((int32_t)(11ULL));
uint32_t dn_tmp_91=((uint32_t)(3ULL));
uint32_t dn_tmp_92=((uint32_t)(0ULL));
uint32_t dn_tmp_93=((uint32_t)(0ULL));
dn_f_network_negotiation_failure(dn_tmp_90,dn_tmp_91,dn_tmp_92,dn_tmp_93); });return dn_tmp_94; }
}
else{
}
{ dn_r_TransportRequirements dn_tmp_106=({ uint32_t dn_tmp_95=v_required;
uint32_t dn_tmp_96=v_forbidden;
uint32_t dn_tmp_97=({ uint32_t dn_tmp_98=({ uint32_t dn_tmp_99=((v_local)->f_transport).f_preferred;
uint32_t dn_tmp_100=((v_remote)->f_transport).f_preferred;
dn_U32_bits((uint64_t)dn_tmp_99 | (uint64_t)dn_tmp_100); });
uint32_t dn_tmp_101=({ uint32_t dn_tmp_102=({ uint32_t dn_tmp_103=((uint32_t)(511ULL));
if((unsigned __int128)dn_tmp_103>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_103; });
uint32_t dn_tmp_104=v_forbidden;
dn_U32_bits((uint64_t)dn_tmp_102 ^ (uint64_t)dn_tmp_104); });
dn_U32_bits((uint64_t)dn_tmp_98 & (uint64_t)dn_tmp_101); });
((dn_r_TransportRequirements){dn_tmp_95,dn_tmp_96,dn_tmp_97}); });dn_r_TransportRequirements *dn_tmp_105=&((v_result).f_requirements);*dn_tmp_105=dn_tmp_106; }
{ dn_r_TransportBounds dn_tmp_108=((v_local)->f_resources).f_transport;dn_r_TransportBounds *dn_tmp_107=&((v_result).f_bounds);*dn_tmp_107=dn_tmp_108; }
if(({ size_t dn_tmp_109=(((v_remote)->f_resources).f_transport).f_memory;
size_t dn_tmp_110=((v_result).f_bounds).f_memory;
(dn_tmp_109 < dn_tmp_110); })){
{ size_t dn_tmp_112=(((v_remote)->f_resources).f_transport).f_memory;size_t *dn_tmp_111=&(((v_result).f_bounds).f_memory);*dn_tmp_111=dn_tmp_112; }
}
else{
}
if(({ uint32_t dn_tmp_113=(((v_remote)->f_resources).f_transport).f_queued_messages;
uint32_t dn_tmp_114=((v_result).f_bounds).f_queued_messages;
(dn_tmp_113 < dn_tmp_114); })){
{ uint32_t dn_tmp_116=(((v_remote)->f_resources).f_transport).f_queued_messages;uint32_t *dn_tmp_115=&(((v_result).f_bounds).f_queued_messages);*dn_tmp_115=dn_tmp_116; }
}
else{
}
if(({ uint32_t dn_tmp_117=(((v_remote)->f_resources).f_transport).f_channels;
uint32_t dn_tmp_118=((v_result).f_bounds).f_channels;
(dn_tmp_117 < dn_tmp_118); })){
{ uint32_t dn_tmp_120=(((v_remote)->f_resources).f_transport).f_channels;uint32_t *dn_tmp_119=&(((v_result).f_bounds).f_channels);*dn_tmp_119=dn_tmp_120; }
}
else{
}
{ uint32_t dn_tmp_123=({ size_t dn_tmp_121=(v_outgoing).f_maximum;
if((unsigned __int128)dn_tmp_121>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_121; });uint32_t *dn_tmp_122=&(((v_result).f_bounds).f_max_message);*dn_tmp_122=dn_tmp_123; }
if(({ size_t dn_tmp_124=(v_incoming).f_maximum;
size_t dn_tmp_125=(v_outgoing).f_maximum;
(dn_tmp_124 > dn_tmp_125); })){
{ uint32_t dn_tmp_128=({ size_t dn_tmp_126=(v_incoming).f_maximum;
if((unsigned __int128)dn_tmp_126>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_126; });uint32_t *dn_tmp_127=&(((v_result).f_bounds).f_max_message);*dn_tmp_127=dn_tmp_128; }
}
else{
}
if(((!(({ dn_r_TransportBounds dn_tmp_129=(v_result).f_bounds;
dn_f_transport_bounds_valid(dn_tmp_129); }))) || (({ uint32_t dn_tmp_130=({ uint32_t dn_tmp_131=v_required;
uint32_t dn_tmp_132=((uint32_t)(8ULL));
dn_U32_bits((uint64_t)dn_tmp_131 & (uint64_t)dn_tmp_132); });
uint32_t dn_tmp_133=((uint32_t)(0ULL));
(dn_tmp_130 != dn_tmp_133); }) && ({ uint32_t dn_tmp_134=((v_result).f_bounds).f_channels;
uint32_t dn_tmp_135=((uint32_t)(2ULL));
(dn_tmp_134 < dn_tmp_135); })))){
{ dn_r_NetworkNegotiation dn_tmp_140=({ int32_t dn_tmp_136=((int32_t)(7ULL));
uint32_t dn_tmp_137=((uint32_t)(3ULL));
uint32_t dn_tmp_138=((uint32_t)(0ULL));
uint32_t dn_tmp_139=((uint32_t)(0ULL));
dn_f_network_negotiation_failure(dn_tmp_136,dn_tmp_137,dn_tmp_138,dn_tmp_139); });return dn_tmp_140; }
}
else{
}
{ dn_r_SessionContract dn_tmp_144=({ dn_r_SessionContract dn_tmp_141=(v_local)->f_session;
dn_r_SessionContract dn_tmp_142=(v_remote)->f_session;
dn_f_network_session_intersection(dn_tmp_141,dn_tmp_142); });dn_r_SessionContract *dn_tmp_143=&((v_result).f_session);*dn_tmp_143=dn_tmp_144; }
{ uint64_t dn_tmp_146=((v_local)->f_resources).f_maximum_session_messages;uint64_t *dn_tmp_145=&((v_result).f_maximum_session_messages);*dn_tmp_145=dn_tmp_146; }
if(({ uint64_t dn_tmp_147=((v_remote)->f_resources).f_maximum_session_messages;
uint64_t dn_tmp_148=(v_result).f_maximum_session_messages;
(dn_tmp_147 < dn_tmp_148); })){
{ uint64_t dn_tmp_150=((v_remote)->f_resources).f_maximum_session_messages;uint64_t *dn_tmp_149=&((v_result).f_maximum_session_messages);*dn_tmp_149=dn_tmp_150; }
}
else{
}
{ uint32_t dn_tmp_154=({ uint32_t dn_tmp_151=(v_a).f_pending_proofs;
uint32_t dn_tmp_152=(v_b).f_pending_proofs;
dn_U32_bits((uint64_t)dn_tmp_151 | (uint64_t)dn_tmp_152); });uint32_t *dn_tmp_153=&((v_result).f_pending_proofs);*dn_tmp_153=dn_tmp_154; }
{ uint32_t dn_tmp_158=({ uint32_t dn_tmp_155=(v_a).f_unsupported_security;
uint32_t dn_tmp_156=(v_b).f_unsupported_security;
dn_U32_bits((uint64_t)dn_tmp_155 | (uint64_t)dn_tmp_156); });uint32_t *dn_tmp_157=&((v_result).f_unsupported_security);*dn_tmp_157=dn_tmp_158; }
if(((({ uint32_t dn_tmp_159=(v_result).f_unsupported_security;
uint32_t dn_tmp_160=((uint32_t)(0ULL));
(dn_tmp_159 != dn_tmp_160); }) || ({ uint32_t dn_tmp_161=((v_local)->f_proofs).f_assurance;
uint32_t dn_tmp_162=((uint32_t)(2ULL));
(dn_tmp_161 == dn_tmp_162); })) || ({ uint32_t dn_tmp_163=((v_remote)->f_proofs).f_assurance;
uint32_t dn_tmp_164=((uint32_t)(2ULL));
(dn_tmp_163 == dn_tmp_164); }))){
{ int32_t dn_tmp_166=((int32_t)(6ULL));int32_t *dn_tmp_165=&((v_result).f_status);*dn_tmp_165=dn_tmp_166; }
{ int32_t dn_tmp_168=((int32_t)(6ULL));int32_t *dn_tmp_167=&(((v_result).f_choice).f_status);*dn_tmp_167=dn_tmp_168; }
{ dn_r_NetworkNegotiation dn_tmp_169=v_result;return dn_tmp_169; }
}
else{
}
{ dn_r_TransportChoice dn_tmp_174=({ dn_r_TransportRequirements dn_tmp_170=(v_result).f_requirements;
dn_r_TransportBounds dn_tmp_171=(v_result).f_bounds;
dn_t_arr6_r18_TransportCandidate dn_tmp_172=v_candidates;
dn_f_transport_select(dn_tmp_170,dn_tmp_171,dn_tmp_172); });dn_r_TransportChoice *dn_tmp_173=&((v_result).f_choice);*dn_tmp_173=dn_tmp_174; }
{ int32_t dn_tmp_176=((v_result).f_choice).f_status;int32_t *dn_tmp_175=&((v_result).f_status);*dn_tmp_175=dn_tmp_176; }
{ dn_r_NetworkNegotiation dn_tmp_177=v_result;return dn_tmp_177; }
}

dn_fail("missing native return");
}
static dn_r_NetworkNegotiation dn_cb_network_spec_negotiate(dn_r_NetworkSpec const * v_local,dn_r_NetworkSpec const * v_remote,bool v_initiator,dn_t_arr6_r18_TransportCandidate v_candidates) {
 if(!dn_enter()){return (dn_r_NetworkNegotiation){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkNegotiation){0};}
 dn_r_NetworkNegotiation dn_result=dn_f_network_spec_negotiate(v_local,v_remote,v_initiator,v_candidates);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_NetworkSpec dn_f_sample(uint32_t v_minor) {
{
dn_r_NetworkSpec v_spec=({ uint32_t dn_tmp_0=((uint32_t)(0x444c4e32ULL));
uint32_t dn_tmp_1=((uint32_t)(1ULL));
uint32_t dn_tmp_2=v_minor;
uint32_t dn_tmp_3=((uint32_t)(0ULL));
dn_f_network_spec(dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3); });
dn_r_MessageSchema v_message=({ uint32_t dn_tmp_4=((uint32_t)(1ULL));
uint32_t dn_tmp_5=((uint32_t)(2ULL));
uint32_t dn_tmp_6=((uint32_t)(3ULL));
dn_f_message_schema(dn_tmp_4,dn_tmp_5,dn_tmp_6); });
int32_t v_a=({ dn_r_MessageSchema * dn_tmp_7=(&(v_message));
dn_r_MessageField dn_tmp_8=({ uint32_t dn_tmp_9=((uint32_t)(1ULL));
uint32_t dn_tmp_10=((uint32_t)(3ULL));
uint32_t dn_tmp_11=((uint32_t)(1ULL));
uint32_t dn_tmp_12=((uint32_t)(1ULL));
uint32_t dn_tmp_13=((uint32_t)(0ULL));
dn_f_network_field(dn_tmp_9,dn_tmp_10,dn_tmp_11,dn_tmp_12,dn_tmp_13); });
dn_f_message_add_field(dn_tmp_7,dn_tmp_8); });
(void)(({ bool dn_tmp_14=({ int32_t dn_tmp_15=v_a;
int32_t dn_tmp_16=((int32_t)(0ULL));
(dn_tmp_15 == dn_tmp_16); });
(dn_assert(dn_tmp_14),0); }));
if(({ uint32_t dn_tmp_17=v_minor;
uint32_t dn_tmp_18=((uint32_t)(0ULL));
(dn_tmp_17 != dn_tmp_18); })){
dn_r_MessageField v_added=({ uint32_t dn_tmp_19=((uint32_t)(2ULL));
uint32_t dn_tmp_20=((uint32_t)(6ULL));
uint32_t dn_tmp_21=((uint32_t)(0ULL));
uint32_t dn_tmp_22=((uint32_t)(1ULL));
uint32_t dn_tmp_23=((uint32_t)(128ULL));
dn_f_network_field(dn_tmp_19,dn_tmp_20,dn_tmp_21,dn_tmp_22,dn_tmp_23); });
{ uint32_t dn_tmp_25=((uint32_t)(1ULL));uint32_t *dn_tmp_24=&((v_added).f_first_minor);*dn_tmp_24=dn_tmp_25; }
int32_t v_b=({ dn_r_MessageSchema * dn_tmp_26=(&(v_message));
dn_r_MessageField dn_tmp_27=v_added;
dn_f_message_add_field(dn_tmp_26,dn_tmp_27); });
(void)(({ bool dn_tmp_28=({ int32_t dn_tmp_29=v_b;
int32_t dn_tmp_30=((int32_t)(0ULL));
(dn_tmp_29 == dn_tmp_30); });
(dn_assert(dn_tmp_28),0); }));
}
else{
}
int32_t v_m=({ dn_r_NetworkSpec * dn_tmp_31=(&(v_spec));
dn_r_MessageSchema dn_tmp_32=v_message;
dn_f_network_add_message(dn_tmp_31,dn_tmp_32); });
(void)(({ bool dn_tmp_33=({ int32_t dn_tmp_34=v_m;
int32_t dn_tmp_35=((int32_t)(0ULL));
(dn_tmp_34 == dn_tmp_35); });
(dn_assert(dn_tmp_33),0); }));
{ dn_r_NetworkSpec dn_tmp_36=v_spec;return dn_tmp_36; }
}

dn_fail("missing native return");
}
static dn_r_NetworkSpec dn_cb_sample(uint32_t v_minor) {
 if(!dn_enter()){return (dn_r_NetworkSpec){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_NetworkSpec){0};}
 dn_r_NetworkSpec dn_result=dn_f_sample(v_minor);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_main(void) {
{
dn_r_NetworkSpec v_older=({ uint32_t dn_tmp_0=((uint32_t)(0ULL));
dn_f_sample(dn_tmp_0); });
dn_r_NetworkSpec v_newer=({ uint32_t dn_tmp_1=((uint32_t)(1ULL));
dn_f_sample(dn_tmp_1); });
dn_t_arr6_r18_TransportCandidate v_candidates=({ uint32_t dn_tmp_2=((uint32_t)(63ULL));
dn_f_transport_candidates(dn_tmp_2); });
dn_r_NetworkNegotiation v_shared=({ dn_r_NetworkSpec const * dn_tmp_3=(&(v_newer));
dn_r_NetworkSpec const * dn_tmp_4=(&(v_older));
bool dn_tmp_5=1;
dn_t_arr6_r18_TransportCandidate dn_tmp_6=v_candidates;
dn_f_network_spec_negotiate(dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6); });
(void)(({ bool dn_tmp_7=((({ int32_t dn_tmp_8=(v_shared).f_status;
int32_t dn_tmp_9=((int32_t)(0ULL));
(dn_tmp_8 == dn_tmp_9); }) && ({ uint32_t dn_tmp_10=(v_shared).f_minor;
uint32_t dn_tmp_11=((uint32_t)(0ULL));
(dn_tmp_10 == dn_tmp_11); })) && ({ uint32_t dn_tmp_12=((v_shared).f_bounds).f_max_message;
uint32_t dn_tmp_13=((uint32_t)(24ULL));
(dn_tmp_12 == dn_tmp_13); }));
(dn_assert(dn_tmp_7),0); }));
{ uint32_t dn_tmp_15=((uint32_t)(1ULL));uint32_t *dn_tmp_14=&(((v_newer).f_version).f_minimum_minor);*dn_tmp_14=dn_tmp_15; }
dn_r_NetworkNegotiation v_excluded=({ dn_r_NetworkSpec const * dn_tmp_16=(&(v_newer));
dn_r_NetworkSpec const * dn_tmp_17=(&(v_older));
bool dn_tmp_18=1;
dn_t_arr6_r18_TransportCandidate dn_tmp_19=v_candidates;
dn_f_network_spec_negotiate(dn_tmp_16,dn_tmp_17,dn_tmp_18,dn_tmp_19); });
(void)(({ bool dn_tmp_20=(({ int32_t dn_tmp_21=(v_excluded).f_status;
int32_t dn_tmp_22=((int32_t)(11ULL));
(dn_tmp_21 == dn_tmp_22); }) && ({ uint32_t dn_tmp_23=(v_excluded).f_component;
uint32_t dn_tmp_24=((uint32_t)(1ULL));
(dn_tmp_23 == dn_tmp_24); }));
(dn_assert(dn_tmp_20),0); }));
{ uint32_t dn_tmp_26=((uint32_t)(0ULL));uint32_t *dn_tmp_25=&(((v_newer).f_version).f_minimum_minor);*dn_tmp_25=dn_tmp_26; }
{ uint32_t dn_tmp_32=((uint32_t)(4ULL));uint32_t *dn_tmp_31=&(((*({ dn_t_arr32_r12_MessageField *dn_tmp_27=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_29=&((v_newer).f_messages);size_t dn_tmp_30=((size_t)(0ULL));&dn_tmp_29->data[dn_index(dn_tmp_30,16)]; }))).f_fields);size_t dn_tmp_28=((size_t)(0ULL));&dn_tmp_27->data[dn_index(dn_tmp_28,32)]; }))).f_kind);*dn_tmp_31=dn_tmp_32; }
dn_r_NetworkNegotiation v_changed_type=({ dn_r_NetworkSpec const * dn_tmp_33=(&(v_newer));
dn_r_NetworkSpec const * dn_tmp_34=(&(v_older));
bool dn_tmp_35=1;
dn_t_arr6_r18_TransportCandidate dn_tmp_36=v_candidates;
dn_f_network_spec_negotiate(dn_tmp_33,dn_tmp_34,dn_tmp_35,dn_tmp_36); });
(void)(({ bool dn_tmp_37=((({ int32_t dn_tmp_38=(v_changed_type).f_status;
int32_t dn_tmp_39=((int32_t)(11ULL));
(dn_tmp_38 == dn_tmp_39); }) && ({ uint32_t dn_tmp_40=(v_changed_type).f_message;
uint32_t dn_tmp_41=((uint32_t)(1ULL));
(dn_tmp_40 == dn_tmp_41); })) && ({ uint32_t dn_tmp_42=(v_changed_type).f_field;
uint32_t dn_tmp_43=((uint32_t)(1ULL));
(dn_tmp_42 == dn_tmp_43); }));
(dn_assert(dn_tmp_37),0); }));
{ uint32_t dn_tmp_49=((uint32_t)(3ULL));uint32_t *dn_tmp_48=&(((*({ dn_t_arr32_r12_MessageField *dn_tmp_44=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_46=&((v_newer).f_messages);size_t dn_tmp_47=((size_t)(0ULL));&dn_tmp_46->data[dn_index(dn_tmp_47,16)]; }))).f_fields);size_t dn_tmp_45=((size_t)(0ULL));&dn_tmp_44->data[dn_index(dn_tmp_45,32)]; }))).f_kind);*dn_tmp_48=dn_tmp_49; }
{ uint32_t dn_tmp_55=((uint32_t)(0ULL));uint32_t *dn_tmp_54=&(((*({ dn_t_arr32_r12_MessageField *dn_tmp_50=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_52=&((v_newer).f_messages);size_t dn_tmp_53=((size_t)(0ULL));&dn_tmp_52->data[dn_index(dn_tmp_53,16)]; }))).f_fields);size_t dn_tmp_51=((size_t)(1ULL));&dn_tmp_50->data[dn_index(dn_tmp_51,32)]; }))).f_first_minor);*dn_tmp_54=dn_tmp_55; }
dn_r_NetworkNegotiation v_unknown=({ dn_r_NetworkSpec const * dn_tmp_56=(&(v_newer));
dn_r_NetworkSpec const * dn_tmp_57=(&(v_older));
bool dn_tmp_58=1;
dn_t_arr6_r18_TransportCandidate dn_tmp_59=v_candidates;
dn_f_network_spec_negotiate(dn_tmp_56,dn_tmp_57,dn_tmp_58,dn_tmp_59); });
(void)(({ bool dn_tmp_60=({ int32_t dn_tmp_61=(v_unknown).f_status;
int32_t dn_tmp_62=((int32_t)(11ULL));
(dn_tmp_61 == dn_tmp_62); });
(dn_assert(dn_tmp_60),0); }));
dn_r_NetworkSpec v_receiver=({ uint32_t dn_tmp_63=((uint32_t)(0ULL));
dn_f_sample(dn_tmp_63); });
{ uint32_t dn_tmp_65=((uint32_t)(1ULL));uint32_t *dn_tmp_64=&(((v_receiver).f_wire).f_unknown_fields);*dn_tmp_64=dn_tmp_65; }
{ uint32_t dn_tmp_67=((uint32_t)(2ULL));uint32_t *dn_tmp_66=&(((v_receiver).f_resources).f_maximum_unknown_fields);*dn_tmp_66=dn_tmp_67; }
{ uint32_t dn_tmp_69=((uint32_t)(256ULL));uint32_t *dn_tmp_68=&(((v_receiver).f_resources).f_maximum_unknown_bytes);*dn_tmp_68=dn_tmp_69; }
{ uint32_t dn_tmp_73=((uint32_t)(1ULL));uint32_t *dn_tmp_72=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_70=&((v_newer).f_messages);size_t dn_tmp_71=((size_t)(0ULL));&dn_tmp_70->data[dn_index(dn_tmp_71,16)]; }))).f_senders);*dn_tmp_72=dn_tmp_73; }
{ uint32_t dn_tmp_77=((uint32_t)(1ULL));uint32_t *dn_tmp_76=&(((*({ dn_t_arr16_r13_MessageSchema *dn_tmp_74=&((v_receiver).f_messages);size_t dn_tmp_75=((size_t)(0ULL));&dn_tmp_74->data[dn_index(dn_tmp_75,16)]; }))).f_senders);*dn_tmp_76=dn_tmp_77; }
dn_r_NetworkNegotiation v_extension=({ dn_r_NetworkSpec const * dn_tmp_78=(&(v_newer));
dn_r_NetworkSpec const * dn_tmp_79=(&(v_receiver));
bool dn_tmp_80=1;
dn_t_arr6_r18_TransportCandidate dn_tmp_81=v_candidates;
dn_f_network_spec_negotiate(dn_tmp_78,dn_tmp_79,dn_tmp_80,dn_tmp_81); });
(void)(({ bool dn_tmp_82=((({ int32_t dn_tmp_83=(v_extension).f_status;
int32_t dn_tmp_84=((int32_t)(0ULL));
(dn_tmp_83 == dn_tmp_84); }) && ({ uint32_t dn_tmp_85=(v_extension).f_minor;
uint32_t dn_tmp_86=((uint32_t)(0ULL));
(dn_tmp_85 == dn_tmp_86); })) && ({ uint32_t dn_tmp_87=({ uint32_t dn_tmp_88=(v_extension).f_pending_proofs;
uint32_t dn_tmp_89=((uint32_t)(16ULL));
dn_U32_bits((uint64_t)dn_tmp_88 & (uint64_t)dn_tmp_89); });
uint32_t dn_tmp_90=((uint32_t)(0ULL));
(dn_tmp_87 != dn_tmp_90); }));
(dn_assert(dn_tmp_82),0); }));
{ uint32_t dn_tmp_92=((uint32_t)(8ULL));uint32_t *dn_tmp_91=&(((v_receiver).f_resources).f_maximum_unknown_bytes);*dn_tmp_91=dn_tmp_92; }
dn_r_NetworkNegotiation v_limited=({ dn_r_NetworkSpec const * dn_tmp_93=(&(v_newer));
dn_r_NetworkSpec const * dn_tmp_94=(&(v_receiver));
bool dn_tmp_95=1;
dn_t_arr6_r18_TransportCandidate dn_tmp_96=v_candidates;
dn_f_network_spec_negotiate(dn_tmp_93,dn_tmp_94,dn_tmp_95,dn_tmp_96); });
(void)(({ bool dn_tmp_97=(({ int32_t dn_tmp_98=(v_limited).f_status;
int32_t dn_tmp_99=((int32_t)(7ULL));
(dn_tmp_98 == dn_tmp_99); }) && ({ uint32_t dn_tmp_100=(v_limited).f_component;
uint32_t dn_tmp_101=((uint32_t)(3ULL));
(dn_tmp_100 == dn_tmp_101); }));
(dn_assert(dn_tmp_97),0); }));
{ dn_r_NetworkSpec dn_tmp_104=({ uint32_t dn_tmp_102=((uint32_t)(1ULL));
dn_f_sample(dn_tmp_102); });dn_r_NetworkSpec *dn_tmp_103=&(v_newer);*dn_tmp_103=dn_tmp_104; }
{ dn_r_NetworkSpec dn_tmp_107=({ uint32_t dn_tmp_105=((uint32_t)(0ULL));
dn_f_sample(dn_tmp_105); });dn_r_NetworkSpec *dn_tmp_106=&(v_receiver);*dn_tmp_106=dn_tmp_107; }
{ uint32_t dn_tmp_109=((uint32_t)(1ULL));uint32_t *dn_tmp_108=&(((v_receiver).f_transport).f_forbidden);*dn_tmp_108=dn_tmp_109; }
{ uint32_t dn_tmp_111=((uint32_t)(17ULL));uint32_t *dn_tmp_110=&(((v_newer).f_transport).f_required);*dn_tmp_110=dn_tmp_111; }
dn_r_NetworkNegotiation v_contradictory=({ dn_r_NetworkSpec const * dn_tmp_112=(&(v_newer));
dn_r_NetworkSpec const * dn_tmp_113=(&(v_receiver));
bool dn_tmp_114=1;
dn_t_arr6_r18_TransportCandidate dn_tmp_115=v_candidates;
dn_f_network_spec_negotiate(dn_tmp_112,dn_tmp_113,dn_tmp_114,dn_tmp_115); });
(void)(({ bool dn_tmp_116=({ int32_t dn_tmp_117=(v_contradictory).f_status;
int32_t dn_tmp_118=((int32_t)(11ULL));
(dn_tmp_117 == dn_tmp_118); });
(dn_assert(dn_tmp_116),0); }));
(void)(({ dn_utf8 dn_tmp_119=((dn_utf8){(const uint8_t*)"NetworkSpec version windows directional fields extensions and budgets OK", 72});
(fwrite(dn_tmp_119.data,1,dn_tmp_119.len,stdout),fputc('\n',stdout),0); }));
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
DN_EXPORT size_t dlm_native_sizeof_NetworkNegotiation(void){return 120;}
DN_EXPORT size_t dlm_native_alignof_NetworkNegotiation(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_NetworkNegotiation(void){return 1;}
DN_EXPORT size_t dlm_native_offset_NetworkNegotiation_minor(void){return 0;}
DN_EXPORT size_t dlm_native_offset_NetworkNegotiation_requirements(void){return 4;}
DN_EXPORT size_t dlm_native_offset_NetworkNegotiation_bounds(void){return 16;}
DN_EXPORT size_t dlm_native_offset_NetworkNegotiation_choice(void){return 40;}
DN_EXPORT size_t dlm_native_offset_NetworkNegotiation_session(void){return 68;}
DN_EXPORT size_t dlm_native_offset_NetworkNegotiation_maximum_session_messages(void){return 88;}
DN_EXPORT size_t dlm_native_offset_NetworkNegotiation_pending_proofs(void){return 96;}
DN_EXPORT size_t dlm_native_offset_NetworkNegotiation_unsupported_security(void){return 100;}
DN_EXPORT size_t dlm_native_offset_NetworkNegotiation_status(void){return 104;}
DN_EXPORT size_t dlm_native_offset_NetworkNegotiation_component(void){return 108;}
DN_EXPORT size_t dlm_native_offset_NetworkNegotiation_message(void){return 112;}
DN_EXPORT size_t dlm_native_offset_NetworkNegotiation_field(void){return 116;}
int main(void){(void)dlm_native_thread_init();dn_boundary b={0};jmp_buf dn_jump;b.jump=&dn_jump;b.foreign_marker=dn_foreign_context;dn_current=&b;dn_fp_begin(&b);if(setjmp(dn_jump)){dn_boundary_end(&b,1);fprintf(stderr,"DLM native error: %s\n",dn_error);return 1;}(void)dn_f_main();dn_boundary_end(&b,0);(void)dlm_native_runtime_shutdown();return 0;}
