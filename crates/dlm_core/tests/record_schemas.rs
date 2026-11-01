use dlm_core::{
    lower_program, parse_module, verify_ir, CheckPolicy, IrOp, OrdinaryType, Program, RecordId,
};

fn lower(source: &str) -> dlm_core::IrProgram {
    lower_program(
        &Program::single(parse_module(source).unwrap()),
        CheckPolicy::research(),
    )
    .unwrap()
}

#[test]
fn nested_and_inherited_records_retain_checked_schemas_and_nominal_ids() {
    let source = "module records\ntheory A {\n    record Point(x: Nat, label: Text)\n    record Batch(points: List<Point>, fallback: Option<Point>)\n    fn first(b: Batch) -> Point = unwrap_or(head(field(b, points)), unwrap_or(field(b, fallback), Point(0, \"empty\")))\n}\ntheory B extends A {\n    fn demo() -> Text = field(first(Batch(list(Point(3, \"three\")), none(Point))), label)\n    print_line(demo())\n}\n";
    let program = Program::single(parse_module(source).unwrap());
    let report = dlm_core::Checker::new().check_program(&program);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(
        report.typed.records["A.Point"].fields[0].1,
        OrdinaryType::Nat
    );
    let ir = lower_program(&program, CheckPolicy::research()).unwrap();
    assert!(verify_ir(&ir).is_empty());
    assert_eq!(ir.records.len(), 2);
    assert_eq!(ir.records[0].symbol, "records::A.Point");
    assert_eq!(
        ir.records[1].fields[0].1,
        OrdinaryType::List(Box::new(OrdinaryType::RecordRef(RecordId::new(0))))
    );
    assert!(ir.render().starts_with("DLM-IR v3\n"));
    let runtime = dlm_core::Runtime::new().run_program(&program).unwrap();
    assert_eq!(runtime.output, ["three"]);
}

#[test]
fn schema_corruption_cannot_reach_the_backend() {
    let ir = lower("module m\ntheory T {\n    record P(n: Nat, t: Text)\n    fn make() -> P = P(1, \"x\")\n    fn get(p: P) -> Nat = field(p, n)\n}\n");
    let reject = |mutate: &dyn Fn(&mut dlm_core::IrProgram)| {
        let mut bad = ir.clone();
        mutate(&mut bad);
        assert!(
            !verify_ir(&bad).is_empty(),
            "accepted damaged record schema"
        );
    };
    reject(&|p| {
        if let IrOp::Record { fields, .. } = &mut p.functions[0].body.op {
            fields.pop();
        }
    });
    reject(&|p| {
        if let IrOp::Record { fields, .. } = &mut p.functions[0].body.op {
            fields.swap(0, 1);
        }
    });
    reject(&|p| {
        if let IrOp::Record { fields, .. } = &mut p.functions[0].body.op {
            fields[0].1.op = IrOp::Text("bad".into());
            fields[0].1.ty = Some(OrdinaryType::Text);
        }
    });
    reject(&|p| {
        if let IrOp::Field { field, .. } = &mut p.functions[1].body.op {
            *field = 999;
        }
    });
    reject(&|p| {
        if let IrOp::Field { name, .. } = &mut p.functions[1].body.op {
            *name = "unknown".into();
        }
    });
    reject(&|p| {
        p.functions[1].body.ty = Some(OrdinaryType::Text);
        p.functions[1].ret = Some(OrdinaryType::Text);
    });
    reject(&|p| {
        p.records[0].fields[0].1 = OrdinaryType::RecordRef(RecordId::new(0));
    });
    reject(&|p| {
        p.records[0].fields[0].1 = OrdinaryType::Record("Unresolved".into());
    });
    reject(&|p| {
        p.records[0].fields[0].1 = OrdinaryType::RecordRef(RecordId::new(u32::MAX));
    });
    reject(&|p| {
        p.records[0].fields[1].0 = p.records[0].fields[0].0.clone();
    });
    reject(&|p| {
        p.records.clear();
    });
}

#[test]
fn equal_display_names_in_different_theories_have_distinct_identities() {
    let ir = lower("module names\ntheory A {\n    record P(n: Nat)\n    fn make() -> P = P(1)\n}\ntheory B {\n    record P(n: Nat)\n    fn make() -> P = P(2)\n    fn take(p: P) -> Nat = field(p, n)\n}\n");
    assert_ne!(ir.functions[0].ret, ir.functions[1].ret);
    assert_eq!(ir.records[0].name, ir.records[1].name);
    assert_ne!(ir.records[0].symbol, ir.records[1].symbol);
    let mut bad = ir.clone();
    if let IrOp::Record { record, .. } = &mut bad.functions[1].body.op {
        *record = RecordId::new(0);
    }
    assert!(!verify_ir(&bad).is_empty());
    let mut bad = ir;
    if let IrOp::Field { layout, .. } = &mut bad.functions[2].body.op {
        *layout = RecordId::new(0);
    }
    assert!(!verify_ir(&bad).is_empty());
}
