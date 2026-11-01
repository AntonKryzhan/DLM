//! Checked scalar specialization of monomorphic, allocation-free functions.
//! v0.87: the kinds come from the typed IR, i.e. from the types the checker
//! established (Typed Core); nothing is reconstructed here. An aggregate,
//! capture, I/O or indirect call keeps the uniform ABI. The closure ABI is
//! unchanged.

use std::collections::BTreeMap;
use crate::compile::{IrExpr, IrOp, IrProgram};
use crate::ordinary::OrdinaryType;

#[derive(Clone, Copy, PartialEq, Eq)]
enum Kind { Nat, Int, Bool }
impl Kind {
    fn c(self) -> &'static str { match self { Self::Nat => "nat_t", Self::Int => "int_t", Self::Bool => "int" } }
    fn box_fn(self) -> &'static str { match self { Self::Nat => "dv_nat", Self::Int => "dv_int", Self::Bool => "dv_bool" } }
    fn unbox(self, arg: &str) -> String {
        match self {
            Self::Nat => format!("dv_nat_of({arg}, \"parameter\")"),
            Self::Int => format!("dv_int_of({arg}, \"parameter\")"),
            Self::Bool => format!("dv_truth({arg})"),
        }
    }
}

/// Kind of a checked type, if it is scalar.
fn kind_of(ty: &Option<OrdinaryType>) -> Option<Kind> {
    match ty {
        Some(OrdinaryType::Nat) => Some(Kind::Nat),
        Some(OrdinaryType::Int) => Some(Kind::Int),
        Some(OrdinaryType::Bool) => Some(Kind::Bool),
        _ => None,
    }
}

const SCALAR_BUILTINS: &[&str] = &["mul", "div", "mod", "__sub", "__neg", "nat_eq", "int_eq", "int", "abs", "not", "monus", "pred"];

fn supported(e: &IrExpr, candidates: &[bool]) -> bool {
    use IrOp::*;
    if kind_of(&e.ty).is_none() {
        return false;
    }
    match &e.op {
        Nat(_) | Int(_) | Bool(_) | Local(_) => true,
        Add(a, b) | Pow(a, b) | Gt(a, b) | And(a, b) | Or(a, b) => supported(a, candidates) && supported(b, candidates),
        If(c, a, b) => supported(c, candidates) && supported(a, candidates) && supported(b, candidates),
        Let { value, body, .. } => supported(value, candidates) && supported(body, candidates),
        Call { function, args } => candidates[*function] && args.iter().all(|a| supported(a, candidates)),
        Builtin { name, args } => SCALAR_BUILTINS.contains(&name.as_str()) && args.iter().all(|a| supported(a, candidates)),
        _ => false,
    }
}

struct Emitter {
    temps: Vec<Kind>,
}
impl Emitter {
    fn sequence(&mut self, args: &[&IrExpr]) -> (String, Vec<String>) {
        let temps: Vec<_> = args.iter().map(|a| { let i = self.temps.len(); self.temps.push(kind_of(&a.ty).unwrap()); format!("t{i}") }).collect();
        let prefix = args.iter().zip(&temps).map(|(a, t)| format!("{t} = {}, ", self.expr(a))).collect::<String>();
        (prefix, temps)
    }
    fn expr(&mut self, e: &IrExpr) -> String {
        use IrOp::*;
        let kind = kind_of(&e.ty);
        match &e.op {
            Nat(n) => format!("(((nat_t){}ULL << 64) | {}ULL)", (n >> 64) as u64, *n as u64),
            Int(i) => format!("((int_t)(((nat_t){}ULL << 64) | {}ULL))", (*i as u128 >> 64) as u64, *i as u64),
            Bool(b) => u8::from(*b).to_string(), Local(s) => format!("s{s}"),
            If(c, a, b) => format!("({} ? {} : {})", self.expr(c), self.expr(a), self.expr(b)),
            And(a, b) => format!("({} && {})", self.expr(a), self.expr(b)),
            Or(a, b) => format!("({} || {})", self.expr(a), self.expr(b)),
            Let { slot, value, body } => format!("(s{slot} = {}, {})", self.expr(value), self.expr(body)),
            Add(a, b) | Pow(a, b) | Gt(a, b) => {
                let (prefix, t) = self.sequence(&[a, b]);
                if matches!(e.op, Gt(..)) { return format!("({prefix}{} > {})", t[0], t[1]); }
                let op = if matches!(e.op, Pow(..)) { "dn_pow" } else if kind == Some(Kind::Int) { "di_add" } else { "dn_add" };
                format!("({prefix}{op}({}, {}))", t[0], t[1])
            }
            Call { function, args } => {
                let (prefix, t) = self.sequence(&args.iter().collect::<Vec<_>>());
                format!("({prefix}n{function}({}))", t.join(", "))
            }
            Builtin { name, args } => {
                let (prefix, t) = self.sequence(&args.iter().collect::<Vec<_>>());
                let int = kind == Some(Kind::Int);
                let op = match name.as_str() {
                    "__sub" => "di_sub", "__neg" => "di_neg", "int" => "di_from_nat", "abs" => "di_abs",
                    "pred" => "dn_pred", "monus" => "dn_monus",
                    "mul" => if int { "di_mul" } else { "dn_mul" },
                    "div" => if int { "di_div" } else { "dn_div" },
                    "mod" => if int { "di_mod" } else { "dn_mod" },
                    "nat_eq" | "int_eq" => return format!("({prefix}{} == {})", t[0], t[1]),
                    "not" => return format!("({prefix}!{})", t[0]),
                    _ => unreachable!(),
                };
                format!("({prefix}{op}({}))", t.join(", "))
            }
            _ => unreachable!("scalar candidate was checked"),
        }
    }

