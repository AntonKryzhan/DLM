//! Stage B: corrupted typed IR must be rejected before it reaches a backend.

use dlm_core::{
    lower_program, parse_module, verify_ir, CheckPolicy, IrExpr, IrInit, IrOp, IrProgram,
    OrdinaryType, Program, SourceSpan, COMPILED_BUILTINS,
};

fn lower(items: &str) -> IrProgram {
    let source = format!("module verifier\ntheory T {{\n{items}\n}}\n");
    let program = Program::single(parse_module(&source).unwrap());
    lower_program(&program, CheckPolicy::research()).unwrap_or_else(|e| panic!("{source}\n{e:?}"))
}

fn issues(program: &IrProgram) -> String {
    verify_ir(program).iter().map(ToString::to_string).collect::<Vec<_>>().join(" | ")
}

fn valid(program: &IrProgram) {
    let text = issues(program);
    assert!(text.is_empty(), "{text}");
}

fn rejected(program: &IrProgram, context: &str) {
    assert!(!issues(program).is_empty(), "accepted corrupted IR: {context}");
}

fn change_builtin(node: &mut IrExpr, name: &str, change: &mut dyn FnMut(&mut IrExpr)) -> bool {
    if matches!(&node.op, IrOp::Builtin { name: n, .. } if n == name) {
        change(node);
        return true;
    }
    match &mut node.op {
        IrOp::Call { args, .. } | IrOp::Builtin { args, .. } => args.iter_mut().any(|a| change_builtin(a, name, change)),
        IrOp::Apply { callee, args } => change_builtin(callee, name, change) || args.iter_mut().any(|a| change_builtin(a, name, change)),
        IrOp::Add(a, b) | IrOp::Gt(a, b) | IrOp::Pow(a, b) | IrOp::And(a, b) | IrOp::Or(a, b) | IrOp::UnwrapOr(a, b) => {
            change_builtin(a, name, change) || change_builtin(b, name, change)
        }
        IrOp::If(a, b, c) | IrOp::Case(a, b, c) => {
            change_builtin(a, name, change) || change_builtin(b, name, change) || change_builtin(c, name, change)
        }
        IrOp::Let { value, body, .. } => change_builtin(value, name, change) || change_builtin(body, name, change),
        IrOp::Inject { value, .. } | IrOp::Field { record: value, .. } => change_builtin(value, name, change),
        IrOp::Record { fields, .. } => fields.iter_mut().any(|(_, v)| change_builtin(v, name, change)),
        _ => false,
    }
}

