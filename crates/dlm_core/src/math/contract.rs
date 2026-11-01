//! Uniform error and budget contracts of the scalar numeric APIs (N13).
//!
//! Every refusal of the exact, interval, complex, computable-real,
//! elementary, conversion and portable APIs (host and `std.*` source) falls
//! into one [`ErrorCategory`]; [`MESSAGES`] is the catalogue of all of them.
//! Work is consumed sequentially: a chain of operations on one budget uses
//! the sum of the work of its steps, the first refusal stops the chain, and
//! an exhausted budget is left empty. Host and source count work in different
//! documented units; the contracts (categories, monotone thresholds) are the same.
use super::exact::ExactBudget;
use super::{MathError, MathResult};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum ErrorCategory {
    /// The value is mathematically undefined (division by zero, sqrt/log outside the domain).
    Domain,
    /// The value exists but the requested exact target cannot hold it.
    Unrepresentable,
    /// Malformed or non-canonical input, unknown names, invalid budgets.
    InvalidInput,
    /// An argument exceeds a fixed documented limit (precision, digits, nodes, exponent).
    Limit,
    /// Work, precision or input-size budget exhausted (`UnknownWithinBudget`).
    Budget,
    /// The enclosures at the allowed precision do not decide the answer (`UnknownWithinBudget`).
    Undecided,
}

impl ErrorCategory {
    pub const ALL: [ErrorCategory; 6] = [
        ErrorCategory::Domain,
        ErrorCategory::Unrepresentable,
        ErrorCategory::InvalidInput,
        ErrorCategory::Limit,
        ErrorCategory::Budget,
        ErrorCategory::Undecided,
    ];
    pub fn name(self) -> &'static str {
        match self {
            ErrorCategory::Domain => "domain",
            ErrorCategory::Unrepresentable => "unrepresentable",
            ErrorCategory::InvalidInput => "invalid_input",
            ErrorCategory::Limit => "limit",
            ErrorCategory::Budget => "budget",
            ErrorCategory::Undecided => "undecided",
        }
    }
    pub fn code(self) -> u8 {
        self as u8
    }
    pub fn parse(name: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|c| c.name() == name)
    }
    /// Whether retrying with a larger budget or precision may succeed.
    pub fn retryable(self) -> bool {
        matches!(self, ErrorCategory::Budget | ErrorCategory::Undecided)
    }
}

const UNDECIDED: &[&str] = &[
    "UnknownWithinBudget: rounding undecided",
    "UnknownWithinBudget: computable real refinement limit reached",
];
const DOMAIN: &[&str] = &[
    "division by zero",
    "square root of negative",
    "square root of a negative",
    "logarithm of a non-positive",
    "interval containing zero",
    "negative natural subtraction",
];
const UNREPRESENTABLE: &[&str] = &[
    "is not an integer",
    "is not a finite decimal",
    "does not fit in ",
];
const LIMIT: &[&str] = &[
    "exceeds 4096",
    "exceeds 16384",
    "exponent out of range",
    "must have 1..64 nodes",
];
const INVALID: &[&str] = &[
    "invalid ",
    "non-canonical ",
    "noncanonical ",
    "unknown ",
    "unsupported ",
    "portable number is ",
    "out of order",
    "denominator is zero",
    "must carry 0",
    "must refer to an earlier node",
    "unused operands must be 0",
    "must be 1..16384 bits",
    "witness digest mismatch",
    "witness claim rejected",
];

/// Category of a refusal message, `None` for messages outside the contract
/// (internal invariant failures).
pub fn classify(message: &str) -> Option<ErrorCategory> {
    let has = |list: &[&str]| list.iter().any(|p| message.contains(p));
    if UNDECIDED.iter().any(|p| message.starts_with(p)) {
        Some(ErrorCategory::Undecided)
    } else if message.starts_with("UnknownWithinBudget: ") {
        Some(ErrorCategory::Budget)
    } else if has(INVALID) {
        Some(ErrorCategory::InvalidInput)
    } else if has(DOMAIN) {
        Some(ErrorCategory::Domain)
    } else if has(UNREPRESENTABLE) {
        Some(ErrorCategory::Unrepresentable)
    } else if has(LIMIT) {
        Some(ErrorCategory::Limit)
    } else {
        None
    }
}

