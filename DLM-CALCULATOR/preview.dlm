module calculator.preview
import calculator.view

theory CalculatorPreview extends CalculatorView {
    fn cc_preview_font(name: Text) -> Nat = match file_font_find(name) { err(e) => 0, ok(path) => match file_font_load(path) { err(e) => 0, ok(id) => id } }
    fn main() -> Nat =
        let options = args() in
        let keys = unwrap_or(index(options, 0), "125+75=40*8=200+10%=") in
        let path = unwrap_or(index(options, 1), "calculator-preview.png") in
        let fonts = nvl(list(cc_preview_font("segoeui.ttf"), cc_preview_font("DejaVuSans.ttf"))) in
        let model = cc_sequence(cc_initial(), keys) in
        let chunks = cc_chunks(model, fonts, "=", "", 440, 720) in
        match vx_render(cat_all(chunks), 440, 720, 1, 1) {
            none => let line = print_line("Rendering failed") in 1,
            some(image) => match file_write_png(path, image) { err(e) => let line = print_line(e) in 1, ok(n) => let line = print_line(path) in 0 }
        }
}
