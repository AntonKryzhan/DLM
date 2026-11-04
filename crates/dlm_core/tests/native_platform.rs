//! Host-side acceptance fixtures. The same sources run on Linux and Windows.
use dlm_core::{lower_program, native, CheckPolicy};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicUsize, Ordering};
static SERIAL: AtomicUsize = AtomicUsize::new(0);
fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}
fn directory() -> PathBuf {
    let p = std::env::temp_dir().join(format!(
        "dlm-platform-{}-{}",
        std::process::id(),
        SERIAL.fetch_add(1, Ordering::Relaxed)
    ));
    std::fs::create_dir_all(&p).unwrap();
    p
}
fn mir(example: &str) -> native::Mir {
    let r = root();
    let p = dlm_core::program::load_program_with_paths(
        &r.join("examples/native").join(example),
        &[r.join("lib")],
    )
    .unwrap();
    lower_program(&p, CheckPolicy::allow_unsafe())
        .unwrap()
        .native
}
fn compile(source: &Path, out: &Path, flags: &[&str]) {
    let result = Command::new(dlm_core::default_c_compiler())
        .args(["-std=gnu11", "-O2"])
        .arg(source)
        .args(flags)
        .arg("-o")
        .arg(out)
        .output()
        .unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
}
fn execute(exe: &Path, module: &Path) {
    let result = Command::new(exe).arg(module).output().unwrap();
    assert!(
        result.status.success(),
        "stdout={}\nstderr={}",
        String::from_utf8_lossy(&result.stdout),
        String::from_utf8_lossy(&result.stderr)
    );
    println!("{}", String::from_utf8_lossy(&result.stdout));
}
#[test]
fn managed_callbacks_own_context_until_cancellation_finishes() {
    let m = mir("managed_callbacks.dlm");
    let d = directory();
    let fixture = root().join("crates/dlm_core/tests/native_hosts");
    let object = d.join("callbacks.o");
    compile(&fixture.join("callbacks.c"), &object, &["-c", "-fPIC"]);
    let module = d.join(if cfg!(windows) {
        "callbacks.dll"
    } else {
        "callbacks.so"
    });
    let options = native::build::Options {
        artifact: native::build::Artifact::Sharedlib,
        objects: vec![object],
        ..Default::default()
    };
    native::build::build(&m, &module, &options, false).unwrap();
    let host = d.join(if cfg!(windows) { "host.exe" } else { "host" });
    compile(
        &fixture.join("callback_host.c"),
        &host,
        if cfg!(windows) { &[] } else { &["-ldl"] },
    );
    execute(&host, &module);
    std::fs::remove_dir_all(d).unwrap();
}
#[test]
fn xml_parser_and_writer_match_reference() {
    let m = mir("xml.dlm");
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(vm.output, ["XML OK"]);
    let d = directory();
    let exe = d.join(if cfg!(windows) { "xml.exe" } else { "xml" });
    native::build::build(&m, &exe, &Default::default(), false).unwrap();
    let result = Command::new(exe).output().unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    assert_eq!(
        String::from_utf8_lossy(&result.stdout).replace("\r\n", "\n"),
        "XML OK\n"
    );
    std::fs::remove_dir_all(d).unwrap();
}
#[test]
#[ignore = "requires a live X11 or Win32 desktop and OpenGL"]
fn editor_embeds_in_host_and_preserves_host_state() {
    let m = mir("embedded_editor.dlm");
    let d = directory();
    if cfg!(windows) {
        if let Some(driver) = std::env::var_os("DLM_TEST_OPENGL_DLL") {
            std::fs::copy(driver, d.join("opengl32.dll")).unwrap();
        }
    }
    let module = d.join(if cfg!(windows) {
        "editor.dll"
    } else {
        "editor.so"
    });
    let mut options = native::build::Options {
        artifact: native::build::Artifact::Sharedlib,
        ..Default::default()
    };
    if cfg!(windows) {
        let fixture = root().join("crates/dlm_core/tests/native_hosts");
        let object = d.join("gui_alloc.o");
        compile(&fixture.join("gui_alloc.c"), &object, &["-c"]);
        options.objects.push(object);
        options.flags.extend([
            "-include".into(),
            fixture.join("gui_alloc.h").to_str().unwrap().into(),
        ]);
    }
    native::build::build(&m, &module, &options, false).unwrap();
    let host = d.join(if cfg!(windows) {
        "editor_host.exe"
    } else {
        "editor_host"
    });
    let fixture = root().join("crates/dlm_core/tests/native_hosts/editor_host.c");
    compile(
        &fixture,
        &host,
        if cfg!(windows) {
            &["-luser32", "-lgdi32", "-lopengl32"]
        } else {
            &["-lX11", "-lGL", "-ldl", "-pthread", "-lm"]
        },
    );
    execute(&host, &module);
    std::fs::remove_dir_all(d).unwrap();
}

