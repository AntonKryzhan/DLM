//! Checked declaration identity at the frontend/lowering boundary.

use std::collections::BTreeSet;

use dlm_core::{
    link_program, lower_checked_program, parse_module, parse_module_with_source,
    resolve_module, verify_ir, CheckReport, Checker, DeclId, ExprId, ExprKind,
    DiagnosticKind, FileId, FnDecl, FnMeasure, FnParam, ModuleItem, OrdinaryType, Program,
    RecordId, SourceDeclarationKind, SourceFile, TheoryDecl, TheoryItem, TypedFacts,
};

const SOURCE: &str = "module ids\ntheory T {\n    record P(n: Nat)\n    record Box(p: P)\n    fn one() -> Nat = 1\n    fn make(n: Nat) -> P = P(n)\n    fn get(p: P) -> Nat = field(p, n)\n    print_decimal(get(make(7)))\n}\n";

fn checked(source: &str) -> (Program, CheckReport) {
    let module = parse_module_with_source(source, SourceFile::named(FileId::ROOT, "ids.dlm")).unwrap();
    let program = Program::single(module);
    let report = Checker::new().check_program(&program);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert!(report.typed.is_checked());
    (program, report)
}

fn theory_mut<'a>(program: &'a mut Program, name: &str) -> &'a mut TheoryDecl {
    program.modules.iter_mut().flat_map(|m| &mut m.items).find_map(|item| match item {
        ModuleItem::Theory(t) if t.name == name => Some(t),
        _ => None,
    }).unwrap()
}

fn function_mut<'a>(program: &'a mut Program, name: &str) -> &'a mut FnDecl {
    theory_mut(program, "T").items.iter_mut().find_map(|item| match item {
        TheoryItem::Fn(f) if f.name == name => Some(f),
        _ => None,
    }).unwrap()
}

fn function_id(program: &Program, name: &str) -> DeclId {
    program.modules.iter().flat_map(|m| &m.items).find_map(|item| match item {
        ModuleItem::Theory(t) => t.items.iter().find_map(|item| match item {
            TheoryItem::Fn(f) if f.name == name => Some(f.id),
            _ => None,
        }),
        _ => None,
    }).unwrap()
}

fn record_id(program: &Program, name: &str) -> DeclId {
    program.modules.iter().flat_map(|m| &m.items).find_map(|item| match item {
        ModuleItem::Theory(t) => t.items.iter().find_map(|item| match item {
            TheoryItem::Record(r) if r.name == name => Some(r.id),
            _ => None,
        }),
        _ => None,
    }).unwrap()
}

#[test]
fn source_ids_survive_clones_and_are_not_name_or_parse_position_keys() {
    let (program, _) = checked(SOURCE);
    let (reparsed, _) = checked(SOURCE);
    let copy = program.clone();
    let ids: BTreeSet<_> = ["one", "make", "get"].map(|n| function_id(&program, n)).into_iter()
        .chain([record_id(&program, "P"), record_id(&program, "Box")]).collect();
    assert_eq!(ids.len(), 5);
    assert_eq!(function_id(&copy, "one"), function_id(&program, "one"));
    assert_ne!(function_id(&reparsed, "one"), function_id(&program, "one"));
    assert_ne!(record_id(&reparsed, "P"), record_id(&program, "P"));
    let resolved = resolve_module(program.root()).unwrap();
    let t = &resolved.theories[0];
    assert_eq!(t.values.iter().find(|v| v.name == "one").unwrap().declaration, Some(function_id(&program, "one")));
    assert_eq!(t.values.iter().find(|v| v.name == "P").unwrap().declaration, Some(record_id(&program, "P")));
}

#[test]
fn concurrent_parsers_allocate_disjoint_declaration_identities() {
    let threads: Vec<_> = (0..4).map(|_| std::thread::spawn(|| {
        (0..24).flat_map(|_| {
            let module = parse_module("module m\ntheory T {\n    record P(n: Nat)\n    fn f() -> Nat = 1\n}\n").unwrap();
            let ModuleItem::Theory(t) = &module.items[0] else { panic!() };
            t.items.iter().map(|i| match i {
                TheoryItem::Fn(f) => f.id,
                TheoryItem::Record(r) => r.id,
                _ => panic!(),
            }).collect::<Vec<_>>()
        }).collect::<Vec<_>>()
    })).collect();
    let ids: Vec<_> = threads.into_iter().flat_map(|t| t.join().unwrap()).collect();
    assert_eq!(ids.iter().copied().collect::<BTreeSet<_>>().len(), 4 * 24 * 2);
}

