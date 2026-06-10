module examples.definability_passport

theory Meta {
    let lang = language_L0()
    let enc = encoding_godel()
    let meta = meta_level(1)
    let d = definable_nat(lang, enc, 20, meta)
    let bound = definability_bound(d)
    let level = definability_meta_level(d)

    print_symbolic(d)
    print_decimal(bound)
    print_decimal(level)
}
