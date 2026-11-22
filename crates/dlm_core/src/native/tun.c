#ifndef DLM_NATIVE_TUN_PROVIDER
#define DLM_NATIVE_TUN_PROVIDER
#define DNU_MAGIC UINT64_C(0x444c4e54554e3031)
#define DNU_GRANT UINT64_C(0x444c4e47524e5431)
#define DNU_BRIDGE UINT64_C(0x444c4e4252473031)
#define DNU_PACKET_MAX 65571u
#define DNU_SCRATCH 65575u
#if defined(__linux__) || defined(__FreeBSD__)
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <net/if.h>
#if defined(__linux__)
#include <linux/if_tun.h>
#else
#include <net/if_tun.h>
#include <netinet6/in6_var.h>
#endif
#elif defined(_WIN32)
#include <iphlpapi.h>
#include <netioapi.h>
#endif
typedef struct { uint32_t kind,mode,mtu,max_packet,ring_capacity;size_t memory;bool configure;dnn_addr ipv4,ipv6;uint32_t ipv4_prefix,ipv6_prefix; } dnu_config;
typedef struct { uint64_t magic;uint32_t flags; } dnu_grant;
typedef struct { size_t required,copied;uint32_t family,ether_type,offset;int32_t status,system; } dnu_packet;
typedef struct { uint64_t received,sent,dropped,malformed;size_t used,limit,pending;uint32_t backend,kind,mtu,index;int32_t status,system; } dnu_stats;
#if defined(_WIN32)
typedef struct {
    HMODULE module,ipmodule;void *(WINAPI *create)(const WCHAR*,const WCHAR*,const GUID*);void *(WINAPI *open)(const WCHAR*);void (WINAPI *close)(void*);void (WINAPI *luid)(void*,NET_LUID*);
    void *(WINAPI *start)(void*,DWORD);void (WINAPI *end)(void*);HANDLE (WINAPI *event)(void*);BYTE *(WINAPI *receive)(void*,DWORD*);void (WINAPI *release)(void*,const BYTE*);BYTE *(WINAPI *allocate)(void*,DWORD);void (WINAPI *send)(void*,const BYTE*);
    void (WINAPI *init_address)(PMIB_UNICASTIPADDRESS_ROW);DWORD (WINAPI *create_address)(const MIB_UNICASTIPADDRESS_ROW*);DWORD (WINAPI *delete_address)(const MIB_UNICASTIPADDRESS_ROW*);
    void (WINAPI *init_interface)(PMIB_IPINTERFACE_ROW);DWORD (WINAPI *get_interface)(PMIB_IPINTERFACE_ROW);DWORD (WINAPI *set_interface)(PMIB_IPINTERFACE_ROW);
} dnu_windows;
#endif
typedef struct {
    uint64_t magic,received,sent,dropped,malformed;dnu_config config;size_t used,pending;uint32_t backend,index;bool owned;int fd;char name[64];uint8_t *buffer;
#if defined(_WIN32)
    dnu_windows api;void *adapter,*session;HANDLE event;NET_LUID luid;MIB_UNICASTIPADDRESS_ROW addresses[2];bool added[2];
#endif
} dnu_device;
typedef struct { uint64_t magic,outgoing,incoming,rejected,blocked;dnu_device *device;dna_link *link;uint32_t channel;size_t incoming_count;uint8_t packet[DNU_PACKET_MAX]; } dnu_bridge;
typedef struct { uint64_t outgoing,incoming,rejected,blocked;size_t pending,used;uint32_t authenticated;int32_t status,system; } dnu_bridge_stats;
static void dnu_inc(uint64_t *value) { if(*value!=UINT64_MAX) (*value)++; }
static dnu_device *dnu_get(void *handle) { dnu_device *d=handle;return d && d->magic==DNU_MAGIC?d:NULL; }
static bool dnu_config_valid(dnu_config c) { return (c.kind==1 || c.kind==2) && (c.mode==1 || c.mode==2) && c.mtu>=576 && c.mtu<=65535 && c.max_packet>=c.mtu+(c.kind==2?22u:0u) && c.max_packet<=DNU_PACKET_MAX && c.memory>=sizeof(dnu_device)+DNU_SCRATCH && c.memory<=DNN_MAX && (!c.ipv4.family || (c.ipv4.family==4 && !c.ipv4.port && !c.ipv4.scope && !dnp_zero(c.ipv4.bytes,4) && dnp_zero(c.ipv4.bytes+4,12) && c.ipv4_prefix<=32)) && (!c.ipv6.family || (c.ipv6.family==6 && !c.ipv6.port && !dnp_zero(c.ipv6.bytes,16) && c.ipv6_prefix<=128 && c.mtu>=1280)); }
dnn_open dlm_tun_permission(uint32_t flags) { if(!(flags&1) || flags&~3u) return dnn_open_error(DNN_INVALID,0);dnu_grant *g=calloc(1,sizeof *g);if(!g) return dnn_open_error(DNN_LIMIT,0);g->magic=DNU_GRANT;g->flags=flags;return (dnn_open){g,0,0}; }
void dlm_tun_permission_close(void *handle) { dnu_grant *g=handle;if(!g || g->magic!=DNU_GRANT) return;g->magic=0;free(g); }
dnu_packet dlm_tun_packet(uint32_t kind,const uint8_t *bytes,size_t count) {
    dnu_packet r={count,0,0,0,0,0,0};if(!bytes || !count || count>DNU_PACKET_MAX || (kind!=1 && kind!=2)) { r.status=DNN_INVALID;return r; }size_t offset=0,available=count;
    if(kind==2) { if(count<14) { r.status=DNN_PROTOCOL;return r; }uint32_t type=(uint32_t)bytes[12]<<8|bytes[13];offset=14;for(unsigned tags=0;tags<2 && (type==0x8100 || type==0x88a8);tags++) { if(count-offset<4) { r.status=DNN_PROTOCOL;return r; }type=(uint32_t)bytes[offset+2]<<8|bytes[offset+3];offset+=4; }r.ether_type=type;r.offset=(uint32_t)offset;available=count-offset;if(type==0x0800) r.family=4;else if(type==0x86dd) r.family=6;else { if(type<=1500 && type>available) r.status=DNN_PROTOCOL;return r; } }
    else { r.family=bytes[0]>>4;if(r.family!=4 && r.family!=6) { r.status=DNN_PROTOCOL;return r; } }
    const uint8_t *ip=bytes+offset;size_t length=0;if(r.family==4) { if(available<20 || (ip[0]>>4)!=4 || (ip[0]&15)<5 || (size_t)(ip[0]&15)*4>available) { r.status=DNN_PROTOCOL;return r; }length=(size_t)ip[2]<<8|ip[3];if(length<(size_t)(ip[0]&15)*4) r.status=DNN_PROTOCOL; }
    else { if(available<40 || (ip[0]>>4)!=6) { r.status=DNN_PROTOCOL;return r; }length=40+((size_t)ip[4]<<8|ip[5]); }
    if(length>available || (kind==1 && length!=available)) r.status=DNN_PROTOCOL;return r;
}
static dnu_device *dnu_allocate(dnu_config config) { dnu_device *d=calloc(1,sizeof *d);if(!d) return NULL;d->buffer=malloc(DNU_SCRATCH);if(!d->buffer) { free(d);return NULL; }d->magic=DNU_MAGIC;d->config=config;d->used=sizeof *d+DNU_SCRATCH;d->fd=-1;return d; }
#if defined(_WIN32)
static bool dnu_symbol(HMODULE module,const char *name,void *target,size_t size) { FARPROC symbol=GetProcAddress(module,name);if(!symbol || size!=sizeof symbol) return false;memcpy(target,&symbol,size);return true; }
static bool dnu_windows_load(dnu_windows *a,dnn_text path) {
    memset(a,0,sizeof *a);WCHAR name[32768];if(path.len) { if(path.len>32760 || !path.data || memchr(path.data,0,path.len)) return false;int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char*)path.data,(int)path.len,name,32767);if(!n) return false;name[n]=0;if(!((n>2 && name[1]==L':') || (n>2 && name[0]==L'\\' && name[1]==L'\\'))) return false; }
    else { DWORD n=GetModuleFileNameW(NULL,name,32768);if(!n || n>=32750) return false;while(n && name[n-1]!=L'\\' && name[n-1]!=L'/') n--;memcpy(name+n,L"wintun.dll",11*sizeof(WCHAR)); }
    a->module=LoadLibraryExW(name,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!a->module) return false;
