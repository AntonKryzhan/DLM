//! v0.87 — structural verifier of the typed IR (law 11).
//!
//! Run after lowering (and after every future IR pass). It checks that
//! operands exist, slots are defined before use, calls match the callee
//! signature, and types are consistent: every node is typed, operators get
//! operands of the types their semantics allows, branches agree, and function
//! bodies produce the declared result. A clean result does not prove a pass
//! correct; a failure always means a defect of the implementation.

use std::collections::BTreeSet;
use std::fmt;

use crate::compile::{IrExpr, IrFunction, IrInit, IrOp, IrProgram};
use crate::diagnostics::SourceSpan;
use crate::ordinary::OrdinaryType;
use crate::typed_core::SourceDeclarationKind;

mod builtins;
mod guards;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct IrIssue {
    pub function: String,
    pub message: String,
}

impl fmt::Display for IrIssue {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}: {}", self.function, self.message)
    }
}

struct Verifier<'a> {
    program: &'a IrProgram,
    function: &'a IrFunction,
    partial: &'a [bool],
    defined: Vec<bool>,
    guards: BTreeSet<guards::Fact>,
    issues: Vec<IrIssue>,
}

fn show(ty: &Option<OrdinaryType>) -> String {
    ty.as_ref().map_or_else(|| "?".to_string(), OrdinaryType::canonical)
}

fn is_number(ty: &Option<OrdinaryType>) -> bool {
    matches!(ty, Some(OrdinaryType::Nat | OrdinaryType::Int))
}

fn valid_type(ty: &OrdinaryType, records: usize) -> bool {
    !ty.has_projection() && ty.map_records(&mut |t| match t {
        OrdinaryType::RecordRef(id) if (id.raw() as usize) < records => Some(t.clone()),
        _ => None,
    }).is_some()
}

/// v0.126: every reference to record `id` inside `ty` is under Option, List,
/// Sequence, Result, Sum or Fn (a well-founded recursive field).
fn self_reference_guarded(ty: &OrdinaryType, id: usize, guarded: bool) -> bool {
    use OrdinaryType as T;
    match ty {
        T::RecordRef(r) => guarded || r.raw() as usize != id,
        T::Option(t) | T::List(t) | T::Sequence(t) => self_reference_guarded(t, id, true),
        T::Result(a, b) | T::Sum(a, b) => self_reference_guarded(a, id, true) && self_reference_guarded(b, id, true),
        T::Product(a, b) => self_reference_guarded(a, id, guarded) && self_reference_guarded(b, id, guarded),
        T::Fn(..) | T::EffectFn(..) => {
            let mut safe = true;
            ty.map_records(&mut |t| { if matches!(t, T::RecordRef(r) if r.raw() as usize == id) { safe = false; } Some(t.clone()) });
            safe
        }
        _ => true,
    }
}

fn valid_span(span: SourceSpan, program: &IrProgram) -> bool {
    program.sources.iter().any(|s| s.id == span.file_id)
        && span.start_line > 0
        && (if span.line_only() {
            span.start_line == span.end_line
        } else {
            span.start_col > 0 && span.end_col > 0
                && (span.start_line, span.start_col) < (span.end_line, span.end_col)
        })
}

