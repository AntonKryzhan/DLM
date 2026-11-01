module examples.rational_matrix_demo
import std.matrix_q

theory App extends StdMatrixQ {
    let a = QMatrix(1, 2, list(Rational(false, 1, 2), Rational(false, 1, 3)))
    print_symbolic(qmat_rank(a))
    print_symbolic(qmat_kernel(a))
    print_symbolic(qmat_mul(a, unwrap_or(qmat_kernel(a), QMatrix(0, 0, empty_list(Rational)))))
    print_symbolic(is_err(qmat_mul(QMatrix(1, 1, list(Rational(false, 4294967295, 1))), QMatrix(1, 1, list(Rational(false, 2, 1))))))
}
