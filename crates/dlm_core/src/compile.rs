//! v0.84 — compilation: a checked program is lowered to a small, name-resolved
//! intermediate representation (IR), which a backend turns into native code
//! (`backend_c.rs`).
//!
//! v0.87: the IR is typed. Every node carries the type the checker established
//! for its source expression (`TypedFacts`, keyed by `ExprId`) and its exact
//! source span; functions carry their checked signature, effects and
//! termination status. `verify_ir` checks the result after lowering. Lowering
//! resolves names and makes evaluation order and closures explicit:
//!
//! - statements (theorems, axioms, conjectures) are **erased**: their proofs
//!   were checked and are not needed to run the program; a statement used as a
//!   value is a compile error;
//! - recursion measures and fuel are **erased**: termination was established by
//!   the checker (well-founded measures), so compiled code recurses natively;
//! - names are resolved to local slots, theory globals, functions or builtins;
//! - lambdas become separate functions with an explicit list of captured slots;
//! - imported modules contribute functions only, exactly as in `dlm run`.
//!
//! Only the ordinary surface is compiled. Constructs without a backend
//! implementation (GPU/remote/meta values, bridges) are reported
//! as `E0950 CompileError` instead of being miscompiled.

use std::collections::BTreeMap;
use std::fmt;

use crate::ast::*;
use crate::checker::Checker;
use crate::diagnostics::{Diagnostic, DiagnosticKind, SourceFile, SourceSpan};
use crate::ordinary::OrdinaryType;
use crate::typed_core::{DeclarationOrigin, SourceDeclarationKind, TypedFacts};
use crate::policy::CheckPolicy;
use crate::program::Program;
use crate::ids::{DeclId, RecordId};

