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
struct dn_r_TransportRequirements {
    uint32_t f_required;
    uint32_t f_forbidden;
    uint32_t f_preferred;
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
static uint8_t dn_f_main(void) {
{
dn_r_TransportBounds v_bounds=({ uint32_t dn_tmp_0=((uint32_t)(1024ULL));
uint32_t dn_tmp_1=((uint32_t)(8ULL));
uint32_t dn_tmp_2=((uint32_t)(8ULL));
size_t dn_tmp_3=((size_t)(8388608ULL));
((dn_r_TransportBounds){dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3}); });
dn_t_arr6_r18_TransportCandidate v_candidates=({ uint32_t dn_tmp_4=((uint32_t)(63ULL));
dn_f_transport_candidates(dn_tmp_4); });
dn_r_TransportChoice v_ordered_secure=({ dn_r_TransportRequirements dn_tmp_5=({ uint32_t dn_tmp_6=((uint32_t)(23ULL));
uint32_t dn_tmp_7=((uint32_t)(0ULL));
uint32_t dn_tmp_8=((uint32_t)(8ULL));
((dn_r_TransportRequirements){dn_tmp_6,dn_tmp_7,dn_tmp_8}); });
dn_r_TransportBounds dn_tmp_9=v_bounds;
dn_t_arr6_r18_TransportCandidate dn_tmp_10=v_candidates;
dn_f_transport_select(dn_tmp_5,dn_tmp_9,dn_tmp_10); });
(void)(({ bool dn_tmp_11=(({ int32_t dn_tmp_12=(v_ordered_secure).f_status;
int32_t dn_tmp_13=((int32_t)(0ULL));
(dn_tmp_12 == dn_tmp_13); }) && ({ uint32_t dn_tmp_14=(v_ordered_secure).f_profile;
uint32_t dn_tmp_15=((uint32_t)(4ULL));
(dn_tmp_14 == dn_tmp_15); }));
(dn_assert(dn_tmp_11),0); }));
dn_r_TransportChoice v_reliable_unordered=({ dn_r_TransportRequirements dn_tmp_16=({ uint32_t dn_tmp_17=((uint32_t)(85ULL));
uint32_t dn_tmp_18=((uint32_t)(2ULL));
uint32_t dn_tmp_19=((uint32_t)(0ULL));
((dn_r_TransportRequirements){dn_tmp_17,dn_tmp_18,dn_tmp_19}); });
dn_r_TransportBounds dn_tmp_20=v_bounds;
dn_t_arr6_r18_TransportCandidate dn_tmp_21=v_candidates;
dn_f_transport_select(dn_tmp_16,dn_tmp_20,dn_tmp_21); });
(void)(({ bool dn_tmp_22=(({ int32_t dn_tmp_23=(v_reliable_unordered).f_status;
int32_t dn_tmp_24=((int32_t)(0ULL));
(dn_tmp_23 == dn_tmp_24); }) && ({ uint32_t dn_tmp_25=(v_reliable_unordered).f_profile;
uint32_t dn_tmp_26=((uint32_t)(5ULL));
(dn_tmp_25 == dn_tmp_26); }));
(dn_assert(dn_tmp_22),0); }));
dn_r_TransportChoice v_datagram=({ dn_r_TransportRequirements dn_tmp_27=({ uint32_t dn_tmp_28=((uint32_t)(84ULL));
uint32_t dn_tmp_29=((uint32_t)(3ULL));
uint32_t dn_tmp_30=((uint32_t)(0ULL));
((dn_r_TransportRequirements){dn_tmp_28,dn_tmp_29,dn_tmp_30}); });
dn_r_TransportBounds dn_tmp_31=v_bounds;
dn_t_arr6_r18_TransportCandidate dn_tmp_32=v_candidates;
dn_f_transport_select(dn_tmp_27,dn_tmp_31,dn_tmp_32); });
(void)(({ bool dn_tmp_33=(({ int32_t dn_tmp_34=(v_datagram).f_status;
int32_t dn_tmp_35=((int32_t)(0ULL));
(dn_tmp_34 == dn_tmp_35); }) && ({ uint32_t dn_tmp_36=(v_datagram).f_profile;
uint32_t dn_tmp_37=((uint32_t)(6ULL));
(dn_tmp_36 == dn_tmp_37); }));
(dn_assert(dn_tmp_33),0); }));
dn_r_TransportChoice v_impossible=({ dn_r_TransportRequirements dn_tmp_38=({ uint32_t dn_tmp_39=((uint32_t)(256ULL));
uint32_t dn_tmp_40=((uint32_t)(0ULL));
uint32_t dn_tmp_41=((uint32_t)(0ULL));
((dn_r_TransportRequirements){dn_tmp_39,dn_tmp_40,dn_tmp_41}); });
dn_r_TransportBounds dn_tmp_42=v_bounds;
dn_t_arr6_r18_TransportCandidate dn_tmp_43=v_candidates;
dn_f_transport_select(dn_tmp_38,dn_tmp_42,dn_tmp_43); });
(void)(({ bool dn_tmp_44=(({ int32_t dn_tmp_45=(v_impossible).f_status;
int32_t dn_tmp_46=((int32_t)(6ULL));
(dn_tmp_45 == dn_tmp_46); }) && ({ uint32_t dn_tmp_47=(v_impossible).f_profile;
uint32_t dn_tmp_48=((uint32_t)(0ULL));
(dn_tmp_47 == dn_tmp_48); }));
(dn_assert(dn_tmp_44),0); }));
dn_r_TransportChoice v_contradictory=({ dn_r_TransportRequirements dn_tmp_49=({ uint32_t dn_tmp_50=((uint32_t)(66ULL));
uint32_t dn_tmp_51=((uint32_t)(0ULL));
uint32_t dn_tmp_52=((uint32_t)(0ULL));
((dn_r_TransportRequirements){dn_tmp_50,dn_tmp_51,dn_tmp_52}); });
dn_r_TransportBounds dn_tmp_53=v_bounds;
dn_t_arr6_r18_TransportCandidate dn_tmp_54=v_candidates;
dn_f_transport_select(dn_tmp_49,dn_tmp_53,dn_tmp_54); });
(void)(({ bool dn_tmp_55=({ int32_t dn_tmp_56=(v_contradictory).f_status;
int32_t dn_tmp_57=((int32_t)(4ULL));
(dn_tmp_56 == dn_tmp_57); });
(dn_assert(dn_tmp_55),0); }));
{ size_t dn_tmp_59=((size_t)(0ULL));size_t dn_tmp_58=((size_t)(512ULL));for(size_t v_mask=dn_tmp_59;v_mask<dn_tmp_58;v_mask++){
dn_r_TransportRequirements v_req=({ uint32_t dn_tmp_60=({ size_t dn_tmp_61=v_mask;
if((unsigned __int128)dn_tmp_61>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_61; });
uint32_t dn_tmp_62=((uint32_t)(0ULL));
uint32_t dn_tmp_63=((uint32_t)(0ULL));
((dn_r_TransportRequirements){dn_tmp_60,dn_tmp_62,dn_tmp_63}); });
dn_r_TransportChoice v_result=({ dn_r_TransportRequirements dn_tmp_64=v_req;
dn_r_TransportBounds dn_tmp_65=v_bounds;
dn_t_arr6_r18_TransportCandidate dn_tmp_66=v_candidates;
dn_f_transport_select(dn_tmp_64,dn_tmp_65,dn_tmp_66); });
if(({ int32_t dn_tmp_67=(v_result).f_status;
int32_t dn_tmp_68=((int32_t)(0ULL));
(dn_tmp_67 == dn_tmp_68); })){
(void)(({ bool dn_tmp_69=({ uint32_t dn_tmp_70=({ uint32_t dn_tmp_71=(v_result).f_properties;
uint32_t dn_tmp_72=(v_req).f_required;
dn_U32_bits((uint64_t)dn_tmp_71 & (uint64_t)dn_tmp_72); });
uint32_t dn_tmp_73=(v_req).f_required;
(dn_tmp_70 == dn_tmp_73); });
(dn_assert(dn_tmp_69),0); }));
(void)(({ bool dn_tmp_74=({ int32_t dn_tmp_75=(({ uint32_t dn_tmp_76=(v_result).f_profile;
dn_r_TransportRequirements dn_tmp_77=v_req;
dn_r_TransportBounds dn_tmp_78=v_bounds;
dn_f_transport_check(dn_tmp_76,dn_tmp_77,dn_tmp_78); })).f_status;
int32_t dn_tmp_79=((int32_t)(0ULL));
(dn_tmp_75 == dn_tmp_79); });
(dn_assert(dn_tmp_74),0); }));
}
else{
}
}
 }
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
int main(void){(void)dlm_native_thread_init();dn_boundary b={0};jmp_buf dn_jump;b.jump=&dn_jump;b.foreign_marker=dn_foreign_context;dn_current=&b;dn_fp_begin(&b);if(setjmp(dn_jump)){dn_boundary_end(&b,1);fprintf(stderr,"DLM native error: %s\n",dn_error);return 1;}(void)dn_f_main();dn_boundary_end(&b,0);(void)dlm_native_runtime_shutdown();return 0;}
