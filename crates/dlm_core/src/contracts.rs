//! v0.87 — function contracts with independent properties (law 31).
//!
//! `C_f = (inputs, output, effects, P, Q, termination, failures)`: purity,
//! termination, machine-arithmetic definedness, stack behaviour and
//! mathematical specification are established by different mechanisms and
//! are reported separately. None implies another: a proved termination does
//! not close an overflow obligation, a pure function may overflow, and a
//! theorem about a function is a statement over mathematical numbers.
//! Preconditions/postconditions (`P`, `Q`) are not part of the surface
//! language yet and are reported as `none`.

use std::collections::BTreeMap;
use std::fmt;

use crate::ast::{Expr, ExprKind, ModuleItem, TheoryItem};
use crate::compile::{IrExpr, IrOp, IrProgram};
use crate::diagnostics::Diagnostic;
use crate::ordinary::OrdinaryType;
use crate::policy::CheckPolicy;
use crate::program::Program;

/// Whether every executable arithmetic operation of a function stays in the
/// u128/i128 range for all inputs of its declared types.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum MachineStatus {
    /// The function performs no checked arithmetic.
    NoArithmetic,
    /// All obligations are discharged by the interval analysis.
    Closed { obligations: usize },
    /// Some obligations are open: the runtime checks stay and may fail.
    Open { open: Vec<String>, obligations: usize },
}

/// How the function uses the native stack in compiled code.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StackStatus {
    /// Not recursive.
    NoRecursion,
    /// Recursive only through self calls in tail position, which both
    /// backends compile to a loop: constant stack.
    TailLoop,
    /// Non-tail or mutual recursion: depth bounded by the native stack
    /// (1 GiB reserved). Exhaustion is a defined runtime failure policy, not
    /// a proved bound.
    NativeRecursion,
}

/// A failure the function can produce at run time.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum FailureKind {
    ArithmeticOverflow,
    StackExhaustion,
    InvalidInput,
}

