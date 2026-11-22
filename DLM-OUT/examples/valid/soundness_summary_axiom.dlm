module examples.soundness_summary_axiom

bridge PA_soundness : PA -> Meta {
    kind = soundness
}

theory PA {
    let p = prove(7 > 3)
}

theory Meta {
    let lifted = soundness(PA.p)
}
