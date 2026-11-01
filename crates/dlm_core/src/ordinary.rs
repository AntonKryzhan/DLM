//! v0.73 — Ordinary mathematics surface types.
//!
//! The Stage 2 core layers (partiality, sequence, structural, traversal,
//! function contracts) describe ordinary values with canonical type strings.
//! This module is the single place where `.dlm` surface type syntax is parsed
//! and normalized to those canonical strings.
//!
//! Surface grammar (both `<...>` and call form `(...)` are accepted):
//!
//! ```text
//! Type := Nat | Bool | Text | ByteBuf                         (ByteBuf: v0.102, owned)
//!       | Option<Type> | List<Type> | Sequence<Type>
//!       | Result<Type, Type>
//!       | Product<Type, Type> | Pair<Type, Type> | ProductType<Type * Type>
//!       | Sum<Type, Type> | SumType<Type + Type>                 (v0.74)
//!       | Record<Name> | Name                                     (v0.74, Name starts uppercase)
//! ```
//!
//! Canonical output:
//!
//! ```text
//! Nat, Bool, Text, Option<T>, List<T>, Sequence<T>, Result<T,E>, ProductType<A*B>,
//! SumType<A+B>, Record<Name>
//! ```
//!
//! Record names are resolved by the checker against declared `record` items;
//! this module only parses them.

use crate::ast::{Expr, ExprKind, StatementItem, StatementItemKind};
use crate::passport::{ordinary_type_name, Passport};

/// A parsed ordinary surface type.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum OrdinaryType {
    Nat,
    /// v0.85: signed integers (exact i128 at run time).
    Int,
    Bool,
    Text,
    Option(Box<OrdinaryType>),
    List(Box<OrdinaryType>),
    Sequence(Box<OrdinaryType>),
    Result(Box<OrdinaryType>, Box<OrdinaryType>),
    Product(Box<OrdinaryType>, Box<OrdinaryType>),
    Sum(Box<OrdinaryType>, Box<OrdinaryType>),
    Record(String),
    /// Compiler-only nominal identity, resolved from a checked record declaration.
    RecordRef(crate::ids::RecordId),
    /// v0.75: `Fn<P1, ..., Pn, R>` — pure total function value.
    Fn(Vec<OrdinaryType>, Box<OrdinaryType>),
    EffectFn(Vec<OrdinaryType>, Box<OrdinaryType>, crate::callback::EffectBounds),
    CallArgument(Box<OrdinaryType>),
    CallResult(Box<OrdinaryType>),
    CollectionItem(Box<OrdinaryType>),
    ResultValue(Box<OrdinaryType>),
    ResultError(Box<OrdinaryType>),
    /// v0.102: owned mutable byte buffer (stage D). Moved or borrowed, never
    /// copied; never an element, field, Fn type part or theory-level value
    /// (`docs/OWNERSHIP.md`).
    ByteBuf,
    /// v0.104: owned text builder (stage D), the same ownership rules.
    TextBuf,
    /// v0.107: dense natural number (little-endian 32-bit limbs, at most
    /// `runtime::BIGNAT_MAX_BITS` bits); an immutable copyable value (N14).
    BigNat,
    /// v0.109: owned external file handle (stage D). Linear: every handle is
    /// closed with `file_close`, moved or returned on every path.
    File,
    /// v0.114: IEEE-754 binary64 machine float.
    Float,
    /// v0.114: dense immutable vector of `Float` (at most `runtime::FLOAT_VEC_MAX` elements).
    FloatVec,
    /// v0.115: dense immutable vector of `Nat` (at most `graph::VEC_MAX` elements).
    NatVec,
    /// v0.122: ordered maps `Nat → Nat` and `Text → Nat`.
    NatMap,
    TextMap,
}

/// Names that are never record names.
const RESERVED_TYPE_NAMES: &[&str] = &[
    "Nat", "Int", "Bool", "Text", "Option", "List", "Sequence", "Result", "Product", "Pair", "ProductType", "Sum",
    "SumType", "Record", "Unit", "Fn", "FnEffect", "ArgOf", "ResultOf", "ItemOf", "OkOf", "ErrorOf", "ByteBuf", "TextBuf", "BigNat", "File", "Float", "FloatVec", "NatVec", "NatMap", "TextMap",
];

impl OrdinaryType {
    /// Canonical type string shared with the Stage 2 core layers.
    pub fn canonical(&self) -> String {
        let mut current = self;
        let mut out = String::new();
        let mut depth = 0;
        loop {
            let (head, inner) = match current {
                Self::List(t) => ("List<", t), Self::Option(t) => ("Option<", t),
                Self::Sequence(t) => ("Sequence<", t), _ => break,
            };
            out.push_str(head); depth += 1; current = inner;
        }
        out.push_str(&current.canonical_inner());
        out.extend(std::iter::repeat_n('>', depth));
        out
    }

