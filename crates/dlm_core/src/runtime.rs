use std::collections::BTreeMap;

use crate::ast::*;
use crate::diagnostics::{Diagnostic, DiagnosticKind};

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RuntimeValue {
    NatExact(u128),
    NatSymbolic(String),
    Bool(bool),
    Text(String),
    Infinity { mode: String, repr: String },
    Universe { level: u8 },
    Set { of_level: u8, lives_in: u8 },
    Class { of_level: u8 },
    Language(String),
    Encoding(String),
    MetaLevel(u8),
    DefinableNat { language: String, encoding: String, theory: String, bound: u128, meta_level: u8 },
    Proposition(String),
    Provable { theory: String, proposition: String },
    ConsistencyClaim { theory: String },
    ReflectionClaim { theory: String, proposition: String },
    SelfReferenceClaim { proposition: String },
    ProofTerm(String),
    StaticProof(String),
    Bytes(Vec<u8>),
    Term(String),
    RuntimeWitness(String),
    Node { arch: String, cores: u128, memory_mib: u128 },
    GpuDevice { backend: String, memory_mib: u128 },
    GpuPool { devices: Vec<(String, u128)> },
    VirtualCluster { nodes: Vec<(String, u128, u128)> },
    DistributedMemory { memory_mib: u128 },
    DistributedGpuMemory { memory_mib: u128 },
    GpuValue { inner: Box<RuntimeValue>, memory_mib: u128 },
    GpuKernel { inner: Box<RuntimeValue> },
    MemoryCheckpoint { memory_mib: u128 },
    RemoteCheckpoint { arch: String, inner: Box<RuntimeValue> },
    PortableCode { inner: Box<RuntimeValue> },
    Remote { arch: String, inner: Box<RuntimeValue> },
}

#[derive(Debug, Clone, Default)]
pub struct RunReport {
    pub module_name: String,
    pub theory_count: usize,
    pub values_evaluated: usize,
    pub output: Vec<String>,
}

#[derive(Default)]
pub struct Runtime {
    scopes: BTreeMap<String, BTreeMap<String, RuntimeValue>>,
    output: Vec<String>,
    stdin: String,
    bridges: Vec<BridgeDecl>,
}

impl Runtime {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn with_stdin(stdin: impl Into<String>) -> Self {
        Self {
            stdin: stdin.into(),
            ..Self::default()
        }
    }

    pub fn run_module(mut self, module: &Module) -> Result<RunReport, Diagnostic> {
        self.bridges = module.items.iter().filter_map(|item| match item {
            ModuleItem::Bridge(bridge) => Some(bridge.clone()),
            _ => None,
        }).collect();

        let mut theory_count = 0usize;
        let mut values_evaluated = 0usize;

        for item in &module.items {
            if let ModuleItem::Theory(theory) = item {
                theory_count += 1;
                self.run_theory(theory, &mut values_evaluated)?;
            }
        }

        Ok(RunReport {
            module_name: module.name.clone(),
            theory_count,
            values_evaluated,
            output: self.output,
        })
    }

    fn run_theory(&mut self, theory: &TheoryDecl, values_evaluated: &mut usize) -> Result<(), Diagnostic> {
        self.scopes.entry(theory.name.clone()).or_default();

        for item in &theory.items {
            match item {
                TheoryItem::Let(let_decl) => {
                    let value = self.eval_expr(&let_decl.expr, &theory.name)?;
                    self.scopes
                        .entry(theory.name.clone())
                        .or_default()
                        .insert(let_decl.name.clone(), value);
                    *values_evaluated += 1;
                }
                TheoryItem::Expr(expr) => {
                    let _ = self.eval_expr(expr, &theory.name)?;
                }
            }
        }

        Ok(())
    }

    fn eval_expr(&mut self, expr: &Expr, ambient_theory: &str) -> Result<RuntimeValue, Diagnostic> {
        match &expr.kind {
            ExprKind::IntLiteral(value) => {
                let parsed = value.parse::<u128>().map_err(|_| {
                    Diagnostic::error(
                        DiagnosticKind::RuntimeError,
                        Some(expr.line),
                        format!("integer literal '{value}' is too large for MVP runtime exact evaluation"),
                    ).with_help("The checker may accept symbolic/compressed numbers, but dlm run v0.13 only executes exact u128-sized Nat values.")
                })?;
                Ok(RuntimeValue::NatExact(parsed))
            }
            ExprKind::Ident(name) => self.lookup_ident(name, ambient_theory, expr.line),
            ExprKind::QualifiedIdent { theory, name } => {
                if theory == ambient_theory {
                    self.lookup_ident(name, ambient_theory, expr.line)
                } else {
                    Err(Diagnostic::error(
                        DiagnosticKind::RuntimeError,
                        Some(expr.line),
                        format!("dlm run MVP cannot directly execute cross-theory value '{theory}.{name}'"),
                    ).with_help("Use quote(...) for syntax-level transfer; semantic transport is not executable in v0.13."))
                }
            }
            ExprKind::Power { base, exp } => {
                let base = self.eval_expr(base, ambient_theory)?;
                let exp = self.eval_expr(exp, ambient_theory)?;
                match (base, exp) {
                    (RuntimeValue::NatExact(base), RuntimeValue::NatExact(exp)) => {
                        let exp: u32 = exp.try_into().map_err(|_| {
                            Diagnostic::error(
                                DiagnosticKind::RuntimeError,
                                Some(expr.line),
                                "power exponent is too large for MVP runtime exact evaluation",
                            )
                        })?;
                        base.checked_pow(exp)
                            .map(RuntimeValue::NatExact)
                            .ok_or_else(|| Diagnostic::error(
                                DiagnosticKind::RuntimeError,
                                Some(expr.line),
                                "power result overflows MVP runtime exact Nat range",
                            ).with_help("Use dlm check for symbolic/compressed reasoning; dlm run v0.13 executes only u128-sized exact Nat values."))
                    }
                    (lhs, rhs) => Ok(RuntimeValue::NatSymbolic(format!("({} ^ {})", lhs.display_atom(), rhs.display_atom()))),
                }
            }
            ExprKind::Add { lhs, rhs } => {
                let lhs = self.eval_expr(lhs, ambient_theory)?;
                let rhs = self.eval_expr(rhs, ambient_theory)?;
                match (lhs, rhs) {
                    (RuntimeValue::NatExact(lhs), RuntimeValue::NatExact(rhs)) => lhs.checked_add(rhs)
                        .map(RuntimeValue::NatExact)
                        .ok_or_else(|| Diagnostic::error(
                            DiagnosticKind::RuntimeError,
                            Some(expr.line),
                            "addition result overflows MVP runtime exact Nat range",
                        ).with_help("Use symbolic values for checking; dlm run v0.13 executes only u128-sized exact Nat values.")),
                    (lhs, rhs) => Ok(RuntimeValue::NatSymbolic(format!("({} + {})", lhs.display_atom(), rhs.display_atom()))),
                }
            }
            ExprKind::CompareGt { lhs, rhs } => {
                let lhs = self.eval_expr(lhs, ambient_theory)?;
                let rhs = self.eval_expr(rhs, ambient_theory)?;
                match (lhs, rhs) {
                    (RuntimeValue::NatExact(lhs), RuntimeValue::NatExact(rhs)) => Ok(RuntimeValue::Bool(lhs > rhs)),
                    _ => Err(Diagnostic::error(
                        DiagnosticKind::RuntimeError,
                        Some(expr.line),
                        "comparison is not executable for symbolic values in dlm run v0.13",
                    )),
                }
            }
            ExprKind::Call { name, args } => self.eval_call(name, args, ambient_theory, expr.line),
        }
    }

