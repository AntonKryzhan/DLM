# Состояние законов DLM (по версии v0.131.0)

Сопровождает `DLM_ARCHITECTURAL_LAWS.md`. По закону 42 готовность описывается не процентами, а отдельными
сведениями по каждому требованию:

* **Designed** — есть нормативный контракт и критерии приёмки;
* **Implemented** — механизм существует в этой версии (указан объём);
* **Validated** — есть воспроизводимые проверки (указаны тесты);
* **Proven** — свойство доказано в объявленной модели (в DLM пока не заявляется ни для одной подсистемы);
* **Open** — незакрытые обязательства.

Статусы относятся к конкретному свойству и платформе. Текущая конфигурация проверки — Linux x86-64,
Rust 1.99.1 из предоставленного дистрибутива, GCC 13.3.0. Проверки MinGW/Wine относятся к прежней версии;
Windows x86-64 в этом прогоне не проверялся (нативная поддержка ещё требует подтверждения, закон 38).
Записи «Open» — это технический долг в смысле раздела 0 законов: граница гарантии и условие закрытия.

v0.130 — законы 6, 9, 11–13, 20, 22: явные `FnEffect`, лексический вывод
эффектов при вызове, ограничения callable и встроенные проекции типов; независимые
проверки IR и итеративные обходы. Свидетельства: `effect_functions.rs`,
`effect_library.rs`, `ir_verify.rs`. `std.files` закрывает reader при Result-ошибке;
HTTP-handler может читать файл и писать журнал. Контракт и открытые границы —
`EFFECT_FUNCTIONS.md`. Это Implemented/Validated для указанного фрагмента;
формальное доказательство сохранения семантики и общий этап D/G остаются Open.

v0.88 продолжает B и начинает ограниченный фрагмент C. Схемы записей передаются из checker, вложенные типы
получают `RecordId`, операции проверяются по схемам. Структурированные доказательства Nat-равенств можно
экспортировать и независимо проверять из файла, без исходника и поиска. Свидетельства: `record_schemas.rs`,
`equational_kernel.rs`, `cli_proof_replay.rs`, общий прогон `cargo test --workspace --locked --offline`.

Open для законов 11–13: полные Resolved HIR/MIR и разрешение всех ссылок до lowering. FileId и физические диагностики добавлены в v0.99; передача контрактов/схем по DeclId и номинальных типов из frontend — в v0.100 (SOURCE_IDENTITY.md, DECLARATION_IDENTITY.md). v0.90 проверяет факты управления для четырёх match-проекций
(List/Option/Result) в поддерживаемом IR-фрагменте; это не общий refinement solver. Open для закона 24: определения, общие переписывания,
подстановка, логические связки/кванторы и индукция ещё не имеют независимых свидетельств. Их сертификаты явно
показывают `legacy-trusted`; полный этап C не закрыт.

