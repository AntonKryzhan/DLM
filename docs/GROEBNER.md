# Polynomial ideals over F_p, FinSet (v0.120)

Нативные (Rust `groebner.rs` = C `dvq_*`). Многочлен от `n ≤ 32` переменных — `NatVec` термов `[c, e_1..e_n]`; `p` — простое `< 2^63`; порядки: 0 lex, 1 grlex, 2 grevlex.
* `mv_add/mv_sub/mv_mul(a, b, n, p)` (результат в grevlex), `mv_normalize(a, n, p, order)`.
* `gb_basis(system, ends, n, p, order)` — приведённый базис Грёбнера (Бухбергер, нормальная стратегия, критерий взаимной простоты; монический, по убыванию старших мономов), код `[k, len_1..len_k, термы…]`.
* `gb_reduce(f, system, ends, n, p, order)` — полный остаток (делитель — первый подходящий по списку).
* Лимиты: базис ≤ 2000, работа ≤ 2·10^7 шагов → `none`.

`std.ideals` (`StdIdeals ⊃ StdTopology`): `MPoly`, `mv_var/const/term`, `mv_plus/minus/times/power/eval/text`, `Basis`, `groebner`, `normal_form`, `ideal_contains`, `is_unit_ideal`, `eliminate`.

`std.categories` (`StdCategories ⊃ StdIdeals`): `FinMap` (FinSet), `compose_maps`, `image`, моно/эпи/изо, `inverse_map`, произведения и копроизведения (`proj*`, `pairing`, `inj*`, `copairing`), `equalizer`, `coequalizer`, `pullback` (`Span`), `pushout` (`Cospan`), `characteristic`, `epi_part`/`mono_part`, `hom_count`.

Тест: `crates/dlm_core/tests/groebner.rs`.
