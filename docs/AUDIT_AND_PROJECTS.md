# v0.81 — `dlm audit`, Projects and Mutual Recursion

## `dlm audit`

`dlm audit [--trusted-only|--allow-axioms] [--lib DIR] file.dlm` checks the program and prints its trusted base:

```text
DLM-AUDIT v1
module: examples.std_library_usage
imports: std.nat, std.list
status: closed
trusted components: symbolic-kernel (3)
runtime cross-check: agree=3 skipped=0
statements: 3 proved, 0 axioms, 0 open goals

theorem App.fast_add_twice
  proposition: forall a: Nat. nat_eq(add_fast(a, a), mul(2, a))
  method: symbolic:normalize
  trust: Builtin
  lemmas: add_acc_spec, add_fast_spec
  crosscheck: agree:cases=6
fingerprint: fnv64:...
```

The audit is read from passports, not from separate bookkeeping. Evidence events (`proof:...:theorem=<name>`), lemma
uses and axiom admissions travel in passport histories through lemmas, `extends` and module boundaries, so:

- **lemmas** are transitive: `add_acc_spec`, private to `std.nat`, is listed because `add_fast_spec` used it;
- **axioms** are transitive and attributed. An axiom imported from another module appears under every theorem that
  depends on it (`axiom dependency f_zero: ... <- App.uses`), and a declared but unused axiom is reported as unused;
- a theorem whose trust is `Axiom` or worse but whose history names no axiom gets a warning instead of being hidden.

| method | trusted component |
|---|---|
| `computation`, `exhaustion:below=N`, `exhaustion:finite` | `runtime-evaluator` |
| `symbolic:normalize`, `symbolic:induction` | `symbolic-kernel` (independently cross-checked by the runtime) |
| `axiom`, or any axiom dependency | `axioms` |
| Oracle / Unsafe trust, `kind = unsafe` bridges | `oracle` / `unsafe` |

Status: `closed` (only the evaluator and/or the kernel), `axiom-dependent` (an axiom is declared or used, or a value
has Axiom trust), `unsafe` (Oracle/Unsafe trust or an unsafe bridge). Conjectures are listed as open goals; they
never support a theorem. The fingerprint is FNV-64 of the audit body.

## Projects

A project is a directory with `dlm.project`:

```text
name = DLM
lib = lib                        # search paths for imports
roots = lib/std, examples/valid  # files, or directories of .dlm files
policy = research                # research | trusted_only
```

`dlm project [--trusted-only|--allow-axioms] [--audit] [DIR|dlm.project]` loads every root as a program, checks it
and prints one line per root plus totals. It exits with 1 if any root fails. The repository ships a `dlm.project`
that covers the standard library and every valid example:

```text
roots: 63  failed: 0  proved: 56  axioms: 1  open goals: 1  status: unsafe
```

The status is `unsafe` because examples declare unsafe bridges on purpose. Both `lib/std` modules are `closed`. The
API is `load_project`, `check_project` and `ProjectReport::render`.

## Mutual recursion

```dlm
theory Parity {
    mutual {
        fn even_n(n: Nat) -> Bool decreasing n = if_then_else(nat_eq(n, 0), true, odd_n(pred(n)))
        fn odd_n(n: Nat) -> Bool decreasing n = if_then_else(nat_eq(n, 0), false, even_n(pred(n)))
    }
    theorem parity_split(n: Nat): or(even_n(n), odd_n(n)) by induction n step 2
}
```

- Every member declares one measured parameter (Nat, or List/Sequence), all of the same kind.
- Every call to a member, the function itself included, must be guarded by the caller's measure check and pass a
  strictly smaller value of the caller's measure (`pred`, `monus(m, k)`, `tail`) at the callee's measured position.
  The shared measure therefore decreases on every step around any cycle of the group.
- Members are checked in order, and calls to later members are typed by their signatures. Each member is assumed
  pure and total while the others are checked, so the whole group must be pure and total, or it is rejected and no
  member stays registered.
- Members may not be called inside lambdas, and the block needs at least two functions. Outside a block, a call to a
  function declared later is `E0935 ... declared later in theory`.
- The symbolic kernel unfolds mutual definitions like any measured function. The runtime checks the measure on every
  call.