#define DNU_WIN(member,name) if(!dnu_symbol(a->module,name,&a->member,sizeof a->member)) goto fail
    DNU_WIN(create,"WintunCreateAdapter");DNU_WIN(open,"WintunOpenAdapter");DNU_WIN(close,"WintunCloseAdapter");DNU_WIN(luid,"WintunGetAdapterLuid");DNU_WIN(start,"WintunStartSession");DNU_WIN(end,"WintunEndSession");DNU_WIN(event,"WintunGetReadWaitEvent");DNU_WIN(receive,"WintunReceivePacket");DNU_WIN(release,"WintunReleaseReceivePacket");DNU_WIN(allocate,"WintunAllocateSendPacket");DNU_WIN(send,"WintunSendPacket");
#undef DNU_WIN
    a->ipmodule=LoadLibraryExW(L"iphlpapi.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!a->ipmodule) goto fail;
#define DNU_IP(member,name) if(!dnu_symbol(a->ipmodule,name,&a->member,sizeof a->member)) goto fail
    DNU_IP(init_address,"InitializeUnicastIpAddressEntry");DNU_IP(create_address,"CreateUnicastIpAddressEntry");DNU_IP(delete_address,"DeleteUnicastIpAddressEntry");DNU_IP(init_interface,"InitializeIpInterfaceEntry");DNU_IP(get_interface,"GetIpInterfaceEntry");DNU_IP(set_interface,"SetIpInterfaceEntry");
#undef DNU_IP
    return true;
fail:if(a->ipmodule) FreeLibrary(a->ipmodule);if(a->module) FreeLibrary(a->module);memset(a,0,sizeof *a);return false;
}
static int dnu_windows_configure(dnu_device *d) {
    for(unsigned i=0;i<2;i++) { dnn_addr address=i?d->config.ipv6:d->config.ipv4;if(i && d->config.mtu<1280) continue;MIB_IPINTERFACE_ROW iface;d->api.init_interface(&iface);iface.InterfaceLuid=d->luid;iface.Family=i?AF_INET6:AF_INET;DWORD status=d->api.get_interface(&iface);if(status) return (int)status;iface.NlMtu=d->config.mtu;status=d->api.set_interface(&iface);if(status) return (int)status;if(!address.family) continue;MIB_UNICASTIPADDRESS_ROW *row=&d->addresses[i];d->api.init_address(row);row->InterfaceLuid=d->luid;row->Address.si_family=i?AF_INET6:AF_INET;row->OnLinkPrefixLength=(UINT8)(i?d->config.ipv6_prefix:d->config.ipv4_prefix);row->PrefixOrigin=IpPrefixOriginManual;row->SuffixOrigin=IpSuffixOriginManual;if(i) memcpy(&row->Address.Ipv6.sin6_addr,address.bytes,16);else memcpy(&row->Address.Ipv4.sin_addr,address.bytes,4);status=d->api.create_address(row);if(status) return (int)status;d->added[i]=true; }return 0;
}
#endif
void dlm_tun_close(void *handle) {
    dnu_device *d=dnu_get(handle);if(!d) return;d->magic=0;
#if defined(_WIN32)
    if(d->session) d->api.end(d->session);for(unsigned i=0;i<2;i++) if(d->added[i]) (void)d->api.delete_address(&d->addresses[i]);if(d->adapter) d->api.close(d->adapter);if(d->api.ipmodule) FreeLibrary(d->api.ipmodule);if(d->api.module) FreeLibrary(d->api.module);
#else
    if(d->fd>=0) close(d->fd);
#if defined(__FreeBSD__)
    char owned_name[IFNAMSIZ];if(d->owned && d->index && if_indextoname(d->index,owned_name) && if_nametoindex(owned_name)==d->index) { int control=socket(AF_INET,SOCK_DGRAM,0);if(control>=0) { struct ifreq request={0};memcpy(request.ifr_name,owned_name,strlen(owned_name)+1);(void)ioctl(control,SIOCIFDESTROY,&request);close(control); } }
#endif
#endif
    free(d->buffer);free(d);
}
#if defined(__linux__) || defined(__FreeBSD__)
static int dnu_posix_configure(dnu_device *d) {
    int control=socket(AF_INET,SOCK_DGRAM,0);if(control<0) return errno;struct ifreq r={0};memcpy(r.ifr_name,d->name,strlen(d->name)+1);r.ifr_mtu=(int)d->config.mtu;if(ioctl(control,SIOCSIFMTU,&r)) { int e=errno;close(control);return e; }
    if(d->config.ipv4.family) {
#if defined(__FreeBSD__)
        struct ifaliasreq alias={0};memcpy(alias.ifra_name,d->name,strlen(d->name)+1);struct sockaddr_in *a=(struct sockaddr_in*)&alias.ifra_addr,*mask=(struct sockaddr_in*)&alias.ifra_mask;a->sin_len=mask->sin_len=sizeof *a;a->sin_family=mask->sin_family=AF_INET;memcpy(&a->sin_addr,d->config.ipv4.bytes,4);mask->sin_addr.s_addr=htonl(d->config.ipv4_prefix?UINT32_MAX<<(32-d->config.ipv4_prefix):0);if(ioctl(control,SIOCAIFADDR,&alias)) { int e=errno;close(control);return e; }
#else
        struct sockaddr_in *a=(struct sockaddr_in*)&r.ifr_addr;a->sin_family=AF_INET;memcpy(&a->sin_addr,d->config.ipv4.bytes,4);if(ioctl(control,SIOCSIFADDR,&r)) { int e=errno;close(control);return e; }a=(struct sockaddr_in*)&r.ifr_netmask;a->sin_family=AF_INET;a->sin_addr.s_addr=htonl(d->config.ipv4_prefix?UINT32_MAX<<(32-d->config.ipv4_prefix):0);if(ioctl(control,SIOCSIFNETMASK,&r)) { int e=errno;close(control);return e; }
#endif
    }
    if(d->config.ipv6.family) { int six=socket(AF_INET6,SOCK_DGRAM,0);if(six<0) { int e=errno;close(control);return e; }
#if defined(__FreeBSD__)
        struct in6_aliasreq a={0};memcpy(a.ifra_name,d->name,strlen(d->name)+1);a.ifra_addr.sin6_len=sizeof a.ifra_addr;a.ifra_addr.sin6_family=AF_INET6;memcpy(&a.ifra_addr.sin6_addr,d->config.ipv6.bytes,16);a.ifra_addr.sin6_scope_id=d->config.ipv6.scope;a.ifra_prefixmask.sin6_len=sizeof a.ifra_prefixmask;a.ifra_prefixmask.sin6_family=AF_INET6;for(unsigned bit=0;bit<d->config.ipv6_prefix;bit++) a.ifra_prefixmask.sin6_addr.s6_addr[bit/8]|=(uint8_t)(128u>>(bit%8));a.ifra_lifetime.ia6t_vltime=a.ifra_lifetime.ia6t_pltime=UINT32_MAX;int result=ioctl(six,SIOCAIFADDR_IN6,&a);
#else
        struct { struct in6_addr address;uint32_t prefix;int index; } a={0};memcpy(&a.address,d->config.ipv6.bytes,16);a.prefix=d->config.ipv6_prefix;a.index=(int)d->index;int result=ioctl(six,SIOCSIFADDR,&a);
#endif
        int error=result?errno:0;close(six);if(error) { close(control);return error; }
    }
    memset(&r,0,sizeof r);memcpy(r.ifr_name,d->name,strlen(d->name)+1);if(ioctl(control,SIOCGIFFLAGS,&r)) { int e=errno;close(control);return e; }r.ifr_flags|=IFF_UP;if(ioctl(control,SIOCSIFFLAGS,&r)) { int e=errno;close(control);return e; }close(control);return 0;
}
#endif
dnn_open dlm_tun_open(dnn_open permission,dnn_text name,dnn_text library,dnu_config config) {
    dnu_grant *g=permission.handle;int status=permission.status,system=0;if(!status && (!g || g->magic!=DNU_GRANT || !(g->flags&1) || (config.configure && !(g->flags&2)) || !dnu_config_valid(config) || (config.mode==2 && config.configure) || name.len>63 || (name.len && (!name.data || memchr(name.data,0,name.len))) || (config.mode==2 && !name.len))) status=DNN_INVALID;if(!status) for(size_t i=0;i<name.len;i++) if(name.data[i]<32 || name.data[i]==127 || name.data[i]=='/' || name.data[i]=='\\') status=DNN_INVALID;dnu_device *d=status?NULL:dnu_allocate(config);if(!status && !d) status=DNN_LIMIT;
    if(!status) { if(name.len) memcpy(d->name,name.data,name.len);d->owned=false;
#if defined(_WIN32)
        d->backend=2;if(config.kind!=1) status=DNN_UNSUPPORTED;if(config.ring_capacity<131072 || config.ring_capacity>67108864 || (config.ring_capacity&(config.ring_capacity-1))) status=DNN_INVALID;if(!status && !dnu_windows_load(&d->api,library)) { status=DNN_UNSUPPORTED;system=(int)GetLastError(); }WCHAR wide[128];int n=0;if(!status) { if(!name.len) { memcpy(d->name,"DLN",4);name=(dnn_text){(const uint8_t*)d->name,3}; }n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char*)name.data,(int)name.len,wide,127);if(!n) status=DNN_INVALID;else wide[n]=0; }
        if(!status) { d->adapter=config.mode==1?d->api.create(wide,L"DLN",NULL):d->api.open(wide);if(!d->adapter) { status=DNN_SYSTEM;system=(int)GetLastError(); } }
        if(!status) { d->api.luid(d->adapter,&d->luid);if(config.configure) { system=dnu_windows_configure(d);if(system) status=DNN_SYSTEM; } }
        if(!status) { d->session=d->api.start(d->adapter,config.ring_capacity);if(!d->session) { status=DNN_SYSTEM;system=(int)GetLastError(); }else { d->event=d->api.event(d->session);if(!d->event) { status=DNN_SYSTEM;system=(int)GetLastError(); } } }
