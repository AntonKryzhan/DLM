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
}

#[derive(Debug, Clone)]
pub struct Diagnostic {
    pub severity: Severity,
    pub kind: DiagnosticKind,
    pub line: Option<usize>,
    pub message: String,
    pub help: Option<String>,
}

impl Diagnostic {
    pub fn error(kind: DiagnosticKind, line: Option<usize>, message: impl Into<String>) -> Self {
        Self {
            severity: Severity::Error,
            kind,
            line,
            message: message.into(),
            help: None,
        }
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
        };
        match self.line {
            Some(line) => writeln!(f, "{severity}[{code}] at line {line}: {}", self.message)?,
            None => writeln!(f, "{severity}[{code}]: {}", self.message)?,
        }
        if let Some(help) = &self.help {
            writeln!(f, "  help: {help}")?;
        }
        Ok(())
    }
}
