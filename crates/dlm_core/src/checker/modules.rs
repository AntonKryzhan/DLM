//! v0.80 — imports, `theory Name extends Base`, and `private` declarations.
//!
//! A derived theory starts with the declarations of its base: checked `fn`s,
//! records, theorems / axioms / conjectures (with their passports, so trust
//! and taint carry over) and the lemma registry. Theory values (`let`) are
//! state, not declarations, and are not inherited. A base's `private`
//! declarations stay in the derived tables (inherited bodies may call them)
//! but are hidden from code written in theories of other modules.

use std::collections::{BTreeMap, BTreeSet};

use super::ordinary_surface::{called_names, free_idents};
use super::Checker;
use crate::ast::*;
use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::TypeKind;

fn import_error(line: usize, message: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::ModuleImportError, Some(line), message)
}

/// Identifiers of a type string (`List<Point>` -> `List`, `Point`).
fn type_names(ty: &str) -> impl Iterator<Item = &str> {
    ty.split(|c: char| !(c.is_alphanumeric() || c == '_')).filter(|s| !s.is_empty())
}

impl Checker {
    /// Set up `theory` before its items are checked: register its module,
    /// inherit from its base and check that its own code does not use names
    /// hidden by another module. Returns the inherited names (name -> base).
    pub(super) fn enter_theory(&mut self, theory: &TheoryDecl) -> Result<BTreeMap<String, String>, Diagnostic> {
        let line = theory.line;
        if let Some(owner) = self.theory_modules.get(&theory.name) {
            return Err(import_error(
                line,
                format!("theory '{}' is already declared in module '{owner}'", theory.name),
            ));
        }
        let mut inherited = BTreeMap::new();
        if let Some(base) = &theory.extends {
            if base == &theory.name {
                return Err(import_error(line, format!("theory '{}' cannot extend itself", theory.name)));
            }
            let Some(base_module) = self.theory_modules.get(base).cloned() else {
                return Err(import_error(line, format!("theory '{}' extends unknown theory '{base}'", theory.name)).with_help(
                    "a theory can extend theories declared earlier in its module or in an imported module",
                ));
            };
            let cross_module = base_module != self.current_module;
            inherited = self.inherit(base, &theory.name, cross_module, &base_module);
        }
        self.theory_modules.insert(theory.name.clone(), self.current_module.clone());
        let private = self.private_names.entry(theory.name.clone()).or_default();
        private.extend(theory.private.iter().cloned());
        self.check_hidden_names(theory)?;
        Ok(inherited)
    }

    fn inherit(&mut self, base: &str, derived: &str, cross_module: bool, base_module: &str) -> BTreeMap<String, String> {
        let mut inherited = BTreeMap::new();
        self.typed.get_mut().inherit_records(base, derived);
        let functions = self.functions.get(base).cloned().unwrap_or_default();
        for name in functions.keys() {
            inherited.insert(name.clone(), base.to_string());
        }
        self.functions.entry(derived.to_string()).or_default().extend(functions);
        let records = self.records.get(base).cloned().unwrap_or_default();
        for name in records.keys() {
            inherited.insert(name.clone(), base.to_string());
        }
        self.records.entry(derived.to_string()).or_default().extend(records);
        let statements: Vec<_> = self
            .theories
            .get(base)
            .map(|scope| {
                scope
                    .iter()
                    .filter(|(_, p)| matches!(p.ty, TypeKind::Theorem { .. } | TypeKind::Goal { .. }))
                    .map(|(n, p)| (n.clone(), p.clone()))
                    .collect()
            })
            .unwrap_or_default();
        for (name, _) in &statements {
            inherited.insert(name.clone(), base.to_string());
        }
        self.theories.entry(derived.to_string()).or_default().extend(statements);
        let lemmas = self.lemmas.get(base).cloned().unwrap_or_default();
        self.lemmas.entry(derived.to_string()).or_default().extend(lemmas);

        // Visibility: hidden names stay hidden; private names of another
        // module become hidden; private names of the same module stay private.
        let mut hidden = self.hidden.get(base).cloned().unwrap_or_default();
        let base_private = self.private_names.get(base).cloned().unwrap_or_default();
        if cross_module {
            for name in base_private {
                hidden.insert(name, base_module.to_string());
            }
        } else {
            self.private_names.entry(derived.to_string()).or_default().extend(base_private);
        }
        self.hidden.insert(derived.to_string(), hidden);
        self.inherited_from.insert(derived.to_string(), base.to_string());
        inherited
    }

    /// Code of `theory` must not mention names hidden from it.
    fn check_hidden_names(&self, theory: &TheoryDecl) -> Result<(), Diagnostic> {
        let Some(hidden) = self.hidden.get(&theory.name).filter(|h| !h.is_empty()) else { return Ok(()) };
        let reject = |name: &str, line: usize| -> Result<(), Diagnostic> {
            match hidden.get(name) {
                Some(module) => Err(Diagnostic::error(
                    DiagnosticKind::ModuleInterfaceError,
                    Some(line),
                    format!("'{name}' is private to module '{module}'"),
                )
                .with_help("only non-private declarations of another module are visible in a theory that extends it")),
                None => Ok(()),
            }
        };
        let expr_names = |expr: &Expr, params: &[FnParam]| -> Vec<String> {
            let mut bound: Vec<String> = params.iter().map(|p| p.name.clone()).collect();
            let mut names = Vec::new();
            free_idents(expr, &mut bound, &mut names);
            let mut calls = Vec::new();
            called_names(expr, &mut calls);
            names.extend(calls.into_iter().filter(|c| !params.iter().any(|p| &p.name == c)));
            names
        };
        for item in &theory.items {
            let (names, types, line): (Vec<String>, Vec<String>, usize) = match item {
                TheoryItem::Let(decl) => (expr_names(&decl.expr, &[]), Vec::new(), decl.line),
                TheoryItem::Expr(expr) => (expr_names(expr, &[]), Vec::new(), expr.line),
                TheoryItem::Fn(decl) => {
                    let mut types: Vec<String> = decl.params.iter().map(|p| p.ty.clone()).collect();
                    types.push(decl.ret.clone());
                    (expr_names(&decl.body, &decl.params), types, decl.line)
                }
                TheoryItem::Record(record) => (Vec::new(), record.fields.iter().map(|f| f.ty.clone()).collect(), record.line),
                TheoryItem::Statement(statement) => {
                    let mut names = expr_names(&statement.body, &statement.params);
                    if let Some(hint) = &statement.proof {
                        names.extend(hint.lemmas.iter().cloned());
                    }
                    (names, statement.params.iter().map(|p| p.ty.clone()).collect(), statement.line)
                }
            };
            for name in &names {
                reject(name, line)?;
            }
            for ty in &types {
                for name in type_names(ty) {
                    reject(name, line)?;
                }
            }
        }
        Ok(())
    }

    /// Imports of `module` must already be loaded into this checker.
    pub(super) fn check_imports_loaded(&self, module: &Module, loaded: &BTreeSet<String>) -> Vec<Diagnostic> {
        module
            .imports
            .iter()
            .filter(|import| !loaded.contains(import.path.trim()))
            .map(|import| {
                Diagnostic::error(
                    DiagnosticKind::ModuleImportError,
                    None,
                    format!("module '{}' imports '{}', which is not loaded", module.name, import.path.trim()),
                )
                .with_help("check the program with dlm_core::load_program / link_program (the dlm CLI loads imports from files)")
            })
            .collect()
    }
}
