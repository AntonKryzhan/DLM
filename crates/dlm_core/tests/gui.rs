//! v0.127 — own GUI, stage G1: raster images (`gx_*`, `file_write_png`), `std.ui`
//! (widgets, layout, rendering, hit testing, model/view/update, HTML backend over
//! TCP) and recursion into lists stored in records. Interpreter and native C agree
//! on every line and on the PNG bytes.
use dlm_core::{build_c_executable, default_c_compiler, emit_c, gfx, load_program_with_paths, lower_program, CheckPolicy, Checker, Runtime};
use std::path::{Path, PathBuf};
use std::process::Command;

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

/// Runs `source` (`{D}` = a work directory) in both runtimes; native lines and
/// the files each run left in the work directory.
fn run_both(name: &str, source: &str) -> (Vec<String>, Vec<(String, Vec<u8>)>) {
    let dir = std::env::temp_dir().join(format!("dlm-v127-{}-{name}", std::process::id()));
    let work = dir.join("work");
    std::fs::create_dir_all(&work).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source.replace("{D}", &work.display().to_string())).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let files = |w: &Path| {
        let mut v: Vec<(String, Vec<u8>)> = std::fs::read_dir(w).unwrap().map(|e| e.unwrap()).map(|e| (e.file_name().into_string().unwrap(), std::fs::read(e.path()).unwrap())).collect();
        v.sort();
        v
    };
    let interpreted = Runtime::new().run_program(&p).unwrap().output;
    let interpreted: Vec<String> = interpreted.join("\n").lines().map(str::to_string).collect();
    let first = files(&work);
    std::fs::remove_dir_all(&work).unwrap();
    std::fs::create_dir_all(&work).unwrap();
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(&emit_c(&ir), &dir.join("main.c"), &dir.join(if cfg!(windows) { "main.exe" } else { "main" }), &default_c_compiler()).unwrap();
    let out = Command::new(exe).output().unwrap();
    assert!(out.status.success(), "{}", String::from_utf8_lossy(&out.stderr));
    let native: Vec<String> = String::from_utf8(out.stdout).unwrap().lines().map(str::to_string).collect();
    assert_eq!(interpreted, native);
    assert_eq!(first, files(&work), "files differ between interpreter and native");
    std::fs::remove_dir_all(dir).ok();
    (native, first)
}

