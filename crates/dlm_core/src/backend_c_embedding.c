/* Embedding adapter for the existing DLM vector window renderer. Parent and
 * attached handles are borrowed from the host for the entire editor lifetime. */
static DV *dvb_win_child(DV *pv,DV *tv,DV *wv,DV *hv) {
    nat_t parent=dv_nat_of(pv,"win_child");
    if(!parent || parent>UINTPTR_MAX)return dvo_errs("invalid parent handle");
    return dvw_open_parent(tv,wv,hv,parent);
}
static DV *dvb_win_attach(DV *pv,DV *wv,DV *hv) {
    nat_t handle=dv_nat_of(pv,"win_attach"),w=dv_nat_of(wv,"win_attach"),h=dv_nat_of(hv,"win_attach");
    if(!handle || handle>UINTPTR_MAX || !w || !h || w>16384 || h>16384)return dvo_errs("invalid attached window or size");
    int slot=dvw_slot();if(slot<0)return dvo_errs("too many windows");
#ifdef _WIN32
    HWND window=(HWND)(uintptr_t)handle;
    if(!IsWindow(window) || GetWindowThreadProcessId(window,0)!=GetCurrentThreadId())return dvo_errs("attached window must belong to the current thread");
    dvw_win *x=&dvw_wins[slot];memset(x,0,sizeof *x);x->hwnd=window;x->w=(int)w;x->h=(int)h;x->borrowed=1;x->used=1;
    SetLastError(0);x->host_proc=(WNDPROC)SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)dvw_proc);
    if(!x->host_proc && GetLastError()){x->used=0;return dvo_errs("cannot subclass host window");}
#else
    if(!dvw_load())return dvo_errs("cannot load libX11");
    void *d=dvw_XOpenDisplay(0);if(!d)return dvo_errs("cannot open display");
    dvw_win *x=&dvw_wins[slot];memset(x,0,sizeof *x);x->dpy=d;x->win=(unsigned long)handle;x->w=(int)w;x->h=(int)h;x->borrowed=1;
    if(!dvw_surface(x)){dvw_XCloseDisplay(d);return dvo_errs("unsupported host visual");}
    dvw_XSelectInput(d,x->win,(1L<<15)|(1L<<17));
    x->used=1;dvw_XFlush(d);
#endif
    return dv_result(dv_nat((nat_t)slot+1),1);
}
static DV *dvb_win_resize(DV *iv,DV *wv,DV *hv) {
    dvw_win *x=dvw_get(dv_nat_of(iv,"win_resize"));nat_t w=dv_nat_of(wv,"win_resize"),h=dv_nat_of(hv,"win_resize");
    if(!x||!w||!h||w>16384||h>16384)return dvo_errs("invalid window or size");
#ifdef _WIN32
    RECT rc={0,0,(LONG)w,(LONG)h};AdjustWindowRectEx(&rc,(DWORD)GetWindowLongPtrW(x->hwnd,GWL_STYLE),GetMenu(x->hwnd)!=NULL,(DWORD)GetWindowLongPtrW(x->hwnd,GWL_EXSTYLE));
    if(!SetWindowPos(x->hwnd,0,0,0,rc.right-rc.left,rc.bottom-rc.top,SWP_NOMOVE|SWP_NOZORDER))return dvo_errs("cannot resize window");
#else
    int(*resize)(void*,unsigned long,unsigned,unsigned)=0;*(void**)(&resize)=dlsym(dvw_x11,"XResizeWindow");if(!resize)return dvo_errs("XResizeWindow unavailable");resize(x->dpy,x->win,(unsigned)w,(unsigned)h);dvw_XFlush(x->dpy);
#endif
    x->w=(int)w;x->h=(int)h;dgl_resize(x);return dv_result(dv_nat(0),1);
}
static DV *dvb_win_focus(DV *iv) {
    dvw_win *x=dvw_get(dv_nat_of(iv,"win_focus"));if(!x)return dvo_errs("invalid window");
#ifdef _WIN32
    SetFocus(x->hwnd);
#else
    int(*focus)(void*,unsigned long,int,unsigned long)=0;*(void**)(&focus)=dlsym(dvw_x11,"XSetInputFocus");if(!focus)return dvo_errs("XSetInputFocus unavailable");focus(x->dpy,x->win,1,0);dvw_XFlush(x->dpy);
#endif
    return dv_result(dv_nat(0),1);
}
static DV *dvb_win_dpi(DV *iv) {
    dvw_win *x=dvw_get(dv_nat_of(iv,"win_dpi"));if(!x)return dvo_errs("invalid window");double dpi=96.0;
#ifdef _WIN32
    UINT(WINAPI *getdpi)(HWND)=(UINT(WINAPI*)(HWND))GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow");
    if(getdpi)dpi=(double)getdpi(x->hwnd);else{HDC dc=GetDC(x->hwnd);if(dc){dpi=(double)GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(x->hwnd,dc);}}
#else
    int(*width)(void*,int)=0,*dummy=0;int(*mm)(void*,int)=0;(void)dummy;
    *(void**)(&width)=dlsym(dvw_x11,"XDisplayWidth");*(void**)(&mm)=dlsym(dvw_x11,"XDisplayWidthMM");int s=dvw_XDefaultScreen(x->dpy);if(width&&mm){int m=mm(x->dpy,s);if(m>0)dpi=(double)width(x->dpy,s)*25.4/m;}
#endif
    if(!(dpi>0.0)||!isfinite(dpi))dpi=96.0;return dv_result(dvf_float(dpi),1);
}
