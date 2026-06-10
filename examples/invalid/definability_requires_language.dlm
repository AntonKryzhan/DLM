module examples.definability_requires_language

theory Meta {
    let enc = encoding_godel()
    let meta = meta_level(1)
    let d = definable_nat(7, enc, 20, meta)
}
