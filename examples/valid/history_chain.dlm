module examples.history_chain

bridge PA_quote : PA -> Meta {
    kind = quote
}

bridge PA_soundness : PA -> Meta {
    kind = soundness
}

theory PA {
    let n = 7
    let p = prove(n > 0)
}

theory Meta {
    let code = quote(PA.n)
    let lifted = soundness(PA.p)
}
