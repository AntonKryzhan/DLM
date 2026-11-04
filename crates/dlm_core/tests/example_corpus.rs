//! Corpus-level integrity tests for `examples/` and `docs/MANIFEST.json`.
//!
//! Individual examples are already exercised by targeted tests, but nothing
//! guaranteed that the whole corpus stays consistent. This file enforces:
//!
//! - every `examples/valid/*.dlm` parses and checks under the default policy;
//! - every valid example runs, except an explicit list of checker-only examples
//!   (static proofs / axioms / intentional runtime limits), which must still fail
//!   at runtime so the list cannot silently go stale;
//! - every `examples/invalid/*.dlm` is rejected in its intended mode
//!   (check, `--trusted-only`, or runtime);
//! - no invalid example is rejected only because a builtin is missing from the
//!   checker (that would hide an unimplemented feature behind a "passing" test);
//! - `docs/MANIFEST.json` lists every document in `docs/` by bare file name;
//! - v0.80: examples are loaded as programs (with their imports, `lib/` on
//!   the search path), and every module in `lib/` checks.

use std::fs;
use std::path::{Path, PathBuf};

use dlm_core::{load_program_with_paths, parse_module, CheckPolicy, Checker, Program, Runtime};

/// Valid examples that are checker-level only. `dlm run` must reject them.
const CHECK_ONLY_VALID: &[&str] = &[
    "axiom_research",
    "bridge_soundness_classification",
    "equality_proof_mode",
    "history_chain",
    "reflection_bridge_guard",
    "simple_nat",
    "soundness_research",
    "soundness_summary_axiom",
    "static_proof_small",
];

/// Invalid examples that pass `check` and must fail during `run`.
const RUNTIME_FAILURE_INVALID: &[&str] = &[
    "deploy_on_target_not_in_pool",
    "recursion_fuel_exhausted",
    "runtime_witness_failed",
    "schedule_target_not_in_pool",
];

/// Invalid examples whose name ends with this suffix pass the research policy
/// and must be rejected by `--trusted-only`.
const TRUSTED_ONLY_SUFFIX: &str = "_rejected_by_trusted_only";

/// Diagnostic text that means "the checker does not know this builtin".
const MISSING_BUILTIN_MARKER: &str = "unknown builtin or function";

/// Stdin used for examples that read input at runtime.
const RUN_STDIN: &str = "5";

fn repo_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("..").join("..")
}

fn dlm_files(dir: &str) -> Vec<(String, String)> {
    let path = repo_root().join(dir);
    let mut files: Vec<(String, String)> = fs::read_dir(&path)
        .unwrap_or_else(|err| panic!("cannot read {}: {err}", path.display()))
        .filter_map(|entry| entry.ok().map(|e| e.path()))
        .filter(|p| p.extension().and_then(|e| e.to_str()) == Some("dlm"))
        .map(|p| {
            let stem = p.file_stem().and_then(|s| s.to_str()).unwrap_or_default().to_string();
            let source = fs::read_to_string(&p)
                .unwrap_or_else(|err| panic!("cannot read {}: {err}", p.display()));
            (stem, source)
        })
        .collect();
    files.sort();
    assert!(!files.is_empty(), "no .dlm files found in {dir}");
    files
}

/// v0.80: the example `name` in `dir` with its imports (`lib/` on the search path).
fn load(dir: &str, name: &str) -> Result<Program, Vec<dlm_core::Diagnostic>> {
    let path = repo_root().join(dir).join(format!("{name}.dlm"));
    load_program_with_paths(&path, &[repo_root().join("lib")])
}

fn render(diagnostics: &[dlm_core::Diagnostic]) -> String {
    diagnostics.iter().map(|d| format!("{d}")).collect::<String>()
}

#[test]
fn every_valid_example_checks_under_research_policy() {
    let mut failures = Vec::new();
    for (name, _) in dlm_files("examples/valid") {
        match load("examples/valid", &name) {
            Ok(program) => {
                let report = Checker::with_policy(CheckPolicy::research()).check_program(&program);
                if !report.ok() {
                    failures.push(format!("{name}:\n{}", render(&report.diagnostics)));
                }
            }
            Err(diagnostics) => failures.push(format!("{name} (parse):\n{}", render(&diagnostics))),
        }
    }
    assert!(failures.is_empty(), "valid examples rejected by checker:\n{}", failures.join("\n"));
}

#[test]
fn every_runnable_valid_example_runs_and_check_only_list_is_exact() {
    let files = dlm_files("examples/valid");
    for listed in CHECK_ONLY_VALID {
        assert!(
            files.iter().any(|(name, _)| name == listed),
            "CHECK_ONLY_VALID lists missing example '{listed}'"
        );
    }

    let mut failures = Vec::new();
    for (name, _) in files {
        let Ok(program) = load("examples/valid", &name) else { continue };
        let result = Runtime::with_stdin(RUN_STDIN).run_program(&program);
        let check_only = CHECK_ONLY_VALID.contains(&name.as_str());
        match (check_only, result) {
            (false, Err(diagnostic)) => failures.push(format!("{name}: unexpected runtime error\n{diagnostic}")),
            (true, Ok(_)) => failures.push(format!(
                "{name}: listed as check-only but now runs; remove it from CHECK_ONLY_VALID"
            )),
            _ => {}
        }
    }
    assert!(failures.is_empty(), "{}", failures.join("\n"));
}

