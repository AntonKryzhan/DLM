//! v0.81 — project-level checking.
//!
//! A project is a directory with a `dlm.project` manifest:
//!
//! ```text
//! # comment
//! name = demo
//! lib = lib                      # comma-separated search paths for imports
//! roots = lib/std, examples/valid # files, or directories (their *.dlm files)
//! policy = research              # research | trusted_only
//! ```
//!
//! Every root is loaded as a program (with its imports) and checked; the
//! project report aggregates diagnostics and trusted-base audits.

use std::path::{Path, PathBuf};

use crate::audit::{audit_check_report, AuditStatus, ProgramAudit};
use crate::checker::{CheckReport, Checker};
use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::policy::CheckPolicy;
use crate::program::load_program_with_paths;

/// File name of the project manifest.
pub const PROJECT_MANIFEST: &str = "dlm.project";

#[derive(Debug, Clone)]
pub struct ProjectManifest {
    pub name: String,
    /// Directory of the manifest; relative paths are resolved against it.
    pub dir: PathBuf,
    pub lib: Vec<PathBuf>,
    /// Root files, in order.
    pub roots: Vec<PathBuf>,
    pub trusted_only: bool,
}

fn project_error(message: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::ModuleImportError, None, message)
}

fn list(value: &str) -> Vec<String> {
    value.split(',').map(str::trim).filter(|s| !s.is_empty()).map(str::to_string).collect()
}

/// Parse a manifest text; `dir` is the project directory.
pub fn parse_project_manifest(text: &str, dir: &Path) -> Result<ProjectManifest, Diagnostic> {
    let mut name = None;
    let mut lib = Vec::new();
    let mut root_entries = Vec::new();
    let mut trusted_only = false;
    for (index, raw) in text.lines().enumerate() {
        let line = raw.split('#').next().unwrap_or("").trim();
        if line.is_empty() {
            continue;
        }
        let Some((key, value)) = line.split_once('=') else {
            return Err(project_error(format!("{PROJECT_MANIFEST}:{}: expected `key = value`", index + 1)));
        };
        let value = value.trim();
        match key.trim() {
            "name" if !value.is_empty() => name = Some(value.to_string()),
            "lib" => lib.extend(list(value).into_iter().map(|p| dir.join(p))),
            "roots" => root_entries.extend(list(value)),
            "policy" => {
                trusted_only = match value {
                    "research" => false,
                    "trusted_only" => true,
                    other => {
                        return Err(project_error(format!(
                            "{PROJECT_MANIFEST}:{}: unknown policy '{other}'; expected research or trusted_only",
                            index + 1
                        )))
                    }
                }
            }
            other => {
                return Err(project_error(format!("{PROJECT_MANIFEST}:{}: unknown or empty key '{other}'", index + 1)));
            }
        }
    }
    let name = name.ok_or_else(|| project_error(format!("{PROJECT_MANIFEST}: missing `name`")))?;
    if root_entries.is_empty() {
        return Err(project_error(format!("{PROJECT_MANIFEST}: missing `roots`")));
    }
    let mut roots = Vec::new();
    for entry in root_entries {
        let path = dir.join(&entry);
        if path.is_dir() {
            let mut files: Vec<PathBuf> = std::fs::read_dir(&path)
                .map_err(|err| project_error(format!("cannot read root directory {}: {err}", path.display())))?
                .filter_map(|e| e.ok().map(|e| e.path()))
                .filter(|p| p.extension().and_then(|e| e.to_str()) == Some("dlm"))
                .collect();
            files.sort();
            if files.is_empty() {
                return Err(project_error(format!("root directory {} has no .dlm files", path.display())));
            }
            roots.extend(files);
        } else if path.is_file() {
            roots.push(path);
        } else {
            return Err(project_error(format!("root '{entry}' does not exist ({})", path.display())));
        }
    }
    Ok(ProjectManifest { name, dir: dir.to_path_buf(), lib, roots, trusted_only })
}

/// Read `dlm.project` from a directory, or a manifest file path.
pub fn load_project(path: &Path) -> Result<ProjectManifest, Diagnostic> {
    let manifest = if path.is_dir() { path.join(PROJECT_MANIFEST) } else { path.to_path_buf() };
    let text = std::fs::read_to_string(&manifest)
        .map_err(|err| project_error(format!("cannot read {}: {err}", manifest.display())))?;
    let dir = manifest.parent().map(Path::to_path_buf).unwrap_or_default();
    parse_project_manifest(&text, &dir)
}

