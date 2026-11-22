module calculator.tests
import calculator.core

theory CalculatorTests extends CalculatorCore {
    record CalcCase(name: Text, keys: Text, expected: Text)
    record CalcCheck(name: Text, passed: Bool)
    fn cc_expect_entry(m: CalcModel, expected: Text) -> Bool = and(text_eq(field(field(m, engine), entry), expected), cc_invariant(m))
    fn cc_actions(m: CalcModel, keys: List<Text>) -> CalcModel = fold(keys, m, cc_press)
    fn cc_test_check(failures: Nat, test: CalcCheck) -> Nat =
        let line = print_line(concat(if_then_else(field(test, passed), "PASS ", "FAIL "), field(test, name))) in
        failures + if_then_else(field(test, passed), 0, 1)
    fn cc_test_case(failures: Nat, test: CalcCase) -> Nat =
        let model = cc_sequence(cc_initial(), field(test, keys)) in
        let got = field(field(model, engine), entry) in
        let pass = and(text_eq(got, field(test, expected)), cc_invariant(model)) in
        let line = print_line(concat(if_then_else(pass, "PASS ", "FAIL "), concat(field(test, name), concat(" -> ", got)))) in
        failures + if_then_else(pass, 0, 1)
    fn main() -> Nat =
        let cases = list(
            CalcCase("addition", "12+34=", "46"),
            CalcCase("negative result", "2-5=", "-3"),
            CalcCase("multiplication", "12*7=", "84"),
            CalcCase("division", "81/9=", "9"),
            CalcCase("decimal", "0.1+0.2=", "0.3"),
            CalcCase("decimal comma", "1,5*2=", "3"),
            CalcCase("consecutive decimal point", "1..5+2=", "3.5"),
            CalcCase("leading decimal point", ".5+.25=", "0.75"),
            CalcCase("leading zeroes", "00012+3=", "15"),
            CalcCase("repeat equals", "2+3===", "11"),
            CalcCase("repeat zero operand", "5+0==", "5"),
            CalcCase("missing right operand", "5+=", "10"),
            CalcCase("replace operator", "12+*3=", "36"),
            CalcCase("sequential operators", "2+3*4=", "20"),
            CalcCase("new input after equals", "1+2=4", "4"),
            CalcCase("percent", "50%", "0.5"),
            CalcCase("context addition percent", "200+10%=", "220"),
            CalcCase("context subtraction percent", "200-10%=", "180"),
            CalcCase("multiplication percent", "200*10%=", "20"),
            CalcCase("division percent", "200/10%=", "2000"),
            CalcCase("sqrt", "81r", "9"),
            CalcCase("square", "12s", "144"),
            CalcCase("reciprocal", "4i", "0.25"),
            CalcCase("sign input", "5n+2=", "-3"),
            CalcCase("negative right operand", "5+n2=", "3"),
            CalcCase("division by zero", "1/0=", "Ошибка"),
            CalcCase("negative square root", "9nr", "Ошибка"),
            CalcCase("error recovery", "1/0=7+1=", "8"),
            CalcCase("clear", "123C4", "4"),
            CalcCase("display rounding", "1/3=", "0.333333333333"),
            CalcCase("rounding keeps full value", "1/3=*3=", "1"),
            CalcCase("entry precision limit", "1234567890123456789", "1234567890123456"),
            CalcCase("new decimal after result", "2+3=.5", "0.5"),
            CalcCase("negative decimal input", "n.5*2=", "-1"),
            CalcCase("negative input from zero", "n5+2=", "-3"),
            CalcCase("negative zero normalizes", "0n=", "0"),
            CalcCase("unary preserves pending operator", "5+9r=", "8"),
            CalcCase("repeat division", "64/2===", "8"),
            CalcCase("trailing decimal", "2.+3.=", "5")) in
        let history = cc_sequence(cc_initial(), "1+1==========") in
        let huge = cc_recall(cc_initial(), fl_lit("1e308"), "") in
        let checks = list(
            CalcCheck("CE preserves pending operator", cc_expect_entry(cc_actions(cc_sequence(cc_initial(), "12+34"), list("CE", "5", "=")), "17")),
            CalcCheck("CE after result", cc_expect_entry(cc_press(cc_sequence(cc_initial(), "2+3="), "CE"), "0")),
            CalcCheck("backspace edits entry", cc_expect_entry(cc_press(cc_sequence(cc_initial(), "123"), "back"), "12")),
            CalcCheck("backspace decimal", cc_expect_entry(cc_actions(cc_sequence(cc_initial(), "12.3"), list("back", "back")), "12")),
            CalcCheck("backspace negative digit", cc_expect_entry(cc_press(cc_sequence(cc_initial(), "5n"), "back"), "0")),
            CalcCheck("backspace after result", cc_expect_entry(cc_press(cc_sequence(cc_initial(), "2+3="), "back"), "0")),
            CalcCheck("MS and MR", cc_expect_entry(cc_actions(cc_sequence(cc_initial(), "42"), list("MS", "C", "MR")), "42")),
            CalcCheck("memory add and subtract", cc_expect_entry(cc_actions(cc_sequence(cc_initial(), "10"), list("MS", "C", "3", "M+", "C", "2", "M-", "MR")), "11")),
            CalcCheck("memory recall as right operand", cc_expect_entry(cc_actions(cc_sequence(cc_initial(), "8"), list("MS", "C", "2", "+", "MR", "=")), "10")),
            CalcCheck("digit replaces recalled memory", cc_expect_entry(cc_actions(cc_sequence(cc_initial(), "8"), list("MS", "MR", "3")), "3")),
            CalcCheck("MC clears flag", not(field(cc_actions(cc_sequence(cc_initial(), "8"), list("MS", "MC")), has_memory))),
            CalcCheck("zero can be stored", field(cc_press(cc_initial(), "MS"), has_memory)),
            CalcCheck("history retains last eight", nat_eq(length(field(history, history)), 8)),
            CalcCheck("history recall", cc_expect_entry(cc_press(history, "h2"), "9")),
            CalcCheck("history clear preserves result", let m = cc_press(history, "history-clear") in and(is_empty(field(m, history)), cc_expect_entry(m, "11"))),
            CalcCheck("unknown key preserves state", equal(cc_key(history, 999), history)),
            CalcCheck("native Enter", cc_expect_entry(cc_key(cc_sequence(cc_initial(), "12+3"), 130829), "15")),
            CalcCheck("native Escape", cc_expect_entry(cc_key(history, 130843), "0")),
            CalcCheck("native Delete", cc_expect_entry(cc_key(history, 131071), "0")),
            CalcCheck("native Backspace", cc_expect_entry(cc_key(cc_sequence(cc_initial(), "123"), 130824), "12")),
            CalcCheck("format carry", text_eq(cc_format(fl_lit("9.999999999999")), "10")),
            CalcCheck("format large exponent", text_eq(cc_format(fl_lit("1.23456789012345e100")), "1.23456789012e100")),
            CalcCheck("format small exponent", text_eq(cc_format(fl_lit("1.25e-20")), "1.25e-20")),
            CalcCheck("format fixed small", text_eq(cc_format(fl_lit("0.00000125")), "0.00000125")),
            CalcCheck("format subnormal", text_eq(cc_format(fl_lit("5e-324")), "5e-324")),
            CalcCheck("overflow reports error", text_length(field(field(cc_press(huge, "square"), engine), error)) > 0),
            CalcCheck("overflow recovery", cc_expect_entry(cc_actions(huge, list("square", "7", "+", "1", "=")), "8")),
            CalcCheck("memory overflow preserves finite memory", let m = cc_actions(huge, list("MS", "M+")) in and(cc_invariant(m), text_length(field(field(m, engine), error)) > 0)),
            CalcCheck("error can recover from memory", cc_expect_entry(cc_actions(cc_sequence(cc_initial(), "7"), list("MS", "/", "0", "=", "MR")), "7"))) in
        let failed = fold_effect(checks, fold_effect(cases, 0, cc_test_case), cc_test_check) in
        let done = print_line(concat("Failures: ", nat_to_text(failed))) in failed
}
