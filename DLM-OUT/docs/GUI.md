# Собственный GUI DLM: план и этап G1 (v0.127)

В документации до v0.127 GUI упоминался только как цель «позднее — графические приложения»
(DEVELOPMENT_PROGRAM.md) и как необязательный для v1.0 трек (ROADMAP.md). План:

| Этап | Содержание | Состояние |
|---|---|---|
| G1 | Растровые изображения и примитивы, шрифт, PNG; дерево виджетов, раскладка, отрисовка, hit-test; модель/вид/обновление; браузерный бэкенд по HTTP | **v0.127** |
| G2 | Векторная процедурная сцена: пути Безье, скругления, эллипсы, градиенты, клипы, растр внутри, векторный текст (латиница+кириллица), HiDPI, SVG, инкрементальная перерисовка, проверка инвариантов UI | **v0.128** |
| G3 | Нативные окна: X11 (dlopen, интерпретатор и C), Win32 (C); мышь, наведение, нажатие, клавиатура в поле — **ядро v0.128**; далее Wayland, Cocoa, IME, прокрутка, списки/таблицы | частично |
| G4 | Векторная графика, сглаживание, TrueType, GPU-композиция (связь с GPU-треком) | план |

Принципы: вид и раскладка — чистые функции (тестируемы без экрана); эффекты только в цикле событий;
пиксели, PNG и HTML побайтно совпадают в интерпретаторе и нативном C.

## G1: растр (`gx_*`)

Изображение — `NatVec` `[w, h, 0xRRGGBBAA...]`. `gx_new(w, h, color)`, `gx_width/height`, `gx_get`,
`gx_fill_rect`, `gx_line` (Брезенхэм), `gx_circle(.., filled)`, `gx_text(img, x, y, text, color, scale)`
(шрифт 5x7, ячейка 6x8, не-ASCII → `?`), `gx_text_width`, `gx_blit`, `gx_checksum` (FNV-1a);
смешивание source-over целочисленное. `file_write_png(path, img)` — PNG RGBA (deflate stored).

## G1: `std.ui`

`enum Widget { Label, Button, Check, Field, Bar, Spacer, Column, Row, Panel }`, `Theme`, `measure`,
`layout` → `List<Placed>`, `render`/`screenshot`/`save_png`, `hit` → `UiEvent` (`Press`, `Toggle`, `Input`,
`Pointer`), `apply_events`, `click`; `to_html`, `route_event` (`/e/press/<id>`, `/e/toggle/<id>`,
`/e/input/<id>?v=`), `serve_once`, `serve_app(listener, theme, model, view, update, title, count, timeout)`.
Пример: `examples/valid/gui_counter.dlm`. Язык: рекурсия по элементам списка внутри записи-меры через
`map`/`fold` с лямбдой (`decreasing w`). Тест: `tests/gui.rs`.

## G2/G3 (v0.128)

Сцена `vx_*` — `NatVec` команд в 1/16 px: `1` заливка пути (nonzero/evenodd), `2` обводка (круглые стыки),
`3` скруглённый прямоугольник, `4` растр, `5`/`6` клип, `7` текст, `8` эллипс; краски: сплошная, линейный и
радиальный градиент; пути `move/line/quad/cubic/close`. Растеризация целочисленная (точное горизонтальное
покрытие × 16 подстрок), пиксели совпадают в интерпретаторе и C. `vx_render(scene, w, h, num, den)`,
`vx_render_region` (перерисовка прямоугольника), `vx_bounds`, `vx_svg`, `vx_text`, `vx_text_width`.
Шрифт 5x7 расширен кириллицей (также в `gx_text`); векторный текст — штрихи по сетке глифа.

`std.vector`: построители путей/команд, векторный вид виджетов `std.ui` с состояниями `VState(hover, pressed,
disabled)`, `vector_render` (любой масштаб), `vector_svg`, `vector_png`, `vector_chunks` + `dirty` + `redraw`
(инкрементально), `check_ui(init, update, events, invariant, depth)` — перебор всех последовательностей событий
с контрпримером, `vector_page`/`serve_vector_app` (SVG в браузере), `run_window` (нативное окно).

Окна: `win_open(title, w, h)`, `win_present(id, img)`, `win_poll(id, ms)` → `[kind, a, b, c]` (0 нет, 1 закрытие,
2/3 кнопка, 4 движение, 5 клавиша: код Unicode или 0x10000+keysym, 6 размер, 7 expose), `win_size`, `win_close`.
Тест `tests/gui_vector.rs` (окно — под Xvfb с внешним вводом событий).

