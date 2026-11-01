//! Source identity and physical diagnostics across parsing, imports and lowering.

use std::collections::BTreeSet;
use std::path::PathBuf;

use dlm_core::{
    link_program, load_program_with_paths, lower_checked_program, lower_program,
    parse_expr, parse_module, parse_module_with_source, verify_ir, CheckPolicy,
    Checker, Expr, ExprKind, FileId, IrExpr, IrInit, Module, ModuleItem, Program,
    SourceFile, SourceSpan, TheoryItem,
};

fn named(source: &str, path: &str) -> Module {
    parse_module_with_source(source, SourceFile::named(FileId::ROOT, path)).unwrap()
}

fn visit(expr: &Expr, f: &mut impl FnMut(&Expr)) {
    f(expr);
    match &expr.kind {
        ExprKind::Power { base: a, exp: b } | ExprKind::Add { lhs: a, rhs: b } | ExprKind::CompareGt { lhs: a, rhs: b } => {
            visit(a, f);
            visit(b, f);
        }
        ExprKind::Call { args, .. } => args.iter().for_each(|a| visit(a, f)),
        ExprKind::Lambda { body, .. } => visit(body, f),
        ExprKind::Let { value, body, .. } => {
            visit(value, f);
            visit(body, f);
        }
        _ => {}
    }
}

fn body(module: &Module) -> &Expr {
    for item in &module.items {
        if let ModuleItem::Theory(theory) = item {
            for item in &theory.items {
                if let TheoryItem::Fn(decl) = item {
                    return &decl.body;
                }
            }
        }
    }
    panic!("no function");
}

fn visit_ir(expr: &IrExpr, f: &mut impl FnMut(&IrExpr)) {
    f(expr);
    dlm_core::compile::for_each_child(expr, &mut |child| visit_ir(child, f));
}

#[test]
fn named_parser_preserves_utf8_crlf_and_generated_node_origins() {
    let source = "module m\r\ntheory T {\r\n    fn f(n: Nat) -> Text =\r\n        match n {\r\n            0 => \"ёж\",\r\n            succ(k) => concat(\"é\", \"x\")\r\n        }\r\n}\r\n";
    let file = SourceFile::named(FileId::new(37), "src/пример.dlm");
    let module = parse_module_with_source(source, file.clone()).unwrap();
    assert_eq!(module.source, file);
    assert_eq!(dlm_core::resolve_module(&module).unwrap().source, file);
    let expr = body(&module);
    visit(expr, &mut |e| assert_eq!(e.span.file_id, file.id));
    let mut literals = Vec::new();
    visit(expr, &mut |e| {
        if matches!(&e.kind, ExprKind::TextLiteral(t) if t == "ёж") {
            literals.push(e.span);
        }
    });
    assert_eq!(literals, [SourceSpan::new(5, 18, 5, 24).with_file(file.id)]);
    assert_eq!(module.clone().source, file);
    assert_eq!(body(&module.clone()).id, expr.id);
}

#[test]
fn parser_errors_use_physical_positions_after_literal_masks_and_continuations() {
    let source = "module m\ntheory T {\n    let x = concat(\n        \"ёж\",\n        @@@)\n}\n";
    let file = SourceFile::named(FileId::new(8), "src/bad.dlm");
    let diagnostics = parse_module_with_source(source, file.clone()).unwrap_err();
    let error = &diagnostics[0];
    assert_eq!(error.span, Some(SourceSpan::new(5, 9, 5, 12).with_file(file.id)));
    assert_eq!(error.line, Some(5));
    assert_eq!(error.source_path.as_deref(), file.path.as_ref());
    assert!(error.to_string().contains("src/bad.dlm:5:9:"));
    // A placeholder can be wider than the original literal; mapping must not
    // be applied twice when the parser error passes through its parents.
    let error = parse_expr("concat(\"é\", @@@)", 11).unwrap_err();
    assert_eq!(error.span, Some(SourceSpan::new(11, 14, 11, 17)));
}

