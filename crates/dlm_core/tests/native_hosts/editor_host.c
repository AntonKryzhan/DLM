#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <fenv.h>
#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#define OPEN(p) LoadLibraryA(p)
#define SYM(p,n) GetProcAddress(p,n)
#define CLOSE(p) FreeLibrary(p)
typedef HWND surface_t;
static unsigned host_buttons;static HGLRC expected_host_context;
static void *(*reentrant_create)(size_t,bool);static void(*fail_gui_allocation)(void);static void *retained_editor;static int test_parent_reentry;
static LRESULT CALLBACK host_proc(HWND h,UINT m,WPARAM wp,LPARAM lp){if(expected_host_context&&m==WM_PAINT)assert(wglGetCurrentContext()==expected_host_context);if(m==WM_PARENTNOTIFY&&LOWORD(wp)==WM_CREATE&&test_parent_reentry){test_parent_reentry=0;retained_editor=reentrant_create((size_t)h,false);assert(retained_editor);fail_gui_allocation();}if(m==WM_LBUTTONDOWN)host_buttons++;return DefWindowProcW(h,m,wp,lp);}
static HWND make_surface(HWND parent,int x,int y,int w,int h){HWND v=CreateWindowW(L"DLMValidationHost",L"host",parent?WS_CHILD|WS_VISIBLE:WS_OVERLAPPEDWINDOW|WS_VISIBLE,x,y,w,h,parent,NULL,GetModuleHandleW(NULL),NULL);assert(v);return v;}
#else
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <dlfcn.h>
#include <pthread.h>
#define OPEN(p) dlopen(p,RTLD_NOW|RTLD_LOCAL)
#define SYM(p,n) dlsym(p,n)
#define CLOSE(p) dlclose(p)
typedef Window surface_t;
static Display *display;
static surface_t make_surface(surface_t parent,int x,int y,int w,int h){Window v=XCreateSimpleWindow(display,parent,x,y,w,h,0,0,0);XSelectInput(display,v,ButtonPressMask|StructureNotifyMask);XMapWindow(display,v);XSync(display,False);return v;}
#endif
#define LOAD(type,n) type n=(type)(uintptr_t)SYM(module,#n);assert(n)
typedef void(*Destroy)(void*);typedef const char*(*Error)(void);
static Destroy destroy_on_other_thread;static Error error_on_other_thread;static void *other_editor;
#ifdef _WIN32
static DWORD WINAPI wrong_thread(void *unused)
#else
static void *wrong_thread(void *unused)
#endif
{(void)unused;destroy_on_other_thread(other_editor);assert(error_on_other_thread());return 0;}
static void preserve_gl(void *expected){
#ifdef _WIN32
 assert((void*)wglGetCurrentContext()==expected);
#else
 assert((void*)glXGetCurrentContext()==expected);
#endif
 GLfloat c[4];GLint v[4];glGetFloatv(GL_COLOR_CLEAR_VALUE,c);glGetIntegerv(GL_VIEWPORT,v);assert(c[0]>.12f&&c[0]<.13f&&c[1]>.24f&&c[1]<.26f);assert(v[0]==1&&v[1]==2&&v[2]==17&&v[3]==19);assert(fegetround()==FE_DOWNWARD);
}
int main(int argc,char **argv){setvbuf(stdout,NULL,_IONBF,0);setvbuf(stderr,NULL,_IONBF,0);assert(argc==2);
#ifdef _WIN32
 SetErrorMode(0x8003);_set_error_mode(_OUT_TO_STDERR);
 WNDCLASSW wc={0};wc.style=CS_OWNDC;wc.lpfnWndProc=host_proc;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"DLMValidationHost";assert(RegisterClassW(&wc));
 surface_t parent=make_surface(NULL,10,10,480,320);assert(SetWindowPos(parent,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW));HDC dc=GetDC(parent);PIXELFORMATDESCRIPTOR pfd={0};pfd.nSize=sizeof pfd;pfd.nVersion=1;pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pfd.iPixelType=PFD_TYPE_RGBA;pfd.cColorBits=32;pfd.cStencilBits=8;int pf=ChoosePixelFormat(dc,&pfd);assert(pf&&SetPixelFormat(dc,pf,&pfd));HGLRC context=wglCreateContext(dc);assert(context&&wglMakeCurrent(dc,context));expected_host_context=context;
#else
 display=XOpenDisplay(NULL);assert(display);int screen=DefaultScreen(display);int attrs[]={GLX_RGBA,GLX_DOUBLEBUFFER,GLX_STENCIL_SIZE,8,None};XVisualInfo *visual=glXChooseVisual(display,screen,attrs);assert(visual);
 Colormap colormap=XCreateColormap(display,RootWindow(display,screen),visual->visual,AllocNone);XSetWindowAttributes wa={0};wa.colormap=colormap;wa.event_mask=StructureNotifyMask;surface_t parent=XCreateWindow(display,RootWindow(display,screen),10,10,480,320,0,visual->depth,InputOutput,visual->visual,CWColormap|CWEventMask,&wa);XMapWindow(display,parent);XSync(display,False);GLXContext context=glXCreateContext(display,visual,NULL,True);assert(context&&glXMakeCurrent(display,parent,context));
#endif
 assert(glGetString(GL_VERSION));printf("host OpenGL: %s / %s\n",glGetString(GL_VERSION),glGetString(GL_RENDERER));
 glClearColor(.125f,.25f,.5f,1.f);glViewport(1,2,17,19);fesetround(FE_DOWNWARD);
 void *module=(void*)OPEN(argv[1]);assert(module);
 typedef void*(*Create)(size_t,bool);typedef void*(*Attach)(size_t);typedef size_t(*Handle)(void*);typedef void(*Resize)(void*,size_t,size_t);typedef void(*Draw)(void*,size_t,size_t,uint64_t);typedef void(*Focus)(void*);typedef double(*Dpi)(void*);typedef void(*Input)(void*,size_t,size_t,size_t,size_t);typedef uint64_t(*Poll)(void*);typedef uint64_t(*Pixel)(void*,size_t,size_t);typedef bool(*State)(void);typedef uint64_t(*Count)(void);
 LOAD(Create,editor_create);LOAD(Attach,editor_attach);LOAD(Handle,editor_handle);LOAD(Resize,editor_resize);LOAD(Draw,editor_draw);LOAD(Focus,editor_focus);LOAD(Focus,editor_gpu);LOAD(Dpi,editor_dpi);LOAD(Input,editor_input);LOAD(Poll,editor_poll);LOAD(Pixel,editor_pixel);LOAD(Destroy,editor_destroy);LOAD(Error,dlm_native_last_error);LOAD(State,dlm_native_runtime_shutdown);LOAD(State,dlm_native_runtime_init);LOAD(Count,dlm_native_live_allocations);

#ifdef _WIN32
 reentrant_create=editor_create;fail_gui_allocation=(void(*)(void))(uintptr_t)SYM(module,"dlm_test_fail_next_gui_allocation");assert(fail_gui_allocation);test_parent_reentry=1;
 void *failed=editor_create((size_t)parent,false);assert(!failed&&dlm_native_last_error());assert(retained_editor);surface_t retained_window=(surface_t)editor_handle(retained_editor);assert(IsWindow(retained_window));editor_draw(retained_editor,64,64,UINT64_C(0x335577ff));assert(!dlm_native_last_error());editor_destroy(retained_editor);assert(!dlm_native_last_error());preserve_gl((void*)context);
#endif
 void *a=editor_create((size_t)parent,false);assert(a&&!dlm_native_last_error());void *b=editor_create((size_t)parent,true);if(!b)fprintf(stderr,"GPU: %s\n",dlm_native_last_error());assert(b&&!dlm_native_last_error());preserve_gl((void*)context);
 surface_t aw=(surface_t)editor_handle(a),bw=(surface_t)editor_handle(b);assert(aw&&bw&&aw!=bw);
#ifdef _WIN32
 SetWindowPos(bw,NULL,150,0,64,64,SWP_NOZORDER);assert(GetParent(aw)==parent&&GetParent(bw)==parent);
#else
 XMoveWindow(display,bw,150,0);Window root,found,*children=NULL;unsigned n=0;assert(XQueryTree(display,aw,&root,&found,&children,&n)&&found==parent);if(children)XFree(children);
#endif
 editor_resize(a,96,80);editor_resize(b,96,80);editor_focus(a);assert(editor_dpi(a)>0&&editor_dpi(b)>0);editor_draw(a,96,80,UINT64_C(0x123456ff));editor_draw(b,96,80,UINT64_C(0x654321ff));assert(!dlm_native_last_error());assert(editor_pixel(b,40,40)==UINT64_C(0x654321ff));preserve_gl((void*)context);
#ifdef _WIN32
 HDC child_dc=GetDC(aw);SetLastError(0);COLORREF pixel=GetPixel(child_dc,40,40);DWORD pixel_error=GetLastError();RECT rect,clip;GetWindowRect(aw,&rect);int clip_kind=GetClipBox(child_dc,&clip);printf("CPU pixel=%08lx expected=%08lx error=%lu visible=%d window=%ld,%ld,%ld,%ld clip=%d:%ld,%ld,%ld,%ld parent_style=%llx\n",(unsigned long)pixel,(unsigned long)RGB(0x12,0x34,0x56),pixel_error,IsWindowVisible(aw),rect.left,rect.top,rect.right,rect.bottom,clip_kind,clip.left,clip.top,clip.right,clip.bottom,(unsigned long long)GetWindowLongPtrW(parent,GWL_STYLE));assert(pixel==RGB(0x12,0x34,0x56));ReleaseDC(aw,child_dc);
#else
 XSync(display,False);XImage *image=XGetImage(display,aw,40,40,1,1,AllPlanes,ZPixmap);assert(image);assert((XGetPixel(image,0,0)&0xffffff)==0x123456);XDestroyImage(image);
#endif
 for(unsigned i=0;i<100&&editor_poll(a);i++){};
#ifdef _WIN32
 SendMessageW(aw,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(5,6));SendMessageW(aw,WM_CHAR,'Z',0);SendMessageW(aw,WM_MOUSEWHEEL,MAKEWPARAM(0,120),MAKELPARAM(20,20));
#else
 XEvent ev={0};ev.xbutton.display=display;ev.xbutton.window=aw;ev.xbutton.root=RootWindow(display,screen);ev.xbutton.same_screen=True;ev.xbutton.x=5;ev.xbutton.y=6;ev.xbutton.button=1;ev.xbutton.type=ButtonPress;XSendEvent(display,aw,False,ButtonPressMask,&ev);ev.xkey.type=KeyPress;ev.xkey.keycode=XKeysymToKeycode(display,XK_z);XSendEvent(display,aw,False,KeyPressMask,&ev);ev.xbutton.type=ButtonPress;ev.xbutton.button=4;XSendEvent(display,aw,False,ButtonPressMask,&ev);XSync(display,False);
#endif
 unsigned events=0;for(unsigned i=0;i<100;i++){uint64_t kind=editor_poll(a);if(!kind)break;if(kind<32)events|=1u<<(unsigned)kind;}assert((events&((1u<<2)|(1u<<5)|(1u<<8)))==((1u<<2)|(1u<<5)|(1u<<8)));
 surface_t borrowed=make_surface(parent,0,130,64,64);void *attached=editor_attach((size_t)borrowed);assert(attached&&!dlm_native_last_error());editor_draw(attached,64,64,UINT64_C(0xabcdefFF));editor_input(attached,8,1,2,1);events=0;for(unsigned i=0;i<100;i++){uint64_t kind=editor_poll(attached);if(!kind)break;events|=1u<<(unsigned)kind;}assert(events&(1u<<8));
#ifdef _WIN32
 unsigned before=host_buttons;SendMessageW(borrowed,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(1,2));assert(host_buttons==before+1);
#endif
 editor_gpu(attached);if(dlm_native_last_error())fprintf(stderr,"attached GPU: %s\n",dlm_native_last_error());assert(!dlm_native_last_error());editor_draw(attached,64,64,UINT64_C(0xabcdefFF));assert(editor_pixel(attached,20,20)==UINT64_C(0xabcdefFF));preserve_gl((void*)context);
 editor_destroy(attached);assert(!dlm_native_last_error());
#ifdef _WIN32
 assert(IsWindow(borrowed));DestroyWindow(borrowed);
#else
 XWindowAttributes check;assert(XGetWindowAttributes(display,borrowed,&check));assert(check.your_event_mask&ButtonPressMask);XDestroyWindow(display,borrowed);
 // Attach a 32-bit visual too: it must use that visual and an owned GC.
 XVisualInfo argb;if(XMatchVisualInfo(display,screen,32,TrueColor,&argb)){Colormap cm=XCreateColormap(display,parent,argb.visual,AllocNone);XSetWindowAttributes at={0};at.colormap=cm;at.border_pixel=0;Window w=XCreateWindow(display,parent,300,0,64,64,0,32,InputOutput,argb.visual,CWColormap|CWBorderPixel,&at);XMapWindow(display,w);XSync(display,False);void *p=editor_attach((size_t)w);assert(p);editor_draw(p,64,64,UINT64_C(0x123456ff));XSync(display,False);XImage *im=XGetImage(display,w,20,20,1,1,AllPlanes,ZPixmap);assert(im&&XGetPixel(im,0,0)==0xff123456UL);XDestroyImage(im);editor_destroy(p);XDestroyWindow(display,w);XFreeColormap(display,cm);}
#endif
 destroy_on_other_thread=editor_destroy;error_on_other_thread=dlm_native_last_error;other_editor=a;
#ifdef _WIN32
 HANDLE thread=CreateThread(NULL,0,wrong_thread,NULL,0,NULL);assert(thread);WaitForSingleObject(thread,INFINITE);CloseHandle(thread);
#else
 pthread_t thread;assert(!pthread_create(&thread,NULL,wrong_thread,NULL));pthread_join(thread,NULL);
#endif
 assert(editor_handle(a)==(size_t)aw);assert(!dlm_native_runtime_shutdown());editor_destroy(a);assert(!dlm_native_last_error());editor_draw(b,96,80,UINT64_C(0xaabbccff));assert(editor_pixel(b,40,40)==UINT64_C(0xaabbccff));editor_destroy(b);assert(!dlm_native_last_error());preserve_gl((void*)context);assert(dlm_native_live_allocations()==0);assert(dlm_native_runtime_shutdown());assert(dlm_native_runtime_init());a=editor_create((size_t)parent,true);assert(a);editor_destroy(a);assert(dlm_native_runtime_shutdown());preserve_gl((void*)context);
#ifdef _WIN32
 assert(CLOSE(module));wglMakeCurrent(NULL,NULL);wglDeleteContext(context);ReleaseDC(parent,dc);DestroyWindow(parent);UnregisterClassW(wc.lpszClassName,wc.hInstance);
#else
 assert(!CLOSE(module));glXMakeCurrent(display,None,NULL);glXDestroyContext(display,context);XDestroyWindow(display,parent);XFreeColormap(display,colormap);XFree(visual);XCloseDisplay(display);
#endif
 fesetround(FE_TONEAREST);puts("embedded editor: CPU/GPU, input, attach, affinity, host GL/FPU, independent lifetimes OK");return 0;}
