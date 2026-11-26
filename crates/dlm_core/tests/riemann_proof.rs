use std::io::{self, Read};

use dlm_core::audit::{audit_check_report, AuditStatus, TrustedComponent};
use dlm_core::math::riemann;
use dlm_core::program::{link_program, Program};
use dlm_core::{
    audit_certificate_against_theorem, export_certificate_text, parse_module, Capability,
    CheckPolicy, Checker, DiagnosticKind, ProofCertificateStatus, Provenance, Runtime, TrustLevel,
    TypeKind, ValidationState,
};

const STD_RIEMANN: &str = include_str!("../../../lib/std/riemann.dlm");

#[test]
fn certificate_keeps_the_exact_statement_and_its_external_trusted_base() {
    let module = parse_module(STD_RIEMANN).unwrap();
    let report = Checker::new().check_module(&module);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let [(name, passport)] = report.inferred.as_slice() else {
        panic!("expected one theorem")
    };
    let [certificate] = report.certificates.as_slice() else {
        panic!("expected one certificate")
    };
    assert_eq!(name, "StdRiemann.riemann_hypothesis");
    assert_eq!(
        passport.ty,
        TypeKind::Theorem {
            name: "riemann_hypothesis".into(),
            proposition: riemann::PROPOSITION.into(),
        }
    );
    assert_eq!(passport.trust, TrustLevel::Axiom);
    assert_eq!(passport.provenance, Provenance::ExternalFile);
    assert_eq!(passport.validation, ValidationState::ConstraintChecked);
    assert!(!passport
        .capabilities
        .contains(Capability::CanProofKernelCheck));
    assert_eq!(
        certificate.status,
        ProofCertificateStatus::ExternallyVerified
    );
    assert!(certificate.kernel.is_none());

    let audit = audit_certificate_against_theorem(certificate, passport, 1);
    assert!(audit.verified());
    assert!(!audit.independently_checked);
    let text = export_certificate_text(certificate, 1).unwrap();
    assert!(text.contains("proof_evidence: external-lean"));
    assert!(text.contains(riemann::SOLUTION_SHA256));
    assert!(text.contains(riemann::CHALLENGE_SHA256));

    let audit = audit_check_report(&report, vec![]);
    let [statement] = audit.statements.as_slice() else {
        panic!("expected one audit statement")
    };
    assert_eq!(audit.status, AuditStatus::AxiomDependent);
    assert!(statement
        .components
        .contains(&TrustedComponent::ExternalLean));
    assert!(!statement.unattributed_taint);
    assert_eq!(
        statement.axioms,
        riemann::AXIOMS.map(|name| format!("Lean.{name}"))
    );
}

#[test]
fn modified_evidence_cannot_be_resigned_as_a_different_proof() {
    let report = Checker::new().check_module(&parse_module(STD_RIEMANN).unwrap());
    let certificate = &report.certificates[0];
    let passport = &report.inferred[0].1;

    let mut changed = certificate.clone();
    changed.trace.pop();
    changed.trace_len = changed.trace.len();
    changed.fingerprint = changed.recomputed_fingerprint();
    assert!(export_certificate_text(&changed, 1).is_err());
    assert!(!audit_certificate_against_theorem(&changed, passport, 1).verified());

    let mut downgraded = certificate.clone();
    downgraded.status = ProofCertificateStatus::Checked;
    downgraded.fingerprint = downgraded.recomputed_fingerprint();
    assert!(export_certificate_text(&downgraded, 1).is_err());
    downgraded.trace.clear();
    downgraded.trace_len = 0;
    downgraded.fingerprint = downgraded.recomputed_fingerprint();
    assert!(!audit_certificate_against_theorem(&downgraded, passport, 1).verified());

    let mut unrelated = passport.clone();
    unrelated.ty = TypeKind::Theorem {
        name: certificate.theorem_name.clone(),
        proposition: "false".into(),
    };
    let mut rebound = certificate.clone();
    rebound.proposition = "false".into();
    rebound.fingerprint = rebound.recomputed_fingerprint();
    assert!(!audit_certificate_against_theorem(&rebound, &unrelated, 1).verified());
}

#[test]
fn declaration_rejects_rebinding_and_erases_from_compiled_code() {
    let module = parse_module(STD_RIEMANN).unwrap();
    let strict = Checker::with_policy(CheckPolicy::trusted_only()).check_module(&module);
    assert!(strict
        .diagnostics
        .iter()
        .any(|d| d.kind == DiagnosticKind::TrustTaintError));
    assert!(strict.certificates.is_empty());

    let unknown =
        parse_module("module m\ntheory T {\n theorem t from Lean.LFunction_eq_zero_re\n}\n")
            .unwrap();
    assert!(Checker::new()
        .check_module(&unknown)
        .diagnostics
        .iter()
        .any(|d| d.kind == DiagnosticKind::StatementTheoremError
            && d.message.contains("unknown external theorem"),));
    assert!(parse_module(
        "module m\ntheory T {\n theorem t from Lean.riemannHypothesis: false\n}\n"
    )
    .is_err());

    let ir = dlm_core::compile::lower_program(&Program::single(module), CheckPolicy::research())
        .unwrap()
        .render();
    assert!(ir.contains("erased statements: 1"));
    assert!(!ir.contains(riemann::SOURCE));
    assert!(!ir.contains("riemannZeta"));
}

#[test]
fn standard_library_import_keeps_trust_and_runtime_display() {
    let root = parse_module("module app\nimport std.riemann\ntheory T extends StdRiemann {\n let fact = riemann_hypothesis\n print_symbolic(fact)\n}\n").unwrap();
    let program = link_program(root, vec![parse_module(STD_RIEMANN).unwrap()]).unwrap();
    let report = Checker::new().check_program(&program);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let (_, fact) = report
        .inferred
        .iter()
        .find(|(name, _)| name == "T.fact")
        .unwrap();
    assert_eq!(fact.trust, TrustLevel::Axiom);
    assert!(fact
        .history
        .events()
        .iter()
        .any(|event| event.contains(riemann::SOLUTION_SHA256)));
    let audit = audit_check_report(&report, vec!["std.riemann".into()]);
    assert!(audit.components.contains(&TrustedComponent::ExternalLean));
    let output = Runtime::new().run_program(&program).unwrap().output;
    assert_eq!(
        output,
        [format!("ExternalTheorem<{}>", riemann::PROPOSITION)]
    );
}

#[test]
fn export_hash_handles_padding_and_short_reads() {
    use dlm_core::spectral::{sha256, sha256_reader};

    struct ShortReads<'a>(&'a [u8]);
    impl Read for ShortReads<'_> {
        fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
            let size = buf.len().min(13);
            self.0.read(&mut buf[..size])
        }
    }

    let mut data = vec![b'a'; 1_000_000];
    let expected = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
    assert_eq!(sha256_reader(ShortReads(&data)).unwrap(), expected);
    assert_eq!(sha256(&data), expected);
    for len in [65, 64, 63, 56, 55, 1, 0] {
        data.truncate(len);
        assert_eq!(sha256_reader(ShortReads(&data)).unwrap(), sha256(&data));
    }
}
