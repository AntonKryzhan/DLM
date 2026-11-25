/* Bounded CPU execution. Every transferred allocation is detached from its
   source boundary before publication and adopted by its next owner. */
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef HANDLE dnc_thread;
typedef struct { CRITICAL_SECTION mutex; CONDITION_VARIABLE changed; } dnc_sync;
static bool dnc_init(dnc_sync *s){if(!InitializeCriticalSectionEx(&s->mutex,0,0))return false;InitializeConditionVariable(&s->changed);return true;}
static void dnc_lock(dnc_sync*s){EnterCriticalSection(&s->mutex);}
static void dnc_unlock(dnc_sync*s){LeaveCriticalSection(&s->mutex);}
static void dnc_signal(dnc_sync*s){WakeAllConditionVariable(&s->changed);}
static void dnc_wait(dnc_sync*s,uint32_t ms){SleepConditionVariableCS(&s->changed,&s->mutex,ms);}
static void dnc_destroy(dnc_sync*s){DeleteCriticalSection(&s->mutex);}
static uint64_t dnc_now(void){return GetTickCount64();}
static void dnc_pause(uint32_t ms){Sleep(ms);}
#else
#include <pthread.h>
#include <time.h>
#include <errno.h>
typedef pthread_t dnc_thread;
typedef struct { pthread_mutex_t mutex; pthread_cond_t changed; } dnc_sync;
static bool dnc_init(dnc_sync*s){if(pthread_mutex_init(&s->mutex,NULL))return false;if(pthread_cond_init(&s->changed,NULL)){pthread_mutex_destroy(&s->mutex);return false;}return true;}
static void dnc_lock(dnc_sync*s){pthread_mutex_lock(&s->mutex);}
static void dnc_unlock(dnc_sync*s){pthread_mutex_unlock(&s->mutex);}
static void dnc_signal(dnc_sync*s){pthread_cond_broadcast(&s->changed);}
static void dnc_wait(dnc_sync*s,uint32_t ms){if(ms==UINT32_MAX){pthread_cond_wait(&s->changed,&s->mutex);return;}struct timespec t;clock_gettime(CLOCK_REALTIME,&t);t.tv_sec+=ms/1000;t.tv_nsec+=(long)(ms%1000)*1000000;if(t.tv_nsec>=1000000000){t.tv_sec++;t.tv_nsec-=1000000000;}pthread_cond_timedwait(&s->changed,&s->mutex,&t);}
static void dnc_destroy(dnc_sync*s){pthread_cond_destroy(&s->changed);pthread_mutex_destroy(&s->mutex);}
static uint64_t dnc_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;}
static void dnc_pause(uint32_t ms){struct timespec t={(time_t)(ms/1000),(long)(ms%1000)*1000000};while(nanosleep(&t,&t)&&errno==EINTR){}}
#endif

typedef struct dn_future {bool(*poll)(void*);void(*dispose)(void*);void *data,*result;bool ready;} dn_future;
static void dn_future_finalize(void*raw){dn_future*f=raw;if(f->dispose){void(*dispose)(void*)=f->dispose;f->dispose=NULL;dispose(f->data);}}
static void dn_future_drop(dn_future*f){if(f){((dn_mem*)f-1)->finalize=NULL;dn_future_finalize(f);dn_deallocate(f);}}
static dn_future *dn_future_open(size_t size,size_t alignment,bool(*poll)(void*),void(*dispose)(void*)){
 if(size>SIZE_MAX-sizeof(dn_future)-alignment)dn_fail("future size overflow");
 dn_future*f=dn_allocate_aligned(sizeof *f+size+alignment,alignment);f->data=(void*)(((uintptr_t)(f+1)+alignment-1)&~(uintptr_t)(alignment-1));f->poll=poll;f->dispose=dispose;((dn_mem*)f-1)->finalize=dn_future_finalize;return f;
}
static bool dn_future_poll(dn_future*f){dn_nonnull(f);if(!f->ready)f->ready=f->poll(f->data);return f->ready;}

