# Numerical linear algebra (v0.116)

Матрицы — `FloatVec` по строкам. Нативные `la_*` (Rust `linalg.rs` = C `dvl_*`, одинаковые последовательности операций без FMA, результаты побитово совпадают):

* `la_lu(a,n)` (упакованные L\U, частичный выбор), `la_lu_perm`, `la_det` (вырожденная → `0.0`), `la_inverse`;
* `la_cholesky(a,n)` (нижняя L, `none` если не положительно определена);
* `la_qr_q(a,m,n)` / `la_qr_r` (тонкий QR Хаусхолдера, `m ≥ n`), `la_lstsq(a,b,m,n)`;
* `la_eigh_values/vectors(a,n)` (циклический Якоби, только симметричные, по возрастанию, векторы — столбцы);
* `la_svd_values(a,m,n)` (односторонний Якоби, по убыванию), `la_rank(a,m,n,tol)`;
* `la_identity`, `la_trace`;
* CSR: `la_csr_matvec(rowptr, index, values, x)`, `la_cg(rowptr, index, values, b, tol, iterations)` (сопряжённые градиенты, `none` без сходимости или если не SPD).

`std.linalg` (`StdLinalg ⊃ StdGraphs ⊃ …`): `Matrix`, `LU`, `QR`, `Eigen`, `Csr`, `Triplet`; арифметика, `det/inverse/solve/lu/cholesky/qr/least_squares/eigh/singular_values/rank/condition/spectral_norm/matrix_power/dominant_eigenvalue`, CSR из триплетов, `laplacian_1d`.

Аргумент типа в `none`/`empty_list` для составных типов пишется `List(Nat)`, `Option(Nat)`.

Тест: `crates/dlm_core/tests/linalg.rs`.
