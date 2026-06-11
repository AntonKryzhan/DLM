use std::collections::{BTreeMap, BTreeSet};
use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum ExportVisibility {
    Public,
    Private,
}

impl fmt::Display for ExportVisibility {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ExportVisibility::Public => write!(f, "public"),
            ExportVisibility::Private => write!(f, "private"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ImportDecl {
    pub path: String,
    pub alias: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ExportDecl {
    pub symbol: String,
    pub visibility: ExportVisibility,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ModuleManifest {
    pub name: String,
    pub imports: Vec<ImportDecl>,
    pub exports: Vec<ExportDecl>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ImportEdge {
    pub from: String,
    pub to: String,
    pub alias: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ImportGraph {
    pub root: String,
    pub modules: BTreeMap<String, ModuleManifest>,
    pub edges: Vec<ImportEdge>,
}

pub fn import_decl(path: impl Into<String>, alias: Option<impl Into<String>>) -> ImportDecl {
    ImportDecl {
        path: path.into(),
        alias: alias.map(Into::into),
    }
}

pub fn public_export(symbol: impl Into<String>) -> ExportDecl {
    ExportDecl {
        symbol: symbol.into(),
        visibility: ExportVisibility::Public,
    }
}

pub fn private_export(symbol: impl Into<String>) -> ExportDecl {
    ExportDecl {
        symbol: symbol.into(),
        visibility: ExportVisibility::Private,
    }
}

pub fn module_manifest(
    name: impl Into<String>,
    imports: Vec<ImportDecl>,
    exports: Vec<ExportDecl>,
    line: usize,
) -> Result<ModuleManifest, Diagnostic> {
    let name = require_non_empty(name.into(), "module name", line)?;
    let manifest = ModuleManifest {
        name,
        imports,
        exports,
    };
    validate_module_manifest(&manifest, line)?;
    Ok(manifest)
}

pub fn validate_module_manifest(manifest: &ModuleManifest, line: usize) -> Result<(), Diagnostic> {
    require_non_empty(manifest.name.clone(), "module name", line)?;

    let mut import_paths = BTreeSet::new();
    let mut aliases = BTreeSet::new();
    for import in &manifest.imports {
        let path = require_non_empty(import.path.clone(), "import path", line)?;
        if !import_paths.insert(path.clone()) {
            return Err(module_error(
                line,
                format!("duplicate import `{path}` in module `{}`", manifest.name),
                "imports are semantic dependencies and must be unique per module",
            ));
        }
        if let Some(alias) = &import.alias {
            let alias = require_non_empty(alias.clone(), "import alias", line)?;
            if !aliases.insert(alias.clone()) {
                return Err(module_error(
                    line,
                    format!("duplicate import alias `{alias}` in module `{}`", manifest.name),
                    "aliases must not shadow each other inside one module manifest",
                ));
            }
        }
    }

    let mut export_symbols = BTreeSet::new();
    for export in &manifest.exports {
        let symbol = require_non_empty(export.symbol.clone(), "export symbol", line)?;
        if !export_symbols.insert(symbol.clone()) {
            return Err(module_error(
                line,
                format!("duplicate export `{symbol}` in module `{}`", manifest.name),
                "a symbol cannot be both public and private in the same manifest",
            ));
        }
    }

    Ok(())
}

pub fn build_import_graph(
    root: impl Into<String>,
    manifests: Vec<ModuleManifest>,
    line: usize,
) -> Result<ImportGraph, Vec<Diagnostic>> {
    let root = match require_non_empty(root.into(), "root module", line) {
        Ok(root) => root,
        Err(err) => return Err(vec![err]),
    };

    let mut diagnostics = Vec::new();
    let mut modules = BTreeMap::new();

    for manifest in manifests {
        if let Err(err) = validate_module_manifest(&manifest, line) {
            diagnostics.push(err);
            continue;
        }
        let name = manifest.name.clone();
        if modules.insert(name.clone(), manifest).is_some() {
            diagnostics.push(module_error(
                line,
                format!("duplicate module manifest `{name}`"),
                "each module must have one manifest in the import graph",
            ));
        }
    }

    if !modules.contains_key(&root) {
        diagnostics.push(module_error(
            line,
            format!("root module `{root}` is missing from the import graph"),
            "the root module must be one of the supplied manifests",
        ));
    }

    let mut edges = Vec::new();
    for (from, manifest) in &modules {
        for import in &manifest.imports {
            if modules.contains_key(&import.path) {
                edges.push(ImportEdge {
                    from: from.clone(),
                    to: import.path.clone(),
                    alias: import.alias.clone(),
                });
            } else {
                diagnostics.push(module_error(
                    line,
                    format!("module `{from}` imports missing module `{}`", import.path),
                    "imports must resolve to known module manifests before public/private export checks",
                ));
            }
        }
    }

    if diagnostics.is_empty() {
        if let Some(cycle) = find_import_cycle(&modules) {
            diagnostics.push(module_error(
                line,
                format!("cyclic import graph detected: {}", cycle.join(" -> ")),
                "import graphs must be acyclic so trust and visibility flow in one direction",
            ));
        }
    }

    if diagnostics.is_empty() {
        Ok(ImportGraph { root, modules, edges })
    } else {
        Err(diagnostics)
    }
}

pub fn require_public_export(
    graph: &ImportGraph,
    module: &str,
    symbol: &str,
    line: usize,
) -> Result<ExportDecl, Diagnostic> {
    let manifest = graph.modules.get(module).ok_or_else(|| {
        module_error(
            line,
            format!("unknown module `{module}` in import graph"),
            "public export lookup must target a resolved module",
        )
    })?;

    let export = manifest.exports.iter().find(|export| export.symbol == symbol).ok_or_else(|| {
        module_error(
            line,
            format!("module `{module}` does not export symbol `{symbol}`"),
            "only declared public exports may be imported by another module",
        )
    })?;

    match export.visibility {
        ExportVisibility::Public => Ok(export.clone()),
        ExportVisibility::Private => Err(module_error(
            line,
            format!("module `{module}` has private symbol `{symbol}`, not a public export"),
            "private declarations must not leak across module boundaries",
        )),
    }
}

pub fn imported_public_symbols(
    graph: &ImportGraph,
    module: &str,
    line: usize,
) -> Result<Vec<(String, String)>, Diagnostic> {
    let manifest = graph.modules.get(module).ok_or_else(|| {
        module_error(
            line,
            format!("unknown module `{module}` in import graph"),
            "cannot list imports for an unresolved module",
        )
    })?;

    let mut symbols = Vec::new();
    for import in &manifest.imports {
        let target = graph.modules.get(&import.path).ok_or_else(|| {
            module_error(
                line,
                format!("module `{module}` imports missing module `{}`", import.path),
                "the import graph must be resolved before imported symbols are listed",
            )
        })?;
        for export in &target.exports {
            if export.visibility == ExportVisibility::Public {
                symbols.push((target.name.clone(), export.symbol.clone()));
            }
        }
    }
    Ok(symbols)
}

pub fn module_manifest_passport(theory: &str, manifest: &ModuleManifest) -> Passport {
    Passport {
        ty: TypeKind::ModuleManifest {
            module: manifest.name.clone(),
        },
        construction: ConstructionMode::Definable,
        capabilities: module_capabilities(),
        cost: CostClass::SmallFinite,
        trust: TrustLevel::Builtin,
        provenance: Provenance::BuiltinKnown,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_event(format!(
            "module:manifest:{}:imports={}:exports={}",
            manifest.name,
            manifest.imports.len(),
            manifest.exports.len()
        )),
        location: LocationContext::local(),
    }
}

pub fn import_graph_passport(theory: &str, graph: &ImportGraph) -> Passport {
    let module_histories: Vec<HistoryChain> = graph
        .modules
        .values()
        .map(|manifest| module_manifest_passport(theory, manifest).history)
        .collect();
    let sources: Vec<&HistoryChain> = module_histories.iter().collect();

    Passport {
        ty: TypeKind::ImportGraph {
            root: graph.root.clone(),
        },
        construction: ConstructionMode::Definable,
        capabilities: module_capabilities(),
        cost: CostClass::SmallFinite,
        trust: TrustLevel::Builtin,
        provenance: Provenance::BuiltinKnown,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: HistoryChain::merge_many(
            sources,
            format!("module:import_graph:{}:edges={}", graph.root, graph.edges.len()),
        ),
        location: LocationContext::local(),
    }
}

pub fn module_export_passport(
    theory: &str,
    module: &str,
    export: &ExportDecl,
    source: &Passport,
) -> Passport {
    let visibility = export.visibility.to_string();
    Passport {
        ty: TypeKind::ModuleExport {
            module: module.to_string(),
            symbol: export.symbol.clone(),
            visibility,
        },
        construction: source.construction,
        capabilities: source.capabilities.clone(),
        cost: source.cost,
        trust: source.trust,
        provenance: source.provenance,
        validation: source.validation,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_source(
            &source.history,
            format!("module:export:{module}:{}:{}", export.symbol, export.visibility),
        ),
        location: LocationContext::local(),
    }
}

fn find_import_cycle(modules: &BTreeMap<String, ModuleManifest>) -> Option<Vec<String>> {
    let mut visited = BTreeSet::new();
    let mut visiting = BTreeSet::new();
    let mut stack = Vec::new();

    for name in modules.keys() {
        if !visited.contains(name) {
            if let Some(cycle) = visit_import(name, modules, &mut visited, &mut visiting, &mut stack) {
                return Some(cycle);
            }
        }
    }
    None
}

fn visit_import(
    name: &str,
    modules: &BTreeMap<String, ModuleManifest>,
    visited: &mut BTreeSet<String>,
    visiting: &mut BTreeSet<String>,
    stack: &mut Vec<String>,
) -> Option<Vec<String>> {
    if visiting.contains(name) {
        let start = stack.iter().position(|item| item == name).unwrap_or(0);
        let mut cycle = stack[start..].to_vec();
        cycle.push(name.to_string());
        return Some(cycle);
    }
    if visited.contains(name) {
        return None;
    }

    visiting.insert(name.to_string());
    stack.push(name.to_string());

    if let Some(module) = modules.get(name) {
        for import in &module.imports {
            if modules.contains_key(&import.path) {
                if let Some(cycle) = visit_import(&import.path, modules, visited, visiting, stack) {
                    return Some(cycle);
                }
            }
        }
    }

    stack.pop();
    visiting.remove(name);
    visited.insert(name.to_string());
    None
}

fn module_capabilities() -> CapabilitySet {
    CapabilitySet::from([
        Capability::CanSymbolicPrint,
        Capability::CanInspectAst,
        Capability::CanMetaLevelReason,
    ])
}

fn require_non_empty(value: String, label: &str, line: usize) -> Result<String, Diagnostic> {
    if value.trim().is_empty() {
        Err(module_error(
            line,
            format!("{label} must not be empty"),
            "module/import/export identifiers are semantic names, not display-only labels",
        ))
    } else {
        Ok(value)
    }
}

fn module_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::ModuleImportError, Some(line), message).with_help(help)
}
