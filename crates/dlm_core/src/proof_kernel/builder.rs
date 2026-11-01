//! Small proof producer, outside the trusted replay kernel. Unsupported source
//! constructs return None; existing symbolic proofs retain their legacy status.

use super::*;
use crate::ast::{Expr, ExprKind, StatementItem};

struct Builder {
    arena: Arena,
    variables: Vec<String>,
    steps: Vec<KernelRule>,
    equations: Vec<(usize, usize)>,
}

impl Builder {
    fn step(&mut self, rule: KernelRule, equation: (usize, usize)) -> Option<usize> {
        if self.steps.len() >= self.arena.budget.max_steps {
            return None;
        }
        let id = self.steps.len();
        self.steps.push(rule);
        self.equations.push(equation);
        Some(id)
    }
    fn refl(&mut self, t: usize) -> Option<usize> {
        self.step(KernelRule::Refl(t), (t, t))
    }
    fn sym(&mut self, p: usize) -> Option<usize> {
        let (a, b) = self.equations[p];
        self.step(KernelRule::Sym(p), (b, a))
    }
    fn trans(&mut self, p: usize, q: usize) -> Option<usize> {
        let (a, b) = self.equations[p];
        let (c, d) = self.equations[q];
        if b != c {
            return None;
        }
        self.step(KernelRule::Trans(p, q), (a, d))
    }
    fn term(&mut self, e: &Expr, depth: usize) -> Option<usize> {
        if depth > 96 {
            return None;
        }
        let term = match &e.kind {
            ExprKind::Ident(name) => {
                KernelTerm::Var(self.variables.iter().position(|n| n == name)?)
            }
            ExprKind::IntLiteral(n) => KernelTerm::Nat(n.parse().ok()?),
            ExprKind::Add { lhs, rhs } => {
                KernelTerm::Add(self.term(lhs, depth + 1)?, self.term(rhs, depth + 1)?)
            }
            ExprKind::Call { name, args } if name == "mul" && args.len() == 2 => KernelTerm::Mul(
                self.term(&args[0], depth + 1)?,
                self.term(&args[1], depth + 1)?,
            ),
            _ => return None,
        };
        self.arena.make(term).ok()
    }
    fn candidates(&self, t: usize) -> Vec<(AlgebraLaw, Vec<usize>)> {
        use AlgebraLaw::*;
        match self.arena.terms[t] {
            KernelTerm::Add(a, b) => {
                let mut out = vec![
                    (AddComm, vec![a, b]),
                    (AddZero, vec![a]),
                    (ZeroAdd, vec![b]),
                ];
                if let KernelTerm::Add(x, y) = self.arena.terms[a] {
                    out.push((AddAssoc, vec![x, y, b]));
                }
                out
            }
            KernelTerm::Mul(a, b) => {
                let mut out = vec![
                    (MulComm, vec![a, b]),
                    (MulOne, vec![a]),
                    (OneMul, vec![b]),
                    (MulZero, vec![a]),
                    (ZeroMul, vec![b]),
                ];
                if let KernelTerm::Mul(x, y) = self.arena.terms[a] {
                    out.push((MulAssoc, vec![x, y, b]));
                }
                if let KernelTerm::Add(x, y) = self.arena.terms[b] {
                    out.push((Distribute, vec![a, x, y]));
                }
                if let KernelTerm::Add(x, y) = self.arena.terms[a] {
                    out.push((DistributeRight, vec![x, y, b]));
                }
                out
            }
            _ => Vec::new(),
        }
    }
    fn direct(&mut self, a: usize, b: usize) -> Option<usize> {
        if a == b {
            return self.refl(a);
        }
        for (lhs, rhs, reverse) in [(a, b, false), (b, a, true)] {
            for (law, args) in self.candidates(lhs) {
                let eq = law_equation(&mut self.arena, law, &args).ok()?;
                if eq == (lhs, rhs) {
                    let p = self.step(KernelRule::Law(law, args), eq)?;
                    return if reverse { self.sym(p) } else { Some(p) };
                }
            }
        }
        None
    }
    fn normalize(&mut self, t: usize, depth: usize) -> Option<(usize, usize)> {
        if depth > 96 {
            return None;
        }
        let original = self.arena.terms[t].clone();
        let (a, b, multiply) = match original {
            KernelTerm::Add(a, b) => (a, b, false),
            KernelTerm::Mul(a, b) => (a, b, true),
            _ => return Some((t, self.refl(t)?)),
        };
        let (a, pa) = self.normalize(a, depth + 1)?;
        let (b, pb) = self.normalize(b, depth + 1)?;
        let rebuilt = self
            .arena
            .make(if multiply {
                KernelTerm::Mul(a, b)
            } else {
                KernelTerm::Add(a, b)
            })
            .ok()?;
        let p = self.step(
            if multiply {
                KernelRule::Mul(pa, pb)
            } else {
                KernelRule::Add(pa, pb)
            },
            (t, rebuilt),
        )?;
        use AlgebraLaw::*;
        let rule = match (&self.arena.terms[a], &self.arena.terms[b], multiply) {
            (KernelTerm::Nat(0), _, false) => Some((ZeroAdd, vec![b])),
            (_, KernelTerm::Nat(0), false) => Some((AddZero, vec![a])),
            (KernelTerm::Nat(0), _, true) => Some((ZeroMul, vec![b])),
            (_, KernelTerm::Nat(0), true) => Some((MulZero, vec![a])),
            (KernelTerm::Nat(1), _, true) => Some((OneMul, vec![b])),
            (_, KernelTerm::Nat(1), true) => Some((MulOne, vec![a])),
            _ => None,
        };
        let (result, q) = if let Some((law, args)) = rule {
            let eq = law_equation(&mut self.arena, law, &args).ok()?;
            (eq.1, self.step(KernelRule::Law(law, args), eq)?)
        } else if let (KernelTerm::Nat(x), KernelTerm::Nat(y)) =
            (&self.arena.terms[a], &self.arena.terms[b])
        {
            let n = if multiply {
                x.checked_mul(*y)?
            } else {
                x.checked_add(*y)?
            };
            let result = self.arena.make(KernelTerm::Nat(n)).ok()?;
            (
                result,
                self.step(KernelRule::Eval(rebuilt), (rebuilt, result))?,
            )
        } else {
            (rebuilt, self.refl(rebuilt)?)
        };
        Some((result, self.trans(p, q)?))
    }
}

