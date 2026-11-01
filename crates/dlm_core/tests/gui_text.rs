//! v0.129 — text of every script and scrolling widgets: TrueType fonts (`file_font_*`,
//! `vx_text_fonts`, scene op 9 with font fallback and fit), simplified bidi and Arabic
//! joining, `\\u{..}` escapes, code points, Scroll/ListView/Table with Select/Wheel events
//! and keyboard navigation, XIM text input in native windows. Interpreter and native C
//! agree on printed lines, PNG and SVG bytes.
use dlm_core::{build_c_executable, default_c_compiler, emit_c, font, load_program_with_paths, lower_program, vector, CheckPolicy, Checker, Runtime};
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
    let dir = std::env::temp_dir().join(format!("dlm-v129-{}-{name}", std::process::id()));
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

const FONT_TEXT: &str = r#"    fn fid(r: Result<Text, Text>) -> Nat = match r { ok(p) => match file_font_load(p) { ok(n) => n, err(e) => 0 }, err(e) => 0 }
    let fonts = nvl(list(fid(file_font_find("dejavusans.ttf")), fid(file_font_find("wqy-zenhei")), fid(file_font_find("dejavuserif.ttf"))))
    print_line(match file_font_load("/nonexistent/x.ttf") { ok(n) => "?", err(e) => e })
    print_line(match file_font_find("no-such-font-xyz") { ok(n) => "?", err(e) => e })
    print_symbolic(vx_codepoints("A\u{416}\u{1F600}\x41"))
    print_line(vx_from_codepoints(nvl(list(1046, 55296, 1114112, 65))))
    let sc = cat_all(list(rrect(0, 0, px(320), px(160), 0, solid(4294967295)),
        vx_text_fonts(px(4), px(4), px(20), 0, fonts, linear(0, 0, 255, px(200), 0, 4278190335), "Ёжик ∑ 漢字 Ωμέγα ü\nвторая строка"),
        vx_text_fonts(px(4), px(56), px(16), px(80), fonts, solid(255), "сжать по ширине"),
        vx_text_fonts(px(4), px(76), px(18), 0, fonts, solid(255), "مرحبا بالعالم 123 — שלום (1)"),
        vx_text_fonts(px(4), px(100), px(14), 0, nv_empty(), solid(255), "stroke Ж!"),
        vx_text_fonts(px(4), px(120), px(18), 0, fonts, solid(255), "\u{10FFFF}\u{1D538} e\u{301}")))
    print_line(nat_to_text(vx_font_width(fonts, "Ёжик ∑ 漢字", px(20))))
    print_symbolic(unwrap_or(vx_bounds(sc, 3, 2), nv_empty()))
    print_line(nat_to_text(gx_checksum(unwrap_or(vx_render(sc, 480, 240, 3, 2), nv_empty()))))
    print_line(nat_to_text(gx_checksum(unwrap_or(vx_render_region(unwrap_or(gx_new(320, 160, 4294967295), nv_empty()), sc, 1, 1, 10, 10, 120, 60), nv_empty()))))
    print_line(match file_write_png("{D}/text.png", unwrap_or(vx_render(sc, 320, 160, 1, 1), nv_empty())) { ok(n) => "png", err(e) => e })
    print_line(match file_write_atomic("{D}/text.svg", unwrap_or(vx_svg(sc, 320, 160), "bad")) { ok(n) => "svg", err(e) => e })
"#;

