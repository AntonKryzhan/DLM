//! v0.73 — Ordinary mathematics surface wiring for the checker.
//!
//! Connects `.dlm` syntax to the Stage 2 core layers instead of duplicating
//! them:
//!
//! ```text
//! some/none/ok/err      -> partiality::{option_*, result_*}
//! list/sequence         -> sequence::{list_*, sequence_*}
//! pair                  -> structural::{product_type, product_term}
//! fn declarations       -> function::function_type + function_contract::function_contract
//! single-argument calls -> function::application_term (cross-check)
//! map/fold              -> traversal::{map,fold}_collection_passport
//! ```
//!
//! Every ordinary value is typed by its canonical ordinary type
//! (`crate::ordinary`). Proof, theorem, truth, runtime-witness and resource
//! objects are not ordinary values and are rejected at this boundary.

use std::collections::BTreeMap;

use super::Checker;
use crate::ast::*;
use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::function::{application_term, function_type, function_type_passport, lambda_term, ApplicationStatus};
use crate::logic::bound_variable;
use crate::function_contract::{
    function_contract, function_contract_passport, require_verified_function_contract,
    FunctionContractReport, FunctionEffect, FunctionEffectKind, FunctionPurity, FunctionTotality,
};
use crate::ordinary::{
    OrdinaryType, collection_item, domain_of_params, fn_signature, fn_type_text, option_item, parse_ordinary_type,
    product_components, record_type_name, result_components, sum_components, surface_type_of, type_from_expr,
};
use crate::partiality::{
    option_none, option_some, option_type, option_value_passport, result_err, result_ok, result_type,
    result_value_passport,
};
use crate::passport::*;
use crate::sequence::{
    list_type, list_value, list_value_passport, sequence_type, sequence_value, sequence_value_passport,
};
use crate::structural::{
    product_term, product_term_passport, product_type, record_field_decl, record_field_value, record_term,
    record_term_passport, record_type, record_type_passport, sum_injection, sum_injection_passport, sum_type,
    SumInjectionSide,
};
use crate::recursion::{
    recursion_scheme, recursion_scheme_passport, RecursionMeasureKind, RecursionSchemeReport, RecursionStatus,
};
use crate::traversal::{
    fold_collection_passport, map_collection_passport, require_verified_bounded_fold,
    require_verified_bounded_map,
};

/// A checked `fn` declaration.
#[derive(Debug, Clone)]
pub(crate) struct CheckedFn {
    pub decl: FnDecl,
    pub function_type: Passport,
    pub contract: FunctionContractReport,
    pub contract_passport: Passport,
    pub body: Passport,
    pub recursion: Option<RecursionSchemeReport>,
}

pub(crate) type FunctionTable = BTreeMap<String, BTreeMap<String, CheckedFn>>;

/// v0.75: function argument of `map`, `fold` or `case` — a declared fn named
/// directly, or any expression of type `Fn<...>` (lambda, parameter, let).
#[derive(Debug, Clone)]
pub(crate) struct FnArg {
    pub label: String,
    pub params: Vec<String>,
    pub ret: String,
    pub contract: FunctionContractReport,
    pub history: HistoryChain,
    pub trust: TrustLevel,
    pub provenance: Provenance,
}

/// Names handled by [`Checker::infer_ordinary_call`]. Kept in sync with the
/// runtime so both sides agree on the ordinary surface.
pub const ORDINARY_BUILTINS: &[&str] = &[
    "effect_fn", "map_effect", "fold_effect", "each_effect", "filter_effect", "find_effect", "any_effect", "all_effect", "try_map_effect", "try_each_effect",
    "true", "false", "not", "and", "or", "nat_eq", "mul", "monus", "pred", "if_then_else", "some", "none", "ok",
    "err", "is_some", "is_none", "is_ok", "is_err", "unwrap_or", "list", "sequence", "empty_list",
    "empty_sequence", "length", "index", "head", "tail", "is_empty", "cons", "pair", "fst", "snd", "map", "fold", "inl",
    "inr", "is_left", "case", "field", "__match_head", "__match_some", "__match_variant", "__enum_payload", "__match_ok", "__match_err", "concat", "text_length", "text_eq",
    "nat_to_text", "text_to_nat", "substring", "lines", "words", "join", "int", "abs", "int_eq", "div", "mod",
    "int_to_text", "text_to_int", "__sub", "__neg", "buf_new", "buf_from_list", "buf_len", "buf_get", "buf_set",
    "buf_push", "buf_to_list", "buf_free", "buf_copy", "buf_slice", "buf_append", "buf_eq", "buf_from_text", "buf_to_text", "tbuf_new", "tbuf_push", "tbuf_len", "tbuf_finish",
    "equal", "less",
    "fl_of_nat", "fl_of_int", "fl_lit", "fl_parse", "fl_add", "fl_sub", "fl_mul", "fl_div", "fl_neg", "fl_abs", "fl_min", "fl_max", "fl_sqrt", "fl_exp", "fl_log", "fl_sin", "fl_cos", "fl_tan", "fl_atan", "fl_atan2", "fl_pow", "fl_floor", "fl_ceil", "fl_round", "fl_trunc", "fl_lt", "fl_le", "fl_eq", "fl_is_nan", "fl_is_finite", "fl_to_text", "fl_to_int", "fl_bits", "fl_from_bits", "fv_new", "fv_from_list", "fv_to_list", "fv_len", "fv_get", "fv_set", "fv_add", "fv_sub", "fv_mul", "fv_scale", "fv_axpy", "fv_dot", "fv_sum", "fv_norm", "fv_max", "fv_min", "fv_slice", "fv_concat", "fv_matmul", "fv_transpose", "fv_empty", "fv_solve",
    "nv_empty", "nv_new", "nv_from_list", "nv_to_list", "nv_len", "nv_get", "nv_set", "nv_push", "nv_slice", "nv_concat", "nv_range", "nv_sum", "nv_max", "nv_min", "nv_sort", "nv_reverse", "nv_find", "nv_to_fv", "gr_bfs", "gr_dfs", "gr_toposort", "gr_components", "gr_scc", "gr_bipartite", "gr_dijkstra", "gr_bellman_ford", "gr_floyd", "gr_mst", "gr_maxflow",
    "la_lu", "la_lu_perm", "la_det", "la_inverse", "la_cholesky", "la_qr_q", "la_qr_r", "la_lstsq", "la_eigh_values", "la_eigh_vectors", "la_svd_values", "la_rank", "la_identity", "la_trace", "la_csr_matvec", "la_cg",
    "fft_forward", "fft_inverse", "fft_convolve", "lp_solve", "lp_status",
    "nt_gcd", "nt_lcm", "nt_mod_mul", "nt_mod_pow", "nt_mod_inv", "nt_is_prime", "nt_next_prime", "nt_factor", "nt_totient", "nt_divisors", "nt_primes", "nt_crt", "nt_sqrt_mod", "nt_isqrt", "pf_add", "pf_sub", "pf_mul", "pf_div", "pf_rem", "pf_gcd", "pf_powmod", "pf_eval", "pf_irreducible", "pm_compose", "pm_inverse", "pm_power", "pm_order", "pm_sign", "pm_cycle_type", "pm_orbit", "pm_group_order",
    "sat_solve", "sat_status", "tp_homology", "tp_rips",
    "gb_basis", "gb_reduce", "mv_add", "mv_sub", "mv_mul", "mv_normalize",
    "sat_proof", "sat_check", "sat_count", "ilp_solve", "ilp_status",
    "km_empty", "km_insert", "km_get", "km_remove", "km_contains", "km_len", "km_keys", "km_values", "km_from", "km_add", "km_min", "km_max", "km_floor", "km_ceil", "km_merge", "km_range", "tm_empty", "tm_insert", "tm_get", "tm_remove", "tm_contains", "tm_len", "tm_keys", "tm_values", "tm_from", "tm_add", "tm_merge", "tm_count_words",
    "la_pcg", "la_bicgstab", "la_gmres", "geo_orient2d", "geo_incircle", "geo_orient3d", "geo_delaunay", "kd_knn", "kd_radius", "fft2_forward", "fft2_inverse", "sig_stft", "sig_dwt", "sig_idwt", "sig_iir", "sig_sha256", "bvh_raycast", "bvh_winding", "bvh_pairs", "gx_new", "gx_width", "gx_height", "gx_get", "gx_fill_rect", "gx_line", "gx_circle", "gx_text", "gx_text_width", "gx_blit", "gx_checksum", "vx_render", "vx_render_region", "vx_bounds", "vx_text_width", "vx_svg", "vx_text", "vx_text_fonts", "vx_font_width", "vx_font_has", "vx_font_advance", "vx_font_metrics", "vx_codepoints", "vx_from_codepoints", "file_write_png", "file_font_load", "file_font_find",
    "file_open", "file_ok", "file_error", "file_write", "file_write_bytes", "file_read_line", "file_read_all", "file_read_bytes", "file_flush", "file_close", "file_remove", "file_set_buffered", "file_seek", "file_tell", "file_size", "file_truncate", "file_exists", "file_is_dir", "file_size_of", "file_mtime", "file_list_dir", "file_walk", "file_make_dir", "file_remove_dir", "file_rename", "file_copy", "file_write_atomic", "proc_run", "proc_env", "net_resolve", "net_listen", "net_local_port", "net_accept", "net_connect", "net_send", "net_recv_line", "net_recv_all", "net_shutdown", "net_close", "proc_spawn", "proc_poll", "proc_wait", "proc_kill", "win_open", "win_present", "win_poll", "win_size", "win_close", "win_gpu", "win_present_scene", "win_read",
    "bn_from_nat", "bn_to_nat", "bn_from_words", "bn_to_words", "bn_from_text", "bn_to_text", "bn_bits", "bn_limbs", "bn_cmp", "bn_add", "bn_monus", "bn_mul", "bn_div", "bn_mod", "bn_shl", "bn_shr", "bn_gcd", "bn_sqrt", "bn_powmod", "bn_modinv", "bn_is_prime", "bn_next_prime", "bn_random", "bn_to_hex", "bn_from_hex",
];

/// v0.102: owned byte-buffer builtins (stage D, `docs/OWNERSHIP.md`) and, for
/// each, which argument positions borrow (`false`) or move (`true`) a
/// `ByteBuf`; other positions are ordinary values.
pub const BYTE_BUFFER_BUILTINS: &[(&str, &[Option<bool>])] = &[
    ("buf_new", &[None, None]),
    ("buf_from_list", &[None]),
    ("buf_len", &[Some(false)]),
    ("buf_get", &[Some(false), None]),
    ("buf_set", &[Some(true), None, None]),
    ("buf_push", &[Some(true), None]),
    ("buf_to_list", &[Some(true)]),
    ("buf_free", &[Some(true)]),
    // v0.103
    ("buf_copy", &[Some(false)]),
    ("buf_slice", &[Some(false), None, None]),
    ("buf_append", &[Some(true), Some(false)]),
    ("buf_eq", &[Some(false), Some(false)]),
    ("buf_from_text", &[None]),
    ("buf_to_text", &[Some(false)]),
    // v0.104: text builders
    ("tbuf_new", &[]),
    ("tbuf_push", &[Some(true), None]),
    ("tbuf_len", &[Some(false)]),
    ("tbuf_finish", &[Some(true)]),
    // v0.109: external file handles (linear; docs/FILES.md)
    ("file_open", &[None, None]),
    ("file_ok", &[Some(false)]),
    ("file_error", &[Some(false)]),
    ("file_write", &[Some(false), None]),
    ("file_write_bytes", &[Some(false), Some(false)]),
    ("file_read_line", &[Some(false)]),
    ("file_read_all", &[Some(false)]),
    ("file_read_bytes", &[Some(false), None]),
    ("file_flush", &[Some(false)]),
    ("file_close", &[Some(true)]),
    ("file_remove", &[None]),
    ("file_set_buffered", &[Some(false), None]),
    ("file_seek", &[Some(false), None]),
    ("file_tell", &[Some(false)]),
    ("file_size", &[Some(false)]),
    ("file_truncate", &[Some(false), None]),
    ("file_exists", &[None]),
    ("file_is_dir", &[None]),
    ("file_size_of", &[None]),
    ("file_mtime", &[None]),
    ("file_list_dir", &[None]),
    ("file_walk", &[None]),
    ("file_make_dir", &[None]),
    ("file_remove_dir", &[None]),
    ("file_rename", &[None, None]),
    ("file_copy", &[None, None]),
    ("file_write_atomic", &[None, None]),
    ("file_write_png", &[None, None]),
    ("proc_run", &[None, None, None]),
    ("proc_env", &[None]),
    ("net_resolve", &[None]),
    ("net_listen", &[None, None]),
    ("net_local_port", &[None]),
    ("net_accept", &[None, None]),
    ("net_connect", &[None, None, None]),
    ("net_send", &[None, None]),
    ("net_recv_line", &[None]),
    ("net_recv_all", &[None]),
    ("net_shutdown", &[None]),
    ("net_close", &[None]),
    ("proc_spawn", &[None, None, None]),
    ("proc_poll", &[None]),
    ("proc_wait", &[None]),
    ("proc_kill", &[None]),
    ("win_open", &[None, None, None]),
    ("win_present", &[None, None]),
    ("win_poll", &[None, None]),
    ("win_size", &[None]),
    ("win_close", &[None]),
    ("win_gpu", &[None]),
    ("win_present_scene", &[None, None, None, None, None, None]),
    ("win_read", &[None]),
    ("file_font_load", &[None]),
    ("file_font_find", &[None]),
];

/// v0.114: machine floats and float vectors `(name, parameter types, result type)`;
/// pure and total (`none` for out-of-range indices, mismatched lengths or shapes).
pub const FLOAT_BUILTINS: &[(&str, &[&str], &str)] = &[
    ("fl_of_nat", &["Nat"], "Float"),
    ("fl_of_int", &["Int"], "Float"),
    ("fl_lit", &["Text"], "Float"),
    ("fl_parse", &["Text"], "Option<Float>"),
    ("fl_add", &["Float", "Float"], "Float"),
    ("fl_sub", &["Float", "Float"], "Float"),
    ("fl_mul", &["Float", "Float"], "Float"),
    ("fl_div", &["Float", "Float"], "Float"),
    ("fl_neg", &["Float"], "Float"),
    ("fl_abs", &["Float"], "Float"),
    ("fl_min", &["Float", "Float"], "Float"),
    ("fl_max", &["Float", "Float"], "Float"),
    ("fl_sqrt", &["Float"], "Float"),
    ("fl_exp", &["Float"], "Float"),
    ("fl_log", &["Float"], "Float"),
    ("fl_sin", &["Float"], "Float"),
    ("fl_cos", &["Float"], "Float"),
    ("fl_tan", &["Float"], "Float"),
    ("fl_atan", &["Float"], "Float"),
    ("fl_atan2", &["Float", "Float"], "Float"),
    ("fl_pow", &["Float", "Float"], "Float"),
    ("fl_floor", &["Float"], "Float"),
    ("fl_ceil", &["Float"], "Float"),
    ("fl_round", &["Float"], "Float"),
    ("fl_trunc", &["Float"], "Float"),
    ("fl_lt", &["Float", "Float"], "Bool"),
    ("fl_le", &["Float", "Float"], "Bool"),
    ("fl_eq", &["Float", "Float"], "Bool"),
    ("fl_is_nan", &["Float"], "Bool"),
    ("fl_is_finite", &["Float"], "Bool"),
    ("fl_to_text", &["Float"], "Text"),
    ("fl_to_int", &["Float"], "Option<Int>"),
    ("fl_bits", &["Float"], "Nat"),
    ("fl_from_bits", &["Nat"], "Float"),
    ("fv_new", &["Nat", "Float"], "Option<FloatVec>"),
    ("fv_from_list", &["List<Float>"], "Option<FloatVec>"),
    ("fv_to_list", &["FloatVec"], "List<Float>"),
    ("fv_len", &["FloatVec"], "Nat"),
    ("fv_get", &["FloatVec", "Nat"], "Option<Float>"),
    ("fv_set", &["FloatVec", "Nat", "Float"], "Option<FloatVec>"),
    ("fv_add", &["FloatVec", "FloatVec"], "Option<FloatVec>"),
    ("fv_sub", &["FloatVec", "FloatVec"], "Option<FloatVec>"),
    ("fv_mul", &["FloatVec", "FloatVec"], "Option<FloatVec>"),
    ("fv_scale", &["FloatVec", "Float"], "FloatVec"),
    ("fv_axpy", &["Float", "FloatVec", "FloatVec"], "Option<FloatVec>"),
    ("fv_dot", &["FloatVec", "FloatVec"], "Option<Float>"),
    ("fv_sum", &["FloatVec"], "Float"),
    ("fv_norm", &["FloatVec"], "Float"),
    ("fv_max", &["FloatVec"], "Option<Float>"),
    ("fv_min", &["FloatVec"], "Option<Float>"),
    ("fv_slice", &["FloatVec", "Nat", "Nat"], "Option<FloatVec>"),
    ("fv_concat", &["FloatVec", "FloatVec"], "Option<FloatVec>"),
    ("fv_matmul", &["FloatVec", "FloatVec", "Nat", "Nat", "Nat"], "Option<FloatVec>"),
    ("fv_transpose", &["FloatVec", "Nat", "Nat"], "Option<FloatVec>"),
    ("fv_empty", &[], "FloatVec"),
    ("fv_solve", &["FloatVec", "FloatVec", "Nat"], "Option<FloatVec>"),
];

/// v0.115: natural-number vectors and graph algorithms `(name, parameter types, result type)`;
/// pure and total (`docs/GRAPHS.md`).
pub const GRAPH_BUILTINS: &[(&str, &[&str], &str)] = &[
    ("nv_empty", &[], "NatVec"),
    ("nv_new", &["Nat", "Nat"], "Option<NatVec>"),
    ("nv_from_list", &["List<Nat>"], "Option<NatVec>"),
    ("nv_to_list", &["NatVec"], "List<Nat>"),
    ("nv_len", &["NatVec"], "Nat"),
    ("nv_get", &["NatVec", "Nat"], "Option<Nat>"),
    ("nv_set", &["NatVec", "Nat", "Nat"], "Option<NatVec>"),
    ("nv_push", &["NatVec", "Nat"], "Option<NatVec>"),
    ("nv_slice", &["NatVec", "Nat", "Nat"], "Option<NatVec>"),
    ("nv_concat", &["NatVec", "NatVec"], "Option<NatVec>"),
    ("nv_range", &["Nat", "Nat"], "Option<NatVec>"),
    ("nv_sum", &["NatVec"], "Option<Nat>"),
    ("nv_max", &["NatVec"], "Option<Nat>"),
    ("nv_min", &["NatVec"], "Option<Nat>"),
    ("nv_sort", &["NatVec"], "NatVec"),
    ("nv_reverse", &["NatVec"], "NatVec"),
    ("nv_find", &["NatVec", "Nat"], "Option<Nat>"),
    ("nv_to_fv", &["NatVec"], "FloatVec"),
    ("gr_bfs", &["Nat", "NatVec", "NatVec", "Nat"], "Option<NatVec>"),
    ("gr_dfs", &["Nat", "NatVec", "NatVec", "Nat"], "Option<NatVec>"),
    ("gr_toposort", &["Nat", "NatVec", "NatVec"], "Option<NatVec>"),
    ("gr_components", &["Nat", "NatVec", "NatVec"], "Option<NatVec>"),
    ("gr_scc", &["Nat", "NatVec", "NatVec"], "Option<NatVec>"),
    ("gr_bipartite", &["Nat", "NatVec", "NatVec"], "Option<NatVec>"),
    ("gr_dijkstra", &["Nat", "NatVec", "NatVec", "FloatVec", "Nat"], "Option<FloatVec>"),
    ("gr_bellman_ford", &["Nat", "NatVec", "NatVec", "FloatVec", "Nat"], "Option<FloatVec>"),
    ("gr_floyd", &["Nat", "NatVec", "NatVec", "FloatVec"], "Option<FloatVec>"),
    ("gr_mst", &["Nat", "NatVec", "NatVec", "FloatVec"], "Option<NatVec>"),
    ("gr_maxflow", &["Nat", "NatVec", "NatVec", "NatVec", "Nat", "Nat"], "Option<Nat>"),
];