    fn canonical_inner(&self) -> String {
        match self {
            OrdinaryType::Nat => "Nat".to_string(),
            OrdinaryType::Int => "Int".to_string(),
            OrdinaryType::Bool => "Bool".to_string(),
            OrdinaryType::Text => "Text".to_string(),
            OrdinaryType::ByteBuf => "ByteBuf".to_string(),
            OrdinaryType::TextBuf => "TextBuf".to_string(),
            OrdinaryType::BigNat => "BigNat".to_string(),
            OrdinaryType::File => "File".to_string(),
            OrdinaryType::Float => "Float".to_string(),
            OrdinaryType::FloatVec => "FloatVec".to_string(),
            OrdinaryType::NatVec => "NatVec".to_string(),
            OrdinaryType::NatMap => "NatMap".to_string(),
            OrdinaryType::TextMap => "TextMap".to_string(),
            OrdinaryType::Option(item) => format!("Option<{}>", item.canonical()),
            OrdinaryType::List(item) => format!("List<{}>", item.canonical()),
            OrdinaryType::Sequence(item) => format!("Sequence<{}>", item.canonical()),
            OrdinaryType::Result(ok, err) => format!("Result<{},{}>", ok.canonical(), err.canonical()),
            OrdinaryType::Product(lhs, rhs) => format!("ProductType<{}*{}>", lhs.canonical(), rhs.canonical()),
            OrdinaryType::Sum(lhs, rhs) => format!("SumType<{}+{}>", lhs.canonical(), rhs.canonical()),
            OrdinaryType::Record(name) => format!("Record<{name}>"),
            OrdinaryType::RecordRef(id) => format!("Record<{id}>"),
            OrdinaryType::Fn(params, ret) => {
                let mut parts: Vec<String> = params.iter().map(OrdinaryType::canonical).collect();
                parts.push(ret.canonical());
                format!("Fn<{}>", parts.join(","))
            }
            OrdinaryType::EffectFn(params, ret, bounds) => {
                let mut parts = vec![bounds.canonical()];
                parts.extend(params.iter().map(OrdinaryType::canonical));
                parts.push(ret.canonical());
                format!("FnEffect<{}>", parts.join(","))
            }
            OrdinaryType::CallArgument(t) => format!("ArgOf<{}>", t.canonical()),
            OrdinaryType::CallResult(t) => format!("ResultOf<{}>", t.canonical()),
            OrdinaryType::CollectionItem(t) => format!("ItemOf<{}>", t.canonical()),
            OrdinaryType::ResultValue(t) => format!("OkOf<{}>", t.canonical()),
            OrdinaryType::ResultError(t) => format!("ErrorOf<{}>", t.canonical()),
        }
    }

    /// Record names referenced anywhere in this type.
    pub fn record_names(&self, out: &mut Vec<String>) {
        match self {
            OrdinaryType::Nat | OrdinaryType::Int | OrdinaryType::Bool | OrdinaryType::Text | OrdinaryType::ByteBuf | OrdinaryType::TextBuf | OrdinaryType::BigNat | OrdinaryType::File | OrdinaryType::Float | OrdinaryType::FloatVec | OrdinaryType::NatVec | OrdinaryType::NatMap | OrdinaryType::TextMap => {}
            OrdinaryType::Option(t) | OrdinaryType::List(t) | OrdinaryType::Sequence(t) | OrdinaryType::CallArgument(t) | OrdinaryType::CallResult(t)
            | OrdinaryType::CollectionItem(t) | OrdinaryType::ResultValue(t) | OrdinaryType::ResultError(t) => t.record_names(out),
            OrdinaryType::Result(a, b) | OrdinaryType::Product(a, b) | OrdinaryType::Sum(a, b) => {
                a.record_names(out);
                b.record_names(out);
            }
            OrdinaryType::Record(name) => out.push(name.clone()),
            OrdinaryType::RecordRef(_) => {}
            OrdinaryType::Fn(params, ret) | OrdinaryType::EffectFn(params, ret, _) => {
                params.iter().for_each(|p| p.record_names(out));
                ret.record_names(out);
            }
        }
    }

