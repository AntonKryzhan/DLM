use crate::ast::*;
use crate::diagnostics::{Diagnostic, DiagnosticKind, SourceSpan};

pub fn parse_module(source: &str) -> Result<Module, Vec<Diagnostic>> {
    let mut diagnostics = Vec::new();
    let mut module_name: Option<String> = None;
    let mut imports = Vec::new();
    let mut items = Vec::new();
    let mut current_theory: Option<TheoryDecl> = None;
    let mut current_bridge: Option<BridgeDecl> = None;

    for (idx, raw_line) in source.lines().enumerate() {
        let line_no = idx + 1;
        let line = strip_comment(raw_line).trim().to_string();
        if line.is_empty() {
            continue;
        }

        if current_bridge.is_some() {
            if line.starts_with("kind") {
                if let Some(kind_text) = line.split('=').nth(1) {
                    let kind = parse_bridge_kind(kind_text.trim().trim_end_matches(';'));
                    if let Some(bridge) = &mut current_bridge {
                        bridge.kind = kind;
                    }
                }
                continue;
            }
            if line == "}" {
                if let Some(bridge) = current_bridge.take() {
                    items.push(ModuleItem::Bridge(bridge));
                }
                continue;
            }
            continue;
        }

        if let Some(theory) = &mut current_theory {
            if line == "}" {
                let theory = current_theory.take().expect("theory exists");
                items.push(ModuleItem::Theory(theory));
                continue;
            }
            if let Some(rest) = line.strip_prefix("let ") {
                match parse_let(rest, line_no, find_column(raw_line, "let").unwrap_or(1) + 4) {
                    Ok(let_decl) => theory.items.push(TheoryItem::Let(let_decl)),
                    Err(diag) => diagnostics.push(diag),
                }
                continue;
            }
            if line.starts_with("type ") || line.starts_with("axiom ") || line.starts_with("fn ") {
                // MVP parser accepts but ignores declarations not needed by checker v0.1.
                continue;
            }
            match parse_expr_at(&line, line_no, find_column(raw_line, line.as_str()).unwrap_or(1)) {
                Ok(expr) => theory.items.push(TheoryItem::Expr(expr)),
                Err(diag) => diagnostics.push(diag),
            }
            continue;
        }

        if let Some(rest) = line.strip_prefix("module ") {
            module_name = Some(rest.trim().trim_end_matches(';').to_string());
            continue;
        }

        if let Some(rest) = line.strip_prefix("import ") {
            imports.push(ImportDecl {
                path: rest.trim().trim_end_matches(';').to_string(),
            });
            continue;
        }

        if let Some(rest) = line.strip_prefix("theory ") {
            let name = rest.trim().trim_end_matches('{').trim().to_string();
            if name.is_empty() {
                diagnostics.push(Diagnostic::error_at(
                    DiagnosticKind::ParseError,
                    SourceSpan::line_col(line_no, find_column(raw_line, "theory").unwrap_or(1), line.len()),
                    "missing theory name",
                ));
            } else {
                current_theory = Some(TheoryDecl {
                    name,
                    items: Vec::new(),
                });
            }
            continue;
        }

        if let Some(rest) = line.strip_prefix("bridge ") {
            match parse_bridge_header(rest, line_no) {
                Ok(bridge) => {
                    if line.ends_with("}") {
                        items.push(ModuleItem::Bridge(bridge));
                    } else {
                        current_bridge = Some(bridge);
                    }
                }
                Err(diag) => diagnostics.push(diag),
            }
            continue;
        }

        diagnostics.push(Diagnostic::error(
            DiagnosticKind::NoAmbientTheoryError,
            Some(line_no),
            "value-level declaration outside theory context is not allowed in MVP",
        ));
    }

    if let Some(theory) = current_theory.take() {
        diagnostics.push(Diagnostic::error(
            DiagnosticKind::ParseError,
            None,
            format!("unclosed theory block '{}'; missing '}}'", theory.name),
        ));
    }
    if let Some(bridge) = current_bridge.take() {
        diagnostics.push(Diagnostic::error(
            DiagnosticKind::ParseError,
            None,
            format!("unclosed bridge block '{}'; missing '}}'", bridge.name),
        ));
    }

    if !diagnostics.is_empty() {
        return Err(diagnostics);
    }

    Ok(Module {
        name: module_name.unwrap_or_else(|| "anonymous".to_string()),
        imports,
        items,
    })
}

