//! v0.73 — Ordinary mathematics surface execution.
//!
//! Executes the operations typed by `checker::ordinary_surface`. Functions are
//! non-recursive (the checker rejects recursion) and every traversal is
//! bounded by the collection length, so evaluation always terminates. A call
//! depth guard is still kept as a defensive runtime boundary.

use std::collections::BTreeMap;
use std::sync::Arc;

use super::{Runtime, RuntimeValue};
use crate::ast::*;
use crate::diagnostics::{Diagnostic, DiagnosticKind};

/// Defensive bound on the total depth of nested user-function calls. Each
/// recursive function is additionally bounded by its own recursion fuel.
pub const MAX_CALL_DEPTH: usize = 100_000;

pub(crate) type RuntimeFunctionTable = BTreeMap<String, BTreeMap<String, Arc<FnDecl>>>;

/// v0.102: largest `ByteBuf` length (bytes), the same in the native runtime.
pub const BYTE_BUFFER_MAX: usize = 1 << 28;

fn runtime_error(line: usize, message: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::RuntimeError, Some(line), message)
}

fn arity(name: &str, args: &[Expr], expected: usize, line: usize) -> Result<(), Diagnostic> {
    if args.len() == expected {
        Ok(())
    } else {
        Err(runtime_error(line, format!("{name} expects {expected} argument(s), got {}", args.len())))
    }
}

fn expect_nat(name: &str, value: RuntimeValue, line: usize) -> Result<u128, Diagnostic> {
    match value {
        RuntimeValue::NatExact(n) => Ok(n),
        other => Err(runtime_error(line, format!("{name} can execute only exact Nat values"))
            .with_help(format!("runtime value was: {}", other.display_atom()))),
    }
}

fn expect_bool(name: &str, value: RuntimeValue, line: usize) -> Result<bool, Diagnostic> {
    match value {
        RuntimeValue::Bool(b) => Ok(b),
        other => Err(runtime_error(line, format!("{name} expects a Bool value"))
            .with_help(format!("runtime value was: {}", other.display_atom()))),
    }
}

fn expect_collection(name: &str, value: RuntimeValue, line: usize) -> Result<(bool, Vec<RuntimeValue>), Diagnostic> {
    match value {
        RuntimeValue::List(items) => Ok((false, items)),
        RuntimeValue::Sequence(items) => Ok((true, items)),
        other => Err(runtime_error(line, format!("{name} expects a List or Sequence value"))
            .with_help(format!("runtime value was: {}", other.display_atom()))),
    }
}

enum TailStep {
    Again(Vec<RuntimeValue>),
    Done(RuntimeValue),
}

fn measure_size(value: &RuntimeValue) -> Option<u128> {
    match value {
        RuntimeValue::NatExact(n) => Some(*n),
        RuntimeValue::List(items) | RuntimeValue::Sequence(items) => Some(items.len() as u128),
        _ => None,
    }
}

fn rebuild(is_sequence: bool, items: Vec<RuntimeValue>) -> RuntimeValue {
    if is_sequence {
        RuntimeValue::Sequence(items)
    } else {
        RuntimeValue::List(items)
    }
}

/// v0.109: state of one file handle; the native runtime keeps the same state
/// (`dv_file`). Writes go straight to the file, so the contents seen by a later
/// open are the same whenever the handle is closed.
#[derive(Default)]
pub(crate) struct FileSlot {
    path: String,
    mode: u8,
    file: Option<std::fs::File>,
    error: Option<String>,
    written: u128,
    pending: Vec<u8>,
    eof: bool,
    /// v0.125: `file_set_buffered`: writes collect in `outbuf` until a flush.
    buffered: bool,
    outbuf: Vec<u8>,
    closed: bool,
}

impl Drop for FileSlot {
    fn drop(&mut self) {
        self.flush_out();
    }
}

/// v0.125: `cannot <what> (os error N)`, the same text as the native `dvz_fail`.
fn os_fail(what: String, err: &std::io::Error) -> String {
    format!("{what} (os error {})", err.raw_os_error().unwrap_or(0))
}

fn fs_walk(root: &std::path::Path, rel: &str, out: &mut Vec<String>) -> Result<(), String> {
    let dir = if rel.is_empty() { root.to_path_buf() } else { root.join(rel) };
    let mut names = Vec::new();
    for entry in std::fs::read_dir(&dir).map_err(|e| os_fail(format!("cannot list directory `{}`", dir.display()), &e))? {
        let entry = entry.map_err(|e| os_fail(format!("cannot list directory `{}`", dir.display()), &e))?;
        if let Ok(name) = entry.file_name().into_string() {
            names.push(name);
        }
    }
    names.sort();
    for name in names {
        let path = if rel.is_empty() { name.clone() } else { format!("{rel}/{name}") };
        let full = root.join(&path);
        let meta = std::fs::symlink_metadata(&full).map_err(|e| os_fail(format!("cannot stat `{}`", full.display()), &e))?;
        if meta.file_type().is_dir() {
            fs_walk(root, &path, out)?;
        } else if meta.file_type().is_file() {
            out.push(path);
        }
    }
    Ok(())
}

impl FileSlot {
    fn fail(&mut self, message: String) {
        if self.error.is_none() {
            self.error = Some(message);
        }
    }
    fn readable(&mut self) -> bool {
        if self.error.is_some() {
            return false;
        }
        if self.mode != b'r' {
            self.fail(format!("file `{}` is not open for reading", self.path));
            return false;
        }
        true
    }
    fn writable(&mut self) -> bool {
        if self.error.is_some() {
            return false;
        }
        if self.mode != b'w' && self.mode != b'a' {
            self.fail(format!("file `{}` is not open for writing", self.path));
            return false;
        }
        true
    }
    /// Reads more bytes into `pending`; `false` at the end or on failure.
    fn fill(&mut self) -> bool {
        use std::io::Read;
        if self.eof {
            return false;
        }
        let mut chunk = [0u8; 65536];
        match self.file.as_mut().map(|f| f.read(&mut chunk)) {
            Some(Ok(0)) => {
                self.eof = true;
                false
            }
            Some(Ok(n)) => {
                self.pending.extend_from_slice(&chunk[..n]);
                true
            }
            _ => {
                self.fail(format!("cannot read file `{}`", self.path));
                self.eof = true;
                false
            }
        }
    }
    /// Writes `outbuf`; failures are sticky like any other write failure.
    fn flush_out(&mut self) -> bool {
        use std::io::Write;
        if self.outbuf.is_empty() {
            return self.error.is_none();
        }
        let data = std::mem::take(&mut self.outbuf);
        if self.error.is_some() {
            return false;
        }
        match self.file.as_mut().map(|f| f.write_all(&data).and_then(|_| f.flush())) {
            Some(Ok(())) => true,
            _ => {
                self.fail(format!("cannot write file `{}`", self.path));
                false
            }
        }
    }
    fn write(&mut self, bytes: &[u8]) -> bool {
        use std::io::Write;
        if !self.writable() {
            return false;
        }
        if self.buffered {
            self.outbuf.extend_from_slice(bytes);
            self.written += bytes.len() as u128;
            if self.outbuf.len() >= 65536 {
                return self.flush_out();
            }
            return true;
        }
        match self.file.as_mut().map(|f| f.write_all(bytes).and_then(|_| f.flush())) {
            Some(Ok(())) => {
                self.written += bytes.len() as u128;
                true
            }
            _ => {
                self.fail(format!("cannot write file `{}`", self.path));
                false
            }
        }
    }
    fn text(&mut self, bytes: Vec<u8>) -> Option<String> {
        match String::from_utf8(bytes) {
            Ok(t) => Some(t),
            Err(_) => {
                self.fail(format!("file `{}` is not valid UTF-8", self.path));
                None
            }
        }
    }
}

impl Runtime {
    pub(super) fn eval_ordinary_call(
        &mut self,
        name: &str,
        args: &[Expr],
        theory: &str,
        line: usize,
    ) -> Option<Result<RuntimeValue, Diagnostic>> {
        if !crate::checker::ORDINARY_BUILTINS.contains(&name) {
            return None;
        }
        Some(self.eval_ordinary_call_inner(name, args, theory, line))
    }

    /// Dispatch only; each group lives in its own small function so a deep
    /// recursion pays a small stack frame per level (v0.74).
    fn eval_ordinary_call_inner(&mut self, name: &str, args: &[Expr], theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        match name {
            "true" | "false" | "not" | "and" | "or" | "nat_eq" | "mul" | "monus" | "pred" => self.eval_ordinary_scalar(name, args, theory, line),
            "equal" | "less" => {
                arity(name, args, 2, line)?;
                let a = self.eval_expr(&args[0], theory)?;
                let b = self.eval_expr(&args[1], theory)?;
                Ok(RuntimeValue::Bool(if name == "equal" {
                    a == b
                } else {
                    match (&a, &b) {
                        (RuntimeValue::NatExact(x), RuntimeValue::NatExact(y)) => x < y,
                        (RuntimeValue::IntExact(x), RuntimeValue::IntExact(y)) => x < y,
                        (RuntimeValue::Text(x), RuntimeValue::Text(y)) => x.as_bytes() < y.as_bytes(),
                        _ => return Err(runtime_error(line, "less expects Nat, Int or Text")),
                    }
                }))
            }
            "if_then_else" => self.eval_ordinary_control(name, args, theory, line),
            "effect_fn" | "some" | "none" | "ok" | "err" | "is_some" | "is_none" | "is_ok" | "is_err" | "unwrap_or" | "__match_ok" | "__match_err" => self.eval_ordinary_partial(name, args, theory, line),
            "list" | "sequence" | "empty_list" | "empty_sequence" | "length" | "index" | "head" | "tail" | "is_empty" | "cons" | "__match_head" | "__match_some" | "__match_variant" | "__enum_payload" => self.eval_ordinary_collections(name, args, theory, line),
            "pair" | "fst" | "snd" => self.eval_ordinary_product(name, args, theory, line),
            "map" | "fold" => self.eval_ordinary_traversal(name, args, theory, line),
            effect if crate::callback::traversal_argument(effect).is_some() => self.eval_effect_traversal(name,args,theory,line),
            "inl" | "inr" | "is_left" | "case" | "field" => self.eval_ordinary_structural(name, args, theory, line),
            text if crate::checker::TEXT_BUILTINS.contains(&text) => self.eval_ordinary_text(name, args, theory, line),
            int if crate::checker::INT_BUILTINS.contains(&int) => self.eval_ordinary_int(name, args, theory, line),
            buf if buf.starts_with("buf_") || buf.starts_with("tbuf_") => self.eval_ordinary_buffer(name, args, theory, line),
            dense if dense.starts_with("bn_") => self.eval_ordinary_dense(name, args, theory, line),
            float if float.starts_with("fl_") || float.starts_with("fv_") => self.eval_ordinary_float(name, args, theory, line),
            graph if graph.starts_with("nv_") || graph.starts_with("gr_") || graph.starts_with("la_") || graph.starts_with("fft_") || graph.starts_with("lp_") || graph.starts_with("nt_") || graph.starts_with("pf_") || graph.starts_with("pm_") || graph.starts_with("sat_") || graph.starts_with("tp_") || graph.starts_with("gb_") || graph.starts_with("mv_") || graph.starts_with("ilp_") || graph.starts_with("km_") || graph.starts_with("tm_") || graph.starts_with("geo_") || graph.starts_with("kd_") || graph.starts_with("fft2_") || graph.starts_with("sig_") || graph.starts_with("bvh_") || graph.starts_with("gx_") || graph.starts_with("vx_") => self.eval_ordinary_graph(name, args, theory, line),
            file if file.starts_with("file_") || file.starts_with("proc_") || file.starts_with("net_") || file.starts_with("win_") => self.eval_ordinary_file(name, args, theory, line),
            other => Err(runtime_error(line, format!("unknown ordinary builtin '{other}'"))),
        }
    }

