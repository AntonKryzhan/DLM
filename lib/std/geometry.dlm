// Applied geometry on machine floats (v0.115, docs/GRAPHS.md): 2D/3D vectors, orientation and
// intersection predicates, polygons, convex hull, affine transforms, 4×4 matrices, rays.
// Predicates use exact sign tests on float expressions (no epsilon) unless a tolerance is passed.
module std.geometry
import std.probability

// on top of std.probability (and std.numerics), so one program reaches every float family
theory StdGeometry extends StdProbability {
    record Vec2(x: Float, y: Float)
    record Vec3(x: Float, y: Float, z: Float)
    record Box2(lo: Vec2, hi: Vec2)
    // x' = a·x + b·y + c, y' = d·x + e·y + f
    record Affine2(a: Float, b: Float, c: Float, d: Float, e: Float, f: Float)

    // ---- 2D vectors ----
    fn v2(x: Float, y: Float) -> Vec2 = Vec2(x, y)
    fn v2_add(p: Vec2, q: Vec2) -> Vec2 = Vec2(fl_add(field(p, x), field(q, x)), fl_add(field(p, y), field(q, y)))
    fn v2_sub(p: Vec2, q: Vec2) -> Vec2 = Vec2(fl_sub(field(p, x), field(q, x)), fl_sub(field(p, y), field(q, y)))
    fn v2_scale(p: Vec2, k: Float) -> Vec2 = Vec2(fl_mul(field(p, x), k), fl_mul(field(p, y), k))
    fn v2_dot(p: Vec2, q: Vec2) -> Float = fl_add(fl_mul(field(p, x), field(q, x)), fl_mul(field(p, y), field(q, y)))
    fn v2_cross(p: Vec2, q: Vec2) -> Float = fl_sub(fl_mul(field(p, x), field(q, y)), fl_mul(field(p, y), field(q, x)))
    fn v2_norm(p: Vec2) -> Float = fl_sqrt(v2_dot(p, p))
    fn v2_dist(p: Vec2, q: Vec2) -> Float = v2_norm(v2_sub(p, q))
    fn v2_normalize(p: Vec2) -> Option<Vec2> = let l = v2_norm(p) in if_then_else(fl_eq(l, 0.0), none(Vec2), some(v2_scale(p, fl_div(1.0, l))))
    fn v2_lerp(p: Vec2, q: Vec2, t: Float) -> Vec2 = v2_add(p, v2_scale(v2_sub(q, p), t))
    fn v2_rotate(p: Vec2, angle: Float) -> Vec2 =
        let c = fl_cos(angle) in let s = fl_sin(angle) in
        Vec2(fl_sub(fl_mul(c, field(p, x)), fl_mul(s, field(p, y))), fl_add(fl_mul(s, field(p, x)), fl_mul(c, field(p, y))))
    fn v2_angle(p: Vec2) -> Float = fl_atan2(field(p, y), field(p, x))
    fn v2_near(p: Vec2, q: Vec2, tol: Float) -> Bool = fl_le(v2_dist(p, q), tol)

    // ---- predicates ----
    // twice the signed area of abc: > 0 counter-clockwise, < 0 clockwise, 0 collinear
    fn orient2d(a: Vec2, b: Vec2, c: Vec2) -> Float = v2_cross(v2_sub(b, a), v2_sub(c, a))
    // 1 counter-clockwise, 2 clockwise, 0 collinear
    fn orientation(a: Vec2, b: Vec2, c: Vec2) -> Nat = let o = orient2d(a, b, c) in if_then_else(fl_lt(0.0, o), 1, if_then_else(fl_lt(o, 0.0), 2, 0))
    fn in_box(p: Vec2, a: Vec2, b: Vec2) -> Bool =
        and(and(fl_le(fl_min(field(a, x), field(b, x)), field(p, x)), fl_le(field(p, x), fl_max(field(a, x), field(b, x)))),
            and(fl_le(fl_min(field(a, y), field(b, y)), field(p, y)), fl_le(field(p, y), fl_max(field(a, y), field(b, y)))))
    fn on_segment(p: Vec2, a: Vec2, b: Vec2) -> Bool = and(nat_eq(orientation(a, b, p), 0), in_box(p, a, b))
    // closed segments ab and cd share a point
    fn segments_intersect(a: Vec2, b: Vec2, c: Vec2, d: Vec2) -> Bool =
        let o1 = orientation(a, b, c) in let o2 = orientation(a, b, d) in let o3 = orientation(c, d, a) in let o4 = orientation(c, d, b) in
        or(and(not(nat_eq(o1, o2)), not(nat_eq(o3, o4))),
            or(or(and(nat_eq(o1, 0), on_segment(c, a, b)), and(nat_eq(o2, 0), on_segment(d, a, b))),
                or(and(nat_eq(o3, 0), on_segment(a, c, d)), and(nat_eq(o4, 0), on_segment(b, c, d)))))
    // intersection point of lines ab and cd; none when parallel
    fn line_intersection(a: Vec2, b: Vec2, c: Vec2, d: Vec2) -> Option<Vec2> =
        let r = v2_sub(b, a) in let s = v2_sub(d, c) in let den = v2_cross(r, s) in
        if_then_else(fl_eq(den, 0.0), none(Vec2), some(v2_add(a, v2_scale(r, fl_div(v2_cross(v2_sub(c, a), s), den)))))
    fn closest_on_segment(p: Vec2, a: Vec2, b: Vec2) -> Vec2 =
        let ab = v2_sub(b, a) in let l2 = v2_dot(ab, ab) in
        if_then_else(fl_eq(l2, 0.0), a, v2_add(a, v2_scale(ab, fl_max(0.0, fl_min(1.0, fl_div(v2_dot(v2_sub(p, a), ab), l2))))))
    fn segment_distance(p: Vec2, a: Vec2, b: Vec2) -> Float = v2_dist(p, closest_on_segment(p, a, b))
    fn triangle_area(a: Vec2, b: Vec2, c: Vec2) -> Float = fl_abs(fl_mul(0.5, orient2d(a, b, c)))
    fn point_in_triangle(p: Vec2, a: Vec2, b: Vec2, c: Vec2) -> Bool =
        let d1 = orient2d(a, b, p) in let d2 = orient2d(b, c, p) in let d3 = orient2d(c, a, p) in
        if_then_else(fl_eq(orient2d(a, b, c), 0.0), or(on_segment(p, a, b), or(on_segment(p, b, c), on_segment(p, c, a))),
            not(and(or(or(fl_lt(d1, 0.0), fl_lt(d2, 0.0)), fl_lt(d3, 0.0)), or(or(fl_lt(0.0, d1), fl_lt(0.0, d2)), fl_lt(0.0, d3)))))
    fn circumcenter(a: Vec2, b: Vec2, c: Vec2) -> Option<Vec2> =
        let bx = fl_sub(field(b, x), field(a, x)) in let by = fl_sub(field(b, y), field(a, y)) in
        let cx = fl_sub(field(c, x), field(a, x)) in let cy = fl_sub(field(c, y), field(a, y)) in
        let d = fl_mul(2.0, fl_sub(fl_mul(bx, cy), fl_mul(by, cx))) in
        if_then_else(fl_eq(d, 0.0), none(Vec2),
            let b2 = fl_add(fl_mul(bx, bx), fl_mul(by, by)) in let c2 = fl_add(fl_mul(cx, cx), fl_mul(cy, cy)) in
            some(Vec2(fl_add(field(a, x), fl_div(fl_sub(fl_mul(cy, b2), fl_mul(by, c2)), d)), fl_add(field(a, y), fl_div(fl_sub(fl_mul(bx, c2), fl_mul(cx, b2)), d)))))

    // ---- polygons (vertex lists, implicitly closed) ----
    private fn edge_sum(ps: List<Vec2>, first: Vec2, f: Fn<Vec2, Vec2, Float>) -> Float decreasing ps =
        if_then_else(is_empty(ps), 0.0, match head(ps) { none => 0.0, some(p) => match head(tail(ps)) { none => f(p, first), some(q) => fl_add(f(p, q), edge_sum(tail(ps), first, f)) } })
    fn polygon_signed_area(ps: List<Vec2>) -> Float = match head(ps) { none => 0.0, some(p0) => fl_mul(0.5, edge_sum(ps, p0, v2_cross)) }
    fn polygon_area(ps: List<Vec2>) -> Float = fl_abs(polygon_signed_area(ps))
    fn polygon_perimeter(ps: List<Vec2>) -> Float = match head(ps) { none => 0.0, some(p0) => edge_sum(ps, p0, v2_dist) }
    private fn cx_term(p: Vec2, q: Vec2) -> Float = fl_mul(fl_add(field(p, x), field(q, x)), v2_cross(p, q))
    private fn cy_term(p: Vec2, q: Vec2) -> Float = fl_mul(fl_add(field(p, y), field(q, y)), v2_cross(p, q))
    fn polygon_centroid(ps: List<Vec2>) -> Option<Vec2> =
        match head(ps) { none => none(Vec2), some(p0) => let a = polygon_signed_area(ps) in
            if_then_else(fl_eq(a, 0.0), none(Vec2), let k = fl_div(1.0, fl_mul(6.0, a)) in some(Vec2(fl_mul(k, edge_sum(ps, p0, cx_term)), fl_mul(k, edge_sum(ps, p0, cy_term))))) }
    private fn crossing(p: Vec2, a: Vec2, b: Vec2) -> Bool =
        and(not(nat_eq(if_then_else(fl_lt(field(p, y), field(a, y)), 1, 0), if_then_else(fl_lt(field(p, y), field(b, y)), 1, 0))),
            fl_lt(field(p, x), fl_add(field(a, x), fl_div(fl_mul(fl_sub(field(p, y), field(a, y)), fl_sub(field(b, x), field(a, x))), fl_sub(field(b, y), field(a, y))))))
    private fn crossings(ps: List<Vec2>, first: Vec2, p: Vec2) -> Nat decreasing ps =
        if_then_else(is_empty(ps), 0, match head(ps) { none => 0, some(a) => let b = unwrap_or(head(tail(ps)), first) in if_then_else(crossing(p, a, b), 1, 0) + crossings(tail(ps), first, p) })
    // even-odd rule; boundary points may fall either way
    fn polygon_contains(ps: List<Vec2>, p: Vec2) -> Bool = match head(ps) { none => false, some(p0) => nat_eq(mod(crossings(ps, p0, p), 2), 1) }
    private fn lex_less(p: Vec2, q: Vec2) -> Bool = or(fl_lt(field(p, x), field(q, x)), and(fl_eq(field(p, x), field(q, x)), fl_lt(field(p, y), field(q, y))))
    private fn insert_point(ps: List<Vec2>, p: Vec2) -> List<Vec2> decreasing ps =
        if_then_else(is_empty(ps), list(p), match head(ps) { none => list(p), some(h) => if_then_else(lex_less(h, p), cons(h, insert_point(tail(ps), p)), cons(p, ps)) })
    fn sort_points(ps: List<Vec2>) -> List<Vec2> = fold(ps, empty_list(Vec2), insert_point)
    // stack is reversed: head = top
    private fn hull_push(stack: List<Vec2>, p: Vec2) -> List<Vec2> decreasing stack =
        if_then_else(is_empty(stack), list(p), match head(stack) { none => list(p), some(b) => match head(tail(stack)) { none => cons(p, stack), some(a) =>
            if_then_else(fl_le(orient2d(a, b, p), 0.0), hull_push(tail(stack), p), cons(p, stack)) } })
    private fn half_hull(ps: List<Vec2>) -> List<Vec2> = fold(ps, empty_list(Vec2), hull_push)
    private fn reverse2(ps: List<Vec2>) -> List<Vec2> = fold(ps, empty_list(Vec2), fun(acc: List<Vec2>, p: Vec2, cons(p, acc)))
    private fn append2(xs: List<Vec2>, ys: List<Vec2>) -> List<Vec2> = fold(reverse2(xs), ys, fun(acc: List<Vec2>, p: Vec2, cons(p, acc)))
    private fn dedup(ps: List<Vec2>) -> List<Vec2> decreasing ps =
        if_then_else(is_empty(ps), ps, match head(ps) { none => ps, some(p) => match head(tail(ps)) { none => ps, some(q) =>
            if_then_else(and(fl_eq(field(p, x), field(q, x)), fl_eq(field(p, y), field(q, y))), dedup(tail(ps)), cons(p, dedup(tail(ps)))) } })
    // convex hull, counter-clockwise from the lowest-leftmost point, collinear points dropped (Andrew's monotone chain)
    fn convex_hull(points: List<Vec2>) -> List<Vec2> =
        let s = dedup(sort_points(points)) in
        if_then_else(3 > length(s), s,
            let lower = half_hull(s) in let upper = half_hull(reverse2(s)) in
            append2(reverse2(tail(lower)), reverse2(tail(upper))))
    fn bounding_box(ps: List<Vec2>) -> Option<Box2> =
        match head(ps) { none => none(Box2), some(p0) => some(fold(ps, Box2(p0, p0), fun(b: Box2, p: Vec2,
            Box2(Vec2(fl_min(field(field(b, lo), x), field(p, x)), fl_min(field(field(b, lo), y), field(p, y))),
                Vec2(fl_max(field(field(b, hi), x), field(p, x)), fl_max(field(field(b, hi), y), field(p, y))))))) }
    fn boxes_overlap(a: Box2, b: Box2) -> Bool =
        and(and(fl_le(field(field(a, lo), x), field(field(b, hi), x)), fl_le(field(field(b, lo), x), field(field(a, hi), x))),
            and(fl_le(field(field(a, lo), y), field(field(b, hi), y)), fl_le(field(field(b, lo), y), field(field(a, hi), y))))

    // ---- 2D affine transforms ----
    fn affine_identity() -> Affine2 = Affine2(1.0, 0.0, 0.0, 0.0, 1.0, 0.0)
    fn translation2(t: Vec2) -> Affine2 = Affine2(1.0, 0.0, field(t, x), 0.0, 1.0, field(t, y))
    fn rotation2(angle: Float) -> Affine2 = let c = fl_cos(angle) in let s = fl_sin(angle) in Affine2(c, fl_neg(s), 0.0, s, c, 0.0)
    fn scaling2(sx: Float, sy: Float) -> Affine2 = Affine2(sx, 0.0, 0.0, 0.0, sy, 0.0)
    fn apply2(m: Affine2, p: Vec2) -> Vec2 =
        Vec2(fl_add(fl_add(fl_mul(field(m, a), field(p, x)), fl_mul(field(m, b), field(p, y))), field(m, c)),
            fl_add(fl_add(fl_mul(field(m, d), field(p, x)), fl_mul(field(m, e), field(p, y))), field(m, f)))
    // compose(m, k) applies k first, then m
    fn compose2(m: Affine2, k: Affine2) -> Affine2 =
        Affine2(fl_add(fl_mul(field(m, a), field(k, a)), fl_mul(field(m, b), field(k, d))), fl_add(fl_mul(field(m, a), field(k, b)), fl_mul(field(m, b), field(k, e))),
            fl_add(fl_add(fl_mul(field(m, a), field(k, c)), fl_mul(field(m, b), field(k, f))), field(m, c)),
            fl_add(fl_mul(field(m, d), field(k, a)), fl_mul(field(m, e), field(k, d))), fl_add(fl_mul(field(m, d), field(k, b)), fl_mul(field(m, e), field(k, e))),
            fl_add(fl_add(fl_mul(field(m, d), field(k, c)), fl_mul(field(m, e), field(k, f))), field(m, f)))
    fn invert2(m: Affine2) -> Option<Affine2> =
        let det = fl_sub(fl_mul(field(m, a), field(m, e)), fl_mul(field(m, b), field(m, d))) in
        if_then_else(fl_eq(det, 0.0), none(Affine2),
            let ia = fl_div(field(m, e), det) in let ib = fl_div(fl_neg(field(m, b)), det) in
            let id = fl_div(fl_neg(field(m, d)), det) in let ie = fl_div(field(m, a), det) in
            some(Affine2(ia, ib, fl_neg(fl_add(fl_mul(ia, field(m, c)), fl_mul(ib, field(m, f)))), id, ie, fl_neg(fl_add(fl_mul(id, field(m, c)), fl_mul(ie, field(m, f)))))))

    // ---- 3D vectors ----
    fn v3(x: Float, y: Float, z: Float) -> Vec3 = Vec3(x, y, z)
    fn v3_add(p: Vec3, q: Vec3) -> Vec3 = Vec3(fl_add(field(p, x), field(q, x)), fl_add(field(p, y), field(q, y)), fl_add(field(p, z), field(q, z)))
    fn v3_sub(p: Vec3, q: Vec3) -> Vec3 = Vec3(fl_sub(field(p, x), field(q, x)), fl_sub(field(p, y), field(q, y)), fl_sub(field(p, z), field(q, z)))
    fn v3_scale(p: Vec3, k: Float) -> Vec3 = Vec3(fl_mul(field(p, x), k), fl_mul(field(p, y), k), fl_mul(field(p, z), k))
    fn v3_dot(p: Vec3, q: Vec3) -> Float = fl_add(fl_add(fl_mul(field(p, x), field(q, x)), fl_mul(field(p, y), field(q, y))), fl_mul(field(p, z), field(q, z)))
    fn v3_cross(p: Vec3, q: Vec3) -> Vec3 =
        Vec3(fl_sub(fl_mul(field(p, y), field(q, z)), fl_mul(field(p, z), field(q, y))),
            fl_sub(fl_mul(field(p, z), field(q, x)), fl_mul(field(p, x), field(q, z))),
            fl_sub(fl_mul(field(p, x), field(q, y)), fl_mul(field(p, y), field(q, x))))
    fn v3_norm(p: Vec3) -> Float = fl_sqrt(v3_dot(p, p))
    fn v3_dist(p: Vec3, q: Vec3) -> Float = v3_norm(v3_sub(p, q))
    fn v3_normalize(p: Vec3) -> Option<Vec3> = let l = v3_norm(p) in if_then_else(fl_eq(l, 0.0), none(Vec3), some(v3_scale(p, fl_div(1.0, l))))
    fn v3_to_fv(p: Vec3) -> FloatVec = unwrap_or(fv_from_list(list(field(p, x), field(p, y), field(p, z))), fv_empty())

    // ---- 4×4 row-major matrices as FloatVec(16), homogeneous points ----
    fn m4(xs: List<Float>) -> FloatVec = unwrap_or(fv_from_list(xs), fv_empty())
    fn m4_identity() -> FloatVec = m4(list(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0))
    fn m4_translation(t: Vec3) -> FloatVec = m4(list(1.0, 0.0, 0.0, field(t, x), 0.0, 1.0, 0.0, field(t, y), 0.0, 0.0, 1.0, field(t, z), 0.0, 0.0, 0.0, 1.0))
    fn m4_scaling(s: Vec3) -> FloatVec = m4(list(field(s, x), 0.0, 0.0, 0.0, 0.0, field(s, y), 0.0, 0.0, 0.0, 0.0, field(s, z), 0.0, 0.0, 0.0, 0.0, 1.0))
    fn m4_rotation_x(t: Float) -> FloatVec = let c = fl_cos(t) in let s = fl_sin(t) in m4(list(1.0, 0.0, 0.0, 0.0, 0.0, c, fl_neg(s), 0.0, 0.0, s, c, 0.0, 0.0, 0.0, 0.0, 1.0))
    fn m4_rotation_y(t: Float) -> FloatVec = let c = fl_cos(t) in let s = fl_sin(t) in m4(list(c, 0.0, s, 0.0, 0.0, 1.0, 0.0, 0.0, fl_neg(s), 0.0, c, 0.0, 0.0, 0.0, 0.0, 1.0))
    fn m4_rotation_z(t: Float) -> FloatVec = let c = fl_cos(t) in let s = fl_sin(t) in m4(list(c, fl_neg(s), 0.0, 0.0, s, c, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0))
    fn m4_mul(a: FloatVec, b: FloatVec) -> FloatVec = unwrap_or(fv_matmul(a, b, 4, 4, 4), fv_empty())
    fn m4_transpose(a: FloatVec) -> FloatVec = unwrap_or(fv_transpose(a, 4, 4), fv_empty())
    // m·(p, 1), divided by w; none for a malformed matrix or w = 0
    fn m4_apply(m: FloatVec, p: Vec3) -> Option<Vec3> =
        match fv_matmul(m, m4(list(field(p, x), field(p, y), field(p, z), 1.0)), 4, 4, 1) { none => none(Vec3), some(r) =>
            let w = unwrap_or(fv_get(r, 3), 0.0) in
            if_then_else(fl_eq(w, 0.0), none(Vec3), some(Vec3(fl_div(unwrap_or(fv_get(r, 0), 0.0), w), fl_div(unwrap_or(fv_get(r, 1), 0.0), w), fl_div(unwrap_or(fv_get(r, 2), 0.0), w)))) }

    // ---- rays: origin + t·direction, t >= 0 ----
    fn ray_plane(origin: Vec3, dir: Vec3, point: Vec3, normal: Vec3) -> Option<Float> =
        let den = v3_dot(dir, normal) in
        if_then_else(fl_eq(den, 0.0), none(Float), let t = fl_div(v3_dot(v3_sub(point, origin), normal), den) in if_then_else(fl_le(0.0, t), some(t), none(Float)))
    fn ray_sphere(origin: Vec3, dir: Vec3, center: Vec3, radius: Float) -> Option<Float> =
        let oc = v3_sub(origin, center) in let a = v3_dot(dir, dir) in let b = fl_mul(2.0, v3_dot(oc, dir)) in
        let c = fl_sub(v3_dot(oc, oc), fl_mul(radius, radius)) in let disc = fl_sub(fl_mul(b, b), fl_mul(fl_mul(4.0, a), c)) in
        if_then_else(or(fl_lt(disc, 0.0), fl_eq(a, 0.0)), none(Float),
            let q = fl_sqrt(disc) in let t0 = fl_div(fl_sub(fl_neg(b), q), fl_mul(2.0, a)) in let t1 = fl_div(fl_add(fl_neg(b), q), fl_mul(2.0, a)) in
            if_then_else(fl_le(0.0, t0), some(t0), if_then_else(fl_le(0.0, t1), some(t1), none(Float))))
    // Möller–Trumbore; none when parallel or missed
    fn ray_triangle(origin: Vec3, dir: Vec3, a: Vec3, b: Vec3, c: Vec3) -> Option<Float> =
        let e1 = v3_sub(b, a) in let e2 = v3_sub(c, a) in let p = v3_cross(dir, e2) in let det = v3_dot(e1, p) in
        if_then_else(fl_eq(det, 0.0), none(Float),
            let inv = fl_div(1.0, det) in let s = v3_sub(origin, a) in let u = fl_mul(v3_dot(s, p), inv) in
            if_then_else(or(fl_lt(u, 0.0), fl_lt(1.0, u)), none(Float),
                let q = v3_cross(s, e1) in let v = fl_mul(v3_dot(dir, q), inv) in
                if_then_else(or(fl_lt(v, 0.0), fl_lt(1.0, fl_add(u, v))), none(Float),
                    let t = fl_mul(v3_dot(e2, q), inv) in if_then_else(fl_le(0.0, t), some(t), none(Float)))))

    theorem unit_square_has_area_one: fl_eq(polygon_area(list(Vec2(0.0, 0.0), Vec2(1.0, 0.0), Vec2(1.0, 1.0), Vec2(0.0, 1.0))), 1.0)
}
