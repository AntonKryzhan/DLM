use dlm_core::{parse_module, Checker};

#[test]
fn simple_nat_passes() {
    let source = r#"
module demo

theory Core {
    let a = 7
    let b = 10^100
    let c = a + b
    print_decimal(a)
}
"#;
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(report.ok(), "expected OK, got: {:?}", report.diagnostics);
}

#[test]
fn busy_beaver_print_fails() {
    let source = r#"
module demo

theory Core {
    let bb = BB(1000)
    print_decimal(bb)
}
"#;
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "expected diagnostic");
}

#[test]
fn quote_bridge_passes() {
    let source = r#"
module demo

bridge Core_quote : Core -> Meta {
    kind = quote
}

theory Core {
    let n = 7
}

theory Meta {
    let code = quote(Core.n)
}
"#;
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(report.ok(), "expected OK, got: {:?}", report.diagnostics);
}

#[test]
fn runtime_small_program_prints_output() {
    let source = r#"
module demo

theory Core {
    let a = 10
    let b = 20
    let c = a + b
    print_decimal(c)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected check OK, got: {:?}",
        check.diagnostics
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(run.output, vec!["30".to_string()]);
}

#[test]
fn runtime_overflow_is_error_even_if_symbolic_check_passes() {
    let source = r#"
module demo

theory Core {
    let a = 340282366920938463463374607431768211455
    let b = 1
    let c = a + b
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(check.ok(), "checker accepts compressed/symbolic values");

    let run = dlm_core::Runtime::new().run_module(&module);
    assert!(
        run.is_err(),
        "runtime exact evaluator must reject u128 overflow"
    );
}

#[test]
fn static_proof_from_runtime_fails() {
    let source = r#"
module demo

theory Core {
    let n = read_nat()
    let p = prove(n > 0)
}
"#;
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "expected RuntimeStaticMismatch diagnostic");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("RuntimeStaticMismatch")));
}

#[test]
fn runtime_witness_from_runtime_input_passes_check() {
    let source = r#"
module demo

theory Core {
    let n = read_nat()
    let positive = require(n > 0)
    print_decimal(n)
}
"#;
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(report.ok(), "expected OK, got: {:?}", report.diagnostics);
}

#[test]
fn runtime_read_nat_with_stdin_prints_output() {
    let source = r#"
module demo

theory Core {
    let n = read_nat()
    let positive = require(n > 0)
    print_decimal(n)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected check OK, got: {:?}",
        check.diagnostics
    );

    let run = dlm_core::Runtime::with_stdin("42")
        .run_module(&module)
        .expect("runtime");
    assert_eq!(run.output, vec!["42".to_string()]);
}

#[test]
fn runtime_require_can_fail() {
    let source = r#"
module demo

theory Core {
    let n = read_nat()
    let positive = require(n > 0)
    print_decimal(n)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected check OK, got: {:?}",
        check.diagnostics
    );

    let run = dlm_core::Runtime::with_stdin("0").run_module(&module);
    assert!(run.is_err(), "require(n > 0) must fail for stdin 0");
}

#[test]
fn axiom_trust_passes_in_research_mode() {
    let source = r#"
module demo

theory Core {
    let assumption = axiom_true()
    let p = prove(assumption)
}
"#;
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(
        report.ok(),
        "research policy permits Axiom trust, got: {:?}",
        report.diagnostics
    );
}

#[test]
fn axiom_trust_fails_in_trusted_only_mode() {
    let source = r#"
module demo

theory Core {
    let assumption = axiom_true()
    let p = prove(assumption)
}
"#;
    let module = parse_module(source).expect("parse");
    let report = Checker::with_policy(dlm_core::CheckPolicy::trusted_only()).check_module(&module);
    assert!(!report.ok(), "trusted-only policy must reject Axiom trust");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("TrustTaintError")));
}

#[test]
fn unsafe_nat_requires_allow_unsafe_policy() {
    let source = r#"
module demo

theory Core {
    let n = unsafe_nat()
}
"#;
    let module = parse_module(source).expect("parse");
    let default_report = Checker::new().check_module(&module);
    assert!(
        !default_report.ok(),
        "default research policy must reject Unsafe trust"
    );

    let unsafe_report =
        Checker::with_policy(dlm_core::CheckPolicy::allow_unsafe()).check_module(&module);
    assert!(
        unsafe_report.ok(),
        "allow-unsafe policy should accept Unsafe trust, got: {:?}",
        unsafe_report.diagnostics
    );
}

#[test]
fn transport_bridge_passes_check_and_run() {
    let source = r#"
module demo

theory PA {
    let n = 7
}

theory Meta {
    let m = transport(PA.n)
    print_decimal(m)
}

bridge PA_to_Meta : PA -> Meta {
    kind = transport
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected transport bridge to pass, got: {:?}",
        check.diagnostics
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(run.output, vec!["7".to_string()]);
}

#[test]
fn transport_without_bridge_fails() {
    let source = r#"
module demo

theory PA {
    let n = 7
}

theory Meta {
    let m = transport(PA.n)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "transport without bridge must fail");
    assert!(check
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("TheoryBridgeError")));
}

#[test]
fn soundness_bridge_is_axiom_tainted() {
    let source = r#"
module demo

theory PA {
    let p = prove(7 > 0)
}

theory Meta {
    let lifted = soundness(PA.p)
}

bridge PA_soundness : PA -> Meta {
    kind = soundness
}
"#;
    let module = parse_module(source).expect("parse");
    let research = Checker::new().check_module(&module);
    assert!(
        research.ok(),
        "research mode should accept soundness as Axiom-tainted, got: {:?}",
        research.diagnostics
    );

    let strict = Checker::with_policy(dlm_core::CheckPolicy::trusted_only()).check_module(&module);
    assert!(
        !strict.ok(),
        "trusted-only must reject soundness Axiom taint"
    );
    assert!(strict
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("TrustTaintError")));
}

#[test]
fn soundness_without_bridge_fails() {
    let source = r#"
module demo

theory PA {
    let p = prove(7 > 0)
}

theory Meta {
    let lifted = soundness(PA.p)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "soundness without explicit bridge must fail");
    assert!(check
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("TheoryBridgeError")));
}

