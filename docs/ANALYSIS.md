# Analysis and optimization (v0.117)

Нативные (Rust `spectral.rs` = C `dva_*`):
* `fft_forward(x)`, `fft_inverse(x)` — комплексный вектор чередованием `[re0, im0, …]`; степень двойки — radix-2, иначе прямое ДПФ (≤ 8192 точек); обратное делит на `n`.
* `fft_convolve(a, b)` — линейная свёртка вещественных последовательностей.
* `lp_solve(c, A, b, m, n)` / `lp_status(…)` — максимум `c·x` при `A x ≤ b`, `x ≥ 0`; двухфазный симплекс, правило Бленда; статус 0 оптимум, 1 несовместна, 2 неограничена, 3 некорректный ввод, 4 лимит итераций.

`std.analysis` (`StdAnalysis ⊃ StdLinalg`): дуальные числа `Dual` (AD вперёд: `d_*`, `ad_derivative`, `ad_newton`), `gradient`, `jacobian`, Гаусс–Лежандр (`integrate`), адаптивный Симпсон, Дорманд–Принс 5(4) (`ode_solve`), натуральный кубический сплайн, многочлены (`poly_*`, корни Дюрана–Кернера), `Cx`, `power_spectrum`, `convolve`.

`std.optimize` (`StdOptimize ⊃ StdAnalysis`): `nelder_mead`, `bfgs`/`bfgs_numeric` (Армихо), `levenberg_marquardt`, `linear_program` (`LpResult`).

Тест: `crates/dlm_core/tests/analysis.rs`.
