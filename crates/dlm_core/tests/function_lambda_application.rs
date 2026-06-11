use dlm_core::{
    application_term, application_term_passport, bound_variable, export_application_term,
    export_function_type, export_lambda_term, function_type, function_type_passport, lambda_term,
    lambda_term_passport, ApplicationStatus, Passport, TrustLevel, TypeKind,
};

#[test]
fn function_type_and_lambda_are_not_theorem_proof_or_truth() {
    let fty = function_type("Nat", "Nat", true, true, &[], 1).unwrap();
    let fty_pass = function_type_passport("Core", &fty, &[]);
    assert!(matches!(fty_pass.ty, TypeKind::FunctionType { .. }));
    assert!(!matches!(fty_pass.ty, TypeKind::Theorem { .. }));
    assert!(!matches!(fty_pass.ty, TypeKind::StaticProof(_)));
    assert!(!matches!(fty_pass.ty, TypeKind::TruthClaim { .. }));

    let x = bound_variable("x", "Nat", 1).unwrap();
    let lam = lambda_term(x, "succ(x)", &fty, vec![], &[&fty_pass], 1).unwrap();
    let lam_pass = lambda_term_passport("Core", &lam, &[&fty_pass]);
    assert!(matches!(lam_pass.ty, TypeKind::LambdaTerm { .. }));
    assert!(!matches!(lam_pass.ty, TypeKind::Theorem { .. }));
    assert!(!matches!(lam_pass.ty, TypeKind::StaticProof(_)));
}

#[test]
fn application_checks_argument_domain_without_producing_proof() {
    let fty = function_type("Nat", "Nat", true, true, &[], 1).unwrap();
    let fty_pass = function_type_passport("Core", &fty, &[]);
    let nat = Passport::literal_nat("Core");

    let app = application_term(&fty_pass, &nat, "1", 1).unwrap();
    assert_eq!(app.status, ApplicationStatus::Applied);
    assert_eq!(app.expected_domain, "Nat");
    assert_eq!(app.argument_domain, "Nat");
    assert!(app.result.contains(": Nat"));

    let app_pass = application_term_passport("Core", &app, &[&fty_pass, &nat]);
    assert!(matches!(app_pass.ty, TypeKind::ApplicationTerm { .. }));
    assert!(!matches!(app_pass.ty, TypeKind::StaticProof(_)));
}

#[test]
fn application_reports_domain_mismatch_as_rejected_status() {
    let fty = function_type("Bool", "Nat", true, true, &[], 1).unwrap();
    let fty_pass = function_type_passport("Core", &fty, &[]);
    let nat = Passport::literal_nat("Core");

    let app = application_term(&fty_pass, &nat, "1", 1).unwrap();
    assert_eq!(app.status, ApplicationStatus::RejectedDomainMismatch);
    assert_eq!(app.expected_domain, "Bool");
    assert_eq!(app.argument_domain, "Nat");
}

#[test]
fn proof_theorem_truth_and_runtime_objects_are_not_ordinary_functions_or_arguments() {
    let proof_term = Passport::proof_term("Meta", "true_intro", None);
    let static_proof = Passport::static_proof("Meta", "P", &proof_term);
    let nat = Passport::literal_nat("Core");

    let as_function = application_term(&static_proof, &nat, "1", 1);
    assert!(as_function.is_err());

    let fty = function_type("Nat", "Nat", true, true, &[], 1).unwrap();
    let fty_pass = function_type_passport("Core", &fty, &[]);
    let as_argument = application_term(&fty_pass, &static_proof, "proof", 1);
    assert!(as_argument.is_err());

    let runtime_witness = Passport::runtime_witness("Meta", "P", &nat);
    let runtime_as_argument = application_term(&fty_pass, &runtime_witness, "runtime", 1);
    assert!(runtime_as_argument.is_err());
}

#[test]
fn lambda_rejects_parameter_domain_mismatch_and_shadowed_capture() {
    let fty = function_type("Nat", "Nat", true, true, &[], 1).unwrap();
    let b = bound_variable("b", "Bool", 1).unwrap();
    let mismatch = lambda_term(b, "b", &fty, vec![], &[], 1);
    assert!(mismatch.is_err());

    let x = bound_variable("x", "Nat", 1).unwrap();
    let shadowed = lambda_term(x, "x", &fty, vec!["x".to_string()], &[], 1);
    assert!(shadowed.is_err());
}

#[test]
fn function_objects_preserve_axiom_taint_from_sources() {
    let axiom_source = Passport::axiom_nat("Core");
    let fty = function_type("Nat", "Nat", true, true, &[&axiom_source], 1).unwrap();
    assert!(fty.has_axiom_taint);
    assert_eq!(fty.max_trust, TrustLevel::Axiom);

    let fty_pass = function_type_passport("Core", &fty, &[&axiom_source]);
    assert_eq!(fty_pass.trust, TrustLevel::Axiom);

    let x = bound_variable("x", "Nat", 1).unwrap();
    let lam = lambda_term(x, "x", &fty, vec![], &[&fty_pass], 1).unwrap();
    assert!(lam.has_axiom_taint);
    let lam_pass = lambda_term_passport("Core", &lam, &[&fty_pass]);
    assert_eq!(lam_pass.trust, TrustLevel::Axiom);
}

#[test]
fn function_exports_are_stable_and_order_sensitive() {
    let f1 = function_type("Nat", "Bool", true, true, &[], 1).unwrap();
    let f2 = function_type("Bool", "Nat", true, true, &[], 1).unwrap();
    assert_ne!(f1.fingerprint, f2.fingerprint);

    let f1_again = function_type("Nat", "Bool", true, true, &[], 1).unwrap();
    assert_eq!(f1.fingerprint, f1_again.fingerprint);

    let x = bound_variable("x", "Nat", 1).unwrap();
    let lam = lambda_term(x, "is_zero(x)", &f1, vec![], &[], 1).unwrap();
    let exported_type = export_function_type(&f1);
    let exported_lambda = export_lambda_term(&lam);
    assert!(exported_type.contains("function_type_report: v1"));
    assert!(exported_lambda.contains("lambda_term_report: v1"));

    let f1_pass = function_type_passport("Core", &f1, &[]);
    let nat = Passport::literal_nat("Core");
    let app = application_term(&f1_pass, &nat, "0", 1).unwrap();
    let exported_app = export_application_term(&app);
    assert!(exported_app.contains("application_term_report: v1"));
    assert!(exported_app.contains("status: applied"));
}
