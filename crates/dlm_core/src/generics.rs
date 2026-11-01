//! v0.110 — stage E-min: generic functions and records with explicit type
//! arguments, expanded by monomorphization before checking.
//!
//! `fn id<T>(x: T) -> T = x` and `record Box<T>(value: T)` are templates. A
//! use `id<Nat>(5)`, a type `Box<List<Nat>>` or a constructor `Box<Nat>(5)` is
//! an instance; the parser writes it as the mangled name `id__of__Nat`
//! ([`mangle`]). [`expand_program`] removes the templates and puts, at each
//! template's position in its own theory, one ordinary declaration per used
//! instance with the type parameters substituted, so the checker, Typed Core,
//! IR, interpreter and native C see only monomorphic code. Instances of
//! instances are generated until none are new (bounded by [`MAX_INSTANCES`]).
use crate::ast::{ClassInfo, Expr, ExprKind, FnDecl, FnParam, ModuleItem, RecordDecl, TheoryItem};
use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::ids::DeclId;
use crate::ordinary::{parse_ordinary_type, split_top_level, OrdinaryType};
use crate::program::Program;
use crate::ExprId;
use std::collections::{BTreeMap, BTreeSet};

pub const INSTANCE_SEPARATOR: &str = "__of__";
pub const MAX_INSTANCES: usize = 1024;
/// Bound individual names too: nested record arguments can grow exponentially.
pub const MAX_INSTANCE_NAME_BYTES: usize = 65536;
/// Builtin type constructors written with `<...>` that are not templates.
pub const BUILTIN_TYPE_HEADS: &[&str] = &[
    "List",
    "Option",
    "Sequence",
    "Result",
    "Product",
    "Pair",
    "ProductType",
    "Sum",
    "SumType",
    "Fn", "FnEffect", "ArgOf", "ResultOf", "ItemOf", "OkOf", "ErrorOf",
    "Record",
];

fn escape(text: &str) -> String {
    let mut out = String::with_capacity(text.len() * 2);
    for c in text.chars() {
        match c {
            '_' => out.push_str("_u"),
            '<' => out.push_str("_l"),
            '>' => out.push_str("_r"),
            ',' => out.push_str("_c"),
            '*' => out.push_str("_p"),
            '+' => out.push_str("_s"),
            c if c.is_ascii_alphanumeric() => out.push(c),
            _ => {}
        }
    }
    out
}

fn unescape(text: &str) -> Option<String> {
    let mut out = String::with_capacity(text.len());
    let mut chars = text.chars();
    while let Some(c) = chars.next() {
        if c != '_' {
            out.push(c);
            continue;
        }
        out.push(match chars.next()? {
            'u' => '_',
            'l' => '<',
            'r' => '>',
            'c' => ',',
            'p' => '*',
            's' => '+',
            _ => return None,
        });
    }
    Some(out)
}

/// Instance name of `head<args>` (canonical argument types); injective.
pub fn mangle(head: &str, args: &[String]) -> String {
    format!("{head}{INSTANCE_SEPARATOR}{}", escape(&args.join(",")))
}

/// `(head, canonical argument types)` of an instance name.
pub fn demangle(name: &str) -> Option<(String, Vec<String>)> {
    let at = name.rfind(INSTANCE_SEPARATOR)?;
    let (head, rest) = (&name[..at], &name[at + INSTANCE_SEPARATOR.len()..]);
    if head.is_empty() || rest.is_empty() || rest.contains("__") {
        return None;
    }
    let decoded = unescape(rest)?;
    Some((
        head.to_string(),
        split_top_level(&decoded, ',')
            .into_iter()
            .map(str::to_string)
            .collect(),
    ))
}

/// Rewrites `name<types>` in expression text (outside builtin type heads) into
/// instance names; text literals are already masked by the parser.
pub fn encode_generic_text(text: &str) -> Result<String, String> {
    encode_generic_text_mapped(text).map(|(text, _)| text)
}

/// Encoded and original byte ranges, used to retain source spans after mangling.
pub(crate) fn encode_generic_text_mapped(
    text: &str,
) -> Result<(String, Vec<(usize, usize, usize, usize)>), String> {
    if !text.contains('<') {
        return Ok((text.to_string(), Vec::new()));
    }
    let bytes = text.as_bytes();
    let mut out = String::with_capacity(text.len());
    let mut ranges = Vec::new();
    let mut i = 0;
    while i < bytes.len() {
        let c = bytes[i] as char;
        if c.is_ascii_alphabetic() || c == '_' {
            let start = i;
            while i < bytes.len()
                && ((bytes[i] as char).is_ascii_alphanumeric() || bytes[i] == b'_')
            {
                i += 1;
            }
            let ident = &text[start..i];
            if i < bytes.len() && bytes[i] == b'<' && !BUILTIN_TYPE_HEADS.contains(&ident) {
                let (mut angle, mut paren) = (0i32, 0i32);
                let mut close = None;
                for (k, ch) in text[i..].char_indices() {
                    match ch {
                        '<' => angle += 1,
                        '(' => paren += 1,
                        ')' if paren == 0 => break,
                        ')' => paren -= 1,
                        '>' => {
                            angle -= 1;
                            if angle == 0 {
                                close = Some(i + k);
                                break;
                            }
                        }
                        _ => {}
                    }
                }
                let close =
                    close.ok_or_else(|| format!("unclosed type argument list of `{ident}`"))?;
                let inner = encode_generic_text(&text[i + 1..close])?;
                let mut args = Vec::new();
                for part in split_top_level(&inner, ',') {
                    args.push(
                        crate::ordinary::canonical_type_text(part)
                            .map_err(|e| format!("type argument of `{ident}`: {e}"))?,
                    );
                }
                let instance = mangle(ident, &args);
                if instance.len() > MAX_INSTANCE_NAME_BYTES {
                    return Err(format!("generic type expansion exceeds {MAX_INSTANCE_NAME_BYTES} bytes (polymorphic recursion?)"));
                }
                ranges.push((out.len(), instance.len(), start, close + 1 - start));
                out.push_str(&instance);
                i = close + 1;
            } else {
                out.push_str(ident);
            }
        } else {
            let c = text[i..].chars().next().unwrap();
            out.push(c);
            i += c.len_utf8();
        }
    }
    Ok((out, ranges))
}

/// v0.111: bounds a type parameter may carry: `Eq` (structural `equal`), `Ord` (`less`: Nat, Int, Text).
pub const BOUNDS: &[&str] = &["Eq", "Ord", "Callable", "UnaryCallable", "NullaryCallable", "PureCallable"];

/// `Name<T: Eq, U>` declaration header -> (`Name`, [`T`, `U`], [[`Eq`], []]).
pub fn split_type_params(header: &str) -> Result<(String, Vec<String>, Vec<Vec<String>>), String> {
    let header = header.trim();
    let Some(open) = header.find('<') else {
        return Ok((header.to_string(), Vec::new(), Vec::new()));
    };
    let Some(inner) = header[open + 1..].strip_suffix('>') else {
        return Err(format!("malformed type parameter list `{header}`"));
    };
    let mut params: Vec<String> = Vec::new();
    let mut bounds: Vec<Vec<String>> = Vec::new();
    for raw in inner.split(',').map(str::trim) {
        let (p, b) = match raw.split_once(':') {
            Some((p, b)) => (
                p.trim(),
                b.split('+')
                    .map(|x| x.trim().to_string())
                    .collect::<Vec<_>>(),
            ),
            None => (raw, Vec::new()),
        };
        if let Some(bad) = b
            .iter()
            .find(|x| !crate::ordinary::is_record_name(x) || x.contains('<'))
        {
            return Err(format!(
                "invalid bound `{bad}` on type parameter `{p}` (Eq, Ord or a class name)"
            ));
        }
        bounds.push(b);
        if !crate::ordinary::is_record_name(p)
            || BUILTIN_TYPE_HEADS.contains(&p)
            || p.contains("__")
        {
            return Err(format!("type parameter `{p}` must be a capitalized name"));
        }
        if params.iter().any(|q| q == p) {
            return Err(format!("type parameter `{p}` is declared twice"));
        }
        params.push(p.to_string());
    }
    let name = header[..open].trim();
    if name.contains("__") {
        return Err(format!("generic name `{name}` may not contain `__`"));
    }
    Ok((name.to_string(), params, bounds))
}

#[derive(Clone)]
enum Template {
    Fn(FnDecl),
    Record(RecordDecl),
}

impl Template {
    fn params(&self) -> &[String] {
        match self {
            Template::Fn(f) => &f.type_params,
            Template::Record(r) => &r.type_params,
        }
    }
    fn bounds(&self) -> &[Vec<String>] {
        match self {
            Template::Fn(f) => &f.type_bounds,
            Template::Record(r) => &r.type_bounds,
        }
    }
}

