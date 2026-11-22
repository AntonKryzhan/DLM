// Software-rasterized vector scene. Logical size 1200 x 800, units 1/16 px.
module universe.view
import universe.core

theory UniverseView extends UniverseCore {
    record UButton(id: Text, label: Text, x: Nat, y: Nat, w: Nat, h: Nat)
    // Pairwise concatenation keeps large immutable scenes from copying every
    // preceding particle again on each append. Order is identical to cat_all.
    fn u_merge_commands(xs: List<NatVec>) -> List<NatVec> decreasing xs =
        match xs { [] => xs, cons(a, rest) => match rest { [] => list(a), cons(b, more) => cons(cat(a, b), u_merge_commands(more)) } }
    fn u_join_round(xs: List<NatVec>, rounds: Nat) -> NatVec decreasing rounds =
        if_then_else(nat_eq(rounds, 0), cat_all(xs),
            if_then_else(is_empty(tail(xs)), unwrap_or(head(xs), nv_empty()), u_join_round(u_merge_commands(xs), pred(rounds))))
    fn u_join(xs: List<NatVec>) -> NatVec = u_join_round(xs, 16)
    fn u_bg() -> Nat = 135340031
    fn u_ink() -> Nat = 3891198207
    fn u_muted() -> Nat = 2174004991
    fn u_accent() -> Nat = 1608773631
    fn u_color(kind: Nat) -> Nat = unwrap_or(index(list(1608773631, 4287604991, 4124475903, 2996568063, 2380314367), mod(kind, 5)), u_accent())
    fn u_alpha(color: Nat, a: Nat) -> Nat = mul(div(color, 256), 256) + ui_min_v(a, 255)
    fn u_mode_name(mode: Nat) -> Text = unwrap_or(index(list("Искусственная жизнь", "Млечный Путь", "Упругая сеть", "Морские волны", "Аттрактор Лоренца", "Космическая паутина", "Квантовое поле", "Чёрная дыра"), mode), "DLM Universe")
    fn u_mode_short(mode: Nat) -> Text = unwrap_or(index(list("Жизнь", "Млечный Путь", "Упругие сети", "Море", "Лоренц", "Вселенная", "Квантовый мир", "Чёрная дыра"), mode), "")
    fn u_count_label(s: USettings) -> Text =
        if_then_else(field(s, count) > 400,
            concat("GPU: ", concat(nat_to_text(div(field(s, count), 1000000)), " млн")),
            concat("Объекты: ", nat_to_text(field(s, count))))
    fn u_mode_detail(mode: Nat) -> Text = unwrap_or(index(list("Пять видов. Простые правила. Сложное поведение.", "Бар, рукава, звёздные ассоциации и пылевые полосы.", "Связанные пружинами узлы передают возмущения.", "Спектр моря: зыбь, короткие волны и капиллярная рябь.", "Хаотическая система. Близкие траектории расходятся.", "Кластеры, группы и филаменты. Статистическая карта.", "Два пакета, две щели. Уравнение Гросса–Питаевского.", "Линзирование и неоднородный вращающийся аккреционный диск."), mode), "")
    fn u_quality_label(a: UApp) -> Text =
        let w = field(a, world) in let s = field(a, settings) in let n = field(s, count) in
        if_then_else(nat_eq(field(w, mode), 6), if_then_else(n > 4999999, "Поле: 1024×768", if_then_else(n > 999999, "Поле: 512×384", "Поле: 256×192")),
        if_then_else(or(nat_eq(field(w, mode), 3), nat_eq(field(w, mode), 7)), if_then_else(n > 4999999, "Качество: высокое", if_then_else(n > 999999, "Качество: среднее", "Качество: лёгкое")),
        if_then_else(and(or(nat_eq(field(w, mode), 1), nat_eq(field(w, mode), 5)), and(field(s, gpu), 401 > n)), concat("GPU: ", concat(nat_to_text(div(u_gpu_count(a), 1000)), " тыс")), u_count_label(s))))
    fn u_field_label(a: UApp) -> Text =
        let mode = field(field(a, world), mode) in let enabled = field(field(a, settings), fields) in
        if_then_else(nat_eq(mode, 5), if_then_else(enabled, "Масштаб: локальные 500 Мпк", "Обзор: до 14,2 Гпк"),
        if_then_else(nat_eq(mode, 6), if_then_else(enabled, "Взаимодействие: g = 650", "Взаимодействие: g = 0"),
        if_then_else(nat_eq(mode, 7), if_then_else(enabled, "Оптика: мягкий ореол", "Оптика: без ореола"),
        if_then_else(nat_eq(mode, 3), if_then_else(enabled, "Вид: морская поверхность", "Вид: нормали поверхности"),
        if_then_else(nat_eq(mode, 1), if_then_else(enabled, "Солнечная орбита: да", "Солнечная орбита: нет"),
        if_then_else(enabled, "Геометрия: включена", "Геометрия: выключена"))))))
    fn u_trails_label(a: UApp) -> Text =
        let s = field(a, settings) in let mode = field(field(a, world), mode) in let enabled = field(s, trails) in
        if_then_else(nat_eq(mode, 3), if_then_else(enabled, "Пена: да", "Пена: нет"),
        if_then_else(nat_eq(mode, 6), if_then_else(enabled, "Фаза: да", "Фаза: нет"),
        if_then_else(nat_eq(mode, 7), if_then_else(enabled, "Звёзды: да", "Звёзды: нет"),
        if_then_else(nat_eq(mode, 5), if_then_else(field(s, fields), if_then_else(enabled, "Слой: 110 Мпк", "Слой: 500 Мпк"), if_then_else(enabled, "Лин. срез", "Лог. срез")),
        if_then_else(u_native_scene(a), "Следы: GPU", if_then_else(enabled, "Следы: да", "Следы: нет"))))))
    fn u_scale(w: Nat, h: Nat) -> Nat = ui_max_v(1, ui_min_v(div(mul(w, 1000), 1200), div(mul(h, 1000), 800)))
    fn u_ox(w: Nat, h: Nat) -> Nat = div(monus(div(mul(w, 1000), u_scale(w, h)), 1200), 2)
    fn u_oy(w: Nat, h: Nat) -> Nat = div(monus(div(mul(h, 1000), u_scale(w, h)), 800), 2)
    fn u_text(fonts: NatVec, x: Nat, y: Nat, size: Nat, width: Nat, color: Nat, text: Text) -> NatVec = font_text_cmd(px(x), px(y), px(size), px(width), fonts, solid(color), text)
    fn u_screen(p: UVec, c: UCamera, ox: Nat, oy: Nat) -> UVec =
        UVec(fl_add(fl_of_nat(ox + 24), fl_mul(0.864, fl_add(500.0, fl_mul(fl_sub(field(p, x), field(c, x)), field(c, zoom))))),
            fl_add(fl_of_nat(oy + 100), fl_mul(0.864, fl_add(350.0, fl_mul(fl_sub(field(p, y), field(c, y)), field(c, zoom))))))
    fn u_coord(x: Float) -> Nat = u_nat(fl_mul(x, 16.0))
    fn u_visible(p: UVec, ox: Nat, oy: Nat) -> Bool =
        and(and(fl_le(fl_of_nat(ox + 24), field(p, x)), fl_le(field(p, x), fl_of_nat(ox + 888))), and(fl_le(fl_of_nat(oy + 100), field(p, y)), fl_le(field(p, y), fl_of_nat(oy + 705))))
    fn u_line(a: UVec, b: UVec, width: Nat, color: Nat) -> NatVec =
        stroke_path(width, solid(color), line_to(move_to(path_start(), u_coord(field(a, x)), u_coord(field(a, y))), u_coord(field(b, x)), u_coord(field(b, y))))
    fn u_dot(p: UVec, radius: Nat, color: Nat) -> NatVec = ellipse_cmd(u_coord(field(p, x)), u_coord(field(p, y)), radius, radius, solid(color))
    fn u_buttons(a: UApp) -> List<UButton> =
        let s = field(a, settings) in
        list(UButton("m0", "01   Жизнь частиц", 928, 156, 232, 28), UButton("m1", "02   Млечный Путь", 928, 187, 232, 28),
            UButton("m2", "03   Упругие сети", 928, 218, 232, 28), UButton("m3", "04   Морские волны", 928, 249, 232, 28), UButton("m4", "05   Лоренц", 928, 280, 232, 28),
            UButton("m5", "06   Карта Вселенной", 928, 311, 232, 28), UButton("m6", "07   Квантовый мир", 928, 342, 232, 28), UButton("m7", "08   Чёрная дыра", 928, 373, 232, 28),
            UButton("auto", if_then_else(field(s, automatic), "Автопоказ   •   включён", "Автопоказ   •   выключен"), 928, 422, 232, 38),
            UButton("pause", if_then_else(field(s, paused), "Продолжить", "Пауза"), 928, 476, 110, 38), UButton("reset", "Заново", 1050, 476, 110, 38),
            UButton("camera", "Камера", 928, 524, 110, 38), UButton("trails", u_trails_label(a), 1050, 524, 110, 38),
            UButton("quality", u_quality_label(a), 928, 572, 146, 36), UButton("speed", concat("×", nat_to_text(field(s, speed))), 1086, 572, 74, 36),
            UButton("fields", u_field_label(a), 928, 620, 232, 34))
    fn u_hit(a: UApp, x: Nat, y: Nat) -> Text =
        fold(u_buttons(a), "", fun(found: Text, b: UButton, if_then_else(inside(Rect(field(b, x), field(b, y), field(b, w), field(b, h)), x, y), field(b, id), found)))
    fn u_button(a: UApp, b: UButton, fonts: NatVec, ox: Nat, oy: Nat) -> NatVec =
        let id = field(b, id) in let selected = text_eq(id, concat("m", nat_to_text(field(field(a, world), mode)))) in
        let active = or(selected, and(text_eq(id, "auto"), field(field(a, settings), automatic))) in
        let hover = text_eq(field(a, hover), id) in let down = text_eq(field(a, pressed), id) in
        let base = if_then_else(active, 389762047, 354564351) in
        let color = if_then_else(down, darken(base, 30), if_then_else(hover, lighten(base, 18), base)) in
        u_join(list(rrect(px(ox + field(b, x)), px(oy + field(b, y)), px(field(b, w)), px(field(b, h)), px(9), solid(color)),
            if_then_else(selected, rrect(px(ox + field(b, x)), px(oy + field(b, y) + 5), px(3), px(monus(field(b, h), 10)), px(1), solid(u_accent())), nv_empty()),
            u_text(fonts, ox + field(b, x) + 14, oy + field(b, y) + div(monus(field(b, h), 14), 2), 14, monus(field(b, w), 28), if_then_else(active, u_accent(), u_ink()), field(b, label))))
    fn u_stars(ox: Nat, oy: Nat) -> NatVec = u_join(map(u_indices(85), fun(i: Nat,
        u_dot(UVec(fl_of_nat(ox + 32 + u_nat(u_range(821, mul(i, 2), 0.0, 844.0))), fl_of_nat(oy + 112 + u_nat(u_range(821, mul(i, 2) + 1, 0.0, 578.0)))),
            if_then_else(nat_eq(mod(i, 9), 0), 16, 8), u_alpha(2023276799, if_then_else(nat_eq(mod(i, 9), 0), 100, 45))))))
    fn u_grid(c: UCamera, ox: Nat, oy: Nat) -> NatVec =
        u_join(map(u_indices(13), fun(i: Nat, let x = fl_mul(fl_of_nat(i), 100.0) in
            cat(u_line(u_screen(UVec(x, 0.0), c, ox, oy), u_screen(UVec(x, 700.0), c, ox, oy), 8, 692414517),
                if_then_else(8 > i, u_line(u_screen(UVec(0.0, x), c, ox, oy), u_screen(UVec(1000.0, x), c, ox, oy), 8, 692414517), nv_empty())))))
    fn u_particle(p: UParticle, mode: Nat, tick: Nat, c: UCamera, ox: Nat, oy: Nat, age: Nat) -> NatVec =
        let pos = u_screen(u_position(p, mode, tick), c, ox, oy) in
        if_then_else(not(u_visible(pos, ox, oy)), nv_empty(),
            let kind = if_then_else(nat_eq(mode, 3), mod(u_nat(fl_add(field(p, z), 40.0)), 5), field(p, kind)) in
            let color = u_color(kind) in let radius = ui_max_v(18, u_nat(fl_mul(30.0, field(c, zoom)))) in
            if_then_else(age > 0, u_dot(pos, ui_max_v(10, monus(radius, mul(age, 2))), u_alpha(color, div(90, age + 1))),
                u_join(list(u_dot(pos, mul(radius, 3), u_alpha(color, 16)), u_dot(pos, radius + 8, u_alpha(color, 100)), u_dot(pos, radius, color), u_dot(pos, div(radius, 2), u_alpha(4294967295, 200))))))
    fn u_trails(w: UWorld, c: UCamera, ox: Nat, oy: Nat) -> NatVec =
        u_join(map(list(5, 3, 1), fun(i: Nat, match index(field(w, history), i) { none => nv_empty(), some(ps) =>
            u_join(map(ps, fun(p: UParticle, u_particle(p, field(w, mode), monus(field(w, tick), i + 1), c, ox, oy, i + 1)))) })))
    fn u_connections(w: UWorld, c: UCamera, ox: Nat, oy: Nat) -> NatVec =
        let mode = field(w, mode) in let ps = field(w, points) in let data = u_pack(ps) in let n = length(ps) in
        if_then_else(nat_eq(mode, 0),
            u_join(map(u_take(u_indices(div(nv_len(field(w, pairs)), 2)), 500), fun(k: Nat,
                let i = unwrap_or(nv_get(field(w, pairs), mul(k, 2)), 0) in let j = unwrap_or(nv_get(field(w, pairs), mul(k, 2) + 1), 0) in
                let ax = u_get(data, mul(i, 3)) in let ay = u_get(data, mul(i, 3) + 1) in let bx = u_get(data, mul(j, 3)) in let by = u_get(data, mul(j, 3) + 1) in
                let d2 = fl_add(fl_mul(fl_sub(ax, bx), fl_sub(ax, bx)), fl_mul(fl_sub(ay, by), fl_sub(ay, by))) in
                if_then_else(fl_lt(d2, 2025.0), u_line(u_screen(UVec(ax, ay), c, ox, oy), u_screen(UVec(bx, by), c, ox, oy), 9, u_alpha(u_color(mod(i, 5)), 45)), nv_empty())))),
        if_then_else(or(nat_eq(mode, 2), nat_eq(mode, 3)), u_join(map(ps, fun(p: UParticle,
            let i = field(p, id) in let here = u_screen(UVec(field(p, x), field(p, y)), c, ox, oy) in
            cat(if_then_else(and(19 > mod(i, 20), n > i + 1), u_line(here, u_screen(UVec(u_get(data, mul(i + 1, 3)), u_get(data, mul(i + 1, 3) + 1)), c, ox, oy), 12, 1608773477), nv_empty()),
                if_then_else(n > i + 20, u_line(here, u_screen(UVec(u_get(data, mul(i + 20, 3)), u_get(data, mul(i + 20, 3) + 1)), c, ox, oy), 12, 2996567888), nv_empty()))))), nv_empty()))
    fn u_pointer_art(a: UApp, ox: Nat, oy: Nat) -> NatVec =
        let m = field(a, pointer) in let p = u_screen(UVec(field(m, x), field(m, y)), field(a, camera), ox, oy) in
        if_then_else(and(and(field(m, inside), field(m, button) > 0), or(nat_eq(field(field(a, world), mode), 0), or(nat_eq(field(field(a, world), mode), 2), nat_eq(field(field(a, world), mode), 6)))),
            let color = if_then_else(nat_eq(field(m, button), 1), u_accent(), u_color(1)) in
            u_join(list(u_dot(p, 144, u_alpha(color, 30)), u_line(UVec(fl_sub(field(p, x), 8.0), field(p, y)), UVec(fl_add(field(p, x), 8.0), field(p, y)), 18, color),
                u_line(UVec(field(p, x), fl_sub(field(p, y), 8.0)), UVec(field(p, x), fl_add(field(p, y), 8.0)), 18, color))), nv_empty())
    // Compact command 10: GPU particles (count/mode/seed/tick/camera/pointer/canvas).
    // The native renderer consumes this opcode without allocating per-particle DLM objects.
    fn u_gpu_particles(a: UApp, ox: Nat, oy: Nat) -> NatVec =
        let w = field(a, world) in let c = field(a, camera) in let p = field(a, pointer) in let s = field(a, settings) in
        nvl(list(10, u_gpu_count(a), field(w, mode), field(w, seed), field(w, tick),
            u_nat(fl_mul(fl_add(field(c, x), 4096.0), 1000.0)),
            u_nat(fl_mul(fl_add(field(c, y), 4096.0), 1000.0)),
            u_nat(fl_mul(field(c, zoom), 1000.0)),
            u_nat(fl_mul(fl_add(field(p, x), 4096.0), 1000.0)),
            u_nat(fl_mul(fl_add(field(p, y), 4096.0), 1000.0)),
            if_then_else(field(p, inside), field(p, button), 0),
            px(ox + 24), px(oy + 100), px(864), px(605), field(s, speed) + if_then_else(field(s, fields), 4, 0) + if_then_else(field(s, trails), 8, 0)))
    fn u_model_hud(a: UApp, fonts: NatVec, ox: Nat, oy: Nat) -> NatVec =
        let mode = field(field(a, world), mode) in let s = field(a, settings) in let tick = field(field(a, world), tick) in
        let line = if_then_else(nat_eq(mode, 1), concat("Время: ", concat(nat_to_text(div(tick, 4)), " млн лет  •  кпк  •  диск, гало и вращающийся бар")),
            if_then_else(nat_eq(mode, 3), concat("Время: ", concat(nat_to_text(div(mul(tick, 45), 1000)), " с  •  глубина 50 м  •  пик спектра 8 с")),
            if_then_else(nat_eq(mode, 5), if_then_else(field(s, fields), "Мпк • скопления и филаменты • F — логарифмический обзор", if_then_else(field(s, trails), "Линейный срез до 14,2 Гпк • колесо: приближение до ×32 • не каталог наблюдений", "Логарифмический срез до 14,2 Гпк • не каталог наблюдений")),
            if_then_else(nat_eq(mode, 6), "Яркость: |ψ|²  •  цвет: фаза  •  F: взаимодействие  •  безразмерные единицы",
            if_then_else(nat_eq(mode, 7), "G = M = c = 1  •  горизонт r = 2  •  ISCO r = 6  •  невращающаяся дыра", ""))))) in
        if_then_else(text_length(line) > 0, u_join(list(rrect(px(ox + 40), px(oy + 662), px(832), px(28), px(6), solid(118299115)),
            u_text(fonts, ox + 52, oy + 670, 11, 805, u_muted(), line))), nv_empty())
    fn u_map_marker(a: UApp, fonts: NatVec, ox: Nat, oy: Nat, p: UVec, label: Text, color: Nat) -> NatVec =
        let pos = u_screen(p, field(a, camera), ox, oy) in
        if_then_else(u_visible(pos, ox, oy), u_join(list(u_dot(pos, 45, color), u_text(fonts, u_nat(field(pos, x)) + 7, u_nat(field(pos, y)), 11, 240, color, label))), nv_empty())
    fn u_horizon_locator(a: UApp, fonts: NatVec, ox: Nat, oy: Nat) -> NatVec =
        let camera = field(a, camera) in let zoom = field(camera, zoom) in
        let width = ui_max_v(4, ui_min_v(68, u_nat(fl_div(127.27, zoom)))) in
        let height = ui_max_v(3, ui_min_v(68, u_nat(fl_div(89.09, zoom)))) in
        let cx = u_nat(u_clamp(fl_add(816.0, fl_mul(fl_sub(field(camera, x), 500.0), 0.12727)), fl_add(781.0, fl_div(fl_of_nat(width), 2.0)), fl_sub(851.0, fl_div(fl_of_nat(width), 2.0)))) in
        let cy = u_nat(u_clamp(fl_add(590.0, fl_mul(fl_sub(field(camera, y), 350.0), 0.12727)), fl_add(555.0, fl_div(fl_of_nat(height), 2.0)), fl_sub(625.0, fl_div(fl_of_nat(height), 2.0)))) in
        u_join(list(rrect(px(ox + 763), px(oy + 535), px(106), px(111), px(8), solid(118299115)),
            ellipse_cmd(px(ox + 816), px(oy + 590), px(36), px(36), solid(u_alpha(u_muted(), 85))),
            ellipse_cmd(px(ox + 816), px(oy + 590), px(35), px(35), solid(135340031)),
            rrect(px(ox + monus(cx, div(width, 2))), px(oy + monus(cy, div(height, 2))), px(width), px(height), px(1), solid(u_alpha(u_accent(), 140))),
            u_text(fonts, ox + 779, oy + 631, 10, 82, u_muted(), "14,2 Гпк • схема")))
    fn u_map_hud(a: UApp, fonts: NatVec, ox: Nat, oy: Nat) -> NatVec =
        if_then_else(nat_eq(field(field(a, world), mode), 5),
            if_then_else(field(field(a, settings), fields), u_join(list(
                u_map_marker(a, fonts, ox, oy, UVec(500.0, 350.0), "Млечный Путь", u_ink()),
                u_map_marker(a, fonts, ox, oy, UVec(562.0, 269.4), "Великий аттрактор / Норма ~70 Мпк", u_color(1)),
                u_map_marker(a, fonts, ox, oy, UVec(531.0, 368.6), "Местный войд ~23 Мпк", u_accent()),
                u_map_marker(a, fonts, ox, oy, UVec(283.0, 497.25), "Войд (условный)", u_color(2)),
                u_map_marker(a, fonts, ox, oy, UVec(701.5, 210.5), "Сверхскопление (условное)", u_color(4)))),
                u_join(list(u_map_marker(a, fonts, ox, oy, UVec(500.0, 350.0), "Наблюдатель", u_ink()), u_horizon_locator(a, fonts, ox, oy), u_text(fonts, ox + 52, oy + 638, 10, 690, u_muted(), if_then_else(field(field(a, settings), trails), "Срез увеличен: скопления и филаменты. Колесо — масштаб; T — лог. расстояния.", "Лог. расстояния растягивают угловые размеры дальних узлов. T — линейная проекция."))))), nv_empty())
    fn u_mw_hud(a: UApp, fonts: NatVec, ox: Nat, oy: Nat) -> NatVec =
        if_then_else(and(nat_eq(field(field(a, world), mode), 1), field(field(a, settings), fields)),
            let camera = field(a, camera) in
            let omega = fl_sqrt(fl_div(fl_neg(field(u_mw_force(8.2, 0.0), x)), 8.2)) in
            let phase = fl_neg(fl_mul(fl_mul(fl_of_nat(field(field(a, world), tick)), 0.25), omega)) in
            u_join(list(u_join(map(u_indices(96), fun(i: Nat,
                let p = fl_mul(fl_of_nat(i), 0.06544984694978735) in let q = fl_add(p, 0.06544984694978735) in
                u_line(u_screen(UVec(fl_add(500.0, fl_mul(172.2, fl_cos(p))), fl_add(350.0, fl_mul(123.984, fl_sin(p)))), camera, ox, oy),
                    u_screen(UVec(fl_add(500.0, fl_mul(172.2, fl_cos(q))), fl_add(350.0, fl_mul(123.984, fl_sin(q)))), camera, ox, oy), 8, u_alpha(u_color(4), 60))))),
                u_map_marker(a, fonts, ox, oy, UVec(fl_add(500.0, fl_mul(172.2, fl_cos(phase))), fl_add(350.0, fl_mul(123.984, fl_sin(phase)))), "Солнечная орбита ~8,2 кпк", u_color(4)))), nv_empty())
    fn u_canvas(a: UApp, fonts: NatVec, ox: Nat, oy: Nat) -> NatVec =
        let w = field(a, world) in let s = field(a, settings) in let c = field(a, camera) in
        u_join(list(
            clip_cmd(px(ox + 24), px(oy + 100), px(864), px(605)),
            rrect(px(ox + 24), px(oy + 100), px(864), px(605), 0, solid(118299135)), if_then_else(nat_eq(field(w, mode), 5), nv_empty(), u_stars(ox, oy)),
            if_then_else(and(field(s, fields), or(nat_eq(field(w, mode), 0), nat_eq(field(w, mode), 2))), u_grid(c, ox, oy), nv_empty()),

            if_then_else(u_native_scene(a), nv_empty(), if_then_else(field(s, trails), u_trails(w, c, ox, oy), nv_empty())),
            if_then_else(u_native_scene(a), nv_empty(), if_then_else(field(s, fields), u_connections(w, c, ox, oy), nv_empty())),
            if_then_else(u_native_scene(a), u_gpu_particles(a, ox, oy),
                if_then_else(field(w, mode) > 4, u_text(fonts, ox + 175, oy + 390, 18, 600, u_accent(), "Этой сцене нужен GPU. Запусти без --cpu."),
                u_join(map(field(w, points), fun(p: UParticle, u_particle(p, field(w, mode), field(w, tick), c, ox, oy, 0)))))), u_pointer_art(a, ox, oy), u_map_hud(a, fonts, ox, oy), u_mw_hud(a, fonts, ox, oy), unclip_cmd(),
            rrect(px(ox + 40), px(oy + 116), px(470), px(60), px(9), solid(118299110)),
            u_text(fonts, ox + 54, oy + 128, 19, 440, u_ink(), u_mode_name(field(w, mode))),
            u_text(fonts, ox + 54, oy + 154, 11, 440, u_muted(), u_mode_detail(field(w, mode))),
            u_model_hud(a, fonts, ox, oy)))
    fn u_scene(a: UApp, fonts: NatVec, width: Nat, height: Nat) -> NatVec =
        let w = field(a, world) in let s = field(a, settings) in let c = field(a, camera) in
        let ox = u_ox(width, height) in let oy = u_oy(width, height) in let scale = u_scale(width, height) in
        let world_w = div(mul(width, 1000) + pred(scale), scale) in let world_h = div(mul(height, 1000) + pred(scale), scale) in
        u_join(list(rrect(0, 0, px(world_w), px(world_h), 0, solid(u_bg())),
            ellipse_cmd(px(ox + 42), px(oy + 44), px(17), px(17), solid(389762047)),
            u_dot(UVec(fl_of_nat(ox + 42), fl_of_nat(oy + 44)), 60, u_accent()),
            u_text(fonts, ox + 74, oy + 24, 27, 500, u_ink(), "DLM UNIVERSE"),
            u_text(fonts, ox + 75, oy + 59, 12, 600, u_muted(), "Живые системы из простых математических правил"),
            rrect(px(ox + 970), px(oy + 28), px(206), px(35), px(17), solid(354564351)),
            u_dot(UVec(fl_of_nat(ox + 988), fl_of_nat(oy + 45)), 37, if_then_else(field(s, paused), u_color(2), u_accent())),
            u_text(fonts, ox + 1002, oy + 38, 12, 158, u_ink(), if_then_else(field(s, paused), "СИМУЛЯЦИЯ НА ПАУЗЕ", if_then_else(u_native_scene(a), "GPU ВЫЧИСЛЕНИЯ", "ВЫЧИСЛЯЕТСЯ В DLM"))),
            rrect(px(ox + 23), px(oy + 99), px(866), px(607), px(14), solid(607472383)),
            u_canvas(a, fonts, ox, oy),
            rrect(px(ox + 912), px(oy + 100), px(264), px(605), px(14), solid(219621631)),
            u_text(fonts, ox + 928, oy + 122, 11, 232, u_muted(), "ВЫБЕРИ СИСТЕМУ ИЛИ ПРОСТО НАБЛЮДАЙ"),
            u_join(map(u_buttons(a), fun(b: UButton, u_button(a, b, fonts, ox, oy)))),
            u_text(fonts, ox + 930, oy + 674, 11, 230, u_muted(), concat("ШАГ ", concat(nat_to_text(field(w, tick)), concat("    •    ", if_then_else(field(c, automatic), "АВТОКАМЕРА", "РУЧНАЯ КАМЕРА"))))),
            u_text(fonts, ox + 26, oy + 733, 12, 1140, u_ink(), "ЛКМ / ПКМ — источник (частицы, квантовое поле)    X — убрать    Колесо / стрелки — камера    F — параметры сцены"),
            u_text(fonts, ox + 26, oy + 759, 11, 1140, u_muted(), "Пробел — пауза    1–8 — сцены    A — автопоказ    C — камера    T — следы    F — геометрия    R — заново    Esc — выход")))
}