impl MathError {
    pub fn category(&self) -> Option<ErrorCategory> {
        classify(&self.0)
    }
}

/// Category of the refusal of a result, `None` for success or an unclassified message.
pub trait MathResultExt {
    fn category(&self) -> Option<ErrorCategory>;
}
impl<T> MathResultExt for MathResult<T> {
    fn category(&self) -> Option<ErrorCategory> {
        self.as_ref().err().and_then(MathError::category)
    }
}

use ErrorCategory::{
    Budget as B, Domain as D, InvalidInput as I, Limit as L, Undecided as U, Unrepresentable as R,
};
/// Every refusal of the scalar numeric APIs, host and source (`h`, `s`, `hs`),
/// with its category. Messages with a variable part are given with an example value.
pub const MESSAGES: &[(&str, ErrorCategory, &str)] = &[
    (
        "UnknownWithinBudget: exact arithmetic work exhausted",
        B,
        "h",
    ),
    ("UnknownWithinBudget: exact decimal input exceeded", B, "h"),
    (
        "UnknownWithinBudget: exact intermediate precision exceeded",
        B,
        "h",
    ),
    ("UnknownWithinBudget: exact precision exceeded", B, "h"),
    ("UnknownWithinBudget: exact rational input exceeded", B, "h"),
    ("UnknownWithinBudget: source decimal input exceeded", B, "s"),
    (
        "UnknownWithinBudget: source decimal steps exhausted",
        B,
        "s",
    ),
    ("UnknownWithinBudget: source gcd steps exhausted", B, "s"),
    ("UnknownWithinBudget: source power steps exhausted", B, "s"),
    ("UnknownWithinBudget: source precision exceeded", B, "s"),
    (
        "UnknownWithinBudget: source rational input exceeded",
        B,
        "s",
    ),
    (
        "UnknownWithinBudget: source rational power steps exhausted",
        B,
        "s",
    ),
    (
        "UnknownWithinBudget: source work reservation exhausted",
        B,
        "s",
    ),
    (
        "UnknownWithinBudget: exact interval input exceeded",
        B,
        "hs",
    ),
    ("UnknownWithinBudget: exact complex input exceeded", B, "hs"),
    (
        "UnknownWithinBudget: computable real input exceeded",
        B,
        "hs",
    ),
    (
        "UnknownWithinBudget: computable complex input exceeded",
        B,
        "s",
    ),
    (
        "UnknownWithinBudget: elementary series steps exhausted",
        B,
        "s",
    ),
    ("UnknownWithinBudget: decimal input exceeded", B, "hs"),
    (
        "UnknownWithinBudget: computable real refinement limit reached",
        U,
        "hs",
    ),
    (
        "UnknownWithinBudget: rounding undecided at precision 64",
        U,
        "hs",
    ),
    ("exact division by zero", D, "hs"),
    ("exact rational division by zero", D, "hs"),
    ("exact square root of negative integer", D, "hs"),
    ("negative natural subtraction", D, "h"),
    ("interval division by an interval containing zero", D, "hs"),
    ("interval square root of negative values", D, "hs"),
    ("exact complex division by zero", D, "hs"),
    ("computable real division by zero", D, "hs"),
    ("computable real logarithm of a non-positive value", D, "hs"),
    ("computable real square root of a negative value", D, "hs"),
    ("rational is not a finite decimal", R, "hs"),
    ("rational is not an integer", R, "hs"),
    ("integer does not fit in Int", R, "hs"),
    ("integer does not fit in Nat", R, "hs"),
    (
        "interval square root resolution exceeds 16384 bits",
        L,
        "hs",
    ),
    ("computable real precision exceeds 4096 bits", L, "hs"),
    ("computable real program must have 1..64 nodes", L, "hs"),
    ("decimal digit count exceeds 4096", L, "hs"),
    ("decimal exponent out of range", L, "hs"),
    ("exact precision must be 1..16384 bits", I, "h"),
    ("invalid exact source budget", I, "s"),
    ("exact rational denominator is zero", I, "hs"),
    ("invalid exact natural decimal", I, "h"),
    ("invalid exact decimal", I, "s"),
    ("invalid big integer words", I, "s"),
    ("invalid canonical big integer", I, "s"),
    ("invalid canonical big rational", I, "s"),
    ("noncanonical big rational", I, "s"),
    ("interval bounds out of order", I, "hs"),
    ("invalid exact interval text: expected [<lo>,<hi>]", I, "hs"),
    (
        "invalid exact complex text: expected <re>+<im>i or <re>-<im>i",
        I,
        "hs",
    ),
    ("computable real non-constant node must carry 0", I, "hs"),
    (
        "computable real operand must refer to an earlier node",
        I,
        "hs",
    ),
    ("computable real unused operands must be 0", I, "hs"),
    ("invalid computable real operation", I, "s"),
    ("invalid computable real text", I, "hs"),
    ("invalid computable complex text", I, "hs"),
    ("invalid decimal number", I, "hs"),
    ("unknown rounding mode", I, "s"),
    ("unknown rounding mode `up`", I, "h"),
    ("invalid portable number", I, "hs"),
    ("non-canonical portable number", I, "hs"),
    ("unsupported portable number version", I, "hs"),
    ("unknown portable number family `float`", I, "hs"),
    ("portable number is `int`, expected `rat`", I, "h"),
    ("invalid scalar witness", I, "hs"),
    ("unsupported scalar witness version", I, "hs"),
    ("unknown scalar witness kind `sum`", I, "hs"),
    ("scalar witness digest mismatch", I, "hs"),
    ("scalar witness claim rejected: quotient", I, "hs"),
];

