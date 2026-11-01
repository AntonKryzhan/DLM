use std::fmt;
use std::path::PathBuf;
use std::sync::Arc;

use crate::ids::FileId;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Severity {
    Error,
    Warning,
    Info,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum DiagnosticKind {
    ParseError,
    NameError,
    AccessError,
    TheoryBridgeError,
    NoAmbientTheoryError,
    UnsupportedFeature,
    RuntimeStaticMismatch,
    TrustTaintError,
    RuntimeError,
    InfinityModeError,
    EqualityModeError,
    MigrationBridgeError,
    DistributedResourceError,
    UniverseLevelError,
    DefinabilityError,
    BigNumberError,
    ProofKernelError,
    TruthBoundaryError,
    IncompletenessBoundaryError,
    ReflectionBoundaryError,
    MetaLevelError,
    StatementTheoremError,
    ProofObligationError,
    TacticScriptError,
    ProofCertificateError,
    ProofCertificateAuditError,
    EqualityRewriteError,
    RewriteNormalizationError,
    InductionError,
    ModuleImportError,
    ModuleInterfaceError,
    MetatheoryDependencyError,
    MetatheoryClosureError,
    ConservativeExtensionError,
    TheoremDependencyError,
    SoundnessBoundaryError,
    TrustedBaseError,
    MetatheoryFoundationError,
    LogicFormulaError,
    SubstitutionError,
    FunctionTermError,
    FunctionContractError,
    StructuralTypeError,
    StructuralEliminationError,
    PartialityTypeError,
    SequenceTypeError,
    TraversalError,
    RecursionBoundaryError,
    TerminationBudgetError,
    StandardPreludeError,
    PreludeEvaluationError,
    PreludeLoweringError,
    BackendCapabilityError,
    BackendLayoutError,
    DenseRuntimeError,
    /// v0.102: a `ByteBuf` owner is used after a move, moved twice, consumed on
    /// one branch only, captured, stored in a container or left as an rvalue.
    OwnershipError,
    /// v0.84: a checked program uses a construct the compiler does not support.
    CompileError,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SourceSpan {
    /// Compilation-local source identity; never a stable package identifier.
    pub file_id: FileId,
    pub start_line: usize,
    pub start_col: usize,
    pub end_line: usize,
    pub end_col: usize,
}

impl SourceSpan {
    pub fn line(line: usize) -> Self {
        Self {
            file_id: FileId::ROOT,
            start_line: line,
            start_col: 0,
            end_line: line,
            end_col: 0,
        }
    }

    pub fn line_col(line: usize, col: usize, len: usize) -> Self {
        let width = len.max(1);
        Self {
            file_id: FileId::ROOT,
            start_line: line,
            start_col: col.max(1),
            end_line: line,
            end_col: col.max(1) + width,
        }
    }

    pub fn new(start_line: usize, start_col: usize, end_line: usize, end_col: usize) -> Self {
        Self {
            file_id: FileId::ROOT,
            start_line,
            start_col,
            end_line,
            end_col,
        }
    }

    pub fn with_file(mut self, file_id: FileId) -> Self {
        self.file_id = file_id;
        self
    }

    pub fn line_only(&self) -> bool {
        self.start_col == 0 && self.end_col == 0
    }

    pub fn single_line(&self) -> bool {
        self.start_line == self.end_line
    }

    pub fn primary_line(&self) -> usize {
        self.start_line
    }
}

/// A source file in one compilation. Anonymous, in-memory modules have no path.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SourceFile {
    pub id: FileId,
    pub path: Option<PathBuf>,
}

impl Default for SourceFile {
    fn default() -> Self {
        Self { id: FileId::ROOT, path: None }
    }
}

impl SourceFile {
    pub fn named(id: FileId, path: impl Into<PathBuf>) -> Self {
        Self { id, path: Some(path.into()) }
    }

    pub fn line_span(&self, line: usize) -> SourceSpan {
        SourceSpan::line(line).with_file(self.id)
    }
}

impl fmt::Display for SourceSpan {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.line_only() {
            write!(f, "line {}", self.start_line)
        } else if self.single_line() {
            write!(f, "line {}, column {}", self.start_line, self.start_col)
        } else {
            write!(
                f,
                "line {}, column {} to line {}, column {}",
                self.start_line, self.start_col, self.end_line, self.end_col
            )
        }
    }
}

