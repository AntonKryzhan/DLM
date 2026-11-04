use super::*;
#[derive(Clone, Debug)]
struct Loan {
    path: String,
    mutable: bool,
    range: Option<(usize, usize)>,
    holder: String,
    parents: BTreeSet<String>,
}
#[derive(Clone)]
struct Var {
    ty: Ty,
    mutable: bool,
    loans: Vec<Loan>,
}
#[derive(Clone, Default)]
struct Env {
    vars: BTreeMap<String, Var>,
    moved: BTreeSet<String>,
    pending: Vec<Loan>,
    bounds: BTreeMap<String, String>,
    range: Option<(usize, usize)>,
    loops: usize,
    unsafe_: usize,
    returning: bool,
}
fn overlaps(a: &str, b: &str) -> bool {
    let parts = |s: &str| {
        s.split(['.', '[', ']'])
            .filter(|s| !s.is_empty())
            .map(str::to_owned)
            .collect::<Vec<_>>()
    };
    let (a, b) = (parts(a), parts(b));
    a.iter()
        .zip(&b)
        .all(|(a, b)| a == b || a == "*" || b == "*")
}

fn conflict(a: &Loan, b: &Loan) -> bool {
    if !overlaps(&a.path, &b.path) || (!a.mutable && !b.mutable) {
        return false;
    }
    if let (Some((a, b)), Some((c, d))) = (a.range, b.range) {
        return a < d && c < b;
    }
    true
}
fn place(e: &Expr) -> Option<String> {
    match &e.op {
        Op::Name(n) => Some(n.clone()),
        Op::Field(a, n) => place(a).map(|p| format!("{p}.{n}")),
        Op::Index(a, i) | Op::IndexProven(a, i) => place(a).map(|p| {
            format!(
                "{p}[{}]",
                if let Op::Number(n) = &i.op {
                    integer_literal(n)
                        .map(|n| n.to_string())
                        .unwrap_or_else(|| "*".into())
                } else {
                    "*".into()
                }
            )
        }),
        Op::Unary(op, a) if op == "*" => place(a),
        _ => None,
    }
}
fn raw_place(e: &Expr) -> bool {
    match &e.op {
        Op::Unary(op, a) if op == "*" => {
            matches!(a.ty(), Ty::Ptr(_) | Ty::NonNull(_) | Ty::BorrowedPtr(_))
        }
        Op::Field(a, _) | Op::Index(a, _) | Op::IndexProven(a, _) => raw_place(a),
        _ => false,
    }
}
fn loop_break(b: &[Stmt]) -> bool {
    b.iter().any(|s| match s {
        Stmt::Break(_) => true,
        Stmt::If(_, a, b) => loop_break(a) || loop_break(b),
        Stmt::Unsafe(b) | Stmt::Block(b) => loop_break(b),
        _ => false,
    })
}
fn unsafe_block(b: &[Stmt]) -> bool {
    b.iter().any(|s| match s {
        Stmt::Unsafe(_) => true,
        Stmt::If(_, a, b) => unsafe_block(a) || unsafe_block(b),
        Stmt::While(_, b) | Stmt::For(_, _, _, b) | Stmt::Block(b) => unsafe_block(b),
        _ => false,
    })
}
fn subrange(
    parent: Option<(usize, usize)>,
    child: Option<(usize, usize)>,
) -> Option<(usize, usize)> {
    match (parent, child) {
        (Some((p, _)), Some((a, b))) => p.checked_add(a).zip(p.checked_add(b)),
        (_, Some(x)) => Some(x),
        (x, None) => x,
    }
}
fn child_parents(q: &Loan, parent: &str) -> BTreeSet<String> {
    let mut p = q.parents.clone();
    p.insert(parent.to_string());
    p
}
fn root(p: &str) -> &str {
    p.split(['.', '[']).next().unwrap_or(p)
}
fn compatible(got: &Ty, want: &Ty) -> bool {
    got == want || matches!((got,want),(Ty::Ref(a,true),Ty::Ref(b,false)) if a==b)
}
pub fn owned(t: &Ty, records: &[Record]) -> bool {
    fn go(t: &Ty, r: &[Record], seen: &mut BTreeSet<String>) -> bool {
        match t {
            Ty::Window | Ty::ThreadBound(_) | Ty::Resource(..) | Ty::Buffer(_) | Ty::Boxed(_) => {
                true
            }
            Ty::Array(t, _) => go(t, r, seen),
            Ty::Record(n) if seen.insert(n.clone()) => r
                .iter()
                .find(|r| r.name == *n)
                .is_some_and(|d| d.fields.iter().any(|(_, t)| go(t, r, seen))),
            _ => false,
        }
    }
    go(t, records, &mut BTreeSet::new())
}
pub fn affine(t: &Ty, r: &[Record]) -> bool {
    owned(t, r)
        || match t {
            Ty::Atomic(_)
            | Ty::Ref(_, true)
            | Ty::Slice(_, true)
            | Ty::Split(_)
            | Ty::ForeignBuffer(_) => true,
            Ty::Array(t, _) => affine(t, r),
            Ty::Record(n) => r
                .iter()
                .find(|d| d.name == *n)
                .is_some_and(|d| d.fields.iter().any(|(_, t)| affine(t, r))),
            _ => false,
        }
}

pub fn zeroable(t: &Ty, records: &[Record]) -> bool {
    match t {
        Ty::NonNull(_)
        | Ty::FnPtr(..)
        | Ty::Ref(..)
        | Ty::BorrowedPtr(_)
        | Ty::ThreadBound(_)
        | Ty::Resource(..) => false,
        Ty::Array(t, _) => zeroable(t, records),
        Ty::Record(n) => records
            .iter()
            .find(|r| r.name == *n)
            .is_some_and(|r| r.fields.iter().all(|(_, t)| zeroable(t, records))),
        _ => true,
    }
}
pub(super) fn has_affinity(t: &Ty, records: &[Record]) -> bool {
    fn visit(t: &Ty, records: &[Record], seen: &mut BTreeSet<String>) -> bool {
        match t {
            Ty::Window | Ty::ThreadBound(_) => true,
            Ty::Resource(t, _) | Ty::Buffer(t) | Ty::Boxed(t) | Ty::Array(t, _) => {
                visit(t, records, seen)
            }
            Ty::Record(n) if seen.insert(n.clone()) => records
                .iter()
                .find(|r| r.name == *n)
                .is_some_and(|r| r.fields.iter().any(|(_, t)| visit(t, records, seen))),
            _ => false,
        }
    }
    visit(t, records, &mut BTreeSet::new())
}

fn contains_borrow(t: &Ty, records: &[Record]) -> bool {
    fn visit(t: &Ty, records: &[Record], seen: &mut BTreeSet<String>) -> bool {
        if t.borrowed() {
            return true;
        }
        match t {
            Ty::Resource(t, _)
            | Ty::ThreadBound(t)
            | Ty::Buffer(t)
            | Ty::Boxed(t)
            | Ty::Array(t, _) => visit(t, records, seen),
            Ty::Record(n) if seen.insert(n.clone()) => records
                .iter()
                .find(|r| r.name == *n)
                .is_some_and(|r| r.fields.iter().any(|(_, t)| visit(t, records, seen))),
            _ => false,
        }
    }
    visit(t, records, &mut BTreeSet::new())
}

fn drop_calls(t: &Ty, records: &[Record], out: &mut BTreeSet<String>, seen: &mut BTreeSet<String>) {
    match t {
        Ty::Resource(t, name) => {
            out.insert(name.clone());
            drop_calls(t, records, out, seen);
        }
        Ty::ThreadBound(t) | Ty::Buffer(t) | Ty::Boxed(t) | Ty::Array(t, _) => {
            drop_calls(t, records, out, seen)
        }
        Ty::Record(n) if seen.insert(n.clone()) => {
            if let Some(r) = records.iter().find(|r| r.name == *n) {
                for (_, t) in &r.fields {
                    drop_calls(t, records, out, seen);
                }
            }
        }
        _ => {}
    }
}