| Закон | Implemented / Validated | Open |
|---|---|---|
| 1. Смысловые слои | Source (паспорта, теории, доказательства) отделён от IR: IR стирает утверждения и паспорта, хранит только типы, эффекты и статус завершения; C runtime не содержит доказательных данных. `compile.rs`, `TYPED_CORE.md`; `compile_c.rs`, `typed_core.rs` | Явной метки слоя у каждой структуры нет; runtime-дескрипторы регионов пока только для GPU/dense-исследовательского слоя |
| 2. Операции через паспорт | Проверка capabilities и trust в checker (например, `print_decimal` требует `can_print_decimal`, сжатые Nat не печатаются). `checker_smoke.rs`, `ordinary_surface.rs`, `trusted_base_closure.rs` | Компилятор опирается на проверенный модуль, но собственных capability-проверок не делает |
| 3. Proof наверху, стирание внизу | Утверждения проверяются checker'ом и стираются в IR (`erased statements: N`); `--emit-ir` показывает стирание. `compile_c.rs` (`lowering_erases_statements...`) | Нет отдельного ErasureReport и формального `ProofIR -> ErasedRuntimeIR` |
| 4. Паспорта на регионах | Исследовательский слой: `dense_runtime.rs`, `backend_layout.rs` (дескрипторы буферов) | Исполняемые коллекции не имеют плотного представления (закон 34) |
| 5. Детерминированное чистое ядро | Чистые вычисления детерминированы; fingerprint'ы аудита стабильны. `audit_project.rs` (`fingerprints_are_stable...`), `property_invariants.rs` | Нет явного источника случайности и часов как эффектов |
| 6. Явные эффекты | Эффекты `io@stdout/stdin/args` в контракте функции, распространяются по цепочке вызовов; `Fn` чист по построению; `FnEffect` несёт явные эффекты и частичность; утверждения не могут зависеть от них. `function_contracts.rs`, `typed_core.rs`, `text_io.rs` | Пользовательские переменные эффектов и subtyping отсутствуют; явные FnEffect и generic callable сохраняют конечные границы; время/случайность как общие эффекты открыты |
| 7. Capabilities как маршрутизатор | Исследовательский GPU/remote слой (`backend_capability.rs`) | Нативный backend выбирает скалярный путь по типам, а не по capabilities |
| 8. Контракт bridge | `BridgeProfile`, профили сохранения, unsafe-мосты видны в аудите. `soundness_boundary_ledger.rs`, `audit_project.rs` | Мосты не компилируются (E0950) |
| 9. Trust только ухудшается | Join доверия, taint аксиом/oracle/unsafe сохраняется, trusted-only отвергает аксиомы. `trusted_base_closure.rs`, `audit_project.rs`, `soundness_boundary_ledger.rs` | — |
| 10. History: полная в аудите | HistoryChain в паспортах, `dlm explain`, аудит с fingerprint. `certificate_audit.rs`, `audit_project.rs` | Политика хранения динамического аудита не определена |
| 11. Checker из passes; Typed Core | Pass-отчёты (`passes.rs`); **v0.87: Typed Core** — типы каждого выражения и контракты функций передаются в типизированное IR, верификатор IR после lowering. `typed_core.rs`, `checker_passes.rs` | Основная семантика всё ещё в монолитном `checker.rs`; нет отдельных HIR/MIR/Representation IR; верификатор структурный, не доказывает эквивалентность |
| 12. После resolution — ID | `ExprId`, индексы функций IR, стабильные символы; v0.88 — `RecordId`/`RecordRef`; v0.100 — `DeclId` функций/записей через AST/resolver/Typed Core/IR, обязательные контракты и схемы по ID, номинальные типы из frontend. `declaration_identity.rs`, `cli_declaration_identity.rs`, `resolver_ids.rs`, `typed_core.rs` | Разрешение value/call/local/constructor-ссылок ещё частично в source-адаптере lowering; полный Resolved HIR отсутствует |
| 13. Span у каждого объекта | **v0.99:** FileId у Span, контекст файла у AST/resolver, проверенные позиции в Typed Core/IR; физические диагностики парсера и выражений с путями импортов. Верификатор проверяет файловые ID, диапазоны и origin. `source_identity.rs`, `cli_source_locations.rs`, `typed_core.rs`, `diagnostic_spans.rs` | Точные позиции отдельных токенов всех объявлений/import и всего исследовательского слоя ещё не заданы; runtime/native source maps открыты |
| 14. Плотные runtime-данные | v0.86: малые Nat/Int без выделения памяти, скалярные функции без упаковки. `compile_c.rs` | Списки — массивы указателей с копирующим `cons`; нет плотных векторов |
| 15–17. GPU batch-first, location, materialization | Исследовательский слой с паспортами location/materialization. `gpu_checkpoint_restore.rs`, `backend_capability.rs` | Нет исполнения на GPU в нативном backend |
| 18. Cost-class | `CostClass` в паспорте; бюджеты нормализации. `termination_budget.rs` | Нет модели стоимости в компиляторе |
| 19. Верифицированная оптимизация | Оптимизаций, меняющих семантику, нет: скалярная специализация и хвостовые циклы сохраняют порядок вычисления, проверки переполнения и ленивость. Дифференциальные тесты с ошибками. `typed_core.rs`, `compile_c.rs` | Нет PassReport/OptimizationCertificate; доверие к C-компилятору (`-O2`) не формализовано |
| 20. Кэш по проверенному смыслу | Fingerprint'ы аудита и проектов. `audit_project.rs` | Кэша компиляции нет |
| 21. Trusted base видна | `dlm audit`: символьное ядро, runtime-исполнитель, аксиомы; v0.88 добавляет `equational-replay-kernel`, сила сертификата различается как `kernel-replay` / `legacy-trusted`. `audit_project.rs`, `equational_kernel.rs` | Нативная цепочка (lowering, C runtime, C-компилятор) не включена в отчёт аудита |
| 22. Объяснимость назад | Паспорта с историей, сертификаты, аудит; **v0.87:** каждый узел IR хранит исходный `ExprId` и Span. `typed_core.rs` | Исполняемый файл не хранит ссылок на исходник |
| 23. AI-agent-friendly | Документация по каждой возможности, `TEST_MATRIX.md`, законы и этот файл | Крупные файлы (`checker.rs`, `passport.rs`) |
| 24. Минимальное доказательное ядро | v0.88: плоский DAG термов/выводов для Nat-равенств, независимый checker, явный внешний контекст аксиом, экспорт и replay без исходника; подмена шага после rehash отвергается. `EQUATIONAL_REPLAY.md`, `equational_kernel.rs`, `cli_proof_replay.rs` | Частичный этап C: нет независимого replay определений, общей нормализации, лемм, связок/кванторов, подстановки и Nat/list-индукции. Старые доказательства остаются `legacy-trusted`; soundness самого нового ядра формально не доказана |
| 25. Честное понижение статуса | Статусы Open/Axiom/Conjecture; машинные обязательства «open»; неизвестность в пределах бюджета. `ordinary_theorems.rs`, `machine_arithmetic.rs` | — |
| 26. Владение, alias, lifetime | Неизменяемые значения; регионы вызовов и группы выживших (v0.86.1) освобождают память без use-after-free (ASan/LSan). **v0.102 (D-min):** изменяемый `ByteBuf` с единственным владельцем, move/borrow по всем путям (ветви, ленивые операнды, аргументы одного вызова), запрет захвата и хранения в данных, план освобождения в Typed Core, `__release` в IR, изменение на месте и очистка живых буферов при ошибке исполнения. `OWNERSHIP.md`; `ownership.rs`, `cli_creal_ownership.rs`, `compile_c.rs` | Внешние дескрипторы с ошибками закрытия, долгоживущие разделяемые заимствования, заимствования частей, буферы внутри структур данных (этап E), отмена задач (этап G), формальная корректность анализа |
| 27. ABI/layout | Исследовательский `backend_layout.rs` | Нет внешних границ ABI в нативном backend |
| 28. Режимы и профили гарантий | Политики `research` / `trusted-only`; `--require-no-overflow`. `cli_programs.rs` | Нет профилей debug-audit / release-safe / release-fast / certified |
| 29. Ограниченная нормализация | Бюджеты символьного ядра (глубина 384, ограничения case split, защита от циклов переписывания), fuel интерпретатора. `termination_budget.rs`, `symbolic_kernel_soundness.rs` | Checker защищается большим стеком потока, а не итеративными обходами |
| 30. Единая операционная семантика | **v0.87:** нормативная спецификация `SEMANTICS.md`; интерпретатор и компилятор сравниваются по наблюдаемому поведению **включая ошибки исполнения** (вывод до ошибки, сообщение, код выхода); `dlm run` печатает вывод, сделанный до ошибки. `typed_core.rs`, `compile_c.rs`, `cli_programs.rs` | Различия политик ресурсов (fuel / стек) задокументированы, а не устранены |
| 31. Чистота, завершение, машинная определённость независимы | **v0.87:** `dlm contracts` — эффекты, завершение, машинная арифметика, стек, отказы и математическая спецификация по отдельности; хвостовые вызовы самой себя — цикл в обоих путях backend; исчерпание стека — определённая ошибка исполнения. `typed_core.rs`, `cli_programs.rs` | Нет пред/постусловий в языке; взаимные хвостовые вызовы не устраняются; нет неограниченных процессов |
| 32. Математическая и машинная арифметика | `Nat`/`Int` — проверяемые u128/i128; `Defined_128` и интервальный анализ в Typed Core. В v0.93–v0.97 реализована отдельная точная многословная арифметика host/`std.exact` в DLM/C до 16384 бит с явными бюджетами. `machine_arithmetic.rs`, `integers.rs`, `SOURCE_EXACT_PRECISION.md` | Многословные числа не заменяют встроенные `Nat`/`Int`; рациональные интервалы (v0.101) и вычислимые вещественные (v0.102, `COMPUTABLE_REALS.md`) — библиотечные значения, не машинные типы. **v0.114:** машинные `Float`/`FloatVec` (binary64) и `std.numerics` (`FLOATS.md`). **v0.115:** `NatVec`, графовые алгоритмы, `std.graphs/geometry/probability` (`GRAPHS.md`). **v0.116:** численная линейная алгебра `la_*`, `std.linalg` (`LINALG.md`). **v0.117:** FFT, симплекс, `std.analysis`, `std.optimize` (`ANALYSIS.md`). **v0.118:** теория чисел, `F_p[x]`, группы перестановок, `std.number_theory` (`ALGEBRA.md`). **v0.119:** SAT, гомологии над Z, персистентность, `std.logic`, `std.topology` (`LOGIC_TOPOLOGY.md`). **v0.120:** базисы Грёбнера над F_p, `std.ideals`, `std.categories` (`GROEBNER.md`). **v0.123:** крыловские решатели, точные предикаты, Делоне, k-d деревья, STFT/вейвлеты/IIR, PCA/MCMC/AR (`KERNELS.md`). **v0.124:** BigNat powmod/modinv/BPSW, 256-битный mulmod, BVH (`BIGNUM.md`). **v0.121:** сертификаты невыполнимости с независимой RUP-проверкой, подсчёт моделей, ЦЛП (`PROOFS_ILP.md`); нет межпроцедурных диапазонов, модульной/насыщаемой машинной арифметики и независимых скалярных proof certificates |
| 33. Обобщения | **v0.110 (E-min):** обобщённые функции и записи с явными аргументами типов, мономорфизация до проверки, `std.collections`. `GENERICS.md`, `generics.rs` **v0.111:** локальный вывод аргументов типов, ограничения `Eq`/`Ord`, `equal`/`less`. **v0.112:** классы типов и экземпляры (в т.ч. обобщённые), ограничения-классы, `std.classes`. **v0.113:** суперклассы и методы по умолчанию. | Многопараметрические классы, обобщения в доказательствах |
| 34. Коллекции, текст, байты, пути | Text — UTF-8 с индексами по скалярам; v0.102: владеемый изменяемый `ByteBuf` (до 2^28 байт, `OWNERSHIP.md`); матричные данные на host имеют ограниченные специализированные представления, а `std.exact` работает через обычные списки слов  **v0.122:** упорядоченные словари `NatMap`/`TextMap`, `std.maps` (`MAPS.md`).| В исполняемом DLM нет универсальных плотных векторов, срезов, builders, общего байтового и системного пути; плотного source/native scalar layout нет |
| 35. FFI | — | Нет (этап F) |
| 36. Стандартная библиотека | `std.nat`, `std.list` и специализированные `std.f2`, `std.matrix_f2`, `std.homology_f2`, `std.exact`, `std.matrix_q`, `std.matrix_bigq`, `std.complex_q` в пределах их отдельных профилей (`FINITE_MATHEMATICS.md`, `SOURCE_EXACT_PRECISION.md`) | Нет полноценного системного слоя (файлы, сеть, процессы, FFI), общего набора контейнеров, числовых и текстовых API; специализированные математические модули его не заменяют |
| 37. Структурированный параллелизм | — | Нет (этап G) |
| 38. Пакеты и инструменты | `dlm.project`, импорты, аудит проекта; CI Linux/Windows. `audit_project.rs` | Нет lock-файла, раздельной компиляции, LSP; Windows не подтверждён на нативном хосте |
| 39. Воспроизводимые проверки | Генераторы программ, дифференциальные тесты, санитайзеры, проверки мутаций ядра и сборщика памяти | Нет baseline-набора нагрузок и порогов регрессии |
| 40. Математическая автоматизация | Символьная нормализация, ограниченные F2/Fp-гомологии, конечная алгебра, точные матрицы и Q-свидетельства с отдельным runtime-checker в v0.89–v0.97 | Нет SMT, e-graphs, refinement types; вычислительные witness не являются `StaticProof`, общие формальные доказательства алгоритмов открыты |

