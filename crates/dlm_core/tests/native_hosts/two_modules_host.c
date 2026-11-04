#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#ifdef _WIN32
#include <windows.h>
#define OPEN(p) LoadLibraryA(p)
#define SYM(p,n) GetProcAddress(p,n)
#define CLOSE(p) FreeLibrary(p)
#else
#include <dlfcn.h>
#define OPEN(p) dlopen(p,RTLD_NOW|RTLD_GLOBAL)
#define SYM(p,n) dlsym(p,n)
#define CLOSE(p) dlclose(p)
#endif
typedef struct Api {uint32_t version;size_t size;void*(*create)(float);void(*destroy)(void*);void(*set_gain)(void*,float);void(*process)(void*,float*,size_t);} Api;
int main(int argc,char **argv){assert(argc==3);void *a=(void*)OPEN(argv[1]),*b=(void*)OPEN(argv[2]);assert(a&&b&&a!=b);Api(*get_a)(void)=(Api(*)(void))(uintptr_t)SYM(a,"component_api");Api(*get_b)(void)=(Api(*)(void))(uintptr_t)SYM(b,"component_api");assert(get_a&&get_b);Api api_a=get_a(),api_b=get_b();assert((uintptr_t)api_a.create==(uintptr_t)SYM(a,"component_create"));assert((uintptr_t)api_b.create==(uintptr_t)SYM(b,"component_create"));assert(api_a.create!=api_b.create);
 void *first=api_a.create(.5f),*second=api_b.create(2.f);assert(first&&second);float values[]={2,4};api_a.process(first,values,2);assert(values[0]==1&&values[1]==2);api_b.process(second,values,2);assert(values[0]==2&&values[1]==4);api_a.destroy(first);bool(*stop_a)(void)=(bool(*)(void))(uintptr_t)SYM(a,"dlm_native_runtime_shutdown");bool(*stop_b)(void)=(bool(*)(void))(uintptr_t)SYM(b,"dlm_native_runtime_shutdown");assert(stop_a&&stop_b&&stop_a());api_b.process(second,values,2);assert(values[0]==4&&values[1]==8);api_b.destroy(second);assert(stop_b());
#ifdef _WIN32
 assert(CLOSE(b)&&CLOSE(a));
#else
 assert(!CLOSE(b)&&!CLOSE(a));
#endif
 puts("two modules: independent runtime, function tables and shutdown OK");return 0;}
