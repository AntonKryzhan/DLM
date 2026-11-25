use dlm_core::{
    lower_program,
    native::{self, Mir},
    parse_module, CheckPolicy, Program,
};
use std::{
    path::PathBuf,
    process::Command,
    sync::atomic::{AtomicUsize, Ordering},
};
static SERIAL: AtomicUsize = AtomicUsize::new(0);
fn directory() -> PathBuf {
    let p = std::env::temp_dir().join(format!(
        "dlm-future-{}-{}",
        std::process::id(),
        SERIAL.fetch_add(1, Ordering::Relaxed)
    ));
    std::fs::create_dir_all(&p).unwrap();
    p
}
fn mir(s: &str) -> Mir {
    lower_program(
        &Program::single(parse_module(&format!("module test.future\nnative {{ {s} }}")).unwrap()),
        CheckPolicy::default(),
    )
    .unwrap()
    .native
}
fn both(s: &str, want: &str) {
    let m = mir(s);
    let mut vm = native::vm::Vm::new(&m);
    vm.run().unwrap();
    assert_eq!(vm.output.join("\n"), want);
    let d = directory();
    let path = d.join(if cfg!(windows) { "async.exe" } else { "async" });
    let exe = native::build::build(&m, &path, &Default::default(), false).unwrap();
    let result = Command::new(exe).output().unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    assert_eq!(
        String::from_utf8(result.stdout)
            .unwrap()
            .replace("\r\n", "\n"),
        format!("{want}\n")
    );
    std::fs::remove_dir_all(d).unwrap();
}
#[test]
fn async_function_resumes_and_returns_owned_data() {
    both(
        r#"
async fn value(x:U64)->U64{await yield_now();return x+U64(1);}
async fn output()->Buffer<U64>{let mut b:Buffer<U64>=buffer<U64>(2);b[0]=await value(U64(6));await sleep(2);b[1]=await value(U64(8));return move b;}
fn main()->Unit{let result:Buffer<U64>=block_on(output());assert(result[0]==U64(7));assert(result[1]==U64(9));print("state machine OK");}
"#,
        "state machine OK",
    );
}
#[test]
fn async_main_awaits_child_tasks_on_one_worker() {
    both(
        r#"
async fn value(x:U64)->U64{await sleep(1);return x;}
async fn main()->Unit{let a:Task<U64>=spawn_child(value(U64(12)));let b:Task<U64>=spawn_child(value(U64(30)));let result:U64=(await move a)+(await move b);assert(result==U64(42));print("async main OK");}
"#,
        "async main OK",
    );
}
#[test]
fn async_channels_apply_backpressure_without_blocking_the_only_worker() {
    both(
        r#"
async fn produce(tx:Sender<U64>)->U64{for i in 0..100{let ok:Bool=await send_async(sender_clone(&tx),cast<U64>(i));assert(ok);}return 100;}
async fn consume(rx:Receiver<U64>)->U64{let mut total:U64=0;for i in 0..100{total=total+(await receive_async(receiver_clone(&rx)));}return total;}
async fn main()->Unit{let tx:Sender<U64>=channel<U64>(1);let rx:Receiver<U64>=channel_receiver(&tx);let a:Task<U64>=spawn_child(produce(sender_clone(&tx)));let b:Task<U64>=spawn_child(consume(move rx));assert((await move a)==U64(100));assert((await move b)==U64(4950));print("async channels OK");}
"#,
        "async channels OK",
    );
}
#[test]
fn async_loops_branches_continue_break_and_select_preserve_order() {
    both(
        r#"
async fn pick(x:U64,ms:U32)->U64{await sleep(ms);return x;}
async fn flag()->Bool{await yield_now();return true;}
async fn main()->Unit{let mut sum:U64=0;for i in 0..10{await yield_now();if i==USize(2){continue;}if i==USize(8){break;}sum=sum+cast<U64>(i);}assert(sum==U64(26));let mut n:U64=0;while n<U64(3){n=n+U64(1);await yield_now();}assert(n==U64(3));let b:Bool=false&&(await flag());assert(!b);let c:Bool=true||(await flag());assert(c);let winner:U64=await select(pick(U64(1),1000),pick(U64(2),0));assert(winner==U64(2));print("async control flow OK");}
"#,
        "async control flow OK",
    );
}
fn reject(s: &str, why: &str) {
    let module = parse_module(&format!("module test.future\nnative {{ {s} }}")).unwrap();
    let errors = native::check_program(&Program::single(module)).unwrap_err();
    assert!(
        errors.iter().any(|e| e.message.contains(why)),
        "expected {why}: {errors:?}"
    );
}
#[test]
fn suspension_rejects_borrowed_captures_live_views_and_duplicate_future_owners() {
    reject(
        "fn main()->Unit{await sleep(1);}",
        "only valid inside async",
    );
    reject(
        "async fn f(x:&U64)->U64{await yield_now();return *x;}",
        "owned Send-safe",
    );
    reject(
        "async fn main()->Unit{let x:U64=1;let view:&U64=&x;await yield_now();print(*view);}",
        "remain live across await",
    );
    reject(
        "async fn main()->Unit{let f:Future<Unit>=sleep(1);let copy:Future<Unit>=f;}",
        "explicit move",
    );
    reject(
        "async fn main()->Unit{let f:Future<Unit>=sleep(1);await move f;await move f;}",
        "use after move",
    );
    reject(
        "fn main()->Unit{let f:Future<Unit>=zero<Future<Unit>>();}",
        "zero",
    );
    reject(
        "export async fn f()->U64{await yield_now();return 1;}",
        "without export",
    );
    reject("fn sum(x:&U64,y:U64)->U64 guarantees(ThreadSafe){return *x+y;}async fn f()->U64{await yield_now();return 2;}async fn main()->Unit{let x:U64=1;let y:U64=sum(&x,await f());}","remain live across await");
}
#[test]
fn owned_temporaries_resizing_and_scoped_borrows_survive_suspension() {
    both(
        r#"
record Payload repr(C){data:Buffer<U64>;value:U64;}
async fn value()->U64{await yield_now();return 42;}
async fn payload()->Payload{let mut bytes:Buffer<U64>=buffer<U64>(2);await yield_now();resize(&mut bytes,64);bytes[63]=U64(9);await yield_now();return Payload(move bytes,await value());}
async fn main()->Unit{let p:Payload=await payload();{let v:&U64=&p.value;assert(*v==U64(42));}await yield_now();let bytes:Buffer<U64>=move p.data;assert(bytes[63]==U64(9));let x:U64=bytes[cast<USize>(await value())];assert(x==U64(0));print("async ownership OK");}
"#,
        "async ownership OK",
    );
}
#[test]
fn explicit_executor_accepts_lazy_futures_and_cancels_suspended_jobs() {
    both(
        r#"
async fn result()->U64{await yield_now();return 42;}
async fn slow()->U64{let bytes:Buffer<U64>=buffer<U64>(16);await sleep(10000);return 9;}
fn main()->Unit{let pool:Executor=executor(1,8);let task:Task<U64>=spawn(&pool,result());assert(join(move task)==U64(42));let cancelled:Task<U64>=spawn(&pool,slow());assert(task_cancel(&cancelled));assert(task_wait(&cancelled,U32(4294967295))==U32(3));print("async executor OK");}
"#,
        "async executor OK",
    );
}
#[test]
fn selecting_cancels_nested_children_without_deadlocking_the_only_worker() {
    both(
        r#"
async fn child()->U64{await sleep(10000);return 9;}
async fn slow()->U64{let task:Task<U64>=spawn_child(child());return await move task;}
async fn fast()->U64{await yield_now();return 42;}
async fn main()->Unit{let value:U64=await select(slow(),fast());assert(value==U64(42));print("select cancellation OK");}
"#,
        "select cancellation OK",
    );
}
#[test]
fn mutex_and_rwlock_share_owned_state_between_cpu_tasks_and_futures() {
    both(
        r#"
fn increment(value:&mut U64,amount:U64)->U64 guarantees(ThreadSafe){*value=*value+amount;return *value;}
fn read(value:&U64)->U64 guarantees(ThreadSafe){return *value;}
fn cpu(counter:Mutex<U64>)->U64 guarantees(ThreadSafe){for i in 0..1000{let v:U64=mutex_with(&counter,increment,U64(1));}return 1000;}
async fn future(counter:Mutex<U64>,data:RwLock<U64>)->U64{await yield_now();let v:U64=mutex_with(&counter,increment,U64(2));let w:U64=rw_write(&data,increment,v);await yield_now();return rw_read(&data,read);}
fn main()->Unit{let pool:Executor=executor(2,8);let counter:Mutex<U64>=mutex<U64>(U64(0));let first:Task<U64>=spawn(&pool,cpu,mutex_clone(&counter));let second:Task<U64>=spawn(&pool,cpu,mutex_clone(&counter));assert(join(move first)==U64(1000));assert(join(move second)==U64(1000));let data:RwLock<U64>=rwlock<U64>(U64(40));let value:U64=block_on(future(mutex_clone(&counter),rwlock_clone(&data)));assert(value==U64(2042));assert(!lock_poisoned(&counter));print("shared locks OK");}
"#,
        "shared locks OK",
    );
}
#[test]
fn lock_errors_poison_writers_and_allow_explicit_recovery() {
    both(
        r#"
fn broken(value:&mut U64)->U64 guarantees(ThreadSafe){*value=U64(7);assert(false);return 1;}
fn read(value:&mut U64)->U64 guarantees(ThreadSafe){return *value;}
fn worker(lock:Mutex<U64>)->U64 guarantees(ThreadSafe){return mutex_with(&lock,broken);}
fn main()->Unit{let pool:Executor=executor(1,4);let lock:Mutex<U64>=mutex<U64>(U64(0));let t:Task<U64>=spawn(&pool,worker,mutex_clone(&lock));assert(task_wait(&t,U32(4294967295))==U32(4));assert(lock_poisoned(&lock));lock_clear_poison(&lock);assert(mutex_with(&lock,read)==U64(7));print("poison recovery OK");}
"#,
        "poison recovery OK",
    );
}
#[test]
fn lock_callbacks_cannot_escape_borrows_or_use_wrong_mutability() {
    reject("fn f(x:&U64)->U64 guarantees(ThreadSafe){return *x;}fn main()->Unit{let m:Mutex<U64>=mutex<U64>(U64(0));let v:U64=mutex_with(&m,f);}","matching borrowed value");
    reject("fn f(x:&mut U64)->U64 guarantees(ThreadSafe){return *x;}fn main()->Unit{let m:RwLock<U64>=rwlock<U64>(U64(0));let v:U64=rw_read(&m,f);}","matching borrowed value");
    reject(
        "fn main()->Unit{let m:Mutex<U64>=zero<Mutex<U64>>();}",
        "zero",
    );
}
#[test]
fn dln_scope_completions_resume_futures_and_abandoned_waits_cancel_operations() {
    let d = directory();
    let source = d.join("network.dlm");
    std::fs::write(&source,r#"
module test.network_future
import std.net_tasks
native {
    async fn completion(future:Future<ReactorCompletion>)->ReactorCompletion{return await move future;}
    fn discard(future:Future<ReactorCompletion>)->Unit{}
    fn main()->Unit{
        let cancel:Resource<OpaqueHandle,net_cancel_dispose>=network_cancel();
        let mut scope:NetworkScope=scope_open(1,4,32,1);
        let operation:NetworkOperation=scope_timer(&mut scope,2);assert(network_ok(&operation.error));
        let mut task:NetworkTask=scope_task(operation);
        let pool:Executor=executor(1,8);let result:Task<ReactorCompletion>=spawn(&pool,completion(move task.future));
        for i in 0..1000{let progress:NetworkTransfer=scope_poll(&mut scope,1,&cancel);let state:U32=scope_task_poll(&mut scope,&mut task.control);if task_status(&result)>=U32(2){break;}}
        assert(task_wait(&result,1000)==U32(2));let value:ReactorCompletion=join(move result);assert(value.operation==operation.id);assert(network_ok(&value.transfer.error));
        let released:NetworkTransfer=scope_ack(&mut scope,operation.id);assert(network_ok(&released.error));
        let pending:NetworkOperation=scope_timer(&mut scope,60000);let mut abandoned:NetworkTask=scope_task(pending);discard(move abandoned.future);
        let mut state:U32=scope_task_poll(&mut scope,&mut abandoned.control);
        for i in 0..1000{if state==U32(2){break;}let progress:NetworkTransfer=scope_poll(&mut scope,1,&cancel);state=scope_task_poll(&mut scope,&mut abandoned.control);}
        assert(state==U32(2));let stats:ScopeStatistics=scope_statistics(&scope);assert(stats.reactor.pending==U32(0));assert(stats.reactor.completed==U32(0));
        print("DLN future bridge OK");
    }
}
"#).unwrap();
    let lib = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../lib");
    let program = dlm_core::load_program_with_paths(&source, &[lib]).unwrap();
    let m = lower_program(&program, CheckPolicy::allow_unsafe())
        .unwrap()
        .native;
    let exe = native::build::build(&m, &d.join("network"), &Default::default(), false).unwrap();
    let output = Command::new(exe).output().unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(
        String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n"),
        "DLN future bridge OK\n"
    );
    std::fs::remove_dir_all(d).unwrap();
}
#[test]
fn rwlock_allows_two_read_callbacks_to_make_progress_together() {
    both(
        r#"
fn inspect(value:&U64,ready:Sender<U64>,release:Receiver<U64>)->U64 guarantees(ThreadSafe){assert(send(&ready,U64(1)));let mut signal:U64=0;assert(receive(&release,&mut signal));return *value;}
fn reader(lock:RwLock<U64>,ready:Sender<U64>,release:Receiver<U64>)->U64 guarantees(ThreadSafe){return rw_read(&lock,inspect,move ready,move release);}
fn main()->Unit{let pool:Executor=executor(2,8);let data:RwLock<U64>=rwlock<U64>(U64(42));let ready:Sender<U64>=channel<U64>(2);let signals:Receiver<U64>=channel_receiver(&ready);let release:Sender<U64>=channel<U64>(2);let receivers:Receiver<U64>=channel_receiver(&release);let a:Task<U64>=spawn(&pool,reader,rwlock_clone(&data),sender_clone(&ready),receiver_clone(&receivers));let b:Task<U64>=spawn(&pool,reader,rwlock_clone(&data),sender_clone(&ready),receiver_clone(&receivers));let mut signal:U64=0;assert(receive(&signals,&mut signal));assert(receive(&signals,&mut signal));assert(send(&release,U64(1)));assert(send(&release,U64(1)));assert(join(move a)==U64(42));assert(join(move b)==U64(42));print("parallel readers OK");}
"#,
        "parallel readers OK",
    );
}
#[test]
fn cancellation_failure_and_shared_buffers_release_all_allocations() {
    let m = mir(r#"
record Packet repr(C){bytes:Buffer<U64>;tag:U64;}
async fn fail()->U64{await yield_now();assert(false);return 1;}
async fn bad_packet()->Packet{let bytes:Buffer<U64>=buffer<U64>(64);return Packet(move bytes,await fail());}
async fn slow()->Buffer<U64>{let bytes:Buffer<U64>=buffer<U64>(64);await sleep(60000);return move bytes;}
async fn fast()->Buffer<U64>{await yield_now();return buffer<U64>(4);}
fn mutate(value:&mut Buffer<U64>)->U64 guarantees(ThreadSafe){resize(value,128);let local:Buffer<U64>=buffer<U64>(32);assert(false);return 1;}
fn change(lock:Mutex<Buffer<U64>>)->U64 guarantees(ThreadSafe){return mutex_with(&lock,mutate);}
export fn exercise()->U64{let result:Buffer<U64>=block_on(select(slow(),fast()));assert(len(&result)==USize(4));let lock:Mutex<Buffer<U64>>=mutex<Buffer<U64>>(buffer<U64>(8));let pool:Executor=executor(1,8);let task:Task<U64>=spawn(&pool,change,mutex_clone(&lock));assert(task_wait(&task,U32(4294967295))==U32(4));assert(lock_poisoned(&lock));return 42;}
export fn broken()->U64{let result:Packet=block_on(bad_packet());return result.tag;}
"#);
    let d = directory();
    let source = d.join("stress.c");
    let exe = d.join(if cfg!(windows) {
        "stress.exe"
    } else {
        "stress"
    });
    let mut c = native::emit::emit(&m, false);
    c.push_str(r#"
#ifdef DNC_TRACK_ALLOCATIONS
void *__real_calloc(size_t,size_t);void __real_free(void*);static _Atomic uint64_t live_blocks;
void *__wrap_calloc(size_t n,size_t s){void*p=__real_calloc(n,s);if(p)atomic_fetch_add(&live_blocks,1);return p;}
void __wrap_free(void*p){if(p)atomic_fetch_sub(&live_blocks,1);__real_free(p);}
static bool clean(void){return !dlm_native_live_allocations()&&!atomic_load(&live_blocks);}
#else
static bool clean(void){return !dlm_native_live_allocations();}
#endif
int main(void){for(int i=0;i<30;i++){if(exercise()!=42||dlm_native_last_error()||!clean())return 31;(void)broken();if(!dlm_native_last_error()||!clean())return 32;dlm_native_clear_error();}return dlm_native_runtime_shutdown()?0:33;}
"#);
    std::fs::write(&source, c).unwrap();
    let mut command = Command::new(dlm_core::default_c_compiler());
    command
        .args(["-std=gnu11", "-O1"])
        .arg(&source)
        .args(["-pthread", "-lm", "-o"])
        .arg(&exe);
    if cfg!(target_os = "linux") {
        command.args([
            "-DDNC_TRACK_ALLOCATIONS",
            "-Wl,--wrap=calloc",
            "-Wl,--wrap=free",
        ]);
    }
    let output = command.output().unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let output = Command::new(exe).output().unwrap();
    assert!(
        output.status.success(),
        "exit {:?}: {}",
        output.status.code(),
        String::from_utf8_lossy(&output.stderr)
    );
    std::fs::remove_dir_all(d).unwrap();
}
#[test]
fn blocking_tasks_run_outside_the_only_async_worker() {
    both(
        r#"
fn blocked(ready:Sender<U64>,release:Receiver<U64>)->U64 guarantees(ThreadSafe){assert(send(&ready,U64(1)));let mut signal:U64=0;assert(receive(&release,&mut signal));return signal;}
async fn main()->Unit{let ready:Sender<U64>=channel<U64>(1);let signals:Receiver<U64>=channel_receiver(&ready);let release:Sender<U64>=channel<U64>(1);let receiver:Receiver<U64>=channel_receiver(&release);let work:Task<U64>=spawn_blocking(blocked,sender_clone(&ready),move receiver);assert((await receive_async(move signals))==U64(1));assert(await send_async(move release,U64(42)));assert((await move work)==U64(42));print("isolated blocking pool OK");}
"#,
        "isolated blocking pool OK",
    );
}
#[test]
fn blocking_offload_requires_checked_owned_threadsafe_arguments() {
    reject("fn f()->U64 guarantees(ThreadSafe){return 1;}fn main()->Unit{let t:Task<U64>=spawn_blocking(f);}", "requires an async scope");
    reject(
        "fn f()->U64{return 1;}async fn main()->Unit{let t:Task<U64>=spawn_blocking(f);}",
        "checked ThreadSafe",
    );
    reject("fn f(x:&U64)->U64 guarantees(ThreadSafe){return *x;}async fn main()->Unit{let x:U64=1;let t:Task<U64>=spawn_blocking(f,&x);}", "owned Send-safe");
}
#[test]
fn deterministic_vm_replays_async_order_and_virtual_timers() {
    let m = mir(r#"
async fn work(id:U64,delay:U32)->U64{print(id);await sleep(delay);print(id+U64(10));return id;}
async fn main()->Unit{let a:Task<U64>=spawn_child(work(U64(1),8));let b:Task<U64>=spawn_child(work(U64(2),1));assert((await move a)==U64(1));assert((await move b)==U64(2));}
"#);
    for _ in 0..20 {
        let mut vm = native::vm::Vm::new(&m).with_deterministic_scheduler();
        vm.run().unwrap();
        assert_eq!(vm.output, ["1", "2", "12", "11"]);
    }
    let m = mir("async fn main()->Unit{while true{await yield_now();}}");
    let mut vm = native::vm::Vm::new(&m)
        .with_step_limit(50)
        .with_deterministic_scheduler();
    assert!(vm.run().is_err());
}
#[test]
fn deterministic_vm_drives_explicit_executors_and_cancellation() {
    let m = mir(r#"
async fn value()->U64{await sleep(2);return 42;}
async fn slow()->U64{await sleep(1000000);return 9;}
fn main()->Unit{let pool:Executor=executor(2,8);let task:Task<U64>=spawn(&pool,value());assert(task_status(&task)==U32(0));assert(task_wait(&task,0)==U32(0));assert(join(move task)==U64(42));let cancelled:Task<U64>=spawn(&pool,slow());assert(task_cancel(&cancelled));assert(task_wait(&cancelled,10)==U32(3));print("deterministic executor OK");}
"#);
    let mut vm = native::vm::Vm::new(&m).with_deterministic_scheduler();
    vm.run().unwrap();
    assert_eq!(vm.output, ["deterministic executor OK"]);
}
#[test]
fn external_cancellation_reaps_nested_tasks_on_the_only_worker() {
    both(
        r#"
async fn slow()->U64{await sleep(60000);return 1;}
async fn parent(ready:Sender<U64>)->U64{let child:Task<U64>=spawn_child(slow());assert(await send_async(move ready,U64(1)));return await move child;}
fn main()->Unit{let ready:Sender<U64>=channel<U64>(1);let signal:Receiver<U64>=channel_receiver(&ready);let pool:Executor=executor(1,8);let parent:Task<U64>=spawn(&pool,parent(sender_clone(&ready)));let mut value:U64=0;assert(receive(&signal,&mut value));assert(task_cancel(&parent));assert(task_wait(&parent,U32(4294967295))==U32(3));print("external nested cancellation OK");}
"#,
        "external nested cancellation OK",
    );
}

#[test]
fn parked_futures_reserve_capacity_and_cancel_before_closing_endpoints() {
    both(
        r#"
async fn parked(ready: Sender<U64>, input: Receiver<U64>) -> U64 {
    assert(await send_async(move ready, U64(1)));
    return await receive_async(move input);
}
fn never_queued() -> U64 guarantees(ThreadSafe) { return 9; }
fn main() -> Unit {
    let pool: Executor = executor(2, 1);
    for i in 0..40 {
        let ready: Channel<U64> = channel<U64>(1);
        let started: Receiver<U64> = channel_receiver(&ready);
        let tx: Sender<U64> = channel<U64>(1);
        let rx: Receiver<U64> = channel_receiver(&tx);
        let task: JoinHandle<U64> = spawn(&pool, parked(move ready, move rx));
        let mut signal: U64 = 0;
        assert(receive(&started, &mut signal));
        let refused: Task<U64> = spawn(&pool, never_queued);
        assert(task_wait(&refused, 1000) == U32(5));
        let spare: Sender<U64> = channel<U64>(1);
        let spare_rx: Receiver<U64> = channel_receiver(&spare);
        let refused_future: Task<U64> = spawn(&pool, receive_async(move spare_rx));
        assert(task_status(&refused_future) == U32(5));
        assert(!sender_connected(&spare));
        if i % USize(2) == USize(0) {
            assert(try_send(&tx, U64(42)));
            assert(task_wait(&task, 1000) == U32(2));
            assert(join(move task) == U64(42));
        } else {
            assert(task_cancel(&task));
            assert(task_wait(&task, 1000) == U32(3));
            assert(!sender_connected(&tx));
        }
    }
    print("capacity and cancellation OK");
}
"#,
        "capacity and cancellation OK",
    );
}

#[test]
fn async_return_cannot_hide_a_live_child_in_a_future_factory() {
    reject(
        "async fn escape(task:Task<U64>)->U64{return await move task;}",
        "owned Send-safe parameters",
    );
}

#[test]
fn existing_native_functions_take_precedence_over_contextual_intrinsics() {
    both(
        r#"
fn receive(x: U64) -> U64 guarantees(ThreadSafe) { return x; }
fn select(x: U64) -> U64 guarantees(ThreadSafe) { return x + U64(1); }
fn spawn(x: U64, y: U64) -> U64 guarantees(ThreadSafe) { return x + y; }
fn parallel_map(x: U64) -> U64 guarantees(ThreadSafe) { return x * 2; }
async fn produce() -> U64 { await yield_now(); return U64(10); }
async fn main() -> Unit {
    let x: U64 = receive(await produce());
    let result: U64 = spawn(parallel_map(x), select(await produce()));
    assert(result == U64(31));
    print("existing declarations OK");
}
"#,
        "existing declarations OK",
    );
}