typedef struct dn_pool dn_pool;
typedef struct dn_task dn_task;
typedef struct dn_executor {dn_pool *pool;uint64_t owner;} dn_executor;
struct dn_task {dn_pool *pool;dn_task *next,*all_next,*observer;void(*run)(void*);bool(*poll)(void*);void(*dispose)(void*);void *data,*result;unsigned state;uint64_t wake_at;bool notified;_Atomic bool cancelled;dn_task *cancel_parent;_Atomic bool *cancel_group;char error[256];};
struct dn_pool {dnc_sync sync;dnc_thread *threads;size_t workers,capacity,queued,references,waiting_count,polling;bool closing,favor_waiting;dn_task *head,*tail,*tasks,*waiting,*waiting_tail;dn_executor*blocking;};
static DN_TLS dn_task *dnc_current_task;
static bool dnc_cancelled(dn_task *task) {
 for (; task; task = task->cancel_parent) {
  if (atomic_load_explicit(&task->cancelled, memory_order_acquire) ||
      (task->cancel_group && atomic_load_explicit(task->cancel_group, memory_order_acquire))) return true;
 }
 return false;
}
static bool dn_task_cancelled(void){return dnc_cancelled(dnc_current_task);}
static bool dn_task_sleep(uint32_t ms){uint64_t start=dnc_now();while(dnc_now()-start<ms){if(dn_task_cancelled())return false;uint64_t left=ms-(dnc_now()-start);if(left>ms)break;dnc_pause(left>10?10:(uint32_t)left);}return !dn_task_cancelled();}
static void dnc_pool_free(dn_pool*p){dnc_destroy(&p->sync);free(p->threads);free(p);}
static void dnc_park_until(uint64_t deadline) {
 if (dnc_current_task && deadline < dnc_current_task->wake_at) dnc_current_task->wake_at = deadline;
}
static void dnc_wake_task(dn_task *task) {
 dn_pool *pool = task->pool;
 dnc_lock(&pool->sync);
 task->notified = true;
 dnc_signal(&pool->sync);
 dnc_unlock(&pool->sync);
}
static void dnc_dispose_task_value(dn_task *task) {
 if (task->dispose) {
  void (*dispose)(void *) = task->dispose;
  task->dispose = NULL;
  dispose(task->data);
 }
}
static void dnc_execute(dn_task *task) {
 dn_task *previous = dnc_current_task;
 const bool polling = task->poll != NULL;
 const bool cancelled = dnc_cancelled(task);
 dnc_current_task = task;
 jmp_buf jump;
 dn_boundary boundary = {0};
 boundary.jump = &jump;
 boundary.previous = dn_current;
 boundary.foreign_marker = dn_foreign_context;
 dn_current = &boundary;
 dn_fp_begin(&boundary);
 volatile bool failed = false, ready = true;
 if (setjmp(jump) == 0) {
  if (cancelled) dnc_dispose_task_value(task);
  else if (polling) ready = task->poll(task->data);
  else task->run(task->data);
 } else {
  failed = true;
  if (task->cancel_group) atomic_store_explicit(task->cancel_group, true, memory_order_release);
  snprintf(task->error, sizeof task->error, "%s", dn_error ? dn_error : "task failed");
  dnc_dispose_task_value(task);
 }
 dn_boundary_end(&boundary, failed);
 dn_error = NULL;
 dnc_current_task = previous;
 dn_pool *pool = task->pool;
 dnc_lock(&pool->sync);
 if (polling) pool->polling--;
 if (!failed && !ready) {
  task->state = 7;
  task->next = NULL;
  if (pool->waiting_tail) pool->waiting_tail->next = task;
  else pool->waiting = task;
  pool->waiting_tail = task;
  pool->waiting_count++;
 } else {
  task->state = failed ? 4 : cancelled ? 3 : 2;
  if (task->observer) {
   task->observer->notified = true;
   task->observer = NULL;
  }
 }
 dnc_signal(&pool->sync);
 dnc_unlock(&pool->sync);
}
static dn_task *dnc_take(dn_pool*p){
 uint64_t now=dnc_now();dn_task**at=&p->waiting,*previous=NULL;
 while(*at&&(*at)->wake_at>now&&!(*at)->notified&&!dnc_cancelled(*at)){previous=*at;at=&(*at)->next;}
 dn_task*t=NULL;
 if(*at&&(!p->head||p->favor_waiting)){t=*at;*at=t->next;if(p->waiting_tail==t)p->waiting_tail=previous;p->waiting_count--;p->favor_waiting=false;}
 else if(p->head){t=p->head;p->head=t->next;if(!p->head)p->tail=NULL;p->queued--;p->favor_waiting=true;}
 if(t){t->state=1;t->notified=false;t->wake_at=UINT64_MAX;if(t->poll)p->polling++;}return t;
}
static bool dnc_help(dn_pool*p){dnc_lock(&p->sync);dn_task*t=dnc_take(p);dnc_unlock(&p->sync);if(t)dnc_execute(t);return t!=NULL;}
static uint32_t dnc_wait_interval(dn_pool *pool) {
 uint64_t deadline = UINT64_MAX;
 for (dn_task *task = pool->waiting; task; task = task->next)
  if (task->wake_at < deadline) deadline = task->wake_at;
 if (deadline == UINT64_MAX) return UINT32_MAX;
 uint64_t now = dnc_now();
 uint64_t delay = deadline > now ? deadline - now : 0;
 return delay >= UINT32_MAX ? UINT32_MAX - 1 : (uint32_t)delay;
}
#if defined(_WIN32)
static DWORD WINAPI dnc_worker(void*raw)
#else
static void *dnc_worker(void*raw)
#endif
{
 dn_pool*p=raw;dlm_native_thread_init();
 for(;;){
  dnc_lock(&p->sync);dn_task*t;
  while(!(t=dnc_take(p))){if(p->closing&&!p->waiting){dnc_unlock(&p->sync);return 0;}dnc_wait(&p->sync,dnc_wait_interval(p));}
  dnc_unlock(&p->sync);dnc_execute(t);
 }
 return 0;
}
static void dnc_join_workers(dn_pool*p){for(size_t i=0;i<p->workers;i++){
#if defined(_WIN32)
 WaitForSingleObject(p->threads[i],INFINITE);CloseHandle(p->threads[i]);
#else
 pthread_join(p->threads[i],NULL);
#endif
}}
static void dn_executor_finalize(void*raw);
static dn_executor *dn_executor_open_internal(size_t,size_t,bool);
static void dn_executor_finalize(void*raw){dn_executor*e=raw;if(!e->pool)return;dn_check_thread(e->owner);dn_pool*p=e->pool;e->pool=NULL;
 dnc_lock(&p->sync);p->closing=true;for(dn_task*t=p->tasks;t;t=t->all_next)atomic_store_explicit(&t->cancelled,true,memory_order_release);dnc_signal(&p->sync);dnc_unlock(&p->sync);
 dnc_join_workers(p);if(p->blocking){dn_executor*b=p->blocking;p->blocking=NULL;((dn_mem*)b-1)->finalize=NULL;dn_executor_finalize(b);dn_deallocate(b);}dnc_lock(&p->sync);bool last=--p->references==0;dnc_unlock(&p->sync);if(last)dnc_pool_free(p);
}
static dn_executor *dn_executor_open_internal(size_t workers,size_t capacity,bool blocking){
 if(!workers||workers>256||!capacity||capacity>1048576)dn_fail("executor limits out of range");
 dn_executor*e=dn_allocate(sizeof *e);e->owner=dn_thread_id();((dn_mem*)e-1)->finalize=dn_executor_finalize;
 dn_pool*p=calloc(1,sizeof *p);if(!p)dn_fail("out of memory");p->threads=calloc(workers,sizeof *p->threads);if(!p->threads){free(p);dn_fail("out of memory");}
 if(!dnc_init(&p->sync)){free(p->threads);free(p);dn_fail("executor synchronization failed");}
 p->capacity=capacity;p->references=1;e->pool=p;
 for(size_t i=0;i<workers;i++){
#if defined(_WIN32)
  p->threads[i]=CreateThread(NULL,0,dnc_worker,p,0,NULL);bool ok=p->threads[i]!=NULL;
#else
  bool ok=pthread_create(&p->threads[i],NULL,dnc_worker,p)==0;
#endif
  if(!ok){dn_executor_finalize(e);dn_fail("executor worker creation failed");}p->workers++;
 }if(blocking){p->blocking=dn_executor_open_internal(workers>4?4:workers,capacity,false);dn_detach(p->blocking);}return e;
}
static dn_executor*dn_executor_open(size_t workers,size_t capacity){return dn_executor_open_internal(workers,capacity,true);}
static dn_executor*dn_blocking_executor(void){if(!dnc_current_task||!dnc_current_task->pool->blocking)dn_fail("blocking task requires an async scope");return dnc_current_task->pool->blocking;}
static unsigned dn_task_status(dn_task*t){dn_nonnull(t);dn_pool*p=t->pool;dnc_lock(&p->sync);unsigned s=t->state;dnc_unlock(&p->sync);return s==7?0:s;}
static unsigned dn_task_poll_ready(dn_task *task) {
 dn_nonnull(task);
 dn_pool *pool = task->pool;
 dnc_lock(&pool->sync);
 unsigned state = task->state == 7 ? 0 : task->state;
 if (state < 2) {
  if (dnc_current_task->pool == pool) task->observer = dnc_current_task;
  else dnc_park_until(dnc_now() + 1);
 }
 dnc_unlock(&pool->sync);
 return state;
}
static bool dn_task_cancel(dn_task*t){dn_nonnull(t);dn_pool*p=t->pool;dnc_lock(&p->sync);bool live=t->state<2||t->state==6||t->state==7;if(live)atomic_store_explicit(&t->cancelled,true,memory_order_release);dnc_signal(&p->sync);dnc_unlock(&p->sync);return live;}
static unsigned dn_task_wait(dn_task*t,uint32_t ms){dn_nonnull(t);if(t==dnc_current_task)dn_fail("task cannot wait on itself");dn_pool*p=t->pool;uint64_t start=dnc_now();dnc_lock(&p->sync);
 while(t->state<2||t->state==6||t->state==7){uint64_t elapsed=dnc_now()-start;if(ms!=UINT32_MAX&&elapsed>=ms)break;
  if(dnc_current_task&&dnc_current_task->pool==p){dnc_unlock(&p->sync);if(!dnc_help(p))dnc_pause(1);dnc_lock(&p->sync);}
  else{uint32_t delay=ms==UINT32_MAX?1000:(uint32_t)((ms-elapsed)>1000?1000:ms-elapsed);dnc_wait(&p->sync,delay);}}
 unsigned state=t->state;dnc_unlock(&p->sync);return state==7?0:state;
}
static void dn_task_finalize(void*raw){dn_task*t=raw;if(!t->pool)return;dn_pool*p=t->pool;
 dnc_lock(&p->sync);if(t->state==6)t->state=3;else if(t->state<2||t->state==7)atomic_store_explicit(&t->cancelled,true,memory_order_release);dnc_signal(&p->sync);dnc_unlock(&p->sync);
 dn_task_wait(t,UINT32_MAX);dnc_dispose_task_value(t);
 dnc_lock(&p->sync);dn_task**link=&p->tasks;while(*link&&*link!=t)link=&(*link)->all_next;if(*link)*link=t->all_next;bool last=--p->references==0;dnc_unlock(&p->sync);t->pool=NULL;if(last)dnc_pool_free(p);
}
static dn_task *dn_pool_prepare(dn_pool*pool,void(*run)(void*),void(*dispose)(void*),size_t size,size_t alignment){
 if(size>SIZE_MAX-sizeof(dn_task)-alignment)dn_fail("task size overflow");
 dn_task*t=dn_allocate_aligned(sizeof *t+size+alignment,alignment);t->data=(void*)(((uintptr_t)(t+1)+alignment-1)&~(uintptr_t)(alignment-1));t->run=run;t->dispose=dispose;t->state=6;atomic_init(&t->cancelled,false);t->pool=pool;((dn_mem*)t-1)->finalize=dn_task_finalize;
 dn_pool*p=t->pool;dnc_lock(&p->sync);t->all_next=p->tasks;p->tasks=t;p->references++;dnc_unlock(&p->sync);return t;
}
static dn_pool *dn_executor_pool(dn_executor *executor) {
 dn_nonnull(executor);
 dn_check_thread(executor->owner);
 if (!executor->pool) dn_fail("closed executor");
 return executor->pool;
}
static void dnc_enqueue(dn_pool *pool, dn_task *task) {
 task->state = 0;
 task->next = NULL;
 if (pool->tail) pool->tail->next = task;
 else pool->head = task;
 pool->tail = task;
 pool->queued++;
}
static void dn_task_submit(dn_task *task) {
 dn_pool *pool = task->pool;
 dnc_lock(&pool->sync);
 bool rejected = pool->closing || pool->queued + pool->waiting_count + pool->polling >= pool->capacity;
 if (rejected) task->state = 5;
 else {
  dnc_enqueue(pool, task);
  dnc_signal(&pool->sync);
 }
 dnc_unlock(&pool->sync);
 if (rejected) dnc_dispose_task_value(task);
}

