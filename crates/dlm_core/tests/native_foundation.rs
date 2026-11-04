use dlm_core::native::{self, Mir};
use dlm_core::{lower_program, parse_module, CheckPolicy, Program};
use std::path::PathBuf;
use std::process::Command;
use std::sync::atomic::{AtomicUsize, Ordering};
static SERIAL: AtomicUsize = AtomicUsize::new(0);
fn dir() -> PathBuf {
    let d = std::env::temp_dir().join(format!(
        "dlm-native-{}-{}",
        std::process::id(),
        SERIAL.fetch_add(1, Ordering::Relaxed)
    ));
    std::fs::create_dir_all(&d).unwrap();
    d
}
fn mir(body: &str) -> Mir {
    let m = parse_module(&format!("module test.native\nnative {{\n{body}\n}}\n")).unwrap();
    lower_program(&Program::single(m), CheckPolicy::allow_unsafe())
        .unwrap()
        .native
}
fn reject(body: &str, why: &str) {
    let module = parse_module(&format!("module test.native\nnative {{\n{body}\n}}\n")).unwrap();
    let errors = native::check_program(&Program::single(module)).unwrap_err();
    assert!(
        errors.iter().any(|e| e.message.contains(why)),
        "expected {why}: {errors:?}"
    );
}
fn run_c(m: &Mir) -> String {
    let d = dir();
    let exe = d.join("demo");
    native::build::build(m, &exe, &Default::default(), false).unwrap();
    let r = Command::new(&exe).output().unwrap();
    assert!(r.status.success(), "{}", String::from_utf8_lossy(&r.stderr));
    let s = String::from_utf8(r.stdout).unwrap().replace("\r\n", "\n");
    std::fs::remove_dir_all(d).unwrap();
    s
}
#[test]
fn full_application_matches_reference() {
    let p = Program::single(
        parse_module(include_str!("../../../examples/native/foundation.dlm")).unwrap(),
    );
    let m = lower_program(&p, CheckPolicy::research()).unwrap().native;
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(run_c(&m), format!("{}\n", vm.output.join("\n")));
}
#[test]
fn machine_integer_boundaries_and_ieee_values() {
    let m = mir(r#"
fn main() -> Unit {
 let a: U8 = 255; let b: I8 = -128;
 assert(wrapping_add(a, U8(1)) == U8(0));
 assert(saturating_add(a, U8(1)) == U8(255));
 assert(saturating_sub(b, I8(1)) == I8(-128));
 assert(wrapping_sub(b, I8(1)) == I8(127));
 let c: U16 = 65535; let d: I16 = -32768;
 assert(wrapping_mul(c, U16(2)) == U16(65534));
 assert((d >> I16(3)) == I16(-4096));
 let e: U32 = 4294967295; let f: I32 = -2147483648;
 assert(wrapping_add(e, U32(1)) == U32(0));
 assert(saturating_sub(f, I32(1)) == f);
 let g: U64 = 18446744073709551615; let h: I64 = -9223372036854775808;
 assert(wrapping_mul(g,g) == U64(1));
 assert(saturating_mul(g,g) == g);
 assert((h >> I64(63)) == I64(-1));
 let us: USize = 18446744073709551615; let si: ISize = -9223372036854775808;
 assert(wrapping_add(us,USize(1)) == USize(0));
 assert(wrapping_sub(si,ISize(1)) == ISize(9223372036854775807));
 let x: F32 = 16777216.0;
 assert(x + F32(1) == x);
 assert(sign_bit(F32(-0.0)));
 assert(is_nan(nan<F64>()));
 assert(!is_finite(infinity<F32>()));
 assert(bits(from_bits<F64>(U64(9221120237041090561))) == U64(9221120237041090561));
 print("machine OK");
}
"#);
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(run_c(&m), "machine OK\n");
}

#[test]
fn binary32_rounding_is_direct_and_hex_digits_are_not_exponents() {
    let m = mir(r#"
fn main()->Unit{
 let a:U64=9223372586610589697;
 assert(bits(cast<F32>(a))==U32(0x5f000001));
 let b:I64=4611686293305294849;
 assert(bits(cast<F32>(b))==U32(0x5e800001));
 let c:F32=1.00000005960464477539062500000000000000000000000000000000000000001;
 assert(bits(c)==U32(0x3f800001));
 let d:F32=0x8000008000000001;
 assert(bits(d)==U32(0x5f000001));
 let h:USize=0xDEADBEEF;assert(h==USize(3735928559));
 assert(0xFE+1==USize(255));
 let min:I64=-9_223_372_036_854_775_808;
 let hexmin:I64=-0x8000000000000000;assert(min==hexmin);
 assert(is_nan(nan<F32>()));assert(sign_bit(F64(-0.0)));
 print("rounding OK");
}
"#);
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    let d = dir();
    let exe = d.join("rounding");
    let options = native::build::Options {
        flags: vec!["-ffast-math".into()],
        ..Default::default()
    };
    native::build::build(&m, &exe, &options, false).unwrap();
    let r = Command::new(&exe).output().unwrap();
    assert!(r.status.success(), "{}", String::from_utf8_lossy(&r.stderr));
    assert_eq!(
        String::from_utf8_lossy(&r.stdout).replace("\r\n", "\n"),
        "rounding OK\n"
    );
    std::fs::remove_dir_all(d).unwrap();
}
#[test]
fn scalar_functions_have_no_dynamic_values_or_heap_calls() {
    let m=mir("export fn affine(x:F64,y:F64,z:F64)->F64 guarantees(NoAlloc,NoBlocking,NoIO,NoUnwind,BoundedWork,ThreadSafe){return x*y+z;}");
    let c = native::emit::emit(&m, false);
    let start = c
        .find("static double dn_f_affine(double v_x,double v_y,double v_z) {")
        .unwrap();
    let end = c[start..].find("\n}\n").unwrap() + start;
    assert!(!c[start..end].contains("allocate"));
    assert!(!c.contains("DV *"));
    let d = dir();
    let source = d.join("a.c");
    std::fs::write(&source, c).unwrap();
    let assembly = d.join("a.s");
    let r = Command::new(dlm_core::default_c_compiler())
        .args(["-std=gnu11", "-O2", "-S", "-ffp-contract=off"])
        .arg(&source)
        .arg("-o")
        .arg(&assembly)
        .output()
        .unwrap();
    assert!(r.status.success(), "{}", String::from_utf8_lossy(&r.stderr));
    let s = std::fs::read_to_string(assembly).unwrap();
    assert!(s.contains("mulsd") && s.contains("addsd"));
    std::fs::remove_dir_all(d).unwrap();
}
#[test]
fn ownership_aliases_and_effect_lies_are_rejected() {
    reject("fn main()->Unit{let a:Buffer<F64>=buffer<F64>(2);let b:Buffer<F64>=move a;print(len(&a));}","use after move");
    reject(
        "fn main()->Unit{let a:Buffer<F64>=buffer<F64>(2);let b:Buffer<F64>=a;}",
        "explicit move",
    );
    reject("fn main()->Unit{let mut a:Buffer<F64>=buffer<F64>(2);let s:MutSlice<F64>=mut_slice(&mut a,0,2);a[0]=1.0;print(len(&s));}","borrow conflict");
    reject("fn g()->F64{let b:Buffer<F64>=buffer<F64>(4);return 1.0;} fn f()->F64 guarantees(NoAlloc){return g();}","violates NoAlloc");
    reject(
        "fn f()->Unit guarantees(BoundedWork){while true{break;}}",
        "violates BoundedWork",
    );
    reject(
        "fn main()->Unit{let a:AtomicU32=atomic<U32>(0);print(atomic_load(&a,Release));}",
        "load cannot",
    );
    reject("fn main()->Unit{let a:AtomicU32=atomic<U32>(0);atomic_compare_exchange(&a,0,1,Release,Acquire);}","failure ordering");
    reject(
        "fn main()->Unit{let mut a:Buffer<U8>=buffer<U8>(2);let s:Slice<U8>=slice(&a,0,2);s[0]=2;}",
        "immutable",
    );
    reject(
        "unsafe fn bad()->Slice<U8>{let mut a:Buffer<U8>=buffer<U8>(2);return slice(&a,0,2);}",
        "returning a borrow",
    );
    reject(
        "extern \"C\" fn f(x:U32)->U32 from \"untrusted\";",
        "trust list",
    );
}
#[test]
fn split_slices_and_partial_record_moves() {
    let m = mir(r#"
record State repr(C){a:Buffer<U32>;b:Buffer<U32>;}
fn main()->Unit{
 let mut a:Buffer<U32>=buffer<U32>(8);
 {let mut parts:Split<U32>=split_at_mut(&mut a,3);parts.left[0]=U32(9);parts.right[0]=U32(7);}
 assert(a[0]==U32(9));assert(a[3]==U32(7));
 let mut state:State=State(move a,buffer<U32>(1));
 let b:Buffer<U32>=move state.a;
 state.b[0]=U32(5);
 assert(b[3]==U32(7));assert(state.b[0]==U32(5));
 print("ownership OK");
}
"#);
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(run_c(&m), "ownership OK\n");
}
#[test]
fn forged_native_ir_is_rejected() {
    let mut m = mir("export fn f(x:F64)->F64{return x+1.0;}");
    m.layouts.insert(
        "forged".into(),
        native::Layout {
            size: 1,
            align: 1,
            offsets: vec![],
            version: 1,
        },
    );
    assert!(native::build::build(&m, &dir().join("bad"), &Default::default(), true).is_err());
}
#[cfg(target_os = "linux")]
#[test]
fn shared_library_abi_instances_callbacks_and_threads() {
    let m = mir(r#"
 trust "fixture";
 record Packet repr(C,version=3){tag:U32;value:F64;}
 record State repr(C){counter:AtomicU64;data:Buffer<F64>;}
 record Registration repr(C){state:Box<State>;token:U64;}
 extern "C" fn fixture_sum(p:Packet,data:Ptr<F64>,n:USize)->F64 from "fixture" guarantees(NoAlloc,NoBlocking,NoIO,NoUnwind,BoundedWork,ThreadSafe);
 extern "C" fn fixture_parallel(cb:FnPtr<OpaqueHandle,U64,U64>,ctx:OpaqueHandle)->U64 from "fixture" guarantees(NoAlloc,NoIO,NoUnwind,ThreadSafe);
 extern "C" fn fixture_register(cb:FnPtr<OpaqueHandle,U64,U64>,ctx:OpaqueHandle)->U64 from "fixture" guarantees(NoAlloc,NoBlocking,NoIO,NoUnwind,BoundedWork);
 extern "C" fn fixture_cancel(token:U64)->Unit from "fixture" guarantees(NoAlloc,NoBlocking,NoIO,NoUnwind,BoundedWork);
 extern "C" fn fixture_fire(token:U64,count:U64)->U64 from "fixture" guarantees(NoAlloc,NoIO,NoUnwind);
 extern "C" fn fixture_nested(cb:FnPtr<U64,U64>,depth:U64)->U64 from "fixture" guarantees(NoAlloc,NoBlocking,NoIO,NoUnwind);
 export fn create(seed:U64)->OpaqueHandle{let state:Box<State>=box<State>(State(atomic<U64>(seed),buffer<F64>(8)));return handle(into_raw(move state));}
 export unsafe fn destroy(h:OpaqueHandle)->Unit{let owner:Box<State>=from_raw(handle_ptr<State>(h));}
 unsafe fn callback(userdata:OpaqueHandle,amount:U64)->U64{
   let ctx:Ptr<State>=handle_ptr<State>(userdata);
   let mut old:U64=atomic_load(&(*ctx).counter,Relaxed);
   while true{if atomic_compare_exchange(&(*ctx).counter,old,old+amount,AcqRel,Acquire){return old+amount;}old=atomic_load(&(*ctx).counter,Relaxed);}
 }
 export unsafe fn parallel(h:OpaqueHandle)->U64{return fixture_parallel(fn_ptr(callback),h);}
 unsafe fn nested(depth:U64)->U64{if depth==U64(0){return U64(7);}return fixture_nested(fn_ptr(nested),depth-U64(1))+U64(1);}
 export unsafe fn reentrant(depth:U64)->U64{return nested(depth);}
 export unsafe fn subscribe(seed:U64)->OpaqueHandle{
   let state:Box<State>=box<State>(State(atomic<U64>(seed),buffer<F64>(8)));
   let mut registration:Box<Registration>=box<Registration>(Registration(move state,U64(0)));
   let token:U64=fixture_register(fn_ptr(callback),handle(as_raw(borrowed_ptr(&mut *registration.state))));
   assert(token!=U64(0));
   registration.token=token;
   return handle(into_raw(move registration));
 }
 export unsafe fn fire(h:OpaqueHandle,count:U64)->U64{return fixture_fire((*handle_ptr<Registration>(h)).token,count);}
 export unsafe fn unsubscribe(h:OpaqueHandle)->U64{
   let registration:Box<Registration>=from_raw(handle_ptr<Registration>(h));
   let token:U64=registration.token;
   fixture_cancel(token);
   return fixture_fire(token,U64(1));
 }
 export unsafe fn value(h:OpaqueHandle)->U64{return atomic_load(&(*handle_ptr<State>(h)).counter,Acquire);}
 export unsafe fn external(p:Packet,data:Ptr<F64>,n:USize)->F64{return fixture_sum(p,data,n);}
 export fn fail_after_alloc()->U64{let b:Buffer<F64>=buffer<F64>(32);assert(false);return U64(7);}
 "#);
    let d = dir();
    let fixture = d.join("fixture.c");
    std::fs::write(&fixture,r#"
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
typedef struct{uint32_t tag;double value;} Packet;
double fixture_sum(Packet p,double *x,size_t n){double s=p.value;for(size_t i=0;i<n;i++)s+=x[i];return s+p.tag;}
typedef uint64_t(*Callback)(void*,uint64_t);
typedef struct{Callback cb;void *ctx;} Job;
static Job registrations[8];
uint64_t fixture_register(Callback cb,void *ctx){for(size_t i=0;i<8;i++)if(!registrations[i].cb){registrations[i]=(Job){cb,ctx};return i+1;}return 0;}
void fixture_cancel(uint64_t token){if(token&&token<=8)registrations[token-1]=(Job){0};}
uint64_t fixture_fire(uint64_t token,uint64_t count){if(!token||token>8||!registrations[token-1].cb)return 0;Job j=registrations[token-1];uint64_t value=0;for(uint64_t i=0;i<count;i++)value=j.cb(j.ctx,1);return value;}
uint64_t fixture_nested(uint64_t(*cb)(uint64_t),uint64_t depth){return cb(depth);}
static void *worker(void *p){Job *j=p;for(int i=0;i<1000;i++)j->cb(j->ctx,1);return 0;}
uint64_t fixture_parallel(Callback cb,void *ctx){pthread_t t[4];Job j={cb,ctx};for(int i=0;i<4;i++)pthread_create(t+i,0,worker,&j);for(int i=0;i<4;i++)pthread_join(t[i],0);return 4000;}
"#).unwrap();
    let obj = d.join("fixture.o");
    assert!(Command::new(dlm_core::default_c_compiler())
        .args(["-c", "-fPIC"])
        .arg(&fixture)
        .arg("-o")
        .arg(&obj)
        .status()
        .unwrap()
        .success());
    let so = d.join("app.so");
    let opts = native::build::Options {
        artifact: native::build::Artifact::Sharedlib,
        objects: vec![obj],
        flags: vec!["-pthread".into()],
        ..Default::default()
    };
    native::build::build(&m, &so, &opts, false).unwrap();
    let host = d.join("host.c");
    std::fs::write(&host,r#"
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef struct{uint32_t tag;double value;} Packet;
#define SYM(T,n) T n=(T)dlsym(lib,#n);assert(n)
int main(int argc,char **argv){assert(argc==2);void *lib=dlopen(argv[1],RTLD_NOW);if(!lib){puts(dlerror());return 1;}
 typedef void *(*Create)(uint64_t);typedef void(*Destroy)(void*);typedef uint64_t(*Value)(void*);typedef uint64_t(*Parallel)(void*);typedef uint64_t(*Fire)(void*,uint64_t);typedef uint64_t(*Reentrant)(uint64_t);typedef double(*External)(Packet,double*,size_t);typedef size_t(*Size)(void);typedef const char*(*Error)(void);typedef uint64_t(*Count)(void);
 SYM(Create,create);SYM(Destroy,destroy);SYM(Value,value);SYM(Parallel,parallel);SYM(External,external);
 SYM(Create,subscribe);SYM(Fire,fire);SYM(Value,unsubscribe);
 SYM(Reentrant,reentrant);assert(reentrant(32)==39);
 SYM(Size,dlm_native_sizeof_Packet);SYM(Size,dlm_native_alignof_Packet);SYM(Size,dlm_native_offset_Packet_value);SYM(Error,dlm_native_last_error);SYM(Count,dlm_native_allocations);SYM(Count,dlm_native_frees);SYM(Count,fail_after_alloc);
 assert(dlm_native_sizeof_Packet()==sizeof(Packet));assert(dlm_native_alignof_Packet()==_Alignof(Packet));assert(dlm_native_offset_Packet_value()==offsetof(Packet,value));
 void *a=create(10),*b=create(90);assert(a&&b&&a!=b);assert(value(a)==10&&value(b)==90);assert(parallel(a)==4000);assert(value(a)==4010&&value(b)==90);
 double x[]={1.0,2.0,3.0};Packet p={4,2.5};assert(external(p,x,3)==12.5);
 destroy(a);assert(value(b)==90);destroy(b);
 a=subscribe(7);b=subscribe(31);assert(a&&b&&a!=b);assert(fire(a,100)==107);assert(fire(b,3)==34);assert(unsubscribe(a)==0);assert(fire(b,2)==36);assert(unsubscribe(b)==0);
 uint64_t made=dlm_native_allocations(),freed=dlm_native_frees();assert(made==freed);
 assert(fail_after_alloc()==0);assert(strstr(dlm_native_last_error(),"assertion"));assert(dlm_native_allocations()-made==dlm_native_frees()-freed);
 assert(dlclose(lib)==0);puts("host OK");return 0;}
"#).unwrap();
    let exe = d.join("host");
    assert!(Command::new(dlm_core::default_c_compiler())
        .arg(&host)
        .args(["-ldl", "-o"])
        .arg(&exe)
        .status()
        .unwrap()
        .success());
    let result = Command::new(exe).arg(so).output().unwrap();
    assert!(
        result.status.success(),
        "{} {}",
        String::from_utf8_lossy(&result.stdout),
        String::from_utf8_lossy(&result.stderr)
    );
    assert_eq!(result.stdout, b"host OK\n");
    std::fs::remove_dir_all(d).unwrap();
}

#[test]
fn aliasing_existing_mutable_references_is_rejected() {
    reject("fn f(a:&mut F64,b:&mut F64)->Unit{*a=1.0;*b=2.0;}fn main()->Unit{let mut x:F64=0.0;let r:&mut F64=&mut x;f(r,r);}","borrow");
    reject("fn main()->Unit{let mut b:Array<F64,16>=zero<Array<F64,16>>();let p:&mut F64=&mut b[10];let q:&mut F64=&mut b[0xA];}","borrow");
    reject("fn main()->Unit{let mut b:Array<F64,16>=zero<Array<F64,16>>();let p:&mut F64=&mut b[10];let q:&mut F64=&mut b[1_0];}","borrow");
}

#[test]
fn values_copy_and_mutable_slices_remain_disjoint() {
    let m = mir(r#"
record P repr(C){x:F64;}
fn main()->Unit{
 let a:P=P(1.0);let mut b:P=a;b.x=2.0;assert(a.x==1.0);
 let mut x:Array<U32,4>=zero<Array<U32,4>>();x[0]=U32(3);
 let mut y:Array<U32,4>=x;y[0]=U32(9);assert(x[0]==U32(3));
 let mut z:Buffer<U32>=buffer<U32>(8);
 {let mut left:MutSlice<U32>=mut_slice(&mut z,0,4);let mut right:MutSlice<U32>=mut_slice(&mut z,4,4);left[0]=U32(5);right[0]=U32(7);}
 assert(z[0]==U32(5));assert(z[4]==U32(7));
 let b32:U32=2143289345;assert(bits(from_bits<F32>(b32))==b32);
 print("copies OK");
}
"#);
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(run_c(&m), "copies OK\n");
}

#[test]
fn borrowed_results_are_tied_to_the_declared_source() {
    let m = mir(r#"
fn prefix(bytes:&Buffer<U8>)->Slice<U8> borrows(bytes){return slice(bytes,0,1);}
fn main()->Unit{let mut bytes:Buffer<U8>=buffer<U8>(2);bytes[0]=U8(7);{let view:Slice<U8>=prefix(&bytes);assert(view[0]==U8(7));}bytes[0]=U8(9);print("lifetimes OK");}
"#);
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(run_c(&m), "lifetimes OK\n");
    reject("fn bad(a:&Buffer<U8>)->Slice<U8> borrows(a){let b:Buffer<U8>=buffer<U8>(1);return slice(&b,0,1);}","escapes");
}

#[test]
fn serialization_library_matches_reference() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../");
    let p = dlm_core::program::load_program_with_paths(
        &root.join("examples/native/serialization.dlm"),
        &[root.join("lib")],
    )
    .unwrap();
    let m = lower_program(&p, CheckPolicy::research()).unwrap().native;
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(run_c(&m), "serialization OK\n");
}

#[test]
fn native_window_bridge_obeys_the_platform_display_contract() {
    let m = mir(
        "export fn open_editor()->Unit{let w:Window=window_open(\"test\",64,64);window_focus(&w);}",
    );
    let d = dir();
    let file = d.join("gui.c");
    std::fs::write(&file,format!("{}\nint main(void){{open_editor();\n#ifdef _WIN32\nif(dlm_native_last_error())return 1;\n#else\nif(!dlm_native_last_error())return 1;\n#endif\nif(dlm_native_allocations()!=dlm_native_frees())return 2;return !dlm_native_runtime_shutdown();}}\n",native::emit::emit(&m,false))).unwrap();
    let executable = d.join(if cfg!(windows) {
        "gui-test.exe"
    } else {
        "gui-test"
    });
    let result = Command::new(dlm_core::default_c_compiler())
        .args(["-std=gnu11"])
        .arg(&file)
        .args(if cfg!(windows) {
            vec!["-lm", "-lshell32", "-lws2_32", "-lgdi32", "-luser32"]
        } else {
            vec!["-lm", "-ldl", "-pthread"]
        })
        .arg("-o")
        .arg(&executable)
        .output()
        .unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    let result = Command::new(&executable)
        .env_remove("DISPLAY")
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
fn traversals_can_prove_bounds_without_trusting_annotations() {
    let m = mir(r#"
fn sum(values:&Buffer<F64>)->F64 guarantees(NoAlloc,NoBlocking,NoIO,NoUnwind,BoundedWork,ThreadSafe){let mut total:F64=0.0;for i in 0..len(values){total=total+values[i];}return total;}
fn main()->Unit{let mut b:Buffer<F64>=buffer<F64>(2);b[0]=3.0;b[1]=4.0;assert(sum(&b)==7.0);print("bounds OK");}
"#);
    let c = native::emit::emit(&m, true);
    let start = c.find("static double dn_f_sum(").unwrap();
    let start = c[start + 1..].find("static double dn_f_sum(").unwrap() + start + 1;
    let end = c[start..]
        .find("static uint8_t dn_f_main")
        .unwrap_or(c.len() - start)
        + start;
    assert!(!c[start..end].contains("dn_index("));
    assert_eq!(run_c(&m), "bounds OK\n");
    reject(
        "fn f(b:&mut Buffer<F64>)->Unit{for i in 0..len(b){resize(b,0);print(b[i]);}}",
        "invalidate",
    );
}

#[cfg(target_os = "linux")]
#[test]
fn errors_preserve_retained_objects_and_release_locals_under_sanitizers() {
    let m = mir(r#"
record State repr(C){data:Buffer<F64>;}
export fn create()->OpaqueHandle{let b:Box<State>=box<State>(State(buffer<F64>(2)));return handle(into_raw(move b));}
export unsafe fn destroy(h:OpaqueHandle)->Unit{let b:Box<State>=from_raw(handle_ptr<State>(h));}
export unsafe fn resize_fail(h:OpaqueHandle)->Unit{let p:Ptr<State>=handle_ptr<State>(h);resize(&mut (*p).data,128);(*p).data[0]=7.0;assert(false);}
export unsafe fn assign_fail(h:OpaqueHandle)->Unit{let p:Ptr<State>=handle_ptr<State>(h);let b:Buffer<F64>=buffer<F64>(64);(*p).data=move b;(*p).data[0]=9.0;assert(false);}
export unsafe fn state_read(h:OpaqueHandle)->F64{let p:Ptr<State>=handle_ptr<State>(h);return (*p).data[0];}
fn consume(b:Box<State>,n:U64)->Unit{}
fn failure()->U64{assert(false);return U64(0);}
export unsafe fn consume_fail(h:OpaqueHandle)->Unit{let b:Box<State>=from_raw(handle_ptr<State>(h));consume(move b,failure());}
"#);
    let d = dir();
    let source = d.join("errors.c");
    let host = r#"
int main(void){void *h=create();resize_fail(h);if(!dlm_native_last_error())return 1;if(state_read(h)!=7.0)return 2;assign_fail(h);if(!dlm_native_last_error())return 3;if(state_read(h)!=9.0)return 4;destroy(h);if(dlm_native_allocations()!=dlm_native_frees())return 5;h=create();consume_fail(h);if(!dlm_native_last_error())return 6;if(dlm_native_allocations()!=dlm_native_frees())return 7;return 0;}
"#;
    std::fs::write(
        &source,
        format!("{}\n{host}", native::emit::emit(&m, false)),
    )
    .unwrap();
    let exe = d.join("errors");
    let result = Command::new(dlm_core::default_c_compiler())
        .args([
            "-std=gnu11",
            "-O2",
            "-g",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
        ])
        .arg(&source)
        .args(["-lm", "-o"])
        .arg(&exe)
        .output()
        .unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    let result = Command::new(exe)
        .env("ASAN_OPTIONS", "detect_leaks=0:halt_on_error=1")
        .output()
        .unwrap();
    assert!(
        result.status.success(),
        "status {:?}: {}",
        result.status.code(),
        String::from_utf8_lossy(&result.stderr)
    );
    std::fs::remove_dir_all(d).unwrap();
}

#[test]
fn arrays_nonzero_initialization_and_aligned_buffers() {
    let m = mir(r#"
record R repr(C){data:Buffer<U32>;}
fn main()->Unit{
 let mut a:Array<Buffer<U32>,2>=array<Array<Buffer<U32>,2>>(buffer<U32>(1),buffer<U32>(2));
 a[0][0]=U32(7);assert(a[0][0]==U32(7));
 let mut records:Buffer<R>=buffer<R>(0);push(&mut records,R(buffer<U32>(4)));records[0].data[0]=U32(9);assert(records[0].data[0]==U32(9));
 let mut aligned:Buffer<F64>=buffer_aligned<F64>(4,64);let vector:Simd<F64,4>=simd_splat<Simd<F64,4>>(2.0);simd_store_aligned(&mut aligned,0,vector);resize(&mut aligned,16);let copy:Simd<F64,4>=simd_load_aligned<Simd<F64,4>>(&aligned,0);simd_store(&mut aligned,4,copy);assert(aligned[4]==2.0);print("dense OK");
}
"#);
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(run_c(&m), "dense OK\n");
    reject(
        "record R repr(C){p:NonNull<U8>;}fn main()->Unit{let r:R=zero<R>();}",
        "no valid zero",
    );
}

#[test]
fn float_formatting_matches_reference() {
    let m=mir("fn main()->Unit{print(0.1);print(-0.0);print(F32(0.1));print(1.0e-20);print(1.0e20);print(nan<F64>());print(infinity<F32>());}");
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(run_c(&m), format!("{}\n", vm.output.join("\n")));
}

#[cfg(target_os = "linux")]
#[test]
fn abi_headers_plugin_staticlib_and_host_fp_state() {
    let m = mir(r#"
record Packet repr(C,version=2){tag:U32;value:F64;}
export fn multiply(x:F32,y:F32)->F32 guarantees(NoAlloc,NoBlocking,NoIO,NoUnwind,BoundedWork,ThreadSafe){return x*y;}
export fn packet(x:Packet)->F64{return x.value;}
export fn nothing()->Unit{}
"#);
    let d = dir();
    let plugin = d.join("sample.plugin");
    let options = native::build::Options {
        artifact: native::build::Artifact::Plugin,
        plugin_profile: Some("c-abi-v1".into()),
        ..Default::default()
    };
    native::build::build(&m, &plugin, &options, false).unwrap();
    assert!(plugin.join("module.so").is_file());
    assert!(plugin.join("manifest.json").is_file());
    let header = plugin.join("module.so.h");
    let cpp = d.join("header.cpp");
    std::fs::write(
        &cpp,
        format!(
            "#include \"{}\"\nstatic_assert(sizeof(dn_r_Packet)==16);\nint main(){{return 0;}}\n",
            header.display()
        ),
    )
    .unwrap();
    let r = Command::new("g++")
        .args(["-std=c++17", "-c"])
        .arg(cpp)
        .arg("-o")
        .arg(d.join("header.o"))
        .output()
        .unwrap();
    assert!(r.status.success(), "{}", String::from_utf8_lossy(&r.stderr));
    let archive = d.join("sample.a");
    native::build::build(
        &m,
        &archive,
        &native::build::Options {
            artifact: native::build::Artifact::Staticlib,
            ..Default::default()
        },
        false,
    )
    .unwrap();
    let host = d.join("fp.c");
    std::fs::write(&host,format!(r#"
#include "{}"
#include <immintrin.h>
#include <string.h>
#include <assert.h>
int main(void){{dlm_native_thread_init();uint32_t b=1;float x;memcpy(&x,&b,4);unsigned saved=_mm_getcsr();unsigned host=saved|0xc040;_mm_setcsr(host);float y=multiply(x,1.0f);uint32_t result;memcpy(&result,&y,4);assert(result==1);assert(_mm_getcsr()==host);_mm_setcsr(saved);dn_r_Packet p={{3,2.5}};assert(packet(p)==2.5);nothing();return 0;}}
"#,header.display())).unwrap();
    let exe = d.join("host");
    let r = Command::new(dlm_core::default_c_compiler())
        .arg(host)
        .arg(&archive)
        .args(["-lm", "-o"])
        .arg(&exe)
        .output()
        .unwrap();
    assert!(r.status.success(), "{}", String::from_utf8_lossy(&r.stderr));
    let r = Command::new(exe).output().unwrap();
    assert!(r.status.success(), "{}", String::from_utf8_lossy(&r.stderr));
    std::fs::remove_dir_all(d).unwrap();
}

#[test]
fn nested_dense_index_expressions_are_evaluated_once() {
    let m = mir(r#"
fn next(index:&mut USize)->USize{let old:USize=*index;*index=old+1;return old;}
fn main()->Unit{let mut values:Buffer<Buffer<F64>>=buffer<Buffer<F64>>(2);values[0]=buffer<F64>(1);values[1]=buffer<F64>(1);values[0][0]=7.0;values[1][0]=9.0;let mut index:USize=0;let value:F64=values[next(&mut index)][0];assert(value==7.0);assert(index==1);print("order OK");}
"#);
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(run_c(&m), "order OK\n");
}

#[test]
fn ffi_can_import_symbols_already_declared_by_c_headers() {
    let m = mir(r#"
trust "c";
extern "C" fn strlen(text:Ptr<U8>)->USize from "c" guarantees(NoAlloc,NoBlocking,NoIO,NoUnwind,BoundedWork,ThreadSafe);
unsafe fn main()->Unit{let text:Slice<U8>=utf8_bytes("hello");assert(strlen(data(&text))==5);print("stdlib FFI OK");}
"#);
    assert_eq!(run_c(&m), "stdlib FFI OK\n");
}

#[cfg(target_os = "linux")]
#[test]
fn thread_bound_state_rejects_a_foreign_thread() {
    let m = mir(r#"
export fn bound_create()->OpaqueHandle{let b:Box<ThreadBound<F64>>=box<ThreadBound<F64>>(thread_bind<F64>(5.0));return handle(into_raw(move b));}
export unsafe fn bound_get(h:OpaqueHandle)->F64 guarantees(ThreadAffine){let p:Ptr<ThreadBound<F64>>=handle_ptr<ThreadBound<F64>>(h);return *thread_get(&*p);}
export unsafe fn bound_destroy(h:OpaqueHandle)->Unit{let b:Box<ThreadBound<F64>>=from_raw(handle_ptr<ThreadBound<F64>>(h));}
"#);
    let d = dir();
    let source = d.join("affinity.c");
    std::fs::write(&source,format!(r#"{}
#include <pthread.h>
#include <assert.h>
static void *worker(void *h){{assert(bound_get(h)==0.0);assert(dlm_native_last_error());return 0;}}
int main(void){{void *h=bound_create();pthread_t t;assert(pthread_create(&t,0,worker,h)==0);assert(pthread_join(t,0)==0);assert(bound_get(h)==5.0);assert(!dlm_native_last_error());bound_destroy(h);assert(dlm_native_allocations()==dlm_native_frees());return 0;}}
"#,native::emit::emit(&m,false))).unwrap();
    let exe = d.join("affinity");
    let r = Command::new(dlm_core::default_c_compiler())
        .args(["-std=gnu11", "-O2"])
        .arg(source)
        .args(["-pthread", "-lm", "-o"])
        .arg(&exe)
        .output()
        .unwrap();
    assert!(r.status.success(), "{}", String::from_utf8_lossy(&r.stderr));
    let r = Command::new(exe).output().unwrap();
    assert!(r.status.success(), "{}", String::from_utf8_lossy(&r.stderr));
    std::fs::remove_dir_all(d).unwrap();
}

#[test]
fn checked_integer_errors_match_the_reference() {
    let m = mir("fn main()->Unit{let x:U8=255;print(x+U8(1));}");
    let mut vm = native::vm::Vm::new(&m);
    assert!(vm.run().unwrap_err().contains("integer overflow"));
    let d = dir();
    let exe = d.join("error");
    native::build::build(&m, &exe, &Default::default(), false).unwrap();
    let r = Command::new(exe).output().unwrap();
    assert_eq!(r.status.code(), Some(1));
    assert!(String::from_utf8_lossy(&r.stderr).contains("integer overflow"));
    std::fs::remove_dir_all(d).unwrap();
}

#[test]
fn assignment_rhs_runs_before_resolving_its_destination() {
    let m = mir(r#"
fn change(b:&mut Buffer<F64>)->F64{resize(b,8);return 4.0;}
fn main()->Unit{let mut b:Buffer<F64>=buffer<F64>(1);b[0]=change(&mut b);assert(b[0]==4.0);assert(len(&b)==8);let x:U64=7;let y:U64=move x;assert(x==y);print("assignment OK");}
"#);
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(run_c(&m), "assignment OK\n");
    reject(
        "record R repr(C){a:Buffer<U8>;p:NonNull<U8>;}fn bad(r:&mut R)->R{return move *r;}",
        "valid replacement",
    );
}

#[test]
fn derived_exclusive_borrows_keep_their_parent_lifetime() {
    let m = mir(r#"
fn same(p:&mut F64)->&mut F64 borrows(p){return move p;}
fn halves(p:&mut Buffer<F64>)->Unit{let mut left:MutSlice<F64>=mut_slice(p,0,2);let mut right:MutSlice<F64>=mut_slice(p,2,2);left[0]=3.0;right[0]=4.0;}
fn main()->Unit{let mut x:F64=0.0;{let r:&mut F64=same(&mut x);*r=5.0;}assert(x==5.0);let mut b:Buffer<F64>=buffer<F64>(4);halves(&mut b);assert(b[0]==3.0);assert(b[2]==4.0);{let mut parts:Split<F64>=split_at_mut(&mut b,2);let l:&mut MutSlice<F64>=&mut parts.left;let r:&mut MutSlice<F64>=&mut parts.right;l[0]=7.0;r[0]=8.0;}assert(b[0]==7.0);assert(b[2]==8.0);print("derived OK");}
"#);
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(run_c(&m), "derived OK\n");
    reject("fn main()->Unit{let mut x:F64=0.0;let r:&mut F64=&mut x;let s:&mut F64=&mut *r;*r=2.0;*s=3.0;}","borrow");
}

#[test]
fn index_evaluation_cannot_invalidate_its_base_storage() {
    reject("fn change(b:&mut Buffer<Buffer<F64>>)->USize{resize(b,100);return 0;}fn main()->Unit{let mut b:Buffer<Buffer<F64>>=buffer<Buffer<F64>>(1);b[0]=buffer<F64>(1);print(b[0][change(&mut b)]);}","borrow");
}

#[test]
fn resources_finalize_on_scope_move_and_error() {
    let m = mir(r#"
record Payload repr(C){counter:AtomicU64;bytes:Buffer<U8>;}
fn finalize(p:&mut Payload)->Unit guarantees(NoUnwind){atomic_store(&p.counter, U64(7),SeqCst);}
fn main()->Unit{
 let counter:AtomicU64=atomic<U64>(0);
 let r:Resource<Payload,finalize>=resource(Payload(move counter,buffer<U8>(3)));
 let moved:Resource<Payload,finalize>=move r;
 {let p:&Payload=resource_get(&moved);assert(len(&p.bytes)==USize(3));}
 resource_close(move moved);
 print("resource OK");
}
"#);
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(run_c(&m), "resource OK\n");
    reject("fn fail(p:&mut U64)->Unit{assert(false);} fn main()->Unit{let r:Resource<U64,fail>=resource(U64(1));}","NoUnwind");
    reject("fn finish(p:&mut U64)->Unit guarantees(NoUnwind){} fn main()->Unit{let r:Resource<U64,finish>=resource(U64(1));let s:Resource<U64,finish>=move r;resource_close(move r);}","use after move");
}

#[test]
fn ordinary_bridge_values_and_narrowing_match_reference() {
    let p = Program::single(
        parse_module(include_str!("../../../examples/native/ordinary_bridge.dlm")).unwrap(),
    );
    let m = lower_program(&p, CheckPolicy::allow_unsafe())
        .unwrap()
        .native;
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(run_c(&m), "ordinary bridge OK\n");
    assert!(vm
        .call("narrowing_error", vec![])
        .unwrap_err()
        .contains("narrowing"));
    assert!(vm
        .call("invalid_utf8", vec![])
        .unwrap_err()
        .contains("UTF-8"));
    let c = native::emit::emit(&m, false);
    assert!(c.contains("dn_f_affine") && c.contains("dn_bridge_text"));
}

#[test]
fn checked_elementwise_traversal_can_vectorize_without_fast_math() {
    let m = mir(r#"
export unsafe fn kernel(data_:Ptr<F64>,count:USize,gain:F64)->Unit guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork,ThreadSafe){
 let mut values:ForeignBuffer<F64>=foreign(data_,count);
 for i in 0..len(&values){values[i]=values[i]*gain;}
}
"#);
    let d = dir();
    let source = d.join("kernel.c");
    let assembly = d.join("kernel.s");
    std::fs::write(&source, native::emit::emit(&m, false)).unwrap();
    let r = Command::new(dlm_core::default_c_compiler())
        .args([
            "-std=gnu11",
            "-O3",
            "-ftree-vectorize",
            "-fno-fast-math",
            "-ffp-contract=off",
            "-S",
        ])
        .arg(&source)
        .arg("-o")
        .arg(&assembly)
        .output()
        .unwrap();
    assert!(r.status.success(), "{}", String::from_utf8_lossy(&r.stderr));
    let asm = std::fs::read_to_string(assembly).unwrap();
    assert!(
        asm.contains("mulpd"),
        "expected packed binary64 multiplication in the checked traversal"
    );
    std::fs::remove_dir_all(d).unwrap();
}

#[test]
fn owned_values_cannot_hide_nested_borrowed_lifetimes() {
    reject("record View repr(C){items:Slice<U8>;} record Nested repr(C){view:View;} fn main()->Unit{let b:Box<Nested>=box<Nested>(zero<Nested>());}","borrowed lifetime");
    let m=mir("record P repr(C,version=7){x:U32;y:F64;} fn main()->Unit{assert(size_of<P>()==USize(16));assert(align_of<P>()==USize(8));assert(abi_version<P>()==USize(7));print(\"layout OK\");}");
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(run_c(&m), "layout OK\n");
}

#[test]
fn resource_control_allocation_and_temporary_oom_do_not_leak_acquisitions() {
    let m = mir(r#"
 trust "oom.fixture";
 extern "C" fn acquire_resource()->OpaqueHandle from "oom.fixture" guarantees(NoIO,NoUnwind,ThreadSafe);
 extern "C" fn release_resource(h:OpaqueHandle)->Unit from "oom.fixture" guarantees(NoAlloc,NoBlocking,NoIO,NoUnwind,BoundedWork,ThreadSafe);
 unsafe fn finish(h:&mut OpaqueHandle)->Unit guarantees(NoUnwind){release_resource(*h);}
 export unsafe fn acquire_owned()->OpaqueHandle{let r:Resource<OpaqueHandle,finish>=resource(acquire_resource());let owner:Box<Resource<OpaqueHandle,finish>>=box<Resource<OpaqueHandle,finish>>(move r);return handle(into_raw(move owner));}
 export unsafe fn release_owned(h:OpaqueHandle)->Unit{let owner:Box<Resource<OpaqueHandle,finish>>=from_raw(handle_ptr<Resource<OpaqueHandle,finish>>(h));}
 "#);
    let d = dir();
    let source = d.join("resource_oom.c");
    let exe = d.join(if cfg!(windows) {
        "resource_oom.exe"
    } else {
        "resource_oom"
    });
    std::fs::write(&source,format!(r#"#include <stdlib.h>
void *dn_test_calloc(size_t,size_t);
#define calloc dn_test_calloc
{}
#undef calloc
#include <assert.h>
static unsigned acquire_count,release_count,fail_at;
void *dn_test_calloc(size_t n,size_t s){{if(fail_at&&!--fail_at)return NULL;return calloc(n,s);}}
void *acquire_resource(void){{void *p=malloc(1);assert(p);acquire_count++;return p;}}
void release_resource(void *p){{assert(p);release_count++;free(p);}}
int main(void){{dlm_native_thread_init();fail_at=1;assert(!acquire_owned());assert(dlm_native_last_error());assert(acquire_count==0&&release_count==0);fail_at=2;assert(!acquire_owned());assert(dlm_native_last_error());assert(acquire_count==1&&release_count==1);fail_at=0;void *p=acquire_owned();assert(p&&!dlm_native_last_error());release_owned(p);assert(acquire_count==release_count);assert(!dlm_native_live_allocations());assert(dlm_native_allocations()==dlm_native_frees());return 0;}}
"#,native::emit::emit(&m,false))).unwrap();
    let r = Command::new(dlm_core::default_c_compiler())
        .args(["-std=gnu11", "-O2"])
        .args(if cfg!(target_os = "linux") {
            vec!["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        } else {
            vec![]
        })
        .arg(&source)
        .args(["-lm", "-o"])
        .arg(&exe)
        .output()
        .unwrap();
    assert!(r.status.success(), "{}", String::from_utf8_lossy(&r.stderr));
    let r = Command::new(exe)
        .env("ASAN_OPTIONS", "detect_leaks=0:abort_on_error=1")
        .env("UBSAN_OPTIONS", "halt_on_error=1")
        .output()
        .unwrap();
    assert!(r.status.success(), "{}", String::from_utf8_lossy(&r.stderr));
    std::fs::remove_dir_all(d).unwrap();
}

#[test]
fn shared_references_and_slices_qualify_pointer_values_correctly_in_c() {
    let m = mir(r#"
 record Item repr(C){value:U32;}
 fn value(item:&Box<Item>)->U32{return (*item).value;}
 unsafe fn main()->Unit{
  let owner:Box<Item>=box<Item>(Item(U32(42)));assert(value(&owner)==U32(42));
  let data_:Buffer<Ptr<U8>>=buffer<Ptr<U8>>(1);let view:Slice<Ptr<U8>>=slice(&data_,0,1);assert(address(view[0])==USize(0));
 }
 "#);
    let d = dir();
    let exe = d.join(if cfg!(windows) {
        "qualifiers.exe"
    } else {
        "qualifiers"
    });
    let options = native::build::Options {
        flags: vec!["-Werror=incompatible-pointer-types".into()],
        ..Default::default()
    };
    native::build::build(&m, &exe, &options, false).unwrap();
    assert!(Command::new(exe).status().unwrap().success());
    std::fs::remove_dir_all(d).unwrap();
}

#[test]
fn trusted_dynamic_callbacks_carry_checked_execution_contracts() {
    let m = mir(r#"
 trust "host.contracts";
 extern "C" fn numeric_hook(x:F64)->F64 from "host.contracts" guarantees(NoAlloc,NoBlocking,NoIO,NoUnwind,BoundedWork,ThreadSafe);
 fn twice(x:F64)->F64 guarantees(NoAlloc,NoBlocking,NoIO,NoUnwind,BoundedWork,ThreadSafe){return x*2.0;}
 unsafe fn apply(hook:FnPtr<F64,F64>,x:F64)->F64 guarantees(NoAlloc,NoBlocking,NoIO,NoUnwind,BoundedWork,ThreadSafe){return call_trusted(numeric_hook,hook,x);}
 fn main()->Unit{unsafe{assert(apply(fn_ptr(twice),3.0)==6.0);}print("trusted callback OK");}
 "#);
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(run_c(&m), "trusted callback OK\n");
    reject(
        r#"trust "host";extern "C" fn contract(x:U32)->U32 from "host" guarantees(NoAlloc,NoBlocking,NoIO,NoUnwind,BoundedWork,ThreadSafe);fn allocate(x:U32)->U32{let b:Buffer<U8>=buffer<U8>(1);return x;}unsafe fn bad(x:U32)->U32 guarantees(NoAlloc){return call_trusted(contract,fn_ptr(allocate),x);}"#,
        "violates NoAlloc",
    );
    reject(
        r#"trust "host";extern "C" fn contract(x:U32)->U32 from "host";fn bad(hook:FnPtr<U32,U32>)->U32{return call_trusted(contract,hook,U32(1));}"#,
        "requires unsafe",
    );
}
