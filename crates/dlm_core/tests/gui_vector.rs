//! v0.128 — vector UI (G2) and native windows (G3 core): `vx_*` scenes (paths,
//! Bézier curves, rounded rectangles, ellipses, gradients, clipping, images,
//! stroked Unicode text with Cyrillic), HiDPI rendering, SVG, incremental redraw,
//! `check_ui`, `win_*` and `run_window`. Interpreter and native C agree on the
//! printed lines and on the PNG bytes; the window test runs under Xvfb when present.
use dlm_core::{build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program, vector, CheckPolicy, Checker, Runtime};
use std::path::{Path, PathBuf};
use std::process::Command;

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

struct Built {
    dir: PathBuf,
    program: dlm_core::Program,
    exe: PathBuf,
}

fn build(name: &str, module: &str, source_body: &str) -> Built {
    let dir = std::env::temp_dir().join(format!("dlm-v128-{}-{name}", std::process::id()));
    let work = dir.join("work");
    std::fs::create_dir_all(&work).unwrap();
    let path = dir.join("main.dlm");
    let source = format!("module {module}\nimport std.vector\ntheory App extends StdVector {{\n{}}}\n", source_body.replace("{D}", &work.display().to_string()));
    std::fs::write(&path, source).unwrap();
    let program = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&program);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let ir = lower_program(&program, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(&emit_c(&ir), &dir.join("main.c"), &dir.join(if cfg!(windows) { "main.exe" } else { "main" }), &default_c_compiler()).unwrap();
    Built { dir, program, exe }
}

fn files(dir: &Path) -> Vec<(String, Vec<u8>)> {
    let mut v: Vec<(String, Vec<u8>)> = std::fs::read_dir(dir).unwrap().map(|e| e.unwrap()).map(|e| (e.file_name().into_string().unwrap(), std::fs::read(e.path()).unwrap())).collect();
    v.sort();
    v
}

fn run_both(name: &str, module: &str, body: &str) -> (Vec<String>, Vec<(String, Vec<u8>)>) {
    let b = build(name, module, body);
    let work = b.dir.join("work");
    let interpreted = Runtime::new().run_program(&b.program).unwrap().output;
    let interpreted: Vec<String> = interpreted.join("\n").lines().map(str::to_string).collect();
    let first = files(&work);
    std::fs::remove_dir_all(&work).unwrap();
    std::fs::create_dir_all(&work).unwrap();
    let out = Command::new(&b.exe).output().unwrap();
    assert!(out.status.success(), "{}", String::from_utf8_lossy(&out.stderr));
    let native: Vec<String> = String::from_utf8(out.stdout).unwrap().lines().map(str::to_string).collect();
    assert_eq!(interpreted, native);
    assert_eq!(first, files(&work), "files differ between interpreter and native");
    std::fs::remove_dir_all(&b.dir).ok();
    (native, first)
}