typedef struct {dn_future*future;dn_task*task;} dnc_future_job;
static bool dnc_future_run(void*raw){dnc_future_job*j=raw;bool ready=dn_future_poll(j->future);if(ready)j->task->result=j->future->result;return ready;}
static void dnc_future_dispose(void*raw){dnc_future_job*j=raw;dn_future_drop(j->future);j->future=NULL;}
static dn_task *dn_pool_spawn_future(dn_pool*p,dn_future*f){dn_task*t=dn_pool_prepare(p,NULL,dnc_future_dispose,sizeof(dnc_future_job),_Alignof(dnc_future_job));dnc_future_job*j=t->data;j->future=f;j->task=t;dn_detach(f);t->poll=dnc_future_run;dn_task_submit(t);return t;}
static dn_task *dn_spawn_future(dn_executor*e,dn_future*f){return dn_pool_spawn_future(dn_executor_pool(e),f);}
static dn_task *dn_spawn_child(dn_future*f){if(!dnc_current_task)dn_fail("spawn_child requires an executor scope");return dn_pool_spawn_future(dnc_current_task->pool,f);}

typedef struct {uint64_t start;uint32_t delay;bool begun,yielded,is_yield;uint8_t result;} dnc_timer_future;
static bool dnc_timer_poll(void *raw) {
 dnc_timer_future *timer = raw;
 uint64_t now = dnc_now();
 if (!timer->begun) { timer->begun = true; timer->start = now; }
 if (timer->is_yield && !timer->yielded) {
  timer->yielded = true;
  dnc_park_until(now);
  return false;
 }
 if (now - timer->start >= timer->delay) return true;
 dnc_park_until(timer->start + timer->delay);
 return false;
}
static void dnc_timer_dispose(void*raw){(void)raw;}
static dn_future *dn_sleep_future(uint32_t delay,bool is_yield){dn_future*f=dn_future_open(sizeof(dnc_timer_future),_Alignof(dnc_timer_future),dnc_timer_poll,dnc_timer_dispose);dnc_timer_future*t=f->data;t->delay=delay;t->is_yield=is_yield;f->result=&t->result;return f;}