    /// v0.126: every occurrence of record `name` is under `Option`, `List`,
    /// `Sequence`, `Result` or `Sum`, so finite values exist without it
    /// (well-founded recursive records). `guarded` is the current context.
    pub fn self_reference_guarded(&self, name: &str, guarded: bool) -> bool {
        match self {
            OrdinaryType::Record(n) => guarded || n != name,
            OrdinaryType::Option(t) | OrdinaryType::List(t) | OrdinaryType::Sequence(t) => t.self_reference_guarded(name, true),
            OrdinaryType::Result(a, b) | OrdinaryType::Sum(a, b) => a.self_reference_guarded(name, true) && b.self_reference_guarded(name, true),
            OrdinaryType::Product(a, b) => a.self_reference_guarded(name, guarded) && b.self_reference_guarded(name, guarded),
            OrdinaryType::Fn(..) | OrdinaryType::EffectFn(..) => {
                let mut names = Vec::new();
                self.record_names(&mut names);
                !names.iter().any(|n| n == name)
            },
            OrdinaryType::CallArgument(t) | OrdinaryType::CallResult(t) | OrdinaryType::CollectionItem(t)
            | OrdinaryType::ResultValue(t) | OrdinaryType::ResultError(t) => t.self_reference_guarded(name, guarded),
            _ => true,
        }
    }

    /// v0.102: `ByteBuf` occurs strictly inside this type (an element, a
    /// component or a part of a function type).
    pub fn nests_byte_buf(&self) -> bool {
        fn any(t: &OrdinaryType) -> bool {
            t.is_resource() || t.nests_byte_buf()
        }
        match self {
            OrdinaryType::Option(t) | OrdinaryType::List(t) | OrdinaryType::Sequence(t) | OrdinaryType::CallArgument(t) | OrdinaryType::CallResult(t)
            | OrdinaryType::CollectionItem(t) | OrdinaryType::ResultValue(t) | OrdinaryType::ResultError(t) => any(t),
            OrdinaryType::Result(a, b) | OrdinaryType::Product(a, b) | OrdinaryType::Sum(a, b) => any(a) || any(b),
            OrdinaryType::Fn(params, ret) | OrdinaryType::EffectFn(params, ret, _) => params.iter().any(any) || any(ret),
            _ => false,
        }
    }

    /// v0.104: an owned resource type (`ByteBuf`, `TextBuf`).
    pub fn is_resource(&self) -> bool {
        matches!(self, OrdinaryType::ByteBuf | OrdinaryType::TextBuf | OrdinaryType::File)
    }

    /// v0.109: a linear resource (`File`): it may not be released implicitly.
    pub fn is_linear(&self) -> bool {
        matches!(self, OrdinaryType::File)
    }

    pub fn has_projection(&self) -> bool {
        let mut pending = vec![self];
        while let Some(t) = pending.pop() {
            match t {
                Self::CallArgument(_) | Self::CallResult(_) | Self::CollectionItem(_)
                | Self::ResultValue(_) | Self::ResultError(_) => return true,
                Self::List(t) | Self::Option(t) | Self::Sequence(t) => pending.push(t),
                Self::Result(a,b) | Self::Product(a,b) | Self::Sum(a,b) => { pending.push(a); pending.push(b); }
                Self::Fn(p,r) | Self::EffectFn(p,r,_) => { pending.extend(p); pending.push(r); }
                _ => {}
            }
        }
        false
    }

    pub(crate) fn map_records(&self, f: &mut impl FnMut(&Self) -> Option<Self>) -> Option<Self> {
        let mut layers = Vec::new();
        let mut current = self;
        loop {
            let (tag, t) = match current {
                Self::List(t) => (0,t), Self::Option(t) => (1,t), Self::Sequence(t) => (2,t), _ => break,
            };
            layers.push(tag); current = t;
        }
        let mut value = current.map_records_inner(f)?;
        for tag in layers.into_iter().rev() {
            value = match tag { 0 => Self::List(Box::new(value)), 1 => Self::Option(Box::new(value)), _ => Self::Sequence(Box::new(value)) };
        }
        Some(value)
    }

    fn map_records_inner(&self, f: &mut impl FnMut(&Self) -> Option<Self>) -> Option<Self> {
        use OrdinaryType::*;
        Some(match self {
            Record(_) | RecordRef(_) => return f(self),
            Option(t) => Option(Box::new(t.map_records(f)?)),
            List(t) => List(Box::new(t.map_records(f)?)),
            Sequence(t) => Sequence(Box::new(t.map_records(f)?)),
            Result(a, b) => Result(Box::new(a.map_records(f)?), Box::new(b.map_records(f)?)),
            Product(a, b) => Product(Box::new(a.map_records(f)?), Box::new(b.map_records(f)?)),
            Sum(a, b) => Sum(Box::new(a.map_records(f)?), Box::new(b.map_records(f)?)),
            Fn(params, ret) => Fn(params.iter().map(|p| p.map_records(f)).collect::<std::option::Option<Vec<_>>>()?, Box::new(ret.map_records(f)?)),
            EffectFn(params, ret, bounds) => EffectFn(params.iter().map(|p| p.map_records(f)).collect::<std::option::Option<Vec<_>>>()?, Box::new(ret.map_records(f)?), bounds.clone()),
            CallArgument(t) => match t.map_records(f)? {
                Fn(p, _) | EffectFn(p, _, _) if p.len() == 1 => p.into_iter().next()?,
                t => CallArgument(Box::new(t)),
            },
            CallResult(t) => match t.map_records(f)? {
                Fn(_, r) | EffectFn(_, r, _) => *r, t => CallResult(Box::new(t)),
            },
            CollectionItem(t) => match t.map_records(f)? {
                List(t) | Option(t) | Sequence(t) => *t, NatVec => Nat, FloatVec => Float,
                t => CollectionItem(Box::new(t)),
            },
            ResultValue(t) => match t.map_records(f)? {
                Result(t, _) => *t, t => ResultValue(Box::new(t)),
            },
            ResultError(t) => match t.map_records(f)? {
                Result(_, t) => *t, t => ResultError(Box::new(t)),
            },
            _ => self.clone(),
        })
    }
}

