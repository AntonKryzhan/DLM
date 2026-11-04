//! v0.87 — Typed Core facts: what the checker established about a program,
//! handed to lowering so that the IR and the backend do not reconstruct types
//! or effects heuristically (laws 11 and 12, stage B).
//!
//! The checker records the checked type of every expression it infers, keyed
//! by `ExprId`, and the contract of every checked function. An expression that
//! was inferred with two different types is recorded as a conflict; lowering
//! leaves such nodes untyped and the IR verifier rejects them.

use std::collections::{BTreeMap, BTreeSet};

use crate::ast::{ExprId, ExprKind, FnDecl, Module, ModuleItem, RecordDecl, TheoryItem};
use crate::diagnostics::{SourceFile, SourceSpan};
use crate::ids::{DeclId, RecordId};
use crate::ordinary::OrdinaryType;
use crate::program::Program;

/// Checked facts about one function (`Theory.name`).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FunctionFacts {
    pub params: Vec<(String, OrdinaryType)>,
    pub ret: OrdinaryType,
    /// Effects of the body, including effects of called functions
    /// (`kind@boundary`, sorted). Function values are pure by construction.
    pub effects: Vec<String>,
    /// `pure` / `effectful`.
    pub purity: String,
    /// Termination as established by the checker: `non-recursive`, the
    /// recursion measure (`nat_decreasing`, `structural_subterm`,
    /// `lexicographic`), or `mutual(..)` for a mutual group.
    pub termination: String,
    pub line: usize,
    pub span: SourceSpan,
    /// v0.102: `ByteBuf` parameters still owned when the body has produced
    /// its value; compiled code releases them there (`crate::ownership`).
    pub released_params: Vec<String>,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct TypedFacts {
    pub native: crate::native::Mir,
    pub expr_types: BTreeMap<ExprId, OrdinaryType>,
    /// Every source expression, including callable arguments checked through
    /// collection contracts rather than ordinary expression inference.
    pub expr_spans: BTreeMap<ExprId, SourceSpan>,
    /// Expressions inferred with different types (never expected).
    pub conflicts: BTreeSet<ExprId>,
    /// `Theory.name` -> facts, for every checked function of every module.
    pub functions: BTreeMap<String, FunctionFacts>,
    /// Checked schemas of declarations (`Theory.name`), before ID resolution.
    pub records: BTreeMap<String, RecordFacts>,
    /// Authoritative checked declarations; name-keyed maps above are display
    /// views and are not inputs to native lowering.
    pub function_declarations: BTreeMap<DeclId, CheckedFunctionDeclaration>,
    pub record_declarations: BTreeMap<DeclId, CheckedRecordDeclaration>,
    /// Types with record names resolved by the frontend, before lowering.
    pub executable_types: BTreeMap<ExprId, OrdinaryType>,
    /// v0.102: `let` expressions whose `ByteBuf` owner is still held after the
    /// body; compiled code releases it there (`crate::ownership`).
    pub released_lets: BTreeSet<ExprId>,
    /// v0.102: `if_then_else` branches after which the named owners, consumed
    /// on the other branch only, are released.
    pub released_branches: BTreeMap<ExprId, Vec<String>>,
    record_scopes: BTreeMap<String, BTreeMap<String, RecordId>>,
    contexts: Vec<ModuleContext>,
    checked: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct ModuleContext {
    name: String,
    source: SourceFile,
    imports: Vec<String>,
    theories: Vec<(String, Option<String>, BTreeSet<String>)>,
}

fn program_contexts(program: &Program) -> Vec<ModuleContext> {
    program.modules.iter().map(|module| ModuleContext {
        name: module.name.clone(), source: module.source.clone(),
        imports: module.imports.iter().map(|i| i.path.clone()).collect(),
        theories: module.items.iter().filter_map(|item| match item {
            ModuleItem::Theory(t) => Some((t.name.clone(), t.extends.clone(), t.private.clone())),
            _ => None,
        }).collect(),
    }).collect()
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SourceDeclarationKind {
    Function,
    Record,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DeclarationOrigin {
    pub kind: SourceDeclarationKind,
    pub module: String,
    pub theory: String,
    pub name: String,
    pub span: SourceSpan,
}

impl DeclarationOrigin {
    pub fn symbol(&self) -> String {
        format!("{}::{}.{}", self.module, self.theory, self.name)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CheckedFunctionDeclaration {
    pub declaration: FnDecl,
    pub origin: DeclarationOrigin,
    /// Canonical types already contain nominal RecordId references.
    pub facts: FunctionFacts,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CheckedRecordDeclaration {
    pub declaration: RecordDecl,
    pub origin: DeclarationOrigin,
    pub record_id: RecordId,
    pub facts: RecordFacts,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RecordFacts {
    pub fields: Vec<(String, OrdinaryType)>,
    pub line: usize,
    pub span: SourceSpan,
}

impl TypedFacts {
    pub(crate) fn bind_contexts(&mut self, program: &Program) {
        self.contexts = program_contexts(program);
    }

    pub(crate) fn matches_contexts(&self, program: &Program) -> bool {
        self.contexts == program_contexts(program)
    }

    pub fn is_checked(&self) -> bool {
        self.checked && self.conflicts.is_empty()
    }

    pub(crate) fn finish(&mut self, success: bool) {
        self.checked = success;
    }

    pub fn executable_type_of(&self, id: ExprId) -> Option<&OrdinaryType> {
        if self.conflicts.contains(&id) { return None; }
        self.executable_types.get(&id)
    }

    pub(crate) fn inherit_records(&mut self, base: &str, derived: &str) {
        let records = self.record_scopes.get(base).cloned().unwrap_or_default();
        self.record_scopes.entry(derived.to_string()).or_default().extend(records);
    }

    fn nominal_type(&self, ty: &OrdinaryType, theory: &str) -> Option<OrdinaryType> {
        ty.map_records(&mut |t| match t {
            OrdinaryType::Record(name) => self.record_scopes.get(theory)?.get(name).copied().map(OrdinaryType::RecordRef),
            OrdinaryType::RecordRef(id) => self.record_declarations.values().any(|r| r.record_id == *id).then(|| t.clone()),
            _ => unreachable!(),
        })
    }

    /// As `nominal_type`, also accepting the record being registered.
    fn nominal_type_pending(&self, ty: &OrdinaryType, theory: &str, pending: RecordId) -> Option<OrdinaryType> {
        ty.map_records(&mut |t| match t {
            OrdinaryType::Record(name) => self.record_scopes.get(theory)?.get(name).copied().map(OrdinaryType::RecordRef),
            OrdinaryType::RecordRef(id) => (*id == pending || self.record_declarations.values().any(|r| r.record_id == *id)).then(|| t.clone()),
            _ => unreachable!(),
        })
    }

    pub(crate) fn record_function(
        &mut self, module: &str, theory: &str, decl: &FnDecl, facts: FunctionFacts,
    ) -> Result<(), String> {
        if self.function_declarations.contains_key(&decl.id) || self.record_declarations.contains_key(&decl.id) {
            return Err(format!("duplicate checked declaration identity {}", decl.id));
        }
        let params = facts.params.iter().map(|(name, ty)| self.nominal_type(ty, theory).map(|t| (name.clone(), t)))
            .collect::<Option<Vec<_>>>().ok_or("unresolved record in checked function parameters")?;
        let ret = self.nominal_type(&facts.ret, theory).ok_or("unresolved record in checked function result")?;
        let origin = DeclarationOrigin { kind: SourceDeclarationKind::Function, module: module.to_string(), theory: theory.to_string(), name: decl.name.clone(), span: facts.span };
        self.function_declarations.insert(decl.id, CheckedFunctionDeclaration {
            declaration: decl.clone(), origin, facts: FunctionFacts { params, ret, ..facts.clone() },
        });
        self.functions.insert(format!("{theory}.{}", decl.name), facts);
        Ok(())
    }

    pub(crate) fn record_record(
        &mut self, module: &str, theory: &str, decl: &RecordDecl, facts: RecordFacts,
    ) -> Result<(), String> {
        if self.function_declarations.contains_key(&decl.id) || self.record_declarations.contains_key(&decl.id) {
            return Err(format!("duplicate checked declaration identity {}", decl.id));
        }
        let raw = u32::try_from(self.record_declarations.len()).map_err(|_| "record identity space exhausted")?;
        let record_id = RecordId::new(raw);
        // v0.126: registered first, so recursive fields resolve to the record itself
        let previous = self.record_scopes.entry(theory.to_string()).or_default().insert(decl.name.clone(), record_id);
        let fields = match facts.fields.iter().map(|(name, ty)| self.nominal_type_pending(ty, theory, record_id).map(|t| (name.clone(), t))).collect::<Option<Vec<_>>>() {
            Some(fields) => fields,
            None => {
                let scope = self.record_scopes.entry(theory.to_string()).or_default();
                match previous {
                    Some(p) => scope.insert(decl.name.clone(), p),
                    None => scope.remove(&decl.name),
                };
                return Err("unresolved record in checked schema".to_string());
            }
        };
        let origin = DeclarationOrigin { kind: SourceDeclarationKind::Record, module: module.to_string(), theory: theory.to_string(), name: decl.name.clone(), span: facts.span };
        self.record_declarations.insert(decl.id, CheckedRecordDeclaration {
            declaration: decl.clone(), origin, record_id, facts: RecordFacts { fields, ..facts.clone() },
        });
        self.record_scopes.entry(theory.to_string()).or_default().insert(decl.name.clone(), record_id);
        self.records.insert(format!("{theory}.{}", decl.name), facts);
        Ok(())
    }

    pub(crate) fn record_sources(&mut self, module: &Module) {
        let mut pending = Vec::new();
        for item in &module.items {
            if let ModuleItem::Theory(theory) = item {
                for item in &theory.items {
                    match item {
                        TheoryItem::Let(decl) => pending.push(&decl.expr),
                        TheoryItem::Expr(expr) => pending.push(expr),
                        TheoryItem::Fn(decl) => pending.push(&decl.body),
                        TheoryItem::Statement(statement) => pending.push(&statement.body),
                        TheoryItem::Record(_) => {}
                    }
                }
            }
        }
        while let Some(expr) = pending.pop() {
            self.record_span(expr.id, expr.span);
            match &expr.kind {
                ExprKind::Power { base: a, exp: b } | ExprKind::Add { lhs: a, rhs: b } | ExprKind::CompareGt { lhs: a, rhs: b } => {
                    pending.push(a);
                    pending.push(b);
                }
                ExprKind::Call { args, .. } => pending.extend(args),
                ExprKind::Lambda { body, .. } => pending.push(body),
                ExprKind::Let { value, body, .. } => {
                    pending.push(value);
                    pending.push(body);
                }
                _ => {}
            }
        }
    }

    pub fn type_of(&self, id: ExprId) -> Option<&OrdinaryType> {
        if self.conflicts.contains(&id) {
            return None;
        }
        self.expr_types.get(&id)
    }

    pub(crate) fn record(&mut self, id: ExprId, ty: OrdinaryType, span: SourceSpan, theory: &str) {
        self.record_span(id, span);
        match self.nominal_type(&ty, theory) {
            Some(nominal) => {
                if self.executable_types.insert(id, nominal.clone()).is_some_and(|old| old != nominal) {
                    self.conflicts.insert(id);
                }
            }
            None => { self.conflicts.insert(id); }
        }
        match self.expr_types.get(&id) {
            Some(existing) if *existing != ty => {
                self.conflicts.insert(id);
            }
            Some(_) => {}
            None => {
                self.expr_types.insert(id, ty);
            }
        }
    }

    fn record_span(&mut self, id: ExprId, span: SourceSpan) {
        if self.expr_spans.insert(id, span).is_some_and(|old| old != span) {
            self.conflicts.insert(id);
        }
    }
}
