//! Computable real numbers as finite expression programs (numeric criterion N08).
//!
//! A real is denoted by a program: a nonempty list of at most 64 nodes, each a
//! rational constant or an operation on earlier nodes; the last node is the
//! value (grammar and semantics in `docs/COMPUTABLE_REALS.md`). `approx(n)`
//! returns a closed rational interval that contains the denoted real and has
//! width at most `2^-n`, or an explicit failure:
//! * a domain error when the bounds prove it (a divisor that is exactly zero,
//!   the square root of a value whose interval is below zero);
//! * `UnknownWithinBudget` when the refinement limit, precision or work budget
//!   is reached first (for example a divisor whose interval keeps containing
//!   zero: it may be zero or merely small). This is never a refutation.
//!
//! Comparison is decided by the bounds only and returns `None` (undecided)
//! when the intervals still overlap at the requested precision.
use super::exact::{ExactBudget, Natural, Rational};
use super::interval_q::{IntervalOrder, IntervalQ, MAX_SQRT_BITS};
use super::{require, MathError, MathResult};
use std::cmp::Ordering;

pub const MAX_NODES: usize = 64;
/// Largest requested precision `n` (width `<= 2^-n`).
pub const MAX_PRECISION: u32 = 4096;
/// Refinement rounds: working resolution `(n + 8) * 2^r`, `r < ROUNDS`.
pub const ROUNDS: u32 = 6;
pub const GUARD_BITS: u32 = 8;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CRealOp {
    Const,
    Add,
    Sub,
    Mul,
    Neg,
    Inv,
    Div,
    Sqrt,
    Square,
    Abs,
    /// v0.104 (N10): elementary functions and the constant pi.
    Exp,
    Log,
    Sin,
    Cos,
    Pi,
}

