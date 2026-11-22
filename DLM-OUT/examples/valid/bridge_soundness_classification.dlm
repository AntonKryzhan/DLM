module examples.bridge_soundness_classification

bridge PA_def : PA -> Meta {
    kind = definitional
}

bridge PA_cons : PA -> Meta {
    kind = conservative
}

bridge PA_quote : PA -> Meta {
    kind = quote
}

bridge PA_transport : PA -> Meta {
    kind = transport
}

bridge PA_soundness : PA -> Meta {
    kind = soundness
}

bridge PA_reflection : PA -> Meta {
    kind = reflection
}

theory PA {
    let n = 7
    let p = prove(n > 0)
}

theory Meta {
    let code = quote(PA.n)
    let moved = transport(PA.n)
    let lifted = soundness(PA.p)
}