#[test]
fn parser_locations_match_original_bytes_for_varied_text_lengths_and_indentation() {
    for literal in ["", "a", "éёж", "long text with spaces", "a\\\"b\\n\\t"] {
        for indent in [1, 4, 13] {
            for split in [false, true] {
                let token_line = format!("{}\"{literal}\", @@@)", " ".repeat(indent));
                let source = if split {
                    format!("module m\r\ntheory T {{\r\n    let x = concat(\r\n{token_line}\r\n}}\r\n")
                } else {
                    format!("module m\ntheory T {{\n    let x = concat({token_line}\n}}\n")
                };
                let diagnostics = parse_module(&source).unwrap_err();
                let span = diagnostics[0].span.unwrap();
                let (line, text) = source.lines().enumerate().find(|(_, l)| l.contains("@@@")).unwrap();
                let start = text.find("@@@").unwrap() + 1;
                assert_eq!(span, SourceSpan::new(line + 1, start, line + 1, start + 3), "{source}\n{diagnostics:?}");
            }
        }
    }
}

#[test]
fn delayed_structural_parser_errors_keep_the_earlier_header_line() {
    let source = "module m\ntheory T {\n    mutual {\n        fn f() -> Nat = 0\n    }\n}\n";
    let diagnostics = parse_module_with_source(source, SourceFile::named(FileId::new(5), "mutual.dlm")).unwrap_err();
    let error = diagnostics.iter().find(|d| d.message.contains("at least two fn")).unwrap();
    assert_eq!(error.line, Some(3));
    assert_eq!(error.span.unwrap().file_id, FileId::new(5));
    assert!(error.to_string().contains("mutual.dlm:3:"));
}

#[test]
fn literal_lexing_errors_and_following_parses_do_not_reuse_a_logical_context() {
    let source = "module m\ntheory T {\n    let x = concat(\n        \"a\",\n        @@@)\n}\n";
    assert!(parse_module(source).is_err());
    let diagnostics = parse_module_with_source(
        "module m\ntheory T {\n    let s = \"bad\\q\"\n}\n",
        SourceFile::named(FileId::new(3), "next.dlm"),
    ).unwrap_err();
    assert_eq!(diagnostics[0].span, Some(SourceSpan::new(3, 17, 3, 18).with_file(FileId::new(3))));
    assert_eq!(parse_expr("@@@", 2).unwrap_err().span, Some(SourceSpan::new(2, 1, 2, 4)));
}

#[test]
fn linking_assigns_distinct_reproducible_ids_to_independent_parses() {
    let root = named("module app\nimport lib.b\nimport lib.a\ntheory App {\n    fn main() -> Nat = 0\n}\n", "app.dlm");
    let a = named("module lib.a\ntheory A {\n    fn one() -> Nat = 1\n}\n", "lib/a.dlm");
    let b = named("module lib.b\nimport lib.a\ntheory B extends A {\n    fn two() -> Nat = one() + one()\n}\n", "lib/b.dlm");
    let id = body(&a).id;
    let first = link_program(root.clone(), vec![a.clone(), b.clone()]).unwrap();
    let second = link_program(root, vec![b, a]).unwrap();
    let sources = |p: &Program| p.modules.iter().map(|m| m.source.clone()).collect::<Vec<_>>();
    assert_eq!(sources(&first), sources(&second));
    assert_eq!(first.root().source.id, FileId::ROOT);
    assert_eq!(first.modules.iter().map(|m| m.source.id).collect::<BTreeSet<_>>().len(), 3);
    assert_eq!(body(&first.modules[0]).id, id);
    for module in &first.modules {
        visit(body(module), &mut |e| assert_eq!(e.span.file_id, module.source.id));
        assert_eq!(first.source_file(module.source.id), Some(&module.source));
    }
}

#[test]
fn checker_keeps_the_inner_error_location_in_each_imported_module() {
    let root = named("module app\nimport lib.a\nimport lib.b\ntheory App {\n    let v = missing_root\n}\n", "app.dlm");
    let a = named("module lib.a\ntheory A {\n    fn fail() -> Nat =\n        1 + missing_a\n}\n", "lib/a.dlm");
    let b = named("module lib.b\ntheory B {\n    fn fail() -> Nat =\n        2 + missing_b\n}\n", "lib/b.dlm");
    let program = link_program(root, vec![a, b]).unwrap();
    let report = Checker::new().check_program(&program);
    for (needle, path, line, col) in [
        ("missing_a", "lib/a.dlm", 4, 13),
        ("missing_b", "lib/b.dlm", 4, 13),
        ("missing_root", "app.dlm", 5, 13),
    ] {
        let diagnostic = report.diagnostics.iter().find(|d| d.message.contains(needle)).unwrap();
        let span = diagnostic.span.unwrap();
        assert_eq!((span.start_line, span.start_col), (line, col), "{diagnostic:?}");
        assert_eq!(diagnostic.source_path.as_deref(), Some(&PathBuf::from(path)));
        assert_eq!(program.source_file(span.file_id).unwrap().path.as_ref(), diagnostic.source_path.as_deref());
    }
}

