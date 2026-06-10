module examples.soundness_research

theory PA {
    let p = prove(7 > 0)
}

theory Meta {
    let lifted = soundness(PA.p)
}

bridge PA_soundness : PA -> Meta {
    kind = soundness
}