fn samples() -> Vec<(&'static str, &'static str, &'static str)> {
    vec![
        ("effect_fn", "effect_fn(FnEffect(Pure,Nat,Nat),fun(n:Nat,n))", "FnEffect<Pure,Nat,Nat>"),
        ("map_effect", "map_effect(list(1),fun(n:Nat,n))", "List<Nat>"),
        ("fold_effect", "fold_effect(list(1),0,fun(a:Nat,n:Nat,a+n))", "Nat"),
        ("each_effect", "each_effect(list(1),fun(n:Nat,n))", "Nat"),
        ("filter_effect", "filter_effect(list(1),fun(n:Nat,true))", "List<Nat>"),
        ("find_effect", "find_effect(list(1),fun(n:Nat,true))", "Option<Nat>"),
        ("any_effect", "any_effect(list(1),fun(n:Nat,true))", "Bool"),
        ("all_effect", "all_effect(list(1),fun(n:Nat,true))", "Bool"),
        ("try_map_effect", "try_map_effect(list(1),fun(n:Nat,ok(n,Text)))", "Result<List<Nat>,Text>"),
        ("try_each_effect", "try_each_effect(list(1),fun(n:Nat,ok(n,Text)))", "Result<Nat,Text>"),
        ("not", "not(true)", "Bool"),
        ("nat_eq", "nat_eq(1, 2)", "Bool"),
        ("mul", "mul(1, 2)", "Nat"),
        ("monus", "monus(1, 2)", "Nat"),
        ("pred", "pred(1)", "Nat"),
        ("some", "some(1)", "Option<Nat>"),
        ("ok", "ok(1, Text)", "Result<Nat,Text>"),
        ("err", "err(\"error\", Nat)", "Result<Nat,Text>"),
        ("is_some", "is_some(some(1))", "Bool"),
        ("is_none", "is_none(none(Nat))", "Bool"),
        ("is_ok", "is_ok(ok(1, Text))", "Bool"),
        ("is_err", "is_err(err(\"error\", Nat))", "Bool"),
        ("is_left", "is_left(inl(1, Text))", "Bool"),
        ("int", "int(1)", "Int"),
        ("abs", "abs(-1)", "Nat"),
        ("int_eq", "int_eq(-1, -2)", "Bool"),
        ("div", "div(-1, -2)", "Int"),
        ("mod", "mod(1, 2)", "Nat"),
        ("int_to_text", "int_to_text(-1)", "Text"),
        ("text_to_int", "text_to_int(\"-1\")", "Option<Int>"),
        ("__sub", "int(2) - int(1)", "Int"),
        ("__neg", "-int(1)", "Int"),
        ("list", "list(1, 2)", "List<Nat>"),
        ("sequence", "sequence(1, 2)", "Sequence<Nat>"),
        ("length", "length(list(1, 2))", "Nat"),
        ("index", "index(sequence(1, 2), 0)", "Option<Nat>"),
        ("head", "head(list(1, 2))", "Option<Nat>"),
        ("tail", "tail(sequence(1, 2))", "Sequence<Nat>"),
        ("is_empty", "is_empty(empty_list(Nat))", "Bool"),
        ("cons", "cons(1, empty_sequence(Nat))", "Sequence<Nat>"),
        ("__match_head", "match list(1) { [] => 0, cons(h, t) => h }", "Nat"),
        ("__match_some", "match some(1) { none => 0, some(n) => n }", "Nat"),
        ("__match_variant", "match A(1) { A(n) => n, B => 0 }", "Nat"),
        ("__enum_payload", "A_f0(A(1))", "Nat"),
        ("__match_ok", "match ok(1, Text) { ok(n) => n, err(e) => 0 }", "Nat"),
        ("__match_err", "match err(\"bad\", Nat) { ok(n) => \"ok\", err(e) => e }", "Text"),
        ("pair", "pair(1, true)", "Product<Nat,Bool>"),
        ("fst", "fst(pair(1, true))", "Nat"),
        ("snd", "snd(pair(1, true))", "Bool"),
        ("map", "map(sequence(1, 2), fun(n: Nat, nat_to_text(n)))", "Sequence<Text>"),
        ("fold", "fold(list(1, 2), 0, fun(a: Nat, n: Nat, a + n))", "Nat"),
        ("concat", "concat(\"a\", \"b\")", "Text"),
        ("text_length", "text_length(\"a\")", "Nat"),
        ("text_eq", "text_eq(\"a\", \"b\")", "Bool"),
        ("nat_to_text", "nat_to_text(1)", "Text"),
        ("text_to_nat", "text_to_nat(\"1\")", "Option<Nat>"),
        ("substring", "substring(\"abc\", 0, 1)", "Text"),
        ("lines", "lines(\"a\\nb\")", "List<Text>"),
        ("words", "words(\"a b\")", "List<Text>"),
        ("join", "join(list(\"a\", \"b\"), \",\")", "Text"),
        ("print_line", "print_line(\"a\")", "Text"),
        ("print_text", "print_text(\"a\")", "Text"),
        ("print_decimal", "print_decimal(1)", "Text"),
        ("print_symbolic", "print_symbolic(pair(1, true))", "Text"),
        ("read_line", "read_line()", "Option<Text>"),
        ("read_all", "read_all()", "Text"),
        ("read_nat", "read_nat()", "Nat"),
        ("args", "args()", "List<Text>"),
        // v0.102: owned byte buffers and the release inserted by lowering
        ("buf_new", "buf_new(2, 0)", "ByteBuf"),
        ("buf_from_list", "buf_from_list(list(1, 2))", "ByteBuf"),
        ("buf_len", "let b = buf_new(2, 0) in buf_len(b)", "Nat"),
        ("buf_get", "let b = buf_new(2, 0) in buf_get(b, 1)", "Option<Nat>"),
        ("buf_set", "buf_set(buf_new(2, 0), 1, 7)", "ByteBuf"),
        ("buf_push", "buf_push(buf_new(2, 0), 7)", "ByteBuf"),
        ("buf_to_list", "buf_to_list(buf_new(2, 0))", "List<Nat>"),
        ("buf_free", "buf_free(buf_new(2, 0))", "Nat"),
        ("__release", "let b = buf_new(2, 0) in buf_len(b)", "Nat"),
        ("buf_copy", "let b = buf_new(2, 0) in buf_copy(b)", "ByteBuf"),
        ("buf_slice", "let b = buf_new(2, 0) in buf_slice(b, 0, 1)", "ByteBuf"),
        ("buf_append", "let b = buf_new(2, 0) in buf_append(buf_new(1, 0), b)", "ByteBuf"),
        ("buf_eq", "let b = buf_new(2, 0) in buf_eq(b, b)", "Bool"),
        ("buf_from_text", "buf_from_text(\"ab\")", "ByteBuf"),
        ("buf_to_text", "let b = buf_new(2, 65) in buf_to_text(b)", "Option<Text>"),
        ("tbuf_new", "tbuf_new()", "TextBuf"),
        ("tbuf_push", "tbuf_push(tbuf_new(), \"a\")", "TextBuf"),
        ("tbuf_len", "let t = tbuf_new() in tbuf_len(t)", "Nat"),
        ("tbuf_finish", "tbuf_finish(tbuf_new())", "Text"),
        ("fl_of_nat", "fl_of_nat(2)", "Float"),
        ("fl_of_int", "fl_of_int(int(2))", "Float"),
        ("fl_lit", "fl_lit(\"1.5\")", "Float"),
        ("fl_parse", "fl_parse(\"1.5\")", "Option<Float>"),
        ("fl_add", "fl_add(fl_of_nat(2), fl_of_nat(2))", "Float"),
        ("fl_sub", "fl_sub(fl_of_nat(2), fl_of_nat(2))", "Float"),
        ("fl_mul", "fl_mul(fl_of_nat(2), fl_of_nat(2))", "Float"),
        ("fl_div", "fl_div(fl_of_nat(2), fl_of_nat(2))", "Float"),
        ("fl_neg", "fl_neg(fl_of_nat(2))", "Float"),
        ("fl_abs", "fl_abs(fl_of_nat(2))", "Float"),
        ("fl_min", "fl_min(fl_of_nat(2), fl_of_nat(2))", "Float"),
        ("fl_max", "fl_max(fl_of_nat(2), fl_of_nat(2))", "Float"),
        ("fl_sqrt", "fl_sqrt(fl_of_nat(2))", "Float"),
        ("fl_exp", "fl_exp(fl_of_nat(2))", "Float"),
        ("fl_log", "fl_log(fl_of_nat(2))", "Float"),
        ("fl_sin", "fl_sin(fl_of_nat(2))", "Float"),
        ("fl_cos", "fl_cos(fl_of_nat(2))", "Float"),
        ("fl_tan", "fl_tan(fl_of_nat(2))", "Float"),
        ("fl_atan", "fl_atan(fl_of_nat(2))", "Float"),
        ("fl_atan2", "fl_atan2(fl_of_nat(2), fl_of_nat(2))", "Float"),
        ("fl_pow", "fl_pow(fl_of_nat(2), fl_of_nat(2))", "Float"),
        ("fl_floor", "fl_floor(fl_of_nat(2))", "Float"),
        ("fl_ceil", "fl_ceil(fl_of_nat(2))", "Float"),
        ("fl_round", "fl_round(fl_of_nat(2))", "Float"),
        ("fl_trunc", "fl_trunc(fl_of_nat(2))", "Float"),
        ("fl_lt", "fl_lt(fl_of_nat(2), fl_of_nat(2))", "Bool"),
        ("fl_le", "fl_le(fl_of_nat(2), fl_of_nat(2))", "Bool"),
        ("fl_eq", "fl_eq(fl_of_nat(2), fl_of_nat(2))", "Bool"),
        ("fl_is_nan", "fl_is_nan(fl_of_nat(2))", "Bool"),
        ("fl_is_finite", "fl_is_finite(fl_of_nat(2))", "Bool"),
        ("fl_to_text", "fl_to_text(fl_of_nat(2))", "Text"),
        ("fl_to_int", "fl_to_int(fl_of_nat(2))", "Option<Int>"),
        ("fl_bits", "fl_bits(fl_of_nat(2))", "Nat"),
        ("fl_from_bits", "fl_from_bits(2)", "Float"),
        ("fv_new", "fv_new(2, fl_of_nat(2))", "Option<FloatVec>"),
        ("fv_from_list", "fv_from_list(list(fl_of_nat(1), fl_of_nat(2)))", "Option<FloatVec>"),
        ("fv_to_list", "fv_to_list(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()))", "List<Float>"),
        ("fv_len", "fv_len(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()))", "Nat"),
        ("fv_get", "fv_get(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), 2)", "Option<Float>"),
        ("fv_set", "fv_set(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), 2, fl_of_nat(2))", "Option<FloatVec>"),
        ("fv_add", "fv_add(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()))", "Option<FloatVec>"),
        ("fv_sub", "fv_sub(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()))", "Option<FloatVec>"),
        ("fv_mul", "fv_mul(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()))", "Option<FloatVec>"),
        ("fv_scale", "fv_scale(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), fl_of_nat(2))", "FloatVec"),
        ("fv_axpy", "fv_axpy(fl_of_nat(2), unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()))", "Option<FloatVec>"),
        ("fv_dot", "fv_dot(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()))", "Option<Float>"),
        ("fv_sum", "fv_sum(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()))", "Float"),
        ("fv_norm", "fv_norm(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()))", "Float"),
        ("fv_max", "fv_max(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()))", "Option<Float>"),
        ("fv_min", "fv_min(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()))", "Option<Float>"),
        ("fv_slice", "fv_slice(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), 2, 2)", "Option<FloatVec>"),
        ("fv_concat", "fv_concat(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()))", "Option<FloatVec>"),
        ("fv_matmul", "fv_matmul(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), 2, 2, 2)", "Option<FloatVec>"),
        ("fv_transpose", "fv_transpose(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), 2, 2)", "Option<FloatVec>"),
        ("fv_empty", "fv_empty()", "FloatVec"),
        ("fv_solve", "fv_solve(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), 2)", "Option<FloatVec>"),
        ("nv_empty", "nv_empty()", "NatVec"),
        ("nv_new", "nv_new(3, 1)", "Option<NatVec>"),
        ("nv_from_list", "nv_from_list(list(1, 2))", "Option<NatVec>"),
        ("nv_to_list", "nv_to_list(unwrap_or(nv_range(0, 3), nv_empty()))", "List<Nat>"),
        ("nv_len", "nv_len(unwrap_or(nv_range(0, 3), nv_empty()))", "Nat"),
        ("nv_get", "nv_get(unwrap_or(nv_range(0, 3), nv_empty()), 1)", "Option<Nat>"),
        ("nv_set", "nv_set(unwrap_or(nv_range(0, 3), nv_empty()), 1, 1)", "Option<NatVec>"),
        ("nv_push", "nv_push(unwrap_or(nv_range(0, 3), nv_empty()), 1)", "Option<NatVec>"),
        ("nv_slice", "nv_slice(unwrap_or(nv_range(0, 3), nv_empty()), 1, 1)", "Option<NatVec>"),
        ("nv_concat", "nv_concat(unwrap_or(nv_range(0, 3), nv_empty()), unwrap_or(nv_range(0, 3), nv_empty()))", "Option<NatVec>"),
        ("nv_range", "nv_range(3, 1)", "Option<NatVec>"),
        ("nv_sum", "nv_sum(unwrap_or(nv_range(0, 3), nv_empty()))", "Option<Nat>"),
        ("nv_max", "nv_max(unwrap_or(nv_range(0, 3), nv_empty()))", "Option<Nat>"),
        ("nv_min", "nv_min(unwrap_or(nv_range(0, 3), nv_empty()))", "Option<Nat>"),
        ("nv_sort", "nv_sort(unwrap_or(nv_range(0, 3), nv_empty()))", "NatVec"),
        ("nv_reverse", "nv_reverse(unwrap_or(nv_range(0, 3), nv_empty()))", "NatVec"),
        ("nv_find", "nv_find(unwrap_or(nv_range(0, 3), nv_empty()), 1)", "Option<Nat>"),
        ("nv_to_fv", "nv_to_fv(unwrap_or(nv_range(0, 3), nv_empty()))", "FloatVec"),
        ("gr_bfs", "gr_bfs(3, unwrap_or(nv_range(0, 3), nv_empty()), unwrap_or(nv_range(0, 3), nv_empty()), 0)", "Option<NatVec>"),
        ("gr_dfs", "gr_dfs(3, unwrap_or(nv_range(0, 3), nv_empty()), unwrap_or(nv_range(0, 3), nv_empty()), 0)", "Option<NatVec>"),
        ("gr_toposort", "gr_toposort(3, unwrap_or(nv_range(0, 3), nv_empty()), unwrap_or(nv_range(0, 3), nv_empty()))", "Option<NatVec>"),
        ("gr_components", "gr_components(3, unwrap_or(nv_range(0, 3), nv_empty()), unwrap_or(nv_range(0, 3), nv_empty()))", "Option<NatVec>"),
        ("gr_scc", "gr_scc(3, unwrap_or(nv_range(0, 3), nv_empty()), unwrap_or(nv_range(0, 3), nv_empty()))", "Option<NatVec>"),
        ("gr_bipartite", "gr_bipartite(3, unwrap_or(nv_range(0, 3), nv_empty()), unwrap_or(nv_range(0, 3), nv_empty()))", "Option<NatVec>"),
        ("gr_dijkstra", "gr_dijkstra(3, unwrap_or(nv_range(0, 3), nv_empty()), unwrap_or(nv_range(0, 3), nv_empty()), unwrap_or(fv_new(3, fl_of_nat(1)), fv_empty()), 0)", "Option<FloatVec>"),
        ("gr_bellman_ford", "gr_bellman_ford(3, unwrap_or(nv_range(0, 3), nv_empty()), unwrap_or(nv_range(0, 3), nv_empty()), unwrap_or(fv_new(3, fl_of_nat(1)), fv_empty()), 0)", "Option<FloatVec>"),
        ("gr_floyd", "gr_floyd(3, unwrap_or(nv_range(0, 3), nv_empty()), unwrap_or(nv_range(0, 3), nv_empty()), unwrap_or(fv_new(3, fl_of_nat(1)), fv_empty()))", "Option<FloatVec>"),
        ("gr_mst", "gr_mst(3, unwrap_or(nv_range(0, 3), nv_empty()), unwrap_or(nv_range(0, 3), nv_empty()), unwrap_or(fv_new(3, fl_of_nat(1)), fv_empty()))", "Option<NatVec>"),
        ("gr_maxflow", "gr_maxflow(3, unwrap_or(nv_range(0, 3), nv_empty()), unwrap_or(nv_range(0, 3), nv_empty()), unwrap_or(nv_range(0, 3), nv_empty()), 0, 0)", "Option<Nat>"),
        ("la_lu", "la_lu(unwrap_or(fv_from_list(list(fl_of_nat(4), fl_of_nat(1), fl_of_nat(1), fl_of_nat(3))), fv_empty()), 2)", "Option<FloatVec>"),
        ("la_lu_perm", "la_lu_perm(unwrap_or(fv_from_list(list(fl_of_nat(4), fl_of_nat(1), fl_of_nat(1), fl_of_nat(3))), fv_empty()), 2)", "Option<NatVec>"),
        ("la_det", "la_det(unwrap_or(fv_from_list(list(fl_of_nat(4), fl_of_nat(1), fl_of_nat(1), fl_of_nat(3))), fv_empty()), 2)", "Option<Float>"),
        ("la_inverse", "la_inverse(unwrap_or(fv_from_list(list(fl_of_nat(4), fl_of_nat(1), fl_of_nat(1), fl_of_nat(3))), fv_empty()), 2)", "Option<FloatVec>"),
        ("la_cholesky", "la_cholesky(unwrap_or(fv_from_list(list(fl_of_nat(4), fl_of_nat(1), fl_of_nat(1), fl_of_nat(3))), fv_empty()), 2)", "Option<FloatVec>"),
        ("la_qr_q", "la_qr_q(unwrap_or(fv_from_list(list(fl_of_nat(4), fl_of_nat(1), fl_of_nat(1), fl_of_nat(3))), fv_empty()), 2, 2)", "Option<FloatVec>"),
        ("la_qr_r", "la_qr_r(unwrap_or(fv_from_list(list(fl_of_nat(4), fl_of_nat(1), fl_of_nat(1), fl_of_nat(3))), fv_empty()), 2, 2)", "Option<FloatVec>"),
        ("la_lstsq", "la_lstsq(unwrap_or(fv_from_list(list(fl_of_nat(4), fl_of_nat(1), fl_of_nat(1), fl_of_nat(3))), fv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), 2, 2)", "Option<FloatVec>"),
        ("la_eigh_values", "la_eigh_values(unwrap_or(fv_from_list(list(fl_of_nat(4), fl_of_nat(1), fl_of_nat(1), fl_of_nat(3))), fv_empty()), 2)", "Option<FloatVec>"),
        ("la_eigh_vectors", "la_eigh_vectors(unwrap_or(fv_from_list(list(fl_of_nat(4), fl_of_nat(1), fl_of_nat(1), fl_of_nat(3))), fv_empty()), 2)", "Option<FloatVec>"),
        ("la_svd_values", "la_svd_values(unwrap_or(fv_from_list(list(fl_of_nat(4), fl_of_nat(1), fl_of_nat(1), fl_of_nat(3))), fv_empty()), 2, 2)", "Option<FloatVec>"),
        ("la_rank", "la_rank(unwrap_or(fv_from_list(list(fl_of_nat(4), fl_of_nat(1), fl_of_nat(1), fl_of_nat(3))), fv_empty()), 2, 2, fl_of_nat(0))", "Option<Nat>"),
        ("la_identity", "la_identity(3)", "Option<FloatVec>"),
        ("la_trace", "la_trace(unwrap_or(fv_from_list(list(fl_of_nat(4), fl_of_nat(1), fl_of_nat(1), fl_of_nat(3))), fv_empty()), 2)", "Option<Float>"),
        ("la_csr_matvec", "la_csr_matvec(unwrap_or(nv_from_list(list(0, 1, 2)), nv_empty()), unwrap_or(nv_from_list(list(0, 1)), nv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()))", "Option<FloatVec>"),
        ("la_cg", "la_cg(unwrap_or(nv_from_list(list(0, 1, 2)), nv_empty()), unwrap_or(nv_from_list(list(0, 1)), nv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), fl_of_nat(0), 10)", "Option<FloatVec>"),
        ("fft_forward", "fft_forward(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()))", "Option<FloatVec>"),
        ("fft_inverse", "fft_inverse(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()))", "Option<FloatVec>"),
        ("fft_convolve", "fft_convolve(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()))", "Option<FloatVec>"),
        ("lp_solve", "lp_solve(unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), 2, 2)", "Option<FloatVec>"),
        ("lp_status", "lp_status(unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), 2, 2)", "Nat"),
        ("nt_gcd", "nt_gcd(12, 7)", "Nat"),
        ("nt_lcm", "nt_lcm(12, 7)", "Option<Nat>"),
        ("nt_mod_mul", "nt_mod_mul(12, 7, 5)", "Option<Nat>"),
        ("nt_mod_pow", "nt_mod_pow(12, 7, 5)", "Option<Nat>"),
        ("nt_mod_inv", "nt_mod_inv(12, 7)", "Option<Nat>"),
        ("nt_is_prime", "nt_is_prime(12)", "Bool"),
        ("nt_next_prime", "nt_next_prime(12)", "Option<Nat>"),
        ("nt_factor", "nt_factor(12)", "Option<NatVec>"),
        ("nt_totient", "nt_totient(12)", "Nat"),
        ("nt_divisors", "nt_divisors(12)", "Option<NatVec>"),
        ("nt_primes", "nt_primes(12)", "Option<NatVec>"),
        ("nt_crt", "nt_crt(unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()))", "Option<Nat>"),
        ("nt_sqrt_mod", "nt_sqrt_mod(12, 7)", "Option<Nat>"),
        ("nt_isqrt", "nt_isqrt(12)", "Nat"),
        ("pf_add", "pf_add(unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), 5)", "Option<NatVec>"),
        ("pf_sub", "pf_sub(unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), 5)", "Option<NatVec>"),
        ("pf_mul", "pf_mul(unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), 5)", "Option<NatVec>"),
        ("pf_div", "pf_div(unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), 5)", "Option<NatVec>"),
        ("pf_rem", "pf_rem(unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), 5)", "Option<NatVec>"),
        ("pf_gcd", "pf_gcd(unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), 5)", "Option<NatVec>"),
        ("pf_powmod", "pf_powmod(unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), 3, unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), 5)", "Option<NatVec>"),
        ("pf_eval", "pf_eval(unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), 3, 5)", "Option<Nat>"),
        ("pf_irreducible", "pf_irreducible(unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), 5)", "Option<Bool>"),
        ("pm_compose", "pm_compose(unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()))", "Option<NatVec>"),
        ("pm_inverse", "pm_inverse(unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()))", "Option<NatVec>"),
        ("pm_power", "pm_power(unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), 7)", "Option<NatVec>"),
        ("pm_order", "pm_order(unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()))", "Option<Nat>"),
        ("pm_sign", "pm_sign(unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()))", "Option<Nat>"),
        ("pm_cycle_type", "pm_cycle_type(unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()))", "Option<NatVec>"),
        ("pm_orbit", "pm_orbit(unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), 3, 0)", "Option<NatVec>"),
        ("pm_group_order", "pm_group_order(unwrap_or(nv_from_list(list(1, 2, 0)), nv_empty()), 3)", "Option<Nat>"),
        ("sat_solve", "sat_solve(unwrap_or(nv_from_list(list(0, 2, 1)), nv_empty()), unwrap_or(nv_from_list(list(2, 3)), nv_empty()), 2)", "Option<NatVec>"),
        ("sat_status", "sat_status(unwrap_or(nv_from_list(list(0, 2, 1)), nv_empty()), unwrap_or(nv_from_list(list(2, 3)), nv_empty()), 2)", "Nat"),
        ("tp_homology", "tp_homology(unwrap_or(nv_from_list(list(0, 2, 1)), nv_empty()), unwrap_or(nv_from_list(list(2, 3)), nv_empty()))", "Option<NatVec>"),
        ("tp_rips", "tp_rips(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), 2, 1, fl_of_nat(2))", "Option<FloatVec>"),
        ("gb_basis", "gb_basis(unwrap_or(nv_from_list(list(1, 2, 0, 6, 0, 1)), nv_empty()), unwrap_or(nv_from_list(list(6)), nv_empty()), 2, 7, 0)", "Option<NatVec>"),
        ("gb_reduce", "gb_reduce(unwrap_or(nv_from_list(list(1, 2, 0, 6, 0, 1)), nv_empty()), unwrap_or(nv_from_list(list(1, 2, 0, 6, 0, 1)), nv_empty()), unwrap_or(nv_from_list(list(6)), nv_empty()), 2, 7, 0)", "Option<NatVec>"),
        ("mv_add", "mv_add(unwrap_or(nv_from_list(list(1, 2, 0, 6, 0, 1)), nv_empty()), unwrap_or(nv_from_list(list(1, 2, 0, 6, 0, 1)), nv_empty()), 2, 7)", "Option<NatVec>"),
        ("mv_sub", "mv_sub(unwrap_or(nv_from_list(list(1, 2, 0, 6, 0, 1)), nv_empty()), unwrap_or(nv_from_list(list(1, 2, 0, 6, 0, 1)), nv_empty()), 2, 7)", "Option<NatVec>"),
        ("mv_mul", "mv_mul(unwrap_or(nv_from_list(list(1, 2, 0, 6, 0, 1)), nv_empty()), unwrap_or(nv_from_list(list(1, 2, 0, 6, 0, 1)), nv_empty()), 2, 7)", "Option<NatVec>"),
        ("mv_normalize", "mv_normalize(unwrap_or(nv_from_list(list(1, 2, 0, 6, 0, 1)), nv_empty()), 2, 7, 1)", "Option<NatVec>"),
        ("sat_proof", "sat_proof(unwrap_or(nv_from_list(list(0, 1)), nv_empty()), unwrap_or(nv_from_list(list(1, 2)), nv_empty()), 1)", "Option<NatVec>"),
        ("sat_check", "sat_check(unwrap_or(nv_from_list(list(0, 1)), nv_empty()), unwrap_or(nv_from_list(list(1, 2)), nv_empty()), 1, unwrap_or(nv_from_list(list(0)), nv_empty()))", "Option<Bool>"),
        ("sat_count", "sat_count(unwrap_or(nv_from_list(list(0, 1)), nv_empty()), unwrap_or(nv_from_list(list(1, 2)), nv_empty()), 1)", "Option<Nat>"),
        ("ilp_solve", "ilp_solve(unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), 2, 2, unwrap_or(nv_new(2, 1), nv_empty()))", "Option<FloatVec>"),
        ("ilp_status", "ilp_status(unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), 2, 2, unwrap_or(nv_new(2, 1), nv_empty()))", "Nat"),
        ("km_empty", "km_empty()", "NatMap"),
        ("km_insert", "km_insert(unwrap_or(km_insert(km_empty(), 1, 2), km_empty()), 2, 3)", "Option<NatMap>"),
        ("km_get", "km_get(unwrap_or(km_insert(km_empty(), 1, 2), km_empty()), 2)", "Option<Nat>"),
        ("km_remove", "km_remove(unwrap_or(km_insert(km_empty(), 1, 2), km_empty()), 2)", "NatMap"),
        ("km_contains", "km_contains(unwrap_or(km_insert(km_empty(), 1, 2), km_empty()), 2)", "Bool"),
        ("km_len", "km_len(unwrap_or(km_insert(km_empty(), 1, 2), km_empty()))", "Nat"),
        ("km_keys", "km_keys(unwrap_or(km_insert(km_empty(), 1, 2), km_empty()))", "NatVec"),
        ("km_values", "km_values(unwrap_or(km_insert(km_empty(), 1, 2), km_empty()))", "NatVec"),
        ("km_from", "km_from(unwrap_or(nv_from_list(list(1, 2)), nv_empty()), unwrap_or(nv_from_list(list(1, 2)), nv_empty()))", "Option<NatMap>"),
        ("km_add", "km_add(unwrap_or(km_insert(km_empty(), 1, 2), km_empty()), 2, 3)", "Option<NatMap>"),
        ("km_min", "km_min(unwrap_or(km_insert(km_empty(), 1, 2), km_empty()))", "Option<Nat>"),
        ("km_max", "km_max(unwrap_or(km_insert(km_empty(), 1, 2), km_empty()))", "Option<Nat>"),
        ("km_floor", "km_floor(unwrap_or(km_insert(km_empty(), 1, 2), km_empty()), 2)", "Option<Nat>"),
        ("km_ceil", "km_ceil(unwrap_or(km_insert(km_empty(), 1, 2), km_empty()), 2)", "Option<Nat>"),
        ("km_merge", "km_merge(unwrap_or(km_insert(km_empty(), 1, 2), km_empty()), unwrap_or(km_insert(km_empty(), 1, 2), km_empty()))", "Option<NatMap>"),
        ("km_range", "km_range(unwrap_or(km_insert(km_empty(), 1, 2), km_empty()), 2, 3)", "NatMap"),
        ("tm_empty", "tm_empty()", "TextMap"),
        ("tm_insert", "tm_insert(unwrap_or(tm_insert(tm_empty(), \"a\", 2), tm_empty()), \"a\", 3)", "Option<TextMap>"),
        ("tm_get", "tm_get(unwrap_or(tm_insert(tm_empty(), \"a\", 2), tm_empty()), \"a\")", "Option<Nat>"),
        ("tm_remove", "tm_remove(unwrap_or(tm_insert(tm_empty(), \"a\", 2), tm_empty()), \"a\")", "TextMap"),
        ("tm_contains", "tm_contains(unwrap_or(tm_insert(tm_empty(), \"a\", 2), tm_empty()), \"a\")", "Bool"),
        ("tm_len", "tm_len(unwrap_or(tm_insert(tm_empty(), \"a\", 2), tm_empty()))", "Nat"),
        ("tm_keys", "tm_keys(unwrap_or(tm_insert(tm_empty(), \"a\", 2), tm_empty()))", "List<Text>"),
        ("tm_values", "tm_values(unwrap_or(tm_insert(tm_empty(), \"a\", 2), tm_empty()))", "NatVec"),
        ("tm_from", "tm_from(list(\"a\", \"b\"), unwrap_or(nv_from_list(list(1, 2)), nv_empty()))", "Option<TextMap>"),
        ("tm_add", "tm_add(unwrap_or(tm_insert(tm_empty(), \"a\", 2), tm_empty()), \"a\", 3)", "Option<TextMap>"),
        ("tm_merge", "tm_merge(unwrap_or(tm_insert(tm_empty(), \"a\", 2), tm_empty()), unwrap_or(tm_insert(tm_empty(), \"a\", 2), tm_empty()))", "Option<TextMap>"),
        ("tm_count_words", "tm_count_words(\"a\")", "TextMap"),
        ("la_pcg", "la_pcg(unwrap_or(nv_from_list(list(0, 1, 2)), nv_empty()), unwrap_or(nv_from_list(list(0, 1)), nv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), fl_of_nat(0), 10)", "Option<FloatVec>"),
        ("la_bicgstab", "la_bicgstab(unwrap_or(nv_from_list(list(0, 1, 2)), nv_empty()), unwrap_or(nv_from_list(list(0, 1)), nv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), fl_of_nat(0), 10)", "Option<FloatVec>"),
        ("la_gmres", "la_gmres(unwrap_or(nv_from_list(list(0, 1, 2)), nv_empty()), unwrap_or(nv_from_list(list(0, 1)), nv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), fl_of_nat(0), 2, 10)", "Option<FloatVec>"),
        ("geo_orient2d", "geo_orient2d(unwrap_or(fv_new(6, fl_of_nat(1)), fv_empty()))", "Option<Nat>"),
        ("geo_incircle", "geo_incircle(unwrap_or(fv_new(8, fl_of_nat(1)), fv_empty()))", "Option<Nat>"),
        ("geo_orient3d", "geo_orient3d(unwrap_or(fv_new(12, fl_of_nat(1)), fv_empty()))", "Option<Nat>"),
        ("geo_delaunay", "geo_delaunay(unwrap_or(fv_new(6, fl_of_nat(1)), fv_empty()))", "Option<NatVec>"),
        ("kd_knn", "kd_knn(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), 2, 1)", "Option<NatVec>"),
        ("kd_radius", "kd_radius(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), 2, unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), fl_of_nat(1))", "Option<NatVec>"),
        ("fft2_forward", "fft2_forward(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), 1, 2)", "Option<FloatVec>"),
        ("fft2_inverse", "fft2_inverse(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), 1, 2)", "Option<FloatVec>"),
        ("sig_stft", "sig_stft(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), 1)", "Option<FloatVec>"),
        ("sig_dwt", "sig_dwt(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), 1, 0)", "Option<FloatVec>"),
        ("sig_idwt", "sig_idwt(unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()), 1, 0)", "Option<FloatVec>"),
        ("sig_iir", "sig_iir(unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(2, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(4, fl_of_nat(1)), fv_empty()))", "Option<FloatVec>"),
        ("sig_sha256", "sig_sha256(\"abc\")", "Text"),
        ("bvh_raycast", "bvh_raycast(unwrap_or(fv_new(9, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(3, fl_of_nat(0)), fv_empty()), unwrap_or(fv_new(3, fl_of_nat(1)), fv_empty()))", "Option<FloatVec>"),
        ("bvh_winding", "bvh_winding(unwrap_or(fv_new(9, fl_of_nat(1)), fv_empty()), unwrap_or(fv_new(3, fl_of_nat(0)), fv_empty()))", "Option<FloatVec>"),
        ("bvh_pairs", "bvh_pairs(unwrap_or(fv_new(6, fl_of_nat(1)), fv_empty()))", "Option<NatVec>"),
        ("equal", "equal(list(1, 2), list(1, 2))", "Bool"),
        ("less", "less(\"a\", \"b\")", "Bool"),
        ("file_open", "file_open(\"/nonexistent/x\", \"r\")", "File"),
        ("file_ok", "let f = file_open(\"/nonexistent/x\", \"r\") in let v = file_ok(f) in let c = file_close(f) in v", "Bool"),
        ("file_error", "let f = file_open(\"/nonexistent/x\", \"r\") in let v = file_error(f) in let c = file_close(f) in v", "Text"),
        ("file_write", "let f = file_open(\"/nonexistent/x\", \"w\") in let v = file_write(f, \"a\") in let c = file_close(f) in v", "Bool"),
        ("file_write_bytes", "let f = file_open(\"/nonexistent/x\", \"w\") in let b = buf_new(1, 0) in let v = file_write_bytes(f, b) in let c = file_close(f) in v", "Bool"),
        ("file_read_line", "let f = file_open(\"/nonexistent/x\", \"r\") in let v = file_read_line(f) in let c = file_close(f) in v", "Option<Text>"),
        ("file_read_all", "let f = file_open(\"/nonexistent/x\", \"r\") in let v = file_read_all(f) in let c = file_close(f) in v", "Text"),
        ("file_read_bytes", "let f = file_open(\"/nonexistent/x\", \"r\") in let v = file_read_bytes(f, 4) in let c = file_close(f) in buf_free(v)", "Nat"),
        ("file_flush", "let f = file_open(\"/nonexistent/x\", \"w\") in let v = file_flush(f) in let c = file_close(f) in v", "Bool"),
        ("file_close", "file_close(file_open(\"/nonexistent/x\", \"r\"))", "Result<Nat,Text>"),
        ("file_remove", "file_remove(\"/nonexistent/x\")", "Result<Nat,Text>"),
        ("file_set_buffered", "let f = file_open(\"/nonexistent/x\", \"w\") in let v = file_set_buffered(f, true) in let c = file_close(f) in v", "Bool"),
        ("file_seek", "let f = file_open(\"/nonexistent/x\", \"w\") in let v = file_seek(f, 0) in let c = file_close(f) in v", "Bool"),
        ("file_tell", "let f = file_open(\"/nonexistent/x\", \"w\") in let v = file_tell(f) in let c = file_close(f) in v", "Nat"),
        ("file_size", "let f = file_open(\"/nonexistent/x\", \"w\") in let v = file_size(f) in let c = file_close(f) in v", "Nat"),
        ("file_truncate", "let f = file_open(\"/nonexistent/x\", \"w\") in let v = file_truncate(f, 0) in let c = file_close(f) in v", "Bool"),
        ("file_exists", "file_exists(\"/nonexistent/x\")", "Bool"),
        ("file_is_dir", "file_is_dir(\"/nonexistent/x\")", "Bool"),
        ("file_size_of", "file_size_of(\"/nonexistent/x\")", "Result<Nat,Text>"),
        ("file_mtime", "file_mtime(\"/nonexistent/x\")", "Result<Nat,Text>"),
        ("file_list_dir", "file_list_dir(\"/nonexistent/x\")", "Result<List<Text>,Text>"),
        ("file_walk", "file_walk(\"/nonexistent/x\")", "Result<List<Text>,Text>"),
        ("file_make_dir", "file_make_dir(\"/nonexistent/x\")", "Result<Nat,Text>"),
        ("file_remove_dir", "file_remove_dir(\"/nonexistent/x\")", "Result<Nat,Text>"),
        ("file_rename", "file_rename(\"/nonexistent/x\", \"/nonexistent/y\")", "Result<Nat,Text>"),
        ("file_copy", "file_copy(\"/nonexistent/x\", \"/nonexistent/y\")", "Result<Nat,Text>"),
        ("proc_run", "proc_run(\"/nonexistent/x\", empty_list(Text), \"\")", "Result<List<Text>,Text>"),
        ("proc_env", "proc_env(\"DLM_NONE\")", "Option<Text>"),
        ("net_resolve", "net_resolve(\"localhost\")", "Result<List<Text>,Text>"),
        ("net_listen", "net_listen(\"127.0.0.1\", 0)", "Result<Nat,Text>"),
        ("net_local_port", "net_local_port(0)", "Result<Nat,Text>"),
        ("net_accept", "net_accept(0, 1)", "Result<Nat,Text>"),
        ("net_connect", "net_connect(\"127.0.0.1\", 1, 1)", "Result<Nat,Text>"),
        ("net_send", "net_send(0, \"x\")", "Result<Nat,Text>"),
        ("net_recv_line", "net_recv_line(0)", "Result<Option<Text>,Text>"),
        ("net_recv_all", "net_recv_all(0)", "Result<Text,Text>"),
        ("net_shutdown", "net_shutdown(0)", "Result<Nat,Text>"),
        ("net_close", "net_close(0)", "Result<Nat,Text>"),
        ("proc_spawn", "proc_spawn(\"/nonexistent/x\", empty_list(Text), \"\")", "Result<Nat,Text>"),
        ("proc_poll", "proc_poll(0)", "Result<Option<Nat>,Text>"),
        ("proc_wait", "proc_wait(0)", "Result<List<Text>,Text>"),
        ("proc_kill", "proc_kill(0)", "Result<Nat,Text>"),
        ("gx_new", "gx_new(2, 2, 255)", "Option<NatVec>"),
        ("gx_width", "gx_width(unwrap_or(gx_new(2, 2, 255), nv_empty()))", "Nat"),
        ("gx_height", "gx_height(unwrap_or(gx_new(2, 2, 255), nv_empty()))", "Nat"),
        ("gx_get", "gx_get(unwrap_or(gx_new(2, 2, 255), nv_empty()), 0, 0)", "Option<Nat>"),
        ("gx_fill_rect", "gx_fill_rect(unwrap_or(gx_new(2, 2, 255), nv_empty()), 0, 0, 1, 1, 255)", "NatVec"),
        ("gx_line", "gx_line(unwrap_or(gx_new(2, 2, 255), nv_empty()), 0, 0, 1, 1, 255)", "NatVec"),
        ("gx_circle", "gx_circle(unwrap_or(gx_new(2, 2, 255), nv_empty()), 1, 1, 1, 255, true)", "NatVec"),
        ("gx_text", "gx_text(unwrap_or(gx_new(2, 2, 255), nv_empty()), 0, 0, \"a\", 255, 1)", "NatVec"),
        ("gx_text_width", "gx_text_width(\"ab\", 1)", "Nat"),
        ("gx_blit", "gx_blit(unwrap_or(gx_new(2, 2, 255), nv_empty()), unwrap_or(gx_new(2, 2, 255), nv_empty()), 0, 0)", "NatVec"),
        ("gx_checksum", "gx_checksum(unwrap_or(gx_new(2, 2, 255), nv_empty()))", "Nat"),
        ("file_write_png", "file_write_png(\"/nonexistent/x.png\", unwrap_or(gx_new(2, 2, 255), nv_empty()))", "Result<Nat,Text>"),
        ("vx_render", "vx_render(unwrap_or(nv_from_list(list(3, 0, 0, 16, 16, 4, 0, 4294967295)), nv_empty()), 2, 2, 1, 1)", "Option<NatVec>"),
        ("vx_render_region", "vx_render_region(unwrap_or(gx_new(2, 2, 255), nv_empty()), unwrap_or(nv_from_list(list(3, 0, 0, 16, 16, 4, 0, 4294967295)), nv_empty()), 1, 1, 0, 0, 1, 1)", "Option<NatVec>"),
        ("vx_bounds", "vx_bounds(unwrap_or(nv_from_list(list(3, 0, 0, 16, 16, 4, 0, 4294967295)), nv_empty()), 1, 1)", "Option<NatVec>"),
        ("vx_text_width", "vx_text_width(\"ab\", 128)", "Nat"),
        ("vx_svg", "vx_svg(unwrap_or(nv_from_list(list(3, 0, 0, 16, 16, 4, 0, 4294967295)), nv_empty()), 2, 2)", "Option<Text>"),
        ("vx_text", "vx_text(0, 0, 128, 16, unwrap_or(nv_from_list(list(0, 255)), nv_empty()), \"ab\")", "NatVec"),
        ("vx_text_fonts", "vx_text_fonts(0, 0, 128, 0, nv_empty(), unwrap_or(nv_from_list(list(0, 255)), nv_empty()), \"ab\")", "NatVec"),
        ("vx_font_width", "vx_font_width(nv_empty(), \"ab\", 128)", "Nat"),
        ("vx_font_has", "vx_font_has(0, 65)", "Bool"),
        ("vx_font_advance", "vx_font_advance(0, 65, 128)", "Nat"),
        ("vx_font_metrics", "vx_font_metrics(0)", "NatVec"),
        ("vx_codepoints", "vx_codepoints(\"ab\")", "NatVec"),
        ("vx_from_codepoints", "vx_from_codepoints(nv_empty())", "Text"),
        ("file_font_load", "file_font_load(\"/nonexistent/x.ttf\")", "Result<Nat,Text>"),
        ("file_font_find", "file_font_find(\"nonexistent-font\")", "Result<Text,Text>"),
        ("win_child", "win_child(0, \"w\", 0, 0)", "Result<Nat,Text>"),
        ("win_attach", "win_attach(0, 0, 0)", "Result<Nat,Text>"),
        ("win_resize", "win_resize(0, 0, 0)", "Result<Nat,Text>"),
        ("win_focus", "win_focus(0)", "Result<Nat,Text>"),
        ("win_dpi", "win_dpi(0)", "Result<Float,Text>"),
        ("win_open", "win_open(\"w\", 0, 0)", "Result<Nat,Text>"),
        ("win_present", "win_present(0, nv_empty())", "Result<Nat,Text>"),
        ("win_poll", "win_poll(0, 0)", "Result<NatVec,Text>"),
        ("win_size", "win_size(0)", "Result<NatVec,Text>"),
        ("win_close", "win_close(0)", "Result<Nat,Text>"),
        ("win_gpu", "win_gpu(0)", "Result<Text,Text>"),
        ("win_present_scene", "win_present_scene(0, nv_empty(), 1, 1, 1, 1)", "Result<Nat,Text>"),
        ("win_read", "win_read(0)", "Result<NatVec,Text>"),
        ("file_write_atomic", "file_write_atomic(\"/nonexistent/x\", \"/nonexistent/y\")", "Result<Nat,Text>"),
        ("bn_from_nat", "bn_from_nat(5)", "BigNat"),
        ("bn_to_nat", "bn_to_nat(bn_from_nat(5))", "Option<Nat>"),
        ("bn_from_words", "bn_from_words(list(1, 2))", "Option<BigNat>"),
        ("bn_to_words", "bn_to_words(bn_from_nat(5))", "List<Nat>"),
        ("bn_from_text", "bn_from_text(\"12\")", "Option<BigNat>"),
        ("bn_to_text", "bn_to_text(bn_from_nat(5))", "Text"),
        ("bn_bits", "bn_bits(bn_from_nat(5))", "Nat"),
        ("bn_limbs", "bn_limbs(bn_from_nat(5))", "Nat"),
        ("bn_cmp", "bn_cmp(bn_from_nat(5), bn_from_nat(6))", "Int"),
        ("bn_add", "bn_add(bn_from_nat(5), bn_from_nat(6))", "Option<BigNat>"),
        ("bn_monus", "bn_monus(bn_from_nat(5), bn_from_nat(6))", "BigNat"),
        ("bn_mul", "bn_mul(bn_from_nat(5), bn_from_nat(6))", "Option<BigNat>"),
        ("bn_div", "bn_div(bn_from_nat(5), bn_from_nat(6))", "Option<BigNat>"),
        ("bn_mod", "bn_mod(bn_from_nat(5), bn_from_nat(6))", "Option<BigNat>"),
        ("bn_shl", "bn_shl(bn_from_nat(5), 3)", "Option<BigNat>"),
        ("bn_shr", "bn_shr(bn_from_nat(5), 1)", "BigNat"),
        ("bn_gcd", "bn_gcd(bn_from_nat(4), bn_from_nat(6))", "BigNat"),
        ("bn_sqrt", "bn_sqrt(bn_from_nat(17))", "BigNat"),
        ("bn_powmod", "bn_powmod(bn_from_nat(3), bn_from_nat(5), bn_from_nat(7))", "Option<BigNat>"),
        ("bn_modinv", "bn_modinv(bn_from_nat(3), bn_from_nat(7))", "Option<BigNat>"),
        ("bn_is_prime", "bn_is_prime(bn_from_nat(97))", "Bool"),
        ("bn_next_prime", "bn_next_prime(bn_from_nat(97))", "Option<BigNat>"),
        ("bn_random", "bn_random(1, 64)", "Option<BigNat>"),
        ("bn_to_hex", "bn_to_hex(bn_from_nat(255))", "Text"),
        ("bn_from_hex", "bn_from_hex(\"ff\")", "Option<BigNat>"),
    ]
}