/// Builtins the backend implements, with their source arity (`None` = variadic).
/// Type arguments are erased before IR verification (`ok` / `err`: one operand).
pub const COMPILED_BUILTINS: &[(&str, Option<usize>)] = &[
    ("effect_fn", Some(1)), ("map_effect", Some(2)), ("fold_effect", Some(3)),
    ("each_effect", Some(2)), ("filter_effect", Some(2)), ("find_effect", Some(2)),
    ("any_effect", Some(2)), ("all_effect", Some(2)), ("try_map_effect", Some(2)), ("try_each_effect", Some(2)),
    ("not", Some(1)),
    ("nat_eq", Some(2)),
    ("mul", Some(2)),
    ("monus", Some(2)),
    ("pred", Some(1)),
    ("some", Some(1)),
    ("ok", Some(2)),
    ("err", Some(2)),
    ("is_some", Some(1)),
    ("is_none", Some(1)),
    ("is_ok", Some(1)),
    ("is_left", Some(1)),
    ("int", Some(1)),
    ("abs", Some(1)),
    ("int_eq", Some(2)),
    ("div", Some(2)),
    ("mod", Some(2)),
    ("int_to_text", Some(1)),
    ("text_to_int", Some(1)),
    ("__sub", Some(2)),
    ("__neg", Some(1)),
    ("is_err", Some(1)),
    ("list", None),
    ("sequence", None),
    ("length", Some(1)),
    ("index", Some(2)),
    ("head", Some(1)),
    ("tail", Some(1)),
    ("is_empty", Some(1)),
    ("cons", Some(2)),
    ("__match_head", Some(1)),
    ("__match_some", Some(1)),
    ("__match_variant", Some(2)),
    ("__enum_payload", Some(2)),
    ("__match_ok", Some(1)),
    ("__match_err", Some(1)),
    ("pair", Some(2)),
    ("fst", Some(1)),
    ("snd", Some(1)),
    ("map", Some(2)),
    ("fold", Some(3)),
    ("concat", Some(2)),
    ("text_length", Some(1)),
    // v0.102: owned byte buffers and the release inserted by lowering.
    ("buf_new", Some(2)),
    ("buf_from_list", Some(1)),
    ("buf_len", Some(1)),
    ("buf_get", Some(2)),
    ("buf_set", Some(3)),
    ("buf_push", Some(2)),
    ("buf_to_list", Some(1)),
    ("buf_free", Some(1)),
    ("buf_copy", Some(1)),
    ("buf_slice", Some(3)),
    ("buf_append", Some(2)),
    ("buf_eq", Some(2)),
    ("buf_from_text", Some(1)),
    ("buf_to_text", Some(1)),
    ("tbuf_new", Some(0)),
    ("tbuf_push", Some(2)),
    ("tbuf_len", Some(1)),
    ("tbuf_finish", Some(1)),
    // v0.114: machine floats and float vectors.
    ("fl_of_nat", Some(1)),
    ("fl_of_int", Some(1)),
    ("fl_lit", Some(1)),
    ("fl_parse", Some(1)),
    ("fl_add", Some(2)),
    ("fl_sub", Some(2)),
    ("fl_mul", Some(2)),
    ("fl_div", Some(2)),
    ("fl_neg", Some(1)),
    ("fl_abs", Some(1)),
    ("fl_min", Some(2)),
    ("fl_max", Some(2)),
    ("fl_sqrt", Some(1)),
    ("fl_exp", Some(1)),
    ("fl_log", Some(1)),
    ("fl_sin", Some(1)),
    ("fl_cos", Some(1)),
    ("fl_tan", Some(1)),
    ("fl_atan", Some(1)),
    ("fl_atan2", Some(2)),
    ("fl_pow", Some(2)),
    ("fl_floor", Some(1)),
    ("fl_ceil", Some(1)),
    ("fl_round", Some(1)),
    ("fl_trunc", Some(1)),
    ("fl_lt", Some(2)),
    ("fl_le", Some(2)),
    ("fl_eq", Some(2)),
    ("fl_is_nan", Some(1)),
    ("fl_is_finite", Some(1)),
    ("fl_to_text", Some(1)),
    ("fl_to_int", Some(1)),
    ("fl_bits", Some(1)),
    ("fl_from_bits", Some(1)),
    ("fv_new", Some(2)),
    ("fv_from_list", Some(1)),
    ("fv_to_list", Some(1)),
    ("fv_len", Some(1)),
    ("fv_get", Some(2)),
    ("fv_set", Some(3)),
    ("fv_add", Some(2)),
    ("fv_sub", Some(2)),
    ("fv_mul", Some(2)),
    ("fv_scale", Some(2)),
    ("fv_axpy", Some(3)),
    ("fv_dot", Some(2)),
    ("fv_sum", Some(1)),
    ("fv_norm", Some(1)),
    ("fv_max", Some(1)),
    ("fv_min", Some(1)),
    ("fv_slice", Some(3)),
    ("fv_concat", Some(2)),
    ("fv_matmul", Some(5)),
    ("fv_transpose", Some(3)),
    ("fv_empty", Some(0)),
    ("fv_solve", Some(3)),
    // v0.115: natural-number vectors and graphs.
    ("nv_empty", Some(0)),
    ("nv_new", Some(2)),
    ("nv_from_list", Some(1)),
    ("nv_to_list", Some(1)),
    ("nv_len", Some(1)),
    ("nv_get", Some(2)),
    ("nv_set", Some(3)),
    ("nv_push", Some(2)),
    ("nv_slice", Some(3)),
    ("nv_concat", Some(2)),
    ("nv_range", Some(2)),
    ("nv_sum", Some(1)),
    ("nv_max", Some(1)),
    ("nv_min", Some(1)),
    ("nv_sort", Some(1)),
    ("nv_reverse", Some(1)),
    ("nv_find", Some(2)),
    ("nv_to_fv", Some(1)),
    ("gr_bfs", Some(4)),
    ("gr_dfs", Some(4)),
    ("gr_toposort", Some(3)),
    ("gr_components", Some(3)),
    ("gr_scc", Some(3)),
    ("gr_bipartite", Some(3)),
    ("gr_dijkstra", Some(5)),
    ("gr_bellman_ford", Some(5)),
    ("gr_floyd", Some(4)),
    ("gr_mst", Some(4)),
    ("gr_maxflow", Some(6)),
    // v0.116: numerical linear algebra.
    ("la_lu", Some(2)),
    ("la_lu_perm", Some(2)),
    ("la_det", Some(2)),
    ("la_inverse", Some(2)),
    ("la_cholesky", Some(2)),
    ("la_qr_q", Some(3)),
    ("la_qr_r", Some(3)),
    ("la_lstsq", Some(4)),
    ("la_eigh_values", Some(2)),
    ("la_eigh_vectors", Some(2)),
    ("la_svd_values", Some(3)),
    ("la_rank", Some(4)),
    ("la_identity", Some(1)),
    ("la_trace", Some(2)),
    ("la_csr_matvec", Some(4)),
    ("la_cg", Some(6)),
    ("fft_forward", Some(1)),
    ("fft_inverse", Some(1)),
    ("fft_convolve", Some(2)),
    ("lp_solve", Some(5)),
    ("lp_status", Some(5)),
    ("nt_gcd", Some(2)),
    ("nt_lcm", Some(2)),
    ("nt_mod_mul", Some(3)),
    ("nt_mod_pow", Some(3)),
    ("nt_mod_inv", Some(2)),
    ("nt_is_prime", Some(1)),
    ("nt_next_prime", Some(1)),
    ("nt_factor", Some(1)),
    ("nt_totient", Some(1)),
    ("nt_divisors", Some(1)),
    ("nt_primes", Some(1)),
    ("nt_crt", Some(2)),
    ("nt_sqrt_mod", Some(2)),
    ("nt_isqrt", Some(1)),
    ("pf_add", Some(3)),
    ("pf_sub", Some(3)),
    ("pf_mul", Some(3)),
    ("pf_div", Some(3)),
    ("pf_rem", Some(3)),
    ("pf_gcd", Some(3)),
    ("pf_powmod", Some(4)),
    ("pf_eval", Some(3)),
    ("pf_irreducible", Some(2)),
    ("pm_compose", Some(2)),
    ("pm_inverse", Some(1)),
    ("pm_power", Some(2)),
    ("pm_order", Some(1)),
    ("pm_sign", Some(1)),
    ("pm_cycle_type", Some(1)),
    ("pm_orbit", Some(3)),
    ("pm_group_order", Some(2)),
    ("sat_solve", Some(3)),
    ("sat_status", Some(3)),
    ("tp_homology", Some(2)),
    ("tp_rips", Some(4)),
    ("gb_basis", Some(5)),
    ("gb_reduce", Some(6)),
    ("mv_add", Some(4)),
    ("mv_sub", Some(4)),
    ("mv_mul", Some(4)),
    ("mv_normalize", Some(4)),
    ("sat_proof", Some(3)),
    ("sat_check", Some(4)),
    ("sat_count", Some(3)),
    ("ilp_solve", Some(6)),
    ("ilp_status", Some(6)),
    ("km_empty", Some(0)),
    ("km_insert", Some(3)),
    ("km_get", Some(2)),
    ("km_remove", Some(2)),
    ("km_contains", Some(2)),
    ("km_len", Some(1)),
    ("km_keys", Some(1)),
    ("km_values", Some(1)),
    ("km_from", Some(2)),
    ("km_add", Some(3)),
    ("km_min", Some(1)),
    ("km_max", Some(1)),
    ("km_floor", Some(2)),
    ("km_ceil", Some(2)),
    ("km_merge", Some(2)),
    ("km_range", Some(3)),
    ("tm_empty", Some(0)),
    ("tm_insert", Some(3)),
    ("tm_get", Some(2)),
    ("tm_remove", Some(2)),
    ("tm_contains", Some(2)),
    ("tm_len", Some(1)),
    ("tm_keys", Some(1)),
    ("tm_values", Some(1)),
    ("tm_from", Some(2)),
    ("tm_add", Some(3)),
    ("tm_merge", Some(2)),
    ("tm_count_words", Some(1)),
    ("la_pcg", Some(6)),
    ("la_bicgstab", Some(6)),
    ("la_gmres", Some(7)),
    ("geo_orient2d", Some(1)),
    ("geo_incircle", Some(1)),
    ("geo_orient3d", Some(1)),
    ("geo_delaunay", Some(1)),
    ("kd_knn", Some(4)),
    ("kd_radius", Some(4)),
    ("fft2_forward", Some(3)),
    ("fft2_inverse", Some(3)),
    ("sig_stft", Some(3)),
    ("sig_dwt", Some(3)),
    ("sig_idwt", Some(3)),
    ("sig_iir", Some(3)),
    ("sig_sha256", Some(1)),
    ("bvh_raycast", Some(3)),
    ("bvh_winding", Some(2)),
    ("bvh_pairs", Some(1)),
    ("gx_new", Some(3)),
    ("gx_width", Some(1)),
    ("gx_height", Some(1)),
    ("gx_get", Some(3)),
    ("gx_fill_rect", Some(6)),
    ("gx_line", Some(6)),
    ("gx_circle", Some(6)),
    ("gx_text", Some(6)),
    ("gx_text_width", Some(2)),
    ("gx_blit", Some(4)),
    ("gx_checksum", Some(1)),
    ("vx_render", Some(5)),
    ("vx_render_region", Some(8)),
    ("vx_bounds", Some(3)),
    ("vx_text_width", Some(2)),
    ("vx_svg", Some(3)),
    ("vx_text", Some(6)),
    ("vx_text_fonts", Some(7)),
    ("vx_font_width", Some(3)),
    ("vx_font_has", Some(2)),
    ("vx_font_advance", Some(3)),
    ("vx_font_metrics", Some(1)),
    ("vx_codepoints", Some(1)),
    ("vx_from_codepoints", Some(1)),
    ("file_font_load", Some(1)),
    ("file_font_find", Some(1)),
    // v0.111: structural equality and order (generic bounds Eq, Ord).
    ("equal", Some(2)),
    ("less", Some(2)),
    // v0.109: file handles.
    ("file_open", Some(2)),
    ("file_ok", Some(1)),
    ("file_error", Some(1)),
    ("file_write", Some(2)),
    ("file_write_bytes", Some(2)),
    ("file_read_line", Some(1)),
    ("file_read_all", Some(1)),
    ("file_read_bytes", Some(2)),
    ("file_flush", Some(1)),
    ("file_close", Some(1)),
    ("file_remove", Some(1)),
    ("file_set_buffered", Some(2)),
    ("file_seek", Some(2)),
    ("file_tell", Some(1)),
    ("file_size", Some(1)),
    ("file_truncate", Some(2)),
    ("file_exists", Some(1)),
    ("file_is_dir", Some(1)),
    ("file_size_of", Some(1)),
    ("file_mtime", Some(1)),
    ("file_list_dir", Some(1)),
    ("file_walk", Some(1)),
    ("file_make_dir", Some(1)),
    ("file_remove_dir", Some(1)),
    ("file_rename", Some(2)),
    ("file_copy", Some(2)),
    ("file_write_atomic", Some(2)),
    ("file_write_png", Some(2)),
    ("proc_run", Some(3)),
    ("proc_env", Some(1)),
    ("net_resolve", Some(1)),
    ("net_listen", Some(2)),
    ("net_local_port", Some(1)),
    ("net_accept", Some(2)),
    ("net_connect", Some(3)),
    ("net_send", Some(2)),
    ("net_recv_line", Some(1)),
    ("net_recv_all", Some(1)),
    ("net_shutdown", Some(1)),
    ("net_close", Some(1)),
    ("proc_spawn", Some(3)),
    ("proc_poll", Some(1)),
    ("proc_wait", Some(1)),
    ("proc_kill", Some(1)),
    ("win_open", Some(3)),
    ("win_present", Some(2)),
    ("win_poll", Some(2)),
    ("win_size", Some(1)),
    ("win_close", Some(1)),
    ("win_gpu", Some(1)),
    ("win_present_scene", Some(6)),
    ("win_read", Some(1)),
    // v0.107: dense natural numbers.
    ("bn_from_nat", Some(1)),
    ("bn_to_nat", Some(1)),
    ("bn_from_words", Some(1)),
    ("bn_to_words", Some(1)),
    ("bn_from_text", Some(1)),
    ("bn_to_text", Some(1)),
    ("bn_bits", Some(1)),
    ("bn_limbs", Some(1)),
    ("bn_cmp", Some(2)),
    ("bn_add", Some(2)),
    ("bn_monus", Some(2)),
    ("bn_mul", Some(2)),
    ("bn_div", Some(2)),
    ("bn_mod", Some(2)),
    ("bn_shl", Some(2)),
    ("bn_shr", Some(2)),
    ("bn_gcd", Some(2)),
    ("bn_sqrt", Some(1)),
    ("bn_powmod", Some(3)),
    ("bn_modinv", Some(2)),
    ("bn_is_prime", Some(1)),
    ("bn_next_prime", Some(1)),
    ("bn_random", Some(2)),
    ("bn_to_hex", Some(1)),
    ("bn_from_hex", Some(1)),
    ("__release", Some(2)),
    ("text_eq", Some(2)),
    ("nat_to_text", Some(1)),
    ("text_to_nat", Some(1)),
    ("substring", Some(3)),
    ("lines", Some(1)),
    ("words", Some(1)),
    ("join", Some(2)),
    ("print_line", Some(1)),
    ("print_text", Some(1)),
    ("print_decimal", Some(1)),
    ("print_symbolic", Some(1)),
    ("read_line", Some(0)),
    ("read_all", Some(0)),
    ("read_nat", Some(0)),
    ("args", Some(0)),
];

