use crate::diagnostics::{SourceFile, SourceSpan};
use crate::ids::{DeclId, FileId};

#[derive(Debug, Clone)]
pub struct Module {
    pub name: String,
    pub imports: Vec<ImportDecl>,
    pub items: Vec<ModuleItem>,
    pub source: SourceFile,
}

impl Module {
    /// Linking gives independently parsed modules distinct local identities.
    /// Expression IDs and physical positions are preserved.
    pub(crate) fn set_file_id(&mut self, id: FileId) {
        self.source.id = id;
        for item in &mut self.items {
            if let ModuleItem::Theory(theory) = item {
                for item in &mut theory.items {
                    match item {
                        TheoryItem::Let(decl) => decl.expr.set_file_id(id),
                        TheoryItem::Expr(expr) => expr.set_file_id(id),
                        TheoryItem::Fn(decl) => decl.body.set_file_id(id),
                        TheoryItem::Statement(statement) => statement.body.set_file_id(id),
                        TheoryItem::Record(_) => {}
                    }
                }
            }
        }
    }
}

#[derive(Debug, Clone)]
pub struct ImportDecl {
    pub path: String,
}

#[derive(Debug, Clone)]
pub enum ModuleItem {
    Theory(TheoryDecl),
    Bridge(BridgeDecl),
}

#[derive(Debug, Clone)]
pub struct TheoryDecl {
    pub name: String,
    pub items: Vec<TheoryItem>,
    /// v0.80: `theory Name extends Base` — inherits Base's declarations
    /// (fns, records, theorems, axioms, conjectures), not its values.
    pub extends: Option<String>,
    /// v0.80: names declared `private`; they are not visible to theories of
    /// other modules that extend this one.
    pub private: std::collections::BTreeSet<String>,
    /// v0.80: line of the `theory` header.
    pub line: usize,
}

#[derive(Debug, Clone)]
pub enum TheoryItem {
    Let(LetDecl),
    Expr(Expr),
    Fn(FnDecl),
    Record(RecordDecl),
    Statement(StatementItem),
}

/// v0.76: kind of a surface statement.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StatementItemKind {
    /// `theorem name: P` or `theorem name(x: T, ...) below N: P` — proved by
    /// computation (closed) or exhaustion (bounded).
    Theorem,
    /// `axiom name(x: T, ...): P` — admitted without proof, Axiom-tainted.
    Axiom,
    /// `conjecture name(x: T, ...) tested N: P` — unbounded claim with test
    /// evidence below N. Never a theorem.
    Conjecture,
}

impl StatementItemKind {
    pub fn keyword(self) -> &'static str {
        match self {
            StatementItemKind::Theorem => "theorem",
            StatementItemKind::Axiom => "axiom",
            StatementItemKind::Conjecture => "conjecture",
        }
    }
}

/// v0.76: `theorem` / `axiom` / `conjecture` item.
#[derive(Debug, Clone)]
pub struct StatementItem {
    pub kind: StatementItemKind,
    pub name: String,
    /// Universally quantified variables.
    pub params: Vec<FnParam>,
    /// `below N` (theorem) or `tested N` (conjecture): Nat variables range over `0..N`.
    pub bound: Option<u64>,
    pub body: Expr,
    /// v0.77: `by normalize` / `by induction x`, optionally `using lemma, ...`.
    pub proof: Option<ProofHint>,
    pub line: usize,
}

/// v0.77: symbolic proof tactic of an unbounded theorem.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ProofTactic {
    Normalize,
    Induction(String),
}

/// v0.77: `by <tactic> [using lemma, ...]`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProofHint {
    pub tactic: ProofTactic,
    pub lemmas: Vec<String>,
    /// v0.79: `step s` and `generalizing x, ...` of `by induction n`.
    pub induction: InductionOptions,
}

/// v0.79: options of `by induction n [step s] [generalizing x, ...]`.
///
/// `step = s` proves the bases `P(0) .. P(s-1)` and the step
/// `P(k) and .. and P(k+s-1) -> P(k+s)`. `generalizing` quantifies the
/// induction hypothesis over the listed variables instead of fixing them.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InductionOptions {
    pub step: u32,
    pub generalizing: Vec<String>,
}