#elif defined(__linux__) || defined(__FreeBSD__)
        (void)library;d->backend=1;for(size_t i=0;i<name.len;i++) { unsigned c=name.data[i];if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.')) status=DNN_INVALID; }if(name.len>=IFNAMSIZ) status=DNN_INVALID;
#if defined(__linux__)
        if(!status && config.mode==2) { d->index=if_nametoindex(d->name);if(!d->index) { status=DNN_SYSTEM;system=ENODEV; } }if(!status) { d->fd=open("/dev/net/tun",O_RDWR|O_NONBLOCK|O_CLOEXEC);if(d->fd<0) { status=DNN_SYSTEM;system=errno; } }
        if(!status) { struct ifreq request={0};request.ifr_flags=(short)((config.kind==1?IFF_TUN:IFF_TAP)|IFF_NO_PI|(config.mode==1?IFF_TUN_EXCL:0));if(name.len) memcpy(request.ifr_name,d->name,name.len+1);if(ioctl(d->fd,TUNSETIFF,&request)) { status=DNN_SYSTEM;system=errno; }else { memcpy(d->name,request.ifr_name,IFNAMSIZ);d->name[IFNAMSIZ-1]=0;uint32_t index=if_nametoindex(d->name);if(config.mode==2 && d->index!=index) status=DNN_INVALID;d->index=index;d->owned=config.mode==1; } }
#else
        if(!status) { d->backend=3;if(name.len) { const char *prefix=config.kind==1?"tun":"tap";if(name.len<3 || memcmp(d->name,prefix,3)) status=DNN_INVALID;for(size_t i=3;i<name.len;i++) if(d->name[i]<'0' || d->name[i]>'9') status=DNN_INVALID; }if(!name.len) memcpy(d->name,config.kind==1?"tun":"tap",4);if(!status && config.mode==1) { int control=socket(AF_INET,SOCK_DGRAM,0);if(control<0) { status=DNN_SYSTEM;system=errno; }else { struct ifreq request={0};memcpy(request.ifr_name,d->name,strlen(d->name)+1);if(ioctl(control,SIOCIFCREATE2,&request)) { status=DNN_SYSTEM;system=errno; }else { memcpy(d->name,request.ifr_name,IFNAMSIZ);d->owned=true; }close(control); } }d->index=if_nametoindex(d->name);if(!status && !d->index) { status=DNN_SYSTEM;system=ENODEV; } }
        if(!status) { char device[80];int n=snprintf(device,sizeof device,"/dev/%s",d->name);if(n<0 || (size_t)n>=sizeof device) status=DNN_INVALID;else { d->fd=open(device,O_RDWR|O_NONBLOCK|O_CLOEXEC);if(d->fd<0) { status=DNN_SYSTEM;system=errno; } } }if(!status && config.kind==1 && config.configure) { int mode=IFF_BROADCAST|IFF_MULTICAST;if(ioctl(d->fd,TUNSIFMODE,&mode)) { status=DNN_SYSTEM;system=errno; } }if(!status && config.kind==1) { int one=1;if(ioctl(d->fd,TUNSIFHEAD,&one)) { status=DNN_SYSTEM;system=errno; } }
#endif
        if(!status && config.configure) { system=dnu_posix_configure(d);if(system) status=DNN_SYSTEM; }
#else
        (void)library;status=DNN_UNSUPPORTED;
#endif
    }dlm_tun_permission_close(permission.handle);if(status) { dlm_tun_close(d);return dnn_open_error(status,system); }return (dnn_open){d,0,0};
}
dnn_open dlm_tun_adopt(int64_t descriptor,dnu_config config) {
#if defined(_WIN32)
    (void)descriptor;(void)config;return dnn_open_error(DNN_UNSUPPORTED,0);
#else
    if(descriptor<0 || descriptor>INT_MAX) return dnn_open_error(DNN_INVALID,0);int fd=(int)descriptor,status=0,system=0;if(!dnu_config_valid(config) || config.configure) status=DNN_INVALID;int type=0;socklen_t size=sizeof type;if(!status && (getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&size) || (type!=SOCK_DGRAM && type!=SOCK_SEQPACKET))) { status=DNN_INVALID;system=errno; }if(!status) { int flags=fcntl(fd,F_GETFL,0),clo=fcntl(fd,F_GETFD,0);if(flags<0 || clo<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK) || fcntl(fd,F_SETFD,clo|FD_CLOEXEC)) { status=DNN_SYSTEM;system=errno; } }dnu_device *d=status?NULL:dnu_allocate(config);if(!status && !d) status=DNN_LIMIT;if(status) { close(fd);return dnn_open_error(status,system); }d->fd=fd;d->backend=4;return (dnn_open){d,0,0};
