//! Local dominance facts for parser-generated partial projections. Facts are
//! tied to immutable slots/globals, never to textual expression equality.
use super::Verifier;
use crate::compile::{IrExpr, IrOp};
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub(super) enum Subject {
    Local(usize),
    Global(usize),
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub(super) enum Variant {
    NonEmpty,
    Some,
    Ok,
    Err,
}
pub(super) type Fact = (Subject, Variant);
fn subject(node: &IrExpr) -> Option<Subject> {
    match node.op {
        IrOp::Local(slot) => Some(Subject::Local(slot)),
        IrOp::Global(id) => Some(Subject::Global(id)),
        _ => None,
    }
}
fn facts(condition: &IrExpr, truth: bool, out: &mut Vec<Fact>) {
    match &condition.op {
        IrOp::Builtin { name, args } if args.len() == 1 => {
            if name == "not" {
                facts(&args[0], !truth, out);
                return;
            }
            let variant = match (name.as_str(), truth) {
                ("is_empty", false) => Variant::NonEmpty,
                ("is_some", true) | ("is_none", false) => Variant::Some,
                ("is_ok", true) | ("is_err", false) => Variant::Ok,
                ("is_err", true) | ("is_ok", false) => Variant::Err,
                _ => return,
            };
            if let Some(s) = subject(&args[0]) {
                out.push((s, variant));
            }
        }
        IrOp::And(a, b) if truth => {
            facts(a, true, out);
            facts(b, true, out);
        }
        IrOp::Or(a, b) if !truth => {
            facts(a, false, out);
            facts(b, false, out);
        }
        _ => {}
    }
}
impl Verifier<'_> {
    pub(super) fn branch(&mut self, node: &IrExpr, condition: &IrExpr, truth: bool) {
        let before = self.guards.clone();
        let mut known = Vec::new();
        facts(condition, truth, &mut known);
        self.guards.extend(known);
        self.expr(node);
        self.guards = before;
    }
    pub(super) fn projection_guard(&mut self, name: &str, args: &[IrExpr]) {
        let required = match name {
            "__match_head" => Variant::NonEmpty,
            "__match_some" => Variant::Some,
            "__match_ok" => Variant::Ok,
            "__match_err" => Variant::Err,
            _ => return,
        };
        if args.len() != 1 {
            return;
        } // Arity is reported by the builtin verifier.
        if !subject(&args[0]).is_some_and(|s| self.guards.contains(&(s, required))) {
            self.issue(format!(
                "{name} requires a dominating {required:?} guard for the same slot/global"
            ));
        }
    }
}
