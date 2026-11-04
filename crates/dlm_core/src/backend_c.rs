//! v0.84 — C backend: IR -> one self-contained C file (the runtime from
//! `backend_c_runtime.c` plus generated functions), compiled by the system C
//! compiler (GCC/Clang with `unsigned __int128`; POSIX or native Windows).
//!
//! Uniform entry points use `DV *f(DV **env, DV **args)`; pure numeric
//! functions also have internal scalar entry points. Uniform frames are
//! arrays of slots; arguments of calls and builtins are evaluated into
//! per-call-site temporaries with the comma operator, so evaluation is
//! left-to-right as in `dlm run` (C leaves argument order unspecified).

use std::path::{Path, PathBuf};
use std::process::Command;

#[path = "backend_c_scalar.rs"]
mod scalar;

use crate::compile::{IrExpr, IrInit, IrOp, IrProgram};
use crate::diagnostics::{Diagnostic, DiagnosticKind};

/// The embedded C runtime.
pub const C_RUNTIME: &str = concat!(include_str!("backend_c_runtime.c"), "\n", include_str!("backend_c_embedding.c"));

/// C string literal for arbitrary bytes (octal escapes for everything that is
/// not plain printable ASCII, so no trigraph or escape can be formed).
fn c_string(text: &str) -> String {
    let mut out = String::from("\"");
    for byte in text.bytes() {
        if byte.is_ascii_alphanumeric() || b" .,:;!#$%&'()*+-/<=>@[]^_`{|}~".contains(&byte) {
            out.push(byte as char);
        } else {
            out.push_str(&format!("\\{byte:03o}"));
        }
    }
    out.push('"');
    out
}

struct Emitter<'a> {
    program: &'a IrProgram,
    temps: usize,
    /// File-scope field-name arrays of record types (shared by all functions).
    record_fields: &'a mut Vec<Vec<String>>,
}

