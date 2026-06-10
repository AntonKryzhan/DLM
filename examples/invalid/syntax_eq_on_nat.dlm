module examples.syntax_eq_on_nat

theory Core {
    let a = 7
    let b = 7
    let same = eq_syntax(a, b)
}
