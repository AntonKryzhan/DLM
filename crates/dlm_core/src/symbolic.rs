//! v0.77 — Symbolic proof kernel for unbounded Nat statements.
//!
//! The kernel decides equalities and simple comparisons of Nat-valued terms
//! by normalization. It is used by `theorem ... by normalize` and
//! `theorem ... by induction n`.
//!
//! # Term normal form
//!
//! Nat terms normalize to polynomials with integer coefficients over *atoms*.
//! An atom is either a statement variable or the canonical key of a term the
//! kernel does not reduce: a call that is not unfolded, an undecided
//! `if_then_else`, a `pred`/`monus` that cannot be simplified, and so on.
//!
//! # Soundness
//!
//! Every rule replaces a term by one with the same value under every
//! assignment of Nat values to the variables:
//!
//! - `+`, `mul`, `^k` are commutative-semiring operations; two polynomials over
//!   independent indeterminates denote the same function on Nat iff they have
//!   the same coefficients.
//! - An atom key is built only from normalized arguments, and all functions are
//!   pure and deterministic, so equal keys denote equal values.
//! - Every atom denotes a natural number, so a polynomial with nonnegative
//!   coefficients is `>= 0`, and `>= c` when its constant term is `c`. `pred`,
//!   `monus`, `nat_eq`, `>` and `if_then_else` are simplified only when this
//!   bound decides them.
//! - Unfolding a declared function replaces a call by its body with the
//!   arguments substituted. This is the defining equation of a pure total
//!   function. Applying a lambda is beta-reduction.
//! - A rewrite rule comes from a lemma (a proven or admitted theorem) or from
//!   the induction hypothesis. It replaces one side of an equation by the other.
//! - The linear-combination step accepts a goal `G` when `lhs(G) - rhs(G)` is
//!   a rational multiple of `lhs(H) - rhs(H)` for the hypothesis `H`, which is
//!   zero by `H`.
//!
//! The unfolding policy and the step budget affect only completeness and
//! termination, never soundness. A goal the kernel cannot decide is reported
//! with its residual normal form.

use std::cell::{Cell, RefCell};
use std::collections::BTreeMap;
use std::fmt;

use crate::ast::{Expr, ExprKind, FnDecl, FnParam, InductionOptions};

/// Maximum number of unfold / rewrite / beta steps per proof obligation.
pub const SYMBOLIC_STEP_BUDGET: usize = 20_000;

/// v0.79: maximum nesting of normalization. Guards the native stack against
/// rewrite chains that keep re-entering the kernel.
pub const MAX_SYMBOLIC_DEPTH: usize = 384;

/// v0.79: stack of the thread that runs whole proofs
/// ([`prove_by_normalization`], [`prove_by_induction_with`]); virtual,
/// committed lazily. It covers [`MAX_SYMBOLIC_DEPTH`] with a wide margin even
/// in debug builds.
pub const KERNEL_STACK_BYTES: usize = 256 << 20;

/// Run a whole proof on a thread with [`KERNEL_STACK_BYTES`] of stack, so the
/// depth guard, not the caller's stack size, bounds normalization.
fn on_kernel_thread<T: Send>(f: impl FnOnce() -> T + Send) -> T {
    std::thread::scope(|scope| {
        let handle = std::thread::Builder::new()
            .name("dlm-kernel".to_string())
            .stack_size(KERNEL_STACK_BYTES)
            .spawn_scoped(scope, f)
            .expect("cannot start the symbolic kernel thread");
        match handle.join() {
            Ok(result) => result,
            Err(panic) => std::panic::resume_unwind(panic),
        }
    })
}

/// v0.79: largest `step` of `by induction n step s`.
pub const MAX_INDUCTION_STEP: u32 = 4;

/// v0.79: largest number of Bool variables split by cases in one proof.
pub const MAX_BOOL_SPLIT_VARIABLES: usize = 6;
const MAX_MONOMIALS: usize = 4_096;
const MAX_POWER: u128 = 32;

/// Product of atoms with exponents, sorted by atom key.
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Default)]
pub struct Monomial(Vec<(String, u32)>);

impl Monomial {
    fn atom(key: &str) -> Self {
        Monomial(vec![(key.to_string(), 1)])
    }

    fn mul(&self, other: &Monomial) -> Monomial {
        let mut map: BTreeMap<String, u32> = BTreeMap::new();
        for (key, exp) in self.0.iter().chain(other.0.iter()) {
            *map.entry(key.clone()).or_default() += exp;
        }
        Monomial(map.into_iter().collect())
    }

    fn render(&self) -> String {
        self.0
            .iter()
            .map(|(key, exp)| if *exp == 1 { key.clone() } else { format!("{key}^{exp}") })
            .collect::<Vec<_>>()
            .join("*")
    }
}

/// Polynomial with `i128` coefficients over atoms. Zero coefficients are never stored.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Poly(BTreeMap<Monomial, i128>);

impl Poly {
    pub fn constant(value: i128) -> Self {
        let mut poly = Poly::default();
        if value != 0 {
            poly.0.insert(Monomial::default(), value);
        }
        poly
    }

    pub fn atom(key: &str) -> Self {
        let mut poly = Poly::default();
        poly.0.insert(Monomial::atom(key), 1);
        poly
    }

    pub fn is_zero(&self) -> bool {
        self.0.is_empty()
    }

    pub fn constant_term(&self) -> i128 {
        self.0.get(&Monomial::default()).copied().unwrap_or(0)
    }

    pub fn as_constant(&self) -> Option<i128> {
        match self.0.len() {
            0 => Some(0),
            1 => self.0.get(&Monomial::default()).copied(),
            _ => None,
        }
    }

    fn non_constant_coefficients(&self) -> impl Iterator<Item = i128> + '_ {
        self.0.iter().filter(|(m, _)| !m.0.is_empty()).map(|(_, c)| *c)
    }

    /// Exactly `1 * atom`.
    pub fn single_atom(&self) -> Option<&str> {
        if self.0.len() != 1 {
            return None;
        }
        let (monomial, coefficient) = self.0.iter().next()?;
        match (monomial.0.as_slice(), coefficient) {
            ([(key, 1)], 1) => Some(key.as_str()),
            _ => None,
        }
    }

    fn mentions(&self, key: &str) -> bool {
        self.0.keys().any(|m| m.0.iter().any(|(k, _)| k == key || k.contains(key)))
    }

    fn add(&self, other: &Poly) -> Result<Poly, KernelError> {
        let mut out = self.0.clone();
        for (monomial, coefficient) in &other.0 {
            let entry = out.entry(monomial.clone()).or_insert(0);
            *entry = entry.checked_add(*coefficient).ok_or(KernelError::Overflow)?;
            if *entry == 0 {
                out.remove(monomial);
            }
        }
        Ok(Poly(out))
    }

    fn scale(&self, factor: i128) -> Result<Poly, KernelError> {
        let mut out = BTreeMap::new();
        if factor == 0 {
            return Ok(Poly(out));
        }
        for (monomial, coefficient) in &self.0 {
            out.insert(monomial.clone(), coefficient.checked_mul(factor).ok_or(KernelError::Overflow)?);
        }
        Ok(Poly(out))
    }

    fn sub(&self, other: &Poly) -> Result<Poly, KernelError> {
        self.add(&other.scale(-1)?)
    }

    fn mul(&self, other: &Poly) -> Result<Poly, KernelError> {
        let mut out: BTreeMap<Monomial, i128> = BTreeMap::new();
        for (ma, ca) in &self.0 {
            for (mb, cb) in &other.0 {
                let product = ca.checked_mul(*cb).ok_or(KernelError::Overflow)?;
                let entry = out.entry(ma.mul(mb)).or_insert(0);
                *entry = entry.checked_add(product).ok_or(KernelError::Overflow)?;
            }
        }
        out.retain(|_, c| *c != 0);
        if out.len() > MAX_MONOMIALS {
            return Err(KernelError::TooLarge);
        }
        Ok(Poly(out))
    }

    fn pow(&self, exponent: u128) -> Result<Poly, KernelError> {
        let mut out = Poly::constant(1);
        for _ in 0..exponent {
            out = out.mul(self)?;
        }
        Ok(out)
    }

    /// `self >= 1` for every assignment.
    fn surely_positive(&self) -> bool {
        self.constant_term() >= 1 && self.non_constant_coefficients().all(|c| c >= 0)
    }

    /// `self <= 0` for every assignment.
    fn surely_nonpositive(&self) -> bool {
        self.constant_term() <= 0 && self.non_constant_coefficients().all(|c| c <= 0)
    }

    pub fn render(&self) -> String {
        if self.0.is_empty() {
            return "0".to_string();
        }
        let mut parts = Vec::new();
        for (monomial, coefficient) in &self.0 {
            let part = match (monomial.0.is_empty(), *coefficient) {
                (true, c) => c.to_string(),
                (false, 1) => monomial.render(),
                (false, -1) => format!("-{}", monomial.render()),
                (false, c) => format!("{c}*{}", monomial.render()),
            };
            parts.push(part);
        }
        parts.join(" + ").replace("+ -", "- ")
    }
}

impl fmt::Display for Poly {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.render())
    }
}

/// Symbolic value of a term.
#[derive(Debug, Clone)]
pub enum Sym {
    Nat(Poly),
    /// v0.85: an Int value. Its atoms carry no sign assumption: no positivity
    /// reasoning is ever applied to an Int polynomial.
    Int(Poly),
    Bool(bool),
    /// A named declared function used as a value.
    FnRef(String),
    /// A lambda with its captured symbolic environment.
    Closure { key: String, params: Vec<FnParam>, body: Expr, env: BTreeMap<String, Sym> },
    /// v0.78: a list `items ++ rest`, where `rest` is the key of an unknown
    /// list (a variable or an irreducible list term) or `None` for the end.
    List { items: Vec<Sym>, rest: Option<String> },
    /// v0.78: `some(v)`.
    OptSome(Box<Sym>),
    /// v0.78: `none`.
    OptNone,
    /// Any other term, identified by its canonical key.
    Opaque(String),
}

