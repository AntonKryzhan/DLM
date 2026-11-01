module examples.symbolic_proof_requires_nat_variables

theory T {
    theorem flags(bs: List<Bool>): nat_eq(length(bs), length(bs)) by normalize
}
