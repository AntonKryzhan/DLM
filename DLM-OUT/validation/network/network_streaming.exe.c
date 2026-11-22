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
typedef struct dn_r_ProtocolValue dn_r_ProtocolValue;
typedef struct dn_r_ProtocolFrame dn_r_ProtocolFrame;
typedef struct dn_r_Frame32Decoder dn_r_Frame32Decoder;
typedef struct dn_r_Frame32Step dn_r_Frame32Step;
struct dn_r_Frame32Decoder {
    uint32_t f_maximum;
    uint32_t f_header;
    uint32_t f_prefix_count;
    uint32_t f_remaining;
    int32_t f_fault;
};
struct dn_r_Frame32Step {
    size_t f_consumed;
    size_t f_payload_offset;
    size_t f_payload_count;
    uint32_t f_remaining;
    bool f_complete;
    int32_t f_status;
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
typedef struct dn_t_buf_U8 { uint8_t *data;size_t len,cap; } dn_t_buf_U8;
typedef struct dn_t_slice0_U8 { uint8_t const *data;size_t len; } dn_t_slice0_U8;
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
_Static_assert(sizeof(dn_r_Frame32Decoder)==20,"Frame32Decoder ABI size");
_Static_assert(_Alignof(dn_r_Frame32Decoder)==4,"Frame32Decoder ABI alignment");
_Static_assert(offsetof(dn_r_Frame32Decoder,f_maximum)==0,"Frame32Decoder.maximum ABI offset");
_Static_assert(offsetof(dn_r_Frame32Decoder,f_header)==4,"Frame32Decoder.header ABI offset");
_Static_assert(offsetof(dn_r_Frame32Decoder,f_prefix_count)==8,"Frame32Decoder.prefix_count ABI offset");
_Static_assert(offsetof(dn_r_Frame32Decoder,f_remaining)==12,"Frame32Decoder.remaining ABI offset");
_Static_assert(offsetof(dn_r_Frame32Decoder,f_fault)==16,"Frame32Decoder.fault ABI offset");
_Static_assert(sizeof(dn_r_Frame32Step)==40,"Frame32Step ABI size");
_Static_assert(_Alignof(dn_r_Frame32Step)==8,"Frame32Step ABI alignment");
_Static_assert(offsetof(dn_r_Frame32Step,f_consumed)==0,"Frame32Step.consumed ABI offset");
_Static_assert(offsetof(dn_r_Frame32Step,f_payload_offset)==8,"Frame32Step.payload_offset ABI offset");
_Static_assert(offsetof(dn_r_Frame32Step,f_payload_count)==16,"Frame32Step.payload_count ABI offset");
_Static_assert(offsetof(dn_r_Frame32Step,f_remaining)==24,"Frame32Step.remaining ABI offset");
_Static_assert(offsetof(dn_r_Frame32Step,f_complete)==28,"Frame32Step.complete ABI offset");
_Static_assert(offsetof(dn_r_Frame32Step,f_status)==32,"Frame32Step.status ABI offset");
static void dn_drop_buf_U8(dn_t_buf_U8 volatile*);static void dn_retain_buf_U8(dn_t_buf_U8 volatile*);
static void dn_adopt_buf_U8(dn_t_buf_U8 volatile*);
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
static dn_r_Frame32Decoder dn_f_frame32_decoder(uint32_t v_maximum);
static uint8_t dn_f_frame32_reset(dn_r_Frame32Decoder * v_state);
static bool dn_f_frame32_valid(dn_r_Frame32Decoder const * v_state);
static dn_r_Frame32Step dn_f_frame32_feed(dn_r_Frame32Decoder * v_state,dn_t_slice0_U8 v_input,size_t v_offset);
static int32_t dn_f_frame32_finish(dn_r_Frame32Decoder const * v_state);
static dn_t_slice0_U8 dn_f_frame32_view(dn_t_buf_U8 const * v_input,size_t v_base_offset,dn_r_Frame32Step v_step);
static int32_t dn_f_frame32_write_header(dn_t_buf_U8 * v_output,size_t v_offset,uint32_t v_count,uint32_t v_maximum);
static uint8_t dn_f_main(void);
static void dn_drop_buf_U8(dn_t_buf_U8 volatile *x); static void dn_retain_buf_U8(dn_t_buf_U8 volatile *x);
static void dn_drop_buf_U8(dn_t_buf_U8 volatile *x){dn_deallocate(x->data);*x=(dn_t_buf_U8){0};}
static void dn_retain_buf_U8(dn_t_buf_U8 volatile *x){dn_detach(x->data);}
static dn_t_buf_U8 dn_new_buf_U8(size_t n){dn_t_buf_U8 x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(uint8_t)),_Alignof(uint8_t));x.len=x.cap=n;return x;}
static dn_t_buf_U8 dn_aligned_buf_U8(size_t n,size_t a){if(a<_Alignof(uint8_t)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_U8 x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(uint8_t)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_U8(dn_t_buf_U8 *x,uint8_t v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;uint8_t *p=dn_allocate_aligned(dn_size(cap,sizeof(uint8_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(uint8_t));if(x->len)memcpy(p,x->data,x->len*sizeof(uint8_t));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;x->len++;return 0;}
static uint8_t dn_resize_buf_U8(dn_t_buf_U8 *x,size_t n){if(n<x->len){}if(n>x->cap){size_t cap=n;uint8_t *p=dn_allocate_aligned(dn_size(cap,sizeof(uint8_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(uint8_t));if(x->len)memcpy(p,x->data,x->len*sizeof(uint8_t));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(uint8_t));x->len=n;return 0;}
static void dn_adopt_buf_U8(dn_t_buf_U8 volatile*x){dn_adopt(x->data);}
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
static dn_r_Frame32Decoder dn_cb_frame32_decoder(uint32_t v_maximum);
static void dn_cb_frame32_reset(dn_r_Frame32Decoder * v_state);
static bool dn_cb_frame32_valid(dn_r_Frame32Decoder const * v_state);
static dn_r_Frame32Step dn_cb_frame32_feed(dn_r_Frame32Decoder * v_state,dn_t_slice0_U8 v_input,size_t v_offset);
static int32_t dn_cb_frame32_finish(dn_r_Frame32Decoder const * v_state);
static dn_t_slice0_U8 dn_cb_frame32_view(dn_t_buf_U8 const * v_input,size_t v_base_offset,dn_r_Frame32Step v_step);
static int32_t dn_cb_frame32_write_header(dn_t_buf_U8 * v_output,size_t v_offset,uint32_t v_count,uint32_t v_maximum);
static void dn_cb_main(void);
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
static dn_r_Frame32Decoder dn_f_frame32_decoder(uint32_t v_maximum) {
{
{ dn_r_Frame32Decoder dn_tmp_5=({ uint32_t dn_tmp_0=v_maximum;
uint32_t dn_tmp_1=((uint32_t)(0ULL));
uint32_t dn_tmp_2=((uint32_t)(0ULL));
uint32_t dn_tmp_3=((uint32_t)(0ULL));
int32_t dn_tmp_4=((int32_t)(0ULL));
((dn_r_Frame32Decoder){dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4}); });return dn_tmp_5; }
}

dn_fail("missing native return");
}
static dn_r_Frame32Decoder dn_cb_frame32_decoder(uint32_t v_maximum) {
 if(!dn_enter()){return (dn_r_Frame32Decoder){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_Frame32Decoder){0};}
 dn_r_Frame32Decoder dn_result=dn_f_frame32_decoder(v_maximum);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_frame32_reset(dn_r_Frame32Decoder * v_state) {
{
{ uint32_t dn_tmp_1=((uint32_t)(0ULL));uint32_t *dn_tmp_0=&((v_state)->f_header);*dn_tmp_0=dn_tmp_1; }
{ uint32_t dn_tmp_3=((uint32_t)(0ULL));uint32_t *dn_tmp_2=&((v_state)->f_prefix_count);*dn_tmp_2=dn_tmp_3; }
{ uint32_t dn_tmp_5=((uint32_t)(0ULL));uint32_t *dn_tmp_4=&((v_state)->f_remaining);*dn_tmp_4=dn_tmp_5; }
{ int32_t dn_tmp_7=((int32_t)(0ULL));int32_t *dn_tmp_6=&((v_state)->f_fault);*dn_tmp_6=dn_tmp_7; }
}

return 0;
}
static void dn_cb_frame32_reset(dn_r_Frame32Decoder * v_state) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_frame32_reset(v_state);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static bool dn_f_frame32_valid(dn_r_Frame32Decoder const * v_state) {
{
if(({ uint32_t dn_tmp_0=(v_state)->f_prefix_count;
uint32_t dn_tmp_1=({ uint32_t dn_tmp_2=((uint32_t)(4ULL));
if((unsigned __int128)dn_tmp_2>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_2; });
(dn_tmp_0 > dn_tmp_1); })){
{ bool dn_tmp_3=0;return dn_tmp_3; }
}
else{
}
if(({ int32_t dn_tmp_4=(v_state)->f_fault;
int32_t dn_tmp_5=({ int32_t dn_tmp_6=((int32_t)(7ULL));
if((__int128)dn_tmp_6<(-((__int128)1<<31))||(__int128)dn_tmp_6>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_6; });
(dn_tmp_4 == dn_tmp_5); })){
{ bool dn_tmp_15=((({ uint32_t dn_tmp_7=(v_state)->f_prefix_count;
uint32_t dn_tmp_8=({ uint32_t dn_tmp_9=((uint32_t)(4ULL));
if((unsigned __int128)dn_tmp_9>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_9; });
(dn_tmp_7 == dn_tmp_8); }) && ({ uint32_t dn_tmp_10=(v_state)->f_header;
uint32_t dn_tmp_11=(v_state)->f_maximum;
(dn_tmp_10 > dn_tmp_11); })) && ({ uint32_t dn_tmp_12=(v_state)->f_remaining;
uint32_t dn_tmp_13=({ uint32_t dn_tmp_14=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_14>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_14; });
(dn_tmp_12 == dn_tmp_13); }));return dn_tmp_15; }
}
else{
}
if(({ int32_t dn_tmp_16=(v_state)->f_fault;
int32_t dn_tmp_17=({ int32_t dn_tmp_18=((int32_t)(0ULL));
if((__int128)dn_tmp_18<(-((__int128)1<<31))||(__int128)dn_tmp_18>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_18; });
(dn_tmp_16 != dn_tmp_17); })){
{ bool dn_tmp_19=0;return dn_tmp_19; }
}
else{
}
if(({ uint32_t dn_tmp_20=(v_state)->f_prefix_count;
uint32_t dn_tmp_21=({ uint32_t dn_tmp_22=((uint32_t)(4ULL));
if((unsigned __int128)dn_tmp_22>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_22; });
(dn_tmp_20 < dn_tmp_21); })){
{ bool dn_tmp_34=(({ uint32_t dn_tmp_23=(v_state)->f_remaining;
uint32_t dn_tmp_24=({ uint32_t dn_tmp_25=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_25>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_25; });
(dn_tmp_23 == dn_tmp_24); }) && ({ uint32_t dn_tmp_26=({ uint32_t dn_tmp_27=(v_state)->f_header;
uint32_t dn_tmp_28=({ uint32_t dn_tmp_29=(v_state)->f_prefix_count;
uint32_t dn_tmp_30=({ uint32_t dn_tmp_31=((uint32_t)(8ULL));
if((unsigned __int128)dn_tmp_31>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_31; });
dn_U32_mul(dn_tmp_29,dn_tmp_30); });
dn_U32_shr(dn_tmp_27,dn_tmp_28); });
uint32_t dn_tmp_32=({ uint32_t dn_tmp_33=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_33>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_33; });
(dn_tmp_26 == dn_tmp_32); }));return dn_tmp_34; }
}
else{
}
{ bool dn_tmp_42=((({ uint32_t dn_tmp_35=(v_state)->f_header;
uint32_t dn_tmp_36=(v_state)->f_maximum;
(dn_tmp_35 <= dn_tmp_36); }) && ({ uint32_t dn_tmp_37=(v_state)->f_remaining;
uint32_t dn_tmp_38=({ uint32_t dn_tmp_39=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_39>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_39; });
(dn_tmp_37 > dn_tmp_38); })) && ({ uint32_t dn_tmp_40=(v_state)->f_remaining;
uint32_t dn_tmp_41=(v_state)->f_header;
(dn_tmp_40 <= dn_tmp_41); }));return dn_tmp_42; }
}

dn_fail("missing native return");
}
static bool dn_cb_frame32_valid(dn_r_Frame32Decoder const * v_state) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_frame32_valid(v_state);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_Frame32Step dn_f_frame32_feed(dn_r_Frame32Decoder * v_state,dn_t_slice0_U8 v_input,size_t v_offset) {
{
if(({ size_t dn_tmp_0=v_offset;
size_t dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_input;
(dn_tmp_2).len; });
(dn_tmp_0 > dn_tmp_1); })){
{ dn_r_Frame32Step dn_tmp_9=({ size_t dn_tmp_3=((size_t)(0ULL));
size_t dn_tmp_4=v_offset;
size_t dn_tmp_5=((size_t)(0ULL));
uint32_t dn_tmp_6=((uint32_t)(0ULL));
bool dn_tmp_7=0;
int32_t dn_tmp_8=((int32_t)(4ULL));
((dn_r_Frame32Step){dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8}); });return dn_tmp_9; }
}
else{
}
if((!(({ dn_r_Frame32Decoder * dn_tmp_10=v_state;
dn_f_frame32_valid(dn_tmp_10); })))){
{ dn_r_Frame32Step dn_tmp_17=({ size_t dn_tmp_11=((size_t)(0ULL));
size_t dn_tmp_12=v_offset;
size_t dn_tmp_13=((size_t)(0ULL));
uint32_t dn_tmp_14=((uint32_t)(0ULL));
bool dn_tmp_15=0;
int32_t dn_tmp_16=((int32_t)(4ULL));
((dn_r_Frame32Step){dn_tmp_11,dn_tmp_12,dn_tmp_13,dn_tmp_14,dn_tmp_15,dn_tmp_16}); });return dn_tmp_17; }
}
else{
}
if(({ int32_t dn_tmp_18=(v_state)->f_fault;
int32_t dn_tmp_19=({ int32_t dn_tmp_20=((int32_t)(0ULL));
if((__int128)dn_tmp_20<(-((__int128)1<<31))||(__int128)dn_tmp_20>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_20; });
(dn_tmp_18 != dn_tmp_19); })){
{ dn_r_Frame32Step dn_tmp_27=({ size_t dn_tmp_21=((size_t)(0ULL));
size_t dn_tmp_22=v_offset;
size_t dn_tmp_23=((size_t)(0ULL));
uint32_t dn_tmp_24=(v_state)->f_remaining;
bool dn_tmp_25=0;
int32_t dn_tmp_26=(v_state)->f_fault;
((dn_r_Frame32Step){dn_tmp_21,dn_tmp_22,dn_tmp_23,dn_tmp_24,dn_tmp_25,dn_tmp_26}); });return dn_tmp_27; }
}
else{
}
size_t v_position=v_offset;
{ size_t dn_tmp_29=((size_t)(0ULL));size_t dn_tmp_28=((size_t)(4ULL));for(size_t v_index=dn_tmp_29;v_index<dn_tmp_28;v_index++){
if((({ uint32_t dn_tmp_30=(v_state)->f_prefix_count;
uint32_t dn_tmp_31=({ uint32_t dn_tmp_32=((uint32_t)(4ULL));
if((unsigned __int128)dn_tmp_32>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_32; });
(dn_tmp_30 == dn_tmp_31); }) || ({ size_t dn_tmp_33=v_position;
size_t dn_tmp_34=({ dn_t_slice0_U8 dn_tmp_35=v_input;
(dn_tmp_35).len; });
(dn_tmp_33 == dn_tmp_34); }))){
break;
}
else{
}
{ uint32_t dn_tmp_45=({ uint32_t dn_tmp_36=({ uint32_t dn_tmp_37=(v_state)->f_header;
uint32_t dn_tmp_38=({ uint32_t dn_tmp_39=((uint32_t)(8ULL));
if((unsigned __int128)dn_tmp_39>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_39; });
dn_U32_shl(dn_tmp_37,dn_tmp_38); });
uint32_t dn_tmp_40=({ uint8_t dn_tmp_41=(*({ dn_t_slice0_U8 *dn_tmp_42=&(v_input);size_t dn_tmp_43=v_position;&dn_tmp_42->data[dn_index(dn_tmp_43,dn_tmp_42->len)]; }));
if((unsigned __int128)dn_tmp_41>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_41; });
dn_U32_bits((uint64_t)dn_tmp_36 | (uint64_t)dn_tmp_40); });uint32_t *dn_tmp_44=&((v_state)->f_header);*dn_tmp_44=dn_tmp_45; }
{ uint32_t dn_tmp_50=({ uint32_t dn_tmp_46=(v_state)->f_prefix_count;
uint32_t dn_tmp_47=({ uint32_t dn_tmp_48=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_48>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_48; });
dn_U32_add(dn_tmp_46,dn_tmp_47); });uint32_t *dn_tmp_49=&((v_state)->f_prefix_count);*dn_tmp_49=dn_tmp_50; }
{ size_t dn_tmp_54=({ size_t dn_tmp_51=v_position;
size_t dn_tmp_52=((size_t)(1ULL));
dn_USize_add(dn_tmp_51,dn_tmp_52); });size_t *dn_tmp_53=&(v_position);*dn_tmp_53=dn_tmp_54; }
if(({ uint32_t dn_tmp_55=(v_state)->f_prefix_count;
uint32_t dn_tmp_56=({ uint32_t dn_tmp_57=((uint32_t)(4ULL));
if((unsigned __int128)dn_tmp_57>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_57; });
(dn_tmp_55 == dn_tmp_56); })){
if(({ uint32_t dn_tmp_58=(v_state)->f_header;
uint32_t dn_tmp_59=(v_state)->f_maximum;
(dn_tmp_58 > dn_tmp_59); })){
{ int32_t dn_tmp_61=((int32_t)(7ULL));int32_t *dn_tmp_60=&((v_state)->f_fault);*dn_tmp_60=dn_tmp_61; }
{ dn_r_Frame32Step dn_tmp_70=({ size_t dn_tmp_62=({ size_t dn_tmp_63=v_position;
size_t dn_tmp_64=v_offset;
dn_USize_sub(dn_tmp_63,dn_tmp_64); });
size_t dn_tmp_65=v_position;
size_t dn_tmp_66=((size_t)(0ULL));
uint32_t dn_tmp_67=((uint32_t)(0ULL));
bool dn_tmp_68=0;
int32_t dn_tmp_69=((int32_t)(7ULL));
((dn_r_Frame32Step){dn_tmp_62,dn_tmp_65,dn_tmp_66,dn_tmp_67,dn_tmp_68,dn_tmp_69}); });return dn_tmp_70; }
}
else{
}
{ uint32_t dn_tmp_72=(v_state)->f_header;uint32_t *dn_tmp_71=&((v_state)->f_remaining);*dn_tmp_71=dn_tmp_72; }
}
else{
}
}
 }
if(({ uint32_t dn_tmp_73=(v_state)->f_prefix_count;
uint32_t dn_tmp_74=({ uint32_t dn_tmp_75=((uint32_t)(4ULL));
if((unsigned __int128)dn_tmp_75>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_75; });
(dn_tmp_73 < dn_tmp_74); })){
{ dn_r_Frame32Step dn_tmp_84=({ size_t dn_tmp_76=({ size_t dn_tmp_77=v_position;
size_t dn_tmp_78=v_offset;
dn_USize_sub(dn_tmp_77,dn_tmp_78); });
size_t dn_tmp_79=v_position;
size_t dn_tmp_80=((size_t)(0ULL));
uint32_t dn_tmp_81=((uint32_t)(0ULL));
bool dn_tmp_82=0;
int32_t dn_tmp_83=((int32_t)(1ULL));
((dn_r_Frame32Step){dn_tmp_76,dn_tmp_79,dn_tmp_80,dn_tmp_81,dn_tmp_82,dn_tmp_83}); });return dn_tmp_84; }
}
else{
}
size_t v_available=({ size_t dn_tmp_85=({ dn_t_slice0_U8 dn_tmp_86=v_input;
(dn_tmp_86).len; });
size_t dn_tmp_87=v_position;
dn_USize_sub(dn_tmp_85,dn_tmp_87); });
size_t v_count=({ uint32_t dn_tmp_88=(v_state)->f_remaining;
if((unsigned __int128)dn_tmp_88>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_88; });
if(({ size_t dn_tmp_89=v_count;
size_t dn_tmp_90=v_available;
(dn_tmp_89 > dn_tmp_90); })){
{ size_t dn_tmp_92=v_available;size_t *dn_tmp_91=&(v_count);*dn_tmp_91=dn_tmp_92; }
}
else{
}
{ uint32_t dn_tmp_97=({ uint32_t dn_tmp_93=(v_state)->f_remaining;
uint32_t dn_tmp_94=({ size_t dn_tmp_95=v_count;
if((unsigned __int128)dn_tmp_95>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_95; });
dn_U32_sub(dn_tmp_93,dn_tmp_94); });uint32_t *dn_tmp_96=&((v_state)->f_remaining);*dn_tmp_96=dn_tmp_97; }
uint32_t v_left=(v_state)->f_remaining;
bool v_complete=({ uint32_t dn_tmp_98=v_left;
uint32_t dn_tmp_99=({ uint32_t dn_tmp_100=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_100>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_100; });
(dn_tmp_98 == dn_tmp_99); });
if(v_complete){
(void)(({ dn_r_Frame32Decoder * dn_tmp_101=v_state;
dn_f_frame32_reset(dn_tmp_101); }));
}
else{
}
int32_t v_status=((int32_t)(0ULL));
if((({ size_t dn_tmp_102=v_count;
size_t dn_tmp_103=((size_t)(0ULL));
(dn_tmp_102 == dn_tmp_103); }) && (!(v_complete)))){
{ int32_t dn_tmp_105=((int32_t)(1ULL));int32_t *dn_tmp_104=&(v_status);*dn_tmp_104=dn_tmp_105; }
}
else{
}
{ dn_r_Frame32Step dn_tmp_116=({ size_t dn_tmp_106=({ size_t dn_tmp_107=({ size_t dn_tmp_108=v_position;
size_t dn_tmp_109=v_offset;
dn_USize_sub(dn_tmp_108,dn_tmp_109); });
size_t dn_tmp_110=v_count;
dn_USize_add(dn_tmp_107,dn_tmp_110); });
size_t dn_tmp_111=v_position;
size_t dn_tmp_112=v_count;
uint32_t dn_tmp_113=v_left;
bool dn_tmp_114=v_complete;
int32_t dn_tmp_115=v_status;
((dn_r_Frame32Step){dn_tmp_106,dn_tmp_111,dn_tmp_112,dn_tmp_113,dn_tmp_114,dn_tmp_115}); });return dn_tmp_116; }
}

dn_fail("missing native return");
}
static dn_r_Frame32Step dn_cb_frame32_feed(dn_r_Frame32Decoder * v_state,dn_t_slice0_U8 v_input,size_t v_offset) {
 if(!dn_enter()){return (dn_r_Frame32Step){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_Frame32Step){0};}
 dn_r_Frame32Step dn_result=dn_f_frame32_feed(v_state,v_input,v_offset);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static int32_t dn_f_frame32_finish(dn_r_Frame32Decoder const * v_state) {
{
if((!(({ dn_r_Frame32Decoder const * dn_tmp_0=v_state;
dn_f_frame32_valid(dn_tmp_0); })))){
{ int32_t dn_tmp_1=((int32_t)(4ULL));return dn_tmp_1; }
}
else{
}
if(({ int32_t dn_tmp_2=(v_state)->f_fault;
int32_t dn_tmp_3=({ int32_t dn_tmp_4=((int32_t)(0ULL));
if((__int128)dn_tmp_4<(-((__int128)1<<31))||(__int128)dn_tmp_4>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_4; });
(dn_tmp_2 != dn_tmp_3); })){
{ int32_t dn_tmp_5=(v_state)->f_fault;return dn_tmp_5; }
}
else{
}
if((({ uint32_t dn_tmp_6=(v_state)->f_prefix_count;
uint32_t dn_tmp_7=({ uint32_t dn_tmp_8=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_8>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_8; });
(dn_tmp_6 == dn_tmp_7); }) && ({ uint32_t dn_tmp_9=(v_state)->f_remaining;
uint32_t dn_tmp_10=({ uint32_t dn_tmp_11=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_11; });
(dn_tmp_9 == dn_tmp_10); }))){
{ int32_t dn_tmp_12=((int32_t)(0ULL));return dn_tmp_12; }
}
else{
}
{ int32_t dn_tmp_13=((int32_t)(1ULL));return dn_tmp_13; }
}

dn_fail("missing native return");
}
static int32_t dn_cb_frame32_finish(dn_r_Frame32Decoder const * v_state) {
 if(!dn_enter()){return (int32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (int32_t){0};}
 int32_t dn_result=dn_f_frame32_finish(v_state);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_slice0_U8 dn_f_frame32_view(dn_t_buf_U8 const * v_input,size_t v_base_offset,dn_r_Frame32Step v_step) {
{
(void)(({ bool dn_tmp_0=({ size_t dn_tmp_1=v_base_offset;
size_t dn_tmp_2=({ dn_t_buf_U8 const * dn_tmp_3=v_input;
(*(dn_tmp_3)).len; });
(dn_tmp_1 <= dn_tmp_2); });
(dn_assert(dn_tmp_0),0); }));
(void)(({ bool dn_tmp_4=({ size_t dn_tmp_5=(v_step).f_payload_offset;
size_t dn_tmp_6=({ size_t dn_tmp_7=({ dn_t_buf_U8 const * dn_tmp_8=v_input;
(*(dn_tmp_8)).len; });
size_t dn_tmp_9=v_base_offset;
dn_USize_sub(dn_tmp_7,dn_tmp_9); });
(dn_tmp_5 <= dn_tmp_6); });
(dn_assert(dn_tmp_4),0); }));
{ dn_t_slice0_U8 dn_tmp_15=({ dn_t_buf_U8 const * dn_tmp_10=v_input;
size_t dn_tmp_11=({ size_t dn_tmp_12=v_base_offset;
size_t dn_tmp_13=(v_step).f_payload_offset;
dn_USize_add(dn_tmp_12,dn_tmp_13); });
size_t dn_tmp_14=(v_step).f_payload_count;
({ dn_range(dn_tmp_11,dn_tmp_14,(*(dn_tmp_10)).len);(dn_t_slice0_U8){(*(dn_tmp_10)).data?(*(dn_tmp_10)).data+dn_tmp_11:NULL,dn_tmp_14}; }); });return dn_tmp_15; }
}

dn_fail("missing native return");
}
static dn_t_slice0_U8 dn_cb_frame32_view(dn_t_buf_U8 const * v_input,size_t v_base_offset,dn_r_Frame32Step v_step) {
 if(!dn_enter()){return (dn_t_slice0_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_slice0_U8){0};}
 dn_t_slice0_U8 dn_result=dn_f_frame32_view(v_input,v_base_offset,v_step);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static int32_t dn_f_frame32_write_header(dn_t_buf_U8 * v_output,size_t v_offset,uint32_t v_count,uint32_t v_maximum) {
{
if(({ uint32_t dn_tmp_0=v_count;
uint32_t dn_tmp_1=v_maximum;
(dn_tmp_0 > dn_tmp_1); })){
{ int32_t dn_tmp_2=((int32_t)(7ULL));return dn_tmp_2; }
}
else{
}
{ int32_t dn_tmp_8=({ dn_t_buf_U8 * dn_tmp_3=v_output;
size_t dn_tmp_4=v_offset;
size_t dn_tmp_5=((size_t)(4ULL));
uint64_t dn_tmp_6=({ uint32_t dn_tmp_7=v_count;
if((unsigned __int128)dn_tmp_7>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_7; });
dn_f_protocol_write_be(dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6); });return dn_tmp_8; }
}

dn_fail("missing native return");
}
static int32_t dn_cb_frame32_write_header(dn_t_buf_U8 * v_output,size_t v_offset,uint32_t v_count,uint32_t v_maximum) {
 if(!dn_enter()){return (int32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (int32_t){0};}
 int32_t dn_result=dn_f_frame32_write_header(v_output,v_offset,v_count,v_maximum);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_main(void) {
{
dn_t_buf_U8 volatile v_chunk={0};
dn_t_buf_U8 volatile v_integer={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_integer);
dn_drop_buf_U8(&v_chunk);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
dn_r_Frame32Decoder v_state=({ uint32_t dn_tmp_2=((uint32_t)(1048576ULL));
dn_f_frame32_decoder(dn_tmp_2); });
v_chunk=({ size_t dn_tmp_3=((size_t)(256ULL));
dn_new_buf_U8(dn_tmp_3); });
int32_t v_header=({ dn_t_buf_U8 * dn_tmp_4=(&(v_chunk));
size_t dn_tmp_5=((size_t)(0ULL));
uint32_t dn_tmp_6=((uint32_t)(1048576ULL));
uint32_t dn_tmp_7=((uint32_t)(1048576ULL));
dn_f_frame32_write_header(dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7); });
(void)(({ bool dn_tmp_8=({ int32_t dn_tmp_9=v_header;
int32_t dn_tmp_10=({ int32_t dn_tmp_11=((int32_t)(0ULL));
if((__int128)dn_tmp_11<(-((__int128)1<<31))||(__int128)dn_tmp_11>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_11; });
(dn_tmp_9 == dn_tmp_10); });
(dn_assert(dn_tmp_8),0); }));
{ size_t dn_tmp_13=((size_t)(0ULL));size_t dn_tmp_12=((size_t)(4ULL));for(size_t v_i=dn_tmp_13;v_i<dn_tmp_12;v_i++){
dn_r_Frame32Step v_step=({ dn_r_Frame32Decoder * dn_tmp_14=(&(v_state));
dn_t_slice0_U8 dn_tmp_15=({ dn_t_buf_U8 const * dn_tmp_16=(&(v_chunk));
size_t dn_tmp_17=v_i;
size_t dn_tmp_18=((size_t)(1ULL));
({ dn_range(dn_tmp_17,dn_tmp_18,(*(dn_tmp_16)).len);(dn_t_slice0_U8){(*(dn_tmp_16)).data?(*(dn_tmp_16)).data+dn_tmp_17:NULL,dn_tmp_18}; }); });
size_t dn_tmp_19=((size_t)(0ULL));
dn_f_frame32_feed(dn_tmp_14,dn_tmp_15,dn_tmp_19); });
(void)(({ bool dn_tmp_20=(({ size_t dn_tmp_21=(v_step).f_consumed;
size_t dn_tmp_22=({ size_t dn_tmp_23=((size_t)(1ULL));
if((unsigned __int128)dn_tmp_23>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_23; });
(dn_tmp_21 == dn_tmp_22); }) && ({ size_t dn_tmp_24=(v_step).f_payload_count;
size_t dn_tmp_25=({ size_t dn_tmp_26=((size_t)(0ULL));
if((unsigned __int128)dn_tmp_26>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_26; });
(dn_tmp_24 == dn_tmp_25); }));
(dn_assert(dn_tmp_20),0); }));
}
 }
size_t v_total=((size_t)(0ULL));
bool v_finished=0;
{ size_t dn_tmp_28=((size_t)(0ULL));size_t dn_tmp_27=((size_t)(4096ULL));for(size_t v_i=dn_tmp_28;v_i<dn_tmp_27;v_i++){
dn_r_Frame32Step v_step=({ dn_r_Frame32Decoder * dn_tmp_29=(&(v_state));
dn_t_slice0_U8 dn_tmp_30=({ dn_t_buf_U8 const * dn_tmp_31=(&(v_chunk));
size_t dn_tmp_32=((size_t)(0ULL));
size_t dn_tmp_33=((size_t)(256ULL));
({ dn_range(dn_tmp_32,dn_tmp_33,(*(dn_tmp_31)).len);(dn_t_slice0_U8){(*(dn_tmp_31)).data?(*(dn_tmp_31)).data+dn_tmp_32:NULL,dn_tmp_33}; }); });
size_t dn_tmp_34=((size_t)(0ULL));
dn_f_frame32_feed(dn_tmp_29,dn_tmp_30,dn_tmp_34); });
(void)(({ bool dn_tmp_35=(({ int32_t dn_tmp_36=(v_step).f_status;
int32_t dn_tmp_37=({ int32_t dn_tmp_38=((int32_t)(0ULL));
if((__int128)dn_tmp_38<(-((__int128)1<<31))||(__int128)dn_tmp_38>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_38; });
(dn_tmp_36 == dn_tmp_37); }) && ({ size_t dn_tmp_39=(v_step).f_consumed;
size_t dn_tmp_40=({ size_t dn_tmp_41=((size_t)(256ULL));
if((unsigned __int128)dn_tmp_41>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_41; });
(dn_tmp_39 == dn_tmp_40); }));
(dn_assert(dn_tmp_35),0); }));
{ size_t dn_tmp_45=({ size_t dn_tmp_42=v_total;
size_t dn_tmp_43=(v_step).f_payload_count;
dn_USize_add(dn_tmp_42,dn_tmp_43); });size_t *dn_tmp_44=&(v_total);*dn_tmp_44=dn_tmp_45; }
{ bool dn_tmp_47=(v_step).f_complete;bool *dn_tmp_46=&(v_finished);*dn_tmp_46=dn_tmp_47; }
}
 }
(void)(({ bool dn_tmp_48=((({ size_t dn_tmp_49=v_total;
size_t dn_tmp_50=({ size_t dn_tmp_51=((size_t)(1048576ULL));
if((unsigned __int128)dn_tmp_51>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_51; });
(dn_tmp_49 == dn_tmp_50); }) && v_finished) && ({ int32_t dn_tmp_52=({ dn_r_Frame32Decoder const * dn_tmp_53=(&(v_state));
dn_f_frame32_finish(dn_tmp_53); });
int32_t dn_tmp_54=({ int32_t dn_tmp_55=((int32_t)(0ULL));
if((__int128)dn_tmp_55<(-((__int128)1<<31))||(__int128)dn_tmp_55>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_55; });
(dn_tmp_52 == dn_tmp_54); }));
(dn_assert(dn_tmp_48),0); }));
v_integer=({ size_t dn_tmp_56=((size_t)(10ULL));
dn_new_buf_U8(dn_tmp_56); });
dn_r_ProtocolValue v_encoded=({ dn_t_buf_U8 * dn_tmp_57=(&(v_integer));
size_t dn_tmp_58=((size_t)(0ULL));
uint64_t dn_tmp_59=({ uint64_t dn_tmp_60=((uint64_t)(18446744073709551615ULL));
if((unsigned __int128)dn_tmp_60>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_60; });
dn_f_uleb128_write(dn_tmp_57,dn_tmp_58,dn_tmp_59); });
(void)(({ bool dn_tmp_61=(({ int32_t dn_tmp_62=(v_encoded).f_status;
int32_t dn_tmp_63=({ int32_t dn_tmp_64=((int32_t)(0ULL));
if((__int128)dn_tmp_64<(-((__int128)1<<31))||(__int128)dn_tmp_64>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_64; });
(dn_tmp_62 == dn_tmp_63); }) && ({ size_t dn_tmp_65=(v_encoded).f_next;
size_t dn_tmp_66=({ size_t dn_tmp_67=((size_t)(10ULL));
if((unsigned __int128)dn_tmp_67>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_67; });
(dn_tmp_65 == dn_tmp_66); }));
(dn_assert(dn_tmp_61),0); }));
dn_r_ProtocolValue v_decoded=({ dn_t_slice0_U8 dn_tmp_68=({ dn_t_buf_U8 const * dn_tmp_69=(&(v_integer));
size_t dn_tmp_70=((size_t)(0ULL));
size_t dn_tmp_71=((size_t)(10ULL));
({ dn_range(dn_tmp_70,dn_tmp_71,(*(dn_tmp_69)).len);(dn_t_slice0_U8){(*(dn_tmp_69)).data?(*(dn_tmp_69)).data+dn_tmp_70:NULL,dn_tmp_71}; }); });
size_t dn_tmp_72=((size_t)(0ULL));
bool dn_tmp_73=1;
dn_f_uleb128_read(dn_tmp_68,dn_tmp_72,dn_tmp_73); });
(void)(({ bool dn_tmp_74=(({ int32_t dn_tmp_75=(v_decoded).f_status;
int32_t dn_tmp_76=({ int32_t dn_tmp_77=((int32_t)(0ULL));
if((__int128)dn_tmp_77<(-((__int128)1<<31))||(__int128)dn_tmp_77>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_77; });
(dn_tmp_75 == dn_tmp_76); }) && ({ uint64_t dn_tmp_78=(v_decoded).f_value;
uint64_t dn_tmp_79=({ uint64_t dn_tmp_80=((uint64_t)(18446744073709551615ULL));
if((unsigned __int128)dn_tmp_80>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_80; });
(dn_tmp_78 == dn_tmp_79); }));
(dn_assert(dn_tmp_74),0); }));
(void)(({ bool dn_tmp_81=({ int64_t dn_tmp_82=({ uint64_t dn_tmp_83=({ int64_t dn_tmp_84=({ int64_t dn_tmp_85=((int64_t)(-INT64_C(9223372036854775807)-1));
if((__int128)dn_tmp_85<(-((__int128)1<<63))||(__int128)dn_tmp_85>(((__int128)1<<63)-1))dn_fail("numeric conversion out of range");(int64_t)dn_tmp_85; });
dn_f_zigzag_encode(dn_tmp_84); });
dn_f_zigzag_decode(dn_tmp_83); });
int64_t dn_tmp_86=({ int64_t dn_tmp_87=((int64_t)(-INT64_C(9223372036854775807)-1));
if((__int128)dn_tmp_87<(-((__int128)1<<63))||(__int128)dn_tmp_87>(((__int128)1<<63)-1))dn_fail("numeric conversion out of range");(int64_t)dn_tmp_87; });
(dn_tmp_82 == dn_tmp_86); });
(dn_assert(dn_tmp_81),0); }));
(void)(({ dn_utf8 dn_tmp_88=((dn_utf8){(const uint8_t*)"streaming codec OK", 18});
(fwrite(dn_tmp_88.data,1,dn_tmp_88.len,stdout),fputc('\n',stdout),0); }));
dn_drop_buf_U8(&v_integer);
dn_drop_buf_U8(&v_chunk);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_main(void) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_main();dn_boundary_end(&dn_b,0);dn_leave();return;
}
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
DN_EXPORT size_t dlm_native_sizeof_Frame32Decoder(void){return 20;}
DN_EXPORT size_t dlm_native_alignof_Frame32Decoder(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_Frame32Decoder(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Frame32Decoder_maximum(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Frame32Decoder_header(void){return 4;}
DN_EXPORT size_t dlm_native_offset_Frame32Decoder_prefix_count(void){return 8;}
DN_EXPORT size_t dlm_native_offset_Frame32Decoder_remaining(void){return 12;}
DN_EXPORT size_t dlm_native_offset_Frame32Decoder_fault(void){return 16;}
DN_EXPORT size_t dlm_native_sizeof_Frame32Step(void){return 40;}
DN_EXPORT size_t dlm_native_alignof_Frame32Step(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_Frame32Step(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Frame32Step_consumed(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Frame32Step_payload_offset(void){return 8;}
DN_EXPORT size_t dlm_native_offset_Frame32Step_payload_count(void){return 16;}
DN_EXPORT size_t dlm_native_offset_Frame32Step_remaining(void){return 24;}
DN_EXPORT size_t dlm_native_offset_Frame32Step_complete(void){return 28;}
DN_EXPORT size_t dlm_native_offset_Frame32Step_status(void){return 32;}
int main(void){(void)dlm_native_thread_init();dn_boundary b={0};jmp_buf dn_jump;b.jump=&dn_jump;b.foreign_marker=dn_foreign_context;dn_current=&b;dn_fp_begin(&b);if(setjmp(dn_jump)){dn_boundary_end(&b,1);fprintf(stderr,"DLM native error: %s\n",dn_error);return 1;}(void)dn_f_main();dn_boundary_end(&b,0);(void)dlm_native_runtime_shutdown();return 0;}