const LISTS: &str = r#"    record Lm(sel: Nat, off: Nat, toff: Nat, tsel: Nat, name: Text)
    fn names() -> List<Text> = list("Анна", "Борис", "Вера", "Глеб", "Дарья", "Егор", "Ζωή", "שלום", "مرحبا", "日本語", "한국어", "Ελλάδα")
    fn rows() -> List<List<Text>> = list(list("1", "Москва", "Россия"), list("2", "東京", "日本"), list("3", "Αθήνα", "Ελλάδα"), list("4", "Zürich", "Schweiz"), list("5", "서울", "대한민국"))
    fn view(m: Lm) -> Widget = Panel("Юникод: списки и таблицы", Column(6, list(
        Field("name", field(m, name), 14),
        ListView("names", names(), field(m, sel), 4, field(m, off), 12),
        Table("cities", list("№", "Город", "Страна"), rows(), list(2, 8, 9), field(m, tsel), 3, field(m, toff)),
        Scroll("more", 40, 1, list(Label("one"), Label("two"), Label("three"), Label("four"))))))
    fn update(m: Lm, e: UiEvent) -> Lm = match e {
        Select(id, i) => if_then_else(text_eq(id, "names"), Lm(i, follow(field(m, off), i, 4), field(m, toff), field(m, tsel), field(m, name)),
            Lm(field(m, sel), field(m, off), follow(field(m, toff), i, 3), i, field(m, name))),
        Wheel(id, d) => if_then_else(text_eq(id, "names"), Lm(field(m, sel), scrolled(field(m, off), d, length(names()), 4), field(m, toff), field(m, tsel), field(m, name)),
            Lm(field(m, sel), field(m, off), scrolled(field(m, toff), d, length(rows()), 3), field(m, tsel), field(m, name))),
        Input(id, v) => Lm(field(m, sel), field(m, off), field(m, toff), field(m, tsel), v),
        _ => m }
    fn show(m: Lm) -> Text = join(list(nat_to_text(field(m, sel)), nat_to_text(field(m, off)), nat_to_text(field(m, toff)), nat_to_text(field(m, tsel)), field(m, name)), " ")
    fn fid(r: Result<Text, Text>) -> Nat = match r { ok(p) => match file_font_load(p) { ok(n) => n, err(e) => 0 }, err(e) => 0 }
    let fonts = nvl(list(fid(file_font_find("dejavusans.ttf")), fid(file_font_find("wqy-zenhei"))))
    let t = default_theme()
    let m0 = Lm(2, 1, 0, 1, "Привет 😀")
    let items = layout(t, view(m0), 6, 6)
    print_line(join(map(items, fun(p: Placed, concat(concat(nat_to_text(field(p, kind)), ":"), field(p, text)))), " "))
    print_line(match hit(items, 30, 90) { none => "none", some(e) => match e { Select(id, i) => concat(concat(id, " "), nat_to_text(i)), _ => "other" } })
    print_line(match wheel_at(items, 30, 90, true) { none => "none", some(e) => match e { Wheel(id, d) => concat(id, if_then_else(d, " down", " up")), _ => "other" } })
    print_line(show(apply_events(m0, update, list(Wheel("names", true), Select("names", 9), Wheel("cities", true), Select("cities", 4), Wheel("names", false)))))
    print_line(join(list(nat_to_text(scrolled(8, true, 12, 4)), nat_to_text(scrolled(0, false, 12, 4)), nat_to_text(follow(5, 2, 4)), nat_to_text(follow(1, 9, 4))), " "))
    print_line(match route_event("/e/select/4/names") { none => "none", some(e) => match e { Select(id, i) => concat(id, nat_to_text(i)), _ => "?" } })
    print_line(match route_event("/e/wheel/up/cities") { none => "none", some(e) => match e { Wheel(id, d) => concat(id, if_then_else(d, " down", " up")), _ => "?" } })
    print_line(if_then_else(is_some(index_of(html_item(t, Placed(9, "names", "Вера", true, Rect(0, 0, 40, 20), 2, 0)), "/e/select/2/names")), "html rows", "no html rows"))
    print_line(if_then_else(is_some(index_of(vc_hit_html(t, Placed(7, "cities", "", false, Rect(0, 0, 40, 60), 0, 5)), "/e/wheel/down/cities")), "page wheel", "no page wheel"))
    print_line(nat_to_text(text_length(vector_page_f(t, fonts, view(m0), "x"))))
    print_line(match save_png("{D}/bitmap.png", t, view(m0)) { ok(n) => "bitmap", err(e) => e })
    print_line(match vector_png_f("{D}/fonts.png", t, fonts, no_state(), view(m0), 3, 2) { ok(n) => "fonts", err(e) => e })
    print_line(match vector_png("{D}/stroke.png", t, no_state(), view(m0), 1, 1) { ok(n) => "stroke", err(e) => e })