/// Produces kernel-checkable evidence for a deliberately bounded Nat fragment.
/// Does not consume a symbolic success flag, proof trace or theorem passport.
pub fn build_equational_proof(item: &StatementItem) -> Option<KernelProof> {
    if item.bound.is_some() || item.params.iter().any(|p| p.ty != "Nat") {
        return None;
    }
    let ExprKind::Call { name, args } = &item.body.kind else {
        return None;
    };
    if name != "nat_eq" || args.len() != 2 {
        return None;
    }
    let budget = KernelBudget::default();
    let mut b = Builder {
        arena: Arena {
            terms: Vec::new(),
            index: BTreeMap::new(),
            work: 0,
            budget,
        },
        variables: item.params.iter().map(|p| p.name.clone()).collect(),
        steps: Vec::new(),
        equations: Vec::new(),
    };
    let lhs = b.term(&args[0], 0)?;
    let rhs = b.term(&args[1], 0)?;
    let root = if let Some(p) = b.direct(lhs, rhs) {
        p
    } else {
        let (a, p) = b.normalize(lhs, 0)?;
        let (c, q) = b.normalize(rhs, 0)?;
        let middle = b.direct(a, c)?;
        let start = b.trans(p, middle)?;
        let end = b.sym(q)?;
        b.trans(start, end)?
    };
    let proof = KernelProof {
        variables: b.variables,
        terms: b.arena.terms,
        steps: b.steps,
        goal: (lhs, rhs),
        root,
    };
    check_kernel_proof(&proof, &KernelContext::default(), budget).ok()?;
    if proof.proposition(budget).ok()? != crate::ordinary::statement_proposition(item) {
        return None;
    }
    Some(proof)
}
