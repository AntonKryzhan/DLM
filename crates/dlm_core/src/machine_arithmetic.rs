//! Conservative arithmetic obligations for the checked, executable IR.
//!
//! A symbolic ring proof ranges over mathematical numbers. It does not prove
//! that intermediate operations fit u128/i128. This independent interval pass
//! checks every function (including unused functions and lambdas) over its full
//! parameter domains. Unknown bounds leave an obligation open; no samples,
//! axioms or symbolic equalities can discharge a machine-range obligation.

use crate::compile::{IrExpr, IrOp, IrProgram};
use crate::ordinary::OrdinaryType;
use crate::{lower_program, CheckPolicy, Diagnostic, Program};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ArithmeticObligation {
    pub function: String,
    pub expression: String,
    pub proved: bool,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ArithmeticSafetyReport {
    pub obligations: Vec<ArithmeticObligation>,
}

impl ArithmeticSafetyReport {
    pub fn verified(&self) -> bool {
        self.obligations.iter().all(|o| o.proved)
    }

    pub fn render(&self) -> String {
        let proved = self.obligations.iter().filter(|o| o.proved).count();
        let mut out = format!("machine arithmetic: {proved}/{} obligations proved (u128 Nat / i128 Int)\n", self.obligations.len());
        for o in self.obligations.iter().filter(|o| !o.proved) {
            out.push_str(&format!("  open: {}: {}\n", o.function, o.expression));
        }
        out
    }
}

#[derive(Debug, Clone, Copy)]
enum Range {
    Nat(u128, u128),
    Int(i128, i128),
    Bool(Option<bool>),
    Unknown,
}

impl Range {
    fn ty(ty: &str) -> Self {
        match ty {
            "Nat" => Self::Nat(0, u128::MAX),
            "Int" => Self::Int(i128::MIN, i128::MAX),
            "Bool" => Self::Bool(None),
            _ => Self::Unknown,
        }
    }

    /// v0.87: the full range of a checked type.
    fn of(ty: &Option<OrdinaryType>) -> Self {
        match ty {
            Some(OrdinaryType::Nat) => Self::ty("Nat"),
            Some(OrdinaryType::Int) => Self::ty("Int"),
            Some(OrdinaryType::Bool) => Self::ty("Bool"),
            _ => Self::Unknown,
        }
    }

    fn union(self, other: Self) -> Self {
        match (self, other) {
            (Self::Nat(a, b), Self::Nat(c, d)) => Self::Nat(a.min(c), b.max(d)),
            (Self::Int(a, b), Self::Int(c, d)) => Self::Int(a.min(c), b.max(d)),
            (Self::Bool(a), Self::Bool(b)) => Self::Bool(if a == b { a } else { None }),
            _ => Self::Unknown,
        }
    }
}

struct Analysis<'a> {
    ir: &'a IrProgram,
    report: ArithmeticSafetyReport,
    function: String,
}

