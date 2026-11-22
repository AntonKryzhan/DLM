// Standard calculator. Float64 arithmetic; 12 significant digits on output.
// The full value is kept separately from its formatted display text.
module calculator.core
import std.vector

theory CalculatorCore extends StdVector {
    record CalcEngine(entry: Text, value: Float, acc: Float, op: Text, replace: Bool, ready: Bool, repeat_op: Text, repeat_rhs: Float, error: Text, hint: Text)
    record CalcHistory(text: Text, value: Float)
    record CalcModel(engine: CalcEngine, memory: Float, has_memory: Bool, history: List<CalcHistory>)

    fn cc_engine() -> CalcEngine = CalcEngine("0", 0.0, 0.0, "", true, false, "", 0.0, "", "")
    fn cc_initial() -> CalcModel = CalcModel(cc_engine(), 0.0, false, empty_list(CalcHistory))
    fn cc_put(m: CalcModel, e: CalcEngine) -> CalcModel = CalcModel(e, field(m, memory), field(m, has_memory), field(m, history))
    fn cc_take_history(xs: List<CalcHistory>, n: Nat) -> List<CalcHistory> decreasing n =
        if_then_else(nat_eq(n, 0), empty_list(CalcHistory), match xs { [] => xs, cons(h, rest) => cons(h, cc_take_history(rest, pred(n))) })
    fn cc_log(m: CalcModel, e: CalcEngine, line: Text) -> CalcModel =
        CalcModel(e, field(m, memory), field(m, has_memory), cons(CalcHistory(line, field(e, value)), cc_take_history(field(m, history), 7)))
    fn cc_zeros(n: Nat) -> Text decreasing n = if_then_else(nat_eq(n, 0), "", concat("0", cc_zeros(pred(n))))
    fn cc_first_digit(s: Text, i: Nat, left: Nat) -> Nat decreasing left =
        if_then_else(nat_eq(left, 0), i, if_then_else(text_eq(substring(s, i, 1), "0"), cc_first_digit(s, i + 1, pred(left)), i))
    fn cc_trim_zeroes(s: Text, left: Nat) -> Text decreasing left =
        if_then_else(nat_eq(left, 0), s,
            if_then_else(or(not(text_length(s) > 1), not(text_eq(substring(s, pred(text_length(s)), 1), "0"))), s,
                cc_trim_zeroes(substring(s, 0, pred(text_length(s))), pred(left))))
    fn cc_format_parts(digits: Text, exponent: Int) -> Text =
        if_then_else(and(not(-6 > exponent), int(12) > exponent),
            if_then_else(not(int(0) > exponent),
                let at = abs(exponent) + 1 in if_then_else(not(text_length(digits) > at),
                    concat(digits, cc_zeros(monus(at, text_length(digits)))),
                    concat(substring(digits, 0, at), concat(".", substring(digits, at, monus(text_length(digits), at))))),
                concat("0.", concat(cc_zeros(monus(abs(exponent), 1)), digits))),
            concat(substring(digits, 0, 1), concat(if_then_else(text_length(digits) > 1, concat(".", substring(digits, 1, pred(text_length(digits)))), ""), concat("e", int_to_text(exponent)))))
    fn cc_format(x: Float) -> Text =
        if_then_else(fl_eq(x, 0.0), "0", if_then_else(not(fl_is_finite(x)), "Ошибка",
            let raw = fl_to_text(fl_abs(x)) in
            let pos = unwrap_or(index_of(raw, "e"), text_length(raw)) in
            let mantissa = substring(raw, 0, pos) in
            let extra = if_then_else(text_length(raw) > pos, unwrap_or(text_to_int(substring(raw, pos + 1, monus(text_length(raw), pos + 1))), int(0)), int(0)) in
            let dot = unwrap_or(index_of(mantissa, "."), text_length(mantissa)) in
            let all = replace_all(mantissa, ".", "") in
            let first = cc_first_digit(all, 0, text_length(all)) in
            let significant = substring(all, first, monus(text_length(all), first)) in
            let initial = unwrap_or(text_to_nat(substring(significant, 0, 12)), 0) in
            let next = unwrap_or(text_to_nat(substring(significant, 12, 1)), 0) in
            let rounded = initial + if_then_else(next > 4, 1, 0) in
            let carry = rounded > 999999999999 in
            let kept = nat_to_text(if_then_else(carry, div(rounded, 10), rounded)) in
            let exponent = extra + int(dot) - int(first) - int(1) + if_then_else(carry, int(1), int(0)) in
            let out = cc_format_parts(cc_trim_zeroes(kept, text_length(kept)), exponent) in
            concat(if_then_else(fl_lt(x, 0.0), "-", ""), out)))
    fn cc_symbol(op: Text) -> Text = if_then_else(text_eq(op, "*"), "×", if_then_else(text_eq(op, "/"), "÷", if_then_else(text_eq(op, "-"), "−", op)))
    fn cc_expression(a: Float, op: Text, b: Float) -> Text = concat(cc_format(a), concat(" ", concat(cc_symbol(op), concat(" ", cc_format(b)))))
    fn cc_pending(e: CalcEngine) -> Text = if_then_else(nat_eq(text_length(field(e, op)), 0), "", concat(cc_format(field(e, acc)), concat(" ", cc_symbol(field(e, op)))))
    fn cc_failed(message: Text) -> CalcEngine = CalcEngine("Ошибка", 0.0, 0.0, "", true, false, "", 0.0, message, "")
    fn cc_value(e: CalcEngine, text: Text, value: Float, replace: Bool, hint: Text) -> CalcEngine =
        CalcEngine(text, value, field(e, acc), field(e, op), replace, true, "", 0.0, "", hint)
    fn cc_parse_entry(text: Text) -> Float =
        let n = text_length(text) in let normalized = if_then_else(text_eq(substring(text, monus(n, 1), 1), "."), concat(text, "0"), text) in
        unwrap_or(fl_parse(normalized), 0.0)
    fn cc_digit(m: CalcModel, digit: Text) -> CalcModel =
        let before = field(m, engine) in let e = if_then_else(text_length(field(before, error)) > 0, cc_engine(), before) in
        let old = if_then_else(field(e, replace), "0", field(e, entry)) in
        let count = text_length(replace_all(replace_all(old, ".", ""), "-", "")) in
        if_then_else(and(not(field(e, replace)), not(16 > count)), m,
            let text = if_then_else(text_eq(old, "0"), digit, if_then_else(text_eq(old, "-0"), concat("-", digit), concat(old, digit))) in
            cc_put(m, cc_value(e, text, cc_parse_entry(text), false, cc_pending(e))))
    fn cc_dot(m: CalcModel) -> CalcModel =
        let before = field(m, engine) in let e = if_then_else(text_length(field(before, error)) > 0, cc_engine(), before) in
        let old = if_then_else(field(e, replace), "0", field(e, entry)) in
        if_then_else(is_some(index_of(old, ".")), m, cc_put(m, cc_value(e, concat(old, "."), cc_parse_entry(old), false, cc_pending(e))))
    fn cc_clear_entry(m: CalcModel) -> CalcModel =
        let before = field(m, engine) in let e = if_then_else(text_length(field(before, error)) > 0, cc_engine(), before) in
        cc_put(m, cc_value(e, "0", 0.0, true, cc_pending(e)))
    fn cc_back(m: CalcModel) -> CalcModel =
        let e = field(m, engine) in
        if_then_else(or(field(e, replace), text_length(field(e, error)) > 0), cc_clear_entry(m),
            let trimmed = substring(field(e, entry), 0, monus(text_length(field(e, entry)), 1)) in
            let text = if_then_else(or(nat_eq(text_length(trimmed), 0), text_eq(trimmed, "-")), "0", trimmed) in
            cc_put(m, cc_value(e, text, cc_parse_entry(text), false, cc_pending(e))))
    fn cc_sign(m: CalcModel) -> CalcModel =
        let e = field(m, engine) in
        if_then_else(text_length(field(e, error)) > 0, cc_put(m, cc_value(cc_engine(), "-0", fl_neg(0.0), false, "")),
            if_then_else(and(text_length(field(e, op)) > 0, not(field(e, ready))), cc_put(m, cc_value(e, "-0", fl_neg(0.0), false, cc_pending(e))),
                let old = field(e, entry) in
                let text = if_then_else(text_eq(substring(old, 0, 1), "-"), substring(old, 1, pred(text_length(old))), concat("-", old)) in
                cc_put(m, cc_value(e, text, fl_neg(field(e, value)), and(field(e, replace), not(fl_eq(field(e, value), 0.0))), cc_pending(e)))))
    fn cc_eval(a: Float, op: Text, b: Float) -> Result<Float, Text> =
        if_then_else(and(text_eq(op, "/"), fl_eq(b, 0.0)), err("Деление на ноль", Float),
            let value = if_then_else(text_eq(op, "+"), fl_add(a, b), if_then_else(text_eq(op, "-"), fl_sub(a, b), if_then_else(text_eq(op, "*"), fl_mul(a, b), fl_div(a, b)))) in
            if_then_else(fl_is_finite(value), ok(value, Text), err("Переполнение", Float)))
    fn cc_operator(m: CalcModel, op: Text) -> CalcModel =
        let e = field(m, engine) in
        if_then_else(text_length(field(e, error)) > 0, m,
            if_then_else(and(text_length(field(e, op)) > 0, field(e, ready)),
                match cc_eval(field(e, acc), field(e, op), field(e, value)) {
                    err(message) => cc_put(m, cc_failed(message)),
                    ok(value) => let next = CalcEngine(cc_format(value), value, value, op, true, false, "", 0.0, "", concat(cc_format(value), concat(" ", cc_symbol(op)))) in
                        cc_log(m, next, concat(cc_expression(field(e, acc), field(e, op), field(e, value)), concat(" = ", cc_format(value))))
                },
                let value = if_then_else(text_length(field(e, op)) > 0, field(e, acc), field(e, value)) in
                cc_put(m, CalcEngine(cc_format(value), value, value, op, true, false, "", 0.0, "", concat(cc_format(value), concat(" ", cc_symbol(op)))))))
    fn cc_equals(m: CalcModel) -> CalcModel =
        let e = field(m, engine) in
        if_then_else(text_length(field(e, error)) > 0, m,
            let pending = text_length(field(e, op)) > 0 in
            let op = if_then_else(pending, field(e, op), field(e, repeat_op)) in
            if_then_else(nat_eq(text_length(op), 0), cc_put(m, cc_value(e, cc_format(field(e, value)), field(e, value), true, concat(cc_format(field(e, value)), " ="))),
                let left = if_then_else(pending, field(e, acc), field(e, value)) in
                let right = if_then_else(pending, if_then_else(field(e, ready), field(e, value), field(e, acc)), field(e, repeat_rhs)) in
                match cc_eval(left, op, right) {
                    err(message) => cc_put(m, cc_failed(message)),
                    ok(value) => let expression = cc_expression(left, op, right) in
                        cc_log(m, CalcEngine(cc_format(value), value, value, "", true, false, op, right, "", concat(expression, " =")), concat(expression, concat(" = ", cc_format(value))))
                }))
    fn cc_unary_value(e: CalcEngine, action: Text) -> Result<Float, Text> =
        let value = field(e, value) in
        if_then_else(and(text_eq(action, "sqrt"), fl_lt(value, 0.0)), err("Корень из отрицательного числа", Float),
        if_then_else(and(text_eq(action, "recip"), fl_eq(value, 0.0)), err("Деление на ноль", Float),
            let result = if_then_else(text_eq(action, "sqrt"), fl_sqrt(value), if_then_else(text_eq(action, "square"), fl_mul(value, value),
                if_then_else(text_eq(action, "recip"), fl_div(1.0, value),
                    if_then_else(or(text_eq(field(e, op), "+"), text_eq(field(e, op), "-")), fl_mul(field(e, acc), fl_div(value, 100.0)), fl_div(value, 100.0))))) in
            if_then_else(fl_is_finite(result), ok(result, Text), err("Переполнение", Float))))
    fn cc_unary(m: CalcModel, action: Text) -> CalcModel =
        let e = field(m, engine) in
        if_then_else(text_length(field(e, error)) > 0, m,
            match cc_unary_value(e, action) {
                err(message) => cc_put(m, cc_failed(message)),
                ok(value) => let before = cc_format(field(e, value)) in
                    let expression = if_then_else(text_eq(action, "sqrt"), concat("√(", concat(before, ")")),
                        if_then_else(text_eq(action, "square"), concat("(", concat(before, ")²")),
                        if_then_else(text_eq(action, "recip"), concat("1 ÷ (", concat(before, ")")),
                            concat(before, if_then_else(or(text_eq(field(e, op), "+"), text_eq(field(e, op), "-")), concat("% от ", cc_format(field(e, acc))), "%"))))) in
                    cc_log(m, cc_value(e, cc_format(value), value, true, if_then_else(text_length(field(e, op)) > 0, cc_pending(e), expression)), concat(expression, concat(" = ", cc_format(value))))
            })
    fn cc_recall(m: CalcModel, value: Float, message: Text) -> CalcModel =
        let before = field(m, engine) in let e = if_then_else(text_length(field(before, error)) > 0, cc_engine(), before) in
        cc_put(m, cc_value(e, cc_format(value), value, true, if_then_else(text_length(field(e, op)) > 0, cc_pending(e), message)))
    fn cc_memory(m: CalcModel, action: Text) -> CalcModel =
        if_then_else(text_eq(action, "MC"), CalcModel(field(m, engine), 0.0, false, field(m, history)),
        if_then_else(text_eq(action, "MR"), if_then_else(field(m, has_memory), cc_recall(m, field(m, memory), "Из памяти"), m),
        if_then_else(text_length(field(field(m, engine), error)) > 0, m,
            let old = if_then_else(field(m, has_memory), field(m, memory), 0.0) in
            let value = field(field(m, engine), value) in
            let next = if_then_else(text_eq(action, "MS"), value, if_then_else(text_eq(action, "M+"), fl_add(old, value), fl_sub(old, value))) in
            if_then_else(fl_is_finite(next), CalcModel(field(m, engine), next, true, field(m, history)), cc_put(m, cc_failed("Переполнение памяти"))))))
    fn cc_press(m: CalcModel, key: Text) -> CalcModel =
        if_then_else(and(nat_eq(text_length(key), 1), is_some(index_of("0123456789", key))), cc_digit(m, key),
        if_then_else(or(text_eq(key, "."), text_eq(key, ",")), cc_dot(m),
        if_then_else(text_eq(key, "C"), cc_put(m, cc_engine()),
        if_then_else(text_eq(key, "CE"), cc_clear_entry(m),
        if_then_else(text_eq(key, "back"), cc_back(m),
        if_then_else(text_eq(key, "sign"), cc_sign(m),
        if_then_else(text_eq(key, "="), cc_equals(m),
        if_then_else(or(or(text_eq(key, "+"), text_eq(key, "-")), or(text_eq(key, "*"), text_eq(key, "/"))), cc_operator(m, key),
        if_then_else(or(or(text_eq(key, "sqrt"), text_eq(key, "square")), or(text_eq(key, "recip"), text_eq(key, "%"))), cc_unary(m, key),
        if_then_else(or(or(text_eq(key, "MC"), text_eq(key, "MR")), or(or(text_eq(key, "M+"), text_eq(key, "M-")), text_eq(key, "MS"))), cc_memory(m, key),
        if_then_else(text_eq(key, "history-clear"), CalcModel(field(m, engine), field(m, memory), field(m, has_memory), empty_list(CalcHistory)),
        if_then_else(and(nat_eq(text_length(key), 2), text_eq(substring(key, 0, 1), "h")),
            match index(field(m, history), unwrap_or(text_to_nat(substring(key, 1, 1)), 99)) {
                none => m, some(item) => cc_recall(m, field(item, value), "Из истории") }, m))))))))))))
    fn cc_key(m: CalcModel, code: Nat) -> CalcModel =
        if_then_else(and(code > 47, 58 > code), cc_press(m, substring("0123456789", monus(code, 48), 1)),
        if_then_else(or(nat_eq(code, 46), nat_eq(code, 44)), cc_dot(m),
        if_then_else(or(nat_eq(code, 13), or(nat_eq(code, 130829), nat_eq(code, 61))), cc_equals(m),
        if_then_else(or(nat_eq(code, 8), nat_eq(code, 130824)), cc_back(m),
        if_then_else(or(nat_eq(code, 127), nat_eq(code, 131071)), cc_clear_entry(m),
        if_then_else(or(or(nat_eq(code, 27), nat_eq(code, 130843)), or(nat_eq(code, 67), nat_eq(code, 99))), cc_put(m, cc_engine()),
        if_then_else(nat_eq(code, 43), cc_operator(m, "+"),
        if_then_else(nat_eq(code, 45), cc_operator(m, "-"),
        if_then_else(nat_eq(code, 42), cc_operator(m, "*"),
        if_then_else(nat_eq(code, 47), cc_operator(m, "/"),
        if_then_else(nat_eq(code, 37), cc_unary(m, "%"),
        if_then_else(or(nat_eq(code, 82), nat_eq(code, 114)), cc_unary(m, "sqrt"),
        if_then_else(or(nat_eq(code, 83), nat_eq(code, 115)), cc_unary(m, "square"),
        if_then_else(or(nat_eq(code, 73), nat_eq(code, 105)), cc_unary(m, "recip"),
        if_then_else(or(nat_eq(code, 78), nat_eq(code, 110)), cc_sign(m),
        if_then_else(or(nat_eq(code, 72), nat_eq(code, 104)), cc_press(m, "history-clear"), m))))))))))))))))
    fn cc_sequence(m: CalcModel, keys: Text) -> CalcModel = fold(nv_to_list(vx_codepoints(keys)), m, fun(acc: CalcModel, code: Nat, cc_key(acc, code)))
    fn cc_invariant(m: CalcModel) -> Bool =
        let e = field(m, engine) in and(and(fl_is_finite(field(e, value)), fl_is_finite(field(e, acc))),
            and(and(fl_is_finite(field(e, repeat_rhs)), fl_is_finite(field(m, memory))), not(length(field(m, history)) > 8)))
}
