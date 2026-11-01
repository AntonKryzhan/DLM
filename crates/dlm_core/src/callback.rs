//! Explicit upper bounds for callable effects and partiality.
use crate::function_contract::{FunctionEffect, FunctionEffectKind};
use crate::ordinary::OrdinaryType;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EffectBounds {
    names: Vec<String>,
}

const EFFECTS: &[(&str, FunctionEffectKind, &str)] = &[
    ("Stdout", FunctionEffectKind::Io, "stdout"),
    ("Stdin", FunctionEffectKind::Io, "stdin"),
    ("Args", FunctionEffectKind::Io, "args"),
    ("File", FunctionEffectKind::Io, "file"),
    ("Process", FunctionEffectKind::Io, "process"),
    ("Net", FunctionEffectKind::Io, "net"),
    ("Window", FunctionEffectKind::Io, "window"),
    ("ByteBuffer", FunctionEffectKind::Runtime, "byte_buffer"),
    ("TextBuffer", FunctionEffectKind::Runtime, "text_buffer"),
    ("Require", FunctionEffectKind::Runtime, "require"),
    ("EnumPayload", FunctionEffectKind::Runtime, "enum_payload"),
    ("GPU", FunctionEffectKind::GpuExecution, "gpu"),
    ("Remote", FunctionEffectKind::RemoteExecution, "remote"),
];

impl EffectBounds {
    pub fn parse(text: &str) -> Result<Self, String> {
        let mut names = Vec::new();
        for name in text.split('+').map(str::trim) {
            if name != "Pure" && name != "Partial" && !EFFECTS.iter().any(|(n, _, _)| *n == name) {
                return Err(format!("unknown callback effect bound `{name}`"));
            }
            if names.iter().any(|n| n == name) {
                return Err(format!("duplicate callback effect bound `{name}`"));
            }
            names.push(name.to_string());
        }
        if names.len() > 1 && names.iter().any(|n| n == "Pure") {
            return Err("Pure cannot be combined with another callback bound".to_string());
        }
        names.sort();
        Ok(Self { names })
    }

    pub fn canonical(&self) -> String {
        self.names.join("+")
    }
    pub fn partial(&self) -> bool {
        self.names.iter().any(|n| n == "Partial")
    }
    pub fn effects(&self) -> Vec<FunctionEffect> {
        self.names
            .iter()
            .filter_map(|name| {
                EFFECTS
                    .iter()
                    .find(|(n, _, _)| n == name)
                    .map(|(_, kind, boundary)| FunctionEffect {
                        kind: *kind,
                        boundary: (*boundary).to_string(),
                    })
            })
            .collect()
    }
    pub fn effect_names(&self) -> Vec<String> {
        self.effects().iter().map(ToString::to_string).collect()
    }
    pub fn allows(&self, actual: &[String], partial: bool) -> Result<(), String> {
        if partial && !self.partial() {
            return Err("callback requires the Partial bound".to_string());
        }
        let allowed = self.effect_names();
        if let Some(effect) = actual.iter().find(|e| !allowed.contains(e)) {
            return Err(format!(
                "callback effect `{effect}` is outside [{}]",
                self.canonical()
            ));
        }
        Ok(())
    }
    pub fn from_effects(effects: &[FunctionEffect], partial: bool) -> Result<Self, String> {
        let mut names = Vec::new();
        for effect in effects {
            let name = EFFECTS
                .iter()
                .find(|(_, k, b)| *k == effect.kind && *b == effect.boundary)
                .ok_or_else(|| format!("unsupported callback effect `{effect}`"))?
                .0;
            if !names.contains(&name) {
                names.push(name);
            }
        }
        if partial {
            names.push("Partial");
        }
        if names.is_empty() {
            names.push("Pure");
        }
        Self::parse(&names.join("+"))
    }
}

pub fn widening(source: &OrdinaryType, target: &OrdinaryType) -> Result<(), String> {
    let OrdinaryType::EffectFn(params, ret, bounds) = target else {
        return Err("effect_fn expects a FnEffect target type".to_string());
    };
    let (actual_params, actual_ret, effects, partial) = match source {
        OrdinaryType::Fn(p, r) => (p, r, Vec::new(), false),
        OrdinaryType::EffectFn(p, r, b) => (p, r, b.effect_names(), b.partial()),
        _ => return Err("effect_fn expects a callable value".to_string()),
    };
    if params != actual_params || ret != actual_ret {
        return Err("effect_fn parameter and result types must match exactly".to_string());
    }
    bounds.allows(&effects, partial)
}

pub(crate) fn traversal_argument(name: &str) -> Option<usize> {
    match name {
        "map_effect" | "each_effect" | "filter_effect" | "find_effect" | "any_effect"
        | "all_effect" | "try_map_effect" | "try_each_effect" => Some(1),
        "fold_effect" => Some(2),
        _ => None,
    }
}
