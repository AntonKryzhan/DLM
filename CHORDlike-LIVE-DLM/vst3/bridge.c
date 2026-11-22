/* CHORD-like LIVE / DLM — VST3 host adapter (Steinberg VST3 C ABI subset).
 * DSP is implemented in DLM; this C file translates the host binary ABI.
 * No JUCE dependency. Generic host parameter editor is used.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include "CHORDlikeLiveDLM.h"
#ifdef _WIN32
#include <windows.h>
#include "CHORDlikeLiveUI.h"
#endif
#ifdef _WIN32
#define CALL __stdcall
#define API __declspec(dllexport)
#define E_NOIF ((int32_t)0x80004002)
#define E_ARG ((int32_t)0x80070057)
#define E_NI ((int32_t)0x80004001)
#else
#define CALL
#define API __attribute__((visibility("default")))
#define E_NOIF -1
#define E_ARG 2
#define E_NI 3
#endif
#define OK 0
#define FAIL 1
#define N 48
#define MAGIC 0x334d4c44U
#define MAXCHUNK 8192

typedef int32_t R;
typedef uint16_t S16[128];
typedef char UID[16];
typedef struct {char vendor[64],url[256],email[128];int32_t flags;} FactoryInfo;
typedef struct {UID cid;int32_t cardinality;char category[32],name[64];} ClassInfo;
typedef struct {int32_t media,dir,channels;S16 name;int32_t type;uint32_t flags;} BusInfo;
typedef struct {uint32_t id;S16 title,shortTitle,units;int32_t steps;double normalDefault;int32_t unit,flags;} ParamInfo;
typedef struct {int32_t mode,sampleSize,maxBlock;double rate;} Setup;
typedef struct {int32_t channels;uint64_t silence;union{float **f32;double **f64;};} Bus;
typedef struct {int32_t mode,sampleSize,frames,inputCount,outputCount;Bus*in,*out;void*paramIn,*paramOut,*eventsIn,*eventsOut,*context;} Process;

/* Every VST3 interface is an object containing a vtable pointer. */
typedef struct {const void **vt;} Obj;
#define METHOD(x,idx,ret,...) ((ret (CALL *)(void*, ##__VA_ARGS__))(((Obj*)(x))->vt[idx]))
#define MC(x,idx,ret,...) METHOD(x,idx,ret, ##__VA_ARGS__)
/* Slots: IPluginFactory = 7, IComponent = 14, IAudioProcessor = 11,
   IEditController = 18. Their ordering follows the official VST3 C API. */