#[test]
fn quote_inspect_ast_passes_check_and_run() {
    let source = r#"
module demo

bridge PA_quote : PA -> Meta {
    kind = quote
}

theory PA {
    let n = 7
}

theory Meta {
    let code = quote(PA.n)
    let ast = inspect_ast(code)
    print_text(ast)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected quote inspect to pass, got: {:?}",
        check.diagnostics
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(run.output, vec!["AST<PA.n>".to_string()]);
}

#[test]
fn inspect_ast_on_nat_fails() {
    let source = r#"
module demo

theory Core {
    let n = 7
    let ast = inspect_ast(n)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "inspect_ast on Nat must fail");
    assert!(check
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("AccessError")));
}

#[test]
fn quoted_term_cannot_be_added_as_nat() {
    let source = r#"
module demo

bridge PA_quote : PA -> Meta {
    kind = quote
}

theory PA {
    let n = 7
}

theory Meta {
    let code = quote(PA.n)
    let bad = code + 1
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "Term must not support Nat addition");
    assert!(check
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("AccessError")));
}

#[test]
fn typed_infinity_modes_pass_check_and_run() {
    let source = r#"
module demo

theory Core {
    let c = aleph0()
    let c_next = cardinal_succ(c)
    let o = omega()
    let o_next = ordinal_succ(o)
    print_symbolic(c_next)
    print_symbolic(o_next)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "typed infinity operations should pass, got: {:?}",
        check.diagnostics
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "cardinal_succ(ℵ0)".to_string(),
            "ordinal_succ(ω)".to_string()
        ]
    );
}

#[test]
fn ambiguous_infinity_is_rejected() {
    let source = r#"
module demo

theory Core {
    let x = infinity()
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "ambiguous infinity() must fail");
    assert!(check
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("InfinityModeError")));
}

#[test]
fn cardinal_succ_rejects_ordinal_infinity() {
    let source = r#"
module demo

theory Core {
    let o = omega()
    let bad = cardinal_succ(o)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "cardinal_succ(omega()) must fail");
    assert!(check
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("InfinityModeError")));
}

#[test]
fn equality_modes_check_and_run() {
    let source = r#"
module demo

bridge PA_quote : PA -> Meta {
    kind = quote
}

theory PA {
    let n = 7
    let m = 8
}

theory Meta {
    let value_same = eq_value(7, 7)
    let code_n = quote(PA.n)
    let code_m = quote(PA.m)
    let syntax_same = eq_syntax(code_n, code_n)
    let syntax_different = eq_syntax(code_n, code_m)

    print_symbolic(value_same)
    print_symbolic(syntax_same)
    print_symbolic(syntax_different)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected check OK, got: {:?}",
        check.diagnostics
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec!["true".to_string(), "true".to_string(), "false".to_string()]
    );
}

#[test]
fn ambiguous_equality_fails() {
    let source = r#"
module demo

theory Core {
    let a = 7
    let b = 7
    let same = equals(a, b)
}
"#;
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "expected EqualityModeError diagnostic");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("EqualityModeError")));
}

#[test]
fn syntax_equality_on_nat_fails() {
    let source = r#"
module demo

theory Core {
    let a = 7
    let b = 7
    let same = eq_syntax(a, b)
}
"#;
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "expected EqualityModeError diagnostic");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("EqualityModeError")));
}

#[test]
fn value_equality_on_term_fails() {
    let source = r#"
module demo

bridge PA_quote : PA -> Meta {
    kind = quote
}

theory PA {
    let n = 7
}

theory Meta {
    let code = quote(PA.n)
    let bad = eq_value(code, code)
}
"#;
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "expected AccessError diagnostic");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("AccessError")));
}

#[test]
fn passport_history_tracks_quote_and_soundness() {
    let source = r#"
module demo

bridge PA_quote : PA -> Meta {
    kind = quote
}

bridge PA_soundness : PA -> Meta {
    kind = soundness
}

theory PA {
    let n = 7
    let p = prove(n > 0)
}

theory Meta {
    let code = quote(PA.n)
    let lifted = soundness(PA.p)
}
"#;
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(report.ok(), "expected OK, got: {:?}", report.diagnostics);

    let code = report
        .inferred
        .iter()
        .find(|(name, _)| name == "Meta.code")
        .expect("Meta.code inferred")
        .1
        .clone();
    assert!(
        code.history.contains_event("bridge:quote"),
        "quote history missing: {}",
        code.history.summary()
    );

    let lifted = report
        .inferred
        .iter()
        .find(|(name, _)| name == "Meta.lifted")
        .expect("Meta.lifted inferred")
        .1
        .clone();
    assert!(
        lifted.history.contains_event("bridge:soundness"),
        "soundness history missing: {}",
        lifted.history.summary()
    );
    assert!(
        lifted.history.contains_event("axiom:soundness_assumption"),
        "soundness axiom history missing: {}",
        lifted.history.summary()
    );
}

#[test]
fn migration_bridge_passes_check_and_run_symbolic_remote() {
    let source = r#"
module demo

bridge Local_to_Cluster : Local -> Cluster {
    kind = migration
}

theory Local {
    let n = 7
}

theory Cluster {
    let arm = node_arm()
    let remote = migrate(arm, Local.n)
    print_symbolic(remote)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected migration bridge to pass, got: {:?}",
        check.diagnostics
    );
    let remote = check
        .inferred
        .iter()
        .find(|(name, _)| name == "Cluster.remote")
        .expect("Cluster.remote inferred")
        .1
        .clone();
    assert!(
        remote.history.contains_event("migration:"),
        "migration history missing: {}",
        remote.history.summary()
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(run.output, vec!["remote[aarch64](7)".to_string()]);
}

#[test]
fn migration_without_bridge_fails() {
    let source = r#"
module demo

theory Local {
    let n = 7
}

theory Cluster {
    let arm = node_arm()
    let remote = migrate(arm, Local.n)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "migration without bridge must fail");
    assert!(check
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("MigrationBridgeError")));
}

#[test]
fn migration_target_must_be_node() {
    let source = r#"
module demo

bridge Local_to_Cluster : Local -> Cluster {
    kind = migration
}

theory Local {
    let n = 7
}

theory Cluster {
    let not_node = 1
    let remote = migrate(not_node, Local.n)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "migration target must be a Node");
    assert!(check
        .diagnostics
        .iter()
        .any(
            |diag| format!("{:?}", diag.kind).contains("MigrationBridgeError")
                || format!("{:?}", diag.kind).contains("AccessError")
        ));
}