impl fmt::Display for FailureKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            FailureKind::ArithmeticOverflow => "arithmetic-overflow",
            FailureKind::StackExhaustion => "stack-exhaustion",
            FailureKind::InvalidInput => "invalid-input",
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FunctionContractSummary {
    /// `Theory.name`.
    pub function: String,
    /// `module::Theory.name`.
    pub symbol: String,
    pub inputs: Vec<OrdinaryType>,
    pub output: Option<OrdinaryType>,
    /// Effects (`kind@boundary`); empty means pure.
    pub effects: Vec<String>,
    /// As established by the checker: `non-recursive`, a recursion measure,
    /// or `mutual(..)`.
    pub termination: String,
    pub machine: MachineStatus,
    pub stack: StackStatus,
    pub scalar: bool,
    pub failures: Vec<FailureKind>,
    /// Statements of the root module that mention the function, as
    /// `kind name` (`theorem t`, `axiom a`, `conjecture c`): properties over
    /// mathematical numbers.
    pub theorems: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ContractReport {
    pub module: String,
    pub functions: Vec<FunctionContractSummary>,
}

fn calls_in(expr: &IrExpr, id: usize, tail: bool, out: &mut (usize, usize)) {
    match &expr.op {
        IrOp::Call { function, args } => {
            if *function == id {
                if tail {
                    out.0 += 1;
                } else {
                    out.1 += 1;
                }
            }
            args.iter().for_each(|a| calls_in(a, id, false, out));
        }
        IrOp::Let { value, body, .. } => {
            calls_in(value, id, false, out);
            calls_in(body, id, tail, out);
        }
        IrOp::If(c, a, b) => {
            calls_in(c, id, false, out);
            calls_in(a, id, tail, out);
            calls_in(b, id, tail, out);
        }
        _ => crate::compile::for_each_child(expr, &mut |child| calls_in(child, id, false, out)),
    }
}

fn uses_builtin(expr: &IrExpr, names: &[&str]) -> bool {
    if let IrOp::Builtin { name, .. } = &expr.op {
        if names.contains(&name.as_str()) {
            return true;
        }
    }
    let mut found = false;
    crate::compile::for_each_child(expr, &mut |child| found |= uses_builtin(child, names));
    found
}

fn mentions(expr: &Expr, name: &str) -> bool {
    match &expr.kind {
        ExprKind::Call { name: called, args } => called == name || args.iter().any(|a| mentions(a, name)),
        ExprKind::Ident(ident) => ident == name,
        ExprKind::Add { lhs, rhs } | ExprKind::CompareGt { lhs, rhs } => mentions(lhs, name) || mentions(rhs, name),
        ExprKind::Power { base, exp } => mentions(base, name) || mentions(exp, name),
        ExprKind::Lambda { body, .. } => mentions(body, name),
        ExprKind::Let { value, body, .. } => mentions(value, name) || mentions(body, name),
        _ => false,
    }
}

/// Contracts of every declared function of the root module's program,
/// computed from the typed IR (checked types, effects and termination), the
/// machine-range analysis and the backend's code shape.
pub fn function_contracts(program: &Program, policy: CheckPolicy) -> Result<ContractReport, Vec<Diagnostic>> {
    let ir = crate::compile::lower_program(program, policy)?;
    Ok(contracts_of(program, &ir))
}

pub fn contracts_of(program: &Program, ir: &IrProgram) -> ContractReport {
    let arithmetic = crate::machine_arithmetic::check_machine_arithmetic_ir(ir);
    let mut obligations: BTreeMap<&str, (usize, Vec<String>)> = BTreeMap::new();
    for o in &arithmetic.obligations {
        let entry = obligations.entry(o.function.as_str()).or_default();
        entry.0 += 1;
        if !o.proved {
            entry.1.push(o.expression.clone());
        }
    }
    let scalar = crate::backend_c::scalar_functions(ir);
    let mut theorems: BTreeMap<String, Vec<String>> = BTreeMap::new();
    for item in &program.root().items {
        let ModuleItem::Theory(theory) = item else { continue };
        for item in &theory.items {
            let TheoryItem::Statement(statement) = item else { continue };
            for item in &theory.items {
                if let TheoryItem::Fn(decl) = item {
                    if mentions(&statement.body, &decl.name) {
                        theorems.entry(format!("{}.{}", theory.name, decl.name)).or_default().push(format!("{} {}", format!("{:?}", statement.kind).to_lowercase(), statement.name));
                    }
                }
            }
        }
    }
    let mut functions = Vec::new();
    for (id, f) in ir.functions.iter().enumerate() {
        if f.is_lambda || f.symbol.is_empty() {
            continue;
        }
        let (count, open) = obligations.get(f.name.as_str()).cloned().unwrap_or_default();
        let machine = if count == 0 {
            MachineStatus::NoArithmetic
        } else if open.is_empty() {
            MachineStatus::Closed { obligations: count }
        } else {
            MachineStatus::Open { open, obligations: count }
        };
        let mut calls = (0usize, 0usize);
        calls_in(&f.body, id, true, &mut calls);
        let mutual = f.termination.starts_with("mutual(");
        let stack = if mutual || calls.1 > 0 {
            StackStatus::NativeRecursion
        } else if calls.0 > 0 {
            StackStatus::TailLoop
        } else {
            StackStatus::NoRecursion
        };
        let mut failures = Vec::new();
        if matches!(machine, MachineStatus::Open { .. }) {
            failures.push(FailureKind::ArithmeticOverflow);
        }
        if stack == StackStatus::NativeRecursion {
            failures.push(FailureKind::StackExhaustion);
        }
        if uses_builtin(&f.body, &["read_nat"]) {
            failures.push(FailureKind::InvalidInput);
        }
        functions.push(FunctionContractSummary {
            function: f.name.clone(),
            symbol: f.symbol.clone(),
            inputs: f.param_types.clone(),
            output: f.ret.clone(),
            effects: f.effects.clone(),
            termination: f.termination.clone(),
            machine,
            stack,
            scalar: scalar.contains(&id),
            failures,
            theorems: theorems.get(&f.name).cloned().unwrap_or_default(),
        });
    }
    ContractReport { module: ir.module.clone(), functions }
}

impl ContractReport {
    pub fn render(&self) -> String {
        let mut out = format!("DLM-CONTRACTS v1\nmodule: {}\n", self.module);
        for c in &self.functions {
            out.push_str(&format!(
                "fn {} ({}) -> {}\n",
                c.function,
                c.inputs.iter().map(OrdinaryType::canonical).collect::<Vec<_>>().join(", "),
                c.output.as_ref().map_or_else(|| "?".to_string(), OrdinaryType::canonical)
            ));
            out.push_str(&format!("  effects: {}\n", if c.effects.is_empty() { "none (pure)".to_string() } else { c.effects.join(", ") }));
            out.push_str(&format!(
                "  termination: {}\n",
                match c.termination.as_str() {
                    "non-recursive" => "non-recursive".to_string(),
                    "tail_loop" => "not established (tail loop, may run forever)".to_string(),
                    t if t.starts_with("mutual(") => format!("proved for the mutual group {}", &t["mutual".len()..]),
                    t => format!("proved ({t})"),
                }
            ));
            out.push_str(&format!(
                "  machine arithmetic: {}\n",
                match &c.machine {
                    MachineStatus::NoArithmetic => "no checked arithmetic".to_string(),
                    MachineStatus::Closed { obligations } => format!("closed ({obligations} obligation(s) proved in u128/i128)"),
                    MachineStatus::Open { open, obligations } => {
                        format!("open: {} of {obligations} obligation(s) [{}]; runtime checks stay", open.len(), open.join("; "))
                    }
                }
            ));
            out.push_str(&format!(
                "  stack: {}{}\n",
                match c.stack {
                    StackStatus::NoRecursion => "no recursion",
                    StackStatus::TailLoop => "self tail calls compiled to a loop (constant stack)",
                    StackStatus::NativeRecursion => "native recursion (depth bounded by the 1 GiB stack)",
                },
                if c.scalar { "; native scalar entry point" } else { "" }
            ));
            out.push_str(&format!(
                "  failures: {}\n",
                if c.failures.is_empty() { "none".to_string() } else { c.failures.iter().map(ToString::to_string).collect::<Vec<_>>().join(", ") }
            ));
            out.push_str("  pre/postconditions: none declared\n");
            out.push_str(&format!(
                "  specification (mathematical): {}\n",
                if c.theorems.is_empty() { "none".to_string() } else { c.theorems.join(", ") }
            ));
        }
        let count = |pred: &dyn Fn(&FunctionContractSummary) -> bool| self.functions.iter().filter(|c| pred(c)).count();
        out.push_str(&format!(
            "summary: functions {}  pure {}  termination-proved {}  machine-closed {}  machine-open {}  constant-stack {}  with-theorems {}\n",
            self.functions.len(),
            count(&|c| c.effects.is_empty()),
            self.functions.len(),
            count(&|c| !matches!(c.machine, MachineStatus::Open { .. })),
            count(&|c| matches!(c.machine, MachineStatus::Open { .. })),
            count(&|c| c.stack != StackStatus::NativeRecursion),
            count(&|c| !c.theorems.is_empty()),
        ));
        out
    }
}
