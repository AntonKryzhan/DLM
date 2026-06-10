# GRAMMAR.md — EBNF Grammar for DLM / ЯРД v1.0 MVP

Этот документ задаёт минимальную EBNF-грамматику `.dlm` для MVP.

## 1. Lexical conventions

```ebnf
letter      = "A".."Z" | "a".."z" | "_" ;
digit       = "0".."9" ;
ident       = letter , { letter | digit } ;
path_ident  = ident , { "::" , ident } ;
integer     = digit , { digit | "_" } ;
string      = '"' , { character } , '"' ;
comment     = "//" , { character } , newline ;
block_comment = "/*" , { character } , "*/" ;
```

Keywords:

```text
module import pub private theory bridge kind preserves transforms
in fn let type axiom theorem proof witness require effects
where if else match return as from to true false
StaticProof RuntimeWitness Result Option External Runtime Checked
```

## 2. File

```ebnf
file = module_decl , { module_item } ;
```

## 3. Module declaration

```ebnf
module_decl = "module" , module_path ;
module_path = ident , { "." , ident } ;
```

Example:

```dlm
module arithmetic.pa
```

## 4. Module items

```ebnf
module_item = import_decl
            | theory_decl
            | bridge_decl
            | alias_decl
            | test_decl
            ;
```

No value-level `let`, `fn`, `proof`, `theorem` is allowed directly at module top-level in MVP.

## 5. Imports

```ebnf
import_decl = "import" , path_ident , [ "as" , ident ] ;
```

Examples:

```dlm
import theories.pa::PA
import theories.meta::MetaArithmetic as Meta
```

## 6. Theory declaration

```ebnf
theory_decl = visibility , "theory" , ident , theory_body ;
visibility  = [ "pub" | "private" ] ;
theory_body = "{" , { theory_item } , "}" ;
```

Theory items:

```ebnf
theory_item = type_decl
            | fn_decl
            | let_decl
            | axiom_decl
            | theorem_decl
            | proof_decl
            | import_theory_decl
            ;
```

Example:

```dlm
pub theory PA {
    type Nat
    let seven = 7
}
```

## 7. Ambient theory block

```ebnf
in_theory_block = "in" , "theory" , path_ident , "{" , { theory_item } , "}" ;
```

MVP may allow `in theory` only inside tests/examples or module-level script sections.

## 8. Type declarations

```ebnf
type_decl = visibility , "type" , ident , [ ":" , type_expr ] ;
```

Examples:

```dlm
type Nat
type Term<T>
type Proof<P>
```

## 9. Function declarations

```ebnf
fn_decl = visibility , "fn" , ident , generic_params , "(" , param_list , ")" ,
          "->" , type_expr , [ effects_clause ] , [ where_clause ] , fn_body_or_semicolon ;

generic_params = [ "<" , ident , { "," , ident } , ">" ] ;
param_list = [ param , { "," , param } ] ;
param = ident , ":" , type_expr ;
effects_clause = "effects" , effect , { "," , effect } ;
fn_body_or_semicolon = block | ";" ;
```

Examples:

```dlm
fn succ(n: Nat) -> Nat;
fn read(stdin: Stdin) -> Result<External<Bytes>, IOError> effects IO, ExternalInput;
```

## 10. Let declarations

```ebnf
let_decl = "let" , ident , [ ":" , type_expr ] , "=" , expr , ";" ;
```

Example:

```dlm
let n = 7;
let g = Graham();
```

## 11. Axiom declarations

```ebnf
axiom_decl = "axiom" , ident , ":" , prop_expr , ";" ;
```

Axioms produce `TrustLevel::Axiom` unless marked builtin by the standard core.

## 12. Theorem declarations

```ebnf
theorem_decl = "theorem" , ident , ":" , prop_expr , [ theorem_body ] ;
theorem_body = "=" , proof_expr , ";" | ";" ;
```

MVP may parse theorem declarations but only check proof source kind, not full proof calculus.

## 13. Proof declarations

```ebnf
proof_decl = "proof" , ident , ":" , proof_type , "=" , proof_expr , ";" ;
proof_type = ( "StaticProof" | "RuntimeWitness" | "Proof" ) , "<" , prop_expr , ">" ;
```

Examples:

```dlm
proof p : StaticProof<seven + zero = seven> = builtin(add_zero);
witness w : RuntimeWitness<n > 0> = require(n > 0);
```

## 14. Bridge declarations

```ebnf
bridge_decl = visibility , [ trust_modifier ] , "bridge" , ident , ":" ,
              theory_ref , "->" , theory_ref , bridge_body ;

trust_modifier = "trusted" | "unsafe" ;
bridge_body = "{" , { bridge_item } , "}" ;
bridge_item = bridge_kind
            | bridge_preserves
            | bridge_transforms
            ;

bridge_kind = "kind" , "=" , bridge_kind_name , ";" ;
bridge_kind_name = "quote" | "transport" | "definitional_extension" | "soundness" | "reflection" | "unsafe_cast" ;
bridge_preserves = "preserves" , "=" , "[" , ident , { "," , ident } , "]" , ";" ;
bridge_transforms = "transforms" , ":" , { transform_rule } ;
transform_rule = type_expr , "->" , type_expr , ";" ;
```

Example:

```dlm
pub bridge PA_quote : PA -> MetaArithmetic {
    kind = quote;
    preserves = [syntax];
    transforms:
        PA.Prop -> MetaArithmetic.Term<PA.Prop>;
}
```

## 15. Expressions

```ebnf
expr = literal
     | path_ident
     | call_expr
     | binary_expr
     | unary_expr
     | block
     | if_expr
     | require_expr
     | quote_expr
     | transport_expr
     ;

literal = integer | string | "true" | "false" ;
call_expr = path_ident , "(" , [ expr , { "," , expr } ] , ")" ;
require_expr = "require" , "(" , expr , ")" ;
quote_expr = "quote" , [ "[" , path_ident , "]" ] , "(" , expr , ")" ;
transport_expr = "transport" , "[" , path_ident , "]" , "(" , expr , ")" ;
```

## 16. Types

```ebnf
type_expr = path_ident
          | type_expr , "<" , type_arg_list , ">"
          | type_expr , "[" , passport_annotation , "]"
          ;

type_arg_list = type_expr , { "," , type_expr } ;
```

## 17. Passport annotations

Explicit passport annotations are optional and limited in MVP.

```ebnf
passport_annotation = passport_field , { "," , passport_field } ;
passport_field = ident , "=" , ident ;
```

Examples:

```dlm
let n : Nat[construction=literal, cost=trivial] = 7;
let raw : External<Bytes>[validation=raw, provenance=stdin] = io.read(stdin);
```

## 18. Effects

```ebnf
effect = "Pure" | "Runtime" | "IO" | "ExternalInput" | "Oracle" | "Unsafe" ;
```

Default effect for functions without clause is `Pure`.

## 19. MVP grammar restrictions

MVP intentionally excludes:

- macros;
- user-defined infix operators;
- higher-rank polymorphism;
- full dependent pattern matching;
- user-defined passport transformers;
- implicit bridge search;
- typeclass resolution.