impl CRealOp {
    pub const ALL: [CRealOp; 15] = [
        CRealOp::Const,
        CRealOp::Add,
        CRealOp::Sub,
        CRealOp::Mul,
        CRealOp::Neg,
        CRealOp::Inv,
        CRealOp::Div,
        CRealOp::Sqrt,
        CRealOp::Square,
        CRealOp::Abs,
        CRealOp::Exp,
        CRealOp::Log,
        CRealOp::Sin,
        CRealOp::Cos,
        CRealOp::Pi,
    ];
    /// Numeric code shared with the ordinary DLM library (`CRealNode.op`).
    pub fn code(self) -> u32 {
        CRealOp::ALL.iter().position(|o| *o == self).unwrap() as u32
    }
    pub fn name(self) -> &'static str {
        match self {
            CRealOp::Const => "c",
            CRealOp::Add => "add",
            CRealOp::Sub => "sub",
            CRealOp::Mul => "mul",
            CRealOp::Neg => "neg",
            CRealOp::Inv => "inv",
            CRealOp::Div => "div",
            CRealOp::Sqrt => "sqrt",
            CRealOp::Square => "square",
            CRealOp::Abs => "abs",
            CRealOp::Exp => "exp",
            CRealOp::Log => "log",
            CRealOp::Sin => "sin",
            CRealOp::Cos => "cos",
            CRealOp::Pi => "pi",
        }
    }
    pub fn arity(self) -> usize {
        match self {
            CRealOp::Const | CRealOp::Pi => 0,
            CRealOp::Add | CRealOp::Sub | CRealOp::Mul | CRealOp::Div => 2,
            _ => 1,
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CNode {
    pub op: CRealOp,
    /// Operand indices (earlier nodes); unused operands are 0.
    pub a: usize,
    pub b: usize,
    /// The constant of a `Const` node; zero otherwise.
    pub value: Rational,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CReal {
    nodes: Vec<CNode>,
}

enum Step {
    Done(IntervalQ),
    Retry,
}

impl CReal {
    /// Validates the program: 1..=64 nodes, operands refer to earlier nodes,
    /// unused operands and non-constant values are zero, constants fit the
    /// precision.
    pub fn new(nodes: Vec<CNode>, budget: &mut ExactBudget) -> MathResult<Self> {
        require(
            !nodes.is_empty() && nodes.len() <= MAX_NODES,
            "computable real program must have 1..64 nodes",
        )?;
        for (i, node) in nodes.iter().enumerate() {
            let arity = node.op.arity();
            require(
                (arity < 1 || node.a < i) && (arity < 2 || node.b < i),
                "computable real operand must refer to an earlier node",
            )?;
            require(
                (arity >= 1 || node.a == 0) && (arity >= 2 || node.b == 0),
                "computable real unused operands must be 0",
            )?;
            if node.op == CRealOp::Const {
                node.value.check_budget(budget)?;
            } else {
                require(
                    node.value.is_zero(),
                    "computable real non-constant node must carry 0",
                )?;
            }
        }
        budget.charge(nodes.len())?;
        Ok(Self { nodes })
    }
    pub fn constant(value: Rational, budget: &mut ExactBudget) -> MathResult<Self> {
        Self::new(
            vec![CNode {
                op: CRealOp::Const,
                a: 0,
                b: 0,
                value,
            }],
            budget,
        )
    }
    pub fn nodes(&self) -> &[CNode] {
        &self.nodes
    }
    /// Text form: tokens separated by single spaces, nodes separated by the
    /// token `;`; a node is `c <rational>` or `<op> <a> [<b>]` with canonical
    /// decimal indices (`c 2 ; sqrt 0 ; c 1 ; add 1 2`). The last node is the
    /// value.
    pub fn parse(text: &str, budget: &mut ExactBudget) -> MathResult<Self> {
        require(
            text.len() <= MAX_NODES * (2 * budget.max_bits() + 16),
            "UnknownWithinBudget: computable real input exceeded",
        )?;
        let invalid = || MathError("invalid computable real text".into());
        let tokens: Vec<&str> = text.split(' ').collect();
        // Canonical decimal, at most u128 (the source Nat range); larger
        // in-range values fail the earlier-node check of `new`.
        let index = |w: &str| -> MathResult<usize> {
            let i: u128 = w.parse().map_err(|_| invalid())?;
            require(i.to_string() == w, "invalid computable real text")?;
            Ok(usize::try_from(i).unwrap_or(usize::MAX))
        };
        let mut nodes = Vec::new();
        let mut at = 0;
        loop {
            require(
                nodes.len() < MAX_NODES,
                "computable real program must have 1..64 nodes",
            )?;
            let op = CRealOp::ALL
                .iter()
                .copied()
                .find(|o| tokens.get(at) == Some(&o.name()))
                .ok_or_else(invalid)?;
            let width = if op == CRealOp::Const { 1 } else { op.arity() };
            let operands = tokens.get(at + 1..at + 1 + width).ok_or_else(invalid)?;
            let node = match (op, operands) {
                (CRealOp::Const, [q]) => CNode {
                    op,
                    a: 0,
                    b: 0,
                    value: Rational::parse(q, budget)?,
                },
                (CRealOp::Pi, []) => CNode {
                    op,
                    a: 0,
                    b: 0,
                    value: Rational::zero(),
                },
                (_, [a]) => CNode {
                    op,
                    a: index(a)?,
                    b: 0,
                    value: Rational::zero(),
                },
                (_, [a, b]) => CNode {
                    op,
                    a: index(a)?,
                    b: index(b)?,
                    value: Rational::zero(),
                },
                _ => return Err(invalid()),
            };
            nodes.push(node);
            at += 1 + width;
            match tokens.get(at) {
                None => break,
                Some(&";") if at + 1 < tokens.len() => at += 1,
                Some(_) => return Err(invalid()),
            }
        }
        Self::new(nodes, budget)
    }

    /// One evaluation of the program with square roots at resolution `k`.
    fn evaluate(&self, k: u32, budget: &mut ExactBudget) -> MathResult<Step> {
        let mut values: Vec<IntervalQ> = Vec::with_capacity(self.nodes.len());
        for node in &self.nodes {
            let a = || &values[node.a];
            let value = match node.op {
                CRealOp::Const => IntervalQ::point(node.value.clone(), budget)?,
                CRealOp::Add => a().checked_add(&values[node.b], budget)?,
                CRealOp::Sub => a().checked_sub(&values[node.b], budget)?,
                CRealOp::Mul => a().checked_mul(&values[node.b], budget)?,
                CRealOp::Neg => a().checked_neg(budget)?,
                CRealOp::Square => a().checked_square(budget)?,
                CRealOp::Abs => a().checked_abs(budget)?,
                CRealOp::Exp => super::elementary::exp(a(), k, budget)?,
                CRealOp::Log => {
                    let x = a();
                    if !x.hi().negative() && !x.hi().is_zero() {
                        if x.lo().negative() || x.lo().is_zero() {
                            return Ok(Step::Retry);
                        }
                        super::elementary::log(x, k, budget)?
                    } else {
                        return Err(MathError(
                            "computable real logarithm of a non-positive value".into(),
                        ));
                    }
                }
                CRealOp::Sin => super::elementary::trig(a(), k, false, budget)?,
                CRealOp::Cos => super::elementary::trig(a(), k, true, budget)?,
                CRealOp::Pi => super::elementary::pi(k, budget)?,
                CRealOp::Inv | CRealOp::Div => {
                    let divisor = if node.op == CRealOp::Inv {
                        a()
                    } else {
                        &values[node.b]
                    };
                    if divisor.lo().is_zero() && divisor.hi().is_zero() {
                        return Err(MathError("computable real division by zero".into()));
                    }
                    if divisor.contains_zero(budget)? {
                        return Ok(Step::Retry);
                    }
                    if node.op == CRealOp::Inv {
                        a().checked_inv(budget)?
                    } else {
                        a().checked_div(divisor, budget)?
                    }
                }
                CRealOp::Sqrt => {
                    let x = a();
                    if x.hi().negative() {
                        return Err(MathError(
                            "computable real square root of a negative value".into(),
                        ));
                    }
                    if x.lo().negative() {
                        return Ok(Step::Retry);
                    }
                    x.checked_sqrt(k, budget)?
                }
            };
            values.push(value);
        }
        Ok(Step::Done(values.pop().expect("nonempty program")))
    }

    /// Interval containing the real, of width `<= 2^-n`.
    pub fn approx(&self, n: u32, budget: &mut ExactBudget) -> MathResult<IntervalQ> {
        require(
            n <= MAX_PRECISION,
            "computable real precision exceeds 4096 bits",
        )?;
        let scale = Natural::from_u128(2).checked_pow(u128::from(n), budget)?;
        let tolerance = Rational::new(false, Natural::from_u128(1), scale, budget)?;
        let mut k = n + GUARD_BITS;
        for _ in 0..ROUNDS {
            if k > MAX_SQRT_BITS {
                break;
            }
            if let Step::Done(value) = self.evaluate(k, budget)? {
                if value.width(budget)?.checked_cmp(&tolerance, budget)? != Ordering::Greater {
                    return Ok(value);
                }
            }
            k *= 2;
        }
        Err(MathError(
            "UnknownWithinBudget: computable real refinement limit reached".into(),
        ))
    }

    /// Order decided by the bounds at precision `n`; `None` when undecided.
    /// `Some(Equal)` only when both are the same exact point.
    pub fn compare(
        &self,
        other: &Self,
        n: u32,
        budget: &mut ExactBudget,
    ) -> MathResult<Option<Ordering>> {
        let x = self.approx(n, budget)?;
        let y = other.approx(n, budget)?;
        Ok(match x.compare(&y, budget)? {
            IntervalOrder::Less => Some(Ordering::Less),
            IntervalOrder::Greater => Some(Ordering::Greater),
            IntervalOrder::Equal => Some(Ordering::Equal),
            IntervalOrder::Overlap => None,
        })
    }
}

/// v0.103: builds a program from embedded programs (their nodes are appended
/// with shifted operands) and new operations; the last node is the root.
#[derive(Clone, Debug, Default)]
pub struct CRealBuilder {
    nodes: Vec<CNode>,
}

impl CRealBuilder {
    pub fn new() -> Self {
        Self::default()
    }
    /// Appends `x` and returns the index of its root.
    pub fn embed(&mut self, x: &CReal) -> usize {
        let offset = self.nodes.len();
        for node in x.nodes() {
            let arity = node.op.arity();
            self.nodes.push(CNode {
                op: node.op,
                a: if arity >= 1 { node.a + offset } else { 0 },
                b: if arity >= 2 { node.b + offset } else { 0 },
                value: node.value.clone(),
            });
        }
        self.nodes.len() - 1
    }
    /// Appends an operation on earlier nodes (`b` is ignored for unary ones).
    pub fn op(&mut self, op: CRealOp, a: usize, b: usize) -> usize {
        let arity = op.arity();
        assert!(arity > 0, "constants are embedded");
        self.nodes.push(CNode {
            op,
            a,
            b: if arity == 2 { b } else { 0 },
            value: Rational::zero(),
        });
        self.nodes.len() - 1
    }
    /// The program rooted at the last node (validated, at most 64 nodes).
    pub fn finish(self, budget: &mut ExactBudget) -> MathResult<CReal> {
        CReal::new(self.nodes, budget)
    }
}

impl std::fmt::Display for CReal {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        for (i, node) in self.nodes.iter().enumerate() {
            if i > 0 {
                f.write_str(" ; ")?;
            }
            match node.op.arity() {
                0 if node.op == CRealOp::Pi => f.write_str("pi")?,
                0 => write!(f, "c {}", node.value)?,
                1 => write!(f, "{} {}", node.op.name(), node.a)?,
                _ => write!(f, "{} {} {}", node.op.name(), node.a, node.b)?,
            }
        }
        Ok(())
    }
}
