// Homology/cohomology of adjacent explicitly supplied prime-field boundaries.
module std.homology_fp
import std.matrix_fp

theory StdHomologyFp extends StdMatrixFp {
    record FpHomology(betti: Nat, cycles: FpMatrix)
    private fn hp_homology(down: FpMatrix, up: FpMatrix) -> Result<FpHomology, Text> =
        match pmat_kernel(down) {
            err(e) => err(e, FpHomology),
            ok(k) => match pmat_extend_basis(up, k) {
                err(e) => err(e, FpHomology),
                ok(cycles) => ok(FpHomology(field(cycles, cols), cycles), Text)
            }
        }
    fn homology_fp(down: FpMatrix, up: FpMatrix) -> Result<FpHomology, Text> =
        if_then_else(not(and(pmat_valid(down), pmat_valid(up))), err("invalid prime boundary matrix", FpHomology),
            match pmat_mul(down, up) {
                err(e) => err(e, FpHomology),
                ok(product) => if_then_else(pmat_is_zero(product), hp_homology(down, up), err("boundary squared is nonzero", FpHomology))
            })
    fn cohomology_fp(down: FpMatrix, up: FpMatrix) -> Result<FpHomology, Text> =
        match pmat_transpose(up) { err(e) => err(e, FpHomology), ok(d) => match pmat_transpose(down) { err(e) => err(e, FpHomology), ok(u) => homology_fp(d, u) } }
    theorem prime_point_homology: nat_eq(field(unwrap_or(homology_fp(FpMatrix(3, 0, 1, empty_list(Nat)), FpMatrix(3, 1, 0, empty_list(Nat))), FpHomology(999, FpMatrix(3, 0, 0, empty_list(Nat)))), betti), 1)
}