struct Subst<'a> {
    map: &'a BTreeMap<String, OrdinaryType>,
}

/// v0.111: spans of an instance placed in another module carry that module's file.
fn refile(e: &mut Expr, file: crate::ids::FileId) {
    e.span.file_id = file;
    match &mut e.kind {
        ExprKind::Call { args, .. } => args.iter_mut().for_each(|a| refile(a, file)),
        ExprKind::Lambda { body, .. } => refile(body, file),
        ExprKind::Let { value, body, .. } => {
            refile(value, file);
            refile(body, file);
        }
        ExprKind::Add { lhs, rhs }
        | ExprKind::CompareGt { lhs, rhs }
        | ExprKind::Power {
            base: lhs,
            exp: rhs,
        } => {
            refile(lhs, file);
            refile(rhs, file);
        }
        _ => {}
    }
}

impl Subst<'_> {
    fn ty(&self, t: &OrdinaryType) -> OrdinaryType {
        t.map_records(&mut |r| match r {
            OrdinaryType::Record(name) => Some(match self.map.get(name) {
                Some(arg) => arg.clone(),
                None => OrdinaryType::Record(self.name(name)),
            }),
            other => Some(other.clone()),
        })
        .unwrap_or_else(|| t.clone())
    }
    fn text(&self, s: &str) -> String {
        parse_ordinary_type(s)
            .map(|t| self.ty(&t).canonical())
            .unwrap_or_else(|_| s.to_string())
    }
    /// Instance names inside a template body are re-mangled with substituted arguments.
    fn name(&self, n: &str) -> String {
        match demangle(n) {
            Some((head, args)) => mangle(
                &head,
                &args.iter().map(|a| self.text(a)).collect::<Vec<_>>(),
            ),
            None => n.to_string(),
        }
    }
    fn expr(&self, e: &Expr, bound: &BTreeSet<String>) -> Expr {
        let kind = match &e.kind {
            ExprKind::Ident(n) => ExprKind::Ident(if bound.contains(n) {
                n.clone()
            } else {
                self.name(n)
            }),
            ExprKind::Call { name, args } => ExprKind::Call {
                name: if bound.contains(name) {
                    name.clone()
                } else {
                    self.name(name)
                },
                args: args
                    .iter()
                    .enumerate()
                    .map(|(i, a)| {
                        if matches!(
                            (name.as_str(), i),
                            ("none" | "empty_list" | "empty_sequence" | "effect_fn", 0)
                                | ("ok" | "err" | "inl" | "inr", 1)
                        ) {
                            self.type_expr(a)
                        } else {
                            self.expr(a, bound)
                        }
                    })
                    .collect(),
            },
            ExprKind::Lambda { params, body } => ExprKind::Lambda {
                params: params
                    .iter()
                    .map(|p| FnParam {
                        name: p.name.clone(),
                        ty: self.text(&p.ty),
                    })
                    .collect(),
                body: Box::new({
                    let mut inner = bound.clone();
                    inner.extend(params.iter().map(|p| p.name.clone()));
                    self.expr(body, &inner)
                }),
            },
            ExprKind::Let { name, value, body } => ExprKind::Let {
                name: name.clone(),
                value: Box::new(self.expr(value, bound)),
                body: Box::new({
                    let mut inner = bound.clone();
                    inner.insert(name.clone());
                    self.expr(body, &inner)
                }),
            },
            ExprKind::Add { lhs, rhs } => ExprKind::Add {
                lhs: Box::new(self.expr(lhs, bound)),
                rhs: Box::new(self.expr(rhs, bound)),
            },
            ExprKind::CompareGt { lhs, rhs } => ExprKind::CompareGt {
                lhs: Box::new(self.expr(lhs, bound)),
                rhs: Box::new(self.expr(rhs, bound)),
            },
            ExprKind::Power { base, exp } => ExprKind::Power {
                base: Box::new(self.expr(base, bound)),
                exp: Box::new(self.expr(exp, bound)),
            },
            other => other.clone(),
        };
        Expr {
            kind,
            line: e.line,
            id: ExprId::fresh(),
            span: e.span,
        }
    }

    fn type_expr(&self, e: &Expr) -> Expr {
        let kind = match &e.kind {
            ExprKind::Ident(n) => ExprKind::Ident(match self.map.get(n) {
                Some(arg) => arg.canonical(),
                None => self.name(n),
            }),
            ExprKind::Call { name, args } => ExprKind::Call {
                name: self.name(name),
                args: args.iter().enumerate().map(|(i, a)| {
                    if name == "FnEffect" && i == 0 {
                        // Bounds are literal metadata, even if a type parameter has the same name.
                        self.expr(a, &BTreeSet::new())
                    } else { self.type_expr(a) }
                }).collect(),
            },
            _ => return self.expr(e, &BTreeSet::new()),
        };
        Expr {
            kind,
            line: e.line,
            id: ExprId::fresh(),
            span: e.span,
        }
    }
}

/// Instance names used by an expression or a type string.
fn names_in_type(s: &str, out: &mut Vec<String>) {
    if let Ok(t) = parse_ordinary_type(s) {
        t.map_records(&mut |r| {
            if let OrdinaryType::Record(n) = r {
                if demangle(n).is_some() {
                    out.push(n.clone());
                }
                if let Some((_, args)) = demangle(n) {
                    for a in args {
                        names_in_type(&a, out);
                    }
                }
            }
            Some(r.clone())
        });
    }
}

fn names_in_expr(e: &Expr, out: &mut Vec<String>) {
    match &e.kind {
        ExprKind::Ident(n) => {
            if demangle(n).is_some() {
                out.push(n.clone());
            }
            if n.contains('<') || demangle(n).is_some() {
                names_in_type(n, out);
            }
        }
        ExprKind::Call { name, args } => {
            if demangle(name).is_some() {
                out.push(name.clone());
                names_in_type(name, out);
            }
            args.iter().for_each(|a| names_in_expr(a, out));
        }
        ExprKind::Lambda { params, body } => {
            params.iter().for_each(|p| names_in_type(&p.ty, out));
            names_in_expr(body, out);
        }
        ExprKind::Let { value, body, .. } => {
            names_in_expr(value, out);
            names_in_expr(body, out);
        }
        ExprKind::Add { lhs, rhs }
        | ExprKind::CompareGt { lhs, rhs }
        | ExprKind::Power {
            base: lhs,
            exp: rhs,
        } => {
            names_in_expr(lhs, out);
            names_in_expr(rhs, out);
        }
        _ => {}
    }
}

fn names_in_item(item: &TheoryItem, out: &mut Vec<String>) {
    match item {
        TheoryItem::Fn(f) => {
            f.params.iter().for_each(|p| names_in_type(&p.ty, out));
            names_in_type(&f.ret, out);
            names_in_expr(&f.body, out);
        }
        TheoryItem::Record(r) => r.fields.iter().for_each(|p| names_in_type(&p.ty, out)),
        TheoryItem::Let(l) => names_in_expr(&l.expr, out),
        TheoryItem::Expr(e) => names_in_expr(e, out),
        TheoryItem::Statement(s) => {
            s.params.iter().for_each(|p| names_in_type(&p.ty, out));
            names_in_expr(&s.body, out);
        }
    }
}

fn error(line: usize, message: String) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::FunctionTermError, Some(line), message)
        .with_help("generic declarations take explicit type arguments: id<Nat>(x), Box<Text>(t), fn f(b: Box<Nat>) (docs/GENERICS.md)")
}

