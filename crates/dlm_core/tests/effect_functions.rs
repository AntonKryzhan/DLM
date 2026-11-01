//! Explicit callable bounds must agree at the source, IR and executable boundaries.
#[path = "support/effect_support.rs"]
mod support;
use dlm_core::{lower_program, verify_ir, CheckPolicy, IrOp, OrdinaryType};
use support::*;

#[test]
fn effectful_closures_are_first_class_and_capture_lexically() {
    let p = program(
        r#"
    record Handler(run: FnEffect<Stdout, Nat, Nat>)
    fn make(prefix: Text) -> FnEffect<Stdout, Nat, Nat> =
        effect_fn(FnEffect(Stdout, Nat, Nat), fun(n: Nat, let log = print_line(concat(prefix, nat_to_text(n))) in n + 1))
    fn invoke<F: UnaryCallable>(action: F, n: ArgOf<F>) -> ResultOf<F> = action(n)
    let first = make("value=")
    let fs = list(first, make("other="))
    let h = Handler(unwrap_or(index(fs, 1), first))
    let action = field(h, run)
    print_decimal(invoke(first, 3) + action(4))
    let factory = fun(prefix: Text, make(prefix))
    let generated = factory("last=")
    print_decimal(generated(6))
    "#,
    );
    assert_eq!(
        run_both(&p, &scratch("first-class")),
        ["value=3", "other=4", "9", "last=6", "7"]
    );
}

#[test]
fn file_effects_and_zero_argument_callbacks_work() {
    let dir = scratch("file-callback");
    let file = dir.join("input.txt");
    std::fs::write(&file, "content").unwrap();
    let p = program(&format!(
        r#"
    fn reader(path: Text) -> Text = let f = file_open(path, "r") in let text = file_read_all(f) in let closed = file_close(f) in text
    fn load() -> Text = reader("{}")
    fn invoke(action: FnEffect<File, Text, Text>, path: Text) -> Text = action(path)
    fn invoke0(action: FnEffect<File, Text>) -> Text = action()
    let reader_value = effect_fn(FnEffect(File, Text, Text), reader)
    let thunk = effect_fn(FnEffect(File, Text), load)
    print_line(invoke(reader_value, "{}"))
    print_line(invoke0(thunk))
    "#,
        file.display(),
        file.display()
    ));
    assert_eq!(run_both(&p, &dir), ["content", "content"]);
}

#[test]
fn bounds_can_widen_and_partiality_is_explicit() {
    let p = program(
        r#"
    fn countdown(n: Nat) -> Nat loop = if_then_else(nat_eq(n, 0), 0, countdown(pred(n)))
    let small = effect_fn(FnEffect(Partial, Nat, Nat), countdown)
    let wider = effect_fn(FnEffect(Stdout + Partial, Nat, Nat), small)
    print_decimal(wider(10000))
    let pure = effect_fn(FnEffect(Pure, Nat, Nat), fun(n: Nat, n + 1))
    print_decimal(fold(map(list(1, 2), pure), 1, fun(a: Nat, n: Nat, a + n)))
    "#,
    );
    assert_eq!(run_both(&p, &scratch("widening")), ["0", "6"]);
}

#[test]
fn case_propagates_executed_branch_effects() {
    let p = program(
        r#"
    fn left(n: Nat) -> Nat = let log = print_line("left") in n + 1
    fn right(t: Text) -> Nat = let log = print_line(t) in text_length(t)
    print_decimal(case(inl(7, Text), left, right))
    let a = effect_fn(FnEffect(Stdout, Nat, Nat), left)
    let b = effect_fn(FnEffect(Stdout, Text, Nat), right)
    print_decimal(case(inl(6, Text), a, b))
    "#,
    );
    assert_eq!(run_both(&p, &scratch("case")), ["left", "8", "left", "7"]);
}

#[test]
fn effect_inference_respects_creation_aliases_and_shadowing() {
    let p = program(
        r#"
    fn action(n: Nat) -> Nat = n
    fn build() -> FnEffect<Stdout, Nat, Nat> = effect_fn(FnEffect(Stdout, Nat, Nat), fun(n: Nat, let log = print_decimal(n) in n))
    fn execute(action: FnEffect<Stdout, Nat, Nat>) -> Nat = action(1)
    fn local() -> Nat = let action = build() in action(2)
    fn innocent(n:Nat) -> Bool = let unused = build() in true
    theorem construction_is_pure: innocent(1)
    print_decimal(execute(build()) + local())
    "#,
    );
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    for f in &ir.functions {
        if f.name == "App.build" {
            assert!(f.effects.is_empty());
        }
        if f.name == "App.execute" || f.name == "App.local" {
            assert_eq!(f.effects, ["io@stdout"]);
        }
    }
    assert_eq!(run_both(&p, &scratch("inference")), ["1", "2", "3"]);
}