#[test]
fn versioned_settings_migrate_and_preserve_unknown_fields() {
    let m = mir("versioned_state.dlm");
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(vm.output, ["versioned settings OK"]);
    let d = directory();
    let exe = d.join(if cfg!(windows) {
        "settings.exe"
    } else {
        "settings"
    });
    native::build::build(&m, &exe, &Default::default(), false).unwrap();
    let result = Command::new(exe).output().unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    assert_eq!(
        String::from_utf8_lossy(&result.stdout).replace("\r\n", "\n"),
        "versioned settings OK\n"
    );
    std::fs::remove_dir_all(d).unwrap();
}

#[test]
fn component_function_table_and_hot_loop_have_stable_abi() {
    let m = mir("component.dlm");
    let d = directory();
    let module = d.join(if cfg!(windows) {
        "component.dll"
    } else {
        "component.so"
    });
    let options = native::build::Options {
        artifact: native::build::Artifact::Sharedlib,
        ..Default::default()
    };
    native::build::build(&m, &module, &options, false).unwrap();
    let host = d.join(if cfg!(windows) {
        "component_host.exe"
    } else {
        "component_host"
    });
    compile(
        &root().join("crates/dlm_core/tests/native_hosts/component_host.c"),
        &host,
        if cfg!(windows) { &[] } else { &["-ldl"] },
    );
    execute(&host, &module);
    std::fs::remove_dir_all(d).unwrap();
}