    fn eval_call(&mut self, name: &str, args: &[Expr], ambient_theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        match name {
            "print_decimal" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "print_decimal expects one argument"));
                }
                let value = self.eval_expr(&args[0], ambient_theory)?;
                match value {
                    RuntimeValue::NatExact(n) => {
                        let text = n.to_string();
                        self.output.push(text.clone());
                        Ok(RuntimeValue::Text(text))
                    }
                    other => Err(Diagnostic::error(
                        DiagnosticKind::RuntimeError,
                        Some(line),
                        "print_decimal can execute only exact Nat values in dlm run v0.13",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "print_text" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "print_text expects one argument"));
                }
                let value = self.eval_expr(&args[0], ambient_theory)?;
                match value {
                    RuntimeValue::Text(text) => {
                        self.output.push(text.clone());
                        Ok(RuntimeValue::Text(text))
                    }
                    other => Err(Diagnostic::error(
                        DiagnosticKind::RuntimeError,
                        Some(line),
                        "print_text can execute only Text values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "universe" => Err(Diagnostic::error(
                DiagnosticKind::UniverseLevelError,
                Some(line),
                "ambiguous universe is not executable",
            ).with_help("use U0(), U1(), U2(), or universe_succ(U n)")),
            "U0" | "universe0" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(RuntimeValue::Universe { level: 0 })
            }
            "U1" | "universe1" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(RuntimeValue::Universe { level: 1 })
            }
            "U2" | "universe2" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(RuntimeValue::Universe { level: 2 })
            }
            "universe_succ" | "next_universe" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one Universe argument")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::Universe { level } => Ok(RuntimeValue::Universe { level: level.saturating_add(1) }),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::UniverseLevelError,
                        Some(line),
                        "universe_succ can execute only Universe values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "set_of" | "set_from_universe" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one Universe argument")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::Universe { level } => Ok(RuntimeValue::Set { of_level: level, lives_in: level.saturating_add(1) }),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::UniverseLevelError,
                        Some(line),
                        "set_of can execute only Universe values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "class_of" | "class_from_universe" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one Universe argument")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::Universe { level } => Ok(RuntimeValue::Class { of_level: level }),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::UniverseLevelError,
                        Some(line),
                        "class_of can execute only Universe values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "set_lives_in" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "set_lives_in expects one Set argument"));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::Set { lives_in, .. } => Ok(RuntimeValue::NatExact(lives_in as u128)),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::UniverseLevelError,
                        Some(line),
                        "set_lives_in can execute only Set values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "class_level" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "class_level expects one Class argument"));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::Class { of_level } => Ok(RuntimeValue::NatExact(of_level as u128)),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::UniverseLevelError,
                        Some(line),
                        "class_level can execute only Class values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "set_of_all_sets" | "russell_set" => Err(Diagnostic::error(
                DiagnosticKind::UniverseLevelError,
                Some(line),
                "set_of_all_sets is not executable because it is not a well-typed universe object",
            )),

            "language_L0" | "L0_language" | "language_core" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::DefinabilityError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(RuntimeValue::Language("L0".to_string()))
            }
            "encoding_godel" | "godel_encoding" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::DefinabilityError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(RuntimeValue::Encoding("Godel".to_string()))
            }
            "meta_level" | "M" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::DefinabilityError, Some(line), format!("{name} expects one literal Nat level")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::NatExact(value) if value <= u8::MAX as u128 => Ok(RuntimeValue::MetaLevel(value as u8)),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::DefinabilityError,
                        Some(line),
                        "meta_level can execute only u8-sized Nat levels",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "definable_nat" | "define_nat" => {
                if args.len() != 4 {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DefinabilityError,
                        Some(line),
                        "definable_nat requires explicit language, encoding, bound and meta-level",
                    ).with_help("use: definable_nat(language_L0(), encoding_godel(), 20, meta_level(1))"));
                }
                let language = match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::Language(value) => value,
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::DefinabilityError,
                        Some(line),
                        "definable_nat first argument must be Language",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                let encoding = match self.eval_expr(&args[1], ambient_theory)? {
                    RuntimeValue::Encoding(value) => value,
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::DefinabilityError,
                        Some(line),
                        "definable_nat second argument must be Encoding",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                let bound = match self.eval_expr(&args[2], ambient_theory)? {
                    RuntimeValue::NatExact(value) if value > 0 => value,
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::DefinabilityError,
                        Some(line),
                        "definable_nat bound must be a positive exact Nat",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                let meta_level = match self.eval_expr(&args[3], ambient_theory)? {
                    RuntimeValue::MetaLevel(value) => value,
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::DefinabilityError,
                        Some(line),
                        "definable_nat fourth argument must be MetaLevel",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                Ok(RuntimeValue::DefinableNat { language, encoding, theory: ambient_theory.to_string(), bound, meta_level })
            }
            "definability_bound" | "definition_bound" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::DefinabilityError, Some(line), format!("{name} expects one DefinableNat argument")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::DefinableNat { bound, .. } => Ok(RuntimeValue::NatExact(bound)),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::DefinabilityError,
                        Some(line),
                        "definability_bound can execute only DefinableNat values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "definability_meta_level" | "definition_meta_level" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::DefinabilityError, Some(line), format!("{name} expects one DefinableNat argument")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::DefinableNat { meta_level, .. } => Ok(RuntimeValue::NatExact(meta_level as u128)),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::DefinabilityError,
                        Some(line),
                        "definability_meta_level can execute only DefinableNat values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "berry_number" | "smallest_undefinable" | "undefinable_nat" => Err(Diagnostic::error(
                DiagnosticKind::DefinabilityError,
                Some(line),
                "bare undefinability/Berry-style construction is not executable",
            ).with_help("Definability must be relative to language, encoding, theory, bound and meta-level.")),

            "big_number" | "huge_number" => Err(Diagnostic::error(
                DiagnosticKind::BigNumberError,
                Some(line),
                "bare big_number is not executable",
            ).with_help("use Graham(), TREE(n), BB(n), or fast_growing(level)")),
            "Graham" | "graham" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::BigNumberError, Some(line), "Graham expects no arguments"));
                }
                Ok(RuntimeValue::NatSymbolic("Graham()".to_string()))
            }
            "TREE" | "tree" | "tree_number" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::BigNumberError, Some(line), "TREE expects one positive Nat parameter"));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::NatExact(value) if value > 0 => Ok(RuntimeValue::NatSymbolic(format!("TREE({value})"))),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::BigNumberError,
                        Some(line),
                        "TREE parameter must be a positive exact Nat in MVP runtime",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "BB" | "busy_beaver" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::BigNumberError, Some(line), "BB expects one positive Nat parameter"));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::NatExact(value) if value > 0 => Ok(RuntimeValue::NatSymbolic(format!("BB({value})"))),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::BigNumberError,
                        Some(line),
                        "BB parameter must be a positive exact Nat in MVP runtime",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "fast_growing" | "FGH" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::BigNumberError, Some(line), "fast_growing expects one positive Nat level"));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::NatExact(value) if value > 0 => Ok(RuntimeValue::NatSymbolic(format!("FGH({value})"))),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::BigNumberError,
                        Some(line),
                        "fast_growing level must be a positive exact Nat in MVP runtime",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "growth_parameter" | "big_number_parameter" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::BigNumberError, Some(line), format!("{name} expects one BigNat argument")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::NatSymbolic(text) => {
                        let parameter = text.rsplit_once('(')
                            .and_then(|(_, tail)| tail.strip_suffix(')'))
                            .and_then(|raw| raw.parse::<u128>().ok())
                            .unwrap_or(0);
                        Ok(RuntimeValue::NatExact(parameter))
                    }
                    other => Err(Diagnostic::error(
                        DiagnosticKind::BigNumberError,
                        Some(line),
                        "growth_parameter can execute only symbolic BigNat values in MVP runtime",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "infinity" => Err(Diagnostic::error(
                DiagnosticKind::InfinityModeError,
                Some(line),
                "ambiguous infinity is not executable",
            ).with_help("use aleph0()/infinity_cardinal(), omega()/infinity_ordinal(), limit_omega(), potential_infinity(), class_infinity(class_of(U0())), or universe_infinity(U0())")),
            "aleph0" | "infinity_cardinal" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(RuntimeValue::Infinity { mode: "cardinal".to_string(), repr: "ℵ0".to_string() })
            }
            "omega" | "infinity_ordinal" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(RuntimeValue::Infinity { mode: "ordinal".to_string(), repr: "ω".to_string() })
            }
            "limit_omega" | "infinity_limit" | "limit_infinity" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(RuntimeValue::Infinity { mode: "limit".to_string(), repr: "limit(ω)".to_string() })
            }
            "potential_infinity" | "infinity_potential" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(RuntimeValue::Infinity { mode: "potential".to_string(), repr: "∞ₚ".to_string() })
            }
            "class_infinity" | "proper_class_infinity" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one Class argument")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::Class { of_level } => Ok(RuntimeValue::Infinity { mode: "class".to_string(), repr: format!("Class∞<U{of_level}>") }),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::InfinityModeError,
                        Some(line),
                        "class_infinity can execute only on Class<U n>",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "universe_infinity" | "infinity_universe" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one Universe argument")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::Universe { level } => Ok(RuntimeValue::Infinity { mode: "universe".to_string(), repr: format!("Universe∞<U{level}>") }),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::InfinityModeError,
                        Some(line),
                        "universe_infinity can execute only on Universe<U n>",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "cardinal_succ" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "cardinal_succ expects one argument"));
                }
                let value = self.eval_expr(&args[0], ambient_theory)?;
                match value {
                    RuntimeValue::Infinity { mode, repr } if mode == "cardinal" => {
                        Ok(RuntimeValue::Infinity { mode, repr: format!("cardinal_succ({repr})") })
                    }
                    other => Err(Diagnostic::error(
                        DiagnosticKind::InfinityModeError,
                        Some(line),
                        "cardinal_succ can execute only Infinity<cardinal>",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "ordinal_succ" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "ordinal_succ expects one argument"));
                }
                let value = self.eval_expr(&args[0], ambient_theory)?;
                match value {
                    RuntimeValue::Infinity { mode, repr } if mode == "ordinal" => {
                        Ok(RuntimeValue::Infinity { mode, repr: format!("ordinal_succ({repr})") })
                    }
                    other => Err(Diagnostic::error(
                        DiagnosticKind::InfinityModeError,
                        Some(line),
                        "ordinal_succ can execute only Infinity<ordinal>",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "cardinal_add" | "cardinal_sum" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects two arguments")));
                }
                let lhs = self.eval_expr(&args[0], ambient_theory)?;
                let rhs = self.eval_expr(&args[1], ambient_theory)?;
                match (lhs, rhs) {
                    (RuntimeValue::Infinity { mode: ma, repr: ra }, RuntimeValue::Infinity { mode: mb, repr: rb }) if ma == "cardinal" && mb == "cardinal" => {
                        Ok(RuntimeValue::Infinity { mode: "cardinal".to_string(), repr: format!("cardinal_add({ra}, {rb})") })
                    }
                    (lhs, rhs) => Err(Diagnostic::error(
                        DiagnosticKind::InfinityModeError,
                        Some(line),
                        "cardinal_add can execute only on Infinity<cardinal> operands",
                    ).with_help(format!("left: {}; right: {}", lhs.display_atom(), rhs.display_atom()))),
                }
            }
            "ordinal_add" | "ordinal_sum" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects two arguments")));
                }
                let lhs = self.eval_expr(&args[0], ambient_theory)?;
                let rhs = self.eval_expr(&args[1], ambient_theory)?;
                match (lhs, rhs) {
                    (RuntimeValue::Infinity { mode: ma, repr: ra }, RuntimeValue::Infinity { mode: mb, repr: rb }) if ma == "ordinal" && mb == "ordinal" => {
                        Ok(RuntimeValue::Infinity { mode: "ordinal".to_string(), repr: format!("ordinal_add({ra}, {rb})") })
                    }
                    (lhs, rhs) => Err(Diagnostic::error(
                        DiagnosticKind::InfinityModeError,
                        Some(line),
                        "ordinal_add can execute only on Infinity<ordinal> operands",
                    ).with_help(format!("left: {}; right: {}", lhs.display_atom(), rhs.display_atom()))),
                }
            }
            "potential_step" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "potential_step expects one argument"));
                }
                let value = self.eval_expr(&args[0], ambient_theory)?;
                match value {
                    RuntimeValue::Infinity { mode, repr } if mode == "potential" => {
                        Ok(RuntimeValue::Infinity { mode, repr: format!("potential_step({repr})") })
                    }
                    other => Err(Diagnostic::error(
                        DiagnosticKind::InfinityModeError,
                        Some(line),
                        "potential_step can execute only Infinity<potential>",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "node_x86" | "node_x86_64" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(RuntimeValue::Node { arch: "x86_64".to_string(), cores: 1, memory_mib: 1024 })
            }
            "node_arm" | "node_aarch64" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(RuntimeValue::Node { arch: "aarch64".to_string(), cores: 1, memory_mib: 1024 })
            }
            "node_x86_with" | "node_x86_64_with" => self.eval_node_with("x86_64", args, line),
            "node_arm_with" | "node_aarch64_with" => self.eval_node_with("aarch64", args, line),
            "gpu_cuda" | "gpu_device_cuda" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(RuntimeValue::GpuDevice { backend: "cuda".to_string(), memory_mib: 8192 })
            }
            "gpu_rocm" | "gpu_device_rocm" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(RuntimeValue::GpuDevice { backend: "rocm".to_string(), memory_mib: 8192 })
            }
            "gpu_cuda_with" | "gpu_device_cuda_with" => self.eval_gpu_with("cuda", args, line),
            "gpu_rocm_with" | "gpu_device_rocm_with" => self.eval_gpu_with("rocm", args, line),
            "gpu_pool" | "virtual_gpu_pool" => {
                if args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects at least one GpuDevice argument")));
                }
                let mut devices = Vec::new();
                for arg in args {
                    match self.eval_expr(arg, ambient_theory)? {
                        RuntimeValue::GpuDevice { backend, memory_mib } => devices.push((backend, memory_mib)),
                        other => return Err(Diagnostic::error(
                            DiagnosticKind::DistributedResourceError,
                            Some(line),
                            "gpu_pool can execute only GpuDevice runtime values",
                        ).with_help(format!("runtime value was: {}", other.display_atom()))),
                    }
                }
                Ok(RuntimeValue::GpuPool { devices })
            }
            "distributed_gpu_memory" | "allocate_gpu_memory" | "gpu_memory_region" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects gpu_pool and memory_mib")));
                }
                let devices = match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::GpuPool { devices } => devices,
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "distributed_gpu_memory can execute only with a GpuPool as first argument",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                let requested = match self.eval_expr(&args[1], ambient_theory)? {
                    RuntimeValue::NatExact(value) => value,
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "distributed_gpu_memory memory_mib must evaluate to exact Nat in MVP",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                if requested == 0 {
                    return Err(Diagnostic::error(DiagnosticKind::DistributedResourceError, Some(line), "distributed_gpu_memory memory_mib must be greater than zero"));
                }
                let total = devices.iter().try_fold(0u128, |acc, (_, mem)| acc.checked_add(*mem))
                    .ok_or_else(|| Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "gpu pool memory total overflowed u128 in MVP runtime"))?;
                if requested > total {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        format!("distributed_gpu_memory request {requested} MiB exceeds GpuPool memory {total} MiB"),
                    ));
                }
                Ok(RuntimeValue::DistributedGpuMemory { memory_mib: requested })
            }
            "gpu_memory_mib" | "distributed_gpu_memory_mib" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one DistributedGpuMemory argument")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::DistributedGpuMemory { memory_mib } => Ok(RuntimeValue::NatExact(memory_mib)),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "gpu_memory_mib can execute only DistributedGpuMemory values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "copy_to_gpu" | "gpu_upload" | "upload_to_gpu" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects source value and DistributedGpuMemory region")));
                }
                let inner = self.eval_expr(&args[0], ambient_theory)?;
                let memory_mib = match self.eval_expr(&args[1], ambient_theory)? {
                    RuntimeValue::DistributedGpuMemory { memory_mib } => memory_mib,
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "copy_to_gpu can execute only with DistributedGpuMemory as second argument",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                Ok(RuntimeValue::GpuValue { inner: Box::new(inner), memory_mib })
            }
            "copy_from_gpu" | "gpu_download" | "download_from_gpu" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one GpuValue")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::GpuValue { inner, .. } => Ok(*inner),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "copy_from_gpu can execute only GpuValue<T> values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "compile_gpu_kernel" | "gpu_kernel" | "make_gpu_kernel" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one value")));
                }
                let inner = self.eval_expr(&args[0], ambient_theory)?;
                Ok(RuntimeValue::GpuKernel { inner: Box::new(inner) })
            }
            "launch_kernel" | "launch_gpu_kernel" | "gpu_launch" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects DistributedGpuMemory and GpuKernel")));
                }
                let memory_mib = match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::DistributedGpuMemory { memory_mib } => memory_mib,
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "launch_kernel can execute only with DistributedGpuMemory as first argument",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                let inner = match self.eval_expr(&args[1], ambient_theory)? {
                    RuntimeValue::GpuKernel { inner } => inner,
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "launch_kernel can execute only with GpuKernel<T> as second argument",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                Ok(RuntimeValue::GpuValue { inner, memory_mib })
            }
            "virtual_pool" | "virtual_cluster" => {
                if args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects at least one Node argument")));
                }
                let mut nodes = Vec::new();
                for arg in args {
                    match self.eval_expr(arg, ambient_theory)? {
                        RuntimeValue::Node { arch, cores, memory_mib } => nodes.push((arch, cores, memory_mib)),
                        other => return Err(Diagnostic::error(
                            DiagnosticKind::DistributedResourceError,
                            Some(line),
                            "virtual_pool can execute only Node runtime values",
                        ).with_help(format!("runtime value was: {}", other.display_atom()))),
                    }
                }
                Ok(RuntimeValue::VirtualCluster { nodes })
            }
            "pool_cores" | "cluster_cores" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one VirtualCluster argument")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::VirtualCluster { nodes } => {
                        let total = nodes.iter().try_fold(0u128, |acc, (_, cores, _)| acc.checked_add(*cores))
                            .ok_or_else(|| Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "pool_cores overflowed u128 in MVP runtime"))?;
                        Ok(RuntimeValue::NatExact(total))
                    }
                    other => Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "pool_cores can execute only VirtualCluster values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "pool_memory_mib" | "cluster_memory_mib" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one VirtualCluster argument")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::VirtualCluster { nodes } => {
                        let total = nodes.iter().try_fold(0u128, |acc, (_, _, mem)| acc.checked_add(*mem))
                            .ok_or_else(|| Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "pool_memory_mib overflowed u128 in MVP runtime"))?;
                        Ok(RuntimeValue::NatExact(total))
                    }
                    other => Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "pool_memory_mib can execute only VirtualCluster values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "distributed_memory" | "allocate_memory" | "memory_region" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects pool and memory_mib")));
                }
                let nodes = match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::VirtualCluster { nodes } => nodes,
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "distributed_memory can execute only with a VirtualCluster as first argument",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                let requested = match self.eval_expr(&args[1], ambient_theory)? {
                    RuntimeValue::NatExact(value) => value,
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "distributed_memory memory_mib must evaluate to exact Nat in MVP",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                if requested == 0 {
                    return Err(Diagnostic::error(DiagnosticKind::DistributedResourceError, Some(line), "distributed_memory memory_mib must be greater than zero"));
                }
                let total = nodes.iter().try_fold(0u128, |acc, (_, _, mem)| acc.checked_add(*mem))
                    .ok_or_else(|| Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "virtual cluster memory total overflowed u128 in MVP runtime"))?;
                if requested > total {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        format!("distributed_memory request {requested} MiB exceeds VirtualCluster memory {total} MiB"),
                    ));
                }
                Ok(RuntimeValue::DistributedMemory { memory_mib: requested })
            }
            "memory_region_mib" | "distributed_memory_mib" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one DistributedMemory argument")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::DistributedMemory { memory_mib } => Ok(RuntimeValue::NatExact(memory_mib)),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "memory_region_mib can execute only DistributedMemory values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "checkpoint_memory" | "checkpoint" | "checkpoint_region" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one DistributedMemory argument")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::DistributedMemory { memory_mib } => Ok(RuntimeValue::MemoryCheckpoint { memory_mib }),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "checkpoint_memory can execute only DistributedMemory values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "restore_checkpoint" | "restore_memory" | "restore" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one MemoryCheckpoint argument")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::MemoryCheckpoint { memory_mib } => Ok(RuntimeValue::DistributedMemory { memory_mib }),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "restore_checkpoint can execute only MemoryCheckpoint values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "compile_portable" | "portable_code" | "make_portable" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one value")));
                }
                let inner = self.eval_expr(&args[0], ambient_theory)?;
                Ok(RuntimeValue::PortableCode { inner: Box::new(inner) })
            }
            "deploy_portable" | "deploy_code" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects target node and PortableCode")));
                }
                let target = self.eval_expr(&args[0], ambient_theory)?;
                let arch = match target {
                    RuntimeValue::Node { arch, .. } => arch,
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::MigrationBridgeError,
                        Some(line),
                        "deploy_portable target must be a Node runtime value",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                match self.eval_expr(&args[1], ambient_theory)? {
                    RuntimeValue::PortableCode { inner } => Ok(RuntimeValue::Remote { arch, inner }),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "deploy_portable can execute only PortableCode<T> values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "deploy_on" | "deploy_to_pool" => {
                if args.len() != 3 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects pool, target node and PortableCode")));
                }
                let nodes = match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::VirtualCluster { nodes } => nodes,
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "deploy_on can execute only with a VirtualCluster as first argument",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                let target = self.eval_expr(&args[1], ambient_theory)?;
                let (arch, cores, memory_mib) = match target {
                    RuntimeValue::Node { arch, cores, memory_mib } => (arch, cores, memory_mib),
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::MigrationBridgeError,
                        Some(line),
                        "deploy_on target must be a Node runtime value",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                if !nodes.iter().any(|(node_arch, node_cores, node_mem)| {
                    node_arch == &arch && *node_cores == cores && *node_mem == memory_mib
                }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "deploy_on target node is not a member of the VirtualCluster",
                    ).with_help(format!("target was: node<{arch}>{{cores={cores}, memory_mib={memory_mib}}}")));
                }
                match self.eval_expr(&args[2], ambient_theory)? {
                    RuntimeValue::PortableCode { inner } => Ok(RuntimeValue::Remote { arch, inner }),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "deploy_on can execute only PortableCode<T> values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "materialize_remote" | "materialize" | "fetch_remote" | "collect_remote" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one Remote value")));
                }

                let remote = match &args[0].kind {
                    ExprKind::QualifiedIdent { theory: source, name } => {
                        if source != ambient_theory && !self.has_bridge(source, ambient_theory, BridgeKind::Materialize) {
                            return Err(Diagnostic::error(
                                DiagnosticKind::TheoryBridgeError,
                                Some(line),
                                format!("no executable materialize bridge from theory {source} to {ambient_theory} in scope"),
                            ).with_help("declare or import: bridge Name : Source -> Target { kind = materialize }"));
                        }
                        self.lookup_qualified_runtime(source, name, line)?
                    }
                    _ => self.eval_expr(&args[0], ambient_theory)?,
                };

                match remote {
                    RuntimeValue::Remote { inner, .. } => Ok(*inner),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "materialize_remote can execute only Remote<T@arch> values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "checkpoint_remote" | "checkpoint_job" | "checkpoint_remote_job" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one Remote value")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::Remote { arch, inner } => Ok(RuntimeValue::RemoteCheckpoint { arch, inner }),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "checkpoint_remote can execute only Remote<T@arch> values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "restore_remote" | "restore_job" | "restore_remote_checkpoint" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects target node and RemoteCheckpoint")));
                }
                let target = self.eval_expr(&args[0], ambient_theory)?;
                let target_arch = match target {
                    RuntimeValue::Node { arch, .. } => arch,
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::MigrationBridgeError,
                        Some(line),
                        "restore_remote target must be a Node runtime value",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                match self.eval_expr(&args[1], ambient_theory)? {
                    RuntimeValue::RemoteCheckpoint { inner, .. } => Ok(RuntimeValue::Remote { arch: target_arch, inner }),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "restore_remote can execute only RemoteCheckpoint<T@arch> values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "live_migrate" | "live_migrate_remote" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects target node and Remote value")));
                }
                let target = self.eval_expr(&args[0], ambient_theory)?;
                let target_arch = match target {
                    RuntimeValue::Node { arch, .. } => arch,
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::MigrationBridgeError,
                        Some(line),
                        "live_migrate target must be a Node runtime value",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                match self.eval_expr(&args[1], ambient_theory)? {
                    RuntimeValue::Remote { inner, .. } => Ok(RuntimeValue::Remote { arch: target_arch, inner }),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "live_migrate can execute only Remote<T@arch> values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "schedule" | "schedule_on" => {
                if args.len() != 3 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects pool, target node and source value")));
                }
                let nodes = match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::VirtualCluster { nodes } => nodes,
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "schedule_on can execute only with a VirtualCluster as first argument",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };

                let target = self.eval_expr(&args[1], ambient_theory)?;
                let (arch, cores, memory_mib) = match target {
                    RuntimeValue::Node { arch, cores, memory_mib } => (arch, cores, memory_mib),
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::MigrationBridgeError,
                        Some(line),
                        "schedule_on target must be a Node runtime value",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };

                if !nodes.iter().any(|(node_arch, node_cores, node_mem)| {
                    node_arch == &arch && *node_cores == cores && *node_mem == memory_mib
                }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "schedule_on target node is not a member of the VirtualCluster",
                    ).with_help(format!("target was: node<{arch}>{{cores={cores}, memory_mib={memory_mib}}}")));
                }

                match &args[2].kind {
                    ExprKind::QualifiedIdent { theory: source, name } => {
                        if source != ambient_theory && !self.has_bridge(source, ambient_theory, BridgeKind::Migration) {
                            return Err(Diagnostic::error(
                                DiagnosticKind::MigrationBridgeError,
                                Some(line),
                                format!("no executable migration bridge from theory {source} to {ambient_theory} in scope for schedule_on"),
                            ));
                        }
                        let inner = self.lookup_qualified_runtime(source, name, line)?;
                        Ok(RuntimeValue::Remote { arch, inner: Box::new(inner) })
                    }
                    _ => {
                        let inner = self.eval_expr(&args[2], ambient_theory)?;
                        Ok(RuntimeValue::Remote { arch, inner: Box::new(inner) })
                    }
                }
            }
            "migrate" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "migrate expects target node and source value"));
                }
                let target = self.eval_expr(&args[0], ambient_theory)?;
                let arch = match target {
                    RuntimeValue::Node { arch, .. } => arch,
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::MigrationBridgeError,
                        Some(line),
                        "migrate target must be a Node runtime value",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                match &args[1].kind {
                    ExprKind::QualifiedIdent { theory: source, name } => {
                        if !self.has_bridge(source, ambient_theory, BridgeKind::Migration) {
                            return Err(Diagnostic::error(
                                DiagnosticKind::MigrationBridgeError,
                                Some(line),
                                format!("no executable migration bridge from theory {source} to {ambient_theory} in scope"),
                            ));
                        }
                        let inner = self.lookup_qualified_runtime(source, name, line)?;
                        Ok(RuntimeValue::Remote { arch, inner: Box::new(inner) })
                    }
                    _ => {
                        if !self.has_bridge(ambient_theory, ambient_theory, BridgeKind::Migration) {
                            return Err(Diagnostic::error(
                                DiagnosticKind::MigrationBridgeError,
                                Some(line),
                                format!("no executable migration bridge from theory {ambient_theory} to {ambient_theory} in scope"),
                            ));
                        }
                        let inner = self.eval_expr(&args[1], ambient_theory)?;
                        Ok(RuntimeValue::Remote { arch, inner: Box::new(inner) })
                    }
                }
            }
            "equals" | "eq" => Err(Diagnostic::error(
                DiagnosticKind::EqualityModeError,
                Some(line),
                "ambiguous equality is not executable",
            ).with_help("use an explicit equality mode: eq_value(...), eq_syntax(...), or eq_proof(...)")),
            "eq_value" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "eq_value expects two arguments"));
                }
                let lhs = self.eval_expr(&args[0], ambient_theory)?;
                let rhs = self.eval_expr(&args[1], ambient_theory)?;
                match (&lhs, &rhs) {
                    (RuntimeValue::NatExact(a), RuntimeValue::NatExact(b)) => Ok(RuntimeValue::Bool(a == b)),
                    (RuntimeValue::Bool(a), RuntimeValue::Bool(b)) => Ok(RuntimeValue::Bool(a == b)),
                    (RuntimeValue::Infinity { mode: ma, repr: ra }, RuntimeValue::Infinity { mode: mb, repr: rb }) if ma == mb => Ok(RuntimeValue::Bool(ra == rb)),
                    _ => Err(Diagnostic::error(
                        DiagnosticKind::RuntimeError,
                        Some(line),
                        "eq_value can execute only direct value equality for exact values of the same runtime kind",
                    ).with_help(format!("left: {}\n  right: {}", lhs.display_atom(), rhs.display_atom()))),
                }
            }
            "eq_proof" => Err(Diagnostic::error(
                DiagnosticKind::RuntimeError,
                Some(line),
                "eq_proof is a proof-level equality check and is not executable in dlm run v0.13",
            ).with_help("Use eq_value(...) for direct runtime equality or eq_syntax(...) for Term syntax equality.")),
            "eq_syntax" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "eq_syntax expects two Term arguments"));
                }
                let lhs = self.eval_expr(&args[0], ambient_theory)?;
                let rhs = self.eval_expr(&args[1], ambient_theory)?;
                match (&lhs, &rhs) {
                    (RuntimeValue::Term(a), RuntimeValue::Term(b)) => Ok(RuntimeValue::Bool(a == b)),
                    _ => Err(Diagnostic::error(
                        DiagnosticKind::RuntimeError,
                        Some(line),
                        "eq_syntax can execute only Term syntax equality",
                    ).with_help(format!("left: {}\n  right: {}", lhs.display_atom(), rhs.display_atom()))),
                }
            }
            "print_symbolic" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "print_symbolic expects one argument"));
                }
                let value = self.eval_expr(&args[0], ambient_theory)?;
                let text = value.display_atom();
                self.output.push(text.clone());
                Ok(RuntimeValue::Text(text))
            }
            "read_nat" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "read_nat expects no arguments"));
                }
                let text = self.stdin.trim();
                if text.is_empty() {
                    return Err(Diagnostic::error(
                        DiagnosticKind::RuntimeError,
                        Some(line),
                        "read_nat requires stdin data in dlm run v0.13",
                    ).with_help("pass input with: dlm run <file.dlm> --stdin 42"));
                }
                let parsed = text.parse::<u128>().map_err(|_| {
                    Diagnostic::error(
                        DiagnosticKind::RuntimeError,
                        Some(line),
                        format!("stdin value '{text}' is not a valid MVP Nat"),
                    ).with_help("dlm run v0.13 read_nat supports non-negative u128-sized decimal integers only.")
                })?;
                Ok(RuntimeValue::NatExact(parsed))
            }
            "require" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "require expects one boolean condition"));
                }
                let value = self.eval_expr(&args[0], ambient_theory)?;
                match value {
                    RuntimeValue::Bool(true) => Ok(RuntimeValue::RuntimeWitness(format!("line_{line}_condition"))),
                    RuntimeValue::Bool(false) => Err(Diagnostic::error(
                        DiagnosticKind::RuntimeError,
                        Some(line),
                        "runtime requirement failed",
                    ).with_help("require(...) creates a RuntimeWitness only when the checked condition is true.")),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::RuntimeError,
                        Some(line),
                        "require expects a runtime Bool value",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "prove" => Err(Diagnostic::error(
                DiagnosticKind::RuntimeError,
                Some(line),
                "prove is a static checker operation and is not executable in dlm run v0.13",
            ).with_help("Use require(...) for runtime data and RuntimeWitness creation.")),
            "prop_true" | "true_prop" | "proposition_true" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(RuntimeValue::Proposition("true".to_string()))
            }
            "prop_gt" | "gt_prop" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects two exact Nat arguments")));
                }
                let lhs = self.eval_expr(&args[0], ambient_theory)?;
                let rhs = self.eval_expr(&args[1], ambient_theory)?;
                match (lhs, rhs) {
                    (RuntimeValue::NatExact(a), RuntimeValue::NatExact(b)) if a > b => Ok(RuntimeValue::Proposition("gt".to_string())),
                    (RuntimeValue::NatExact(a), RuntimeValue::NatExact(b)) => Err(Diagnostic::error(
                        DiagnosticKind::RuntimeError,
                        Some(line),
                        format!("prop_gt failed at runtime: {a} is not greater than {b}"),
                    )),
                    (lhs, rhs) => Err(Diagnostic::error(
                        DiagnosticKind::RuntimeError,
                        Some(line),
                        "prop_gt runtime execution requires exact Nat operands in MVP",
                    ).with_help(format!("left: {}\n  right: {}", lhs.display_atom(), rhs.display_atom()))),
                }
            }
            "provable_of" | "provability_of" | "provable" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one StaticProof argument")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::StaticProof(proposition) => Ok(RuntimeValue::Provable { theory: ambient_theory.to_string(), proposition }),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::TruthBoundaryError,
                        Some(line),
                        "provable_of can execute only StaticProof values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "truth_from_provable" | "assert_truth" | "truth" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one Provable claim")));
                }
                let value = self.eval_expr(&args[0], ambient_theory)?;
                match value {
                    RuntimeValue::Provable { .. } => Err(Diagnostic::error(
                        DiagnosticKind::TheoryBridgeError,
                        Some(line),
                        "Provable(phi) cannot execute as Truth(phi) without explicit soundness/axiom-tainted lift",
                    )),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::TruthBoundaryError,
                        Some(line),
                        "truth_from_provable can execute only Provable values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "truth_from_provable_axiom" | "assume_truth_from_provable" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one Provable claim")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::Provable { proposition, .. } => Ok(RuntimeValue::StaticProof(format!("truth_from_provable:{proposition}"))),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::TruthBoundaryError,
                        Some(line),
                        "truth_from_provable_axiom can execute only Provable values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "consistency_claim" | "consistency_of_current" | "consistent_current" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects no arguments in MVP")));
                }
                Ok(RuntimeValue::ConsistencyClaim { theory: ambient_theory.to_string() })
            }
            "prove_consistency" | "prove_own_consistency" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one Consistency<T> claim")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::ConsistencyClaim { theory } => Err(Diagnostic::error(
                        DiagnosticKind::IncompletenessBoundaryError,
                        Some(line),
                        format!("cannot execute proof of Consistency<{theory}> in the current MVP theory context"),
                    ).with_help("Use assume_consistency(...) for an explicit Axiom-tainted assumption.")),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::IncompletenessBoundaryError,
                        Some(line),
                        "prove_consistency can execute only Consistency<T> claims",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "consistency_axiom" | "assume_consistency" | "consistency_from_axiom" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one Consistency<T> claim")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::ConsistencyClaim { theory } => Ok(RuntimeValue::StaticProof(format!("consistency_axiom:{theory}"))),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::IncompletenessBoundaryError,
                        Some(line),
                        "assume_consistency can execute only Consistency<T> claims",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "reflection_claim" | "reflection_of" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one Provable<T> claim")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::Provable { theory, proposition } => {
                        if !self.has_bridge(&theory, ambient_theory, BridgeKind::Reflection) {
                            return Err(Diagnostic::error(
                                DiagnosticKind::ReflectionBoundaryError,
                                Some(line),
                                format!("no executable reflection bridge from theory {theory} to {ambient_theory} in scope"),
                            ).with_help("declare or import an explicit reflection bridge: bridge Name : Source -> Target { kind = reflection }"));
                        }
                        Ok(RuntimeValue::ReflectionClaim { theory, proposition })
                    }
                    other => Err(Diagnostic::error(
                        DiagnosticKind::ReflectionBoundaryError,
                        Some(line),
                        "reflection_claim can execute only Provable<T> claims",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "reflection_axiom" | "assume_reflection" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one Reflection<T> claim")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::ReflectionClaim { theory, proposition } => Ok(RuntimeValue::StaticProof(format!("reflection_axiom:{theory}:{proposition}"))),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::ReflectionBoundaryError,
                        Some(line),
                        "reflection_axiom can execute only Reflection<T> claims",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "self_reference" | "self_reference_claim" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one Prop value")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::Proposition(proposition) => Ok(RuntimeValue::SelfReferenceClaim { proposition }),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::ReflectionBoundaryError,
                        Some(line),
                        "self_reference can execute only Prop values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "godel_sentence" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "godel_sentence expects no arguments"));
                }
                Ok(RuntimeValue::SelfReferenceClaim { proposition: "godel_sentence".to_string() })
            }
            "self_reference_axiom" | "assume_self_reference" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one SelfReference<T> claim")));
                }
                match self.eval_expr(&args[0], ambient_theory)? {
                    RuntimeValue::SelfReferenceClaim { proposition } => Ok(RuntimeValue::StaticProof(format!("self_reference_axiom:{proposition}"))),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::ReflectionBoundaryError,
                        Some(line),
                        "self_reference_axiom can execute only SelfReference<T> claims",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "reflect_provable" | "prove_self_reference" | "truth_of_self_reference" | "truth_of_self" | "says_unprovable_self" | "liar_sentence" | "truth_of_own_truth" => {
                Err(Diagnostic::error(
                    DiagnosticKind::ReflectionBoundaryError,
                    Some(line),
                    format!("{name} crosses the reflection/self-reference boundary implicitly"),
                ).with_help("Use explicit claim constructors and visible Axiom-tainted lifts instead."))
            }
            "proof_true" | "true_intro" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(RuntimeValue::ProofTerm("true_intro".to_string()))
            }
            "proof_gt" | "gt_intro" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects two exact Nat arguments")));
                }
                let lhs = self.eval_expr(&args[0], ambient_theory)?;
                let rhs = self.eval_expr(&args[1], ambient_theory)?;
                match (lhs, rhs) {
                    (RuntimeValue::NatExact(a), RuntimeValue::NatExact(b)) if a > b => Ok(RuntimeValue::ProofTerm("gt_intro".to_string())),
                    (RuntimeValue::NatExact(a), RuntimeValue::NatExact(b)) => Err(Diagnostic::error(
                        DiagnosticKind::ProofKernelError,
                        Some(line),
                        format!("proof_gt kernel check failed: {a} is not greater than {b}"),
                    )),
                    (lhs, rhs) => Err(Diagnostic::error(
                        DiagnosticKind::ProofKernelError,
                        Some(line),
                        "proof_gt runtime execution requires exact Nat operands in MVP",
                    ).with_help(format!("left: {}\n  right: {}", lhs.display_atom(), rhs.display_atom()))),
                }
            }
            "check_proof" | "kernel_check" | "verify_proof" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("{name} expects one ProofTerm argument")));
                }
                let term = self.eval_expr(&args[0], ambient_theory)?;
                match term {
                    RuntimeValue::ProofTerm(rule) => Ok(RuntimeValue::StaticProof(format!("kernel_checked:{rule}"))),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::ProofKernelError,
                        Some(line),
                        "check_proof requires a runtime ProofTerm",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "fake_proof" | "unchecked_proof" | "bare_proof" => Err(Diagnostic::error(
                DiagnosticKind::ProofKernelError,
                Some(line),
                "bare/fake proof terms are not executable",
            ).with_help("use proof_true(), proof_gt(...), or another checked kernel constructor")),
            "quote" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "quote expects one argument"));
                }
                match &args[0].kind {
                    ExprKind::QualifiedIdent { theory, name } => Ok(RuntimeValue::Term(format!("{theory}.{name}"))),
                    other => Ok(RuntimeValue::Term(format!("{:?}", other))),
                }
            }
            "inspect_ast" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "inspect_ast expects one Term argument"));
                }
                let value = self.eval_expr(&args[0], ambient_theory)?;
                match value {
                    RuntimeValue::Term(term) => Ok(RuntimeValue::Text(format!("AST<{term}>"))),
                    other => Err(Diagnostic::error(
                        DiagnosticKind::RuntimeError,
                        Some(line),
                        "inspect_ast can execute only Term values",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "transport" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "transport expects one qualified source value"));
                }
                match &args[0].kind {
                    ExprKind::QualifiedIdent { theory: source, name } => {
                        if !self.has_bridge(source, ambient_theory, BridgeKind::Transport) {
                            return Err(Diagnostic::error(
                                DiagnosticKind::RuntimeError,
                                Some(line),
                                format!("no executable transport bridge from theory {source} to {ambient_theory} in scope"),
                            ));
                        }
                        self.lookup_qualified_runtime(source, name, line)
                    }
                    _ => Err(Diagnostic::error(
                        DiagnosticKind::RuntimeError,
                        Some(line),
                        "transport MVP expects a qualified source expression like PA.n",
                    )),
                }
            }
            "soundness" => Err(Diagnostic::error(
                DiagnosticKind::RuntimeError,
                Some(line),
                "soundness is a static theory bridge operation and is not executable in dlm run v0.13",
            )),
            "read" | "read_stdin" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), format!("'{name}' expects no arguments")));
                }
                Ok(RuntimeValue::Bytes(self.stdin.as_bytes().to_vec()))
            }
            "parse_nat" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "parse_nat expects one argument"));
                }
                let value = self.eval_expr(&args[0], ambient_theory)?;
                let bytes = match value {
                    RuntimeValue::Bytes(bytes) => bytes,
                    other => return Err(Diagnostic::error(
                        DiagnosticKind::RuntimeError,
                        Some(line),
                        "parse_nat expects raw Bytes",
                    ).with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                let text = String::from_utf8(bytes).map_err(|_| Diagnostic::error(
                    DiagnosticKind::RuntimeError,
                    Some(line),
                    "parse_nat input is not valid UTF-8",
                ))?;
                let trimmed = text.trim();
                let parsed = trimmed.parse::<u128>().map_err(|_| Diagnostic::error(
                    DiagnosticKind::RuntimeError,
                    Some(line),
                    format!("raw input '{trimmed}' is not a valid MVP Nat"),
                ))?;
                Ok(RuntimeValue::NatExact(parsed))
            }
            other => Err(Diagnostic::error(
                DiagnosticKind::RuntimeError,
                Some(line),
                format!("unknown runtime builtin '{other}'"),
            )),
        }
    }

    fn eval_node_with(&mut self, arch: &str, args: &[Expr], line: usize) -> Result<RuntimeValue, Diagnostic> {
        if args.len() != 2 {
            return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "node_*_with expects cores and memory_mib"));
        }
        let cores = self.eval_expr(&args[0], "")?;
        let memory = self.eval_expr(&args[1], "")?;
        let RuntimeValue::NatExact(cores) = cores else {
            return Err(Diagnostic::error(DiagnosticKind::DistributedResourceError, Some(line), "node cores must evaluate to exact Nat in MVP"));
        };
        let RuntimeValue::NatExact(memory_mib) = memory else {
            return Err(Diagnostic::error(DiagnosticKind::DistributedResourceError, Some(line), "node memory_mib must evaluate to exact Nat in MVP"));
        };
        if cores == 0 || memory_mib == 0 {
            return Err(Diagnostic::error(DiagnosticKind::DistributedResourceError, Some(line), "node resources must be greater than zero"));
        }
        Ok(RuntimeValue::Node { arch: arch.to_string(), cores, memory_mib })
    }

    fn eval_gpu_with(&mut self, backend: &str, args: &[Expr], line: usize) -> Result<RuntimeValue, Diagnostic> {
        if args.len() != 1 {
            return Err(Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), "gpu_*_with expects memory_mib"));
        }
        let memory = self.eval_expr(&args[0], "")?;
        let RuntimeValue::NatExact(memory_mib) = memory else {
            return Err(Diagnostic::error(DiagnosticKind::DistributedResourceError, Some(line), "gpu memory_mib must evaluate to exact Nat in MVP"));
        };
        if memory_mib == 0 {
            return Err(Diagnostic::error(DiagnosticKind::DistributedResourceError, Some(line), "gpu memory_mib must be greater than zero"));
        }
        Ok(RuntimeValue::GpuDevice { backend: backend.to_string(), memory_mib })
    }

    fn lookup_ident(&self, name: &str, ambient_theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        self.scopes
            .get(ambient_theory)
            .and_then(|scope| scope.get(name))
            .cloned()
            .ok_or_else(|| Diagnostic::error(
                DiagnosticKind::RuntimeError,
                Some(line),
                format!("runtime name '{name}' is not defined in theory {ambient_theory}"),
            ))
    }

    fn lookup_qualified_runtime(&self, theory: &str, name: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        self.scopes
            .get(theory)
            .and_then(|scope| scope.get(name))
            .cloned()
            .ok_or_else(|| Diagnostic::error(
                DiagnosticKind::RuntimeError,
                Some(line),
                format!("runtime qualified name '{theory}.{name}' is not defined"),
            ))
    }

    fn has_bridge(&self, source: &str, target: &str, kind: BridgeKind) -> bool {
        crate::bridge::has_bridge(&self.bridges, source, target, &kind)
    }
}