impl Sym {
    pub fn key(&self) -> String {
        match self {
            Sym::List { items, rest } => {
                let items = items.iter().map(Sym::key).collect::<Vec<_>>().join(",");
                match rest {
                    Some(rest) if items.is_empty() => format!("[|{rest}]"),
                    Some(rest) => format!("[{items}|{rest}]"),
                    None => format!("[{items}]"),
                }
            }
            Sym::OptSome(inner) => format!("some({})", inner.key()),
            Sym::OptNone => "none".to_string(),
            Sym::Nat(poly) => poly.render(),
            Sym::Int(poly) => format!("int({})", poly.render()),
            Sym::Bool(value) => value.to_string(),
            Sym::FnRef(name) => format!("fn:{name}"),
            Sym::Closure { key, .. } => key.clone(),
            Sym::Opaque(key) => key.clone(),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum KernelError {
    Overflow,
    TooLarge,
    Budget,
    /// v0.79: normalization nested deeper than [`MAX_SYMBOLIC_DEPTH`].
    Depth,
    Unsupported(String),
}

impl fmt::Display for KernelError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            KernelError::Overflow => write!(f, "coefficient overflow"),
            KernelError::TooLarge => write!(f, "normal form too large"),
            KernelError::Budget => write!(f, "step budget of {SYMBOLIC_STEP_BUDGET} exhausted"),
            KernelError::Depth => write!(f, "normalization nested deeper than {MAX_SYMBOLIC_DEPTH}"),
            KernelError::Unsupported(what) => write!(f, "unsupported: {what}"),
        }
    }
}

/// An equational lemma usable as a rewrite rule: `forall vars. lhs = rhs`,
/// oriented so that `lhs` is a function call.
#[derive(Debug, Clone)]
pub struct LemmaRule {
    pub name: String,
    pub vars: Vec<String>,
    pub lhs: Expr,
    pub rhs: Expr,
}

impl LemmaRule {
    /// Build a rule from `nat_eq(a, b)` with universally quantified `vars`.
    /// The side that is a call of a declared function becomes the pattern.
    pub fn from_equation(name: &str, vars: &[String], body: &Expr, functions: &BTreeMap<String, FnDecl>) -> Result<Self, String> {
        let (a, b) = equation_sides(body)
            .ok_or_else(|| format!("lemma '{name}' is not an equation nat_eq(lhs, rhs)"))?;
        let is_call = |e: &Expr| matches!(&e.kind, ExprKind::Call { name, .. } if functions.contains_key(name));
        let (lhs, rhs) = if is_call(a) {
            (a.clone(), b.clone())
        } else if is_call(b) {
            (b.clone(), a.clone())
        } else {
            return Err(format!("lemma '{name}' cannot be used as a rewrite rule: neither side is a call of a declared fn"));
        };
        Ok(LemmaRule { name: name.to_string(), vars: vars.to_vec(), lhs, rhs })
    }
}

/// `nat_eq(a, b)` → `(a, b)`.
pub fn equation_sides(expr: &Expr) -> Option<(&Expr, &Expr)> {
    match &expr.kind {
        ExprKind::Call { name, args } if name == "nat_eq" && args.len() == 2 => Some((&args[0], &args[1])),
        _ => None,
    }
}

/// Normalization engine for one proof obligation.
pub struct Kernel<'a> {
    functions: &'a BTreeMap<String, FnDecl>,
    lemmas: &'a [LemmaRule],
    ground: RefCell<BTreeMap<String, Poly>>,
    budget: Cell<usize>,
    trace: RefCell<Vec<String>>,
    /// v0.79: hypotheses quantified over generalized variables, with the
    /// bindings of the fixed variables (`by induction n generalizing m`).
    local_rules: RefCell<Vec<(LemmaRule, BTreeMap<String, Sym>)>>,
    /// v0.79: rewrites in progress (rule name + argument keys); a rewrite is
    /// not re-entered for the same call, which cuts rewrite cycles.
    active_rewrites: RefCell<Vec<String>>,
    depth: Cell<usize>,
    /// v0.79: irreducible Bool terms known to be true (hypotheses).
    truths: RefCell<Vec<String>>,
    /// v0.85: when the theory or the goal involves Int, an irreducible term of
    /// unknown type (`Opaque`) is not assumed to be a natural number in
    /// arithmetic (it might be an Int, which can be negative).
    strict_opaque: Cell<bool>,
}

/// v0.85: `expr` uses Int (builtins, operators or Int-typed lambda parameters).
pub fn expr_mentions_int(expr: &Expr) -> bool {
    match &expr.kind {
        ExprKind::Call { name, args } => {
            crate::checker::INT_BUILTINS.contains(&name.as_str()) || args.iter().any(expr_mentions_int)
        }
        ExprKind::Add { lhs, rhs } | ExprKind::CompareGt { lhs, rhs } => expr_mentions_int(lhs) || expr_mentions_int(rhs),
        ExprKind::Power { base, exp } => expr_mentions_int(base) || expr_mentions_int(exp),
        ExprKind::Lambda { params, body } => params.iter().any(|p| p.ty.contains("Int")) || expr_mentions_int(body),
        ExprKind::Let { value, body, .. } => expr_mentions_int(value) || expr_mentions_int(body),
        ExprKind::IntLiteral(_) | ExprKind::TextLiteral(_) | ExprKind::Ident(_) | ExprKind::QualifiedIdent { .. } => false,
    }
}

/// v0.85: a declaration whose signature or body involves Int.
fn decl_mentions_int(decl: &FnDecl) -> bool {
    decl.ret.contains("Int") || decl.params.iter().any(|p| p.ty.contains("Int")) || expr_mentions_int(&decl.body)
}

impl<'a> Kernel<'a> {
    pub fn new(functions: &'a BTreeMap<String, FnDecl>, lemmas: &'a [LemmaRule]) -> Self {
        Kernel {
            functions,
            lemmas,
            ground: RefCell::new(BTreeMap::new()),
            budget: Cell::new(SYMBOLIC_STEP_BUDGET),
            trace: RefCell::new(Vec::new()),
            local_rules: RefCell::new(Vec::new()),
            active_rewrites: RefCell::new(Vec::new()),
            depth: Cell::new(0),
            truths: RefCell::new(Vec::new()),
            strict_opaque: Cell::new(
                functions.values().any(decl_mentions_int) || lemmas.iter().any(|l| expr_mentions_int(&l.lhs) || expr_mentions_int(&l.rhs)),
            ),
        }
    }

    /// v0.85: see `strict_opaque`.
    pub fn set_strict_opaque(&self) {
        self.strict_opaque.set(true);
    }

    fn as_int(&self, sym: Sym) -> Result<Poly, KernelError> {
        match sym {
            Sym::Int(poly) => Ok(poly),
            Sym::Opaque(key) => Ok(Poly::atom(&key)),
            other => Err(KernelError::Unsupported(format!("non-Int term `{}` in Int position", other.key()))),
        }
    }

    /// v0.79: the irreducible Bool term `key` is true (an induction hypothesis).
    pub fn add_truth(&self, key: &str) {
        self.truths.borrow_mut().push(key.to_string());
    }

    /// v0.79: install a hypothesis `forall vars. lhs = rhs` where the other
    /// free variables are bound by `fixed`.
    pub fn add_local_rule(&self, rule: LemmaRule, fixed: BTreeMap<String, Sym>) {
        self.local_rules.borrow_mut().push((rule, fixed));
    }

    /// Ground rewrite `atom -> poly` (used for the induction hypothesis).
    pub fn add_ground_rule(&self, atom: &str, replacement: Poly) {
        self.ground.borrow_mut().insert(atom.to_string(), replacement);
    }

    pub fn trace(&self) -> Vec<String> {
        self.trace.borrow().clone()
    }

    pub fn steps_used(&self) -> usize {
        SYMBOLIC_STEP_BUDGET - self.budget.get()
    }

    fn spend(&self, event: impl FnOnce() -> String) -> Result<(), KernelError> {
        let left = self.budget.get();
        if left == 0 {
            return Err(KernelError::Budget);
        }
        self.budget.set(left - 1);
        let mut trace = self.trace.borrow_mut();
        if trace.len() < 64 {
            trace.push(event());
        }
        Ok(())
    }

    fn nat_atom(&self, key: String) -> Result<Poly, KernelError> {
        let replacement = self.ground.borrow().get(&key).cloned();
        match replacement {
            Some(poly) => {
                self.spend(|| format!("hypothesis:{key}"))?;
                Ok(poly)
            }
            None => Ok(Poly::atom(&key)),
        }
    }

    fn as_nat(&self, sym: Sym) -> Result<Poly, KernelError> {
        match sym {
            Sym::Nat(poly) => Ok(poly),
            Sym::Bool(_) | Sym::FnRef(_) | Sym::Closure { .. } | Sym::List { .. } | Sym::OptSome(_) | Sym::OptNone | Sym::Int(_) => {
                Err(KernelError::Unsupported(format!("non-Nat term `{}` in Nat position", sym.key())))
            }
            Sym::Opaque(key) if self.strict_opaque.get() => {
                Err(KernelError::Unsupported(format!("term `{key}` of unknown type in arithmetic (the theory uses Int)")))
            }
            Sym::Opaque(key) => self.nat_atom(key),
        }
    }

    /// An irreducible term of the given ordinary type.
    fn atom_of_type(&self, ty: &str, key: String) -> Result<Sym, KernelError> {
        Ok(if ty == "Nat" {
            Sym::Nat(self.nat_atom(key)?)
        } else if ty == "Int" {
            Sym::Int(Poly::atom(&key))
        } else if ty.starts_with("List<") {
            Sym::List { items: Vec::new(), rest: Some(key) }
        } else {
            Sym::Opaque(key)
        })
    }

    /// Same kind of irreducible term as `like`.
    fn atom_like(&self, like: &Sym, key: String) -> Result<Sym, KernelError> {
        Ok(match like {
            Sym::Nat(_) => Sym::Nat(self.nat_atom(key)?),
            Sym::Int(_) => Sym::Int(Poly::atom(&key)),
            Sym::List { .. } => Sym::List { items: Vec::new(), rest: Some(key) },
            _ => Sym::Opaque(key),
        })
    }

    fn as_list(sym: Sym) -> Option<(Vec<Sym>, Option<String>)> {
        match sym {
            Sym::List { items, rest } => Some((items, rest)),
            Sym::Opaque(key) => Some((Vec::new(), Some(key))),
            _ => None,
        }
    }

