use std::collections::BTreeMap;

use crate::ast::*;
use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::*;
use crate::policy;
pub use crate::policy::CheckPolicy;

#[derive(Debug, Clone)]
pub struct CheckReport {
    pub module_name: String,
    pub theory_count: usize,
    pub value_count: usize,
    pub inferred: Vec<(String, Passport)>,
    pub bridges: Vec<BridgeDecl>,
    pub diagnostics: Vec<Diagnostic>,
}

impl CheckReport {
    pub fn ok(&self) -> bool {
        self.diagnostics.is_empty()
    }
}

#[derive(Default)]
pub struct Checker {
    policy: CheckPolicy,
    theories: BTreeMap<String, BTreeMap<String, Passport>>,
    bridges: Vec<BridgeDecl>,
    inferred: Vec<(String, Passport)>,
    diagnostics: Vec<Diagnostic>,
}

impl Checker {
    pub fn new() -> Self {
        Self::with_policy(CheckPolicy::default())
    }

    pub fn with_policy(policy: CheckPolicy) -> Self {
        Self { policy, ..Self::default() }
    }

    pub fn check_module(mut self, module: &Module) -> CheckReport {
        self.bridges = module.items.iter().filter_map(|item| match item {
            ModuleItem::Bridge(bridge) => Some(bridge.clone()),
            _ => None,
        }).collect();

        let mut theory_count = 0usize;
        for item in &module.items {
            if let ModuleItem::Theory(theory) = item {
                theory_count += 1;
                self.check_theory(theory);
            }
        }

        CheckReport {
            module_name: module.name.clone(),
            theory_count,
            value_count: self.inferred.len(),
            inferred: self.inferred,
            bridges: self.bridges,
            diagnostics: self.diagnostics,
        }
    }

    fn check_theory(&mut self, theory: &TheoryDecl) {
        self.theories.entry(theory.name.clone()).or_default();
        for item in &theory.items {
            match item {
                TheoryItem::Let(let_decl) => {
                    match self.infer_expr(&let_decl.expr, &theory.name) {
                        Ok(passport) => {
                            if let Err(diag) = self.validate_policy(&passport, let_decl.line) {
                                self.diagnostics.push(diag);
                            }
                            self.theories.entry(theory.name.clone()).or_default().insert(let_decl.name.clone(), passport.clone());
                            self.inferred.push((format!("{}.{}", theory.name, let_decl.name), passport));
                        }
                        Err(diag) => self.diagnostics.push(diag),
                    }
                }
                TheoryItem::Expr(expr) => {
                    match self.infer_expr(expr, &theory.name) {
                        Ok(passport) => {
                            if let Err(diag) = self.validate_policy(&passport, expr.line) {
                                self.diagnostics.push(diag);
                            }
                        }
                        Err(diag) => self.diagnostics.push(diag),
                    }
                }
            }
        }
    }

    fn infer_expr(&self, expr: &Expr, ambient_theory: &str) -> Result<Passport, Diagnostic> {
        match &expr.kind {
            ExprKind::IntLiteral(value) => {
                if value.len() <= 18 {
                    Ok(Passport::literal_nat(ambient_theory))
                } else {
                    Ok(Passport::compressed_nat(ambient_theory))
                }
            }
            ExprKind::Ident(name) => self.lookup_ident(name, ambient_theory, expr.line),
            ExprKind::QualifiedIdent { theory, name } => self.lookup_qualified(theory, name, ambient_theory, expr.line),
            ExprKind::Power { base, exp } => {
                let base = self.infer_expr(base, ambient_theory)?;
                let exp = self.infer_expr(exp, ambient_theory)?;
                self.require_capability(&base, Capability::CanAddAsNat, expr.line, "power base must be Nat-like")?;
                self.require_capability(&exp, Capability::CanAddAsNat, expr.line, "power exponent must be Nat-like")?;
                Ok(Passport::compressed_nat(ambient_theory))
            }
            ExprKind::Add { lhs, rhs } => {
                let lhs = self.infer_expr(lhs, ambient_theory)?;
                let rhs = self.infer_expr(rhs, ambient_theory)?;
                self.require_capability(&lhs, Capability::CanAddAsNat, expr.line, "left operand of + must support Nat addition")?;
                self.require_capability(&rhs, Capability::CanAddAsNat, expr.line, "right operand of + must support Nat addition")?;
                Ok(Passport::add_result(&lhs, &rhs, ambient_theory))
            }
            ExprKind::CompareGt { lhs, rhs } => {
                let lhs = self.infer_expr(lhs, ambient_theory)?;
                let rhs = self.infer_expr(rhs, ambient_theory)?;
                let direct = lhs.capabilities.contains(Capability::CanCompareDirect)
                    && rhs.capabilities.contains(Capability::CanCompareDirect);
                let by_proof = lhs.capabilities.contains(Capability::CanCompareByProof)
                    && rhs.capabilities.contains(Capability::CanCompareByProof);
                if !direct && !by_proof {
                    return Err(Diagnostic::error(
                        DiagnosticKind::AccessError,
                        Some(expr.line),
                        "comparison requires can_compare_direct or can_compare_by_proof on both operands",
                    ).with_help(format!("left: {lhs}\n  right: {rhs}")));
                }
                Ok(Passport {
                    ty: TypeKind::Bool,
                    construction: lhs.construction.max(rhs.construction),
                    capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
                    cost: lhs.cost.max(rhs.cost),
                    trust: lhs.trust.max(rhs.trust),
                    provenance: lhs.provenance.max(rhs.provenance),
                    validation: lhs.validation.max(rhs.validation),
                    theory: TheoryContext::new(ambient_theory),
                    history: HistoryChain::merge2(&lhs.history, &rhs.history, "compare:gt"),
                    location: LocationContext::local(),
                })
            }
            ExprKind::Call { name, args } => self.infer_call(name, args, ambient_theory, expr.line),
        }
    }

