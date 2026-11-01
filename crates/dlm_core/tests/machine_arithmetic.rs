use dlm_core::{check_machine_arithmetic, parse_module, CheckPolicy, Program, Runtime};

fn program(body: &str) -> Program {
    Program::single(parse_module(&format!("module m\ntheory T {{\n{body}\n}}\n")).unwrap())
}

fn verified(body: &str) -> bool {
    check_machine_arithmetic(&program(body), CheckPolicy::trusted_only()).unwrap().verified()
}

#[test]
fn mathematical_theorems_do_not_discharge_machine_obligations() {
    let p = program("    fn double(n: Nat) -> Nat = n + n\n    theorem double_is_twice(n: Nat): nat_eq(double(n), mul(2, n)) by normalize");
    let report = check_machine_arithmetic(&p, CheckPolicy::trusted_only()).unwrap();
    assert!(!report.verified());
    assert_eq!(report.obligations.len(), 1);
    assert!(report.render().contains("open: T.double: (%0 + %0)"));
    // Bounded input to one caller does not certify all exported invocations.
    assert!(!verified("    fn inc(n: Nat) -> Nat = n + 1\n    print_symbolic(inc(2))"));
}

#[test]
fn safe_full_domain_arithmetic_and_guarded_bounds_are_proved() {
    for body in [
        "    fn same(n: Nat) -> Nat = n + 0",
        "    fn zero(n: Int) -> Int = mul(n, int(0))",
        "    fn quotient(n: Int) -> Int = div(n, int(1))",
        "    fn succ(n: Nat) -> Nat = if_then_else(n > 1000, n, n + 1)",
        "    fn inc_negative(i: Int) -> Int = if_then_else(i > -2, i, i + int(1))",
        "    fn negate(i: Int) -> Int = if_then_else(i > -170141183460469231731687303715884105728, mul(i, -1), int(0))",
        "    print_symbolic(2 ^ 127)\n    print_symbolic(int(13) - -9)",
    ] {
        let p = program(body);
        let report = check_machine_arithmetic(&p, CheckPolicy::trusted_only()).unwrap();
        assert!(report.verified(), "{body}\n{}", report.render());
    }
}

#[test]
fn exact_boundaries_and_intermediate_overflow_are_not_hidden() {
    for expr in [
        "340282366920938463463374607431768211455 + 1",
        "mul(170141183460469231731687303715884105728, 2)",
        "monus(340282366920938463463374607431768211455 + 1, 1)",
        "int(340282366920938463463374607431768211455)",
        "-(-170141183460469231731687303715884105728)",
        "-170141183460469231731687303715884105728 - int(1)",
        "div(-170141183460469231731687303715884105728, -1)",
        "mod(-170141183460469231731687303715884105728, -1)",
        "2 ^ 128",
        "1 ^ 4294967296",
    ] {
        assert!(!verified(&format!("    print_symbolic({expr})")), "{expr}");
    }
    assert!(verified("    print_symbolic(340282366920938463463374607431768211455 + 0)"));
    assert!(verified("    print_symbolic(mul(-170141183460469231731687303715884105728, int(1)))"));
    assert!(verified("    print_symbolic(div(7, 0))\n    print_symbolic(mod(-7, int(0)))"));
}

#[test]
fn unknown_calls_captures_and_recursive_results_are_conservative() {
    assert!(!verified("    fn n(x: Nat) -> Nat = x\n    print_symbolic(n(0) + 1)"));
    assert!(!verified("    fn f(xs: List<Nat>) -> List<Nat> = map(xs, fun(x: Nat, x + 1))"));
    assert!(!verified("    fn count(n: Nat) -> Nat decreasing n = match n { 0 => 0, succ(k) => count(k) + 1 }"));
    assert!(!verified("    fn f(r: Result<Nat, Text>) -> Nat = unwrap_or(r, 0) + 1"));
}

#[test]
fn closed_arithmetic_report_is_checked_against_runtime_at_boundaries() {
    let naturals = [0, 1, 2, (1u128 << 62) - 1, 1u128 << 62, u128::MAX / 2, u128::MAX];
    let integers = [i128::MIN, i128::MIN + 1, -(1i128 << 62) - 1, -(1i128 << 62), -1, 0, 1, (1i128 << 62) - 1, 1i128 << 62, i128::MAX];
    let lit = |i: i128| if i < 0 { i.to_string() } else { format!("int({i})") };
    let mut expressions = Vec::new();
    for a in naturals {
        for b in naturals {
            expressions.extend([format!("{a} + {b}"), format!("mul({a}, {b})"), format!("div({a}, {b})"), format!("mod({a}, {b})")]);
        }
    }
    for a in integers {
        for b in integers {
            let (a, b) = (lit(a), lit(b));
            expressions.extend([format!("{a} + {b}"), format!("{a} - {b}"), format!("mul({a}, {b})"), format!("div({a}, {b})"), format!("mod({a}, {b})")]);
        }
    }
    for expr in expressions {
        let p = program(&format!("    print_symbolic({expr})"));
        let report = check_machine_arithmetic(&p, CheckPolicy::trusted_only()).unwrap();
        let result = Runtime::new().run_program(&p);
        // Closed constants are exact intervals: both directions must agree.
        assert_eq!(report.verified(), result.is_ok(), "{expr}\n{}\n{result:?}", report.render());
    }
}