fn strip_comment(line: &str) -> &str {
    line.split("//").next().unwrap_or(line)
}

fn parse_let(rest: &str, line: usize, rest_col: usize) -> Result<LetDecl, Diagnostic> {
    let trimmed = rest.trim().trim_end_matches(';');
    let trimmed_col = rest_col + rest.find(trimmed).unwrap_or(0);
    let Some((name, expr_text)) = trimmed.split_once('=') else {
        return Err(Diagnostic::error_at(
            DiagnosticKind::ParseError,
            SourceSpan::line_col(line, trimmed_col, trimmed.len()),
            "expected let name = expression",
        ));
    };
    let name_text = name.trim();
    if name_text.is_empty() {
        return Err(Diagnostic::error_at(
            DiagnosticKind::ParseError,
            SourceSpan::line_col(line, trimmed_col, 1),
            "missing let binding name",
        ));
    }
    let eq_col = trimmed_col + trimmed.find('=').unwrap_or(0);
    let expr = parse_expr_at(expr_text, line, eq_col + 1)?;
    Ok(LetDecl { name: name_text.to_string(), expr, line })
}

fn parse_bridge_header(rest: &str, line: usize) -> Result<BridgeDecl, Diagnostic> {
    // bridge PA_quote : PA -> MetaArithmetic {
    let header = rest.trim().trim_end_matches('{').trim();
    let Some((name, rhs)) = header.split_once(':') else {
        return Err(Diagnostic::error(
            DiagnosticKind::ParseError,
            Some(line),
            "expected bridge Name : Source -> Target",
        ));
    };
    let Some((source, target)) = rhs.split_once("->") else {
        return Err(Diagnostic::error(
            DiagnosticKind::ParseError,
            Some(line),
            "expected bridge source -> target",
        ));
    };
    Ok(BridgeDecl {
        name: name.trim().to_string(),
        source: source.trim().to_string(),
        target: target.trim().to_string(),
        kind: BridgeKind::Unknown("unspecified".to_string()),
        line,
    })
}

fn parse_bridge_kind(text: &str) -> BridgeKind {
    match text.trim().trim_matches('"') {
        "definitional" | "definition" | "definitional_extension" => BridgeKind::Definitional,
        "conservative" | "conservative_extension" => BridgeKind::Conservative,
        "quote" => BridgeKind::Quote,
        "transport" => BridgeKind::Transport,
        "soundness" => BridgeKind::Soundness,
        "reflection" | "reflective" => BridgeKind::Reflection,
        "migration" => BridgeKind::Migration,
        "materialize" | "materialization" => BridgeKind::Materialize,
        "unsafe" | "unsafe_cast" => BridgeKind::Unsafe,
        other => BridgeKind::Unknown(other.to_string()),
    }
}

pub fn parse_expr(text: &str, line: usize) -> Result<Expr, Diagnostic> {
    parse_expr_at(text, line, 1)
}