impl Analysis<'_> {
    fn obligation(&mut self, expr: &IrExpr, proved: bool) {
        self.report.obligations.push(ArithmeticObligation {
            function: self.function.clone(), expression: expr.to_string(), proved,
        });
    }

    fn binary(&mut self, name: &str, x: Range, y: Range, expr: &IrExpr) -> Range {
        use Range::*;
        // v0.87: an operand without a known interval has the full range of
        // its checked type (operator operands are typed in the IR).
        let operands: Vec<&IrExpr> = match &expr.op {
            IrOp::Add(a, b) | IrOp::Pow(a, b) => vec![a, b],
            IrOp::Builtin { args, .. } => args.iter().collect(),
            _ => Vec::new(),
        };
        let widen = |range: Range, index: usize| match (range, operands.get(index)) {
            (Unknown, Some(operand)) => Range::of(&operand.ty),
            (range, _) => range,
        };
        let (x, y) = (widen(x, 0), widen(y, 1));
        let range = match (name, x, y) {
            ("add", Nat(a, b), Nat(c, d)) => a.checked_add(c).zip(b.checked_add(d)).map(|(a, b)| Nat(a, b)),
            ("mul", Nat(a, b), Nat(c, d)) => a.checked_mul(c).zip(b.checked_mul(d)).map(|(a, b)| Nat(a, b)),
            ("add", Int(a, b), Int(c, d)) => a.checked_add(c).zip(b.checked_add(d)).map(|(a, b)| Int(a, b)),
            ("__sub", Int(a, b), Int(c, d)) => a.checked_sub(d).zip(b.checked_sub(c)).map(|(a, b)| Int(a, b)),
            ("mul", Int(a, b), Int(c, d)) => {
                let products: Option<Vec<_>> = [(a, c), (a, d), (b, c), (b, d)].into_iter().map(|(a, b)| a.checked_mul(b)).collect();
                products.map(|p| Int(*p.iter().min().unwrap(), *p.iter().max().unwrap()))
            }
            ("div" | "mod", Nat(..), Nat(..)) => Some(Range::ty("Nat")),
            ("div" | "mod", Int(a, _), Int(c, d)) if a > i128::MIN || c > -1 || d < -1 => Some(Range::ty("Int")),
            ("pow", Nat(_, b), Nat(c, d)) if d <= u32::MAX as u128 => {
                // 0^0=1, so include 1 when the exponent may be zero.
                b.checked_pow(d as u32).map(|max| Nat(0, if c == 0 { max.max(1) } else { max }))
            }
            _ => None,
        };
        self.obligation(expr, range.is_some());
        range.unwrap_or(match (x, y) { (Nat(..), _) => Range::ty("Nat"), (Int(..), _) => Range::ty("Int"), _ => Unknown })
    }

    // Refinement uses only executable comparisons against constants. No
    // theorem/axiom is assumed, and branches that cannot run are skipped.
    fn refine(condition: &IrExpr, slots: &mut [Range], truth: bool) -> bool {
        let IrOp::Gt(lhs, rhs) = &condition.op else { return true };
        match (&lhs.op, &rhs.op) {
            (IrOp::Local(s), IrOp::Nat(k)) => {
                let (lo, hi) = match slots[*s] { Range::Nat(lo, hi) => (lo, hi), _ => (0, u128::MAX) };
                if truth {
                    let Some(k) = k.checked_add(1) else { return false };
                    if lo.max(k) > hi { return false; }
                    slots[*s] = Range::Nat(lo.max(k), hi);
                } else {
                    if lo > hi.min(*k) { return false; }
                    slots[*s] = Range::Nat(lo, hi.min(*k));
                }
            }
            (IrOp::Local(s), IrOp::Int(k)) => {
                let (lo, hi) = match slots[*s] { Range::Int(lo, hi) => (lo, hi), _ => (i128::MIN, i128::MAX) };
                if truth {
                    let Some(k) = k.checked_add(1) else { return false };
                    if lo.max(k) > hi { return false; }
                    slots[*s] = Range::Int(lo.max(k), hi);
                } else {
                    if lo > hi.min(*k) { return false; }
                    slots[*s] = Range::Int(lo, hi.min(*k));
                }
            }
            _ => {}
        }
        true
    }

    fn expr(&mut self, expr: &IrExpr, slots: &mut [Range]) -> Range {
        use IrOp::*;
        match &expr.op {
            Nat(n) => Range::Nat(*n, *n),
            Int(i) => Range::Int(*i, *i),
            Bool(b) => Range::Bool(Some(*b)),
            Local(s) => match slots[*s] {
                Range::Unknown => Range::of(&expr.ty),
                range => range,
            },
            Add(a, b) | Pow(a, b) => {
                let x = self.expr(a, slots);
                let y = self.expr(b, slots);
                self.binary(if matches!(expr.op, Add(..)) { "add" } else { "pow" }, x, y, expr)
            }
            Gt(a, b) => {
                let x = self.expr(a, slots);
                let y = self.expr(b, slots);
                Range::Bool(match (x, y) {
                    (Range::Nat(a, b), Range::Nat(c, d)) => if a > d { Some(true) } else if b <= c { Some(false) } else { None },
                    (Range::Int(a, b), Range::Int(c, d)) => if a > d { Some(true) } else if b <= c { Some(false) } else { None },
                    _ => None,
                })
            }
            Let { slot, value, body } => {
                slots[*slot] = self.expr(value, slots);
                self.expr(body, slots)
            }
            If(c, a, b) => {
                let condition = self.expr(c, slots);
                let mut left = slots.to_vec();
                let mut right = slots.to_vec();
                let a = if !matches!(condition, Range::Bool(Some(false))) && Self::refine(c, &mut left, true) { Some(self.expr(a, &mut left)) } else { None };
                let b = if !matches!(condition, Range::Bool(Some(true))) && Self::refine(c, &mut right, false) { Some(self.expr(b, &mut right)) } else { None };
                match (a, b) { (Some(a), Some(b)) => a.union(b), (Some(a), _) | (_, Some(a)) => a, _ => Range::Unknown }
            }
            Builtin { name, args } => {
                let values: Vec<_> = args.iter().map(|a| self.expr(a, slots)).collect();
                match name.as_str() {
                    "mul" | "div" | "mod" | "__sub" => {
                        let x = values[0]; let y = values[1];
                        // Subtraction has only an Int signature.
                        self.binary(name, x, y, expr)
                    }
                    "__neg" => {
                        let range = match values[0] { Range::Int(a, b) => b.checked_neg().zip(a.checked_neg()).map(|(a, b)| Range::Int(a, b)), _ => None };
                        self.obligation(expr, range.is_some());
                        range.unwrap_or(Range::ty("Int"))
                    }
                    "int" => {
                        let range = match values[0] { Range::Nat(a, b) if b <= i128::MAX as u128 => Some(Range::Int(a as i128, b as i128)), _ => None };
                        self.obligation(expr, range.is_some());
                        range.unwrap_or(Range::ty("Int"))
                    }
                    "abs" => Range::Nat(0, 1u128 << 127),
                    "pred" => match values[0] { Range::Nat(a, b) => Range::Nat(a.saturating_sub(1), b.saturating_sub(1)), _ => Range::ty("Nat") },
                    "monus" => match (values[0], values[1]) { (Range::Nat(a, b), Range::Nat(c, d)) => Range::Nat(a.saturating_sub(d), b.saturating_sub(c)), _ => Range::ty("Nat") },
                    "length" | "text_length" => Range::Nat(0, usize::MAX as u128),
                    // v0.102: buffer lengths are bounded by the runtime limit.
                    "buf_len" | "buf_free" | "tbuf_len" => Range::Nat(0, crate::runtime::BYTE_BUFFER_MAX as u128),
                    "bn_bits" => Range::Nat(0, crate::dense_nat::MAX_BITS as u128),
                    "bn_limbs" => Range::Nat(0, crate::dense_nat::MAX_LIMBS as u128),
                    "bn_cmp" => Range::Int(-1, 1),
                    "km_len" | "tm_len" => Range::Nat(0, crate::maps::MAX as u128),
                    "nv_len" => Range::Nat(0, crate::graph::VEC_MAX as u128),
                    "fv_len" => Range::Nat(0, crate::runtime::FLOAT_VEC_MAX as u128),
                    "fl_bits" => Range::Nat(0, u64::MAX as u128),
                    "__release" => values[0],
                    "read_nat" => Range::ty("Nat"),
                    _ => Range::Unknown,
                }
            }
            Call { function, args } => {
                for arg in args { self.expr(arg, slots); }
                // Function results use the full range of their checked type.
                Range::of(&self.ir.functions[*function].ret)
            }
            Apply { callee, args } => {
                self.expr(callee, slots);
                for arg in args { self.expr(arg, slots); }
                Range::Unknown
            }
            And(a, b) | Or(a, b) | UnwrapOr(a, b) => {
                self.expr(a, slots);
                // Conservative: include all possible fallback executions.
                self.expr(b, slots);
                Range::Unknown
            }
            Case(s, a, b) => { self.expr(s, slots); self.expr(a, slots); self.expr(b, slots); Range::Unknown }
            Inject { value, .. } | Field { record: value, .. } => { self.expr(value, slots); Range::Unknown }
            Record { fields, .. } => { for (_, value) in fields { self.expr(value, slots); } Range::Unknown }
            Text(_) | Global(_) | FunctionValue(_) | Closure { .. } | Empty(_) => Range::Unknown,
        }
    }
}

