module examples.theorem_is_not_an_ordinary_value

theory T {
    theorem trivial: true
    let boxed = some(trivial)
}
