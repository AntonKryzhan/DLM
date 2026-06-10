use std::collections::BTreeMap;

use crate::ast::{BridgeKind, Module, ModuleItem, TheoryItem};
use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::ids::{BridgeId, IdAllocator, ModuleId, TheoryId, ValueId};

#[derive(Debug, Clone)]
pub struct ResolvedModule {
    pub id: ModuleId,
    pub name: String,
    pub imports: Vec<ResolvedImport>,
    pub theories: Vec<ResolvedTheory>,
    pub bridges: Vec<ResolvedBridge>,
    pub symbols: SymbolTable,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ResolvedImport {
    pub path: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ResolvedTheory {
    pub id: TheoryId,
    pub name: String,
    pub values: Vec<ResolvedValue>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ResolvedValue {
    pub id: ValueId,
    pub theory: TheoryId,
    pub name: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ResolvedBridge {
    pub id: BridgeId,
    pub name: String,
    pub source: TheoryId,
    pub target: TheoryId,
    pub kind: BridgeKind,
}

#[derive(Debug, Clone, Default)]
pub struct SymbolTable {
    theories_by_name: BTreeMap<String, TheoryId>,
    values_by_theory: BTreeMap<TheoryId, BTreeMap<String, ValueId>>,
    bridges_by_name: BTreeMap<String, BridgeId>,
}

impl SymbolTable {
    pub fn theory_id(&self, name: &str) -> Option<TheoryId> {
        self.theories_by_name.get(name).copied()
    }

    pub fn value_id(&self, theory: TheoryId, name: &str) -> Option<ValueId> {
        self.values_by_theory
            .get(&theory)
            .and_then(|values| values.get(name).copied())
    }

    pub fn bridge_id(&self, name: &str) -> Option<BridgeId> {
        self.bridges_by_name.get(name).copied()
    }

    pub fn theory_count(&self) -> usize {
        self.theories_by_name.len()
    }

    pub fn bridge_count(&self) -> usize {
        self.bridges_by_name.len()
    }

    fn insert_theory(&mut self, name: String, id: TheoryId) -> Option<TheoryId> {
        self.theories_by_name.insert(name, id)
    }

    fn insert_value(&mut self, theory: TheoryId, name: String, id: ValueId) -> Option<ValueId> {
        self.values_by_theory
            .entry(theory)
            .or_default()
            .insert(name, id)
    }

    fn insert_bridge(&mut self, name: String, id: BridgeId) -> Option<BridgeId> {
        self.bridges_by_name.insert(name, id)
    }
}

#[derive(Debug, Clone, Default)]
pub struct Resolver {
    ids: IdAllocator,
}

impl Resolver {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn resolve(&mut self, module: &Module) -> Result<ResolvedModule, Vec<Diagnostic>> {
        let mut diagnostics = Vec::new();
        let mut symbols = SymbolTable::default();
        let mut theories = Vec::new();
        let mut bridges = Vec::new();

        for item in &module.items {
            if let ModuleItem::Theory(theory) = item {
                if symbols.theory_id(&theory.name).is_some() {
                    diagnostics.push(Diagnostic::error(
                        DiagnosticKind::NameError,
                        None,
                        format!("duplicate theory '{}'", theory.name),
                    ));
                    continue;
                }

                let id = self.ids.alloc_theory();
                symbols.insert_theory(theory.name.clone(), id);
                theories.push(ResolvedTheory {
                    id,
                    name: theory.name.clone(),
                    values: Vec::new(),
                });
            }
        }

        for item in &module.items {
            if let ModuleItem::Theory(theory) = item {
                let Some(theory_id) = symbols.theory_id(&theory.name) else {
                    continue;
                };
                let Some(resolved_theory) = theories.iter_mut().find(|item| item.id == theory_id) else {
                    continue;
                };

                for theory_item in &theory.items {
                    if let TheoryItem::Let(let_decl) = theory_item {
                        if symbols.value_id(theory_id, &let_decl.name).is_some() {
                            diagnostics.push(Diagnostic::error(
                                DiagnosticKind::NameError,
                                Some(let_decl.line),
                                format!(
                                    "duplicate value '{}' in theory '{}'",
                                    let_decl.name, theory.name
                                ),
                            ));
                            continue;
                        }

                        let value_id = self.ids.alloc_value();
                        symbols.insert_value(theory_id, let_decl.name.clone(), value_id);
                        resolved_theory.values.push(ResolvedValue {
                            id: value_id,
                            theory: theory_id,
                            name: let_decl.name.clone(),
                        });
                    }
                }
            }
        }

        for item in &module.items {
            if let ModuleItem::Bridge(bridge) = item {
                if symbols.bridge_id(&bridge.name).is_some() {
                    diagnostics.push(Diagnostic::error(
                        DiagnosticKind::NameError,
                        Some(bridge.line),
                        format!("duplicate bridge '{}'", bridge.name),
                    ));
                    continue;
                }

                let Some(source) = symbols.theory_id(&bridge.source) else {
                    diagnostics.push(Diagnostic::error(
                        DiagnosticKind::NameError,
                        Some(bridge.line),
                        format!(
                            "bridge '{}' references unknown source theory '{}'",
                            bridge.name, bridge.source
                        ),
                    ));
                    continue;
                };
                let Some(target) = symbols.theory_id(&bridge.target) else {
                    diagnostics.push(Diagnostic::error(
                        DiagnosticKind::NameError,
                        Some(bridge.line),
                        format!(
                            "bridge '{}' references unknown target theory '{}'",
                            bridge.name, bridge.target
                        ),
                    ));
                    continue;
                };

                let id = self.ids.alloc_bridge();
                symbols.insert_bridge(bridge.name.clone(), id);
                bridges.push(ResolvedBridge {
                    id,
                    name: bridge.name.clone(),
                    source,
                    target,
                    kind: bridge.kind.clone(),
                });
            }
        }

        if !diagnostics.is_empty() {
            return Err(diagnostics);
        }

        Ok(ResolvedModule {
            id: self.ids.alloc_module(),
            name: module.name.clone(),
            imports: module
                .imports
                .iter()
                .map(|import| ResolvedImport {
                    path: import.path.clone(),
                })
                .collect(),
            theories,
            bridges,
            symbols,
        })
    }
}

pub fn resolve_module(module: &Module) -> Result<ResolvedModule, Vec<Diagnostic>> {
    Resolver::new().resolve(module)
}