    /// v0.109: file handles (stage D). Failures are recorded in the handle
    /// (sticky, first error wins) and reported by `file_ok`, `file_error` and
    /// `file_close`; they are never runtime errors.
    #[inline(never)]
    fn eval_ordinary_file(&mut self, name: &str, args: &[Expr], theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        let Some((_, params, _)) = crate::checker::FILE_BUILTINS.iter().find(|(n, _, _)| *n == name) else {
            return Err(runtime_error(line, format!("unknown ordinary builtin '{name}'")));
        };
        arity(name, args, params.len(), line)?;
        let mut values = Vec::with_capacity(args.len());
        for arg in args {
            values.push(self.eval_expr(arg, theory)?);
        }
        let text = |v: &RuntimeValue| -> Result<String, Diagnostic> {
            match v {
                RuntimeValue::Text(t) => Ok(t.clone()),
                other => Err(runtime_error(line, format!("{name} expects Text")).with_help(format!("runtime value was: {}", other.display_atom()))),
            }
        };
        let result = |r: Result<u128, String>| match r {
            Ok(n) => RuntimeValue::ResultValue { is_ok: true, value: Box::new(RuntimeValue::NatExact(n)) },
            Err(e) => RuntimeValue::ResultValue { is_ok: false, value: Box::new(RuntimeValue::Text(e)) },
        };
        match name {
            "file_open" => {
                let (path, mode) = (text(&values[0])?, text(&values[1])?);
                let mut slot = FileSlot::default();
                slot.path = path.clone();
                let opened = match mode.as_str() {
                    "r" => Some(std::fs::File::open(&path)),
                    "w" => Some(std::fs::File::create(&path)),
                    // positioned at the end, as C stdio does for append streams (`file_tell`)
                    "a" => Some(std::fs::OpenOptions::new().append(true).create(true).open(&path).and_then(|mut f| {
                        use std::io::Seek;
                        f.seek(std::io::SeekFrom::End(0)).map(|_| f)
                    })),
                    _ => None,
                };
                match opened {
                    None => slot.fail(format!("invalid file mode `{mode}`")),
                    Some(Err(_)) => slot.fail(format!("cannot open file `{path}`")),
                    Some(Ok(f)) => {
                        slot.file = Some(f);
                        slot.mode = mode.as_bytes()[0];
                    }
                }
                let id = match self.free_files.pop() {
                    Some(id) => { self.files[id] = slot; id }
                    None => { self.files.push(slot); self.files.len() - 1 }
                };
                return Ok(RuntimeValue::File(id));
            }
            "file_remove" => {
                let path = text(&values[0])?;
                return Ok(result(std::fs::remove_file(&path).map(|_| 0).map_err(|_| format!("cannot remove file `{path}`"))));
            }
            n if n.starts_with("proc_") || n.starts_with("net_") || n.starts_with("win_") => return self.eval_os(n, &values, line),
            "file_font_load" => return Ok(result(self.fonts.load(&text(&values[0])?))),
            "file_font_find" => {
                return Ok(match crate::font::find(&text(&values[0])?) {
                    Ok(p) => RuntimeValue::ResultValue { is_ok: true, value: Box::new(RuntimeValue::Text(p)) },
                    Err(e) => RuntimeValue::ResultValue { is_ok: false, value: Box::new(RuntimeValue::Text(e)) },
                });
            }
            "file_write_png" => {
                let path = text(&values[0])?;
                let img = match &values[1] {
                    RuntimeValue::NatVec(v) => v.clone(),
                    other => return Err(runtime_error(line, "file_write_png expects a NatVec image").with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                return Ok(result(match crate::gfx::png(&img) {
                    None => Err(format!("cannot encode `{path}`: not a non-empty image")),
                    Some(bytes) => std::fs::write(&path, &bytes).map(|_| bytes.len() as u128).map_err(|e| format!("cannot write `{path}` (os error {})", e.raw_os_error().unwrap_or(0))),
                }));
            }
            "file_exists" => return Ok(RuntimeValue::Bool(std::fs::metadata(text(&values[0])?).is_ok())),
            "file_is_dir" => return Ok(RuntimeValue::Bool(std::fs::metadata(text(&values[0])?).is_ok_and(|m| m.is_dir()))),
            "file_size_of" => {
                let path = text(&values[0])?;
                return Ok(result(std::fs::metadata(&path).map(|m| u128::from(m.len())).map_err(|e| os_fail(format!("cannot stat `{path}`"), &e))));
            }
            "file_mtime" => {
                let path = text(&values[0])?;
                return Ok(result(std::fs::metadata(&path).and_then(|m| m.modified()).map(|t| t.duration_since(std::time::UNIX_EPOCH).map_or(0, |d| u128::from(d.as_secs()))).map_err(|e| os_fail(format!("cannot stat `{path}`"), &e))));
            }
            "file_make_dir" => {
                let path = text(&values[0])?;
                return Ok(result(std::fs::create_dir_all(&path).map(|_| 0).map_err(|e| os_fail(format!("cannot create directory `{path}`"), &e))));
            }
            "file_remove_dir" => {
                let path = text(&values[0])?;
                return Ok(result(std::fs::remove_dir(&path).map(|_| 0).map_err(|e| os_fail(format!("cannot remove directory `{path}`"), &e))));
            }
            "file_rename" => {
                let (from, to) = (text(&values[0])?, text(&values[1])?);
                return Ok(result(std::fs::rename(&from, &to).map(|_| 0).map_err(|e| os_fail(format!("cannot rename `{from}` to `{to}`"), &e))));
            }
            "file_copy" => {
                let (from, to) = (text(&values[0])?, text(&values[1])?);
                let copied = (|| -> std::io::Result<u128> {
                    let data = std::fs::read(&from)?;
                    std::fs::write(&to, &data)?;
                    Ok(data.len() as u128)
                })();
                return Ok(result(copied.map_err(|e| os_fail(format!("cannot copy `{from}` to `{to}`"), &e))));
            }
            "file_write_atomic" => {
                let (path, body) = (text(&values[0])?, text(&values[1])?);
                use std::io::Write;
                use std::sync::atomic::{AtomicU64, Ordering};
                static NEXT_TEMP: AtomicU64 = AtomicU64::new(0);
                let done = (|| -> std::io::Result<()> {
                    for _ in 0..128 {
                        let id = NEXT_TEMP.fetch_add(1, Ordering::Relaxed);
                        let tmp = format!("{path}.dlm-tmp.{}.{id}", std::process::id());
                        let mut f = match std::fs::OpenOptions::new().write(true).create_new(true).open(&tmp) {
                            Ok(f) => f,
                            Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => continue,
                            Err(e) => return Err(e),
                        };
                        let written = f.write_all(body.as_bytes()).and_then(|_| f.flush());
                        drop(f);
                        let replaced = written.and_then(|_| std::fs::rename(&tmp, &path));
                        if replaced.is_err() { let _ = std::fs::remove_file(&tmp); }
                        return replaced;
                    }
                    Err(std::io::Error::from(std::io::ErrorKind::AlreadyExists))
                })();
                return Ok(result(done.map(|_| body.len() as u128).map_err(|e| os_fail(format!("cannot replace `{path}`"), &e))));
            }
            "file_list_dir" | "file_walk" => {
                let path = text(&values[0])?;
                let listed = if name == "file_walk" {
                    let mut out = Vec::new();
                    fs_walk(std::path::Path::new(&path), "", &mut out).map(|_| out)
                } else {
                    std::fs::read_dir(&path)
                        .and_then(|it| it.map(|e| e.map(|e| e.file_name().into_string().ok())).collect::<std::io::Result<Vec<_>>>())
                        .map(|names| {
                            let mut names: Vec<String> = names.into_iter().flatten().collect();
                            names.sort();
                            names
                        })
                        .map_err(|e| os_fail(format!("cannot list directory `{path}`"), &e))
                };
                return Ok(match listed {
                    Ok(names) => RuntimeValue::ResultValue { is_ok: true, value: Box::new(RuntimeValue::List(names.into_iter().map(RuntimeValue::Text).collect())) },
                    Err(e) => RuntimeValue::ResultValue { is_ok: false, value: Box::new(RuntimeValue::Text(e)) },
                });
            }
            _ => {}
        }
        let id = match &values[0] {
            RuntimeValue::File(id) if *id < self.files.len() && !self.files[*id].closed => *id,
            other => return Err(runtime_error(line, format!("{name} expects a File")).with_help(format!("runtime value was: {}", other.display_atom()))),
        };
        let slot = &mut self.files[id];
        Ok(match name {
            "file_ok" => RuntimeValue::Bool(slot.error.is_none()),
            "file_error" => RuntimeValue::Text(slot.error.clone().unwrap_or_default()),
            "file_write" => {
                let t = text(&values[1])?;
                RuntimeValue::Bool(slot.write(t.as_bytes()))
            }
            "file_write_bytes" => match &values[1] {
                RuntimeValue::Bytes(b) => RuntimeValue::Bool(slot.write(b)),
                other => return Err(runtime_error(line, format!("{name} expects a ByteBuf")).with_help(format!("runtime value was: {}", other.display_atom()))),
            },
            "file_flush" => RuntimeValue::Bool(slot.flush_out()),
            "file_set_buffered" => {
                let on = matches!(values[1], RuntimeValue::Bool(true));
                let ok = slot.flush_out();
                slot.buffered = on;
                RuntimeValue::Bool(ok)
            }
            "file_seek" => {
                use std::io::Seek;
                let pos = expect_nat(name, values[1].clone(), line)?;
                if !slot.flush_out() {
                    return Ok(RuntimeValue::Bool(false));
                }
                if pos > i64::MAX as u128 {
                    slot.fail(format!("cannot seek file `{}`", slot.path));
                    return Ok(RuntimeValue::Bool(false));
                }
                let pos = pos as u64;
                match slot.file.as_mut().map(|f| f.seek(std::io::SeekFrom::Start(pos))) {
                    Some(Ok(_)) if pos <= i64::MAX as u64 => {
                        slot.pending.clear();
                        slot.eof = false;
                        RuntimeValue::Bool(true)
                    }
                    _ => {
                        slot.fail(format!("cannot seek file `{}`", slot.path));
                        RuntimeValue::Bool(false)
                    }
                }
            }
            "file_tell" => {
                use std::io::Seek;
                if !slot.flush_out() {
                    return Ok(RuntimeValue::NatExact(0));
                }
                match slot.file.as_mut().map(|f| f.stream_position()) {
                    Some(Ok(p)) => RuntimeValue::NatExact(u128::from(p) - slot.pending.len() as u128),
                    _ => {
                        slot.fail(format!("cannot seek file `{}`", slot.path));
                        RuntimeValue::NatExact(0)
                    }
                }
            }
            "file_size" => {
                if !slot.flush_out() {
                    return Ok(RuntimeValue::NatExact(0));
                }
                match slot.file.as_ref().map(|f| f.metadata()) {
                    Some(Ok(m)) => RuntimeValue::NatExact(u128::from(m.len())),
                    _ => {
                        slot.fail(format!("cannot stat file `{}`", slot.path));
                        RuntimeValue::NatExact(0)
                    }
                }
            }
            "file_truncate" => {
                let n = expect_nat(name, values[1].clone(), line)?;
                if !slot.writable() || !slot.flush_out() {
                    return Ok(RuntimeValue::Bool(false));
                }
                if n > i64::MAX as u128 {
                    slot.fail(format!("cannot truncate file `{}`", slot.path));
                    return Ok(RuntimeValue::Bool(false));
                }
                match slot.file.as_ref().map(|f| f.set_len(n as u64)) {
                    Some(Ok(())) if n <= i64::MAX as u128 => RuntimeValue::Bool(true),
                    _ => {
                        slot.fail(format!("cannot truncate file `{}`", slot.path));
                        RuntimeValue::Bool(false)
                    }
                }
            }
            "file_read_line" => {
                if !slot.readable() {
                    return Ok(RuntimeValue::OptionValue(None));
                }
                loop {
                    if let Some(i) = slot.pending.iter().position(|&c| c == b'\n') {
                        let mut bytes: Vec<u8> = slot.pending.drain(..=i).collect();
                        bytes.pop();
                        break RuntimeValue::OptionValue(slot.text(bytes).map(|t| Box::new(RuntimeValue::Text(t))));
                    }
                    if !slot.fill() {
                        if slot.error.is_some() || slot.pending.is_empty() {
                            break RuntimeValue::OptionValue(None);
                        }
                        let bytes = std::mem::take(&mut slot.pending);
                        break RuntimeValue::OptionValue(slot.text(bytes).map(|t| Box::new(RuntimeValue::Text(t))));
                    }
                }
            }
            "file_read_all" => {
                if !slot.readable() {
                    return Ok(RuntimeValue::Text(String::new()));
                }
                while slot.fill() {}
                if slot.error.is_some() {
                    return Ok(RuntimeValue::Text(String::new()));
                }
                let bytes = std::mem::take(&mut slot.pending);
                RuntimeValue::Text(slot.text(bytes).unwrap_or_default())
            }
            "file_read_bytes" => {
                let n = expect_nat(name, values[1].clone(), line)?;
                if n > BYTE_BUFFER_MAX as u128 {
                    slot.fail(format!("byte buffer length exceeds {BYTE_BUFFER_MAX}"));
                }
                if !slot.readable() {
                    return Ok(RuntimeValue::Bytes(Vec::new()));
                }
                while (slot.pending.len() as u128) < n && slot.fill() {}
                if slot.error.is_some() {
                    return Ok(RuntimeValue::Bytes(Vec::new()));
                }
                let take = (n as usize).min(slot.pending.len());
                RuntimeValue::Bytes(slot.pending.drain(..take).collect())
            }
            "file_close" => {
                slot.flush_out();
                let mut closed = std::mem::take(slot);
                slot.closed = true;
                self.free_files.push(id);
                closed.file = None;
                result(match closed.error.take() {
                    Some(e) => Err(e),
                    None => Ok(closed.written),
                })
            }
            _ => return Err(runtime_error(line, format!("unknown ordinary builtin '{name}'"))),
        })
    }

    /// v0.115: natural-number vectors and graph algorithms (`crate::graph`), the same definitions as the native `dvg_*`.
    #[inline(never)]
    fn eval_ordinary_graph(&mut self, name: &str, args: &[Expr], theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        use crate::graph as gr;
        let Some((_, params, _)) = crate::checker::GRAPH_BUILTINS.iter().chain(crate::checker::LINALG_BUILTINS.iter()).find(|(n, _, _)| *n == name) else {
            return Err(runtime_error(line, format!("unknown ordinary builtin '{name}'")));
        };
        arity(name, args, params.len(), line)?;
        let mut values = Vec::with_capacity(args.len());
        for arg in args {
            values.push(self.eval_expr(arg, theory)?);
        }
        let bad = |what: &str, v: &RuntimeValue| runtime_error(line, format!("{name} expects {what}")).with_help(format!("runtime value was: {}", v.display_atom()));
        let nv = |i: usize| -> Result<Vec<u128>, Diagnostic> {
            match &values[i] {
                RuntimeValue::NatVec(b) => Ok(b.clone()),
                other => Err(bad("a NatVec", other)),
            }
        };
        let fv = |i: usize| -> Result<Vec<f64>, Diagnostic> {
            match &values[i] {
                RuntimeValue::FloatVec(b) => Ok(b.iter().map(|x| f64::from_bits(*x)).collect()),
                other => Err(bad("a FloatVec", other)),
            }
        };
        let n = |i: usize| expect_nat(name, values[i].clone(), line);
        let ov = |x: Option<Vec<u128>>| RuntimeValue::OptionValue(x.filter(|x| x.len() <= gr::VEC_MAX).map(|x| Box::new(RuntimeValue::NatVec(x))));
        let of = |x: Option<Vec<f64>>| RuntimeValue::OptionValue(x.map(|x| Box::new(RuntimeValue::FloatVec(x.into_iter().map(f64::to_bits).collect()))));
        let on = |x: Option<u128>| RuntimeValue::OptionValue(x.map(|x| Box::new(RuntimeValue::NatExact(x))));
        let nm = |i: usize| -> Result<Vec<(u128, u128)>, Diagnostic> {
            match &values[i] {
                RuntimeValue::NatMap(m) => Ok(m.clone()),
                other => Err(bad("a NatMap", other)),
            }
        };
        let tmap = |i: usize| -> Result<Vec<(String, u128)>, Diagnostic> {
            match &values[i] {
                RuntimeValue::TextMap(m) => Ok(m.clone()),
                other => Err(bad("a TextMap", other)),
            }
        };
        let txt = |i: usize| -> Result<String, Diagnostic> {
            match &values[i] {
                RuntimeValue::Text(t) => Ok(t.clone()),
                other => Err(bad("Text", other)),
            }
        };
        let om = |x: Option<Vec<(u128, u128)>>| RuntimeValue::OptionValue(x.map(|m| Box::new(RuntimeValue::NatMap(m))));
        let otm = |x: Option<Vec<(String, u128)>>| RuntimeValue::OptionValue(x.map(|m| Box::new(RuntimeValue::TextMap(m))));
        use crate::maps as mp;
        let fo = |x: Option<f64>| RuntimeValue::OptionValue(x.map(|x| Box::new(RuntimeValue::Float(x.to_bits()))));
        let fl = |i: usize| -> Result<f64, Diagnostic> {
            match &values[i] {
                RuntimeValue::Float(b) => Ok(f64::from_bits(*b)),
                other => Err(bad("a Float", other)),
            }
        };
        use crate::linalg as la;
        use crate::number as nt;
        Ok(match name {
            "nv_empty" => RuntimeValue::NatVec(Vec::new()),
            "nv_new" => {
                let (len, x) = (n(0)?, n(1)?);
                ov((len <= gr::VEC_MAX as u128).then(|| vec![x; len as usize]))
            }
            "nv_from_list" => {
                let (_, items) = expect_collection(name, values[0].clone(), line)?;
                let mut out = Vec::with_capacity(items.len());
                for it in items {
                    out.push(expect_nat(name, it, line)?);
                }
                ov(Some(out))
            }
            "nv_to_list" => RuntimeValue::List(nv(0)?.into_iter().map(RuntimeValue::NatExact).collect()),
            "nv_len" => RuntimeValue::NatExact(nv(0)?.len() as u128),
            "nv_get" => {
                let (a, i) = (nv(0)?, n(1)?);
                on(usize::try_from(i).ok().and_then(|i| a.get(i).copied()))
            }
            "nv_set" => {
                let (mut a, i, x) = (nv(0)?, n(1)?, n(2)?);
                ov(match usize::try_from(i).ok().filter(|i| *i < a.len()) {
                    Some(i) => {
                        a[i] = x;
                        Some(a)
                    }
                    None => None,
                })
            }
            "nv_push" => {
                let (mut a, x) = (nv(0)?, n(1)?);
                a.push(x);
                ov(Some(a))
            }
            "nv_slice" => {
                let (a, s, l) = (nv(0)?, n(1)?, n(2)?);
                ov(s.checked_add(l).filter(|e| *e <= a.len() as u128).map(|e| a[s as usize..e as usize].to_vec()))
            }
            "nv_concat" => {
                let (mut a, b) = (nv(0)?, nv(1)?);
                a.extend(b);
                ov(Some(a))
            }
            "nv_range" => ov(gr::range(n(0)?, n(1)?)),
            "nv_sum" => on(gr::sum(&nv(0)?)),
            "nv_max" => on(nv(0)?.into_iter().max()),
            "nv_min" => on(nv(0)?.into_iter().min()),
            "nv_sort" => {
                let mut a = nv(0)?;
                a.sort_unstable();
                RuntimeValue::NatVec(a)
            }
            "nv_reverse" => {
                let mut a = nv(0)?;
                a.reverse();
                RuntimeValue::NatVec(a)
            }
            "nv_find" => {
                let (a, x) = (nv(0)?, n(1)?);
                on(a.iter().position(|y| *y == x).map(|i| i as u128))
            }
            "nv_to_fv" => RuntimeValue::FloatVec(nv(0)?.into_iter().map(|x| (x as f64).to_bits()).collect()),
            "gr_bfs" => ov(gr::bfs(n(0)?, &nv(1)?, &nv(2)?, n(3)?)),
            "gr_dfs" => ov(gr::dfs(n(0)?, &nv(1)?, &nv(2)?, n(3)?)),
            "gr_toposort" => ov(gr::toposort(n(0)?, &nv(1)?, &nv(2)?)),
            "gr_components" => ov(gr::components(n(0)?, &nv(1)?, &nv(2)?)),
            "gr_scc" => ov(gr::scc(n(0)?, &nv(1)?, &nv(2)?)),
            "gr_bipartite" => ov(gr::bipartite(n(0)?, &nv(1)?, &nv(2)?)),
            "gr_dijkstra" => of(gr::dijkstra(n(0)?, &nv(1)?, &nv(2)?, &fv(3)?, n(4)?)),
            "gr_bellman_ford" => of(gr::bellman_ford(n(0)?, &nv(1)?, &nv(2)?, &fv(3)?, n(4)?)),
            "gr_floyd" => of(gr::floyd(n(0)?, &nv(1)?, &nv(2)?, &fv(3)?)),
            "gr_mst" => ov(gr::mst(n(0)?, &nv(1)?, &nv(2)?, &fv(3)?)),
            "gr_maxflow" => on(gr::maxflow(n(0)?, &nv(1)?, &nv(2)?, &nv(3)?, n(4)?, n(5)?)),
            "la_lu" => of(la::lu(&fv(0)?, n(1)?)),
            "la_lu_perm" => ov(la::lu_perm(&fv(0)?, n(1)?)),
            "la_det" => fo(la::det(&fv(0)?, n(1)?)),
            "la_inverse" => of(la::inverse(&fv(0)?, n(1)?)),
            "la_cholesky" => of(la::cholesky(&fv(0)?, n(1)?)),
            "la_qr_q" => of(la::qr(&fv(0)?, n(1)?, n(2)?).map(|(q, _)| q)),
            "la_qr_r" => of(la::qr(&fv(0)?, n(1)?, n(2)?).map(|(_, r)| r)),
            "la_lstsq" => of(la::lstsq(&fv(0)?, &fv(1)?, n(2)?, n(3)?)),
            "la_eigh_values" => of(la::eigh(&fv(0)?, n(1)?).map(|(v, _)| v)),
            "la_eigh_vectors" => of(la::eigh(&fv(0)?, n(1)?).map(|(_, v)| v)),
            "la_svd_values" => of(la::svd_values(&fv(0)?, n(1)?, n(2)?)),
            "la_rank" => on(la::rank(&fv(0)?, n(1)?, n(2)?, fl(3)?)),
            "la_identity" => of(la::identity(n(0)?)),
            "la_trace" => fo(la::trace(&fv(0)?, n(1)?)),
            "la_csr_matvec" => of(la::csr_matvec(&nv(0)?, &nv(1)?, &fv(2)?, &fv(3)?)),
            "gb_basis" => ov(crate::groebner::gb_basis(&nv(0)?, &nv(1)?, n(2)?, n(3)?, n(4)?)),
            "gb_reduce" => ov(crate::groebner::gb_reduce(&nv(0)?, &nv(1)?, &nv(2)?, n(3)?, n(4)?, n(5)?)),
            "mv_add" => ov(crate::groebner::mv_binary(&nv(0)?, &nv(1)?, n(2)?, n(3)?, 0)),
            "mv_sub" => ov(crate::groebner::mv_binary(&nv(0)?, &nv(1)?, n(2)?, n(3)?, 1)),
            "mv_mul" => ov(crate::groebner::mv_binary(&nv(0)?, &nv(1)?, n(2)?, n(3)?, 2)),
            "mv_normalize" => ov(crate::groebner::mv_normalize(&nv(0)?, n(1)?, n(2)?, n(3)?)),
            "la_pcg" => of(crate::sparse::pcg(&nv(0)?, &nv(1)?, &fv(2)?, &fv(3)?, fl(4)?, n(5)?)),
            "la_bicgstab" => of(crate::sparse::bicgstab(&nv(0)?, &nv(1)?, &fv(2)?, &fv(3)?, fl(4)?, n(5)?)),
            "la_gmres" => of(crate::sparse::gmres(&nv(0)?, &nv(1)?, &fv(2)?, &fv(3)?, fl(4)?, n(5)?, n(6)?)),
            "geo_orient2d" => on(crate::geometry::orient2d(&fv(0)?)),
            "geo_incircle" => on(crate::geometry::incircle(&fv(0)?)),
            "geo_orient3d" => on(crate::geometry::orient3d(&fv(0)?)),
            "geo_delaunay" => ov(crate::geometry::delaunay(&fv(0)?)),
            "kd_knn" => ov(crate::geometry::knn(&fv(0)?, &fv(1)?, n(2)?, n(3)?)),
            "kd_radius" => ov(crate::geometry::radius(&fv(0)?, n(1)?, &fv(2)?, fl(3)?)),
            "fft2_forward" => of(crate::spectral::fft2(&fv(0)?, n(1)?, n(2)?, false)),
            "fft2_inverse" => of(crate::spectral::fft2(&fv(0)?, n(1)?, n(2)?, true)),
            "sig_stft" => of(crate::spectral::stft(&fv(0)?, &fv(1)?, n(2)?)),
            "sig_dwt" => of(crate::spectral::dwt(&fv(0)?, n(1)?, n(2)?)),
            "sig_idwt" => of(crate::spectral::idwt(&fv(0)?, n(1)?, n(2)?)),
            "sig_iir" => of(crate::spectral::iir(&fv(0)?, &fv(1)?, &fv(2)?)),
            "sig_sha256" => RuntimeValue::Text(crate::spectral::sha256(txt(0)?.as_bytes())),
            "bvh_raycast" => of(crate::bvh::raycast(&fv(0)?, &fv(1)?, &fv(2)?)),
            "bvh_winding" => of(crate::bvh::winding(&fv(0)?, &fv(1)?)),
            "bvh_pairs" => ov(crate::bvh::aabb_pairs(&fv(0)?)),
            "gx_new" => ov(crate::gfx::new(n(0)?, n(1)?, n(2)?)),
            "gx_width" | "gx_height" => {
                let img = nv(0)?;
                let ok = img.len() >= 2 && img[0].checked_mul(img[1]).and_then(|p| p.checked_add(2)) == Some(img.len() as u128);
                RuntimeValue::NatExact(if ok { img[usize::from(name == "gx_height")] } else { 0 })
            }
            "gx_get" => on(crate::gfx::get(&nv(0)?, n(1)?, n(2)?)),
            "gx_fill_rect" => RuntimeValue::NatVec(crate::gfx::fill_rect(nv(0)?, n(1)?, n(2)?, n(3)?, n(4)?, n(5)?)),
            "gx_line" => RuntimeValue::NatVec(crate::gfx::line(nv(0)?, n(1)?, n(2)?, n(3)?, n(4)?, n(5)?)),
            "gx_circle" => {
                let filled = matches!(values[5], RuntimeValue::Bool(true));
                RuntimeValue::NatVec(crate::gfx::circle(nv(0)?, n(1)?, n(2)?, n(3)?, n(4)?, filled))
            }
            "gx_text" => RuntimeValue::NatVec(crate::gfx::text(nv(0)?, n(1)?, n(2)?, &txt(3)?, n(4)?, n(5)?)),
            "gx_text_width" => RuntimeValue::NatExact(crate::gfx::text_width(&txt(0)?, n(1)?)),
            "gx_blit" => RuntimeValue::NatVec(crate::gfx::blit(nv(0)?, &nv(1)?, n(2)?, n(3)?)),
            "gx_checksum" => RuntimeValue::NatExact(crate::gfx::checksum(&nv(0)?)),
            "vx_render" => ov(crate::vector::render_fonts(&nv(0)?, n(1)?, n(2)?, n(3)?, n(4)?, &self.fonts)),
            "vx_render_region" => ov(crate::vector::render_region_fonts(nv(0)?, &nv(1)?, n(2)?, n(3)?, n(4)?, n(5)?, n(6)?, n(7)?, &self.fonts)),
            "vx_bounds" => ov(crate::vector::bounds_fonts(&nv(0)?, n(1)?, n(2)?, &self.fonts)),
            "vx_text_fonts" => RuntimeValue::NatVec(crate::vector::font_text_command(n(0)?, n(1)?, n(2)?, n(3)?, &nv(4)?, &nv(5)?, &txt(6)?)),
            "vx_font_width" => {
                let cps = crate::vector::visual(&txt(1)?.chars().map(|c| u128::from(c as u32)).collect::<Vec<_>>());
                RuntimeValue::NatExact(crate::vector::font_text_width(&self.fonts, &nv(0)?, &cps, n(2)?))
            }
            "vx_font_has" => {
                let (id, cp) = (n(0)?, n(1)?);
                RuntimeValue::Bool(self.fonts.get(id).is_some_and(|f| u32::try_from(cp).is_ok_and(|c| f.glyph_index(c) != 0)))
            }
            "vx_font_advance" => {
                let (id, cp, size) = (n(0)?, n(1)?, n(2)?);
                let em = size.min(crate::vector::FONT_SIZE_MAX) * 27 / 32;
                RuntimeValue::NatExact(self.fonts.get(id).and_then(|f| u32::try_from(cp).ok().map(|c| (f, f.glyph_index(c)))).map_or(0, |(f, g)| u128::try_from(f.advance(g)).unwrap_or(0) * em / f.upem.max(1) as u128))
            }
            "vx_font_metrics" => RuntimeValue::NatVec(self.fonts.get(n(0)?).map_or_else(Vec::new, |f| vec![f.upem.max(0) as u128, f.ascender.max(0) as u128, f.descender.unsigned_abs() as u128, u128::from(f.num_glyphs)])),
            "vx_codepoints" => RuntimeValue::NatVec(txt(0)?.chars().map(|c| u128::from(c as u32)).collect()),
            "vx_from_codepoints" => RuntimeValue::Text(nv(0)?.iter().map(|&c| u32::try_from(c).ok().and_then(char::from_u32).unwrap_or('\u{FFFD}')).collect()),
            "vx_text_width" => RuntimeValue::NatExact(crate::vector::text_width(&txt(0)?, n(1)?)),
            "vx_text" => RuntimeValue::NatVec(crate::vector::text_command(n(0)?, n(1)?, n(2)?, n(3)?, &nv(4)?, &txt(5)?)),
            "vx_svg" => RuntimeValue::OptionValue(crate::vector::svg_fonts(&nv(0)?, n(1)?, n(2)?, &self.fonts).map(|t| Box::new(RuntimeValue::Text(t)))),
            "km_empty" => RuntimeValue::NatMap(Vec::new()),
            "km_insert" => om(mp::insert(&nm(0)?, n(1)?, n(2)?)),
            "km_get" => on(mp::get(&nm(0)?, &n(1)?)),
            "km_remove" => RuntimeValue::NatMap(mp::remove(&nm(0)?, &n(1)?)),
            "km_contains" => RuntimeValue::Bool(mp::get(&nm(0)?, &n(1)?).is_some()),
            "km_len" => RuntimeValue::NatExact(nm(0)?.len() as u128),
            "km_keys" => RuntimeValue::NatVec(nm(0)?.into_iter().map(|e| e.0).collect()),
            "km_values" => RuntimeValue::NatVec(nm(0)?.into_iter().map(|e| e.1).collect()),
            "km_from" => om(mp::from_pairs(nv(0)?, &nv(1)?)),
            "km_add" => om(mp::add(&nm(0)?, n(1)?, n(2)?)),
            "km_min" => on(nm(0)?.first().map(|e| e.0)),
            "km_max" => on(nm(0)?.last().map(|e| e.0)),
            "km_floor" => on(mp::floor(&nm(0)?, &n(1)?)),
            "km_ceil" => on(mp::ceil(&nm(0)?, &n(1)?)),
            "km_merge" => om(mp::merge(&nm(0)?, &nm(1)?)),
            "km_range" => RuntimeValue::NatMap(mp::range(&nm(0)?, &n(1)?, &n(2)?)),
            "tm_empty" => RuntimeValue::TextMap(Vec::new()),
            "tm_insert" => otm(mp::insert(&tmap(0)?, txt(1)?, n(2)?)),
            "tm_get" => on(mp::get(&tmap(0)?, &txt(1)?)),
            "tm_remove" => RuntimeValue::TextMap(mp::remove(&tmap(0)?, &txt(1)?)),
            "tm_contains" => RuntimeValue::Bool(mp::get(&tmap(0)?, &txt(1)?).is_some()),
            "tm_len" => RuntimeValue::NatExact(tmap(0)?.len() as u128),
            "tm_keys" => RuntimeValue::List(tmap(0)?.into_iter().map(|e| RuntimeValue::Text(e.0)).collect()),
            "tm_values" => RuntimeValue::NatVec(tmap(0)?.into_iter().map(|e| e.1).collect()),
            "tm_from" => {
                let (_, items) = expect_collection(name, values[0].clone(), line)?;
                let mut keys = Vec::with_capacity(items.len());
                for it in items {
                    match it {
                        RuntimeValue::Text(t) => keys.push(t),
                        other => return Err(bad("a List<Text>", &other)),
                    }
                }
                otm(mp::from_pairs(keys, &nv(1)?))
            }
            "tm_add" => otm(mp::add(&tmap(0)?, txt(1)?, n(2)?)),
            "tm_merge" => otm(mp::merge(&tmap(0)?, &tmap(1)?)),
            "tm_count_words" => {
                let mut m: Vec<(String, u128)> = Vec::new();
                for w in txt(0)?.split_whitespace() {
                    m = mp::add(&m, w.to_string(), 1).unwrap_or(m);
                }
                RuntimeValue::TextMap(m)
            }
            "sat_proof" => ov(crate::combinatorics::sat_proof(&nv(0)?, &nv(1)?, n(2)?)),
            "sat_check" => RuntimeValue::OptionValue(crate::combinatorics::sat_check(&nv(0)?, &nv(1)?, n(2)?, &nv(3)?).map(|b| Box::new(RuntimeValue::Bool(b)))),
            "sat_count" => on(crate::combinatorics::sat_count(&nv(0)?, &nv(1)?, n(2)?)),
            "ilp_solve" => {
                let (s, x) = crate::spectral::ilp(&fv(0)?, &fv(1)?, &fv(2)?, n(3)?, n(4)?, &nv(5)?);
                of((s == 0).then_some(x))
            }
            "ilp_status" => RuntimeValue::NatExact(crate::spectral::ilp(&fv(0)?, &fv(1)?, &fv(2)?, n(3)?, n(4)?, &nv(5)?).0),
            "sat_solve" => {
                let (s, m) = crate::combinatorics::sat(&nv(0)?, &nv(1)?, n(2)?);
                ov((s == 0).then_some(m))
            }
            "sat_status" => RuntimeValue::NatExact(crate::combinatorics::sat(&nv(0)?, &nv(1)?, n(2)?).0),
            "tp_homology" => ov(crate::combinatorics::homology(&nv(0)?, &nv(1)?)),
            "tp_rips" => of(crate::combinatorics::rips(&fv(0)?, n(1)?, n(2)?, fl(3)?)),
            "nt_gcd" => RuntimeValue::NatExact(nt::gcd(n(0)?, n(1)?)),
            "nt_lcm" => on(nt::lcm(n(0)?, n(1)?)),
            "nt_mod_mul" => {
                let (a, b, m) = (n(0)?, n(1)?, n(2)?);
                on((m != 0).then(|| nt::mulmod(a, b, m)))
            }
            "nt_mod_pow" => on(nt::powmod(n(0)?, n(1)?, n(2)?)),
            "nt_mod_inv" => on(nt::mod_inv(n(0)?, n(1)?)),
            "nt_is_prime" => RuntimeValue::Bool(nt::is_prime(n(0)?)),
            "nt_next_prime" => on(nt::next_prime(n(0)?)),
            "nt_factor" => ov(nt::factor(n(0)?)),
            "nt_totient" => RuntimeValue::NatExact(nt::totient(n(0)?)),
            "nt_divisors" => ov(nt::divisors(n(0)?)),
            "nt_primes" => ov(nt::primes(n(0)?)),
            "nt_crt" => on(nt::crt(&nv(0)?, &nv(1)?)),
            "nt_sqrt_mod" => on(nt::sqrt_mod(n(0)?, n(1)?)),
            "nt_isqrt" => RuntimeValue::NatExact(nt::isqrt(n(0)?)),
            "pf_add" => ov(nt::pf_add(&nv(0)?, &nv(1)?, n(2)?)),
            "pf_sub" => ov(nt::pf_sub(&nv(0)?, &nv(1)?, n(2)?)),
            "pf_mul" => ov(nt::pf_mul(&nv(0)?, &nv(1)?, n(2)?)),
            "pf_div" => ov(nt::pf_div(&nv(0)?, &nv(1)?, n(2)?)),
            "pf_rem" => ov(nt::pf_rem(&nv(0)?, &nv(1)?, n(2)?)),
            "pf_gcd" => ov(nt::pf_gcd(&nv(0)?, &nv(1)?, n(2)?)),
            "pf_powmod" => ov(nt::pf_powmod(&nv(0)?, n(1)?, &nv(2)?, n(3)?)),
            "pf_eval" => on(nt::pf_eval(&nv(0)?, n(1)?, n(2)?)),
            "pf_irreducible" => RuntimeValue::OptionValue(nt::pf_irreducible(&nv(0)?, n(1)?).map(|b| Box::new(RuntimeValue::Bool(b)))),
            "pm_compose" => ov(nt::pm_compose(&nv(0)?, &nv(1)?)),
            "pm_inverse" => ov(nt::pm_inverse(&nv(0)?)),
            "pm_power" => ov(nt::pm_power(&nv(0)?, n(1)?)),
            "pm_order" => on(nt::pm_order(&nv(0)?)),
            "pm_sign" => on(nt::pm_sign(&nv(0)?)),
            "pm_cycle_type" => ov(nt::pm_cycle_type(&nv(0)?)),
            "pm_orbit" => ov(nt::pm_orbit(&nv(0)?, n(1)?, n(2)?)),
            "pm_group_order" => on(nt::pm_group_order(&nv(0)?, n(1)?)),
            "fft_forward" => of(crate::spectral::fft(&fv(0)?, false)),
            "fft_inverse" => of(crate::spectral::fft(&fv(0)?, true)),
            "fft_convolve" => of(crate::spectral::convolve(&fv(0)?, &fv(1)?)),
            "lp_solve" => {
                let (s, x) = crate::spectral::simplex(&fv(0)?, &fv(1)?, &fv(2)?, n(3)?, n(4)?);
                of((s == 0).then_some(x))
            }
            "lp_status" => RuntimeValue::NatExact(crate::spectral::simplex(&fv(0)?, &fv(1)?, &fv(2)?, n(3)?, n(4)?).0),
            "la_cg" => of(la::cg(&nv(0)?, &nv(1)?, &fv(2)?, &fv(3)?, fl(4)?, n(5)?)),
            _ => return Err(runtime_error(line, format!("unknown ordinary builtin '{name}'"))),
        })
    }

    /// v0.114: machine floats and float vectors (`crate::float`), the same definitions as the native `dvf_*`.
    #[inline(never)]
    fn eval_ordinary_float(&mut self, name: &str, args: &[Expr], theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        use crate::float as fl;
        let Some((_, params, _)) = crate::checker::FLOAT_BUILTINS.iter().find(|(n, _, _)| *n == name) else {
            return Err(runtime_error(line, format!("unknown ordinary builtin '{name}'")));
        };
        arity(name, args, params.len(), line)?;
        let mut values = Vec::with_capacity(args.len());
        for arg in args {
            values.push(self.eval_expr(arg, theory)?);
        }
        let bad = |what: &str, v: &RuntimeValue| runtime_error(line, format!("{name} expects {what}")).with_help(format!("runtime value was: {}", v.display_atom()));
        let f = |i: usize| -> Result<f64, Diagnostic> {
            match &values[i] {
                RuntimeValue::Float(b) => Ok(f64::from_bits(*b)),
                other => Err(bad("a Float", other)),
            }
        };
        let v = |i: usize| -> Result<Vec<f64>, Diagnostic> {
            match &values[i] {
                RuntimeValue::FloatVec(b) => Ok(b.iter().map(|x| f64::from_bits(*x)).collect()),
                other => Err(bad("a FloatVec", other)),
            }
        };
        let n = |i: usize| expect_nat(name, values[i].clone(), line);
        let fl_ = |x: f64| RuntimeValue::Float(x.to_bits());
        let vec_ = |x: Vec<f64>| RuntimeValue::FloatVec(x.into_iter().map(f64::to_bits).collect());
        let ov = |x: Option<Vec<f64>>| RuntimeValue::OptionValue(x.filter(|x| x.len() <= fl::VEC_MAX).map(|x| Box::new(RuntimeValue::FloatVec(x.into_iter().map(f64::to_bits).collect()))));
        let of = |x: Option<f64>| RuntimeValue::OptionValue(x.map(|x| Box::new(RuntimeValue::Float(x.to_bits()))));
        let text = |i: usize| -> Result<String, Diagnostic> {
            match &values[i] {
                RuntimeValue::Text(t) => Ok(t.clone()),
                other => Err(bad("Text", other)),
            }
        };
        Ok(match name {
            "fl_of_nat" => fl_(n(0)? as f64),
            "fl_of_int" => match &values[0] {
                RuntimeValue::IntExact(i) => fl_(*i as f64),
                other => return Err(bad("an Int", other)),
            },
            "fl_lit" => fl_(fl::literal(&text(0)?)),
            "fl_parse" => of(fl::parse(&text(0)?)),
            "fl_add" => fl_(f(0)? + f(1)?),
            "fl_sub" => fl_(f(0)? - f(1)?),
            "fl_mul" => fl_(f(0)? * f(1)?),
            "fl_div" => fl_(f(0)? / f(1)?),
            "fl_neg" => fl_(-f(0)?),
            "fl_abs" => fl_(f(0)?.abs()),
            "fl_min" => fl_(fl::min(f(0)?, f(1)?)),
            "fl_max" => fl_(fl::max(f(0)?, f(1)?)),
            "fl_sqrt" => fl_(f(0)?.sqrt()),
            "fl_exp" => fl_(f(0)?.exp()),
            "fl_log" => fl_(f(0)?.ln()),
            "fl_sin" => fl_(f(0)?.sin()),
            "fl_cos" => fl_(f(0)?.cos()),
            "fl_tan" => fl_(f(0)?.tan()),
            "fl_atan" => fl_(f(0)?.atan()),
            "fl_atan2" => fl_(f(0)?.atan2(f(1)?)),
            "fl_pow" => fl_(f(0)?.powf(f(1)?)),
            "fl_floor" => fl_(f(0)?.floor()),
            "fl_ceil" => fl_(f(0)?.ceil()),
            "fl_round" => fl_(f(0)?.round()),
            "fl_trunc" => fl_(f(0)?.trunc()),
            "fl_lt" => RuntimeValue::Bool(f(0)? < f(1)?),
            "fl_le" => RuntimeValue::Bool(f(0)? <= f(1)?),
            "fl_eq" => RuntimeValue::Bool(f(0)? == f(1)?),
            "fl_is_nan" => RuntimeValue::Bool(f(0)?.is_nan()),
            "fl_is_finite" => RuntimeValue::Bool(f(0)?.is_finite()),
            "fl_to_text" => RuntimeValue::Text(fl::text(f(0)?)),
            "fl_to_int" => RuntimeValue::OptionValue(fl::to_int(f(0)?).map(|i| Box::new(RuntimeValue::IntExact(i)))),
            "fl_bits" => RuntimeValue::NatExact(u128::from(f(0)?.to_bits())),
            "fl_from_bits" => RuntimeValue::Float(n(0)? as u64),
            "fv_empty" => vec_(Vec::new()),
            "fv_new" => {
                let (len, x) = (n(0)?, f(1)?);
                ov((len <= fl::VEC_MAX as u128).then(|| vec![x; len as usize]))
            }
            "fv_from_list" => {
                let (_, items) = expect_collection(name, values[0].clone(), line)?;
                let mut out = Vec::with_capacity(items.len());
                for it in &items {
                    match it {
                        RuntimeValue::Float(b) => out.push(f64::from_bits(*b)),
                        other => return Err(bad("a List<Float>", other)),
                    }
                }
                ov(Some(out))
            }
            "fv_to_list" => RuntimeValue::List(v(0)?.into_iter().map(fl_).collect()),
            "fv_len" => RuntimeValue::NatExact(v(0)?.len() as u128),
            "fv_get" => {
                let (a, i) = (v(0)?, n(1)?);
                of(usize::try_from(i).ok().and_then(|i| a.get(i).copied()))
            }
            "fv_set" => {
                let (mut a, i, x) = (v(0)?, n(1)?, f(2)?);
                ov(match usize::try_from(i).ok().filter(|i| *i < a.len()) {
                    Some(i) => {
                        a[i] = x;
                        Some(a)
                    }
                    None => None,
                })
            }
            "fv_add" => ov(fl::zip(&v(0)?, &v(1)?, |x, y| x + y)),
            "fv_sub" => ov(fl::zip(&v(0)?, &v(1)?, |x, y| x - y)),
            "fv_mul" => ov(fl::zip(&v(0)?, &v(1)?, |x, y| x * y)),
            "fv_scale" => {
                let s = f(1)?;
                vec_(v(0)?.into_iter().map(|x| x * s).collect())
            }
            "fv_axpy" => {
                let a = f(0)?;
                ov(fl::zip(&v(1)?, &v(2)?, |x, y| a * x + y))
            }
            "fv_dot" => of(fl::dot(&v(0)?, &v(1)?)),
            "fv_sum" => fl_(fl::sum(&v(0)?)),
            "fv_norm" => fl_(fl::norm(&v(0)?)),
            "fv_max" => of(v(0)?.into_iter().reduce(fl::max)),
            "fv_min" => of(v(0)?.into_iter().reduce(fl::min)),
            "fv_slice" => {
                let (a, s, l) = (v(0)?, n(1)?, n(2)?);
                ov(s.checked_add(l).filter(|e| *e <= a.len() as u128).map(|e| a[s as usize..e as usize].to_vec()))
            }
            "fv_concat" => {
                let (mut a, b) = (v(0)?, v(1)?);
                a.extend(b);
                ov(Some(a))
            }
            "fv_matmul" => ov(fl::matmul(&v(0)?, &v(1)?, n(2)?, n(3)?, n(4)?)),
            "fv_transpose" => ov(fl::transpose(&v(0)?, n(1)?, n(2)?)),
            "fv_solve" => ov(fl::solve(&v(0)?, &v(1)?, n(2)?)),
            _ => return Err(runtime_error(line, format!("unknown ordinary builtin '{name}'"))),
        })
    }

    /// v0.107: dense natural numbers; the same algorithms as the native `dvn_*`.
    #[inline(never)]
    fn eval_ordinary_dense(&mut self, name: &str, args: &[Expr], theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        use crate::dense_nat as dn;
        let Some((_, params, _)) = crate::checker::DENSE_NAT_BUILTINS.iter().find(|(n, _, _)| *n == name) else {
            return Err(runtime_error(line, format!("unknown ordinary builtin '{name}'")));
        };
        arity(name, args, params.len(), line)?;
        let mut values = Vec::with_capacity(args.len());
        for arg in args {
            values.push(self.eval_expr(arg, theory)?);
        }
        let limbs = |v: &RuntimeValue| -> Result<Vec<u32>, Diagnostic> {
            match v {
                RuntimeValue::BigNat(x) => Ok(x.clone()),
                other => Err(runtime_error(line, format!("{name} expects a BigNat")).with_help(format!("runtime value was: {}", other.display_atom()))),
            }
        };
        let big = RuntimeValue::BigNat;
        let opt = |x: Option<Vec<u32>>| RuntimeValue::OptionValue(x.map(|x| Box::new(RuntimeValue::BigNat(x))));
        let nat = |v: &RuntimeValue| expect_nat(name, v.clone(), line);
        Ok(match name {
            "bn_from_nat" => big(dn::from_u128(nat(&values[0])?)),
            "bn_to_nat" => RuntimeValue::OptionValue(dn::to_u128(&limbs(&values[0])?).map(|n| Box::new(RuntimeValue::NatExact(n)))),
            "bn_from_words" => {
                let (_, items) = expect_collection(name, values[0].clone(), line)?;
                let words = items.into_iter().map(|w| expect_nat(name, w, line)).collect::<Result<Vec<u128>, _>>()?;
                opt(dn::from_words(&words))
            }
            "bn_to_words" => RuntimeValue::List(dn::to_words(&limbs(&values[0])?).into_iter().map(RuntimeValue::NatExact).collect()),
            "bn_from_text" => match &values[0] {
                RuntimeValue::Text(t) => opt(dn::from_decimal(t)),
                other => return Err(runtime_error(line, format!("{name} expects Text")).with_help(format!("runtime value was: {}", other.display_atom()))),
            },
            "bn_to_text" => RuntimeValue::Text(dn::to_decimal(&limbs(&values[0])?)),
            "bn_bits" => RuntimeValue::NatExact(dn::bits(&limbs(&values[0])?) as u128),
            "bn_limbs" => RuntimeValue::NatExact(limbs(&values[0])?.len() as u128),
            "bn_sqrt" => big(dn::sqrt(&limbs(&values[0])?)),
            "bn_shl" => opt(dn::shl(&limbs(&values[0])?, nat(&values[1])?)),
            "bn_shr" => big(dn::shr(&limbs(&values[0])?, nat(&values[1])?)),
            "bn_powmod" => opt(crate::bignum::powmod(&limbs(&values[0])?, &limbs(&values[1])?, &limbs(&values[2])?)),
            "bn_is_prime" => RuntimeValue::Bool(crate::bignum::is_prime(&limbs(&values[0])?)),
            "bn_next_prime" => opt(crate::bignum::next_prime(&limbs(&values[0])?)),
            "bn_random" => opt(crate::bignum::random(nat(&values[0])?, nat(&values[1])?)),
            "bn_to_hex" => RuntimeValue::Text(crate::bignum::to_hex(&limbs(&values[0])?)),
            "bn_from_hex" => match &values[0] {
                RuntimeValue::Text(t) => opt(crate::bignum::from_hex(t)),
                other => return Err(runtime_error(line, format!("{name} expects Text")).with_help(format!("runtime value was: {}", other.display_atom()))),
            },
            _ => {
                let (a, b) = (limbs(&values[0])?, limbs(&values[1])?);
                match name {
                    "bn_cmp" => RuntimeValue::IntExact(dn::cmp(&a, &b) as i128),
                    "bn_add" => opt(dn::add(&a, &b)),
                    "bn_monus" => big(dn::monus(&a, &b)),
                    "bn_mul" => opt(dn::mul(&a, &b)),
                    "bn_div" => opt(dn::divrem(&a, &b).map(|(q, _)| q)),
                    "bn_mod" => opt(dn::divrem(&a, &b).map(|(_, r)| r)),
                    "bn_gcd" => big(dn::gcd(&a, &b)),
                    "bn_modinv" => opt(crate::bignum::modinv(&a, &b)),
                    _ => return Err(runtime_error(line, format!("unknown ordinary builtin '{name}'"))),
                }
            }
        })
    }

    /// v0.102: owned byte buffers. The interpreter has value semantics; the
    /// ownership checker guarantees that a moved buffer is never observed
    /// again, so this agrees with the in-place native runtime.
    #[inline(never)]
    fn eval_ordinary_buffer(&mut self, name: &str, args: &[Expr], theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        let buffer = |value: RuntimeValue| -> Result<Vec<u8>, Diagnostic> {
            match value {
                RuntimeValue::Bytes(bytes) => Ok(bytes),
                other => Err(runtime_error(line, format!("{name} expects a ByteBuf")).with_help(format!("runtime value was: {}", other.display_atom()))),
            }
        };
        let byte = |value: u128| -> Result<u8, Diagnostic> {
            u8::try_from(value).map_err(|_| runtime_error(line, "byte value exceeds 255"))
        };
        let limit = |n: u128| -> Result<usize, Diagnostic> {
            if n > BYTE_BUFFER_MAX as u128 {
                Err(runtime_error(line, format!("byte buffer length exceeds {BYTE_BUFFER_MAX}")))
            } else {
                Ok(n as usize)
            }
        };
        match name {
            "buf_new" => {
                arity(name, args, 2, line)?;
                let n = self.eval_expr(&args[0], theory)?;
                let n = expect_nat(name, n, line)?;
                let fill = self.eval_expr(&args[1], theory)?;
                let fill = expect_nat(name, fill, line)?;
                let n = limit(n)?;
                Ok(RuntimeValue::Bytes(vec![byte(fill)?; n]))
            }
            "buf_from_list" => {
                arity(name, args, 1, line)?;
                let xs = self.eval_expr(&args[0], theory)?;
                let (_, items) = expect_collection(name, xs, line)?;
                limit(items.len() as u128)?;
                let bytes = items.into_iter().map(|item| byte(expect_nat(name, item, line)?)).collect::<Result<Vec<u8>, _>>()?;
                Ok(RuntimeValue::Bytes(bytes))
            }
            "buf_len" | "buf_to_list" | "buf_free" => {
                arity(name, args, 1, line)?;
                let b = buffer(self.eval_expr(&args[0], theory)?)?;
                Ok(match name {
                    "buf_to_list" => RuntimeValue::List(b.into_iter().map(|x| RuntimeValue::NatExact(u128::from(x))).collect()),
                    _ => RuntimeValue::NatExact(b.len() as u128),
                })
            }
            "buf_get" => {
                arity(name, args, 2, line)?;
                let b = buffer(self.eval_expr(&args[0], theory)?)?;
                let i = self.eval_expr(&args[1], theory)?;
                let i = expect_nat(name, i, line)?;
                let item = usize::try_from(i).ok().and_then(|i| b.get(i).copied());
                Ok(RuntimeValue::OptionValue(item.map(|x| Box::new(RuntimeValue::NatExact(u128::from(x))))))
            }
            "buf_set" => {
                arity(name, args, 3, line)?;
                let mut b = buffer(self.eval_expr(&args[0], theory)?)?;
                let i = self.eval_expr(&args[1], theory)?;
                let i = expect_nat(name, i, line)?;
                let v = self.eval_expr(&args[2], theory)?;
                let v = byte(expect_nat(name, v, line)?)?;
                let slot = usize::try_from(i).ok().and_then(|i| b.get_mut(i)).ok_or_else(|| runtime_error(line, "byte buffer index out of range"))?;
                *slot = v;
                Ok(RuntimeValue::Bytes(b))
            }
            "buf_push" => {
                arity(name, args, 2, line)?;
                let mut b = buffer(self.eval_expr(&args[0], theory)?)?;
                let v = self.eval_expr(&args[1], theory)?;
                let v = byte(expect_nat(name, v, line)?)?;
                limit(b.len() as u128 + 1)?;
                b.push(v);
                Ok(RuntimeValue::Bytes(b))
            }
            // v0.103: copies, ranges, concatenation, comparison and UTF-8 text.
            "buf_copy" => {
                arity(name, args, 1, line)?;
                Ok(RuntimeValue::Bytes(buffer(self.eval_expr(&args[0], theory)?)?))
            }
            "buf_slice" => {
                arity(name, args, 3, line)?;
                let b = buffer(self.eval_expr(&args[0], theory)?)?;
                let start = self.eval_expr(&args[1], theory)?;
                let start = expect_nat(name, start, line)?;
                let len = self.eval_expr(&args[2], theory)?;
                let len = expect_nat(name, len, line)?;
                let end = start.checked_add(len).filter(|&end| end <= b.len() as u128)
                    .ok_or_else(|| runtime_error(line, "byte buffer range out of bounds"))?;
                Ok(RuntimeValue::Bytes(b[start as usize..end as usize].to_vec()))
            }
            "buf_append" => {
                arity(name, args, 2, line)?;
                let mut a = buffer(self.eval_expr(&args[0], theory)?)?;
                let b = buffer(self.eval_expr(&args[1], theory)?)?;
                limit(a.len() as u128 + b.len() as u128)?;
                a.extend_from_slice(&b);
                Ok(RuntimeValue::Bytes(a))
            }
            "buf_eq" => {
                arity(name, args, 2, line)?;
                let a = buffer(self.eval_expr(&args[0], theory)?)?;
                let b = buffer(self.eval_expr(&args[1], theory)?)?;
                Ok(RuntimeValue::Bool(a == b))
            }
            "buf_from_text" => {
                arity(name, args, 1, line)?;
                let t = match self.eval_expr(&args[0], theory)? {
                    RuntimeValue::Text(t) => t,
                    other => return Err(runtime_error(line, "buf_from_text expects Text").with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                limit(t.len() as u128)?;
                Ok(RuntimeValue::Bytes(t.into_bytes()))
            }
            "buf_to_text" => {
                arity(name, args, 1, line)?;
                let b = buffer(self.eval_expr(&args[0], theory)?)?;
                Ok(RuntimeValue::OptionValue(String::from_utf8(b).ok().map(|t| Box::new(RuntimeValue::Text(t)))))
            }
            // v0.104: text builders (UTF-8 bytes; lengths in Unicode scalars)
            "tbuf_new" => {
                arity(name, args, 0, line)?;
                Ok(RuntimeValue::Bytes(Vec::new()))
            }
            "tbuf_push" => {
                arity(name, args, 2, line)?;
                let mut t = buffer(self.eval_expr(&args[0], theory)?)?;
                let s = match self.eval_expr(&args[1], theory)? {
                    RuntimeValue::Text(s) => s,
                    other => return Err(runtime_error(line, "tbuf_push expects Text").with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                limit(t.len() as u128 + s.len() as u128)?;
                t.extend_from_slice(s.as_bytes());
                Ok(RuntimeValue::Bytes(t))
            }
            "tbuf_len" | "tbuf_finish" => {
                arity(name, args, 1, line)?;
                let t = String::from_utf8(buffer(self.eval_expr(&args[0], theory)?)?).map_err(|_| runtime_error(line, "text buffer is not UTF-8"))?;
                Ok(if name == "tbuf_len" { RuntimeValue::NatExact(t.chars().count() as u128) } else { RuntimeValue::Text(t) })
            }
            other => Err(runtime_error(line, format!("unknown byte buffer builtin '{other}'"))),
        }
    }

    #[inline(never)]
    fn eval_ordinary_scalar(&mut self, name: &str, args: &[Expr], theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        match name {
            "true" | "false" => {
                arity(name, args, 0, line)?;
                Ok(RuntimeValue::Bool(name == "true"))
            }
            "not" => {
                arity(name, args, 1, line)?;
                let b = self.eval_expr(&args[0], theory)?;
                Ok(RuntimeValue::Bool(!expect_bool(name, b, line)?))
            }
            "and" | "or" => {
                arity(name, args, 2, line)?;
                // Short-circuit evaluation; both operands are pure ordinary values.
                let lhs = self.eval_expr(&args[0], theory)?;
                let lhs = expect_bool(name, lhs, line)?;
                if (name == "and" && !lhs) || (name == "or" && lhs) {
                    return Ok(RuntimeValue::Bool(lhs));
                }
                let rhs = self.eval_expr(&args[1], theory)?;
                Ok(RuntimeValue::Bool(expect_bool(name, rhs, line)?))
            }
            "nat_eq" => {
                arity(name, args, 2, line)?;
                let a = self.eval_expr(&args[0], theory)?;
                let b = self.eval_expr(&args[1], theory)?;
                Ok(RuntimeValue::Bool(expect_nat(name, a, line)? == expect_nat(name, b, line)?))
            }
            "mul" => {
                arity(name, args, 2, line)?;
                let a = self.eval_expr(&args[0], theory)?;
                let b = self.eval_expr(&args[1], theory)?;
                if let (RuntimeValue::IntExact(a), RuntimeValue::IntExact(b)) = (&a, &b) {
                    return a.checked_mul(*b).map(RuntimeValue::IntExact).ok_or_else(|| runtime_error(line, "Int multiplication overflows the exact Int range"));
                }
                expect_nat(name, a, line)?
                    .checked_mul(expect_nat(name, b, line)?)
                    .map(RuntimeValue::NatExact)
                    .ok_or_else(|| runtime_error(line, "multiplication result overflows MVP runtime exact Nat range"))
            }
            "monus" => {
                arity(name, args, 2, line)?;
                let a = self.eval_expr(&args[0], theory)?;
                let b = self.eval_expr(&args[1], theory)?;
                Ok(RuntimeValue::NatExact(expect_nat(name, a, line)?.saturating_sub(expect_nat(name, b, line)?)))
            }
            "pred" => {
                arity(name, args, 1, line)?;
                let a = self.eval_expr(&args[0], theory)?;
                Ok(RuntimeValue::NatExact(expect_nat(name, a, line)?.saturating_sub(1)))
            }
            other => Err(runtime_error(line, format!("unknown ordinary builtin '{other}'"))),
        }
    }

    #[inline(never)]
    fn eval_ordinary_control(&mut self, name: &str, args: &[Expr], theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        match name {
            "if_then_else" => {
                arity(name, args, 3, line)?;
                let cond = self.eval_expr(&args[0], theory)?;
                // Lazy: only the selected branch is evaluated.
                if expect_bool(name, cond, line)? {
                    self.eval_expr(&args[1], theory)
                } else {
                    self.eval_expr(&args[2], theory)
                }
            }
            other => Err(runtime_error(line, format!("unknown ordinary builtin '{other}'"))),
        }
    }

    #[inline(never)]
    fn eval_ordinary_partial(&mut self, name: &str, args: &[Expr], theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        match name {
            "effect_fn" => { arity(name,args,2,line)?; self.eval_expr(&args[1],theory) }
            "some" => {
                arity(name, args, 1, line)?;
                let value = self.eval_expr(&args[0], theory)?;
                Ok(RuntimeValue::OptionValue(Some(Box::new(value))))
            }
            "none" => {
                arity(name, args, 1, line)?;
                Ok(RuntimeValue::OptionValue(None))
            }
            "ok" | "err" => {
                arity(name, args, 2, line)?;
                let value = self.eval_expr(&args[0], theory)?;
                Ok(RuntimeValue::ResultValue { is_ok: name == "ok", value: Box::new(value) })
            }
            "is_some" | "is_none" => {
                arity(name, args, 1, line)?;
                match self.eval_expr(&args[0], theory)? {
                    RuntimeValue::OptionValue(inner) => Ok(RuntimeValue::Bool(inner.is_some() == (name == "is_some"))),
                    other => Err(runtime_error(line, format!("{name} expects an Option value"))
                        .with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "is_ok" | "is_err" => {
                arity(name, args, 1, line)?;
                match self.eval_expr(&args[0], theory)? {
                    RuntimeValue::ResultValue { is_ok, .. } => Ok(RuntimeValue::Bool(is_ok == (name == "is_ok"))),
                    other => Err(runtime_error(line, format!("{name} expects a Result value"))
                        .with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "__match_ok" | "__match_err" => {
                arity(name, args, 1, line)?;
                match self.eval_expr(&args[0], theory)? {
                    RuntimeValue::ResultValue { is_ok, value } if is_ok == (name == "__match_ok") => Ok(*value),
                    _ => Err(runtime_error(line, "result pattern matched the wrong variant")),
                }
            }
            "unwrap_or" => {
                arity(name, args, 2, line)?;
                match self.eval_expr(&args[0], theory)? {
                    RuntimeValue::OptionValue(Some(value)) => Ok(*value),
                    RuntimeValue::ResultValue { is_ok: true, value } => Ok(*value),
                    RuntimeValue::OptionValue(None) | RuntimeValue::ResultValue { is_ok: false, .. } => {
                        self.eval_expr(&args[1], theory)
                    }
                    other => Err(runtime_error(line, "unwrap_or expects an Option or Result value")
                        .with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            other => Err(runtime_error(line, format!("unknown ordinary builtin '{other}'"))),
        }
    }

    /// v0.85: Int builtins (exact i128, overflow is an error) and total
    /// division (`div(a, 0) = 0`, `mod(a, 0) = a`; Euclidean for Int).
    #[inline(never)]
    fn eval_ordinary_int(&mut self, name: &str, args: &[Expr], theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        let int = |value: RuntimeValue| -> Result<i128, Diagnostic> {
            match value {
                RuntimeValue::IntExact(i) => Ok(i),
                other => Err(runtime_error(line, format!("{name} expects Int")).with_help(format!("runtime value was: {}", other.display_atom()))),
            }
        };
        let shown = match name {
            "__sub" => "Int subtraction",
            "__neg" => "Int negation",
            other => other,
        };
        let overflow = || runtime_error(line, format!("{shown} overflows the exact Int range"));
        match name {
            "int" => {
                arity(name, args, 1, line)?;
                let n = self.eval_expr(&args[0], theory)?;
                let n = expect_nat(name, n, line)?;
                i128::try_from(n).map(RuntimeValue::IntExact).map_err(|_| overflow())
            }
            "abs" => {
                arity(name, args, 1, line)?;
                let i = int(self.eval_expr(&args[0], theory)?)?;
                Ok(RuntimeValue::NatExact(i.unsigned_abs()))
            }
            "int_eq" => {
                arity(name, args, 2, line)?;
                let a = int(self.eval_expr(&args[0], theory)?)?;
                let b = int(self.eval_expr(&args[1], theory)?)?;
                Ok(RuntimeValue::Bool(a == b))
            }
            "div" | "mod" => {
                arity(name, args, 2, line)?;
                let a = self.eval_expr(&args[0], theory)?;
                let b = self.eval_expr(&args[1], theory)?;
                match (a, b) {
                    (RuntimeValue::NatExact(a), RuntimeValue::NatExact(b)) => Ok(RuntimeValue::NatExact(match (name, b) {
                        ("div", 0) => 0,
                        ("mod", 0) => a,
                        ("div", b) => a / b,
                        (_, b) => a % b,
                    })),
                    (RuntimeValue::IntExact(a), RuntimeValue::IntExact(b)) => {
                        let value = match (name, b) {
                            ("div", 0) => Some(0),
                            ("mod", 0) => Some(a),
                            ("div", b) => a.checked_div_euclid(b),
                            (_, b) => a.checked_rem_euclid(b),
                        };
                        value.map(RuntimeValue::IntExact).ok_or_else(overflow)
                    }
                    (a, b) => Err(runtime_error(line, format!("{name} expects two Nat or two Int values"))
                        .with_help(format!("runtime values were: {}, {}", a.display_atom(), b.display_atom()))),
                }
            }
            "int_to_text" => {
                arity(name, args, 1, line)?;
                Ok(RuntimeValue::Text(int(self.eval_expr(&args[0], theory)?)?.to_string()))
            }
            "text_to_int" => {
                arity(name, args, 1, line)?;
                let RuntimeValue::Text(t) = self.eval_expr(&args[0], theory)? else {
                    return Err(runtime_error(line, "text_to_int expects Text"));
                };
                // Optional `-`, then decimal digits only.
                let digits = t.strip_prefix('-').unwrap_or(&t);
                let valid = !digits.is_empty() && digits.chars().all(|c| c.is_ascii_digit());
                let parsed = if valid { t.parse::<i128>().ok() } else { None };
                Ok(RuntimeValue::OptionValue(parsed.map(|i| Box::new(RuntimeValue::IntExact(i)))))
            }
            "__sub" => {
                arity(name, args, 2, line)?;
                let a = int(self.eval_expr(&args[0], theory)?)?;
                let b = int(self.eval_expr(&args[1], theory)?)?;
                a.checked_sub(b).map(RuntimeValue::IntExact).ok_or_else(overflow)
            }
            "__neg" => {
                arity(name, args, 1, line)?;
                // `-5`: the literal is read as an Int.
                if let ExprKind::IntLiteral(text) = &args[0].kind {
                    let n: u128 = text.parse().map_err(|_| overflow())?;
                    return if n == 1u128 << 127 {
                        Ok(RuntimeValue::IntExact(i128::MIN))
                    } else {
                        i128::try_from(n).map(|n| RuntimeValue::IntExact(-n)).map_err(|_| overflow())
                    };
                }
                let i = int(self.eval_expr(&args[0], theory)?)?;
                i.checked_neg().map(RuntimeValue::IntExact).ok_or_else(overflow)
            }
            other => Err(runtime_error(line, format!("unknown Int builtin '{other}'"))),
        }
    }

    /// v0.83: Text builtins. Lengths and positions count Unicode scalar values.
    #[inline(never)]
    fn eval_ordinary_text(&mut self, name: &str, args: &[Expr], theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        let text = |value: RuntimeValue| -> Result<String, Diagnostic> {
            match value {
                RuntimeValue::Text(text) => Ok(text),
                other => Err(runtime_error(line, format!("{name} expects Text")).with_help(format!("runtime value was: {}", other.display_atom()))),
            }
        };
        match name {
            "concat" => {
                arity(name, args, 2, line)?;
                let a = text(self.eval_expr(&args[0], theory)?)?;
                let b = text(self.eval_expr(&args[1], theory)?)?;
                Ok(RuntimeValue::Text(a + &b))
            }
            "text_length" => {
                arity(name, args, 1, line)?;
                let t = text(self.eval_expr(&args[0], theory)?)?;
                Ok(RuntimeValue::NatExact(t.chars().count() as u128))
            }
            "text_eq" => {
                arity(name, args, 2, line)?;
                let a = text(self.eval_expr(&args[0], theory)?)?;
                let b = text(self.eval_expr(&args[1], theory)?)?;
                Ok(RuntimeValue::Bool(a == b))
            }
            "nat_to_text" => {
                arity(name, args, 1, line)?;
                let n = self.eval_expr(&args[0], theory)?;
                Ok(RuntimeValue::Text(expect_nat(name, n, line)?.to_string()))
            }
            "text_to_nat" => {
                arity(name, args, 1, line)?;
                let t = text(self.eval_expr(&args[0], theory)?)?;
                let parsed = (!t.is_empty() && t.chars().all(|c| c.is_ascii_digit())).then(|| t.parse::<u128>().ok()).flatten();
                Ok(RuntimeValue::OptionValue(parsed.map(|n| Box::new(RuntimeValue::NatExact(n)))))
            }
            "substring" => {
                arity(name, args, 3, line)?;
                let t = text(self.eval_expr(&args[0], theory)?)?;
                let start = self.eval_expr(&args[1], theory)?;
                let start = usize::try_from(expect_nat(name, start, line)?).unwrap_or(usize::MAX);
                let len = self.eval_expr(&args[2], theory)?;
                let len = usize::try_from(expect_nat(name, len, line)?).unwrap_or(usize::MAX);
                Ok(RuntimeValue::Text(t.chars().skip(start).take(len).collect()))
            }
            "lines" | "words" => {
                arity(name, args, 1, line)?;
                let t = text(self.eval_expr(&args[0], theory)?)?;
                let parts: Vec<RuntimeValue> = if name == "lines" {
                    t.lines().map(|l| RuntimeValue::Text(l.to_string())).collect()
                } else {
                    t.split_whitespace().map(|w| RuntimeValue::Text(w.to_string())).collect()
                };
                Ok(RuntimeValue::List(parts))
            }
            "join" => {
                arity(name, args, 2, line)?;
                let parts = self.eval_expr(&args[0], theory)?;
                let (_, items) = expect_collection(name, parts, line)?;
                let sep = text(self.eval_expr(&args[1], theory)?)?;
                let pieces = items.into_iter().map(text).collect::<Result<Vec<_>, _>>()?;
                Ok(RuntimeValue::Text(pieces.join(&sep)))
            }
            other => Err(runtime_error(line, format!("unknown text builtin '{other}'"))),
        }
    }

    #[inline(never)]
    fn eval_ordinary_collections(&mut self, name: &str, args: &[Expr], theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        match name {
            "list" | "sequence" => {
                let mut items = Vec::with_capacity(args.len());
                for arg in args {
                    items.push(self.eval_expr(arg, theory)?);
                }
                Ok(rebuild(name == "sequence", items))
            }
            "empty_list" | "empty_sequence" => {
                arity(name, args, 1, line)?;
                Ok(rebuild(name == "empty_sequence", Vec::new()))
            }
            "length" => {
                arity(name, args, 1, line)?;
                let value = self.eval_expr(&args[0], theory)?;
                let (_, items) = expect_collection(name, value, line)?;
                Ok(RuntimeValue::NatExact(items.len() as u128))
            }
            "index" => {
                arity(name, args, 2, line)?;
                let value = self.eval_expr(&args[0], theory)?;
                let (_, items) = expect_collection(name, value, line)?;
                let position = self.eval_expr(&args[1], theory)?;
                let position = expect_nat(name, position, line)?;
                let found = usize::try_from(position).ok().and_then(|i| items.into_iter().nth(i));
                Ok(RuntimeValue::OptionValue(found.map(Box::new)))
            }
            // v0.82: `match` desugaring; the guard makes the failure arms unreachable.
            "__match_head" => {
                arity(name, args, 1, line)?;
                let value = self.eval_expr(&args[0], theory)?;
                let (_, items) = expect_collection(name, value, line)?;
                items.into_iter().next().ok_or_else(|| runtime_error(line, "list pattern matched an empty list"))
            }
            "__match_variant" | "__enum_payload" => {
                arity(name, args, 2, line)?;
                match self.eval_expr(&args[0], theory)? {
                    RuntimeValue::OptionValue(Some(value)) => Ok(*value),
                    _ => Err(runtime_error(line, "enum value has a variant that no match arm covers")),
                }
            }
            "__match_some" => {
                arity(name, args, 1, line)?;
                match self.eval_expr(&args[0], theory)? {
                    RuntimeValue::OptionValue(Some(value)) => Ok(*value),
                    other => Err(runtime_error(line, "option pattern `some` matched a non-some value")
                        .with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "head" => {
                arity(name, args, 1, line)?;
                let value = self.eval_expr(&args[0], theory)?;
                let (_, items) = expect_collection(name, value, line)?;
                Ok(RuntimeValue::OptionValue(items.into_iter().next().map(Box::new)))
            }
            "tail" => {
                arity(name, args, 1, line)?;
                let value = self.eval_expr(&args[0], theory)?;
                let (is_sequence, items) = expect_collection(name, value, line)?;
                Ok(rebuild(is_sequence, items.into_iter().skip(1).collect()))
            }
            "cons" => {
                arity(name, args, 2, line)?;
                let head = self.eval_expr(&args[0], theory)?;
                let value = self.eval_expr(&args[1], theory)?;
                let (is_sequence, items) = expect_collection(name, value, line)?;
                let mut out = Vec::with_capacity(items.len() + 1);
                out.push(head);
                out.extend(items);
                Ok(rebuild(is_sequence, out))
            }
            "is_empty" => {
                arity(name, args, 1, line)?;
                let value = self.eval_expr(&args[0], theory)?;
                let (_, items) = expect_collection(name, value, line)?;
                Ok(RuntimeValue::Bool(items.is_empty()))
            }
            other => Err(runtime_error(line, format!("unknown ordinary builtin '{other}'"))),
        }
    }

    #[inline(never)]
    fn eval_ordinary_product(&mut self, name: &str, args: &[Expr], theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        match name {
            "pair" => {
                arity(name, args, 2, line)?;
                let lhs = self.eval_expr(&args[0], theory)?;
                let rhs = self.eval_expr(&args[1], theory)?;
                Ok(RuntimeValue::Pair(Box::new(lhs), Box::new(rhs)))
            }
            "fst" | "snd" => {
                arity(name, args, 1, line)?;
                match self.eval_expr(&args[0], theory)? {
                    RuntimeValue::Pair(lhs, rhs) => Ok(if name == "fst" { *lhs } else { *rhs }),
                    other => Err(runtime_error(line, format!("{name} expects a Product value"))
                        .with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            other => Err(runtime_error(line, format!("unknown ordinary builtin '{other}'"))),
        }
    }

    #[inline(never)]
    fn eval_ordinary_traversal(&mut self, name: &str, args: &[Expr], theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        match name {
            "map" => {
                arity(name, args, 2, line)?;
                let value = self.eval_expr(&args[0], theory)?;
                let (is_sequence, items) = expect_collection(name, value, line)?;
                let function = self.function_value(&args[1], theory, name, line)?;
                // Fuel = collection length: exactly one application per item.
                let mut out = Vec::with_capacity(items.len());
                for item in items {
                    out.push(self.apply_callable(&function, vec![item], theory, line)?);
                }
                Ok(rebuild(is_sequence, out))
            }
            "fold" => {
                arity(name, args, 3, line)?;
                let value = self.eval_expr(&args[0], theory)?;
                let (_, items) = expect_collection(name, value, line)?;
                let mut acc = self.eval_expr(&args[1], theory)?;
                let function = self.function_value(&args[2], theory, name, line)?;
                for item in items {
                    acc = self.apply_callable(&function, vec![acc, item], theory, line)?;
                }
                Ok(acc)
            }
            other => Err(runtime_error(line, format!("unknown ordinary builtin '{other}'"))),
        }
    }

    /// v0.75: evaluate a function argument to a callable value (a declared fn
    /// by name evaluates to `FnRef`; lambdas evaluate to `Closure`).
    fn function_value(&mut self, expr: &Expr, theory: &str, op: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        match self.eval_expr(expr, theory)? {
            value @ (RuntimeValue::FnRef(_) | RuntimeValue::Closure { .. }) => Ok(value),
            other => Err(runtime_error(line, format!("{op} expects a function value"))
                .with_help(format!("runtime value was: {}", other.display_atom()))),
        }
    }

    pub(super) fn make_closure(&self, params: &[FnParam], body: &Expr) -> RuntimeValue {
        let mut names = Vec::new();
        crate::ordinary::free_value_names(body,&mut params.iter().map(|p|p.name.clone()).collect(),&mut names);
        let env = names.into_iter().filter_map(|n|self.locals.last().and_then(|f|f.get(&n)).map(|v|(n,v.clone()))).collect();
        RuntimeValue::Closure {
            params: params.iter().map(|p| p.name.clone()).collect(),
            body: Box::new(body.clone()),
            env,
        }
    }

    /// Apply a `FnRef` or `Closure`.
    fn apply_callable(&mut self, callee: &RuntimeValue, values: Vec<RuntimeValue>, theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        match callee {
            RuntimeValue::FnRef(name) => {
                let function = self
                    .functions
                    .get(theory)
                    .and_then(|fns| fns.get(name))
                    .cloned()
                    .ok_or_else(|| runtime_error(line, format!("unknown function '{name}' in theory {theory}")))?;
                self.apply_function(&function, values, theory, line)
            }
            RuntimeValue::Closure { params, body, env } => {
                if params.len() != values.len() {
                    return Err(runtime_error(
                        line,
                        format!("function value expects {} argument(s), got {}", params.len(), values.len()),
                    ));
                }
                if self.locals.len() >= MAX_CALL_DEPTH {
                    return Err(runtime_error(line, format!("call depth exceeded {MAX_CALL_DEPTH} while calling a lambda")));
                }
                self.check_stack_budget("<lambda>", line)?;
                let mut frame = env.clone();
                frame.extend(params.iter().cloned().zip(values));
                self.locals.push(frame);
                self.call_names.push("<lambda>".to_string());
                let result = self.eval_expr(body, theory);
                self.call_names.pop();
                self.locals.pop();
                result
            }
            other => Err(runtime_error(line, "value is not a function")
                .with_help(format!("runtime value was: {}", other.display_atom()))),
        }
    }

    /// Call of a user `fn`; `None` when `name` is not a declared function.
    pub(super) fn eval_user_fn_call(
        &mut self,
        name: &str,
        args: &[Expr],
        theory: &str,
        line: usize,
    ) -> Option<Result<RuntimeValue, Diagnostic>> {
        // v0.75: function-typed values (locals first, then theory scope) shadow
        // declared fns, matching the checker's resolution order.
        let local_callee = self
            .locals
            .last()
            .and_then(|frame| frame.get(name))
            .or_else(|| self.scopes.get(theory).and_then(|scope| scope.get(name)))
            .cloned();
        if let Some(callee) = local_callee {
            return Some((|| {
                let mut values = Vec::with_capacity(args.len());
                for arg in args {
                    values.push(self.eval_expr(arg, theory)?);
                }
                self.apply_callable(&callee, values, theory, line)
            })());
        }
        let Some(function) = self.functions.get(theory).and_then(|fns| fns.get(name)).cloned() else {
            let record = self.records.get(theory)?.get(name)?.clone();
            return Some(self.construct_record(&record, args, theory, line));
        };
        Some((|| {
            if args.len() != function.params.len() {
                return Err(runtime_error(
                    line,
                    format!("fn '{}' expects {} argument(s), got {}", function.name, function.params.len(), args.len()),
                ));
            }
            let mut values = Vec::with_capacity(args.len());
            for arg in args {
                values.push(self.eval_expr(arg, theory)?);
            }
            self.apply_function(&function, values, theory, line)
        })())
    }

    fn construct_record(&mut self, record: &RecordDecl, args: &[Expr], theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        if args.len() != record.fields.len() {
            return Err(runtime_error(
                line,
                format!("record '{}' expects {} field value(s), got {}", record.name, record.fields.len(), args.len()),
            ));
        }
        let mut fields = Vec::with_capacity(args.len());
        for (field, arg) in record.fields.iter().zip(args) {
            fields.push((field.name.clone(), self.eval_expr(arg, theory)?));
        }
        Ok(RuntimeValue::Record { name: record.name.clone(), fields })
    }

    fn apply_function(&mut self, function: &FnDecl, values: Vec<RuntimeValue>, theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        if self.locals.len() >= MAX_CALL_DEPTH {
            return Err(runtime_error(line, format!("call depth exceeded {MAX_CALL_DEPTH} while calling fn '{}'", function.name))
                .with_help("checked programs bound recursion by fuel; this guard is a defensive runtime boundary"));
        }
        self.check_stack_budget(&function.name, line)?;
        if function.measure.as_ref().is_some_and(|m| m.is_loop()) {
            return self.apply_tail_loop(function, values, theory);
        }
        if let Some(measure) = &function.measure {
            self.check_recursion_step(function, measure, &values, line)?;
        }
        let frame: BTreeMap<String, RuntimeValue> = function
            .params
            .iter()
            .map(|p| p.name.clone())
            .zip(values)
            .collect();
        self.locals.push(frame);
        self.call_names.push(function.name.clone());
        *self.fn_depth.entry(function.name.clone()).or_default() += 1;
        let result = self.eval_expr(&function.body, theory);
        if let Some(depth) = self.fn_depth.get_mut(&function.name) {
            *depth -= 1;
        }
        self.call_names.pop();
        self.locals.pop();
        result
    }

    /// v0.126: processes, environment and TCP connections (`crate::os`).
    fn eval_os(&mut self, name: &str, values: &[RuntimeValue], line: usize) -> Result<RuntimeValue, Diagnostic> {
        let text = |i: usize| -> Result<String, Diagnostic> {
            match &values[i] {
                RuntimeValue::Text(t) => Ok(t.clone()),
                other => Err(runtime_error(line, format!("{name} expects Text")).with_help(format!("runtime value was: {}", other.display_atom()))),
            }
        };
        let nat = |i: usize| expect_nat(name, values[i].clone(), line);
        let res = |r: Result<RuntimeValue, String>| match r {
            Ok(v) => RuntimeValue::ResultValue { is_ok: true, value: Box::new(v) },
            Err(e) => RuntimeValue::ResultValue { is_ok: false, value: Box::new(RuntimeValue::Text(e)) },
        };
        let texts = |xs: Vec<String>| RuntimeValue::List(xs.into_iter().map(RuntimeValue::Text).collect());
        let n = |r: Result<u128, String>| res(r.map(RuntimeValue::NatExact));
        Ok(match name {
            "proc_run" => {
                let (_, items) = expect_collection(name, values[1].clone(), line)?;
                let args = items
                    .into_iter()
                    .map(|v| match v {
                        RuntimeValue::Text(t) => Ok(t),
                        other => Err(runtime_error(line, "proc_run expects List<Text> arguments").with_help(format!("runtime value was: {}", other.display_atom()))),
                    })
                    .collect::<Result<Vec<_>, _>>()?;
                res(crate::os::proc_run(&text(0)?, &args, &text(2)?).map(texts))
            }
            "proc_spawn" => {
                let (_, items) = expect_collection(name, values[1].clone(), line)?;
                let args = items
                    .into_iter()
                    .map(|v| match v {
                        RuntimeValue::Text(t) => Ok(t),
                        other => Err(runtime_error(line, "proc_spawn expects List<Text> arguments").with_help(format!("runtime value was: {}", other.display_atom()))),
                    })
                    .collect::<Result<Vec<_>, _>>()?;
                n(self.procs.spawn(&text(0)?, &args, &text(2)?))
            }
            "proc_poll" => res(self.procs.poll(nat(0)?).map(|c| RuntimeValue::OptionValue(c.map(|c| Box::new(RuntimeValue::NatExact(c)))))),
            "proc_wait" => res(self.procs.wait(nat(0)?).map(texts)),
            "proc_kill" => n(self.procs.kill(nat(0)?)),
            "win_open" => n(self.windows.open(&text(0)?, nat(1)?, nat(2)?)),
            "win_present" => match &values[1] {
                RuntimeValue::NatVec(img) => n(self.windows.present(nat(0)?, img)),
                other => return Err(runtime_error(line, "win_present expects a NatVec image").with_help(format!("runtime value was: {}", other.display_atom()))),
            },
            "win_poll" => res(self.windows.poll(nat(0)?, nat(1)?).map(|e| RuntimeValue::NatVec(e.to_vec()))),
            "win_size" => res(self.windows.size(nat(0)?).map(RuntimeValue::NatVec)),
            "win_close" => n(self.windows.close(nat(0)?)),
            "win_gpu" => res(self.windows.gpu(nat(0)?).map(RuntimeValue::Text)),
            "win_present_scene" => {
                let scene = match &values[1] {
                    RuntimeValue::NatVec(v) => v.clone(),
                    other => return Err(runtime_error(line, "win_present_scene expects a NatVec scene").with_help(format!("runtime value was: {}", other.display_atom()))),
                };
                let (id, w, h, num, den) = (nat(0)?, nat(2)?, nat(3)?, nat(4)?, nat(5)?);
                let shown = if w == 0 || h == 0 || w > 16384 || h > 16384 || num == 0 || den == 0 || num > 65536 || den > 65536 {
                    Err("win_present_scene needs a size up to 16384 and a scale num/den in 1..65536".to_string())
                } else {
                    match self.windows.is_gpu(id) {
                        Err(e) => Err(e),
                        Ok(true) => match crate::vector::gpu_items(&scene, w, h, num, den, &self.fonts) {
                            None => Err("invalid scene".to_string()),
                            Some(items) => self.windows.present_gpu(id, items, w, h),
                        },
                        Ok(false) => match crate::vector::render_fonts(&scene, w, h, num, den, &self.fonts) {
                            None => Err("invalid scene".to_string()),
                            Some(img) => self.windows.present(id, &img).map(|_| 0),
                        },
                    }
                };
                n(shown)
            }
            "win_read" => res(self.windows.read(nat(0)?).map(RuntimeValue::NatVec)),
            "proc_env" => {
                let key = text(0)?;
                let valid = !key.is_empty() && !key.contains(['=', '\0']);
                RuntimeValue::OptionValue(valid.then(|| std::env::var(&key).ok()).flatten().map(|v| Box::new(RuntimeValue::Text(v))))
            }
            "net_resolve" => res(crate::os::resolve(&text(0)?).map(texts)),
            "net_listen" => n(self.net.listen(&text(0)?, nat(1)?)),
            "net_local_port" => n(self.net.local_port(nat(0)?)),
            "net_accept" => n(self.net.accept(nat(0)?, nat(1)?)),
            "net_connect" => n(self.net.connect(&text(0)?, nat(1)?, nat(2)?)),
            "net_send" => n(self.net.send(nat(0)?, &text(1)?)),
            "net_recv_line" => res(self.net.recv_line(nat(0)?).map(|l| RuntimeValue::OptionValue(l.map(|t| Box::new(RuntimeValue::Text(t)))))),
            "net_recv_all" => res(self.net.recv_all(nat(0)?).map(RuntimeValue::Text)),
            "net_shutdown" => n(self.net.shutdown(nat(0)?)),
            "net_close" => n(self.net.close(nat(0)?)),
            _ => return Err(runtime_error(line, format!("unknown ordinary builtin '{name}'"))),
        })
    }

    /// v0.125: a `loop` function. Self tail calls (the only recursive calls the
    /// checker admits) rebind the parameters and repeat, so a long-running
    /// loop uses constant stack, like the native `for (;;)` of the C backend.
    fn apply_tail_loop(&mut self, function: &FnDecl, mut values: Vec<RuntimeValue>, theory: &str) -> Result<RuntimeValue, Diagnostic> {
        self.call_names.push(function.name.clone());
        let result = loop {
            let frame: BTreeMap<String, RuntimeValue> = function.params.iter().map(|p| p.name.clone()).zip(values).collect();
            self.locals.push(frame);
            let step = self.eval_tail(&function.body, &function.name, theory);
            self.locals.pop();
            match step {
                Ok(TailStep::Again(next)) => values = next,
                Ok(TailStep::Done(v)) => break Ok(v),
                Err(e) => break Err(e),
            }
        };
        self.call_names.pop();
        result
    }

    fn eval_tail(&mut self, expr: &Expr, function: &str, theory: &str) -> Result<TailStep, Diagnostic> {
        let shadowed = |rt: &Self, name: &str| rt.locals.last().is_some_and(|f| f.contains_key(name));
        match &expr.kind {
            ExprKind::Call { name, args } if name == function && !shadowed(self, name) => {
                let mut values = Vec::with_capacity(args.len());
                for arg in args {
                    values.push(self.eval_expr(arg, theory)?);
                }
                Ok(TailStep::Again(values))
            }
            ExprKind::Call { name, args } if name == "if_then_else" && args.len() == 3 && !shadowed(self, name) => {
                let cond = self.eval_expr(&args[0], theory)?;
                if expect_bool(name, cond, expr.line)? {
                    self.eval_tail(&args[1], function, theory)
                } else {
                    self.eval_tail(&args[2], function, theory)
                }
            }
            ExprKind::Let { name, value, body } => {
                let value = self.eval_expr(value, theory)?;
                self.locals.last_mut().expect("loop frame").insert(name.clone(), value);
                self.eval_tail(body, function, theory)
            }
            _ => self.eval_expr(expr, theory).map(TailStep::Done),
        }
    }

    /// Graceful failure before the runtime thread stack is exhausted. The
    /// per-level stack cost depends on how deeply the function body nests
    /// expressions, so a depth limit alone cannot rule out an overflow abort.
    fn check_stack_budget(&self, function: &str, line: usize) -> Result<(), Diagnostic> {
        if self.stack_base == 0 {
            return Ok(());
        }
        let marker = 0u8;
        let used = self.stack_base.abs_diff(std::ptr::addr_of!(marker) as usize);
        let budget = super::RUNTIME_STACK_BYTES - super::RUNTIME_STACK_RESERVE_BYTES;
        if used > budget {
            return Err(runtime_error(
                line,
                format!(
                    "runtime stack budget exhausted while calling fn '{}' ({} MiB used)",
                    function,
                    used >> 20
                ),
            )
            .with_help("lower the recursion fuel or simplify the recursive body; the process is not aborted"));
        }
        Ok(())
    }

    /// Recursion fuel and measure decrease, checked on every call of a
    /// recursive function. The checker proves decrease statically; the runtime
    /// re-checks it (defense in depth) and enforces the operational fuel.
    fn check_recursion_step(&self, function: &FnDecl, measure: &FnMeasure, values: &[RuntimeValue], line: usize) -> Result<(), Diagnostic> {
        let fuel = measure.fuel.unwrap_or(crate::checker::DEFAULT_RECURSION_FUEL);
        let depth = self.fn_depth.get(&function.name).copied().unwrap_or(0);
        if depth >= fuel {
            return Err(runtime_error(
                line,
                format!("recursion fuel exhausted for fn '{}' (fuel {fuel})", function.name),
            )
            .with_help("the measure guarantees termination; fuel bounds operational depth. Raise it with: decreasing <param> fuel <N>"));
        }
        let is_direct_recursion = self.call_names.last() == Some(&function.name);
        if !is_direct_recursion {
            return Ok(());
        }
        // Lexicographic comparison of the measured tuple (v0.75); a simple
        // measure is a one-element tuple.
        let mut previous = Vec::with_capacity(measure.params.len());
        let mut next = Vec::with_capacity(measure.params.len());
        for measured in &measure.params {
            let index = function.params.iter().position(|p| &p.name == measured);
            previous.push(self.locals.last().and_then(|frame| frame.get(measured)).and_then(measure_size));
            next.push(index.and_then(|i| values.get(i)).and_then(measure_size));
        }
        // v0.126: record (enum) measures are structural; the checker proved the
        // decrease, and only the fuel is enforced here.
        if measure.params.iter().any(|m| {
            let index = function.params.iter().position(|p| &p.name == m);
            matches!(index.and_then(|i| values.get(i)), Some(RuntimeValue::Record { .. }))
        }) {
            return Ok(());
        }
        let previous: Option<Vec<u128>> = previous.into_iter().collect();
        let next: Option<Vec<u128>> = next.into_iter().collect();
        match (previous, next) {
            (Some(previous), Some(next)) if next < previous => Ok(()),
            (previous, next) => Err(runtime_error(
                line,
                format!(
                    "measure ({}) of fn '{}' did not strictly decrease: {:?} -> {:?}",
                    measure.params.join(", "),
                    function.name,
                    previous,
                    next
                ),
            )
            .with_help("unreachable for checked programs; indicates a checker/runtime mismatch")),
        }
    }

    #[inline(never)]
    fn eval_ordinary_structural(&mut self, name: &str, args: &[Expr], theory: &str, line: usize) -> Result<RuntimeValue, Diagnostic> {
        match name {
            "inl" | "inr" => {
                arity(name, args, 2, line)?;
                let value = self.eval_expr(&args[0], theory)?;
                Ok(RuntimeValue::SumValue { is_left: name == "inl", value: Box::new(value) })
            }
            "is_left" => {
                arity(name, args, 1, line)?;
                match self.eval_expr(&args[0], theory)? {
                    RuntimeValue::SumValue { is_left, .. } => Ok(RuntimeValue::Bool(is_left)),
                    other => Err(runtime_error(line, "is_left expects a Sum value")
                        .with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "case" => {
                arity(name, args, 3, line)?;
                match self.eval_expr(&args[0], theory)? {
                    RuntimeValue::SumValue { is_left, value } => {
                        let branch = if is_left { &args[1] } else { &args[2] };
                        let function = self.function_value(branch, theory, name, line)?;
                        self.apply_callable(&function, vec![*value], theory, line)
                    }
                    other => Err(runtime_error(line, "case expects a Sum value")
                        .with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            "field" => {
                arity(name, args, 2, line)?;
                let ExprKind::Ident(field_name) = &args[1].kind else {
                    return Err(runtime_error(line, "field expects a field name"));
                };
                match self.eval_expr(&args[0], theory)? {
                    RuntimeValue::Record { name: record, fields } => fields
                        .into_iter()
                        .find(|(field, _)| field == field_name)
                        .map(|(_, value)| value)
                        .ok_or_else(|| runtime_error(line, format!("record `{record}` has no field `{field_name}`"))),
                    other => Err(runtime_error(line, "field expects a record value")
                        .with_help(format!("runtime value was: {}", other.display_atom()))),
                }
            }
            other => Err(runtime_error(line, format!("unknown ordinary builtin '{other}'"))),
        }
    }

    pub(super) fn register_function(&mut self, theory: &str, decl: &FnDecl) {
        self.functions.entry(theory.to_string()).or_default().insert(decl.name.clone(), Arc::new(decl.clone()));
    }

    pub(super) fn lookup_local_or_function(&self, name: &str, theory: &str) -> Option<RuntimeValue> {
        if let Some(value) = self.locals.last().and_then(|frame| frame.get(name)) {
            return Some(value.clone());
        }
        self.scopes
            .get(theory)
            .and_then(|scope| scope.get(name))
            .cloned()
            .or_else(|| {
                self.functions
                    .get(theory)
                    .filter(|fns| fns.contains_key(name))
                    .map(|_| RuntimeValue::FnRef(name.to_string()))
            })
    }
}

#[cfg(test)]
mod file_slot_tests {
    use super::*;

    #[test]
    fn closed_file_slots_are_reused_without_retaining_file_state() {
        let expr = crate::parser::parse_expr(
            "let f = file_open(\"unused\", \"invalid\") in file_close(f)", 1,
        ).unwrap();
        let mut runtime = Runtime::new();
        for _ in 0..4096 {
            let value = runtime.eval_expr(&expr, "App").unwrap();
            assert!(matches!(value, RuntimeValue::ResultValue { is_ok: false, .. }));
        }
        assert_eq!(runtime.files.len(), 1);
        assert_eq!(runtime.free_files, [0]);
        let slot = &runtime.files[0];
        assert!(slot.closed && slot.path.is_empty() && slot.error.is_none());
        assert!(slot.file.is_none() && slot.pending.capacity() == 0);
    }
}

#[path = "effect_traversal.rs"]
mod effect_traversal;
