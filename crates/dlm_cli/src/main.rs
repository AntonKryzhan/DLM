mod math_commands;
mod rational_commands;
mod q_witness_commands;

use std::env;
use std::fs;
use std::process;

use dlm_core::{export_certificate_text, parse_module_with_source, CheckPolicy, Checker, FileId, Runtime, SoundnessSummary, SourceFile};

/// v0.80: library directories from `--lib` (searched after the importer's
/// and the root's directories, before `DLM_PATH`).
static LIB_PATHS: std::sync::Mutex<Vec<std::path::PathBuf>> = std::sync::Mutex::new(Vec::new());

/// v0.83: program arguments after `--` (`args()`).
static PROGRAM_ARGS: std::sync::Mutex<Vec<String>> = std::sync::Mutex::new(Vec::new());

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        print_usage_and_exit(2);
    }

    match args[1].as_str() {
        "check" => {
            let (policy, path) = parse_check_args(&args[2..]);
            run_check(&path, policy);
        }
        "run" => {
            let (policy, path, stdin) = parse_run_args(&args[2..]);
            run_program(&path, stdin, policy);
        }
        "explain" => {
            let (policy, path) = parse_check_args(&args[2..]);
            run_explain(&path, policy);
        }
        "certify" => {
            let (policy, path) = parse_check_args(&args[2..]);
            run_certify(&path, policy);
        }
        "proof-export" if args.len() >= 4 => {
            let (policy, path) = parse_check_args(&args[2..args.len() - 1]);
            run_proof_export(&path, args.last().unwrap(), policy);
        }
        "proof-check" if args.len() == 3 => run_proof_check(&args[2]),
        "homology" if args.len() == 5 && args[2] == "--prime" => math_commands::homology_prime(&args[3], &args[4]),
        "homology" if args.len() == 3 => run_homology(&args[2]),
        "homology-export" if args.len() == 4 => math_commands::export(&args[2], &args[3]),
        "homology-check" if args.len() == 3 || args.len() == 4 => math_commands::check(&args[2], args.get(3).map(String::as_str)),
        "matrix-q" => rational_commands::run(&args[2..]),
        "matrix-q-export" if args.len() == 4 => q_witness_commands::export(&args[2], &args[3]),
        "matrix-q-check" if args.len() == 3 || args.len() == 4 => q_witness_commands::check(&args[2], args.get(3).map(String::as_str)),
        "audit" => {
            let (policy, path) = parse_check_args(&args[2..]);
            run_audit(&path, policy);
        }
        "contracts" => {
            let (policy, path) = parse_check_args(&args[2..]);
            run_contracts(&path, policy);
        }
        "project" => run_project(&args[2..]),
        "build" => run_build(&args[2..]),
        "--version" | "-V" => {
            println!("dlm {}", env!("CARGO_PKG_VERSION"));
        }
        "help" | "--help" | "-h" => print_usage_and_exit(0),
        other => {
            eprintln!("error: unknown command '{other}'");
            print_usage_and_exit(2);
        }
    }
}

fn parse_check_args(args: &[String]) -> (CheckPolicy, String) {
    let mut policy = CheckPolicy::research();
    let mut path: Option<String> = None;

    let mut i = 0usize;
    while i < args.len() {
        match args[i].as_str() {
            "--trusted-only" | "--no-axioms" => policy = CheckPolicy::trusted_only(),
            "--allow-axioms" | "--research" => policy = CheckPolicy::research(),
            "--allow-unsafe" => policy = CheckPolicy::allow_unsafe(),
            "--lib" => {
                i += 1;
                if i >= args.len() {
                    eprintln!("error: --lib expects a directory");
                    print_usage_and_exit(2);
                }
                LIB_PATHS.lock().expect("lib paths").push(std::path::PathBuf::from(&args[i]));
            }
            flag if flag.starts_with('-') => {
                eprintln!("error: unsupported check option '{flag}'");
                print_usage_and_exit(2);
            }
            file => {
                if path.is_some() {
                    eprintln!("error: dlm check expects one .dlm file path");
                    print_usage_and_exit(2);
                }
                path = Some(file.to_string());
            }
        }
        i += 1;
    }

    let Some(path) = path else {
        eprintln!("error: dlm check expects one .dlm file path");
        print_usage_and_exit(2);
    };
    (policy, path)
}