/// Parse surface type text such as `List<Option<Nat>>`, `Result(Nat, Text)` or
/// `ProductType<Nat*Bool>`.
pub fn parse_ordinary_type(text: &str) -> Result<OrdinaryType, String> {
    let mut current = text.trim();
    let mut layers = Vec::new();
    while let Some((head, inner)) = split_type_application(current)? {
        let tag = match head { "List" => 0, "Option" => 1, "Sequence" => 2, _ => break };
        if inner.trim().is_empty() || split_top_level(inner, ',').len() != 1 { break; }
        layers.push(tag); current = inner.trim();
    }
    let mut value = parse_ordinary_type_inner(current)?;
    for tag in layers.into_iter().rev() {
        value = match tag { 0 => OrdinaryType::List(Box::new(value)), 1 => OrdinaryType::Option(Box::new(value)), _ => OrdinaryType::Sequence(Box::new(value)) };
    }
    Ok(value)
}

fn parse_ordinary_type_inner(text: &str) -> Result<OrdinaryType, String> {
    let text = text.trim();
    if text.is_empty() {
        return Err("empty type".to_string());
    }
    let (head, args) = match split_type_application(text)? {
        Some((head, inner)) => (head, Some(inner)),
        None => (text, None),
    };
    let args_list = |inner: &str, sep: char| -> Result<Vec<OrdinaryType>, String> {
        split_top_level(inner, sep)
            .into_iter()
            .map(parse_ordinary_type)
            .collect()
    };
    match (head, args) {
        ("Nat", None) => Ok(OrdinaryType::Nat),
        ("Int", None) => Ok(OrdinaryType::Int),
        ("Bool", None) => Ok(OrdinaryType::Bool),
        ("Text", None) => Ok(OrdinaryType::Text),
        ("ByteBuf", None) => Ok(OrdinaryType::ByteBuf),
        ("TextBuf", None) => Ok(OrdinaryType::TextBuf),
        ("BigNat", None) => Ok(OrdinaryType::BigNat),
        ("File", None) => Ok(OrdinaryType::File),
        ("Float", None) => Ok(OrdinaryType::Float),
        ("FloatVec", None) => Ok(OrdinaryType::FloatVec),
        ("NatVec", None) => Ok(OrdinaryType::NatVec),
        ("NatMap", None) => Ok(OrdinaryType::NatMap),
        ("TextMap", None) => Ok(OrdinaryType::TextMap),
        ("Option", Some(inner)) => one_arg(head, args_list(inner, ',')?).map(|t| OrdinaryType::Option(Box::new(t))),
        ("List", Some(inner)) => one_arg(head, args_list(inner, ',')?).map(|t| OrdinaryType::List(Box::new(t))),
        ("Sequence", Some(inner)) => one_arg(head, args_list(inner, ',')?).map(|t| OrdinaryType::Sequence(Box::new(t))),
        ("Result", Some(inner)) => {
            two_args(head, args_list(inner, ',')?).map(|(a, b)| OrdinaryType::Result(Box::new(a), Box::new(b)))
        }
        ("Product" | "Pair", Some(inner)) => {
            let sep = if split_top_level(inner, ',').len() > 1 { ',' } else { '*' };
            two_args(head, args_list(inner, sep)?).map(|(a, b)| OrdinaryType::Product(Box::new(a), Box::new(b)))
        }
        ("ProductType", Some(inner)) => {
            two_args(head, args_list(inner, '*')?).map(|(a, b)| OrdinaryType::Product(Box::new(a), Box::new(b)))
        }
        ("Sum", Some(inner)) => {
            let sep = if split_top_level(inner, ',').len() > 1 { ',' } else { '+' };
            two_args(head, args_list(inner, sep)?).map(|(a, b)| OrdinaryType::Sum(Box::new(a), Box::new(b)))
        }
        ("SumType", Some(inner)) => {
            two_args(head, args_list(inner, '+')?).map(|(a, b)| OrdinaryType::Sum(Box::new(a), Box::new(b)))
        }
        ("FnEffect", Some(inner)) => {
            let parts = split_top_level(inner, ',');
            if parts.len() < 2 { return Err("FnEffect expects bounds, optional parameters, and a result type".to_string()); }
            let bounds = crate::callback::EffectBounds::parse(parts[0])?;
            let mut types = parts[1..].iter().map(|t| parse_ordinary_type(t)).collect::<Result<Vec<_>,_>>()?;
            let ret = types.pop().expect("a result type");
            Ok(OrdinaryType::EffectFn(types, Box::new(ret), bounds))
        }
        ("ArgOf" | "ResultOf" | "ItemOf" | "OkOf" | "ErrorOf", Some(inner)) => {
            let t = one_arg(head, args_list(inner, ',')?)?;
            let valid_domain = match (head, &t) {
                ("ArgOf", OrdinaryType::Fn(p, _) | OrdinaryType::EffectFn(p, _, _)) => p.len() == 1,
                ("ResultOf", OrdinaryType::Fn(..) | OrdinaryType::EffectFn(..)) => true,
                ("ItemOf", OrdinaryType::List(_) | OrdinaryType::Sequence(_) | OrdinaryType::Option(_)
                    | OrdinaryType::NatVec | OrdinaryType::FloatVec) => true,
                ("OkOf" | "ErrorOf", OrdinaryType::Result(..)) => true,
                (_, OrdinaryType::Record(_) | OrdinaryType::CallArgument(_) | OrdinaryType::CallResult(_)
                    | OrdinaryType::CollectionItem(_) | OrdinaryType::ResultValue(_) | OrdinaryType::ResultError(_)) => true,
                _ => false,
            };
            if !valid_domain { return Err(format!("{head} is not defined for {}", t.canonical())); }
            let projected = match head {
                "ArgOf" => OrdinaryType::CallArgument(Box::new(t)),
                "ResultOf" => OrdinaryType::CallResult(Box::new(t)),
                "ItemOf" => OrdinaryType::CollectionItem(Box::new(t)),
                "OkOf" => OrdinaryType::ResultValue(Box::new(t)),
                _ => OrdinaryType::ResultError(Box::new(t)),
            };
            let reduced = projected.map_records(&mut |t| Some(t.clone())).expect("identity substitution");
            Ok(reduced)
        }
        ("Fn", Some(inner)) => {
            let mut args = args_list(inner, ',')?;
            if args.len() < 2 {
                return Err("`Fn` expects at least one parameter type and a result type".to_string());
            }
            let ret = args.pop().expect("at least two");
            Ok(OrdinaryType::Fn(args, Box::new(ret)))
        }
        ("Record", Some(inner)) => {
            let name = inner.trim();
            if is_record_name(name) {
                Ok(OrdinaryType::Record(name.to_string()))
            } else {
                Err(format!("invalid record name `{name}`"))
            }
        }
        (name, None) if is_record_name(name) => Ok(OrdinaryType::Record(name.to_string())),
        // v0.110: `Box<Nat>` — an instance of a generic record (`crate::generics`).
        (name, Some(inner)) if is_record_name(name) && text.contains('<') => {
            let args = args_list(inner, ',')?.iter().map(OrdinaryType::canonical).collect::<Vec<_>>();
            Ok(OrdinaryType::Record(crate::generics::mangle(name, &args)))
        }
        (name, None) => Err(format!("unknown ordinary type `{name}`")),
        (name, Some(_)) => Err(format!("unknown ordinary type constructor `{name}`")),
    }
}