typedef struct {dn_future*left,*right,*owner;} dnc_select_future;
static bool dnc_select_poll(void*raw){dnc_select_future*s=raw;if(s->left&&dn_future_poll(s->left)){s->owner->result=s->left->result;dn_future_drop(s->right);s->right=NULL;return true;}if(s->right&&dn_future_poll(s->right)){s->owner->result=s->right->result;dn_future_drop(s->left);s->left=NULL;return true;}return false;}
static void dnc_select_dispose(void*raw){dnc_select_future*s=raw;dn_future_drop(s->left);dn_future_drop(s->right);s->left=s->right=NULL;}
static dn_future *dn_select_future(dn_future*left,dn_future*right){dn_future*f=dn_future_open(sizeof(dnc_select_future),_Alignof(dnc_select_future),dnc_select_poll,dnc_select_dispose);dnc_select_future*s=f->data;s->left=left;s->right=right;s->owner=f;dn_detach(left);dn_detach(right);return f;}

typedef struct dnc_channel_watch {
 dn_task *task;
 struct dnc_channel_watch *next;
} dnc_channel_watch;
typedef struct dn_channel {dnc_sync sync;size_t capacity,stride,head,count,senders,receivers,references;bool closed;dnc_channel_watch *watches;void*raw;uint8_t*data;void(*dispose)(void*);} dn_channel;
typedef struct dn_endpoint {dn_channel*channel;bool sender;} dn_endpoint;
static void dnc_channel_notify(dn_channel *channel) {
 dnc_signal(&channel->sync);
 while (channel->watches) {
  dnc_channel_watch *watch = channel->watches;
  channel->watches = watch->next;
  dn_task *task = watch->task;
  watch->task = NULL;
  watch->next = NULL;
  dnc_wake_task(task);
 }
}
static void dnc_channel_unwatch(dn_channel *channel, dnc_channel_watch *watch) {
 if (!watch->task) return;
 dnc_channel_watch **link = &channel->watches;
 while (*link && *link != watch) link = &(*link)->next;
 if (*link) *link = watch->next;
 watch->task = NULL;
 watch->next = NULL;
}
static void dnc_channel_watch_drop(dn_endpoint *endpoint, dnc_channel_watch *watch) {
 if (!endpoint || !endpoint->channel) return;
 dn_channel *channel = endpoint->channel;
 dnc_lock(&channel->sync);
 dnc_channel_unwatch(channel, watch);
 dnc_unlock(&channel->sync);
}
static void dnc_channel_arm(dn_channel *channel, dnc_channel_watch *watch) {
 watch->task = dnc_current_task;
 watch->next = channel->watches;
 channel->watches = watch;
}
static void dn_endpoint_finalize(void*raw){dn_endpoint*e=raw;if(!e->channel)return;dn_channel*c=e->channel;e->channel=NULL;
 size_t discarded=0,head=0;dnc_lock(&c->sync);if(e->sender)c->senders--;else{c->receivers--;if(!c->receivers){c->closed=true;discarded=c->count;head=c->head;c->count=0;}}
 /* Release this endpoint reference after nested message finalizers finish. */
 dnc_channel_notify(c);dnc_unlock(&c->sync);
 for(size_t i=0;i<discarded;i++)c->dispose(c->data+((head+i)%c->capacity)*c->stride);
 dnc_lock(&c->sync);bool last=--c->references==0;dnc_unlock(&c->sync);
 if(last){for(size_t i=0;i<c->count;i++)c->dispose(c->data+((c->head+i)%c->capacity)*c->stride);dnc_destroy(&c->sync);free(c->raw);free(c);}
}
static dn_endpoint *dn_channel_open(size_t capacity,size_t stride,size_t alignment,void(*dispose)(void*)){
 if(!capacity||capacity>1048576||!stride||stride>67108864/capacity)dn_fail("channel limits out of range");
 dn_endpoint*e=dn_allocate(sizeof *e);((dn_mem*)e-1)->finalize=dn_endpoint_finalize;
 dn_channel*c=calloc(1,sizeof *c);if(!c)dn_fail("out of memory");c->raw=calloc(1,capacity*stride+alignment);if(!c->raw){free(c);dn_fail("out of memory");}c->data=(uint8_t*)(((uintptr_t)c->raw+alignment-1)&~(uintptr_t)(alignment-1));
 if(!dnc_init(&c->sync)){free(c->raw);free(c);dn_fail("channel synchronization failed");}
 c->capacity=capacity;c->stride=stride;c->dispose=dispose;c->senders=1;c->references=1;e->channel=c;e->sender=true;return e;
}
static dn_endpoint *dn_endpoint_clone(dn_endpoint*source,bool sender){dn_nonnull(source);if(!source->channel)dn_fail("closed channel endpoint");
 dn_endpoint*e=dn_allocate(sizeof *e);dn_channel*c=source->channel;
 dnc_lock(&c->sync);if(!sender&&c->closed){dnc_unlock(&c->sync);dn_fail("closed channel endpoint");}c->references++;if(sender)c->senders++;else c->receivers++;dnc_channel_notify(c);dnc_unlock(&c->sync);
 e->channel=c;e->sender=sender;((dn_mem*)e-1)->finalize=dn_endpoint_finalize;return e;
}
/* send consumes its owned argument on both success and failure. */
static bool dn_channel_send(dn_endpoint*e,void*item,uint32_t ms){dn_nonnull(e);dn_channel*c=e->channel;dn_nonnull(c);dnc_lock(&c->sync);
 uint64_t start=ms&&ms!=UINT32_MAX?dnc_now():0;
 while(ms&&c->count==c->capacity&&c->receivers&&!dn_task_cancelled()){
  uint32_t delay=10;
  if(ms!=UINT32_MAX){uint64_t elapsed=dnc_now()-start;if(elapsed>=ms)break;if(ms-elapsed<delay)delay=(uint32_t)(ms-elapsed);}
  dnc_wait(&c->sync,delay);
 }
 bool ok=c->count<c->capacity&&c->receivers&&!c->closed&&!dn_task_cancelled();if(ok){memcpy(c->data+((c->head+c->count)%c->capacity)*c->stride,item,c->stride);memset(item,0,c->stride);c->count++;dnc_channel_notify(c);}dnc_unlock(&c->sync);if(!ok)c->dispose(item);return ok;
}
static bool dn_channel_receive(dn_endpoint*e,void*item,bool wait){dn_nonnull(e);dn_channel*c=e->channel;dn_nonnull(c);dnc_lock(&c->sync);
 while(wait&&!c->count&&c->senders&&!dn_task_cancelled())dnc_wait(&c->sync,10);
 bool ok=c->count&&!dn_task_cancelled();if(ok){memcpy(item,c->data+c->head*c->stride,c->stride);memset(c->data+c->head*c->stride,0,c->stride);c->head=(c->head+1)%c->capacity;c->count--;dnc_channel_notify(c);}dnc_unlock(&c->sync);return ok;
}