impl Emitter<'_> {
    fn temp(&mut self) -> usize {
        self.temps += 1;
        self.temps - 1
    }

    /// Evaluate `args` into fresh consecutive temporaries; returns the
    /// sequencing prefix and the first temporary.
    fn sequence(&mut self, args: &[IrExpr]) -> (String, usize) {
        let first = self.temps;
        let indices: Vec<usize> = args.iter().map(|_| self.temp()).collect();
        let mut prefix = String::new();
        for (index, arg) in indices.iter().zip(args) {
            let code = self.expr(arg);
            prefix.push_str(&format!("t[{index}] = {code}, "));
        }
        (prefix, first)
    }

    fn function_value(&self, id: usize) -> String {
        let function = &self.program.functions[id];
        let display = format!("fn:{}", function.name.rsplit('.').next().unwrap_or(&function.name));
        format!("dv_closure(f{id}, {}, {}, 0, 0)", function.params.len(), c_string(&display))
    }

    fn expr(&mut self, expr: &IrExpr) -> String {
        match &expr.op {
            IrOp::Nat(n) => format!("dv_nat_lit({}ULL, {}ULL)", (n >> 64) as u64, *n as u64),
            IrOp::Int(i) => format!("dv_int_lit({}LL, {}ULL)", (*i >> 64) as i64, *i as u64),
            IrOp::Bool(true) => "DV_TRUE".to_string(),
            IrOp::Bool(false) => "DV_FALSE".to_string(),
            IrOp::Text(text) => format!("dv_text({}, {})", c_string(text), text.len()),
            IrOp::Local(slot) => format!("s[{slot}]"),
            IrOp::Global(index) => format!("dv_global_get(&G[{index}], {})", c_string(&self.program.globals[*index])),
            IrOp::FunctionValue(id) => self.function_value(*id),
            IrOp::Closure { function, captures } => {
                let lambda = &self.program.functions[*function];
                let display = format!("fun({})", lambda.params.join(", "));
                let values = if captures.is_empty() {
                    "0".to_string()
                } else {
                    format!("(DV *[]){{{}}}", captures.iter().map(|c| format!("s[{c}]")).collect::<Vec<_>>().join(", "))
                };
                format!("dv_closure(f{function}, {}, {}, {}, {values})", lambda.params.len(), c_string(&display), captures.len())
            }
            IrOp::Call { function, args } => {
                if args.is_empty() {
                    return format!("f{function}(0, 0)");
                }
                let (prefix, first) = self.sequence(args);
                format!("({prefix}f{function}(0, &t[{first}]))")
            }
            IrOp::Apply { callee, args } => {
                let mut all = vec![(**callee).clone()];
                all.extend(args.iter().cloned());
                let (prefix, first) = self.sequence(&all);
                format!("({prefix}dv_apply(t[{first}], {}, &t[{}]))", args.len(), first + 1)
            }
            IrOp::Builtin { name, args } => {
                if args.is_empty() {
                    return format!("dvb_{name}()");
                }
                let (prefix, first) = self.sequence(args);
                let params: Vec<String> = (0..args.len()).map(|i| format!("t[{}]", first + i)).collect();
                if name == "list" || name == "sequence" {
                    return format!("({prefix}dv_list(&t[{first}], {}, {}))", args.len(), u8::from(name == "sequence"));
                }
                format!("({prefix}dvb_{name}({}))", params.join(", "))
            }
            IrOp::Empty("none") => "DV_NONE".to_string(),
            IrOp::Empty("empty_sequence") => "dv_list(0, 0, 1)".to_string(),
            IrOp::Empty(_) => "dv_list(0, 0, 0)".to_string(),
            IrOp::Add(a, b) => self.binary("dvb_add", a, b),
            IrOp::Gt(a, b) => self.binary("dvb_gt", a, b),
            IrOp::Pow(a, b) => self.binary("dvb_pow", a, b),
            IrOp::If(c, a, b) => format!("(dv_truth({}) ? {} : {})", self.expr(c), self.expr(a), self.expr(b)),
            IrOp::And(a, b) => format!("(dv_truth({}) ? dv_bool(dv_truth({})) : DV_FALSE)", self.expr(a), self.expr(b)),
            IrOp::Or(a, b) => format!("(dv_truth({}) ? DV_TRUE : dv_bool(dv_truth({})))", self.expr(a), self.expr(b)),
            IrOp::UnwrapOr(a, b) => {
                let t = self.temp();
                format!("(t[{t}] = {}, dv_has_value(t[{t}]) ? dv_inner(t[{t}]) : {})", self.expr(a), self.expr(b))
            }
            IrOp::Let { slot, value, body } => format!("(s[{slot}] = {}, {})", self.expr(value), self.expr(body)),
            IrOp::Inject { left, value } => format!("dv_inject({}, {})", self.expr(value), u8::from(*left)),
            IrOp::Case(s, a, b) => {
                let t = self.temp();
                format!("(t[{t}] = {}, dv_sum_left(t[{t}]) ? dv_case(t[{t}], {}) : dv_case(t[{t}], {}))", self.expr(s), self.expr(a), self.expr(b))
            }
            IrOp::Record { name, fields, .. } => {
                let values: Vec<IrExpr> = fields.iter().map(|(_, v)| v.clone()).collect();
                let (prefix, first) = self.sequence(&values);
                // Field names live in a file-scope array: records outlive frames.
                let names: Vec<String> = fields.iter().map(|(n, _)| n.clone()).collect();
                let table = match self.record_fields.iter().position(|existing| *existing == names) {
                    Some(index) => index,
                    None => {
                        self.record_fields.push(names);
                        self.record_fields.len() - 1
                    }
                };
                format!("({prefix}dv_record({}, {}, R{table}, &t[{first}]))", c_string(name), fields.len())
            }
            IrOp::Field { record, layout, field, .. } => {
                let name = &self.program.records[layout.raw() as usize].fields[*field].0;
                format!("dv_field({}, {})", self.expr(record), c_string(name))
            }
        }
    }

    // Self tail calls reuse the current region and frame. Argument graphs are
    // retained before old temporaries are freed, including aliased captures.
    fn tail(&mut self, expr: &IrExpr, id: usize, captures: usize) -> String {
        match &expr.op {
            IrOp::Call { function, args } if *function == id => {
                let (prefix, first) = self.sequence(args);
                let mut out = if prefix.is_empty() { String::new() } else { format!("    (void)({prefix}0);\n") };
                out.push_str(&format!("    dv_region_finish_many(&region, &t[{first}], {}, 0);\n", args.len()));
                for i in 0..args.len() {
                    out.push_str(&format!("    s[{}] = t[{}];\n", captures + i, first + i));
                }
                out.push_str("    continue;\n");
                out
            }
            IrOp::Let { slot, value, body } => {
                let value = self.expr(value);
                format!("    s[{slot}] = {value};\n{}", self.tail(body, id, captures))
            }
            IrOp::If(c, a, b) => {
                let c = self.expr(c);
                let a = self.tail(a, id, captures);
                let b = self.tail(b, id, captures);
                format!("    if (dv_truth({c})) {{\n{a}    }} else {{\n{b}    }}\n")
            }
            _ => format!("    return dv_region_leave(&region, {});\n", self.expr(expr)),
        }
    }

    fn binary(&mut self, function: &str, a: &IrExpr, b: &IrExpr) -> String {
        let (prefix, first) = self.sequence(&[a.clone(), b.clone()]);
        format!("({prefix}{function}(t[{first}], t[{}]))", first + 1)
    }
}

