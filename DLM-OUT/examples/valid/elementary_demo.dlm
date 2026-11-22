module examples.elementary_demo
import std.creal

// Elementary functions of computable reals with guaranteed enclosures; the
// report is assembled in an owned text builder (TextBuf).
theory App extends StdCReal {
    let budget = ExactBudget(512, 1000000000000000)
    fn show(t: Text, n: Nat) -> Text = match creal_parse(budget, t) {
        err(e) => e,
        ok(x) => match creal_approx(field(x, budget), field(x, value), n) {
            err(e) => e,
            ok(v) => match ivq_text(field(v, budget), field(v, value)) { err(e) => e, ok(s) => field(s, value) }
        }
    }
    fn line(r: TextBuf, label: Text, t: Text) -> TextBuf = tbuf_push(r, concat(label, concat(" ", concat(show(t, 8), "\n"))))
    fn report() -> Text =
        let r = line(tbuf_new(), "pi", "pi") in
        let r = line(r, "e", "c 1 ; exp 0") in
        let r = line(r, "log2", "c 2 ; log 0") in
        let r = line(r, "sin1", "c 1 ; sin 0") in
        let r = line(r, "cos(pi)", "pi ; cos 0") in
        let r = line(r, "log(-1)", "c -1 ; log 0") in
        tbuf_finish(r)
    print_text(report())
}