#[test]
fn checker_resolves_nested_nominal_types_before_lowering() {
    let (program, report) = checked(SOURCE);
    let p = &report.typed.record_declarations[&record_id(&program, "P")];
    let container = &report.typed.record_declarations[&record_id(&program, "Box")];
    let make = &report.typed.function_declarations[&function_id(&program, "make")];
    assert_eq!(container.facts.fields[0].1, OrdinaryType::RecordRef(p.record_id));
    assert_eq!(make.facts.ret, OrdinaryType::RecordRef(p.record_id));
    // The old public view retains source names for display and consumers.
    assert_eq!(report.typed.functions["T.make"].ret, OrdinaryType::Record("P".into()));
    let ir = lower_checked_program(&program, &report.typed).unwrap();
    assert!(verify_ir(&ir).is_empty());
    let f = ir.functions.iter().find(|f| f.name == "T.make").unwrap();
    assert_eq!(f.declaration, Some(function_id(&program, "make")));
    assert_eq!(f.ret, Some(make.facts.ret.clone()));
    assert_eq!(ir.records[p.record_id.raw() as usize].declaration, p.declaration.id);
    assert!(ir.render().contains("declaration decl#"));
}

#[test]
fn mutable_legacy_name_views_do_not_supply_backend_facts() {
    let (program, mut report) = checked(SOURCE);
    report.typed.functions.clear();
    report.typed.records.clear();
    report.typed.expr_types.clear();
    let ir = lower_checked_program(&program, &report.typed).unwrap();
    assert!(verify_ir(&ir).is_empty(), "{:?}", verify_ir(&ir));
    assert_eq!(ir.functions.iter().find(|f| f.name == "T.make").unwrap().ret, Some(OrdinaryType::RecordRef(RecordId::new(0))));
}

#[test]
fn missing_canonical_declarations_never_fall_back_to_surface_signatures() {
    let (program, report) = checked(SOURCE);
    let mut missing_fn = report.typed.clone();
    missing_fn.function_declarations.remove(&function_id(&program, "make"));
    assert!(lower_checked_program(&program, &missing_fn).unwrap_err()[0].message.contains("missing checked function declaration ID"));
    let mut missing_record = report.typed.clone();
    missing_record.record_declarations.remove(&record_id(&program, "P"));
    assert!(lower_checked_program(&program, &missing_record).unwrap_err()[0].message.contains("missing checked record declaration ID"));
}

#[test]
fn facts_from_an_identical_reparse_are_not_silently_reused() {
    let (program, _) = checked(SOURCE);
    let (_, report) = checked(SOURCE);
    assert!(lower_checked_program(&program, &report.typed).unwrap_err()[0].message.contains("declaration ID"));
}

#[test]
fn swapping_same_signature_facts_cannot_change_declaration_identity() {
    let source = "module ids\ntheory T {\n    fn a() -> Nat = 1\n    fn b() -> Nat = 2\n}\n";
    let (program, mut report) = checked(source);
    let a = function_id(&program, "a");
    let b = function_id(&program, "b");
    let first = report.typed.function_declarations[&a].clone();
    let second = report.typed.function_declarations[&b].clone();
    report.typed.function_declarations.insert(a, second);
    report.typed.function_declarations.insert(b, first);
    let diagnostics = lower_checked_program(&program, &report.typed).unwrap_err();
    assert!(diagnostics.iter().all(|d| d.kind == DiagnosticKind::CompileError));
}

#[test]
fn independent_function_reordering_preserves_id_bound_contracts() {
    let (mut program, report) = checked("module ids\ntheory T {\n    fn a(n: Nat) -> Nat = n + 1\n    fn b(t: Text) -> Text = concat(t, \"!\")\n}\n");
    theory_mut(&mut program, "T").items.swap(0, 1);
    let ir = lower_checked_program(&program, &report.typed).unwrap();
    assert!(verify_ir(&ir).is_empty());
    assert_eq!(ir.functions[0].name, "T.b");
    assert_eq!(ir.functions[0].param_types, [OrdinaryType::Text]);
    assert_eq!(ir.functions[0].declaration, Some(function_id(&program, "b")));
    assert_eq!(ir.functions[1].param_types, [OrdinaryType::Nat]);
}

#[test]
fn changing_checked_function_or_record_snapshots_requires_rechecking() {
    let (program, report) = checked(SOURCE);
    let reject = |mutate: &dyn Fn(&mut Program)| {
        let mut changed = program.clone();
        mutate(&mut changed);
        assert!(lower_checked_program(&changed, &report.typed).is_err());
    };
    reject(&|p| function_mut(p, "one").name = "renamed".into());
    reject(&|p| function_mut(p, "one").ret = "Text".into());
    reject(&|p| function_mut(p, "one").body.kind = ExprKind::IntLiteral("2".into()));
    reject(&|p| function_mut(p, "one").body.id = ExprId::fresh());
    reject(&|p| function_mut(p, "make").params[0].name = "renamed".into());
    reject(&|p| function_mut(p, "make").params[0].ty = "Int".into());
    reject(&|p| function_mut(p, "make").measure = Some(FnMeasure { params: vec!["n".into()], fuel: Some(3) }));
    reject(&|p| {
        let TheoryItem::Record(r) = &mut theory_mut(p, "T").items[0] else { panic!() };
        r.fields.push(FnParam { name: "extra".into(), ty: "Nat".into() });
    });
}

