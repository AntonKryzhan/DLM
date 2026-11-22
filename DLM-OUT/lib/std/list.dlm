// std.list — List<Nat> functions with proved specifications.
module std.list
import std.nat

theory StdList extends StdNat {
    fn len(xs: List<Nat>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, 1 + len(tail(xs)))
    fn sum_list(xs: List<Nat>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, unwrap_or(head(xs), 0) + sum_list(tail(xs)))
    fn inc_all(xs: List<Nat>) -> List<Nat> = map(xs, fun(x: Nat, x + 1))
    fn double_all(xs: List<Nat>) -> List<Nat> = map(xs, double)
    fn count_acc(xs: List<Nat>, acc: Nat) -> Nat decreasing xs = if_then_else(is_empty(xs), acc, count_acc(tail(xs), acc + 1))

    theorem len_spec(xs: List<Nat>): nat_eq(len(xs), length(xs)) by induction xs
    theorem cons_length(x: Nat, xs: List<Nat>): nat_eq(length(cons(x, xs)), length(xs) + 1) by normalize
    theorem inc_all_length(xs: List<Nat>): nat_eq(length(inc_all(xs)), length(xs)) by induction xs
    theorem inc_all_sum(xs: List<Nat>): nat_eq(sum_list(inc_all(xs)), sum_list(xs) + length(xs)) by induction xs
    theorem double_all_sum(xs: List<Nat>): nat_eq(sum_list(double_all(xs)), mul(2, sum_list(xs))) by induction xs
    theorem count_acc_spec(xs: List<Nat>, acc: Nat): nat_eq(count_acc(xs, acc), length(xs) + acc) by induction xs generalizing acc
    theorem sum_bounds_length(xs: List<Nat>): not(length(xs) > sum_list(inc_all(xs))) by induction xs
}
