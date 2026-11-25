use super::{check, emit, Function, Layout, Mir, Record, Ty};
use crate::TypeId;
use std::collections::{BTreeMap, BTreeSet};
use std::fmt::{self, Write};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct LayoutId(pub u32);

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct LayoutVersion(pub u32);

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct LayoutFingerprint(String);

impl LayoutFingerprint {
    fn of(schema: &str) -> Self {
        Self(crate::spectral::sha256(schema.as_bytes()))
    }
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for LayoutFingerprint {
    fn fmt(&self, out: &mut fmt::Formatter<'_>) -> fmt::Result {
        out.write_str(&self.0)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AbiProfile {
    SystemV64,
    Win64,
}

impl AbiProfile {
    pub fn name(self) -> &'static str {
        match self {
            Self::SystemV64 => "c-abi-v1:x86_64-sysv",
            Self::Win64 => "c-abi-v1:x86_64-win64",
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LayoutContract {
    pub ty: Ty,
    pub version: LayoutVersion,
    pub size: usize,
    pub alignment: usize,
    pub field_offsets: Vec<usize>,
    pub element_stride: Option<usize>,
    pub send: bool,
    pub sync: bool,
    pub owned: bool,
    pub c_abi: bool,
    pub fingerprint: LayoutFingerprint,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TypeLayout {
    pub type_id: TypeId,
    pub layout_id: LayoutId,
    pub contract: LayoutContract,
}

impl Mir {
    pub fn layout_contract(&self, ty: &Ty) -> Result<LayoutContract, String> {
        check::validate_ty(ty, &self.unit.records, 1).map_err(|error| error.message)?;
        describe(ty, &self.unit.records, &mut BTreeMap::new())
    }

    pub fn type_layouts(&self) -> Result<Vec<TypeLayout>, String> {
        let mut layouts = BTreeMap::new();
        emit::all_types(self)
            .into_iter()
            .enumerate()
            .map(|(index, ty)| {
                let id = u32::try_from(index).map_err(|_| "type layout table exceeds u32")?;
                Ok(TypeLayout {
                    type_id: TypeId::new(id),
                    layout_id: LayoutId(id),
                    contract: describe(&ty, &self.unit.records, &mut layouts)?,
                })
            })
            .collect()
    }
}

fn describe(
    ty: &Ty,
    records: &[Record],
    layouts: &mut BTreeMap<String, Layout>,
) -> Result<LayoutContract, String> {
    let metrics = |t: &Ty, layouts: &mut BTreeMap<_, _>| {
        check::layout_ty(t, records, layouts, &mut BTreeSet::new())
    };
    let (size, alignment) = metrics(ty, layouts)?;
    let (version, field_offsets) = if let Ty::Record(name) = ty {
        let l = &layouts[name];
        (LayoutVersion(l.version), l.offsets.clone())
    } else {
        (LayoutVersion(1), Vec::new())
    };
    let element_stride = match ty {
        Ty::Array(t, _) | Ty::Buffer(t) | Ty::Slice(t, _) | Ty::ForeignBuffer(t) | Ty::Split(t) => {
            Some(metrics(t, layouts)?.0)
        }
        Ty::Simd(s, _) => Some(s.bits() as usize / 8),
        _ => None,
    };
    let stride = element_stride.unwrap_or(0);
    let mut schema = format!("dlm/layout/v1;x86_64-le;{size};{alignment};{stride};");
    let mut required = BTreeSet::new();
    encode_ty(ty, &mut schema, &mut required);
    let mut encoded = BTreeSet::new();
    while let Some(name) = required.pop_first() {
        if !encoded.insert(name.clone()) {
            continue;
        }
        let record = records
            .iter()
            .find(|r| r.name == name)
            .ok_or_else(|| format!("unknown native record {name}"))?;
        let (size, align) = metrics(&Ty::Record(name.clone()), layouts)?;
        let layout = &layouts[&name];
        text(&mut schema, &name);
        write!(
            schema,
            ";{};{size};{align};{};{};{};",
            layout.version,
            u8::from(record.union),
            u8::from(record.packed),
            record.fields.len()
        )
        .unwrap();
        for ((field, ty), offset) in record.fields.iter().zip(&layout.offsets) {
            text(&mut schema, field);
            write!(schema, "{offset};").unwrap();
            encode_ty(ty, &mut schema, &mut required);
        }
    }
    Ok(LayoutContract {
        ty: ty.clone(),
        version,
        size,
        alignment,
        field_offsets,
        element_stride,
        send: check::sendable(ty, records),
        sync: check::sync_readable(ty, records),
        owned: check::owned(ty, records),
        c_abi: check::abi(ty, records),
        fingerprint: LayoutFingerprint::of(&schema),
    })
}

fn text(out: &mut String, s: &str) {
    write!(out, "{}:{s}", s.len()).unwrap();
}

fn encode_ty(ty: &Ty, out: &mut String, records: &mut BTreeSet<String>) {
    let (tag, element) = match ty {
        Ty::Record(n) => {
            out.push_str("record;");
            text(out, n);
            records.insert(n.clone());
            return;
        }
        Ty::Array(t, n) => {
            write!(out, "array;{n};").unwrap();
            encode_ty(t, out, records);
            return;
        }
        Ty::Resource(t, f) => {
            out.push_str("resource;");
            text(out, f);
            encode_ty(t, out, records);
            return;
        }
        Ty::FnPtr(params, ret) => {
            write!(out, "function-pointer;{};", params.len()).unwrap();
            for t in params {
                encode_ty(t, out, records);
            }
            encode_ty(ret, out, records);
            return;
        }
        Ty::Task(t) => ("task", t),
        Ty::Future(t) => ("future", t),
        Ty::Mutex(t) => ("mutex", t),
        Ty::RwLock(t) => ("rwlock", t),
        Ty::Sender(t) => ("sender", t),
        Ty::Receiver(t) => ("receiver", t),
        Ty::ThreadBound(t) => ("thread-bound", t),
        Ty::Buffer(t) => ("buffer", t),
        Ty::Boxed(t) => ("box", t),
        Ty::Slice(t, false) => ("slice", t),
        Ty::Slice(t, true) => ("mutable-slice", t),
        Ty::Split(t) => ("split", t),
        Ty::Ref(t, false) => ("ref", t),
        Ty::Ref(t, true) => ("mutable-ref", t),
        Ty::Ptr(t) => ("pointer", t),
        Ty::NonNull(t) => ("nonnull", t),
        Ty::BorrowedPtr(t) => ("borrowed-pointer", t),
        Ty::ForeignBuffer(t) => ("foreign-buffer", t),
        Ty::Atomic(t) => ("atomic", t),
        Ty::Scalar(_)
        | Ty::Simd(..)
        | Ty::Bool
        | Ty::Unit
        | Ty::Str
        | Ty::Window
        | Ty::Executor
        | Ty::DbPool
        | Ty::Handle => {
            out.push_str("value;");
            text(out, &ty.name());
            return;
        }
    };
    write!(out, "{tag};").unwrap();
    encode_ty(element, out, records);
}

pub fn export_fingerprint(
    m: &Mir,
    f: &Function,
    profile: AbiProfile,
) -> Result<LayoutFingerprint, String> {
    let mut schema = String::from("dlm/export/v1;");
    text(&mut schema, profile.name());
    text(
        &mut schema,
        f.export.as_deref().ok_or("function is not exported")?,
    );
    write!(schema, ";{};{};", f.params.len(), u8::from(f.unsafe_)).unwrap();
    let mut layouts = BTreeMap::new();
    for t in f
        .params
        .iter()
        .map(|(_, t)| t)
        .chain(std::iter::once(&f.ret))
    {
        text(
            &mut schema,
            describe(t, &m.unit.records, &mut layouts)?
                .fingerprint
                .as_str(),
        );
    }
    write!(schema, "{};", f.guarantees.len()).unwrap();
    for effect in &f.guarantees {
        text(&mut schema, effect);
    }
    Ok(LayoutFingerprint::of(&schema))
}

pub(super) const HEADER: &str = "const char *dlm_native_abi_profile(void);\nconst char *dlm_native_record_fingerprint(const char *name);\nconst char *dlm_native_export_fingerprint(const char *name);\n";

pub(super) fn c_exports(m: &Mir, out: &mut String) {
    out.push_str("#if defined(_WIN32)\n");
    for (profile, branch_end) in [
        (AbiProfile::Win64, "#else\n"),
        (AbiProfile::SystemV64, "#endif\n"),
    ] {
        writeln!(
            out,
            "DN_EXPORT const char *dlm_native_abi_profile(void){{return \"{}\";}}",
            profile.name()
        )
        .unwrap();
        out.push_str("DN_EXPORT const char *dlm_native_export_fingerprint(const char *name){\n if (!name) return NULL;\n");
        for f in &m.unit.functions {
            let Some(name) = &f.export else {
                continue;
            };
            let fp = export_fingerprint(m, f, profile).expect("checked native export");
            writeln!(out, " if (!strcmp(name,\"{name}\")) return \"{fp}\";").unwrap();
        }
        out.push_str(" return NULL;\n}\n");
        out.push_str(branch_end);
    }
    out.push_str("DN_EXPORT const char *dlm_native_record_fingerprint(const char *name){\n if (!name) return NULL;\n");
    for name in m.layouts.keys() {
        let fp = m
            .layout_contract(&Ty::Record(name.clone()))
            .expect("checked native layout")
            .fingerprint;
        writeln!(out, " if (!strcmp(name,\"{name}\")) return \"{fp}\";").unwrap();
    }
    out.push_str(" return NULL;\n}\n");
}