#[test]
fn every_compiled_builtin_rejects_wrong_results_operands_and_runtime_arity() {
    let cases = samples();
    let names: std::collections::BTreeSet<_> = cases.iter().map(|(name, _, _)| *name).collect();
    assert_eq!(names, COMPILED_BUILTINS.iter().map(|(name, _)| *name).collect());
    for (name, expression, ret) in cases {
        // v0.125: enum patterns need an enum declaration
        let prefix = if matches!(name, "__match_variant" | "__enum_payload") { "    enum E { A(n: Nat), B }\n" } else { "" };
        let program = lower(&format!("{prefix}    fn probe() -> {ret} = {expression}"));
        valid(&program);
        let symbol = if name == "__enum_payload" { "T.A_f0" } else { "T.probe" };
        let id = program.functions.iter().position(|f| f.name == symbol).unwrap();
        let mut arity = 0;
        let mut broken = program.clone();
        assert!(change_builtin(&mut broken.functions[id].body, name, &mut |node| {
            node.ty = Some(if node.ty == Some(OrdinaryType::Bool) { OrdinaryType::Nat } else { OrdinaryType::Bool });
            if let IrOp::Builtin { args, .. } = &node.op { arity = args.len(); }
        }));
        // Keep the enclosing signature consistent when the builtin is the
        // body: rejection must come from the operation's own type rule.
        broken.functions[id].ret = broken.functions[id].body.ty.clone();
        rejected(&broken, &format!("{name}: result"));
        for index in 0..arity {
            // print_symbolic accepts every ordinary value; its argument has
            // no narrower type constraint. All other operands are constrained.
            if name == "print_symbolic" { continue; }
            let mut broken = program.clone();
            change_builtin(&mut broken.functions[id].body, name, &mut |node| {
                if let IrOp::Builtin { args, .. } = &mut node.op {
                    let arg = &mut args[index];
                    *arg = if arg.ty == Some(OrdinaryType::Bool) {
                        IrExpr::new(IrOp::Nat(0), Some(OrdinaryType::Nat), arg.span)
                    } else {
                        IrExpr::new(IrOp::Bool(false), Some(OrdinaryType::Bool), arg.span)
                    };
                }
            });
            rejected(&broken, &format!("{name}: operand {index}"));
        }
        let mut broken = program.clone();
        change_builtin(&mut broken.functions[id].body, name, &mut |node| {
            if let IrOp::Builtin { args, .. } = &mut node.op {
                if arity == 0 {
                    args.push(IrExpr::new(IrOp::Nat(0), Some(OrdinaryType::Nat), node.span));
                } else { args.clear(); }
            }
        });
        rejected(&broken, &format!("{name}: runtime arity"));
    }
}

