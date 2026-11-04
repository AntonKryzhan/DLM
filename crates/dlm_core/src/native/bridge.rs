//! Explicit value conversion at the boundary to the existing ordinary core.
//! Ordinary code is checked independently; native loops never use dynamic values.
use super::vm::Value as V;
use super::*;
use crate::compile::{IrFunction, IrProgram};
use crate::ordinary::OrdinaryType as O;
use crate::runtime::RuntimeValue as R;
use std::cell::RefCell;
use std::fmt::Write;
use std::rc::Rc;

fn target<'a>(ir: &'a IrProgram, f: &Function) -> Option<(usize, &'a IrFunction)> {
    let name = f.ordinary.as_ref()?;
    let mut matches = ir
        .functions
        .iter()
        .enumerate()
        .filter(|(_, v)| v.name == *name || v.symbol == *name);
    let first = matches.next()?;
    if matches.next().is_some() {
        None
    } else {
        Some(first)
    }
}
fn record<'a>(ir: &'a IrProgram, t: &O) -> Option<&'a crate::compile::IrRecord> {
    match t {
        O::RecordRef(id) => ir.records.get(id.raw() as usize),
        O::Record(n) => ir.records.iter().find(|r| r.name == *n || r.symbol == *n),
        _ => None,
    }
}
fn compatible(t: &Ty, o: &O, u: &Unit, ir: &IrProgram, input: bool) -> bool {
    if let Ty::Ref(t, false) = t {
        return input && compatible(t, o, u, ir, true);
    }
    match (t, o) {
        (Ty::Scalar(s), O::Float) => s.float(),
        (Ty::Scalar(s), O::Nat) => !s.signed() && !s.float(),
        (Ty::Scalar(s), O::Int) => s.signed(),
        (Ty::Bool, O::Bool) | (Ty::Str, O::Text) => input || *t == Ty::Bool,
        (Ty::Buffer(q) | Ty::Array(q, _) | Ty::Slice(q, false), O::Text) => {
            **q == Ty::Scalar(Scalar::U8) && (!matches!(t, Ty::Slice(..)) || input)
        }
        (Ty::Buffer(q) | Ty::Array(q, _) | Ty::Slice(q, false), O::FloatVec) => {
            matches!(&**q,Ty::Scalar(s) if s.float()) && (!matches!(t, Ty::Slice(..)) || input)
        }
        (Ty::Buffer(q) | Ty::Array(q, _) | Ty::Slice(q, false), O::NatVec) => {
            matches!(&**q,Ty::Scalar(s) if !s.float()&&!s.signed())
                && (!matches!(t, Ty::Slice(..)) || input)
        }
        (Ty::Record(n), _) => {
            let Some(a) = u.records.iter().find(|r| r.name == *n) else {
                return false;
            };
            let Some(b) = record(ir, o) else {
                return false;
            };
            !a.union
                && a.fields.len() == b.fields.len()
                && a.fields
                    .iter()
                    .zip(&b.fields)
                    .all(|((n, t), (m, q))| n == m && compatible(t, q, u, ir, input))
        }
        _ => false,
    }
}
pub(super) fn lower(u: &Unit) -> Result<Option<IrProgram>, Vec<Diagnostic>> {
    if !u.functions.iter().any(|f| f.ordinary.is_some()) {
        return Ok(None);
    }
    let Some(p) = &u.ordinary_program else {
        return Err(vec![error(
            1,
            "ordinary bridge requires checked source program",
        )]);
    };
    if p.modules.is_empty()
        || p.modules
            .iter()
            .any(|m| !m.native.is_empty() || m.native.ordinary_program.is_some())
    {
        return Err(vec![error(
            1,
            "ordinary bridge source must contain only ordinary declarations",
        )]);
    }
    let ir = crate::lower_program(p, crate::CheckPolicy::allow_unsafe())?;
    if ir.main.is_some()
        || ir.init.iter().any(|i| {
            let e = match i {
                crate::compile::IrInit::Global { expr, .. }
                | crate::compile::IrInit::Effect(expr) => expr,
            };
            !crate::compile::init_effects(e, &ir.functions).is_empty()
        })
    {
        return Err(vec![error(
            1,
            "ordinary bridge requires pure initializers and no ordinary main",
        )]);
    }
    let mut errors = vec![];
    for f in u.functions.iter().filter(|f| f.ordinary.is_some()) {
        let Some((_, old)) = target(&ir, f) else {
            errors.push(error(
                f.line,
                "ordinary bridge target is missing or ambiguous",
            ));
            continue;
        };
        if old.captures != 0 || !old.effects.is_empty() {
            errors.push(error(
                f.line,
                "ordinary bridge target must be a pure function without captures",
            ));
            continue;
        }
        let valid = f.body.is_none()
            && f.library.is_none()
            && f.params.len() == old.param_types.len()
            && f.params.iter().zip(&old.param_types).all(|((_, t), q)| {
                (!check::owned(t, &u.records) || matches!(t, Ty::Ref(_, false)))
                    && compatible(t, q, u, &ir, true)
            })
            && old
                .ret
                .as_ref()
                .is_some_and(|q| compatible(&f.ret, q, u, &ir, false));
        if !valid {
            errors.push(error(f.line,"incompatible ordinary bridge value types; owned input containers require a shared reference"));
        }
    }
    if errors.is_empty() {
        Ok(Some(ir))
    } else {
        Err(errors)
    }
}
struct Convert<'a> {
    u: &'a Unit,
    ir: &'a IrProgram,
    code: String,
    serial: usize,
}
impl Convert<'_> {
    fn tmp(&mut self) -> String {
        let s = format!("dn_bridge_{}", self.serial);
        self.serial += 1;
        s
    }
    fn storage(&self, t: &Ty, x: &str) -> (String, String) {
        match t {
            Ty::Array(_, n) => (format!("({x}).data"), n.to_string()),
            _ => (format!("({x}).data"), format!("({x}).len")),
        }
    }
    fn pack(&mut self, t: &Ty, o: &O, x: &str) -> String {
        if let Ty::Ref(t, false) = t {
            return self.pack(t, o, &format!("(*({x}))"));
        }
        match (t, o) {
            (Ty::Scalar(_), O::Float) => format!("dvf_float((double)({x}))"),
            (Ty::Scalar(_), O::Nat) => format!("dv_nat((nat_t)({x}))"),
            (Ty::Scalar(_), O::Int) => format!("dv_int((int_t)({x}))"),
            (Ty::Bool, O::Bool) => format!("dv_bool({x})"),
            (Ty::Str, O::Text) => format!("dn_bridge_text(({x}).data,({x}).len)"),
            (Ty::Buffer(_) | Ty::Array(..) | Ty::Slice(..), O::Text) => {
                let (d, n) = self.storage(t, x);
                format!("dn_bridge_text({d},{n})")
            }
            (Ty::Buffer(_) | Ty::Array(..) | Ty::Slice(..), O::FloatVec | O::NatVec) => {
                let (d, n) = self.storage(t, x);
                let v = self.tmp();
                let i = self.tmp();
                let fl = *o == O::FloatVec;
                let field = if fl { "fv" } else { "nv" };
                let tag = if fl { "DV_FVEC" } else { "DV_NVEC" };
                writeln!(self.code,"DV *{v}=dv_new({tag});{v}->u.{field}.n={n};{v}->u.{field}.d=dv_alloc(dn_size({n},sizeof(*{v}->u.{field}.d)));for(size_t {i}=0;{i}<{n};{i}++){v}->u.{field}.d[{i}]={d}[{i}];").unwrap();
                v
            }
            (Ty::Record(n), _) => {
                let a = self.u.records.iter().find(|r| r.name == *n).unwrap();
                let b = record(self.ir, o).unwrap();
                let v = self.tmp();
                let names = a
                    .fields
                    .iter()
                    .map(|(n, _)| emit::q(n))
                    .collect::<Vec<_>>()
                    .join(",");
                let mut vals = vec![];
                for ((n, t), (_, q)) in a.fields.iter().zip(&b.fields) {
                    let value = self.pack(t, q, &format!("({x}).f_{n}"));
                    let tmp = self.tmp();
                    writeln!(self.code, "DV *{tmp}={value};").unwrap();
                    vals.push(tmp);
                }
                writeln!(self.code,"static const char *const {v}_names[]={{{names}}};DV *{v}_values[]={{{}}};DV *{v}=dv_record({}, {},{v}_names,{v}_values);",vals.join(","),emit::q(&b.name),a.fields.len()).unwrap();
                v
            }
            _ => unreachable!("checked bridge input"),
        }
    }
    fn unpack(&mut self, t: &Ty, o: &O, x: &str) -> String {
        let c = emit::c_type(t);
        let v = self.tmp();
        match (t, o) {
            (Ty::Scalar(s), O::Nat) => {
                let max = if s.bits() == 64 {
                    "UINT64_MAX".into()
                } else {
                    format!("((UINT64_C(1)<<{})-1)", s.bits())
                };
                writeln!(self.code,"nat_t {v}_n=dv_nat_value({x});if({v}_n>{max})dn_fail(\"ordinary bridge numeric narrowing\");{c} {v}=({c}){v}_n;").unwrap();
            }
            (Ty::Scalar(s), O::Int) => {
                let bits = s.bits();
                writeln!(self.code,"int_t {v}_n=dv_int_value({x});if({v}_n<-((int_t)1<<{})||{v}_n>((((int_t)1<<{})-1)))dn_fail(\"ordinary bridge numeric narrowing\");{c} {v}=({c}){v}_n;",bits-1,bits-1).unwrap();
            }
            (Ty::Scalar(_), O::Float) => {
                writeln!(self.code, "{c} {v}=({c})({x})->u.f;").unwrap();
            }
            (Ty::Bool, O::Bool) => {
                writeln!(self.code, "bool {v}=dv_bool_value({x});").unwrap();
            }
            (Ty::Buffer(q) | Ty::Array(q, _), O::Text | O::FloatVec | O::NatVec) => {
                let field = match o {
                    O::Text => "text",
                    O::FloatVec => "fv",
                    _ => "nv",
                };
                let data = if *o == O::Text { "s" } else { "d" };
                let n = format!("({x})->u.{field}.n");
                let d = format!("({x})->u.{field}.{data}");
                if let Ty::Array(_, len) = t {
                    writeln!(
                        self.code,
                        "if({n}!={len})dn_fail(\"ordinary bridge array length\");{c} {v}={{0}};"
                    )
                    .unwrap();
                } else {
                    writeln!(self.code,"{c} {v}={{0}};{v}.data=dn_allocate_aligned(dn_size({n},sizeof(*{v}.data)),_Alignof({}));{v}.len={v}.cap={n};",emit::c_type(q)).unwrap();
                }
                let i = self.tmp();
                writeln!(self.code, "for(size_t {i}=0;{i}<{n};{i}++){{").unwrap();
                if *o == O::NatVec {
                    let Ty::Scalar(s) = &**q else { unreachable!() };
                    let max = if s.bits() == 64 {
                        "UINT64_MAX".into()
                    } else {
                        format!("((UINT64_C(1)<<{})-1)", s.bits())
                    };
                    writeln!(
                        self.code,
                        "if({d}[{i}]>{max})dn_fail(\"ordinary bridge numeric narrowing\");"
                    )
                    .unwrap();
                }
                writeln!(self.code, "{v}.data[{i}]=({}){d}[{i}];}}", emit::c_type(q)).unwrap();
            }
            (Ty::Record(n), _) => {
                let a = self.u.records.iter().find(|r| r.name == *n).unwrap();
                let b = record(self.ir, o).unwrap();
                writeln!(self.code, "{c} {v}={{0}};").unwrap();
                for ((n, t), (_, q)) in a.fields.iter().zip(&b.fields) {
                    let child = self.unpack(t, q, &format!("dv_field({x},{})", emit::q(n)));
                    writeln!(self.code, "{v}.f_{n}={child};").unwrap();
                }
            }
            _ => unreachable!("checked bridge output"),
        }
        v
    }
}
pub(super) const C_HELPERS: &str = r#"
static DV *dn_bridge_text(const uint8_t *p,size_t n){
 for(size_t i=0;i<n;){uint32_t c=p[i++];size_t k=0;uint32_t min=0;if(c<128)continue;
 if(c>=0xc2&&c<=0xdf){c&=31;k=1;min=128;}else if(c>=0xe0&&c<=0xef){c&=15;k=2;min=2048;}else if(c>=0xf0&&c<=0xf4){c&=7;k=3;min=65536;}else dn_fail("ordinary bridge invalid UTF-8");
 if(k>n-i)dn_fail("ordinary bridge invalid UTF-8");while(k--){if((p[i]&0xc0)!=0x80)dn_fail("ordinary bridge invalid UTF-8");c=(c<<6)|(p[i++]&63);}if(c<min||c>0x10ffff||(c>=0xd800&&c<=0xdfff))dn_fail("ordinary bridge invalid UTF-8");
 }return dv_text((const char*)p,n);
}
"#;
pub(super) fn c_body(u: &Unit, ir: &IrProgram, f: &Function) -> String {
    let (id, old) = target(ir, f).unwrap();
    let mut cv=Convert{u,ir,code:String::from("dn_ui_guard guard;dn_ui_begin(&guard);dv_region region;dv_region_enter(&region);memset(G,0,sizeof G);dlm_program();\n"),serial:0};
    let mut args = vec![];
    for ((n, t), q) in f.params.iter().zip(&old.param_types) {
        let value = cv.pack(t, q, &format!("v_{n}"));
        let tmp = cv.tmp();
        writeln!(cv.code, "DV *{tmp}={value};").unwrap();
        args.push(tmp);
    }
    writeln!(
        cv.code,
        "DV *dn_bridge_args[{}]={{{}}};DV *dn_bridge_result=f{id}(NULL,dn_bridge_args);",
        args.len().max(1),
        if args.is_empty() {
            "NULL".into()
        } else {
            args.join(",")
        }
    )
    .unwrap();
    let result = cv.unpack(&f.ret, old.ret.as_ref().unwrap(), "dn_bridge_result");
    writeln!(
        cv.code,
        "dv_region_leave(&region,0);memset(G,0,sizeof G);dn_ui_end(&guard);return {result};"
    )
    .unwrap();
    cv.code
}
fn pack(v: &V, t: &Ty, o: &O, u: &Unit, ir: &IrProgram) -> Result<R, String> {
    if let (V::Ref(p), Ty::Ref(t, false)) = (v, t) {
        return pack(&p.borrow(), t, o, u, ir);
    }
    Ok(match (v, t, o) {
        (V::U(v), _, O::Nat) => R::NatExact(*v as u128),
        (V::I(v), _, O::Int) => R::IntExact(*v as i128),
        (V::F(v), _, O::Float) => R::Float(v.to_bits()),
        (V::F32(v), _, O::Float) => R::Float((f32::from_bits(*v) as f64).to_bits()),
        (V::Bool(v), _, O::Bool) => R::Bool(*v),
        (V::Text(v), _, O::Text) => R::Text(v.clone()),
        (V::Seq(_) | V::Slice(..), _, O::Text | O::FloatVec | O::NatVec) => {
            let items = sequence(v)?;
            match o {
                O::Text => {
                    let bytes = items
                        .iter()
                        .map(|v| match v {
                            V::U(n) => Ok(*n as u8),
                            _ => Err("bridge byte type"),
                        })
                        .collect::<Result<Vec<_>, _>>()?;
                    R::Text(String::from_utf8(bytes).map_err(|_| "ordinary bridge invalid UTF-8")?)
                }
                O::FloatVec => R::FloatVec(
                    items
                        .iter()
                        .map(|v| match v {
                            V::F(n) => Ok(n.to_bits()),
                            V::F32(n) => Ok((f32::from_bits(*n) as f64).to_bits()),
                            _ => Err("bridge float type"),
                        })
                        .collect::<Result<_, _>>()?,
                ),
                _ => R::NatVec(
                    items
                        .iter()
                        .map(|v| match v {
                            V::U(n) => Ok(*n as u128),
                            _ => Err("bridge Nat type"),
                        })
                        .collect::<Result<_, _>>()?,
                ),
            }
        }
        (V::Record(values), Ty::Record(n), _) => {
            let a = u.records.iter().find(|r| r.name == *n).unwrap();
            let b = record(ir, o).unwrap();
            let fields = a
                .fields
                .iter()
                .zip(&b.fields)
                .map(|((n, t), (_, q))| Ok((n.clone(), pack(&values[n].borrow(), t, q, u, ir)?)))
                .collect::<Result<_, String>>()?;
            R::Record {
                name: b.name.clone(),
                fields,
            }
        }
        _ => return Err("ordinary bridge input representation".into()),
    })
}
fn sequence(v: &V) -> Result<Vec<V>, String> {
    match v {
        V::Seq(p) => Ok(p.borrow().iter().map(|p| p.borrow().clone()).collect()),
        V::Slice(p, a, n) => Ok(p.borrow()[*a..*a + *n]
            .iter()
            .map(|p| p.borrow().clone())
            .collect()),
        _ => Err("bridge sequence".into()),
    }
}
fn unpack(v: R, t: &Ty, o: &O, u: &Unit, ir: &IrProgram) -> Result<V, String> {
    let narrow = || "ordinary bridge numeric narrowing".to_string();
    Ok(match (v, t, o) {
        (R::NatExact(n), Ty::Scalar(s), O::Nat) => {
            if n > ((1u128 << s.bits()) - 1) {
                return Err(narrow());
            }
            V::U(n as u64)
        }
        (R::IntExact(n), Ty::Scalar(s), O::Int) => {
            let bound = 1i128 << (s.bits() - 1);
            if n < -bound || n >= bound {
                return Err(narrow());
            }
            V::I(n as i64)
        }
        (R::Float(n), Ty::Scalar(Scalar::F32), _) => V::F32((f64::from_bits(n) as f32).to_bits()),
        (R::Float(n), _, _) => V::F(f64::from_bits(n)),
        (R::Bool(b), _, _) => V::Bool(b),
        (v, Ty::Buffer(q) | Ty::Array(q, _), _) => {
            let items = match v {
                R::Text(s) => s.bytes().map(|n| V::U(n as u64)).collect::<Vec<_>>(),
                R::FloatVec(a) => a
                    .into_iter()
                    .map(|n| {
                        if **q == Ty::Scalar(Scalar::F32) {
                            V::F32((f64::from_bits(n) as f32).to_bits())
                        } else {
                            V::F(f64::from_bits(n))
                        }
                    })
                    .collect(),
                R::NatVec(a) => a
                    .into_iter()
                    .map(|n| unpack(R::NatExact(n), q, &O::Nat, u, ir))
                    .collect::<Result<_, _>>()?,
                _ => return Err("bridge sequence result".into()),
            };
            if matches!(t,Ty::Array(_,n) if *n!=items.len()) {
                return Err("ordinary bridge array length".into());
            }
            V::Seq(Rc::new(RefCell::new(
                items
                    .into_iter()
                    .map(|v| Rc::new(RefCell::new(v)))
                    .collect(),
            )))
        }
        (R::Record { fields, .. }, Ty::Record(n), _) => {
            let a = u.records.iter().find(|r| r.name == *n).unwrap();
            let b = record(ir, o).unwrap();
            let mut values = BTreeMap::new();
            for ((n, t), (_, q)) in a.fields.iter().zip(&b.fields) {
                let v = fields
                    .iter()
                    .find(|(k, _)| k == n)
                    .ok_or("bridge record field")?
                    .1
                    .clone();
                values.insert(n.clone(), Rc::new(RefCell::new(unpack(v, t, q, u, ir)?)));
            }
            V::Record(values)
        }
        _ => return Err("ordinary bridge result representation".into()),
    })
}
pub(super) fn call(vm: &mut vm::Vm<'_>, f: &Function, args: Vec<V>) -> Result<V, String> {
    let ir = lower(&vm.mir.unit)
        .map_err(|d| d[0].message.clone())?
        .unwrap();
    let (_, old) = target(&ir, f).unwrap();
    let values = args
        .iter()
        .zip(&f.params)
        .zip(&old.param_types)
        .map(|((v, (_, t)), q)| pack(v, t, q, &vm.mir.unit, &ir))
        .collect::<Result<_, _>>()?;
    let name = old
        .name
        .rsplit_once('.')
        .ok_or("ordinary target requires Theory.function")?;
    let result = crate::runtime::Runtime::call_native_ordinary(
        vm.mir.unit.ordinary_program.as_ref().unwrap(),
        name.0,
        name.1,
        values,
        f.line,
    )
    .map_err(|e| e.message)?;
    unpack(result, &f.ret, old.ret.as_ref().unwrap(), &vm.mir.unit, &ir)
}