/// Operation of an IR node.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum IrOp {
    Nat(u128),
    /// v0.85: Int constant (`-5`).
    Int(i128),
    Bool(bool),
    Text(String),
    /// Slot of the current function frame.
    Local(usize),
    /// Theory-level value (`let`), by global index.
    Global(usize),
    /// A function used as a value (closure without captures).
    FunctionValue(usize),
    /// Lambda: function id and the slots it captures, in order.
    Closure { function: usize, captures: Vec<usize> },
    /// Direct call of a function.
    Call { function: usize, args: Vec<IrExpr> },
    /// Call of a function value.
    Apply { callee: Box<IrExpr>, args: Vec<IrExpr> },
    Builtin { name: String, args: Vec<IrExpr> },
    /// `none(T)`, `empty_list(T)`, `empty_sequence(T)` (the type is the node's).
    Empty(&'static str),
    Add(Box<IrExpr>, Box<IrExpr>),
    Gt(Box<IrExpr>, Box<IrExpr>),
    Pow(Box<IrExpr>, Box<IrExpr>),
    /// Lazy conditional, `and`, `or`, `unwrap_or`.
    If(Box<IrExpr>, Box<IrExpr>, Box<IrExpr>),
    And(Box<IrExpr>, Box<IrExpr>),
    Or(Box<IrExpr>, Box<IrExpr>),
    UnwrapOr(Box<IrExpr>, Box<IrExpr>),
    /// `slot := value; body`.
    Let { slot: usize, value: Box<IrExpr>, body: Box<IrExpr> },
    /// `inl(v, T)` / `inr(v, T)` (the type is the node's).
    Inject { left: bool, value: Box<IrExpr> },
    /// `case(sum, f, g)`: only the selected function value is evaluated.
    Case(Box<IrExpr>, Box<IrExpr>, Box<IrExpr>),
    /// Record construction, fields in declaration order.
    Record { record: RecordId, name: String, fields: Vec<(String, IrExpr)> },
    /// `field(record, name)`.
    Field { record: Box<IrExpr>, layout: RecordId, field: usize, name: String },
}

/// v0.87: a node of the typed IR (Typed Core lowered to execution order).
/// `ty` is the type the checker established for the source expression, never
/// reconstructed by the backend; `None` marks a node the checker did not type,
/// which `verify_ir` rejects.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct IrExpr {
    pub op: IrOp,
    pub ty: Option<OrdinaryType>,
    pub span: SourceSpan,
    /// Source expression this node was lowered from (`None` for synthetic
    /// nodes such as initializer calls).
    pub origin: Option<ExprId>,
}

impl IrExpr {
    pub fn new(op: IrOp, ty: Option<OrdinaryType>, span: SourceSpan) -> Self {
        Self { op, ty, span, origin: None }
    }
}

/// A compiled function: a top-level `fn`, a lambda or a theory initializer.
#[derive(Debug, Clone)]
pub struct IrFunction {
    /// None for lifted lambdas and synthetic initializers.
    pub declaration: Option<DeclId>,
    /// `Theory.name` for functions, `Theory.lambdaN@line` for lambdas,
    /// `Theory.<initN>` for theory-level code. The stable identity of an
    /// exported function is `module::Theory.name` (`symbol`).
    pub name: String,
    /// `module::Theory.name`, or empty for lambdas and initializers.
    pub symbol: String,
    /// Parameter names (display of function values).
    pub params: Vec<String>,
    /// v0.87: checked parameter types.
    pub param_types: Vec<OrdinaryType>,
    /// v0.87: checked result type.
    pub ret: Option<OrdinaryType>,
    /// Captured values (lambdas only); they occupy the first slots.
    pub captures: usize,
    /// Total frame slots: captures, then parameters, then `let`s.
    pub slots: usize,
    /// v0.87: type of every slot.
    pub slot_types: Vec<Option<OrdinaryType>>,
    pub body: IrExpr,
    pub is_lambda: bool,
    /// v0.87: effects of the body (`kind@boundary`), from the checked contract.
    pub effects: Vec<String>,
    /// v0.87: termination established by the checker (see `FunctionFacts`).
    pub termination: String,
    /// v0.87: source line of the declaration.
    pub line: usize,
    /// v0.103: slots of `&ByteBuf` parameters (borrowed, never released here).
    pub borrowed: Vec<usize>,
}

/// Theory-level evaluation, in program order.
#[derive(Debug, Clone)]
pub enum IrInit {
    /// `let name = expr` of a theory.
    Global { index: usize, expr: IrExpr },
    /// A theory-level expression evaluated for its effects.
    Effect(IrExpr),
}

