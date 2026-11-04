#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#ifdef _WIN32
#include <windows.h>
#define OPEN(p) LoadLibraryA(p)
#define SYM(p,n) GetProcAddress(p,n)
#define CLOSE(p) FreeLibrary(p)
#define PAUSE() Sleep(1)
#else
#include <dlfcn.h>
#include <time.h>
#define OPEN(p) dlopen(p,RTLD_NOW|RTLD_LOCAL)
#define SYM(p,n) dlsym(p,n)
#define CLOSE(p) dlclose(p)
#define PAUSE() nanosleep(&(struct timespec){0,1000000},NULL)
#endif
#define LOAD(type,n) type n=(type)(uintptr_t)SYM(module,#n);assert(n)
int main(int argc,char **argv){assert(argc==2);void *module=(void*)OPEN(argv[1]);assert(module);
 typedef void*(*Create)(uint64_t,float);typedef void(*Destroy)(void*);typedef uint64_t(*Count)(void*);typedef void(*Process)(void*,float*,size_t);typedef void(*Fail)(void);typedef const char*(*Error)(void);typedef unsigned(*Active)(void);typedef uint64_t(*Counter)(void);typedef bool(*State)(void);
 LOAD(Create,callback_create);LOAD(Destroy,callback_destroy);LOAD(Count,callback_count);LOAD(Process,callback_process);LOAD(Fail,callback_fail);LOAD(Error,dlm_native_last_error);LOAD(Active,host_active_subscriptions);LOAD(Counter,dlm_native_allocations);LOAD(Counter,dlm_native_frees);LOAD(Counter,dlm_native_live_allocations);LOAD(Counter,dlm_native_thread_init);LOAD(State,dlm_native_runtime_init);LOAD(State,dlm_native_runtime_shutdown);
 dlm_native_thread_init();assert(dlm_native_runtime_init());
 for(unsigned round=0;round<5;round++){
 void *a=callback_create(7,.5f),*b=callback_create(100,2.f);assert(a&&b&&!dlm_native_last_error());
 for(unsigned wait=0;wait<20000&&(callback_count(a)!=16007||callback_count(b)!=16100);wait++)PAUSE();
 assert(callback_count(a)==16007&&callback_count(b)==16100);assert(host_active_subscriptions()==2);
 float x[]={1,2,3,4};callback_process(a,x,4);assert(x[0]==.5f&&x[3]==2.f);callback_process(b,x,4);assert(x[0]==1.f&&x[3]==4.f);
 assert(!dlm_native_runtime_shutdown());callback_destroy(a);assert(!dlm_native_last_error()&&host_active_subscriptions()==1);assert(callback_count(b)==16100);
 callback_fail();assert(dlm_native_last_error()&&host_active_subscriptions()==1);callback_destroy(b);assert(!dlm_native_last_error()&&host_active_subscriptions()==0);
 }
 assert(dlm_native_live_allocations()==0);assert(dlm_native_allocations()==dlm_native_frees());assert(dlm_native_runtime_shutdown());assert(dlm_native_runtime_shutdown());assert(!callback_create(0,1)&&dlm_native_last_error());assert(dlm_native_runtime_init());void *a=callback_create(0,1);assert(a);callback_destroy(a);assert(dlm_native_runtime_shutdown());
#ifdef _WIN32
 assert(CLOSE(module)!=0);
#else
 assert(CLOSE(module)==0);
#endif
 puts("managed callbacks: 4 threads, lifetime, failure, shutdown/restart OK");return 0;}
