module universe.preview
import universe.window

theory UniversePreview extends UniverseWindow {
    // Headless image: preview <mode 0..7> <steps> <output.png> <count>.
    fn main() -> Nat =
        let options = args() in
        let mode = unwrap_or(text_to_nat(unwrap_or(index(options, 0), "0")), 0) in
        let steps = ui_min_v(unwrap_or(text_to_nat(unwrap_or(index(options, 1), "100")), 100), 3000) in
        let path = unwrap_or(index(options, 2), "Universe-preview.png") in
        let count = unwrap_or(text_to_nat(unwrap_or(index(options, 3), "240")), 240) in
        let app = u_initial(count, 7291) in
        let w = u_steps(u_create(mode, 7291, field(field(app, settings), count)), u_pointer(), true, steps) in
        let scene = u_scene(u_with_world(app, w), u_fonts(), 1200, 800) in
        match vx_render(scene, 1200, 800, 1, 1) {
            none => let line = print_line("Rendering failed") in 1,
            some(image) => match file_write_png(path, image) { err(e) => let line = print_line(e) in 1, ok(n) => let line = print_line(path) in 0 }
        }
}