    /// Normalize `expr` under `env`. `unfold_recursive` is false inside
    /// undecided branches to keep normalization finite and small.
    pub fn normalize(&self, expr: &Expr, env: &BTreeMap<String, Sym>, unfold_recursive: bool) -> Result<Sym, KernelError> {
        let depth = self.depth.get();
        if depth >= MAX_SYMBOLIC_DEPTH {
            return Err(KernelError::Depth);
        }
        self.depth.set(depth + 1);
        let mut result = self.normalize_inner(expr, env, unfold_recursive);
        self.depth.set(depth);
        if let Ok(Sym::Opaque(key)) = &result {
            if self.truths.borrow().contains(key) {
                self.spend(|| format!("hypothesis:{key}"))?;
                result = Ok(Sym::Bool(true));
            }
        }
        result
    }

    fn normalize_inner(&self, expr: &Expr, env: &BTreeMap<String, Sym>, unfold_recursive: bool) -> Result<Sym, KernelError> {
        match &expr.kind {
            // v0.83: a Text literal is an irreducible term keyed by its
            // quoted, escaped text; distinct literals are distinct values.
            ExprKind::TextLiteral(text) => Ok(Sym::Opaque(format!("{text:?}"))),
            // v0.82: `let` is substitution of the normalized value.
            ExprKind::Let { name, value, body } => {
                let value = self.normalize(value, env, unfold_recursive)?;
                let mut inner = env.clone();
                inner.insert(name.clone(), value);
                self.normalize(body, &inner, unfold_recursive)
            }
            ExprKind::IntLiteral(value) => {
                let value: i128 = value.parse().map_err(|_| KernelError::Unsupported(format!("literal {value}")))?;
                Ok(Sym::Nat(Poly::constant(value)))
            }
            ExprKind::Ident(name) => Ok(match env.get(name) {
                Some(sym) => sym.clone(),
                None if self.functions.contains_key(name) => Sym::FnRef(name.clone()),
                None => Sym::Opaque(name.clone()),
            }),
            ExprKind::QualifiedIdent { theory, name } => Ok(Sym::Opaque(format!("{theory}.{name}"))),
            ExprKind::Add { lhs, rhs } => {
                let a = self.normalize(lhs, env, unfold_recursive)?;
                let b = self.normalize(rhs, env, unfold_recursive)?;
                if matches!(a, Sym::Int(_)) || matches!(b, Sym::Int(_)) {
                    return Ok(Sym::Int(self.as_int(a)?.add(&self.as_int(b)?)?));
                }
                Ok(Sym::Nat(self.as_nat(a)?.add(&self.as_nat(b)?)?))
            }
            ExprKind::Power { base, exp } => {
                let base = self.normalize(base, env, unfold_recursive)?;
                let exp = self.normalize(exp, env, unfold_recursive)?;
                let base = self.as_nat(base)?;
                let exp = self.as_nat(exp)?;
                match exp.as_constant() {
                    Some(k) if (0..=MAX_POWER as i128).contains(&k) => Ok(Sym::Nat(base.pow(k as u128)?)),
                    _ => Ok(Sym::Nat(self.nat_atom(format!("pow({},{})", base.render(), exp.render()))?)),
                }
            }
            ExprKind::CompareGt { lhs, rhs } => {
                let a = self.normalize(lhs, env, unfold_recursive)?;
                let b = self.normalize(rhs, env, unfold_recursive)?;
                // v0.85: Int comparisons decide only on constant differences.
                if matches!(a, Sym::Int(_)) || matches!(b, Sym::Int(_)) {
                    let diff = self.as_int(a.clone())?.sub(&self.as_int(b.clone())?)?;
                    return Ok(match diff.as_constant() {
                        Some(d) => Sym::Bool(d > 0),
                        None => Sym::Opaque(format!("gt({},{})", a.key(), b.key())),
                    });
                }
                let diff = self.as_nat(a.clone())?.sub(&self.as_nat(b.clone())?)?;
                Ok(if diff.surely_positive() {
                    Sym::Bool(true)
                } else if diff.surely_nonpositive() {
                    Sym::Bool(false)
                } else {
                    Sym::Opaque(format!("gt({},{})", a.key(), b.key()))
                })
            }
            ExprKind::Lambda { params, body } => Ok(Sym::Closure {
                key: format!(
                    "fun({};{})",
                    params.iter().map(|p| format!("{}:{}", p.name, p.ty)).collect::<Vec<_>>().join(","),
                    render_with_env(body, env, &params.iter().map(|p| p.name.clone()).collect::<Vec<_>>())
                ),
                params: params.clone(),
                body: (**body).clone(),
                env: env.clone(),
            }),
            ExprKind::Call { name, args } => self.normalize_call(name, args, env, unfold_recursive),
        }
    }