static INTERACTIVE: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

fn parse_run_args(args: &[String]) -> (CheckPolicy, String, String) {
    let mut policy = CheckPolicy::research();
    let mut path: Option<String> = None;
    let mut stdin = String::new();

    let mut i = 0usize;
    while i < args.len() {
        match args[i].as_str() {
            "--trusted-only" | "--no-axioms" => policy = CheckPolicy::trusted_only(),
            "--allow-axioms" | "--research" => policy = CheckPolicy::research(),
            "--allow-unsafe" => policy = CheckPolicy::allow_unsafe(),
            "--lib" => {
                i += 1;
                if i >= args.len() {
                    eprintln!("error: --lib expects a directory");
                    print_usage_and_exit(2);
                }
                LIB_PATHS.lock().expect("lib paths").push(std::path::PathBuf::from(&args[i]));
            }
            // v0.125: live stdin and streamed output for long-running loops
            "--interactive" => INTERACTIVE.store(true, std::sync::atomic::Ordering::Relaxed),
            "--stdin" => {
                i += 1;
                if i >= args.len() {
                    eprintln!("error: --stdin expects text argument");
                    print_usage_and_exit(2);
                }
                stdin = args[i].clone();
            }
            "--stdin-file" => {
                i += 1;
                if i >= args.len() {
                    eprintln!("error: --stdin-file expects a path");
                    print_usage_and_exit(2);
                }
                stdin = match fs::read_to_string(&args[i]) {
                    Ok(text) => text,
                    Err(err) => {
                        eprintln!("error: cannot read '{}': {err}", args[i]);
                        process::exit(2);
                    }
                };
            }
            "--" => {
                PROGRAM_ARGS.lock().expect("program args").extend(args[i + 1..].iter().cloned());
                break;
            }
            flag if flag.starts_with('-') => {
                eprintln!("error: unsupported run option '{flag}'");
                print_usage_and_exit(2);
            }
            file => {
                if path.is_some() {
                    eprintln!("error: dlm run expects one .dlm file path");
                    print_usage_and_exit(2);
                }
                path = Some(file.to_string());
            }
        }
        i += 1;
    }

    let Some(path) = path else {
        eprintln!("error: dlm run expects one .dlm file path");
        print_usage_and_exit(2);
    };
    (policy, path, stdin)
}

/// v0.80: the module at `path` and the modules it imports.
fn load_program(path: &str) -> dlm_core::Program {
    let module = load_module(path);
    if module.imports.is_empty() {
        return dlm_core::Program::single(module);
    }
    let mut search = LIB_PATHS.lock().expect("lib paths").clone();
    if let Some(value) = env::var_os("DLM_PATH") {
        search.extend(env::split_paths(&value));
    }
    match dlm_core::load_program_with_paths(std::path::Path::new(path), &search) {
        Ok(program) => program,
        Err(diagnostics) => {
            eprintln!("DLM import: {path}\n");
            for diagnostic in diagnostics {
                eprint!("{diagnostic}");
            }
            process::exit(1);
        }
    }
}

fn load_module(path: &str) -> dlm_core::Module {
    let source = match fs::read_to_string(path) {
        Ok(source) => source,
        Err(err) => {
            eprintln!("error: cannot read '{path}': {err}");
            process::exit(2);
        }
    };

    match parse_module_with_source(&source, SourceFile::named(FileId::ROOT, path)) {
        Ok(module) => module,
        Err(diagnostics) => {
            eprintln!("DLM parse: {path}\n");
            for diagnostic in diagnostics {
                eprint!("{diagnostic}");
            }
            process::exit(1);
        }
    }
}

fn run_check(path: &str, policy: CheckPolicy) {
    let program = load_program(path);
    let report = Checker::with_policy(policy).check_program(&program);
    println!("DLM check: {path}\n");
    if report.ok() {
        println!("OK");
        println!("module: {}", report.module_name);
        println!("theories: {}", report.theory_count);
        println!("values checked: {}", report.value_count);
        if !report.inferred.is_empty() {
            println!("\ninferred passports:");
            for (name, passport) in report.inferred {
                println!("  {name} : {passport}");
            }
        }
    } else {
        for diagnostic in report.diagnostics {
            eprint!("{diagnostic}");
        }
        process::exit(1);
    }
}