const VECTOR_UI: &str = r#"    record Pay(amount: Nat, confirmed: Bool, accepted: Bool)
    fn view(m: Pay) -> Widget = Panel("Платёж", Column(6, list(
        Label(concat("Сумма: ", nat_to_text(field(m, amount)))),
        Row(6, list(Button("add", "+100"), Button("pay", "Оплатить"))),
        Check("terms", "Согласен с условиями", field(m, accepted)),
        Field("name", "Иван", 12),
        Bar(field(m, amount), 500, 160))))
    fn step(m: Pay, e: UiEvent) -> Pay = match e {
        Press(b) => if_then_else(text_eq(b, "add"), Pay(field(m, amount) + 100, field(m, confirmed), field(m, accepted)),
            if_then_else(and(text_eq(b, "pay"), field(m, accepted)), Pay(field(m, amount), true, field(m, accepted)), m)),
        Toggle(b) => if_then_else(field(m, confirmed), m, Pay(field(m, amount), field(m, confirmed), not(field(m, accepted)))),
        _ => m }
    fn sloppy(m: Pay, e: UiEvent) -> Pay = match e { Press(b) => if_then_else(text_eq(b, "pay"), Pay(field(m, amount), true, field(m, accepted)), m), _ => step(m, e) }
    fn safe(m: Pay) -> Bool = or(not(field(m, confirmed)), field(m, accepted))
    fn evs() -> List<UiEvent> = list(Press("add"), Press("pay"), Toggle("terms"))
    fn showp(o: Option<List<UiEvent>>) -> Text = match o { none => "verified", some(p) => join(map(p, fun(e: UiEvent, match e { Press(b) => concat("press ", b), Toggle(b) => concat("toggle ", b), _ => "?" })), " -> ") }
    let t = default_theme()
    let m0 = Pay(200, false, true)
    let st = VState("add", "pay", list("none"))
    let img1 = vector_render(t, st, view(m0), 1, 1)
    let img2 = vector_render(t, st, view(m0), 2, 1)
    print_line(nat_to_text(gx_checksum(img1)))
    print_line(nat_to_text(gx_checksum(img2)))
    print_line(nat_to_text(gx_checksum(vector_render(t, st, view(m0), 5, 4))))
    print_line(match vector_png("{D}/vec1.png", t, st, view(m0), 1, 1) { ok(n) => "png1", err(e) => e })
    print_line(match vector_png("{D}/vec2.png", t, st, view(m0), 2, 1) { ok(n) => "png2", err(e) => e })
    print_line(nat_to_text(text_length(vector_svg(t, st, view(m0)))))
    let c0 = vector_chunks(t, st, view(m0))
    let c1 = vector_chunks(t, st, view(step(m0, Press("add"))))
    print_symbolic(dirty(c0, c1, 1, 1))
    let inc = redraw(img1, c0, c1, 1, 1)
    print_line(if_then_else(equal(inc, vector_render(t, st, view(step(m0, Press("add"))), 1, 1)), "incremental == full", "MISMATCH"))
    print_line(showp(check_ui(Pay(0, false, false), step, evs(), safe, 4)))
    print_line(showp(check_ui(Pay(0, false, false), sloppy, evs(), safe, 4)))
    print_line(nat_to_text(vx_text_width("Привет", 128)))
    print_line(nat_to_text(text_length(vector_page(t, view(m0), "demo"))))
"#;

const SHAPES: &str = r#"    fn view() -> Widget = Panel("Демо", Column(6, list(Label("Вектор"), Button("ok", "OK"), Check("c", "флажок", true))))
    let p = close_path(cubic_to(quad_to(line_to(move_to(path_start(), px(10), px(10)), px(60), px(10)), px(90), px(40), px(60), px(70)), px(40), px(90), px(5), px(60), px(10), px(10)))
    let sc = cat_all(list(rrect(0, 0, px(200), px(120), px(10), radial(px(100), px(60), px(120), 4294967295, 2290649343)),
        fill_path(1, linear(0, 0, 4278190335, px(100), px(100), 65535), p), stroke_path(px(3), solid(255), p),
        ellipse_cmd(px(150), px(60), px(30), px(20), solid(16711935)), clip_cmd(px(120), px(10), px(60), px(30)), text_cmd(px(110), px(15), px(16), px(2), solid(255), "Clip Ёж"), unclip_cmd(),
        image_cmd(px(150), px(90), px(40), px(20), unwrap_or(gx_new(2, 1, 4278190335), nv_empty()))))
    print_line(unwrap_or(vx_svg(sc, 200, 120), "bad"))
    print_line(match file_write_png("{D}/shapes.png", unwrap_or(vx_render(sc, 400, 240, 2, 1), nv_empty())) { ok(n) => "ok", err(e) => e })
    print_symbolic(unwrap_or(vx_bounds(sc, 1, 1), nv_empty()))
    print_line(vector_svg(default_theme(), no_state(), view()))
"#;

const WINDOW: &str = r#"    record Model(count: Nat, name: Text, ok: Bool)
    fn view(m: Model) -> Widget = Panel("Окно DLM", Column(6, list(
        Label(concat("count: ", nat_to_text(field(m, count)))),
        Button("inc", "+1"), Check("ok", "готово", field(m, ok)), Field("name", field(m, name), 12))))
    fn update(m: Model, e: UiEvent) -> Model = match e {
        Press(b) => if_then_else(text_eq(b, "inc"), Model(field(m, count) + 1, field(m, name), field(m, ok)), m),
        Toggle(b) => Model(field(m, count), field(m, name), not(field(m, ok))),
        Input(b, v) => Model(field(m, count), v, field(m, ok)),
        _ => m }
    print_line(match run_window("DLM test", default_theme(), Model(0, "ab", false), view, update, 1, 1, 0) {
        err(e) => e, ok(m) => concat(concat(concat(nat_to_text(field(m, count)), " "), field(m, name)), if_then_else(field(m, ok), " ok", " -")) })
    print_line(drop_last_char("Ёж"))
    print_line(concat(char_text(1046), char_text(65)))