/// Monomorphizes every generic template of the program (see the module docs).
pub fn expand_program(program: &mut Program) -> Vec<Diagnostic> {
    let mut diagnostics = infer_type_arguments(program);
    apply_defaults(program);
    let sigs = collect_sigs(program);
    // theory name -> (module index, item index), its base, its templates
    let mut theories: BTreeMap<String, (usize, usize)> = BTreeMap::new();
    let mut extends: BTreeMap<String, String> = BTreeMap::new();
    let mut templates: BTreeMap<(String, String), (Template, usize)> = BTreeMap::new();
    for (mi, module) in program.modules.iter().enumerate() {
        for (ii, item) in module.items.iter().enumerate() {
            let ModuleItem::Theory(t) = item else {
                continue;
            };
            theories.insert(t.name.clone(), (mi, ii));
            if let Some(base) = &t.extends {
                extends.insert(t.name.clone(), base.clone());
            }
            let mut declared = BTreeMap::new();
            for (pos, it) in t.items.iter().enumerate() {
                let (name, line, generic) = match it {
                    TheoryItem::Fn(f) => (&f.name, f.line, !f.type_params.is_empty()),
                    TheoryItem::Record(r) => (&r.name, r.line, !r.type_params.is_empty()),
                    TheoryItem::Let(l) => (&l.name, l.line, false),
                    TheoryItem::Statement(s) => (&s.name, s.line, false),
                    TheoryItem::Expr(_) => continue,
                };
                if let Some(was_generic) = declared.insert(name.clone(), generic) {
                    if generic || was_generic {
                        diagnostics.push(Diagnostic::error(
                            DiagnosticKind::NameError,
                            Some(line),
                            format!("duplicate value '{name}' in theory '{}'", t.name),
                        ));
                    }
                }
                let template = match it {
                    TheoryItem::Fn(f) if !f.type_params.is_empty() => Template::Fn(f.clone()),
                    TheoryItem::Record(r) if !r.type_params.is_empty() => {
                        Template::Record(r.clone())
                    }
                    _ => continue,
                };
                if let Template::Fn(f) = &template {
                    if !f.mutual.is_empty() {
                        diagnostics.push(error(
                            f.line,
                            format!("generic fn '{}' cannot be part of a mutual block", f.name),
                        ));
                    }
                }
                let name = match &template {
                    Template::Fn(f) => f.name.clone(),
                    Template::Record(r) => r.name.clone(),
                };
                templates.insert((t.name.clone(), name), (template, pos));
            }
        }
    }
    let classes = class_tables(program, &mut diagnostics);
    if templates.is_empty() {
        return diagnostics;
    }
    let visible = |theory: &str, head: &str| -> Option<String> {
        let mut current = Some(theory.to_string());
        let mut seen = BTreeSet::new();
        while let Some(t) = current {
            if !seen.insert(t.clone()) {
                return None;
            }
            if templates.contains_key(&(t.clone(), head.to_string())) {
                return Some(t);
            }
            current = extends.get(&t).cloned();
        }
        None
    };
    // instances per template, in creation order
    let mut instances: BTreeMap<(String, String), Vec<TheoryItem>> = BTreeMap::new();
    let mut local: BTreeMap<String, Vec<TheoryItem>> = BTreeMap::new();
    let mut made: BTreeSet<String> = BTreeSet::new();
    let mut work: Vec<(String, TheoryItem)> = Vec::new();
    for (name, &(mi, ii)) in &theories {
        let ModuleItem::Theory(t) = &program.modules[mi].items[ii] else {
            continue;
        };
        for it in &t.items {
            let is_template = matches!(it, TheoryItem::Fn(f) if !f.type_params.is_empty())
                || matches!(it, TheoryItem::Record(r) if !r.type_params.is_empty());
            if !is_template {
                work.push((name.clone(), it.clone()));
            }
        }
    }
    let file_of: BTreeMap<String, crate::ids::FileId> = theories
        .iter()
        .map(|(t, &(mi, _))| (t.clone(), program.modules[mi].source.id))
        .collect();
    let mut count = 0usize;
    'expand: while let Some((theory, item)) = work.pop() {
        let mut names = Vec::new();
        names_in_item(&item, &mut names);
        for n in names {
            if n.len() > MAX_INSTANCE_NAME_BYTES {
                let line = match &item {
                    TheoryItem::Fn(f) => f.line,
                    TheoryItem::Record(r) => r.line,
                    TheoryItem::Let(l) => l.line,
                    TheoryItem::Statement(s) => s.line,
                    TheoryItem::Expr(e) => e.line,
                };
                diagnostics.push(error(line, format!("generic type expansion exceeds {MAX_INSTANCE_NAME_BYTES} bytes (polymorphic recursion?)")));
                break 'expand;
            }
            let Some((head, args)) = demangle(&n) else {
                continue;
            };
            let Some(mut template_theory) = visible(&theory, &head) else {
                continue;
            };
            let mut key = (template_theory.clone(), head.clone());
            // v0.112: a class method resolves to a concrete instance method or to an instance template.
            let mut impl_map: Option<BTreeMap<String, OrdinaryType>> = None;
            if let (Template::Fn(f), _) = &templates[&key] {
                if let Some(ClassInfo::Method { class, .. }) = &f.class_info {
                    if made.contains(&format!("{theory}.{n}"))
                        || sigs
                            .func(&theory, &n)
                            .is_some_and(|(_, _, tps)| tps.is_empty())
                    {
                        continue;
                    }
                    made.insert(format!("{theory}.{n}"));
                    let Some(x) = args.first().filter(|_| args.len() == 1).map(|a| ty(a)) else {
                        diagnostics.push(error(
                            f.line,
                            format!("class method '{head}' takes exactly one type argument"),
                        ));
                        continue;
                    };
                    let chain = sigs.chain(&theory);
                    let found: Vec<_> = classes
                        .generic
                        .iter()
                        .filter(|g| {
                            g.class == *class && g.method == head && chain.contains(&g.theory)
                        })
                        .filter_map(|g| {
                            let mut m = BTreeMap::new();
                            unify(&g.ty, &x, &g.params, &mut m).then_some((g, m))
                        })
                        .collect();
                    match found.as_slice() {
                        [(g, m)] => {
                            template_theory = g.theory.clone();
                            key = (g.theory.clone(), g.template.clone());
                            impl_map = Some(m.clone());
                        }
                        [] => {
                            diagnostics.push(error(
                                f.line,
                                format!(
                                    "no instance `{class}<{}>` provides method '{head}'",
                                    x.canonical()
                                ),
                            ));
                            continue;
                        }
                        _ => {
                            diagnostics.push(error(
                                f.line,
                                format!(
                                    "overlapping instances of `{class}` for `{}`",
                                    x.canonical()
                                ),
                            ));
                            continue;
                        }
                    }
                }
            }
            // v0.111: an instance whose arguments name records the template's theory cannot see
            // lives in the requesting theory (after its records).
            // v0.112: instances that need class instances live with the requester, who sees them.
            let class_bound = templates[&key]
                .0
                .bounds()
                .iter()
                .flatten()
                .any(|b| !BOUNDS.contains(&b.as_str()));
            let shared = !class_bound
                && args
                    .iter()
                    .all(|a| type_visible(&sigs, &template_theory, &ty(a)));
            let owner = if shared {
                template_theory.clone()
            } else {
                theory.clone()
            };
            let instance_name = mangle(&head, &args);
            if impl_map.is_none() && made.contains(&format!("{owner}.{instance_name}")) {
                continue;
            }
            let (template, _) = &templates[&key];
            let args: Vec<String> = match &impl_map {
                Some(m) => template
                    .params()
                    .iter()
                    .map(|p| m.get(p).map_or_else(|| p.clone(), OrdinaryType::canonical))
                    .collect(),
                None => args,
            };
            let line = match template {
                Template::Fn(f) => f.line,
                Template::Record(r) => r.line,
            };
            if args.len() != template.params().len() {
                diagnostics.push(error(
                    line,
                    format!(
                        "generic '{head}' expects {} type argument(s), got {}",
                        template.params().len(),
                        args.len()
                    ),
                ));
                made.insert(format!("{owner}.{instance_name}"));
                continue;
            }
            let mut map = BTreeMap::new();
            let mut bad = false;
            for (p, a) in template.params().iter().zip(&args) {
                match parse_ordinary_type(a) {
                    Ok(t) => {
                        map.insert(p.clone(), t);
                    }
                    Err(e) => {
                        diagnostics.push(error(
                            line,
                            format!("generic '{head}': type argument `{a}`: {e}"),
                        ));
                        bad = true;
                    }
                }
            }
            made.insert(format!("{owner}.{instance_name}"));
            for ((p, bounds), a) in template.params().iter().zip(template.bounds()).zip(&args) {
                for bound in bounds {
                    let arg = parse_ordinary_type(a).unwrap_or(OrdinaryType::Bool);
                    let fields = |n: &str| {
                        sigs.fields(&owner, n)
                            .map(|fs| fs.into_iter().map(|(_, t)| t).collect::<Vec<_>>())
                    };
                    let ok = if BOUNDS.contains(&bound.as_str()) {
                        satisfies(&arg, bound, &fields, 0)
                    } else {
                        classes.has_instance(
                            bound,
                            &arg,
                            &sigs.chain(&theory),
                            &sigs,
                            &BTreeMap::new(),
                        )
                    };
                    if !ok {
                        diagnostics.push(error(line, format!("type `{a}` does not satisfy `{bound}` required by `{p}` of '{head}'")));
                        bad = true;
                    }
                }
            }
            if bad {
                continue;
            }
            count += 1;
            if count > MAX_INSTANCES {
                diagnostics.push(error(line, format!("more than {MAX_INSTANCES} generic instances (polymorphic recursion?) at '{head}'")));
                break 'expand;
            }
            let s = Subst { map: &map };
            let generated = match template {
                Template::Fn(f) => TheoryItem::Fn(FnDecl {
                    id: DeclId::fresh(),
                    name: instance_name.clone(),
                    params: f
                        .params
                        .iter()
                        .map(|p| FnParam {
                            name: p.name.clone(),
                            ty: s.text(&p.ty),
                        })
                        .collect(),
                    ret: s.text(&f.ret),
                    measure: f.measure.clone(),
                    body: s.expr(&f.body, &f.params.iter().map(|p| p.name.clone()).collect()),
                    line: f.line,
                    mutual: Vec::new(),
                    borrowed: f.borrowed.clone(),
                    type_params: Vec::new(),
                    type_bounds: Vec::new(),
                    class_info: None,
                }),
                Template::Record(r) => TheoryItem::Record(RecordDecl {
                    id: DeclId::fresh(),
                    name: instance_name.clone(),
                    fields: r
                        .fields
                        .iter()
                        .map(|p| FnParam {
                            name: p.name.clone(),
                            ty: s.text(&p.ty),
                        })
                        .collect(),
                    enum_variants: r.enum_variants.clone(),
                    line: r.line,
                    type_params: Vec::new(),
                    type_bounds: Vec::new(),
                }),
            };
            work.push((owner.clone(), generated.clone()));
            let mut generated = generated;
            if let (TheoryItem::Fn(f), Some(&file)) = (&mut generated, file_of.get(&owner)) {
                if file_of.get(&template_theory) != Some(&file) {
                    refile(&mut f.body, file);
                }
            }
            if shared {
                instances.entry(key).or_default().push(generated);
            } else {
                local.entry(owner.clone()).or_default().push(generated);
            }
        }
    }
    // replace each template by its instances (records first, then functions in creation order)
    for (name, &(mi, ii)) in &theories {
        let ModuleItem::Theory(t) = &mut program.modules[mi].items[ii] else {
            continue;
        };
        let private: Vec<String> = templates
            .keys()
            .filter(|(owner, tn)| owner == name && t.private.contains(tn))
            .flat_map(|key| instances.get(key).into_iter().flatten())
            .map(|it| match it {
                TheoryItem::Fn(f) => f.name.clone(),
                TheoryItem::Record(r) => r.name.clone(),
                _ => String::new(),
            })
            .collect();
        t.private.extend(private);
        let old = std::mem::take(&mut t.items);
        for it in old {
            let template = match &it {
                TheoryItem::Fn(f) if !f.type_params.is_empty() => Some(f.name.clone()),
                TheoryItem::Record(r) if !r.type_params.is_empty() => Some(r.name.clone()),
                _ => None,
            };
            match template {
                Some(tn) => {
                    // instances created later depend on earlier ones: emit in reverse creation order of dependencies
                    // an instance found while generating another one is its dependency: reverse creation order
                    let mut list = instances.remove(&(name.clone(), tn)).unwrap_or_default();
                    list.reverse();
                    list.sort_by_key(|it| matches!(it, TheoryItem::Fn(_)));
                    t.items.extend(list);
                }
                None => t.items.push(it),
            }
        }
        if let Some(mut list) = local.remove(name) {
            // dependencies first; each goes right after the last item it refers to
            list.reverse();
            list.sort_by_key(|it| matches!(it, TheoryItem::Fn(_)));
            let after_records = t
                .items
                .iter()
                .rposition(|it| matches!(it, TheoryItem::Record(_)))
                .map_or(0, |i| i + 1);
            for g in list {
                let refs = refs_in_item(&g);
                let at = t
                    .items
                    .iter()
                    .rposition(|it| item_name(it).is_some_and(|n| refs.contains(n)))
                    .map_or(after_records, |i| (i + 1).max(after_records));
                t.items.insert(at, g);
            }
        }
    }
    diagnostics
}

