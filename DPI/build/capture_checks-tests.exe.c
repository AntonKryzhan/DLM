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
typedef struct dn_r_Gene dn_r_Gene;
typedef struct dn_r_Individual dn_r_Individual;
typedef struct dn_r_Options dn_r_Options;
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
typedef struct dn_t_buf_U8 { uint8_t *data;size_t len,cap; } dn_t_buf_U8;
typedef struct dn_t_buf_buf_U8 { dn_t_buf_U8 *data;size_t len,cap; } dn_t_buf_buf_U8;
struct dn_r_Options {
    bool f_aggressive;
    bool f_fake;
    bool f_http;
    bool f_sni;
    bool f_reverse;
    bool f_ech;
    bool f_adaptive;
    bool f_learn;
    bool f_all_ports;
    bool f_game;
    bool f_whitelist;
    uint32_t f_quic;
    uint32_t f_probe_ms;
    uint32_t f_fake_mode;
    uint32_t f_split_mode;
    uint32_t f_strategy;
    uint32_t f_first_packets;
    uint32_t f_jitter_ms;
    uint32_t f_ttl_margin;
    uint32_t f_ttl_spread;
    uint32_t f_ttl_max;
    uint32_t f_min_hops;
    uint32_t f_debug;
    bool f_help;
    bool f_error;
    bool f_auto_strategy;
    uint32_t f_forced_arm;
    bool f_server_window;
    bool f_udp_obfuscate;
    bool f_diagnostic;
    uint32_t f_handshake_timeout;
    bool f_safe_automatic;
    bool f_telegram;
    bool f_cloud_cdn;
    bool f_driver_check;
    dn_t_buf_U8 f_driver_report;
    uint32_t f_parent_pid;
    bool f_gene_mode;
    dn_r_Gene f_gene;
    uint32_t f_gene_target;
    dn_t_buf_U8 f_trial_tag;
    dn_t_buf_buf_U8 f_paths;
};
typedef struct dn_t_arr20_U8 { uint8_t data[20]; } dn_t_arr20_U8;
typedef struct dn_t_arr12_U32 { uint32_t data[12]; } dn_t_arr12_U32;
typedef struct dn_t_buf_r4_Gene { dn_r_Gene *data;size_t len,cap; } dn_t_buf_r4_Gene;
typedef struct dn_t_buf_r10_Individual { dn_r_Individual *data;size_t len,cap; } dn_t_buf_r10_Individual;
typedef struct dn_t_slice0_U8 { uint8_t const *data;size_t len; } dn_t_slice0_U8;
_Static_assert(sizeof(dn_r_Host)==264,"Host ABI size");
_Static_assert(_Alignof(dn_r_Host)==8,"Host ABI alignment");
_Static_assert(offsetof(dn_r_Host,f_bytes)==0,"Host.bytes ABI offset");
_Static_assert(offsetof(dn_r_Host,f_length)==256,"Host.length ABI offset");
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
_Static_assert(sizeof(dn_r_Options)==200,"Options ABI size");
_Static_assert(_Alignof(dn_r_Options)==8,"Options ABI alignment");
_Static_assert(offsetof(dn_r_Options,f_aggressive)==0,"Options.aggressive ABI offset");
_Static_assert(offsetof(dn_r_Options,f_fake)==1,"Options.fake ABI offset");
_Static_assert(offsetof(dn_r_Options,f_http)==2,"Options.http ABI offset");
_Static_assert(offsetof(dn_r_Options,f_sni)==3,"Options.sni ABI offset");
_Static_assert(offsetof(dn_r_Options,f_reverse)==4,"Options.reverse ABI offset");
_Static_assert(offsetof(dn_r_Options,f_ech)==5,"Options.ech ABI offset");
_Static_assert(offsetof(dn_r_Options,f_adaptive)==6,"Options.adaptive ABI offset");
_Static_assert(offsetof(dn_r_Options,f_learn)==7,"Options.learn ABI offset");
_Static_assert(offsetof(dn_r_Options,f_all_ports)==8,"Options.all_ports ABI offset");
_Static_assert(offsetof(dn_r_Options,f_game)==9,"Options.game ABI offset");
_Static_assert(offsetof(dn_r_Options,f_whitelist)==10,"Options.whitelist ABI offset");
_Static_assert(offsetof(dn_r_Options,f_quic)==12,"Options.quic ABI offset");
_Static_assert(offsetof(dn_r_Options,f_probe_ms)==16,"Options.probe_ms ABI offset");
_Static_assert(offsetof(dn_r_Options,f_fake_mode)==20,"Options.fake_mode ABI offset");
_Static_assert(offsetof(dn_r_Options,f_split_mode)==24,"Options.split_mode ABI offset");
_Static_assert(offsetof(dn_r_Options,f_strategy)==28,"Options.strategy ABI offset");
_Static_assert(offsetof(dn_r_Options,f_first_packets)==32,"Options.first_packets ABI offset");
_Static_assert(offsetof(dn_r_Options,f_jitter_ms)==36,"Options.jitter_ms ABI offset");
_Static_assert(offsetof(dn_r_Options,f_ttl_margin)==40,"Options.ttl_margin ABI offset");
_Static_assert(offsetof(dn_r_Options,f_ttl_spread)==44,"Options.ttl_spread ABI offset");
_Static_assert(offsetof(dn_r_Options,f_ttl_max)==48,"Options.ttl_max ABI offset");
_Static_assert(offsetof(dn_r_Options,f_min_hops)==52,"Options.min_hops ABI offset");
_Static_assert(offsetof(dn_r_Options,f_debug)==56,"Options.debug ABI offset");
_Static_assert(offsetof(dn_r_Options,f_help)==60,"Options.help ABI offset");
_Static_assert(offsetof(dn_r_Options,f_error)==61,"Options.error ABI offset");
_Static_assert(offsetof(dn_r_Options,f_auto_strategy)==62,"Options.auto_strategy ABI offset");
_Static_assert(offsetof(dn_r_Options,f_forced_arm)==64,"Options.forced_arm ABI offset");
_Static_assert(offsetof(dn_r_Options,f_server_window)==68,"Options.server_window ABI offset");
_Static_assert(offsetof(dn_r_Options,f_udp_obfuscate)==69,"Options.udp_obfuscate ABI offset");
_Static_assert(offsetof(dn_r_Options,f_diagnostic)==70,"Options.diagnostic ABI offset");
_Static_assert(offsetof(dn_r_Options,f_handshake_timeout)==72,"Options.handshake_timeout ABI offset");
_Static_assert(offsetof(dn_r_Options,f_safe_automatic)==76,"Options.safe_automatic ABI offset");
_Static_assert(offsetof(dn_r_Options,f_telegram)==77,"Options.telegram ABI offset");
_Static_assert(offsetof(dn_r_Options,f_cloud_cdn)==78,"Options.cloud_cdn ABI offset");
_Static_assert(offsetof(dn_r_Options,f_driver_check)==79,"Options.driver_check ABI offset");
_Static_assert(offsetof(dn_r_Options,f_driver_report)==80,"Options.driver_report ABI offset");
_Static_assert(offsetof(dn_r_Options,f_parent_pid)==104,"Options.parent_pid ABI offset");
_Static_assert(offsetof(dn_r_Options,f_gene_mode)==108,"Options.gene_mode ABI offset");
_Static_assert(offsetof(dn_r_Options,f_gene)==112,"Options.gene ABI offset");
_Static_assert(offsetof(dn_r_Options,f_gene_target)==144,"Options.gene_target ABI offset");
_Static_assert(offsetof(dn_r_Options,f_trial_tag)==152,"Options.trial_tag ABI offset");
_Static_assert(offsetof(dn_r_Options,f_paths)==176,"Options.paths ABI offset");
static void dn_drop_r7_Options(dn_r_Options volatile*);static void dn_retain_r7_Options(dn_r_Options volatile*);
static void dn_drop_buf_U8(dn_t_buf_U8 volatile*);static void dn_retain_buf_U8(dn_t_buf_U8 volatile*);
static void dn_drop_buf_r4_Gene(dn_t_buf_r4_Gene volatile*);static void dn_retain_buf_r4_Gene(dn_t_buf_r4_Gene volatile*);
static void dn_drop_buf_r10_Individual(dn_t_buf_r10_Individual volatile*);static void dn_retain_buf_r10_Individual(dn_t_buf_r10_Individual volatile*);
static void dn_drop_buf_buf_U8(dn_t_buf_buf_U8 volatile*);static void dn_retain_buf_buf_U8(dn_t_buf_buf_U8 volatile*);
static void dn_adopt_r7_Options(dn_r_Options volatile*);
static void dn_adopt_buf_U8(dn_t_buf_U8 volatile*);
static void dn_adopt_buf_r4_Gene(dn_t_buf_r4_Gene volatile*);
static void dn_adopt_buf_r10_Individual(dn_t_buf_r10_Individual volatile*);
static void dn_adopt_buf_buf_U8(dn_t_buf_buf_U8 volatile*);
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
static dn_r_Options dn_f_default_options(void);
static bool dn_f_auto_ttl(dn_r_Options * v_o,dn_t_slice0_U8 v_value);
static bool dn_f_option(dn_r_Options * v_o,dn_t_slice0_U8 v_arg);
static uint8_t dn_f_automatic_options(dn_r_Options * v_o);
static uint8_t dn_f_finish_options(dn_r_Options * v_o);
static dn_t_buf_U8 dn_f_divert_filter(dn_r_Options const * v_o);
static uint8_t dn_f_main(void);
static void dn_drop_r7_Options(dn_r_Options volatile *x); static void dn_retain_r7_Options(dn_r_Options volatile *x);
static void dn_drop_r7_Options(dn_r_Options volatile *x){dn_drop_buf_buf_U8(&x->f_paths);dn_drop_buf_U8(&x->f_trial_tag);dn_drop_buf_U8(&x->f_driver_report);}
static void dn_retain_r7_Options(dn_r_Options volatile *x){dn_retain_buf_U8(&x->f_driver_report);dn_retain_buf_U8(&x->f_trial_tag);dn_retain_buf_buf_U8(&x->f_paths);}
static void dn_adopt_r7_Options(dn_r_Options volatile*x){dn_adopt_buf_U8(&x->f_driver_report);dn_adopt_buf_U8(&x->f_trial_tag);dn_adopt_buf_buf_U8(&x->f_paths);}
static void dn_drop_buf_U8(dn_t_buf_U8 volatile *x); static void dn_retain_buf_U8(dn_t_buf_U8 volatile *x);
static void dn_drop_buf_U8(dn_t_buf_U8 volatile *x){dn_deallocate(x->data);*x=(dn_t_buf_U8){0};}
static void dn_retain_buf_U8(dn_t_buf_U8 volatile *x){dn_detach(x->data);}
static dn_t_buf_U8 dn_new_buf_U8(size_t n){dn_t_buf_U8 x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(uint8_t)),_Alignof(uint8_t));x.len=x.cap=n;return x;}
static dn_t_buf_U8 dn_aligned_buf_U8(size_t n,size_t a){if(a<_Alignof(uint8_t)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_U8 x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(uint8_t)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_U8(dn_t_buf_U8 *x,uint8_t v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;uint8_t *p=dn_allocate_aligned(dn_size(cap,sizeof(uint8_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(uint8_t));if(x->len)memcpy(p,x->data,x->len*sizeof(uint8_t));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;x->len++;return 0;}
static uint8_t dn_resize_buf_U8(dn_t_buf_U8 *x,size_t n){if(n<x->len){}if(n>x->cap){size_t cap=n;uint8_t *p=dn_allocate_aligned(dn_size(cap,sizeof(uint8_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(uint8_t));if(x->len)memcpy(p,x->data,x->len*sizeof(uint8_t));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(uint8_t));x->len=n;return 0;}
static void dn_adopt_buf_U8(dn_t_buf_U8 volatile*x){dn_adopt(x->data);}
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
static dn_r_Options dn_cb_default_options(void);
static bool dn_cb_auto_ttl(dn_r_Options * v_o,dn_t_slice0_U8 v_value);
static bool dn_cb_option(dn_r_Options * v_o,dn_t_slice0_U8 v_arg);
static void dn_cb_automatic_options(dn_r_Options * v_o);
static void dn_cb_finish_options(dn_r_Options * v_o);
static dn_t_buf_U8 dn_cb_divert_filter(dn_r_Options const * v_o);
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
static dn_r_Options dn_f_default_options(void) {
{
dn_r_Options volatile v_o={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_r7_Options(&v_o);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_o=({ ((dn_r_Options){0}); });
{ bool dn_tmp_3=1;bool *dn_tmp_2=&((v_o).f_cloud_cdn);*dn_tmp_2=dn_tmp_3; }
{ bool dn_tmp_5=1;bool *dn_tmp_4=&((v_o).f_telegram);*dn_tmp_4=dn_tmp_5; }
{ dn_r_Gene dn_tmp_7=({ dn_f_gene_default(); });dn_r_Gene *dn_tmp_6=&((v_o).f_gene);*dn_tmp_6=dn_tmp_7; }
{ bool dn_tmp_9=1;bool *dn_tmp_8=&((v_o).f_auto_strategy);*dn_tmp_8=dn_tmp_9; }
{ uint32_t dn_tmp_11=((uint32_t)(8ULL));uint32_t *dn_tmp_10=&((v_o).f_forced_arm);*dn_tmp_10=dn_tmp_11; }
{ bool dn_tmp_13=1;bool *dn_tmp_12=&((v_o).f_server_window);*dn_tmp_12=dn_tmp_13; }
{ bool dn_tmp_15=1;bool *dn_tmp_14=&((v_o).f_diagnostic);*dn_tmp_14=dn_tmp_15; }
{ uint32_t dn_tmp_17=((uint32_t)(6000ULL));uint32_t *dn_tmp_16=&((v_o).f_handshake_timeout);*dn_tmp_16=dn_tmp_17; }
{ bool dn_tmp_19=1;bool *dn_tmp_18=&((v_o).f_aggressive);*dn_tmp_18=dn_tmp_19; }
{ bool dn_tmp_21=1;bool *dn_tmp_20=&((v_o).f_sni);*dn_tmp_20=dn_tmp_21; }
{ bool dn_tmp_23=1;bool *dn_tmp_22=&((v_o).f_ech);*dn_tmp_22=dn_tmp_23; }
{ bool dn_tmp_25=1;bool *dn_tmp_24=&((v_o).f_adaptive);*dn_tmp_24=dn_tmp_25; }
{ bool dn_tmp_27=1;bool *dn_tmp_26=&((v_o).f_learn);*dn_tmp_26=dn_tmp_27; }
{ uint32_t dn_tmp_29=((uint32_t)(750ULL));uint32_t *dn_tmp_28=&((v_o).f_probe_ms);*dn_tmp_28=dn_tmp_29; }
{ uint32_t dn_tmp_31=((uint32_t)(3ULL));uint32_t *dn_tmp_30=&((v_o).f_first_packets);*dn_tmp_30=dn_tmp_31; }
{ size_t dn_tmp_33=((size_t)(0ULL));size_t dn_tmp_32=((size_t)(8ULL));for(size_t v_i=dn_tmp_33;v_i<dn_tmp_32;v_i++){
(void)(({ dn_t_buf_buf_U8 * dn_tmp_34=(&((v_o).f_paths));
dn_t_buf_U8 dn_tmp_35=({ size_t dn_tmp_36=((size_t)(0ULL));
dn_new_buf_U8(dn_tmp_36); });
dn_adopt_buf_U8(&dn_tmp_35);
dn_push_buf_buf_U8(dn_tmp_34,dn_tmp_35); }));
}
 }
{ dn_r_Options dn_tmp_38=({ dn_r_Options dn_tmp_37=v_o;v_o=(dn_r_Options){0};dn_adopt_r7_Options(&dn_tmp_37);dn_tmp_37; });dn_adopt_r7_Options(&dn_tmp_38);dn_drop_r7_Options(&v_o);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_38; }
dn_drop_r7_Options(&v_o);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_r_Options dn_cb_default_options(void) {
 if(!dn_enter()){return (dn_r_Options){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_r_Options){0};}
 dn_r_Options dn_result=dn_f_default_options();dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_auto_ttl(dn_r_Options * v_o,dn_t_slice0_U8 v_value) {
{
size_t v_a=({ dn_t_slice0_U8 dn_tmp_0=v_value;
size_t dn_tmp_1=((size_t)(0ULL));
uint8_t dn_tmp_2=({ uint8_t dn_tmp_3=((uint8_t)(44ULL));
if((unsigned __int128)dn_tmp_3>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_3; });
dn_f_byte_find(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
if(({ size_t dn_tmp_4=v_a;
size_t dn_tmp_5=({ dn_t_slice0_U8 dn_tmp_6=v_value;
(dn_tmp_6).len; });
(dn_tmp_4 == dn_tmp_5); })){
{ bool dn_tmp_7=0;return dn_tmp_7; }
}
else{
}
size_t v_b=({ dn_t_slice0_U8 dn_tmp_8=v_value;
size_t dn_tmp_9=({ size_t dn_tmp_10=v_a;
size_t dn_tmp_11=((size_t)(1ULL));
dn_USize_add(dn_tmp_10,dn_tmp_11); });
uint8_t dn_tmp_12=({ uint8_t dn_tmp_13=((uint8_t)(44ULL));
if((unsigned __int128)dn_tmp_13>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_13; });
dn_f_byte_find(dn_tmp_8,dn_tmp_9,dn_tmp_12); });
if(({ size_t dn_tmp_14=v_b;
size_t dn_tmp_15=({ dn_t_slice0_U8 dn_tmp_16=v_value;
(dn_tmp_16).len; });
(dn_tmp_14 == dn_tmp_15); })){
{ bool dn_tmp_17=0;return dn_tmp_17; }
}
else{
}
uint32_t v_x=((uint32_t)(0ULL));
uint32_t v_y=((uint32_t)(0ULL));
uint32_t v_z=((uint32_t)(0ULL));
if((((!(({ dn_t_slice0_U8 dn_tmp_18=({ dn_t_slice0_U8 const * dn_tmp_19=(&(v_value));
size_t dn_tmp_20=((size_t)(0ULL));
size_t dn_tmp_21=v_a;
({ dn_range(dn_tmp_20,dn_tmp_21,(*(dn_tmp_19)).len);(dn_t_slice0_U8){(*(dn_tmp_19)).data?(*(dn_tmp_19)).data+dn_tmp_20:NULL,dn_tmp_21}; }); });
uint32_t dn_tmp_22=((uint32_t)(255ULL));
uint32_t * dn_tmp_23=(&(v_x));
dn_f_decimal(dn_tmp_18,dn_tmp_22,dn_tmp_23); }))) || (!(({ dn_t_slice0_U8 dn_tmp_24=({ dn_t_slice0_U8 const * dn_tmp_25=(&(v_value));
size_t dn_tmp_26=({ size_t dn_tmp_27=v_a;
size_t dn_tmp_28=((size_t)(1ULL));
dn_USize_add(dn_tmp_27,dn_tmp_28); });
size_t dn_tmp_29=({ size_t dn_tmp_30=({ size_t dn_tmp_31=v_b;
size_t dn_tmp_32=v_a;
dn_USize_sub(dn_tmp_31,dn_tmp_32); });
size_t dn_tmp_33=((size_t)(1ULL));
dn_USize_sub(dn_tmp_30,dn_tmp_33); });
({ dn_range(dn_tmp_26,dn_tmp_29,(*(dn_tmp_25)).len);(dn_t_slice0_U8){(*(dn_tmp_25)).data?(*(dn_tmp_25)).data+dn_tmp_26:NULL,dn_tmp_29}; }); });
uint32_t dn_tmp_34=((uint32_t)(255ULL));
uint32_t * dn_tmp_35=(&(v_y));
dn_f_decimal(dn_tmp_24,dn_tmp_34,dn_tmp_35); })))) || (!(({ dn_t_slice0_U8 dn_tmp_36=({ dn_t_slice0_U8 const * dn_tmp_37=(&(v_value));
size_t dn_tmp_38=({ size_t dn_tmp_39=v_b;
size_t dn_tmp_40=((size_t)(1ULL));
dn_USize_add(dn_tmp_39,dn_tmp_40); });
size_t dn_tmp_41=({ size_t dn_tmp_42=({ size_t dn_tmp_43=({ dn_t_slice0_U8 dn_tmp_44=v_value;
(dn_tmp_44).len; });
size_t dn_tmp_45=v_b;
dn_USize_sub(dn_tmp_43,dn_tmp_45); });
size_t dn_tmp_46=((size_t)(1ULL));
dn_USize_sub(dn_tmp_42,dn_tmp_46); });
({ dn_range(dn_tmp_38,dn_tmp_41,(*(dn_tmp_37)).len);(dn_t_slice0_U8){(*(dn_tmp_37)).data?(*(dn_tmp_37)).data+dn_tmp_38:NULL,dn_tmp_41}; }); });
uint32_t dn_tmp_47=((uint32_t)(255ULL));
uint32_t * dn_tmp_48=(&(v_z));
dn_f_decimal(dn_tmp_36,dn_tmp_47,dn_tmp_48); }))))){
{ bool dn_tmp_49=0;return dn_tmp_49; }
}
else{
}
{ uint32_t dn_tmp_51=v_x;uint32_t *dn_tmp_50=&((v_o)->f_ttl_margin);*dn_tmp_50=dn_tmp_51; }
{ uint32_t dn_tmp_53=v_y;uint32_t *dn_tmp_52=&((v_o)->f_ttl_spread);*dn_tmp_52=dn_tmp_53; }
{ uint32_t dn_tmp_55=v_z;uint32_t *dn_tmp_54=&((v_o)->f_ttl_max);*dn_tmp_54=dn_tmp_55; }
{ bool dn_tmp_56=1;return dn_tmp_56; }
}

dn_fail("missing native return");
}
static bool dn_cb_auto_ttl(dn_r_Options * v_o,dn_t_slice0_U8 v_value) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_auto_ttl(v_o,v_value);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static bool dn_f_option(dn_r_Options * v_o,dn_t_slice0_U8 v_arg) {
{
size_t v_eq=({ dn_t_slice0_U8 dn_tmp_0=v_arg;
size_t dn_tmp_1=((size_t)(0ULL));
uint8_t dn_tmp_2=({ uint8_t dn_tmp_3=((uint8_t)(61ULL));
if((unsigned __int128)dn_tmp_3>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_3; });
dn_f_byte_find(dn_tmp_0,dn_tmp_1,dn_tmp_2); });
dn_t_slice0_U8 v_name=({ dn_t_slice0_U8 const * dn_tmp_4=(&(v_arg));
size_t dn_tmp_5=((size_t)(0ULL));
size_t dn_tmp_6=v_eq;
({ dn_range(dn_tmp_5,dn_tmp_6,(*(dn_tmp_4)).len);(dn_t_slice0_U8){(*(dn_tmp_4)).data?(*(dn_tmp_4)).data+dn_tmp_5:NULL,dn_tmp_6}; }); });
if(({ size_t dn_tmp_7=v_eq;
size_t dn_tmp_8=({ dn_t_slice0_U8 dn_tmp_9=v_arg;
(dn_tmp_9).len; });
(dn_tmp_7 == dn_tmp_8); })){
if(({ dn_t_slice0_U8 dn_tmp_10=v_name;
dn_t_slice0_U8 dn_tmp_11=({ dn_utf8 dn_tmp_12=((dn_utf8){(const uint8_t*)"--driver-check", 14});
((dn_t_slice0_U8){dn_tmp_12.data,dn_tmp_12.len}); });
dn_f_bytes_equal(dn_tmp_10,dn_tmp_11); })){
{ bool dn_tmp_14=1;bool *dn_tmp_13=&((v_o)->f_driver_check);*dn_tmp_13=dn_tmp_14; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_15=v_name;
dn_t_slice0_U8 dn_tmp_16=({ dn_utf8 dn_tmp_17=((dn_utf8){(const uint8_t*)"--cloud-cdn", 11});
((dn_t_slice0_U8){dn_tmp_17.data,dn_tmp_17.len}); });
dn_f_bytes_equal(dn_tmp_15,dn_tmp_16); })){
{ bool dn_tmp_19=1;bool *dn_tmp_18=&((v_o)->f_cloud_cdn);*dn_tmp_18=dn_tmp_19; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_20=v_name;
dn_t_slice0_U8 dn_tmp_21=({ dn_utf8 dn_tmp_22=((dn_utf8){(const uint8_t*)"--no-cloud-cdn", 14});
((dn_t_slice0_U8){dn_tmp_22.data,dn_tmp_22.len}); });
dn_f_bytes_equal(dn_tmp_20,dn_tmp_21); })){
{ bool dn_tmp_24=0;bool *dn_tmp_23=&((v_o)->f_cloud_cdn);*dn_tmp_23=dn_tmp_24; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_25=v_name;
dn_t_slice0_U8 dn_tmp_26=({ dn_utf8 dn_tmp_27=((dn_utf8){(const uint8_t*)"--telegram", 10});
((dn_t_slice0_U8){dn_tmp_27.data,dn_tmp_27.len}); });
dn_f_bytes_equal(dn_tmp_25,dn_tmp_26); })){
{ bool dn_tmp_29=1;bool *dn_tmp_28=&((v_o)->f_telegram);*dn_tmp_28=dn_tmp_29; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_30=v_name;
dn_t_slice0_U8 dn_tmp_31=({ dn_utf8 dn_tmp_32=((dn_utf8){(const uint8_t*)"--no-telegram", 13});
((dn_t_slice0_U8){dn_tmp_32.data,dn_tmp_32.len}); });
dn_f_bytes_equal(dn_tmp_30,dn_tmp_31); })){
{ bool dn_tmp_34=0;bool *dn_tmp_33=&((v_o)->f_telegram);*dn_tmp_33=dn_tmp_34; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_35=v_name;
dn_t_slice0_U8 dn_tmp_36=({ dn_utf8 dn_tmp_37=((dn_utf8){(const uint8_t*)"--automatic", 11});
((dn_t_slice0_U8){dn_tmp_37.data,dn_tmp_37.len}); });
dn_f_bytes_equal(dn_tmp_35,dn_tmp_36); })){
{ bool dn_tmp_39=1;bool *dn_tmp_38=&((v_o)->f_safe_automatic);*dn_tmp_38=dn_tmp_39; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_40=v_name;
dn_t_slice0_U8 dn_tmp_41=({ dn_utf8 dn_tmp_42=((dn_utf8){(const uint8_t*)"--no-automatic", 14});
((dn_t_slice0_U8){dn_tmp_42.data,dn_tmp_42.len}); });
dn_f_bytes_equal(dn_tmp_40,dn_tmp_41); })){
{ bool dn_tmp_44=0;bool *dn_tmp_43=&((v_o)->f_safe_automatic);*dn_tmp_43=dn_tmp_44; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_45=v_name;
dn_t_slice0_U8 dn_tmp_46=({ dn_utf8 dn_tmp_47=((dn_utf8){(const uint8_t*)"--adaptive-strategies", 21});
((dn_t_slice0_U8){dn_tmp_47.data,dn_tmp_47.len}); });
dn_f_bytes_equal(dn_tmp_45,dn_tmp_46); })){
{ bool dn_tmp_49=1;bool *dn_tmp_48=&((v_o)->f_auto_strategy);*dn_tmp_48=dn_tmp_49; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_50=v_name;
dn_t_slice0_U8 dn_tmp_51=({ dn_utf8 dn_tmp_52=((dn_utf8){(const uint8_t*)"--no-adaptive-strategies", 24});
((dn_t_slice0_U8){dn_tmp_52.data,dn_tmp_52.len}); });
dn_f_bytes_equal(dn_tmp_50,dn_tmp_51); })){
{ bool dn_tmp_54=0;bool *dn_tmp_53=&((v_o)->f_auto_strategy);*dn_tmp_53=dn_tmp_54; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_55=v_name;
dn_t_slice0_U8 dn_tmp_56=({ dn_utf8 dn_tmp_57=((dn_utf8){(const uint8_t*)"--server-window", 15});
((dn_t_slice0_U8){dn_tmp_57.data,dn_tmp_57.len}); });
dn_f_bytes_equal(dn_tmp_55,dn_tmp_56); })){
{ bool dn_tmp_59=1;bool *dn_tmp_58=&((v_o)->f_server_window);*dn_tmp_58=dn_tmp_59; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_60=v_name;
dn_t_slice0_U8 dn_tmp_61=({ dn_utf8 dn_tmp_62=((dn_utf8){(const uint8_t*)"--no-server-window", 18});
((dn_t_slice0_U8){dn_tmp_62.data,dn_tmp_62.len}); });
dn_f_bytes_equal(dn_tmp_60,dn_tmp_61); })){
{ bool dn_tmp_64=0;bool *dn_tmp_63=&((v_o)->f_server_window);*dn_tmp_63=dn_tmp_64; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_65=v_name;
dn_t_slice0_U8 dn_tmp_66=({ dn_utf8 dn_tmp_67=((dn_utf8){(const uint8_t*)"--udp-obfuscate", 15});
((dn_t_slice0_U8){dn_tmp_67.data,dn_tmp_67.len}); });
dn_f_bytes_equal(dn_tmp_65,dn_tmp_66); })){
{ bool dn_tmp_69=1;bool *dn_tmp_68=&((v_o)->f_udp_obfuscate);*dn_tmp_68=dn_tmp_69; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_70=v_name;
dn_t_slice0_U8 dn_tmp_71=({ dn_utf8 dn_tmp_72=((dn_utf8){(const uint8_t*)"--diagnostic-log", 16});
((dn_t_slice0_U8){dn_tmp_72.data,dn_tmp_72.len}); });
dn_f_bytes_equal(dn_tmp_70,dn_tmp_71); })){
{ bool dn_tmp_74=1;bool *dn_tmp_73=&((v_o)->f_diagnostic);*dn_tmp_73=dn_tmp_74; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_75=v_name;
dn_t_slice0_U8 dn_tmp_76=({ dn_utf8 dn_tmp_77=((dn_utf8){(const uint8_t*)"--no-diagnostic-log", 19});
((dn_t_slice0_U8){dn_tmp_77.data,dn_tmp_77.len}); });
dn_f_bytes_equal(dn_tmp_75,dn_tmp_76); })){
{ bool dn_tmp_79=0;bool *dn_tmp_78=&((v_o)->f_diagnostic);*dn_tmp_78=dn_tmp_79; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_80=v_name;
dn_t_slice0_U8 dn_tmp_81=({ dn_utf8 dn_tmp_82=((dn_utf8){(const uint8_t*)"--aggressive-frag", 17});
((dn_t_slice0_U8){dn_tmp_82.data,dn_tmp_82.len}); });
dn_f_bytes_equal(dn_tmp_80,dn_tmp_81); })){
{ bool dn_tmp_84=1;bool *dn_tmp_83=&((v_o)->f_aggressive);*dn_tmp_83=dn_tmp_84; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_85=v_name;
dn_t_slice0_U8 dn_tmp_86=({ dn_utf8 dn_tmp_87=((dn_utf8){(const uint8_t*)"--no-aggressive-frag", 20});
((dn_t_slice0_U8){dn_tmp_87.data,dn_tmp_87.len}); });
dn_f_bytes_equal(dn_tmp_85,dn_tmp_86); })){
{ bool dn_tmp_89=0;bool *dn_tmp_88=&((v_o)->f_aggressive);*dn_tmp_88=dn_tmp_89; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_90=v_name;
dn_t_slice0_U8 dn_tmp_91=({ dn_utf8 dn_tmp_92=((dn_utf8){(const uint8_t*)"--frag-by-sni", 13});
((dn_t_slice0_U8){dn_tmp_92.data,dn_tmp_92.len}); });
dn_f_bytes_equal(dn_tmp_90,dn_tmp_91); })){
{ bool dn_tmp_94=1;bool *dn_tmp_93=&((v_o)->f_sni);*dn_tmp_93=dn_tmp_94; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_95=v_name;
dn_t_slice0_U8 dn_tmp_96=({ dn_utf8 dn_tmp_97=((dn_utf8){(const uint8_t*)"--no-frag-by-sni", 16});
((dn_t_slice0_U8){dn_tmp_97.data,dn_tmp_97.len}); });
dn_f_bytes_equal(dn_tmp_95,dn_tmp_96); })){
{ bool dn_tmp_99=0;bool *dn_tmp_98=&((v_o)->f_sni);*dn_tmp_98=dn_tmp_99; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_100=v_name;
dn_t_slice0_U8 dn_tmp_101=({ dn_utf8 dn_tmp_102=((dn_utf8){(const uint8_t*)"--apply-ech", 11});
((dn_t_slice0_U8){dn_tmp_102.data,dn_tmp_102.len}); });
dn_f_bytes_equal(dn_tmp_100,dn_tmp_101); })){
{ bool dn_tmp_104=1;bool *dn_tmp_103=&((v_o)->f_ech);*dn_tmp_103=dn_tmp_104; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_105=v_name;
dn_t_slice0_U8 dn_tmp_106=({ dn_utf8 dn_tmp_107=((dn_utf8){(const uint8_t*)"--no-apply-ech", 14});
((dn_t_slice0_U8){dn_tmp_107.data,dn_tmp_107.len}); });
dn_f_bytes_equal(dn_tmp_105,dn_tmp_106); })){
{ bool dn_tmp_109=0;bool *dn_tmp_108=&((v_o)->f_ech);*dn_tmp_108=dn_tmp_109; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_110=v_name;
dn_t_slice0_U8 dn_tmp_111=({ dn_utf8 dn_tmp_112=((dn_utf8){(const uint8_t*)"--adaptive-retry", 16});
((dn_t_slice0_U8){dn_tmp_112.data,dn_tmp_112.len}); });
dn_f_bytes_equal(dn_tmp_110,dn_tmp_111); })){
{ bool dn_tmp_114=1;bool *dn_tmp_113=&((v_o)->f_adaptive);*dn_tmp_113=dn_tmp_114; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_115=v_name;
dn_t_slice0_U8 dn_tmp_116=({ dn_utf8 dn_tmp_117=((dn_utf8){(const uint8_t*)"--no-adaptive-retry", 19});
((dn_t_slice0_U8){dn_tmp_117.data,dn_tmp_117.len}); });
dn_f_bytes_equal(dn_tmp_115,dn_tmp_116); })){
{ bool dn_tmp_119=0;bool *dn_tmp_118=&((v_o)->f_adaptive);*dn_tmp_118=dn_tmp_119; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_120=v_name;
dn_t_slice0_U8 dn_tmp_121=({ dn_utf8 dn_tmp_122=((dn_utf8){(const uint8_t*)"--learn-strategy", 16});
((dn_t_slice0_U8){dn_tmp_122.data,dn_tmp_122.len}); });
dn_f_bytes_equal(dn_tmp_120,dn_tmp_121); })){
{ bool dn_tmp_124=1;bool *dn_tmp_123=&((v_o)->f_learn);*dn_tmp_123=dn_tmp_124; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_125=v_name;
dn_t_slice0_U8 dn_tmp_126=({ dn_utf8 dn_tmp_127=((dn_utf8){(const uint8_t*)"--no-learn-strategy", 19});
((dn_t_slice0_U8){dn_tmp_127.data,dn_tmp_127.len}); });
dn_f_bytes_equal(dn_tmp_125,dn_tmp_126); })){
{ bool dn_tmp_129=0;bool *dn_tmp_128=&((v_o)->f_learn);*dn_tmp_128=dn_tmp_129; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_130=v_name;
dn_t_slice0_U8 dn_tmp_131=({ dn_utf8 dn_tmp_132=((dn_utf8){(const uint8_t*)"--fake-packets", 14});
((dn_t_slice0_U8){dn_tmp_132.data,dn_tmp_132.len}); });
dn_f_bytes_equal(dn_tmp_130,dn_tmp_131); })){
{ bool dn_tmp_134=1;bool *dn_tmp_133=&((v_o)->f_fake);*dn_tmp_133=dn_tmp_134; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_135=v_name;
dn_t_slice0_U8 dn_tmp_136=({ dn_utf8 dn_tmp_137=((dn_utf8){(const uint8_t*)"--http-manip", 12});
((dn_t_slice0_U8){dn_tmp_137.data,dn_tmp_137.len}); });
dn_f_bytes_equal(dn_tmp_135,dn_tmp_136); })){
{ bool dn_tmp_139=1;bool *dn_tmp_138=&((v_o)->f_http);*dn_tmp_138=dn_tmp_139; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_140=v_name;
dn_t_slice0_U8 dn_tmp_141=({ dn_utf8 dn_tmp_142=((dn_utf8){(const uint8_t*)"--reverse-frag", 14});
((dn_t_slice0_U8){dn_tmp_142.data,dn_tmp_142.len}); });
dn_f_bytes_equal(dn_tmp_140,dn_tmp_141); })){
{ bool dn_tmp_144=1;bool *dn_tmp_143=&((v_o)->f_reverse);*dn_tmp_143=dn_tmp_144; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_145=v_name;
dn_t_slice0_U8 dn_tmp_146=({ dn_utf8 dn_tmp_147=((dn_utf8){(const uint8_t*)"--block-quic", 12});
((dn_t_slice0_U8){dn_tmp_147.data,dn_tmp_147.len}); });
dn_f_bytes_equal(dn_tmp_145,dn_tmp_146); })){
{ uint32_t dn_tmp_149=((uint32_t)(2ULL));uint32_t *dn_tmp_148=&((v_o)->f_quic);*dn_tmp_148=dn_tmp_149; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_150=v_name;
dn_t_slice0_U8 dn_tmp_151=({ dn_utf8 dn_tmp_152=((dn_utf8){(const uint8_t*)"--all-ports", 11});
((dn_t_slice0_U8){dn_tmp_152.data,dn_tmp_152.len}); });
dn_f_bytes_equal(dn_tmp_150,dn_tmp_151); })){
{ bool dn_tmp_154=1;bool *dn_tmp_153=&((v_o)->f_all_ports);*dn_tmp_153=dn_tmp_154; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_155=v_name;
dn_t_slice0_U8 dn_tmp_156=({ dn_utf8 dn_tmp_157=((dn_utf8){(const uint8_t*)"--game-mode", 11});
((dn_t_slice0_U8){dn_tmp_157.data,dn_tmp_157.len}); });
dn_f_bytes_equal(dn_tmp_155,dn_tmp_156); })){
{ bool dn_tmp_159=1;bool *dn_tmp_158=&((v_o)->f_game);*dn_tmp_158=dn_tmp_159; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_160=v_name;
dn_t_slice0_U8 dn_tmp_161=({ dn_utf8 dn_tmp_162=((dn_utf8){(const uint8_t*)"--debug", 7});
((dn_t_slice0_U8){dn_tmp_162.data,dn_tmp_162.len}); });
dn_f_bytes_equal(dn_tmp_160,dn_tmp_161); })){
if(({ uint32_t dn_tmp_163=(v_o)->f_debug;
uint32_t dn_tmp_164=({ uint32_t dn_tmp_165=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_165>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_165; });
(dn_tmp_163 < dn_tmp_164); })){
{ uint32_t dn_tmp_167=((uint32_t)(1ULL));uint32_t *dn_tmp_166=&((v_o)->f_debug);*dn_tmp_166=dn_tmp_167; }
}
else{
}
}
else{
if(({ dn_t_slice0_U8 dn_tmp_168=v_name;
dn_t_slice0_U8 dn_tmp_169=({ dn_utf8 dn_tmp_170=((dn_utf8){(const uint8_t*)"--debug-verbose", 15});
((dn_t_slice0_U8){dn_tmp_170.data,dn_tmp_170.len}); });
dn_f_bytes_equal(dn_tmp_168,dn_tmp_169); })){
{ uint32_t dn_tmp_172=((uint32_t)(2ULL));uint32_t *dn_tmp_171=&((v_o)->f_debug);*dn_tmp_171=dn_tmp_172; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_173=v_name;
dn_t_slice0_U8 dn_tmp_174=({ dn_utf8 dn_tmp_175=((dn_utf8){(const uint8_t*)"--whitelist-only", 16});
((dn_t_slice0_U8){dn_tmp_175.data,dn_tmp_175.len}); });
dn_f_bytes_equal(dn_tmp_173,dn_tmp_174); })){
{ bool dn_tmp_177=1;bool *dn_tmp_176=&((v_o)->f_whitelist);*dn_tmp_176=dn_tmp_177; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_178=v_name;
dn_t_slice0_U8 dn_tmp_179=({ dn_utf8 dn_tmp_180=((dn_utf8){(const uint8_t*)"--auto-ttl", 10});
((dn_t_slice0_U8){dn_tmp_180.data,dn_tmp_180.len}); });
dn_f_bytes_equal(dn_tmp_178,dn_tmp_179); })){
{ uint32_t dn_tmp_182=((uint32_t)(2ULL));uint32_t *dn_tmp_181=&((v_o)->f_ttl_margin);*dn_tmp_181=dn_tmp_182; }
{ uint32_t dn_tmp_184=((uint32_t)(4ULL));uint32_t *dn_tmp_183=&((v_o)->f_ttl_spread);*dn_tmp_183=dn_tmp_184; }
{ uint32_t dn_tmp_186=((uint32_t)(16ULL));uint32_t *dn_tmp_185=&((v_o)->f_ttl_max);*dn_tmp_185=dn_tmp_186; }
}
else{
if((({ dn_t_slice0_U8 dn_tmp_187=v_name;
dn_t_slice0_U8 dn_tmp_188=({ dn_utf8 dn_tmp_189=((dn_utf8){(const uint8_t*)"--help", 6});
((dn_t_slice0_U8){dn_tmp_189.data,dn_tmp_189.len}); });
dn_f_bytes_equal(dn_tmp_187,dn_tmp_188); }) || ({ dn_t_slice0_U8 dn_tmp_190=v_name;
dn_t_slice0_U8 dn_tmp_191=({ dn_utf8 dn_tmp_192=((dn_utf8){(const uint8_t*)"-h", 2});
((dn_t_slice0_U8){dn_tmp_192.data,dn_tmp_192.len}); });
dn_f_bytes_equal(dn_tmp_190,dn_tmp_191); }))){
{ bool dn_tmp_194=1;bool *dn_tmp_193=&((v_o)->f_help);*dn_tmp_193=dn_tmp_194; }
}
else{
{ bool dn_tmp_195=0;return dn_tmp_195; }
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
{ bool dn_tmp_196=1;return dn_tmp_196; }
}
else{
}
dn_t_slice0_U8 v_value=({ dn_t_slice0_U8 const * dn_tmp_197=(&(v_arg));
size_t dn_tmp_198=({ size_t dn_tmp_199=v_eq;
size_t dn_tmp_200=((size_t)(1ULL));
dn_USize_add(dn_tmp_199,dn_tmp_200); });
size_t dn_tmp_201=({ size_t dn_tmp_202=({ size_t dn_tmp_203=({ dn_t_slice0_U8 dn_tmp_204=v_arg;
(dn_tmp_204).len; });
size_t dn_tmp_205=v_eq;
dn_USize_sub(dn_tmp_203,dn_tmp_205); });
size_t dn_tmp_206=((size_t)(1ULL));
dn_USize_sub(dn_tmp_202,dn_tmp_206); });
({ dn_range(dn_tmp_198,dn_tmp_201,(*(dn_tmp_197)).len);(dn_t_slice0_U8){(*(dn_tmp_197)).data?(*(dn_tmp_197)).data+dn_tmp_198:NULL,dn_tmp_201}; }); });
if(({ dn_t_slice0_U8 dn_tmp_207=v_name;
dn_t_slice0_U8 dn_tmp_208=({ dn_utf8 dn_tmp_209=((dn_utf8){(const uint8_t*)"--driver-report", 15});
((dn_t_slice0_U8){dn_tmp_209.data,dn_tmp_209.len}); });
dn_f_bytes_equal(dn_tmp_207,dn_tmp_208); })){
{ dn_t_buf_U8 dn_tmp_212=({ dn_t_slice0_U8 dn_tmp_210=v_value;
dn_f_bytes_copy(dn_tmp_210); });dn_adopt_buf_U8(&dn_tmp_212);dn_t_buf_U8 volatile *dn_tmp_211=&((v_o)->f_driver_report);dn_drop_buf_U8(dn_tmp_211);dn_retain_buf_U8(&dn_tmp_212);*dn_tmp_211=dn_tmp_212; }
{ bool dn_tmp_213=1;return dn_tmp_213; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_214=v_name;
dn_t_slice0_U8 dn_tmp_215=({ dn_utf8 dn_tmp_216=((dn_utf8){(const uint8_t*)"--trial-tag", 11});
((dn_t_slice0_U8){dn_tmp_216.data,dn_tmp_216.len}); });
dn_f_bytes_equal(dn_tmp_214,dn_tmp_215); })){
if(({ size_t dn_tmp_217=({ dn_t_slice0_U8 dn_tmp_218=v_value;
(dn_tmp_218).len; });
size_t dn_tmp_219=((size_t)(64ULL));
(dn_tmp_217 > dn_tmp_219); })){
{ bool dn_tmp_220=0;return dn_tmp_220; }
}
else{
}
{ dn_t_buf_U8 dn_tmp_223=({ dn_t_slice0_U8 dn_tmp_221=v_value;
dn_f_bytes_copy(dn_tmp_221); });dn_adopt_buf_U8(&dn_tmp_223);dn_t_buf_U8 volatile *dn_tmp_222=&((v_o)->f_trial_tag);dn_drop_buf_U8(dn_tmp_222);dn_retain_buf_U8(&dn_tmp_223);*dn_tmp_222=dn_tmp_223; }
{ bool dn_tmp_224=1;return dn_tmp_224; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_225=v_name;
dn_t_slice0_U8 dn_tmp_226=({ dn_utf8 dn_tmp_227=((dn_utf8){(const uint8_t*)"--gene", 6});
((dn_t_slice0_U8){dn_tmp_227.data,dn_tmp_227.len}); });
dn_f_bytes_equal(dn_tmp_225,dn_tmp_226); })){
{ bool dn_tmp_231=({ dn_t_slice0_U8 dn_tmp_228=v_value;
dn_r_Gene * dn_tmp_229=(&((v_o)->f_gene));
dn_f_gene_parse(dn_tmp_228,dn_tmp_229); });bool *dn_tmp_230=&((v_o)->f_gene_mode);*dn_tmp_230=dn_tmp_231; }
{ bool dn_tmp_232=(v_o)->f_gene_mode;return dn_tmp_232; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_233=v_name;
dn_t_slice0_U8 dn_tmp_234=({ dn_utf8 dn_tmp_235=((dn_utf8){(const uint8_t*)"--gene-target", 13});
((dn_t_slice0_U8){dn_tmp_235.data,dn_tmp_235.len}); });
dn_f_bytes_equal(dn_tmp_233,dn_tmp_234); })){
{ bool dn_tmp_239=({ dn_t_slice0_U8 dn_tmp_236=v_value;
uint32_t dn_tmp_237=((uint32_t)(6ULL));
uint32_t * dn_tmp_238=(&((v_o)->f_gene_target));
dn_f_decimal(dn_tmp_236,dn_tmp_237,dn_tmp_238); });return dn_tmp_239; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_240=v_name;
dn_t_slice0_U8 dn_tmp_241=({ dn_utf8 dn_tmp_242=((dn_utf8){(const uint8_t*)"--parent-pid", 12});
((dn_t_slice0_U8){dn_tmp_242.data,dn_tmp_242.len}); });
dn_f_bytes_equal(dn_tmp_240,dn_tmp_241); })){
{ bool dn_tmp_246=({ dn_t_slice0_U8 dn_tmp_243=v_value;
uint32_t dn_tmp_244=((uint32_t)(4294967295ULL));
uint32_t * dn_tmp_245=(&((v_o)->f_parent_pid));
dn_f_decimal(dn_tmp_243,dn_tmp_244,dn_tmp_245); });return dn_tmp_246; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_247=v_name;
dn_t_slice0_U8 dn_tmp_248=({ dn_utf8 dn_tmp_249=((dn_utf8){(const uint8_t*)"--strategy-profile", 18});
((dn_t_slice0_U8){dn_tmp_249.data,dn_tmp_249.len}); });
dn_f_bytes_equal(dn_tmp_247,dn_tmp_248); })){
if(({ dn_t_slice0_U8 dn_tmp_250=v_value;
dn_t_slice0_U8 dn_tmp_251=({ dn_utf8 dn_tmp_252=((dn_utf8){(const uint8_t*)"auto", 4});
((dn_t_slice0_U8){dn_tmp_252.data,dn_tmp_252.len}); });
dn_f_ascii_equal(dn_tmp_250,dn_tmp_251); })){
{ uint32_t dn_tmp_254=((uint32_t)(8ULL));uint32_t *dn_tmp_253=&((v_o)->f_forced_arm);*dn_tmp_253=dn_tmp_254; }
{ bool dn_tmp_256=1;bool *dn_tmp_255=&((v_o)->f_auto_strategy);*dn_tmp_255=dn_tmp_256; }
{ bool dn_tmp_257=1;return dn_tmp_257; }
}
else{
}
{ bool dn_tmp_261=({ dn_t_slice0_U8 dn_tmp_258=v_value;
uint32_t dn_tmp_259=((uint32_t)(7ULL));
uint32_t * dn_tmp_260=(&((v_o)->f_forced_arm));
dn_f_decimal(dn_tmp_258,dn_tmp_259,dn_tmp_260); });return dn_tmp_261; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_262=v_name;
dn_t_slice0_U8 dn_tmp_263=({ dn_utf8 dn_tmp_264=((dn_utf8){(const uint8_t*)"--handshake-timeout-ms", 22});
((dn_t_slice0_U8){dn_tmp_264.data,dn_tmp_264.len}); });
dn_f_bytes_equal(dn_tmp_262,dn_tmp_263); })){
{ bool dn_tmp_271=(({ dn_t_slice0_U8 dn_tmp_265=v_value;
uint32_t dn_tmp_266=((uint32_t)(30000ULL));
uint32_t * dn_tmp_267=(&((v_o)->f_handshake_timeout));
dn_f_decimal(dn_tmp_265,dn_tmp_266,dn_tmp_267); }) && ({ uint32_t dn_tmp_268=(v_o)->f_handshake_timeout;
uint32_t dn_tmp_269=({ uint32_t dn_tmp_270=((uint32_t)(1000ULL));
if((unsigned __int128)dn_tmp_270>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_270; });
(dn_tmp_268 >= dn_tmp_269); }));return dn_tmp_271; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_272=v_name;
dn_t_slice0_U8 dn_tmp_273=({ dn_utf8 dn_tmp_274=((dn_utf8){(const uint8_t*)"--quic-mode", 11});
((dn_t_slice0_U8){dn_tmp_274.data,dn_tmp_274.len}); });
dn_f_bytes_equal(dn_tmp_272,dn_tmp_273); })){
if((({ dn_t_slice0_U8 dn_tmp_275=v_value;
size_t dn_tmp_276=((size_t)(0ULL));
dn_t_slice0_U8 dn_tmp_277=({ dn_utf8 dn_tmp_278=((dn_utf8){(const uint8_t*)"pass", 4});
((dn_t_slice0_U8){dn_tmp_278.data,dn_tmp_278.len}); });
dn_f_ascii_at(dn_tmp_275,dn_tmp_276,dn_tmp_277); }) && ({ size_t dn_tmp_279=({ dn_t_slice0_U8 dn_tmp_280=v_value;
(dn_tmp_280).len; });
size_t dn_tmp_281=((size_t)(4ULL));
(dn_tmp_279 == dn_tmp_281); }))){
{ uint32_t dn_tmp_283=((uint32_t)(0ULL));uint32_t *dn_tmp_282=&((v_o)->f_quic);*dn_tmp_282=dn_tmp_283; }
}
else{
if((({ dn_t_slice0_U8 dn_tmp_284=v_value;
size_t dn_tmp_285=((size_t)(0ULL));
dn_t_slice0_U8 dn_tmp_286=({ dn_utf8 dn_tmp_287=((dn_utf8){(const uint8_t*)"auto", 4});
((dn_t_slice0_U8){dn_tmp_287.data,dn_tmp_287.len}); });
dn_f_ascii_at(dn_tmp_284,dn_tmp_285,dn_tmp_286); }) && ({ size_t dn_tmp_288=({ dn_t_slice0_U8 dn_tmp_289=v_value;
(dn_tmp_289).len; });
size_t dn_tmp_290=((size_t)(4ULL));
(dn_tmp_288 == dn_tmp_290); }))){
{ uint32_t dn_tmp_292=((uint32_t)(1ULL));uint32_t *dn_tmp_291=&((v_o)->f_quic);*dn_tmp_291=dn_tmp_292; }
}
else{
if((({ dn_t_slice0_U8 dn_tmp_293=v_value;
size_t dn_tmp_294=((size_t)(0ULL));
dn_t_slice0_U8 dn_tmp_295=({ dn_utf8 dn_tmp_296=((dn_utf8){(const uint8_t*)"drop", 4});
((dn_t_slice0_U8){dn_tmp_296.data,dn_tmp_296.len}); });
dn_f_ascii_at(dn_tmp_293,dn_tmp_294,dn_tmp_295); }) && ({ size_t dn_tmp_297=({ dn_t_slice0_U8 dn_tmp_298=v_value;
(dn_tmp_298).len; });
size_t dn_tmp_299=((size_t)(4ULL));
(dn_tmp_297 == dn_tmp_299); }))){
{ uint32_t dn_tmp_301=((uint32_t)(2ULL));uint32_t *dn_tmp_300=&((v_o)->f_quic);*dn_tmp_300=dn_tmp_301; }
}
else{
if((({ dn_t_slice0_U8 dn_tmp_302=v_value;
size_t dn_tmp_303=((size_t)(0ULL));
dn_t_slice0_U8 dn_tmp_304=({ dn_utf8 dn_tmp_305=((dn_utf8){(const uint8_t*)"ipfrag", 6});
((dn_t_slice0_U8){dn_tmp_305.data,dn_tmp_305.len}); });
dn_f_ascii_at(dn_tmp_302,dn_tmp_303,dn_tmp_304); }) && ({ size_t dn_tmp_306=({ dn_t_slice0_U8 dn_tmp_307=v_value;
(dn_tmp_307).len; });
size_t dn_tmp_308=((size_t)(6ULL));
(dn_tmp_306 == dn_tmp_308); }))){
{ uint32_t dn_tmp_310=((uint32_t)(3ULL));uint32_t *dn_tmp_309=&((v_o)->f_quic);*dn_tmp_309=dn_tmp_310; }
}
else{
{ bool dn_tmp_311=0;return dn_tmp_311; }
}
}
}
}
}
else{
if(({ dn_t_slice0_U8 dn_tmp_312=v_name;
dn_t_slice0_U8 dn_tmp_313=({ dn_utf8 dn_tmp_314=((dn_utf8){(const uint8_t*)"--split-mode", 12});
((dn_t_slice0_U8){dn_tmp_314.data,dn_tmp_314.len}); });
dn_f_bytes_equal(dn_tmp_312,dn_tmp_313); })){
if(({ dn_t_slice0_U8 dn_tmp_315=v_value;
dn_t_slice0_U8 dn_tmp_316=({ dn_utf8 dn_tmp_317=((dn_utf8){(const uint8_t*)"tcp", 3});
((dn_t_slice0_U8){dn_tmp_317.data,dn_tmp_317.len}); });
dn_f_ascii_equal(dn_tmp_315,dn_tmp_316); })){
{ uint32_t dn_tmp_319=((uint32_t)(0ULL));uint32_t *dn_tmp_318=&((v_o)->f_split_mode);*dn_tmp_318=dn_tmp_319; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_320=v_value;
dn_t_slice0_U8 dn_tmp_321=({ dn_utf8 dn_tmp_322=((dn_utf8){(const uint8_t*)"ip", 2});
((dn_t_slice0_U8){dn_tmp_322.data,dn_tmp_322.len}); });
dn_f_ascii_equal(dn_tmp_320,dn_tmp_321); })){
{ uint32_t dn_tmp_324=((uint32_t)(1ULL));uint32_t *dn_tmp_323=&((v_o)->f_split_mode);*dn_tmp_323=dn_tmp_324; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_325=v_value;
dn_t_slice0_U8 dn_tmp_326=({ dn_utf8 dn_tmp_327=((dn_utf8){(const uint8_t*)"both", 4});
((dn_t_slice0_U8){dn_tmp_327.data,dn_tmp_327.len}); });
dn_f_ascii_equal(dn_tmp_325,dn_tmp_326); })){
{ uint32_t dn_tmp_329=((uint32_t)(2ULL));uint32_t *dn_tmp_328=&((v_o)->f_split_mode);*dn_tmp_328=dn_tmp_329; }
}
else{
{ bool dn_tmp_330=0;return dn_tmp_330; }
}
}
}
}
else{
if(({ dn_t_slice0_U8 dn_tmp_331=v_name;
dn_t_slice0_U8 dn_tmp_332=({ dn_utf8 dn_tmp_333=((dn_utf8){(const uint8_t*)"--split-strategy", 16});
((dn_t_slice0_U8){dn_tmp_333.data,dn_tmp_333.len}); });
dn_f_bytes_equal(dn_tmp_331,dn_tmp_332); })){
if(({ dn_t_slice0_U8 dn_tmp_334=v_value;
dn_t_slice0_U8 dn_tmp_335=({ dn_utf8 dn_tmp_336=((dn_utf8){(const uint8_t*)"semantic", 8});
((dn_t_slice0_U8){dn_tmp_336.data,dn_tmp_336.len}); });
dn_f_ascii_equal(dn_tmp_334,dn_tmp_335); })){
{ uint32_t dn_tmp_338=((uint32_t)(0ULL));uint32_t *dn_tmp_337=&((v_o)->f_strategy);*dn_tmp_337=dn_tmp_338; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_339=v_value;
dn_t_slice0_U8 dn_tmp_340=({ dn_utf8 dn_tmp_341=((dn_utf8){(const uint8_t*)"semantic-multi", 14});
((dn_t_slice0_U8){dn_tmp_341.data,dn_tmp_341.len}); });
dn_f_ascii_equal(dn_tmp_339,dn_tmp_340); })){
{ uint32_t dn_tmp_343=((uint32_t)(1ULL));uint32_t *dn_tmp_342=&((v_o)->f_strategy);*dn_tmp_342=dn_tmp_343; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_344=v_value;
dn_t_slice0_U8 dn_tmp_345=({ dn_utf8 dn_tmp_346=((dn_utf8){(const uint8_t*)"legacy", 6});
((dn_t_slice0_U8){dn_tmp_346.data,dn_tmp_346.len}); });
dn_f_ascii_equal(dn_tmp_344,dn_tmp_345); })){
{ uint32_t dn_tmp_348=((uint32_t)(2ULL));uint32_t *dn_tmp_347=&((v_o)->f_strategy);*dn_tmp_347=dn_tmp_348; }
}
else{
{ bool dn_tmp_349=0;return dn_tmp_349; }
}
}
}
}
else{
if(({ dn_t_slice0_U8 dn_tmp_350=v_name;
dn_t_slice0_U8 dn_tmp_351=({ dn_utf8 dn_tmp_352=((dn_utf8){(const uint8_t*)"--fake-mode", 11});
((dn_t_slice0_U8){dn_tmp_352.data,dn_tmp_352.len}); });
dn_f_bytes_equal(dn_tmp_350,dn_tmp_351); })){
if(({ dn_t_slice0_U8 dn_tmp_353=v_value;
dn_t_slice0_U8 dn_tmp_354=({ dn_utf8 dn_tmp_355=((dn_utf8){(const uint8_t*)"tls-badseq", 10});
((dn_t_slice0_U8){dn_tmp_355.data,dn_tmp_355.len}); });
dn_f_ascii_equal(dn_tmp_353,dn_tmp_354); })){
{ uint32_t dn_tmp_357=((uint32_t)(0ULL));uint32_t *dn_tmp_356=&((v_o)->f_fake_mode);*dn_tmp_356=dn_tmp_357; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_358=v_value;
dn_t_slice0_U8 dn_tmp_359=({ dn_utf8 dn_tmp_360=((dn_utf8){(const uint8_t*)"rst-ttl", 7});
((dn_t_slice0_U8){dn_tmp_360.data,dn_tmp_360.len}); });
dn_f_ascii_equal(dn_tmp_358,dn_tmp_359); })){
{ uint32_t dn_tmp_362=((uint32_t)(1ULL));uint32_t *dn_tmp_361=&((v_o)->f_fake_mode);*dn_tmp_361=dn_tmp_362; }
}
else{
{ bool dn_tmp_363=0;return dn_tmp_363; }
}
}
}
else{
if(({ dn_t_slice0_U8 dn_tmp_364=v_name;
dn_t_slice0_U8 dn_tmp_365=({ dn_utf8 dn_tmp_366=((dn_utf8){(const uint8_t*)"--first-packets", 15});
((dn_t_slice0_U8){dn_tmp_366.data,dn_tmp_366.len}); });
dn_f_bytes_equal(dn_tmp_364,dn_tmp_365); })){
{ bool dn_tmp_370=({ dn_t_slice0_U8 dn_tmp_367=v_value;
uint32_t dn_tmp_368=((uint32_t)(1024ULL));
uint32_t * dn_tmp_369=(&((v_o)->f_first_packets));
dn_f_decimal(dn_tmp_367,dn_tmp_368,dn_tmp_369); });return dn_tmp_370; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_371=v_name;
dn_t_slice0_U8 dn_tmp_372=({ dn_utf8 dn_tmp_373=((dn_utf8){(const uint8_t*)"--handshake-jitter-ms", 21});
((dn_t_slice0_U8){dn_tmp_373.data,dn_tmp_373.len}); });
dn_f_bytes_equal(dn_tmp_371,dn_tmp_372); })){
{ bool dn_tmp_377=({ dn_t_slice0_U8 dn_tmp_374=v_value;
uint32_t dn_tmp_375=((uint32_t)(250ULL));
uint32_t * dn_tmp_376=(&((v_o)->f_jitter_ms));
dn_f_decimal(dn_tmp_374,dn_tmp_375,dn_tmp_376); });return dn_tmp_377; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_378=v_name;
dn_t_slice0_U8 dn_tmp_379=({ dn_utf8 dn_tmp_380=((dn_utf8){(const uint8_t*)"--quic-probe-ms", 15});
((dn_t_slice0_U8){dn_tmp_380.data,dn_tmp_380.len}); });
dn_f_bytes_equal(dn_tmp_378,dn_tmp_379); })){
{ bool dn_tmp_387=(({ dn_t_slice0_U8 dn_tmp_381=v_value;
uint32_t dn_tmp_382=((uint32_t)(5000ULL));
uint32_t * dn_tmp_383=(&((v_o)->f_probe_ms));
dn_f_decimal(dn_tmp_381,dn_tmp_382,dn_tmp_383); }) && ({ uint32_t dn_tmp_384=(v_o)->f_probe_ms;
uint32_t dn_tmp_385=({ uint32_t dn_tmp_386=((uint32_t)(200ULL));
if((unsigned __int128)dn_tmp_386>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_386; });
(dn_tmp_384 >= dn_tmp_385); }));return dn_tmp_387; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_388=v_name;
dn_t_slice0_U8 dn_tmp_389=({ dn_utf8 dn_tmp_390=((dn_utf8){(const uint8_t*)"--min-ttl", 9});
((dn_t_slice0_U8){dn_tmp_390.data,dn_tmp_390.len}); });
dn_f_bytes_equal(dn_tmp_388,dn_tmp_389); })){
{ bool dn_tmp_394=({ dn_t_slice0_U8 dn_tmp_391=v_value;
uint32_t dn_tmp_392=((uint32_t)(255ULL));
uint32_t * dn_tmp_393=(&((v_o)->f_min_hops));
dn_f_decimal(dn_tmp_391,dn_tmp_392,dn_tmp_393); });return dn_tmp_394; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_395=v_name;
dn_t_slice0_U8 dn_tmp_396=({ dn_utf8 dn_tmp_397=((dn_utf8){(const uint8_t*)"--auto-ttl", 10});
((dn_t_slice0_U8){dn_tmp_397.data,dn_tmp_397.len}); });
dn_f_bytes_equal(dn_tmp_395,dn_tmp_396); })){
{ bool dn_tmp_400=({ dn_r_Options * dn_tmp_398=v_o;
dn_t_slice0_U8 dn_tmp_399=v_value;
dn_f_auto_ttl(dn_tmp_398,dn_tmp_399); });return dn_tmp_400; }
}
else{
size_t v_index=((size_t)(8ULL));
if(({ dn_t_slice0_U8 dn_tmp_401=v_name;
dn_t_slice0_U8 dn_tmp_402=({ dn_utf8 dn_tmp_403=((dn_utf8){(const uint8_t*)"--hostlist", 10});
((dn_t_slice0_U8){dn_tmp_403.data,dn_tmp_403.len}); });
dn_f_bytes_equal(dn_tmp_401,dn_tmp_402); })){
{ size_t dn_tmp_405=((size_t)(0ULL));size_t *dn_tmp_404=&(v_index);*dn_tmp_404=dn_tmp_405; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_406=v_name;
dn_t_slice0_U8 dn_tmp_407=({ dn_utf8 dn_tmp_408=((dn_utf8){(const uint8_t*)"--blacklist", 11});
((dn_t_slice0_U8){dn_tmp_408.data,dn_tmp_408.len}); });
dn_f_bytes_equal(dn_tmp_406,dn_tmp_407); })){
{ size_t dn_tmp_410=((size_t)(6ULL));size_t *dn_tmp_409=&(v_index);*dn_tmp_409=dn_tmp_410; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_411=v_name;
dn_t_slice0_U8 dn_tmp_412=({ dn_utf8 dn_tmp_413=((dn_utf8){(const uint8_t*)"--hostlist-exclude", 18});
((dn_t_slice0_U8){dn_tmp_413.data,dn_tmp_413.len}); });
dn_f_bytes_equal(dn_tmp_411,dn_tmp_412); })){
{ size_t dn_tmp_415=((size_t)(1ULL));size_t *dn_tmp_414=&(v_index);*dn_tmp_414=dn_tmp_415; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_416=v_name;
dn_t_slice0_U8 dn_tmp_417=({ dn_utf8 dn_tmp_418=((dn_utf8){(const uint8_t*)"--ipset", 7});
((dn_t_slice0_U8){dn_tmp_418.data,dn_tmp_418.len}); });
dn_f_bytes_equal(dn_tmp_416,dn_tmp_417); })){
{ size_t dn_tmp_420=((size_t)(2ULL));size_t *dn_tmp_419=&(v_index);*dn_tmp_419=dn_tmp_420; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_421=v_name;
dn_t_slice0_U8 dn_tmp_422=({ dn_utf8 dn_tmp_423=((dn_utf8){(const uint8_t*)"--ipset-exclude", 15});
((dn_t_slice0_U8){dn_tmp_423.data,dn_tmp_423.len}); });
dn_f_bytes_equal(dn_tmp_421,dn_tmp_422); })){
{ size_t dn_tmp_425=((size_t)(3ULL));size_t *dn_tmp_424=&(v_index);*dn_tmp_424=dn_tmp_425; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_426=v_name;
dn_t_slice0_U8 dn_tmp_427=({ dn_utf8 dn_tmp_428=((dn_utf8){(const uint8_t*)"--aws-json", 10});
((dn_t_slice0_U8){dn_tmp_428.data,dn_tmp_428.len}); });
dn_f_bytes_equal(dn_tmp_426,dn_tmp_427); })){
{ size_t dn_tmp_430=((size_t)(4ULL));size_t *dn_tmp_429=&(v_index);*dn_tmp_429=dn_tmp_430; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_431=v_name;
dn_t_slice0_U8 dn_tmp_432=({ dn_utf8 dn_tmp_433=((dn_utf8){(const uint8_t*)"--aws-services", 14});
((dn_t_slice0_U8){dn_tmp_433.data,dn_tmp_433.len}); });
dn_f_bytes_equal(dn_tmp_431,dn_tmp_432); })){
{ size_t dn_tmp_435=((size_t)(5ULL));size_t *dn_tmp_434=&(v_index);*dn_tmp_434=dn_tmp_435; }
}
else{
if(({ dn_t_slice0_U8 dn_tmp_436=v_name;
dn_t_slice0_U8 dn_tmp_437=({ dn_utf8 dn_tmp_438=((dn_utf8){(const uint8_t*)"--strategy-file", 15});
((dn_t_slice0_U8){dn_tmp_438.data,dn_tmp_438.len}); });
dn_f_bytes_equal(dn_tmp_436,dn_tmp_437); })){
{ size_t dn_tmp_440=((size_t)(7ULL));size_t *dn_tmp_439=&(v_index);*dn_tmp_439=dn_tmp_440; }
}
else{
}
}
}
}
}
}
}
}
if(({ size_t dn_tmp_441=v_index;
size_t dn_tmp_442=((size_t)(8ULL));
(dn_tmp_441 == dn_tmp_442); })){
{ bool dn_tmp_443=0;return dn_tmp_443; }
}
else{
}
{ dn_t_buf_U8 dn_tmp_448=({ dn_t_slice0_U8 dn_tmp_446=v_value;
dn_f_bytes_copy(dn_tmp_446); });dn_adopt_buf_U8(&dn_tmp_448);dn_t_buf_U8 volatile *dn_tmp_447=&((*({ dn_t_buf_buf_U8 volatile *dn_tmp_444=&((v_o)->f_paths);size_t dn_tmp_445=v_index;&dn_tmp_444->data[dn_index(dn_tmp_445,dn_tmp_444->len)]; })));dn_drop_buf_U8(dn_tmp_447);dn_retain_buf_U8(&dn_tmp_448);*dn_tmp_447=dn_tmp_448; }
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
}
{ bool dn_tmp_449=1;return dn_tmp_449; }
}

