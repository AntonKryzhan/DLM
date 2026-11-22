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
typedef struct dn_r_Host dn_r_Host;
typedef struct dn_r_Overlapped dn_r_Overlapped;
typedef struct dn_r_Gene dn_r_Gene;
typedef struct dn_r_Individual dn_r_Individual;
typedef struct dn_r_TrainingView dn_r_TrainingView;
typedef struct dn_r_StartupView dn_r_StartupView;
typedef struct dn_r_WinClass dn_r_WinClass;
typedef struct dn_r_WinMessage dn_r_WinMessage;
typedef struct dn_r_Rect dn_r_Rect;
typedef struct dn_r_CreateStruct dn_r_CreateStruct;
typedef struct dn_r_ShellExecute dn_r_ShellExecute;
typedef struct dn_r_OpenFileName dn_r_OpenFileName;
typedef struct dn_r_PaintStruct dn_r_PaintStruct;
typedef struct dn_r_DrawItem dn_r_DrawItem;
typedef struct dn_r_MinMaxInfo dn_r_MinMaxInfo;
typedef struct dn_r_Control dn_r_Control;
typedef struct dn_r_Gui dn_r_Gui;
typedef struct dn_t_resource_h_close_handle dn_t_resource_h_close_handle;
typedef struct dn_t_resource_h_delete_object dn_t_resource_h_delete_object;
typedef struct dn_t_resource_h_free_local dn_t_resource_h_free_local;
struct dn_t_resource_h_close_handle { void * value;bool active; };
struct dn_t_resource_h_delete_object { void * value;bool active; };
struct dn_t_resource_h_free_local { void * value;bool active; };
struct dn_r_Control {
    void * f_window;
    size_t f_id;
    int32_t f_x;
    int32_t f_y;
    int32_t f_width;
    int32_t f_height;
};
struct dn_r_CreateStruct {
    void * f_parameter;
    void * f_instance;
    void * f_menu;
    void * f_parent;
    int32_t f_height;
    int32_t f_width;
    int32_t f_y;
    int32_t f_x;
    int32_t f_style;
    uint16_t * f_name;
    uint16_t * f_class_;
    uint32_t f_exstyle;
};
struct dn_r_Rect {
    int32_t f_left;
    int32_t f_top;
    int32_t f_right;
    int32_t f_bottom;
};
struct dn_r_DrawItem {
    uint32_t f_type_;
    uint32_t f_control_id;
    uint32_t f_item_id;
    uint32_t f_action;
    uint32_t f_state;
    void * f_window;
    void * f_dc;
    dn_r_Rect f_rect;
    size_t f_data_;
};
struct dn_r_Gene {
    uint32_t f_reverse;
    uint32_t f_cut1;
    uint32_t f_cut2;
    uint32_t f_overlap;
    uint32_t f_guard;
    uint32_t f_repeats;
    uint32_t f_gap;
    uint32_t f_template_;
};
typedef struct dn_t_buf_r7_Control { dn_r_Control *data;size_t len,cap; } dn_t_buf_r7_Control;
typedef struct dn_t_buf_U8 { uint8_t *data;size_t len,cap; } dn_t_buf_U8;
struct dn_r_TrainingView {
    uint32_t f_elapsed;
    uint32_t f_budget;
    uint32_t f_generation;
    uint32_t f_tested;
    uint32_t f_passed;
    uint32_t f_total;
    uint32_t f_controls_passed;
    uint32_t f_controls_total;
    uint32_t f_attributable;
    uint32_t f_validated;
    uint32_t f_media;
    uint32_t f_phase;
    uint32_t f_browser;
    uint64_t f_method;
    uint32_t f_target;
    uint32_t f_telegram_web_passed;
    uint32_t f_telegram_web_total;
    uint32_t f_telegram_desktop_passed;
    uint32_t f_telegram_desktop_total;
    uint32_t f_mwo_passed;
    uint32_t f_mwo_total;
};
struct dn_r_Gui {
    dn_t_buf_r7_Control f_controls;
    void * f_window;
    void * f_font;
    void * f_title_font;
    void * f_status_font;
    void * f_background;
    void * f_card;
    void * f_accent;
    void * f_secondary;
    dn_t_resource_h_close_handle * f_process;
    dn_t_resource_h_close_handle * f_preflight;
    dn_t_buf_U8 f_note;
    uint32_t f_phase;
    uint64_t f_deadline;
    int32_t f_dpi;
    dn_t_resource_h_close_handle * f_training;
    dn_r_TrainingView f_view;
    bool f_history;
    dn_t_buf_U8 f_session;
};
typedef struct dn_t_arr254_U8 { uint8_t data[254]; } dn_t_arr254_U8;
struct dn_r_Host {
    dn_t_arr254_U8 f_bytes;
    size_t f_length;
};
struct dn_r_Individual {
    dn_r_Gene f_gene;
    uint32_t f_fitness;
    uint32_t f_trials;
};
struct dn_r_MinMaxInfo {
    int32_t f_reserved_x;
    int32_t f_reserved_y;
    int32_t f_max_x;
    int32_t f_max_y;
    int32_t f_position_x;
    int32_t f_position_y;
    int32_t f_minimum_x;
    int32_t f_minimum_y;
    int32_t f_track_x;
    int32_t f_track_y;
};
struct dn_r_OpenFileName {
    uint32_t f_size;
    void * f_owner;
    void * f_instance;
    uint16_t * f_filter;
    uint16_t * f_custom_filter;
    uint32_t f_custom_size;
    uint32_t f_filter_index;
    uint16_t * f_file;
    uint32_t f_file_capacity;
    uint16_t * f_file_title;
    uint32_t f_title_capacity;
    uint16_t * f_initial_dir;
    uint16_t * f_title;
    uint32_t f_flags;
    uint16_t f_file_offset;
    uint16_t f_extension_offset;
    uint16_t * f_default_extension;
    void * f_userdata;
    void * f_hook;
    uint16_t * f_template_;
    uint8_t * f_reserved;
    uint32_t f_reserved_word;
    uint32_t f_extended_flags;
};
struct dn_r_Overlapped {
    size_t f_internal;
    size_t f_internal_high;
    uint64_t f_offset;
    void * f_event;
};
typedef struct dn_t_arr32_U8 { uint8_t data[32]; } dn_t_arr32_U8;
struct dn_r_PaintStruct {
    void * f_dc;
    int32_t f_erase;
    dn_r_Rect f_rect;
    int32_t f_restore;
    int32_t f_update;
    dn_t_arr32_U8 f_reserved;
};
struct dn_r_ShellExecute {
    uint32_t f_size;
    uint32_t f_mask;
    void * f_window;
    uint16_t * f_verb;
    uint16_t * f_file;
    uint16_t * f_parameters;
    uint16_t * f_directory;
    int32_t f_show;
    void * f_instance;
    uint8_t * f_idlist;
    uint16_t * f_class_;
    void * f_class_key;
    uint32_t f_hotkey;
    void * f_icon;
    void * f_process;
};
struct dn_r_StartupView {
    bool f_found;
    bool f_ready;
    bool f_driver;
    uint32_t f_error;
};
typedef int64_t (*dn_t_fn_h__U32__USize__h_ret_I64)(void *,uint32_t,size_t,void *);
struct dn_r_WinClass {
    uint32_t f_size;
    uint32_t f_style;
    dn_t_fn_h__U32__USize__h_ret_I64 f_procedure;
    int32_t f_class_extra;
    int32_t f_window_extra;
    void * f_instance;
    void * f_icon;
    void * f_cursor;
    void * f_background;
    uint16_t * f_menu;
    uint16_t * f_name;
    void * f_small_icon;
};
struct dn_r_WinMessage {
    void * f_window;
    uint32_t f_message;
    size_t f_wparam;
    void * f_lparam;
    uint32_t f_time;
    int32_t f_x;
    int32_t f_y;
    uint32_t f_private_;
};
typedef struct dn_t_arr20_U8 { uint8_t data[20]; } dn_t_arr20_U8;
typedef struct dn_t_arr12_U32 { uint32_t data[12]; } dn_t_arr12_U32;
typedef struct dn_t_arr7_str { dn_utf8 data[7]; } dn_t_arr7_str;
typedef struct dn_t_buf_U16 { uint16_t *data;size_t len,cap; } dn_t_buf_U16;
typedef struct dn_t_buf_r4_Gene { dn_r_Gene *data;size_t len,cap; } dn_t_buf_r4_Gene;
typedef struct dn_t_buf_r10_Individual { dn_r_Individual *data;size_t len,cap; } dn_t_buf_r10_Individual;
typedef struct dn_t_buf_buf_U8 { dn_t_buf_U8 *data;size_t len,cap; } dn_t_buf_buf_U8;
typedef struct dn_t_slice0_U8 { uint8_t const *data;size_t len; } dn_t_slice0_U8;
typedef int32_t (*dn_t_fn_U32_ret_I32)(uint32_t);
_Static_assert(sizeof(dn_r_Host)==264,"Host ABI size");
_Static_assert(_Alignof(dn_r_Host)==8,"Host ABI alignment");
_Static_assert(offsetof(dn_r_Host,f_bytes)==0,"Host.bytes ABI offset");
_Static_assert(offsetof(dn_r_Host,f_length)==256,"Host.length ABI offset");
_Static_assert(sizeof(dn_r_Overlapped)==32,"Overlapped ABI size");
_Static_assert(_Alignof(dn_r_Overlapped)==8,"Overlapped ABI alignment");
_Static_assert(offsetof(dn_r_Overlapped,f_internal)==0,"Overlapped.internal ABI offset");
_Static_assert(offsetof(dn_r_Overlapped,f_internal_high)==8,"Overlapped.internal_high ABI offset");
_Static_assert(offsetof(dn_r_Overlapped,f_offset)==16,"Overlapped.offset ABI offset");
_Static_assert(offsetof(dn_r_Overlapped,f_event)==24,"Overlapped.event ABI offset");
_Static_assert(sizeof(dn_r_Gene)==32,"Gene ABI size");
_Static_assert(_Alignof(dn_r_Gene)==4,"Gene ABI alignment");
_Static_assert(offsetof(dn_r_Gene,f_reverse)==0,"Gene.reverse ABI offset");
_Static_assert(offsetof(dn_r_Gene,f_cut1)==4,"Gene.cut1 ABI offset");
_Static_assert(offsetof(dn_r_Gene,f_cut2)==8,"Gene.cut2 ABI offset");
_Static_assert(offsetof(dn_r_Gene,f_overlap)==12,"Gene.overlap ABI offset");
_Static_assert(offsetof(dn_r_Gene,f_guard)==16,"Gene.guard ABI offset");
_Static_assert(offsetof(dn_r_Gene,f_repeats)==20,"Gene.repeats ABI offset");
_Static_assert(offsetof(dn_r_Gene,f_gap)==24,"Gene.gap ABI offset");
_Static_assert(offsetof(dn_r_Gene,f_template_)==28,"Gene.template_ ABI offset");
_Static_assert(sizeof(dn_r_Individual)==40,"Individual ABI size");
_Static_assert(_Alignof(dn_r_Individual)==4,"Individual ABI alignment");
_Static_assert(offsetof(dn_r_Individual,f_gene)==0,"Individual.gene ABI offset");
_Static_assert(offsetof(dn_r_Individual,f_fitness)==32,"Individual.fitness ABI offset");
_Static_assert(offsetof(dn_r_Individual,f_trials)==36,"Individual.trials ABI offset");
_Static_assert(sizeof(dn_r_TrainingView)==96,"TrainingView ABI size");
_Static_assert(_Alignof(dn_r_TrainingView)==8,"TrainingView ABI alignment");
_Static_assert(offsetof(dn_r_TrainingView,f_elapsed)==0,"TrainingView.elapsed ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_budget)==4,"TrainingView.budget ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_generation)==8,"TrainingView.generation ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_tested)==12,"TrainingView.tested ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_passed)==16,"TrainingView.passed ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_total)==20,"TrainingView.total ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_controls_passed)==24,"TrainingView.controls_passed ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_controls_total)==28,"TrainingView.controls_total ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_attributable)==32,"TrainingView.attributable ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_validated)==36,"TrainingView.validated ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_media)==40,"TrainingView.media ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_phase)==44,"TrainingView.phase ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_browser)==48,"TrainingView.browser ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_method)==56,"TrainingView.method ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_target)==64,"TrainingView.target ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_telegram_web_passed)==68,"TrainingView.telegram_web_passed ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_telegram_web_total)==72,"TrainingView.telegram_web_total ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_telegram_desktop_passed)==76,"TrainingView.telegram_desktop_passed ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_telegram_desktop_total)==80,"TrainingView.telegram_desktop_total ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_mwo_passed)==84,"TrainingView.mwo_passed ABI offset");
_Static_assert(offsetof(dn_r_TrainingView,f_mwo_total)==88,"TrainingView.mwo_total ABI offset");
_Static_assert(sizeof(dn_r_StartupView)==8,"StartupView ABI size");
_Static_assert(_Alignof(dn_r_StartupView)==4,"StartupView ABI alignment");
_Static_assert(offsetof(dn_r_StartupView,f_found)==0,"StartupView.found ABI offset");
_Static_assert(offsetof(dn_r_StartupView,f_ready)==1,"StartupView.ready ABI offset");
_Static_assert(offsetof(dn_r_StartupView,f_driver)==2,"StartupView.driver ABI offset");
_Static_assert(offsetof(dn_r_StartupView,f_error)==4,"StartupView.error ABI offset");
_Static_assert(sizeof(dn_r_WinClass)==80,"WinClass ABI size");
_Static_assert(_Alignof(dn_r_WinClass)==8,"WinClass ABI alignment");
_Static_assert(offsetof(dn_r_WinClass,f_size)==0,"WinClass.size ABI offset");
_Static_assert(offsetof(dn_r_WinClass,f_style)==4,"WinClass.style ABI offset");
_Static_assert(offsetof(dn_r_WinClass,f_procedure)==8,"WinClass.procedure ABI offset");
_Static_assert(offsetof(dn_r_WinClass,f_class_extra)==16,"WinClass.class_extra ABI offset");
_Static_assert(offsetof(dn_r_WinClass,f_window_extra)==20,"WinClass.window_extra ABI offset");
_Static_assert(offsetof(dn_r_WinClass,f_instance)==24,"WinClass.instance ABI offset");
_Static_assert(offsetof(dn_r_WinClass,f_icon)==32,"WinClass.icon ABI offset");
_Static_assert(offsetof(dn_r_WinClass,f_cursor)==40,"WinClass.cursor ABI offset");
_Static_assert(offsetof(dn_r_WinClass,f_background)==48,"WinClass.background ABI offset");
_Static_assert(offsetof(dn_r_WinClass,f_menu)==56,"WinClass.menu ABI offset");
_Static_assert(offsetof(dn_r_WinClass,f_name)==64,"WinClass.name ABI offset");
_Static_assert(offsetof(dn_r_WinClass,f_small_icon)==72,"WinClass.small_icon ABI offset");
_Static_assert(sizeof(dn_r_WinMessage)==48,"WinMessage ABI size");
_Static_assert(_Alignof(dn_r_WinMessage)==8,"WinMessage ABI alignment");
_Static_assert(offsetof(dn_r_WinMessage,f_window)==0,"WinMessage.window ABI offset");
_Static_assert(offsetof(dn_r_WinMessage,f_message)==8,"WinMessage.message ABI offset");
_Static_assert(offsetof(dn_r_WinMessage,f_wparam)==16,"WinMessage.wparam ABI offset");
_Static_assert(offsetof(dn_r_WinMessage,f_lparam)==24,"WinMessage.lparam ABI offset");
_Static_assert(offsetof(dn_r_WinMessage,f_time)==32,"WinMessage.time ABI offset");
_Static_assert(offsetof(dn_r_WinMessage,f_x)==36,"WinMessage.x ABI offset");
_Static_assert(offsetof(dn_r_WinMessage,f_y)==40,"WinMessage.y ABI offset");
_Static_assert(offsetof(dn_r_WinMessage,f_private_)==44,"WinMessage.private_ ABI offset");
_Static_assert(sizeof(dn_r_Rect)==16,"Rect ABI size");
_Static_assert(_Alignof(dn_r_Rect)==4,"Rect ABI alignment");
_Static_assert(offsetof(dn_r_Rect,f_left)==0,"Rect.left ABI offset");
_Static_assert(offsetof(dn_r_Rect,f_top)==4,"Rect.top ABI offset");
_Static_assert(offsetof(dn_r_Rect,f_right)==8,"Rect.right ABI offset");
_Static_assert(offsetof(dn_r_Rect,f_bottom)==12,"Rect.bottom ABI offset");
_Static_assert(sizeof(dn_r_CreateStruct)==80,"CreateStruct ABI size");
_Static_assert(_Alignof(dn_r_CreateStruct)==8,"CreateStruct ABI alignment");
_Static_assert(offsetof(dn_r_CreateStruct,f_parameter)==0,"CreateStruct.parameter ABI offset");
_Static_assert(offsetof(dn_r_CreateStruct,f_instance)==8,"CreateStruct.instance ABI offset");
_Static_assert(offsetof(dn_r_CreateStruct,f_menu)==16,"CreateStruct.menu ABI offset");
_Static_assert(offsetof(dn_r_CreateStruct,f_parent)==24,"CreateStruct.parent ABI offset");
_Static_assert(offsetof(dn_r_CreateStruct,f_height)==32,"CreateStruct.height ABI offset");
_Static_assert(offsetof(dn_r_CreateStruct,f_width)==36,"CreateStruct.width ABI offset");
_Static_assert(offsetof(dn_r_CreateStruct,f_y)==40,"CreateStruct.y ABI offset");
_Static_assert(offsetof(dn_r_CreateStruct,f_x)==44,"CreateStruct.x ABI offset");
_Static_assert(offsetof(dn_r_CreateStruct,f_style)==48,"CreateStruct.style ABI offset");
_Static_assert(offsetof(dn_r_CreateStruct,f_name)==56,"CreateStruct.name ABI offset");
_Static_assert(offsetof(dn_r_CreateStruct,f_class_)==64,"CreateStruct.class_ ABI offset");
_Static_assert(offsetof(dn_r_CreateStruct,f_exstyle)==72,"CreateStruct.exstyle ABI offset");
_Static_assert(sizeof(dn_r_ShellExecute)==112,"ShellExecute ABI size");
_Static_assert(_Alignof(dn_r_ShellExecute)==8,"ShellExecute ABI alignment");
_Static_assert(offsetof(dn_r_ShellExecute,f_size)==0,"ShellExecute.size ABI offset");
_Static_assert(offsetof(dn_r_ShellExecute,f_mask)==4,"ShellExecute.mask ABI offset");
_Static_assert(offsetof(dn_r_ShellExecute,f_window)==8,"ShellExecute.window ABI offset");
_Static_assert(offsetof(dn_r_ShellExecute,f_verb)==16,"ShellExecute.verb ABI offset");
_Static_assert(offsetof(dn_r_ShellExecute,f_file)==24,"ShellExecute.file ABI offset");
_Static_assert(offsetof(dn_r_ShellExecute,f_parameters)==32,"ShellExecute.parameters ABI offset");
_Static_assert(offsetof(dn_r_ShellExecute,f_directory)==40,"ShellExecute.directory ABI offset");
_Static_assert(offsetof(dn_r_ShellExecute,f_show)==48,"ShellExecute.show ABI offset");
_Static_assert(offsetof(dn_r_ShellExecute,f_instance)==56,"ShellExecute.instance ABI offset");
_Static_assert(offsetof(dn_r_ShellExecute,f_idlist)==64,"ShellExecute.idlist ABI offset");
_Static_assert(offsetof(dn_r_ShellExecute,f_class_)==72,"ShellExecute.class_ ABI offset");
_Static_assert(offsetof(dn_r_ShellExecute,f_class_key)==80,"ShellExecute.class_key ABI offset");
_Static_assert(offsetof(dn_r_ShellExecute,f_hotkey)==88,"ShellExecute.hotkey ABI offset");
_Static_assert(offsetof(dn_r_ShellExecute,f_icon)==96,"ShellExecute.icon ABI offset");
_Static_assert(offsetof(dn_r_ShellExecute,f_process)==104,"ShellExecute.process ABI offset");
_Static_assert(sizeof(dn_r_OpenFileName)==152,"OpenFileName ABI size");
_Static_assert(_Alignof(dn_r_OpenFileName)==8,"OpenFileName ABI alignment");
_Static_assert(offsetof(dn_r_OpenFileName,f_size)==0,"OpenFileName.size ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_owner)==8,"OpenFileName.owner ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_instance)==16,"OpenFileName.instance ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_filter)==24,"OpenFileName.filter ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_custom_filter)==32,"OpenFileName.custom_filter ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_custom_size)==40,"OpenFileName.custom_size ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_filter_index)==44,"OpenFileName.filter_index ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_file)==48,"OpenFileName.file ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_file_capacity)==56,"OpenFileName.file_capacity ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_file_title)==64,"OpenFileName.file_title ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_title_capacity)==72,"OpenFileName.title_capacity ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_initial_dir)==80,"OpenFileName.initial_dir ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_title)==88,"OpenFileName.title ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_flags)==96,"OpenFileName.flags ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_file_offset)==100,"OpenFileName.file_offset ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_extension_offset)==102,"OpenFileName.extension_offset ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_default_extension)==104,"OpenFileName.default_extension ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_userdata)==112,"OpenFileName.userdata ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_hook)==120,"OpenFileName.hook ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_template_)==128,"OpenFileName.template_ ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_reserved)==136,"OpenFileName.reserved ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_reserved_word)==144,"OpenFileName.reserved_word ABI offset");
_Static_assert(offsetof(dn_r_OpenFileName,f_extended_flags)==148,"OpenFileName.extended_flags ABI offset");
_Static_assert(sizeof(dn_r_PaintStruct)==72,"PaintStruct ABI size");
_Static_assert(_Alignof(dn_r_PaintStruct)==8,"PaintStruct ABI alignment");
_Static_assert(offsetof(dn_r_PaintStruct,f_dc)==0,"PaintStruct.dc ABI offset");
_Static_assert(offsetof(dn_r_PaintStruct,f_erase)==8,"PaintStruct.erase ABI offset");
_Static_assert(offsetof(dn_r_PaintStruct,f_rect)==12,"PaintStruct.rect ABI offset");
_Static_assert(offsetof(dn_r_PaintStruct,f_restore)==28,"PaintStruct.restore ABI offset");
_Static_assert(offsetof(dn_r_PaintStruct,f_update)==32,"PaintStruct.update ABI offset");
_Static_assert(offsetof(dn_r_PaintStruct,f_reserved)==36,"PaintStruct.reserved ABI offset");
_Static_assert(sizeof(dn_r_DrawItem)==64,"DrawItem ABI size");
_Static_assert(_Alignof(dn_r_DrawItem)==8,"DrawItem ABI alignment");
_Static_assert(offsetof(dn_r_DrawItem,f_type_)==0,"DrawItem.type_ ABI offset");
_Static_assert(offsetof(dn_r_DrawItem,f_control_id)==4,"DrawItem.control_id ABI offset");
_Static_assert(offsetof(dn_r_DrawItem,f_item_id)==8,"DrawItem.item_id ABI offset");
_Static_assert(offsetof(dn_r_DrawItem,f_action)==12,"DrawItem.action ABI offset");
_Static_assert(offsetof(dn_r_DrawItem,f_state)==16,"DrawItem.state ABI offset");
_Static_assert(offsetof(dn_r_DrawItem,f_window)==24,"DrawItem.window ABI offset");
_Static_assert(offsetof(dn_r_DrawItem,f_dc)==32,"DrawItem.dc ABI offset");
_Static_assert(offsetof(dn_r_DrawItem,f_rect)==40,"DrawItem.rect ABI offset");
_Static_assert(offsetof(dn_r_DrawItem,f_data_)==56,"DrawItem.data_ ABI offset");
_Static_assert(sizeof(dn_r_MinMaxInfo)==40,"MinMaxInfo ABI size");
_Static_assert(_Alignof(dn_r_MinMaxInfo)==4,"MinMaxInfo ABI alignment");
_Static_assert(offsetof(dn_r_MinMaxInfo,f_reserved_x)==0,"MinMaxInfo.reserved_x ABI offset");
_Static_assert(offsetof(dn_r_MinMaxInfo,f_reserved_y)==4,"MinMaxInfo.reserved_y ABI offset");
_Static_assert(offsetof(dn_r_MinMaxInfo,f_max_x)==8,"MinMaxInfo.max_x ABI offset");
_Static_assert(offsetof(dn_r_MinMaxInfo,f_max_y)==12,"MinMaxInfo.max_y ABI offset");
_Static_assert(offsetof(dn_r_MinMaxInfo,f_position_x)==16,"MinMaxInfo.position_x ABI offset");
_Static_assert(offsetof(dn_r_MinMaxInfo,f_position_y)==20,"MinMaxInfo.position_y ABI offset");
_Static_assert(offsetof(dn_r_MinMaxInfo,f_minimum_x)==24,"MinMaxInfo.minimum_x ABI offset");
_Static_assert(offsetof(dn_r_MinMaxInfo,f_minimum_y)==28,"MinMaxInfo.minimum_y ABI offset");
_Static_assert(offsetof(dn_r_MinMaxInfo,f_track_x)==32,"MinMaxInfo.track_x ABI offset");
_Static_assert(offsetof(dn_r_MinMaxInfo,f_track_y)==36,"MinMaxInfo.track_y ABI offset");
_Static_assert(sizeof(dn_r_Control)==32,"Control ABI size");
_Static_assert(_Alignof(dn_r_Control)==8,"Control ABI alignment");
_Static_assert(offsetof(dn_r_Control,f_window)==0,"Control.window ABI offset");
_Static_assert(offsetof(dn_r_Control,f_id)==8,"Control.id ABI offset");
_Static_assert(offsetof(dn_r_Control,f_x)==16,"Control.x ABI offset");
_Static_assert(offsetof(dn_r_Control,f_y)==20,"Control.y ABI offset");
_Static_assert(offsetof(dn_r_Control,f_width)==24,"Control.width ABI offset");
_Static_assert(offsetof(dn_r_Control,f_height)==28,"Control.height ABI offset");
_Static_assert(sizeof(dn_r_Gui)==288,"Gui ABI size");
_Static_assert(_Alignof(dn_r_Gui)==8,"Gui ABI alignment");
_Static_assert(offsetof(dn_r_Gui,f_controls)==0,"Gui.controls ABI offset");
_Static_assert(offsetof(dn_r_Gui,f_window)==24,"Gui.window ABI offset");
_Static_assert(offsetof(dn_r_Gui,f_font)==32,"Gui.font ABI offset");
_Static_assert(offsetof(dn_r_Gui,f_title_font)==40,"Gui.title_font ABI offset");
_Static_assert(offsetof(dn_r_Gui,f_status_font)==48,"Gui.status_font ABI offset");
_Static_assert(offsetof(dn_r_Gui,f_background)==56,"Gui.background ABI offset");
_Static_assert(offsetof(dn_r_Gui,f_card)==64,"Gui.card ABI offset");
_Static_assert(offsetof(dn_r_Gui,f_accent)==72,"Gui.accent ABI offset");
_Static_assert(offsetof(dn_r_Gui,f_secondary)==80,"Gui.secondary ABI offset");
_Static_assert(offsetof(dn_r_Gui,f_process)==88,"Gui.process ABI offset");
_Static_assert(offsetof(dn_r_Gui,f_preflight)==96,"Gui.preflight ABI offset");
_Static_assert(offsetof(dn_r_Gui,f_note)==104,"Gui.note ABI offset");
_Static_assert(offsetof(dn_r_Gui,f_phase)==128,"Gui.phase ABI offset");
_Static_assert(offsetof(dn_r_Gui,f_deadline)==136,"Gui.deadline ABI offset");
_Static_assert(offsetof(dn_r_Gui,f_dpi)==144,"Gui.dpi ABI offset");
_Static_assert(offsetof(dn_r_Gui,f_training)==152,"Gui.training ABI offset");
_Static_assert(offsetof(dn_r_Gui,f_view)==160,"Gui.view ABI offset");
_Static_assert(offsetof(dn_r_Gui,f_history)==256,"Gui.history ABI offset");
_Static_assert(offsetof(dn_r_Gui,f_session)==264,"Gui.session ABI offset");
static void dn_drop_resource_h_close_handle(dn_t_resource_h_close_handle * volatile*);static void dn_retain_resource_h_close_handle(dn_t_resource_h_close_handle * volatile*);
static void dn_drop_resource_h_delete_object(dn_t_resource_h_delete_object * volatile*);static void dn_retain_resource_h_delete_object(dn_t_resource_h_delete_object * volatile*);
static void dn_drop_resource_h_free_local(dn_t_resource_h_free_local * volatile*);static void dn_retain_resource_h_free_local(dn_t_resource_h_free_local * volatile*);
static void dn_drop_r3_Gui(dn_r_Gui volatile*);static void dn_retain_r3_Gui(dn_r_Gui volatile*);
static void dn_drop_buf_U8(dn_t_buf_U8 volatile*);static void dn_retain_buf_U8(dn_t_buf_U8 volatile*);
static void dn_drop_buf_U16(dn_t_buf_U16 volatile*);static void dn_retain_buf_U16(dn_t_buf_U16 volatile*);
static void dn_drop_buf_r7_Control(dn_t_buf_r7_Control volatile*);static void dn_retain_buf_r7_Control(dn_t_buf_r7_Control volatile*);
static void dn_drop_buf_r4_Gene(dn_t_buf_r4_Gene volatile*);static void dn_retain_buf_r4_Gene(dn_t_buf_r4_Gene volatile*);
static void dn_drop_buf_r10_Individual(dn_t_buf_r10_Individual volatile*);static void dn_retain_buf_r10_Individual(dn_t_buf_r10_Individual volatile*);
static void dn_drop_buf_buf_U8(dn_t_buf_buf_U8 volatile*);static void dn_retain_buf_buf_U8(dn_t_buf_buf_U8 volatile*);
static void dn_drop_box_r3_Gui(dn_r_Gui * volatile*);static void dn_retain_box_r3_Gui(dn_r_Gui * volatile*);
static void dn_adopt_resource_h_close_handle(dn_t_resource_h_close_handle * volatile*);
static void dn_adopt_resource_h_delete_object(dn_t_resource_h_delete_object * volatile*);
static void dn_adopt_resource_h_free_local(dn_t_resource_h_free_local * volatile*);
static void dn_adopt_r3_Gui(dn_r_Gui volatile*);
static void dn_adopt_buf_U8(dn_t_buf_U8 volatile*);
static void dn_adopt_buf_U16(dn_t_buf_U16 volatile*);
static void dn_adopt_buf_r7_Control(dn_t_buf_r7_Control volatile*);
static void dn_adopt_buf_r4_Gene(dn_t_buf_r4_Gene volatile*);
static void dn_adopt_buf_r10_Individual(dn_t_buf_r10_Individual volatile*);
static void dn_adopt_buf_buf_U8(dn_t_buf_buf_U8 volatile*);
static void dn_adopt_box_r3_Gui(dn_r_Gui * volatile*);
static size_t dn_f_min_size(size_t v_a,size_t v_b);
static size_t dn_f_max_size(size_t v_a,size_t v_b);
static uint8_t dn_f_ascii_lower(uint8_t v_c);
static bool dn_f_space(uint8_t v_c);
static bool dn_f_bytes_at(dn_t_slice0_U8 v_b,size_t v_at,dn_t_slice0_U8 v_text);
static bool dn_f_bytes_equal(dn_t_slice0_U8 v_a,dn_t_slice0_U8 v_b);
static bool dn_f_ascii_at(dn_t_slice0_U8 v_b,size_t v_at,dn_t_slice0_U8 v_text);
static bool dn_f_ascii_equal(dn_t_slice0_U8 v_a,dn_t_slice0_U8 v_b);
static size_t dn_f_bytes_find(dn_t_slice0_U8 v_b,size_t v_start,dn_t_slice0_U8 v_text);
static size_t dn_f_byte_find(dn_t_slice0_U8 v_b,size_t v_start,uint8_t v_c);
static dn_t_slice0_U8 dn_f_trimmed(dn_t_slice0_U8 v_b);
static dn_t_buf_U8 dn_f_bytes_copy(dn_t_slice0_U8 v_b);
static uint8_t dn_f_bytes_append(dn_t_buf_U8 * v_b,dn_t_slice0_U8 v_text);
static bool dn_f_decimal(dn_t_slice0_U8 v_b,uint32_t v_limit,uint32_t * v_value);
static uint8_t dn_f_append_number(dn_t_buf_U8 * v_b,uint64_t v_number);
static dn_t_buf_U8 dn_f_zbytes(dn_t_slice0_U8 v_text);
static uint16_t dn_f_be16(dn_t_slice0_U8 v_b,size_t v_p);
static uint32_t dn_f_be32(dn_t_slice0_U8 v_b,size_t v_p);
static dn_r_Host dn_f_domain(dn_t_slice0_U8 v_b,bool v_wildcard);
static uint64_t dn_f_host_hash(dn_r_Host const * v_h);
static bool dn_f_host_equal(dn_r_Host const * v_a,dn_r_Host const * v_b);
static size_t dn_f_middle_label(dn_r_Host const * v_h);
static bool dn_f_handle_valid(void * v_h);
static uint8_t dn_f_close_handle(void * * v_value);
static uint8_t dn_f_free_local(void * * v_value);
static dn_t_buf_U16 dn_f_wide(dn_t_slice0_U8 v_text);
static dn_t_buf_U8 dn_f_utf8_wide(uint16_t * v_source,size_t v_count);
static size_t dn_f_wide_length(uint16_t * v_source);
static dn_t_buf_buf_U8 dn_f_command_arguments(void);
static dn_t_buf_U8 dn_f_executable_path(void);
static dn_t_buf_U8 dn_f_directory_path(dn_t_slice0_U8 v_path);
static bool dn_f_set_application_directory(void);
static dn_t_buf_U8 dn_f_read_file(dn_t_slice0_U8 v_path,size_t v_maximum);
static bool dn_f_write_all(void * v_file,dn_t_slice0_U8 v_b);
static bool dn_f_write_file(dn_t_slice0_U8 v_path,dn_t_slice0_U8 v_b);
static uint8_t dn_f_log_line(void * v_log,dn_t_slice0_U8 v_text);
static uint8_t dn_f_log_error(void * v_log,dn_t_slice0_U8 v_text,uint32_t v_code);
static dn_t_buf_U8 dn_f_stop_name(uint32_t v_pid);
static bool dn_f_signal_stop(uint32_t v_pid);
static int32_t dn_f_console_control(uint32_t v_kind);
static dn_r_Gene dn_f_gene_default(void);
static bool dn_f_gene_valid(dn_r_Gene const * v_g);
static uint32_t dn_f_gene_field(dn_r_Gene const * v_g,size_t v_i);
static uint8_t dn_f_gene_set(dn_r_Gene * v_g,size_t v_i,uint32_t v_n);
static bool dn_f_gene_parse(dn_t_slice0_U8 v_text,dn_r_Gene * v_g);
static dn_t_buf_U8 dn_f_gene_text(dn_r_Gene const * v_g);
static uint64_t dn_f_gene_hash(dn_r_Gene const * v_g);
static uint32_t dn_f_evolution_random(uint64_t * v_state);
static uint8_t dn_f_gene_normalize(dn_r_Gene * v_g);
static uint8_t dn_f_gene_mutate(dn_r_Gene * v_g,uint64_t * v_state);
static uint32_t dn_f_min_u32(uint32_t v_a,uint32_t v_b);
static dn_r_Gene dn_f_gene_cross(dn_r_Gene const * v_a,dn_r_Gene const * v_b,uint64_t * v_state);
static size_t dn_f_tournament(dn_t_buf_r10_Individual const * v_items,uint64_t * v_state);
static dn_t_buf_r4_Gene dn_f_evolve(dn_t_buf_r10_Individual const * v_items,uint64_t * v_state,size_t v_count);
static bool dn_f_profile_gene(dn_t_slice0_U8 v_text,dn_r_Gene * v_gene,uint32_t * v_target);
static bool dn_f_training_view(dn_t_slice0_U8 v_text,dn_r_TrainingView * v_view);
static dn_t_buf_U8 dn_f_training_caption(dn_r_TrainingView const * v_v,bool v_history);
static dn_t_buf_U8 dn_f_training_clock(dn_r_TrainingView const * v_v);
static dn_r_StartupView dn_f_startup_view(dn_t_slice0_U8 v_text);
static dn_t_buf_U8 dn_f_startup_caption(uint32_t v_code);
static uint8_t dn_f_delete_object(void * * v_value);
static void * dn_f_control(dn_r_Gui const * v_g,size_t v_id);
static uint32_t dn_f_rgb(uint32_t v_red,uint32_t v_green,uint32_t v_blue);
static uint8_t dn_f_ui_log(dn_r_Gui * v_g,dn_t_slice0_U8 v_text);
static int32_t dn_f_scale(dn_r_Gui const * v_g,int32_t v_value);
static int32_t dn_f_width_at(dn_r_Gui const * v_g,int32_t v_value);
static uint8_t dn_f_rounded(void * v_dc,dn_r_Rect v_rect,void * v_brush,int32_t v_radius);
static uint8_t dn_f_paint_text(dn_r_Gui const * v_g,void * v_dc,dn_t_slice0_U8 v_text,void * v_font,uint32_t v_color,int32_t v_x,int32_t v_y,int32_t v_width,int32_t v_height);
static uint8_t dn_f_gui_paint(dn_r_Gui const * v_g);
static uint8_t dn_f_gui_layout(dn_r_Gui const * v_g);
static uint8_t dn_f_add_button(dn_r_Gui * v_g,dn_utf8 v_title,size_t v_id,int32_t v_x,int32_t v_y,int32_t v_width,int32_t v_height);
static uint32_t dn_f_selected_target(dn_r_Gui const * v_g);
static uint8_t dn_f_gui_build(dn_r_Gui * v_g);
static uint8_t dn_f_gui_draw_button(dn_r_Gui const * v_g,dn_r_DrawItem const * v_item);
static void * dn_f_launch(dn_r_Gui const * v_g,dn_t_slice0_U8 v_filename,dn_t_slice0_U8 v_parameters,bool v_elevate,int32_t v_show);
static dn_t_buf_U8 dn_f_local_path(dn_utf8 v_name);
static uint8_t dn_f_running_ui(dn_r_Gui const * v_g);
static uint8_t dn_f_gui_stop(dn_r_Gui * v_g);
static uint8_t dn_f_start_engine(dn_r_Gui * v_g);
static uint8_t dn_f_gui_start(dn_r_Gui * v_g);
static bool dn_f_ready(uint32_t v_pid);
static uint8_t dn_f_gui_poll(dn_r_Gui * v_g);
static uint8_t dn_f_auxiliary(dn_r_Gui * v_g,dn_utf8 v_filename,dn_t_slice0_U8 v_parameters);
static dn_t_buf_U8 dn_f_training_stop_path(dn_r_Gui const * v_g);
static uint8_t dn_f_stop_training(dn_r_Gui * v_g,bool v_wait);
static uint8_t dn_f_start_training(dn_r_Gui * v_g);
static uint8_t dn_f_poll_training(dn_r_Gui * v_g);
static uint8_t dn_f_profile_dialog(dn_r_Gui * v_g,bool v_save);
static int64_t dn_f_window_proc(void * v_window,uint32_t v_message,size_t v_wparam,void * v_lparam);
static uint8_t dn_f_gui_main(void);
static uint8_t dn_f_main(void);
static void dn_drop_resource_h_close_handle(dn_t_resource_h_close_handle * volatile *x); static void dn_retain_resource_h_close_handle(dn_t_resource_h_close_handle * volatile *x);
static void dn_final_resource_h_close_handle(void *raw){dn_t_resource_h_close_handle *p=raw;if(p->active){p->active=false;(void)dn_f_close_handle(&p->value);}}
static dn_t_resource_h_close_handle * dn_begin_resource_h_close_handle(void){dn_t_resource_h_close_handle *p=dn_allocate_aligned(sizeof *p,_Alignof(dn_t_resource_h_close_handle));((dn_mem*)p-1)->finalize=dn_final_resource_h_close_handle;return p;}
static dn_t_resource_h_close_handle * dn_commit_resource_h_close_handle(dn_t_resource_h_close_handle * p,void * v){p->value=v;p->active=true;return p;}
static void dn_drop_resource_h_close_handle(dn_t_resource_h_close_handle * volatile *x){if(*x){dn_t_resource_h_close_handle *p=*x;*x=NULL;((dn_mem*)p-1)->finalize=NULL;dn_final_resource_h_close_handle(p);dn_deallocate(p);}}
static void dn_retain_resource_h_close_handle(dn_t_resource_h_close_handle * volatile *x){if(*x)dn_detach(*x);}
static void * dn_take_resource_h_close_handle(dn_t_resource_h_close_handle * p){dn_nonnull(p);void * v=p->value;p->active=false;dn_deallocate(p);return v;}
static void dn_adopt_resource_h_close_handle(dn_t_resource_h_close_handle * volatile*x){dn_adopt(*x);}
static void dn_drop_resource_h_delete_object(dn_t_resource_h_delete_object * volatile *x); static void dn_retain_resource_h_delete_object(dn_t_resource_h_delete_object * volatile *x);
static void dn_final_resource_h_delete_object(void *raw){dn_t_resource_h_delete_object *p=raw;if(p->active){p->active=false;(void)dn_f_delete_object(&p->value);}}
static dn_t_resource_h_delete_object * dn_begin_resource_h_delete_object(void){dn_t_resource_h_delete_object *p=dn_allocate_aligned(sizeof *p,_Alignof(dn_t_resource_h_delete_object));((dn_mem*)p-1)->finalize=dn_final_resource_h_delete_object;return p;}
static dn_t_resource_h_delete_object * dn_commit_resource_h_delete_object(dn_t_resource_h_delete_object * p,void * v){p->value=v;p->active=true;return p;}
static void dn_drop_resource_h_delete_object(dn_t_resource_h_delete_object * volatile *x){if(*x){dn_t_resource_h_delete_object *p=*x;*x=NULL;((dn_mem*)p-1)->finalize=NULL;dn_final_resource_h_delete_object(p);dn_deallocate(p);}}
static void dn_retain_resource_h_delete_object(dn_t_resource_h_delete_object * volatile *x){if(*x)dn_detach(*x);}
static void * dn_take_resource_h_delete_object(dn_t_resource_h_delete_object * p){dn_nonnull(p);void * v=p->value;p->active=false;dn_deallocate(p);return v;}
static void dn_adopt_resource_h_delete_object(dn_t_resource_h_delete_object * volatile*x){dn_adopt(*x);}
static void dn_drop_resource_h_free_local(dn_t_resource_h_free_local * volatile *x); static void dn_retain_resource_h_free_local(dn_t_resource_h_free_local * volatile *x);
static void dn_final_resource_h_free_local(void *raw){dn_t_resource_h_free_local *p=raw;if(p->active){p->active=false;(void)dn_f_free_local(&p->value);}}
static dn_t_resource_h_free_local * dn_begin_resource_h_free_local(void){dn_t_resource_h_free_local *p=dn_allocate_aligned(sizeof *p,_Alignof(dn_t_resource_h_free_local));((dn_mem*)p-1)->finalize=dn_final_resource_h_free_local;return p;}
static dn_t_resource_h_free_local * dn_commit_resource_h_free_local(dn_t_resource_h_free_local * p,void * v){p->value=v;p->active=true;return p;}
static void dn_drop_resource_h_free_local(dn_t_resource_h_free_local * volatile *x){if(*x){dn_t_resource_h_free_local *p=*x;*x=NULL;((dn_mem*)p-1)->finalize=NULL;dn_final_resource_h_free_local(p);dn_deallocate(p);}}
static void dn_retain_resource_h_free_local(dn_t_resource_h_free_local * volatile *x){if(*x)dn_detach(*x);}
static void * dn_take_resource_h_free_local(dn_t_resource_h_free_local * p){dn_nonnull(p);void * v=p->value;p->active=false;dn_deallocate(p);return v;}
static void dn_adopt_resource_h_free_local(dn_t_resource_h_free_local * volatile*x){dn_adopt(*x);}
static void dn_drop_r3_Gui(dn_r_Gui volatile *x); static void dn_retain_r3_Gui(dn_r_Gui volatile *x);
static void dn_drop_r3_Gui(dn_r_Gui volatile *x){dn_drop_buf_U8(&x->f_session);dn_drop_resource_h_close_handle(&x->f_training);dn_drop_buf_U8(&x->f_note);dn_drop_resource_h_close_handle(&x->f_preflight);dn_drop_resource_h_close_handle(&x->f_process);dn_drop_buf_r7_Control(&x->f_controls);}
static void dn_retain_r3_Gui(dn_r_Gui volatile *x){dn_retain_buf_r7_Control(&x->f_controls);dn_retain_resource_h_close_handle(&x->f_process);dn_retain_resource_h_close_handle(&x->f_preflight);dn_retain_buf_U8(&x->f_note);dn_retain_resource_h_close_handle(&x->f_training);dn_retain_buf_U8(&x->f_session);}
static void dn_adopt_r3_Gui(dn_r_Gui volatile*x){dn_adopt_buf_r7_Control(&x->f_controls);dn_adopt_resource_h_close_handle(&x->f_process);dn_adopt_resource_h_close_handle(&x->f_preflight);dn_adopt_buf_U8(&x->f_note);dn_adopt_resource_h_close_handle(&x->f_training);dn_adopt_buf_U8(&x->f_session);}
static void dn_drop_buf_U8(dn_t_buf_U8 volatile *x); static void dn_retain_buf_U8(dn_t_buf_U8 volatile *x);
static void dn_drop_buf_U8(dn_t_buf_U8 volatile *x){dn_deallocate(x->data);*x=(dn_t_buf_U8){0};}
static void dn_retain_buf_U8(dn_t_buf_U8 volatile *x){dn_detach(x->data);}
static dn_t_buf_U8 dn_new_buf_U8(size_t n){dn_t_buf_U8 x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(uint8_t)),_Alignof(uint8_t));x.len=x.cap=n;return x;}
static dn_t_buf_U8 dn_aligned_buf_U8(size_t n,size_t a){if(a<_Alignof(uint8_t)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_U8 x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(uint8_t)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_U8(dn_t_buf_U8 *x,uint8_t v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;uint8_t *p=dn_allocate_aligned(dn_size(cap,sizeof(uint8_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(uint8_t));if(x->len)memcpy(p,x->data,x->len*sizeof(uint8_t));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;x->len++;return 0;}
static uint8_t dn_resize_buf_U8(dn_t_buf_U8 *x,size_t n){if(n<x->len){}if(n>x->cap){size_t cap=n;uint8_t *p=dn_allocate_aligned(dn_size(cap,sizeof(uint8_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(uint8_t));if(x->len)memcpy(p,x->data,x->len*sizeof(uint8_t));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(uint8_t));x->len=n;return 0;}
static void dn_adopt_buf_U8(dn_t_buf_U8 volatile*x){dn_adopt(x->data);}
static void dn_drop_buf_U16(dn_t_buf_U16 volatile *x); static void dn_retain_buf_U16(dn_t_buf_U16 volatile *x);
static void dn_drop_buf_U16(dn_t_buf_U16 volatile *x){dn_deallocate(x->data);*x=(dn_t_buf_U16){0};}
static void dn_retain_buf_U16(dn_t_buf_U16 volatile *x){dn_detach(x->data);}
static dn_t_buf_U16 dn_new_buf_U16(size_t n){dn_t_buf_U16 x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(uint16_t)),_Alignof(uint16_t));x.len=x.cap=n;return x;}
static dn_t_buf_U16 dn_aligned_buf_U16(size_t n,size_t a){if(a<_Alignof(uint16_t)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_U16 x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(uint16_t)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_U16(dn_t_buf_U16 *x,uint16_t v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;uint16_t *p=dn_allocate_aligned(dn_size(cap,sizeof(uint16_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(uint16_t));if(x->len)memcpy(p,x->data,x->len*sizeof(uint16_t));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;x->len++;return 0;}
static uint8_t dn_resize_buf_U16(dn_t_buf_U16 *x,size_t n){if(n<x->len){}if(n>x->cap){size_t cap=n;uint16_t *p=dn_allocate_aligned(dn_size(cap,sizeof(uint16_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(uint16_t));if(x->len)memcpy(p,x->data,x->len*sizeof(uint16_t));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(uint16_t));x->len=n;return 0;}
static void dn_adopt_buf_U16(dn_t_buf_U16 volatile*x){dn_adopt(x->data);}
static void dn_drop_buf_r7_Control(dn_t_buf_r7_Control volatile *x); static void dn_retain_buf_r7_Control(dn_t_buf_r7_Control volatile *x);
static void dn_drop_buf_r7_Control(dn_t_buf_r7_Control volatile *x){dn_deallocate(x->data);*x=(dn_t_buf_r7_Control){0};}
static void dn_retain_buf_r7_Control(dn_t_buf_r7_Control volatile *x){dn_detach(x->data);}
static dn_t_buf_r7_Control dn_new_buf_r7_Control(size_t n){dn_t_buf_r7_Control x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(dn_r_Control)),_Alignof(dn_r_Control));x.len=x.cap=n;return x;}
static dn_t_buf_r7_Control dn_aligned_buf_r7_Control(size_t n,size_t a){if(a<_Alignof(dn_r_Control)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_r7_Control x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(dn_r_Control)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_r7_Control(dn_t_buf_r7_Control *x,dn_r_Control v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;dn_r_Control *p=dn_allocate_aligned(dn_size(cap,sizeof(dn_r_Control)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(dn_r_Control));if(x->len)memcpy(p,x->data,x->len*sizeof(dn_r_Control));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;x->len++;return 0;}
static uint8_t dn_resize_buf_r7_Control(dn_t_buf_r7_Control *x,size_t n){if(n<x->len){}if(n>x->cap){size_t cap=n;dn_r_Control *p=dn_allocate_aligned(dn_size(cap,sizeof(dn_r_Control)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(dn_r_Control));if(x->len)memcpy(p,x->data,x->len*sizeof(dn_r_Control));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(dn_r_Control));x->len=n;return 0;}
static void dn_adopt_buf_r7_Control(dn_t_buf_r7_Control volatile*x){dn_adopt(x->data);}
static void dn_drop_buf_r4_Gene(dn_t_buf_r4_Gene volatile *x); static void dn_retain_buf_r4_Gene(dn_t_buf_r4_Gene volatile *x);
static void dn_drop_buf_r4_Gene(dn_t_buf_r4_Gene volatile *x){dn_deallocate(x->data);*x=(dn_t_buf_r4_Gene){0};}
static void dn_retain_buf_r4_Gene(dn_t_buf_r4_Gene volatile *x){dn_detach(x->data);}
static dn_t_buf_r4_Gene dn_new_buf_r4_Gene(size_t n){dn_t_buf_r4_Gene x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(dn_r_Gene)),_Alignof(dn_r_Gene));x.len=x.cap=n;return x;}
static dn_t_buf_r4_Gene dn_aligned_buf_r4_Gene(size_t n,size_t a){if(a<_Alignof(dn_r_Gene)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_r4_Gene x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(dn_r_Gene)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_r4_Gene(dn_t_buf_r4_Gene *x,dn_r_Gene v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;dn_r_Gene *p=dn_allocate_aligned(dn_size(cap,sizeof(dn_r_Gene)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(dn_r_Gene));if(x->len)memcpy(p,x->data,x->len*sizeof(dn_r_Gene));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;x->len++;return 0;}
static uint8_t dn_resize_buf_r4_Gene(dn_t_buf_r4_Gene *x,size_t n){if(n<x->len){}if(n>x->cap){size_t cap=n;dn_r_Gene *p=dn_allocate_aligned(dn_size(cap,sizeof(dn_r_Gene)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(dn_r_Gene));if(x->len)memcpy(p,x->data,x->len*sizeof(dn_r_Gene));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(dn_r_Gene));x->len=n;return 0;}
static void dn_adopt_buf_r4_Gene(dn_t_buf_r4_Gene volatile*x){dn_adopt(x->data);}
static void dn_drop_buf_r10_Individual(dn_t_buf_r10_Individual volatile *x); static void dn_retain_buf_r10_Individual(dn_t_buf_r10_Individual volatile *x);
static void dn_drop_buf_r10_Individual(dn_t_buf_r10_Individual volatile *x){dn_deallocate(x->data);*x=(dn_t_buf_r10_Individual){0};}
static void dn_retain_buf_r10_Individual(dn_t_buf_r10_Individual volatile *x){dn_detach(x->data);}
static dn_t_buf_r10_Individual dn_new_buf_r10_Individual(size_t n){dn_t_buf_r10_Individual x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(dn_r_Individual)),_Alignof(dn_r_Individual));x.len=x.cap=n;return x;}
static dn_t_buf_r10_Individual dn_aligned_buf_r10_Individual(size_t n,size_t a){if(a<_Alignof(dn_r_Individual)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_r10_Individual x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(dn_r_Individual)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_r10_Individual(dn_t_buf_r10_Individual *x,dn_r_Individual v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;dn_r_Individual *p=dn_allocate_aligned(dn_size(cap,sizeof(dn_r_Individual)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(dn_r_Individual));if(x->len)memcpy(p,x->data,x->len*sizeof(dn_r_Individual));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;x->len++;return 0;}
static uint8_t dn_resize_buf_r10_Individual(dn_t_buf_r10_Individual *x,size_t n){if(n<x->len){}if(n>x->cap){size_t cap=n;dn_r_Individual *p=dn_allocate_aligned(dn_size(cap,sizeof(dn_r_Individual)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(dn_r_Individual));if(x->len)memcpy(p,x->data,x->len*sizeof(dn_r_Individual));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(dn_r_Individual));x->len=n;return 0;}
static void dn_adopt_buf_r10_Individual(dn_t_buf_r10_Individual volatile*x){dn_adopt(x->data);}
static void dn_drop_buf_buf_U8(dn_t_buf_buf_U8 volatile *x); static void dn_retain_buf_buf_U8(dn_t_buf_buf_U8 volatile *x);
static void dn_drop_buf_buf_U8(dn_t_buf_buf_U8 volatile *x){for(size_t i=0;i<x->len;i++)dn_drop_buf_U8(&x->data[i]);dn_deallocate(x->data);*x=(dn_t_buf_buf_U8){0};}
static void dn_retain_buf_buf_U8(dn_t_buf_buf_U8 volatile *x){for(size_t i=0;i<x->len;i++)dn_retain_buf_U8(&x->data[i]);dn_detach(x->data);}
static dn_t_buf_buf_U8 dn_new_buf_buf_U8(size_t n){dn_t_buf_buf_U8 x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(dn_t_buf_U8)),_Alignof(dn_t_buf_U8));x.len=x.cap=n;return x;}
static dn_t_buf_buf_U8 dn_aligned_buf_buf_U8(size_t n,size_t a){if(a<_Alignof(dn_t_buf_U8)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_buf_U8 x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(dn_t_buf_U8)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_buf_U8(dn_t_buf_buf_U8 *x,dn_t_buf_U8 v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;dn_t_buf_U8 *p=dn_allocate_aligned(dn_size(cap,sizeof(dn_t_buf_U8)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(dn_t_buf_U8));if(x->len)memcpy(p,x->data,x->len*sizeof(dn_t_buf_U8));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;dn_retain_buf_U8(&x->data[x->len]);x->len++;return 0;}
static uint8_t dn_resize_buf_buf_U8(dn_t_buf_buf_U8 *x,size_t n){if(n<x->len){for(size_t i=n;i<x->len;i++)dn_drop_buf_U8(&x->data[i]);}if(n>x->cap){size_t cap=n;dn_t_buf_U8 *p=dn_allocate_aligned(dn_size(cap,sizeof(dn_t_buf_U8)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(dn_t_buf_U8));if(x->len)memcpy(p,x->data,x->len*sizeof(dn_t_buf_U8));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(dn_t_buf_U8));x->len=n;return 0;}
static void dn_adopt_buf_buf_U8(dn_t_buf_buf_U8 volatile*x){for(size_t i=0;i<x->len;i++)dn_adopt_buf_U8(&x->data[i]);dn_adopt(x->data);}
static void dn_drop_box_r3_Gui(dn_r_Gui * volatile *x); static void dn_retain_box_r3_Gui(dn_r_Gui * volatile *x);
static dn_r_Gui * dn_new_box_r3_Gui(dn_r_Gui v){dn_r_Gui * p=dn_allocate_aligned(sizeof(dn_r_Gui),_Alignof(dn_r_Gui));*p=v;return p;}
static void dn_drop_box_r3_Gui(dn_r_Gui * volatile *x){if(*x){dn_drop_r3_Gui(*x);dn_deallocate(*x);*x=0;}}
static void dn_retain_box_r3_Gui(dn_r_Gui * volatile *x){if(*x){dn_retain_r3_Gui(*x);dn_detach(*x);}}
static void dn_adopt_box_r3_Gui(dn_r_Gui * volatile*x){if(*x){dn_adopt_r3_Gui(*x);dn_adopt(*x);}}
static size_t dn_cb_min_size(size_t v_a,size_t v_b);
static size_t dn_cb_max_size(size_t v_a,size_t v_b);
static uint8_t dn_cb_ascii_lower(uint8_t v_c);
static bool dn_cb_space(uint8_t v_c);
static bool dn_cb_bytes_at(dn_t_slice0_U8 v_b,size_t v_at,dn_t_slice0_U8 v_text);
static bool dn_cb_bytes_equal(dn_t_slice0_U8 v_a,dn_t_slice0_U8 v_b);
static bool dn_cb_ascii_at(dn_t_slice0_U8 v_b,size_t v_at,dn_t_slice0_U8 v_text);
static bool dn_cb_ascii_equal(dn_t_slice0_U8 v_a,dn_t_slice0_U8 v_b);
static size_t dn_cb_bytes_find(dn_t_slice0_U8 v_b,size_t v_start,dn_t_slice0_U8 v_text);
static size_t dn_cb_byte_find(dn_t_slice0_U8 v_b,size_t v_start,uint8_t v_c);
static dn_t_slice0_U8 dn_cb_trimmed(dn_t_slice0_U8 v_b);
static dn_t_buf_U8 dn_cb_bytes_copy(dn_t_slice0_U8 v_b);
static void dn_cb_bytes_append(dn_t_buf_U8 * v_b,dn_t_slice0_U8 v_text);
static bool dn_cb_decimal(dn_t_slice0_U8 v_b,uint32_t v_limit,uint32_t * v_value);
static void dn_cb_append_number(dn_t_buf_U8 * v_b,uint64_t v_number);
static dn_t_buf_U8 dn_cb_zbytes(dn_t_slice0_U8 v_text);
static uint16_t dn_cb_be16(dn_t_slice0_U8 v_b,size_t v_p);
static uint32_t dn_cb_be32(dn_t_slice0_U8 v_b,size_t v_p);
static dn_r_Host dn_cb_domain(dn_t_slice0_U8 v_b,bool v_wildcard);
static uint64_t dn_cb_host_hash(dn_r_Host const * v_h);
static bool dn_cb_host_equal(dn_r_Host const * v_a,dn_r_Host const * v_b);
static size_t dn_cb_middle_label(dn_r_Host const * v_h);
extern int32_t dn_ext_CloseHandle(void * v_h) __asm__("CloseHandle");
extern uint32_t dn_ext_GetLastError(void) __asm__("GetLastError");
extern void dn_ext_SetLastError(uint32_t v_error) __asm__("SetLastError");
extern uint64_t dn_ext_GetTickCount64(void) __asm__("GetTickCount64");
extern uint32_t dn_ext_GetCurrentProcessId(void) __asm__("GetCurrentProcessId");
extern uint32_t dn_ext_GetModuleFileNameW(void * v_module_,uint16_t * v_path,uint32_t v_count) __asm__("GetModuleFileNameW");
extern uint16_t * dn_ext_GetCommandLineW(void) __asm__("GetCommandLineW");
extern uint16_t * * dn_ext_CommandLineToArgvW(uint16_t * v_command,int32_t * v_count) __asm__("CommandLineToArgvW");
extern void * dn_ext_LocalFree(void * v_memory) __asm__("LocalFree");
extern int32_t dn_ext_MultiByteToWideChar(uint32_t v_cp,uint32_t v_flags,uint8_t * v_source,int32_t v_count,uint16_t * v_target,int32_t v_capacity) __asm__("MultiByteToWideChar");
extern int32_t dn_ext_WideCharToMultiByte(uint32_t v_cp,uint32_t v_flags,uint16_t * v_source,int32_t v_count,uint8_t * v_target,int32_t v_capacity,uint8_t * v_default_,int32_t * v_used) __asm__("WideCharToMultiByte");
extern void * dn_ext_CreateFileW(uint16_t * v_path,uint32_t v_access,uint32_t v_share,uint8_t * v_security,uint32_t v_creation,uint32_t v_attributes,void * v_template_) __asm__("CreateFileW");
extern int32_t dn_ext_GetFileSizeEx(void * v_file,int64_t * v_size) __asm__("GetFileSizeEx");
extern int32_t dn_ext_ReadFile(void * v_file,uint8_t * v_bytes,uint32_t v_capacity,uint32_t * v_read,dn_r_Overlapped * v_overlapped) __asm__("ReadFile");
extern int32_t dn_ext_WriteFile(void * v_file,uint8_t * v_bytes,uint32_t v_count,uint32_t * v_written,dn_r_Overlapped * v_overlapped) __asm__("WriteFile");
extern int32_t dn_ext_FlushFileBuffers(void * v_file) __asm__("FlushFileBuffers");
extern void * dn_ext_GetStdHandle(uint32_t v_which) __asm__("GetStdHandle");
extern int32_t dn_ext_SetCurrentDirectoryW(uint16_t * v_path) __asm__("SetCurrentDirectoryW");
extern int32_t dn_ext_SetConsoleOutputCP(uint32_t v_cp) __asm__("SetConsoleOutputCP");
extern void * dn_ext_CreateEventW(uint8_t * v_security,int32_t v_manual,int32_t v_initial,uint16_t * v_name) __asm__("CreateEventW");
extern void * dn_ext_OpenEventW(uint32_t v_access,int32_t v_inherit,uint16_t * v_name) __asm__("OpenEventW");
extern int32_t dn_ext_SetEvent(void * v_event) __asm__("SetEvent");
extern int32_t dn_ext_ResetEvent(void * v_event) __asm__("ResetEvent");
extern void * dn_ext_CreateMutexW(uint8_t * v_security,int32_t v_owner,uint16_t * v_name) __asm__("CreateMutexW");
extern uint32_t dn_ext_WaitForSingleObject(void * v_object,uint32_t v_timeout) __asm__("WaitForSingleObject");
extern uint32_t dn_ext_WaitForMultipleObjects(uint32_t v_count,void * * v_handles,int32_t v_all_,uint32_t v_timeout) __asm__("WaitForMultipleObjects");
extern int32_t dn_ext_GetOverlappedResult(void * v_file,dn_r_Overlapped * v_overlapped,uint32_t * v_count,int32_t v_wait) __asm__("GetOverlappedResult");
extern int32_t dn_ext_CancelIoEx(void * v_file,dn_r_Overlapped * v_overlapped) __asm__("CancelIoEx");
extern int32_t dn_ext_SetConsoleCtrlHandler(dn_t_fn_U32_ret_I32 v_callback,int32_t v_add) __asm__("SetConsoleCtrlHandler");
extern void dn_ext_ExitProcess(uint32_t v_code) __asm__("ExitProcess");
extern int32_t dn_ext_GetExitCodeProcess(void * v_process,uint32_t * v_code) __asm__("GetExitCodeProcess");
extern void * dn_ext_OpenProcess(uint32_t v_access,int32_t v_inherit,uint32_t v_pid) __asm__("OpenProcess");
extern uint32_t dn_ext_GetProcessId(void * v_process) __asm__("GetProcessId");
extern void * dn_ext_GetModuleHandleW(uint16_t * v_name) __asm__("GetModuleHandleW");
extern int32_t dn_ext_SetProcessDPIAware(void) __asm__("SetProcessDPIAware");
extern int32_t dn_ext_MulDiv(int32_t v_number,int32_t v_numerator,int32_t v_denominator) __asm__("MulDiv");
static bool dn_cb_handle_valid(void * v_h);
static void dn_cb_close_handle(void * * v_value);
static void dn_cb_free_local(void * * v_value);
static dn_t_buf_U16 dn_cb_wide(dn_t_slice0_U8 v_text);
static dn_t_buf_U8 dn_cb_utf8_wide(uint16_t * v_source,size_t v_count);
static size_t dn_cb_wide_length(uint16_t * v_source);
static dn_t_buf_buf_U8 dn_cb_command_arguments(void);
static dn_t_buf_U8 dn_cb_executable_path(void);
static dn_t_buf_U8 dn_cb_directory_path(dn_t_slice0_U8 v_path);
static bool dn_cb_set_application_directory(void);
static dn_t_buf_U8 dn_cb_read_file(dn_t_slice0_U8 v_path,size_t v_maximum);
static bool dn_cb_write_all(void * v_file,dn_t_slice0_U8 v_b);
static bool dn_cb_write_file(dn_t_slice0_U8 v_path,dn_t_slice0_U8 v_b);
static void dn_cb_log_line(void * v_log,dn_t_slice0_U8 v_text);
static void dn_cb_log_error(void * v_log,dn_t_slice0_U8 v_text,uint32_t v_code);
static dn_t_buf_U8 dn_cb_stop_name(uint32_t v_pid);
static bool dn_cb_signal_stop(uint32_t v_pid);
static int32_t dn_cb_console_control(uint32_t v_kind);
static dn_r_Gene dn_cb_gene_default(void);
static bool dn_cb_gene_valid(dn_r_Gene const * v_g);
static uint32_t dn_cb_gene_field(dn_r_Gene const * v_g,size_t v_i);
static void dn_cb_gene_set(dn_r_Gene * v_g,size_t v_i,uint32_t v_n);
static bool dn_cb_gene_parse(dn_t_slice0_U8 v_text,dn_r_Gene * v_g);
static dn_t_buf_U8 dn_cb_gene_text(dn_r_Gene const * v_g);
static uint64_t dn_cb_gene_hash(dn_r_Gene const * v_g);
static uint32_t dn_cb_evolution_random(uint64_t * v_state);
static void dn_cb_gene_normalize(dn_r_Gene * v_g);
static void dn_cb_gene_mutate(dn_r_Gene * v_g,uint64_t * v_state);
static uint32_t dn_cb_min_u32(uint32_t v_a,uint32_t v_b);
static dn_r_Gene dn_cb_gene_cross(dn_r_Gene const * v_a,dn_r_Gene const * v_b,uint64_t * v_state);
static size_t dn_cb_tournament(dn_t_buf_r10_Individual const * v_items,uint64_t * v_state);
static dn_t_buf_r4_Gene dn_cb_evolve(dn_t_buf_r10_Individual const * v_items,uint64_t * v_state,size_t v_count);
static bool dn_cb_profile_gene(dn_t_slice0_U8 v_text,dn_r_Gene * v_gene,uint32_t * v_target);
static bool dn_cb_training_view(dn_t_slice0_U8 v_text,dn_r_TrainingView * v_view);
static dn_t_buf_U8 dn_cb_training_caption(dn_r_TrainingView const * v_v,bool v_history);
static dn_t_buf_U8 dn_cb_training_clock(dn_r_TrainingView const * v_v);
static dn_r_StartupView dn_cb_startup_view(dn_t_slice0_U8 v_text);
static dn_t_buf_U8 dn_cb_startup_caption(uint32_t v_code);
extern int32_t dn_ext_GetOpenFileNameW(dn_r_OpenFileName * v_dialog) __asm__("GetOpenFileNameW");
extern int32_t dn_ext_GetSaveFileNameW(dn_r_OpenFileName * v_dialog) __asm__("GetSaveFileNameW");
extern uint16_t dn_ext_RegisterClassExW(dn_r_WinClass * v_class_) __asm__("RegisterClassExW");
extern void * dn_ext_CreateWindowExW(uint32_t v_exstyle,uint16_t * v_class_,uint16_t * v_title,uint32_t v_style,int32_t v_x,int32_t v_y,int32_t v_width,int32_t v_height,void * v_parent,size_t v_menu,void * v_instance,void * v_parameter) __asm__("CreateWindowExW");
extern int64_t dn_ext_DefWindowProcW(void * v_window,uint32_t v_message,size_t v_wparam,void * v_lparam) __asm__("DefWindowProcW");
extern void * dn_ext_GetWindowLongPtrW(void * v_window,int32_t v_index) __asm__("GetWindowLongPtrW");
extern void * dn_ext_SetWindowLongPtrW(void * v_window,int32_t v_index,void * v_value) __asm__("SetWindowLongPtrW");
extern int64_t dn_ext_SendMessageW(void * v_window,uint32_t v_message,size_t v_wparam,size_t v_lparam) __asm__("SendMessageW");
extern int32_t dn_ext_SetWindowTextW(void * v_window,uint16_t * v_text) __asm__("SetWindowTextW");
extern void * dn_ext_OpenMutexW(uint32_t v_access,int32_t v_inherit,uint16_t * v_name) __asm__("OpenMutexW");
extern int32_t dn_ext_EnableWindow(void * v_window,int32_t v_enabled) __asm__("EnableWindow");
extern int32_t dn_ext_DestroyWindow(void * v_window) __asm__("DestroyWindow");
extern int32_t dn_ext_ShowWindow(void * v_window,int32_t v_command) __asm__("ShowWindow");
extern int32_t dn_ext_UpdateWindow(void * v_window) __asm__("UpdateWindow");
extern int32_t dn_ext_GetMessageW(dn_r_WinMessage * v_message,void * v_window,uint32_t v_minimum,uint32_t v_maximum) __asm__("GetMessageW");
extern int32_t dn_ext_TranslateMessage(dn_r_WinMessage * v_message) __asm__("TranslateMessage");
extern int64_t dn_ext_DispatchMessageW(dn_r_WinMessage * v_message) __asm__("DispatchMessageW");
extern int32_t dn_ext_IsDialogMessageW(void * v_window,dn_r_WinMessage * v_message) __asm__("IsDialogMessageW");
extern void dn_ext_PostQuitMessage(int32_t v_code) __asm__("PostQuitMessage");
extern void * dn_ext_LoadCursorW(void * v_instance,size_t v_id) __asm__("LoadCursorW");
extern int32_t dn_ext_GetClientRect(void * v_window,dn_r_Rect * v_rect) __asm__("GetClientRect");
extern int32_t dn_ext_MoveWindow(void * v_window,int32_t v_x,int32_t v_y,int32_t v_width,int32_t v_height,int32_t v_repaint) __asm__("MoveWindow");
extern size_t dn_ext_SetTimer(void * v_window,size_t v_id,uint32_t v_timeout,void * v_callback) __asm__("SetTimer");
extern int32_t dn_ext_KillTimer(void * v_window,size_t v_id) __asm__("KillTimer");
extern int32_t dn_ext_ShellExecuteExW(dn_r_ShellExecute * v_execute) __asm__("ShellExecuteExW");
extern int32_t dn_ext_TerminateProcess(void * v_process,uint32_t v_code) __asm__("TerminateProcess");
extern int32_t dn_ext_GetSystemMetrics(int32_t v_index) __asm__("GetSystemMetrics");
extern uint32_t dn_ext_GetDpiForWindow(void * v_window) __asm__("GetDpiForWindow");
extern int32_t dn_ext_InvalidateRect(void * v_window,dn_r_Rect * v_rect,int32_t v_erase) __asm__("InvalidateRect");
extern void * dn_ext_CreateFontW(int32_t v_height,int32_t v_width,int32_t v_escapement,int32_t v_orientation,int32_t v_weight,uint32_t v_italic,uint32_t v_underline,uint32_t v_strike,uint32_t v_charset,uint32_t v_output,uint32_t v_clip,uint32_t v_quality,uint32_t v_pitch,uint16_t * v_face) __asm__("CreateFontW");
extern int32_t dn_ext_DeleteObject(void * v_object) __asm__("DeleteObject");
extern void * dn_ext_CreateSolidBrush(uint32_t v_color) __asm__("CreateSolidBrush");
extern void * dn_ext_GetStockObject(int32_t v_id) __asm__("GetStockObject");
extern void * dn_ext_SelectObject(void * v_dc,void * v_object) __asm__("SelectObject");
extern uint32_t dn_ext_SetTextColor(void * v_dc,uint32_t v_color) __asm__("SetTextColor");
extern int32_t dn_ext_SetBkMode(void * v_dc,int32_t v_mode) __asm__("SetBkMode");
extern int32_t dn_ext_RoundRect(void * v_dc,int32_t v_left,int32_t v_top,int32_t v_right,int32_t v_bottom,int32_t v_ellipse_width,int32_t v_ellipse_height) __asm__("RoundRect");
extern int32_t dn_ext_DrawTextW(void * v_dc,uint16_t * v_text,int32_t v_count,dn_r_Rect * v_rect,uint32_t v_format) __asm__("DrawTextW");
extern int32_t dn_ext_FillRect(void * v_dc,dn_r_Rect * v_rect,void * v_brush) __asm__("FillRect");
extern int32_t dn_ext_DrawFocusRect(void * v_dc,dn_r_Rect * v_rect) __asm__("DrawFocusRect");
extern void * dn_ext_BeginPaint(void * v_window,dn_r_PaintStruct * v_paint) __asm__("BeginPaint");
extern int32_t dn_ext_EndPaint(void * v_window,dn_r_PaintStruct * v_paint) __asm__("EndPaint");
extern int32_t dn_ext_DwmSetWindowAttribute(void * v_window,uint32_t v_attribute,uint32_t * v_value,uint32_t v_size) __asm__("DwmSetWindowAttribute");
static void dn_cb_delete_object(void * * v_value);
static void * dn_cb_control(dn_r_Gui const * v_g,size_t v_id);
static uint32_t dn_cb_rgb(uint32_t v_red,uint32_t v_green,uint32_t v_blue);
static void dn_cb_ui_log(dn_r_Gui * v_g,dn_t_slice0_U8 v_text);
static int32_t dn_cb_scale(dn_r_Gui const * v_g,int32_t v_value);
static int32_t dn_cb_width_at(dn_r_Gui const * v_g,int32_t v_value);
static void dn_cb_rounded(void * v_dc,dn_r_Rect v_rect,void * v_brush,int32_t v_radius);
static void dn_cb_paint_text(dn_r_Gui const * v_g,void * v_dc,dn_t_slice0_U8 v_text,void * v_font,uint32_t v_color,int32_t v_x,int32_t v_y,int32_t v_width,int32_t v_height);
static void dn_cb_gui_paint(dn_r_Gui const * v_g);
static void dn_cb_gui_layout(dn_r_Gui const * v_g);
static void dn_cb_add_button(dn_r_Gui * v_g,dn_utf8 v_title,size_t v_id,int32_t v_x,int32_t v_y,int32_t v_width,int32_t v_height);
static uint32_t dn_cb_selected_target(dn_r_Gui const * v_g);
static void dn_cb_gui_build(dn_r_Gui * v_g);
static void dn_cb_gui_draw_button(dn_r_Gui const * v_g,dn_r_DrawItem const * v_item);
static void * dn_cb_launch(dn_r_Gui const * v_g,dn_t_slice0_U8 v_filename,dn_t_slice0_U8 v_parameters,bool v_elevate,int32_t v_show);
static dn_t_buf_U8 dn_cb_local_path(dn_utf8 v_name);
static void dn_cb_running_ui(dn_r_Gui const * v_g);
static void dn_cb_gui_stop(dn_r_Gui * v_g);
static void dn_cb_start_engine(dn_r_Gui * v_g);
static void dn_cb_gui_start(dn_r_Gui * v_g);
static bool dn_cb_ready(uint32_t v_pid);
static void dn_cb_gui_poll(dn_r_Gui * v_g);
static void dn_cb_auxiliary(dn_r_Gui * v_g,dn_utf8 v_filename,dn_t_slice0_U8 v_parameters);
static dn_t_buf_U8 dn_cb_training_stop_path(dn_r_Gui const * v_g);
static void dn_cb_stop_training(dn_r_Gui * v_g,bool v_wait);
static void dn_cb_start_training(dn_r_Gui * v_g);
static void dn_cb_poll_training(dn_r_Gui * v_g);
static void dn_cb_profile_dialog(dn_r_Gui * v_g,bool v_save);
static int64_t dn_cb_window_proc(void * v_window,uint32_t v_message,size_t v_wparam,void * v_lparam);
static void dn_cb_gui_main(void);
static void dn_cb_main(void);
static size_t dn_f_min_size(size_t v_a,size_t v_b) {
{
if(({ size_t dn_tmp_0=v_a;
size_t dn_tmp_1=v_b;
(dn_tmp_0 < dn_tmp_1); })){
{ size_t dn_tmp_2=v_a;return dn_tmp_2; }
}
else{
}
{ size_t dn_tmp_3=v_b;return dn_tmp_3; }
}

dn_fail("missing native return");
}
static size_t dn_cb_min_size(size_t v_a,size_t v_b) {
 if(!dn_enter()){return (size_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (size_t){0};}
 size_t dn_result=dn_f_min_size(v_a,v_b);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static size_t dn_f_max_size(size_t v_a,size_t v_b) {
{
if(({ size_t dn_tmp_0=v_a;
size_t dn_tmp_1=v_b;
(dn_tmp_0 > dn_tmp_1); })){
{ size_t dn_tmp_2=v_a;return dn_tmp_2; }
}
else{
}
{ size_t dn_tmp_3=v_b;return dn_tmp_3; }
}

dn_fail("missing native return");
}
static size_t dn_cb_max_size(size_t v_a,size_t v_b) {
 if(!dn_enter()){return (size_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (size_t){0};}
 size_t dn_result=dn_f_max_size(v_a,v_b);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_ascii_lower(uint8_t v_c) {
{
if((({ uint8_t dn_tmp_0=v_c;
uint8_t dn_tmp_1=({ uint8_t dn_tmp_2=((uint8_t)(65ULL));
if((unsigned __int128)dn_tmp_2>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_2; });
(dn_tmp_0 >= dn_tmp_1); }) && ({ uint8_t dn_tmp_3=v_c;
uint8_t dn_tmp_4=({ uint8_t dn_tmp_5=((uint8_t)(90ULL));
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_5; });
(dn_tmp_3 <= dn_tmp_4); }))){
{ uint8_t dn_tmp_9=({ uint8_t dn_tmp_6=v_c;
uint8_t dn_tmp_7=({ uint8_t dn_tmp_8=((uint8_t)(32ULL));
if((unsigned __int128)dn_tmp_8>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_8; });
dn_U8_add(dn_tmp_6,dn_tmp_7); });return dn_tmp_9; }
}
else{
}
{ uint8_t dn_tmp_10=v_c;return dn_tmp_10; }
}

dn_fail("missing native return");
}
static uint8_t dn_cb_ascii_lower(uint8_t v_c) {
 if(!dn_enter()){return (uint8_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint8_t){0};}
 uint8_t dn_result=dn_f_ascii_lower(v_c);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_space(uint8_t v_c) {
{
{ bool dn_tmp_12=(((({ uint8_t dn_tmp_0=v_c;
uint8_t dn_tmp_1=({ uint8_t dn_tmp_2=((uint8_t)(32ULL));
if((unsigned __int128)dn_tmp_2>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_2; });
(dn_tmp_0 == dn_tmp_1); }) || ({ uint8_t dn_tmp_3=v_c;
uint8_t dn_tmp_4=({ uint8_t dn_tmp_5=((uint8_t)(9ULL));
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_5; });
(dn_tmp_3 == dn_tmp_4); })) || ({ uint8_t dn_tmp_6=v_c;
uint8_t dn_tmp_7=({ uint8_t dn_tmp_8=((uint8_t)(10ULL));
if((unsigned __int128)dn_tmp_8>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_8; });
(dn_tmp_6 == dn_tmp_7); })) || ({ uint8_t dn_tmp_9=v_c;
uint8_t dn_tmp_10=({ uint8_t dn_tmp_11=((uint8_t)(13ULL));
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_11; });
(dn_tmp_9 == dn_tmp_10); }));return dn_tmp_12; }
}

dn_fail("missing native return");
}
static bool dn_cb_space(uint8_t v_c) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_space(v_c);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_bytes_at(dn_t_slice0_U8 v_b,size_t v_at,dn_t_slice0_U8 v_text) {
{
if((({ size_t dn_tmp_0=v_at;
size_t dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_b;
(dn_tmp_2).len; });
(dn_tmp_0 > dn_tmp_1); }) || ({ size_t dn_tmp_3=({ dn_t_slice0_U8 dn_tmp_4=v_text;
(dn_tmp_4).len; });
size_t dn_tmp_5=({ size_t dn_tmp_6=({ dn_t_slice0_U8 dn_tmp_7=v_b;
(dn_tmp_7).len; });
size_t dn_tmp_8=v_at;
dn_USize_sub(dn_tmp_6,dn_tmp_8); });
(dn_tmp_3 > dn_tmp_5); }))){
{ bool dn_tmp_9=0;return dn_tmp_9; }
}
else{
}
{ size_t dn_tmp_12=((size_t)(0ULL));size_t dn_tmp_11=({ dn_t_slice0_U8 dn_tmp_10=v_text;
(dn_tmp_10).len; });for(size_t v_i=dn_tmp_12;v_i<dn_tmp_11;v_i++){
if(({ uint8_t dn_tmp_13=(*({ dn_t_slice0_U8 *dn_tmp_14=&(v_b);size_t dn_tmp_15=({ size_t dn_tmp_16=v_at;
size_t dn_tmp_17=v_i;
dn_USize_add(dn_tmp_16,dn_tmp_17); });&dn_tmp_14->data[dn_index(dn_tmp_15,dn_tmp_14->len)]; }));
uint8_t dn_tmp_18=(*({ dn_t_slice0_U8 *dn_tmp_19=&(v_text);size_t dn_tmp_20=v_i;&dn_tmp_19->data[dn_tmp_20]; }));
(dn_tmp_13 != dn_tmp_18); })){
{ bool dn_tmp_21=0;return dn_tmp_21; }
}
else{
}
}
 }
{ bool dn_tmp_22=1;return dn_tmp_22; }
}

dn_fail("missing native return");
}
static bool dn_cb_bytes_at(dn_t_slice0_U8 v_b,size_t v_at,dn_t_slice0_U8 v_text) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_bytes_at(v_b,v_at,v_text);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_bytes_equal(dn_t_slice0_U8 v_a,dn_t_slice0_U8 v_b) {
{
{ bool dn_tmp_7=(({ size_t dn_tmp_0=({ dn_t_slice0_U8 dn_tmp_1=v_a;
(dn_tmp_1).len; });
size_t dn_tmp_2=({ dn_t_slice0_U8 dn_tmp_3=v_b;
(dn_tmp_3).len; });
(dn_tmp_0 == dn_tmp_2); }) && ({ dn_t_slice0_U8 dn_tmp_4=v_a;
size_t dn_tmp_5=((size_t)(0ULL));
dn_t_slice0_U8 dn_tmp_6=v_b;
dn_f_bytes_at(dn_tmp_4,dn_tmp_5,dn_tmp_6); }));return dn_tmp_7; }
}

dn_fail("missing native return");
}
static bool dn_cb_bytes_equal(dn_t_slice0_U8 v_a,dn_t_slice0_U8 v_b) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_bytes_equal(v_a,v_b);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_ascii_at(dn_t_slice0_U8 v_b,size_t v_at,dn_t_slice0_U8 v_text) {
{
if((({ size_t dn_tmp_0=v_at;
size_t dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_b;
(dn_tmp_2).len; });
(dn_tmp_0 > dn_tmp_1); }) || ({ size_t dn_tmp_3=({ dn_t_slice0_U8 dn_tmp_4=v_text;
(dn_tmp_4).len; });
size_t dn_tmp_5=({ size_t dn_tmp_6=({ dn_t_slice0_U8 dn_tmp_7=v_b;
(dn_tmp_7).len; });
size_t dn_tmp_8=v_at;
dn_USize_sub(dn_tmp_6,dn_tmp_8); });
(dn_tmp_3 > dn_tmp_5); }))){
{ bool dn_tmp_9=0;return dn_tmp_9; }
}
else{
}
{ size_t dn_tmp_12=((size_t)(0ULL));size_t dn_tmp_11=({ dn_t_slice0_U8 dn_tmp_10=v_text;
(dn_tmp_10).len; });for(size_t v_i=dn_tmp_12;v_i<dn_tmp_11;v_i++){
if(({ uint8_t dn_tmp_13=({ uint8_t dn_tmp_14=(*({ dn_t_slice0_U8 *dn_tmp_15=&(v_b);size_t dn_tmp_16=({ size_t dn_tmp_17=v_at;
size_t dn_tmp_18=v_i;
dn_USize_add(dn_tmp_17,dn_tmp_18); });&dn_tmp_15->data[dn_index(dn_tmp_16,dn_tmp_15->len)]; }));
dn_f_ascii_lower(dn_tmp_14); });
uint8_t dn_tmp_19=({ uint8_t dn_tmp_20=(*({ dn_t_slice0_U8 *dn_tmp_21=&(v_text);size_t dn_tmp_22=v_i;&dn_tmp_21->data[dn_tmp_22]; }));
dn_f_ascii_lower(dn_tmp_20); });
(dn_tmp_13 != dn_tmp_19); })){
{ bool dn_tmp_23=0;return dn_tmp_23; }
}
else{
}
}
 }
{ bool dn_tmp_24=1;return dn_tmp_24; }
}

dn_fail("missing native return");
}
static bool dn_cb_ascii_at(dn_t_slice0_U8 v_b,size_t v_at,dn_t_slice0_U8 v_text) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_ascii_at(v_b,v_at,v_text);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_ascii_equal(dn_t_slice0_U8 v_a,dn_t_slice0_U8 v_b) {
{
{ bool dn_tmp_7=(({ size_t dn_tmp_0=({ dn_t_slice0_U8 dn_tmp_1=v_a;
(dn_tmp_1).len; });
size_t dn_tmp_2=({ dn_t_slice0_U8 dn_tmp_3=v_b;
(dn_tmp_3).len; });
(dn_tmp_0 == dn_tmp_2); }) && ({ dn_t_slice0_U8 dn_tmp_4=v_a;
size_t dn_tmp_5=((size_t)(0ULL));
dn_t_slice0_U8 dn_tmp_6=v_b;
dn_f_ascii_at(dn_tmp_4,dn_tmp_5,dn_tmp_6); }));return dn_tmp_7; }
}

dn_fail("missing native return");
}
static bool dn_cb_ascii_equal(dn_t_slice0_U8 v_a,dn_t_slice0_U8 v_b) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_ascii_equal(v_a,v_b);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static size_t dn_f_bytes_find(dn_t_slice0_U8 v_b,size_t v_start,dn_t_slice0_U8 v_text) {
{
size_t v_i=v_start;
while(({ size_t dn_tmp_0=v_i;
size_t dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_b;
(dn_tmp_2).len; });
(dn_tmp_0 < dn_tmp_1); })){
if(({ dn_t_slice0_U8 dn_tmp_3=v_b;
size_t dn_tmp_4=v_i;
dn_t_slice0_U8 dn_tmp_5=v_text;
dn_f_bytes_at(dn_tmp_3,dn_tmp_4,dn_tmp_5); })){
{ size_t dn_tmp_6=v_i;return dn_tmp_6; }
}
else{
}
{ size_t dn_tmp_10=({ size_t dn_tmp_7=v_i;
size_t dn_tmp_8=((size_t)(1ULL));
dn_USize_add(dn_tmp_7,dn_tmp_8); });size_t *dn_tmp_9=&(v_i);*dn_tmp_9=dn_tmp_10; }
}
{ size_t dn_tmp_12=({ dn_t_slice0_U8 dn_tmp_11=v_b;
(dn_tmp_11).len; });return dn_tmp_12; }
}

dn_fail("missing native return");
}
static size_t dn_cb_bytes_find(dn_t_slice0_U8 v_b,size_t v_start,dn_t_slice0_U8 v_text) {
 if(!dn_enter()){return (size_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (size_t){0};}
 size_t dn_result=dn_f_bytes_find(v_b,v_start,v_text);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static size_t dn_f_byte_find(dn_t_slice0_U8 v_b,size_t v_start,uint8_t v_c) {
{
size_t v_i=v_start;
while(({ size_t dn_tmp_0=v_i;
size_t dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_b;
(dn_tmp_2).len; });
(dn_tmp_0 < dn_tmp_1); })){
if(({ uint8_t dn_tmp_3=(*({ dn_t_slice0_U8 *dn_tmp_4=&(v_b);size_t dn_tmp_5=v_i;&dn_tmp_4->data[dn_index(dn_tmp_5,dn_tmp_4->len)]; }));
uint8_t dn_tmp_6=v_c;
(dn_tmp_3 == dn_tmp_6); })){
{ size_t dn_tmp_7=v_i;return dn_tmp_7; }
}
else{
}
{ size_t dn_tmp_11=({ size_t dn_tmp_8=v_i;
size_t dn_tmp_9=((size_t)(1ULL));
dn_USize_add(dn_tmp_8,dn_tmp_9); });size_t *dn_tmp_10=&(v_i);*dn_tmp_10=dn_tmp_11; }
}
{ size_t dn_tmp_13=({ dn_t_slice0_U8 dn_tmp_12=v_b;
(dn_tmp_12).len; });return dn_tmp_13; }
}

dn_fail("missing native return");
}
static size_t dn_cb_byte_find(dn_t_slice0_U8 v_b,size_t v_start,uint8_t v_c) {
 if(!dn_enter()){return (size_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (size_t){0};}
 size_t dn_result=dn_f_byte_find(v_b,v_start,v_c);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_slice0_U8 dn_f_trimmed(dn_t_slice0_U8 v_b) {
{
size_t v_a=((size_t)(0ULL));
size_t v_z=({ dn_t_slice0_U8 dn_tmp_0=v_b;
(dn_tmp_0).len; });
while((({ size_t dn_tmp_1=v_a;
size_t dn_tmp_2=v_z;
(dn_tmp_1 < dn_tmp_2); }) && ({ uint8_t dn_tmp_3=(*({ dn_t_slice0_U8 *dn_tmp_4=&(v_b);size_t dn_tmp_5=v_a;&dn_tmp_4->data[dn_index(dn_tmp_5,dn_tmp_4->len)]; }));
dn_f_space(dn_tmp_3); }))){
{ size_t dn_tmp_9=({ size_t dn_tmp_6=v_a;
size_t dn_tmp_7=((size_t)(1ULL));
dn_USize_add(dn_tmp_6,dn_tmp_7); });size_t *dn_tmp_8=&(v_a);*dn_tmp_8=dn_tmp_9; }
}
while((({ size_t dn_tmp_10=v_z;
size_t dn_tmp_11=v_a;
(dn_tmp_10 > dn_tmp_11); }) && ({ uint8_t dn_tmp_12=(*({ dn_t_slice0_U8 *dn_tmp_13=&(v_b);size_t dn_tmp_14=({ size_t dn_tmp_15=v_z;
size_t dn_tmp_16=((size_t)(1ULL));
dn_USize_sub(dn_tmp_15,dn_tmp_16); });&dn_tmp_13->data[dn_index(dn_tmp_14,dn_tmp_13->len)]; }));
dn_f_space(dn_tmp_12); }))){
{ size_t dn_tmp_20=({ size_t dn_tmp_17=v_z;
size_t dn_tmp_18=((size_t)(1ULL));
dn_USize_sub(dn_tmp_17,dn_tmp_18); });size_t *dn_tmp_19=&(v_z);*dn_tmp_19=dn_tmp_20; }
}
{ dn_t_slice0_U8 dn_tmp_26=({ dn_t_slice0_U8 const * dn_tmp_21=(&(v_b));
size_t dn_tmp_22=v_a;
size_t dn_tmp_23=({ size_t dn_tmp_24=v_z;
size_t dn_tmp_25=v_a;
dn_USize_sub(dn_tmp_24,dn_tmp_25); });
({ dn_range(dn_tmp_22,dn_tmp_23,(*(dn_tmp_21)).len);(dn_t_slice0_U8){(*(dn_tmp_21)).data?(*(dn_tmp_21)).data+dn_tmp_22:NULL,dn_tmp_23}; }); });return dn_tmp_26; }
}

dn_fail("missing native return");
}
static dn_t_slice0_U8 dn_cb_trimmed(dn_t_slice0_U8 v_b) {
 if(!dn_enter()){return (dn_t_slice0_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_slice0_U8){0};}
 dn_t_slice0_U8 dn_result=dn_f_trimmed(v_b);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_buf_U8 dn_f_bytes_copy(dn_t_slice0_U8 v_b) {
{
dn_t_buf_U8 volatile v_result={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_result);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_result=({ size_t dn_tmp_2=({ dn_t_slice0_U8 dn_tmp_3=v_b;
(dn_tmp_3).len; });
dn_new_buf_U8(dn_tmp_2); });
{ size_t dn_tmp_6=((size_t)(0ULL));size_t dn_tmp_5=({ dn_t_slice0_U8 dn_tmp_4=v_b;
(dn_tmp_4).len; });for(size_t v_i=dn_tmp_6;v_i<dn_tmp_5;v_i++){
{ uint8_t dn_tmp_12=(*({ dn_t_slice0_U8 *dn_tmp_9=&(v_b);size_t dn_tmp_10=v_i;&dn_tmp_9->data[dn_tmp_10]; }));uint8_t *dn_tmp_11=&((*({ dn_t_buf_U8 volatile *dn_tmp_7=&(v_result);size_t dn_tmp_8=v_i;&dn_tmp_7->data[dn_index(dn_tmp_8,dn_tmp_7->len)]; })));*dn_tmp_11=dn_tmp_12; }
}
 }
{ dn_t_buf_U8 dn_tmp_14=({ dn_t_buf_U8 dn_tmp_13=v_result;v_result=(dn_t_buf_U8){0};dn_adopt_buf_U8(&dn_tmp_13);dn_tmp_13; });dn_adopt_buf_U8(&dn_tmp_14);dn_drop_buf_U8(&v_result);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_14; }
dn_drop_buf_U8(&v_result);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_t_buf_U8 dn_cb_bytes_copy(dn_t_slice0_U8 v_b) {
 if(!dn_enter()){return (dn_t_buf_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_buf_U8){0};}
 dn_t_buf_U8 dn_result=dn_f_bytes_copy(v_b);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_bytes_append(dn_t_buf_U8 * v_b,dn_t_slice0_U8 v_text) {
{
size_t v_old=({ dn_t_buf_U8 * dn_tmp_0=v_b;
(*(dn_tmp_0)).len; });
(void)(({ dn_t_buf_U8 * dn_tmp_1=v_b;
size_t dn_tmp_2=({ size_t dn_tmp_3=v_old;
size_t dn_tmp_4=({ dn_t_slice0_U8 dn_tmp_5=v_text;
(dn_tmp_5).len; });
dn_USize_add(dn_tmp_3,dn_tmp_4); });
dn_resize_buf_U8(dn_tmp_1,dn_tmp_2); }));
{ size_t dn_tmp_8=((size_t)(0ULL));size_t dn_tmp_7=({ dn_t_slice0_U8 dn_tmp_6=v_text;
(dn_tmp_6).len; });for(size_t v_i=dn_tmp_8;v_i<dn_tmp_7;v_i++){
{ uint8_t dn_tmp_16=(*({ dn_t_slice0_U8 *dn_tmp_13=&(v_text);size_t dn_tmp_14=v_i;&dn_tmp_13->data[dn_tmp_14]; }));uint8_t *dn_tmp_15=&((*({ dn_t_buf_U8 * dn_tmp_9=v_b;size_t dn_tmp_10=({ size_t dn_tmp_11=v_old;
size_t dn_tmp_12=v_i;
dn_USize_add(dn_tmp_11,dn_tmp_12); });&dn_tmp_9->data[dn_index(dn_tmp_10,dn_tmp_9->len)]; })));*dn_tmp_15=dn_tmp_16; }
}
 }
}

return 0;
}
static void dn_cb_bytes_append(dn_t_buf_U8 * v_b,dn_t_slice0_U8 v_text) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_bytes_append(v_b,v_text);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static bool dn_f_decimal(dn_t_slice0_U8 v_b,uint32_t v_limit,uint32_t * v_value) {
{
if(({ size_t dn_tmp_0=({ dn_t_slice0_U8 dn_tmp_1=v_b;
(dn_tmp_1).len; });
size_t dn_tmp_2=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_2); })){
{ bool dn_tmp_3=0;return dn_tmp_3; }
}
else{
}
uint32_t v_n=((uint32_t)(0ULL));
{ size_t dn_tmp_6=((size_t)(0ULL));size_t dn_tmp_5=({ dn_t_slice0_U8 dn_tmp_4=v_b;
(dn_tmp_4).len; });for(size_t v_i=dn_tmp_6;v_i<dn_tmp_5;v_i++){
uint8_t v_c=(*({ dn_t_slice0_U8 *dn_tmp_7=&(v_b);size_t dn_tmp_8=v_i;&dn_tmp_7->data[dn_tmp_8]; }));
if((({ uint8_t dn_tmp_9=v_c;
uint8_t dn_tmp_10=({ uint8_t dn_tmp_11=((uint8_t)(48ULL));
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_11; });
(dn_tmp_9 < dn_tmp_10); }) || ({ uint8_t dn_tmp_12=v_c;
uint8_t dn_tmp_13=({ uint8_t dn_tmp_14=((uint8_t)(57ULL));
if((unsigned __int128)dn_tmp_14>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_14; });
(dn_tmp_12 > dn_tmp_13); }))){
{ bool dn_tmp_15=0;return dn_tmp_15; }
}
else{
}
uint32_t v_d=({ uint32_t dn_tmp_16=({ uint8_t dn_tmp_17=v_c;
if((unsigned __int128)dn_tmp_17>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_17; });
uint32_t dn_tmp_18=({ uint32_t dn_tmp_19=((uint32_t)(48ULL));
if((unsigned __int128)dn_tmp_19>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_19; });
dn_U32_sub(dn_tmp_16,dn_tmp_18); });
if((({ uint32_t dn_tmp_20=v_n;
uint32_t dn_tmp_21=({ uint32_t dn_tmp_22=v_limit;
uint32_t dn_tmp_23=({ uint32_t dn_tmp_24=((uint32_t)(10ULL));
if((unsigned __int128)dn_tmp_24>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_24; });
dn_U32_div(dn_tmp_22,dn_tmp_23); });
(dn_tmp_20 > dn_tmp_21); }) || (({ uint32_t dn_tmp_25=v_n;
uint32_t dn_tmp_26=({ uint32_t dn_tmp_27=v_limit;
uint32_t dn_tmp_28=({ uint32_t dn_tmp_29=((uint32_t)(10ULL));
if((unsigned __int128)dn_tmp_29>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_29; });
dn_U32_div(dn_tmp_27,dn_tmp_28); });
(dn_tmp_25 == dn_tmp_26); }) && ({ uint32_t dn_tmp_30=v_d;
uint32_t dn_tmp_31=({ uint32_t dn_tmp_32=v_limit;
uint32_t dn_tmp_33=({ uint32_t dn_tmp_34=((uint32_t)(10ULL));
if((unsigned __int128)dn_tmp_34>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_34; });
dn_U32_mod(dn_tmp_32,dn_tmp_33); });
(dn_tmp_30 > dn_tmp_31); })))){
{ bool dn_tmp_35=0;return dn_tmp_35; }
}
else{
}
{ uint32_t dn_tmp_42=({ uint32_t dn_tmp_36=({ uint32_t dn_tmp_37=v_n;
uint32_t dn_tmp_38=({ uint32_t dn_tmp_39=((uint32_t)(10ULL));
if((unsigned __int128)dn_tmp_39>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_39; });
dn_U32_mul(dn_tmp_37,dn_tmp_38); });
uint32_t dn_tmp_40=v_d;
dn_U32_add(dn_tmp_36,dn_tmp_40); });uint32_t *dn_tmp_41=&(v_n);*dn_tmp_41=dn_tmp_42; }
}
 }
{ uint32_t dn_tmp_44=v_n;uint32_t *dn_tmp_43=&((*((uint32_t *)dn_nonnull((void*)(v_value)))));*dn_tmp_43=dn_tmp_44; }
{ bool dn_tmp_45=1;return dn_tmp_45; }
}

dn_fail("missing native return");
}
static bool dn_cb_decimal(dn_t_slice0_U8 v_b,uint32_t v_limit,uint32_t * v_value) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_decimal(v_b,v_limit,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_append_number(dn_t_buf_U8 * v_b,uint64_t v_number) {
{
dn_t_arr20_U8 v_digits=({ ((dn_t_arr20_U8){0}); });
uint64_t v_n=v_number;
size_t v_count=((size_t)(0ULL));
while(1){
{ uint8_t dn_tmp_10=({ uint8_t dn_tmp_2=({ uint64_t dn_tmp_3=({ uint64_t dn_tmp_4=v_n;
uint64_t dn_tmp_5=({ uint64_t dn_tmp_6=((uint64_t)(10ULL));
if((unsigned __int128)dn_tmp_6>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_6; });
dn_U64_mod(dn_tmp_4,dn_tmp_5); });
if((unsigned __int128)dn_tmp_3>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_3; });
uint8_t dn_tmp_7=({ uint8_t dn_tmp_8=((uint8_t)(48ULL));
if((unsigned __int128)dn_tmp_8>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_8; });
dn_U8_add(dn_tmp_2,dn_tmp_7); });uint8_t *dn_tmp_9=&((*({ dn_t_arr20_U8 *dn_tmp_0=&(v_digits);size_t dn_tmp_1=v_count;&dn_tmp_0->data[dn_index(dn_tmp_1,20)]; })));*dn_tmp_9=dn_tmp_10; }
{ size_t dn_tmp_14=({ size_t dn_tmp_11=v_count;
size_t dn_tmp_12=((size_t)(1ULL));
dn_USize_add(dn_tmp_11,dn_tmp_12); });size_t *dn_tmp_13=&(v_count);*dn_tmp_13=dn_tmp_14; }
{ uint64_t dn_tmp_19=({ uint64_t dn_tmp_15=v_n;
uint64_t dn_tmp_16=({ uint64_t dn_tmp_17=((uint64_t)(10ULL));
if((unsigned __int128)dn_tmp_17>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_17; });
dn_U64_div(dn_tmp_15,dn_tmp_16); });uint64_t *dn_tmp_18=&(v_n);*dn_tmp_18=dn_tmp_19; }
if(({ uint64_t dn_tmp_20=v_n;
uint64_t dn_tmp_21=({ uint64_t dn_tmp_22=((uint64_t)(0ULL));
if((unsigned __int128)dn_tmp_22>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_22; });
(dn_tmp_20 == dn_tmp_21); })){
break;
}
else{
}
}
while(({ size_t dn_tmp_23=v_count;
size_t dn_tmp_24=((size_t)(0ULL));
(dn_tmp_23 > dn_tmp_24); })){
{ size_t dn_tmp_28=({ size_t dn_tmp_25=v_count;
size_t dn_tmp_26=((size_t)(1ULL));
dn_USize_sub(dn_tmp_25,dn_tmp_26); });size_t *dn_tmp_27=&(v_count);*dn_tmp_27=dn_tmp_28; }
(void)(({ dn_t_buf_U8 * dn_tmp_29=v_b;
uint8_t dn_tmp_30=(*({ dn_t_arr20_U8 *dn_tmp_31=&(v_digits);size_t dn_tmp_32=v_count;&dn_tmp_31->data[dn_index(dn_tmp_32,20)]; }));
dn_push_buf_U8(dn_tmp_29,dn_tmp_30); }));
}
}

return 0;
}
static void dn_cb_append_number(dn_t_buf_U8 * v_b,uint64_t v_number) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_append_number(v_b,v_number);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static dn_t_buf_U8 dn_f_zbytes(dn_t_slice0_U8 v_text) {
{
dn_t_buf_U8 volatile v_b={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_b=({ dn_t_slice0_U8 dn_tmp_2=v_text;
dn_f_bytes_copy(dn_tmp_2); });
(void)(({ dn_t_buf_U8 * dn_tmp_3=(&(v_b));
uint8_t dn_tmp_4=({ uint8_t dn_tmp_5=((uint8_t)(0ULL));
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_5; });
dn_push_buf_U8(dn_tmp_3,dn_tmp_4); }));
{ dn_t_buf_U8 dn_tmp_7=({ dn_t_buf_U8 dn_tmp_6=v_b;v_b=(dn_t_buf_U8){0};dn_adopt_buf_U8(&dn_tmp_6);dn_tmp_6; });dn_adopt_buf_U8(&dn_tmp_7);dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_7; }
dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_t_buf_U8 dn_cb_zbytes(dn_t_slice0_U8 v_text) {
 if(!dn_enter()){return (dn_t_buf_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_buf_U8){0};}
 dn_t_buf_U8 dn_result=dn_f_zbytes(v_text);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint16_t dn_f_be16(dn_t_slice0_U8 v_b,size_t v_p) {
{
{ uint16_t dn_tmp_2=({ dn_t_slice0_U8 const * dn_tmp_0=(&(v_b));
size_t dn_tmp_1=v_p;
dn_U16_bits(dn_binary_read((*(dn_tmp_0)).data,(*(dn_tmp_0)).len,dn_tmp_1,2,1)); });return dn_tmp_2; }
}

dn_fail("missing native return");
}
static uint16_t dn_cb_be16(dn_t_slice0_U8 v_b,size_t v_p) {
 if(!dn_enter()){return (uint16_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint16_t){0};}
 uint16_t dn_result=dn_f_be16(v_b,v_p);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_be32(dn_t_slice0_U8 v_b,size_t v_p) {
{
{ uint32_t dn_tmp_2=({ dn_t_slice0_U8 const * dn_tmp_0=(&(v_b));
size_t dn_tmp_1=v_p;
dn_U32_bits(dn_binary_read((*(dn_tmp_0)).data,(*(dn_tmp_0)).len,dn_tmp_1,4,1)); });return dn_tmp_2; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_be32(dn_t_slice0_U8 v_b,size_t v_p) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_be32(v_b,v_p);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_Host dn_f_domain(dn_t_slice0_U8 v_b,bool v_wildcard) {
{
dn_r_Host v_result=({ ((dn_r_Host){0}); });
size_t v_a=((size_t)(0ULL));
size_t v_z=({ dn_t_slice0_U8 dn_tmp_0=v_b;
(dn_tmp_0).len; });
if((v_wildcard && ({ dn_t_slice0_U8 dn_tmp_1=v_b;
size_t dn_tmp_2=((size_t)(0ULL));
dn_t_slice0_U8 dn_tmp_3=({ dn_utf8 dn_tmp_4=((dn_utf8){(const uint8_t*)"*.", 2});
((dn_t_slice0_U8){dn_tmp_4.data,dn_tmp_4.len}); });
dn_f_bytes_at(dn_tmp_1,dn_tmp_2,dn_tmp_3); }))){
{ size_t dn_tmp_6=((size_t)(2ULL));size_t *dn_tmp_5=&(v_a);*dn_tmp_5=dn_tmp_6; }
}
else{
}
if(((v_wildcard && ({ size_t dn_tmp_7=v_a;
size_t dn_tmp_8=v_z;
(dn_tmp_7 < dn_tmp_8); })) && ({ uint8_t dn_tmp_9=(*({ dn_t_slice0_U8 *dn_tmp_10=&(v_b);size_t dn_tmp_11=v_a;&dn_tmp_10->data[dn_index(dn_tmp_11,dn_tmp_10->len)]; }));
uint8_t dn_tmp_12=({ uint8_t dn_tmp_13=((uint8_t)(46ULL));
if((unsigned __int128)dn_tmp_13>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_13; });
(dn_tmp_9 == dn_tmp_12); }))){
{ size_t dn_tmp_17=({ size_t dn_tmp_14=v_a;
size_t dn_tmp_15=((size_t)(1ULL));
dn_USize_add(dn_tmp_14,dn_tmp_15); });size_t *dn_tmp_16=&(v_a);*dn_tmp_16=dn_tmp_17; }
}
else{
}
if((({ size_t dn_tmp_18=v_a;
size_t dn_tmp_19=v_z;
(dn_tmp_18 < dn_tmp_19); }) && ({ uint8_t dn_tmp_20=(*({ dn_t_slice0_U8 *dn_tmp_21=&(v_b);size_t dn_tmp_22=({ size_t dn_tmp_23=v_z;
size_t dn_tmp_24=((size_t)(1ULL));
dn_USize_sub(dn_tmp_23,dn_tmp_24); });&dn_tmp_21->data[dn_index(dn_tmp_22,dn_tmp_21->len)]; }));
uint8_t dn_tmp_25=({ uint8_t dn_tmp_26=((uint8_t)(46ULL));
if((unsigned __int128)dn_tmp_26>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_26; });
(dn_tmp_20 == dn_tmp_25); }))){
{ size_t dn_tmp_30=({ size_t dn_tmp_27=v_z;
size_t dn_tmp_28=((size_t)(1ULL));
dn_USize_sub(dn_tmp_27,dn_tmp_28); });size_t *dn_tmp_29=&(v_z);*dn_tmp_29=dn_tmp_30; }
}
else{
}
if((({ size_t dn_tmp_31=v_z;
size_t dn_tmp_32=v_a;
(dn_tmp_31 <= dn_tmp_32); }) || ({ size_t dn_tmp_33=({ size_t dn_tmp_34=v_z;
size_t dn_tmp_35=v_a;
dn_USize_sub(dn_tmp_34,dn_tmp_35); });
size_t dn_tmp_36=((size_t)(253ULL));
(dn_tmp_33 > dn_tmp_36); }))){
{ dn_r_Host dn_tmp_37=v_result;return dn_tmp_37; }
}
else{
}
size_t v_label=((size_t)(0ULL));
uint8_t v_previous=((uint8_t)(0ULL));
{ size_t dn_tmp_39=v_a;size_t dn_tmp_38=v_z;for(size_t v_i=dn_tmp_39;v_i<dn_tmp_38;v_i++){
uint8_t v_c=({ uint8_t dn_tmp_40=(*({ dn_t_slice0_U8 *dn_tmp_41=&(v_b);size_t dn_tmp_42=v_i;&dn_tmp_41->data[dn_index(dn_tmp_42,dn_tmp_41->len)]; }));
dn_f_ascii_lower(dn_tmp_40); });
if(({ uint8_t dn_tmp_43=v_c;
uint8_t dn_tmp_44=({ uint8_t dn_tmp_45=((uint8_t)(46ULL));
if((unsigned __int128)dn_tmp_45>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_45; });
(dn_tmp_43 == dn_tmp_44); })){
if((({ size_t dn_tmp_46=v_label;
size_t dn_tmp_47=((size_t)(0ULL));
(dn_tmp_46 == dn_tmp_47); }) || ({ uint8_t dn_tmp_48=v_previous;
uint8_t dn_tmp_49=({ uint8_t dn_tmp_50=((uint8_t)(45ULL));
if((unsigned __int128)dn_tmp_50>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_50; });
(dn_tmp_48 == dn_tmp_49); }))){
{ dn_r_Host dn_tmp_51=({ ((dn_r_Host){0}); });return dn_tmp_51; }
}
else{
}
{ size_t dn_tmp_53=((size_t)(0ULL));size_t *dn_tmp_52=&(v_label);*dn_tmp_52=dn_tmp_53; }
}
else{
if((!((((({ uint8_t dn_tmp_54=v_c;
uint8_t dn_tmp_55=({ uint8_t dn_tmp_56=((uint8_t)(97ULL));
if((unsigned __int128)dn_tmp_56>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_56; });
(dn_tmp_54 >= dn_tmp_55); }) && ({ uint8_t dn_tmp_57=v_c;
uint8_t dn_tmp_58=({ uint8_t dn_tmp_59=((uint8_t)(122ULL));
if((unsigned __int128)dn_tmp_59>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_59; });
(dn_tmp_57 <= dn_tmp_58); })) || (({ uint8_t dn_tmp_60=v_c;
uint8_t dn_tmp_61=({ uint8_t dn_tmp_62=((uint8_t)(48ULL));
if((unsigned __int128)dn_tmp_62>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_62; });
(dn_tmp_60 >= dn_tmp_61); }) && ({ uint8_t dn_tmp_63=v_c;
uint8_t dn_tmp_64=({ uint8_t dn_tmp_65=((uint8_t)(57ULL));
if((unsigned __int128)dn_tmp_65>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_65; });
(dn_tmp_63 <= dn_tmp_64); }))) || ({ uint8_t dn_tmp_66=v_c;
uint8_t dn_tmp_67=({ uint8_t dn_tmp_68=((uint8_t)(45ULL));
if((unsigned __int128)dn_tmp_68>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_68; });
(dn_tmp_66 == dn_tmp_67); }))))){
{ dn_r_Host dn_tmp_69=({ ((dn_r_Host){0}); });return dn_tmp_69; }
}
else{
}
if((({ size_t dn_tmp_70=v_label;
size_t dn_tmp_71=((size_t)(0ULL));
(dn_tmp_70 == dn_tmp_71); }) && ({ uint8_t dn_tmp_72=v_c;
uint8_t dn_tmp_73=({ uint8_t dn_tmp_74=((uint8_t)(45ULL));
if((unsigned __int128)dn_tmp_74>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_74; });
(dn_tmp_72 == dn_tmp_73); }))){
{ dn_r_Host dn_tmp_75=({ ((dn_r_Host){0}); });return dn_tmp_75; }
}
else{
}
{ size_t dn_tmp_79=({ size_t dn_tmp_76=v_label;
size_t dn_tmp_77=((size_t)(1ULL));
dn_USize_add(dn_tmp_76,dn_tmp_77); });size_t *dn_tmp_78=&(v_label);*dn_tmp_78=dn_tmp_79; }
if(({ size_t dn_tmp_80=v_label;
size_t dn_tmp_81=((size_t)(63ULL));
(dn_tmp_80 > dn_tmp_81); })){
{ dn_r_Host dn_tmp_82=({ ((dn_r_Host){0}); });return dn_tmp_82; }
}
else{
}
}
{ uint8_t dn_tmp_88=v_c;uint8_t *dn_tmp_87=&((*({ dn_t_arr254_U8 *dn_tmp_83=&((v_result).f_bytes);size_t dn_tmp_84=({ size_t dn_tmp_85=v_i;
size_t dn_tmp_86=v_a;
dn_USize_sub(dn_tmp_85,dn_tmp_86); });&dn_tmp_83->data[dn_index(dn_tmp_84,254)]; })));*dn_tmp_87=dn_tmp_88; }
{ uint8_t dn_tmp_90=v_c;uint8_t *dn_tmp_89=&(v_previous);*dn_tmp_89=dn_tmp_90; }
}
 }
if((({ size_t dn_tmp_91=v_label;
size_t dn_tmp_92=((size_t)(0ULL));
(dn_tmp_91 == dn_tmp_92); }) || ({ uint8_t dn_tmp_93=v_previous;
uint8_t dn_tmp_94=({ uint8_t dn_tmp_95=((uint8_t)(45ULL));
if((unsigned __int128)dn_tmp_95>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_95; });
(dn_tmp_93 == dn_tmp_94); }))){
{ dn_r_Host dn_tmp_96=({ ((dn_r_Host){0}); });return dn_tmp_96; }
}
else{
}
{ size_t dn_tmp_100=({ size_t dn_tmp_97=v_z;
size_t dn_tmp_98=v_a;
dn_USize_sub(dn_tmp_97,dn_tmp_98); });size_t *dn_tmp_99=&((v_result).f_length);*dn_tmp_99=dn_tmp_100; }
{ dn_r_Host dn_tmp_101=v_result;return dn_tmp_101; }
}

dn_fail("missing native return");
}
static dn_r_Host dn_cb_domain(dn_t_slice0_U8 v_b,bool v_wildcard) {
 if(!dn_enter()){return (dn_r_Host){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_Host){0};}
 dn_r_Host dn_result=dn_f_domain(v_b,v_wildcard);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint64_t dn_f_host_hash(dn_r_Host const * v_h) {
{
uint64_t v_value=((uint64_t)(14695981039346656037ULL));
{ size_t dn_tmp_1=((size_t)(0ULL));size_t dn_tmp_0=(v_h)->f_length;for(size_t v_i=dn_tmp_1;v_i<dn_tmp_0;v_i++){
{ uint64_t dn_tmp_11=({ uint64_t dn_tmp_2=({ uint64_t dn_tmp_3=v_value;
uint64_t dn_tmp_4=({ uint8_t dn_tmp_5=(*({ dn_t_arr254_U8 *dn_tmp_6=&((v_h)->f_bytes);size_t dn_tmp_7=v_i;&dn_tmp_6->data[dn_index(dn_tmp_7,254)]; }));
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_5; });
dn_U64_bits((uint64_t)dn_tmp_3 ^ (uint64_t)dn_tmp_4); });
uint64_t dn_tmp_8=({ uint64_t dn_tmp_9=((uint64_t)(1099511628211ULL));
if((unsigned __int128)dn_tmp_9>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_9; });
dn_U64_wrap_mul(dn_tmp_2,dn_tmp_8); });uint64_t *dn_tmp_10=&(v_value);*dn_tmp_10=dn_tmp_11; }
}
 }
{ uint64_t dn_tmp_18=({ uint64_t dn_tmp_12=v_value;
uint64_t dn_tmp_13=({ uint64_t dn_tmp_14=v_value;
uint64_t dn_tmp_15=({ uint64_t dn_tmp_16=((uint64_t)(33ULL));
if((unsigned __int128)dn_tmp_16>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_16; });
dn_U64_shr(dn_tmp_14,dn_tmp_15); });
dn_U64_bits((uint64_t)dn_tmp_12 ^ (uint64_t)dn_tmp_13); });uint64_t *dn_tmp_17=&(v_value);*dn_tmp_17=dn_tmp_18; }
{ uint64_t dn_tmp_23=({ uint64_t dn_tmp_19=v_value;
uint64_t dn_tmp_20=({ uint64_t dn_tmp_21=((uint64_t)(0xff51afd7ed558ccdULL));
if((unsigned __int128)dn_tmp_21>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_21; });
dn_U64_wrap_mul(dn_tmp_19,dn_tmp_20); });uint64_t *dn_tmp_22=&(v_value);*dn_tmp_22=dn_tmp_23; }
{ uint64_t dn_tmp_29=({ uint64_t dn_tmp_24=v_value;
uint64_t dn_tmp_25=({ uint64_t dn_tmp_26=v_value;
uint64_t dn_tmp_27=({ uint64_t dn_tmp_28=((uint64_t)(33ULL));
if((unsigned __int128)dn_tmp_28>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_28; });
dn_U64_shr(dn_tmp_26,dn_tmp_27); });
dn_U64_bits((uint64_t)dn_tmp_24 ^ (uint64_t)dn_tmp_25); });return dn_tmp_29; }
}

dn_fail("missing native return");
}
static uint64_t dn_cb_host_hash(dn_r_Host const * v_h) {
 if(!dn_enter()){return (uint64_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint64_t){0};}
 uint64_t dn_result=dn_f_host_hash(v_h);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_host_equal(dn_r_Host const * v_a,dn_r_Host const * v_b) {
{
if(({ size_t dn_tmp_0=(v_a)->f_length;
size_t dn_tmp_1=(v_b)->f_length;
(dn_tmp_0 != dn_tmp_1); })){
{ bool dn_tmp_2=0;return dn_tmp_2; }
}
else{
}
{ size_t dn_tmp_4=((size_t)(0ULL));size_t dn_tmp_3=(v_a)->f_length;for(size_t v_i=dn_tmp_4;v_i<dn_tmp_3;v_i++){
if(({ uint8_t dn_tmp_5=(*({ dn_t_arr254_U8 *dn_tmp_6=&((v_a)->f_bytes);size_t dn_tmp_7=v_i;&dn_tmp_6->data[dn_index(dn_tmp_7,254)]; }));
uint8_t dn_tmp_8=(*({ dn_t_arr254_U8 *dn_tmp_9=&((v_b)->f_bytes);size_t dn_tmp_10=v_i;&dn_tmp_9->data[dn_index(dn_tmp_10,254)]; }));
(dn_tmp_5 != dn_tmp_8); })){
{ bool dn_tmp_11=0;return dn_tmp_11; }
}
else{
}
}
 }
{ bool dn_tmp_12=1;return dn_tmp_12; }
}

dn_fail("missing native return");
}
static bool dn_cb_host_equal(dn_r_Host const * v_a,dn_r_Host const * v_b) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_host_equal(v_a,v_b);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static size_t dn_f_middle_label(dn_r_Host const * v_h) {
{
size_t v_end=(v_h)->f_length;
size_t v_begin=((size_t)(0ULL));
size_t v_last=(v_h)->f_length;
{ size_t dn_tmp_1=((size_t)(0ULL));size_t dn_tmp_0=(v_h)->f_length;for(size_t v_i=dn_tmp_1;v_i<dn_tmp_0;v_i++){
if(({ uint8_t dn_tmp_2=(*({ dn_t_arr254_U8 *dn_tmp_3=&((v_h)->f_bytes);size_t dn_tmp_4=v_i;&dn_tmp_3->data[dn_index(dn_tmp_4,254)]; }));
uint8_t dn_tmp_5=({ uint8_t dn_tmp_6=((uint8_t)(46ULL));
if((unsigned __int128)dn_tmp_6>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_6; });
(dn_tmp_2 == dn_tmp_5); })){
{ size_t dn_tmp_8=v_i;size_t *dn_tmp_7=&(v_last);*dn_tmp_7=dn_tmp_8; }
}
else{
}
}
 }
if((({ size_t dn_tmp_9=v_last;
size_t dn_tmp_10=(v_h)->f_length;
(dn_tmp_9 == dn_tmp_10); }) || ({ size_t dn_tmp_11=v_last;
size_t dn_tmp_12=((size_t)(0ULL));
(dn_tmp_11 == dn_tmp_12); }))){
{ size_t dn_tmp_15=({ size_t dn_tmp_13=(v_h)->f_length;
size_t dn_tmp_14=((size_t)(2ULL));
dn_USize_div(dn_tmp_13,dn_tmp_14); });return dn_tmp_15; }
}
else{
}
{ size_t dn_tmp_17=v_last;size_t *dn_tmp_16=&(v_end);*dn_tmp_16=dn_tmp_17; }
{ size_t dn_tmp_19=((size_t)(0ULL));size_t dn_tmp_18=v_end;for(size_t v_i=dn_tmp_19;v_i<dn_tmp_18;v_i++){
if(({ uint8_t dn_tmp_20=(*({ dn_t_arr254_U8 *dn_tmp_21=&((v_h)->f_bytes);size_t dn_tmp_22=v_i;&dn_tmp_21->data[dn_index(dn_tmp_22,254)]; }));
uint8_t dn_tmp_23=({ uint8_t dn_tmp_24=((uint8_t)(46ULL));
if((unsigned __int128)dn_tmp_24>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_24; });
(dn_tmp_20 == dn_tmp_23); })){
{ size_t dn_tmp_28=({ size_t dn_tmp_25=v_i;
size_t dn_tmp_26=((size_t)(1ULL));
dn_USize_add(dn_tmp_25,dn_tmp_26); });size_t *dn_tmp_27=&(v_begin);*dn_tmp_27=dn_tmp_28; }
}
else{
}
}
 }
if(((({ size_t dn_tmp_29=({ size_t dn_tmp_30=({ size_t dn_tmp_31=(v_h)->f_length;
size_t dn_tmp_32=v_last;
dn_USize_sub(dn_tmp_31,dn_tmp_32); });
size_t dn_tmp_33=((size_t)(1ULL));
dn_USize_sub(dn_tmp_30,dn_tmp_33); });
size_t dn_tmp_34=((size_t)(2ULL));
(dn_tmp_29 == dn_tmp_34); }) && ({ size_t dn_tmp_35=({ size_t dn_tmp_36=v_end;
size_t dn_tmp_37=v_begin;
dn_USize_sub(dn_tmp_36,dn_tmp_37); });
size_t dn_tmp_38=((size_t)(3ULL));
(dn_tmp_35 <= dn_tmp_38); })) && ({ size_t dn_tmp_39=v_begin;
size_t dn_tmp_40=((size_t)(0ULL));
(dn_tmp_39 > dn_tmp_40); }))){
{ size_t dn_tmp_44=({ size_t dn_tmp_41=v_begin;
size_t dn_tmp_42=((size_t)(1ULL));
dn_USize_sub(dn_tmp_41,dn_tmp_42); });size_t *dn_tmp_43=&(v_end);*dn_tmp_43=dn_tmp_44; }
{ size_t dn_tmp_46=((size_t)(0ULL));size_t *dn_tmp_45=&(v_begin);*dn_tmp_45=dn_tmp_46; }
{ size_t dn_tmp_48=((size_t)(0ULL));size_t dn_tmp_47=v_end;for(size_t v_i=dn_tmp_48;v_i<dn_tmp_47;v_i++){
if(({ uint8_t dn_tmp_49=(*({ dn_t_arr254_U8 *dn_tmp_50=&((v_h)->f_bytes);size_t dn_tmp_51=v_i;&dn_tmp_50->data[dn_index(dn_tmp_51,254)]; }));
uint8_t dn_tmp_52=({ uint8_t dn_tmp_53=((uint8_t)(46ULL));
if((unsigned __int128)dn_tmp_53>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_53; });
(dn_tmp_49 == dn_tmp_52); })){
{ size_t dn_tmp_57=({ size_t dn_tmp_54=v_i;
size_t dn_tmp_55=((size_t)(1ULL));
dn_USize_add(dn_tmp_54,dn_tmp_55); });size_t *dn_tmp_56=&(v_begin);*dn_tmp_56=dn_tmp_57; }
}
else{
}
}
 }
}
else{
}
{ size_t dn_tmp_64=({ size_t dn_tmp_58=v_begin;
size_t dn_tmp_59=({ size_t dn_tmp_60=({ size_t dn_tmp_61=v_end;
size_t dn_tmp_62=v_begin;
dn_USize_sub(dn_tmp_61,dn_tmp_62); });
size_t dn_tmp_63=((size_t)(2ULL));
dn_USize_div(dn_tmp_60,dn_tmp_63); });
dn_USize_add(dn_tmp_58,dn_tmp_59); });return dn_tmp_64; }
}

dn_fail("missing native return");
}
static size_t dn_cb_middle_label(dn_r_Host const * v_h) {
 if(!dn_enter()){return (size_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (size_t){0};}
 size_t dn_result=dn_f_middle_label(v_h);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_handle_valid(void * v_h) {
{
{ bool dn_tmp_7=(({ size_t dn_tmp_0=({ void * dn_tmp_1=v_h;
((size_t)(uintptr_t)dn_tmp_1); });
size_t dn_tmp_2=((size_t)(0ULL));
(dn_tmp_0 != dn_tmp_2); }) && ({ size_t dn_tmp_3=({ void * dn_tmp_4=v_h;
((size_t)(uintptr_t)dn_tmp_4); });
size_t dn_tmp_5=({ size_t dn_tmp_6=((size_t)(18446744073709551615ULL));
if((unsigned __int128)dn_tmp_6>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_6; });
(dn_tmp_3 != dn_tmp_5); }));return dn_tmp_7; }
}

dn_fail("missing native return");
}
static bool dn_cb_handle_valid(void * v_h) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_handle_valid(v_h);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_close_handle(void * * v_value) {
{
(void)(({ void * dn_tmp_0=(*((void * *)dn_nonnull((void*)(v_value))));
({ int32_t dn_foreign=dn_ext_CloseHandle(dn_tmp_0);dn_fp_default();dn_foreign; }); }));
}

return 0;
}
static void dn_cb_close_handle(void * * v_value) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_close_handle(v_value);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_free_local(void * * v_value) {
{
(void)(({ void * dn_tmp_0=(*((void * *)dn_nonnull((void*)(v_value))));
({ void * dn_foreign=dn_ext_LocalFree(dn_tmp_0);dn_fp_default();dn_foreign; }); }));
}

return 0;
}
static void dn_cb_free_local(void * * v_value) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_free_local(v_value);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static dn_t_buf_U16 dn_f_wide(dn_t_slice0_U8 v_text) {
{
dn_t_buf_U16 volatile v_output={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U16(&v_output);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_output=({ size_t dn_tmp_2=((size_t)(1ULL));
dn_new_buf_U16(dn_tmp_2); });
if(({ size_t dn_tmp_3=({ dn_t_slice0_U8 dn_tmp_4=v_text;
(dn_tmp_4).len; });
size_t dn_tmp_5=((size_t)(0ULL));
(dn_tmp_3 == dn_tmp_5); })){
{ dn_t_buf_U16 dn_tmp_7=({ dn_t_buf_U16 dn_tmp_6=v_output;v_output=(dn_t_buf_U16){0};dn_adopt_buf_U16(&dn_tmp_6);dn_tmp_6; });dn_adopt_buf_U16(&dn_tmp_7);dn_drop_buf_U16(&v_output);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_7; }
}
else{
}
int32_t v_count=({ uint32_t dn_tmp_8=((uint32_t)(65001ULL));
uint32_t dn_tmp_9=((uint32_t)(8ULL));
uint8_t * dn_tmp_10=({ dn_t_slice0_U8 const * dn_tmp_11=(&(v_text));
((uint8_t *)(*(dn_tmp_11)).data); });
int32_t dn_tmp_12=({ size_t dn_tmp_13=({ dn_t_slice0_U8 dn_tmp_14=v_text;
(dn_tmp_14).len; });
if((unsigned __int128)dn_tmp_13>(((unsigned __int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_13; });
uint16_t * dn_tmp_15=({ ((uint16_t *)NULL); });
int32_t dn_tmp_16=((int32_t)(0ULL));
({ int32_t dn_foreign=dn_ext_MultiByteToWideChar(dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_12,dn_tmp_15,dn_tmp_16);dn_fp_default();dn_foreign; }); });
if(({ int32_t dn_tmp_17=v_count;
int32_t dn_tmp_18=({ int32_t dn_tmp_19=((int32_t)(0ULL));
if((__int128)dn_tmp_19<(-((__int128)1<<31))||(__int128)dn_tmp_19>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_19; });
(dn_tmp_17 <= dn_tmp_18); })){
{ dn_t_buf_U16 dn_tmp_21=({ dn_t_buf_U16 dn_tmp_20=v_output;v_output=(dn_t_buf_U16){0};dn_adopt_buf_U16(&dn_tmp_20);dn_tmp_20; });dn_adopt_buf_U16(&dn_tmp_21);dn_drop_buf_U16(&v_output);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_21; }
}
else{
}
(void)(({ dn_t_buf_U16 * dn_tmp_22=(&(v_output));
size_t dn_tmp_23=({ size_t dn_tmp_24=({ int32_t dn_tmp_25=v_count;
if((__int128)dn_tmp_25<0||(__int128)dn_tmp_25>(((__int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_25; });
size_t dn_tmp_26=((size_t)(1ULL));
dn_USize_add(dn_tmp_24,dn_tmp_26); });
dn_resize_buf_U16(dn_tmp_22,dn_tmp_23); }));
if(({ int32_t dn_tmp_27=({ uint32_t dn_tmp_28=((uint32_t)(65001ULL));
uint32_t dn_tmp_29=((uint32_t)(8ULL));
uint8_t * dn_tmp_30=({ dn_t_slice0_U8 const * dn_tmp_31=(&(v_text));
((uint8_t *)(*(dn_tmp_31)).data); });
int32_t dn_tmp_32=({ size_t dn_tmp_33=({ dn_t_slice0_U8 dn_tmp_34=v_text;
(dn_tmp_34).len; });
if((unsigned __int128)dn_tmp_33>(((unsigned __int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_33; });
uint16_t * dn_tmp_35=({ dn_t_buf_U16 * dn_tmp_36=(&(v_output));
((uint16_t *)(*(dn_tmp_36)).data); });
int32_t dn_tmp_37=v_count;
({ int32_t dn_foreign=dn_ext_MultiByteToWideChar(dn_tmp_28,dn_tmp_29,dn_tmp_30,dn_tmp_32,dn_tmp_35,dn_tmp_37);dn_fp_default();dn_foreign; }); });
int32_t dn_tmp_38=v_count;
(dn_tmp_27 != dn_tmp_38); })){
{ dn_t_buf_U16 dn_tmp_40=({ size_t dn_tmp_39=((size_t)(1ULL));
dn_new_buf_U16(dn_tmp_39); });dn_adopt_buf_U16(&dn_tmp_40);dn_drop_buf_U16(&v_output);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_40; }
}
else{
}
{ dn_t_buf_U16 dn_tmp_42=({ dn_t_buf_U16 dn_tmp_41=v_output;v_output=(dn_t_buf_U16){0};dn_adopt_buf_U16(&dn_tmp_41);dn_tmp_41; });dn_adopt_buf_U16(&dn_tmp_42);dn_drop_buf_U16(&v_output);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_42; }
dn_drop_buf_U16(&v_output);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_t_buf_U16 dn_cb_wide(dn_t_slice0_U8 v_text) {
 if(!dn_enter()){return (dn_t_buf_U16){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_buf_U16){0};}
 dn_t_buf_U16 dn_result=dn_f_wide(v_text);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_buf_U8 dn_f_utf8_wide(uint16_t * v_source,size_t v_count) {
{
dn_t_buf_U8 volatile v_output={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_output);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_output=({ size_t dn_tmp_2=((size_t)(0ULL));
dn_new_buf_U8(dn_tmp_2); });
if((({ size_t dn_tmp_3=v_count;
size_t dn_tmp_4=((size_t)(0ULL));
(dn_tmp_3 == dn_tmp_4); }) || ({ size_t dn_tmp_5=v_count;
size_t dn_tmp_6=((size_t)(32767ULL));
(dn_tmp_5 > dn_tmp_6); }))){
{ dn_t_buf_U8 dn_tmp_8=({ dn_t_buf_U8 dn_tmp_7=v_output;v_output=(dn_t_buf_U8){0};dn_adopt_buf_U8(&dn_tmp_7);dn_tmp_7; });dn_adopt_buf_U8(&dn_tmp_8);dn_drop_buf_U8(&v_output);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_8; }
}
else{
}
int32_t v_bytes=({ uint32_t dn_tmp_9=((uint32_t)(65001ULL));
uint32_t dn_tmp_10=((uint32_t)(128ULL));
uint16_t * dn_tmp_11=v_source;
int32_t dn_tmp_12=({ size_t dn_tmp_13=v_count;
if((unsigned __int128)dn_tmp_13>(((unsigned __int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_13; });
uint8_t * dn_tmp_14=({ ((uint8_t *)NULL); });
int32_t dn_tmp_15=((int32_t)(0ULL));
uint8_t * dn_tmp_16=({ ((uint8_t *)NULL); });
int32_t * dn_tmp_17=({ ((int32_t *)NULL); });
({ int32_t dn_foreign=dn_ext_WideCharToMultiByte(dn_tmp_9,dn_tmp_10,dn_tmp_11,dn_tmp_12,dn_tmp_14,dn_tmp_15,dn_tmp_16,dn_tmp_17);dn_fp_default();dn_foreign; }); });
if(({ int32_t dn_tmp_18=v_bytes;
int32_t dn_tmp_19=({ int32_t dn_tmp_20=((int32_t)(0ULL));
if((__int128)dn_tmp_20<(-((__int128)1<<31))||(__int128)dn_tmp_20>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_20; });
(dn_tmp_18 <= dn_tmp_19); })){
{ dn_t_buf_U8 dn_tmp_22=({ dn_t_buf_U8 dn_tmp_21=v_output;v_output=(dn_t_buf_U8){0};dn_adopt_buf_U8(&dn_tmp_21);dn_tmp_21; });dn_adopt_buf_U8(&dn_tmp_22);dn_drop_buf_U8(&v_output);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_22; }
}
else{
}
(void)(({ dn_t_buf_U8 * dn_tmp_23=(&(v_output));
size_t dn_tmp_24=({ int32_t dn_tmp_25=v_bytes;
if((__int128)dn_tmp_25<0||(__int128)dn_tmp_25>(((__int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_25; });
dn_resize_buf_U8(dn_tmp_23,dn_tmp_24); }));
if(({ int32_t dn_tmp_26=({ uint32_t dn_tmp_27=((uint32_t)(65001ULL));
uint32_t dn_tmp_28=((uint32_t)(128ULL));
uint16_t * dn_tmp_29=v_source;
int32_t dn_tmp_30=({ size_t dn_tmp_31=v_count;
if((unsigned __int128)dn_tmp_31>(((unsigned __int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_31; });
uint8_t * dn_tmp_32=({ dn_t_buf_U8 * dn_tmp_33=(&(v_output));
((uint8_t *)(*(dn_tmp_33)).data); });
int32_t dn_tmp_34=v_bytes;
uint8_t * dn_tmp_35=({ ((uint8_t *)NULL); });
int32_t * dn_tmp_36=({ ((int32_t *)NULL); });
({ int32_t dn_foreign=dn_ext_WideCharToMultiByte(dn_tmp_27,dn_tmp_28,dn_tmp_29,dn_tmp_30,dn_tmp_32,dn_tmp_34,dn_tmp_35,dn_tmp_36);dn_fp_default();dn_foreign; }); });
int32_t dn_tmp_37=v_bytes;
(dn_tmp_26 != dn_tmp_37); })){
{ dn_t_buf_U8 dn_tmp_39=({ size_t dn_tmp_38=((size_t)(0ULL));
dn_new_buf_U8(dn_tmp_38); });dn_adopt_buf_U8(&dn_tmp_39);dn_drop_buf_U8(&v_output);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_39; }
}
else{
}
{ dn_t_buf_U8 dn_tmp_41=({ dn_t_buf_U8 dn_tmp_40=v_output;v_output=(dn_t_buf_U8){0};dn_adopt_buf_U8(&dn_tmp_40);dn_tmp_40; });dn_adopt_buf_U8(&dn_tmp_41);dn_drop_buf_U8(&v_output);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_41; }
dn_drop_buf_U8(&v_output);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_t_buf_U8 dn_cb_utf8_wide(uint16_t * v_source,size_t v_count) {
 if(!dn_enter()){return (dn_t_buf_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_buf_U8){0};}
 dn_t_buf_U8 dn_result=dn_f_utf8_wide(v_source,v_count);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static size_t dn_f_wide_length(uint16_t * v_source) {
{
if(({ size_t dn_tmp_0=({ uint16_t * dn_tmp_1=v_source;
((size_t)(uintptr_t)dn_tmp_1); });
size_t dn_tmp_2=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_2); })){
{ size_t dn_tmp_3=((size_t)(0ULL));return dn_tmp_3; }
}
else{
}
size_t v_count=((size_t)(0ULL));
while(({ size_t dn_tmp_4=v_count;
size_t dn_tmp_5=((size_t)(32767ULL));
(dn_tmp_4 < dn_tmp_5); })){
if(({ uint16_t dn_tmp_6=(*((uint16_t *)dn_nonnull((void*)(({ uint16_t * dn_tmp_7=v_source;
size_t dn_tmp_8=v_count;
((uint16_t *)dn_offset(dn_tmp_7,dn_tmp_8,sizeof(*dn_tmp_7))); })))));
uint16_t dn_tmp_9=({ uint16_t dn_tmp_10=((uint16_t)(0ULL));
if((unsigned __int128)dn_tmp_10>(((unsigned __int128)1<<16)-1))dn_fail("numeric conversion out of range");(uint16_t)dn_tmp_10; });
(dn_tmp_6 == dn_tmp_9); })){
break;
}
else{
}
{ size_t dn_tmp_14=({ size_t dn_tmp_11=v_count;
size_t dn_tmp_12=((size_t)(1ULL));
dn_USize_add(dn_tmp_11,dn_tmp_12); });size_t *dn_tmp_13=&(v_count);*dn_tmp_13=dn_tmp_14; }
}
{ size_t dn_tmp_15=v_count;return dn_tmp_15; }
}

dn_fail("missing native return");
}
static size_t dn_cb_wide_length(uint16_t * v_source) {
 if(!dn_enter()){return (size_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (size_t){0};}
 size_t dn_result=dn_f_wide_length(v_source);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_buf_buf_U8 dn_f_command_arguments(void) {
{
dn_t_resource_h_free_local * volatile v_owner={0};
dn_t_buf_buf_U8 volatile v_args={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_buf_U8(&v_args);
dn_drop_resource_h_free_local(&v_owner);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
int32_t v_count=((int32_t)(0ULL));
uint16_t * * v_raw=({ uint16_t * dn_tmp_2=({ ({ uint16_t * dn_foreign=dn_ext_GetCommandLineW();dn_fp_default();dn_foreign; }); });
int32_t * dn_tmp_3=({ int32_t * dn_tmp_4=({ int32_t * dn_tmp_5=(&(v_count));
((int32_t *)dn_tmp_5); });
((int32_t *)dn_tmp_4); });
({ uint16_t * * dn_foreign=dn_ext_CommandLineToArgvW(dn_tmp_2,dn_tmp_3);dn_fp_default();dn_foreign; }); });
v_owner=({ dn_t_resource_h_free_local * dn_tmp_6=dn_begin_resource_h_free_local();dn_commit_resource_h_free_local(dn_tmp_6,({ uint16_t * * dn_tmp_7=v_raw;
((void*)dn_tmp_7); })); });
v_args=({ size_t dn_tmp_8=((size_t)(0ULL));
dn_new_buf_buf_U8(dn_tmp_8); });
if(({ size_t dn_tmp_9=({ uint16_t * * dn_tmp_10=v_raw;
((size_t)(uintptr_t)dn_tmp_10); });
size_t dn_tmp_11=((size_t)(0ULL));
(dn_tmp_9 == dn_tmp_11); })){
{ dn_t_buf_buf_U8 dn_tmp_13=({ dn_t_buf_buf_U8 dn_tmp_12=v_args;v_args=(dn_t_buf_buf_U8){0};dn_adopt_buf_buf_U8(&dn_tmp_12);dn_tmp_12; });dn_adopt_buf_buf_U8(&dn_tmp_13);dn_drop_buf_buf_U8(&v_args);
dn_drop_resource_h_free_local(&v_owner);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_13; }
}
else{
}
{ size_t dn_tmp_16=((size_t)(0ULL));size_t dn_tmp_15=({ int32_t dn_tmp_14=v_count;
if((__int128)dn_tmp_14<0||(__int128)dn_tmp_14>(((__int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_14; });for(size_t v_i=dn_tmp_16;v_i<dn_tmp_15;v_i++){
uint16_t * v_word=(*((uint16_t * *)dn_nonnull((void*)(({ uint16_t * * dn_tmp_17=v_raw;
size_t dn_tmp_18=v_i;
((uint16_t * *)dn_offset(dn_tmp_17,dn_tmp_18,sizeof(*dn_tmp_17))); })))));
(void)(({ dn_t_buf_buf_U8 * dn_tmp_19=(&(v_args));
dn_t_buf_U8 dn_tmp_20=({ uint16_t * dn_tmp_21=v_word;
size_t dn_tmp_22=({ uint16_t * dn_tmp_23=v_word;
dn_f_wide_length(dn_tmp_23); });
dn_f_utf8_wide(dn_tmp_21,dn_tmp_22); });
dn_adopt_buf_U8(&dn_tmp_20);
dn_push_buf_buf_U8(dn_tmp_19,dn_tmp_20); }));
}
 }
{ dn_t_buf_buf_U8 dn_tmp_25=({ dn_t_buf_buf_U8 dn_tmp_24=v_args;v_args=(dn_t_buf_buf_U8){0};dn_adopt_buf_buf_U8(&dn_tmp_24);dn_tmp_24; });dn_adopt_buf_buf_U8(&dn_tmp_25);dn_drop_buf_buf_U8(&v_args);
dn_drop_resource_h_free_local(&v_owner);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_25; }
dn_drop_buf_buf_U8(&v_args);
dn_drop_resource_h_free_local(&v_owner);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_t_buf_buf_U8 dn_cb_command_arguments(void) {
 if(!dn_enter()){return (dn_t_buf_buf_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_buf_buf_U8){0};}
 dn_t_buf_buf_U8 dn_result=dn_f_command_arguments();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_buf_U8 dn_f_executable_path(void) {
{
dn_t_buf_U16 volatile v_path={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U16(&v_path);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_path=({ size_t dn_tmp_2=((size_t)(32768ULL));
dn_new_buf_U16(dn_tmp_2); });
uint32_t v_count=({ void * dn_tmp_3=({ ((void *){0}); });
uint16_t * dn_tmp_4=({ dn_t_buf_U16 * dn_tmp_5=(&(v_path));
((uint16_t *)(*(dn_tmp_5)).data); });
uint32_t dn_tmp_6=((uint32_t)(32768ULL));
({ uint32_t dn_foreign=dn_ext_GetModuleFileNameW(dn_tmp_3,dn_tmp_4,dn_tmp_6);dn_fp_default();dn_foreign; }); });
if((({ uint32_t dn_tmp_7=v_count;
uint32_t dn_tmp_8=({ uint32_t dn_tmp_9=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_9>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_9; });
(dn_tmp_7 == dn_tmp_8); }) || ({ uint32_t dn_tmp_10=v_count;
uint32_t dn_tmp_11=({ uint32_t dn_tmp_12=((uint32_t)(32768ULL));
if((unsigned __int128)dn_tmp_12>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_12; });
(dn_tmp_10 >= dn_tmp_11); }))){
{ dn_t_buf_U8 dn_tmp_14=({ size_t dn_tmp_13=((size_t)(0ULL));
dn_new_buf_U8(dn_tmp_13); });dn_adopt_buf_U8(&dn_tmp_14);dn_drop_buf_U16(&v_path);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_14; }
}
else{
}
{ dn_t_buf_U8 dn_tmp_19=({ uint16_t * dn_tmp_15=({ dn_t_buf_U16 const * dn_tmp_16=(&(v_path));
((uint16_t *)(*(dn_tmp_16)).data); });
size_t dn_tmp_17=({ uint32_t dn_tmp_18=v_count;
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_18; });
dn_f_utf8_wide(dn_tmp_15,dn_tmp_17); });dn_adopt_buf_U8(&dn_tmp_19);dn_drop_buf_U16(&v_path);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_19; }
dn_drop_buf_U16(&v_path);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_t_buf_U8 dn_cb_executable_path(void) {
 if(!dn_enter()){return (dn_t_buf_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_buf_U8){0};}
 dn_t_buf_U8 dn_result=dn_f_executable_path();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_buf_U8 dn_f_directory_path(dn_t_slice0_U8 v_path) {
{
size_t v_last=((size_t)(0ULL));
{ size_t dn_tmp_2=((size_t)(0ULL));size_t dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_0=v_path;
(dn_tmp_0).len; });for(size_t v_i=dn_tmp_2;v_i<dn_tmp_1;v_i++){
if((({ uint8_t dn_tmp_3=(*({ dn_t_slice0_U8 *dn_tmp_4=&(v_path);size_t dn_tmp_5=v_i;&dn_tmp_4->data[dn_tmp_5]; }));
uint8_t dn_tmp_6=({ uint8_t dn_tmp_7=((uint8_t)(92ULL));
if((unsigned __int128)dn_tmp_7>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_7; });
(dn_tmp_3 == dn_tmp_6); }) || ({ uint8_t dn_tmp_8=(*({ dn_t_slice0_U8 *dn_tmp_9=&(v_path);size_t dn_tmp_10=v_i;&dn_tmp_9->data[dn_tmp_10]; }));
uint8_t dn_tmp_11=({ uint8_t dn_tmp_12=((uint8_t)(47ULL));
if((unsigned __int128)dn_tmp_12>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_12; });
(dn_tmp_8 == dn_tmp_11); }))){
{ size_t dn_tmp_14=v_i;size_t *dn_tmp_13=&(v_last);*dn_tmp_13=dn_tmp_14; }
}
else{
}
}
 }
{ dn_t_buf_U8 dn_tmp_19=({ dn_t_slice0_U8 dn_tmp_15=({ dn_t_slice0_U8 const * dn_tmp_16=(&(v_path));
size_t dn_tmp_17=((size_t)(0ULL));
size_t dn_tmp_18=v_last;
({ dn_range(dn_tmp_17,dn_tmp_18,(*(dn_tmp_16)).len);(dn_t_slice0_U8){(*(dn_tmp_16)).data?(*(dn_tmp_16)).data+dn_tmp_17:NULL,dn_tmp_18}; }); });
dn_f_bytes_copy(dn_tmp_15); });dn_adopt_buf_U8(&dn_tmp_19);return dn_tmp_19; }
}

dn_fail("missing native return");
}
static dn_t_buf_U8 dn_cb_directory_path(dn_t_slice0_U8 v_path) {
 if(!dn_enter()){return (dn_t_buf_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_buf_U8){0};}
 dn_t_buf_U8 dn_result=dn_f_directory_path(v_path);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_set_application_directory(void) {
{
dn_t_buf_U8 volatile v_path={0};
dn_t_buf_U8 volatile v_dir={0};
dn_t_buf_U16 volatile v_w={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U16(&v_w);
dn_drop_buf_U8(&v_dir);
dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_path=({ dn_f_executable_path(); });
v_dir=({ dn_t_slice0_U8 dn_tmp_2=({ dn_t_buf_U8 const * dn_tmp_3=(&(v_path));
size_t dn_tmp_4=((size_t)(0ULL));
size_t dn_tmp_5=({ dn_t_buf_U8 const * dn_tmp_6=(&(v_path));
(*(dn_tmp_6)).len; });
({ dn_range(dn_tmp_4,dn_tmp_5,(*(dn_tmp_3)).len);(dn_t_slice0_U8){(*(dn_tmp_3)).data?(*(dn_tmp_3)).data+dn_tmp_4:NULL,dn_tmp_5}; }); });
dn_f_directory_path(dn_tmp_2); });
v_w=({ dn_t_slice0_U8 dn_tmp_7=({ dn_t_buf_U8 const * dn_tmp_8=(&(v_dir));
size_t dn_tmp_9=((size_t)(0ULL));
size_t dn_tmp_10=({ dn_t_buf_U8 const * dn_tmp_11=(&(v_dir));
(*(dn_tmp_11)).len; });
({ dn_range(dn_tmp_9,dn_tmp_10,(*(dn_tmp_8)).len);(dn_t_slice0_U8){(*(dn_tmp_8)).data?(*(dn_tmp_8)).data+dn_tmp_9:NULL,dn_tmp_10}; }); });
dn_f_wide(dn_tmp_7); });
{ bool dn_tmp_17=({ int32_t dn_tmp_12=({ uint16_t * dn_tmp_13=({ dn_t_buf_U16 const * dn_tmp_14=(&(v_w));
((uint16_t *)(*(dn_tmp_14)).data); });
({ int32_t dn_foreign=dn_ext_SetCurrentDirectoryW(dn_tmp_13);dn_fp_default();dn_foreign; }); });
int32_t dn_tmp_15=({ int32_t dn_tmp_16=((int32_t)(0ULL));
if((__int128)dn_tmp_16<(-((__int128)1<<31))||(__int128)dn_tmp_16>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_16; });
(dn_tmp_12 != dn_tmp_15); });dn_drop_buf_U16(&v_w);
dn_drop_buf_U8(&v_dir);
dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_17; }
dn_drop_buf_U16(&v_w);
dn_drop_buf_U8(&v_dir);
dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static bool dn_cb_set_application_directory(void) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_set_application_directory();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_buf_U8 dn_f_read_file(dn_t_slice0_U8 v_path,size_t v_maximum) {
{
dn_t_buf_U16 volatile v_name={0};
dn_t_resource_h_close_handle * volatile v_owner={0};
dn_t_buf_U8 volatile v_output={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_output);
dn_drop_resource_h_close_handle(&v_owner);
dn_drop_buf_U16(&v_name);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_name=({ dn_t_slice0_U8 dn_tmp_2=v_path;
dn_f_wide(dn_tmp_2); });
v_owner=({ dn_t_resource_h_close_handle * dn_tmp_3=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_3,({ uint16_t * dn_tmp_4=({ dn_t_buf_U16 const * dn_tmp_5=(&(v_name));
((uint16_t *)(*(dn_tmp_5)).data); });
uint32_t dn_tmp_6=((uint32_t)(0x80000000ULL));
uint32_t dn_tmp_7=((uint32_t)(7ULL));
uint8_t * dn_tmp_8=({ ((uint8_t *)NULL); });
uint32_t dn_tmp_9=((uint32_t)(3ULL));
uint32_t dn_tmp_10=((uint32_t)(0x80ULL));
void * dn_tmp_11=({ ((void *){0}); });
({ void * dn_foreign=dn_ext_CreateFileW(dn_tmp_4,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_11);dn_fp_default();dn_foreign; }); })); });
void * v_file=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_12=(&(v_owner));
(&((*dn_tmp_12)->value)); })))));
int64_t v_size=((int64_t)(0ULL));
if(((((!(({ void * dn_tmp_13=v_file;
dn_f_handle_valid(dn_tmp_13); }))) || ({ int32_t dn_tmp_14=({ void * dn_tmp_15=v_file;
int64_t * dn_tmp_16=({ int64_t * dn_tmp_17=({ int64_t * dn_tmp_18=(&(v_size));
((int64_t *)dn_tmp_18); });
((int64_t *)dn_tmp_17); });
({ int32_t dn_foreign=dn_ext_GetFileSizeEx(dn_tmp_15,dn_tmp_16);dn_fp_default();dn_foreign; }); });
int32_t dn_tmp_19=({ int32_t dn_tmp_20=((int32_t)(0ULL));
if((__int128)dn_tmp_20<(-((__int128)1<<31))||(__int128)dn_tmp_20>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_20; });
(dn_tmp_14 == dn_tmp_19); })) || ({ int64_t dn_tmp_21=v_size;
int64_t dn_tmp_22=({ int64_t dn_tmp_23=((int64_t)(0ULL));
if((__int128)dn_tmp_23<(-((__int128)1<<63))||(__int128)dn_tmp_23>(((__int128)1<<63)-1))dn_fail("numeric conversion out of range");(int64_t)dn_tmp_23; });
(dn_tmp_21 < dn_tmp_22); })) || ({ uint64_t dn_tmp_24=({ int64_t dn_tmp_25=v_size;
if((__int128)dn_tmp_25<0||(__int128)dn_tmp_25>(((__int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_25; });
uint64_t dn_tmp_26=({ size_t dn_tmp_27=v_maximum;
if((unsigned __int128)dn_tmp_27>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_27; });
(dn_tmp_24 > dn_tmp_26); }))){
{ dn_t_buf_U8 dn_tmp_29=({ size_t dn_tmp_28=((size_t)(0ULL));
dn_new_buf_U8(dn_tmp_28); });dn_adopt_buf_U8(&dn_tmp_29);dn_drop_buf_U8(&v_output);
dn_drop_resource_h_close_handle(&v_owner);
dn_drop_buf_U16(&v_name);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_29; }
}
else{
}
v_output=({ size_t dn_tmp_30=({ int64_t dn_tmp_31=v_size;
if((__int128)dn_tmp_31<0||(__int128)dn_tmp_31>(((__int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_31; });
dn_new_buf_U8(dn_tmp_30); });
size_t v_position=((size_t)(0ULL));
while(({ size_t dn_tmp_32=v_position;
size_t dn_tmp_33=({ dn_t_buf_U8 const * dn_tmp_34=(&(v_output));
(*(dn_tmp_34)).len; });
(dn_tmp_32 < dn_tmp_33); })){
uint32_t v_actual=((uint32_t)(0ULL));
uint32_t v_remaining=({ size_t dn_tmp_35=({ size_t dn_tmp_36=({ dn_t_buf_U8 const * dn_tmp_37=(&(v_output));
(*(dn_tmp_37)).len; });
size_t dn_tmp_38=v_position;
dn_USize_sub(dn_tmp_36,dn_tmp_38); });
if((unsigned __int128)dn_tmp_35>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_35; });
if(((({ int32_t dn_tmp_39=({ void * dn_tmp_40=v_file;
uint8_t * dn_tmp_41=({ uint8_t * dn_tmp_42=({ dn_t_buf_U8 * dn_tmp_43=(&(v_output));
((uint8_t *)(*(dn_tmp_43)).data); });
size_t dn_tmp_44=v_position;
((uint8_t *)dn_offset(dn_tmp_42,dn_tmp_44,sizeof(*dn_tmp_42))); });
uint32_t dn_tmp_45=v_remaining;
uint32_t * dn_tmp_46=({ uint32_t * dn_tmp_47=({ uint32_t * dn_tmp_48=(&(v_actual));
((uint32_t *)dn_tmp_48); });
((uint32_t *)dn_tmp_47); });
dn_r_Overlapped * dn_tmp_49=({ ((dn_r_Overlapped *)NULL); });
({ int32_t dn_foreign=dn_ext_ReadFile(dn_tmp_40,dn_tmp_41,dn_tmp_45,dn_tmp_46,dn_tmp_49);dn_fp_default();dn_foreign; }); });
int32_t dn_tmp_50=({ int32_t dn_tmp_51=((int32_t)(0ULL));
if((__int128)dn_tmp_51<(-((__int128)1<<31))||(__int128)dn_tmp_51>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_51; });
(dn_tmp_39 == dn_tmp_50); }) || ({ uint32_t dn_tmp_52=v_actual;
uint32_t dn_tmp_53=({ uint32_t dn_tmp_54=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_54>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_54; });
(dn_tmp_52 == dn_tmp_53); })) || ({ uint32_t dn_tmp_55=v_actual;
uint32_t dn_tmp_56=v_remaining;
(dn_tmp_55 > dn_tmp_56); }))){
{ dn_t_buf_U8 dn_tmp_58=({ size_t dn_tmp_57=((size_t)(0ULL));
dn_new_buf_U8(dn_tmp_57); });dn_adopt_buf_U8(&dn_tmp_58);dn_drop_buf_U8(&v_output);
dn_drop_resource_h_close_handle(&v_owner);
dn_drop_buf_U16(&v_name);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_58; }
}
else{
}
{ size_t dn_tmp_63=({ size_t dn_tmp_59=v_position;
size_t dn_tmp_60=({ uint32_t dn_tmp_61=v_actual;
if((unsigned __int128)dn_tmp_61>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_61; });
dn_USize_add(dn_tmp_59,dn_tmp_60); });size_t *dn_tmp_62=&(v_position);*dn_tmp_62=dn_tmp_63; }
}
{ dn_t_buf_U8 dn_tmp_65=({ dn_t_buf_U8 dn_tmp_64=v_output;v_output=(dn_t_buf_U8){0};dn_adopt_buf_U8(&dn_tmp_64);dn_tmp_64; });dn_adopt_buf_U8(&dn_tmp_65);dn_drop_buf_U8(&v_output);
dn_drop_resource_h_close_handle(&v_owner);
dn_drop_buf_U16(&v_name);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_65; }
dn_drop_buf_U8(&v_output);
dn_drop_resource_h_close_handle(&v_owner);
dn_drop_buf_U16(&v_name);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_t_buf_U8 dn_cb_read_file(dn_t_slice0_U8 v_path,size_t v_maximum) {
 if(!dn_enter()){return (dn_t_buf_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_buf_U8){0};}
 dn_t_buf_U8 dn_result=dn_f_read_file(v_path,v_maximum);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_write_all(void * v_file,dn_t_slice0_U8 v_b) {
{
size_t v_at=((size_t)(0ULL));
while(({ size_t dn_tmp_0=v_at;
size_t dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_b;
(dn_tmp_2).len; });
(dn_tmp_0 < dn_tmp_1); })){
uint32_t v_count=((uint32_t)(0ULL));
uint32_t v_remaining=({ size_t dn_tmp_3=({ size_t dn_tmp_4=({ dn_t_slice0_U8 dn_tmp_5=v_b;
(dn_tmp_5).len; });
size_t dn_tmp_6=v_at;
dn_USize_sub(dn_tmp_4,dn_tmp_6); });
if((unsigned __int128)dn_tmp_3>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_3; });
if(((({ int32_t dn_tmp_7=({ void * dn_tmp_8=v_file;
uint8_t * dn_tmp_9=({ uint8_t * dn_tmp_10=({ dn_t_slice0_U8 const * dn_tmp_11=(&(v_b));
((uint8_t *)(*(dn_tmp_11)).data); });
size_t dn_tmp_12=v_at;
((uint8_t *)dn_offset(dn_tmp_10,dn_tmp_12,sizeof(*dn_tmp_10))); });
uint32_t dn_tmp_13=v_remaining;
uint32_t * dn_tmp_14=({ uint32_t * dn_tmp_15=({ uint32_t * dn_tmp_16=(&(v_count));
((uint32_t *)dn_tmp_16); });
((uint32_t *)dn_tmp_15); });
dn_r_Overlapped * dn_tmp_17=({ ((dn_r_Overlapped *)NULL); });
({ int32_t dn_foreign=dn_ext_WriteFile(dn_tmp_8,dn_tmp_9,dn_tmp_13,dn_tmp_14,dn_tmp_17);dn_fp_default();dn_foreign; }); });
int32_t dn_tmp_18=({ int32_t dn_tmp_19=((int32_t)(0ULL));
if((__int128)dn_tmp_19<(-((__int128)1<<31))||(__int128)dn_tmp_19>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_19; });
(dn_tmp_7 == dn_tmp_18); }) || ({ uint32_t dn_tmp_20=v_count;
uint32_t dn_tmp_21=({ uint32_t dn_tmp_22=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_22>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_22; });
(dn_tmp_20 == dn_tmp_21); })) || ({ uint32_t dn_tmp_23=v_count;
uint32_t dn_tmp_24=v_remaining;
(dn_tmp_23 > dn_tmp_24); }))){
{ bool dn_tmp_25=0;return dn_tmp_25; }
}
else{
}
{ size_t dn_tmp_30=({ size_t dn_tmp_26=v_at;
size_t dn_tmp_27=({ uint32_t dn_tmp_28=v_count;
if((unsigned __int128)dn_tmp_28>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_28; });
dn_USize_add(dn_tmp_26,dn_tmp_27); });size_t *dn_tmp_29=&(v_at);*dn_tmp_29=dn_tmp_30; }
}
{ bool dn_tmp_31=1;return dn_tmp_31; }
}

dn_fail("missing native return");
}
static bool dn_cb_write_all(void * v_file,dn_t_slice0_U8 v_b) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_write_all(v_file,v_b);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_write_file(dn_t_slice0_U8 v_path,dn_t_slice0_U8 v_b) {
{
dn_t_buf_U16 volatile v_name={0};
dn_t_resource_h_close_handle * volatile v_owner={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_resource_h_close_handle(&v_owner);
dn_drop_buf_U16(&v_name);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_name=({ dn_t_slice0_U8 dn_tmp_2=v_path;
dn_f_wide(dn_tmp_2); });
v_owner=({ dn_t_resource_h_close_handle * dn_tmp_3=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_3,({ uint16_t * dn_tmp_4=({ dn_t_buf_U16 const * dn_tmp_5=(&(v_name));
((uint16_t *)(*(dn_tmp_5)).data); });
uint32_t dn_tmp_6=((uint32_t)(0x40000000ULL));
uint32_t dn_tmp_7=((uint32_t)(1ULL));
uint8_t * dn_tmp_8=({ ((uint8_t *)NULL); });
uint32_t dn_tmp_9=((uint32_t)(2ULL));
uint32_t dn_tmp_10=((uint32_t)(0x80ULL));
void * dn_tmp_11=({ ((void *){0}); });
({ void * dn_foreign=dn_ext_CreateFileW(dn_tmp_4,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_11);dn_fp_default();dn_foreign; }); })); });
void * v_file=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_12=(&(v_owner));
(&((*dn_tmp_12)->value)); })))));
{ bool dn_tmp_16=(({ void * dn_tmp_13=v_file;
dn_f_handle_valid(dn_tmp_13); }) && ({ void * dn_tmp_14=v_file;
dn_t_slice0_U8 dn_tmp_15=v_b;
dn_f_write_all(dn_tmp_14,dn_tmp_15); }));dn_drop_resource_h_close_handle(&v_owner);
dn_drop_buf_U16(&v_name);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_16; }
dn_drop_resource_h_close_handle(&v_owner);
dn_drop_buf_U16(&v_name);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static bool dn_cb_write_file(dn_t_slice0_U8 v_path,dn_t_slice0_U8 v_b) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_write_file(v_path,v_b);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_log_line(void * v_log,dn_t_slice0_U8 v_text) {
{
dn_t_buf_U8 volatile v_b={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_b=({ dn_t_slice0_U8 dn_tmp_2=v_text;
dn_f_bytes_copy(dn_tmp_2); });
(void)(({ dn_t_buf_U8 * dn_tmp_3=(&(v_b));
dn_t_slice0_U8 dn_tmp_4=({ dn_utf8 dn_tmp_5=((dn_utf8){(const uint8_t*)"\015\012", 2});
((dn_t_slice0_U8){dn_tmp_5.data,dn_tmp_5.len}); });
dn_f_bytes_append(dn_tmp_3,dn_tmp_4); }));
if(({ void * dn_tmp_6=v_log;
dn_f_handle_valid(dn_tmp_6); })){
(void)(({ void * dn_tmp_7=v_log;
dn_t_slice0_U8 dn_tmp_8=({ dn_t_buf_U8 const * dn_tmp_9=(&(v_b));
size_t dn_tmp_10=((size_t)(0ULL));
size_t dn_tmp_11=({ dn_t_buf_U8 const * dn_tmp_12=(&(v_b));
(*(dn_tmp_12)).len; });
({ dn_range(dn_tmp_10,dn_tmp_11,(*(dn_tmp_9)).len);(dn_t_slice0_U8){(*(dn_tmp_9)).data?(*(dn_tmp_9)).data+dn_tmp_10:NULL,dn_tmp_11}; }); });
dn_f_write_all(dn_tmp_7,dn_tmp_8); }));
}
else{
}
void * v_console=({ uint32_t dn_tmp_13=((uint32_t)(0xfffffff5ULL));
({ void * dn_foreign=dn_ext_GetStdHandle(dn_tmp_13);dn_fp_default();dn_foreign; }); });
if(({ void * dn_tmp_14=v_console;
dn_f_handle_valid(dn_tmp_14); })){
(void)(({ void * dn_tmp_15=v_console;
dn_t_slice0_U8 dn_tmp_16=({ dn_t_buf_U8 const * dn_tmp_17=(&(v_b));
size_t dn_tmp_18=((size_t)(0ULL));
size_t dn_tmp_19=({ dn_t_buf_U8 const * dn_tmp_20=(&(v_b));
(*(dn_tmp_20)).len; });
({ dn_range(dn_tmp_18,dn_tmp_19,(*(dn_tmp_17)).len);(dn_t_slice0_U8){(*(dn_tmp_17)).data?(*(dn_tmp_17)).data+dn_tmp_18:NULL,dn_tmp_19}; }); });
dn_f_write_all(dn_tmp_15,dn_tmp_16); }));
}
else{
}
dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_log_line(void * v_log,dn_t_slice0_U8 v_text) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_log_line(v_log,v_text);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_log_error(void * v_log,dn_t_slice0_U8 v_text,uint32_t v_code) {
{
dn_t_buf_U8 volatile v_b={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_b=({ dn_t_slice0_U8 dn_tmp_2=v_text;
dn_f_bytes_copy(dn_tmp_2); });
(void)(({ dn_t_buf_U8 * dn_tmp_3=(&(v_b));
uint64_t dn_tmp_4=({ uint32_t dn_tmp_5=v_code;
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_5; });
dn_f_append_number(dn_tmp_3,dn_tmp_4); }));
(void)(({ void * dn_tmp_6=v_log;
dn_t_slice0_U8 dn_tmp_7=({ dn_t_buf_U8 const * dn_tmp_8=(&(v_b));
size_t dn_tmp_9=((size_t)(0ULL));
size_t dn_tmp_10=({ dn_t_buf_U8 const * dn_tmp_11=(&(v_b));
(*(dn_tmp_11)).len; });
({ dn_range(dn_tmp_9,dn_tmp_10,(*(dn_tmp_8)).len);(dn_t_slice0_U8){(*(dn_tmp_8)).data?(*(dn_tmp_8)).data+dn_tmp_9:NULL,dn_tmp_10}; }); });
dn_f_log_line(dn_tmp_6,dn_tmp_7); }));
dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_log_error(void * v_log,dn_t_slice0_U8 v_text,uint32_t v_code) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_log_error(v_log,v_text,v_code);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static dn_t_buf_U8 dn_f_stop_name(uint32_t v_pid) {
{
dn_t_buf_U8 volatile v_b={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_b=({ dn_t_slice0_U8 dn_tmp_2=({ dn_utf8 dn_tmp_3=((dn_utf8){(const uint8_t*)"Local\134NoDPI.Stop.", 17});
((dn_t_slice0_U8){dn_tmp_3.data,dn_tmp_3.len}); });
dn_f_bytes_copy(dn_tmp_2); });
(void)(({ dn_t_buf_U8 * dn_tmp_4=(&(v_b));
uint64_t dn_tmp_5=({ uint32_t dn_tmp_6=v_pid;
if((unsigned __int128)dn_tmp_6>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_6; });
dn_f_append_number(dn_tmp_4,dn_tmp_5); }));
{ dn_t_buf_U8 dn_tmp_8=({ dn_t_buf_U8 dn_tmp_7=v_b;v_b=(dn_t_buf_U8){0};dn_adopt_buf_U8(&dn_tmp_7);dn_tmp_7; });dn_adopt_buf_U8(&dn_tmp_8);dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_8; }
dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_t_buf_U8 dn_cb_stop_name(uint32_t v_pid) {
 if(!dn_enter()){return (dn_t_buf_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_buf_U8){0};}
 dn_t_buf_U8 dn_result=dn_f_stop_name(v_pid);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_signal_stop(uint32_t v_pid) {
{
dn_t_buf_U8 volatile v_name={0};
dn_t_buf_U16 volatile v_w={0};
dn_t_resource_h_close_handle * volatile v_owner={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_resource_h_close_handle(&v_owner);
dn_drop_buf_U16(&v_w);
dn_drop_buf_U8(&v_name);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_name=({ uint32_t dn_tmp_2=v_pid;
dn_f_stop_name(dn_tmp_2); });
v_w=({ dn_t_slice0_U8 dn_tmp_3=({ dn_t_buf_U8 const * dn_tmp_4=(&(v_name));
size_t dn_tmp_5=((size_t)(0ULL));
size_t dn_tmp_6=({ dn_t_buf_U8 const * dn_tmp_7=(&(v_name));
(*(dn_tmp_7)).len; });
({ dn_range(dn_tmp_5,dn_tmp_6,(*(dn_tmp_4)).len);(dn_t_slice0_U8){(*(dn_tmp_4)).data?(*(dn_tmp_4)).data+dn_tmp_5:NULL,dn_tmp_6}; }); });
dn_f_wide(dn_tmp_3); });
v_owner=({ dn_t_resource_h_close_handle * dn_tmp_8=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_8,({ uint32_t dn_tmp_9=((uint32_t)(2ULL));
int32_t dn_tmp_10=((int32_t)(0ULL));
uint16_t * dn_tmp_11=({ dn_t_buf_U16 const * dn_tmp_12=(&(v_w));
((uint16_t *)(*(dn_tmp_12)).data); });
({ void * dn_foreign=dn_ext_OpenEventW(dn_tmp_9,dn_tmp_10,dn_tmp_11);dn_fp_default();dn_foreign; }); })); });
void * v_event=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_13=(&(v_owner));
(&((*dn_tmp_13)->value)); })))));
{ bool dn_tmp_19=(({ void * dn_tmp_14=v_event;
dn_f_handle_valid(dn_tmp_14); }) && ({ int32_t dn_tmp_15=({ void * dn_tmp_16=v_event;
({ int32_t dn_foreign=dn_ext_SetEvent(dn_tmp_16);dn_fp_default();dn_foreign; }); });
int32_t dn_tmp_17=({ int32_t dn_tmp_18=((int32_t)(0ULL));
if((__int128)dn_tmp_18<(-((__int128)1<<31))||(__int128)dn_tmp_18>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_18; });
(dn_tmp_15 != dn_tmp_17); }));dn_drop_resource_h_close_handle(&v_owner);
dn_drop_buf_U16(&v_w);
dn_drop_buf_U8(&v_name);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_19; }
dn_drop_resource_h_close_handle(&v_owner);
dn_drop_buf_U16(&v_w);
dn_drop_buf_U8(&v_name);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static bool dn_cb_signal_stop(uint32_t v_pid) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_signal_stop(v_pid);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static int32_t dn_f_console_control(uint32_t v_kind) {
{
if(((({ uint32_t dn_tmp_0=v_kind;
uint32_t dn_tmp_1=({ uint32_t dn_tmp_2=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_2>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_2; });
(dn_tmp_0 <= dn_tmp_1); }) || ({ uint32_t dn_tmp_3=v_kind;
uint32_t dn_tmp_4=({ uint32_t dn_tmp_5=((uint32_t)(5ULL));
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_5; });
(dn_tmp_3 == dn_tmp_4); })) || ({ uint32_t dn_tmp_6=v_kind;
uint32_t dn_tmp_7=({ uint32_t dn_tmp_8=((uint32_t)(6ULL));
if((unsigned __int128)dn_tmp_8>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_8; });
(dn_tmp_6 == dn_tmp_7); }))){
(void)(({ uint32_t dn_tmp_9=({ ({ uint32_t dn_foreign=dn_ext_GetCurrentProcessId();dn_fp_default();dn_foreign; }); });
dn_f_signal_stop(dn_tmp_9); }));
{ int32_t dn_tmp_10=((int32_t)(1ULL));return dn_tmp_10; }
}
else{
}
{ int32_t dn_tmp_11=((int32_t)(0ULL));return dn_tmp_11; }
}

dn_fail("missing native return");
}
static int32_t dn_cb_console_control(uint32_t v_kind) {
 if(!dn_enter()){return (int32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (int32_t){0};}
 int32_t dn_result=dn_f_console_control(v_kind);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_Gene dn_f_gene_default(void) {
{
{ dn_r_Gene dn_tmp_8=({ uint32_t dn_tmp_0=((uint32_t)(1ULL));
uint32_t dn_tmp_1=((uint32_t)(0ULL));
uint32_t dn_tmp_2=((uint32_t)(0ULL));
uint32_t dn_tmp_3=((uint32_t)(0ULL));
uint32_t dn_tmp_4=((uint32_t)(0ULL));
uint32_t dn_tmp_5=((uint32_t)(0ULL));
uint32_t dn_tmp_6=((uint32_t)(1048576ULL));
uint32_t dn_tmp_7=((uint32_t)(0ULL));
((dn_r_Gene){dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7}); });return dn_tmp_8; }
}

dn_fail("missing native return");
}
static dn_r_Gene dn_cb_gene_default(void) {
 if(!dn_enter()){return (dn_r_Gene){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_Gene){0};}
 dn_r_Gene dn_result=dn_f_gene_default();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_gene_valid(dn_r_Gene const * v_g) {
{
{ bool dn_tmp_39=(((((((((({ uint32_t dn_tmp_0=(v_g)->f_reverse;
uint32_t dn_tmp_1=({ uint32_t dn_tmp_2=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_2>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_2; });
(dn_tmp_0 <= dn_tmp_1); }) && ({ uint32_t dn_tmp_3=(v_g)->f_cut1;
uint32_t dn_tmp_4=({ uint32_t dn_tmp_5=((uint32_t)(1200ULL));
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_5; });
(dn_tmp_3 <= dn_tmp_4); })) && ({ uint32_t dn_tmp_6=(v_g)->f_cut2;
uint32_t dn_tmp_7=({ uint32_t dn_tmp_8=((uint32_t)(1200ULL));
if((unsigned __int128)dn_tmp_8>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_8; });
(dn_tmp_6 <= dn_tmp_7); })) && ({ uint32_t dn_tmp_9=(v_g)->f_overlap;
uint32_t dn_tmp_10=({ uint32_t dn_tmp_11=((uint32_t)(32ULL));
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_11; });
(dn_tmp_9 <= dn_tmp_10); })) && ({ uint32_t dn_tmp_12=(v_g)->f_guard;
uint32_t dn_tmp_13=({ uint32_t dn_tmp_14=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_14>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_14; });
(dn_tmp_12 <= dn_tmp_13); })) && ({ uint32_t dn_tmp_15=(v_g)->f_repeats;
uint32_t dn_tmp_16=({ uint32_t dn_tmp_17=((uint32_t)(3ULL));
if((unsigned __int128)dn_tmp_17>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_17; });
(dn_tmp_15 <= dn_tmp_16); })) && ({ uint32_t dn_tmp_18=(v_g)->f_gap;
uint32_t dn_tmp_19=({ uint32_t dn_tmp_20=((uint32_t)(65536ULL));
if((unsigned __int128)dn_tmp_20>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_20; });
(dn_tmp_18 >= dn_tmp_19); })) && ({ uint32_t dn_tmp_21=(v_g)->f_gap;
uint32_t dn_tmp_22=({ uint32_t dn_tmp_23=((uint32_t)(16777216ULL));
if((unsigned __int128)dn_tmp_23>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_23; });
(dn_tmp_21 <= dn_tmp_22); })) && ({ uint32_t dn_tmp_24=(v_g)->f_template_;
uint32_t dn_tmp_25=({ uint32_t dn_tmp_26=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_26>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_26; });
(dn_tmp_24 <= dn_tmp_25); })) && ((({ uint32_t dn_tmp_27=(v_g)->f_guard;
uint32_t dn_tmp_28=({ uint32_t dn_tmp_29=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_29>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_29; });
(dn_tmp_27 == dn_tmp_28); }) && ({ uint32_t dn_tmp_30=(v_g)->f_repeats;
uint32_t dn_tmp_31=({ uint32_t dn_tmp_32=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_32>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_32; });
(dn_tmp_30 == dn_tmp_31); })) || (({ uint32_t dn_tmp_33=(v_g)->f_guard;
uint32_t dn_tmp_34=({ uint32_t dn_tmp_35=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_35>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_35; });
(dn_tmp_33 > dn_tmp_34); }) && ({ uint32_t dn_tmp_36=(v_g)->f_repeats;
uint32_t dn_tmp_37=({ uint32_t dn_tmp_38=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_38>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_38; });
(dn_tmp_36 > dn_tmp_37); }))));return dn_tmp_39; }
}

dn_fail("missing native return");
}
static bool dn_cb_gene_valid(dn_r_Gene const * v_g) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_gene_valid(v_g);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_gene_field(dn_r_Gene const * v_g,size_t v_i) {
{
if(({ size_t dn_tmp_0=v_i;
size_t dn_tmp_1=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); })){
{ uint32_t dn_tmp_2=(v_g)->f_reverse;return dn_tmp_2; }
}
else{
}
if(({ size_t dn_tmp_3=v_i;
size_t dn_tmp_4=((size_t)(1ULL));
(dn_tmp_3 == dn_tmp_4); })){
{ uint32_t dn_tmp_5=(v_g)->f_cut1;return dn_tmp_5; }
}
else{
}
if(({ size_t dn_tmp_6=v_i;
size_t dn_tmp_7=((size_t)(2ULL));
(dn_tmp_6 == dn_tmp_7); })){
{ uint32_t dn_tmp_8=(v_g)->f_cut2;return dn_tmp_8; }
}
else{
}
if(({ size_t dn_tmp_9=v_i;
size_t dn_tmp_10=((size_t)(3ULL));
(dn_tmp_9 == dn_tmp_10); })){
{ uint32_t dn_tmp_11=(v_g)->f_overlap;return dn_tmp_11; }
}
else{
}
if(({ size_t dn_tmp_12=v_i;
size_t dn_tmp_13=((size_t)(4ULL));
(dn_tmp_12 == dn_tmp_13); })){
{ uint32_t dn_tmp_14=(v_g)->f_guard;return dn_tmp_14; }
}
else{
}
if(({ size_t dn_tmp_15=v_i;
size_t dn_tmp_16=((size_t)(5ULL));
(dn_tmp_15 == dn_tmp_16); })){
{ uint32_t dn_tmp_17=(v_g)->f_repeats;return dn_tmp_17; }
}
else{
}
if(({ size_t dn_tmp_18=v_i;
size_t dn_tmp_19=((size_t)(6ULL));
(dn_tmp_18 == dn_tmp_19); })){
{ uint32_t dn_tmp_20=(v_g)->f_gap;return dn_tmp_20; }
}
else{
}
{ uint32_t dn_tmp_21=(v_g)->f_template_;return dn_tmp_21; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_gene_field(dn_r_Gene const * v_g,size_t v_i) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_gene_field(v_g,v_i);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_gene_set(dn_r_Gene * v_g,size_t v_i,uint32_t v_n) {
{
if(({ size_t dn_tmp_0=v_i;
size_t dn_tmp_1=((size_t)(0ULL));
(dn_tmp_0 == dn_tmp_1); })){
{ uint32_t dn_tmp_3=v_n;uint32_t *dn_tmp_2=&((v_g)->f_reverse);*dn_tmp_2=dn_tmp_3; }
}
else{
if(({ size_t dn_tmp_4=v_i;
size_t dn_tmp_5=((size_t)(1ULL));
(dn_tmp_4 == dn_tmp_5); })){
{ uint32_t dn_tmp_7=v_n;uint32_t *dn_tmp_6=&((v_g)->f_cut1);*dn_tmp_6=dn_tmp_7; }
}
else{
if(({ size_t dn_tmp_8=v_i;
size_t dn_tmp_9=((size_t)(2ULL));
(dn_tmp_8 == dn_tmp_9); })){
{ uint32_t dn_tmp_11=v_n;uint32_t *dn_tmp_10=&((v_g)->f_cut2);*dn_tmp_10=dn_tmp_11; }
}
else{
if(({ size_t dn_tmp_12=v_i;
size_t dn_tmp_13=((size_t)(3ULL));
(dn_tmp_12 == dn_tmp_13); })){
{ uint32_t dn_tmp_15=v_n;uint32_t *dn_tmp_14=&((v_g)->f_overlap);*dn_tmp_14=dn_tmp_15; }
}
else{
if(({ size_t dn_tmp_16=v_i;
size_t dn_tmp_17=((size_t)(4ULL));
(dn_tmp_16 == dn_tmp_17); })){
{ uint32_t dn_tmp_19=v_n;uint32_t *dn_tmp_18=&((v_g)->f_guard);*dn_tmp_18=dn_tmp_19; }
}
else{
if(({ size_t dn_tmp_20=v_i;
size_t dn_tmp_21=((size_t)(5ULL));
(dn_tmp_20 == dn_tmp_21); })){
{ uint32_t dn_tmp_23=v_n;uint32_t *dn_tmp_22=&((v_g)->f_repeats);*dn_tmp_22=dn_tmp_23; }
}
else{
if(({ size_t dn_tmp_24=v_i;
size_t dn_tmp_25=((size_t)(6ULL));
(dn_tmp_24 == dn_tmp_25); })){
{ uint32_t dn_tmp_27=v_n;uint32_t *dn_tmp_26=&((v_g)->f_gap);*dn_tmp_26=dn_tmp_27; }
}
else{
{ uint32_t dn_tmp_29=v_n;uint32_t *dn_tmp_28=&((v_g)->f_template_);*dn_tmp_28=dn_tmp_29; }
}
}
}
}
}
}
}
}

return 0;
}
static void dn_cb_gene_set(dn_r_Gene * v_g,size_t v_i,uint32_t v_n) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_gene_set(v_g,v_i,v_n);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static bool dn_f_gene_parse(dn_t_slice0_U8 v_text,dn_r_Gene * v_g) {
{
dn_r_Gene v_value=({ ((dn_r_Gene){0}); });
size_t v_at=((size_t)(0ULL));
{ size_t dn_tmp_1=((size_t)(0ULL));size_t dn_tmp_0=((size_t)(8ULL));for(size_t v_i=dn_tmp_1;v_i<dn_tmp_0;v_i++){
size_t v_end=({ dn_t_slice0_U8 dn_tmp_2=v_text;
size_t dn_tmp_3=v_at;
uint8_t dn_tmp_4=({ uint8_t dn_tmp_5=((uint8_t)(44ULL));
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_5; });
dn_f_byte_find(dn_tmp_2,dn_tmp_3,dn_tmp_4); });
if(((({ size_t dn_tmp_6=v_i;
size_t dn_tmp_7=((size_t)(7ULL));
(dn_tmp_6 < dn_tmp_7); }) && ({ size_t dn_tmp_8=v_end;
size_t dn_tmp_9=({ dn_t_slice0_U8 dn_tmp_10=v_text;
(dn_tmp_10).len; });
(dn_tmp_8 == dn_tmp_9); })) || (({ size_t dn_tmp_11=v_i;
size_t dn_tmp_12=((size_t)(7ULL));
(dn_tmp_11 == dn_tmp_12); }) && ({ size_t dn_tmp_13=v_end;
size_t dn_tmp_14=({ dn_t_slice0_U8 dn_tmp_15=v_text;
(dn_tmp_15).len; });
(dn_tmp_13 != dn_tmp_14); })))){
{ bool dn_tmp_16=0;return dn_tmp_16; }
}
else{
}
uint32_t v_n=((uint32_t)(0ULL));
if((!(({ dn_t_slice0_U8 dn_tmp_17=({ dn_t_slice0_U8 const * dn_tmp_18=(&(v_text));
size_t dn_tmp_19=v_at;
size_t dn_tmp_20=({ size_t dn_tmp_21=v_end;
size_t dn_tmp_22=v_at;
dn_USize_sub(dn_tmp_21,dn_tmp_22); });
({ dn_range(dn_tmp_19,dn_tmp_20,(*(dn_tmp_18)).len);(dn_t_slice0_U8){(*(dn_tmp_18)).data?(*(dn_tmp_18)).data+dn_tmp_19:NULL,dn_tmp_20}; }); });
uint32_t dn_tmp_23=((uint32_t)(16777216ULL));
uint32_t * dn_tmp_24=(&(v_n));
dn_f_decimal(dn_tmp_17,dn_tmp_23,dn_tmp_24); })))){
{ bool dn_tmp_25=0;return dn_tmp_25; }
}
else{
}
(void)(({ dn_r_Gene * dn_tmp_26=(&(v_value));
size_t dn_tmp_27=v_i;
uint32_t dn_tmp_28=v_n;
dn_f_gene_set(dn_tmp_26,dn_tmp_27,dn_tmp_28); }));
{ size_t dn_tmp_32=({ size_t dn_tmp_29=v_end;
size_t dn_tmp_30=((size_t)(1ULL));
dn_USize_add(dn_tmp_29,dn_tmp_30); });size_t *dn_tmp_31=&(v_at);*dn_tmp_31=dn_tmp_32; }
}
 }
if((!(({ dn_r_Gene const * dn_tmp_33=(&(v_value));
dn_f_gene_valid(dn_tmp_33); })))){
{ bool dn_tmp_34=0;return dn_tmp_34; }
}
else{
}
{ dn_r_Gene dn_tmp_36=v_value;dn_r_Gene *dn_tmp_35=&((*((dn_r_Gene *)dn_nonnull((void*)(v_g)))));*dn_tmp_35=dn_tmp_36; }
{ bool dn_tmp_37=1;return dn_tmp_37; }
}

dn_fail("missing native return");
}
static bool dn_cb_gene_parse(dn_t_slice0_U8 v_text,dn_r_Gene * v_g) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_gene_parse(v_text,v_g);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_buf_U8 dn_f_gene_text(dn_r_Gene const * v_g) {
{
dn_t_buf_U8 volatile v_b={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_b=({ size_t dn_tmp_2=((size_t)(0ULL));
dn_new_buf_U8(dn_tmp_2); });
{ size_t dn_tmp_4=((size_t)(0ULL));size_t dn_tmp_3=((size_t)(8ULL));for(size_t v_i=dn_tmp_4;v_i<dn_tmp_3;v_i++){
if(({ size_t dn_tmp_5=v_i;
size_t dn_tmp_6=((size_t)(0ULL));
(dn_tmp_5 > dn_tmp_6); })){
(void)(({ dn_t_buf_U8 * dn_tmp_7=(&(v_b));
uint8_t dn_tmp_8=({ uint8_t dn_tmp_9=((uint8_t)(44ULL));
if((unsigned __int128)dn_tmp_9>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_9; });
dn_push_buf_U8(dn_tmp_7,dn_tmp_8); }));
}
else{
}
(void)(({ dn_t_buf_U8 * dn_tmp_10=(&(v_b));
uint64_t dn_tmp_11=({ uint32_t dn_tmp_12=({ dn_r_Gene const * dn_tmp_13=v_g;
size_t dn_tmp_14=v_i;
dn_f_gene_field(dn_tmp_13,dn_tmp_14); });
if((unsigned __int128)dn_tmp_12>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_12; });
dn_f_append_number(dn_tmp_10,dn_tmp_11); }));
}
 }
{ dn_t_buf_U8 dn_tmp_16=({ dn_t_buf_U8 dn_tmp_15=v_b;v_b=(dn_t_buf_U8){0};dn_adopt_buf_U8(&dn_tmp_15);dn_tmp_15; });dn_adopt_buf_U8(&dn_tmp_16);dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_16; }
dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_t_buf_U8 dn_cb_gene_text(dn_r_Gene const * v_g) {
 if(!dn_enter()){return (dn_t_buf_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_buf_U8){0};}
 dn_t_buf_U8 dn_result=dn_f_gene_text(v_g);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint64_t dn_f_gene_hash(dn_r_Gene const * v_g) {
{
uint64_t v_hash=((uint64_t)(1469598103934665603ULL));
{ size_t dn_tmp_1=((size_t)(0ULL));size_t dn_tmp_0=((size_t)(8ULL));for(size_t v_i=dn_tmp_1;v_i<dn_tmp_0;v_i++){
{ uint64_t dn_tmp_11=({ uint64_t dn_tmp_2=({ uint64_t dn_tmp_3=v_hash;
uint64_t dn_tmp_4=({ uint32_t dn_tmp_5=({ dn_r_Gene const * dn_tmp_6=v_g;
size_t dn_tmp_7=v_i;
dn_f_gene_field(dn_tmp_6,dn_tmp_7); });
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_5; });
dn_U64_bits((uint64_t)dn_tmp_3 ^ (uint64_t)dn_tmp_4); });
uint64_t dn_tmp_8=({ uint64_t dn_tmp_9=((uint64_t)(1099511628211ULL));
if((unsigned __int128)dn_tmp_9>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_9; });
dn_U64_wrap_mul(dn_tmp_2,dn_tmp_8); });uint64_t *dn_tmp_10=&(v_hash);*dn_tmp_10=dn_tmp_11; }
}
 }
{ uint64_t dn_tmp_12=v_hash;return dn_tmp_12; }
}

dn_fail("missing native return");
}
static uint64_t dn_cb_gene_hash(dn_r_Gene const * v_g) {
 if(!dn_enter()){return (uint64_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint64_t){0};}
 uint64_t dn_result=dn_f_gene_hash(v_g);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_evolution_random(uint64_t * v_state) {
{
{ uint64_t dn_tmp_7=({ uint64_t dn_tmp_0=({ uint64_t dn_tmp_1=(*((uint64_t *)dn_nonnull((void*)(v_state))));
uint64_t dn_tmp_2=({ uint64_t dn_tmp_3=((uint64_t)(6364136223846793005ULL));
if((unsigned __int128)dn_tmp_3>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_3; });
dn_U64_wrap_mul(dn_tmp_1,dn_tmp_2); });
uint64_t dn_tmp_4=({ uint64_t dn_tmp_5=((uint64_t)(1442695040888963407ULL));
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_5; });
dn_U64_wrap_add(dn_tmp_0,dn_tmp_4); });uint64_t *dn_tmp_6=&((*((uint64_t *)dn_nonnull((void*)(v_state)))));*dn_tmp_6=dn_tmp_7; }
{ uint32_t dn_tmp_12=({ uint64_t dn_tmp_8=({ uint64_t dn_tmp_9=(*((uint64_t *)dn_nonnull((void*)(v_state))));
uint64_t dn_tmp_10=({ uint64_t dn_tmp_11=((uint64_t)(32ULL));
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_11; });
dn_U64_shr(dn_tmp_9,dn_tmp_10); });
if((unsigned __int128)dn_tmp_8>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_8; });return dn_tmp_12; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_evolution_random(uint64_t * v_state) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_evolution_random(v_state);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_gene_normalize(dn_r_Gene * v_g) {
{
if(({ uint32_t dn_tmp_0=(v_g)->f_guard;
uint32_t dn_tmp_1=({ uint32_t dn_tmp_2=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_2>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_2; });
(dn_tmp_0 == dn_tmp_1); })){
{ uint32_t dn_tmp_4=((uint32_t)(0ULL));uint32_t *dn_tmp_3=&((v_g)->f_repeats);*dn_tmp_3=dn_tmp_4; }
{ uint32_t dn_tmp_6=((uint32_t)(0ULL));uint32_t *dn_tmp_5=&((v_g)->f_template_);*dn_tmp_5=dn_tmp_6; }
{ uint32_t dn_tmp_8=((uint32_t)(1048576ULL));uint32_t *dn_tmp_7=&((v_g)->f_gap);*dn_tmp_7=dn_tmp_8; }
}
else{
if(({ uint32_t dn_tmp_9=(v_g)->f_repeats;
uint32_t dn_tmp_10=({ uint32_t dn_tmp_11=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_11; });
(dn_tmp_9 == dn_tmp_10); })){
{ uint32_t dn_tmp_13=((uint32_t)(1ULL));uint32_t *dn_tmp_12=&((v_g)->f_repeats);*dn_tmp_12=dn_tmp_13; }
}
else{
}
}
}

return 0;
}
static void dn_cb_gene_normalize(dn_r_Gene * v_g) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_gene_normalize(v_g);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_gene_mutate(dn_r_Gene * v_g,uint64_t * v_state) {
{
dn_t_arr12_U32 v_anchors=({ uint32_t dn_tmp_0=((uint32_t)(0ULL));
uint32_t dn_tmp_1=((uint32_t)(1ULL));
uint32_t dn_tmp_2=((uint32_t)(2ULL));
uint32_t dn_tmp_3=((uint32_t)(5ULL));
uint32_t dn_tmp_4=((uint32_t)(32ULL));
uint32_t dn_tmp_5=((uint32_t)(64ULL));
uint32_t dn_tmp_6=((uint32_t)(128ULL));
uint32_t dn_tmp_7=((uint32_t)(256ULL));
uint32_t dn_tmp_8=((uint32_t)(512ULL));
uint32_t dn_tmp_9=((uint32_t)(768ULL));
uint32_t dn_tmp_10=((uint32_t)(1024ULL));
uint32_t dn_tmp_11=((uint32_t)(1200ULL));
((dn_t_arr12_U32){{dn_tmp_0,dn_tmp_1,dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_11}}); });
{ size_t dn_tmp_13=((size_t)(0ULL));size_t dn_tmp_12=((size_t)(2ULL));for(size_t v_turn=dn_tmp_13;v_turn<dn_tmp_12;v_turn++){
size_t v_i=({ uint32_t dn_tmp_14=({ uint32_t dn_tmp_15=({ uint64_t * dn_tmp_16=v_state;
dn_f_evolution_random(dn_tmp_16); });
uint32_t dn_tmp_17=({ uint32_t dn_tmp_18=((uint32_t)(8ULL));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_18; });
dn_U32_mod(dn_tmp_15,dn_tmp_17); });
if((unsigned __int128)dn_tmp_14>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_14; });
uint32_t v_r=({ uint64_t * dn_tmp_19=v_state;
dn_f_evolution_random(dn_tmp_19); });
if(({ size_t dn_tmp_20=v_i;
size_t dn_tmp_21=((size_t)(0ULL));
(dn_tmp_20 == dn_tmp_21); })){
{ uint32_t dn_tmp_26=({ uint32_t dn_tmp_22=v_r;
uint32_t dn_tmp_23=({ uint32_t dn_tmp_24=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_24>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_24; });
dn_U32_mod(dn_tmp_22,dn_tmp_23); });uint32_t *dn_tmp_25=&((v_g)->f_reverse);*dn_tmp_25=dn_tmp_26; }
}
else{
if((({ size_t dn_tmp_27=v_i;
size_t dn_tmp_28=((size_t)(1ULL));
(dn_tmp_27 == dn_tmp_28); }) || ({ size_t dn_tmp_29=v_i;
size_t dn_tmp_30=((size_t)(2ULL));
(dn_tmp_29 == dn_tmp_30); }))){
uint32_t v_cut=(*({ dn_t_arr12_U32 *dn_tmp_31=&(v_anchors);size_t dn_tmp_32=({ uint32_t dn_tmp_33=({ uint32_t dn_tmp_34=v_r;
uint32_t dn_tmp_35=({ uint32_t dn_tmp_36=((uint32_t)(12ULL));
if((unsigned __int128)dn_tmp_36>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_36; });
dn_U32_mod(dn_tmp_34,dn_tmp_35); });
if((unsigned __int128)dn_tmp_33>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_33; });&dn_tmp_31->data[dn_index(dn_tmp_32,12)]; }));
if((({ uint32_t dn_tmp_37=({ uint32_t dn_tmp_38=v_r;
uint32_t dn_tmp_39=({ uint32_t dn_tmp_40=((uint32_t)(16ULL));
if((unsigned __int128)dn_tmp_40>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_40; });
dn_U32_bits((uint64_t)dn_tmp_38 & (uint64_t)dn_tmp_39); });
uint32_t dn_tmp_41=({ uint32_t dn_tmp_42=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_42>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_42; });
(dn_tmp_37 != dn_tmp_41); }) && ({ uint32_t dn_tmp_43=v_cut;
uint32_t dn_tmp_44=({ uint32_t dn_tmp_45=((uint32_t)(8ULL));
if((unsigned __int128)dn_tmp_45>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_45; });
(dn_tmp_43 > dn_tmp_44); }))){
{ uint32_t dn_tmp_54=({ uint32_t dn_tmp_46=({ uint32_t dn_tmp_47=v_cut;
uint32_t dn_tmp_48=({ uint32_t dn_tmp_49=v_r;
uint32_t dn_tmp_50=({ uint32_t dn_tmp_51=((uint32_t)(17ULL));
if((unsigned __int128)dn_tmp_51>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_51; });
dn_U32_mod(dn_tmp_49,dn_tmp_50); });
dn_U32_add(dn_tmp_47,dn_tmp_48); });
uint32_t dn_tmp_52=((uint32_t)(1200ULL));
dn_f_min_u32(dn_tmp_46,dn_tmp_52); });uint32_t *dn_tmp_53=&(v_cut);*dn_tmp_53=dn_tmp_54; }
}
else{
}
(void)(({ dn_r_Gene * dn_tmp_55=v_g;
size_t dn_tmp_56=v_i;
uint32_t dn_tmp_57=v_cut;
dn_f_gene_set(dn_tmp_55,dn_tmp_56,dn_tmp_57); }));
}
else{
if(({ size_t dn_tmp_58=v_i;
size_t dn_tmp_59=((size_t)(3ULL));
(dn_tmp_58 == dn_tmp_59); })){
{ uint32_t dn_tmp_64=({ uint32_t dn_tmp_60=v_r;
uint32_t dn_tmp_61=({ uint32_t dn_tmp_62=((uint32_t)(33ULL));
if((unsigned __int128)dn_tmp_62>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_62; });
dn_U32_mod(dn_tmp_60,dn_tmp_61); });uint32_t *dn_tmp_63=&((v_g)->f_overlap);*dn_tmp_63=dn_tmp_64; }
}
else{
if(({ size_t dn_tmp_65=v_i;
size_t dn_tmp_66=((size_t)(4ULL));
(dn_tmp_65 == dn_tmp_66); })){
{ uint32_t dn_tmp_71=({ uint32_t dn_tmp_67=v_r;
uint32_t dn_tmp_68=({ uint32_t dn_tmp_69=((uint32_t)(3ULL));
if((unsigned __int128)dn_tmp_69>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_69; });
dn_U32_mod(dn_tmp_67,dn_tmp_68); });uint32_t *dn_tmp_70=&((v_g)->f_guard);*dn_tmp_70=dn_tmp_71; }
}
else{
if(({ size_t dn_tmp_72=v_i;
size_t dn_tmp_73=((size_t)(5ULL));
(dn_tmp_72 == dn_tmp_73); })){
{ uint32_t dn_tmp_80=({ uint32_t dn_tmp_74=((uint32_t)(1ULL));
uint32_t dn_tmp_75=({ uint32_t dn_tmp_76=v_r;
uint32_t dn_tmp_77=({ uint32_t dn_tmp_78=((uint32_t)(3ULL));
if((unsigned __int128)dn_tmp_78>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_78; });
dn_U32_mod(dn_tmp_76,dn_tmp_77); });
dn_U32_add(dn_tmp_74,dn_tmp_75); });uint32_t *dn_tmp_79=&((v_g)->f_repeats);*dn_tmp_79=dn_tmp_80; }
}
else{
if(({ size_t dn_tmp_81=v_i;
size_t dn_tmp_82=((size_t)(6ULL));
(dn_tmp_81 == dn_tmp_82); })){
{ uint32_t dn_tmp_90=({ uint32_t dn_tmp_83=({ uint32_t dn_tmp_84=((uint32_t)(65536ULL));
if((unsigned __int128)dn_tmp_84>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_84; });
uint32_t dn_tmp_85=({ uint32_t dn_tmp_86=v_r;
uint32_t dn_tmp_87=({ uint32_t dn_tmp_88=((uint32_t)(9ULL));
if((unsigned __int128)dn_tmp_88>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_88; });
dn_U32_mod(dn_tmp_86,dn_tmp_87); });
dn_U32_shl(dn_tmp_83,dn_tmp_85); });uint32_t *dn_tmp_89=&((v_g)->f_gap);*dn_tmp_89=dn_tmp_90; }
}
else{
{ uint32_t dn_tmp_95=({ uint32_t dn_tmp_91=v_r;
uint32_t dn_tmp_92=({ uint32_t dn_tmp_93=((uint32_t)(3ULL));
if((unsigned __int128)dn_tmp_93>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_93; });
dn_U32_mod(dn_tmp_91,dn_tmp_92); });uint32_t *dn_tmp_94=&((v_g)->f_template_);*dn_tmp_94=dn_tmp_95; }
}
}
}
}
}
}
}
 }
(void)(({ dn_r_Gene * dn_tmp_96=v_g;
dn_f_gene_normalize(dn_tmp_96); }));
}

return 0;
}
static void dn_cb_gene_mutate(dn_r_Gene * v_g,uint64_t * v_state) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_gene_mutate(v_g,v_state);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint32_t dn_f_min_u32(uint32_t v_a,uint32_t v_b) {
{
if(({ uint32_t dn_tmp_0=v_a;
uint32_t dn_tmp_1=v_b;
(dn_tmp_0 < dn_tmp_1); })){
{ uint32_t dn_tmp_2=v_a;return dn_tmp_2; }
}
else{
}
{ uint32_t dn_tmp_3=v_b;return dn_tmp_3; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_min_u32(uint32_t v_a,uint32_t v_b) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_min_u32(v_a,v_b);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_Gene dn_f_gene_cross(dn_r_Gene const * v_a,dn_r_Gene const * v_b,uint64_t * v_state) {
{
dn_r_Gene v_g=(*((dn_r_Gene const *)dn_nonnull((void*)(v_a))));
{ size_t dn_tmp_1=((size_t)(0ULL));size_t dn_tmp_0=((size_t)(8ULL));for(size_t v_i=dn_tmp_1;v_i<dn_tmp_0;v_i++){
if(({ uint32_t dn_tmp_2=({ uint32_t dn_tmp_3=({ uint64_t * dn_tmp_4=v_state;
dn_f_evolution_random(dn_tmp_4); });
uint32_t dn_tmp_5=({ uint32_t dn_tmp_6=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_6>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_6; });
dn_U32_mod(dn_tmp_3,dn_tmp_5); });
uint32_t dn_tmp_7=({ uint32_t dn_tmp_8=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_8>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_8; });
(dn_tmp_2 == dn_tmp_7); })){
(void)(({ dn_r_Gene * dn_tmp_9=(&(v_g));
size_t dn_tmp_10=v_i;
uint32_t dn_tmp_11=({ dn_r_Gene const * dn_tmp_12=v_b;
size_t dn_tmp_13=v_i;
dn_f_gene_field(dn_tmp_12,dn_tmp_13); });
dn_f_gene_set(dn_tmp_9,dn_tmp_10,dn_tmp_11); }));
}
else{
}
}
 }
(void)(({ dn_r_Gene * dn_tmp_14=(&(v_g));
dn_f_gene_normalize(dn_tmp_14); }));
{ dn_r_Gene dn_tmp_15=v_g;return dn_tmp_15; }
}

dn_fail("missing native return");
}
static dn_r_Gene dn_cb_gene_cross(dn_r_Gene const * v_a,dn_r_Gene const * v_b,uint64_t * v_state) {
 if(!dn_enter()){return (dn_r_Gene){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_Gene){0};}
 dn_r_Gene dn_result=dn_f_gene_cross(v_a,v_b,v_state);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static size_t dn_f_tournament(dn_t_buf_r10_Individual const * v_items,uint64_t * v_state) {
{
size_t v_best=({ size_t dn_tmp_0=({ uint32_t dn_tmp_1=({ uint64_t * dn_tmp_2=v_state;
dn_f_evolution_random(dn_tmp_2); });
if((unsigned __int128)dn_tmp_1>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_1; });
size_t dn_tmp_3=({ dn_t_buf_r10_Individual const * dn_tmp_4=v_items;
(*(dn_tmp_4)).len; });
dn_USize_mod(dn_tmp_0,dn_tmp_3); });
{ size_t dn_tmp_6=((size_t)(0ULL));size_t dn_tmp_5=((size_t)(2ULL));for(size_t v_n=dn_tmp_6;v_n<dn_tmp_5;v_n++){
size_t v_i=({ size_t dn_tmp_7=({ uint32_t dn_tmp_8=({ uint64_t * dn_tmp_9=v_state;
dn_f_evolution_random(dn_tmp_9); });
if((unsigned __int128)dn_tmp_8>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_8; });
size_t dn_tmp_10=({ dn_t_buf_r10_Individual const * dn_tmp_11=v_items;
(*(dn_tmp_11)).len; });
dn_USize_mod(dn_tmp_7,dn_tmp_10); });
if(({ uint32_t dn_tmp_12=((*({ dn_t_buf_r10_Individual const * dn_tmp_13=v_items;size_t dn_tmp_14=v_i;&dn_tmp_13->data[dn_index(dn_tmp_14,dn_tmp_13->len)]; }))).f_fitness;
uint32_t dn_tmp_15=((*({ dn_t_buf_r10_Individual const * dn_tmp_16=v_items;size_t dn_tmp_17=v_best;&dn_tmp_16->data[dn_index(dn_tmp_17,dn_tmp_16->len)]; }))).f_fitness;
(dn_tmp_12 > dn_tmp_15); })){
{ size_t dn_tmp_19=v_i;size_t *dn_tmp_18=&(v_best);*dn_tmp_18=dn_tmp_19; }
}
else{
}
}
 }
{ size_t dn_tmp_20=v_best;return dn_tmp_20; }
}

dn_fail("missing native return");
}
static size_t dn_cb_tournament(dn_t_buf_r10_Individual const * v_items,uint64_t * v_state) {
 if(!dn_enter()){return (size_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (size_t){0};}
 size_t dn_result=dn_f_tournament(v_items,v_state);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_buf_r4_Gene dn_f_evolve(dn_t_buf_r10_Individual const * v_items,uint64_t * v_state,size_t v_count) {
{
dn_t_buf_r4_Gene volatile v_output={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_r4_Gene(&v_output);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_output=({ size_t dn_tmp_2=((size_t)(0ULL));
dn_new_buf_r4_Gene(dn_tmp_2); });
size_t v_elite=({ dn_t_buf_r10_Individual const * dn_tmp_3=v_items;
(*(dn_tmp_3)).len; });
{ size_t dn_tmp_6=((size_t)(0ULL));size_t dn_tmp_5=({ dn_t_buf_r10_Individual const * dn_tmp_4=v_items;
(*(dn_tmp_4)).len; });for(size_t v_i=dn_tmp_6;v_i<dn_tmp_5;v_i++){
if((({ uint32_t dn_tmp_7=((*({ dn_t_buf_r10_Individual const * dn_tmp_8=v_items;size_t dn_tmp_9=v_i;&dn_tmp_8->data[dn_tmp_9]; }))).f_trials;
uint32_t dn_tmp_10=({ uint32_t dn_tmp_11=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_11; });
(dn_tmp_7 > dn_tmp_10); }) && (({ size_t dn_tmp_12=v_elite;
size_t dn_tmp_13=({ dn_t_buf_r10_Individual const * dn_tmp_14=v_items;
(*(dn_tmp_14)).len; });
(dn_tmp_12 == dn_tmp_13); }) || ({ uint32_t dn_tmp_15=((*({ dn_t_buf_r10_Individual const * dn_tmp_16=v_items;size_t dn_tmp_17=v_i;&dn_tmp_16->data[dn_tmp_17]; }))).f_fitness;
uint32_t dn_tmp_18=((*({ dn_t_buf_r10_Individual const * dn_tmp_19=v_items;size_t dn_tmp_20=v_elite;&dn_tmp_19->data[dn_index(dn_tmp_20,dn_tmp_19->len)]; }))).f_fitness;
(dn_tmp_15 > dn_tmp_18); })))){
{ size_t dn_tmp_22=v_i;size_t *dn_tmp_21=&(v_elite);*dn_tmp_21=dn_tmp_22; }
}
else{
}
}
 }
if(({ size_t dn_tmp_23=v_elite;
size_t dn_tmp_24=({ dn_t_buf_r10_Individual const * dn_tmp_25=v_items;
(*(dn_tmp_25)).len; });
(dn_tmp_23 < dn_tmp_24); })){
(void)(({ dn_t_buf_r4_Gene * dn_tmp_26=(&(v_output));
dn_r_Gene dn_tmp_27=((*({ dn_t_buf_r10_Individual const * dn_tmp_28=v_items;size_t dn_tmp_29=v_elite;&dn_tmp_28->data[dn_index(dn_tmp_29,dn_tmp_28->len)]; }))).f_gene;
dn_push_buf_r4_Gene(dn_tmp_26,dn_tmp_27); }));
}
else{
}
size_t v_attempts=((size_t)(0ULL));
while((({ size_t dn_tmp_30=({ dn_t_buf_r4_Gene const * dn_tmp_31=(&(v_output));
(*(dn_tmp_31)).len; });
size_t dn_tmp_32=v_count;
(dn_tmp_30 < dn_tmp_32); }) && ({ size_t dn_tmp_33=v_attempts;
size_t dn_tmp_34=({ size_t dn_tmp_35=v_count;
size_t dn_tmp_36=({ size_t dn_tmp_37=((size_t)(64ULL));
if((unsigned __int128)dn_tmp_37>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_37; });
dn_USize_mul(dn_tmp_35,dn_tmp_36); });
(dn_tmp_33 < dn_tmp_34); }))){
{ size_t dn_tmp_41=({ size_t dn_tmp_38=v_attempts;
size_t dn_tmp_39=((size_t)(1ULL));
dn_USize_add(dn_tmp_38,dn_tmp_39); });size_t *dn_tmp_40=&(v_attempts);*dn_tmp_40=dn_tmp_41; }
dn_r_Gene v_gene=({ dn_f_gene_default(); });
if(({ size_t dn_tmp_42=({ dn_t_buf_r10_Individual const * dn_tmp_43=v_items;
(*(dn_tmp_43)).len; });
size_t dn_tmp_44=((size_t)(0ULL));
(dn_tmp_42 > dn_tmp_44); })){
size_t v_a=({ dn_t_buf_r10_Individual const * dn_tmp_45=v_items;
uint64_t * dn_tmp_46=v_state;
dn_f_tournament(dn_tmp_45,dn_tmp_46); });
size_t v_b=({ dn_t_buf_r10_Individual const * dn_tmp_47=v_items;
uint64_t * dn_tmp_48=v_state;
dn_f_tournament(dn_tmp_47,dn_tmp_48); });
{ dn_r_Gene dn_tmp_57=({ dn_r_Gene const * dn_tmp_49=(&(((*({ dn_t_buf_r10_Individual const * dn_tmp_50=v_items;size_t dn_tmp_51=v_a;&dn_tmp_50->data[dn_index(dn_tmp_51,dn_tmp_50->len)]; }))).f_gene));
dn_r_Gene const * dn_tmp_52=(&(((*({ dn_t_buf_r10_Individual const * dn_tmp_53=v_items;size_t dn_tmp_54=v_b;&dn_tmp_53->data[dn_index(dn_tmp_54,dn_tmp_53->len)]; }))).f_gene));
uint64_t * dn_tmp_55=v_state;
dn_f_gene_cross(dn_tmp_49,dn_tmp_52,dn_tmp_55); });dn_r_Gene *dn_tmp_56=&(v_gene);*dn_tmp_56=dn_tmp_57; }
}
else{
}
(void)(({ dn_r_Gene * dn_tmp_58=(&(v_gene));
uint64_t * dn_tmp_59=v_state;
dn_f_gene_mutate(dn_tmp_58,dn_tmp_59); }));
bool v_duplicate=0;
{ size_t dn_tmp_62=((size_t)(0ULL));size_t dn_tmp_61=({ dn_t_buf_r4_Gene const * dn_tmp_60=(&(v_output));
(*(dn_tmp_60)).len; });for(size_t v_i=dn_tmp_62;v_i<dn_tmp_61;v_i++){
{ bool dn_tmp_70=(v_duplicate || ({ uint64_t dn_tmp_63=({ dn_r_Gene const * dn_tmp_64=(&((*({ dn_t_buf_r4_Gene volatile *dn_tmp_65=&(v_output);size_t dn_tmp_66=v_i;&dn_tmp_65->data[dn_tmp_66]; }))));
dn_f_gene_hash(dn_tmp_64); });
uint64_t dn_tmp_67=({ dn_r_Gene const * dn_tmp_68=(&(v_gene));
dn_f_gene_hash(dn_tmp_68); });
(dn_tmp_63 == dn_tmp_67); }));bool *dn_tmp_69=&(v_duplicate);*dn_tmp_69=dn_tmp_70; }
}
 }
if(((!(v_duplicate)) && ({ dn_r_Gene const * dn_tmp_71=(&(v_gene));
dn_f_gene_valid(dn_tmp_71); }))){
(void)(({ dn_t_buf_r4_Gene * dn_tmp_72=(&(v_output));
dn_r_Gene dn_tmp_73=v_gene;
dn_push_buf_r4_Gene(dn_tmp_72,dn_tmp_73); }));
}
else{
}
}
{ dn_t_buf_r4_Gene dn_tmp_75=({ dn_t_buf_r4_Gene dn_tmp_74=v_output;v_output=(dn_t_buf_r4_Gene){0};dn_adopt_buf_r4_Gene(&dn_tmp_74);dn_tmp_74; });dn_adopt_buf_r4_Gene(&dn_tmp_75);dn_drop_buf_r4_Gene(&v_output);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_75; }
dn_drop_buf_r4_Gene(&v_output);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_t_buf_r4_Gene dn_cb_evolve(dn_t_buf_r10_Individual const * v_items,uint64_t * v_state,size_t v_count) {
 if(!dn_enter()){return (dn_t_buf_r4_Gene){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_buf_r4_Gene){0};}
 dn_t_buf_r4_Gene dn_result=dn_f_evolve(v_items,v_state,v_count);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_profile_gene(dn_t_slice0_U8 v_text,dn_r_Gene * v_gene,uint32_t * v_target) {
{
bool v_schema=0;
bool v_found=0;
size_t v_at=((size_t)(0ULL));
bool v_active=0;
bool v_target_seen=0;
dn_r_Gene v_parsed=({ dn_f_gene_default(); });
uint32_t v_scope=((uint32_t)(0ULL));
if(({ dn_t_slice0_U8 dn_tmp_0=v_text;
size_t dn_tmp_1=((size_t)(0ULL));
dn_t_slice0_U8 dn_tmp_2=({ dn_utf8 dn_tmp_3=((dn_utf8){(const uint8_t*)"\357\273\277", 3});
((dn_t_slice0_U8){dn_tmp_3.data,dn_tmp_3.len}); });
dn_f_bytes_at(dn_tmp_0,dn_tmp_1,dn_tmp_2); })){
{ size_t dn_tmp_5=((size_t)(3ULL));size_t *dn_tmp_4=&(v_at);*dn_tmp_4=dn_tmp_5; }
}
else{
}
while(({ size_t dn_tmp_6=v_at;
size_t dn_tmp_7=({ dn_t_slice0_U8 dn_tmp_8=v_text;
(dn_tmp_8).len; });
(dn_tmp_6 < dn_tmp_7); })){
size_t v_end=({ dn_t_slice0_U8 dn_tmp_9=v_text;
size_t dn_tmp_10=v_at;
uint8_t dn_tmp_11=({ uint8_t dn_tmp_12=((uint8_t)(10ULL));
if((unsigned __int128)dn_tmp_12>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_12; });
dn_f_byte_find(dn_tmp_9,dn_tmp_10,dn_tmp_11); });
dn_t_slice0_U8 v_raw=({ dn_t_slice0_U8 const * dn_tmp_13=(&(v_text));
size_t dn_tmp_14=v_at;
size_t dn_tmp_15=({ size_t dn_tmp_16=v_end;
size_t dn_tmp_17=v_at;
dn_USize_sub(dn_tmp_16,dn_tmp_17); });
({ dn_range(dn_tmp_14,dn_tmp_15,(*(dn_tmp_13)).len);(dn_t_slice0_U8){(*(dn_tmp_13)).data?(*(dn_tmp_13)).data+dn_tmp_14:NULL,dn_tmp_15}; }); });
dn_t_slice0_U8 v_line=({ dn_t_slice0_U8 dn_tmp_18=v_raw;
dn_f_trimmed(dn_tmp_18); });
{ size_t dn_tmp_22=({ size_t dn_tmp_19=v_end;
size_t dn_tmp_20=((size_t)(1ULL));
dn_USize_add(dn_tmp_19,dn_tmp_20); });size_t *dn_tmp_21=&(v_at);*dn_tmp_21=dn_tmp_22; }
if(({ dn_t_slice0_U8 dn_tmp_23=v_line;
dn_t_slice0_U8 dn_tmp_24=({ dn_utf8 dn_tmp_25=((dn_utf8){(const uint8_t*)"[DPIProfile]", 12});
((dn_t_slice0_U8){dn_tmp_25.data,dn_tmp_25.len}); });
dn_f_bytes_equal(dn_tmp_23,dn_tmp_24); })){
{ bool dn_tmp_27=1;bool *dn_tmp_26=&(v_active);*dn_tmp_26=dn_tmp_27; }
continue;
}
else{
}
if(((({ size_t dn_tmp_28=({ dn_t_slice0_U8 dn_tmp_29=v_line;
(dn_tmp_29).len; });
size_t dn_tmp_30=((size_t)(0ULL));
(dn_tmp_28 == dn_tmp_30); }) || ({ uint8_t dn_tmp_31=(*({ dn_t_slice0_U8 *dn_tmp_32=&(v_line);size_t dn_tmp_33=((size_t)(0ULL));&dn_tmp_32->data[dn_index(dn_tmp_33,dn_tmp_32->len)]; }));
uint8_t dn_tmp_34=({ uint8_t dn_tmp_35=((uint8_t)(35ULL));
if((unsigned __int128)dn_tmp_35>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_35; });
(dn_tmp_31 == dn_tmp_34); })) || ({ uint8_t dn_tmp_36=(*({ dn_t_slice0_U8 *dn_tmp_37=&(v_line);size_t dn_tmp_38=((size_t)(0ULL));&dn_tmp_37->data[dn_index(dn_tmp_38,dn_tmp_37->len)]; }));
uint8_t dn_tmp_39=({ uint8_t dn_tmp_40=((uint8_t)(59ULL));
if((unsigned __int128)dn_tmp_40>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_40; });
(dn_tmp_36 == dn_tmp_39); }))){
continue;
}
else{
}
if(({ uint8_t dn_tmp_41=(*({ dn_t_slice0_U8 *dn_tmp_42=&(v_line);size_t dn_tmp_43=((size_t)(0ULL));&dn_tmp_42->data[dn_index(dn_tmp_43,dn_tmp_42->len)]; }));
uint8_t dn_tmp_44=({ uint8_t dn_tmp_45=((uint8_t)(91ULL));
if((unsigned __int128)dn_tmp_45>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_45; });
(dn_tmp_41 == dn_tmp_44); })){
{ bool dn_tmp_47=0;bool *dn_tmp_46=&(v_active);*dn_tmp_46=dn_tmp_47; }
continue;
}
else{
}
if((!(v_active))){
continue;
}
else{
}
size_t v_equal=({ dn_t_slice0_U8 dn_tmp_48=v_line;
size_t dn_tmp_49=((size_t)(0ULL));
uint8_t dn_tmp_50=({ uint8_t dn_tmp_51=((uint8_t)(61ULL));
if((unsigned __int128)dn_tmp_51>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_51; });
dn_f_byte_find(dn_tmp_48,dn_tmp_49,dn_tmp_50); });
if(({ size_t dn_tmp_52=v_equal;
size_t dn_tmp_53=({ dn_t_slice0_U8 dn_tmp_54=v_line;
(dn_tmp_54).len; });
(dn_tmp_52 == dn_tmp_53); })){
{ bool dn_tmp_55=0;return dn_tmp_55; }
}
else{
}
dn_t_slice0_U8 v_key=({ dn_t_slice0_U8 const * dn_tmp_56=(&(v_line));
size_t dn_tmp_57=((size_t)(0ULL));
size_t dn_tmp_58=v_equal;
({ dn_range(dn_tmp_57,dn_tmp_58,(*(dn_tmp_56)).len);(dn_t_slice0_U8){(*(dn_tmp_56)).data?(*(dn_tmp_56)).data+dn_tmp_57:NULL,dn_tmp_58}; }); });
dn_t_slice0_U8 v_value=({ dn_t_slice0_U8 const * dn_tmp_59=(&(v_line));
size_t dn_tmp_60=({ size_t dn_tmp_61=v_equal;
size_t dn_tmp_62=((size_t)(1ULL));
dn_USize_add(dn_tmp_61,dn_tmp_62); });
size_t dn_tmp_63=({ size_t dn_tmp_64=({ size_t dn_tmp_65=({ dn_t_slice0_U8 dn_tmp_66=v_line;
(dn_tmp_66).len; });
size_t dn_tmp_67=v_equal;
dn_USize_sub(dn_tmp_65,dn_tmp_67); });
size_t dn_tmp_68=((size_t)(1ULL));
dn_USize_sub(dn_tmp_64,dn_tmp_68); });
({ dn_range(dn_tmp_60,dn_tmp_63,(*(dn_tmp_59)).len);(dn_t_slice0_U8){(*(dn_tmp_59)).data?(*(dn_tmp_59)).data+dn_tmp_60:NULL,dn_tmp_63}; }); });
if(({ dn_t_slice0_U8 dn_tmp_69=v_key;
dn_t_slice0_U8 dn_tmp_70=({ dn_utf8 dn_tmp_71=((dn_utf8){(const uint8_t*)"Schema", 6});
((dn_t_slice0_U8){dn_tmp_71.data,dn_tmp_71.len}); });
dn_f_bytes_equal(dn_tmp_69,dn_tmp_70); })){
if((v_schema || (!(({ dn_t_slice0_U8 dn_tmp_72=v_value;
dn_t_slice0_U8 dn_tmp_73=({ dn_utf8 dn_tmp_74=((dn_utf8){(const uint8_t*)"1", 1});
((dn_t_slice0_U8){dn_tmp_74.data,dn_tmp_74.len}); });
dn_f_bytes_equal(dn_tmp_72,dn_tmp_73); }))))){
{ bool dn_tmp_75=0;return dn_tmp_75; }
}
else{
}
{ bool dn_tmp_77=1;bool *dn_tmp_76=&(v_schema);*dn_tmp_76=dn_tmp_77; }
}
else{
}
if(({ dn_t_slice0_U8 dn_tmp_78=v_key;
dn_t_slice0_U8 dn_tmp_79=({ dn_utf8 dn_tmp_80=((dn_utf8){(const uint8_t*)"Gene", 4});
((dn_t_slice0_U8){dn_tmp_80.data,dn_tmp_80.len}); });
dn_f_bytes_equal(dn_tmp_78,dn_tmp_79); })){
if((v_found || (!(({ dn_t_slice0_U8 dn_tmp_81=v_value;
dn_r_Gene * dn_tmp_82=(&(v_parsed));
dn_f_gene_parse(dn_tmp_81,dn_tmp_82); }))))){
{ bool dn_tmp_83=0;return dn_tmp_83; }
}
else{
}
{ bool dn_tmp_85=1;bool *dn_tmp_84=&(v_found);*dn_tmp_84=dn_tmp_85; }
}
else{
}
if(({ dn_t_slice0_U8 dn_tmp_86=v_key;
dn_t_slice0_U8 dn_tmp_87=({ dn_utf8 dn_tmp_88=((dn_utf8){(const uint8_t*)"Target", 6});
((dn_t_slice0_U8){dn_tmp_88.data,dn_tmp_88.len}); });
dn_f_bytes_equal(dn_tmp_86,dn_tmp_87); })){
if((v_target_seen || (!(({ dn_t_slice0_U8 dn_tmp_89=v_value;
uint32_t dn_tmp_90=((uint32_t)(6ULL));
uint32_t * dn_tmp_91=(&(v_scope));
dn_f_decimal(dn_tmp_89,dn_tmp_90,dn_tmp_91); }))))){
{ bool dn_tmp_92=0;return dn_tmp_92; }
}
else{
}
{ bool dn_tmp_94=1;bool *dn_tmp_93=&(v_target_seen);*dn_tmp_93=dn_tmp_94; }
}
else{
}
}
if((((!(v_schema)) || (!(v_found))) || (!(v_target_seen)))){
{ bool dn_tmp_95=0;return dn_tmp_95; }
}
else{
}
{ dn_r_Gene dn_tmp_97=v_parsed;dn_r_Gene *dn_tmp_96=&((*((dn_r_Gene *)dn_nonnull((void*)(v_gene)))));*dn_tmp_96=dn_tmp_97; }
{ uint32_t dn_tmp_99=v_scope;uint32_t *dn_tmp_98=&((*((uint32_t *)dn_nonnull((void*)(v_target)))));*dn_tmp_98=dn_tmp_99; }
{ bool dn_tmp_100=1;return dn_tmp_100; }
}

dn_fail("missing native return");
}
static bool dn_cb_profile_gene(dn_t_slice0_U8 v_text,dn_r_Gene * v_gene,uint32_t * v_target) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_profile_gene(v_text,v_gene,v_target);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_training_view(dn_t_slice0_U8 v_text,dn_r_TrainingView * v_view) {
{
dn_r_TrainingView v_v=({ ((dn_r_TrainingView){0}); });
size_t v_at=((size_t)(0ULL));
size_t v_fields=((size_t)(0ULL));
if(({ dn_t_slice0_U8 dn_tmp_0=v_text;
size_t dn_tmp_1=((size_t)(0ULL));
dn_t_slice0_U8 dn_tmp_2=({ dn_utf8 dn_tmp_3=((dn_utf8){(const uint8_t*)"\357\273\277", 3});
((dn_t_slice0_U8){dn_tmp_3.data,dn_tmp_3.len}); });
dn_f_bytes_at(dn_tmp_0,dn_tmp_1,dn_tmp_2); })){
{ size_t dn_tmp_5=((size_t)(3ULL));size_t *dn_tmp_4=&(v_at);*dn_tmp_4=dn_tmp_5; }
}
else{
}
while(({ size_t dn_tmp_6=v_at;
size_t dn_tmp_7=({ dn_t_slice0_U8 dn_tmp_8=v_text;
(dn_tmp_8).len; });
(dn_tmp_6 < dn_tmp_7); })){
size_t v_end=({ dn_t_slice0_U8 dn_tmp_9=v_text;
size_t dn_tmp_10=v_at;
uint8_t dn_tmp_11=({ uint8_t dn_tmp_12=((uint8_t)(10ULL));
if((unsigned __int128)dn_tmp_12>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_12; });
dn_f_byte_find(dn_tmp_9,dn_tmp_10,dn_tmp_11); });
dn_t_slice0_U8 v_raw=({ dn_t_slice0_U8 const * dn_tmp_13=(&(v_text));
size_t dn_tmp_14=v_at;
size_t dn_tmp_15=({ size_t dn_tmp_16=v_end;
size_t dn_tmp_17=v_at;
dn_USize_sub(dn_tmp_16,dn_tmp_17); });
({ dn_range(dn_tmp_14,dn_tmp_15,(*(dn_tmp_13)).len);(dn_t_slice0_U8){(*(dn_tmp_13)).data?(*(dn_tmp_13)).data+dn_tmp_14:NULL,dn_tmp_15}; }); });
dn_t_slice0_U8 v_line=({ dn_t_slice0_U8 dn_tmp_18=v_raw;
dn_f_trimmed(dn_tmp_18); });
{ size_t dn_tmp_22=({ size_t dn_tmp_19=v_end;
size_t dn_tmp_20=((size_t)(1ULL));
dn_USize_add(dn_tmp_19,dn_tmp_20); });size_t *dn_tmp_21=&(v_at);*dn_tmp_21=dn_tmp_22; }
size_t v_equal=({ dn_t_slice0_U8 dn_tmp_23=v_line;
size_t dn_tmp_24=((size_t)(0ULL));
uint8_t dn_tmp_25=({ uint8_t dn_tmp_26=((uint8_t)(61ULL));
if((unsigned __int128)dn_tmp_26>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_26; });
dn_f_byte_find(dn_tmp_23,dn_tmp_24,dn_tmp_25); });
if(({ size_t dn_tmp_27=v_equal;
size_t dn_tmp_28=({ dn_t_slice0_U8 dn_tmp_29=v_line;
(dn_tmp_29).len; });
(dn_tmp_27 == dn_tmp_28); })){
continue;
}
else{
}
dn_t_slice0_U8 v_key=({ dn_t_slice0_U8 const * dn_tmp_30=(&(v_line));
size_t dn_tmp_31=((size_t)(0ULL));
size_t dn_tmp_32=v_equal;
({ dn_range(dn_tmp_31,dn_tmp_32,(*(dn_tmp_30)).len);(dn_t_slice0_U8){(*(dn_tmp_30)).data?(*(dn_tmp_30)).data+dn_tmp_31:NULL,dn_tmp_32}; }); });
dn_t_slice0_U8 v_value=({ dn_t_slice0_U8 const * dn_tmp_33=(&(v_line));
size_t dn_tmp_34=({ size_t dn_tmp_35=v_equal;
size_t dn_tmp_36=((size_t)(1ULL));
dn_USize_add(dn_tmp_35,dn_tmp_36); });
size_t dn_tmp_37=({ size_t dn_tmp_38=({ size_t dn_tmp_39=({ dn_t_slice0_U8 dn_tmp_40=v_line;
(dn_tmp_40).len; });
size_t dn_tmp_41=v_equal;
dn_USize_sub(dn_tmp_39,dn_tmp_41); });
size_t dn_tmp_42=((size_t)(1ULL));
dn_USize_sub(dn_tmp_38,dn_tmp_42); });
({ dn_range(dn_tmp_34,dn_tmp_37,(*(dn_tmp_33)).len);(dn_t_slice0_U8){(*(dn_tmp_33)).data?(*(dn_tmp_33)).data+dn_tmp_34:NULL,dn_tmp_37}; }); });
if((({ dn_t_slice0_U8 dn_tmp_43=v_key;
dn_t_slice0_U8 dn_tmp_44=({ dn_utf8 dn_tmp_45=((dn_utf8){(const uint8_t*)"Gene", 4});
((dn_t_slice0_U8){dn_tmp_45.data,dn_tmp_45.len}); });
dn_f_ascii_equal(dn_tmp_43,dn_tmp_44); }) && ({ size_t dn_tmp_46=({ dn_t_slice0_U8 dn_tmp_47=v_value;
(dn_tmp_47).len; });
size_t dn_tmp_48=((size_t)(0ULL));
(dn_tmp_46 > dn_tmp_48); }))){
dn_r_Gene v_gene=({ dn_f_gene_default(); });
if((!(({ dn_t_slice0_U8 dn_tmp_49=v_value;
dn_r_Gene * dn_tmp_50=(&(v_gene));
dn_f_gene_parse(dn_tmp_49,dn_tmp_50); })))){
{ bool dn_tmp_51=0;return dn_tmp_51; }
}
else{
}
{ uint64_t dn_tmp_54=({ dn_r_Gene const * dn_tmp_52=(&(v_gene));
dn_f_gene_hash(dn_tmp_52); });uint64_t *dn_tmp_53=&((v_v).f_method);*dn_tmp_53=dn_tmp_54; }
continue;
}
else{
}
if(({ dn_t_slice0_U8 dn_tmp_55=v_key;
dn_t_slice0_U8 dn_tmp_56=({ dn_utf8 dn_tmp_57=((dn_utf8){(const uint8_t*)"phase", 5});
((dn_t_slice0_U8){dn_tmp_57.data,dn_tmp_57.len}); });
dn_f_ascii_equal(dn_tmp_55,dn_tmp_56); })){
if(({ dn_t_slice0_U8 dn_tmp_58=v_value;
dn_t_slice0_U8 dn_tmp_59=({ dn_utf8 dn_tmp_60=((dn_utf8){(const uint8_t*)"driver_check", 12});
((dn_t_slice0_U8){dn_tmp_60.data,dn_tmp_60.len}); });
dn_f_bytes_equal(dn_tmp_58,dn_tmp_59); })){
{ uint32_t dn_tmp_62=((uint32_t)(5ULL));uint32_t *dn_tmp_61=&((v_v).f_phase);*dn_tmp_61=dn_tmp_62; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_63=v_value;
dn_t_slice0_U8 dn_tmp_64=({ dn_utf8 dn_tmp_65=((dn_utf8){(const uint8_t*)"baseline", 8});
((dn_t_slice0_U8){dn_tmp_65.data,dn_tmp_65.len}); });
dn_f_bytes_equal(dn_tmp_63,dn_tmp_64); })){
{ uint32_t dn_tmp_67=((uint32_t)(6ULL));uint32_t *dn_tmp_66=&((v_v).f_phase);*dn_tmp_66=dn_tmp_67; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_68=v_value;
dn_t_slice0_U8 dn_tmp_69=({ dn_utf8 dn_tmp_70=((dn_utf8){(const uint8_t*)"error", 5});
((dn_t_slice0_U8){dn_tmp_70.data,dn_tmp_70.len}); });
dn_f_bytes_equal(dn_tmp_68,dn_tmp_69); })){
{ uint32_t dn_tmp_72=((uint32_t)(4ULL));uint32_t *dn_tmp_71=&((v_v).f_phase);*dn_tmp_71=dn_tmp_72; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_73=v_value;
dn_t_slice0_U8 dn_tmp_74=({ dn_utf8 dn_tmp_75=((dn_utf8){(const uint8_t*)"completed", 9});
((dn_t_slice0_U8){dn_tmp_75.data,dn_tmp_75.len}); });
dn_f_bytes_equal(dn_tmp_73,dn_tmp_74); })){
{ uint32_t dn_tmp_77=((uint32_t)(3ULL));uint32_t *dn_tmp_76=&((v_v).f_phase);*dn_tmp_76=dn_tmp_77; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_78=v_value;
dn_t_slice0_U8 dn_tmp_79=({ dn_utf8 dn_tmp_80=((dn_utf8){(const uint8_t*)"stopped", 7});
((dn_t_slice0_U8){dn_tmp_80.data,dn_tmp_80.len}); });
dn_f_bytes_equal(dn_tmp_78,dn_tmp_79); })){
{ uint32_t dn_tmp_82=((uint32_t)(2ULL));uint32_t *dn_tmp_81=&((v_v).f_phase);*dn_tmp_81=dn_tmp_82; }
}
else{
{ uint32_t dn_tmp_84=((uint32_t)(1ULL));uint32_t *dn_tmp_83=&((v_v).f_phase);*dn_tmp_83=dn_tmp_84; }
}
}
}
}
}
continue;
}
else{
}
uint32_t v_n=((uint32_t)(0ULL));
bool v_known=((((((((((((((((((((((((((((({ dn_t_slice0_U8 dn_tmp_85=v_key;
dn_t_slice0_U8 dn_tmp_86=({ dn_utf8 dn_tmp_87=((dn_utf8){(const uint8_t*)"mwo_passed", 10});
((dn_t_slice0_U8){dn_tmp_87.data,dn_tmp_87.len}); });
dn_f_ascii_equal(dn_tmp_85,dn_tmp_86); }) || ({ dn_t_slice0_U8 dn_tmp_88=v_key;
dn_t_slice0_U8 dn_tmp_89=({ dn_utf8 dn_tmp_90=((dn_utf8){(const uint8_t*)"mwo_total", 9});
((dn_t_slice0_U8){dn_tmp_90.data,dn_tmp_90.len}); });
dn_f_ascii_equal(dn_tmp_88,dn_tmp_89); })) || ({ dn_t_slice0_U8 dn_tmp_91=v_key;
dn_t_slice0_U8 dn_tmp_92=({ dn_utf8 dn_tmp_93=((dn_utf8){(const uint8_t*)"MwoHttpPassed", 13});
((dn_t_slice0_U8){dn_tmp_93.data,dn_tmp_93.len}); });
dn_f_ascii_equal(dn_tmp_91,dn_tmp_92); })) || ({ dn_t_slice0_U8 dn_tmp_94=v_key;
dn_t_slice0_U8 dn_tmp_95=({ dn_utf8 dn_tmp_96=((dn_utf8){(const uint8_t*)"MwoHttpTotal", 12});
((dn_t_slice0_U8){dn_tmp_96.data,dn_tmp_96.len}); });
dn_f_ascii_equal(dn_tmp_94,dn_tmp_95); })) || ({ dn_t_slice0_U8 dn_tmp_97=v_key;
dn_t_slice0_U8 dn_tmp_98=({ dn_utf8 dn_tmp_99=((dn_utf8){(const uint8_t*)"target", 6});
((dn_t_slice0_U8){dn_tmp_99.data,dn_tmp_99.len}); });
dn_f_ascii_equal(dn_tmp_97,dn_tmp_98); })) || ({ dn_t_slice0_U8 dn_tmp_100=v_key;
dn_t_slice0_U8 dn_tmp_101=({ dn_utf8 dn_tmp_102=((dn_utf8){(const uint8_t*)"telegram_web_passed", 19});
((dn_t_slice0_U8){dn_tmp_102.data,dn_tmp_102.len}); });
dn_f_ascii_equal(dn_tmp_100,dn_tmp_101); })) || ({ dn_t_slice0_U8 dn_tmp_103=v_key;
dn_t_slice0_U8 dn_tmp_104=({ dn_utf8 dn_tmp_105=((dn_utf8){(const uint8_t*)"telegram_web_total", 18});
((dn_t_slice0_U8){dn_tmp_105.data,dn_tmp_105.len}); });
dn_f_ascii_equal(dn_tmp_103,dn_tmp_104); })) || ({ dn_t_slice0_U8 dn_tmp_106=v_key;
dn_t_slice0_U8 dn_tmp_107=({ dn_utf8 dn_tmp_108=((dn_utf8){(const uint8_t*)"telegram_desktop_passed", 23});
((dn_t_slice0_U8){dn_tmp_108.data,dn_tmp_108.len}); });
dn_f_ascii_equal(dn_tmp_106,dn_tmp_107); })) || ({ dn_t_slice0_U8 dn_tmp_109=v_key;
dn_t_slice0_U8 dn_tmp_110=({ dn_utf8 dn_tmp_111=((dn_utf8){(const uint8_t*)"telegram_desktop_total", 22});
((dn_t_slice0_U8){dn_tmp_111.data,dn_tmp_111.len}); });
dn_f_ascii_equal(dn_tmp_109,dn_tmp_110); })) || ({ dn_t_slice0_U8 dn_tmp_112=v_key;
dn_t_slice0_U8 dn_tmp_113=({ dn_utf8 dn_tmp_114=((dn_utf8){(const uint8_t*)"TelegramWebPassed", 17});
((dn_t_slice0_U8){dn_tmp_114.data,dn_tmp_114.len}); });
dn_f_ascii_equal(dn_tmp_112,dn_tmp_113); })) || ({ dn_t_slice0_U8 dn_tmp_115=v_key;
dn_t_slice0_U8 dn_tmp_116=({ dn_utf8 dn_tmp_117=((dn_utf8){(const uint8_t*)"TelegramWebTotal", 16});
((dn_t_slice0_U8){dn_tmp_117.data,dn_tmp_117.len}); });
dn_f_ascii_equal(dn_tmp_115,dn_tmp_116); })) || ({ dn_t_slice0_U8 dn_tmp_118=v_key;
dn_t_slice0_U8 dn_tmp_119=({ dn_utf8 dn_tmp_120=((dn_utf8){(const uint8_t*)"TelegramDesktopPassed", 21});
((dn_t_slice0_U8){dn_tmp_120.data,dn_tmp_120.len}); });
dn_f_ascii_equal(dn_tmp_118,dn_tmp_119); })) || ({ dn_t_slice0_U8 dn_tmp_121=v_key;
dn_t_slice0_U8 dn_tmp_122=({ dn_utf8 dn_tmp_123=((dn_utf8){(const uint8_t*)"TelegramDesktopTotal", 20});
((dn_t_slice0_U8){dn_tmp_123.data,dn_tmp_123.len}); });
dn_f_ascii_equal(dn_tmp_121,dn_tmp_122); })) || ({ dn_t_slice0_U8 dn_tmp_124=v_key;
dn_t_slice0_U8 dn_tmp_125=({ dn_utf8 dn_tmp_126=((dn_utf8){(const uint8_t*)"elapsed", 7});
((dn_t_slice0_U8){dn_tmp_126.data,dn_tmp_126.len}); });
dn_f_ascii_equal(dn_tmp_124,dn_tmp_125); })) || ({ dn_t_slice0_U8 dn_tmp_127=v_key;
dn_t_slice0_U8 dn_tmp_128=({ dn_utf8 dn_tmp_129=((dn_utf8){(const uint8_t*)"budget", 6});
((dn_t_slice0_U8){dn_tmp_129.data,dn_tmp_129.len}); });
dn_f_ascii_equal(dn_tmp_127,dn_tmp_128); })) || ({ dn_t_slice0_U8 dn_tmp_130=v_key;
dn_t_slice0_U8 dn_tmp_131=({ dn_utf8 dn_tmp_132=((dn_utf8){(const uint8_t*)"generation", 10});
((dn_t_slice0_U8){dn_tmp_132.data,dn_tmp_132.len}); });
dn_f_ascii_equal(dn_tmp_130,dn_tmp_131); })) || ({ dn_t_slice0_U8 dn_tmp_133=v_key;
dn_t_slice0_U8 dn_tmp_134=({ dn_utf8 dn_tmp_135=((dn_utf8){(const uint8_t*)"tested", 6});
((dn_t_slice0_U8){dn_tmp_135.data,dn_tmp_135.len}); });
dn_f_ascii_equal(dn_tmp_133,dn_tmp_134); })) || ({ dn_t_slice0_U8 dn_tmp_136=v_key;
dn_t_slice0_U8 dn_tmp_137=({ dn_utf8 dn_tmp_138=((dn_utf8){(const uint8_t*)"passed", 6});
((dn_t_slice0_U8){dn_tmp_138.data,dn_tmp_138.len}); });
dn_f_ascii_equal(dn_tmp_136,dn_tmp_137); })) || ({ dn_t_slice0_U8 dn_tmp_139=v_key;
dn_t_slice0_U8 dn_tmp_140=({ dn_utf8 dn_tmp_141=((dn_utf8){(const uint8_t*)"total", 5});
((dn_t_slice0_U8){dn_tmp_141.data,dn_tmp_141.len}); });
dn_f_ascii_equal(dn_tmp_139,dn_tmp_140); })) || ({ dn_t_slice0_U8 dn_tmp_142=v_key;
dn_t_slice0_U8 dn_tmp_143=({ dn_utf8 dn_tmp_144=((dn_utf8){(const uint8_t*)"controls_passed", 15});
((dn_t_slice0_U8){dn_tmp_144.data,dn_tmp_144.len}); });
dn_f_ascii_equal(dn_tmp_142,dn_tmp_143); })) || ({ dn_t_slice0_U8 dn_tmp_145=v_key;
dn_t_slice0_U8 dn_tmp_146=({ dn_utf8 dn_tmp_147=((dn_utf8){(const uint8_t*)"controls_total", 14});
((dn_t_slice0_U8){dn_tmp_147.data,dn_tmp_147.len}); });
dn_f_ascii_equal(dn_tmp_145,dn_tmp_146); })) || ({ dn_t_slice0_U8 dn_tmp_148=v_key;
dn_t_slice0_U8 dn_tmp_149=({ dn_utf8 dn_tmp_150=((dn_utf8){(const uint8_t*)"attributable", 12});
((dn_t_slice0_U8){dn_tmp_150.data,dn_tmp_150.len}); });
dn_f_ascii_equal(dn_tmp_148,dn_tmp_149); })) || ({ dn_t_slice0_U8 dn_tmp_151=v_key;
dn_t_slice0_U8 dn_tmp_152=({ dn_utf8 dn_tmp_153=((dn_utf8){(const uint8_t*)"validated", 9});
((dn_t_slice0_U8){dn_tmp_153.data,dn_tmp_153.len}); });
dn_f_ascii_equal(dn_tmp_151,dn_tmp_152); })) || ({ dn_t_slice0_U8 dn_tmp_154=v_key;
dn_t_slice0_U8 dn_tmp_155=({ dn_utf8 dn_tmp_156=((dn_utf8){(const uint8_t*)"media", 5});
((dn_t_slice0_U8){dn_tmp_156.data,dn_tmp_156.len}); });
dn_f_ascii_equal(dn_tmp_154,dn_tmp_155); })) || ({ dn_t_slice0_U8 dn_tmp_157=v_key;
dn_t_slice0_U8 dn_tmp_158=({ dn_utf8 dn_tmp_159=((dn_utf8){(const uint8_t*)"ControlsPassed", 14});
((dn_t_slice0_U8){dn_tmp_159.data,dn_tmp_159.len}); });
dn_f_ascii_equal(dn_tmp_157,dn_tmp_158); })) || ({ dn_t_slice0_U8 dn_tmp_160=v_key;
dn_t_slice0_U8 dn_tmp_161=({ dn_utf8 dn_tmp_162=((dn_utf8){(const uint8_t*)"ControlsTotal", 13});
((dn_t_slice0_U8){dn_tmp_162.data,dn_tmp_162.len}); });
dn_f_ascii_equal(dn_tmp_160,dn_tmp_161); })) || ({ dn_t_slice0_U8 dn_tmp_163=v_key;
dn_t_slice0_U8 dn_tmp_164=({ dn_utf8 dn_tmp_165=((dn_utf8){(const uint8_t*)"MediaTested", 11});
((dn_t_slice0_U8){dn_tmp_165.data,dn_tmp_165.len}); });
dn_f_ascii_equal(dn_tmp_163,dn_tmp_164); })) || ({ dn_t_slice0_U8 dn_tmp_166=v_key;
dn_t_slice0_U8 dn_tmp_167=({ dn_utf8 dn_tmp_168=((dn_utf8){(const uint8_t*)"browser", 7});
((dn_t_slice0_U8){dn_tmp_168.data,dn_tmp_168.len}); });
dn_f_ascii_equal(dn_tmp_166,dn_tmp_167); })) || ({ dn_t_slice0_U8 dn_tmp_169=v_key;
dn_t_slice0_U8 dn_tmp_170=({ dn_utf8 dn_tmp_171=((dn_utf8){(const uint8_t*)"BrowserTested", 13});
((dn_t_slice0_U8){dn_tmp_171.data,dn_tmp_171.len}); });
dn_f_ascii_equal(dn_tmp_169,dn_tmp_170); }));
if((!(v_known))){
continue;
}
else{
}
if((!(({ dn_t_slice0_U8 dn_tmp_172=v_value;
uint32_t dn_tmp_173=((uint32_t)(1000000000ULL));
uint32_t * dn_tmp_174=(&(v_n));
dn_f_decimal(dn_tmp_172,dn_tmp_173,dn_tmp_174); })))){
{ bool dn_tmp_175=0;return dn_tmp_175; }
}
else{
}
{ size_t dn_tmp_179=({ size_t dn_tmp_176=v_fields;
size_t dn_tmp_177=((size_t)(1ULL));
dn_USize_add(dn_tmp_176,dn_tmp_177); });size_t *dn_tmp_178=&(v_fields);*dn_tmp_178=dn_tmp_179; }
if((({ dn_t_slice0_U8 dn_tmp_180=v_key;
dn_t_slice0_U8 dn_tmp_181=({ dn_utf8 dn_tmp_182=((dn_utf8){(const uint8_t*)"mwo_passed", 10});
((dn_t_slice0_U8){dn_tmp_182.data,dn_tmp_182.len}); });
dn_f_ascii_equal(dn_tmp_180,dn_tmp_181); }) || ({ dn_t_slice0_U8 dn_tmp_183=v_key;
dn_t_slice0_U8 dn_tmp_184=({ dn_utf8 dn_tmp_185=((dn_utf8){(const uint8_t*)"MwoHttpPassed", 13});
((dn_t_slice0_U8){dn_tmp_185.data,dn_tmp_185.len}); });
dn_f_ascii_equal(dn_tmp_183,dn_tmp_184); }))){
{ uint32_t dn_tmp_187=v_n;uint32_t *dn_tmp_186=&((v_v).f_mwo_passed);*dn_tmp_186=dn_tmp_187; }
}
else{
}
if((({ dn_t_slice0_U8 dn_tmp_188=v_key;
dn_t_slice0_U8 dn_tmp_189=({ dn_utf8 dn_tmp_190=((dn_utf8){(const uint8_t*)"mwo_total", 9});
((dn_t_slice0_U8){dn_tmp_190.data,dn_tmp_190.len}); });
dn_f_ascii_equal(dn_tmp_188,dn_tmp_189); }) || ({ dn_t_slice0_U8 dn_tmp_191=v_key;
dn_t_slice0_U8 dn_tmp_192=({ dn_utf8 dn_tmp_193=((dn_utf8){(const uint8_t*)"MwoHttpTotal", 12});
((dn_t_slice0_U8){dn_tmp_193.data,dn_tmp_193.len}); });
dn_f_ascii_equal(dn_tmp_191,dn_tmp_192); }))){
{ uint32_t dn_tmp_195=v_n;uint32_t *dn_tmp_194=&((v_v).f_mwo_total);*dn_tmp_194=dn_tmp_195; }
}
else{
}
if(({ dn_t_slice0_U8 dn_tmp_196=v_key;
dn_t_slice0_U8 dn_tmp_197=({ dn_utf8 dn_tmp_198=((dn_utf8){(const uint8_t*)"target", 6});
((dn_t_slice0_U8){dn_tmp_198.data,dn_tmp_198.len}); });
dn_f_ascii_equal(dn_tmp_196,dn_tmp_197); })){
{ uint32_t dn_tmp_200=v_n;uint32_t *dn_tmp_199=&((v_v).f_target);*dn_tmp_199=dn_tmp_200; }
}
else{
}
if((({ dn_t_slice0_U8 dn_tmp_201=v_key;
dn_t_slice0_U8 dn_tmp_202=({ dn_utf8 dn_tmp_203=((dn_utf8){(const uint8_t*)"telegram_web_passed", 19});
((dn_t_slice0_U8){dn_tmp_203.data,dn_tmp_203.len}); });
dn_f_ascii_equal(dn_tmp_201,dn_tmp_202); }) || ({ dn_t_slice0_U8 dn_tmp_204=v_key;
dn_t_slice0_U8 dn_tmp_205=({ dn_utf8 dn_tmp_206=((dn_utf8){(const uint8_t*)"TelegramWebPassed", 17});
((dn_t_slice0_U8){dn_tmp_206.data,dn_tmp_206.len}); });
dn_f_ascii_equal(dn_tmp_204,dn_tmp_205); }))){
{ uint32_t dn_tmp_208=v_n;uint32_t *dn_tmp_207=&((v_v).f_telegram_web_passed);*dn_tmp_207=dn_tmp_208; }
}
else{
}
if((({ dn_t_slice0_U8 dn_tmp_209=v_key;
dn_t_slice0_U8 dn_tmp_210=({ dn_utf8 dn_tmp_211=((dn_utf8){(const uint8_t*)"telegram_web_total", 18});
((dn_t_slice0_U8){dn_tmp_211.data,dn_tmp_211.len}); });
dn_f_ascii_equal(dn_tmp_209,dn_tmp_210); }) || ({ dn_t_slice0_U8 dn_tmp_212=v_key;
dn_t_slice0_U8 dn_tmp_213=({ dn_utf8 dn_tmp_214=((dn_utf8){(const uint8_t*)"TelegramWebTotal", 16});
((dn_t_slice0_U8){dn_tmp_214.data,dn_tmp_214.len}); });
dn_f_ascii_equal(dn_tmp_212,dn_tmp_213); }))){
{ uint32_t dn_tmp_216=v_n;uint32_t *dn_tmp_215=&((v_v).f_telegram_web_total);*dn_tmp_215=dn_tmp_216; }
}
else{
}
if((({ dn_t_slice0_U8 dn_tmp_217=v_key;
dn_t_slice0_U8 dn_tmp_218=({ dn_utf8 dn_tmp_219=((dn_utf8){(const uint8_t*)"telegram_desktop_passed", 23});
((dn_t_slice0_U8){dn_tmp_219.data,dn_tmp_219.len}); });
dn_f_ascii_equal(dn_tmp_217,dn_tmp_218); }) || ({ dn_t_slice0_U8 dn_tmp_220=v_key;
dn_t_slice0_U8 dn_tmp_221=({ dn_utf8 dn_tmp_222=((dn_utf8){(const uint8_t*)"TelegramDesktopPassed", 21});
((dn_t_slice0_U8){dn_tmp_222.data,dn_tmp_222.len}); });
dn_f_ascii_equal(dn_tmp_220,dn_tmp_221); }))){
{ uint32_t dn_tmp_224=v_n;uint32_t *dn_tmp_223=&((v_v).f_telegram_desktop_passed);*dn_tmp_223=dn_tmp_224; }
}
else{
}
if((({ dn_t_slice0_U8 dn_tmp_225=v_key;
dn_t_slice0_U8 dn_tmp_226=({ dn_utf8 dn_tmp_227=((dn_utf8){(const uint8_t*)"telegram_desktop_total", 22});
((dn_t_slice0_U8){dn_tmp_227.data,dn_tmp_227.len}); });
dn_f_ascii_equal(dn_tmp_225,dn_tmp_226); }) || ({ dn_t_slice0_U8 dn_tmp_228=v_key;
dn_t_slice0_U8 dn_tmp_229=({ dn_utf8 dn_tmp_230=((dn_utf8){(const uint8_t*)"TelegramDesktopTotal", 20});
((dn_t_slice0_U8){dn_tmp_230.data,dn_tmp_230.len}); });
dn_f_ascii_equal(dn_tmp_228,dn_tmp_229); }))){
{ uint32_t dn_tmp_232=v_n;uint32_t *dn_tmp_231=&((v_v).f_telegram_desktop_total);*dn_tmp_231=dn_tmp_232; }
}
else{
}
if(({ dn_t_slice0_U8 dn_tmp_233=v_key;
dn_t_slice0_U8 dn_tmp_234=({ dn_utf8 dn_tmp_235=((dn_utf8){(const uint8_t*)"elapsed", 7});
((dn_t_slice0_U8){dn_tmp_235.data,dn_tmp_235.len}); });
dn_f_ascii_equal(dn_tmp_233,dn_tmp_234); })){
{ uint32_t dn_tmp_237=v_n;uint32_t *dn_tmp_236=&((v_v).f_elapsed);*dn_tmp_236=dn_tmp_237; }
}
else{
}
if(({ dn_t_slice0_U8 dn_tmp_238=v_key;
dn_t_slice0_U8 dn_tmp_239=({ dn_utf8 dn_tmp_240=((dn_utf8){(const uint8_t*)"budget", 6});
((dn_t_slice0_U8){dn_tmp_240.data,dn_tmp_240.len}); });
dn_f_ascii_equal(dn_tmp_238,dn_tmp_239); })){
{ uint32_t dn_tmp_242=v_n;uint32_t *dn_tmp_241=&((v_v).f_budget);*dn_tmp_241=dn_tmp_242; }
}
else{
}
if(({ dn_t_slice0_U8 dn_tmp_243=v_key;
dn_t_slice0_U8 dn_tmp_244=({ dn_utf8 dn_tmp_245=((dn_utf8){(const uint8_t*)"generation", 10});
((dn_t_slice0_U8){dn_tmp_245.data,dn_tmp_245.len}); });
dn_f_ascii_equal(dn_tmp_243,dn_tmp_244); })){
{ uint32_t dn_tmp_247=v_n;uint32_t *dn_tmp_246=&((v_v).f_generation);*dn_tmp_246=dn_tmp_247; }
}
else{
}
if(({ dn_t_slice0_U8 dn_tmp_248=v_key;
dn_t_slice0_U8 dn_tmp_249=({ dn_utf8 dn_tmp_250=((dn_utf8){(const uint8_t*)"tested", 6});
((dn_t_slice0_U8){dn_tmp_250.data,dn_tmp_250.len}); });
dn_f_ascii_equal(dn_tmp_248,dn_tmp_249); })){
{ uint32_t dn_tmp_252=v_n;uint32_t *dn_tmp_251=&((v_v).f_tested);*dn_tmp_251=dn_tmp_252; }
}
else{
}
if(({ dn_t_slice0_U8 dn_tmp_253=v_key;
dn_t_slice0_U8 dn_tmp_254=({ dn_utf8 dn_tmp_255=((dn_utf8){(const uint8_t*)"passed", 6});
((dn_t_slice0_U8){dn_tmp_255.data,dn_tmp_255.len}); });
dn_f_ascii_equal(dn_tmp_253,dn_tmp_254); })){
{ uint32_t dn_tmp_257=v_n;uint32_t *dn_tmp_256=&((v_v).f_passed);*dn_tmp_256=dn_tmp_257; }
}
else{
}
if(({ dn_t_slice0_U8 dn_tmp_258=v_key;
dn_t_slice0_U8 dn_tmp_259=({ dn_utf8 dn_tmp_260=((dn_utf8){(const uint8_t*)"total", 5});
((dn_t_slice0_U8){dn_tmp_260.data,dn_tmp_260.len}); });
dn_f_ascii_equal(dn_tmp_258,dn_tmp_259); })){
{ uint32_t dn_tmp_262=v_n;uint32_t *dn_tmp_261=&((v_v).f_total);*dn_tmp_261=dn_tmp_262; }
}
else{
}
if((({ dn_t_slice0_U8 dn_tmp_263=v_key;
dn_t_slice0_U8 dn_tmp_264=({ dn_utf8 dn_tmp_265=((dn_utf8){(const uint8_t*)"controls_passed", 15});
((dn_t_slice0_U8){dn_tmp_265.data,dn_tmp_265.len}); });
dn_f_ascii_equal(dn_tmp_263,dn_tmp_264); }) || ({ dn_t_slice0_U8 dn_tmp_266=v_key;
dn_t_slice0_U8 dn_tmp_267=({ dn_utf8 dn_tmp_268=((dn_utf8){(const uint8_t*)"ControlsPassed", 14});
((dn_t_slice0_U8){dn_tmp_268.data,dn_tmp_268.len}); });
dn_f_ascii_equal(dn_tmp_266,dn_tmp_267); }))){
{ uint32_t dn_tmp_270=v_n;uint32_t *dn_tmp_269=&((v_v).f_controls_passed);*dn_tmp_269=dn_tmp_270; }
}
else{
}
if((({ dn_t_slice0_U8 dn_tmp_271=v_key;
dn_t_slice0_U8 dn_tmp_272=({ dn_utf8 dn_tmp_273=((dn_utf8){(const uint8_t*)"controls_total", 14});
((dn_t_slice0_U8){dn_tmp_273.data,dn_tmp_273.len}); });
dn_f_ascii_equal(dn_tmp_271,dn_tmp_272); }) || ({ dn_t_slice0_U8 dn_tmp_274=v_key;
dn_t_slice0_U8 dn_tmp_275=({ dn_utf8 dn_tmp_276=((dn_utf8){(const uint8_t*)"ControlsTotal", 13});
((dn_t_slice0_U8){dn_tmp_276.data,dn_tmp_276.len}); });
dn_f_ascii_equal(dn_tmp_274,dn_tmp_275); }))){
{ uint32_t dn_tmp_278=v_n;uint32_t *dn_tmp_277=&((v_v).f_controls_total);*dn_tmp_277=dn_tmp_278; }
}
else{
}
if((({ dn_t_slice0_U8 dn_tmp_279=v_key;
dn_t_slice0_U8 dn_tmp_280=({ dn_utf8 dn_tmp_281=((dn_utf8){(const uint8_t*)"browser", 7});
((dn_t_slice0_U8){dn_tmp_281.data,dn_tmp_281.len}); });
dn_f_ascii_equal(dn_tmp_279,dn_tmp_280); }) || ({ dn_t_slice0_U8 dn_tmp_282=v_key;
dn_t_slice0_U8 dn_tmp_283=({ dn_utf8 dn_tmp_284=((dn_utf8){(const uint8_t*)"BrowserTested", 13});
((dn_t_slice0_U8){dn_tmp_284.data,dn_tmp_284.len}); });
dn_f_ascii_equal(dn_tmp_282,dn_tmp_283); }))){
{ uint32_t dn_tmp_286=v_n;uint32_t *dn_tmp_285=&((v_v).f_browser);*dn_tmp_285=dn_tmp_286; }
}
else{
}
if(({ dn_t_slice0_U8 dn_tmp_287=v_key;
dn_t_slice0_U8 dn_tmp_288=({ dn_utf8 dn_tmp_289=((dn_utf8){(const uint8_t*)"attributable", 12});
((dn_t_slice0_U8){dn_tmp_289.data,dn_tmp_289.len}); });
dn_f_ascii_equal(dn_tmp_287,dn_tmp_288); })){
{ uint32_t dn_tmp_291=v_n;uint32_t *dn_tmp_290=&((v_v).f_attributable);*dn_tmp_290=dn_tmp_291; }
}
else{
}
if(({ dn_t_slice0_U8 dn_tmp_292=v_key;
dn_t_slice0_U8 dn_tmp_293=({ dn_utf8 dn_tmp_294=((dn_utf8){(const uint8_t*)"validated", 9});
((dn_t_slice0_U8){dn_tmp_294.data,dn_tmp_294.len}); });
dn_f_ascii_equal(dn_tmp_292,dn_tmp_293); })){
{ uint32_t dn_tmp_296=v_n;uint32_t *dn_tmp_295=&((v_v).f_validated);*dn_tmp_295=dn_tmp_296; }
}
else{
}
if((({ dn_t_slice0_U8 dn_tmp_297=v_key;
dn_t_slice0_U8 dn_tmp_298=({ dn_utf8 dn_tmp_299=((dn_utf8){(const uint8_t*)"media", 5});
((dn_t_slice0_U8){dn_tmp_299.data,dn_tmp_299.len}); });
dn_f_ascii_equal(dn_tmp_297,dn_tmp_298); }) || ({ dn_t_slice0_U8 dn_tmp_300=v_key;
dn_t_slice0_U8 dn_tmp_301=({ dn_utf8 dn_tmp_302=((dn_utf8){(const uint8_t*)"MediaTested", 11});
((dn_t_slice0_U8){dn_tmp_302.data,dn_tmp_302.len}); });
dn_f_ascii_equal(dn_tmp_300,dn_tmp_301); }))){
{ uint32_t dn_tmp_304=v_n;uint32_t *dn_tmp_303=&((v_v).f_media);*dn_tmp_303=dn_tmp_304; }
}
else{
}
}
if((((((((((({ size_t dn_tmp_305=v_fields;
size_t dn_tmp_306=((size_t)(0ULL));
(dn_tmp_305 == dn_tmp_306); }) || ({ uint32_t dn_tmp_307=(v_v).f_passed;
uint32_t dn_tmp_308=(v_v).f_total;
(dn_tmp_307 > dn_tmp_308); })) || ({ uint32_t dn_tmp_309=(v_v).f_controls_passed;
uint32_t dn_tmp_310=(v_v).f_controls_total;
(dn_tmp_309 > dn_tmp_310); })) || ({ uint32_t dn_tmp_311=(v_v).f_validated;
uint32_t dn_tmp_312=({ uint32_t dn_tmp_313=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_313>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_313; });
(dn_tmp_311 > dn_tmp_312); })) || ({ uint32_t dn_tmp_314=(v_v).f_media;
uint32_t dn_tmp_315=({ uint32_t dn_tmp_316=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_316>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_316; });
(dn_tmp_314 > dn_tmp_315); })) || ({ uint32_t dn_tmp_317=(v_v).f_browser;
uint32_t dn_tmp_318=({ uint32_t dn_tmp_319=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_319>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_319; });
(dn_tmp_317 > dn_tmp_318); })) || ({ uint32_t dn_tmp_320=(v_v).f_target;
uint32_t dn_tmp_321=({ uint32_t dn_tmp_322=((uint32_t)(6ULL));
if((unsigned __int128)dn_tmp_322>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_322; });
(dn_tmp_320 > dn_tmp_321); })) || ({ uint32_t dn_tmp_323=(v_v).f_mwo_passed;
uint32_t dn_tmp_324=(v_v).f_mwo_total;
(dn_tmp_323 > dn_tmp_324); })) || ({ uint32_t dn_tmp_325=(v_v).f_telegram_web_passed;
uint32_t dn_tmp_326=(v_v).f_telegram_web_total;
(dn_tmp_325 > dn_tmp_326); })) || ({ uint32_t dn_tmp_327=(v_v).f_telegram_desktop_passed;
uint32_t dn_tmp_328=(v_v).f_telegram_desktop_total;
(dn_tmp_327 > dn_tmp_328); }))){
{ bool dn_tmp_329=0;return dn_tmp_329; }
}
else{
}
{ dn_r_TrainingView dn_tmp_331=v_v;dn_r_TrainingView *dn_tmp_330=&((*((dn_r_TrainingView *)dn_nonnull((void*)(v_view)))));*dn_tmp_330=dn_tmp_331; }
{ bool dn_tmp_332=1;return dn_tmp_332; }
}

dn_fail("missing native return");
}
static bool dn_cb_training_view(dn_t_slice0_U8 v_text,dn_r_TrainingView * v_view) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_training_view(v_text,v_view);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_buf_U8 dn_f_training_caption(dn_r_TrainingView const * v_v,bool v_history) {
{
dn_t_buf_U8 volatile v_b={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_b=({ size_t dn_tmp_2=((size_t)(0ULL));
dn_new_buf_U8(dn_tmp_2); });
if(v_history){
(void)(({ dn_t_buf_U8 * dn_tmp_3=(&(v_b));
dn_t_slice0_U8 dn_tmp_4=({ dn_utf8 dn_tmp_5=((dn_utf8){(const uint8_t*)"\320\236\321\206\320\265\320\275\320\272\320\260 \320\270\320\267 \321\204\320\260\320\271\320\273\320\260: ", 30});
((dn_t_slice0_U8){dn_tmp_5.data,dn_tmp_5.len}); });
dn_f_bytes_append(dn_tmp_3,dn_tmp_4); }));
}
else{
if(({ uint32_t dn_tmp_6=(v_v)->f_target;
uint32_t dn_tmp_7=({ uint32_t dn_tmp_8=((uint32_t)(5ULL));
if((unsigned __int128)dn_tmp_8>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_8; });
(dn_tmp_6 == dn_tmp_7); })){
(void)(({ dn_t_buf_U8 * dn_tmp_9=(&(v_b));
dn_t_slice0_U8 dn_tmp_10=({ dn_utf8 dn_tmp_11=((dn_utf8){(const uint8_t*)"HTTPS MWO/CDN: ", 15});
((dn_t_slice0_U8){dn_tmp_11.data,dn_tmp_11.len}); });
dn_f_bytes_append(dn_tmp_9,dn_tmp_10); }));
}
else{
if(((({ uint32_t dn_tmp_12=(v_v)->f_target;
uint32_t dn_tmp_13=({ uint32_t dn_tmp_14=((uint32_t)(3ULL));
if((unsigned __int128)dn_tmp_14>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_14; });
(dn_tmp_12 == dn_tmp_13); }) || ({ uint32_t dn_tmp_15=(v_v)->f_target;
uint32_t dn_tmp_16=({ uint32_t dn_tmp_17=((uint32_t)(4ULL));
if((unsigned __int128)dn_tmp_17>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_17; });
(dn_tmp_15 == dn_tmp_16); })) || ({ uint32_t dn_tmp_18=(v_v)->f_target;
uint32_t dn_tmp_19=({ uint32_t dn_tmp_20=((uint32_t)(6ULL));
if((unsigned __int128)dn_tmp_20>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_20; });
(dn_tmp_18 == dn_tmp_19); }))){
(void)(({ dn_t_buf_U8 * dn_tmp_21=(&(v_b));
dn_t_slice0_U8 dn_tmp_22=({ dn_utf8 dn_tmp_23=((dn_utf8){(const uint8_t*)"HTTPS + MTProto TCP/WSS: ", 25});
((dn_t_slice0_U8){dn_tmp_23.data,dn_tmp_23.len}); });
dn_f_bytes_append(dn_tmp_21,dn_tmp_22); }));
}
else{
if(({ uint32_t dn_tmp_24=(v_v)->f_browser;
uint32_t dn_tmp_25=({ uint32_t dn_tmp_26=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_26>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_26; });
(dn_tmp_24 == dn_tmp_25); })){
(void)(({ dn_t_buf_U8 * dn_tmp_27=(&(v_b));
dn_t_slice0_U8 dn_tmp_28=({ dn_utf8 dn_tmp_29=((dn_utf8){(const uint8_t*)"HTTP + Chromium: ", 17});
((dn_t_slice0_U8){dn_tmp_29.data,dn_tmp_29.len}); });
dn_f_bytes_append(dn_tmp_27,dn_tmp_28); }));
}
else{
(void)(({ dn_t_buf_U8 * dn_tmp_30=(&(v_b));
dn_t_slice0_U8 dn_tmp_31=({ dn_utf8 dn_tmp_32=((dn_utf8){(const uint8_t*)"HTTP-\320\277\321\200\320\276\320\261\321\213: ", 17});
((dn_t_slice0_U8){dn_tmp_32.data,dn_tmp_32.len}); });
dn_f_bytes_append(dn_tmp_30,dn_tmp_31); }));
}
}
}
}
if((({ uint32_t dn_tmp_33=(v_v)->f_total;
uint32_t dn_tmp_34=({ uint32_t dn_tmp_35=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_35>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_35; });
(dn_tmp_33 == dn_tmp_34); }) || ({ uint32_t dn_tmp_36=(v_v)->f_attributable;
uint32_t dn_tmp_37=({ uint32_t dn_tmp_38=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_38>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_38; });
(dn_tmp_36 == dn_tmp_37); }))){
(void)(({ dn_t_buf_U8 * dn_tmp_39=(&(v_b));
dn_t_slice0_U8 dn_tmp_40=({ dn_utf8 dn_tmp_41=((dn_utf8){(const uint8_t*)"\320\275\320\265\321\202 \320\277\320\276\320\264\321\202\320\262\320\265\321\200\320\266\320\264\321\221\320\275\320\275\320\276\320\263\320\276 \321\200\320\265\320\267\321\203\320\273\321\214\321\202\320\260\321\202\320\260", 58});
((dn_t_slice0_U8){dn_tmp_41.data,dn_tmp_41.len}); });
dn_f_bytes_append(dn_tmp_39,dn_tmp_40); }));
}
else{
(void)(({ dn_t_buf_U8 * dn_tmp_42=(&(v_b));
uint64_t dn_tmp_43=({ uint32_t dn_tmp_44=(v_v)->f_passed;
if((unsigned __int128)dn_tmp_44>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_44; });
dn_f_append_number(dn_tmp_42,dn_tmp_43); }));
(void)(({ dn_t_buf_U8 * dn_tmp_45=(&(v_b));
uint8_t dn_tmp_46=({ uint8_t dn_tmp_47=((uint8_t)(47ULL));
if((unsigned __int128)dn_tmp_47>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_47; });
dn_push_buf_U8(dn_tmp_45,dn_tmp_46); }));
(void)(({ dn_t_buf_U8 * dn_tmp_48=(&(v_b));
uint64_t dn_tmp_49=({ uint32_t dn_tmp_50=(v_v)->f_total;
if((unsigned __int128)dn_tmp_50>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_50; });
dn_f_append_number(dn_tmp_48,dn_tmp_49); }));
(void)(({ dn_t_buf_U8 * dn_tmp_51=(&(v_b));
dn_t_slice0_U8 dn_tmp_52=({ dn_utf8 dn_tmp_53=((dn_utf8){(const uint8_t*)" (", 2});
((dn_t_slice0_U8){dn_tmp_53.data,dn_tmp_53.len}); });
dn_f_bytes_append(dn_tmp_51,dn_tmp_52); }));
(void)(({ dn_t_buf_U8 * dn_tmp_54=(&(v_b));
uint64_t dn_tmp_55=({ uint64_t dn_tmp_56=({ uint64_t dn_tmp_57=({ uint32_t dn_tmp_58=(v_v)->f_passed;
if((unsigned __int128)dn_tmp_58>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_58; });
uint64_t dn_tmp_59=({ uint64_t dn_tmp_60=((uint64_t)(100ULL));
if((unsigned __int128)dn_tmp_60>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_60; });
dn_U64_mul(dn_tmp_57,dn_tmp_59); });
uint64_t dn_tmp_61=({ uint32_t dn_tmp_62=(v_v)->f_total;
if((unsigned __int128)dn_tmp_62>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_62; });
dn_U64_div(dn_tmp_56,dn_tmp_61); });
dn_f_append_number(dn_tmp_54,dn_tmp_55); }));
(void)(({ dn_t_buf_U8 * dn_tmp_63=(&(v_b));
dn_t_slice0_U8 dn_tmp_64=({ dn_utf8 dn_tmp_65=((dn_utf8){(const uint8_t*)"%)", 2});
((dn_t_slice0_U8){dn_tmp_65.data,dn_tmp_65.len}); });
dn_f_bytes_append(dn_tmp_63,dn_tmp_64); }));
}
if(({ uint64_t dn_tmp_66=(v_v)->f_method;
uint64_t dn_tmp_67=({ uint64_t dn_tmp_68=((uint64_t)(0ULL));
if((unsigned __int128)dn_tmp_68>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_68; });
(dn_tmp_66 != dn_tmp_67); })){
(void)(({ dn_t_buf_U8 * dn_tmp_69=(&(v_b));
dn_t_slice0_U8 dn_tmp_70=({ dn_utf8 dn_tmp_71=((dn_utf8){(const uint8_t*)" \342\200\242 \320\274\320\265\321\202\320\276\320\264 ", 16});
((dn_t_slice0_U8){dn_tmp_71.data,dn_tmp_71.len}); });
dn_f_bytes_append(dn_tmp_69,dn_tmp_70); }));
(void)(({ dn_t_buf_U8 * dn_tmp_72=(&(v_b));
uint64_t dn_tmp_73=({ uint64_t dn_tmp_74=(v_v)->f_method;
uint64_t dn_tmp_75=({ uint64_t dn_tmp_76=((uint64_t)(1000000ULL));
if((unsigned __int128)dn_tmp_76>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_76; });
dn_U64_mod(dn_tmp_74,dn_tmp_75); });
dn_f_append_number(dn_tmp_72,dn_tmp_73); }));
}
else{
}
(void)(({ dn_t_buf_U8 * dn_tmp_77=(&(v_b));
dn_t_slice0_U8 dn_tmp_78=({ dn_utf8 dn_tmp_79=((dn_utf8){(const uint8_t*)" \342\200\242 \320\262\320\260\321\200\320\270\320\260\320\275\321\202\320\276\320\262 ", 24});
((dn_t_slice0_U8){dn_tmp_79.data,dn_tmp_79.len}); });
dn_f_bytes_append(dn_tmp_77,dn_tmp_78); }));
(void)(({ dn_t_buf_U8 * dn_tmp_80=(&(v_b));
uint64_t dn_tmp_81=({ uint32_t dn_tmp_82=(v_v)->f_tested;
if((unsigned __int128)dn_tmp_82>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_82; });
dn_f_append_number(dn_tmp_80,dn_tmp_81); }));
(void)(({ dn_t_buf_U8 * dn_tmp_83=(&(v_b));
dn_t_slice0_U8 dn_tmp_84=({ dn_utf8 dn_tmp_85=((dn_utf8){(const uint8_t*)" \342\200\242 \320\277\320\276\320\272\320\276\320\273\320\265\320\275\320\270\320\265 ", 24});
((dn_t_slice0_U8){dn_tmp_85.data,dn_tmp_85.len}); });
dn_f_bytes_append(dn_tmp_83,dn_tmp_84); }));
(void)(({ dn_t_buf_U8 * dn_tmp_86=(&(v_b));
uint64_t dn_tmp_87=({ uint32_t dn_tmp_88=(v_v)->f_generation;
if((unsigned __int128)dn_tmp_88>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_88; });
dn_f_append_number(dn_tmp_86,dn_tmp_87); }));
if(({ uint32_t dn_tmp_89=(v_v)->f_validated;
uint32_t dn_tmp_90=({ uint32_t dn_tmp_91=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_91>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_91; });
(dn_tmp_89 == dn_tmp_90); })){
(void)(({ dn_t_buf_U8 * dn_tmp_92=(&(v_b));
dn_t_slice0_U8 dn_tmp_93=({ dn_utf8 dn_tmp_94=((dn_utf8){(const uint8_t*)"\012\320\225\321\201\321\202\321\214 \321\203\320\273\321\203\321\207\321\210\320\265\320\275\320\270\320\265 \320\275\320\260 \320\277\320\276\320\262\321\202\320\276\321\200\320\275\320\276\320\271 \320\277\321\200\320\276\320\262\320\265\321\200\320\272\320\265 \320\277\321\200\320\276\321\202\320\276\320\272\320\276\320\273\320\276\320\262.", 91});
((dn_t_slice0_U8){dn_tmp_94.data,dn_tmp_94.len}); });
dn_f_bytes_append(dn_tmp_92,dn_tmp_93); }));
}
else{
(void)(({ dn_t_buf_U8 * dn_tmp_95=(&(v_b));
dn_t_slice0_U8 dn_tmp_96=({ dn_utf8 dn_tmp_97=((dn_utf8){(const uint8_t*)"\012\320\243\320\273\321\203\321\207\321\210\320\265\320\275\320\270\320\265 \320\276\321\202\320\275\320\276\321\201\320\270\321\202\320\265\320\273\321\214\320\275\320\276 \320\272\320\276\320\275\321\202\321\200\320\276\320\273\321\217 \320\277\320\276\320\272\320\260 \320\275\320\265 \320\277\320\276\320\264\321\202\320\262\320\265\321\200\320\266\320\264\320\265\320\275\320\276.", 101});
((dn_t_slice0_U8){dn_tmp_97.data,dn_tmp_97.len}); });
dn_f_bytes_append(dn_tmp_95,dn_tmp_96); }));
}
{ dn_t_buf_U8 dn_tmp_99=({ dn_t_buf_U8 dn_tmp_98=v_b;v_b=(dn_t_buf_U8){0};dn_adopt_buf_U8(&dn_tmp_98);dn_tmp_98; });dn_adopt_buf_U8(&dn_tmp_99);dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_99; }
dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_t_buf_U8 dn_cb_training_caption(dn_r_TrainingView const * v_v,bool v_history) {
 if(!dn_enter()){return (dn_t_buf_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_buf_U8){0};}
 dn_t_buf_U8 dn_result=dn_f_training_caption(v_v,v_history);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_buf_U8 dn_f_training_clock(dn_r_TrainingView const * v_v) {
{
dn_t_buf_U8 volatile v_b={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_b=({ dn_t_slice0_U8 dn_tmp_2=({ dn_utf8 dn_tmp_3=((dn_utf8){(const uint8_t*)"\320\237\320\276\320\270\321\201\320\272: ", 12});
((dn_t_slice0_U8){dn_tmp_3.data,dn_tmp_3.len}); });
dn_f_bytes_copy(dn_tmp_2); });
(void)(({ dn_t_buf_U8 * dn_tmp_4=(&(v_b));
uint64_t dn_tmp_5=({ uint32_t dn_tmp_6=({ uint32_t dn_tmp_7=(v_v)->f_elapsed;
uint32_t dn_tmp_8=({ uint32_t dn_tmp_9=((uint32_t)(60ULL));
if((unsigned __int128)dn_tmp_9>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_9; });
dn_U32_div(dn_tmp_7,dn_tmp_8); });
if((unsigned __int128)dn_tmp_6>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_6; });
dn_f_append_number(dn_tmp_4,dn_tmp_5); }));
(void)(({ dn_t_buf_U8 * dn_tmp_10=(&(v_b));
uint8_t dn_tmp_11=({ uint8_t dn_tmp_12=((uint8_t)(58ULL));
if((unsigned __int128)dn_tmp_12>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_12; });
dn_push_buf_U8(dn_tmp_10,dn_tmp_11); }));
if(({ uint32_t dn_tmp_13=({ uint32_t dn_tmp_14=(v_v)->f_elapsed;
uint32_t dn_tmp_15=({ uint32_t dn_tmp_16=((uint32_t)(60ULL));
if((unsigned __int128)dn_tmp_16>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_16; });
dn_U32_mod(dn_tmp_14,dn_tmp_15); });
uint32_t dn_tmp_17=({ uint32_t dn_tmp_18=((uint32_t)(10ULL));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_18; });
(dn_tmp_13 < dn_tmp_17); })){
(void)(({ dn_t_buf_U8 * dn_tmp_19=(&(v_b));
uint8_t dn_tmp_20=({ uint8_t dn_tmp_21=((uint8_t)(48ULL));
if((unsigned __int128)dn_tmp_21>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_21; });
dn_push_buf_U8(dn_tmp_19,dn_tmp_20); }));
}
else{
}
(void)(({ dn_t_buf_U8 * dn_tmp_22=(&(v_b));
uint64_t dn_tmp_23=({ uint32_t dn_tmp_24=({ uint32_t dn_tmp_25=(v_v)->f_elapsed;
uint32_t dn_tmp_26=({ uint32_t dn_tmp_27=((uint32_t)(60ULL));
if((unsigned __int128)dn_tmp_27>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_27; });
dn_U32_mod(dn_tmp_25,dn_tmp_26); });
if((unsigned __int128)dn_tmp_24>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_24; });
dn_f_append_number(dn_tmp_22,dn_tmp_23); }));
(void)(({ dn_t_buf_U8 * dn_tmp_28=(&(v_b));
dn_t_slice0_U8 dn_tmp_29=({ dn_utf8 dn_tmp_30=((dn_utf8){(const uint8_t*)" / ", 3});
((dn_t_slice0_U8){dn_tmp_30.data,dn_tmp_30.len}); });
dn_f_bytes_append(dn_tmp_28,dn_tmp_29); }));
(void)(({ dn_t_buf_U8 * dn_tmp_31=(&(v_b));
uint64_t dn_tmp_32=({ uint32_t dn_tmp_33=({ uint32_t dn_tmp_34=(v_v)->f_budget;
uint32_t dn_tmp_35=({ uint32_t dn_tmp_36=((uint32_t)(60ULL));
if((unsigned __int128)dn_tmp_36>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_36; });
dn_U32_div(dn_tmp_34,dn_tmp_35); });
if((unsigned __int128)dn_tmp_33>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_33; });
dn_f_append_number(dn_tmp_31,dn_tmp_32); }));
(void)(({ dn_t_buf_U8 * dn_tmp_37=(&(v_b));
dn_t_slice0_U8 dn_tmp_38=({ dn_utf8 dn_tmp_39=((dn_utf8){(const uint8_t*)" \320\274\320\270\320\275 \342\200\242 \320\272\320\276\320\275\321\202\321\200\320\276\320\273\321\214 ", 29});
((dn_t_slice0_U8){dn_tmp_39.data,dn_tmp_39.len}); });
dn_f_bytes_append(dn_tmp_37,dn_tmp_38); }));
(void)(({ dn_t_buf_U8 * dn_tmp_40=(&(v_b));
uint64_t dn_tmp_41=({ uint32_t dn_tmp_42=(v_v)->f_controls_passed;
if((unsigned __int128)dn_tmp_42>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_42; });
dn_f_append_number(dn_tmp_40,dn_tmp_41); }));
(void)(({ dn_t_buf_U8 * dn_tmp_43=(&(v_b));
uint8_t dn_tmp_44=({ uint8_t dn_tmp_45=((uint8_t)(47ULL));
if((unsigned __int128)dn_tmp_45>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_45; });
dn_push_buf_U8(dn_tmp_43,dn_tmp_44); }));
(void)(({ dn_t_buf_U8 * dn_tmp_46=(&(v_b));
uint64_t dn_tmp_47=({ uint32_t dn_tmp_48=(v_v)->f_controls_total;
if((unsigned __int128)dn_tmp_48>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_48; });
dn_f_append_number(dn_tmp_46,dn_tmp_47); }));
if((({ uint32_t dn_tmp_49=(v_v)->f_telegram_web_total;
uint32_t dn_tmp_50=({ uint32_t dn_tmp_51=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_51>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_51; });
(dn_tmp_49 > dn_tmp_50); }) || ({ uint32_t dn_tmp_52=(v_v)->f_telegram_desktop_total;
uint32_t dn_tmp_53=({ uint32_t dn_tmp_54=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_54>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_54; });
(dn_tmp_52 > dn_tmp_53); }))){
(void)(({ dn_t_buf_U8 * dn_tmp_55=(&(v_b));
dn_t_slice0_U8 dn_tmp_56=({ dn_utf8 dn_tmp_57=((dn_utf8){(const uint8_t*)" \342\200\242 TG Web ", 12});
((dn_t_slice0_U8){dn_tmp_57.data,dn_tmp_57.len}); });
dn_f_bytes_append(dn_tmp_55,dn_tmp_56); }));
(void)(({ dn_t_buf_U8 * dn_tmp_58=(&(v_b));
uint64_t dn_tmp_59=({ uint32_t dn_tmp_60=(v_v)->f_telegram_web_passed;
if((unsigned __int128)dn_tmp_60>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_60; });
dn_f_append_number(dn_tmp_58,dn_tmp_59); }));
(void)(({ dn_t_buf_U8 * dn_tmp_61=(&(v_b));
uint8_t dn_tmp_62=({ uint8_t dn_tmp_63=((uint8_t)(47ULL));
if((unsigned __int128)dn_tmp_63>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_63; });
dn_push_buf_U8(dn_tmp_61,dn_tmp_62); }));
(void)(({ dn_t_buf_U8 * dn_tmp_64=(&(v_b));
uint64_t dn_tmp_65=({ uint32_t dn_tmp_66=(v_v)->f_telegram_web_total;
if((unsigned __int128)dn_tmp_66>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_66; });
dn_f_append_number(dn_tmp_64,dn_tmp_65); }));
(void)(({ dn_t_buf_U8 * dn_tmp_67=(&(v_b));
dn_t_slice0_U8 dn_tmp_68=({ dn_utf8 dn_tmp_69=((dn_utf8){(const uint8_t*)" \342\200\242 Desktop ", 13});
((dn_t_slice0_U8){dn_tmp_69.data,dn_tmp_69.len}); });
dn_f_bytes_append(dn_tmp_67,dn_tmp_68); }));
(void)(({ dn_t_buf_U8 * dn_tmp_70=(&(v_b));
uint64_t dn_tmp_71=({ uint32_t dn_tmp_72=(v_v)->f_telegram_desktop_passed;
if((unsigned __int128)dn_tmp_72>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_72; });
dn_f_append_number(dn_tmp_70,dn_tmp_71); }));
(void)(({ dn_t_buf_U8 * dn_tmp_73=(&(v_b));
uint8_t dn_tmp_74=({ uint8_t dn_tmp_75=((uint8_t)(47ULL));
if((unsigned __int128)dn_tmp_75>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_75; });
dn_push_buf_U8(dn_tmp_73,dn_tmp_74); }));
(void)(({ dn_t_buf_U8 * dn_tmp_76=(&(v_b));
uint64_t dn_tmp_77=({ uint32_t dn_tmp_78=(v_v)->f_telegram_desktop_total;
if((unsigned __int128)dn_tmp_78>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_78; });
dn_f_append_number(dn_tmp_76,dn_tmp_77); }));
}
else{
if((({ uint32_t dn_tmp_79=(v_v)->f_media;
uint32_t dn_tmp_80=({ uint32_t dn_tmp_81=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_81>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_81; });
(dn_tmp_79 == dn_tmp_80); }) && ({ uint32_t dn_tmp_82=(v_v)->f_mwo_total;
uint32_t dn_tmp_83=({ uint32_t dn_tmp_84=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_84>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_84; });
(dn_tmp_82 == dn_tmp_83); }))){
(void)(({ dn_t_buf_U8 * dn_tmp_85=(&(v_b));
dn_t_slice0_U8 dn_tmp_86=({ dn_utf8 dn_tmp_87=((dn_utf8){(const uint8_t*)" \342\200\242 \320\262\320\270\320\264\320\265\320\276 \320\275\320\265 \320\277\321\200\320\276\320\262\320\265\321\200\320\265\320\275\320\276", 39});
((dn_t_slice0_U8){dn_tmp_87.data,dn_tmp_87.len}); });
dn_f_bytes_append(dn_tmp_85,dn_tmp_86); }));
}
else{
if(({ uint32_t dn_tmp_88=(v_v)->f_media;
uint32_t dn_tmp_89=({ uint32_t dn_tmp_90=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_90>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_90; });
(dn_tmp_88 == dn_tmp_89); })){
(void)(({ dn_t_buf_U8 * dn_tmp_91=(&(v_b));
dn_t_slice0_U8 dn_tmp_92=({ dn_utf8 dn_tmp_93=((dn_utf8){(const uint8_t*)" \342\200\242 \320\265\321\201\321\202\321\214 \320\277\321\200\320\276\320\261\320\260 \320\262\320\270\320\264\320\265\320\276-CDN", 39});
((dn_t_slice0_U8){dn_tmp_93.data,dn_tmp_93.len}); });
dn_f_bytes_append(dn_tmp_91,dn_tmp_92); }));
}
else{
}
}
}
if(({ uint32_t dn_tmp_94=(v_v)->f_mwo_total;
uint32_t dn_tmp_95=({ uint32_t dn_tmp_96=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_96>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_96; });
(dn_tmp_94 > dn_tmp_95); })){
(void)(({ dn_t_buf_U8 * dn_tmp_97=(&(v_b));
dn_t_slice0_U8 dn_tmp_98=({ dn_utf8 dn_tmp_99=((dn_utf8){(const uint8_t*)" \342\200\242 MWO HTTPS ", 15});
((dn_t_slice0_U8){dn_tmp_99.data,dn_tmp_99.len}); });
dn_f_bytes_append(dn_tmp_97,dn_tmp_98); }));
(void)(({ dn_t_buf_U8 * dn_tmp_100=(&(v_b));
uint64_t dn_tmp_101=({ uint32_t dn_tmp_102=(v_v)->f_mwo_passed;
if((unsigned __int128)dn_tmp_102>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_102; });
dn_f_append_number(dn_tmp_100,dn_tmp_101); }));
(void)(({ dn_t_buf_U8 * dn_tmp_103=(&(v_b));
uint8_t dn_tmp_104=({ uint8_t dn_tmp_105=((uint8_t)(47ULL));
if((unsigned __int128)dn_tmp_105>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_105; });
dn_push_buf_U8(dn_tmp_103,dn_tmp_104); }));
(void)(({ dn_t_buf_U8 * dn_tmp_106=(&(v_b));
uint64_t dn_tmp_107=({ uint32_t dn_tmp_108=(v_v)->f_mwo_total;
if((unsigned __int128)dn_tmp_108>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_108; });
dn_f_append_number(dn_tmp_106,dn_tmp_107); }));
(void)(({ dn_t_buf_U8 * dn_tmp_109=(&(v_b));
dn_t_slice0_U8 dn_tmp_110=({ dn_utf8 dn_tmp_111=((dn_utf8){(const uint8_t*)" \342\200\242 \320\262\321\205\320\276\320\264 \320\262 \320\270\320\263\321\200\320\265 \320\262\321\200\321\203\321\207\320\275\321\203\321\216", 40});
((dn_t_slice0_U8){dn_tmp_111.data,dn_tmp_111.len}); });
dn_f_bytes_append(dn_tmp_109,dn_tmp_110); }));
}
else{
}
{ dn_t_buf_U8 dn_tmp_113=({ dn_t_buf_U8 dn_tmp_112=v_b;v_b=(dn_t_buf_U8){0};dn_adopt_buf_U8(&dn_tmp_112);dn_tmp_112; });dn_adopt_buf_U8(&dn_tmp_113);dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_113; }
dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_t_buf_U8 dn_cb_training_clock(dn_r_TrainingView const * v_v) {
 if(!dn_enter()){return (dn_t_buf_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_buf_U8){0};}
 dn_t_buf_U8 dn_result=dn_f_training_clock(v_v);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_r_StartupView dn_f_startup_view(dn_t_slice0_U8 v_text) {
{
dn_r_StartupView v_v=({ ((dn_r_StartupView){0}); });
size_t v_at=((size_t)(0ULL));
while(({ size_t dn_tmp_0=v_at;
size_t dn_tmp_1=({ dn_t_slice0_U8 dn_tmp_2=v_text;
(dn_tmp_2).len; });
(dn_tmp_0 < dn_tmp_1); })){
size_t v_end=({ dn_t_slice0_U8 dn_tmp_3=v_text;
size_t dn_tmp_4=v_at;
uint8_t dn_tmp_5=((uint8_t)(10ULL));
dn_f_byte_find(dn_tmp_3,dn_tmp_4,dn_tmp_5); });
dn_t_slice0_U8 v_raw=({ dn_t_slice0_U8 const * dn_tmp_6=(&(v_text));
size_t dn_tmp_7=v_at;
size_t dn_tmp_8=({ size_t dn_tmp_9=v_end;
size_t dn_tmp_10=v_at;
dn_USize_sub(dn_tmp_9,dn_tmp_10); });
({ dn_range(dn_tmp_7,dn_tmp_8,(*(dn_tmp_6)).len);(dn_t_slice0_U8){(*(dn_tmp_6)).data?(*(dn_tmp_6)).data+dn_tmp_7:NULL,dn_tmp_8}; }); });
dn_t_slice0_U8 v_line=({ dn_t_slice0_U8 dn_tmp_11=v_raw;
dn_f_trimmed(dn_tmp_11); });
{ size_t dn_tmp_15=({ size_t dn_tmp_12=v_end;
size_t dn_tmp_13=((size_t)(1ULL));
dn_USize_add(dn_tmp_12,dn_tmp_13); });size_t *dn_tmp_14=&(v_at);*dn_tmp_14=dn_tmp_15; }
size_t v_eq=({ dn_t_slice0_U8 dn_tmp_16=v_line;
size_t dn_tmp_17=((size_t)(0ULL));
uint8_t dn_tmp_18=((uint8_t)(61ULL));
dn_f_byte_find(dn_tmp_16,dn_tmp_17,dn_tmp_18); });
if(({ size_t dn_tmp_19=v_eq;
size_t dn_tmp_20=({ dn_t_slice0_U8 dn_tmp_21=v_line;
(dn_tmp_21).len; });
(dn_tmp_19 == dn_tmp_20); })){
continue;
}
else{
}
dn_t_slice0_U8 v_key=({ dn_t_slice0_U8 const * dn_tmp_22=(&(v_line));
size_t dn_tmp_23=((size_t)(0ULL));
size_t dn_tmp_24=v_eq;
({ dn_range(dn_tmp_23,dn_tmp_24,(*(dn_tmp_22)).len);(dn_t_slice0_U8){(*(dn_tmp_22)).data?(*(dn_tmp_22)).data+dn_tmp_23:NULL,dn_tmp_24}; }); });
dn_t_slice0_U8 v_value=({ dn_t_slice0_U8 const * dn_tmp_25=(&(v_line));
size_t dn_tmp_26=({ size_t dn_tmp_27=v_eq;
size_t dn_tmp_28=((size_t)(1ULL));
dn_USize_add(dn_tmp_27,dn_tmp_28); });
size_t dn_tmp_29=({ size_t dn_tmp_30=({ size_t dn_tmp_31=({ dn_t_slice0_U8 dn_tmp_32=v_line;
(dn_tmp_32).len; });
size_t dn_tmp_33=v_eq;
dn_USize_sub(dn_tmp_31,dn_tmp_33); });
size_t dn_tmp_34=((size_t)(1ULL));
dn_USize_sub(dn_tmp_30,dn_tmp_34); });
({ dn_range(dn_tmp_26,dn_tmp_29,(*(dn_tmp_25)).len);(dn_t_slice0_U8){(*(dn_tmp_25)).data?(*(dn_tmp_25)).data+dn_tmp_26:NULL,dn_tmp_29}; }); });
uint32_t v_n=((uint32_t)(0ULL));
if(({ dn_t_slice0_U8 dn_tmp_35=v_key;
dn_t_slice0_U8 dn_tmp_36=({ dn_utf8 dn_tmp_37=((dn_utf8){(const uint8_t*)"component", 9});
((dn_t_slice0_U8){dn_tmp_37.data,dn_tmp_37.len}); });
dn_f_bytes_equal(dn_tmp_35,dn_tmp_36); })){
{ bool dn_tmp_43=({ dn_t_slice0_U8 dn_tmp_38=v_value;
size_t dn_tmp_39=((size_t)(0ULL));
dn_t_slice0_U8 dn_tmp_40=({ dn_utf8 dn_tmp_41=((dn_utf8){(const uint8_t*)"WinDivert", 9});
((dn_t_slice0_U8){dn_tmp_41.data,dn_tmp_41.len}); });
dn_f_bytes_at(dn_tmp_38,dn_tmp_39,dn_tmp_40); });bool *dn_tmp_42=&((v_v).f_driver);*dn_tmp_42=dn_tmp_43; }
}
else{
}
if(({ dn_t_slice0_U8 dn_tmp_44=v_key;
dn_t_slice0_U8 dn_tmp_45=({ dn_utf8 dn_tmp_46=((dn_utf8){(const uint8_t*)"win32_error", 11});
((dn_t_slice0_U8){dn_tmp_46.data,dn_tmp_46.len}); });
dn_f_bytes_equal(dn_tmp_44,dn_tmp_45); })){
if((!(({ dn_t_slice0_U8 dn_tmp_47=v_value;
uint32_t dn_tmp_48=((uint32_t)(4294967295ULL));
uint32_t * dn_tmp_49=(&(v_n));
dn_f_decimal(dn_tmp_47,dn_tmp_48,dn_tmp_49); })))){
{ dn_r_StartupView dn_tmp_50=({ ((dn_r_StartupView){0}); });return dn_tmp_50; }
}
else{
}
{ uint32_t dn_tmp_52=v_n;uint32_t *dn_tmp_51=&((v_v).f_error);*dn_tmp_51=dn_tmp_52; }
{ bool dn_tmp_54=1;bool *dn_tmp_53=&((v_v).f_found);*dn_tmp_53=dn_tmp_54; }
}
else{
}
if(({ dn_t_slice0_U8 dn_tmp_55=v_key;
dn_t_slice0_U8 dn_tmp_56=({ dn_utf8 dn_tmp_57=((dn_utf8){(const uint8_t*)"ready", 5});
((dn_t_slice0_U8){dn_tmp_57.data,dn_tmp_57.len}); });
dn_f_bytes_equal(dn_tmp_55,dn_tmp_56); })){
if((!(({ dn_t_slice0_U8 dn_tmp_58=v_value;
uint32_t dn_tmp_59=((uint32_t)(1ULL));
uint32_t * dn_tmp_60=(&(v_n));
dn_f_decimal(dn_tmp_58,dn_tmp_59,dn_tmp_60); })))){
{ dn_r_StartupView dn_tmp_61=({ ((dn_r_StartupView){0}); });return dn_tmp_61; }
}
else{
}
{ bool dn_tmp_66=({ uint32_t dn_tmp_62=v_n;
uint32_t dn_tmp_63=({ uint32_t dn_tmp_64=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_64>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_64; });
(dn_tmp_62 == dn_tmp_63); });bool *dn_tmp_65=&((v_v).f_ready);*dn_tmp_65=dn_tmp_66; }
}
else{
}
}
{ dn_r_StartupView dn_tmp_67=v_v;return dn_tmp_67; }
}

dn_fail("missing native return");
}
static dn_r_StartupView dn_cb_startup_view(dn_t_slice0_U8 v_text) {
 if(!dn_enter()){return (dn_r_StartupView){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_StartupView){0};}
 dn_r_StartupView dn_result=dn_f_startup_view(v_text);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_buf_U8 dn_f_startup_caption(uint32_t v_code) {
{
dn_t_buf_U8 volatile v_b={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_b=({ dn_t_slice0_U8 dn_tmp_2=({ dn_utf8 dn_tmp_3=((dn_utf8){(const uint8_t*)"WinDivert: \320\276\321\210\320\270\320\261\320\272\320\260 ", 24});
((dn_t_slice0_U8){dn_tmp_3.data,dn_tmp_3.len}); });
dn_f_bytes_copy(dn_tmp_2); });
(void)(({ dn_t_buf_U8 * dn_tmp_4=(&(v_b));
uint64_t dn_tmp_5=({ uint32_t dn_tmp_6=v_code;
if((unsigned __int128)dn_tmp_6>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_6; });
dn_f_append_number(dn_tmp_4,dn_tmp_5); }));
if(({ uint32_t dn_tmp_7=v_code;
uint32_t dn_tmp_8=({ uint32_t dn_tmp_9=((uint32_t)(5ULL));
if((unsigned __int128)dn_tmp_9>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_9; });
(dn_tmp_7 == dn_tmp_8); })){
(void)(({ dn_t_buf_U8 * dn_tmp_10=(&(v_b));
dn_t_slice0_U8 dn_tmp_11=({ dn_utf8 dn_tmp_12=((dn_utf8){(const uint8_t*)". \320\235\321\203\320\266\320\265\320\275 \320\267\320\260\320\277\321\203\321\201\320\272 \321\201 \320\277\321\200\320\260\320\262\320\260\320\274\320\270 \320\260\320\264\320\274\320\270\320\275\320\270\321\201\321\202\321\200\320\260\321\202\320\276\321\200\320\260.", 73});
((dn_t_slice0_U8){dn_tmp_12.data,dn_tmp_12.len}); });
dn_f_bytes_append(dn_tmp_10,dn_tmp_11); }));
}
else{
if(({ uint32_t dn_tmp_13=v_code;
uint32_t dn_tmp_14=({ uint32_t dn_tmp_15=((uint32_t)(58ULL));
if((unsigned __int128)dn_tmp_15>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_15; });
(dn_tmp_13 == dn_tmp_14); })){
(void)(({ dn_t_buf_U8 * dn_tmp_16=(&(v_b));
dn_t_slice0_U8 dn_tmp_17=({ dn_utf8 dn_tmp_18=((dn_utf8){(const uint8_t*)" \320\277\321\200\320\270 \320\276\321\202\320\272\321\200\321\213\321\202\320\270\320\270 \320\264\321\200\320\260\320\271\320\262\320\265\321\200\320\260. \320\255\321\202\320\276 \321\201\320\261\320\276\320\271 \320\267\320\260\320\277\321\203\321\201\320\272\320\260 \320\277\320\265\321\200\320\265\321\205\320\262\320\260\321\202\320\260, \320\260 \320\275\320\265 \320\277\321\200\320\276\320\262\320\265\321\200\320\272\320\260 \320\264\320\276\321\201\321\202\321\203\320\277\320\275\320\276\321\201\321\202\320\270 \321\201\320\260\320\271\321\202\320\260. \320\227\320\260\320\277\321\203\321\201\321\202\320\270\321\202\320\265 Check-Driver.cmd \320\270 \321\201\320\276\320\261\320\265\321\200\320\270\321\202\320\265 \320\276\321\202\321\207\321\221\321\202.", 221});
((dn_t_slice0_U8){dn_tmp_18.data,dn_tmp_18.len}); });
dn_f_bytes_append(dn_tmp_16,dn_tmp_17); }));
}
else{
if((({ uint32_t dn_tmp_19=v_code;
uint32_t dn_tmp_20=({ uint32_t dn_tmp_21=((uint32_t)(577ULL));
if((unsigned __int128)dn_tmp_21>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_21; });
(dn_tmp_19 == dn_tmp_20); }) || ({ uint32_t dn_tmp_22=v_code;
uint32_t dn_tmp_23=({ uint32_t dn_tmp_24=((uint32_t)(1275ULL));
if((unsigned __int128)dn_tmp_24>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_24; });
(dn_tmp_22 == dn_tmp_23); }))){
(void)(({ dn_t_buf_U8 * dn_tmp_25=(&(v_b));
dn_t_slice0_U8 dn_tmp_26=({ dn_utf8 dn_tmp_27=((dn_utf8){(const uint8_t*)". Windows \320\275\320\265 \321\200\320\260\320\267\321\200\320\265\321\210\320\270\320\273\320\260 \320\267\320\260\320\263\321\200\321\203\320\267\320\270\321\202\321\214 \320\264\321\200\320\260\320\271\320\262\320\265\321\200. \320\237\320\276\320\264\321\200\320\276\320\261\320\275\320\276\321\201\321\202\320\270 \342\200\224 \320\262 Check-Driver.cmd.", 116});
((dn_t_slice0_U8){dn_tmp_27.data,dn_tmp_27.len}); });
dn_f_bytes_append(dn_tmp_25,dn_tmp_26); }));
}
else{
if(({ uint32_t dn_tmp_28=v_code;
uint32_t dn_tmp_29=({ uint32_t dn_tmp_30=((uint32_t)(654ULL));
if((unsigned __int128)dn_tmp_30>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_30; });
(dn_tmp_28 == dn_tmp_29); })){
(void)(({ dn_t_buf_U8 * dn_tmp_31=(&(v_b));
dn_t_slice0_U8 dn_tmp_32=({ dn_utf8 dn_tmp_33=((dn_utf8){(const uint8_t*)". \320\227\320\260\320\263\321\200\321\203\320\266\320\265\320\275\320\260 \320\275\320\265\321\201\320\276\320\262\320\274\320\265\321\201\321\202\320\270\320\274\320\260\321\217 \320\262\320\265\321\200\321\201\320\270\321\217 \320\264\321\200\320\260\320\271\320\262\320\265\321\200\320\260. \320\227\320\260\320\272\321\200\320\276\320\271\321\202\320\265 \320\264\321\200\321\203\320\263\320\270\320\265 DPI-\320\277\321\200\320\276\320\263\321\200\320\260\320\274\320\274\321\213 \320\270 \320\277\320\265\321\200\320\265\320\267\320\260\320\277\321\203\321\201\321\202\320\270\321\202\320\265 Windows.", 170});
((dn_t_slice0_U8){dn_tmp_33.data,dn_tmp_33.len}); });
dn_f_bytes_append(dn_tmp_31,dn_tmp_32); }));
}
else{
if(({ uint32_t dn_tmp_34=v_code;
uint32_t dn_tmp_35=({ uint32_t dn_tmp_36=((uint32_t)(1753ULL));
if((unsigned __int128)dn_tmp_36>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_36; });
(dn_tmp_34 == dn_tmp_35); })){
(void)(({ dn_t_buf_U8 * dn_tmp_37=(&(v_b));
dn_t_slice0_U8 dn_tmp_38=({ dn_utf8 dn_tmp_39=((dn_utf8){(const uint8_t*)". \320\235\320\265\320\264\320\276\321\201\321\202\321\203\320\277\320\275\320\260 \321\201\320\273\321\203\320\266\320\261\320\260 \321\204\320\270\320\273\321\214\321\202\321\200\320\260\321\206\320\270\320\270 BFE; \321\201\320\274. Check-Driver.cmd.", 85});
((dn_t_slice0_U8){dn_tmp_39.data,dn_tmp_39.len}); });
dn_f_bytes_append(dn_tmp_37,dn_tmp_38); }));
}
else{
(void)(({ dn_t_buf_U8 * dn_tmp_40=(&(v_b));
dn_t_slice0_U8 dn_tmp_41=({ dn_utf8 dn_tmp_42=((dn_utf8){(const uint8_t*)". \320\241\320\274. Check-Driver.cmd \320\270 \320\266\321\203\321\200\320\275\320\260\320\273 \320\267\320\260\320\277\321\203\321\201\320\272\320\260.", 56});
((dn_t_slice0_U8){dn_tmp_42.data,dn_tmp_42.len}); });
dn_f_bytes_append(dn_tmp_40,dn_tmp_41); }));
}
}
}
}
}
{ dn_t_buf_U8 dn_tmp_44=({ dn_t_buf_U8 dn_tmp_43=v_b;v_b=(dn_t_buf_U8){0};dn_adopt_buf_U8(&dn_tmp_43);dn_tmp_43; });dn_adopt_buf_U8(&dn_tmp_44);dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_44; }
dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_t_buf_U8 dn_cb_startup_caption(uint32_t v_code) {
 if(!dn_enter()){return (dn_t_buf_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_buf_U8){0};}
 dn_t_buf_U8 dn_result=dn_f_startup_caption(v_code);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_delete_object(void * * v_value) {
{
(void)(({ void * dn_tmp_0=(*((void * *)dn_nonnull((void*)(v_value))));
({ int32_t dn_foreign=dn_ext_DeleteObject(dn_tmp_0);dn_fp_default();dn_foreign; }); }));
}

return 0;
}
static void dn_cb_delete_object(void * * v_value) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_delete_object(v_value);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static void * dn_f_control(dn_r_Gui const * v_g,size_t v_id) {
{
{ size_t dn_tmp_2=((size_t)(0ULL));size_t dn_tmp_1=({ dn_t_buf_r7_Control const * dn_tmp_0=(&((v_g)->f_controls));
(*(dn_tmp_0)).len; });for(size_t v_i=dn_tmp_2;v_i<dn_tmp_1;v_i++){
if(({ size_t dn_tmp_3=((*({ dn_t_buf_r7_Control volatile *dn_tmp_4=&((v_g)->f_controls);size_t dn_tmp_5=v_i;&dn_tmp_4->data[dn_index(dn_tmp_5,dn_tmp_4->len)]; }))).f_id;
size_t dn_tmp_6=v_id;
(dn_tmp_3 == dn_tmp_6); })){
{ void * dn_tmp_9=((*({ dn_t_buf_r7_Control volatile *dn_tmp_7=&((v_g)->f_controls);size_t dn_tmp_8=v_i;&dn_tmp_7->data[dn_index(dn_tmp_8,dn_tmp_7->len)]; }))).f_window;return dn_tmp_9; }
}
else{
}
}
 }
{ void * dn_tmp_10=({ ((void *){0}); });return dn_tmp_10; }
}

dn_fail("missing native return");
}
static void * dn_cb_control(dn_r_Gui const * v_g,size_t v_id) {
 if(!dn_enter()){return (void *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (void *){0};}
 void * dn_result=dn_f_control(v_g,v_id);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint32_t dn_f_rgb(uint32_t v_red,uint32_t v_green,uint32_t v_blue) {
{
{ uint32_t dn_tmp_10=({ uint32_t dn_tmp_0=({ uint32_t dn_tmp_1=v_red;
uint32_t dn_tmp_2=({ uint32_t dn_tmp_3=v_green;
uint32_t dn_tmp_4=({ uint32_t dn_tmp_5=((uint32_t)(8ULL));
if((unsigned __int128)dn_tmp_5>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_5; });
dn_U32_shl(dn_tmp_3,dn_tmp_4); });
dn_U32_bits((uint64_t)dn_tmp_1 | (uint64_t)dn_tmp_2); });
uint32_t dn_tmp_6=({ uint32_t dn_tmp_7=v_blue;
uint32_t dn_tmp_8=({ uint32_t dn_tmp_9=((uint32_t)(16ULL));
if((unsigned __int128)dn_tmp_9>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_9; });
dn_U32_shl(dn_tmp_7,dn_tmp_8); });
dn_U32_bits((uint64_t)dn_tmp_0 | (uint64_t)dn_tmp_6); });return dn_tmp_10; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_rgb(uint32_t v_red,uint32_t v_green,uint32_t v_blue) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_rgb(v_red,v_green,v_blue);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_ui_log(dn_r_Gui * v_g,dn_t_slice0_U8 v_text) {
{
{ dn_t_buf_U8 dn_tmp_2=({ dn_t_slice0_U8 dn_tmp_0=v_text;
dn_f_bytes_copy(dn_tmp_0); });dn_adopt_buf_U8(&dn_tmp_2);dn_t_buf_U8 volatile *dn_tmp_1=&((v_g)->f_note);dn_drop_buf_U8(dn_tmp_1);dn_retain_buf_U8(&dn_tmp_2);*dn_tmp_1=dn_tmp_2; }
(void)(({ void * dn_tmp_3=(v_g)->f_window;
dn_r_Rect * dn_tmp_4=({ ((dn_r_Rect *)NULL); });
int32_t dn_tmp_5=((int32_t)(1ULL));
({ int32_t dn_foreign=dn_ext_InvalidateRect(dn_tmp_3,dn_tmp_4,dn_tmp_5);dn_fp_default();dn_foreign; }); }));
}

return 0;
}
static void dn_cb_ui_log(dn_r_Gui * v_g,dn_t_slice0_U8 v_text) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_ui_log(v_g,v_text);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static int32_t dn_f_scale(dn_r_Gui const * v_g,int32_t v_value) {
{
{ int32_t dn_tmp_3=({ int32_t dn_tmp_0=v_value;
int32_t dn_tmp_1=(v_g)->f_dpi;
int32_t dn_tmp_2=((int32_t)(96ULL));
({ int32_t dn_foreign=dn_ext_MulDiv(dn_tmp_0,dn_tmp_1,dn_tmp_2);dn_fp_default();dn_foreign; }); });return dn_tmp_3; }
}

dn_fail("missing native return");
}
static int32_t dn_cb_scale(dn_r_Gui const * v_g,int32_t v_value) {
 if(!dn_enter()){return (int32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (int32_t){0};}
 int32_t dn_result=dn_f_scale(v_g,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static int32_t dn_f_width_at(dn_r_Gui const * v_g,int32_t v_value) {
{
dn_r_Rect v_rect=({ ((dn_r_Rect){0}); });
(void)(({ void * dn_tmp_0=(v_g)->f_window;
dn_r_Rect * dn_tmp_1=({ dn_r_Rect * dn_tmp_2=({ dn_r_Rect * dn_tmp_3=(&(v_rect));
((dn_r_Rect *)dn_tmp_3); });
((dn_r_Rect *)dn_tmp_2); });
({ int32_t dn_foreign=dn_ext_GetClientRect(dn_tmp_0,dn_tmp_1);dn_fp_default();dn_foreign; }); }));
{ int32_t dn_tmp_7=({ int32_t dn_tmp_4=v_value;
int32_t dn_tmp_5=(v_rect).f_right;
int32_t dn_tmp_6=((int32_t)(1000ULL));
({ int32_t dn_foreign=dn_ext_MulDiv(dn_tmp_4,dn_tmp_5,dn_tmp_6);dn_fp_default();dn_foreign; }); });return dn_tmp_7; }
}

dn_fail("missing native return");
}
static int32_t dn_cb_width_at(dn_r_Gui const * v_g,int32_t v_value) {
 if(!dn_enter()){return (int32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (int32_t){0};}
 int32_t dn_result=dn_f_width_at(v_g,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_rounded(void * v_dc,dn_r_Rect v_rect,void * v_brush,int32_t v_radius) {
{
void * v_old_brush=({ void * dn_tmp_0=v_dc;
void * dn_tmp_1=v_brush;
({ void * dn_foreign=dn_ext_SelectObject(dn_tmp_0,dn_tmp_1);dn_fp_default();dn_foreign; }); });
void * v_old_pen=({ void * dn_tmp_2=v_dc;
void * dn_tmp_3=({ int32_t dn_tmp_4=((int32_t)(8ULL));
({ void * dn_foreign=dn_ext_GetStockObject(dn_tmp_4);dn_fp_default();dn_foreign; }); });
({ void * dn_foreign=dn_ext_SelectObject(dn_tmp_2,dn_tmp_3);dn_fp_default();dn_foreign; }); });
(void)(({ void * dn_tmp_5=v_dc;
int32_t dn_tmp_6=(v_rect).f_left;
int32_t dn_tmp_7=(v_rect).f_top;
int32_t dn_tmp_8=(v_rect).f_right;
int32_t dn_tmp_9=(v_rect).f_bottom;
int32_t dn_tmp_10=v_radius;
int32_t dn_tmp_11=v_radius;
({ int32_t dn_foreign=dn_ext_RoundRect(dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8,dn_tmp_9,dn_tmp_10,dn_tmp_11);dn_fp_default();dn_foreign; }); }));
(void)(({ void * dn_tmp_12=v_dc;
void * dn_tmp_13=v_old_pen;
({ void * dn_foreign=dn_ext_SelectObject(dn_tmp_12,dn_tmp_13);dn_fp_default();dn_foreign; }); }));
(void)(({ void * dn_tmp_14=v_dc;
void * dn_tmp_15=v_old_brush;
({ void * dn_foreign=dn_ext_SelectObject(dn_tmp_14,dn_tmp_15);dn_fp_default();dn_foreign; }); }));
}

return 0;
}
static void dn_cb_rounded(void * v_dc,dn_r_Rect v_rect,void * v_brush,int32_t v_radius) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_rounded(v_dc,v_rect,v_brush,v_radius);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_paint_text(dn_r_Gui const * v_g,void * v_dc,dn_t_slice0_U8 v_text,void * v_font,uint32_t v_color,int32_t v_x,int32_t v_y,int32_t v_width,int32_t v_height) {
{
dn_t_buf_U16 volatile v_w={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U16(&v_w);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
void * v_old=({ void * dn_tmp_2=v_dc;
void * dn_tmp_3=v_font;
({ void * dn_foreign=dn_ext_SelectObject(dn_tmp_2,dn_tmp_3);dn_fp_default();dn_foreign; }); });
(void)(({ void * dn_tmp_4=v_dc;
int32_t dn_tmp_5=((int32_t)(1ULL));
({ int32_t dn_foreign=dn_ext_SetBkMode(dn_tmp_4,dn_tmp_5);dn_fp_default();dn_foreign; }); }));
(void)(({ void * dn_tmp_6=v_dc;
uint32_t dn_tmp_7=v_color;
({ uint32_t dn_foreign=dn_ext_SetTextColor(dn_tmp_6,dn_tmp_7);dn_fp_default();dn_foreign; }); }));
v_w=({ dn_t_slice0_U8 dn_tmp_8=v_text;
dn_f_wide(dn_tmp_8); });
dn_r_Rect v_rect=({ int32_t dn_tmp_9=({ dn_r_Gui const * dn_tmp_10=v_g;
int32_t dn_tmp_11=v_x;
dn_f_width_at(dn_tmp_10,dn_tmp_11); });
int32_t dn_tmp_12=({ dn_r_Gui const * dn_tmp_13=v_g;
int32_t dn_tmp_14=v_y;
dn_f_scale(dn_tmp_13,dn_tmp_14); });
int32_t dn_tmp_15=({ dn_r_Gui const * dn_tmp_16=v_g;
int32_t dn_tmp_17=({ int32_t dn_tmp_18=v_x;
int32_t dn_tmp_19=v_width;
dn_I32_add(dn_tmp_18,dn_tmp_19); });
dn_f_width_at(dn_tmp_16,dn_tmp_17); });
int32_t dn_tmp_20=({ dn_r_Gui const * dn_tmp_21=v_g;
int32_t dn_tmp_22=({ int32_t dn_tmp_23=v_y;
int32_t dn_tmp_24=v_height;
dn_I32_add(dn_tmp_23,dn_tmp_24); });
dn_f_scale(dn_tmp_21,dn_tmp_22); });
((dn_r_Rect){dn_tmp_9,dn_tmp_12,dn_tmp_15,dn_tmp_20}); });
(void)(({ void * dn_tmp_25=v_dc;
uint16_t * dn_tmp_26=({ dn_t_buf_U16 const * dn_tmp_27=(&(v_w));
((uint16_t *)(*(dn_tmp_27)).data); });
int32_t dn_tmp_28=dn_I32_neg(((int32_t)(1ULL)));
dn_r_Rect * dn_tmp_29=({ dn_r_Rect * dn_tmp_30=({ dn_r_Rect * dn_tmp_31=(&(v_rect));
((dn_r_Rect *)dn_tmp_31); });
((dn_r_Rect *)dn_tmp_30); });
uint32_t dn_tmp_32=((uint32_t)(0x810ULL));
({ int32_t dn_foreign=dn_ext_DrawTextW(dn_tmp_25,dn_tmp_26,dn_tmp_28,dn_tmp_29,dn_tmp_32);dn_fp_default();dn_foreign; }); }));
(void)(({ void * dn_tmp_33=v_dc;
void * dn_tmp_34=v_old;
({ void * dn_foreign=dn_ext_SelectObject(dn_tmp_33,dn_tmp_34);dn_fp_default();dn_foreign; }); }));
dn_drop_buf_U16(&v_w);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_paint_text(dn_r_Gui const * v_g,void * v_dc,dn_t_slice0_U8 v_text,void * v_font,uint32_t v_color,int32_t v_x,int32_t v_y,int32_t v_width,int32_t v_height) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_paint_text(v_g,v_dc,v_text,v_font,v_color,v_x,v_y,v_width,v_height);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_gui_paint(dn_r_Gui const * v_g) {
{
dn_r_PaintStruct v_paint=({ ((dn_r_PaintStruct){0}); });
void * v_dc=({ void * dn_tmp_0=(v_g)->f_window;
dn_r_PaintStruct * dn_tmp_1=({ dn_r_PaintStruct * dn_tmp_2=({ dn_r_PaintStruct * dn_tmp_3=(&(v_paint));
((dn_r_PaintStruct *)dn_tmp_3); });
((dn_r_PaintStruct *)dn_tmp_2); });
({ void * dn_foreign=dn_ext_BeginPaint(dn_tmp_0,dn_tmp_1);dn_fp_default();dn_foreign; }); });
dn_r_Rect v_rect=({ ((dn_r_Rect){0}); });
(void)(({ void * dn_tmp_4=(v_g)->f_window;
dn_r_Rect * dn_tmp_5=({ dn_r_Rect * dn_tmp_6=({ dn_r_Rect * dn_tmp_7=(&(v_rect));
((dn_r_Rect *)dn_tmp_7); });
((dn_r_Rect *)dn_tmp_6); });
({ int32_t dn_foreign=dn_ext_GetClientRect(dn_tmp_4,dn_tmp_5);dn_fp_default();dn_foreign; }); }));
(void)(({ void * dn_tmp_8=v_dc;
dn_r_Rect * dn_tmp_9=({ dn_r_Rect * dn_tmp_10=({ dn_r_Rect * dn_tmp_11=(&(v_rect));
((dn_r_Rect *)dn_tmp_11); });
((dn_r_Rect *)dn_tmp_10); });
void * dn_tmp_12=(v_g)->f_background;
({ int32_t dn_foreign=dn_ext_FillRect(dn_tmp_8,dn_tmp_9,dn_tmp_12);dn_fp_default();dn_foreign; }); }));
(void)(({ void * dn_tmp_13=v_dc;
dn_r_Rect dn_tmp_14=({ int32_t dn_tmp_15=({ dn_r_Gui const * dn_tmp_16=v_g;
int32_t dn_tmp_17=((int32_t)(32ULL));
dn_f_width_at(dn_tmp_16,dn_tmp_17); });
int32_t dn_tmp_18=({ dn_r_Gui const * dn_tmp_19=v_g;
int32_t dn_tmp_20=((int32_t)(27ULL));
dn_f_scale(dn_tmp_19,dn_tmp_20); });
int32_t dn_tmp_21=({ dn_r_Gui const * dn_tmp_22=v_g;
int32_t dn_tmp_23=((int32_t)(94ULL));
dn_f_width_at(dn_tmp_22,dn_tmp_23); });
int32_t dn_tmp_24=({ dn_r_Gui const * dn_tmp_25=v_g;
int32_t dn_tmp_26=((int32_t)(81ULL));
dn_f_scale(dn_tmp_25,dn_tmp_26); });
((dn_r_Rect){dn_tmp_15,dn_tmp_18,dn_tmp_21,dn_tmp_24}); });
void * dn_tmp_27=(v_g)->f_accent;
int32_t dn_tmp_28=({ dn_r_Gui const * dn_tmp_29=v_g;
int32_t dn_tmp_30=((int32_t)(16ULL));
dn_f_scale(dn_tmp_29,dn_tmp_30); });
dn_f_rounded(dn_tmp_13,dn_tmp_14,dn_tmp_27,dn_tmp_28); }));
(void)(({ dn_r_Gui const * dn_tmp_31=v_g;
void * dn_tmp_32=v_dc;
dn_t_slice0_U8 dn_tmp_33=({ dn_utf8 dn_tmp_34=((dn_utf8){(const uint8_t*)"N", 1});
((dn_t_slice0_U8){dn_tmp_34.data,dn_tmp_34.len}); });
void * dn_tmp_35=(v_g)->f_title_font;
uint32_t dn_tmp_36=({ uint32_t dn_tmp_37=((uint32_t)(255ULL));
uint32_t dn_tmp_38=((uint32_t)(255ULL));
uint32_t dn_tmp_39=((uint32_t)(255ULL));
dn_f_rgb(dn_tmp_37,dn_tmp_38,dn_tmp_39); });
int32_t dn_tmp_40=((int32_t)(47ULL));
int32_t dn_tmp_41=((int32_t)(34ULL));
int32_t dn_tmp_42=((int32_t)(60ULL));
int32_t dn_tmp_43=((int32_t)(44ULL));
dn_f_paint_text(dn_tmp_31,dn_tmp_32,dn_tmp_33,dn_tmp_35,dn_tmp_36,dn_tmp_40,dn_tmp_41,dn_tmp_42,dn_tmp_43); }));
(void)(({ dn_r_Gui const * dn_tmp_44=v_g;
void * dn_tmp_45=v_dc;
dn_t_slice0_U8 dn_tmp_46=({ dn_utf8 dn_tmp_47=((dn_utf8){(const uint8_t*)"NoDPI", 5});
((dn_t_slice0_U8){dn_tmp_47.data,dn_tmp_47.len}); });
void * dn_tmp_48=(v_g)->f_title_font;
uint32_t dn_tmp_49=({ uint32_t dn_tmp_50=((uint32_t)(244ULL));
uint32_t dn_tmp_51=((uint32_t)(247ULL));
uint32_t dn_tmp_52=((uint32_t)(252ULL));
dn_f_rgb(dn_tmp_50,dn_tmp_51,dn_tmp_52); });
int32_t dn_tmp_53=((int32_t)(116ULL));
int32_t dn_tmp_54=((int32_t)(23ULL));
int32_t dn_tmp_55=((int32_t)(450ULL));
int32_t dn_tmp_56=((int32_t)(45ULL));
dn_f_paint_text(dn_tmp_44,dn_tmp_45,dn_tmp_46,dn_tmp_48,dn_tmp_49,dn_tmp_53,dn_tmp_54,dn_tmp_55,dn_tmp_56); }));
(void)(({ dn_r_Gui const * dn_tmp_57=v_g;
void * dn_tmp_58=v_dc;
dn_t_slice0_U8 dn_tmp_59=({ dn_utf8 dn_tmp_60=((dn_utf8){(const uint8_t*)"\320\220\320\262\321\202\320\276\320\274\320\260\321\202\320\270\321\207\320\265\321\201\320\272\320\270\320\271 \320\277\320\276\320\264\320\261\320\276\321\200 \342\200\242 DLM 0.133.0", 57});
((dn_t_slice0_U8){dn_tmp_60.data,dn_tmp_60.len}); });
void * dn_tmp_61=(v_g)->f_font;
uint32_t dn_tmp_62=({ uint32_t dn_tmp_63=((uint32_t)(147ULL));
uint32_t dn_tmp_64=((uint32_t)(163ULL));
uint32_t dn_tmp_65=((uint32_t)(187ULL));
dn_f_rgb(dn_tmp_63,dn_tmp_64,dn_tmp_65); });
int32_t dn_tmp_66=((int32_t)(119ULL));
int32_t dn_tmp_67=((int32_t)(72ULL));
int32_t dn_tmp_68=((int32_t)(420ULL));
int32_t dn_tmp_69=((int32_t)(24ULL));
dn_f_paint_text(dn_tmp_57,dn_tmp_58,dn_tmp_59,dn_tmp_61,dn_tmp_62,dn_tmp_66,dn_tmp_67,dn_tmp_68,dn_tmp_69); }));
(void)(({ dn_r_Gui const * dn_tmp_70=v_g;
void * dn_tmp_71=v_dc;
dn_t_slice0_U8 dn_tmp_72=({ dn_utf8 dn_tmp_73=((dn_utf8){(const uint8_t*)"\320\241\320\265\321\200\320\262\320\270\321\201 \320\264\320\273\321\217 \320\277\320\276\320\264\320\261\320\276\321\200\320\260 \320\270 \320\277\321\200\320\276\320\262\320\265\321\200\320\272\320\270", 54});
((dn_t_slice0_U8){dn_tmp_73.data,dn_tmp_73.len}); });
void * dn_tmp_74=(v_g)->f_font;
uint32_t dn_tmp_75=({ uint32_t dn_tmp_76=((uint32_t)(147ULL));
uint32_t dn_tmp_77=((uint32_t)(163ULL));
uint32_t dn_tmp_78=((uint32_t)(187ULL));
dn_f_rgb(dn_tmp_76,dn_tmp_77,dn_tmp_78); });
int32_t dn_tmp_79=((int32_t)(570ULL));
int32_t dn_tmp_80=((int32_t)(35ULL));
int32_t dn_tmp_81=((int32_t)(380ULL));
int32_t dn_tmp_82=((int32_t)(26ULL));
dn_f_paint_text(dn_tmp_70,dn_tmp_71,dn_tmp_72,dn_tmp_74,dn_tmp_75,dn_tmp_79,dn_tmp_80,dn_tmp_81,dn_tmp_82); }));
(void)(({ void * dn_tmp_83=v_dc;
dn_r_Rect dn_tmp_84=({ int32_t dn_tmp_85=({ dn_r_Gui const * dn_tmp_86=v_g;
int32_t dn_tmp_87=((int32_t)(28ULL));
dn_f_width_at(dn_tmp_86,dn_tmp_87); });
int32_t dn_tmp_88=({ dn_r_Gui const * dn_tmp_89=v_g;
int32_t dn_tmp_90=((int32_t)(120ULL));
dn_f_scale(dn_tmp_89,dn_tmp_90); });
int32_t dn_tmp_91=({ dn_r_Gui const * dn_tmp_92=v_g;
int32_t dn_tmp_93=((int32_t)(972ULL));
dn_f_width_at(dn_tmp_92,dn_tmp_93); });
int32_t dn_tmp_94=({ dn_r_Gui const * dn_tmp_95=v_g;
int32_t dn_tmp_96=((int32_t)(380ULL));
dn_f_scale(dn_tmp_95,dn_tmp_96); });
((dn_r_Rect){dn_tmp_85,dn_tmp_88,dn_tmp_91,dn_tmp_94}); });
void * dn_tmp_97=(v_g)->f_card;
int32_t dn_tmp_98=({ dn_r_Gui const * dn_tmp_99=v_g;
int32_t dn_tmp_100=((int32_t)(20ULL));
dn_f_scale(dn_tmp_99,dn_tmp_100); });
dn_f_rounded(dn_tmp_83,dn_tmp_84,dn_tmp_97,dn_tmp_98); }));
dn_utf8 v_title=((dn_utf8){(const uint8_t*)"\320\236\320\261\321\200\320\260\320\261\320\276\321\202\320\272\320\260 \320\262\321\213\320\272\320\273\321\216\321\207\320\265\320\275\320\260", 37});
dn_utf8 v_detail=((dn_utf8){(const uint8_t*)"\320\235\320\260\320\266\320\274\320\270\321\202\320\265 \302\253\320\222\320\272\320\273\321\216\321\207\320\270\321\202\321\214\302\273. \320\237\321\200\320\276\320\263\321\200\320\260\320\274\320\274\320\260 \321\201\320\275\320\260\321\207\320\260\320\273\320\260 \320\277\321\200\320\276\320\262\320\265\321\200\320\270\321\202 \320\270\321\201\321\205\320\276\320\264\320\275\320\276\320\265 \320\277\320\276\320\264\320\272\320\273\321\216\321\207\320\265\320\275\320\270\320\265.", 128});
uint32_t v_color=({ uint32_t dn_tmp_101=((uint32_t)(244ULL));
uint32_t dn_tmp_102=((uint32_t)(247ULL));
uint32_t dn_tmp_103=((uint32_t)(252ULL));
dn_f_rgb(dn_tmp_101,dn_tmp_102,dn_tmp_103); });
if(({ uint32_t dn_tmp_104=(v_g)->f_phase;
uint32_t dn_tmp_105=({ uint32_t dn_tmp_106=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_106>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_106; });
(dn_tmp_104 == dn_tmp_105); })){
{ dn_utf8 dn_tmp_108=((dn_utf8){(const uint8_t*)"\320\237\321\200\320\276\320\262\320\265\321\200\321\217\321\216 \320\277\320\276\320\264\320\272\320\273\321\216\321\207\320\265\320\275\320\270\320\265\342\200\246", 42});dn_utf8 *dn_tmp_107=&(v_title);*dn_tmp_107=dn_tmp_108; }
{ dn_utf8 dn_tmp_110=((dn_utf8){(const uint8_t*)"\320\224\320\262\320\270\320\266\320\276\320\272 \320\276\321\201\321\202\320\260\320\275\320\276\320\262\320\273\320\265\320\275. \320\237\321\200\320\276\320\262\320\265\321\200\321\217\321\216\321\202\321\201\321\217 \321\200\320\276\321\201\321\201\320\270\320\271\321\201\320\272\320\270\320\265 \320\270 \320\262\320\275\320\265\321\210\320\275\320\270\320\271 \320\272\320\276\320\275\321\202\321\200\320\276\320\273\321\214\320\275\321\213\320\271 \321\201\320\260\320\271\321\202.", 129});dn_utf8 *dn_tmp_109=&(v_detail);*dn_tmp_109=dn_tmp_110; }
}
else{
}
if(({ uint32_t dn_tmp_111=(v_g)->f_phase;
uint32_t dn_tmp_112=({ uint32_t dn_tmp_113=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_113>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_113; });
(dn_tmp_111 == dn_tmp_112); })){
{ dn_utf8 dn_tmp_115=((dn_utf8){(const uint8_t*)"\320\237\320\276\320\264\320\263\320\276\321\202\320\260\320\262\320\273\320\270\320\262\320\260\321\216 \320\276\320\261\321\200\320\260\320\261\320\276\321\202\320\272\321\203\342\200\246", 48});dn_utf8 *dn_tmp_114=&(v_title);*dn_tmp_114=dn_tmp_115; }
{ dn_utf8 dn_tmp_117=((dn_utf8){(const uint8_t*)"\320\237\320\276\320\264\321\202\320\262\320\265\321\200\320\264\320\270\321\202\320\265 \320\267\320\260\320\277\321\200\320\276\321\201 Windows. \320\236\320\266\320\270\320\264\320\260\321\216 \320\263\320\276\321\202\320\276\320\262\320\275\320\276\321\201\321\202\320\270 \320\264\320\262\320\270\320\266\320\272\320\260.", 92});dn_utf8 *dn_tmp_116=&(v_detail);*dn_tmp_116=dn_tmp_117; }
}
else{
}
if(({ uint32_t dn_tmp_118=(v_g)->f_phase;
uint32_t dn_tmp_119=({ uint32_t dn_tmp_120=((uint32_t)(3ULL));
if((unsigned __int128)dn_tmp_120>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_120; });
(dn_tmp_118 == dn_tmp_119); })){
{ dn_utf8 dn_tmp_122=((dn_utf8){(const uint8_t*)"\320\236\320\261\321\200\320\260\320\261\320\276\321\202\320\272\320\260 \320\262\320\272\320\273\321\216\321\207\320\265\320\275\320\260", 35});dn_utf8 *dn_tmp_121=&(v_title);*dn_tmp_121=dn_tmp_122; }
{ dn_utf8 dn_tmp_124=((dn_utf8){(const uint8_t*)"\320\241\321\202\321\200\320\260\321\202\320\265\320\263\320\270\320\270 \320\262\321\213\320\261\320\270\321\200\320\260\321\216\321\202\321\201\321\217 \320\260\320\262\321\202\320\276\320\274\320\260\321\202\320\270\321\207\320\265\321\201\320\272\320\270. \320\237\321\200\320\276\320\262\320\265\321\200\321\214\321\202\320\265 Telegram, \320\262\320\270\320\264\320\265\320\276 YouTube \320\270 \320\263\320\276\320\273\320\276\321\201 Discord.", 138});dn_utf8 *dn_tmp_123=&(v_detail);*dn_tmp_123=dn_tmp_124; }
{ uint32_t dn_tmp_129=({ uint32_t dn_tmp_125=((uint32_t)(81ULL));
uint32_t dn_tmp_126=((uint32_t)(219ULL));
uint32_t dn_tmp_127=((uint32_t)(167ULL));
dn_f_rgb(dn_tmp_125,dn_tmp_126,dn_tmp_127); });uint32_t *dn_tmp_128=&(v_color);*dn_tmp_128=dn_tmp_129; }
}
else{
}
if(({ uint32_t dn_tmp_130=(v_g)->f_phase;
uint32_t dn_tmp_131=({ uint32_t dn_tmp_132=((uint32_t)(5ULL));
if((unsigned __int128)dn_tmp_132>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_132; });
(dn_tmp_130 == dn_tmp_131); })){
{ dn_utf8 dn_tmp_134=((dn_utf8){(const uint8_t*)"\320\237\320\276\320\270\321\201\320\272 \321\201\321\202\321\200\320\260\321\202\320\265\320\263\320\270\320\270", 29});dn_utf8 *dn_tmp_133=&(v_title);*dn_tmp_133=dn_tmp_134; }
{ dn_utf8 dn_tmp_136=((dn_utf8){(const uint8_t*)"\320\241\321\200\320\260\320\262\320\275\320\270\320\262\320\260\321\216 \320\274\320\265\321\202\320\276\320\264\321\213 \320\275\320\260 \320\262\320\260\321\210\320\265\320\274 \320\277\320\276\320\264\320\272\320\273\321\216\321\207\320\265\320\275\320\270\320\270.", 71});dn_utf8 *dn_tmp_135=&(v_detail);*dn_tmp_135=dn_tmp_136; }
}
else{
}
if(({ uint32_t dn_tmp_137=(v_g)->f_phase;
uint32_t dn_tmp_138=({ uint32_t dn_tmp_139=((uint32_t)(6ULL));
if((unsigned __int128)dn_tmp_139>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_139; });
(dn_tmp_137 == dn_tmp_138); })){
{ dn_utf8 dn_tmp_141=((dn_utf8){(const uint8_t*)"\320\236\321\210\320\270\320\261\320\272\320\260 \320\267\320\260\320\277\321\203\321\201\320\272\320\260 \320\270\320\273\320\270 \320\277\321\200\320\276\320\262\320\265\321\200\320\272\320\270", 51});dn_utf8 *dn_tmp_140=&(v_title);*dn_tmp_140=dn_tmp_141; }
{ dn_utf8 dn_tmp_143=((dn_utf8){(const uint8_t*)"\320\235\320\260\320\266\320\274\320\270\321\202\320\265 \302\253\320\237\321\200\320\276\320\262\320\265\321\200\320\270\321\202\321\214 \321\201\320\265\321\200\320\262\320\270\321\201\302\273: \320\276\321\202\320\272\321\200\320\276\320\265\321\202\321\201\321\217 \320\264\320\270\320\260\320\263\320\275\320\276\321\201\321\202\320\270\320\272\320\260 \320\264\321\200\320\260\320\271\320\262\320\265\321\200\320\260.", 111});dn_utf8 *dn_tmp_142=&(v_detail);*dn_tmp_142=dn_tmp_143; }
{ uint32_t dn_tmp_148=({ uint32_t dn_tmp_144=((uint32_t)(255ULL));
uint32_t dn_tmp_145=((uint32_t)(192ULL));
uint32_t dn_tmp_146=((uint32_t)(103ULL));
dn_f_rgb(dn_tmp_144,dn_tmp_145,dn_tmp_146); });uint32_t *dn_tmp_147=&(v_color);*dn_tmp_147=dn_tmp_148; }
}
else{
}
if((({ uint32_t dn_tmp_149=(v_g)->f_phase;
uint32_t dn_tmp_150=({ uint32_t dn_tmp_151=((uint32_t)(5ULL));
if((unsigned __int128)dn_tmp_151>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_151; });
(dn_tmp_149 == dn_tmp_150); }) && ({ uint32_t dn_tmp_152=((v_g)->f_view).f_phase;
uint32_t dn_tmp_153=({ uint32_t dn_tmp_154=((uint32_t)(5ULL));
if((unsigned __int128)dn_tmp_154>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_154; });
(dn_tmp_152 == dn_tmp_153); }))){
{ dn_utf8 dn_tmp_156=((dn_utf8){(const uint8_t*)"\320\237\321\200\320\276\320\262\320\265\321\200\321\217\321\216 \320\264\321\200\320\260\320\271\320\262\320\265\321\200\342\200\246", 34});dn_utf8 *dn_tmp_155=&(v_title);*dn_tmp_155=dn_tmp_156; }
{ dn_utf8 dn_tmp_158=((dn_utf8){(const uint8_t*)"\320\237\321\200\320\276\320\262\320\265\321\200\320\272\320\260 WinDivert \320\264\320\276 \320\264\320\273\320\270\321\202\320\265\320\273\321\214\320\275\321\213\321\205 \321\201\320\265\321\202\320\265\320\262\321\213\321\205 \320\277\321\200\320\276\320\261; \320\277\320\265\321\200\320\265\321\205\320\262\320\260\321\202 \320\275\320\265 \320\262\320\272\320\273\321\216\321\207\321\221\320\275.", 115});dn_utf8 *dn_tmp_157=&(v_detail);*dn_tmp_157=dn_tmp_158; }
}
else{
}
if((({ uint32_t dn_tmp_159=(v_g)->f_phase;
uint32_t dn_tmp_160=({ uint32_t dn_tmp_161=((uint32_t)(5ULL));
if((unsigned __int128)dn_tmp_161>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_161; });
(dn_tmp_159 == dn_tmp_160); }) && ({ uint32_t dn_tmp_162=((v_g)->f_view).f_phase;
uint32_t dn_tmp_163=({ uint32_t dn_tmp_164=((uint32_t)(6ULL));
if((unsigned __int128)dn_tmp_164>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_164; });
(dn_tmp_162 == dn_tmp_163); }))){
{ dn_utf8 dn_tmp_166=((dn_utf8){(const uint8_t*)"\320\230\321\201\321\205\320\276\320\264\320\275\321\213\320\265 \321\201\320\265\321\202\320\265\320\262\321\213\320\265 \320\277\321\200\320\276\320\261\321\213\342\200\246", 45});dn_utf8 *dn_tmp_165=&(v_title);*dn_tmp_165=dn_tmp_166; }
{ dn_utf8 dn_tmp_168=((dn_utf8){(const uint8_t*)"\320\236\320\261\321\203\321\207\320\265\320\275\320\270\320\265 \320\265\321\211\321\221 \320\275\320\265 \320\275\320\260\321\207\320\260\320\273\320\276\321\201\321\214: \321\201\320\276\320\261\320\270\321\200\320\260\320\265\321\202\321\201\321\217 \321\200\320\265\320\267\321\203\320\273\321\214\321\202\320\260\321\202 \320\261\320\265\320\267 \320\276\320\261\321\200\320\260\320\261\320\276\321\202\320\272\320\270.", 113});dn_utf8 *dn_tmp_167=&(v_detail);*dn_tmp_167=dn_tmp_168; }
}
else{
}
if(({ uint32_t dn_tmp_169=(v_g)->f_phase;
uint32_t dn_tmp_170=({ uint32_t dn_tmp_171=((uint32_t)(4ULL));
if((unsigned __int128)dn_tmp_171>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_171; });
(dn_tmp_169 == dn_tmp_170); })){
{ dn_utf8 dn_tmp_173=((dn_utf8){(const uint8_t*)"\320\235\321\203\320\266\320\275\320\276 \320\277\321\200\320\276\320\262\320\265\321\200\320\270\321\202\321\214 \321\201\320\276\320\265\320\264\320\270\320\275\320\265\320\275\320\270\320\265", 50});dn_utf8 *dn_tmp_172=&(v_title);*dn_tmp_172=dn_tmp_173; }
{ dn_utf8 dn_tmp_175=((dn_utf8){(const uint8_t*)"\320\235\320\260\320\266\320\274\320\270\321\202\320\265 \302\253\320\237\321\200\320\276\320\262\320\265\321\200\320\270\321\202\321\214 \321\201\320\265\321\200\320\262\320\270\321\201\302\273, \320\267\320\260\321\202\320\265\320\274 \302\253\320\241\320\276\320\261\321\200\320\260\321\202\321\214 \320\276\321\202\321\207\321\221\321\202\302\273: \320\276\321\202\321\207\321\221\321\202 \320\277\320\276\320\272\320\260\320\266\320\265\321\202 \321\215\321\202\320\260\320\277 \321\201\320\261\320\276\321\217.", 138});dn_utf8 *dn_tmp_174=&(v_detail);*dn_tmp_174=dn_tmp_175; }
{ uint32_t dn_tmp_180=({ uint32_t dn_tmp_176=((uint32_t)(255ULL));
uint32_t dn_tmp_177=((uint32_t)(192ULL));
uint32_t dn_tmp_178=((uint32_t)(103ULL));
dn_f_rgb(dn_tmp_176,dn_tmp_177,dn_tmp_178); });uint32_t *dn_tmp_179=&(v_color);*dn_tmp_179=dn_tmp_180; }
}
else{
}
(void)(({ dn_r_Gui const * dn_tmp_181=v_g;
void * dn_tmp_182=v_dc;
dn_t_slice0_U8 dn_tmp_183=({ dn_utf8 dn_tmp_184=v_title;
((dn_t_slice0_U8){dn_tmp_184.data,dn_tmp_184.len}); });
void * dn_tmp_185=(v_g)->f_status_font;
uint32_t dn_tmp_186=v_color;
int32_t dn_tmp_187=((int32_t)(60ULL));
int32_t dn_tmp_188=((int32_t)(147ULL));
int32_t dn_tmp_189=((int32_t)(865ULL));
int32_t dn_tmp_190=((int32_t)(46ULL));
dn_f_paint_text(dn_tmp_181,dn_tmp_182,dn_tmp_183,dn_tmp_185,dn_tmp_186,dn_tmp_187,dn_tmp_188,dn_tmp_189,dn_tmp_190); }));
if(((({ uint32_t dn_tmp_191=(v_g)->f_phase;
uint32_t dn_tmp_192=({ uint32_t dn_tmp_193=((uint32_t)(4ULL));
if((unsigned __int128)dn_tmp_193>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_193; });
(dn_tmp_191 != dn_tmp_192); }) && ({ uint32_t dn_tmp_194=(v_g)->f_phase;
uint32_t dn_tmp_195=({ uint32_t dn_tmp_196=((uint32_t)(6ULL));
if((unsigned __int128)dn_tmp_196>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_196; });
(dn_tmp_194 != dn_tmp_195); })) && (({ uint32_t dn_tmp_197=((v_g)->f_view).f_total;
uint32_t dn_tmp_198=({ uint32_t dn_tmp_199=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_199>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_199; });
(dn_tmp_197 > dn_tmp_198); }) || ({ uint32_t dn_tmp_200=(v_g)->f_phase;
uint32_t dn_tmp_201=({ uint32_t dn_tmp_202=((uint32_t)(5ULL));
if((unsigned __int128)dn_tmp_202>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_202; });
(dn_tmp_200 == dn_tmp_201); })))){
dn_t_buf_U8 volatile v_caption={0};
dn_boundary dn_tmp_203={0};jmp_buf dn_tmp_204;dn_tmp_203.jump=&dn_tmp_204;dn_tmp_203.previous=dn_current;dn_tmp_203.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_203;if(setjmp(dn_tmp_204)){dn_current=dn_tmp_203.previous;dn_drop_buf_U8(&v_caption);
dn_boundary_end(&dn_tmp_203,1);dn_raise();}
v_caption=({ dn_r_TrainingView const * dn_tmp_205=(&((v_g)->f_view));
bool dn_tmp_206=(v_g)->f_history;
dn_f_training_caption(dn_tmp_205,dn_tmp_206); });
(void)(({ dn_r_Gui const * dn_tmp_207=v_g;
void * dn_tmp_208=v_dc;
dn_t_slice0_U8 dn_tmp_209=({ dn_t_buf_U8 const * dn_tmp_210=(&(v_caption));
size_t dn_tmp_211=((size_t)(0ULL));
size_t dn_tmp_212=({ dn_t_buf_U8 const * dn_tmp_213=(&(v_caption));
(*(dn_tmp_213)).len; });
({ dn_range(dn_tmp_211,dn_tmp_212,(*(dn_tmp_210)).len);(dn_t_slice0_U8){(*(dn_tmp_210)).data?(*(dn_tmp_210)).data+dn_tmp_211:NULL,dn_tmp_212}; }); });
void * dn_tmp_214=(v_g)->f_font;
uint32_t dn_tmp_215=({ uint32_t dn_tmp_216=((uint32_t)(166ULL));
uint32_t dn_tmp_217=((uint32_t)(181ULL));
uint32_t dn_tmp_218=((uint32_t)(202ULL));
dn_f_rgb(dn_tmp_216,dn_tmp_217,dn_tmp_218); });
int32_t dn_tmp_219=((int32_t)(60ULL));
int32_t dn_tmp_220=((int32_t)(207ULL));
int32_t dn_tmp_221=((int32_t)(860ULL));
int32_t dn_tmp_222=((int32_t)(58ULL));
dn_f_paint_text(dn_tmp_207,dn_tmp_208,dn_tmp_209,dn_tmp_214,dn_tmp_215,dn_tmp_219,dn_tmp_220,dn_tmp_221,dn_tmp_222); }));
dn_drop_buf_U8(&v_caption);
dn_boundary_end(&dn_tmp_203,0);
}
else{
(void)(({ dn_r_Gui const * dn_tmp_223=v_g;
void * dn_tmp_224=v_dc;
dn_t_slice0_U8 dn_tmp_225=({ dn_utf8 dn_tmp_226=v_detail;
((dn_t_slice0_U8){dn_tmp_226.data,dn_tmp_226.len}); });
void * dn_tmp_227=(v_g)->f_font;
uint32_t dn_tmp_228=({ uint32_t dn_tmp_229=((uint32_t)(166ULL));
uint32_t dn_tmp_230=((uint32_t)(181ULL));
uint32_t dn_tmp_231=((uint32_t)(202ULL));
dn_f_rgb(dn_tmp_229,dn_tmp_230,dn_tmp_231); });
int32_t dn_tmp_232=((int32_t)(60ULL));
int32_t dn_tmp_233=((int32_t)(207ULL));
int32_t dn_tmp_234=((int32_t)(860ULL));
int32_t dn_tmp_235=((int32_t)(53ULL));
dn_f_paint_text(dn_tmp_223,dn_tmp_224,dn_tmp_225,dn_tmp_227,dn_tmp_228,dn_tmp_232,dn_tmp_233,dn_tmp_234,dn_tmp_235); }));
}
if((({ uint32_t dn_tmp_236=((v_g)->f_view).f_budget;
uint32_t dn_tmp_237=({ uint32_t dn_tmp_238=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_238>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_238; });
(dn_tmp_236 > dn_tmp_237); }) || ({ uint32_t dn_tmp_239=((v_g)->f_view).f_total;
uint32_t dn_tmp_240=({ uint32_t dn_tmp_241=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_241>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_241; });
(dn_tmp_239 > dn_tmp_240); }))){
dn_t_buf_U8 volatile v_clock={0};
dn_boundary dn_tmp_242={0};jmp_buf dn_tmp_243;dn_tmp_242.jump=&dn_tmp_243;dn_tmp_242.previous=dn_current;dn_tmp_242.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_242;if(setjmp(dn_tmp_243)){dn_current=dn_tmp_242.previous;dn_drop_buf_U8(&v_clock);
dn_boundary_end(&dn_tmp_242,1);dn_raise();}
v_clock=({ dn_r_TrainingView const * dn_tmp_244=(&((v_g)->f_view));
dn_f_training_clock(dn_tmp_244); });
(void)(({ dn_r_Gui const * dn_tmp_245=v_g;
void * dn_tmp_246=v_dc;
dn_t_slice0_U8 dn_tmp_247=({ dn_t_buf_U8 const * dn_tmp_248=(&(v_clock));
size_t dn_tmp_249=((size_t)(0ULL));
size_t dn_tmp_250=({ dn_t_buf_U8 const * dn_tmp_251=(&(v_clock));
(*(dn_tmp_251)).len; });
({ dn_range(dn_tmp_249,dn_tmp_250,(*(dn_tmp_248)).len);(dn_t_slice0_U8){(*(dn_tmp_248)).data?(*(dn_tmp_248)).data+dn_tmp_249:NULL,dn_tmp_250}; }); });
void * dn_tmp_252=(v_g)->f_font;
uint32_t dn_tmp_253=({ uint32_t dn_tmp_254=((uint32_t)(147ULL));
uint32_t dn_tmp_255=((uint32_t)(163ULL));
uint32_t dn_tmp_256=((uint32_t)(187ULL));
dn_f_rgb(dn_tmp_254,dn_tmp_255,dn_tmp_256); });
int32_t dn_tmp_257=((int32_t)(60ULL));
int32_t dn_tmp_258=((int32_t)(342ULL));
int32_t dn_tmp_259=((int32_t)(860ULL));
int32_t dn_tmp_260=((int32_t)(38ULL));
dn_f_paint_text(dn_tmp_245,dn_tmp_246,dn_tmp_247,dn_tmp_252,dn_tmp_253,dn_tmp_257,dn_tmp_258,dn_tmp_259,dn_tmp_260); }));
dn_drop_buf_U8(&v_clock);
dn_boundary_end(&dn_tmp_242,0);
}
else{
(void)(({ dn_r_Gui const * dn_tmp_261=v_g;
void * dn_tmp_262=v_dc;
dn_t_slice0_U8 dn_tmp_263=({ dn_utf8 dn_tmp_264=((dn_utf8){(const uint8_t*)"DNS \320\270 \320\273\320\276\320\272\320\260\320\273\321\214\320\275\320\260\321\217 \321\201\320\265\321\202\321\214 \320\277\321\200\320\276\321\205\320\276\320\264\321\217\321\202 \320\261\320\265\320\267 \320\277\321\200\320\265\320\276\320\261\321\200\320\260\320\267\320\276\320\262\320\260\320\275\320\270\320\271.", 88});
((dn_t_slice0_U8){dn_tmp_264.data,dn_tmp_264.len}); });
void * dn_tmp_265=(v_g)->f_font;
uint32_t dn_tmp_266=({ uint32_t dn_tmp_267=((uint32_t)(147ULL));
uint32_t dn_tmp_268=((uint32_t)(163ULL));
uint32_t dn_tmp_269=((uint32_t)(187ULL));
dn_f_rgb(dn_tmp_267,dn_tmp_268,dn_tmp_269); });
int32_t dn_tmp_270=((int32_t)(60ULL));
int32_t dn_tmp_271=((int32_t)(346ULL));
int32_t dn_tmp_272=((int32_t)(860ULL));
int32_t dn_tmp_273=((int32_t)(25ULL));
dn_f_paint_text(dn_tmp_261,dn_tmp_262,dn_tmp_263,dn_tmp_265,dn_tmp_266,dn_tmp_270,dn_tmp_271,dn_tmp_272,dn_tmp_273); }));
}
if(({ uint32_t dn_tmp_274=((v_g)->f_view).f_budget;
uint32_t dn_tmp_275=({ uint32_t dn_tmp_276=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_276>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_276; });
(dn_tmp_274 > dn_tmp_275); })){
uint32_t v_amount=({ uint32_t dn_tmp_277=((v_g)->f_view).f_elapsed;
uint32_t dn_tmp_278=((v_g)->f_view).f_budget;
dn_f_min_u32(dn_tmp_277,dn_tmp_278); });
int32_t v_extent=({ int32_t dn_tmp_279=({ dn_r_Gui const * dn_tmp_280=v_g;
int32_t dn_tmp_281=((int32_t)(880ULL));
dn_f_width_at(dn_tmp_280,dn_tmp_281); });
int32_t dn_tmp_282=({ uint32_t dn_tmp_283=v_amount;
if((unsigned __int128)dn_tmp_283>(((unsigned __int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_283; });
int32_t dn_tmp_284=({ uint32_t dn_tmp_285=((v_g)->f_view).f_budget;
if((unsigned __int128)dn_tmp_285>(((unsigned __int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_285; });
({ int32_t dn_foreign=dn_ext_MulDiv(dn_tmp_279,dn_tmp_282,dn_tmp_284);dn_fp_default();dn_foreign; }); });
(void)(({ void * dn_tmp_286=v_dc;
dn_r_Rect dn_tmp_287=({ int32_t dn_tmp_288=({ dn_r_Gui const * dn_tmp_289=v_g;
int32_t dn_tmp_290=((int32_t)(60ULL));
dn_f_width_at(dn_tmp_289,dn_tmp_290); });
int32_t dn_tmp_291=({ dn_r_Gui const * dn_tmp_292=v_g;
int32_t dn_tmp_293=((int32_t)(385ULL));
dn_f_scale(dn_tmp_292,dn_tmp_293); });
int32_t dn_tmp_294=({ dn_r_Gui const * dn_tmp_295=v_g;
int32_t dn_tmp_296=((int32_t)(940ULL));
dn_f_width_at(dn_tmp_295,dn_tmp_296); });
int32_t dn_tmp_297=({ dn_r_Gui const * dn_tmp_298=v_g;
int32_t dn_tmp_299=((int32_t)(391ULL));
dn_f_scale(dn_tmp_298,dn_tmp_299); });
((dn_r_Rect){dn_tmp_288,dn_tmp_291,dn_tmp_294,dn_tmp_297}); });
void * dn_tmp_300=(v_g)->f_secondary;
int32_t dn_tmp_301=({ dn_r_Gui const * dn_tmp_302=v_g;
int32_t dn_tmp_303=((int32_t)(6ULL));
dn_f_scale(dn_tmp_302,dn_tmp_303); });
dn_f_rounded(dn_tmp_286,dn_tmp_287,dn_tmp_300,dn_tmp_301); }));
if(({ int32_t dn_tmp_304=v_extent;
int32_t dn_tmp_305=({ int32_t dn_tmp_306=((int32_t)(0ULL));
if((__int128)dn_tmp_306<(-((__int128)1<<31))||(__int128)dn_tmp_306>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_306; });
(dn_tmp_304 > dn_tmp_305); })){
(void)(({ void * dn_tmp_307=v_dc;
dn_r_Rect dn_tmp_308=({ int32_t dn_tmp_309=({ dn_r_Gui const * dn_tmp_310=v_g;
int32_t dn_tmp_311=((int32_t)(60ULL));
dn_f_width_at(dn_tmp_310,dn_tmp_311); });
int32_t dn_tmp_312=({ dn_r_Gui const * dn_tmp_313=v_g;
int32_t dn_tmp_314=((int32_t)(385ULL));
dn_f_scale(dn_tmp_313,dn_tmp_314); });
int32_t dn_tmp_315=({ int32_t dn_tmp_316=({ dn_r_Gui const * dn_tmp_317=v_g;
int32_t dn_tmp_318=((int32_t)(60ULL));
dn_f_width_at(dn_tmp_317,dn_tmp_318); });
int32_t dn_tmp_319=v_extent;
dn_I32_add(dn_tmp_316,dn_tmp_319); });
int32_t dn_tmp_320=({ dn_r_Gui const * dn_tmp_321=v_g;
int32_t dn_tmp_322=((int32_t)(391ULL));
dn_f_scale(dn_tmp_321,dn_tmp_322); });
((dn_r_Rect){dn_tmp_309,dn_tmp_312,dn_tmp_315,dn_tmp_320}); });
void * dn_tmp_323=(v_g)->f_accent;
int32_t dn_tmp_324=({ dn_r_Gui const * dn_tmp_325=v_g;
int32_t dn_tmp_326=((int32_t)(6ULL));
dn_f_scale(dn_tmp_325,dn_tmp_326); });
dn_f_rounded(dn_tmp_307,dn_tmp_308,dn_tmp_323,dn_tmp_324); }));
}
else{
}
}
else{
}
(void)(({ void * dn_tmp_327=v_dc;
dn_r_Rect dn_tmp_328=({ int32_t dn_tmp_329=({ dn_r_Gui const * dn_tmp_330=v_g;
int32_t dn_tmp_331=((int32_t)(28ULL));
dn_f_width_at(dn_tmp_330,dn_tmp_331); });
int32_t dn_tmp_332=({ dn_r_Gui const * dn_tmp_333=v_g;
int32_t dn_tmp_334=((int32_t)(400ULL));
dn_f_scale(dn_tmp_333,dn_tmp_334); });
int32_t dn_tmp_335=({ dn_r_Gui const * dn_tmp_336=v_g;
int32_t dn_tmp_337=((int32_t)(972ULL));
dn_f_width_at(dn_tmp_336,dn_tmp_337); });
int32_t dn_tmp_338=({ dn_r_Gui const * dn_tmp_339=v_g;
int32_t dn_tmp_340=((int32_t)(540ULL));
dn_f_scale(dn_tmp_339,dn_tmp_340); });
((dn_r_Rect){dn_tmp_329,dn_tmp_332,dn_tmp_335,dn_tmp_338}); });
void * dn_tmp_341=(v_g)->f_card;
int32_t dn_tmp_342=({ dn_r_Gui const * dn_tmp_343=v_g;
int32_t dn_tmp_344=((int32_t)(20ULL));
dn_f_scale(dn_tmp_343,dn_tmp_344); });
dn_f_rounded(dn_tmp_327,dn_tmp_328,dn_tmp_341,dn_tmp_342); }));
(void)(({ dn_r_Gui const * dn_tmp_345=v_g;
void * dn_tmp_346=v_dc;
dn_t_slice0_U8 dn_tmp_347=({ dn_t_buf_U8 const * dn_tmp_348=(&((v_g)->f_note));
size_t dn_tmp_349=((size_t)(0ULL));
size_t dn_tmp_350=({ dn_t_buf_U8 const * dn_tmp_351=(&((v_g)->f_note));
(*(dn_tmp_351)).len; });
({ dn_range(dn_tmp_349,dn_tmp_350,(*(dn_tmp_348)).len);(dn_t_slice0_U8){(*(dn_tmp_348)).data?(*(dn_tmp_348)).data+dn_tmp_349:NULL,dn_tmp_350}; }); });
void * dn_tmp_352=(v_g)->f_font;
uint32_t dn_tmp_353=({ uint32_t dn_tmp_354=((uint32_t)(147ULL));
uint32_t dn_tmp_355=((uint32_t)(163ULL));
uint32_t dn_tmp_356=((uint32_t)(187ULL));
dn_f_rgb(dn_tmp_354,dn_tmp_355,dn_tmp_356); });
int32_t dn_tmp_357=((int32_t)(32ULL));
int32_t dn_tmp_358=((int32_t)(556ULL));
int32_t dn_tmp_359=((int32_t)(940ULL));
int32_t dn_tmp_360=((int32_t)(58ULL));
dn_f_paint_text(dn_tmp_345,dn_tmp_346,dn_tmp_347,dn_tmp_352,dn_tmp_353,dn_tmp_357,dn_tmp_358,dn_tmp_359,dn_tmp_360); }));
(void)(({ dn_r_Gui const * dn_tmp_361=v_g;
void * dn_tmp_362=v_dc;
dn_t_slice0_U8 dn_tmp_363=({ dn_utf8 dn_tmp_364=((dn_utf8){(const uint8_t*)"2.4  /  \320\226\321\203\321\200\320\275\320\260\320\273\321\213 \321\201\320\276\321\205\321\200\320\260\320\275\321\217\321\216\321\202\321\201\321\217 \320\273\320\276\320\272\320\260\320\273\321\214\320\275\320\276. \320\235\320\260\321\201\321\202\321\200\320\276\320\271\320\272\320\270 \321\201\320\265\321\202\320\270 Windows \320\275\320\265 \320\274\320\265\320\275\321\217\321\216\321\202\321\201\321\217.", 122});
((dn_t_slice0_U8){dn_tmp_364.data,dn_tmp_364.len}); });
void * dn_tmp_365=(v_g)->f_font;
uint32_t dn_tmp_366=({ uint32_t dn_tmp_367=((uint32_t)(115ULL));
uint32_t dn_tmp_368=((uint32_t)(133ULL));
uint32_t dn_tmp_369=((uint32_t)(157ULL));
dn_f_rgb(dn_tmp_367,dn_tmp_368,dn_tmp_369); });
int32_t dn_tmp_370=((int32_t)(32ULL));
int32_t dn_tmp_371=((int32_t)(626ULL));
int32_t dn_tmp_372=((int32_t)(940ULL));
int32_t dn_tmp_373=((int32_t)(35ULL));
dn_f_paint_text(dn_tmp_361,dn_tmp_362,dn_tmp_363,dn_tmp_365,dn_tmp_366,dn_tmp_370,dn_tmp_371,dn_tmp_372,dn_tmp_373); }));
(void)(({ void * dn_tmp_374=(v_g)->f_window;
dn_r_PaintStruct * dn_tmp_375=({ dn_r_PaintStruct * dn_tmp_376=({ dn_r_PaintStruct * dn_tmp_377=(&(v_paint));
((dn_r_PaintStruct *)dn_tmp_377); });
((dn_r_PaintStruct *)dn_tmp_376); });
({ int32_t dn_foreign=dn_ext_EndPaint(dn_tmp_374,dn_tmp_375);dn_fp_default();dn_foreign; }); }));
}

return 0;
}
static void dn_cb_gui_paint(dn_r_Gui const * v_g) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_gui_paint(v_g);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_gui_layout(dn_r_Gui const * v_g) {
{
{ size_t dn_tmp_2=((size_t)(0ULL));size_t dn_tmp_1=({ dn_t_buf_r7_Control const * dn_tmp_0=(&((v_g)->f_controls));
(*(dn_tmp_0)).len; });for(size_t v_i=dn_tmp_2;v_i<dn_tmp_1;v_i++){
dn_r_Control v_c=(*({ dn_t_buf_r7_Control volatile *dn_tmp_3=&((v_g)->f_controls);size_t dn_tmp_4=v_i;&dn_tmp_3->data[dn_index(dn_tmp_4,dn_tmp_3->len)]; }));
(void)(({ void * dn_tmp_5=(v_c).f_window;
int32_t dn_tmp_6=({ dn_r_Gui const * dn_tmp_7=v_g;
int32_t dn_tmp_8=(v_c).f_x;
dn_f_width_at(dn_tmp_7,dn_tmp_8); });
int32_t dn_tmp_9=({ dn_r_Gui const * dn_tmp_10=v_g;
int32_t dn_tmp_11=(v_c).f_y;
dn_f_scale(dn_tmp_10,dn_tmp_11); });
int32_t dn_tmp_12=({ dn_r_Gui const * dn_tmp_13=v_g;
int32_t dn_tmp_14=(v_c).f_width;
dn_f_width_at(dn_tmp_13,dn_tmp_14); });
int32_t dn_tmp_15=({ dn_r_Gui const * dn_tmp_16=v_g;
int32_t dn_tmp_17=(v_c).f_height;
dn_f_scale(dn_tmp_16,dn_tmp_17); });
int32_t dn_tmp_18=((int32_t)(1ULL));
({ int32_t dn_foreign=dn_ext_MoveWindow(dn_tmp_5,dn_tmp_6,dn_tmp_9,dn_tmp_12,dn_tmp_15,dn_tmp_18);dn_fp_default();dn_foreign; }); }));
}
 }
(void)(({ void * dn_tmp_19=(v_g)->f_window;
dn_r_Rect * dn_tmp_20=({ ((dn_r_Rect *)NULL); });
int32_t dn_tmp_21=((int32_t)(1ULL));
({ int32_t dn_foreign=dn_ext_InvalidateRect(dn_tmp_19,dn_tmp_20,dn_tmp_21);dn_fp_default();dn_foreign; }); }));
}

return 0;
}
static void dn_cb_gui_layout(dn_r_Gui const * v_g) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_gui_layout(v_g);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_add_button(dn_r_Gui * v_g,dn_utf8 v_title,size_t v_id,int32_t v_x,int32_t v_y,int32_t v_width,int32_t v_height) {
{
dn_t_buf_U16 volatile v_class_={0};
dn_t_buf_U16 volatile v_text={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U16(&v_text);
dn_drop_buf_U16(&v_class_);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_class_=({ dn_t_slice0_U8 dn_tmp_2=({ dn_utf8 dn_tmp_3=((dn_utf8){(const uint8_t*)"BUTTON", 6});
((dn_t_slice0_U8){dn_tmp_3.data,dn_tmp_3.len}); });
dn_f_wide(dn_tmp_2); });
v_text=({ dn_t_slice0_U8 dn_tmp_4=({ dn_utf8 dn_tmp_5=v_title;
((dn_t_slice0_U8){dn_tmp_5.data,dn_tmp_5.len}); });
dn_f_wide(dn_tmp_4); });
void * v_child=({ uint32_t dn_tmp_6=((uint32_t)(0ULL));
uint16_t * dn_tmp_7=({ dn_t_buf_U16 const * dn_tmp_8=(&(v_class_));
((uint16_t *)(*(dn_tmp_8)).data); });
uint16_t * dn_tmp_9=({ dn_t_buf_U16 const * dn_tmp_10=(&(v_text));
((uint16_t *)(*(dn_tmp_10)).data); });
uint32_t dn_tmp_11=((uint32_t)(0x5001000bULL));
int32_t dn_tmp_12=((int32_t)(0ULL));
int32_t dn_tmp_13=((int32_t)(0ULL));
int32_t dn_tmp_14=((int32_t)(10ULL));
int32_t dn_tmp_15=((int32_t)(10ULL));
void * dn_tmp_16=(v_g)->f_window;
size_t dn_tmp_17=v_id;
void * dn_tmp_18=({ uint16_t * dn_tmp_19=({ ((uint16_t *)NULL); });
({ void * dn_foreign=dn_ext_GetModuleHandleW(dn_tmp_19);dn_fp_default();dn_foreign; }); });
void * dn_tmp_20=({ ((void *){0}); });
({ void * dn_foreign=dn_ext_CreateWindowExW(dn_tmp_6,dn_tmp_7,dn_tmp_9,dn_tmp_11,dn_tmp_12,dn_tmp_13,dn_tmp_14,dn_tmp_15,dn_tmp_16,dn_tmp_17,dn_tmp_18,dn_tmp_20);dn_fp_default();dn_foreign; }); });
(void)(({ bool dn_tmp_21=({ void * dn_tmp_22=v_child;
dn_f_handle_valid(dn_tmp_22); });
(dn_assert(dn_tmp_21),0); }));
(void)(({ void * dn_tmp_23=v_child;
uint32_t dn_tmp_24=((uint32_t)(0x30ULL));
size_t dn_tmp_25=({ void * dn_tmp_26=(v_g)->f_font;
((size_t)(uintptr_t)dn_tmp_26); });
size_t dn_tmp_27=((size_t)(0ULL));
({ int64_t dn_foreign=dn_ext_SendMessageW(dn_tmp_23,dn_tmp_24,dn_tmp_25,dn_tmp_27);dn_fp_default();dn_foreign; }); }));
(void)(({ dn_t_buf_r7_Control * dn_tmp_28=(&((v_g)->f_controls));
dn_r_Control dn_tmp_29=({ void * dn_tmp_30=v_child;
size_t dn_tmp_31=v_id;
int32_t dn_tmp_32=v_x;
int32_t dn_tmp_33=v_y;
int32_t dn_tmp_34=v_width;
int32_t dn_tmp_35=v_height;
((dn_r_Control){dn_tmp_30,dn_tmp_31,dn_tmp_32,dn_tmp_33,dn_tmp_34,dn_tmp_35}); });
dn_push_buf_r7_Control(dn_tmp_28,dn_tmp_29); }));
dn_drop_buf_U16(&v_text);
dn_drop_buf_U16(&v_class_);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_add_button(dn_r_Gui * v_g,dn_utf8 v_title,size_t v_id,int32_t v_x,int32_t v_y,int32_t v_width,int32_t v_height) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_add_button(v_g,v_title,v_id,v_x,v_y,v_width,v_height);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint32_t dn_f_selected_target(dn_r_Gui const * v_g) {
{
int64_t v_selected=({ void * dn_tmp_0=({ dn_r_Gui const * dn_tmp_1=v_g;
size_t dn_tmp_2=((size_t)(20ULL));
dn_f_control(dn_tmp_1,dn_tmp_2); });
uint32_t dn_tmp_3=((uint32_t)(0x147ULL));
size_t dn_tmp_4=((size_t)(0ULL));
size_t dn_tmp_5=((size_t)(0ULL));
({ int64_t dn_foreign=dn_ext_SendMessageW(dn_tmp_0,dn_tmp_3,dn_tmp_4,dn_tmp_5);dn_fp_default();dn_foreign; }); });
if((({ int64_t dn_tmp_6=v_selected;
int64_t dn_tmp_7=({ int64_t dn_tmp_8=((int64_t)(0ULL));
if((__int128)dn_tmp_8<(-((__int128)1<<63))||(__int128)dn_tmp_8>(((__int128)1<<63)-1))dn_fail("numeric conversion out of range");(int64_t)dn_tmp_8; });
(dn_tmp_6 < dn_tmp_7); }) || ({ int64_t dn_tmp_9=v_selected;
int64_t dn_tmp_10=({ int64_t dn_tmp_11=((int64_t)(6ULL));
if((__int128)dn_tmp_11<(-((__int128)1<<63))||(__int128)dn_tmp_11>(((__int128)1<<63)-1))dn_fail("numeric conversion out of range");(int64_t)dn_tmp_11; });
(dn_tmp_9 > dn_tmp_10); }))){
{ uint32_t dn_tmp_12=((uint32_t)(5ULL));return dn_tmp_12; }
}
else{
}
{ uint32_t dn_tmp_14=({ int64_t dn_tmp_13=v_selected;
if((__int128)dn_tmp_13<0||(__int128)dn_tmp_13>(((__int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_13; });return dn_tmp_14; }
}

dn_fail("missing native return");
}
static uint32_t dn_cb_selected_target(dn_r_Gui const * v_g) {
 if(!dn_enter()){return (uint32_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (uint32_t){0};}
 uint32_t dn_result=dn_f_selected_target(v_g);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_gui_build(dn_r_Gui * v_g) {
{
dn_t_buf_U16 volatile v_combo_class={0};
dn_t_buf_U16 volatile v_empty={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U16(&v_empty);
dn_drop_buf_U16(&v_combo_class);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
(void)(({ dn_r_Gui * dn_tmp_2=v_g;
dn_utf8 dn_tmp_3=((dn_utf8){(const uint8_t*)"\320\222\320\272\320\273\321\216\321\207\320\270\321\202\321\214", 16});
size_t dn_tmp_4=((size_t)(10ULL));
int32_t dn_tmp_5=((int32_t)(60ULL));
int32_t dn_tmp_6=((int32_t)(280ULL));
int32_t dn_tmp_7=((int32_t)(330ULL));
int32_t dn_tmp_8=((int32_t)(48ULL));
dn_f_add_button(dn_tmp_2,dn_tmp_3,dn_tmp_4,dn_tmp_5,dn_tmp_6,dn_tmp_7,dn_tmp_8); }));
(void)(({ dn_r_Gui * dn_tmp_9=v_g;
dn_utf8 dn_tmp_10=((dn_utf8){(const uint8_t*)"\320\236\320\261\321\203\321\207\320\260\321\202\321\214 10 \320\274\320\270\320\275\321\203\321\202", 28});
size_t dn_tmp_11=((size_t)(16ULL));
int32_t dn_tmp_12=((int32_t)(430ULL));
int32_t dn_tmp_13=((int32_t)(280ULL));
int32_t dn_tmp_14=((int32_t)(500ULL));
int32_t dn_tmp_15=((int32_t)(48ULL));
dn_f_add_button(dn_tmp_9,dn_tmp_10,dn_tmp_11,dn_tmp_12,dn_tmp_13,dn_tmp_14,dn_tmp_15); }));
(void)(({ dn_r_Gui * dn_tmp_16=v_g;
dn_utf8 dn_tmp_17=((dn_utf8){(const uint8_t*)"\320\237\321\200\320\276\320\262\320\265\321\200\320\270\321\202\321\214 \321\201\320\265\321\200\320\262\320\270\321\201", 31});
size_t dn_tmp_18=((size_t)(13ULL));
int32_t dn_tmp_19=((int32_t)(52ULL));
int32_t dn_tmp_20=((int32_t)(416ULL));
int32_t dn_tmp_21=((int32_t)(280ULL));
int32_t dn_tmp_22=((int32_t)(46ULL));
dn_f_add_button(dn_tmp_16,dn_tmp_17,dn_tmp_18,dn_tmp_19,dn_tmp_20,dn_tmp_21,dn_tmp_22); }));
(void)(({ dn_r_Gui * dn_tmp_23=v_g;
dn_utf8 dn_tmp_24=((dn_utf8){(const uint8_t*)"\320\241\320\276\320\261\321\200\320\260\321\202\321\214 \320\276\321\202\321\207\321\221\321\202", 25});
size_t dn_tmp_25=((size_t)(14ULL));
int32_t dn_tmp_26=((int32_t)(356ULL));
int32_t dn_tmp_27=((int32_t)(416ULL));
int32_t dn_tmp_28=((int32_t)(280ULL));
int32_t dn_tmp_29=((int32_t)(46ULL));
dn_f_add_button(dn_tmp_23,dn_tmp_24,dn_tmp_25,dn_tmp_26,dn_tmp_27,dn_tmp_28,dn_tmp_29); }));
(void)(({ dn_r_Gui * dn_tmp_30=v_g;
dn_utf8 dn_tmp_31=((dn_utf8){(const uint8_t*)"\320\226\321\203\321\200\320\275\320\260\320\273", 12});
size_t dn_tmp_32=((size_t)(15ULL));
int32_t dn_tmp_33=((int32_t)(660ULL));
int32_t dn_tmp_34=((int32_t)(416ULL));
int32_t dn_tmp_35=((int32_t)(280ULL));
int32_t dn_tmp_36=((int32_t)(46ULL));
dn_f_add_button(dn_tmp_30,dn_tmp_31,dn_tmp_32,dn_tmp_33,dn_tmp_34,dn_tmp_35,dn_tmp_36); }));
(void)(({ dn_r_Gui * dn_tmp_37=v_g;
dn_utf8 dn_tmp_38=((dn_utf8){(const uint8_t*)"\320\241\320\276\321\205\321\200\320\260\320\275\320\270\321\202\321\214 \320\277\321\200\320\276\321\204\320\270\320\273\321\214", 33});
size_t dn_tmp_39=((size_t)(17ULL));
int32_t dn_tmp_40=((int32_t)(52ULL));
int32_t dn_tmp_41=((int32_t)(478ULL));
int32_t dn_tmp_42=((int32_t)(430ULL));
int32_t dn_tmp_43=((int32_t)(42ULL));
dn_f_add_button(dn_tmp_37,dn_tmp_38,dn_tmp_39,dn_tmp_40,dn_tmp_41,dn_tmp_42,dn_tmp_43); }));
(void)(({ dn_r_Gui * dn_tmp_44=v_g;
dn_utf8 dn_tmp_45=((dn_utf8){(const uint8_t*)"\320\227\320\260\320\263\321\200\321\203\320\267\320\270\321\202\321\214 \320\277\321\200\320\276\321\204\320\270\320\273\321\214", 33});
size_t dn_tmp_46=((size_t)(18ULL));
int32_t dn_tmp_47=((int32_t)(512ULL));
int32_t dn_tmp_48=((int32_t)(478ULL));
int32_t dn_tmp_49=((int32_t)(430ULL));
int32_t dn_tmp_50=((int32_t)(42ULL));
dn_f_add_button(dn_tmp_44,dn_tmp_45,dn_tmp_46,dn_tmp_47,dn_tmp_48,dn_tmp_49,dn_tmp_50); }));
v_combo_class=({ dn_t_slice0_U8 dn_tmp_51=({ dn_utf8 dn_tmp_52=((dn_utf8){(const uint8_t*)"COMBOBOX", 8});
((dn_t_slice0_U8){dn_tmp_52.data,dn_tmp_52.len}); });
dn_f_wide(dn_tmp_51); });
v_empty=({ dn_t_slice0_U8 dn_tmp_53=({ dn_utf8 dn_tmp_54=((dn_utf8){(const uint8_t*)"", 0});
((dn_t_slice0_U8){dn_tmp_54.data,dn_tmp_54.len}); });
dn_f_wide(dn_tmp_53); });
void * v_combo=({ uint32_t dn_tmp_55=((uint32_t)(0ULL));
uint16_t * dn_tmp_56=({ dn_t_buf_U16 const * dn_tmp_57=(&(v_combo_class));
((uint16_t *)(*(dn_tmp_57)).data); });
uint16_t * dn_tmp_58=({ dn_t_buf_U16 const * dn_tmp_59=(&(v_empty));
((uint16_t *)(*(dn_tmp_59)).data); });
uint32_t dn_tmp_60=((uint32_t)(0x50210203ULL));
int32_t dn_tmp_61=((int32_t)(0ULL));
int32_t dn_tmp_62=((int32_t)(0ULL));
int32_t dn_tmp_63=((int32_t)(10ULL));
int32_t dn_tmp_64=((int32_t)(10ULL));
void * dn_tmp_65=(v_g)->f_window;
size_t dn_tmp_66=((size_t)(20ULL));
void * dn_tmp_67=({ uint16_t * dn_tmp_68=({ ((uint16_t *)NULL); });
({ void * dn_foreign=dn_ext_GetModuleHandleW(dn_tmp_68);dn_fp_default();dn_foreign; }); });
void * dn_tmp_69=({ ((void *){0}); });
({ void * dn_foreign=dn_ext_CreateWindowExW(dn_tmp_55,dn_tmp_56,dn_tmp_58,dn_tmp_60,dn_tmp_61,dn_tmp_62,dn_tmp_63,dn_tmp_64,dn_tmp_65,dn_tmp_66,dn_tmp_67,dn_tmp_69);dn_fp_default();dn_foreign; }); });
(void)(({ bool dn_tmp_70=({ void * dn_tmp_71=v_combo;
dn_f_handle_valid(dn_tmp_71); });
(dn_assert(dn_tmp_70),0); }));
(void)(({ void * dn_tmp_72=v_combo;
uint32_t dn_tmp_73=((uint32_t)(0x30ULL));
size_t dn_tmp_74=({ void * dn_tmp_75=(v_g)->f_font;
((size_t)(uintptr_t)dn_tmp_75); });
size_t dn_tmp_76=((size_t)(0ULL));
({ int64_t dn_foreign=dn_ext_SendMessageW(dn_tmp_72,dn_tmp_73,dn_tmp_74,dn_tmp_76);dn_fp_default();dn_foreign; }); }));
dn_t_arr7_str v_names=({ dn_utf8 dn_tmp_77=((dn_utf8){(const uint8_t*)"YouTube", 7});
dn_utf8 dn_tmp_78=((dn_utf8){(const uint8_t*)"Discord", 7});
dn_utf8 dn_tmp_79=((dn_utf8){(const uint8_t*)"YouTube + Discord", 17});
dn_utf8 dn_tmp_80=((dn_utf8){(const uint8_t*)"Telegram Web + Desktop", 22});
dn_utf8 dn_tmp_81=((dn_utf8){(const uint8_t*)"YouTube + Discord + Telegram", 28});
dn_utf8 dn_tmp_82=((dn_utf8){(const uint8_t*)"MWO + CDN / Cloudflare / Azure", 30});
dn_utf8 dn_tmp_83=((dn_utf8){(const uint8_t*)"\320\222\321\201\320\265 \321\201\320\265\321\200\320\262\320\270\321\201\321\213", 21});
((dn_t_arr7_str){{dn_tmp_77,dn_tmp_78,dn_tmp_79,dn_tmp_80,dn_tmp_81,dn_tmp_82,dn_tmp_83}}); });
{ size_t dn_tmp_85=((size_t)(0ULL));size_t dn_tmp_84=((size_t)(7ULL));for(size_t v_i=dn_tmp_85;v_i<dn_tmp_84;v_i++){
dn_t_buf_U16 volatile v_name={0};
dn_boundary dn_tmp_86={0};jmp_buf dn_tmp_87;dn_tmp_86.jump=&dn_tmp_87;dn_tmp_86.previous=dn_current;dn_tmp_86.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_86;if(setjmp(dn_tmp_87)){dn_current=dn_tmp_86.previous;dn_drop_buf_U16(&v_name);
dn_boundary_end(&dn_tmp_86,1);dn_raise();}
v_name=({ dn_t_slice0_U8 dn_tmp_88=({ dn_utf8 dn_tmp_89=(*({ dn_t_arr7_str *dn_tmp_90=&(v_names);size_t dn_tmp_91=v_i;&dn_tmp_90->data[dn_index(dn_tmp_91,7)]; }));
((dn_t_slice0_U8){dn_tmp_89.data,dn_tmp_89.len}); });
dn_f_wide(dn_tmp_88); });
(void)(({ void * dn_tmp_92=v_combo;
uint32_t dn_tmp_93=((uint32_t)(0x143ULL));
size_t dn_tmp_94=((size_t)(0ULL));
size_t dn_tmp_95=({ uint16_t * dn_tmp_96=({ dn_t_buf_U16 const * dn_tmp_97=(&(v_name));
((uint16_t *)(*(dn_tmp_97)).data); });
((size_t)(uintptr_t)dn_tmp_96); });
({ int64_t dn_foreign=dn_ext_SendMessageW(dn_tmp_92,dn_tmp_93,dn_tmp_94,dn_tmp_95);dn_fp_default();dn_foreign; }); }));
dn_drop_buf_U16(&v_name);
dn_boundary_end(&dn_tmp_86,0);
}
 }
(void)(({ void * dn_tmp_98=v_combo;
uint32_t dn_tmp_99=((uint32_t)(0x14eULL));
size_t dn_tmp_100=((size_t)(5ULL));
size_t dn_tmp_101=((size_t)(0ULL));
({ int64_t dn_foreign=dn_ext_SendMessageW(dn_tmp_98,dn_tmp_99,dn_tmp_100,dn_tmp_101);dn_fp_default();dn_foreign; }); }));
(void)(({ dn_t_buf_r7_Control * dn_tmp_102=(&((v_g)->f_controls));
dn_r_Control dn_tmp_103=({ void * dn_tmp_104=v_combo;
size_t dn_tmp_105=((size_t)(20ULL));
int32_t dn_tmp_106=((int32_t)(570ULL));
int32_t dn_tmp_107=((int32_t)(69ULL));
int32_t dn_tmp_108=((int32_t)(380ULL));
int32_t dn_tmp_109=((int32_t)(200ULL));
((dn_r_Control){dn_tmp_104,dn_tmp_105,dn_tmp_106,dn_tmp_107,dn_tmp_108,dn_tmp_109}); });
dn_push_buf_r7_Control(dn_tmp_102,dn_tmp_103); }));
(void)(({ dn_r_Gui * dn_tmp_110=v_g;
dn_f_gui_layout(dn_tmp_110); }));
dn_drop_buf_U16(&v_empty);
dn_drop_buf_U16(&v_combo_class);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_gui_build(dn_r_Gui * v_g) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_gui_build(v_g);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_gui_draw_button(dn_r_Gui const * v_g,dn_r_DrawItem const * v_item) {
{
dn_t_buf_U16 volatile v_w={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U16(&v_w);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
void * v_brush=(v_g)->f_secondary;
uint32_t v_color=({ uint32_t dn_tmp_2=((uint32_t)(229ULL));
uint32_t dn_tmp_3=((uint32_t)(237ULL));
uint32_t dn_tmp_4=((uint32_t)(249ULL));
dn_f_rgb(dn_tmp_2,dn_tmp_3,dn_tmp_4); });
if(({ uint32_t dn_tmp_5=(v_item)->f_control_id;
uint32_t dn_tmp_6=({ uint32_t dn_tmp_7=((uint32_t)(10ULL));
if((unsigned __int128)dn_tmp_7>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_7; });
(dn_tmp_5 == dn_tmp_6); })){
{ void * dn_tmp_9=(v_g)->f_accent;void * *dn_tmp_8=&(v_brush);*dn_tmp_8=dn_tmp_9; }
{ uint32_t dn_tmp_14=({ uint32_t dn_tmp_10=((uint32_t)(255ULL));
uint32_t dn_tmp_11=((uint32_t)(255ULL));
uint32_t dn_tmp_12=((uint32_t)(255ULL));
dn_f_rgb(dn_tmp_10,dn_tmp_11,dn_tmp_12); });uint32_t *dn_tmp_13=&(v_color);*dn_tmp_13=dn_tmp_14; }
}
else{
}
if(({ uint32_t dn_tmp_15=({ uint32_t dn_tmp_16=(v_item)->f_state;
uint32_t dn_tmp_17=({ uint32_t dn_tmp_18=((uint32_t)(4ULL));
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_18; });
dn_U32_bits((uint64_t)dn_tmp_16 & (uint64_t)dn_tmp_17); });
uint32_t dn_tmp_19=({ uint32_t dn_tmp_20=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_20>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_20; });
(dn_tmp_15 != dn_tmp_19); })){
{ void * dn_tmp_22=(v_g)->f_card;void * *dn_tmp_21=&(v_brush);*dn_tmp_21=dn_tmp_22; }
{ uint32_t dn_tmp_27=({ uint32_t dn_tmp_23=((uint32_t)(115ULL));
uint32_t dn_tmp_24=((uint32_t)(133ULL));
uint32_t dn_tmp_25=((uint32_t)(157ULL));
dn_f_rgb(dn_tmp_23,dn_tmp_24,dn_tmp_25); });uint32_t *dn_tmp_26=&(v_color);*dn_tmp_26=dn_tmp_27; }
}
else{
}
if(({ uint32_t dn_tmp_28=({ uint32_t dn_tmp_29=(v_item)->f_state;
uint32_t dn_tmp_30=({ uint32_t dn_tmp_31=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_31>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_31; });
dn_U32_bits((uint64_t)dn_tmp_29 & (uint64_t)dn_tmp_30); });
uint32_t dn_tmp_32=({ uint32_t dn_tmp_33=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_33>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_33; });
(dn_tmp_28 != dn_tmp_32); })){
{ void * dn_tmp_35=(v_g)->f_card;void * *dn_tmp_34=&(v_brush);*dn_tmp_34=dn_tmp_35; }
}
else{
}
(void)(({ void * dn_tmp_36=(v_item)->f_dc;
dn_r_Rect dn_tmp_37=(v_item)->f_rect;
void * dn_tmp_38=v_brush;
int32_t dn_tmp_39=({ dn_r_Gui const * dn_tmp_40=v_g;
int32_t dn_tmp_41=((int32_t)(12ULL));
dn_f_scale(dn_tmp_40,dn_tmp_41); });
dn_f_rounded(dn_tmp_36,dn_tmp_37,dn_tmp_38,dn_tmp_39); }));
void * v_old=({ void * dn_tmp_42=(v_item)->f_dc;
void * dn_tmp_43=(v_g)->f_font;
({ void * dn_foreign=dn_ext_SelectObject(dn_tmp_42,dn_tmp_43);dn_fp_default();dn_foreign; }); });
(void)(({ void * dn_tmp_44=(v_item)->f_dc;
int32_t dn_tmp_45=((int32_t)(1ULL));
({ int32_t dn_foreign=dn_ext_SetBkMode(dn_tmp_44,dn_tmp_45);dn_fp_default();dn_foreign; }); }));
(void)(({ void * dn_tmp_46=(v_item)->f_dc;
uint32_t dn_tmp_47=v_color;
({ uint32_t dn_foreign=dn_ext_SetTextColor(dn_tmp_46,dn_tmp_47);dn_fp_default();dn_foreign; }); }));
dn_utf8 v_text=((dn_utf8){(const uint8_t*)"\320\222\320\272\320\273\321\216\321\207\320\270\321\202\321\214", 16});
if((({ uint32_t dn_tmp_48=(v_item)->f_control_id;
uint32_t dn_tmp_49=({ uint32_t dn_tmp_50=((uint32_t)(10ULL));
if((unsigned __int128)dn_tmp_50>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_50; });
(dn_tmp_48 == dn_tmp_49); }) && (({ uint32_t dn_tmp_51=(v_g)->f_phase;
uint32_t dn_tmp_52=({ uint32_t dn_tmp_53=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_53>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_53; });
(dn_tmp_51 == dn_tmp_52); }) || ({ uint32_t dn_tmp_54=(v_g)->f_phase;
uint32_t dn_tmp_55=({ uint32_t dn_tmp_56=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_56>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_56; });
(dn_tmp_54 == dn_tmp_55); })))){
{ dn_utf8 dn_tmp_58=((dn_utf8){(const uint8_t*)"\320\236\321\202\320\274\320\265\320\275\320\270\321\202\321\214", 16});dn_utf8 *dn_tmp_57=&(v_text);*dn_tmp_57=dn_tmp_58; }
}
else{
if((({ uint32_t dn_tmp_59=(v_item)->f_control_id;
uint32_t dn_tmp_60=({ uint32_t dn_tmp_61=((uint32_t)(10ULL));
if((unsigned __int128)dn_tmp_61>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_61; });
(dn_tmp_59 == dn_tmp_60); }) && ({ void * dn_tmp_62=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_63=(&((v_g)->f_process));
(&((*dn_tmp_63)->value)); })))));
dn_f_handle_valid(dn_tmp_62); }))){
{ dn_utf8 dn_tmp_65=((dn_utf8){(const uint8_t*)"\320\236\321\202\320\272\320\273\321\216\321\207\320\270\321\202\321\214", 18});dn_utf8 *dn_tmp_64=&(v_text);*dn_tmp_64=dn_tmp_65; }
}
else{
}
}
if(({ uint32_t dn_tmp_66=(v_item)->f_control_id;
uint32_t dn_tmp_67=({ uint32_t dn_tmp_68=((uint32_t)(13ULL));
if((unsigned __int128)dn_tmp_68>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_68; });
(dn_tmp_66 == dn_tmp_67); })){
{ dn_utf8 dn_tmp_70=((dn_utf8){(const uint8_t*)"\320\237\321\200\320\276\320\262\320\265\321\200\320\270\321\202\321\214 \321\201\320\265\321\200\320\262\320\270\321\201", 31});dn_utf8 *dn_tmp_69=&(v_text);*dn_tmp_69=dn_tmp_70; }
if(({ uint32_t dn_tmp_71=(v_g)->f_phase;
uint32_t dn_tmp_72=({ uint32_t dn_tmp_73=((uint32_t)(6ULL));
if((unsigned __int128)dn_tmp_73>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_73; });
(dn_tmp_71 == dn_tmp_72); })){
{ dn_utf8 dn_tmp_75=((dn_utf8){(const uint8_t*)"\320\237\321\200\320\276\320\262\320\265\321\200\320\270\321\202\321\214 \320\264\321\200\320\260\320\271\320\262\320\265\321\200", 33});dn_utf8 *dn_tmp_74=&(v_text);*dn_tmp_74=dn_tmp_75; }
}
else{
}
}
else{
}
if(({ uint32_t dn_tmp_76=(v_item)->f_control_id;
uint32_t dn_tmp_77=({ uint32_t dn_tmp_78=((uint32_t)(14ULL));
if((unsigned __int128)dn_tmp_78>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_78; });
(dn_tmp_76 == dn_tmp_77); })){
{ dn_utf8 dn_tmp_80=((dn_utf8){(const uint8_t*)"\320\241\320\276\320\261\321\200\320\260\321\202\321\214 \320\276\321\202\321\207\321\221\321\202", 25});dn_utf8 *dn_tmp_79=&(v_text);*dn_tmp_79=dn_tmp_80; }
}
else{
}
if(({ uint32_t dn_tmp_81=(v_item)->f_control_id;
uint32_t dn_tmp_82=({ uint32_t dn_tmp_83=((uint32_t)(15ULL));
if((unsigned __int128)dn_tmp_83>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_83; });
(dn_tmp_81 == dn_tmp_82); })){
{ dn_utf8 dn_tmp_85=((dn_utf8){(const uint8_t*)"\320\226\321\203\321\200\320\275\320\260\320\273", 12});dn_utf8 *dn_tmp_84=&(v_text);*dn_tmp_84=dn_tmp_85; }
}
else{
}
if(({ uint32_t dn_tmp_86=(v_item)->f_control_id;
uint32_t dn_tmp_87=({ uint32_t dn_tmp_88=((uint32_t)(16ULL));
if((unsigned __int128)dn_tmp_88>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_88; });
(dn_tmp_86 == dn_tmp_87); })){
{ dn_utf8 dn_tmp_90=((dn_utf8){(const uint8_t*)"\320\236\320\261\321\203\321\207\320\260\321\202\321\214 10 \320\274\320\270\320\275\321\203\321\202", 28});dn_utf8 *dn_tmp_89=&(v_text);*dn_tmp_89=dn_tmp_90; }
if(({ void * dn_tmp_91=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_92=(&((v_g)->f_training));
(&((*dn_tmp_92)->value)); })))));
dn_f_handle_valid(dn_tmp_91); })){
{ dn_utf8 dn_tmp_94=((dn_utf8){(const uint8_t*)"\320\236\321\201\321\202\320\260\320\275\320\276\320\262\320\270\321\202\321\214 \320\277\320\276\320\270\321\201\320\272", 31});dn_utf8 *dn_tmp_93=&(v_text);*dn_tmp_93=dn_tmp_94; }
}
else{
}
}
else{
}
if(({ uint32_t dn_tmp_95=(v_item)->f_control_id;
uint32_t dn_tmp_96=({ uint32_t dn_tmp_97=((uint32_t)(17ULL));
if((unsigned __int128)dn_tmp_97>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_97; });
(dn_tmp_95 == dn_tmp_96); })){
{ dn_utf8 dn_tmp_99=((dn_utf8){(const uint8_t*)"\320\241\320\276\321\205\321\200\320\260\320\275\320\270\321\202\321\214 \320\277\321\200\320\276\321\204\320\270\320\273\321\214", 33});dn_utf8 *dn_tmp_98=&(v_text);*dn_tmp_98=dn_tmp_99; }
}
else{
}
if(({ uint32_t dn_tmp_100=(v_item)->f_control_id;
uint32_t dn_tmp_101=({ uint32_t dn_tmp_102=((uint32_t)(18ULL));
if((unsigned __int128)dn_tmp_102>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_102; });
(dn_tmp_100 == dn_tmp_101); })){
{ dn_utf8 dn_tmp_104=((dn_utf8){(const uint8_t*)"\320\227\320\260\320\263\321\200\321\203\320\267\320\270\321\202\321\214 \320\277\321\200\320\276\321\204\320\270\320\273\321\214", 33});dn_utf8 *dn_tmp_103=&(v_text);*dn_tmp_103=dn_tmp_104; }
}
else{
}
v_w=({ dn_t_slice0_U8 dn_tmp_105=({ dn_utf8 dn_tmp_106=v_text;
((dn_t_slice0_U8){dn_tmp_106.data,dn_tmp_106.len}); });
dn_f_wide(dn_tmp_105); });
dn_r_Rect v_rect=(v_item)->f_rect;
(void)(({ void * dn_tmp_107=(v_item)->f_dc;
uint16_t * dn_tmp_108=({ dn_t_buf_U16 const * dn_tmp_109=(&(v_w));
((uint16_t *)(*(dn_tmp_109)).data); });
int32_t dn_tmp_110=dn_I32_neg(((int32_t)(1ULL)));
dn_r_Rect * dn_tmp_111=({ dn_r_Rect * dn_tmp_112=({ dn_r_Rect * dn_tmp_113=(&(v_rect));
((dn_r_Rect *)dn_tmp_113); });
((dn_r_Rect *)dn_tmp_112); });
uint32_t dn_tmp_114=((uint32_t)(0x825ULL));
({ int32_t dn_foreign=dn_ext_DrawTextW(dn_tmp_107,dn_tmp_108,dn_tmp_110,dn_tmp_111,dn_tmp_114);dn_fp_default();dn_foreign; }); }));
if(({ uint32_t dn_tmp_115=({ uint32_t dn_tmp_116=(v_item)->f_state;
uint32_t dn_tmp_117=({ uint32_t dn_tmp_118=((uint32_t)(16ULL));
if((unsigned __int128)dn_tmp_118>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_118; });
dn_U32_bits((uint64_t)dn_tmp_116 & (uint64_t)dn_tmp_117); });
uint32_t dn_tmp_119=({ uint32_t dn_tmp_120=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_120>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_120; });
(dn_tmp_115 != dn_tmp_119); })){
{ int32_t dn_tmp_125=({ int32_t dn_tmp_121=(v_rect).f_left;
int32_t dn_tmp_122=({ int32_t dn_tmp_123=((int32_t)(4ULL));
if((__int128)dn_tmp_123<(-((__int128)1<<31))||(__int128)dn_tmp_123>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_123; });
dn_I32_add(dn_tmp_121,dn_tmp_122); });int32_t *dn_tmp_124=&((v_rect).f_left);*dn_tmp_124=dn_tmp_125; }
{ int32_t dn_tmp_130=({ int32_t dn_tmp_126=(v_rect).f_top;
int32_t dn_tmp_127=({ int32_t dn_tmp_128=((int32_t)(4ULL));
if((__int128)dn_tmp_128<(-((__int128)1<<31))||(__int128)dn_tmp_128>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_128; });
dn_I32_add(dn_tmp_126,dn_tmp_127); });int32_t *dn_tmp_129=&((v_rect).f_top);*dn_tmp_129=dn_tmp_130; }
{ int32_t dn_tmp_135=({ int32_t dn_tmp_131=(v_rect).f_right;
int32_t dn_tmp_132=({ int32_t dn_tmp_133=((int32_t)(4ULL));
if((__int128)dn_tmp_133<(-((__int128)1<<31))||(__int128)dn_tmp_133>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_133; });
dn_I32_sub(dn_tmp_131,dn_tmp_132); });int32_t *dn_tmp_134=&((v_rect).f_right);*dn_tmp_134=dn_tmp_135; }
{ int32_t dn_tmp_140=({ int32_t dn_tmp_136=(v_rect).f_bottom;
int32_t dn_tmp_137=({ int32_t dn_tmp_138=((int32_t)(4ULL));
if((__int128)dn_tmp_138<(-((__int128)1<<31))||(__int128)dn_tmp_138>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_138; });
dn_I32_sub(dn_tmp_136,dn_tmp_137); });int32_t *dn_tmp_139=&((v_rect).f_bottom);*dn_tmp_139=dn_tmp_140; }
(void)(({ void * dn_tmp_141=(v_item)->f_dc;
dn_r_Rect * dn_tmp_142=({ dn_r_Rect * dn_tmp_143=({ dn_r_Rect * dn_tmp_144=(&(v_rect));
((dn_r_Rect *)dn_tmp_144); });
((dn_r_Rect *)dn_tmp_143); });
({ int32_t dn_foreign=dn_ext_DrawFocusRect(dn_tmp_141,dn_tmp_142);dn_fp_default();dn_foreign; }); }));
}
else{
}
(void)(({ void * dn_tmp_145=(v_item)->f_dc;
void * dn_tmp_146=v_old;
({ void * dn_foreign=dn_ext_SelectObject(dn_tmp_145,dn_tmp_146);dn_fp_default();dn_foreign; }); }));
dn_drop_buf_U16(&v_w);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_gui_draw_button(dn_r_Gui const * v_g,dn_r_DrawItem const * v_item) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_gui_draw_button(v_g,v_item);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static void * dn_f_launch(dn_r_Gui const * v_g,dn_t_slice0_U8 v_filename,dn_t_slice0_U8 v_parameters,bool v_elevate,int32_t v_show) {
{
dn_t_buf_U8 volatile v_own={0};
dn_t_buf_U8 volatile v_dir={0};
dn_t_buf_U16 volatile v_file={0};
dn_t_buf_U16 volatile v_directory={0};
dn_t_buf_U16 volatile v_args={0};
dn_t_buf_U16 volatile v_verb={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U16(&v_verb);
dn_drop_buf_U16(&v_args);
dn_drop_buf_U16(&v_directory);
dn_drop_buf_U16(&v_file);
dn_drop_buf_U8(&v_dir);
dn_drop_buf_U8(&v_own);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_own=({ dn_f_executable_path(); });
v_dir=({ dn_t_slice0_U8 dn_tmp_2=({ dn_t_buf_U8 const * dn_tmp_3=(&(v_own));
size_t dn_tmp_4=((size_t)(0ULL));
size_t dn_tmp_5=({ dn_t_buf_U8 const * dn_tmp_6=(&(v_own));
(*(dn_tmp_6)).len; });
({ dn_range(dn_tmp_4,dn_tmp_5,(*(dn_tmp_3)).len);(dn_t_slice0_U8){(*(dn_tmp_3)).data?(*(dn_tmp_3)).data+dn_tmp_4:NULL,dn_tmp_5}; }); });
dn_f_directory_path(dn_tmp_2); });
v_file=({ dn_t_slice0_U8 dn_tmp_7=v_filename;
dn_f_wide(dn_tmp_7); });
v_directory=({ dn_t_slice0_U8 dn_tmp_8=({ dn_t_buf_U8 const * dn_tmp_9=(&(v_dir));
size_t dn_tmp_10=((size_t)(0ULL));
size_t dn_tmp_11=({ dn_t_buf_U8 const * dn_tmp_12=(&(v_dir));
(*(dn_tmp_12)).len; });
({ dn_range(dn_tmp_10,dn_tmp_11,(*(dn_tmp_9)).len);(dn_t_slice0_U8){(*(dn_tmp_9)).data?(*(dn_tmp_9)).data+dn_tmp_10:NULL,dn_tmp_11}; }); });
dn_f_wide(dn_tmp_8); });
v_args=({ dn_t_slice0_U8 dn_tmp_13=v_parameters;
dn_f_wide(dn_tmp_13); });
dn_utf8 v_verb_name=((dn_utf8){(const uint8_t*)"open", 4});
if(v_elevate){
{ dn_utf8 dn_tmp_15=((dn_utf8){(const uint8_t*)"runas", 5});dn_utf8 *dn_tmp_14=&(v_verb_name);*dn_tmp_14=dn_tmp_15; }
}
else{
}
v_verb=({ dn_t_slice0_U8 dn_tmp_16=({ dn_utf8 dn_tmp_17=v_verb_name;
((dn_t_slice0_U8){dn_tmp_17.data,dn_tmp_17.len}); });
dn_f_wide(dn_tmp_16); });
dn_r_ShellExecute v_execute=({ ((dn_r_ShellExecute){0}); });
{ uint32_t dn_tmp_20=({ size_t dn_tmp_18=({ ((size_t)sizeof(dn_r_ShellExecute)); });
if((unsigned __int128)dn_tmp_18>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_18; });uint32_t *dn_tmp_19=&((v_execute).f_size);*dn_tmp_19=dn_tmp_20; }
{ uint32_t dn_tmp_22=((uint32_t)(0x40ULL));uint32_t *dn_tmp_21=&((v_execute).f_mask);*dn_tmp_21=dn_tmp_22; }
{ void * dn_tmp_24=(v_g)->f_window;void * *dn_tmp_23=&((v_execute).f_window);*dn_tmp_23=dn_tmp_24; }
{ uint16_t * dn_tmp_27=({ dn_t_buf_U16 const * dn_tmp_25=(&(v_verb));
((uint16_t *)(*(dn_tmp_25)).data); });uint16_t * *dn_tmp_26=&((v_execute).f_verb);*dn_tmp_26=dn_tmp_27; }
{ uint16_t * dn_tmp_30=({ dn_t_buf_U16 const * dn_tmp_28=(&(v_file));
((uint16_t *)(*(dn_tmp_28)).data); });uint16_t * *dn_tmp_29=&((v_execute).f_file);*dn_tmp_29=dn_tmp_30; }
{ uint16_t * dn_tmp_33=({ dn_t_buf_U16 const * dn_tmp_31=(&(v_args));
((uint16_t *)(*(dn_tmp_31)).data); });uint16_t * *dn_tmp_32=&((v_execute).f_parameters);*dn_tmp_32=dn_tmp_33; }
{ uint16_t * dn_tmp_36=({ dn_t_buf_U16 const * dn_tmp_34=(&(v_directory));
((uint16_t *)(*(dn_tmp_34)).data); });uint16_t * *dn_tmp_35=&((v_execute).f_directory);*dn_tmp_35=dn_tmp_36; }
{ int32_t dn_tmp_38=v_show;int32_t *dn_tmp_37=&((v_execute).f_show);*dn_tmp_37=dn_tmp_38; }
if(({ int32_t dn_tmp_39=({ dn_r_ShellExecute * dn_tmp_40=({ dn_r_ShellExecute * dn_tmp_41=({ dn_r_ShellExecute * dn_tmp_42=(&(v_execute));
((dn_r_ShellExecute *)dn_tmp_42); });
((dn_r_ShellExecute *)dn_tmp_41); });
({ int32_t dn_foreign=dn_ext_ShellExecuteExW(dn_tmp_40);dn_fp_default();dn_foreign; }); });
int32_t dn_tmp_43=({ int32_t dn_tmp_44=((int32_t)(0ULL));
if((__int128)dn_tmp_44<(-((__int128)1<<31))||(__int128)dn_tmp_44>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_44; });
(dn_tmp_39 == dn_tmp_43); })){
{ void * dn_tmp_45=({ ((void *){0}); });dn_drop_buf_U16(&v_verb);
dn_drop_buf_U16(&v_args);
dn_drop_buf_U16(&v_directory);
dn_drop_buf_U16(&v_file);
dn_drop_buf_U8(&v_dir);
dn_drop_buf_U8(&v_own);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_45; }
}
else{
}
{ void * dn_tmp_46=(v_execute).f_process;dn_drop_buf_U16(&v_verb);
dn_drop_buf_U16(&v_args);
dn_drop_buf_U16(&v_directory);
dn_drop_buf_U16(&v_file);
dn_drop_buf_U8(&v_dir);
dn_drop_buf_U8(&v_own);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_46; }
dn_drop_buf_U16(&v_verb);
dn_drop_buf_U16(&v_args);
dn_drop_buf_U16(&v_directory);
dn_drop_buf_U16(&v_file);
dn_drop_buf_U8(&v_dir);
dn_drop_buf_U8(&v_own);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static void * dn_cb_launch(dn_r_Gui const * v_g,dn_t_slice0_U8 v_filename,dn_t_slice0_U8 v_parameters,bool v_elevate,int32_t v_show) {
 if(!dn_enter()){return (void *){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (void *){0};}
 void * dn_result=dn_f_launch(v_g,v_filename,v_parameters,v_elevate,v_show);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static dn_t_buf_U8 dn_f_local_path(dn_utf8 v_name) {
{
dn_t_buf_U8 volatile v_own={0};
dn_t_buf_U8 volatile v_path={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_path);
dn_drop_buf_U8(&v_own);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_own=({ dn_f_executable_path(); });
v_path=({ dn_t_slice0_U8 dn_tmp_2=({ dn_t_buf_U8 const * dn_tmp_3=(&(v_own));
size_t dn_tmp_4=((size_t)(0ULL));
size_t dn_tmp_5=({ dn_t_buf_U8 const * dn_tmp_6=(&(v_own));
(*(dn_tmp_6)).len; });
({ dn_range(dn_tmp_4,dn_tmp_5,(*(dn_tmp_3)).len);(dn_t_slice0_U8){(*(dn_tmp_3)).data?(*(dn_tmp_3)).data+dn_tmp_4:NULL,dn_tmp_5}; }); });
dn_f_directory_path(dn_tmp_2); });
(void)(({ dn_t_buf_U8 * dn_tmp_7=(&(v_path));
uint8_t dn_tmp_8=({ uint8_t dn_tmp_9=((uint8_t)(92ULL));
if((unsigned __int128)dn_tmp_9>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_9; });
dn_push_buf_U8(dn_tmp_7,dn_tmp_8); }));
(void)(({ dn_t_buf_U8 * dn_tmp_10=(&(v_path));
dn_t_slice0_U8 dn_tmp_11=({ dn_utf8 dn_tmp_12=v_name;
((dn_t_slice0_U8){dn_tmp_12.data,dn_tmp_12.len}); });
dn_f_bytes_append(dn_tmp_10,dn_tmp_11); }));
{ dn_t_buf_U8 dn_tmp_14=({ dn_t_buf_U8 dn_tmp_13=v_path;v_path=(dn_t_buf_U8){0};dn_adopt_buf_U8(&dn_tmp_13);dn_tmp_13; });dn_adopt_buf_U8(&dn_tmp_14);dn_drop_buf_U8(&v_path);
dn_drop_buf_U8(&v_own);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_14; }
dn_drop_buf_U8(&v_path);
dn_drop_buf_U8(&v_own);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_t_buf_U8 dn_cb_local_path(dn_utf8 v_name) {
 if(!dn_enter()){return (dn_t_buf_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_buf_U8){0};}
 dn_t_buf_U8 dn_result=dn_f_local_path(v_name);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_running_ui(dn_r_Gui const * v_g) {
{
dn_t_buf_U16 volatile v_text={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U16(&v_text);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
dn_utf8 v_label=((dn_utf8){(const uint8_t*)"\320\222\320\272\320\273\321\216\321\207\320\270\321\202\321\214", 16});
if(({ void * dn_tmp_2=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_3=(&((v_g)->f_process));
(&((*dn_tmp_3)->value)); })))));
dn_f_handle_valid(dn_tmp_2); })){
{ dn_utf8 dn_tmp_5=((dn_utf8){(const uint8_t*)"\320\236\321\202\320\272\320\273\321\216\321\207\320\270\321\202\321\214", 18});dn_utf8 *dn_tmp_4=&(v_label);*dn_tmp_4=dn_tmp_5; }
}
else{
}
if((({ uint32_t dn_tmp_6=(v_g)->f_phase;
uint32_t dn_tmp_7=({ uint32_t dn_tmp_8=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_8>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_8; });
(dn_tmp_6 == dn_tmp_7); }) || ({ uint32_t dn_tmp_9=(v_g)->f_phase;
uint32_t dn_tmp_10=({ uint32_t dn_tmp_11=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_11>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_11; });
(dn_tmp_9 == dn_tmp_10); }))){
{ dn_utf8 dn_tmp_13=((dn_utf8){(const uint8_t*)"\320\236\321\202\320\274\320\265\320\275\320\270\321\202\321\214", 16});dn_utf8 *dn_tmp_12=&(v_label);*dn_tmp_12=dn_tmp_13; }
}
else{
}
v_text=({ dn_t_slice0_U8 dn_tmp_14=({ dn_utf8 dn_tmp_15=v_label;
((dn_t_slice0_U8){dn_tmp_15.data,dn_tmp_15.len}); });
dn_f_wide(dn_tmp_14); });
(void)(({ void * dn_tmp_16=({ dn_r_Gui const * dn_tmp_17=v_g;
size_t dn_tmp_18=((size_t)(10ULL));
dn_f_control(dn_tmp_17,dn_tmp_18); });
uint16_t * dn_tmp_19=({ dn_t_buf_U16 const * dn_tmp_20=(&(v_text));
((uint16_t *)(*(dn_tmp_20)).data); });
({ int32_t dn_foreign=dn_ext_SetWindowTextW(dn_tmp_16,dn_tmp_19);dn_fp_default();dn_foreign; }); }));
int32_t v_enable=((int32_t)(1ULL));
if((({ uint32_t dn_tmp_21=(v_g)->f_phase;
uint32_t dn_tmp_22=({ uint32_t dn_tmp_23=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_23>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_23; });
(dn_tmp_21 == dn_tmp_22); }) || ({ uint32_t dn_tmp_24=(v_g)->f_phase;
uint32_t dn_tmp_25=({ uint32_t dn_tmp_26=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_26>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_26; });
(dn_tmp_24 == dn_tmp_25); }))){
{ int32_t dn_tmp_28=((int32_t)(0ULL));int32_t *dn_tmp_27=&(v_enable);*dn_tmp_27=dn_tmp_28; }
}
else{
}
if(({ void * dn_tmp_29=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_30=(&((v_g)->f_training));
(&((*dn_tmp_30)->value)); })))));
dn_f_handle_valid(dn_tmp_29); })){
{ int32_t dn_tmp_32=((int32_t)(0ULL));int32_t *dn_tmp_31=&(v_enable);*dn_tmp_31=dn_tmp_32; }
}
else{
}
int32_t v_toggle=((int32_t)(1ULL));
if(({ void * dn_tmp_33=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_34=(&((v_g)->f_training));
(&((*dn_tmp_34)->value)); })))));
dn_f_handle_valid(dn_tmp_33); })){
{ int32_t dn_tmp_36=((int32_t)(0ULL));int32_t *dn_tmp_35=&(v_toggle);*dn_tmp_35=dn_tmp_36; }
}
else{
}
(void)(({ void * dn_tmp_37=({ dn_r_Gui const * dn_tmp_38=v_g;
size_t dn_tmp_39=((size_t)(10ULL));
dn_f_control(dn_tmp_38,dn_tmp_39); });
int32_t dn_tmp_40=v_toggle;
({ int32_t dn_foreign=dn_ext_EnableWindow(dn_tmp_37,dn_tmp_40);dn_fp_default();dn_foreign; }); }));
(void)(({ void * dn_tmp_41=({ dn_r_Gui const * dn_tmp_42=v_g;
size_t dn_tmp_43=((size_t)(13ULL));
dn_f_control(dn_tmp_42,dn_tmp_43); });
int32_t dn_tmp_44=v_enable;
({ int32_t dn_foreign=dn_ext_EnableWindow(dn_tmp_41,dn_tmp_44);dn_fp_default();dn_foreign; }); }));
(void)(({ void * dn_tmp_45=({ dn_r_Gui const * dn_tmp_46=v_g;
size_t dn_tmp_47=((size_t)(20ULL));
dn_f_control(dn_tmp_46,dn_tmp_47); });
int32_t dn_tmp_48=v_enable;
({ int32_t dn_foreign=dn_ext_EnableWindow(dn_tmp_45,dn_tmp_48);dn_fp_default();dn_foreign; }); }));
(void)(({ void * dn_tmp_49=({ dn_r_Gui const * dn_tmp_50=v_g;
size_t dn_tmp_51=((size_t)(18ULL));
dn_f_control(dn_tmp_50,dn_tmp_51); });
int32_t dn_tmp_52=v_enable;
({ int32_t dn_foreign=dn_ext_EnableWindow(dn_tmp_49,dn_tmp_52);dn_fp_default();dn_foreign; }); }));
(void)(({ void * dn_tmp_53=({ dn_r_Gui const * dn_tmp_54=v_g;
size_t dn_tmp_55=((size_t)(10ULL));
dn_f_control(dn_tmp_54,dn_tmp_55); });
dn_r_Rect * dn_tmp_56=({ ((dn_r_Rect *)NULL); });
int32_t dn_tmp_57=((int32_t)(1ULL));
({ int32_t dn_foreign=dn_ext_InvalidateRect(dn_tmp_53,dn_tmp_56,dn_tmp_57);dn_fp_default();dn_foreign; }); }));
(void)(({ void * dn_tmp_58=(v_g)->f_window;
dn_r_Rect * dn_tmp_59=({ ((dn_r_Rect *)NULL); });
int32_t dn_tmp_60=((int32_t)(1ULL));
({ int32_t dn_foreign=dn_ext_InvalidateRect(dn_tmp_58,dn_tmp_59,dn_tmp_60);dn_fp_default();dn_foreign; }); }));
dn_drop_buf_U16(&v_text);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_running_ui(dn_r_Gui const * v_g) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_running_ui(v_g);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_gui_stop(dn_r_Gui * v_g) {
{
void * v_probe=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_0=(&((v_g)->f_preflight));
(&((*dn_tmp_0)->value)); })))));
if(({ void * dn_tmp_1=v_probe;
dn_f_handle_valid(dn_tmp_1); })){
(void)(({ void * dn_tmp_2=v_probe;
uint32_t dn_tmp_3=((uint32_t)(3ULL));
({ int32_t dn_foreign=dn_ext_TerminateProcess(dn_tmp_2,dn_tmp_3);dn_fp_default();dn_foreign; }); }));
(void)(({ void * dn_tmp_4=v_probe;
uint32_t dn_tmp_5=((uint32_t)(1000ULL));
({ uint32_t dn_foreign=dn_ext_WaitForSingleObject(dn_tmp_4,dn_tmp_5);dn_fp_default();dn_foreign; }); }));
{ dn_t_resource_h_close_handle * dn_tmp_8=({ dn_t_resource_h_close_handle * dn_tmp_6=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_6,({ ((void *){0}); })); });dn_adopt_resource_h_close_handle(&dn_tmp_8);dn_t_resource_h_close_handle * volatile *dn_tmp_7=&((v_g)->f_preflight);dn_drop_resource_h_close_handle(dn_tmp_7);dn_retain_resource_h_close_handle(&dn_tmp_8);*dn_tmp_7=dn_tmp_8; }
}
else{
}
void * v_process=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_9=(&((v_g)->f_process));
(&((*dn_tmp_9)->value)); })))));
if(({ void * dn_tmp_10=v_process;
dn_f_handle_valid(dn_tmp_10); })){
if(((!(({ uint32_t dn_tmp_11=({ void * dn_tmp_12=v_process;
({ uint32_t dn_foreign=dn_ext_GetProcessId(dn_tmp_12);dn_fp_default();dn_foreign; }); });
dn_f_signal_stop(dn_tmp_11); }))) || ({ uint32_t dn_tmp_13=({ void * dn_tmp_14=v_process;
uint32_t dn_tmp_15=((uint32_t)(3000ULL));
({ uint32_t dn_foreign=dn_ext_WaitForSingleObject(dn_tmp_14,dn_tmp_15);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_16=({ uint32_t dn_tmp_17=((uint32_t)(258ULL));
if((unsigned __int128)dn_tmp_17>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_17; });
(dn_tmp_13 == dn_tmp_16); }))){
(void)(({ void * dn_tmp_18=v_process;
uint32_t dn_tmp_19=((uint32_t)(3ULL));
({ int32_t dn_foreign=dn_ext_TerminateProcess(dn_tmp_18,dn_tmp_19);dn_fp_default();dn_foreign; }); }));
(void)(({ void * dn_tmp_20=v_process;
uint32_t dn_tmp_21=((uint32_t)(1000ULL));
({ uint32_t dn_foreign=dn_ext_WaitForSingleObject(dn_tmp_20,dn_tmp_21);dn_fp_default();dn_foreign; }); }));
}
else{
}
if(({ uint32_t dn_tmp_22=({ void * dn_tmp_23=v_process;
uint32_t dn_tmp_24=((uint32_t)(0ULL));
({ uint32_t dn_foreign=dn_ext_WaitForSingleObject(dn_tmp_23,dn_tmp_24);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_25=({ uint32_t dn_tmp_26=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_26>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_26; });
(dn_tmp_22 != dn_tmp_25); })){
{ uint32_t dn_tmp_28=((uint32_t)(4ULL));uint32_t *dn_tmp_27=&((v_g)->f_phase);*dn_tmp_27=dn_tmp_28; }
(void)(({ dn_r_Gui * dn_tmp_29=v_g;
dn_t_slice0_U8 dn_tmp_30=({ dn_utf8 dn_tmp_31=((dn_utf8){(const uint8_t*)"\320\224\320\262\320\270\320\266\320\276\320\272 \320\265\321\211\321\221 \320\275\320\265 \320\277\320\276\320\264\321\202\320\262\320\265\321\200\320\264\320\270\320\273 \320\276\321\201\321\202\320\260\320\275\320\276\320\262\320\272\321\203. \320\237\320\276\320\262\321\202\320\276\321\200\320\270\321\202\320\265 \320\276\321\202\320\272\320\273\321\216\321\207\320\265\320\275\320\270\320\265 \320\270\320\273\320\270 \320\267\320\260\320\272\321\200\320\276\320\271\321\202\320\265 \320\276\320\272\320\275\320\276.", 139});
((dn_t_slice0_U8){dn_tmp_31.data,dn_tmp_31.len}); });
dn_f_ui_log(dn_tmp_29,dn_tmp_30); }));
(void)(({ dn_r_Gui * dn_tmp_32=v_g;
dn_f_running_ui(dn_tmp_32); }));
{ uint8_t dn_tmp_33=0;return dn_tmp_33; }
}
else{
}
{ dn_t_resource_h_close_handle * dn_tmp_36=({ dn_t_resource_h_close_handle * dn_tmp_34=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_34,({ ((void *){0}); })); });dn_adopt_resource_h_close_handle(&dn_tmp_36);dn_t_resource_h_close_handle * volatile *dn_tmp_35=&((v_g)->f_process);dn_drop_resource_h_close_handle(dn_tmp_35);dn_retain_resource_h_close_handle(&dn_tmp_36);*dn_tmp_35=dn_tmp_36; }
}
else{
}
{ uint32_t dn_tmp_38=((uint32_t)(0ULL));uint32_t *dn_tmp_37=&((v_g)->f_phase);*dn_tmp_37=dn_tmp_38; }
(void)(({ dn_r_Gui * dn_tmp_39=v_g;
dn_f_running_ui(dn_tmp_39); }));
(void)(({ dn_r_Gui * dn_tmp_40=v_g;
dn_t_slice0_U8 dn_tmp_41=({ dn_utf8 dn_tmp_42=((dn_utf8){(const uint8_t*)"\320\236\320\261\321\200\320\260\320\261\320\276\321\202\320\272\320\260 \320\276\321\201\321\202\320\260\320\275\320\276\320\262\320\273\320\265\320\275\320\260; \320\277\320\265\321\200\320\265\321\205\320\262\320\260\321\202 \320\277\320\260\320\272\320\265\321\202\320\276\320\262 \321\201\320\275\321\217\321\202.", 84});
((dn_t_slice0_U8){dn_tmp_42.data,dn_tmp_42.len}); });
dn_f_ui_log(dn_tmp_40,dn_tmp_41); }));
}

return 0;
}
static void dn_cb_gui_stop(dn_r_Gui * v_g) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_gui_stop(v_g);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_start_engine(dn_r_Gui * v_g) {
{
dn_t_buf_U8 volatile v_path={0};
dn_t_buf_U8 volatile v_args={0};
dn_t_buf_U8 volatile v_profile={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_profile);
dn_drop_buf_U8(&v_args);
dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_path=({ dn_utf8 dn_tmp_2=((dn_utf8){(const uint8_t*)"NoDPI.exe", 9});
dn_f_local_path(dn_tmp_2); });
v_args=({ dn_t_slice0_U8 dn_tmp_3=({ dn_utf8 dn_tmp_4=((dn_utf8){(const uint8_t*)"--config=config.ini --automatic --parent-pid=", 45});
((dn_t_slice0_U8){dn_tmp_4.data,dn_tmp_4.len}); });
dn_f_bytes_copy(dn_tmp_3); });
(void)(({ dn_t_buf_U8 * dn_tmp_5=(&(v_args));
uint64_t dn_tmp_6=({ uint32_t dn_tmp_7=({ ({ uint32_t dn_foreign=dn_ext_GetCurrentProcessId();dn_fp_default();dn_foreign; }); });
if((unsigned __int128)dn_tmp_7>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_7; });
dn_f_append_number(dn_tmp_5,dn_tmp_6); }));
v_profile=({ dn_t_slice0_U8 dn_tmp_8=({ dn_utf8 dn_tmp_9=((dn_utf8){(const uint8_t*)"active.profile", 14});
((dn_t_slice0_U8){dn_tmp_9.data,dn_tmp_9.len}); });
size_t dn_tmp_10=((size_t)(65536ULL));
dn_f_read_file(dn_tmp_8,dn_tmp_10); });
if(({ size_t dn_tmp_11=({ dn_t_buf_U8 const * dn_tmp_12=(&(v_profile));
(*(dn_tmp_12)).len; });
size_t dn_tmp_13=((size_t)(0ULL));
(dn_tmp_11 > dn_tmp_13); })){
(void)(({ dn_t_buf_U8 * dn_tmp_14=(&(v_args));
dn_t_slice0_U8 dn_tmp_15=({ dn_utf8 dn_tmp_16=((dn_utf8){(const uint8_t*)" --strategy-file=active.profile", 31});
((dn_t_slice0_U8){dn_tmp_16.data,dn_tmp_16.len}); });
dn_f_bytes_append(dn_tmp_14,dn_tmp_15); }));
}
else{
}
{ uint32_t dn_tmp_18=((uint32_t)(2ULL));uint32_t *dn_tmp_17=&((v_g)->f_phase);*dn_tmp_17=dn_tmp_18; }
(void)(({ dn_r_Gui * dn_tmp_19=v_g;
dn_f_running_ui(dn_tmp_19); }));
(void)(({ void * dn_tmp_20=(v_g)->f_window;
({ int32_t dn_foreign=dn_ext_UpdateWindow(dn_tmp_20);dn_fp_default();dn_foreign; }); }));
{ dn_t_resource_h_close_handle * dn_tmp_36=({ dn_t_resource_h_close_handle * dn_tmp_21=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_21,({ dn_r_Gui * dn_tmp_22=v_g;
dn_t_slice0_U8 dn_tmp_23=({ dn_t_buf_U8 const * dn_tmp_24=(&(v_path));
size_t dn_tmp_25=((size_t)(0ULL));
size_t dn_tmp_26=({ dn_t_buf_U8 const * dn_tmp_27=(&(v_path));
(*(dn_tmp_27)).len; });
({ dn_range(dn_tmp_25,dn_tmp_26,(*(dn_tmp_24)).len);(dn_t_slice0_U8){(*(dn_tmp_24)).data?(*(dn_tmp_24)).data+dn_tmp_25:NULL,dn_tmp_26}; }); });
dn_t_slice0_U8 dn_tmp_28=({ dn_t_buf_U8 const * dn_tmp_29=(&(v_args));
size_t dn_tmp_30=((size_t)(0ULL));
size_t dn_tmp_31=({ dn_t_buf_U8 const * dn_tmp_32=(&(v_args));
(*(dn_tmp_32)).len; });
({ dn_range(dn_tmp_30,dn_tmp_31,(*(dn_tmp_29)).len);(dn_t_slice0_U8){(*(dn_tmp_29)).data?(*(dn_tmp_29)).data+dn_tmp_30:NULL,dn_tmp_31}; }); });
bool dn_tmp_33=1;
int32_t dn_tmp_34=((int32_t)(0ULL));
dn_f_launch(dn_tmp_22,dn_tmp_23,dn_tmp_28,dn_tmp_33,dn_tmp_34); })); });dn_adopt_resource_h_close_handle(&dn_tmp_36);dn_t_resource_h_close_handle * volatile *dn_tmp_35=&((v_g)->f_process);dn_drop_resource_h_close_handle(dn_tmp_35);dn_retain_resource_h_close_handle(&dn_tmp_36);*dn_tmp_35=dn_tmp_36; }
if((!(({ void * dn_tmp_37=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_38=(&((v_g)->f_process));
(&((*dn_tmp_38)->value)); })))));
dn_f_handle_valid(dn_tmp_37); })))){
{ uint32_t dn_tmp_40=((uint32_t)(4ULL));uint32_t *dn_tmp_39=&((v_g)->f_phase);*dn_tmp_39=dn_tmp_40; }
(void)(({ dn_r_Gui * dn_tmp_41=v_g;
dn_t_slice0_U8 dn_tmp_42=({ dn_utf8 dn_tmp_43=((dn_utf8){(const uint8_t*)"\320\227\320\260\320\277\321\203\321\201\320\272 \320\276\321\202\320\274\320\265\320\275\321\221\320\275 \320\270\320\273\320\270 \320\276\321\202\321\201\321\203\321\202\321\201\321\202\320\262\321\203\320\265\321\202 NoDPI.exe. \320\237\320\276\320\264\321\200\320\276\320\261\320\275\320\276\321\201\321\202\320\270 \342\200\224 \320\262 \320\266\321\203\321\200\320\275\320\260\320\273\320\265.", 114});
((dn_t_slice0_U8){dn_tmp_43.data,dn_tmp_43.len}); });
dn_f_ui_log(dn_tmp_41,dn_tmp_42); }));
(void)(({ dn_r_Gui * dn_tmp_44=v_g;
dn_f_running_ui(dn_tmp_44); }));
{ uint8_t dn_tmp_45=0;dn_drop_buf_U8(&v_profile);
dn_drop_buf_U8(&v_args);
dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_45; }
}
else{
}
{ uint64_t dn_tmp_50=({ uint64_t dn_tmp_46=({ ({ uint64_t dn_foreign=dn_ext_GetTickCount64();dn_fp_default();dn_foreign; }); });
uint64_t dn_tmp_47=({ uint64_t dn_tmp_48=((uint64_t)(25000ULL));
if((unsigned __int128)dn_tmp_48>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_48; });
dn_U64_add(dn_tmp_46,dn_tmp_47); });uint64_t *dn_tmp_49=&((v_g)->f_deadline);*dn_tmp_49=dn_tmp_50; }
dn_drop_buf_U8(&v_profile);
dn_drop_buf_U8(&v_args);
dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_start_engine(dn_r_Gui * v_g) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_start_engine(v_g);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_gui_start(dn_r_Gui * v_g) {
{
dn_t_buf_U16 volatile v_lock_name={0};
dn_t_resource_h_close_handle * volatile v_existing={0};
dn_t_buf_U8 volatile v_path={0};
dn_t_buf_U8 volatile v_args={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_args);
dn_drop_buf_U8(&v_path);
dn_drop_resource_h_close_handle(&v_existing);
dn_drop_buf_U16(&v_lock_name);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
if((({ void * dn_tmp_2=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_3=(&((v_g)->f_process));
(&((*dn_tmp_3)->value)); })))));
dn_f_handle_valid(dn_tmp_2); }) || ({ void * dn_tmp_4=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_5=(&((v_g)->f_preflight));
(&((*dn_tmp_5)->value)); })))));
dn_f_handle_valid(dn_tmp_4); }))){
{ uint8_t dn_tmp_6=0;dn_drop_buf_U8(&v_args);
dn_drop_buf_U8(&v_path);
dn_drop_resource_h_close_handle(&v_existing);
dn_drop_buf_U16(&v_lock_name);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_6; }
}
else{
}
v_lock_name=({ dn_t_slice0_U8 dn_tmp_7=({ dn_utf8 dn_tmp_8=((dn_utf8){(const uint8_t*)"Local\134NoDPI.Engine.v5", 21});
((dn_t_slice0_U8){dn_tmp_8.data,dn_tmp_8.len}); });
dn_f_wide(dn_tmp_7); });
(void)(({ uint32_t dn_tmp_9=((uint32_t)(0ULL));
({ dn_ext_SetLastError(dn_tmp_9);dn_fp_default();0; }); }));
v_existing=({ dn_t_resource_h_close_handle * dn_tmp_10=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_10,({ uint32_t dn_tmp_11=((uint32_t)(0x100000ULL));
int32_t dn_tmp_12=((int32_t)(0ULL));
uint16_t * dn_tmp_13=({ dn_t_buf_U16 const * dn_tmp_14=(&(v_lock_name));
((uint16_t *)(*(dn_tmp_14)).data); });
({ void * dn_foreign=dn_ext_OpenMutexW(dn_tmp_11,dn_tmp_12,dn_tmp_13);dn_fp_default();dn_foreign; }); })); });
if((({ void * dn_tmp_15=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_16=(&(v_existing));
(&((*dn_tmp_16)->value)); })))));
dn_f_handle_valid(dn_tmp_15); }) || ({ uint32_t dn_tmp_17=({ ({ uint32_t dn_foreign=dn_ext_GetLastError();dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_18=({ uint32_t dn_tmp_19=((uint32_t)(5ULL));
if((unsigned __int128)dn_tmp_19>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_19; });
(dn_tmp_17 == dn_tmp_18); }))){
{ uint32_t dn_tmp_21=((uint32_t)(4ULL));uint32_t *dn_tmp_20=&((v_g)->f_phase);*dn_tmp_20=dn_tmp_21; }
(void)(({ dn_r_Gui * dn_tmp_22=v_g;
dn_t_slice0_U8 dn_tmp_23=({ dn_utf8 dn_tmp_24=((dn_utf8){(const uint8_t*)"\320\224\321\200\321\203\320\263\320\276\320\271 \320\264\320\262\320\270\320\266\320\276\320\272 NoDPI \321\203\320\266\320\265 \320\267\320\260\320\277\321\203\321\211\320\265\320\275. \320\236\321\201\321\202\320\260\320\275\320\276\320\262\320\270\321\202\320\265 \320\265\320\263\320\276 \320\277\320\265\321\200\320\265\320\264 \320\262\320\272\320\273\321\216\321\207\320\265\320\275\320\270\320\265\320\274 \321\215\321\202\320\276\320\263\320\276 \320\276\320\272\320\275\320\260.", 135});
((dn_t_slice0_U8){dn_tmp_24.data,dn_tmp_24.len}); });
dn_f_ui_log(dn_tmp_22,dn_tmp_23); }));
(void)(({ dn_r_Gui * dn_tmp_25=v_g;
dn_f_running_ui(dn_tmp_25); }));
{ uint8_t dn_tmp_26=0;dn_drop_buf_U8(&v_args);
dn_drop_buf_U8(&v_path);
dn_drop_resource_h_close_handle(&v_existing);
dn_drop_buf_U16(&v_lock_name);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_26; }
}
else{
}
v_path=({ dn_utf8 dn_tmp_27=((dn_utf8){(const uint8_t*)"Prepare-Auto.ps1", 16});
dn_f_local_path(dn_tmp_27); });
v_args=({ dn_t_slice0_U8 dn_tmp_28=({ dn_utf8 dn_tmp_29=((dn_utf8){(const uint8_t*)"-NoProfile -ExecutionPolicy Bypass -File \042", 42});
((dn_t_slice0_U8){dn_tmp_29.data,dn_tmp_29.len}); });
dn_f_bytes_copy(dn_tmp_28); });
(void)(({ dn_t_buf_U8 * dn_tmp_30=(&(v_args));
dn_t_slice0_U8 dn_tmp_31=({ dn_t_buf_U8 const * dn_tmp_32=(&(v_path));
size_t dn_tmp_33=((size_t)(0ULL));
size_t dn_tmp_34=({ dn_t_buf_U8 const * dn_tmp_35=(&(v_path));
(*(dn_tmp_35)).len; });
({ dn_range(dn_tmp_33,dn_tmp_34,(*(dn_tmp_32)).len);(dn_t_slice0_U8){(*(dn_tmp_32)).data?(*(dn_tmp_32)).data+dn_tmp_33:NULL,dn_tmp_34}; }); });
dn_f_bytes_append(dn_tmp_30,dn_tmp_31); }));
(void)(({ dn_t_buf_U8 * dn_tmp_36=(&(v_args));
dn_t_slice0_U8 dn_tmp_37=({ dn_utf8 dn_tmp_38=((dn_utf8){(const uint8_t*)"\042 -CheckBaseline", 16});
((dn_t_slice0_U8){dn_tmp_38.data,dn_tmp_38.len}); });
dn_f_bytes_append(dn_tmp_36,dn_tmp_37); }));
{ dn_t_resource_h_close_handle * dn_tmp_51=({ dn_t_resource_h_close_handle * dn_tmp_39=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_39,({ dn_r_Gui * dn_tmp_40=v_g;
dn_t_slice0_U8 dn_tmp_41=({ dn_utf8 dn_tmp_42=((dn_utf8){(const uint8_t*)"powershell.exe", 14});
((dn_t_slice0_U8){dn_tmp_42.data,dn_tmp_42.len}); });
dn_t_slice0_U8 dn_tmp_43=({ dn_t_buf_U8 const * dn_tmp_44=(&(v_args));
size_t dn_tmp_45=((size_t)(0ULL));
size_t dn_tmp_46=({ dn_t_buf_U8 const * dn_tmp_47=(&(v_args));
(*(dn_tmp_47)).len; });
({ dn_range(dn_tmp_45,dn_tmp_46,(*(dn_tmp_44)).len);(dn_t_slice0_U8){(*(dn_tmp_44)).data?(*(dn_tmp_44)).data+dn_tmp_45:NULL,dn_tmp_46}; }); });
bool dn_tmp_48=0;
int32_t dn_tmp_49=((int32_t)(0ULL));
dn_f_launch(dn_tmp_40,dn_tmp_41,dn_tmp_43,dn_tmp_48,dn_tmp_49); })); });dn_adopt_resource_h_close_handle(&dn_tmp_51);dn_t_resource_h_close_handle * volatile *dn_tmp_50=&((v_g)->f_preflight);dn_drop_resource_h_close_handle(dn_tmp_50);dn_retain_resource_h_close_handle(&dn_tmp_51);*dn_tmp_50=dn_tmp_51; }
if((!(({ void * dn_tmp_52=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_53=(&((v_g)->f_preflight));
(&((*dn_tmp_53)->value)); })))));
dn_f_handle_valid(dn_tmp_52); })))){
{ uint32_t dn_tmp_55=((uint32_t)(4ULL));uint32_t *dn_tmp_54=&((v_g)->f_phase);*dn_tmp_54=dn_tmp_55; }
(void)(({ dn_r_Gui * dn_tmp_56=v_g;
dn_t_slice0_U8 dn_tmp_57=({ dn_utf8 dn_tmp_58=((dn_utf8){(const uint8_t*)"\320\235\320\265\321\202 NoDPIProbe.exe. \320\240\320\260\321\201\320\277\320\260\320\272\321\203\320\271\321\202\320\265 \320\262\321\201\320\265 \321\204\320\260\320\271\320\273\321\213 \320\276\320\261\320\275\320\276\320\262\320\273\320\265\320\275\320\270\321\217.", 83});
((dn_t_slice0_U8){dn_tmp_58.data,dn_tmp_58.len}); });
dn_f_ui_log(dn_tmp_56,dn_tmp_57); }));
(void)(({ dn_r_Gui * dn_tmp_59=v_g;
dn_f_running_ui(dn_tmp_59); }));
{ uint8_t dn_tmp_60=0;dn_drop_buf_U8(&v_args);
dn_drop_buf_U8(&v_path);
dn_drop_resource_h_close_handle(&v_existing);
dn_drop_buf_U16(&v_lock_name);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_60; }
}
else{
}
{ uint32_t dn_tmp_62=((uint32_t)(1ULL));uint32_t *dn_tmp_61=&((v_g)->f_phase);*dn_tmp_61=dn_tmp_62; }
{ uint64_t dn_tmp_67=({ uint64_t dn_tmp_63=({ ({ uint64_t dn_foreign=dn_ext_GetTickCount64();dn_fp_default();dn_foreign; }); });
uint64_t dn_tmp_64=({ uint64_t dn_tmp_65=((uint64_t)(45000ULL));
if((unsigned __int128)dn_tmp_65>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_65; });
dn_U64_add(dn_tmp_63,dn_tmp_64); });uint64_t *dn_tmp_66=&((v_g)->f_deadline);*dn_tmp_66=dn_tmp_67; }
(void)(({ dn_r_Gui * dn_tmp_68=v_g;
dn_t_slice0_U8 dn_tmp_69=({ dn_utf8 dn_tmp_70=((dn_utf8){(const uint8_t*)"\320\237\321\200\320\276\320\262\320\265\321\200\320\272\320\260 \320\262\321\213\320\277\320\276\320\273\320\275\321\217\320\265\321\202\321\201\321\217 \320\261\320\265\320\267 NoDPI. \320\237\321\200\320\270 \320\276\320\261\321\211\320\265\320\274 \320\276\321\202\320\272\320\260\320\267\320\265 \320\277\320\265\321\200\320\265\321\205\320\262\320\260\321\202 \320\275\320\265 \320\262\320\272\320\273\321\216\321\207\320\260\320\265\321\202\321\201\321\217.", 128});
((dn_t_slice0_U8){dn_tmp_70.data,dn_tmp_70.len}); });
dn_f_ui_log(dn_tmp_68,dn_tmp_69); }));
(void)(({ dn_r_Gui * dn_tmp_71=v_g;
dn_f_running_ui(dn_tmp_71); }));
dn_drop_buf_U8(&v_args);
dn_drop_buf_U8(&v_path);
dn_drop_resource_h_close_handle(&v_existing);
dn_drop_buf_U16(&v_lock_name);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_gui_start(dn_r_Gui * v_g) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_gui_start(v_g);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static bool dn_f_ready(uint32_t v_pid) {
{
dn_t_buf_U8 volatile v_name={0};
dn_t_buf_U16 volatile v_w={0};
dn_t_resource_h_close_handle * volatile v_owner={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_resource_h_close_handle(&v_owner);
dn_drop_buf_U16(&v_w);
dn_drop_buf_U8(&v_name);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_name=({ dn_t_slice0_U8 dn_tmp_2=({ dn_utf8 dn_tmp_3=((dn_utf8){(const uint8_t*)"Local\134NoDPI.Ready.", 18});
((dn_t_slice0_U8){dn_tmp_3.data,dn_tmp_3.len}); });
dn_f_bytes_copy(dn_tmp_2); });
(void)(({ dn_t_buf_U8 * dn_tmp_4=(&(v_name));
uint64_t dn_tmp_5=({ uint32_t dn_tmp_6=v_pid;
if((unsigned __int128)dn_tmp_6>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_6; });
dn_f_append_number(dn_tmp_4,dn_tmp_5); }));
v_w=({ dn_t_slice0_U8 dn_tmp_7=({ dn_t_buf_U8 const * dn_tmp_8=(&(v_name));
size_t dn_tmp_9=((size_t)(0ULL));
size_t dn_tmp_10=({ dn_t_buf_U8 const * dn_tmp_11=(&(v_name));
(*(dn_tmp_11)).len; });
({ dn_range(dn_tmp_9,dn_tmp_10,(*(dn_tmp_8)).len);(dn_t_slice0_U8){(*(dn_tmp_8)).data?(*(dn_tmp_8)).data+dn_tmp_9:NULL,dn_tmp_10}; }); });
dn_f_wide(dn_tmp_7); });
v_owner=({ dn_t_resource_h_close_handle * dn_tmp_12=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_12,({ uint32_t dn_tmp_13=((uint32_t)(0x100000ULL));
int32_t dn_tmp_14=((int32_t)(0ULL));
uint16_t * dn_tmp_15=({ dn_t_buf_U16 const * dn_tmp_16=(&(v_w));
((uint16_t *)(*(dn_tmp_16)).data); });
({ void * dn_foreign=dn_ext_OpenEventW(dn_tmp_13,dn_tmp_14,dn_tmp_15);dn_fp_default();dn_foreign; }); })); });
void * v_event=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_17=(&(v_owner));
(&((*dn_tmp_17)->value)); })))));
{ bool dn_tmp_24=(({ void * dn_tmp_18=v_event;
dn_f_handle_valid(dn_tmp_18); }) && ({ uint32_t dn_tmp_19=({ void * dn_tmp_20=v_event;
uint32_t dn_tmp_21=((uint32_t)(0ULL));
({ uint32_t dn_foreign=dn_ext_WaitForSingleObject(dn_tmp_20,dn_tmp_21);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_22=({ uint32_t dn_tmp_23=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_23>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_23; });
(dn_tmp_19 == dn_tmp_22); }));dn_drop_resource_h_close_handle(&v_owner);
dn_drop_buf_U16(&v_w);
dn_drop_buf_U8(&v_name);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_24; }
dn_drop_resource_h_close_handle(&v_owner);
dn_drop_buf_U16(&v_w);
dn_drop_buf_U8(&v_name);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static bool dn_cb_ready(uint32_t v_pid) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_ready(v_pid);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_gui_poll(dn_r_Gui * v_g) {
{
(void)(({ dn_r_Gui * dn_tmp_0=v_g;
dn_f_poll_training(dn_tmp_0); }));
void * v_probe=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_1=(&((v_g)->f_preflight));
(&((*dn_tmp_1)->value)); })))));
if(({ void * dn_tmp_2=v_probe;
dn_f_handle_valid(dn_tmp_2); })){
uint32_t v_code=((uint32_t)(259ULL));
if(({ int32_t dn_tmp_3=({ void * dn_tmp_4=v_probe;
uint32_t * dn_tmp_5=({ uint32_t * dn_tmp_6=({ uint32_t * dn_tmp_7=(&(v_code));
((uint32_t *)dn_tmp_7); });
((uint32_t *)dn_tmp_6); });
({ int32_t dn_foreign=dn_ext_GetExitCodeProcess(dn_tmp_4,dn_tmp_5);dn_fp_default();dn_foreign; }); });
int32_t dn_tmp_8=({ int32_t dn_tmp_9=((int32_t)(0ULL));
if((__int128)dn_tmp_9<(-((__int128)1<<31))||(__int128)dn_tmp_9>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_9; });
(dn_tmp_3 == dn_tmp_8); })){
{ uint32_t dn_tmp_11=((uint32_t)(2ULL));uint32_t *dn_tmp_10=&(v_code);*dn_tmp_10=dn_tmp_11; }
}
else{
}
if(({ uint32_t dn_tmp_12=v_code;
uint32_t dn_tmp_13=({ uint32_t dn_tmp_14=((uint32_t)(259ULL));
if((unsigned __int128)dn_tmp_14>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_14; });
(dn_tmp_12 != dn_tmp_13); })){
{ dn_t_resource_h_close_handle * dn_tmp_17=({ dn_t_resource_h_close_handle * dn_tmp_15=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_15,({ ((void *){0}); })); });dn_adopt_resource_h_close_handle(&dn_tmp_17);dn_t_resource_h_close_handle * volatile *dn_tmp_16=&((v_g)->f_preflight);dn_drop_resource_h_close_handle(dn_tmp_16);dn_retain_resource_h_close_handle(&dn_tmp_17);*dn_tmp_16=dn_tmp_17; }
if(({ uint32_t dn_tmp_18=v_code;
uint32_t dn_tmp_19=({ uint32_t dn_tmp_20=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_20>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_20; });
(dn_tmp_18 == dn_tmp_19); })){
(void)(({ dn_r_Gui * dn_tmp_21=v_g;
dn_f_start_engine(dn_tmp_21); }));
}
else{
{ uint32_t dn_tmp_23=((uint32_t)(4ULL));uint32_t *dn_tmp_22=&((v_g)->f_phase);*dn_tmp_22=dn_tmp_23; }
(void)(({ dn_r_Gui * dn_tmp_24=v_g;
dn_t_slice0_U8 dn_tmp_25=({ dn_utf8 dn_tmp_26=((dn_utf8){(const uint8_t*)"\320\230\321\201\321\205\320\276\320\264\320\275\321\213\320\271 HTTPS \320\275\320\265\320\264\320\276\321\201\321\202\321\203\320\277\320\265\320\275. \320\237\321\200\320\276\320\262\320\265\321\200\321\214\321\202\320\265 VPN / Kill Switch / DNS \321\207\320\265\321\200\320\265\320\267 \302\253\320\237\321\200\320\276\320\262\320\265\321\200\320\270\321\202\321\214 \321\201\320\265\321\202\321\214\302\273. \320\224\320\262\320\270\320\266\320\276\320\272 \320\275\320\265 \320\262\320\272\320\273\321\216\321\207\321\221\320\275.", 165});
((dn_t_slice0_U8){dn_tmp_26.data,dn_tmp_26.len}); });
dn_f_ui_log(dn_tmp_24,dn_tmp_25); }));
(void)(({ dn_r_Gui * dn_tmp_27=v_g;
dn_f_running_ui(dn_tmp_27); }));
}
}
else{
if(({ uint64_t dn_tmp_28=({ ({ uint64_t dn_foreign=dn_ext_GetTickCount64();dn_fp_default();dn_foreign; }); });
uint64_t dn_tmp_29=(v_g)->f_deadline;
(dn_tmp_28 >= dn_tmp_29); })){
(void)(({ dn_r_Gui * dn_tmp_30=v_g;
dn_f_gui_stop(dn_tmp_30); }));
{ uint32_t dn_tmp_32=((uint32_t)(4ULL));uint32_t *dn_tmp_31=&((v_g)->f_phase);*dn_tmp_31=dn_tmp_32; }
(void)(({ dn_r_Gui * dn_tmp_33=v_g;
dn_t_slice0_U8 dn_tmp_34=({ dn_utf8 dn_tmp_35=((dn_utf8){(const uint8_t*)"\320\230\321\201\321\205\320\276\320\264\320\275\320\260\321\217 \320\277\321\200\320\276\320\262\320\265\321\200\320\272\320\260 \320\277\321\200\320\265\320\262\321\213\321\201\320\270\320\273\320\260 \320\262\321\200\320\265\320\274\321\217 \320\276\320\266\320\270\320\264\320\260\320\275\320\270\321\217. \320\235\321\203\320\266\320\265\320\275 \320\276\321\202\321\207\321\221\321\202 \320\277\321\200\320\276\320\262\320\265\321\200\320\272\320\270 \321\201\320\265\321\202\320\270.", 130});
((dn_t_slice0_U8){dn_tmp_35.data,dn_tmp_35.len}); });
dn_f_ui_log(dn_tmp_33,dn_tmp_34); }));
}
else{
}
}
}
else{
}
void * v_process=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_36=(&((v_g)->f_process));
(&((*dn_tmp_36)->value)); })))));
if(({ void * dn_tmp_37=v_process;
dn_f_handle_valid(dn_tmp_37); })){
uint32_t v_code=((uint32_t)(259ULL));
if(({ int32_t dn_tmp_38=({ void * dn_tmp_39=v_process;
uint32_t * dn_tmp_40=({ uint32_t * dn_tmp_41=({ uint32_t * dn_tmp_42=(&(v_code));
((uint32_t *)dn_tmp_42); });
((uint32_t *)dn_tmp_41); });
({ int32_t dn_foreign=dn_ext_GetExitCodeProcess(dn_tmp_39,dn_tmp_40);dn_fp_default();dn_foreign; }); });
int32_t dn_tmp_43=({ int32_t dn_tmp_44=((int32_t)(0ULL));
if((__int128)dn_tmp_44<(-((__int128)1<<31))||(__int128)dn_tmp_44>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_44; });
(dn_tmp_38 == dn_tmp_43); })){
{ uint32_t dn_tmp_46=((uint32_t)(2ULL));uint32_t *dn_tmp_45=&(v_code);*dn_tmp_45=dn_tmp_46; }
}
else{
}
if(({ uint32_t dn_tmp_47=v_code;
uint32_t dn_tmp_48=({ uint32_t dn_tmp_49=((uint32_t)(259ULL));
if((unsigned __int128)dn_tmp_49>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_49; });
(dn_tmp_47 != dn_tmp_48); })){
dn_t_buf_U8 volatile v_path={0};
dn_t_buf_U8 volatile v_text={0};
dn_t_buf_U8 volatile v_line={0};
dn_boundary dn_tmp_50={0};jmp_buf dn_tmp_51;dn_tmp_50.jump=&dn_tmp_51;dn_tmp_50.previous=dn_current;dn_tmp_50.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_50;if(setjmp(dn_tmp_51)){dn_current=dn_tmp_50.previous;dn_drop_buf_U8(&v_line);
dn_drop_buf_U8(&v_text);
dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_50,1);dn_raise();}
uint32_t v_pid=({ void * dn_tmp_52=v_process;
({ uint32_t dn_foreign=dn_ext_GetProcessId(dn_tmp_52);dn_fp_default();dn_foreign; }); });
{ dn_t_resource_h_close_handle * dn_tmp_55=({ dn_t_resource_h_close_handle * dn_tmp_53=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_53,({ ((void *){0}); })); });dn_adopt_resource_h_close_handle(&dn_tmp_55);dn_t_resource_h_close_handle * volatile *dn_tmp_54=&((v_g)->f_process);dn_drop_resource_h_close_handle(dn_tmp_54);dn_retain_resource_h_close_handle(&dn_tmp_55);*dn_tmp_54=dn_tmp_55; }
{ uint32_t dn_tmp_57=((uint32_t)(6ULL));uint32_t *dn_tmp_56=&((v_g)->f_phase);*dn_tmp_56=dn_tmp_57; }
v_path=({ dn_t_slice0_U8 dn_tmp_58=({ dn_utf8 dn_tmp_59=((dn_utf8){(const uint8_t*)"logs/startup-", 13});
((dn_t_slice0_U8){dn_tmp_59.data,dn_tmp_59.len}); });
dn_f_bytes_copy(dn_tmp_58); });
(void)(({ dn_t_buf_U8 * dn_tmp_60=(&(v_path));
uint64_t dn_tmp_61=({ uint32_t dn_tmp_62=v_pid;
if((unsigned __int128)dn_tmp_62>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_62; });
dn_f_append_number(dn_tmp_60,dn_tmp_61); }));
(void)(({ dn_t_buf_U8 * dn_tmp_63=(&(v_path));
dn_t_slice0_U8 dn_tmp_64=({ dn_utf8 dn_tmp_65=((dn_utf8){(const uint8_t*)".status", 7});
((dn_t_slice0_U8){dn_tmp_65.data,dn_tmp_65.len}); });
dn_f_bytes_append(dn_tmp_63,dn_tmp_64); }));
v_text=({ dn_t_slice0_U8 dn_tmp_66=({ dn_t_buf_U8 const * dn_tmp_67=(&(v_path));
size_t dn_tmp_68=((size_t)(0ULL));
size_t dn_tmp_69=({ dn_t_buf_U8 const * dn_tmp_70=(&(v_path));
(*(dn_tmp_70)).len; });
({ dn_range(dn_tmp_68,dn_tmp_69,(*(dn_tmp_67)).len);(dn_t_slice0_U8){(*(dn_tmp_67)).data?(*(dn_tmp_67)).data+dn_tmp_68:NULL,dn_tmp_69}; }); });
size_t dn_tmp_71=((size_t)(16384ULL));
dn_f_read_file(dn_tmp_66,dn_tmp_71); });
dn_r_StartupView v_info=({ dn_t_slice0_U8 dn_tmp_72=({ dn_t_buf_U8 const * dn_tmp_73=(&(v_text));
size_t dn_tmp_74=((size_t)(0ULL));
size_t dn_tmp_75=({ dn_t_buf_U8 const * dn_tmp_76=(&(v_text));
(*(dn_tmp_76)).len; });
({ dn_range(dn_tmp_74,dn_tmp_75,(*(dn_tmp_73)).len);(dn_t_slice0_U8){(*(dn_tmp_73)).data?(*(dn_tmp_73)).data+dn_tmp_74:NULL,dn_tmp_75}; }); });
dn_f_startup_view(dn_tmp_72); });
v_line=({ dn_t_slice0_U8 dn_tmp_77=({ dn_utf8 dn_tmp_78=((dn_utf8){(const uint8_t*)"\320\224\320\262\320\270\320\266\320\276\320\272 \320\267\320\260\320\262\320\265\321\200\321\210\321\221\320\275, \320\272\320\276\320\264 ", 38});
((dn_t_slice0_U8){dn_tmp_78.data,dn_tmp_78.len}); });
dn_f_bytes_copy(dn_tmp_77); });
(void)(({ dn_t_buf_U8 * dn_tmp_79=(&(v_line));
uint64_t dn_tmp_80=({ uint32_t dn_tmp_81=v_code;
if((unsigned __int128)dn_tmp_81>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_81; });
dn_f_append_number(dn_tmp_79,dn_tmp_80); }));
(void)(({ dn_t_buf_U8 * dn_tmp_82=(&(v_line));
dn_t_slice0_U8 dn_tmp_83=({ dn_utf8 dn_tmp_84=((dn_utf8){(const uint8_t*)". \320\236\321\202\320\272\321\200\320\276\320\271\321\202\320\265 Check-Driver.cmd \320\270 \321\201\320\276\320\261\320\265\321\200\320\270\321\202\320\265 \320\276\321\202\321\207\321\221\321\202.", 67});
((dn_t_slice0_U8){dn_tmp_84.data,dn_tmp_84.len}); });
dn_f_bytes_append(dn_tmp_82,dn_tmp_83); }));
if(((((v_info).f_found && (v_info).f_driver) && (!((v_info).f_ready))) && ({ uint32_t dn_tmp_85=(v_info).f_error;
uint32_t dn_tmp_86=({ uint32_t dn_tmp_87=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_87>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_87; });
(dn_tmp_85 > dn_tmp_86); }))){
{ dn_t_buf_U8 dn_tmp_90=({ uint32_t dn_tmp_88=(v_info).f_error;
dn_f_startup_caption(dn_tmp_88); });dn_adopt_buf_U8(&dn_tmp_90);dn_t_buf_U8 volatile *dn_tmp_89=&(v_line);dn_drop_buf_U8(dn_tmp_89);dn_retain_buf_U8(&dn_tmp_90);*dn_tmp_89=dn_tmp_90; }
}
else{
}
(void)(({ dn_r_Gui * dn_tmp_91=v_g;
dn_t_slice0_U8 dn_tmp_92=({ dn_t_buf_U8 const * dn_tmp_93=(&(v_line));
size_t dn_tmp_94=((size_t)(0ULL));
size_t dn_tmp_95=({ dn_t_buf_U8 const * dn_tmp_96=(&(v_line));
(*(dn_tmp_96)).len; });
({ dn_range(dn_tmp_94,dn_tmp_95,(*(dn_tmp_93)).len);(dn_t_slice0_U8){(*(dn_tmp_93)).data?(*(dn_tmp_93)).data+dn_tmp_94:NULL,dn_tmp_95}; }); });
dn_f_ui_log(dn_tmp_91,dn_tmp_92); }));
(void)(({ dn_r_Gui * dn_tmp_97=v_g;
dn_f_running_ui(dn_tmp_97); }));
dn_drop_buf_U8(&v_line);
dn_drop_buf_U8(&v_text);
dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_50,0);
}
else{
if((({ uint32_t dn_tmp_98=(v_g)->f_phase;
uint32_t dn_tmp_99=({ uint32_t dn_tmp_100=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_100>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_100; });
(dn_tmp_98 == dn_tmp_99); }) && ({ uint32_t dn_tmp_101=({ void * dn_tmp_102=v_process;
({ uint32_t dn_foreign=dn_ext_GetProcessId(dn_tmp_102);dn_fp_default();dn_foreign; }); });
dn_f_ready(dn_tmp_101); }))){
{ uint32_t dn_tmp_104=((uint32_t)(3ULL));uint32_t *dn_tmp_103=&((v_g)->f_phase);*dn_tmp_103=dn_tmp_104; }
(void)(({ dn_r_Gui * dn_tmp_105=v_g;
dn_f_running_ui(dn_tmp_105); }));
(void)(({ dn_r_Gui * dn_tmp_106=v_g;
dn_t_slice0_U8 dn_tmp_107=({ dn_utf8 dn_tmp_108=((dn_utf8){(const uint8_t*)"\320\224\320\262\320\270\320\266\320\276\320\272 \320\263\320\276\321\202\320\276\320\262. \320\237\321\200\320\276\320\261\321\213 \321\201\320\260\320\271\321\202\320\260 \320\275\320\265 \320\267\320\260\320\274\320\265\320\275\321\217\321\216\321\202 \320\262\320\276\321\201\320\277\321\200\320\276\320\270\320\267\320\262\320\265\320\264\320\265\320\275\320\270\320\265 \320\262\320\270\320\264\320\265\320\276 \320\270 \320\263\320\276\320\273\320\276\321\201\320\276\320\262\320\276\320\271 \320\267\320\262\320\276\320\275\320\276\320\272.", 146});
((dn_t_slice0_U8){dn_tmp_108.data,dn_tmp_108.len}); });
dn_f_ui_log(dn_tmp_106,dn_tmp_107); }));
}
else{
if((({ uint32_t dn_tmp_109=(v_g)->f_phase;
uint32_t dn_tmp_110=({ uint32_t dn_tmp_111=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_111>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_111; });
(dn_tmp_109 == dn_tmp_110); }) && ({ uint64_t dn_tmp_112=({ ({ uint64_t dn_foreign=dn_ext_GetTickCount64();dn_fp_default();dn_foreign; }); });
uint64_t dn_tmp_113=(v_g)->f_deadline;
(dn_tmp_112 >= dn_tmp_113); }))){
(void)(({ dn_r_Gui * dn_tmp_114=v_g;
dn_f_gui_stop(dn_tmp_114); }));
{ uint32_t dn_tmp_116=((uint32_t)(6ULL));uint32_t *dn_tmp_115=&((v_g)->f_phase);*dn_tmp_115=dn_tmp_116; }
(void)(({ dn_r_Gui * dn_tmp_117=v_g;
dn_t_slice0_U8 dn_tmp_118=({ dn_utf8 dn_tmp_119=((dn_utf8){(const uint8_t*)"\320\224\320\262\320\270\320\266\320\276\320\272 \320\275\320\265 \320\277\320\276\320\264\321\202\320\262\320\265\321\200\320\264\320\270\320\273 \320\263\320\276\321\202\320\276\320\262\320\275\320\276\321\201\321\202\321\214 \320\270 \320\276\321\201\321\202\320\260\320\275\320\276\320\262\320\273\320\265\320\275. \320\236\321\202\320\272\321\200\320\276\320\271\321\202\320\265 \320\266\321\203\321\200\320\275\320\260\320\273.", 115});
((dn_t_slice0_U8){dn_tmp_119.data,dn_tmp_119.len}); });
dn_f_ui_log(dn_tmp_117,dn_tmp_118); }));
}
else{
}
}
}
}
else{
}
}

return 0;
}
static void dn_cb_gui_poll(dn_r_Gui * v_g) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_gui_poll(v_g);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_auxiliary(dn_r_Gui * v_g,dn_utf8 v_filename,dn_t_slice0_U8 v_parameters) {
{
dn_t_buf_U8 volatile v_path={0};
dn_t_resource_h_close_handle * volatile v_owner={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_resource_h_close_handle(&v_owner);
dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_path=({ dn_utf8 dn_tmp_2=v_filename;
dn_f_local_path(dn_tmp_2); });
v_owner=({ dn_t_resource_h_close_handle * dn_tmp_3=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_3,({ dn_r_Gui * dn_tmp_4=v_g;
dn_t_slice0_U8 dn_tmp_5=({ dn_t_buf_U8 const * dn_tmp_6=(&(v_path));
size_t dn_tmp_7=((size_t)(0ULL));
size_t dn_tmp_8=({ dn_t_buf_U8 const * dn_tmp_9=(&(v_path));
(*(dn_tmp_9)).len; });
({ dn_range(dn_tmp_7,dn_tmp_8,(*(dn_tmp_6)).len);(dn_t_slice0_U8){(*(dn_tmp_6)).data?(*(dn_tmp_6)).data+dn_tmp_7:NULL,dn_tmp_8}; }); });
dn_t_slice0_U8 dn_tmp_10=v_parameters;
bool dn_tmp_11=({ dn_t_slice0_U8 dn_tmp_12=({ dn_utf8 dn_tmp_13=v_filename;
((dn_t_slice0_U8){dn_tmp_13.data,dn_tmp_13.len}); });
dn_t_slice0_U8 dn_tmp_14=({ dn_utf8 dn_tmp_15=((dn_utf8){(const uint8_t*)"Check-Driver.cmd", 16});
((dn_t_slice0_U8){dn_tmp_15.data,dn_tmp_15.len}); });
dn_f_bytes_equal(dn_tmp_12,dn_tmp_14); });
int32_t dn_tmp_16=((int32_t)(1ULL));
dn_f_launch(dn_tmp_4,dn_tmp_5,dn_tmp_10,dn_tmp_11,dn_tmp_16); })); });
if((!(({ void * dn_tmp_17=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_18=(&(v_owner));
(&((*dn_tmp_18)->value)); })))));
dn_f_handle_valid(dn_tmp_17); })))){
(void)(({ dn_r_Gui * dn_tmp_19=v_g;
dn_t_slice0_U8 dn_tmp_20=({ dn_utf8 dn_tmp_21=((dn_utf8){(const uint8_t*)"\320\235\320\265 \321\203\320\264\320\260\320\273\320\276\321\201\321\214 \320\276\321\202\320\272\321\200\321\213\321\202\321\214 \321\204\320\260\320\271\320\273. \320\237\321\200\320\276\320\262\320\265\321\200\321\214\321\202\320\265 \321\201\320\276\321\201\321\202\320\260\320\262 \320\276\320\261\320\275\320\276\320\262\320\273\320\265\320\275\320\270\321\217.", 98});
((dn_t_slice0_U8){dn_tmp_21.data,dn_tmp_21.len}); });
dn_f_ui_log(dn_tmp_19,dn_tmp_20); }));
}
else{
}
dn_drop_resource_h_close_handle(&v_owner);
dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_auxiliary(dn_r_Gui * v_g,dn_utf8 v_filename,dn_t_slice0_U8 v_parameters) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_auxiliary(v_g,v_filename,v_parameters);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static dn_t_buf_U8 dn_f_training_stop_path(dn_r_Gui const * v_g) {
{
dn_t_buf_U8 volatile v_path={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_path=({ dn_t_slice0_U8 dn_tmp_2=({ dn_utf8 dn_tmp_3=((dn_utf8){(const uint8_t*)"logs/training-", 14});
((dn_t_slice0_U8){dn_tmp_3.data,dn_tmp_3.len}); });
dn_f_bytes_copy(dn_tmp_2); });
(void)(({ dn_t_buf_U8 * dn_tmp_4=(&(v_path));
dn_t_slice0_U8 dn_tmp_5=({ dn_t_buf_U8 const * dn_tmp_6=(&((v_g)->f_session));
size_t dn_tmp_7=((size_t)(0ULL));
size_t dn_tmp_8=({ dn_t_buf_U8 const * dn_tmp_9=(&((v_g)->f_session));
(*(dn_tmp_9)).len; });
({ dn_range(dn_tmp_7,dn_tmp_8,(*(dn_tmp_6)).len);(dn_t_slice0_U8){(*(dn_tmp_6)).data?(*(dn_tmp_6)).data+dn_tmp_7:NULL,dn_tmp_8}; }); });
dn_f_bytes_append(dn_tmp_4,dn_tmp_5); }));
(void)(({ dn_t_buf_U8 * dn_tmp_10=(&(v_path));
dn_t_slice0_U8 dn_tmp_11=({ dn_utf8 dn_tmp_12=((dn_utf8){(const uint8_t*)".stop", 5});
((dn_t_slice0_U8){dn_tmp_12.data,dn_tmp_12.len}); });
dn_f_bytes_append(dn_tmp_10,dn_tmp_11); }));
{ dn_t_buf_U8 dn_tmp_14=({ dn_t_buf_U8 dn_tmp_13=v_path;v_path=(dn_t_buf_U8){0};dn_adopt_buf_U8(&dn_tmp_13);dn_tmp_13; });dn_adopt_buf_U8(&dn_tmp_14);dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_14; }
dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_t_buf_U8 dn_cb_training_stop_path(dn_r_Gui const * v_g) {
 if(!dn_enter()){return (dn_t_buf_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_buf_U8){0};}
 dn_t_buf_U8 dn_result=dn_f_training_stop_path(v_g);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_stop_training(dn_r_Gui * v_g,bool v_wait) {
{
dn_t_buf_U8 volatile v_path={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
void * v_process=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_2=(&((v_g)->f_training));
(&((*dn_tmp_2)->value)); })))));
if((!(({ void * dn_tmp_3=v_process;
dn_f_handle_valid(dn_tmp_3); })))){
{ uint8_t dn_tmp_4=0;dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_4; }
}
else{
}
v_path=({ dn_r_Gui * dn_tmp_5=v_g;
dn_f_training_stop_path(dn_tmp_5); });
(void)(({ dn_t_slice0_U8 dn_tmp_6=({ dn_t_buf_U8 const * dn_tmp_7=(&(v_path));
size_t dn_tmp_8=((size_t)(0ULL));
size_t dn_tmp_9=({ dn_t_buf_U8 const * dn_tmp_10=(&(v_path));
(*(dn_tmp_10)).len; });
({ dn_range(dn_tmp_8,dn_tmp_9,(*(dn_tmp_7)).len);(dn_t_slice0_U8){(*(dn_tmp_7)).data?(*(dn_tmp_7)).data+dn_tmp_8:NULL,dn_tmp_9}; }); });
dn_t_slice0_U8 dn_tmp_11=({ dn_utf8 dn_tmp_12=((dn_utf8){(const uint8_t*)"stop\012", 5});
((dn_t_slice0_U8){dn_tmp_12.data,dn_tmp_12.len}); });
dn_f_write_file(dn_tmp_6,dn_tmp_11); }));
(void)(({ dn_r_Gui * dn_tmp_13=v_g;
dn_t_slice0_U8 dn_tmp_14=({ dn_utf8 dn_tmp_15=((dn_utf8){(const uint8_t*)"\320\236\321\201\321\202\320\260\320\275\320\260\320\262\320\273\320\270\320\262\320\260\321\216 \320\277\320\276\320\270\321\201\320\272. \320\227\320\260\320\262\320\265\321\200\321\210\320\260\320\265\321\202\321\201\321\217 \321\202\320\265\320\272\321\203\321\211\320\260\321\217 \320\277\321\200\320\276\320\261\320\260 \320\270 \321\201\320\275\320\270\320\274\320\260\320\265\321\202\321\201\321\217 \320\277\320\265\321\200\320\265\321\205\320\262\320\260\321\202.", 125});
((dn_t_slice0_U8){dn_tmp_15.data,dn_tmp_15.len}); });
dn_f_ui_log(dn_tmp_13,dn_tmp_14); }));
if(v_wait){
if(({ uint32_t dn_tmp_16=({ void * dn_tmp_17=v_process;
uint32_t dn_tmp_18=((uint32_t)(6000ULL));
({ uint32_t dn_foreign=dn_ext_WaitForSingleObject(dn_tmp_17,dn_tmp_18);dn_fp_default();dn_foreign; }); });
uint32_t dn_tmp_19=({ uint32_t dn_tmp_20=((uint32_t)(258ULL));
if((unsigned __int128)dn_tmp_20>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_20; });
(dn_tmp_16 == dn_tmp_19); })){
(void)(({ void * dn_tmp_21=v_process;
uint32_t dn_tmp_22=((uint32_t)(3ULL));
({ int32_t dn_foreign=dn_ext_TerminateProcess(dn_tmp_21,dn_tmp_22);dn_fp_default();dn_foreign; }); }));
(void)(({ void * dn_tmp_23=v_process;
uint32_t dn_tmp_24=((uint32_t)(1000ULL));
({ uint32_t dn_foreign=dn_ext_WaitForSingleObject(dn_tmp_23,dn_tmp_24);dn_fp_default();dn_foreign; }); }));
}
else{
}
{ dn_t_resource_h_close_handle * dn_tmp_27=({ dn_t_resource_h_close_handle * dn_tmp_25=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_25,({ ((void *){0}); })); });dn_adopt_resource_h_close_handle(&dn_tmp_27);dn_t_resource_h_close_handle * volatile *dn_tmp_26=&((v_g)->f_training);dn_drop_resource_h_close_handle(dn_tmp_26);dn_retain_resource_h_close_handle(&dn_tmp_27);*dn_tmp_26=dn_tmp_27; }
{ uint32_t dn_tmp_29=((uint32_t)(0ULL));uint32_t *dn_tmp_28=&((v_g)->f_phase);*dn_tmp_28=dn_tmp_29; }
(void)(({ dn_r_Gui * dn_tmp_30=v_g;
dn_f_running_ui(dn_tmp_30); }));
}
else{
}
dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_stop_training(dn_r_Gui * v_g,bool v_wait) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_stop_training(v_g,v_wait);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_start_training(dn_r_Gui * v_g) {
{
dn_t_buf_U8 volatile v_path={0};
dn_t_buf_U8 volatile v_args={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_args);
dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
(void)(({ dn_r_Gui * dn_tmp_2=v_g;
dn_f_gui_stop(dn_tmp_2); }));
if((({ void * dn_tmp_3=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_4=(&((v_g)->f_process));
(&((*dn_tmp_4)->value)); })))));
dn_f_handle_valid(dn_tmp_3); }) || ({ void * dn_tmp_5=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_6=(&((v_g)->f_training));
(&((*dn_tmp_6)->value)); })))));
dn_f_handle_valid(dn_tmp_5); }))){
{ uint8_t dn_tmp_7=0;dn_drop_buf_U8(&v_args);
dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_7; }
}
else{
}
{ dn_t_buf_U8 dn_tmp_10=({ size_t dn_tmp_8=((size_t)(0ULL));
dn_new_buf_U8(dn_tmp_8); });dn_adopt_buf_U8(&dn_tmp_10);dn_t_buf_U8 volatile *dn_tmp_9=&((v_g)->f_session);dn_drop_buf_U8(dn_tmp_9);dn_retain_buf_U8(&dn_tmp_10);*dn_tmp_9=dn_tmp_10; }
(void)(({ dn_t_buf_U8 * dn_tmp_11=(&((v_g)->f_session));
uint64_t dn_tmp_12=({ uint32_t dn_tmp_13=({ ({ uint32_t dn_foreign=dn_ext_GetCurrentProcessId();dn_fp_default();dn_foreign; }); });
if((unsigned __int128)dn_tmp_13>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_13; });
dn_f_append_number(dn_tmp_11,dn_tmp_12); }));
(void)(({ dn_t_buf_U8 * dn_tmp_14=(&((v_g)->f_session));
uint8_t dn_tmp_15=({ uint8_t dn_tmp_16=((uint8_t)(45ULL));
if((unsigned __int128)dn_tmp_16>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_16; });
dn_push_buf_U8(dn_tmp_14,dn_tmp_15); }));
(void)(({ dn_t_buf_U8 * dn_tmp_17=(&((v_g)->f_session));
uint64_t dn_tmp_18=({ ({ uint64_t dn_foreign=dn_ext_GetTickCount64();dn_fp_default();dn_foreign; }); });
dn_f_append_number(dn_tmp_17,dn_tmp_18); }));
v_path=({ dn_utf8 dn_tmp_19=((dn_utf8){(const uint8_t*)"Train.ps1", 9});
dn_f_local_path(dn_tmp_19); });
v_args=({ dn_t_slice0_U8 dn_tmp_20=({ dn_utf8 dn_tmp_21=((dn_utf8){(const uint8_t*)"-NoProfile -ExecutionPolicy Bypass -File \042", 42});
((dn_t_slice0_U8){dn_tmp_21.data,dn_tmp_21.len}); });
dn_f_bytes_copy(dn_tmp_20); });
(void)(({ dn_t_buf_U8 * dn_tmp_22=(&(v_args));
dn_t_slice0_U8 dn_tmp_23=({ dn_t_buf_U8 const * dn_tmp_24=(&(v_path));
size_t dn_tmp_25=((size_t)(0ULL));
size_t dn_tmp_26=({ dn_t_buf_U8 const * dn_tmp_27=(&(v_path));
(*(dn_tmp_27)).len; });
({ dn_range(dn_tmp_25,dn_tmp_26,(*(dn_tmp_24)).len);(dn_t_slice0_U8){(*(dn_tmp_24)).data?(*(dn_tmp_24)).data+dn_tmp_25:NULL,dn_tmp_26}; }); });
dn_f_bytes_append(dn_tmp_22,dn_tmp_23); }));
(void)(({ dn_t_buf_U8 * dn_tmp_28=(&(v_args));
dn_t_slice0_U8 dn_tmp_29=({ dn_utf8 dn_tmp_30=((dn_utf8){(const uint8_t*)"\042 -Seconds 600 -Target ", 23});
((dn_t_slice0_U8){dn_tmp_30.data,dn_tmp_30.len}); });
dn_f_bytes_append(dn_tmp_28,dn_tmp_29); }));
(void)(({ dn_t_buf_U8 * dn_tmp_31=(&(v_args));
uint64_t dn_tmp_32=({ uint32_t dn_tmp_33=({ dn_r_Gui * dn_tmp_34=v_g;
dn_f_selected_target(dn_tmp_34); });
if((unsigned __int128)dn_tmp_33>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_33; });
dn_f_append_number(dn_tmp_31,dn_tmp_32); }));
(void)(({ dn_t_buf_U8 * dn_tmp_35=(&(v_args));
dn_t_slice0_U8 dn_tmp_36=({ dn_utf8 dn_tmp_37=((dn_utf8){(const uint8_t*)" -OwnerPid ", 11});
((dn_t_slice0_U8){dn_tmp_37.data,dn_tmp_37.len}); });
dn_f_bytes_append(dn_tmp_35,dn_tmp_36); }));
(void)(({ dn_t_buf_U8 * dn_tmp_38=(&(v_args));
uint64_t dn_tmp_39=({ uint32_t dn_tmp_40=({ ({ uint32_t dn_foreign=dn_ext_GetCurrentProcessId();dn_fp_default();dn_foreign; }); });
if((unsigned __int128)dn_tmp_40>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(uint64_t)dn_tmp_40; });
dn_f_append_number(dn_tmp_38,dn_tmp_39); }));
(void)(({ dn_t_buf_U8 * dn_tmp_41=(&(v_args));
dn_t_slice0_U8 dn_tmp_42=({ dn_utf8 dn_tmp_43=((dn_utf8){(const uint8_t*)" -Session ", 10});
((dn_t_slice0_U8){dn_tmp_43.data,dn_tmp_43.len}); });
dn_f_bytes_append(dn_tmp_41,dn_tmp_42); }));
(void)(({ dn_t_buf_U8 * dn_tmp_44=(&(v_args));
dn_t_slice0_U8 dn_tmp_45=({ dn_t_buf_U8 const * dn_tmp_46=(&((v_g)->f_session));
size_t dn_tmp_47=((size_t)(0ULL));
size_t dn_tmp_48=({ dn_t_buf_U8 const * dn_tmp_49=(&((v_g)->f_session));
(*(dn_tmp_49)).len; });
({ dn_range(dn_tmp_47,dn_tmp_48,(*(dn_tmp_46)).len);(dn_t_slice0_U8){(*(dn_tmp_46)).data?(*(dn_tmp_46)).data+dn_tmp_47:NULL,dn_tmp_48}; }); });
dn_f_bytes_append(dn_tmp_44,dn_tmp_45); }));
{ dn_t_resource_h_close_handle * dn_tmp_62=({ dn_t_resource_h_close_handle * dn_tmp_50=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_50,({ dn_r_Gui * dn_tmp_51=v_g;
dn_t_slice0_U8 dn_tmp_52=({ dn_utf8 dn_tmp_53=((dn_utf8){(const uint8_t*)"powershell.exe", 14});
((dn_t_slice0_U8){dn_tmp_53.data,dn_tmp_53.len}); });
dn_t_slice0_U8 dn_tmp_54=({ dn_t_buf_U8 const * dn_tmp_55=(&(v_args));
size_t dn_tmp_56=((size_t)(0ULL));
size_t dn_tmp_57=({ dn_t_buf_U8 const * dn_tmp_58=(&(v_args));
(*(dn_tmp_58)).len; });
({ dn_range(dn_tmp_56,dn_tmp_57,(*(dn_tmp_55)).len);(dn_t_slice0_U8){(*(dn_tmp_55)).data?(*(dn_tmp_55)).data+dn_tmp_56:NULL,dn_tmp_57}; }); });
bool dn_tmp_59=1;
int32_t dn_tmp_60=((int32_t)(0ULL));
dn_f_launch(dn_tmp_51,dn_tmp_52,dn_tmp_54,dn_tmp_59,dn_tmp_60); })); });dn_adopt_resource_h_close_handle(&dn_tmp_62);dn_t_resource_h_close_handle * volatile *dn_tmp_61=&((v_g)->f_training);dn_drop_resource_h_close_handle(dn_tmp_61);dn_retain_resource_h_close_handle(&dn_tmp_62);*dn_tmp_61=dn_tmp_62; }
if((!(({ void * dn_tmp_63=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_64=(&((v_g)->f_training));
(&((*dn_tmp_64)->value)); })))));
dn_f_handle_valid(dn_tmp_63); })))){
{ uint32_t dn_tmp_66=((uint32_t)(4ULL));uint32_t *dn_tmp_65=&((v_g)->f_phase);*dn_tmp_65=dn_tmp_66; }
(void)(({ dn_r_Gui * dn_tmp_67=v_g;
dn_t_slice0_U8 dn_tmp_68=({ dn_utf8 dn_tmp_69=((dn_utf8){(const uint8_t*)"\320\236\320\261\321\203\321\207\320\265\320\275\320\270\320\265 \320\275\320\265 \320\267\320\260\320\277\321\203\321\211\320\265\320\275\320\276. \320\222\320\276\320\267\320\274\320\276\320\266\320\275\320\260 \320\276\321\202\320\274\320\265\320\275\320\260 UAC \320\270\320\273\320\270 \320\276\321\202\321\201\321\203\321\202\321\201\321\202\320\262\320\270\320\265 Train.ps1.", 112});
((dn_t_slice0_U8){dn_tmp_69.data,dn_tmp_69.len}); });
dn_f_ui_log(dn_tmp_67,dn_tmp_68); }));
{ uint8_t dn_tmp_70=0;dn_drop_buf_U8(&v_args);
dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_70; }
}
else{
}
{ uint32_t dn_tmp_72=((uint32_t)(5ULL));uint32_t *dn_tmp_71=&((v_g)->f_phase);*dn_tmp_71=dn_tmp_72; }
{ bool dn_tmp_74=0;bool *dn_tmp_73=&((v_g)->f_history);*dn_tmp_73=dn_tmp_74; }
{ dn_r_TrainingView dn_tmp_76=({ ((dn_r_TrainingView){0}); });dn_r_TrainingView *dn_tmp_75=&((v_g)->f_view);*dn_tmp_75=dn_tmp_76; }
{ uint32_t dn_tmp_78=((uint32_t)(600ULL));uint32_t *dn_tmp_77=&(((v_g)->f_view).f_budget);*dn_tmp_77=dn_tmp_78; }
{ uint32_t dn_tmp_81=({ dn_r_Gui * dn_tmp_79=v_g;
dn_f_selected_target(dn_tmp_79); });uint32_t *dn_tmp_80=&(((v_g)->f_view).f_target);*dn_tmp_80=dn_tmp_81; }
(void)(({ dn_r_Gui * dn_tmp_82=v_g;
dn_f_running_ui(dn_tmp_82); }));
(void)(({ dn_r_Gui * dn_tmp_83=v_g;
dn_t_slice0_U8 dn_tmp_84=({ dn_utf8 dn_tmp_85=((dn_utf8){(const uint8_t*)"\320\232\320\276\320\275\321\202\321\200\320\276\320\273\321\214 \320\261\320\265\320\267 \320\276\320\261\321\200\320\260\320\261\320\276\321\202\320\272\320\270, \320\272\320\260\320\275\320\264\320\270\320\264\320\260\321\202\321\213, \320\277\320\276\320\262\321\202\320\276\321\200\320\275\320\260\321\217 \320\277\321\200\320\276\320\262\320\265\321\200\320\272\320\260 \320\262\321\213\320\261\321\200\320\260\320\275\320\275\320\276\320\263\320\276 \321\201\320\265\321\200\320\262\320\270\321\201\320\260. Telegram \320\277\321\200\320\276\320\262\320\265\321\200\321\217\320\265\321\202\321\201\321\217 \320\277\320\276 HTTPS, WSS \320\270 MTProto TCP.", 200});
((dn_t_slice0_U8){dn_tmp_85.data,dn_tmp_85.len}); });
dn_f_ui_log(dn_tmp_83,dn_tmp_84); }));
dn_drop_buf_U8(&v_args);
dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_start_training(dn_r_Gui * v_g) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_start_training(v_g);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_poll_training(dn_r_Gui * v_g) {
{
dn_t_buf_U8 volatile v_text={0};
dn_t_buf_U8 volatile v_session={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_session);
dn_drop_buf_U8(&v_text);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
void * v_process=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_2=(&((v_g)->f_training));
(&((*dn_tmp_2)->value)); })))));
if((!(({ void * dn_tmp_3=v_process;
dn_f_handle_valid(dn_tmp_3); })))){
{ uint8_t dn_tmp_4=0;dn_drop_buf_U8(&v_session);
dn_drop_buf_U8(&v_text);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_4; }
}
else{
}
v_text=({ dn_t_slice0_U8 dn_tmp_5=({ dn_utf8 dn_tmp_6=((dn_utf8){(const uint8_t*)"logs/training.status", 20});
((dn_t_slice0_U8){dn_tmp_6.data,dn_tmp_6.len}); });
size_t dn_tmp_7=((size_t)(16384ULL));
dn_f_read_file(dn_tmp_5,dn_tmp_7); });
v_session=({ dn_t_slice0_U8 dn_tmp_8=({ dn_utf8 dn_tmp_9=((dn_utf8){(const uint8_t*)"session=", 8});
((dn_t_slice0_U8){dn_tmp_9.data,dn_tmp_9.len}); });
dn_f_bytes_copy(dn_tmp_8); });
(void)(({ dn_t_buf_U8 * dn_tmp_10=(&(v_session));
dn_t_slice0_U8 dn_tmp_11=({ dn_t_buf_U8 const * dn_tmp_12=(&((v_g)->f_session));
size_t dn_tmp_13=((size_t)(0ULL));
size_t dn_tmp_14=({ dn_t_buf_U8 const * dn_tmp_15=(&((v_g)->f_session));
(*(dn_tmp_15)).len; });
({ dn_range(dn_tmp_13,dn_tmp_14,(*(dn_tmp_12)).len);(dn_t_slice0_U8){(*(dn_tmp_12)).data?(*(dn_tmp_12)).data+dn_tmp_13:NULL,dn_tmp_14}; }); });
dn_f_bytes_append(dn_tmp_10,dn_tmp_11); }));
bool v_matched=0;
if(({ size_t dn_tmp_16=({ dn_t_slice0_U8 dn_tmp_17=({ dn_t_buf_U8 const * dn_tmp_18=(&(v_text));
size_t dn_tmp_19=((size_t)(0ULL));
size_t dn_tmp_20=({ dn_t_buf_U8 const * dn_tmp_21=(&(v_text));
(*(dn_tmp_21)).len; });
({ dn_range(dn_tmp_19,dn_tmp_20,(*(dn_tmp_18)).len);(dn_t_slice0_U8){(*(dn_tmp_18)).data?(*(dn_tmp_18)).data+dn_tmp_19:NULL,dn_tmp_20}; }); });
size_t dn_tmp_22=((size_t)(0ULL));
dn_t_slice0_U8 dn_tmp_23=({ dn_t_buf_U8 const * dn_tmp_24=(&(v_session));
size_t dn_tmp_25=((size_t)(0ULL));
size_t dn_tmp_26=({ dn_t_buf_U8 const * dn_tmp_27=(&(v_session));
(*(dn_tmp_27)).len; });
({ dn_range(dn_tmp_25,dn_tmp_26,(*(dn_tmp_24)).len);(dn_t_slice0_U8){(*(dn_tmp_24)).data?(*(dn_tmp_24)).data+dn_tmp_25:NULL,dn_tmp_26}; }); });
dn_f_bytes_find(dn_tmp_17,dn_tmp_22,dn_tmp_23); });
size_t dn_tmp_28=({ dn_t_buf_U8 const * dn_tmp_29=(&(v_text));
(*(dn_tmp_29)).len; });
(dn_tmp_16 < dn_tmp_28); })){
dn_r_TrainingView v_view=({ ((dn_r_TrainingView){0}); });
if(({ dn_t_slice0_U8 dn_tmp_30=({ dn_t_buf_U8 const * dn_tmp_31=(&(v_text));
size_t dn_tmp_32=((size_t)(0ULL));
size_t dn_tmp_33=({ dn_t_buf_U8 const * dn_tmp_34=(&(v_text));
(*(dn_tmp_34)).len; });
({ dn_range(dn_tmp_32,dn_tmp_33,(*(dn_tmp_31)).len);(dn_t_slice0_U8){(*(dn_tmp_31)).data?(*(dn_tmp_31)).data+dn_tmp_32:NULL,dn_tmp_33}; }); });
dn_r_TrainingView * dn_tmp_35=(&(v_view));
dn_f_training_view(dn_tmp_30,dn_tmp_35); })){
{ dn_r_TrainingView dn_tmp_37=v_view;dn_r_TrainingView *dn_tmp_36=&((v_g)->f_view);*dn_tmp_36=dn_tmp_37; }
{ bool dn_tmp_39=1;bool *dn_tmp_38=&(v_matched);*dn_tmp_38=dn_tmp_39; }
}
else{
}
}
else{
}
uint32_t v_code=((uint32_t)(259ULL));
if(({ int32_t dn_tmp_40=({ void * dn_tmp_41=v_process;
uint32_t * dn_tmp_42=({ uint32_t * dn_tmp_43=({ uint32_t * dn_tmp_44=(&(v_code));
((uint32_t *)dn_tmp_44); });
((uint32_t *)dn_tmp_43); });
({ int32_t dn_foreign=dn_ext_GetExitCodeProcess(dn_tmp_41,dn_tmp_42);dn_fp_default();dn_foreign; }); });
int32_t dn_tmp_45=({ int32_t dn_tmp_46=((int32_t)(0ULL));
if((__int128)dn_tmp_46<(-((__int128)1<<31))||(__int128)dn_tmp_46>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_46; });
(dn_tmp_40 == dn_tmp_45); })){
{ uint32_t dn_tmp_48=((uint32_t)(2ULL));uint32_t *dn_tmp_47=&(v_code);*dn_tmp_47=dn_tmp_48; }
}
else{
}
if(({ uint32_t dn_tmp_49=v_code;
uint32_t dn_tmp_50=({ uint32_t dn_tmp_51=((uint32_t)(259ULL));
if((unsigned __int128)dn_tmp_51>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_51; });
(dn_tmp_49 != dn_tmp_50); })){
{ dn_t_resource_h_close_handle * dn_tmp_54=({ dn_t_resource_h_close_handle * dn_tmp_52=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_52,({ ((void *){0}); })); });dn_adopt_resource_h_close_handle(&dn_tmp_54);dn_t_resource_h_close_handle * volatile *dn_tmp_53=&((v_g)->f_training);dn_drop_resource_h_close_handle(dn_tmp_53);dn_retain_resource_h_close_handle(&dn_tmp_54);*dn_tmp_53=dn_tmp_54; }
{ uint32_t dn_tmp_56=((uint32_t)(0ULL));uint32_t *dn_tmp_55=&((v_g)->f_phase);*dn_tmp_55=dn_tmp_56; }
if(((({ uint32_t dn_tmp_57=v_code;
uint32_t dn_tmp_58=({ uint32_t dn_tmp_59=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_59>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_59; });
(dn_tmp_57 != dn_tmp_58); }) || (!(v_matched))) || ({ uint32_t dn_tmp_60=((v_g)->f_view).f_phase;
uint32_t dn_tmp_61=({ uint32_t dn_tmp_62=((uint32_t)(4ULL));
if((unsigned __int128)dn_tmp_62>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_62; });
(dn_tmp_60 == dn_tmp_61); }))){
{ uint32_t dn_tmp_64=((uint32_t)(6ULL));uint32_t *dn_tmp_63=&((v_g)->f_phase);*dn_tmp_63=dn_tmp_64; }
(void)(({ dn_r_Gui * dn_tmp_65=v_g;
dn_t_slice0_U8 dn_tmp_66=({ dn_utf8 dn_tmp_67=((dn_utf8){(const uint8_t*)"\320\237\320\276\320\270\321\201\320\272 \320\267\320\260\320\262\320\265\321\200\321\210\321\221\320\275 \321\201 \320\276\321\210\320\270\320\261\320\272\320\276\320\271. \320\241\320\276\320\261\320\265\321\200\320\270\321\202\320\265 \320\276\321\202\321\207\321\221\321\202: \320\276\320\275 \321\201\320\276\320\264\320\265\321\200\320\266\320\270\321\202 training.status \320\270 \321\200\320\265\320\267\321\203\320\273\321\214\321\202\320\260\321\202\321\213 \320\277\321\200\320\276\320\261.", 147});
((dn_t_slice0_U8){dn_tmp_67.data,dn_tmp_67.len}); });
dn_f_ui_log(dn_tmp_65,dn_tmp_66); }));
}
else{
if(({ uint32_t dn_tmp_68=((v_g)->f_view).f_validated;
uint32_t dn_tmp_69=({ uint32_t dn_tmp_70=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_70>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_70; });
(dn_tmp_68 == dn_tmp_69); })){
(void)(({ dn_r_Gui * dn_tmp_71=v_g;
dn_t_slice0_U8 dn_tmp_72=({ dn_utf8 dn_tmp_73=((dn_utf8){(const uint8_t*)"\320\243\320\273\321\203\321\207\321\210\320\265\320\275\320\270\320\265 \321\201\320\265\321\202\320\265\320\262\321\213\321\205 \320\277\321\200\320\276\320\261 \320\277\320\276\320\264\321\202\320\262\320\265\321\200\320\266\320\264\320\265\320\275\320\276. \320\237\321\200\320\276\321\204\320\270\320\273\321\214 \320\260\320\272\321\202\320\270\320\262\320\270\321\200\320\276\320\262\320\260\320\275 \320\264\320\273\321\217 \321\201\320\273\320\265\320\264\321\203\321\216\321\211\320\265\320\263\320\276 \320\262\320\272\320\273\321\216\321\207\320\265\320\275\320\270\321\217; \320\262\320\270\320\264\320\265\320\276 \320\270 \320\263\320\276\320\273\320\276\321\201 \320\277\321\200\320\276\320\262\320\265\321\200\321\214\321\202\320\265 \320\276\321\202\320\264\320\265\320\273\321\214\320\275\320\276.", 216});
((dn_t_slice0_U8){dn_tmp_73.data,dn_tmp_73.len}); });
dn_f_ui_log(dn_tmp_71,dn_tmp_72); }));
}
else{
(void)(({ dn_r_Gui * dn_tmp_74=v_g;
dn_t_slice0_U8 dn_tmp_75=({ dn_utf8 dn_tmp_76=((dn_utf8){(const uint8_t*)"\320\237\320\276\320\264\321\202\320\262\320\265\321\200\320\266\320\264\321\221\320\275\320\275\320\276\320\263\320\276 \321\203\320\273\321\203\321\207\321\210\320\265\320\275\320\270\321\217 \320\275\320\265\321\202. \320\237\321\200\320\265\320\266\320\275\320\270\320\271 \320\260\320\272\321\202\320\270\320\262\320\275\321\213\320\271 \320\277\321\200\320\276\321\204\320\270\320\273\321\214 \321\201\320\276\321\205\321\200\320\260\320\275\321\221\320\275; \320\274\320\276\320\266\320\275\320\276 \321\201\320\276\321\205\321\200\320\260\320\275\320\270\321\202\321\214 \320\270\321\201\321\201\320\273\320\265\320\264\320\276\320\262\320\260\320\275\320\275\321\213\320\271 \320\272\320\260\320\275\320\264\320\270\320\264\320\260\321\202 \320\276\321\202\320\264\320\265\320\273\321\214\320\275\320\276.", 214});
((dn_t_slice0_U8){dn_tmp_76.data,dn_tmp_76.len}); });
dn_f_ui_log(dn_tmp_74,dn_tmp_75); }));
}
}
(void)(({ dn_r_Gui * dn_tmp_77=v_g;
dn_f_running_ui(dn_tmp_77); }));
if(v_matched){
size_t v_at=({ dn_t_slice0_U8 dn_tmp_78=({ dn_t_buf_U8 const * dn_tmp_79=(&(v_text));
size_t dn_tmp_80=((size_t)(0ULL));
size_t dn_tmp_81=({ dn_t_buf_U8 const * dn_tmp_82=(&(v_text));
(*(dn_tmp_82)).len; });
({ dn_range(dn_tmp_80,dn_tmp_81,(*(dn_tmp_79)).len);(dn_t_slice0_U8){(*(dn_tmp_79)).data?(*(dn_tmp_79)).data+dn_tmp_80:NULL,dn_tmp_81}; }); });
size_t dn_tmp_83=((size_t)(0ULL));
dn_t_slice0_U8 dn_tmp_84=({ dn_utf8 dn_tmp_85=((dn_utf8){(const uint8_t*)"message=", 8});
((dn_t_slice0_U8){dn_tmp_85.data,dn_tmp_85.len}); });
dn_f_bytes_find(dn_tmp_78,dn_tmp_83,dn_tmp_84); });
if(({ size_t dn_tmp_86=v_at;
size_t dn_tmp_87=({ dn_t_buf_U8 const * dn_tmp_88=(&(v_text));
(*(dn_tmp_88)).len; });
(dn_tmp_86 < dn_tmp_87); })){
dn_t_buf_U8 volatile v_note={0};
dn_boundary dn_tmp_89={0};jmp_buf dn_tmp_90;dn_tmp_89.jump=&dn_tmp_90;dn_tmp_89.previous=dn_current;dn_tmp_89.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_89;if(setjmp(dn_tmp_90)){dn_current=dn_tmp_89.previous;dn_drop_buf_U8(&v_note);
dn_boundary_end(&dn_tmp_89,1);dn_raise();}
size_t v_end=({ dn_t_slice0_U8 dn_tmp_91=({ dn_t_buf_U8 const * dn_tmp_92=(&(v_text));
size_t dn_tmp_93=((size_t)(0ULL));
size_t dn_tmp_94=({ dn_t_buf_U8 const * dn_tmp_95=(&(v_text));
(*(dn_tmp_95)).len; });
({ dn_range(dn_tmp_93,dn_tmp_94,(*(dn_tmp_92)).len);(dn_t_slice0_U8){(*(dn_tmp_92)).data?(*(dn_tmp_92)).data+dn_tmp_93:NULL,dn_tmp_94}; }); });
size_t dn_tmp_96=v_at;
uint8_t dn_tmp_97=({ uint8_t dn_tmp_98=((uint8_t)(10ULL));
if((unsigned __int128)dn_tmp_98>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_98; });
dn_f_byte_find(dn_tmp_91,dn_tmp_96,dn_tmp_97); });
v_note=({ dn_t_slice0_U8 dn_tmp_99=({ dn_t_buf_U8 const * dn_tmp_100=(&((v_g)->f_note));
size_t dn_tmp_101=((size_t)(0ULL));
size_t dn_tmp_102=({ dn_t_buf_U8 const * dn_tmp_103=(&((v_g)->f_note));
(*(dn_tmp_103)).len; });
({ dn_range(dn_tmp_101,dn_tmp_102,(*(dn_tmp_100)).len);(dn_t_slice0_U8){(*(dn_tmp_100)).data?(*(dn_tmp_100)).data+dn_tmp_101:NULL,dn_tmp_102}; }); });
dn_f_bytes_copy(dn_tmp_99); });
(void)(({ dn_t_buf_U8 * dn_tmp_104=(&(v_note));
dn_t_slice0_U8 dn_tmp_105=({ dn_utf8 dn_tmp_106=((dn_utf8){(const uint8_t*)"\012", 1});
((dn_t_slice0_U8){dn_tmp_106.data,dn_tmp_106.len}); });
dn_f_bytes_append(dn_tmp_104,dn_tmp_105); }));
(void)(({ dn_t_buf_U8 * dn_tmp_107=(&(v_note));
dn_t_slice0_U8 dn_tmp_108=({ dn_t_buf_U8 const * dn_tmp_109=(&(v_text));
size_t dn_tmp_110=({ size_t dn_tmp_111=v_at;
size_t dn_tmp_112=((size_t)(8ULL));
dn_USize_add(dn_tmp_111,dn_tmp_112); });
size_t dn_tmp_113=({ size_t dn_tmp_114=({ size_t dn_tmp_115=({ size_t dn_tmp_116=v_end;
size_t dn_tmp_117=v_at;
dn_USize_sub(dn_tmp_116,dn_tmp_117); });
size_t dn_tmp_118=((size_t)(8ULL));
dn_USize_sub(dn_tmp_115,dn_tmp_118); });
size_t dn_tmp_119=((size_t)(300ULL));
dn_f_min_size(dn_tmp_114,dn_tmp_119); });
({ dn_range(dn_tmp_110,dn_tmp_113,(*(dn_tmp_109)).len);(dn_t_slice0_U8){(*(dn_tmp_109)).data?(*(dn_tmp_109)).data+dn_tmp_110:NULL,dn_tmp_113}; }); });
dn_f_bytes_append(dn_tmp_107,dn_tmp_108); }));
(void)(({ dn_r_Gui * dn_tmp_120=v_g;
dn_t_slice0_U8 dn_tmp_121=({ dn_t_buf_U8 const * dn_tmp_122=(&(v_note));
size_t dn_tmp_123=((size_t)(0ULL));
size_t dn_tmp_124=({ dn_t_buf_U8 const * dn_tmp_125=(&(v_note));
(*(dn_tmp_125)).len; });
({ dn_range(dn_tmp_123,dn_tmp_124,(*(dn_tmp_122)).len);(dn_t_slice0_U8){(*(dn_tmp_122)).data?(*(dn_tmp_122)).data+dn_tmp_123:NULL,dn_tmp_124}; }); });
dn_f_ui_log(dn_tmp_120,dn_tmp_121); }));
dn_drop_buf_U8(&v_note);
dn_boundary_end(&dn_tmp_89,0);
}
else{
}
}
else{
}
}
else{
}
(void)(({ void * dn_tmp_126=(v_g)->f_window;
dn_r_Rect * dn_tmp_127=({ ((dn_r_Rect *)NULL); });
int32_t dn_tmp_128=((int32_t)(1ULL));
({ int32_t dn_foreign=dn_ext_InvalidateRect(dn_tmp_126,dn_tmp_127,dn_tmp_128);dn_fp_default();dn_foreign; }); }));
(void)(({ void * dn_tmp_129=({ dn_r_Gui * dn_tmp_130=v_g;
size_t dn_tmp_131=((size_t)(16ULL));
dn_f_control(dn_tmp_130,dn_tmp_131); });
dn_r_Rect * dn_tmp_132=({ ((dn_r_Rect *)NULL); });
int32_t dn_tmp_133=((int32_t)(1ULL));
({ int32_t dn_foreign=dn_ext_InvalidateRect(dn_tmp_129,dn_tmp_132,dn_tmp_133);dn_fp_default();dn_foreign; }); }));
dn_drop_buf_U8(&v_session);
dn_drop_buf_U8(&v_text);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_poll_training(dn_r_Gui * v_g) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_poll_training(v_g);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_profile_dialog(dn_r_Gui * v_g,bool v_save) {
{
dn_t_buf_U16 volatile v_file={0};
dn_t_buf_U16 volatile v_extension={0};
dn_t_buf_U16 volatile v_filter={0};
dn_t_buf_U8 volatile v_path={0};
dn_t_buf_U8 volatile v_contents={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_contents);
dn_drop_buf_U8(&v_path);
dn_drop_buf_U16(&v_filter);
dn_drop_buf_U16(&v_extension);
dn_drop_buf_U16(&v_file);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_file=({ size_t dn_tmp_2=((size_t)(32768ULL));
dn_new_buf_U16(dn_tmp_2); });
v_extension=({ dn_t_slice0_U8 dn_tmp_3=({ dn_utf8 dn_tmp_4=((dn_utf8){(const uint8_t*)"profile", 7});
((dn_t_slice0_U8){dn_tmp_4.data,dn_tmp_4.len}); });
dn_f_wide(dn_tmp_3); });
v_filter=({ dn_t_slice0_U8 dn_tmp_5=({ dn_utf8 dn_tmp_6=((dn_utf8){(const uint8_t*)"\320\237\321\200\320\276\321\204\320\270\320\273\321\214 NoDPI (*.profile)\000*.profile\000\320\222\321\201\320\265 \321\204\320\260\320\271\320\273\321\213\000*.*\000\000", 66});
((dn_t_slice0_U8){dn_tmp_6.data,dn_tmp_6.len}); });
dn_f_wide(dn_tmp_5); });
dn_r_OpenFileName v_dialog=({ ((dn_r_OpenFileName){0}); });
{ uint32_t dn_tmp_9=({ size_t dn_tmp_7=({ ((size_t)sizeof(dn_r_OpenFileName)); });
if((unsigned __int128)dn_tmp_7>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_7; });uint32_t *dn_tmp_8=&((v_dialog).f_size);*dn_tmp_8=dn_tmp_9; }
{ void * dn_tmp_11=(v_g)->f_window;void * *dn_tmp_10=&((v_dialog).f_owner);*dn_tmp_10=dn_tmp_11; }
{ uint16_t * dn_tmp_14=({ dn_t_buf_U16 const * dn_tmp_12=(&(v_filter));
((uint16_t *)(*(dn_tmp_12)).data); });uint16_t * *dn_tmp_13=&((v_dialog).f_filter);*dn_tmp_13=dn_tmp_14; }
{ uint16_t * dn_tmp_17=({ dn_t_buf_U16 * dn_tmp_15=(&(v_file));
((uint16_t *)(*(dn_tmp_15)).data); });uint16_t * *dn_tmp_16=&((v_dialog).f_file);*dn_tmp_16=dn_tmp_17; }
{ uint32_t dn_tmp_19=((uint32_t)(32768ULL));uint32_t *dn_tmp_18=&((v_dialog).f_file_capacity);*dn_tmp_18=dn_tmp_19; }
{ uint16_t * dn_tmp_22=({ dn_t_buf_U16 const * dn_tmp_20=(&(v_extension));
((uint16_t *)(*(dn_tmp_20)).data); });uint16_t * *dn_tmp_21=&((v_dialog).f_default_extension);*dn_tmp_21=dn_tmp_22; }
{ uint32_t dn_tmp_24=((uint32_t)(0x81808ULL));uint32_t *dn_tmp_23=&((v_dialog).f_flags);*dn_tmp_23=dn_tmp_24; }
int32_t v_selected=((int32_t)(0ULL));
if(v_save){
{ uint32_t dn_tmp_26=((uint32_t)(0x8080aULL));uint32_t *dn_tmp_25=&((v_dialog).f_flags);*dn_tmp_25=dn_tmp_26; }
{ int32_t dn_tmp_31=({ dn_r_OpenFileName * dn_tmp_27=({ dn_r_OpenFileName * dn_tmp_28=({ dn_r_OpenFileName * dn_tmp_29=(&(v_dialog));
((dn_r_OpenFileName *)dn_tmp_29); });
((dn_r_OpenFileName *)dn_tmp_28); });
({ int32_t dn_foreign=dn_ext_GetSaveFileNameW(dn_tmp_27);dn_fp_default();dn_foreign; }); });int32_t *dn_tmp_30=&(v_selected);*dn_tmp_30=dn_tmp_31; }
}
else{
{ int32_t dn_tmp_36=({ dn_r_OpenFileName * dn_tmp_32=({ dn_r_OpenFileName * dn_tmp_33=({ dn_r_OpenFileName * dn_tmp_34=(&(v_dialog));
((dn_r_OpenFileName *)dn_tmp_34); });
((dn_r_OpenFileName *)dn_tmp_33); });
({ int32_t dn_foreign=dn_ext_GetOpenFileNameW(dn_tmp_32);dn_fp_default();dn_foreign; }); });int32_t *dn_tmp_35=&(v_selected);*dn_tmp_35=dn_tmp_36; }
}
if(({ int32_t dn_tmp_37=v_selected;
int32_t dn_tmp_38=({ int32_t dn_tmp_39=((int32_t)(0ULL));
if((__int128)dn_tmp_39<(-((__int128)1<<31))||(__int128)dn_tmp_39>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_39; });
(dn_tmp_37 == dn_tmp_38); })){
{ uint8_t dn_tmp_40=0;dn_drop_buf_U8(&v_contents);
dn_drop_buf_U8(&v_path);
dn_drop_buf_U16(&v_filter);
dn_drop_buf_U16(&v_extension);
dn_drop_buf_U16(&v_file);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_40; }
}
else{
}
v_path=({ uint16_t * dn_tmp_41=({ dn_t_buf_U16 const * dn_tmp_42=(&(v_file));
((uint16_t *)(*(dn_tmp_42)).data); });
size_t dn_tmp_43=({ uint16_t * dn_tmp_44=({ dn_t_buf_U16 const * dn_tmp_45=(&(v_file));
((uint16_t *)(*(dn_tmp_45)).data); });
dn_f_wide_length(dn_tmp_44); });
dn_f_utf8_wide(dn_tmp_41,dn_tmp_43); });
v_contents=({ size_t dn_tmp_46=((size_t)(0ULL));
dn_new_buf_U8(dn_tmp_46); });
if(v_save){
if((!((v_g)->f_history))){
{ dn_t_buf_U8 dn_tmp_51=({ dn_t_slice0_U8 dn_tmp_47=({ dn_utf8 dn_tmp_48=((dn_utf8){(const uint8_t*)"logs/training-best.profile", 26});
((dn_t_slice0_U8){dn_tmp_48.data,dn_tmp_48.len}); });
size_t dn_tmp_49=((size_t)(65536ULL));
dn_f_read_file(dn_tmp_47,dn_tmp_49); });dn_adopt_buf_U8(&dn_tmp_51);dn_t_buf_U8 volatile *dn_tmp_50=&(v_contents);dn_drop_buf_U8(dn_tmp_50);dn_retain_buf_U8(&dn_tmp_51);*dn_tmp_50=dn_tmp_51; }
}
else{
}
if(({ size_t dn_tmp_52=({ dn_t_buf_U8 const * dn_tmp_53=(&(v_contents));
(*(dn_tmp_53)).len; });
size_t dn_tmp_54=((size_t)(0ULL));
(dn_tmp_52 == dn_tmp_54); })){
{ dn_t_buf_U8 dn_tmp_59=({ dn_t_slice0_U8 dn_tmp_55=({ dn_utf8 dn_tmp_56=((dn_utf8){(const uint8_t*)"active.profile", 14});
((dn_t_slice0_U8){dn_tmp_56.data,dn_tmp_56.len}); });
size_t dn_tmp_57=((size_t)(65536ULL));
dn_f_read_file(dn_tmp_55,dn_tmp_57); });dn_adopt_buf_U8(&dn_tmp_59);dn_t_buf_U8 volatile *dn_tmp_58=&(v_contents);dn_drop_buf_U8(dn_tmp_58);dn_retain_buf_U8(&dn_tmp_59);*dn_tmp_58=dn_tmp_59; }
}
else{
}
}
else{
{ dn_t_buf_U8 dn_tmp_67=({ dn_t_slice0_U8 dn_tmp_60=({ dn_t_buf_U8 const * dn_tmp_61=(&(v_path));
size_t dn_tmp_62=((size_t)(0ULL));
size_t dn_tmp_63=({ dn_t_buf_U8 const * dn_tmp_64=(&(v_path));
(*(dn_tmp_64)).len; });
({ dn_range(dn_tmp_62,dn_tmp_63,(*(dn_tmp_61)).len);(dn_t_slice0_U8){(*(dn_tmp_61)).data?(*(dn_tmp_61)).data+dn_tmp_62:NULL,dn_tmp_63}; }); });
size_t dn_tmp_65=((size_t)(65536ULL));
dn_f_read_file(dn_tmp_60,dn_tmp_65); });dn_adopt_buf_U8(&dn_tmp_67);dn_t_buf_U8 volatile *dn_tmp_66=&(v_contents);dn_drop_buf_U8(dn_tmp_66);dn_retain_buf_U8(&dn_tmp_67);*dn_tmp_66=dn_tmp_67; }
}
dn_r_Gene v_gene=({ dn_f_gene_default(); });
uint32_t v_target=((uint32_t)(0ULL));
if((!(({ dn_t_slice0_U8 dn_tmp_68=({ dn_t_buf_U8 const * dn_tmp_69=(&(v_contents));
size_t dn_tmp_70=((size_t)(0ULL));
size_t dn_tmp_71=({ dn_t_buf_U8 const * dn_tmp_72=(&(v_contents));
(*(dn_tmp_72)).len; });
({ dn_range(dn_tmp_70,dn_tmp_71,(*(dn_tmp_69)).len);(dn_t_slice0_U8){(*(dn_tmp_69)).data?(*(dn_tmp_69)).data+dn_tmp_70:NULL,dn_tmp_71}; }); });
dn_r_Gene * dn_tmp_73=(&(v_gene));
uint32_t * dn_tmp_74=(&(v_target));
dn_f_profile_gene(dn_tmp_68,dn_tmp_73,dn_tmp_74); })))){
(void)(({ dn_r_Gui * dn_tmp_75=v_g;
dn_t_slice0_U8 dn_tmp_76=({ dn_utf8 dn_tmp_77=((dn_utf8){(const uint8_t*)"\320\235\320\265\321\202 \320\272\320\276\321\200\321\200\320\265\320\272\321\202\320\275\320\276\320\263\320\276 \320\277\321\200\320\276\321\204\320\270\320\273\321\217 \320\264\320\273\321\217 \321\215\321\202\320\276\320\271 \320\276\320\277\320\265\321\200\320\260\321\206\320\270\320\270. \320\241\320\275\320\260\321\207\320\260\320\273\320\260 \320\262\321\213\320\277\320\276\320\273\320\275\320\270\321\202\320\265 \320\277\320\276\320\270\321\201\320\272 \320\270\320\273\320\270 \320\262\321\213\320\261\320\265\321\200\320\270\321\202\320\265 \321\204\320\260\320\271\320\273 Schema=1.", 166});
((dn_t_slice0_U8){dn_tmp_77.data,dn_tmp_77.len}); });
dn_f_ui_log(dn_tmp_75,dn_tmp_76); }));
{ uint8_t dn_tmp_78=0;dn_drop_buf_U8(&v_contents);
dn_drop_buf_U8(&v_path);
dn_drop_buf_U16(&v_filter);
dn_drop_buf_U16(&v_extension);
dn_drop_buf_U16(&v_file);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_78; }
}
else{
}
if(v_save){
if(({ dn_t_slice0_U8 dn_tmp_79=({ dn_t_buf_U8 const * dn_tmp_80=(&(v_path));
size_t dn_tmp_81=((size_t)(0ULL));
size_t dn_tmp_82=({ dn_t_buf_U8 const * dn_tmp_83=(&(v_path));
(*(dn_tmp_83)).len; });
({ dn_range(dn_tmp_81,dn_tmp_82,(*(dn_tmp_80)).len);(dn_t_slice0_U8){(*(dn_tmp_80)).data?(*(dn_tmp_80)).data+dn_tmp_81:NULL,dn_tmp_82}; }); });
dn_t_slice0_U8 dn_tmp_84=({ dn_t_buf_U8 const * dn_tmp_85=(&(v_contents));
size_t dn_tmp_86=((size_t)(0ULL));
size_t dn_tmp_87=({ dn_t_buf_U8 const * dn_tmp_88=(&(v_contents));
(*(dn_tmp_88)).len; });
({ dn_range(dn_tmp_86,dn_tmp_87,(*(dn_tmp_85)).len);(dn_t_slice0_U8){(*(dn_tmp_85)).data?(*(dn_tmp_85)).data+dn_tmp_86:NULL,dn_tmp_87}; }); });
dn_f_write_file(dn_tmp_79,dn_tmp_84); })){
(void)(({ dn_r_Gui * dn_tmp_89=v_g;
dn_t_slice0_U8 dn_tmp_90=({ dn_utf8 dn_tmp_91=((dn_utf8){(const uint8_t*)"\320\237\321\200\320\276\321\204\320\270\320\273\321\214 \320\270 \320\270\320\267\320\274\320\265\321\200\320\265\320\275\320\275\320\260\321\217 \320\276\321\206\320\265\320\275\320\272\320\260 \320\277\321\200\320\276\320\261 \321\201\320\276\321\205\321\200\320\260\320\275\320\265\320\275\321\213. \320\255\321\202\320\276 \320\264\320\260\320\275\320\275\321\213\320\265 \320\277\320\260\321\200\320\260\320\274\320\265\321\202\321\200\320\276\320\262, \320\275\320\265 \320\270\321\201\320\277\320\276\320\273\320\275\321\217\320\265\320\274\321\213\320\271 \320\272\320\276\320\264.", 158});
((dn_t_slice0_U8){dn_tmp_91.data,dn_tmp_91.len}); });
dn_f_ui_log(dn_tmp_89,dn_tmp_90); }));
}
else{
(void)(({ dn_r_Gui * dn_tmp_92=v_g;
dn_t_slice0_U8 dn_tmp_93=({ dn_utf8 dn_tmp_94=((dn_utf8){(const uint8_t*)"\320\235\320\265 \321\203\320\264\320\260\320\273\320\276\321\201\321\214 \321\201\320\276\321\205\321\200\320\260\320\275\320\270\321\202\321\214 \320\277\321\200\320\276\321\204\320\270\320\273\321\214.", 54});
((dn_t_slice0_U8){dn_tmp_94.data,dn_tmp_94.len}); });
dn_f_ui_log(dn_tmp_92,dn_tmp_93); }));
}
}
else{
dn_t_buf_U8 volatile v_previous={0};
dn_boundary dn_tmp_95={0};jmp_buf dn_tmp_96;dn_tmp_95.jump=&dn_tmp_96;dn_tmp_95.previous=dn_current;dn_tmp_95.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_95;if(setjmp(dn_tmp_96)){dn_current=dn_tmp_95.previous;dn_drop_buf_U8(&v_previous);
dn_boundary_end(&dn_tmp_95,1);dn_raise();}
v_previous=({ dn_t_slice0_U8 dn_tmp_97=({ dn_utf8 dn_tmp_98=((dn_utf8){(const uint8_t*)"active.profile", 14});
((dn_t_slice0_U8){dn_tmp_98.data,dn_tmp_98.len}); });
size_t dn_tmp_99=((size_t)(65536ULL));
dn_f_read_file(dn_tmp_97,dn_tmp_99); });
if((({ size_t dn_tmp_100=({ dn_t_buf_U8 const * dn_tmp_101=(&(v_previous));
(*(dn_tmp_101)).len; });
size_t dn_tmp_102=((size_t)(0ULL));
(dn_tmp_100 > dn_tmp_102); }) && (!(({ dn_t_slice0_U8 dn_tmp_103=({ dn_utf8 dn_tmp_104=((dn_utf8){(const uint8_t*)"active.profile.previous", 23});
((dn_t_slice0_U8){dn_tmp_104.data,dn_tmp_104.len}); });
dn_t_slice0_U8 dn_tmp_105=({ dn_t_buf_U8 const * dn_tmp_106=(&(v_previous));
size_t dn_tmp_107=((size_t)(0ULL));
size_t dn_tmp_108=({ dn_t_buf_U8 const * dn_tmp_109=(&(v_previous));
(*(dn_tmp_109)).len; });
({ dn_range(dn_tmp_107,dn_tmp_108,(*(dn_tmp_106)).len);(dn_t_slice0_U8){(*(dn_tmp_106)).data?(*(dn_tmp_106)).data+dn_tmp_107:NULL,dn_tmp_108}; }); });
dn_f_write_file(dn_tmp_103,dn_tmp_105); }))))){
(void)(({ dn_r_Gui * dn_tmp_110=v_g;
dn_t_slice0_U8 dn_tmp_111=({ dn_utf8 dn_tmp_112=((dn_utf8){(const uint8_t*)"\320\235\320\265 \321\203\320\264\320\260\320\273\320\276\321\201\321\214 \321\201\320\276\320\267\320\264\320\260\321\202\321\214 \320\272\320\276\320\277\320\270\321\216 \320\277\321\200\320\265\320\266\320\275\320\265\320\263\320\276 \320\277\321\200\320\276\321\204\320\270\320\273\321\217.", 78});
((dn_t_slice0_U8){dn_tmp_112.data,dn_tmp_112.len}); });
dn_f_ui_log(dn_tmp_110,dn_tmp_111); }));
{ uint8_t dn_tmp_113=0;dn_drop_buf_U8(&v_previous);
dn_boundary_end(&dn_tmp_95,0);
dn_drop_buf_U8(&v_contents);
dn_drop_buf_U8(&v_path);
dn_drop_buf_U16(&v_filter);
dn_drop_buf_U16(&v_extension);
dn_drop_buf_U16(&v_file);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_113; }
}
else{
}
if((!(({ dn_t_slice0_U8 dn_tmp_114=({ dn_utf8 dn_tmp_115=((dn_utf8){(const uint8_t*)"active.profile", 14});
((dn_t_slice0_U8){dn_tmp_115.data,dn_tmp_115.len}); });
dn_t_slice0_U8 dn_tmp_116=({ dn_t_buf_U8 const * dn_tmp_117=(&(v_contents));
size_t dn_tmp_118=((size_t)(0ULL));
size_t dn_tmp_119=({ dn_t_buf_U8 const * dn_tmp_120=(&(v_contents));
(*(dn_tmp_120)).len; });
({ dn_range(dn_tmp_118,dn_tmp_119,(*(dn_tmp_117)).len);(dn_t_slice0_U8){(*(dn_tmp_117)).data?(*(dn_tmp_117)).data+dn_tmp_118:NULL,dn_tmp_119}; }); });
dn_f_write_file(dn_tmp_114,dn_tmp_116); })))){
(void)(({ dn_r_Gui * dn_tmp_121=v_g;
dn_t_slice0_U8 dn_tmp_122=({ dn_utf8 dn_tmp_123=((dn_utf8){(const uint8_t*)"\320\235\320\265 \321\203\320\264\320\260\320\273\320\276\321\201\321\214 \321\203\321\201\321\202\320\260\320\275\320\276\320\262\320\270\321\202\321\214 \320\277\321\200\320\276\321\204\320\270\320\273\321\214.", 56});
((dn_t_slice0_U8){dn_tmp_123.data,dn_tmp_123.len}); });
dn_f_ui_log(dn_tmp_121,dn_tmp_122); }));
{ uint8_t dn_tmp_124=0;dn_drop_buf_U8(&v_previous);
dn_boundary_end(&dn_tmp_95,0);
dn_drop_buf_U8(&v_contents);
dn_drop_buf_U8(&v_path);
dn_drop_buf_U16(&v_filter);
dn_drop_buf_U16(&v_extension);
dn_drop_buf_U16(&v_file);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_124; }
}
else{
}
(void)(({ void * dn_tmp_125=({ dn_r_Gui * dn_tmp_126=v_g;
size_t dn_tmp_127=((size_t)(20ULL));
dn_f_control(dn_tmp_126,dn_tmp_127); });
uint32_t dn_tmp_128=((uint32_t)(0x14eULL));
size_t dn_tmp_129=({ uint32_t dn_tmp_130=v_target;
if((unsigned __int128)dn_tmp_130>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_130; });
size_t dn_tmp_131=((size_t)(0ULL));
({ int64_t dn_foreign=dn_ext_SendMessageW(dn_tmp_125,dn_tmp_128,dn_tmp_129,dn_tmp_131);dn_fp_default();dn_foreign; }); }));
dn_r_TrainingView v_view=({ ((dn_r_TrainingView){0}); });
if(({ dn_t_slice0_U8 dn_tmp_132=({ dn_t_buf_U8 const * dn_tmp_133=(&(v_contents));
size_t dn_tmp_134=((size_t)(0ULL));
size_t dn_tmp_135=({ dn_t_buf_U8 const * dn_tmp_136=(&(v_contents));
(*(dn_tmp_136)).len; });
({ dn_range(dn_tmp_134,dn_tmp_135,(*(dn_tmp_133)).len);(dn_t_slice0_U8){(*(dn_tmp_133)).data?(*(dn_tmp_133)).data+dn_tmp_134:NULL,dn_tmp_135}; }); });
dn_r_TrainingView * dn_tmp_137=(&(v_view));
dn_f_training_view(dn_tmp_132,dn_tmp_137); })){
{ dn_r_TrainingView dn_tmp_139=v_view;dn_r_TrainingView *dn_tmp_138=&((v_g)->f_view);*dn_tmp_138=dn_tmp_139; }
{ bool dn_tmp_141=1;bool *dn_tmp_140=&((v_g)->f_history);*dn_tmp_140=dn_tmp_141; }
}
else{
}
(void)(({ dn_r_Gui * dn_tmp_142=v_g;
dn_t_slice0_U8 dn_tmp_143=({ dn_utf8 dn_tmp_144=((dn_utf8){(const uint8_t*)"\320\237\321\200\320\276\321\204\320\270\320\273\321\214 \320\267\320\260\320\263\321\200\321\203\320\266\320\265\320\275. \320\236\321\206\320\265\320\275\320\272\320\260 \320\276\321\202\320\275\320\276\321\201\320\270\321\202\321\201\321\217 \320\272 \321\201\320\276\321\205\321\200\320\260\320\275\321\221\320\275\320\275\321\213\320\274 \320\270\321\201\320\277\321\213\321\202\320\260\320\275\320\270\321\217\320\274; \320\277\321\200\320\276\321\204\320\270\320\273\321\214 \320\277\321\200\320\270\320\274\320\265\320\275\321\217\320\265\321\202\321\201\321\217 \320\277\321\200\320\270 \321\201\320\273\320\265\320\264\321\203\321\216\321\211\320\265\320\274 \320\262\320\272\320\273\321\216\321\207\320\265\320\275\320\270\320\270.", 196});
((dn_t_slice0_U8){dn_tmp_144.data,dn_tmp_144.len}); });
dn_f_ui_log(dn_tmp_142,dn_tmp_143); }));
dn_drop_buf_U8(&v_previous);
dn_boundary_end(&dn_tmp_95,0);
}
dn_drop_buf_U8(&v_contents);
dn_drop_buf_U8(&v_path);
dn_drop_buf_U16(&v_filter);
dn_drop_buf_U16(&v_extension);
dn_drop_buf_U16(&v_file);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_profile_dialog(dn_r_Gui * v_g,bool v_save) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_profile_dialog(v_g,v_save);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static int64_t dn_f_window_proc(void * v_window,uint32_t v_message,size_t v_wparam,void * v_lparam) {
{
if(({ uint32_t dn_tmp_0=v_message;
uint32_t dn_tmp_1=({ uint32_t dn_tmp_2=((uint32_t)(129ULL));
if((unsigned __int128)dn_tmp_2>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_2; });
(dn_tmp_0 == dn_tmp_1); })){
dn_r_CreateStruct * v_create=({ void * dn_tmp_3=v_lparam;
((dn_r_CreateStruct *)dn_tmp_3); });
(void)(({ void * dn_tmp_4=v_window;
int32_t dn_tmp_5=dn_I32_neg(((int32_t)(21ULL)));
void * dn_tmp_6=((*((dn_r_CreateStruct *)dn_nonnull((void*)(v_create))))).f_parameter;
({ void * dn_foreign=dn_ext_SetWindowLongPtrW(dn_tmp_4,dn_tmp_5,dn_tmp_6);dn_fp_default();dn_foreign; }); }));
{ int64_t dn_tmp_7=((int64_t)(1ULL));return dn_tmp_7; }
}
else{
}
void * v_userdata=({ void * dn_tmp_8=v_window;
int32_t dn_tmp_9=dn_I32_neg(((int32_t)(21ULL)));
({ void * dn_foreign=dn_ext_GetWindowLongPtrW(dn_tmp_8,dn_tmp_9);dn_fp_default();dn_foreign; }); });
if((!(({ void * dn_tmp_10=v_userdata;
dn_f_handle_valid(dn_tmp_10); })))){
{ int64_t dn_tmp_15=({ void * dn_tmp_11=v_window;
uint32_t dn_tmp_12=v_message;
size_t dn_tmp_13=v_wparam;
void * dn_tmp_14=v_lparam;
({ int64_t dn_foreign=dn_ext_DefWindowProcW(dn_tmp_11,dn_tmp_12,dn_tmp_13,dn_tmp_14);dn_fp_default();dn_foreign; }); });return dn_tmp_15; }
}
else{
}
dn_r_Gui * v_g=({ void * dn_tmp_16=v_userdata;
((dn_r_Gui *)dn_tmp_16); });
if(({ uint32_t dn_tmp_17=v_message;
uint32_t dn_tmp_18=({ uint32_t dn_tmp_19=((uint32_t)(15ULL));
if((unsigned __int128)dn_tmp_19>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_19; });
(dn_tmp_17 == dn_tmp_18); })){
(void)(({ dn_r_Gui const * dn_tmp_20=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
dn_f_gui_paint(dn_tmp_20); }));
{ int64_t dn_tmp_21=((int64_t)(0ULL));return dn_tmp_21; }
}
else{
}
if(({ uint32_t dn_tmp_22=v_message;
uint32_t dn_tmp_23=({ uint32_t dn_tmp_24=((uint32_t)(20ULL));
if((unsigned __int128)dn_tmp_24>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_24; });
(dn_tmp_22 == dn_tmp_23); })){
{ int64_t dn_tmp_25=((int64_t)(1ULL));return dn_tmp_25; }
}
else{
}
if(({ uint32_t dn_tmp_26=v_message;
uint32_t dn_tmp_27=({ uint32_t dn_tmp_28=((uint32_t)(5ULL));
if((unsigned __int128)dn_tmp_28>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_28; });
(dn_tmp_26 == dn_tmp_27); })){
(void)(({ dn_r_Gui const * dn_tmp_29=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
dn_f_gui_layout(dn_tmp_29); }));
{ int64_t dn_tmp_30=((int64_t)(0ULL));return dn_tmp_30; }
}
else{
}
if(({ uint32_t dn_tmp_31=v_message;
uint32_t dn_tmp_32=({ uint32_t dn_tmp_33=((uint32_t)(36ULL));
if((unsigned __int128)dn_tmp_33>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_33; });
(dn_tmp_31 == dn_tmp_32); })){
dn_r_MinMaxInfo * v_info=({ void * dn_tmp_34=v_lparam;
((dn_r_MinMaxInfo *)dn_tmp_34); });
{ int32_t dn_tmp_38=({ dn_r_Gui const * dn_tmp_35=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
int32_t dn_tmp_36=((int32_t)(760ULL));
dn_f_scale(dn_tmp_35,dn_tmp_36); });int32_t *dn_tmp_37=&(((*((dn_r_MinMaxInfo *)dn_nonnull((void*)(v_info))))).f_minimum_x);*dn_tmp_37=dn_tmp_38; }
{ int32_t dn_tmp_42=({ dn_r_Gui const * dn_tmp_39=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
int32_t dn_tmp_40=((int32_t)(720ULL));
dn_f_scale(dn_tmp_39,dn_tmp_40); });int32_t *dn_tmp_41=&(((*((dn_r_MinMaxInfo *)dn_nonnull((void*)(v_info))))).f_minimum_y);*dn_tmp_41=dn_tmp_42; }
{ int64_t dn_tmp_43=((int64_t)(0ULL));return dn_tmp_43; }
}
else{
}
if(({ uint32_t dn_tmp_44=v_message;
uint32_t dn_tmp_45=({ uint32_t dn_tmp_46=((uint32_t)(43ULL));
if((unsigned __int128)dn_tmp_46>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_46; });
(dn_tmp_44 == dn_tmp_45); })){
dn_r_DrawItem * v_item=({ void * dn_tmp_47=v_lparam;
((dn_r_DrawItem *)dn_tmp_47); });
(void)(({ dn_r_Gui const * dn_tmp_48=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
dn_r_DrawItem const * dn_tmp_49=(&((*((dn_r_DrawItem *)dn_nonnull((void*)(v_item))))));
dn_f_gui_draw_button(dn_tmp_48,dn_tmp_49); }));
{ int64_t dn_tmp_50=((int64_t)(1ULL));return dn_tmp_50; }
}
else{
}
if((({ uint32_t dn_tmp_51=v_message;
uint32_t dn_tmp_52=({ uint32_t dn_tmp_53=((uint32_t)(273ULL));
if((unsigned __int128)dn_tmp_53>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_53; });
(dn_tmp_51 == dn_tmp_52); }) && ({ size_t dn_tmp_54=({ size_t dn_tmp_55=({ size_t dn_tmp_56=v_wparam;
size_t dn_tmp_57=({ size_t dn_tmp_58=((size_t)(16ULL));
if((unsigned __int128)dn_tmp_58>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_58; });
dn_USize_shr(dn_tmp_56,dn_tmp_57); });
size_t dn_tmp_59=({ size_t dn_tmp_60=((size_t)(65535ULL));
if((unsigned __int128)dn_tmp_60>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_60; });
dn_USize_bits((uint64_t)dn_tmp_55 & (uint64_t)dn_tmp_59); });
size_t dn_tmp_61=({ size_t dn_tmp_62=((size_t)(0ULL));
if((unsigned __int128)dn_tmp_62>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_62; });
(dn_tmp_54 == dn_tmp_61); }))){
size_t v_id=({ size_t dn_tmp_63=v_wparam;
size_t dn_tmp_64=({ size_t dn_tmp_65=((size_t)(65535ULL));
if((unsigned __int128)dn_tmp_65>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_65; });
dn_USize_bits((uint64_t)dn_tmp_63 & (uint64_t)dn_tmp_64); });
if(({ size_t dn_tmp_66=v_id;
size_t dn_tmp_67=({ size_t dn_tmp_68=((size_t)(10ULL));
if((unsigned __int128)dn_tmp_68>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_68; });
(dn_tmp_66 == dn_tmp_67); })){
if((({ uint32_t dn_tmp_69=((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))).f_phase;
uint32_t dn_tmp_70=({ uint32_t dn_tmp_71=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_71>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_71; });
(dn_tmp_69 == dn_tmp_70); }) || ({ void * dn_tmp_72=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_73=(&(((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))).f_process));
(&((*dn_tmp_73)->value)); })))));
dn_f_handle_valid(dn_tmp_72); }))){
(void)(({ dn_r_Gui * dn_tmp_74=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
dn_f_gui_stop(dn_tmp_74); }));
}
else{
(void)(({ dn_r_Gui * dn_tmp_75=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
dn_f_gui_start(dn_tmp_75); }));
}
}
else{
}
if(({ size_t dn_tmp_76=v_id;
size_t dn_tmp_77=({ size_t dn_tmp_78=((size_t)(13ULL));
if((unsigned __int128)dn_tmp_78>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_78; });
(dn_tmp_76 == dn_tmp_77); })){
if(({ uint32_t dn_tmp_79=((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))).f_phase;
uint32_t dn_tmp_80=({ uint32_t dn_tmp_81=((uint32_t)(6ULL));
if((unsigned __int128)dn_tmp_81>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_81; });
(dn_tmp_79 == dn_tmp_80); })){
(void)(({ dn_r_Gui * dn_tmp_82=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
dn_utf8 dn_tmp_83=((dn_utf8){(const uint8_t*)"Check-Driver.cmd", 16});
dn_t_slice0_U8 dn_tmp_84=({ dn_utf8 dn_tmp_85=((dn_utf8){(const uint8_t*)"", 0});
((dn_t_slice0_U8){dn_tmp_85.data,dn_tmp_85.len}); });
dn_f_auxiliary(dn_tmp_82,dn_tmp_83,dn_tmp_84); }));
}
else{
if((({ uint32_t dn_tmp_86=({ dn_r_Gui const * dn_tmp_87=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
dn_f_selected_target(dn_tmp_87); });
uint32_t dn_tmp_88=({ uint32_t dn_tmp_89=((uint32_t)(5ULL));
if((unsigned __int128)dn_tmp_89>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_89; });
(dn_tmp_86 == dn_tmp_88); }) || ({ uint32_t dn_tmp_90=({ dn_r_Gui const * dn_tmp_91=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
dn_f_selected_target(dn_tmp_91); });
uint32_t dn_tmp_92=({ uint32_t dn_tmp_93=((uint32_t)(6ULL));
if((unsigned __int128)dn_tmp_93>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_93; });
(dn_tmp_90 == dn_tmp_92); }))){
(void)(({ dn_r_Gui * dn_tmp_94=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
dn_utf8 dn_tmp_95=((dn_utf8){(const uint8_t*)"Check-MWO.cmd", 13});
dn_t_slice0_U8 dn_tmp_96=({ dn_utf8 dn_tmp_97=((dn_utf8){(const uint8_t*)"", 0});
((dn_t_slice0_U8){dn_tmp_97.data,dn_tmp_97.len}); });
dn_f_auxiliary(dn_tmp_94,dn_tmp_95,dn_tmp_96); }));
}
else{
if((({ uint32_t dn_tmp_98=({ dn_r_Gui const * dn_tmp_99=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
dn_f_selected_target(dn_tmp_99); });
uint32_t dn_tmp_100=({ uint32_t dn_tmp_101=((uint32_t)(3ULL));
if((unsigned __int128)dn_tmp_101>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_101; });
(dn_tmp_98 == dn_tmp_100); }) || ({ uint32_t dn_tmp_102=({ dn_r_Gui const * dn_tmp_103=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
dn_f_selected_target(dn_tmp_103); });
uint32_t dn_tmp_104=({ uint32_t dn_tmp_105=((uint32_t)(4ULL));
if((unsigned __int128)dn_tmp_105>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_105; });
(dn_tmp_102 == dn_tmp_104); }))){
(void)(({ dn_r_Gui * dn_tmp_106=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
dn_utf8 dn_tmp_107=((dn_utf8){(const uint8_t*)"Check-Telegram.cmd", 18});
dn_t_slice0_U8 dn_tmp_108=({ dn_utf8 dn_tmp_109=((dn_utf8){(const uint8_t*)"", 0});
((dn_t_slice0_U8){dn_tmp_109.data,dn_tmp_109.len}); });
dn_f_auxiliary(dn_tmp_106,dn_tmp_107,dn_tmp_108); }));
}
else{
(void)(({ dn_r_Gui * dn_tmp_110=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
dn_utf8 dn_tmp_111=((dn_utf8){(const uint8_t*)"Check-Network.cmd", 17});
dn_t_slice0_U8 dn_tmp_112=({ dn_utf8 dn_tmp_113=((dn_utf8){(const uint8_t*)"", 0});
((dn_t_slice0_U8){dn_tmp_113.data,dn_tmp_113.len}); });
dn_f_auxiliary(dn_tmp_110,dn_tmp_111,dn_tmp_112); }));
}
}
}
}
else{
}
if(({ size_t dn_tmp_114=v_id;
size_t dn_tmp_115=({ size_t dn_tmp_116=((size_t)(14ULL));
if((unsigned __int128)dn_tmp_116>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_116; });
(dn_tmp_114 == dn_tmp_115); })){
(void)(({ dn_r_Gui * dn_tmp_117=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
dn_utf8 dn_tmp_118=((dn_utf8){(const uint8_t*)"Collect-Diagnostics.cmd", 23});
dn_t_slice0_U8 dn_tmp_119=({ dn_utf8 dn_tmp_120=((dn_utf8){(const uint8_t*)"", 0});
((dn_t_slice0_U8){dn_tmp_120.data,dn_tmp_120.len}); });
dn_f_auxiliary(dn_tmp_117,dn_tmp_118,dn_tmp_119); }));
}
else{
}
if(({ size_t dn_tmp_121=v_id;
size_t dn_tmp_122=({ size_t dn_tmp_123=((size_t)(16ULL));
if((unsigned __int128)dn_tmp_123>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_123; });
(dn_tmp_121 == dn_tmp_122); })){
if(({ void * dn_tmp_124=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_close_handle * const * dn_tmp_125=(&(((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))).f_training));
(&((*dn_tmp_125)->value)); })))));
dn_f_handle_valid(dn_tmp_124); })){
(void)(({ dn_r_Gui * dn_tmp_126=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
bool dn_tmp_127=0;
dn_f_stop_training(dn_tmp_126,dn_tmp_127); }));
}
else{
(void)(({ dn_r_Gui * dn_tmp_128=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
dn_f_start_training(dn_tmp_128); }));
}
}
else{
}
if(({ size_t dn_tmp_129=v_id;
size_t dn_tmp_130=({ size_t dn_tmp_131=((size_t)(17ULL));
if((unsigned __int128)dn_tmp_131>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_131; });
(dn_tmp_129 == dn_tmp_130); })){
(void)(({ dn_r_Gui * dn_tmp_132=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
bool dn_tmp_133=1;
dn_f_profile_dialog(dn_tmp_132,dn_tmp_133); }));
}
else{
}
if(({ size_t dn_tmp_134=v_id;
size_t dn_tmp_135=({ size_t dn_tmp_136=((size_t)(18ULL));
if((unsigned __int128)dn_tmp_136>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_136; });
(dn_tmp_134 == dn_tmp_135); })){
(void)(({ dn_r_Gui * dn_tmp_137=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
bool dn_tmp_138=0;
dn_f_profile_dialog(dn_tmp_137,dn_tmp_138); }));
}
else{
}
if(({ size_t dn_tmp_139=v_id;
size_t dn_tmp_140=({ size_t dn_tmp_141=((size_t)(15ULL));
if((unsigned __int128)dn_tmp_141>(((unsigned __int128)1<<64)-1))dn_fail("numeric conversion out of range");(size_t)dn_tmp_141; });
(dn_tmp_139 == dn_tmp_140); })){
dn_t_buf_U8 volatile v_path={0};
dn_t_buf_U8 volatile v_args={0};
dn_t_resource_h_close_handle * volatile v_owner={0};
dn_boundary dn_tmp_142={0};jmp_buf dn_tmp_143;dn_tmp_142.jump=&dn_tmp_143;dn_tmp_142.previous=dn_current;dn_tmp_142.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_142;if(setjmp(dn_tmp_143)){dn_current=dn_tmp_142.previous;dn_drop_resource_h_close_handle(&v_owner);
dn_drop_buf_U8(&v_args);
dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_142,1);dn_raise();}
v_path=({ dn_utf8 dn_tmp_144=((dn_utf8){(const uint8_t*)"NoDPI.log", 9});
dn_f_local_path(dn_tmp_144); });
v_args=({ size_t dn_tmp_145=((size_t)(0ULL));
dn_new_buf_U8(dn_tmp_145); });
(void)(({ dn_t_buf_U8 * dn_tmp_146=(&(v_args));
uint8_t dn_tmp_147=({ uint8_t dn_tmp_148=((uint8_t)(34ULL));
if((unsigned __int128)dn_tmp_148>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_148; });
dn_push_buf_U8(dn_tmp_146,dn_tmp_147); }));
(void)(({ dn_t_buf_U8 * dn_tmp_149=(&(v_args));
dn_t_slice0_U8 dn_tmp_150=({ dn_t_buf_U8 const * dn_tmp_151=(&(v_path));
size_t dn_tmp_152=((size_t)(0ULL));
size_t dn_tmp_153=({ dn_t_buf_U8 const * dn_tmp_154=(&(v_path));
(*(dn_tmp_154)).len; });
({ dn_range(dn_tmp_152,dn_tmp_153,(*(dn_tmp_151)).len);(dn_t_slice0_U8){(*(dn_tmp_151)).data?(*(dn_tmp_151)).data+dn_tmp_152:NULL,dn_tmp_153}; }); });
dn_f_bytes_append(dn_tmp_149,dn_tmp_150); }));
(void)(({ dn_t_buf_U8 * dn_tmp_155=(&(v_args));
uint8_t dn_tmp_156=({ uint8_t dn_tmp_157=((uint8_t)(34ULL));
if((unsigned __int128)dn_tmp_157>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_157; });
dn_push_buf_U8(dn_tmp_155,dn_tmp_156); }));
v_owner=({ dn_t_resource_h_close_handle * dn_tmp_158=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_158,({ dn_r_Gui const * dn_tmp_159=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
dn_t_slice0_U8 dn_tmp_160=({ dn_utf8 dn_tmp_161=((dn_utf8){(const uint8_t*)"notepad.exe", 11});
((dn_t_slice0_U8){dn_tmp_161.data,dn_tmp_161.len}); });
dn_t_slice0_U8 dn_tmp_162=({ dn_t_buf_U8 const * dn_tmp_163=(&(v_args));
size_t dn_tmp_164=((size_t)(0ULL));
size_t dn_tmp_165=({ dn_t_buf_U8 const * dn_tmp_166=(&(v_args));
(*(dn_tmp_166)).len; });
({ dn_range(dn_tmp_164,dn_tmp_165,(*(dn_tmp_163)).len);(dn_t_slice0_U8){(*(dn_tmp_163)).data?(*(dn_tmp_163)).data+dn_tmp_164:NULL,dn_tmp_165}; }); });
bool dn_tmp_167=0;
int32_t dn_tmp_168=((int32_t)(1ULL));
dn_f_launch(dn_tmp_159,dn_tmp_160,dn_tmp_162,dn_tmp_167,dn_tmp_168); })); });
dn_drop_resource_h_close_handle(&v_owner);
dn_drop_buf_U8(&v_args);
dn_drop_buf_U8(&v_path);
dn_boundary_end(&dn_tmp_142,0);
}
else{
}
{ int64_t dn_tmp_169=((int64_t)(0ULL));return dn_tmp_169; }
}
else{
}
if(({ uint32_t dn_tmp_170=v_message;
uint32_t dn_tmp_171=({ uint32_t dn_tmp_172=((uint32_t)(275ULL));
if((unsigned __int128)dn_tmp_172>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_172; });
(dn_tmp_170 == dn_tmp_171); })){
(void)(({ dn_r_Gui * dn_tmp_173=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
dn_f_gui_poll(dn_tmp_173); }));
{ int64_t dn_tmp_174=((int64_t)(0ULL));return dn_tmp_174; }
}
else{
}
if(({ uint32_t dn_tmp_175=v_message;
uint32_t dn_tmp_176=({ uint32_t dn_tmp_177=((uint32_t)(16ULL));
if((unsigned __int128)dn_tmp_177>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_177; });
(dn_tmp_175 == dn_tmp_176); })){
(void)(({ dn_r_Gui * dn_tmp_178=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
bool dn_tmp_179=1;
dn_f_stop_training(dn_tmp_178,dn_tmp_179); }));
(void)(({ dn_r_Gui * dn_tmp_180=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_g))))));
dn_f_gui_stop(dn_tmp_180); }));
(void)(({ void * dn_tmp_181=v_window;
({ int32_t dn_foreign=dn_ext_DestroyWindow(dn_tmp_181);dn_fp_default();dn_foreign; }); }));
{ int64_t dn_tmp_182=((int64_t)(0ULL));return dn_tmp_182; }
}
else{
}
if(({ uint32_t dn_tmp_183=v_message;
uint32_t dn_tmp_184=({ uint32_t dn_tmp_185=((uint32_t)(2ULL));
if((unsigned __int128)dn_tmp_185>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_185; });
(dn_tmp_183 == dn_tmp_184); })){
(void)(({ void * dn_tmp_186=v_window;
size_t dn_tmp_187=((size_t)(1ULL));
({ int32_t dn_foreign=dn_ext_KillTimer(dn_tmp_186,dn_tmp_187);dn_fp_default();dn_foreign; }); }));
(void)(({ int32_t dn_tmp_188=((int32_t)(0ULL));
({ dn_ext_PostQuitMessage(dn_tmp_188);dn_fp_default();0; }); }));
{ int64_t dn_tmp_189=((int64_t)(0ULL));return dn_tmp_189; }
}
else{
}
{ int64_t dn_tmp_194=({ void * dn_tmp_190=v_window;
uint32_t dn_tmp_191=v_message;
size_t dn_tmp_192=v_wparam;
void * dn_tmp_193=v_lparam;
({ int64_t dn_foreign=dn_ext_DefWindowProcW(dn_tmp_190,dn_tmp_191,dn_tmp_192,dn_tmp_193);dn_fp_default();dn_foreign; }); });return dn_tmp_194; }
}

dn_fail("missing native return");
}
static int64_t dn_cb_window_proc(void * v_window,uint32_t v_message,size_t v_wparam,void * v_lparam) {
 if(!dn_enter()){return (int64_t){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (int64_t){0};}
 int64_t dn_result=dn_f_window_proc(v_window,v_message,v_wparam,v_lparam);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_gui_main(void) {
{
dn_t_resource_h_delete_object * volatile v_background={0};
dn_t_resource_h_delete_object * volatile v_card={0};
dn_t_resource_h_delete_object * volatile v_accent={0};
dn_t_resource_h_delete_object * volatile v_secondary={0};
dn_t_buf_U16 volatile v_class_name={0};
dn_r_Gui * volatile v_state={0};
dn_t_buf_U16 volatile v_title={0};
dn_t_buf_U16 volatile v_face={0};
dn_t_resource_h_delete_object * volatile v_font={0};
dn_t_resource_h_delete_object * volatile v_title_font={0};
dn_t_resource_h_delete_object * volatile v_status_font={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_resource_h_delete_object(&v_status_font);
dn_drop_resource_h_delete_object(&v_title_font);
dn_drop_resource_h_delete_object(&v_font);
dn_drop_buf_U16(&v_face);
dn_drop_buf_U16(&v_title);
dn_drop_box_r3_Gui(&v_state);
dn_drop_buf_U16(&v_class_name);
dn_drop_resource_h_delete_object(&v_secondary);
dn_drop_resource_h_delete_object(&v_accent);
dn_drop_resource_h_delete_object(&v_card);
dn_drop_resource_h_delete_object(&v_background);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
(void)(({ ({ int32_t dn_foreign=dn_ext_SetProcessDPIAware();dn_fp_default();dn_foreign; }); }));
if((!(({ dn_f_set_application_directory(); })))){
(void)(({ dn_utf8 dn_tmp_2=((dn_utf8){(const uint8_t*)"Cannot select application directory", 35});
(fwrite(dn_tmp_2.data,1,dn_tmp_2.len,stdout),fputc('\n',stdout),0); }));
{ uint8_t dn_tmp_3=0;dn_drop_resource_h_delete_object(&v_status_font);
dn_drop_resource_h_delete_object(&v_title_font);
dn_drop_resource_h_delete_object(&v_font);
dn_drop_buf_U16(&v_face);
dn_drop_buf_U16(&v_title);
dn_drop_box_r3_Gui(&v_state);
dn_drop_buf_U16(&v_class_name);
dn_drop_resource_h_delete_object(&v_secondary);
dn_drop_resource_h_delete_object(&v_accent);
dn_drop_resource_h_delete_object(&v_card);
dn_drop_resource_h_delete_object(&v_background);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_3; }
}
else{
}
v_background=({ dn_t_resource_h_delete_object * dn_tmp_4=dn_begin_resource_h_delete_object();dn_commit_resource_h_delete_object(dn_tmp_4,({ uint32_t dn_tmp_5=({ uint32_t dn_tmp_6=((uint32_t)(12ULL));
uint32_t dn_tmp_7=((uint32_t)(17ULL));
uint32_t dn_tmp_8=((uint32_t)(27ULL));
dn_f_rgb(dn_tmp_6,dn_tmp_7,dn_tmp_8); });
({ void * dn_foreign=dn_ext_CreateSolidBrush(dn_tmp_5);dn_fp_default();dn_foreign; }); })); });
v_card=({ dn_t_resource_h_delete_object * dn_tmp_9=dn_begin_resource_h_delete_object();dn_commit_resource_h_delete_object(dn_tmp_9,({ uint32_t dn_tmp_10=({ uint32_t dn_tmp_11=((uint32_t)(21ULL));
uint32_t dn_tmp_12=((uint32_t)(30ULL));
uint32_t dn_tmp_13=((uint32_t)(44ULL));
dn_f_rgb(dn_tmp_11,dn_tmp_12,dn_tmp_13); });
({ void * dn_foreign=dn_ext_CreateSolidBrush(dn_tmp_10);dn_fp_default();dn_foreign; }); })); });
v_accent=({ dn_t_resource_h_delete_object * dn_tmp_14=dn_begin_resource_h_delete_object();dn_commit_resource_h_delete_object(dn_tmp_14,({ uint32_t dn_tmp_15=({ uint32_t dn_tmp_16=((uint32_t)(73ULL));
uint32_t dn_tmp_17=((uint32_t)(120ULL));
uint32_t dn_tmp_18=((uint32_t)(244ULL));
dn_f_rgb(dn_tmp_16,dn_tmp_17,dn_tmp_18); });
({ void * dn_foreign=dn_ext_CreateSolidBrush(dn_tmp_15);dn_fp_default();dn_foreign; }); })); });
v_secondary=({ dn_t_resource_h_delete_object * dn_tmp_19=dn_begin_resource_h_delete_object();dn_commit_resource_h_delete_object(dn_tmp_19,({ uint32_t dn_tmp_20=({ uint32_t dn_tmp_21=((uint32_t)(36ULL));
uint32_t dn_tmp_22=((uint32_t)(50ULL));
uint32_t dn_tmp_23=((uint32_t)(72ULL));
dn_f_rgb(dn_tmp_21,dn_tmp_22,dn_tmp_23); });
({ void * dn_foreign=dn_ext_CreateSolidBrush(dn_tmp_20);dn_fp_default();dn_foreign; }); })); });
void * v_instance=({ uint16_t * dn_tmp_24=({ ((uint16_t *)NULL); });
({ void * dn_foreign=dn_ext_GetModuleHandleW(dn_tmp_24);dn_fp_default();dn_foreign; }); });
v_class_name=({ dn_t_slice0_U8 dn_tmp_25=({ dn_utf8 dn_tmp_26=((dn_utf8){(const uint8_t*)"NoDPI.DLM.AutoWindow", 20});
((dn_t_slice0_U8){dn_tmp_26.data,dn_tmp_26.len}); });
dn_f_wide(dn_tmp_25); });
dn_r_WinClass v_class_=({ uint32_t dn_tmp_27=({ size_t dn_tmp_28=({ ((size_t)sizeof(dn_r_WinClass)); });
if((unsigned __int128)dn_tmp_28>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_28; });
uint32_t dn_tmp_29=((uint32_t)(3ULL));
dn_t_fn_h__U32__USize__h_ret_I64 dn_tmp_30=dn_cb_window_proc;
int32_t dn_tmp_31=((int32_t)(0ULL));
int32_t dn_tmp_32=((int32_t)(0ULL));
void * dn_tmp_33=v_instance;
void * dn_tmp_34=({ ((void *){0}); });
void * dn_tmp_35=({ void * dn_tmp_36=({ ((void *){0}); });
size_t dn_tmp_37=((size_t)(32512ULL));
({ void * dn_foreign=dn_ext_LoadCursorW(dn_tmp_36,dn_tmp_37);dn_fp_default();dn_foreign; }); });
void * dn_tmp_38=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_delete_object * const * dn_tmp_39=(&(v_background));
(&((*dn_tmp_39)->value)); })))));
uint16_t * dn_tmp_40=({ ((uint16_t *)NULL); });
uint16_t * dn_tmp_41=({ dn_t_buf_U16 const * dn_tmp_42=(&(v_class_name));
((uint16_t *)(*(dn_tmp_42)).data); });
void * dn_tmp_43=({ ((void *){0}); });
((dn_r_WinClass){dn_tmp_27,dn_tmp_29,dn_tmp_30,dn_tmp_31,dn_tmp_32,dn_tmp_33,dn_tmp_34,dn_tmp_35,dn_tmp_38,dn_tmp_40,dn_tmp_41,dn_tmp_43}); });
if(({ uint16_t dn_tmp_44=({ dn_r_WinClass * dn_tmp_45=({ dn_r_WinClass * dn_tmp_46=({ dn_r_WinClass * dn_tmp_47=(&(v_class_));
((dn_r_WinClass *)dn_tmp_47); });
((dn_r_WinClass *)dn_tmp_46); });
({ uint16_t dn_foreign=dn_ext_RegisterClassExW(dn_tmp_45);dn_fp_default();dn_foreign; }); });
uint16_t dn_tmp_48=({ uint16_t dn_tmp_49=((uint16_t)(0ULL));
if((unsigned __int128)dn_tmp_49>(((unsigned __int128)1<<16)-1))dn_fail("numeric conversion out of range");(uint16_t)dn_tmp_49; });
(dn_tmp_44 == dn_tmp_48); })){
{ uint8_t dn_tmp_50=0;dn_drop_resource_h_delete_object(&v_status_font);
dn_drop_resource_h_delete_object(&v_title_font);
dn_drop_resource_h_delete_object(&v_font);
dn_drop_buf_U16(&v_face);
dn_drop_buf_U16(&v_title);
dn_drop_box_r3_Gui(&v_state);
dn_drop_buf_U16(&v_class_name);
dn_drop_resource_h_delete_object(&v_secondary);
dn_drop_resource_h_delete_object(&v_accent);
dn_drop_resource_h_delete_object(&v_card);
dn_drop_resource_h_delete_object(&v_background);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_50; }
}
else{
}
v_state=({ dn_r_Gui dn_tmp_51=({ dn_t_buf_r7_Control dn_tmp_52=({ size_t dn_tmp_53=((size_t)(0ULL));
dn_new_buf_r7_Control(dn_tmp_53); });
dn_adopt_buf_r7_Control(&dn_tmp_52);
void * dn_tmp_54=({ ((void *){0}); });
void * dn_tmp_55=({ int32_t dn_tmp_56=((int32_t)(17ULL));
({ void * dn_foreign=dn_ext_GetStockObject(dn_tmp_56);dn_fp_default();dn_foreign; }); });
void * dn_tmp_57=({ int32_t dn_tmp_58=((int32_t)(17ULL));
({ void * dn_foreign=dn_ext_GetStockObject(dn_tmp_58);dn_fp_default();dn_foreign; }); });
void * dn_tmp_59=({ int32_t dn_tmp_60=((int32_t)(17ULL));
({ void * dn_foreign=dn_ext_GetStockObject(dn_tmp_60);dn_fp_default();dn_foreign; }); });
void * dn_tmp_61=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_delete_object * const * dn_tmp_62=(&(v_background));
(&((*dn_tmp_62)->value)); })))));
void * dn_tmp_63=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_delete_object * const * dn_tmp_64=(&(v_card));
(&((*dn_tmp_64)->value)); })))));
void * dn_tmp_65=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_delete_object * const * dn_tmp_66=(&(v_accent));
(&((*dn_tmp_66)->value)); })))));
void * dn_tmp_67=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_delete_object * const * dn_tmp_68=(&(v_secondary));
(&((*dn_tmp_68)->value)); })))));
dn_t_resource_h_close_handle * dn_tmp_69=({ dn_t_resource_h_close_handle * dn_tmp_70=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_70,({ ((void *){0}); })); });
dn_adopt_resource_h_close_handle(&dn_tmp_69);
dn_t_resource_h_close_handle * dn_tmp_71=({ dn_t_resource_h_close_handle * dn_tmp_72=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_72,({ ((void *){0}); })); });
dn_adopt_resource_h_close_handle(&dn_tmp_71);
dn_t_buf_U8 dn_tmp_73=({ dn_t_slice0_U8 dn_tmp_74=({ dn_utf8 dn_tmp_75=((dn_utf8){(const uint8_t*)"\320\240\321\203\321\207\320\275\321\213\321\205 \320\275\320\260\321\201\321\202\321\200\320\276\320\265\320\272 \320\275\320\265\321\202. \320\235\320\260\320\266\320\274\320\270\321\202\320\265 \302\253\320\222\320\272\320\273\321\216\321\207\320\270\321\202\321\214\302\273.", 74});
((dn_t_slice0_U8){dn_tmp_75.data,dn_tmp_75.len}); });
dn_f_bytes_copy(dn_tmp_74); });
dn_adopt_buf_U8(&dn_tmp_73);
uint32_t dn_tmp_76=((uint32_t)(0ULL));
uint64_t dn_tmp_77=((uint64_t)(0ULL));
int32_t dn_tmp_78=((int32_t)(96ULL));
dn_t_resource_h_close_handle * dn_tmp_79=({ dn_t_resource_h_close_handle * dn_tmp_80=dn_begin_resource_h_close_handle();dn_commit_resource_h_close_handle(dn_tmp_80,({ ((void *){0}); })); });
dn_adopt_resource_h_close_handle(&dn_tmp_79);
dn_r_TrainingView dn_tmp_81=({ ((dn_r_TrainingView){0}); });
bool dn_tmp_82=0;
dn_t_buf_U8 dn_tmp_83=({ size_t dn_tmp_84=((size_t)(0ULL));
dn_new_buf_U8(dn_tmp_84); });
dn_adopt_buf_U8(&dn_tmp_83);
((dn_r_Gui){dn_tmp_52,dn_tmp_54,dn_tmp_55,dn_tmp_57,dn_tmp_59,dn_tmp_61,dn_tmp_63,dn_tmp_65,dn_tmp_67,dn_tmp_69,dn_tmp_71,dn_tmp_73,dn_tmp_76,dn_tmp_77,dn_tmp_78,dn_tmp_79,dn_tmp_81,dn_tmp_82,dn_tmp_83}); });
dn_adopt_r3_Gui(&dn_tmp_51);
dn_new_box_r3_Gui(dn_tmp_51); });
v_title=({ dn_t_slice0_U8 dn_tmp_85=({ dn_utf8 dn_tmp_86=((dn_utf8){(const uint8_t*)"NoDPI \342\200\224 \320\220\320\262\321\202\320\276\320\274\320\260\321\202\320\270\321\207\320\265\321\201\320\272\320\270\320\271 \321\200\320\265\320\266\320\270\320\274", 49});
((dn_t_slice0_U8){dn_tmp_86.data,dn_tmp_86.len}); });
dn_f_wide(dn_tmp_85); });
void * v_window=({ uint32_t dn_tmp_87=((uint32_t)(0ULL));
uint16_t * dn_tmp_88=({ dn_t_buf_U16 const * dn_tmp_89=(&(v_class_name));
((uint16_t *)(*(dn_tmp_89)).data); });
uint16_t * dn_tmp_90=({ dn_t_buf_U16 const * dn_tmp_91=(&(v_title));
((uint16_t *)(*(dn_tmp_91)).data); });
uint32_t dn_tmp_92=((uint32_t)(0x02cf0000ULL));
int32_t dn_tmp_93=((int32_t)(80ULL));
int32_t dn_tmp_94=((int32_t)(40ULL));
int32_t dn_tmp_95=((int32_t)(900ULL));
int32_t dn_tmp_96=((int32_t)(760ULL));
void * dn_tmp_97=({ ((void *){0}); });
size_t dn_tmp_98=((size_t)(0ULL));
void * dn_tmp_99=v_instance;
void * dn_tmp_100=({ dn_r_Gui * dn_tmp_101=({ dn_r_Gui * dn_tmp_102=({ dn_r_Gui * dn_tmp_103=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_state))))));
((dn_r_Gui *)dn_tmp_103); });
((dn_r_Gui *)dn_tmp_102); });
((void*)dn_tmp_101); });
({ void * dn_foreign=dn_ext_CreateWindowExW(dn_tmp_87,dn_tmp_88,dn_tmp_90,dn_tmp_92,dn_tmp_93,dn_tmp_94,dn_tmp_95,dn_tmp_96,dn_tmp_97,dn_tmp_98,dn_tmp_99,dn_tmp_100);dn_fp_default();dn_foreign; }); });
if((!(({ void * dn_tmp_104=v_window;
dn_f_handle_valid(dn_tmp_104); })))){
{ uint8_t dn_tmp_105=0;dn_drop_resource_h_delete_object(&v_status_font);
dn_drop_resource_h_delete_object(&v_title_font);
dn_drop_resource_h_delete_object(&v_font);
dn_drop_buf_U16(&v_face);
dn_drop_buf_U16(&v_title);
dn_drop_box_r3_Gui(&v_state);
dn_drop_buf_U16(&v_class_name);
dn_drop_resource_h_delete_object(&v_secondary);
dn_drop_resource_h_delete_object(&v_accent);
dn_drop_resource_h_delete_object(&v_card);
dn_drop_resource_h_delete_object(&v_background);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_105; }
}
else{
}
{ void * dn_tmp_107=v_window;void * *dn_tmp_106=&((v_state)->f_window);*dn_tmp_106=dn_tmp_107; }
{ int32_t dn_tmp_111=({ uint32_t dn_tmp_108=({ void * dn_tmp_109=v_window;
({ uint32_t dn_foreign=dn_ext_GetDpiForWindow(dn_tmp_109);dn_fp_default();dn_foreign; }); });
if((unsigned __int128)dn_tmp_108>(((unsigned __int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_108; });int32_t *dn_tmp_110=&((v_state)->f_dpi);*dn_tmp_110=dn_tmp_111; }
if(({ int32_t dn_tmp_112=(v_state)->f_dpi;
int32_t dn_tmp_113=({ int32_t dn_tmp_114=((int32_t)(0ULL));
if((__int128)dn_tmp_114<(-((__int128)1<<31))||(__int128)dn_tmp_114>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_114; });
(dn_tmp_112 <= dn_tmp_113); })){
{ int32_t dn_tmp_116=((int32_t)(96ULL));int32_t *dn_tmp_115=&((v_state)->f_dpi);*dn_tmp_115=dn_tmp_116; }
}
else{
}
int32_t v_window_width=({ dn_r_Gui const * dn_tmp_117=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_state))))));
int32_t dn_tmp_118=((int32_t)(900ULL));
dn_f_scale(dn_tmp_117,dn_tmp_118); });
int32_t v_window_height=({ dn_r_Gui const * dn_tmp_119=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_state))))));
int32_t dn_tmp_120=((int32_t)(760ULL));
dn_f_scale(dn_tmp_119,dn_tmp_120); });
int32_t v_screen_width=({ int32_t dn_tmp_121=((int32_t)(0ULL));
({ int32_t dn_foreign=dn_ext_GetSystemMetrics(dn_tmp_121);dn_fp_default();dn_foreign; }); });
int32_t v_screen_height=({ int32_t dn_tmp_122=((int32_t)(1ULL));
({ int32_t dn_foreign=dn_ext_GetSystemMetrics(dn_tmp_122);dn_fp_default();dn_foreign; }); });
if(({ int32_t dn_tmp_123=v_window_width;
int32_t dn_tmp_124=({ int32_t dn_tmp_125=v_screen_width;
int32_t dn_tmp_126=({ int32_t dn_tmp_127=((int32_t)(40ULL));
if((__int128)dn_tmp_127<(-((__int128)1<<31))||(__int128)dn_tmp_127>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_127; });
dn_I32_sub(dn_tmp_125,dn_tmp_126); });
(dn_tmp_123 > dn_tmp_124); })){
{ int32_t dn_tmp_132=({ int32_t dn_tmp_128=v_screen_width;
int32_t dn_tmp_129=({ int32_t dn_tmp_130=((int32_t)(40ULL));
if((__int128)dn_tmp_130<(-((__int128)1<<31))||(__int128)dn_tmp_130>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_130; });
dn_I32_sub(dn_tmp_128,dn_tmp_129); });int32_t *dn_tmp_131=&(v_window_width);*dn_tmp_131=dn_tmp_132; }
}
else{
}
if(({ int32_t dn_tmp_133=v_window_height;
int32_t dn_tmp_134=({ int32_t dn_tmp_135=v_screen_height;
int32_t dn_tmp_136=({ int32_t dn_tmp_137=((int32_t)(80ULL));
if((__int128)dn_tmp_137<(-((__int128)1<<31))||(__int128)dn_tmp_137>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_137; });
dn_I32_sub(dn_tmp_135,dn_tmp_136); });
(dn_tmp_133 > dn_tmp_134); })){
{ int32_t dn_tmp_142=({ int32_t dn_tmp_138=v_screen_height;
int32_t dn_tmp_139=({ int32_t dn_tmp_140=((int32_t)(80ULL));
if((__int128)dn_tmp_140<(-((__int128)1<<31))||(__int128)dn_tmp_140>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_140; });
dn_I32_sub(dn_tmp_138,dn_tmp_139); });int32_t *dn_tmp_141=&(v_window_height);*dn_tmp_141=dn_tmp_142; }
}
else{
}
int32_t v_fit_dpi=({ int32_t dn_tmp_143=({ int32_t dn_tmp_144=v_window_height;
int32_t dn_tmp_145=({ int32_t dn_tmp_146=((int32_t)(40ULL));
if((__int128)dn_tmp_146<(-((__int128)1<<31))||(__int128)dn_tmp_146>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_146; });
dn_I32_sub(dn_tmp_144,dn_tmp_145); });
int32_t dn_tmp_147=((int32_t)(96ULL));
int32_t dn_tmp_148=((int32_t)(670ULL));
({ int32_t dn_foreign=dn_ext_MulDiv(dn_tmp_143,dn_tmp_147,dn_tmp_148);dn_fp_default();dn_foreign; }); });
if((({ int32_t dn_tmp_149=(v_state)->f_dpi;
int32_t dn_tmp_150=v_fit_dpi;
(dn_tmp_149 > dn_tmp_150); }) && ({ int32_t dn_tmp_151=v_fit_dpi;
int32_t dn_tmp_152=({ int32_t dn_tmp_153=((int32_t)(96ULL));
if((__int128)dn_tmp_153<(-((__int128)1<<31))||(__int128)dn_tmp_153>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_153; });
(dn_tmp_151 >= dn_tmp_152); }))){
{ int32_t dn_tmp_155=v_fit_dpi;int32_t *dn_tmp_154=&((v_state)->f_dpi);*dn_tmp_154=dn_tmp_155; }
}
else{
}
(void)(({ void * dn_tmp_156=v_window;
int32_t dn_tmp_157=((int32_t)(20ULL));
int32_t dn_tmp_158=((int32_t)(20ULL));
int32_t dn_tmp_159=v_window_width;
int32_t dn_tmp_160=v_window_height;
int32_t dn_tmp_161=((int32_t)(0ULL));
({ int32_t dn_foreign=dn_ext_MoveWindow(dn_tmp_156,dn_tmp_157,dn_tmp_158,dn_tmp_159,dn_tmp_160,dn_tmp_161);dn_fp_default();dn_foreign; }); }));
v_face=({ dn_t_slice0_U8 dn_tmp_162=({ dn_utf8 dn_tmp_163=((dn_utf8){(const uint8_t*)"Segoe UI", 8});
((dn_t_slice0_U8){dn_tmp_163.data,dn_tmp_163.len}); });
dn_f_wide(dn_tmp_162); });
v_font=({ dn_t_resource_h_delete_object * dn_tmp_164=dn_begin_resource_h_delete_object();dn_commit_resource_h_delete_object(dn_tmp_164,({ int32_t dn_tmp_165=dn_I32_neg(({ dn_r_Gui const * dn_tmp_166=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_state))))));
int32_t dn_tmp_167=((int32_t)(15ULL));
dn_f_scale(dn_tmp_166,dn_tmp_167); }));
int32_t dn_tmp_168=((int32_t)(0ULL));
int32_t dn_tmp_169=((int32_t)(0ULL));
int32_t dn_tmp_170=((int32_t)(0ULL));
int32_t dn_tmp_171=((int32_t)(400ULL));
uint32_t dn_tmp_172=((uint32_t)(0ULL));
uint32_t dn_tmp_173=((uint32_t)(0ULL));
uint32_t dn_tmp_174=((uint32_t)(0ULL));
uint32_t dn_tmp_175=((uint32_t)(1ULL));
uint32_t dn_tmp_176=((uint32_t)(0ULL));
uint32_t dn_tmp_177=((uint32_t)(0ULL));
uint32_t dn_tmp_178=((uint32_t)(5ULL));
uint32_t dn_tmp_179=((uint32_t)(0ULL));
uint16_t * dn_tmp_180=({ dn_t_buf_U16 const * dn_tmp_181=(&(v_face));
((uint16_t *)(*(dn_tmp_181)).data); });
({ void * dn_foreign=dn_ext_CreateFontW(dn_tmp_165,dn_tmp_168,dn_tmp_169,dn_tmp_170,dn_tmp_171,dn_tmp_172,dn_tmp_173,dn_tmp_174,dn_tmp_175,dn_tmp_176,dn_tmp_177,dn_tmp_178,dn_tmp_179,dn_tmp_180);dn_fp_default();dn_foreign; }); })); });
v_title_font=({ dn_t_resource_h_delete_object * dn_tmp_182=dn_begin_resource_h_delete_object();dn_commit_resource_h_delete_object(dn_tmp_182,({ int32_t dn_tmp_183=dn_I32_neg(({ dn_r_Gui const * dn_tmp_184=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_state))))));
int32_t dn_tmp_185=((int32_t)(32ULL));
dn_f_scale(dn_tmp_184,dn_tmp_185); }));
int32_t dn_tmp_186=((int32_t)(0ULL));
int32_t dn_tmp_187=((int32_t)(0ULL));
int32_t dn_tmp_188=((int32_t)(0ULL));
int32_t dn_tmp_189=((int32_t)(700ULL));
uint32_t dn_tmp_190=((uint32_t)(0ULL));
uint32_t dn_tmp_191=((uint32_t)(0ULL));
uint32_t dn_tmp_192=((uint32_t)(0ULL));
uint32_t dn_tmp_193=((uint32_t)(1ULL));
uint32_t dn_tmp_194=((uint32_t)(0ULL));
uint32_t dn_tmp_195=((uint32_t)(0ULL));
uint32_t dn_tmp_196=((uint32_t)(5ULL));
uint32_t dn_tmp_197=((uint32_t)(0ULL));
uint16_t * dn_tmp_198=({ dn_t_buf_U16 const * dn_tmp_199=(&(v_face));
((uint16_t *)(*(dn_tmp_199)).data); });
({ void * dn_foreign=dn_ext_CreateFontW(dn_tmp_183,dn_tmp_186,dn_tmp_187,dn_tmp_188,dn_tmp_189,dn_tmp_190,dn_tmp_191,dn_tmp_192,dn_tmp_193,dn_tmp_194,dn_tmp_195,dn_tmp_196,dn_tmp_197,dn_tmp_198);dn_fp_default();dn_foreign; }); })); });
v_status_font=({ dn_t_resource_h_delete_object * dn_tmp_200=dn_begin_resource_h_delete_object();dn_commit_resource_h_delete_object(dn_tmp_200,({ int32_t dn_tmp_201=dn_I32_neg(({ dn_r_Gui const * dn_tmp_202=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_state))))));
int32_t dn_tmp_203=((int32_t)(28ULL));
dn_f_scale(dn_tmp_202,dn_tmp_203); }));
int32_t dn_tmp_204=((int32_t)(0ULL));
int32_t dn_tmp_205=((int32_t)(0ULL));
int32_t dn_tmp_206=((int32_t)(0ULL));
int32_t dn_tmp_207=((int32_t)(600ULL));
uint32_t dn_tmp_208=((uint32_t)(0ULL));
uint32_t dn_tmp_209=((uint32_t)(0ULL));
uint32_t dn_tmp_210=((uint32_t)(0ULL));
uint32_t dn_tmp_211=((uint32_t)(1ULL));
uint32_t dn_tmp_212=((uint32_t)(0ULL));
uint32_t dn_tmp_213=((uint32_t)(0ULL));
uint32_t dn_tmp_214=((uint32_t)(5ULL));
uint32_t dn_tmp_215=((uint32_t)(0ULL));
uint16_t * dn_tmp_216=({ dn_t_buf_U16 const * dn_tmp_217=(&(v_face));
((uint16_t *)(*(dn_tmp_217)).data); });
({ void * dn_foreign=dn_ext_CreateFontW(dn_tmp_201,dn_tmp_204,dn_tmp_205,dn_tmp_206,dn_tmp_207,dn_tmp_208,dn_tmp_209,dn_tmp_210,dn_tmp_211,dn_tmp_212,dn_tmp_213,dn_tmp_214,dn_tmp_215,dn_tmp_216);dn_fp_default();dn_foreign; }); })); });
if(({ void * dn_tmp_218=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_delete_object * const * dn_tmp_219=(&(v_font));
(&((*dn_tmp_219)->value)); })))));
dn_f_handle_valid(dn_tmp_218); })){
{ void * dn_tmp_222=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_delete_object * const * dn_tmp_220=(&(v_font));
(&((*dn_tmp_220)->value)); })))));void * *dn_tmp_221=&((v_state)->f_font);*dn_tmp_221=dn_tmp_222; }
}
else{
}
if(({ void * dn_tmp_223=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_delete_object * const * dn_tmp_224=(&(v_title_font));
(&((*dn_tmp_224)->value)); })))));
dn_f_handle_valid(dn_tmp_223); })){
{ void * dn_tmp_227=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_delete_object * const * dn_tmp_225=(&(v_title_font));
(&((*dn_tmp_225)->value)); })))));void * *dn_tmp_226=&((v_state)->f_title_font);*dn_tmp_226=dn_tmp_227; }
}
else{
}
if(({ void * dn_tmp_228=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_delete_object * const * dn_tmp_229=(&(v_status_font));
(&((*dn_tmp_229)->value)); })))));
dn_f_handle_valid(dn_tmp_228); })){
{ void * dn_tmp_232=(*((void * const *)dn_nonnull((void*)(({ dn_t_resource_h_delete_object * const * dn_tmp_230=(&(v_status_font));
(&((*dn_tmp_230)->value)); })))));void * *dn_tmp_231=&((v_state)->f_status_font);*dn_tmp_231=dn_tmp_232; }
}
else{
}
uint32_t v_dark=((uint32_t)(1ULL));
(void)(({ void * dn_tmp_233=v_window;
uint32_t dn_tmp_234=((uint32_t)(20ULL));
uint32_t * dn_tmp_235=({ uint32_t * dn_tmp_236=({ uint32_t * dn_tmp_237=(&(v_dark));
((uint32_t *)dn_tmp_237); });
((uint32_t *)dn_tmp_236); });
uint32_t dn_tmp_238=((uint32_t)(4ULL));
({ int32_t dn_foreign=dn_ext_DwmSetWindowAttribute(dn_tmp_233,dn_tmp_234,dn_tmp_235,dn_tmp_238);dn_fp_default();dn_foreign; }); }));
uint32_t v_corners=((uint32_t)(2ULL));
(void)(({ void * dn_tmp_239=v_window;
uint32_t dn_tmp_240=((uint32_t)(33ULL));
uint32_t * dn_tmp_241=({ uint32_t * dn_tmp_242=({ uint32_t * dn_tmp_243=(&(v_corners));
((uint32_t *)dn_tmp_243); });
((uint32_t *)dn_tmp_242); });
uint32_t dn_tmp_244=((uint32_t)(4ULL));
({ int32_t dn_foreign=dn_ext_DwmSetWindowAttribute(dn_tmp_239,dn_tmp_240,dn_tmp_241,dn_tmp_244);dn_fp_default();dn_foreign; }); }));
(void)(({ bool dn_tmp_245=({ size_t dn_tmp_246=({ ((size_t)sizeof(dn_r_WinClass)); });
size_t dn_tmp_247=((size_t)(80ULL));
(dn_tmp_246 == dn_tmp_247); });
(dn_assert(dn_tmp_245),0); }));
(void)(({ bool dn_tmp_248=({ size_t dn_tmp_249=({ ((size_t)sizeof(dn_r_WinMessage)); });
size_t dn_tmp_250=((size_t)(48ULL));
(dn_tmp_249 == dn_tmp_250); });
(dn_assert(dn_tmp_248),0); }));
(void)(({ bool dn_tmp_251=({ size_t dn_tmp_252=({ ((size_t)sizeof(dn_r_ShellExecute)); });
size_t dn_tmp_253=((size_t)(112ULL));
(dn_tmp_252 == dn_tmp_253); });
(dn_assert(dn_tmp_251),0); }));
(void)(({ bool dn_tmp_254=({ size_t dn_tmp_255=({ ((size_t)sizeof(dn_r_OpenFileName)); });
size_t dn_tmp_256=((size_t)(152ULL));
(dn_tmp_255 == dn_tmp_256); });
(dn_assert(dn_tmp_254),0); }));
(void)(({ bool dn_tmp_257=({ size_t dn_tmp_258=({ ((size_t)sizeof(dn_r_PaintStruct)); });
size_t dn_tmp_259=((size_t)(72ULL));
(dn_tmp_258 == dn_tmp_259); });
(dn_assert(dn_tmp_257),0); }));
(void)(({ bool dn_tmp_260=({ size_t dn_tmp_261=({ ((size_t)sizeof(dn_r_DrawItem)); });
size_t dn_tmp_262=((size_t)(64ULL));
(dn_tmp_261 == dn_tmp_262); });
(dn_assert(dn_tmp_260),0); }));
(void)(({ bool dn_tmp_263=({ size_t dn_tmp_264=({ ((size_t)sizeof(dn_r_MinMaxInfo)); });
size_t dn_tmp_265=((size_t)(40ULL));
(dn_tmp_264 == dn_tmp_265); });
(dn_assert(dn_tmp_263),0); }));
(void)(({ dn_r_Gui * dn_tmp_266=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_state))))));
dn_f_gui_build(dn_tmp_266); }));
(void)(({ void * dn_tmp_267=v_window;
size_t dn_tmp_268=((size_t)(1ULL));
uint32_t dn_tmp_269=((uint32_t)(500ULL));
void * dn_tmp_270=({ ((void *){0}); });
({ size_t dn_foreign=dn_ext_SetTimer(dn_tmp_267,dn_tmp_268,dn_tmp_269,dn_tmp_270);dn_fp_default();dn_foreign; }); }));
(void)(({ void * dn_tmp_271=v_window;
int32_t dn_tmp_272=((int32_t)(5ULL));
({ int32_t dn_foreign=dn_ext_ShowWindow(dn_tmp_271,dn_tmp_272);dn_fp_default();dn_foreign; }); }));
(void)(({ void * dn_tmp_273=v_window;
({ int32_t dn_foreign=dn_ext_UpdateWindow(dn_tmp_273);dn_fp_default();dn_foreign; }); }));
dn_r_WinMessage v_message=({ ((dn_r_WinMessage){0}); });
while(1){
int32_t v_result=({ dn_r_WinMessage * dn_tmp_274=({ dn_r_WinMessage * dn_tmp_275=({ dn_r_WinMessage * dn_tmp_276=(&(v_message));
((dn_r_WinMessage *)dn_tmp_276); });
((dn_r_WinMessage *)dn_tmp_275); });
void * dn_tmp_277=({ ((void *){0}); });
uint32_t dn_tmp_278=((uint32_t)(0ULL));
uint32_t dn_tmp_279=((uint32_t)(0ULL));
({ int32_t dn_foreign=dn_ext_GetMessageW(dn_tmp_274,dn_tmp_277,dn_tmp_278,dn_tmp_279);dn_fp_default();dn_foreign; }); });
if(({ int32_t dn_tmp_280=v_result;
int32_t dn_tmp_281=({ int32_t dn_tmp_282=((int32_t)(0ULL));
if((__int128)dn_tmp_282<(-((__int128)1<<31))||(__int128)dn_tmp_282>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_282; });
(dn_tmp_280 <= dn_tmp_281); })){
break;
}
else{
}
if(({ int32_t dn_tmp_283=({ void * dn_tmp_284=v_window;
dn_r_WinMessage * dn_tmp_285=({ dn_r_WinMessage * dn_tmp_286=({ dn_r_WinMessage * dn_tmp_287=(&(v_message));
((dn_r_WinMessage *)dn_tmp_287); });
((dn_r_WinMessage *)dn_tmp_286); });
({ int32_t dn_foreign=dn_ext_IsDialogMessageW(dn_tmp_284,dn_tmp_285);dn_fp_default();dn_foreign; }); });
int32_t dn_tmp_288=({ int32_t dn_tmp_289=((int32_t)(0ULL));
if((__int128)dn_tmp_289<(-((__int128)1<<31))||(__int128)dn_tmp_289>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_289; });
(dn_tmp_283 == dn_tmp_288); })){
(void)(({ dn_r_WinMessage * dn_tmp_290=({ dn_r_WinMessage * dn_tmp_291=({ dn_r_WinMessage * dn_tmp_292=(&(v_message));
((dn_r_WinMessage *)dn_tmp_292); });
((dn_r_WinMessage *)dn_tmp_291); });
({ int32_t dn_foreign=dn_ext_TranslateMessage(dn_tmp_290);dn_fp_default();dn_foreign; }); }));
(void)(({ dn_r_WinMessage * dn_tmp_293=({ dn_r_WinMessage * dn_tmp_294=({ dn_r_WinMessage * dn_tmp_295=(&(v_message));
((dn_r_WinMessage *)dn_tmp_295); });
((dn_r_WinMessage *)dn_tmp_294); });
({ int64_t dn_foreign=dn_ext_DispatchMessageW(dn_tmp_293);dn_fp_default();dn_foreign; }); }));
}
else{
}
}
(void)(({ dn_r_Gui * dn_tmp_296=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_state))))));
bool dn_tmp_297=1;
dn_f_stop_training(dn_tmp_296,dn_tmp_297); }));
(void)(({ dn_r_Gui * dn_tmp_298=(&((*((dn_r_Gui *)dn_nonnull((void*)(v_state))))));
dn_f_gui_stop(dn_tmp_298); }));
dn_drop_resource_h_delete_object(&v_status_font);
dn_drop_resource_h_delete_object(&v_title_font);
dn_drop_resource_h_delete_object(&v_font);
dn_drop_buf_U16(&v_face);
dn_drop_buf_U16(&v_title);
dn_drop_box_r3_Gui(&v_state);
dn_drop_buf_U16(&v_class_name);
dn_drop_resource_h_delete_object(&v_secondary);
dn_drop_resource_h_delete_object(&v_accent);
dn_drop_resource_h_delete_object(&v_card);
dn_drop_resource_h_delete_object(&v_background);
dn_boundary_end(&dn_tmp_0,0);
}

return 0;
}
static void dn_cb_gui_main(void) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_gui_main();dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_main(void) {
{
(void)(({ dn_f_gui_main(); }));
}

return 0;
}
static void dn_cb_main(void) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_main();dn_boundary_end(&dn_b,0);dn_leave();return;
}
DN_EXPORT size_t dlm_native_sizeof_Host(void){return 264;}
DN_EXPORT size_t dlm_native_alignof_Host(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_Host(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Host_bytes(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Host_length(void){return 256;}
DN_EXPORT size_t dlm_native_sizeof_Overlapped(void){return 32;}
DN_EXPORT size_t dlm_native_alignof_Overlapped(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_Overlapped(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Overlapped_internal(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Overlapped_internal_high(void){return 8;}
DN_EXPORT size_t dlm_native_offset_Overlapped_offset(void){return 16;}
DN_EXPORT size_t dlm_native_offset_Overlapped_event(void){return 24;}
DN_EXPORT size_t dlm_native_sizeof_Gene(void){return 32;}
DN_EXPORT size_t dlm_native_alignof_Gene(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_Gene(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Gene_reverse(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Gene_cut1(void){return 4;}
DN_EXPORT size_t dlm_native_offset_Gene_cut2(void){return 8;}
DN_EXPORT size_t dlm_native_offset_Gene_overlap(void){return 12;}
DN_EXPORT size_t dlm_native_offset_Gene_guard(void){return 16;}
DN_EXPORT size_t dlm_native_offset_Gene_repeats(void){return 20;}
DN_EXPORT size_t dlm_native_offset_Gene_gap(void){return 24;}
DN_EXPORT size_t dlm_native_offset_Gene_template_(void){return 28;}
DN_EXPORT size_t dlm_native_sizeof_Individual(void){return 40;}
DN_EXPORT size_t dlm_native_alignof_Individual(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_Individual(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Individual_gene(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Individual_fitness(void){return 32;}
DN_EXPORT size_t dlm_native_offset_Individual_trials(void){return 36;}
DN_EXPORT size_t dlm_native_sizeof_TrainingView(void){return 96;}
DN_EXPORT size_t dlm_native_alignof_TrainingView(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_TrainingView(void){return 1;}
DN_EXPORT size_t dlm_native_offset_TrainingView_elapsed(void){return 0;}
DN_EXPORT size_t dlm_native_offset_TrainingView_budget(void){return 4;}
DN_EXPORT size_t dlm_native_offset_TrainingView_generation(void){return 8;}
DN_EXPORT size_t dlm_native_offset_TrainingView_tested(void){return 12;}
DN_EXPORT size_t dlm_native_offset_TrainingView_passed(void){return 16;}
DN_EXPORT size_t dlm_native_offset_TrainingView_total(void){return 20;}
DN_EXPORT size_t dlm_native_offset_TrainingView_controls_passed(void){return 24;}
DN_EXPORT size_t dlm_native_offset_TrainingView_controls_total(void){return 28;}
DN_EXPORT size_t dlm_native_offset_TrainingView_attributable(void){return 32;}
DN_EXPORT size_t dlm_native_offset_TrainingView_validated(void){return 36;}
DN_EXPORT size_t dlm_native_offset_TrainingView_media(void){return 40;}
DN_EXPORT size_t dlm_native_offset_TrainingView_phase(void){return 44;}
DN_EXPORT size_t dlm_native_offset_TrainingView_browser(void){return 48;}
DN_EXPORT size_t dlm_native_offset_TrainingView_method(void){return 56;}
DN_EXPORT size_t dlm_native_offset_TrainingView_target(void){return 64;}
DN_EXPORT size_t dlm_native_offset_TrainingView_telegram_web_passed(void){return 68;}
DN_EXPORT size_t dlm_native_offset_TrainingView_telegram_web_total(void){return 72;}
DN_EXPORT size_t dlm_native_offset_TrainingView_telegram_desktop_passed(void){return 76;}
DN_EXPORT size_t dlm_native_offset_TrainingView_telegram_desktop_total(void){return 80;}
DN_EXPORT size_t dlm_native_offset_TrainingView_mwo_passed(void){return 84;}
DN_EXPORT size_t dlm_native_offset_TrainingView_mwo_total(void){return 88;}
DN_EXPORT size_t dlm_native_sizeof_StartupView(void){return 8;}
DN_EXPORT size_t dlm_native_alignof_StartupView(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_StartupView(void){return 1;}
DN_EXPORT size_t dlm_native_offset_StartupView_found(void){return 0;}
DN_EXPORT size_t dlm_native_offset_StartupView_ready(void){return 1;}
DN_EXPORT size_t dlm_native_offset_StartupView_driver(void){return 2;}
DN_EXPORT size_t dlm_native_offset_StartupView_error(void){return 4;}
DN_EXPORT size_t dlm_native_sizeof_WinClass(void){return 80;}
DN_EXPORT size_t dlm_native_alignof_WinClass(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_WinClass(void){return 1;}
DN_EXPORT size_t dlm_native_offset_WinClass_size(void){return 0;}
DN_EXPORT size_t dlm_native_offset_WinClass_style(void){return 4;}
DN_EXPORT size_t dlm_native_offset_WinClass_procedure(void){return 8;}
DN_EXPORT size_t dlm_native_offset_WinClass_class_extra(void){return 16;}
DN_EXPORT size_t dlm_native_offset_WinClass_window_extra(void){return 20;}
DN_EXPORT size_t dlm_native_offset_WinClass_instance(void){return 24;}
DN_EXPORT size_t dlm_native_offset_WinClass_icon(void){return 32;}
DN_EXPORT size_t dlm_native_offset_WinClass_cursor(void){return 40;}
DN_EXPORT size_t dlm_native_offset_WinClass_background(void){return 48;}
DN_EXPORT size_t dlm_native_offset_WinClass_menu(void){return 56;}
DN_EXPORT size_t dlm_native_offset_WinClass_name(void){return 64;}
DN_EXPORT size_t dlm_native_offset_WinClass_small_icon(void){return 72;}
DN_EXPORT size_t dlm_native_sizeof_WinMessage(void){return 48;}
DN_EXPORT size_t dlm_native_alignof_WinMessage(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_WinMessage(void){return 1;}
DN_EXPORT size_t dlm_native_offset_WinMessage_window(void){return 0;}
DN_EXPORT size_t dlm_native_offset_WinMessage_message(void){return 8;}
DN_EXPORT size_t dlm_native_offset_WinMessage_wparam(void){return 16;}
DN_EXPORT size_t dlm_native_offset_WinMessage_lparam(void){return 24;}
DN_EXPORT size_t dlm_native_offset_WinMessage_time(void){return 32;}
DN_EXPORT size_t dlm_native_offset_WinMessage_x(void){return 36;}
DN_EXPORT size_t dlm_native_offset_WinMessage_y(void){return 40;}
DN_EXPORT size_t dlm_native_offset_WinMessage_private_(void){return 44;}
DN_EXPORT size_t dlm_native_sizeof_Rect(void){return 16;}
DN_EXPORT size_t dlm_native_alignof_Rect(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_Rect(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Rect_left(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Rect_top(void){return 4;}
DN_EXPORT size_t dlm_native_offset_Rect_right(void){return 8;}
DN_EXPORT size_t dlm_native_offset_Rect_bottom(void){return 12;}
DN_EXPORT size_t dlm_native_sizeof_CreateStruct(void){return 80;}
DN_EXPORT size_t dlm_native_alignof_CreateStruct(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_CreateStruct(void){return 1;}
DN_EXPORT size_t dlm_native_offset_CreateStruct_parameter(void){return 0;}
DN_EXPORT size_t dlm_native_offset_CreateStruct_instance(void){return 8;}
DN_EXPORT size_t dlm_native_offset_CreateStruct_menu(void){return 16;}
DN_EXPORT size_t dlm_native_offset_CreateStruct_parent(void){return 24;}
DN_EXPORT size_t dlm_native_offset_CreateStruct_height(void){return 32;}
DN_EXPORT size_t dlm_native_offset_CreateStruct_width(void){return 36;}
DN_EXPORT size_t dlm_native_offset_CreateStruct_y(void){return 40;}
DN_EXPORT size_t dlm_native_offset_CreateStruct_x(void){return 44;}
DN_EXPORT size_t dlm_native_offset_CreateStruct_style(void){return 48;}
DN_EXPORT size_t dlm_native_offset_CreateStruct_name(void){return 56;}
DN_EXPORT size_t dlm_native_offset_CreateStruct_class_(void){return 64;}
DN_EXPORT size_t dlm_native_offset_CreateStruct_exstyle(void){return 72;}
DN_EXPORT size_t dlm_native_sizeof_ShellExecute(void){return 112;}
DN_EXPORT size_t dlm_native_alignof_ShellExecute(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_ShellExecute(void){return 1;}
DN_EXPORT size_t dlm_native_offset_ShellExecute_size(void){return 0;}
DN_EXPORT size_t dlm_native_offset_ShellExecute_mask(void){return 4;}
DN_EXPORT size_t dlm_native_offset_ShellExecute_window(void){return 8;}
DN_EXPORT size_t dlm_native_offset_ShellExecute_verb(void){return 16;}
DN_EXPORT size_t dlm_native_offset_ShellExecute_file(void){return 24;}
DN_EXPORT size_t dlm_native_offset_ShellExecute_parameters(void){return 32;}
DN_EXPORT size_t dlm_native_offset_ShellExecute_directory(void){return 40;}
DN_EXPORT size_t dlm_native_offset_ShellExecute_show(void){return 48;}
DN_EXPORT size_t dlm_native_offset_ShellExecute_instance(void){return 56;}
DN_EXPORT size_t dlm_native_offset_ShellExecute_idlist(void){return 64;}
DN_EXPORT size_t dlm_native_offset_ShellExecute_class_(void){return 72;}
DN_EXPORT size_t dlm_native_offset_ShellExecute_class_key(void){return 80;}
DN_EXPORT size_t dlm_native_offset_ShellExecute_hotkey(void){return 88;}
DN_EXPORT size_t dlm_native_offset_ShellExecute_icon(void){return 96;}
DN_EXPORT size_t dlm_native_offset_ShellExecute_process(void){return 104;}
DN_EXPORT size_t dlm_native_sizeof_OpenFileName(void){return 152;}
DN_EXPORT size_t dlm_native_alignof_OpenFileName(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_OpenFileName(void){return 1;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_size(void){return 0;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_owner(void){return 8;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_instance(void){return 16;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_filter(void){return 24;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_custom_filter(void){return 32;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_custom_size(void){return 40;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_filter_index(void){return 44;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_file(void){return 48;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_file_capacity(void){return 56;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_file_title(void){return 64;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_title_capacity(void){return 72;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_initial_dir(void){return 80;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_title(void){return 88;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_flags(void){return 96;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_file_offset(void){return 100;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_extension_offset(void){return 102;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_default_extension(void){return 104;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_userdata(void){return 112;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_hook(void){return 120;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_template_(void){return 128;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_reserved(void){return 136;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_reserved_word(void){return 144;}
DN_EXPORT size_t dlm_native_offset_OpenFileName_extended_flags(void){return 148;}
DN_EXPORT size_t dlm_native_sizeof_PaintStruct(void){return 72;}
DN_EXPORT size_t dlm_native_alignof_PaintStruct(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_PaintStruct(void){return 1;}
DN_EXPORT size_t dlm_native_offset_PaintStruct_dc(void){return 0;}
DN_EXPORT size_t dlm_native_offset_PaintStruct_erase(void){return 8;}
DN_EXPORT size_t dlm_native_offset_PaintStruct_rect(void){return 12;}
DN_EXPORT size_t dlm_native_offset_PaintStruct_restore(void){return 28;}
DN_EXPORT size_t dlm_native_offset_PaintStruct_update(void){return 32;}
DN_EXPORT size_t dlm_native_offset_PaintStruct_reserved(void){return 36;}
DN_EXPORT size_t dlm_native_sizeof_DrawItem(void){return 64;}
DN_EXPORT size_t dlm_native_alignof_DrawItem(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_DrawItem(void){return 1;}
DN_EXPORT size_t dlm_native_offset_DrawItem_type_(void){return 0;}
DN_EXPORT size_t dlm_native_offset_DrawItem_control_id(void){return 4;}
DN_EXPORT size_t dlm_native_offset_DrawItem_item_id(void){return 8;}
DN_EXPORT size_t dlm_native_offset_DrawItem_action(void){return 12;}
DN_EXPORT size_t dlm_native_offset_DrawItem_state(void){return 16;}
DN_EXPORT size_t dlm_native_offset_DrawItem_window(void){return 24;}
DN_EXPORT size_t dlm_native_offset_DrawItem_dc(void){return 32;}
DN_EXPORT size_t dlm_native_offset_DrawItem_rect(void){return 40;}
DN_EXPORT size_t dlm_native_offset_DrawItem_data_(void){return 56;}
DN_EXPORT size_t dlm_native_sizeof_MinMaxInfo(void){return 40;}
DN_EXPORT size_t dlm_native_alignof_MinMaxInfo(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_MinMaxInfo(void){return 1;}
DN_EXPORT size_t dlm_native_offset_MinMaxInfo_reserved_x(void){return 0;}
DN_EXPORT size_t dlm_native_offset_MinMaxInfo_reserved_y(void){return 4;}
DN_EXPORT size_t dlm_native_offset_MinMaxInfo_max_x(void){return 8;}
DN_EXPORT size_t dlm_native_offset_MinMaxInfo_max_y(void){return 12;}
DN_EXPORT size_t dlm_native_offset_MinMaxInfo_position_x(void){return 16;}
DN_EXPORT size_t dlm_native_offset_MinMaxInfo_position_y(void){return 20;}
DN_EXPORT size_t dlm_native_offset_MinMaxInfo_minimum_x(void){return 24;}
DN_EXPORT size_t dlm_native_offset_MinMaxInfo_minimum_y(void){return 28;}
DN_EXPORT size_t dlm_native_offset_MinMaxInfo_track_x(void){return 32;}
DN_EXPORT size_t dlm_native_offset_MinMaxInfo_track_y(void){return 36;}
DN_EXPORT size_t dlm_native_sizeof_Control(void){return 32;}
DN_EXPORT size_t dlm_native_alignof_Control(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_Control(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Control_window(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Control_id(void){return 8;}
DN_EXPORT size_t dlm_native_offset_Control_x(void){return 16;}
DN_EXPORT size_t dlm_native_offset_Control_y(void){return 20;}
DN_EXPORT size_t dlm_native_offset_Control_width(void){return 24;}
DN_EXPORT size_t dlm_native_offset_Control_height(void){return 28;}
DN_EXPORT size_t dlm_native_sizeof_Gui(void){return 288;}
DN_EXPORT size_t dlm_native_alignof_Gui(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_Gui(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Gui_controls(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Gui_window(void){return 24;}
DN_EXPORT size_t dlm_native_offset_Gui_font(void){return 32;}
DN_EXPORT size_t dlm_native_offset_Gui_title_font(void){return 40;}
DN_EXPORT size_t dlm_native_offset_Gui_status_font(void){return 48;}
DN_EXPORT size_t dlm_native_offset_Gui_background(void){return 56;}
DN_EXPORT size_t dlm_native_offset_Gui_card(void){return 64;}
DN_EXPORT size_t dlm_native_offset_Gui_accent(void){return 72;}
DN_EXPORT size_t dlm_native_offset_Gui_secondary(void){return 80;}
DN_EXPORT size_t dlm_native_offset_Gui_process(void){return 88;}
DN_EXPORT size_t dlm_native_offset_Gui_preflight(void){return 96;}
DN_EXPORT size_t dlm_native_offset_Gui_note(void){return 104;}
DN_EXPORT size_t dlm_native_offset_Gui_phase(void){return 128;}
DN_EXPORT size_t dlm_native_offset_Gui_deadline(void){return 136;}
DN_EXPORT size_t dlm_native_offset_Gui_dpi(void){return 144;}
DN_EXPORT size_t dlm_native_offset_Gui_training(void){return 152;}
DN_EXPORT size_t dlm_native_offset_Gui_view(void){return 160;}
DN_EXPORT size_t dlm_native_offset_Gui_history(void){return 256;}
DN_EXPORT size_t dlm_native_offset_Gui_session(void){return 264;}
int main(void){(void)dlm_native_thread_init();dn_boundary b={0};jmp_buf dn_jump;b.jump=&dn_jump;b.foreign_marker=dn_foreign_context;dn_current=&b;dn_fp_begin(&b);if(setjmp(dn_jump)){dn_boundary_end(&b,1);fprintf(stderr,"DLM native error: %s\n",dn_error);return 1;}(void)dn_f_main();dn_boundary_end(&b,0);(void)dlm_native_runtime_shutdown();return 0;}