#[test]
fn declaration_and_resolution_errors_keep_file_context() {
    let module = named("module m\ntheory T {\n    fn main(n: Nat) -> Nat = n\n}\n", "declarations.dlm");
    let report = Checker::new().check_module(&module);
    let diagnostic = report.diagnostics.iter().find(|d| d.message.contains("main must not take parameters")).unwrap();
    assert_eq!(diagnostic.source_path.as_deref(), module.source.path.as_ref());
    assert!(diagnostic.to_string().contains("declarations.dlm:3:"));
    let module = named("module m\ntheory T {\n}\ntheory T {\n}\n", "duplicate.dlm");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok());
    assert!(report.diagnostics.iter().all(|d| d.source_path.as_deref() == module.source.path.as_ref()));
}

#[test]
fn checked_source_facts_survive_lambdas_match_inheritance_and_initializers() {
    let root = named("module app\nimport lib.base\ntheory App extends Base {\n    fn pick(n: Nat) -> Nat = match n { 0 => 7, succ(k) => unwrap_or(head(map(list(k), fun(x: Nat, x + 1))), 0) }\n    print_decimal(pick(one()))\n}\n", "app.dlm");
    let library = named("module lib.base\ntheory Base {\n    record P(n: Nat)\n    fn one() -> Nat = 1\n}\n", "lib/base.dlm");
    let program = link_program(root, vec![library]).unwrap();
    let report = Checker::new().check_program(&program);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let ir = lower_checked_program(&program, &report.typed).unwrap();
    assert!(verify_ir(&ir).is_empty(), "{:?}", verify_ir(&ir));
    assert_eq!(ir.sources.len(), 2);
    assert_eq!(ir.records[0].span.file_id, program.modules[0].source.id);
    assert_eq!(report.typed.functions["Base.one"].span.file_id, program.modules[0].source.id);
    let mut compared = 0;
    for function in &ir.functions {
        visit_ir(&function.body, &mut |node| {
            assert!(program.source_file(node.span.file_id).is_some());
            if let Some(span) = node.origin.and_then(|id| report.typed.expr_spans.get(&id)) {
                assert_eq!(node.span, *span);
                compared += 1;
            }
        });
    }
    assert!(compared > 8);
    for init in &ir.init {
        let (IrInit::Global { expr, .. } | IrInit::Effect(expr)) = init;
        assert_eq!(expr.span.file_id, FileId::ROOT);
        assert!(expr.origin.is_none());
    }
    assert_eq!(dlm_core::Runtime::new().run_program(&program).unwrap().output, ["1"]);
    assert!(ir.render().contains("source file#1: \"lib/base.dlm\""));
}

#[test]
fn named_fold_callbacks_keep_origins_when_checked_by_a_collection_contract() {
    let source = "module m\ntheory T {\n    fn step(acc: Nat, x: Nat) -> Nat = acc + x\n    fn sum(xs: List<Nat>) -> Nat = fold(xs, 0, step)\n    print_decimal(sum(list(1, 2, 3)))\n}\n";
    let program = Program::single(named(source, "fold.dlm"));
    let report = Checker::new().check_program(&program);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let ir = lower_checked_program(&program, &report.typed).unwrap();
    assert!(verify_ir(&ir).is_empty(), "{:?}", verify_ir(&ir));
    let sum = ir.functions.iter().find(|f| f.name == "T.sum").unwrap();
    let mut callbacks = 0;
    visit_ir(&sum.body, &mut |node| {
        if matches!(node.op, dlm_core::IrOp::FunctionValue(_)) {
            assert_eq!(report.typed.expr_spans.get(&node.origin.unwrap()), Some(&node.span));
            callbacks += 1;
        }
    });
    assert_eq!(callbacks, 1);
    assert_eq!(dlm_core::Runtime::new().run_program(&program).unwrap().output, ["6"]);
}

