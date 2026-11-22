# SAT certificates, model counting, integer programming (v0.121)

Нативные (Rust = C):
* `sat_proof(lits, ends, nvars)` — для невыполнимой формулы последовательность выученных клауз CDCL `[k, len_1, lits…, …]`; иначе `none`.
* `sat_check(lits, ends, nvars, proof)` — независимая от решателя проверка RUP (обратное распространение единичных клауз по исходным и предыдущим клаузам; в конце — пустая клауза). `some(true/false)`, `none` на некорректном вводе или лимите работы (2^32).
* `sat_count(lits, ends, nvars)` — точное число моделей (DPLL с распространением, ≤ 10^6 узлов, `none` при переполнении u128).
* `ilp_solve/ilp_status(c, A, b, m, n, integer)` — максимум `c·x` при `A x ≤ b`, `x ≥ 0`, `x_j ∈ Z` где `integer[j] ≠ 0`; ветви и границы поверх симплекса (первая дробная переменная, сначала ветвь `≤ ⌊x⌋`, допуск 1e-6, ≤ 10^4 узлов). Статусы как у `lp_status`.

`std.logic`: `refutation`, `check_refutation`, `verified_unsat`, `model_count`, `entails`. `std.optimize`: `integer_program`, `all_integer`, `knapsack`.

Тест: `crates/dlm_core/tests/proofs_ilp.rs`.