## Этапы (раздел 42)

| Этап | Состояние на v0.102.0 |
|---|---|
| A. Семантическая фиксация | Implemented: `SEMANTICS.md` для исполняемого слоя; Validated: дифференциальные тесты с ошибками. Open: правила для исследовательского слоя |
| B. Typed Core | Implemented/Validated: типы, эффекты, завершение, ID/Span; v0.88 — номинальные схемы; v0.90 — ограниченные guards match; v0.99 — файловые ID и физические диагностики; v0.100 — проверенные объявления по DeclId, контекст/снимки, номинальные типы из checker и проверка происхождения IR. `declaration_identity.rs`, `cli_declaration_identity.rs`, `source_identity.rs`, `ir_verify.rs`, `record_schemas.rs`, `typed_core.rs`, `compile_c.rs`. Open: полные HIR/MIR и frontend-resolution всех ссылок, полные позиции объявлений и runtime source maps |
| C. Доказательные объекты | Implemented/Validated: начальный фрагмент Nat-равенств, независимая проверка DAG, экспорт/replay, явные предпосылки API и бюджеты. Open: определения, общий нормализатор с полным свидетельством, леммы, логика, подстановка и индукция; полный этап не закрыт |
| D. Владение и ресурсы | Implemented/Validated (D-min, v0.102): `ByteBuf` с move/borrow, путевой анализ, освобождение в конце областей и в ветвях, интерпретатор/C совпадают, очистка при ошибках, 26 отрицательных сценариев lifetime, ASan/LSan и MinGW/Wine. `OWNERSHIP.md`, `ownership.rs`. Open: внешние handles и ошибки их закрытия, отмена, заимствования частей и буферы в контейнерах |
| E–H | Open: специализированная математика v0.89–v0.97 не закрыла плотные общие данные, системный API, сеть или toolchain |