pub(super) fn layout_ty(
    t: &Ty,
    r: &[Record],
    layouts: &mut BTreeMap<String, Layout>,
    stack: &mut BTreeSet<String>,
) -> Result<(usize, usize), String> {
    Ok(match t {
        Ty::Scalar(s) => {
            let n = s.bits() as usize / 8;
            (n, n)
        }
        Ty::Bool => (1, 1),
        Ty::Unit => (1, 1),
        Ty::Split(_) => (32, 8),
        Ty::Str | Ty::Slice(..) | Ty::ForeignBuffer(_) => (16, 8),
        Ty::Buffer(_) => (24, 8),
        Ty::Window
        | Ty::Resource(..)
        | Ty::Boxed(_)
        | Ty::Ptr(_)
        | Ty::Ref(..)
        | Ty::NonNull(_)
        | Ty::BorrowedPtr(_)
        | Ty::Handle
        | Ty::FnPtr(..) => (8, 8),
        Ty::Atomic(t) => layout_ty(t, r, layouts, stack)?,
        Ty::ThreadBound(t) => {
            let (s, a) = layout_ty(t, r, layouts, stack)?;
            let a = a.max(8);
            (
                ((s + 7) & !7)
                    .checked_add(8)
                    .and_then(|n| n.checked_add(a - 1))
                    .ok_or("thread-bound layout overflow")?
                    & !(a - 1),
                a,
            )
        }
        Ty::Simd(s, n) => {
            let z = s.bits() as usize / 8;
            let size = z.checked_mul(*n).ok_or("SIMD layout overflow")?;
            (size, z)
        }
        Ty::Array(t, n) => {
            let (s, a) = layout_ty(t, r, layouts, stack)?;
            (s.checked_mul(*n).ok_or("array layout overflow")?, a)
        }
        Ty::Record(n) => {
            if let Some(l) = layouts.get(n) {
                return Ok((l.size, l.align));
            }
            if stack.len() >= 128 {
                return Err("native record nesting exceeds 128".into());
            }
            if !stack.insert(n.clone()) {
                return Err(format!("recursive by-value layout {n}"));
            }
            let d = r
                .iter()
                .find(|d| d.name == *n)
                .ok_or_else(|| format!("unknown native record {n}"))?;
            let (mut size, mut align) = (0usize, 1usize);
            let mut offsets = Vec::new();
            for (_, t) in &d.fields {
                let (s, a) = layout_ty(t, r, layouts, stack)?;
                let a = if d.packed { 1 } else { a };
                align = align.max(a);
                let offset = if d.union {
                    0
                } else {
                    size.checked_add(a - 1).ok_or("layout overflow")? & !(a - 1)
                };
                offsets.push(offset);
                size = if d.union {
                    size.max(s)
                } else {
                    offset.checked_add(s).ok_or("layout overflow")?
                };
            }
            align = align.max(d.align.unwrap_or(1));
            size = size.checked_add(align - 1).ok_or("layout overflow")? & !(align - 1);
            if size > isize::MAX as usize {
                return Err("record exceeds address space".into());
            }
            layouts.insert(
                n.clone(),
                Layout {
                    size,
                    align,
                    offsets,
                    version: d.version,
                },
            );
            stack.remove(n);
            (size, align)
        }
    })
}
fn validate_ty(t: &Ty, records: &[Record], line: usize) -> Result<(), Diagnostic> {
    match t {
        Ty::Record(n) => {
            if !records.iter().any(|r| r.name == *n) {
                return Err(error(line, format!("unknown native type {n}")));
            }
        }
        Ty::Array(t, n) => {
            if *n == 0 || *n > 1 << 28 {
                return Err(error(line, "array length must be in 1..2^28"));
            }
            validate_ty(t, records, line)?;
        }
        Ty::Atomic(t) => {
            if !matches!(
                &**t,
                Ty::Bool | Ty::Scalar(Scalar::U32 | Scalar::U64 | Scalar::F32)
            ) {
                return Err(error(line, "atomic type must be Bool, U32, U64 or F32"));
            }
        }
        Ty::Simd(s, n) => {
            if !s.float() || !matches!(*n, 2 | 4 | 8 | 16) {
                return Err(error(
                    line,
                    "SIMD profiles support F32/F64 with 2, 4, 8 or 16 lanes",
                ));
            }
        }
        Ty::FnPtr(p, r) => {
            for t in p.iter().chain(std::iter::once(&**r)) {
                validate_ty(t, records, line)?;
                if !abi(t, records) {
                    return Err(error(line, "function pointers require C ABI types"));
                }
            }
        }
        Ty::Resource(t, _) | Ty::ThreadBound(t) | Ty::Buffer(t) | Ty::Boxed(t) => {
            validate_ty(t, records, line)?;
            if contains_borrow(t, records) {
                return Err(error(line, "owned storage cannot hide borrowed lifetimes"));
            }
        }
        Ty::Split(t)
        | Ty::Slice(t, _)
        | Ty::Ref(t, _)
        | Ty::Ptr(t)
        | Ty::NonNull(t)
        | Ty::BorrowedPtr(t)
        | Ty::ForeignBuffer(t) => validate_ty(t, records, line)?,
        _ => {}
    }
    Ok(())
}
fn abi(t: &Ty, records: &[Record]) -> bool {
    match t {
        Ty::Scalar(_)
        | Ty::Bool
        | Ty::Unit
        | Ty::Handle
        | Ty::Ptr(_)
        | Ty::NonNull(_)
        | Ty::FnPtr(..) => true,
        Ty::Array(t, _) => abi(t, records),
        Ty::Record(n) => records.iter().find(|r| r.name == *n).is_some_and(|r| {
            !r.fields
                .iter()
                .any(|(_, t)| owned(t, records) || t.borrowed())
                && r.fields.iter().all(|(_, t)| abi(t, records))
        }),
        _ => false,
    }
}
fn symbol(n: &str) -> bool {
    !n.is_empty()
        && n.bytes()
            .enumerate()
            .all(|(i, b)| b.is_ascii_alphabetic() || b == b'_' || (i > 0 && b.is_ascii_digit()))
}
pub fn check(mut unit: Unit) -> Result<Mir, Vec<Diagnostic>> {
    let mut errors = Vec::new();
    let mut names = BTreeSet::new();
    let mut exports = BTreeSet::new();
    let mut layouts = BTreeMap::new();
    for r in &unit.records {
        let diagnostic_start = errors.len();
        if !names.insert(r.name.clone())
            || Scalar::parse(&r.name).is_some()
            || [
                "Bool",
                "Unit",
                "Utf8",
                "Window",
                "ThreadBound",
                "Resource",
                "OpaqueHandle",
                "Array",
                "Buffer",
                "Box",
                "Slice",
                "MutSlice",
                "Split",
                "Ref",
                "MutRef",
                "Ptr",
                "NonNull",
                "BorrowedPtr",
                "ForeignBuffer",
                "FnPtr",
                "Simd",
                "Atomic",
                "AtomicBool",
                "AtomicU32",
                "AtomicU64",
                "AtomicF32",
            ]
            .contains(&r.name.as_str())
        {
            errors.push(error(r.line, "duplicate or reserved native type"));
        }
        if r.fields.is_empty() {
            errors.push(error(r.line, "empty C records have no portable ABI"));
        }
        if r.align
            .is_some_and(|a| a == 0 || !a.is_power_of_two() || a > 4096)
        {
            errors.push(error(r.line, "alignment must be a power of two <=4096"));
        }
        let mut fields = BTreeSet::new();
        for (n, t) in &r.fields {
            if !fields.insert(n) {
                errors.push(error(r.line, "duplicate native field"));
            }
            if let Err(d) = validate_ty(t, &unit.records, r.line) {
                errors.push(d);
            }
            if t.borrowed() {
                errors.push(error(
                    r.line,
                    "records cannot erase a borrowed lifetime; pass a borrow as a parameter",
                ));
            }
            if r.union && owned(t, &unit.records) {
                errors.push(error(
                    r.line,
                    "unions cannot own resources without an active variant",
                ));
            }
        }
        if let Err(s) = layout_ty(
            &Ty::Record(r.name.clone()),
            &unit.records,
            &mut layouts,
            &mut BTreeSet::new(),
        ) {
            errors.push(error(r.line, s));
        }
        for d in &mut errors[diagnostic_start..] {
            *d = d.clone().in_source(&r.source);
        }
    }
    if !errors.is_empty() {
        return Err(errors);
    }
    let mut fnames = BTreeSet::new();
    for f in &unit.functions {
        let diagnostic_start = errors.len();
        if f.body.is_none() && f.library.is_none() && f.ordinary.is_none() {
            errors.push(error(
                f.line,
                "native function needs a body or an explicit foreign/ordinary declaration",
            ));
        }

        if !fnames.insert(f.name.clone()) || names.contains(&f.name) || builtin_name(&f.name) {
            errors.push(error(
                f.line,
                format!("duplicate or reserved function {}", f.name),
            ));
        }
        for (_, t) in &f.params {
            if let Err(d) = validate_ty(t, &unit.records, f.line) {
                errors.push(d);
            }
        }
        if let Err(d) = validate_ty(&f.ret, &unit.records, f.line) {
            errors.push(d);
        }
        if let Some(s) = &f.export {
            if !symbol(s)
                || s == "main"
                || s.starts_with("dn_")
                || s.starts_with("dlm_native_")
                || !exports.insert(s.clone())
            {
                errors.push(error(
                    f.line,
                    "invalid, reserved or duplicate exported C symbol",
                ));
            }
        }
        if let Some(n) = &f.borrow_result {
            if !f.ret.borrowed() || !f.params.iter().any(|(p, t)| p == n && t.borrowed()) {
                errors.push(error(
                    f.line,
                    "borrowed result must be tied to a borrowed parameter with borrows(name)",
                ));
            }
        }
        if f.ret.borrowed() && f.borrow_result.is_none() {
            errors.push(error(
                f.line,
                "returning a borrow requires borrows(parameter)",
            ));
        }
        if f.library.is_some() || f.export.is_some() {
            if !f.params.iter().all(|(_, t)| abi(t, &unit.records)) || !abi(&f.ret, &unit.records) {
                errors.push(error(
                    f.line,
                    "external ABI cannot contain owning or borrowed descriptors",
                ));
            }
        }
        if let Some(lib) = &f.library {
            if !unit.trusted.contains(lib) {
                errors.push(error(
                    f.line,
                    format!("external dependency '{lib}' is not in the explicit trust list"),
                ));
            }
            if lib.is_empty() {
                errors.push(error(
                    f.line,
                    "external declaration must name its dependency with from",
                ));
            }
        }
        if f.name == "main" && (!f.params.is_empty() || f.ret != Ty::Unit || f.library.is_some()) {
            errors.push(error(
                f.line,
                "native main must have signature fn main() -> Unit",
            ));
        }
        for g in &f.guarantees {
            if ![
                "NoAlloc",
                "NoBlocking",
                "NoIO",
                "NoUnwind",
                "BoundedWork",
                "ThreadSafe",
                "ThreadAffine",
            ]
            .contains(&g.as_str())
            {
                errors.push(error(f.line, format!("unknown execution guarantee {g}")));
            }
        }
        if f.guarantees.contains("ThreadSafe") && f.guarantees.contains("ThreadAffine") {
            errors.push(error(
                f.line,
                "ThreadSafe and ThreadAffine are incompatible",
            ));
        }
        for d in &mut errors[diagnostic_start..] {
            *d = d.clone().in_source(&f.source);
        }
    }
    if !errors.is_empty() {
        return Err(errors);
    }
    let ordinary_ir = super::bridge::lower(&unit)?;
    let signatures = unit.functions.clone();
    let records = unit.records.clone();
    let mut effects = BTreeMap::new();
    for f in &mut unit.functions {
        let diagnostic_start = errors.len();
        if f.ordinary.is_some() {
            debug_assert!(ordinary_ir.is_some());
            effects.insert(
                f.name.clone(),
                Effects {
                    alloc: true,
                    blocking: true,
                    io: false,
                    fail: true,
                    unbounded: true,
                    unsafe_memory: false,
                    affine: false,
                    calls: BTreeSet::new(),
                },
            );
            continue;
        }
        if f.body.is_none() {
            effects.insert(
                f.name.clone(),
                Effects {
                    alloc: !f.guarantees.contains("NoAlloc"),
                    blocking: !f.guarantees.contains("NoBlocking"),
                    io: !f.guarantees.contains("NoIO"),
                    fail: !f.guarantees.contains("NoUnwind"),
                    unbounded: !f.guarantees.contains("BoundedWork"),
                    unsafe_memory: !f.guarantees.contains("ThreadSafe"),
                    affine: f.guarantees.contains("ThreadAffine"),
                    calls: BTreeSet::new(),
                },
            );
            continue;
        }
        let mut c = Check {
            records: &records,
            functions: &signatures,
            ret: f.ret.clone(),
            borrow_result: f.borrow_result.clone(),
            effect: Effects::default(),
        };
        let mut env = Env::default();
        env.unsafe_ = usize::from(f.unsafe_);
        for (n, t) in &f.params {
            if env
                .vars
                .insert(
                    n.clone(),
                    Var {
                        ty: t.clone(),
                        mutable: false,
                        loans: if t.borrowed() {
                            vec![Loan {
                                path: n.clone(),
                                mutable: matches!(
                                    t,
                                    Ty::Ref(_, true) | Ty::Slice(_, true) | Ty::Split(_)
                                ),
                                range: None,
                                holder: n.clone(),
                                parents: BTreeSet::new(),
                            }]
                        } else {
                            vec![]
                        },
                    },
                )
                .is_some()
            {
                errors.push(error(f.line, "duplicate parameter"));
            }
        }
        match c.block(f.body.as_mut().unwrap(), &mut env, false) {
            Ok(returns) => {
                if f.ret != Ty::Unit && !returns {
                    errors.push(error(f.line, "not all control paths return a value"));
                }
            }
            Err(d) => errors.push(d),
        }
        if f.params
            .iter()
            .any(|(_, t)| owned(t, &records) && has_affinity(t, &records))
        {
            c.effect.affine = true;
            c.effect.fail = true;
        }
        if f.params.iter().any(|(_, t)| owned(t, &records)) {
            c.effect.blocking = true;
            for (_, t) in &f.params {
                drop_calls(t, &records, &mut c.effect.calls, &mut BTreeSet::new());
            }
        }
        effects.insert(f.name.clone(), c.effect);
        for d in &mut errors[diagnostic_start..] {
            *d = d.clone().in_source(&f.source);
        }
    }
    // Calls form a finite monotone dataflow analysis. Recursive SCCs do not
    // establish a finite work bound merely by carrying a user annotation.
    for f in &unit.functions {
        if reaches(&f.name, &f.name, &effects, &mut BTreeSet::new()) {
            effects.get_mut(&f.name).unwrap().unbounded = true;
        }
    }
    loop {
        let old = effects.clone();
        for e in effects.values_mut() {
            for n in e.calls.clone() {
                if let Some(x) = old.get(&n) {
                    e.join(x);
                }
            }
        }
        if effects == old {
            break;
        }
    }
    for f in &unit.functions {
        if f.body.is_none() && f.ordinary.is_none() {
            continue;
        }
        let diagnostic_start = errors.len();
        let e = &effects[&f.name];
        for (g, bad) in [
            ("NoAlloc", e.alloc),
            ("NoBlocking", e.blocking),
            ("NoIO", e.io),
            ("NoUnwind", e.fail),
            ("BoundedWork", e.unbounded),
            ("ThreadSafe", e.unsafe_memory || e.affine),
            ("ThreadAffine", !e.affine),
        ] {
            if bad && f.guarantees.contains(g) {
                errors.push(error(
                    f.line,
                    format!(
                        "{} violates {g}; reachable calls: {}",
                        f.name,
                        e.calls.iter().cloned().collect::<Vec<_>>().join(", ")
                    ),
                ));
            }
        }
        for d in &mut errors[diagnostic_start..] {
            *d = d.clone().in_source(&f.source);
        }
    }
    if !errors.is_empty() {
        return Err(errors);
    }
    let mir = Mir {
        unit,
        layouts,
        effects,
    };
    for t in super::emit::all_types(&mir) {
        if let Ty::Resource(value, finalizer) = t {
            let Some(f) = mir.unit.functions.iter().find(|f| f.name == finalizer) else {
                errors.push(error(1, format!("unknown resource finalizer {finalizer}")));
                continue;
            };
            if f.body.is_none()
                || f.params.len() != 1
                || f.params[0].1 != Ty::Ref(value, true)
                || f.ret != Ty::Unit
            {
                errors.push(
                    error(
                        f.line,
                        "resource finalizer must be a native function (&mut T) -> Unit",
                    )
                    .in_source(&f.source),
                );
            } else if mir.effects[&finalizer].fail {
                errors.push(
                    error(
                        f.line,
                        format!("resource finalizer {finalizer} must establish NoUnwind"),
                    )
                    .in_source(&f.source),
                );
            }
        }
    }
    if errors.is_empty() {
        Ok(mir)
    } else {
        Err(errors)
    }
}