"#;

const INJECT: &str = r#"#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static Window find(Display *d, Window w, const char *name) {
    char *n = 0;
    if (XFetchName(d, w, &n) && n) { int eq = !strcmp(n, name); XFree(n); if (eq) return w; }
    Window root, parent, *kids; unsigned nk;
    if (!XQueryTree(d, w, &root, &parent, &kids, &nk)) return 0;
    Window found = 0;
    for (unsigned i = 0; i < nk && !found; i++) found = find(d, kids[i], name);
    if (kids) XFree(kids);
    return found;
}
static void button(Display *d, Window w, int type, int x, int y) {
    XEvent e; memset(&e, 0, sizeof e);
    e.xbutton.type = type; e.xbutton.window = w; e.xbutton.x = x; e.xbutton.y = y; e.xbutton.button = 1; e.xbutton.same_screen = True;
    XSendEvent(d, w, False, 0, &e); XFlush(d); usleep(60000);
}
static void motion(Display *d, Window w, int x, int y) {
    XEvent e; memset(&e, 0, sizeof e);
    e.xmotion.type = MotionNotify; e.xmotion.window = w; e.xmotion.x = x; e.xmotion.y = y; e.xmotion.same_screen = True;
    XSendEvent(d, w, False, 0, &e); XFlush(d); usleep(60000);
}
static void key(Display *d, Window w, KeySym s) {
    XEvent e; memset(&e, 0, sizeof e);
    e.xkey.type = KeyPress; e.xkey.window = w; e.xkey.display = d; e.xkey.keycode = XKeysymToKeycode(d, s); e.xkey.same_screen = True;
    XSendEvent(d, w, False, 0, &e); XFlush(d); usleep(60000);
}
int main(int argc, char **argv) {
    Display *d = XOpenDisplay(0);
    Window w = 0;
    for (int i = 0; i < 200 && !w; i++) { w = find(d, DefaultRootWindow(d), "DLM test"); if (!w) usleep(50000); }
    if (!w) { fprintf(stderr, "no window\n"); return 1; }
    usleep(300000);
    if (argc > 1) { char cmd[256]; snprintf(cmd, sizeof cmd, "import -window root %s", argv[1]); system(cmd); }
    motion(d, w, 20, 70);
    button(d, w, ButtonPress, 20, 70); button(d, w, ButtonRelease, 20, 70);
    button(d, w, ButtonPress, 20, 70); button(d, w, ButtonRelease, 20, 70);
    button(d, w, ButtonPress, 20, 100); button(d, w, ButtonRelease, 20, 100);
    button(d, w, ButtonPress, 40, 135); button(d, w, ButtonRelease, 40, 135);
    key(d, w, XK_c); key(d, w, XK_BackSpace); key(d, w, XK_BackSpace);
    motion(d, w, 20, 70);
    usleep(200000);
    if (argc > 2) { char cmd[256]; snprintf(cmd, sizeof cmd, "import -window root %s", argv[2]); system(cmd); }
    XEvent e; memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage; e.xclient.window = w; e.xclient.message_type = XInternAtom(d, "WM_PROTOCOLS", False);
    e.xclient.format = 32; e.xclient.data.l[0] = XInternAtom(d, "WM_DELETE_WINDOW", False);
    XSendEvent(d, w, False, 0, &e); XFlush(d);
    XCloseDisplay(d);
    return 0;
}
"#;

#[test]
fn vector_ui_agrees_between_interpreter_and_native() {
    let (out, files) = run_both("ui", "test.vec", VECTOR_UI);
    let want: &[&str] = &[
        "1214559563773721130",
        "10264418930178492023",
        "12507661854961403026",
        "png1",
        "png2",
        "14390",
        "nvec[11, 40, 174, 176]",
        "incremental == full",
        "verified",
        "press pay",
        "576",
        "15161",
    ];
    assert_eq!(out, want);
    assert_eq!(files.len(), 2);
    assert_eq!(&files[1].1[16..24], &[0, 0, 2, 0x3c, 0, 0, 1, 0x74]);
}