dn_fail("missing native return");
}
static bool dn_cb_option(dn_r_Options * v_o,dn_t_slice0_U8 v_arg) {
 if(!dn_enter()){return (bool){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (bool){0};}
 bool dn_result=dn_f_option(v_o,v_arg);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_automatic_options(dn_r_Options * v_o) {
{
{ bool dn_tmp_1=1;bool *dn_tmp_0=&((v_o)->f_safe_automatic);*dn_tmp_0=dn_tmp_1; }
{ bool dn_tmp_3=1;bool *dn_tmp_2=&((v_o)->f_auto_strategy);*dn_tmp_2=dn_tmp_3; }
{ uint32_t dn_tmp_5=((uint32_t)(8ULL));uint32_t *dn_tmp_4=&((v_o)->f_forced_arm);*dn_tmp_4=dn_tmp_5; }
{ bool dn_tmp_7=0;bool *dn_tmp_6=&((v_o)->f_server_window);*dn_tmp_6=dn_tmp_7; }
{ bool dn_tmp_9=0;bool *dn_tmp_8=&((v_o)->f_udp_obfuscate);*dn_tmp_8=dn_tmp_9; }
{ uint32_t dn_tmp_11=((uint32_t)(1ULL));uint32_t *dn_tmp_10=&((v_o)->f_quic);*dn_tmp_10=dn_tmp_11; }
{ bool dn_tmp_13=1;bool *dn_tmp_12=&((v_o)->f_aggressive);*dn_tmp_12=dn_tmp_13; }
{ bool dn_tmp_15=1;bool *dn_tmp_14=&((v_o)->f_sni);*dn_tmp_14=dn_tmp_15; }
{ bool dn_tmp_17=1;bool *dn_tmp_16=&((v_o)->f_ech);*dn_tmp_16=dn_tmp_17; }
{ bool dn_tmp_19=1;bool *dn_tmp_18=&((v_o)->f_adaptive);*dn_tmp_18=dn_tmp_19; }
{ bool dn_tmp_21=1;bool *dn_tmp_20=&((v_o)->f_learn);*dn_tmp_20=dn_tmp_21; }
{ bool dn_tmp_23=0;bool *dn_tmp_22=&((v_o)->f_fake);*dn_tmp_22=dn_tmp_23; }
{ bool dn_tmp_25=0;bool *dn_tmp_24=&((v_o)->f_http);*dn_tmp_24=dn_tmp_25; }
{ bool dn_tmp_27=0;bool *dn_tmp_26=&((v_o)->f_reverse);*dn_tmp_26=dn_tmp_27; }
{ bool dn_tmp_29=0;bool *dn_tmp_28=&((v_o)->f_all_ports);*dn_tmp_28=dn_tmp_29; }
{ bool dn_tmp_31=0;bool *dn_tmp_30=&((v_o)->f_game);*dn_tmp_30=dn_tmp_31; }
{ bool dn_tmp_33=0;bool *dn_tmp_32=&((v_o)->f_whitelist);*dn_tmp_32=dn_tmp_33; }
{ uint32_t dn_tmp_35=((uint32_t)(0ULL));uint32_t *dn_tmp_34=&((v_o)->f_split_mode);*dn_tmp_34=dn_tmp_35; }
{ uint32_t dn_tmp_37=((uint32_t)(0ULL));uint32_t *dn_tmp_36=&((v_o)->f_strategy);*dn_tmp_36=dn_tmp_37; }
{ uint32_t dn_tmp_39=((uint32_t)(3ULL));uint32_t *dn_tmp_38=&((v_o)->f_first_packets);*dn_tmp_38=dn_tmp_39; }
{ uint32_t dn_tmp_41=((uint32_t)(0ULL));uint32_t *dn_tmp_40=&((v_o)->f_jitter_ms);*dn_tmp_40=dn_tmp_41; }
{ uint32_t dn_tmp_43=((uint32_t)(0ULL));uint32_t *dn_tmp_42=&((v_o)->f_ttl_margin);*dn_tmp_42=dn_tmp_43; }
{ uint32_t dn_tmp_45=((uint32_t)(0ULL));uint32_t *dn_tmp_44=&((v_o)->f_ttl_spread);*dn_tmp_44=dn_tmp_45; }
{ uint32_t dn_tmp_47=((uint32_t)(0ULL));uint32_t *dn_tmp_46=&((v_o)->f_ttl_max);*dn_tmp_46=dn_tmp_47; }
{ uint32_t dn_tmp_49=((uint32_t)(0ULL));uint32_t *dn_tmp_48=&((v_o)->f_min_hops);*dn_tmp_48=dn_tmp_49; }
{ bool dn_tmp_51=1;bool *dn_tmp_50=&((v_o)->f_diagnostic);*dn_tmp_50=dn_tmp_51; }
{ uint32_t dn_tmp_53=((uint32_t)(1ULL));uint32_t *dn_tmp_52=&((v_o)->f_debug);*dn_tmp_52=dn_tmp_53; }
{ uint32_t dn_tmp_55=((uint32_t)(6000ULL));uint32_t *dn_tmp_54=&((v_o)->f_handshake_timeout);*dn_tmp_54=dn_tmp_55; }
if((v_o)->f_gene_mode){
{ bool dn_tmp_57=0;bool *dn_tmp_56=&((v_o)->f_adaptive);*dn_tmp_56=dn_tmp_57; }
}
else{
}
}

return 0;
}
static void dn_cb_automatic_options(dn_r_Options * v_o) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_automatic_options(v_o);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static uint8_t dn_f_finish_options(dn_r_Options * v_o) {
{
if((v_o)->f_safe_automatic){
(void)(({ dn_r_Options * dn_tmp_0=v_o;
dn_f_automatic_options(dn_tmp_0); }));
}
else{
}
if((({ size_t dn_tmp_1=({ dn_t_buf_U8 const * dn_tmp_2=(&((*({ dn_t_buf_buf_U8 volatile *dn_tmp_3=&((v_o)->f_paths);size_t dn_tmp_4=((size_t)(0ULL));&dn_tmp_3->data[dn_index(dn_tmp_4,dn_tmp_3->len)]; }))));
(*(dn_tmp_2)).len; });
size_t dn_tmp_5=((size_t)(0ULL));
(dn_tmp_1 == dn_tmp_5); }) && ({ size_t dn_tmp_6=({ dn_t_buf_U8 const * dn_tmp_7=(&((*({ dn_t_buf_buf_U8 volatile *dn_tmp_8=&((v_o)->f_paths);size_t dn_tmp_9=((size_t)(6ULL));&dn_tmp_8->data[dn_index(dn_tmp_9,dn_tmp_8->len)]; }))));
(*(dn_tmp_7)).len; });
size_t dn_tmp_10=((size_t)(0ULL));
(dn_tmp_6 > dn_tmp_10); }))){
dn_t_buf_U8 volatile v_fallback={0};
dn_boundary dn_tmp_11={0};jmp_buf dn_tmp_12;dn_tmp_11.jump=&dn_tmp_12;dn_tmp_11.previous=dn_current;dn_tmp_11.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_11;if(setjmp(dn_tmp_12)){dn_current=dn_tmp_11.previous;dn_drop_buf_U8(&v_fallback);
dn_boundary_end(&dn_tmp_11,1);dn_raise();}
v_fallback=({ dn_t_slice0_U8 dn_tmp_13=({ dn_t_buf_U8 const * dn_tmp_14=(&((*({ dn_t_buf_buf_U8 volatile *dn_tmp_15=&((v_o)->f_paths);size_t dn_tmp_16=((size_t)(6ULL));&dn_tmp_15->data[dn_index(dn_tmp_16,dn_tmp_15->len)]; }))));
size_t dn_tmp_17=((size_t)(0ULL));
size_t dn_tmp_18=({ dn_t_buf_U8 const * dn_tmp_19=(&((*({ dn_t_buf_buf_U8 volatile *dn_tmp_20=&((v_o)->f_paths);size_t dn_tmp_21=((size_t)(6ULL));&dn_tmp_20->data[dn_index(dn_tmp_21,dn_tmp_20->len)]; }))));
(*(dn_tmp_19)).len; });
({ dn_range(dn_tmp_17,dn_tmp_18,(*(dn_tmp_14)).len);(dn_t_slice0_U8){(*(dn_tmp_14)).data?(*(dn_tmp_14)).data+dn_tmp_17:NULL,dn_tmp_18}; }); });
dn_f_bytes_copy(dn_tmp_13); });
{ dn_t_buf_U8 dn_tmp_26=({ dn_t_buf_U8 dn_tmp_24=v_fallback;v_fallback=(dn_t_buf_U8){0};dn_adopt_buf_U8(&dn_tmp_24);dn_tmp_24; });dn_adopt_buf_U8(&dn_tmp_26);dn_t_buf_U8 volatile *dn_tmp_25=&((*({ dn_t_buf_buf_U8 volatile *dn_tmp_22=&((v_o)->f_paths);size_t dn_tmp_23=((size_t)(0ULL));&dn_tmp_22->data[dn_index(dn_tmp_23,dn_tmp_22->len)]; })));dn_drop_buf_U8(dn_tmp_25);dn_retain_buf_U8(&dn_tmp_26);*dn_tmp_25=dn_tmp_26; }
dn_drop_buf_U8(&v_fallback);
dn_boundary_end(&dn_tmp_11,0);
}
else{
}
if((v_o)->f_game){
{ uint32_t dn_tmp_28=((uint32_t)(0ULL));uint32_t *dn_tmp_27=&((v_o)->f_quic);*dn_tmp_27=dn_tmp_28; }
{ bool dn_tmp_30=0;bool *dn_tmp_29=&((v_o)->f_http);*dn_tmp_29=dn_tmp_30; }
}
else{
}
}

return 0;
}
static void dn_cb_finish_options(dn_r_Options * v_o) {
 if(!dn_enter()){return;} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return;}
 (void)dn_f_finish_options(v_o);dn_boundary_end(&dn_b,0);dn_leave();return;
}
static dn_t_buf_U8 dn_f_divert_filter(dn_r_Options const * v_o) {
{
dn_t_buf_U8 volatile v_b={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
v_b=({ dn_t_slice0_U8 dn_tmp_2=({ dn_utf8 dn_tmp_3=((dn_utf8){(const uint8_t*)"not loopback and not impostor and (ip or ipv6) and ((outbound and tcp", 69});
((dn_t_slice0_U8){dn_tmp_3.data,dn_tmp_3.len}); });
dn_f_bytes_copy(dn_tmp_2); });
if((!((v_o)->f_all_ports))){
if((v_o)->f_telegram){
(void)(({ dn_t_buf_U8 * dn_tmp_4=(&(v_b));
dn_t_slice0_U8 dn_tmp_5=({ dn_utf8 dn_tmp_6=((dn_utf8){(const uint8_t*)" and (tcp.DstPort == 80 or tcp.DstPort == 443 or tcp.DstPort == 5222)", 69});
((dn_t_slice0_U8){dn_tmp_6.data,dn_tmp_6.len}); });
dn_f_bytes_append(dn_tmp_4,dn_tmp_5); }));
}
else{
(void)(({ dn_t_buf_U8 * dn_tmp_7=(&(v_b));
dn_t_slice0_U8 dn_tmp_8=({ dn_utf8 dn_tmp_9=((dn_utf8){(const uint8_t*)" and (tcp.DstPort == 80 or tcp.DstPort == 443)", 46});
((dn_t_slice0_U8){dn_tmp_9.data,dn_tmp_9.len}); });
dn_f_bytes_append(dn_tmp_7,dn_tmp_8); }));
}
}
else{
}
(void)(({ dn_t_buf_U8 * dn_tmp_10=(&(v_b));
dn_t_slice0_U8 dn_tmp_11=({ dn_utf8 dn_tmp_12=((dn_utf8){(const uint8_t*)" and (tcp.PayloadLength > 0 or tcp.Syn or tcp.Fin or tcp.Rst)) or (inbound and tcp", 82});
((dn_t_slice0_U8){dn_tmp_12.data,dn_tmp_12.len}); });
dn_f_bytes_append(dn_tmp_10,dn_tmp_11); }));
if((!((v_o)->f_all_ports))){
if((v_o)->f_telegram){
(void)(({ dn_t_buf_U8 * dn_tmp_13=(&(v_b));
dn_t_slice0_U8 dn_tmp_14=({ dn_utf8 dn_tmp_15=((dn_utf8){(const uint8_t*)" and (tcp.SrcPort == 80 or tcp.SrcPort == 443 or tcp.SrcPort == 5222)", 69});
((dn_t_slice0_U8){dn_tmp_15.data,dn_tmp_15.len}); });
dn_f_bytes_append(dn_tmp_13,dn_tmp_14); }));
}
else{
(void)(({ dn_t_buf_U8 * dn_tmp_16=(&(v_b));
dn_t_slice0_U8 dn_tmp_17=({ dn_utf8 dn_tmp_18=((dn_utf8){(const uint8_t*)" and (tcp.SrcPort == 80 or tcp.SrcPort == 443)", 46});
((dn_t_slice0_U8){dn_tmp_18.data,dn_tmp_18.len}); });
dn_f_bytes_append(dn_tmp_16,dn_tmp_17); }));
}
}
else{
}
(void)(({ dn_t_buf_U8 * dn_tmp_19=(&(v_b));
dn_t_slice0_U8 dn_tmp_20=({ dn_utf8 dn_tmp_21=((dn_utf8){(const uint8_t*)")", 1});
((dn_t_slice0_U8){dn_tmp_21.data,dn_tmp_21.len}); });
dn_f_bytes_append(dn_tmp_19,dn_tmp_20); }));
if(((!((v_o)->f_game)) && ({ uint32_t dn_tmp_22=(v_o)->f_quic;
uint32_t dn_tmp_23=({ uint32_t dn_tmp_24=((uint32_t)(0ULL));
if((unsigned __int128)dn_tmp_24>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_24; });
(dn_tmp_22 != dn_tmp_23); }))){
(void)(({ dn_t_buf_U8 * dn_tmp_25=(&(v_b));
dn_t_slice0_U8 dn_tmp_26=({ dn_utf8 dn_tmp_27=((dn_utf8){(const uint8_t*)" or (outbound and udp and udp.DstPort == 443)", 45});
((dn_t_slice0_U8){dn_tmp_27.data,dn_tmp_27.len}); });
dn_f_bytes_append(dn_tmp_25,dn_tmp_26); }));
if(({ uint32_t dn_tmp_28=(v_o)->f_quic;
uint32_t dn_tmp_29=({ uint32_t dn_tmp_30=((uint32_t)(1ULL));
if((unsigned __int128)dn_tmp_30>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_30; });
(dn_tmp_28 == dn_tmp_29); })){
(void)(({ dn_t_buf_U8 * dn_tmp_31=(&(v_b));
dn_t_slice0_U8 dn_tmp_32=({ dn_utf8 dn_tmp_33=((dn_utf8){(const uint8_t*)" or (inbound and udp and udp.SrcPort == 443 and udp.PayloadLength >= 7 and udp.Payload[0] >= 0x80)", 98});
((dn_t_slice0_U8){dn_tmp_33.data,dn_tmp_33.len}); });
dn_f_bytes_append(dn_tmp_31,dn_tmp_32); }));
}
else{
}
}
else{
}
if(((v_o)->f_udp_obfuscate && (!((v_o)->f_game)))){
(void)(({ dn_t_buf_U8 * dn_tmp_34=(&(v_b));
dn_t_slice0_U8 dn_tmp_35=({ dn_utf8 dn_tmp_36=((dn_utf8){(const uint8_t*)" or (outbound and udp and (udp.DstPort == 3478 or udp.DstPort == 5349 or udp.DstPort >= 50000)) or (inbound and udp and (udp.SrcPort == 3478 or udp.SrcPort == 5349 or udp.SrcPort >= 50000))", 189});
((dn_t_slice0_U8){dn_tmp_36.data,dn_tmp_36.len}); });
dn_f_bytes_append(dn_tmp_34,dn_tmp_35); }));
}
else{
}
(void)(({ dn_t_buf_U8 * dn_tmp_37=(&(v_b));
dn_t_slice0_U8 dn_tmp_38=({ dn_utf8 dn_tmp_39=((dn_utf8){(const uint8_t*)")", 1});
((dn_t_slice0_U8){dn_tmp_39.data,dn_tmp_39.len}); });
dn_f_bytes_append(dn_tmp_37,dn_tmp_38); }));
(void)(({ dn_t_buf_U8 * dn_tmp_40=(&(v_b));
uint8_t dn_tmp_41=({ uint8_t dn_tmp_42=((uint8_t)(0ULL));
if((unsigned __int128)dn_tmp_42>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_42; });
dn_push_buf_U8(dn_tmp_40,dn_tmp_41); }));
{ dn_t_buf_U8 dn_tmp_44=({ dn_t_buf_U8 dn_tmp_43=v_b;v_b=(dn_t_buf_U8){0};dn_adopt_buf_U8(&dn_tmp_43);dn_tmp_43; });dn_adopt_buf_U8(&dn_tmp_44);dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,0);
return dn_tmp_44; }
dn_drop_buf_U8(&v_b);
dn_boundary_end(&dn_tmp_0,0);
}

dn_fail("missing native return");
}
static dn_t_buf_U8 dn_cb_divert_filter(dn_r_Options const * v_o) {
 if(!dn_enter()){return (dn_t_buf_U8){0};} dn_boundary dn_b={0};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;
 if(setjmp(dn_jump)){dn_boundary_end(&dn_b,1);dn_leave();return (dn_t_buf_U8){0};}
 dn_t_buf_U8 dn_result=dn_f_divert_filter(v_o);dn_boundary_end(&dn_b,0);dn_leave();return dn_result;
}
static uint8_t dn_f_main(void) {
{
{ size_t dn_tmp_1=((size_t)(0ULL));size_t dn_tmp_0=((size_t)(4ULL));for(size_t v_mode=dn_tmp_1;v_mode<dn_tmp_0;v_mode++){
{ size_t dn_tmp_3=((size_t)(0ULL));size_t dn_tmp_2=((size_t)(2ULL));for(size_t v_all=dn_tmp_3;v_all<dn_tmp_2;v_all++){
{ size_t dn_tmp_5=((size_t)(0ULL));size_t dn_tmp_4=((size_t)(2ULL));for(size_t v_game=dn_tmp_5;v_game<dn_tmp_4;v_game++){
{ size_t dn_tmp_7=((size_t)(0ULL));size_t dn_tmp_6=((size_t)(2ULL));for(size_t v_udp=dn_tmp_7;v_udp<dn_tmp_6;v_udp++){
dn_r_Options volatile v_o={0};
dn_t_buf_U8 volatile v_b={0};
dn_boundary dn_tmp_8={0};jmp_buf dn_tmp_9;dn_tmp_8.jump=&dn_tmp_9;dn_tmp_8.previous=dn_current;dn_tmp_8.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_8;if(setjmp(dn_tmp_9)){dn_current=dn_tmp_8.previous;dn_drop_buf_U8(&v_b);
dn_drop_r7_Options(&v_o);
dn_boundary_end(&dn_tmp_8,1);dn_raise();}
v_o=({ dn_f_default_options(); });
{ uint32_t dn_tmp_12=({ size_t dn_tmp_10=v_mode;
if((unsigned __int128)dn_tmp_10>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_10; });uint32_t *dn_tmp_11=&((v_o).f_quic);*dn_tmp_11=dn_tmp_12; }
{ bool dn_tmp_16=({ size_t dn_tmp_13=v_all;
size_t dn_tmp_14=((size_t)(1ULL));
(dn_tmp_13 == dn_tmp_14); });bool *dn_tmp_15=&((v_o).f_all_ports);*dn_tmp_15=dn_tmp_16; }
{ bool dn_tmp_20=({ size_t dn_tmp_17=v_game;
size_t dn_tmp_18=((size_t)(1ULL));
(dn_tmp_17 == dn_tmp_18); });bool *dn_tmp_19=&((v_o).f_game);*dn_tmp_19=dn_tmp_20; }
{ bool dn_tmp_24=({ size_t dn_tmp_21=v_udp;
size_t dn_tmp_22=((size_t)(1ULL));
(dn_tmp_21 == dn_tmp_22); });bool *dn_tmp_23=&((v_o).f_udp_obfuscate);*dn_tmp_23=dn_tmp_24; }
v_b=({ dn_r_Options const * dn_tmp_25=(&(v_o));
dn_f_divert_filter(dn_tmp_25); });
int32_t v_depth=((int32_t)(0ULL));
(void)(({ bool dn_tmp_26=({ uint8_t dn_tmp_27=(*({ dn_t_buf_U8 volatile *dn_tmp_28=&(v_b);size_t dn_tmp_29=({ size_t dn_tmp_30=({ dn_t_buf_U8 const * dn_tmp_31=(&(v_b));
(*(dn_tmp_31)).len; });
size_t dn_tmp_32=((size_t)(1ULL));
dn_USize_sub(dn_tmp_30,dn_tmp_32); });&dn_tmp_28->data[dn_index(dn_tmp_29,dn_tmp_28->len)]; }));
uint8_t dn_tmp_33=({ uint8_t dn_tmp_34=((uint8_t)(0ULL));
if((unsigned __int128)dn_tmp_34>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_34; });
(dn_tmp_27 == dn_tmp_33); });
(dn_assert(dn_tmp_26),0); }));
{ size_t dn_tmp_39=((size_t)(0ULL));size_t dn_tmp_38=({ size_t dn_tmp_35=({ dn_t_buf_U8 const * dn_tmp_36=(&(v_b));
(*(dn_tmp_36)).len; });
size_t dn_tmp_37=((size_t)(1ULL));
dn_USize_sub(dn_tmp_35,dn_tmp_37); });for(size_t v_i=dn_tmp_39;v_i<dn_tmp_38;v_i++){
if(({ uint8_t dn_tmp_40=(*({ dn_t_buf_U8 volatile *dn_tmp_41=&(v_b);size_t dn_tmp_42=v_i;&dn_tmp_41->data[dn_index(dn_tmp_42,dn_tmp_41->len)]; }));
uint8_t dn_tmp_43=({ uint8_t dn_tmp_44=((uint8_t)(40ULL));
if((unsigned __int128)dn_tmp_44>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_44; });
(dn_tmp_40 == dn_tmp_43); })){
{ int32_t dn_tmp_49=({ int32_t dn_tmp_45=v_depth;
int32_t dn_tmp_46=({ int32_t dn_tmp_47=((int32_t)(1ULL));
if((__int128)dn_tmp_47<(-((__int128)1<<31))||(__int128)dn_tmp_47>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_47; });
dn_I32_add(dn_tmp_45,dn_tmp_46); });int32_t *dn_tmp_48=&(v_depth);*dn_tmp_48=dn_tmp_49; }
}
else{
}
if(({ uint8_t dn_tmp_50=(*({ dn_t_buf_U8 volatile *dn_tmp_51=&(v_b);size_t dn_tmp_52=v_i;&dn_tmp_51->data[dn_index(dn_tmp_52,dn_tmp_51->len)]; }));
uint8_t dn_tmp_53=({ uint8_t dn_tmp_54=((uint8_t)(41ULL));
if((unsigned __int128)dn_tmp_54>(((unsigned __int128)1<<8)-1))dn_fail("numeric conversion out of range");(uint8_t)dn_tmp_54; });
(dn_tmp_50 == dn_tmp_53); })){
{ int32_t dn_tmp_59=({ int32_t dn_tmp_55=v_depth;
int32_t dn_tmp_56=({ int32_t dn_tmp_57=((int32_t)(1ULL));
if((__int128)dn_tmp_57<(-((__int128)1<<31))||(__int128)dn_tmp_57>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_57; });
dn_I32_sub(dn_tmp_55,dn_tmp_56); });int32_t *dn_tmp_58=&(v_depth);*dn_tmp_58=dn_tmp_59; }
}
else{
}
(void)(({ bool dn_tmp_60=({ int32_t dn_tmp_61=v_depth;
int32_t dn_tmp_62=({ int32_t dn_tmp_63=((int32_t)(0ULL));
if((__int128)dn_tmp_63<(-((__int128)1<<31))||(__int128)dn_tmp_63>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_63; });
(dn_tmp_61 >= dn_tmp_62); });
(dn_assert(dn_tmp_60),0); }));
}
 }
(void)(({ bool dn_tmp_64=({ int32_t dn_tmp_65=v_depth;
int32_t dn_tmp_66=({ int32_t dn_tmp_67=((int32_t)(0ULL));
if((__int128)dn_tmp_67<(-((__int128)1<<31))||(__int128)dn_tmp_67>(((__int128)1<<31)-1))dn_fail("numeric conversion out of range");(int32_t)dn_tmp_67; });
(dn_tmp_65 == dn_tmp_66); });
(dn_assert(dn_tmp_64),0); }));
(void)(({ bool dn_tmp_68=({ dn_t_slice0_U8 dn_tmp_69=({ dn_t_buf_U8 const * dn_tmp_70=(&(v_b));
size_t dn_tmp_71=((size_t)(0ULL));
size_t dn_tmp_72=({ dn_t_buf_U8 const * dn_tmp_73=(&(v_b));
(*(dn_tmp_73)).len; });
({ dn_range(dn_tmp_71,dn_tmp_72,(*(dn_tmp_70)).len);(dn_t_slice0_U8){(*(dn_tmp_70)).data?(*(dn_tmp_70)).data+dn_tmp_71:NULL,dn_tmp_72}; }); });
size_t dn_tmp_74=((size_t)(0ULL));
dn_t_slice0_U8 dn_tmp_75=({ dn_utf8 dn_tmp_76=((dn_utf8){(const uint8_t*)"not loopback and not impostor and (ip or ipv6) and (", 52});
((dn_t_slice0_U8){dn_tmp_76.data,dn_tmp_76.len}); });
dn_f_bytes_at(dn_tmp_69,dn_tmp_74,dn_tmp_75); });
(dn_assert(dn_tmp_68),0); }));
dn_drop_buf_U8(&v_b);
dn_drop_r7_Options(&v_o);
dn_boundary_end(&dn_tmp_8,0);
}
 }
}
 }
}
 }
}
 }
(void)(({ dn_utf8 dn_tmp_77=((dn_utf8){(const uint8_t*)"DPI DLM 2.0: 32 capture filter combinations passed", 50});
(fwrite(dn_tmp_77.data,1,dn_tmp_77.len,stdout),fputc('\n',stdout),0); }));
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
DN_EXPORT size_t dlm_native_sizeof_Options(void){return 200;}
DN_EXPORT size_t dlm_native_alignof_Options(void){return 8;}
DN_EXPORT uint32_t dlm_native_version_Options(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Options_aggressive(void){return 0;}
DN_EXPORT size_t dlm_native_offset_Options_fake(void){return 1;}
DN_EXPORT size_t dlm_native_offset_Options_http(void){return 2;}
DN_EXPORT size_t dlm_native_offset_Options_sni(void){return 3;}
DN_EXPORT size_t dlm_native_offset_Options_reverse(void){return 4;}
DN_EXPORT size_t dlm_native_offset_Options_ech(void){return 5;}
DN_EXPORT size_t dlm_native_offset_Options_adaptive(void){return 6;}
DN_EXPORT size_t dlm_native_offset_Options_learn(void){return 7;}
DN_EXPORT size_t dlm_native_offset_Options_all_ports(void){return 8;}
DN_EXPORT size_t dlm_native_offset_Options_game(void){return 9;}
DN_EXPORT size_t dlm_native_offset_Options_whitelist(void){return 10;}
DN_EXPORT size_t dlm_native_offset_Options_quic(void){return 12;}
DN_EXPORT size_t dlm_native_offset_Options_probe_ms(void){return 16;}
DN_EXPORT size_t dlm_native_offset_Options_fake_mode(void){return 20;}
DN_EXPORT size_t dlm_native_offset_Options_split_mode(void){return 24;}
DN_EXPORT size_t dlm_native_offset_Options_strategy(void){return 28;}
DN_EXPORT size_t dlm_native_offset_Options_first_packets(void){return 32;}
DN_EXPORT size_t dlm_native_offset_Options_jitter_ms(void){return 36;}
DN_EXPORT size_t dlm_native_offset_Options_ttl_margin(void){return 40;}
DN_EXPORT size_t dlm_native_offset_Options_ttl_spread(void){return 44;}
DN_EXPORT size_t dlm_native_offset_Options_ttl_max(void){return 48;}
DN_EXPORT size_t dlm_native_offset_Options_min_hops(void){return 52;}
DN_EXPORT size_t dlm_native_offset_Options_debug(void){return 56;}
DN_EXPORT size_t dlm_native_offset_Options_help(void){return 60;}
DN_EXPORT size_t dlm_native_offset_Options_error(void){return 61;}
DN_EXPORT size_t dlm_native_offset_Options_auto_strategy(void){return 62;}
DN_EXPORT size_t dlm_native_offset_Options_forced_arm(void){return 64;}
DN_EXPORT size_t dlm_native_offset_Options_server_window(void){return 68;}
DN_EXPORT size_t dlm_native_offset_Options_udp_obfuscate(void){return 69;}
DN_EXPORT size_t dlm_native_offset_Options_diagnostic(void){return 70;}
DN_EXPORT size_t dlm_native_offset_Options_handshake_timeout(void){return 72;}
DN_EXPORT size_t dlm_native_offset_Options_safe_automatic(void){return 76;}
DN_EXPORT size_t dlm_native_offset_Options_telegram(void){return 77;}
DN_EXPORT size_t dlm_native_offset_Options_cloud_cdn(void){return 78;}
DN_EXPORT size_t dlm_native_offset_Options_driver_check(void){return 79;}
DN_EXPORT size_t dlm_native_offset_Options_driver_report(void){return 80;}
DN_EXPORT size_t dlm_native_offset_Options_parent_pid(void){return 104;}
DN_EXPORT size_t dlm_native_offset_Options_gene_mode(void){return 108;}
DN_EXPORT size_t dlm_native_offset_Options_gene(void){return 112;}
DN_EXPORT size_t dlm_native_offset_Options_gene_target(void){return 144;}
DN_EXPORT size_t dlm_native_offset_Options_trial_tag(void){return 152;}
DN_EXPORT size_t dlm_native_offset_Options_paths(void){return 176;}
int main(void){(void)dlm_native_thread_init();dn_boundary b={0};jmp_buf dn_jump;b.jump=&dn_jump;b.foreign_marker=dn_foreign_context;dn_current=&b;dn_fp_begin(&b);if(setjmp(dn_jump)){dn_boundary_end(&b,1);fprintf(stderr,"DLM native error: %s\n",dn_error);return 1;}(void)dn_f_main();dn_boundary_end(&b,0);(void)dlm_native_runtime_shutdown();return 0;}
