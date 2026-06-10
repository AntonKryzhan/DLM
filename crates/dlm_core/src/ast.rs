#[derive(Debug, Clone)]
pub struct Module {
    pub name: String,
    pub imports: Vec<ImportDecl>,
    pub items: Vec<ModuleItem>,
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
}

#[derive(Debug, Clone)]
pub enum TheoryItem {
    Let(LetDecl),
    Expr(Expr),
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

#[derive(Debug, Clone)]
pub struct Expr {
    pub kind: ExprKind,
    pub line: usize,
}

#[derive(Debug, Clone)]
pub enum ExprKind {
    IntLiteral(String),
    Ident(String),
    QualifiedIdent { theory: String, name: String },
    Power { base: Box<Expr>, exp: Box<Expr> },
    Add { lhs: Box<Expr>, rhs: Box<Expr> },
    Call { name: String, args: Vec<Expr> },
    CompareGt { lhs: Box<Expr>, rhs: Box<Expr> },
}

impl Expr {
    pub fn int(value: impl Into<String>, line: usize) -> Self {
        Self {
            kind: ExprKind::IntLiteral(value.into()),
            line,
        }
    }

    pub fn ident(name: impl Into<String>, line: usize) -> Self {
        Self {
            kind: ExprKind::Ident(name.into()),
            line,
        }
    }
}