#[test]
fn empty_values_and_sum_elimination_preserve_their_types() {
    for (expression, ret) in [("none(Nat)", "Option<Nat>"), ("empty_list(Text)", "List<Text>"), ("empty_sequence(Int)", "Sequence<Int>")] {
        let program = lower(&format!("    fn probe() -> {ret} = {expression}"));
        valid(&program);
        let mut broken = program.clone();
        broken.functions[0].body.ty = Some(OrdinaryType::Nat);
        broken.functions[0].ret = Some(OrdinaryType::Nat);
        rejected(&broken, expression);
        let mut broken = program;
        broken.functions[0].body.op = IrOp::Empty("unknown");
        rejected(&broken, "unknown empty constructor");
    }
    let program = lower("    fn probe() -> Nat = case(inl(1, Text), fun(n: Nat, n), fun(t: Text, text_length(t)))");
    valid(&program);
    let id = program.functions.iter().position(|f| f.name == "T.probe").unwrap();
    let mut broken = program.clone();
    if let IrOp::Case(_, left, right) = &mut broken.functions[id].body.op { std::mem::swap(left, right); }
    rejected(&broken, "sum branch domains");
    let mut broken = program;
    broken.functions[id].body.ty = Some(OrdinaryType::Text);
    broken.functions[id].ret = Some(OrdinaryType::Text);
    rejected(&broken, "sum branch results");
}