// ---------------------------------------------------------------------------
// v0.111: inference of omitted type arguments and bound checks.

/// Signatures visible to the local inference: per theory, functions
/// `(param types, result, type params)` and records (field name, type).
#[derive(Default)]
struct Sigs {
    fns: BTreeMap<String, BTreeMap<String, (Vec<OrdinaryType>, OrdinaryType, Vec<String>)>>,
    records: BTreeMap<String, BTreeMap<String, (Vec<(String, OrdinaryType)>, Vec<String>)>>,
    extends: BTreeMap<String, String>,
}

fn ty(s: &str) -> OrdinaryType {
    parse_ordinary_type(s).unwrap_or(OrdinaryType::Record("__unknown".into()))
}

impl Sigs {
    fn chain(&self, theory: &str) -> Vec<String> {
        let mut out = vec![theory.to_string()];
        while let Some(b) = self.extends.get(out.last().unwrap()) {
            if out.contains(b) || out.len() > 64 {
                break;
            }
            out.push(b.clone());
        }
        out
    }
    fn func(
        &self,
        theory: &str,
        name: &str,
    ) -> Option<&(Vec<OrdinaryType>, OrdinaryType, Vec<String>)> {
        self.chain(theory)
            .iter()
            .find_map(|t| self.fns.get(t).and_then(|m| m.get(name)))
    }
    fn record(
        &self,
        theory: &str,
        name: &str,
    ) -> Option<&(Vec<(String, OrdinaryType)>, Vec<String>)> {
        self.chain(theory)
            .iter()
            .find_map(|t| self.records.get(t).and_then(|m| m.get(name)))
    }
    /// Fields of a (possibly instance) record type.
    fn fields(&self, theory: &str, name: &str) -> Option<Vec<(String, OrdinaryType)>> {
        if let Some((fields, params)) = self.record(theory, name) {
            if params.is_empty() {
                return Some(fields.clone());
            }
        }
        let (head, args) = demangle(name)?;
        let (fields, params) = self.record(theory, &head)?;
        let map: BTreeMap<String, OrdinaryType> = params
            .iter()
            .cloned()
            .zip(args.iter().map(|a| ty(a)))
            .collect();
        let s = Subst { map: &map };
        Some(fields.iter().map(|(n, t)| (n.clone(), s.ty(t))).collect())
    }
}

fn unify(
    pattern: &OrdinaryType,
    actual: &OrdinaryType,
    params: &[String],
    map: &mut BTreeMap<String, OrdinaryType>,
) -> bool {
    use OrdinaryType as T;
    let resolved = pattern.has_projection().then(|| Subst { map }.ty(pattern));
    let pattern = resolved.as_ref().unwrap_or(pattern);
    match (pattern, actual) {
        (T::Record(p), _) if params.contains(p) => match map.get(p) {
            Some(bound) => bound == actual,
            None => {
                map.insert(p.clone(), actual.clone());
                true
            }
        },
        (T::Record(p), T::Record(a)) => match (demangle(p), demangle(a)) {
            (Some((hp, ap)), Some((ha, aa))) if hp == ha && ap.len() == aa.len() => ap
                .iter()
                .zip(&aa)
                .all(|(x, y)| unify(&ty(x), &ty(y), params, map)),
            _ => p == a,
        },
        (T::Option(a), T::Option(b))
        | (T::List(a), T::List(b))
        | (T::Sequence(a), T::Sequence(b)) => unify(a, b, params, map),
        (T::Result(a, b), T::Result(c, d))
        | (T::Product(a, b), T::Product(c, d))
        | (T::Sum(a, b), T::Sum(c, d)) => unify(a, c, params, map) && unify(b, d, params, map),
        (T::EffectFn(pa,ra,ba), T::EffectFn(pb,rb,bb)) if ba == bb && pa.len() == pb.len() => {
            pa.iter().zip(pb).all(|(x,y)|unify(x,y,params,map)) && unify(ra,rb,params,map)
        }
        (T::Fn(pa, ra), T::Fn(pb, rb)) if pa.len() == pb.len() => {
            pa.iter().zip(pb).all(|(x, y)| unify(x, y, params, map)) && unify(ra, rb, params, map)
        }
        _ => pattern == actual,
    }
}

fn elem(t: Option<OrdinaryType>) -> Option<OrdinaryType> {
    match t? {
        OrdinaryType::List(x) | OrdinaryType::Sequence(x) | OrdinaryType::Option(x) => Some(*x),
        _ => None,
    }
}

struct Infer<'a> {
    sigs: &'a Sigs,
    theory: String,
    diagnostics: Vec<Diagnostic>,
}

type Env = Vec<(String, OrdinaryType)>;