typedef struct {const char*name,*label,*unit;double lo,hi,def;int steps;} Def;
static const Def P[N]={
 {"profile","DSP Profile","",0,2,0,2},{"customTaps","Custom FIR Length","",0,4,2,4},
 {"oversampling","Oversampling","",0,5,4,5},{"filterMode","Hugo 2 Filter","",0,3,2,3},
 {"crossfeed","Headphone Crossfeed","%",0,100,0,0},{"character","Creative DAC Character","%",0,100,100,0},
 {"output","Output Gain","dB",-24,12,-7.5,0},{"mix","Dry Wet","%",0,100,100,0},
 {"pulseModel","Pulse Array Model","",0,2,2,2},{"reconstructionPhase","Reconstruction Phase","",0,2,2,2},
 {"liveOptimization","LIVE Linear Optimization","",0,1,1,1},{"dacArchitecture","DAC Architecture","",0,3,3,3},
 {"dacMismatch","DAC Element Mismatch","%",0,100,25,0},{"tubeType","Tube Valve Type","",0,6,0,6},
 {"tubeTopology","Tube Output Topology","",0,1,0,1},{"tubeDrive","Tube Drive","dB",0,36,12,0},
 {"tubeBias","Tube Bias","%",-100,100,0,0},{"tubeSag","Tube Cathode Sag","%",0,100,0,0},
 {"tubeWet","Tube Stage Mix","%",0,100,100,0},{"tubeRouting","Valve Routing","",0,1,0,1},
 {"tubeLowCrossover","Low / Mid Crossover","Hz",40,1400,403,0},
 {"tubeHighCrossover","Mid / High Crossover","Hz",900,12000,6415,0},
 {"tubeLowType","Low Valve","",0,6,1,6},{"tubeLowTopology","Low Topology","",0,1,0,1},
 {"tubeLowDrive","Low Drive","dB",0,36,10.2,0},{"tubeLowBias","Low Bias","%",-100,100,-52,0},
 {"tubeLowSag","Low Sag","%",0,100,100,0},{"tubeLowWet","Low Wet","%",0,100,100,0},
 {"tubeMidType","Mid Valve","",0,6,1,6},{"tubeMidTopology","Mid Topology","",0,1,0,1},
 {"tubeMidDrive","Mid Drive","dB",0,36,12,0},{"tubeMidBias","Mid Bias","%",-100,100,-46.4,0},
 {"tubeMidSag","Mid Sag","%",0,100,100,0},{"tubeMidWet","Mid Wet","%",0,100,100,0},
 {"tubeHighType","High Valve","",0,6,4,6},{"tubeHighTopology","High Topology","",0,1,1,1},
 {"tubeHighDrive","High Drive","dB",0,36,9.8,0},{"tubeHighBias","High Bias","%",-100,100,-96.8,0},
 {"tubeHighSag","High Sag","%",0,100,100,0},{"tubeHighWet","High Wet","%",0,100,100,0},
 {"tubeCpuMode","Fast Tube CPU","",0,1,1,1},{"autoMatch","Auto Level Match","",0,1,1,1},
 {"peakLimiter","True Peak Limiter","",0,1,1,1},{"peakCeiling","Peak Ceiling","dB",-12,-0.1,-4.5,0},
 {"wtaFocus","WTA Focus","%",0,100,77.2,0},{"phaseMotion","Phase Motion","%",0,100,50.4,0},
 {"hfTone","HF Tone","%",0,100,28.8,0},{"pulseColor","Pulse Color","%",0,100,66.8,0}
};
static double clamp(double x){return !isfinite(x)?0:(x<0?0:(x>1?1:x));}
static double plain(int i,double x){double p=P[i].lo+clamp(x)*(P[i].hi-P[i].lo);return P[i].steps?floor(p+0.5):p;}
static double norm(int i,double x){return clamp((x-P[i].lo)/(P[i].hi-P[i].lo));}
static uint32_t id(int i){const unsigned char*s=(const unsigned char*)P[i].name;uint32_t h=2166136261u;while(*s){h^=*s++;h*=16777619u;}return h;}
static int index_of(uint32_t x){for(int i=0;i<N;i++)if(id(i)==x)return i;return -1;}
static void defaults(double*values){for(int i=0;i<N;i++)values[i]=norm(i,P[i].def);}
static void u16(uint16_t*out,const char*in){memset(out,0,128*sizeof(*out));for(int j=0;j<127&&in[j];j++)out[j]=(unsigned char)in[j];}
static void uid(UID out,uint32_t a,uint32_t b,uint32_t c,uint32_t d){
#ifdef _WIN32
    out[0]=a;out[1]=a>>8;out[2]=a>>16;out[3]=a>>24;
    out[4]=b>>16;out[5]=b>>24;out[6]=b;out[7]=b>>8;
    for(int j=0;j<4;j++){out[j+8]=c>>(24-8*j);out[j+12]=d>>(24-8*j);}
#else
    uint32_t w[4]={a,b,c,d};
    for(int k=0;k<4;k++)for(int j=0;j<4;j++)out[k*4+j]=w[k]>>(24-8*j);
#endif
}
static void class_id(UID u,int controller){if(controller)uid(u,0xA64335A3,0x6D2F4C6B,0xAD6112AA,0x43108D02);else uid(u,0xB18E1991,0x60DD4B25,0x8D0A66C9,0xAB94D0A1);}
static int uidmatch(const char*data,uint32_t a,uint32_t b,uint32_t c,uint32_t d){UID q;uid(q,a,b,c,d);return data&&!memcmp(data,q,16);}
static int is_unknown(const char *x){return uidmatch(x,0,0,0xC0000000,0x46);}
static int is_factory(const char *x){return uidmatch(x,0x7A4D811C,0x52114A1F,0xAED9D2EE,0x0B43BF9F);}
static int is_component(const char *x){return uidmatch(x,0xE831FF31,0xF2D54301,0x928EBBEE,0x25697802);}
static int is_audio(const char *x){return uidmatch(x,0x42043F99,0xB7DA453C,0xA569E79D,0x9AAEC33D);}
static int is_ctrl(const char *x){return uidmatch(x,0xDCD7BBE3,0x7742448D,0xA874AACC,0x979C759E);}

typedef struct {Obj component,audio;atomic_uint refs;void*dsp;double rate;double values[N],applied[N];} Processor;
typedef struct View View;
typedef struct {Obj iface;atomic_uint refs;double values[N];void*handler;View*views;} Controller;
#define PROC(i) ((Processor*)(i))
#define FROM_AUDIO(i) ((Processor*)((char*)(i)-offsetof(Processor,audio)))
static const void* cvt[14], *avt[11], *evt[18], *fvt[7];
static uint32_t CALL add_p(void*i){return atomic_fetch_add(&PROC(i)->refs,1)+1;}
static uint32_t CALL release_p(void*i){Processor*p=PROC(i);unsigned n=atomic_fetch_sub(&p->refs,1);if(n==1){if(p->dsp)live_destroy(p->dsp);free(p);}return n-1;}
static uint32_t CALL add_a(void*i){return add_p(FROM_AUDIO(i));}
static uint32_t CALL release_a(void*i){return release_p(FROM_AUDIO(i));}
static R CALL qi_p(void*i,const char*name,void**out){Processor*p=PROC(i);if(!out)return E_ARG;*out=0;
    if(is_unknown(name)||is_component(name))*out=&p->component;else if(is_audio(name))*out=&p->audio;else return E_NOIF;add_p(p);return OK;}
