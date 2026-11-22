module universe.tests
import universe.core

theory UniverseTests extends UniverseCore {
    record UCheck(name: Text, passed: Bool)
    fn u_check(failures: Nat, test: UCheck) -> Nat =
        let line = print_line(concat(if_then_else(field(test, passed), "PASS ", "FAIL "), field(test, name))) in failures + if_then_else(field(test, passed), 0, 1)
    fn u_near(a: Float, b: Float) -> Bool = fl_le(fl_abs(fl_sub(a, b)), 0.000000001)
    fn u_all_pairs(n: Nat) -> NatVec = fold(u_indices(n), nv_empty(), fun(acc: NatVec, i: Nat,
        fold(u_indices(monus(n, i + 1)), acc, fun(v: NatVec, j: Nat, cat(v, nvl(list(i, i + j + 1)))))))
    fn u_test_forces(ps: List<UParticle>, pairs: NatVec) -> FloatVec =
        let data = u_pack(ps) in fold(u_indices(div(nv_len(pairs), 2)), unwrap_or(fv_new(mul(length(ps), 2), 0.0), fv_empty()), fun(f: FloatVec, k: Nat, u_pair_force(f, data, pairs, k, 7291)))
    fn u_mode_test(failures: Nat, mode: Nat) -> Nat =
        let a = u_initial(40, 7291) in let w = u_create(mode, 7291, 40) in
        let next = u_steps(w, u_pointer(), true, 140) in
        let ok = and(u_invariant(u_with_world(a, next)), and(nat_eq(field(next, tick), 140), not(equal(field(w, points), field(next, points))))) in
        let line = print_line(concat(if_then_else(ok, "PASS mode ", "FAIL mode "), nat_to_text(mode))) in failures + if_then_else(ok, 0, 1)
    fn main() -> Nat =
        let a = u_initial(40, 7291) in let w = field(a, world) in let ps = field(w, points) in
        let left = UParticle(0, 100.0, 100.0, 0.0, 0.0, 0.0, 0) in
        let right = UParticle(1, 140.0, 100.0, 0.0, 0.0, 0.0, 0) in
        let pairforces = u_test_forces(list(left, right), nvl(list(0, 1))) in
        let history = u_steps(w, u_pointer(), true, 10) in
        let near_cycle = UWorld(ps, 7, 7291, 899, nv_empty(), 0.0, empty_list(List(UParticle))) in
        let cycled = u_tick(u_with_world(a, near_cycle)) in
        let tests = list(
            UCheck("eight keyboard scenes", nat_eq(field(field(u_key(a, 56), world), mode), 7)),
            UCheck("new scenes keep bounded valid state", fold(list(5, 6, 7), true, fun(ok: Bool, mode: Nat, and(ok, u_invariant(u_tick(u_select(u_enable_gpu(a, true), mode))))))),
            UCheck("field clock is native at low quality", let g = u_select(u_enable_gpu(a, true), 6) in let next = u_tick(g) in and(nat_eq(field(field(next, world), tick), 1), equal(field(field(g, world), points), field(field(next, world), points)))),
            UCheck("Milky Way native count is explicit", nat_eq(u_gpu_count(u_select(u_enable_gpu(a, true), 1)), 20000)),
            UCheck("MW rotates with trailing arms", let p = u_point(1, 7291, 23, 40) in fl_lt(fl_sub(fl_mul(field(p, x), field(p, vy)), fl_mul(field(p, y), field(p, vx))), 0.0)),
            UCheck("MW radius has no clamp pile-up", fold(u_indices(100), true, fun(ok: Bool, i: Nat, let r = u_mw_radius(7291, i) in and(ok, and(fl_lt(0.025, r), fl_lt(r, 23.0)))))),
            UCheck("rotating bar breaks axial symmetry", not(fl_eq(field(u_mw_force_at(3.0, 0.0, 0.0), x), field(u_mw_force_at(0.0, 3.0, 0.0), y)))),
            UCheck("bar force varies continuously in time", fl_is_finite(field(u_mw_force_at(0.0, 0.0, 1000000.0), x))),
            UCheck("MW midplane gravity attracts", fl_lt(field(u_mw_force(8.0, 0.0), x), 0.0)),
            UCheck("initial invariant", u_invariant(a)),
            UCheck("count clamps low", nat_eq(length(field(field(u_initial(0, 1), world), points)), 40)),
            UCheck("count clamps high", nat_eq(length(field(field(u_initial(99999, 1), world), points)), 400)),
            UCheck("seed normalizes", nat_eq(field(u_create(0, 2147483648, 40), seed), 1)),
            UCheck("mode wraps", nat_eq(field(u_create(17, 1, 40), mode), 1)),
            UCheck("same seed reproduces initial state", equal(a, u_initial(40, 7291))),
            UCheck("different seeds differ", not(equal(field(w, points), field(u_create(0, 7292, 40), points)))),
            UCheck("broad phase matches all-pairs force exactly", equal(u_test_forces(ps, u_pairs(ps, 90.0)), u_test_forces(ps, u_all_pairs(40)))),
            UCheck("near boxes produce one candidate", equal(u_pairs(list(left, right), 90.0), nvl(list(0, 1)))),
            UCheck("same-species pair forces cancel", and(u_near(fl_add(u_get(pairforces, 0), u_get(pairforces, 2)), 0.0), u_near(fl_add(u_get(pairforces, 1), u_get(pairforces, 3)), 0.0))),
            UCheck("attraction has expected direction", fl_lt(0.0, u_get(pairforces, 0))),
            UCheck("coincident pair remains finite", fl_is_finite(fv_sum(u_test_forces(list(left, left), nvl(list(0, 1)))))),
            UCheck("finite interaction cutoff", fl_eq(u_strength(90.0, 1.0), 0.0)),
            UCheck("short distance repels", fl_lt(u_strength(1.0, 1.0), 0.0)),
            UCheck("five species have asymmetric rules", not(fl_eq(u_rule(0, 1, 7291), u_rule(1, 0, 7291)))),
            UCheck("history bounded", nat_eq(length(field(history, history)), 6)),
            UCheck("trails off drops history", is_empty(field(u_step(history, u_pointer(), false), history))),
            UCheck("pause freezes complete model", let p = u_action(a, "pause") in equal(u_tick(p), p)),
            UCheck("pause toggles back", equal(u_action(u_action(a, "pause"), "pause"), a)),
            UCheck("auto advances and wraps scene", and(nat_eq(field(field(cycled, world), mode), 0), nat_eq(field(field(cycled, world), tick), 0))),
            UCheck("manual selection disables auto", not(field(field(u_action(a, "m3"), settings), automatic))),
            UCheck("manual mode remains selected", nat_eq(field(field(u_tick(u_with_world(u_action(a, "auto"), near_cycle)), world), mode), 7)),
            UCheck("invalid action preserves model", equal(u_action(a, "m9"), a)),
            UCheck("unknown key preserves model", equal(u_key(a, 999), a)),
            UCheck("keyboard scene selects correct index", nat_eq(field(field(u_key(a, 53), world), mode), 4)),
            UCheck("space pauses", field(field(u_key(a, 32), settings), paused)),
            UCheck("speed cycles 1 2 3 1", nat_eq(field(field(u_action(u_action(u_action(a, "speed"), "speed"), "speed"), settings), speed), 1)),
            UCheck("quality cycles 120 240 400", nat_eq(field(field(u_action(u_action(u_action(u_initial(120, 1), "quality"), "quality"), "quality"), settings), count), 120)),
            UCheck("GPU count does not allocate a million DLM records", and(nat_eq(field(field(u_initial(1000000, 1), settings), count), 1000000), nat_eq(length(field(field(u_initial(1000000, 1), world), points)), 400))),
            UCheck("GPU mode ticks without CPU physics", let g = u_enable_gpu(u_initial(1000000, 1), true) in
                let t = u_tick(g) in and(nat_eq(field(field(t, world), tick), 1), equal(field(field(t, world), points), field(field(g, world), points)))),
            UCheck("GPU quality cycles 400 to one million", nat_eq(field(field(u_action(u_enable_gpu(u_initial(400, 1), true), "quality"), settings), count), 1000000)),
            UCheck("GPU count cap is sixteen million", nat_eq(field(field(u_initial(20000000, 1), settings), count), 16000000)),
            UCheck("GPU five million uses bounded DLM state", and(nat_eq(field(field(u_initial(5000000, 1), settings), count), 5000000), nat_eq(length(field(field(u_initial(5000000, 1), world), points)), 400))),
            UCheck("GPU advanced quality sequence 2 5 10 16", let g = u_enable_gpu(u_initial(2000000, 1), true) in
                let a5 = u_action(g, "quality") in let a10 = u_action(a5, "quality") in let a16 = u_action(a10, "quality") in
                and(and(nat_eq(field(field(a5, settings), count), 5000000), nat_eq(field(field(a10, settings), count), 10000000)),
                    nat_eq(field(field(a16, settings), count), 16000000))),
            UCheck("GPU advanced quality cycles back to 120", let g = u_enable_gpu(u_initial(16000000, 1), true) in
                nat_eq(field(field(u_action(g, "quality"), settings), count), 120)),
            UCheck("source attraction direction", fl_lt(0.0, field(u_mouse_force(UVec(100.0, 100.0), UPointer(200.0, 100.0, 1, true)), x))),
            UCheck("source repulsion direction", fl_lt(field(u_mouse_force(UVec(100.0, 100.0), UPointer(200.0, 100.0, 3, true)), x), 0.0)),
            UCheck("source at exact center is finite", fl_is_finite(field(u_mouse_force(UVec(100.0, 100.0), UPointer(100.0, 100.0, 1, true)), x))),
            UCheck("inactive pointer has no force", fl_eq(field(u_mouse_force(UVec(100.0, 100.0), u_pointer()), x), 0.0)),
            UCheck("wall clamps and reflects", let p = u_integrate(UParticle(0, 977.0, 350.0, 0.0, 4.0, 0.0, 0), 0.0, 0.0, 1.0, 4.0, u_pointer()) in and(fl_eq(field(p, x), 978.0), fl_lt(field(p, vx), 0.0))),
            UCheck("sea dispersion satisfies finite-depth equation", let k = u_sea_k(0.43) in fl_le(fl_abs(fl_sub(fl_mul(fl_mul(9.81, k), u_sea_tanh(fl_mul(50.0, k))), 0.1849)), 0.00000001)),
            UCheck("JONSWAP spectrum is positive", fl_lt(0.0, u_sea_amplitude(0.8))),
            UCheck("wave height is bounded by spectral amplitudes", let p = u_wave_point(left, 40, 7291, 73, u_pointer()) in
                fl_le(fl_abs(field(p, z)), fold(u_indices(24), 0.0, fun(total: Float, i: Nat, fl_add(total, u_sea_amplitude(fl_add(0.43, fl_mul(fl_of_nat(i), 0.0717391304347826)))))))),
            UCheck("field quality uses three meaningful levels", let g = u_select(u_enable_gpu(a, true), 6) in let b = u_action(g, "quality") in let c = u_action(b, "quality") in
                and(nat_eq(field(field(b, settings), count), 1000000), and(nat_eq(field(field(c, settings), count), 5000000), nat_eq(field(field(u_action(c, "quality"), settings), count), 120)))),
            UCheck("Lorenz midpoint step changes position", not(equal(u_lorenz_point(UParticle(0, 1.0, 1.0, 1.0, 0.0, 0.0, 0)), UParticle(0, 1.0, 1.0, 1.0, 0.0, 0.0, 0))))) in
        let failed = fold_effect(u_indices(5), fold_effect(tests, 0, u_check), u_mode_test) in
        let line = print_line(concat("Failures: ", nat_to_text(failed))) in failed
}
