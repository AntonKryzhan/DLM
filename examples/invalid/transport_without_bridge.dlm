module examples.transport_without_bridge

theory PA {
    let n = 7
}

theory Meta {
    let m = transport(PA.n)
}
