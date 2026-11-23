#include "../../../dlm_gateway/include/dlm_gateway.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(DLN_CONTINUITY_ACCEPTANCE)
extern uint64_t gateway_continuity_delivery(uint8_t*,size_t,uint64_t,uint64_t);
extern dln_gateway_descriptor gateway_continuity_abi(dln_gateway_descriptor);
_Static_assert(sizeof(dln_gateway_descriptor)==72,"delivery descriptor ABI");
_Static_assert(sizeof(dln_gateway_delivery_receipt)==96,"delivery receipt ABI");
_Static_assert(sizeof(dln_gateway_path_metrics)==88,"path metrics ABI");
_Static_assert(sizeof(dln_gateway_pool_stats)==80,"pool statistics ABI");
#endif
#if defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
typedef SOCKET socket_type;
#define close_socket closesocket
#define BAD_SOCKET INVALID_SOCKET
static DWORD WINAPI echo_thread(void *argument)
#else
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_type;
#define close_socket close
#define BAD_SOCKET (-1)
static void *echo_thread(void *argument)
#endif
{
    socket_type listener=*(socket_type*)argument;
    for(unsigned session=0;session<2;session++) {
        socket_type peer=accept(listener,NULL,NULL);assert(peer!=BAD_SOCKET);
        unsigned char bytes[32768];
        for(;;) { int n=(int)recv(peer,(char*)bytes,sizeof bytes,0);if(n<=0) break;int at=0;while(at<n) { int sent=(int)send(peer,(const char*)bytes+at,n-at,0);assert(sent>0);at+=sent; } }
        close_socket(peer);
    }
    return 0;
}
extern uint64_t gateway_dlm_exchange(uint8_t*,size_t,uint8_t*,size_t);
static char *load(const char *path) { FILE *file=fopen(path,"rb");assert(file);assert(!fseek(file,0,SEEK_END));long n=ftell(file);assert(n>=0 && n<=65536);rewind(file);char *text=calloc((size_t)n+1,1);assert(text && fread(text,1,(size_t)n,file)==(size_t)n);assert(!fclose(file));return text; }
static char *substitute(const char *source,const char *token,const char *value) { const char *at=strstr(source,token);assert(at && !strstr(at+strlen(token),token));size_t prefix=(size_t)(at-source),suffix=strlen(at+strlen(token));char *result=malloc(prefix+strlen(value)+suffix+1);assert(result);memcpy(result,source,prefix);strcpy(result+prefix,value);strcpy(result+prefix+strlen(value),at+strlen(token));return result; }
int main(int argc,char **argv) {
    assert(argc==3 && dln_gateway_abi()==0x10000);
#if defined(_WIN32)
    WSADATA data;assert(!WSAStartup(MAKEWORD(2,2),&data));
#endif
    socket_type listener=socket(AF_INET,SOCK_STREAM,0);assert(listener!=BAD_SOCKET);struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);assert(!bind(listener,(struct sockaddr*)&address,sizeof address) && !listen(listener,4));
#if defined(_WIN32)
    int size=sizeof address;
#else
    socklen_t size=sizeof address;
#endif
    assert(!getsockname(listener,(struct sockaddr*)&address,&size));char port[16],destination[64];snprintf(port,sizeof port,"%u",ntohs(address.sin_port));snprintf(destination,sizeof destination,"127.0.0.1:%s",port);
#if defined(_WIN32)
    HANDLE worker=CreateThread(NULL,0,echo_thread,&listener,0,NULL);assert(worker);
#else
    pthread_t worker;assert(!pthread_create(&worker,NULL,echo_thread,&listener));
