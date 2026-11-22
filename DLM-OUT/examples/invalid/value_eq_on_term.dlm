module examples.value_eq_on_term

bridge PA_quote : PA -> Meta {
    kind = quote
}

theory PA {
    let n = 7
}

theory Meta {
    let code = quote(PA.n)
    let bad = eq_value(code, code)
}
