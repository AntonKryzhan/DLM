//! Type rules at the executable IR boundary. Type arguments of constructors
//! have already been erased; `ok` and `err` therefore have one runtime operand.

use super::{is_number, show, Verifier};
use crate::compile::{IrExpr, COMPILED_BUILTINS};
use crate::ordinary::OrdinaryType;

/// Monomorphic builtin signatures. Polymorphic operations are checked below.
fn signature(name: &str) -> Option<(Vec<OrdinaryType>, OrdinaryType)> {
    use OrdinaryType::*;
    Some(match name {
        "not" => (vec![Bool], Bool),
        "nat_eq" => (vec![Nat, Nat], Bool),
        "int_eq" => (vec![Int, Int], Bool),
        "monus" => (vec![Nat, Nat], Nat),
        "pred" => (vec![Nat], Nat),
        "int" => (vec![Nat], Int),
        "abs" => (vec![Int], Nat),
        "__sub" => (vec![Int, Int], Int),
        "__neg" => (vec![Int], Int),
        "int_to_text" => (vec![Int], Text),
        "nat_to_text" => (vec![Nat], Text),
        "text_to_int" => (vec![Text], Option(Box::new(Int))),
        "text_to_nat" => (vec![Text], Option(Box::new(Nat))),
        "concat" => (vec![Text, Text], Text),
        "text_length" => (vec![Text], Nat),
        "text_eq" => (vec![Text, Text], Bool),
        "substring" => (vec![Text, Nat, Nat], Text),
        "lines" | "words" => (vec![Text], List(Box::new(Text))),
        "join" => (vec![List(Box::new(Text)), Text], Text),
        "print_line" | "print_text" => (vec![Text], Text),
        "read_line" => (vec![], Option(Box::new(Text))),
        "read_all" => (vec![], Text),
        "read_nat" => (vec![], Nat),
        "args" => (vec![], List(Box::new(Text))),
        // v0.102: owned byte buffers (ownership is verified by the checker
        // and, for releases, below).
        "buf_new" => (vec![Nat, Nat], ByteBuf),
        "buf_from_list" => (vec![List(Box::new(Nat))], ByteBuf),
        "buf_len" | "buf_free" => (vec![ByteBuf], Nat),
        "buf_get" => (vec![ByteBuf, Nat], Option(Box::new(Nat))),
        "buf_set" => (vec![ByteBuf, Nat, Nat], ByteBuf),
        "buf_push" => (vec![ByteBuf, Nat], ByteBuf),
        "buf_to_list" => (vec![ByteBuf], List(Box::new(Nat))),
        "buf_copy" => (vec![ByteBuf], ByteBuf),
        "buf_slice" => (vec![ByteBuf, Nat, Nat], ByteBuf),
        "buf_append" => (vec![ByteBuf, ByteBuf], ByteBuf),
        "buf_eq" => (vec![ByteBuf, ByteBuf], Bool),
        "buf_from_text" => (vec![Text], ByteBuf),
        "buf_to_text" => (vec![ByteBuf], Option(Box::new(Text))),
        "tbuf_new" => (vec![], TextBuf),
        "tbuf_push" => (vec![TextBuf, Text], TextBuf),
        "tbuf_len" => (vec![TextBuf], Nat),
        "tbuf_finish" => (vec![TextBuf], Text),
        "fl_of_nat" => (vec![Nat], Float),
        "fl_of_int" => (vec![Int], Float),
        "fl_lit" => (vec![Text], Float),
        "fl_parse" => (vec![Text], Option(Box::new(Float))),
        "fl_add" => (vec![Float, Float], Float),
        "fl_sub" => (vec![Float, Float], Float),
        "fl_mul" => (vec![Float, Float], Float),
        "fl_div" => (vec![Float, Float], Float),
        "fl_neg" => (vec![Float], Float),
        "fl_abs" => (vec![Float], Float),
        "fl_min" => (vec![Float, Float], Float),
        "fl_max" => (vec![Float, Float], Float),
        "fl_sqrt" => (vec![Float], Float),
        "fl_exp" => (vec![Float], Float),
        "fl_log" => (vec![Float], Float),
        "fl_sin" => (vec![Float], Float),
        "fl_cos" => (vec![Float], Float),
        "fl_tan" => (vec![Float], Float),
        "fl_atan" => (vec![Float], Float),
        "fl_atan2" => (vec![Float, Float], Float),
        "fl_pow" => (vec![Float, Float], Float),
        "fl_floor" => (vec![Float], Float),
        "fl_ceil" => (vec![Float], Float),
        "fl_round" => (vec![Float], Float),
        "fl_trunc" => (vec![Float], Float),
        "fl_lt" => (vec![Float, Float], Bool),
        "fl_le" => (vec![Float, Float], Bool),
        "fl_eq" => (vec![Float, Float], Bool),
        "fl_is_nan" => (vec![Float], Bool),
        "fl_is_finite" => (vec![Float], Bool),
        "fl_to_text" => (vec![Float], Text),
        "fl_to_int" => (vec![Float], Option(Box::new(Int))),
        "fl_bits" => (vec![Float], Nat),
        "fl_from_bits" => (vec![Nat], Float),
        "fv_new" => (vec![Nat, Float], Option(Box::new(FloatVec))),
        "fv_from_list" => (vec![List(Box::new(Float))], Option(Box::new(FloatVec))),
        "fv_to_list" => (vec![FloatVec], List(Box::new(Float))),
        "fv_len" => (vec![FloatVec], Nat),
        "fv_get" => (vec![FloatVec, Nat], Option(Box::new(Float))),
        "fv_set" => (vec![FloatVec, Nat, Float], Option(Box::new(FloatVec))),
        "fv_add" => (vec![FloatVec, FloatVec], Option(Box::new(FloatVec))),
        "fv_sub" => (vec![FloatVec, FloatVec], Option(Box::new(FloatVec))),
        "fv_mul" => (vec![FloatVec, FloatVec], Option(Box::new(FloatVec))),
        "fv_scale" => (vec![FloatVec, Float], FloatVec),
        "fv_axpy" => (vec![Float, FloatVec, FloatVec], Option(Box::new(FloatVec))),
        "fv_dot" => (vec![FloatVec, FloatVec], Option(Box::new(Float))),
        "fv_sum" => (vec![FloatVec], Float),
        "fv_norm" => (vec![FloatVec], Float),
        "fv_max" => (vec![FloatVec], Option(Box::new(Float))),
        "fv_min" => (vec![FloatVec], Option(Box::new(Float))),
        "fv_slice" => (vec![FloatVec, Nat, Nat], Option(Box::new(FloatVec))),
        "fv_concat" => (vec![FloatVec, FloatVec], Option(Box::new(FloatVec))),
        "fv_matmul" => (vec![FloatVec, FloatVec, Nat, Nat, Nat], Option(Box::new(FloatVec))),
        "fv_transpose" => (vec![FloatVec, Nat, Nat], Option(Box::new(FloatVec))),
        "fv_empty" => (vec![], FloatVec),
        "fv_solve" => (vec![FloatVec, FloatVec, Nat], Option(Box::new(FloatVec))),
        "nv_empty" => (vec![], NatVec),
        "nv_new" => (vec![Nat, Nat], Option(Box::new(NatVec))),
        "nv_from_list" => (vec![List(Box::new(Nat))], Option(Box::new(NatVec))),
        "nv_to_list" => (vec![NatVec], List(Box::new(Nat))),
        "nv_len" => (vec![NatVec], Nat),
        "nv_get" => (vec![NatVec, Nat], Option(Box::new(Nat))),
        "nv_set" => (vec![NatVec, Nat, Nat], Option(Box::new(NatVec))),
        "nv_push" => (vec![NatVec, Nat], Option(Box::new(NatVec))),
        "nv_slice" => (vec![NatVec, Nat, Nat], Option(Box::new(NatVec))),
        "nv_concat" => (vec![NatVec, NatVec], Option(Box::new(NatVec))),
        "nv_range" => (vec![Nat, Nat], Option(Box::new(NatVec))),
        "nv_sum" => (vec![NatVec], Option(Box::new(Nat))),
        "nv_max" => (vec![NatVec], Option(Box::new(Nat))),
        "nv_min" => (vec![NatVec], Option(Box::new(Nat))),
        "nv_sort" => (vec![NatVec], NatVec),
        "nv_reverse" => (vec![NatVec], NatVec),
        "nv_find" => (vec![NatVec, Nat], Option(Box::new(Nat))),
        "nv_to_fv" => (vec![NatVec], FloatVec),
        "gr_bfs" => (vec![Nat, NatVec, NatVec, Nat], Option(Box::new(NatVec))),
        "gr_dfs" => (vec![Nat, NatVec, NatVec, Nat], Option(Box::new(NatVec))),
        "gr_toposort" => (vec![Nat, NatVec, NatVec], Option(Box::new(NatVec))),
        "gr_components" => (vec![Nat, NatVec, NatVec], Option(Box::new(NatVec))),
        "gr_scc" => (vec![Nat, NatVec, NatVec], Option(Box::new(NatVec))),
        "gr_bipartite" => (vec![Nat, NatVec, NatVec], Option(Box::new(NatVec))),
        "gr_dijkstra" => (vec![Nat, NatVec, NatVec, FloatVec, Nat], Option(Box::new(FloatVec))),
        "gr_bellman_ford" => (vec![Nat, NatVec, NatVec, FloatVec, Nat], Option(Box::new(FloatVec))),
        "gr_floyd" => (vec![Nat, NatVec, NatVec, FloatVec], Option(Box::new(FloatVec))),
        "gr_mst" => (vec![Nat, NatVec, NatVec, FloatVec], Option(Box::new(NatVec))),
        "gr_maxflow" => (vec![Nat, NatVec, NatVec, NatVec, Nat, Nat], Option(Box::new(Nat))),
        "la_lu" => (vec![FloatVec, Nat], Option(Box::new(FloatVec))),
        "la_lu_perm" => (vec![FloatVec, Nat], Option(Box::new(NatVec))),
        "la_det" => (vec![FloatVec, Nat], Option(Box::new(Float))),
        "la_inverse" => (vec![FloatVec, Nat], Option(Box::new(FloatVec))),
        "la_cholesky" => (vec![FloatVec, Nat], Option(Box::new(FloatVec))),
        "la_qr_q" => (vec![FloatVec, Nat, Nat], Option(Box::new(FloatVec))),
        "la_qr_r" => (vec![FloatVec, Nat, Nat], Option(Box::new(FloatVec))),
        "la_lstsq" => (vec![FloatVec, FloatVec, Nat, Nat], Option(Box::new(FloatVec))),
        "la_eigh_values" => (vec![FloatVec, Nat], Option(Box::new(FloatVec))),
        "la_eigh_vectors" => (vec![FloatVec, Nat], Option(Box::new(FloatVec))),
        "la_svd_values" => (vec![FloatVec, Nat, Nat], Option(Box::new(FloatVec))),
        "la_rank" => (vec![FloatVec, Nat, Nat, Float], Option(Box::new(Nat))),
        "la_identity" => (vec![Nat], Option(Box::new(FloatVec))),
        "la_trace" => (vec![FloatVec, Nat], Option(Box::new(Float))),
        "la_csr_matvec" => (vec![NatVec, NatVec, FloatVec, FloatVec], Option(Box::new(FloatVec))),
        "la_cg" => (vec![NatVec, NatVec, FloatVec, FloatVec, Float, Nat], Option(Box::new(FloatVec))),
        "fft_forward" => (vec![FloatVec], Option(Box::new(FloatVec))),
        "fft_inverse" => (vec![FloatVec], Option(Box::new(FloatVec))),
        "fft_convolve" => (vec![FloatVec, FloatVec], Option(Box::new(FloatVec))),
        "lp_solve" => (vec![FloatVec, FloatVec, FloatVec, Nat, Nat], Option(Box::new(FloatVec))),
        "lp_status" => (vec![FloatVec, FloatVec, FloatVec, Nat, Nat], Nat),
        "nt_gcd" => (vec![Nat, Nat], Nat),
        "nt_lcm" => (vec![Nat, Nat], Option(Box::new(Nat))),
        "nt_mod_mul" => (vec![Nat, Nat, Nat], Option(Box::new(Nat))),
        "nt_mod_pow" => (vec![Nat, Nat, Nat], Option(Box::new(Nat))),
        "nt_mod_inv" => (vec![Nat, Nat], Option(Box::new(Nat))),
        "nt_is_prime" => (vec![Nat], Bool),
        "nt_next_prime" => (vec![Nat], Option(Box::new(Nat))),
        "nt_factor" => (vec![Nat], Option(Box::new(NatVec))),
        "nt_totient" => (vec![Nat], Nat),
        "nt_divisors" => (vec![Nat], Option(Box::new(NatVec))),
        "nt_primes" => (vec![Nat], Option(Box::new(NatVec))),
        "nt_crt" => (vec![NatVec, NatVec], Option(Box::new(Nat))),
        "nt_sqrt_mod" => (vec![Nat, Nat], Option(Box::new(Nat))),
        "nt_isqrt" => (vec![Nat], Nat),
        "pf_add" => (vec![NatVec, NatVec, Nat], Option(Box::new(NatVec))),
        "pf_sub" => (vec![NatVec, NatVec, Nat], Option(Box::new(NatVec))),
        "pf_mul" => (vec![NatVec, NatVec, Nat], Option(Box::new(NatVec))),
        "pf_div" => (vec![NatVec, NatVec, Nat], Option(Box::new(NatVec))),
        "pf_rem" => (vec![NatVec, NatVec, Nat], Option(Box::new(NatVec))),
        "pf_gcd" => (vec![NatVec, NatVec, Nat], Option(Box::new(NatVec))),
        "pf_powmod" => (vec![NatVec, Nat, NatVec, Nat], Option(Box::new(NatVec))),
        "pf_eval" => (vec![NatVec, Nat, Nat], Option(Box::new(Nat))),
        "pf_irreducible" => (vec![NatVec, Nat], Option(Box::new(Bool))),
        "pm_compose" => (vec![NatVec, NatVec], Option(Box::new(NatVec))),
        "pm_inverse" => (vec![NatVec], Option(Box::new(NatVec))),
        "pm_power" => (vec![NatVec, Nat], Option(Box::new(NatVec))),
        "pm_order" => (vec![NatVec], Option(Box::new(Nat))),
        "pm_sign" => (vec![NatVec], Option(Box::new(Nat))),
        "pm_cycle_type" => (vec![NatVec], Option(Box::new(NatVec))),
        "pm_orbit" => (vec![NatVec, Nat, Nat], Option(Box::new(NatVec))),
        "pm_group_order" => (vec![NatVec, Nat], Option(Box::new(Nat))),
        "sat_solve" => (vec![NatVec, NatVec, Nat], Option(Box::new(NatVec))),
        "sat_status" => (vec![NatVec, NatVec, Nat], Nat),
        "tp_homology" => (vec![NatVec, NatVec], Option(Box::new(NatVec))),
        "tp_rips" => (vec![FloatVec, Nat, Nat, Float], Option(Box::new(FloatVec))),
        "gb_basis" => (vec![NatVec, NatVec, Nat, Nat, Nat], Option(Box::new(NatVec))),
        "gb_reduce" => (vec![NatVec, NatVec, NatVec, Nat, Nat, Nat], Option(Box::new(NatVec))),
        "mv_add" => (vec![NatVec, NatVec, Nat, Nat], Option(Box::new(NatVec))),
        "mv_sub" => (vec![NatVec, NatVec, Nat, Nat], Option(Box::new(NatVec))),
        "mv_mul" => (vec![NatVec, NatVec, Nat, Nat], Option(Box::new(NatVec))),
        "mv_normalize" => (vec![NatVec, Nat, Nat, Nat], Option(Box::new(NatVec))),
        "sat_proof" => (vec![NatVec, NatVec, Nat], Option(Box::new(NatVec))),
        "sat_check" => (vec![NatVec, NatVec, Nat, NatVec], Option(Box::new(Bool))),
        "sat_count" => (vec![NatVec, NatVec, Nat], Option(Box::new(Nat))),
        "ilp_solve" => (vec![FloatVec, FloatVec, FloatVec, Nat, Nat, NatVec], Option(Box::new(FloatVec))),
        "ilp_status" => (vec![FloatVec, FloatVec, FloatVec, Nat, Nat, NatVec], Nat),
        "km_empty" => (vec![], NatMap),
        "km_insert" => (vec![NatMap, Nat, Nat], Option(Box::new(NatMap))),
        "km_get" => (vec![NatMap, Nat], Option(Box::new(Nat))),
        "km_remove" => (vec![NatMap, Nat], NatMap),
        "km_contains" => (vec![NatMap, Nat], Bool),
        "km_len" => (vec![NatMap], Nat),
        "km_keys" => (vec![NatMap], NatVec),
        "km_values" => (vec![NatMap], NatVec),
        "km_from" => (vec![NatVec, NatVec], Option(Box::new(NatMap))),
        "km_add" => (vec![NatMap, Nat, Nat], Option(Box::new(NatMap))),
        "km_min" => (vec![NatMap], Option(Box::new(Nat))),
        "km_max" => (vec![NatMap], Option(Box::new(Nat))),
        "km_floor" => (vec![NatMap, Nat], Option(Box::new(Nat))),
        "km_ceil" => (vec![NatMap, Nat], Option(Box::new(Nat))),
        "km_merge" => (vec![NatMap, NatMap], Option(Box::new(NatMap))),
        "km_range" => (vec![NatMap, Nat, Nat], NatMap),
        "tm_empty" => (vec![], TextMap),
        "tm_insert" => (vec![TextMap, Text, Nat], Option(Box::new(TextMap))),
        "tm_get" => (vec![TextMap, Text], Option(Box::new(Nat))),
        "tm_remove" => (vec![TextMap, Text], TextMap),
        "tm_contains" => (vec![TextMap, Text], Bool),
        "tm_len" => (vec![TextMap], Nat),
        "tm_keys" => (vec![TextMap], List(Box::new(Text))),
        "tm_values" => (vec![TextMap], NatVec),
        "tm_from" => (vec![List(Box::new(Text)), NatVec], Option(Box::new(TextMap))),
        "tm_add" => (vec![TextMap, Text, Nat], Option(Box::new(TextMap))),
        "tm_merge" => (vec![TextMap, TextMap], Option(Box::new(TextMap))),
        "tm_count_words" => (vec![Text], TextMap),
        "la_pcg" => (vec![NatVec, NatVec, FloatVec, FloatVec, Float, Nat], Option(Box::new(FloatVec))),
        "la_bicgstab" => (vec![NatVec, NatVec, FloatVec, FloatVec, Float, Nat], Option(Box::new(FloatVec))),
        "la_gmres" => (vec![NatVec, NatVec, FloatVec, FloatVec, Float, Nat, Nat], Option(Box::new(FloatVec))),
        "geo_orient2d" => (vec![FloatVec], Option(Box::new(Nat))),
        "geo_incircle" => (vec![FloatVec], Option(Box::new(Nat))),
        "geo_orient3d" => (vec![FloatVec], Option(Box::new(Nat))),
        "geo_delaunay" => (vec![FloatVec], Option(Box::new(NatVec))),
        "kd_knn" => (vec![FloatVec, FloatVec, Nat, Nat], Option(Box::new(NatVec))),
        "kd_radius" => (vec![FloatVec, Nat, FloatVec, Float], Option(Box::new(NatVec))),
        "fft2_forward" => (vec![FloatVec, Nat, Nat], Option(Box::new(FloatVec))),
        "fft2_inverse" => (vec![FloatVec, Nat, Nat], Option(Box::new(FloatVec))),
        "sig_stft" => (vec![FloatVec, FloatVec, Nat], Option(Box::new(FloatVec))),
        "sig_dwt" => (vec![FloatVec, Nat, Nat], Option(Box::new(FloatVec))),
        "sig_idwt" => (vec![FloatVec, Nat, Nat], Option(Box::new(FloatVec))),
        "sig_iir" => (vec![FloatVec, FloatVec, FloatVec], Option(Box::new(FloatVec))),
        "sig_sha256" => (vec![Text], Text),
        "bvh_raycast" => (vec![FloatVec, FloatVec, FloatVec], Option(Box::new(FloatVec))),
        "bvh_winding" => (vec![FloatVec, FloatVec], Option(Box::new(FloatVec))),
        "bvh_pairs" => (vec![FloatVec], Option(Box::new(NatVec))),
        "gx_new" => (vec![Nat, Nat, Nat], Option(Box::new(NatVec))),
        "gx_width" | "gx_height" | "gx_checksum" => (vec![NatVec], Nat),
        "gx_get" => (vec![NatVec, Nat, Nat], Option(Box::new(Nat))),
        "gx_fill_rect" | "gx_line" => (vec![NatVec, Nat, Nat, Nat, Nat, Nat], NatVec),
        "gx_circle" => (vec![NatVec, Nat, Nat, Nat, Nat, Bool], NatVec),
        "gx_text" => (vec![NatVec, Nat, Nat, Text, Nat, Nat], NatVec),
        "gx_text_width" => (vec![Text, Nat], Nat),
        "gx_blit" => (vec![NatVec, NatVec, Nat, Nat], NatVec),
        "vx_render" => (vec![NatVec, Nat, Nat, Nat, Nat], Option(Box::new(NatVec))),
        "vx_render_region" => (vec![NatVec, NatVec, Nat, Nat, Nat, Nat, Nat, Nat], Option(Box::new(NatVec))),
        "vx_bounds" => (vec![NatVec, Nat, Nat], Option(Box::new(NatVec))),
        "vx_text_width" => (vec![Text, Nat], Nat),
        "vx_svg" => (vec![NatVec, Nat, Nat], Option(Box::new(Text))),
        "vx_text" => (vec![Nat, Nat, Nat, Nat, NatVec, Text], NatVec),
        "vx_text_fonts" => (vec![Nat, Nat, Nat, Nat, NatVec, NatVec, Text], NatVec),
        "vx_font_width" => (vec![NatVec, Text, Nat], Nat),
        "vx_font_has" => (vec![Nat, Nat], Bool),
        "vx_font_advance" => (vec![Nat, Nat, Nat], Nat),
        "vx_font_metrics" => (vec![Nat], NatVec),
        "vx_codepoints" => (vec![Text], NatVec),
        "vx_from_codepoints" => (vec![NatVec], Text),
        "file_font_load" => (vec![Text], Result(Box::new(Nat), Box::new(Text))),
        "file_font_find" => (vec![Text], Result(Box::new(Text), Box::new(Text))),
        "file_write_png" => (vec![Text, NatVec], Result(Box::new(Nat), Box::new(Text))),
        "file_open" => (vec![Text, Text], File),
        "file_ok" => (vec![File], Bool),
        "file_error" => (vec![File], Text),
        "file_write" => (vec![File, Text], Bool),
        "file_write_bytes" => (vec![File, ByteBuf], Bool),
        "file_read_line" => (vec![File], Option(Box::new(Text))),
        "file_read_all" => (vec![File], Text),
        "file_read_bytes" => (vec![File, Nat], ByteBuf),
        "file_flush" => (vec![File], Bool),
        "file_close" => (vec![File], Result(Box::new(Nat), Box::new(Text))),
        "file_remove" => (vec![Text], Result(Box::new(Nat), Box::new(Text))),
        "file_set_buffered" => (vec![File, Bool], Bool),
        "file_seek" => (vec![File, Nat], Bool),
        "file_tell" | "file_size" => (vec![File], Nat),
        "file_truncate" => (vec![File, Nat], Bool),
        "file_exists" | "file_is_dir" => (vec![Text], Bool),
        "file_size_of" | "file_mtime" | "file_make_dir" | "file_remove_dir" => (vec![Text], Result(Box::new(Nat), Box::new(Text))),
        "file_list_dir" | "file_walk" => (vec![Text], Result(Box::new(List(Box::new(Text))), Box::new(Text))),
        "file_rename" | "file_copy" | "file_write_atomic" => (vec![Text, Text], Result(Box::new(Nat), Box::new(Text))),
        "proc_run" => (vec![Text, List(Box::new(Text)), Text], Result(Box::new(List(Box::new(Text))), Box::new(Text))),
        "proc_env" => (vec![Text], Option(Box::new(Text))),
        "net_resolve" => (vec![Text], Result(Box::new(List(Box::new(Text))), Box::new(Text))),
        "net_listen" => (vec![Text, Nat], Result(Box::new(Nat), Box::new(Text))),
        "net_local_port" | "net_shutdown" | "net_close" => (vec![Nat], Result(Box::new(Nat), Box::new(Text))),
        "net_accept" => (vec![Nat, Nat], Result(Box::new(Nat), Box::new(Text))),
        "net_send" => (vec![Nat, Text], Result(Box::new(Nat), Box::new(Text))),
        "net_connect" => (vec![Text, Nat, Nat], Result(Box::new(Nat), Box::new(Text))),
        "net_recv_line" => (vec![Nat], Result(Box::new(Option(Box::new(Text))), Box::new(Text))),
        "net_recv_all" => (vec![Nat], Result(Box::new(Text), Box::new(Text))),
        "proc_spawn" => (vec![Text, List(Box::new(Text)), Text], Result(Box::new(Nat), Box::new(Text))),
        "proc_poll" => (vec![Nat], Result(Box::new(Option(Box::new(Nat))), Box::new(Text))),
        "proc_wait" => (vec![Nat], Result(Box::new(List(Box::new(Text))), Box::new(Text))),
        "proc_kill" => (vec![Nat], Result(Box::new(Nat), Box::new(Text))),
        "win_child" => (vec![Nat, Text, Nat, Nat], Result(Box::new(Nat), Box::new(Text))),
        "win_attach" | "win_resize" => (vec![Nat, Nat, Nat], Result(Box::new(Nat), Box::new(Text))),
        "win_focus" => (vec![Nat], Result(Box::new(Nat), Box::new(Text))),
        "win_dpi" => (vec![Nat], Result(Box::new(Float), Box::new(Text))),
        "win_open" => (vec![Text, Nat, Nat], Result(Box::new(Nat), Box::new(Text))),
        "win_present" => (vec![Nat, NatVec], Result(Box::new(Nat), Box::new(Text))),
        "win_poll" => (vec![Nat, Nat], Result(Box::new(NatVec), Box::new(Text))),
        "win_size" => (vec![Nat], Result(Box::new(NatVec), Box::new(Text))),
        "win_close" => (vec![Nat], Result(Box::new(Nat), Box::new(Text))),
        "win_gpu" => (vec![Nat], Result(Box::new(Text), Box::new(Text))),
        "win_present_scene" => (vec![Nat, NatVec, Nat, Nat, Nat, Nat], Result(Box::new(Nat), Box::new(Text))),
        "win_read" => (vec![Nat], Result(Box::new(NatVec), Box::new(Text))),
        "bn_from_nat" => (vec![Nat], BigNat),
        "bn_to_nat" => (vec![BigNat], Option(Box::new(Nat))),
        "bn_from_words" => (vec![List(Box::new(Nat))], Option(Box::new(BigNat))),
        "bn_to_words" => (vec![BigNat], List(Box::new(Nat))),
        "bn_from_text" => (vec![Text], Option(Box::new(BigNat))),
        "bn_to_text" => (vec![BigNat], Text),
        "bn_bits" => (vec![BigNat], Nat),
        "bn_limbs" => (vec![BigNat], Nat),
        "bn_cmp" => (vec![BigNat, BigNat], Int),
        "bn_add" => (vec![BigNat, BigNat], Option(Box::new(BigNat))),
        "bn_monus" => (vec![BigNat, BigNat], BigNat),
        "bn_mul" => (vec![BigNat, BigNat], Option(Box::new(BigNat))),
        "bn_div" => (vec![BigNat, BigNat], Option(Box::new(BigNat))),
        "bn_mod" => (vec![BigNat, BigNat], Option(Box::new(BigNat))),
        "bn_shl" => (vec![BigNat, Nat], Option(Box::new(BigNat))),
        "bn_shr" => (vec![BigNat, Nat], BigNat),
        "bn_gcd" => (vec![BigNat, BigNat], BigNat),
        "bn_sqrt" => (vec![BigNat], BigNat),
        "bn_powmod" => (vec![BigNat, BigNat, BigNat], Option(Box::new(BigNat))),
        "bn_modinv" => (vec![BigNat, BigNat], Option(Box::new(BigNat))),
        "bn_is_prime" => (vec![BigNat], Bool),
        "bn_next_prime" => (vec![BigNat], Option(Box::new(BigNat))),
        "bn_random" => (vec![Nat, Nat], Option(Box::new(BigNat))),
        "bn_to_hex" => (vec![BigNat], Text),
        "bn_from_hex" => (vec![Text], Option(Box::new(BigNat))),
        _ => return None,
    })
}