Общая оценка на v0.102.0: A/B имеют реализованные и проверенные части, но не
все критерии; C принят в объёме C-min (replay Nat-равенств); D принят в объёме
D-min (владение `ByteBuf`); полные C/D и E–H открыты.
Математическая очередь закрывает 8/16 критериев числовой основы,
не подменяя этапы A–H. Для выпуска языка общего назначения обязательны
прикладные приёмочные сценарии из `ROADMAP.md`, а не только формальная
математика. Это уточнение документации не является новой проверкой кода.

## 0.89.0 computational mathematics boundary

`math` is a bounded exact host-side runtime library, and `dlm homology` exposes
its finite F2 topology computations. `std.f2` provides ordinary source-level
scalar functions. Laws 1/3/4: matrix entries hold only residues; no StaticProof
or full passport enters dense storage. Laws 5/39/40: deterministic exact
algorithms, explicit size errors, exhaustive small-matrix checks and known
homology fixtures; assurance remains runtime-checked. See
`FINITE_MATHEMATICS.md` for exact limits and acceptance scope.

Open: regional passports and language/backend bindings for matrices; independent
algorithm certificates; general algebraic structures, integral coefficients,
HoTT and higher theories. No B/C milestone is declared complete by this module.

## 0.90.0 source mathematics and witnesses

