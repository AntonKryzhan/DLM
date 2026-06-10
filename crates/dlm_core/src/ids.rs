use std::fmt;

macro_rules! define_id {
    ($name:ident, $prefix:literal) => {
        #[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
        pub struct $name(pub u32);

        impl $name {
            pub const fn new(raw: u32) -> Self {
                Self(raw)
            }

            pub const fn raw(self) -> u32 {
                self.0
            }
        }

        impl fmt::Display for $name {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(f, "{}{}", $prefix, self.0)
            }
        }
    };
}

define_id!(FileId, "file#");
define_id!(ModuleId, "module#");
define_id!(TheoryId, "theory#");
define_id!(ValueId, "value#");
define_id!(TypeId, "type#");
define_id!(BridgeId, "bridge#");
define_id!(ProofId, "proof#");

impl FileId {
    pub const ROOT: Self = Self(0);
}

/// Monotonic ID allocator used by the v0.34 resolver skeleton.
///
/// IDs are process-local compiler/checker identifiers. They are intentionally
/// not stable serialization IDs and must not be used as source-level names.
#[derive(Debug, Clone, Default)]
pub struct IdAllocator {
    next_file: u32,
    next_module: u32,
    next_theory: u32,
    next_value: u32,
    next_type: u32,
    next_bridge: u32,
    next_proof: u32,
}

impl IdAllocator {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn alloc_file(&mut self) -> FileId {
        FileId(take_next(&mut self.next_file))
    }

    pub fn alloc_module(&mut self) -> ModuleId {
        ModuleId(take_next(&mut self.next_module))
    }

    pub fn alloc_theory(&mut self) -> TheoryId {
        TheoryId(take_next(&mut self.next_theory))
    }

    pub fn alloc_value(&mut self) -> ValueId {
        ValueId(take_next(&mut self.next_value))
    }

    pub fn alloc_type(&mut self) -> TypeId {
        TypeId(take_next(&mut self.next_type))
    }

    pub fn alloc_bridge(&mut self) -> BridgeId {
        BridgeId(take_next(&mut self.next_bridge))
    }

    pub fn alloc_proof(&mut self) -> ProofId {
        ProofId(take_next(&mut self.next_proof))
    }
}

fn take_next(next: &mut u32) -> u32 {
    let id = *next;
    *next = next.checked_add(1).expect("DLM compiler ID space exhausted");
    id
}
