use crate::ast::*;
use crate::diagnostics::{Diagnostic, DiagnosticKind};

/// v0.31 guard: reflection and self-reference are not ordinary object-level
/// computations. A theory may use non-diagonal reflection only when it is
/// connected to an explicit reflection bridge. Diagonal/self-truth forms are
/// rejected even behind such a bridge.
pub fn check_reflection_guard(module: &Module) -> Vec<Diagnostic> {
    let bridges: Vec<&BridgeDecl> = module
        .items
        .iter()
        .filter_map(|item| match item {
            ModuleItem::Bridge(bridge) => Some(bridge),
            _ => None,
        })
        .collect();

    let mut diagnostics = Vec::new();

    for item in &module.items {
        let ModuleItem::Theory(theory) = item else {
            continue;
        };

        let has_reflection_bridge = has_reflection_bridge(&bridges, &theory.name);

        for theory_item in &theory.items {
            match theory_item {
                TheoryItem::Let(let_decl) => {
                    if expr_mentions_binding(&let_decl.expr, &let_decl.name) {
                        diagnostics.push(reflection_error(
                            let_decl.line,
                            format!(
                                "self-reference guard rejected `{}`: a value may not mention its own binding without a reflective bridge and a later proof-kernel check",
                                let_decl.name
                            ),
                        ));
                    }

                    check_expr(
                        &let_decl.expr,
                        &theory.name,
                        Some(&let_decl.name),
                        has_reflection_bridge,
                        &mut diagnostics,
                    );
                }
                TheoryItem::Expr(expr) => {
                    check_expr(
                        expr,
                        &theory.name,
                        None,
                        has_reflection_bridge,
                        &mut diagnostics,
                    );
                }
            }
        }
    }

    diagnostics
}

fn has_reflection_bridge(bridges: &[&BridgeDecl], theory: &str) -> bool {
    bridges.iter().any(|bridge| {
        bridge.kind == BridgeKind::Reflection && (bridge.source == theory || bridge.target == theory)
    })
}

fn check_expr(
    expr: &Expr,
    theory: &str,
    binding: Option<&str>,
    has_reflection_bridge: bool,
    diagnostics: &mut Vec<Diagnostic>,
) {
    match &expr.kind {
        ExprKind::Call { name, args } => {
            if is_diagonal_self_reference_call(name) {
                diagnostics.push(reflection_error(
                    expr.line,
                    format!(
                        "self-reference guard rejected `{}` in theory `{}`: diagonal truth/provability forms require an explicit reflective bridge plus a proof-kernel rule, not an object-level builtin",
                        name, theory
                    ),
                ));
            } else if is_reflection_call(name) && !has_reflection_bridge {
                diagnostics.push(reflection_error(
                    expr.line,
                    format!(
                        "reflection guard rejected `{}` in theory `{}`: reflection requires an explicit `reflection` bridge",
                        name, theory
                    ),
                ));
            }

            if is_truth_or_provability_call(name) && args.iter().any(expr_is_self_marker) {
                diagnostics.push(reflection_error(
                    expr.line,
                    format!(
                        "self-reference guard rejected `{}` in theory `{}`: object-level truth/provability may not be applied to the current formula",
                        name, theory
                    ),
                ));
            }

            for arg in args {
                check_expr(arg, theory, binding, has_reflection_bridge, diagnostics);
            }
        }
        ExprKind::Add { lhs, rhs } | ExprKind::CompareGt { lhs, rhs } => {
            check_expr(lhs, theory, binding, has_reflection_bridge, diagnostics);
            check_expr(rhs, theory, binding, has_reflection_bridge, diagnostics);
        }
        ExprKind::Power { base, exp } => {
            check_expr(base, theory, binding, has_reflection_bridge, diagnostics);
            check_expr(exp, theory, binding, has_reflection_bridge, diagnostics);
        }
        ExprKind::Ident(name) => {
            if binding == Some(name.as_str()) {
                diagnostics.push(reflection_error(
                    expr.line,
                    format!(
                        "self-reference guard rejected `{}`: direct binding recursion is not a proof of a fixed point",
                        name
                    ),
                ));
            }
        }
        ExprKind::QualifiedIdent { name, .. } => {
            if binding == Some(name.as_str()) {
                diagnostics.push(reflection_error(
                    expr.line,
                    format!(
                        "self-reference guard rejected `{}`: direct binding recursion is not a proof of a fixed point",
                        name
                    ),
                ));
            }
        }
        ExprKind::IntLiteral(_) => {}
    }
}

