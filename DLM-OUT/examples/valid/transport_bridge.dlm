module examples.transport_bridge

theory PA {
    let n = 7
}

theory Meta {
    let m = transport(PA.n)
    print_decimal(m)
}

bridge PA_to_Meta : PA -> Meta {
    kind = transport
}