impl Infer<'_> {
    fn type_arg(e: &Expr) -> Option<OrdinaryType> {
        crate::ordinary::type_from_expr(e).ok().map(|s| ty(&s))
    }

    fn infer(&self, e: &Expr, env: &Env) -> Option<OrdinaryType> {
        use OrdinaryType as T;
        match &e.kind {
            ExprKind::IntLiteral(_) => Some(T::Nat),
            ExprKind::TextLiteral(_) => Some(T::Text),
            ExprKind::Ident(n) => env
                .iter()
                .rev()
                .find(|(m, _)| m == n)
                .map(|(_, t)| t.clone())
                .or_else(|| {
                    self.sigs
                        .func(&self.theory, n)
                        .filter(|(_, _, tps)| tps.is_empty())
                        .map(|(params, ret, _)| T::Fn(params.clone(), Box::new(ret.clone())))
                }),
            ExprKind::Add { lhs, rhs } => self.infer(lhs, env).or_else(|| self.infer(rhs, env)),
            ExprKind::CompareGt { .. } => Some(T::Bool),
            ExprKind::Power { .. } => Some(T::Nat),
            ExprKind::Lambda { params, body } => {
                let mut inner = env.clone();
                inner.extend(params.iter().map(|p| (p.name.clone(), ty(&p.ty))));
                Some(T::Fn(
                    params.iter().map(|p| ty(&p.ty)).collect(),
                    Box::new(self.infer(body, &inner)?),
                ))
            }
            ExprKind::Let { name, value, body } => {
                let mut inner = env.clone();
                if let Some(t) = self.infer(value, env) {
                    inner.push((name.clone(), t));
                }
                self.infer(body, &inner)
            }
            ExprKind::Call { name, args } => self.call(name, args, env),
            _ => None,
        }
    }

    fn call(&self, name: &str, args: &[Expr], env: &Env) -> Option<OrdinaryType> {
        use OrdinaryType as T;
        let a = |i: usize| args.get(i).and_then(|x| self.infer(x, env));
        let b = |t: OrdinaryType| Some(Box::new(t));
        if let Some((_, T::Fn(_, ret) | T::EffectFn(_, ret, _))) = env.iter().rev().find(|(m, _)| m == name) {
            return Some((**ret).clone());
        }
        Some(match name {
            "true" | "false" | "not" | "and" | "or" | "nat_eq" | "int_eq" | "text_eq"
            | "is_some" | "is_none" | "is_ok" | "is_err" | "is_empty" | "is_left" | "equal"
            | "less" | "buf_eq" => T::Bool,
            "mul" | "monus" | "pred" | "length" | "text_length" | "abs" | "buf_len"
            | "buf_free" | "tbuf_len" => T::Nat,
            "int" | "__sub" | "__neg" => T::Int,
            "div" | "mod" => a(0)?,
            "concat" | "nat_to_text" | "int_to_text" | "substring" | "join" | "print_line"
            | "print_text" | "read_all" | "tbuf_finish" => T::Text,
            "text_to_nat" => T::Option(b(T::Nat)?),
            "text_to_int" => T::Option(b(T::Int)?),
            "lines" | "words" | "args" => T::List(b(T::Text)?),
            "read_line" => T::Option(b(T::Text)?),
            "if_then_else" => a(1).or_else(|| a(2))?,
            "effect_fn" => Self::type_arg(args.first()?)?,
            "some" => T::Option(b(a(0)?)?),
            "none" => T::Option(b(Self::type_arg(args.first()?)?)?),
            "ok" => T::Result(b(a(0)?)?, b(Self::type_arg(args.get(1)?)?)?),
            "err" => T::Result(b(Self::type_arg(args.get(1)?)?)?, b(a(0)?)?),
            "list" => T::List(b(a(0)?)?),
            "sequence" => T::Sequence(b(a(0)?)?),
            "empty_list" => T::List(b(Self::type_arg(args.first()?)?)?),
            "empty_sequence" => T::Sequence(b(Self::type_arg(args.first()?)?)?),
            "head" | "index" => T::Option(b(elem(a(0))?)?),
            "tail" | "cons" => a(if name == "cons" { 1 } else { 0 })?,
            "unwrap_or" => a(1).or_else(|| elem(a(0)))?,
            "__match_head" | "__match_some" | "__match_variant" => elem(a(0))?,
            "__match_ok" | "__match_err" => match a(0)? {
                T::Result(x, y) => *if name == "__match_ok" { x } else { y },
                _ => return None,
            },
            "pair" => T::Product(b(a(0)?)?, b(a(1)?)?),
            "inl" => T::Sum(b(a(0)?)?, b(Self::type_arg(args.get(1)?)?)?),
            "inr" => T::Sum(b(Self::type_arg(args.get(1)?)?)?, b(a(0)?)?),
            "fst" | "snd" => match a(0)? {
                T::Product(x, y) => *if name == "fst" { x } else { y },
                _ => return None,
            },
            "map" | "map_effect" => {
                let ret = match a(1)? {
                    T::Fn(_, r) | T::EffectFn(_, r, _) => r,
                    _ => return None,
                };
                match a(0)? {
                    T::Sequence(_) => T::Sequence(ret),
                    _ => T::List(ret),
                }
            }
            "fold" | "fold_effect" => a(1)?,
            "each_effect" => T::Nat,
            "filter_effect" => a(0)?,
            "find_effect" => T::Option(b(elem(a(0))?)?),
            "any_effect" | "all_effect" => T::Bool,
            "try_map_effect" | "try_each_effect" => {
                let r = match a(1)? { T::Fn(_,r) | T::EffectFn(_,r,_) => r, _ => return None };
                let T::Result(ok,err) = *r else { return None };
                let success = if name == "try_each_effect" { T::Nat }
                    else if matches!(a(0)?, T::Sequence(_)) { T::Sequence(ok) } else { T::List(ok) };
                T::Result(Box::new(success),err)
            }
            "field" => {
                let T::Record(r) = a(0)? else { return None };
                let ExprKind::Ident(label) = &args.get(1)?.kind else {
                    return None;
                };
                self.sigs
                    .fields(&self.theory, &r)?
                    .into_iter()
                    .find(|(n, _)| n == label)?
                    .1
            }
            _ => {
                if let Some((_, params, ret)) = crate::checker::DENSE_NAT_BUILTINS
                    .iter()
                    .chain(crate::checker::FILE_BUILTINS)
                    .chain(crate::checker::FLOAT_BUILTINS)
                    .chain(crate::checker::GRAPH_BUILTINS)
                    .chain(crate::checker::LINALG_BUILTINS)
                    .find(|(n, _, _)| *n == name)
                {
                    let _ = params;
                    return Some(ty(ret));
                }
                if let Some((params, ret, tps)) = self.sigs.func(&self.theory, name) {
                    if tps.is_empty() {
                        return Some(ret.clone());
                    }
                    let map = self.bind(params, tps, args, env)?;
                    return Some(Subst { map: &map }.ty(ret));
                }
                if let Some((head, targs)) = demangle(name) {
                    if let Some((_, ret, tps)) = self.sigs.func(&self.theory, &head) {
                        let map: BTreeMap<String, OrdinaryType> = tps
                            .iter()
                            .cloned()
                            .zip(targs.iter().map(|t| ty(t)))
                            .collect();
                        return Some(Subst { map: &map }.ty(ret));
                    }
                    if self.sigs.record(&self.theory, &head).is_some() {
                        return Some(T::Record(name.to_string()));
                    }
                }
                if self
                    .sigs
                    .record(&self.theory, name)
                    .is_some_and(|(_, p)| p.is_empty())
                {
                    return Some(T::Record(name.to_string()));
                }
                return None;
            }
        })
    }

    /// Type arguments of a template call inferred from its arguments.
    fn bind(
        &self,
        params: &[OrdinaryType],
        tps: &[String],
        args: &[Expr],
        env: &Env,
    ) -> Option<BTreeMap<String, OrdinaryType>> {
        if params.len() != args.len() {
            return None;
        }
        let mut map = BTreeMap::new();
        let mut deferred = Vec::new();
        for (p, arg) in params.iter().zip(args) {
            if let Some(t) = self.infer(arg, env) {
                if p.has_projection() { deferred.push((p,t)); continue; }
                if !unify(p, &t, tps, &mut map) {
                    return None;
                }
            }
        }
        for (p,t) in deferred { if !unify(p,&t,tps,&mut map) { return None; } }
        tps.iter().all(|t| map.contains_key(t)).then_some(map)
    }

    /// Rewrites calls of templates without type arguments into instance names.
    fn rewrite(&mut self, e: &Expr, env: &Env) -> Expr {
        let kind = match &e.kind {
            ExprKind::Lambda { params, body } => {
                let mut inner = env.clone();
                inner.extend(params.iter().map(|p| (p.name.clone(), ty(&p.ty))));
                ExprKind::Lambda {
                    params: params.clone(),
                    body: Box::new(self.rewrite(body, &inner)),
                }
            }
            ExprKind::Let { name, value, body } => {
                let value = self.rewrite(value, env);
                let mut inner = env.clone();
                if let Some(t) = self.infer(&value, env) {
                    inner.push((name.clone(), t));
                }
                ExprKind::Let {
                    name: name.clone(),
                    value: Box::new(value),
                    body: Box::new(self.rewrite(body, &inner)),
                }
            }
            ExprKind::Add { lhs, rhs } => ExprKind::Add {
                lhs: Box::new(self.rewrite(lhs, env)),
                rhs: Box::new(self.rewrite(rhs, env)),
            },
            ExprKind::CompareGt { lhs, rhs } => ExprKind::CompareGt {
                lhs: Box::new(self.rewrite(lhs, env)),
                rhs: Box::new(self.rewrite(rhs, env)),
            },
            ExprKind::Power { base, exp } => ExprKind::Power {
                base: Box::new(self.rewrite(base, env)),
                exp: Box::new(self.rewrite(exp, env)),
            },
            ExprKind::Call { name, args } => {
                let args: Vec<Expr> = args.iter().map(|a| self.rewrite(a, env)).collect();
                let mut name = name.clone();
                let shadowed = env.iter().any(|(m, _)| *m == name);
                if !shadowed {
                    if let Some((params, _, tps)) = self.sigs.func(&self.theory, &name).cloned() {
                        if !tps.is_empty() {
                            match self.bind(&params, &tps, &args, env) {
                                Some(map) => name = mangle(&name, &tps.iter().map(|t| map[t].canonical()).collect::<Vec<_>>()),
                                None => self.diagnostics.push(error(
                                    e.line,
                                    format!("cannot infer the type arguments of `{name}`; write {name}<{}>(...)", tps.join(", ")),
                                )),
                            }
                        }
                    } else if let Some((fields, tps)) =
                        self.sigs.record(&self.theory, &name).cloned()
                    {
                        if !tps.is_empty() {
                            let field_types: Vec<OrdinaryType> =
                                fields.iter().map(|(_, t)| t.clone()).collect();
                            match self.bind(&field_types, &tps, &args, env) {
                                Some(map) => name = mangle(&name, &tps.iter().map(|t| map[t].canonical()).collect::<Vec<_>>()),
                                None => self.diagnostics.push(error(
                                    e.line,
                                    format!("cannot infer the type arguments of `{name}`; write {name}<{}>(...)", tps.join(", ")),
                                )),
                            }
                        }
                    }
                }
                ExprKind::Call { name, args }
            }
            other => other.clone(),
        };
        Expr {
            kind,
            line: e.line,
            id: e.id,
            span: e.span,
        }
    }
}

