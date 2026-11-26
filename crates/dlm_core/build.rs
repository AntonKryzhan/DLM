use std::fmt::Write;
use std::path::PathBuf;
use std::process::Command;

#[path = "src/math_dispatch/config.rs"]
mod config;

fn main() {
    println!("cargo:rerun-if-changed=src/math_dispatch/config.rs");
    let mut header = String::new();
    for (name, value) in [
        ("MATMUL_BLOCK_WORK", config::MATMUL_BLOCK_WORK),
        ("MATMUL_AVX2_WORK", config::MATMUL_AVX2_WORK),
        ("MATMUL_MIN_COLUMNS", config::MATMUL_MIN_COLUMNS),
        ("MATMUL_MIN_INNER", config::MATMUL_MIN_INNER),
        ("MATMUL_BLOCK_COLUMNS", config::MATMUL_BLOCK_COLUMNS),
        ("FFT_TWIDDLE_BLOCK", config::FFT_TWIDDLE_BLOCK),
        ("CSR_PARALLEL_ROWS", config::CSR_PARALLEL_ROWS),
        ("CSR_PARALLEL_NNZ", config::CSR_PARALLEL_NNZ),
        ("DFT_MAX", config::DFT_MAX),
        ("NTT_MODULUS", config::NTT_MODULUS),
        ("NTT_ROOT", config::NTT_ROOT),
        ("NTT_MAX", config::NTT_MAX),
        ("NTT_DIRECT_FACTOR", config::NTT_DIRECT_FACTOR),
    ] {
        writeln!(header, "#define DLM_MATH_{name} {value}u").unwrap();
    }
    let out = PathBuf::from(std::env::var_os("OUT_DIR").expect("Cargo OUT_DIR"));
    std::fs::write(out.join("math_dispatch.h"), &header)
        .expect("write mathematical kernel configuration");

    let read = |file: &str| {
        let path = format!("src/math_compute/{file}");
        println!("cargo:rerun-if-changed={path}");
        std::fs::read_to_string(path).expect("read mathematical compute source")
    };
    let platform = read("vendor/vk_platform.h");
    let vulkan = read("vendor/vulkan_core.h").replace("#include \"vk_platform.h\"", &platform);
    let shaders = read("shaders/spv.inc");
    let executor = read("vulkan.c");
    let kernels = read("compute.c");
    let csr = read("csr.c");
    let source = format!(
        "#if defined(_WIN32) || defined(__linux__)\n#define VK_NO_PROTOTYPES\n{vulkan}\n#undef VK_NO_PROTOTYPES\n{shaders}\n#endif\n{executor}\n{kernels}\n{csr}\n"
    );
    std::fs::write(out.join("math_compute.c"), &source).expect("write embedded compute source");
    println!("cargo:rustc-check-cfg=cfg(dlm_math_compute)");
    link_executor(&out, &header, &source);
}

fn link_executor(out: &std::path::Path, header: &str, source: &str) {
    let target = std::env::var("TARGET").unwrap();
    let host = std::env::var("HOST").unwrap();
    let os = std::env::var("CARGO_CFG_TARGET_OS").unwrap();
    if std::env::var("CARGO_CFG_TARGET_POINTER_WIDTH").as_deref() != Ok("64")
        || (os != "linux" && (os != "windows" || !target.ends_with("-gnu")))
    {
        return;
    }

    let target_cc = format!("CC_{}", target.replace('-', "_"));
    let target_linker = format!(
        "CARGO_TARGET_{}_LINKER",
        target.replace('-', "_").to_uppercase()
    );
    for key in [&target_cc, &target_linker, "CC"] {
        println!("cargo:rerun-if-env-changed={key}");
    }
    let cc = std::env::var_os(target_cc)
        .or_else(|| std::env::var_os(target_linker))
        .or_else(|| (host == target).then(|| std::env::var_os("CC")).flatten())
        .unwrap_or_else(|| {
            if host == target {
                if os == "windows" { "gcc" } else { "cc" }.into()
            } else {
                format!("{target}-gcc").into()
            }
        });
    let Ok(machine) = Command::new(&cc).arg("-dumpmachine").output() else {
        return;
    };
    let Ok(machine) = std::str::from_utf8(&machine.stdout) else {
        return;
    };
    let arch = std::env::var("CARGO_CFG_TARGET_ARCH").unwrap();
    if !machine.starts_with(&arch)
        || (os == "windows" && !machine.contains("mingw") && !machine.contains("windows-gnu"))
        || (os == "linux" && !machine.contains("linux"))
        || (target.ends_with("musl") && !machine.contains("musl"))
    {
        return;
    }
    let standalone = out.join("math_compute_native.c");
    std::fs::write(
        &standalone,
        format!("#define DLM_MATH_STATIC\n{header}{source}"),
    )
    .expect("write interpreter compute source");
    let object = out.join("math_compute.o");
    let status = Command::new(&cc)
        .args([
            "-std=gnu11",
            "-O2",
            "-fno-fast-math",
            "-ffp-contract=off",
            "-fPIC",
            "-c",
        ])
        .arg(&standalone)
        .arg("-o")
        .arg(&object)
        .status()
        .expect("run mathematical compute C compiler");
    assert!(status.success(), "compile mathematical compute executor");
    let ar = Command::new(&cc)
        .arg("-print-prog-name=ar")
        .output()
        .expect("find C archiver");
    assert!(ar.status.success(), "find mathematical compute archiver");
    let ar = std::str::from_utf8(&ar.stdout)
        .expect("C archiver path")
        .trim();
    let status = Command::new(ar)
        .arg("crs")
        .arg(out.join("libdlm_math_compute.a"))
        .arg(object)
        .status()
        .expect("run mathematical compute archiver");
    assert!(status.success(), "archive mathematical compute executor");
    println!("cargo:rustc-cfg=dlm_math_compute");
    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rustc-link-lib=static=dlm_math_compute");
    if os == "linux" {
        for lib in ["dl", "pthread", "m"] {
            println!("cargo:rustc-link-lib={lib}");
        }
    }
}