#endif
}
static int dnu_fill(dnu_device *d,int *system) { if(d->pending) return 0;
#if defined(_WIN32)
    DWORD count=0;BYTE *packet=d->api.receive(d->session,&count);if(!packet) { DWORD error=GetLastError();*system=(int)error;return error==ERROR_NO_MORE_ITEMS?DNN_WOULD_BLOCK:error==ERROR_HANDLE_EOF?DNN_EOF:DNN_SYSTEM; }if(count>DNU_SCRATCH) { d->api.release(d->session,packet);dnu_inc(&d->dropped);return DNN_LIMIT; }memcpy(d->buffer,packet,count);d->api.release(d->session,packet);d->pending=count;
#else
    ssize_t n=read(d->fd,d->buffer,DNU_SCRATCH);if(n<0) { *system=errno;return DNN_AGAIN(errno)?DNN_WOULD_BLOCK:DNN_SYSTEM; }if(!n) return DNN_EOF;d->pending=(size_t)n;
#if defined(__FreeBSD__)
    if(d->backend==3 && d->config.kind==1) { if(d->pending<4) { d->pending=0;return DNN_PROTOCOL; }uint32_t family=dnp_read32(d->buffer);if(family!=AF_INET && family!=AF_INET6) { d->pending=0;return DNN_PROTOCOL; }memmove(d->buffer,d->buffer+4,d->pending-4);d->pending-=4; }
#endif
#endif
    dnu_inc(&d->received);return 0;
}
dnu_packet dlm_tun_peek(void *handle) { dnu_packet r={0};dnu_device *d=dnu_get(handle);if(!d) { r.status=DNN_INVALID;return r; }int system=0,status=dnu_fill(d,&system);if(status) { r.status=status;r.system=system;return r; }r=dlm_tun_packet(d->config.kind,d->buffer,d->pending);if(d->pending>d->config.max_packet) r.status=DNN_LIMIT;return r; }
dnn_io dlm_tun_discard(void *handle) { dnu_device *d=dnu_get(handle);if(!d || !d->pending) return dnn_result(0,DNN_INVALID,0);size_t count=d->pending;d->pending=0;dnu_inc(&d->dropped);return dnn_result(count,0,0); }
dnu_packet dlm_tun_receive(void *handle,uint8_t *output,size_t capacity) { dnu_packet r=dlm_tun_peek(handle);if(r.status) return r;dnu_device *d=handle;if(!dnn_buffer(output,capacity)) { r.status=DNN_INVALID;return r; }if(capacity<d->pending) { r.status=DNN_TRUNCATED;return r; }memcpy(output,d->buffer,d->pending);r.copied=d->pending;d->pending=0;return r; }
dnn_io dlm_tun_send(void *handle,const uint8_t *bytes,size_t count) {
    dnu_device *d=dnu_get(handle);if(!d) return dnn_result(0,DNN_INVALID,0);dnu_packet packet=dlm_tun_packet(d->config.kind,bytes,count);if(packet.status || count>d->config.max_packet) { dnu_inc(&d->malformed);return dnn_result(0,packet.status?packet.status:DNN_LIMIT,0); }
#if defined(_WIN32)
    BYTE *out=d->api.allocate(d->session,(DWORD)count);if(!out) { DWORD error=GetLastError();return dnn_result(0,error==ERROR_BUFFER_OVERFLOW?DNN_WOULD_BLOCK:error==ERROR_HANDLE_EOF?DNN_EOF:DNN_SYSTEM,(int)error); }memcpy(out,bytes,count);d->api.send(d->session,out);
#else
    const uint8_t *data=bytes;size_t length=count;
#if defined(__FreeBSD__)
    uint8_t framed[DNU_SCRATCH];if(d->backend==3 && d->config.kind==1) { dnp_write32(framed,packet.family==4?AF_INET:AF_INET6);memcpy(framed+4,bytes,count);data=framed;length=count+4; }
#endif
    ssize_t sent=d->backend==4?send(d->fd,data,length,MSG_NOSIGNAL):write(d->fd,data,length);if(sent<0) return dnn_result(0,DNN_AGAIN(errno)?DNN_WOULD_BLOCK:DNN_SYSTEM,errno);if((size_t)sent!=length) return dnn_result(0,DNN_PROTOCOL,0);
#endif
    dnu_inc(&d->sent);return dnn_result(count,0,0);
}
dnn_io dlm_tun_wait(void *handle,uint32_t ms,void *cancel) { dnu_device *d=dnu_get(handle);if(!d || ms>60000) return dnn_result(0,DNN_INVALID,0);uint64_t end=dnn_deadline(ms);do { if(dnn_cancelled(cancel)) return dnn_result(0,DNN_CANCELLED,0);dnu_packet packet=dlm_tun_peek(d);if(packet.status!=DNN_WOULD_BLOCK) return dnn_result(packet.required,packet.status,packet.system);uint32_t delay=(uint32_t)dnn_remaining(end);if(!delay) return dnn_result(0,ms?DNN_TIMEOUT:DNN_WOULD_BLOCK,0);if(delay>10) delay=10;
#if defined(_WIN32)
    DWORD status=WaitForSingleObject(d->event,delay);if(status==WAIT_FAILED) return dnn_result(0,DNN_SYSTEM,(int)GetLastError());
#else
    struct pollfd entry={d->fd,POLLIN,0};int ready=poll(&entry,1,(int)delay);if(ready<0 && !DNN_INTR(errno)) return dnn_result(0,DNN_SYSTEM,errno);
#endif
    }while(dnn_remaining(end));return dnn_result(0,DNN_TIMEOUT,0); }
