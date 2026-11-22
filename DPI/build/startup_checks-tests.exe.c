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
typedef struct dn_r_StartupView dn_r_StartupView;
typedef struct dn_t_arr254_U8 { uint8_t data[254]; } dn_t_arr254_U8;
struct dn_r_Host {
    dn_t_arr254_U8 f_bytes;
    size_t f_length;
};
struct dn_r_StartupView {
    bool f_found;
    bool f_ready;
    bool f_driver;
    uint32_t f_error;
};
typedef struct dn_t_arr20_U8 { uint8_t data[20]; } dn_t_arr20_U8;
typedef struct dn_t_buf_U8 { uint8_t *data;size_t len,cap; } dn_t_buf_U8;
typedef struct dn_t_slice0_U8 { uint8_t const *data;size_t len; } dn_t_slice0_U8;
_Static_assert(sizeof(dn_r_Host)==264,"Host ABI size");
_Static_assert(_Alignof(dn_r_Host)==8,"Host ABI alignment");
_Static_assert(offsetof(dn_r_Host,f_bytes)==0,"Host.bytes ABI offset");
_Static_assert(offsetof(dn_r_Host,f_length)==256,"Host.length ABI offset");
_Static_assert(sizeof(dn_r_StartupView)==8,"StartupView ABI size");
_Static_assert(_Alignof(dn_r_StartupView)==4,"StartupView ABI alignment");
_Static_assert(offsetof(dn_r_StartupView,f_found)==0,"StartupView.found ABI offset");
_Static_assert(offsetof(dn_r_StartupView,f_ready)==1,"StartupView.ready ABI offset");
_Static_assert(offsetof(dn_r_StartupView,f_driver)==2,"StartupView.driver ABI offset");
_Static_assert(offsetof(dn_r_StartupView,f_error)==4,"StartupView.error ABI offset");
static void dn_drop_buf_U8(dn_t_buf_U8 volatile*);static void dn_retain_buf_U8(dn_t_buf_U8 volatile*);
static void dn_adopt_buf_U8(dn_t_buf_U8 volatile*);
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
static dn_r_StartupView dn_f_startup_view(dn_t_slice0_U8 v_text);
static dn_t_buf_U8 dn_f_startup_caption(uint32_t v_code);
static uint8_t dn_f_main(void);
static void dn_drop_buf_U8(dn_t_buf_U8 volatile *x); static void dn_retain_buf_U8(dn_t_buf_U8 volatile *x);
static void dn_drop_buf_U8(dn_t_buf_U8 volatile *x){dn_deallocate(x->data);*x=(dn_t_buf_U8){0};}
static void dn_retain_buf_U8(dn_t_buf_U8 volatile *x){dn_detach(x->data);}
static dn_t_buf_U8 dn_new_buf_U8(size_t n){dn_t_buf_U8 x={0};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof(uint8_t)),_Alignof(uint8_t));x.len=x.cap=n;return x;}
static dn_t_buf_U8 dn_aligned_buf_U8(size_t n,size_t a){if(a<_Alignof(uint8_t)||a>1048576||!a||(a&(a-1)))dn_fail("invalid buffer alignment");dn_t_buf_U8 x={0};x.data=dn_allocate_aligned(dn_size(n,sizeof(uint8_t)),a);x.len=x.cap=n;return x;}
static uint8_t dn_push_buf_U8(dn_t_buf_U8 *x,uint8_t v){if(x->len==SIZE_MAX)dn_fail("buffer length overflow");if(x->len==x->cap){size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;uint8_t *p=dn_allocate_aligned(dn_size(cap,sizeof(uint8_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(uint8_t));if(x->len)memcpy(p,x->data,x->len*sizeof(uint8_t));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}x->data[x->len]=v;x->len++;return 0;}
static uint8_t dn_resize_buf_U8(dn_t_buf_U8 *x,size_t n){if(n<x->len){}if(n>x->cap){size_t cap=n;uint8_t *p=dn_allocate_aligned(dn_size(cap,sizeof(uint8_t)),x->data?((dn_mem*)x->data-1)->alignment:_Alignof(uint8_t));if(x->len)memcpy(p,x->data,x->len*sizeof(uint8_t));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof(uint8_t));x->len=n;return 0;}
static void dn_adopt_buf_U8(dn_t_buf_U8 volatile*x){dn_adopt(x->data);}
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
static dn_r_StartupView dn_cb_startup_view(dn_t_slice0_U8 v_text);
static dn_t_buf_U8 dn_cb_startup_caption(uint32_t v_code);
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
static uint8_t dn_f_main(void) {
{
dn_t_buf_U8 volatile v_caption={0};
dn_boundary dn_tmp_0={0};jmp_buf dn_tmp_1;dn_tmp_0.jump=&dn_tmp_1;dn_tmp_0.previous=dn_current;dn_tmp_0.foreign_marker=dn_foreign_context;dn_current=&dn_tmp_0;if(setjmp(dn_tmp_1)){dn_current=dn_tmp_0.previous;dn_drop_buf_U8(&v_caption);
dn_boundary_end(&dn_tmp_0,1);dn_raise();}
dn_r_StartupView v_failed=({ dn_t_slice0_U8 dn_tmp_2=({ dn_utf8 dn_tmp_3=((dn_utf8){(const uint8_t*)"pid=42\012component=WinDivertOpen\012win32_error=58\012ready=0\012", 54});
((dn_t_slice0_U8){dn_tmp_3.data,dn_tmp_3.len}); });
dn_f_startup_view(dn_tmp_2); });
(void)(({ bool dn_tmp_4=((((v_failed).f_found && (v_failed).f_driver) && (!((v_failed).f_ready))) && ({ uint32_t dn_tmp_5=(v_failed).f_error;
uint32_t dn_tmp_6=({ uint32_t dn_tmp_7=((uint32_t)(58ULL));
if((unsigned __int128)dn_tmp_7>(((unsigned __int128)1<<32)-1))dn_fail("numeric conversion out of range");(uint32_t)dn_tmp_7; });
(dn_tmp_5 == dn_tmp_6); }));
(dn_assert(dn_tmp_4),0); }));
v_caption=({ uint32_t dn_tmp_8=(v_failed).f_error;
dn_f_startup_caption(dn_tmp_8); });
(void)(({ bool dn_tmp_9=({ size_t dn_tmp_10=({ dn_t_slice0_U8 dn_tmp_11=({ dn_t_buf_U8 const * dn_tmp_12=(&(v_caption));
size_t dn_tmp_13=((size_t)(0ULL));
size_t dn_tmp_14=({ dn_t_buf_U8 const * dn_tmp_15=(&(v_caption));
(*(dn_tmp_15)).len; });
({ dn_range(dn_tmp_13,dn_tmp_14,(*(dn_tmp_12)).len);(dn_t_slice0_U8){(*(dn_tmp_12)).data?(*(dn_tmp_12)).data+dn_tmp_13:NULL,dn_tmp_14}; }); });
size_t dn_tmp_16=((size_t)(0ULL));
dn_t_slice0_U8 dn_tmp_17=({ dn_utf8 dn_tmp_18=((dn_utf8){(const uint8_t*)"58", 2});
((dn_t_slice0_U8){dn_tmp_18.data,dn_tmp_18.len}); });
dn_f_bytes_find(dn_tmp_11,dn_tmp_16,dn_tmp_17); });
size_t dn_tmp_19=({ dn_t_buf_U8 const * dn_tmp_20=(&(v_caption));
(*(dn_tmp_20)).len; });
(dn_tmp_10 < dn_tmp_19); });
(dn_assert(dn_tmp_9),0); }));
dn_r_StartupView v_policy=({ dn_t_slice0_U8 dn_tmp_21=({ dn_utf8 dn_tmp_22=((dn_utf8){(const uint8_t*)"component=PolicyConfiguration\012win32_error=87\012ready=0\012", 53});
((dn_t_slice0_U8){dn_tmp_22.data,dn_tmp_22.len}); });
dn_f_startup_view(dn_tmp_21); });
(void)(({ bool dn_tmp_23=(((v_policy).f_found && (!((v_policy).f_driver))) && (!((v_policy).f_ready)));
(dn_assert(dn_tmp_23),0); }));
dn_r_StartupView v_success=({ dn_t_slice0_U8 dn_tmp_24=({ dn_utf8 dn_tmp_25=((dn_utf8){(const uint8_t*)"component=EngineReady\012win32_error=0\012ready=1\012", 44});
((dn_t_slice0_U8){dn_tmp_25.data,dn_tmp_25.len}); });
dn_f_startup_view(dn_tmp_24); });
(void)(({ bool dn_tmp_26=(((v_success).f_found && (v_success).f_ready) && (!((v_success).f_driver)));
(dn_assert(dn_tmp_26),0); }));
dn_r_StartupView v_empty=({ dn_t_slice0_U8 dn_tmp_27=({ dn_utf8 dn_tmp_28=((dn_utf8){(const uint8_t*)"", 0});
((dn_t_slice0_U8){dn_tmp_28.data,dn_tmp_28.len}); });
dn_f_startup_view(dn_tmp_27); });
(void)(({ bool dn_tmp_29=(!((v_empty).f_found));
(dn_assert(dn_tmp_29),0); }));
dn_r_StartupView v_broken=({ dn_t_slice0_U8 dn_tmp_30=({ dn_utf8 dn_tmp_31=((dn_utf8){(const uint8_t*)"win32_error=4294967296\012ready=1\012", 31});
((dn_t_slice0_U8){dn_tmp_31.data,dn_tmp_31.len}); });
dn_f_startup_view(dn_tmp_30); });
(void)(({ bool dn_tmp_32=(!((v_broken).f_found));
(dn_assert(dn_tmp_32),0); }));
dn_r_StartupView v_incomplete=({ dn_t_slice0_U8 dn_tmp_33=({ dn_utf8 dn_tmp_34=((dn_utf8){(const uint8_t*)"win32_error=58\012ready=2\012", 23});
((dn_t_slice0_U8){dn_tmp_34.data,dn_tmp_34.len}); });
dn_f_startup_view(dn_tmp_33); });
(void)(({ bool dn_tmp_35=(!((v_incomplete).f_found));
(dn_assert(dn_tmp_35),0); }));
(void)(({ dn_utf8 dn_tmp_36=((dn_utf8){(const uint8_t*)"DPI 2.4 startup status and failure classification passed", 56});
(fwrite(dn_tmp_36.data,1,dn_tmp_36.len,stdout),fputc('\n',stdout),0); }));
dn_drop_buf_U8(&v_caption);
dn_boundary_end(&dn_tmp_0,0);
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
DN_EXPORT size_t dlm_native_sizeof_StartupView(void){return 8;}
DN_EXPORT size_t dlm_native_alignof_StartupView(void){return 4;}
DN_EXPORT uint32_t dlm_native_version_StartupView(void){return 1;}
DN_EXPORT size_t dlm_native_offset_StartupView_found(void){return 0;}
DN_EXPORT size_t dlm_native_offset_StartupView_ready(void){return 1;}
DN_EXPORT size_t dlm_native_offset_StartupView_driver(void){return 2;}
DN_EXPORT size_t dlm_native_offset_StartupView_error(void){return 4;}
int main(void){(void)dlm_native_thread_init();dn_boundary b={0};jmp_buf dn_jump;b.jump=&dn_jump;b.foreign_marker=dn_foreign_context;dn_current=&b;dn_fp_begin(&b);if(setjmp(dn_jump)){dn_boundary_end(&b,1);fprintf(stderr,"DLM native error: %s\n",dn_error);return 1;}(void)dn_f_main();dn_boundary_end(&b,0);(void)dlm_native_runtime_shutdown();return 0;}