/// Check mathematical proofs first, then independently discharge arithmetic
/// range obligations for the very same program. The report is conservative:
/// `verified()` means no executable arithmetic operation can overflow, for
/// any value in the declared input domains. It does not certify memory bounds,
/// stack bounds, valid input text, or successful I/O. Runtime guards stay on.
pub fn check_machine_arithmetic(program: &Program, policy: CheckPolicy) -> Result<ArithmeticSafetyReport, Vec<Diagnostic>> {
    let ir = lower_program(program, policy)?;
    Ok(check_machine_arithmetic_ir(&ir))
}

/// v0.87: the same analysis over an already lowered (typed) IR program.
/// Parameters, including lambda parameters, range over their checked types;
/// captured values are unknown.
pub fn check_machine_arithmetic_ir(ir: &IrProgram) -> ArithmeticSafetyReport {
    let mut analysis = Analysis { ir, report: ArithmeticSafetyReport::default(), function: String::new() };
    for f in &ir.functions {
        analysis.function = f.name.clone();
        let mut slots = vec![Range::Unknown; f.slots];
        for (slot, ty) in slots.iter_mut().skip(f.captures).zip(&f.param_types) {
            *slot = Range::of(&Some(ty.clone()));
        }
        analysis.expr(&f.body, &mut slots);
    }
    analysis.report
}
