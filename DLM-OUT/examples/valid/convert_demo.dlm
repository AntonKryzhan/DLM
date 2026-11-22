module examples.convert_demo
import std.convert

// Explicit numeric conversions (rounding modes, decimals, computable reals) and portable numeric texts.
theory App extends StdConvert {
    let b = ExactBudget(512, 1000000000000000)
    fn q(t: Text) -> BigRational = match bigrat_parse(b, t) { err(e) => BigRational(false, empty_list(Nat), list(1)), ok(v) => field(v, value) }
    fn x(t: Text) -> CReal = match creal_parse(b, t) { err(e) => CReal(empty_list(CRealNode)), ok(v) => field(v, value) }
    fn it(r: Result<BigIntCalc, Text>) -> Text = match r { err(e) => e, ok(v) => match big_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } }
    fn tt(r: Result<BigTextCalc, Text>) -> Text = match r { err(e) => e, ok(v) => field(v, value) }
    fn qt(r: Result<BigRationalCalc, Text>) -> Text = match r { err(e) => e, ok(v) => tt(bigrat_text(field(v, budget), field(v, value))) }
    print_line(concat("round -5/2: ", concat(it(cv_round(b, q("-5/2"), 0)), concat(" ", concat(it(cv_round(b, q("-5/2"), 1)), concat(" ", concat(it(cv_round(b, q("-5/2"), 2)), concat(" ", it(cv_round(b, q("-5/2"), 3))))))))))
    print_line(concat("22/7 to 5 digits: ", tt(cv_decimal(b, q("22/7"), 5, 3))))
    print_line(concat("-1/8 exactly: ", tt(cv_decimal_exact(b, q("-1/8")))))
    print_line(concat("1/3 exactly: ", tt(cv_decimal_exact(b, q("1/3")))))
    print_line(concat("parse -2.50e1: ", qt(cv_parse_decimal(b, "-2.50e1"))))
    print_line(concat("pi to 10 digits: ", tt(cv_creal_decimal(b, x("pi"), 10, 3, 64))))
    print_line(concat("floor(e): ", it(cv_creal_round(b, x("c 1 ; exp 0"), 0, 64))))
    print_line(concat("floor(sqrt2 - sqrt2): ", it(cv_creal_round(b, x("c 2 ; sqrt 0 ; sub 1 1"), 0, 64))))
    print_line(tt(pn_rat(b, q("-6/8"))))
    print_line(concat("dlm1:rat:6/8 -> ", pn_category(b, "dlm1:rat:6/8")))
    print_line(concat("dlm2:int:1 -> ", pn_category(b, "dlm2:int:1")))
}