#[test]
fn remote_value_cannot_print_decimal() {
    let source = r#"
module demo

bridge Local_to_Cluster : Local -> Cluster {
    kind = migration
}

theory Local {
    let n = 7
}

theory Cluster {
    let arm = node_arm()
    let remote = migrate(arm, Local.n)
    print_decimal(remote)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        !check.ok(),
        "Remote<Nat> must not be decimal-printable without explicit materialization bridge"
    );
    assert!(check
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("AccessError")));
}

#[test]
fn virtual_cluster_pool_check_and_run() {
    let source = r#"
module demo

theory Cluster {
    let x86 = node_x86_64_with(8, 32768)
    let arm = node_aarch64_with(16, 65536)
    let pool = virtual_pool(x86, arm)
    let cores = pool_cores(pool)
    let memory = pool_memory_mib(pool)
    print_decimal(cores)
    print_decimal(memory)
    print_symbolic(pool)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected virtual cluster pool to pass, got: {:?}",
        check.diagnostics
    );
    let pool = check
        .inferred
        .iter()
        .find(|(name, _)| name == "Cluster.pool")
        .expect("Cluster.pool inferred")
        .1
        .clone();
    assert!(
        pool.history.contains_event("cluster:virtual_pool"),
        "cluster history missing: {}",
        pool.history.summary()
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "24".to_string(),
            "98304".to_string(),
            "virtual_cluster<nodes=2, cores=24, memory_mib=98304>".to_string(),
        ]
    );
}

#[test]
fn virtual_pool_requires_nodes() {
    let source = r#"
module demo

theory Cluster {
    let not_node = 7
    let pool = virtual_pool(not_node)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "virtual_pool must reject non-node values");
    assert!(check
        .diagnostics
        .iter()
        .any(
            |diag| format!("{:?}", diag.kind).contains("DistributedResourceError")
                || format!("{:?}", diag.kind).contains("AccessError")
        ));
}

#[test]
fn pool_cores_requires_virtual_cluster() {
    let source = r#"
module demo

theory Cluster {
    let node = node_x86_64_with(8, 32768)
    let cores = pool_cores(node)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "pool_cores must reject plain Node values");
    assert!(check
        .diagnostics
        .iter()
        .any(
            |diag| format!("{:?}", diag.kind).contains("DistributedResourceError")
                || format!("{:?}", diag.kind).contains("AccessError")
        ));
}

#[test]
fn schedule_on_virtual_pool_check_and_run() {
    let source = r#"
module demo

bridge Local_to_Cluster : Local -> Cluster {
    kind = migration
}

theory Local {
    let payload = 9
}

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let arm = node_aarch64_with(8, 16384)
    let pool = virtual_pool(x86, arm)
    let job = schedule_on(pool, arm, Local.payload)
    print_symbolic(job)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected schedule_on to pass, got: {:?}",
        check.diagnostics
    );
    let job = check
        .inferred
        .iter()
        .find(|(name, _)| name == "Cluster.job")
        .expect("Cluster.job inferred")
        .1
        .clone();
    assert!(
        job.history.contains_event("cluster:schedule"),
        "schedule history missing: {}",
        job.history.summary()
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(run.output, vec!["remote[aarch64](9)".to_string()]);
}

#[test]
fn schedule_without_bridge_fails() {
    let source = r#"
module demo

theory Local {
    let payload = 9
}

theory Cluster {
    let arm = node_aarch64_with(8, 16384)
    let pool = virtual_pool(arm)
    let job = schedule_on(pool, arm, Local.payload)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        !check.ok(),
        "schedule_on cross-theory without migration bridge must fail"
    );
    assert!(check
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("MigrationBridgeError")));
}

#[test]
fn schedule_requires_virtual_cluster() {
    let source = r#"
module demo

theory Cluster {
    let node = node_x86_64_with(4, 8192)
    let value = 9
    let job = schedule_on(node, node, value)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        !check.ok(),
        "schedule_on must reject non-cluster pool argument"
    );
    assert!(check
        .diagnostics
        .iter()
        .any(
            |diag| format!("{:?}", diag.kind).contains("DistributedResourceError")
                || format!("{:?}", diag.kind).contains("AccessError")
        ));
}

#[test]
fn schedule_target_not_in_pool_fails_at_runtime() {
    let source = r#"
module demo

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let arm = node_aarch64_with(8, 16384)
    let pool = virtual_pool(x86)
    let value = 9
    let job = schedule_on(pool, arm, value)
    print_symbolic(job)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(check.ok(), "static checker only verifies types/capabilities; runtime verifies pool membership, got: {:?}", check.diagnostics);
    let run = dlm_core::Runtime::new().run_module(&module);
    assert!(
        run.is_err(),
        "runtime must reject scheduling to a node outside the virtual pool"
    );
}

#[test]
fn distributed_memory_region_check_and_run() {
    let source = r#"
module demo

theory Cluster {
    let x86 = node_x86_64_with(8, 32768)
    let arm = node_aarch64_with(16, 65536)
    let pool = virtual_pool(x86, arm)
    let mem = distributed_memory(pool, 49152)
    let cap = memory_region_mib(mem)
    print_decimal(cap)
    print_symbolic(mem)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected distributed memory check to pass, got: {:?}",
        check.diagnostics
    );
    let mem = check
        .inferred
        .iter()
        .find(|(name, _)| name == "Cluster.mem")
        .expect("Cluster.mem inferred")
        .1
        .clone();
    assert!(
        mem.history.contains_event("memory:distributed_region"),
        "memory history missing: {}",
        mem.history.summary()
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "49152".to_string(),
            "distributed_memory<memory_mib=49152>".to_string(),
        ]
    );
}

#[test]
fn distributed_memory_rejects_non_cluster() {
    let source = r#"
module demo

theory Cluster {
    let node = node_x86_64_with(8, 32768)
    let mem = distributed_memory(node, 1024)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        !check.ok(),
        "distributed_memory must reject non-cluster pool argument"
    );
    assert!(check
        .diagnostics
        .iter()
        .any(
            |diag| format!("{:?}", diag.kind).contains("DistributedResourceError")
                || format!("{:?}", diag.kind).contains("AccessError")
        ));
}

#[test]
fn distributed_memory_rejects_excess_request() {
    let source = r#"
module demo

theory Cluster {
    let x86 = node_x86_64_with(8, 32768)
    let pool = virtual_pool(x86)
    let mem = distributed_memory(pool, 65536)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        !check.ok(),
        "distributed_memory must reject requests above known pool memory"
    );
    assert!(check
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("DistributedResourceError")));
}