/// Canonical string for surface type text.
pub fn canonical_type_text(text: &str) -> Result<String, String> {
    parse_ordinary_type(text).map(|t| t.canonical())
}

/// Interpret an expression in type position: `Nat`, `List(Nat)`,
/// `Result(Nat, Text)` and so on.
pub fn type_from_expr(expr: &Expr) -> Result<String, String> {
    fn render(expr: &Expr) -> Result<String, String> {
        match &expr.kind {
            ExprKind::Ident(name) => Ok(name.clone()),
            ExprKind::Add { lhs, rhs } => Ok(format!("{}+{}", render(lhs)?, render(rhs)?)),
            ExprKind::Call { name, args } if !args.is_empty() => {
                let parts = args.iter().map(render).collect::<Result<Vec<_>, _>>()?;
                Ok(format!("{name}<{}>", parts.join(",")))
            }
            _ => Err("expected a type such as Nat, Bool, Text, Option(Nat) or List(Nat)".to_string()),
        }
    }
    canonical_type_text(&render(expr)?)
}

/// Canonical ordinary type of a value passport, or `None` when the value is
/// not an ordinary surface value (proofs, theorems, truth claims, runtime
/// witnesses, resources, infinities, reports, ...).
pub fn surface_type_of(passport: &Passport) -> Option<String> {
    let name = ordinary_type_name(&passport.ty);
    canonical_type_text(&name).ok()
}

