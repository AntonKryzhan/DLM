#include <assert.h>
#include "../../src/native/network.c"

int main(void) {
    dnn_open opened=dlm_net_reactor_new(2,4,64);assert(opened.status==DNN_OK);
    dnr_reactor *r=(dnr_reactor*)opened.handle;
    dnn_addr address;memset(&address,0,sizeof address);address.family=4;address.bytes[0]=127;address.bytes[3]=1;
    dnn_open socket=dlm_net_bind_address(address,3,0,false);assert(socket.status==DNN_OK);
    dnn_resolved local=dlm_net_local(socket.handle);assert(local.status==DNN_OK);
    dnr_request adopted=dlm_net_reactor_adopt(r,socket,DNN_UDP);assert(adopted.status==DNN_OK);
    for(unsigned int iteration=0;iteration<300;iteration++) {
        uint8_t bytes[3]={0,255,128};
        dnr_request reading=dlm_net_reactor_submit(r,adopted.channel,DNR_RECEIVE,NULL,8,false,1000,address);
        dnr_request writing=dlm_net_reactor_submit(r,adopted.channel,DNR_SEND,bytes,3,false,1000,local.address);
        assert(reading.status==DNN_OK && writing.status==DNN_OK);
        unsigned int done=0;
        for(unsigned int turn=0;turn<100 && done<2;turn++) {
            dnn_io wait=dlm_net_reactor_poll(r,1000,NULL);assert(wait.status==DNN_OK);
            dnr_completion result=dlm_net_reactor_result(r,0);assert(result.status==DNN_OK && result.count==3);
            if(result.kind==DNR_RECEIVE) {
                uint8_t target[4]={1,1,1,1};dnn_io copied=dlm_net_reactor_copy(r,result.id,0,target,4);
                assert(copied.status==DNN_OK && copied.count==3);
                assert(target[0]==0 && target[1]==255 && target[2]==128 && target[3]==1);
            }
            assert(dlm_net_reactor_ack(r,result.id).status==DNN_OK);done++;
        }
        assert(done==2);assert(dlm_net_reactor_stats(r).buffered_bytes==0);
    }
    dnr_request pending=dlm_net_reactor_submit(r,adopted.channel,DNR_RECEIVE,NULL,64,false,10000,address);
    assert(pending.status==DNN_OK);
    void *cancel=dlm_net_cancel_new();assert(cancel);dlm_net_cancel_signal(cancel);
    assert(dlm_net_reactor_poll(r,1000,cancel).status==DNN_CANCELLED);
    assert(dlm_net_reactor_result(r,pending.id).status==DNN_CANCELLED);
    dlm_net_cancel_close(cancel);dlm_net_reactor_close(r);
    assert(dlm_net_provider_shutdown().status==DNN_OK);return 0;
}