#[test]
fn lowering_rejects_source_rebinding_after_checking() {
    let mut module = named("module m\ntheory T {\n    fn one() -> Nat = 1\n}\n", "before.dlm");
    let report = Checker::new().check_module(&module);
    assert!(report.ok());
    if let ModuleItem::Theory(theory) = &mut module.items[0] {
        if let TheoryItem::Fn(decl) = &mut theory.items[0] {
            decl.body.span.file_id = FileId::new(99);
        }
    }
    let diagnostics = lower_checked_program(&Program::single(module), &report.typed).unwrap_err();
    assert!(diagnostics.iter().any(|d| d.message.contains("source location differs")));
}

#[test]
fn lowering_reports_the_original_library_file_for_unsupported_literals() {
    let root = named("module app\nimport lib.base\ntheory App extends Base {\n    print_symbolic(huge())\n}\n", "app.dlm");
    let library = named("module lib.base\ntheory Base {\n    fn huge() -> Nat =\n        340282366920938463463374607431768211456\n}\n", "lib/base.dlm");
    let program = link_program(root, vec![library]).unwrap();
    let diagnostics = lower_program(&program, CheckPolicy::research()).unwrap_err();
    let diagnostic = diagnostics.iter().find(|d| d.message.contains("exceeds the compiled Nat range")).unwrap();
    assert_eq!(diagnostic.source_path.as_deref(), Some(&PathBuf::from("lib/base.dlm")));
    assert_eq!((diagnostic.span.unwrap().start_line, diagnostic.span.unwrap().start_col), (4, 9));
}

#[test]
fn ir_verifier_rejects_unknown_files_duplicate_sources_and_invalid_positions() {
    let source = "module m\ntheory T {\n    record P(n: Nat)\n    fn one() -> Nat = 1\n    print_decimal(one())\n}\n";
    let ir = lower_program(&Program::single(named(source, "m.dlm")), CheckPolicy::research()).unwrap();
    let reject = |mutate: &dyn Fn(&mut dlm_core::IrProgram)| {
        let mut bad = ir.clone();
        mutate(&mut bad);
        let issues = verify_ir(&bad);
        assert!(issues.iter().any(|i| i.message.contains("source") || i.message.contains("file identity")), "{issues:?}");
    };
    reject(&|p| p.sources.clear());
    reject(&|p| p.sources.push(p.sources[0].clone()));
    reject(&|p| p.functions[0].body.span.file_id = FileId::new(404));
    reject(&|p| p.functions[0].body.span.end_line = 0);
    reject(&|p| p.functions[0].body.span.end_col = p.functions[0].body.span.start_col);
    reject(&|p| p.functions[0].body.span.start_col += 1);
    reject(&|p| p.functions[0].body.origin = Some(dlm_core::Expr::int("0", 1).id));
    reject(&|p| p.records[0].span.file_id = FileId::new(404));
    reject(&|p| {
        let (IrInit::Global { expr, .. } | IrInit::Effect(expr)) = &mut p.init[0];
        expr.span.file_id = FileId::new(404);
    });
}

#[test]
fn file_loader_preserves_imported_parse_and_semantic_locations() {
    let dir = std::env::temp_dir().join(format!("dlm_source_identity_{}", std::process::id()));
    std::fs::create_dir_all(dir.join("lib")).unwrap();
    let root = dir.join("app.dlm");
    let library = dir.join("lib/base.dlm");
    std::fs::write(&root, "module app\nimport lib.base\ntheory App {\n}\n").unwrap();
    std::fs::write(&library, "module lib.base\ntheory Base {\n    let v = concat(\n        \"a\",\n        @@@)\n}\n").unwrap();
    let diagnostics = load_program_with_paths(&root, &[]).unwrap_err();
    assert_eq!(diagnostics[0].source_path.as_deref(), Some(&library));
    assert_eq!((diagnostics[0].span.unwrap().start_line, diagnostics[0].span.unwrap().start_col), (5, 9));
    std::fs::write(&library, "module lib.base\ntheory Base {\n    fn f() -> Nat =\n        missing\n}\n").unwrap();
    let program = load_program_with_paths(&root, &[]).unwrap();
    let report = Checker::new().check_program(&program);
    let diagnostic = report.diagnostics.iter().find(|d| d.message.contains("missing")).unwrap();
    assert_eq!(diagnostic.source_path.as_deref(), Some(&library));
    assert_eq!((diagnostic.span.unwrap().start_line, diagnostic.span.unwrap().start_col), (4, 9));
    std::fs::remove_dir_all(dir).unwrap();
}
