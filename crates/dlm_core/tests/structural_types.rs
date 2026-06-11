use dlm_core::*;

#[test]
fn product_types_and_terms_are_structural_not_proofs_or_truth() {
    let product = product_type("Nat", "Nat", &[], 1).unwrap();
    let product_passport = product_type_passport("Logic", &product, &[]);
    assert!(matches!(product_passport.ty, TypeKind::ProductType { .. }));
    assert!(!matches!(product_passport.ty, TypeKind::Theorem { .. } | TypeKind::StaticProof(_) | TypeKind::TruthClaim { .. }));

    let lhs = Passport::literal_nat("Logic");
    let rhs = Passport::compressed_nat("Logic");
    let term = product_term(&product, &lhs, &rhs, 1).unwrap();
    assert_eq!(term.product_type, "Nat*Nat");
    let term_passport = product_term_passport("Logic", &term, &[&lhs, &rhs]);
    assert!(matches!(term_passport.ty, TypeKind::ProductTerm { .. }));
    assert!(term_passport.history.contains_event("structural:product_term"));

    let bad = product_type("Nat", "Text", &[], 1).unwrap();
    assert!(product_term(&bad, &lhs, &rhs, 1).is_err());
}

#[test]
fn sum_types_require_selected_side_type() {
    let sum = sum_type("Nat", "Text", &[], 1).unwrap();
    let sum_passport = sum_type_passport("Logic", &sum, &[]);
    assert!(matches!(sum_passport.ty, TypeKind::SumType { .. }));

    let nat = Passport::literal_nat("Logic");
    let left = sum_injection(&sum, SumInjectionSide::Left, &nat, 1).unwrap();
    assert_eq!(left.side, SumInjectionSide::Left);
    assert_eq!(left.value_type, "Nat");
    let left_passport = sum_injection_passport("Logic", &left, &[&nat]);
    assert!(matches!(left_passport.ty, TypeKind::SumInjection { .. }));

    assert!(sum_injection(&sum, SumInjectionSide::Right, &nat, 1).is_err());
}

#[test]
fn record_types_reject_duplicate_fields_and_record_terms_require_exact_fields() {
    let x = record_field_decl("x", "Nat", 1).unwrap();
    let y = record_field_decl("y", "Nat", 1).unwrap();
    let duplicate = record_field_decl("x", "Text", 1).unwrap();
    assert!(record_type("Point", vec![x.clone(), duplicate], &[], 1).is_err());

    let ty = record_type("Point", vec![x, y], &[], 1).unwrap();
    let record_ty_passport = record_type_passport("Logic", &ty, &[]);
    assert!(matches!(record_ty_passport.ty, TypeKind::RecordType { .. }));

    let nx = Passport::literal_nat("Logic");
    let ny = Passport::compressed_nat("Logic");
    let vx = record_field_value("x", &nx, 1).unwrap();
    let vy = record_field_value("y", &ny, 1).unwrap();
    let term = record_term(&ty, vec![vx.clone(), vy], &[&nx, &ny], 1).unwrap();
    assert_eq!(term.name, "Point");
    assert_eq!(term.fields.len(), 2);

    let missing = record_term(&ty, vec![vx], &[&nx], 1);
    assert!(missing.is_err());
}

#[test]
fn record_projection_is_explicit_and_field_checked() {
    let ty = record_type(
        "Point",
        vec![
            record_field_decl("x", "Nat", 1).unwrap(),
            record_field_decl("y", "Nat", 1).unwrap(),
        ],
        &[],
        1,
    )
    .unwrap();
    let x = Passport::literal_nat("Logic");
    let y = Passport::compressed_nat("Logic");
    let term = record_term(
        &ty,
        vec![
            record_field_value("x", &x, 1).unwrap(),
            record_field_value("y", &y, 1).unwrap(),
        ],
        &[&x, &y],
        1,
    )
    .unwrap();
    let projection = record_projection(&term, "x", 1).unwrap();
    assert_eq!(projection.result_type, "Nat");
    assert_eq!(projection.field, "x");
    assert!(record_projection(&term, "z", 1).is_err());

    let projection_passport = record_projection_passport("Logic", &projection, &[&x]);
    assert!(matches!(projection_passport.ty, TypeKind::RecordProjection { .. }));
    assert!(projection_passport.history.contains_event("structural:record_projection"));
}

#[test]
fn proof_truth_theorem_and_runtime_objects_are_not_structural_values() {
    let product = product_type("Nat", "Nat", &[], 1).unwrap();
    let nat = Passport::literal_nat("Logic");

    let proof_term = Passport::proof_term("Logic", "intro", None);
    let proof = Passport::static_proof("Logic", "P", &proof_term);
    assert!(product_term(&product, &proof, &nat, 1).is_err());

    let provable = Passport::provable_claim("Logic", "Logic", "P", &proof);
    let truth = Passport::axiom_truth_from_provable("Logic", "P", &provable);
    assert!(product_term(&product, &truth, &nat, 1).is_err());

    let statement = statement_passport("Logic", "P");
    let theorem_passport = theorem_from_static_proof("Logic", "p", &statement, &proof, 1).unwrap();
    assert!(product_term(&product, &theorem_passport, &nat, 1).is_err());

    let runtime = Passport::runtime_nat_from_input("Logic");
    let witness = Passport::runtime_witness("Logic", "P", &runtime);
    assert!(record_field_value("w", &witness, 1).is_err());
}

#[test]
fn structural_objects_preserve_axiom_oracle_and_unsafe_taint() {
    let axiom = Passport::axiom_nat("Logic");
    let unsafe_nat = Passport::unsafe_nat("Logic");
    let product = product_type("Nat", "Nat", &[&axiom], 1).unwrap();
    let term = product_term(&product, &axiom, &unsafe_nat, 1).unwrap();
    assert!(term.has_axiom_taint);
    assert!(term.has_unsafe_taint);
    assert!(term.max_trust >= TrustLevel::Unsafe);

    let passport = product_term_passport("Logic", &term, &[&axiom, &unsafe_nat]);
    assert!(passport.trust >= TrustLevel::Unsafe);
    assert!(passport.history.contains_event("structural:product_term"));
}

#[test]
fn structural_exports_are_stable_and_order_sensitive() {
    let first = record_type(
        "Pair",
        vec![
            record_field_decl("a", "Nat", 1).unwrap(),
            record_field_decl("b", "Text", 1).unwrap(),
        ],
        &[],
        1,
    )
    .unwrap();
    let first_again = record_type(
        "Pair",
        vec![
            record_field_decl("a", "Nat", 1).unwrap(),
            record_field_decl("b", "Text", 1).unwrap(),
        ],
        &[],
        1,
    )
    .unwrap();
    let second = record_type(
        "Pair",
        vec![
            record_field_decl("b", "Text", 1).unwrap(),
            record_field_decl("a", "Nat", 1).unwrap(),
        ],
        &[],
        1,
    )
    .unwrap();
    assert_eq!(first.fingerprint, first_again.fingerprint);
    assert_ne!(first.fingerprint, second.fingerprint);

    let exported = export_record_type(&first);
    assert!(exported.contains("record_type_report: v1"));
    assert!(exported.contains("fields: a:Nat,b:Text"));
}