#[derive(Debug, Clone)]
pub struct IrProgram {
    pub module: String,
    pub sources: Vec<SourceFile>,
    /// Checked source locations, keyed by compilation-local expression ID.
    pub source_spans: BTreeMap<ExprId, SourceSpan>,
    pub declarations: BTreeMap<DeclId, DeclarationOrigin>,
    pub functions: Vec<IrFunction>,
    /// `Theory.name` of every global.
    pub globals: Vec<String>,
    /// v0.87: checked type of every global.
    pub global_types: Vec<Option<OrdinaryType>>,
    pub init: Vec<IrInit>,
    /// Function id of `main()`, if the root module declares one.
    pub main: Option<usize>,
    /// Number of statements erased.
    pub erased_statements: usize,
    /// Checked record layouts, addressed by compilation-local RecordId.
    pub records: Vec<IrRecord>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct IrRecord {
    pub declaration: DeclId,
    pub name: String,
    pub symbol: String,
    pub fields: Vec<(String, OrdinaryType)>,
    pub span: SourceSpan,
}

impl fmt::Display for IrExpr {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let list = |items: &[IrExpr]| items.iter().map(ToString::to_string).collect::<Vec<_>>().join(", ");
        match &self.op {
            IrOp::Nat(n) => write!(f, "{n}"),
            IrOp::Int(i) => write!(f, "int({i})"),
            IrOp::Bool(b) => write!(f, "{b}"),
            IrOp::Text(t) => write!(f, "{t:?}"),
            IrOp::Local(s) => write!(f, "%{s}"),
            IrOp::Global(g) => write!(f, "@g{g}"),
            IrOp::FunctionValue(id) => write!(f, "&f{id}"),
            IrOp::Closure { function, captures } => {
                write!(f, "closure(f{function}; {})", captures.iter().map(|c| format!("%{c}")).collect::<Vec<_>>().join(", "))
            }
            IrOp::Call { function, args } => write!(f, "f{function}({})", list(args)),
            IrOp::Apply { callee, args } => write!(f, "apply({callee}; {})", list(args)),
            IrOp::Builtin { name, args } => write!(f, "{name}({})", list(args)),
            IrOp::Empty(kind) => write!(f, "{kind}"),
            IrOp::Add(a, b) => write!(f, "({a} + {b})"),
            IrOp::Gt(a, b) => write!(f, "({a} > {b})"),
            IrOp::Pow(a, b) => write!(f, "({a} ^ {b})"),
            IrOp::If(c, a, b) => write!(f, "if {c} then {a} else {b}"),
            IrOp::And(a, b) => write!(f, "({a} and {b})"),
            IrOp::Or(a, b) => write!(f, "({a} or {b})"),
            IrOp::UnwrapOr(a, b) => write!(f, "unwrap_or({a}, {b})"),
            IrOp::Let { slot, value, body } => write!(f, "let %{slot} = {value} in {body}"),
            IrOp::Inject { left, value } => write!(f, "{}({value})", if *left { "inl" } else { "inr" }),
            IrOp::Case(s, a, b) => write!(f, "case({s}, {a}, {b})"),
            IrOp::Record { name, fields, .. } => write!(
                f,
                "{name}{{{}}}",
                fields.iter().map(|(n, v)| format!("{n}: {v}")).collect::<Vec<_>>().join(", ")
            ),
            IrOp::Field { record, name, .. } => write!(f, "{record}.{name}"),
        }
    }
}

fn type_text(ty: &Option<OrdinaryType>) -> String {
    ty.as_ref().map_or_else(|| "?".to_string(), OrdinaryType::canonical)
}

impl IrProgram {
    /// Display/source view only; executable IR retains nominal IDs.
    pub fn source_type(&self, ty: &OrdinaryType) -> Option<OrdinaryType> {
        ty.map_records(&mut |t| match t {
            OrdinaryType::RecordRef(id) => self.records.get(id.raw() as usize).map(|r| OrdinaryType::Record(r.name.clone())),
            OrdinaryType::Record(_) => Some(t.clone()),
            _ => unreachable!(),
        })
    }

    /// Human-readable listing of the typed IR (`dlm build --emit-ir`).
    pub fn render(&self) -> String {
        let mut out = format!("DLM-IR v3\nmodule: {}\nerased statements: {}\n", self.module, self.erased_statements);
        for source in &self.sources {
            out.push_str(&format!("source {}: {}\n", source.id, source.path.as_ref().map_or_else(|| "<memory>".to_string(), |p| format!("{:?}", p))));
        }
        for (id, origin) in &self.declarations {
            out.push_str(&format!("declaration {id} {:?} {} at {} {}\n", origin.kind, origin.symbol(), origin.span.file_id, origin.span));
        }
        for (id, record) in self.records.iter().enumerate() {
            out.push_str(&format!("record record#{id} {} ({})\n", record.symbol, record.fields.iter().map(|(n, t)| format!("{n}: {}", t.canonical())).collect::<Vec<_>>().join(", ")));
            out.push_str(&format!("  declaration {}\n", record.declaration));
        }
        for (index, name) in self.globals.iter().enumerate() {
            out.push_str(&format!("global @g{index} {name} : {}\n", type_text(&self.global_types[index])));
        }
        for (id, function) in self.functions.iter().enumerate() {
            out.push_str(&format!(
                "fn f{id} {}({}) captures={} slots={}\n  {}\n  : ({}) -> {} effects=[{}] termination={}\n  at {} {}\n",
                function.name,
                function.params.join(", "),
                function.captures,
                function.slots,
                function.body,
                function.param_types.iter().map(OrdinaryType::canonical).collect::<Vec<_>>().join(", "),
                type_text(&function.ret),
                function.effects.join(", "),
                function.termination,
                function.body.span.file_id,
                function.body.span,
            ));
            if let Some(declaration) = function.declaration {
                out.push_str(&format!("  declaration {declaration}\n"));
            }
        }
        for init in &self.init {
            match init {
                IrInit::Global { index, expr } => out.push_str(&format!("init @g{index} = {expr}\n")),
                IrInit::Effect(expr) => out.push_str(&format!("run {expr}\n")),
            }
        }
        if let Some(main) = self.main {
            out.push_str(&format!("main f{main}\n"));
        }
        out
    }
}

fn compile_error(line: Option<usize>, message: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::CompileError, line, message)
}

/// Function-level lowering state.
struct Frame {
    /// Names in scope, innermost last: (name, slot).
    scope: Vec<(String, usize)>,
    slots: usize,
    /// Checked type of every slot.
    types: Vec<Option<OrdinaryType>>,
}

impl Frame {
    fn new() -> Self {
        Frame { scope: Vec::new(), slots: 0, types: Vec::new() }
    }

    fn bind(&mut self, name: &str, ty: Option<OrdinaryType>) -> usize {
        let slot = self.slots;
        self.slots += 1;
        self.types.push(ty);
        self.scope.push((name.to_string(), slot));
        slot
    }

    fn lookup(&self, name: &str) -> Option<usize> {
        self.scope.iter().rev().find(|(n, _)| n == name).map(|(_, s)| *s)
    }
}

struct Lowering<'a> {
    functions: Vec<IrFunction>,
    /// Theory -> fn name -> (function id, declaration). Includes inherited fns.
    table: BTreeMap<String, BTreeMap<String, usize>>,
    decls: Vec<(String, &'a FnDecl)>,
    /// Theory -> let name -> global index (values defined so far).
    globals_by_theory: BTreeMap<String, BTreeMap<String, usize>>,
    globals: Vec<String>,
    global_types: Vec<Option<OrdinaryType>>,
    /// Theory -> statement names (erased).
    statements: BTreeMap<String, Vec<String>>,
    /// Theory -> record name -> field names (with inheritance).
    records: BTreeMap<String, BTreeMap<String, RecordId>>,
    record_layouts: Vec<IrRecord>,
    lambda_counter: usize,
    /// v0.87: the checker's Typed Core facts.
    facts: &'a TypedFacts,
}

/// v0.102: `__release(value, owner)`: evaluates to `value` after releasing the
/// `ByteBuf` in `slot` (the end of its owner's scope).
fn release(value: IrExpr, slot: usize, frame: &Frame) -> IrExpr {
    let span = value.span;
    let owner = IrExpr::new(IrOp::Local(slot), frame.types[slot].clone(), span);
    let ty = value.ty.clone();
    IrExpr::new(IrOp::Builtin { name: "__release".to_string(), args: vec![value, owner] }, ty, span)
}