Implemented/Validated: ordinary DLM matrix/homology libraries (public axes 32),
Result constructor patterns, local dominance facts for match payload extraction,
and bounded algorithm-independent F2 rank/homology checking with standalone
export/replay and optional external boundary binding. Layers: source libraries
-> existing checker/Typed Core/C runtime; separate host runtime-witness checker.
No matrix builtin bypass, StaticProof, Nat-kernel extension or dense-memory
performance claim is introduced. See `MATH_SOURCE_AND_WITNESSES.md` for trusted
components, budgets and precise acceptance conditions (laws 1–5, 11, 24, 39, 40).
Open: regional passports/dense lowering, formal algorithm proofs, general
algebraic structures and higher mathematics. Stage C remains partial.

## 0.91.0 finite algebra

Implemented/Validated: bounded group/ring/field/module laws and maps, normal
quotients, checked first-isomorphism construction, source libraries and C parity.
Layers: host runtime/audit APIs; ordinary source -> existing checker/Typed Core/C.
No StaticProof/kernel rule or optimizer authority is added. Source raw records
are revalidated; host invariant-bearing objects have private fields. Budgets
are fixed carrier bounds (64 host / 8 source), scalar prime support <=251.
`FINITE_ALGEBRA.md` specifies exact scope, cost and trusted base;
`MATHEMATICS_PROGRESS.md` retains unfinished work and per-patch acceptance.
A planning effort estimate is separate from implemented/validated readiness.

Open: formal finite-law certificates, generic/unbounded algebra, ideals and
quotient rings/modules, non-F2 matrices/homology, arbitrary precision, full
proof-kernel replay and higher mathematics. The field-label metadata repair
fixes one dependency/recursion/capture analysis case; Resolved HIR remains open.

## 0.92.0 bounded numeric fields and oriented topology

Implemented/Validated: canonical signed rational profiles (u128 host,
32-bit source), prime-field matrices, signed simplicial boundaries, homology,
cohomology and host induced ranks. `NUMERIC_FIELDS.md` defines operations,
bounds, tests, layers and trusted base. No compiler/IR rule, kernel extension,
ABI or ownership change. Public source records are revalidated; host invariant
fields are private. Exact wide intermediates and source arithmetic bounds
separate mathematical equality from machine definedness (laws 29/30/32).
Runtime computation does not acquire StaticProof/independent-witness authority
(laws 24/28/40). Per-patch progress remains criterion-based (laws 39/41/42).

Open: arbitrary precision, Q matrices, integral torsion, odd-field chain tensor
signs, standalone Fp/Q witness checking, generic algebra and proof-kernel
completion. Prior F2 export/replay is preserved. See the numeric specification
for precise closure criteria and `MATHEMATICS_PROGRESS.md` for remaining scope.

## 0.93.0 exact precision budgets and rational matrices

Implemented/Validated: private canonical limb-based integer/rational
values with caller-owned precision/work limits, host rational matrix algorithms
and bounded ordinary source `std.matrix_q` with interpreter/C parity. Layers,
input/intermediate/output profiles and errors are specified in
`EXACT_RATIONAL_MATRICES.md` (laws 1/3/4/29/30/32/39/40). No source Nat/Int,
compiler builtin, IR grammar, ABI or proof-kernel rule changes. Budget exhaustion
is UnknownWithinBudget rather than a mathematical result; runtime evidence does
not grant independent StaticProof authority. New future application requests
are Planned/Open in FUTURE_MATH_APPLICATIONS.md (laws 41/42).

Open: configurable precision in source/native C, dense/generic buffers,
independent Q certificates, formal algorithm proofs, Q homology, symbolic and
real/interval algebra. The original proof-kernel/ownership obligations and
both deferred application tracks remain open.

## 0.94.0 ordinary source multiprecision arithmetic

Implemented/Validated: `std.exact` reference arithmetic on canonical
radix-65536 records, 1..1024-bit source profiles, bounded private algorithms,
immutable explicit reservation budgets and Result errors, interpreter/host/
Python/native-C comparison. No compiler builtin, numeric tag, Nat/Int/IR/ABI
change or new kernel rule. Source and host resource units are distinct; the
source reservation formula is not a formal cost/real-time certificate. Public
record consumers validate invariants; private helpers cannot cross the import
boundary or be shadowed by inherited redeclarations (laws 1/3/24/29/30/32/39/40).
See SOURCE_EXACT_ARITHMETIC.md for exact scope and limits (41/42).

Open: full source precision extension, dense limb buffers/generics, source
BigRational matrices, independent arithmetic/Q witnesses and formal proofs.
Budget copying is possible: this library adds no linear ownership or global
resource meter. Proof-kernel/resource and deferred application obligations
remain open.

## 0.95.0 source BigRational matrices and Q runtime witnesses

Implemented/Validated: budget-threaded ordinary BigRational matrices
(public axes4/private augmentation4x8), exact source/native-C operations and
separate host Q rank/full-kernel checking by factorization/identity equations.
Producer, checker and codec are separate. Exact scalar arithmetic remains
trusted; no elimination is run by the checker, and no StaticProof/kernel or
optimizer authority follows from acceptance. Inverse/affine-solution residual
APIs and external matrix binding are explicit. Bounds, errors, trust, layers
and test evidence are in SOURCE_BIGRATIONAL_MATRICES.md and Q_MATRIX_WITNESSES.md
(laws 1/3/4/24/28/29/30/32/39/40/41/42).

