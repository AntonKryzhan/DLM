# Machine floats (v0.114)

`Float` — IEEE-754 binary64, `FloatVec` — плотный вектор `Float` (≤ 2^24 элементов). Значения, не ресурсы.

* Литералы: `1.5`, `-0.25` (→ `fl_lit("…")`); `fl_lit("1e-9")`, `fl_lit("nan")`/`"inf"` — по тексту, NaN при неверном тексте; `fl_parse` → `Option<Float>`.
* Скалярные: `fl_add/sub/mul/div/neg/abs/min/max`, `fl_sqrt/exp/log/sin/cos/tan/atan/atan2/pow`, `fl_floor/ceil/round/trunc`, `fl_lt/le/eq/is_nan/is_finite`, `fl_of_nat/of_int/to_int/to_text/bits/from_bits`.
* Векторные: `fv_new/from_list/to_list/len/get/set/empty/slice/concat`, `fv_add/sub/mul/scale/axpy/dot/sum/norm/max/min`, `fv_matmul(a,b,n,m,k)`, `fv_transpose(a,r,c)`, `fv_solve(a,b,n)` (Гаусс с частичным выбором). Несовпадение размеров → `none`.
* `equal` на Float побитовый (`nan == nan`, `0.0 != -0.0`); `fl_eq` — IEEE.
* Текст: кратчайший round-trip, `1.0e300`, `-0.0`, `nan`, `inf`; одинаков в интерпретаторе и C.
* C: `double`, `-ffp-contract=off`, `-lm`. `+ - * / sqrt` совпадают побитово; трансцендентные — через libm платформы.
* `std.numerics`: bisection, newton, secant, derivative, trapezoid, simpson, rk4 (скаляр и FloatVec), golden_min, gradient_descent, horner, interpolate, mean/variance/stddev/median/covariance/correlation/linear_fit, RNG (64-бит LCG, uniform, range, Box–Muller), solve_linear, residual_norm.
* Тест: `crates/dlm_core/tests/floats.rs`.
