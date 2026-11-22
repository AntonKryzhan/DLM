# Сетевые проверки DLM v0.134.0

Дата: 19 ноября 2026. Платформа: Linux x86-64, Rust 1.99.1,
системный libcurl 8.14.1 (capabilities=255), OpenSSL 3.5.7.

Полный набор executable-тестов workspace: **1011 passed, 0 failed, 1 ignored**
(151 отчёт test harness). Пропущен GUI-тест, требующий рабочего стола.
Документационные тесты отдельно завершились успешно (0 тестов).
После последних изменений повторно прошли audit_project (7), example_corpus (6)
и native_network (4), а также release-сборка workspace.

Новые Rust-проверки покрывают реальный бинарный TCP/UDP из DLM, соответствие
кодеков VM и C, граничные/RFC-векторы, все неполные префиксы, запрет ложного
NoIO, явную unsafe-политику, видимый аудит и точечные проектные допущения.

Независимые локальные peers: 15 тестов Python прошли без пропусков.
Проверены бинарные TCP/UDP, IPv4/IPv6, пустые/усечённые UDP-датаграммы,
частичный TCP EOF, would-block, общий таймаут, отмена, multi-socket poll,
входные границы, CSPRNG, отключённый backend и lifecycle провайдера.
HTTPS/TLS 1.3 проверены с локальным CA; неподходящий CA, имя, public-key pin
и TLS 1.2 отвергнуты. HTTP/2 согласован с независимым Node http2 сервером.
WebSocket проверен с независимым сервером: авторизация, двоичные данные,
частичные фреймы, фрагментированное сообщение и PING/PONG.
Скомпилированная DLM-библиотека выполнила HTTPS-запрос с динамическим URL;
повторные создание, автоматическое закрытие и shutdown backend прошли.

Проверка минимальных объявлений libcurl против установленных публичных
заголовков: network_curl_abi.c, компиляция с -Wall -Wextra -Werror -Wno-misleading-indentation — прошла.
ASan/UBSan loopback-примера — прошли с detect_leaks=0. LeakSanitizer
недоступен в этой среде из-за ptrace, покрытие LSan не заявляется.

HTTP/3-only проверен на запрет перехода к HTTP/1.1 при недоступном UDP-сервисе.
Успешная HTTP/3-сессия с настоящим HTTP/3-сервером не проверялась.
ECH передаётся backend, успешное ECH-рукопожатие не проверялось.
Windows/macOS в этом прогоне не запускались. Готовый Windows-компилятор
не входит в исходный патч; Build-Network.cmd пересобирает его на месте.

Воспроизводимые команды из DLM-main:

```bash
cargo test --workspace --release --locked --offline
DLM_NETWORK_REQUIRE_CURL=1 python3 crates/dlm_core/tests/native_hosts/network.py
cc -std=gnu11 -Wall -Wextra -Werror -Wno-misleading-indentation crates/dlm_core/tests/native_hosts/network_curl_abi.c -o network_curl_abi -ldl
./network_curl_abi
```

Сборка Windows и её PowerShell launcher включены как исходники и должны быть
выполнены на Windows; этот отчёт не выдаёт Linux-проверки за Windows-проверки.