/// v0.116: numerical linear algebra `(name, parameter types, result type)`; pure and total (`docs/LINALG.md`).
pub const LINALG_BUILTINS: &[(&str, &[&str], &str)] = &[
    ("la_lu", &["FloatVec", "Nat"], "Option<FloatVec>"),
    ("la_lu_perm", &["FloatVec", "Nat"], "Option<NatVec>"),
    ("la_det", &["FloatVec", "Nat"], "Option<Float>"),
    ("la_inverse", &["FloatVec", "Nat"], "Option<FloatVec>"),
    ("la_cholesky", &["FloatVec", "Nat"], "Option<FloatVec>"),
    ("la_qr_q", &["FloatVec", "Nat", "Nat"], "Option<FloatVec>"),
    ("la_qr_r", &["FloatVec", "Nat", "Nat"], "Option<FloatVec>"),
    ("la_lstsq", &["FloatVec", "FloatVec", "Nat", "Nat"], "Option<FloatVec>"),
    ("la_eigh_values", &["FloatVec", "Nat"], "Option<FloatVec>"),
    ("la_eigh_vectors", &["FloatVec", "Nat"], "Option<FloatVec>"),
    ("la_svd_values", &["FloatVec", "Nat", "Nat"], "Option<FloatVec>"),
    ("la_rank", &["FloatVec", "Nat", "Nat", "Float"], "Option<Nat>"),
    ("la_identity", &["Nat"], "Option<FloatVec>"),
    ("la_trace", &["FloatVec", "Nat"], "Option<Float>"),
    ("la_csr_matvec", &["NatVec", "NatVec", "FloatVec", "FloatVec"], "Option<FloatVec>"),
    ("la_cg", &["NatVec", "NatVec", "FloatVec", "FloatVec", "Float", "Nat"], "Option<FloatVec>"),
    // v0.117: Fourier transforms and linear programming (docs/ANALYSIS.md)
    ("fft_forward", &["FloatVec"], "Option<FloatVec>"),
    ("fft_inverse", &["FloatVec"], "Option<FloatVec>"),
    ("fft_convolve", &["FloatVec", "FloatVec"], "Option<FloatVec>"),
    ("lp_solve", &["FloatVec", "FloatVec", "FloatVec", "Nat", "Nat"], "Option<FloatVec>"),
    ("lp_status", &["FloatVec", "FloatVec", "FloatVec", "Nat", "Nat"], "Nat"),
    // v0.118: number theory, polynomials over F_p, permutation groups (docs/ALGEBRA.md)
    ("nt_gcd", &["Nat", "Nat"], "Nat"),
    ("nt_lcm", &["Nat", "Nat"], "Option<Nat>"),
    ("nt_mod_mul", &["Nat", "Nat", "Nat"], "Option<Nat>"),
    ("nt_mod_pow", &["Nat", "Nat", "Nat"], "Option<Nat>"),
    ("nt_mod_inv", &["Nat", "Nat"], "Option<Nat>"),
    ("nt_is_prime", &["Nat"], "Bool"),
    ("nt_next_prime", &["Nat"], "Option<Nat>"),
    ("nt_factor", &["Nat"], "Option<NatVec>"),
    ("nt_totient", &["Nat"], "Nat"),
    ("nt_divisors", &["Nat"], "Option<NatVec>"),
    ("nt_primes", &["Nat"], "Option<NatVec>"),
    ("nt_crt", &["NatVec", "NatVec"], "Option<Nat>"),
    ("nt_sqrt_mod", &["Nat", "Nat"], "Option<Nat>"),
    ("nt_isqrt", &["Nat"], "Nat"),
    ("pf_add", &["NatVec", "NatVec", "Nat"], "Option<NatVec>"),
    ("pf_sub", &["NatVec", "NatVec", "Nat"], "Option<NatVec>"),
    ("pf_mul", &["NatVec", "NatVec", "Nat"], "Option<NatVec>"),
    ("pf_div", &["NatVec", "NatVec", "Nat"], "Option<NatVec>"),
    ("pf_rem", &["NatVec", "NatVec", "Nat"], "Option<NatVec>"),
    ("pf_gcd", &["NatVec", "NatVec", "Nat"], "Option<NatVec>"),
    ("pf_powmod", &["NatVec", "Nat", "NatVec", "Nat"], "Option<NatVec>"),
    ("pf_eval", &["NatVec", "Nat", "Nat"], "Option<Nat>"),
    ("pf_irreducible", &["NatVec", "Nat"], "Option<Bool>"),
    ("pm_compose", &["NatVec", "NatVec"], "Option<NatVec>"),
    ("pm_inverse", &["NatVec"], "Option<NatVec>"),
    ("pm_power", &["NatVec", "Nat"], "Option<NatVec>"),
    ("pm_order", &["NatVec"], "Option<Nat>"),
    ("pm_sign", &["NatVec"], "Option<Nat>"),
    ("pm_cycle_type", &["NatVec"], "Option<NatVec>"),
    ("pm_orbit", &["NatVec", "Nat", "Nat"], "Option<NatVec>"),
    ("pm_group_order", &["NatVec", "Nat"], "Option<Nat>"),
    // v0.119: SAT, integral homology, Rips persistence (docs/LOGIC_TOPOLOGY.md)
    ("sat_solve", &["NatVec", "NatVec", "Nat"], "Option<NatVec>"),
    ("sat_status", &["NatVec", "NatVec", "Nat"], "Nat"),
    ("tp_homology", &["NatVec", "NatVec"], "Option<NatVec>"),
    ("tp_rips", &["FloatVec", "Nat", "Nat", "Float"], "Option<FloatVec>"),
    // v0.120: multivariate polynomials over F_p, Gröbner bases (docs/GROEBNER.md)
    ("gb_basis", &["NatVec", "NatVec", "Nat", "Nat", "Nat"], "Option<NatVec>"),
    ("gb_reduce", &["NatVec", "NatVec", "NatVec", "Nat", "Nat", "Nat"], "Option<NatVec>"),
    ("mv_add", &["NatVec", "NatVec", "Nat", "Nat"], "Option<NatVec>"),
    ("mv_sub", &["NatVec", "NatVec", "Nat", "Nat"], "Option<NatVec>"),
    ("mv_mul", &["NatVec", "NatVec", "Nat", "Nat"], "Option<NatVec>"),
    ("mv_normalize", &["NatVec", "Nat", "Nat", "Nat"], "Option<NatVec>"),
    // v0.121: SAT certificates and counting, integer programming (docs/PROOFS_ILP.md)
    ("sat_proof", &["NatVec", "NatVec", "Nat"], "Option<NatVec>"),
    ("sat_check", &["NatVec", "NatVec", "Nat", "NatVec"], "Option<Bool>"),
    ("sat_count", &["NatVec", "NatVec", "Nat"], "Option<Nat>"),
    ("ilp_solve", &["FloatVec", "FloatVec", "FloatVec", "Nat", "Nat", "NatVec"], "Option<FloatVec>"),
    ("ilp_status", &["FloatVec", "FloatVec", "FloatVec", "Nat", "Nat", "NatVec"], "Nat"),
    // v0.122: ordered maps NatMap / TextMap (docs/MAPS.md)
    ("km_empty", &[], "NatMap"),
    ("km_insert", &["NatMap", "Nat", "Nat"], "Option<NatMap>"),
    ("km_get", &["NatMap", "Nat"], "Option<Nat>"),
    ("km_remove", &["NatMap", "Nat"], "NatMap"),
    ("km_contains", &["NatMap", "Nat"], "Bool"),
    ("km_len", &["NatMap"], "Nat"),
    ("km_keys", &["NatMap"], "NatVec"),
    ("km_values", &["NatMap"], "NatVec"),
    ("km_from", &["NatVec", "NatVec"], "Option<NatMap>"),
    ("km_add", &["NatMap", "Nat", "Nat"], "Option<NatMap>"),
    ("km_min", &["NatMap"], "Option<Nat>"),
    ("km_max", &["NatMap"], "Option<Nat>"),
    ("km_floor", &["NatMap", "Nat"], "Option<Nat>"),
    ("km_ceil", &["NatMap", "Nat"], "Option<Nat>"),
    ("km_merge", &["NatMap", "NatMap"], "Option<NatMap>"),
    ("km_range", &["NatMap", "Nat", "Nat"], "NatMap"),
    ("tm_empty", &[], "TextMap"),
    ("tm_insert", &["TextMap", "Text", "Nat"], "Option<TextMap>"),
    ("tm_get", &["TextMap", "Text"], "Option<Nat>"),
    ("tm_remove", &["TextMap", "Text"], "TextMap"),
    ("tm_contains", &["TextMap", "Text"], "Bool"),
    ("tm_len", &["TextMap"], "Nat"),
    ("tm_keys", &["TextMap"], "List<Text>"),
    ("tm_values", &["TextMap"], "NatVec"),
    ("tm_from", &["List<Text>", "NatVec"], "Option<TextMap>"),
    ("tm_add", &["TextMap", "Text", "Nat"], "Option<TextMap>"),
    ("tm_merge", &["TextMap", "TextMap"], "Option<TextMap>"),
    ("tm_count_words", &["Text"], "TextMap"),
    // v0.123: sparse solvers, exact predicates, Delaunay, k-d trees, signal kernels, SHA-256 (docs/KERNELS.md)
    ("la_pcg", &["NatVec", "NatVec", "FloatVec", "FloatVec", "Float", "Nat"], "Option<FloatVec>"),
    ("la_bicgstab", &["NatVec", "NatVec", "FloatVec", "FloatVec", "Float", "Nat"], "Option<FloatVec>"),
    ("la_gmres", &["NatVec", "NatVec", "FloatVec", "FloatVec", "Float", "Nat", "Nat"], "Option<FloatVec>"),
    ("geo_orient2d", &["FloatVec"], "Option<Nat>"),
    ("geo_incircle", &["FloatVec"], "Option<Nat>"),
    ("geo_orient3d", &["FloatVec"], "Option<Nat>"),
    ("geo_delaunay", &["FloatVec"], "Option<NatVec>"),
    ("kd_knn", &["FloatVec", "FloatVec", "Nat", "Nat"], "Option<NatVec>"),
    ("kd_radius", &["FloatVec", "Nat", "FloatVec", "Float"], "Option<NatVec>"),
    ("fft2_forward", &["FloatVec", "Nat", "Nat"], "Option<FloatVec>"),
    ("fft2_inverse", &["FloatVec", "Nat", "Nat"], "Option<FloatVec>"),
    ("sig_stft", &["FloatVec", "FloatVec", "Nat"], "Option<FloatVec>"),
    ("sig_dwt", &["FloatVec", "Nat", "Nat"], "Option<FloatVec>"),
    ("sig_idwt", &["FloatVec", "Nat", "Nat"], "Option<FloatVec>"),
    ("sig_iir", &["FloatVec", "FloatVec", "FloatVec"], "Option<FloatVec>"),
    ("sig_sha256", &["Text"], "Text"),
    ("bvh_raycast", &["FloatVec", "FloatVec", "FloatVec"], "Option<FloatVec>"),
    ("bvh_winding", &["FloatVec", "FloatVec"], "Option<FloatVec>"),
    ("bvh_pairs", &["FloatVec"], "Option<NatVec>"),
    // v0.127: raster images `[w, h, 0xRRGGBBAA...]` (crate::gfx)
    ("gx_new", &["Nat", "Nat", "Nat"], "Option<NatVec>"),
    ("gx_width", &["NatVec"], "Nat"),
    ("gx_height", &["NatVec"], "Nat"),
    ("gx_get", &["NatVec", "Nat", "Nat"], "Option<Nat>"),
    ("gx_fill_rect", &["NatVec", "Nat", "Nat", "Nat", "Nat", "Nat"], "NatVec"),
    ("gx_line", &["NatVec", "Nat", "Nat", "Nat", "Nat", "Nat"], "NatVec"),
    ("gx_circle", &["NatVec", "Nat", "Nat", "Nat", "Nat", "Bool"], "NatVec"),
    ("gx_text", &["NatVec", "Nat", "Nat", "Text", "Nat", "Nat"], "NatVec"),
    ("gx_text_width", &["Text", "Nat"], "Nat"),
    ("gx_blit", &["NatVec", "NatVec", "Nat", "Nat"], "NatVec"),
    ("gx_checksum", &["NatVec"], "Nat"),
    // v0.128: vector scenes (crate::vector)
    ("vx_render", &["NatVec", "Nat", "Nat", "Nat", "Nat"], "Option<NatVec>"),
    ("vx_render_region", &["NatVec", "NatVec", "Nat", "Nat", "Nat", "Nat", "Nat", "Nat"], "Option<NatVec>"),
    ("vx_bounds", &["NatVec", "Nat", "Nat"], "Option<NatVec>"),
    ("vx_text_width", &["Text", "Nat"], "Nat"),
    ("vx_svg", &["NatVec", "Nat", "Nat"], "Option<Text>"),
    ("vx_text", &["Nat", "Nat", "Nat", "Nat", "NatVec", "Text"], "NatVec"),
    // v0.129: TrueType text (fonts by registry id; `file_font_load`), code points.
    ("vx_text_fonts", &["Nat", "Nat", "Nat", "Nat", "NatVec", "NatVec", "Text"], "NatVec"),
    ("vx_font_width", &["NatVec", "Text", "Nat"], "Nat"),
    ("vx_font_has", &["Nat", "Nat"], "Bool"),
    ("vx_font_advance", &["Nat", "Nat", "Nat"], "Nat"),
    ("vx_font_metrics", &["Nat"], "NatVec"),
    ("vx_codepoints", &["Text"], "NatVec"),
    ("vx_from_codepoints", &["NatVec"], "Text"),
];

/// v0.109: file handle builtins `(name, parameter types, result type)`.
pub const FILE_BUILTINS: &[(&str, &[&str], &str)] = &[
    ("file_open", &["Text", "Text"], "File"),
    ("file_ok", &["File"], "Bool"),
    ("file_error", &["File"], "Text"),
    ("file_write", &["File", "Text"], "Bool"),
    ("file_write_bytes", &["File", "ByteBuf"], "Bool"),
    ("file_read_line", &["File"], "Option<Text>"),
    ("file_read_all", &["File"], "Text"),
    ("file_read_bytes", &["File", "Nat"], "ByteBuf"),
    ("file_flush", &["File"], "Bool"),
    ("file_close", &["File"], "Result<Nat,Text>"),
    ("file_remove", &["Text"], "Result<Nat,Text>"),
    ("file_set_buffered", &["File", "Bool"], "Bool"),
    ("file_seek", &["File", "Nat"], "Bool"),
    ("file_tell", &["File"], "Nat"),
    ("file_size", &["File"], "Nat"),
    ("file_truncate", &["File", "Nat"], "Bool"),
    ("file_exists", &["Text"], "Bool"),
    ("file_is_dir", &["Text"], "Bool"),
    ("file_size_of", &["Text"], "Result<Nat,Text>"),
    ("file_mtime", &["Text"], "Result<Nat,Text>"),
    ("file_list_dir", &["Text"], "Result<List<Text>,Text>"),
    ("file_walk", &["Text"], "Result<List<Text>,Text>"),
    ("file_make_dir", &["Text"], "Result<Nat,Text>"),
    ("file_remove_dir", &["Text"], "Result<Nat,Text>"),
    ("file_rename", &["Text", "Text"], "Result<Nat,Text>"),
    ("file_copy", &["Text", "Text"], "Result<Nat,Text>"),
    ("file_write_atomic", &["Text", "Text"], "Result<Nat,Text>"),
    ("file_write_png", &["Text", "NatVec"], "Result<Nat,Text>"),
    // v0.126: processes, environment, TCP (crate::os)
    ("proc_run", &["Text", "List<Text>", "Text"], "Result<List<Text>,Text>"),
    ("proc_env", &["Text"], "Option<Text>"),
    ("net_resolve", &["Text"], "Result<List<Text>,Text>"),
    ("net_listen", &["Text", "Nat"], "Result<Nat,Text>"),
    ("net_local_port", &["Nat"], "Result<Nat,Text>"),
    ("net_accept", &["Nat", "Nat"], "Result<Nat,Text>"),
    ("net_connect", &["Text", "Nat", "Nat"], "Result<Nat,Text>"),
    ("net_send", &["Nat", "Text"], "Result<Nat,Text>"),
    ("net_recv_line", &["Nat"], "Result<Option<Text>,Text>"),
    ("net_recv_all", &["Nat"], "Result<Text,Text>"),
    ("net_shutdown", &["Nat"], "Result<Nat,Text>"),
    ("net_close", &["Nat"], "Result<Nat,Text>"),
    ("proc_spawn", &["Text", "List<Text>", "Text"], "Result<Nat,Text>"),
    ("proc_poll", &["Nat"], "Result<Option<Nat>,Text>"),
    ("proc_wait", &["Nat"], "Result<List<Text>,Text>"),
    ("proc_kill", &["Nat"], "Result<Nat,Text>"),
    // v0.128: native windows (crate::window)
    ("win_open", &["Text", "Nat", "Nat"], "Result<Nat,Text>"),
    ("win_present", &["Nat", "NatVec"], "Result<Nat,Text>"),
    ("win_poll", &["Nat", "Nat"], "Result<NatVec,Text>"),
    ("win_size", &["Nat"], "Result<NatVec,Text>"),
    ("win_close", &["Nat"], "Result<Nat,Text>"),
    // v0.131: GPU presentation (OpenGL): enable, draw a vector scene, read the frame back
    ("win_gpu", &["Nat"], "Result<Text,Text>"),
    ("win_present_scene", &["Nat", "NatVec", "Nat", "Nat", "Nat", "Nat"], "Result<Nat,Text>"),
    ("win_read", &["Nat"], "Result<NatVec,Text>"),
    ("file_font_load", &["Text"], "Result<Nat,Text>"),
    ("file_font_find", &["Text"], "Result<Text,Text>"),
];

