use std::{env, path::PathBuf, process::Command};
fn main() {
    let root = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").unwrap());
    let out = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    let windows = env::var("CARGO_CFG_TARGET_OS").unwrap() == "windows";
    let object = out.join("gateway_native.o");
    let library = out.join("libgateway_native.a");
    for file in [
        "native.c",
        "../dlm_core/src/native/network.c",
        "../dlm_core/src/native/nodes.c",
        "../dlm_core/src/native/adaptive.c",
        "../dlm_core/src/native/tun.c",
    ] {
        println!("cargo:rerun-if-changed={}", root.join(file).display());
    }
    for variable in ["CC", "AR"] {
        println!("cargo:rerun-if-env-changed={variable}");
    }
    let mut cc = Command::new(env::var_os("CC").unwrap_or_else(|| "gcc".into()));
    cc.args([
        "-std=gnu11",
        "-O2",
        "-fPIC",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wno-misleading-indentation",
    ])
    .arg("-c")
    .arg(root.join("native.c"))
    .arg("-o")
    .arg(&object);
    assert!(
        cc.status().expect("start gateway C compiler").success(),
        "gateway native compilation failed"
    );
    assert!(
        Command::new(env::var_os("AR").unwrap_or_else(|| "ar".into()))
            .arg("crs")
            .arg(&library)
            .arg(&object)
            .status()
            .expect("start archiver")
            .success()
    );
    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rustc-link-lib=static=gateway_native");
    if windows {
        println!("cargo:rustc-link-lib=ws2_32");
    } else {
        for name in ["dl", "m", "pthread"] {
            println!("cargo:rustc-link-lib={name}");
        }
    }
}