Open: dense/generic layouts, expanded source profiles, source witness synthesis,
serialized determinant/RREF/solution/inverse proofs, general proof terms,
Q homology and formal algorithm/cost correctness. Ownership, full stage C and
the deferred positive-geometry/huge-number tracks remain open.

## 0.96.0 scalar arithmetic within numeric-section closure

Implemented/Validated: exact powers, nonnegative lcm and integer square roots
with remainders in the host API and ordinary source/native C. Integer/rational
domains, zero conventions, full machine exponent ranges, precision, immutable
source budget sequencing and bounded private helpers are explicit in
EXACT_SCALAR_OPERATIONS.md. Independent integer/Fraction/isqrt fixtures,
boundary/resource tests and interpreter/C comparison supply computational
evidence (laws 1/3/24/28/29/30/32/39/40/41/42).

No compiler builtin, representation tag, IR/ABI or kernel rule changes. Runtime
arithmetic adds no StaticProof/optimizer authority. N05 closes in the declared
profiles; 4/16 numeric criteria are closed and 12 remain open. Full source
precision, dense layouts, numerical families, scalar witness checking, platform
acceptance and general proof/ownership obligations remain required. The active
numeric section must close before the next mathematical section begins.

## 0.97.0 bounded source precision through 16384 bits

Implemented/Validated: ordinary source/native wide profiles, compatible small
limits, word division, blocked gcd/text, grouped decimals and word-fold roots.
SOURCE_EXACT_PRECISION.md states intermediate/machine bounds, resource/error
contracts and evidence. Immutable interpreter function declarations are shared
internally; public APIs, evaluation order and recursion checks persist.
No numeric builtin, IR/ABI or kernel rule changes (laws 1/3/24/29/30/32/39/40).

N04 closes on bounded computational evidence; 5/16 criteria close, 11 remain
open (41/42). Sharing a precision ceiling does not unify host/source cost or
text units. No dense-data/performance, scalar StaticProof, formal algorithm
correctness or full platform claim is introduced. N06–N16 and architectural
D/E dependencies remain open.

## 0.98.0 exact complex numbers over Q

Implemented/Validated: private host rational pairs and the ordinary `std.complex_q`
module, canonical input checks, arithmetic, conjugation, squared modulus and
sequential budget consumption. EXACT_COMPLEX_Q.md fixes the Q(i) domain and
the requirement that every rational intermediate fit the selected precision.
All source operations use existing ordinary lowering and scalar APIs; no new
numeric tag, builtin, IR/ABI, proof object or kernel rule is introduced
(laws 1/3/24/29/30/32/40). Runtime results provide no StaticProof or optimizer
authority. Three closed computational regression theorems use existing legacy
certificates; they are not independently replayed general field laws.
Independent Fraction-pair oracles, raw/domain/resource boundary
tests, interpreter/C parity and CLI run/build close N06 in its declared
profile: 6/16 numeric criteria closed, 10 open. Computational acceptance is
tracked in MATHEMATICS_PROGRESS.md (39/41/42); general field/algorithm
correctness, dense layouts, common resource
units, ownership and full platform acceptance retain their open obligations.

## Согласование инженерной и математической очередей (0.98)

