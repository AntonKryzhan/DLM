module examples.no_theory_bridge

theory PA {
    let n = 7
}

theory Meta {
    let copied = PA.n
}