impl Verifier<'_> {
    /// Returns (is_sequence, item_type) after checking a collection operand.
    fn collection(&mut self, node: &IrExpr, name: &str) -> Option<(bool, OrdinaryType)> {
        match &node.ty {
            Some(OrdinaryType::List(item)) => Some((false, (**item).clone())),
            Some(OrdinaryType::Sequence(item)) => Some((true, (**item).clone())),
            _ => {
                self.issue(format!("{name} expects List or Sequence, got {}", show(&node.ty)));
                None
            }
        }
    }

    fn effect_traversal(&mut self, node: &IrExpr, name: &str, args: &[IrExpr]) {
        use OrdinaryType::*;
        let Some((sequence,item)) = self.collection(&args[0],name) else { return };
        let index = crate::callback::traversal_argument(name).expect("effect traversal");
        let (params,ret) = match &args[index].ty {
            Some(Fn(p,r)) => (p,r),
            Some(EffectFn(p,r,b)) => { for e in b.effect_names() { self.effect(&e); } (p,r) }
            _ => { self.issue("effect traversal requires a callable"); return; }
        };
        if args[index].ty.as_ref().is_some_and(OrdinaryType::nests_byte_buf) { self.issue("resource in callback signature"); }
        let collection = |t: OrdinaryType| if sequence { Sequence(Box::new(t)) } else { List(Box::new(t)) };
        let expected = if name == "fold_effect" {
            let Some(acc) = &args[1].ty else { self.issue("untyped fold initial value"); return };
            if params != &[acc.clone(),item] || ret.as_ref() != acc { self.issue("fold_effect callback signature differs"); }
            acc.clone()
        } else {
            if params != &[item.clone()] { self.issue("effect traversal callback must take one collection element"); }
            match name {
                "map_effect" => collection((**ret).clone()),
                "each_effect" => Nat,
                "try_map_effect" | "try_each_effect" => {
                    let Result(ok,err) = ret.as_ref() else { self.issue("fallible callback must return Result"); return };
                    let success = if name == "try_each_effect" { Nat } else { collection((**ok).clone()) };
                    Result(Box::new(success),err.clone())
                }
                _ => {
                    if ret.as_ref() != &Bool { self.issue("effect traversal predicate must return Bool"); }
                    match name { "filter_effect" => collection(item), "find_effect" => Option(Box::new(item)), _ => Bool }
                }
            }
        };
        self.expect(node,&expected,"effect traversal result");
    }

    pub(super) fn builtin(&mut self, node: &IrExpr, name: &str, args: &[IrExpr]) {
        use OrdinaryType::*;
        let Some((_, arity)) = COMPILED_BUILTINS.iter().find(|(n, _)| *n == name) else {
            self.issue(format!("unknown builtin {name}"));
            return;
        };
        let arity = if matches!(name, "ok" | "err") { Some(1) } else { *arity };
        if let Some(expected) = arity {
            if args.len() != expected {
                self.issue(format!("{name} with {} argument(s), expects {expected}", args.len()));
                return;
            }
        } else if args.is_empty() {
            self.issue(format!("{name} requires at least one operand; use an empty constructor"));
            return;
        }

        if name == "effect_fn" {
            match (&args[0].ty,&node.ty) {
                (Some(source),Some(target)) => if let Err(e) = crate::callback::widening(source,target) { self.issue(e); },
                _ => self.issue("effect_fn requires typed operands"),
            }
            return;
        }
        if crate::callback::traversal_argument(name).is_some() { self.effect_traversal(node,name,args); return; }
        if let Some((params, result)) = signature(name) {
            for (arg, param) in args.iter().zip(&params) {
                self.expect(arg, param, &format!("operand of {name}"));
            }
            self.expect(node, &result, &format!("result of {name}"));
            return;
        }

        match name {
            "mul" | "div" | "mod" => {
                if !is_number(&node.ty) {
                    self.issue(format!("{name} result `{node}` is not Nat or Int"));
                }
                for arg in args {
                    self.same(&arg.ty, &node.ty, &format!("operand of {name}"), node);
                }
            }
            "some" => match &node.ty {
                Some(Option(item)) => self.expect(&args[0], item, "some payload"),
                _ => self.issue(format!("some must return Option, got {}", show(&node.ty))),
            },
            "ok" | "err" => match &node.ty {
                Some(Result(ok, err)) => self.expect(&args[0], if name == "ok" { ok } else { err }, "Result payload"),
                _ => self.issue(format!("{name} must return Result, got {}", show(&node.ty))),
            },
            "is_some" | "is_none" | "is_ok" | "is_err" | "is_left" => {
                let valid = match name {
                    "is_some" | "is_none" => matches!(args[0].ty, Some(Option(_))),
                    "is_ok" | "is_err" => matches!(args[0].ty, Some(Result(..))),
                    _ => matches!(args[0].ty, Some(Sum(..))),
                };
                if !valid {
                    self.issue(format!("invalid operand type {} for {name}", show(&args[0].ty)));
                }
                self.expect(node, &Bool, "predicate result");
            }
            "list" | "sequence" => {
                let item = match (&node.ty, name) {
                    (Some(List(item)), "list") | (Some(Sequence(item)), "sequence") => Some(item),
                    _ => None,
                };
                if let Some(item) = item {
                    for arg in args {
                        self.expect(arg, item, "collection element");
                    }
                } else {
                    self.issue(format!("{name} has invalid result type {}", show(&node.ty)));
                }
            }
            "length" | "index" | "head" | "tail" | "is_empty" | "__match_head" => {
                if name == "index" {
                    self.expect(&args[1], &Nat, "index position");
                }
                if let Some((_, item)) = self.collection(&args[0], name) {
                    match name {
                        "length" => self.expect(node, &Nat, "length result"),
                        "is_empty" => self.expect(node, &Bool, "is_empty result"),
                        "index" | "head" => self.expect(node, &Option(Box::new(item)), "collection lookup result"),
                        "__match_head" => self.expect(node, &item, "list pattern payload"),
                        _ => self.same(&node.ty, &args[0].ty, "tail result type differs", node),
                    }
                }
            }
            "__match_variant" | "__enum_payload" => match &args[0].ty {
                Some(Option(item)) if args.len() == 2 => {
                    self.expect(&args[1], &Text, "enum variant coverage");
                    self.expect(node, item, "enum variant payload")
                }
                _ => self.issue(format!("enum pattern expects an Option field, got {}", show(&args[0].ty))),
            },
            "__match_some" => match &args[0].ty {
                Some(Option(item)) => self.expect(node, item, "option pattern payload"),
                _ => self.issue(format!("option pattern expects Option, got {}", show(&args[0].ty))),
            },
            "__match_ok" | "__match_err" => match &args[0].ty {
                Some(Result(good, error)) => self.expect(node, if name == "__match_ok" { good } else { error }, "result pattern payload"),
                _ => self.issue(format!("result pattern expects Result, got {}", show(&args[0].ty))),
            },
            "cons" => {
                if let Some((_, item)) = self.collection(&args[1], name) {
                    self.expect(&args[0], &item, "cons head");
                    self.same(&node.ty, &args[1].ty, "cons result type differs from the collection", node);
                }
            }
            "equal" | "less" => {
                self.same(&args[1].ty, &args[0].ty, "compared operands differ", node);
                if name == "less" && !matches!(args[0].ty, Some(Nat | Int | Text)) {
                    self.issue(format!("less expects Nat, Int or Text, got {}", show(&args[0].ty)));
                }
                if name == "equal" && args[0].ty.as_ref().is_some_and(|t| matches!(t, Fn(..) | EffectFn(..)) || t.is_resource() || t.nests_byte_buf()) {
                    self.issue(format!("equal expects an Eq type, got {}", show(&args[0].ty)));
                }
                self.expect(node, &Bool, "comparison result");
            }
            "pair" => match (&args[0].ty, &args[1].ty) {
                (Some(left), Some(right)) => self.expect(node, &Product(Box::new(left.clone()), Box::new(right.clone())), "pair result"),
                _ => self.issue("untyped pair operand"),
            },
            "fst" | "snd" => match &args[0].ty {
                Some(Product(left, right)) => self.expect(node, if name == "fst" { left } else { right }, "product projection"),
                _ => self.issue(format!("{name} expects Product, got {}", show(&args[0].ty))),
            },
            "map" => {
                if let Some((sequence, item)) = self.collection(&args[0], name) {
                    match &args[1].ty {
                        Some(Fn(params, ret)) | Some(EffectFn(params, ret, _)) if params == &[item.clone()] => {
                            if let Some(EffectFn(_,_,b)) = &args[1].ty {
                                if !b.effects().is_empty() || b.partial() { self.issue("map requires a pure total function"); }
                            }
                            let result = if sequence { Sequence(ret.clone()) } else { List(ret.clone()) };
                            self.expect(node, &result, "map result");
                        }
                        _ => self.issue("map function must take exactly one collection element"),
                    }
                }
            }
            "fold" => {
                if let Some((_, item)) = self.collection(&args[0], name) {
                    if let Some(acc) = &args[1].ty {
                        self.expect(&args[2], &Fn(vec![acc.clone(), item], Box::new(acc.clone())), "fold step");
                        self.expect(node, acc, "fold result");
                    }
                }
            }
            "print_decimal" => {
                if !is_number(&args[0].ty) {
                    self.issue(format!("print_decimal expects Nat or Int, got {}", show(&args[0].ty)));
                }
                self.expect(node, &Text, "print_decimal result");
            }
            "print_symbolic" => self.expect(node, &Text, "print_symbolic result"),
            // v0.102: `__release(value, owner)` releases a ByteBuf owner slot
            // after its scope has produced `value`.
            "__release" => {
                self.same(&node.ty, &args[0].ty, "release result differs from the released scope", node);
                match args[1].op {
                    crate::compile::IrOp::Local(slot) if self.function.borrowed.iter().any(|&p| p + self.function.captures == slot) => {
                        self.issue("a borrowed (&ByteBuf) parameter is released by its borrower");
                    }
                    crate::compile::IrOp::Local(_) => {}
                    _ => self.issue("released owner is not a local slot"),
                }
                if !args[1].ty.as_ref().is_some_and(OrdinaryType::is_resource) {
                    self.issue(format!("released owner is not a resource: {}", super::show(&args[1].ty)));
                }
            }
            // A newly compiled builtin needs an explicit IR type rule.
            _ => self.issue(format!("builtin {name} has no IR type rule")),
        }
    }
}