dnu_stats dlm_tun_stats(void *handle) { dnu_stats r={0};dnu_device *d=dnu_get(handle);if(!d) r.status=DNN_INVALID;else { r.received=d->received;r.sent=d->sent;r.dropped=d->dropped;r.malformed=d->malformed;r.used=d->used;r.limit=d->config.memory;r.pending=d->pending;r.backend=d->backend;r.kind=d->config.kind;r.mtu=d->config.mtu;r.index=d->index; }return r; }
dnn_io dlm_tun_name(void *handle,uint8_t *output,size_t capacity) { dnu_device *d=dnu_get(handle);if(!d || !dnn_buffer(output,capacity)) return dnn_result(0,DNN_INVALID,0);size_t count=strlen(d->name);if(capacity<count) return dnn_result(0,DNN_TRUNCATED,0);memcpy(output,d->name,count);return dnn_result(count,0,0); }
void dlm_tun_bridge_close(void *handle) { dnu_bridge *bridge=handle;if(!bridge || bridge->magic!=DNU_BRIDGE) return;bridge->magic=0;dlm_tun_close(bridge->device);dlm_adaptive_close(bridge->link);free(bridge); }
dnn_open dlm_tun_bridge_new(dnn_open device,dnn_open adaptive,uint32_t channel) { dnu_device *d=dnu_get(device.handle);dna_link *link=dna_get(adaptive.handle);int status=device.status?device.status:adaptive.status;if(!status && (!d || !link || channel>=link->config.bounds.channels || link->config.bounds.max_message<d->config.max_packet || !(link->config.requirements.required&4))) status=DNN_INVALID;dnu_bridge *bridge=status?NULL:calloc(1,sizeof *bridge);if(!status && !bridge) status=DNN_LIMIT;if(status) { dlm_tun_close(device.handle);dlm_adaptive_close(adaptive.handle);return dnn_open_error(status,0); }bridge->magic=DNU_BRIDGE;bridge->device=d;bridge->link=link;bridge->channel=channel;return (dnn_open){bridge,0,0}; }
dnn_io dlm_tun_bridge_poll(void *handle,uint32_t ms,void *cancel) {
    dnu_bridge *b=handle;if(!b || b->magic!=DNU_BRIDGE || ms>60000) return dnn_result(0,DNN_INVALID,0);uint64_t end=dnn_deadline(ms);size_t progress=0;do { if(dnn_cancelled(cancel)) return dnn_result(progress,DNN_CANCELLED,0);dnn_io polled=dlm_adaptive_poll(b->link,0,cancel);if(polled.status) return polled;dna_stats stats=dlm_adaptive_stats(b->link);if(!stats.authenticated || !stats.active) { dnu_inc(&b->blocked);return dnn_result(progress,DNN_WOULD_BLOCK,0); }
        for(unsigned work=0;work<64;work++) { if(b->incoming_count) { dnn_io sent=dlm_tun_send(b->device,b->packet,b->incoming_count);if(sent.status==DNN_WOULD_BLOCK) { dnu_inc(&b->blocked);break; }if(sent.status) return sent;b->incoming_count=0;dnu_inc(&b->incoming);progress++; }dna_message_result pending=dlm_adaptive_peek(b->link);if(pending.status==DNN_WOULD_BLOCK) break;if(pending.status) return dnn_result(progress,pending.status,pending.system);if(pending.count>sizeof b->packet) return dnn_result(progress,DNN_LIMIT,0);dna_delivery received=dlm_adaptive_receive(b->link,b->packet,sizeof b->packet);if(received.status) return dnn_result(progress,received.status,received.system);dnu_packet checked=dlm_tun_packet(b->device->config.kind,b->packet,received.copied);if(received.channel!=b->channel || checked.status || received.copied>b->device->config.max_packet) { dnu_inc(&b->rejected);continue; }b->incoming_count=received.copied; }
        for(unsigned work=0;work<64;work++) { dnu_packet packet=dlm_tun_peek(b->device);if(packet.status==DNN_WOULD_BLOCK) break;if(packet.status) { if(packet.status==DNN_PROTOCOL || packet.status==DNN_LIMIT) { if(b->device->pending) (void)dlm_tun_discard(b->device);dnu_inc(&b->rejected);continue; }return dnn_result(progress,packet.status,packet.system); }dna_message_result sent=dlm_adaptive_send(b->link,b->channel,b->device->buffer,b->device->pending);if(sent.status==DNN_WOULD_BLOCK) { dnu_inc(&b->blocked);break; }if(sent.status) return dnn_result(progress,sent.status,sent.system);b->device->pending=0;dnu_inc(&b->outgoing);progress++; }
        if(!ms || progress) break;uint32_t delay=(uint32_t)dnn_remaining(end);if(!delay) break;if(delay>5) delay=5;
#if defined(_WIN32)
        Sleep(delay);
#else
        struct timespec wait={0,(long)delay*1000000};nanosleep(&wait,NULL);
#endif
    }while(dnn_remaining(end));return dnn_result(progress,0,0);
}
dnu_bridge_stats dlm_tun_bridge_stats(void *handle) { dnu_bridge_stats r={0};dnu_bridge *b=handle;if(!b || b->magic!=DNU_BRIDGE) r.status=DNN_INVALID;else { r.outgoing=b->outgoing;r.incoming=b->incoming;r.rejected=b->rejected;r.blocked=b->blocked;r.pending=b->incoming_count+b->device->pending;r.used=sizeof *b+b->device->used+b->link->used;r.authenticated=dlm_adaptive_stats(b->link).authenticated; }return r; }
#endif