#[test]
fn checkpoint_restore_memory_check_and_run() {
    let source = r#"
module demo

theory Cluster {
    let x86 = node_x86_64_with(8, 32768)
    let pool = virtual_pool(x86)
    let mem = distributed_memory(pool, 4096)
    let snap = checkpoint_memory(mem)
    let restored = restore_checkpoint(snap)
    let cap = memory_region_mib(restored)
    print_decimal(cap)
    print_symbolic(snap)
    print_symbolic(restored)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected checkpoint/restore to pass, got: {:?}",
        check.diagnostics
    );
    let snap = check
        .inferred
        .iter()
        .find(|(name, _)| name == "Cluster.snap")
        .expect("Cluster.snap inferred")
        .1
        .clone();
    assert!(
        snap.history.contains_event("checkpoint:memory"),
        "checkpoint history missing: {}",
        snap.history.summary()
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "4096".to_string(),
            "memory_checkpoint<memory_mib=4096>".to_string(),
            "distributed_memory<memory_mib=4096>".to_string(),
        ]
    );
}

#[test]
fn checkpoint_requires_distributed_memory() {
    let source = r#"
module demo

theory Cluster {
    let x86 = node_x86_64_with(8, 32768)
    let pool = virtual_pool(x86)
    let snap = checkpoint_memory(pool)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        !check.ok(),
        "checkpoint_memory must reject non-memory values"
    );
    assert!(check
        .diagnostics
        .iter()
        .any(
            |diag| format!("{:?}", diag.kind).contains("DistributedResourceError")
                || format!("{:?}", diag.kind).contains("AccessError")
        ));
}

#[test]
fn restore_requires_checkpoint() {
    let source = r#"
module demo

theory Cluster {
    let x86 = node_x86_64_with(8, 32768)
    let pool = virtual_pool(x86)
    let mem = distributed_memory(pool, 1024)
    let restored = restore_checkpoint(mem)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        !check.ok(),
        "restore_checkpoint must reject plain memory regions"
    );
    assert!(check
        .diagnostics
        .iter()
        .any(
            |diag| format!("{:?}", diag.kind).contains("DistributedResourceError")
                || format!("{:?}", diag.kind).contains("AccessError")
        ));
}

#[test]
fn remote_checkpoint_restore_check_and_run() {
    let source = r#"
module demo

bridge Local_to_Cluster : Local -> Cluster {
    kind = migration
}

theory Local {
    let payload = 11
}

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let arm = node_aarch64_with(8, 16384)
    let pool = virtual_pool(x86, arm)
    let job = schedule_on(pool, arm, Local.payload)
    let snap = checkpoint_remote(job)
    let restored = restore_remote(x86, snap)
    print_symbolic(snap)
    print_symbolic(restored)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected remote checkpoint/restore to pass, got: {:?}",
        check.diagnostics
    );
    let snap = check
        .inferred
        .iter()
        .find(|(name, _)| name == "Cluster.snap")
        .expect("Cluster.snap inferred")
        .1
        .clone();
    assert!(
        snap.history.contains_event("checkpoint:remote"),
        "remote checkpoint history missing: {}",
        snap.history.summary()
    );

    let restored = check
        .inferred
        .iter()
        .find(|(name, _)| name == "Cluster.restored")
        .expect("Cluster.restored inferred")
        .1
        .clone();
    assert!(
        restored.history.contains_event("checkpoint:restore_remote"),
        "remote restore history missing: {}",
        restored.history.summary()
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "remote_checkpoint[aarch64](11)".to_string(),
            "remote[x86_64](11)".to_string(),
        ]
    );
}

#[test]
fn live_migrate_remote_check_and_run() {
    let source = r#"
module demo

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let arm = node_aarch64_with(8, 16384)
    let pool = virtual_pool(x86, arm)
    let payload = 13
    let job = schedule_on(pool, x86, payload)
    let moved = live_migrate(arm, job)
    print_symbolic(job)
    print_symbolic(moved)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected live migration to pass, got: {:?}",
        check.diagnostics
    );
    let moved = check
        .inferred
        .iter()
        .find(|(name, _)| name == "Cluster.moved")
        .expect("Cluster.moved inferred")
        .1
        .clone();
    assert!(
        moved.history.contains_event("migration:live_remote"),
        "live migration history missing: {}",
        moved.history.summary()
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "remote[x86_64](13)".to_string(),
            "remote[aarch64](13)".to_string(),
        ]
    );
}

#[test]
fn remote_checkpoint_rejects_non_remote() {
    let source = r#"
module demo

theory Cluster {
    let payload = 7
    let snap = checkpoint_remote(payload)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        !check.ok(),
        "checkpoint_remote must reject non-Remote values"
    );
    assert!(check
        .diagnostics
        .iter()
        .any(
            |diag| format!("{:?}", diag.kind).contains("DistributedResourceError")
                || format!("{:?}", diag.kind).contains("AccessError")
        ));
}

#[test]
fn restore_remote_rejects_non_checkpoint() {
    let source = r#"
module demo

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let payload = 7
    let restored = restore_remote(x86, payload)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "restore_remote must reject plain values");
    assert!(check
        .diagnostics
        .iter()
        .any(
            |diag| format!("{:?}", diag.kind).contains("DistributedResourceError")
                || format!("{:?}", diag.kind).contains("AccessError")
        ));
}

#[test]
fn remote_materialize_with_bridge_check_and_run() {
    let source = r#"
module demo

bridge Local_to_Cluster : Local -> Cluster {
    kind = migration
}

bridge Cluster_to_Return : Cluster -> Return {
    kind = materialize
}

theory Local {
    let payload = 21
}

theory Cluster {
    let arm = node_aarch64_with(8, 16384)
    let pool = virtual_pool(arm)
    let job = schedule_on(pool, arm, Local.payload)
    print_symbolic(job)
}

theory Return {
    let back = materialize_remote(Cluster.job)
    print_decimal(back)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected materialize bridge to pass, got: {:?}",
        check.diagnostics
    );
    let back = check
        .inferred
        .iter()
        .find(|(name, _)| name == "Return.back")
        .expect("Return.back inferred")
        .1
        .clone();
    assert!(
        back.history.contains_event("remote:materialize"),
        "materialize history missing: {}",
        back.history.summary()
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec!["remote[aarch64](21)".to_string(), "21".to_string()]
    );
}

