use super::*;
use std::path::{Path, PathBuf};
use std::process::Command;
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Artifact {
    Executable,
    Staticlib,
    Sharedlib,
    Plugin,
}
impl Artifact {
    pub fn parse(s: &str) -> Option<Self> {
        Some(match s {
            "executable" => Self::Executable,
            "staticlib" => Self::Staticlib,
            "sharedlib" => Self::Sharedlib,
            "plugin" => Self::Plugin,
            _ => return None,
        })
    }
}
#[derive(Debug, Clone)]
pub struct Options {
    pub artifact: Artifact,
    pub compiler: String,
    pub cpu: String,
    pub libraries: Vec<String>,
    pub library_dirs: Vec<PathBuf>,
    pub objects: Vec<PathBuf>,
    pub flags: Vec<String>,
    pub plugin_profile: Option<String>,
}
impl Default for Options {
    fn default() -> Self {
        Self {
            artifact: Artifact::Executable,
            compiler: crate::default_c_compiler(),
            cpu: "auto".into(),
            libraries: vec![],
            library_dirs: vec![],
            objects: vec![],
            flags: vec![],
            plugin_profile: None,
        }
    }
}
fn run(cmd: &mut Command) -> Result<(), Diagnostic> {
    let res = cmd
        .output()
        .map_err(|e| error(1, format!("cannot start compiler/tool: {e}")))?;
    if !res.status.success() {
        return Err(error(
            1,
            format!(
                "native build failed:\n{}",
                String::from_utf8_lossy(&res.stderr)
            ),
        ));
    }
    Ok(())
}
pub fn build(m: &Mir, out: &Path, options: &Options, c_only: bool) -> Result<PathBuf, Diagnostic> {
    // Verify a second time at the backend trust boundary: public Rust callers
    // cannot forge a MIR to bypass source ownership or representation checks.
    let verified = check::check(m.unit.clone()).map_err(|d| d.into_iter().next().unwrap())?;
    if &verified != m {
        return Err(error(1, "forged or inconsistent native MIR"));
    }
    let profile = match options.cpu.as_str() {
        "auto" => 0,
        "scalar" => 1,
        "sse2" => 2,
        "avx2" => 3,
        "avx512" => 4,
        _ => {
            return Err(error(
                1,
                "CPU profile must be auto, scalar, sse2, avx2 or avx512",
            ))
        }
    };
    if options.artifact == Artifact::Plugin && options.plugin_profile.as_deref() != Some("c-abi-v1")
    {
        return Err(error(1, "plugin target requires --plugin-profile c-abi-v1"));
    }
    let triple = if c_only {
        if cfg!(windows) {
            "x86_64-windows".to_string()
        } else if cfg!(target_os = "macos") {
            "x86_64-darwin".to_string()
        } else {
            "x86_64-linux".to_string()
        }
    } else {
        let target = Command::new(&options.compiler)
            .arg("-dumpmachine")
            .output()
            .map_err(|e| error(1, e.to_string()))?;
        if !target.status.success() {
            return Err(error(1, "C compiler does not report a target triple"));
        }
        let triple = String::from_utf8_lossy(&target.stdout).into_owned();
        if !triple.contains("x86_64") && !triple.contains("amd64") {
            return Err(error(1, "the native ABI profile currently requires x86-64"));
        }
        triple
    };
    let windows = triple.contains("mingw") || triple.contains("windows");
    let dest = if options.artifact == Artifact::Plugin {
        std::fs::create_dir_all(out).map_err(|e| error(1, e.to_string()))?;
        out.join(if windows {
            "module.dll"
        } else if triple.contains("darwin") {
            "module.dylib"
        } else {
            "module.so"
        })
    } else {
        out.to_path_buf()
    };
    if let Some(parent) = dest.parent().filter(|p| !p.as_os_str().is_empty()) {
        std::fs::create_dir_all(parent).map_err(|e| error(1, e.to_string()))?;
    }
    let cpath = PathBuf::from(format!("{}.c", dest.display()));
    let hpath = PathBuf::from(format!("{}.h", dest.display()));
    std::fs::write(
        &cpath,
        emit::emit(m, options.artifact == Artifact::Executable),
    )
    .map_err(|e| error(1, e.to_string()))?;
    std::fs::write(&hpath, emit::header(m)).map_err(|e| error(1, e.to_string()))?;
    if c_only {
        return Ok(cpath);
    }
    let binary =
        if windows && options.artifact == Artifact::Executable && dest.extension().is_none() {
            dest.with_extension("exe")
        } else {
            dest.clone()
        };
    let mut cc = Command::new(&options.compiler);
    cc.args([
        "-std=gnu11",
        "-O3",
        "-ftree-vectorize",
        "-fvisibility=hidden",
    ])
    .arg(format!("-DDN_CPU_PROFILE={profile}"));
    if options.artifact != Artifact::Executable && !windows {
        cc.arg("-fPIC");
    }
    cc.args(&options.flags);
    cc.args(["-fno-fast-math", "-ffp-contract=off"]);
    let object = dest.with_extension("native.o");
    match options.artifact {
        Artifact::Sharedlib | Artifact::Plugin => {
            cc.arg(if triple.contains("darwin") {
                "-dynamiclib"
            } else {
                "-shared"
            });
        }
        Artifact::Staticlib => {
            cc.arg("-c");
        }
        _ => {}
    }
    cc.arg(&cpath)
        .arg("-o")
        .arg(if options.artifact == Artifact::Staticlib {
            &object
        } else {
            &binary
        });
    if options.artifact != Artifact::Staticlib {
        cc.args(&options.objects);
        for d in &options.library_dirs {
            cc.arg("-L").arg(d);
        }
        for l in &options.libraries {
            cc.arg(format!("-l{l}"));
        }
        if windows {
            cc.args(["-lshell32", "-lws2_32", "-lgdi32", "-luser32"]);
        } else {
            cc.args(["-pthread", "-lm", "-ldl"]);
            if triple.contains("linux")
                && matches!(options.artifact, Artifact::Sharedlib | Artifact::Plugin)
            {
                cc.args(["-Wl,-z,now", "-Wl,-Bsymbolic-functions"]);
            }
        }
    }
    run(&mut cc)?;
    if options.artifact == Artifact::Staticlib {
        let ar = std::env::var("AR").unwrap_or_else(|_| "ar".into());
        let temporary = dest.with_extension(format!("{}.new.a", std::process::id()));
        if temporary.exists() {
            std::fs::remove_file(&temporary).map_err(|e| error(1, e.to_string()))?;
        }
        let mut cmd = Command::new(ar);
        cmd.arg("rcs")
            .arg(&temporary)
            .arg(&object)
            .args(&options.objects);
        run(&mut cmd)?;
        if cfg!(windows) && dest.exists() {
            std::fs::remove_file(&dest).map_err(|e| error(1, e.to_string()))?;
        }
        std::fs::rename(&temporary, &dest).map_err(|e| error(1, e.to_string()))?;
        std::fs::remove_file(object).map_err(|e| error(1, e.to_string()))?;
    }
    if options.artifact == Artifact::Plugin {
        let exports = m
            .unit
            .functions
            .iter()
            .filter_map(|f| f.export.as_ref())
            .map(|s| format!("\"{s}\""))
            .collect::<Vec<_>>()
            .join(",");
        std::fs::write(out.join("manifest.json"),format!("{{\n  \"profile\":\"c-abi-v1\",\n  \"abi_version\":1,\n  \"module\":\"{}\",\n  \"exports\":[{exports}]\n}}\n",dest.file_name().unwrap().to_string_lossy())).map_err(|e|error(1,e.to_string()))?;
    }
    Ok(binary)
}