#[test]
fn checked_module_inheritance_and_visibility_contexts_cannot_be_rebound() {
    let source = "module ids\ntheory A {\n    fn value() -> Nat = 1\n}\ntheory B {\n    fn value() -> Nat = 2\n}\ntheory T extends A {\n    fn main() -> Nat = value()\n}\n";
    let (program, report) = checked(source);
    let reject = |mutate: &dyn Fn(&mut Program)| {
        let mut changed = program.clone();
        mutate(&mut changed);
        assert!(lower_checked_program(&changed, &report.typed).unwrap_err()[0].message.contains("context differs"));
    };
    reject(&|p| theory_mut(p, "T").extends = Some("B".into()));
    reject(&|p| { theory_mut(p, "T").private.insert("main".into()); });
    reject(&|p| p.modules[0].name = "another_module".into());
    reject(&|p| p.modules[0].source.path = Some("another_source.dlm".into()));
}

#[test]
fn duplicate_declaration_ids_and_wrong_nominal_schema_order_are_rejected() {
    let (program, report) = checked(SOURCE);
    let mut changed = program.clone();
    let id = function_id(&changed, "one");
    function_mut(&mut changed, "make").id = id;
    assert!(!Checker::new().check_program(&changed).ok());
    assert!(lower_checked_program(&changed, &report.typed).is_err());
    let mut wrong_schema = report.typed.clone();
    wrong_schema.record_declarations.get_mut(&record_id(&program, "P")).unwrap().record_id = RecordId::new(999);
    assert!(lower_checked_program(&program, &wrong_schema).is_err());
}

#[test]
fn unfinished_failed_and_conflicting_facts_are_not_checked_programs() {
    let (program, report) = checked(SOURCE);
    assert!(lower_checked_program(&program, &TypedFacts::default()).unwrap_err()[0].message.contains("unvalidated"));
    let failed = Program::single(parse_module("module bad\ntheory Bad {\n    fn f() -> Nat = unknown\n}\n").unwrap());
    let failed_report = Checker::new().check_program(&failed);
    assert!(!failed_report.ok() && !failed_report.typed.is_checked());
    assert!(lower_checked_program(&failed, &failed_report.typed).is_err());
    let mut conflicting = report.typed.clone();
    conflicting.conflicts.insert(ExprId::fresh());
    assert!(lower_checked_program(&program, &conflicting).is_err());
}

#[test]
fn inheritance_callbacks_and_lambdas_retain_original_nominal_declarations() {
    let base = parse_module_with_source("module lib.base\ntheory Base {\n    record P(n: Nat)\n    private fn helper(n: Nat) -> Nat = n + 1\n    fn make(n: Nat) -> P = P(helper(n))\n    fn step(acc: P, n: Nat) -> P = P(field(acc, n) + n)\n}\n", SourceFile::named(FileId::ROOT, "lib/base.dlm")).unwrap();
    let root = parse_module_with_source("module app\nimport lib.base\ntheory App extends Base {\n    fn run() -> Nat = let p = fold(list(1, 2, 3), P(0), step) in field(unwrap_or(head(map(list(p), fun(x: P, make(field(x, n))))), P(0)), n)\n    print_decimal(run())\n}\n", SourceFile::named(FileId::ROOT, "app.dlm")).unwrap();
    let program = link_program(root, vec![base]).unwrap();
    let report = Checker::new().check_program(&program);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let ir = lower_checked_program(&program, &report.typed).unwrap();
    assert!(verify_ir(&ir).is_empty(), "{:?}", verify_ir(&ir));
    let id = record_id(&program, "P");
    assert_eq!(ir.records.len(), 1);
    assert_eq!(ir.records[0].declaration, id);
    assert_eq!(ir.declarations[&id].symbol(), "lib.base::Base.P");
    assert!(ir.functions.iter().filter(|f| f.is_lambda).all(|f| f.declaration.is_none()));
    assert_eq!(dlm_core::Runtime::new().run_program(&program).unwrap().output, ["7"]);
}

#[test]
fn verifier_rejects_damaged_declaration_provenance_without_panicking() {
    let (program, report) = checked(SOURCE);
    let ir = lower_checked_program(&program, &report.typed).unwrap();
    let one = ir.functions.iter().position(|f| f.name == "T.one").unwrap();
    let make = ir.functions.iter().position(|f| f.name == "T.make").unwrap();
    let id = function_id(&program, "one");
    let reject = |mutate: &dyn Fn(&mut dlm_core::IrProgram)| {
        let mut bad = ir.clone();
        mutate(&mut bad);
        let issues = verify_ir(&bad);
        assert!(issues.iter().any(|i| i.message.contains("declaration")), "{issues:?}");
    };
    reject(&|p| p.declarations.clear());
    reject(&|p| p.functions[one].declaration = None);
    reject(&|p| p.functions[one].declaration = Some(DeclId::fresh()));
    reject(&|p| p.functions[make].declaration = p.functions[one].declaration);
    reject(&|p| p.records[0].declaration = id);
    reject(&|p| p.declarations.get_mut(&id).unwrap().kind = SourceDeclarationKind::Record);
    reject(&|p| p.declarations.get_mut(&id).unwrap().theory = "Wrong".into());
    reject(&|p| p.declarations.get_mut(&id).unwrap().span.file_id = FileId::new(404));
    reject(&|p| p.functions[one].is_lambda = true);
    reject(&|p| { p.functions.remove(one); });
}
