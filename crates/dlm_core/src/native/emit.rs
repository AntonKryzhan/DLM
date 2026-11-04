use super::*;
use std::fmt::Write;
pub const RUNTIME: &str = include_str!("runtime.c");
pub fn c_type(t: &Ty) -> String {
    match t {
        Ty::Scalar(s) => s.c().into(),
        Ty::Bool => "bool".into(),
        Ty::Unit => "uint8_t".into(),
        Ty::Str => "dn_utf8".into(),
        Ty::Window => "dn_window *".into(),
        Ty::Resource(..) => format!("dn_t_{} *", key(t)),
        Ty::Handle => "void *".into(),
        Ty::Record(n) => format!("dn_r_{n}"),
        Ty::Ptr(t) | Ty::NonNull(t) | Ty::BorrowedPtr(t) | Ty::Boxed(t) => {
            format!("{} *", c_type(t))
        }
        Ty::Ref(t, m) => format!(
            "{}{} *",
            c_type(t),
            if *m || matches!(&**t, Ty::Atomic(_)) {
                ""
            } else {
                " const"
            }
        ),
        Ty::Atomic(t) => format!(
            "_Atomic({})",
            if **t == Ty::Scalar(Scalar::F32) {
                "uint32_t".into()
            } else {
                c_type(t)
            }
        ),
        _ => format!("dn_t_{}", key(t)),
    }
}
fn key(t: &Ty) -> String {
    match t {
        Ty::Scalar(s) => s.name().into(),
        Ty::Record(n) => format!("r{}_{n}", n.len()),
        Ty::Bool => "b".into(),
        Ty::Unit => "v".into(),
        Ty::Handle => "h".into(),
        Ty::Str => "str".into(),
        Ty::Split(t) => format!("split_{}", key(t)),
        Ty::Window => "window".into(),
        Ty::ThreadBound(t) => format!("thread_{}", key(t)),
        Ty::Resource(t, f) => format!("resource_{}_{}", key(t), f),
        Ty::Buffer(t) => format!("buf_{}", key(t)),
        Ty::Boxed(t) => format!("box_{}", key(t)),
        Ty::Array(t, n) => format!("arr{n}_{}", key(t)),
        Ty::Slice(t, m) => format!("slice{}_{}", usize::from(*m), key(t)),
        Ty::Ref(t, m) => format!("ref{}_{}", usize::from(*m), key(t)),
        Ty::Ptr(t) => format!("ptr_{}", key(t)),
        Ty::NonNull(t) => format!("nn_{}", key(t)),
        Ty::BorrowedPtr(t) => format!("bp_{}", key(t)),
        Ty::ForeignBuffer(t) => format!("foreign_{}", key(t)),
        Ty::Atomic(t) => format!("atom_{}", key(t)),
        Ty::Simd(s, n) => format!("simd{n}_{}", s.name()),
        Ty::FnPtr(p, r) => format!(
            "fn_{}_ret_{}",
            p.iter().map(key).collect::<Vec<_>>().join("__"),
            key(r)
        ),
    }
}
fn cname(n: &str) -> String {
    format!("dn_f_{n}")
}
fn local(n: &str) -> String {
    format!("v_{n}")
}
pub(super) fn q(s: &str) -> String {
    let mut r = String::from("\"");
    for b in s.bytes() {
        if b.is_ascii_alphanumeric() || b" .,:;!#%&'()*+-/<=>@[]^_`{|}~".contains(&b) {
            r.push(b as char);
        } else {
            write!(r, "\\{b:03o}").unwrap();
        }
    }
    r.push('"');
    r
}
fn scalar_arithmetic() -> String {
    let mut o = String::new();
    for s in [
        Scalar::U8,
        Scalar::U16,
        Scalar::U32,
        Scalar::U64,
        Scalar::USize,
        Scalar::I8,
        Scalar::I16,
        Scalar::I32,
        Scalar::I64,
        Scalar::ISize,
    ] {
        let c = s.c();
        let n = s.name();
        let bits = s.bits();
        let signed = s.signed();
        let wide = if signed {
            "__int128"
        } else {
            "unsigned __int128"
        };
        let min = if signed {
            format!("(-((__int128)1<<{}) )", bits - 1)
        } else {
            "0".into()
        };
        let max = if signed {
            format!("(((__int128)1<<{})-1)", bits - 1)
        } else {
            format!("(((unsigned __int128)1<<{bits})-1)")
        };
        writeln!(o,"static {c} dn_{n}_bits(uint64_t x){{ {c} r; {} b=({})x;memcpy(&r,&b,sizeof r);return r;}}",match bits{8=>"uint8_t",16=>"uint16_t",32=>"uint32_t",_=>"uint64_t"},match bits{8=>"uint8_t",16=>"uint16_t",32=>"uint32_t",_=>"uint64_t"}).unwrap();
        for (op, ch) in [("add", "+"), ("sub", "-"), ("mul", "*")] {
            let overflow = if !signed && op == "sub" {
                "a<b".to_string()
            } else {
                format!("v<{min} || v>{max}")
            };
            writeln!(o,"static {c} dn_{n}_{op}({c} a,{c} b){{{wide} v=({wide})a{ch}({wide})b;if({overflow})dn_fail(\"integer overflow\");return ({c})v;}}").unwrap();
            writeln!(o,"static {c} dn_{n}_wrap_{op}({c} a,{c} b){{uint64_t x=(uint64_t)a{ch}(uint64_t)b;return dn_{n}_bits(x);}}").unwrap();
            let sat = if !signed && op == "sub" {
                "if(a<b)return 0;".to_string()
            } else {
                String::new()
            };
            writeln!(o,"static {c} dn_{n}_sat_{op}({c} a,{c} b){{{sat}{wide} v=({wide})a{ch}({wide})b;if(v<{min})return ({c}){min};if(v>{max})return ({c}){max};return ({c})v;}}").unwrap();
        }
        for (op, ch) in [("div", "/"), ("mod", "%")] {
            let extra = if signed {
                format!("if(a==({c}){min} && b==-1)dn_fail(\"integer overflow\");")
            } else {
                String::new()
            };
            writeln!(o,"static {c} dn_{n}_{op}({c} a,{c} b){{if(!b)dn_fail(\"division by zero\");{extra}return a{ch}b;}}").unwrap();
        }
        writeln!(o,"static {c} dn_{n}_shl({c} a,{c} b){{if({}b>={bits})dn_fail(\"shift count out of range\");return dn_{n}_bits((uint64_t)a<<b);}}",if signed{"b<0 || "}else{""}).unwrap();
        let shift = if signed {
            format!("uint64_t u=(uint64_t)a; if(a<0 && b>0)u=(u>>(unsigned)b)|(UINT64_MAX<<({bits}-(unsigned)b));else u>>=(unsigned)b;return dn_{n}_bits(u);")
        } else {
            format!("return ({c})(a>>b);")
        };
        writeln!(o,"static {c} dn_{n}_shr({c} a,{c} b){{if({}b>={bits})dn_fail(\"shift count out of range\");{shift}}}",if signed{"b<0 || "}else{""}).unwrap();
        if signed {
            writeln!(o,"static {c} dn_{n}_neg({c} a){{if(a==({c}){min})dn_fail(\"integer overflow\");return -a;}}").unwrap();
        }
    }
    o
}
fn collect_ty(t: &Ty, all: &mut BTreeSet<Ty>) {
    if !all.insert(t.clone()) {
        return;
    }
    if let Ty::Split(t) = t {
        collect_ty(&Ty::Slice(t.clone(), true), all);
    }
    match t {
        Ty::Resource(t, _)
        | Ty::ThreadBound(t)
        | Ty::Split(t)
        | Ty::Array(t, _)
        | Ty::Buffer(t)
        | Ty::Boxed(t)
        | Ty::Slice(t, _)
        | Ty::Ref(t, _)
        | Ty::Ptr(t)
        | Ty::NonNull(t)
        | Ty::BorrowedPtr(t)
        | Ty::ForeignBuffer(t)
        | Ty::Atomic(t) => collect_ty(t, all),
        Ty::FnPtr(p, r) => {
            for t in p {
                collect_ty(t, all);
            }
            collect_ty(r, all);
        }
        _ => {}
    }
}
fn expr_types(e: &Expr, all: &mut BTreeSet<Ty>) {
    collect_ty(e.ty(), all);
    match &e.op {
        Op::Unary(_, a) | Op::Move(a) | Op::Borrow(a, _) | Op::Field(a, _) => expr_types(a, all),
        Op::Index(a, b) | Op::IndexProven(a, b) | Op::Binary(_, a, b) => {
            expr_types(a, all);
            expr_types(b, all);
        }
        Op::Call(_, types, args) => {
            for t in types {
                collect_ty(t, all);
            }
            for a in args {
                expr_types(a, all);
            }
        }
        Op::Invoke(f, args) => {
            expr_types(f, all);
            for a in args {
                expr_types(a, all);
            }
        }
        _ => {}
    }
}
fn stmt_types(b: &[Stmt], all: &mut BTreeSet<Ty>) {
    for s in b {
        match s {
            Stmt::Let { ty, value, .. } => {
                collect_ty(ty, all);
                expr_types(value, all);
            }
            Stmt::Set(a, b) => {
                expr_types(a, all);
                expr_types(b, all);
            }
            Stmt::Expr(e) | Stmt::Return(Some(e), _) => expr_types(e, all),
            Stmt::If(c, a, b) => {
                expr_types(c, all);
                stmt_types(a, all);
                stmt_types(b, all);
            }
            Stmt::While(c, b) => {
                expr_types(c, all);
                stmt_types(b, all);
            }
            Stmt::For(_, a, b, c) => {
                expr_types(a, all);
                expr_types(b, all);
                stmt_types(c, all);
            }
            Stmt::Unsafe(b) | Stmt::Block(b) => stmt_types(b, all),
            _ => {}
        }
    }
}
pub(super) fn all_types(m: &Mir) -> BTreeSet<Ty> {
    let mut all = BTreeSet::new();
    for r in &m.unit.records {
        collect_ty(&Ty::Record(r.name.clone()), &mut all);
        for (_, t) in &r.fields {
            collect_ty(t, &mut all);
        }
    }
    for f in &m.unit.functions {
        collect_ty(&f.ret, &mut all);
        for (_, t) in &f.params {
            collect_ty(t, &mut all);
        }
        if let Some(b) = &f.body {
            stmt_types(b, &mut all);
        }
    }
    all
}
fn typedef(
    t: &Ty,
    all: &BTreeSet<Ty>,
    records: &[Record],
    done: &mut BTreeSet<Ty>,
    out: &mut String,
) {
    if done.contains(t) {
        return;
    }
    // Pointers to nominal records use forward declarations; dense types need a
    // complete element layout. C rejects a forged recursive by-value layout too.
    match t {
        Ty::Split(q) => typedef(&Ty::Slice(q.clone(), true), all, records, done, out),
        Ty::Ptr(q) | Ty::NonNull(q) | Ty::BorrowedPtr(q) | Ty::Ref(q, _) | Ty::Boxed(q) => {
            if !matches!(&**q, Ty::Record(_) | Ty::Resource(..)) {
                typedef(q, all, records, done, out);
            }
        }
        Ty::Buffer(q) | Ty::Slice(q, _) | Ty::ForeignBuffer(q) => {
            if !matches!(&**q, Ty::Record(_) | Ty::Resource(..)) {
                typedef(q, all, records, done, out);
            }
        }
        Ty::Resource(q, _) | Ty::ThreadBound(q) | Ty::Array(q, _) | Ty::Atomic(q) => {
            typedef(q, all, records, done, out)
        }
        Ty::FnPtr(p, r) => {
            for q in p {
                typedef(q, all, records, done, out);
            }
            typedef(r, all, records, done, out);
        }
        _ => {}
    }
    if let Ty::Record(n) = t {
        if let Some(r) = records.iter().find(|r| r.name == *n) {
            for (_, q) in &r.fields {
                if !matches!(
                    q,
                    Ty::Ptr(_)
                        | Ty::NonNull(_)
                        | Ty::BorrowedPtr(_)
                        | Ty::Boxed(_)
                        | Ty::Buffer(_)
                        | Ty::Slice(..)
                        | Ty::Ref(..)
                ) {
                    typedef(q, all, records, done, out);
                } else {
                    typedef(q, all, records, done, out);
                }
            }
            let tag = if r.union { "union" } else { "struct" };
            let attrs = if r.packed || r.align.is_some() {
                format!(
                    " __attribute__(({}{}{}))",
                    if r.packed { "packed" } else { "" },
                    if r.packed && r.align.is_some() {
                        ","
                    } else {
                        ""
                    },
                    r.align.map(|a| format!("aligned({a})")).unwrap_or_default()
                )
            } else {
                String::new()
            };
            writeln!(out, "{tag}{attrs} dn_r_{n} {{").unwrap();
            for (f, q) in &r.fields {
                writeln!(out, "    {} f_{f};", c_type(q)).unwrap();
            }
            out.push_str("};\n");
        }
    } else {
        let c = c_type(t);
        match t {
            Ty::Resource(q, _) => {
                let base = format!("dn_t_{}", key(t));
                writeln!(out, "struct {base} {{ {} value;bool active; }};", c_type(q)).unwrap();
            }
            Ty::ThreadBound(q) => {
                writeln!(
                    out,
                    "typedef struct {c} {{ {} value;uint64_t owner; }} {c};",
                    c_type(q)
                )
                .unwrap();
            }
            Ty::Split(q) => {
                let sc = c_type(&Ty::Slice(q.clone(), true));
                writeln!(out, "typedef struct {c} {{ {sc} f_left,f_right; }} {c};").unwrap();
            }
            Ty::Array(q, n) => {
                writeln!(
                    out,
                    "typedef struct {c} {{ {} data[{n}]; }} {c};",
                    c_type(q)
                )
                .unwrap();
            }
            Ty::Buffer(q) => {
                writeln!(
                    out,
                    "typedef struct {c} {{ {} *data;size_t len,cap; }} {c};",
                    c_type(q)
                )
                .unwrap();
            }
            Ty::Slice(q, m) => {
                writeln!(
                    out,
                    "typedef struct {c} {{ {}{} *data;size_t len; }} {c};",
                    c_type(q),
                    if *m { "" } else { " const" }
                )
                .unwrap();
            }
            Ty::ForeignBuffer(q) => {
                writeln!(
                    out,
                    "typedef struct {c} {{ {} *data;size_t len; }} {c};",
                    c_type(q)
                )
                .unwrap();
            }
            Ty::FnPtr(p, r) => {
                writeln!(
                    out,
                    "typedef {} (*{c})({});",
                    if **r == Ty::Unit {
                        "void".into()
                    } else {
                        c_type(r)
                    },
                    if p.is_empty() {
                        "void".into()
                    } else {
                        p.iter().map(c_type).collect::<Vec<_>>().join(",")
                    }
                )
                .unwrap();
            }
            Ty::Simd(s, n) => {
                writeln!(out, "typedef struct {c} {{ {} data[{n}]; }} {c};", s.c()).unwrap();
            }
            _ => {}
        }
    }
    done.insert(t.clone());
    let _ = all;
}
fn drop_name(t: &Ty) -> String {
    format!("dn_drop_{}", key(t))
}
fn retain_name(t: &Ty) -> String {
    format!("dn_retain_{}", key(t))
}
fn adopt_name(t: &Ty) -> String {
    format!("dn_adopt_{}", key(t))
}
fn helpers(t: &Ty, records: &[Record]) -> String {
    let c = c_type(t);
    let k = key(t);
    let mut o = String::new();
    if check::owned(t, records) {
        writeln!(
            o,
            "static void {}({c} volatile *x); static void {}({c} volatile *x);",
            drop_name(t),
            retain_name(t)
        )
        .unwrap();
    }
    match t {
        Ty::Resource(v, finalizer) => {
            let base = format!("dn_t_{k}");
            let vc = c_type(v);
            let drop = if check::owned(v, records) {
                format!("{}(&p->value);", drop_name(v))
            } else {
                String::new()
            };
            let retain = if check::owned(v, records) {
                format!("{}(&p->value);", retain_name(v))
            } else {
                String::new()
            };
            let adopt = if check::owned(v, records) {
                format!("{}(&v);", adopt_name(v))
            } else {
                String::new()
            };
            writeln!(o,"static void dn_final_{k}(void *raw){{{base} *p=raw;if(p->active){{p->active=false;(void){}(&p->value);{drop}}}}}
static {c} dn_begin_{k}(void){{{base} *p=dn_allocate_aligned(sizeof *p,_Alignof({base}));((dn_mem*)p-1)->finalize=dn_final_{k};return p;}}
static {c} dn_commit_{k}({c} p,{vc} v){{p->value=v;p->active=true;{retain}return p;}}
static void {}({c} volatile *x){{if(*x){{{base} *p=*x;*x=NULL;((dn_mem*)p-1)->finalize=NULL;dn_final_{k}(p);dn_deallocate(p);}}}}
static void {}({c} volatile *x){{if(*x)dn_detach(*x);}}
static {vc} dn_take_{k}({c} p){{dn_nonnull(p);{vc} v=p->value;p->active=false;dn_deallocate(p);{adopt}return v;}}",cname(finalizer),drop_name(t),retain_name(t)).unwrap();
        }
        Ty::Window => {
            writeln!(o,"static void {}(dn_window *volatile *x){{dn_window_drop(x);}}\nstatic void {}(dn_window *volatile *x){{dn_window_retain(x);}}",drop_name(t),retain_name(t)).unwrap();
        }
        Ty::ThreadBound(v) => {
            let drop = if check::owned(v, records) {
                format!("{}(&x->value);", drop_name(v))
            } else {
                String::new()
            };
            let retain = if check::owned(v, records) {
                format!("{}(&x->value);", retain_name(v))
            } else {
                String::new()
            };
            writeln!(o,"static void {}({c} volatile *x){{if(x->owner){{dn_check_thread(x->owner);{drop}x->owner=0;}}}}\nstatic void {}({c} volatile *x){{{retain}}}",drop_name(t),retain_name(t)).unwrap();
        }
        Ty::Buffer(v) => {
            let vc = c_type(v);
            let drop = if check::owned(v, records) {
                format!("for(size_t i=0;i<x->len;i++){}(&x->data[i]);", drop_name(v))
            } else {
                String::new()
            };
            let retain = if check::owned(v, records) {
                format!(
                    "for(size_t i=0;i<x->len;i++){}(&x->data[i]);",
                    retain_name(v)
                )
            } else {
                String::new()
            };
            writeln!(
                o,
                "static void {}({c} volatile *x){{{drop}dn_deallocate(x->data);*x=({c}){{0}};}}",
                drop_name(t)
            )
            .unwrap();
            writeln!(
                o,
                "static void {}({c} volatile *x){{{retain}dn_detach(x->data);}}",
                retain_name(t)
            )
            .unwrap();
            writeln!(o,"static {c} dn_new_{k}(size_t n){{{c} x={{0}};if(n)x.data=dn_allocate_aligned(dn_size(n,sizeof({vc})),_Alignof({vc}));x.len=x.cap=n;return x;}}").unwrap();
            writeln!(o,"static {c} dn_aligned_{k}(size_t n,size_t a){{if(a<_Alignof({vc})||a>1048576||!a||(a&(a-1)))dn_fail(\"invalid buffer alignment\");{c} x={{0}};x.data=dn_allocate_aligned(dn_size(n,sizeof({vc})),a);x.len=x.cap=n;return x;}}").unwrap();
            let retain_value = if check::owned(v, records) {
                format!("{}(&x->data[x->len]);", retain_name(v))
            } else {
                String::new()
            };
            writeln!(o,"static uint8_t dn_push_{k}({c} *x,{vc} v){{if(x->len==SIZE_MAX)dn_fail(\"buffer length overflow\");if(x->len==x->cap){{size_t cap=x->cap>SIZE_MAX/2?x->len+1:x->cap?x->cap*2:4;{vc} *p=dn_allocate_aligned(dn_size(cap,sizeof({vc})),x->data?((dn_mem*)x->data-1)->alignment:_Alignof({vc}));if(x->len)memcpy(p,x->data,x->len*sizeof({vc}));dn_deallocate(x->data);x->data=p;x->cap=cap;dn_detach(p);}}x->data[x->len]=v;{retain_value}x->len++;return 0;}}").unwrap();
            let valid = if check::zeroable(v, records) {
                String::new()
            } else {
                String::from(
                    "if(n>x->len)dn_fail(\"element type requires explicit initialization\");",
                )
            };
            let shrink = if check::owned(v, records) {
                format!("for(size_t i=n;i<x->len;i++){}(&x->data[i]);", drop_name(v))
            } else {
                String::new()
            };
            writeln!(o,"static uint8_t dn_resize_{k}({c} *x,size_t n){{{valid}if(n<x->len){{{shrink}}}if(n>x->cap){{size_t cap=n;{vc} *p=dn_allocate_aligned(dn_size(cap,sizeof({vc})),x->data?((dn_mem*)x->data-1)->alignment:_Alignof({vc}));if(x->len)memcpy(p,x->data,x->len*sizeof({vc}));dn_deallocate(x->data);x->data=p;dn_detach(p);x->cap=cap;}}if(n>x->len)memset(x->data+x->len,0,(n-x->len)*sizeof({vc}));x->len=n;return 0;}}").unwrap();
        }
        Ty::Boxed(v) => {
            let vc = c_type(v);
            let drop = if check::owned(v, records) {
                format!("{}(*x);", drop_name(v))
            } else {
                String::new()
            };
            let retain = if check::owned(v, records) {
                format!("{}(*x);", retain_name(v))
            } else {
                String::new()
            };
            writeln!(o,"static {c} dn_new_{k}({vc} v){{{c} p=dn_allocate_aligned(sizeof({vc}),_Alignof({vc}));*p=v;return p;}}\nstatic void {}({c} volatile *x){{if(*x){{{drop}dn_deallocate(*x);*x=0;}}}}\nstatic void {}({c} volatile *x){{if(*x){{{retain}dn_detach(*x);}}}}",drop_name(t),retain_name(t)).unwrap();
        }
        Ty::Array(v, n) if check::owned(t, records) => {
            writeln!(o,"static void {}({c} volatile *x){{for(size_t i=0;i<{n};i++){}(&x->data[i]);}}\nstatic void {}({c} volatile *x){{for(size_t i=0;i<{n};i++){}(&x->data[i]);}}",drop_name(t),drop_name(v),retain_name(t),retain_name(v)).unwrap();
        }
        Ty::Record(n) if check::owned(t, records) => {
            let r = records.iter().find(|r| r.name == *n).unwrap();
            let drops = r
                .fields
                .iter()
                .rev()
                .filter(|(_, t)| check::owned(t, records))
                .map(|(n, t)| format!("{}(&x->f_{n});", drop_name(t)))
                .collect::<String>();
            let retains = r
                .fields
                .iter()
                .filter(|(_, t)| check::owned(t, records))
                .map(|(n, t)| format!("{}(&x->f_{n});", retain_name(t)))
                .collect::<String>();
            writeln!(o,"static void {}({c} volatile *x){{{drops}}}\nstatic void {}({c} volatile *x){{{retains}}}",drop_name(t),retain_name(t)).unwrap();
        }
        _ => {}
    }
    o
}
fn adopt_helper(t: &Ty, records: &[Record]) -> String {
    if !check::owned(t, records) {
        return String::new();
    }
    let c = c_type(t);
    let body = match t {
        Ty::Buffer(v) => format!(
            "{}dn_adopt(x->data);",
            if check::owned(v, records) {
                format!(
                    "for(size_t i=0;i<x->len;i++){}(&x->data[i]);",
                    adopt_name(v)
                )
            } else {
                String::new()
            }
        ),
        Ty::Boxed(v) => format!(
            "if(*x){{{}dn_adopt(*x);}}",
            if check::owned(v, records) {
                format!("{}(*x);", adopt_name(v))
            } else {
                String::new()
            }
        ),
        Ty::Window | Ty::Resource(..) => "dn_adopt(*x);".into(),
        Ty::ThreadBound(v) => {
            if check::owned(v, records) {
                format!("{}(&x->value);", adopt_name(v))
            } else {
                "(void)x;".into()
            }
        }
        Ty::Array(v, n) => format!("for(size_t i=0;i<{n};i++){}(&x->data[i]);", adopt_name(v)),
        Ty::Record(n) => records
            .iter()
            .find(|r| r.name == *n)
            .unwrap()
            .fields
            .iter()
            .filter(|(_, t)| check::owned(t, records))
            .map(|(n, t)| format!("{}(&x->f_{n});", adopt_name(t)))
            .collect(),
        _ => unreachable!(),
    };
    format!("static void {}({c} volatile*x){{{body}}}\n", adopt_name(t))
}
fn declarations(m: &Mir) -> String {
    let all = all_types(m);
    let mut out = String::new();
    for r in &m.unit.records {
        let tag = if r.union { "union" } else { "struct" };
        writeln!(out, "typedef {tag} dn_r_{} dn_r_{};", r.name, r.name).unwrap();
    }
    for t in &all {
        if matches!(t, Ty::Resource(..)) {
            writeln!(out, "typedef struct dn_t_{} dn_t_{};", key(t), key(t)).unwrap();
        }
    }
    let mut done = BTreeSet::new();
    for t in &all {
        typedef(t, &all, &m.unit.records, &mut done, &mut out);
    }
    for r in &m.unit.records {
        let l = &m.layouts[&r.name];
        writeln!(out,"_Static_assert(sizeof(dn_r_{})=={},\"{} ABI size\");\n_Static_assert(_Alignof(dn_r_{})=={},\"{} ABI alignment\");",r.name,l.size,r.name,r.name,l.align,r.name).unwrap();
        for ((n, _), off) in r.fields.iter().zip(&l.offsets) {
            writeln!(
                out,
                "_Static_assert(offsetof(dn_r_{},f_{n})=={off},\"{}.{n} ABI offset\");",
                r.name, r.name
            )
            .unwrap();
        }
    }
    out
}
fn affinity_helper(t: &Ty, records: &[Record]) -> String {
    if !check::has_affinity(t, records) {
        return String::new();
    }
    let c = c_type(t);
    let k = key(t);
    let child = |t: &Ty, place: &str| {
        if check::has_affinity(t, records) {
            format!("dn_affinity_{}({place});", key(t))
        } else {
            String::new()
        }
    };
    let body = match t {
        Ty::Window => "if(*x)dn_check_thread((*x)->owner);".into(),
        Ty::ThreadBound(q) => format!(
            "if(x->owner){{dn_check_thread(x->owner);{}}}",
            child(q, "&x->value")
        ),
        Ty::Resource(q, _) => format!("if(*x&&(*x)->active){{{}}}", child(q, "&(*x)->value")),
        Ty::Boxed(q) => format!("if(*x){{{}}}", child(q, "*x")),
        Ty::Buffer(q) => format!(
            "for(size_t i=0;i<x->len;i++){{{}}}",
            child(q, "&x->data[i]")
        ),
        Ty::Array(q, n) => format!("for(size_t i=0;i<{n};i++){{{}}}", child(q, "&x->data[i]")),
        Ty::Record(n) => records
            .iter()
            .find(|r| r.name == *n)
            .unwrap()
            .fields
            .iter()
            .map(|(n, t)| child(t, &format!("&x->f_{n}")))
            .collect(),
        _ => String::new(),
    };
    format!("static void dn_affinity_{k}({c} const *x){{{body}}}\n")
}
struct Emitter<'a> {
    mir: &'a Mir,
    temp: usize,
    locals: Vec<Vec<(String, Ty)>>,
    guards: Vec<Option<String>>,
    owned_params: BTreeSet<String>,
    loops: Vec<usize>,
}
impl Emitter<'_> {
    fn local(&self, n: &str) -> String {
        if self.owned_params.contains(n) {
            format!("dn_param_{n}")
        } else {
            local(n)
        }
    }
    fn tmp(&mut self) -> String {
        let n = self.temp;
        self.temp += 1;
        format!("dn_tmp_{n}")
    }
    fn seq(&mut self, args: &[Expr]) -> (String, Vec<String>) {
        let mut p = String::new();
        let mut names = Vec::new();
        for a in args {
            let n = self.tmp();
            let v = self.expr(a);
            writeln!(p, "{} {n}={v};", c_type(a.ty())).unwrap();
            if check::owned(a.ty(), &self.mir.unit.records)
                && !matches!(
                    a.op,
                    Op::Name(_)
                        | Op::Field(..)
                        | Op::Index(..)
                        | Op::IndexProven(..)
                        | Op::Unary(..)
                )
            {
                writeln!(p, "{}(&{n});", adopt_name(a.ty())).unwrap();
            }
            names.push(n);
        }
        (p, names)
    }
    fn place(&mut self, e: &Expr) -> String {
        match &e.op {
            Op::Name(n) => self.local(n),
            Op::Unary(op, a) if op == "*" => format!(
                "(*(({})dn_nonnull((void*)({}))))",
                c_type(a.ty()),
                self.expr(a)
            ),
            Op::Field(a, n) => {
                let base = self.place_or_expr(a);
                if matches!(a.ty(), Ty::Ref(..) | Ty::Boxed(_)) {
                    format!("({base})->f_{n}")
                } else {
                    format!("({base}).f_{n}")
                }
            }
            Op::Index(a, i) | Op::IndexProven(a, i) => {
                let pointer = self.tmp();
                let index = self.tmp();
                let (declaration, ty) = if let Ty::Ref(t, _) = a.ty() {
                    (
                        format!("{} {pointer}={};", c_type(a.ty()), self.expr(a)),
                        &**t,
                    )
                } else {
                    (
                        format!(
                            "{} {}*{pointer}=&({});",
                            c_type(a.ty()),
                            if check::owned(a.ty(), &self.mir.unit.records) {
                                "volatile "
                            } else {
                                ""
                            },
                            self.place(a)
                        ),
                        a.ty(),
                    )
                };
                let length = if let Ty::Array(_, n) = ty {
                    n.to_string()
                } else {
                    format!("{pointer}->len")
                };
                let ix = self.expr(i);
                let checked = if matches!(e.op, Op::IndexProven(..)) {
                    index.clone()
                } else {
                    format!("dn_index({index},{length})")
                };
                format!("(*({{ {declaration}size_t {index}={ix};&{pointer}->data[{checked}]; }}))")
            }
            _ => unreachable!("checked place"),
        }
    }
    fn place_or_expr(&mut self, e: &Expr) -> String {
        if matches!(
            e.op,
            Op::Name(_) | Op::Field(..) | Op::Index(..) | Op::IndexProven(..)
        ) || matches!(&e.op,Op::Unary(op,_)if op=="*")
        {
            self.place(e)
        } else {
            self.expr(e)
        }
    }
    fn expr(&mut self, e: &Expr) -> String {
        let c = c_type(e.ty());
        match &e.op {
            Op::Number(n) => {
                let v = n.replace('_', "");
                if e.ty().scalar().unwrap().float() {
                    if v.starts_with("0x") || v.starts_with("0X") {
                        return format!("(({c})({v}ULL))");
                    }
                    format!(
                        "(({c})({v}{}{}))",
                        if !v.contains(['.', 'e', 'E']) {
                            ".0"
                        } else {
                            ""
                        },
                        if e.ty().scalar() == Some(Scalar::F32) {
                            "f"
                        } else {
                            ""
                        }
                    )
                } else {
                    format!("(({c})({v}ULL))")
                }
            }
            Op::Bool(v) => usize::from(*v).to_string(),
            Op::Text(s) => format!("((dn_utf8){{(const uint8_t*){}, {}}})", q(s), s.len()),
            Op::Name(n) => match n.as_str() {
                "Relaxed" => "memory_order_relaxed".into(),
                "Acquire" => "memory_order_acquire".into(),
                "Release" => "memory_order_release".into(),
                "AcqRel" => "memory_order_acq_rel".into(),
                "SeqCst" => "memory_order_seq_cst".into(),
                _ => self.local(n),
            },
            Op::Borrow(a, _) => format!("(&({}))", self.place(a)),
            Op::Move(a) => {
                if !check::affine(a.ty(), &self.mir.unit.records) {
                    return self.expr(a);
                }
                let v = self.place(a);
                let tmp = self.tmp();
                let adopt = if check::owned(a.ty(), &self.mir.unit.records) {
                    format!("{}(&{tmp});", adopt_name(a.ty()))
                } else {
                    String::new()
                };
                format!("({{ {c} {tmp}={v};{v}=({c}){{0}};{adopt}{tmp}; }})")
            }
            Op::Field(..) | Op::Index(..) | Op::IndexProven(..) => self.place(e),
            Op::Unary(op, a) => {
                if op == "*" {
                    return self.place(e);
                }
                let v = self.expr(a);
                if op == "-" && e.ty().scalar().is_some_and(|s| !s.float()) {
                    let s = e.ty().scalar().unwrap();
                    if let Op::Number(n) = &a.op {
                        if integer_literal(n) == Some(1u128 << (s.bits() - 1)) {
                            return format!(
                                "(({c})(-INT64_C({})-1))",
                                (1u128 << (s.bits() - 1)) - 1
                            );
                        }
                    }
                    format!("dn_{}_neg({v})", s.name())
                } else if op == "~" {
                    format!(
                        "dn_{}_bits(~(uint64_t)({v}))",
                        e.ty().scalar().unwrap().name()
                    )
                } else {
                    format!("({op}({v}))")
                }
            }
            Op::Binary(op, a, b) => {
                if op == "&&" || op == "||" {
                    return format!("({} {op} {})", self.expr(a), self.expr(b));
                }
                let (p, v) = self.seq(&[(**a).clone(), (**b).clone()]);
                let (a, b) = (&v[0], &v[1]);
                let s = match e.ty() {
                    Ty::Scalar(s) => Some(*s),
                    _ => None,
                };
                let body = if let Some(s) = s.filter(|s| !s.float()) {
                    let name = match op.as_str() {
                        "+" => Some("add"),
                        "-" => Some("sub"),
                        "*" => Some("mul"),
                        "/" => Some("div"),
                        "%" => Some("mod"),
                        "<<" => Some("shl"),
                        ">>" => Some("shr"),
                        _ => None,
                    };
                    if let Some(n) = name {
                        format!("dn_{}_{n}({a},{b})", s.name())
                    } else {
                        format!("dn_{}_bits((uint64_t){a} {op} (uint64_t){b})", s.name())
                    }
                } else {
                    format!("({a} {op} {b})")
                };
                format!("({{ {p}{body}; }})")
            }
            Op::Call(n, types, args) => self.call(n, types, args, e.ty()),
            Op::Invoke(f, args) => {
                let callee = self.expr(f);
                let (p, v) = self.seq(args);
                if *e.ty() == Ty::Unit {
                    format!("({{ {} dn_fp={callee};if(!dn_fp)dn_fail(\"null callback\");{p}dn_fp({});dn_fp_default();0; }})",c_type(f.ty()),v.join(","))
                } else {
                    format!("({{ {} dn_fp={callee};if(!dn_fp)dn_fail(\"null callback\");{p}{c} dn_result=dn_fp({});dn_fp_default();dn_result; }})",c_type(f.ty()),v.join(","))
                }
            }
        }
    }
    fn conversion(&mut self, src: &Expr, to: &Ty) -> String {
        let (p, a) = self.seq(std::slice::from_ref(src));
        let a = &a[0];
        let c = c_type(to);
        let s = to.scalar().unwrap();
        let from = src.ty().scalar().unwrap();
        if s.float() {
            return format!("({{ {p}({c}){a}; }})");
        }
        let bits = s.bits();
        let min = if s.signed() {
            format!("-ldexpl(1.0L,{})", bits - 1)
        } else {
            "0.0L".into()
        };
        let limit = format!("ldexpl(1.0L,{})", bits - u32::from(s.signed()));
        let check = if from.float() {
            format!("long double dn_n=(long double){a};if(!isfinite({a})||truncl(dn_n)!=dn_n||dn_n<{min}||dn_n>={limit})dn_fail(\"numeric conversion out of range\");")
        } else if from.signed() {
            let mn = if s.signed() {
                format!("(-((__int128)1<<{}))", bits - 1)
            } else {
                "0".into()
            };
            let mx = if s.signed() {
                format!("(((__int128)1<<{})-1)", bits - 1)
            } else {
                format!("(((__int128)1<<{bits})-1)")
            };
            format!("if((__int128){a}<{mn}||(__int128){a}>{mx})dn_fail(\"numeric conversion out of range\");")
        } else {
            let max = if s.signed() {
                format!("(((unsigned __int128)1<<{})-1)", bits - 1)
            } else {
                format!("(((unsigned __int128)1<<{bits})-1)")
            };
            format!("if((unsigned __int128){a}>{max})dn_fail(\"numeric conversion out of range\");")
        };
        format!("({{ {p}{check}({c}){a}; }})")
    }
    fn call(&mut self, n: &str, types: &[Ty], args: &[Expr], ret: &Ty) -> String {
        let c = c_type(ret);
        if n == "call_trusted" {
            let (prefix, values) = self.seq(&args[1..]);
            let call = format!("({})({})", values[0], values[1..].join(","));
            return if *ret == Ty::Unit {
                format!("({{ {prefix}{call};dn_fp_default();0; }})")
            } else {
                format!("({{ {prefix}{c} dn_callback_result={call};dn_fp_default();dn_callback_result; }})")
            };
        }
        if n == "resource" {
            let tmp = self.tmp();
            let value = self.expr(&args[0]);
            return format!(
                "({{ {c} {tmp}=dn_begin_{}();dn_commit_{}({tmp},{value}); }})",
                key(ret),
                key(ret)
            );
        }
        if n == "cast" || Scalar::parse(n).is_some() {
            return self.conversion(&args[0], ret);
        }
        if n == "fn_ptr" {
            let Op::Name(name) = &args[0].op else {
                unreachable!()
            };
            let f = self
                .mir
                .unit
                .functions
                .iter()
                .find(|f| f.name == *name)
                .unwrap();
            return if let Some(s) = &f.export {
                s.clone()
            } else if f.library.is_some() {
                format!("dn_ext_{name}")
            } else {
                format!("dn_cb_{name}")
            };
        }
        let (p, v) = self.seq(args);
        let a = |i: usize| v[i].clone();
        let storage = |i: usize| {
            let t = args[i].ty();
            let (t, b) = if let Ty::Ref(t, _) = t {
                (&**t, format!("(*({}))", v[i]))
            } else {
                (t, format!("({})", v[i]))
            };
            let len = if let Ty::Array(_, n) = t {
                n.to_string()
            } else {
                format!("{b}.len")
            };
            (format!("{b}.data"), len)
        };
        let body = if let Some(f) = self.mir.unit.functions.iter().find(|f| f.name == n) {
            let call = format!(
                "{}({})",
                if f.library.is_some() {
                    format!("dn_ext_{n}")
                } else {
                    cname(n)
                },
                v.join(",")
            );
            if f.library.is_some() {
                if *ret == Ty::Unit {
                    format!("({{ {call};dn_fp_default();0; }})")
                } else {
                    format!("({{ {c} dn_foreign={call};dn_fp_default();dn_foreign; }})")
                }
            } else {
                call
            }
        } else if self.mir.unit.records.iter().any(|r| r.name == n) {
            format!("(({c}){{{}}})", v.join(","))
        } else {
            match n{
            "size_of"=>format!("((size_t)sizeof({}))",c_type(&types[0])),
            "align_of"=>format!("((size_t)_Alignof({}))",c_type(&types[0])),
            "abi_version"=>{let Ty::Record(name)=&types[0] else{unreachable!()};self.mir.layouts[name].version.to_string()},
            "resource_get"|"resource_get_mut"=>format!("(&((*{})->value))",a(0)),
            "resource_close"=>format!("({{ {}(&{});0; }})",drop_name(args[0].ty()),a(0)),
            "resource_into_inner"=>format!("dn_take_{}({})",key(args[0].ty()),a(0)),
            "thread_bind"=>format!("(({c}){{{},dn_thread_id()}})",a(0)),
            "thread_get"|"thread_get_mut"=>format!("({{ dn_check_thread({}->owner);&({}->value); }})",a(0),a(0)),
            "window_open"=>format!("dn_window_create(0,0,{},{},{})",a(0),a(1),a(2)),
            "window_child"=>format!("dn_window_create(1,{},{},{},{})",a(0),a(1),a(2),a(3)),
            "window_attach"=>format!("dn_window_create(2,{},(dn_utf8){{0}},{},{})",a(0),a(1),a(2)),
            "window_resize"|"window_focus"|"window_gpu"=>format!("dn_window_action(*{}, {}, {}, {})",a(0),if n=="window_resize"{0}else if n=="window_focus"{1}else{2},if n=="window_resize"{a(1)}else{"0".into()},if n=="window_resize"{a(2)}else{"0".into()}),
            "window_input"=>format!("dn_window_input(*{},{},{},{},{})",a(0),a(1),a(2),a(3),a(4)),
            "window_handle"=>format!("dn_window_handle(*{})",a(0)),
            "window_read"=>format!("({{ {c} image={{0}};image.data=dn_window_read(*{},&image.len);image.cap=image.len;image; }})",a(0)),
            "window_dpi"=>format!("dn_window_dpi(*{})",a(0)),
            "window_poll"=>format!("({{ {c} event;dn_window_poll(*{},{},event.data);event; }})",a(0),a(1)),
            "window_scene"=>{let(data,len)=storage(1);format!("dn_window_scene(*{},{data},{len},{},{},{},{})",a(0),a(2),a(3),a(4),a(5))},
            "array"=>format!("(({c}){{{{{}}}}})",v.join(",")),
            "push"=>{let Ty::Ref(t,_)=args[0].ty() else{unreachable!()};format!("dn_push_{}({},{})",key(t),a(0),a(1))},
            "buffer_aligned"=>format!("dn_aligned_{}({},{})",key(ret),a(0),a(1)),
            "buffer"|"box"=>format!("dn_new_{}({})",key(ret),a(0)),
            "zero"|"union_zero"=>format!("(({c}){{0}})"),
            "len"=>storage(0).1,
            "resize"=>{let Ty::Ref(t,_)=args[0].ty() else{unreachable!()};format!("dn_resize_{}({},{})",key(t),a(0),a(1))},
            "split_at_mut"=>{let(data,len)=storage(0);let Ty::Split(t)=ret else{unreachable!()};let sc=c_type(&Ty::Slice(t.clone(),true));format!("({{ dn_range({},0,{len});({c}){{({sc}){{{data},{}}},({sc}){{{data}?{data}+{}:NULL,{len}-{}}}}}; }})",a(1),a(1),a(1),a(1))},
            "slice"|"mut_slice"=>{let(data,len)=storage(0);format!("({{ dn_range({},{},{len});({c}){{{data}?{data}+{}:NULL,{}}}; }})",a(1),a(2),a(1),a(2))},
            "borrowed_ptr"|"as_raw"=>format!("(({c}){})",a(0)),
            "data"=>format!("(({}){})",c_type(ret),storage(0).0),
            "foreign"=>format!("({{ if({}&&!{})dn_fail(\"null foreign buffer\");dn_size({},sizeof(*{}));({c}){{{},{}}}; }})",a(1),a(0),a(1),a(0),a(0),a(1)),
            "ptr_offset"=>format!("(({c})dn_offset({},{},sizeof(*{})))",a(0),a(1),a(0)),
            "nonnull"=>format!("(({c})dn_nonnull((void*){}))",a(0)),"null"=>format!("(({c})NULL)"),
            "into_raw"=>format!("({{ {}(&{});{}; }})",retain_name(args[0].ty()),a(0),a(0)),
            "from_raw"=>if check::has_affinity(ret,&self.mir.unit.records){format!("({{ {c} dn_raw=dn_nonnull((void*){});dn_affinity_{}(&dn_raw);dn_raw; }})",a(0),key(ret))}else{format!("(({c})dn_nonnull((void*){}))",a(0))},
            "handle"=>format!("((void*){})",a(0)),"handle_ptr"=>format!("(({c}){})",a(0)),"address"=>format!("((size_t)(uintptr_t){})",a(0)),
            "atomic"=>{if let Ty::Atomic(t)=ret{if **t==Ty::Scalar(Scalar::F32){format!("dn_f32_bits({})",a(0))}else{a(0)}}else{unreachable!()}},
            "atomic_load"|"atomic_store"|"atomic_exchange"|"atomic_compare_exchange"|"atomic_is_lock_free"=>{let Ty::Ref(t,_)=args[0].ty() else{unreachable!()};let Ty::Atomic(t)=&**t else{unreachable!()};let fl=**t==Ty::Scalar(Scalar::F32);let val=|i:usize|if fl{format!("dn_f32_bits({})",a(i))}else{a(i)};let raw=match n{"atomic_load"=>format!("atomic_load_explicit({},{})",a(0),a(1)),"atomic_store"=>format!("(atomic_store_explicit({},{},{}),0)",a(0),val(1),a(2)),"atomic_exchange"=>format!("atomic_exchange_explicit({},{},{})",a(0),val(1),a(2)),"atomic_compare_exchange"=>format!("({{ {} dn_expected={};atomic_compare_exchange_strong_explicit({},&dn_expected,{},{},{}); }})",if fl{"uint32_t".into()}else{c_type(t)},val(1),a(0),val(2),a(3),a(4)),_=>format!("atomic_is_lock_free({})",a(0))};if fl&&(n=="atomic_load"||n=="atomic_exchange"){format!("dn_f32_from_bits({raw})")}else{raw}},
            "simd_splat"=>{let Ty::Simd(_,lanes)=ret else{unreachable!()};format!("({{ {c} dn_v;for(size_t dn_i=0;dn_i<{lanes};dn_i++)dn_v.data[dn_i]={};dn_v; }})",a(0))},
            "simd_add"|"simd_mul"|"simd_eq"=>{let Ty::Simd(s,lanes)=ret else{unreachable!()};if n=="simd_eq"{format!("({{ {c} dn_v;for(size_t dn_i=0;dn_i<{lanes};dn_i++)dn_v.data[dn_i]=({}.data[dn_i]=={}.data[dn_i])?1.0:0.0;dn_v; }})",a(0),a(1))}else{format!("({{ {c} dn_v;dn_simd_{}(dn_v.data,{}.data,{}.data,{lanes},{});dn_v; }})",if *s==Scalar::F32{"f32"}else{"f64"},a(0),a(1),usize::from(n=="simd_mul"))}},
            "simd_load"|"simd_load_aligned"=>{let Ty::Simd(s,lanes)=ret else{unreachable!()};let(data,len)=storage(0);let aligned=if n.ends_with("aligned"){format!("dn_aligned({data}+{},{});",a(1),(*lanes*s.bits() as usize/8).min(64))}else{String::new()};format!("({{ dn_range({},{lanes},{len});{aligned}{c} dn_v;memcpy(dn_v.data,{data}+{},sizeof dn_v);dn_v; }})",a(1),a(1))},
            "simd_store"|"simd_store_aligned"=>{let Ty::Simd(s,lanes)=args[2].ty() else{unreachable!()};let(data,len)=storage(0);let aligned=if n.ends_with("aligned"){format!("dn_aligned({data}+{},{});",a(1),(*lanes*s.bits() as usize/8).min(64))}else{String::new()};format!("({{ dn_range({},{lanes},{len});{aligned}memcpy({data}+{},{}.data,sizeof {});0; }})",a(1),a(1),a(2),a(2))},
            "read_le"|"read_be"=>{let s=ret.scalar().unwrap();let(data,len)=storage(0);let raw=format!("dn_binary_read({data},{len},{},{},{})",a(1),s.bits()/8,usize::from(n.ends_with("be")));if s.float(){format!("dn_{}_from_bits({raw})",if s==Scalar::F32{"f32"}else{"f64"})}else{format!("dn_{}_bits({raw})",s.name())}},
            "write_le"|"write_be"=>{let s=types[0].scalar().unwrap();let(data,len)=storage(0);let val=if s.float(){format!("dn_{}_bits({})",if s==Scalar::F32{"f32"}else{"f64"},a(2))}else{format!("(uint64_t){}",a(2))};format!("(dn_binary_write({data},{len},{},{},{val},{}),0)",a(1),s.bits()/8,usize::from(n.ends_with("be")))},
            "bits"=>format!("dn_{}_bits({})",if args[0].ty()==&Ty::Scalar(Scalar::F32){"f32"}else{"f64"},a(0)),"from_bits"=>format!("dn_{}_from_bits({})",if ret==&Ty::Scalar(Scalar::F32){"f32"}else{"f64"},a(0)),
            "nan"=>format!("(({c})NAN)"),"infinity"=>format!("(({c})INFINITY)"),
            "sqrt"|"sin"|"cos"|"exp"|"log"|"abs"|"floor"|"ceil"|"trunc"=>format!("{}{}({})",if n=="abs"{"fabs"}else{n},if ret==&Ty::Scalar(Scalar::F32){"f"}else{""},a(0)),
            "is_nan"=>format!("isnan({})",a(0)),"is_finite"=>format!("isfinite({})",a(0)),"sign_bit"=>format!("(signbit({})!=0)",a(0)),
            "assert"=>format!("(dn_assert({}),0)",a(0)),
            "print"=>match args[0].ty(){Ty::Str=>format!("(fwrite({}.data,1,{}.len,stdout),fputc('\\n',stdout),0)",a(0),a(0)),Ty::Bool=>format!("(puts({}?\"true\":\"false\"),0)",a(0)),Ty::Scalar(s) if s.float()=>format!("dn_print_f64((double){})",a(0)),Ty::Scalar(s) if s.signed()=>format!("(printf(\"%\" PRId64 \"\\n\",(int64_t){}),0)",a(0)),_=>format!("(printf(\"%\" PRIu64 \"\\n\",(uint64_t){}),0)",a(0))},
            "utf8_bytes"=>format!("(({c}){{{}.data,{}.len}})",a(0),a(0)),
            n if n.starts_with("wrapping_")||n.starts_with("saturating_")=>{let mode=if n.starts_with("wrapping_"){"wrap"}else{"sat"};format!("dn_{}_{}_{}({},{})",ret.scalar().unwrap().name(),mode,n.split('_').nth(1).unwrap(),a(0),a(1))},
            _=>unreachable!("verified native operation {n}")
        }
        };
        format!("({{ {p}{body}; }})")
    }
    fn drop_scope(&self, index: usize) -> String {
        self.locals[index]
            .iter()
            .rev()
            .filter(|(_, t)| check::owned(t, &self.mir.unit.records))
            .map(|(n, t)| format!("{}(&{});\n", drop_name(t), self.local(n)))
            .collect()
    }
    fn cleanup(&self, from: usize) -> String {
        let mut out = String::new();
        for i in (from..self.locals.len()).rev() {
            out.push_str(&self.drop_scope(i));
            if let Some(g) = &self.guards[i] {
                out.push_str(&format!("dn_boundary_end(&{g},0);\n"));
            }
        }
        out
    }
    fn guard(&mut self, index: usize) -> String {
        if !self.locals[index]
            .iter()
            .any(|(_, t)| check::owned(t, &self.mir.unit.records))
        {
            return String::new();
        }
        let g = self.tmp();
        let j = self.tmp();
        self.guards[index] = Some(g.clone());
        format!("dn_boundary {g}={{0}};jmp_buf {j};{g}.jump=&{j};{g}.previous=dn_current;{g}.foreign_marker=dn_foreign_context;dn_current=&{g};if(setjmp({j})){{dn_current={g}.previous;{}dn_boundary_end(&{g},1);dn_raise();}}\n",self.drop_scope(index))
    }
    fn block(&mut self, b: &[Stmt]) -> String {
        let owned: Vec<_> = b
            .iter()
            .filter_map(|s| {
                if let Stmt::Let { name, ty, .. } = s {
                    if check::owned(ty, &self.mir.unit.records) {
                        Some((name.clone(), ty.clone()))
                    } else {
                        None
                    }
                } else {
                    None
                }
            })
            .collect();
        self.locals.push(owned.clone());
        self.guards.push(None);
        let index = self.locals.len() - 1;
        let mut o = String::from("{\n");
        for (n, t) in owned {
            writeln!(o, "{} volatile {}={{0}};", c_type(&t), self.local(&n)).unwrap();
        }
        o.push_str(&self.guard(index));
        for s in b {
            o.push_str(&self.stmt(s));
        }
        o.push_str(&self.cleanup(index));
        self.locals.pop();
        self.guards.pop();
        o.push_str("}\n");
        o
    }
    fn stmt(&mut self, s: &Stmt) -> String {
        match s {
            Stmt::Let {
                name, ty, value, ..
            } => {
                let code = self.expr(value);
                if check::owned(ty, &self.mir.unit.records) {
                    format!("{}={code};\n", self.local(name))
                } else {
                    self.locals
                        .last_mut()
                        .unwrap()
                        .push((name.clone(), ty.clone()));
                    format!("{} {}={code};\n", c_type(ty), self.local(name))
                }
            }
            Stmt::Set(a, b) => {
                let p = self.place(a);
                let v = self.expr(b);
                let ptr = self.tmp();
                let tmp = self.tmp();
                let owned = check::owned(a.ty(), &self.mir.unit.records);
                let drop = if owned {
                    format!(
                        "{}({ptr});{}(&{tmp});",
                        drop_name(a.ty()),
                        retain_name(a.ty())
                    )
                } else {
                    String::new()
                };
                format!(
                    "{{ {} {tmp}={v};{adopt}{} {}*{ptr}=&({p});{drop}*{ptr}={tmp}; }}\n",
                    c_type(a.ty()),
                    c_type(a.ty()),
                    if owned { "volatile " } else { "" },
                    adopt = if owned {
                        format!("{}(&{tmp});", adopt_name(a.ty()))
                    } else {
                        String::new()
                    }
                )
            }
            Stmt::Expr(e) => format!("(void)({});\n", self.expr(e)),
            Stmt::Return(e, _) => {
                let code = e
                    .as_ref()
                    .map(|e| self.expr(e))
                    .unwrap_or_else(|| "0".into());
                let c = e
                    .as_ref()
                    .map(|e| c_type(e.ty()))
                    .unwrap_or_else(|| "uint8_t".into());
                let tmp = self.tmp();
                let adopt = e
                    .as_ref()
                    .filter(|e| check::owned(e.ty(), &self.mir.unit.records))
                    .map(|e| format!("{}(&{tmp});", adopt_name(e.ty())))
                    .unwrap_or_default();
                format!(
                    "{{ {c} {tmp}={code};{adopt}{}return {tmp}; }}\n",
                    self.cleanup(0)
                )
            }
            Stmt::If(c, a, b) => {
                let c = self.expr(c);
                let a = self.block(a);
                let b = self.block(b);
                format!("if({c}){a}else{b}")
            }
            Stmt::While(c, b) => {
                let c = self.expr(c);
                self.loops.push(self.locals.len());
                let body = self.block(b);
                self.loops.pop();
                format!("while({c}){body}")
            }
            Stmt::For(n, a, b, body) => {
                let a = self.expr(a);
                let b = self.expr(b);
                let lim = self.tmp();
                let start = self.tmp();
                self.loops.push(self.locals.len());
                let body = self.block(body);
                self.loops.pop();
                format!("{{ size_t {start}={a};size_t {lim}={b};for(size_t {}={start};{}<{lim};{}++){body} }}\n",self.local(n),self.local(n),self.local(n))
            }
            Stmt::Break(_) => format!("{}break;\n", self.cleanup(*self.loops.last().unwrap())),
            Stmt::Continue(_) => {
                format!("{}continue;\n", self.cleanup(*self.loops.last().unwrap()))
            }
            Stmt::Unsafe(b) | Stmt::Block(b) => self.block(b),
        }
    }
}
fn signature(f: &Function, name: &str) -> String {
    format!(
        "{} {name}({})",
        c_type(&f.ret),
        if f.params.is_empty() {
            "void".into()
        } else {
            f.params
                .iter()
                .map(|(n, t)| format!("{} {}", c_type(t), local(n)))
                .collect::<Vec<_>>()
                .join(",")
        }
    )
}
fn abi_signature(f: &Function, name: &str) -> String {
    if f.ret == Ty::Unit {
        signature(f, name).replacen("uint8_t ", "void ", 1)
    } else {
        signature(f, name)
    }
}
/// Full translation unit. Numeric values stay unboxed. The optional window
/// adapter reuses the existing GUI runtime; instances own their application data.
pub fn emit(m: &Mir, executable: bool) -> String {
    let gui = all_types(m).contains(&Ty::Window);
    let ordinary = super::bridge::lower(&m.unit).expect("verified ordinary bridge");
    let legacy_needed = gui || ordinary.is_some();
    let mut out = String::new();
    if legacy_needed {
        out.push_str("static _Noreturn void dn_legacy_error(const char *);\n");
        let ordinary_c = ordinary.as_ref().map(crate::backend_c::emit_c);
        let legacy = ordinary_c
            .as_deref()
            .unwrap_or(crate::backend_c::C_RUNTIME)
            .replace(
                "int main(int argc, char **argv)",
                "static int dn_legacy_main(int argc, char **argv)",
            );
        let legacy=legacy.replace("static LRESULT CALLBACK dvw_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {","static LRESULT CALLBACK dn_legacy_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {")
            .replace("#ifndef DVW_CALL_HOST","static LRESULT dn_ui_call_host(WNDPROC,HWND,UINT,WPARAM,LPARAM);\n#define DVW_CALL_HOST dn_ui_call_host\n#ifndef DVW_CALL_HOST");
        let start = legacy
            .find("static void dv_fail(const char *message) {")
            .unwrap();
        let end = start + legacy[start..].find("\n}").unwrap() + 2;
        out.push_str(&legacy[..start]);
        out.push_str("static void dv_fail(const char *message) { dn_legacy_error(message); }");
        out.push_str(&legacy[end..]);
        if ordinary.is_none() {
            out.push_str("\nstatic void dlm_program(void) {}\n");
        }
    }
    if legacy_needed {
        out.push_str(
            "static void dn_legacy_shutdown(void);\n#define DN_RUNTIME_CLOSE dn_legacy_shutdown\n",
        );
    }
    out.push_str(RUNTIME);
    if legacy_needed {
        out.push_str(include_str!("window.c"));
    }
    if ordinary.is_some() {
        out.push_str(super::bridge::C_HELPERS);
    }
    out.push_str(&scalar_arithmetic());
    out.push_str(&declarations(m));
    let all = all_types(m);
    for t in &all {
        if check::owned(t, &m.unit.records) {
            writeln!(
                out,
                "static void {}({} volatile*);static void {}({} volatile*);",
                drop_name(t),
                c_type(t),
                retain_name(t),
                c_type(t)
            )
            .unwrap();
        }
    }
    for t in &all {
        if check::owned(t, &m.unit.records) {
            writeln!(
                out,
                "static void {}({} volatile*);",
                adopt_name(t),
                c_type(t)
            )
            .unwrap();
        }
    }
    for f in &m.unit.functions {
        if f.library.is_none() {
            writeln!(out, "static {};", signature(f, &cname(&f.name))).unwrap();
        }
    }
    for t in &all {
        if check::has_affinity(t, &m.unit.records) {
            writeln!(
                out,
                "static void dn_affinity_{}({} const *);",
                key(t),
                c_type(t)
            )
            .unwrap();
        }
    }
    for t in &all {
        out.push_str(&affinity_helper(t, &m.unit.records));
        out.push_str(&helpers(t, &m.unit.records));
        out.push_str(&adopt_helper(t, &m.unit.records));
    }
    for f in &m.unit.functions {
        if f.library.is_some() {
            writeln!(
                out,
                "extern {} __asm__({});",
                abi_signature(f, &format!("dn_ext_{}", f.name)),
                q(&f.name)
            )
            .unwrap();
        }
        if f.body.is_some() || f.ordinary.is_some() {
            writeln!(
                out,
                "static {};",
                abi_signature(f, &format!("dn_cb_{}", f.name))
            )
            .unwrap();
        }
        if let Some(s) = &f.export {
            writeln!(out, "DN_EXPORT {};", abi_signature(f, s)).unwrap();
        }
    }
    for f in m
        .unit
        .functions
        .iter()
        .filter(|f| f.body.is_some() || f.ordinary.is_some())
    {
        let owned_params = f
            .params
            .iter()
            .filter(|(_, t)| check::owned(t, &m.unit.records))
            .map(|(n, _)| n.clone())
            .collect();
        let mut e = Emitter {
            mir: m,
            temp: 0,
            locals: vec![f.params.clone()],
            guards: vec![None],
            owned_params,
            loops: vec![],
        };
        let mut preamble = String::new();
        for (n, t) in &f.params {
            if check::owned(t, &m.unit.records) {
                writeln!(
                    preamble,
                    "{} volatile {}={};",
                    c_type(t),
                    e.local(n),
                    local(n)
                )
                .unwrap();
            }
        }
        preamble.push_str(&e.guard(0));
        let body = if f.ordinary.is_some() {
            super::bridge::c_body(&m.unit, ordinary.as_ref().unwrap(), f)
        } else {
            format!("{preamble}{}", e.block(f.body.as_ref().unwrap()))
        };
        let cleanup = e.cleanup(0);
        writeln!(
            out,
            "static {} {{\n{}\n{cleanup}{}\n}}",
            signature(f, &cname(&f.name)),
            body,
            if f.ret == Ty::Unit {
                "return 0;"
            } else {
                "dn_fail(\"missing native return\");"
            }
        )
        .unwrap();
        let mut wrappers = vec![(format!("dn_cb_{}", f.name), "static")];
        if let Some(s) = &f.export {
            wrappers.push((s.clone(), "DN_EXPORT"));
        }
        for (s, visibility) in wrappers {
            let args = f
                .params
                .iter()
                .map(|(n, _)| local(n))
                .collect::<Vec<_>>()
                .join(",");
            let ret = c_type(&f.ret);
            let fail = if f.ret == Ty::Unit {
                "return;".into()
            } else {
                format!("return ({ret}){{0}};")
            };
            let invoke = if f.ret == Ty::Unit {
                format!(
                    "(void){}({args});dn_boundary_end(&dn_b,0);dn_leave();return;",
                    cname(&f.name)
                )
            } else {
                format!(
                    "{ret} dn_result={}({args});dn_boundary_end(&dn_b,0);dn_leave();return dn_result;",
                    cname(&f.name)
                )
            };
            writeln!(out,"{visibility} {} {{\n if(!dn_enter()){{{fail}}} dn_boundary dn_b={{0}};jmp_buf dn_jump;dn_b.jump=&dn_jump;dn_b.previous=dn_current;dn_b.foreign_marker=dn_foreign_context;dn_current=&dn_b;dn_fp_begin(&dn_b);if(!dn_b.previous)dn_error=NULL;\n if(setjmp(dn_jump)){{dn_boundary_end(&dn_b,1);dn_leave();{fail}}}\n {invoke}\n}}",abi_signature(f,&s)).unwrap();
        }
    }
    for r in &m.unit.records {
        let l = &m.layouts[&r.name];
        writeln!(out,"DN_EXPORT size_t dlm_native_sizeof_{}(void){{return {};}}\nDN_EXPORT size_t dlm_native_alignof_{}(void){{return {};}}\nDN_EXPORT uint32_t dlm_native_version_{}(void){{return {};}}",r.name,l.size,r.name,l.align,r.name,l.version).unwrap();
        for ((f, _), off) in r.fields.iter().zip(&l.offsets) {
            writeln!(
                out,
                "DN_EXPORT size_t dlm_native_offset_{}_{f}(void){{return {off};}}",
                r.name
            )
            .unwrap();
        }
    }
    if executable {
        if let Some(f) = m
            .unit
            .functions
            .iter()
            .find(|f| f.name == "main" && f.body.is_some())
        {
            let call = cname(&f.name);
            out.push_str(&format!("int main(void){{(void)dlm_native_thread_init();dn_boundary b={{0}};jmp_buf dn_jump;b.jump=&dn_jump;b.foreign_marker=dn_foreign_context;dn_current=&b;dn_fp_begin(&b);if(setjmp(dn_jump)){{dn_boundary_end(&b,1);fprintf(stderr,\"DLM native error: %s\\n\",dn_error);return 1;}}(void){call}();dn_boundary_end(&b,0);(void)dlm_native_runtime_shutdown();return 0;}}\n"));
        } else {
            out.push_str("int main(void){return 0;}\n");
        }
    }
    out
}
pub fn header(m: &Mir) -> String {
    fn visible(t: &Ty, by_value: bool, m: &Mir, all: &mut BTreeSet<Ty>) {
        if matches!(t, Ty::Record(_)) && !by_value {
            return;
        }
        if !all.insert(t.clone()) {
            return;
        }
        match t {
            Ty::Record(n) => {
                if let Some(r) = m.unit.records.iter().find(|r| r.name == *n) {
                    for (_, t) in &r.fields {
                        visible(t, true, m, all);
                    }
                }
            }
            Ty::Array(t, _) => visible(t, true, m, all),
            Ty::Ptr(t) | Ty::NonNull(t) | Ty::BorrowedPtr(t) | Ty::Boxed(t) | Ty::Ref(t, _) => {
                visible(t, false, m, all)
            }
            Ty::FnPtr(p, r) => {
                for t in p {
                    visible(t, true, m, all);
                }
                visible(r, true, m, all);
            }
            Ty::Buffer(t)
            | Ty::Slice(t, _)
            | Ty::ForeignBuffer(t)
            | Ty::ThreadBound(t)
            | Ty::Atomic(t) => visible(t, true, m, all),
            _ => {}
        }
    }
    let mut all = BTreeSet::new();
    for f in &m.unit.functions {
        if f.export.is_some() {
            visible(&f.ret, true, m, &mut all);
            for (_, t) in &f.params {
                visible(t, true, m, &mut all);
            }
        }
    }
    let mut out=String::from("#ifndef DLM_NATIVE_APPLICATION_H\n#define DLM_NATIVE_APPLICATION_H\n#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#ifdef __cplusplus\n#include <atomic>\n#define DN_HEADER_ATOMIC(T) std::atomic<T>\n#define DN_HEADER_ASSERT static_assert\n#else\n#include <stdatomic.h>\n#define DN_HEADER_ATOMIC(T) _Atomic(T)\n#define DN_HEADER_ASSERT _Static_assert\n#endif\ntypedef struct dn_utf8 {const uint8_t *data;size_t len;} dn_utf8;\ntypedef struct dn_window dn_window;\n");
    for r in &m.unit.records {
        writeln!(
            out,
            "typedef {} dn_r_{} dn_r_{};",
            if r.union { "union" } else { "struct" },
            r.name,
            r.name
        )
        .unwrap();
    }
    for t in &all {
        if matches!(t, Ty::Resource(..)) {
            writeln!(out, "typedef struct dn_t_{} dn_t_{};", key(t), key(t)).unwrap();
        }
    }
    let mut done = BTreeSet::new();
    for t in &all {
        typedef(t, &all, &m.unit.records, &mut done, &mut out);
    }
    out.push_str("#ifdef __cplusplus\nextern \"C\" {\n#endif\nconst char *dlm_native_last_error(void);\nvoid dlm_native_clear_error(void);\nuint32_t dlm_native_abi_version(void);\nuint64_t dlm_native_thread_init(void);\nuint64_t dlm_native_allocations(void);\nuint64_t dlm_native_frees(void);\nint dlm_native_cpu_profile(void);\nuint64_t dlm_native_live_allocations(void);\nbool dlm_native_runtime_init(void);\nbool dlm_native_runtime_shutdown(void);\n");
    for f in &m.unit.functions {
        if let Some(s) = &f.export {
            writeln!(out, "{};", abi_signature(f, s)).unwrap();
        }
    }
    for r in &m.unit.records {
        writeln!(out,"size_t dlm_native_sizeof_{}(void);\nsize_t dlm_native_alignof_{}(void);\nuint32_t dlm_native_version_{}(void);",r.name,r.name,r.name).unwrap();
        for (f, _) in &r.fields {
            writeln!(out, "size_t dlm_native_offset_{}_{f}(void);", r.name).unwrap();
        }
    }
    out.push_str(
        "#ifdef __cplusplus\n}\n#endif\n#undef DN_HEADER_ATOMIC\n#undef DN_HEADER_ASSERT\n#endif\n",
    );
    out.replace("_Atomic(", "DN_HEADER_ATOMIC(").replace(
        "#define DN_HEADER_ATOMIC(T) DN_HEADER_ATOMIC(T)",
        "#define DN_HEADER_ATOMIC(T) _Atomic(T)",
    )
}