#[test]
fn shapes_and_svg_agree_between_interpreter_and_native() {
    let (out, files) = run_both("shapes", "test.svg", SHAPES);
    let want: &[&str] = &[
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"120\" viewBox=\"0 0 200 120\"><defs><radialGradient id=\"g1\" gradientUnits=\"userSpaceOnUse\" cx=\"100\" cy=\"60\" r=\"120\"><stop offset=\"0\" stop-color=\"#ffffff\" stop-opacity=\"1\"/><stop offset=\"1\" stop-color=\"#888888\" stop-opacity=\"1\"/></radialGradient></defs><rect x=\"0\" y=\"0\" width=\"200\" height=\"120\" rx=\"10\" fill=\"url(#g1)\" fill-opacity=\"1\"/><defs><linearGradient id=\"g2\" gradientUnits=\"userSpaceOnUse\" x1=\"0\" y1=\"0\" x2=\"100\" y2=\"100\"><stop offset=\"0\" stop-color=\"#ff0000\" stop-opacity=\"1\"/><stop offset=\"1\" stop-color=\"#0000ff\" stop-opacity=\"1\"/></linearGradient></defs><path d=\"M 10 10 L 60 10 Q 90 40 60 70 C 40 90 5 60 10 10 Z\" fill=\"url(#g2)\" fill-opacity=\"1\" fill-rule=\"evenodd\"/><path d=\"M 10 10 L 60 10 Q 90 40 60 70 C 40 90 5 60 10 10 Z\" fill=\"none\" stroke=\"#000000\" stroke-opacity=\"1\" stroke-width=\"3\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><ellipse cx=\"150\" cy=\"60\" rx=\"30\" ry=\"20\" fill=\"#00ff00\" fill-opacity=\"1\"/><clipPath id=\"c3\"><rect x=\"120\" y=\"10\" width=\"60\" height=\"30\"/></clipPath><g clip-path=\"url(#c3)\"><path d=\"M 111 18 L 111 20 M 111 20 L 111 22 M 111 22 L 111 24 M 111 24 L 111 26 M 111 26 L 113 28 M 113 16 L 115 16 M 113 16 L 111 18 M 113 28 L 115 28 M 115 16 L 117 16 M 115 28 L 117 28 M 117 16 L 119 18 M 119 26 L 117 28 M 125 16 L 127 16 M 125 28 L 127 28 M 127 16 L 127 18 M 127 18 L 127 20 M 127 20 L 127 22 M 127 22 L 127 24 M 127 24 L 127 26 M 127 26 L 127 28 M 127 28 L 129 28 M 137 20 L 139 20 M 137 28 L 139 28 M 139 16 L 139 16 M 139 20 L 139 22 M 139 22 L 139 24 M 139 24 L 139 26 M 139 26 L 139 28 M 139 28 L 141 28 M 147 20 L 149 20 M 147 20 L 147 22 M 147 22 L 147 24 M 147 24 L 149 24 M 147 24 L 147 26 M 147 26 L 147 28 M 149 20 L 151 20 M 149 24 L 151 24 M 151 20 L 153 20 M 151 24 L 153 24 M 153 20 L 155 22 M 155 22 L 153 24 M 171 18 L 173 18 M 171 18 L 171 20 M 171 20 L 171 22 M 171 22 L 173 22 M 171 22 L 171 24 M 171 24 L 171 26 M 171 26 L 171 28 M 171 28 L 173 28 M 173 16 L 173 18 M 173 18 L 175 18 M 173 22 L 175 22 M 173 28 L 175 28 M 175 18 L 177 18 M 175 22 L 177 22 M 175 28 L 177 28 M 177 16 L 177 18 M 177 18 L 179 18 M 177 28 L 179 28 M 183 20 L 185 22 M 185 22 L 187 22 M 185 26 L 187 26 M 185 26 L 183 28 M 187 20 L 187 22 M 187 22 L 189 22 M 187 22 L 187 24 M 187 24 L 187 26 M 187 26 L 189 26 M 187 26 L 187 28 M 189 26 L 191 28 M 191 20 L 189 22\" fill=\"none\" stroke=\"#000000\" stroke-opacity=\"1\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/></g><image x=\"150\" y=\"90\" width=\"40\" height=\"20\" preserveAspectRatio=\"none\" style=\"image-rendering:pixelated\" href=\"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAYAAAD0In+KAAAAFElEQVR4AQEJAPb/AP8AAP//AAD/EfcD/d750N0AAAAASUVORK5CYII=\"/></svg>",
        "ok",
        "nvec[0, 0, 201, 121]",
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"118\" height=\"130\" viewBox=\"0 0 118 130\"><rect x=\"0\" y=\"0\" width=\"118\" height=\"130\" rx=\"0\" fill=\"#f1f1f1\" fill-opacity=\"1\"/><rect x=\"8\" y=\"10\" width=\"106\" height=\"118\" rx=\"8\" fill=\"#333333\" fill-opacity=\"1\"/><rect x=\"6\" y=\"6\" width=\"106\" height=\"118\" rx=\"8\" fill=\"#ebebeb\" fill-opacity=\"1\"/><clipPath id=\"c1\"><rect x=\"6\" y=\"6\" width=\"106\" height=\"28\"/></clipPath><g clip-path=\"url(#c1)\"><defs><linearGradient id=\"g2\" gradientUnits=\"userSpaceOnUse\" x1=\"6\" y1=\"6\" x2=\"6\" y2=\"34\"><stop offset=\"0\" stop-color=\"#6a87e1\" stop-opacity=\"1\"/><stop offset=\"1\" stop-color=\"#5677dd\" stop-opacity=\"1\"/></linearGradient></defs><rect x=\"6\" y=\"6\" width=\"106\" height=\"44\" rx=\"8\" fill=\"url(#g2)\" fill-opacity=\"1\"/></g><path d=\"M 13 23 L 15 23 M 13 23 L 13 25 M 15 15 L 15 17 M 15 17 L 15 19 M 15 19 L 15 21 M 15 21 L 15 23 M 15 23 L 17 23 M 17 13 L 19 13 M 17 13 L 15 15 M 17 23 L 19 23 M 19 13 L 19 15 M 19 15 L 19 17 M 19 17 L 19 19 M 19 19 L 19 21 M 19 21 L 19 23 M 19 23 L 21 23 M 21 23 L 21 25 M 25 19 L 25 21 M 25 21 L 27 21 M 25 21 L 25 23 M 25 23 L 27 25 M 27 17 L 29 17 M 27 17 L 25 19 M 27 21 L 29 21 M 27 25 L 29 25 M 29 17 L 31 17 M 29 21 L 31 21 M 29 25 L 31 25 M 31 17 L 33 19 M 31 21 L 33 21 M 33 19 L 33 21 M 37 17 L 37 19 M 37 19 L 39 19 M 37 19 L 37 21 M 37 21 L 37 23 M 37 23 L 37 25 M 39 19 L 41 21 M 43 19 L 45 19 M 43 19 L 41 21 M 45 17 L 45 19 M 45 19 L 45 21 M 45 21 L 45 23 M 45 23 L 45 25 M 49 19 L 49 21 M 49 21 L 49 23 M 49 23 L 51 25 M 51 17 L 53 17 M 51 17 L 49 19 M 51 25 L 53 25 M 53 17 L 55 17 M 53 25 L 55 25 M 55 17 L 57 19 M 57 19 L 57 21 M 57 21 L 57 23 M 57 23 L 55 25\" fill=\"none\" stroke=\"#ffffff\" stroke-opacity=\"1\" stroke-width=\"1.625\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M 13 41 L 15 41 M 13 41 L 13 43 M 13 43 L 13 45 M 13 45 L 13 47 M 13 47 L 15 47 M 13 47 L 13 49 M 13 49 L 13 51 M 13 51 L 13 53 M 13 53 L 15 53 M 15 41 L 17 41 M 15 47 L 17 47 M 15 53 L 17 53 M 17 41 L 19 41 M 17 47 L 19 47 M 17 53 L 19 53 M 19 41 L 21 43 M 19 47 L 21 49 M 21 43 L 21 45 M 21 45 L 19 47 M 21 49 L 21 51 M 21 51 L 19 53 M 25 47 L 25 49 M 25 49 L 27 49 M 25 49 L 25 51 M 25 51 L 27 53 M 27 45 L 29 45 M 27 45 L 25 47 M 27 49 L 29 49 M 27 53 L 29 53 M 29 45 L 31 45 M 29 49 L 31 49 M 29 53 L 31 53 M 31 45 L 33 47 M 31 49 L 33 49 M 33 47 L 33 49 M 37 45 L 37 47 M 37 47 L 37 49 M 37 49 L 39 49 M 37 49 L 37 51 M 37 51 L 37 53 M 39 49 L 41 51 M 41 47 L 39 49 M 41 51 L 43 53 M 43 45 L 41 47 M 49 45 L 51 45 M 51 45 L 53 45 M 53 45 L 55 45 M 53 45 L 53 47 M 53 47 L 53 49 M 53 49 L 53 51 M 53 51 L 53 53 M 55 45 L 57 45 M 61 47 L 61 49 M 61 49 L 61 51 M 61 51 L 63 53 M 63 45 L 65 45 M 63 45 L 61 47 M 63 53 L 65 53 M 65 45 L 67 45 M 65 53 L 67 53 M 67 45 L 69 47 M 69 47 L 69 49 M 69 49 L 69 51 M 69 51 L 67 53 M 73 45 L 75 45 M 73 45 L 73 47 M 73 47 L 73 49 M 73 49 L 75 49 M 73 49 L 73 51 M 73 51 L 73 53 M 75 45 L 77 45 M 75 49 L 77 49 M 77 45 L 79 45 M 77 49 L 79 49 M 79 45 L 81 47 M 81 47 L 79 49\" fill=\"none\" stroke=\"#222222\" stroke-opacity=\"1\" stroke-width=\"1.625\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><rect x=\"12\" y=\"63\" width=\"36\" height=\"28\" rx=\"6\" fill=\"#384d8f\" fill-opacity=\"1\"/><defs><linearGradient id=\"g3\" gradientUnits=\"userSpaceOnUse\" x1=\"12\" y1=\"62\" x2=\"12\" y2=\"90\"><stop offset=\"0\" stop-color=\"#7792e4\" stop-opacity=\"1\"/><stop offset=\"1\" stop-color=\"#5677dd\" stop-opacity=\"1\"/></linearGradient></defs><rect x=\"12\" y=\"62\" width=\"36\" height=\"28\" rx=\"6\" fill=\"url(#g3)\" fill-opacity=\"1\"/><path d=\"M 19 71 L 19 73 M 19 73 L 19 75 M 19 75 L 19 77 M 19 77 L 19 79 M 19 79 L 21 81 M 21 69 L 23 69 M 21 69 L 19 71 M 21 81 L 23 81 M 23 69 L 25 69 M 23 81 L 25 81 M 25 69 L 27 71 M 27 71 L 27 73 M 27 73 L 27 75 M 27 75 L 27 77 M 27 77 L 27 79 M 27 79 L 25 81 M 31 69 L 31 71 M 31 71 L 31 73 M 31 73 L 31 75 M 31 75 L 33 75 M 31 75 L 31 77 M 31 77 L 31 79 M 31 79 L 31 81 M 33 75 L 35 77 M 35 73 L 33 75 M 35 77 L 37 79 M 37 71 L 35 73 M 37 79 L 39 81 M 39 69 L 37 71\" fill=\"none\" stroke=\"#ffffff\" stroke-opacity=\"1\" stroke-width=\"1.625\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><rect x=\"12\" y=\"96\" width=\"16\" height=\"16\" rx=\"4\" fill=\"#989898\" fill-opacity=\"1\"/><rect x=\"13\" y=\"97\" width=\"14\" height=\"14\" rx=\"4\" fill=\"#5677dd\" fill-opacity=\"1\"/><path d=\"M 16 104 L 19.1875 107.1875 L 24 100.75\" fill=\"none\" stroke=\"#ffffff\" stroke-opacity=\"1\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M 35 103 L 35 105 M 35 105 L 37 107 M 37 101 L 39 101 M 37 101 L 35 103 M 37 107 L 39 107 M 39 97 L 39 99 M 39 99 L 39 101 M 39 101 L 41 101 M 39 101 L 39 103 M 39 103 L 39 105 M 39 105 L 39 107 M 39 107 L 41 107 M 39 107 L 39 109 M 41 101 L 43 103 M 43 103 L 43 105 M 43 105 L 41 107 M 49 103 L 49 105 M 49 105 L 49 107 M 49 107 L 47 109 M 51 101 L 53 101 M 51 101 L 49 103 M 53 101 L 55 101 M 55 101 L 55 103 M 55 103 L 55 105 M 55 105 L 55 107 M 55 107 L 55 109 M 59 107 L 61 109 M 61 101 L 63 101 M 61 105 L 63 105 M 61 105 L 59 107 M 61 109 L 63 109 M 63 101 L 65 101 M 63 105 L 65 105 M 63 109 L 65 109 M 65 101 L 67 103 M 65 105 L 67 105 M 65 109 L 67 109 M 67 103 L 67 105 M 67 105 L 67 107 M 67 107 L 67 109 M 71 101 L 73 103 M 73 103 L 75 103 M 73 107 L 75 107 M 73 107 L 71 109 M 75 101 L 75 103 M 75 103 L 77 103 M 75 103 L 75 105 M 75 105 L 75 107 M 75 107 L 77 107 M 75 107 L 75 109 M 77 107 L 79 109 M 79 101 L 77 103 M 83 103 L 83 105 M 83 105 L 83 107 M 83 107 L 85 109 M 85 101 L 87 101 M 85 101 L 83 103 M 85 109 L 87 109 M 87 101 L 89 101 M 87 109 L 89 109 M 89 101 L 91 103 M 91 103 L 91 105 M 91 105 L 91 107 M 91 107 L 89 109 M 95 101 L 95 103 M 95 103 L 95 105 M 95 105 L 97 105 M 95 105 L 95 107 M 95 107 L 95 109 M 97 105 L 99 107 M 99 103 L 97 105 M 99 107 L 101 109 M 101 101 L 99 103\" fill=\"none\" stroke=\"#222222\" stroke-opacity=\"1\" stroke-width=\"1.625\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/></svg>",
    ];
    assert_eq!(out, want);
    assert_eq!(files.len(), 1);
}