fn collect_sigs(program: &Program) -> Sigs {
    let mut sigs = Sigs::default();
    for module in &program.modules {
        for item in &module.items {
            let ModuleItem::Theory(t) = item else {
                continue;
            };
            if let Some(b) = &t.extends {
                sigs.extends.insert(t.name.clone(), b.clone());
            }
            for it in &t.items {
                match it {
                    TheoryItem::Fn(f) => {
                        sigs.fns.entry(t.name.clone()).or_default().insert(
                            f.name.clone(),
                            (
                                f.params.iter().map(|p| ty(&p.ty)).collect(),
                                ty(&f.ret),
                                f.type_params.clone(),
                            ),
                        );
                    }
                    TheoryItem::Record(r) => {
                        sigs.records.entry(t.name.clone()).or_default().insert(
                            r.name.clone(),
                            (
                                r.fields
                                    .iter()
                                    .map(|p| (p.name.clone(), ty(&p.ty)))
                                    .collect(),
                                r.type_params.clone(),
                            ),
                        );
                    }
                    _ => {}
                }
            }
        }
    }
    sigs
}

/// v0.111: rewrites template calls whose type arguments can be inferred
/// locally from argument types; reports those that cannot.
pub fn infer_type_arguments(program: &mut Program) -> Vec<Diagnostic> {
    let sigs = collect_sigs(program);
    if !sigs
        .fns
        .values()
        .flat_map(|m| m.values())
        .any(|(_, _, tps)| !tps.is_empty())
        && !sigs
            .records
            .values()
            .flat_map(|m| m.values())
            .any(|(_, tps)| !tps.is_empty())
    {
        return Vec::new();
    }
    let mut diagnostics = Vec::new();
    for module in &mut program.modules {
        for item in &mut module.items {
            let ModuleItem::Theory(t) = item else {
                continue;
            };
            let mut inf = Infer {
                sigs: &sigs,
                theory: t.name.clone(),
                diagnostics: Vec::new(),
            };
            let mut globals: Env = Vec::new();
            for it in &mut t.items {
                match it {
                    TheoryItem::Fn(f) => {
                        let mut env = globals.clone();
                        env.extend(f.params.iter().map(|p| (p.name.clone(), ty(&p.ty))));
                        f.body = inf.rewrite(&f.body, &env);
                    }
                    TheoryItem::Let(l) => {
                        l.expr = inf.rewrite(&l.expr, &globals);
                        if let Some(t) = inf.infer(&l.expr, &globals) {
                            globals.push((l.name.clone(), t));
                        }
                    }
                    TheoryItem::Expr(e) => *e = inf.rewrite(e, &globals),
                    TheoryItem::Statement(s) => {
                        let mut env = globals.clone();
                        env.extend(s.params.iter().map(|p| (p.name.clone(), ty(&p.ty))));
                        s.body = inf.rewrite(&s.body, &env);
                    }
                    TheoryItem::Record(_) => {}
                }
            }
            diagnostics.append(&mut inf.diagnostics);
        }
    }
    diagnostics
}

/// v0.111: every record named by `t` is visible from `theory` (directly or as an instance of a visible template).
fn type_visible(sigs: &Sigs, theory: &str, t: &OrdinaryType) -> bool {
    let mut ok = true;
    t.map_records(&mut |r| {
        if let OrdinaryType::Record(n) = r {
            ok &= match sigs.record(theory, n) {
                Some((_, params)) => params.is_empty(),
                None => demangle(n).is_some_and(|(h, args)| {
                    sigs.record(theory, &h).is_some()
                        && args.iter().all(|a| type_visible(sigs, theory, &ty(a)))
                }),
            };
        }
        Some(r.clone())
    });
    ok
}

/// v0.111: `t` satisfies `bound` (`Eq`: no functions or resources inside; `Ord`: Nat, Int, Text).
pub fn satisfies(
    t: &OrdinaryType,
    bound: &str,
    fields: &dyn Fn(&str) -> Option<Vec<OrdinaryType>>,
    depth: usize,
) -> bool {
    use OrdinaryType as T;
    if matches!(bound, "Callable" | "UnaryCallable" | "NullaryCallable" | "PureCallable") {
        let (params, pure) = match t {
            T::Fn(p,_) => (p,true), T::EffectFn(p,_,b) => (p,b.effects().is_empty() && !b.partial()),
            _ => return false,
        };
        return match bound { "UnaryCallable" => params.len() == 1, "NullaryCallable" => params.is_empty(), "PureCallable" => pure, _ => true };
    }
    if bound == "Ord" {
        return matches!(t, T::Nat | T::Int | T::Text);
    }
    if bound != "Eq" {
        return false;
    }
    let mut pending = vec![t.clone()];
    let mut checked = BTreeSet::new();
    while let Some(t) = pending.pop() {
        match t {
            T::Nat | T::Int | T::Bool | T::Text | T::BigNat | T::Float | T::FloatVec | T::NatVec | T::NatMap | T::TextMap => {}
            T::Option(x) | T::List(x) | T::Sequence(x) => pending.push(*x),
            T::Result(a, b) | T::Product(a, b) | T::Sum(a, b) => {
                pending.push(*a);
                pending.push(*b);
            }
            // v0.126: recursive records — a record already being checked on this
            // path is assumed to satisfy the bound (coinductive), so the walk is linear.
            T::Record(n) => {
                if !checked.insert(n.clone()) {
                    continue;
                }
                if checked.len().saturating_add(depth) > MAX_INSTANCES
                    || n.len() > MAX_INSTANCE_NAME_BYTES
                {
                    return false;
                }
                let Some(fs) = fields(&n) else {
                    return false;
                };
                pending.extend(fs);
            }
            _ => return false,
        }
    }
    true
}

// ---------------------------------------------------------------------------
// v0.112: type classes.

