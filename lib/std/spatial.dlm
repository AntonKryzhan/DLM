// Spatial structures on robust predicates (v0.123, docs/KERNELS.md): exact orientation tests,
// Delaunay meshes, triangle-mesh measures and adjacency, k-nearest neighbours;
// v0.124: 3-D triangle soups — BVH ray casting, winding numbers (point in mesh), area, volume, box overlaps.
module std.spatial
import std.statistics

theory StdSpatial extends StdStatistics {
    record Mesh(points: FloatVec, triangles: NatVec)

    private fn sp_ix(n: Nat) -> List<Nat> = nv_to_list(unwrap_or(nv_range(0, n), nv_empty()))
    private fn coords(ps: List<Vec2>) -> FloatVec = fold(ps, fv_empty(), fun(acc: FloatVec, p: Vec2, unwrap_or(fv_concat(acc, unwrap_or(fv_from_list(list(field(p, x), field(p, y))), fv_empty())), acc)))
    // exact orientation: 1 counter-clockwise, 2 clockwise, 0 collinear (none outside the supported range)
    fn exact_orientation(a: Vec2, b: Vec2, c: Vec2) -> Option<Nat> = geo_orient2d(coords(list(a, b, c)))
    fn exact_in_circle(a: Vec2, b: Vec2, c: Vec2, d: Vec2) -> Option<Nat> = geo_incircle(coords(list(a, b, c, d)))
    fn exact_collinear(a: Vec2, b: Vec2, c: Vec2) -> Bool = nat_eq(unwrap_or(exact_orientation(a, b, c), 3), 0)
    fn delaunay_mesh(ps: List<Vec2>) -> Option<Mesh> = let c = coords(ps) in match geo_delaunay(c) { none => none(Mesh), some(t) => some(Mesh(c, t)) }
    fn triangle_count(m: Mesh) -> Nat = div(nv_len(field(m, triangles)), 3)
    fn vertex(m: Mesh, i: Nat) -> Vec2 = Vec2(unwrap_or(fv_get(field(m, points), mul(2, i)), 0.0), unwrap_or(fv_get(field(m, points), mul(2, i) + 1), 0.0))
    fn corner(m: Mesh, t: Nat, k: Nat) -> Nat = unwrap_or(nv_get(field(m, triangles), mul(3, t) + k), 0)
    fn mesh_area(m: Mesh) -> Float =
        fold(sp_ix(triangle_count(m)), 0.0, fun(acc: Float, t: Nat, fl_add(acc, triangle_area(vertex(m, corner(m, t, 0)), vertex(m, corner(m, t, 1)), vertex(m, corner(m, t, 2))))))
    private fn edge_key(n: Nat, a: Nat, b: Nat) -> Nat = if_then_else(a > b, mul(b, n) + a, mul(a, n) + b)
    // undirected edge -> number of incident triangles
    fn edge_use(m: Mesh) -> NatMap =
        let n = div(fv_len(field(m, points)), 2) in
        fold(sp_ix(triangle_count(m)), km_empty(), fun(acc: NatMap, t: Nat, fold(sp_ix(3), acc, fun(a2: NatMap, k: Nat,
            unwrap_or(km_add(a2, edge_key(n, corner(m, t, k), corner(m, t, mod(k + 1, 3))), 1), a2)))))
    fn mesh_edge_count(m: Mesh) -> Nat = km_len(edge_use(m))
    fn boundary_edge_count(m: Mesh) -> Nat = km_len(km_filter(edge_use(m), fun(k: Nat, v: Nat, nat_eq(v, 1))))
    // V - E + F of the triangulated region (1 for a disc)
    fn mesh_euler(m: Mesh) -> Int =
        let used = fold(sp_ix(triangle_count(m)), km_empty(), fun(acc: NatMap, t: Nat, fold(sp_ix(3), acc, fun(a2: NatMap, k: Nat, unwrap_or(km_insert(a2, corner(m, t, k), 1), a2))))) in
        int(km_len(used) + triangle_count(m)) - int(mesh_edge_count(m))
    // the mesh satisfies the empty-circumcircle property (checked with the exact predicate, O(T·V))
    fn is_delaunay(m: Mesh) -> Bool =
        let n = div(fv_len(field(m, points)), 2) in
        fold(sp_ix(triangle_count(m)), true, fun(acc: Bool, t: Nat, and(acc, fold(sp_ix(n), true, fun(a2: Bool, v: Nat,
            and(a2, not(nat_eq(unwrap_or(exact_in_circle(vertex(m, corner(m, t, 0)), vertex(m, corner(m, t, 1)), vertex(m, corner(m, t, 2)), vertex(m, v)), 0), 1))))))))
    fn nearest(ps: List<Vec2>, q: Vec2, k: Nat) -> Option<NatVec> = kd_knn(coords(ps), coords(list(q)), 2, k)
    fn within(ps: List<Vec2>, q: Vec2, r: Float) -> Option<NatVec> = kd_radius(coords(ps), 2, coords(list(q)), r)

    // v0.124: triangle soups (9 coordinates per triangle), BVH ray casting, winding numbers, box overlaps
    private fn sp_c(t: FloatVec, i: Nat, k: Nat) -> Float = unwrap_or(fv_get(t, mul(9, i) + k), 0.0)
    fn soup_triangle_count(t: FloatVec) -> Nat = div(fv_len(t), 9)
    fn soup_triangle_area(t: FloatVec, i: Nat) -> Float =
        let ax = fl_sub(sp_c(t, i, 3), sp_c(t, i, 0)) in let ay = fl_sub(sp_c(t, i, 4), sp_c(t, i, 1)) in let az = fl_sub(sp_c(t, i, 5), sp_c(t, i, 2)) in
        let bx = fl_sub(sp_c(t, i, 6), sp_c(t, i, 0)) in let by = fl_sub(sp_c(t, i, 7), sp_c(t, i, 1)) in let bz = fl_sub(sp_c(t, i, 8), sp_c(t, i, 2)) in
        let cx = fl_sub(fl_mul(ay, bz), fl_mul(az, by)) in let cy = fl_sub(fl_mul(az, bx), fl_mul(ax, bz)) in let cz = fl_sub(fl_mul(ax, by), fl_mul(ay, bx)) in
        fl_mul(0.5, fl_sqrt(fl_add(fl_add(fl_mul(cx, cx), fl_mul(cy, cy)), fl_mul(cz, cz))))
    fn soup_area(t: FloatVec) -> Float = fold(sp_ix(soup_triangle_count(t)), 0.0, fun(acc: Float, i: Nat, fl_add(acc, soup_triangle_area(t, i))))
    // signed tetrahedron volume against the origin (outward orientation gives a positive total)
    fn soup_triangle_volume(t: FloatVec, i: Nat) -> Float =
        let cx = fl_sub(fl_mul(sp_c(t, i, 4), sp_c(t, i, 8)), fl_mul(sp_c(t, i, 5), sp_c(t, i, 7))) in
        let cy = fl_sub(fl_mul(sp_c(t, i, 5), sp_c(t, i, 6)), fl_mul(sp_c(t, i, 3), sp_c(t, i, 8))) in
        let cz = fl_sub(fl_mul(sp_c(t, i, 3), sp_c(t, i, 7)), fl_mul(sp_c(t, i, 4), sp_c(t, i, 6))) in
        fl_div(fl_add(fl_add(fl_mul(sp_c(t, i, 0), cx), fl_mul(sp_c(t, i, 1), cy)), fl_mul(sp_c(t, i, 2), cz)), 6.0)
    fn soup_volume(t: FloatVec) -> Float = fold(sp_ix(soup_triangle_count(t)), 0.0, fun(acc: Float, i: Nat, fl_add(acc, soup_triangle_volume(t, i))))
    private fn sp_v3(p: Vec3) -> FloatVec = unwrap_or(fv_from_list(list(field(p, x), field(p, y), field(p, z))), fv_empty())
    // distance along the ray to the nearest triangle (Möller–Trumbore through the BVH)
    fn ray_distance(t: FloatVec, origin: Vec3, dir: Vec3) -> Option<Float> =
        match bvh_raycast(t, sp_v3(origin), sp_v3(dir)) { none => none(Float), some(h) =>
            let d = unwrap_or(fv_get(h, 1), fl_neg(1.0)) in if_then_else(fl_lt(d, 0.0), none(Float), some(d)) }
    fn ray_triangle_index(t: FloatVec, origin: Vec3, dir: Vec3) -> Option<Int> =
        match bvh_raycast(t, sp_v3(origin), sp_v3(dir)) { none => none(Int), some(h) =>
            let i = unwrap_or(fv_get(h, 0), fl_neg(1.0)) in if_then_else(fl_lt(i, 0.0), none(Int), fl_to_int(i)) }
    fn winding_number(t: FloatVec, p: Vec3) -> Float = unwrap_or(fv_get(unwrap_or(bvh_winding(t, sp_v3(p)), fv_empty()), 0), 0.0)
    fn inside_mesh(t: FloatVec, p: Vec3) -> Bool = fl_lt(0.5, winding_number(t, p))
    fn box_overlaps(boxes: FloatVec) -> Option<NatVec> = bvh_pairs(boxes)
    fn box_overlap_count(boxes: FloatVec) -> Nat = div(nv_len(unwrap_or(bvh_pairs(boxes), nv_empty())), 2)

    theorem unit_triangle_is_counter_clockwise: nat_eq(unwrap_or(geo_orient2d(unwrap_or(fv_from_list(list(0.0, 0.0, 1.0, 0.0, 0.0, 1.0)), fv_empty())), 0), 1)
}
