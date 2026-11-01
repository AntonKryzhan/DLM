//! v0.128: native windows for the interpreter (stage G3 core). Linux: Xlib
//! loaded at run time (no build dependency); the native runtime (`dvw_*`)
//! does the same and also supports Win32. Events: `[kind, a, b, c]` with
//! 0 none, 1 close, 2 press (x, y, button), 3 release, 4 motion (x, y),
//! 5 key (code point; function keys 0x10000 + X11 keysym 0xFE00..0xFFFF, other keys
//! without text 0x110000 + keysym), 6 resize (w, h), 7 expose,
//! 8 wheel (x, y, 1 up / 2 down / 3 left / 4 right). v0.129: text of any script
//! through the X input method (XIM, `Xutf8LookupString`); keys without text
//! keep the keysym form.

pub type Event = [u128; 4];

fn over_white(c: u128) -> u32 {
    let a = (c & 0xFF) as u32;
    let ch = |s: u32| (((c >> s) & 0xFF) as u32 * a + 255 * (255 - a) + 127) / 255;
    (ch(24) << 16) | (ch(16) << 8) | ch(8)
}

#[cfg(target_os = "linux")]
mod imp {
    use super::Event;
    use std::ffi::{c_char, c_int, c_long, c_uint, c_ulong, c_void, CString};

    #[link(name = "dl")]
    extern "C" {
        fn dlopen(file: *const c_char, flag: c_int) -> *mut c_void;
        fn dlsym(handle: *mut c_void, name: *const c_char) -> *mut c_void;
    }
    #[repr(C)]
    struct PollFd {
        fd: c_int,
        events: i16,
        revents: i16,
    }
    extern "C" {
        fn poll(fds: *mut PollFd, n: c_ulong, timeout: c_int) -> c_int;
        fn setlocale(category: c_int, locale: *const c_char) -> *mut c_char;
    }
    const LC_CTYPE: c_int = 0;

    type P = *mut c_void;
    #[allow(non_snake_case)]
    struct X11 {
        XOpenDisplay: unsafe extern "C" fn(*const c_char) -> P,
        XDefaultScreen: unsafe extern "C" fn(P) -> c_int,
        XRootWindow: unsafe extern "C" fn(P, c_int) -> c_ulong,
        XBlackPixel: unsafe extern "C" fn(P, c_int) -> c_ulong,
        XWhitePixel: unsafe extern "C" fn(P, c_int) -> c_ulong,
        XCreateSimpleWindow: unsafe extern "C" fn(P, c_ulong, c_int, c_int, c_uint, c_uint, c_uint, c_ulong, c_ulong) -> c_ulong,
        XStoreName: unsafe extern "C" fn(P, c_ulong, *const c_char) -> c_int,
        XSelectInput: unsafe extern "C" fn(P, c_ulong, c_long) -> c_int,
        XMapWindow: unsafe extern "C" fn(P, c_ulong) -> c_int,
        XFlush: unsafe extern "C" fn(P) -> c_int,
        XPending: unsafe extern "C" fn(P) -> c_int,
        XNextEvent: unsafe extern "C" fn(P, P) -> c_int,
        XDefaultVisual: unsafe extern "C" fn(P, c_int) -> P,
        XDefaultDepth: unsafe extern "C" fn(P, c_int) -> c_int,
        XDefaultGC: unsafe extern "C" fn(P, c_int) -> P,
        XCreateImage: unsafe extern "C" fn(P, P, c_uint, c_int, c_int, *mut c_char, c_uint, c_uint, c_int, c_int) -> P,
        XPutImage: unsafe extern "C" fn(P, c_ulong, P, P, c_int, c_int, c_int, c_int, c_uint, c_uint) -> c_int,
        XFree: unsafe extern "C" fn(P) -> c_int,
        XDestroyWindow: unsafe extern "C" fn(P, c_ulong) -> c_int,
        XCloseDisplay: unsafe extern "C" fn(P) -> c_int,
        XInternAtom: unsafe extern "C" fn(P, *const c_char, c_int) -> c_ulong,
        XSetWMProtocols: unsafe extern "C" fn(P, c_ulong, *mut c_ulong, c_int) -> c_int,
        XConnectionNumber: unsafe extern "C" fn(P) -> c_int,
        XLookupString: unsafe extern "C" fn(P, *mut c_char, c_int, *mut c_ulong, P) -> c_int,
        im: Option<Im>,
        XChangeProperty: Option<unsafe extern "C" fn(P, c_ulong, c_ulong, c_ulong, c_int, c_int, *const u8, c_int) -> c_int>,
    }

    /// Optional input-method entry points (all present or none used).
    #[allow(non_snake_case)]
    struct Im {
        XSetLocaleModifiers: unsafe extern "C" fn(*const c_char) -> *mut c_char,
        XOpenIM: unsafe extern "C" fn(P, P, *mut c_char, *mut c_char) -> P,
        XCloseIM: unsafe extern "C" fn(P) -> c_int,
        XCreateIC: unsafe extern "C" fn(P, ...) -> P,
        XGetICValues: unsafe extern "C" fn(P, ...) -> *mut c_char,
        XSetICFocus: unsafe extern "C" fn(P),
        XDestroyIC: unsafe extern "C" fn(P),
        XFilterEvent: unsafe extern "C" fn(P, c_ulong) -> c_int,
        Xutf8LookupString: unsafe extern "C" fn(P, P, *mut c_char, c_int, *mut c_ulong, *mut c_int) -> c_int,
    }

