use super::*;

const LAWS: &[(AlgebraLaw, &str)] = &[
    (AlgebraLaw::AddZero, "add_zero"),
    (AlgebraLaw::ZeroAdd, "zero_add"),
    (AlgebraLaw::MulOne, "mul_one"),
    (AlgebraLaw::OneMul, "one_mul"),
    (AlgebraLaw::MulZero, "mul_zero"),
    (AlgebraLaw::ZeroMul, "zero_mul"),
    (AlgebraLaw::AddComm, "add_comm"),
    (AlgebraLaw::MulComm, "mul_comm"),
    (AlgebraLaw::AddAssoc, "add_assoc"),
    (AlgebraLaw::MulAssoc, "mul_assoc"),
    (AlgebraLaw::Distribute, "distribute"),
    (AlgebraLaw::DistributeRight, "distribute_right"),
];

impl KernelProof {
    /// Versioned, flat representation. Use `check_kernel_proof` before trusting
    /// an object; serialization and its fingerprint are not logical evidence.
    pub fn to_text(&self) -> String {
        let mut out = String::from("DLM-KERNEL-PROOF v1\nrules nat-semiring-v1\n");
        for var in &self.variables {
            out.push_str(&format!("var {var}\n"));
        }
        for term in &self.terms {
            let text = match term {
                KernelTerm::Var(id) => format!("var {id}"),
                KernelTerm::Nat(n) => format!("nat {n}"),
                KernelTerm::Add(a, b) => format!("add {a} {b}"),
                KernelTerm::Mul(a, b) => format!("mul {a} {b}"),
            };
            out.push_str(&format!("term {text}\n"));
        }
        for step in &self.steps {
            let text = match step {
                KernelRule::Refl(t) => format!("refl {t}"),
                KernelRule::Sym(p) => format!("sym {p}"),
                KernelRule::Trans(p, q) => format!("trans {p} {q}"),
                KernelRule::Add(p, q) => format!("add {p} {q}"),
                KernelRule::Mul(p, q) => format!("mul {p} {q}"),
                KernelRule::Eval(t) => format!("eval {t}"),
                KernelRule::Axiom(id) => format!("axiom {id}"),
                KernelRule::Law(law, args) => format!(
                    "law {} {}",
                    LAWS.iter().find(|(l, _)| l == law).unwrap().1,
                    args.iter()
                        .map(ToString::to_string)
                        .collect::<Vec<_>>()
                        .join(" ")
                ),
            };
            out.push_str(&format!("proof {text}\n"));
        }
        out.push_str(&format!(
            "goal {} {}\nroot {}\nEND-DLM-KERNEL-PROOF\n",
            self.goal.0, self.goal.1, self.root
        ));
        out
    }

