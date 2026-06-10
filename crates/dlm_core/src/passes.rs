use crate::ast::Module;
use crate::diagnostics::Diagnostic;
use crate::resolve::{resolve_module, ResolvedModule};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum PassId {
    RawAstAccepted,
    NameResolution,
    LegacyChecker,
}

impl PassId {
    pub const fn as_str(self) -> &'static str {
        match self {
            PassId::RawAstAccepted => "raw_ast_accepted",
            PassId::NameResolution => "name_resolution",
            PassId::LegacyChecker => "legacy_checker",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PassStatus {
    Passed,
    Failed,
    Skipped,
}

#[derive(Debug, Clone)]
pub struct PassReport {
    pub id: PassId,
    pub status: PassStatus,
    pub diagnostics: Vec<Diagnostic>,
    pub note: Option<String>,
}

impl PassReport {
    pub fn passed(id: PassId, note: impl Into<String>) -> Self {
        Self {
            id,
            status: PassStatus::Passed,
            diagnostics: Vec::new(),
            note: Some(note.into()),
        }
    }

    pub fn failed(id: PassId, diagnostics: Vec<Diagnostic>, note: impl Into<String>) -> Self {
        Self {
            id,
            status: PassStatus::Failed,
            diagnostics,
            note: Some(note.into()),
        }
    }

    pub fn skipped(id: PassId, note: impl Into<String>) -> Self {
        Self {
            id,
            status: PassStatus::Skipped,
            diagnostics: Vec::new(),
            note: Some(note.into()),
        }
    }

    pub fn ok(&self) -> bool {
        self.status == PassStatus::Passed && self.diagnostics.is_empty()
    }
}

#[derive(Debug, Clone, Default)]
pub struct PassPipelineReport {
    pub passes: Vec<PassReport>,
}

impl PassPipelineReport {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn push(&mut self, pass: PassReport) {
        self.passes.push(pass);
    }

    pub fn ok(&self) -> bool {
        self.passes.iter().all(PassReport::ok)
    }

    pub fn len(&self) -> usize {
        self.passes.len()
    }

    pub fn is_empty(&self) -> bool {
        self.passes.is_empty()
    }

    pub fn find(&self, id: PassId) -> Option<&PassReport> {
        self.passes.iter().find(|pass| pass.id == id)
    }

    pub fn diagnostics(&self) -> impl Iterator<Item = &Diagnostic> {
        self.passes
            .iter()
            .flat_map(|pass| pass.diagnostics.iter())
    }
}

#[derive(Debug, Clone)]
pub struct FrontendPassOutput {
    pub resolved: ResolvedModule,
    pub report: PassPipelineReport,
}

pub fn run_frontend_passes(module: &Module) -> Result<FrontendPassOutput, PassPipelineReport> {
    let mut report = PassPipelineReport::new();
    report.push(PassReport::passed(
        PassId::RawAstAccepted,
        "parser already produced a RawAST module",
    ));

    match resolve_module(module) {
        Ok(resolved) => {
            report.push(PassReport::passed(
                PassId::NameResolution,
                format!(
                    "resolved {} theories, {} bridges",
                    resolved.theories.len(),
                    resolved.bridges.len()
                ),
            ));
            Ok(FrontendPassOutput { resolved, report })
        }
        Err(diagnostics) => {
            report.push(PassReport::failed(
                PassId::NameResolution,
                diagnostics,
                "name resolution rejected the module",
            ));
            Err(report)
        }
    }
}