/// v0.107: dense natural numbers (`BigNat`, `crate::dense_nat`): pure, total
/// operations; `none` for a zero divisor, an invalid input or a result above
/// `dense_nat::MAX_BITS` bits. `(name, parameter types, result type)`.
pub const DENSE_NAT_BUILTINS: &[(&str, &[&str], &str)] = &[
    ("bn_from_nat", &["Nat"], "BigNat"),
    ("bn_to_nat", &["BigNat"], "Option<Nat>"),
    ("bn_from_words", &["List<Nat>"], "Option<BigNat>"),
    ("bn_to_words", &["BigNat"], "List<Nat>"),
    ("bn_from_text", &["Text"], "Option<BigNat>"),
    ("bn_to_text", &["BigNat"], "Text"),
    ("bn_bits", &["BigNat"], "Nat"),
    ("bn_limbs", &["BigNat"], "Nat"),
    ("bn_cmp", &["BigNat", "BigNat"], "Int"),
    ("bn_add", &["BigNat", "BigNat"], "Option<BigNat>"),
    ("bn_monus", &["BigNat", "BigNat"], "BigNat"),
    ("bn_mul", &["BigNat", "BigNat"], "Option<BigNat>"),
    ("bn_div", &["BigNat", "BigNat"], "Option<BigNat>"),
    ("bn_mod", &["BigNat", "BigNat"], "Option<BigNat>"),
    ("bn_shl", &["BigNat", "Nat"], "Option<BigNat>"),
    ("bn_shr", &["BigNat", "Nat"], "BigNat"),
    ("bn_gcd", &["BigNat", "BigNat"], "BigNat"),
    ("bn_sqrt", &["BigNat"], "BigNat"),
    ("bn_powmod", &["BigNat", "BigNat", "BigNat"], "Option<BigNat>"),
    ("bn_modinv", &["BigNat", "BigNat"], "Option<BigNat>"),
    ("bn_is_prime", &["BigNat"], "Bool"),
    ("bn_next_prime", &["BigNat"], "Option<BigNat>"),
    ("bn_random", &["Nat", "Nat"], "Option<BigNat>"),
    ("bn_to_hex", &["BigNat"], "Text"),
    ("bn_from_hex", &["Text"], "Option<BigNat>"),
];

/// v0.85: Int builtins and the operators `-` (`__sub`) and unary `-` (`__neg`).
pub const INT_BUILTINS: &[&str] = &["int", "abs", "int_eq", "div", "mod", "int_to_text", "text_to_int", "__sub", "__neg"];

/// v0.83: Text builtins (pure and total).
pub const TEXT_BUILTINS: &[&str] =
    &["concat", "text_length", "text_eq", "nat_to_text", "text_to_nat", "substring", "lines", "words", "join"];

/// v0.83: passport of a Text literal.
pub(crate) fn text_literal(theory: &str) -> Passport {
    let mut passport = typed_value(theory, "Text", &[], "created:literal_text");
    passport.construction = ConstructionMode::Literal;
    passport
}

/// v0.74: default operational fuel (maximum recursion depth) for a recursive
/// function without an explicit `fuel N` clause.
pub const DEFAULT_RECURSION_FUEL: u64 = 10_000;

/// v0.74: largest accepted `fuel`; equals the runtime call-depth guard.
pub const MAX_RECURSION_FUEL: u64 = crate::runtime::MAX_CALL_DEPTH as u64;

/// Effect of a builtin used inside a function body: `(kind, boundary, partial)`.
pub(crate) fn builtin_effect(name: &str) -> Option<(FunctionEffectKind, &'static str, bool)> {
    match name {
        "__enum_payload" => Some((FunctionEffectKind::Runtime, "enum_payload", true)),
        "print_decimal" | "print_text" | "print_symbolic" | "print_line" => Some((FunctionEffectKind::Io, "stdout", false)),
        "read_line" | "read_all" => Some((FunctionEffectKind::Io, "stdin", false)),
        "args" => Some((FunctionEffectKind::Io, "args", false)),
        "read_nat" | "read" | "read_stdin" | "parse_nat" => Some((FunctionEffectKind::Io, "stdin", true)),
        "require" => Some((FunctionEffectKind::Runtime, "require", true)),
        // v0.102: allocation, mutation and release of an owned buffer; the
        // constructors and mutators fail on out-of-range lengths, indices or bytes.
        "buf_new" | "buf_from_list" | "buf_set" | "buf_push" | "buf_slice" | "buf_append" | "buf_from_text" => {
            Some((FunctionEffectKind::Runtime, "byte_buffer", true))
        }
        "buf_len" | "buf_get" | "buf_to_list" | "buf_free" | "buf_copy" | "buf_eq" | "buf_to_text" => {
            Some((FunctionEffectKind::Runtime, "byte_buffer", false))
        }
        "tbuf_push" => Some((FunctionEffectKind::Runtime, "text_buffer", true)),
        // v0.109: file handles; failures are values (sticky handle errors), never runtime errors.
        f if f.starts_with("file_") && FILE_BUILTINS.iter().any(|(n, _, _)| *n == f) => Some((FunctionEffectKind::Io, "file", false)),
        f if f.starts_with("proc_") && FILE_BUILTINS.iter().any(|(n, _, _)| *n == f) => Some((FunctionEffectKind::Io, "process", false)),
        f if f.starts_with("net_") && FILE_BUILTINS.iter().any(|(n, _, _)| *n == f) => Some((FunctionEffectKind::Io, "net", false)),
        f if f.starts_with("win_") && FILE_BUILTINS.iter().any(|(n, _, _)| *n == f) => Some((FunctionEffectKind::Io, "window", false)),
        "tbuf_new" | "tbuf_len" | "tbuf_finish" => Some((FunctionEffectKind::Runtime, "text_buffer", false)),
        "copy_to_gpu" | "gpu_upload" | "upload_to_gpu" | "copy_from_gpu" | "gpu_download" | "download_from_gpu"
        | "launch_kernel" | "gpu_launch" | "run_kernel" => Some((FunctionEffectKind::GpuExecution, "gpu", false)),
        "deploy_portable" | "deploy_on" | "schedule" | "migrate" | "live_migrate" | "materialize_remote"
        | "checkpoint_remote" | "restore_remote" => Some((FunctionEffectKind::RemoteExecution, "remote", false)),
        _ => None,
    }
}

pub(super) fn called_names(expr: &Expr, out: &mut Vec<String>) {
    match &expr.kind {
        ExprKind::Call { name, args } => {
            out.push(name.clone());
            // field(record, label): the label is metadata, not a value reference.
            let values = if name == "field" && args.len() == 2 { &args[..1] } else { args };
            for arg in values {
                called_names(arg, out);
            }
        }
        ExprKind::Add { lhs, rhs } | ExprKind::CompareGt { lhs, rhs } => {
            called_names(lhs, out);
            called_names(rhs, out);
        }
        ExprKind::Power { base, exp } => {
            called_names(base, out);
            called_names(exp, out);
        }
        ExprKind::Ident(name) => out.push(name.clone()),
        ExprKind::Lambda { body, .. } => called_names(body, out),
        ExprKind::Let { value, body, .. } => {
            called_names(value, out);
            called_names(body, out);
        }
        ExprKind::IntLiteral(_) | ExprKind::TextLiteral(_) | ExprKind::QualifiedIdent { .. } => {}
    }
}

fn arity(name: &str, args: &[Expr], expected: usize, line: usize) -> Result<(), Diagnostic> {
    if args.len() == expected {
        Ok(())
    } else {
        Err(Diagnostic::error(
            DiagnosticKind::ParseError,
            Some(line),
            format!("{name} expects {expected} argument(s), got {}", args.len()),
        ))
    }
}

fn nat_caps(exact: bool) -> CapabilitySet {
    if exact {
        CapabilitySet::from([
            Capability::CanAddAsNat,
            Capability::CanPrintDecimal,
            Capability::CanSymbolicPrint,
            Capability::CanSerializeForMigration,
            Capability::CanCompilePortableCode,
            Capability::CanCompareDirect,
            Capability::CanComputeModular,
        ])
    } else {
        CapabilitySet::from([
            Capability::CanAddAsNat,
            Capability::CanSymbolicPrint,
            Capability::CanSerializeForMigration,
            Capability::CanCompilePortableCode,
            Capability::CanCompareByProof,
            Capability::CanComputeModular,
        ])
    }
}

fn value_caps() -> CapabilitySet {
    CapabilitySet::from([
        Capability::CanSymbolicPrint,
        Capability::CanSerializeForMigration,
        Capability::CanCompilePortableCode,
    ])
}

/// Passport for an ordinary value of canonical type `ty` derived from
/// `sources`. Taint, provenance and validation never decrease.
pub(crate) fn typed_value(theory: &str, ty: &str, sources: &[&Passport], event: impl Into<String>) -> Passport {
    let exact_nat = sources
        .iter()
        .filter(|s| s.ty == TypeKind::Nat)
        .all(|s| s.capabilities.contains(Capability::CanPrintDecimal));
    let (kind, capabilities) = match ty {
        "Nat" => (TypeKind::Nat, nat_caps(exact_nat)),
        "Bool" => {
            let mut caps = value_caps();
            caps.insert(Capability::CanCompareDirect);
            (TypeKind::Bool, caps)
        }
        "Text" => (TypeKind::Text, CapabilitySet::from([Capability::CanSymbolicPrint])),
        // v0.85: Int values compare directly and print as decimals.
        "Int" => {
            let mut caps = value_caps();
            caps.insert(Capability::CanCompareDirect);
            caps.insert(Capability::CanPrintDecimal);
            (TypeKind::TypedValue { of_type: "Int".to_string() }, caps)
        }
        other => (TypeKind::TypedValue { of_type: other.to_string() }, value_caps()),
    };
    let history = if sources.is_empty() {
        HistoryChain::from_event(event)
    } else {
        HistoryChain::merge_many(sources.iter().map(|s| &s.history), event)
    };
    Passport {
        ty: kind,
        construction: ConstructionMode::Definable,
        capabilities,
        cost: sources.iter().map(|s| s.cost).max().unwrap_or(CostClass::SmallFinite).max(CostClass::SmallFinite),
        trust: sources.iter().map(|s| s.trust).max().unwrap_or(TrustLevel::Checked),
        provenance: sources
            .iter()
            .map(|s| s.provenance)
            .max()
            .unwrap_or(Provenance::InternalDerived)
            .max(Provenance::InternalDerived),
        validation: sources.iter().map(|s| s.validation).max().unwrap_or(ValidationState::StaticChecked),
        theory: TheoryContext::new(theory),
        history,
        location: LocationContext::local(),
    }
}

fn bool_literal(theory: &str, value: bool) -> Passport {
    Passport {
        ty: TypeKind::Bool,
        construction: ConstructionMode::Literal,
        capabilities: CapabilitySet::from([
            Capability::CanSymbolicPrint,
            Capability::CanSerializeForMigration,
            Capability::CanCompilePortableCode,
            Capability::CanCompareDirect,
        ]),
        cost: CostClass::Trivial,
        trust: TrustLevel::Checked,
        provenance: Provenance::InternalLiteral,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_event(format!("created:literal_bool:{value}")),
        location: LocationContext::local(),
    }
}

pub(super) fn static_evidence(theory: &str, predicate: String, event: String) -> Passport {
    Passport {
        ty: TypeKind::StaticProof(predicate),
        construction: ConstructionMode::ProofFinite,
        capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
        cost: CostClass::ProofRequired,
        trust: TrustLevel::Checked,
        provenance: Provenance::InternalDerived,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_event(event),
        location: LocationContext::local(),
    }
}

fn recursion_error(line: usize, message: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::RecursionBoundaryError, Some(line), message).with_help(
        "declare a measure and decrease it under a guard, e.g. \
         fn f(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 1, f(pred(n))) or \
         fn g(xs: List<Nat>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, g(tail(xs)))",
    )
}

/// v0.74/v0.75: decidable well-foundedness check for a recursive `fn`.
///
/// For each measured parameter `m`:
///
/// ```text
/// Nat m        decreasing argument  pred(m) | monus(m, k)  with literal k >= 1
///              guard                then-branch of  m > k | not(nat_eq(m, 0))
///                                   else-branch of  nat_eq(m, 0) | nat_eq(0, m)
/// List/Seq m   decreasing argument  tail(m)
///              guard                then-branch of  not(is_empty(m)) | length(m) > k
///                                   else-branch of  is_empty(m) | nat_eq(length(m), 0)
/// ```
///
/// A simple measure `decreasing m` needs every recursive call to pass a
/// decreasing argument for `m` under a guard for `m`.
///
/// A lexicographic measure `decreasing (m1, ..., mk)` (v0.75) accepts a call
/// when some position `i` passes a guarded decreasing argument for `mi` and
/// every earlier position passes `mj` unchanged. Later positions are
/// unrestricted. This is the standard lexicographic order on `Nat^k`, which is
/// well-founded.
///
/// `if_then_else` is lazy at runtime, so an unguarded branch is never
/// evaluated. The function may only be referenced through direct calls, and
/// never inside a lambda (a lambda may be called outside the guard).
fn check_well_founded(decl: &FnDecl) -> Result<RecursionMeasureKind, Diagnostic> {
    check_well_founded_in(decl, &[])
}

/// v0.125: a `loop` function may refer to itself only by direct calls in tail
/// position: the body itself, both branches of a tail `if_then_else` and the
/// body of a tail `let` (`match` is desugared into these). The runtime and the
/// C backend execute such calls as iterations, never as nested calls.
fn check_tail_loop(decl: &FnDecl) -> Result<(), Diagnostic> {
    fn walk(decl: &FnDecl, e: &Expr, tail: bool) -> Result<(), Diagnostic> {
        let me = decl.name.as_str();
        match &e.kind {
            ExprKind::Call { name, args } if name == me => {
                if !tail {
                    return Err(recursion_error(e.line, format!("fn '{me}' is declared `loop`; its recursive calls must be in tail position"))
                        .with_help("a recursive call is in tail position when its value is the function's result: the body, a branch of a tail if_then_else, or the body of a tail let / match arm"));
                }
                args.iter().try_for_each(|a| walk(decl, a, false))
            }
            ExprKind::Call { name, args } if name == "if_then_else" && args.len() == 3 => {
                walk(decl, &args[0], false)?;
                walk(decl, &args[1], tail)?;
                walk(decl, &args[2], tail)
            }
            ExprKind::Call { args, .. } => args.iter().try_for_each(|a| walk(decl, a, false)),
            ExprKind::Let { value, body, .. } => {
                walk(decl, value, false)?;
                walk(decl, body, tail)
            }
            ExprKind::Lambda { body, .. } => {
                let mut calls = Vec::new();
                called_names(body, &mut calls);
                if calls.iter().any(|c| c == me) {
                    return Err(recursion_error(e.line, format!("fn '{me}' is declared `loop`; it cannot be called from a lambda")));
                }
                walk(decl, body, false)
            }
            ExprKind::Ident(name) if name == me => Err(recursion_error(e.line, format!("fn '{me}' is declared `loop`; it cannot be used as a value"))),
            ExprKind::Add { lhs, rhs } | ExprKind::CompareGt { lhs, rhs } => {
                walk(decl, lhs, false)?;
                walk(decl, rhs, false)
            }
            ExprKind::Power { base, exp } => {
                walk(decl, base, false)?;
                walk(decl, exp, false)
            }
            _ => Ok(()),
        }
    }
    walk(decl, &decl.body, true)
}

/// v0.81: the measured parameter (index, kind) of a mutual-group member.
fn single_measure(decl: &FnDecl) -> Result<(usize, RecursionMeasureKind), Diagnostic> {
    let line = decl.line;
    let measure = decl.measure.as_ref().ok_or_else(|| {
        recursion_error(line, format!("fn '{}' is in a mutual block but declares no measure", decl.name))
    })?;
    let [name] = measure.params.as_slice() else {
        return Err(recursion_error(
            line,
            format!("fn '{}' is in a mutual block; mutual recursion supports one measured parameter", decl.name),
        ));
    };
    let index = decl
        .params
        .iter()
        .position(|p| &p.name == name)
        .ok_or_else(|| recursion_error(line, format!("measure '{name}' is not a parameter of '{}'", decl.name)))?;
    let ty = &decl.params[index].ty;
    let kind = if ty == "Nat" {
        RecursionMeasureKind::NatDecreasing
    } else if collection_item(ty).is_some() {
        RecursionMeasureKind::StructuralSubterm
    } else {
        return Err(recursion_error(line, format!("measure '{name}' has type {ty}; only Nat or List/Sequence parameters can decrease")));
    };
    Ok((index, kind))
}

/// v0.74 / v0.81: well-foundedness of `decl`, whose body may also call the
/// other members of its mutual `group`. Every call to a group member must be
/// guarded by `decl`'s measure check and pass a strictly smaller value of
/// `decl`'s measure at the callee's measured position, so the shared measure
/// decreases on every step around any cycle of the group.
fn check_well_founded_in(decl: &FnDecl, group: &[FnDecl]) -> Result<RecursionMeasureKind, Diagnostic> {
    let line = decl.line;
    let mut others = Vec::new();
    for member in group.iter().filter(|m| m.name != decl.name) {
        let (index, kind) = single_measure(member)?;
        others.push((member.name.as_str(), index, kind));
    }
    if !group.is_empty() {
        let (_, own_kind) = single_measure(decl)?;
        if let Some((name, _, _)) = others.iter().find(|(_, _, kind)| *kind != own_kind) {
            return Err(recursion_error(
                line,
                format!("mutual functions '{}' and '{name}' measure different kinds of values", decl.name),
            ));
        }
    }
    let Some(measure) = &decl.measure else {
        return Err(recursion_error(line, format!("fn '{}' is recursive but declares no measure", decl.name)));
    };
    let mut measured = Vec::with_capacity(measure.params.len());
    for name in &measure.params {
        let index = decl
            .params
            .iter()
            .position(|p| &p.name == name)
            .ok_or_else(|| recursion_error(line, format!("measure '{name}' is not a parameter of '{}'", decl.name)))?;
        let ty = &decl.params[index].ty;
        let record = record_type_name(ty).is_some();
        let kind = if ty == "Nat" {
            RecursionMeasureKind::NatDecreasing
        } else if collection_item(ty).is_some() || record {
            RecursionMeasureKind::StructuralSubterm
        } else {
            return Err(recursion_error(
                line,
                format!("measure '{name}' has type {ty}; only Nat, List/Sequence or record (enum) parameters can decrease"),
            ));
        };
        measured.push(Measured { name: name.as_str(), index, kind, record });
    }
    let ctx = MeasureCtx { function: &decl.name, measured, others, aliases: std::cell::RefCell::new(Vec::new()) };
    // v0.126: a record measure needs no guard: its smaller values are payloads
    // unwrapped from the parameter (the unwrap fails instead of recursing).
    let initial: Vec<bool> = ctx.measured.iter().map(|m| m.record).collect();
    ctx.walk(&decl.body, &initial, false)?;
    Ok(if ctx.measured.len() == 1 { ctx.measured[0].kind } else { RecursionMeasureKind::Lexicographic })
}

struct Measured<'a> {
    name: &'a str,
    index: usize,
    kind: RecursionMeasureKind,
    /// v0.126: a recursive record (enum) parameter.
    record: bool,
}

/// v0.126: `__match_variant(field(x, f), _)` / `__match_some(field(x, f))` /
/// `__match_head(field(x, f))` — a value stored inside record `x`, for which
/// `inner(x)` holds. Such values are strictly smaller (records are finite).
fn record_payload_of(arg: &Expr, inner: &dyn Fn(&Expr) -> bool) -> bool {
    let ExprKind::Call { name, args } = &arg.kind else { return false };
    let component = match name.as_str() {
        "field" if args.len() == 2 => &args[0],
        "__match_variant" | "__match_some" | "__match_head" if !args.is_empty() => &args[0],
        _ => return false,
    };
    inner(component) || record_payload_of(component, inner)
}

