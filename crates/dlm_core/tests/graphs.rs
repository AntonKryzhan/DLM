//! v0.115 — `NatVec`, native graph algorithms (`gr_*`), `std.graphs`,
//! `std.geometry`, `std.probability`. Interpreter and native C agree on every
//! printed line (`docs/GRAPHS.md`).
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    parse_module, CheckPolicy, Checker, Program, Runtime,
};
use std::path::{Path, PathBuf};
use std::process::Command;

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn run_both(name: &str, source: &str) -> Vec<String> {
    let dir = std::env::temp_dir().join(format!("dlm-graphs-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let interpreted = Runtime::new().run_program(&p).unwrap().output;
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(
        &emit_c(&ir),
        &dir.join("main.c"),
        &dir.join(if cfg!(windows) { "main.exe" } else { "main" }),
        &default_c_compiler(),
    )
    .unwrap();
    let out = Command::new(exe).output().unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let native: Vec<String> = String::from_utf8(out.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect();
    assert_eq!(interpreted, native);
    std::fs::remove_dir_all(dir).ok();
    native
}

const BUILTINS: &str = r#"    fn nv(xs: List<Nat>) -> NatVec = unwrap_or(nv_from_list(xs), nv_empty())
    fn fv(xs: List<Float>) -> FloatVec = unwrap_or(fv_from_list(xs), fv_empty())
    fn on(x: Option<NatVec>) -> Text = match x { none => "none", some(v) => print_symbolic(v) }
    fn of(x: Option<FloatVec>) -> Text = match x { none => "none", some(v) => print_symbolic(v) }
    fn nt(x: Option<Nat>) -> Text = match x { none => "none", some(v) => nat_to_text(v) }
    let s = nv(list(0, 0, 1, 2, 3))
    let d = nv(list(1, 2, 3, 3, 4))
    let w = fv(list(1.0, 4.0, 2.0, 0.5, 3.0))
    print_line(on(gr_bfs(6, s, d, 0)))
    print_line(on(gr_dfs(6, s, d, 0)))
    print_line(on(gr_toposort(6, s, d)))
    print_line(on(gr_toposort(2, nv(list(0, 1)), nv(list(1, 0)))))
    print_line(on(gr_components(6, s, d)))
    print_line(on(gr_scc(5, nv(list(0, 1, 2, 3, 4, 2)), nv(list(1, 2, 0, 4, 3, 3)))))
    print_line(on(gr_bipartite(4, nv(list(0, 1, 2, 3)), nv(list(1, 2, 3, 0)))))
    print_line(on(gr_bipartite(3, nv(list(0, 1, 2)), nv(list(1, 2, 0)))))
    print_line(of(gr_dijkstra(6, s, d, w, 0)))
    print_line(of(gr_dijkstra(6, s, d, fv(list(1.0, -4.0, 2.0, 0.5, 3.0)), 0)))
    print_line(of(gr_bellman_ford(6, s, d, fv(list(1.0, -4.0, 2.0, 0.5, 3.0)), 0)))
    print_line(of(gr_bellman_ford(3, nv(list(0, 1, 2)), nv(list(1, 2, 1)), fv(list(1.0, 1.0, -3.0)), 0)))
    print_line(of(gr_floyd(3, nv(list(0, 1, 2)), nv(list(1, 2, 0)), fv(list(1.0, 2.0, 3.0)))))
    print_line(on(gr_mst(4, nv(list(0, 1, 2, 3, 0)), nv(list(1, 2, 3, 0, 2)), fv(list(1.0, 2.0, 1.0, 3.0, 1.0)))))
    print_line(nt(gr_maxflow(4, nv(list(0, 0, 1, 2, 1)), nv(list(1, 2, 3, 3, 2)), nv(list(3, 2, 2, 3, 1)), 0, 3)))
    print_line(on(gr_bfs(2, nv(list(0)), nv(list(5)), 0)))
    print_line(on(nv_range(5, 4)))
    print_line(nt(nv_sum(nv(list(1, 2, 3)))))
    print_line(nt(nv_sum(nv(list(340282366920938463463374607431768211455, 1)))))
    print_line(print_symbolic(nv_sort(nv(list(5, 1, 4, 1)))))
    print_line(print_symbolic(nv_reverse(nv(list(5, 1, 4)))))
    print_line(nt(nv_find(nv(list(5, 1, 4)), 4)))
    print_line(on(nv_set(nv(list(5, 1, 4)), 1, 9)))
    print_line(on(nv_push(nv(list(5)), 6)))
    print_line(on(nv_slice(nv(list(5, 1, 4)), 1, 2)))
    print_line(print_symbolic(nv_to_fv(nv(list(3, 7)))))
    print_line(nat_to_text(nv_len(nv(list(3, 7)))))
    print_line(if_then_else(equal(nv(list(1, 2)), nv(list(1, 2))), "eq", "ne"))
"#;

#[test]
fn natvec_and_graph_builtins_agree_between_interpreter_and_native() {
    let out = run_both(
        "builtins",
        &format!("module test.graphs\ntheory App {{\n{BUILTINS}}}\n"),
    );
    let want: &[&str] = &[
        "nvec[0, 1, 1, 2, 3, 6]",
        "nvec[0, 1, 1, 2, 3, 6]",
        "nvec[0, 1, 3, 4, 2]",
        "nvec[0, 1, 3, 4, 2]",
        "nvec[0, 1, 2, 3, 4, 5]",
        "nvec[0, 1, 2, 3, 4, 5]",
        "none",
        "nvec[0, 0, 0, 0, 0, 1]",
        "nvec[0, 0, 0, 0, 0, 1]",
        "nvec[0, 0, 0, 1, 1]",
        "nvec[0, 0, 0, 1, 1]",
        "nvec[0, 1, 0, 1]",
        "nvec[0, 1, 0, 1]",
        "none",
        "fvec[0.0, 1.0, 4.0, 3.0, 6.0, inf]",
        "fvec[0.0, 1.0, 4.0, 3.0, 6.0, inf]",
        "none",
        "fvec[0.0, 1.0, -4.0, -3.5, -0.5, inf]",
        "fvec[0.0, 1.0, -4.0, -3.5, -0.5, inf]",
        "none",
        "fvec[0.0, 1.0, 3.0, 5.0, 0.0, 2.0, 3.0, 4.0, 0.0]",
        "fvec[0.0, 1.0, 3.0, 5.0, 0.0, 2.0, 3.0, 4.0, 0.0]",
        "nvec[0, 2, 4]",
        "nvec[0, 2, 4]",
        "5",
        "none",
        "nvec[5, 6, 7, 8]",
        "nvec[5, 6, 7, 8]",
        "6",
        "none",
        "nvec[1, 1, 4, 5]",
        "nvec[1, 1, 4, 5]",
        "nvec[4, 1, 5]",
        "nvec[4, 1, 5]",
        "2",
        "nvec[5, 9, 4]",
        "nvec[5, 9, 4]",
        "nvec[5, 6]",
        "nvec[5, 6]",
        "nvec[1, 4]",
        "nvec[1, 4]",
        "fvec[3.0, 7.0]",
        "fvec[3.0, 7.0]",
        "2",
        "eq",
    ];
    assert_eq!(out, want);
}

const LIBRARY: &str = r#"    fn t(x: Float) -> Text = fl_to_text(x)
    fn nt(x: Nat) -> Text = nat_to_text(x)
    fn o(x: Option<Float>) -> Text = match x { none => "none", some(y) => t(y) }
    fn on(x: Option<Nat>) -> Text = match x { none => "none", some(y) => nat_to_text(y) }
    fn path(x: Option<Route>) -> Text = match x { none => "none", some(p) => join(map(field(p, vertices), nt), " ") }
    fn pt(p: Vec2) -> Text = concat(t(field(p, x)), concat(",", t(field(p, y))))
    fn op(p: Option<Vec2>) -> Text = match p { none => "none", some(q) => pt(q) }
    fn tr(r: Option<TestResult>) -> Text = match r { none => "none", some(x) => concat(t(field(x, statistic)), concat(" ", t(field(x, p_value)))) }
    fn fvl(xs: List<Float>) -> FloatVec = unwrap_or(fv_from_list(xs), fv_empty())
    let g = add_edge(add_edge(add_edge(add_edge(add_edge(graph(6), 0, 1, 7.0), 0, 2, 9.0), 1, 2, 1.0), 2, 3, 2.0), 1, 3, 5.0)
    print_line(path(shortest_path(g, 0, 3)))
    print_line(o(path_weight(g, 0, 3)))
    print_line(path(shortest_path(g, 0, 5)))
    print_line(join(map(neighbors(g, 0), nt), " "))
    print_line(nat_to_text(in_degree(g, 3)))
    print_line(nat_to_text(component_count(g)))
    print_line(if_then_else(is_dag(g), "dag", "cyclic"))
    print_line(o(mst_weight(g)))
    print_line(on(max_flow(g, unwrap_or(nv_from_list(list(3, 2, 1, 4, 2)), nv_empty()), 0, 3)))
    print_line(on(hops(from_edges(4, list(0, 1, 2), list(1, 2, 3)), 0, 3)))
    print_line(nat_to_text(scc_count(from_edges(4, list(0, 1, 2, 3), list(1, 0, 3, 2)))))
    let sq = list(v2(0.0, 0.0), v2(2.0, 0.0), v2(2.0, 2.0), v2(0.0, 2.0))
    print_line(t(polygon_area(sq)))
    print_line(t(polygon_perimeter(sq)))
    print_line(op(polygon_centroid(sq)))
    print_line(if_then_else(polygon_contains(sq, v2(1.0, 1.0)), "in", "out"))
    print_line(if_then_else(polygon_contains(sq, v2(3.0, 1.0)), "in", "out"))
    print_line(join(map(convex_hull(list(v2(0.0, 0.0), v2(1.0, 1.0), v2(2.0, 2.0), v2(2.0, 0.0), v2(0.0, 2.0), v2(1.0, 0.5))), pt), " "))
    print_line(if_then_else(segments_intersect(v2(0.0, 0.0), v2(2.0, 2.0), v2(0.0, 2.0), v2(2.0, 0.0)), "x", "-"))
    print_line(if_then_else(segments_intersect(v2(0.0, 0.0), v2(1.0, 0.0), v2(2.0, 0.0), v2(3.0, 0.0)), "x", "-"))
    print_line(op(line_intersection(v2(0.0, 0.0), v2(2.0, 2.0), v2(0.0, 2.0), v2(2.0, 0.0))))
    print_line(t(segment_distance(v2(1.0, 1.0), v2(0.0, 0.0), v2(2.0, 0.0))))
    print_line(op(circumcenter(v2(0.0, 0.0), v2(2.0, 0.0), v2(0.0, 2.0))))
    print_line(pt(apply2(compose2(translation2(v2(1.0, 0.0)), scaling2(2.0, 3.0)), v2(1.0, 1.0))))
    print_line(match invert2(compose2(translation2(v2(1.0, 0.0)), scaling2(2.0, 3.0))) { none => "none", some(m) => pt(apply2(m, v2(3.0, 3.0))) })
    print_line(match m4_apply(m4_mul(m4_translation(v3(1.0, 2.0, 3.0)), m4_scaling(v3(2.0, 2.0, 2.0))), v3(1.0, 1.0, 1.0)) { none => "none", some(p) => concat(t(field(p, x)), concat(",", concat(t(field(p, y)), concat(",", t(field(p, z)))))) })
    print_line(o(ray_sphere(v3(0.0, 0.0, -5.0), v3(0.0, 0.0, 1.0), v3(0.0, 0.0, 0.0), 1.0)))
    print_line(o(ray_triangle(v3(0.25, 0.25, -1.0), v3(0.0, 0.0, 1.0), v3(0.0, 0.0, 0.0), v3(1.0, 0.0, 0.0), v3(0.0, 1.0, 0.0))))
    print_line(o(ray_plane(v3(0.0, 0.0, 0.0), v3(0.0, 1.0, 0.0), v3(0.0, 5.0, 0.0), v3(0.0, 1.0, 0.0))))
    print_line(t(erf(0.5)))
    print_line(t(erf(3.5)))
    print_line(t(erfc(5.0)))
    print_line(t(gamma(5.0)))
    print_line(t(log_gamma(0.5)))
    print_line(t(normal_cdf(1.96, 0.0, 1.0)))
    print_line(o(normal_quantile(0.975, 0.0, 1.0)))
    print_line(t(chi_square_cdf(3.84, 1)))
    print_line(t(student_t_cdf(2.0, 10.0)))
    print_line(t(beta_inc(2.0, 3.0, 0.4)))
    print_line(t(binomial_pmf(10, 3, 0.5)))
    print_line(t(binomial_cdf(10, 5, 0.5)))
    print_line(t(poisson_pmf(2, 3.0)))
    print_line(t(poisson_cdf(2, 3.0)))
    print_line(t(field(sample_exponential(5, 2.0), value)))
    print_line(nat_to_text(field(sample_poisson(5, 4.0), value)))
    print_line(nat_to_text(field(sample_discrete(9, fvl(list(1.0, 0.0, 3.0))), value)))
    print_line(o(quantile(fvl(list(1.0, 2.0, 3.0, 4.0)), 0.5)))
    print_line(print_symbolic(histogram(fvl(list(0.0, 0.5, 1.0, 1.5, 2.0, 7.0)), 0.0, 2.0, 2)))
    print_line(tr(t_test(fvl(list(5.1, 4.9, 5.3, 5.2, 4.8, 5.0)), 5.0)))
    print_line(tr(welch_t_test(fvl(list(1.0, 2.0, 3.0, 4.0)), fvl(list(2.0, 3.0, 4.0, 5.0, 6.0)))))
    print_line(tr(chi_square_test(fvl(list(10.0, 20.0, 30.0)), fvl(list(20.0, 20.0, 20.0)))))
    print_line(o(mean(normal_samples(3, 500, 10.0, 2.0))))
"#;

#[test]
fn graph_geometry_probability_libraries_agree_between_interpreter_and_native() {
    let out = run_both(
        "library",
        &format!(
            "module test.glib\nimport std.graphs\ntheory App extends StdGraphs {{\n{LIBRARY}}}\n"
        ),
    );
    assert_eq!(out.len(), 52, "{out:?}");
    let exact: &[&str] = &[
        "0 1 2 3",
        "10.0",
        "none",
        "2 1",
        "2",
        "3",
        "dag",
        "10.0",
        "5",
        "3",
        "2",
        "4.0",
        "8.0",
        "1.0,1.0",
        "in",
        "out",
        "0.0,0.0 2.0,0.0 2.0,2.0 0.0,2.0",
        "x",
        "-",
        "1.0,1.0",
        "1.0",
        "1.0,1.0",
        "3.0,3.0",
        "1.0,1.0",
        "3.0,4.0,5.0",
        "4.0",
        "1.0",
        "5.0",
    ];
    assert_eq!(&out[..exact.len()], exact);
    let reference = [
        0.5204998778130465,
        0.9999992569016276,
        1.537459794428035e-12,
        23.99999999999995,
        0.5723649429247,
        0.9750021048517794,
        1.959963984540054,
        0.9499564787512949,
        0.9633059826146297,
        0.524799999999998,
        0.11718749999999999,
        0.6230468749999999,
        0.22404180765538756,
        0.4231900811268434,
        0.8128121918239852,
        4.0,
    ];
    for (i, want) in reference.iter().enumerate() {
        let got: f64 = out[exact.len() + i].parse().unwrap();
        assert!(
            (got - want).abs() <= 1e-9 * want.abs().max(1.0),
            "#{i}: {got} vs {want}"
        );
    }
    let tail = &out[exact.len() + reference.len()..];
    assert_eq!(&tail[1..4], ["2.5", "nvec[2, 3]", "nvec[2, 3]"]);
    assert!(
        tail[4].starts_with("0.65465367070797") && tail[6].starts_with("10.0 0.00673794699908"),
        "{tail:?}"
    );
}

fn diagnostics(body: &str) -> Vec<String> {
    let src = format!("module test.graphs\ntheory App {{\n{body}\n}}\n");
    let p = Program::single(parse_module(&src).unwrap_or_else(|d| panic!("{d:?}")));
    Checker::with_policy(CheckPolicy::trusted_only())
        .check_program(&p)
        .diagnostics
        .iter()
        .map(|d| d.message.clone())
        .collect()
}

#[test]
fn natvec_and_graphs_are_typed() {
    for bad in [
        "fn f(v: NatVec) -> Option<Nat> = nv_get(v, 1.0)",
        "fn f(v: FloatVec) -> Nat = nv_len(v)",
        "fn f(v: NatVec) -> Option<NatVec> = gr_bfs(3, v, v)",
        "fn f(v: NatVec) -> Option<FloatVec> = gr_dijkstra(3, v, v, v, 0)",
        "fn f(v: NatVec) -> Nat = nv_sum(v)",
    ] {
        assert!(!diagnostics(bad).is_empty(), "accepted: {bad}");
    }
    for ok in [
        "fn f(v: NatVec) -> Nat = unwrap_or(nv_get(v, 0), 0) + nv_len(v)",
        "fn f(v: NatVec) -> Bool = equal(nv_sort(v), v)",
        "fn f(v: NatVec, w: FloatVec) -> Option<FloatVec> = gr_dijkstra(nv_len(v), v, v, w, 0)",
    ] {
        let e = diagnostics(ok);
        assert!(e.is_empty(), "{ok}\n{e:?}");
    }
}
