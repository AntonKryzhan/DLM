// Uniform error and budget contracts of the scalar numeric modules (N13), the host's math::contract.
// Categories: 0 domain, 1 unrepresentable, 2 invalid_input, 3 limit, 4 budget, 5 undecided, 6 unclassified.
// See docs/NUMERIC_CONTRACTS.md.
module std.numeric
import std.convert

theory StdNumeric extends StdConvert {
    private fn nm_at(n: Nat, s: Text, p: Text, i: Nat) -> Bool decreasing n fuel 4096 =
        if_then_else(nat_eq(n, 0), false, or(text_eq(substring(s, i, text_length(p)), p), nm_at(pred(n), s, p, i + 1)))
    fn nm_contains(s: Text, p: Text) -> Bool = if_then_else(text_length(p) > text_length(s), false, nm_at(monus(text_length(s), text_length(p)) + 1, s, p, 0))
    fn nm_starts(s: Text, p: Text) -> Bool = text_eq(substring(s, 0, text_length(p)), p)
    private fn nm_any(s: Text, ps: List<Text>) -> Bool = fold(ps, false, fun(acc: Bool, p: Text, or(acc, nm_contains(s, p))))
    fn nm_code(e: Text) -> Nat =
        if_then_else(or(nm_starts(e, "UnknownWithinBudget: rounding undecided"), nm_starts(e, "UnknownWithinBudget: computable real refinement limit reached")), 5,
        if_then_else(nm_starts(e, "UnknownWithinBudget: "), 4,
        if_then_else(nm_any(e, list("invalid ", "non-canonical ", "noncanonical ", "unknown ", "unsupported ", "portable number is ", "out of order", "denominator is zero",
            "must carry 0", "must refer to an earlier node", "unused operands must be 0", "must be 1..16384 bits", "witness digest mismatch", "witness claim rejected")), 2,
        if_then_else(nm_any(e, list("division by zero", "square root of negative", "square root of a negative", "logarithm of a non-positive", "interval containing zero", "negative natural subtraction")), 0,
        if_then_else(nm_any(e, list("is not an integer", "is not a finite decimal", "does not fit in ")), 1,
        if_then_else(nm_any(e, list("exceeds 4096", "exceeds 16384", "exponent out of range", "must have 1..64 nodes")), 3, 6))))))
    fn nm_name(code: Nat) -> Text = unwrap_or(index(list("domain", "unrepresentable", "invalid_input", "limit", "budget", "undecided"), code), "unclassified")
    fn nm_category(e: Text) -> Text = nm_name(nm_code(e))
    // Retrying with more work or precision may succeed only for budget and undecided refusals.
    fn nm_retryable(e: Text) -> Bool = or(nat_eq(nm_code(e), 4), nat_eq(nm_code(e), 5))
    // Work consumed between two budgets of one chain (source units).
    fn nm_work(before: ExactBudget, after: ExactBudget) -> Nat = monus(field(before, work), field(after, work))
    fn nm_with_work(b: ExactBudget, work: Nat) -> ExactBudget = ExactBudget(field(b, bits), work)

    // Closed finite computations of the classification, not general contract proofs.
    theorem budget_is_retryable: nm_retryable("UnknownWithinBudget: source work reservation exhausted")
    theorem domain_is_classified: text_eq(nm_category("computable real division by zero"), "domain")
}