struct GenericImpl {
    class: String,
    method: String,
    theory: String,
    template: String,
    ty: OrdinaryType,
    params: Vec<String>,
    bounds: Vec<Vec<String>>,
}

#[derive(Default)]
struct Classes {
    /// class -> its methods
    methods: BTreeMap<String, Vec<String>>,
    /// (class, canonical type, theory) of every instance block
    concrete: Vec<(String, String, String)>,
    generic: Vec<GenericImpl>,
    supers: BTreeMap<String, Vec<String>>,
}

impl Classes {
    fn assumes(&self, bounds: &[String], class: &str) -> bool {
        let mut pending = bounds.to_vec();
        let mut seen = BTreeSet::new();
        while let Some(b) = pending.pop() {
            if !seen.insert(b.clone()) {
                continue;
            }
            if b == class || (b == "Ord" && class == "Eq") {
                return true;
            }
            pending.extend(self.supers.get(&b).into_iter().flatten().cloned());
        }
        false
    }

    fn has_instance(
        &self,
        class: &str,
        t: &OrdinaryType,
        chain: &[String],
        sigs: &Sigs,
        assumptions: &BTreeMap<String, Vec<String>>,
    ) -> bool {
        let mut remaining = MAX_INSTANCES;
        self.resolve(
            class,
            t,
            chain,
            sigs,
            assumptions,
            &mut BTreeSet::new(),
            &mut BTreeMap::new(),
            &mut remaining,
        )
    }

    fn resolve(
        &self,
        class: &str,
        t: &OrdinaryType,
        chain: &[String],
        sigs: &Sigs,
        assumptions: &BTreeMap<String, Vec<String>>,
        active: &mut BTreeSet<(String, String)>,
        cache: &mut BTreeMap<(String, String), bool>,
        remaining: &mut usize,
    ) -> bool {
        if let OrdinaryType::Record(n) = t {
            if assumptions.get(n).is_some_and(|bs| self.assumes(bs, class)) {
                return true;
            }
        }
        if BOUNDS.contains(&class) {
            let fields = |n: &str| {
                if assumptions.get(n).is_some_and(|bs| self.assumes(bs, "Eq")) {
                    return Some(Vec::new());
                }
                sigs.fields(&chain[0], n)
                    .map(|fs| fs.into_iter().map(|(_, t)| t).collect())
            };
            return satisfies(t, class, &fields, 0);
        }
        let key = (class.to_string(), t.canonical());
        if let Some(ok) = cache.get(&key) {
            return *ok;
        }
        if *remaining == 0 || active.len() >= 64 || !active.insert(key.clone()) {
            return false;
        }
        *remaining -= 1;
        let concrete = self
            .concrete
            .iter()
            .filter(|(k, ty, th)| k == class && *ty == key.1 && chain.contains(th))
            .count();
        // Each generic block contributes one candidate, regardless of its method count.
        let mut found = BTreeMap::new();
        for g in &self.generic {
            if g.class != class || !chain.contains(&g.theory) {
                continue;
            }
            let mut map = BTreeMap::new();
            if unify(&g.ty, t, &g.params, &mut map) {
                found
                    .entry((g.theory.clone(), g.ty.canonical()))
                    .or_insert((g, map));
            }
        }
        let ok = if concrete + found.len() != 1 {
            false
        } else if concrete == 1 {
            true
        } else {
            let (g, map) = found.into_values().next().unwrap();
            g.params.iter().zip(&g.bounds).all(|(param, bounds)| {
                map.get(param).is_some_and(|arg| {
                    bounds.iter().all(|bound| {
                        self.resolve(
                            bound,
                            arg,
                            chain,
                            sigs,
                            assumptions,
                            active,
                            cache,
                            remaining,
                        )
                    })
                })
            })
        };
        active.remove(&key);
        cache.insert(key, ok);
        ok
    }
}

/// Classes, instances and the checks of instance blocks (complete, matching signatures, not duplicated).
fn class_tables(program: &Program, diagnostics: &mut Vec<Diagnostic>) -> Classes {
    let mut out = Classes::default();
    let ordinary_sigs = collect_sigs(program);
    let mut supers_of: BTreeMap<String, Vec<String>> = BTreeMap::new();
    let mut extends: BTreeMap<String, String> = BTreeMap::new();
    for module in &program.modules {
        for item in &module.items {
            if let ModuleItem::Theory(t) = item {
                if let Some(b) = &t.extends {
                    extends.insert(t.name.clone(), b.clone());
                }
            }
        }
    }
    let chain = |theory: &str| -> Vec<String> {
        let mut out = vec![theory.to_string()];
        while let Some(b) = extends.get(out.last().unwrap()) {
            if out.contains(b) || out.len() > 64 {
                break;
            }
            out.push(b.clone());
        }
        out
    };
    // class -> (param, method -> (params, ret), line)
    let mut sigs: BTreeMap<String, (String, BTreeMap<String, (Vec<String>, String)>)> =
        BTreeMap::new();
    for module in &program.modules {
        for item in &module.items {
            let ModuleItem::Theory(t) = item else {
                continue;
            };
            for it in &t.items {
                if let TheoryItem::Fn(f) = it {
                    if let Some(ClassInfo::Method {
                        class,
                        param,
                        supers,
                        ..
                    }) = &f.class_info
                    {
                        supers_of.insert(class.clone(), supers.clone());
                        let entry = sigs
                            .entry(class.clone())
                            .or_insert_with(|| (param.clone(), BTreeMap::new()));
                        entry.1.insert(
                            f.name.clone(),
                            (
                                f.params.iter().map(|p| p.ty.clone()).collect(),
                                f.ret.clone(),
                            ),
                        );
                        out.methods
                            .entry(class.clone())
                            .or_default()
                            .push(f.name.clone());
                    }
                }
            }
        }
    }
    // instance blocks: (theory, class, type) -> methods with their signatures and line
    let mut blocks: BTreeMap<
        (String, String, String),
        Vec<(
            String,
            Vec<String>,
            String,
            usize,
            Vec<String>,
            Vec<Vec<String>>,
        )>,
    > = BTreeMap::new();
    for module in &program.modules {
        for item in &module.items {
            let ModuleItem::Theory(t) = item else {
                continue;
            };
            for it in &t.items {
                let TheoryItem::Fn(f) = it else { continue };
                let Some(ClassInfo::Impl { class, method, ty }) = &f.class_info else {
                    continue;
                };
                blocks
                    .entry((t.name.clone(), class.clone(), ty.clone()))
                    .or_default()
                    .push((
                        method.clone(),
                        f.params.iter().map(|p| p.ty.clone()).collect(),
                        f.ret.clone(),
                        f.line,
                        f.type_params.clone(),
                        f.type_bounds.clone(),
                    ));
                if !f.type_params.is_empty() {
                    out.generic.push(GenericImpl {
                        class: class.clone(),
                        method: method.clone(),
                        theory: t.name.clone(),
                        template: f.name.clone(),
                        ty: self::ty(ty),
                        params: f.type_params.clone(),
                        bounds: f.type_bounds.clone(),
                    });
                }
            }
        }
    }
    let mut seen: BTreeMap<(String, String), String> = BTreeMap::new();
    for ((theory, class, ty_text), methods) in &blocks {
        let line = methods.first().map_or(0, |m| m.3);
        let Some((param, class_methods)) = sigs.get(class) else {
            diagnostics.push(error(line, format!("instance of unknown class `{class}`")));
            continue;
        };
        let mut names: Vec<&String> = methods.iter().map(|m| &m.0).collect();
        names.sort();
        names.dedup();
        if names.len() != methods.len() {
            diagnostics.push(error(
                line,
                format!("duplicate instance `{class}<{ty_text}>` in theory {theory}"),
            ));
        }
        if let Some(other) = seen.insert((class.clone(), ty_text.clone()), theory.clone()) {
            diagnostics.push(error(
                line,
                format!("duplicate instance `{class}<{ty_text}>` (also in theory {other})"),
            ));
        }
        if methods[0].4.is_empty() {
            out.concrete
                .push((class.clone(), ty_text.clone(), theory.clone()));
        }
        let map: BTreeMap<String, OrdinaryType> =
            [(param.clone(), self::ty(ty_text))].into_iter().collect();
        let s = Subst { map: &map };
        for name in class_methods.keys() {
            if !methods.iter().any(|m| &m.0 == name) {
                diagnostics.push(error(
                    line,
                    format!("instance `{class}<{ty_text}>` does not define method '{name}'"),
                ));
            }
        }
        for (name, params, ret, mline, _, _) in methods {
            let Some((cparams, cret)) = class_methods.get(name) else {
                diagnostics.push(error(
                    *mline,
                    format!("'{name}' is not a method of class `{class}`"),
                ));
                continue;
            };
            let want: Vec<String> = cparams.iter().map(|p| s.text(p)).collect();
            if *params != want || *ret != s.text(cret) {
                diagnostics.push(error(
                    *mline,
                    format!("method '{name}' of instance `{class}<{ty_text}>` must have type ({}) -> {}", want.join(", "), s.text(cret)),
                ));
            }
        }
    }
    // v0.113: superclasses — an instance of a class needs instances of its superclasses for the same type
    out.supers = supers_of.clone();
    for (class, supers) in &supers_of {
        let mut pending = supers.clone();
        let mut visited = BTreeSet::new();
        while let Some(s) = pending.pop() {
            if s == *class {
                diagnostics.push(error(
                    0,
                    format!("cyclic superclass requirements of class `{class}`"),
                ));
                break;
            }
            if visited.insert(s.clone()) {
                pending.extend(supers_of.get(&s).into_iter().flatten().cloned());
            }
        }
        for s in supers {
            if !supers_of.contains_key(s) {
                diagnostics.push(error(
                    0,
                    format!("superclass `{s}` of class `{class}` is not a class"),
                ));
            }
        }
    }
    for ((theory, class, ty_text), methods) in &blocks {
        let line = methods.first().map_or(0, |m| m.3);
        let x = self::ty(ty_text);
        let assumptions = methods[0]
            .4
            .iter()
            .cloned()
            .zip(methods[0].5.iter().cloned())
            .collect();
        let mut pending: Vec<String> = supers_of.get(class).cloned().unwrap_or_default();
        let mut done: BTreeSet<String> = BTreeSet::new();
        while let Some(s) = pending.pop() {
            if !done.insert(s.clone()) {
                continue;
            }
            if supers_of.contains_key(&s)
                && !out.has_instance(&s, &x, &chain(theory), &ordinary_sigs, &assumptions)
            {
                diagnostics.push(error(
                    line,
                    format!("instance `{class}<{ty_text}>` requires an instance `{s}<{ty_text}>` (superclass)"),
                ));
            }
            pending.extend(supers_of.get(&s).cloned().unwrap_or_default());
        }
    }
    for (class, concrete, theory) in &out.concrete {
        for g in &out.generic {
            if g.class == *class
                && chain(theory).contains(&g.theory)
                && unify(&g.ty, &self::ty(concrete), &g.params, &mut BTreeMap::new())
            {
                diagnostics.push(error(
                    0,
                    format!(
                        "overlapping concrete and generic instances of `{class}` for `{concrete}`"
                    ),
                ));
                break;
            }
        }
    }
    out
}