#[test]
fn effects_cannot_disappear_through_calls_or_function_values() {
    let program = lower("    fn pure(n: Nat) -> Nat = n\n    fn say(n: Nat) -> Nat = let _ = print_decimal(n) in n\n    fn relay(n: Nat) -> Nat = say(n)\n    fn value() -> Fn<Nat,Nat> = pure\n    fn closure() -> Fn<Nat,Nat> = fun(n: Nat, n)");
    valid(&program);
    for name in ["T.say", "T.relay"] {
        let mut broken = program.clone();
        broken.functions.iter_mut().find(|f| f.name == name).unwrap().effects.clear();
        rejected(&broken, name);
    }
    let mut broken = program.clone();
    let say = broken.functions.iter().position(|f| f.name == "T.say").unwrap();
    broken.functions.iter_mut().find(|f| f.name == "T.value").unwrap().body.op = IrOp::FunctionValue(say);
    rejected(&broken, "effectful function value");
    let mut broken = program;
    broken.functions.iter_mut().find(|f| f.is_lambda).unwrap().effects.push("io@stdout".to_string());
    rejected(&broken, "effectful lambda");
}

#[test]
fn initializers_cannot_hide_an_invalid_call_or_missing_global() {
    let program = lower("    let n = 1\n    print_decimal(n)");
    valid(&program);
    let mut broken = program.clone();
    let IrInit::Global { expr, .. } = &mut broken.init[0] else { panic!() };
    expr.ty = Some(OrdinaryType::Bool);
    broken.global_types[0] = Some(OrdinaryType::Bool);
    broken.init.truncate(1);
    broken.functions.pop();
    rejected(&broken, "initializer type differs from its callee");
    let mut broken = program.clone();
    let IrInit::Effect(expr) = &mut broken.init[1] else { panic!() };
    expr.ty = None;
    rejected(&broken, "untyped effect initializer");
    let mut broken = program.clone();
    broken.init.remove(0);
    rejected(&broken, "global has no initializer");
    let mut broken = program;
    broken.init.push(broken.init[0].clone());
    rejected(&broken, "global initialized twice");
}