/// Type of a function value with the given signature.
fn fn_type(params: &[OrdinaryType], ret: &Option<OrdinaryType>) -> Option<OrdinaryType> {
    ret.as_ref().map(|ret| OrdinaryType::Fn(params.to_vec(), Box::new(ret.clone())))
}

impl<'a> Lowering<'a> {
    fn statement_error(&self, theory: &str, name: &str, line: usize) -> Option<Diagnostic> {
        self.statements.get(theory).filter(|s| s.iter().any(|n| n == name)).map(|_| {
            compile_error(Some(line), format!("'{name}' is a statement (proof object); statements are erased by compilation and cannot be used as values"))
        })
    }

    /// The checked type of a source expression.
    fn checked(&self, expr: &Expr, _theory: &str) -> Option<OrdinaryType> {
        self.facts.executable_type_of(expr.id).cloned()
    }

    fn function_value_type(&self, id: usize) -> Option<OrdinaryType> {
        let f = &self.functions[id];
        fn_type(&f.param_types, &f.ret)
    }

    fn lower(&mut self, expr: &Expr, theory: &str, frame: &mut Frame) -> Result<IrExpr, Diagnostic> {
        if self.facts.expr_spans.get(&expr.id).is_some_and(|span| *span != expr.span) {
            return Err(compile_error(Some(expr.line), "internal compiler error: source location differs from checked expression").with_span(expr.span));
        }
        let mut node = self.lower_node(expr, theory, frame).map_err(|d| d.at_expr(expr.span))?;
        node.origin.get_or_insert(expr.id);
        Ok(node)
    }

    fn lower_node(&mut self, expr: &Expr, theory: &str, frame: &mut Frame) -> Result<IrExpr, Diagnostic> {
        let line = expr.line;
        let ty = self.checked(expr, theory);
        let node = |op: IrOp, ty: Option<OrdinaryType>| IrExpr::new(op, ty, expr.span);
        Ok(match &expr.kind {
            ExprKind::IntLiteral(text) => node(
                IrOp::Nat(text.parse::<u128>().map_err(|_| {
                    compile_error(Some(line), format!("integer literal {text} exceeds the compiled Nat range (u128)"))
                })?),
                Some(OrdinaryType::Nat),
            ),
            ExprKind::TextLiteral(text) => node(IrOp::Text(text.clone()), Some(OrdinaryType::Text)),
            ExprKind::Ident(name) => self.lower_ident(name, theory, frame, expr, ty)?,
            ExprKind::QualifiedIdent { theory: other, name } => {
                if other == theory {
                    self.lower_ident(name, theory, frame, expr, ty)?
                } else {
                    return Err(compile_error(Some(line), format!("cross-theory value '{other}.{name}' is not compilable")));
                }
            }
            ExprKind::Add { lhs, rhs } => {
                let (a, b) = (self.lower(lhs, theory, frame)?, self.lower(rhs, theory, frame)?);
                node(IrOp::Add(Box::new(a), Box::new(b)), ty)
            }
            ExprKind::CompareGt { lhs, rhs } => {
                let (a, b) = (self.lower(lhs, theory, frame)?, self.lower(rhs, theory, frame)?);
                node(IrOp::Gt(Box::new(a), Box::new(b)), ty)
            }
            ExprKind::Power { base, exp } => {
                let (a, b) = (self.lower(base, theory, frame)?, self.lower(exp, theory, frame)?);
                node(IrOp::Pow(Box::new(a), Box::new(b)), ty)
            }
            ExprKind::Let { name, value, body } => {
                let value = self.lower(value, theory, frame)?;
                let slot = frame.bind(name, value.ty.clone());
                let body = self.lower(body, theory, frame);
                frame.scope.pop();
                let mut body = body?;
                if self.facts.released_lets.contains(&expr.id) {
                    body = release(body, slot, frame);
                }
                let ty = ty.or_else(|| body.ty.clone());
                node(IrOp::Let { slot, value: Box::new(value), body: Box::new(body) }, ty)
            }
            ExprKind::Lambda { params, body } => self.lower_lambda(params, body, theory, frame, expr, ty)?,
            ExprKind::Call { name, args } => self.lower_call(name, args, theory, frame, expr, ty)?,
        })
    }

    fn lower_ident(&mut self, name: &str, theory: &str, frame: &Frame, expr: &Expr, ty: Option<OrdinaryType>) -> Result<IrExpr, Diagnostic> {
        let line = expr.line;
        if let Some(slot) = frame.lookup(name) {
            return Ok(IrExpr::new(IrOp::Local(slot), ty.or_else(|| frame.types[slot].clone()), expr.span));
        }
        if let Some(index) = self.globals_by_theory.get(theory).and_then(|g| g.get(name)).copied() {
            return Ok(IrExpr::new(IrOp::Global(index), ty.or_else(|| self.global_types[index].clone()), expr.span));
        }
        if let Some(diag) = self.statement_error(theory, name, line) {
            return Err(diag);
        }
        if let Some(id) = self.table.get(theory).and_then(|t| t.get(name)).copied() {
            // A function named as a value: its checked signature.
            return Ok(IrExpr::new(IrOp::FunctionValue(id), ty.or_else(|| self.function_value_type(id)), expr.span));
        }
        Err(compile_error(Some(line), format!("name '{name}' cannot be compiled in theory '{theory}'")))
    }

    fn lower_args(&mut self, args: &[Expr], theory: &str, frame: &mut Frame) -> Result<Vec<IrExpr>, Diagnostic> {
        args.iter().map(|a| self.lower(a, theory, frame)).collect()
    }

