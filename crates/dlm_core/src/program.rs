//! v0.80 — multi-module programs.
//!
//! `import lib.nat` names a module by its `module` declaration. A [`Program`]
//! is the root module plus every module it imports, transitively, in
//! dependency order (imports before importers, root last). Import cycles,
//! missing modules and name mismatches are `E0917 ModuleImportError`.
//!
//! From files, `import a.b.c` in a file in directory `D` is looked up as
//! `D/a/b/c.dlm`, then `D/c.dlm`, then `R/a/b/c.dlm` where `R` is the directory
//! of the root file, then `P/a/b/c.dlm` for every search path `P` (the
//! `DLM_PATH` environment variable for [`load_program`], `--lib` in the CLI).
//! The file found must declare `module a.b.c`.

use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};

use crate::ast::{Module, ModuleItem};
use crate::diagnostics::{Diagnostic, DiagnosticKind, SourceFile};
use crate::ids::{FileId, IdAllocator};
use crate::parser::parse_module_with_source;

/// Maximum number of modules in one program.
pub const MAX_PROGRAM_MODULES: usize = 256;

/// A root module with its imported modules in dependency order.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Program {
    /// Dependency order; the root module is last.
    pub modules: Vec<Module>,
    /// v0.110: errors of the generic expansion (`crate::generics`), reported by the checker.
    pub generic_diagnostics: Vec<Diagnostic>,
}

impl Program {
    /// A program of one module without imports.
    pub fn single(module: Module) -> Self {
        let mut program = Program { modules: vec![module], generic_diagnostics: Vec::new() };
        program.generic_diagnostics = crate::generics::expand_program(&mut program);
        program
    }

    pub fn root(&self) -> &Module {
        self.modules.last().expect("a program has a root module")
    }

    /// Imported modules, in dependency order.
    pub fn dependencies(&self) -> &[Module] {
        &self.modules[..self.modules.len() - 1]
    }

    pub fn source_file(&self, id: FileId) -> Option<&SourceFile> {
        self.modules.iter().map(|m| &m.source).find(|s| s.id == id)
    }

    pub(crate) fn locate_diagnostic(&self, mut diagnostic: Diagnostic) -> Diagnostic {
        if diagnostic.source_path.is_none() {
            if let Some(source) = diagnostic.span.and_then(|s| self.source_file(s.file_id)) {
                diagnostic.source_path = source.path.clone().map(std::sync::Arc::new);
            }
        }
        diagnostic
    }
}

fn import_error(message: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::ModuleImportError, None, message)
}

/// Order `root` and the modules it needs from `library` (by module name).
pub fn link_program(root: Module, library: Vec<Module>) -> Result<Program, Vec<Diagnostic>> {
    let mut by_name: BTreeMap<String, Module> = BTreeMap::new();
    for module in library {
        if by_name.contains_key(&module.name) {
            return Err(vec![import_error(format!("module '{}' is provided twice", module.name))]);
        }
        by_name.insert(module.name.clone(), module);
    }
    link(root, &mut |name: &str, _importer: &str| {
        by_name.get(name).cloned().ok_or_else(|| vec![import_error(format!("module '{name}' is not available"))])
    })
}

/// Load `path` and every module it imports from files, with the search paths
/// of the `DLM_PATH` environment variable.
pub fn load_program(path: &Path) -> Result<Program, Vec<Diagnostic>> {
    let search: Vec<PathBuf> = std::env::var_os("DLM_PATH").map(|v| std::env::split_paths(&v).collect()).unwrap_or_default();
    load_program_with_paths(path, &search)
}

/// Load `path` and every module it imports from files, looking in
/// `search_paths` after the importer's and the root's directories.
pub fn load_program_with_paths(path: &Path, search_paths: &[PathBuf]) -> Result<Program, Vec<Diagnostic>> {
    let mut ids = IdAllocator::new();
    let mut read = |path: &Path| -> Result<Module, Vec<Diagnostic>> {
        let file = SourceFile::named(ids.alloc_file(), path);
        let source = std::fs::read_to_string(path)
            .map_err(|err| vec![import_error(format!("cannot read {}: {err}", path.display())).in_source(&file)])?;
        parse_module_with_source(&source, file)
    };
    let root = read(path)?;
    let root_dir = path.parent().map(Path::to_path_buf).unwrap_or_default();
    let mut directories: BTreeMap<String, PathBuf> = BTreeMap::new();
    directories.insert(root.name.clone(), root_dir.clone());
    link(root, &mut |name: &str, importer: &str| {
        let importer_dir = directories.get(importer).cloned().unwrap_or_else(|| root_dir.clone());
        let relative: PathBuf = name.split('.').collect::<PathBuf>().with_extension("dlm");
        let last = format!("{}.dlm", name.rsplit('.').next().unwrap_or(name));
        let mut candidates = vec![importer_dir.join(&relative), importer_dir.join(&last), root_dir.join(&relative)];
        candidates.extend(search_paths.iter().map(|p| p.join(&relative)));
        let mut seen = BTreeSet::new();
        candidates.retain(|c| seen.insert(c.clone()));
        let Some(found) = candidates.iter().find(|candidate| candidate.is_file()) else {
            return Err(vec![import_error(format!(
                "module '{name}' imported by '{importer}' not found; looked for {}",
                candidates.iter().map(|c| c.display().to_string()).collect::<Vec<_>>().join(", ")
            ))]);
        };
        let module = read(found)?;
        if module.name != name {
            return Err(vec![import_error(format!(
                "{} declares module '{}', but it was imported as '{name}'",
                found.display(),
                module.name
            ))]);
        }
        directories.insert(name.to_string(), found.parent().map(Path::to_path_buf).unwrap_or_default());
        Ok(module)
    })
}

