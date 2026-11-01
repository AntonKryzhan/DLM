# Complex numbers with computable real components (0.103.0)

Closes N09. `z = re + i·im`, both components computable-real programs (COMPUTABLE_REALS.md). Host
`math::ccomplex::CComplex`, ordinary DLM `std.ccomplex` (`record CComplex(re: CReal, im: CReal)`).

| Operation | Host | DLM | Construction of the component programs |
|---|---|---|---|
| text | `parse`, `Display` | `cc_parse`, `cc_text` | `<re> \| <im>` (tokens separated by single spaces) |
| validate / from real | `new`, `from_real` | `cc_check`, `cc_from_real` | — |
| add, sub, neg, conj | `checked_add/sub/neg`, `conj` | `cc_add/sub/neg/conj` | componentwise; `conj` keeps `re` and negates `im` |
| mul | `checked_mul` | `cc_mul` | `[a, b, c, d]`, `ac − bd` / `[a, b, c, d]`, `ad + bc` |
| inv | `checked_inv` | `cc_inv` | `[a, b]`, `a/(a² + b²)` / `[a, b]`, `−(b/(a² + b²))` |
| div | `checked_div` | `cc_div` | `[a, b, c, d]`, `(ac + bd)/(c² + d²)` / `(bc − ad)/(c² + d²)` |
| \|z\|², \|z\| | `abs_sq`, `abs` | `cc_abs_sq`, `cc_abs` | `[a, b]`, `a² + b²` (then `sqrt`) |
| approximation | `approx(n)` | `cc_approx` | both components, width ≤ 2^−n each |
| equality | `equals(w, n)` | `cc_equal` | 1 apart (a component disjoint), 0 the same exact point, 2 undecided |

Inputs are embedded in the listed order (operand indices shifted), operations follow in the order written; the
host and the source library build identical programs. Composed programs keep the 64-node limit. Domain errors
and UnknownWithinBudget are those of N08 (zero is a proved division by zero, `z` with an undecided zero part is
unknown).

Evidence: `tools/generate_ccomplex103_oracle.py` builds 497 requests (8 named numbers with all unary/binary
operations, identities `i² = −1`, `|3 − 4i| = 5`, undecided `√2·√2`, 150 random) and checks every result
against complex arithmetic on 900-digit Decimal values of the operands. Host: identical program texts for all
rows, identical intervals at the minimal precision, 495 precision boundaries. DLM interpreter and native C: 74
requests, 56 boundaries, program texts equal to the host; raw records, text errors, privacy; `ccomplex_demo.dlm`.
