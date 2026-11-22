module examples.matrix_homology_f2
import std.homology_f2

theory MatrixHomologyDemo extends StdHomologyF2 {
    fn betti_or_error(h: Result<F2Homology, Text>) -> Nat = match h { ok(g) => field(g, betti), err(e) => 999 }
    let zero = F2Matrix(0, 0, empty_list(Bool))
    // Boundary of the three-edge circle, columns (01), (12), (02).
    let d0 = F2Matrix(0, 3, empty_list(Bool))
    let d1 = F2Matrix(3, 3, list(true, false, true, true, true, false, false, true, true))
    let d2 = F2Matrix(3, 0, empty_list(Bool))
    let filled = F2Matrix(3, 1, list(true, true, true))
    print_decimal(unwrap_or(mat_rank(d1), 999))
    print_decimal(betti_or_error(homology_f2(d0, d1)))
    print_decimal(betti_or_error(homology_f2(d1, d2)))
    print_decimal(betti_or_error(cohomology_f2(d1, d2)))
    print_decimal(betti_or_error(homology_f2(d1, filled)))
    print_symbolic(mat_kernel(d1))
    print_symbolic(chain_homology_f2(list(d0, d1)))
    // Deliberately malformed records are rejected by the public API.
    print_symbolic(mat_rank(F2Matrix(2, 2, list(true))))
    print_symbolic(homology_f2(F2Matrix(1, 1, list(true)), F2Matrix(1, 1, list(true))))
}