/// Fetch a module by name for an importer.
type Fetch<'a> = dyn FnMut(&str, &str) -> Result<Module, Vec<Diagnostic>> + 'a;

/// Depth-first linking with cycle detection.
fn link(
    root: Module,
    fetch: &mut Fetch<'_>,
) -> Result<Program, Vec<Diagnostic>> {
    let mut ordered: Vec<Module> = Vec::new();
    let mut done: BTreeSet<String> = BTreeSet::new();
    let mut stack: Vec<String> = Vec::new();
    visit(root, fetch, &mut ordered, &mut done, &mut stack)?;
    // Theory names are global within a program.
    let mut owners: BTreeMap<String, String> = BTreeMap::new();
    for module in &ordered {
        for item in &module.items {
            if let ModuleItem::Theory(theory) = item {
                if let Some(owner) = owners.insert(theory.name.clone(), module.name.clone()) {
                    if owner != module.name {
                        return Err(vec![import_error(format!(
                            "theory '{}' is declared in modules '{owner}' and '{}'",
                            theory.name, module.name
                        ))]);
                    }
                }
            }
        }
    }
    // The root keeps ROOT for compatibility with single-module programs.
    // Rebind in-memory modules too; independent parsers all start at ROOT.
    let dependencies = ordered.len() - 1;
    for (index, module) in ordered.iter_mut().enumerate() {
        let id = if index == dependencies { FileId::ROOT } else { FileId::new(index as u32 + 1) };
        module.set_file_id(id);
    }
    let mut program = Program { modules: ordered, generic_diagnostics: Vec::new() };
    program.generic_diagnostics = crate::generics::expand_program(&mut program);
    Ok(program)
}

fn visit(
    module: Module,
    fetch: &mut Fetch<'_>,
    ordered: &mut Vec<Module>,
    done: &mut BTreeSet<String>,
    stack: &mut Vec<String>,
) -> Result<(), Vec<Diagnostic>> {
    stack.push(module.name.clone());
    for import in &module.imports {
        let name = import.path.trim();
        if name == module.name || stack.iter().any(|m| m == name) {
            let mut cycle = stack.clone();
            cycle.push(name.to_string());
            return Err(vec![import_error(format!("import cycle: {}", cycle.join(" -> ")))]);
        }
        if done.contains(name) {
            continue;
        }
        if ordered.len() + stack.len() >= MAX_PROGRAM_MODULES {
            return Err(vec![import_error(format!("a program may contain at most {MAX_PROGRAM_MODULES} modules"))]);
        }
        let imported = fetch(name, &module.name)?;
        visit(imported, fetch, ordered, done, stack)?;
    }
    stack.pop();
    done.insert(module.name.clone());
    ordered.push(module);
    Ok(())
}

/// v0.83: the entry point of a module: the `fn main()` (no parameters)
/// declared by one of its theories, with that theory's name.
pub fn main_function(module: &Module) -> Option<(String, crate::ast::FnDecl)> {
    module.items.iter().find_map(|item| match item {
        ModuleItem::Theory(theory) => theory.items.iter().find_map(|item| match item {
            crate::ast::TheoryItem::Fn(decl) if decl.name == "main" && decl.params.is_empty() => {
                Some((theory.name.clone(), decl.clone()))
            }
            _ => None,
        }),
        _ => None,
    })
}

#[cfg(test)]
mod tests {
    use crate::parser::parse_module;
    use super::*;

    fn module(source: &str) -> Module {
        parse_module(source).expect("parse")
    }

    #[test]
    fn modules_are_ordered_by_dependency() {
        let root = module("module app\nimport lib.b\nimport lib.a\ntheory App {\n}\n");
        let a = module("module lib.a\ntheory A {\n}\n");
        let b = module("module lib.b\nimport lib.a\ntheory B {\n}\n");
        let program = link_program(root, vec![b, a]).unwrap();
        let names: Vec<&str> = program.modules.iter().map(|m| m.name.as_str()).collect();
        assert_eq!(names, ["lib.a", "lib.b", "app"]);
        assert_eq!(program.root().name, "app");
        assert_eq!(program.dependencies().len(), 2);
    }

    #[test]
    fn cycles_missing_modules_and_theory_clashes_are_errors() {
        let root = module("module app\nimport x\ntheory App {\n}\n");
        let x = module("module x\nimport y\ntheory X {\n}\n");
        let y = module("module y\nimport x\ntheory Y {\n}\n");
        let err = link_program(root.clone(), vec![x.clone(), y]).unwrap_err();
        assert!(err[0].message.contains("import cycle: app -> x -> y -> x"), "{err:?}");
        let err = link_program(root.clone(), vec![]).unwrap_err();
        assert!(err[0].message.contains("module 'x' is not available"));
        let clash = module("module x\ntheory App {\n}\n");
        let err = link_program(root, vec![clash]).unwrap_err();
        assert!(err[0].message.contains("theory 'App' is declared in modules 'x' and 'app'"), "{err:?}");
        let selfish = module("module s\nimport s\ntheory S {\n}\n");
        assert!(link_program(selfish, vec![]).unwrap_err()[0].message.contains("import cycle: s -> s"));
    }
}