#[derive(Debug, Clone)]
pub struct Diagnostic {
    pub severity: Severity,
    pub kind: DiagnosticKind,
    pub line: Option<usize>,
    pub span: Option<SourceSpan>,
    pub source_path: Option<Arc<PathBuf>>,
    pub message: String,
    pub help: Option<String>,
}

impl Diagnostic {
    pub fn error(kind: DiagnosticKind, line: Option<usize>, message: impl Into<String>) -> Self {
        Self {
            severity: Severity::Error,
            kind,
            line,
            span: line.map(SourceSpan::line),
            source_path: None,
            message: message.into(),
            help: None,
        }
    }

    pub fn error_at(kind: DiagnosticKind, span: SourceSpan, message: impl Into<String>) -> Self {
        Self {
            severity: Severity::Error,
            kind,
            line: Some(span.primary_line()),
            span: Some(span),
            source_path: None,
            message: message.into(),
            help: None,
        }
    }

    pub fn with_span(mut self, span: SourceSpan) -> Self {
        self.line = Some(span.primary_line());
        self.span = Some(span);
        self
    }

    /// Keep an inner error's precise location when it propagates through
    /// enclosing expressions. Legacy, line-only errors acquire this span.
    pub(crate) fn at_expr(self, span: SourceSpan) -> Self {
        if self.span.is_none_or(|s| s.line_only()) {
            self.with_span(span)
        } else {
            self
        }
    }

    pub(crate) fn in_source(mut self, source: &SourceFile) -> Self {
        if let Some(span) = &mut self.span {
            if span.line_only() {
                span.file_id = source.id;
            }
        }
        if self.source_path.is_none() && self.span.is_none_or(|s| s.file_id == source.id) {
            self.source_path = source.path.clone().map(Arc::new);
        }
        self
    }

    pub fn with_help(mut self, help: impl Into<String>) -> Self {
        self.help = Some(help.into());
        self
    }
}