#[test]
fn executable_static_shared_plugin_and_cpp_header_are_portable() {
    let program=dlm_core::Program::single(dlm_core::parse_module("module portable\nnative {trust \"portable.fixture\"; record Packet repr(C,version=2){tag:U32;value:F64;} extern \"C\" fn reflect_external(packet:Packet,data_:Ptr<U32>,n:USize)->Packet from \"portable.fixture\" guarantees(NoAlloc,NoBlocking,NoIO,NoUnwind,BoundedWork,ThreadSafe); export fn ping(x:U32)->U32{return wrapping_add(x,U32(1));} export fn reflect(packet:Packet)->Packet{return packet;} export unsafe fn roundtrip(packet:Packet,data_:Ptr<U32>,n:USize)->Packet{return reflect_external(packet,data_,n);} fn main()->Unit{assert(ping(U32(4))==U32(5));}}\n").unwrap());
    let m = lower_program(&program, CheckPolicy::allow_unsafe())
        .unwrap()
        .native;
    let d = directory();
    let foreign = d.join("foreign.c");
    std::fs::write(&foreign,"#include <stdint.h>\n#include <stddef.h>\ntypedef struct {uint32_t tag;double value;} Packet;Packet reflect_external(Packet p,uint32_t *data,size_t n){p.tag+=(uint32_t)n;p.value+=n?data[0]:0;return p;}\n").unwrap();
    let object = d.join("foreign.o");
    compile(&foreign, &object, &["-c", "-fPIC"]);
    let options = native::build::Options {
        objects: vec![object.clone()],
        ..Default::default()
    };
    let exe = native::build::build(&m, &d.join("portable"), &options, false).unwrap();
    assert!(exe.is_file());
    if cfg!(windows) {
        assert_eq!(exe.extension().unwrap(), "exe");
    }
    assert!(Command::new(&exe).status().unwrap().success());
    let archive = d.join("portable.a");
    let options = native::build::Options {
        artifact: native::build::Artifact::Staticlib,
        objects: vec![object.clone()],
        ..Default::default()
    };
    native::build::build(&m, &archive, &options, false).unwrap();
    let source = d.join("static_host.c");
    std::fs::write(&source,"#include <assert.h>\n#include <stdint.h>\n#include <stddef.h>\ntypedef struct {uint32_t tag;double value;} Packet;_Static_assert(sizeof(Packet)==16,\"size\");_Static_assert(offsetof(Packet,value)==8,\"offset\");uint32_t ping(uint32_t);Packet reflect(Packet);Packet roundtrip(Packet,uint32_t*,size_t);int main(void){assert(ping(41)==42);Packet p={3,2.5};Packet q=reflect(p);assert(q.tag==3&&q.value==2.5);uint32_t data[2]={7,8};q=roundtrip(p,data,2);assert(q.tag==5&&q.value==9.5);q=roundtrip(p,NULL,0);assert(q.tag==3&&q.value==2.5);return 0;}\n").unwrap();
    let host = d.join(if cfg!(windows) {
        "static_host.exe"
    } else {
        "static_host"
    });
    let args = if cfg!(windows) {
        vec![archive.to_str().unwrap()]
    } else {
        vec![archive.to_str().unwrap(), "-lm"]
    };
    compile(&source, &host, &args);
    assert!(Command::new(&host).status().unwrap().success());
    let plugin = d.join("plugin");
    let options = native::build::Options {
        artifact: native::build::Artifact::Plugin,
        plugin_profile: Some("c-abi-v1".into()),
        objects: vec![object],
        ..Default::default()
    };
    let module = native::build::build(&m, &plugin, &options, false).unwrap();
    assert_eq!(
        module.file_name().unwrap(),
        if cfg!(windows) {
            "module.dll"
        } else if cfg!(target_os = "macos") {
            "module.dylib"
        } else {
            "module.so"
        }
    );
    let manifest = std::fs::read_to_string(plugin.join("manifest.json")).unwrap();
    assert!(manifest.contains(module.file_name().unwrap().to_str().unwrap()));
    let cpp = d.join("header.cpp");
    std::fs::write(&cpp,format!("#include \"{}\"\nstatic_assert(sizeof(dn_r_Packet)==16);static_assert(alignof(dn_r_Packet)==8);int main(){{return 0;}}\n",Path::new(&format!("{}.h",module.display())).file_name().unwrap().to_str().unwrap())).unwrap();
    let result = Command::new(std::env::var("CXX").unwrap_or_else(|_| {
        if cfg!(windows) {
            "g++".into()
        } else {
            "c++".into()
        }
    }))
    .arg("-std=c++17")
    .arg("-I")
    .arg(&plugin)
    .arg("-c")
    .arg(&cpp)
    .arg("-o")
    .arg(d.join("header.o"))
    .output()
    .unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    std::fs::remove_dir_all(d).unwrap();
}

#[test]
fn independently_loaded_modules_bind_their_own_runtime_and_function_tables() {
    let m = mir("component.dlm");
    let d = directory();
    let first = d.join(if cfg!(windows) {
        "first.dll"
    } else {
        "first.so"
    });
    let second = d.join(if cfg!(windows) {
        "second.dll"
    } else {
        "second.so"
    });
    let options = native::build::Options {
        artifact: native::build::Artifact::Sharedlib,
        ..Default::default()
    };
    native::build::build(&m, &first, &options, false).unwrap();
    native::build::build(&m, &second, &options, false).unwrap();
    let host = d.join(if cfg!(windows) {
        "two_modules.exe"
    } else {
        "two_modules"
    });
    compile(
        &root().join("crates/dlm_core/tests/native_hosts/two_modules_host.c"),
        &host,
        if cfg!(windows) { &[] } else { &["-ldl"] },
    );
    let result = Command::new(&host).args([first, second]).output().unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    println!("{}", String::from_utf8_lossy(&result.stdout));
    std::fs::remove_dir_all(d).unwrap();
}