    fn load() -> Option<&'static X11> {
        static LIB: std::sync::OnceLock<Option<X11>> = std::sync::OnceLock::new();
        LIB.get_or_init(|| unsafe {
            let mut h = dlopen(c"libX11.so.6".as_ptr(), 2);
            if h.is_null() {
                h = dlopen(c"libX11.so".as_ptr(), 2);
            }
            if h.is_null() {
                return None;
            }
            macro_rules! sym {
                ($n:ident) => {{
                    let name = CString::new(stringify!($n)).ok()?;
                    let p = dlsym(h, name.as_ptr());
                    if p.is_null() {
                        return None;
                    }
                    std::mem::transmute::<P, _>(p)
                }};
            }
            macro_rules! opt {
                ($n:ident) => {{
                    let name = CString::new(stringify!($n)).ok()?;
                    let p = dlsym(h, name.as_ptr());
                    if p.is_null() { None } else { Some(std::mem::transmute::<P, _>(p)) }
                }};
            }
            let im = (|| {
                Some(Im {
                    XSetLocaleModifiers: opt!(XSetLocaleModifiers)?, XOpenIM: opt!(XOpenIM)?, XCloseIM: opt!(XCloseIM)?, XCreateIC: opt!(XCreateIC)?,
                    XGetICValues: opt!(XGetICValues)?, XSetICFocus: opt!(XSetICFocus)?, XDestroyIC: opt!(XDestroyIC)?, XFilterEvent: opt!(XFilterEvent)?,
                    Xutf8LookupString: opt!(Xutf8LookupString)?,
                })
            })();
            Some(X11 {
                im,
                XChangeProperty: opt!(XChangeProperty),
                XOpenDisplay: sym!(XOpenDisplay), XDefaultScreen: sym!(XDefaultScreen), XRootWindow: sym!(XRootWindow),
                XBlackPixel: sym!(XBlackPixel), XWhitePixel: sym!(XWhitePixel), XCreateSimpleWindow: sym!(XCreateSimpleWindow),
                XStoreName: sym!(XStoreName), XSelectInput: sym!(XSelectInput), XMapWindow: sym!(XMapWindow), XFlush: sym!(XFlush),
                XPending: sym!(XPending), XNextEvent: sym!(XNextEvent), XDefaultVisual: sym!(XDefaultVisual), XDefaultDepth: sym!(XDefaultDepth),
                XDefaultGC: sym!(XDefaultGC), XCreateImage: sym!(XCreateImage), XPutImage: sym!(XPutImage), XFree: sym!(XFree),
                XDestroyWindow: sym!(XDestroyWindow), XCloseDisplay: sym!(XCloseDisplay), XInternAtom: sym!(XInternAtom),
                XSetWMProtocols: sym!(XSetWMProtocols), XConnectionNumber: sym!(XConnectionNumber), XLookupString: sym!(XLookupString),
            })
        })
        .as_ref()
    }

    pub struct Win {
        dpy: P,
        win: c_ulong,
        wm_delete: c_ulong,
        gc: P,
        im: P,
        ic: P,
        queue: std::collections::VecDeque<Event>,
        pub gl: Option<gl::GlWin>,
        pub w: u128,
        pub h: u128,
    }

    impl Win {
        pub fn is_gpu(&self) -> bool {
            self.gl.is_some()
        }
    }

    /// A UTF-8 locale for the input method: the user's, else C.UTF-8.
    fn utf8_locale() {
        static DONE: std::sync::Once = std::sync::Once::new();
        DONE.call_once(|| unsafe {
            let l = setlocale(LC_CTYPE, c"".as_ptr());
            let plain = l.is_null() || {
                let s = std::ffi::CStr::from_ptr(l).to_bytes();
                s == b"C" || s == b"POSIX"
            };
            if plain && setlocale(LC_CTYPE, c"C.UTF-8".as_ptr()).is_null() && setlocale(LC_CTYPE, c"C.utf8".as_ptr()).is_null() {
                setlocale(LC_CTYPE, c"en_US.UTF-8".as_ptr());
            }
        });
    }

    /// Opens the input method and context; returns the extra event mask it needs.
    unsafe fn open_ime(m: &Im, d: P, win: c_ulong) -> (P, P, c_long) {
        utf8_locale();
        (m.XSetLocaleModifiers)(c"".as_ptr());
        let mut im = (m.XOpenIM)(d, std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut());
        if im.is_null() {
            (m.XSetLocaleModifiers)(c"@im=none".as_ptr());
            im = (m.XOpenIM)(d, std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut());
        }
        if im.is_null() {
            return (std::ptr::null_mut(), std::ptr::null_mut(), 0);
        }
        let style: c_long = 0x0008 | 0x0400;
        let ic = (m.XCreateIC)(im, c"inputStyle".as_ptr(), style, c"clientWindow".as_ptr(), win, c"focusWindow".as_ptr(), win, std::ptr::null_mut::<c_void>());
        if ic.is_null() {
            (m.XCloseIM)(im);
            return (std::ptr::null_mut(), std::ptr::null_mut(), 0);
        }
        let mut mask: c_long = 0;
        (m.XGetICValues)(ic, c"filterEvents".as_ptr(), &mut mask as *mut c_long, std::ptr::null_mut::<c_void>());
        (m.XSetICFocus)(ic);
        (im, ic, mask)
    }

    // The display connection is used by one runtime thread at a time.
    unsafe impl Send for Win {}

    pub fn open(title: &str, w: u128, h: u128) -> Result<Win, String> {
        let x = load().ok_or("cannot load libX11")?;
        unsafe {
            let d = (x.XOpenDisplay)(std::ptr::null());
            if d.is_null() {
                return Err("cannot open display".into());
            }
            let scr = (x.XDefaultScreen)(d);
            let win = (x.XCreateSimpleWindow)(d, (x.XRootWindow)(d, scr), 0, 0, w as c_uint, h as c_uint, 0, (x.XBlackPixel)(d, scr), (x.XWhitePixel)(d, scr));
            let t = CString::new(title).unwrap_or_default();
            (x.XStoreName)(d, win, t.as_ptr());
            if let Some(cp) = x.XChangeProperty {
                cp(d, win, (x.XInternAtom)(d, c"_NET_WM_NAME".as_ptr(), 0), (x.XInternAtom)(d, c"UTF8_STRING".as_ptr(), 0), 8, 0, t.as_ptr() as *const u8, t.as_bytes().len() as c_int);
            }
            let (im, ic, imask) = match &x.im {
                Some(m) => open_ime(m, d, win),
                None => (std::ptr::null_mut(), std::ptr::null_mut(), 0),
            };
            (x.XSelectInput)(d, win, (1 << 0) | (1 << 2) | (1 << 3) | (1 << 6) | (1 << 15) | (1 << 17) | (1 << 21) | imask);
            let mut del = (x.XInternAtom)(d, c"WM_DELETE_WINDOW".as_ptr(), 0);
            (x.XSetWMProtocols)(d, win, &mut del, 1);
            (x.XMapWindow)(d, win);
            (x.XFlush)(d);
            Ok(Win { dpy: d, win, wm_delete: del, gc: (x.XDefaultGC)(d, scr), im, ic, queue: Default::default(), gl: None, w, h })
        }
    }

    pub fn present(win: &Win, img: &[u128], w: usize, h: usize) -> Result<u128, String> {
        let x = load().ok_or("cannot load libX11")?;
        unsafe {
            let scr = (x.XDefaultScreen)(win.dpy);
            let depth = (x.XDefaultDepth)(win.dpy, scr);
            if depth < 24 {
                return Err("unsupported display depth".into());
            }
            let mut data: Vec<u32> = img[2..2 + w * h].iter().map(|&c| super::over_white(c)).collect();
            let im = (x.XCreateImage)(win.dpy, (x.XDefaultVisual)(win.dpy, scr), depth as c_uint, 2, 0, data.as_mut_ptr() as *mut c_char, w as c_uint, h as c_uint, 32, 0);
            if im.is_null() {
                return Err("cannot create image".into());
            }
            (x.XPutImage)(win.dpy, win.win, win.gc, im, 0, 0, 0, 0, w as c_uint, h as c_uint);
            (x.XFlush)(win.dpy);
            *((im as *mut u8).add(16) as *mut *mut c_char) = std::ptr::null_mut();
            (x.XFree)(im);
        }
        Ok((w * h) as u128)
    }

    pub fn poll_event(win: &mut Win, ms: u128) -> Result<Event, String> {
        let x = load().ok_or("cannot load libX11")?;
        let mut ev: [i64; 24];
        unsafe {
            loop {
                if let Some(e) = win.queue.pop_front() {
                    return Ok(e);
                }
                if (x.XPending)(win.dpy) == 0 {
                    let mut p = PollFd { fd: (x.XConnectionNumber)(win.dpy), events: 1, revents: 0 };
                    let r = poll(&mut p, 1, ms.min(i32::MAX as u128) as c_int);
                    if r <= 0 || (x.XPending)(win.dpy) == 0 {
                        return Ok([0, 0, 0, 0]);
                    }
                }
                ev = [0; 24];
                (x.XNextEvent)(win.dpy, ev.as_mut_ptr() as P);
                let ime = x.im.as_ref().filter(|_| !win.ic.is_null());
                if let Some(m) = ime {
                    if (m.XFilterEvent)(ev.as_mut_ptr() as P, 0) != 0 {
                        continue;
                    }
                }
                let b = ev.as_ptr() as *const u8;
                let int_at = |o: usize| *(b.add(o) as *const i32);
                let nat_at = |o: usize| int_at(o).max(0) as u128;
                let button = *(b.add(84) as *const u32);
                if matches!(int_at(0), 4 | 5) && (4..=7).contains(&button) {
                    if int_at(0) == 5 {
                        continue;
                    }
                    return Ok([8, nat_at(64), nat_at(68), u128::from(button - 3)]);
                }
                if int_at(0) == 9 {
                    if let Some(m) = ime {
                        (m.XSetICFocus)(win.ic);
                    }
                }
                if let (2, Some(m)) = (int_at(0), ime) {
                    let mut tb = [0 as c_char; 256];
                    let (mut ks, mut st): (c_ulong, c_int) = (0, 0);
                    let n = (m.Xutf8LookupString)(win.ic, ev.as_mut_ptr() as P, tb.as_mut_ptr(), 256, &mut ks, &mut st);
                    if (st == 2 || st == 4) && n > 0 && n <= 256 {
                        let bytes: Vec<u8> = tb[..n as usize].iter().map(|&c| c as u8).collect();
                        let cps: Vec<u128> = String::from_utf8_lossy(&bytes).chars().map(|c| u128::from(c as u32)).collect();
                        if !cps.is_empty() && cps.iter().all(|&c| c >= 32 && c != 127) {
                            for &c in &cps[1..] {
                                if win.queue.len() < 64 {
                                    win.queue.push_back([5, c, 0, 0]);
                                }
                            }
                            return Ok([5, cps[0], 0, 0]);
                        }
                    }
                }
                match int_at(0) {
                    4 | 5 => return Ok([if int_at(0) == 4 { 2 } else { 3 }, nat_at(64), nat_at(68), u128::from(*(b.add(84) as *const u32))]),
                    6 => return Ok([4, nat_at(64), nat_at(68), 0]),
                    2 => {
                        let mut buf = [0 as c_char; 32];
                        let mut sym: c_ulong = 0;
                        let n = (x.XLookupString)(ev.as_mut_ptr() as P, buf.as_mut_ptr(), 32, &mut sym, std::ptr::null_mut());
                        let c0 = if n > 0 { buf[0] as u8 } else { 0 };
                        if n == 1 && c0 >= 32 && c0 != 127 {
                            return Ok([5, u128::from(c0), 0, 0]);
                        }
                        if (0x0100_0100..=0x0110_FFFF).contains(&sym) {
                            return Ok([5, (sym - 0x0100_0000) as u128, 0, 0]);
                        }
                        // function keys (keysym 0xFE00..0xFFFF) at 0x1FE00.., other keys without text past Unicode
                        return Ok([5, if (0xFE00..=0xFFFF).contains(&sym) { 0x10000 } else { 0x11_0000 } + sym as u128, 0, 0]);
                    }
                    12 => return Ok([7, 0, 0, 0]),
                    22 => {
                        let (w, h) = (int_at(56), int_at(60));
                        if w as u128 != win.w || h as u128 != win.h {
                            win.w = w.max(0) as u128;
                            win.h = h.max(0) as u128;
                            if let Some(g) = &win.gl {
                                gl::resize(win.dpy, g, win.w, win.h);
                            }
                            return Ok([6, win.w, win.h, 0]);
                        }
                    }
                    33 if *(b.add(56) as *const c_ulong) == win.wm_delete => return Ok([1, 0, 0, 0]),
                    _ => {}
                }
            }
        }
    }

    pub fn close(mut win: Win) {
        if let Some(g) = win.gl.take() {
            gl::close(win.dpy, g);
        }
        if let Some(x) = load() {
            unsafe {
                if let (Some(m), false) = (&x.im, win.ic.is_null()) {
                    (m.XDestroyIC)(win.ic);
                    (m.XCloseIM)(win.im);
                }
                (x.XDestroyWindow)(win.dpy, win.win);
                (x.XCloseDisplay)(win.dpy);
            }
        }
    }

    /// v0.131: OpenGL presentation (GLX, libGL loaded at run time) of `vector::gpu_items`,
    /// the same drawing as the native runtime (`dgl_*`): fans for convex contours,
    /// stencil-then-cover otherwise, gradients by texture combine, clips by scissor, MSAA.
    pub mod gl {
        use super::{dlopen, dlsym, P};
        use crate::vector::{GpuItem, GpuPaint};
        use std::ffi::{c_char, c_int, c_long, c_uint, c_ulong, c_void, CString};

        #[repr(C)]
        struct XVisualInfo {
            visual: P,
            visualid: c_ulong,
            screen: c_int,
            depth: c_int,
            class: c_int,
            red_mask: c_ulong,
            green_mask: c_ulong,
            blue_mask: c_ulong,
            colormap_size: c_int,
            bits_per_rgb: c_int,
        }
        #[repr(C)]
        #[derive(Default)]
        struct XSetWindowAttributes {
            background_pixmap: c_ulong,
            background_pixel: c_ulong,
            border_pixmap: c_ulong,
            border_pixel: c_ulong,
            bit_gravity: c_int,
            win_gravity: c_int,
            backing_store: c_int,
            backing_planes: c_ulong,
            backing_pixel: c_ulong,
            save_under: c_int,
            event_mask: c_long,
            do_not_propagate_mask: c_long,
            override_redirect: c_int,
            colormap: c_ulong,
            cursor: c_ulong,
        }

        #[allow(non_snake_case)]
        struct Gl {
            glXChooseVisual: unsafe extern "C" fn(P, c_int, *mut c_int) -> *mut XVisualInfo,
            glXCreateContext: unsafe extern "C" fn(P, *mut XVisualInfo, P, c_int) -> P,
            glXMakeCurrent: unsafe extern "C" fn(P, c_ulong, P) -> c_int,
            glXSwapBuffers: unsafe extern "C" fn(P, c_ulong),
            glXDestroyContext: unsafe extern "C" fn(P, P),
            XCreateColormap: unsafe extern "C" fn(P, c_ulong, P, c_int) -> c_ulong,
            XFreeColormap: unsafe extern "C" fn(P, c_ulong) -> c_int,
            XCreateWindow: unsafe extern "C" fn(P, c_ulong, c_int, c_int, c_uint, c_uint, c_uint, c_int, c_uint, P, c_ulong, *mut XSetWindowAttributes) -> c_ulong,
            XResizeWindow: unsafe extern "C" fn(P, c_ulong, c_uint, c_uint) -> c_int,
            XMapWindow: unsafe extern "C" fn(P, c_ulong) -> c_int,
            XDestroyWindow: unsafe extern "C" fn(P, c_ulong) -> c_int,
            XRootWindow: unsafe extern "C" fn(P, c_int) -> c_ulong,
            XDefaultScreen: unsafe extern "C" fn(P) -> c_int,
            XFree: unsafe extern "C" fn(P) -> c_int,
            glViewport: unsafe extern "C" fn(c_int, c_int, c_int, c_int),
            glMatrixMode: unsafe extern "C" fn(c_uint),
            glLoadIdentity: unsafe extern "C" fn(),
            glOrtho: unsafe extern "C" fn(f64, f64, f64, f64, f64, f64),
            glClearColor: unsafe extern "C" fn(f32, f32, f32, f32),
            glClearStencil: unsafe extern "C" fn(c_int),
            glClear: unsafe extern "C" fn(c_uint),
            glEnable: unsafe extern "C" fn(c_uint),
            glDisable: unsafe extern "C" fn(c_uint),
            glBlendFunc: unsafe extern "C" fn(c_uint, c_uint),
            glStencilFunc: unsafe extern "C" fn(c_uint, c_int, c_uint),
            glStencilOp: unsafe extern "C" fn(c_uint, c_uint, c_uint),
            glStencilMask: unsafe extern "C" fn(c_uint),
            glColorMask: unsafe extern "C" fn(u8, u8, u8, u8),
            glCullFace: unsafe extern "C" fn(c_uint),
            glBegin: unsafe extern "C" fn(c_uint),
            glEnd: unsafe extern "C" fn(),
            glVertex2f: unsafe extern "C" fn(f32, f32),
            glColor4ub: unsafe extern "C" fn(u8, u8, u8, u8),
            glTexCoord2f: unsafe extern "C" fn(f32, f32),
            glGenTextures: unsafe extern "C" fn(c_int, *mut c_uint),
            glDeleteTextures: unsafe extern "C" fn(c_int, *const c_uint),
            glBindTexture: unsafe extern "C" fn(c_uint, c_uint),
            glTexImage2D: unsafe extern "C" fn(c_uint, c_int, c_int, c_int, c_int, c_int, c_uint, c_uint, *const c_void),
            glTexParameteri: unsafe extern "C" fn(c_uint, c_uint, c_int),
            glTexEnvi: unsafe extern "C" fn(c_uint, c_uint, c_int),
            glTexEnvfv: unsafe extern "C" fn(c_uint, c_uint, *const f32),
            glScissor: unsafe extern "C" fn(c_int, c_int, c_int, c_int),
            glReadBuffer: unsafe extern "C" fn(c_uint),
            glReadPixels: unsafe extern "C" fn(c_int, c_int, c_int, c_int, c_uint, c_uint, *mut c_void),
            glPixelStorei: unsafe extern "C" fn(c_uint, c_int),
            glFinish: unsafe extern "C" fn(),
            glGetString: unsafe extern "C" fn(c_uint) -> *const c_char,
            glGetIntegerv: unsafe extern "C" fn(c_uint, *mut c_int),
            glGetError: unsafe extern "C" fn() -> c_uint,
            fbo: Option<Fbo>,
        }

        /// GL 3.0 framebuffer objects: MSAA without a multisampled window.
        #[allow(non_snake_case)]
        struct Fbo {
            glGenFramebuffers: unsafe extern "C" fn(c_int, *mut c_uint),
            glDeleteFramebuffers: unsafe extern "C" fn(c_int, *const c_uint),
            glBindFramebuffer: unsafe extern "C" fn(c_uint, c_uint),
            glGenRenderbuffers: unsafe extern "C" fn(c_int, *mut c_uint),
            glDeleteRenderbuffers: unsafe extern "C" fn(c_int, *const c_uint),
            glBindRenderbuffer: unsafe extern "C" fn(c_uint, c_uint),
            glRenderbufferStorageMultisample: unsafe extern "C" fn(c_uint, c_int, c_uint, c_int, c_int),
            glFramebufferRenderbuffer: unsafe extern "C" fn(c_uint, c_uint, c_uint, c_uint),
            glCheckFramebufferStatus: unsafe extern "C" fn(c_uint) -> c_uint,
            glBlitFramebuffer: unsafe extern "C" fn(c_int, c_int, c_int, c_int, c_int, c_int, c_int, c_int, c_uint, c_uint),
        }

        fn load() -> Option<&'static Gl> {
            static LIB: std::sync::OnceLock<Option<Gl>> = std::sync::OnceLock::new();
            LIB.get_or_init(|| unsafe {
                let mut g = dlopen(c"libGL.so.1".as_ptr(), 2);
                if g.is_null() {
                    g = dlopen(c"libGL.so".as_ptr(), 2);
                }
                let mut x = dlopen(c"libX11.so.6".as_ptr(), 2);
                if x.is_null() {
                    x = dlopen(c"libX11.so".as_ptr(), 2);
                }
                if g.is_null() || x.is_null() {
                    return None;
                }
                macro_rules! sym {
                    ($h:expr, $n:ident) => {{
                        let name = CString::new(stringify!($n)).ok()?;
                        let p = dlsym($h, name.as_ptr());
                        if p.is_null() {
                            return None;
                        }
                        std::mem::transmute::<P, _>(p)
                    }};
                }
                let proc_addr: unsafe extern "C" fn(*const u8) -> P = sym!(g, glXGetProcAddressARB);
                macro_rules! ext {
                    ($n:ident) => {{
                        let name = CString::new(stringify!($n)).ok()?;
                        let p = proc_addr(name.as_ptr() as *const u8);
                        if p.is_null() { None } else { Some(std::mem::transmute::<P, _>(p)) }
                    }};
                }
                let fbo = (|| {
                    Some(Fbo {
                        glGenFramebuffers: ext!(glGenFramebuffers)?, glDeleteFramebuffers: ext!(glDeleteFramebuffers)?, glBindFramebuffer: ext!(glBindFramebuffer)?,
                        glGenRenderbuffers: ext!(glGenRenderbuffers)?, glDeleteRenderbuffers: ext!(glDeleteRenderbuffers)?, glBindRenderbuffer: ext!(glBindRenderbuffer)?,
                        glRenderbufferStorageMultisample: ext!(glRenderbufferStorageMultisample)?, glFramebufferRenderbuffer: ext!(glFramebufferRenderbuffer)?,
                        glCheckFramebufferStatus: ext!(glCheckFramebufferStatus)?, glBlitFramebuffer: ext!(glBlitFramebuffer)?,
                    })
                })();
                Some(Gl {
                    fbo,
                    glGetIntegerv: sym!(g, glGetIntegerv), glGetError: sym!(g, glGetError),
                    glXChooseVisual: sym!(g, glXChooseVisual), glXCreateContext: sym!(g, glXCreateContext), glXMakeCurrent: sym!(g, glXMakeCurrent),
                    glXSwapBuffers: sym!(g, glXSwapBuffers), glXDestroyContext: sym!(g, glXDestroyContext),
                    XCreateColormap: sym!(x, XCreateColormap), XFreeColormap: sym!(x, XFreeColormap), XCreateWindow: sym!(x, XCreateWindow),
                    XResizeWindow: sym!(x, XResizeWindow), XMapWindow: sym!(x, XMapWindow), XDestroyWindow: sym!(x, XDestroyWindow),
                    XRootWindow: sym!(x, XRootWindow), XDefaultScreen: sym!(x, XDefaultScreen), XFree: sym!(x, XFree),
                    glViewport: sym!(g, glViewport), glMatrixMode: sym!(g, glMatrixMode), glLoadIdentity: sym!(g, glLoadIdentity), glOrtho: sym!(g, glOrtho),
                    glClearColor: sym!(g, glClearColor), glClearStencil: sym!(g, glClearStencil), glClear: sym!(g, glClear), glEnable: sym!(g, glEnable),
                    glDisable: sym!(g, glDisable), glBlendFunc: sym!(g, glBlendFunc), glStencilFunc: sym!(g, glStencilFunc), glStencilOp: sym!(g, glStencilOp),
                    glStencilMask: sym!(g, glStencilMask), glColorMask: sym!(g, glColorMask), glCullFace: sym!(g, glCullFace), glBegin: sym!(g, glBegin),
                    glEnd: sym!(g, glEnd), glVertex2f: sym!(g, glVertex2f), glColor4ub: sym!(g, glColor4ub), glTexCoord2f: sym!(g, glTexCoord2f),
                    glGenTextures: sym!(g, glGenTextures), glDeleteTextures: sym!(g, glDeleteTextures), glBindTexture: sym!(g, glBindTexture),
                    glTexImage2D: sym!(g, glTexImage2D), glTexParameteri: sym!(g, glTexParameteri), glTexEnvi: sym!(g, glTexEnvi), glTexEnvfv: sym!(g, glTexEnvfv),
                    glScissor: sym!(g, glScissor), glReadBuffer: sym!(g, glReadBuffer), glReadPixels: sym!(g, glReadPixels), glPixelStorei: sym!(g, glPixelStorei),
                    glFinish: sym!(g, glFinish), glGetString: sym!(g, glGetString),
                })
            })
            .as_ref()
        }

        /// A GL child window covering the parent (input still goes to the parent) and its context.
        pub struct GlWin {
            child: c_ulong,
            cmap: c_ulong,
            ctx: P,
            ramp: c_uint,
            radial: c_uint,
            items: Vec<GpuItem>,
            vw: i64,
            vh: i64,
            fbo: c_uint,
            rb: [c_uint; 2],
            fbo_size: (i64, i64),
            samples: c_int,
            pub info: String,
        }

        const TEX2D: c_uint = 0x0DE1;

        unsafe fn textures(g: &Gl) -> (c_uint, c_uint) {
            let mut ramp = [0u8; 256 * 4];
            for (i, p) in ramp.chunks_mut(4).enumerate() {
                p.copy_from_slice(&[255, 255, 255, i as u8]);
            }
            let mut rad = vec![0u8; 128 * 128 * 4];
            for y in 0..128 {
                for x in 0..128 {
                    let (dx, dy) = ((x as f64 + 0.5 - 64.0) / 64.0, (y as f64 + 0.5 - 64.0) / 64.0);
                    let d = (dx * dx + dy * dy).sqrt();
                    let a = if d >= 1.0 { 255 } else { (d * 255.0 + 0.5) as u8 };
                    rad[4 * (y * 128 + x)..4 * (y * 128 + x) + 4].copy_from_slice(&[255, 255, 255, a]);
                }
            }
            let mut t = [0 as c_uint; 2];
            (g.glGenTextures)(2, t.as_mut_ptr());
            for (k, &tex) in t.iter().enumerate() {
                (g.glBindTexture)(TEX2D, tex);
                (g.glTexParameteri)(TEX2D, 0x2801, 0x2601);
                (g.glTexParameteri)(TEX2D, 0x2800, 0x2601);
                (g.glTexParameteri)(TEX2D, 0x2802, 0x812F);
                (g.glTexParameteri)(TEX2D, 0x2803, 0x812F);
                if k == 0 {
                    (g.glTexImage2D)(TEX2D, 0, 0x1908, 256, 1, 0, 0x1908, 0x1401, ramp.as_ptr() as *const c_void);
                } else {
                    (g.glTexImage2D)(TEX2D, 0, 0x1908, 128, 128, 0, 0x1908, 0x1401, rad.as_ptr() as *const c_void);
                }
            }
            (t[0], t[1])
        }

        pub fn open(dpy: P, parent: c_ulong, w: u128, h: u128) -> Result<GlWin, String> {
            let g = load().ok_or("cannot load libGL")?;
            unsafe {
                let mut ms = [4, 5, 8, 8, 9, 8, 10, 8, 13, 8, 100000, 1, 100001, 4, 0];
                let mut plain = [4, 5, 8, 8, 9, 8, 10, 8, 13, 8, 0];
                let scr = (g.XDefaultScreen)(dpy);
                let mut vi = (g.glXChooseVisual)(dpy, scr, ms.as_mut_ptr());
                if vi.is_null() {
                    vi = (g.glXChooseVisual)(dpy, scr, plain.as_mut_ptr());
                }
                if vi.is_null() {
                    return Err("no OpenGL visual with a stencil buffer".into());
                }
                let cmap = (g.XCreateColormap)(dpy, (g.XRootWindow)(dpy, (*vi).screen), (*vi).visual, 0);
                let mut a = XSetWindowAttributes { colormap: cmap, ..Default::default() };
                let child = (g.XCreateWindow)(dpy, parent, 0, 0, w.max(1) as c_uint, h.max(1) as c_uint, 0, (*vi).depth, 1, (*vi).visual, (1 << 0) | (1 << 3) | (1 << 11) | (1 << 13), &mut a);
                let ctx = (g.glXCreateContext)(dpy, vi, std::ptr::null_mut(), 1);
                (g.XFree)(vi as P);
                if child == 0 || ctx.is_null() {
                    if child != 0 {
                        (g.XDestroyWindow)(dpy, child);
                    }
                    (g.XFreeColormap)(dpy, cmap);
                    return Err("cannot create an OpenGL context".into());
                }
                (g.XMapWindow)(dpy, child);
                if (g.glXMakeCurrent)(dpy, child, ctx) == 0 {
                    (g.glXDestroyContext)(dpy, ctx);
                    (g.XDestroyWindow)(dpy, child);
                    (g.XFreeColormap)(dpy, cmap);
                    return Err("cannot make the OpenGL context current".into());
                }
                let text = |e: c_uint| {
                    let p = (g.glGetString)(e);
                    if p.is_null() { "?".to_string() } else { std::ffi::CStr::from_ptr(p).to_string_lossy().into_owned() }
                };
                let info = format!("OpenGL {}; {}; {}", text(0x1F02), text(0x1F01), text(0x1F00));
                let (ramp, radial) = textures(g);
                Ok(GlWin { child, cmap, ctx, ramp, radial, items: Vec::new(), vw: 0, vh: 0, fbo: 0, rb: [0, 0], fbo_size: (0, 0), samples: 0, info })
            }
        }

        pub fn resize(dpy: P, w: &GlWin, width: u128, height: u128) {
            if let Some(g) = load() {
                unsafe {
                    (g.XResizeWindow)(dpy, w.child, width.max(1) as c_uint, height.max(1) as c_uint);
                }
            }
        }

        pub fn close(dpy: P, w: GlWin) {
            if let Some(g) = load() {
                unsafe {
                    (g.glXMakeCurrent)(dpy, w.child, w.ctx);
                    let t = [w.ramp, w.radial];
                    (g.glDeleteTextures)(2, t.as_ptr());
                    if let (Some(f), true) = (&g.fbo, w.fbo != 0) {
                        (f.glDeleteRenderbuffers)(2, w.rb.as_ptr());
                        (f.glDeleteFramebuffers)(1, &w.fbo);
                    }
                    (g.glXMakeCurrent)(dpy, 0, std::ptr::null_mut());
                    (g.glXDestroyContext)(dpy, w.ctx);
                    (g.XDestroyWindow)(dpy, w.child);
                    (g.XFreeColormap)(dpy, w.cmap);
                }
            }
        }

        fn rgba(c: u32) -> [u8; 4] {
            c.to_be_bytes()
        }

        unsafe fn tc(g: &Gl, p: &GpuPaint, x: f64, y: f64) {
            match *p {
                GpuPaint::Solid(_) => {}
                GpuPaint::Linear { x0, y0, x1, y1, .. } => {
                    let (dx, dy) = (x1 - x0, y1 - y0);
                    let dd = dx * dx + dy * dy;
                    let t = if dd > 0.0 { ((x - x0) * dx + (y - y0) * dy) / dd } else { 0.0 };
                    (g.glTexCoord2f)(((0.5 + t * 255.0) / 256.0) as f32, 0.5);
                }
                GpuPaint::Radial { cx, cy, r, .. } => {
                    let r = if r > 0.0 { r } else { 1e-9 };
                    (g.glTexCoord2f)((0.5 + (x - cx) / (2.0 * r)) as f32, (0.5 + (y - cy) / (2.0 * r)) as f32);
                }
            }
        }

        unsafe fn vertex(g: &Gl, p: Option<&GpuPaint>, x: f64, y: f64) {
            if let Some(p) = p {
                tc(g, p, x, y);
            }
            (g.glVertex2f)(x as f32, y as f32);
        }

        unsafe fn paint_begin(g: &Gl, w: &GlWin, p: &GpuPaint) {
            let (c0, c1, tex) = match *p {
                GpuPaint::Solid(c) => {
                    (g.glDisable)(TEX2D);
                    let [r, gr, b, a] = rgba(c);
                    (g.glColor4ub)(r, gr, b, a);
                    return;
                }
                GpuPaint::Radial { r, c1, .. } if r <= 0.0 => {
                    (g.glDisable)(TEX2D);
                    let [rr, gg, b, a] = rgba(c1);
                    (g.glColor4ub)(rr, gg, b, a);
                    return;
                }
                GpuPaint::Linear { c0, c1, .. } => (c0, c1, w.ramp),
                GpuPaint::Radial { c0, c1, .. } => (c0, c1, w.radial),
            };
            let e = rgba(c1);
            let env = [e[0] as f32 / 255.0, e[1] as f32 / 255.0, e[2] as f32 / 255.0, e[3] as f32 / 255.0];
            (g.glEnable)(TEX2D);
            (g.glBindTexture)(TEX2D, tex);
            for (k, v) in [(0x2200, 0x8570), (0x8571, 0x8575), (0x8572, 0x8575), (0x8580, 0x8576), (0x8590, 0x0300), (0x8581, 0x8577), (0x8591, 0x0300),
                (0x8582, 0x1702), (0x8592, 0x0302), (0x8588, 0x8576), (0x8598, 0x0302), (0x8589, 0x8577), (0x8599, 0x0302), (0x858A, 0x1702), (0x859A, 0x0302)]
            {
                (g.glTexEnvi)(0x2300, k, v);
            }
            (g.glTexEnvfv)(0x2300, 0x2201, env.as_ptr());
            let [r, gr, b, a] = rgba(c0);
            (g.glColor4ub)(r, gr, b, a);
        }

        unsafe fn scissor(g: &Gl, vy: i64, vh: i64, c: (i64, i64, i64, i64)) {
            let (w, h) = ((c.2 - c.0).max(0), (c.3 - c.1).max(0));
            (g.glScissor)(c.0 as c_int, (vy + vh - c.1 - h) as c_int, w as c_int, h as c_int);
        }

        unsafe fn fan(g: &Gl, poly: &[(f64, f64)], p: Option<&GpuPaint>) {
            (g.glBegin)(0x0006);
            for &(x, y) in poly {
                vertex(g, p, x, y);
            }
            (g.glEnd)();
        }

        /// MSAA render target of the viewport size, as the native runtime (`dgl_target`).
        unsafe fn target(g: &Gl, w: &mut GlWin) {
            let Some(f) = &g.fbo else { return };
            if w.samples < 0 || (w.fbo != 0 && w.fbo_size == (w.vw, w.vh)) {
                return;
            }
            if w.fbo == 0 {
                let (mut sb, mut maxs) = (0, 0);
                (g.glGetIntegerv)(0x80A8, &mut sb);
                (g.glGetIntegerv)(0x8D57, &mut maxs);
                (g.glGetError)();
                if sb > 0 || maxs < 2 {
                    w.samples = -1;
                    return;
                }
                w.samples = maxs.min(8);
                (f.glGenFramebuffers)(1, &mut w.fbo);
                (f.glGenRenderbuffers)(2, w.rb.as_mut_ptr());
            }
            (f.glBindRenderbuffer)(0x8D41, w.rb[0]);
            (f.glRenderbufferStorageMultisample)(0x8D41, w.samples, 0x8058, w.vw as c_int, w.vh as c_int);
            (f.glBindRenderbuffer)(0x8D41, w.rb[1]);
            (f.glRenderbufferStorageMultisample)(0x8D41, w.samples, 0x88F0, w.vw as c_int, w.vh as c_int);
            (f.glBindFramebuffer)(0x8D40, w.fbo);
            (f.glFramebufferRenderbuffer)(0x8D40, 0x8CE0, 0x8D41, w.rb[0]);
            (f.glFramebufferRenderbuffer)(0x8D40, 0x821A, 0x8D41, w.rb[1]);
            let complete = (f.glCheckFramebufferStatus)(0x8D40) == 0x8CD5;
            (f.glBindFramebuffer)(0x8D40, 0);
            if !complete {
                (f.glDeleteRenderbuffers)(2, w.rb.as_ptr());
                (f.glDeleteFramebuffers)(1, &w.fbo);
                w.fbo = 0;
                w.samples = -1;
                return;
            }
            w.fbo_size = (w.vw, w.vh);
        }

        /// Draws the items; returns the window row of the viewport bottom (for reading back).
        unsafe fn draw(g: &Gl, w: &mut GlWin, win_w: i64, win_h: i64) -> i64 {
            let (vw, vh) = (w.vw, w.vh);
            target(g, w);
            (g.glDisable)(0x0C11);
            (g.glViewport)(0, 0, win_w.max(1) as c_int, win_h.max(1) as c_int);
            (g.glClearColor)(1.0, 1.0, 1.0, 1.0);
            (g.glClearStencil)(0);
            (g.glStencilMask)(0xFF);
            (g.glClear)(0x4000 | 0x0400);
            let window_vy = win_h.max(1) - vh;
            let mut vy = window_vy;
            if let (Some(f), true) = (&g.fbo, w.fbo != 0) {
                (f.glBindFramebuffer)(0x8D40, w.fbo);
                (g.glViewport)(0, 0, vw as c_int, vh as c_int);
                (g.glClear)(0x4000 | 0x0400);
                vy = 0;
            }
            let w = &*w;
            (g.glViewport)(0, vy as c_int, vw as c_int, vh as c_int);
            (g.glMatrixMode)(0x1701);
            (g.glLoadIdentity)();
            (g.glOrtho)(0.0, vw as f64, vh as f64, 0.0, -1.0, 1.0);
            (g.glMatrixMode)(0x1700);
            (g.glLoadIdentity)();
            (g.glEnable)(0x0BE2);
            (g.glBlendFunc)(0x0302, 0x0303);
            (g.glEnable)(0x809D);
            (g.glEnable)(0x0C11);
            for item in &w.items {
                match item {
                    GpuItem::Shape { polys, convex, evenodd, paint, clip } => {
                        if clip.2 <= clip.0 || clip.3 <= clip.1 || polys.is_empty() {
                            continue;
                        }
                        scissor(g, vy, vh, *clip);
                        if *convex {
                            paint_begin(g, w, paint);
                            fan(g, &polys[0], Some(paint));
                            continue;
                        }
                        let (mut x0, mut y0, mut x1, mut y1) = (f64::MAX, f64::MAX, f64::MIN, f64::MIN);
                        for &(x, y) in polys.iter().flatten() {
                            x0 = x0.min(x);
                            y0 = y0.min(y);
                            x1 = x1.max(x);
                            y1 = y1.max(y);
                        }
                        if x0 > x1 {
                            continue;
                        }
                        (g.glDisable)(TEX2D);
                        (g.glColorMask)(0, 0, 0, 0);
                        (g.glEnable)(0x0B90);
                        (g.glStencilFunc)(0x0207, 0, 0xFF);
                        if *evenodd {
                            (g.glStencilMask)(0x01);
                            (g.glStencilOp)(0x1E00, 0x1E00, 0x150A);
                            for p in polys.iter().filter(|p| p.len() >= 3) {
                                fan(g, p, None);
                            }
                        } else {
                            (g.glStencilMask)(0xFF);
                            (g.glEnable)(0x0B44);
                            (g.glCullFace)(0x0405);
                            (g.glStencilOp)(0x1E00, 0x1E00, 0x8507);
                            for p in polys.iter().filter(|p| p.len() >= 3) {
                                fan(g, p, None);
                            }
                            (g.glCullFace)(0x0404);
                            (g.glStencilOp)(0x1E00, 0x1E00, 0x8508);
                            for p in polys.iter().filter(|p| p.len() >= 3) {
                                fan(g, p, None);
                            }
                            (g.glDisable)(0x0B44);
                        }
                        (g.glColorMask)(1, 1, 1, 1);
                        (g.glStencilMask)(0xFF);
                        (g.glStencilFunc)(0x0205, 0, if *evenodd { 0x01 } else { 0xFF });
                        (g.glStencilOp)(0, 0, 0);
                        paint_begin(g, w, paint);
                        let (x1, y1) = (x1 + 1.0 / 256.0, y1 + 1.0 / 256.0);
                        (g.glBegin)(0x0007);
                        vertex(g, Some(paint), x0, y0);
                        vertex(g, Some(paint), x1, y0);
                        vertex(g, Some(paint), x1, y1);
                        vertex(g, Some(paint), x0, y1);
                        (g.glEnd)();
                        (g.glDisable)(0x0B90);
                    }
                    GpuItem::Image { x0, y0, x1, y1, iw, ih, px, clip } => {
                        if *iw == 0 || *ih == 0 || x1 <= x0 || y1 <= y0 || clip.2 <= clip.0 || clip.3 <= clip.1 || *iw > 16384 || *ih > 16384 {
                            continue;
                        }
                        let bytes: Vec<u8> = px.iter().flat_map(|c| c.to_be_bytes()).collect();
                        let mut t: c_uint = 0;
                        (g.glGenTextures)(1, &mut t);
                        (g.glBindTexture)(TEX2D, t);
                        for (k, v) in [(0x2801, 0x2600), (0x2800, 0x2600), (0x2802, 0x812F), (0x2803, 0x812F)] {
                            (g.glTexParameteri)(TEX2D, k, v);
                        }
                        (g.glPixelStorei)(0x0CF5, 1);
                        (g.glTexImage2D)(TEX2D, 0, 0x1908, *iw as c_int, *ih as c_int, 0, 0x1908, 0x1401, bytes.as_ptr() as *const c_void);
                        scissor(g, vy, vh, *clip);
                        (g.glEnable)(TEX2D);
                        (g.glTexEnvi)(0x2300, 0x2200, 0x2100);
                        (g.glColor4ub)(255, 255, 255, 255);
                        (g.glBegin)(0x0007);
                        for (s, t2, x, y) in [(0.0, 0.0, x0, y0), (1.0, 0.0, x1, y0), (1.0, 1.0, x1, y1), (0.0, 1.0, x0, y1)] {
                            (g.glTexCoord2f)(s, t2);
                            (g.glVertex2f)(*x as f32, *y as f32);
                        }
                        (g.glEnd)();
                        (g.glDisable)(TEX2D);
                        (g.glDeleteTextures)(1, &t);
                    }
                }
            }
            (g.glDisable)(0x0C11);
            if let (Some(f), true) = (&g.fbo, w.fbo != 0) {
                (f.glBindFramebuffer)(0x8CA8, w.fbo);
                (f.glBindFramebuffer)(0x8CA9, 0);
                (f.glBlitFramebuffer)(0, 0, vw as c_int, vh as c_int, 0, window_vy as c_int, vw as c_int, (window_vy + vh) as c_int, 0x4000, 0x2600);
                (f.glBindFramebuffer)(0x8D40, 0);
            }
            window_vy
        }

        /// Draws the items and shows them (swap buffers).
        pub fn present(dpy: P, w: &mut GlWin, items: Vec<GpuItem>, vw: u128, vh: u128, win_w: u128, win_h: u128) -> Result<u128, String> {
            let g = load().ok_or("cannot load libGL")?;
            w.items = items;
            w.vw = vw as i64;
            w.vh = vh as i64;
            unsafe {
                if (g.glXMakeCurrent)(dpy, w.child, w.ctx) == 0 {
                    return Err("cannot make the OpenGL context current".into());
                }
                draw(g, w, win_w as i64, win_h as i64);
                (g.glXSwapBuffers)(dpy, w.child);
            }
            Ok(1)
        }

        /// The last presented frame, drawn again into the back buffer and read back.
        pub fn read(dpy: P, w: &mut GlWin, win_w: u128, win_h: u128) -> Result<Vec<u128>, String> {
            let g = load().ok_or("cannot load libGL")?;
            if w.vw == 0 || w.vh == 0 {
                return Err("win_read needs a GPU window with a presented scene".into());
            }
            let (vw, vh) = (w.vw as usize, w.vh as usize);
            let mut px = vec![0u8; vw * vh * 4];
            unsafe {
                if (g.glXMakeCurrent)(dpy, w.child, w.ctx) == 0 {
                    return Err("cannot make the OpenGL context current".into());
                }
                let vy = draw(g, w, win_w as i64, win_h as i64) as c_int;
                (g.glFinish)();
                (g.glReadBuffer)(0x0405);
                (g.glPixelStorei)(0x0D05, 1);
                (g.glReadPixels)(0, vy, vw as c_int, vh as c_int, 0x1908, 0x1401, px.as_mut_ptr() as *mut c_void);
            }
            let mut out = Vec::with_capacity(vw * vh + 2);
            out.push(vw as u128);
            out.push(vh as u128);
            for y in 0..vh {
                for x in 0..vw {
                    let p = &px[4 * ((vh - 1 - y) * vw + x)..];
                    out.push((u128::from(p[0]) << 24) | (u128::from(p[1]) << 16) | (u128::from(p[2]) << 8) | 255);
                }
            }
            Ok(out)
        }
    }

    pub fn gpu(win: &mut Win) -> Result<String, String> {
        if win.gl.is_none() {
            win.gl = Some(gl::open(win.dpy, win.win, win.w, win.h)?);
        }
        Ok(win.gl.as_ref().map(|g| g.info.clone()).unwrap_or_default())
    }

    pub fn present_gpu(win: &mut Win, items: Vec<crate::vector::GpuItem>, vw: u128, vh: u128) -> Result<u128, String> {
        let (dpy, ww, wh) = (win.dpy, win.w, win.h);
        match win.gl.as_mut() {
            Some(g) => gl::present(dpy, g, items, vw, vh, ww, wh),
            None => Err("not a GPU window".into()),
        }
    }

    pub fn read_gpu(win: &mut Win) -> Result<Vec<u128>, String> {
        let (dpy, ww, wh) = (win.dpy, win.w, win.h);
        match win.gl.as_mut() {
            Some(g) => gl::read(dpy, g, ww, wh),
            None => Err("win_read needs a GPU window with a presented scene".into()),
        }
    }
}