    fn normalize_call(&self, name: &str, args: &[Expr], env: &BTreeMap<String, Sym>, unfold_recursive: bool) -> Result<Sym, KernelError> {
        // Lazy forms first.
        if name == "if_then_else" && args.len() == 3 {
            let cond = self.normalize(&args[0], env, unfold_recursive)?;
            return match cond {
                Sym::Bool(true) => self.normalize(&args[1], env, unfold_recursive),
                Sym::Bool(false) => self.normalize(&args[2], env, unfold_recursive),
                cond => {
                    let then_branch = self.normalize(&args[1], env, false)?;
                    let else_branch = self.normalize(&args[2], env, false)?;
                    if then_branch.key() == else_branch.key() {
                        return Ok(then_branch);
                    }
                    let key = format!("ite({},{},{})", cond.key(), then_branch.key(), else_branch.key());
                    Ok(if matches!(then_branch, Sym::Int(_)) || matches!(else_branch, Sym::Int(_)) {
                        Sym::Int(Poly::atom(&key))
                    } else if matches!(then_branch, Sym::Nat(_)) || matches!(else_branch, Sym::Nat(_)) {
                        Sym::Nat(self.nat_atom(key)?)
                    } else if matches!(then_branch, Sym::List { .. }) || matches!(else_branch, Sym::List { .. }) {
                        Sym::List { items: Vec::new(), rest: Some(key) }
                    } else {
                        Sym::Opaque(key)
                    })
                }
            };
        }
        if (name == "and" || name == "or") && args.len() == 2 {
            let lhs = self.normalize(&args[0], env, unfold_recursive)?;
            let absorbing = name == "or";
            return match lhs {
                Sym::Bool(value) if value == absorbing => Ok(Sym::Bool(value)),
                Sym::Bool(_) => self.normalize(&args[1], env, unfold_recursive),
                lhs => {
                    let rhs = self.normalize(&args[1], env, unfold_recursive)?;
                    Ok(match rhs {
                        Sym::Bool(value) if value == absorbing => Sym::Bool(value),
                        Sym::Bool(_) => lhs,
                        rhs => Sym::Opaque(format!("{name}({},{})", lhs.key(), rhs.key())),
                    })
                }
            };
        }

        // v0.80: lemmas with nested call patterns (`sum_list(inc_all(?xs))`)
        // match the call syntactically before its arguments are normalized.
        if !env.contains_key(name) && self.functions.contains_key(name) {
            if let Some(result) = self.rewrite_syntactic(name, args, env, unfold_recursive)? {
                return Ok(result);
            }
        }

        // Calls of function-valued variables: beta-reduction / named call.
        if let Some(callee) = env.get(name).cloned() {
            let values = args.iter().map(|a| self.normalize(a, env, unfold_recursive)).collect::<Result<Vec<_>, _>>()?;
            return self.apply(callee, values, unfold_recursive);
        }

        let values = args.iter().map(|a| self.normalize(a, env, unfold_recursive)).collect::<Result<Vec<_>, _>>()?;
        match (name, values.as_slice()) {
            ("true", []) => return Ok(Sym::Bool(true)),
            ("false", []) => return Ok(Sym::Bool(false)),
            ("not", [Sym::Bool(value)]) => return Ok(Sym::Bool(!value)),
            ("mul", [Sym::Int(a), Sym::Int(b)]) => return Ok(Sym::Int(a.mul(b)?)),
            ("mul", [a, b]) => return Ok(Sym::Nat(self.as_nat(a.clone())?.mul(&self.as_nat(b.clone())?)?)),
            // v0.85: Int.
            ("int", [n]) => return Ok(Sym::Int(self.as_nat(n.clone())?)),
            ("__sub", [a, b]) => return Ok(Sym::Int(self.as_int(a.clone())?.sub(&self.as_int(b.clone())?)?)),
            ("__neg", [Sym::Nat(n)]) => return Ok(Sym::Int(n.scale(-1)?)),
            ("__neg", [a]) => return Ok(Sym::Int(self.as_int(a.clone())?.scale(-1)?)),
            ("int_eq", [a, b]) => {
                let diff = self.as_int(a.clone())?.sub(&self.as_int(b.clone())?)?;
                return Ok(match diff.as_constant() {
                    Some(d) => Sym::Bool(d == 0),
                    None => Sym::Opaque(format!("int_eq({},{})", a.key(), b.key())),
                });
            }
            ("abs", [a]) => {
                let a = self.as_int(a.clone())?;
                return Ok(match a.as_constant() {
                    Some(c) => Sym::Nat(Poly::constant(c.checked_abs().ok_or(KernelError::Overflow)?)),
                    None => {
                        // abs(x) = abs(-x): pick the sign with the smaller rendering.
                        let (plain, negated) = (a.render(), a.scale(-1)?.render());
                        Sym::Nat(self.nat_atom(format!("abs({})", if negated < plain { negated } else { plain }))?)
                    }
                });
            }
            ("div" | "mod", [Sym::Int(a), Sym::Int(b)]) => {
                let key = format!("{name}(int({}),int({}))", a.render(), b.render());
                return Ok(Sym::Int(match (a.as_constant(), b.as_constant()) {
                    (_, Some(0)) => if name == "div" { Poly::constant(0) } else { a.clone() },
                    (_, Some(1)) => if name == "div" { a.clone() } else { Poly::constant(0) },
                    (Some(x), Some(y)) => Poly::constant(
                        if name == "div" { x.checked_div_euclid(y) } else { x.checked_rem_euclid(y) }.ok_or(KernelError::Overflow)?,
                    ),
                    _ => Poly::atom(&key),
                }));
            }
            ("div" | "mod", [a, b]) => {
                let (a, b) = (self.as_nat(a.clone())?, self.as_nat(b.clone())?);
                return Ok(Sym::Nat(match (a.as_constant(), b.as_constant()) {
                    (_, Some(0)) => if name == "div" { Poly::constant(0) } else { a.clone() },
                    (_, Some(1)) => if name == "div" { a.clone() } else { Poly::constant(0) },
                    (Some(x), Some(y)) => Poly::constant(if name == "div" { x / y } else { x % y }),
                    _ => self.nat_atom(format!("{name}({},{})", a.render(), b.render()))?,
                }));
            }
            ("nat_eq", [a, b]) => {
                let diff = self.as_nat(a.clone())?.sub(&self.as_nat(b.clone())?)?;
                return Ok(if diff.is_zero() {
                    Sym::Bool(true)
                } else if diff.surely_positive() || diff.scale(-1)?.surely_positive() {
                    Sym::Bool(false)
                } else {
                    Sym::Opaque(format!("nat_eq({},{})", a.key(), b.key()))
                });
            }
            ("pred", [a]) => {
                let a = self.as_nat(a.clone())?;
                if a.is_zero() {
                    return Ok(Sym::Nat(a));
                }
                if a.surely_positive() {
                    return Ok(Sym::Nat(a.sub(&Poly::constant(1))?));
                }
                return Ok(Sym::Nat(self.nat_atom(format!("pred({})", a.render()))?));
            }
            ("monus", [a, b]) => {
                let a = self.as_nat(a.clone())?;
                let b = self.as_nat(b.clone())?;
                let diff = a.sub(&b)?;
                // a - b >= 0 for every assignment: truncation never happens.
                if diff.constant_term() >= 0 && diff.non_constant_coefficients().all(|c| c >= 0) {
                    return Ok(Sym::Nat(diff));
                }
                if diff.surely_nonpositive() {
                    return Ok(Sym::Nat(Poly::constant(0)));
                }
                return Ok(Sym::Nat(self.nat_atom(format!("monus({},{})", a.render(), b.render()))?));
            }
            // v0.78: lists and options.
            ("list", items) => return Ok(Sym::List { items: items.to_vec(), rest: None }),
            ("empty_list", [_]) => return Ok(Sym::List { items: Vec::new(), rest: None }),
            ("cons", [head, list]) => {
                if let Some((mut items, rest)) = Self::as_list(list.clone()) {
                    items.insert(0, head.clone());
                    return Ok(Sym::List { items, rest });
                }
            }
            ("tail", [list]) => {
                if let Some((items, rest)) = Self::as_list(list.clone()) {
                    return Ok(match (items.is_empty(), rest) {
                        (false, rest) => Sym::List { items: items[1..].to_vec(), rest },
                        (true, None) => Sym::List { items: Vec::new(), rest: None },
                        (true, Some(rest)) => Sym::List { items: Vec::new(), rest: Some(format!("tail({rest})")) },
                    });
                }
            }
            ("head", [list]) => {
                if let Some((items, rest)) = Self::as_list(list.clone()) {
                    return Ok(match (items.first(), rest) {
                        (Some(first), _) => Sym::OptSome(Box::new(first.clone())),
                        (None, None) => Sym::OptNone,
                        (None, Some(rest)) => Sym::Opaque(format!("head({rest})")),
                    });
                }
            }
            ("is_empty", [list]) => {
                if let Some((items, rest)) = Self::as_list(list.clone()) {
                    return Ok(match (items.is_empty(), rest) {
                        (false, _) => Sym::Bool(false),
                        (true, None) => Sym::Bool(true),
                        (true, Some(rest)) => Sym::Opaque(format!("is_empty({rest})")),
                    });
                }
            }
            ("length", [list]) => {
                if let Some((items, rest)) = Self::as_list(list.clone()) {
                    let mut length = Poly::constant(items.len() as i128);
                    if let Some(rest) = rest {
                        length = length.add(&self.nat_atom(format!("length({rest})"))?)?;
                    }
                    return Ok(Sym::Nat(length));
                }
            }
            ("map", [list, function]) => {
                if let Some((items, rest)) = Self::as_list(list.clone()) {
                    let mut mapped = Vec::with_capacity(items.len());
                    for item in items {
                        mapped.push(self.apply(function.clone(), vec![item], unfold_recursive)?);
                    }
                    let rest = rest.map(|rest| format!("map({rest},{})", function.key()));
                    return Ok(Sym::List { items: mapped, rest });
                }
            }
            ("fold", [list, initial, function]) => {
                if let Some((items, rest)) = Self::as_list(list.clone()) {
                    let mut accumulator = initial.clone();
                    for item in items {
                        accumulator = self.apply(function.clone(), vec![accumulator, item], unfold_recursive)?;
                    }
                    return match rest {
                        None => Ok(accumulator),
                        Some(rest) => {
                            let key = format!("fold({rest},{},{})", accumulator.key(), function.key());
                            self.atom_like(initial, key)
                        }
                    };
                }
            }
            // v0.83: Text literals are `Opaque` keys in Rust debug form.
            ("text_eq", [Sym::Opaque(a), Sym::Opaque(b)]) if a == b => return Ok(Sym::Bool(true)),
            ("text_eq", [Sym::Opaque(a), Sym::Opaque(b)]) if text_literal(a).is_some() && text_literal(b).is_some() => {
                return Ok(Sym::Bool(false));
            }
            ("concat", [Sym::Opaque(a), Sym::Opaque(b)]) => {
                if let (Some(a), Some(b)) = (text_literal(a), text_literal(b)) {
                    return Ok(Sym::Opaque(format!("{:?}", a + &b)));
                }
            }
            ("text_length", [Sym::Opaque(t)]) => {
                if let Some(t) = text_literal(t) {
                    return Ok(Sym::Nat(Poly::constant(t.chars().count() as i128)));
                }
            }
            ("nat_to_text", [Sym::Nat(n)]) => {
                if let Some(n) = n.as_constant() {
                    return Ok(Sym::Opaque(format!("{:?}", n.to_string())));
                }
            }
            // v0.82: produced only by `match` desugaring, under an is_empty /
            // is_none guard.
            ("__match_head", [list]) => {
                if let Some((items, rest)) = Self::as_list(list.clone()) {
                    return match (items.first(), rest) {
                        (Some(first), _) => Ok(first.clone()),
                        (None, rest) => Ok(Sym::Opaque(format!("__match_head({})", rest.unwrap_or_default()))),
                    };
                }
            }
            ("__match_some", [Sym::OptSome(value)]) => return Ok((**value).clone()),
            ("__match_variant", [Sym::OptSome(value), _]) => return Ok((**value).clone()),
            ("some", [value]) => return Ok(Sym::OptSome(Box::new(value.clone()))),
            ("none", [_]) => return Ok(Sym::OptNone),
            ("unwrap_or", [option, default]) => match option {
                Sym::OptSome(value) => return Ok((**value).clone()),
                Sym::OptNone => return Ok(default.clone()),
                other => return self.atom_like(default, format!("unwrap_or({},{})", other.key(), default.key())),
            },
            ("is_some", [Sym::OptSome(_)]) | ("is_none", [Sym::OptNone]) => return Ok(Sym::Bool(true)),
            ("is_some", [Sym::OptNone]) | ("is_none", [Sym::OptSome(_)]) => return Ok(Sym::Bool(false)),
            _ => {}
        }

        if let Some(decl) = self.functions.get(name) {
            if let Some(result) = self.rewrite_with_lemmas(name, &values, unfold_recursive)? {
                return Ok(result);
            }
            if self.may_unfold(decl, &values, unfold_recursive) {
                self.spend(|| format!("unfold:{name}"))?;
                let body_env: BTreeMap<String, Sym> =
                    decl.params.iter().map(|p| p.name.clone()).zip(values.iter().cloned()).collect();
                return self.normalize(&decl.body, &body_env, unfold_recursive);
            }
            let key = format!("{name}({})", values.iter().map(Sym::key).collect::<Vec<_>>().join(","));
            return self.atom_of_type(&decl.ret, key);
        }

        Ok(Sym::Opaque(format!("{name}({})", values.iter().map(Sym::key).collect::<Vec<_>>().join(","))))
    }

    fn apply(&self, callee: Sym, values: Vec<Sym>, unfold_recursive: bool) -> Result<Sym, KernelError> {
        match callee {
            Sym::FnRef(name) => {
                // Re-enter through a synthetic call on already normalized values.
                let mut env = BTreeMap::new();
                let mut args = Vec::with_capacity(values.len());
                for (index, value) in values.into_iter().enumerate() {
                    let slot = format!("#arg{index}");
                    env.insert(slot.clone(), value);
                    args.push(Expr::ident(slot, 0));
                }
                self.normalize_call(&name, &args, &env, unfold_recursive)
            }
            Sym::Closure { key, params, body, mut env } => {
                if params.len() != values.len() {
                    return Err(KernelError::Unsupported(format!("lambda arity mismatch in {key}")));
                }
                self.spend(|| "beta".to_string())?;
                for (param, value) in params.iter().zip(values) {
                    env.insert(param.name.clone(), value);
                }
                self.normalize(&body, &env, unfold_recursive)
            }
            other => Ok(Sym::Opaque(format!("apply({},{})", other.key(), values.iter().map(Sym::key).collect::<Vec<_>>().join(",")))),
        }
    }

    /// Non-recursive functions always unfold. A measured function unfolds
    /// when every measured Nat argument is constant or surely positive, so its
    /// guards decide; list measures are not unfolded symbolically.
    fn may_unfold(&self, decl: &FnDecl, values: &[Sym], unfold_recursive: bool) -> bool {
        let Some(measure) = &decl.measure else { return true };
        if !unfold_recursive || measure.is_loop() {
            return false;
        }
        measure.params.iter().all(|measured| {
            let index = decl.params.iter().position(|p| &p.name == measured);
            match index.and_then(|i| values.get(i)) {
                Some(Sym::Nat(poly)) => poly.as_constant().is_some() || poly.surely_positive(),
                // v0.78: a list measure unfolds when the list is known to be
                // empty or known to be non-empty, so `is_empty` guards decide.
                Some(Sym::List { items, rest }) => !items.is_empty() || rest.is_none(),
                _ => false,
            }
        })
    }

