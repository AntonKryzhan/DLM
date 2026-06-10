module examples.equality_modes

bridge PA_quote : PA -> Meta {
    kind = quote
}

theory PA {
    let n = 7
    let m = 8
}

theory Meta {
    let value_same = eq_value(7, 7)
    let code_n = quote(PA.n)
    let code_m = quote(PA.m)
    let syntax_same = eq_syntax(code_n, code_n)
    let syntax_different = eq_syntax(code_n, code_m)

    print_symbolic(value_same)
    print_symbolic(syntax_same)
    print_symbolic(syntax_different)
}
