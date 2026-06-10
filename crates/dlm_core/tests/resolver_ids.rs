use dlm_core::{
    parse_module, resolve_module, BridgeKind, DiagnosticKind, IdAllocator, ModuleId, TheoryId,
};

#[test]
fn id_allocator_uses_separate_monotonic_spaces() {
    let mut ids = IdAllocator::new();

    assert_eq!(ids.alloc_module(), ModuleId::new(0));
    assert_eq!(ids.alloc_module(), ModuleId::new(1));
    assert_eq!(ids.alloc_theory(), TheoryId::new(0));
    assert_eq!(ids.alloc_theory(), TheoryId::new(1));
    assert_eq!(ids.alloc_value().raw(), 0);
    assert_eq!(ids.alloc_value().raw(), 1);
}

#[test]
fn resolver_assigns_ids_to_theories_values_and_bridges() {
    let source = r#"
module examples.resolver

theory A {
    let x = 1
}

theory B {
    let y = A.x
}

bridge A_to_B : A -> B {
    kind = transport
}
"#;

    let module = parse_module(source).expect("module parses");
    let resolved = resolve_module(&module).expect("module resolves");

    let a = resolved.symbols.theory_id("A").expect("A theory id");
    let b = resolved.symbols.theory_id("B").expect("B theory id");

    assert_ne!(a, b);
    assert_eq!(resolved.symbols.theory_count(), 2);
    assert!(resolved.symbols.value_id(a, "x").is_some());
    assert!(resolved.symbols.value_id(b, "y").is_some());
    assert!(resolved.symbols.bridge_id("A_to_B").is_some());
    assert_eq!(resolved.bridges.len(), 1);
    assert_eq!(resolved.bridges[0].source, a);
    assert_eq!(resolved.bridges[0].target, b);
    assert_eq!(resolved.bridges[0].kind, BridgeKind::Transport);
}

#[test]
fn resolver_rejects_duplicate_values_inside_theory() {
    let source = r#"
module examples.duplicate_value

theory A {
    let x = 1
    let x = 2
}
"#;

    let module = parse_module(source).expect("module parses");
    let errors = resolve_module(&module).expect_err("duplicate value must fail resolution");

    assert_eq!(errors.len(), 1);
    assert_eq!(errors[0].kind, DiagnosticKind::NameError);
    assert_eq!(errors[0].line, Some(6));
    assert!(errors[0].message.contains("duplicate value 'x'"));
}

#[test]
fn resolver_rejects_bridge_to_unknown_theory() {
    let source = r#"
module examples.bad_bridge

theory A {
    let x = 1
}

bridge A_to_B : A -> B {
    kind = transport
}
"#;

    let module = parse_module(source).expect("module parses");
    let errors = resolve_module(&module).expect_err("unknown target theory must fail resolution");

    assert_eq!(errors.len(), 1);
    assert_eq!(errors[0].kind, DiagnosticKind::NameError);
    assert!(errors[0].message.contains("unknown target theory 'B'"));
}