impl Verifier<'_> {
    fn issue(&mut self, message: impl Into<String>) {
        self.issues.push(IrIssue { function: self.function.name.clone(), message: message.into() });
    }

    fn expect(&mut self, node: &IrExpr, ty: &OrdinaryType, what: &str) {
        let pure_effect_fn = match (&node.ty,ty) {
            (Some(OrdinaryType::EffectFn(p,r,b)),OrdinaryType::Fn(q,s)) => p == q && r == s && b.effects().is_empty() && !b.partial(),
            _ => false,
        };
        if node.ty.as_ref() != Some(ty) && !pure_effect_fn {
            self.issue(format!("{what} `{node}` has type {}, expected {}", show(&node.ty), ty.canonical()));
        }
    }

    fn same(&mut self, a: &Option<OrdinaryType>, b: &Option<OrdinaryType>, what: &str, node: &IrExpr) {
        if a != b {
            self.issue(format!("{what} in `{node}`: {} vs {}", show(a), show(b)));
        }
    }

    fn effect(&mut self, effect: &str) {
        if !self.function.effects.iter().any(|declared| declared == effect) {
            self.issue(format!("body effect {effect} is missing from the function contract"));
        }
    }

    fn function_value(&mut self, node: &IrExpr, callee: &IrFunction, id: usize) {
        let (params,ret) = match &node.ty {
            Some(OrdinaryType::Fn(p,r)) => {
                if !callee.effects.is_empty() || self.partial.get(id).copied().unwrap_or(true) {
                    self.issue(format!("effectful or partial function {} used as a pure total function value",callee.name));
                }
                (p,r)
            }
            Some(OrdinaryType::EffectFn(p,r,b)) => {
                if let Err(e) = b.allows(&callee.effects,self.partial.get(id).copied().unwrap_or(true)) { self.issue(e); }
                (p,r)
            }
            _ => { self.issue("function value has no callable type"); return; }
        };
        if params != &callee.param_types || Some(ret.as_ref()) != callee.ret.as_ref() {
            self.issue("function value type differs from its signature");
        }
        if node.ty.as_ref().is_some_and(OrdinaryType::nests_byte_buf) { self.issue("owned resource in callable signature"); }
    }

    fn case_branch(&mut self, node: &IrExpr, param: &OrdinaryType, ret: &OrdinaryType) {
        match &node.ty {
            Some(OrdinaryType::Fn(p,r)) | Some(OrdinaryType::EffectFn(p,r,_)) => {
                if p != &[param.clone()] || r.as_ref() != ret { self.issue("case branch signature differs"); }
                if let Some(OrdinaryType::EffectFn(_,_,b)) = &node.ty {
                    for e in b.effect_names() { self.effect(&e); }
                }
            }
            _ => self.issue("case branch must be callable"),
        }
    }

    fn slot(&mut self, slot: usize, node: &IrExpr) -> bool {
        if slot >= self.function.slots || slot >= self.function.slot_types.len() {
            self.issue(format!("slot %{slot} out of range ({} slots) in `{node}`", self.function.slots));
            return false;
        }
        if !self.defined[slot] {
            self.issue(format!("slot %{slot} used before definition in `{node}`"));
        }
        true
    }

    fn expr(&mut self, node: &IrExpr) {
        if !valid_span(node.span, self.program) {
            self.issue(format!("invalid source span or unknown file identity in `{node}`"));
        }
        if let Some(origin) = node.origin {
            if self.program.source_spans.get(&origin) != Some(&node.span) {
                self.issue(format!("source origin {origin:?} does not match the checked span in `{node}`"));
            }
        }
        if node.ty.is_none() {
            self.issue(format!("untyped node `{node}`"));
        }
        let ty = node.ty.clone();
        if ty.as_ref().is_some_and(|t| !valid_type(t, self.program.records.len())) {
            self.issue(format!("unresolved or unknown record identity in node `{node}`"));
        }
        // v0.102: a ByteBuf is never an element, component or function-type part.
        if ty.as_ref().is_some_and(OrdinaryType::nests_byte_buf) {
            self.issue(format!("ByteBuf nested in the type of `{node}`"));
        }
        match &node.op {
            IrOp::Nat(_) => self.expect(node, &OrdinaryType::Nat, "Nat constant"),
            IrOp::Int(_) => self.expect(node, &OrdinaryType::Int, "Int constant"),
            IrOp::Bool(_) => self.expect(node, &OrdinaryType::Bool, "Bool constant"),
            IrOp::Text(_) => self.expect(node, &OrdinaryType::Text, "Text constant"),
            IrOp::Local(slot) => {
                if self.slot(*slot, node) {
                    let slot_ty = self.function.slot_types[*slot].clone();
                    self.same(&ty, &slot_ty, "local type differs from its slot", node);
                }
            }
            IrOp::Global(index) => match self.program.global_types.get(*index) {
                Some(global) => {
                    let global = global.clone();
                    self.same(&ty, &global, "global type differs", node);
                }
                None => self.issue(format!("unknown global @g{index}")),
            },
            IrOp::FunctionValue(id) => match self.program.functions.get(*id) {
                Some(callee) if !callee.is_lambda && callee.captures == 0 => {
                    self.function_value(node,callee,*id);
                }
                _ => self.issue(format!("function value &f{id} does not name a function")),
            },
            IrOp::Closure { function, captures } => match self.program.functions.get(*function) {
                Some(callee) if callee.is_lambda => {
                    self.function_value(node,callee,*function);
                    if captures.len() != callee.captures {
                        self.issue(format!("closure captures {} slots, lambda expects {}", captures.len(), callee.captures));
                    }
                    for (index, slot) in captures.iter().enumerate() {
                        if self.slot(*slot, node) {
                            let (outer, inner) = (self.function.slot_types[*slot].clone(), callee.slot_types.get(index).cloned().flatten());
                            self.same(&outer, &inner, "captured slot type differs", node);
                        }
                    }

                }
                _ => self.issue(format!("closure of f{function}, which is not a lambda")),
            },
            IrOp::Call { function, args } => {
                args.iter().for_each(|a| self.expr(a));
                match self.program.functions.get(*function) {
                    Some(callee) if !callee.is_lambda && callee.captures == 0 => {
                        if args.len() != callee.param_types.len() || args.len() != callee.params.len() {
                            self.issue(format!("call of {} with {} argument(s), expects {}", callee.name, args.len(), callee.params.len()));
                        }
                        for (arg, param) in args.iter().zip(&callee.param_types) {
                            self.expect(arg, param, "argument");
                        }
                        let ret = callee.ret.clone();
                        self.same(&ty, &ret, "call result type differs from the callee result", node);
                        for effect in &callee.effects {
                            self.effect(effect);
                        }
                    }
                    _ => self.issue(format!("call of f{function}, which is not a function")),
                }
            }
            IrOp::Apply { callee, args } => {
                self.expr(callee);
                args.iter().for_each(|a| self.expr(a));
                match &callee.ty {
                    Some(OrdinaryType::Fn(params, ret)) | Some(OrdinaryType::EffectFn(params, ret, _)) => {
                        if let Some(OrdinaryType::EffectFn(_,_,b)) = &callee.ty { for e in b.effect_names() { self.effect(&e); } }
                        if params.len() != args.len() {
                            self.issue(format!("apply with {} argument(s) to a function of {} parameter(s)", args.len(), params.len()));
                        }
                        for (arg, param) in args.iter().zip(params) {
                            self.expect(arg, param, "argument");
                        }
                        self.same(&ty, &Some((**ret).clone()), "apply result type differs", node);
                    }
                    other => self.issue(format!("apply of a non-function value of type {}", show(other))),
                }
            }
            IrOp::Builtin { name, args } => {
                args.iter().for_each(|a| self.expr(a));
                self.builtin(node, name, args);
                self.projection_guard(name, args);
                if let Some((kind, boundary, _)) = crate::checker::builtin_effect(name) {
                    self.effect(&format!("{kind}@{boundary}"));
                }
            }
            IrOp::Empty(kind) => {
                let valid = matches!((*kind, &ty),
                    ("none", Some(OrdinaryType::Option(_)))
                    | ("empty_list", Some(OrdinaryType::List(_)))
                    | ("empty_sequence", Some(OrdinaryType::Sequence(_)))
                );
                if !valid {
                    self.issue(format!("invalid empty constructor {kind} of type {}", show(&ty)));
                }
            }
            IrOp::Add(a, b) | IrOp::Pow(a, b) => {
                self.expr(a);
                self.expr(b);
                let pow = matches!(node.op, IrOp::Pow(..));
                if pow {
                    self.expect(a, &OrdinaryType::Nat, "power base");
                    self.expect(b, &OrdinaryType::Nat, "power exponent");
                } else if !is_number(&a.ty) {
                    self.issue(format!("operand `{a}` of + has type {}, expected Nat or Int", show(&a.ty)));
                }
                self.same(&a.ty, &b.ty, "operands of an arithmetic operator differ", node);
                self.same(&ty, &a.ty, "arithmetic result type differs from its operands", node);
            }
            IrOp::Gt(a, b) => {
                self.expr(a);
                self.expr(b);
                if !is_number(&a.ty) {
                    self.issue(format!("operand `{a}` of > has type {}, expected Nat or Int", show(&a.ty)));
                }
                self.same(&a.ty, &b.ty, "operands of > differ", node);
                self.expect(node, &OrdinaryType::Bool, "comparison");
            }
            IrOp::If(c, a, b) => {
                self.expr(c);
                self.branch(a, c, true);
                self.branch(b, c, false);
                self.expect(c, &OrdinaryType::Bool, "condition");
                self.same(&a.ty, &ty, "then-branch type differs", node);
                self.same(&b.ty, &ty, "else-branch type differs", node);
            }
            IrOp::And(a, b) | IrOp::Or(a, b) => {
                self.expr(a);
                self.branch(b, a, matches!(node.op, IrOp::And(..)));
                self.expect(a, &OrdinaryType::Bool, "operand");
                self.expect(b, &OrdinaryType::Bool, "operand");
                self.expect(node, &OrdinaryType::Bool, "logical result");
            }
            IrOp::UnwrapOr(a, b) => {
                self.expr(a);
                self.expr(b);
                match &a.ty {
                    Some(OrdinaryType::Option(inner) | OrdinaryType::Result(inner, _)) => {
                        self.same(&Some((**inner).clone()), &ty, "unwrap_or result differs from the payload", node);
                    }
                    other => self.issue(format!("unwrap_or of {} (not Option or Result)", show(other))),
                }
                self.same(&b.ty, &ty, "unwrap_or default type differs", node);
            }
            IrOp::Let { slot, value, body } => {
                self.expr(value);
                if *slot < self.function.slots && *slot < self.function.slot_types.len() {
                    let slot_ty = self.function.slot_types[*slot].clone();
                    self.same(&value.ty, &slot_ty, "let value type differs from its slot", node);
                    let before = self.defined[*slot];
                    if before {
                        self.issue(format!("let overwrites active slot %{slot}"));
                    }
                    self.defined[*slot] = true;
                    let guard_before = self.guards.clone();
                    self.guards.retain(|(s, _)| *s != guards::Subject::Local(*slot));
                    self.expr(body);
                    self.guards = guard_before;
                    self.defined[*slot] = before;
                } else {
                    self.issue(format!("let binds slot %{slot} out of range"));
                    self.expr(body);
                }
                self.same(&body.ty, &ty, "let result differs from its body", node);
            }
            IrOp::Inject { value, left } => {
                self.expr(value);
                match &ty {
                    Some(OrdinaryType::Sum(l, r)) => {
                        let side = if *left { l } else { r };
                        self.same(&value.ty, &Some((**side).clone()), "injected value type differs", node);
                    }
                    other => self.issue(format!("inl/inr of type {}", show(other))),
                }
            }
            IrOp::Case(s, f, g) => {
                self.expr(s);
                self.expr(f);
                self.expr(g);
                match (&s.ty, &ty) {
                    (Some(OrdinaryType::Sum(left, right)), Some(ret)) => {
                        self.case_branch(f,left,ret);
                        self.case_branch(g,right,ret);
                    }
                    _ => self.issue(format!("case requires a Sum operand and a typed result in `{node}`")),
                }
            }
            IrOp::Record { record, name, fields } => {
                fields.iter().for_each(|(_, v)| self.expr(v));
                self.expect(node, &OrdinaryType::RecordRef(*record), "record constructor");
                match self.program.records.get(record.raw() as usize) {
                    Some(schema) => {
                        if name != &schema.name || fields.len() != schema.fields.len() {
                            self.issue(format!("record {record} constructor differs from its checked schema"));
                        }
                        for ((field, value), (expected, ty)) in fields.iter().zip(&schema.fields) {
                            if field != expected {
                                self.issue(format!("record {record} field order/name differs: {field}, expected {expected}"));
                            }
                            self.expect(value, ty, "record field value");
                        }
                    }
                    None => self.issue(format!("unknown record layout {record}")),
                }
                let mut seen = BTreeSet::new();
                for (field, _) in fields {
                    if !seen.insert(field) {
                        self.issue(format!("duplicate record field {name}.{field}"));
                    }
                }
            }
            IrOp::Field { record, layout, field, name } => {
                self.expr(record);
                self.expect(record, &OrdinaryType::RecordRef(*layout), "field operand");
                match self.program.records.get(layout.raw() as usize).and_then(|r| r.fields.get(*field)) {
                    Some((expected, ty)) => {
                        if name != expected {
                            self.issue(format!("field index/name differs: {name}, expected {expected}"));
                        }
                        self.expect(node, ty, "field result");
                    }
                    None => self.issue(format!("unknown field #{field} of {layout}")),
                }
            }
        }
    }
}

