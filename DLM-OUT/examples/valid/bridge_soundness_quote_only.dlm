module examples.bridge_soundness_quote_only

bridge PA_quote : PA -> Meta {
    kind = quote
}

theory PA {
    let n = 7
}

theory Meta {
    let code = quote(PA.n)
    let ast = inspect_ast(code)
    print_text(ast)
}
