# v0.80 — Modules, Theory Extension and the Standard Library

## Syntax

```text
module app.main
import std.list                      (* module name; resolved to a file *)

theory App extends StdList {         (* inherits StdList's declarations *)
    private fn helper(n: Nat) -> Nat = n + 1
    fn f(xs: List<Nat>) -> Nat = sum_list(inc_all(xs)) + helper(0)
    theorem f_spec(xs: List<Nat>): nat_eq(f(xs), sum_list(xs) + length(xs) + 1) by normalize using inc_all_sum
}
```

- `import a.b.c` names a module by its `module` declaration.
- `theory Name extends Base` starts `Name` with the declarations of `Base`: checked `fn`s, records, and theorems /
  axioms / conjectures, with their passports and the lemma registry. Base can be declared earlier in the same module
  or in an imported module.
- `private` before `fn`, `record`, `theorem`, `axiom` or `conjecture` hides the name from theories of **other
  modules** that extend this theory, also through chains (`C extends B extends A`). Theories of the same module see
  private names.

## Rules

| rule | diagnostic |
|---|---|
| imported module not found | `E0917 ModuleImportError ... not found; looked for ...` |
| file declares a different module name | `E0917 ... declares module 'x', but it was imported as 'y'` |
| import cycle | `E0917 import cycle: a -> b -> a` |
| one theory name in two modules | `E0917 theory 'T' is declared in modules 'a' and 'b'` |
| extends an unknown or later theory, or itself | `E0917 ... extends unknown theory` / `cannot extend itself` |
| derived code uses a private name of another module | `E0918 ModuleInterfaceError 'x' is private to module 'm'` |
| derived theory redeclares an inherited name (private ones included) | `E0002 redeclares 'x' inherited from 'Base'` |
| module with imports checked without its imports | `E0917 ... imports 'x', which is not loaded` |

Values (`let`) are theory state, not declarations, and are not inherited. Imported modules contribute declarations
only; their expressions and effects are not executed by `dlm run`.

## Trust

Inherited passports keep their trust and history. An imported axiom used as a lemma still taints the result with
`Axiom`, and `--trusted-only` still rejects it. Module boundaries and `private` affect visibility, not trust.

## Resolution of files

`import a.b.c` from a file in directory `D` (root file in `R`) is looked up in this order:

```text
D/a/b/c.dlm   D/c.dlm   R/a/b/c.dlm   P/a/b/c.dlm for each search path P
```

Search paths come from `--lib <dir>` (repeatable) and the `DLM_PATH` environment variable. The API is
`load_program(path)` (uses `DLM_PATH`), `load_program_with_paths(path, &[dirs])`, and `link_program(root, modules)`
for in-memory modules. `Checker::check_program(&program)` checks imported modules first. The report describes the
root module, and diagnostics from imported modules are prefixed with `in imported module '<name>':`.
`Runtime::run_program(&program)` executes the root.

```text
dlm check --lib lib examples/valid/std_library_usage.dlm
DLM_PATH=lib dlm run examples/valid/std_library_usage.dlm      # prints 9, 55, 42
```

## Standard library (`lib/std`)

| module | theory | contents |
|---|---|---|
| `std.nat` | `StdNat` | `add`, `double`, `square`, `sum_to`, `fact`, `max`, `min`, `le`, `half`, `even`, `add_fast` (private accumulator); 13 proved theorems: `add_spec`, `gauss`, `fact_positive`, `square_ge`, `half_bound`, `even_or_next`, ... |
| `std.list` | `StdList extends StdNat` | `len`, `sum_list`, `inc_all`, `double_all`, `count_acc`; 7 proved theorems: `len_spec`, `inc_all_sum`, `double_all_sum`, `count_acc_spec`, ... |

Every library theorem is proved symbolically and cross-checked by the runtime. A corpus test checks every
`lib/std` module under `--trusted-only`.

## Nested lemma patterns

Lemmas whose left side nests calls (`sum_list(inc_all(xs))`) are matched syntactically: calls must agree in name
and arity, pattern variables bind to the normalized values of the matching sub-expressions, and other sub-terms
must normalize to the same key. This is tried before the arguments are normalized, so the pattern is still visible.
A fuzz test applies kernel-proved nested lemmas to 2000 statements, and every proved statement is re-checked by the
runtime. A matcher that ignores the inner function name is caught.