static unsigned dn_channel_poll_send(dn_endpoint *endpoint, void *item, dnc_channel_watch *watch) {
 dn_channel *channel = dn_nonnull(endpoint->channel);
 unsigned status;
 dnc_lock(&channel->sync);
 dnc_channel_unwatch(channel, watch);
 if (!channel->receivers || channel->closed) status = 2;
 else if (channel->count < channel->capacity) {
  size_t end = (channel->head + channel->count) % channel->capacity;
  memcpy(channel->data + end * channel->stride, item, channel->stride);
  memset(item, 0, channel->stride);
  channel->count++;
  dnc_channel_notify(channel);
  status = 1;
 } else {
  dnc_channel_arm(channel, watch);
  status = 0;
 }
 dnc_unlock(&channel->sync);
 return status;
}
static unsigned dn_channel_poll_receive(dn_endpoint *endpoint, void *item, dnc_channel_watch *watch) {
 dn_channel *channel = dn_nonnull(endpoint->channel);
 unsigned status;
 dnc_lock(&channel->sync);
 dnc_channel_unwatch(channel, watch);
 if (channel->count) {
  uint8_t *head = channel->data + channel->head * channel->stride;
  memcpy(item, head, channel->stride);
  memset(head, 0, channel->stride);
  channel->head = (channel->head + 1) % channel->capacity;
  channel->count--;
  dnc_channel_notify(channel);
  status = 1;
 } else if (!channel->senders) status = 2;
 else {
  dnc_channel_arm(channel, watch);
  status = 0;
 }
 dnc_unlock(&channel->sync);
 return status;
}