/// Verify a typed IR program; an empty result means no issue was found.
pub fn verify_ir(program: &IrProgram) -> Vec<IrIssue> {
    let partial = partial_functions(program);
    let mut issues = Vec::new();
    let mut files = BTreeSet::new();
    if program.sources.is_empty() || program.sources.iter().any(|s| !files.insert(s.id)) {
        issues.push(IrIssue { function: program.module.clone(), message: "missing or duplicate source file identity".to_string() });
    }
    let mut symbols = BTreeSet::new();
    let mut declarations = BTreeSet::new();
    let mut declaration_symbols = BTreeSet::new();
    for origin in program.declarations.values() {
        if !valid_span(origin.span, program) || !origin.span.line_only()
            || origin.module.is_empty() || origin.theory.is_empty() || origin.name.is_empty()
            || !declaration_symbols.insert(origin.symbol())
        {
            issues.push(IrIssue { function: origin.symbol(), message: "invalid or duplicate checked declaration origin".to_string() });
        }
    }
    for (id, record) in program.records.iter().enumerate() {
        let origin = program.declarations.get(&record.declaration);
        if !declarations.insert(record.declaration) || origin.is_none_or(|o| {
            o.kind != SourceDeclarationKind::Record || o.symbol() != record.symbol || o.name != record.name || o.span != record.span
        }) {
            issues.push(IrIssue { function: record.symbol.clone(), message: "record declaration identity does not match its checked origin".to_string() });
        }
        if !valid_span(record.span, program) {
            issues.push(IrIssue { function: record.symbol.clone(), message: "invalid record source span or unknown file identity".to_string() });
        }
        if record.symbol.is_empty() || !symbols.insert(&record.symbol) {
            issues.push(IrIssue { function: record.symbol.clone(), message: "missing or duplicate record symbol".to_string() });
        }
        let mut fields = BTreeSet::new();
        for (name, ty) in &record.fields {
            if !fields.insert(name) || !(valid_type(ty, id) || (valid_type(ty, id + 1) && self_reference_guarded(ty, id, false))) {
                issues.push(IrIssue { function: record.symbol.clone(), message: format!("invalid field {name}: duplicate name or unresolved/forward/cyclic record identity") });
            }
        }
    }
    if program.global_types.len() != program.globals.len() {
        issues.push(IrIssue { function: program.module.clone(), message: "global types do not match globals".to_string() });
    }
    for (index, ty) in program.global_types.iter().enumerate() {
        if ty.as_ref().is_none_or(|t| !valid_type(t, program.records.len())) {
            issues.push(IrIssue { function: program.module.clone(), message: format!("untyped or unresolved global @g{index}") });
        }
    }
    for function in &program.functions {
        let mut verifier = Verifier { program, function, partial: &partial, defined: vec![false; function.slots.min(function.slot_types.len())], guards: BTreeSet::new(), issues: Vec::new() };
        match function.declaration {
            Some(id) => {
                let origin = program.declarations.get(&id);
                if !declarations.insert(id) || origin.is_none_or(|o| {
                    o.kind != SourceDeclarationKind::Function || o.symbol() != function.symbol
                        || format!("{}.{}", o.theory, o.name) != function.name
                        || o.span.file_id != function.body.span.file_id || o.span.start_line != function.line
                }) || function.is_lambda {
                    verifier.issue("function declaration identity does not match its checked origin");
                }
            }
            None if !function.symbol.is_empty() || !(function.is_lambda || function.name.contains(".<init")) => {
                verifier.issue("source function is missing its declaration identity");
            }
            None => {}
        }
        if function.slot_types.len() != function.slots {
            verifier.issue(format!("{} slot types for {} slots", function.slot_types.len(), function.slots));
        }
        if function.param_types.len() != function.params.len() {
            verifier.issue("parameter types do not match parameters");
        }
        if !function.is_lambda && function.captures != 0 {
            verifier.issue("non-lambda function has captured slots");
        }
        for (index, ty) in function.slot_types.iter().enumerate() {
            if ty.as_ref().is_none_or(|t| !valid_type(t, program.records.len())) {
                verifier.issue(format!("untyped or unresolved slot %{index}"));
            }
        }
        for ty in function.param_types.iter().chain(function.ret.iter()) {
            if !valid_type(ty, program.records.len()) {
                verifier.issue("unresolved or unknown record identity in signature");
            }
        }
        let entries = match function.captures.checked_add(function.params.len()) {
            Some(entries) => entries,
            None => {
                verifier.issue("captures and parameters overflow the frame size");
                usize::MAX
            }
        };
        if entries > function.slots {
            verifier.issue("captures and parameters exceed the frame");
        }
        for slot in 0..entries.min(verifier.defined.len()) {
            verifier.defined[slot] = true;
        }
        for (index, ty) in function.param_types.iter().enumerate() {
            let Some(slot) = function.captures.checked_add(index) else {
                verifier.issue("parameter slot index overflows");
                continue;
            };
            if function.slot_types.get(slot).cloned().flatten().as_ref() != Some(ty) {
                verifier.issue(format!("parameter slot %{slot} type differs from the signature"));
            }
        }
        verifier.expr(&function.body);
        if function.ret.is_none() || function.body.ty != function.ret {
            verifier.issue(format!("body type {} differs from the result type {}", show(&function.body.ty), show(&function.ret)));
        }
        issues.extend(verifier.issues);
    }
    if declarations.len() != program.declarations.len() {
        issues.push(IrIssue { function: program.module.clone(), message: "checked declaration origins do not match the executable program".to_string() });
    }
    let mut initialized = BTreeSet::new();
    for init in &program.init {
        let (IrInit::Global { expr, .. } | IrInit::Effect(expr)) = init;
        if !valid_span(expr.span, program) {
            issues.push(IrIssue { function: program.module.clone(), message: "invalid initializer source span or unknown file identity".to_string() });
        }
        match &expr.op {
            IrOp::Call { function, args } if args.is_empty() => {
                match program.functions.get(*function) {
                    Some(f) if f.name.contains(".<init") && !f.is_lambda && f.captures == 0 && f.params.is_empty() && f.param_types.is_empty() => {
                        if expr.ty.is_none() || expr.ty != f.ret {
                            issues.push(IrIssue { function: program.module.clone(), message: format!("initializer `{expr}` type differs from its callee") });
                        }
                    }
                    _ => issues.push(IrIssue { function: program.module.clone(), message: format!("initializer `{expr}` does not name a parameterless <init> function") }),
                }
            }
            _ => issues.push(IrIssue { function: program.module.clone(), message: format!("initializer `{expr}` is not a call of an <init> function") }),
        }
        if let IrInit::Global { index, expr } = init {
            if *index >= program.globals.len() || program.global_types.get(*index) != Some(&expr.ty) {
                issues.push(IrIssue { function: program.module.clone(), message: format!("global @g{index} type differs from its initializer") });
            }
            if !initialized.insert(*index) {
                issues.push(IrIssue { function: program.module.clone(), message: format!("global @g{index} initialized more than once") });
            }
        }
    }
    for index in 0..program.globals.len() {
        if !initialized.contains(&index) {
            issues.push(IrIssue { function: program.module.clone(), message: format!("global @g{index} has no initializer") });
        }
    }
    if let Some(main) = program.main {
        match program.functions.get(main) {
            Some(f) if f.params.is_empty() && f.captures == 0 && !f.is_lambda => {}
            _ => issues.push(IrIssue { function: program.module.clone(), message: format!("main f{main} is not a function without parameters") }),
        }
    }
    issues
}