"#;

const WINDOW: &str = r#"    record Lm(sel: Nat, off: Nat, toff: Nat, tsel: Nat, name: Text)
    fn names() -> List<Text> = list("Анна", "Борис", "Вера", "Глеб", "Дарья", "Егор", "Ζωή", "שלום", "مرحبا", "日本語", "한국어", "Ελλάδα")
    fn rows() -> List<List<Text>> = list(list("1", "Москва", "Россия"), list("2", "東京", "日本"), list("3", "Αθήνα", "Ελλάδα"), list("4", "Zürich", "Schweiz"), list("5", "서울", "대한민국"))
    fn view(m: Lm) -> Widget = Panel("Юникод: списки и таблицы", Column(6, list(
        Field("name", field(m, name), 14),
        ListView("names", names(), field(m, sel), 4, field(m, off), 12),
        Table("cities", list("№", "Город", "Страна"), rows(), list(2, 8, 9), field(m, tsel), 3, field(m, toff)))))
    fn update(m: Lm, e: UiEvent) -> Lm = match e {
        Select(id, i) => if_then_else(text_eq(id, "names"), Lm(i, follow(field(m, off), i, 4), field(m, toff), field(m, tsel), field(m, name)),
            Lm(field(m, sel), field(m, off), follow(field(m, toff), i, 3), i, field(m, name))),
        Wheel(id, d) => if_then_else(text_eq(id, "names"), Lm(field(m, sel), scrolled(field(m, off), d, length(names()), 4), field(m, toff), field(m, tsel), field(m, name)),
            Lm(field(m, sel), field(m, off), scrolled(field(m, toff), d, length(rows()), 3), field(m, tsel), field(m, name))),
        Input(id, v) => Lm(field(m, sel), field(m, off), field(m, toff), field(m, tsel), v),
        _ => m }
    let fa = match file_font_load("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf") { ok(n) => n, err(e) => 0 }
    let fb = match file_font_find("wqy-zenhei") { ok(p) => match file_font_load(p) { ok(n) => n, err(e) => 0 }, err(e) => 0 }
    print_line(match run_window_f("DLM test", default_theme(), nvl(list(fa, fb)), Lm(2, 1, 0, 1, "Привет 😀"), view, update, 1, 1, 0) { err(e) => e, ok(m) =>
        join(list(nat_to_text(field(m, sel)), nat_to_text(field(m, off)), nat_to_text(field(m, toff)), nat_to_text(field(m, tsel)), field(m, name)), " ") })
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
static void wheel(Display *d, Window w, int x, int y, int b) {
    XEvent e; memset(&e, 0, sizeof e);
    e.xbutton.type = ButtonPress; e.xbutton.window = w; e.xbutton.x = x; e.xbutton.y = y; e.xbutton.button = b; e.xbutton.same_screen = True;
    XSendEvent(d, w, False, 0, &e); XFlush(d); usleep(60000);
    e.xbutton.type = ButtonRelease;
    XSendEvent(d, w, False, 0, &e); XFlush(d); usleep(60000);
}
static void keycode(Display *d, Window w, int kc) {
    XEvent e; memset(&e, 0, sizeof e);
    e.xkey.type = KeyPress; e.xkey.window = w; e.xkey.display = d; e.xkey.keycode = kc; e.xkey.same_screen = True;
    XSendEvent(d, w, False, 0, &e); XFlush(d); usleep(60000);
}
int main(int argc, char **argv) {
    Display *d = XOpenDisplay(0);
    KeySym syms[4] = { 0x6f6, 0x6d1, 0x100263A, 0x0101F642 };
    for (int i = 0; i < 4; i++) XChangeKeyboardMapping(d, 200 + i, 1, &syms[i], 1);
    XSync(d, False);
    Window w = 0;
    for (int i = 0; i < 200 && !w; i++) { w = find(d, DefaultRootWindow(d), "DLM test"); if (!w) usleep(50000); }
    if (!w) { fprintf(stderr, "no window\n"); return 1; }
    usleep(300000);
    button(d, w, ButtonPress, 50, 50); button(d, w, ButtonRelease, 50, 50);
    key(d, w, XK_BackSpace);
    for (int i = 0; i < 4; i++) keycode(d, w, 200 + i);
    wheel(d, w, 50, 100, 5);
    button(d, w, ButtonPress, 50, 86); button(d, w, ButtonRelease, 50, 86);
    key(d, w, XK_Down);
    wheel(d, w, 100, 220, 5);
    usleep(300000);
    if (argc > 1) { char cmd[256]; snprintf(cmd, sizeof cmd, "import -window root %s", argv[1]); system(cmd); }
    XEvent e; memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage; e.xclient.window = w; e.xclient.message_type = XInternAtom(d, "WM_PROTOCOLS", False);
    e.xclient.format = 32; e.xclient.data.l[0] = XInternAtom(d, "WM_DELETE_WINDOW", False);
    XSendEvent(d, w, False, 0, &e); XFlush(d);
    XCloseDisplay(d);
    return 0;
}
"#;