    fn lower_call(
        &mut self,
        name: &str,
        args: &[Expr],
        theory: &str,
        frame: &mut Frame,
        expr: &Expr,
        ty: Option<OrdinaryType>,
    ) -> Result<IrExpr, Diagnostic> {
        let line = expr.line;
        let span = expr.span;
        let node = |op: IrOp, ty: Option<OrdinaryType>| IrExpr::new(op, ty, span);
        // Resolution order of `dlm run`: ordinary builtins, then a local or
        // theory-level function value, then user functions, then the
        // remaining (I/O) builtins.
        let ordinary = crate::checker::ORDINARY_BUILTINS.contains(&name);
        if !ordinary {
            if let Some(slot) = frame.lookup(name) {
                let callee = IrExpr::new(IrOp::Local(slot), frame.types[slot].clone(), span);
                return Ok(node(IrOp::Apply { callee: Box::new(callee), args: self.lower_args(args, theory, frame)? }, ty));
            }
            if let Some(index) = self.globals_by_theory.get(theory).and_then(|g| g.get(name)).copied() {
                let callee = IrExpr::new(IrOp::Global(index), self.global_types[index].clone(), span);
                return Ok(node(IrOp::Apply { callee: Box::new(callee), args: self.lower_args(args, theory, frame)? }, ty));
            }
        }
        let arity_error = |expected: usize| {
            compile_error(Some(line), format!("{name} expects {expected} argument(s), got {}", args.len()))
        };
        match name {
            "true" | "false" if args.is_empty() => return Ok(node(IrOp::Bool(name == "true"), Some(OrdinaryType::Bool))),
            "if_then_else" => {
                let [c, a, b] = args else { return Err(arity_error(3)) };
                let (c, a_ir, b_ir) = (self.lower(c, theory, frame)?, self.lower(a, theory, frame)?, self.lower(b, theory, frame)?);
                let (a, b) = (self.release_branch(a, a_ir, frame)?, self.release_branch(b, b_ir, frame)?);
                return Ok(node(IrOp::If(Box::new(c), Box::new(a), Box::new(b)), ty));
            }
            "and" | "or" => {
                let [a, b] = args else { return Err(arity_error(2)) };
                let (a, b) = (Box::new(self.lower(a, theory, frame)?), Box::new(self.lower(b, theory, frame)?));
                return Ok(node(if name == "and" { IrOp::And(a, b) } else { IrOp::Or(a, b) }, ty));
            }
            "unwrap_or" => {
                let [a, b] = args else { return Err(arity_error(2)) };
                let (a, b) = (self.lower(a, theory, frame)?, self.lower(b, theory, frame)?);
                return Ok(node(IrOp::UnwrapOr(Box::new(a), Box::new(b)), ty));
            }
            "effect_fn" => {
                let [_target, value] = args else { return Err(arity_error(2)) };
                let value = self.lower(value, theory, frame)?;
                return Ok(node(IrOp::Builtin { name: name.to_string(), args: vec![value] }, ty));
            }
            "none" => return Ok(node(IrOp::Empty("none"), ty)),
            "empty_list" => return Ok(node(IrOp::Empty("empty_list"), ty)),
            "empty_sequence" => return Ok(node(IrOp::Empty("empty_sequence"), ty)),
            "inl" | "inr" => {
                let [value, _ty] = args else { return Err(arity_error(2)) };
                let value = self.lower(value, theory, frame)?;
                return Ok(node(IrOp::Inject { left: name == "inl", value: Box::new(value) }, ty));
            }
            "case" => {
                let [s, a, b] = args else { return Err(arity_error(3)) };
                let (s, a, b) = (self.lower(s, theory, frame)?, self.lower(a, theory, frame)?, self.lower(b, theory, frame)?);
                return Ok(node(IrOp::Case(Box::new(s), Box::new(a), Box::new(b)), ty));
            }
            "field" => {
                let [record, field] = args else { return Err(arity_error(2)) };
                let ExprKind::Ident(field) = &field.kind else {
                    return Err(compile_error(Some(line), "field expects a field name"));
                };
                let record = self.lower(record, theory, frame)?;
                let Some(OrdinaryType::RecordRef(layout)) = record.ty else {
                    return Err(compile_error(Some(line), "field operand has no resolved record identity"));
                };
                let index = self.record_layouts[layout.raw() as usize].fields.iter().position(|(n, _)| n == field)
                    .ok_or_else(|| compile_error(Some(line), format!("unknown checked field {field}")))?;
                return Ok(node(IrOp::Field { record: Box::new(record), layout, field: index, name: field.clone() }, ty));
            }
            // v0.85: `-5` is an Int constant.
            "__neg" if matches!(args.first().map(|a| &a.kind), Some(ExprKind::IntLiteral(_))) => {
                let ExprKind::IntLiteral(text) = &args[0].kind else { unreachable!() };
                let n: u128 = text.parse().map_err(|_| compile_error(Some(line), format!("Int literal -{text} is out of range")))?;
                let value = if n == 1u128 << 127 {
                    i128::MIN
                } else {
                    -i128::try_from(n).map_err(|_| compile_error(Some(line), format!("Int literal -{text} is out of range")))?
                };
                return Ok(node(IrOp::Int(value), Some(OrdinaryType::Int)));
            }
            // `ok(v, E)` / `err(v, T)`: the type argument is in the node type.
            "ok" | "err" => {
                let [value, _ty] = args else { return Err(arity_error(2)) };
                let value = self.lower(value, theory, frame)?;
                return Ok(node(IrOp::Builtin { name: name.to_string(), args: vec![value] }, ty));
            }
            _ => {}
        }
        let builtin = COMPILED_BUILTINS.iter().find(|(builtin, _)| *builtin == name);
        if !ordinary || builtin.is_none() {
            if let Some(diag) = self.statement_error(theory, name, line) {
                return Err(diag);
            }
            if let Some(id) = self.table.get(theory).and_then(|t| t.get(name)).copied() {
                let args = self.lower_args(args, theory, frame)?;
                let ty = ty.or_else(|| self.functions[id].ret.clone());
                return Ok(node(IrOp::Call { function: id, args }, ty));
            }
            if let Some(record) = self.records.get(theory).and_then(|r| r.get(name)).copied() {
                let fields: Vec<String> = self.record_layouts[record.raw() as usize].fields.iter().map(|(n, _)| n.clone()).collect();
                if fields.len() != args.len() {
                    return Err(arity_error(fields.len()));
                }
                let values = self.lower_args(args, theory, frame)?;
                let ty = ty.or(Some(OrdinaryType::RecordRef(record)));
                return Ok(node(IrOp::Record { record, name: name.to_string(), fields: fields.into_iter().zip(values).collect() }, ty));
            }
        }
        if let Some((_, arity)) = builtin {
            if let Some(expected) = arity {
                if args.len() != *expected {
                    return Err(arity_error(*expected));
                }
            }
            let args = self.lower_args(args, theory, frame)?;
            return Ok(node(IrOp::Builtin { name: name.to_string(), args }, ty));
        }
        Err(compile_error(Some(line), format!("'{name}' is not supported by the compiler yet"))
            .with_help("the compiler covers Nat, Int, Bool, Text, Option, Result, List, Sequence, Product, Sum, records, functions, lambdas, let, match and line I/O; run other programs with `dlm run`"))
    }

    /// v0.102: releases the owners that the checker's plan names after this
    /// branch (they are consumed on the other branch only).
    fn release_branch(&self, source: &Expr, mut node: IrExpr, frame: &Frame) -> Result<IrExpr, Diagnostic> {
        for name in self.facts.released_branches.get(&source.id).into_iter().flatten() {
            let slot = frame.lookup(name).ok_or_else(|| compile_error(Some(source.line), format!("internal compiler error: released owner '{name}' is not in scope")))?;
            node = release(node, slot, frame);
        }
        Ok(node)
    }

    fn lower_lambda(
        &mut self,
        params: &[FnParam],
        body: &Expr,
        theory: &str,
        frame: &Frame,
        expr: &Expr,
        ty: Option<OrdinaryType>,
    ) -> Result<IrExpr, Diagnostic> {
        let line = expr.line;
        // Captured slots: free names of the body that are locals of the frame.
        let mut free = Vec::new();
        let mut bound: Vec<String> = params.iter().map(|p| p.name.clone()).collect();
        free_names(body, &mut bound, &mut free);
        let captured: Vec<(String, usize)> =
            free.iter().filter_map(|name| frame.lookup(name).map(|slot| (name.clone(), slot))).collect();
        let mut inner = Frame::new();
        for (name, slot) in &captured {
            inner.bind(name, frame.types[*slot].clone());
        }
        let (param_types, checked_ret, effects, partial) = match ty.clone() {
            Some(OrdinaryType::Fn(p,r)) => (p,r,Vec::new(),false),
            Some(OrdinaryType::EffectFn(p,r,b)) => (p,r,b.effect_names(),b.partial()),
            _ => return Err(compile_error(Some(line), "missing checked lambda signature")),
        };
        if param_types.len() != params.len() {
            return Err(compile_error(Some(line), "checked lambda parameter count differs from source"));
        }
        for (param, ty) in params.iter().zip(&param_types) {
            inner.bind(&param.name, Some(ty.clone()));
        }
        let ir_body = self.lower(body, theory, &mut inner)?;
        self.lambda_counter += 1;
        let id = self.functions.len();
        let ret = Some(*checked_ret);
        self.functions.push(IrFunction {
            declaration: None,
            name: format!("{theory}.lambda{}@{line}", self.lambda_counter),
            symbol: String::new(),
            params: params.iter().map(|p| p.name.clone()).collect(),
            param_types,
            ret,
            captures: captured.len(),
            slots: inner.slots,
            slot_types: inner.types,
            body: ir_body,
            is_lambda: true,
            effects,
            termination: if partial { "callback_partial" } else { "non-recursive" }.to_string(),
            line,
            borrowed: Vec::new(),
        });
        Ok(IrExpr::new(IrOp::Closure { function: id, captures: captured.into_iter().map(|(_, slot)| slot).collect() }, ty, expr.span))
    }
}