fn run_explain(path: &str, policy: CheckPolicy) {
    let program = load_program(path);
    let report = Checker::with_policy(policy).check_program(&program);
    println!("DLM explain: {path}\n");
    if report.ok() {
        println!("OK");
        let summary = SoundnessSummary::from_report(&report);
        println!("\n{}", summary.render_human());
    } else {
        eprintln!("DLM explain blocked by check errors: {path}\n");
        for diagnostic in report.diagnostics {
            eprint!("{diagnostic}");
        }
        process::exit(1);
    }
}

/// v0.76: check the module and print the proof certificate of every theorem
/// and axiom it declares.
fn run_certify(path: &str, policy: CheckPolicy) {
    let program = load_program(path);
    let report = Checker::with_policy(policy).check_program(&program);
    if !report.ok() {
        eprintln!("DLM certify blocked by check errors: {path}\n");
        for diagnostic in report.diagnostics {
            eprint!("{diagnostic}");
        }
        process::exit(1);
    }
    println!("DLM certify: {path}\n");
    println!("certificates: {}", report.certificates.len());
    for certificate in &report.certificates {
        match export_certificate_text(certificate, 0) {
            Ok(text) => {
                print!("\n{text}");
                // The core certificate records the proof trace; the evidence
                // method (computation / exhaustion / axiom) lives in the
                // theorem passport history.
                let key = format!("{}.{}", certificate.theory, certificate.theorem_name);
                if let Some((_, passport)) = report.inferred.iter().find(|(name, _)| name == &key) {
                    let own_suffix = format!(":theorem={}", certificate.theorem_name);
                    let own_axiom = format!("theorem:axiom:{}:", certificate.theorem_name);
                    for event in passport.history.events() {
                        if (event.starts_with("proof:") && event.ends_with(&own_suffix)) || event.starts_with(&own_axiom) {
                            println!("evidence: {event}");
                        } else if event.starts_with("crosscheck:") && event.ends_with(&own_suffix) {
                            println!("crosscheck: {event}");
                        } else if let Some(lemma) = event.strip_prefix("lemma:use:") {
                            println!("lemma: {lemma}");
                        }
                    }
                }
            }
            Err(diagnostic) => {
                eprint!("{diagnostic}");
                process::exit(1);
            }
        }
    }
}

/// Export exactly one independently replayable artifact, without CLI framing.
fn run_proof_export(path: &str, name: &str, policy: CheckPolicy) {
    let report = Checker::with_policy(policy).check_program(&load_program(path));
    if !report.ok() {
        for diagnostic in report.diagnostics { eprint!("{diagnostic}"); }
        process::exit(1);
    }
    let proof = report.certificates.iter().find(|c| format!("{}.{}", c.theory, c.theorem_name) == name).and_then(|c| c.kernel.as_ref());
    match proof {
        Some(proof) => print!("{}", proof.to_text()),
        None => {
            eprintln!("no independently replayable proof for '{name}'; use a qualified Theory.theorem in the supported Nat equational fragment");
            process::exit(1);
        }
    }
}

fn run_proof_check(path: &str) {
    use std::io::Read;
    use dlm_core::proof_kernel::{check_kernel_proof, KernelBudget, KernelContext, KernelProof};
    use dlm_core::proof_kernel::KernelErrorKind;
    let budget = KernelBudget::default();
    // v0.101 (C-min): exit 1 = the artifact is not a proof (Invalid), 3 = the
    // check ran out of budget (UnknownWithinBudget, not a refutation), 2 = the
    // file cannot be read as text.
    let mut text = String::new();
    let read = fs::File::open(path)
        .and_then(|file| file.take(budget.max_text_bytes as u64 + 1).read_to_string(&mut text));
    if let Err(e) = read {
        eprintln!("kernel proof check failed: cannot read {path}: {e}");
        process::exit(2);
    }
    let result = KernelProof::from_text(&text, budget).and_then(|proof| {
        let report = check_kernel_proof(&proof, &KernelContext::default(), budget)?;
        let proposition = proof.proposition(budget)?;
        Ok((report, proposition))
    });
    match result {
        Ok((report, proposition)) => {
            println!("kernel proof: checked\nproposition: {proposition}\nterms: {}\nsteps: {}\naxioms: []\nmodel: mathematical Nat; machine definedness is a separate obligation", report.checked_terms, report.checked_steps);
        }
        Err(error) => {
            eprintln!("kernel proof check failed: {error}");
            process::exit(if error.kind == KernelErrorKind::UnknownWithinBudget { 3 } else { 1 });
        }
    }
}

