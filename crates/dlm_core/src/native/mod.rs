//! Native application execution domain. Source, checked core and representation
//! remain separate from the mathematical (passport-carrying) value domain.
pub mod bridge;
pub mod build;
pub mod check;
pub mod emit;
pub mod syntax;
pub mod vm;

use crate::{Diagnostic, DiagnosticKind, Program};
use std::collections::{BTreeMap, BTreeSet};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Scalar {
    U8,
    U16,
    U32,
    U64,
    I8,
    I16,
    I32,
    I64,
    F32,
    F64,
    USize,
    ISize,
}
impl Scalar {
    pub fn parse(s: &str) -> Option<Self> {
        Some(match s {
            "U8" => Self::U8,
            "U16" => Self::U16,
            "U32" => Self::U32,
            "U64" => Self::U64,
            "I8" => Self::I8,
            "I16" => Self::I16,
            "I32" => Self::I32,
            "I64" => Self::I64,
            "F32" => Self::F32,
            "F64" => Self::F64,
            "USize" => Self::USize,
            "ISize" => Self::ISize,
            _ => return None,
        })
    }
    pub fn name(self) -> &'static str {
        match self {
            Self::U8 => "U8",
            Self::U16 => "U16",
            Self::U32 => "U32",
            Self::U64 => "U64",
            Self::I8 => "I8",
            Self::I16 => "I16",
            Self::I32 => "I32",
            Self::I64 => "I64",
            Self::F32 => "F32",
            Self::F64 => "F64",
            Self::USize => "USize",
            Self::ISize => "ISize",
        }
    }
    pub fn c(self) -> &'static str {
        match self {
            Self::U8 => "uint8_t",
            Self::U16 => "uint16_t",
            Self::U32 => "uint32_t",
            Self::U64 => "uint64_t",
            Self::I8 => "int8_t",
            Self::I16 => "int16_t",
            Self::I32 => "int32_t",
            Self::I64 => "int64_t",
            Self::F32 => "float",
            Self::F64 => "double",
            Self::USize => "size_t",
            Self::ISize => "intptr_t",
        }
    }
    pub fn bits(self) -> u32 {
        match self {
            Self::U8 | Self::I8 => 8,
            Self::U16 | Self::I16 => 16,
            Self::U32 | Self::I32 | Self::F32 => 32,
            _ => 64,
        }
    }
    pub fn float(self) -> bool {
        matches!(self, Self::F32 | Self::F64)
    }
    pub fn signed(self) -> bool {
        matches!(
            self,
            Self::I8 | Self::I16 | Self::I32 | Self::I64 | Self::ISize
        )
    }
}

fn integer_literal(text: &str) -> Option<u128> {
    let text = text.replace('_', "");
    if let Some(hex) = text.strip_prefix("0x").or_else(|| text.strip_prefix("0X")) {
        u128::from_str_radix(hex, 16).ok()
    } else {
        text.parse().ok()
    }
}

