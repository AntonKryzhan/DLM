/* This adapter reuses dvw/dvy/dgl, including the procedural vector scene
 * format, input method, GPU contexts and their existing cleanup routines. */
typedef struct dn_window {size_t id;uint64_t owner;} dn_window;
static atomic_flag dn_ui_mutex=ATOMIC_FLAG_INIT;
static DN_TLS unsigned dn_ui_depth;
static DN_TLS char dn_ui_message[512];
static void dn_ui_enter(void){if(!dn_ui_depth++)while(atomic_flag_test_and_set_explicit(&dn_ui_mutex,memory_order_acquire)){};}
static void dn_ui_leave(void){if(!--dn_ui_depth)atomic_flag_clear_explicit(&dn_ui_mutex,memory_order_release);}
typedef struct dn_ui_guard {struct dn_ui_guard *previous;dv_region *base;unsigned depth;unsigned char used[DVW_MAX];int gl_saved;
#ifdef _WIN32
HDC gl_dc;HGLRC gl_context;
#else
void *gl_display,*gl_context;unsigned long gl_drawable;
#endif
} dn_ui_guard;
static void dn_ui_save_gl(dn_ui_guard *g){if(g->gl_saved)return;g->gl_saved=1;if(!dgl_load())return;
#ifdef _WIN32
 HGLRC(WINAPI *context)(void)=(HGLRC(WINAPI*)(void))GetProcAddress(dgl_lib,"wglGetCurrentContext");HDC(WINAPI *dc)(void)=(HDC(WINAPI*)(void))GetProcAddress(dgl_lib,"wglGetCurrentDC");if(context)g->gl_context=context();if(dc)g->gl_dc=dc();
#else
 void *(*display)(void)=0,*(*context)(void)=0;unsigned long(*drawable)(void)=0;
 *(void**)(&display)=dlsym(dgl_lib,"glXGetCurrentDisplay");*(void**)(&context)=dlsym(dgl_lib,"glXGetCurrentContext");*(void**)(&drawable)=dlsym(dgl_lib,"glXGetCurrentDrawable");if(display)g->gl_display=display();if(context)g->gl_context=context();if(drawable)g->gl_drawable=drawable();
#endif
}
static void dn_ui_restore_gl(dn_ui_guard *g){if(!g->gl_saved||!dgl_lib)return;
#ifdef _WIN32
 dgl_wglMakeCurrent(g->gl_dc,g->gl_context);
#else
 void *display=g->gl_display;if(!display){void *(*current)(void)=0;*(void**)(&current)=dlsym(dgl_lib,"glXGetCurrentDisplay");if(current)display=current();}if(display)dgl_glXMakeCurrent(display,g->gl_drawable,g->gl_context);
#endif
}
static void dn_ui_close_slot(size_t i){dvw_win *x=&dvw_wins[i];if(!x->used)return;
#ifdef _WIN32
    dgl_close(x);if(x->borrowed){if(x->host_proc)SetWindowLongPtrW(x->hwnd,GWLP_WNDPROC,(LONG_PTR)x->host_proc);}else DestroyWindow(x->hwnd);free(x->pix);x->pix=NULL;
#else
    if(x->ic&&dvw_XDestroyIC)dvw_XDestroyIC(x->ic);if(x->im&&dvw_XCloseIM)dvw_XCloseIM(x->im);dgl_close(x);if(!x->borrowed)dvw_XDestroyWindow(x->dpy,x->win);dvw_free_gc(x);dvw_XCloseDisplay(x->dpy);
#endif
    x->used=0;
}
static void dn_ui_unwind(void *target){while(dn_foreign_context&&dn_foreign_context!=target){dn_ui_guard *g=dn_foreign_context;dn_foreign_context=g->previous;while(dv_current!=g->base)dv_region_leave(dv_current,0);for(size_t i=0;i<DVW_MAX;i++)if(!g->used[i]&&dvw_wins[i].used&&!dvw_wins[i].native_managed)dn_ui_close_slot(i);dn_ui_restore_gl(g);while(dn_ui_depth>g->depth)dn_ui_leave();}}
static void dn_ui_begin(dn_ui_guard *g){memset(g,0,sizeof *g);g->previous=dn_foreign_context;g->depth=dn_ui_depth;dn_ui_enter();g->base=dv_current;for(size_t i=0;i<DVW_MAX;i++)g->used[i]=(unsigned char)dvw_wins[i].used;dn_foreign_context=g;dn_foreign_unwind=dn_ui_unwind;}
static void dn_ui_end(dn_ui_guard *g){dn_ui_restore_gl(g);dn_foreign_context=g->previous;dn_ui_leave();}
static _Noreturn void dn_legacy_error(const char *message){size_t n=strlen(message);if(n>=sizeof dn_ui_message)n=sizeof dn_ui_message-1;memcpy(dn_ui_message,message,n);dn_ui_message[n]=0;dn_fail(dn_ui_message);}
static int dn_ui_ok(DV *r){if(r->u.res.ok)return 1;DV *e=r->u.res.v;size_t n=e->u.text.n;if(n>=sizeof dn_ui_message)n=sizeof dn_ui_message-1;memcpy(dn_ui_message,e->u.text.s,n);dn_ui_message[n]=0;return 0;}
static void dn_window_final(void *p){dn_window *w=p;if(!w->id)return;dn_ui_guard guard;dn_ui_begin(&guard);if(dvw_wins[w->id-1].gpu)dn_ui_save_gl(&guard);dn_ui_close_slot(w->id-1);dn_ui_end(&guard);w->id=0;}
static dn_window *dn_window_create(int kind,size_t parent,dn_utf8 title,size_t width,size_t height){
    dn_window *w=dn_allocate(sizeof *w);w->owner=dn_thread_id();
    dn_ui_guard guard;dn_ui_begin(&guard);dv_region region;dv_region_enter(&region);
    DV *r=kind==0?dvb_win_open(dv_text((const char*)title.data,title.len),dv_nat(width),dv_nat(height)):kind==1?dvb_win_child(dv_nat(parent),dv_text((const char*)title.data,title.len),dv_nat(width),dv_nat(height)):dvb_win_attach(dv_nat(parent),dv_nat(width),dv_nat(height));
    int ok=dn_ui_ok(r);if(ok){w->id=(size_t)dv_nat_value(r->u.res.v);((dn_mem*)w-1)->finalize=dn_window_final;dvw_wins[w->id-1].native_managed=1;}
    dv_region_leave(&region,0);dn_ui_end(&guard);
    if(!ok)dn_fail(dn_ui_message);return w;
}
static dn_window *dn_window_live(dn_window *w){dn_nonnull(w);dn_check_thread(w->owner);if(!w->id)dn_fail("closed window");return w;}
static void dn_window_drop(dn_window *volatile *p){if(*p){dn_check_thread((*p)->owner);dn_window_final(*p);((dn_mem*)*p-1)->finalize=0;dn_deallocate(*p);*p=0;}}
static void dn_window_retain(dn_window *volatile *p){if(*p)dn_detach(*p);}
static uint8_t dn_window_action(dn_window *w,int action,size_t width,size_t height){dn_window_live(w);dn_ui_guard guard;dn_ui_begin(&guard);if(action==2||dvw_wins[w->id-1].gpu)dn_ui_save_gl(&guard);dv_region region;dv_region_enter(&region);DV *r=action==0?dvb_win_resize(dv_nat(w->id),dv_nat(width),dv_nat(height)):action==1?dvb_win_focus(dv_nat(w->id)):dvb_win_gpu(dv_nat(w->id));int ok=dn_ui_ok(r);dv_region_leave(&region,0);dn_ui_end(&guard);if(!ok)dn_fail(dn_ui_message);return 0;}
static double dn_window_dpi(dn_window *w){dn_window_live(w);dn_ui_guard guard;dn_ui_begin(&guard);dv_region region;dv_region_enter(&region);DV *r=dvb_win_dpi(dv_nat(w->id));int ok=dn_ui_ok(r);double dpi=ok?r->u.res.v->u.f:0;dv_region_leave(&region,0);dn_ui_end(&guard);if(!ok)dn_fail(dn_ui_message);return dpi;}
static void dn_window_poll(dn_window *w,size_t ms,uint64_t event[4]){dn_window_live(w);dn_ui_guard guard;dn_ui_begin(&guard);if(dvw_wins[w->id-1].gpu)dn_ui_save_gl(&guard);dv_region region;dv_region_enter(&region);DV *r=dvb_win_poll(dv_nat(w->id),dv_nat(ms));int ok=dn_ui_ok(r);if(ok)for(size_t i=0;i<4;i++)event[i]=(uint64_t)r->u.res.v->u.nv.d[i];dv_region_leave(&region,0);dn_ui_end(&guard);if(!ok)dn_fail(dn_ui_message);}
static uint8_t dn_window_scene(dn_window *w,const uint64_t *scene,size_t count,size_t width,size_t height,size_t num,size_t den){dn_window_live(w);dn_ui_guard guard;dn_ui_begin(&guard);if(dvw_wins[w->id-1].gpu)dn_ui_save_gl(&guard);dv_region region;dv_region_enter(&region);nat_t *items=dv_alloc(dn_size(count,sizeof(nat_t)));for(size_t i=0;i<count;i++)items[i]=scene[i];DV *commands=dv_new(DV_NVEC);commands->u.nv.d=items;commands->u.nv.n=count;DV *r=dvb_win_present_scene(dv_nat(w->id),commands,dv_nat(width),dv_nat(height),dv_nat(num),dv_nat(den));int ok=dn_ui_ok(r);dv_region_leave(&region,0);dn_ui_end(&guard);if(!ok)dn_fail(dn_ui_message);return 0;}

