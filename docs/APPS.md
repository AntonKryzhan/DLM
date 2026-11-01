# Приложения: циклы, перечисления, интерактивный запуск (v0.125)

## `loop`-функции

```dlm
fn repl(seen: Nat) -> Nat loop = match read_line() { none => seen, some(l) =>
    if_then_else(text_eq(l, "quit"), seen, let u = print_line(l) in repl(seen + 1)) }
```

* `-> T loop` вместо `decreasing`: меры нет, функция может работать бесконечно (partial, termination `tail_loop`).
* Рекурсивные вызовы — только прямые хвостовые: тело, ветви хвостового `if_then_else`, тело хвостового `let`/arm `match`. Нельзя: вызов внутри выражения, из лямбды, как значение, в `mutual`.
* Интерпретатор и C исполняют хвостовой вызов как итерацию (постоянный стек). Владение `File`/`ByteBuf` проверяется как обычно: ресурс передаётся в следующую итерацию аргументом или закрывается на выходе.
* Не может участвовать в теоремах (не total).

## `enum`

```dlm
enum Shape { Circle(r: Nat), Rect(w: Nat, h: Nat), Empty }
fn area(s: Shape) -> Nat = match s { Circle(r) => mul(3, mul(r, r)), Rect(w, h) => mul(w, h), Empty => 0 }
```

* Конструкторы — функции `Circle(2)`, `Empty()`; образцы `Circle(r)`, `Rect(_, h)`, `Empty`, `_`.
* Проверка полноты при проверке программы: `match on enum Shape is not exhaustive: no arm for Empty`; неизвестный вариант — ошибка.
* Раскрытие (v0.126): запись `Shape(v_Circle: Option<Bool>, f_Circle_0: Option<Nat>, ...)`, функции `Circle`, `Circle_f0`. Имена вариантов уникальны в теории. С v0.126 — рекурсивные и обобщённые (OS.md); без ресурсов.

## `dlm run --interactive`

stdin читается по требованию, вывод печатается сразу (как у нативной программы). Без флага — прежнее поведение (`--stdin`).

Тесты: `tests/apps.rs`.
