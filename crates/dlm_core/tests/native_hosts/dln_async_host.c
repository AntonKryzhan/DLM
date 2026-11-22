#include <assert.h>
#include "../../src/native/network.c"
#if !defined(_WIN32)
#include <pthread.h>
#endif

typedef struct { void *waker; uint32_t delay, iterations; _Atomic uint32_t started, rejected; } wake_job;
static void pause_ms(uint32_t ms) {
#if defined(_WIN32)
    Sleep(ms);
#else
    struct timespec delay={ms/1000,(long)(ms%1000)*1000000};
    while(nanosleep(&delay,&delay) && errno==EINTR) {}
#endif
}
static void run_wakes(wake_job *job) {
    atomic_store(&job->started,1);pause_ms(job->delay);
    for(uint32_t i=0;i<job->iterations;i++) {
        dnn_io result=dlm_net_async_wake(job->waker);
        assert(result.status==DNN_OK || result.status==DNN_CANCELLED);
        if(result.status==DNN_CANCELLED) atomic_fetch_add(&job->rejected,1);
    }
}
#if defined(_WIN32)
typedef HANDLE test_thread;
static DWORD WINAPI wake_thread(void *argument) { run_wakes((wake_job*)argument);return 0; }
static test_thread start_thread(wake_job *job) { HANDLE thread=CreateThread(NULL,0,wake_thread,job,0,NULL);assert(thread);return thread; }
static void join_thread(test_thread thread) { assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);assert(CloseHandle(thread)); }
#else
typedef pthread_t test_thread;
static void *wake_thread(void *argument) { run_wakes((wake_job*)argument);return NULL; }
static test_thread start_thread(wake_job *job) { pthread_t thread;assert(!pthread_create(&thread,NULL,wake_thread,job));return thread; }
static void join_thread(test_thread thread) { assert(!pthread_join(thread,NULL)); }
#endif
static dnn_addr loopback(void) {
    dnn_addr address;memset(&address,0,sizeof address);address.family=4;address.bytes[0]=127;address.bytes[3]=1;return address;
}
static void *new_scope(uint32_t channels,uint32_t operations,size_t bytes,uint32_t wakes) {
    dnn_open opened=dlm_net_async_new(channels,operations,bytes,wakes);assert(opened.status==DNN_OK);return opened.handle;
}
static void *new_waker(void *scope,uint64_t operation,uint32_t action) {
    dnn_open result=dlm_net_async_waker(scope,operation,action);assert(result.status==DNN_OK);return result.handle;
}
static void test_thread_event_and_late_token(void) {
    void *group=new_scope(1,4,0,2);
    dnr_request event=dlm_net_reactor_timer(group,3000,true);assert(event.status==DNN_OK);
    void *waker=new_waker(group,event.id,1);
    wake_job job={.waker=waker,.delay=20,.iterations=1};
    test_thread thread=start_thread(&job);uint64_t begin=dlm_net_clock_ms();
    dnn_io result=dlm_net_reactor_poll(group,3000,NULL);
    assert(result.status==DNN_OK && dlm_net_clock_ms()-begin<2000);
    assert(dlm_net_reactor_result(group,event.id).status==DNN_OK);join_thread(thread);
    dlm_net_async_close(group);
    assert(dlm_net_async_wake(waker).status==DNN_CANCELLED);
    assert(dlm_net_provider_shutdown().status==DNN_INVALID);
    dlm_net_async_waker_close(waker);assert(dlm_net_provider_shutdown().status==DNN_OK);
}
static void test_bounded_queue_and_join_retained_results(void) {
    void *group=new_scope(1,4,0,1);
    dnr_request a=dlm_net_reactor_timer(group,3000,true),b=dlm_net_reactor_timer(group,3000,true);
    dnr_request timer=dlm_net_reactor_timer(group,10,false);assert(timer.status==DNN_OK);
    void *wa=new_waker(group,a.id,1),*wb=new_waker(group,b.id,1);
    assert(dlm_net_async_wake(wa).count==1);assert(dlm_net_async_wake(wa).count==0);
    assert(dlm_net_async_wake(wb).status==DNN_LIMIT);
    dnx_stats stats=dlm_net_async_stats(group);assert(stats.queued==1 && stats.queue_limit==1);
    assert(dlm_net_async_join(group,1,NULL).status==DNN_TIMEOUT);
    assert(dlm_net_reactor_result(group,a.id).status==DNN_OK);
    assert(dlm_net_async_wake(wb).status==DNN_OK);
    dnn_io joined=dlm_net_async_join(group,3000,NULL);assert(joined.status==DNN_OK && joined.count==3);
    stats=dlm_net_async_stats(group);assert(!stats.reactor.pending && !stats.kernel_pending);
    assert(dlm_net_reactor_ack(group,b.id).status==DNN_OK);
    assert(dlm_net_reactor_result(group,0).id==a.id);
    assert(dlm_net_reactor_ack(group,a.id).status==DNN_OK);
    assert(dlm_net_reactor_result(group,0).id==timer.id);
    dlm_net_async_waker_close(wa);dlm_net_async_waker_close(wb);dlm_net_async_close(group);
}
static void test_udp_cancel_and_kernel_retirement(void) {
    void *group=new_scope(1,4,65536,2);
    dnn_open socket=dlm_net_bind_address(loopback(),DNN_UDP,0,false);assert(socket.status==DNN_OK);
    dnr_request adopted=dlm_net_async_adopt(group,socket,DNN_UDP);assert(adopted.status==DNN_OK);
    dnr_request reading=dlm_net_reactor_submit(group,adopted.channel,DNR_RECEIVE,NULL,65536,false,3000,loopback());
    assert(reading.status==DNN_OK);
    dnr_request timer=dlm_net_reactor_timer(group,0,false);assert(timer.status==DNN_OK);
    void *waker=new_waker(group,reading.id,2);
    wake_job job={.waker=waker,.delay=20,.iterations=1};test_thread thread=start_thread(&job);
    dnn_io joined=dlm_net_async_join(group,3000,NULL);assert(joined.status==DNN_OK && joined.count==2);
    assert(dlm_net_reactor_result(group,reading.id).status==DNN_CANCELLED);
    assert(dlm_net_async_stats(group).kernel_pending==0);
    assert(dlm_net_reactor_ack(group,reading.id).status==DNN_OK);
    assert(dlm_net_async_stats(group).reactor.buffered_bytes==0);
    join_thread(thread);dlm_net_async_waker_close(waker);dlm_net_async_close(group);
}
static void test_signal_close_race(void) {
    for(uint32_t iteration=0;iteration<100;iteration++) {
        void *group=new_scope(1,1,0,1);void *waker=new_waker(group,0,3);
        wake_job job={.waker=waker,.iterations=10000};test_thread thread=start_thread(&job);
        while(!atomic_load(&job.started)) {}
        dlm_net_async_close(group);join_thread(thread);
        assert(dlm_net_async_wake(waker).status==DNN_CANCELLED);
        dlm_net_async_waker_close(waker);assert(dlm_net_provider_shutdown().status==DNN_OK);
    }
}
static void test_bounded_kernel_notifications(void) {
    dnr_reactor *group=(dnr_reactor*)new_scope(1,1,0,1);void *waker=new_waker(group,0,3);
    wake_job job={.waker=waker,.iterations=50000};test_thread thread=start_thread(&job);
    for(uint32_t iteration=0;iteration<1000;iteration++) {
        dnx_commands(group);dnx_lock(group->control);
        assert(!group->control->count || group->control->notified);
        dnx_unlock(group->control);
    }
    join_thread(thread);dnx_commands(group);
#if defined(_WIN32)
    assert(dnx_wait(group,0)<=1);assert(dnx_wait(group,0)==0);
#else
    uint8_t bytes[64];ssize_t count=read(group->control->reader,bytes,sizeof bytes);
    assert(count<=1);
#endif
    dlm_net_async_waker_close(waker);dlm_net_async_close(group);
}
static void test_pending_close_and_udp_boundaries(void) {
    for(uint32_t iteration=0;iteration<100;iteration++) {
        void *group=new_scope(1,4,32,1);
        dnn_open socket=dlm_net_bind_address(loopback(),DNN_UDP,0,false);assert(socket.status==DNN_OK);
        dnn_resolved local=dlm_net_local(socket.handle);assert(local.status==DNN_OK);
        dnr_request adopted=dlm_net_async_adopt(group,socket,DNN_UDP);assert(adopted.status==DNN_OK);
        dnr_request reading=dlm_net_reactor_submit(group,adopted.channel,DNR_RECEIVE,NULL,2,false,3000,loopback());
        uint8_t payload[4]={0,255,128,42};
        dnr_request writing=dlm_net_reactor_submit(group,adopted.channel,DNR_SEND,payload,4,false,3000,local.address);
        assert(reading.status==DNN_OK && writing.status==DNN_OK);
        assert(dlm_net_async_join(group,3000,NULL).status==DNN_OK);
        dnr_completion result=dlm_net_reactor_result(group,reading.id);assert(result.status==DNN_TRUNCATED && result.count==2);
        uint8_t output[4]={1,1,1,1};assert(dlm_net_reactor_copy(group,reading.id,0,output,4).count==2);
        assert(output[0]==0 && output[1]==255 && output[2]==1);
        assert(dlm_net_reactor_ack(group,reading.id).status==DNN_OK);
        assert(dlm_net_reactor_ack(group,writing.id).status==DNN_OK);
        reading=dlm_net_reactor_submit(group,adopted.channel,DNR_RECEIVE,NULL,0,false,3000,loopback());
        writing=dlm_net_reactor_submit(group,adopted.channel,DNR_SEND,payload,1,false,3000,local.address);
        assert(reading.status==DNN_OK && writing.status==DNN_OK);
        assert(dlm_net_async_join(group,3000,NULL).status==DNN_OK);
        result=dlm_net_reactor_result(group,reading.id);assert(result.status==DNN_TRUNCATED && result.count==0);
        assert(dlm_net_reactor_ack(group,reading.id).status==DNN_OK);
        assert(dlm_net_reactor_ack(group,writing.id).status==DNN_OK);
        reading=dlm_net_reactor_submit(group,adopted.channel,DNR_RECEIVE,NULL,32,false,3000,loopback());assert(reading.status==DNN_OK);
        dlm_net_async_close(group);assert(dlm_net_provider_shutdown().status==DNN_OK);
    }
}
int main(void) {
    assert(dlm_net_async_new(1,1,0,0).status==DNN_INVALID);
    assert(dlm_net_async_new(1,1,0,4097).status==DNN_INVALID);
    void *group=new_scope(1,1,0,1);uint32_t backend=dlm_net_async_stats(group).reactor.backend;dlm_net_async_close(group);
#if defined(_WIN32)
    assert(backend==5);
#elif defined(DLM_REACTOR_FORCE_POLL)
    assert(backend==4);
#endif
    test_thread_event_and_late_token();test_bounded_queue_and_join_retained_results();
    test_udp_cancel_and_kernel_retirement();test_signal_close_race();test_bounded_kernel_notifications();test_pending_close_and_udp_boundaries();
    assert(dlm_net_provider_shutdown().status==DNN_OK);
    printf("scoped threads queue cancel and close OK; backend=%u\n",(unsigned)backend);return 0;
}