#[test]
fn local_remote_materialize_needs_no_cross_theory_bridge() {
    let source = r#"
module demo

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let pool = virtual_pool(x86)
    let payload = 17
    let job = schedule_on(pool, x86, payload)
    let back = materialize_remote(job)
    print_symbolic(job)
    print_decimal(back)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected local materialize to pass, got: {:?}",
        check.diagnostics
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec!["remote[x86_64](17)".to_string(), "17".to_string()]
    );
}

#[test]
fn materialize_without_bridge_fails() {
    let source = r#"
module demo

bridge Local_to_Cluster : Local -> Cluster {
    kind = migration
}

theory Local {
    let payload = 21
}

theory Cluster {
    let arm = node_aarch64_with(8, 16384)
    let pool = virtual_pool(arm)
    let job = schedule_on(pool, arm, Local.payload)
}

theory Return {
    let back = materialize_remote(Cluster.job)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "materialize without bridge must fail");
    assert!(check
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("TheoryBridgeError")));
}

#[test]
fn materialize_rejects_non_remote() {
    let source = r#"
module demo

theory Core {
    let n = 7
    let back = materialize_remote(n)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        !check.ok(),
        "materialize_remote must reject non-Remote values"
    );
    assert!(check
        .diagnostics
        .iter()
        .any(
            |diag| format!("{:?}", diag.kind).contains("DistributedResourceError")
                || format!("{:?}", diag.kind).contains("AccessError")
        ));
}

#[test]
fn portable_code_deploy_check_and_run() {
    let source = r#"
module demo

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let arm = node_aarch64_with(8, 16384)
    let pool = virtual_pool(x86, arm)
    let payload = 33
    let code = compile_portable(payload)
    let job = deploy_on(pool, arm, code)
    print_symbolic(code)
    print_symbolic(job)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected portable deploy to pass, got: {:?}",
        check.diagnostics
    );
    let code = check
        .inferred
        .iter()
        .find(|(name, _)| name == "Cluster.code")
        .expect("Cluster.code inferred")
        .1
        .clone();
    assert!(
        code.history.contains_event("portable:compile"),
        "portable compile history missing: {}",
        code.history.summary()
    );

    let job = check
        .inferred
        .iter()
        .find(|(name, _)| name == "Cluster.job")
        .expect("Cluster.job inferred")
        .1
        .clone();
    assert!(
        job.history.contains_event("portable:deploy_on"),
        "portable deploy history missing: {}",
        job.history.summary()
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "portable_code(33)".to_string(),
            "remote[aarch64](33)".to_string(),
        ]
    );
}

#[test]
fn compile_portable_requires_serializable_value() {
    let source = r#"
module demo

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let pool = virtual_pool(x86)
    let code = compile_portable(pool)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        !check.ok(),
        "compile_portable must reject non-serializable values"
    );
    assert!(check
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("AccessError")));
}

#[test]
fn deploy_requires_portable_code() {
    let source = r#"
module demo

theory Cluster {
    let arm = node_aarch64_with(8, 16384)
    let payload = 33
    let job = deploy_portable(arm, payload)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        !check.ok(),
        "deploy_portable must reject non-PortableCode values"
    );
    assert!(check
        .diagnostics
        .iter()
        .any(
            |diag| format!("{:?}", diag.kind).contains("DistributedResourceError")
                || format!("{:?}", diag.kind).contains("AccessError")
        ));
}

#[test]
fn gpu_memory_pool_check_and_run() {
    let source = r#"
module demo

theory Cluster {
    let gpu0 = gpu_cuda_with(24576)
    let gpu1 = gpu_cuda_with(24576)
    let gpool = gpu_pool(gpu0, gpu1)
    let vram = distributed_gpu_memory(gpool, 32768)
    let cap = gpu_memory_mib(vram)
    print_decimal(cap)
    print_symbolic(gpool)
    print_symbolic(vram)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected GPU memory pool to pass, got: {:?}",
        check.diagnostics
    );
    let vram = check
        .inferred
        .iter()
        .find(|(name, _)| name == "Cluster.vram")
        .expect("Cluster.vram inferred")
        .1
        .clone();
    assert!(
        vram.history
            .contains_event("gpu_memory:distributed_region:32768MiB"),
        "GPU memory history missing: {}",
        vram.history.summary()
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "32768".to_string(),
            "gpu_pool<devices=2, memory_mib=49152>".to_string(),
            "distributed_gpu_memory<memory_mib=32768>".to_string(),
        ]
    );
}

#[test]
fn gpu_memory_rejects_cpu_node_as_pool() {
    let source = r#"
module demo

theory Cluster {
    let cpu = node_x86_64_with(8, 32768)
    let mem = distributed_gpu_memory(cpu, 1024)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        !check.ok(),
        "distributed_gpu_memory must reject non-GpuPool values"
    );
    assert!(check
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("AccessError")
            || format!("{:?}", diag.kind).contains("DistributedResourceError")));
}

#[test]
fn gpu_memory_rejects_oversized_request() {
    let source = r#"
module demo

theory Cluster {
    let gpu0 = gpu_cuda_with(8192)
    let gpool = gpu_pool(gpu0)
    let vram = distributed_gpu_memory(gpool, 16384)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        !check.ok(),
        "distributed_gpu_memory must reject requests larger than the pool"
    );
    assert!(check
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("DistributedResourceError")));
}

#[test]
fn gpu_cpu_transfer_check_and_run() {
    let source = r#"
module demo

theory Cluster {
    let gpu0 = gpu_cuda_with(24576)
    let gpool = gpu_pool(gpu0)
    let vram = distributed_gpu_memory(gpool, 8192)
    let payload = 55
    let on_gpu = copy_to_gpu(payload, vram)
    let back = copy_from_gpu(on_gpu)
    print_symbolic(on_gpu)
    print_decimal(back)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected GPU CPU transfer to pass, got: {:?}",
        check.diagnostics
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "gpu_value<memory_mib=8192>(55)".to_string(),
            "55".to_string()
        ]
    );
}

#[test]
fn copy_to_gpu_requires_gpu_memory_region() {
    let source = r#"
module demo

theory Cluster {
    let x86 = node_x86_64_with(8, 32768)
    let pool = virtual_pool(x86)
    let ram = distributed_memory(pool, 4096)
    let payload = 55
    let on_gpu = copy_to_gpu(payload, ram)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "copy_to_gpu to CPU RAM must fail");
}