#[test]
fn active_slots_and_malformed_capture_counts_are_rejected() {
    let program = lower("    fn probe(n: Nat) -> Nat = let k = 1 in n + k");
    valid(&program);
    let mut broken = program.clone();
    if let IrOp::Let { slot, body, .. } = &mut broken.functions[0].body.op {
        *slot = 0;
        **body = IrExpr::new(IrOp::Local(0), Some(OrdinaryType::Nat), SourceSpan::line(3));
    }
    rejected(&broken, "let overwrites a live parameter");
    let mut broken = program.clone();
    broken.functions[0].captures = usize::MAX;
    rejected(&broken, "capture count overflow");
    let mut broken = program;
    broken.functions[0].slots = usize::MAX;
    rejected(&broken, "frame size inconsistent with the slot table");
}

#[test]
fn record_nodes_keep_their_nominal_type_and_unique_fields() {
    let program = lower("    record P(n: Nat, t: Text)\n    fn probe() -> P = P(1, \"x\")\n    fn get(p: P) -> Nat = field(p, n)");
    valid(&program);
    let mut broken = program.clone();
    broken.functions[0].body.ty = Some(OrdinaryType::Nat);
    broken.functions[0].ret = Some(OrdinaryType::Nat);
    rejected(&broken, "record nominal type");
    let mut broken = program.clone();
    if let IrOp::Record { fields, .. } = &mut broken.functions[0].body.op {
        fields[1].0 = fields[0].0.clone();
    }
    rejected(&broken, "duplicate record field");
    let mut broken = program;
    if let IrOp::Field { record, .. } = &mut broken.functions[1].body.op {
        **record = IrExpr::new(IrOp::Nat(1), Some(OrdinaryType::Nat), record.span);
    }
    rejected(&broken, "field access on a Nat");
}