#[test]
fn font_text_agrees_between_interpreter_and_native() {
    let (out, files) = run_both("fonts", "test.fonts", FONT_TEXT);
    assert_eq!(out[0], "cannot read `/nonexistent/x.ttf` (os error 2)");
    assert_eq!(out[1], "no font file matching `no-such-font-xyz`");
    assert_eq!(out[2], "nvec[65, 1046, 128512, 65]");
    assert_eq!(out[3], "Ж\u{FFFD}\u{FFFD}A");
    assert_eq!(out[out.len() - 2..], ["png", "svg"]);
    assert_eq!(files.len(), 2);
}

#[test]
fn lists_tables_and_scrolling_agree_between_interpreter_and_native() {
    let (out, files) = run_both("lists", "test.lists", LISTS);
    assert!(out[0].starts_with("6:Юникод: списки и таблицы 3:Привет 😀 7: 9:Борис 9:Вера 9:Глеб 9:Дарья 8: 7: 11:№ 11:Город 11:Страна 9: 10:1 10:Москва"), "{}", out[0]);
    assert!(out[0].ends_with("7: 0:two 0:three 8:"), "{}", out[0]);
    assert_eq!(out[3], "9 5 2 4 Привет 😀");
    assert_eq!(out[4], "8 0 2 6");
    assert_eq!(out[5], "names4");
    assert_eq!(out[6], "cities up");
    assert_eq!(out[7..9], ["html rows", "page wheel"]);
    assert_eq!(out[10..], ["bitmap", "fonts", "stroke"]);
    assert_eq!(files.len(), 3);
}