/// Compact source rendering of an expression, used as audited lambda body text and as
/// statement proposition text (v0.75/v0.76).
pub fn render_expr(expr: &Expr) -> String {
    match &expr.kind {
        ExprKind::IntLiteral(value) => value.clone(),
        ExprKind::TextLiteral(text) => format!("{text:?}"),
        ExprKind::Ident(name) => name.clone(),
        ExprKind::QualifiedIdent { theory, name } => format!("{theory}.{name}"),
        ExprKind::Power { base, exp } => format!("({} ^ {})", render_expr(base), render_expr(exp)),
        ExprKind::Add { lhs, rhs } => format!("({} + {})", render_expr(lhs), render_expr(rhs)),
        ExprKind::CompareGt { lhs, rhs } => format!("({} > {})", render_expr(lhs), render_expr(rhs)),
        ExprKind::Call { name, args } if args.is_empty() && (name == "true" || name == "false") => name.clone(),
        // v0.85: Int subtraction and negation are written as operators.
        ExprKind::Call { name, args } if name == "__sub" && args.len() == 2 => {
            format!("({} - {})", render_expr(&args[0]), render_expr(&args[1]))
        }
        ExprKind::Call { name, args } if name == "__neg" && args.len() == 1 => format!("-{}", render_expr(&args[0])),
        ExprKind::Call { name, args } => {
            format!("{name}({})", args.iter().map(render_expr).collect::<Vec<_>>().join(", "))
        }
        ExprKind::Lambda { params, body } => format!(
            "fun({}, {})",
            params.iter().map(|p| format!("{}: {}", p.name, p.ty)).collect::<Vec<_>>().join(", "),
            render_expr(body)
        ),
        ExprKind::Let { name, value, body } => format!("(let {name} = {} in {})", render_expr(value), render_expr(body)),
    }
}

/// v0.76: canonical proposition text of a statement item. Bounded theorems
/// state the bound; axioms and conjectures state the unbounded claim.
pub fn statement_proposition(item: &StatementItem) -> String {
    let body = render_expr(&item.body);
    if item.params.is_empty() {
        return body;
    }
    let vars: Vec<String> = item
        .params
        .iter()
        .map(|p| match (item.kind, item.bound) {
            (StatementItemKind::Theorem, Some(bound)) if p.ty == "Nat" => format!("{}: Nat < {bound}", p.name),
            _ => format!("{}: {}", p.name, p.ty),
        })
        .collect();
    format!("forall {}. {body}", vars.join(", "))
}

/// `true` for an identifier that may name a record type: ASCII, starts with an
/// uppercase letter, and is not a reserved type constructor.
pub fn is_record_name(name: &str) -> bool {
    let mut chars = name.chars();
    matches!(chars.next(), Some(c) if c.is_ascii_uppercase())
        && chars.all(|c| c.is_ascii_alphanumeric() || c == '_')
        && !RESERVED_TYPE_NAMES.contains(&name)
}

/// `(params, result)` of a canonical `Fn<P..., R>`.
pub fn fn_signature(canonical: &str) -> Option<(Vec<String>, String)> {
    match parse_ordinary_type(canonical).ok()? {
        OrdinaryType::Fn(params, ret) | OrdinaryType::EffectFn(params, ret, _) => Some((params.iter().map(OrdinaryType::canonical).collect(), ret.canonical())),
        _ => None,
    }
}

/// Canonical `Fn<P..., R>` for a parameter list and result type.
pub fn fn_type_text(params: &[String], ret: &str) -> String {
    let mut parts = params.to_vec();
    parts.push(ret.to_string());
    format!("Fn<{}>", parts.join(","))
}

/// `(left, right)` of a canonical `SumType<A+B>`.
pub fn sum_components(canonical: &str) -> Option<(String, String)> {
    match parse_ordinary_type(canonical).ok()? {
        OrdinaryType::Sum(lhs, rhs) => Some((lhs.canonical(), rhs.canonical())),
        _ => None,
    }
}

/// Record name of a canonical `Record<Name>`.
pub fn record_type_name(canonical: &str) -> Option<String> {
    match parse_ordinary_type(canonical).ok()? {
        OrdinaryType::Record(name) => Some(name),
        _ => None,
    }
}