static R CALL qi_a(void*i,const char*n,void**out){return qi_p(FROM_AUDIO(i),n,out);}
static R CALL init_p(void*i,void*ctx){(void)i;(void)ctx;return OK;}
static R CALL term_p(void*i){Processor*p=PROC(i);if(p->dsp){live_destroy(p->dsp);p->dsp=NULL;}return OK;}
static R CALL controller_class(void*i,UID result){(void)i;if(!result)return E_ARG;class_id(result,1);return OK;}
static R CALL set_io(void*i,int32_t mode){(void)i;(void)mode;return OK;}
static int32_t CALL bus_count(void*i,int32_t media,int32_t direction){(void)i;return (media==0&&(direction==0||direction==1))?1:0;}
static R CALL bus_info(void*i,int32_t media,int32_t dir,int32_t idx,BusInfo*b){(void)i;if(media||idx||!b||(dir!=0&&dir!=1))return E_ARG;memset(b,0,sizeof(*b));b->media=0;b->dir=dir;b->channels=2;b->type=0;b->flags=1;u16(b->name,dir?"Stereo Output":"Stereo Input");return OK;}
static R CALL routing(void*i,void*a,void*b){(void)i;(void)a;(void)b;return E_NI;}
static R CALL activate_bus(void*i,int32_t m,int32_t dir,int32_t index,uint8_t active){(void)i;(void)active;return !m&&!index&&(dir==0||dir==1)?OK:FAIL;}
static R CALL active_p(void*i,uint8_t active){Processor*p=PROC(i);if(!active&&p->dsp)live_reset(p->dsp);return OK;}
static R read_stream(void*s,void*dst,int32_t size){if(!s)return FAIL;int32_t got=0;return MC(s,3,R,void*,int32_t,int32_t*)(s,dst,size,&got)==OK&&got==size?OK:FAIL;}
static R write_stream(void*s,void*src,int32_t size){if(!s)return FAIL;int32_t done=0;return MC(s,4,R,void*,int32_t,int32_t*)(s,src,size,&done)==OK&&done==size?OK:FAIL;}
static R load(void*s,double*values){uint32_t header[3];double temp[N];if(read_stream(s,header,sizeof header)!=OK)return FAIL;if(header[0]!=MAGIC||header[1]!=1||header[2]!=N)return FAIL;if(read_stream(s,temp,sizeof temp)!=OK)return FAIL;for(int j=0;j<N;j++)values[j]=clamp(temp[j]);return OK;}
static R save(void*s,double*values){uint32_t header[3]={MAGIC,1,N};return write_stream(s,header,sizeof header)==OK?write_stream(s,values,sizeof(double)*N):FAIL;}
static R CALL get_state_p(void*i,void*s){return save(s,PROC(i)->values);}
static R CALL set_state_p(void*i,void*s){Processor*p=PROC(i);R r=load(s,p->values);if(!r)for(int n=0;n<N;n++)p->applied[n]=-1;return r;}
static R CALL set_arr(void*i,uint64_t*in,int32_t ni,uint64_t*out,int32_t no){(void)i;return in&&out&&ni==1&&no==1&&in[0]==3&&out[0]==3?OK:FAIL;}
static R CALL get_arr(void*i,int32_t dir,int32_t idx,uint64_t*arr){(void)i;if(!arr||idx||(dir!=0&&dir!=1))return E_ARG;*arr=3;return OK;}
static R CALL sample_type(void*i,int32_t sz){(void)i;return sz==0?OK:FAIL;}
static uint32_t CALL latency(void*i){(void)i;return 173;}
static R CALL setup(void*i,Setup*s){Processor*p=FROM_AUDIO(i);if(!s||s->sampleSize||!isfinite(s->rate)||s->rate<8000||s->maxBlock<0)return E_ARG;
    if(p->dsp)live_destroy(p->dsp);
    p->rate=s->rate;
    p->dsp=live_create_ex(s->rate,(uint32_t)plain(2,p->values[2]),(uint32_t)plain(8,p->values[8]),(uint32_t)plain(11,p->values[11]),(float)(plain(12,p->values[12])*0.01));
    for(int k=0;k<N;k++)p->applied[k]=-1;
    return p->dsp?OK:FAIL;}
