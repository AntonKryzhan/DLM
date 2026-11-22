module examples.case_branch_types_differ

theory T {
    fn keep(n: Nat) -> Nat = n
    fn flag(b: Bool) -> Bool = b
    let r = case(inl(1, Bool), keep, flag)
}
