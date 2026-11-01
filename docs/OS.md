# Рекурсивные данные, процессы, сеть (v0.126)

## Рекурсивные записи и enum

* Поле записи может ссылаться на саму запись под `Option`/`List`/`Sequence`/`Result`/`Sum`: `record Dir(name: Text, subdirs: List<Dir>)`. Без такой обёртки — ошибка.
* `enum` может быть рекурсивным и обобщённым: `enum Tree { Leaf, Node(l: Tree, v: Nat, r: Tree) }`, `enum Opt<T> { Some(x: T), None }` (нульарные варианты обобщённых enum: `None<Nat>()`).
* Раскрытие: одна запись `Name(v_A: Option<Bool>, f_A_0: Option<T>, ...)`, конструкторы, аксессоры `A_f0`.
* Мера `decreasing t` для параметра-записи: рекурсивный вызов получает значение, извлечённое из `t` (связка `match`, `field` + распаковка, голова списка-поля). Охрана не нужна. В рантайме проверяется только fuel.

## Процессы

| функция | результат |
|---|---|
| `proc_run(prog, args, input)` | `Result<List<Text>,Text>` = `[код, stdout, stderr]` (сигнал: 128+N) |
| `proc_spawn(prog, args, input)` | `Result<Nat,Text>` — фоновый процесс, ввод/вывод через временные файлы |
| `proc_poll(id)` / `proc_wait(id)` / `proc_kill(id)` | `Option<код>` / `[код, stdout, stderr]` (id освобождается) / `ok(0)` |
| `proc_env(name)` | `Option<Text>` |

Незавершённые дети убиваются при выходе. `std.process`: `ProcResult`, `run`, `run_with_input`, `shell`, `succeeded`, `output_of`, `spawn`, `wait_for`, `is_running`, `run_parallel`, `env_or`.

## TCP

`net_resolve`, `net_listen(host, port)` (порт 0 — любой), `net_local_port`, `net_accept(l, ms)`, `net_connect(host, port, ms)`, `net_send`, `net_recv_line`, `net_recv_all`, `net_shutdown`, `net_close`; `ms = 0` — без тайм-аута. Сокеты — `Nat`-идентификаторы с 1 (не линейные ресурсы). Ошибки `cannot <op> ... (os error N)`. `std.net`: `parse_http`, `http_request`, `http_get` (HTTP/1.0, без TLS), `http_serve_one`/`http_serve` с чистым обработчиком `Fn<Text, Text>`.

Нативный C: POSIX (fork/exec, sockets) и Windows (CreateProcess, Winsock, `-lws2_32`). Тест: `tests/os_records.rs`.