static R CALL processing(void*i,uint8_t b){(void)i;(void)b;return OK;}
static float vf(Processor*p,int i){return (float)plain(i,p->values[i]);}
static uint32_t vu(Processor*p,int i){return (uint32_t)plain(i,p->values[i]);}
static float pct(Processor*p,int i){return vf(p,i)*0.01f;}
static void apply(Processor*p){
  int changed[N];for(int k=0;k<N;k++)changed[k]=p->applied[k]!=p->values[k];
  if(changed[2]||changed[8]||changed[11]||changed[12])live_request_mode(p->dsp,vu(p,2),vu(p,8),vu(p,11),pct(p,12));
  if(changed[10])live_fast_mode(p->dsp,vu(p,10)!=0);
  if(changed[40])live_tube_cpu_mode(p->dsp,vu(p,40));
  int t=0;for(int k=13;k<=18;k++)t|=changed[k];
  if(t)live_tube_config(p->dsp,vu(p,13),vu(p,14),vf(p,15),pct(p,16),pct(p,17),pct(p,18));
  if(changed[19])live_tube_routing(p->dsp,vu(p,19));
  if(changed[20]||changed[21])live_band_frequency(p->dsp,vf(p,20),vf(p,21));
  for(uint32_t band=0;band<3;band++){
    int j=22+6*band;int any=0;for(int k=0;k<6;k++)any|=changed[j+k];
    if(any)live_band_config(p->dsp,band,vu(p,j),vu(p,j+1),vf(p,j+2),pct(p,j+3),pct(p,j+4),pct(p,j+5));
  }memcpy(p->applied,p->values,sizeof p->values);
}
/* Host automation queues: IParameterChanges slots 3/4, IParamValueQueue 3/4/5. */
typedef struct {void*q;int idx,cursor,count;} Q;
static int get_point(Q*q,int32_t*offset,double*value){return MC(q->q,5,R,int32_t,int32_t*,double*)(q->q,q->cursor,offset,value)==OK;}
static R process_piece(Processor*p,Process*d,int begin,int end){
  if(end<=begin)return OK;
  if(!p->dsp||d->sampleSize||d->inputCount<1||d->outputCount<1||!d->in||!d->out)return FAIL;
  Bus *a=d->in,*b=d->out;
  if(a->channels<2||b->channels<2||!a->f32||!b->f32||!a->f32[0]||!a->f32[1]||!b->f32[0]||!b->f32[1])return FAIL;
  if(b->f32[0]==b->f32[1]||b->f32[0]==a->f32[1]||b->f32[1]==a->f32[0])return FAIL;
  apply(p);
  for(int pos=begin;pos<end;pos+=MAXCHUNK){size_t n=(size_t)(end-pos>MAXCHUNK?MAXCHUNK:end-pos);
    if(a->f32[0]!=b->f32[0])memmove(b->f32[0]+pos,a->f32[0]+pos,n*sizeof(float));
    if(a->f32[1]!=b->f32[1])memmove(b->f32[1]+pos,a->f32[1]+pos,n*sizeof(float));
    if(live_process_final(p->dsp,b->f32[0]+pos,b->f32[1]+pos,n,
       pct(p,4),pct(p,7),vf(p,6),vu(p,3),vu(p,9),pct(p,44),pct(p,45),pct(p,46),
       pct(p,5),pct(p,47),vu(p,41)!=0,vu(p,42)!=0,vf(p,43))!=1)return FAIL;
  }b->silence=0;return OK;
}
static R CALL process_audio(void*i,Process*d){Processor*p=FROM_AUDIO(i);if(!d||d->frames<0)return E_ARG;
  Q q[N];int count=0;
  if(d->paramIn){int num=MC(d->paramIn,3,int32_t)(d->paramIn);for(int j=0;j<num&&count<N;j++){
    void *queue=MC(d->paramIn,4,void*,int32_t)(d->paramIn,j);if(!queue)continue;
    int k=index_of(MC(queue,3,uint32_t)(queue));int points=MC(queue,4,int32_t)(queue);
    if(k>=0&&points>0)q[count++]=(Q){queue,k,0,points};
  }}
  int pos=0;
  for(;;){int next=d->frames+1;
    for(int j=0;j<count;j++)if(q[j].cursor<q[j].count){int32_t offset=0;double value=0;if(get_point(&q[j],&offset,&value)){
      if(offset<0)offset=0;
      if(offset<next)next=offset;
    }}
    if(next>d->frames)return process_piece(p,d,pos,d->frames);
    R result=process_piece(p,d,pos,next);if(result!=OK)return result;
    int moved=0;for(int j=0;j<count;j++)while(q[j].cursor<q[j].count){
      int32_t offset=0;double value=0;if(!get_point(&q[j],&offset,&value)||offset>next)break;
      p->values[q[j].idx]=clamp(value);q[j].cursor++;moved=1;
    }
    if(!moved)return FAIL;
    pos=next;
  }
}
static uint32_t CALL tail(void*i){(void)i;return 173;}

