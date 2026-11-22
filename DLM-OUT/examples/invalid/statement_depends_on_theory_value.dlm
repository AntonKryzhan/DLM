module examples.statement_depends_on_theory_value

theory T {
    let x = 5
    theorem about_x: nat_eq(x, 5)
}