fn expr_mentions_binding(expr: &Expr, binding: &str) -> bool {
    match &expr.kind {
        ExprKind::Ident(name) => name == binding,
        ExprKind::QualifiedIdent { name, .. } => name == binding,
        ExprKind::Add { lhs, rhs } | ExprKind::CompareGt { lhs, rhs } => {
            expr_mentions_binding(lhs, binding) || expr_mentions_binding(rhs, binding)
        }
        ExprKind::Power { base, exp } => {
            expr_mentions_binding(base, binding) || expr_mentions_binding(exp, binding)
        }
        ExprKind::Call { args, .. } => args.iter().any(|arg| expr_mentions_binding(arg, binding)),
        ExprKind::IntLiteral(_) => false,
    }
}

fn expr_is_self_marker(expr: &Expr) -> bool {
    match &expr.kind {
        ExprKind::Ident(name) => is_self_marker(name),
        ExprKind::QualifiedIdent { name, .. } => is_self_marker(name),
        ExprKind::Call { name, args } => {
            is_self_marker(name)
                || is_diagonal_self_reference_call(name)
                || args.iter().any(expr_is_self_marker)
        }
        ExprKind::Add { lhs, rhs } | ExprKind::CompareGt { lhs, rhs } => {
            expr_is_self_marker(lhs) || expr_is_self_marker(rhs)
        }
        ExprKind::Power { base, exp } => {
            expr_is_self_marker(base) || expr_is_self_marker(exp)
        }
        ExprKind::IntLiteral(_) => false,
    }
}

fn is_reflection_call(name: &str) -> bool {
    matches!(
        name,
        "reflect"
            | "reflection"
            | "reflect_ast"
            | "inspect_ast"
            | "quote_ast"
            | "quote_formula"
            | "reify"
            | "unquote"
            | "eval_ast"
            | "eval_formula"
            | "diagonalize"
            | "fixed_point"
            | "godel_sentence"
            | "liar_sentence"
            | "truth_of"
            | "truth_claim"
            | "provability_of"
            | "provable_of"
            | "unprovable_of"
            | "not_provable"
            | "consistency_of"
            | "quote_self"
            | "inspect_self"
            | "self_reference"
            | "says_about_self"
            | "truth_of_self"
            | "provable_self"
            | "unprovable_self"
            | "says_unprovable_self"
    )
}

fn is_diagonal_self_reference_call(name: &str) -> bool {
    matches!(
        name,
        "quote_self"
            | "inspect_self"
            | "self_reference"
            | "says_about_self"
            | "truth_of_self"
            | "provable_self"
            | "unprovable_self"
            | "says_unprovable_self"
            | "godel_sentence"
            | "liar_sentence"
            | "diagonalize"
            | "fixed_point"
    )
}

fn is_truth_or_provability_call(name: &str) -> bool {
    matches!(
        name,
        "truth_of"
            | "truth_claim"
            | "provability_of"
            | "provable_of"
            | "unprovable_of"
            | "not_provable"
            | "consistency_of"
    )
}

fn is_self_marker(name: &str) -> bool {
    matches!(name, "self" | "this" | "this_formula" | "current_formula")
}

fn reflection_error(line: usize, message: String) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::ReflectionBoundaryError, Some(line), message)
}
