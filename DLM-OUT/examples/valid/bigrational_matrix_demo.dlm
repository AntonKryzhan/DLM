module examples.bigrational_matrix_demo
import std.matrix_bigq

theory App extends StdMatrixBigQ {
    let b = ExactBudget(512, 1000000000000000)
    let zero = BigRational(false, empty_list(Nat), list(1))
    let one = BigRational(false, list(1), list(1))
    let big = unwrap_or(bigrat_parse(b, "340282366920938463463374607431768211456"), BigRationalCalc(zero, b))
    let a = BigMatrix(2, 2, list(field(big, value), one, one, zero))
    fn scalar(r: Result<BigRationalCalc, Text>) -> Text = match r {
        err(e) => e, ok(v) => match bigrat_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) }
    }
    fn matrix_text(r: Result<BigMatrixCalc, Text>) -> Text = match r {
        err(e) => e, ok(v) => match bmat_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) }
    }
    fn rank(r: Result<BigMatrixRank, Text>) -> Nat = match r { err(e) => 999, ok(v) => field(v, rank) }
    fn particular(r: Result<BigMatrixSolution, Text>) -> Result<BigMatrixCalc, Text> = match r {
        err(e) => err(e, BigMatrixCalc), ok(v) => ok(BigMatrixCalc(field(v, particular), field(v, budget)), Text)
    }
    print_line(scalar(bmat_det(field(big, budget), a)))
    print_symbolic(matrix_text(bmat_inverse(field(big, budget), a)))
    print_decimal(rank(bmat_rank(field(big, budget), a)))
    let system = BigMatrix(2, 2, list(one, BigRational(false, list(1), list(2)), BigRational(false, list(1), list(3)), one))
    let rhs = BigMatrix(2, 1, list(one, BigRational(false, list(2), list(1))))
    print_symbolic(matrix_text(particular(bmat_solve(b, system, rhs))))
}
