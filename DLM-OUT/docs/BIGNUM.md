# Крупные числа, BPSW, BVH (v0.124)

## Теория чисел

* `nt_mod_mul`/`nt_mod_pow`/`nt_factor`: умножение по модулю через 256-битное произведение и шаг Кнута 3/2 (вместо удвоения), ρ-Полларда до c = 32, r = 2^26 (множители до ~2^50).
* `nt_is_prime`: MR по 20 базам (детерминированно ниже 3.317·10^24), выше — дополнительно сильный тест Люка (Selfridge), т. е. BPSW.

## BigNat (до 2^20 бит, модуль до 2^19 бит)

| функция | тип |
|---|---|
| `bn_powmod(a, e, m)` | `Option<BigNat>` (none: m = 0 или слишком велик) |
| `bn_modinv(a, m)` | `Option<BigNat>` |
| `bn_is_prime(n)` | `Bool` — пробное деление < 1000, MR по 12 базам, сильный Люка (BPSW) |
| `bn_next_prime(n)` | `Option<BigNat>` |
| `bn_random(seed, bits)` | `Option<BigNat>` — SplitMix64, старший бит установлен |
| `bn_to_hex` / `bn_from_hex` | `Text` / `Option<BigNat>` |

`std.bigprime` (`StdBigPrime extends StdSpatial`): `random_prime`, `big_rsa_key/encrypt/decrypt/sign/verify`, `mersenne`, `lucas_lehmer`, `big_crt`, `big_prime`. Нет гарантий постоянного времени — не для боевой криптографии.

## Сетки и пространственные индексы

* `bvh_raycast(tris, origins, dirs)` → `[треугольник, t]` на луч, `[-1, -1]` при промахе. BVH (медиана по длинной оси, листья ≤ 4, расширенные коробки), Мёллер–Трумбор, при равных t — меньший индекс; ответ не зависит от дерева.
* `bvh_winding(tris, points)` — обобщённое число оборотов (телесные углы), ≈1 внутри замкнутой сетки.
* `bvh_pairs(boxes)` — пары пересекающихся AABB (6 чисел на коробку), sweep-and-prune, сортированы.
* `std.spatial`: `soup_area`, `soup_volume`, `ray_distance`, `ray_triangle_index`, `winding_number`, `inside_mesh`, `box_overlaps`, `box_overlap_count`.

Интерпретатор и C выполняют одни и те же операции; тест `bignum_bvh.rs`.
