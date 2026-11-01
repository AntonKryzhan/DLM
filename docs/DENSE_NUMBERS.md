# Dense numbers and the cost profile (N14), 0.107.0

**`BigNat`** — new ordinary type: immutable, copyable natural number in canonical little-endian 32-bit limbs,
at most 2^20 bits (`dense_nat::MAX_BITS`). Interpreter: `crate::dense_nat`; native C: `DV_BIGNAT`, `dvn_*`
(region-allocated limbs, the same algorithms: schoolbook product, Knuth algorithm D, Euclid, Newton sqrt).
Builtins (pure, total; `none` for a zero divisor, invalid input or a result over the limit):

`bn_from_nat`, `bn_to_nat`, `bn_from_words` (base-65536 words of `std.exact`), `bn_to_words`, `bn_from_text`,
`bn_to_text`, `bn_bits`, `bn_limbs`, `bn_cmp`, `bn_add`, `bn_monus`, `bn_mul`, `bn_div`, `bn_mod`, `bn_shl`,
`bn_shr`, `bn_gcd`, `bn_sqrt`.

**`std.exact`** keeps `List<Nat>` storage and its work units, but add/sub/mul/div/cmp, decimal input/output and
integer square root run on the dense kernel (`bx_d*`); the list algorithms remain as reference and fallback.
Values, refusals and budgets are unchanged; every source module above it (interval, complex, creal, elementary,
convert, witness) gets the native speed.

**`std.dense`** — dense storage: `DInt(negative, magnitude: BigNat)`, `dz_from_big`, `dz_to_big`, `dz_parse`,
`dz_text`, `dz_add`, `dz_sub`, `dz_mul`, `dz_cmp`, `dz_div_rem` (std.exact semantics), `dz_gcd`, `dz_sqrt`,
`dz_pow`, `dz_limbs`. Same precision limits and messages as `std.exact`; work in dense units
(`1 + limbs·limbs` for products/quotients, `1 + max limbs` otherwise).

**Profile** (`math::profile`): seven fixed workloads (product, division, gcd, sqrt, decimal output, decimal
input, power, up to 16384 bits) with reproducible host work, result limbs and results; `THRESHOLDS` bound host
work, memory (limbs) and `std.dense` work. `DLM_DENSE_TRACE=1` prints native dense values, limbs and the
largest value. Tests: `dense_profile.rs` (host reproducibility and identities; std.dense and std.exact in the
interpreter and native C equal to the host, within thresholds, native peak ≤ 1025 limbs), `dense_nat` unit tests.
