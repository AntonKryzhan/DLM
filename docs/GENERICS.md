# Generics (stage E-min), 0.110.0

Templates: `fn name<T, U>(...) -> R = body` and `record Name<T>(field: T, ...)`; type parameters are
capitalized names. Uses take **explicit** type arguments: `id<Nat>(5)`, `Box<Text>("a")` (constructor),
types `Box<List<Nat>>` in signatures, fields, lambda parameters and type arguments (`none(T)`, `empty_list(Box<T>)`).
There are no constraints: equality and order are passed as function values (`Fn<K, K, Bool>`).

`crate::generics::expand_program` runs when a `Program` is built (single module or linked imports): templates
are removed and every used instance becomes an ordinary declaration at the template's place in its own theory
(name `head__of__<escaped canonical args>`, injective, `mangle`/`demangle`), with substituted types and fresh
expression ids; instances found inside instances are generated until none are new (≤ 1024, else a
polymorphic-recursion error). Private templates give private instances. The checker, Typed Core, IR,
interpreter and native C see only monomorphic code; a template is checked per instance (errors name the
instance). Errors: wrong number of type arguments, malformed parameter lists, unclosed `<`.

Library: `std.collections` — `Duo`, `Stack`, `Queue`, `Entry`, `AssocMap`; `list_reverse/append/filter/any/all/
count/find/take/drop/last/contains/zip/max_by/insert_by/sort_by`, `stack_*`, `queue_*`, `map_*`.
Tests: `tests/generics.rs`.

## v0.111: inference and bounds

* Type arguments may be omitted when the call's arguments determine them (`list_reverse(list(1, 2))`,
  `Duo(7, "x")`): a local inference over literals, parameters, `let`, lambdas, records, builtins and known
  signatures unifies the template's parameter types (`infer_type_arguments`); otherwise
  `cannot infer the type arguments of f; write f<T>(...)`.
* Bounds `T: Eq`, `T: Ord` (`T: Eq + Ord`) are checked per instance (`does not satisfy`). New builtins:
  `equal(a, b)` — structural equality of Eq types (no functions/resources inside), `less(a, b)` — Nat, Int,
  Text (byte order). Interpreter and C (`dv_equal`) agree.
* An instance whose type arguments name records the template's theory cannot see is placed in the requesting
  theory, after its records (spans re-filed to that module).
* `std.collections`: `list_contains_eq`, `list_index_of`, `list_dedup`, `list_sort`, `list_min`, `list_max`,
  `map_get_eq`, `map_put_eq`.

## v0.112: type classes

```text
class Show<T> {                       instance Show<Nat> {
    fn show(x: T) -> Text                 fn show(x: Nat) -> Text = nat_to_text(x)
}                                     }
instance<A: Show> Show<List<A>> {     fn describe<T: Show + Size>(x: T) -> Text = ...
    fn show(xs: List<A>) -> Text = concat("[", join(map(xs, fun(x: A, show(x))), ","))
}
```

A class has one type parameter and method signatures (no bodies). A concrete instance method becomes the
ordinary fn `show__of__Nat`; a generic instance is a template generated on demand for the requested type
(by unification with its head; overlapping matches are rejected). Calls resolve by the inferred argument type;
a class bound `T: Show` on a template is checked per instance (an instance must exist) and such templates are
instantiated in the requesting theory, which sees its instances. Checks: unknown class, missing or extra
methods, method types (class signature with `T` substituted), duplicate and overlapping instances, missing
instance (`no instance Show<Text> provides method 'show'`). Library: `std.classes` — `Show` (Nat, Int, Bool,
Text, List, Option), `Monoid` (Nat, Text, List; `mempty(witness)`), `mconcat`, `show_all`.
Tests: `tests/type_classes.rs`.

## v0.113: superclasses and default methods

`class Ord2<T>: Eq2 + Show { fn before(a: T, b: T) -> Bool  fn max_of(a: T, b: T) -> T = if_then_else(before(a, b), b, a) }`.
An instance of a class needs instances of all its (transitive) superclasses for the same type
(`instance Before<Nat> requires an instance Same<Nat>`); generic instances may satisfy them through generic
instances. A class method with a body is a default: instances that omit it receive it with `T` replaced by the
instance type (`apply_defaults`; a template for generic instances); an instance may override it.
`std.classes`: `Semigroup` (`mappend`, default `mtimes`) and `Monoid: Semigroup` (`mempty`).
Ownership diagnostics now name the resource type (`File 'h' is used after it was moved`).

## v0.130: callable constraints and builtin associated projections

`Callable`, `UnaryCallable`, `NullaryCallable` and `PureCallable` constrain `Fn`
and explicitly bounded `FnEffect` instances. `ArgOf<F>`, `ResultOf<F>`, `ItemOf<C>`,
`OkOf<R>` and `ErrorOf<R>` reduce after substitution, including nested projections
and record fields. Inference defers projected arguments until ordinary arguments
bind their source type. The complete callable type preserves its effect bounds.
See [EFFECT_FUNCTIONS.md](EFFECT_FUNCTIONS.md) for contracts, examples and limits.
User-defined associated types and higher-kinded abstraction remain open.