/// `(lhs, rhs)` components of a canonical `ProductType<A*B>`.
pub fn product_components(canonical: &str) -> Option<(String, String)> {
    match parse_ordinary_type(canonical).ok()? {
        OrdinaryType::Product(lhs, rhs) => Some((lhs.canonical(), rhs.canonical())),
        _ => None,
    }
}

/// Item type of a canonical `Option<T>`.
pub fn option_item(canonical: &str) -> Option<String> {
    match parse_ordinary_type(canonical).ok()? {
        OrdinaryType::Option(item) => Some(item.canonical()),
        _ => None,
    }
}

/// `(ok, err)` of a canonical `Result<T,E>`.
pub fn result_components(canonical: &str) -> Option<(String, String)> {
    match parse_ordinary_type(canonical).ok()? {
        OrdinaryType::Result(ok, err) => Some((ok.canonical(), err.canonical())),
        _ => None,
    }
}

/// `("List" | "Sequence", item)` of a canonical collection type.
pub fn collection_item(canonical: &str) -> Option<(&'static str, String)> {
    match parse_ordinary_type(canonical).ok()? {
        OrdinaryType::List(item) => Some(("List", item.canonical())),
        OrdinaryType::Sequence(item) => Some(("Sequence", item.canonical())),
        _ => None,
    }
}

/// Function domain for a parameter list: `Unit`, the single parameter type, or
/// a left-nested `ProductType<...>` for several parameters. Two parameters
/// produce `ProductType<A*B>`, which is exactly the fold step domain expected by
/// the traversal layer.
pub fn domain_of_params(param_types: &[String]) -> String {
    match param_types {
        [] => "Unit".to_string(),
        [single] => single.clone(),
        [first, rest @ ..] => rest
            .iter()
            .fold(first.clone(), |acc, next| format!("ProductType<{acc}*{next}>")),
    }
}

fn one_arg(head: &str, mut args: Vec<OrdinaryType>) -> Result<OrdinaryType, String> {
    if args.len() != 1 {
        return Err(format!("`{head}` expects exactly one type argument"));
    }
    Ok(args.remove(0))
}

fn two_args(head: &str, mut args: Vec<OrdinaryType>) -> Result<(OrdinaryType, OrdinaryType), String> {
    if args.len() != 2 {
        return Err(format!("`{head}` expects exactly two type arguments"));
    }
    let rhs = args.remove(1);
    let lhs = args.remove(0);
    Ok((lhs, rhs))
}

/// `Head<inner>` or `Head(inner)` -> `Some((head, inner))`; bare name -> `None`.
fn split_type_application(text: &str) -> Result<Option<(&str, &str)>, String> {
    let Some(open) = text.find(['<', '(']) else {
        return Ok(None);
    };
    let close = match text.as_bytes()[open] {
        b'<' => '>',
        _ => ')',
    };
    if !text.ends_with(close) {
        return Err(format!("malformed type `{text}`"));
    }
    let head = text[..open].trim();
    if head.is_empty() || !head.chars().all(|c| c.is_ascii_alphanumeric() || c == '_') {
        return Err(format!("malformed type `{text}`"));
    }
    Ok(Some((head, &text[open + 1..text.len() - 1])))
}

