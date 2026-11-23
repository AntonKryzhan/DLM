#include "../../../dlm_gateway/include/dlm_gateway.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
typedef SOCKET socket_type;
#define close_socket closesocket
#define BAD_SOCKET INVALID_SOCKET
static DWORD WINAPI echo_thread(void *argument)
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
typedef int socket_type;
#define close_socket close
#define BAD_SOCKET (-1)
static void *echo_thread(void *argument)
#endif
{
 socket_type socket=*(socket_type*)argument;
 for(unsigned i=0;i<3;i++) {
  unsigned char bytes[65535];struct sockaddr_storage peer;
#if defined(_WIN32)
  int length=sizeof peer;
#else
  socklen_t length=sizeof peer;
#endif
  int n=(int)recvfrom(socket,(char*)bytes,sizeof bytes,0,(struct sockaddr*)&peer,&length);assert(n>=0);int sent=(int)sendto(socket,(const char*)bytes,n,0,(struct sockaddr*)&peer,length);assert(sent==n);
 }
 return 0;
}
extern uint64_t gateway_proxy_dlm(uint8_t*,size_t,uint8_t*,size_t,uint32_t);
extern dln_gateway_proxy_statistics gateway_proxy_abi(dln_gateway_proxy_statistics);
extern dln_gateway_ip_assignment gateway_proxy_assignment_abi(dln_gateway_ip_assignment);
extern dln_gateway_ip_route gateway_proxy_route_abi(dln_gateway_ip_route);
_Static_assert(sizeof(dln_gateway_proxy_statistics)==72,"proxy statistics ABI");
_Static_assert(sizeof(dln_gateway_ip_assignment)==24,"IP assignment ABI");
_Static_assert(sizeof(dln_gateway_ip_route)==40,"IP route ABI");
static char *load(const char *path) { FILE *file=fopen(path,"rb");assert(file);assert(!fseek(file,0,SEEK_END));long n=ftell(file);assert(n>=0 && n<=65536);rewind(file);char *text=calloc((size_t)n+1,1);assert(text && fread(text,1,(size_t)n,file)==(size_t)n);assert(!fclose(file));return text; }
static char *substitute(const char *source,const char *token,const char *value) { const char *at=strstr(source,token);assert(at && !strstr(at+strlen(token),token));size_t prefix=(size_t)(at-source),suffix=strlen(at+strlen(token));char *result=malloc(prefix+strlen(value)+suffix+1);assert(result);memcpy(result,source,prefix);strcpy(result+prefix,value);strcpy(result+prefix+strlen(value),at+strlen(token));return result; }
int main(int argc,char **argv) {
 assert(argc==3 && dln_gateway_abi()==0x10000);
#if defined(_WIN32)
 WSADATA data;assert(!WSAStartup(MAKEWORD(2,2),&data));
#endif
 socket_type socket_handle=socket(AF_INET,SOCK_DGRAM,0);assert(socket_handle!=BAD_SOCKET);struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);assert(!bind(socket_handle,(struct sockaddr*)&a,sizeof a));
#if defined(_WIN32)
 int length=sizeof a;HANDLE worker=CreateThread(NULL,0,echo_thread,&socket_handle,0,NULL);assert(worker);
#else
 socklen_t length=sizeof a;pthread_t worker;assert(!pthread_create(&worker,NULL,echo_thread,&socket_handle));
#endif
 assert(!getsockname(socket_handle,(struct sockaddr*)&a,&length));char port[16],target[64];snprintf(port,sizeof port,"%u",ntohs(a.sin_port));snprintf(target,sizeof target,"127.0.0.1:%s",port);
 char *original=load(argv[1]),*settings=substitute(original,"__ECHO_PORT__",port);free(original);dln_gateway_opened server=dln_gateway_proxy_server_open((uint8_t*)settings,strlen(settings));if(server.status)fprintf(stderr,"proxy server %d/%d\n",server.status,server.system);assert(!server.status && server.handle);free(settings);
 uint8_t address[128]={0};dln_gateway_io bound=dln_gateway_proxy_server_address(server.handle,address,sizeof address-1);assert(!bound.status && bound.count<sizeof address);original=load(argv[2]);settings=substitute(original,"__PROXY__",(char*)address);free(original);
 dln_gateway_opened client=dln_gateway_proxy_open((uint8_t*)settings,strlen(settings),2,(uint8_t*)target,strlen(target));assert(!client.status && client.handle);uint8_t binary[]={65,0,255};assert(!dln_gateway_proxy_write(client.handle,binary,3).status);
 assert(dln_gateway_proxy_read(client.handle,65535,NULL,1).status==4);uint8_t small=165;dln_gateway_transfer short_result=dln_gateway_proxy_read(client.handle,65535,&small,1);assert(short_result.status==10 && short_result.required==3 && small==165);uint8_t output[4]={0,0,0,165};dln_gateway_transfer received=dln_gateway_proxy_take(client.handle,output,sizeof output);assert(!received.status && received.copied==3 && !memcmp(output,binary,3) && output[3]==165);
 assert(!dln_gateway_proxy_write(client.handle,NULL,0).status);dln_gateway_transfer empty=dln_gateway_proxy_read(client.handle,65535,NULL,0);assert(!empty.status && !empty.required && !empty.copied);assert(!dln_gateway_proxy_assignments(client.handle,NULL,0).status && !dln_gateway_proxy_routes(client.handle,NULL,0).status);
 dln_gateway_opened cancel=dln_gateway_proxy_cancel_open(client.handle);assert(!cancel.status && cancel.handle);assert(!dln_gateway_proxy_cancel_request(cancel.handle).status);assert(dln_gateway_proxy_read(client.handle,65535,output,sizeof output).status==9);dln_gateway_proxy_cancel_close(cancel.handle);dln_gateway_proxy_close(client.handle);
 uint64_t checked=gateway_proxy_dlm((uint8_t*)settings,strlen(settings),(uint8_t*)target,strlen(target),2);if(checked!=31)fprintf(stderr,"DLM proxy %llu\n",(unsigned long long)checked);assert(checked==31);free(settings);
 dln_gateway_proxy_statistics shape={0};shape.accepted=UINT64_C(0x8877665544332211);shape.failed=UINT64_MAX;dln_gateway_proxy_statistics echoed=gateway_proxy_abi(shape);assert(!memcmp(&shape,&echoed,sizeof shape));dln_gateway_ip_assignment assignment={4,32,{10,1,0,2}};dln_gateway_ip_assignment assigned=gateway_proxy_assignment_abi(assignment);assert(!memcmp(&assignment,&assigned,sizeof assignment));dln_gateway_ip_route route={6,17,{0},{0}};route.first[0]=32;route.last[15]=255;dln_gateway_ip_route routed=gateway_proxy_route_abi(route);assert(!memcmp(&route,&routed,sizeof route));
#if defined(_WIN32)
 assert(WaitForSingleObject(worker,10000)==WAIT_OBJECT_0);CloseHandle(worker);
#else
 assert(!pthread_join(worker,NULL));
#endif
 close_socket(socket_handle);dln_gateway_proxy_statistics stats=dln_gateway_proxy_server_statistics(server.handle);assert(stats.authenticated==2 && stats.udp==2 && stats.ip==0 && stats.incoming==6 && stats.outgoing==6);dln_gateway_proxy_server_close(server.handle);
 puts("{\"case\":\"proxy_dlm_ffi\",\"status\":\"PASS\",\"binary_bytes\":6,\"empty_datagram\":true,\"cancellation\":true,\"retained_short_output\":true,\"ip_assignment_layout\":24,\"ip_route_layout\":40}");return 0;
}
