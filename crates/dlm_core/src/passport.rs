use std::collections::BTreeSet;
use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum InfinityMode {
    Cardinal,
    Ordinal,
    Limit,
    Potential,
    Class,
    Universe,
}

impl fmt::Display for InfinityMode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            InfinityMode::Cardinal => write!(f, "cardinal"),
            InfinityMode::Ordinal => write!(f, "ordinal"),
            InfinityMode::Limit => write!(f, "limit"),
            InfinityMode::Potential => write!(f, "potential"),
            InfinityMode::Class => write!(f, "class"),
            InfinityMode::Universe => write!(f, "universe"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum NodeArch {
    X86_64,
    Aarch64,
}

impl fmt::Display for NodeArch {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            NodeArch::X86_64 => write!(f, "x86_64"),
            NodeArch::Aarch64 => write!(f, "aarch64"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum GpuBackend {
    Cuda,
    Rocm,
}

impl fmt::Display for GpuBackend {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            GpuBackend::Cuda => write!(f, "cuda"),
            GpuBackend::Rocm => write!(f, "rocm"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum LocationContext {
    Local,
    Node { arch: NodeArch },
    Remote { arch: NodeArch },
}

impl LocationContext {
    pub fn local() -> Self {
        Self::Local
    }
    pub fn node(arch: NodeArch) -> Self {
        Self::Node { arch }
    }
    pub fn remote(arch: NodeArch) -> Self {
        Self::Remote { arch }
    }
}

impl fmt::Display for LocationContext {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            LocationContext::Local => write!(f, "local"),
            LocationContext::Node { arch } => write!(f, "node<{arch}>"),
            LocationContext::Remote { arch } => write!(f, "remote<{arch}>"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TypeKind {
    Nat,
    Bool,
    Bytes,
    Text,
    Infinity {
        mode: InfinityMode,
    },
    Universe {
        level: u8,
    },
    Set {
        of_level: u8,
        lives_in: u8,
    },
    Class {
        of_level: u8,
    },
    Language {
        name: String,
    },
    Encoding {
        name: String,
    },
    MetaLevel {
        level: u8,
    },
    DefinableNat {
        language: String,
        encoding: String,
        object_theory: String,
        bound: u128,
        meta_level: u8,
    },
    BigNat {
        family: String,
        parameter: Option<u128>,
    },
    Prop {
        name: String,
    },
    Provable {
        object_theory: String,
        proposition: String,
    },
    TruthClaim {
        proposition: String,
    },
    ReflectionClaim {
        object_theory: String,
        proposition: String,
    },
    SelfReferenceClaim {
        proposition: String,
    },
    Statement {
        proposition: String,
    },
    Theorem {
        name: String,
        proposition: String,
    },
    Goal {
        proposition: String,
    },
    Hypothesis {
        proposition: String,
    },
    EqProof {
        lhs: String,
        rhs: String,
    },
    RewriteRule {
        name: String,
        lhs: String,
        rhs: String,
    },
    RewriteCertificate {
        from: String,
        to: String,
    },
    NatInductionScheme {
        proposition_family: String,
    },
    InductionBaseCase {
        proposition: String,
    },
    InductionStepCase {
        proposition: String,
    },
    InductionProof {
        proposition: String,
    },
    ModuleManifest {
        module: String,
    },
    ImportGraph {
        root: String,
    },
    ModuleExport {
        module: String,
        symbol: String,
        visibility: String,
    },
    ModuleInterface {
        module: String,
    },
    ModuleImportAudit {
        importer: String,
        provider: String,
        status: String,
    },
    AxiomRegistry {
        theory: String,
    },
    MetatheoryDependencyAudit {
        subject: String,
        status: String,
    },
    MetatheoryClosureReport {
        subject: String,
        status: String,
    },
    ConservativeExtensionAudit {
        base: String,
        extension: String,
        status: String,
    },
    GlobalMetatheoryInventory {
        subject: String,
        status: String,
    },
    SoundnessBoundaryLedger {
        subject: String,
        status: String,
    },
    TrustedBaseClosure {
        subject: String,
        status: String,
    },
    MetatheoryFoundationExit {
        subject: String,
        status: String,
    },
    LogicalFormula {
        form: String,
    },
    QuantifiedFormula {
        quantifier: String,
        variable: String,
        domain: String,
        body: String,
    },
    VariableScopeReport {
        subject: String,
    },
    AlphaEquivalenceReport {
        lhs: String,
        rhs: String,
        status: String,
    },
    SubstitutionReport {
        variable: String,
        status: String,
    },
    FunctionType {
        domain: String,
        codomain: String,
    },
    LambdaTerm {
        parameter: String,
        domain: String,
        body: String,
    },
    ApplicationTerm {
        function: String,
        argument: String,
        result: String,
        status: String,
    },
    FunctionContract {
        name: String,
        status: String,
    },
    ProductType {
        lhs: String,
        rhs: String,
    },
    ProductTerm {
        lhs: String,
        rhs: String,
        product_type: String,
    },
    SumType {
        left: String,
        right: String,
    },
    SumInjection {
        side: String,
        value: String,
        sum_type: String,
    },
    RecordType {
        name: String,
        fields: String,
    },
    RecordTerm {
        name: String,
        fields: String,
    },
    RecordProjection {
        record: String,
        field: String,
        result: String,
    },
    ProductElimination {
        product_type: String,
        lhs: String,
        rhs: String,
    },
    SumElimination {
        sum_type: String,
        side: String,
        result: String,
    },
    RecordPattern {
        record: String,
        fields: String,
    },
    OptionType {
        item: String,
    },
    OptionValue {
        kind: String,
        item: String,
    },
    ResultType {
        ok: String,
        err: String,
    },
    ResultValue {
        kind: String,
        value: String,
        result_type: String,
    },
    PartialityReport {
        subject: String,
        status: String,
    },
    ListType {
        item: String,
    },
    ListValue {
        item: String,
        len: usize,
    },
    SequenceType {
        item: String,
    },
    SequenceValue {
        item: String,
        len: usize,
    },
    SequenceIndex {
        sequence: String,
        index: usize,
        status: String,
    },
    MapTraversal {
        source: String,
        function: String,
        result: String,
    },
    FoldTraversal {
        source: String,
        step: String,
        result: String,
        fuel: usize,
    },
    TraversalReport {
        subject: String,
        status: String,
    },
    RecursionScheme {
        name: String,
        measure: String,
        status: String,
    },
    RecursiveCall {
        scheme: String,
        status: String,
        fuel_after: usize,
    },
    RecursionReport {
        subject: String,
        status: String,
    },
    ComputationBudget {
        name: String,
        status: String,
    },
    TerminationBudgetReport {
        subject: String,
        status: String,
    },
    StandardPreludeContract {
        name: String,
        operation: String,
        status: String,
    },
    PreludeEvaluationReport {
        name: String,
        operation: String,
        status: String,
    },
    PreludeLoweringReport {
        name: String,
        target: String,
        status: String,
    },
    BackendCapabilityContract {
        name: String,
        target: String,
        status: String,
    },
    BackendLoweringReport {
        name: String,
        target: String,
        status: String,
    },
    ConsistencyClaim {
        theory: String,
    },
    ProofTerm {
        rule: String,
    },
    Term {
        of_theory: String,
        of_type: String,
    },
    Node {
        arch: NodeArch,
    },
    GpuDevice {
        backend: GpuBackend,
    },
    GpuPool,
    VirtualCluster,
    DistributedMemory {
        memory_mib: Option<u128>,
    },
    DistributedGpuMemory {
        memory_mib: Option<u128>,
    },
    GpuValue {
        inner: Box<TypeKind>,
    },
    GpuKernel {
        inner: Box<TypeKind>,
    },
    MemoryCheckpoint {
        memory_mib: Option<u128>,
    },
    RemoteCheckpoint {
        inner: Box<TypeKind>,
        source_arch: NodeArch,
    },
    PortableCode {
        inner: Box<TypeKind>,
    },
    Remote {
        inner: Box<TypeKind>,
        target_arch: NodeArch,
    },
    Result(Box<TypeKind>),
    StaticProof(String),
    RuntimeWitness(String),
    Unknown(String),
}

impl fmt::Display for TypeKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            TypeKind::Nat => write!(f, "Nat"),
            TypeKind::Bool => write!(f, "Bool"),
            TypeKind::Bytes => write!(f, "Bytes"),
            TypeKind::Text => write!(f, "Text"),
            TypeKind::Infinity { mode } => write!(f, "Infinity<{mode}>"),
            TypeKind::Universe { level } => write!(f, "U{level}"),
            TypeKind::Set { of_level, lives_in } => write!(f, "Set<U{of_level}->U{lives_in}>"),
            TypeKind::Class { of_level } => write!(f, "Class<U{of_level}>"),
            TypeKind::Language { name } => write!(f, "Language<{name}>"),
            TypeKind::Encoding { name } => write!(f, "Encoding<{name}>"),
            TypeKind::MetaLevel { level } => write!(f, "MetaLevel<M{level}>"),
            TypeKind::DefinableNat {
                language,
                encoding,
                object_theory,
                bound,
                meta_level,
            } => write!(
                f,
                "DefinableNat<{language},{encoding},{object_theory},bound={bound},M{meta_level}>"
            ),
            TypeKind::BigNat { family, parameter } => match parameter {
                Some(parameter) => write!(f, "BigNat<{family}({parameter})>"),
                None => write!(f, "BigNat<{family}>"),
            },
            TypeKind::Prop { name } => write!(f, "Prop<{name}>"),
            TypeKind::Provable {
                object_theory,
                proposition,
            } => write!(f, "Provable<{object_theory}.{proposition}>"),
            TypeKind::TruthClaim { proposition } => write!(f, "TruthClaim<{proposition}>"),
            TypeKind::ReflectionClaim {
                object_theory,
                proposition,
            } => write!(f, "Reflection<{object_theory}.{proposition}>"),
            TypeKind::SelfReferenceClaim { proposition } => write!(f, "SelfReference<{proposition}>"),
            TypeKind::Statement { proposition } => write!(f, "Statement<{proposition}>"),
            TypeKind::Theorem { name, proposition } => write!(f, "Theorem<{name}:{proposition}>"),
            TypeKind::Goal { proposition } => write!(f, "Goal<{proposition}>"),
            TypeKind::Hypothesis { proposition } => write!(f, "Hypothesis<{proposition}>"),
            TypeKind::EqProof { lhs, rhs } => write!(f, "EqProof<{lhs}={rhs}>"),
            TypeKind::RewriteRule { name, lhs, rhs } => write!(f, "RewriteRule<{name}:{lhs}->{rhs}>"),
            TypeKind::RewriteCertificate { from, to } => write!(f, "RewriteCertificate<{from}->{to}>"),
            TypeKind::NatInductionScheme { proposition_family } => {
                write!(f, "InductionScheme<Nat,{proposition_family}>")
            }
            TypeKind::InductionBaseCase { proposition } => write!(f, "BaseCase<{proposition}>"),
            TypeKind::InductionStepCase { proposition } => write!(f, "StepCase<{proposition}>"),
            TypeKind::InductionProof { proposition } => write!(f, "InductionProof<{proposition}>"),
            TypeKind::ModuleManifest { module } => write!(f, "ModuleManifest<{module}>"),
            TypeKind::ImportGraph { root } => write!(f, "ImportGraph<root={root}>"),
            TypeKind::ModuleExport { module, symbol, visibility } => {
                write!(f, "ModuleExport<{module}.{symbol}:{visibility}>")
            }
            TypeKind::ModuleInterface { module } => write!(f, "ModuleInterface<{module}>"),
            TypeKind::ModuleImportAudit { importer, provider, status } => {
                write!(f, "ModuleImportAudit<{importer}->{provider}:{status}>")
            }
            TypeKind::AxiomRegistry { theory } => write!(f, "AxiomRegistry<{theory}>"),
            TypeKind::MetatheoryDependencyAudit { subject, status } => {
                write!(f, "MetatheoryDependencyAudit<{subject}:{status}>")
            }
            TypeKind::MetatheoryClosureReport { subject, status } => {
                write!(f, "MetatheoryClosureReport<{subject}:{status}>")
            }
            TypeKind::ConservativeExtensionAudit { base, extension, status } => {
                write!(f, "ConservativeExtensionAudit<{base}->{extension}:{status}>")
            }
            TypeKind::GlobalMetatheoryInventory { subject, status } => {
                write!(f, "GlobalMetatheoryInventory<{subject}:{status}>")
            }
            TypeKind::SoundnessBoundaryLedger { subject, status } => {
                write!(f, "SoundnessBoundaryLedger<{subject}:{status}>")
            }
            TypeKind::TrustedBaseClosure { subject, status } => {
                write!(f, "TrustedBaseClosure<{subject}:{status}>")
            }
            TypeKind::MetatheoryFoundationExit { subject, status } => {
                write!(f, "MetatheoryFoundationExit<{subject}:{status}>")
            }
            TypeKind::LogicalFormula { form } => write!(f, "LogicalFormula<{form}>"),
            TypeKind::QuantifiedFormula { quantifier, variable, domain, body } => {
                write!(f, "QuantifiedFormula<{quantifier} {variable}:{domain}. {body}>")
            }
            TypeKind::VariableScopeReport { subject } => write!(f, "VariableScopeReport<{subject}>"),
            TypeKind::AlphaEquivalenceReport { lhs, rhs, status } => {
                write!(f, "AlphaEquivalenceReport<{lhs}~{rhs}:{status}>")
            }
            TypeKind::SubstitutionReport { variable, status } => {
                write!(f, "SubstitutionReport<{variable}:{status}>")
            }
            TypeKind::FunctionType { domain, codomain } => {
                write!(f, "FunctionType<{domain}->{codomain}>")
            }
            TypeKind::LambdaTerm { parameter, domain, body } => {
                write!(f, "LambdaTerm<{parameter}:{domain}. {body}>")
            }
            TypeKind::ApplicationTerm { function, argument, result, status } => {
                write!(f, "ApplicationTerm<{function}({argument})=>{result}:{status}>")
            }
            TypeKind::FunctionContract { name, status } => {
                write!(f, "FunctionContract<{name}:{status}>")
            }
            TypeKind::ProductType { lhs, rhs } => write!(f, "ProductType<{lhs}*{rhs}>"),
            TypeKind::ProductTerm { lhs, rhs, product_type } => {
                write!(f, "ProductTerm<({lhs},{rhs}):{product_type}>")
            }
            TypeKind::SumType { left, right } => write!(f, "SumType<{left}+{right}>"),
            TypeKind::SumInjection { side, value, sum_type } => {
                write!(f, "SumInjection<{side}:{value}:{sum_type}>")
            }
            TypeKind::RecordType { name, fields } => write!(f, "RecordType<{name}{{{fields}}}>") ,
            TypeKind::RecordTerm { name, fields } => write!(f, "RecordTerm<{name}{{{fields}}}>") ,
            TypeKind::RecordProjection { record, field, result } => {
                write!(f, "RecordProjection<{record}.{field}:{result}>")
            }
            TypeKind::ProductElimination { product_type, lhs, rhs } => {
                write!(f, "ProductElimination<{product_type}=>({lhs},{rhs})>")
            }
            TypeKind::SumElimination { sum_type, side, result } => {
                write!(f, "SumElimination<{sum_type}:{side}=>{result}>")
            }
            TypeKind::RecordPattern { record, fields } => {
                write!(f, "RecordPattern<{record}{{{fields}}}>")
            }
            TypeKind::OptionType { item } => write!(f, "OptionType<{item}>") ,
            TypeKind::OptionValue { kind, item } => write!(f, "OptionValue<{kind}:{item}>") ,
            TypeKind::ResultType { ok, err } => write!(f, "ResultType<{ok},{err}>") ,
            TypeKind::ResultValue { kind, value, result_type } => {
                write!(f, "ResultValue<{kind}:{value}:{result_type}>")
            }
            TypeKind::PartialityReport { subject, status } => {
                write!(f, "PartialityReport<{subject}:{status}>")
            }
            TypeKind::ListType { item } => write!(f, "ListType<{item}>") ,
            TypeKind::ListValue { item, len } => write!(f, "ListValue<{item};len={len}>") ,
            TypeKind::SequenceType { item } => write!(f, "SequenceType<{item}>") ,
            TypeKind::SequenceValue { item, len } => write!(f, "SequenceValue<{item};len={len}>") ,
            TypeKind::SequenceIndex { sequence, index, status } => {
                write!(f, "SequenceIndex<{sequence}[{index}]:{status}>")
            }
            TypeKind::MapTraversal { source, function, result } => {
                write!(f, "MapTraversal<{source}:{function}=>{result}>")
            }
            TypeKind::FoldTraversal { source, step, result, fuel } => {
                write!(f, "FoldTraversal<{source}:{step}=>{result};fuel={fuel}>")
            }
            TypeKind::TraversalReport { subject, status } => {
                write!(f, "TraversalReport<{subject}:{status}>")
            }
            TypeKind::RecursionScheme { name, measure, status } => {
                write!(f, "RecursionScheme<{name}:{measure}:{status}>")
            }
            TypeKind::RecursiveCall { scheme, status, fuel_after } => {
                write!(f, "RecursiveCall<{scheme}:{status};fuel_after={fuel_after}>")
            }
            TypeKind::RecursionReport { subject, status } => {
                write!(f, "RecursionReport<{subject}:{status}>")
            }
            TypeKind::ComputationBudget { name, status } => {
                write!(f, "ComputationBudget<{name}:{status}>")
            }
            TypeKind::TerminationBudgetReport { subject, status } => {
                write!(f, "TerminationBudgetReport<{subject}:{status}>")
            }
            TypeKind::StandardPreludeContract { name, operation, status } => {
                write!(f, "StandardPreludeContract<{name}:{operation}:{status}>")
            }
            TypeKind::PreludeEvaluationReport { name, operation, status } => {
                write!(f, "PreludeEvaluationReport<{name}:{operation}:{status}>")
            }
            TypeKind::PreludeLoweringReport { name, target, status } => {
                write!(f, "PreludeLoweringReport<{name}:{target}:{status}>")
            }
            TypeKind::BackendCapabilityContract { name, target, status } => {
                write!(f, "BackendCapabilityContract<{name}:{target}:{status}>")
            }
            TypeKind::BackendLoweringReport { name, target, status } => {
                write!(f, "BackendLoweringReport<{name}:{target}:{status}>")
            }
            TypeKind::ConsistencyClaim { theory } => write!(f, "Consistency<{theory}>"),
            TypeKind::ProofTerm { rule } => write!(f, "ProofTerm<{rule}>"),
            TypeKind::Term { of_theory, of_type } => write!(f, "Term<{of_theory}.{of_type}>"),
            TypeKind::Node { arch } => write!(f, "Node<{arch}>"),
            TypeKind::GpuDevice { backend } => write!(f, "GpuDevice<{backend}>"),
            TypeKind::GpuPool => write!(f, "GpuPool"),
            TypeKind::VirtualCluster => write!(f, "VirtualCluster"),
            TypeKind::DistributedMemory { memory_mib } => match memory_mib {
                Some(value) => write!(f, "DistributedMemory<{value}MiB>"),
                None => write!(f, "DistributedMemory"),
            },
            TypeKind::DistributedGpuMemory { memory_mib } => match memory_mib {
                Some(value) => write!(f, "DistributedGpuMemory<{value}MiB>"),
                None => write!(f, "DistributedGpuMemory"),
            },
            TypeKind::GpuValue { inner } => write!(f, "GpuValue<{inner}>"),
            TypeKind::GpuKernel { inner } => write!(f, "GpuKernel<{inner}>"),
            TypeKind::MemoryCheckpoint { memory_mib } => match memory_mib {
                Some(value) => write!(f, "MemoryCheckpoint<{value}MiB>"),
                None => write!(f, "MemoryCheckpoint"),
            },
            TypeKind::RemoteCheckpoint { inner, source_arch } => {
                write!(f, "RemoteCheckpoint<{inner}@{source_arch}>")
            }
            TypeKind::PortableCode { inner } => write!(f, "PortableCode<{inner}>"),
            TypeKind::Remote { inner, target_arch } => write!(f, "Remote<{inner}@{target_arch}>"),
            TypeKind::Result(inner) => write!(f, "Result<{inner}>"),
            TypeKind::StaticProof(predicate) => write!(f, "StaticProof<{predicate}>"),
            TypeKind::RuntimeWitness(predicate) => write!(f, "RuntimeWitness<{predicate}>"),
            TypeKind::Unknown(name) => write!(f, "{name}"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum ConstructionMode {
    Literal,
    Compressed,
    Recursive,
    ProofFinite,
    Definable,
    Oracle,
    External,
    Unsafe,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum CostClass {
    Trivial,
    SmallFinite,
    LargeFinite,
    Compressed,
    Recursive,
    NonExpandable,
    ProofRequired,
    Uncomputable,
    OracleRequired,
    UnsafeUnknown,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum TrustLevel {
    Checked,
    Builtin,
    Axiom,
    Oracle,
    Unsafe,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum ValidationState {
    StaticChecked,
    Raw,
    Parsed,
    RuntimeChecked,
    ConstraintChecked,
    Assumed,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Provenance {
    InternalLiteral,
    InternalDerived,
    BuiltinKnown,
    RuntimeInput,
    ExternalFile,
    OracleInput,
    UnsafeExternal,
}

/// MVP v0.9: append-only provenance/bridge/trust history.
///
/// This is intentionally a small string-based chain for MVP. Later versions can
/// replace it with a typed enum plus hashes, epochs and node IDs without changing
/// the user-facing law: derived values must remember important past transitions.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HistoryChain {
    events: Vec<String>,
}

impl HistoryChain {
    pub fn empty() -> Self {
        Self { events: Vec::new() }
    }

    pub fn from_event(event: impl Into<String>) -> Self {
        Self {
            events: vec![event.into()],
        }
    }

    pub fn from_source(source: &HistoryChain, event: impl Into<String>) -> Self {
        let mut events = source.events.clone();
        events.push(event.into());
        Self { events }
    }

    pub fn merge2(lhs: &HistoryChain, rhs: &HistoryChain, event: impl Into<String>) -> Self {
        let mut events = lhs.events.clone();
        // HistoryChain is a provenance log, not a set.
        // Repeated events are meaningful for resource accounting: for example,
        // two equal GPU devices must contribute their VRAM twice.
        events.extend(rhs.events.iter().cloned());
        events.push(event.into());
        Self { events }
    }

    pub fn merge_many<'a>(
        sources: impl IntoIterator<Item = &'a HistoryChain>,
        event: impl Into<String>,
    ) -> Self {
        let mut events = Vec::new();
        // Preserve multiplicity and order. Deduplicating here corrupts pooled
        // resources when two nodes/devices have identical resource events.
        for source in sources {
            events.extend(source.events.iter().cloned());
        }
        events.push(event.into());
        Self { events }
    }

    pub fn push(&mut self, event: impl Into<String>) {
        self.events.push(event.into());
    }

    pub fn summary(&self) -> String {
        if self.events.is_empty() {
            "empty".to_string()
        } else {
            self.events.join(" -> ")
        }
    }

    pub fn contains_event(&self, needle: &str) -> bool {
        self.events.iter().any(|event| event.contains(needle))
    }

    pub fn events(&self) -> &[String] {
        &self.events
    }

    pub fn total_node_memory_mib(&self) -> Option<u128> {
        let mut total = 0u128;
        let mut found = false;
        for event in &self.events {
            if let Some(raw) = event.strip_prefix("node_resource:memory_mib:") {
                let value = raw.parse::<u128>().ok()?;
                total = total.checked_add(value)?;
                found = true;
            }
        }
        found.then_some(total)
    }

    pub fn total_gpu_memory_mib(&self) -> Option<u128> {
        let mut total = 0u128;
        let mut found = false;
        for event in &self.events {
            if let Some(raw) = event.strip_prefix("gpu_resource:memory_mib:") {
                let value = raw.parse::<u128>().ok()?;
                total = total.checked_add(value)?;
                found = true;
            }
        }
        found.then_some(total)
    }
}

impl fmt::Display for HistoryChain {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "[{}]", self.summary())
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TheoryContext {
    pub home: String,
    pub valid_in: BTreeSet<String>,
    pub bridge_trace: Vec<String>,
}

impl TheoryContext {
    pub fn new(home: impl Into<String>) -> Self {
        let home = home.into();
        let mut valid_in = BTreeSet::new();
        valid_in.insert(home.clone());
        Self {
            home,
            valid_in,
            bridge_trace: Vec::new(),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Capability {
    CanAddAsNat,
    CanPrintDecimal,
    CanSymbolicPrint,
    CanCompareDirect,
    CanCompareByProof,
    CanComputeModular,
    CanInspectAst,
    CanCompareSyntax,
    CanParse,
    CanRuntimeCompare,
    CanCardinalArithmetic,
    CanOrdinalArithmetic,
    CanLimitReason,
    CanUniverseLevel,
    CanFormSet,
    CanFormClass,
    CanLiftUniverse,
    CanClassReason,
    CanSetReason,
    CanDefineInLanguage,
    CanUseEncoding,
    CanMetaLevelReason,
    CanDefinabilityReason,
    CanExtractDefinabilityBound,
    CanExtractDefinabilityMeta,
    CanBigNumberReason,
    CanExtractGrowthClass,
    CanProofKernelCheck,
    CanPropositionReason,
    CanProvabilityReason,
    CanTruthBoundaryReason,
    CanExtractProvabilityTheory,
    CanConsistencyReason,
    CanIncompletenessReason,
    CanHostRuntime,
    CanAcceptMigration,
    CanMigrateOut,
    CanSerializeForMigration,
    CanRemoteSymbolicPrint,
    CanCheckpointRemote,
    CanRestoreRemoteCheckpoint,
    CanLiveMigrateRemote,
    CanMaterializeRemote,
    CanCompilePortableCode,
    CanDeployPortableCode,
    CanCrossArchPortable,
    CanVirtualizeCores,
    CanVirtualizeMemory,
    CanScheduleRuntime,
    CanAllocateDistributedMemory,
    CanUseDistributedMemory,
    CanCheckpointMemory,
    CanRestoreCheckpoint,
    CanHostGpuRuntime,
    CanAllocateGpuMemory,
    CanUseGpuMemory,
    CanCheckpointGpuMemory,
    CanLaunchGpuKernel,
    CanCompileGpuKernel,
    CanCopyCpuToGpu,
    CanCopyGpuToCpu,
    CanGpuPeerTransfer,
    CanGpuUnifiedAddressing,
    RequiresOracle,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CapabilitySet {
    inner: BTreeSet<Capability>,
}

impl CapabilitySet {
    pub fn empty() -> Self {
        Self {
            inner: BTreeSet::new(),
        }
    }

    pub fn from<const N: usize>(caps: [Capability; N]) -> Self {
        Self {
            inner: caps.into_iter().collect(),
        }
    }

    pub fn contains(&self, cap: Capability) -> bool {
        self.inner.contains(&cap)
    }

    pub fn insert(&mut self, cap: Capability) {
        self.inner.insert(cap);
    }

    pub fn intersection(&self, other: &Self) -> Self {
        Self {
            inner: self.inner.intersection(&other.inner).copied().collect(),
        }
    }

    pub fn union_for_builtin_only(&self, other: &Self) -> Self {
        Self {
            inner: self.inner.union(&other.inner).copied().collect(),
        }
    }

    pub fn names(&self) -> Vec<&'static str> {
        self.inner
            .iter()
            .map(|cap| match cap {
                Capability::CanAddAsNat => "can_add_as_nat",
                Capability::CanPrintDecimal => "can_print_decimal",
                Capability::CanSymbolicPrint => "can_symbolic_print",
                Capability::CanCompareDirect => "can_compare_direct",
                Capability::CanCompareByProof => "can_compare_by_proof",
                Capability::CanComputeModular => "can_compute_modular",
                Capability::CanInspectAst => "can_inspect_ast",
                Capability::CanCompareSyntax => "can_compare_syntax",
                Capability::CanParse => "can_parse",
                Capability::CanRuntimeCompare => "can_runtime_compare",
                Capability::CanCardinalArithmetic => "can_cardinal_arithmetic",
                Capability::CanOrdinalArithmetic => "can_ordinal_arithmetic",
                Capability::CanLimitReason => "can_limit_reason",
                Capability::CanUniverseLevel => "can_universe_level",
                Capability::CanFormSet => "can_form_set",
                Capability::CanFormClass => "can_form_class",
                Capability::CanLiftUniverse => "can_lift_universe",
                Capability::CanClassReason => "can_class_reason",
                Capability::CanSetReason => "can_set_reason",
                Capability::CanDefineInLanguage => "can_define_in_language",
                Capability::CanUseEncoding => "can_use_encoding",
                Capability::CanMetaLevelReason => "can_meta_level_reason",
                Capability::CanDefinabilityReason => "can_definability_reason",
                Capability::CanExtractDefinabilityBound => "can_extract_definability_bound",
                Capability::CanExtractDefinabilityMeta => "can_extract_definability_meta",
                Capability::CanBigNumberReason => "can_big_number_reason",
                Capability::CanExtractGrowthClass => "can_extract_growth_class",
                Capability::CanProofKernelCheck => "can_proof_kernel_check",
                Capability::CanPropositionReason => "can_proposition_reason",
                Capability::CanProvabilityReason => "can_provability_reason",
                Capability::CanTruthBoundaryReason => "can_truth_boundary_reason",
                Capability::CanExtractProvabilityTheory => "can_extract_provability_theory",
                Capability::CanConsistencyReason => "can_consistency_reason",
                Capability::CanIncompletenessReason => "can_incompleteness_reason",
                Capability::CanHostRuntime => "can_host_runtime",
                Capability::CanAcceptMigration => "can_accept_migration",
                Capability::CanMigrateOut => "can_migrate_out",
                Capability::CanSerializeForMigration => "can_serialize_for_migration",
                Capability::CanRemoteSymbolicPrint => "can_remote_symbolic_print",
                Capability::CanCheckpointRemote => "can_checkpoint_remote",
                Capability::CanRestoreRemoteCheckpoint => "can_restore_remote_checkpoint",
                Capability::CanLiveMigrateRemote => "can_live_migrate_remote",
                Capability::CanMaterializeRemote => "can_materialize_remote",
                Capability::CanCompilePortableCode => "can_compile_portable_code",
                Capability::CanDeployPortableCode => "can_deploy_portable_code",
                Capability::CanCrossArchPortable => "can_cross_arch_portable",
                Capability::CanVirtualizeCores => "can_virtualize_cores",
                Capability::CanVirtualizeMemory => "can_virtualize_memory",
                Capability::CanScheduleRuntime => "can_schedule_runtime",
                Capability::CanAllocateDistributedMemory => "can_allocate_distributed_memory",
                Capability::CanUseDistributedMemory => "can_use_distributed_memory",
                Capability::CanCheckpointMemory => "can_checkpoint_memory",
                Capability::CanRestoreCheckpoint => "can_restore_checkpoint",
                Capability::CanHostGpuRuntime => "can_host_gpu_runtime",
                Capability::CanAllocateGpuMemory => "can_allocate_gpu_memory",
                Capability::CanUseGpuMemory => "can_use_gpu_memory",
                Capability::CanCheckpointGpuMemory => "can_checkpoint_gpu_memory",
                Capability::CanLaunchGpuKernel => "can_launch_gpu_kernel",
                Capability::CanCompileGpuKernel => "can_compile_gpu_kernel",
                Capability::CanCopyCpuToGpu => "can_copy_cpu_to_gpu",
                Capability::CanCopyGpuToCpu => "can_copy_gpu_to_cpu",
                Capability::CanGpuPeerTransfer => "can_gpu_peer_transfer",
                Capability::CanGpuUnifiedAddressing => "can_gpu_unified_addressing",
                Capability::RequiresOracle => "requires_oracle",
            })
            .collect()
    }
}

impl fmt::Display for CapabilitySet {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let names = self.names();
        write!(f, "{{{}}}", names.join(", "))
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Passport {
    pub ty: TypeKind,
    pub construction: ConstructionMode,
    pub capabilities: CapabilitySet,
    pub cost: CostClass,
    pub trust: TrustLevel,
    pub provenance: Provenance,
    pub validation: ValidationState,
    pub theory: TheoryContext,
    pub history: HistoryChain,
    pub location: LocationContext,
}

impl Passport {
    pub fn literal_nat(theory: &str) -> Self {
        Self {
            ty: TypeKind::Nat,
            construction: ConstructionMode::Literal,
            capabilities: CapabilitySet::from([
                Capability::CanAddAsNat,
                Capability::CanPrintDecimal,
                Capability::CanSymbolicPrint,
                Capability::CanSerializeForMigration,
                Capability::CanCompilePortableCode,
                Capability::CanCompileGpuKernel,
                Capability::CanCompareDirect,
                Capability::CanComputeModular,
            ]),
            cost: CostClass::Trivial,
            trust: TrustLevel::Checked,
            provenance: Provenance::InternalLiteral,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_event("created:literal_nat"),
            location: LocationContext::local(),
        }
    }

    pub fn compressed_nat(theory: &str) -> Self {
        Self {
            ty: TypeKind::Nat,
            construction: ConstructionMode::Compressed,
            capabilities: CapabilitySet::from([
                Capability::CanAddAsNat,
                Capability::CanSymbolicPrint,
                Capability::CanSerializeForMigration,
                Capability::CanCompilePortableCode,
                Capability::CanCompileGpuKernel,
                Capability::CanCompareByProof,
                Capability::CanComputeModular,
            ]),
            cost: CostClass::Compressed,
            trust: TrustLevel::Checked,
            provenance: Provenance::InternalDerived,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_event("created:compressed_nat"),
            location: LocationContext::local(),
        }
    }

    pub fn recursive_nat(theory: &str) -> Self {
        Self::big_nat(
            theory,
            "Graham",
            None,
            ConstructionMode::Recursive,
            CostClass::NonExpandable,
            "big_number:Graham",
        )
    }

    pub fn definable_noncomputable_nat(theory: &str) -> Self {
        Self::busy_beaver_nat(theory, None)
    }

    pub fn big_nat(
        theory: &str,
        family: impl Into<String>,
        parameter: Option<u128>,
        construction: ConstructionMode,
        cost: CostClass,
        event: impl Into<String>,
    ) -> Self {
        Self {
            ty: TypeKind::BigNat {
                family: family.into(),
                parameter,
            },
            construction,
            capabilities: CapabilitySet::from([
                Capability::CanAddAsNat,
                Capability::CanSymbolicPrint,
                Capability::CanSerializeForMigration,
                Capability::CanCompilePortableCode,
                Capability::CanCompareByProof,
                Capability::CanBigNumberReason,
                Capability::CanExtractGrowthClass,
            ]),
            cost,
            trust: TrustLevel::Builtin,
            provenance: Provenance::BuiltinKnown,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_event(event),
            location: LocationContext::local(),
        }
    }

    pub fn graham_nat(theory: &str) -> Self {
        Self::big_nat(
            theory,
            "Graham",
            None,
            ConstructionMode::Recursive,
            CostClass::NonExpandable,
            "big_number:Graham",
        )
    }

    pub fn tree_nat(theory: &str, parameter: u128) -> Self {
        Self::big_nat(
            theory,
            "TREE",
            Some(parameter),
            ConstructionMode::ProofFinite,
            CostClass::ProofRequired,
            format!("big_number:TREE:{parameter}"),
        )
    }

    pub fn busy_beaver_nat(theory: &str, parameter: Option<u128>) -> Self {
        let event = match parameter {
            Some(parameter) => format!("big_number:BB:{parameter}:definable_noncomputable"),
            None => "big_number:BB:definable_noncomputable".to_string(),
        };
        Self::big_nat(
            theory,
            "BB",
            parameter,
            ConstructionMode::Definable,
            CostClass::Uncomputable,
            event,
        )
    }

    pub fn fast_growing_nat(theory: &str, level: u128) -> Self {
        Self::big_nat(
            theory,
            "FGH",
            Some(level),
            ConstructionMode::Recursive,
            CostClass::Recursive,
            format!("big_number:fast_growing:{level}"),
        )
    }

    pub fn big_number_resource_nat(theory: &str, source: &Passport, event: &str) -> Self {
        Self {
            ty: TypeKind::Nat,
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanAddAsNat,
                Capability::CanPrintDecimal,
                Capability::CanSymbolicPrint,
                Capability::CanCompareDirect,
                Capability::CanComputeModular,
                Capability::CanSerializeForMigration,
                Capability::CanCompilePortableCode,
                Capability::CanCompileGpuKernel,
            ]),
            cost: CostClass::SmallFinite,
            trust: source.trust.max(TrustLevel::Builtin),
            provenance: source.provenance.max(Provenance::BuiltinKnown),
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, event),
            location: LocationContext::local(),
        }
    }

    pub fn cardinal_infinity(theory: &str) -> Self {
        Self {
            ty: TypeKind::Infinity {
                mode: InfinityMode::Cardinal,
            },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanSerializeForMigration,
                Capability::CanCompareByProof,
                Capability::CanCardinalArithmetic,
            ]),
            cost: CostClass::ProofRequired,
            trust: TrustLevel::Builtin,
            provenance: Provenance::BuiltinKnown,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_event("created:infinity_cardinal"),
            location: LocationContext::local(),
        }
    }

    pub fn ordinal_infinity(theory: &str) -> Self {
        Self {
            ty: TypeKind::Infinity {
                mode: InfinityMode::Ordinal,
            },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanSerializeForMigration,
                Capability::CanCompareByProof,
                Capability::CanOrdinalArithmetic,
            ]),
            cost: CostClass::ProofRequired,
            trust: TrustLevel::Builtin,
            provenance: Provenance::BuiltinKnown,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_event("created:infinity_ordinal"),
            location: LocationContext::local(),
        }
    }

    pub fn limit_infinity(theory: &str) -> Self {
        Self {
            ty: TypeKind::Infinity {
                mode: InfinityMode::Limit,
            },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanSerializeForMigration,
                Capability::CanCompareByProof,
                Capability::CanLimitReason,
            ]),
            cost: CostClass::ProofRequired,
            trust: TrustLevel::Builtin,
            provenance: Provenance::BuiltinKnown,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_event("created:infinity_limit"),
            location: LocationContext::local(),
        }
    }

    pub fn potential_infinity(theory: &str) -> Self {
        Self {
            ty: TypeKind::Infinity {
                mode: InfinityMode::Potential,
            },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanSerializeForMigration,
                Capability::CanCompareByProof,
                Capability::CanLimitReason,
            ]),
            cost: CostClass::ProofRequired,
            trust: TrustLevel::Builtin,
            provenance: Provenance::BuiltinKnown,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_event("created:infinity_potential"),
            location: LocationContext::local(),
        }
    }

    pub fn class_infinity(theory: &str, source: &Passport) -> Self {
        Self {
            ty: TypeKind::Infinity {
                mode: InfinityMode::Class,
            },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanSerializeForMigration,
                Capability::CanCompareByProof,
                Capability::CanClassReason,
            ]),
            cost: CostClass::ProofRequired,
            trust: source.trust.max(TrustLevel::Builtin),
            provenance: source.provenance.max(Provenance::BuiltinKnown),
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, "created:infinity_class"),
            location: LocationContext::local(),
        }
    }

    pub fn universe_infinity(theory: &str, source: &Passport) -> Self {
        Self {
            ty: TypeKind::Infinity {
                mode: InfinityMode::Universe,
            },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanSerializeForMigration,
                Capability::CanCompareByProof,
                Capability::CanUniverseLevel,
            ]),
            cost: CostClass::ProofRequired,
            trust: source.trust.max(TrustLevel::Builtin),
            provenance: source.provenance.max(Provenance::BuiltinKnown),
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, "created:infinity_universe"),
            location: LocationContext::local(),
        }
    }

    pub fn infinity_succ_result(source: &Passport, theory: &str) -> Self {
        Self {
            ty: source.ty.clone(),
            construction: source.construction,
            capabilities: source.capabilities.clone(),
            cost: source.cost,
            trust: source.trust,
            provenance: source.provenance,
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, "derived:infinity_succ"),
            location: LocationContext::local(),
        }
    }

    pub fn infinity_binary_result(
        lhs: &Passport,
        rhs: &Passport,
        theory: &str,
        event: &str,
    ) -> Self {
        Self {
            ty: lhs.ty.clone(),
            construction: lhs.construction.max(rhs.construction),
            capabilities: lhs.capabilities.intersection(&rhs.capabilities),
            cost: lhs.cost.max(rhs.cost),
            trust: lhs.trust.max(rhs.trust),
            provenance: lhs.provenance.max(rhs.provenance),
            validation: lhs.validation.max(rhs.validation),
            theory: TheoryContext::new(theory),
            history: HistoryChain::merge2(&lhs.history, &rhs.history, event),
            location: LocationContext::local(),
        }
    }

    pub fn universe(theory: &str, level: u8) -> Self {
        Self {
            ty: TypeKind::Universe { level },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanCompareByProof,
                Capability::CanUniverseLevel,
                Capability::CanFormSet,
                Capability::CanFormClass,
                Capability::CanLiftUniverse,
            ]),
            cost: CostClass::ProofRequired,
            trust: TrustLevel::Builtin,
            provenance: Provenance::BuiltinKnown,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_event(format!("universe:U{level}")),
            location: LocationContext::local(),
        }
    }

    pub fn universe_succ(source: &Passport, theory: &str) -> Self {
        let next = match &source.ty {
            TypeKind::Universe { level } => (*level).saturating_add(1),
            _ => 0,
        };
        let mut value = Self::universe(theory, next);
        value.history =
            HistoryChain::from_source(&source.history, format!("universe:succ:U{next}"));
        value
    }

    pub fn set_of_universe(source: &Passport, theory: &str) -> Self {
        let (of_level, lives_in) = match &source.ty {
            TypeKind::Universe { level } => (*level, (*level).saturating_add(1)),
            _ => (0, 1),
        };
        Self {
            ty: TypeKind::Set { of_level, lives_in },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanCompareByProof,
                Capability::CanSetReason,
            ]),
            cost: CostClass::ProofRequired,
            trust: source.trust.max(TrustLevel::Builtin),
            provenance: source.provenance.max(Provenance::BuiltinKnown),
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(
                &source.history,
                format!("set:of:U{of_level}:lives_in:U{lives_in}"),
            ),
            location: LocationContext::local(),
        }
    }

    pub fn class_of_universe(source: &Passport, theory: &str) -> Self {
        let of_level = match &source.ty {
            TypeKind::Universe { level } => *level,
            _ => 0,
        };
        Self {
            ty: TypeKind::Class { of_level },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanCompareByProof,
                Capability::CanClassReason,
            ]),
            cost: CostClass::ProofRequired,
            trust: source.trust.max(TrustLevel::Builtin),
            provenance: source.provenance.max(Provenance::BuiltinKnown),
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, format!("class:of:U{of_level}")),
            location: LocationContext::local(),
        }
    }

    pub fn universe_level_nat(source: &Passport, theory: &str, event: &str) -> Self {
        Self {
            ty: TypeKind::Nat,
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanAddAsNat,
                Capability::CanPrintDecimal,
                Capability::CanSymbolicPrint,
                Capability::CanCompareDirect,
                Capability::CanComputeModular,
                Capability::CanSerializeForMigration,
                Capability::CanCompilePortableCode,
                Capability::CanCompileGpuKernel,
            ]),
            cost: CostClass::SmallFinite,
            trust: source.trust.max(TrustLevel::Builtin),
            provenance: source.provenance.max(Provenance::BuiltinKnown),
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, event),
            location: LocationContext::local(),
        }
    }

    pub fn language(theory: &str, name: impl Into<String>) -> Self {
        let name = name.into();
        Self {
            ty: TypeKind::Language { name: name.clone() },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanDefineInLanguage,
            ]),
            cost: CostClass::ProofRequired,
            trust: TrustLevel::Builtin,
            provenance: Provenance::BuiltinKnown,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_event(format!("definability:language:{name}")),
            location: LocationContext::local(),
        }
    }

    pub fn encoding(theory: &str, name: impl Into<String>) -> Self {
        let name = name.into();
        Self {
            ty: TypeKind::Encoding { name: name.clone() },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanUseEncoding,
            ]),
            cost: CostClass::ProofRequired,
            trust: TrustLevel::Builtin,
            provenance: Provenance::BuiltinKnown,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_event(format!("definability:encoding:{name}")),
            location: LocationContext::local(),
        }
    }

    pub fn meta_level(theory: &str, level: u8, source_history: Option<&HistoryChain>) -> Self {
        let history = match source_history {
            Some(history) => {
                HistoryChain::from_source(history, format!("definability:meta_level:M{level}"))
            }
            None => HistoryChain::from_event(format!("definability:meta_level:M{level}")),
        };
        Self {
            ty: TypeKind::MetaLevel { level },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanMetaLevelReason,
            ]),
            cost: CostClass::ProofRequired,
            trust: TrustLevel::Builtin,
            provenance: Provenance::BuiltinKnown,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history,
            location: LocationContext::local(),
        }
    }

    pub fn definable_nat(
        theory: &str,
        language: &Passport,
        encoding: &Passport,
        bound: u128,
        meta: &Passport,
    ) -> Self {
        let language_name = match &language.ty {
            TypeKind::Language { name } => name.clone(),
            _ => "unknown_language".to_string(),
        };
        let encoding_name = match &encoding.ty {
            TypeKind::Encoding { name } => name.clone(),
            _ => "unknown_encoding".to_string(),
        };
        let meta_level = match &meta.ty {
            TypeKind::MetaLevel { level } => *level,
            _ => 0,
        };
        Self {
            ty: TypeKind::DefinableNat {
                language: language_name.clone(),
                encoding: encoding_name.clone(),
                object_theory: theory.to_string(),
                bound,
                meta_level,
            },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanCompareByProof,
                Capability::CanDefinabilityReason,
                Capability::CanExtractDefinabilityBound,
                Capability::CanExtractDefinabilityMeta,
            ]),
            cost: CostClass::ProofRequired,
            trust: language.trust.max(encoding.trust).max(meta.trust).max(TrustLevel::Builtin),
            provenance: language.provenance.max(encoding.provenance).max(meta.provenance).max(Provenance::BuiltinKnown),
            validation: language.validation.max(encoding.validation).max(meta.validation),
            theory: TheoryContext::new(theory),
            history: HistoryChain::merge_many(
                [&language.history, &encoding.history, &meta.history],
                format!("definability:definable_nat:{language_name}:{encoding_name}:bound:{bound}:M{meta_level}"),
            ),
            location: LocationContext::local(),
        }
    }

    pub fn definability_resource_nat(theory: &str, source: &Passport, event: &str) -> Self {
        Self {
            ty: TypeKind::Nat,
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanAddAsNat,
                Capability::CanPrintDecimal,
                Capability::CanSymbolicPrint,
                Capability::CanCompareDirect,
                Capability::CanComputeModular,
                Capability::CanSerializeForMigration,
                Capability::CanCompilePortableCode,
                Capability::CanCompileGpuKernel,
            ]),
            cost: CostClass::SmallFinite,
            trust: source.trust.max(TrustLevel::Builtin),
            provenance: source.provenance.max(Provenance::BuiltinKnown),
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, event),
            location: LocationContext::local(),
        }
    }

    pub fn axiom_bool(theory: &str) -> Self {
        Self {
            ty: TypeKind::Bool,
            construction: ConstructionMode::ProofFinite,
            capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
            cost: CostClass::ProofRequired,
            trust: TrustLevel::Axiom,
            provenance: Provenance::BuiltinKnown,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_event("axiom:bool"),
            location: LocationContext::local(),
        }
    }

    pub fn axiom_nat(theory: &str) -> Self {
        Self {
            ty: TypeKind::Nat,
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanAddAsNat,
                Capability::CanSymbolicPrint,
                Capability::CanSerializeForMigration,
                Capability::CanCompareByProof,
            ]),
            cost: CostClass::ProofRequired,
            trust: TrustLevel::Axiom,
            provenance: Provenance::BuiltinKnown,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_event("axiom:nat"),
            location: LocationContext::local(),
        }
    }

    pub fn unsafe_nat(theory: &str) -> Self {
        Self {
            ty: TypeKind::Nat,
            construction: ConstructionMode::Unsafe,
            capabilities: CapabilitySet::from([
                Capability::CanAddAsNat,
                Capability::CanPrintDecimal,
                Capability::CanSymbolicPrint,
                Capability::CanSerializeForMigration,
                Capability::CanCompareDirect,
                Capability::CanComputeModular,
            ]),
            cost: CostClass::UnsafeUnknown,
            trust: TrustLevel::Unsafe,
            provenance: Provenance::UnsafeExternal,
            validation: ValidationState::Assumed,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_event("unsafe:assumed_nat"),
            location: LocationContext::local(),
        }
    }

    pub fn runtime_nat_from_input(theory: &str) -> Self {
        Self {
            ty: TypeKind::Nat,
            construction: ConstructionMode::External,
            capabilities: CapabilitySet::from([
                Capability::CanAddAsNat,
                Capability::CanPrintDecimal,
                Capability::CanSymbolicPrint,
                Capability::CanSerializeForMigration,
                Capability::CanCompareDirect,
                Capability::CanRuntimeCompare,
                Capability::CanComputeModular,
            ]),
            cost: CostClass::SmallFinite,
            trust: TrustLevel::Checked,
            provenance: Provenance::RuntimeInput,
            validation: ValidationState::Parsed,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_event("runtime_input:read_nat"),
            location: LocationContext::local(),
        }
    }

    pub fn runtime_witness(theory: &str, predicate: impl Into<String>, source: &Passport) -> Self {
        Self {
            ty: TypeKind::RuntimeWitness(predicate.into()),
            construction: ConstructionMode::External,
            capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
            cost: source.cost,
            trust: source.trust,
            provenance: source.provenance,
            validation: ValidationState::RuntimeChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, "runtime_witness:require"),
            location: LocationContext::local(),
        }
    }

    pub fn static_proof(theory: &str, predicate: impl Into<String>, source: &Passport) -> Self {
        Self {
            ty: TypeKind::StaticProof(predicate.into()),
            construction: ConstructionMode::ProofFinite,
            capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
            cost: CostClass::ProofRequired,
            trust: source.trust,
            provenance: source.provenance,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, "static_proof:prove"),
            location: LocationContext::local(),
        }
    }

    pub fn proposition(
        theory: &str,
        name: impl Into<String>,
        source: Option<&Passport>,
        event: impl Into<String>,
    ) -> Self {
        let name = name.into();
        let (trust, provenance, validation, history) = match source {
            Some(source) => (
                source.trust.max(TrustLevel::Builtin),
                source.provenance.max(Provenance::BuiltinKnown),
                source.validation,
                HistoryChain::from_source(&source.history, event),
            ),
            None => (
                TrustLevel::Builtin,
                Provenance::BuiltinKnown,
                ValidationState::StaticChecked,
                HistoryChain::from_event(event),
            ),
        };
        Self {
            ty: TypeKind::Prop { name },
            construction: ConstructionMode::ProofFinite,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanCompareByProof,
                Capability::CanPropositionReason,
            ]),
            cost: CostClass::ProofRequired,
            trust,
            provenance,
            validation,
            theory: TheoryContext::new(theory),
            history,
            location: LocationContext::local(),
        }
    }

    pub fn provable_claim(
        theory: &str,
        object_theory: impl Into<String>,
        proposition: impl Into<String>,
        source: &Passport,
    ) -> Self {
        let object_theory = object_theory.into();
        let proposition = proposition.into();
        Self {
            ty: TypeKind::Provable {
                object_theory: object_theory.clone(),
                proposition: proposition.clone(),
            },
            construction: ConstructionMode::ProofFinite,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanProvabilityReason,
                Capability::CanExtractProvabilityTheory,
            ]),
            cost: CostClass::ProofRequired,
            trust: source.trust.max(TrustLevel::Builtin),
            provenance: source.provenance.max(Provenance::BuiltinKnown),
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(
                &source.history,
                format!("provability:of:{object_theory}:{proposition}"),
            ),
            location: LocationContext::local(),
        }
    }

    pub fn provability_resource_nat(theory: &str, source: &Passport, event: &str) -> Self {
        Self {
            ty: TypeKind::Nat,
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanAddAsNat,
                Capability::CanPrintDecimal,
                Capability::CanSymbolicPrint,
                Capability::CanCompareDirect,
                Capability::CanComputeModular,
                Capability::CanSerializeForMigration,
                Capability::CanCompilePortableCode,
                Capability::CanCompileGpuKernel,
            ]),
            cost: CostClass::SmallFinite,
            trust: source.trust.max(TrustLevel::Builtin),
            provenance: source.provenance.max(Provenance::BuiltinKnown),
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, event),
            location: LocationContext::local(),
        }
    }

    pub fn proof_term(theory: &str, rule: impl Into<String>, source: Option<&Passport>) -> Self {
        let rule = rule.into();
        let (trust, provenance, validation, history) = match source {
            Some(source) => (
                source.trust,
                source.provenance,
                source.validation,
                HistoryChain::from_source(&source.history, format!("proof_kernel:term:{rule}")),
            ),
            None => (
                TrustLevel::Checked,
                Provenance::InternalDerived,
                ValidationState::StaticChecked,
                HistoryChain::from_event(format!("proof_kernel:term:{rule}")),
            ),
        };
        Self {
            ty: TypeKind::ProofTerm { rule },
            construction: ConstructionMode::ProofFinite,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanProofKernelCheck,
            ]),
            cost: CostClass::ProofRequired,
            trust,
            provenance,
            validation,
            theory: TheoryContext::new(theory),
            history,
            location: LocationContext::local(),
        }
    }

    pub fn kernel_checked_proof(
        theory: &str,
        predicate: impl Into<String>,
        source: &Passport,
    ) -> Self {
        Self {
            ty: TypeKind::StaticProof(predicate.into()),
            construction: ConstructionMode::ProofFinite,
            capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
            cost: CostClass::ProofRequired,
            trust: source.trust,
            provenance: source.provenance,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, "proof_kernel:check"),
            location: LocationContext::local(),
        }
    }

    pub fn axiom_truth_from_provable(
        theory: &str,
        predicate: impl Into<String>,
        source: &Passport,
    ) -> Self {
        let predicate = predicate.into();
        Self {
            ty: TypeKind::StaticProof(format!("truth_from_provable:{predicate}")),
            construction: ConstructionMode::ProofFinite,
            capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
            cost: CostClass::ProofRequired,
            trust: TrustLevel::Axiom,
            provenance: source.provenance.max(Provenance::BuiltinKnown),
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, "truth:from_provable_axiom"),
            location: LocationContext::local(),
        }
    }

    pub fn reflection_claim(
        theory: &str,
        object_theory: impl Into<String>,
        proposition: impl Into<String>,
        source: &Passport,
    ) -> Self {
        let object_theory = object_theory.into();
        let proposition = proposition.into();
        Self {
            ty: TypeKind::ReflectionClaim {
                object_theory: object_theory.clone(),
                proposition: proposition.clone(),
            },
            construction: ConstructionMode::ProofFinite,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanProvabilityReason,
                Capability::CanTruthBoundaryReason,
            ]),
            cost: CostClass::ProofRequired,
            trust: source.trust.max(TrustLevel::Builtin),
            provenance: source.provenance.max(Provenance::BuiltinKnown),
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(
                &source.history,
                format!("reflection:claim:{object_theory}:{proposition}"),
            ),
            location: LocationContext::local(),
        }
    }

    pub fn axiom_reflection_proof(
        theory: &str,
        object_theory: impl Into<String>,
        proposition: impl Into<String>,
        source: &Passport,
    ) -> Self {
        let object_theory = object_theory.into();
        let proposition = proposition.into();
        Self {
            ty: TypeKind::StaticProof(format!("reflection_axiom:{object_theory}:{proposition}")),
            construction: ConstructionMode::ProofFinite,
            capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
            cost: CostClass::ProofRequired,
            trust: TrustLevel::Axiom,
            provenance: source.provenance.max(Provenance::BuiltinKnown),
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(
                &source.history,
                format!("reflection:axiom:{object_theory}:{proposition}"),
            ),
            location: LocationContext::local(),
        }
    }

    pub fn self_reference_claim(
        theory: &str,
        proposition: impl Into<String>,
        source: Option<&Passport>,
        event: impl Into<String>,
    ) -> Self {
        let proposition = proposition.into();
        let event = event.into();
        let (trust, provenance, validation, history) = match source {
            Some(source) => (
                source.trust.max(TrustLevel::Builtin),
                source.provenance.max(Provenance::BuiltinKnown),
                source.validation,
                HistoryChain::from_source(&source.history, event),
            ),
            None => (
                TrustLevel::Builtin,
                Provenance::BuiltinKnown,
                ValidationState::StaticChecked,
                HistoryChain::from_event(event),
            ),
        };
        Self {
            ty: TypeKind::SelfReferenceClaim {
                proposition: proposition.clone(),
            },
            construction: ConstructionMode::ProofFinite,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanPropositionReason,
                Capability::CanTruthBoundaryReason,
            ]),
            cost: CostClass::ProofRequired,
            trust,
            provenance,
            validation,
            theory: TheoryContext::new(theory),
            history,
            location: LocationContext::local(),
        }
    }

    pub fn axiom_self_reference_proof(
        theory: &str,
        proposition: impl Into<String>,
        source: &Passport,
    ) -> Self {
        let proposition = proposition.into();
        Self {
            ty: TypeKind::StaticProof(format!("self_reference_axiom:{proposition}")),
            construction: ConstructionMode::ProofFinite,
            capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
            cost: CostClass::ProofRequired,
            trust: TrustLevel::Axiom,
            provenance: source.provenance.max(Provenance::BuiltinKnown),
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(
                &source.history,
                format!("self_reference:axiom:{proposition}"),
            ),
            location: LocationContext::local(),
        }
    }

    pub fn consistency_claim(
        theory: &str,
        target_theory: impl Into<String>,
        source: Option<&Passport>,
    ) -> Self {
        let target_theory = target_theory.into();
        let history = match source {
            Some(source) => HistoryChain::from_source(
                &source.history,
                format!("consistency:claim:{target_theory}"),
            ),
            None => HistoryChain::from_event(format!("consistency:claim:{target_theory}")),
        };
        let (trust, provenance, validation) = match source {
            Some(source) => (
                source.trust.max(TrustLevel::Builtin),
                source.provenance.max(Provenance::BuiltinKnown),
                source.validation,
            ),
            None => (
                TrustLevel::Builtin,
                Provenance::BuiltinKnown,
                ValidationState::StaticChecked,
            ),
        };
        Self {
            ty: TypeKind::ConsistencyClaim {
                theory: target_theory,
            },
            construction: ConstructionMode::ProofFinite,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanConsistencyReason,
                Capability::CanIncompletenessReason,
            ]),
            cost: CostClass::ProofRequired,
            trust,
            provenance,
            validation,
            theory: TheoryContext::new(theory),
            history,
            location: LocationContext::local(),
        }
    }

    pub fn axiom_consistency_proof(
        theory: &str,
        target_theory: impl Into<String>,
        source: &Passport,
    ) -> Self {
        let target_theory = target_theory.into();
        Self {
            ty: TypeKind::StaticProof(format!("consistency_axiom:{target_theory}")),
            construction: ConstructionMode::ProofFinite,
            capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
            cost: CostClass::ProofRequired,
            trust: TrustLevel::Axiom,
            provenance: source.provenance.max(Provenance::BuiltinKnown),
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(
                &source.history,
                format!("consistency:axiom:{target_theory}"),
            ),
            location: LocationContext::local(),
        }
    }

    pub fn raw_external_bytes(theory: &str) -> Self {
        Self {
            ty: TypeKind::Bytes,
            construction: ConstructionMode::External,
            capabilities: CapabilitySet::from([Capability::CanParse]),
            cost: CostClass::SmallFinite,
            trust: TrustLevel::Checked,
            provenance: Provenance::RuntimeInput,
            validation: ValidationState::Raw,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_event("runtime_input:raw_bytes"),
            location: LocationContext::local(),
        }
    }

    pub fn term_of(
        theory: &str,
        source_theory: &str,
        source_type: &str,
        bridge_name: &str,
        source: &Passport,
    ) -> Self {
        let mut ctx = TheoryContext::new(source_theory);
        ctx.valid_in.insert(theory.to_string());
        ctx.bridge_trace.push(bridge_name.to_string());
        Self {
            ty: TypeKind::Term {
                of_theory: source_theory.to_string(),
                of_type: source_type.to_string(),
            },
            construction: ConstructionMode::Literal,
            capabilities: CapabilitySet::from([
                Capability::CanInspectAst,
                Capability::CanCompareSyntax,
                Capability::CanSymbolicPrint,
            ]),
            cost: CostClass::SmallFinite,
            trust: TrustLevel::Builtin,
            provenance: Provenance::InternalDerived,
            validation: ValidationState::StaticChecked,
            theory: ctx,
            history: HistoryChain::from_source(
                &source.history,
                format!("bridge:quote:{bridge_name}"),
            ),
            location: LocationContext::local(),
        }
    }

    pub fn transported_to(&self, target_theory: &str, bridge_name: &str) -> Self {
        let mut transported = self.clone();
        let source_home = self.theory.home.clone();
        transported.theory.home = target_theory.to_string();
        transported.theory.valid_in.insert(source_home);
        transported
            .theory
            .valid_in
            .insert(target_theory.to_string());
        transported
            .theory
            .bridge_trace
            .push(bridge_name.to_string());
        transported
            .history
            .push(format!("bridge:transport:{bridge_name}"));
        transported
    }

    pub fn soundness_proof(
        theory: &str,
        predicate: impl Into<String>,
        source: &Passport,
        bridge_name: &str,
    ) -> Self {
        let mut ctx = TheoryContext::new(theory);
        ctx.valid_in.insert(source.theory.home.clone());
        ctx.bridge_trace.push(bridge_name.to_string());
        let mut history =
            HistoryChain::from_source(&source.history, format!("bridge:soundness:{bridge_name}"));
        history.push("axiom:soundness_assumption");
        Self {
            ty: TypeKind::StaticProof(predicate.into()),
            construction: ConstructionMode::ProofFinite,
            capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
            cost: CostClass::ProofRequired,
            trust: source.trust.max(TrustLevel::Axiom),
            provenance: source.provenance,
            validation: ValidationState::StaticChecked,
            theory: ctx,
            history,
            location: LocationContext::local(),
        }
    }

    pub fn node(theory: &str, arch: NodeArch) -> Self {
        Self::node_with_resources(theory, arch, None, None)
    }

    pub fn node_with_resources(
        theory: &str,
        arch: NodeArch,
        cores: Option<u128>,
        memory_mib: Option<u128>,
    ) -> Self {
        let mut history = HistoryChain::from_event(format!("node:{arch}"));
        if let Some(cores) = cores {
            history.push(format!("node_resource:cores:{cores}"));
        }
        if let Some(memory_mib) = memory_mib {
            history.push(format!("node_resource:memory_mib:{memory_mib}"));
        }
        Self {
            ty: TypeKind::Node { arch },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanHostRuntime,
                Capability::CanAcceptMigration,
                Capability::CanSymbolicPrint,
                Capability::CanCrossArchPortable,
            ]),
            cost: CostClass::SmallFinite,
            trust: TrustLevel::Builtin,
            provenance: Provenance::BuiltinKnown,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history,
            location: LocationContext::node(arch),
        }
    }

    pub fn virtual_cluster(theory: &str, nodes: &[Passport]) -> Self {
        Self {
            ty: TypeKind::VirtualCluster,
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanHostRuntime,
                Capability::CanSymbolicPrint,
                Capability::CanVirtualizeCores,
                Capability::CanVirtualizeMemory,
                Capability::CanScheduleRuntime,
                Capability::CanAllocateDistributedMemory,
            ]),
            cost: CostClass::SmallFinite,
            trust: nodes
                .iter()
                .map(|n| n.trust)
                .max()
                .unwrap_or(TrustLevel::Builtin)
                .max(TrustLevel::Builtin),
            provenance: Provenance::InternalDerived,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::merge_many(
                nodes.iter().map(|n| &n.history),
                "cluster:virtual_pool",
            ),
            location: LocationContext::local(),
        }
    }

    pub fn cluster_resource_nat(theory: &str, source: &Passport, event: &str) -> Self {
        Self {
            ty: TypeKind::Nat,
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanAddAsNat,
                Capability::CanPrintDecimal,
                Capability::CanSymbolicPrint,
                Capability::CanCompareDirect,
                Capability::CanComputeModular,
            ]),
            cost: CostClass::SmallFinite,
            trust: source.trust,
            provenance: source.provenance,
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, event),
            location: LocationContext::local(),
        }
    }

    pub fn distributed_memory_region(theory: &str, pool: &Passport, memory_mib: u128) -> Self {
        Self {
            ty: TypeKind::DistributedMemory {
                memory_mib: Some(memory_mib),
            },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanUseDistributedMemory,
                Capability::CanCheckpointMemory,
            ]),
            cost: pool.cost.max(CostClass::SmallFinite),
            trust: pool.trust,
            provenance: pool.provenance,
            validation: pool.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(
                &pool.history,
                format!("memory:distributed_region:{memory_mib}MiB"),
            ),
            location: LocationContext::local(),
        }
    }

    pub fn memory_region_resource_nat(theory: &str, source: &Passport, event: &str) -> Self {
        Self {
            ty: TypeKind::Nat,
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanAddAsNat,
                Capability::CanPrintDecimal,
                Capability::CanSymbolicPrint,
                Capability::CanCompareDirect,
                Capability::CanComputeModular,
            ]),
            cost: CostClass::SmallFinite,
            trust: source.trust,
            provenance: source.provenance,
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, event),
            location: LocationContext::local(),
        }
    }

    pub fn memory_checkpoint(theory: &str, source: &Passport) -> Self {
        let memory_mib = match &source.ty {
            TypeKind::DistributedMemory { memory_mib } => *memory_mib,
            _ => None,
        };
        Self {
            ty: TypeKind::MemoryCheckpoint { memory_mib },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanRestoreCheckpoint,
            ]),
            cost: source.cost.max(CostClass::SmallFinite),
            trust: source.trust,
            provenance: source.provenance,
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, "checkpoint:memory"),
            location: LocationContext::local(),
        }
    }

    pub fn restored_memory_region(theory: &str, source: &Passport) -> Self {
        let memory_mib = match &source.ty {
            TypeKind::MemoryCheckpoint { memory_mib } => *memory_mib,
            _ => None,
        };
        Self {
            ty: TypeKind::DistributedMemory { memory_mib },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanUseDistributedMemory,
                Capability::CanCheckpointMemory,
            ]),
            cost: source.cost.max(CostClass::SmallFinite),
            trust: source.trust,
            provenance: source.provenance,
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, "checkpoint:restore_memory"),
            location: LocationContext::local(),
        }
    }

    pub fn migrated_to(
        &self,
        target_theory: &str,
        target_arch: NodeArch,
        bridge_name: &str,
    ) -> Self {
        let mut ctx = TheoryContext::new(target_theory);
        ctx.valid_in.insert(self.theory.home.clone());
        ctx.bridge_trace.push(bridge_name.to_string());
        Self {
            ty: TypeKind::Remote {
                inner: Box::new(self.ty.clone()),
                target_arch,
            },
            construction: self.construction,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanRemoteSymbolicPrint,
                Capability::CanCheckpointRemote,
                Capability::CanLiveMigrateRemote,
                Capability::CanMaterializeRemote,
            ]),
            cost: self.cost.max(CostClass::SmallFinite),
            trust: self.trust,
            provenance: self.provenance,
            validation: self.validation,
            theory: ctx,
            history: HistoryChain::from_source(
                &self.history,
                format!("migration:{bridge_name}:to:{target_arch}"),
            ),
            location: LocationContext::remote(target_arch),
        }
    }

    pub fn scheduled_to(
        &self,
        target_theory: &str,
        target_arch: NodeArch,
        bridge_name: &str,
        pool: &Passport,
        target: &Passport,
    ) -> Self {
        let mut ctx = TheoryContext::new(target_theory);
        ctx.valid_in.insert(self.theory.home.clone());
        if bridge_name != "local_schedule" {
            ctx.bridge_trace.push(bridge_name.to_string());
        }
        Self {
            ty: TypeKind::Remote {
                inner: Box::new(self.ty.clone()),
                target_arch,
            },
            construction: self.construction,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanRemoteSymbolicPrint,
                Capability::CanCheckpointRemote,
                Capability::CanLiveMigrateRemote,
                Capability::CanMaterializeRemote,
            ]),
            cost: self
                .cost
                .max(pool.cost)
                .max(target.cost)
                .max(CostClass::SmallFinite),
            trust: self.trust.max(pool.trust).max(target.trust),
            provenance: self.provenance.max(pool.provenance).max(target.provenance),
            validation: self.validation.max(pool.validation).max(target.validation),
            theory: ctx,
            history: HistoryChain::merge_many(
                [&pool.history, &target.history, &self.history],
                format!("cluster:schedule:{bridge_name}:to:{target_arch}"),
            ),
            location: LocationContext::remote(target_arch),
        }
    }

    pub fn portable_code(theory: &str, source: &Passport) -> Self {
        Self {
            ty: TypeKind::PortableCode {
                inner: Box::new(source.ty.clone()),
            },
            construction: source.construction.max(ConstructionMode::Definable),
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanDeployPortableCode,
                Capability::CanCrossArchPortable,
            ]),
            cost: source.cost.max(CostClass::SmallFinite),
            trust: source.trust,
            provenance: source.provenance,
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, "portable:compile"),
            location: LocationContext::local(),
        }
    }

    pub fn deployed_portable_code(
        theory: &str,
        code: &Passport,
        target: &Passport,
        target_arch: NodeArch,
        event: impl Into<String>,
    ) -> Self {
        let inner = match &code.ty {
            TypeKind::PortableCode { inner } => (**inner).clone(),
            _ => TypeKind::Unknown("PortableCodeInner".to_string()),
        };
        Self {
            ty: TypeKind::Remote {
                inner: Box::new(inner),
                target_arch,
            },
            construction: code.construction,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanRemoteSymbolicPrint,
                Capability::CanCheckpointRemote,
                Capability::CanLiveMigrateRemote,
                Capability::CanMaterializeRemote,
            ]),
            cost: code.cost.max(target.cost).max(CostClass::SmallFinite),
            trust: code.trust.max(target.trust),
            provenance: code.provenance.max(target.provenance),
            validation: code.validation.max(target.validation),
            theory: TheoryContext::new(theory),
            history: HistoryChain::merge2(&code.history, &target.history, event),
            location: LocationContext::remote(target_arch),
        }
    }

    pub fn remote_checkpoint(theory: &str, source: &Passport) -> Self {
        let (inner, source_arch) = match &source.ty {
            TypeKind::Remote { inner, target_arch } => ((**inner).clone(), *target_arch),
            _ => (
                TypeKind::Unknown("RemoteCheckpointSource".to_string()),
                NodeArch::X86_64,
            ),
        };
        Self {
            ty: TypeKind::RemoteCheckpoint {
                inner: Box::new(inner),
                source_arch,
            },
            construction: source.construction,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanRestoreRemoteCheckpoint,
                Capability::CanCrossArchPortable,
            ]),
            cost: source.cost.max(CostClass::SmallFinite),
            trust: source.trust,
            provenance: source.provenance,
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, "checkpoint:remote"),
            location: LocationContext::local(),
        }
    }

    pub fn restored_remote(
        theory: &str,
        checkpoint: &Passport,
        target: &Passport,
        target_arch: NodeArch,
    ) -> Self {
        let inner = match &checkpoint.ty {
            TypeKind::RemoteCheckpoint { inner, .. } => (**inner).clone(),
            _ => TypeKind::Unknown("RemoteCheckpointInner".to_string()),
        };
        Self {
            ty: TypeKind::Remote {
                inner: Box::new(inner),
                target_arch,
            },
            construction: checkpoint.construction,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanRemoteSymbolicPrint,
                Capability::CanCheckpointRemote,
                Capability::CanLiveMigrateRemote,
                Capability::CanMaterializeRemote,
            ]),
            cost: checkpoint.cost.max(target.cost).max(CostClass::SmallFinite),
            trust: checkpoint.trust.max(target.trust),
            provenance: checkpoint.provenance.max(target.provenance),
            validation: checkpoint.validation.max(target.validation),
            theory: TheoryContext::new(theory),
            history: HistoryChain::merge2(
                &checkpoint.history,
                &target.history,
                format!("checkpoint:restore_remote:to:{target_arch}"),
            ),
            location: LocationContext::remote(target_arch),
        }
    }

    pub fn live_migrated_remote(
        theory: &str,
        source: &Passport,
        target: &Passport,
        target_arch: NodeArch,
    ) -> Self {
        let inner = match &source.ty {
            TypeKind::Remote { inner, .. } => (**inner).clone(),
            _ => TypeKind::Unknown("RemoteInner".to_string()),
        };
        Self {
            ty: TypeKind::Remote {
                inner: Box::new(inner),
                target_arch,
            },
            construction: source.construction,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanRemoteSymbolicPrint,
                Capability::CanCheckpointRemote,
                Capability::CanLiveMigrateRemote,
                Capability::CanMaterializeRemote,
            ]),
            cost: source.cost.max(target.cost).max(CostClass::SmallFinite),
            trust: source.trust.max(target.trust),
            provenance: source.provenance.max(target.provenance),
            validation: source.validation.max(target.validation),
            theory: TheoryContext::new(theory),
            history: HistoryChain::merge2(
                &source.history,
                &target.history,
                format!("migration:live_remote:to:{target_arch}"),
            ),
            location: LocationContext::remote(target_arch),
        }
    }

    pub fn materialized_remote(theory: &str, source: &Passport, bridge_name: &str) -> Self {
        let inner = match &source.ty {
            TypeKind::Remote { inner, .. } => (**inner).clone(),
            _ => TypeKind::Unknown("RemoteInner".to_string()),
        };
        let capabilities =
            Self::capabilities_for_materialized_inner(&inner, source.construction, source.cost);
        let event = if bridge_name == "local_materialize" {
            "remote:materialize:local".to_string()
        } else {
            format!("remote:materialize:{bridge_name}")
        };
        Self {
            ty: inner,
            construction: source.construction,
            capabilities,
            cost: source.cost.max(CostClass::SmallFinite),
            trust: source.trust,
            provenance: source.provenance,
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, event),
            location: LocationContext::local(),
        }
    }

    fn capabilities_for_materialized_inner(
        inner: &TypeKind,
        construction: ConstructionMode,
        cost: CostClass,
    ) -> CapabilitySet {
        match inner {
            TypeKind::Nat => {
                let mut caps = CapabilitySet::from([
                    Capability::CanAddAsNat,
                    Capability::CanSymbolicPrint,
                    Capability::CanSerializeForMigration,
                    Capability::CanCompilePortableCode,
                    Capability::CanCompileGpuKernel,
                ]);
                if construction == ConstructionMode::Literal && cost <= CostClass::SmallFinite {
                    caps.insert(Capability::CanPrintDecimal);
                    caps.insert(Capability::CanCompareDirect);
                    caps.insert(Capability::CanComputeModular);
                } else {
                    caps.insert(Capability::CanCompareByProof);
                    if cost < CostClass::Uncomputable {
                        caps.insert(Capability::CanComputeModular);
                    }
                }
                caps
            }
            TypeKind::Universe { .. } => CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanCompareByProof,
                Capability::CanUniverseLevel,
                Capability::CanFormSet,
                Capability::CanFormClass,
                Capability::CanLiftUniverse,
            ]),
            TypeKind::Set { .. } => CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanCompareByProof,
                Capability::CanSetReason,
            ]),
            TypeKind::Class { .. } => CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanCompareByProof,
                Capability::CanClassReason,
            ]),
            TypeKind::Language { .. } => CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanDefineInLanguage,
            ]),
            TypeKind::Encoding { .. } => {
                CapabilitySet::from([Capability::CanSymbolicPrint, Capability::CanUseEncoding])
            }
            TypeKind::MetaLevel { .. } => {
                CapabilitySet::from([Capability::CanSymbolicPrint, Capability::CanMetaLevelReason])
            }
            TypeKind::DefinableNat { .. } => CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanCompareByProof,
                Capability::CanDefinabilityReason,
                Capability::CanExtractDefinabilityBound,
                Capability::CanExtractDefinabilityMeta,
            ]),
            TypeKind::Bool | TypeKind::Text => CapabilitySet::from([Capability::CanSymbolicPrint]),
            TypeKind::ReflectionClaim { .. }
            | TypeKind::SelfReferenceClaim { .. }
            | TypeKind::Statement { .. }
            | TypeKind::Theorem { .. }
            | TypeKind::Goal { .. }
            | TypeKind::Hypothesis { .. } => CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanPropositionReason,
            ]),
            TypeKind::Infinity { mode } => match mode {
                InfinityMode::Cardinal => CapabilitySet::from([
                    Capability::CanSymbolicPrint,
                    Capability::CanSerializeForMigration,
                    Capability::CanCompareByProof,
                    Capability::CanCardinalArithmetic,
                ]),
                InfinityMode::Ordinal => CapabilitySet::from([
                    Capability::CanSymbolicPrint,
                    Capability::CanSerializeForMigration,
                    Capability::CanCompareByProof,
                    Capability::CanOrdinalArithmetic,
                ]),
                _ => CapabilitySet::from([
                    Capability::CanSymbolicPrint,
                    Capability::CanSerializeForMigration,
                    Capability::CanCompareByProof,
                ]),
            },
            _ => CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanSerializeForMigration,
            ]),
        }
    }

    pub fn gpu_device_with(theory: &str, backend: GpuBackend, memory_mib: Option<u128>) -> Self {
        let mut history = HistoryChain::from_event(format!("gpu:{backend}"));
        if let Some(memory_mib) = memory_mib {
            history.push(format!("gpu_resource:memory_mib:{memory_mib}"));
        }
        Self {
            ty: TypeKind::GpuDevice { backend },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanHostGpuRuntime,
                Capability::CanAcceptMigration,
                Capability::CanAllocateGpuMemory,
                Capability::CanGpuPeerTransfer,
                Capability::CanGpuUnifiedAddressing,
            ]),
            cost: CostClass::SmallFinite,
            trust: TrustLevel::Builtin,
            provenance: Provenance::BuiltinKnown,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history,
            location: LocationContext::local(),
        }
    }

    pub fn gpu_pool(theory: &str, devices: &[Passport]) -> Self {
        Self {
            ty: TypeKind::GpuPool,
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanHostGpuRuntime,
                Capability::CanAllocateGpuMemory,
                Capability::CanGpuPeerTransfer,
                Capability::CanGpuUnifiedAddressing,
                Capability::CanLaunchGpuKernel,
            ]),
            cost: CostClass::SmallFinite,
            trust: devices
                .iter()
                .map(|item| item.trust)
                .max()
                .unwrap_or(TrustLevel::Builtin),
            provenance: devices
                .iter()
                .map(|item| item.provenance)
                .max()
                .unwrap_or(Provenance::BuiltinKnown),
            validation: devices
                .iter()
                .map(|item| item.validation)
                .max()
                .unwrap_or(ValidationState::StaticChecked),
            theory: TheoryContext::new(theory),
            history: HistoryChain::merge_many(
                devices.iter().map(|item| &item.history),
                "gpu_pool:create",
            ),
            location: LocationContext::local(),
        }
    }

    pub fn distributed_gpu_memory_region(theory: &str, pool: &Passport, memory_mib: u128) -> Self {
        Self {
            ty: TypeKind::DistributedGpuMemory {
                memory_mib: Some(memory_mib),
            },
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanUseGpuMemory,
                Capability::CanCheckpointGpuMemory,
                Capability::CanLaunchGpuKernel,
                Capability::CanCopyCpuToGpu,
                Capability::CanCopyGpuToCpu,
                Capability::CanGpuPeerTransfer,
            ]),
            cost: CostClass::SmallFinite,
            trust: pool.trust,
            provenance: pool.provenance,
            validation: pool.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(
                &pool.history,
                format!("gpu_memory:distributed_region:{memory_mib}MiB"),
            ),
            location: LocationContext::local(),
        }
    }

    pub fn gpu_memory_resource_nat(theory: &str, source: &Passport, event: &str) -> Self {
        Self {
            ty: TypeKind::Nat,
            construction: ConstructionMode::Definable,
            capabilities: CapabilitySet::from([
                Capability::CanAddAsNat,
                Capability::CanPrintDecimal,
                Capability::CanSymbolicPrint,
                Capability::CanCompareDirect,
                Capability::CanComputeModular,
            ]),
            cost: CostClass::SmallFinite,
            trust: source.trust,
            provenance: source.provenance,
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, event),
            location: LocationContext::local(),
        }
    }

    pub fn gpu_value(theory: &str, source: &Passport, region: &Passport) -> Self {
        // Keep the inner value construction class.
        // GPU residency is represented by TypeKind::GpuValue and history, not by
        // degrading a literal/small Nat into Definable. Otherwise copy_from_gpu()
        // would lose can_print_decimal for exact small values.
        Self {
            ty: TypeKind::GpuValue {
                inner: Box::new(source.ty.clone()),
            },
            construction: source.construction,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanCopyGpuToCpu,
                Capability::CanGpuPeerTransfer,
            ]),
            cost: source.cost.max(region.cost).max(CostClass::SmallFinite),
            trust: source.trust.max(region.trust),
            provenance: source.provenance.max(region.provenance),
            validation: source.validation.max(region.validation),
            theory: TheoryContext::new(theory),
            history: HistoryChain::merge2(&source.history, &region.history, "copy:cpu_to_gpu"),
            location: LocationContext::local(),
        }
    }

    pub fn gpu_kernel(theory: &str, source: &Passport) -> Self {
        // Keep the inner value construction class.
        // Kernel-ness is represented by TypeKind::GpuKernel and history, not by
        // degrading a literal/small Nat into Definable. Otherwise launching the
        // kernel and copying the result back would lose exact-value capabilities.
        Self {
            ty: TypeKind::GpuKernel {
                inner: Box::new(source.ty.clone()),
            },
            construction: source.construction,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanLaunchGpuKernel,
                Capability::CanCrossArchPortable,
            ]),
            cost: source.cost.max(CostClass::SmallFinite),
            trust: source.trust,
            provenance: source.provenance,
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, "gpu_kernel:compile"),
            location: LocationContext::local(),
        }
    }

    pub fn launched_gpu_kernel(theory: &str, region: &Passport, kernel: &Passport) -> Self {
        let inner = match &kernel.ty {
            TypeKind::GpuKernel { inner } => (**inner).clone(),
            _ => TypeKind::Unknown("GpuKernelInner".to_string()),
        };
        Self {
            ty: TypeKind::GpuValue {
                inner: Box::new(inner),
            },
            construction: kernel.construction,
            capabilities: CapabilitySet::from([
                Capability::CanSymbolicPrint,
                Capability::CanCopyGpuToCpu,
                Capability::CanGpuPeerTransfer,
            ]),
            cost: kernel.cost.max(region.cost).max(CostClass::SmallFinite),
            trust: kernel.trust.max(region.trust),
            provenance: kernel.provenance.max(region.provenance),
            validation: kernel.validation.max(region.validation),
            theory: TheoryContext::new(theory),
            history: HistoryChain::merge2(&region.history, &kernel.history, "gpu_kernel:launch"),
            location: LocationContext::local(),
        }
    }

    pub fn copied_from_gpu(theory: &str, source: &Passport) -> Self {
        let inner = match &source.ty {
            TypeKind::GpuValue { inner } => (**inner).clone(),
            _ => TypeKind::Unknown("GpuValueInner".to_string()),
        };
        Self {
            ty: inner.clone(),
            construction: source.construction,
            capabilities: Self::capabilities_for_materialized_inner(
                &inner,
                source.construction,
                source.cost,
            ),
            cost: source.cost.max(CostClass::SmallFinite),
            trust: source.trust,
            provenance: source.provenance,
            validation: source.validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(&source.history, "copy:gpu_to_cpu"),
            location: LocationContext::local(),
        }
    }

    pub fn add_result(lhs: &Passport, rhs: &Passport, theory: &str) -> Self {
        let construction = lhs.construction.max(rhs.construction);
        let cost = lhs.cost.max(rhs.cost);
        let trust = lhs.trust.max(rhs.trust);
        let provenance = lhs.provenance.max(rhs.provenance);
        let validation = lhs.validation.max(rhs.validation);
        let capabilities = lhs.capabilities.intersection(&rhs.capabilities);
        Self {
            ty: TypeKind::Nat,
            construction,
            capabilities,
            cost,
            trust,
            provenance,
            validation,
            theory: TheoryContext::new(theory),
            history: HistoryChain::merge2(&lhs.history, &rhs.history, "derived:add"),
            location: LocationContext::local(),
        }
    }
}

impl fmt::Display for Passport {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}<construction={:?}, cost={:?}, trust={:?}, provenance={:?}, validation={:?}, theory={}, location={}, caps={}, history={}>",
            self.ty,
            self.construction,
            self.cost,
            self.trust,
            self.provenance,
            self.validation,
            self.theory.home,
            self.location,
            self.capabilities,
            self.history
        )
    }
}