Основной инженерный порядок и ограниченный переходный критерий C-min заданы
в [ROADMAP.md](../ROADMAP.md#c-min). N14 зависит от D/E, N15 — от C и правил
конкретного числового семейства. Общая реализация учитывается один раз,
приёмка каждого критерия остаётся отдельной. Полный C сохраняет статус Open.
Числовой раздел сохраняет 6/16 закрытых и 10/16 открытых критериев; Q(i) и
все записи проверки 0.98 сохранены. Последние 705 тестов относятся к проверке
реализации 0.98, а изменение документации не является новым полным прогоном.

## 0.99.0 source identity and physical diagnostics (A/B)

Implemented/Validated: compilation-local FileId in SourceSpan, source context
through AST/resolver/checked facts/IR, physical parser and expression diagnostics,
original paths for imported function errors, and verifier checks for source
identity/range/origin consistency. SOURCE_IDENTITY.md states API compatibility,
trust boundaries, acceptance cases and remaining work (laws 11/12/13/22/30/39/40/41/42).
No language syntax, runtime layout, arithmetic domain, effect or C ABI changes.
No proof or optimizer authority is acquired from source metadata. The source
name boundary and full HIR/MIR remain Open; C-min still requires its separate
six-condition acceptance. Ownership D and numeric N07–N16 remain open.

## 0.100.0 checked declaration identity (B)

Implemented/Validated: process-local source DeclIds through AST/resolver/Typed
Core/IR, mandatory ID-selected checked function contracts and record schemas,
frontend-resolved nominal types, checked-run/context/snapshot guards and
structural declaration-origin verification. DECLARATION_IDENTITY.md fixes the
scope, API and trust boundaries, negative tests and interpreter/native evidence
(laws 11/12/13/21/22/30/39/40/41/42). Name-based display views confer no compiler
authority. No syntax, runtime layout, C ABI, numeric-domain or kernel-rule change.

The bounded declaration handoff closes; this does not close full A/B, C-min or
C. Next is explicit six-condition C-min acceptance before D/E. Complete Resolved
HIR/MIR and frontend resolution of every reference remain Open. N01–N06 stay
closed, N07–N16 remain open, and research R0–R6 remains deferred.

## 0.100.1 complete cumulative distribution

The separately supplied Q(i) API is integrated additively: existing host/source
ComplexQ interfaces coexist with ComplexRational and BigComplex/bigc_*.
EXACT_COMPLEX.md specifies the additional text API and current 16384-bit profile;
CUMULATIVE_BUILD.md identifies the complete distribution and acceptance scope.
The new bounded separator scan covers wide accepted text. Both interfaces,
precision/work distinctions and interpreter/native execution have regression cases.

No new kernel rule, optimizer authority, runtime ABI or readiness milestone is
introduced. Arithmetic results remain runtime values; the two additional closed
theorems use existing legacy finite evaluation, not independent general field-law
proofs. N01–N06 stay closed (6/16); N07–N16 remain open.
C-min still requires its six-condition acceptance. R0–R6 is included only as a
deferred design document; no research mechanism is implemented.

## 0.101.0 C-min accepted; rational intervals (N07)

Stage C, bounded criterion C-min: **Validated.** All six conditions are mapped to
dedicated tests (`c_min_acceptance.rs`, `cli_c_min.rs`, plus `equational_kernel.rs`
and `cli_proof_replay.rs`); see the table in EQUATIONAL_REPLAY.md. The codec now
requires the canonical spelling of an artifact, and `proof-check` separates
Invalid (exit 1), unreadable input (2) and UnknownWithinBudget (3). No kernel
rule changed (`nat-semiring-v1`). Full stage C remains Open: definitions, lemmas,
substitution, connectives, quantifiers and induction are not replayed. The next
engineering stage by law 42 is D (ownership and mutable resources).

Numeric section: rational intervals with inclusion, tightness, hull/intersection,
zero-divisor refusal and guaranteed square roots (EXACT_INTERVALS.md) close N07;
7/16 numeric criteria are closed. Runtime values, no StaticProof authority, no
compiler builtin, IR/ABI or tag changes (laws 1/3/24/25/28/29/30/32/39/41/42).

## 0.102.0 D-min: owned byte buffers; computable reals (N08)

Stage D, bounded criterion D-min: **Validated** for one resource kind. `ByteBuf`
is an owned mutable buffer with borrow (`buf_len`, `buf_get`) and move
(`buf_set`, `buf_push`, `buf_to_list`, `buf_free`, `ByteBuf` parameters,
`let`, results) positions; the checker pass (`crate::ownership`, `E0943`)
rejects use after move, double and sibling moves, moves in lazy operands,
captures, buffers in containers/records/Fn types/type arguments/globals/
statements and rvalue borrows, and releases owners consumed on one branch only
at the end of the other branch. The release plan is a Typed Core fact; lowering
inserts `__release`, the IR verifier checks it and rejects nested `ByteBuf`
types. The native runtime mutates in place outside the immutable regions and
releases live buffers when a runtime error ends the program (laws 26/30/31/41).
Full D remains Open: external handles and close errors, cancellation, borrows
of parts and buffers inside data structures.

Numeric section: computable reals as checked expression DAGs with guaranteed
enclosures of width `2^-n`, a fixed refinement schedule, domain errors only
when proved by the bounds and an undecided comparison (COMPUTABLE_REALS.md)
close N08; 8/16 numeric criteria are closed. Runtime values, no StaticProof
authority (laws 25/32/39/42).

## 0.103.0 borrowed ByteBuf parameters; computable complex numbers (N09)

D-min extended: `&ByteBuf` parameters borrow for a call, exclusive mutation forbids one buffer as borrowed and
moved argument of the same call, six new buffer operations, IR check of borrowed slots (OWNERSHIP.md). N09
closed (COMPUTABLE_COMPLEX.md), 9/16 numeric criteria. Full D remains Open (handles, cancellation, parts).

## 0.104.0 TextBuf; elementary functions (N10)

D-min: a second resource type `TextBuf` under the same ownership pass. N10 closed (ELEMENTARY_FUNCTIONS.md), 10/16.
Checker: merged passport histories are bounded (`HISTORY_LIMIT`, first/last events kept with an elision event);
`std.exact` skips gcd for dyadic values (same canonical results).

## 0.105.0 numeric conversions (N11) and portable format (N12)

N11 and N12 closed (NUMERIC_CONVERSIONS.md), 12/16. No implicit conversion between numeric families; undecidable
roundings of computable reals end in `UnknownWithinBudget`.

## 0.106.0 numeric contracts (N13) and scalar witnesses (N15)

N13, N15 closed (NUMERIC_CONTRACTS.md, SCALAR_WITNESSES.md), 14/16. Every scalar refusal has one category;
witness acceptance is a runtime check with an explicit trust level (`checked`/`replayed`), never a StaticProof.

## 0.107.0 dense numbers (N14)

N14 closed (DENSE_NUMBERS.md), 15/16. New ordinary type `BigNat` (copyable, immutable, region-allocated in C);
`std.exact` values and budgets unchanged under the dense kernel.

## 0.108.0 numeric acceptance (N16)

Numeric section 16/16 (NUMERIC_ACCEPTANCE.md). `std.dense` now extends the numeric chain (`StdScalarWitness`).

## 0.109.0 D: file handles

External handles with close/flush errors (open item of D-min): linear `File`, sticky errors reported by
`file_close`, exit cleanup after runtime errors, interpreter/C parity (FILES.md, `files.rs`).

## 0.110.0 E-min: generics

Law 33: generic functions and records with explicit type arguments, expanded before checking (GENERICS.md).

## 0.111.0 E: inference and bounds

Law 33: omitted type arguments inferred from argument types; bounds Eq/Ord checked per instance (GENERICS.md).

## 0.112.0 E: type classes

Law 33: single-parameter classes, concrete and generic instances resolved by monomorphization (GENERICS.md).

## 0.113.0 E: superclasses and default methods

Law 33: superclasses checked per instance, default methods (GENERICS.md).

## 0.114.0 Machine floats

Law 32: `Float`/`FloatVec`, bitwise interpreter/C parity of printed results, `std.numerics` (FLOATS.md).

## 0.115.0 Graphs, geometry, probability

Law 32/36: `NatVec`, native `gr_*` with interpreter/C parity, `std.graphs`, `std.geometry`, `std.probability` (GRAPHS.md).

## 0.116.0 Numerical linear algebra

Law 32: `la_*` factorizations with bitwise interpreter/C parity, `std.linalg` (LINALG.md).

## 0.117.0 Analysis and optimization

Law 32/36: `fft_*`, `lp_*` with interpreter/C parity, `std.analysis`, `std.optimize` (ANALYSIS.md).

## 0.118.0 Number theory and permutation groups

Law 32/36: exact `nt_*`, `pf_*`, `pm_*` with interpreter/C parity, `std.number_theory` (ALGEBRA.md).

## 0.119.0 SAT and topology

Law 32/36: `sat_*`, `tp_*` with interpreter/C parity, `std.logic`, `std.topology` (LOGIC_TOPOLOGY.md).

## 0.120.0 Gröbner bases and FinSet

Law 32/36: `mv_*`, `gb_*` with interpreter/C parity, `std.ideals`, `std.categories` (GROEBNER.md).

## 0.121.0 SAT certificates and ILP

Law 32/36: `sat_proof`/`sat_check` (independent RUP checking), `sat_count`, `ilp_*` (PROOFS_ILP.md).

## 0.122.0 Ordered maps

Law 34: native `NatMap`/`TextMap` with interpreter/C parity, `std.maps` (MAPS.md).

## 0.123.0 Sparse solvers, robust geometry, signals, statistics

Law 32/36: `la_pcg/bicgstab/gmres`, `geo_*`, `kd_*`, `fft2_*`, `sig_*` with interpreter/C parity; `std.signal`, `std.statistics`, `std.spatial` (KERNELS.md).

## 0.124.0 BigNat primes, BPSW, BVH

Law 32/36: `bn_powmod/modinv/is_prime/next_prime/random/to_hex/from_hex`, BPSW in `nt_is_prime`, `bvh_*` with interpreter/C parity; `std.bigprime` (BIGNUM.md).

## 0.125.0 Loops, enums, file system

Law 34/36: `loop` functions (partial, `tail_loop`, constant stack in interpreter and C), `enum` with checked exhaustiveness, `file_*` path/directory/positioning operations with interpreter/C parity; `std.files` (APPS.md, FILES.md).

## 0.126.0 Recursive data, processes, TCP

Law 34/36: guarded recursive records, recursive/generic enums, record measures; `proc_*`, `net_*` with interpreter/C parity; `std.process`, `std.net` (OS.md).

## 0.127.0 Own GUI, stage G1

Law 34/36: `gx_*`, `file_write_png` with byte-identical interpreter/C output; `std.ui`; record-list recursion in measures (GUI.md).

## 0.128.0 Vector UI and native windows

Law 34/36: `vx_*` byte-identical in interpreter and C; `win_*` (X11 both runtimes, Win32 native); `std.vector` (GUI.md).

## 0.129.0 TrueType text, lists and scrolling

Law 34/36: `file_font_*`, `vx_text_fonts`/op 9, `vx_font_*`, `vx_codepoints` byte-identical in interpreter and C; `std.ui` Scroll/ListView/Table (GUI.md).

## 0.131.0 GPU presentation

Law 34/36: `win_gpu`, `win_present_scene`, `win_read` in both runtimes with the same draw list (`vector::gpu_items` / `dgl_*`); GPU pixels are driver-dependent and compared approximately (GUI.md).
