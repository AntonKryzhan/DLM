//! v0.102 — static ownership of `ByteBuf` resources (stage D, `docs/OWNERSHIP.md`).
//!
//! A `ByteBuf` value has exactly one owner: a parameter declared `ByteBuf` or a
//! `let` whose value is a buffer. An owner is either *borrowed* (read by
//! `buf_len`/`buf_get`, which leave it usable) or *moved* (passed to a
//! consuming builtin or a `ByteBuf` parameter, bound by `let`, or returned).
//! The pass checks, over every control path:
//! * no use after a move and no second move;
//! * an outer owner consumed on one branch of `if_then_else` only is released
//!   at the end of the other branch;
//! * lazily evaluated operands (`and`/`or`/`unwrap_or` right side) move nothing;
//! * function values capture no owner and take or return no buffer;
//! * a buffer never becomes an element, field, ordinary argument, theory-level
//!   value or statement part; a borrowed argument is a named owner (an
//!   unnamed buffer would be lost).
//!
//! The result is the release plan: owners still held at the end of their
//! scope (parameters at the end of the body, `let`s at the end of their body,
//! owners consumed only by the other branch of a conditional) are released
//! there by the compiled code, so every path releases every buffer once. In a call `f(b, g(b))` the move
//! of `b` into `f` happens when `f` is entered, after all arguments are
//! evaluated; borrows of `b` by sibling arguments are therefore allowed, but
//! a sibling that moves `b` is a double move. v0.103: a `&ByteBuf` parameter
//! borrows the caller's owner for the call (never moved or released by the
//! callee), and one call may not receive the same buffer both as a direct
//! borrowed and a moved argument (exclusive mutation).
use crate::ast::{Expr, ExprId, ExprKind, FnDecl};
use crate::checker::BYTE_BUFFER_BUILTINS;
use crate::diagnostics::{Diagnostic, DiagnosticKind, SourceSpan};
use crate::ordinary::parse_ordinary_type;
use std::collections::{BTreeMap, BTreeSet};

/// Ownership shape of a user function: for each parameter `None` (ordinary),
/// `Some(true)` (`ByteBuf`, moved by a call) or `Some(false)` (`&ByteBuf`,
/// borrowed for the call); and whether it returns a buffer.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FnShape {
    pub params: Vec<Option<bool>>,
    pub ret: bool,
    /// v0.109: the result is a linear resource (`File`).
    pub linear_ret: bool,
    /// v0.113: resource type of the result (`ByteBuf`, `TextBuf`, `File`), `""` otherwise.
    pub ret_kind: &'static str,
}

impl FnShape {
    pub fn of(decl: &FnDecl) -> Self {
        let is_buf = |ty: &str| parse_ordinary_type(ty).is_ok_and(|t| t.is_resource());
        Self {
            params: decl
                .params
                .iter()
                .map(|p| is_buf(&p.ty).then(|| !decl.borrowed.contains(&p.name)))
                .collect(),
            ret: is_buf(&decl.ret),
            linear_ret: parse_ordinary_type(&decl.ret).is_ok_and(|t| t.is_linear()),
            ret_kind: resource_kind(&decl.ret),
        }
    }
}

/// Owners released at the end of their scope.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ReleasePlan {
    /// `ByteBuf` parameters still owned when the body has produced its value.
    pub params: Vec<String>,
    /// `let` expressions whose buffer is still owned after their body.
    pub lets: BTreeSet<ExprId>,
    /// Branches of `if_then_else` after which the named owners, consumed on
    /// the other branch only, are released.
    pub branches: BTreeMap<ExprId, Vec<String>>,
}

fn error(line: usize, message: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::OwnershipError, Some(line), message)
        .with_help("a ByteBuf has one owner: borrow it with buf_len/buf_get, move it into buf_set/buf_push/buf_to_list/buf_free or a ByteBuf parameter, or return it (docs/OWNERSHIP.md)")
}