    fn rewrite_with_lemmas(&self, name: &str, values: &[Sym], unfold_recursive: bool) -> Result<Option<Sym>, KernelError> {
        let local = self.local_rules.borrow().clone();
        let empty = BTreeMap::new();
        let candidates = self.lemmas.iter().map(|lemma| (lemma, &empty)).chain(local.iter().map(|(rule, fixed)| (rule, fixed)));
        for (lemma, fixed) in candidates {
            let ExprKind::Call { name: pattern_name, args: pattern_args } = &lemma.lhs.kind else { continue };
            if pattern_name != name || pattern_args.len() != values.len() {
                continue;
            }
            let mut bindings: BTreeMap<String, Sym> = fixed.clone();
            let mut matched = true;
            for (pattern, value) in pattern_args.iter().zip(values) {
                match &pattern.kind {
                    ExprKind::Ident(var) if lemma.vars.contains(var) => match bindings.get(var) {
                        Some(bound) if bound.key() != value.key() => {
                            matched = false;
                            break;
                        }
                        Some(_) => {}
                        None => {
                            bindings.insert(var.clone(), value.clone());
                        }
                    },
                    _ => {
                        let mut free = Vec::new();
                        collect_idents(pattern, &mut free);
                        if free.iter().any(|v| lemma.vars.contains(v) && !bindings.contains_key(v)) {
                            matched = false;
                            break;
                        }
                        let expected = self.normalize(pattern, &bindings, false)?;
                        if expected.key() != value.key() {
                            matched = false;
                            break;
                        }
                    }
                }
            }
            if matched && lemma.vars.iter().all(|v| bindings.contains_key(v)) {
                let active = format!("{}:{name}({})", lemma.name, values.iter().map(Sym::key).collect::<Vec<_>>().join(","));
                if self.active_rewrites.borrow().contains(&active) {
                    // Re-entering the same rewrite would cycle; leave the call.
                    continue;
                }
                self.spend(|| format!("lemma:{}", lemma.name))?;
                self.active_rewrites.borrow_mut().push(active);
                let result = self.normalize(&lemma.rhs, &bindings, unfold_recursive);
                self.active_rewrites.borrow_mut().pop();
                return Ok(Some(result?));
            }
        }
        Ok(None)
    }

    /// v0.80: rewrite `name(args)` with a lemma whose pattern has a nested
    /// call (`f(g(?x))`). Pattern variables bind to the normalized values of
    /// the matching sub-expressions; calls must match by name and arity;
    /// anything else must normalize to the same key. Sound: the call equals
    /// the lemma's left side under these bindings.
    fn rewrite_syntactic(
        &self,
        name: &str,
        args: &[Expr],
        env: &BTreeMap<String, Sym>,
        unfold_recursive: bool,
    ) -> Result<Option<Sym>, KernelError> {
        let local = self.local_rules.borrow().clone();
        let empty = BTreeMap::new();
        let candidates = self.lemmas.iter().map(|lemma| (lemma, &empty)).chain(local.iter().map(|(rule, fixed)| (rule, fixed)));
        for (lemma, fixed) in candidates {
            let ExprKind::Call { name: pattern_name, args: pattern_args } = &lemma.lhs.kind else { continue };
            if pattern_name != name || pattern_args.len() != args.len() {
                continue;
            }
            let nested = pattern_args.iter().any(|p| matches!(&p.kind, ExprKind::Call { .. }) && {
                let mut free = Vec::new();
                collect_idents(p, &mut free);
                free.iter().any(|v| lemma.vars.contains(v))
            });
            if !nested {
                continue;
            }
            let mut bindings: BTreeMap<String, Sym> = fixed.clone();
            let mut matched = true;
            for (pattern, expr) in pattern_args.iter().zip(args) {
                if !self.match_pattern(pattern, expr, env, &lemma.vars, &mut bindings)? {
                    matched = false;
                    break;
                }
            }
            if !matched || !lemma.vars.iter().all(|v| bindings.contains_key(v)) {
                continue;
            }
            let active = format!(
                "syntactic:{}:{name}({})",
                lemma.name,
                lemma.vars.iter().map(|v| bindings[v].key()).collect::<Vec<_>>().join(",")
            );
            if self.active_rewrites.borrow().contains(&active) {
                continue;
            }
            self.spend(|| format!("lemma:{}", lemma.name))?;
            self.active_rewrites.borrow_mut().push(active);
            let result = self.normalize(&lemma.rhs, &bindings, unfold_recursive);
            self.active_rewrites.borrow_mut().pop();
            return Ok(Some(result?));
        }
        Ok(None)
    }

    fn match_pattern(
        &self,
        pattern: &Expr,
        expr: &Expr,
        env: &BTreeMap<String, Sym>,
        vars: &[String],
        bindings: &mut BTreeMap<String, Sym>,
    ) -> Result<bool, KernelError> {
        if let ExprKind::Ident(var) = &pattern.kind {
            if vars.contains(var) {
                let value = self.normalize(expr, env, false)?;
                return Ok(match bindings.get(var) {
                    Some(bound) => bound.key() == value.key(),
                    None => {
                        bindings.insert(var.clone(), value);
                        true
                    }
                });
            }
        }
        if let (ExprKind::Call { name: pn, args: pargs }, ExprKind::Call { name: en, args: eargs }) = (&pattern.kind, &expr.kind) {
            let mut free = Vec::new();
            collect_idents(pattern, &mut free);
            if free.iter().any(|v| vars.contains(v) && !bindings.contains_key(v)) {
                if pn != en || pargs.len() != eargs.len() || env.contains_key(en) || !self.functions.contains_key(en) {
                    return Ok(false);
                }
                for (p, e) in pargs.iter().zip(eargs) {
                    if !self.match_pattern(p, e, env, vars, bindings)? {
                        return Ok(false);
                    }
                }
                return Ok(true);
            }
        }
        let mut free = Vec::new();
        collect_idents(pattern, &mut free);
        if free.iter().any(|v| vars.contains(v) && !bindings.contains_key(v)) {
            return Ok(false);
        }
        let expected = self.normalize(pattern, bindings, false)?;
        let actual = self.normalize(expr, env, false)?;
        Ok(expected.key() == actual.key())
    }

    /// v0.78: linear fact of a proposition: `nat_eq(a, b)` is `a - b = 0`,
    /// `a > b` is `a - b - 1 >= 0`, `not(a > b)` is `b - a >= 0`.
    pub fn fact(&self, body: &Expr, env: &BTreeMap<String, Sym>) -> Result<Option<Fact>, KernelError> {
        // v0.85: Int sides give no fact (no sign assumptions on Int).
        if expr_mentions_int(body) || env.values().any(|v| matches!(v, Sym::Int(_))) {
            return Ok(None);
        }
        let difference = |a: &Expr, b: &Expr| -> Result<Poly, KernelError> {
            let a = self.normalize(a, env, true)?;
            let b = self.normalize(b, env, true)?;
            self.as_nat(a)?.sub(&self.as_nat(b)?)
        };
        Ok(match &body.kind {
            ExprKind::Call { name, args } if name == "nat_eq" && args.len() == 2 => Some(Fact::Eq(difference(&args[0], &args[1])?)),
            ExprKind::CompareGt { lhs, rhs } => Some(Fact::Ge(difference(lhs, rhs)?.sub(&Poly::constant(1))?)),
            ExprKind::Call { name, args } if name == "not" && args.len() == 1 => match &args[0].kind {
                ExprKind::CompareGt { lhs, rhs } => Some(Fact::Ge(difference(rhs, lhs)?)),
                _ => None,
            },
            _ => None,
        })
    }

    /// Normalize both sides of an equation body to polynomials.
    pub fn equation(&self, body: &Expr, env: &BTreeMap<String, Sym>) -> Result<Option<(Poly, Poly)>, KernelError> {
        let Some((a, b)) = equation_sides(body) else { return Ok(None) };
        let a = self.normalize(a, env, true)?;
        let b = self.normalize(b, env, true)?;
        Ok(Some((self.as_nat(a)?, self.as_nat(b)?)))
    }
}

/// v0.83: decode a Text literal key (Rust debug form of a `str`).
fn text_literal(key: &str) -> Option<String> {
    let inner = key.strip_prefix('"')?.strip_suffix('"')?;
    let mut out = String::with_capacity(inner.len());
    let mut chars = inner.chars();
    while let Some(c) = chars.next() {
        if c == '"' {
            return None;
        }
        if c != '\\' {
            out.push(c);
            continue;
        }
        match chars.next()? {
            'n' => out.push('\n'),
            't' => out.push('\t'),
            'r' => out.push('\r'),
            '0' => out.push('\0'),
            '\\' => out.push('\\'),
            '"' => out.push('"'),
            '\'' => out.push('\''),
            'u' => {
                if chars.next()? != '{' {
                    return None;
                }
                let hex: String = chars.by_ref().take_while(|c| *c != '}').collect();
                out.push(char::from_u32(u32::from_str_radix(&hex, 16).ok()?)?);
            }
            _ => return None,
        }
    }
    // Only keys that round-trip are literals.
    (format!("{out:?}") == key).then_some(out)
}

fn collect_idents(expr: &Expr, out: &mut Vec<String>) {
    match &expr.kind {
        ExprKind::Ident(name) => out.push(name.clone()),
        ExprKind::Call { args, .. } => args.iter().for_each(|a| collect_idents(a, out)),
        ExprKind::Add { lhs, rhs } | ExprKind::CompareGt { lhs, rhs } => {
            collect_idents(lhs, out);
            collect_idents(rhs, out);
        }
        ExprKind::Power { base, exp } => {
            collect_idents(base, out);
            collect_idents(exp, out);
        }
        ExprKind::Lambda { body, .. } => collect_idents(body, out),
        ExprKind::Let { value, body, .. } => {
            collect_idents(value, out);
            collect_idents(body, out);
        }
        ExprKind::IntLiteral(_) | ExprKind::TextLiteral(_) | ExprKind::QualifiedIdent { .. } => {}
    }
}

