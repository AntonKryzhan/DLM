// Usage: dlm run examples/valid/effect_file_transform.dlm -- INPUT OUTPUT
module examples.effects.files
import std.files

theory EffectFiles extends StdFiles {
    let options = args()
    let transform = effect_fn(FnEffect(Stdout, Text, Text), fun(text: Text,
        let log = print_line(concat("characters: ", nat_to_text(text_length(text)))) in concat(text, "\n")))
    print_line(match index(options, 0) {
        none => "usage: INPUT OUTPUT",
        some(src) => match index(options, 1) {
            none => "usage: INPUT OUTPUT",
            some(dst) => match transform_text(src, dst, transform) {
                err(e) => e, ok(n) => concat("written: ", nat_to_text(n))
            }
        }
    })
}