/// v0.81: print the trusted-base audit of the module (and its imports).
/// v0.87: `dlm contracts file.dlm`: per-function contracts with independent
/// statuses (effects, termination, machine arithmetic, stack, failures,
/// mathematical specification).
fn run_contracts(path: &str, policy: CheckPolicy) {
    let program = load_program(path);
    match dlm_core::function_contracts(&program, policy) {
        Ok(report) => print!("{}", report.render()),
        Err(diagnostics) => {
            eprintln!("DLM contracts blocked: {path}\n");
            for diagnostic in diagnostics {
                eprint!("{diagnostic}");
            }
            process::exit(1);
        }
    }
}

fn run_audit(path: &str, policy: CheckPolicy) {
    let program = load_program(path);
    let report = Checker::with_policy(policy).check_program(&program);
    if !report.ok() {
        eprintln!("DLM audit blocked by check errors: {path}\n");
        for diagnostic in report.diagnostics {
            eprint!("{diagnostic}");
        }
        process::exit(1);
    }
    let imports = program.dependencies().iter().map(|m| m.name.clone()).collect();
    let audit = dlm_core::audit_check_report(&report, imports);
    print!("{}", dlm_core::render_program_audit(&audit));
}

/// v0.84: `dlm build file.dlm [-o out] [--emit-ir] [--c-only] [--lib DIR]`:
/// check, lower to IR, emit C and compile it with the system C compiler.
fn run_build(args: &[String]) {
    let mut policy = CheckPolicy::research();
    let mut path: Option<String> = None;
    let mut output: Option<String> = None;
    let mut emit_ir = false;
    let mut c_only = false;
    let mut check_overflow = false;
    let mut require_no_overflow = false;
    let mut i = 0usize;
    while i < args.len() {
        match args[i].as_str() {
            "--trusted-only" | "--no-axioms" => policy = CheckPolicy::trusted_only(),
            "--allow-axioms" | "--research" => policy = CheckPolicy::research(),
            "--emit-ir" => emit_ir = true,
            "--c-only" => c_only = true,
            "--check-overflow" => check_overflow = true,
            "--require-no-overflow" => { check_overflow = true; require_no_overflow = true; }
            "-o" | "--lib" => {
                i += 1;
                let Some(value) = args.get(i) else {
                    eprintln!("error: {} expects a value", args[i - 1]);
                    print_usage_and_exit(2);
                };
                if args[i - 1] == "-o" {
                    output = Some(value.clone());
                } else {
                    LIB_PATHS.lock().expect("lib paths").push(std::path::PathBuf::from(value));
                }
            }
            flag if flag.starts_with('-') => {
                eprintln!("error: unsupported build option '{flag}'");
                print_usage_and_exit(2);
            }
            file => {
                if path.is_some() {
                    eprintln!("error: dlm build expects one .dlm file path");
                    print_usage_and_exit(2);
                }
                path = Some(file.to_string());
            }
        }
        i += 1;
    }
    let Some(path) = path else {
        eprintln!("error: dlm build expects one .dlm file path");
        print_usage_and_exit(2);
    };
    let program = load_program(&path);
    let ir = match dlm_core::lower_program(&program, policy) {
        Ok(ir) => ir,
        Err(diagnostics) => {
            eprintln!("DLM build failed: {path}\n");
            for diagnostic in diagnostics {
                eprint!("{diagnostic}");
            }
            process::exit(1);
        }
    };
    if check_overflow {
        let report = dlm_core::check_machine_arithmetic(&program, policy).expect("program was checked above");
        print!("{}", report.render());
        if require_no_overflow && !report.verified() {
            eprintln!("error: machine arithmetic obligations remain open; mathematical proofs alone do not establish 128-bit execution safety");
            process::exit(1);
        }
    }
    if emit_ir {
        print!("{}", ir.render());
    }
    let output = output.unwrap_or_else(|| {
        std::path::Path::new(&path).with_extension("").to_string_lossy().into_owned()
    });
    let c_path = format!("{output}.c");
    let c_source = dlm_core::emit_c(&ir);
    if c_only {
        if let Err(err) = fs::write(&c_path, c_source) {
            eprintln!("error: cannot write {c_path}: {err}");
            process::exit(1);
        }
        println!("DLM build: {path} -> {c_path}");
        return;
    }
    let compiler = dlm_core::default_c_compiler();
    match dlm_core::build_c_executable(&c_source, std::path::Path::new(&c_path), std::path::Path::new(&output), &compiler) {
        Ok(executable) => {
            println!("DLM build: {path} -> {}", executable.display());
            println!("functions: {}  globals: {}  erased statements: {}", ir.functions.len(), ir.globals.len(), ir.erased_statements);
        }
        Err(diagnostic) => {
            eprint!("{diagnostic}");
            process::exit(1);
        }
    }
}