#[test]
fn copy_from_gpu_requires_gpu_value() {
    let source = r#"
module demo

theory Cluster {
    let payload = 55
    let back = copy_from_gpu(payload)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "copy_from_gpu(non-gpu-value) must fail");
}

#[test]
fn gpu_value_cannot_print_decimal() {
    let source = r#"
module demo

theory Cluster {
    let gpu0 = gpu_cuda_with(8192)
    let gpool = gpu_pool(gpu0)
    let vram = distributed_gpu_memory(gpool, 4096)
    let payload = 55
    let on_gpu = copy_to_gpu(payload, vram)
    print_decimal(on_gpu)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "GpuValue must not have can_print_decimal");
}

#[test]
fn gpu_kernel_launch_check_and_run() {
    let source = r#"
module demo

theory Cluster {
    let gpu0 = gpu_cuda_with(24576)
    let gpool = gpu_pool(gpu0)
    let vram = distributed_gpu_memory(gpool, 8192)
    let payload = 77
    let kernel = compile_gpu_kernel(payload)
    let result = launch_kernel(vram, kernel)
    let back = copy_from_gpu(result)
    print_symbolic(kernel)
    print_symbolic(result)
    print_decimal(back)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected GPU kernel launch to pass, got: {:?}",
        check.diagnostics
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "gpu_kernel(77)".to_string(),
            "gpu_value<memory_mib=8192>(77)".to_string(),
            "77".to_string(),
        ]
    );
}

#[test]
fn compile_gpu_kernel_requires_serializable_value() {
    let source = r#"
module demo

theory Cluster {
    let gpu0 = gpu_cuda_with(8192)
    let gpool = gpu_pool(gpu0)
    let kernel = compile_gpu_kernel(gpool)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "compile_gpu_kernel(gpu_pool) must fail");
}

#[test]
fn launch_kernel_requires_gpu_memory_region() {
    let source = r#"
module demo

theory Cluster {
    let x86 = node_x86_64_with(8, 32768)
    let pool = virtual_pool(x86)
    let ram = distributed_memory(pool, 4096)
    let payload = 77
    let kernel = compile_gpu_kernel(payload)
    let result = launch_kernel(ram, kernel)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        !check.ok(),
        "launch_kernel must reject CPU DistributedMemory"
    );
}

#[test]
fn launch_kernel_requires_gpu_kernel() {
    let source = r#"
module demo

theory Cluster {
    let gpu0 = gpu_cuda_with(8192)
    let gpool = gpu_pool(gpu0)
    let vram = distributed_gpu_memory(gpool, 4096)
    let payload = 77
    let result = launch_kernel(vram, payload)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        !check.ok(),
        "launch_kernel must reject non-GpuKernel arguments"
    );
}

#[test]
fn gpu_kernel_cannot_print_decimal() {
    let source = r#"
module demo

theory Cluster {
    let payload = 77
    let kernel = compile_gpu_kernel(payload)
    print_decimal(kernel)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "GpuKernel must not have can_print_decimal");
}

#[test]
fn universe_set_class_check_and_run() {
    let source = r#"
module demo

theory Foundations {
    let u0 = U0()
    let u1 = universe_succ(u0)
    let s0 = set_of(u0)
    let c0 = class_of(u0)
    let lives = set_lives_in(s0)
    let level = class_level(c0)

    print_symbolic(u0)
    print_symbolic(u1)
    print_symbolic(s0)
    print_symbolic(c0)
    print_decimal(lives)
    print_decimal(level)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected universe hierarchy example to pass, got: {:?}",
        check.diagnostics
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "U0".to_string(),
            "U1".to_string(),
            "Set<U0->U1>".to_string(),
            "Class<U0>".to_string(),
            "1".to_string(),
            "0".to_string(),
        ]
    );
}

#[test]
fn ambiguous_universe_is_rejected() {
    let source = r#"
module demo

theory Foundations {
    let u = universe()
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "bare universe() must be rejected");
}

#[test]
fn set_of_all_sets_is_rejected() {
    let source = r#"
module demo

theory Foundations {
    let bad = set_of_all_sets()
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "set_of_all_sets must be rejected");
}

#[test]
fn set_of_requires_universe() {
    let source = r#"
module demo

theory Foundations {
    let u0 = U0()
    let s0 = set_of(u0)
    let bad = set_of(s0)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        !check.ok(),
        "set_of(Set) must be rejected; only set_of(Universe) is valid"
    );
}

#[test]
fn class_level_requires_class() {
    let source = r#"
module demo

theory Foundations {
    let u0 = U0()
    let bad = class_level(u0)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        !check.ok(),
        "class_level(U0) must be rejected; it requires Class<U n>"
    );
}

#[test]
fn definability_passport_check_and_run() {
    let source = r#"
module demo

theory Meta {
    let lang = language_L0()
    let enc = encoding_godel()
    let meta = meta_level(1)
    let d = definable_nat(lang, enc, 20, meta)
    let bound = definability_bound(d)
    let level = definability_meta_level(d)

    print_symbolic(d)
    print_decimal(bound)
    print_decimal(level)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected check OK, got: {:?}",
        check.diagnostics
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "definable_nat<L0,Godel,Meta,bound=20,M1>".to_string(),
            "20".to_string(),
            "1".to_string(),
        ]
    );
}

#[test]
fn bare_definable_nat_fails() {
    let source = r#"
module demo

theory Meta {
    let d = definable_nat(20)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "bare definability must fail");
    assert!(check
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("DefinabilityError")));
}

#[test]
fn berry_paradox_fails() {
    let source = r#"
module demo

theory Meta {
    let b = berry_number(20)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "Berry-style construction must fail");
    assert!(check
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("DefinabilityError")));
}

#[test]
fn definability_requires_language() {
    let source = r#"
module demo

theory Meta {
    let enc = encoding_godel()
    let meta = meta_level(1)
    let d = definable_nat(7, enc, 20, meta)
}
"#;
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        !check.ok(),
        "definable_nat must require Language as first argument"
    );
    assert!(check
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("DefinabilityError")));
}

#[test]
fn big_number_hierarchy_check_and_run() {
    let source = include_str!("../../../examples/valid/big_number_hierarchy.dlm");
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(check.ok(), "expected OK, got: {:?}", check.diagnostics);

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "Graham()".to_string(),
            "TREE(3)".to_string(),
            "BB(1000)".to_string(),
            "FGH(5)".to_string(),
            "3".to_string(),
        ]
    );
}

