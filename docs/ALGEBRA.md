# Number theory, F_p[x], permutation groups (v0.118)

Нативные (Rust `number.rs` = C `dvt_*`), точные на `Nat` (u128):
* `nt_gcd`, `nt_lcm`, `nt_mod_mul`, `nt_mod_pow`, `nt_mod_inv`, `nt_crt` (не обязательно взаимно простые модули), `nt_sqrt_mod` (Тонелли–Шенкс, меньший корень), `nt_isqrt`;
* `nt_is_prime` (Миллер–Рабин, 20 простых оснований; детерминирован < 3.3·10^24), `nt_next_prime`, `nt_primes(limit)` (решето, ≤ 2^24);
* `nt_factor` (пробное деление + Брент–Поллард с фиксированным бюджетом: множители примерно до 2^40, иначе `none`), `nt_totient`, `nt_divisors`.
* `F_p[x]` (коэффициенты по возрастанию, `p` простое `< 2^64`, степень ≤ 8191): `pf_add/sub/mul/div/rem/gcd` (монический), `pf_powmod`, `pf_eval`, `pf_irreducible` (тест Рабина).
* Перестановки (`p[i]`, `(p∘q)[i] = p[q[i]]`, n ≤ 256, ≤ 64 образующих): `pm_compose/inverse/power/order/sign/cycle_type`, `pm_orbit`, `pm_group_order` (детерминированный Шрайер–Симс; `none` при переполнении u128 или лимите работы).

`std.number_theory` (`StdNumberTheory ⊃ StdOptimize`): `Zmod`, `sigma`, `mobius`, `primitive_root`, `discrete_log`, RSA (`RsaKey`), `irreducible(p,k)`, `Gf` (GF(p^k): `gf_add/sub/mul/pow/inv/div`), `PermGroup` (`symmetric/alternating/cyclic/dihedral_group`, `group_order`, `orbit`, `is_transitive`).

Тест: `crates/dlm_core/tests/number.rs`.