fn reaches(
    start: &str,
    n: &str,
    e: &BTreeMap<String, Effects>,
    seen: &mut BTreeSet<String>,
) -> bool {
    if !seen.insert(n.to_string()) {
        return false;
    }
    e.get(n).is_some_and(|x| {
        x.calls
            .iter()
            .any(|c| c == start || reaches(start, c, e, seen))
    })
}
struct Check<'a> {
    records: &'a [Record],
    functions: &'a [Function],
    ret: Ty,
    borrow_result: Option<String>,
    effect: Effects,
}
impl Check<'_> {
    fn expect(&mut self, e: &mut Expr, t: &Ty, env: &mut Env) -> Result<(), Diagnostic> {
        let got = self.expr(e, Some(t), env)?;
        if !compatible(&got, t) {
            return Err(error(
                e.line,
                format!("expected {}, found {}", t.name(), got.name()),
            ));
        }
        Ok(())
    }
    fn access(&self, p: &str, write: bool, env: &Env, line: usize) -> Result<(), Diagnostic> {
        if env.moved.iter().any(|m| overlaps(m, p)) {
            return Err(error(line, format!("use after move: {p}")));
        }
        let n = root(p);
        let var = env
            .vars
            .get(n)
            .ok_or_else(|| error(line, format!("unknown local {n}")))?;
        if write
            && !var.mutable
            && !matches!(
                var.ty,
                Ty::Ref(_, true)
                    | Ty::Slice(_, true)
                    | Ty::Split(_)
                    | Ty::Ptr(_)
                    | Ty::NonNull(_)
                    | Ty::BorrowedPtr(_)
                    | Ty::ForeignBuffer(_)
            )
        {
            return Err(error(line, format!("{n} is immutable")));
        }
        let origins = if var.loans.is_empty() {
            vec![Loan {
                path: p.into(),
                mutable: write,
                range: env.range,
                holder: n.into(),
                parents: BTreeSet::new(),
            }]
        } else {
            var.loans
                .iter()
                .map(|l| Loan {
                    path: format!("{}{}", l.path, &p[n.len()..]),
                    mutable: write,
                    range: subrange(l.range, env.range),
                    holder: n.into(),
                    parents: l.parents.clone(),
                })
                .collect()
        };
        for v in env.vars.values() {
            for loan in &v.loans {
                for candidate in &origins {
                    if loan.holder != n
                        && !candidate.parents.contains(&loan.holder)
                        && conflict(loan, candidate)
                    {
                        return Err(error(
                            line,
                            format!(
                                "borrow conflict: {p} overlaps {} held by {}",
                                loan.path, loan.holder
                            ),
                        ));
                    }
                }
            }
        }
        for loan in &env.pending {
            for candidate in &origins {
                if loan.holder != n
                    && !candidate.parents.contains(&loan.holder)
                    && conflict(loan, candidate)
                {
                    return Err(error(line, "overlapping argument borrows"));
                }
            }
        }
        Ok(())
    }
    fn move_value(&mut self, e: &Expr, env: &mut Env) -> Result<(), Diagnostic> {
        if !affine(e.ty(), self.records) {
            return Ok(());
        }
        if let Some(p) = place(e) {
            return Err(error(e.line, format!("{} requires explicit move", p)));
        }
        if matches!(&e.op,Op::Call(n,_,_) if n=="slice"||n=="mut_slice") {
            return Ok(());
        }
        let _ = env;
        Ok(())
    }
    fn expr(&mut self, e: &mut Expr, want: Option<&Ty>, env: &mut Env) -> Result<Ty, Diagnostic> {
        let l = e.line;
        let t = match &mut e.op {
            Op::Number(n) => {
                let t = want.filter(|t| t.scalar().is_some()).cloned().unwrap_or(
                    if !n.starts_with("0x") && !n.starts_with("0X") && n.contains(['.', 'e', 'E']) {
                        Ty::Scalar(Scalar::F64)
                    } else {
                        Ty::Scalar(Scalar::USize)
                    },
                );
                let s = t.scalar().unwrap();
                let text = n.replace('_', "");
                if s.float() {
                    let f = float_literal(&text, s)
                        .ok_or_else(|| error(l, "invalid floating literal"))?;
                    if !f.is_finite() {
                        return Err(error(
                            l,
                            "floating literal out of range; use infinity explicitly",
                        ));
                    }
                } else {
                    let v = integer_literal(&text)
                        .ok_or_else(|| error(l, "invalid integer literal"))?;
                    let max = if s.signed() {
                        (1u128 << (s.bits() - 1)) - 1
                    } else {
                        (1u128 << s.bits()) - 1
                    };
                    if v > max {
                        return Err(error(l, format!("literal exceeds {}", s.name())));
                    }
                }
                t
            }
            Op::Bool(_) => Ty::Bool,
            Op::Text(_) => Ty::Str,
            Op::Name(n) => {
                if ["Relaxed", "Acquire", "Release", "AcqRel", "SeqCst"].contains(&n.as_str()) {
                    Ty::Scalar(Scalar::U32)
                } else {
                    self.access(n, false, env, l)?;
                    env.vars[n].ty.clone()
                }
            }
            Op::Move(a) => {
                let t = if place(a).is_some() {
                    let t = self.place_type(a, env)?;
                    if let Some(p) = place(a) {
                        self.access(&p, false, env, l)?;
                    }
                    t
                } else {
                    self.expr(a, want, env)?
                };
                if !affine(&t, self.records) {
                    if t.borrowed() {
                        self.propagate_borrow(a, env);
                    }
                    e.ty = Some(t.clone());
                    return Ok(t);
                }
                let p = place(a).ok_or_else(|| error(l, "move requires a local place"))?;
                if env
                    .vars
                    .get(root(&p))
                    .is_some_and(|v| !owned(&v.ty, self.records))
                    && !matches!(a.op, Op::Name(_))
                    && !zeroable(&t, self.records)
                {
                    return Err(error(
                        l,
                        "moving through a borrow requires a valid replacement value",
                    ));
                }
                let mut movable = env.clone();
                if let Some(v) = movable.vars.get_mut(root(&p)) {
                    v.mutable = true;
                }
                if !self.writable(a, &movable) {
                    return Err(error(l, "cannot move through a shared borrow"));
                }
                if !env.returning
                    && env
                        .bounds
                        .values()
                        .any(|b| b == &p || b.starts_with(&format!("{p}.")))
                {
                    return Err(error(l, "move would invalidate a proved traversal bound"));
                }
                self.access(&p, true, &movable, l)?;
                if p.contains("[*]") {
                    return Err(error(l, "moving an element requires a constant index"));
                }
                if t.borrowed() {
                    self.propagate_borrow(a, env);
                    if matches!(a.op, Op::Name(_)) {
                        if let Some(v) = env.vars.get_mut(root(&p)) {
                            v.loans.clear();
                        }
                    }
                }
                env.moved.insert(p);
                t
            }
            Op::Borrow(a, m) => {
                let t = self.place_type(a, env)?;
                let p = place(a);
                if p.is_none() {
                    if env.unsafe_ > 0 && raw_place(a) {
                        e.ty = Some(Ty::Ref(Box::new(t.clone()), *m));
                        return Ok(Ty::Ref(Box::new(t), *m));
                    }
                    return Err(error(l, "a borrow requires an addressable local place"));
                }
                let p = p.unwrap();
                if *m
                    && matches!(t, Ty::Buffer(_) | Ty::Array(..) | Ty::Slice(..))
                    && env.bounds.values().any(|b| overlaps(&p, b))
                {
                    return Err(error(
                        l,
                        "mutable storage borrow would invalidate a proved traversal bound",
                    ));
                }
                if *m && !self.writable(a, env) {
                    return Err(error(l, "mutable borrow of an immutable place"));
                }
                if let Op::Field(base, _) = &a.op {
                    let bt = match base.ty() {
                        Ty::Ref(t, _) | Ty::Boxed(t) => &**t,
                        t => t,
                    };
                    if let Ty::Record(n) = bt {
                        if self.records.iter().any(|r| r.name == *n && r.packed) {
                            return Err(error(l, "packed fields cannot be borrowed"));
                        }
                    }
                }
                self.access(&p, *m, env, l)?;
                let v = &env.vars[root(&p)];
                let loans = if v.loans.is_empty() {
                    vec![Loan {
                        path: p.clone(),
                        mutable: *m,
                        range: env.range,
                        holder: "$argument".into(),
                        parents: BTreeSet::from([root(&p).to_string()]),
                    }]
                } else {
                    v.loans
                        .iter()
                        .map(|q| Loan {
                            path: format!("{}{}", q.path, &p[root(&p).len()..]),
                            mutable: *m,
                            range: subrange(q.range, env.range),
                            holder: "$argument".into(),
                            parents: child_parents(q, root(&p)),
                        })
                        .collect()
                };
                env.pending.extend(loans);
                Ty::Ref(Box::new(t), *m)
            }
            Op::Unary(op, a) if op == "*" => {
                let t = self.expr(a, None, env)?;
                match t {
                    Ty::Ref(t, _) | Ty::Boxed(t) => *t,
                    Ty::Ptr(t) | Ty::NonNull(t) | Ty::BorrowedPtr(t) => {
                        if env.unsafe_ == 0 {
                            return Err(error(l, "raw pointer dereference requires unsafe"));
                        }
                        if !matches!(&*t, Ty::Atomic(_)) {
                            self.effect.unsafe_memory = true;
                        }
                        self.effect.fail = true;
                        *t
                    }
                    _ => return Err(error(l, "cannot dereference this type")),
                }
            }
            Op::Unary(op, a) => {
                if op == "-" {
                    if let Op::Number(n) = &a.op {
                        if let Some(Ty::Scalar(s)) = want {
                            if s.signed() {
                                if integer_literal(n) == Some(1u128 << (s.bits() - 1)) {
                                    a.ty = Some(Ty::Scalar(*s));
                                    e.ty = Some(Ty::Scalar(*s));
                                    return Ok(Ty::Scalar(*s));
                                }
                            }
                        }
                    }
                }
                let t = self.expr(a, want, env)?;
                match op.as_str() {
                    "!" if t == Ty::Bool => Ty::Bool,
                    "-" if t.scalar().is_some_and(|s| s.float() || s.signed()) => {
                        if !t.scalar().unwrap().float() {
                            self.effect.fail = true;
                        }
                        t
                    }
                    "~" if t.scalar().is_some_and(|s| !s.float()) => t,
                    _ => return Err(error(l, "invalid unary operand")),
                }
            }
            Op::Binary(op, a, b) => {
                let context = if matches!(op.as_str(), "&&" | "||") {
                    Some(Ty::Bool)
                } else {
                    want.filter(|t| t.scalar().is_some()).cloned()
                };
                let at = self.expr(a, context.as_ref(), env)?;
                self.expect(b, &at, env)?;
                if ["&&", "||"].contains(&op.as_str()) {
                    if at != Ty::Bool {
                        return Err(error(l, "logical operands must be Bool"));
                    }
                    Ty::Bool
                } else if ["==", "!=", "<", ">", "<=", ">="].contains(&op.as_str()) {
                    if matches!(at, Ty::Handle | Ty::Ptr(_) | Ty::NonNull(_))
                        && !matches!(op.as_str(), "==" | "!=")
                    {
                        return Err(error(
                            l,
                            "pointer ordering is not defined; only equality is supported",
                        ));
                    }
                    if !matches!(
                        at,
                        Ty::Scalar(_) | Ty::Bool | Ty::Handle | Ty::Ptr(_) | Ty::NonNull(_)
                    ) {
                        return Err(error(l, "comparison requires scalar operands"));
                    }
                    Ty::Bool
                } else {
                    let s = at
                        .scalar()
                        .ok_or_else(|| error(l, "arithmetic requires machine scalars"))?;
                    if s.float() && ["%", "&", "|", "^", "<<", ">>"].contains(&op.as_str()) {
                        return Err(error(l, "bit operations require integer operands"));
                    }
                    if !s.float() && ["+", "-", "*", "/", "%", "<<", ">>"].contains(&op.as_str()) {
                        self.effect.fail = true;
                    }
                    at
                }
            }
            Op::Field(..) | Op::Index(..) | Op::IndexProven(..) => {
                let t = self.place_type(e, env)?;
                if raw_place(e) && !matches!(t, Ty::Atomic(_)) {
                    self.effect.unsafe_memory = true;
                }
                if let Some(p) = place(e) {
                    self.access(&p, false, env, l)?;
                }
                e.ty = Some(t.clone());
                return Ok(t);
            }
            Op::Invoke(f, args) => {
                let t = self.expr(f, None, env)?;
                let Ty::FnPtr(p, r) = t else {
                    return Err(error(l, "indirect call requires FnPtr"));
                };
                if env.unsafe_ == 0 {
                    return Err(error(l,"indirect ABI call requires unsafe and an explicit trusted callback contract"));
                }
                self.effect.alloc = true;
                self.effect.blocking = true;
                self.effect.io = true;
                self.effect.fail = true;
                self.effect.unbounded = true;
                self.effect.unsafe_memory = true;
                self.args(args, &p, env, l)?;
                *r
            }
            Op::Call(n, types, args) => {
                let before = env.pending.len();
                let t = self.call(n, types, args, want, env, l)?;
                if !t.borrowed() {
                    env.pending.truncate(before);
                }
                t
            }
        };
        e.ty = Some(t.clone());
        Ok(t)
    }
    fn writable(&self, e: &Expr, env: &Env) -> bool {
        match &e.op {
            Op::Name(n) => env.vars.get(n).is_some_and(|v| v.mutable),
            Op::Field(a, _) => match a.ty() {
                Ty::Ref(_, m) => *m,
                Ty::Ptr(_) | Ty::NonNull(_) | Ty::BorrowedPtr(_) => true,
                _ => self.writable(a, env),
            },
            Op::Index(a, _) | Op::IndexProven(a, _) => match a.ty() {
                Ty::Slice(_, m) => *m,
                Ty::ForeignBuffer(_) => true,
                Ty::Ref(t, m) => *m && !matches!(&**t, Ty::Slice(_, false)),
                _ => self.writable(a, env),
            },
            Op::Unary(op, a) if op == "*" => match a.ty() {
                Ty::Ref(_, m) => *m,
                Ty::Ptr(_) | Ty::NonNull(_) | Ty::BorrowedPtr(_) => true,
                Ty::Boxed(_) => self.writable(a, env),
                _ => false,
            },
            _ => false,
        }
    }
    fn place_type(&mut self, e: &mut Expr, env: &mut Env) -> Result<Ty, Diagnostic> {
        let l = e.line;
        let t = match &mut e.op {
            Op::Unary(op, a) if op == "*" => {
                let t = self.expr(a, None, env)?;
                match t {
                    Ty::Ref(t, _) | Ty::Boxed(t) => *t,
                    Ty::Ptr(t) | Ty::NonNull(t) | Ty::BorrowedPtr(t) => {
                        if env.unsafe_ == 0 {
                            return Err(error(l, "raw dereference requires unsafe"));
                        }
                        self.effect.fail = true;
                        *t
                    }
                    _ => return Err(error(l, "invalid dereference")),
                }
            }
            Op::Name(n) => env
                .vars
                .get(n)
                .map(|v| v.ty.clone())
                .ok_or_else(|| error(l, "unknown place"))?,
            Op::Field(a, n) => {
                let at = if place(a).is_some() {
                    self.place_type(a, env)?
                } else {
                    self.expr(a, None, env)?
                };
                if owned(&at, self.records) && place(a).is_none() && !raw_place(a) {
                    return Err(error(l, "bind an owning result before projecting a field"));
                }
                let rt = match &at {
                    Ty::Ref(t, _) | Ty::Boxed(t) => &**t,
                    _ => &at,
                };
                if let Ty::Split(t) = rt {
                    if n != "left" && n != "right" {
                        return Err(error(l, "Split fields are left and right"));
                    }
                    Ty::Slice(t.clone(), true)
                } else {
                    let Ty::Record(r) = rt else {
                        return Err(error(l, "field access requires a record"));
                    };
                    let d = self.records.iter().find(|d| d.name == *r).unwrap();
                    if d.union && env.unsafe_ == 0 {
                        return Err(error(l, "union field access requires unsafe"));
                    }
                    d.fields
                        .iter()
                        .find(|(f, _)| f == n)
                        .map(|(_, t)| t.clone())
                        .ok_or_else(|| error(l, "unknown field"))?
                }
            }
            Op::Index(a, i) | Op::IndexProven(a, i) => {
                if place(a).is_none() {
                    return Err(error(l, "bind dense storage before indexing it"));
                }
                let at = if place(a).is_some() {
                    self.place_type(a, env)?
                } else {
                    self.expr(a, None, env)?
                };
                let saved = env.pending.len();
                self.propagate_borrow(a, env);
                for q in &mut env.pending[saved..] {
                    q.holder = "$index_base".into();
                    q.mutable = false;
                }
                self.expect(i, &Ty::Scalar(Scalar::USize), env)?;
                env.pending.truncate(saved);
                let proven = if let (Some(p), Op::Name(n)) = (place(a), &i.op) {
                    env.bounds.get(n) == Some(&p)
                } else {
                    false
                };
                self.effect.fail |= !proven;
                let t = if let Ty::Ref(t, _) = at { *t } else { at };
                match t {
                    Ty::Array(t, _) | Ty::Buffer(t) | Ty::Slice(t, _) | Ty::ForeignBuffer(t) => *t,
                    _ => return Err(error(l, "index requires dense storage")),
                }
            }
            _ => return self.expr(e, None, env),
        };
        if let Op::Index(a, i) | Op::IndexProven(a, i) = &e.op {
            let proven = if let (Some(p), Op::Name(n)) = (place(a), &i.op) {
                env.bounds.get(n) == Some(&p)
            } else {
                false
            };
            e.op = if proven {
                Op::IndexProven(a.clone(), i.clone())
            } else {
                Op::Index(a.clone(), i.clone())
            };
        }
        e.ty = Some(t.clone());
        Ok(t)
    }
    fn args(
        &mut self,
        args: &mut [Expr],
        params: &[Ty],
        env: &mut Env,
        l: usize,
    ) -> Result<(), Diagnostic> {
        if args.len() != params.len() {
            return Err(error(
                l,
                format!("expected {} arguments, found {}", params.len(), args.len()),
            ));
        }
        for (a, t) in args.iter_mut().zip(params) {
            self.expect(a, t, env)?;
            if !matches!(t, Ty::Ref(..) | Ty::Slice(_, false) | Ty::BorrowedPtr(_)) {
                self.move_value(a, env)?;
            }
            if matches!(t,Ty::Ref(q,true) if matches!(&**q,Ty::Buffer(_)|Ty::Array(..)|Ty::Slice(..)))
                && place(a).is_some_and(|p| env.bounds.values().any(|b| overlaps(&p, b)))
            {
                return Err(error(l, "call could invalidate a proved traversal bound"));
            }
            if matches!(t, Ty::Ref(..)) {
                let count = env.pending.len();
                self.propagate_borrow(a, env);
                for q in &mut env.pending[count..] {
                    q.holder = "$call_argument".into();
                    if let Ty::Ref(_, m) = t {
                        q.mutable = *m;
                    }
                }
            }
        }
        Ok(())
    }
    fn propagate_borrow(&self, arg: &Expr, env: &mut Env) {
        let target = match &arg.op {
            Op::Borrow(a, _) | Op::Move(a) => place(a),
            _ => place(arg),
        };
        if let Some(p) = target {
            if let Some(v) = env.vars.get(root(&p)) {
                if v.loans.is_empty() {
                    env.pending.push(Loan {
                        path: p.clone(),
                        mutable: matches!(
                            arg.ty(),
                            Ty::Ref(_, true) | Ty::Slice(_, true) | Ty::Split(_)
                        ),
                        range: env.range,
                        holder: "$view".into(),
                        parents: BTreeSet::from([root(&p).to_string()]),
                    });
                } else {
                    let loans = v
                        .loans
                        .iter()
                        .map(|q| Loan {
                            path: format!("{}{}", q.path, &p[root(&p).len()..]),
                            mutable: q.mutable,
                            range: subrange(q.range, env.range),
                            holder: q.holder.clone(),
                            parents: child_parents(q, root(&p)),
                        })
                        .collect::<Vec<_>>();
                    env.pending.extend(loans);
                }
            }
        }
    }
    fn call(
        &mut self,
        n: &str,
        types: &[Ty],
        args: &mut [Expr],
        want: Option<&Ty>,
        env: &mut Env,
        l: usize,
    ) -> Result<Ty, Diagnostic> {
        if let Some(f) = self.functions.iter().find(|f| f.name == n) {
            if !types.is_empty() {
                return Err(error(
                    l,
                    "native functions are monomorphic; generic operations require an intrinsic",
                ));
            }
            if f.unsafe_ && env.unsafe_ == 0 {
                return Err(error(
                    l,
                    format!("unsafe call {n} requires an unsafe block"),
                ));
            }
            let before = env.clone();
            let pending_before = env.pending.len();
            self.effect.calls.insert(n.into());
            self.args(
                args,
                &f.params.iter().map(|(_, t)| t.clone()).collect::<Vec<_>>(),
                env,
                l,
            )?;
            env.pending.truncate(pending_before);
            if let Some(source) = &f.borrow_result {
                let index = f.params.iter().position(|(p, _)| p == source).unwrap();
                let mut origins = before.clone();
                origins.pending.clear();
                self.propagate_borrow(&args[index], &mut origins);
                if origins.pending.is_empty() {
                    return Err(error(
                        l,
                        "bind the lifetime source before calling a borrowing function",
                    ));
                }
                for mut q in origins.pending {
                    q.mutable = matches!(
                        f.ret,
                        Ty::Ref(_, true) | Ty::Slice(_, true) | Ty::Split(_) | Ty::ForeignBuffer(_)
                    );
                    q.holder = "$result".into();
                    env.pending.push(q);
                }
            }
            return Ok(f.ret.clone());
        }
        if let Some(r) = self.records.iter().find(|r| r.name == n) {
            if r.union {
                return Err(error(
                    l,
                    "construct unions with union_zero, then assign the active field in unsafe",
                ));
            }
            self.args(
                args,
                &r.fields.iter().map(|(_, t)| t.clone()).collect::<Vec<_>>(),
                env,
                l,
            )?;
            return Ok(Ty::Record(n.into()));
        }
        if let Some(s) = Scalar::parse(n) {
            if args.len() != 1 || !types.is_empty() {
                return Err(error(l, "numeric constructor expects one operand"));
            }
            let source = self.expr(&mut args[0], Some(&Ty::Scalar(s)), env)?;
            if source.scalar().is_none() {
                return Err(error(l, "numeric conversion requires a machine scalar"));
            }
            self.effect.fail |= source != Ty::Scalar(s) && !s.float();
            return Ok(Ty::Scalar(s));
        }
        let type_arg = || {
            types
                .first()
                .cloned()
                .or_else(|| want.and_then(|t| t.element().cloned()))
                .ok_or_else(|| error(l, format!("{n} requires an explicit element type")))
        };
        let arity = |a: usize| {
            if args.len() == a {
                Ok(())
            } else {
                Err(error(l, format!("{n} expects {a} arguments")))
            }
        };
        match n {
            "call_trusted" => {
                if env.unsafe_ == 0 {
                    return Err(error(
                        l,
                        "call_trusted requires unsafe and a trusted foreign callback contract",
                    ));
                }
                if args.len() < 2 || !types.is_empty() {
                    return Err(error(
                        l,
                        "call_trusted expects a contract, a function pointer and its arguments",
                    ));
                }
                let Op::Name(name) = &args[0].op else {
                    return Err(error(
                        l,
                        "callback contract must name a trusted extern declaration",
                    ));
                };
                let contract = self
                    .functions
                    .iter()
                    .find(|f| f.name == *name && f.library.is_some())
                    .cloned()
                    .ok_or_else(|| {
                        error(
                            l,
                            "callback contract must name a trusted extern declaration",
                        )
                    })?;
                let signature = Ty::FnPtr(
                    contract.params.iter().map(|(_, t)| t.clone()).collect(),
                    Box::new(contract.ret.clone()),
                );
                args[0].ty = Some(signature.clone());
                self.expect(&mut args[1], &signature, env)?;
                self.effect.calls.insert(contract.name.clone());
                if let Op::Call(op, _, a) = &args[1].op {
                    if op == "fn_ptr" {
                        if let Some(Expr {
                            op: Op::Name(name), ..
                        }) = a.first()
                        {
                            self.effect.calls.insert(name.clone());
                        }
                    }
                }
                self.args(
                    &mut args[2..],
                    &contract
                        .params
                        .iter()
                        .map(|(_, t)| t.clone())
                        .collect::<Vec<_>>(),
                    env,
                    l,
                )?;
                Ok(contract.ret)
            }
            "size_of" | "align_of" | "abi_version" => {
                arity(0)?;
                if types.len() != 1 {
                    return Err(error(l, "layout query requires exactly one type"));
                }
                validate_ty(&types[0], self.records, l)?;
                layout_ty(
                    &types[0],
                    self.records,
                    &mut BTreeMap::new(),
                    &mut BTreeSet::new(),
                )
                .map_err(|s| error(l, s))?;
                if n == "abi_version" && !matches!(types[0], Ty::Record(_)) {
                    return Err(error(l, "ABI version query requires a record type"));
                }
                Ok(Ty::Scalar(Scalar::USize))
            }
            "resource" => {
                arity(1)?;
                let t = types
                    .first()
                    .cloned()
                    .or_else(|| want.cloned())
                    .ok_or_else(|| error(l, "resource requires Resource<T,finalizer>"))?;
                let Ty::Resource(value, finalizer) = &t else {
                    return Err(error(l, "resource requires Resource<T,finalizer>"));
                };
                validate_ty(&t, self.records, l)?;
                let f = self
                    .functions
                    .iter()
                    .find(|f| f.name == *finalizer)
                    .ok_or_else(|| error(l, "unknown resource finalizer"))?;
                if f.unsafe_ && env.unsafe_ == 0 {
                    return Err(error(
                        l,
                        "unsafe resource finalizer requires an unsafe context",
                    ));
                }
                self.expect(&mut args[0], value, env)?;
                self.move_value(&args[0], env)?;
                self.effect.alloc = true;
                self.effect.blocking = true;
                self.effect.fail = true;
                self.effect.calls.insert(finalizer.clone());
                Ok(t)
            }
            "resource_get" | "resource_get_mut" => {
                arity(1)?;
                let t = self.expr(&mut args[0], None, env)?;
                let Ty::Ref(t, mutable) = t else {
                    return Err(error(l, "resource access requires an explicit borrow"));
                };
                let Ty::Resource(value, _) = *t else {
                    return Err(error(l, "resource access requires Resource"));
                };
                let write = n == "resource_get_mut";
                if write && env.unsafe_ == 0 {
                    return Err(error(
                        l,
                        "mutable resource access requires unsafe: preserve the finalizer invariant",
                    ));
                }
                if write && !mutable {
                    return Err(error(
                        l,
                        "mutable resource access requires an exclusive borrow",
                    ));
                }
                self.propagate_borrow(&args[0], env);
                Ok(Ty::Ref(value, write))
            }
            "resource_close" | "resource_into_inner" => {
                arity(1)?;
                let t = self.expr(&mut args[0], None, env)?;
                let Ty::Resource(value, finalizer) = &t else {
                    return Err(error(l, "resource transfer requires Resource"));
                };
                if n == "resource_into_inner" && env.unsafe_ == 0 {
                    return Err(error(l, "cancelling a resource finalizer requires unsafe"));
                }
                self.move_value(&args[0], env)?;
                self.effect.blocking = true;
                if n == "resource_close" {
                    self.effect.calls.insert(finalizer.clone());
                    Ok(Ty::Unit)
                } else {
                    self.effect.fail = true;
                    Ok((**value).clone())
                }
            }
            "cast" => {
                arity(1)?;
                let t = types
                    .first()
                    .ok_or_else(|| error(l, "cast requires a target type"))?;
                if types.len() != 1 || t.scalar().is_none() {
                    return Err(error(l, "cast target must be a machine scalar"));
                }
                let src = self.expr(&mut args[0], None, env)?;
                if src.scalar().is_none() {
                    return Err(error(l, "cast source must be a machine scalar"));
                }
                self.effect.fail |=
                    !t.scalar().unwrap().float() || src.scalar().is_some_and(|s| s.float());
                Ok(t.clone())
            }
            "thread_bind" => {
                arity(1)?;
                let t = types
                    .first()
                    .cloned()
                    .or_else(|| {
                        want.and_then(|t| {
                            if let Ty::ThreadBound(t) = t {
                                Some((**t).clone())
                            } else {
                                None
                            }
                        })
                    })
                    .ok_or_else(|| error(l, "thread_bind requires a value type"))?;
                self.expect(&mut args[0], &t, env)?;
                self.move_value(&args[0], env)?;
                self.effect.affine = true;
                Ok(Ty::ThreadBound(Box::new(t)))
            }
            "thread_get" | "thread_get_mut" => {
                arity(1)?;
                let t = self.expr(&mut args[0], None, env)?;
                let Ty::Ref(t, m) = t else {
                    return Err(error(l, "thread access requires an explicit borrow"));
                };
                let Ty::ThreadBound(t) = *t else {
                    return Err(error(l, "thread access requires ThreadBound<T>"));
                };
                if n == "thread_get_mut" && !m {
                    return Err(error(l, "mutable thread access requires &mut"));
                }
                self.effect.affine = true;
                self.effect.fail = true;
                self.propagate_borrow(&args[0], env);
                Ok(Ty::Ref(t, n == "thread_get_mut"))
            }
            "window_open" | "window_child" | "window_attach" => {
                arity(if n == "window_open" || n == "window_attach" {
                    3
                } else {
                    4
                })?;
                if n != "window_open" && env.unsafe_ == 0 {
                    return Err(error(
                        l,
                        "embedding requires a valid borrowed host window in unsafe",
                    ));
                }
                let params = match n {
                    "window_open" => vec![
                        Ty::Str,
                        Ty::Scalar(Scalar::USize),
                        Ty::Scalar(Scalar::USize),
                    ],
                    "window_child" => vec![
                        Ty::Scalar(Scalar::USize),
                        Ty::Str,
                        Ty::Scalar(Scalar::USize),
                        Ty::Scalar(Scalar::USize),
                    ],
                    _ => vec![
                        Ty::Scalar(Scalar::USize),
                        Ty::Scalar(Scalar::USize),
                        Ty::Scalar(Scalar::USize),
                    ],
                };
                self.args(args, &params, env, l)?;
                self.effect.alloc = true;
                self.effect.blocking = true;
                self.effect.io = true;
                self.effect.affine = true;
                self.effect.fail = true;
                Ok(Ty::Window)
            }
            "window_resize" | "window_focus" | "window_dpi" | "window_poll" | "window_gpu"
            | "window_scene" | "window_input" | "window_handle" | "window_read" => {
                let count = match n {
                    "window_resize" => 3,
                    "window_poll" => 2,
                    "window_scene" => 6,
                    "window_input" => 5,
                    _ => 1,
                };
                arity(count)?;
                self.expect(&mut args[0], &Ty::Ref(Box::new(Ty::Window), false), env)?;
                if n == "window_scene" {
                    let t = self.expr(&mut args[1], None, env)?;
                    if !matches!(t,Ty::Ref(ref b,_) if matches!(&**b,Ty::Buffer(t)|Ty::Array(t,_)|Ty::Slice(t,_) if **t==Ty::Scalar(Scalar::U64)))
                    {
                        return Err(error(l, "scene requires a borrow of U64 drawing commands"));
                    }
                }
                for a in args
                    .iter_mut()
                    .skip(if n == "window_scene" { 2 } else { 1 })
                {
                    self.expect(a, &Ty::Scalar(Scalar::USize), env)?;
                }
                self.effect.alloc = true;
                self.effect.blocking = true;
                self.effect.io = true;
                self.effect.affine = true;
                self.effect.fail = true;
                Ok(if n == "window_dpi" {
                    Ty::Scalar(Scalar::F64)
                } else if n == "window_handle" {
                    Ty::Scalar(Scalar::USize)
                } else if n == "window_read" {
                    Ty::Buffer(Box::new(Ty::Scalar(Scalar::U64)))
                } else if n == "window_poll" {
                    Ty::Array(Box::new(Ty::Scalar(Scalar::U64)), 4)
                } else {
                    Ty::Unit
                })
            }
            "buffer" | "buffer_aligned" => {
                arity(if n == "buffer" { 1 } else { 2 })?;
                let t = type_arg()?;
                if !zeroable(&t, self.records) && !matches!(&args[0].op,Op::Number(n) if n=="0") {
                    return Err(error(l,"element type requires explicit initialization; create an empty buffer and push values"));
                }
                validate_ty(&t, self.records, l)?;
                self.expect(&mut args[0], &Ty::Scalar(Scalar::USize), env)?;
                if n == "buffer_aligned" {
                    self.expect(&mut args[1], &Ty::Scalar(Scalar::USize), env)?;
                }
                self.effect.alloc = true;
                self.effect.blocking = true;
                self.effect.fail = true;
                Ok(Ty::Buffer(Box::new(t)))
            }
            "box" => {
                arity(1)?;
                let t = type_arg()?;
                validate_ty(&t, self.records, l)?;
                self.expect(&mut args[0], &t, env)?;
                self.move_value(&args[0], env)?;
                self.effect.alloc = true;
                self.effect.blocking = true;
                self.effect.fail = true;
                Ok(Ty::Boxed(Box::new(t)))
            }
            "zero" | "union_zero" => {
                arity(0)?;
                let t = types
                    .first()
                    .cloned()
                    .or_else(|| want.cloned())
                    .ok_or_else(|| error(l, "zero requires a target type"))?;
                validate_ty(&t, self.records, l)?;
                if contains_borrow(&t, self.records) || !zeroable(&t, self.records) {
                    return Err(error(l, "this type has no valid zero value"));
                }
                Ok(t)
            }
            "len" => {
                arity(1)?;
                let t = self.expr(&mut args[0], None, env)?;
                let t = if let Ty::Ref(t, _) = &t { &**t } else { &t };
                if matches!(t, Ty::Buffer(_))
                    && place(&args[0]).is_none()
                    && !matches!(args[0].ty(), Ty::Ref(..))
                {
                    return Err(error(
                        l,
                        "bind or borrow owned storage before inspecting it",
                    ));
                }
                if !matches!(
                    t,
                    Ty::Array(..) | Ty::Buffer(_) | Ty::Slice(..) | Ty::ForeignBuffer(_) | Ty::Str
                ) {
                    return Err(error(l, "len requires dense storage or Utf8"));
                }
                Ok(Ty::Scalar(Scalar::USize))
            }
            "array" => {
                let t = types
                    .first()
                    .cloned()
                    .or_else(|| want.cloned())
                    .ok_or_else(|| error(l, "array requires Array<T,N> type"))?;
                let Ty::Array(item, count) = &t else {
                    return Err(error(l, "array requires Array<T,N> type"));
                };
                validate_ty(&t, self.records, l)?;
                if args.len() != *count {
                    return Err(error(
                        l,
                        "array initializer count differs from its fixed length",
                    ));
                }
                self.args(args, &vec![(**item).clone(); *count], env, l)?;
                Ok(t)
            }
            "push" => {
                arity(2)?;
                let t = self.expr(&mut args[0], None, env)?;
                let Ty::Ref(t, true) = t else {
                    return Err(error(l, "push requires &mut Buffer<T>"));
                };
                let Ty::Buffer(item) = *t else {
                    return Err(error(l, "push requires &mut Buffer<T>"));
                };
                self.expect(&mut args[1], &item, env)?;
                self.move_value(&args[1], env)?;
                self.effect.alloc = true;
                self.effect.blocking = true;
                self.effect.fail = true;
                Ok(Ty::Unit)
            }
            "resize" => {
                arity(2)?;
                if place(&args[0]).is_some_and(|p| env.bounds.values().any(|b| overlaps(&p, b))) {
                    return Err(error(l, "resize would invalidate a proved traversal bound"));
                }
                let t = self.expr(&mut args[0], None, env)?;
                if !matches!(t,Ty::Ref(ref t,true) if matches!(&**t,Ty::Buffer(_))) {
                    return Err(error(l, "resize requires &mut Buffer"));
                }
                self.expect(&mut args[1], &Ty::Scalar(Scalar::USize), env)?;
                self.effect.alloc = true;
                self.effect.blocking = true;
                self.effect.fail = true;
                Ok(Ty::Unit)
            }
            "split_at_mut" => {
                arity(2)?;
                let t = self.expr(&mut args[0], None, env)?;
                let Ty::Ref(t, true) = t else {
                    return Err(error(
                        l,
                        "split_at_mut requires an exclusive mutable borrow",
                    ));
                };
                let item = match *t {
                    Ty::ForeignBuffer(t) | Ty::Buffer(t) | Ty::Array(t, _) | Ty::Slice(t, true) => {
                        t
                    }
                    _ => return Err(error(l, "split_at_mut requires mutable dense storage")),
                };
                self.expect(&mut args[1], &Ty::Scalar(Scalar::USize), env)?;
                self.effect.fail = true;
                self.propagate_borrow(&args[0], env);
                Ok(Ty::Split(item))
            }
            "slice" | "mut_slice" => {
                arity(3)?;
                let range = match (&args[1].op, &args[2].op) {
                    (Op::Number(a), Op::Number(b)) => a
                        .parse::<usize>()
                        .ok()
                        .zip(b.parse::<usize>().ok())
                        .and_then(|(a, b)| a.checked_add(b).map(|end| (a, end))),
                    _ => None,
                };
                let old = env.range;
                env.range = range;
                let result = self.expr(&mut args[0], None, env);
                env.range = old;
                let t = result?;
                let Ty::Ref(t, m) = t else {
                    return Err(error(l, "slice requires an explicit owner borrow"));
                };
                if n == "mut_slice" && !m {
                    return Err(error(l, "MutSlice requires an exclusive mutable borrow"));
                }
                let item = match *t {
                    Ty::ForeignBuffer(t) | Ty::Buffer(t) | Ty::Array(t, _) | Ty::Slice(t, _) => t,
                    _ => return Err(error(l, "slice requires a dense buffer, array or slice")),
                };
                for a in &mut args[1..] {
                    self.expect(a, &Ty::Scalar(Scalar::USize), env)?;
                }
                self.effect.fail = true;
                if !matches!(args[0].op, Op::Borrow(..)) {
                    let saved = env.range;
                    env.range = range;
                    self.propagate_borrow(&args[0], env);
                    env.range = saved;
                }
                Ok(Ty::Slice(item, n == "mut_slice"))
            }
            "borrowed_ptr" => {
                arity(1)?;
                let t = self.expr(&mut args[0], None, env)?;
                let Ty::Ref(t, _) = t else {
                    return Err(error(l, "borrowed_ptr requires a lifetime-bound reference"));
                };
                self.propagate_borrow(&args[0], env);
                Ok(Ty::BorrowedPtr(t))
            }
            "as_raw" => {
                arity(1)?;
                if env.unsafe_ == 0 {
                    return Err(error(l, "escaping a borrowed pointer requires unsafe"));
                }
                let t = self.expr(&mut args[0], None, env)?;
                let Ty::BorrowedPtr(t) = t else {
                    return Err(error(l, "as_raw requires BorrowedPtr<T>"));
                };
                Ok(Ty::Ptr(t))
            }
            "data" => {
                arity(1)?;
                if env.unsafe_ == 0 {
                    return Err(error(l, "exposing a raw pointer requires unsafe"));
                }
                let t = self.expr(&mut args[0], None, env)?;
                let t = if let Ty::Ref(t, _) = t { *t } else { t };
                let item = t
                    .element()
                    .ok_or_else(|| error(l, "data requires dense storage"))?;
                Ok(Ty::Ptr(Box::new(item.clone())))
            }
            "foreign" => {
                arity(2)?;
                if env.unsafe_ == 0 {
                    return Err(error(
                        l,
                        "foreign memory requires unsafe and a host lifetime contract",
                    ));
                }
                let t = self.expr(&mut args[0], None, env)?;
                let Ty::Ptr(t) = t else {
                    return Err(error(l, "foreign requires Ptr<T>"));
                };
                self.expect(&mut args[1], &Ty::Scalar(Scalar::USize), env)?;
                self.effect.fail = true;
                Ok(Ty::ForeignBuffer(t))
            }
            "ptr_offset" => {
                arity(2)?;
                if env.unsafe_ == 0 {
                    return Err(error(l, "pointer arithmetic requires unsafe"));
                }
                let t = self.expr(&mut args[0], None, env)?;
                if !matches!(t, Ty::Ptr(_) | Ty::NonNull(_) | Ty::BorrowedPtr(_)) {
                    return Err(error(l, "ptr_offset requires a pointer"));
                }
                self.expect(&mut args[1], &Ty::Scalar(Scalar::USize), env)?;
                self.effect.fail = true;
                Ok(t)
            }
            "nonnull" => {
                arity(1)?;
                let t = self.expr(&mut args[0], None, env)?;
                let Ty::Ptr(t) = t else {
                    return Err(error(l, "nonnull requires Ptr<T>"));
                };
                self.effect.fail = true;
                Ok(Ty::NonNull(t))
            }
            "null" => {
                arity(0)?;
                Ok(Ty::Ptr(Box::new(type_arg()?)))
            }
            "into_raw" => {
                arity(1)?;
                let t = self.expr(&mut args[0], None, env)?;
                self.move_value(&args[0], env)?;
                let Ty::Boxed(t) = t else {
                    return Err(error(l, "into_raw consumes Box<T>"));
                };
                Ok(Ty::NonNull(t))
            }
            "from_raw" => {
                arity(1)?;
                if env.unsafe_ == 0 {
                    return Err(error(
                        l,
                        "from_raw requires the unique allocation ownership contract",
                    ));
                }
                let t = self.expr(&mut args[0], None, env)?;
                let t = match t {
                    Ty::NonNull(t) | Ty::Ptr(t) => t,
                    _ => return Err(error(l, "from_raw requires an allocation pointer")),
                };
                self.effect.fail = true;
                Ok(Ty::Boxed(t))
            }
            "handle" => {
                arity(1)?;
                let t = self.expr(&mut args[0], None, env)?;
                if !matches!(t, Ty::Ptr(_) | Ty::NonNull(_)) {
                    return Err(error(l, "handle requires a pointer"));
                }
                Ok(Ty::Handle)
            }
            "handle_ptr" => {
                arity(1)?;
                if env.unsafe_ == 0 {
                    return Err(error(
                        l,
                        "handle_ptr requires an external object type/lifetime contract",
                    ));
                }
                self.expect(&mut args[0], &Ty::Handle, env)?;
                Ok(Ty::Ptr(Box::new(type_arg()?)))
            }
            "address" => {
                arity(1)?;
                let t = self.expr(&mut args[0], None, env)?;
                if !matches!(t, Ty::Ptr(_) | Ty::NonNull(_) | Ty::Handle) {
                    return Err(error(l, "address requires a pointer"));
                }
                Ok(Ty::Scalar(Scalar::USize))
            }
            "fn_ptr" => {
                arity(1)?;
                let Op::Name(name) = &args[0].op else {
                    return Err(error(l, "fn_ptr requires a function declaration"));
                };
                let f = self
                    .functions
                    .iter()
                    .find(|f| f.name == *name)
                    .ok_or_else(|| error(l, "unknown callback function"))?;
                if !f.params.iter().all(|(_, t)| abi(t, self.records)) || !abi(&f.ret, self.records)
                {
                    return Err(error(l, "callback requires a fixed C signature"));
                }
                let t = Ty::FnPtr(
                    f.params.iter().map(|(_, t)| t.clone()).collect(),
                    Box::new(f.ret.clone()),
                );
                args[0].ty = Some(t.clone());
                Ok(t)
            }
            "atomic" => {
                arity(1)?;
                let t = types
                    .first()
                    .cloned()
                    .or_else(|| {
                        want.and_then(|t| {
                            if let Ty::Atomic(t) = t {
                                Some((**t).clone())
                            } else {
                                None
                            }
                        })
                    })
                    .ok_or_else(|| error(l, "atomic requires a value type"))?;
                validate_ty(&Ty::Atomic(Box::new(t.clone())), self.records, l)?;
                self.expect(&mut args[0], &t, env)?;
                Ok(Ty::Atomic(Box::new(t)))
            }
            "atomic_load"
            | "atomic_store"
            | "atomic_exchange"
            | "atomic_compare_exchange"
            | "atomic_is_lock_free" => {
                arity(match n {
                    "atomic_load" => 2,
                    "atomic_store" | "atomic_exchange" => 3,
                    "atomic_compare_exchange" => 5,
                    _ => 1,
                })?;
                let t = self.expr(&mut args[0], None, env)?;
                let Ty::Ref(t, m) = t else {
                    return Err(error(l, "atomic operation requires an explicit borrow"));
                };
                let Ty::Atomic(t) = *t else {
                    return Err(error(l, "atomic operation requires Atomic<T>"));
                };
                if n == "atomic_is_lock_free" {
                    return Ok(Ty::Bool);
                }
                if !m && n != "atomic_load" { /* Atomics mutate through a shared reference by definition. */
                }
                let count = if n == "atomic_compare_exchange" {
                    2
                } else if n == "atomic_load" {
                    0
                } else {
                    1
                };
                for a in args.iter_mut().skip(1).take(count) {
                    self.expect(a, &t, env)?;
                }
                let start = 1 + count;
                for a in &mut args[start..] {
                    let Op::Name(order) = &a.op else {
                        return Err(error(
                            l,
                            "memory order must be an explicit ordering constant",
                        ));
                    };
                    if !["Relaxed", "Acquire", "Release", "AcqRel", "SeqCst"]
                        .contains(&order.as_str())
                    {
                        return Err(error(l, "unknown memory ordering"));
                    }
                    if n == "atomic_load" && ["Release", "AcqRel"].contains(&order.as_str()) {
                        return Err(error(l, "load cannot use Release or AcqRel"));
                    }
                    if n == "atomic_store" && ["Acquire", "AcqRel"].contains(&order.as_str()) {
                        return Err(error(l, "store cannot use Acquire or AcqRel"));
                    }
                    a.ty = Some(Ty::Scalar(Scalar::U32));
                }
                if n == "atomic_compare_exchange" {
                    let order = |i: usize| {
                        if let Op::Name(n) = &args[i].op {
                            n.as_str()
                        } else {
                            unreachable!()
                        }
                    };
                    let (s, f) = (order(3), order(4));
                    let valid = match s {
                        "Relaxed" | "Release" => f == "Relaxed",
                        "Acquire" | "AcqRel" => ["Relaxed", "Acquire"].contains(&f),
                        "SeqCst" => ["Relaxed", "Acquire", "SeqCst"].contains(&f),
                        _ => false,
                    };
                    if !valid {
                        return Err(error(
                            l,
                            "compare_exchange failure ordering is invalid or stronger than success",
                        ));
                    }
                }
                Ok(if n == "atomic_store" {
                    Ty::Unit
                } else if n == "atomic_compare_exchange" {
                    Ty::Bool
                } else {
                    *t
                })
            }
            "simd_splat" => {
                arity(1)?;
                let t = types
                    .first()
                    .cloned()
                    .or_else(|| want.cloned())
                    .ok_or_else(|| error(l, "simd_splat requires Simd type"))?;
                validate_ty(&t, self.records, l)?;
                let Ty::Simd(s, _) = t else {
                    return Err(error(l, "simd_splat requires Simd type"));
                };
                self.expect(&mut args[0], &Ty::Scalar(s), env)?;
                Ok(t)
            }
            "simd_add" | "simd_mul" | "simd_eq" => {
                arity(2)?;
                let t = self.expr(&mut args[0], want, env)?;
                if !matches!(t, Ty::Simd(..)) {
                    return Err(error(l, "vector operation requires Simd"));
                }
                self.expect(&mut args[1], &t, env)?;
                Ok(t)
            }
            "simd_load" | "simd_load_aligned" => {
                arity(2)?;
                let t = types
                    .first()
                    .cloned()
                    .or_else(|| want.cloned())
                    .ok_or_else(|| error(l, "simd_load requires a Simd type"))?;
                let Ty::Simd(s, _) = t else {
                    return Err(error(l, "simd_load requires a Simd type"));
                };
                let src = self.expr(&mut args[0], None, env)?;
                if !matches!(src,Ty::Ref(ref q,_) if matches!(&**q,Ty::Buffer(v)|Ty::Array(v,_)|Ty::Slice(v,_) if **v==Ty::Scalar(s)))
                {
                    return Err(error(
                        l,
                        "simd_load requires a borrow of matching dense scalars",
                    ));
                }
                self.expect(&mut args[1], &Ty::Scalar(Scalar::USize), env)?;
                self.effect.fail = true;
                Ok(t)
            }
            "simd_store" | "simd_store_aligned" => {
                arity(3)?;
                let t = self.expr(&mut args[2], None, env)?;
                let Ty::Simd(s, _) = t else {
                    return Err(error(l, "simd_store requires Simd value"));
                };
                let dst = self.expr(&mut args[0], None, env)?;
                if !matches!(dst,Ty::Ref(ref q,true) if matches!(&**q,Ty::Buffer(v)|Ty::Array(v,_)|Ty::Slice(v,true) if **v==Ty::Scalar(s)))
                {
                    return Err(error(
                        l,
                        "simd_store requires an exclusive mutable scalar destination",
                    ));
                }
                self.expect(&mut args[1], &Ty::Scalar(Scalar::USize), env)?;
                self.effect.fail = true;
                Ok(Ty::Unit)
            }
            "read_le" | "read_be" | "write_le" | "write_be" => {
                let write = n.starts_with("write");
                arity(if write { 3 } else { 2 })?;
                let t = types
                    .first()
                    .cloned()
                    .or_else(|| want.cloned())
                    .ok_or_else(|| error(l, "binary access requires a machine scalar type"))?;
                if t.scalar().is_none() {
                    return Err(error(l, "binary encoding requires a machine scalar"));
                }
                let b = self.expr(&mut args[0], None, env)?;
                if !matches!(b,Ty::Ref(ref q,m) if (!write||m)&&matches!(&**q,Ty::Buffer(v)|Ty::Slice(v,_)|Ty::Array(v,_) if **v==Ty::Scalar(Scalar::U8)))
                {
                    return Err(error(
                        l,
                        "binary access requires an appropriate byte storage borrow",
                    ));
                }
                self.expect(&mut args[1], &Ty::Scalar(Scalar::USize), env)?;
                if write {
                    self.expect(&mut args[2], &t, env)?;
                }
                self.effect.fail = true;
                Ok(if write { Ty::Unit } else { t })
            }
            "bits" | "from_bits" => {
                arity(1)?;
                let t = types.first().cloned().or_else(|| want.cloned());
                let src = self.expr(&mut args[0], None, env)?;
                let out = if n == "bits" {
                    match src {
                        Ty::Scalar(Scalar::F32) => Ty::Scalar(Scalar::U32),
                        Ty::Scalar(Scalar::F64) => Ty::Scalar(Scalar::U64),
                        _ => return Err(error(l, "bits requires F32/F64")),
                    }
                } else {
                    let t = t.ok_or_else(|| error(l, "from_bits requires F32/F64 target"))?;
                    if !matches!(
                        (&src, &t),
                        (Ty::Scalar(Scalar::U32), Ty::Scalar(Scalar::F32))
                            | (Ty::Scalar(Scalar::U64), Ty::Scalar(Scalar::F64))
                    ) {
                        return Err(error(l, "from_bits size mismatch"));
                    }
                    t
                };
                Ok(out)
            }
            "nan" | "infinity" => {
                arity(0)?;
                let t = types
                    .first()
                    .cloned()
                    .or_else(|| want.cloned())
                    .unwrap_or(Ty::Scalar(Scalar::F64));
                if !t.scalar().is_some_and(Scalar::float) {
                    return Err(error(l, "NaN/Inf require F32/F64"));
                }
                Ok(t)
            }
            "sqrt" | "sin" | "cos" | "exp" | "log" | "abs" | "floor" | "ceil" | "trunc"
            | "is_nan" | "is_finite" | "sign_bit" => {
                arity(1)?;
                let t = self.expr(&mut args[0], want.filter(|t| t.scalar().is_some()), env)?;
                if !t.scalar().is_some_and(Scalar::float) {
                    return Err(error(l, "floating operation requires F32/F64"));
                }
                Ok(if ["is_nan", "is_finite", "sign_bit"].contains(&n) {
                    Ty::Bool
                } else {
                    t
                })
            }
            "assert" => {
                arity(1)?;
                self.expect(&mut args[0], &Ty::Bool, env)?;
                self.effect.fail = true;
                Ok(Ty::Unit)
            }
            "print" => {
                arity(1)?;
                let t = self.expr(&mut args[0], None, env)?;
                if !matches!(t, Ty::Scalar(_) | Ty::Bool | Ty::Str) {
                    return Err(error(l, "print requires a scalar or Utf8"));
                }
                self.effect.io = true;
                self.effect.blocking = true;
                self.effect.alloc = true;
                self.effect.fail = true;
                Ok(Ty::Unit)
            }
            "utf8_bytes" => {
                arity(1)?;
                self.expect(&mut args[0], &Ty::Str, env)?;
                Ok(Ty::Slice(Box::new(Ty::Scalar(Scalar::U8)), false))
            }
            n if n.starts_with("wrapping_") || n.starts_with("saturating_") => {
                arity(2)?;
                let op = n.split('_').nth(1).unwrap_or("");
                if !["add", "sub", "mul"].contains(&op) {
                    return Err(error(l, "unknown integer operation"));
                }
                let t = self.expr(&mut args[0], want, env)?;
                if !t.scalar().is_some_and(|s| !s.float()) {
                    return Err(error(l, "integer operation requires a machine integer"));
                }
                self.expect(&mut args[1], &t, env)?;
                Ok(t)
            }
            _ => Err(error(l, format!("unknown native operation {n}"))),
        }
    }
    fn block(
        &mut self,
        stmts: &mut [Stmt],
        env: &mut Env,
        scoped: bool,
    ) -> Result<bool, Diagnostic> {
        let original: BTreeSet<_> = env.vars.keys().cloned().collect();
        let mut returns = false;
        for s in stmts {
            if returns {
                return Err(error(
                    1,
                    "unreachable statement after return or control transfer",
                ));
            }
            env.pending.clear();
            match s {
                Stmt::Let {
                    name,
                    ty,
                    mutable,
                    value,
                    line,
                } => {
                    validate_ty(ty, self.records, *line)?;
                    if env.vars.contains_key(name) {
                        return Err(error(*line, format!("local {name} already declared")));
                    }
                    self.expect(value, ty, env)?;
                    self.move_value(value, env)?;
                    let mut loans = if ty.borrowed() {
                        env.pending.clone()
                    } else {
                        vec![]
                    };
                    for q in &mut loans {
                        q.holder = name.clone();
                    }
                    if ty.borrowed() && loans.is_empty() {
                        let source = match &value.op {
                            Op::Name(n) => Some(n),
                            Op::Move(a) => {
                                if let Op::Name(n) = &a.op {
                                    Some(n)
                                } else {
                                    None
                                }
                            }
                            _ => None,
                        };
                        if let Some(n) = source {
                            if let Some(v) = env.vars.get(n) {
                                loans = v.loans.clone();
                                for q in &mut loans {
                                    q.holder = name.clone();
                                }
                            }
                        }
                    }
                    if owned(ty, self.records) {
                        drop_calls(
                            ty,
                            self.records,
                            &mut self.effect.calls,
                            &mut BTreeSet::new(),
                        );
                        if has_affinity(ty, self.records) {
                            self.effect.affine = true;
                            self.effect.fail = true;
                        }
                        self.effect.blocking = true;
                    }
                    env.vars.insert(
                        name.clone(),
                        Var {
                            ty: ty.clone(),
                            mutable: *mutable,
                            loans,
                        },
                    );
                }
                Stmt::Set(a, b) => {
                    let p = place(a).ok_or_else(|| error(a.line, "assignment requires a place"))?;
                    let mut probe = env.clone();
                    let t = self.place_type(a, &mut probe)?;
                    self.expect(b, &t, env)?;
                    self.move_value(b, env)?;
                    env.pending.clear();
                    let replaced = |m: &String| {
                        m == &p
                            || m.starts_with(&format!("{p}."))
                            || m.starts_with(&format!("{p}["))
                    };
                    env.moved.retain(|m| !replaced(m));
                    self.access(&p, true, env, a.line)?;
                    self.expr(a, None, env)?;
                    if env.bounds.values().any(|b| {
                        b == &p
                            || b.starts_with(&format!("{p}."))
                            || b.starts_with(&format!("{p}["))
                    }) {
                        return Err(error(
                            a.line,
                            "assignment would invalidate a proved traversal bound",
                        ));
                    }
                    if !self.writable(a, env) {
                        return Err(error(
                            a.line,
                            "assignment through an immutable place or borrow",
                        ));
                    }
                    if matches!(t, Ty::Atomic(_)) {
                        return Err(error(
                            a.line,
                            "atomic updates require an explicit memory-order operation",
                        ));
                    }
                    if owned(&t, self.records) {
                        drop_calls(
                            &t,
                            self.records,
                            &mut self.effect.calls,
                            &mut BTreeSet::new(),
                        );
                        self.effect.blocking = true;
                        if has_affinity(&t, self.records) {
                            self.effect.affine = true;
                            self.effect.fail = true;
                        }
                    }
                    if matches!(
                        t,
                        Ty::Ref(..) | Ty::Slice(..) | Ty::BorrowedPtr(_) | Ty::ForeignBuffer(_)
                    ) {
                        return Err(error(
                            a.line,
                            "borrow assignment requires a fresh lexical binding",
                        ));
                    }
                    env.moved.retain(|m| !replaced(m));
                }
                Stmt::Expr(e) => {
                    let t = self.expr(e, None, env)?;
                    if owned(&t, self.records) {
                        return Err(error(e.line, "an owning result must be bound or returned"));
                    }
                }
                Stmt::Return(v, l) => {
                    let ret = self.ret.clone();
                    let returning = env.returning;
                    env.returning = true;
                    if let Some(e) = v {
                        self.expect(e, &ret, env)?;
                        self.move_value(e, env)?;
                        if contains_borrow(&ret, self.records) {
                            let source = self.borrow_result.as_ref().ok_or_else(|| {
                                error(*l, "returning a borrow requires a declared lifetime")
                            })?;
                            let mut loans = env.pending.clone();
                            if let Some(path) = place(e) {
                                if let Some(v) = env.vars.get(root(&path)) {
                                    loans.extend(v.loans.clone());
                                    if v.loans.is_empty() && root(&path) == source {
                                        loans.push(Loan {
                                            path: source.clone(),
                                            mutable: false,
                                            range: None,
                                            holder: String::new(),
                                            parents: BTreeSet::new(),
                                        });
                                    }
                                }
                            }
                            if loans.is_empty() || loans.iter().any(|l| root(&l.path) != source) {
                                return Err(error(
                                    *l,
                                    "borrowed result escapes its declared input lifetime",
                                ));
                            }
                        }
                    } else if ret != Ty::Unit {
                        return Err(error(*l, "missing return value"));
                    }
                    env.returning = returning;
                    returns = true;
                }
                Stmt::If(c, a, b) => {
                    self.expect(c, &Ty::Bool, env)?;
                    env.pending.clear();
                    let (mut ea, mut eb) = (env.clone(), env.clone());
                    let (ra, rb) = (self.block(a, &mut ea, true)?, self.block(b, &mut eb, true)?);
                    env.moved = if ra {
                        eb.moved
                    } else if rb {
                        ea.moved
                    } else {
                        ea.moved.union(&eb.moved).cloned().collect()
                    };
                    returns = ra && rb;
                }
                Stmt::While(c, b) => {
                    self.expect(c, &Ty::Bool, env)?;
                    self.effect.unbounded = true;
                    env.pending.clear();
                    let mut inner = env.clone();
                    inner.loops += 1;
                    self.block(b, &mut inner, true)?;
                    if inner.moved != env.moved {
                        return Err(error(
                            c.line,
                            "loop cannot consume an outer owner on a repeated path",
                        ));
                    }
                    if matches!(c.op, Op::Bool(true)) && !loop_break(b) {
                        returns = true;
                    }
                }
                Stmt::For(n, a, b, body) => {
                    self.expect(a, &Ty::Scalar(Scalar::USize), env)?;
                    self.expect(b, &Ty::Scalar(Scalar::USize), env)?;
                    if env.vars.contains_key(n) {
                        return Err(error(a.line, "loop variable shadows a local"));
                    }
                    env.pending.clear();
                    let mut inner = env.clone();
                    inner.loops += 1;
                    inner.vars.insert(
                        n.clone(),
                        Var {
                            ty: Ty::Scalar(Scalar::USize),
                            mutable: false,
                            loans: vec![],
                        },
                    );
                    if inner.unsafe_ == 0 && !unsafe_block(body) {
                        if let Op::Call(name, _, args) = &b.op {
                            if name == "len" {
                                let target = match &args[0].op {
                                    Op::Borrow(a, _) => place(a),
                                    _ => place(&args[0]),
                                };
                                if let Some(target) = target {
                                    inner.bounds.insert(n.clone(), target);
                                }
                            }
                        }
                    } else {
                        inner.bounds.clear();
                    }
                    self.block(body, &mut inner, true)?;
                    if inner.moved != env.moved {
                        return Err(error(
                            a.line,
                            "loop cannot consume an outer owner on a repeated path",
                        ));
                    }
                }
                Stmt::Break(l) | Stmt::Continue(l) => {
                    if env.loops == 0 {
                        return Err(error(*l, "loop transfer outside a loop"));
                    }
                    returns = true;
                }
                Stmt::Unsafe(b) => {
                    env.unsafe_ += 1;
                    returns = self.block(b, env, true)?;
                    env.unsafe_ -= 1;
                }
                Stmt::Block(b) => returns = self.block(b, env, true)?,
            }
        }
        env.pending.clear();
        if scoped {
            let removed: Vec<_> = env
                .vars
                .keys()
                .filter(|n| !original.contains(*n))
                .cloned()
                .collect();
            for n in removed {
                env.vars.remove(&n);
                env.moved.retain(|p| root(p) != n);
            }
        }
        Ok(returns)
    }
}
pub fn builtin_name(n: &str) -> bool {
    Scalar::parse(n).is_some()
        || [
            "size_of",
            "align_of",
            "abi_version",
            "resource",
            "resource_get",
            "resource_get_mut",
            "resource_close",
            "resource_into_inner",
            "window_open",
            "window_child",
            "window_attach",
            "window_resize",
            "window_focus",
            "window_dpi",
            "window_poll",
            "window_gpu",
            "window_scene",
            "window_input",
            "window_handle",
            "window_read",
            "thread_bind",
            "thread_get",
            "thread_get_mut",
            "array",
            "push",
            "cast",
            "buffer",
            "buffer_aligned",
            "box",
            "zero",
            "union_zero",
            "len",
            "resize",
            "slice",
            "mut_slice",
            "split_at_mut",
            "borrowed_ptr",
            "as_raw",
            "data",
            "foreign",
            "ptr_offset",
            "nonnull",
            "null",
            "into_raw",
            "from_raw",
            "handle",
            "handle_ptr",
            "address",
            "fn_ptr",
            "call_trusted",
            "atomic",
            "atomic_load",
            "atomic_store",
            "atomic_exchange",
            "atomic_compare_exchange",
            "atomic_is_lock_free",
            "simd_splat",
            "simd_add",
            "simd_mul",
            "simd_eq",
            "simd_load",
            "simd_load_aligned",
            "simd_store",
            "simd_store_aligned",
            "read_le",
            "read_be",
            "write_le",
            "write_be",
            "bits",
            "from_bits",
            "nan",
            "infinity",
            "sqrt",
            "sin",
            "cos",
            "exp",
            "log",
            "abs",
            "floor",
            "ceil",
            "trunc",
            "is_nan",
            "is_finite",
            "sign_bit",
            "assert",
            "print",
            "utf8_bytes",
        ]
        .contains(&n)
        || n.starts_with("wrapping_")
        || n.starts_with("saturating_")
}

pub fn requires_unsafe(unit: &Unit) -> bool {
    unit.functions.iter().any(|f| {
        f.unsafe_ || f.library.is_some() || f.body.as_ref().is_some_and(|b| unsafe_block(b))
    })
}
