module examples.add_quoted_term_as_nat

bridge PA_quote : PA -> Meta {
    kind = quote
}

theory PA {
    let n = 7
}

theory Meta {
    let code = quote(PA.n)
    let bad = code + 1
}
