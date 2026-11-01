# Scalar witnesses (N15), 0.106.0

Host `math::scalar_witness`, source `lib/std/scalar_witness.dlm` (`std.scalar_witness`). One line:

```text
dlmw1|<kind>|<field>|...|<digest>
```

Values are canonical portable texts (`dlm1:...`), parameters canonical decimals, the digest is a polynomial hash
mod 2^61 - 1 (base 257) over every byte before it — transport integrity only, not authentication. Characters:
printable ASCII without `"` and backslash, at most 4096.

| kind | fields | check | trust |
|---|---|---|---|
| `quotient` | a, c, q | c ≠ 0, q·c = a | checked |
| `round` | q, mode, n | inequalities on q − n per mode (half_even: tie ⇒ n even) | checked |
| `decimal` | q, digits, mode, text | text canonical; round claim on q·10^d with n = text·10^d | checked |
| `sqrt` | x, k, [lo,hi] | x ≥ 0, lo ≤ 0 or lo² ≤ x, hi ≥ 0 and x ≤ hi², (hi − lo)·2^k ≤ 2 | checked |
| `enclosure` | creal, n, [lo,hi] | recomputed `approx(n)` equals the interval | replayed |

`checked` uses identities/inequalities only (not the producing algorithm); `replayed` reruns it. Refusals
(`invalid_input`): `invalid scalar witness`, `unsupported scalar witness version`, `unknown scalar witness kind`,
`scalar witness digest mismatch`, `scalar witness claim rejected: <kind>`; budget errors stay `budget`.
Producers: `produce_*` / `sw_quotient`, `sw_round`, `sw_decimal`, `sw_sqrt`, `sw_enclosure`; checker `check` /
`sw_check` (`"<trust> <kind>"`). Tests (`tests/scalar_witness.rs`): host and source produce identical lines for
36 claims; every single-character edit is refused; false claims with a correct digest, malformed lines and foreign
versions get identical messages in host, interpreter and native C.