/// Value and work of one operation on a shared budget.
#[derive(Debug)]
pub struct Metered<T> {
    pub value: MathResult<T>,
    pub work: u64,
}

impl<T> Metered<T> {
    pub fn category(&self) -> Option<ErrorCategory> {
        self.value.as_ref().err().and_then(MathError::category)
    }
}

pub fn metered<T>(
    b: &mut ExactBudget,
    f: impl FnOnce(&mut ExactBudget) -> MathResult<T>,
) -> Metered<T> {
    let before = b.remaining_work();
    let value = f(b);
    Metered {
        work: before - b.remaining_work(),
        value,
    }
}

/// One step of a [`chain`].
pub type Step<'a, T> = Box<dyn FnOnce(&mut ExactBudget) -> MathResult<T> + 'a>;

/// Runs `steps` in order on one budget and stops after the first refusal.
pub fn chain<T>(b: &mut ExactBudget, steps: Vec<Step<'_, T>>) -> Vec<Metered<T>> {
    let mut out = Vec::new();
    for step in steps {
        let m = metered(b, step);
        let stop = m.value.is_err();
        out.push(m);
        if stop {
            break;
        }
    }
    out
}

/// Work used by `f` at precision `bits` with an unbounded work budget.
pub fn required_work<T>(
    bits: usize,
    f: impl FnOnce(&mut ExactBudget) -> MathResult<T>,
) -> MathResult<Metered<T>> {
    let mut b = ExactBudget::new(bits, u64::MAX)?;
    Ok(metered(&mut b, f))
}

/// Checks the threshold contract for `f`: with exactly the required work it
/// gives the same answer and leaves 0, with one unit less it is refused with
/// [`ErrorCategory::Budget`] and the budget is left empty. Returns the work.
pub fn check_threshold<T: PartialEq + std::fmt::Debug>(
    bits: usize,
    f: impl Fn(&mut ExactBudget) -> MathResult<T>,
) -> MathResult<u64> {
    let full = required_work(bits, &f)?;
    let fail = |m: String| Err(MathError(format!("budget contract violated: {m}")));
    let mut exact = ExactBudget::new(bits, full.work)?;
    let again = metered(&mut exact, &f);
    if again.value != full.value || exact.remaining_work() != 0 {
        return fail(format!("rerun with {} work differs", full.work));
    }
    if full.work > 0 {
        let mut short = ExactBudget::new(bits, full.work - 1)?;
        let m = metered(&mut short, &f);
        if m.category() != Some(ErrorCategory::Budget) || short.remaining_work() != 0 {
            return fail(format!("{} work: {:?}", full.work - 1, m.value));
        }
    }
    Ok(full.work)
}
