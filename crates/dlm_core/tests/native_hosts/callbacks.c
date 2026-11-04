#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#define EXPORT __declspec(dllexport)
typedef HANDLE thread_t;
#else
#include <pthread.h>
#define EXPORT __attribute__((visibility("default")))
typedef pthread_t thread_t;
#endif
extern uint64_t dlm_native_thread_init(void);
typedef uint64_t (*callback_t)(void*,uint64_t);
typedef struct subscription {callback_t callback;void *context;_Atomic int stop;thread_t threads[4];size_t started;} subscription;
static _Atomic unsigned active;
#ifdef _WIN32
static DWORD WINAPI worker(void *raw)
#else
static void *worker(void *raw)
#endif
{subscription *s=raw;dlm_native_thread_init();for(unsigned i=0;i<4000&&!atomic_load_explicit(&s->stop,memory_order_acquire);i++)s->callback(s->context,1);return 0;}
void host_unsubscribe(void *raw){subscription *s=raw;if(!s)return;atomic_store_explicit(&s->stop,1,memory_order_release);for(size_t i=0;i<s->started;i++){
#ifdef _WIN32
 WaitForSingleObject(s->threads[i],INFINITE);CloseHandle(s->threads[i]);
#else
 pthread_join(s->threads[i],NULL);
#endif
}atomic_fetch_sub_explicit(&active,1,memory_order_relaxed);free(s);}
void *host_subscribe(callback_t callback,void *context){subscription *s=calloc(1,sizeof *s);if(!s)return NULL;s->callback=callback;s->context=context;atomic_fetch_add_explicit(&active,1,memory_order_relaxed);for(size_t i=0;i<4;i++){
#ifdef _WIN32
 s->threads[i]=CreateThread(NULL,0,worker,s,0,NULL);if(!s->threads[i]){host_unsubscribe(s);return NULL;}
#else
 if(pthread_create(&s->threads[i],NULL,worker,s)){host_unsubscribe(s);return NULL;}
#endif
 s->started++;
}return s;}
EXPORT unsigned host_active_subscriptions(void){return atomic_load_explicit(&active,memory_order_relaxed);}