const APP: &str = r#"    record Model(count: Nat, dark: Bool, name: Text)
    fn view(m: Model) -> Widget = Panel("Counter", Column(6, list(
        Label(concat("count: ", nat_to_text(field(m, count)))),
        Row(6, list(Button("inc", "+1"), Button("reset", "reset"))),
        Check("dark", "dark mode", field(m, dark)),
        Field("name", field(m, name), 10),
        Bar(field(m, count), 5, 120))))
    fn update(m: Model, e: UiEvent) -> Model = match e {
        Press(id) => if_then_else(text_eq(id, "inc"), Model(field(m, count) + 1, field(m, dark), field(m, name)), if_then_else(text_eq(id, "reset"), Model(0, field(m, dark), field(m, name)), m)),
        Toggle(id) => Model(field(m, count), not(field(m, dark)), field(m, name)),
        Input(id, v) => Model(field(m, count), field(m, dark), v),
        Pointer(x, y) => m,
        _ => m }
    fn show(m: Model) -> Text = concat(concat(concat(nat_to_text(field(m, count)), if_then_else(field(m, dark), " dark ", " light ")), field(m, name)), "")
    fn nid(r: Result<Nat, Text>) -> Nat = match r { ok(n) => n, err(e) => 0 }
    fn rect_text(r: Rect) -> Text = join(list(nat_to_text(field(r, x)), nat_to_text(field(r, y)), nat_to_text(field(r, w)), nat_to_text(field(r, h))), ",")
    fn browser() -> Text =
        let l = nid(net_listen("127.0.0.1", 0)) in let p = nid(net_local_port(l)) in
        let c1 = nid(net_connect("127.0.0.1", p, 2000)) in let s1 = net_send(c1, "GET /e/press/inc HTTP/1.0\r\n\r\n") in
        let c2 = nid(net_connect("127.0.0.1", p, 2000)) in let s2 = net_send(c2, "GET /e/input/name?v=Ann+%C3%A9 HTTP/1.0\r\n\r\n") in
        let c3 = nid(net_connect("127.0.0.1", p, 2000)) in let s3 = net_send(c3, "GET / HTTP/1.0\r\n\r\n") in
        let m = serve_app(l, default_theme(), Model(0, false, ""), view, update, "demo", 3, 2000) in
        let r1 = match net_recv_all(c1) { ok(t) => substring(t, 0, 21), err(e) => e } in
        let page = match net_recv_all(c3) { ok(t) => t, err(e) => e } in
        let x = net_close(c1) in let y = net_close(c2) in let z = net_close(c3) in let q = net_close(l) in
        join(list(show(m), r1, if_then_else(is_some(index_of(page, "count: 1")), "page has count", "page lacks count"), if_then_else(is_some(index_of(page, "Ann")), "page has name", "no name")), " | ")
    let t = default_theme()
    let w = view(Model(2, true, "Bob"))
    print_line(join(map(layout(t, w, 6, 6), fun(p: Placed, concat(concat(nat_to_text(field(p, kind)), ":"), rect_text(field(p, rect))))), " "))
    print_line(nat_to_text(gx_checksum(screenshot(t, w))))
    print_line(show(apply_events(Model(0, false, ""), update, list(Press("inc"), Press("inc"), Toggle("dark"), Input("name", "Zed")))))
    print_line(show(click(t, Model(4, false, "x"), view, update, 20, 70)))
    print_line(match hit(layout(t, w, 6, 6), 1, 1) { none => "nothing", some(e) => "something" })
    print_line(match file_write_png("{D}/ui127.png", screenshot(t, w)) { ok(n) => "png ok", err(e) => e })
    print_line(html_escape("<a href=\"x\">&</a>"))
    print_line(match url_decode("a%20b+c%41") { none => "bad", some(s) => s })
    print_line(browser())
"#;

const ROSE: &str = r#"    record Dir(name: Text, files: List<Text>, subdirs: List<Dir>)
    enum W { Leaf(n: Nat), Box(kids: List<W>) }
    fn count(d: Dir) -> Nat decreasing d = length(field(d, files)) + fold(field(d, subdirs), 0, fun(acc: Nat, s: Dir, acc + count(s)))
    fn total(w: W) -> Nat decreasing w = match w { Leaf(n) => n, Box(kids) => fold(map(kids, fun(k: W, total(k))), 0, fun(a: Nat, x: Nat, a + x)) }
    print_line(nat_to_text(count(Dir("r", list("a", "b"), list(Dir("s", list("c"), empty_list(Dir)), Dir("u", empty_list(Text), list(Dir("v", list("d", "e"), empty_list(Dir)))))))))
    print_line(nat_to_text(total(Box(list(Leaf(1), Box(list(Leaf(2), Leaf(3))), Box(empty_list(W)))))))
"#;

/// Chunks of a PNG, with every CRC checked; the IDAT stored blocks unpacked.
fn png_pixels(png: &[u8]) -> (u32, u32, Vec<u8>) {
    assert_eq!(&png[..8], b"\x89PNG\r\n\x1a\n");
    let (mut i, mut w, mut h, mut z) = (8, 0, 0, Vec::new());
    while i < png.len() {
        let n = u32::from_be_bytes(png[i..i + 4].try_into().unwrap()) as usize;
        let kind = &png[i + 4..i + 8];
        let body = &png[i + 8..i + 8 + n];
        let mut c = !0u32;
        for &b in kind.iter().chain(body) {
            c ^= u32::from(b);
            for _ in 0..8 {
                c = if c & 1 == 1 { 0xEDB8_8320 ^ (c >> 1) } else { c >> 1 };
            }
        }
        assert_eq!(!c, u32::from_be_bytes(png[i + 8 + n..i + 12 + n].try_into().unwrap()));
        match kind {
            b"IHDR" => {
                w = u32::from_be_bytes(body[0..4].try_into().unwrap());
                h = u32::from_be_bytes(body[4..8].try_into().unwrap());
                assert_eq!(&body[8..], &[8, 6, 0, 0, 0]);
            }
            b"IDAT" => z.extend_from_slice(body),
            _ => {}
        }
        i += 12 + n;
    }
    let (mut j, mut raw) = (2, Vec::new());
    loop {
        let last = z[j] & 1 == 1;
        let len = u16::from_le_bytes([z[j + 1], z[j + 2]]) as usize;
        assert_eq!(!len as u16, u16::from_le_bytes([z[j + 3], z[j + 4]]));
        raw.extend_from_slice(&z[j + 5..j + 5 + len]);
        j += 5 + len;
        if last {
            break;
        }
    }
    let (mut a, mut b) = (1u32, 0u32);
    for &x in &raw {
        a = (a + u32::from(x)) % 65521;
        b = (b + a) % 65521;
    }
    assert_eq!((b << 16) | a, u32::from_be_bytes(z[j..j + 4].try_into().unwrap()));
    (w, h, raw)
}

