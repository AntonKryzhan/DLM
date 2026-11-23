#include "../../../dlm_channel_lab/include/dlm_channel_lab.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern uint64_t channel_lab_dlm(uint8_t*,size_t);
extern dlm_lab_summary channel_lab_summary_abi(dlm_lab_summary);
extern dlm_lab_event channel_lab_event_abi(dlm_lab_event);
extern dlm_lab_transfer channel_lab_transfer_abi(dlm_lab_transfer);
_Static_assert(sizeof(dlm_lab_opened)==16,"opened ABI");
_Static_assert(sizeof(dlm_lab_summary)==56,"summary ABI");
_Static_assert(sizeof(dlm_lab_event)==16,"event ABI");
_Static_assert(sizeof(dlm_lab_transfer)==24,"transfer ABI");
int main(void) {
 assert(dlm_channel_lab_abi()==0x10000);
 const char *modes[]={"absolute","interval","presence","count","protocol","order","size","hybrid"};
 for(size_t mode=0;mode<8;mode++) {
  char config[128];int n=snprintf(config,sizeof config,"mode=%s\n",modes[mode]);assert(n>0 && (size_t)n<sizeof config);assert(channel_lab_dlm((uint8_t*)config,(size_t)n)==63);
  dlm_lab_opened lab=dlm_channel_lab_open((uint8_t*)config,(size_t)n);assert(!lab.status && lab.handle);uint8_t payload[]={65,0,255};dlm_lab_io run=dlm_channel_lab_simulate(lab.handle,payload,sizeof payload);assert(!run.status && run.count==1);
  dlm_lab_summary summary=dlm_channel_lab_summary(lab.handle);assert(summary.delivery==1 && summary.mode==mode+1 && summary.payload_bytes==3 && summary.decoded_bytes==3 && summary.events>0);dlm_lab_summary copied=channel_lab_summary_abi(summary);assert(!memcmp(&summary,&copied,sizeof summary));
  uint8_t short_output=165;dlm_lab_transfer small=dlm_channel_lab_read(lab.handle,2,&short_output,1);assert(small.status==10 && small.required==3 && !small.copied && short_output==165);dlm_lab_transfer roundtrip=channel_lab_transfer_abi(small);assert(!memcmp(&small,&roundtrip,sizeof small));uint8_t output[4]={0,0,0,165};dlm_lab_transfer complete=dlm_channel_lab_read(lab.handle,2,output,sizeof output);assert(!complete.status && complete.copied==3 && output[3]==165 && !memcmp(output,payload,3));
  dlm_lab_transfer probe=dlm_channel_lab_read(lab.handle,1,NULL,0);assert(probe.status==10 && probe.required>76);uint8_t *capture=malloc((size_t)probe.required);assert(capture);complete=dlm_channel_lab_read(lab.handle,1,capture,(size_t)probe.required);assert(!complete.status && complete.copied==probe.required);dlm_lab_opened decoded=dlm_channel_lab_decode(capture,(size_t)probe.required);assert(!decoded.status);complete=dlm_channel_lab_read(decoded.handle,2,output,sizeof output);assert(!complete.status && !memcmp(output,payload,3));dlm_channel_lab_close(decoded.handle);capture[0]^=1;decoded=dlm_channel_lab_decode(capture,(size_t)probe.required);assert(decoded.status && !decoded.handle);free(capture);
  dlm_lab_event event={0};assert(!dlm_channel_lab_event(lab.handle,0,&event).status);dlm_lab_event event_copy=channel_lab_event_abi(event);assert(!memcmp(&event,&event_copy,sizeof event));assert(dlm_channel_lab_event(lab.handle,(size_t)summary.events,&event).status==4);assert(dlm_channel_lab_read(lab.handle,7,NULL,0).status==4);
  assert(dlm_channel_lab_simulate(lab.handle,NULL,1).status==4);assert(dlm_channel_lab_read(lab.handle,2,output,sizeof output).status==19);assert(!dlm_channel_lab_simulate(lab.handle,NULL,0).status);assert(dlm_channel_lab_read(lab.handle,2,NULL,0).status==0);
  dlm_lab_opened cancel=dlm_channel_lab_cancellation(lab.handle);assert(!cancel.status);dlm_channel_lab_close(lab.handle);assert(!dlm_channel_lab_cancel(cancel.handle).status);dlm_channel_lab_cancel_close(cancel.handle);
 }
 assert(dlm_channel_lab_open(NULL,1).status==4);assert(dlm_channel_lab_read(NULL,0,NULL,0).status==4);dlm_channel_lab_close(NULL);
 puts("{\"case\":\"experimental_dlm_c_abi\",\"status\":\"PASS\",\"modes\":8,\"binary_and_empty_payloads\":true,\"retained_short_buffers\":true,\"independent_capture_decode\":true,\"cancellation_lifetime\":true}");return 0;
}