static void dn_legacy_shutdown(void){dgl_fbo_ok=-1;
#ifdef _WIN32
 if(dvw_class){UnregisterClassW(dvw_class_name,dvw_instance);dvw_class=0;}if(dgl_lib){FreeLibrary(dgl_lib);dgl_lib=0;}
#else
 if(dgl_lib){dlclose(dgl_lib);dgl_lib=0;}if(dvw_x11){dlclose(dvw_x11);dvw_x11=0;}
#endif
}
static uint8_t dn_window_input(dn_window *w,size_t kind,size_t a,size_t b,size_t c){dn_window_live(w);if(kind<1||kind>9)dn_fail("invalid window event kind");dn_ui_guard guard;dn_ui_begin(&guard);dvw_push(&dvw_wins[w->id-1],kind,a,b,c);dn_ui_end(&guard);return 0;}
static size_t dn_window_handle(dn_window *w){dn_window_live(w);
#ifdef _WIN32
 return (size_t)(uintptr_t)dvw_wins[w->id-1].hwnd;
#else
 return (size_t)dvw_wins[w->id-1].win;
#endif
}
static uint64_t *dn_window_read(dn_window *w,size_t *count){dn_window_live(w);dn_ui_guard guard;dn_ui_begin(&guard);dn_ui_save_gl(&guard);dv_region region;dv_region_enter(&region);DV *r=dvb_win_read(dv_nat(w->id));int ok=dn_ui_ok(r);uint64_t *out=NULL;if(ok){*count=r->u.res.v->u.nv.n;out=dn_allocate(dn_size(*count,sizeof *out));for(size_t i=0;i<*count;i++)out[i]=(uint64_t)r->u.res.v->u.nv.d[i];}dv_region_leave(&region,0);dn_ui_end(&guard);if(!ok)dn_fail(dn_ui_message);return out;}
#ifdef _WIN32
static LRESULT dn_ui_call_host(WNDPROC proc,HWND h,UINT m,WPARAM wp,LPARAM lp){
 dn_ui_guard *host=dn_foreign_context;while(host&&!host->gl_saved)host=host->previous;dn_ui_guard saved={0};if(host){dn_ui_save_gl(&saved);dn_ui_restore_gl(host);}
 unsigned depth=dn_ui_depth;while(dn_ui_depth)dn_ui_leave();LRESULT result=CallWindowProcW(proc,h,m,wp,lp);while(dn_ui_depth<depth)dn_ui_enter();if(host)dn_ui_restore_gl(&saved);return result;
}
static LRESULT CALLBACK dvw_proc(HWND h,UINT m,WPARAM wp,LPARAM lp){
 if(!dn_enter())return DefWindowProcW(h,m,wp,lp);dn_boundary b={0};jmp_buf jump;b.jump=&jump;b.previous=dn_current;b.foreign_marker=dn_foreign_context;dn_current=&b;dn_fp_begin(&b);
 if(setjmp(jump)){dn_boundary_end(&b,1);dn_leave();return 0;}
 dn_ui_guard guard;dn_ui_begin(&guard);dvw_win *x=dvw_by_hwnd(h);if(x&&x->gpu)dn_ui_save_gl(&guard);LRESULT result=dn_legacy_proc(h,m,wp,lp);dn_ui_end(&guard);dn_boundary_end(&b,0);dn_leave();return result;
}
#endif