#[test]
fn ui_toolkit_agrees_between_interpreter_and_native() {
    let (out, files) = run_both("app", &format!("module test.ui\nimport std.ui\ntheory App extends StdUi {{\n{APP}}}\n"));
    let want: &[&str] = &[
        "6:6,6,144,174 0:12,40,96,16 1:12,62,36,28 1:54,62,72,28 2:12,96,130,22 3:12,124,132,28 4:12,158,120,16",
        "3897417622122923747",
        "2 dark Zed",
        "5 light x",
        "nothing",
        "png ok",
        "&lt;a href=&quot;x&quot;&gt;&amp;&lt;/a&gt;",
        "a b cA",
        "1 light Ann é | HTTP/1.0 303 See Othe | page has count | page has name",
    ];
    assert_eq!(out, want);
    let (w, h, raw) = png_pixels(&files[0].1);
    assert_eq!((w, h), (156, 186));
    assert_eq!(raw.len(), (h * (1 + 4 * w)) as usize);
}

#[test]
fn recursion_into_record_lists_agrees() {
    let (out, _) = run_both("rose", &format!("module test.rose\ntheory App {{\n{ROSE}}}\n"));
    assert_eq!(out, vec!["5", "6"]);
}

#[test]
fn raster_primitives() {
    let img = gfx::new(4, 3, 0x0000_00FF).unwrap();
    assert_eq!(img.len(), 14);
    let img = gfx::fill_rect(img, 1, 1, 10, 10, 0xFF00_00FF);
    assert_eq!(gfx::get(&img, 0, 0), Some(0xFF));
    assert_eq!(gfx::get(&img, 3, 2), Some(0xFF00_00FF));
    assert_eq!(gfx::blend(0x0000_00FF, 0xFFFF_FF80), 0x8080_80FF);
    let img = gfx::line(gfx::new(5, 5, 0).unwrap(), 0, 0, 4, 4, 0xFFFF_FFFF);
    assert!((0..5).all(|i| gfx::get(&img, i, i) == Some(0xFFFF_FFFF)));
    assert_eq!((0..25).filter(|i| img[2 + i] != 0).count(), 5);
    let c = gfx::circle(gfx::new(9, 9, 0).unwrap(), 4, 4, 4, 1, true);
    assert_eq!(gfx::get(&c, 4, 0), Some(1));
    assert_eq!(gfx::get(&c, 0, 0), Some(0));
    assert_eq!(gfx::text_width("ab\ncde", 2), 36);
    let t = gfx::text(gfx::new(6, 8, 0).unwrap(), 0, 0, "I", 7, 1);
    assert_eq!((0..7).filter(|y| gfx::get(&t, 2, *y) == Some(7)).count(), 7);
    assert_eq!(gfx::fill_rect(vec![1, 2, 3], 0, 0, 1, 1, 5), vec![1, 2, 3]);
    let (w, h, raw) = png_pixels(&gfx::png(&gfx::new(300, 300, 0x1122_33FF).unwrap()).unwrap());
    assert_eq!((w, h), (300, 300));
    assert_eq!(&raw[..5], &[0, 0x11, 0x22, 0x33, 0xFF]);
}
