/* Spectral sea, unitary interacting quantum field and Schwarzschild rays.
   Included after the particle helpers. Shares only the HDR composition target. */
#include "physics_shaders.h"
static struct {
    up_uint ocean, blackhole, qinit, qstep, qview, copy;
    up_uint fbo, texture, wave[2];
    int width, height, qw, qh, front;
    uint64_t tick;
    unsigned seed;
    int quantum_ready;
    up_uint ocean_cached_program;
    unsigned ocean_seed,ocean_speed;
} up_phys;
static void up_physics_destroy(void) {
    up_uint programs[]={up_phys.ocean,up_phys.blackhole,up_phys.qinit,up_phys.qstep,up_phys.qview,up_phys.copy};
    for(unsigned i=0;i<sizeof programs/sizeof programs[0];i++)if(programs[i])up_glDeleteProgram(programs[i]);
    if(up_phys.fbo)up_glDeleteFramebuffers(1,&up_phys.fbo);
    if(up_phys.texture)dgl_glDeleteTextures(1,&up_phys.texture);
    if(up_phys.wave[0])up_glDeleteBuffers(2,up_phys.wave);
    memset(&up_phys,0,sizeof up_phys);
}
static int up_field_target(int w,int h) {
    if(up_phys.fbo&&up_phys.width==w&&up_phys.height==h)return 1;
    if(!up_phys.fbo)up_glGenFramebuffers(1,&up_phys.fbo);
    if(!up_phys.texture)dgl_glGenTextures(1,&up_phys.texture);
    up_int old_fbo=0,old_texture=0;
    dgl_glGetIntegerv(0x8CA6,&old_fbo);dgl_glGetIntegerv(0x8069,&old_texture);
    dgl_glBindTexture(0x0DE1,up_phys.texture);
    dgl_glTexParameteri(0x0DE1,0x2801,0x2601);dgl_glTexParameteri(0x0DE1,0x2800,0x2601);
    dgl_glTexParameteri(0x0DE1,0x2802,0x812F);dgl_glTexParameteri(0x0DE1,0x2803,0x812F);
    dgl_glTexImage2D(0x0DE1,0,0x881A,w,h,0,0x1908,0x1406,NULL);
    up_glBindFramebuffer(0x8CA9,up_phys.fbo);
    up_glFramebufferTexture2D(0x8CA9,0x8CE0,0x0DE1,up_phys.texture,0);
    int ok=up_glCheckFramebufferStatus(0x8CA9)==0x8CD5;
    up_glBindFramebuffer(0x8CA9,(up_uint)old_fbo);dgl_glBindTexture(0x0DE1,(up_uint)old_texture);
    if(!ok){up_log("physical field framebuffer","incomplete RGBA16F target");return 0;}
    up_phys.width=w;up_phys.height=h;return 1;
}
static float up_wave_number(float omega) {
    // Solve omega^2 = g k tanh(k h), h=50m, by Newton iteration.
    float k=fmaxf(omega*omega/9.81f,.00001f);
    for(int i=0;i<12;i++){
        float t=tanhf(k*50.f),df=9.81f*(t+k*50.f*(1.f-t*t));
        k=fmaxf(.000001f,k-(9.81f*k*t-omega*omega)/fmaxf(df,.00001f));
    }
    return k;
}
static unsigned up_phys_hash(unsigned x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
static float up_phys_rand(unsigned x){return (float)(up_phys_hash(x)&0xffffffu)/16777216.f;}
static float up_wave_amplitude(float omega,float domega) {
    const float peak=.7853981634f;
    float sigma=omega<=peak?.07f:.09f;
    float r=expf(-.5f*powf((omega-peak)/(sigma*peak),2.f));
    float S=.0081f*9.81f*9.81f*powf(omega,-5.f)*expf(-1.25f*powf(peak/omega,4.f))*powf(3.3f,r);
    return sqrtf(2.f*S*domega);
}
static void up_ocean_uniforms(up_uint p,unsigned seed,unsigned speed) {
    speed=speed?speed:1u;
    if(up_phys.ocean_cached_program==p&&up_phys.ocean_seed==seed&&up_phys.ocean_speed==speed)return;
    for(int j=0;j<80;j++){
        float omega,k,amplitude,direction;
        if(j<32){
            float ratio=powf(2.8f/.40f,1.f/31.f);
            omega=.40f*powf(ratio,(float)j);k=up_wave_number(omega);
            amplitude=up_wave_amplitude(omega,omega*(sqrtf(ratio)-1.f/sqrtf(ratio)));
            direction=-.7f+(up_phys_rand(seed+(unsigned)j*37u)-.5f)*2.1f;
            if(j%7==0)direction+=1.7f;
        }else{
            int micro=j>=56;float q=(float)(j-(micro?56:32))/23.f;
            k=micro?12.f*powf(160.f/12.f,q):.55f*powf(12.f/.55f,q);
            // Surface tension sigma/rho = 7.4e-5 m^3/s^2 adds gravity-capillary ripples.
            omega=sqrtf(9.81f*k+.000074f*k*k*k);
            amplitude=(micro?.020f:.038f)/k;
            direction=-.7f+(up_phys_rand(seed+(unsigned)j*37u)-.5f)*(micro?5.6f:3.6f);
            float exposure=omega*.045f*(float)(speed?speed:1u)*.5f;
            amplitude*=exposure>.001f?sinf(exposure)/exposure:1.f;
        }
        char name[40];snprintf(name,sizeof name,"uWaves[%d]",j);
        up_int loc=up_glGetUniformLocation(p,name);
        if(loc>=0)up_glUniform4f(loc,k*cosf(direction),k*sinf(direction),amplitude,omega);
        snprintf(name,sizeof name,"uPhases[%d]",j);
        up_uniform_f(p,name,up_phys_rand(seed+(unsigned)j*71u)*6.28318530718f);
    }
    up_phys.ocean_cached_program=p;up_phys.ocean_seed=seed;up_phys.ocean_speed=speed;
}
static void up_size_uniform(up_uint p,int w,int h) {
    up_int loc=up_glGetUniformLocation(p,"uSize");if(loc>=0)up_glUniform2i(loc,w,h);
}
static int up_quantum_init(int width,unsigned seed,uint64_t tick) {
    int height=width*3/4;
    if(up_phys.quantum_ready&&up_phys.qw==width&&up_phys.seed==seed&&tick>=up_phys.tick)return 1;
    if(!up_state.compute){up_log("quantum field","OpenGL 4.3 compute shaders required");return 0;}
    if(!up_phys.qinit)up_phys.qinit=up_program(NULL,NULL,up_sh_quantum_init);
    if(!up_phys.qstep)up_phys.qstep=up_program(NULL,NULL,up_sh_quantum_step);
    if(!up_phys.qview)up_phys.qview=up_program(up_sh_composite_vertex,up_sh_quantum_view,NULL);
    if(!up_phys.qinit||!up_phys.qstep||!up_phys.qview)return 0;
    if(!up_phys.wave[0])up_glGenBuffers(2,up_phys.wave);
    for(int b=0;b<2;b++){
        up_glBindBuffer(0x90D2,up_phys.wave[b]);up_glBufferData(0x90D2,(up_size)width*height*2*sizeof(float),NULL,0x88E8);
    }
    if(dgl_glGetError()!=0){up_log("quantum field","GPU allocation failed");return 0;}
    up_glBindBufferBase(0x90D2,2,up_phys.wave[0]);up_glUseProgram(up_phys.qinit);
    up_size_uniform(up_phys.qinit,width,height);up_uniform_u(up_phys.qinit,"uSeed",seed);
    up_glDispatchCompute(((unsigned)width*height+255u)/256u,1,1);up_glMemoryBarrier(0x2000);
    up_phys.qw=width;up_phys.qh=height;up_phys.front=0;up_phys.seed=seed;up_phys.tick=tick;up_phys.quantum_ready=1;
    return 1;
}
static void up_quantum_step(float dt,int pass,float mx,float my,float mb,unsigned flags) {
    up_uint p=up_phys.qstep;up_glUseProgram(p);up_size_uniform(p,up_phys.qw,up_phys.qh);
    up_uniform_i(p,"uPass",pass);up_uniform_i(p,"uFields",!!(flags&4));up_uniform_f(p,"uDt",dt);
    up_int loc=up_glGetUniformLocation(p,"uMouse");if(loc>=0)up_glUniform3f(loc,mx,my,mb);
    up_glBindBufferBase(0x90D2,2,up_phys.wave[up_phys.front]);up_glBindBufferBase(0x90D2,3,up_phys.wave[1-up_phys.front]);
    up_glDispatchCompute(((unsigned)up_phys.qw*up_phys.qh+255u)/256u,1,1);up_glMemoryBarrier(0x2000);
    up_phys.front=1-up_phys.front;
}
static int up_field_draw(unsigned mode,unsigned count,unsigned seed,uint64_t tick,unsigned flags,
                         float cx,float cy,float zoom,float mx,float my,float mb,
                         float rx,float ry,float rw,float rh,int viewport_w,int viewport_h) {
    int level=count>=5000000?2:(count>=1000000?1:0);
    float scale=mode==7?(level==2?1.f:(level==1?.82f:.60f)):(level==2?1.f:(level==1?.8f:.6f));
    // Fixed upper bounds keep even a huge resized window within a deliberate ray/field budget.
    int fw=(int)fmaxf(96.f,fminf(rw*scale,mode==7?1600.f:1800.f));
    int fh=(int)fmaxf(72.f,(float)fw*rh/rw);
    if(fh>1200){fw=(int)((float)fw*1200.f/fh);fh=1200;}
    if(mode==6){fw=level==2?1024:(level==1?512:256);fh=fw*3/4;}
    if(!up_field_target(fw,fh))return 0;
    if(!up_phys.copy)up_phys.copy=up_program(up_sh_composite_vertex,up_sh_field_copy,NULL);
    if(!up_phys.copy)return 0;
    up_uint program=0;
    if(mode==3){if(!up_phys.ocean)up_phys.ocean=up_program(up_sh_composite_vertex,up_sh_ocean,NULL);program=up_phys.ocean;}
    else if(mode==7){if(!up_phys.blackhole)up_phys.blackhole=up_program(up_sh_composite_vertex,up_sh_blackhole,NULL);program=up_phys.blackhole;}
    else if(mode==6){
        if(!up_quantum_init(fw,seed,tick))return 0;
        unsigned steps=tick>up_phys.tick?(unsigned)fmin((double)(tick-up_phys.tick),3.):0;
        unsigned refinement=(unsigned)(up_phys.qw/256);
        unsigned substeps=refinement*refinement;
        for(unsigned i=0;i<steps;i++)for(unsigned sub=0;sub<substeps;sub++){
            // Strang composition; dt scales with dx^2 so refinement improves accuracy
            // instead of changing the discrete propagation law at the same model time.
            const float h=.24f/(float)substeps;
            up_quantum_step(h*.5f,4,mx,my,mb,flags);
            for(int j=0;j<3;j++)up_quantum_step(h*.5f,j,mx,my,mb,flags);
            up_quantum_step(h,3,mx,my,mb,flags);
            for(int j=2;j>=0;j--)up_quantum_step(h*.5f,j,mx,my,mb,flags);
            up_quantum_step(h*.5f,4,mx,my,mb,flags);
        }
        up_phys.tick=tick;up_glBindBufferBase(0x90D2,2,up_phys.wave[up_phys.front]);program=up_phys.qview;
    }
    if(!program)return 0;
    up_glBindFramebuffer(0x8CA9,up_phys.fbo);dgl_glViewport(0,0,fw,fh);
    dgl_glDisable(0x0C11);dgl_glDisable(0x0BE2);up_glUseProgram(program);
    up_uniform_i(program,"uQuality",level);
    up_uniform_f(program,"uTime",(float)tick*(mode==3?.045f:1.2f));
    up_uniform_u(program,"uSeed",seed);up_uniform_i(program,"uFields",!!(flags&4));up_uniform_i(program,"uDetail",!!(flags&8));
    up_int loc=up_glGetUniformLocation(program,"uResolution");if(loc>=0)up_glUniform2f(loc,(float)fw,(float)fh);
    loc=up_glGetUniformLocation(program,"uCamera");if(loc>=0)up_glUniform3f(loc,cx,cy,zoom);
    if(mode==3)up_ocean_uniforms(program,seed,flags&3u);
    if(mode==6)up_size_uniform(program,up_phys.qw,up_phys.qh);
    up_glDrawArrays(0x0004,0,3);
    // Stretch the bounded offscreen field into exactly the application's canvas.
    up_glBindFramebuffer(0x8CA9,up_state.hdr_fbo);
    dgl_glViewport((int)rx,(int)((float)viewport_h-ry-rh),(int)rw,(int)rh);
    up_int active=0,texture=0;dgl_glGetIntegerv(0x84E0,&active);up_glActiveTexture(0x84C0);
    dgl_glGetIntegerv(0x8069,&texture);dgl_glBindTexture(0x0DE1,up_phys.texture);
    up_glUseProgram(up_phys.copy);up_uniform_i(up_phys.copy,"uField",0);
    up_uniform_i(up_phys.copy,"uBloom",mode==7?((flags&4u)?1:2):0);
    loc=up_glGetUniformLocation(up_phys.copy,"uFieldSize");if(loc>=0)up_glUniform2f(loc,(float)fw,(float)fh);
    up_glDrawArrays(0x0004,0,3);
    dgl_glBindTexture(0x0DE1,(up_uint)texture);up_glActiveTexture((up_enum)active);
    dgl_glViewport(0,0,viewport_w,viewport_h);dgl_glEnable(0x0BE2);dgl_glBlendFunc(1,1);
    return dgl_glGetError()==0;
}

static void up_physics_stats(FILE *f) {
    if(up_state.mode==1)fprintf(f,"Model: disk/bulge/NFW + rotating bar; adaptive KDK <=0.25 Myr; finite-lived blue star tracers\n");
    if(up_state.mode==5)fprintf(f,"Model: clustered halo/filament statistical catalogue; local slab / linear or logarithmic horizon slice, not observed data\n");
    if(up_state.mode==3||up_state.mode==6||up_state.mode==7){
        fprintf(f,"Field/ray target: %dx%d RGBA16F (quality, not particle count)\n",up_phys.width,up_phys.height);
        if(up_state.mode==3)fprintf(f,"Model: 80 spectral components: JONSWAP + short gravity waves + capillary ripples, depth 50m\n");
        if(up_state.mode==6)fprintf(f,"Model: 2D Gross-Pitaevskii; exact unitary bond splitting; grid %dx%d; complex field %.2f MiB\n",up_phys.qw,up_phys.qh,(double)up_phys.qw*up_phys.qh*16./1048576.);
        if(up_state.mode==7)fprintf(f,"Model: Schwarzschild null geodesics (RK4), nonrotating; sheared turbulent emission disk with smooth taper r=6..42 GM/c^2\n");
    }
}
