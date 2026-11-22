module examples.quote_bridge

bridge Core_quote : Core -> Meta {
    kind = quote
}

theory Core {
    let n = 7
}

theory Meta {
    let code = quote(Core.n)
}
