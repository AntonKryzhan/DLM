module examples.quote_without_bridge

theory PA {
    let n = 7
}

theory Meta {
    let code = quote(PA.n)
}
