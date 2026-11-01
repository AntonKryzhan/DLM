# v0.83 — Text, Line I/O, Program Arguments, `fn main`

## Text literals

```text
"hello"   "tab:\t"   "quote: \""   "backslash: \\"   "line\n"   "nul: \0"
```

Literals live on one line. Commas, parentheses, `=>` and `//` inside a literal are ordinary characters. Unknown
escapes and unterminated literals are parse errors. The parser replaces literals with reserved placeholders
`__text_N` while it splits a line, so names starting with `__text_` cannot be written in source.

## Text operations (pure, total)

| builtin | type |
|---|---|
| `concat(a, b)` | `Text, Text -> Text` |
| `text_length(t)` | `Text -> Nat` (Unicode scalar values) |
| `text_eq(a, b)` | `Text, Text -> Bool` |
| `nat_to_text(n)` | `Nat -> Text` (decimal) |
| `text_to_nat(t)` | `Text -> Option<Nat>` (`none` unless all decimal digits) |
| `substring(t, start, len)` | `Text, Nat, Nat -> Text` (clamped, never fails) |
| `lines(t)`, `words(t)` | `Text -> List<Text>` |
| `join(parts, sep)` | `List<Text>, Text -> Text` |

The symbolic kernel treats a literal as an irreducible value, and distinct literals as distinct values. It folds
`concat`, `text_length` and `text_eq` of literals and `nat_to_text` of constants. Symbolic theorems may quantify
over `Text` variables. The runtime cross-check evaluates them on the sample texts `""`, `"a"`, `"ab c"`, `"12"` and
`"\"x\"\n"`.

## Input and output (effects)

| builtin | effect | meaning |
|---|---|---|
| `read_line()` | `io@stdin` | `Option<Text>`: the next line without its line ending, `none` at the end |
| `read_all()` | `io@stdin` | the unread rest of stdin |
| `args()` | `io@args` | program arguments, `List<Text>` |
| `print_line(t)` | `io@stdout` | prints a Text |
| `print_text`, `print_decimal`, `print_symbolic`, `read_nat` | as before | |

Effects are tracked by function contracts. A function that prints or reads is `Effectful` and cannot appear in
a theorem, which must be pure and total (`E0909 ... uses effects [io@stdout]`). Sequencing uses `let`:
`let _ = print_line("a") in print_line("b")`.

## `fn main`

```dlm
theory WordCount {
    fn count_words(t: Text) -> Nat = length(words(t))
    fn main() -> Nat =
        let input = read_all() in
        let _ = print_line(concat("words: ", nat_to_text(count_words(input)))) in
        0
}
```

- After the root module's theory items run, `dlm run` calls `main()` if one theory of the root module declares it.
- `main` takes no parameters, and a module has at most one (`E0928`).
- A Nat result is the process exit status (saturated at 255). Other results are printed as `main: <value>` with
  status 0.
- `dlm run file.dlm [--stdin TEXT | --stdin-file PATH] [-- arg ...]`. API: `Runtime::with_stdin(..).with_args(..)`,
  `RunReport::main`, `RunReport::exit_code()`.