#[test]
fn tree_cannot_print_decimal() {
    let source = include_str!("../../../examples/invalid/print_decimal_tree.dlm");
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "print_decimal(TREE(3)) must fail");
}

#[test]
fn bare_big_number_is_rejected() {
    let source = include_str!("../../../examples/invalid/bare_big_number.dlm");
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "bare big_number() must fail");
    assert!(check
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("BigNumberError")));
}

#[test]
fn tree_requires_positive_literal_parameter() {
    let source = include_str!("../../../examples/invalid/tree_requires_positive.dlm");
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "TREE(0) must fail");
}

#[test]
fn growth_parameter_requires_big_nat() {
    let source = include_str!("../../../examples/invalid/growth_parameter_requires_big_nat.dlm");
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(!check.ok(), "growth_parameter(Nat) must fail");
}

#[test]
fn minimal_proof_kernel_check_and_run() {
    let source = include_str!("../../../examples/valid/minimal_proof_kernel.dlm");
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected check OK, got: {:?}",
        check.diagnostics
    );

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "proof_term<true_intro>".to_string(),
            "StaticProof<kernel_checked:true_intro>".to_string(),
            "proof_term<gt_intro>".to_string(),
            "StaticProof<kernel_checked:gt_intro>".to_string(),
        ]
    );
}

#[test]
fn check_proof_requires_proof_term() {
    let source = include_str!("../../../examples/invalid/check_proof_requires_proof_term.dlm");
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "expected ProofKernelError");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("ProofKernelError")));
}

#[test]
fn fake_proof_is_rejected() {
    let source = include_str!("../../../examples/invalid/fake_proof_rejected.dlm");
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "expected ProofKernelError");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("ProofKernelError")));
}

#[test]
fn proof_gt_from_runtime_fails() {
    let source = include_str!("../../../examples/invalid/proof_gt_from_runtime_fails.dlm");
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "expected RuntimeStaticMismatch");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("RuntimeStaticMismatch")));
}

#[test]
fn proof_gt_requires_direct_compare() {
    let source = include_str!("../../../examples/invalid/proof_gt_requires_direct_compare.dlm");
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "expected AccessError");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("AccessError")));
}

#[test]
fn soundness_summary_clean_kernel_proof_is_clean() {
    let source = r#"
module demo

theory Kernel {
    let truth = proof_true()
    let checked = check_proof(truth)
}
"#;
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(report.ok(), "expected OK, got: {:?}", report.diagnostics);

    let summary = dlm_core::SoundnessSummary::from_report(&report);
    assert_eq!(summary.proof_terms, 1);
    assert_eq!(summary.static_proofs, 1);
    assert_eq!(summary.kernel_checked_proofs, 1);
    assert_eq!(summary.axiom_tainted, 0);
    assert!(summary.is_clean(), "summary should be clean: {summary:?}");
}

#[test]
fn soundness_summary_detects_axiom_soundness_bridge() {
    let source = r#"
module demo

bridge PA_soundness : PA -> Meta {
    kind = soundness
}

theory PA {
    let p = prove(7 > 3)
}

theory Meta {
    let lifted = soundness(PA.p)
}
"#;
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(report.ok(), "expected OK, got: {:?}", report.diagnostics);

    let summary = dlm_core::SoundnessSummary::from_report(&report);
    assert_eq!(summary.static_proofs, 2);
    assert_eq!(summary.soundness_bridge_events, 1);
    assert_eq!(summary.axiom_tainted, 1);
    assert!(
        !summary.is_clean(),
        "soundness bridge should be axiom-tainted"
    );
}

#[test]
fn bridge_soundness_classification_is_reported() {
    let source = r#"
module demo

bridge PA_def : PA -> Meta {
    kind = definitional
}

bridge PA_cons : PA -> Meta {
    kind = conservative
}

bridge PA_quote : PA -> Meta {
    kind = quote
}

bridge PA_transport : PA -> Meta {
    kind = transport
}

bridge PA_soundness : PA -> Meta {
    kind = soundness
}

bridge PA_reflection : PA -> Meta {
    kind = reflection
}

theory PA {
    let n = 7
    let p = prove(n > 0)
}

theory Meta {
    let code = quote(PA.n)
    let moved = transport(PA.n)
    let lifted = soundness(PA.p)
}
"#;
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(
        report.ok(),
        "expected bridge classification program to check, got: {:?}",
        report.diagnostics
    );

    let summary = dlm_core::SoundnessSummary::from_report(&report);
    assert_eq!(summary.bridge_declarations, 6);
    assert_eq!(summary.definitional_bridge_declarations, 1);
    assert_eq!(summary.conservative_bridge_declarations, 1);
    assert_eq!(summary.quote_bridge_declarations, 1);
    assert_eq!(summary.transport_bridge_declarations, 1);
    assert_eq!(summary.soundness_bridge_declarations, 1);
    assert_eq!(summary.reflection_bridge_declarations, 1);
    assert_eq!(summary.soundness_bridge_events, 1);
    assert_eq!(summary.axiom_tainted, 1);

    let rendered = summary.render_human();
    assert!(
        rendered.contains("syntax-only bridge"),
        "missing quote profile: {rendered}"
    );
    assert!(
        rendered.contains("axiom-tainted truth bridge"),
        "missing soundness profile: {rendered}"
    );
    assert!(
        rendered.contains("reflective bridge"),
        "missing reflection profile: {rendered}"
    );
}

#[test]
fn unsafe_bridge_declaration_is_not_clean_in_explain_summary() {
    let source = r#"
module demo

bridge PA_bad : PA -> Meta {
    kind = unsafe
}

theory PA {
    let n = 7
}

theory Meta {
    let m = 1
}
"#;
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(
        report.ok(),
        "unsafe bridge declarations are explain-level issues, not check errors in MVP: {:?}",
        report.diagnostics
    );

    let summary = dlm_core::SoundnessSummary::from_report(&report);
    assert_eq!(summary.unsafe_bridge_declarations, 1);
    assert!(
        !summary.is_clean(),
        "unsafe bridge declaration must make explain summary not clean"
    );
    assert!(summary.render_human().contains("no safe preservation law"));
}