static uint32_t CALL add_c(void*i){return atomic_fetch_add(&((Controller*)i)->refs,1)+1;}
static uint32_t CALL release_c(void*i){Controller*c=i;unsigned n=atomic_fetch_sub(&c->refs,1);if(n==1){if(c->handler)MC(c->handler,2,uint32_t)(c->handler);free(c);}return n-1;}
static R CALL qi_c(void*i,const char*name,void**out){if(!out)return E_ARG;*out=NULL;if(!is_unknown(name)&&!is_ctrl(name))return E_NOIF;*out=i;add_c(i);return OK;}
static R CALL init_c(void*i,void*ctx){(void)i;(void)ctx;return OK;}
static R CALL term_c(void*i){(void)i;return OK;}
static void view_mark_dirty(Controller*c);
static R CALL set_c(void*i,void*s){Controller*c=i;R r=load(s,c->values);if(r==OK)view_mark_dirty(c);return r;}
static R CALL get_c(void*i,void*s){return save(s,((Controller*)i)->values);}
static int32_t CALL param_count(void*i){(void)i;return N;}
static R CALL param_info(void*i,int32_t index,ParamInfo*p){(void)i;if(!p||index<0||index>=N)return E_ARG;
   memset(p,0,sizeof *p);p->id=id(index);u16(p->title,P[index].label);u16(p->shortTitle,P[index].label);
   u16(p->units,P[index].unit);p->steps=P[index].steps;p->normalDefault=norm(index,P[index].def);p->flags=1;return OK;}
static R CALL param_to_text(void*i,uint32_t x,double v,uint16_t*out){(void)i;int j=index_of(x);if(j<0||!out)return E_ARG;
  char s[70];snprintf(s,sizeof s,P[j].steps?"%.0f":"%.3f",plain(j,v));u16(out,s);return OK;}
static R CALL text_to_param(void*i,uint32_t x,uint16_t*text,double*out){(void)i;int j=index_of(x);if(j<0||!text||!out)return E_ARG;char s[128];int k=0;for(;k<127&&text[k]&&text[k]<128;k++)s[k]=(char)text[k];s[k]=0;
    char*end;double p=strtod(s,&end);if(end==s||!isfinite(p))return FAIL;*out=norm(j,p);return OK;}
static double CALL param_plain(void*i,uint32_t x,double val){(void)i;int j=index_of(x);return j<0?0:plain(j,val);}
static double CALL param_norm(void*i,uint32_t x,double val){(void)i;int j=index_of(x);return j<0?0:norm(j,val);}
static double CALL get_param(void*i,uint32_t x){int j=index_of(x);return j<0?0:((Controller*)i)->values[j];}
static R CALL set_param(void*i,uint32_t x,double val){int j=index_of(x);if(j<0)return E_ARG;Controller*c=i;c->values[j]=clamp(val);view_mark_dirty(c);return OK;}
static R CALL handler(void*i,void*x){Controller*c=i;if(x)MC(x,1,uint32_t)(x);if(c->handler)MC(c->handler,2,uint32_t)(c->handler);c->handler=x;return OK;}
#ifdef _WIN32
/* Host-facing VST3 IPlugView; all pixels and layout originate in live_editor.dlm.
 * This adapter owns only the Steinberg/Win32 lifecycle and parameter gestures. */