typedef struct dnc_shared {dnc_sync sync;_Atomic size_t references;unsigned readers,writers_waiting;bool writing,poisoned,active,rw;void*raw,*value;void(*dispose)(void*);void(*retain)(void*);} dnc_shared;
typedef struct dn_shared_handle {dnc_shared*state;} dn_shared_handle;
typedef struct dnc_held {dnc_shared*state;struct dnc_held*previous;} dnc_held;
static DN_TLS dnc_held*dnc_held_locks;
static void dn_shared_finalize(void*raw){dn_shared_handle*h=raw;if(!h->state)return;dnc_shared*s=h->state;h->state=NULL;if(atomic_fetch_sub_explicit(&s->references,1,memory_order_acq_rel)==1){if(s->active)s->dispose(s->value);dnc_destroy(&s->sync);free(s->raw);free(s);}}
static dn_shared_handle *dn_shared_open(size_t size,size_t alignment,void(*dispose)(void*),void(*retain)(void*),bool rw){
 dn_shared_handle*h=dn_allocate(sizeof *h);((dn_mem*)h-1)->finalize=dn_shared_finalize;dnc_shared*s=calloc(1,sizeof *s);if(!s)dn_fail("out of memory");if(!dnc_init(&s->sync)){free(s);dn_fail("shared lock initialization failed");}s->raw=calloc(1,size+alignment);if(!s->raw){dnc_destroy(&s->sync);free(s);dn_fail("out of memory");}s->value=(void*)(((uintptr_t)s->raw+alignment-1)&~(uintptr_t)(alignment-1));atomic_init(&s->references,1);s->dispose=dispose;s->retain=retain;s->rw=rw;h->state=s;return h;
}
static dn_shared_handle *dn_shared_clone(dn_shared_handle*source){dn_nonnull(source);dn_shared_handle*h=dn_allocate(sizeof *h);h->state=source->state;atomic_fetch_add_explicit(&h->state->references,1,memory_order_relaxed);((dn_mem*)h-1)->finalize=dn_shared_finalize;return h;}
static void dnc_shared_acquire(dnc_shared*s,bool write){for(dnc_held*h=dnc_held_locks;h;h=h->previous)if(h->state==s)dn_fail("recursive shared lock acquisition");
 dnc_lock(&s->sync);if(write)s->writers_waiting++;while((write?(s->writing||s->readers):(s->writing||s->writers_waiting))&&!dn_task_cancelled())dnc_wait(&s->sync,10);
 if(write)s->writers_waiting--;if(s->poisoned||dn_task_cancelled()){bool poison=s->poisoned;dnc_signal(&s->sync);dnc_unlock(&s->sync);dn_fail(poison?"shared lock poisoned":"task cancelled");}if(write)s->writing=true;else s->readers++;dnc_unlock(&s->sync);
}
static void dnc_shared_release(dnc_shared*s,bool write,bool failed){dnc_lock(&s->sync);if(write){s->writing=false;if(failed)s->poisoned=true;}else s->readers--;dnc_signal(&s->sync);dnc_unlock(&s->sync);}
static bool dn_shared_poisoned(dn_shared_handle*h){dnc_shared*s=h->state;dnc_lock(&s->sync);bool result=s->poisoned;dnc_unlock(&s->sync);return result;}
static uint8_t dn_shared_clear_poison(dn_shared_handle*h){dnc_shared*s=h->state;dnc_lock(&s->sync);s->poisoned=false;dnc_signal(&s->sync);dnc_unlock(&s->sync);return 0;}

static bool dn_sender_ready(dn_endpoint *endpoint) {
 dn_channel *channel=endpoint->channel;
 dnc_lock(&channel->sync);
 bool ready=!channel->closed && channel->receivers && channel->count<channel->capacity;
 dnc_unlock(&channel->sync);
 return ready;
}
static bool dn_receiver_connected(dn_endpoint *endpoint) {
 dn_channel *channel=endpoint->channel;
 dnc_lock(&channel->sync);
 bool open=channel->senders || channel->count;
 dnc_unlock(&channel->sync);
 return open;
}
static bool dn_sender_connected(dn_endpoint*e){dn_channel*c=e->channel;dnc_lock(&c->sync);bool connected=c->receivers&&!c->closed;dnc_unlock(&c->sync);return connected;}