    /// v0.87: statements for a body whose self calls in tail position become
    /// a loop (guaranteed elimination, as in the uniform backend).
    fn tail(&mut self, e: &IrExpr, id: usize, params: usize) -> String {
        match &e.op {
            IrOp::Call { function, args } if *function == id => {
                let (prefix, t) = self.sequence(&args.iter().collect::<Vec<_>>());
                let mut out = if prefix.is_empty() { String::new() } else { format!("        (void)({prefix}0);\n") };
                for (p, temp) in t.iter().enumerate().take(params) {
                    out.push_str(&format!("        s{p} = {temp};\n"));
                }
                out.push_str("        continue;\n");
                out
            }
            IrOp::Let { slot, value, body } => format!("        s{slot} = {};\n{}", self.expr(value), self.tail(body, id, params)),
            IrOp::If(c, a, b) => {
                let c = self.expr(c);
                let (a, b) = (self.tail(a, id, params), self.tail(b, id, params));
                format!("        if ({c}) {{\n{a}        }} else {{\n{b}        }}\n")
            }
            _ => format!("        return {};\n", self.expr(e)),
        }
    }
}

/// Whether `e` calls function `id` in tail position.
pub(super) fn has_tail_self_call(e: &IrExpr, id: usize) -> bool {
    match &e.op {
        IrOp::Call { function, .. } => *function == id,
        IrOp::Let { body, .. } => has_tail_self_call(body, id),
        IrOp::If(_, a, b) => has_tail_self_call(a, id) || has_tail_self_call(b, id),
        _ => false,
    }
}

pub(super) struct ScalarBackend {
    pub declarations: String,
    pub definitions: String,
    pub wrappers: BTreeMap<usize, String>,
}
impl ScalarBackend {
    pub fn new(program: &IrProgram) -> Self {
        let scalar_slots = |f: &crate::compile::IrFunction| f.slot_types.iter().all(|t| kind_of(t).is_some());
        let mut candidates: Vec<_> = program
            .functions
            .iter()
            .map(|f| !f.is_lambda && f.captures == 0 && kind_of(&f.ret).is_some() && scalar_slots(f))
            .collect();
        loop {
            let next: Vec<_> = program.functions.iter().enumerate().map(|(i, f)| candidates[i] && supported(&f.body, &candidates)).collect();
            if next == candidates { break; } candidates = next;
        }
        let mut out = Self { declarations: String::new(), definitions: String::new(), wrappers: BTreeMap::new() };
        for (i, f) in program.functions.iter().enumerate().filter(|(i, _)| candidates[*i]) {
            let ret = kind_of(&f.ret).unwrap();
            let slot_kind = |s: usize| kind_of(&f.slot_types[s]).unwrap();
            let params = (0..f.params.len()).map(|p| format!("{} a{p}", slot_kind(p).c())).collect::<Vec<_>>().join(", ");
            let signature = format!("static {} n{i}({})", ret.c(), if params.is_empty() { "void" } else { &params });
            out.declarations.push_str(&format!("{signature};\n"));
            let mut emitter = Emitter { temps: Vec::new() };
            let looped = has_tail_self_call(&f.body, i);
            let body = if looped { emitter.tail(&f.body, i, f.params.len()) } else { emitter.expr(&f.body) };
            let mut code = format!("/* scalar {} */\n{signature} {{\n    DV_STACK_CHECK();\n", f.name);
            for s in 0..f.slots { code.push_str(&format!("    {} s{s};\n", slot_kind(s).c())); }
            for (t, kind) in emitter.temps.iter().enumerate() { code.push_str(&format!("    {} t{t};\n", kind.c())); }
            for p in 0..f.params.len() { code.push_str(&format!("    s{p} = a{p};\n")); }
            if looped {
                code.push_str(&format!("    for (;;) {{\n{body}    }}\n}}\n\n"));
            } else {
                code.push_str(&format!("    return {body};\n}}\n\n"));
            }
            out.definitions.push_str(&code);
            let args = (0..f.params.len()).map(|p| slot_kind(p).unbox(&format!("args[{p}]"))).collect::<Vec<_>>().join(", ");
            out.wrappers.insert(i, format!("static DV *f{i}(DV **env, DV **args) {{\n    (void)env; (void)args;\n    return {}(n{i}({args}));\n}}\n\n", ret.box_fn()));
        }
        out
    }
}