/// Render `expr` with free variables replaced by their symbolic keys.
fn render_with_env(expr: &Expr, env: &BTreeMap<String, Sym>, bound: &[String]) -> String {
    match &expr.kind {
        ExprKind::Ident(name) if !bound.contains(name) => match env.get(name) {
            Some(sym) => format!("[{}]", sym.key()),
            None => name.clone(),
        },
        ExprKind::Ident(name) => name.clone(),
        ExprKind::IntLiteral(value) => value.clone(),
        ExprKind::TextLiteral(text) => format!("{text:?}"),
        ExprKind::QualifiedIdent { theory, name } => format!("{theory}.{name}"),
        ExprKind::Add { lhs, rhs } => format!("({}+{})", render_with_env(lhs, env, bound), render_with_env(rhs, env, bound)),
        ExprKind::CompareGt { lhs, rhs } => format!("({}>{})", render_with_env(lhs, env, bound), render_with_env(rhs, env, bound)),
        ExprKind::Power { base, exp } => format!("({}^{})", render_with_env(base, env, bound), render_with_env(exp, env, bound)),
        ExprKind::Call { name, args } => format!(
            "{}({})",
            if bound.contains(name) { name.clone() } else { env.get(name).map(|s| format!("[{}]", s.key())).unwrap_or_else(|| name.clone()) },
            args.iter().map(|a| render_with_env(a, env, bound)).collect::<Vec<_>>().join(",")
        ),
        ExprKind::Lambda { params, body } => {
            let mut inner = bound.to_vec();
            inner.extend(params.iter().map(|p| p.name.clone()));
            format!(
                "fun({};{})",
                params.iter().map(|p| format!("{}:{}", p.name, p.ty)).collect::<Vec<_>>().join(","),
                render_with_env(body, env, &inner)
            )
        }
        ExprKind::Let { name, value, body } => {
            let mut inner = bound.to_vec();
            inner.push(name.clone());
            format!("let({name};{};{})", render_with_env(value, env, bound), render_with_env(body, env, &inner))
        }
    }
}

/// v0.78: a proposition as a polynomial fact over Nat atoms.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Fact {
    /// `p = 0`
    Eq(Poly),
    /// `p >= 0`
    Ge(Poly),
    /// v0.79: a Bool proposition whose normal form is the irreducible term
    /// with this key holds (an induction hypothesis such as `even(k)`).
    Holds(String),
}

impl fmt::Display for Fact {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Fact::Eq(p) => write!(f, "{p} = 0"),
            Fact::Ge(p) => write!(f, "{p} >= 0"),
            Fact::Holds(key) => write!(f, "{key} holds"),
        }
    }
}

/// `true` when `goal - c * hyp` has only nonnegative coefficients for some
/// rational `c` (`c >= 0` unless `allow_negative`). Every atom is a natural
/// number, so such a polynomial is `>= 0`; with `hyp >= 0` (or `hyp = 0`) this
/// gives `goal >= 0`.
fn nonnegative_combination(goal: &Poly, hyp: &Poly, allow_negative: bool) -> Result<bool, KernelError> {
    let mut candidates: Vec<(i128, i128)> = vec![(0, 1), (1, 1)];
    for (monomial, &hyp_coefficient) in &hyp.0 {
        let goal_coefficient = goal.0.get(monomial).copied().unwrap_or(0);
        candidates.push((goal_coefficient, hyp_coefficient));
    }
    for (mut numerator, mut denominator) in candidates {
        if denominator == 0 {
            continue;
        }
        if denominator < 0 {
            numerator = -numerator;
            denominator = -denominator;
        }
        if numerator < 0 && !allow_negative {
            continue;
        }
        let combination = goal.scale(denominator)?.sub(&hyp.scale(numerator)?)?;
        if combination.0.values().all(|c| *c >= 0) {
            return Ok(true);
        }
    }
    Ok(false)
}

/// `true` when `goal = c * hyp` for a rational `c` (both as differences).
fn is_rational_multiple(goal: &Poly, hyp: &Poly) -> Result<bool, KernelError> {
    if goal.is_zero() {
        return Ok(true);
    }
    let Some((monomial, &hyp_coefficient)) = hyp.0.iter().next() else { return Ok(false) };
    let Some(&goal_coefficient) = goal.0.get(monomial) else { return Ok(false) };
    // goal * hyp_coefficient == hyp * goal_coefficient
    Ok(goal.scale(hyp_coefficient)? == hyp.scale(goal_coefficient)?)
}

/// Outcome of one proof obligation.
#[derive(Debug, Clone)]
pub struct ObligationReport {
    pub steps: usize,
    pub trace: Vec<String>,
    pub method: String,
}

/// Failure with the residual normal form for the user.
#[derive(Debug, Clone)]
pub struct ObligationFailure {
    pub obligation: String,
    pub residual: String,
}

impl fmt::Display for ObligationFailure {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} does not normalize to true; residual: {}", self.obligation, self.residual)
    }
}

/// Prove `body` true under `env` by normalization, optionally using one
/// hypothesis fact by linear / nonnegative combination.
pub fn prove_obligation(
    kernel: &Kernel<'_>,
    obligation: &str,
    body: &Expr,
    env: &BTreeMap<String, Sym>,
    hypothesis: Option<&Fact>,
) -> Result<ObligationReport, ObligationFailure> {
    prove_obligation_with(kernel, obligation, body, env, hypothesis.map(std::slice::from_ref).unwrap_or(&[]))
}

/// v0.79: [`prove_obligation`] with several hypotheses (`step s` induction).
/// Each hypothesis is tried alone, then the sum of all equational and the
/// sum of all inequality hypotheses.
pub fn prove_obligation_with(
    kernel: &Kernel<'_>,
    obligation: &str,
    body: &Expr,
    env: &BTreeMap<String, Sym>,
    hypotheses: &[Fact],
) -> Result<ObligationReport, ObligationFailure> {
    let fail = |residual: String| ObligationFailure { obligation: obligation.to_string(), residual };
    let report = |method: &str| ObligationReport { steps: kernel.steps_used(), trace: kernel.trace(), method: method.to_string() };
    if expr_mentions_int(body) || env.values().any(|v| matches!(v, Sym::Int(_))) {
        kernel.set_strict_opaque();
    }
    let normalized = kernel.normalize(body, env, true).map_err(|e| fail(e.to_string()))?;
    if matches!(normalized, Sym::Bool(true)) {
        return Ok(report("normalize"));
    }
    let goal = kernel.fact(body, env).map_err(|e| fail(e.to_string()))?;
    if let Some(Fact::Ge(g)) = &goal {
        if g.0.values().all(|c| *c >= 0) {
            return Ok(report("normalize+nonnegativity"));
        }
    }

    let mut candidates: Vec<Fact> = hypotheses.to_vec();
    if hypotheses.len() > 1 {
        let sum = |pick: fn(&Fact) -> Option<&Poly>| -> Result<Option<Poly>, KernelError> {
            let mut total: Option<Poly> = None;
            for poly in hypotheses.iter().filter_map(pick) {
                total = Some(match total {
                    None => poly.clone(),
                    Some(total) => total.add(poly)?,
                });
            }
            Ok(total)
        };
        let eq_sum = sum(|f| if let Fact::Eq(p) = f { Some(p) } else { None }).map_err(|e| fail(e.to_string()))?;
        let ge_sum = sum(|f| if let Fact::Ge(p) = f { Some(p) } else { None }).map_err(|e| fail(e.to_string()))?;
        candidates.extend(eq_sum.map(Fact::Eq));
        candidates.extend(ge_sum.map(Fact::Ge));
    }
    for hypothesis in &candidates {
        let combined = match (&goal, hypothesis) {
            (Some(Fact::Eq(g)), Fact::Eq(h)) => {
                is_rational_multiple(g, h).map_err(|e| fail(e.to_string()))?.then_some("normalize+hypothesis_linear_combination")
            }
            (Some(Fact::Ge(g)), Fact::Ge(h)) => nonnegative_combination(g, h, false)
                .map_err(|e| fail(e.to_string()))?
                .then_some("normalize+hypothesis_nonnegative_combination"),
            (Some(Fact::Ge(g)), Fact::Eq(h)) => nonnegative_combination(g, h, true)
                .map_err(|e| fail(e.to_string()))?
                .then_some("normalize+hypothesis_nonnegative_combination"),
            _ => None,
        };
        if let Some(method) = combined {
            return Ok(report(method));
        }
    }
    // Equations keep the v0.77 residual form `lhs = rhs`.
    let shown = match (&goal, kernel.equation(body, env)) {
        (Some(Fact::Eq(_)), Ok(Some((lhs, rhs)))) => Some(format!("{lhs} = {rhs}")),
        (Some(goal), _) => Some(goal.to_string()),
        (None, _) => None,
    };
    let hypotheses_text = hypotheses.iter().map(ToString::to_string).collect::<Vec<_>>().join("; ");
    Err(fail(match (shown, hypotheses.is_empty()) {
        (Some(goal), false) => format!("{goal} (hypothesis: {hypotheses_text})"),
        (Some(goal), true) => goal,
        (None, _) => normalized.key(),
    }))
}

/// Successful symbolic proof of a whole statement.
#[derive(Debug, Clone)]
pub struct SymbolicProof {
    pub method: String,
    pub obligations: Vec<(String, ObligationReport)>,
}

/// Symbolic value of a universally quantified variable.
fn variable_sym(name: &str, ty: &str) -> Sym {
    if ty == "Nat" {
        Sym::Nat(Poly::atom(name))
    } else if ty == "Int" {
        Sym::Int(Poly::atom(name))
    } else if ty.starts_with("List<") {
        Sym::List { items: Vec::new(), rest: Some(name.to_string()) }
    } else {
        Sym::Opaque(name.to_string())
    }
}

fn fresh_name(base: &str, taken: &[FnParam]) -> String {
    let mut name = base.to_string();
    while taken.iter().any(|p| p.name == name) {
        name.push('_');
    }
    name
}

/// v0.79: every assignment of the `Bool` variables (one empty assignment when
/// there are none). Proving each case proves the statement for all values.
fn bool_assignments(vars: &[FnParam]) -> Result<Vec<BTreeMap<String, Sym>>, ObligationFailure> {
    let names: Vec<&str> = vars.iter().filter(|v| v.ty == "Bool").map(|v| v.name.as_str()).collect();
    if names.len() > MAX_BOOL_SPLIT_VARIABLES {
        return Err(ObligationFailure {
            obligation: "case split".into(),
            residual: format!("{} Bool variables; at most {MAX_BOOL_SPLIT_VARIABLES} are split by cases", names.len()),
        });
    }
    Ok((0..1u32 << names.len())
        .map(|mask| names.iter().enumerate().map(|(i, name)| (name.to_string(), Sym::Bool(mask & (1 << i) != 0))).collect())
        .collect())
}

