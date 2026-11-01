module examples.statement_must_be_pure

theory T {
    fn input() -> Nat = read_nat()
    theorem depends_on_input: nat_eq(input(), 1)
}