/// Result for one root file.
#[derive(Debug, Clone)]
pub struct ProjectEntry {
    pub path: PathBuf,
    pub module: Option<String>,
    pub diagnostics: Vec<Diagnostic>,
    pub audit: Option<ProgramAudit>,
}

impl ProjectEntry {
    pub fn ok(&self) -> bool {
        self.diagnostics.is_empty()
    }
}

#[derive(Debug, Clone)]
pub struct ProjectReport {
    pub name: String,
    pub entries: Vec<ProjectEntry>,
}

impl ProjectReport {
    pub fn ok(&self) -> bool {
        self.entries.iter().all(ProjectEntry::ok)
    }

    pub fn failed(&self) -> usize {
        self.entries.iter().filter(|e| !e.ok()).count()
    }

    /// Worst audit status over all checked roots.
    pub fn status(&self) -> Option<AuditStatus> {
        self.entries.iter().filter_map(|e| e.audit.as_ref().map(|a| a.status)).max()
    }

    /// Text summary: one line per root and totals.
    pub fn render(&self) -> String {
        let mut out = format!("DLM project: {}\n\n", self.name);
        let (mut proved, mut axioms, mut open) = (0usize, 0usize, 0usize);
        for entry in &self.entries {
            match (&entry.audit, entry.ok()) {
                (Some(audit), true) => {
                    proved += audit.proved();
                    axioms += audit.statements.iter().filter(|s| s.kind == crate::audit::StatementAuditKind::Axiom).count();
                    open += audit.open_goals();
                    out.push_str(&format!(
                        "ok     {}  module={} status={} proved={} open={}\n",
                        entry.path.display(),
                        audit.module,
                        audit.status,
                        audit.proved(),
                        audit.open_goals()
                    ));
                }
                _ => out.push_str(&format!("FAILED {}  ({} diagnostics)\n", entry.path.display(), entry.diagnostics.len())),
            }
        }
        out.push_str(&format!(
            "\nroots: {}  failed: {}  proved: {}  axioms: {}  open goals: {}  status: {}\n",
            self.entries.len(),
            self.failed(),
            proved,
            axioms,
            open,
            self.status().map(|s| s.to_string()).unwrap_or_else(|| "-".to_string())
        ));
        out
    }
}

/// Check every root of `manifest`. `trusted_only` overrides the manifest
/// policy when `Some`.
pub fn check_project(manifest: &ProjectManifest, trusted_only: Option<bool>) -> ProjectReport {
    let policy = if trusted_only.unwrap_or(manifest.trusted_only) { CheckPolicy::trusted_only() } else { CheckPolicy::research() };
    let mut entries = Vec::with_capacity(manifest.roots.len());
    for root in &manifest.roots {
        let entry = match load_program_with_paths(root, &manifest.lib) {
            Ok(program) => {
                let report: CheckReport = Checker::with_policy(policy).check_program(&program);
                let imports = program.dependencies().iter().map(|m| m.name.clone()).collect();
                let audit = report.ok().then(|| audit_check_report(&report, imports));
                ProjectEntry { path: root.clone(), module: Some(report.module_name.clone()), diagnostics: report.diagnostics, audit }
            }
            Err(diagnostics) => ProjectEntry { path: root.clone(), module: None, diagnostics, audit: None },
        };
        entries.push(entry);
    }
    ProjectReport { name: manifest.name.clone(), entries }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn manifest_parsing() {
        let dir = std::env::temp_dir().join(format!("dlm_project_unit_{}", std::process::id()));
        std::fs::create_dir_all(dir.join("src")).unwrap();
        std::fs::write(dir.join("src/a.dlm"), "module a\ntheory A {\n}\n").unwrap();
        let manifest = parse_project_manifest("# demo\nname = demo\nlib = lib\nroots = src\npolicy = trusted_only\n", &dir).unwrap();
        assert_eq!(manifest.name, "demo");
        assert_eq!(manifest.roots, vec![dir.join("src/a.dlm")]);
        assert_eq!(manifest.lib, vec![dir.join("lib")]);
        assert!(manifest.trusted_only);
        for (text, needle) in [
            ("roots = src\n", "missing `name`"),
            ("name = x\n", "missing `roots`"),
            ("name = x\nroots = nope\n", "does not exist"),
            ("name = x\nroots = src\npolicy = lax\n", "unknown policy 'lax'"),
            ("name = x\nroots = src\ncolor = red\n", "unknown or empty key 'color'"),
            ("name x\n", "expected `key = value`"),
        ] {
            let err = parse_project_manifest(text, &dir).unwrap_err();
            assert!(err.message.contains(needle), "{text:?}: {}", err.message);
        }
        std::fs::remove_dir_all(&dir).ok();
    }
}
