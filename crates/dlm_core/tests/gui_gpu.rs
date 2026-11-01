//! v0.131 — GPU presentation: `win_gpu` (OpenGL context, MSAA), `win_present_scene`
//! (vector scenes drawn by the GPU: convex fans, stencil-then-cover fills, gradient
//! textures, scissor clips, images; software fallback on other windows), `win_read`.
//! The window test runs under Xvfb with GLX when present; interpreter and native agree.
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
    let dir = std::env::temp_dir().join(format!("dlm-v131-{}-{name}", std::process::id()));
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

const GPU_APP: &str = r#"    fn fid(r: Result<Text, Text>) -> Nat = match r { ok(p) => match file_font_load(p) { ok(n) => n, err(e) => 0 }, err(e) => 0 }
    fn chan(c: Nat, s: Nat) -> Nat = mod(div(c, s), 256)
    fn on_white(c: Nat, s: Nat) -> Nat = div(mul(chan(c, s), chan(c, 1)) + mul(255, monus(255, chan(c, 1))) + 127, 255)
    fn d1(a: Nat, b: Nat) -> Nat = if_then_else(a > b, monus(a, b), monus(b, a))
    fn diff(g: NatVec, c: NatVec, k: Nat, acc: Nat) -> Nat decreasing k =
        if_then_else(nat_eq(k, 0), acc, let i = 2 + mul(k, 97) in let x = unwrap_or(nv_get(g, i), 0) in let y = unwrap_or(nv_get(c, i), 0) in
            diff(g, c, pred(k), acc + d1(chan(x, 16777216), on_white(y, 16777216)) + d1(chan(x, 65536), on_white(y, 65536)) + d1(chan(x, 256), on_white(y, 256))))
    fn res(r: Result<Nat, Text>) -> Text = match r { ok(n) => nat_to_text(n), err(e) => e }
    let fonts = nvl(list(fid(file_font_find("dejavusans.ttf"))))
    let star = close_path(line_to(line_to(line_to(line_to(move_to(path_start(), px(150), px(20)), px(190), px(140)), px(90), px(60)), px(210), px(60)), px(110), px(140)))
    let sc = cat_all(list(rrect(0, 0, px(300), px(200), 0, solid(4294967295)),
        rrect(px(10), px(10), px(120), px(80), px(12), linear(px(10), px(10), 4278190335, px(130), px(90), 65535)),
        ellipse_cmd(px(220), px(150), px(60), px(35), radial(px(220), px(150), px(60), 4294902015, 16777215)),
        fill_path(1, solid(2290649343), star), stroke_path(px(3), solid(255), star),
        clip_cmd(px(10), px(120), px(80), px(30)), vx_text_fonts(px(12), px(120), px(28), 0, fonts, solid(255), "Привет GPU"), unclip_cmd(),
        image_cmd(px(250), px(10), px(40), px(40), unwrap_or(gx_new(2, 2, 16711935), nv_empty()))))
    let cpu = unwrap_or(vx_render(sc, 300, 200, 1, 1), nv_empty())
    let report = match win_open("DLM GPU A", 300, 200) { err(e) => concat("window: ", e), ok(a) => match win_open("DLM GPU B", 300, 200) { err(e) => concat("window: ", e), ok(b) =>
        let info = match win_gpu(a) { ok(t) => if_then_else(is_some(index_of(t, "OpenGL")), "gpu: ok", t), err(e) => concat("gpu: ", e) } in
        let shown = res(win_present_scene(a, sc, 300, 200, 1, 1)) in
        let read = match win_read(a) { err(e) => e, ok(img) => if_then_else(nat_eq(nv_len(img), 60002),
            let avg = div(diff(img, cpu, 600, 0), 1800) in if_then_else(12 > avg, "close to software", concat("avg diff ", nat_to_text(avg))), "bad size") } in
        let again = res(win_present_scene(a, sc, 300, 200, 2, 1)) in
        let fallback = res(win_present_scene(b, sc, 300, 200, 1, 1)) in
        let no_read = match win_read(b) { ok(i) => "?", err(e) => e } in
        let invalid = res(win_present_scene(a, nvl(list(99)), 300, 200, 1, 1)) in
        let bad = res(win_present_scene(a, sc, 0, 200, 1, 1)) in
        let missing = match win_gpu(99) { ok(t) => "?", err(e) => e } in
        let ca = win_close(a) in let cb = win_close(b) in
        join(list(info, shown, read, again, fallback, no_read, invalid, bad, missing), "\n") } }
    print_line(report)