#[cfg(not(target_os = "linux"))]
mod imp {
    use super::Event;
    pub struct Win {
        pub w: u128,
        pub h: u128,
    }
    pub fn open(_title: &str, _w: u128, _h: u128) -> Result<Win, String> {
        Err("native windows in `dlm run` need Linux/X11; build the program with `dlm build`".into())
    }
    pub fn present(_win: &Win, _img: &[u128], _w: usize, _h: usize) -> Result<u128, String> {
        Err("no window".into())
    }
    pub fn poll_event(_win: &mut Win, _ms: u128) -> Result<Event, String> {
        Err("no window".into())
    }
    pub fn close(_win: Win) {}
    pub fn gpu(_win: &mut Win) -> Result<String, String> {
        Err("no window".into())
    }
    pub fn present_gpu(_win: &mut Win, _items: Vec<crate::vector::GpuItem>, _vw: u128, _vh: u128) -> Result<u128, String> {
        Err("no window".into())
    }
    pub fn read_gpu(_win: &mut Win) -> Result<Vec<u128>, String> {
        Err("no window".into())
    }
    impl Win {
        pub fn is_gpu(&self) -> bool {
            false
        }
    }
}

/// Up to 64 windows, ids 1..=64 (the lowest free slot), as in the native runtime.
#[derive(Default)]
pub struct WindowTable {
    slots: Vec<Option<imp::Win>>,
}