#[test]
fn every_invalid_example_is_rejected_in_its_intended_mode() {
    let files = dlm_files("examples/invalid");
    for listed in RUNTIME_FAILURE_INVALID {
        assert!(
            files.iter().any(|(name, _)| name == listed),
            "RUNTIME_FAILURE_INVALID lists missing example '{listed}'"
        );
    }

    let mut failures = Vec::new();
    for (name, _) in files {
        let program = match load("examples/invalid", &name) {
            Ok(program) => program,
            // A parse or import error is a valid rejection for an invalid example.
            Err(_) => continue,
        };
        let research = Checker::with_policy(CheckPolicy::research()).check_program(&program);

        if name.ends_with(TRUSTED_ONLY_SUFFIX) {
            if !research.ok() {
                failures.push(format!(
                    "{name}: expected to pass research policy, got:\n{}",
                    render(&research.diagnostics)
                ));
            }
            let trusted = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&program);
            if trusted.ok() {
                failures.push(format!("{name}: expected rejection under --trusted-only"));
            }
            continue;
        }

        if RUNTIME_FAILURE_INVALID.contains(&name.as_str()) {
            if !research.ok() {
                failures.push(format!(
                    "{name}: expected to pass check and fail at runtime, got:\n{}",
                    render(&research.diagnostics)
                ));
            } else if Runtime::with_stdin("0").run_program(&program).is_ok() {
                failures.push(format!("{name}: expected runtime failure"));
            }
            continue;
        }

        if research.ok() {
            failures.push(format!("{name}: expected checker rejection"));
            continue;
        }

        let only_missing_builtins = research.diagnostics.iter().all(|d| {
            d.message.contains(MISSING_BUILTIN_MARKER) || d.message.starts_with("unknown name ")
        });
        if only_missing_builtins {
            failures.push(format!(
                "{name}: rejected only because a builtin is unimplemented:\n{}",
                render(&research.diagnostics)
            ));
        }
    }
    assert!(failures.is_empty(), "{}", failures.join("\n"));
}

/// v0.80: every library module checks and certifies all of its theorems.
#[test]
fn every_library_module_checks() {
    let mut failures = Vec::new();
    let mut count = 0usize;
    let std_dir = repo_root().join("lib").join("std");
    for entry in fs::read_dir(&std_dir).expect("read lib/std") {
        let path = entry.expect("dir entry").path();
        if path.extension().and_then(|e| e.to_str()) != Some("dlm") {
            continue;
        }
        count += 1;
        match load_program_with_paths(&path, &[repo_root().join("lib")]) {
            Ok(program) => {
                let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&program);
                if !report.ok() {
                    failures.push(format!("{}:\n{}", path.display(), render(&report.diagnostics)));
                }
                if report.certificates.is_empty() && report.typed.native.is_empty() {
                    failures.push(format!("{}: no certificates", path.display()));
                }
            }
            Err(diagnostics) => failures.push(format!("{} (load):\n{}", path.display(), render(&diagnostics))),
        }
    }
    assert!(count >= 2, "lib/std should contain the standard modules");
    assert!(failures.is_empty(), "library modules rejected:\n{}", failures.join("\n"));
}

#[test]
fn docs_manifest_lists_every_document_by_bare_name() {
    let docs_dir = repo_root().join("docs");
    let manifest = fs::read_to_string(docs_dir.join("MANIFEST.json")).expect("read docs/MANIFEST.json");
    assert!(
        !manifest.contains("\"docs/"),
        "docs/MANIFEST.json must reference documents by bare file name, not with a docs/ prefix"
    );

    let mut missing = Vec::new();
    for entry in fs::read_dir(&docs_dir).expect("read docs/") {
        let path = entry.expect("dir entry").path();
        if path.extension().and_then(|e| e.to_str()) != Some("md") {
            continue;
        }
        let name = path.file_name().and_then(|n| n.to_str()).unwrap_or_default().to_string();
        if !manifest.contains(&format!("\"{name}\"")) {
            missing.push(name);
        }
    }
    missing.sort();
    assert!(missing.is_empty(), "docs/MANIFEST.json is missing: {missing:?}");
}

/// v0.75: every ```dlm block in documents that describe the implemented surface
/// must check. Fragments without a `theory` are wrapped in one.
#[test]
fn documented_dlm_blocks_check() {
    let mut failures = Vec::new();
    for doc in ["README.md", "docs/ORDINARY_SURFACE.md", "docs/PROOF_SURFACE.md"] {
        let text = fs::read_to_string(repo_root().join(doc)).unwrap_or_else(|err| panic!("read {doc}: {err}"));
        let mut rest = text.as_str();
        let mut offset_line = 1usize;
        while let Some(start) = rest.find("```dlm\n") {
            let block_start = start + "```dlm\n".len();
            let Some(len) = rest[block_start..].find("```") else { break };
            let block = &rest[block_start..block_start + len];
            let line = offset_line + rest[..start].matches('\n').count();
            let body = if block.contains("theory ") { block.to_string() } else { format!("theory Doc {{\n{block}}}\n") };
            let source = if body.contains("module ") { body } else { format!("module doc\n\n{body}") };
            match parse_module(&source) {
                Ok(module) => {
                    let report = Checker::new().check_module(&module);
                    if !report.ok() {
                        failures.push(format!("{doc}:{line}:\n{}", render(&report.diagnostics)));
                    }
                }
                Err(diagnostics) => failures.push(format!("{doc}:{line} (parse):\n{}", render(&diagnostics))),
            }
            offset_line = line + block.matches('\n').count() + 1;
            rest = &rest[block_start + len + 3..];
        }
    }
    assert!(failures.is_empty(), "documented .dlm blocks fail to check:\n{}", failures.join("\n"));
}