impl fmt::Display for Diagnostic {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let severity = match self.severity {
            Severity::Error => "error",
            Severity::Warning => "warning",
            Severity::Info => "info",
        };
        let code = match self.kind {
            DiagnosticKind::ParseError => "E0001 ParseError",
            DiagnosticKind::NameError => "E0002 NameError",
            DiagnosticKind::AccessError => "E0101 AccessError",
            DiagnosticKind::TheoryBridgeError => "E0201 TheoryBridgeError",
            DiagnosticKind::NoAmbientTheoryError => "E0202 NoAmbientTheoryError",
            DiagnosticKind::UnsupportedFeature => "E0900 UnsupportedFeature",
            DiagnosticKind::RuntimeStaticMismatch => "E0301 RuntimeStaticMismatch",
            DiagnosticKind::TrustTaintError => "E0401 TrustTaintError",
            DiagnosticKind::RuntimeError => "E1001 RuntimeError",
            DiagnosticKind::InfinityModeError => "E0501 InfinityModeError",
            DiagnosticKind::EqualityModeError => "E0601 EqualityModeError",
            DiagnosticKind::MigrationBridgeError => "E0701 MigrationBridgeError",
            DiagnosticKind::DistributedResourceError => "E0801 DistributedResourceError",
            DiagnosticKind::UniverseLevelError => "E0901 UniverseLevelError",
            DiagnosticKind::DefinabilityError => "E0902 DefinabilityError",
            DiagnosticKind::BigNumberError => "E0903 BigNumberError",
            DiagnosticKind::ProofKernelError => "E0904 ProofKernelError",
            DiagnosticKind::TruthBoundaryError => "E0905 TruthBoundaryError",
            DiagnosticKind::IncompletenessBoundaryError => "E0906 IncompletenessBoundaryError",
            DiagnosticKind::ReflectionBoundaryError => "E0907 ReflectionBoundaryError",
            DiagnosticKind::MetaLevelError => "E0908 MetaLevelError",
            DiagnosticKind::StatementTheoremError => "E0909 StatementTheoremError",
            DiagnosticKind::ProofObligationError => "E0910 ProofObligationError",
            DiagnosticKind::TacticScriptError => "E0911 TacticScriptError",
            DiagnosticKind::ProofCertificateError => "E0912 ProofCertificateError",
            DiagnosticKind::ProofCertificateAuditError => "E0913 ProofCertificateAuditError",
            DiagnosticKind::EqualityRewriteError => "E0914 EqualityRewriteError",
            DiagnosticKind::RewriteNormalizationError => "E0915 RewriteNormalizationError",
            DiagnosticKind::InductionError => "E0916 InductionError",
            DiagnosticKind::ModuleImportError => "E0917 ModuleImportError",
            DiagnosticKind::ModuleInterfaceError => "E0918 ModuleInterfaceError",
            DiagnosticKind::MetatheoryDependencyError => "E0919 MetatheoryDependencyError",
            DiagnosticKind::MetatheoryClosureError => "E0920 MetatheoryClosureError",
            DiagnosticKind::ConservativeExtensionError => "E0921 ConservativeExtensionError",
            DiagnosticKind::TheoremDependencyError => "E0922 TheoremDependencyError",
            DiagnosticKind::SoundnessBoundaryError => "E0923 SoundnessBoundaryError",
            DiagnosticKind::TrustedBaseError => "E0924 TrustedBaseError",
            DiagnosticKind::MetatheoryFoundationError => "E0925 MetatheoryFoundationError",
            DiagnosticKind::LogicFormulaError => "E0926 LogicFormulaError",
            DiagnosticKind::SubstitutionError => "E0927 SubstitutionError",
            DiagnosticKind::FunctionTermError => "E0928 FunctionTermError",
            DiagnosticKind::FunctionContractError => "E0929 FunctionContractError",
            DiagnosticKind::StructuralTypeError => "E0930 StructuralTypeError",
            DiagnosticKind::StructuralEliminationError => "E0931 StructuralEliminationError",
            DiagnosticKind::PartialityTypeError => "E0932 PartialityTypeError",
            DiagnosticKind::SequenceTypeError => "E0933 SequenceTypeError",
            DiagnosticKind::TraversalError => "E0934 TraversalError",
            DiagnosticKind::RecursionBoundaryError => "E0935 RecursionBoundaryError",
            DiagnosticKind::TerminationBudgetError => "E0936 TerminationBudgetError",
            DiagnosticKind::StandardPreludeError => "E0937 StandardPreludeError",
            DiagnosticKind::PreludeEvaluationError => "E0938 PreludeEvaluationError",
            DiagnosticKind::PreludeLoweringError => "E0939 PreludeLoweringError",
            DiagnosticKind::BackendCapabilityError => "E0940 BackendCapabilityError",
            DiagnosticKind::BackendLayoutError => "E0941 BackendLayoutError",
            DiagnosticKind::DenseRuntimeError => "E0942 DenseRuntimeError",
            DiagnosticKind::OwnershipError => "E0943 OwnershipError",
            DiagnosticKind::CompileError => "E0950 CompileError",
        };
        if let Some(path) = &self.source_path {
            match self.span {
                Some(span) if !span.line_only() => writeln!(f, "{severity}[{code}] at {}:{}:{}: {}", path.display(), span.start_line, span.start_col, self.message)?,
                Some(span) => writeln!(f, "{severity}[{code}] at {}:{}: {}", path.display(), span.start_line, self.message)?,
                None => writeln!(f, "{severity}[{code}] at {}: {}", path.display(), self.message)?,
            }
        } else {
            match (self.span, self.line) {
                (Some(span), _) => writeln!(f, "{severity}[{code}] at {span}: {}", self.message)?,
                (None, Some(line)) => writeln!(f, "{severity}[{code}] at line {line}: {}", self.message)?,
                (None, None) => writeln!(f, "{severity}[{code}]: {}", self.message)?,
            }
        }
        if let Some(help) = &self.help {
            writeln!(f, "  help: {help}")?;
        }
        Ok(())
    }
}
