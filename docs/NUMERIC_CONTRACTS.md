# Error and budget contracts of the scalar numeric APIs (N13), 0.106.0

Host `math::contract`, source `lib/std/numeric.dlm` (`std.numeric`, theory `StdNumeric`).

**Categories** (`classify(message)` / `nm_code`, `nm_category`): `domain` (0: undefined value — division by
zero, sqrt/log outside the domain), `unrepresentable` (1: the target cannot hold the exact value), `invalid_input`
(2: malformed, non-canonical, unknown name, invalid budget, rejected witness), `limit` (3: fixed documented limit),
`budget` (4: `UnknownWithinBudget` work/precision/input exhaustion), `undecided` (5: enclosures do not decide).
Only `budget` and `undecided` are retryable (`retryable`, `nm_retryable`). `MESSAGES` is the catalogue of every
refusal of exact, interval, complex, computable-real, elementary, conversion, portable and witness APIs, host (`h`)
and source (`s`); a test scans the host sources and `lib/std` and fails on any uncatalogued refusal literal.

**Budget.** `metered`, `chain` (steps on one budget, stops at the first refusal), `required_work`,
`check_threshold`: with exactly the required work an operation gives the same answer and leaves 0; with one unit
less it is refused with `budget` and an exhausted budget is left empty. Source: `nm_work`, `nm_with_work`. Units
differ (host: limb operations, source: reservation units); the contracts are identical and tested in both
(`tests/numeric_contract.rs`: 12 host and 10 source operations including domain and undecided refusals, chains).
