# Ordered maps (v0.122)

`NatMap` (`Nat → Nat`) и `TextMap` (`Text → Nat`, ключи в порядке байтов UTF-8) — неизменяемые значения (≤ 2^24 записей), сравниваются `equal`. Rust `maps.rs` = C `dvm_*` (отсортированные массивы, двоичный поиск).
* `km_empty/insert/get/remove/contains/len/keys/values/from/add/min/max/floor/ceil/merge/range`;
* `tm_empty/insert/get/remove/contains/len/keys/values/from/add/merge`, `tm_count_words(text)`.
* `*_from(keys, values)`: при повторе ключа побеждает последнее значение; `*_merge(a, b)`: побеждает `b`; `*_add`: счётчик, `none` при переполнении.
* Печать: `nmap{1: 10, 5: 50}`, `tmap{"a": 1}`.

`std.maps` (`StdMaps ⊃ StdCategories`): `km_entries`, `tm_entries`, `km_map_values`, `km_filter`, `km_fold`, `km_invert`, `km_add_all`, `tm_add_all`, `counts`, `text_counts`, `most_common`, `km_argmax`, `first_positions`.

Тест: `crates/dlm_core/tests/maps.rs`.
