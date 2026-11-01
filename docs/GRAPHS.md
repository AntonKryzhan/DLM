# NatVec, graphs, geometry, probability (v0.115)

* `NatVec` — плотный вектор `Nat` (≤ 2^24), значение. `nv_empty/new/from_list/to_list/len/get/set/push/slice/concat/range/sum/max/min/sort/reverse/find/to_fv`; ошибки размеров и переполнение суммы → `none`.
* Граф — `n` вершин и параллельные `src`, `dst: NatVec` (ребро `i`: `src[i] → dst[i]`), веса `FloatVec`, ёмкости `NatVec`. Смежность обходится в порядке рёбер. Нативные `gr_*` (Rust `graph.rs` = C `dvg_*`):
  * `gr_bfs` (недостижимые = `n`), `gr_dfs` (preorder), `gr_toposort` (Кан, наименьшая готовая вершина; `none` при цикле);
  * `gr_components` (неориентированно), `gr_scc` (Косарайю), `gr_bipartite` (0/1; `none` при нечётном цикле) — метки по первому появлению;
  * `gr_dijkstra` (веса ≥ 0, `inf` для недостижимых), `gr_bellman_ford` (`none` при достижимом отрицательном цикле), `gr_floyd` (`n×n`);
  * `gr_mst` (Краскал, индексы рёбер, порядок (вес, индекс)), `gr_maxflow` (Эдмондс–Карп, целые ёмкости).
* `std.graphs` (`Graph`, `Route`): построение, соседи/степени, достижимость, кратчайший путь с восстановлением, вес MST, поток.
* `std.geometry`: `Vec2/Vec3/Box2/Affine2`, ориентация и пересечения отрезков/прямых, многоугольники (площадь, периметр, центроид, принадлежность), выпуклая оболочка, аффинные 2D, матрицы 4×4 (`FloatVec`), лучи (плоскость, сфера, треугольник).
* `std.probability`: `erf/erfc`, `log_gamma/gamma`, неполные гамма и бета; распределения равномерное, нормальное (+квантиль), экспоненциальное, χ², Стьюдента, гамма, биномиальное, Пуассона, геометрическое; выборки; квантиль, гистограмма, t-тест, Уэлч, χ²-тест.
* Цепочка теорий: `StdGraphs ⊃ StdGeometry ⊃ StdProbability ⊃ StdNumerics`.
* Тест: `crates/dlm_core/tests/graphs.rs`.
