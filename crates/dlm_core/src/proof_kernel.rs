//! Compiler/proof-only equational kernel. Checks a finite DAG, without tactics,
//! symbolic normalization, source evaluation or access to theorem passports.
//! Variables range over mathematical Nat; this does not prove Defined_128.

use std::collections::{BTreeMap, BTreeSet};
use std::fmt;

pub mod builder;
mod codec;

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub enum KernelTerm {
    Var(usize),
    Nat(u128),
    Add(usize, usize),
    Mul(usize, usize),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AlgebraLaw {
    AddZero,
    ZeroAdd,
    MulOne,
    OneMul,
    MulZero,
    ZeroMul,
    AddComm,
    MulComm,
    AddAssoc,
    MulAssoc,
    Distribute,
    DistributeRight,
}

impl AlgebraLaw {
    pub fn arity(self) -> usize {
        match self {
            Self::AddZero
            | Self::ZeroAdd
            | Self::MulOne
            | Self::OneMul
            | Self::MulZero
            | Self::ZeroMul => 1,
            Self::AddComm | Self::MulComm => 2,
            _ => 3,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum KernelRule {
    Refl(usize),
    Sym(usize),
    Trans(usize, usize),
    Add(usize, usize),
    Mul(usize, usize),
    Law(AlgebraLaw, Vec<usize>),
    /// Evaluation of one operation on literal operands (bounded exact checker arithmetic).
    Eval(usize),
    /// Index in an explicitly supplied external context, never in the proof itself.
    Axiom(usize),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct KernelProof {
    pub variables: Vec<String>,
    pub terms: Vec<KernelTerm>,
    pub steps: Vec<KernelRule>,
    pub goal: (usize, usize),
    pub root: usize,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct KernelAxiom {
    pub name: String,
    /// The declaration owns its terms and binders: untrusted proof IDs cannot
    /// change an authorized assumption by reordering the proof's term table.
    pub variables: Vec<String>,
    pub terms: Vec<KernelTerm>,
    pub equation: (usize, usize),
}

#[derive(Debug, Clone, Default)]
pub struct KernelContext {
    pub axioms: Vec<KernelAxiom>,
}

#[derive(Debug, Clone, Copy)]
pub struct KernelBudget {
    pub max_nodes: usize,
    pub max_steps: usize,
    pub max_depth: usize,
    pub max_work: usize,
    pub max_text_bytes: usize,
}

impl Default for KernelBudget {
    fn default() -> Self {
        Self {
            max_nodes: 50_000,
            max_steps: 20_000,
            max_depth: 256,
            max_work: 200_000,
            max_text_bytes: 4 << 20,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum KernelErrorKind {
    Invalid,
    UnknownWithinBudget,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct KernelError {
    pub kind: KernelErrorKind,
    pub message: String,
}

impl fmt::Display for KernelError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}: {}", self.kind, self.message)
    }
}

fn invalid(message: impl Into<String>) -> KernelError {
    KernelError {
        kind: KernelErrorKind::Invalid,
        message: message.into(),
    }
}
fn exhausted(message: impl Into<String>) -> KernelError {
    KernelError {
        kind: KernelErrorKind::UnknownWithinBudget,
        message: message.into(),
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct KernelCheck {
    pub checked_terms: usize,
    pub checked_steps: usize,
    pub used_axioms: Vec<String>,
}

struct Arena {
    terms: Vec<KernelTerm>,
    index: BTreeMap<KernelTerm, usize>,
    work: usize,
    budget: KernelBudget,
}

impl Arena {
    fn make(&mut self, term: KernelTerm) -> Result<usize, KernelError> {
        if self.work >= self.budget.max_work {
            return Err(exhausted("kernel work budget exhausted"));
        }
        self.work += 1;
        if let Some(id) = self.index.get(&term) {
            return Ok(*id);
        }
        if self.terms.len() >= self.budget.max_nodes {
            return Err(exhausted("kernel term budget exhausted"));
        }
        let id = self.terms.len();
        self.index.insert(term.clone(), id);
        self.terms.push(term);
        Ok(id)
    }
    fn add(&mut self, a: usize, b: usize) -> Result<usize, KernelError> {
        self.make(KernelTerm::Add(a, b))
    }
    fn mul(&mut self, a: usize, b: usize) -> Result<usize, KernelError> {
        self.make(KernelTerm::Mul(a, b))
    }
}

fn law_equation(
    arena: &mut Arena,
    law: AlgebraLaw,
    args: &[usize],
) -> Result<(usize, usize), KernelError> {
    if args.len() != law.arity() {
        return Err(invalid("algebra law has the wrong arity"));
    }
    let a = args[0];
    use AlgebraLaw::*;
    Ok(match law {
        AddZero | ZeroAdd | MulOne | OneMul | MulZero | ZeroMul => {
            let literal = arena.make(KernelTerm::Nat(if matches!(law, MulOne | OneMul) {
                1
            } else {
                0
            }))?;
            let lhs = match law {
                AddZero => arena.add(a, literal)?,
                ZeroAdd => arena.add(literal, a)?,
                MulOne | MulZero => arena.mul(a, literal)?,
                _ => arena.mul(literal, a)?,
            };
            (
                lhs,
                if matches!(law, MulZero | ZeroMul) {
                    literal
                } else {
                    a
                },
            )
        }
        AddComm => (arena.add(a, args[1])?, arena.add(args[1], a)?),
        MulComm => (arena.mul(a, args[1])?, arena.mul(args[1], a)?),
        AddAssoc | MulAssoc => {
            let (b, c) = (args[1], args[2]);
            if law == AddAssoc {
                let ab = arena.add(a, b)?;
                let bc = arena.add(b, c)?;
                (arena.add(ab, c)?, arena.add(a, bc)?)
            } else {
                let ab = arena.mul(a, b)?;
                let bc = arena.mul(b, c)?;
                (arena.mul(ab, c)?, arena.mul(a, bc)?)
            }
        }
        Distribute => {
            let bc = arena.add(args[1], args[2])?;
            let ab = arena.mul(a, args[1])?;
            let ac = arena.mul(a, args[2])?;
            (arena.mul(a, bc)?, arena.add(ab, ac)?)
        }
        DistributeRight => {
            let ab = arena.add(a, args[1])?;
            let ac = arena.mul(a, args[2])?;
            let bc = arena.mul(args[1], args[2])?;
            (arena.mul(ab, args[2])?, arena.add(ac, bc)?)
        }
    })
}

fn identifier(name: &str) -> bool {
    let mut chars = name.chars();
    matches!(chars.next(), Some(c) if c.is_alphabetic() || c == '_')
        && chars.all(|c| c.is_alphanumeric() || c == '_')
}

fn import_terms(
    terms: &[KernelTerm],
    variables: usize,
    arena: &mut Arena,
) -> Result<Vec<usize>, KernelError> {
    if terms.len() > arena.budget.max_nodes {
        return Err(exhausted("kernel term budget exhausted"));
    }
    let mut ids = Vec::new();
    let mut depths: Vec<usize> = Vec::new();
    for term in terms {
        let (term, depth) = match term {
            KernelTerm::Var(id) if *id < variables => (term.clone(), 1),
            KernelTerm::Var(_) => return Err(invalid("variable is outside its declared scope")),
            KernelTerm::Nat(_) => (term.clone(), 1),
            KernelTerm::Add(a, b) | KernelTerm::Mul(a, b) => {
                let x = *ids
                    .get(*a)
                    .ok_or_else(|| invalid("term has a forward or unknown operand"))?;
                let y = *ids
                    .get(*b)
                    .ok_or_else(|| invalid("term has a forward or unknown operand"))?;
                let term = if matches!(term, KernelTerm::Add(..)) {
                    KernelTerm::Add(x, y)
                } else {
                    KernelTerm::Mul(x, y)
                };
                (term, 1 + depths[*a].max(depths[*b]))
            }
        };
        if depth > arena.budget.max_depth {
            return Err(exhausted("kernel term depth budget exhausted"));
        }
        ids.push(arena.make(term)?);
        depths.push(depth);
    }
    Ok(ids)
}

/// Verifies every node, including unused nodes. All edges point backward;
/// cycles, forward references and out-of-scope variables are rejected.
pub fn check_kernel_proof(
    proof: &KernelProof,
    context: &KernelContext,
    budget: KernelBudget,
) -> Result<KernelCheck, KernelError> {
    if proof.terms.len() > budget.max_nodes
        || proof.steps.len() > budget.max_steps
        || proof.variables.len() > 1024
    {
        return Err(exhausted("kernel object exceeds the declared size budget"));
    }
    let mut names = BTreeSet::new();
    for name in &proof.variables {
        if name.len() > 1024 || !identifier(name) || !names.insert(name) {
            return Err(invalid("invalid or duplicate variable binder"));
        }
    }
    let mut arena = Arena {
        terms: Vec::new(),
        index: BTreeMap::new(),
        work: 0,
        budget,
    };
    if context.axioms.len() > budget.max_steps {
        return Err(exhausted("axiom context size budget exhausted"));
    }
    let mut axiom_names = BTreeSet::new();
    let mut axiom_name_bytes = 0usize;
    for axiom in &context.axioms {
        axiom_name_bytes = axiom_name_bytes.saturating_add(axiom.name.len());
        if axiom_name_bytes > budget.max_text_bytes {
            return Err(exhausted("axiom context text budget exhausted"));
        }
        if axiom.name.trim().is_empty()
            || axiom.name.chars().any(char::is_control)
            || !axiom_names.insert(&axiom.name)
        {
            return Err(invalid("invalid or duplicate axiom declaration name"));
        }
    }
    let ids = import_terms(&proof.terms, proof.variables.len(), &mut arena)?;
    let term_id = |id: usize| {
        ids.get(id)
            .copied()
            .ok_or_else(|| invalid("unknown term ID"))
    };
    let goal = (term_id(proof.goal.0)?, term_id(proof.goal.1)?);
    let mut equations: Vec<(usize, usize)> = Vec::new();
    let mut used_axioms = BTreeSet::new();
    let mut imported_axioms = BTreeMap::new();
    for step in &proof.steps {
        if arena.work >= budget.max_work {
            return Err(exhausted("kernel work budget exhausted"));
        }
        arena.work += 1;
        let premise = |id: usize| {
            equations
                .get(id)
                .copied()
                .ok_or_else(|| invalid("proof has a forward or unknown premise"))
        };
        let equation = match step {
            KernelRule::Refl(t) => {
                let t = term_id(*t)?;
                (t, t)
            }
            KernelRule::Sym(p) => {
                let (a, b) = premise(*p)?;
                (b, a)
            }
            KernelRule::Trans(p, q) => {
                let (a, b) = premise(*p)?;
                let (c, d) = premise(*q)?;
                if b != c {
                    return Err(invalid("transitivity premises do not meet"));
                }
                (a, d)
            }
            KernelRule::Add(p, q) | KernelRule::Mul(p, q) => {
                let (a, b) = premise(*p)?;
                let (c, d) = premise(*q)?;
                if matches!(step, KernelRule::Add(..)) {
                    (arena.add(a, c)?, arena.add(b, d)?)
                } else {
                    (arena.mul(a, c)?, arena.mul(b, d)?)
                }
            }
            KernelRule::Law(law, args) => {
                if args.len() != law.arity() {
                    return Err(invalid("algebra law has the wrong arity"));
                }
                let args = args
                    .iter()
                    .map(|id| term_id(*id))
                    .collect::<Result<Vec<_>, _>>()?;
                law_equation(&mut arena, *law, &args)?
            }
            KernelRule::Eval(t) => {
                let id = term_id(*t)?;
                let value = match arena.terms[id] {
                    KernelTerm::Nat(n) => n,
                    KernelTerm::Add(a, b) | KernelTerm::Mul(a, b) => {
                        let (KernelTerm::Nat(a), KernelTerm::Nat(b)) =
                            (&arena.terms[a], &arena.terms[b])
                        else {
                            return Err(invalid("evaluation operands must be Nat literals"));
                        };
                        if matches!(arena.terms[id], KernelTerm::Add(..)) {
                            a.checked_add(*b)
                        } else {
                            a.checked_mul(*b)
                        }
                        .ok_or_else(|| {
                            exhausted(
                                "exact constant evaluation exceeds the checker's numeric capacity",
                            )
                        })?
                    }
                    _ => return Err(invalid("evaluation of a variable is not a proof")),
                };
                (id, arena.make(KernelTerm::Nat(value))?)
            }
            KernelRule::Axiom(id) => {
                let axiom = context
                    .axioms
                    .get(*id)
                    .ok_or_else(|| invalid("undeclared axiom"))?;
                if axiom.name.trim().is_empty() {
                    return Err(invalid("axiom has no declared name"));
                }
                if axiom.variables != proof.variables {
                    return Err(invalid("axiom binder scope differs from the proof scope"));
                }
                used_axioms.insert(axiom.name.clone());
                if let Some(eq) = imported_axioms.get(id) {
                    *eq
                } else {
                    let terms = import_terms(&axiom.terms, axiom.variables.len(), &mut arena)?;
                    let a = *terms
                        .get(axiom.equation.0)
                        .ok_or_else(|| invalid("unknown axiom term"))?;
                    let b = *terms
                        .get(axiom.equation.1)
                        .ok_or_else(|| invalid("unknown axiom term"))?;
                    imported_axioms.insert(*id, (a, b));
                    (a, b)
                }
            }
        };
        equations.push(equation);
    }
    if equations.get(proof.root) != Some(&goal) {
        return Err(invalid("proof root does not establish the declared goal"));
    }
    Ok(KernelCheck {
        checked_terms: proof.terms.len(),
        checked_steps: proof.steps.len(),
        used_axioms: used_axioms.into_iter().collect(),
    })
}