fn render_bool_case(case: &BTreeMap<String, Sym>) -> String {
    case.iter().map(|(name, value)| format!("{name} = {}", value.key())).collect::<Vec<_>>().join(", ")
}

/// Environment for `vars`: Bool variables from `case`, overrides from
/// `values`, the rest symbolic.
fn env_for(vars: &[FnParam], case: &BTreeMap<String, Sym>, values: &[(&str, Sym)]) -> BTreeMap<String, Sym> {
    vars.iter()
        .map(|v| {
            let value = values
                .iter()
                .find(|(name, _)| *name == v.name)
                .map(|(_, value)| value.clone())
                .or_else(|| case.get(&v.name).cloned())
                .unwrap_or_else(|| variable_sym(&v.name, &v.ty));
            (v.name.clone(), value)
        })
        .collect()
}

/// Merge the per-case reports of one obligation.
fn merge_case_reports(reports: Vec<ObligationReport>, cases: usize) -> ObligationReport {
    if cases <= 1 {
        return reports.into_iter().next().expect("one case");
    }
    let mut methods: Vec<String> = Vec::new();
    for report in &reports {
        if !methods.contains(&report.method) {
            methods.push(report.method.clone());
        }
    }
    ObligationReport {
        steps: reports.iter().map(|r| r.steps).sum(),
        trace: reports.first().map(|r| r.trace.clone()).unwrap_or_default(),
        method: format!("cases={cases}+{}", methods.join("|")),
    }
}

/// `by normalize`: prove `body` for all values of `vars` (Nat, List<T>;
/// v0.79: Bool by case split).
pub fn prove_by_normalization(
    functions: &BTreeMap<String, FnDecl>,
    lemmas: &[LemmaRule],
    vars: &[FnParam],
    body: &Expr,
) -> Result<SymbolicProof, ObligationFailure> {
    on_kernel_thread(|| normalization_proof(functions, lemmas, vars, body))
}

fn normalization_proof(
    functions: &BTreeMap<String, FnDecl>,
    lemmas: &[LemmaRule],
    vars: &[FnParam],
    body: &Expr,
) -> Result<SymbolicProof, ObligationFailure> {
    let cases = bool_assignments(vars)?;
    let mut reports = Vec::with_capacity(cases.len());
    let mut splits = 0usize;
    for case in &cases {
        let label = if case.is_empty() { "goal".to_string() } else { format!("goal [{}]", render_bool_case(case)) };
        let env = env_for(vars, case, &[]);
        reports.push(prove_with_splits(functions, lemmas, &label, body, &env, 0, &mut splits)?);
    }
    Ok(SymbolicProof { method: "normalize".into(), obligations: vec![("goal".into(), merge_case_reports(reports, cases.len()))] })
}

/// v0.82: deepest nesting of Nat case splits in one obligation.
pub const MAX_CASE_SPLIT_DEPTH: usize = 3;
/// v0.82: largest threshold `c` split on (`n` in `0..=c` or `n >= c + 1`).
pub const MAX_CASE_SPLIT_THRESHOLD: u128 = 16;
/// v0.82: most case obligations created by splitting in one proof.
pub const MAX_CASE_SPLITS: usize = 128;

/// v0.82: the largest literal `c` that the Nat variable `var` is compared
/// with in an undecided condition of the residual: `nat_eq(var,c)`,
/// `nat_eq(c,var)`, `gt(var,c)` or `gt(c,var)`.
fn split_threshold(residual: &str, var: &str) -> Option<u128> {
    let mut best: Option<u128> = None;
    for head in ["nat_eq(", "gt("] {
        let mut rest = residual;
        while let Some(at) = rest.find(head) {
            let preceded_ok = at == 0 || !rest[..at].ends_with(|c: char| c.is_ascii_alphanumeric() || c == '_');
            let args = &rest[at + head.len()..];
            rest = args;
            if !preceded_ok {
                continue;
            }
            let Some(close) = args.find(')') else { continue };
            let Some((a, b)) = args[..close].split_once(',') else { continue };
            // `var` or `c + var` (the normal form of an offset variable).
            let is_var = |text: &str| {
                text == var || text.split_once(" + ").is_some_and(|(c, v)| v == var && c.chars().all(|d| d.is_ascii_digit()))
            };
            let threshold = match (a, b) {
                (a, b) if is_var(a) => b.parse::<u128>().ok(),
                (a, b) if is_var(b) => a.parse::<u128>().ok(),
                _ => None,
            };
            if let Some(c) = threshold.filter(|c| *c <= MAX_CASE_SPLIT_THRESHOLD) {
                best = Some(best.map_or(c, |b| b.max(c)));
            }
        }
    }
    best
}

/// v0.82: prove `body` under `env`; if it does not normalize to true and its
/// residual has an undecided condition comparing a still-symbolic Nat
/// variable `n` with a literal `c`, split `n` into the cases `0, 1, .., c`
/// and `n' + c + 1` (a partition of Nat) and prove every case. Sound because
/// the cases cover all values; bounded by depth, threshold and case count.
fn prove_with_splits(
    functions: &BTreeMap<String, FnDecl>,
    lemmas: &[LemmaRule],
    label: &str,
    body: &Expr,
    env: &BTreeMap<String, Sym>,
    depth: usize,
    splits: &mut usize,
) -> Result<ObligationReport, ObligationFailure> {
    let kernel = Kernel::new(functions, lemmas);
    let failure = match prove_obligation(&kernel, label, body, env, None) {
        Ok(report) => return Ok(report),
        Err(failure) => failure,
    };
    if depth >= MAX_CASE_SPLIT_DEPTH {
        return Err(failure);
    }
    // A variable still bound to a plain atom (not yet split).
    let candidate = env.iter().find_map(|(name, value)| match value {
        Sym::Nat(poly) => {
            let atom = poly.single_atom().filter(|atom| !atom.contains('('))?.to_string();
            (poly == &Poly::atom(&atom)).then_some(())?;
            split_threshold(&failure.residual, &atom).map(|c| (name.clone(), atom, c))
        }
        _ => None,
    });
    let Some((name, atom, threshold)) = candidate else { return Err(failure) };
    let cases = usize::try_from(threshold).unwrap_or(usize::MAX).saturating_add(2);
    if *splits + cases > MAX_CASE_SPLITS {
        return Err(failure);
    }
    *splits += cases;
    let mut steps = 0usize;
    let mut trace = Vec::new();
    for value in 0..=threshold {
        let mut case_env = env.clone();
        case_env.insert(name.clone(), Sym::Nat(Poly::constant(value as i128)));
        let report = prove_with_splits(functions, lemmas, &format!("{label} [{atom} = {value}]"), body, &case_env, depth + 1, splits)?;
        steps += report.steps;
        trace.extend(report.trace.into_iter().take(4));
    }
    let fresh = format!("{atom}_{}", depth + 1);
    let mut case_env = env.clone();
    case_env.insert(
        name.clone(),
        Sym::Nat(Poly::atom(&fresh).add(&Poly::constant(threshold as i128 + 1)).map_err(|e| ObligationFailure {
            obligation: label.to_string(),
            residual: e.to_string(),
        })?),
    );
    let report = prove_with_splits(functions, lemmas, &format!("{label} [{atom} = {fresh} + {}]", threshold + 1), body, &case_env, depth + 1, splits)?;
    steps += report.steps;
    trace.extend(report.trace);
    Ok(ObligationReport { steps, trace, method: format!("cases[{atom}<={threshold}]+{}", report.method) })
}

/// `by induction x`: for `x: Nat`, base `x = 0` and step `x = k + 1` from the
/// hypothesis for `k`; for `x: List<T>` (v0.78), base `x = []` and step
/// `x = cons(h, t)` from the hypothesis for `t`. Other variables are fixed.
pub fn prove_by_induction(
    functions: &BTreeMap<String, FnDecl>,
    lemmas: &[LemmaRule],
    vars: &[FnParam],
    variable: &str,
    body: &Expr,
) -> Result<SymbolicProof, ObligationFailure> {
    prove_by_induction_with(functions, lemmas, vars, variable, body, &InductionOptions::default())
}

/// v0.79: induction with `step s` (Nat only: bases `0 .. s-1`, step `k + s`
/// from the hypotheses for `k .. k+s-1`) and `generalizing` variables (the
/// hypothesis holds for all values of them and is installed as a rewrite
/// rule). Bool variables are split by cases outside the induction.
pub fn prove_by_induction_with(
    functions: &BTreeMap<String, FnDecl>,
    lemmas: &[LemmaRule],
    vars: &[FnParam],
    variable: &str,
    body: &Expr,
    options: &InductionOptions,
) -> Result<SymbolicProof, ObligationFailure> {
    on_kernel_thread(|| induction_proof(functions, lemmas, vars, variable, body, options))
}