typedef struct {int32_t left,top,right,bottom;} ViewRect;
struct View {
    Obj iface;
    atomic_uint refs;
    Controller *controller; /* strong reference; controller's list is non-owning */
    View *next;
    void *editor;
    HWND hwnd;
    WNDPROC oldproc;
    void *frame;
    int32_t width,height;
    int drag,focused;
    int dirty;
};
static const void* vvt[15];
static const wchar_t view_key[]=L"CHORDlike-LIVE-DLM-IPlugView";
static int valid_view_platform(const char *type){return type&&strcmp(type,"HWND")==0;}
static void view_mark_dirty(Controller*c){for(View*v=c->views;v;v=v->next)v->dirty=1;}
static void view_paint(View*v){
    if(!v->editor||!v->hwnd||!IsWindow(v->hwnd))return;
    /* The DLM renderer runs exclusively on the host's UI thread. */
    live_ui_draw(v->editor,v->controller->values,N);
    v->dirty=0;
}
static double view_quantize(int i,double value){
    value=clamp(value);
    if(P[i].steps>0)value=floor(value*P[i].steps+0.5)/P[i].steps;
    return clamp(value);
}
static void view_begin(View*v,int index){
    if(v->drag>=0)return;
    v->drag=index;v->focused=index;
    if(v->controller->handler)MC(v->controller->handler,3,R,uint32_t)(v->controller->handler,id(index));
}
static void view_set(View*v,int index,double value){
    if(index<0||index>=N)return;
    Controller*c=v->controller;value=view_quantize(index,value);
    if(c->values[index]==value)return;
    c->values[index]=value;view_mark_dirty(c);
    if(c->handler)MC(c->handler,4,R,uint32_t,double)(c->handler,id(index),value);
}
static void view_end(View*v){
    int index=v->drag;
    if(index<0)return;
    v->drag=-1;
    if(v->controller->handler)MC(v->controller->handler,5,R,uint32_t)(v->controller->handler,id(index));
}
static void view_resize_window(View*v){
    if(v->editor&&v->width>0&&v->height>0){
        live_ui_resize(v->editor,(size_t)v->width,(size_t)v->height);
        v->dirty=1;view_paint(v);
    }
}
static LRESULT CALLBACK view_window_proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){
    View*v=(View*)GetPropW(hwnd,view_key);
    if(!v||!v->oldproc)return DefWindowProcW(hwnd,message,wp,lp);
    WNDPROC next=v->oldproc;
    switch(message){
        case WM_TIMER:
            if(wp==0xD1A3){if(v->dirty)view_paint(v);return 0;}
            break;
        case WM_LBUTTONDOWN:{
            LRESULT result=CallWindowProcW(next,hwnd,message,wp,lp);
            int x=(int)(short)LOWORD(lp),y=(int)(short)HIWORD(lp);
            if(x>=0&&y>=0){
                int index=live_ui_hit((size_t)x,(size_t)y,(size_t)v->width,(size_t)v->height);
                if(index>=0){SetCapture(hwnd);SetFocus(hwnd);view_begin(v,index);
                    view_set(v,index,live_ui_slider_value((size_t)x,(size_t)v->width,(size_t)index));}
            }return result;
        }
        case WM_MOUSEMOVE:
            if(v->drag>=0){int x=(int)(short)LOWORD(lp);if(x<0)x=0;
                view_set(v,v->drag,live_ui_slider_value((size_t)x,(size_t)v->width,(size_t)v->drag));}
            break;
        case WM_LBUTTONUP:
            if(v->drag>=0){int x=(int)(short)LOWORD(lp);if(x<0)x=0;
                view_set(v,v->drag,live_ui_slider_value((size_t)x,(size_t)v->width,(size_t)v->drag));
                view_end(v);if(GetCapture()==hwnd)ReleaseCapture();}
            break;
        case WM_CAPTURECHANGED:
            view_end(v);break;
        case WM_MOUSEWHEEL:{
            POINT pt={(int)(short)LOWORD(lp),(int)(short)HIWORD(lp)};ScreenToClient(hwnd,&pt);
            if(pt.x>=0&&pt.y>=0){
                int index=live_ui_hit((size_t)pt.x,(size_t)pt.y,(size_t)v->width,(size_t)v->height);
                if(index>=0){int delta=(short)HIWORD(wp);double step=P[index].steps?1.0/P[index].steps:0.02;
                    view_begin(v,index);view_set(v,index,v->controller->values[index]+step*(delta/120.0));view_end(v);
                    return 0;}
            }break;
        }
        case WM_KEYDOWN:
            if(v->focused>=0&&(wp==VK_LEFT||wp==VK_RIGHT||wp==VK_HOME||wp==VK_END)){
                int index=v->focused;double step=P[index].steps?1.0/P[index].steps:0.01;
                double value=wp==VK_HOME?0.0:wp==VK_END?1.0:
                    v->controller->values[index]+(wp==VK_LEFT?-step:step);
                view_begin(v,index);view_set(v,index,value);view_end(v);return 0;
            }break;
        default:break;
    }
    return CallWindowProcW(next,hwnd,message,wp,lp);
}
static void view_detach(View*v){
    view_end(v);
    if(v->hwnd&&IsWindow(v->hwnd)){
        KillTimer(v->hwnd,0xD1A3);
        if(GetCapture()==v->hwnd)ReleaseCapture();
        if(v->oldproc)SetWindowLongPtrW(v->hwnd,GWLP_WNDPROC,(LONG_PTR)v->oldproc);
        RemovePropW(v->hwnd,view_key);
    }
    v->hwnd=NULL;v->oldproc=NULL;
    if(v->editor){live_ui_destroy(v->editor);v->editor=NULL;}
}
static uint32_t CALL view_add(void*i){View*v=i;return atomic_fetch_add(&v->refs,1)+1;}
static uint32_t CALL view_release(void*i){
    View*v=i;unsigned n=atomic_fetch_sub(&v->refs,1);
    if(n==1){
        view_detach(v);
        View**p=&v->controller->views;
        while(*p&&*p!=v)p=&(*p)->next;
        if(*p==v)*p=v->next;
        if(v->frame)MC(v->frame,2,uint32_t)(v->frame);
        release_c(v->controller);free(v);
    }return n-1;
}
static R CALL view_qi(void*i,const char*iid,void**out){
    if(!out)return E_ARG;
    *out=NULL;
    if(is_unknown(iid)||uidmatch(iid,0x5BC32507,0xD06049EA,0xA6151B52,0x2B755B29)){
        *out=i;view_add(i);return OK;
    }return E_NOIF;
}
static R CALL view_platform(void*i,const char*platform){(void)i;return valid_view_platform(platform)?OK:FAIL;}
static R CALL view_attached(void*i,void*parent,const char*platform){
    View*v=i;
    if(!parent||!valid_view_platform(platform)||v->editor)return FAIL;
    v->editor=live_ui_create((size_t)(uintptr_t)parent);
    if(!v->editor)return FAIL;
    v->hwnd=(HWND)(uintptr_t)live_ui_handle(v->editor);
    if(!v->hwnd||!IsWindow(v->hwnd)){view_detach(v);return FAIL;}
    if(!SetPropW(v->hwnd,view_key,(HANDLE)v)){view_detach(v);return FAIL;}
    SetLastError(0);
    v->oldproc=(WNDPROC)(LONG_PTR)SetWindowLongPtrW(v->hwnd,GWLP_WNDPROC,(LONG_PTR)view_window_proc);
    if(!v->oldproc){RemovePropW(v->hwnd,view_key);view_detach(v);return FAIL;}
    view_resize_window(v);
    if(!SetTimer(v->hwnd,0xD1A3,40,NULL)){view_detach(v);return FAIL;}
    return OK;
}
static R CALL view_removed(void*i){View*v=i;if(!v->editor)return OK;view_detach(v);return OK;}
static R CALL view_wheel(void*i,float dist){View*v=i;int j=v->focused;
    if(j<0)return FAIL;
    double step=P[j].steps?1.0/P[j].steps:0.02;
    view_begin(v,j);view_set(v,j,v->controller->values[j]+dist*step);view_end(v);return OK;
}
static R CALL view_key_down(void*i,uint16_t key,int16_t code,int16_t modifiers){
    (void)modifiers;View*v=i;int j=v->focused;if(j<0)return FAIL;
    if(key==37||key==39||code==37||code==39){double step=P[j].steps?1.0/P[j].steps:0.01;
        view_begin(v,j);view_set(v,j,v->controller->values[j]+((key==37||code==37)?-step:step));view_end(v);return OK;}
    return FAIL;
}
static R CALL view_key_up(void*i,uint16_t key,int16_t code,int16_t modifiers){(void)i;(void)key;(void)code;(void)modifiers;return FAIL;}
static R CALL view_get_size(void*i,ViewRect*out){if(!out)return E_ARG;View*v=i;
    out->left=0;out->top=0;out->right=v->width;out->bottom=v->height;return OK;}