impl Default for InductionOptions {
    fn default() -> Self {
        InductionOptions { step: 1, generalizing: Vec::new() }
    }
}

/// v0.74: `record Name(field: T, ...)` — a nominal record type with an
/// ordered field list. Field types are canonical ordinary type strings.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RecordDecl {
    pub id: DeclId,
    pub name: String,
    pub fields: Vec<FnParam>,
    /// Closed enum variants and their payload arities; empty for ordinary records.
    pub enum_variants: Vec<(String, usize)>,
    pub line: usize,
    /// v0.110: `record Name<T, ...>` — a template expanded by `crate::generics`.
    pub type_params: Vec<String>,
    /// v0.111: `T: Eq` / `T: Ord` bounds, parallel to `type_params`.
    pub type_bounds: Vec<Vec<String>>,
}

/// v0.73: `fn name(p: T, ...) -> R = body` — an ordinary, non-recursive
/// function. Types are stored as canonical ordinary type strings.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FnDecl {
    pub id: DeclId,
    pub name: String,
    pub params: Vec<FnParam>,
    pub ret: String,
    /// v0.74: `decreasing <param> [fuel N]` — well-founded measure for a
    /// recursive function. `None` for non-recursive functions.
    pub measure: Option<FnMeasure>,
    pub body: Expr,
    pub line: usize,
    /// v0.81: names of all members of the `mutual { ... }` block this
    /// function belongs to (itself included); empty outside a block.
    pub mutual: Vec<String>,
    /// v0.103: parameters declared `&ByteBuf` (type `ByteBuf`, borrowed for
    /// the duration of the call instead of moved).
    pub borrowed: Vec<String>,
    /// v0.110: `fn name<T, ...>` — a template expanded by `crate::generics`.
    pub type_params: Vec<String>,
    /// v0.111: `T: Eq` / `T: Ord` bounds, parallel to `type_params`.
    pub type_bounds: Vec<Vec<String>>,
    /// v0.112: member of a type class or of one of its instances.
    pub class_info: Option<ClassInfo>,
}

/// v0.112: role of a fn declared inside `class` / `instance` blocks.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ClassInfo {
    /// A method of `class Name<T>: Super + ... { fn m(...) -> R [= default] }`; `default`:
    /// the body is used by instances that omit the method (v0.113); `supers`: superclasses.
    Method { class: String, param: String, default: bool, supers: Vec<String> },
    /// A method of `instance Class<Type> { ... }`; `ty` is the canonical instance type,
    /// which may name the instance's own type parameters (`instance<A> Show<List<A>>`).
    Impl { class: String, method: String, ty: String },
}

/// v0.74: declared recursion measure of a function.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FnMeasure {
    /// Parameters whose value tuple strictly decreases (lexicographically,
    /// v0.75) on every recursive call. One element for a simple measure.
    pub params: Vec<String>,
    /// Explicit operational fuel (maximum recursion depth); `None` = default.
    pub fuel: Option<u64>,
}

impl FnMeasure {
    /// v0.125: `fn f(...) -> T loop = body` — no measure: every recursive
    /// call is a self tail call, executed as an iteration in constant stack.
    /// Termination is not established (the function is partial).
    pub fn tail_loop() -> Self {
        Self { params: Vec::new(), fuel: None }
    }

