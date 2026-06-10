use std::env;
use std::fs;
use std::process;

use dlm_core::{parse_module, CheckPolicy, Checker, Runtime, SoundnessSummary};

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
        "--version" | "-V" => {
            println!("dlm 0.27.0-mvp");
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
            "--stdin" => {
                i += 1;
                if i >= args.len() {
                    eprintln!("error: --stdin expects text argument");
                    print_usage_and_exit(2);
                }
                stdin = args[i].clone();
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

fn load_module(path: &str) -> dlm_core::Module {
    let source = match fs::read_to_string(path) {
        Ok(source) => source,
        Err(err) => {
            eprintln!("error: cannot read '{path}': {err}");
            process::exit(2);
        }
    };

    match parse_module(&source) {
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
    let module = load_module(path);
    let report = Checker::with_policy(policy).check_module(&module);
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
    let module = load_module(path);
    let report = Checker::with_policy(policy).check_module(&module);
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

fn run_program(path: &str, stdin: String, policy: CheckPolicy) {
    let module = load_module(path);

    let check_report = Checker::with_policy(policy).check_module(&module);
    if !check_report.ok() {
        eprintln!("DLM run blocked by check errors: {path}\n");
        for diagnostic in check_report.diagnostics {
            eprint!("{diagnostic}");
        }
        process::exit(1);
    }

    match Runtime::with_stdin(stdin).run_module(&module) {
        Ok(report) => {
            println!("DLM run: {path}\n");
            println!("OK");
            println!("module: {}", report.module_name);
            println!("theories: {}", report.theory_count);
            println!("values evaluated: {}", report.values_evaluated);
            if !report.output.is_empty() {
                println!("\nprogram output:");
                for line in report.output {
                    println!("{line}");
                }
            }
        }
        Err(diagnostic) => {
            eprintln!("DLM runtime error: {path}\n");
            eprint!("{diagnostic}");
            process::exit(1);
        }
    }
}

fn print_usage_and_exit(code: i32) -> ! {
    eprintln!("DLM / ЯРД MVP checker + exact-runtime prototype");
    eprintln!("usage:");
    eprintln!("  dlm check [--trusted-only|--allow-axioms|--allow-unsafe] <file.dlm>");
    eprintln!("  dlm run [--trusted-only|--allow-axioms|--allow-unsafe] <file.dlm> [--stdin <text>]");
    eprintln!("  dlm explain [--trusted-only|--allow-axioms|--allow-unsafe] <file.dlm>");
    eprintln!("  dlm --version");
    process::exit(code);
}
