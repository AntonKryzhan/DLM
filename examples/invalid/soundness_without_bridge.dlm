module examples.soundness_without_bridge

theory PA {
    let p = prove(7 > 0)
}

theory Meta {
    let lifted = soundness(PA.p)
}