/// Free identifiers and called names of `expr` not bound in `bound`.
fn free_names(expr: &Expr, bound: &mut Vec<String>, out: &mut Vec<String>) {
    crate::ordinary::free_value_names(expr,bound,out)
}

/// Lower a program that the checker accepts under `policy`, then verify the
/// typed IR. A verification failure is a defect of the DLM implementation and
/// is reported as such, never as an error of the program.
pub fn lower_program(program: &Program, policy: CheckPolicy) -> Result<IrProgram, Vec<Diagnostic>> {
    let report = Checker::with_policy(policy).check_program(program);
    if !report.ok() {
        return Err(report.diagnostics);
    }
    let ir = lower_checked_program(program, &report.typed)?;
    let issues = crate::ir_verify::verify_ir(&ir);
    if !issues.is_empty() {
        return Err(vec![compile_error(
            None,
            format!("internal compiler error: the typed IR failed verification ({} issue(s)): {}", issues.len(), issues.iter().take(5).map(ToString::to_string).collect::<Vec<_>>().join("; ")),
        )
        .with_help("this is a defect of the DLM implementation, not of the program; please report it")]);
    }
    Ok(ir)
}

/// Lower an already checked program with its Typed Core facts.
pub fn lower_checked_program(program: &Program, facts: &TypedFacts) -> Result<IrProgram, Vec<Diagnostic>> {
    let root = program.root();
    if !facts.is_checked() {
        return Err(vec![compile_error(None, "unvalidated or conflicting Typed Core facts").in_source(&root.source)]);
    }
    if !facts.matches_contexts(program) {
        return Err(vec![compile_error(None, "source module or theory context differs from checked context").in_source(&root.source)]);
    }
    let mut lowering = Lowering {
        functions: Vec::new(),
        table: BTreeMap::new(),
        decls: Vec::new(),
        globals_by_theory: BTreeMap::new(),
        globals: Vec::new(),
        global_types: Vec::new(),
        statements: BTreeMap::new(),
        records: BTreeMap::new(),
        record_layouts: Vec::new(),
        lambda_counter: 0,
        facts,
    };
    let mut erased = 0usize;
    let mut errors = Vec::new();
    let mut declarations = std::collections::BTreeSet::new();

    // Pass 1: function ids for every theory (with inheritance), statements.
    for module in &program.modules {
        for item in &module.items {
            match item {
                ModuleItem::Theory(theory) => {
                    let mut table = theory
                        .extends
                        .as_ref()
                        .and_then(|base| lowering.table.get(base).cloned())
                        .unwrap_or_default();
                    let mut statements = theory
                        .extends
                        .as_ref()
                        .and_then(|base| lowering.statements.get(base).cloned())
                        .unwrap_or_default();
                    let mut records = theory
                        .extends
                        .as_ref()
                        .and_then(|base| lowering.records.get(base).cloned())
                        .unwrap_or_default();
                    for item in &theory.items {
                        match item {
                            TheoryItem::Fn(decl) => {
                                if !declarations.insert(decl.id) {
                                    return Err(vec![compile_error(Some(decl.line), "duplicate source declaration identity").in_source(&module.source)]);
                                }
                                let id = lowering.functions.len();
                                let name = format!("{}.{}", theory.name, decl.name);
                                let checked = facts.function_declarations.get(&decl.id)
                                    .ok_or_else(|| vec![compile_error(Some(decl.line), "missing checked function declaration ID").in_source(&module.source)])?;
                                if checked.declaration.body.span != decl.body.span {
                                    return Err(vec![compile_error(Some(decl.line), "internal compiler error: source location differs from checked expression").with_span(decl.body.span).in_source(&module.source)]);
                                }
                                if checked.declaration != *decl || checked.declaration.body.id != decl.body.id
                                    || checked.origin.kind != SourceDeclarationKind::Function
                                    || checked.origin.module != module.name || checked.origin.theory != theory.name
                                    || checked.origin.name != decl.name || checked.origin.span != module.source.line_span(decl.line)
                                {
                                    return Err(vec![compile_error(Some(decl.line), "source function declaration differs from checked identity or snapshot").in_source(&module.source)]);
                                }
                                let known = &checked.facts;
                                let param_types = known.params.iter().map(|(_, ty)| ty.clone()).collect();
                                let ret = Some(known.ret.clone());
                                lowering.functions.push(IrFunction {
                                    declaration: Some(decl.id),
                                    symbol: format!("{}::{name}", module.name),
                                    name,
                                    params: decl.params.iter().map(|p| p.name.clone()).collect(),
                                    param_types,
                                    ret,
                                    captures: 0,
                                    slots: 0,
                                    slot_types: Vec::new(),
                                    body: IrExpr::new(IrOp::Bool(false), Some(OrdinaryType::Bool), module.source.line_span(decl.line)),
                                    is_lambda: false,
                                    effects: known.effects.clone(),
                                    termination: known.termination.clone(),
                                    line: decl.line,
                                    borrowed: decl.params.iter().enumerate().filter(|(_, p)| decl.borrowed.contains(&p.name)).map(|(i, _)| i).collect(),
                                });
                                lowering.decls.push((theory.name.clone(), decl));
                                table.insert(decl.name.clone(), id);
                            }
                            TheoryItem::Statement(statement) => {
                                statements.push(statement.name.clone());
                                if std::ptr::eq(module, root) {
                                    erased += 1;
                                }
                            }
                            TheoryItem::Record(record) => {
                                if !declarations.insert(record.id) {
                                    return Err(vec![compile_error(Some(record.line), "duplicate source declaration identity").in_source(&module.source)]);
                                }
                                let checked = facts.record_declarations.get(&record.id)
                                    .ok_or_else(|| vec![compile_error(Some(record.line), "missing checked record declaration ID").in_source(&module.source)])?;
                                if checked.declaration != *record || checked.origin.kind != SourceDeclarationKind::Record
                                    || checked.origin.module != module.name || checked.origin.theory != theory.name
                                    || checked.origin.name != record.name || checked.origin.span != module.source.line_span(record.line)
                                    || checked.record_id.raw() as usize != lowering.record_layouts.len()
                                {
                                    return Err(vec![compile_error(Some(record.line), "source record declaration differs from checked identity, snapshot or schema order").in_source(&module.source)]);
                                }
                                let known = &checked.facts;
                                lowering.record_layouts.push(IrRecord { declaration: record.id, name: record.name.clone(), symbol: checked.origin.symbol(), fields: known.fields.clone(), span: known.span });
                                records.insert(record.name.clone(), checked.record_id);
                            }
                            _ => {}
                        }
                    }
                    lowering.records.insert(theory.name.clone(), records);
                    lowering.table.insert(theory.name.clone(), table);
                    lowering.statements.insert(theory.name.clone(), statements);
                }
                ModuleItem::Bridge(bridge) if std::ptr::eq(module, root) => errors.push(compile_error(
                    Some(bridge.line),
                    format!("bridge '{}' is not compilable", bridge.name),
                ).in_source(&module.source)),
                ModuleItem::Bridge(_) => {}
            }
        }
    }

    if declarations.len() != facts.function_declarations.len() + facts.record_declarations.len() {
        return Err(vec![compile_error(None, "checked declarations do not match the program").in_source(&root.source)]);
    }

    // Pass 2: theory-level values of the root module, in order. Function
    // bodies see the globals defined before their theory's items finish, as
    // in `dlm run`; they are lowered afterwards with the complete table.
    let mut init = Vec::new();
    for item in &root.items {
        let ModuleItem::Theory(theory) = item else { continue };
        lowering.globals_by_theory.entry(theory.name.clone()).or_default();
        for item in &theory.items {
            let (line, result) = match item {
                TheoryItem::Let(decl) => {
                    let mut frame = Frame::new();
                    (
                        decl.line,
                        lowering.lower(&decl.expr, &theory.name, &mut frame).map(|expr| {
                            let index = lowering.globals.len();
                            lowering.globals.push(format!("{}.{}", theory.name, decl.name));
                            lowering.global_types.push(expr.ty.clone());
                            lowering.globals_by_theory.entry(theory.name.clone()).or_default().insert(decl.name.clone(), index);
                            (Some(index), expr, frame)
                        }),
                    )
                }
                TheoryItem::Expr(expr) => {
                    let mut frame = Frame::new();
                    (expr.line, lowering.lower(expr, &theory.name, &mut frame).map(|e| (None, e, frame)))
                }
                _ => continue,
            };
            match result {
                // Theory-level code runs in a frame of its own (a synthetic
                // function), so it may bind `let`s and build closures.
                Ok((global, expr, frame)) => {
                    let id = lowering.functions.len();
                    let ret = expr.ty.clone();
                    let span = expr.span;
                    let effects = init_effects(&expr, &lowering.functions);
                    lowering.functions.push(IrFunction {
                        declaration: None,
                        name: format!("{}.<init{}>", theory.name, init.len()),
                        symbol: String::new(),
                        params: Vec::new(),
                        param_types: Vec::new(),
                        ret: ret.clone(),
                        captures: 0,
                        slots: frame.slots,
                        slot_types: frame.types,
                        body: expr,
                        is_lambda: false,
                        effects,
                        termination: "non-recursive".to_string(),
                        line,
                        borrowed: Vec::new(),
                    });
                    let call = IrExpr::new(IrOp::Call { function: id, args: Vec::new() }, ret, span);
                    init.push(match global {
                        Some(index) => IrInit::Global { index, expr: call },
                        None => IrInit::Effect(call),
                    });
                }
                Err(diag) => errors.push(diag),
            }
        }
    }

    // Pass 3: function bodies.
    let decls = lowering.decls.clone();
    let mut fn_id = 0usize;
    for (id, function) in lowering.functions.clone().iter().enumerate() {
        if function.is_lambda || function.name.contains(".<init") {
            continue;
        }
        let (theory, decl) = &decls[fn_id];
        fn_id += 1;
        let mut frame = Frame::new();
        for (index, param) in decl.params.iter().enumerate() {
            frame.bind(&param.name, function.param_types.get(index).cloned());
        }
        let released: Vec<usize> = facts.function_declarations.get(&decl.id).into_iter()
            .flat_map(|d| d.facts.released_params.iter()).filter_map(|name| frame.lookup(name)).collect();
        match lowering.lower(&decl.body, theory, &mut frame) {
            Ok(mut body) => {
                // v0.102: ByteBuf parameters still owned after the body.
                for slot in released {
                    body = release(body, slot, &frame);
                }
                lowering.functions[id].body = body;
                lowering.functions[id].slots = frame.slots;
                lowering.functions[id].slot_types = frame.types;
            }
            Err(diag) => errors.push(diag),
        }
    }
    if !errors.is_empty() {
        return Err(errors.into_iter().map(|d| program.locate_diagnostic(d)).collect());
    }

    let main = crate::program::main_function(root).and_then(|(theory, _)| lowering.table.get(&theory).and_then(|t| t.get("main")).copied());
    Ok(IrProgram {
        module: root.name.clone(),
        sources: program.modules.iter().map(|m| m.source.clone()).collect(),
        source_spans: facts.expr_spans.clone(),
        declarations: facts.function_declarations.iter().map(|(id, d)| (*id, d.origin.clone()))
            .chain(facts.record_declarations.iter().map(|(id, d)| (*id, d.origin.clone()))).collect(),
        functions: lowering.functions,
        globals: lowering.globals,
        global_types: lowering.global_types,
        init,
        main,
        erased_statements: erased,
        records: lowering.record_layouts,
    })
}

