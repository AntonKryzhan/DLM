// std.homology_f2 — homology of explicitly supplied boundary matrices.
module std.homology_f2
import std.matrix_f2

theory StdHomologyF2 extends StdMatrixF2 {
    record F2Homology(betti: Nat, cycles: F2Matrix)

    private fn hf2_homology(down: F2Matrix, up: F2Matrix) -> Result<F2Homology, Text> =
        match mat_kernel(down) {
            err(e) => err(e, F2Homology),
            ok(k) => match mat_extend_basis(up, k) {
                err(e) => err(e, F2Homology),
                ok(cycles) => ok(F2Homology(field(cycles, cols), cycles), Text)
            }
        }
    fn homology_f2(down: F2Matrix, up: F2Matrix) -> Result<F2Homology, Text> =
        if_then_else(not(and(mat_valid(down), mat_valid(up))), err("invalid F2 boundary matrix", F2Homology),
            match mat_mul(down, up) {
                err(e) => err(e, F2Homology),
                ok(product) => if_then_else(mat_is_zero(product), hf2_homology(down, up), err("boundary squared is nonzero", F2Homology))
            })
    fn cohomology_f2(down: F2Matrix, up: F2Matrix) -> Result<F2Homology, Text> =
        match mat_transpose(up) {
            err(e) => err(e, F2Homology),
            ok(d) => match mat_transpose(down) { err(e) => err(e, F2Homology), ok(u) => homology_f2(d, u) }
        }

    private fn hf2_chain_tail(ds: List<F2Matrix>, previous: F2Matrix) -> Bool decreasing ds =
        if_then_else(is_empty(ds), true,
            let current = unwrap_or(head(ds), F2Matrix(0, 0, empty_list(Bool))) in
            match mat_mul(previous, current) { err(e) => false, ok(product) => if_then_else(mat_is_zero(product), hf2_chain_tail(tail(ds), current), false) })
    fn chain_valid_f2(ds: List<F2Matrix>) -> Bool =
        if_then_else(length(ds) > 16, false,
            if_then_else(is_empty(ds), true,
                let first = unwrap_or(head(ds), F2Matrix(0, 0, empty_list(Bool))) in
                if_then_else(and(mat_valid(first), nat_eq(field(first, rows), 0)), hf2_chain_tail(tail(ds), first), false)))
    private fn hf2_groups(ds: List<F2Matrix>) -> Result<List<F2Homology>, Text> decreasing ds =
        if_then_else(is_empty(ds), ok(empty_list(F2Homology), Text),
            let down = unwrap_or(head(ds), F2Matrix(0, 0, empty_list(Bool))) in
            let up = unwrap_or(head(tail(ds)), F2Matrix(field(down, cols), 0, empty_list(Bool))) in
            match hf2_homology(down, up) {
                err(e) => err(e, List(F2Homology)),
                ok(group) => match hf2_groups(tail(ds)) { err(e) => err(e, List(F2Homology)), ok(groups) => ok(cons(group, groups), Text) }
            })
    fn chain_homology_f2(ds: List<F2Matrix>) -> Result<List<F2Homology>, Text> =
        if_then_else(chain_valid_f2(ds), hf2_groups(ds), err("invalid F2 chain complex or degree budget", List(F2Homology)))
    // Ground checks of a point and an invalid boundary, not a general proof.
    theorem point_homology: nat_eq(field(unwrap_or(homology_f2(F2Matrix(0, 1, empty_list(Bool)), F2Matrix(1, 0, empty_list(Bool))), F2Homology(999, F2Matrix(0, 0, empty_list(Bool)))), betti), 1)
    theorem reject_nonzero_boundary_square: is_err(homology_f2(F2Matrix(1, 1, list(true)), F2Matrix(1, 1, list(true))))

}