impl WindowTable {
    /// v0.131: switches the window to OpenGL presentation; the driver's version and renderer.
    pub fn gpu(&mut self, id: u128) -> Result<String, String> {
        imp::gpu(self.get(id)?)
    }

    pub fn is_gpu(&mut self, id: u128) -> Result<bool, String> {
        Ok(self.get(id)?.is_gpu())
    }

    pub fn present_gpu(&mut self, id: u128, items: Vec<crate::vector::GpuItem>, w: u128, h: u128) -> Result<u128, String> {
        imp::present_gpu(self.get(id)?, items, w, h)
    }

    pub fn read(&mut self, id: u128) -> Result<Vec<u128>, String> {
        imp::read_gpu(self.get(id)?)
    }

    pub fn open(&mut self, title: &str, w: u128, h: u128) -> Result<u128, String> {
        if w == 0 || h == 0 || w > 16384 || h > 16384 {
            return Err(format!("invalid window size {w}x{h}"));
        }
        if self.slots.is_empty() {
            self.slots.resize_with(64, || None);
        }
        let slot = self.slots.iter().position(Option::is_none).ok_or("too many windows")?;
        let win = imp::open(title.split('\0').next().unwrap_or(""), w, h)?;
        self.slots[slot] = Some(win);
        Ok(slot as u128 + 1)
    }

