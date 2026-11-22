module calculator.main
import calculator.window

theory CalculatorApp extends CalculatorWindow {
    fn main() -> Nat =
        let options = args() in
        let scale = if_then_else(text_eq(unwrap_or(index(options, 0), ""), "--scale"), unwrap_or(text_to_nat(unwrap_or(index(options, 1), "100")), 100), 100) in
        match cc_run(scale) {
            ok(model) => 0,
            err(message) => let saved = file_write_atomic("calculator-error.txt", message) in let line = print_line(message) in 1
        }
}