    /// Strict single-artifact parser. No source code, certificate traces or
    /// declared axioms are executed or implicitly trusted.
    pub fn from_text(text: &str, budget: KernelBudget) -> Result<Self, KernelError> {
        if text.len() > budget.max_text_bytes {
            return Err(exhausted("proof text exceeds the byte budget"));
        }
        let mut lines = text.lines().filter(|l| !l.trim().is_empty());
        if lines.next() != Some("DLM-KERNEL-PROOF v1") {
            return Err(invalid("unsupported kernel proof format/version"));
        }
        if lines.next() != Some("rules nat-semiring-v1") {
            return Err(invalid("unsupported logical rules/version"));
        }
        let mut proof = Self {
            variables: Vec::new(),
            terms: Vec::new(),
            steps: Vec::new(),
            goal: (0, 0),
            root: 0,
        };
        let mut phase = 0;
        let number = |s: &str| s.parse::<usize>().map_err(|_| invalid("invalid proof ID"));
        let mut ended = false;
        for line in lines.by_ref() {
            let parts: Vec<_> = line.split_whitespace().collect();
            match parts.as_slice() {
                ["var", name] if phase == 0 => proof.variables.push((*name).to_string()),
                ["term", op, args @ ..] if phase <= 1 => {
                    phase = 1;
                    let term = match (*op, args) {
                        ("var", [id]) => KernelTerm::Var(number(id)?),
                        ("nat", [value]) => KernelTerm::Nat(
                            value.parse().map_err(|_| invalid("invalid Nat literal"))?,
                        ),
                        ("add", [a, b]) => KernelTerm::Add(number(a)?, number(b)?),
                        ("mul", [a, b]) => KernelTerm::Mul(number(a)?, number(b)?),
                        _ => return Err(invalid("invalid term constructor/arity")),
                    };
                    proof.terms.push(term);
                }
                ["proof", op, args @ ..] if phase <= 2 => {
                    phase = 2;
                    let step = match (*op, args) {
                        ("refl", [id]) => KernelRule::Refl(number(id)?),
                        ("sym", [id]) => KernelRule::Sym(number(id)?),
                        ("trans", [a, b]) => KernelRule::Trans(number(a)?, number(b)?),
                        ("add", [a, b]) => KernelRule::Add(number(a)?, number(b)?),
                        ("mul", [a, b]) => KernelRule::Mul(number(a)?, number(b)?),
                        ("eval", [id]) => KernelRule::Eval(number(id)?),
                        ("axiom", [id]) => KernelRule::Axiom(number(id)?),
                        ("law", [name, terms @ ..]) => {
                            let law = LAWS
                                .iter()
                                .find(|(_, n)| n == name)
                                .map(|(l, _)| *l)
                                .ok_or_else(|| invalid("unknown algebra law"))?;
                            if terms.len() != law.arity() {
                                return Err(invalid("algebra law has the wrong arity"));
                            }
                            KernelRule::Law(
                                law,
                                terms
                                    .iter()
                                    .map(|s| number(s))
                                    .collect::<Result<Vec<_>, _>>()?,
                            )
                        }
                        _ => return Err(invalid("invalid proof rule/arity")),
                    };
                    proof.steps.push(step);
                }
                ["goal", a, b] if phase <= 2 => {
                    phase = 3;
                    proof.goal = (number(a)?, number(b)?);
                }
                ["root", p] if phase == 3 => {
                    phase = 4;
                    proof.root = number(p)?;
                }
                ["END-DLM-KERNEL-PROOF"] if phase == 4 => {
                    ended = true;
                    break;
                }
                _ => return Err(invalid("unexpected, duplicate or out-of-order proof field")),
            }
            if proof.terms.len() > budget.max_nodes
                || proof.steps.len() > budget.max_steps
                || proof.variables.len() > 1024
            {
                return Err(exhausted("proof parser size budget exhausted"));
            }
        }
        if !ended || lines.next().is_some() {
            return Err(invalid("missing proof terminator or trailing data"));
        }
        // v0.101 (C-min): one artifact has one spelling. Blank lines, other
        // whitespace, CR line ends, `+` signs and leading zeros are refused,
        // so the text (and anything hashed from it) identifies the proof.
        if proof.to_text() != text {
            return Err(invalid("non-canonical kernel proof text"));
        }
        Ok(proof)
    }

    /// Source proposition bound to a proof-certificate identity. Rendering is
    /// iterative and bounded, including expansion of shared DAG subterms.
    pub fn proposition(&self, budget: KernelBudget) -> Result<String, KernelError> {
        if self.terms.len() > budget.max_nodes {
            return Err(exhausted("proposition term budget exhausted"));
        }
        let mut rendered: Vec<String> = Vec::new();
        let mut total: usize = 0;
        for term in &self.terms {
            let text = match term {
                KernelTerm::Var(id) => self
                    .variables
                    .get(*id)
                    .cloned()
                    .ok_or_else(|| invalid("unknown variable"))?,
                KernelTerm::Nat(n) => n.to_string(),
                KernelTerm::Add(a, b) | KernelTerm::Mul(a, b) => {
                    let a = rendered
                        .get(*a)
                        .ok_or_else(|| invalid("forward term reference"))?;
                    let b = rendered
                        .get(*b)
                        .ok_or_else(|| invalid("forward term reference"))?;
                    if a.len().saturating_add(b.len()).saturating_add(8) > budget.max_text_bytes {
                        return Err(exhausted("proposition expansion budget exhausted"));
                    }
                    if matches!(term, KernelTerm::Add(..)) {
                        format!("({a} + {b})")
                    } else {
                        format!("mul({a}, {b})")
                    }
                }
            };
            total = total.saturating_add(text.len());
            if total > budget.max_text_bytes {
                return Err(exhausted("proposition text budget exhausted"));
            }
            rendered.push(text);
        }
        let a = rendered
            .get(self.goal.0)
            .ok_or_else(|| invalid("unknown goal term"))?;
        let b = rendered
            .get(self.goal.1)
            .ok_or_else(|| invalid("unknown goal term"))?;
        let vars = self
            .variables
            .iter()
            .map(|n| format!("{n}: Nat"))
            .collect::<Vec<_>>()
            .join(", ");
        let out = if vars.is_empty() {
            format!("nat_eq({a}, {b})")
        } else {
            format!("forall {vars}. nat_eq({a}, {b})")
        };
        if out.len() > budget.max_text_bytes {
            return Err(exhausted("proposition text budget exhausted"));
        }
        Ok(out)
    }
}