/// v0.81: check every root of a project (`dlm.project`).
fn run_project(args: &[String]) {
    let mut trusted_only = None;
    let mut with_audit = false;
    let mut path: Option<String> = None;
    for arg in args {
        match arg.as_str() {
            "--trusted-only" | "--no-axioms" => trusted_only = Some(true),
            "--allow-axioms" | "--research" => trusted_only = Some(false),
            "--audit" => with_audit = true,
            flag if flag.starts_with('-') => {
                eprintln!("error: unsupported project option '{flag}'");
                print_usage_and_exit(2);
            }
            other => {
                if path.is_some() {
                    eprintln!("error: dlm project expects one directory or dlm.project path");
                    print_usage_and_exit(2);
                }
                path = Some(other.to_string());
            }
        }
    }
    let path = path.unwrap_or_else(|| ".".to_string());
    let manifest = match dlm_core::load_project(std::path::Path::new(&path)) {
        Ok(manifest) => manifest,
        Err(diagnostic) => {
            eprint!("{diagnostic}");
            process::exit(2);
        }
    };
    let report = dlm_core::check_project(&manifest, trusted_only);
    print!("{}", report.render());
    for entry in &report.entries {
        if !entry.ok() {
            eprintln!("\n{}:", entry.path.display());
            for diagnostic in &entry.diagnostics {
                eprint!("{diagnostic}");
            }
        } else if with_audit {
            if let Some(audit) = &entry.audit {
                print!("\n{}", dlm_core::render_program_audit(audit));
            }
        }
    }
    if !report.ok() {
        process::exit(1);
    }
}

fn run_program(path: &str, stdin: String, policy: CheckPolicy) {
    let program = load_program(path);

    let check_report = Checker::with_policy(policy).check_program(&program);
    if !check_report.ok() {
        eprintln!("DLM run blocked by check errors: {path}\n");
        for diagnostic in check_report.diagnostics {
            eprint!("{diagnostic}");
        }
        process::exit(1);
    }

    let program_args = PROGRAM_ARGS.lock().expect("program args").clone();
    let interactive = INTERACTIVE.load(std::sync::atomic::Ordering::Relaxed);
    let runtime = Runtime::with_stdin(stdin).with_args(program_args);
    let runtime = if interactive { runtime.interactive() } else { runtime };
    match runtime.run_program_partial(&program) {
        Ok(report) if interactive => {
            if let Some(main) = &report.main {
                eprintln!("main: {}", main.display_atom());
            }
            let exit_code = report.exit_code();
            if exit_code != 0 {
                process::exit(exit_code);
            }
        }
        Err(failure) if interactive => {
            eprintln!("DLM runtime error: {path}\n");
            eprint!("{}", failure.diagnostic);
            process::exit(1);
        }
        Ok(report) => {
            println!("DLM run: {path}\n");
            println!("OK");
            println!("module: {}", report.module_name);
            println!("theories: {}", report.theory_count);
            println!("values evaluated: {}", report.values_evaluated);
            let exit_code = report.exit_code();
            if !report.output.is_empty() {
                println!("\nprogram output:");
                for line in &report.output {
                    println!("{line}");
                }
            }
            if let Some(main) = &report.main {
                println!("\nmain: {}", main.display_atom());
            }
            if exit_code != 0 {
                process::exit(exit_code);
            }
        }
        Err(failure) => {
            // v0.87: what the program printed before the error is part of its
            // observable behaviour (as in compiled programs).
            if !failure.output.is_empty() {
                println!("DLM run: {path}\n");
                println!("program output (before the runtime error):");
                for line in &failure.output {
                    println!("{line}");
                }
            }
            eprintln!("DLM runtime error: {path}\n");
            eprint!("{}", failure.diagnostic);
            process::exit(1);
        }
    }
}

