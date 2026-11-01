# SAT, integral homology, persistence (v0.119)

Нативные (Rust `combinatorics.rs` = C `dvc_*`):
* `sat_solve(lits, ends, nvars)` → модель `NatVec` (0/1) или `none`; `sat_status` → 0 SAT, 1 UNSAT, 2 некорректный ввод, 3 лимит (2·10^6 конфликтов). Литерал `2v` = `x_v`, `2v+1` = `¬x_v`; клауза `i` = `lits[ends[i-1]..ends[i]]`. CDCL: наблюдаемые литералы, 1UIP, VSIDS, сохранение фазы; детерминирован.
* `tp_homology(faces, ends)` — гомологии замыкания симплициального комплекса над Z (нормальная форма Смита в i128, `none` при переполнении/лимитах): `[top+1, (β_d, k_d, t_1..t_k)…]`.
* `tp_rips(points, dim, maxdim ≤ 2, radius)` — баркод Виеториса–Рипса над F₂: тройки `(d, birth, death)`, `inf` для бессмертных классов.

`std.logic` (`StdLogic ⊃ StdNumberTheory`): `Cnf`, `pos/neg/negate`, `add_clause`, `at_most_one/exactly_one/implies/equivalent`, вентили Цейтина `gate_and/or/xor`, `sat_model`, `satisfies`, `queens`, `sudoku`, `colouring`, `pigeonhole`.

`std.topology` (`StdTopology ⊃ StdLogic`): `Complex`, `homology` (`HomologyGroup`), `betti`, `euler_characteristic`, `sphere`, `ball`, `circle`, `torus`, `projective_plane`, `wedge_of_circles`, `rips_barcode` (`Bar`), `betti_at`, `significant`.

Тест: `crates/dlm_core/tests/logic_topology.rs`.