fn parse_expr_at(text: &str, line: usize, col: usize) -> Result<Expr, Diagnostic> {
    let (text, col) = trim_with_col(text, col);
    let text = text.trim_end();
    let text = text.trim_end_matches(';').trim_end();
    if text.is_empty() {
        return Err(Diagnostic::error_at(
            DiagnosticKind::ParseError,
            SourceSpan::line_col(line, col, 1),
            "empty expression",
        ));
    }

    if let Some(idx) = split_top_level_index(text, '>') {
        return Ok(Expr {
            kind: ExprKind::CompareGt {
                lhs: Box::new(parse_expr_at(&text[..idx], line, col)?),
                rhs: Box::new(parse_expr_at(&text[idx + 1..], line, col + idx + 1)?),
            },
            line,
        });
    }

    if let Some(idx) = split_top_level_index(text, '+') {
        return Ok(Expr {
            kind: ExprKind::Add {
                lhs: Box::new(parse_expr_at(&text[..idx], line, col)?),
                rhs: Box::new(parse_expr_at(&text[idx + 1..], line, col + idx + 1)?),
            },
            line,
        });
    }

    if let Some(idx) = split_top_level_index(text, '^') {
        return Ok(Expr {
            kind: ExprKind::Power {
                base: Box::new(parse_expr_at(&text[..idx], line, col)?),
                exp: Box::new(parse_expr_at(&text[idx + 1..], line, col + idx + 1)?),
            },
            line,
        });
    }

    if text.chars().all(|c| c.is_ascii_digit()) {
        return Ok(Expr::int(text, line));
    }

    if let Some((name, args_text, args_col)) = parse_call_parts(text, col) {
        let args = if args_text.trim().is_empty() {
            Vec::new()
        } else {
            split_args(args_text, args_col)
                .into_iter()
                .map(|(arg, arg_col)| parse_expr_at(arg, line, arg_col))
                .collect::<Result<Vec<_>, _>>()?
        };
        return Ok(Expr {
            kind: ExprKind::Call {
                name: name.to_string(),
                args,
            },
            line,
        });
    }

    if let Some((theory, name)) = text.split_once('.') {
        if is_ident(theory) && is_ident(name) {
            return Ok(Expr {
                kind: ExprKind::QualifiedIdent {
                    theory: theory.to_string(),
                    name: name.to_string(),
                },
                line,
            });
        }
    }

    if is_ident(text) {
        return Ok(Expr::ident(text, line));
    }

    Err(Diagnostic::error_at(
        DiagnosticKind::ParseError,
        SourceSpan::line_col(line, col, text.len()),
        format!("could not parse expression '{text}'"),
    ))
}

fn parse_call_parts(text: &str, col: usize) -> Option<(&str, &str, usize)> {
    let open = text.find('(')?;
    if !text.ends_with(')') {
        return None;
    }
    let name = text[..open].trim();
    if !is_ident(name) {
        return None;
    }
    Some((name, &text[open + 1..text.len() - 1], col + open + 1))
}

fn is_ident(text: &str) -> bool {
    let mut chars = text.chars();
    let Some(first) = chars.next() else {
        return false;
    };
    (first.is_ascii_alphabetic() || first == '_')
        && chars.all(|c| c.is_ascii_alphanumeric() || c == '_')
}

fn split_top_level_index(text: &str, needle: char) -> Option<usize> {
    let mut depth = 0i32;
    for (idx, ch) in text.char_indices().rev() {
        match ch {
            ')' => depth += 1,
            '(' => depth -= 1,
            _ => {}
        }
        if depth == 0 && ch == needle {
            return Some(idx);
        }
    }
    None
}

fn split_args(text: &str, col: usize) -> Vec<(&str, usize)> {
    let mut args = Vec::new();
    let mut start = 0usize;
    let mut depth = 0i32;
    for (idx, ch) in text.char_indices() {
        match ch {
            '(' => depth += 1,
            ')' => depth -= 1,
            ',' if depth == 0 => {
                let raw = &text[start..idx];
                let (trimmed, arg_col) = trim_with_col(raw, col + start);
                args.push((trimmed.trim_end(), arg_col));
                start = idx + 1;
            }
            _ => {}
        }
    }
    let raw = &text[start..];
    let (trimmed, arg_col) = trim_with_col(raw, col + start);
    args.push((trimmed.trim_end(), arg_col));
    args
}

fn trim_with_col(text: &str, col: usize) -> (&str, usize) {
    let trimmed = text.trim_start();
    (trimmed, col + text.len().saturating_sub(trimmed.len()))
}

fn find_column(line: &str, needle: &str) -> Option<usize> {
    line.find(needle).map(|idx| idx + 1)
}