"#;

#[test]
fn gpu_draw_list_marks_convex_contours() {
    let fonts = font::FontTable::default();
    // nonzero fill of a five-pointed star (one self-intersecting contour)
    let mut scene = vec![1u128, 0, 0, 255, 6, 0, 2400, 320, 1, 3040, 2240, 1, 1440, 960, 1, 3360, 960, 1, 1760, 2240, 4];
    scene.extend_from_slice(&[8, 160, 160, 80, 80, 0, 255, 5, 0, 0, 160, 160, 3, 0, 0, 3200, 3200, 0, 0, 255, 6]);
    let items = vector::gpu_items(&scene, 300, 200, 1, 1, &fonts).unwrap();
    assert_eq!(items.len(), 3);
    match &items[0] {
        vector::GpuItem::Shape { convex, evenodd, .. } => assert!(!convex && !evenodd, "a star is not convex"),
        _ => panic!("shape"),
    }
    match &items[1] {
        vector::GpuItem::Shape { convex, clip, .. } => assert!(*convex && *clip == (0, 0, 300, 200)),
        _ => panic!("ellipse"),
    }
    match &items[2] {
        vector::GpuItem::Shape { clip, paint, .. } => {
            assert_eq!(*clip, (0, 0, 10, 10));
            assert!(matches!(paint, vector::GpuPaint::Solid(255)));
        }
        _ => panic!("clipped rectangle"),
    }
    assert!(vector::gpu_items(&[99], 10, 10, 1, 1, &fonts).is_none());
}

/// Real OpenGL under Xvfb (GLX): both runtimes draw on the GPU, read the frame back and agree.
#[test]
fn gpu_presentation_under_xvfb() {
    if cfg!(not(target_os = "linux")) || Command::new("Xvfb").arg("-help").output().is_err() {
        return;
    }
    let b = build("gpu", "test.gpu", GPU_APP);
    let display = format!(":{}", 70 + std::process::id() % 9);
    let mut xvfb = Command::new("Xvfb").args([display.as_str(), "-screen", "0", "640x480x24", "+extension", "GLX"]).spawn().unwrap();
    std::thread::sleep(std::time::Duration::from_millis(800));
    std::env::set_var("DISPLAY", &display);
    let interpreted = Runtime::new().run_program(&b.program).unwrap().output;
    let out = Command::new(&b.exe).output().unwrap();
    xvfb.kill().ok();
    let interpreted: Vec<String> = interpreted.join("\n").lines().map(str::to_string).collect();
    let native: Vec<String> = String::from_utf8(out.stdout).unwrap().lines().map(str::to_string).collect();
    assert_eq!(interpreted, native);
    if native.first().is_some_and(|l| l.starts_with("window:") || l.starts_with("gpu: cannot load") || l.starts_with("gpu: no OpenGL")) {
        std::fs::remove_dir_all(&b.dir).ok();
        return; // no X server or no OpenGL driver here
    }
    assert_eq!(native, vec![
        "gpu: ok",
        "1",
        "close to software",
        "1",
        "0",
        "win_read needs a GPU window with a presented scene",
        "invalid scene",
        "win_present_scene needs a size up to 16384 and a scale num/den in 1..65536",
        "no window 99",
    ]);
    std::fs::remove_dir_all(&b.dir).ok();
}