#[test]
fn display_order_and_arabic_joining() {
    let cps = |s: &str| s.chars().map(|c| u128::from(c as u32)).collect::<Vec<_>>();
    let text = |v: Vec<u128>| v.into_iter().map(|c| char::from_u32(c as u32).unwrap()).collect::<String>();
    assert_eq!(text(vector::visual(&cps("abc 123"))), "abc 123");
    assert_eq!(text(vector::visual(&cps("שלום"))), "םולש");
    assert_eq!(text(vector::visual(&cps("abc שלום 42 def"))), "abc 42 םולש def");
    assert_eq!(text(vector::visual(&cps("שלום (1) abc"))), "abc (1) םולש");
    assert_eq!(text(vector::visual(&cps("א\u{5B8}ב"))), "ב\u{5D0}\u{5B8}");
    // beh isolated, final, initial, medial; lam-alef ligature
    assert_eq!(vector::visual(&cps("ب")), vec![0xFE8F]);
    assert_eq!(vector::visual(&cps("بب")), vec![0xFE90, 0xFE91]);
    assert_eq!(vector::visual(&cps("ببب")), vec![0xFE90, 0xFE92, 0xFE91]);
    assert_eq!(vector::visual(&cps("لا")), vec![0xFEFB]);
    assert_eq!(vector::visual(&cps("بلا")), vec![0xFEFC, 0xFE91]);
    assert_eq!(vector::visual(&cps("ab\nבג")), cps("ab\nגב"));
}

#[test]
fn truetype_parser_reads_system_fonts() {
    let mut fonts = font::FontTable::default();
    assert!(fonts.load("/nonexistent.ttf").unwrap_err().starts_with("cannot read"));
    let Ok(path) = font::find("dejavusans.ttf") else { return };
    let id = fonts.load(&path).unwrap();
    let f = fonts.get(id).unwrap();
    assert!(f.upem > 0 && f.ascender > 0 && f.descender < 0);
    for c in ['A', 'Ж', 'ё', 'Ω', 'ש', 'ب'] {
        let g = f.glyph_index(c as u32);
        assert!(g != 0, "{c}");
        assert!(f.advance(g) > 0);
        assert!(!f.outline(g).is_empty(), "{c}");
    }
    // composite glyph (precomposed letter with a diacritic)
    let g = f.glyph_index('Ǻ' as u32);
    assert!(g != 0 && f.outline(g).len() >= 2);
    assert_eq!(f.glyph_index(0x10FFFF), 0);
    if let Ok(cff) = font::find("notosanscjk-regular.ttc") {
        assert!(fonts.load(&cff).unwrap_err().contains("CFF"));
    }
}

/// XIM text input (Cyrillic, BMP and astral keysyms), wheel, row clicks and arrow keys under Xvfb.
#[test]
fn unicode_input_and_lists_in_a_native_window() {
    if cfg!(not(target_os = "linux")) || Command::new("Xvfb").arg("-help").output().is_err() || !Path::new("/usr/include/X11/Xlib.h").exists() {
        return;
    }
    let b = build("window", "test.win2", WINDOW);
    let helper = b.dir.join("inject");
    std::fs::write(b.dir.join("inject.c"), INJECT).unwrap();
    assert!(Command::new(default_c_compiler()).arg(b.dir.join("inject.c")).arg("-o").arg(&helper).arg("-lX11").status().unwrap().success());
    let display = format!(":{}", 80 + std::process::id() % 9);
    let mut xvfb = Command::new("Xvfb").args([display.as_str(), "-screen", "0", "640x480x24"]).spawn().unwrap();
    std::thread::sleep(std::time::Duration::from_millis(800));
    std::env::set_var("DISPLAY", &display);
    let mut inject = Command::new(&helper).spawn().unwrap();
    let interpreted = Runtime::new().run_program(&b.program).unwrap().output;
    inject.wait().unwrap();
    let mut inject = Command::new(&helper).spawn().unwrap();
    let out = Command::new(&b.exe).output().unwrap();
    inject.wait().unwrap();
    xvfb.kill().ok();
    let interpreted: Vec<String> = interpreted.join("\n").lines().map(str::to_string).collect();
    let native: Vec<String> = String::from_utf8(out.stdout).unwrap().lines().map(str::to_string).collect();
    assert_eq!(interpreted, native);
    assert_eq!(native, vec!["3 2 1 1 Привет жя☺🙂"]);
    std::fs::remove_dir_all(&b.dir).ok();
}