    fn get(&mut self, id: u128) -> Result<&mut imp::Win, String> {
        id.checked_sub(1)
            .and_then(|i| usize::try_from(i).ok())
            .and_then(|i| self.slots.get_mut(i))
            .and_then(Option::as_mut)
            .ok_or_else(|| format!("no window {id}"))
    }

    pub fn present(&mut self, id: u128, img: &[u128]) -> Result<u128, String> {
        let win = self.get(id)?;
        let (w, h) = match (img.first(), img.get(1)) {
            (Some(&w), Some(&h)) if w > 0 && h > 0 && w.checked_mul(h).and_then(|p| p.checked_add(2)) == Some(img.len() as u128) => (w as usize, h as usize),
            _ => return Err("win_present needs a non-empty image".into()),
        };
        imp::present(win, img, w, h)
    }

    pub fn poll(&mut self, id: u128, ms: u128) -> Result<Event, String> {
        imp::poll_event(self.get(id)?, ms)
    }

    pub fn size(&mut self, id: u128) -> Result<Vec<u128>, String> {
        let w = self.get(id)?;
        Ok(vec![w.w, w.h])
    }

    pub fn close(&mut self, id: u128) -> Result<u128, String> {
        self.get(id)?;
        if let Some(w) = self.slots[(id - 1) as usize].take() {
            imp::close(w);
        }
        Ok(0)
    }
}

impl Drop for WindowTable {
    fn drop(&mut self) {
        for w in self.slots.drain(..).flatten() {
            imp::close(w);
        }
    }
}