fn print_usage_and_exit(code: i32) -> ! {
    eprintln!("DLM / ЯРД MVP checker + exact-runtime prototype");
    eprintln!("usage:");
    eprintln!("  dlm check [--trusted-only|--allow-axioms|--allow-unsafe] <file.dlm>");
    eprintln!(
        "  dlm run [--trusted-only|--allow-axioms|--allow-unsafe] <file.dlm> [--stdin <text>|--stdin-file <path>|--interactive] [-- args...]"
    );
    eprintln!("  dlm explain [--trusted-only|--allow-axioms|--allow-unsafe] <file.dlm>");
    eprintln!("  dlm certify [--trusted-only|--allow-axioms|--allow-unsafe] <file.dlm>");
    eprintln!("  dlm proof-export [--trusted-only] <file.dlm> <Theory.theorem>");
    eprintln!("  dlm proof-check <proof.kproof>");
    eprintln!("  dlm matrix-q-export <input.qmat> <out.qproof>");
    eprintln!("  dlm matrix-q-check <proof.qproof> [expected.qmat]");
    eprintln!("  dlm matrix-q [--bits N] [--work N] [--inverse] [--solve rhs.qmat] <matrix.qmat>");
    eprintln!("  dlm homology --prime <p> <complex.simp>  # oriented Fp runtime computation");
    eprintln!("  dlm homology <complex.simp>  # exact F2 runtime computation");
    eprintln!("  dlm homology-export <complex.simp> <out.f2proof>");
    eprintln!("  dlm homology-check <proof.f2proof> [expected-complex.simp]");
    eprintln!("  dlm audit [--trusted-only|--allow-axioms|--allow-unsafe] <file.dlm>");
    eprintln!("  dlm contracts [--trusted-only|--allow-axioms|--allow-unsafe] [--lib DIR] <file.dlm>");
    eprintln!("  dlm build [--trusted-only] [-o <out>] [--emit-ir] [--c-only] [--check-overflow] [--require-no-overflow] <file.dlm>");
    eprintln!("  dlm project [--trusted-only|--allow-axioms] [--audit] [<dir>|<dlm.project>]");
    eprintln!("  dlm --version");
    eprintln!("options:");
    eprintln!("  --lib <dir>   search <dir> for imported modules (repeatable; also DLM_PATH)");
    process::exit(code);
}

fn run_homology(path: &str) {
    use dlm_core::math::{MathError, MathResult, topology::SimplicialComplex};
    let result = (|| -> MathResult<()> {
        // Bound the read itself, including non-regular streams.
        use std::io::Read;
        let mut bytes = Vec::new();
        fs::File::open(path).map_err(|e| MathError(e.to_string()))?
            .take(65537).read_to_end(&mut bytes).map_err(|e| MathError(e.to_string()))?;
        if bytes.len() > 65536 { return Err(MathError("complex input byte budget exceeded".into())); }
        let text = std::str::from_utf8(&bytes).map_err(|e| MathError(e.to_string()))?;
        let complex = SimplicialComplex::parse(text)?;
        let chains = complex.chains()?;
        println!("coefficients: F2; assurance: runtime-checked; homology: ordinary");
        println!("chain dimensions: {:?}", chains.dimensions());
        println!("boundary squared: zero");
        for n in 0..chains.dimensions().len() {
            let h = chains.homology(n)?;
            let co = chains.cohomology(n)?;
            println!("H_{n}: dimension {}; H^{n}: dimension {}", h.betti, co.betti);
            for c in 0..h.representatives.cols() {
                let terms: Vec<_> = complex.simplices()[n].iter().enumerate()
                    .filter(|(r, _)| h.representatives.entries()[r*h.representatives.cols()+c] == 1)
                    .map(|(_, s)| format!("{:?}", s)).collect();
                println!("  cycle {c}: {}", terms.join(" + "));
            }
        }
        Ok(())
    })();
    if let Err(e) = result { eprintln!("homology error: {e}"); process::exit(1); }
}