static R CALL view_on_size(void*i,ViewRect*size){View*v=i;
    if(!size||size->right<=size->left||size->bottom<=size->top)return E_ARG;
    v->width=size->right-size->left;v->height=size->bottom-size->top;
    view_resize_window(v);return OK;
}
static R CALL view_focus(void*i,uint8_t enabled){View*v=i;
    if(enabled&&v->hwnd&&IsWindow(v->hwnd))SetFocus(v->hwnd);
    return OK;}
static R CALL view_set_frame(void*i,void*frame){View*v=i;
    if(frame)MC(frame,1,uint32_t)(frame);
    if(v->frame)MC(v->frame,2,uint32_t)(v->frame);
    v->frame=frame;return OK;
}
static R CALL view_can_resize(void*i){(void)i;return OK;}
static R CALL view_constraint(void*i,ViewRect*rect){(void)i;
    if(!rect)return E_ARG;
    int64_t w=(int64_t)rect->right-rect->left;
    if(w<640)w=640;
    if(w>1600)w=1600;
    int64_t h=(w*736+512)/1024;
    rect->right=rect->left+(int32_t)w;
    rect->bottom=rect->top+(int32_t)h;
    return OK;
}
static const void* vvt[15]={(void*)view_qi,(void*)view_add,(void*)view_release,
    (void*)view_platform,(void*)view_attached,(void*)view_removed,
    (void*)view_wheel,(void*)view_key_down,(void*)view_key_up,
    (void*)view_get_size,(void*)view_on_size,(void*)view_focus,
    (void*)view_set_frame,(void*)view_can_resize,(void*)view_constraint};