#[test]
fn invalid_bounds_proofs_recursion_and_owned_captures_are_rejected() {
    for body in [
        "let f = fun(n: Nat, print_decimal(n))",
        "let f = effect_fn(FnEffect(Pure, Nat, Text), fun(n: Nat, print_decimal(n)))",
        "fn spin(n: Nat) -> Nat loop = spin(n)\nlet f = effect_fn(FnEffect(Pure,Nat,Nat),spin)",
        "let f = effect_fn(FnEffect(Stdout,Nat,Text),fun(n:Nat,print_decimal(n)))\nlet g = effect_fn(FnEffect(Pure,Nat,Text),f)",
        "let f = effect_fn(FnEffect(Oracle,Nat,Nat),fun(n:Nat,n))",
        "let f = effect_fn(FnEffect(Stdout+Stdout,Nat,Nat),fun(n:Nat,n))",
        "let f = effect_fn(FnEffect(Pure+File,Nat,Nat),fun(n:Nat,n))",
        "fn invoke(f: FnEffect<File,File,Nat>) -> Nat = 0",
        "record R(next: FnEffect<Pure,Option<R>,Nat>)",
        "fn capture(b: ByteBuf) -> FnEffect<ByteBuffer,Nat,Nat> = effect_fn(FnEffect(ByteBuffer,Nat,Nat),fun(n:Nat,buf_len(b)))",
        "let f = effect_fn(FnEffect(Stdout,Nat,Text),fun(n:Nat,print_decimal(n)))\nlet xs = map(list(1),f)",
        "theorem invalid: let f=effect_fn(FnEffect(Stdout,Nat,Bool),fun(n:Nat,let log=print_decimal(n) in true)) in f(1)",
        "fn spin(n:Nat)->Bool loop=spin(n)\nfn bad()->Bool=let f=effect_fn(FnEffect(Partial,Nat,Bool),spin) in f(0)\ntheorem invalid: bad()",
        "fn test(f: FnEffect<Stdout,Nat,Bool>) -> Bool = f(1)\nlet f = effect_fn(FnEffect(Stdout,Nat,Bool),fun(n:Nat,let log=print_decimal(n) in true))\ntheorem invalid: test(f)",
    ] { rejects(body); }
}

#[test]
fn ir_cannot_erase_callback_effects_or_narrow_a_conversion() {
    let p = program(
        r#"
    fn invoke(f: FnEffect<Stdout,Nat,Text>) -> Text = f(3)
    let f = effect_fn(FnEffect(Stdout,Nat,Text),fun(n:Nat,print_decimal(n)))
    print_line(invoke(f))
    "#,
    );
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let mut changed = ir.clone();
    changed
        .functions
        .iter_mut()
        .find(|f| f.name == "App.invoke")
        .unwrap()
        .effects
        .clear();
    assert!(!verify_ir(&changed).is_empty());
    let mut changed = ir.clone();
    let f = changed
        .functions
        .iter_mut()
        .find(|f| matches!(&f.body.op,IrOp::Builtin{name,..} if name=="effect_fn"))
        .unwrap();
    f.body.ty = Some(dlm_core::ordinary::parse_ordinary_type("FnEffect<Pure,Nat,Text>").unwrap());
    assert!(!verify_ir(&changed).is_empty());

    let partial = program(
        "fn spin(n:Nat)->Nat loop=spin(n)\nlet f=effect_fn(FnEffect(Partial,Nat,Nat),spin)",
    );
    let mut changed = lower_program(&partial, CheckPolicy::trusted_only()).unwrap();
    let init = changed
        .functions
        .iter_mut()
        .find(|f| matches!(&f.body.op,IrOp::Builtin{name,..} if name=="effect_fn"))
        .unwrap();
    if let IrOp::Builtin { args, .. } = &mut init.body.op {
        args[0].ty = Some(OrdinaryType::Fn(
            vec![OrdinaryType::Nat],
            Box::new(OrdinaryType::Nat),
        ));
    }
    assert!(verify_ir(&changed)
        .iter()
        .any(|i| i.message.contains("partial")));
}

#[test]
fn callback_local_resources_are_released_and_metadata_is_not_captured() {
    let p = program(
        r#"
    let size = effect_fn(FnEffect(ByteBuffer+Partial,Text,Nat),fun(t:Text,let b=buf_from_text(t) in let n=buf_len(b) in let freed=buf_free(b) in n))
    print_decimal(size("abc"))
    fn factory(Stdout:ByteBuf) -> FnEffect<Stdout,Nat,Nat> = effect_fn(FnEffect(Stdout,Nat,Nat),fun(n:Nat,let log=print_line("meta") in n))
    let f = factory(buf_new(1,0))
    print_decimal(f(4))
    "#,
    );
    assert_eq!(
        run_both(&p, &scratch("local-resources")),
        ["3", "meta", "4"]
    );
}

#[test]
fn traversals_cannot_discard_owned_files_or_hide_partiality_from_proofs() {
    rejects(
        "fn opener(path:Text)->File=file_open(path,\"r\")\nlet n=each_effect(list(\"a\"),opener)",
    );
    rejects("fn spin(n:Nat)->Bool loop=spin(n)\nlet f=effect_fn(FnEffect(Partial,Nat,Bool),spin)\ntheorem invalid:f(0)");
    let p = program("let f=effect_fn(FnEffect(Pure,Nat,Nat),fun(n:Nat,n))");
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    assert!(ir
        .global_types
        .iter()
        .any(|t| matches!(t, Some(OrdinaryType::EffectFn(..)))));
}