/// v0.113: instances that omit a method with a default body receive it, with the
/// class parameter replaced by the instance type (generic instances get a template).
fn apply_defaults(program: &mut Program) {
    let mut defaults: BTreeMap<String, Vec<FnDecl>> = BTreeMap::new();
    for module in &program.modules {
        for item in &module.items {
            let ModuleItem::Theory(t) = item else {
                continue;
            };
            for it in &t.items {
                if let TheoryItem::Fn(f) = it {
                    if let Some(ClassInfo::Method {
                        class,
                        default: true,
                        ..
                    }) = &f.class_info
                    {
                        defaults.entry(class.clone()).or_default().push(f.clone());
                    }
                }
            }
        }
    }
    if defaults.is_empty() {
        return;
    }
    for module in &mut program.modules {
        let file = module.source.id;
        for item in &mut module.items {
            let ModuleItem::Theory(t) = item else {
                continue;
            };
            // block (class, type) -> (index of its last method, methods present, type params, bounds)
            let mut blocks: BTreeMap<
                (String, String),
                (usize, Vec<String>, Vec<String>, Vec<Vec<String>>),
            > = BTreeMap::new();
            for (i, it) in t.items.iter().enumerate() {
                if let TheoryItem::Fn(f) = it {
                    if let Some(ClassInfo::Impl { class, method, ty }) = &f.class_info {
                        let entry = blocks.entry((class.clone(), ty.clone())).or_insert((
                            i,
                            Vec::new(),
                            f.type_params.clone(),
                            f.type_bounds.clone(),
                        ));
                        entry.0 = i;
                        entry.1.push(method.clone());
                    }
                }
            }
            let mut inserts: Vec<(usize, TheoryItem)> = Vec::new();
            for ((class, ty_text), (at, present, params, bounds)) in blocks {
                for d in defaults.get(&class).into_iter().flatten() {
                    if present.contains(&d.name) {
                        continue;
                    }
                    let Some(ClassInfo::Method { param, .. }) = &d.class_info else {
                        continue;
                    };
                    let map: BTreeMap<String, OrdinaryType> =
                        [(param.clone(), ty(&ty_text))].into_iter().collect();
                    let s = Subst { map: &map };
                    // the method lives in the instance's module: its spans name that file
                    let mut body =
                        s.expr(&d.body, &d.params.iter().map(|p| p.name.clone()).collect());
                    refile(&mut body, file);
                    let generic = !params.is_empty();
                    inserts.push((
                        at + 1,
                        TheoryItem::Fn(FnDecl {
                            id: DeclId::fresh(),
                            name: if generic {
                                format!("{}__impl{}_default_{at}", d.name, d.line)
                            } else {
                                mangle(&d.name, std::slice::from_ref(&ty_text))
                            },
                            params: d
                                .params
                                .iter()
                                .map(|p| FnParam {
                                    name: p.name.clone(),
                                    ty: s.text(&p.ty),
                                })
                                .collect(),
                            ret: s.text(&d.ret),
                            measure: d.measure.clone(),
                            body,
                            line: d.line,
                            mutual: Vec::new(),
                            borrowed: d.borrowed.clone(),
                            type_params: params.clone(),
                            type_bounds: bounds.clone(),
                            class_info: Some(ClassInfo::Impl {
                                class: class.clone(),
                                method: d.name.clone(),
                                ty: ty_text.clone(),
                            }),
                        }),
                    ));
                }
            }
            let mut groups: BTreeMap<usize, Vec<TheoryItem>> = BTreeMap::new();
            for (at, it) in inserts {
                groups.entry(at).or_default().push(it);
            }
            for (at, mut group) in groups.into_iter().rev() {
                let mut ordered = Vec::new();
                while !group.is_empty() {
                    let pending: BTreeSet<String> = group
                        .iter()
                        .filter_map(item_name)
                        .map(str::to_string)
                        .collect();
                    let next = group
                        .iter()
                        .position(|it| refs_in_item(it).is_disjoint(&pending))
                        .unwrap_or(0);
                    ordered.push(group.remove(next));
                }
                for it in ordered.into_iter().rev() {
                    t.items.insert(at, it);
                }
            }
        }
    }
}

fn item_name(it: &TheoryItem) -> Option<&str> {
    match it {
        TheoryItem::Fn(f) => Some(&f.name),
        TheoryItem::Record(r) => Some(&r.name),
        _ => None,
    }
}

/// Every function, value or record name an item refers to.
fn refs_in_item(it: &TheoryItem) -> BTreeSet<String> {
    fn types(s: &str, out: &mut BTreeSet<String>) {
        if let Ok(t) = parse_ordinary_type(s) {
            t.map_records(&mut |r| {
                if let OrdinaryType::Record(n) = r {
                    out.insert(n.clone());
                }
                Some(r.clone())
            });
        }
    }
    fn expr(e: &Expr, out: &mut BTreeSet<String>) {
        match &e.kind {
            ExprKind::Ident(n) => {
                out.insert(n.clone());
                types(n, out);
            }
            ExprKind::Call { name, args } => {
                out.insert(name.clone());
                args.iter().for_each(|a| expr(a, out));
            }
            ExprKind::Lambda { params, body } => {
                params.iter().for_each(|p| types(&p.ty, out));
                expr(body, out);
            }
            ExprKind::Let { value, body, .. } => {
                expr(value, out);
                expr(body, out);
            }
            ExprKind::Add { lhs, rhs }
            | ExprKind::CompareGt { lhs, rhs }
            | ExprKind::Power {
                base: lhs,
                exp: rhs,
            } => {
                expr(lhs, out);
                expr(rhs, out);
            }
            _ => {}
        }
    }
    let mut out = BTreeSet::new();
    match it {
        TheoryItem::Fn(f) => {
            f.params.iter().for_each(|p| types(&p.ty, &mut out));
            types(&f.ret, &mut out);
            expr(&f.body, &mut out);
            out.remove(&f.name);
        }
        TheoryItem::Record(r) => r.fields.iter().for_each(|p| types(&p.ty, &mut out)),
        _ => {}
    }
    out
}