fn has_self_tail_call(expr: &IrExpr, id: usize) -> bool {
    match &expr.op {
        IrOp::Call { function, .. } => *function == id,
        IrOp::Let { body, .. } => has_self_tail_call(body, id),
        IrOp::If(_, a, b) => has_self_tail_call(a, id) || has_self_tail_call(b, id),
        _ => false,
    }
}

/// v0.87: ids of the functions emitted with native scalar entry points.
pub fn scalar_functions(program: &IrProgram) -> std::collections::BTreeSet<usize> {
    scalar::ScalarBackend::new(program).wrappers.keys().copied().collect()
}

/// Generate the complete C program for `program`.
pub fn emit_c(program: &IrProgram) -> String {
    if !program.native.is_empty() { return crate::native::emit::emit(&program.native, true); }
    let mut out = String::new();
    out.push_str(&format!("/* Generated by dlm build {} from module {} */\n", env!("CARGO_PKG_VERSION"), program.module));
    out.push_str(C_RUNTIME);
    out.push_str("\n/* ---- program ---- */\n\n");
    out.push_str(&format!("static DV *G[{}];\n", program.globals.len().max(1)));
    for (id, function) in program.functions.iter().enumerate() {
        out.push_str(&format!("static DV *f{id}(DV **env, DV **args); /* {} */\n", function.name));
    }
    out.push('\n');
    let scalar = scalar::ScalarBackend::new(program);
    out.push_str(&scalar.declarations);
    out.push_str(&scalar.definitions);
    let mut record_fields: Vec<Vec<String>> = Vec::new();
    let mut functions = String::new();
    for (id, function) in program.functions.iter().enumerate() {
        if let Some(wrapper) = scalar.wrappers.get(&id) {
            functions.push_str(wrapper);
            continue;
        }
        let mut emitter = Emitter { program, temps: 0, record_fields: &mut record_fields };
        let tail = has_self_tail_call(&function.body, id);
        let body = if tail { emitter.tail(&function.body, id, function.captures) } else { emitter.expr(&function.body) };
        let temps = emitter.temps;
        let out = &mut functions;
        out.push_str(&format!("/* {} */\nstatic DV *f{id}(DV **env, DV **args) {{\n", function.name));
        out.push_str(&format!("    DV *s[{}];\n    DV *t[{}];\n", function.slots.max(1), temps.max(1)));
        out.push_str("    dv_region region;\n    DV_STACK_CHECK();\n    dv_region_enter(&region);\n    (void)env; (void)args; (void)s; (void)t;\n");
        for capture in 0..function.captures {
            out.push_str(&format!("    s[{capture}] = env[{capture}];\n"));
        }
        for param in 0..function.params.len() {
            out.push_str(&format!("    s[{}] = args[{param}];\n", function.captures + param));
        }
        if tail {
            out.push_str(&format!("    for (;;) {{\n{body}    }}\n}}\n\n"));
        } else {
            out.push_str(&format!("    return dv_region_leave(&region, {body});\n}}\n\n"));
        }
    }
    let mut entry = String::from("static void dlm_program(void) {\n");
    for init in &program.init {
        let mut emitter = Emitter { program, temps: 0, record_fields: &mut record_fields };
        let out = &mut entry;
        match init {
            IrInit::Global { index, expr } => {
                let code = emitter.expr(expr);
                out.push_str(&format!("    {{ DV *t[{}]; (void)t; dv_region region; dv_region_enter(&region); G[{index}] = {code}; dv_region_leave(&region, G[{index}]); }}\n", emitter.temps.max(1)));
            }
            IrInit::Effect(expr) => {
                let code = emitter.expr(expr);
                out.push_str(&format!("    {{ DV *t[{}]; (void)t; dv_region region; dv_region_enter(&region); (void){code}; dv_region_leave(&region, 0); }}\n", emitter.temps.max(1)));
            }
        }
    }
    if let Some(main) = program.main {
        entry.push_str(&format!(
            "    {{ DV *r = f{main}(0, 0); if (dv_tag(r) == DV_NAT) dv_exit_status = dv_nat_value(r) > 255 ? 255 : (int)dv_nat_value(r); }}\n"
        ));
    }
    entry.push_str("}\n");
    for (index, names) in record_fields.iter().enumerate() {
        let names = names.iter().map(|n| c_string(n)).collect::<Vec<_>>().join(", ");
        out.push_str(&format!("static const char *const R{index}[] = {{{names}}};\n"));
    }
    out.push('\n');
    out.push_str(&functions);
    out.push_str(&entry);
    out
}