/// Split at `sep` outside of `<>`/`()` nesting.
pub fn split_top_level(text: &str, sep: char) -> Vec<&str> {
    let mut parts = Vec::new();
    let mut depth = 0i32;
    let mut start = 0usize;
    for (idx, ch) in text.char_indices() {
        match ch {
            '<' | '(' => depth += 1,
            '>' | ')' => depth -= 1,
            c if c == sep && depth == 0 => {
                parts.push(text[start..idx].trim());
                start = idx + ch.len_utf8();
            }
            _ => {}
        }
    }
    parts.push(text[start..].trim());
    parts
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn canonicalizes_surface_and_core_spellings() {
        assert_eq!(canonical_type_text("Nat").unwrap(), "Nat");
        assert_eq!(canonical_type_text("Option<List<Nat>>").unwrap(), "Option<List<Nat>>");
        assert_eq!(canonical_type_text("Option(List(Nat))").unwrap(), "Option<List<Nat>>");
        assert_eq!(canonical_type_text("Result<Nat, Text>").unwrap(), "Result<Nat,Text>");
        assert_eq!(canonical_type_text("Product<Nat, Bool>").unwrap(), "ProductType<Nat*Bool>");
        assert_eq!(canonical_type_text("ProductType<Nat*Bool>").unwrap(), "ProductType<Nat*Bool>");
        assert_eq!(
            canonical_type_text("Pair<Pair<Nat,Bool>, Nat>").unwrap(),
            "ProductType<ProductType<Nat*Bool>*Nat>"
        );
    }

    #[test]
    fn rejects_unknown_and_malformed_types() {
        assert!(canonical_type_text("any").is_err());
        assert!(canonical_type_text("Option").is_err());
        assert!(canonical_type_text("Option<Nat, Nat>").is_err());
        assert!(canonical_type_text("List<Nat").is_err());
        assert!(canonical_type_text("static_proof<P>").is_err());
    }

    #[test]
    fn sums_and_records_canonicalize() {
        assert_eq!(canonical_type_text("Sum<Nat, Bool>").unwrap(), "SumType<Nat+Bool>");
        assert_eq!(canonical_type_text("SumType<Nat+Bool>").unwrap(), "SumType<Nat+Bool>");
        assert_eq!(canonical_type_text("Point").unwrap(), "Record<Point>");
        assert_eq!(canonical_type_text("Record<Point>").unwrap(), "Record<Point>");
        assert_eq!(canonical_type_text("Box<Nat>").unwrap(), "Record<Box__of__Nat>");
        assert_eq!(canonical_type_text("List<Sum<Point, Nat>>").unwrap(), "List<SumType<Record<Point>+Nat>>");
        let mut names = Vec::new();
        parse_ordinary_type("Result<Point, Sum<Line, Nat>>").unwrap().record_names(&mut names);
        assert_eq!(names, vec!["Point".to_string(), "Line".to_string()]);
    }

    #[test]
    fn function_types_canonicalize() {
        assert_eq!(canonical_type_text("Fn<Nat, Bool>").unwrap(), "Fn<Nat,Bool>");
        assert_eq!(canonical_type_text("Fn(Nat, Nat, Nat)").unwrap(), "Fn<Nat,Nat,Nat>");
        assert_eq!(canonical_type_text("Fn<Fn<Nat, Nat>, List<Nat>>").unwrap(), "Fn<Fn<Nat,Nat>,List<Nat>>");
        assert_eq!(fn_signature("Fn<Nat,Bool,Text>"), Some((vec!["Nat".into(), "Bool".into()], "Text".into())));
        assert_eq!(fn_type_text(&["Nat".into()], "Bool"), "Fn<Nat,Bool>");
        assert!(canonical_type_text("Fn<Nat>").is_err());
        assert!(!is_record_name("Fn"));
    }

    #[test]
    fn domains_match_traversal_expectations() {
        assert_eq!(domain_of_params(&[]), "Unit");
        assert_eq!(domain_of_params(&["Nat".into()]), "Nat");
        assert_eq!(domain_of_params(&["Nat".into(), "Bool".into()]), "ProductType<Nat*Bool>");
        assert_eq!(
            domain_of_params(&["Nat".into(), "Bool".into(), "Text".into()]),
            "ProductType<ProductType<Nat*Bool>*Text>"
        );
    }
}

/// Expression positions that carry type or field metadata rather than values.
pub(crate) fn metadata_argument(name: &str, index: usize) -> bool {
    matches!((name, index), ("none" | "empty_list" | "empty_sequence" | "effect_fn", 0)
        | ("ok" | "err" | "inl" | "inr" | "field", 1))
}

/// Shared lexical capture analysis for checking, interpretation and lowering.
pub(crate) fn free_value_names(expr: &Expr, bound: &mut Vec<String>, out: &mut Vec<String>) {
    let add = |name: &String, out: &mut Vec<String>| {
        if !bound.contains(name) && !out.contains(name) { out.push(name.clone()); }
    };
    match &expr.kind {
        ExprKind::Ident(name) => add(name, out),
        ExprKind::Call { name, args } => {
            if !crate::checker::ORDINARY_BUILTINS.contains(&name.as_str())
                && !crate::compile::COMPILED_BUILTINS.iter().any(|(n,_)| *n == name) { add(name, out); }
            for (i, arg) in args.iter().enumerate() {
                if !metadata_argument(name, i) { free_value_names(arg, bound, out); }
            }
        }
        ExprKind::Add { lhs, rhs } | ExprKind::CompareGt { lhs, rhs } => {
            free_value_names(lhs,bound,out); free_value_names(rhs,bound,out);
        }
        ExprKind::Power { base, exp } => { free_value_names(base,bound,out); free_value_names(exp,bound,out); }
        ExprKind::Lambda { params, body } => {
            let before=bound.len(); bound.extend(params.iter().map(|p|p.name.clone()));
            free_value_names(body,bound,out); bound.truncate(before);
        }
        ExprKind::Let { name, value, body } => {
            free_value_names(value,bound,out); bound.push(name.clone());
            free_value_names(body,bound,out); bound.pop();
        }
        ExprKind::IntLiteral(_) | ExprKind::TextLiteral(_) | ExprKind::QualifiedIdent { .. } => {}
    }
}
