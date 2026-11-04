#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#define OPEN(p) LoadLibraryA(p)
#define SYM(p,n) GetProcAddress(p,n)
#else
#include <dlfcn.h>
#define OPEN(p) dlopen(p,RTLD_NOW|RTLD_LOCAL)
#define SYM(p,n) dlsym(p,n)
#endif
typedef struct Api {uint32_t version;size_t size;void*(*create)(float);void(*destroy)(void*);void(*set_gain)(void*,float);void(*process)(void*,float*,size_t);} Api;
int main(int argc,char **argv){assert(argc==2);void *module=(void*)OPEN(argv[1]);assert(module);Api(*get_api)(void)=(Api(*)(void))(uintptr_t)SYM(module,"component_api");assert(get_api);Api api=get_api();assert(api.version==1&&api.size==sizeof api&&api.create&&api.destroy&&api.set_gain&&api.process);
 uint64_t(*allocations)(void)=(uint64_t(*)(void))(uintptr_t)SYM(module,"dlm_native_allocations");uint64_t(*frees_)(void)=(uint64_t(*)(void))(uintptr_t)SYM(module,"dlm_native_frees");uint64_t(*live)(void)=(uint64_t(*)(void))(uintptr_t)SYM(module,"dlm_native_live_allocations");uint64_t(*init_thread)(void)=(uint64_t(*)(void))(uintptr_t)SYM(module,"dlm_native_thread_init");bool(*shutdown_)(void)=(bool(*)(void))(uintptr_t)SYM(module,"dlm_native_runtime_shutdown");assert(allocations&&frees_&&live&&init_thread&&shutdown_);init_thread();
 void *a=api.create(.5f),*b=api.create(2.f);assert(a&&b&&a!=b);float *samples=malloc(4096*sizeof(float));assert(samples);for(size_t i=0;i<4096;i++)samples[i]=(float)i;api.process(a,samples,4096);assert(samples[42]==21.f);api.process(b,samples,4096);assert(samples[42]==42.f);api.set_gain(a,1.f);
 uint64_t before=allocations();clock_t start=clock();for(size_t block=0;block<2048;block++)api.process(a,samples,4096);clock_t end=clock();assert(allocations()==before&&samples[42]==42.f);printf("component function table: 8388608 samples, %.3f CPU seconds, 0 hot-loop allocations\n",(double)(end-start)/CLOCKS_PER_SEC);
 api.destroy(a);api.process(b,samples,4096);assert(samples[42]==84.f);api.destroy(b);free(samples);assert(!live()&&allocations()==frees_());assert(shutdown_());
#ifdef _WIN32
 assert(FreeLibrary(module));
#else
 assert(!dlclose(module));
#endif
 return 0;}