/// Rejects `ByteBuf` nested in a declared type (element, component, record
/// field or function type part); `what` names the position.
pub fn check_declared_type(
    ty: &str,
    what: &str,
    line: usize,
    allow_top: bool,
) -> Result<(), Diagnostic> {
    let Ok(parsed) = parse_ordinary_type(ty) else {
        return Ok(());
    };
    if parsed.nests_byte_buf() || (!allow_top && parsed.is_resource()) {
        return Err(error(line, format!("{what} has type `{ty}`; a ByteBuf cannot be stored in a container, record, function value or theory-level value")));
    }
    Ok(())
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Mode {
    /// An ordinary position: a buffer here is an error.
    Plain,
    /// A position that takes ownership (let value, result, consuming argument).
    Own,
    /// A borrowing argument: a named owner, left usable.
    Borrow,
}

#[derive(Clone)]
struct Owner {
    name: String,
    /// Where the owner was moved (physical position of the moving use).
    moved: Option<SourceSpan>,
    pending: bool,
    /// A `&ByteBuf` parameter: borrowed from the caller, never moved or released.
    borrowed: bool,
    /// v0.109: a linear owner (`File`) is never released implicitly.
    linear: bool,
    /// v0.113: its resource type, used in diagnostics.
    kind: &'static str,
}

/// v0.113: name of the resource type `ty` (`""` for ordinary types).
pub fn resource_kind(ty: &str) -> &'static str {
    match parse_ordinary_type(ty) {
        Ok(crate::ordinary::OrdinaryType::ByteBuf) => "ByteBuf",
        Ok(crate::ordinary::OrdinaryType::TextBuf) => "TextBuf",
        Ok(crate::ordinary::OrdinaryType::File) => "File",
        _ => "",
    }
}

/// v0.109: builtins that produce a linear resource.
const LINEAR_BUILTINS: &[&str] = &["file_open"];

fn unclosed(line: usize, name: &str) -> Diagnostic {
    Diagnostic::error(
        DiagnosticKind::OwnershipError,
        Some(line),
        format!("File '{name}' is not closed: close it with file_close, return it or move it into a File parameter on every path"),
    )
    .with_help("a File is linear: it is never released implicitly, so its close errors are always observed (docs/FILES.md)")
}

struct Walker<'a> {
    shape: &'a dyn Fn(&str) -> Option<FnShape>,
    /// Innermost last: a name and its owner index, `None` for a non-owner
    /// binding that shadows an owner.
    env: Vec<(String, Option<usize>)>,
    owners: Vec<Owner>,
    /// Owners below this index belong to an enclosing function (capture).
    floor: usize,
    plan: ReleasePlan,
}

