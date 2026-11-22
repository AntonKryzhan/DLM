# Native Application Foundation: приёмка v0.133.0

Реализованы механизмы всех 18 пунктов ТЗ. Пять интеграционных критериев проверены
отдельно на Linux x86-64 и Windows x86-64, включая живое встраивание GUI.
На Linux выполнена полная регрессия; на Windows — приёмочная матрица native
и выборочная старая регрессия. Прерванные по времени Windows-прогоны не
засчитаны как пройденные. Точные границы проверки приведены ниже и в отчёте.

| № ТЗ | Реализация | Проверяемый результат |
| --- | --- | --- |
| 1 | U/I8..64, USize/ISize, F32/F64, битовые операции, checked/wrapping/saturating, явные преобразования | Границы, переполнения, narrowing, NaN/Inf, signed zero, subnormal, совпадение VM/C |
| 2 | Машинные аргументы/результаты и локальные значения конкретно типизированных функций; упаковка только при явном ordinary-переходе | В C нет DV для численного ядра; scalar и packed инструкции в ассемблере; 0 аллокаций горячего цикла |
| 3 | let mut, поля/индексы, for/while, вложенные циклы, break/continue, передача изменяемых ссылок | Совпадение VM/C, постоянная дополнительная память, порядок вычисления RHS и индексов |
| 4 | Array/Buffer/Box/Slice/MutSlice, выравнивание, split_at_mut, проверенный IndexProven | Запись O(1), отсутствие копирования срезов, отрицательные проверки alias/lifetime и инвалидации обходов |
| 5 | F32/F64 SIMD, SSE2/AVX2/AVX-512, CPU dispatch/fallback, автоматическая векторизация совместимых циклов | Скалярный/SIMD результат совпадает; packed F64 mul без fast-math; порядок редукций сохраняется |
| 6 | Владение вложенными агрегатами и Resource<T,finalizer>, частичные move, заимствования, ранние выходы и ошибки | Очистка под ASan/UBSan; fault injection до/после захвата внешнего ресурса; use-after-move отвергается |
| 7 | Ptr/NonNull/OpaqueHandle/BorrowedPtr/ForeignBuffer, unsafe и проверяемые смещения | Нулевая копия внешнего буфера; внешняя память не освобождается DLM; lifetime и null-контракты проверяются |
| 8 | repr(C), packed/aligned, структуры/массивы/union/FnPtr, заголовки, размер/выравнивание/смещения/версии | Независимые C/C++ структуры; проверки layout в C и MIR; size_of/align_of/abi_version в DLM |
| 9 | C imports, объектные/static/shared библиотеки, trusted dependencies, явные ABI/ownership/UTF-8 контракты | Независимая C-библиотека принимает структуру и буфер; разрешены безопасные DLM-обёртки вокруг unsafe |
| 10 | Экспорты и таблицы FnPtr, userdata, вложенный вход, потоки и границы ошибок, Resource регистрации | Четыре потока вызывают callback; отмена дожидается вызовов до освобождения контекста; ошибка автоматически отменяет регистрацию |
| 11 | executable/staticlib/sharedlib/plugin c-abi-v1, target triple, runtime init/shutdown | Независимая загрузка/линковка; правильное имя module.dll/module.so; повторный init; shutdown отклоняется при живых ресурсах |
| 12 | Явные Box/Resource экземпляры без G[] native-состояния; отдельные runtime загруженных модулей | Несколько объектов и две библиотеки; таблицы адресуют свой модуль; закрытие одного не влияет на остальные |
| 13 | AtomicBool/U32/U64/F32, load/store/exchange/CAS, порядки и lock-free | Многопоточные C-хосты, независимые контексты; некорректные ordering-комбинации отвергаются |
| 14 | Транзитивные NoAlloc/NoBlocking/NoIO/NoUnwind/BoundedWork/ThreadSafe/ThreadAffine | Ложные обещания отвергаются с достижимыми вызовами; ThreadBound и GUI проверяют владельца потока |
| 15 | Прежний renderer: child/attach, resize/DPI/focus/input/wheel, CPU/GPU, сохранение GL/FPU хоста | Живой внешний C-хост на обеих ОС: CPU/GPU, ввод, resize, attach/detach, независимые lifecycle, отказ при неверном потоке, GL/FPU хоста и restart; Linux также ARGB32; Windows также reentry и отказ аллокации при создании |
| 16 | Бинарные scalar операции, DLMB/TLV, типизированный codec v1/v2, UTF-8 XML библиотека | 100 независимых Python binary roundtrip, 250 ElementTree roundtrip, Unicode/escaping, unknown fields/flags, повреждённые входы |
| 17 | AST → проверенный native Typed Core/MIR/layout → C, независимая повторная верификация | Поддельные типы/layout/операции/владение и эффекты отвергаются; backend использует установленные типы |
| 18 | Положительные/отрицательные, differential, ABI, threads/callbacks, allocation/fault-injection, GUI и старая регрессия | Linux: 1006 passed, 0 failed в полном прогоне; дополнительные проверки последующих native-изменений. Windows: 44 приёмочные проверки native и 165 старых тестов в 7 наборах прошли; полная старая Windows-регрессия не завершена |

`BoundedWork` означает конечные for-обходы и нерекурсивную цепочку вызовов в
модели числа операций; это не обещание времени ОС. Консервативный анализ может
отвергать допустимую программу. Чужой C-код должен соблюдать явно доверенные
ABI, alias, lifetime, cancellation и execution-контракты.

XML поддерживает UTF-8 XML 1.0 без DTD/внешних сущностей, с ограничением глубины
256. Схемы сериализации задаются обычным DLM-кодом. Имеющиеся Nat/Int/Float,
FloatVec, ByteBuf/TextBuf/File, прежние loop-функции, GUI и доказательное ядро
сохраняют контракты. Существующий Windows interpreter GUI не расширялся: Win32
GUI выполняется в скомпилированном native-модуле.

Команды и контракты: `NATIVE_APPLICATIONS.md`. Воспроизводимые проверки:
`tools/test_native_matrix.sh` и `tools/test_native_matrix.ps1`.
Фактические результаты платформ записаны в `NATIVE_TEST_RESULTS.txt`.
Подробная запись Windows-проверок: `NATIVE_WINDOWS_VALIDATION.json`.
