module examples.unsafe_bridge_profile

bridge PA_bad : PA -> Meta {
    kind = unsafe
}

theory PA {
    let n = 7
}

theory Meta {
    let m = 1
}