## v0.129: шрифты TrueType, все письменности, списки и прокрутка

Шрифты: `file_font_load(path)` → id (свой парсер `head/hhea/maxp/hmtx/cmap 4,12/loca/glyf`, составные глифы,
TTC — первое начертание; CFF/OTF отклоняется), `file_font_find(name)` (поиск `.ttf/.ttc` в системных каталогах),
`vx_font_has`, `vx_font_advance`, `vx_font_metrics` → `[upem, ascender, |descender|, glyphs]`, `vx_font_width`.
Команда сцены `9 x y size max_w nf fonts… paint n cp…` (`vx_text_fonts`): глиф берётся из первого шрифта,
где он есть, иначе штриховой глиф/`?`; шире `max_w` — сжатие по горизонтали; контуры TrueType (квадратичные)
заливаются nonzero; SVG — `Q`-пути. Построитель упорядочивает строку для показа: упрощённый bidi (направление
абзаца по первому сильному символу, нейтральные, числа слева направо, отражение скобок, комбинирующие знаки
остаются при основе) и арабские контекстные формы (Presentation Forms A/B, лам-алиф). Без полноценного
shaping (лигатуры Indic, позиционирование знаков GPOS). `vx_codepoints`, `vx_from_codepoints`; в текстовых
литералах `\u{1F600}` и `\x41`. Интерпретатор и C дают одинаковые пиксели и SVG.

`std.ui`: `Scroll(id, height, offset, kids)`, `ListView(id, items, selected, rows, offset, chars)`,
`Table(id, headers, rows, widths, selected, visible, offset)` — прокрутка целыми строками; `Placed` виды 7–11
(контейнер, конец клипа, строка, ячейка, заголовок); события `Select(id, index)`, `Wheel(id, down)`; `wheel_at`,
`scrolled`, `follow`, `thumb_rect`; HTML: `/e/select/<n>/<id>`, `/e/wheel/{up,down}/<id>`. Варианты `measure_f`,
`layout_f`, `vector_*_f`, `vector_page_f`, `run_window_f` принимают id шрифтов (старые функции = без шрифтов).

Окна: ввод текста через XIM (`Xutf8LookupString`, локаль UTF-8), Win32 — Unicode-окно, суррогатные пары
`WM_CHAR`; событие 8 — колесо `(x, y, 1 вверх / 2 вниз / 3 влево / 4 вправо)`; клавиши без текста:
функциональные `0x10000 + keysym (0xFE00..0xFFFF)`, прочие `0x110000 + keysym`; заголовок `_NET_WM_NAME` UTF-8.
В окне: колесо прокручивает ближайший контейнер, клик по строке выделяет её, стрелки/PgUp/PgDn/Home/End
двигают выделение в списке с фокусом. Тест `tests/gui_text.rs`.

## v0.131: аппаратное ускорение (GPU, OpenGL)

`win_gpu(id)` → `Result<Text, Text>`: окно переходит на OpenGL (Linux — GLX через `libGL.so.1`, дочернее окно
поверх родителя, ввод по-прежнему у родителя; Windows — WGL через `opengl32.dll`, `CS_OWNDC`), ответ — версия,
рендерер и производитель драйвера. Сглаживание: MSAA-цель (FBO, до 8 сэмплов) или многосэмпловый формат окна.
`win_present_scene(id, scene, w, h, num, den)` → `Ok(1)`: сцена `vx_*` рисуется видеокартой; у окна без
`win_gpu` — программный путь (`vx_render` + `win_present`, `Ok(0)`). `win_read(id)` → последний кадр GPU как
изображение (повторная отрисовка в задний буфер и `glReadPixels`).

Отрисовка: контуры те же, что у программного растеризатора (кривые, обводки, глифы TrueType уже разбиты на
многоугольники); выпуклый одиночный контур — веер треугольников, остальное — stencil-then-cover (nonzero:
INCR_WRAP/DECR_WRAP по ориентации граней, even-odd: INVERT); линейный/радиальный градиент — текстура-рампа и
`GL_COMBINE`/`INTERPOLATE`; клип — scissor; растр — текстура NEAREST. Используются функции OpenGL 1.1 + 1.4
(wrap-стенсил) + 3.0 FBO (необязательно), без шейдеров — работает на любом драйвере. Пиксели GPU не побайтно
равны программным (другое сглаживание); тест сравнивает среднее отклонение. Тест `tests/gui_gpu.rs`.