impl Walker<'_> {
    /// v0.113: resource type of the value of `expr` (`ByteBuf` when not determined).
    fn value_kind(&self, expr: &Expr, scope: &mut Vec<(String, &'static str)>) -> &'static str {
        match &expr.kind {
            ExprKind::Ident(n) => match scope.iter().rev().find(|(m, _)| m == n) {
                Some((_, k)) => k,
                None => self.lookup(n).map_or("ByteBuf", |k| self.owners[k].kind),
            },
            ExprKind::Let { name, value, body } => {
                let k = self.value_kind(value, scope);
                scope.push((name.clone(), k));
                let r = self.value_kind(body, scope);
                scope.pop();
                r
            }
            ExprKind::Call { name, args } => match (name.as_str(), args.as_slice()) {
                ("if_then_else", [_, a, _]) => self.value_kind(a, scope),
                ("tbuf_new" | "tbuf_push", _) => "TextBuf",
                ("file_open", _) => "File",
                _ => (self.shape)(name).map(|s| s.ret_kind).filter(|k| !k.is_empty()).unwrap_or("ByteBuf"),
            },
            _ => "ByteBuf",
        }
    }

    /// v0.109: `expr` yields a linear resource; `scope` holds inner `let` names.
    fn linear_value(&self, expr: &Expr, scope: &mut Vec<(String, bool)>) -> bool {
        match &expr.kind {
            ExprKind::Ident(n) => match scope.iter().rev().find(|(m, _)| m == n) {
                Some((_, l)) => *l,
                None => self.lookup(n).is_some_and(|k| self.owners[k].linear),
            },
            ExprKind::Let { name, value, body } => {
                let l = self.linear_value(value, scope);
                scope.push((name.clone(), l));
                let r = self.linear_value(body, scope);
                scope.pop();
                r
            }
            ExprKind::Call { name, args } => match (name.as_str(), args.as_slice()) {
                ("if_then_else", [_, a, b]) => self.linear_value(a, scope) || self.linear_value(b, scope),
                _ if LINEAR_BUILTINS.contains(&name.as_str()) => true,
                _ => (self.shape)(name).is_some_and(|s| s.linear_ret),
            },
            _ => false,
        }
    }

    fn lookup(&self, name: &str) -> Option<usize> {
        self.env
            .iter()
            .rev()
            .find(|(n, _)| n == name)
            .and_then(|(_, k)| *k)
    }

    fn take(&mut self, k: usize, at: &Expr, pending: bool) -> Result<(), Diagnostic> {
        let owner = &mut self.owners[k];
        if owner.borrowed {
            return Err(error(
                at.line,
                format!(
                    "{0} '{1}' is borrowed by this function (&{0}) and cannot be moved",
                    owner.kind, owner.name
                ),
            )
            .at_expr(at.span));
        }
        if let Some(moved) = owner.moved {
            return Err(error(
                at.line,
                format!(
                    "{} '{}' is used after it was moved at line {}",
                    owner.kind, owner.name, moved.start_line
                ),
            )
            .at_expr(at.span));
        }
        if owner.pending {
            return Err(error(
                at.line,
                format!("{} '{}' is moved twice in one call", owner.kind, owner.name),
            )
            .at_expr(at.span));
        }
        if pending {
            owner.pending = true;
        } else {
            owner.moved = Some(at.span);
        }
        Ok(())
    }

    fn ident(&mut self, name: &str, mode: Mode, at: &Expr) -> Result<bool, Diagnostic> {
        let Some(k) = self.lookup(name) else {
            return Ok(false);
        };
        let fail = |message: String| Err(error(at.line, message).at_expr(at.span));
        let kind = self.owners[k].kind;
        if k < self.floor {
            return fail(format!(
                "{kind} '{name}' cannot be captured by a function value"
            ));
        }
        match mode {
            Mode::Plain => match self.owners[k].moved {
                Some(moved) => fail(format!(
                    "{kind} '{name}' is used after it was moved at line {}",
                    moved.start_line
                )),
                None => fail(format!("{kind} '{name}' is used as an ordinary value")),
            },
            Mode::Borrow => match self.owners[k].moved {
                Some(moved) => fail(format!(
                    "{kind} '{name}' is borrowed after it was moved at line {}",
                    moved.start_line
                )),
                None => Ok(true),
            },
            Mode::Own => self.take(k, at, false).map(|()| true),
        }
    }

    fn moved_state(&self, below: usize) -> Vec<(Option<SourceSpan>, bool)> {
        self.owners[..below]
            .iter()
            .map(|o| (o.moved, o.pending))
            .collect()
    }

    /// `true` when `expr` produces a buffer.
    fn walk(&mut self, expr: &Expr, mode: Mode) -> Result<bool, Diagnostic> {
        self.walk_at(expr, mode).map_err(|d| d.at_expr(expr.span))
    }

    fn walk_at(&mut self, expr: &Expr, mode: Mode) -> Result<bool, Diagnostic> {
        let line = expr.line;
        let is_buf = match &expr.kind {
            ExprKind::Ident(name) => return self.ident(name, mode, expr),
            ExprKind::IntLiteral(_)
            | ExprKind::TextLiteral(_)
            | ExprKind::QualifiedIdent { .. } => false,
            ExprKind::Add { lhs, rhs }
            | ExprKind::CompareGt { lhs, rhs }
            | ExprKind::Power {
                base: lhs,
                exp: rhs,
            } => {
                self.walk(lhs, Mode::Plain)?;
                self.walk(rhs, Mode::Plain)?;
                false
            }
            ExprKind::Lambda { params, body } => {
                for p in params {
                    check_declared_type(
                        &p.ty,
                        &format!("function value parameter '{}'", p.name),
                        line,
                        false,
                    )?;
                }
                let floor = std::mem::replace(&mut self.floor, self.owners.len());
                let depth = self.env.len();
                self.env
                    .extend(params.iter().map(|p| (p.name.clone(), None)));
                let result = self.walk(body, Mode::Plain);
                self.env.truncate(depth);
                self.floor = floor;
                result?;
                false
            }
            ExprKind::Let { name, value, body } => {
                let linear = self.linear_value(value, &mut Vec::new());
                let kind = if linear { "File" } else { self.value_kind(value, &mut Vec::new()) };
                let owned = self.walk(value, Mode::Own)?;
                let k = owned.then(|| {
                    self.owners.push(Owner {
                        name: name.clone(),
                        moved: None,
                        pending: false,
                        borrowed: false,
                        linear,
                        kind,
                    });
                    self.owners.len() - 1
                });
                self.env.push((name.clone(), k));
                let result = self.walk(
                    body,
                    if mode == Mode::Borrow {
                        Mode::Own
                    } else {
                        mode
                    },
                );
                self.env.pop();
                if let Some(k) = k {
                    let owner = self.owners.pop().expect("let owner");
                    debug_assert_eq!(self.owners.len(), k);
                    if owner.moved.is_none() {
                        if owner.linear && result.is_ok() {
                            return Err(unclosed(line, &owner.name));
                        }
                        self.plan.lets.insert(expr.id);
                    }
                }
                result?
            }
            ExprKind::Call { name, args } => self.call(name, args, mode, expr)?,
        };
        if is_buf && mode == Mode::Plain {
            return Err(error(line, "a ByteBuf value is used as an ordinary value; bind it with let, move it into a consuming call or return it"));
        }
        if is_buf && mode == Mode::Borrow {
            return Err(error(
                line,
                "a borrowed ByteBuf argument must be a named owner; bind the buffer with let first",
            ));
        }
        Ok(is_buf)
    }

    fn lazy(&mut self, expr: &Expr, op: &str) -> Result<(), Diagnostic> {
        let below = self.owners.len();
        let before = self.moved_state(below);
        self.walk(expr, Mode::Plain)?;
        if let Some(k) = (0..below).find(|&k| self.moved_state(below)[k] != before[k]) {
            return Err(error(
                expr.line,
                format!(
                    "{} '{}' is moved inside the lazily evaluated operand of {op}",
                    self.owners[k].kind, self.owners[k].name
                ),
            ));
        }
        Ok(())
    }

    fn call(
        &mut self,
        name: &str,
        args: &[Expr],
        mode: Mode,
        expr: &Expr,
    ) -> Result<bool, Diagnostic> {
        match (name, args) {
            ("if_then_else", [c, a, b]) => {
                self.walk(c, Mode::Plain)?;
                let below = self.owners.len();
                let start = self.owners.clone();
                let branch = if mode == Mode::Borrow {
                    Mode::Own
                } else {
                    mode
                };
                let then_buf = self.walk(a, branch)?;
                let after_then = std::mem::replace(&mut self.owners, start);
                let else_buf = self.walk(b, branch)?;
                // An owner consumed on one branch only is released at the end
                // of the other branch, so it is consumed on every path.
                for (owner, then_owner) in self.owners[..below].iter_mut().zip(&after_then) {
                    if owner.linear && then_owner.moved.is_some() != owner.moved.is_some() {
                        return Err(Diagnostic::error(
                            DiagnosticKind::OwnershipError,
                            Some(expr.line),
                            format!("File '{}' is consumed on one branch only; close or move it on both branches", owner.name),
                        )
                        .with_help("a File is linear: it is never released implicitly (docs/FILES.md)"));
                    }
                    match (then_owner.moved, owner.moved) {
                        (Some(_), None) => {
                            self.plan
                                .branches
                                .entry(b.id)
                                .or_default()
                                .push(owner.name.clone());
                            owner.moved = Some(b.span);
                        }
                        (None, Some(_)) => self
                            .plan
                            .branches
                            .entry(a.id)
                            .or_default()
                            .push(owner.name.clone()),
                        _ => {}
                    }
                }
                return Ok(then_buf || else_buf);
            }
            ("and" | "or" | "unwrap_or", [a, b]) => {
                self.walk(a, Mode::Plain)?;
                self.lazy(b, name)?;
                return Ok(false);
            }
            _ => {}
        }
        let positions: Vec<Option<bool>> =
            if let Some((_, p)) = BYTE_BUFFER_BUILTINS.iter().find(|(n, _)| *n == name) {
                p.to_vec()
            } else if let Some(shape) = (self.shape)(name) {
                shape.params.clone()
            } else {
                vec![None; args.len()]
            };
        let mut pending = Vec::new();
        let mut borrows = Vec::new();
        for (i, arg) in args.iter().enumerate() {
            if crate::ordinary::metadata_argument(name, i) { continue; }
            match positions.get(i).copied().flatten() {
                Some(false) => {
                    if let ExprKind::Ident(n) = &arg.kind {
                        if self.ident(n, Mode::Borrow, arg)? {
                            borrows.push((self.lookup(n).expect("owner"), arg));
                            continue;
                        }
                    }
                    self.walk(arg, Mode::Borrow)?;
                }
                Some(true) => {
                    if let ExprKind::Ident(n) = &arg.kind {
                        if let Some(k) = self.lookup(n) {
                            if k < self.floor {
                                return Err(error(
                                    arg.line,
                                    format!("{} '{n}' cannot be captured by a function value", self.owners[k].kind),
                                )
                                .at_expr(arg.span));
                            }
                            self.take(k, arg, true)?;
                            pending.push(k);
                            continue;
                        }
                    }
                    self.walk(arg, Mode::Own)?;
                }
                None => {
                    self.walk(arg, Mode::Plain)?;
                }
            }
        }
        // Exclusive mutation: the callee may not receive one buffer both as a
        // moved (mutable) and a borrowed argument.
        if let Some((k, arg)) = borrows.iter().find(|(k, _)| pending.contains(k)) {
            return Err(error(
                arg.line,
                format!(
                    "{} '{}' is borrowed and moved by the same call",
                    self.owners[*k].kind, self.owners[*k].name
                ),
            )
            .at_expr(arg.span));
        }
        for k in pending {
            self.owners[k].pending = false;
            self.owners[k].moved = Some(expr.span);
        }
        Ok(match name {
            "buf_new" | "buf_from_list" | "buf_set" | "buf_push" | "buf_copy" | "buf_slice"
            | "buf_append" | "buf_from_text" | "tbuf_new" | "tbuf_push" | "file_open" | "file_read_bytes" => true,
            _ if BYTE_BUFFER_BUILTINS.iter().any(|(n, _)| *n == name) => false,
            _ => (self.shape)(name).is_some_and(|s| s.ret),
        })
    }
}

/// Checks a function body; `shape` gives the shapes of callable functions.
pub fn check_function(
    decl: &FnDecl,
    shape: &dyn Fn(&str) -> Option<FnShape>,
) -> Result<ReleasePlan, Diagnostic> {
    for p in &decl.params {
        check_declared_type(
            &p.ty,
            &format!("parameter '{}' of fn '{}'", p.name, decl.name),
            decl.line,
            true,
        )?;
    }
    check_declared_type(
        &decl.ret,
        &format!("result of fn '{}'", decl.name),
        decl.line,
        true,
    )?;
    let own = FnShape::of(decl);
    let mut w = Walker {
        shape,
        env: Vec::new(),
        owners: Vec::new(),
        floor: 0,
        plan: ReleasePlan::default(),
    };
    for (p, mode) in decl.params.iter().zip(&own.params) {
        let linear = parse_ordinary_type(&p.ty).is_ok_and(|t| t.is_linear());
        let kind = match resource_kind(&p.ty) {
            "" => "ByteBuf",
            k => k,
        };
        let k = mode.map(|moved| {
            w.owners.push(Owner {
                name: p.name.clone(),
                moved: None,
                pending: false,
                borrowed: !moved,
                linear,
                kind,
            });
            w.owners.len() - 1
        });
        w.env.push((p.name.clone(), k));
    }
    w.walk(&decl.body, if own.ret { Mode::Own } else { Mode::Plain })?;
    for (p, mode) in decl.params.iter().zip(&own.params) {
        if *mode == Some(true) {
            let k = w.lookup(&p.name).expect("parameter owner");
            if w.owners[k].moved.is_none() {
                if w.owners[k].linear {
                    return Err(unclosed(decl.line, &p.name));
                }
                w.plan.params.push(p.name.clone());
            }
        }
    }
    Ok(w.plan)
}

/// Checks theory-level code (`let` values and evaluated expressions): it may
/// create and consume buffers locally but never yields one.
pub fn check_theory_expr(
    expr: &Expr,
    shape: &dyn Fn(&str) -> Option<FnShape>,
) -> Result<ReleasePlan, Diagnostic> {
    let mut w = Walker {
        shape,
        env: Vec::new(),
        owners: Vec::new(),
        floor: 0,
        plan: ReleasePlan::default(),
    };
    if w.walk(expr, Mode::Own)? {
        return Err(error(expr.line, "a ByteBuf cannot be a theory-level value; consume it inside the expression (for example with buf_free)"));
    }
    Ok(w.plan)
}

/// Statements are evaluated symbolically and by finite exhaustion; byte
/// buffers are runtime resources and do not occur in them.
pub fn check_statement_expr(
    expr: &Expr,
    shape: &dyn Fn(&str) -> Option<FnShape>,
) -> Result<(), Diagnostic> {
    fn find(expr: &Expr, shape: &dyn Fn(&str) -> Option<FnShape>) -> Option<usize> {
        match &expr.kind {
            ExprKind::Call { name, args } => {
                if BYTE_BUFFER_BUILTINS.iter().any(|(n, _)| n == name)
                    || shape(name).is_some_and(|s| s.ret || s.params.iter().any(Option::is_some))
                {
                    return Some(expr.line);
                }
                args.iter().find_map(|a| find(a, shape))
            }
            ExprKind::Add { lhs, rhs }
            | ExprKind::CompareGt { lhs, rhs }
            | ExprKind::Power {
                base: lhs,
                exp: rhs,
            } => find(lhs, shape).or_else(|| find(rhs, shape)),
            ExprKind::Lambda { body, .. } => find(body, shape),
            ExprKind::Let { value, body, .. } => find(value, shape).or_else(|| find(body, shape)),
            _ => None,
        }
    }
    match find(expr, shape) {
        Some(line) => Err(error(
            line,
            "byte buffers are runtime resources and cannot occur in statements",
        )),
        None => Ok(()),
    }
}