#[test]
fn rasterizer_is_exact_on_simple_shapes() {
    // a 4x4 opaque square covers exactly 16 pixels, a half-pixel offset gives 50 % edges
    let sq = [3, 16, 16, 64, 64, 0, 0, 0xFF00_00FF];
    let img = vector::render(&sq, 8, 8, 1, 1).unwrap();
    assert_eq!((0..64).filter(|i| img[2 + i] == 0xFF00_00FF).count(), 16);
    let half = [3, 8, 16, 64, 64, 0, 0, 0xFF00_00FF];
    let img = vector::render(&half, 8, 8, 1, 1).unwrap();
    assert_eq!(img[2 + 8 + 0], 0xFF00_0080);
    // incremental region render equals the full render inside the region
    let scene = [8, 64, 64, 48, 32, 1, 0, 0, 0xFF, 128, 128, 0xFFFF_FFFF];
    let full = vector::render(&scene, 8, 8, 1, 1).unwrap();
    let blank = vector::render(&[], 8, 8, 1, 1).unwrap();
    let part = vector::render_region(blank, &scene, 1, 1, 2, 2, 4, 4).unwrap();
    for y in 2..6 {
        for x in 2..6 {
            assert_eq!(part[2 + y * 8 + x], full[2 + y * 8 + x]);
        }
    }
    assert_eq!(vector::text_width("Ёж\nabc", 128), 288);
    assert!(vector::render(&[9], 1, 1, 1, 1).is_none());
    assert!(vector::svg(&[1, 0, 0], 1, 1).is_none());
}