fn induction_proof(
    functions: &BTreeMap<String, FnDecl>,
    lemmas: &[LemmaRule],
    vars: &[FnParam],
    variable: &str,
    body: &Expr,
    options: &InductionOptions,
) -> Result<SymbolicProof, ObligationFailure> {
    let setup = |residual: String| ObligationFailure { obligation: "induction".into(), residual };
    let Some(induction_var) = vars.iter().find(|v| v.name == variable) else {
        return Err(setup(format!("unknown variable '{variable}'")));
    };
    let step = options.step.max(1);
    if step > MAX_INDUCTION_STEP {
        return Err(setup(format!("induction step {step}; supported steps are 1..={MAX_INDUCTION_STEP}")));
    }
    if step > 1 && induction_var.ty != "Nat" {
        return Err(setup(format!("`step {step}` needs a Nat induction variable, '{variable}' has type {}", induction_var.ty)));
    }
    for name in &options.generalizing {
        match vars.iter().find(|v| &v.name == name) {
            None => return Err(setup(format!("cannot generalize unknown variable '{name}'"))),
            Some(v) if v.name == variable => return Err(setup(format!("cannot generalize the induction variable '{name}'"))),
            Some(v) if v.ty == "Bool" => {
                return Err(setup(format!("cannot generalize Bool variable '{name}'; Bool variables are split by cases")))
            }
            Some(_) => {}
        }
    }

    // Values of the induction variable: bases, hypotheses, step.
    let (bases, hypotheses, step_value, step_label): (Vec<(Sym, String)>, Vec<Sym>, Sym, String) = if induction_var.ty == "Nat" {
        let k = fresh_name(&format!("{variable}_k"), vars);
        let at = |offset: u32| Sym::Nat(Poly::atom(&k).add(&Poly::constant(i128::from(offset))).expect("small"));
        (
            (0..step).map(|i| (Sym::Nat(Poly::constant(i128::from(i))), format!("{variable} = {i}"))).collect(),
            (0..step).map(at).collect(),
            at(step),
            format!("{variable} = {k} + {step}"),
        )
    } else if let Some(element) = induction_var.ty.strip_prefix("List<").and_then(|t| t.strip_suffix('>')) {
        let h = fresh_name(&format!("{variable}_h"), vars);
        let t = fresh_name(&format!("{variable}_t"), vars);
        (
            vec![(Sym::List { items: Vec::new(), rest: None }, format!("{variable} = []"))],
            vec![Sym::List { items: Vec::new(), rest: Some(t.clone()) }],
            Sym::List { items: vec![variable_sym(&h, element)], rest: Some(t.clone()) },
            format!("{variable} = cons({h}, {t})"),
        )
    } else {
        return Err(setup(format!("cannot induct on '{variable}' of type {}", induction_var.ty)));
    };

    let cases = bool_assignments(vars)?;
    let case_label = |case: &BTreeMap<String, Sym>, label: &str| {
        if case.is_empty() {
            label.to_string()
        } else {
            format!("{label} [{}]", render_bool_case(case))
        }
    };
    let mut base_reports: Vec<Vec<ObligationReport>> = vec![Vec::new(); bases.len()];
    let mut step_reports = Vec::with_capacity(cases.len());
    for case in &cases {
        for (index, (value, label)) in bases.iter().enumerate() {
            let kernel = Kernel::new(functions, lemmas);
            let env = env_for(vars, case, &[(variable, value.clone())]);
            base_reports[index].push(prove_obligation(&kernel, &case_label(case, &format!("base case {label}")), body, &env, None)?);
        }

        let kernel = Kernel::new(functions, lemmas);
        let obligation = case_label(case, &format!("step case {step_label}"));
        let mut facts = Vec::with_capacity(hypotheses.len());
        for hypothesis_value in &hypotheses {
            let hyp_env = env_for(vars, case, &[(variable, hypothesis_value.clone())]);
            let fact = kernel
                .fact(body, &hyp_env)
                .map_err(|e| ObligationFailure { obligation: obligation.clone(), residual: format!("induction hypothesis: {e}") })?;
            // v0.79: a Bool hypothesis with an irreducible normal form is
            // recorded as a truth: the same term in the goal becomes `true`.
            let fact = match fact {
                None => match kernel.normalize(body, &hyp_env, true) {
                    Ok(Sym::Opaque(key)) => {
                        kernel.add_truth(&key);
                        Some(Fact::Holds(key))
                    }
                    _ => None,
                },
                fact => fact,
            };
            // An equational hypothesis is also installed as a ground rewrite
            // when one side is a single atom the other side does not mention.
            // It is normalized before installation, so it cannot rewrite itself.
            if matches!(fact, Some(Fact::Eq(_))) {
                if let Ok(Some((lhs, rhs))) = kernel.equation(body, &hyp_env) {
                    if let Some(atom) = lhs.single_atom().filter(|a| !rhs.mentions(a)) {
                        kernel.add_ground_rule(atom, rhs.clone());
                    } else if let Some(atom) = rhs.single_atom().filter(|a| !lhs.mentions(a)) {
                        kernel.add_ground_rule(atom, lhs.clone());
                    }
                }
                // v0.79: with generalized variables the hypothesis holds for
                // all of their values: install it as a rewrite rule whose
                // pattern variables are the generalized ones.
                if !options.generalizing.is_empty() {
                    if let Ok(rule) = LemmaRule::from_equation(&format!("IH:{variable}"), &options.generalizing, body, functions) {
                        let fixed: BTreeMap<String, Sym> =
                            hyp_env.iter().filter(|(name, _)| !options.generalizing.contains(name)).map(|(n, s)| (n.clone(), s.clone())).collect();
                        kernel.add_local_rule(rule, fixed);
                    }
                }
            }
            facts.extend(fact);
        }
        let env = env_for(vars, case, &[(variable, step_value.clone())]);
        step_reports.push(prove_obligation_with(&kernel, &obligation, body, &env, &facts)?);
    }

    let mut obligations: Vec<(String, ObligationReport)> = bases
        .iter()
        .zip(base_reports)
        .map(|((_, label), reports)| (format!("base:{label}"), merge_case_reports(reports, cases.len())))
        .collect();
    obligations.push((format!("step:{step_label}"), merge_case_reports(step_reports, cases.len())));
    let mut method = format!("induction:{variable}");
    if step > 1 {
        method.push_str(&format!(":step={step}"));
    }
    if !options.generalizing.is_empty() {
        method.push_str(&format!(":generalizing={}", options.generalizing.join(",")));
    }
    Ok(SymbolicProof { method, obligations })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_expr;

    fn expr(text: &str) -> Expr {
        parse_expr(text, 1).expect("parse")
    }

    #[test]
    fn polynomial_normal_form_is_canonical() {
        let functions = BTreeMap::new();
        let kernel = Kernel::new(&functions, &[]);
        let env: BTreeMap<String, Sym> = ["a", "b"].iter().map(|v| (v.to_string(), Sym::Nat(Poly::atom(v)))).collect();
        let lhs = kernel.normalize(&expr("mul(a + b, a + b)"), &env, true).unwrap().key();
        let rhs = kernel.normalize(&expr("mul(a, a) + mul(2, mul(a, b)) + mul(b, b)"), &env, true).unwrap().key();
        assert_eq!(lhs, rhs);
        assert!(matches!(kernel.normalize(&expr("nat_eq(a + 1, a)"), &env, true).unwrap(), Sym::Bool(false)));
        assert!(matches!(kernel.normalize(&expr("a + 1 > a"), &env, true).unwrap(), Sym::Bool(true)));
        assert!(matches!(kernel.normalize(&expr("pred(a + 3)"), &env, true).unwrap(), Sym::Nat(_)));
        // Undecidable without more information: stays symbolic.
        assert!(matches!(kernel.normalize(&expr("nat_eq(a, b)"), &env, true).unwrap(), Sym::Opaque(_)));
        assert!(matches!(kernel.normalize(&expr("a > b"), &env, true).unwrap(), Sym::Opaque(_)));
    }

    #[test]
    fn truncating_operations_are_simplified_only_when_safe() {
        let functions = BTreeMap::new();
        let kernel = Kernel::new(&functions, &[]);
        let env: BTreeMap<String, Sym> = [("n".to_string(), Sym::Nat(Poly::atom("n")))].into_iter().collect();
        // pred(n) and monus(n, 1) are not n - 1 when n = 0.
        assert_eq!(kernel.normalize(&expr("pred(n)"), &env, true).unwrap().key(), "pred(n)");
        assert_eq!(kernel.normalize(&expr("monus(n, 1)"), &env, true).unwrap().key(), "monus(n,1)");
        assert_eq!(kernel.normalize(&expr("monus(n + 2, 1)"), &env, true).unwrap().key(), "1 + n");
        assert_eq!(kernel.normalize(&expr("monus(n, n + 1)"), &env, true).unwrap().key(), "0");
    }

    #[test]
    fn rational_multiple_check() {
        let a = Poly::atom("x").scale(2).unwrap().sub(&Poly::atom("y")).unwrap();
        let b = a.scale(3).unwrap();
        assert!(is_rational_multiple(&b, &a).unwrap());
        assert!(is_rational_multiple(&a, &b).unwrap());
        assert!(!is_rational_multiple(&Poly::atom("x"), &a).unwrap());
    }

    fn eval(poly: &Poly, x: i128, y: i128) -> i128 {
        poly.0
            .iter()
            .map(|(monomial, coefficient)| {
                coefficient
                    * monomial.0.iter().map(|(atom, power)| if atom == "x" { x } else { y }.pow(*power)).product::<i128>()
            })
            .sum()
    }

    /// v0.78: semantic check of the combination rule on random polynomials of
    /// degree <= 2 over two Nat atoms: whenever the rule accepts, the goal is
    /// nonnegative at every grid point where the hypothesis holds.
    #[test]
    fn nonnegative_combination_is_sound_on_a_grid() {
        let mut state: u64 = 0x5DEE_CE66_D1CE_4E5B;
        let mut next = move |n: u64| {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            (state % n) as i128
        };
        let monomials = [Poly::constant(1), Poly::atom("x"), Poly::atom("y")];
        let random_poly = |next: &mut dyn FnMut(u64) -> i128| {
            let mut poly = Poly::constant(0);
            for a in &monomials {
                for b in &monomials {
                    if next(3) == 0 {
                        poly = poly.add(&a.mul(b).unwrap().scale(next(7) - 3).unwrap()).unwrap();
                    }
                }
            }
            poly
        };
        let (mut accepted_ge, mut accepted_eq) = (0, 0);
        for _ in 0..20000 {
            let goal = random_poly(&mut next);
            let hyp = if next(2) == 0 { random_poly(&mut next) } else { goal.add(&random_poly(&mut next)).unwrap() };
            if nonnegative_combination(&goal, &hyp, false).unwrap() {
                accepted_ge += 1;
                for x in 0..6 {
                    for y in 0..6 {
                        if eval(&hyp, x, y) >= 0 {
                            assert!(eval(&goal, x, y) >= 0, "unsound: goal {goal} >= 0 from {hyp} >= 0 at x={x}, y={y}");
                        }
                    }
                }
            }
            if nonnegative_combination(&goal, &hyp, true).unwrap() {
                accepted_eq += 1;
                for x in 0..6 {
                    for y in 0..6 {
                        if eval(&hyp, x, y) == 0 {
                            assert!(eval(&goal, x, y) >= 0, "unsound: goal {goal} >= 0 from {hyp} = 0 at x={x}, y={y}");
                        }
                    }
                }
            }
        }
        assert!(accepted_ge > 1000 && accepted_eq > accepted_ge, "{accepted_ge} / {accepted_eq}");
        // A negative multiple of an inequality proves nothing.
        assert!(!nonnegative_combination(&Poly::atom("x").scale(-1).unwrap(), &Poly::atom("x"), false).unwrap());
        assert!(nonnegative_combination(&Poly::atom("x").scale(-1).unwrap(), &Poly::atom("x"), true).unwrap());
    }
}
