# Elementary functions of computable reals (0.104.0)

Closes N10. New program nodes `exp a`, `log a`, `sin a`, `cos a` and the constant `pi` (codes 10–14 in
`std.creal`); host `math::elementary`, the same algorithm in `std.creal` (`el_*`). Each refinement round
evaluates the nodes at resolution `w = k`; every intermediate interval is rounded outward to multiples of `2^-p`
(`rnd`: floor/ceiling of `x·2^p`), so sizes stay bounded and only the stated tail bounds matter.

| Node | Enclosure at resolution `w` |
|---|---|
| `exp` | increasing: `[exp(lo), exp(hi)]`; for a point `x > 0`: `s = max(0, bits(num) − bits(den) + 2)`, `y = x/2^s ≤ 1/2` rounded outward at `wp = w + s + 8`, Taylor terms `t_i = t_{i−1}·y/i` until `t_i ≤ 2^-wp`, tail `≤ t_i`, then `s` squarings; `exp(−x) = 1/exp(x)` |
| `log` | `hi ≤ 0`: error `computable real logarithm of a non-positive value`; `lo ≤ 0 < hi`: retry; else `[log(lo), log(hi)]` with `x = 2^m·y`, `1 ≤ y < 2`, `log y = 2 atanh((y−1)/(y+1))`, `log 2 = 2 atanh(1/3)`, odd series with tail `≤` the last term, `wp = w + bits(|m|) + 8` |
| `pi` | `16 atan(1/5) − 4 atan(1/239)`, alternating series, remainder `≤` the last term, `wp = w + 8` |
| `sin`, `cos` | point `|c| ≤ 4`: alternating Taylor series (stop at `k ≥ 2`, remainder `≤` the last term); larger `|c|`: `c − 2πj`, `j = floor(c/(2π_lo) + 1/2)` with π at `w + e + 8`; non-point `X`: `f(mid) ± max(hi − mid, mid − lo)` (Lipschitz 1), clipped to `[−1, 1]` |

Correctness follows from the inclusion property of the interval operations, monotonicity of `exp`/`log`, the
mean value theorem for `sin`/`cos` and the stated series remainders; the N08 schedule doubles `w` until the
width is at most `2^-n`. Results remain runtime values.

Evidence: `tools/generate_elementary104_oracle.py` runs the same algorithm in Python `Fraction` and checks every
result against mpmath (1000 digits) and the width; 263 fixtures (π, e, log 2, sin/cos with and without
reduction, compositions, domain errors, an undecided logarithm, 220 random programs). The host reproduces all of
them exactly; DLM interpreter and native C reproduce 32 plus a precision failure; identities (`exp(log 2) = 2`,
`sin² + cos² = 1`, `cos π = −1`, `sin(π/6) = 1/2`) hold within the bounds; `elementary_demo.dlm`.
