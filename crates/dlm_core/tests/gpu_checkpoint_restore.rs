use dlm_core::{parse_module, Capability, Checker, DiagnosticKind, Runtime, TypeKind};

fn check(source: &str) -> dlm_core::CheckReport {
    let module = parse_module(source).expect("parse");
    Checker::new().check_module(&module)
}

fn passport<'a>(report: &'a dlm_core::CheckReport, name: &str) -> &'a dlm_core::Passport {
    report
        .inferred
        .iter()
        .find(|(n, _)| n == name)
        .map(|(_, p)| p)
        .unwrap_or_else(|| panic!("missing inferred passport for {name}"))
}

const ROUND_TRIP: &str = r#"
module demo

theory Cluster {
    let gpu0 = gpu_cuda_with(24576)
    let gpool = gpu_pool(gpu0)
    let vram = distributed_gpu_memory(gpool, 8192)
    let snap = checkpoint_gpu_memory(vram)
    let restored = restore_gpu_memory(snap)
    let cap = gpu_memory_mib(restored)
    print_decimal(cap)
    print_symbolic(snap)
    print_symbolic(restored)
}
"#;

#[test]
fn gpu_checkpoint_round_trip_checks_and_runs() {
    let report = check(ROUND_TRIP);
    assert!(report.ok(), "round trip must check, got: {:?}", report.diagnostics);

    let snap = passport(&report, "Cluster.snap");
    assert_eq!(snap.ty, TypeKind::GpuMemoryCheckpoint { memory_mib: Some(8192) });
    assert!(snap.capabilities.contains(Capability::CanRestoreGpuMemoryCheckpoint));

    let restored = passport(&report, "Cluster.restored");
    assert_eq!(restored.ty, TypeKind::DistributedGpuMemory { memory_mib: Some(8192) });
    assert!(restored.capabilities.contains(Capability::CanUseGpuMemory));
    assert!(restored.capabilities.contains(Capability::CanCheckpointGpuMemory));

    let module = parse_module(ROUND_TRIP).expect("parse");
    let run = Runtime::new().run_module(&module).expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "8192".to_string(),
            "gpu_memory_checkpoint<memory_mib=8192>".to_string(),
            "distributed_gpu_memory<memory_mib=8192>".to_string(),
        ]
    );
}

#[test]
fn gpu_checkpoint_history_follows_documented_passport_law() {
    let report = check(ROUND_TRIP);
    let restored = passport(&report, "Cluster.restored").to_string();
    assert!(
        restored.contains("gpu_memory:distributed_region:8192MiB -> checkpoint:gpu_memory -> checkpoint:restore_gpu_memory"),
        "unexpected history: {restored}"
    );
}

#[test]
fn gpu_checkpoint_is_not_live_vram() {
    let report = check(
        r#"
module demo

theory Cluster {
    let gpu0 = gpu_cuda_with(8192)
    let gpool = gpu_pool(gpu0)
    let vram = distributed_gpu_memory(gpool, 4096)
    let snap = checkpoint_gpu_memory(vram)
    let cap = gpu_memory_mib(snap)
}
"#,
    );
    assert!(!report.ok(), "GpuMemoryCheckpoint must not be usable as live VRAM");

    let snap = passport(&report, "Cluster.snap");
    for cap in [
        Capability::CanUseGpuMemory,
        Capability::CanLaunchGpuKernel,
        Capability::CanCopyCpuToGpu,
        Capability::CanCopyGpuToCpu,
        Capability::CanGpuPeerTransfer,
        Capability::CanPrintDecimal,
    ] {
        assert!(!snap.capabilities.contains(cap), "checkpoint must not carry {cap:?}");
    }
}

#[test]
fn gpu_checkpoint_requires_distributed_gpu_memory() {
    let report = check(include_str!("../../../examples/invalid/checkpoint_gpu_requires_gpu_memory.dlm"));
    assert!(!report.ok());
    assert!(report
        .diagnostics
        .iter()
        .any(|d| d.kind == DiagnosticKind::DistributedResourceError));
}

#[test]
fn gpu_restore_requires_gpu_checkpoint_and_rejects_cpu_checkpoint() {
    let report = check(include_str!("../../../examples/invalid/restore_gpu_requires_checkpoint.dlm"));
    assert!(!report.ok());
    assert!(report
        .diagnostics
        .iter()
        .any(|d| d.kind == DiagnosticKind::DistributedResourceError));

    let mixed = check(
        r#"
module demo

theory Cluster {
    let x86 = node_x86_64_with(8, 32768)
    let pool = virtual_pool(x86)
    let ram = distributed_memory(pool, 4096)
    let snap = checkpoint_memory(ram)
    let restored = restore_gpu_memory(snap)
}
"#,
    );
    assert!(!mixed.ok(), "a CPU MemoryCheckpoint must not restore into GPU memory");
}

#[test]
fn gpu_checkpoint_cannot_be_printed_as_decimal() {
    let report = check(include_str!("../../../examples/invalid/print_decimal_gpu_checkpoint.dlm"));
    assert!(!report.ok());
    assert!(report.diagnostics.iter().any(|d| d.kind == DiagnosticKind::AccessError));
}

#[test]
fn gpu_checkpoint_aliases_are_equivalent() {
    let report = check(
        r#"
module demo

theory Cluster {
    let gpu0 = gpu_rocm_with(16384)
    let gpool = gpu_pool(gpu0)
    let vram = distributed_gpu_memory(gpool, 2048)
    let a = gpu_checkpoint_memory(vram)
    let b = checkpoint_vram(vram)
    let ra = restore_gpu_checkpoint(a)
    let rb = restore_vram(b)
}
"#,
    );
    assert!(report.ok(), "aliases must check, got: {:?}", report.diagnostics);
    assert_eq!(passport(&report, "Cluster.ra").ty, passport(&report, "Cluster.rb").ty);
}