/// Effects of theory-level code: its I/O builtins and the effects of the
/// functions it calls directly (their contracts already include callees).
fn init_effects(expr: &IrExpr, functions: &[IrFunction]) -> Vec<String> {
    fn walk(expr: &IrExpr, functions: &[IrFunction], out: &mut Vec<String>) {
        match &expr.op {
            IrOp::Builtin { name, args } => {
                out.extend(crate::checker::builtin_effect(name).map(|(kind, boundary, _)| format!("{kind}@{boundary}")));
                if let Some(i) = crate::callback::traversal_argument(name) {
                    if let Some(OrdinaryType::EffectFn(_,_,b)) = args.get(i).and_then(|a|a.ty.as_ref()) { out.extend(b.effect_names()); }
                }
                args.iter().for_each(|a| walk(a, functions, out));
            }
            IrOp::Call { function, args } => {
                // Function bodies are lowered later; their contract effects
                // are already known.
                out.extend(functions.get(*function).map(|f| f.effects.clone()).unwrap_or_default());
                args.iter().for_each(|a| walk(a, functions, out));
            }
            IrOp::Apply { callee, .. } => {
                if let Some(OrdinaryType::EffectFn(_,_,b)) = &callee.ty { out.extend(b.effect_names()); }
                for_each_child(expr, &mut |child| walk(child,functions,out));
            }
            IrOp::Case(_,a,b) => {
                for branch in [a,b] {
                    if let Some(OrdinaryType::EffectFn(_,_,bounds)) = &branch.ty { out.extend(bounds.effect_names()); }
                }
                for_each_child(expr, &mut |child| walk(child,functions,out));
            }
            _ => for_each_child(expr, &mut |child| walk(child, functions, out)),
        }
    }
    let mut out = Vec::new();
    walk(expr, functions, &mut out);
    out.sort();
    out.dedup();
    out
}

/// Visit the direct children of an IR node, in evaluation order.
pub fn for_each_child(expr: &IrExpr, f: &mut dyn FnMut(&IrExpr)) {
    match &expr.op {
        IrOp::Nat(_) | IrOp::Int(_) | IrOp::Bool(_) | IrOp::Text(_) | IrOp::Local(_) | IrOp::Global(_) | IrOp::FunctionValue(_)
        | IrOp::Closure { .. } | IrOp::Empty(_) => {}
        IrOp::Call { args, .. } | IrOp::Builtin { args, .. } => args.iter().for_each(f),
        IrOp::Apply { callee, args } => {
            f(callee);
            args.iter().for_each(f);
        }
        IrOp::Add(a, b) | IrOp::Gt(a, b) | IrOp::Pow(a, b) | IrOp::And(a, b) | IrOp::Or(a, b) | IrOp::UnwrapOr(a, b) => {
            f(a);
            f(b);
        }
        IrOp::If(a, b, c) | IrOp::Case(a, b, c) => {
            f(a);
            f(b);
            f(c);
        }
        IrOp::Let { value, body, .. } => {
            f(value);
            f(body);
        }
        IrOp::Inject { value, .. } | IrOp::Field { record: value, .. } => f(value),
        IrOp::Record { fields, .. } => fields.iter().for_each(|(_, v)| f(v)),
    }
}