#endif
    char *original=load(argv[1]),*server_config=substitute(original,"__ECHO_PORT__",port);free(original);dln_gateway_opened server=dln_gateway_server_open((uint8_t*)server_config,strlen(server_config));if(server.status) fprintf(stderr,"server status %d/%d\n",server.status,server.system);assert(!server.status && server.handle);free(server_config);
    uint8_t local[128]={0};dln_gateway_io local_result=dln_gateway_server_address(server.handle,local,sizeof local-1);assert(!local_result.status && local_result.count<sizeof local);original=load(argv[2]);char *client_config=substitute(original,"__GATEWAY__",(char*)local);free(original);
    dln_gateway_opened client=dln_gateway_client_open((uint8_t*)client_config,strlen(client_config));assert(!client.status && client.handle);dln_gateway_io stream=dln_gateway_stream_open(client.handle,1,(uint8_t*)destination,strlen(destination));assert(!stream.status && stream.count>0);uint32_t id=(uint32_t)stream.count;uint8_t binary[]={65,0,255};dln_gateway_io written=dln_gateway_stream_write(client.handle,id,binary,sizeof binary);assert(!written.status && written.count==sizeof binary);
    dln_gateway_transfer bad=dln_gateway_stream_read(client.handle,id,3,NULL,1);assert(bad.status==4);uint8_t small=165;dln_gateway_transfer short_result=dln_gateway_stream_read(client.handle,id,3,&small,1);assert(short_result.status==10 && short_result.required==3 && short_result.copied==0 && small==165);uint8_t output[4]={0,0,0,165};dln_gateway_transfer received=dln_gateway_take(client.handle,output,sizeof output);assert(!received.status && received.copied==3 && received.outcome==3 && !memcmp(output,binary,3) && output[3]==165);assert(dln_gateway_take(client.handle,output,sizeof output).status==1);assert(!dln_gateway_stream_close(client.handle,id,false).status);dln_gateway_client_close(client.handle);
    uint64_t dlm_result=gateway_dlm_exchange((uint8_t*)client_config,strlen(client_config),(uint8_t*)destination,strlen(destination));if(dlm_result!=63) fprintf(stderr,"DLM gateway result %llu\n",(unsigned long long)dlm_result);assert(dlm_result==63);
#if defined(DLN_CONTINUITY_ACCEPTANCE)
    const char *field=strstr(client_config,"transport=");assert(field);field+=10;size_t kind_length=strcspn(field,"\r\n");assert(kind_length>0 && kind_length<32);char kind[32]={0};memcpy(kind,field,kind_length);size_t pool_capacity=strlen(client_config)+256;char *pool_config=malloc(pool_capacity);assert(pool_config);int config_length=snprintf(pool_config,pool_capacity,"%s\npath=%s,%s,*,loopback,local,local,local,0\n",client_config,kind,(char*)local);assert(config_length>0 && (size_t)config_length<pool_capacity);
    uint64_t durable=gateway_continuity_delivery((uint8_t*)pool_config,(size_t)config_length,(uint64_t)time(NULL)+3600,7);if(durable!=127) fprintf(stderr,"DLM durable continuity result %llu\n",(unsigned long long)durable);assert(durable==127);free(pool_config);
    dln_gateway_descriptor shape={0};shape.stream=UINT64_C(0x1122334455667788);shape.length=UINT64_C(0x8877665544332211);shape.expires=UINT64_C(0x0102030405060708);for(unsigned i=0;i<16;i++)shape.id[i]=(uint8_t)(i*17);for(unsigned i=0;i<32;i++)shape.digest[i]=(uint8_t)(255-i*7);dln_gateway_descriptor echoed=gateway_continuity_abi(shape);assert(!memcmp(&shape,&echoed,sizeof shape));
#endif
    free(client_config);
#if defined(_WIN32)
    assert(WaitForSingleObject(worker,10000)==WAIT_OBJECT_0);CloseHandle(worker);
#else
    assert(!pthread_join(worker,NULL));
#endif
    close_socket(listener);dln_gateway_statistics stats=dln_gateway_server_statistics(server.handle);assert(stats.forwarded==6 && stats.returned==6);
#if defined(DLN_CONTINUITY_ACCEPTANCE)
    assert(stats.authenticated==3);
#else
    assert(stats.authenticated==2);
#endif
    dln_gateway_server_close(server.handle);
#if defined(DLN_CONTINUITY_ACCEPTANCE)
    puts("{\"case\":\"gateway_dlm_continuity\",\"status\":\"PASS\",\"binary_bytes\":6,\"durable_bytes\":3,\"retained_short_output\":true}");return 0;
#else
    puts("{\"case\":\"gateway_dlm_ffi\",\"status\":\"PASS\",\"binary_bytes\":6,\"authenticated\":2,\"retained_short_output\":true}");return 0;
#endif
}