/// Real windows: Xvfb, an Xlib helper that clicks, types and closes; both runtimes end in the same model.
#[test]
fn native_window_under_xvfb() {
    if cfg!(not(target_os = "linux")) || Command::new("Xvfb").arg("-help").output().is_err() || !Path::new("/usr/include/X11/Xlib.h").exists() {
        return;
    }
    let b = build("window", "test.win", WINDOW);
    let helper = b.dir.join("inject");
    std::fs::write(b.dir.join("inject.c"), INJECT).unwrap();
    assert!(Command::new(default_c_compiler()).arg(b.dir.join("inject.c")).arg("-o").arg(&helper).arg("-lX11").status().unwrap().success());
    let display = format!(":{}", 90 + std::process::id() % 9);
    let mut xvfb = Command::new("Xvfb").args([display.as_str(), "-screen", "0", "400x300x24"]).spawn().unwrap();
    std::thread::sleep(std::time::Duration::from_millis(800));
    std::env::set_var("DISPLAY", &display);
    let mut inject = Command::new(&helper).spawn().unwrap();
    let interpreted = Runtime::new().run_program(&b.program).unwrap().output;
    inject.wait().unwrap();
    let mut inject = Command::new(&helper).spawn().unwrap();
    let out = Command::new(&b.exe).output().unwrap();
    inject.wait().unwrap();
    xvfb.kill().ok();
    let native: Vec<String> = String::from_utf8(out.stdout).unwrap().lines().map(str::to_string).collect();
    assert_eq!(interpreted, native);
    assert_eq!(native, vec!["2 a ok", "Ё", "ЖA"]);
    std::fs::remove_dir_all(&b.dir).ok();
}
