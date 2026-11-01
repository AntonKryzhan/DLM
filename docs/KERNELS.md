# Sparse solvers, robust geometry, signals, statistics (v0.123)

Нативные (Rust = C, одинаковые последовательности операций):
* Разреженные (CSR): `la_pcg(rowptr, idx, vals, b, tol, iters)` — CG с предобуславливателем Якоби (диагональ > 0); `la_bicgstab(…)`; `la_gmres(…, tol, restart ≤ 1024, iters)` — GMRES(m) с вращениями Гивенса. `none` при пробое или без сходимости.
* Геометрия: `geo_orient2d(6 чисел)`, `geo_incircle(8)`, `geo_orient3d(12)` — точный знак детерминанта (разложения Шевчука, без FMA), 0/1/2; координаты 0 или |x| ∈ [2^-150, 2^150]. `geo_delaunay(xy)` — заметание + флипы Лоусона с точным incircle; треугольники ccw, канонически упорядочены, повторы точек отброшены. `kd_knn(points, queries, d, k)` (по (расстояние², индекс)), `kd_radius(points, d, center, r)`.
* Сигналы: `fft2_forward/inverse(x, rows, cols)`, `sig_stft(x, window, hop)` (мощности кадров × (w/2+1)), `sig_dwt/sig_idwt(x, levels, 0 Haar | 1 Daubechies-4)` (периодические), `sig_iir(b, a, x)`, `sig_sha256(text)`.

Библиотеки (цепочка `StdSpatial ⊃ StdStatistics ⊃ StdSignal ⊃ StdMaps`):
* `std.linalg`: `preconditioned_cg`, `bicgstab`, `gmres`, `csr_residual`, `laplacian_2d`, `equilibrate`.
* `std.signal`: окна `hann/hamming/blackman`, `fir_lowpass`, `fir`, биквады RBJ (`lowpass/highpass/bandpass/notch`, `Biquad`, `gain`, `filter_cascade`), `spectrogram`, `wavelet_denoise`, `spectrum_2d`, `moving_average`, `snr_db`, `digest`.
* `std.statistics`: `column_means`, `covariance_matrix`, `correlation_matrix`, `pca`/`project`/`explained_variance`, `mvn_samples`, `mahalanobis2`; сопряжённые апостериорные Beta и Normal, `credible_interval`; `metropolis` (`Chain`); временные ряды `acf`, `ar_fit`/`ar_forecast` (Юла–Уокер), `difference`, `exponential_smoothing`, `ljung_box`.
* `std.spatial`: `exact_orientation`, `exact_in_circle`, `delaunay_mesh` (`Mesh`), `mesh_area`, `edge_use`, `mesh_edge_count`, `boundary_edge_count`, `mesh_euler`, `is_delaunay`, `nearest`, `within`.

SHA-256 — для контрольных сумм; криптографическая пригодность библиотеки в целом не заявляется.

Тест: `crates/dlm_core/tests/kernels.rs`.
