use super::vm::Value;
use std::sync::{
    atomic::{AtomicU64, Ordering},
    Arc,
};

#[derive(Clone, Copy, Debug)]
enum Format {
    Bool,
    Unsigned,
    F32,
}

#[derive(Clone, Debug)]
pub struct AtomicValue {
    format: Format,
    raw: Arc<AtomicU64>,
}

impl AtomicValue {
    pub(super) fn new(value: &Value) -> Result<Self, String> {
        let format = match value {
            Value::Bool(_) => Format::Bool,
            Value::U(_) => Format::Unsigned,
            Value::F32(_) => Format::F32,
            _ => return Err("invalid atomic initializer".into()),
        };
        Ok(Self {
            format,
            raw: Arc::new(AtomicU64::new(Self::bits(value)?)),
        })
    }

    fn bits(value: &Value) -> Result<u64, String> {
        match value {
            Value::Bool(value) => Ok(u64::from(*value)),
            Value::U(value) => Ok(*value),
            Value::F32(value) => Ok(u64::from(*value)),
            _ => Err("invalid atomic operand".into()),
        }
    }

    fn value(&self, bits: u64) -> Value {
        match self.format {
            Format::Bool => Value::Bool(bits != 0),
            Format::Unsigned => Value::U(bits),
            Format::F32 => Value::F32(bits as u32),
        }
    }

    pub(super) fn ordering_literal(name: &str) -> Option<u64> {
        Some(match name {
            "Relaxed" => 0,
            "Acquire" => 1,
            "Release" => 2,
            "AcqRel" => 3,
            "SeqCst" => 4,
            _ => return None,
        })
    }

    fn order(value: &Value) -> Result<Ordering, String> {
        Ok(match value.unsigned()? {
            0 => Ordering::Relaxed,
            1 => Ordering::Acquire,
            2 => Ordering::Release,
            3 => Ordering::AcqRel,
            4 => Ordering::SeqCst,
            _ => return Err("invalid atomic ordering".into()),
        })
    }

    pub(super) fn call(&self, name: &str, values: &[Value]) -> Result<Value, String> {
        Ok(match name {
            "atomic_load" => self.value(self.raw.load(Self::order(&values[0])?)),
            "atomic_store" => {
                self.raw
                    .store(Self::bits(&values[0])?, Self::order(&values[1])?);
                Value::Unit
            }
            "atomic_exchange" => self.value(
                self.raw
                    .swap(Self::bits(&values[0])?, Self::order(&values[1])?),
            ),
            "atomic_compare_exchange" => Value::Bool(
                self.raw
                    .compare_exchange(
                        Self::bits(&values[0])?,
                        Self::bits(&values[1])?,
                        Self::order(&values[2])?,
                        Self::order(&values[3])?,
                    )
                    .is_ok(),
            ),
            "atomic_is_lock_free" => Value::Bool(true),
            _ => return Err("unknown atomic operation".into()),
        })
    }
}