impl RuntimeValue {
    fn display_atom(&self) -> String {
        match self {
            RuntimeValue::NatExact(n) => n.to_string(),
            RuntimeValue::NatSymbolic(text) => text.clone(),
            RuntimeValue::Bool(value) => value.to_string(),
            RuntimeValue::Text(value) => format!("\"{value}\""),
            RuntimeValue::Infinity { repr, .. } => repr.clone(),
            RuntimeValue::Universe { level } => format!("U{level}"),
            RuntimeValue::Set { of_level, lives_in } => format!("Set<U{of_level}->U{lives_in}>"),
            RuntimeValue::Class { of_level } => format!("Class<U{of_level}>"),
            RuntimeValue::Language(value) => format!("language<{value}>"),
            RuntimeValue::Encoding(value) => format!("encoding<{value}>"),
            RuntimeValue::MetaLevel(value) => format!("M{value}"),
            RuntimeValue::DefinableNat { language, encoding, theory, bound, meta_level } => format!("definable_nat<{language},{encoding},{theory},bound={bound},M{meta_level}>"),
            RuntimeValue::Proposition(name) => format!("Prop<{name}>"),
            RuntimeValue::Provable { theory, proposition } => format!("Provable<{theory}.{proposition}>"),
            RuntimeValue::ConsistencyClaim { theory } => format!("Consistency<{theory}>"),
            RuntimeValue::ReflectionClaim { theory, proposition } => format!("Reflection<{theory}.{proposition}>"),
            RuntimeValue::SelfReferenceClaim { proposition } => format!("SelfReference<{proposition}>"),
            RuntimeValue::ProofTerm(rule) => format!("proof_term<{rule}>"),
            RuntimeValue::StaticProof(predicate) => format!("StaticProof<{predicate}>"),
            RuntimeValue::Bytes(bytes) => format!("<{} bytes>", bytes.len()),
            RuntimeValue::Term(value) => format!("term({value})"),
            RuntimeValue::RuntimeWitness(value) => format!("witness({value})"),
            RuntimeValue::Node { arch, cores, memory_mib } => format!("node<{arch}>{{cores={cores}, memory_mib={memory_mib}}}"),
            RuntimeValue::GpuDevice { backend, memory_mib } => format!("gpu<{backend}>{{memory_mib={memory_mib}}}"),
            RuntimeValue::GpuPool { devices } => {
                let memory_mib: u128 = devices.iter().map(|(_, m)| *m).sum();
                format!("gpu_pool<devices={}, memory_mib={memory_mib}>", devices.len())
            }
            RuntimeValue::VirtualCluster { nodes } => {
                let cores: u128 = nodes.iter().map(|(_, c, _)| *c).sum();
                let memory_mib: u128 = nodes.iter().map(|(_, _, m)| *m).sum();
                format!("virtual_cluster<nodes={}, cores={}, memory_mib={}>" , nodes.len(), cores, memory_mib)
            }
            RuntimeValue::DistributedMemory { memory_mib } => format!("distributed_memory<memory_mib={memory_mib}>"),
            RuntimeValue::DistributedGpuMemory { memory_mib } => format!("distributed_gpu_memory<memory_mib={memory_mib}>"),
            RuntimeValue::GpuValue { inner, memory_mib } => format!("gpu_value<memory_mib={memory_mib}>({})", inner.display_atom()),
            RuntimeValue::GpuKernel { inner } => format!("gpu_kernel({})", inner.display_atom()),
            RuntimeValue::MemoryCheckpoint { memory_mib } => format!("memory_checkpoint<memory_mib={memory_mib}>"),
            RuntimeValue::RemoteCheckpoint { arch, inner } => format!("remote_checkpoint[{arch}]({})", inner.display_atom()),
            RuntimeValue::PortableCode { inner } => format!("portable_code({})", inner.display_atom()),
            RuntimeValue::Remote { arch, inner } => format!("remote[{arch}]({})", inner.display_atom()),
        }
    }
}