impl Measured<'_> {
    fn is_param(&self, expr: &Expr) -> bool {
        matches!(&expr.kind, ExprKind::Ident(name) if name == self.name)
    }

    fn is_zero(expr: &Expr) -> bool {
        matches!(&expr.kind, ExprKind::IntLiteral(v) if v.trim_start_matches('0').is_empty())
    }

    fn is_size(&self, expr: &Expr) -> bool {
        match self.kind {
            RecursionMeasureKind::NatDecreasing => self.is_param(expr),
            _ => matches!(&expr.kind, ExprKind::Call { name, args } if name == "length" && args.len() == 1 && self.is_param(&args[0])),
        }
    }

    /// `cond` true implies `m` positive / non-empty.
    fn guards_then(&self, cond: &Expr) -> bool {
        match &cond.kind {
            ExprKind::CompareGt { lhs, rhs } => self.is_size(lhs) && matches!(rhs.kind, ExprKind::IntLiteral(_)),
            ExprKind::Call { name, args } if name == "not" && args.len() == 1 => self.guards_else(&args[0]),
            _ => false,
        }
    }

    /// `cond` false implies `m` positive / non-empty.
    fn guards_else(&self, cond: &Expr) -> bool {
        match &cond.kind {
            ExprKind::Call { name, args } if name == "nat_eq" && args.len() == 2 => {
                (self.is_size(&args[0]) && Self::is_zero(&args[1])) || (Self::is_zero(&args[0]) && self.is_size(&args[1]))
            }
            ExprKind::Call { name, args } if name == "is_empty" && args.len() == 1 => {
                self.kind == RecursionMeasureKind::StructuralSubterm && self.is_param(&args[0])
            }
            ExprKind::Call { name, args } if name == "not" && args.len() == 1 => self.guards_then(&args[0]),
            _ => false,
        }
    }

    fn decreases(&self, arg: &Expr) -> bool {
        match (&arg.kind, self.kind) {
            (ExprKind::Call { name, args }, RecursionMeasureKind::NatDecreasing) if name == "pred" && args.len() == 1 => {
                self.is_param(&args[0])
            }
            (ExprKind::Call { name, args }, RecursionMeasureKind::NatDecreasing) if name == "monus" && args.len() == 2 => {
                self.is_param(&args[0])
                    && matches!(&args[1].kind, ExprKind::IntLiteral(v) if v.parse::<u128>().map(|k| k >= 1).unwrap_or(true))
            }
            (ExprKind::Call { name, args }, RecursionMeasureKind::StructuralSubterm) if !self.record && name == "tail" && args.len() == 1 => {
                self.is_param(&args[0])
            }
            (_, RecursionMeasureKind::StructuralSubterm) if self.record => record_payload_of(arg, &|e| self.is_param(e)),
            _ => false,
        }
    }

    fn decrease_hint(&self) -> String {
        if self.record {
            format!("a variant field of {} (a `match` binding)", self.name)
        } else if self.kind == RecursionMeasureKind::NatDecreasing {
            format!("pred({0}) or monus({0}, k) with literal k >= 1", self.name)
        } else {
            format!("tail({})", self.name)
        }
    }

    fn guard_word(&self) -> &'static str {
        if self.kind == RecursionMeasureKind::NatDecreasing {
            "positive"
        } else {
            "non-empty"
        }
    }
}

struct MeasureCtx<'a> {
    function: &'a str,
    measured: Vec<Measured<'a>>,
    /// v0.81: other members of the mutual group: (name, measured index, kind).
    others: Vec<(&'a str, usize, RecursionMeasureKind)>,
    /// v0.82: `let` bindings in scope: (name, measured position whose
    /// decreased value it holds, or `None`).
    aliases: std::cell::RefCell<Vec<(String, Option<usize>)>>,
}

impl MeasureCtx<'_> {
    /// `arg` passes a strictly smaller value of measured position `position`:
    /// `pred(m)` / `monus(m, k)` / `tail(m)`, a `let` name bound to one, or
    /// `pred` / `monus` / `tail` of such a name (never larger than the name).
    fn arg_decreases(&self, position: usize, arg: &Expr) -> bool {
        if self.measured[position].decreases(arg) {
            return true;
        }
        let is_alias = |expr: &Expr| match &expr.kind {
            ExprKind::Ident(name) => self
                .aliases
                .borrow()
                .iter()
                .rev()
                .find(|(alias, _)| alias == name)
                .is_some_and(|(_, measured)| *measured == Some(position)),
            _ => false,
        };
        if is_alias(arg) {
            return true;
        }
        let structural = self.measured[position].kind == RecursionMeasureKind::StructuralSubterm;
        if self.measured[position].record {
            return record_payload_of(arg, &|e| self.measured[position].is_param(e) || is_alias(e));
        }
        match &arg.kind {
            ExprKind::Call { name, args } if !structural && (name == "pred" || name == "monus") && !args.is_empty() => {
                (name == "pred" || matches!(args.get(1).map(|a| &a.kind), Some(ExprKind::IntLiteral(_)))) && is_alias(&args[0])
            }
            ExprKind::Call { name, args } if structural && name == "tail" && args.len() == 1 => is_alias(&args[0]),
            _ => false,
        }
    }

    /// v0.127: `(measured position, element parameter)` when `collection` is a
    /// list stored inside a measured record (a decreased value) and `lambda` is
    /// a literal lambda whose element parameter is the last one.
    fn record_element_lambda(&self, collection: &Expr, lambda: &Expr) -> Option<(usize, String)> {
        let ExprKind::Lambda { params, .. } = &lambda.kind else { return None };
        let element = params.last()?.name.clone();
        let position = (0..self.measured.len()).find(|&i| self.measured[i].record && self.arg_decreases(i, collection))?;
        Some((position, element))
    }

    fn is_group_member(&self, name: &str) -> bool {
        name == self.function || self.others.iter().any(|(other, _, _)| *other == name)
    }

    /// v0.81: a call to another member of the mutual group.
    fn check_mutual_call(&self, callee: &str, callee_index: usize, args: &[Expr], guarded: &[bool], line: usize) -> Result<(), Diagnostic> {
        let own = &self.measured[0];
        if !guarded[0] {
            return Err(recursion_error(
                line,
                format!("mutual call from '{}' to '{callee}' is not guarded by a check that '{}' is {}", self.function, own.name, own.guard_word()),
            ));
        }
        match args.get(callee_index) {
            Some(arg) if self.arg_decreases(0, arg) => Ok(()),
            _ => Err(recursion_error(
                line,
                format!("mutual call from '{}' to '{callee}' must pass {} as its measured argument", self.function, own.decrease_hint()),
            )),
        }
    }

    fn check_call(&self, args: &[Expr], guarded: &[bool], line: usize) -> Result<(), Diagnostic> {
        if let [single] = self.measured.as_slice() {
            if !guarded[0] {
                return Err(recursion_error(
                    line,
                    format!(
                        "recursive call to '{}' is not guarded by a check that '{}' is {}",
                        self.function,
                        single.name,
                        single.guard_word()
                    ),
                ));
            }
            return match args.get(single.index) {
                Some(arg) if self.arg_decreases(0, arg) => Ok(()),
                _ => Err(recursion_error(
                    line,
                    format!("recursive call to '{}' must pass {} as '{}'", self.function, single.decrease_hint(), single.name),
                )),
            };
        }
        for (position, measure) in self.measured.iter().enumerate() {
            let Some(arg) = args.get(measure.index) else { break };
            if guarded[position] && self.arg_decreases(position, arg) {
                return Ok(());
            }
            if !measure.is_param(arg) {
                break;
            }
        }
        let names: Vec<&str> = self.measured.iter().map(|m| m.name).collect();
        Err(recursion_error(
            line,
            format!(
                "recursive call to '{}' does not decrease the lexicographic measure ({})",
                self.function,
                names.join(", ")
            ),
        )
        .with_help(
            "some measured position must pass a guarded pred/monus/tail argument while every earlier measured position is passed unchanged",
        ))
    }

    fn walk(&self, expr: &Expr, guarded: &[bool], in_lambda: bool) -> Result<(), Diagnostic> {
        let line = expr.line;
        match &expr.kind {
            ExprKind::Call { name, args } if name == "if_then_else" && args.len() == 3 => {
                self.walk(&args[0], guarded, in_lambda)?;
                let then_guards: Vec<bool> =
                    self.measured.iter().zip(guarded).map(|(m, g)| *g || m.guards_then(&args[0])).collect();
                let else_guards: Vec<bool> =
                    self.measured.iter().zip(guarded).map(|(m, g)| *g || m.guards_else(&args[0])).collect();
                self.walk(&args[1], &then_guards, in_lambda)?;
                self.walk(&args[2], &else_guards, in_lambda)
            }
            ExprKind::Call { name, args } if name == self.function => {
                if in_lambda {
                    return Err(recursion_error(
                        line,
                        format!("'{}' may not be called inside a lambda in its own body", self.function),
                    ));
                }
                self.check_call(args, guarded, line)?;
                args.iter().try_for_each(|arg| self.walk(arg, guarded, in_lambda))
            }
            ExprKind::Call { name, args } if self.others.iter().any(|(other, _, _)| other == name) => {
                if in_lambda {
                    return Err(recursion_error(
                        line,
                        format!("mutual function '{name}' may not be called inside a lambda in '{}'", self.function),
                    ));
                }
                let (_, index, _) = self.others.iter().find(|(other, _, _)| other == name).copied().expect("member");
                self.check_mutual_call(name, index, args, guarded, line)?;
                args.iter().try_for_each(|arg| self.walk(arg, guarded, in_lambda))
            }
            ExprKind::Call { name, args } if name == "field" && args.len() == 2 => self.walk(&args[0], guarded, in_lambda),
            // v0.127: `map(xs, fun(x, ...))` / `fold(xs, a, fun(acc, x, ...))` over a list
            // stored inside a measured record: the lambda runs only on elements of `xs`,
            // which are strictly smaller, so its element parameter is a decreased alias
            // and direct recursion inside that lambda is allowed.
            ExprKind::Call { name, args }
                if !in_lambda
                    && ((name == "map" && args.len() == 2) || (name == "fold" && args.len() == 3))
                    && self.record_element_lambda(&args[0], args.last().expect("lambda")).is_some() =>
            {
                let (position, element) = self.record_element_lambda(&args[0], args.last().expect("lambda")).expect("checked");
                for arg in &args[..args.len() - 1] {
                    self.walk(arg, guarded, in_lambda)?;
                }
                let ExprKind::Lambda { params, body } = &args.last().expect("lambda").kind else { unreachable!() };
                if params.iter().any(|p| self.is_group_member(&p.name) || self.measured.iter().any(|m| m.name == p.name)) {
                    return Err(recursion_error(line, format!("lambda parameter shadows a recursive function or measured parameter of '{}'", self.function)));
                }
                let pushed = params.len();
                for p in params {
                    self.aliases.borrow_mut().push((p.name.clone(), (p.name == element).then_some(position)));
                }
                let result = self.walk(body, guarded, false);
                for _ in 0..pushed {
                    self.aliases.borrow_mut().pop();
                }
                result
            }
            ExprKind::Call { args, .. } => args.iter().try_for_each(|arg| self.walk(arg, guarded, in_lambda)),
            ExprKind::Ident(name) if self.is_group_member(name) => Err(recursion_error(
                line,
                format!("'{name}' may only be referenced through direct recursive calls"),
            )),
            ExprKind::Lambda { params, body } => {
                if params.iter().any(|p| self.is_group_member(&p.name)) {
                    return Err(recursion_error(
                        line,
                        format!("lambda parameter shadows the recursive function '{}'", self.function),
                    ));
                }
                self.walk(body, guarded, true)
            }
            ExprKind::Add { lhs, rhs } | ExprKind::CompareGt { lhs, rhs } => {
                self.walk(lhs, guarded, in_lambda)?;
                self.walk(rhs, guarded, in_lambda)
            }
            ExprKind::Power { base, exp } => {
                self.walk(base, guarded, in_lambda)?;
                self.walk(exp, guarded, in_lambda)
            }
            ExprKind::Let { name, value, body } => {
                self.walk(value, guarded, in_lambda)?;
                if self.is_group_member(name) || self.measured.iter().any(|m| m.name == name) {
                    return Err(recursion_error(
                        line,
                        format!("let binding '{name}' shadows a recursive function or a measured parameter of '{}'", self.function),
                    ));
                }
                let position = (0..self.measured.len()).find(|&i| self.arg_decreases(i, value));
                self.aliases.borrow_mut().push((name.clone(), position));
                let result = self.walk(body, guarded, in_lambda);
                self.aliases.borrow_mut().pop();
                result
            }
            ExprKind::IntLiteral(_) | ExprKind::TextLiteral(_) | ExprKind::Ident(_) | ExprKind::QualifiedIdent { .. } => Ok(()),
        }
    }
}

pub(crate) use crate::ordinary::render_expr;

/// Free identifiers of `expr` (not bound by enclosing lambdas inside it).
pub(super) fn free_idents(expr: &Expr, bound: &mut Vec<String>, out: &mut Vec<String>) {
    crate::ordinary::free_value_names(expr, bound, out)
}

impl Checker {
    /// Infer an ordinary argument and its canonical ordinary type.
    fn ordinary_arg(
        &self,
        expr: &Expr,
        theory: &str,
        op: &str,
        kind: &DiagnosticKind,
    ) -> Result<(Passport, String), Diagnostic> {
        let passport = self.infer_expr(expr, theory)?;
        match surface_type_of(&passport) {
            Some(ty) => Ok((passport, ty)),
            None => Err(Diagnostic::error(
                kind.clone(),
                Some(expr.line),
                format!("{op} expects an ordinary value, got {}", passport.ty),
            )
            .with_help(
                "ordinary operations accept Nat, Bool, Text, Option, Result, List, Sequence and Product values; proofs, theorems, truth claims, runtime witnesses and resources are not ordinary values",
            )),
        }
    }

    fn expect_type(&self, expr: &Expr, theory: &str, op: &str, expected: &str, kind: &DiagnosticKind) -> Result<Passport, Diagnostic> {
        let (passport, ty) = self.ordinary_arg(expr, theory, op, kind)?;
        if ty != expected {
            return Err(Diagnostic::error(
                kind.clone(),
                Some(expr.line),
                format!("{op} expects `{expected}`, got `{ty}`"),
            )
            .with_help("ordinary operations insert no implicit coercions"));
        }
        Ok(passport)
    }

    fn type_arg(&self, expr: &Expr, theory: &str, op: &str, kind: &DiagnosticKind) -> Result<String, Diagnostic> {
        let ty = type_from_expr(expr).map_err(|err| {
            Diagnostic::error(kind.clone(), Some(expr.line), format!("{op} type argument: {err}"))
                .with_help("type arguments are written as Nat, Bool, Text, Option(Nat), List(Nat), Result(Nat, Text), Product(Nat, Bool), Sum(Nat, Bool) or a record name")
        })?;
        crate::ownership::check_declared_type(&ty, &format!("{op} type argument"), expr.line, false)?;
        self.require_known_records(&ty, theory, expr.line)?;
        Ok(ty)
    }

    /// v0.74: every record name used in `ty` must be declared earlier in `theory`.
    pub(super) fn require_known_records(&self, ty: &str, theory: &str, line: usize) -> Result<(), Diagnostic> {
        let Ok(parsed) = parse_ordinary_type(ty) else {
            return Ok(());
        };
        if parsed.has_projection() {
            return Err(Diagnostic::error(DiagnosticKind::FunctionTermError, Some(line),
                format!("unresolved associated type projection `{ty}`")));
        }
        let mut names = Vec::new();
        parsed.record_names(&mut names);
        for name in names {
            if !self.records.get(theory).is_some_and(|records| records.contains_key(&name)) {
                return Err(Diagnostic::error(
                    DiagnosticKind::NameError,
                    Some(line),
                    format!("unknown record type '{name}' in theory {theory}"),
                )
                .with_help("declare it before use: record Name(field: Type, ...)"));
            }
        }
        Ok(())
    }

    /// Dispatch for v0.73 ordinary builtins. Returns `None` for other names.
    pub(super) fn infer_ordinary_call(
        &self,
        name: &str,
        args: &[Expr],
        theory: &str,
        line: usize,
    ) -> Option<Result<Passport, Diagnostic>> {
        if !ORDINARY_BUILTINS.contains(&name) {
            return None;
        }
        Some(self.infer_ordinary_call_inner(name, args, theory, line))
    }

