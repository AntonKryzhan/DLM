// Deterministic DLM models and state. GPU field/ray solvers live in native/.
module universe.core
import std.vector

theory UniverseCore extends StdVector {
    record UVec(x: Float, y: Float)
    record UParticle(id: Nat, x: Float, y: Float, z: Float, vx: Float, vy: Float, kind: Nat)
    record UWorld(points: List<UParticle>, mode: Nat, seed: Nat, tick: Nat, pairs: NatVec, energy: Float, history: List<List<UParticle>>)
    record UCamera(x: Float, y: Float, zoom: Float, automatic: Bool)
    record UPointer(x: Float, y: Float, button: Nat, inside: Bool)
    record USettings(paused: Bool, automatic: Bool, trails: Bool, fields: Bool, count: Nat, speed: Nat, gpu: Bool)
    record UApp(world: UWorld, settings: USettings, camera: UCamera, pointer: UPointer, hover: Text, pressed: Text)

    fn u_indices(n: Nat) -> List<Nat> = nv_to_list(unwrap_or(nv_range(0, n), nv_empty()))
    fn u_clamp(x: Float, low: Float, high: Float) -> Float = fl_max(low, fl_min(high, x))
    fn u_nat(x: Float) -> Nat = abs(unwrap_or(fl_to_int(fl_max(0.0, x)), int(0)))
    fn u_rand(seed: Nat, i: Nat) -> Float =
        let t = mod(mul(mod(seed, 2147483647) + i + 17, 48271), 2147483647) in
        fl_div(fl_of_nat(mod(mul(t, t + 11) + 97, 2147483647)), 2147483647.0)
    fn u_range(seed: Nat, i: Nat, lo: Float, hi: Float) -> Float = fl_add(lo, fl_mul(fl_sub(hi, lo), u_rand(seed, i)))
    fn u_camera() -> UCamera = UCamera(500.0, 350.0, 1.0, true)
    fn u_pointer() -> UPointer = UPointer(500.0, 350.0, 0, false)
    fn u_take<T>(xs: List<T>, n: Nat) -> List<T> decreasing n =
        if_then_else(nat_eq(n, 0), empty_list(T), match xs { [] => xs, cons(h, rest) => cons(h, u_take(rest, pred(n))) })
    fn u_mw_force(x: Float, y: Float) -> UVec =
        let r2 = fl_max(0.0004, fl_add(fl_mul(x, x), fl_mul(y, y))) in let r = fl_max(0.02, fl_sqrt(r2)) in
        let q = fl_div(r, 17.5) in
        let mh = fl_div(fl_mul(1000000000000.0, fl_sub(fl_log(fl_add(1.0, q)), fl_div(q, fl_add(1.0, q)))), 1.6418724343846136) in
        let disk = fl_div(68000000000.0, fl_mul(fl_add(r2, 10.7584), fl_sqrt(fl_add(r2, 10.7584)))) in
        let bulge = fl_div(5000000000.0, fl_mul(r, fl_mul(fl_add(r, 0.5), fl_add(r, 0.5)))) in
        let halo = fl_div(mh, fl_mul(r2, r)) in
        let k = fl_mul(-0.0000000000044985, fl_add(disk, fl_add(bulge, halo))) in UVec(fl_mul(k, x), fl_mul(k, y))
    fn u_normal(seed: Nat, i: Nat) -> Float =
        fl_mul(fl_sqrt(fl_mul(-2.0, fl_log(fl_max(0.00000001, u_rand(seed, i))))), fl_cos(fl_mul(6.283185307179586, u_rand(seed, i + 113))))
    fn u_mw_radius(seed: Nat, i: Nat) -> Float =
        // Invert the truncated exponential-disk CDF; no star is clamped onto an outer ring.
        let target = fl_mul(u_rand(seed, i), fl_sub(1.0, fl_mul(fl_add(1.0, fl_div(23.0, 2.65)), fl_exp(fl_div(-23.0, 2.65))))) in
        let interval = fold(u_indices(22), UVec(0.025, 23.0), fun(bounds: UVec, j: Nat,
            let r = fl_mul(0.5, fl_add(field(bounds, x), field(bounds, y))) in
            let cdf = fl_sub(1.0, fl_mul(fl_add(1.0, fl_div(r, 2.65)), fl_exp(fl_div(fl_neg(r), 2.65)))) in
            if_then_else(fl_lt(cdf, target), UVec(r, field(bounds, y)), UVec(field(bounds, x), r)))) in
        fl_mul(0.5, fl_add(field(interval, x), field(interval, y)))
    fn u_mw_force_at(x: Float, y: Float, time: Float) -> UVec =
        let angle = fl_add(0.48, fl_mul(-0.040, time)) in let c = fl_cos(angle) in let sn = fl_sin(angle) in
        let bx = fl_add(fl_mul(c, x), fl_mul(sn, y)) in let by = fl_sub(fl_mul(c, y), fl_mul(sn, x)) in
        let d = fl_add(16.0, fl_add(fl_mul(bx, bx), fl_mul(by, by))) in let q = fl_sub(fl_mul(bx, bx), fl_mul(by, by)) in
        let factor = fl_div(1.4, fl_mul(fl_mul(d, d), fl_sqrt(d))) in
        let fx = fl_mul(factor, fl_sub(fl_mul(2.0, bx), fl_div(fl_mul(fl_mul(5.0, q), bx), d))) in
        let fy = fl_mul(factor, fl_sub(fl_mul(-2.0, by), fl_div(fl_mul(fl_mul(5.0, q), by), d))) in
        let base = u_mw_force(x, y) in
        UVec(fl_add(field(base, x), fl_sub(fl_mul(c, fx), fl_mul(sn, fy))), fl_add(field(base, y), fl_add(fl_mul(sn, fx), fl_mul(c, fy))))
    fn u_point(mode: Nat, seed: Nat, i: Nat, n: Nat) -> UParticle =
        let kind = mod(i, 5) in let angle = u_range(seed, mul(i, 7), 0.0, 6.283185307179586) in
        let r = u_range(seed, mul(i, 7) + 1, 50.0, 310.0) in
        if_then_else(nat_eq(mode, 0),
            let group = mod(i, 7) in let ga = fl_mul(fl_of_nat(group), 0.8975979010256552) in
            UParticle(i, fl_add(fl_add(500.0, fl_mul(235.0, fl_cos(ga))), u_range(seed, mul(i, 7) + 2, -85.0, 85.0)),
                fl_add(fl_add(350.0, fl_mul(205.0, fl_sin(ga))), u_range(seed, mul(i, 7) + 3, -70.0, 70.0)), 0.0,
                u_range(seed, mul(i, 7) + 4, -1.0, 1.0), u_range(seed, mul(i, 7) + 5, -1.0, 1.0), kind),
        if_then_else(nat_eq(mode, 1),
            let radius = u_mw_radius(seed, mul(i, 7) + 1) in
            let spiral = fl_lt(0.67, u_rand(seed, mul(i, 7) + 6)) in
            let arm = if_then_else(fl_lt(u_rand(seed, mul(i, 7) + 21), 0.84), mod(i, 2), 2 + mod(i, 2)) in
            let armphase = unwrap_or(index(list(0.48, 3.62159265359, 1.97, 5.02), arm), 0.48) in
            let r = if_then_else(spiral, fl_add(2.8, fl_mul(radius, 0.76)), radius) in
            let theta = if_then_else(spiral, fl_add(armphase, fl_add(fl_div(fl_log(fl_div(r, 2.8)), 0.250019735), fl_mul(u_normal(seed, mul(i, 7) + 3), fl_add(0.085, fl_mul(0.011, r))))), angle) in
            let bar = fl_lt(u_rand(seed, mul(i, 7) + 6), 0.22) in
            let bx = fl_mul(1.75, u_normal(seed, mul(i, 7) + 1)) in let by = fl_mul(0.48, u_normal(seed, mul(i, 7) + 2)) in
            let x = if_then_else(bar, fl_sub(fl_mul(0.8869949228, bx), fl_mul(0.4617791755, by)), fl_mul(r, fl_cos(theta))) in
            let y = if_then_else(bar, fl_add(fl_mul(0.4617791755, bx), fl_mul(0.8869949228, by)), fl_mul(r, fl_sin(theta))) in
            let acc = u_mw_force(x, y) in let distance = fl_max(0.02, fl_sqrt(fl_add(fl_mul(x, x), fl_mul(y, y)))) in
            let vc = fl_sqrt(fl_max(0.0, fl_neg(fl_add(fl_mul(field(acc, x), x), fl_mul(field(acc, y), y))))) in
            UParticle(i, x, y, 0.0, fl_mul(vc, fl_div(y, distance)), fl_neg(fl_mul(vc, fl_div(x, distance))), kind),
        if_then_else(or(nat_eq(mode, 2), nat_eq(mode, 3)),
            let cols = 20 in let rows = div(n + 19, 20) in
            UParticle(i, fl_add(110.0, fl_mul(fl_of_nat(mod(i, cols)), 41.0)),
                fl_add(95.0, fl_mul(fl_of_nat(div(i, cols)), fl_div(510.0, fl_of_nat(ui_max_v(1, pred(rows)))))), 0.0, 0.0, 0.0, kind),
            UParticle(i, u_range(seed, mul(i, 7) + 1, -12.0, 12.0), u_range(seed, mul(i, 7) + 2, -12.0, 12.0),
                u_range(seed, mul(i, 7) + 3, 8.0, 38.0), 0.0, 0.0, kind)) ))
    fn u_create(mode: Nat, seed: Nat, count: Nat) -> UWorld =
        let n = ui_max_v(40, ui_min_v(count, 400)) in let k = mod(mode, 8) in let normalized = mod(seed, 2147483647) in
        UWorld(map(u_indices(n), fun(i: Nat, u_point(k, normalized, i, n))), k, normalized, 0, nv_empty(), 0.0, empty_list(List(UParticle)))
    fn u_particle_count(count: Nat) -> Nat = if_then_else(count > 999999, ui_min_v(count, 16000000), ui_max_v(40, ui_min_v(count, 400)))
    fn u_initial(count: Nat, seed: Nat) -> UApp = UApp(u_create(0, seed, count), USettings(false, true, true, true, u_particle_count(count), 1, false), u_camera(), u_pointer(), "", "")
    fn u_with_world(a: UApp, w: UWorld) -> UApp = UApp(w, field(a, settings), field(a, camera), field(a, pointer), field(a, hover), field(a, pressed))
    fn u_with_settings(a: UApp, s: USettings) -> UApp = UApp(field(a, world), s, field(a, camera), field(a, pointer), field(a, hover), field(a, pressed))
    fn u_enable_gpu(a: UApp, enabled: Bool) -> UApp =
        let s = field(a, settings) in
        u_with_settings(a, USettings(field(s, paused), field(s, automatic), field(s, trails), field(s, fields), field(s, count), field(s, speed), enabled))
    fn u_with_pointer(a: UApp, p: UPointer) -> UApp = UApp(field(a, world), field(a, settings), field(a, camera), p, field(a, hover), field(a, pressed))
    fn u_with_hover(a: UApp, hover: Text, pressed: Text) -> UApp = UApp(field(a, world), field(a, settings), field(a, camera), field(a, pointer), hover, pressed)
    fn u_with_camera(a: UApp, c: UCamera) -> UApp = UApp(field(a, world), field(a, settings), c, field(a, pointer), field(a, hover), field(a, pressed))
    fn u_select(a: UApp, mode: Nat) -> UApp =
        let w = field(a, world) in let s = field(a, settings) in
        UApp(u_create(mode, mod(field(w, seed) + 7919, 2147483647), field(s, count)), s, if_then_else(nat_eq(mod(mode, 8), 5), UCamera(500.0, 350.0, if_then_else(field(s, fields), 1.45, 16.0), true), u_camera()), u_pointer(), field(a, hover), "")
    fn u_position(p: UParticle, mode: Nat, tick: Nat) -> UVec =
        if_then_else(nat_eq(mode, 1), UVec(fl_add(500.0, fl_mul(21.0, field(p, x))), fl_add(350.0, fl_mul(15.12, field(p, y)))),
        if_then_else(nat_eq(mode, 4),
            let angle = fl_mul(fl_of_nat(mod(tick, 20000)), 0.0015) in
            UVec(fl_add(500.0, fl_mul(14.0, fl_add(fl_mul(field(p, x), fl_cos(angle)), fl_mul(field(p, y), fl_sin(angle))))), fl_sub(675.0, fl_mul(12.5, field(p, z)))),
            UVec(field(p, x), field(p, y))))
    fn u_pack(ps: List<UParticle>) -> FloatVec =
        fold(ps, fv_empty(), fun(v: FloatVec, p: UParticle, unwrap_or(fv_concat(v, unwrap_or(fv_from_list(list(field(p, x), field(p, y), fl_of_nat(field(p, kind)))), fv_empty())), v)))
    fn u_get(v: FloatVec, i: Nat) -> Float = unwrap_or(fv_get(v, i), 0.0)
    fn u_boxes(ps: List<UParticle>, radius: Float) -> FloatVec =
        let half = fl_mul(radius, 0.5) in
        fold(ps, fv_empty(), fun(v: FloatVec, p: UParticle, unwrap_or(fv_concat(v, unwrap_or(fv_from_list(list(
            fl_sub(field(p, x), half), fl_sub(field(p, y), half), 0.0, fl_add(field(p, x), half), fl_add(field(p, y), half), 1.0)), fv_empty())), v)))
    fn u_pairs(ps: List<UParticle>, radius: Float) -> NatVec = unwrap_or(bvh_pairs(u_boxes(ps, radius)), nv_empty())
    fn u_rule(a: Nat, b: Nat, seed: Nat) -> Float =
        if_then_else(nat_eq(a, b), 0.65, if_then_else(nat_eq(mod(a + 1, 5), b), 0.9,
            if_then_else(nat_eq(mod(b + 1, 5), a), -0.55, u_range(seed, mul(a, 5) + b + 999, -0.7, 0.7))))
    fn u_strength(distance: Float, coefficient: Float) -> Float =
        if_then_else(fl_lt(distance, 13.0), fl_mul(-1.7, fl_sub(1.0, fl_div(distance, 13.0))),
            fl_mul(fl_mul(coefficient, 0.18), fl_max(0.0, fl_sub(1.0, fl_abs(fl_sub(fl_div(fl_sub(distance, 13.0), 38.5), 1.0))))))
    fn u_add_force(f: FloatVec, i: Nat, x: Float, y: Float) -> FloatVec =
        let k = mul(i, 2) in
        unwrap_or(fv_set(unwrap_or(fv_set(f, k, fl_add(u_get(f, k), x)), f), k + 1, fl_add(u_get(f, k + 1), y)), f)
    fn u_pair_force(f: FloatVec, data: FloatVec, pairs: NatVec, k: Nat, seed: Nat) -> FloatVec =
        let i = unwrap_or(nv_get(pairs, mul(k, 2)), 0) in let j = unwrap_or(nv_get(pairs, mul(k, 2) + 1), 0) in
        let dx = fl_sub(u_get(data, mul(j, 3)), u_get(data, mul(i, 3))) in let dy = fl_sub(u_get(data, mul(j, 3) + 1), u_get(data, mul(i, 3) + 1)) in
        let d = fl_sqrt(fl_add(fl_mul(dx, dx), fl_mul(dy, dy))) in
        if_then_else(or(fl_lt(d, 0.0001), not(fl_lt(d, 90.0))), f,
            let a = u_nat(u_get(data, mul(i, 3) + 2)) in let b = u_nat(u_get(data, mul(j, 3) + 2)) in
            let fi = fl_div(u_strength(d, u_rule(a, b, seed)), d) in let fj = fl_div(u_strength(d, u_rule(b, a, seed)), d) in
            u_add_force(u_add_force(f, i, fl_mul(dx, fi), fl_mul(dy, fi)), j, fl_neg(fl_mul(dx, fj)), fl_neg(fl_mul(dy, fj))))
    fn u_mouse_force(p: UVec, mouse: UPointer) -> UVec =
        if_then_else(and(field(mouse, inside), field(mouse, button) > 0),
            let dx = fl_sub(field(mouse, x), field(p, x)) in let dy = fl_sub(field(mouse, y), field(p, y)) in
            let d2 = fl_add(fl_add(fl_mul(dx, dx), fl_mul(dy, dy)), 900.0) in
            let k = fl_div(if_then_else(nat_eq(field(mouse, button), 1), 90.0, -130.0), d2) in UVec(fl_mul(dx, k), fl_mul(dy, k)), UVec(0.0, 0.0))
    fn u_integrate(p: UParticle, fx: Float, fy: Float, damping: Float, limit: Float, mouse: UPointer) -> UParticle =
        let m = u_mouse_force(UVec(field(p, x), field(p, y)), mouse) in
        let vx = u_clamp(fl_mul(fl_add(field(p, vx), fl_add(fx, field(m, x))), damping), fl_neg(limit), limit) in
        let vy = u_clamp(fl_mul(fl_add(field(p, vy), fl_add(fy, field(m, y))), damping), fl_neg(limit), limit) in
        let x = fl_add(field(p, x), vx) in let y = fl_add(field(p, y), vy) in
        UParticle(field(p, id), u_clamp(x, 22.0, 978.0), u_clamp(y, 22.0, 678.0), field(p, z),
            if_then_else(or(fl_lt(x, 22.0), fl_lt(978.0, x)), fl_mul(vx, -0.7), vx),
            if_then_else(or(fl_lt(y, 22.0), fl_lt(678.0, y)), fl_mul(vy, -0.7), vy), field(p, kind))
    fn u_life_point(p: UParticle, forces: FloatVec, mouse: UPointer) -> UParticle =
        u_integrate(p, fl_add(u_get(forces, mul(field(p, id), 2)), fl_mul(fl_sub(500.0, field(p, x)), 0.00003)),
            fl_add(u_get(forces, mul(field(p, id), 2) + 1), fl_mul(fl_sub(350.0, field(p, y)), 0.00003)), 0.96, 4.0, mouse)
    fn u_mw_kick(p: UParticle, time: Float, dt: Float) -> UParticle =
        let half = fl_mul(0.5, dt) in let f = u_mw_force_at(field(p, x), field(p, y), time) in
        let vx = fl_add(field(p, vx), fl_mul(half, field(f, x))) in let vy = fl_add(field(p, vy), fl_mul(half, field(f, y))) in
        let x = fl_add(field(p, x), fl_mul(dt, vx)) in let y = fl_add(field(p, y), fl_mul(dt, vy)) in
        let g = u_mw_force_at(x, y, fl_add(time, dt)) in
        UParticle(field(p, id), x, y, field(p, z), fl_add(vx, fl_mul(half, field(g, x))), fl_add(vy, fl_mul(half, field(g, y))), field(p, kind))
    fn u_orbit_point(p: UParticle, tick: Nat, mouse: UPointer) -> UParticle =
        let r2 = fl_add(fl_mul(field(p, x), field(p, x)), fl_mul(field(p, y), field(p, y))) in
        let n = if_then_else(fl_lt(r2, 0.16), 4, if_then_else(fl_lt(r2, 1.0), 2, 1)) in let dt = fl_div(0.25, fl_of_nat(n)) in
        let time = fl_mul(fl_of_nat(tick), 0.25) in
        fold(u_indices(n), p, fun(state: UParticle, i: Nat, u_mw_kick(state, fl_add(time, fl_mul(fl_of_nat(i), dt)), dt)))
    fn u_spring_neighbor(p: UParticle, data: FloatVec, j: Nat, rest: Float) -> UVec =
        let dx = fl_sub(u_get(data, mul(j, 3)), field(p, x)) in let dy = fl_sub(u_get(data, mul(j, 3) + 1), field(p, y)) in
        let d = fl_max(0.001, fl_sqrt(fl_add(fl_mul(dx, dx), fl_mul(dy, dy)))) in
        let k = fl_mul(0.035, fl_div(fl_sub(d, rest), d)) in UVec(fl_mul(dx, k), fl_mul(dy, k))
    fn u_spring_point(p: UParticle, data: FloatVec, n: Nat, seed: Nat, tick: Nat, mouse: UPointer) -> UParticle =
        let i = field(p, id) in let origin = u_point(2, seed, i, n) in let rows = div(n + 19, 20) in
        let resty = fl_div(510.0, fl_of_nat(ui_max_v(1, pred(rows)))) in
        let neighbors = list(if_then_else(mod(i, 20) > 0, i, 0), if_then_else(and(19 > mod(i, 20), n > i + 1), i + 2, 0), if_then_else(i > 19, monus(i, 19), 0), if_then_else(n > i + 20, i + 21, 0)) in
        let f = fold(neighbors, UVec(0.0, 0.0), fun(acc: UVec, encoded: Nat,
            if_then_else(nat_eq(encoded, 0), acc, let j = pred(encoded) in
                let v = u_spring_neighbor(p, data, j, if_then_else(or(nat_eq(j + 1, i), nat_eq(i + 1, j)), 41.0, resty)) in
                UVec(fl_add(field(acc, x), field(v, x)), fl_add(field(acc, y), field(v, y)))))) in
        let t = fl_mul(fl_of_nat(tick), 0.04) in
        let pulse = fl_mul(0.13, fl_sin(fl_add(t, fl_mul(fl_of_nat(i), 0.17)))) in
        u_integrate(p, fl_add(field(f, x), fl_add(fl_mul(fl_sub(field(origin, x), field(p, x)), 0.004), pulse)),
            fl_add(field(f, y), fl_add(fl_mul(fl_sub(field(origin, y), field(p, y)), 0.004), fl_mul(pulse, 1.4))), 0.985, 4.0, mouse)
    fn u_sea_tanh(x: Float) -> Float =
        let e = fl_exp(fl_mul(2.0, fl_min(20.0, x))) in fl_div(fl_sub(e, 1.0), fl_add(e, 1.0))
    fn u_sea_k(omega: Float) -> Float =
        fold(u_indices(10), fl_div(fl_mul(omega, omega), 9.81), fun(k: Float, i: Nat,
            let t = u_sea_tanh(fl_mul(k, 50.0)) in
            let derivative = fl_mul(9.81, fl_add(t, fl_mul(fl_mul(k, 50.0), fl_sub(1.0, fl_mul(t, t))))) in
            fl_max(0.000001, fl_sub(k, fl_div(fl_sub(fl_mul(fl_mul(9.81, k), t), fl_mul(omega, omega)), fl_max(0.00001, derivative))))))
    fn u_sea_amplitude(omega: Float) -> Float =
        let sigma = if_then_else(fl_le(omega, 0.7853981634), 0.07, 0.09) in
        let q = fl_div(fl_sub(omega, 0.7853981634), fl_mul(sigma, 0.7853981634)) in
        let peak = fl_exp(fl_mul(-0.5, fl_mul(q, q))) in
        let tail = fl_exp(fl_mul(-1.25, fl_pow(fl_div(0.7853981634, omega), 4.0))) in
        let spectrum = fl_mul(fl_mul(0.77951241, fl_pow(omega, -5.0)), fl_mul(tail, fl_pow(3.3, peak))) in
        fl_sqrt(fl_mul(fl_mul(2.0, spectrum), 0.0717391304347826))
    fn u_sea_height(x: Float, z: Float, t: Float, seed: Nat) -> Float =
        fold(u_indices(24), 0.0, fun(height: Float, i: Nat,
            let omega = fl_add(0.43, fl_mul(fl_of_nat(i), 0.0717391304347826)) in
            let k = u_sea_k(omega) in let direction = u_range(seed, mul(i, 37), -1.05, 0.25) in
            let phase = fl_add(fl_sub(fl_mul(k, fl_add(fl_mul(x, fl_cos(direction)), fl_mul(z, fl_sin(direction)))), fl_mul(omega, t)), u_range(seed, mul(i, 71), 0.0, 6.283185307179586)) in
            fl_add(height, fl_mul(u_sea_amplitude(omega), fl_cos(phase)))))
    fn u_wave_point(p: UParticle, n: Nat, seed: Nat, tick: Nat, mouse: UPointer) -> UParticle =
        let origin = u_point(3, seed, field(p, id), n) in
        let height = u_sea_height(fl_mul(field(origin, x), 0.4), fl_mul(field(origin, y), 0.4), fl_mul(fl_of_nat(tick), 0.045), seed) in
        UParticle(field(p, id), field(origin, x), fl_sub(field(origin, y), fl_mul(height, 18.0)), height, 0.0, 0.0, field(p, kind))
    fn u_lorenz_derivative(p: UParticle) -> UParticle =
        UParticle(field(p, id), fl_mul(10.0, fl_sub(field(p, y), field(p, x))),
            fl_sub(fl_mul(field(p, x), fl_sub(28.0, field(p, z))), field(p, y)),
            fl_sub(fl_mul(field(p, x), field(p, y)), fl_mul(2.6666666666666665, field(p, z))), 0.0, 0.0, field(p, kind))
    fn u_lorenz_add(p: UParticle, d: UParticle, h: Float) -> UParticle =
        UParticle(field(p, id), fl_add(field(p, x), fl_mul(h, field(d, x))), fl_add(field(p, y), fl_mul(h, field(d, y))), fl_add(field(p, z), fl_mul(h, field(d, z))), 0.0, 0.0, field(p, kind))
    fn u_lorenz_point(p: UParticle) -> UParticle =
        // Explicit midpoint RK2, h = 0.008; bounded step, no wall constraints.
        u_lorenz_add(p, u_lorenz_derivative(u_lorenz_add(p, u_lorenz_derivative(p), 0.004)), 0.008)
    fn u_step(w: UWorld, mouse: UPointer, trails: Bool) -> UWorld =
        let ps = field(w, points) in let mode = field(w, mode) in let n = length(ps) in let tick = field(w, tick) in
        let pairs = if_then_else(nat_eq(mode, 0), u_pairs(ps, 90.0), nv_empty()) in
        let data = if_then_else(or(nat_eq(mode, 0), nat_eq(mode, 2)), u_pack(ps), fv_empty()) in
        let forces = if_then_else(nat_eq(mode, 0), fold(u_indices(div(nv_len(pairs), 2)), unwrap_or(fv_new(mul(n, 2), 0.0), fv_empty()), fun(f: FloatVec, k: Nat, u_pair_force(f, data, pairs, k, field(w, seed)))), fv_empty()) in
        let next = map(ps, fun(p: UParticle,
            if_then_else(nat_eq(mode, 0), u_life_point(p, forces, mouse),
            if_then_else(nat_eq(mode, 1), u_orbit_point(p, tick, mouse),
            if_then_else(nat_eq(mode, 2), u_spring_point(p, data, n, field(w, seed), tick, mouse),
            if_then_else(nat_eq(mode, 3), u_wave_point(p, n, field(w, seed), tick + 1, mouse), if_then_else(nat_eq(mode, 4), u_lorenz_point(p), p))))))) in
        let energy = fl_div(fold(next, 0.0, fun(e: Float, p: UParticle, fl_add(e, fl_add(fl_mul(field(p, vx), field(p, vx)), fl_mul(field(p, vy), field(p, vy)))))), fl_of_nat(ui_max_v(1, n))) in
        UWorld(next, mode, field(w, seed), tick + 1, pairs, energy, if_then_else(trails, cons(ps, u_take(field(w, history), 5)), empty_list(List(UParticle))))
    fn u_steps(w: UWorld, mouse: UPointer, trails: Bool, n: Nat) -> UWorld decreasing n =
        if_then_else(nat_eq(n, 0), w, u_steps(u_step(w, mouse, trails), mouse, trails, pred(n)))
    fn u_field_mode(mode: Nat) -> Bool = or(nat_eq(mode, 3), or(nat_eq(mode, 6), nat_eq(mode, 7)))
    fn u_native_scene(a: UApp) -> Bool =
        let mode = field(field(a, world), mode) in let s = field(a, settings) in
        and(field(s, gpu), or(field(s, count) > 400, or(nat_eq(mode, 1), or(nat_eq(mode, 5), u_field_mode(mode)))))
    fn u_gpu_count(a: UApp) -> Nat =
        let s = field(a, settings) in let mode = field(field(a, world), mode) in
        if_then_else(and(or(nat_eq(mode, 1), nat_eq(mode, 5)), 401 > field(s, count)), mul(field(s, count), 500), field(s, count))
    fn u_tick(a: UApp) -> UApp =
        let s = field(a, settings) in
        if_then_else(field(s, paused), a,
            let next = if_then_else(u_native_scene(a),
                let w = field(a, world) in UWorld(field(w, points), field(w, mode), field(w, seed), field(w, tick) + field(s, speed), nv_empty(), 0.0, empty_list(List(UParticle))),
                u_steps(field(a, world), field(a, pointer), field(s, trails), field(s, speed))) in
            let c = field(a, camera) in
            let t = fl_mul(fl_of_nat(field(next, tick)), 0.004) in
            let b = u_with_camera(u_with_world(a, next), if_then_else(and(field(c, automatic), not(or(u_field_mode(field(next, mode)), nat_eq(field(next, mode), 5)))),
                UCamera(fl_add(500.0, fl_mul(24.0, fl_sin(t))), fl_add(350.0, fl_mul(16.0, fl_cos(fl_mul(t, 0.7)))), fl_add(1.02, fl_mul(0.04, fl_sin(fl_mul(t, 0.5)))), true), c)) in
            if_then_else(and(field(s, automatic), not(900 > field(next, tick))), u_select(b, field(next, mode) + 1), b))
    fn u_action(a: UApp, action: Text) -> UApp =
        let s = field(a, settings) in let w = field(a, world) in
        if_then_else(text_eq(action, "pause"), u_with_settings(a, USettings(not(field(s, paused)), field(s, automatic), field(s, trails), field(s, fields), field(s, count), field(s, speed), field(s, gpu))),
        if_then_else(text_eq(action, "auto"), u_with_settings(a, USettings(field(s, paused), not(field(s, automatic)), field(s, trails), field(s, fields), field(s, count), field(s, speed), field(s, gpu))),
        if_then_else(text_eq(action, "trails"), u_with_settings(a, USettings(field(s, paused), field(s, automatic), not(field(s, trails)), field(s, fields), field(s, count), field(s, speed), field(s, gpu))),
        if_then_else(text_eq(action, "fields"),
            let b = u_with_settings(a, USettings(field(s, paused), field(s, automatic), field(s, trails), not(field(s, fields)), field(s, count), field(s, speed), field(s, gpu))) in
            if_then_else(nat_eq(field(w, mode), 5), u_with_camera(b, UCamera(500.0, 350.0, if_then_else(field(s, fields), 16.0, 1.45), true)), b),
        if_then_else(text_eq(action, "speed"), u_with_settings(a, USettings(field(s, paused), field(s, automatic), field(s, trails), field(s, fields), field(s, count), mod(field(s, speed), 3) + 1, field(s, gpu))),
        if_then_else(text_eq(action, "quality"), let count = if_then_else(u_field_mode(field(w, mode)), if_then_else(999999 > field(s, count), 1000000, if_then_else(4999999 > field(s, count), 5000000, 120)), if_then_else(160 > field(s, count), 240,
            if_then_else(300 > field(s, count), 400,
            if_then_else(999999 > field(s, count), if_then_else(field(s, gpu), 1000000, 120),
            if_then_else(1999999 > field(s, count), 2000000,
            if_then_else(4999999 > field(s, count), 5000000,
            if_then_else(9999999 > field(s, count), 10000000,
            if_then_else(15999999 > field(s, count), 16000000, 120)))))))) in
            u_select(u_with_settings(a, USettings(field(s, paused), field(s, automatic), field(s, trails), field(s, fields), count, field(s, speed), field(s, gpu))), field(w, mode)),
        if_then_else(text_eq(action, "reset"), u_select(a, field(w, mode)),
        if_then_else(text_eq(action, "camera"), u_with_camera(a, u_camera()),
        if_then_else(text_eq(action, "source-clear"), let p = field(a, pointer) in u_with_pointer(a, UPointer(field(p, x), field(p, y), 0, field(p, inside))),
        if_then_else(and(nat_eq(text_length(action), 2), text_eq(substring(action, 0, 1), "m")),
            let mode = unwrap_or(text_to_nat(substring(action, 1, 1)), 99) in
            if_then_else(8 > mode, u_select(u_with_settings(a, USettings(field(s, paused), false, field(s, trails), field(s, fields), field(s, count), field(s, speed), field(s, gpu))), mode), a), a))))))))))
    fn u_key(a: UApp, code: Nat) -> UApp =
        if_then_else(and(code > 48, 57 > code), u_action(a, concat("m", nat_to_text(monus(code, 49)))),
        if_then_else(or(nat_eq(code, 32), or(nat_eq(code, 80), nat_eq(code, 112))), u_action(a, "pause"),
        if_then_else(or(nat_eq(code, 65), nat_eq(code, 97)), u_action(a, "auto"),
        if_then_else(or(nat_eq(code, 84), nat_eq(code, 116)), u_action(a, "trails"),
        if_then_else(or(nat_eq(code, 70), nat_eq(code, 102)), u_action(a, "fields"),
        if_then_else(or(nat_eq(code, 82), nat_eq(code, 114)), u_action(a, "reset"),
        if_then_else(or(nat_eq(code, 67), nat_eq(code, 99)), u_action(a, "camera"),
        if_then_else(or(nat_eq(code, 81), nat_eq(code, 113)), u_action(a, "quality"),
        if_then_else(or(nat_eq(code, 83), nat_eq(code, 115)), u_action(a, "speed"),
        if_then_else(or(nat_eq(code, 88), nat_eq(code, 120)), u_action(a, "source-clear"), a))))))))))
    fn u_zoom_limit(a: UApp) -> Float =
        if_then_else(nat_eq(field(field(a, world), mode), 5), if_then_else(field(field(a, settings), fields), 8.0, 32.0), 3.0)
    fn u_invariant(a: UApp) -> Bool =
        let w = field(a, world) in let c = field(a, camera) in let s = field(a, settings) in
        and(and(8 > field(w, mode), and(nat_eq(length(field(w, points)), ui_max_v(40, ui_min_v(field(s, count), 400))), not(length(field(w, history)) > 6))),
            and(and(fl_is_finite(field(c, zoom)), and(fl_le(0.65, field(c, zoom)), fl_le(field(c, zoom), u_zoom_limit(a)))),
                and(and(field(s, speed) > 0, 4 > field(s, speed)), fold(field(w, points), true, fun(ok: Bool, p: UParticle,
                    and(ok, and(and(fl_is_finite(field(p, x)), fl_is_finite(field(p, y))), and(fl_is_finite(field(p, z)), and(fl_is_finite(field(p, vx)), fl_is_finite(field(p, vy)))))))))))
}