#[test]
fn quote_derived_text_does_not_break_soundness_cleanliness() {
    let source = r#"
module demo

bridge PA_quote : PA -> Meta {
    kind = quote
}

theory PA {
    let n = 7
}

theory Meta {
    let code = quote(PA.n)
    let ast = inspect_ast(code)
}
"#;
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(
        report.ok(),
        "expected quote/inspect example to check, got: {:?}",
        report.diagnostics
    );

    let summary = dlm_core::SoundnessSummary::from_report(&report);
    assert_eq!(summary.quote_bridge_events, 2);
    assert!(
        summary.issues.is_empty(),
        "derived Text from inspect_ast must not be treated as quote invariant violation: {:?}",
        summary.issues
    );
    assert!(
        summary.is_clean(),
        "quote-only derived values should remain clean: {summary:?}"
    );
}

#[test]
fn infinity_arithmetic_extended_check_and_run() {
    let source = include_str!("../../../examples/valid/infinity_arithmetic_extended.dlm");
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(check.ok(), "expected OK, got: {:?}", check.diagnostics);

    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "cardinal_add(ℵ0, cardinal_succ(ℵ0))".to_string(),
            "ordinal_add(ω, ordinal_succ(ω))".to_string(),
            "limit(ω)".to_string(),
            "potential_step(∞ₚ)".to_string(),
            "Class∞<U0>".to_string(),
            "Universe∞<U0>".to_string(),
        ]
    );
}

#[test]
fn cardinal_add_requires_cardinal_operands() {
    let source = include_str!("../../../examples/invalid/cardinal_add_requires_cardinal.dlm");
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "expected InfinityModeError");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("InfinityModeError")));
}

#[test]
fn ordinal_add_requires_ordinal_operands() {
    let source = include_str!("../../../examples/invalid/ordinal_add_requires_ordinal.dlm");
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "expected InfinityModeError");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("InfinityModeError")));
}

#[test]
fn class_infinity_requires_class_value() {
    let source = include_str!("../../../examples/invalid/class_infinity_requires_class.dlm");
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "expected InfinityModeError");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("InfinityModeError")));
}

#[test]
fn universe_infinity_requires_universe_value() {
    let source = include_str!("../../../examples/invalid/universe_infinity_requires_universe.dlm");
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "expected UniverseLevelError");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("UniverseLevelError")));
}

#[test]
fn potential_step_requires_potential_infinity() {
    let source = include_str!("../../../examples/invalid/potential_step_requires_potential.dlm");
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "expected InfinityModeError");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("InfinityModeError")));
}

#[test]
fn provability_truth_boundary_check_and_run() {
    let source = include_str!("../../../examples/valid/provability_truth_boundary.dlm");
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected provability boundary example to pass, got: {:?}",
        check.diagnostics
    );
    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "Provable<Meta.kernel_checked:true_intro>".to_string(),
            "StaticProof<truth_from_provable:kernel_checked:true_intro>".to_string(),
        ]
    );
}

#[test]
fn proposition_passport_check_and_run() {
    let source = include_str!("../../../examples/valid/proposition_passport.dlm");
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected proposition example to pass, got: {:?}",
        check.diagnostics
    );
    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec!["Prop<true>".to_string(), "Prop<gt>".to_string()]
    );
}

#[test]
fn truth_from_provable_without_soundness_fails() {
    let source =
        include_str!("../../../examples/invalid/truth_from_provable_without_soundness.dlm");
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(
        !report.ok(),
        "truth_from_provable without soundness/axiom must fail"
    );
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("TheoryBridgeError")));
}

#[test]
fn provable_requires_static_proof() {
    let source = include_str!("../../../examples/invalid/provable_requires_static_proof.dlm");
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "provable_of(non-proof) must fail");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("TruthBoundaryError")));
}

#[test]
fn prop_gt_from_runtime_fails() {
    let source = include_str!("../../../examples/invalid/prop_gt_from_runtime_fails.dlm");
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "prop_gt from runtime input must fail");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("RuntimeStaticMismatch")));
}

#[test]
fn truth_axiom_rejected_by_trusted_only() {
    let source = include_str!("../../../examples/invalid/truth_axiom_rejected_by_trusted_only.dlm");
    let module = parse_module(source).expect("parse");
    let report = Checker::with_policy(dlm_core::CheckPolicy::trusted_only()).check_module(&module);
    assert!(
        !report.ok(),
        "trusted-only must reject truth_from_provable_axiom"
    );
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("TrustTaintError")));
}

#[test]
fn consistency_incompleteness_boundary_check_and_run() {
    let source = include_str!("../../../examples/valid/consistency_incompleteness_boundary.dlm");
    let module = parse_module(source).expect("parse");
    let check = Checker::new().check_module(&module);
    assert!(
        check.ok(),
        "expected consistency boundary example to pass, got: {:?}",
        check.diagnostics
    );
    let run = dlm_core::Runtime::new()
        .run_module(&module)
        .expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "Consistency<Meta>".to_string(),
            "StaticProof<consistency_axiom:Meta>".to_string(),
        ]
    );
}

#[test]
fn prove_own_consistency_fails() {
    let source = include_str!("../../../examples/invalid/prove_own_consistency_fails.dlm");
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "prove_consistency should fail in MVP");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("IncompletenessBoundaryError")));
}

#[test]
fn consistency_axiom_requires_claim() {
    let source = include_str!("../../../examples/invalid/consistency_axiom_requires_claim.dlm");
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(!report.ok(), "assume_consistency(non-claim) must fail");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("IncompletenessBoundaryError")));
}

#[test]
fn consistency_axiom_rejected_by_trusted_only() {
    let source =
        include_str!("../../../examples/invalid/consistency_axiom_rejected_by_trusted_only.dlm");
    let module = parse_module(source).expect("parse");
    let report = Checker::with_policy(dlm_core::CheckPolicy::trusted_only()).check_module(&module);
    assert!(!report.ok(), "trusted-only must reject assume_consistency");
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| format!("{:?}", diag.kind).contains("TrustTaintError")));
}

#[test]
fn consistency_summary_detects_axiom_assumption() {
    let source = include_str!("../../../examples/valid/consistency_summary_axiom.dlm");
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(
        report.ok(),
        "expected check OK, got: {:?}",
        report.diagnostics
    );
    let summary = dlm_core::SoundnessSummary::from_report(&report);
    assert_eq!(summary.consistency_claims, 1);
    assert_eq!(summary.consistency_axiom_lifts, 1);
    assert!(
        !summary.is_clean(),
        "consistency axiom must make summary not clean"
    );
}
