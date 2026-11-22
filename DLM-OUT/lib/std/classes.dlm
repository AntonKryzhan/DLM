// Standard type classes (stage E, docs/GENERICS.md): Show, Monoid with instances for the base types and
// generic instances for List, Option. Class methods resolve by the argument type: show(list(1, 2)).
module std.classes

theory StdClasses {
    class Show<T> {
        fn show(x: T) -> Text
    }
    instance Show<Nat> {
        fn show(x: Nat) -> Text = nat_to_text(x)
    }
    instance Show<Int> {
        fn show(x: Int) -> Text = int_to_text(x)
    }
    instance Show<Bool> {
        fn show(x: Bool) -> Text = if_then_else(x, "true", "false")
    }
    instance Show<Text> {
        fn show(x: Text) -> Text = concat("\"", concat(x, "\""))
    }
    instance<A: Show> Show<List<A>> {
        fn show(xs: List<A>) -> Text = concat("[", concat(join(map(xs, fun(x: A, show(x))), ", "), "]"))
    }
    instance<A: Show> Show<Option<A>> {
        fn show(o: Option<A>) -> Text = match o { none => "none", some(v) => concat("some(", concat(show(v), ")")) }
    }

    // v0.113: superclasses and default methods. `mempty` takes a witness of its type so that the
    // instance is determined by the argument.
    fn mtimes_loop<T: Semigroup>(acc: T, a: T, n: Nat) -> T decreasing n = if_then_else(nat_eq(n, 0), acc, mtimes_loop(mappend(acc, a), a, pred(n)))
    class Semigroup<T> {
        fn mappend(a: T, b: T) -> T
        fn mtimes(a: T, n: Nat) -> T = mtimes_loop(a, a, pred(n))
    }
    class Monoid<T>: Semigroup {
        fn mempty(witness: T) -> T
    }
    instance Semigroup<Nat> {
        fn mappend(a: Nat, b: Nat) -> Nat = a + b
    }
    instance Monoid<Nat> {
        fn mempty(witness: Nat) -> Nat = 0
    }
    instance Semigroup<Text> {
        fn mappend(a: Text, b: Text) -> Text = concat(a, b)
    }
    instance Monoid<Text> {
        fn mempty(witness: Text) -> Text = ""
    }
    instance<A> Semigroup<List<A>> {
        fn mappend(a: List<A>, b: List<A>) -> List<A> = fold(fold(a, empty_list(A), fun(acc: List<A>, x: A, cons(x, acc))), b, fun(acc: List<A>, x: A, cons(x, acc)))
    }
    instance<A> Monoid<List<A>> {
        fn mempty(witness: List<A>) -> List<A> = empty_list(A)
    }
    fn mconcat<T: Monoid>(xs: List<T>, witness: T) -> T = fold(xs, mempty(witness), fun(acc: T, x: T, mappend(acc, x)))
    fn show_all<T: Show>(xs: List<T>) -> Text = join(map(xs, fun(x: T, show(x))), "\n")

    theorem show_of_three: text_eq(show(3), "3")
}