/// Conservative partiality closure, independent of frontend function passports.
fn partial_functions(program: &IrProgram) -> Vec<bool> {
    fn scan(node: &IrExpr, known: &[bool]) -> bool {
        let callback_partial = |expr: &IrExpr| matches!(&expr.ty,Some(OrdinaryType::EffectFn(_,_,b)) if b.partial());
        let own = match &node.op {
            IrOp::Call { function,.. } => known.get(*function).copied().unwrap_or(true),
            IrOp::Apply { callee,.. } => callback_partial(callee),
            IrOp::Case(_,a,b) => callback_partial(a) || callback_partial(b),
            IrOp::Builtin { name,args } => crate::checker::builtin_effect(name).is_some_and(|(_,_,p)|p)
                || crate::callback::traversal_argument(name).and_then(|i|args.get(i)).is_some_and(callback_partial),
            _ => false,
        };
        if own { return true; }
        let mut result = false;
        crate::compile::for_each_child(node,&mut |c|result |= scan(c,known));
        result
    }
    let mut partial: Vec<bool> = program.functions.iter().map(|f|matches!(f.termination.as_str(),"tail_loop" | "callback_partial")).collect();
    loop {
        let next: Vec<bool> = program.functions.iter().enumerate().map(|(i,f)|partial[i] || scan(&f.body,&partial)).collect();
        if next == partial { return partial; }
        partial = next;
    }
}