static void* create_view(Controller*c,const char*name){
    if(!name||strcmp(name,"editor"))return NULL;
    View*v=calloc(1,sizeof *v);if(!v)return NULL;
    v->iface.vt=vvt;atomic_init(&v->refs,1);v->controller=c;add_c(c);
    v->width=1024;v->height=736;v->drag=-1;v->focused=-1;v->dirty=1;
    v->next=c->views;c->views=v;
    return v;
}
#else
static void view_mark_dirty(Controller*c){(void)c;}
static void* create_view(Controller*c,const char*name){(void)c;(void)name;return NULL;}
#endif

static void*CALL view(void*i,const char*name){return create_view((Controller*)i,name);}

/* Static vtables, slots exactly match Steinberg's IUnknown-derived ordering. */
static const void* cvt[14]={(void*)qi_p,(void*)add_p,(void*)release_p,(void*)init_p,(void*)term_p,(void*)controller_class,(void*)set_io,(void*)bus_count,(void*)bus_info,(void*)routing,(void*)activate_bus,(void*)active_p,(void*)set_state_p,(void*)get_state_p};
static const void* avt[11]={(void*)qi_a,(void*)add_a,(void*)release_a,(void*)set_arr,(void*)get_arr,(void*)sample_type,(void*)latency,(void*)setup,(void*)processing,(void*)process_audio,(void*)tail};
static const void* evt[18]={(void*)qi_c,(void*)add_c,(void*)release_c,(void*)init_c,(void*)term_c,(void*)set_c,(void*)set_c,(void*)get_c,(void*)param_count,(void*)param_info,(void*)param_to_text,(void*)text_to_param,(void*)param_plain,(void*)param_norm,(void*)get_param,(void*)set_param,(void*)handler,(void*)view};
static atomic_uint refs=1;
static uint32_t CALL add_f(void*i){(void)i;return atomic_fetch_add(&refs,1)+1;}
static uint32_t CALL release_f(void*i){(void)i;unsigned n=atomic_load(&refs);while(n>1&&!atomic_compare_exchange_weak(&refs,&n,n-1)){}return n>1?n-1:1;}
static R CALL qi_f(void*i,const char*name,void**out){if(!out)return E_ARG;*out=NULL;if(!is_unknown(name)&&!is_factory(name))return E_NOIF;*out=i;add_f(i);return OK;}
static R CALL factory_info(void*i,FactoryInfo*out){(void)i;if(!out)return E_ARG;memset(out,0,sizeof *out);strncpy(out->vendor,"Independent Audio Research",sizeof(out->vendor)-1);out->flags=16;return OK;}
static int32_t CALL class_count(void*i){(void)i;return 2;}
static R CALL class_info(void*i,int32_t idx,ClassInfo*out){(void)i;if(!out||idx<0||idx>1)return E_ARG;memset(out,0,sizeof *out);
   class_id(out->cid,idx);out->cardinality=0x7fffffff;
   strcpy(out->category,idx?"Component Controller Class":"Audio Module Class");
   strcpy(out->name,idx?"CHORD-like LIVE DLM Controller":"CHORD-like LIVE DLM");return OK;}
static R CALL create(void*i,const char*cid,const char*name,void**out){(void)i;if(!cid||!name||!out)return E_ARG;*out=NULL;UID comp,ctrl;class_id(comp,0);class_id(ctrl,1);
  if(!memcmp(cid,comp,16)){
    if(!is_unknown(name)&&!is_component(name)&&!is_audio(name))return E_NOIF;
    Processor*p=calloc(1,sizeof *p);if(!p)return FAIL;p->component.vt=cvt;p->audio.vt=avt;atomic_init(&p->refs,1);
    defaults(p->values);for(int j=0;j<N;j++)p->applied[j]=-1;p->rate=48000;
    *out=is_audio(name)?(void*)&p->audio:(void*)&p->component;return OK;
  }
  if(!memcmp(cid,ctrl,16)){
    if(!is_unknown(name)&&!is_ctrl(name))return E_NOIF;
    Controller*c=calloc(1,sizeof *c);if(!c)return FAIL;c->iface.vt=evt;atomic_init(&c->refs,1);defaults(c->values);*out=c;return OK;
  }return E_NOIF;
}
static const void* fvt[7]={(void*)qi_f,(void*)add_f,(void*)release_f,(void*)factory_info,(void*)class_count,(void*)class_info,(void*)create};
static Obj factory={fvt};
API void *GetPluginFactory(void){add_f(&factory);return &factory;}
API int ModuleEntry(void*handle){(void)handle;return 1;}
API int ModuleExit(void){return 1;}
API int InitDll(void){return 1;}
API int ExitDll(void){return 1;}