fn float_literal(text: &str, scalar: Scalar) -> Option<f64> {
    let text = text.replace('_', "");
    if text.starts_with("0x") || text.starts_with("0X") {
        let n = u64::try_from(integer_literal(&text)?).ok()?;
        Some(if scalar == Scalar::F32 {
            (n as f32) as f64
        } else {
            n as f64
        })
    } else if scalar == Scalar::F32 {
        text.parse::<f32>().ok().map(f64::from)
    } else {
        text.parse().ok()
    }
}

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub enum Ty {
    Scalar(Scalar),
    Bool,
    Unit,
    Str,
    Window,
    ThreadBound(Box<Ty>),
    Resource(Box<Ty>, String),
    Record(String),
    Array(Box<Ty>, usize),
    Buffer(Box<Ty>),
    Boxed(Box<Ty>),
    Slice(Box<Ty>, bool),
    Split(Box<Ty>),
    Ref(Box<Ty>, bool),
    Ptr(Box<Ty>),
    NonNull(Box<Ty>),
    BorrowedPtr(Box<Ty>),
    ForeignBuffer(Box<Ty>),
    Handle,
    FnPtr(Vec<Ty>, Box<Ty>),
    Simd(Scalar, usize),
    Atomic(Box<Ty>),
}
impl Ty {
    pub fn scalar(&self) -> Option<Scalar> {
        if let Self::Scalar(s) = self {
            Some(*s)
        } else {
            None
        }
    }
    pub fn element(&self) -> Option<&Ty> {
        match self {
            Self::ThreadBound(t)
            | Self::Resource(t, _)
            | Self::Array(t, _)
            | Self::Buffer(t)
            | Self::Split(t)
            | Self::Slice(t, _)
            | Self::Ref(t, _)
            | Self::Ptr(t)
            | Self::NonNull(t)
            | Self::BorrowedPtr(t)
            | Self::ForeignBuffer(t)
            | Self::Boxed(t) => Some(t),
            _ => None,
        }
    }
    pub fn borrowed(&self) -> bool {
        matches!(
            self,
            Self::Slice(..)
                | Self::Split(_)
                | Self::Ref(..)
                | Self::BorrowedPtr(..)
                | Self::ForeignBuffer(..)
        )
    }
    pub fn name(&self) -> String {
        match self {
            Self::Scalar(s) => s.name().into(),
            Self::Bool => "Bool".into(),
            Self::Unit => "Unit".into(),
            Self::Str => "Utf8".into(),
            Self::Window => "Window".into(),
            Self::Record(s) => s.clone(),
            Self::Handle => "OpaqueHandle".into(),
            Self::Resource(t, finalizer) => format!("Resource<{},{}>", t.name(), finalizer),
            Self::Simd(s, n) => format!("Simd<{},{}>", s.name(), n),
            Self::Array(t, n) => format!("Array<{},{}>", t.name(), n),
            Self::FnPtr(p, r) => format!(
                "FnPtr<{}>",
                p.iter()
                    .chain(std::iter::once(&**r))
                    .map(Ty::name)
                    .collect::<Vec<_>>()
                    .join(",")
            ),
            _ => {
                let (n, t) = match self {
                    Self::ThreadBound(t) => ("ThreadBound", t),
                    Self::Split(t) => ("Split", t),
                    Self::Buffer(t) => ("Buffer", t),
                    Self::Boxed(t) => ("Box", t),
                    Self::Slice(t, false) => ("Slice", t),
                    Self::Slice(t, true) => ("MutSlice", t),
                    Self::Ref(t, false) => ("Ref", t),
                    Self::Ref(t, true) => ("MutRef", t),
                    Self::Ptr(t) => ("Ptr", t),
                    Self::NonNull(t) => ("NonNull", t),
                    Self::BorrowedPtr(t) => ("BorrowedPtr", t),
                    Self::ForeignBuffer(t) => ("ForeignBuffer", t),
                    Self::Atomic(t) => ("Atomic", t),
                    _ => unreachable!(),
                };
                format!("{n}<{}>", t.name())
            }
        }
    }
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Unit {
    pub records: Vec<Record>,
    pub functions: Vec<Function>,
    pub trusted: BTreeSet<String>,
    pub ordinary_program: Option<Box<Program>>,
}
impl Unit {
    pub fn is_empty(&self) -> bool {
        self.records.is_empty() && self.functions.is_empty()
    }
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Record {
    pub name: String,
    pub fields: Vec<(String, Ty)>,
    pub union: bool,
    pub packed: bool,
    pub align: Option<usize>,
    pub version: u32,
    pub line: usize,
    pub source: crate::SourceFile,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Function {
    pub name: String,
    pub params: Vec<(String, Ty)>,
    pub ret: Ty,
    pub borrow_result: Option<String>,
    pub body: Option<Vec<Stmt>>,
    pub export: Option<String>,
    pub library: Option<String>,
    pub ordinary: Option<String>,
    pub unsafe_: bool,
    pub guarantees: BTreeSet<String>,
    pub line: usize,
    pub source: crate::SourceFile,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Expr {
    pub op: Op,
    pub ty: Option<Ty>,
    pub line: usize,
}
impl Expr {
    pub fn new(op: Op, line: usize) -> Self {
        Self { op, ty: None, line }
    }
    pub fn ty(&self) -> &Ty {
        self.ty.as_ref().expect("verified native core expression")
    }
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Op {
    Number(String),
    Bool(bool),
    Text(String),
    Name(String),
    Unary(String, Box<Expr>),
    Binary(String, Box<Expr>, Box<Expr>),
    Call(String, Vec<Ty>, Vec<Expr>),
    Invoke(Box<Expr>, Vec<Expr>),
    Field(Box<Expr>, String),
    Index(Box<Expr>, Box<Expr>),
    IndexProven(Box<Expr>, Box<Expr>),
    Borrow(Box<Expr>, bool),
    Move(Box<Expr>),
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Stmt {
    Let {
        name: String,
        ty: Ty,
        mutable: bool,
        value: Expr,
        line: usize,
    },
    Set(Expr, Expr),
    Expr(Expr),
    Return(Option<Expr>, usize),
    If(Expr, Vec<Stmt>, Vec<Stmt>),
    While(Expr, Vec<Stmt>),
    For(String, Expr, Expr, Vec<Stmt>),
    Break(usize),
    Continue(usize),
    Unsafe(Vec<Stmt>),
    Block(Vec<Stmt>),
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Effects {
    pub alloc: bool,
    pub blocking: bool,
    pub io: bool,
    pub fail: bool,
    pub unbounded: bool,
    pub unsafe_memory: bool,
    pub affine: bool,
    pub calls: BTreeSet<String>,
}
impl Effects {
    pub fn join(&mut self, x: &Self) {
        self.alloc |= x.alloc;
        self.blocking |= x.blocking;
        self.io |= x.io;
        self.fail |= x.fail;
        self.unbounded |= x.unbounded;
        self.unsafe_memory |= x.unsafe_memory;
        self.affine |= x.affine;
    }
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Layout {
    pub size: usize,
    pub align: usize,
    pub offsets: Vec<usize>,
    pub version: u32,
}
/// A typed MIR owns resolved local types and operations. The backend accepts
/// only this verified form, together with independently computed ABI layouts.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Mir {
    pub unit: Unit,
    pub layouts: BTreeMap<String, Layout>,
    pub effects: BTreeMap<String, Effects>,
}
impl Mir {
    pub fn is_empty(&self) -> bool {
        self.unit.is_empty()
    }
    pub fn render(&self) -> String {
        let mut s = String::from("native MIR (x86-64, explicit scalar/layout/ownership)\n");
        for f in &self.unit.functions {
            s.push_str(&format!(
                "  {}({}) -> {} {:?}\n",
                f.name,
                f.params
                    .iter()
                    .map(|(n, t)| format!("{n}:{}", t.name()))
                    .collect::<Vec<_>>()
                    .join(","),
                f.ret.name(),
                self.effects.get(&f.name)
            ));
        }
        s
    }
}

pub fn error(line: usize, msg: impl Into<String>) -> Diagnostic {
    Diagnostic::error(
        DiagnosticKind::CompileError,
        Some(line),
        format!("native: {}", msg.into()),
    )
}
pub fn collect(program: &Program) -> Unit {
    let mut u = Unit::default();
    for (i, m) in program.modules.iter().enumerate() {
        u.records.extend(m.native.records.clone());
        u.functions.extend(
            m.native
                .functions
                .iter()
                .filter(|f| f.name != "main" || i + 1 == program.modules.len())
                .cloned(),
        );
        u.trusted.extend(m.native.trusted.clone());
    }
    if u.functions.iter().any(|f| f.ordinary.is_some()) {
        let mut ordinary = program.clone();
        for m in &mut ordinary.modules {
            m.native = Unit::default();
        }
        u.ordinary_program = Some(Box::new(ordinary));
    }
    u
}
pub fn check_program(program: &Program) -> Result<Mir, Vec<Diagnostic>> {
    check::check(collect(program))
}
