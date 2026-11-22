// Render and simulation workload for an external wall-clock measurement.
module universe.bench
import universe.window

theory UniverseBench extends UniverseWindow {
    fn u_bench_frames(n: Nat, s: UWindow, total: Nat) -> Nat decreasing n =
        if_then_else(nat_eq(n, 0), total,
            let next = u_tick(field(s, app)) in
            let image = unwrap_or(u_image(s, next), nv_empty()) in
            u_bench_frames(pred(n), UWindow(next, field(s, width), field(s, height), field(s, fonts), image, false), total + nv_len(image)))
    fn main() -> Nat =
        let count = ui_max_v(1, ui_min_v(unwrap_or(text_to_nat(unwrap_or(index(args(), 0), "20")), 20), 300)) in
        let total = u_bench_frames(count, UWindow(u_initial(240, 7291), 1200, 800, u_fonts(), nv_empty(), true), 0) in
        let line = print_line(concat("Rendered frames: ", concat(nat_to_text(count), concat("; samples: ", nat_to_text(total))))) in
        if_then_else(nat_eq(total, mul(count, 960002)), 0, 1)
}