    fn infer_ordinary_call_inner(&self, name: &str, args: &[Expr], theory: &str, line: usize) -> Result<Passport, Diagnostic> {
        let prelude = DiagnosticKind::StandardPreludeError;
        let partial = DiagnosticKind::PartialityTypeError;
        let seq = DiagnosticKind::SequenceTypeError;
        let structural = DiagnosticKind::StructuralTypeError;
        match name {
            "true" | "false" => {
                arity(name, args, 0, line)?;
                Ok(bool_literal(theory, name == "true"))
            }
            "not" => {
                arity(name, args, 1, line)?;
                let b = self.expect_type(&args[0], theory, name, "Bool", &prelude)?;
                Ok(typed_value(theory, "Bool", &[&b], "prelude:bool.not"))
            }
            "and" | "or" => {
                arity(name, args, 2, line)?;
                let a = self.expect_type(&args[0], theory, name, "Bool", &prelude)?;
                let b = self.expect_type(&args[1], theory, name, "Bool", &prelude)?;
                Ok(typed_value(theory, "Bool", &[&a, &b], format!("prelude:bool.{name}")))
            }
            "nat_eq" => {
                arity(name, args, 2, line)?;
                let a = self.expect_type(&args[0], theory, name, "Nat", &prelude)?;
                let b = self.expect_type(&args[1], theory, name, "Nat", &prelude)?;
                if !(a.capabilities.contains(Capability::CanCompareDirect) && b.capabilities.contains(Capability::CanCompareDirect)) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::AccessError,
                        Some(line),
                        "nat_eq requires can_compare_direct on both operands",
                    )
                    .with_help("compressed or symbolic Nat values are compared by proof (eq_proof), not by nat_eq"));
                }
                Ok(typed_value(theory, "Bool", &[&a, &b], "prelude:nat.eq"))
            }
            // v0.85: `mul` multiplies two Nat or two Int values.
            "mul" if self.ordinary_arg(&args.first().cloned().unwrap_or_else(|| Expr::int("0", line)), theory, name, &prelude)?.1 == "Int" => {
                arity(name, args, 2, line)?;
                let a = self.expect_type(&args[0], theory, name, "Int", &prelude)?;
                let b = self.expect_type(&args[1], theory, name, "Int", &prelude)?;
                Ok(typed_value(theory, "Int", &[&a, &b], "prelude:int.mul"))
            }
            "int" => {
                arity(name, args, 1, line)?;
                let n = self.expect_type(&args[0], theory, name, "Nat", &prelude)?;
                Ok(typed_value(theory, "Int", &[&n], "prelude:int.from_nat"))
            }
            "abs" => {
                arity(name, args, 1, line)?;
                let i = self.expect_type(&args[0], theory, name, "Int", &prelude)?;
                let mut result = typed_value(theory, "Nat", &[&i], "prelude:int.abs");
                result.capabilities = nat_caps(true);
                Ok(result)
            }
            "int_eq" => {
                arity(name, args, 2, line)?;
                let a = self.expect_type(&args[0], theory, name, "Int", &prelude)?;
                let b = self.expect_type(&args[1], theory, name, "Int", &prelude)?;
                Ok(typed_value(theory, "Bool", &[&a, &b], "prelude:int.eq"))
            }
            // v0.85: total division: `div(a, 0) = 0`, `mod(a, 0) = a`;
            // Euclidean for Int (`0 <= mod(a, b) < |b|`).
            "div" | "mod" => {
                arity(name, args, 2, line)?;
                let (a, ty) = self.ordinary_arg(&args[0], theory, name, &prelude)?;
                if ty != "Nat" && ty != "Int" {
                    return Err(Diagnostic::error(prelude.clone(), Some(line), format!("{name} expects `Nat` or `Int`, got `{ty}`")));
                }
                let b = self.expect_type(&args[1], theory, name, &ty, &prelude)?;
                let mut result = typed_value(theory, &ty, &[&a, &b], format!("prelude:{}.{name}", ty.to_ascii_lowercase()));
                if ty == "Nat" {
                    result.capabilities = nat_caps(true);
                }
                Ok(result)
            }
            "int_to_text" => {
                arity(name, args, 1, line)?;
                let i = self.expect_type(&args[0], theory, name, "Int", &prelude)?;
                Ok(typed_value(theory, "Text", &[&i], "prelude:int.to_text"))
            }
            "text_to_int" => {
                arity(name, args, 1, line)?;
                let t = self.expect_type(&args[0], theory, name, "Text", &prelude)?;
                Ok(typed_value(theory, "Option<Int>", &[&t], "prelude:int.from_text"))
            }
            "__sub" => {
                arity(name, args, 2, line)?;
                let (a, ty) = self.ordinary_arg(&args[0], theory, "-", &prelude)?;
                if ty != "Int" {
                    return Err(Diagnostic::error(prelude.clone(), Some(line), format!("`-` expects `Int` operands, got `{ty}`"))
                        .with_help("Nat has no subtraction: use monus(a, b) (truncated at 0) or convert with int(a) - int(b)"));
                }
                let b = self.expect_type(&args[1], theory, "-", "Int", &prelude)?;
                Ok(typed_value(theory, "Int", &[&a, &b], "prelude:int.sub"))
            }
            "__neg" => {
                arity(name, args, 1, line)?;
                // `-5` is an Int literal; otherwise the operand must be Int.
                if let ExprKind::IntLiteral(_) = &args[0].kind {
                    let mut literal = typed_value(theory, "Int", &[], "created:literal_int");
                    literal.construction = ConstructionMode::Literal;
                    return Ok(literal);
                }
                let i = self.expect_type(&args[0], theory, "unary -", "Int", &prelude)?;
                Ok(typed_value(theory, "Int", &[&i], "prelude:int.neg"))
            }
            "mul" | "monus" => {
                arity(name, args, 2, line)?;
                let a = self.expect_type(&args[0], theory, name, "Nat", &prelude)?;
                let b = self.expect_type(&args[1], theory, name, "Nat", &prelude)?;
                Ok(typed_value(theory, "Nat", &[&a, &b], format!("prelude:nat.{name}")))
            }
            "pred" => {
                arity(name, args, 1, line)?;
                let a = self.expect_type(&args[0], theory, name, "Nat", &prelude)?;
                Ok(typed_value(theory, "Nat", &[&a], "prelude:nat.pred"))
            }
            "if_then_else" => {
                arity(name, args, 3, line)?;
                let cond = self.expect_type(&args[0], theory, name, "Bool", &prelude)?;
                let (then_p, then_ty) = self.ordinary_arg(&args[1], theory, name, &prelude)?;
                let (else_p, else_ty) = self.ordinary_arg(&args[2], theory, name, &prelude)?;
                if then_ty != else_ty {
                    return Err(Diagnostic::error(
                        prelude.clone(),
                        Some(line),
                        format!("if_then_else branches differ: then is `{then_ty}`, else is `{else_ty}`"),
                    )
                    .with_help("both branches must have exactly the same ordinary type"));
                }
                Ok(typed_value(theory, &then_ty, &[&cond, &then_p, &else_p], "control:if_then_else"))
            }

            "some" => {
                arity(name, args, 1, line)?;
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &partial)?;
                let option = option_type(ty, &[&value], line)?;
                let report = option_some(&option, &value, line)?;
                Ok(option_value_passport(theory, &report, &[&value]))
            }
            "none" => {
                arity(name, args, 1, line)?;
                let item = self.type_arg(&args[0], theory, name, &partial)?;
                let option = option_type(item, &[], line)?;
                let report = option_none(&option, line)?;
                Ok(option_value_passport(theory, &report, &[]))
            }
            "ok" | "err" => {
                arity(name, args, 2, line)?;
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &partial)?;
                let other = self.type_arg(&args[1], theory, name, &partial)?;
                let report = if name == "ok" {
                    result_ok(&result_type(ty, other, &[&value], line)?, &value, line)?
                } else {
                    result_err(&result_type(other, ty, &[&value], line)?, &value, line)?
                };
                Ok(result_value_passport(theory, &report, &[&value]))
            }
            "is_some" | "is_none" => {
                arity(name, args, 1, line)?;
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &partial)?;
                if option_item(&ty).is_none() {
                    return Err(Diagnostic::error(partial.clone(), Some(line), format!("{name} expects Option<T>, got `{ty}`")));
                }
                Ok(typed_value(theory, "Bool", &[&value], format!("partiality:{name}")))
            }
            "is_ok" | "is_err" => {
                arity(name, args, 1, line)?;
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &partial)?;
                if result_components(&ty).is_none() {
                    return Err(Diagnostic::error(partial.clone(), Some(line), format!("{name} expects Result<T,E>, got `{ty}`")));
                }
                Ok(typed_value(theory, "Bool", &[&value], format!("partiality:{name}")))
            }
            "unwrap_or" => {
                arity(name, args, 2, line)?;
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &partial)?;
                let item = option_item(&ty)
                    .or_else(|| result_components(&ty).map(|(ok, _)| ok))
                    .ok_or_else(|| {
                        Diagnostic::error(partial.clone(), Some(line), format!("unwrap_or expects Option<T> or Result<T,E>, got `{ty}`"))
                    })?;
                let default = self.expect_type(&args[1], theory, name, &item, &partial)?;
                Ok(typed_value(theory, &item, &[&value, &default], "partiality:unwrap_or"))
            }

            "list" | "sequence" => {
                if args.is_empty() {
                    return Err(Diagnostic::error(
                        seq.clone(),
                        Some(line),
                        format!("{name}() needs at least one item; use empty_{name}(Type) for an empty collection"),
                    ));
                }
                let mut items = Vec::with_capacity(args.len());
                for arg in args {
                    items.push(self.ordinary_arg(arg, theory, name, &seq)?);
                }
                let item_type = items[0].1.clone();
                let refs: Vec<&Passport> = items.iter().map(|(p, _)| p).collect();
                if name == "list" {
                    let report = list_value(&list_type(item_type, &[], line)?, &refs, line)?;
                    Ok(list_value_passport(theory, &report, &refs))
                } else {
                    let report = sequence_value(&sequence_type(item_type, &[], line)?, &refs, line)?;
                    Ok(sequence_value_passport(theory, &report, &refs))
                }
            }
            "empty_list" | "empty_sequence" => {
                arity(name, args, 1, line)?;
                let item = self.type_arg(&args[0], theory, name, &seq)?;
                if name == "empty_list" {
                    let report = list_value(&list_type(item, &[], line)?, &[], line)?;
                    Ok(list_value_passport(theory, &report, &[]))
                } else {
                    let report = sequence_value(&sequence_type(item, &[], line)?, &[], line)?;
                    Ok(sequence_value_passport(theory, &report, &[]))
                }
            }
            "length" => {
                arity(name, args, 1, line)?;
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &seq)?;
                let (kind, _) = collection_item(&ty)
                    .ok_or_else(|| Diagnostic::error(seq.clone(), Some(line), format!("length expects List<T> or Sequence<T>, got `{ty}`")))?;
                let event = match &value.ty {
                    TypeKind::ListValue { len, .. } | TypeKind::SequenceValue { len, .. } => {
                        format!("prelude:{}.length:static={len}", kind.to_ascii_lowercase())
                    }
                    _ => format!("prelude:{}.length", kind.to_ascii_lowercase()),
                };
                let mut result = typed_value(theory, "Nat", &[&value], event);
                result.capabilities = nat_caps(true);
                Ok(result)
            }
            "index" => {
                arity(name, args, 2, line)?;
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &seq)?;
                let (kind, item) = collection_item(&ty)
                    .ok_or_else(|| Diagnostic::error(seq.clone(), Some(line), format!("index expects List<T> or Sequence<T>, got `{ty}`")))?;
                let position = self.expect_type(&args[1], theory, name, "Nat", &seq)?;
                Ok(typed_value(
                    theory,
                    &format!("Option<{item}>"),
                    &[&value, &position],
                    format!("prelude:{}.index", kind.to_ascii_lowercase()),
                ))
            }
            // v0.83: Text operations.
            "concat" => {
                arity(name, args, 2, line)?;
                let a = self.expect_type(&args[0], theory, name, "Text", &prelude)?;
                let b = self.expect_type(&args[1], theory, name, "Text", &prelude)?;
                Ok(typed_value(theory, "Text", &[&a, &b], "prelude:text.concat"))
            }
            "text_length" => {
                arity(name, args, 1, line)?;
                let t = self.expect_type(&args[0], theory, name, "Text", &prelude)?;
                let mut result = typed_value(theory, "Nat", &[&t], "prelude:text.length");
                result.capabilities = nat_caps(true);
                Ok(result)
            }
            // v0.111: structural equality of `Eq` types and order of `Ord` types (generic bounds).
            "equal" | "less" => {
                arity(name, args, 2, line)?;
                let (a, ty) = self.ordinary_arg(&args[0], theory, name, &prelude)?;
                let b = self.expect_type(&args[1], theory, name, &ty, &prelude)?;
                let bound = if name == "equal" { "Eq" } else { "Ord" };
                let parsed = parse_ordinary_type(&ty).map_err(|e| Diagnostic::error(prelude.clone(), Some(line), e))?;
                let fields = |n: &str| {
                    self.records.get(theory).and_then(|r| r.get(n)).map(|rep| rep.fields.iter().filter_map(|f| parse_ordinary_type(&f.ty).ok()).collect::<Vec<_>>())
                };
                if !crate::generics::satisfies(&parsed, bound, &fields, 0) {
                    return Err(Diagnostic::error(prelude.clone(), Some(line), format!("{name} needs a `{bound}` type, got `{ty}`"))
                        .with_help("Eq: no functions or resources inside; Ord: Nat, Int or Text"));
                }
                Ok(typed_value(theory, "Bool", &[&a, &b], format!("prelude:{name}")))
            }
            "text_eq" => {
                arity(name, args, 2, line)?;
                let a = self.expect_type(&args[0], theory, name, "Text", &prelude)?;
                let b = self.expect_type(&args[1], theory, name, "Text", &prelude)?;
                Ok(typed_value(theory, "Bool", &[&a, &b], "prelude:text.eq"))
            }
            "nat_to_text" => {
                arity(name, args, 1, line)?;
                let n = self.expect_type(&args[0], theory, name, "Nat", &prelude)?;
                Ok(typed_value(theory, "Text", &[&n], "prelude:text.from_nat"))
            }
            "text_to_nat" => {
                arity(name, args, 1, line)?;
                let t = self.expect_type(&args[0], theory, name, "Text", &prelude)?;
                Ok(typed_value(theory, "Option<Nat>", &[&t], "prelude:text.to_nat"))
            }
            "substring" => {
                arity(name, args, 3, line)?;
                let t = self.expect_type(&args[0], theory, name, "Text", &prelude)?;
                let start = self.expect_type(&args[1], theory, name, "Nat", &prelude)?;
                let len = self.expect_type(&args[2], theory, name, "Nat", &prelude)?;
                Ok(typed_value(theory, "Text", &[&t, &start, &len], "prelude:text.substring"))
            }
            "lines" | "words" => {
                arity(name, args, 1, line)?;
                let t = self.expect_type(&args[0], theory, name, "Text", &prelude)?;
                Ok(typed_value(theory, "List<Text>", &[&t], format!("prelude:text.{name}")))
            }
            "join" => {
                arity(name, args, 2, line)?;
                let parts = self.expect_type(&args[0], theory, name, "List<Text>", &seq)?;
                let sep = self.expect_type(&args[1], theory, name, "Text", &prelude)?;
                Ok(typed_value(theory, "Text", &[&parts, &sep], "prelude:text.join"))
            }
            // v0.102: owned byte buffers; ownership is checked separately (`crate::ownership`).
            "buf_new" => {
                arity(name, args, 2, line)?;
                let n = self.expect_type(&args[0], theory, name, "Nat", &prelude)?;
                let fill = self.expect_type(&args[1], theory, name, "Nat", &prelude)?;
                Ok(typed_value(theory, "ByteBuf", &[&n, &fill], "prelude:buf.new"))
            }
            "buf_from_list" => {
                arity(name, args, 1, line)?;
                let xs = self.expect_type(&args[0], theory, name, "List<Nat>", &seq)?;
                Ok(typed_value(theory, "ByteBuf", &[&xs], "prelude:buf.from_list"))
            }
            "buf_len" | "buf_to_list" | "buf_free" => {
                arity(name, args, 1, line)?;
                let b = self.expect_type(&args[0], theory, name, "ByteBuf", &prelude)?;
                let ty = if name == "buf_to_list" { "List<Nat>" } else { "Nat" };
                let mut result = typed_value(theory, ty, &[&b], format!("prelude:buf.{}", &name[4..]));
                if ty == "Nat" {
                    result.capabilities = nat_caps(true);
                }
                Ok(result)
            }
            "buf_get" | "buf_push" => {
                arity(name, args, 2, line)?;
                let b = self.expect_type(&args[0], theory, name, "ByteBuf", &prelude)?;
                let i = self.expect_type(&args[1], theory, name, "Nat", &prelude)?;
                let ty = if name == "buf_get" { "Option<Nat>" } else { "ByteBuf" };
                Ok(typed_value(theory, ty, &[&b, &i], format!("prelude:buf.{}", &name[4..])))
            }
            "buf_copy" | "buf_to_text" => {
                arity(name, args, 1, line)?;
                let b = self.expect_type(&args[0], theory, name, "ByteBuf", &prelude)?;
                let ty = if name == "buf_copy" { "ByteBuf" } else { "Option<Text>" };
                Ok(typed_value(theory, ty, &[&b], format!("prelude:buf.{}", &name[4..])))
            }
            "buf_slice" => {
                arity(name, args, 3, line)?;
                let b = self.expect_type(&args[0], theory, name, "ByteBuf", &prelude)?;
                let start = self.expect_type(&args[1], theory, name, "Nat", &prelude)?;
                let len = self.expect_type(&args[2], theory, name, "Nat", &prelude)?;
                Ok(typed_value(theory, "ByteBuf", &[&b, &start, &len], "prelude:buf.slice"))
            }
            "buf_append" | "buf_eq" => {
                arity(name, args, 2, line)?;
                let a = self.expect_type(&args[0], theory, name, "ByteBuf", &prelude)?;
                let b = self.expect_type(&args[1], theory, name, "ByteBuf", &prelude)?;
                let ty = if name == "buf_append" { "ByteBuf" } else { "Bool" };
                Ok(typed_value(theory, ty, &[&a, &b], format!("prelude:buf.{}", &name[4..])))
            }
            float if float.starts_with("fl_") || float.starts_with("fv_") || float.starts_with("nv_") || float.starts_with("gr_") || float.starts_with("la_") || float.starts_with("fft_") || float.starts_with("lp_") || float.starts_with("nt_") || float.starts_with("pf_") || float.starts_with("pm_") || float.starts_with("sat_") || float.starts_with("tp_") || float.starts_with("gb_") || float.starts_with("mv_") || float.starts_with("ilp_") || float.starts_with("km_") || float.starts_with("tm_") || float.starts_with("geo_") || float.starts_with("kd_") || float.starts_with("fft2_") || float.starts_with("sig_") || float.starts_with("bvh_") || float.starts_with("gx_") || float.starts_with("vx_") => {
                let (_, params, ret) = FLOAT_BUILTINS.iter().chain(GRAPH_BUILTINS.iter()).chain(LINALG_BUILTINS.iter()).find(|(n, _, _)| *n == float).expect("float builtin");
                arity(name, args, params.len(), line)?;
                let mut inputs = Vec::new();
                for (arg, ty) in args.iter().zip(params.iter()) {
                    let kind = if ty.starts_with("List<") { &seq } else { &prelude };
                    inputs.push(self.expect_type(arg, theory, name, ty, kind)?);
                }
                let refs: Vec<&Passport> = inputs.iter().collect();
                let mut result = typed_value(theory, ret, &refs, format!("prelude:{}.{}", if name.starts_with("gb_") || name.starts_with("mv_") { "groebner" } else if name.starts_with("sat_") || name.starts_with("tp_") { "combinatorics" } else if name.starts_with("ilp_") { "linalg" } else if name.starts_with("km_") || name.starts_with("tm_") { "maps" } else if name.starts_with("geo_") || name.starts_with("kd_") || name.starts_with("bvh_") { "geometry" } else if name.starts_with("gx_") || name.starts_with("vx_") { "gfx" } else if name.starts_with("fft2_") || name.starts_with("sig_") { "signal" } else if name.starts_with("nt_") || name.starts_with("pf_") || name.starts_with("pm_") { "number" } else if name.starts_with("la_") || name.starts_with("fft_") || name.starts_with("lp_") { "linalg" } else if name.starts_with("nv_") || name.starts_with("gr_") { "graph" } else { "float" }, &name[3..]));
                if *ret == "Nat" {
                    result.capabilities = nat_caps(true);
                }
                Ok(result)
            }
            file if file.starts_with("file_") || file.starts_with("proc_") || file.starts_with("net_") || file.starts_with("win_") => {
                let (_, params, ret) = FILE_BUILTINS.iter().find(|(n, _, _)| *n == file).expect("file builtin");
                arity(name, args, params.len(), line)?;
                let mut inputs = Vec::new();
                for (arg, ty) in args.iter().zip(params.iter()) {
                    inputs.push(self.expect_type(arg, theory, name, ty, &prelude)?);
                }
                let refs: Vec<&Passport> = inputs.iter().collect();
                Ok(typed_value(theory, ret, &refs, format!("prelude:file.{}", &name[5..])))
            }
            dense if dense.starts_with("bn_") => {
                let (_, params, ret) = DENSE_NAT_BUILTINS.iter().find(|(n, _, _)| *n == dense).expect("dense builtin");
                arity(name, args, params.len(), line)?;
                let mut inputs = Vec::new();
                for (arg, ty) in args.iter().zip(params.iter()) {
                    let kind = if ty.starts_with("List<") { &seq } else { &prelude };
                    inputs.push(self.expect_type(arg, theory, name, ty, kind)?);
                }
                let refs: Vec<&Passport> = inputs.iter().collect();
                let mut result = typed_value(theory, ret, &refs, format!("prelude:bignat.{}", &name[3..]));
                if *ret == "Nat" {
                    result.capabilities = nat_caps(true);
                }
                Ok(result)
            }
            "tbuf_new" => {
                arity(name, args, 0, line)?;
                Ok(typed_value(theory, "TextBuf", &[], "prelude:tbuf.new"))
            }
            "tbuf_push" => {
                arity(name, args, 2, line)?;
                let t = self.expect_type(&args[0], theory, name, "TextBuf", &prelude)?;
                let s = self.expect_type(&args[1], theory, name, "Text", &prelude)?;
                Ok(typed_value(theory, "TextBuf", &[&t, &s], "prelude:tbuf.push"))
            }
            "tbuf_len" | "tbuf_finish" => {
                arity(name, args, 1, line)?;
                let t = self.expect_type(&args[0], theory, name, "TextBuf", &prelude)?;
                let mut result = typed_value(theory, if name == "tbuf_len" { "Nat" } else { "Text" }, &[&t], format!("prelude:tbuf.{}", &name[5..]));
                if name == "tbuf_len" {
                    result.capabilities = nat_caps(true);
                }
                Ok(result)
            }
            "buf_from_text" => {
                arity(name, args, 1, line)?;
                let t = self.expect_type(&args[0], theory, name, "Text", &prelude)?;
                Ok(typed_value(theory, "ByteBuf", &[&t], "prelude:buf.from_text"))
            }
            "buf_set" => {
                arity(name, args, 3, line)?;
                let b = self.expect_type(&args[0], theory, name, "ByteBuf", &prelude)?;
                let i = self.expect_type(&args[1], theory, name, "Nat", &prelude)?;
                let v = self.expect_type(&args[2], theory, name, "Nat", &prelude)?;
                Ok(typed_value(theory, "ByteBuf", &[&b, &i, &v], "prelude:buf.set"))
            }
            // v0.82: produced only by `match` desugaring, inside the branch
            // where the scrutinee is known to be non-empty / `some`; source
            // code cannot name them (identifiers starting with `__` are
            // reserved by the parser).
            "__match_head" => {
                arity(name, args, 1, line)?;
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &seq)?;
                let (kind, item) = collection_item(&ty)
                    .ok_or_else(|| Diagnostic::error(seq.clone(), Some(line), format!("a list pattern needs List<T> or Sequence<T>, got `{ty}`")))?;
                Ok(typed_value(theory, &item, &[&value], format!("match:{}.head", kind.to_ascii_lowercase())))
            }
            "__match_some" => {
                arity(name, args, 1, line)?;
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &seq)?;
                let item = option_item(&ty)
                    .ok_or_else(|| Diagnostic::error(seq.clone(), Some(line), format!("an option pattern needs Option<T>, got `{ty}`")))?;
                Ok(typed_value(theory, &item, &[&value], "match:option.some"))
            }
            // v0.125: payload of an enum variant; the match must name every variant
            "__match_variant" | "__enum_payload" => {
                arity(name, args, 2, line)?;
                let ExprKind::TextLiteral(covered) = &args[1].kind else {
                    return Err(Diagnostic::error(seq.clone(), Some(line), "__match_variant is generated by enum patterns"));
                };
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &seq)?;
                let item = option_item(&ty)
                    .ok_or_else(|| Diagnostic::error(seq.clone(), Some(line), format!("an enum pattern needs a variant field, got `{ty}`")))?;
                let ExprKind::Call { name: field_call, args: field_args } = &args[0].kind else {
                    return Err(Diagnostic::error(seq.clone(), Some(line), "__match_variant is generated by enum patterns"));
                };
                let subject_ty = if field_call == "field" && field_args.len() == 2 { self.ordinary_arg(&field_args[0], theory, name, &seq)?.1 } else { String::new() };
                let record = record_type_name(&subject_ty)
                    .ok_or_else(|| Diagnostic::error(seq.clone(), Some(line), format!("variant patterns need an enum value, got `{subject_ty}`")))?;
                let variants = self.records.get(theory).and_then(|rs| rs.get(&record)).map(|r| r.enum_variants.as_slice()).unwrap_or_default();
                if variants.is_empty() {
                    return Err(Diagnostic::error(seq.clone(), Some(line), format!("variant patterns need an enum value, got `{record}`")));
                }
                let mut named = Vec::new();
                for pattern in covered.split(',').filter(|p| *p != "*") {
                    let (v, arity) = pattern.rsplit_once(':').ok_or_else(|| Diagnostic::error(seq.clone(), Some(line), "invalid enum pattern metadata"))?;
                    let Some((_, expected)) = variants.iter().find(|(w, _)| w == v) else {
                        return Err(Diagnostic::error(seq.clone(), Some(line), format!("enum `{record}` has no variant `{v}`")));
                    };
                    if arity.parse::<usize>().ok() != Some(*expected) {
                        return Err(Diagnostic::error(seq.clone(), Some(line), format!("variant `{v}` pattern expects {expected} field(s), got {arity}")));
                    }
                    named.push(v);
                }
                if !covered.split(',').any(|p| p == "*") {
                    let missing: Vec<_> = variants.iter().filter(|(v, _)| !named.contains(&v.as_str())).map(|(v, _)| v).collect();
                    if !missing.is_empty() {
                        return Err(Diagnostic::error(seq.clone(), Some(line), format!(
                            "match on enum `{record}` is not exhaustive: no arm for {}",
                            missing.iter().map(|v| format!("`{v}`")).collect::<Vec<_>>().join(", ")
                        )).with_help("add the missing variants or a catch-all arm `_ => ...`"));
                    }
                }
                Ok(typed_value(theory, &item, &[&value], "match:enum.variant"))
            }
            "__match_ok" | "__match_err" => {
                arity(name, args, 1, line)?;
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &seq)?;
                let (good, error) = result_components(&ty)
                    .ok_or_else(|| Diagnostic::error(seq.clone(), Some(line), format!("a result pattern needs Result<T, E>, got `{ty}`")))?;
                Ok(typed_value(theory, if name == "__match_ok" { &good } else { &error }, &[&value], format!("match:result.{name}")))
            }
            "head" => {
                arity(name, args, 1, line)?;
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &seq)?;
                let (kind, item) = collection_item(&ty)
                    .ok_or_else(|| Diagnostic::error(seq.clone(), Some(line), format!("head expects List<T> or Sequence<T>, got `{ty}`")))?;
                Ok(typed_value(
                    theory,
                    &format!("Option<{item}>"),
                    &[&value],
                    format!("prelude:{}.head", kind.to_ascii_lowercase()),
                ))
            }

            "tail" => {
                arity(name, args, 1, line)?;
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &seq)?;
                let (kind, _) = collection_item(&ty)
                    .ok_or_else(|| Diagnostic::error(seq.clone(), Some(line), format!("tail expects List<T> or Sequence<T>, got `{ty}`")))?;
                Ok(typed_value(theory, &ty, &[&value], format!("prelude:{}.tail", kind.to_ascii_lowercase())))
            }
            "cons" => {
                arity(name, args, 2, line)?;
                let (collection, ty) = self.ordinary_arg(&args[1], theory, name, &seq)?;
                let (kind, item) = collection_item(&ty)
                    .ok_or_else(|| Diagnostic::error(seq.clone(), Some(line), format!("cons expects List<T> or Sequence<T> as second argument, got `{ty}`")))?;
                let head = self.expect_type(&args[0], theory, name, &item, &seq)?;
                Ok(typed_value(theory, &ty, &[&head, &collection], format!("prelude:{}.cons", kind.to_ascii_lowercase())))
            }
            "is_empty" => {
                arity(name, args, 1, line)?;
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &seq)?;
                let (kind, _) = collection_item(&ty)
                    .ok_or_else(|| Diagnostic::error(seq.clone(), Some(line), format!("is_empty expects List<T> or Sequence<T>, got `{ty}`")))?;
                Ok(typed_value(theory, "Bool", &[&value], format!("prelude:{}.is_empty", kind.to_ascii_lowercase())))
            }

            "pair" => {
                arity(name, args, 2, line)?;
                let (lhs, lhs_ty) = self.ordinary_arg(&args[0], theory, name, &structural)?;
                let (rhs, rhs_ty) = self.ordinary_arg(&args[1], theory, name, &structural)?;
                let report = product_term(&product_type(lhs_ty, rhs_ty, &[], line)?, &lhs, &rhs, line)?;
                Ok(product_term_passport(theory, &report, &[&lhs, &rhs]))
            }
            "fst" | "snd" => {
                arity(name, args, 1, line)?;
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &structural)?;
                let (lhs, rhs) = product_components(&ty)
                    .ok_or_else(|| Diagnostic::error(structural.clone(), Some(line), format!("{name} expects ProductType<A*B>, got `{ty}`")))?;
                let component = if name == "fst" { lhs } else { rhs };
                Ok(typed_value(theory, &component, &[&value], format!("structural:product.{name}")))
            }

            "inl" | "inr" => {
                arity(name, args, 2, line)?;
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &structural)?;
                let other = self.type_arg(&args[1], theory, name, &structural)?;
                let (sum, side) = if name == "inl" {
                    (sum_type(ty, other, &[], line)?, SumInjectionSide::Left)
                } else {
                    (sum_type(other, ty, &[], line)?, SumInjectionSide::Right)
                };
                let report = sum_injection(&sum, side, &value, line)?;
                Ok(sum_injection_passport(theory, &report, &[&value]))
            }
            "is_left" => {
                arity(name, args, 1, line)?;
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &structural)?;
                if sum_components(&ty).is_none() {
                    return Err(Diagnostic::error(structural.clone(), Some(line), format!("is_left expects SumType<A+B>, got `{ty}`")));
                }
                Ok(typed_value(theory, "Bool", &[&value], "structural:sum.is_left"))
            }
            "case" => {
                arity(name, args, 3, line)?;
                let elim = DiagnosticKind::StructuralEliminationError;
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &elim)?;
                let (left, right) = sum_components(&ty)
                    .ok_or_else(|| Diagnostic::error(elim.clone(), Some(line), format!("case expects SumType<A+B>, got `{ty}`")))?;
                let on_left = self.function_arg(&args[1], theory, name, &elim, line)?;
                let on_right = self.function_arg(&args[2], theory, name, &elim, line)?;
                for (function, expected) in [(&on_left, &left), (&on_right, &right)] {
                    if function.params.len() != 1 || &function.params[0] != expected {
                        return Err(Diagnostic::error(
                            elim.clone(),
                            Some(line),
                            format!("case branch `{}` must take exactly one `{expected}` parameter", function.label),
                        ));
                    }
                }
                if on_left.ret != on_right.ret {
                    return Err(Diagnostic::error(
                        elim.clone(),
                        Some(line),
                        format!(
                            "case branches return different types: `{}` returns `{}`, `{}` returns `{}`",
                            on_left.label, on_left.ret, on_right.label, on_right.ret
                        ),
                    )
                    .with_help("both branches of sum elimination must produce the same result type"));
                }
                let mut result = typed_value(
                    theory,
                    &on_left.ret,
                    &[&value],
                    format!("structural:sum.case:{}|{}", on_left.label, on_right.label),
                );
                for branch in [&on_left, &on_right] {
                    result.trust = result.trust.max(branch.trust);
                    result.provenance = result.provenance.max(branch.provenance);
                }
                Ok(result)
            }
            "field" => {
                arity(name, args, 2, line)?;
                let (value, ty) = self.ordinary_arg(&args[0], theory, name, &structural)?;
                let record = record_type_name(&ty)
                    .ok_or_else(|| Diagnostic::error(structural.clone(), Some(line), format!("field expects a record value, got `{ty}`")))?;
                let ExprKind::Ident(field_name) = &args[1].kind else {
                    return Err(Diagnostic::error(structural.clone(), Some(line), "field expects a field name as its second argument"));
                };
                let declared = self
                    .records
                    .get(theory)
                    .and_then(|records| records.get(&record))
                    .and_then(|report| report.fields.iter().find(|f| &f.name == field_name))
                    .ok_or_else(|| {
                        Diagnostic::error(structural.clone(), Some(line), format!("record `{record}` has no field `{field_name}`"))
                    })?;
                Ok(typed_value(theory, &declared.ty, &[&value], format!("structural:record.{record}.{field_name}")))
            }

            "effect_fn" => self.infer_effect_function(args, theory, line),
            "each_effect" | "filter_effect" | "find_effect" | "any_effect" | "all_effect"
            | "try_map_effect" | "try_each_effect" => self.infer_effect_traversal(name, args, theory, line),
            "map" | "map_effect" => {
                arity(name, args, 2, line)?;
                let (collection, ty) = self.ordinary_arg(&args[0], theory, name, &DiagnosticKind::TraversalError)?;
                let (kind, item) = collection_item(&ty).ok_or_else(|| {
                    Diagnostic::error(DiagnosticKind::TraversalError, Some(line), format!("map expects List<T> or Sequence<T>, got `{ty}`"))
                })?;
                let function = if name == "map" { self.traversal_function(&args[1], theory, name, line)? }
                    else { self.function_arg(&args[1], theory, name, &DiagnosticKind::TraversalError, line)? };
                if function.params.len() != 1 || function.params[0] != item {
                    return Err(Diagnostic::error(
                        DiagnosticKind::TraversalError,
                        Some(line),
                        format!("map function `{}` must take exactly one `{item}` parameter", function.label),
                    )
                    .with_help(format!("function contract domain is `{}`", function.contract.domain)));
                }
                let output = function.ret.clone();
                if name == "map" { self.traversal_result(theory, &collection, kind, &output, &function, None, line) }
                else { Ok(self.effect_traversal_value(theory, &format!("{kind}<{output}>"), &[&collection], &function, name)) }
            }
            "fold" | "fold_effect" => {
                arity(name, args, 3, line)?;
                let (collection, ty) = self.ordinary_arg(&args[0], theory, name, &DiagnosticKind::TraversalError)?;
                let (kind, item) = collection_item(&ty).ok_or_else(|| {
                    Diagnostic::error(DiagnosticKind::TraversalError, Some(line), format!("fold expects List<T> or Sequence<T>, got `{ty}`"))
                })?;
                let (initial, acc) = self.ordinary_arg(&args[1], theory, name, &DiagnosticKind::TraversalError)?;
                let function = if name == "fold" { self.traversal_function(&args[2], theory, name, line)? }
                    else { self.function_arg(&args[2], theory, name, &DiagnosticKind::TraversalError, line)? };
                let params: Vec<&str> = function.params.iter().map(String::as_str).collect();
                if params != [acc.as_str(), item.as_str()] || function.ret != acc {
                    return Err(Diagnostic::error(
                        DiagnosticKind::TraversalError,
                        Some(line),
                        format!("fold step `{}` must have signature ({acc}, {item}) -> {acc}", function.label),
                    )
                    .with_help(format!("declared: ({}) -> {}", params.join(", "), function.ret)));
                }
                if name == "fold" { self.traversal_result(theory, &collection, kind, &acc, &function, Some(&initial), line) }
                else { Ok(self.effect_traversal_value(theory, &acc, &[&collection, &initial], &function, name)) }
            }
            other => Err(Diagnostic::error(
                DiagnosticKind::NameError,
                Some(line),
                format!("unknown ordinary builtin '{other}'"),
            )),
        }
    }

    /// v0.75: resolve a function argument. A bare identifier naming a declared
    /// fn (not shadowed by a value) keeps that fn's own contract. Any other
    /// expression must have type `Fn<...>`; function values are pure and total
    /// by construction, so their contract is synthesized as pure/total with a
    /// `function_value_invariant` evidence and the value's taint.
    pub(super) fn function_arg(
        &self,
        expr: &Expr,
        theory: &str,
        op: &str,
        kind: &DiagnosticKind,
        line: usize,
    ) -> Result<FnArg, Diagnostic> {
        if let ExprKind::Ident(name) = &expr.kind {
            if self.callable_value(name, theory).is_none() {
                if let Some(function) = self.functions.get(theory).and_then(|fns| fns.get(name)) {
                    let params: Vec<String> = function.decl.params.iter().map(|p| p.ty.clone()).collect();
                    let fail = |message| Diagnostic::error(kind.clone(), Some(line), message);
                    let ty = if function.contract.effects.is_empty() && function.contract.totality == FunctionTotality::Total && !params.is_empty() {
                        parse_ordinary_type(&fn_type_text(&params, &function.decl.ret)).map_err(fail)?
                    } else {
                        OrdinaryType::EffectFn(params.iter().map(|p|parse_ordinary_type(p)).collect::<Result<Vec<_>,_>>().map_err(fail)?,
                            Box::new(parse_ordinary_type(&function.decl.ret).map_err(fail)?),
                            crate::callback::EffectBounds::from_effects(&function.contract.effects, function.contract.totality != FunctionTotality::Total).map_err(fail)?)
                    };
                    crate::ownership::check_declared_type(&ty.canonical(), "function argument", line, false)?;
                    self.typed.borrow_mut().record(expr.id, ty, expr.span, theory);
                    return Ok(FnArg {
                        label: name.clone(),
                        params: function.decl.params.iter().map(|p| p.ty.clone()).collect(),
                        ret: function.decl.ret.clone(),
                        contract: function.contract.clone(),
                        history: function.contract_passport.history.clone(),
                        trust: function.body.trust,
                        provenance: function.body.provenance,
                    });
                }
            }
        }
        let value = self.infer_expr(expr, theory)?;
        let ty = surface_type_of(&value).unwrap_or_else(|| value.ty.to_string());
        let (params, ret) = fn_signature(&ty).ok_or_else(|| {
            Diagnostic::error(kind.clone(), Some(line), format!("{op} expects a function, got `{ty}`"))
                .with_help("pass a declared fn name, a lambda fun(x: T, body) or a value of type Fn<...>")
        })?;
        let label = match &expr.kind {
            ExprKind::Ident(name) => name.clone(),
            ExprKind::Lambda { .. } => "lambda".to_string(),
            _ => "fn_value".to_string(),
        };
        let (effects, partial) = match parse_ordinary_type(&ty) {
            Ok(OrdinaryType::EffectFn(_,_,bounds)) => (bounds.effects(), bounds.partial()),
            _ => (Vec::new(), false),
        };
        let fn_report = function_type(domain_of_params(&params), ret.clone(), !partial, effects.is_empty(), &[&value], line)?;
        let fn_type = function_type_passport(theory, &fn_report, &[&value]);
        let evidence = static_evidence(
            theory,
            format!("function_value_invariant({label})"),
            format!("totality:function_value:{label}"),
        );
        let evidence_refs = [&evidence];
        let contract = function_contract(
            label.clone(),
            &fn_type,
            if effects.is_empty() { FunctionPurity::Pure } else { FunctionPurity::Effectful },
            if partial { FunctionTotality::Partial } else { FunctionTotality::Total },
            effects,
            if partial { &[] } else { &evidence_refs },
            line,
        )?;
        Ok(FnArg { label, params, ret, contract, history: value.history.clone(), trust: value.trust, provenance: value.provenance })
    }

    fn traversal_function(&self, expr: &Expr, theory: &str, op: &str, line: usize) -> Result<FnArg, Diagnostic> {
        let function = self.function_arg(expr, theory, op, &DiagnosticKind::TraversalError, line)?;
        require_verified_function_contract(&function.contract, line).map_err(|diag| {
            diag.with_help(format!(
                "{op} only accepts pure, total, untainted functions; contract obligations: {}",
                function.contract.open_obligations.join("; ")
            ))
        })?;
        Ok(function)
    }

    #[allow(clippy::too_many_arguments)]
    fn traversal_result(
        &self,
        theory: &str,
        collection: &Passport,
        kind: &str,
        output: &str,
        function: &FnArg,
        initial: Option<&Passport>,
        line: usize,
    ) -> Result<Passport, Diagnostic> {
        let static_len = match &collection.ty {
            TypeKind::ListValue { len, .. } | TypeKind::SequenceValue { len, .. } => Some(*len),
            _ => None,
        };
        let name = &function.label;
        match (initial, static_len) {
            // map over a statically shaped collection: the core traversal report
            // must be verified_bounded, and the result keeps its static length.
            (None, Some(len)) => {
                let report = map_collection_passport(collection, &function.contract, output, len, line)?;
                require_verified_bounded_map(&report, line)?;
                let ty = if kind == "List" {
                    TypeKind::ListValue { item: output.to_string(), len }
                } else {
                    TypeKind::SequenceValue { item: output.to_string(), len }
                };
                Ok(Passport {
                    ty,
                    construction: ConstructionMode::Definable,
                    capabilities: collection.capabilities.clone(),
                    cost: collection.cost,
                    trust: report.max_trust,
                    provenance: report.max_provenance,
                    validation: ValidationState::StaticChecked,
                    theory: TheoryContext::new(theory),
                    history: HistoryChain::merge2(
                        &collection.history,
                        &function.history,
                        format!("traversal:map:{name}:{}:fingerprint={}", report.status, report.fingerprint),
                    ),
                    location: LocationContext::local(),
                })
            }
            (None, None) => Ok(typed_value(
                theory,
                &format!("{kind}<{output}>"),
                &[collection],
                format!("traversal:map:{name}:runtime_bounded_by_length"),
            )),
            (Some(initial), Some(len)) => {
                let report = fold_collection_passport(collection, initial, output, &function.contract, len, line)?;
                require_verified_bounded_fold(&report, line)?;
                let mut result = typed_value(
                    theory,
                    output,
                    &[collection, initial],
                    format!("traversal:fold:{name}:{}:fingerprint={}", report.status, report.fingerprint),
                );
                result.trust = result.trust.max(report.max_trust);
                result.provenance = result.provenance.max(report.max_provenance);
                Ok(result)
            }
            (Some(initial), None) => Ok(typed_value(
                theory,
                output,
                &[collection, initial],
                format!("traversal:fold:{name}:runtime_bounded_by_length"),
            )),
        }
    }

    /// `true` when `name` is already a builtin (old or ordinary).
    pub(super) fn is_builtin_name(&self, name: &str, theory: &str, line: usize) -> bool {
        if ORDINARY_BUILTINS.contains(&name) {
            return true;
        }
        let user_defined = self.callable_value(name, theory).is_some()
            || self.functions.get(theory).is_some_and(|fns| fns.contains_key(name))
            || self.records.get(theory).is_some_and(|records| records.contains_key(name))
            || self.pending_group.borrow().iter().any(|member| member.name == name);
        if user_defined {
            return false;
        }
        match self.infer_call(name, &[], theory, line) {
            Err(diag) => !(diag.kind == DiagnosticKind::NameError && diag.message.starts_with("unknown builtin or function")),
            Ok(_) => true,
        }
    }

    /// Check a `fn` declaration and register its contract.
    pub(super) fn check_fn_decl(&mut self, theory: &str, decl: &FnDecl) -> Result<Passport, Diagnostic> {
        self.check_fn_decl_in(theory, decl, &[])
    }

    /// v0.81: check a `mutual { ... }` group. Members are checked in order;
    /// calls to members not yet checked are typed by their signatures, and
    /// every member must be pure and total (the others were assumed so).
    pub(super) fn check_mutual_group(&mut self, theory: &str, group: &[FnDecl]) -> Result<Vec<Passport>, Diagnostic> {
        for (index, decl) in group.iter().enumerate() {
            if group[..index].iter().any(|other| other.name == decl.name) {
                return Err(Diagnostic::error(
                    DiagnosticKind::NameError,
                    Some(decl.line),
                    format!("fn '{}' is declared twice in a mutual block", decl.name),
                ));
            }
            if self.is_builtin_name(&decl.name, theory, decl.line) {
                return Err(Diagnostic::error(
                    DiagnosticKind::NameError,
                    Some(decl.line),
                    format!("fn '{}' shadows a builtin", decl.name),
                )
                .with_help("user functions may not redefine builtins; choose another name"));
            }
            if self.functions.get(theory).is_some_and(|fns| fns.contains_key(&decl.name)) {
                return Err(Diagnostic::error(
                    DiagnosticKind::NameError,
                    Some(decl.line),
                    format!("fn '{}' is already declared", decl.name),
                ));
            }
        }
        *self.pending_group.borrow_mut() = group.to_vec();
        let mut passports = Vec::with_capacity(group.len());
        let mut failure = None;
        for decl in group {
            match self.check_fn_decl_in(theory, decl, group) {
                Ok(passport) => passports.push(passport),
                Err(diag) => {
                    failure = Some(diag);
                    break;
                }
            }
        }
        self.pending_group.borrow_mut().clear();
        if let Some(diag) = failure {
            // Do not keep a partially checked group.
            if let Some(fns) = self.functions.get_mut(theory) {
                for decl in group {
                    fns.remove(&decl.name);
                }
            }
            return Err(diag);
        }
        for decl in group {
            let function = &self.functions[theory][&decl.name];
            if function.contract.purity != FunctionPurity::Pure || function.contract.totality != FunctionTotality::Total {
                let names: Vec<&str> = group.iter().map(|d| d.name.as_str()).collect();
                if let Some(fns) = self.functions.get_mut(theory) {
                    for member in group {
                        fns.remove(&member.name);
                    }
                }
                return Err(Diagnostic::error(
                    DiagnosticKind::RecursionBoundaryError,
                    Some(decl.line),
                    format!("mutual block [{}] must be pure and total; '{}' is not", names.join(", "), decl.name),
                ));
            }
        }
        Ok(passports)
    }

    fn check_fn_decl_in(&mut self, theory: &str, decl: &FnDecl, group: &[FnDecl]) -> Result<Passport, Diagnostic> {
        let line = decl.line;
        if self.is_builtin_name(&decl.name, theory, line) {
            return Err(Diagnostic::error(
                DiagnosticKind::NameError,
                Some(line),
                format!("fn '{}' shadows a builtin", decl.name),
            )
            .with_help("user functions may not redefine builtins; choose another name"));
        }

        for ty in decl.params.iter().map(|p| &p.ty).chain(std::iter::once(&decl.ret)) {
            self.require_known_records(ty, theory, line)?;
        }
        for param in &decl.params {
            if param.name == decl.name {
                return Err(Diagnostic::error(
                    DiagnosticKind::NameError,
                    Some(line),
                    format!("fn '{}' has a parameter with its own name", decl.name),
                ));
            }
            if fn_signature(&param.ty).is_some() {
                self.check_callable_name(&param.name, theory, line)?;
            }
        }

        let mut calls = Vec::new();
        called_names(&decl.body, &mut calls);
        let looping = decl.measure.as_ref().is_some_and(|m| m.is_loop());
        let measure_kind = if looping {
            if !group.is_empty() {
                return Err(recursion_error(line, format!("fn '{}' is declared `loop`; loop functions cannot be in a mutual block", decl.name)));
            }
            check_tail_loop(decl)?;
            None
        } else if !group.is_empty() {
            Some(check_well_founded_in(decl, group)?)
        } else if calls.iter().any(|call| call == &decl.name) {
            Some(check_well_founded(decl)?)
        } else {
            None
        };
        // Members of the group not checked yet are assumed pure and total
        // here; `check_mutual_group` verifies that afterwards.
        calls.retain(|call| call == &decl.name || !group.iter().any(|member| &member.name == call) || self.functions.get(theory).is_some_and(|fns| fns.contains_key(call)));

        let frame: BTreeMap<String, Passport> = decl
            .params
            .iter()
            .map(|param| {
                (
                    param.name.clone(),
                    typed_value(theory, &param.ty, &[], format!("fn:{}:param:{}:{}", decl.name, param.name, param.ty)),
                )
            })
            .collect();
        if measure_kind.is_some() || looping {
            *self.pending_fn.borrow_mut() = Some(decl.clone());
        }
        self.locals.borrow_mut().push(frame);
        let body = self.infer_expr(&decl.body, theory);
        self.locals.borrow_mut().pop();
        *self.pending_fn.borrow_mut() = None;
        let body = body?;
        // v0.102: ownership of ByteBuf owners over every control path.
        let release = {
            let shape = |name: &str| {
                if name == decl.name {
                    return Some(crate::ownership::FnShape::of(decl));
                }
                group.iter().find(|m| m.name == name).map(crate::ownership::FnShape::of).or_else(|| self.buffer_shape(theory, name))
            };
            crate::ownership::check_function(decl, &shape)?
        };
        {
            let mut typed = self.typed.borrow_mut();
            typed.released_lets.extend(release.lets.iter().copied());
            typed.released_branches.extend(release.branches.clone());
        }

        let body_ty = surface_type_of(&body).ok_or_else(|| {
            Diagnostic::error(
                DiagnosticKind::FunctionTermError,
                Some(line),
                format!("fn '{}' body is not an ordinary value: {}", decl.name, body.ty),
            )
            .with_help("function bodies must produce Nat, Bool, Text, Option, Result, List, Sequence or Product values")
        })?;
        if body_ty != decl.ret {
            return Err(Diagnostic::error(
                DiagnosticKind::FunctionTermError,
                Some(line),
                format!("fn '{}' declares -> {} but its body has type {body_ty}", decl.name, decl.ret),
            ));
        }

        let (effects, partial) = self.effects_in(&decl.body, &decl.params, Some(&decl.name), theory, line)?;
        // v0.125: a tail loop may run forever, so it is never total.
        let partial = partial || looping;
        let purity = if effects.is_empty() { FunctionPurity::Pure } else { FunctionPurity::Effectful };
        let totality = if partial { FunctionTotality::Partial } else { FunctionTotality::Total };

        let param_types: Vec<String> = decl.params.iter().map(|p| p.ty.clone()).collect();
        let fn_report = function_type(
            domain_of_params(&param_types),
            decl.ret.clone(),
            totality == FunctionTotality::Total,
            purity == FunctionPurity::Pure,
            &[&body],
            line,
        )?;
        let fn_type = function_type_passport(theory, &fn_report, &[&body]);

        // Totality evidence is derived from decidable structural facts that the
        // checker has just established:
        //  - non-recursive: no self reference, only previously declared (hence
        //    non-mutually-recursive) functions, no partial builtin;
        //  - recursive: every recursive call passes a strictly smaller measure
        //    (pred/monus/tail) under a positivity/non-emptiness guard.
        let (predicate, event) = match (&measure_kind, &decl.measure) {
            (Some(kind), Some(measure)) => (
                format!("well_founded({}:{}:{kind})", decl.name, measure.params.join(",")),
                format!("totality:well_founded:{kind}:{}:{}", decl.name, measure.params.join(",")),
            ),
            _ => (
                format!("total_structural({})", decl.name),
                format!("totality:structural:non_recursive:{}", decl.name),
            ),
        };
        let structural_evidence = static_evidence(theory, predicate, event);
        let evidence: Vec<&Passport> = if totality == FunctionTotality::Total {
            vec![&structural_evidence]
        } else {
            Vec::new()
        };
        let contract = function_contract(decl.name.clone(), &fn_type, purity, totality, effects, &evidence, line)?;
        let contract_passport = function_contract_passport(theory, &contract, &[&fn_type]);

        let recursion = match (measure_kind, &decl.measure) {
            (Some(kind), Some(measure)) => {
                let fuel = measure.fuel.unwrap_or(DEFAULT_RECURSION_FUEL);
                if fuel > MAX_RECURSION_FUEL {
                    return Err(Diagnostic::error(
                        DiagnosticKind::RecursionBoundaryError,
                        Some(line),
                        format!("fn '{}' declares fuel {fuel}, above the runtime maximum {MAX_RECURSION_FUEL}", decl.name),
                    )
                    .with_help("fuel is the maximum recursion depth the runtime will execute; restructure as tail-accumulating recursion over smaller inputs"));
                }
                let fuel = usize::try_from(fuel).unwrap_or(usize::MAX);
                let scheme = recursion_scheme(decl.name.clone(), &contract, kind, fuel, &[&structural_evidence], line)?;
                if matches!(
                    scheme.status,
                    RecursionStatus::RejectedContract | RecursionStatus::RejectedMeasure | RecursionStatus::RejectedFuelExceeded
                ) {
                    return Err(Diagnostic::error(
                        DiagnosticKind::RecursionBoundaryError,
                        Some(line),
                        format!("recursion scheme for fn '{}' is {}", decl.name, scheme.status),
                    )
                    .with_help(scheme.open_obligations.join("; ")));
                }
                let scheme_passport = recursion_scheme_passport(theory, &scheme, &[&contract_passport]);
                self.inferred.push((format!("{theory}.{}.recursion", decl.name), scheme_passport));
                Some(scheme)
            }
            _ => None,
        };

        // v0.87: Typed Core facts of the function.
        let params: Option<Vec<(String, crate::ordinary::OrdinaryType)>> = decl
            .params
            .iter()
            .map(|p| crate::ordinary::parse_ordinary_type(&p.ty).ok().map(|ty| (p.name.clone(), ty)))
            .collect();
        if let (Some(params), Ok(ret)) = (params, crate::ordinary::parse_ordinary_type(&decl.ret)) {
            let mut effects: Vec<String> = contract.effects.iter().map(ToString::to_string).collect();
            effects.sort();
            effects.dedup();
            let termination = if looping {
                "tail_loop".to_string()
            } else if !decl.mutual.is_empty() {
                format!("mutual({})", decl.mutual.join(","))
            } else {
                recursion.as_ref().map_or_else(|| "non-recursive".to_string(), |scheme| scheme.measure.to_string())
            };
            self.typed.borrow_mut().record_function(
                &self.current_module, theory, decl,
                crate::typed_core::FunctionFacts { params, ret, effects, purity: contract.purity.to_string(), termination, line, span: self.current_source.line_span(line), released_params: release.params.clone() },
            ).map_err(|message| Diagnostic::error(DiagnosticKind::CompileError, Some(line), format!("internal compiler error: {message}")))?;
        }
        self.functions.entry(theory.to_string()).or_default().insert(
            decl.name.clone(),
            CheckedFn {
                decl: decl.clone(),
                function_type: fn_type,
                contract,
                contract_passport: contract_passport.clone(),
                body,
                recursion,
            },
        );
        Ok(contract_passport)
    }

    /// v0.74: `record Name(field: T, ...)` through `structural::record_type`.
    pub(super) fn check_record_decl(&mut self, theory: &str, decl: &RecordDecl) -> Result<Passport, Diagnostic> {
        let line = decl.line;
        if self.is_builtin_name(&decl.name, theory, line) {
            return Err(Diagnostic::error(
                DiagnosticKind::NameError,
                Some(line),
                format!("record '{}' shadows a builtin", decl.name),
            ));
        }
        // Field types may reference records declared earlier and, since
        // v0.126, the record itself under Option/List/Sequence/Result/Sum
        // (values stay finite: they are built bottom-up and immutable).
        let mut fields = Vec::with_capacity(decl.fields.len());
        for field in &decl.fields {
            crate::ownership::check_declared_type(&field.ty, &format!("field '{}' of record '{}'", field.name, decl.name), line, false)?;
            let mut referenced = Vec::new();
            if let Ok(parsed) = parse_ordinary_type(&field.ty) {
                parsed.record_names(&mut referenced);
            }
            if referenced.iter().any(|name| name == &decl.name)
                && !parse_ordinary_type(&field.ty).is_ok_and(|t| t.self_reference_guarded(&decl.name, false))
            {
                return Err(Diagnostic::error(
                    DiagnosticKind::StructuralTypeError,
                    Some(line),
                    format!("record '{}' field '{}' refers to the record itself without Option/List", decl.name, field.name),
                )
                .with_help("a recursive field must be Option<Self>, List<Self> or similar, so that finite values exist"));
            }
            for name in referenced.iter().filter(|n| *n != &decl.name) {
                self.require_known_records(&format!("Record<{name}>"), theory, line)?;
            }
            fields.push(record_field_decl(field.name.clone(), field.ty.clone(), line)?);
        }
        let mut report = record_type(decl.name.clone(), fields, &[], line)?;
        report.enum_variants = decl.enum_variants.clone();
        let passport = record_type_passport(theory, &report, &[]);
        self.typed.borrow_mut().record_record(
            &self.current_module, theory, decl,
            crate::typed_core::RecordFacts {
                fields: report.fields.iter().map(|f| (f.name.clone(), parse_ordinary_type(&f.ty).expect("checked record field type"))).collect(),
                line,
                span: self.current_source.line_span(line),
            },
        ).map_err(|message| Diagnostic::error(DiagnosticKind::CompileError, Some(line), format!("internal compiler error: {message}")))?;
        self.records.entry(theory.to_string()).or_default().insert(decl.name.clone(), report);
        Ok(passport)
    }

    fn infer_record_construction(
        &self,
        record: &crate::structural::RecordTypeReport,
        args: &[Expr],
        theory: &str,
        line: usize,
    ) -> Result<Passport, Diagnostic> {
        if args.len() != record.fields.len() {
            return Err(Diagnostic::error(
                DiagnosticKind::StructuralTypeError,
                Some(line),
                format!("record `{}` expects {} field value(s), got {}", record.name, record.fields.len(), args.len()),
            ));
        }
        if !record.enum_variants.is_empty() {
            let invalid = || Diagnostic::error(DiagnosticKind::StructuralTypeError, Some(line), format!("enum `{}` must be built with a variant constructor", record.name));
            let mut selected = None;
            for (field, arg) in record.fields.iter().zip(args) {
                let ExprKind::Call { name, args } = &arg.kind else { return Err(invalid()); };
                if args.len() != 1 || (name != "some" && name != "none") { return Err(invalid()); }
                if let Some(v) = field.name.strip_prefix("v_") {
                    if name == "some" {
                        if selected.is_some() || !matches!(&args[0].kind, ExprKind::Call { name, args } if name == "true" && args.is_empty()) { return Err(invalid()); }
                        selected = Some(v);
                    }
                }
            }
            let Some(selected) = selected else { return Err(invalid()); };
            let prefix = format!("f_{selected}_");
            for (field, arg) in record.fields.iter().zip(args) {
                if field.name.starts_with("f_") {
                    let ExprKind::Call { name, .. } = &arg.kind else { return Err(invalid()); };
                    if (name == "some") != field.name.strip_prefix(&prefix).and_then(|i| i.parse::<usize>().ok()).is_some() { return Err(invalid()); }
                }
            }
        }
        let mut values = Vec::with_capacity(args.len());
        for arg in args {
            values.push(self.ordinary_arg(arg, theory, &record.name, &DiagnosticKind::StructuralTypeError)?.0);
        }
        let mut field_values = Vec::with_capacity(values.len());
        for (decl, value) in record.fields.iter().zip(&values) {
            field_values.push(record_field_value(decl.name.clone(), value, line)?);
        }
        let refs: Vec<&Passport> = values.iter().collect();
        let report = record_term(record, field_values, &refs, line)?;
        Ok(record_term_passport(theory, &report, &refs))
    }

    /// v0.102: ownership shape of a checked function of `theory`.
    pub(super) fn buffer_shape(&self, theory: &str, name: &str) -> Option<crate::ownership::FnShape> {
        self.functions.get(theory).and_then(|fns| fns.get(name)).map(|f| crate::ownership::FnShape::of(&f.decl))
    }

    /// v0.75: a declared fn used as a value has type `Fn<P..., R>`. Function
    /// values must be pure and total, so effectful or partial functions can be
    /// called but not passed around.
    pub(super) fn lookup_function_value(&self, name: &str, theory: &str, line: usize) -> Option<Result<Passport, Diagnostic>> {
        let function = self.functions.get(theory)?.get(name)?;
        if function.contract.purity != FunctionPurity::Pure || function.contract.totality != FunctionTotality::Total {
            return Some(Err(Diagnostic::error(
                DiagnosticKind::FunctionContractError,
                Some(line),
                format!(
                    "fn '{name}' is {} and {}; only pure total functions can be used as values",
                    function.contract.purity, function.contract.totality
                ),
            )
            .with_help("call it directly instead of passing it, or remove its effects")));
        }
        let params: Vec<String> = function.decl.params.iter().map(|p| p.ty.clone()).collect();
        let mut value = typed_value(
            theory,
            &fn_type_text(&params, &function.decl.ret),
            &[&function.contract_passport],
            format!("fn_value:{name}"),
        );
        value.trust = function.body.trust;
        value.provenance = function.body.provenance.max(Provenance::InternalDerived);
        value.cost = CostClass::SmallFinite;
        Some(Ok(value))
    }

    /// Value bound to `name` in the current local frame or theory scope.
    pub(super) fn callable_value(&self, name: &str, theory: &str) -> Option<Passport> {
        if let Some(local) = self.locals.borrow().last().and_then(|frame| frame.get(name)).cloned() {
            return Some(local);
        }
        self.theories.get(theory).and_then(|scope| scope.get(name)).cloned()
    }

    /// A function-typed binding may not use a builtin name: builtin dispatch
    /// precedes user calls, so the binding could never be called.
    pub(super) fn check_callable_name(&self, name: &str, theory: &str, line: usize) -> Result<(), Diagnostic> {
        if self.is_builtin_name(name, theory, line) {
            return Err(Diagnostic::error(
                DiagnosticKind::NameError,
                Some(line),
                format!("function value '{name}' shadows a builtin"),
            )
            .with_help("choose another name for the function-typed binding"));
        }
        Ok(())
    }

    /// Hook for theory-level `let` bindings.
    pub(super) fn check_function_binding(&self, name: &str, value: &Passport, theory: &str, line: usize) -> Result<(), Diagnostic> {
        match surface_type_of(value) {
            Some(ty) if fn_signature(&ty).is_some() => self.check_callable_name(name, theory, line),
            _ => Ok(()),
        }
    }

    /// Effects of executed calls. Constructing a closure does not run its body.
    pub(super) fn effects_in(&self, body: &Expr, params: &[FnParam], exclude: Option<&str>, theory: &str, _line: usize) -> Result<(Vec<FunctionEffect>, bool), Diagnostic> {
        fn merge(effects: &mut Vec<FunctionEffect>, partial: &mut bool, extra: Vec<FunctionEffect>, is_partial: bool) {
            for e in extra { if !effects.contains(&e) { effects.push(e); } }
            *partial |= is_partial;
        }
        fn walk(c: &Checker, expr: &Expr, env: &mut BTreeMap<String,OrdinaryType>, exclude: Option<&str>, theory: &str,
            effects: &mut Vec<FunctionEffect>, partial: &mut bool) {
            let bounds = |ty: Option<OrdinaryType>| match ty {
                Some(OrdinaryType::EffectFn(_,_,b)) => Some(b), _ => None,
            };
            match &expr.kind {
                ExprKind::Call { name, args } => {
                    if let Some((kind,boundary,p)) = builtin_effect(name) {
                        merge(effects,partial,vec![FunctionEffect { kind,boundary:boundary.to_string() }],p);
                    } else if !ORDINARY_BUILTINS.contains(&name.as_str()) {
                        let local = env.get(name).cloned().or_else(|| c.callable_value(name,theory).and_then(|v|surface_type_of(&v)).and_then(|t|parse_ordinary_type(&t).ok()));
                        if let Some(b) = bounds(local.clone()) {
                            merge(effects,partial,b.effects(),b.partial());
                        } else if local.is_none() && Some(name.as_str()) != exclude {
                            if let Some(f) = c.functions.get(theory).and_then(|fns|fns.get(name)) {
                                merge(effects,partial,f.contract.effects.clone(),f.contract.totality != FunctionTotality::Total);
                            }
                        }
                    }
                    let executed: Vec<usize> = if name == "case" { vec![1,2] }
                        else { crate::callback::traversal_argument(name).into_iter().collect() };
                    for i in executed {
                        if let Some(arg) = args.get(i) {
                            if let Some(b) = bounds(c.typed.borrow().expr_types.get(&arg.id).cloned()) {
                                merge(effects,partial,b.effects(),b.partial());
                            }
                        }
                    }
                    for (i,arg) in args.iter().enumerate() {
                        if !crate::ordinary::metadata_argument(name,i) { walk(c,arg,env,exclude,theory,effects,partial); }
                    }
                }
                ExprKind::Let { name,value,body } => {
                    walk(c,value,env,exclude,theory,effects,partial);
                    let ty = c.typed.borrow().expr_types.get(&value.id).cloned();
                    let old = env.remove(name);
                    if let Some(t) = ty { env.insert(name.clone(),t); }
                    walk(c,body,env,exclude,theory,effects,partial);
                    env.remove(name); if let Some(t) = old { env.insert(name.clone(),t); }
                }
                ExprKind::Add { lhs,rhs } | ExprKind::CompareGt { lhs,rhs } => {
                    walk(c,lhs,env,exclude,theory,effects,partial); walk(c,rhs,env,exclude,theory,effects,partial);
                }
                ExprKind::Power { base,exp } => {
                    walk(c,base,env,exclude,theory,effects,partial); walk(c,exp,env,exclude,theory,effects,partial);
                }
                ExprKind::Lambda { .. } | ExprKind::Ident(_) | ExprKind::QualifiedIdent { .. }
                | ExprKind::IntLiteral(_) | ExprKind::TextLiteral(_) => {}
            }
        }
        let mut env = BTreeMap::new();
        if let Some(frame) = self.locals.borrow().last() {
            for (n,v) in frame {
                if let Some(t) = surface_type_of(v).and_then(|t|parse_ordinary_type(&t).ok()) { env.insert(n.clone(),t); }
            }
        }
        for p in params { if let Ok(t) = parse_ordinary_type(&p.ty) { env.insert(p.name.clone(),t); } }
        let mut effects = Vec::new(); let mut partial = false;
        walk(self,body,&mut env,exclude,theory,&mut effects,&mut partial);
        Ok((effects,partial))
    }

    /// v0.75: `fun(x: T, ..., body)` through `function::lambda_term`.
    pub(super) fn infer_lambda(&self, params: &[FnParam], body: &Expr, theory: &str, line: usize) -> Result<Passport, Diagnostic> {
        self.infer_lambda_with(params, body, theory, line, None)
    }

    fn infer_lambda_with(&self, params: &[FnParam], body: &Expr, theory: &str, line: usize, bounds: Option<&crate::callback::EffectBounds>) -> Result<Passport, Diagnostic> {
        for param in params {
            parse_ordinary_type(&param.ty).map_err(|e| Diagnostic::error(DiagnosticKind::FunctionTermError,Some(line),e))?;
            crate::ownership::check_declared_type(&param.ty,&format!("function value parameter '{}'",param.name),line,false)?;
            self.require_known_records(&param.ty, theory, line)?;
            if fn_signature(&param.ty).is_some() {
                self.check_callable_name(&param.name, theory, line)?;
            }
        }
        let enclosing = self.locals.borrow().last().cloned().unwrap_or_default();
        let mut frame = enclosing.clone();
        for param in params {
            frame.insert(
                param.name.clone(),
                typed_value(theory, &param.ty, &[], format!("lambda:param:{}:{}", param.name, param.ty)),
            );
        }
        self.locals.borrow_mut().push(frame);
        let body_passport = self.infer_expr(body, theory);
        self.locals.borrow_mut().pop();
        let body_passport = body_passport?;

        let ret = surface_type_of(&body_passport).ok_or_else(|| {
            Diagnostic::error(
                DiagnosticKind::FunctionTermError,
                Some(line),
                format!("lambda body is not an ordinary value: {}", body_passport.ty),
            )
        })?;
        let (effects, partial) = self.effects_in(body, params, None, theory, line)?;
        if let Some(bounds) = bounds {
            bounds.allows(&effects.iter().map(ToString::to_string).collect::<Vec<_>>(),partial)
                .map_err(|e|Diagnostic::error(DiagnosticKind::FunctionContractError,Some(line),e))?;
        } else if !effects.is_empty() || partial {
            let effects: Vec<String> = effects.iter().map(ToString::to_string).collect();
            return Err(Diagnostic::error(
                DiagnosticKind::FunctionContractError,
                Some(line),
                format!(
                    "lambda must be pure and total; it has effects [{}]{}",
                    effects.join(", "),
                    if partial { " and is partial" } else { "" }
                ),
            )
            .with_help("function values are pure and total by construction; move effects into a named fn and call it directly"));
        }

        let param_types: Vec<String> = params.iter().map(|p| p.ty.clone()).collect();
        let domain = domain_of_params(&param_types);
        let fn_report = function_type(domain.clone(), ret.clone(), !partial, effects.is_empty(), &[&body_passport], line)?;
        let bound_name = if params.len() == 1 { params[0].name.clone() } else { "args".to_string() };
        let parameter = bound_variable(bound_name, domain, line)?;
        let param_names: Vec<String> = params.iter().map(|p| p.name.clone()).collect();
        let mut free = Vec::new();
        free_idents(body, &mut param_names.clone(), &mut free);
        let captures: Vec<String> = free.into_iter().filter(|name| enclosing.contains_key(name)).collect();
        let report = lambda_term(parameter, render_expr(body), &fn_report, captures.clone(), &[&body_passport], line)?;
        let ty = if let Some(bounds) = bounds {
            OrdinaryType::EffectFn(param_types.iter().map(|p|parse_ordinary_type(p)).collect::<Result<Vec<_>,_>>()
                .map_err(|e|Diagnostic::error(DiagnosticKind::FunctionTermError,Some(line),e))?,
                Box::new(parse_ordinary_type(&ret).map_err(|e|Diagnostic::error(DiagnosticKind::FunctionTermError,Some(line),e))?),bounds.clone()).canonical()
        } else { fn_type_text(&param_types, &ret) };
        crate::ownership::check_declared_type(&ty,"lambda",line,false)?;
        Ok(typed_value(
            theory,
            &ty,
            &[&body_passport],
            format!("lambda:captures=[{}]:fingerprint={}", captures.join(","), report.fingerprint),
        ))
    }

    fn infer_effect_function(&self, args: &[Expr], theory: &str, line: usize) -> Result<Passport, Diagnostic> {
        arity("effect_fn",args,2,line)?;
        let fail = |e| Diagnostic::error(DiagnosticKind::FunctionContractError,Some(line),e);
        let target_text = self.type_arg(&args[0],theory,"effect_fn",&DiagnosticKind::FunctionTermError)?;
        let target = parse_ordinary_type(&target_text).map_err(fail)?;
        let OrdinaryType::EffectFn(params,ret,bounds) = &target else {
            return Err(fail("effect_fn expects FnEffect(bounds, parameters..., result)".to_string()));
        };
        let value = match &args[1].kind {
            ExprKind::Lambda { params,body } => self.infer_lambda_with(params,body,theory,line,Some(bounds))?,
            ExprKind::Ident(name) if self.callable_value(name,theory).is_none()
                && self.functions.get(theory).is_some_and(|fns|fns.contains_key(name)) => {
                let f = &self.functions[theory][name];
                let actual_params = f.decl.params.iter().map(|p|parse_ordinary_type(&p.ty)).collect::<Result<Vec<_>,_>>().map_err(fail)?;
                let actual_ret = parse_ordinary_type(&f.decl.ret).map_err(fail)?;
                if &actual_params != params || &actual_ret != ret.as_ref() {
                    return Err(fail("effect_fn parameter and result types must match exactly".to_string()));
                }
                bounds.allows(&f.contract.effects.iter().map(ToString::to_string).collect::<Vec<_>>(),f.contract.totality != FunctionTotality::Total).map_err(fail)?;
                let mut v = typed_value(theory,&target_text,&[&f.contract_passport],format!("effect_fn:{name}"));
                v.trust=f.body.trust; v.provenance=f.body.provenance; v
            }
            _ => self.infer_expr(&args[1],theory)?,
        };
        let actual = surface_type_of(&value).ok_or_else(||fail("effect_fn expects a function value".to_string()))?;
        let actual_type = parse_ordinary_type(&actual).map_err(fail)?;
        crate::callback::widening(&actual_type,&target).map_err(fail)?;
        self.typed.borrow_mut().record(args[1].id,actual_type,args[1].span,theory);
        Ok(typed_value(theory,&target_text,&[&value],"effect_fn:checked_bounds"))
    }

    fn effect_traversal_value(&self, theory: &str, ty: &str, sources: &[&Passport], f: &FnArg, name: &str) -> Passport {
        let mut value = typed_value(theory,ty,sources,format!("traversal:{name}:ordered"));
        value.trust=value.trust.max(f.trust); value.provenance=value.provenance.max(f.provenance); value
    }

    fn infer_effect_traversal(&self, name: &str, args: &[Expr], theory: &str, line: usize) -> Result<Passport, Diagnostic> {
        arity(name,args,2,line)?;
        let fail = |e|Diagnostic::error(DiagnosticKind::TraversalError,Some(line),e);
        let (xs,ty) = self.ordinary_arg(&args[0],theory,name,&DiagnosticKind::TraversalError)?;
        let (kind,item) = collection_item(&ty).ok_or_else(||fail(format!("{name} expects List<T> or Sequence<T>")))?;
        let action = self.function_arg(&args[1],theory,name,&DiagnosticKind::TraversalError,line)?;
        if action.params != [item.clone()] { return Err(fail(format!("{name} callback must take exactly one {item} parameter"))); }
        let output = match name {
            "each_effect" => "Nat".to_string(),
            "try_map_effect" | "try_each_effect" => {
                let (ok,err) = result_components(&action.ret).ok_or_else(||fail(format!("{name} callback must return Result<T,E>")))?;
                let success = if name == "try_each_effect" { "Nat".to_string() } else { format!("{kind}<{ok}>") };
                format!("Result<{success},{err}>")
            }
            _ => {
                if action.ret != "Bool" { return Err(fail(format!("{name} predicate must return Bool"))); }
                match name { "filter_effect" => ty, "find_effect" => format!("Option<{item}>"), _ => "Bool".to_string() }
            }
        };
        Ok(self.effect_traversal_value(theory,&output,&[&xs],&action,name))
    }

    /// v0.75: call of a function-typed value (`f(x)` where `f: Fn<...>`).
    fn infer_value_call(&self, name: &str, value: &Passport, args: &[Expr], theory: &str, line: usize) -> Result<Passport, Diagnostic> {
        let ty = surface_type_of(value).unwrap_or_else(|| value.ty.to_string());
        let Some((params, ret)) = fn_signature(&ty) else {
            return Err(Diagnostic::error(
                DiagnosticKind::FunctionTermError,
                Some(line),
                format!("'{name}' is not a function; it has type `{ty}`"),
            ));
        };
        if args.len() != params.len() {
            return Err(Diagnostic::error(
                DiagnosticKind::FunctionTermError,
                Some(line),
                format!("function value '{name}' expects {} argument(s), got {}", params.len(), args.len()),
            ));
        }
        let mut sources = vec![value.clone()];
        for (arg, expected) in args.iter().zip(&params) {
            sources.push(self.expect_type(arg, theory, name, expected, &DiagnosticKind::FunctionTermError)?);
        }
        let refs: Vec<&Passport> = sources.iter().collect();
        Ok(typed_value(theory, &ret, &refs, format!("apply:value:{name}")))
    }

    /// Call of a user `fn`; `None` when `name` is not a declared function.
    pub(super) fn infer_user_fn_call(
        &self,
        name: &str,
        args: &[Expr],
        theory: &str,
        line: usize,
    ) -> Option<Result<Passport, Diagnostic>> {
        if let Some(value) = self.callable_value(name, theory) {
            return Some(self.infer_value_call(name, &value, args, theory, line));
        }
        if let Some(function) = self.functions.get(theory).and_then(|fns| fns.get(name)) {
            return Some(self.infer_user_fn_call_inner(&function, args, theory, line));
        }
        if let Some(record) = self.records.get(theory).and_then(|records| records.get(name)).cloned() {
            return Some(self.infer_record_construction(&record, args, theory, line));
        }
        if let Some(pending) = self.pending_fn.borrow().clone().filter(|pending| pending.name == name) {
            return Some(self.infer_recursive_call(&pending, args, theory, line));
        }
        let member = self.pending_group.borrow().iter().find(|member| member.name == name).cloned()?;
        Some(self.infer_recursive_call(&member, args, theory, line))
    }

    /// Recursive call inside the body being checked: typed against the
    /// declared signature; termination is established by `check_well_founded`.
    fn infer_recursive_call(&self, decl: &FnDecl, args: &[Expr], theory: &str, line: usize) -> Result<Passport, Diagnostic> {
        if args.len() != decl.params.len() {
            return Err(Diagnostic::error(
                DiagnosticKind::FunctionTermError,
                Some(line),
                format!("fn '{}' expects {} argument(s), got {}", decl.name, decl.params.len(), args.len()),
            ));
        }
        let mut values = Vec::with_capacity(args.len());
        for (arg, param) in args.iter().zip(&decl.params) {
            let (value, ty) = self.ordinary_arg(arg, theory, &decl.name, &DiagnosticKind::FunctionTermError)?;
            if ty != param.ty {
                return Err(Diagnostic::error(
                    DiagnosticKind::FunctionTermError,
                    Some(arg.line),
                    format!("fn '{}' parameter '{}' expects `{}`, got `{ty}`", decl.name, param.name, param.ty),
                ));
            }
            values.push(value);
        }
        let refs: Vec<&Passport> = values.iter().collect();
        Ok(typed_value(theory, &decl.ret, &refs, format!("apply:fn:{}:recursive", decl.name)))
    }

    fn infer_user_fn_call_inner(&self, function: &CheckedFn, args: &[Expr], theory: &str, line: usize) -> Result<Passport, Diagnostic> {
        let decl = &function.decl;
        if args.len() != decl.params.len() {
            return Err(Diagnostic::error(
                DiagnosticKind::FunctionTermError,
                Some(line),
                format!("fn '{}' expects {} argument(s), got {}", decl.name, decl.params.len(), args.len()),
            ));
        }
        let mut values = Vec::with_capacity(args.len());
        for (arg, param) in args.iter().zip(&decl.params) {
            let (value, ty) = self.ordinary_arg(arg, theory, &decl.name, &DiagnosticKind::FunctionTermError)?;
            if ty != param.ty {
                return Err(Diagnostic::error(
                    DiagnosticKind::FunctionTermError,
                    Some(arg.line),
                    format!("fn '{}' parameter '{}' expects `{}`, got `{ty}`", decl.name, param.name, param.ty),
                ));
            }
            values.push(value);
        }
        if values.len() == 1 {
            // Cross-check with the core application layer.
            let application = application_term(&function.function_type, &values[0], format!("{}_arg", decl.name), line)?;
            if application.status != ApplicationStatus::Applied {
                return Err(Diagnostic::error(
                    DiagnosticKind::FunctionTermError,
                    Some(line),
                    format!(
                        "core application rejected {}: expected domain `{}`, argument domain `{}`",
                        decl.name, application.expected_domain, application.argument_domain
                    ),
                ));
            }
        }
        let refs: Vec<&Passport> = values.iter().collect();
        let mut result = typed_value(
            theory,
            &decl.ret,
            &refs,
            match &function.recursion {
                Some(scheme) => format!(
                    "apply:fn:{}:contract={}:recursion={}:fuel={}",
                    decl.name, function.contract.status, scheme.status, scheme.initial_fuel
                ),
                None => format!("apply:fn:{}:contract={}", decl.name, function.contract.status),
            },
        );
        // Taint carried by the body (e.g. an axiom inside the function) is
        // never cleaned by applying the function.
        result.trust = result.trust.max(function.body.trust);
        result.provenance = result.provenance.max(function.body.provenance);
        result.validation = result.validation.max(function.body.validation);
        Ok(result)
    }
}
