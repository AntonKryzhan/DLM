module examples.std_library_usage
import std.list

theory App extends StdList {
    fn total_plus_count(xs: List<Nat>) -> Nat = sum_list(inc_all(xs))
    fn quad(n: Nat) -> Nat = double(double(n))

    theorem quad_spec(n: Nat): nat_eq(quad(n), mul(4, n)) by normalize using double_spec
    theorem total_plus_count_spec(xs: List<Nat>): nat_eq(total_plus_count(xs), sum_list(xs) + length(xs)) by normalize using inc_all_sum
    theorem fast_add_twice(a: Nat): nat_eq(add_fast(a, a), mul(2, a)) by normalize using add_fast_spec

    let numbers = list(1, 2, 3)
    let total = total_plus_count(numbers)
    let gauss_10 = sum_to(10)
    let fast = add_fast(20, 22)
    print_decimal(total)
    print_decimal(gauss_10)
    print_decimal(fast)
}
