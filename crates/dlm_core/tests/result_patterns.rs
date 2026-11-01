use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, lower_program, parse_module, verify_ir,
    CheckPolicy, Checker, IrOp, OrdinaryType, Program, Runtime,
};
use std::process::Command;
fn program(source: &str) -> Program {
    Program::single(parse_module(source).unwrap())
}
#[test]
fn result_patterns_bind_correct_types_and_evaluate_scrutinee_once_in_both_backends() {
    let p = program(
        r#"module result.patterns
theory T {
    fn describe(r: Result<Nat, Text>) -> Text = match r { ok(n) => nat_to_text(n), err(e) => e }
    fn nested(r: Result<Option<Nat>, Text>) -> Nat = match r { err(e) => text_length(e), ok(o) => match o { none => 7, some(n) => n } }
    fn observed() -> Result<Nat, Text> = let printed = print_line("once") in ok(4, Text)
    fn extract(r: Result<Nat, Text>) -> Nat = match r { ok(n) => n, _ => 99 }
    print_line(describe(ok(42, Text)))
    print_line(describe(err("ошибка", Nat)))
    print_decimal(nested(ok(some(9), Text)))
    print_decimal(nested(ok(none(Nat), Text)))
    print_decimal(nested(err("bad", Option(Nat))))
    print_decimal(match observed() { ok(n) => n, err(_) => 0 })
    print_decimal(extract(err("ignored", Nat)))
}
"#,
    );
    let report = Checker::new().check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let expected = vec!["42", "ошибка", "9", "7", "3", "once", "4", "99"];
    assert_eq!(Runtime::new().run_program(&p).unwrap().output, expected);
    let ir = lower_program(&p, CheckPolicy::research()).unwrap();
    assert!(verify_ir(&ir).is_empty());
    let dir = std::env::temp_dir().join(format!("dlm-result-patterns-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let exe = build_c_executable(
        &emit_c(&ir),
        &dir.join("test.c"),
        &dir.join("test"),
        &default_c_compiler(),
    )
    .unwrap();
    let out = Command::new(exe).output().unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    assert_eq!(
        String::from_utf8(out.stdout).unwrap().trim_end(),
        expected.join("\n")
    );
    std::fs::remove_dir_all(dir).unwrap();
}
#[test]
fn result_match_requires_exhaustive_disjoint_arms_and_valid_payload_types() {
    for body in [
        "match ok(1, Text) { ok(n) => n }",
        "match ok(1, Text) { ok(n) => n, err(e) => 0, _ => 1 }",
        "match ok(1, Text) { ok(n) => n, ok(m) => m, err(e) => 0 }",
        "match ok(1, Text) { ok(n) => n, none => 0 }",
        "match ok(1, Text) { _ => 0, err(e) => 1 }",
        "__match_ok(ok(1, Text))",
        "match ok(1, Text) { ok(__hidden) => 1, err(e) => 0 }",
    ] {
        let text = format!("module m\ntheory T {{\n    let value = {body}\n}}\n");
        assert!(parse_module(&text).is_err(), "{body}");
    }
    for body in [
        "match true { ok(n) => n, err(e) => 0 }",
        "match ok(1, Text) { ok(n) => concat(n, \"x\"), err(e) => e }",
        "match err(\"bad\", Nat) { ok(n) => n, err(e) => e }",
        "match ok(1, Text) { ok(n) => n, err(e) => n }",
    ] {
        let p = program(&format!(
            "module m\ntheory T {{\n    let value = {body}\n}}\n"
        ));
        assert!(!Checker::new().check_program(&p).ok(), "{body}");
    }
}
#[test]
fn ir_result_projection_types_are_verified_independently() {
    let p=program("module m\ntheory T {\n fn get(r: Result<Nat, Text>) -> Nat = match r { ok(n) => n, err(e) => text_length(e) }\n}\n");
    let ir = lower_program(&p, CheckPolicy::research()).unwrap();
    let fun = ir.functions.iter().position(|f| f.name == "T.get").unwrap();
    // Desugaring of identifier scrutinee: if is_ok(r), let n = __match_ok(r), ...
    let mut bad = ir.clone();
    if let IrOp::If(_, yes, _) = &mut bad.functions[fun].body.op {
        if let IrOp::Let { value, .. } = &mut yes.op {
            if let IrOp::Builtin { name, .. } = &mut value.op {
                assert_eq!(name, "__match_ok");
                *name = "__match_err".into();
            } else {
                panic!("projection");
            }
        } else {
            panic!("binder");
        }
    } else {
        panic!("branch");
    }
    assert!(!verify_ir(&bad).is_empty());
    let mut bad = ir;
    if let IrOp::If(_, yes, _) = &mut bad.functions[fun].body.op {
        if let IrOp::Let { value, .. } = &mut yes.op {
            if let IrOp::Builtin { args, .. } = &mut value.op {
                args[0].ty = Some(OrdinaryType::Bool);
            }
        }
    }
    assert!(!verify_ir(&bad).is_empty());
}

#[test]
fn ir_projections_require_the_correct_dominating_variant_even_for_identical_payload_types() {
    let p=program("module m\ntheory T {\n fn pick(r: Result<Nat, Nat>, q: Result<Nat, Nat>) -> Nat = match r { ok(n) => n, err(e) => e }\n}\n");
    let ir = lower_program(&p, CheckPolicy::research()).unwrap();
    let id = ir
        .functions
        .iter()
        .position(|f| f.name == "T.pick")
        .unwrap();
    let mut wrong_branch = ir.clone();
    if let IrOp::If(_, yes, no) = &mut wrong_branch.functions[id].body.op {
        std::mem::swap(yes, no);
    } else {
        panic!("branch");
    }
    assert!(verify_ir(&wrong_branch)
        .iter()
        .any(|e| e.message.contains("dominating")));
    let mut no_guard = ir.clone();
    if let IrOp::If(cond, _, _) = &mut no_guard.functions[id].body.op {
        cond.op = IrOp::Bool(true);
    }
    assert!(verify_ir(&no_guard)
        .iter()
        .any(|e| e.message.contains("dominating")));
    let mut wrong_subject = ir.clone();
    if let IrOp::If(_, yes, _) = &mut wrong_subject.functions[id].body.op {
        if let IrOp::Let { value, .. } = &mut yes.op {
            if let IrOp::Builtin { args, .. } = &mut value.op {
                args[0].op = IrOp::Local(1);
            }
        }
    }
    assert!(verify_ir(&wrong_subject)
        .iter()
        .any(|e| e.message.contains("dominating")));
    let mut hoisted = ir;
    let projection = if let IrOp::If(_, yes, _) = &hoisted.functions[id].body.op {
        if let IrOp::Let { value, .. } = &yes.op {
            *value.clone()
        } else {
            panic!("binder");
        }
    } else {
        panic!("branch");
    };
    hoisted.functions[id].body = projection;
    assert!(verify_ir(&hoisted)
        .iter()
        .any(|e| e.message.contains("dominating")));
}

#[test]
fn option_and_list_projections_also_require_dominance_and_facts_do_not_leak() {
    for (ty, arms) in [
        ("Option<Nat>", "none => 0, some(n) => n"),
        ("List<Nat>", "[] => 0, cons(h, t) => h"),
    ] {
        let p = program(&format!(
            "module m\ntheory T {{\n fn pick(x: {ty}) -> Nat = match x {{ {arms} }}\n}}\n"
        ));
        let mut ir = lower_program(&p, CheckPolicy::research()).unwrap();
        let id = ir
            .functions
            .iter()
            .position(|f| f.name == "T.pick")
            .unwrap();
        if let IrOp::If(_, yes, no) = &mut ir.functions[id].body.op {
            std::mem::swap(yes, no);
        }
        assert!(
            verify_ir(&ir)
                .iter()
                .any(|e| e.message.contains("dominating")),
            "{ty}"
        );
    }
    let p=program("module m\ntheory T {\n fn pick(r: Result<Nat, Nat>) -> Nat = let first = match r { ok(n) => n, err(e) => e } in match r { ok(n) => n, err(e) => e }\n}\n");
    let mut ir = lower_program(&p, CheckPolicy::research()).unwrap();
    let id = ir
        .functions
        .iter()
        .position(|f| f.name == "T.pick")
        .unwrap();
    if let IrOp::Let { body, .. } = &mut ir.functions[id].body.op {
        if let IrOp::If(cond, _, _) = &mut body.op {
            cond.op = IrOp::Bool(true);
        } else {
            panic!("second match");
        }
    } else {
        panic!("let");
    }
    assert!(verify_ir(&ir)
        .iter()
        .any(|e| e.message.contains("dominating")));
}
