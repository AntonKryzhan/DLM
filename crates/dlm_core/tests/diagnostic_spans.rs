use dlm_core::{parse_expr, parse_module, Diagnostic, DiagnosticKind, SourceSpan};

#[test]
fn diagnostic_can_carry_precise_source_span_without_losing_line_api() {
    let span = SourceSpan::line_col(7, 12, 4);
    let diagnostic = Diagnostic::error_at(DiagnosticKind::ParseError, span, "bad token");

    assert_eq!(diagnostic.line, Some(7));
    assert_eq!(diagnostic.span, Some(span));
    assert_eq!(format!("{diagnostic}"), "error[E0001 ParseError] at line 7, column 12: bad token\n");
}

#[test]
fn line_only_diagnostics_keep_existing_human_format() {
    let diagnostic = Diagnostic::error(DiagnosticKind::NameError, Some(3), "unknown name");

    assert_eq!(diagnostic.line, Some(3));
    assert_eq!(diagnostic.span, Some(SourceSpan::line(3)));
    assert_eq!(format!("{diagnostic}"), "error[E0002 NameError] at line 3: unknown name\n");
}

#[test]
fn parser_reports_expression_columns_for_parse_errors() {
    let err = parse_expr("  @@@", 9).expect_err("invalid token must fail");

    assert_eq!(err.line, Some(9));
    assert_eq!(err.span, Some(SourceSpan::line_col(9, 3, 3)));
    assert!(format!("{err}").contains("line 9, column 3"));
}

#[test]
fn module_parser_preserves_span_on_let_expression_error() {
    let source = r#"
module demo

theory Core {
    let n =
}
"#;

    let diagnostics = parse_module(source).expect_err("missing expression must fail");

    assert_eq!(diagnostics.len(), 1);
    assert_eq!(diagnostics[0].line, Some(5));
    assert_eq!(diagnostics[0].span, Some(SourceSpan::line_col(5, 12, 1)));
}