    pub fn is_loop(&self) -> bool {
        self.params.is_empty()
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FnParam {
    pub name: String,
    pub ty: String,
}

#[derive(Debug, Clone)]
pub struct LetDecl {
    pub name: String,
    pub expr: Expr,
    pub line: usize,
}

#[derive(Debug, Clone)]
pub struct BridgeDecl {
    pub name: String,
    pub source: String,
    pub target: String,
    pub kind: BridgeKind,
    pub line: usize,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum BridgeKind {
    Definitional,
    Conservative,
    Quote,
    Transport,
    Soundness,
    Reflection,
    Migration,
    Materialize,
    Unsafe,
    Unknown(String),
}

impl BridgeKind {
    pub fn as_str(&self) -> &str {
        match self {
            BridgeKind::Definitional => "definitional",
            BridgeKind::Conservative => "conservative",
            BridgeKind::Quote => "quote",
            BridgeKind::Transport => "transport",
            BridgeKind::Soundness => "soundness",
            BridgeKind::Reflection => "reflection",
            BridgeKind::Migration => "migration",
            BridgeKind::Materialize => "materialize",
            BridgeKind::Unsafe => "unsafe",
            BridgeKind::Unknown(name) => name.as_str(),
        }
    }
}

/// v0.87: identity of a source expression, unique within the process. Clones
/// keep the id (a desugared `match` may repeat its scrutinee), so every copy of
/// a node refers to the same checked fact. Not a stable export identity.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ExprId(pub u64);

impl ExprId {
    pub fn fresh() -> Self {
        static NEXT: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(1);
        ExprId(NEXT.fetch_add(1, std::sync::atomic::Ordering::Relaxed))
    }
}

/// An expression. `line` is the first line of its logical line (diagnostics
/// keep using it); `span` is the exact physical source range (1-based UTF-8
/// byte columns, end exclusive) when the parser produced the node, the line
/// alone for nodes built elsewhere, and the enclosing construct for nodes
/// generated by desugaring. Equality compares `kind` and `line` only.
#[derive(Debug, Clone)]
pub struct Expr {
    pub kind: ExprKind,
    pub line: usize,
    pub id: ExprId,
    pub span: SourceSpan,
}

impl PartialEq for Expr {
    fn eq(&self, other: &Self) -> bool {
        self.kind == other.kind && self.line == other.line
    }
}

impl Eq for Expr {}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ExprKind {
    IntLiteral(String),
    Ident(String),
    QualifiedIdent { theory: String, name: String },
    Power { base: Box<Expr>, exp: Box<Expr> },
    Add { lhs: Box<Expr>, rhs: Box<Expr> },
    Call { name: String, args: Vec<Expr> },
    CompareGt { lhs: Box<Expr>, rhs: Box<Expr> },
    /// v0.75: `fun(x: T, y: U, body)` — anonymous pure total function.
    Lambda { params: Vec<FnParam>, body: Box<Expr> },
    /// v0.82: `let name = value in body` — `value` is evaluated once and bound
    /// in `body`. `match` is desugared into `let` and `if_then_else`.
    Let { name: String, value: Box<Expr>, body: Box<Expr> },
    /// v0.83: `"text"` — a Text literal (escapes already decoded).
    TextLiteral(String),
}

impl Expr {
    fn set_file_id(&mut self, id: FileId) {
        self.span.file_id = id;
        match &mut self.kind {
            ExprKind::Power { base: a, exp: b } | ExprKind::Add { lhs: a, rhs: b } | ExprKind::CompareGt { lhs: a, rhs: b } => {
                a.set_file_id(id);
                b.set_file_id(id);
            }
            ExprKind::Call { args, .. } => args.iter_mut().for_each(|a| a.set_file_id(id)),
            ExprKind::Lambda { body, .. } => body.set_file_id(id),
            ExprKind::Let { value, body, .. } => {
                value.set_file_id(id);
                body.set_file_id(id);
            }
            ExprKind::IntLiteral(_) | ExprKind::Ident(_) | ExprKind::QualifiedIdent { .. } | ExprKind::TextLiteral(_) => {}
        }
    }

    /// A new node with a fresh id and a line-only span.
    pub fn new(kind: ExprKind, line: usize) -> Self {
        Self { kind, line, id: ExprId::fresh(), span: SourceSpan::line(line) }
    }

    pub fn int(value: impl Into<String>, line: usize) -> Self {
        Self::new(ExprKind::IntLiteral(value.into()), line)
    }

    pub fn ident(name: impl Into<String>, line: usize) -> Self {
        Self::new(ExprKind::Ident(name.into()), line)
    }
}