    fn infer_call(&self, name: &str, args: &[Expr], ambient_theory: &str, line: usize) -> Result<Passport, Diagnostic> {
        match name {
            "universe" => Err(Diagnostic::error(
                DiagnosticKind::UniverseLevelError,
                Some(line),
                "ambiguous universe is not allowed",
            ).with_help("use an explicit universe constructor: U0(), U1(), U2(), or universe_succ(U n)")),
            "U0" | "universe0" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(Passport::universe(ambient_theory, 0))
            }
            "U1" | "universe1" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(Passport::universe(ambient_theory, 1))
            }
            "U2" | "universe2" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(Passport::universe(ambient_theory, 2))
            }
            "universe_succ" | "next_universe" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one Universe argument")));
                }
                let value = self.infer_expr(&args[0], ambient_theory)?;
                self.require_universe(&value, line, "universe_succ requires a Universe<U n> value")?;
                self.require_capability(&value, Capability::CanLiftUniverse, line, "universe_succ requires can_lift_universe")?;
                Ok(Passport::universe_succ(&value, ambient_theory))
            }
            "set_of" | "set_from_universe" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one Universe argument")));
                }
                let value = self.infer_expr(&args[0], ambient_theory)?;
                self.require_universe(&value, line, "set_of requires a Universe<U n>; sets cannot be formed from Set/Class values directly")?;
                self.require_capability(&value, Capability::CanFormSet, line, "set_of requires can_form_set")?;
                let level = match &value.ty {
                    TypeKind::Universe { level } => *level,
                    _ => unreachable!("require_universe checked type"),
                };
                if level == u8::MAX {
                    return Err(Diagnostic::error(
                        DiagnosticKind::UniverseLevelError,
                        Some(line),
                        "cannot form Set<U255 -> U256> in MVP universe hierarchy",
                    ).with_help("MVP uses u8 universe levels; choose a smaller universe level."));
                }
                Ok(Passport::set_of_universe(&value, ambient_theory))
            }
            "class_of" | "class_from_universe" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one Universe argument")));
                }
                let value = self.infer_expr(&args[0], ambient_theory)?;
                self.require_universe(&value, line, "class_of requires a Universe<U n>; classes are meta-level views of universes")?;
                self.require_capability(&value, Capability::CanFormClass, line, "class_of requires can_form_class")?;
                Ok(Passport::class_of_universe(&value, ambient_theory))
            }
            "set_lives_in" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "set_lives_in expects one Set argument"));
                }
                let value = self.infer_expr(&args[0], ambient_theory)?;
                if !matches!(value.ty, TypeKind::Set { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::UniverseLevelError,
                        Some(line),
                        "set_lives_in requires Set<U n -> U n+1>",
                    ).with_help(format!("value passport: {value}")));
                }
                self.require_capability(&value, Capability::CanSetReason, line, "set_lives_in requires can_set_reason")?;
                Ok(Passport::universe_level_nat(&value, ambient_theory, "set:lives_in_level"))
            }
            "class_level" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "class_level expects one Class argument"));
                }
                let value = self.infer_expr(&args[0], ambient_theory)?;
                if !matches!(value.ty, TypeKind::Class { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::UniverseLevelError,
                        Some(line),
                        "class_level requires Class<U n>",
                    ).with_help(format!("value passport: {value}")));
                }
                self.require_capability(&value, Capability::CanClassReason, line, "class_level requires can_class_reason")?;
                Ok(Passport::universe_level_nat(&value, ambient_theory, "class:level"))
            }
            "set_of_all_sets" | "russell_set" => Err(Diagnostic::error(
                DiagnosticKind::UniverseLevelError,
                Some(line),
                "set_of_all_sets is not a valid object in the current universe hierarchy",
            ).with_help("Use class_of(U n) for a proper-class/meta-level view, or set_of(U n) to form a set in U n+1 from one explicit universe level.")),

            "language_L0" | "L0_language" | "language_core" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::DefinabilityError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(Passport::language(ambient_theory, "L0"))
            }
            "encoding_godel" | "godel_encoding" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::DefinabilityError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(Passport::encoding(ambient_theory, "Godel"))
            }
            "meta_level" | "M" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::DefinabilityError, Some(line), format!("{name} expects one literal Nat level")));
                }
                let raw = Self::literal_u128(&args[0]).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::DefinabilityError,
                    Some(line),
                    "meta_level requires a literal Nat in MVP",
                ))?;
                let level: u8 = raw.try_into().map_err(|_| Diagnostic::error(
                    DiagnosticKind::DefinabilityError,
                    Some(line),
                    "meta_level is limited to u8 in MVP",
                ))?;
                Ok(Passport::meta_level(ambient_theory, level, None))
            }
            "definable_nat" | "define_nat" => {
                if args.len() != 4 {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DefinabilityError,
                        Some(line),
                        "definable_nat requires explicit language, encoding, bound and meta-level",
                    ).with_help("use: definable_nat(language_L0(), encoding_godel(), 20, meta_level(1))"));
                }
                let language = self.infer_expr(&args[0], ambient_theory)?;
                self.require_language(&language, line, "definable_nat requires Language as first argument")?;
                self.require_capability(&language, Capability::CanDefineInLanguage, line, "definable_nat language requires can_define_in_language")?;

                let encoding = self.infer_expr(&args[1], ambient_theory)?;
                self.require_encoding(&encoding, line, "definable_nat requires Encoding as second argument")?;
                self.require_capability(&encoding, Capability::CanUseEncoding, line, "definable_nat encoding requires can_use_encoding")?;

                let bound = Self::literal_u128(&args[2]).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::DefinabilityError,
                    Some(line),
                    "definable_nat bound must be a literal Nat in MVP",
                ))?;
                if bound == 0 {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DefinabilityError,
                        Some(line),
                        "definable_nat bound must be greater than zero",
                    ));
                }

                let meta = self.infer_expr(&args[3], ambient_theory)?;
                self.require_meta_level(&meta, line, "definable_nat requires MetaLevel as fourth argument")?;
                self.require_capability(&meta, Capability::CanMetaLevelReason, line, "definable_nat meta-level requires can_meta_level_reason")?;

                Ok(Passport::definable_nat(ambient_theory, &language, &encoding, bound, &meta))
            }
            "definability_bound" | "definition_bound" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::DefinabilityError, Some(line), format!("{name} expects one DefinableNat argument")));
                }
                let value = self.infer_expr(&args[0], ambient_theory)?;
                self.require_definable_nat(&value, line, "definability_bound requires DefinableNat")?;
                self.require_capability(&value, Capability::CanExtractDefinabilityBound, line, "definability_bound requires can_extract_definability_bound")?;
                Ok(Passport::definability_resource_nat(ambient_theory, &value, "definability:bound"))
            }
            "definability_meta_level" | "definition_meta_level" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::DefinabilityError, Some(line), format!("{name} expects one DefinableNat argument")));
                }
                let value = self.infer_expr(&args[0], ambient_theory)?;
                self.require_definable_nat(&value, line, "definability_meta_level requires DefinableNat")?;
                self.require_capability(&value, Capability::CanExtractDefinabilityMeta, line, "definability_meta_level requires can_extract_definability_meta")?;
                Ok(Passport::definability_resource_nat(ambient_theory, &value, "definability:meta_level"))
            }
            "berry_number" | "smallest_undefinable" | "undefinable_nat" => Err(Diagnostic::error(
                DiagnosticKind::DefinabilityError,
                Some(line),
                "bare undefinability/Berry-style construction is not allowed",
            ).with_help("Definability must be relative: Definable<n, language, encoding, theory, bound, meta-level>. Use definable_nat(language_L0(), encoding_godel(), bound, meta_level(k)) for explicit definability passports.")),

            "big_number" | "huge_number" => Err(Diagnostic::error(
                DiagnosticKind::BigNumberError,
                Some(line),
                "bare big_number is not allowed",
            ).with_help("use an explicit constructor: Graham(), TREE(n), BB(n), or fast_growing(level)")),
            "Graham" | "graham" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::BigNumberError, Some(line), "Graham expects no arguments"));
                }
                Ok(Passport::graham_nat(ambient_theory))
            }
            "TREE" | "tree" | "tree_number" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::BigNumberError, Some(line), "TREE expects one positive literal Nat parameter in MVP"));
                }
                let parameter = Self::literal_u128(&args[0]).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::BigNumberError,
                    Some(line),
                    "TREE parameter must be a literal Nat in MVP",
                ))?;
                if parameter == 0 {
                    return Err(Diagnostic::error(DiagnosticKind::BigNumberError, Some(line), "TREE parameter must be greater than zero"));
                }
                let arg = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&arg, Capability::CanAddAsNat, line, "TREE argument must be Nat-like")?;
                Ok(Passport::tree_nat(ambient_theory, parameter))
            }
            "BB" | "busy_beaver" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::BigNumberError, Some(line), "BB expects one positive literal Nat parameter in MVP"));
                }
                let parameter = Self::literal_u128(&args[0]).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::BigNumberError,
                    Some(line),
                    "BB parameter must be a literal Nat in MVP",
                ))?;
                if parameter == 0 {
                    return Err(Diagnostic::error(DiagnosticKind::BigNumberError, Some(line), "BB parameter must be greater than zero"));
                }
                let arg = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&arg, Capability::CanAddAsNat, line, "BB argument must be Nat-like")?;
                Ok(Passport::busy_beaver_nat(ambient_theory, Some(parameter)))
            }
            "fast_growing" | "FGH" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::BigNumberError, Some(line), "fast_growing expects one positive literal Nat level in MVP"));
                }
                let level = Self::literal_u128(&args[0]).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::BigNumberError,
                    Some(line),
                    "fast_growing level must be a literal Nat in MVP",
                ))?;
                if level == 0 {
                    return Err(Diagnostic::error(DiagnosticKind::BigNumberError, Some(line), "fast_growing level must be greater than zero"));
                }
                Ok(Passport::fast_growing_nat(ambient_theory, level))
            }
            "growth_parameter" | "big_number_parameter" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::BigNumberError, Some(line), format!("{name} expects one BigNat argument")));
                }
                let value = self.infer_expr(&args[0], ambient_theory)?;
                self.require_big_nat(&value, line, "growth_parameter requires BigNat")?;
                self.require_capability(&value, Capability::CanExtractGrowthClass, line, "growth_parameter requires can_extract_growth_class")?;
                Ok(Passport::big_number_resource_nat(ambient_theory, &value, "big_number:parameter"))
            }
            "infinity" => Err(Diagnostic::error(
                DiagnosticKind::InfinityModeError,
                Some(line),
                "ambiguous infinity is not allowed",
            ).with_help("use a typed infinity constructor: aleph0()/infinity_cardinal(), omega()/infinity_ordinal(), limit_omega(), potential_infinity(), class_infinity(class_of(U0())), or universe_infinity(U0())")),
            "aleph0" | "infinity_cardinal" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(Passport::cardinal_infinity(ambient_theory))
            }
            "omega" | "infinity_ordinal" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(Passport::ordinal_infinity(ambient_theory))
            }
            "limit_omega" | "infinity_limit" | "limit_infinity" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(Passport::limit_infinity(ambient_theory))
            }
            "potential_infinity" | "infinity_potential" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(Passport::potential_infinity(ambient_theory))
            }
            "class_infinity" | "proper_class_infinity" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one Class argument")));
                }
                let value = self.infer_expr(&args[0], ambient_theory)?;
                self.require_class(&value, line, "class_infinity requires Class<U n>")?;
                self.require_capability(&value, Capability::CanClassReason, line, "class_infinity requires can_class_reason")?;
                Ok(Passport::class_infinity(ambient_theory, &value))
            }
            "universe_infinity" | "infinity_universe" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one Universe argument")));
                }
                let value = self.infer_expr(&args[0], ambient_theory)?;
                self.require_universe(&value, line, "universe_infinity requires Universe<U n>")?;
                self.require_capability(&value, Capability::CanUniverseLevel, line, "universe_infinity requires can_universe_level")?;
                Ok(Passport::universe_infinity(ambient_theory, &value))
            }
            "cardinal_succ" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "cardinal_succ expects one cardinal infinity argument"));
                }
                let value = self.infer_expr(&args[0], ambient_theory)?;
                self.require_infinity_mode(&value, InfinityMode::Cardinal, line, "cardinal_succ requires Infinity<cardinal>")?;
                self.require_capability(&value, Capability::CanCardinalArithmetic, line, "cardinal_succ requires can_cardinal_arithmetic")?;
                Ok(Passport::infinity_succ_result(&value, ambient_theory))
            }
            "ordinal_succ" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "ordinal_succ expects one ordinal infinity argument"));
                }
                let value = self.infer_expr(&args[0], ambient_theory)?;
                self.require_infinity_mode(&value, InfinityMode::Ordinal, line, "ordinal_succ requires Infinity<ordinal>")?;
                self.require_capability(&value, Capability::CanOrdinalArithmetic, line, "ordinal_succ requires can_ordinal_arithmetic")?;
                Ok(Passport::infinity_succ_result(&value, ambient_theory))
            }
            "cardinal_add" | "cardinal_sum" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects two cardinal infinity arguments")));
                }
                let lhs = self.infer_expr(&args[0], ambient_theory)?;
                let rhs = self.infer_expr(&args[1], ambient_theory)?;
                self.require_infinity_mode(&lhs, InfinityMode::Cardinal, line, "cardinal_add requires Infinity<cardinal> on the left")?;
                self.require_infinity_mode(&rhs, InfinityMode::Cardinal, line, "cardinal_add requires Infinity<cardinal> on the right")?;
                self.require_capability(&lhs, Capability::CanCardinalArithmetic, line, "cardinal_add requires can_cardinal_arithmetic on the left")?;
                self.require_capability(&rhs, Capability::CanCardinalArithmetic, line, "cardinal_add requires can_cardinal_arithmetic on the right")?;
                Ok(Passport::infinity_binary_result(&lhs, &rhs, ambient_theory, "infinity:cardinal_add"))
            }
            "ordinal_add" | "ordinal_sum" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects two ordinal infinity arguments")));
                }
                let lhs = self.infer_expr(&args[0], ambient_theory)?;
                let rhs = self.infer_expr(&args[1], ambient_theory)?;
                self.require_infinity_mode(&lhs, InfinityMode::Ordinal, line, "ordinal_add requires Infinity<ordinal> on the left")?;
                self.require_infinity_mode(&rhs, InfinityMode::Ordinal, line, "ordinal_add requires Infinity<ordinal> on the right")?;
                self.require_capability(&lhs, Capability::CanOrdinalArithmetic, line, "ordinal_add requires can_ordinal_arithmetic on the left")?;
                self.require_capability(&rhs, Capability::CanOrdinalArithmetic, line, "ordinal_add requires can_ordinal_arithmetic on the right")?;
                Ok(Passport::infinity_binary_result(&lhs, &rhs, ambient_theory, "infinity:ordinal_add"))
            }
            "potential_step" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "potential_step expects one potential infinity argument"));
                }
                let value = self.infer_expr(&args[0], ambient_theory)?;
                self.require_infinity_mode(&value, InfinityMode::Potential, line, "potential_step requires Infinity<potential>")?;
                self.require_capability(&value, Capability::CanLimitReason, line, "potential_step requires can_limit_reason")?;
                Ok(Passport::infinity_succ_result(&value, ambient_theory))
            }
            "node_x86" | "node_x86_64" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(Passport::node(ambient_theory, NodeArch::X86_64))
            }
            "node_arm" | "node_aarch64" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(Passport::node(ambient_theory, NodeArch::Aarch64))
            }
            "node_x86_with" | "node_x86_64_with" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects cores and memory_mib")));
                }
                let cores = Self::literal_u128(&args[0]).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::DistributedResourceError,
                    Some(line),
                    "node_x86_64_with requires literal cores in MVP",
                ))?;
                let memory_mib = Self::literal_u128(&args[1]).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::DistributedResourceError,
                    Some(line),
                    "node_x86_64_with requires literal memory_mib in MVP",
                ))?;
                Self::require_positive_resource(cores, "cores", line)?;
                Self::require_positive_resource(memory_mib, "memory_mib", line)?;
                Ok(Passport::node_with_resources(ambient_theory, NodeArch::X86_64, Some(cores), Some(memory_mib)))
            }
            "node_arm_with" | "node_aarch64_with" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects cores and memory_mib")));
                }
                let cores = Self::literal_u128(&args[0]).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::DistributedResourceError,
                    Some(line),
                    "node_aarch64_with requires literal cores in MVP",
                ))?;
                let memory_mib = Self::literal_u128(&args[1]).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::DistributedResourceError,
                    Some(line),
                    "node_aarch64_with requires literal memory_mib in MVP",
                ))?;
                Self::require_positive_resource(cores, "cores", line)?;
                Self::require_positive_resource(memory_mib, "memory_mib", line)?;
                Ok(Passport::node_with_resources(ambient_theory, NodeArch::Aarch64, Some(cores), Some(memory_mib)))
            }
            "gpu_cuda" | "gpu_device_cuda" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(Passport::gpu_device_with(ambient_theory, GpuBackend::Cuda, Some(8192)))
            }
            "gpu_rocm" | "gpu_device_rocm" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(Passport::gpu_device_with(ambient_theory, GpuBackend::Rocm, Some(8192)))
            }
            "gpu_cuda_with" | "gpu_device_cuda_with" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects memory_mib")));
                }
                let memory_mib = Self::literal_u128(&args[0]).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::DistributedResourceError,
                    Some(line),
                    "gpu_cuda_with requires literal memory_mib in MVP",
                ))?;
                Self::require_positive_resource(memory_mib, "gpu_memory_mib", line)?;
                Ok(Passport::gpu_device_with(ambient_theory, GpuBackend::Cuda, Some(memory_mib)))
            }
            "gpu_rocm_with" | "gpu_device_rocm_with" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects memory_mib")));
                }
                let memory_mib = Self::literal_u128(&args[0]).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::DistributedResourceError,
                    Some(line),
                    "gpu_rocm_with requires literal memory_mib in MVP",
                ))?;
                Self::require_positive_resource(memory_mib, "gpu_memory_mib", line)?;
                Ok(Passport::gpu_device_with(ambient_theory, GpuBackend::Rocm, Some(memory_mib)))
            }
            "gpu_pool" | "virtual_gpu_pool" => {
                if args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects at least one GpuDevice argument")));
                }
                let mut devices = Vec::new();
                for arg in args {
                    let device = self.infer_expr(arg, ambient_theory)?;
                    self.require_capability(&device, Capability::CanHostGpuRuntime, line, "gpu_pool requires GPU devices with can_host_gpu_runtime")?;
                    if !matches!(device.ty, TypeKind::GpuDevice { .. }) {
                        return Err(Diagnostic::error(
                            DiagnosticKind::DistributedResourceError,
                            Some(line),
                            "gpu_pool accepts only GpuDevice<backend> values",
                        ).with_help(format!("value passport: {device}")));
                    }
                    devices.push(device);
                }
                Ok(Passport::gpu_pool(ambient_theory, &devices))
            }
            "distributed_gpu_memory" | "allocate_gpu_memory" | "gpu_memory_region" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects gpu_pool and memory_mib")));
                }
                let pool = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&pool, Capability::CanAllocateGpuMemory, line, "distributed_gpu_memory requires a GpuPool with can_allocate_gpu_memory")?;
                if !matches!(pool.ty, TypeKind::GpuPool) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "distributed_gpu_memory expects a GpuPool as its first argument",
                    ).with_help(format!("pool passport: {pool}")));
                }
                let requested_mib = Self::literal_u128(&args[1]).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::DistributedResourceError,
                    Some(line),
                    "distributed_gpu_memory requires literal memory_mib in MVP",
                ))?;
                Self::require_positive_resource(requested_mib, "gpu_memory_mib", line)?;
                if let Some(total_mib) = pool.history.total_gpu_memory_mib() {
                    if requested_mib > total_mib {
                        return Err(Diagnostic::error(
                            DiagnosticKind::DistributedResourceError,
                            Some(line),
                            format!("distributed_gpu_memory request {requested_mib} MiB exceeds GpuPool memory {total_mib} MiB"),
                        ).with_help("request a smaller GPU region or add more GPU memory-capable devices to the pool"));
                    }
                }
                Ok(Passport::distributed_gpu_memory_region(ambient_theory, &pool, requested_mib))
            }
            "gpu_memory_mib" | "distributed_gpu_memory_mib" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one DistributedGpuMemory argument")));
                }
                let region = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&region, Capability::CanUseGpuMemory, line, "gpu_memory_mib requires can_use_gpu_memory")?;
                if !matches!(region.ty, TypeKind::DistributedGpuMemory { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "gpu_memory_mib expects a DistributedGpuMemory value",
                    ).with_help(format!("value passport: {region}")));
                }
                Ok(Passport::gpu_memory_resource_nat(ambient_theory, &region, "gpu_memory:region_mib"))
            }
            "copy_to_gpu" | "gpu_upload" | "upload_to_gpu" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects source value and DistributedGpuMemory region")));
                }
                let source = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&source, Capability::CanSerializeForMigration, line, "copy_to_gpu source requires can_serialize_for_migration")?;
                let region = self.infer_expr(&args[1], ambient_theory)?;
                self.require_capability(&region, Capability::CanUseGpuMemory, line, "copy_to_gpu requires can_use_gpu_memory on the target region")?;
                self.require_capability(&region, Capability::CanCopyCpuToGpu, line, "copy_to_gpu target region requires can_copy_cpu_to_gpu")?;
                if !matches!(region.ty, TypeKind::DistributedGpuMemory { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "copy_to_gpu expects a DistributedGpuMemory region as second argument",
                    ).with_help(format!("region passport: {region}")));
                }
                Ok(Passport::gpu_value(ambient_theory, &source, &region))
            }
            "copy_from_gpu" | "gpu_download" | "download_from_gpu" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one GpuValue")));
                }
                let value = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&value, Capability::CanCopyGpuToCpu, line, "copy_from_gpu requires can_copy_gpu_to_cpu")?;
                if !matches!(value.ty, TypeKind::GpuValue { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "copy_from_gpu expects a GpuValue<T>",
                    ).with_help(format!("value passport: {value}")));
                }
                Ok(Passport::copied_from_gpu(ambient_theory, &value))
            }
            "compile_gpu_kernel" | "gpu_kernel" | "make_gpu_kernel" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one compilable value")));
                }
                let source = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&source, Capability::CanCompileGpuKernel, line, "compile_gpu_kernel requires can_compile_gpu_kernel")?;
                self.require_capability(&source, Capability::CanSerializeForMigration, line, "compile_gpu_kernel requires can_serialize_for_migration")?;
                Ok(Passport::gpu_kernel(ambient_theory, &source))
            }
            "launch_kernel" | "launch_gpu_kernel" | "gpu_launch" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects DistributedGpuMemory and GpuKernel")));
                }
                let region = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&region, Capability::CanUseGpuMemory, line, "launch_kernel requires can_use_gpu_memory on the target region")?;
                self.require_capability(&region, Capability::CanLaunchGpuKernel, line, "launch_kernel target region requires can_launch_gpu_kernel")?;
                if !matches!(region.ty, TypeKind::DistributedGpuMemory { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "launch_kernel expects a DistributedGpuMemory region as first argument",
                    ).with_help(format!("region passport: {region}")));
                }
                let kernel = self.infer_expr(&args[1], ambient_theory)?;
                self.require_capability(&kernel, Capability::CanLaunchGpuKernel, line, "launch_kernel requires a GpuKernel with can_launch_gpu_kernel")?;
                if !matches!(kernel.ty, TypeKind::GpuKernel { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "launch_kernel expects a GpuKernel<T> as second argument",
                    ).with_help(format!("kernel passport: {kernel}")));
                }
                Ok(Passport::launched_gpu_kernel(ambient_theory, &region, &kernel))
            }
            "virtual_pool" | "virtual_cluster" => {
                if args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects at least one Node argument")));
                }
                let mut nodes = Vec::new();
                for arg in args {
                    let node = self.infer_expr(arg, ambient_theory)?;
                    self.require_capability(&node, Capability::CanHostRuntime, line, "virtual_pool requires nodes with can_host_runtime")?;
                    if !matches!(node.ty, TypeKind::Node { .. }) {
                        return Err(Diagnostic::error(
                            DiagnosticKind::DistributedResourceError,
                            Some(line),
                            "virtual_pool accepts only Node<x86_64> or Node<aarch64> values",
                        ).with_help(format!("value passport: {node}")));
                    }
                    nodes.push(node);
                }
                Ok(Passport::virtual_cluster(ambient_theory, &nodes))
            }
            "pool_cores" | "cluster_cores" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one VirtualCluster argument")));
                }
                let pool = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&pool, Capability::CanVirtualizeCores, line, "pool_cores requires can_virtualize_cores")?;
                if !matches!(pool.ty, TypeKind::VirtualCluster) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "pool_cores expects a VirtualCluster value",
                    ).with_help(format!("value passport: {pool}")));
                }
                Ok(Passport::cluster_resource_nat(ambient_theory, &pool, "cluster:pool_cores"))
            }
            "pool_memory_mib" | "cluster_memory_mib" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one VirtualCluster argument")));
                }
                let pool = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&pool, Capability::CanVirtualizeMemory, line, "pool_memory_mib requires can_virtualize_memory")?;
                if !matches!(pool.ty, TypeKind::VirtualCluster) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "pool_memory_mib expects a VirtualCluster value",
                    ).with_help(format!("value passport: {pool}")));
                }
                Ok(Passport::cluster_resource_nat(ambient_theory, &pool, "cluster:pool_memory_mib"))
            }
            "distributed_memory" | "allocate_memory" | "memory_region" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects pool and memory_mib")));
                }
                let pool = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&pool, Capability::CanAllocateDistributedMemory, line, "distributed_memory requires a VirtualCluster with can_allocate_distributed_memory")?;
                if !matches!(pool.ty, TypeKind::VirtualCluster) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "distributed_memory expects a VirtualCluster as its first argument",
                    ).with_help(format!("pool passport: {pool}")));
                }
                let requested_mib = Self::literal_u128(&args[1]).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::DistributedResourceError,
                    Some(line),
                    "distributed_memory requires literal memory_mib in MVP",
                ))?;
                Self::require_positive_resource(requested_mib, "memory_mib", line)?;
                if let Some(total_mib) = pool.history.total_node_memory_mib() {
                    if requested_mib > total_mib {
                        return Err(Diagnostic::error(
                            DiagnosticKind::DistributedResourceError,
                            Some(line),
                            format!("distributed_memory request {requested_mib} MiB exceeds VirtualCluster memory {total_mib} MiB"),
                        ).with_help("request a smaller region or add more memory-capable nodes to the pool"));
                    }
                }
                Ok(Passport::distributed_memory_region(ambient_theory, &pool, requested_mib))
            }
            "memory_region_mib" | "distributed_memory_mib" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one DistributedMemory argument")));
                }
                let region = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&region, Capability::CanUseDistributedMemory, line, "memory_region_mib requires can_use_distributed_memory")?;
                if !matches!(region.ty, TypeKind::DistributedMemory { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "memory_region_mib expects a DistributedMemory value",
                    ).with_help(format!("value passport: {region}")));
                }
                Ok(Passport::memory_region_resource_nat(ambient_theory, &region, "memory:region_mib"))
            }
            "checkpoint_memory" | "checkpoint" | "checkpoint_region" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one DistributedMemory argument")));
                }
                let region = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&region, Capability::CanCheckpointMemory, line, "checkpoint_memory requires can_checkpoint_memory")?;
                if !matches!(region.ty, TypeKind::DistributedMemory { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "checkpoint_memory expects a DistributedMemory value",
                    ).with_help(format!("value passport: {region}")));
                }
                Ok(Passport::memory_checkpoint(ambient_theory, &region))
            }
            "restore_checkpoint" | "restore_memory" | "restore" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one MemoryCheckpoint argument")));
                }
                let checkpoint = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&checkpoint, Capability::CanRestoreCheckpoint, line, "restore_checkpoint requires can_restore_checkpoint")?;
                if !matches!(checkpoint.ty, TypeKind::MemoryCheckpoint { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "restore_checkpoint expects a MemoryCheckpoint value",
                    ).with_help(format!("value passport: {checkpoint}")));
                }
                Ok(Passport::restored_memory_region(ambient_theory, &checkpoint))
            }
            "compile_portable" | "portable_code" | "make_portable" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one serializable value")));
                }
                let source = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&source, Capability::CanCompilePortableCode, line, "compile_portable requires can_compile_portable_code")?;
                self.require_capability(&source, Capability::CanSerializeForMigration, line, "compile_portable requires can_serialize_for_migration")?;
                Ok(Passport::portable_code(ambient_theory, &source))
            }
            "deploy_portable" | "deploy_code" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects target node and PortableCode")));
                }
                let target = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&target, Capability::CanAcceptMigration, line, "deploy_portable target requires can_accept_migration")?;
                let target_arch = Self::node_arch(&target).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::MigrationBridgeError,
                    Some(line),
                    "deploy_portable target must be Node<x86_64> or Node<aarch64>",
                ).with_help(format!("target passport: {target}")))?;
                let code = self.infer_expr(&args[1], ambient_theory)?;
                self.require_capability(&code, Capability::CanDeployPortableCode, line, "deploy_portable requires can_deploy_portable_code")?;
                if !matches!(code.ty, TypeKind::PortableCode { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "deploy_portable expects a PortableCode<T> value",
                    ).with_help(format!("code passport: {code}")));
                }
                Ok(Passport::deployed_portable_code(ambient_theory, &code, &target, target_arch, format!("portable:deploy:to:{target_arch}")))
            }
            "deploy_on" | "deploy_to_pool" => {
                if args.len() != 3 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects pool, target node and PortableCode")));
                }
                let pool = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&pool, Capability::CanScheduleRuntime, line, "deploy_on requires a VirtualCluster with can_schedule_runtime")?;
                if !matches!(pool.ty, TypeKind::VirtualCluster) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "deploy_on expects a VirtualCluster as its first argument",
                    ).with_help(format!("pool passport: {pool}")));
                }
                let target = self.infer_expr(&args[1], ambient_theory)?;
                self.require_capability(&target, Capability::CanAcceptMigration, line, "deploy_on target requires can_accept_migration")?;
                let target_arch = Self::node_arch(&target).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::MigrationBridgeError,
                    Some(line),
                    "deploy_on target must be Node<x86_64> or Node<aarch64>",
                ).with_help(format!("target passport: {target}")))?;
                let code = self.infer_expr(&args[2], ambient_theory)?;
                self.require_capability(&code, Capability::CanDeployPortableCode, line, "deploy_on requires can_deploy_portable_code")?;
                if !matches!(code.ty, TypeKind::PortableCode { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "deploy_on expects PortableCode<T> as its third argument",
                    ).with_help(format!("code passport: {code}")));
                }
                let mut deployed = Passport::deployed_portable_code(ambient_theory, &code, &target, target_arch, format!("portable:deploy_on:to:{target_arch}"));
                deployed.trust = deployed.trust.max(pool.trust);
                deployed.provenance = deployed.provenance.max(pool.provenance);
                deployed.validation = deployed.validation.max(pool.validation);
                deployed.cost = deployed.cost.max(pool.cost);
                deployed.history = HistoryChain::merge_many([&pool.history, &code.history, &target.history], format!("portable:deploy_on:to:{target_arch}"));
                Ok(deployed)
            }
            "materialize_remote" | "materialize" | "fetch_remote" | "collect_remote" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one Remote value")));
                }

                let (source_theory, remote) = match &args[0].kind {
                    ExprKind::QualifiedIdent { theory: source, name } => {
                        (source.clone(), self.lookup_qualified(source, name, source, line)?)
                    }
                    _ => (ambient_theory.to_string(), self.infer_expr(&args[0], ambient_theory)?),
                };

                if source_theory != ambient_theory {
                    let Some(bridge) = self.find_bridge(&source_theory, ambient_theory, BridgeKind::Materialize) else {
                        return Err(Diagnostic::error(
                            DiagnosticKind::TheoryBridgeError,
                            Some(line),
                            format!("no materialize bridge from theory {source_theory} to {ambient_theory} in scope"),
                        ).with_help("declare or import: bridge Name : Source -> Target { kind = materialize }"));
                    };
                    self.require_capability(&remote, Capability::CanMaterializeRemote, line, "materialize_remote requires can_materialize_remote")?;
                    if !matches!(remote.ty, TypeKind::Remote { .. }) {
                        return Err(Diagnostic::error(
                            DiagnosticKind::DistributedResourceError,
                            Some(line),
                            "materialize_remote expects a Remote<T@arch> value",
                        ).with_help(format!("value passport: {remote}")));
                    }
                    return Ok(Passport::materialized_remote(ambient_theory, &remote, &bridge.name));
                }

                self.require_capability(&remote, Capability::CanMaterializeRemote, line, "materialize_remote requires can_materialize_remote")?;
                if !matches!(remote.ty, TypeKind::Remote { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "materialize_remote expects a Remote<T@arch> value",
                    ).with_help(format!("value passport: {remote}")));
                }
                Ok(Passport::materialized_remote(ambient_theory, &remote, "local_materialize"))
            }
            "checkpoint_remote" | "checkpoint_job" | "checkpoint_remote_job" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one Remote value")));
                }
                let remote = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&remote, Capability::CanCheckpointRemote, line, "checkpoint_remote requires can_checkpoint_remote")?;
                if !matches!(remote.ty, TypeKind::Remote { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "checkpoint_remote expects a Remote<T@arch> value",
                    ).with_help(format!("value passport: {remote}")));
                }
                Ok(Passport::remote_checkpoint(ambient_theory, &remote))
            }
            "restore_remote" | "restore_job" | "restore_remote_checkpoint" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects target node and RemoteCheckpoint")));
                }
                let target = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&target, Capability::CanAcceptMigration, line, "restore_remote target requires can_accept_migration")?;
                let target_arch = Self::node_arch(&target).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::MigrationBridgeError,
                    Some(line),
                    "restore_remote target must be Node<x86_64> or Node<aarch64>",
                ).with_help(format!("target passport: {target}")))?;

                let checkpoint = self.infer_expr(&args[1], ambient_theory)?;
                self.require_capability(&checkpoint, Capability::CanRestoreRemoteCheckpoint, line, "restore_remote requires can_restore_remote_checkpoint")?;
                if !matches!(checkpoint.ty, TypeKind::RemoteCheckpoint { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "restore_remote expects a RemoteCheckpoint<T@arch> value",
                    ).with_help(format!("checkpoint passport: {checkpoint}")));
                }
                Ok(Passport::restored_remote(ambient_theory, &checkpoint, &target, target_arch))
            }
            "live_migrate" | "live_migrate_remote" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects target node and Remote value")));
                }
                let target = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&target, Capability::CanAcceptMigration, line, "live_migrate target requires can_accept_migration")?;
                let target_arch = Self::node_arch(&target).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::MigrationBridgeError,
                    Some(line),
                    "live_migrate target must be Node<x86_64> or Node<aarch64>",
                ).with_help(format!("target passport: {target}")))?;

                let remote = self.infer_expr(&args[1], ambient_theory)?;
                self.require_capability(&remote, Capability::CanLiveMigrateRemote, line, "live_migrate requires can_live_migrate_remote")?;
                if !matches!(remote.ty, TypeKind::Remote { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "live_migrate expects a Remote<T@arch> value",
                    ).with_help(format!("remote passport: {remote}")));
                }
                Ok(Passport::live_migrated_remote(ambient_theory, &remote, &target, target_arch))
            }
            "schedule" | "schedule_on" => {
                if args.len() != 3 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects pool, target node and source value")));
                }
                let pool = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&pool, Capability::CanScheduleRuntime, line, "schedule_on requires a VirtualCluster with can_schedule_runtime")?;
                if !matches!(pool.ty, TypeKind::VirtualCluster) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::DistributedResourceError,
                        Some(line),
                        "schedule_on expects a VirtualCluster as its first argument",
                    ).with_help(format!("pool passport: {pool}")));
                }

                let target = self.infer_expr(&args[1], ambient_theory)?;
                self.require_capability(&target, Capability::CanAcceptMigration, line, "schedule_on target requires can_accept_migration")?;
                let target_arch = Self::node_arch(&target).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::MigrationBridgeError,
                    Some(line),
                    "schedule_on target must be Node<x86_64> or Node<aarch64>",
                ).with_help(format!("target passport: {target}")))?;

                let (source_theory, source_passport) = match &args[2].kind {
                    ExprKind::QualifiedIdent { theory: source, name } => {
                        (source.clone(), self.lookup_qualified(source, name, source, line)?)
                    }
                    _ => (ambient_theory.to_string(), self.infer_expr(&args[2], ambient_theory)?),
                };

                let bridge_name = if source_theory == ambient_theory {
                    "local_schedule".to_string()
                } else {
                    let Some(bridge) = self.find_bridge(&source_theory, ambient_theory, BridgeKind::Migration) else {
                        return Err(Diagnostic::error(
                            DiagnosticKind::MigrationBridgeError,
                            Some(line),
                            format!("no migration bridge from theory {source_theory} to {ambient_theory} in scope for schedule_on"),
                        ).with_help("declare or import: bridge Name : Source -> Target { kind = migration }"));
                    };
                    bridge.name.clone()
                };

                self.require_capability(&source_passport, Capability::CanSerializeForMigration, line, "schedule_on source requires can_serialize_for_migration")?;
                Ok(source_passport.scheduled_to(ambient_theory, target_arch, &bridge_name, &pool, &target))
            }
            "migrate" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "migrate expects target node and source value"));
                }
                let target = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&target, Capability::CanAcceptMigration, line, "migrate target requires can_accept_migration")?;
                let target_arch = Self::node_arch(&target).ok_or_else(|| Diagnostic::error(
                    DiagnosticKind::MigrationBridgeError,
                    Some(line),
                    "migrate target must be Node<x86_64> or Node<aarch64>",
                ).with_help(format!("target passport: {target}")))?;

                let (source_theory, source_passport) = match &args[1].kind {
                    ExprKind::QualifiedIdent { theory: source, name } => {
                        (source.clone(), self.lookup_qualified(source, name, source, line)?)
                    }
                    _ => (ambient_theory.to_string(), self.infer_expr(&args[1], ambient_theory)?),
                };

                let Some(bridge) = self.find_bridge(&source_theory, ambient_theory, BridgeKind::Migration) else {
                    return Err(Diagnostic::error(
                        DiagnosticKind::MigrationBridgeError,
                        Some(line),
                        format!("no migration bridge from theory {source_theory} to {ambient_theory} in scope"),
                    ).with_help("declare or import: bridge Name : Source -> Target { kind = migration }"));
                };
                self.require_capability(&source_passport, Capability::CanSerializeForMigration, line, "migrate source requires can_serialize_for_migration")?;
                Ok(source_passport.migrated_to(ambient_theory, target_arch, &bridge.name))
            }
            "equals" | "eq" => {
                Err(Diagnostic::error(
                    DiagnosticKind::EqualityModeError,
                    Some(line),
                    "ambiguous equality is not allowed",
                ).with_help("use an explicit equality mode: eq_value(...), eq_syntax(...), or eq_proof(...)"))
            }
            "eq_value" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "eq_value expects two arguments"));
                }
                let lhs = self.infer_expr(&args[0], ambient_theory)?;
                let rhs = self.infer_expr(&args[1], ambient_theory)?;
                if lhs.ty != rhs.ty {
                    return Err(Diagnostic::error(
                        DiagnosticKind::EqualityModeError,
                        Some(line),
                        "eq_value requires both operands to have the same value type",
                    ).with_help(format!("left: {lhs}\n  right: {rhs}")));
                }
                self.require_capability(&lhs, Capability::CanCompareDirect, line, "eq_value requires can_compare_direct on the left operand")?;
                self.require_capability(&rhs, Capability::CanCompareDirect, line, "eq_value requires can_compare_direct on the right operand")?;
                Ok(Passport {
                    ty: TypeKind::Bool,
                    construction: lhs.construction.max(rhs.construction),
                    capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
                    cost: lhs.cost.max(rhs.cost),
                    trust: lhs.trust.max(rhs.trust),
                    provenance: lhs.provenance.max(rhs.provenance),
                    validation: lhs.validation.max(rhs.validation),
                    theory: TheoryContext::new(ambient_theory),
                    history: HistoryChain::merge2(&lhs.history, &rhs.history, "equality:value"),
                    location: LocationContext::local(),
                })
            }
            "eq_proof" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "eq_proof expects two arguments"));
                }
                let lhs = self.infer_expr(&args[0], ambient_theory)?;
                let rhs = self.infer_expr(&args[1], ambient_theory)?;
                if lhs.ty != rhs.ty {
                    return Err(Diagnostic::error(
                        DiagnosticKind::EqualityModeError,
                        Some(line),
                        "eq_proof requires both operands to have the same proof-comparable type",
                    ).with_help(format!("left: {lhs}\n  right: {rhs}")));
                }
                self.require_capability(&lhs, Capability::CanCompareByProof, line, "eq_proof requires can_compare_by_proof on the left operand")?;
                self.require_capability(&rhs, Capability::CanCompareByProof, line, "eq_proof requires can_compare_by_proof on the right operand")?;
                Ok(Passport {
                    ty: TypeKind::Bool,
                    construction: ConstructionMode::ProofFinite,
                    capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
                    cost: CostClass::ProofRequired,
                    trust: lhs.trust.max(rhs.trust),
                    provenance: lhs.provenance.max(rhs.provenance),
                    validation: lhs.validation.max(rhs.validation),
                    theory: TheoryContext::new(ambient_theory),
                    history: HistoryChain::merge2(&lhs.history, &rhs.history, "equality:proof"),
                    location: LocationContext::local(),
                })
            }
            "eq_syntax" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "eq_syntax expects two Term arguments"));
                }
                let lhs = self.infer_expr(&args[0], ambient_theory)?;
                let rhs = self.infer_expr(&args[1], ambient_theory)?;
                if !matches!(lhs.ty, TypeKind::Term { .. }) || !matches!(rhs.ty, TypeKind::Term { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::EqualityModeError,
                        Some(line),
                        "eq_syntax requires Term operands",
                    ).with_help(format!("left: {lhs}\n  right: {rhs}")));
                }
                self.require_capability(&lhs, Capability::CanCompareSyntax, line, "eq_syntax requires can_compare_syntax on the left operand")?;
                self.require_capability(&rhs, Capability::CanCompareSyntax, line, "eq_syntax requires can_compare_syntax on the right operand")?;
                Ok(Passport {
                    ty: TypeKind::Bool,
                    construction: ConstructionMode::ProofFinite,
                    capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
                    cost: CostClass::SmallFinite,
                    trust: lhs.trust.max(rhs.trust),
                    provenance: lhs.provenance.max(rhs.provenance),
                    validation: lhs.validation.max(rhs.validation),
                    theory: TheoryContext::new(ambient_theory),
                    history: HistoryChain::merge2(&lhs.history, &rhs.history, "equality:syntax"),
                    location: LocationContext::local(),
                })
            }
            "print_symbolic" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "print_symbolic expects one argument"));
                }
                let value = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&value, Capability::CanSymbolicPrint, line, "print_symbolic requires can_symbolic_print")?;
                Ok(Passport {
                    ty: TypeKind::Text,
                    construction: value.construction,
                    capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
                    cost: value.cost,
                    trust: value.trust,
                    provenance: value.provenance,
                    validation: value.validation,
                    theory: TheoryContext::new(ambient_theory),
                    history: HistoryChain::from_source(&value.history, "output:print_symbolic"),
                    location: LocationContext::local(),
                })
            }
            "axiom_true" | "assume_true" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(Passport::axiom_bool(ambient_theory))
            }
            "axiom_nat" | "assume_nat" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(Passport::axiom_nat(ambient_theory))
            }
            "unsafe_nat" | "unsafe_assume_nat" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(Passport::unsafe_nat(ambient_theory))
            }
            "read_nat" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "read_nat expects no arguments"));
                }
                Ok(Passport::runtime_nat_from_input(ambient_theory))
            }
            "require" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "require expects one boolean condition"));
                }
                let condition = self.infer_expr(&args[0], ambient_theory)?;
                if !matches!(condition.ty, TypeKind::Bool) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::AccessError,
                        Some(line),
                        "require expects a Bool condition",
                    ).with_help(format!("condition passport: {condition}")));
                }
                Ok(Passport::runtime_witness(
                    ambient_theory,
                    format!("line_{line}_condition"),
                    &condition,
                ))
            }
            "prove" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "prove expects one static boolean condition"));
                }
                let condition = self.infer_expr(&args[0], ambient_theory)?;
                if !matches!(condition.ty, TypeKind::Bool) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::AccessError,
                        Some(line),
                        "prove expects a Bool condition",
                    ).with_help(format!("condition passport: {condition}")));
                }
                if Self::is_runtime_dependent(&condition) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::RuntimeStaticMismatch,
                        Some(line),
                        "cannot create StaticProof from runtime-dependent input",
                    ).with_help(format!(
                        "condition passport: {condition}\n  use require(...) to create a RuntimeWitness instead"
                    )));
                }
                Ok(Passport::static_proof(
                    ambient_theory,
                    format!("line_{line}_condition"),
                    &condition,
                ))
            }
            "prop_true" | "true_prop" | "proposition_true" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(Passport::proposition(ambient_theory, "true", None, "prop:true"))
            }
            "prop_gt" | "gt_prop" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects two Nat-like arguments")));
                }
                let lhs = self.infer_expr(&args[0], ambient_theory)?;
                let rhs = self.infer_expr(&args[1], ambient_theory)?;
                self.require_capability(&lhs, Capability::CanCompareDirect, line, "prop_gt requires can_compare_direct on the left operand")?;
                self.require_capability(&rhs, Capability::CanCompareDirect, line, "prop_gt requires can_compare_direct on the right operand")?;
                if Self::is_runtime_dependent(&lhs) || Self::is_runtime_dependent(&rhs) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::RuntimeStaticMismatch,
                        Some(line),
                        "prop_gt cannot create a static proposition from runtime-dependent input",
                    ).with_help(format!("left: {lhs}\n  right: {rhs}\n  use require(...) for runtime witnesses instead")));
                }
                let condition = Passport {
                    ty: TypeKind::Bool,
                    construction: lhs.construction.max(rhs.construction),
                    capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
                    cost: lhs.cost.max(rhs.cost),
                    trust: lhs.trust.max(rhs.trust),
                    provenance: lhs.provenance.max(rhs.provenance),
                    validation: lhs.validation.max(rhs.validation),
                    theory: TheoryContext::new(ambient_theory),
                    history: HistoryChain::merge2(&lhs.history, &rhs.history, "compare:gt"),
                    location: LocationContext::local(),
                };
                Ok(Passport::proposition(ambient_theory, "gt", Some(&condition), "prop:gt"))
            }
            "provable_of" | "provability_of" | "provable" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one StaticProof argument")));
                }
                let proof = self.infer_expr(&args[0], ambient_theory)?;
                let predicate = match &proof.ty {
                    TypeKind::StaticProof(predicate) => predicate.clone(),
                    _ => return Err(Diagnostic::error(
                        DiagnosticKind::TruthBoundaryError,
                        Some(line),
                        "provable_of requires StaticProof; arbitrary values are not provability claims",
                    ).with_help(format!("value passport: {proof}"))),
                };
                Ok(Passport::provable_claim(ambient_theory, proof.theory.home.clone(), predicate, &proof))
            }
            "truth_from_provable" | "assert_truth" | "truth" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one Provable claim")));
                }
                let claim = self.infer_expr(&args[0], ambient_theory)?;
                if !matches!(claim.ty, TypeKind::Provable { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::TruthBoundaryError,
                        Some(line),
                        "truth_from_provable requires Provable<T>; use provable_of(StaticProof) first",
                    ).with_help(format!("value passport: {claim}")));
                }
                Err(Diagnostic::error(
                    DiagnosticKind::TheoryBridgeError,
                    Some(line),
                    "Provable(phi) cannot be used as Truth(phi) without an explicit soundness bridge or axiom-tainted truth lift",
                ).with_help("Use soundness(...) for an explicit theory bridge, or truth_from_provable_axiom(...) to mark the result Axiom-tainted."))
            }
            "truth_from_provable_axiom" | "assume_truth_from_provable" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one Provable claim")));
                }
                let claim = self.infer_expr(&args[0], ambient_theory)?;
                let predicate = match &claim.ty {
                    TypeKind::Provable { proposition, .. } => proposition.clone(),
                    _ => return Err(Diagnostic::error(
                        DiagnosticKind::TruthBoundaryError,
                        Some(line),
                        "truth_from_provable_axiom requires Provable<T>",
                    ).with_help(format!("value passport: {claim}"))),
                };
                Ok(Passport::axiom_truth_from_provable(ambient_theory, predicate, &claim))
            }
            "consistency_claim" | "consistency_of_current" | "consistent_current" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects no arguments in MVP")));
                }
                Ok(Passport::consistency_claim(ambient_theory, ambient_theory, None))
            }
            "prove_consistency" | "prove_own_consistency" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one Consistency<T> claim")));
                }
                let claim = self.infer_expr(&args[0], ambient_theory)?;
                let target = match &claim.ty {
                    TypeKind::ConsistencyClaim { theory } => theory.clone(),
                    _ => return Err(Diagnostic::error(
                        DiagnosticKind::IncompletenessBoundaryError,
                        Some(line),
                        "prove_consistency requires Consistency<T>",
                    ).with_help(format!("value passport: {claim}"))),
                };
                Err(Diagnostic::error(
                    DiagnosticKind::IncompletenessBoundaryError,
                    Some(line),
                    format!("cannot prove Consistency<{target}> inside the current MVP theory context"),
                ).with_help("Use assume_consistency(...) to mark the result Axiom-tainted, or move the claim through an explicit stronger meta-theory/soundness framework in a future proof-kernel layer."))
            }
            "consistency_axiom" | "assume_consistency" | "consistency_from_axiom" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one Consistency<T> claim")));
                }
                let claim = self.infer_expr(&args[0], ambient_theory)?;
                let target = match &claim.ty {
                    TypeKind::ConsistencyClaim { theory } => theory.clone(),
                    _ => return Err(Diagnostic::error(
                        DiagnosticKind::IncompletenessBoundaryError,
                        Some(line),
                        "assume_consistency requires Consistency<T>",
                    ).with_help(format!("value passport: {claim}"))),
                };
                Ok(Passport::axiom_consistency_proof(ambient_theory, target, &claim))
            }
            "reflection_claim" | "reflection_of" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one Provable<T> claim")));
                }
                let claim = self.infer_expr(&args[0], ambient_theory)?;
                let (object_theory, proposition) = match &claim.ty {
                    TypeKind::Provable { object_theory, proposition } => (object_theory.clone(), proposition.clone()),
                    _ => return Err(Diagnostic::error(
                        DiagnosticKind::ReflectionBoundaryError,
                        Some(line),
                        "reflection_claim requires Provable<T>; reflection is a meta-level claim about provability",
                    ).with_help(format!("value passport: {claim}"))),
                };
                if self.find_bridge(&object_theory, ambient_theory, BridgeKind::Reflection).is_none() {
                    return Err(Diagnostic::error(
                        DiagnosticKind::ReflectionBoundaryError,
                        Some(line),
                        format!("no reflection bridge from theory {object_theory} to {ambient_theory} in scope"),
                    ).with_help("declare or import an explicit reflection bridge: bridge Name : Source -> Target { kind = reflection }"));
                }
                Ok(Passport::reflection_claim(ambient_theory, object_theory, proposition, &claim))
            }
            "reflection_axiom" | "assume_reflection" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one Reflection<T> claim")));
                }
                let claim = self.infer_expr(&args[0], ambient_theory)?;
                let (object_theory, proposition) = match &claim.ty {
                    TypeKind::ReflectionClaim { object_theory, proposition } => (object_theory.clone(), proposition.clone()),
                    _ => return Err(Diagnostic::error(
                        DiagnosticKind::ReflectionBoundaryError,
                        Some(line),
                        "reflection_axiom requires Reflection<T>; use reflection_claim(provable_of(...)) first",
                    ).with_help(format!("value passport: {claim}"))),
                };
                Ok(Passport::axiom_reflection_proof(ambient_theory, object_theory, proposition, &claim))
            }
            "self_reference" | "self_reference_claim" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one Prop value")));
                }
                let prop = self.infer_expr(&args[0], ambient_theory)?;
                let proposition = match &prop.ty {
                    TypeKind::Prop { name } => name.clone(),
                    _ => return Err(Diagnostic::error(
                        DiagnosticKind::ReflectionBoundaryError,
                        Some(line),
                        "self_reference requires Prop<T>; self-reference is a claim object, not a proof or truth value",
                    ).with_help(format!("value passport: {prop}"))),
                };
                Ok(Passport::self_reference_claim(
                    ambient_theory,
                    proposition.clone(),
                    Some(&prop),
                    format!("self_reference:claim:{proposition}"),
                ))
            }
            "godel_sentence" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "godel_sentence expects no arguments"));
                }
                Ok(Passport::self_reference_claim(
                    ambient_theory,
                    "godel_sentence",
                    None,
                    "self_reference:claim:godel_sentence",
                ))
            }
            "self_reference_axiom" | "assume_self_reference" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one SelfReference<T> claim")));
                }
                let claim = self.infer_expr(&args[0], ambient_theory)?;
                let proposition = match &claim.ty {
                    TypeKind::SelfReferenceClaim { proposition } => proposition.clone(),
                    _ => return Err(Diagnostic::error(
                        DiagnosticKind::ReflectionBoundaryError,
                        Some(line),
                        "self_reference_axiom requires SelfReference<T>; use self_reference(...) or godel_sentence() first",
                    ).with_help(format!("value passport: {claim}"))),
                };
                Ok(Passport::axiom_self_reference_proof(ambient_theory, proposition, &claim))
            }
            "reflect_provable" | "prove_self_reference" | "truth_of_self_reference" | "truth_of_self" | "says_unprovable_self" | "liar_sentence" | "truth_of_own_truth" => {
                Err(Diagnostic::error(
                    DiagnosticKind::ReflectionBoundaryError,
                    Some(line),
                    format!("{name} crosses the reflection/self-reference boundary implicitly"),
                ).with_help("Reflection and self-reference must remain explicit claim objects. Use reflection_claim(...), self_reference(...), and an axiom-tainted lift only when the assumption must be visible in the passport history."))
            }
            "proof_true" | "true_intro" => {
                if !args.is_empty() {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects no arguments")));
                }
                Ok(Passport::proof_term(ambient_theory, "true_intro", None))
            }
            "proof_gt" | "gt_intro" => {
                if args.len() != 2 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects two Nat-like arguments")));
                }
                let lhs = self.infer_expr(&args[0], ambient_theory)?;
                let rhs = self.infer_expr(&args[1], ambient_theory)?;
                self.require_capability(&lhs, Capability::CanCompareDirect, line, "proof_gt requires can_compare_direct on the left operand")?;
                self.require_capability(&rhs, Capability::CanCompareDirect, line, "proof_gt requires can_compare_direct on the right operand")?;
                if Self::is_runtime_dependent(&lhs) || Self::is_runtime_dependent(&rhs) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::RuntimeStaticMismatch,
                        Some(line),
                        "proof_gt cannot create a kernel proof term from runtime-dependent input",
                    ).with_help(format!("left: {lhs}\n  right: {rhs}\n  use require(...) for runtime witnesses instead")));
                }
                let condition = Passport {
                    ty: TypeKind::Bool,
                    construction: lhs.construction.max(rhs.construction),
                    capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
                    cost: lhs.cost.max(rhs.cost),
                    trust: lhs.trust.max(rhs.trust),
                    provenance: lhs.provenance.max(rhs.provenance),
                    validation: lhs.validation.max(rhs.validation),
                    theory: TheoryContext::new(ambient_theory),
                    history: HistoryChain::merge2(&lhs.history, &rhs.history, "compare:gt"),
                    location: LocationContext::local(),
                };
                Ok(Passport::proof_term(ambient_theory, "gt_intro", Some(&condition)))
            }
            "check_proof" | "kernel_check" | "verify_proof" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), format!("{name} expects one ProofTerm argument")));
                }
                let term = self.infer_expr(&args[0], ambient_theory)?;
                if !matches!(term.ty, TypeKind::ProofTerm { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::ProofKernelError,
                        Some(line),
                        "check_proof requires a ProofTerm produced by the proof kernel",
                    ).with_help(format!("value passport: {term}")));
                }
                self.require_capability(&term, Capability::CanProofKernelCheck, line, "check_proof requires can_proof_kernel_check")?;
                let predicate = match &term.ty {
                    TypeKind::ProofTerm { rule } => format!("kernel_checked:{rule}"),
                    _ => format!("kernel_checked:line_{line}"),
                };
                Ok(Passport::kernel_checked_proof(ambient_theory, predicate, &term))
            }
            "fake_proof" | "unchecked_proof" | "bare_proof" => Err(Diagnostic::error(
                DiagnosticKind::ProofKernelError,
                Some(line),
                "bare/fake proof terms are not allowed",
            ).with_help("use proof_true(), proof_gt(...), or another kernel constructor, then check_proof(...)")),
            "print_decimal" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "print_decimal expects one argument"));
                }
                let value = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&value, Capability::CanPrintDecimal, line, "print_decimal requires can_print_decimal")?;
                Ok(Passport {
                    ty: TypeKind::Text,
                    construction: ConstructionMode::Literal,
                    capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
                    cost: CostClass::SmallFinite,
                    trust: value.trust,
                    provenance: value.provenance,
                    validation: value.validation,
                    theory: TheoryContext::new(ambient_theory),
                    history: HistoryChain::from_source(&value.history, "output:print_decimal"),
                    location: LocationContext::local(),
                })
            }
            "print_text" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "print_text expects one argument"));
                }
                let value = self.infer_expr(&args[0], ambient_theory)?;
                if !matches!(value.ty, TypeKind::Text) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::AccessError,
                        Some(line),
                        "print_text expects a Text value",
                    ).with_help(format!("value passport: {value}")));
                }
                self.require_capability(&value, Capability::CanSymbolicPrint, line, "print_text requires can_symbolic_print")?;
                Ok(Passport {
                    ty: TypeKind::Text,
                    construction: value.construction,
                    capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
                    cost: value.cost,
                    trust: value.trust,
                    provenance: value.provenance,
                    validation: value.validation,
                    theory: TheoryContext::new(ambient_theory),
                    history: HistoryChain::from_source(&value.history, "output:print_text"),
                    location: LocationContext::local(),
                })
            }
            "inspect_ast" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "inspect_ast expects one Term argument"));
                }
                let value = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&value, Capability::CanInspectAst, line, "inspect_ast requires can_inspect_ast")?;
                if !matches!(value.ty, TypeKind::Term { .. }) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::AccessError,
                        Some(line),
                        "inspect_ast expects a Term value",
                    ).with_help(format!("value passport: {value}")));
                }
                Ok(Passport {
                    ty: TypeKind::Text,
                    construction: ConstructionMode::Literal,
                    capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
                    cost: CostClass::SmallFinite,
                    trust: value.trust,
                    provenance: value.provenance,
                    validation: value.validation,
                    theory: TheoryContext::new(ambient_theory),
                    history: HistoryChain::from_source(&value.history, "inspect:ast"),
                    location: LocationContext::local(),
                })
            }
            "read" | "read_stdin" => Ok(Passport::raw_external_bytes(ambient_theory)),
            "parse_nat" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "parse_nat expects one argument"));
                }
                let raw = self.infer_expr(&args[0], ambient_theory)?;
                self.require_capability(&raw, Capability::CanParse, line, "parse_nat requires raw parse capability")?;
                Ok(Passport::runtime_nat_from_input(ambient_theory))
            }
            "quote" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "quote expects one argument"));
                }
                match &args[0].kind {
                    ExprKind::QualifiedIdent { theory: source, name } => {
                        let source_passport = self.lookup_qualified(source, name, source, line)?;
                        let Some(bridge) = self.find_bridge(source, ambient_theory, BridgeKind::Quote) else {
                            return Err(Diagnostic::error(
                                DiagnosticKind::TheoryBridgeError,
                                Some(line),
                                format!("no quote bridge from theory {source} to {ambient_theory} in scope"),
                            ).with_help("declare or import: bridge Name : Source -> Target { kind = quote }"));
                        };
                        Ok(Passport::term_of(ambient_theory, source, &source_passport.ty.to_string(), &bridge.name, &source_passport))
                    }
                    _ => Err(Diagnostic::error(
                        DiagnosticKind::TheoryBridgeError,
                        Some(line),
                        "quote MVP expects a qualified source expression like PA.n",
                    )),
                }
            }
            "transport" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "transport expects one qualified source value"));
                }
                match &args[0].kind {
                    ExprKind::QualifiedIdent { theory: source, name } => {
                        let source_passport = self.lookup_qualified(source, name, source, line)?;
                        let Some(bridge) = self.find_bridge(source, ambient_theory, BridgeKind::Transport) else {
                            return Err(Diagnostic::error(
                                DiagnosticKind::TheoryBridgeError,
                                Some(line),
                                format!("no transport bridge from theory {source} to {ambient_theory} in scope"),
                            ).with_help("declare or import: bridge Name : Source -> Target { kind = transport }"));
                        };
                        Ok(source_passport.transported_to(ambient_theory, &bridge.name))
                    }
                    _ => Err(Diagnostic::error(
                        DiagnosticKind::TheoryBridgeError,
                        Some(line),
                        "transport MVP expects a qualified source expression like PA.n",
                    )),
                }
            }
            "soundness" => {
                if args.len() != 1 {
                    return Err(Diagnostic::error(DiagnosticKind::ParseError, Some(line), "soundness expects one qualified proof value"));
                }
                match &args[0].kind {
                    ExprKind::QualifiedIdent { theory: source, name } => {
                        let source_passport = self.lookup_qualified(source, name, source, line)?;
                        if !matches!(source_passport.ty, TypeKind::StaticProof(_)) {
                            return Err(Diagnostic::error(
                                DiagnosticKind::TheoryBridgeError,
                                Some(line),
                                "soundness MVP expects a StaticProof from the source theory",
                            ).with_help(format!("source passport: {source_passport}")));
                        }
                        let Some(bridge) = self.find_bridge(source, ambient_theory, BridgeKind::Soundness) else {
                            return Err(Diagnostic::error(
                                DiagnosticKind::TheoryBridgeError,
                                Some(line),
                                format!("no soundness bridge from theory {source} to {ambient_theory} in scope"),
                            ).with_help("declare a trusted bridge Name : Source -> Target { kind = soundness }"));
                        };
                        Ok(Passport::soundness_proof(
                            ambient_theory,
                            format!("soundness_of_{source}_{name}"),
                            &source_passport,
                            &bridge.name,
                        ))
                    }
                    _ => Err(Diagnostic::error(
                        DiagnosticKind::TheoryBridgeError,
                        Some(line),
                        "soundness MVP expects a qualified source proof like PA.p",
                    )),
                }
            }
            other => Err(Diagnostic::error(
                DiagnosticKind::NameError,
                Some(line),
                format!("unknown builtin or function '{other}' in MVP"),
            )),
        }
    }

    fn is_runtime_dependent(passport: &Passport) -> bool {
        matches!(
            passport.provenance,
            Provenance::RuntimeInput | Provenance::ExternalFile | Provenance::OracleInput | Provenance::UnsafeExternal
        ) || matches!(
            passport.validation,
            ValidationState::Raw | ValidationState::Parsed | ValidationState::RuntimeChecked | ValidationState::ConstraintChecked | ValidationState::Assumed
        )
    }

    fn lookup_ident(&self, name: &str, ambient_theory: &str, line: usize) -> Result<Passport, Diagnostic> {
        self.theories.get(ambient_theory)
            .and_then(|scope| scope.get(name))
            .cloned()
            .ok_or_else(|| Diagnostic::error(
                DiagnosticKind::NameError,
                Some(line),
                format!("unknown name '{name}' in theory {ambient_theory}"),
            ))
    }

    fn lookup_qualified(&self, theory: &str, name: &str, ambient_theory: &str, line: usize) -> Result<Passport, Diagnostic> {
        let value = self.theories.get(theory)
            .and_then(|scope| scope.get(name))
            .cloned()
            .ok_or_else(|| Diagnostic::error(
                DiagnosticKind::NameError,
                Some(line),
                format!("unknown qualified name '{theory}.{name}'"),
            ))?;

        if theory == ambient_theory {
            Ok(value)
        } else {
            Err(Diagnostic::error(
                DiagnosticKind::TheoryBridgeError,
                Some(line),
                format!("cannot use {theory}.{name} directly inside theory {ambient_theory}"),
            ).with_help("use quote(...), transport(...), or an explicit TheoryBridge"))
        }
    }

    fn validate_policy(&self, passport: &Passport, line: usize) -> Result<(), Diagnostic> {
        policy::validate_policy(passport, self.policy, line)
    }

    fn require_capability(&self, passport: &Passport, capability: Capability, line: usize, reason: &str) -> Result<(), Diagnostic> {
        crate::passport_rules::require_capability(passport, capability, line, reason)
    }


    fn require_language(&self, passport: &Passport, line: usize, message: &str) -> Result<(), Diagnostic> {
        match &passport.ty {
            TypeKind::Language { .. } => Ok(()),
            _ => Err(Diagnostic::error(
                DiagnosticKind::DefinabilityError,
                Some(line),
                message,
            ).with_help(format!("value passport: {passport}"))),
        }
    }

    fn require_encoding(&self, passport: &Passport, line: usize, message: &str) -> Result<(), Diagnostic> {
        match &passport.ty {
            TypeKind::Encoding { .. } => Ok(()),
            _ => Err(Diagnostic::error(
                DiagnosticKind::DefinabilityError,
                Some(line),
                message,
            ).with_help(format!("value passport: {passport}"))),
        }
    }

    fn require_meta_level(&self, passport: &Passport, line: usize, message: &str) -> Result<(), Diagnostic> {
        match &passport.ty {
            TypeKind::MetaLevel { .. } => Ok(()),
            _ => Err(Diagnostic::error(
                DiagnosticKind::DefinabilityError,
                Some(line),
                message,
            ).with_help(format!("value passport: {passport}"))),
        }
    }

    fn require_definable_nat(&self, passport: &Passport, line: usize, message: &str) -> Result<(), Diagnostic> {
        match &passport.ty {
            TypeKind::DefinableNat { .. } => Ok(()),
            _ => Err(Diagnostic::error(
                DiagnosticKind::DefinabilityError,
                Some(line),
                message,
            ).with_help(format!("value passport: {passport}"))),
        }
    }

    fn require_big_nat(&self, passport: &Passport, line: usize, message: &str) -> Result<(), Diagnostic> {
        match &passport.ty {
            TypeKind::BigNat { .. } => Ok(()),
            _ => Err(Diagnostic::error(
                DiagnosticKind::BigNumberError,
                Some(line),
                message,
            ).with_help(format!("value passport: {passport}"))),
        }
    }

    fn require_universe(&self, passport: &Passport, line: usize, message: &str) -> Result<(), Diagnostic> {
        match &passport.ty {
            TypeKind::Universe { .. } => Ok(()),
            _ => Err(Diagnostic::error(
                DiagnosticKind::UniverseLevelError,
                Some(line),
                message,
            ).with_help(format!("value passport: {passport}"))),
        }
    }

    fn require_class(&self, passport: &Passport, line: usize, message: &str) -> Result<(), Diagnostic> {
        match &passport.ty {
            TypeKind::Class { .. } => Ok(()),
            _ => Err(Diagnostic::error(
                DiagnosticKind::InfinityModeError,
                Some(line),
                message,
            ).with_help(format!("value passport: {passport}"))),
        }
    }

    fn require_infinity_mode(&self, passport: &Passport, mode: InfinityMode, line: usize, reason: &str) -> Result<(), Diagnostic> {
        match &passport.ty {
            TypeKind::Infinity { mode: actual } if *actual == mode => Ok(()),
            _ => Err(Diagnostic::error(
                DiagnosticKind::InfinityModeError,
                Some(line),
                reason,
            ).with_help(format!("value passport: {passport}"))),
        }
    }

    fn node_arch(passport: &Passport) -> Option<NodeArch> {
        match &passport.ty {
            TypeKind::Node { arch } => Some(*arch),
            _ => None,
        }
    }

    fn literal_u128(expr: &Expr) -> Option<u128> {
        match &expr.kind {
            ExprKind::IntLiteral(value) => value.parse::<u128>().ok(),
            _ => None,
        }
    }

    fn require_positive_resource(value: u128, name: &str, line: usize) -> Result<(), Diagnostic> {
        if value > 0 {
            Ok(())
        } else {
            Err(Diagnostic::error(
                DiagnosticKind::DistributedResourceError,
                Some(line),
                format!("node resource {name} must be greater than zero"),
            ))
        }
    }

    fn find_bridge(&self, source: &str, target: &str, kind: BridgeKind) -> Option<&BridgeDecl> {
        crate::bridge::find_bridge(&self.bridges, source, target, &kind)
    }
}
