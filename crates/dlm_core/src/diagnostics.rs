use std::fmt;

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
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SourceSpan {
    pub start_line: usize,
    pub start_col: usize,
    pub end_line: usize,
    pub end_col: usize,
}

impl SourceSpan {
    pub fn line(line: usize) -> Self {
        Self {
            start_line: line,
            start_col: 0,
            end_line: line,
            end_col: 0,
        }
    }

    pub fn line_col(line: usize, col: usize, len: usize) -> Self {
        let width = len.max(1);
        Self {
            start_line: line,
            start_col: col.max(1),
            end_line: line,
            end_col: col.max(1) + width,
        }
    }

    pub fn new(start_line: usize, start_col: usize, end_line: usize, end_col: usize) -> Self {
        Self {
            start_line,
            start_col,
            end_line,
            end_col,
        }
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
            message: message.into(),
            help: None,
        }
    }

    pub fn with_span(mut self, span: SourceSpan) -> Self {
        self.line = Some(span.primary_line());
        self.span = Some(span);
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
        };
        match (self.span, self.line) {
            (Some(span), _) => writeln!(f, "{severity}[{code}] at {span}: {}", self.message)?,
            (None, Some(line)) => writeln!(f, "{severity}[{code}] at line {line}: {}", self.message)?,
            (None, None) => writeln!(f, "{severity}[{code}]: {}", self.message)?,
        }
        if let Some(help) = &self.help {
            writeln!(f, "  help: {help}")?;
        }
        Ok(())
    }
}
