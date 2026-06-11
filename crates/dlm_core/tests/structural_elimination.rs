use dlm_core::*;

fn nat_pair() -> (ProductTypeReport, ProductTermReport, Passport) {
    let product = product_type("Nat", "Nat", &[], 1).unwrap();
    let lhs = Passport::literal_nat("Logic");
    let rhs = Passport::compressed_nat("Logic");
    let term = product_term(&product, &lhs, &rhs, 1).unwrap();
    let passport = product_term_passport("Logic", &term, &[&lhs, &rhs]);
    (product, term, passport)
}

#[test]
fn product_elimination_is_explicit_and_not_a_proof_or_truth() {
    let (_ty, term, passport) = nat_pair();
    let elim = product_elimination(&term, &passport, 1).unwrap();
    assert_eq!(elim.product_type, "Nat*Nat");
    assert_eq!(elim.lhs, "Nat");
    assert_eq!(elim.rhs, "Nat");

    let elim_passport = product_elimination_passport("Logic", &elim, &[&passport]);
    assert!(matches!(elim_passport.ty, TypeKind::ProductElimination { .. }));
    assert!(!matches!(elim_passport.ty, TypeKind::Theorem { .. } | TypeKind::StaticProof(_) | TypeKind::TruthClaim { .. }));
    assert!(elim_passport.history.contains_event("structural:product_elimination"));
}

#[test]
fn sum_elimination_requires_both_branches_to_have_same_result_type() {
    let sum = sum_type("Nat", "Text", &[], 1).unwrap();
    let nat = Passport::literal_nat("Logic");
    let injection = sum_injection(&sum, SumInjectionSide::Left, &nat, 1).unwrap();
    let injection_passport = sum_injection_passport("Logic", &injection, &[&nat]);

    let left_result = Passport::literal_nat("Logic");
    let right_result = Passport::compressed_nat("Logic");
    let elim = sum_elimination(&injection, &injection_passport, &left_result, &right_result, 1).unwrap();
    assert_eq!(elim.result_type, "Nat");
    assert_eq!(elim.selected_side, SumInjectionSide::Left);

    let bad_right = Passport { ty: TypeKind::Text, ..Passport::literal_nat("Logic") };
    assert!(sum_elimination(&injection, &injection_passport, &left_result, &bad_right, 1).is_err());
}

#[test]
fn record_pattern_binds_existing_fields_only_and_preserves_order() {
    let ty = record_type(
        "Point",
        vec![
            record_field_decl("x", "Nat", 1).unwrap(),
            record_field_decl("y", "Nat", 1).unwrap(),
        ],
        &[],
        1,
    ).unwrap();
    let x = Passport::literal_nat("Logic");
    let y = Passport::compressed_nat("Logic");
    let term = record_term(
        &ty,
        vec![record_field_value("x", &x, 1).unwrap(), record_field_value("y", &y, 1).unwrap()],
        &[&x, &y],
        1,
    ).unwrap();
    let term_passport = record_term_passport("Logic", &term, &[&x, &y]);

    let pattern = record_pattern(&term, &term_passport, &["x", "y"], 1).unwrap();
    assert_eq!(pattern.fields.len(), 2);
    assert_eq!(pattern.fields[0].field, "x");
    assert_eq!(pattern.fields[1].field, "y");
    assert!(record_pattern(&term, &term_passport, &["x", "x"], 1).is_err());
    assert!(record_pattern(&term, &term_passport, &["z"], 1).is_err());

    let reversed = record_pattern(&term, &term_passport, &["y", "x"], 1).unwrap();
    assert_ne!(pattern.fingerprint, reversed.fingerprint);
}

#[test]
fn eliminations_reject_proof_truth_theorem_and_runtime_subjects_or_results() {
    let (_ty, term, passport) = nat_pair();
    let proof_term = Passport::proof_term("Logic", "intro", None);
    let proof = Passport::static_proof("Logic", "P", &proof_term);
    assert!(product_elimination(&term, &proof, 1).is_err());

    let sum = sum_type("Nat", "Nat", &[], 1).unwrap();
    let nat = Passport::literal_nat("Logic");
    let injection = sum_injection(&sum, SumInjectionSide::Left, &nat, 1).unwrap();
    let injection_passport = sum_injection_passport("Logic", &injection, &[&nat]);
    assert!(sum_elimination(&injection, &injection_passport, &proof, &nat, 1).is_err());

    let provable = Passport::provable_claim("Logic", "Logic", "P", &proof);
    let truth = Passport::axiom_truth_from_provable("Logic", "P", &provable);
    assert!(sum_elimination(&injection, &injection_passport, &truth, &nat, 1).is_err());

    let statement = statement_passport("Logic", "P");
    let theorem = theorem_from_static_proof("Logic", "p", &statement, &proof, 1).unwrap();
    assert!(sum_elimination(&injection, &injection_passport, &theorem, &nat, 1).is_err());

    let runtime = Passport::runtime_nat_from_input("Logic");
    let witness = Passport::runtime_witness("Logic", "P", &runtime);
    assert!(record_pattern(&RecordTermReport { name: "R".to_string(), fields: vec![], max_trust: TrustLevel::Checked, max_provenance: Provenance::InternalDerived, has_axiom_taint: false, has_oracle_taint: false, has_unsafe_taint: false, fingerprint: "x".to_string() }, &witness, &["x"], 1).is_err());

    assert!(product_elimination(&term, &passport, 1).is_ok());
}

#[test]
fn elimination_reports_preserve_axiom_oracle_and_unsafe_taint() {
    let product = product_type("Nat", "Nat", &[], 1).unwrap();
    let axiom = Passport::axiom_nat("Logic");
    let unsafe_nat = Passport::unsafe_nat("Logic");
    let term = product_term(&product, &axiom, &unsafe_nat, 1).unwrap();
    let term_passport = product_term_passport("Logic", &term, &[&axiom, &unsafe_nat]);
    let elim = product_elimination(&term, &term_passport, 1).unwrap();
    assert!(elim.has_axiom_taint);
    assert!(elim.has_unsafe_taint);
    assert!(elim.max_trust >= TrustLevel::Unsafe);

    let passport = product_elimination_passport("Logic", &elim, &[&term_passport]);
    assert!(passport.trust >= TrustLevel::Unsafe);
}

#[test]
fn elimination_exports_are_stable_and_order_sensitive() {
    let (_ty, term, passport) = nat_pair();
    let elim = product_elimination(&term, &passport, 1).unwrap();
    let text = export_product_elimination(&elim);
    assert!(text.contains("product_elimination_report: v1"));
    assert!(text.contains(&elim.fingerprint));

    let ty = record_type(
        "Pair",
        vec![record_field_decl("a", "Nat", 1).unwrap(), record_field_decl("b", "Nat", 1).unwrap()],
        &[],
        1,
    ).unwrap();
    let a = Passport::literal_nat("Logic");
    let b = Passport::compressed_nat("Logic");
    let term = record_term(
        &ty,
        vec![record_field_value("a", &a, 1).unwrap(), record_field_value("b", &b, 1).unwrap()],
        &[&a, &b],
        1,
    ).unwrap();
    let term_passport = record_term_passport("Logic", &term, &[&a, &b]);
    let ab = record_pattern(&term, &term_passport, &["a", "b"], 1).unwrap();
    let ba = record_pattern(&term, &term_passport, &["b", "a"], 1).unwrap();
    assert_ne!(ab.fingerprint, ba.fingerprint);
    assert!(export_record_pattern(&ab).contains("record_pattern_report: v1"));
}