fn build_error(message: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::CompileError, None, message)
}

/// The C compiler: `$CC`, else native Windows MinGW-w64 `gcc` or POSIX `cc`.
pub fn default_c_compiler() -> String {
    std::env::var("CC").ok().filter(|cc| !cc.trim().is_empty()).unwrap_or_else(|| if cfg!(windows) { "gcc" } else { "cc" }.to_string())
}

/// Compile C source to an executable with `cc -O2`.
pub fn build_c_executable(c_source: &str, c_path: &Path, output: &Path, compiler: &str) -> Result<PathBuf, Diagnostic> {
    build_c_executable_with_flags(c_source, c_path, output, compiler, &[])
}

/// v0.108: as [`build_c_executable`] with extra compiler/linker flags after the
/// defaults (sanitizers, a Windows stack reserve; numeric acceptance, N16).
pub fn build_c_executable_with_flags(c_source: &str, c_path: &Path, output: &Path, compiler: &str, flags: &[&str]) -> Result<PathBuf, Diagnostic> {
    std::fs::write(c_path, c_source).map_err(|err| build_error(format!("cannot write {}: {err}", c_path.display())))?;
    // Query the target rather than the host: a Linux MinGW cross compiler
    // must receive Windows libraries too. Do not parse compiler stderr as flags.
    let target = Command::new(compiler).arg("-dumpmachine").output()
        .map_err(|err| build_error(format!("cannot run C compiler '{compiler}': {err}")).with_help("install 64-bit GCC/Clang (MinGW-w64 on Windows), or set CC"))?;
    if !target.status.success() {
        return Err(build_error(format!("C compiler '{compiler}' does not support GCC-compatible target detection")));
    }
    let target = String::from_utf8_lossy(&target.stdout);
    let windows = target.contains("mingw") || target.contains("windows");
    let output = if windows && output.extension().is_none() { output.with_extension("exe") } else { output.to_path_buf() };
    let mut command = Command::new(compiler);
    command.args(["-O2", "-std=gnu11", "-w", "-ffp-contract=off", "-o"]).arg(&output).arg(c_path);
    if windows { command.args(["-lshell32", "-lws2_32", "-lgdi32", "-luser32"]); } else { command.args(["-pthread", "-lm", "-ldl"]); }
    command.args(flags);
    let result = command.output()
        .map_err(|err| build_error(format!("cannot run C compiler '{compiler}': {err}")).with_help("install gcc or clang, or set CC"))?;
    if !result.status.success() {
        return Err(build_error(format!(
            "C compiler '{compiler}' failed:\n{}",
            String::from_utf8_lossy(&result.stderr).lines().take(20).collect::<Vec<_>>().join("\n")
        )));
    }
    Ok(output)
}
