use std::collections::BTreeSet;
use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::module_system::{require_public_export, ExportDecl, ExportVisibility, ImportGraph};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum ModuleImportAuditStatus {
    Verified,
    Rejected,
}

impl fmt::Display for ModuleImportAuditStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ModuleImportAuditStatus::Verified => write!(f, "verified"),
            ModuleImportAuditStatus::Rejected => write!(f, "rejected"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InterfaceSymbol {
    pub symbol: String,
    pub visibility: ExportVisibility,
    pub ty: String,
    pub trust: TrustLevel,
    pub capabilities: Vec<String>,
    pub source_fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ModuleInterface {
    pub module: String,
    pub symbols: Vec<InterfaceSymbol>,
    pub fingerprint: String,
}

#[derive(Debug, Clone)]
pub struct ModuleImportAuditReport {
    pub importer: String,
    pub provider: String,
    pub requested_symbols: Vec<String>,
    pub resolved_symbols: Vec<InterfaceSymbol>,
    pub diagnostics: Vec<Diagnostic>,
    pub status: ModuleImportAuditStatus,
    pub interface_fingerprint: String,
    pub audit_fingerprint: String,
}

pub fn module_interface(
    module: impl Into<String>,
    exports: Vec<(ExportDecl, Passport)>,
    line: usize,
) -> Result<ModuleInterface, Diagnostic> {
    let module = require_non_empty(module.into(), "module", line)?;
    let mut seen = BTreeSet::new();
    let mut symbols = Vec::new();

    for (export, source) in exports {
        let symbol = require_non_empty(export.symbol.clone(), "export symbol", line)?;
        if !seen.insert(symbol.clone()) {
            return Err(interface_error(
                line,
                format!("duplicate interface symbol `{symbol}` for module `{module}`"),
                "a module interface must contain one canonical entry per exported symbol",
            ));
        }
        symbols.push(interface_symbol(export, source));
    }

    symbols.sort_by(|lhs, rhs| lhs.symbol.cmp(&rhs.symbol));
    let fingerprint = compute_module_interface_fingerprint(&module, &symbols);
    Ok(ModuleInterface { module, symbols, fingerprint })
}

pub fn public_interface_symbols(interface: &ModuleInterface) -> Vec<String> {
    interface
        .symbols
        .iter()
        .filter(|symbol| symbol.visibility == ExportVisibility::Public)
        .map(|symbol| symbol.symbol.clone())
        .collect()
}

pub fn require_interface_symbol(
    interface: &ModuleInterface,
    symbol: &str,
    line: usize,
) -> Result<InterfaceSymbol, Diagnostic> {
    interface
        .symbols
        .iter()
        .find(|item| item.symbol == symbol)
        .cloned()
        .ok_or_else(|| {
            interface_error(
                line,
                format!("module interface `{}` does not contain symbol `{symbol}`", interface.module),
                "interfaces are frozen public/private contracts; stale or incomplete interfaces must not satisfy imports",
            )
        })
}

pub fn audit_module_import(
    graph: &ImportGraph,
    importer: &str,
    provider: &str,
    required_symbols: Vec<String>,
    interface: &ModuleInterface,
    line: usize,
) -> ModuleImportAuditReport {
    let mut diagnostics = Vec::new();
    let mut resolved_symbols = Vec::new();

    if interface.module != provider {
        diagnostics.push(interface_error(
            line,
            format!(
                "interface module `{}` does not match provider `{provider}`",
                interface.module
            ),
            "an import audit must bind the exact provider interface, not a neighboring module contract",
        ));
    }

    if !graph.modules.contains_key(importer) {
        diagnostics.push(interface_error(
            line,
            format!("importer module `{importer}` is missing from the import graph"),
            "import audits require a resolved import graph before interface checks",
        ));
    }

    if !graph.modules.contains_key(provider) {
        diagnostics.push(interface_error(
            line,
            format!("provider module `{provider}` is missing from the import graph"),
            "import audits require a resolved provider manifest before interface checks",
        ));
    }

    if graph.modules.contains_key(importer)
        && graph.modules.contains_key(provider)
        && !graph.edges.iter().any(|edge| edge.from == importer && edge.to == provider)
    {
        diagnostics.push(interface_error(
            line,
            format!("module `{importer}` does not import provider `{provider}`"),
            "public interface symbols cannot be used without an explicit import edge",
        ));
    }

    for symbol in &required_symbols {
        if symbol.trim().is_empty() {
            diagnostics.push(interface_error(
                line,
                "required import symbol must not be empty",
                "import requirements are semantic names, not display placeholders",
            ));
            continue;
        }

        if let Err(err) = require_public_export(graph, provider, symbol, line) {
            diagnostics.push(err);
            continue;
        }

        match require_interface_symbol(interface, symbol, line) {
            Ok(resolved) if resolved.visibility == ExportVisibility::Public => {
                resolved_symbols.push(resolved);
            }
            Ok(resolved) => diagnostics.push(interface_error(
                line,
                format!(
                    "interface symbol `{}` from module `{provider}` is {}, not public",
                    resolved.symbol, resolved.visibility
                ),
                "private interface entries are audit data only and must not cross module boundaries",
            )),
            Err(err) => diagnostics.push(err),
        }
    }

    let status = if diagnostics.is_empty() {
        ModuleImportAuditStatus::Verified
    } else {
        ModuleImportAuditStatus::Rejected
    };
    let audit_fingerprint = compute_module_import_audit_fingerprint(
        importer,
        provider,
        status,
        &required_symbols,
        &resolved_symbols,
        &diagnostics,
        &interface.fingerprint,
    );

    ModuleImportAuditReport {
        importer: importer.to_string(),
        provider: provider.to_string(),
        requested_symbols: required_symbols,
        resolved_symbols,
        diagnostics,
        status,
        interface_fingerprint: interface.fingerprint.clone(),
        audit_fingerprint,
    }
}

pub fn require_verified_module_import_audit(
    report: &ModuleImportAuditReport,
    line: usize,
) -> Result<(), Diagnostic> {
    if report.status == ModuleImportAuditStatus::Verified {
        Ok(())
    } else {
        Err(interface_error(
            line,
            format!(
                "module import audit for `{}` -> `{}` is rejected",
                report.importer, report.provider
            ),
            "only verified import audits may be used as public interface evidence",
        ))
    }
}

pub fn module_interface_passport(theory: &str, interface: &ModuleInterface) -> Passport {
    Passport {
        ty: TypeKind::ModuleInterface {
            module: interface.module.clone(),
        },
        construction: ConstructionMode::Definable,
        capabilities: interface_capabilities(),
        cost: CostClass::SmallFinite,
        trust: interface
            .symbols
            .iter()
            .map(|symbol| symbol.trust)
            .max()
            .unwrap_or(TrustLevel::Builtin),
        provenance: Provenance::BuiltinKnown,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_event(format!(
            "module:interface:{}:symbols={}:fingerprint={}",
            interface.module,
            interface.symbols.len(),
            interface.fingerprint
        )),
        location: LocationContext::local(),
    }
}

pub fn module_import_audit_passport(theory: &str, report: &ModuleImportAuditReport) -> Passport {
    Passport {
        ty: TypeKind::ModuleImportAudit {
            importer: report.importer.clone(),
            provider: report.provider.clone(),
            status: report.status.to_string(),
        },
        construction: ConstructionMode::Definable,
        capabilities: interface_capabilities(),
        cost: CostClass::SmallFinite,
        trust: TrustLevel::Builtin,
        provenance: Provenance::BuiltinKnown,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_event(format!(
            "module:import_audit:{}->{}:{}:fingerprint={}",
            report.importer, report.provider, report.status, report.audit_fingerprint
        )),
        location: LocationContext::local(),
    }
}

pub fn export_module_interface_text(interface: &ModuleInterface) -> String {
    let mut out = String::new();
    out.push_str("DLM module interface\n");
    out.push_str(&format!("module: {}\n", interface.module));
    out.push_str(&format!("fingerprint: {}\n", interface.fingerprint));
    out.push_str("symbols:\n");
    for symbol in &interface.symbols {
        out.push_str(&format!(
            "  - {} [{}] type={} trust={:?} caps={{{}}} source={}\n",
            symbol.symbol,
            symbol.visibility,
            symbol.ty,
            symbol.trust,
            symbol.capabilities.join(", "),
            symbol.source_fingerprint
        ));
    }
    out
}

pub fn render_module_import_audit_report(report: &ModuleImportAuditReport) -> String {
    let mut out = String::new();
    out.push_str("DLM module import audit\n");
    out.push_str(&format!("importer: {}\n", report.importer));
    out.push_str(&format!("provider: {}\n", report.provider));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("interface_fingerprint: {}\n", report.interface_fingerprint));
    out.push_str(&format!("audit_fingerprint: {}\n", report.audit_fingerprint));
    out.push_str(&format!("requested: {}\n", report.requested_symbols.join(", ")));
    out.push_str("resolved:\n");
    for symbol in &report.resolved_symbols {
        out.push_str(&format!("  - {} [{}] {}\n", symbol.symbol, symbol.visibility, symbol.ty));
    }
    if !report.diagnostics.is_empty() {
        out.push_str("diagnostics:\n");
        for diagnostic in &report.diagnostics {
            out.push_str(&format!("  - {}\n", diagnostic.message));
        }
    }
    out
}

fn interface_symbol(export: ExportDecl, source: Passport) -> InterfaceSymbol {
    let capabilities: Vec<String> = source
        .capabilities
        .names()
        .into_iter()
        .map(str::to_string)
        .collect();
    let source_fingerprint = stable_fingerprint(&[
        "interface_symbol".to_string(),
        export.symbol.clone(),
        export.visibility.to_string(),
        source.ty.to_string(),
        format!("trust={:?}", source.trust),
        format!("provenance={:?}", source.provenance),
        format!("validation={:?}", source.validation),
        format!("caps={}", capabilities.join(",")),
    ]);

    InterfaceSymbol {
        symbol: export.symbol,
        visibility: export.visibility,
        ty: source.ty.to_string(),
        trust: source.trust,
        capabilities,
        source_fingerprint,
    }
}

fn compute_module_interface_fingerprint(module: &str, symbols: &[InterfaceSymbol]) -> String {
    let mut parts = vec!["module_interface".to_string(), module.to_string()];
    for symbol in symbols {
        parts.push(symbol.symbol.clone());
        parts.push(symbol.visibility.to_string());
        parts.push(symbol.ty.clone());
        parts.push(format!("{:?}", symbol.trust));
        parts.push(symbol.capabilities.join(","));
        parts.push(symbol.source_fingerprint.clone());
    }
    stable_fingerprint(&parts)
}

fn compute_module_import_audit_fingerprint(
    importer: &str,
    provider: &str,
    status: ModuleImportAuditStatus,
    requested_symbols: &[String],
    resolved_symbols: &[InterfaceSymbol],
    diagnostics: &[Diagnostic],
    interface_fingerprint: &str,
) -> String {
    let mut parts = vec![
        "module_import_audit".to_string(),
        importer.to_string(),
        provider.to_string(),
        status.to_string(),
        interface_fingerprint.to_string(),
    ];
    parts.extend(requested_symbols.iter().cloned());
    for symbol in resolved_symbols {
        parts.push(symbol.symbol.clone());
        parts.push(symbol.source_fingerprint.clone());
    }
    for diagnostic in diagnostics {
        parts.push(diagnostic.kind_string());
        parts.push(diagnostic.message.clone());
    }
    stable_fingerprint(&parts)
}

fn stable_fingerprint(parts: &[String]) -> String {
    let mut hash: u64 = 0xcbf29ce484222325;
    for part in parts {
        for byte in part.as_bytes() {
            hash ^= u64::from(*byte);
            hash = hash.wrapping_mul(0x100000001b3);
        }
        hash ^= 0xff;
        hash = hash.wrapping_mul(0x100000001b3);
    }
    format!("{hash:016x}")
}

trait DiagnosticKindString {
    fn kind_string(&self) -> String;
}

impl DiagnosticKindString for Diagnostic {
    fn kind_string(&self) -> String {
        format!("{:?}", self.kind)
    }
}

fn interface_capabilities() -> CapabilitySet {
    CapabilitySet::from([
        Capability::CanSymbolicPrint,
        Capability::CanInspectAst,
        Capability::CanMetaLevelReason,
    ])
}

fn require_non_empty(value: String, label: &str, line: usize) -> Result<String, Diagnostic> {
    if value.trim().is_empty() {
        Err(interface_error(
            line,
            format!("{label} must not be empty"),
            "module interface names are semantic identifiers, not display-only labels",
        ))
    } else {
        Ok(value)
    }
}

fn interface_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::ModuleInterfaceError, Some(line), message).with_help(help)
}
