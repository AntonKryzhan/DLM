//! Public effectful generic APIs, finite traversal and real I/O integration.
#[path = "support/effect_support.rs"]
mod support;
use dlm_core::{ordinary::canonical_type_text, Runtime};
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::process::Command;
use std::time::{Duration, Instant};
use support::*;

#[test]
fn builtin_associated_projections_normalize_and_check_their_domains() {
    for (input, expected) in [
        ("ResultOf<Fn<Nat,List<ArgOf<F>>>>", "List<ArgOf<Record<F>>>"),
        ("ArgOf<Fn<List<ResultOf<F>>,Nat>>", "List<ResultOf<Record<F>>>"),
        (
            "FnEffect<Stdout + File,Nat,Text>",
            "FnEffect<File+Stdout,Nat,Text>",
        ),
        ("ArgOf<FnEffect<Stdout,Nat,Text>>", "Nat"),
        ("ResultOf<FnEffect<File,Text>>", "Text"),
        ("ItemOf<List<Text>>", "Text"),
        ("ItemOf<NatVec>", "Nat"),
        ("OkOf<Result<List<Nat>,Text>>", "List<Nat>"),
        (
            "ErrorOf<ResultOf<FnEffect<File,Nat,Result<Text,Text>>>>",
            "Text",
        ),
    ] {
        assert_eq!(canonical_type_text(input).unwrap(), expected);
    }
    for input in [
        "ArgOf<Fn<Nat,Text,Bool>>",
        "ResultOf<Nat>",
        "ResultOf<List<ArgOf<F>>>",
        "ArgOf<Fn<Nat,Text,ResultOf<F>>>",
        "ItemOf<Nat>",
        "OkOf<Text>",
        "ErrorOf<List<Text>>",
        "FnEffect<Pure+Partial,Nat,Nat>",
    ] {
        assert!(canonical_type_text(input).is_err(), "accepted {input}");
    }
}

#[test]
fn generic_records_and_callables_infer_associated_argument_and_result_types() {
    let dir = scratch("projected-records");
    let p = library(
        "std.effects",
        "StdEffects",
        r#"
    record Job<F:UnaryCallable>(action:F,input:ArgOf<F>)
    fn first<C>(xs:C,fallback:ItemOf<C>)->ItemOf<C> = unwrap_or(head(xs),fallback)
    fn wrap<F:UnaryCallable>(f:F,x:ArgOf<F>)->ResultOf<Fn<Nat,List<ArgOf<F>>>> = list(x)
    fn typed_identity<Pure>(x:Pure)->FnEffect<Pure,Pure,Pure> = effect_fn(FnEffect(Pure,Pure,Pure),fun(y:Pure,y))
    let neutral = typed_identity(0)
    fn pure_count<F:PureCallable>(f:F)->Nat = 1
    let verified_pure = pure_count(effect_fn(FnEffect(Pure,Nat,Nat),fun(n:Nat,n)))
    fn execute<F:UnaryCallable>(job:Job<F>)->ResultOf<F> = let f=field(job,action) in f(field(job,input))
    let emit=effect_fn(FnEffect(Stdout,Nat,Text),fun(n:Nat,let log=print_line("run") in nat_to_text(n)))
    print_line(execute(Job(emit,first(wrap(emit,neutral(7)),0))))
    print_line(effect_call(8,emit))
    fn zero() -> Text = "zero"
    let thunk=effect_fn(FnEffect(Pure,Text),zero)
    print_line(effect_run(thunk))
    "#,
        &dir,
    );
    assert_eq!(run_both(&p, &dir), ["run", "7", "run", "8", "zero"]);
}

#[test]
fn mapping_folding_and_optional_callbacks_preserve_order() {
    let dir = scratch("ordered");
    let p = library(
        "std.effects",
        "StdEffects",
        r#"
    let emit=effect_fn(FnEffect(Stdout,Nat,Text),fun(n:Nat,let log=print_decimal(n) in nat_to_text(n)))
    print_line(join(effect_map(list(1,2,3),emit),","))
    print_decimal(effect_each(list(4,5),emit))
    let step=effect_fn(FnEffect(Stdout,Nat,Nat,Nat),fun(a:Nat,n:Nat,let log=print_line(concat("fold=",nat_to_text(n))) in a+n))
    print_decimal(effect_fold(list(1,2,3),0,step))
    print_decimal(length(effect_map(empty_list(Nat),emit)))
    print_line(unwrap_or(effect_on_some(some(6),emit),"none"))
    print_line(unwrap_or(effect_on_some(none(Nat),emit),"none"))
    print_line(unwrap_or(effect_on_ok(ok(7,Text),emit),"error"))
    print_line(unwrap_or(effect_on_ok(err("unchanged",Nat),emit),"error"))
    "#,
        &dir,
    );
    assert_eq!(
        run_both(&p, &dir),
        [
            "1", "2", "3", "1,2,3", "4", "5", "2", "fold=1", "fold=2", "fold=3", "6", "0", "6",
            "6", "none", "7", "7", "error"
        ]
    );
}

#[test]
fn predicates_short_circuit_and_empty_collections_do_not_call_back() {
    let dir = scratch("short-circuit");
    let p = library(
        "std.effects",
        "StdEffects",
        r#"
    let keep=effect_fn(FnEffect(Stdout,Nat,Bool),fun(n:Nat,let log=print_decimal(n) in nat_eq(n,2)))
    print_symbolic(effect_any(list(1,2,3),keep))
    print_symbolic(effect_all(list(2,1,3),keep))
    print_decimal(unwrap_or(effect_find(list(1,2,3),keep),0))
    print_decimal(length(effect_filter(list(1,2,3),keep)))
    print_symbolic(effect_all(empty_list(Nat),keep))
    "#,
        &dir,
    );
    assert_eq!(
        run_both(&p, &dir),
        ["1", "2", "true", "2", "1", "false", "1", "2", "2", "1", "2", "3", "1", "true"]
    );
}

#[test]
fn fallible_traversals_stop_on_first_error_and_collect_results_does_not() {
    let dir = scratch("fallible");
    let p = library(
        "std.effects",
        "StdEffects",
        r#"
    let attempt=effect_fn(FnEffect(Stdout,Nat,Result(Nat,Text)),fun(n:Nat,let log=print_decimal(n) in if_then_else(nat_eq(n,2),err("stop",Nat),ok(n,Text))))
    print_line(match effect_try_map(list(1,2,3),attempt) {err(e)=>e,ok(xs)=>"unexpected"})
    print_line(match effect_try_each(list(1,2,3),attempt) {err(e)=>e,ok(n)=>"unexpected"})
    print_decimal(length(effect_collect_results(list(1,2,3),attempt)))
    print_decimal(length(unwrap_or(effect_try_map(list(3,4),attempt),empty_list(Nat))))
    print_decimal(unwrap_or(effect_try_each(empty_list(Nat),attempt),999))
    "#,
        &dir,
    );
    assert_eq!(
        run_both(&p, &dir),
        ["1", "2", "stop", "1", "2", "stop", "1", "2", "3", "3", "3", "4", "2", "0"]
    );
}

#[test]
fn file_transformation_error_preserves_destination_and_success_is_atomic() {
    let dir = scratch("transform");
    let src = dir.join("source.txt");
    let dst = dir.join("destination.txt");
    std::fs::write(&src, "value").unwrap();
    let p = library(
        "std.files",
        "StdFiles",
        &format!(
            r#"
    let initial=file_write_atomic("{}","old")
    let reject=effect_fn(FnEffect(Stdout,Text,Result(Text,Text)),fun(t:Text,let log=print_line("reject") in err("rejected",Text)))
    print_line(match try_transform_text("{}","{}",reject) {{err(e)=>e,ok(n)=>"bad"}})
    print_line(unwrap_or(read_text("{}"),"error"))
    let change=effect_fn(FnEffect(Stdout,Text,Text),fun(t:Text,let log=print_line("change") in concat(t,"!")))
    let done=transform_text("{}","{}",change)
    print_line(unwrap_or(read_text("{}"),"error"))
    "#,
            dst.display(),
            src.display(),
            dst.display(),
            dst.display(),
            src.display(),
            dst.display(),
            dst.display()
        ),
        &dir,
    );
    assert_eq!(
        run_both(&p, &dir),
        ["reject", "rejected", "old", "change", "value!"]
    );
    assert_eq!(std::fs::read_to_string(dst).unwrap(), "value!");
}

#[test]
fn callable_constraints_and_unresolved_projections_are_rejected() {
    for body in [
        "fn invoke<F:Callable>(f:F)->Nat=0\nlet n=invoke(1)",
        "record R(x:Nat)\nfn invalid(r:R)->ResultOf<R>=r",
        "fn pure<F:PureCallable>(f:F)->Nat=0\nlet f=effect_fn(FnEffect(Stdout,Nat,Text),fun(n:Nat,print_decimal(n)))\nlet n=pure(f)",
        "fn unary<F:UnaryCallable>(f:F)->Nat=0\nlet n=unary(fun(a:Nat,b:Nat,a+b))",
        "class Callable<T> { fn apply(x:T)->T }",
    ] { rejects(body); }
}

fn client(port: u16) -> std::thread::JoinHandle<String> {
    std::thread::spawn(move || {
        let until = Instant::now() + Duration::from_secs(5);
        let mut socket = loop {
            match TcpStream::connect(("127.0.0.1", port)) {
                Ok(s) => break s,
                Err(e) => {
                    assert!(Instant::now() < until, "connect: {e}");
                    std::thread::sleep(Duration::from_millis(10));
                }
            }
        };
        socket
            .set_read_timeout(Some(Duration::from_secs(5)))
            .unwrap();
        socket
            .write_all(b"GET /file HTTP/1.0\r\nHost: localhost\r\n\r\n")
            .unwrap();
        let mut response = String::new();
        socket.read_to_string(&mut response).unwrap();
        response
    })
}
#[test]
fn http_handler_can_read_a_file_and_log_the_request() {
    let dir = scratch("http");
    let file = dir.join("body.txt");
    std::fs::write(&file, "response body").unwrap();
    let reservation = TcpListener::bind(("127.0.0.1", 0)).unwrap();
    let port = reservation.local_addr().unwrap().port();
    drop(reservation);
    let p = library(
        "std.net",
        "StdNet",
        &format!(
            r#"
    let handler=effect_fn(FnEffect(File+Stdout,Text,Text),fun(path:Text,let log=print_line(path) in let f=file_open("{}","r") in let text=file_read_all(f) in let close=file_close(f) in text))
    let listener=unwrap_or(net_listen("127.0.0.1",{}),0)
    let served=http_serve_one(listener,3000,handler)
    let closed=net_close(listener)
    print_line(concat("served ",unwrap_or(served,"error")))
    "#,
            file.display(),
            port
        ),
        &dir,
    );
    let exe = build(&p, &dir);
    let request = client(port);
    let result = Runtime::new().run_program(&p).unwrap().output;
    let response = request.join().unwrap();
    assert!(response.starts_with("HTTP/1.0 200 OK"));
    assert!(response.ends_with("response body"));
    assert_eq!(result, ["/file", "served /file"]);
    let request = client(port);
    let result = Command::new(exe).output().unwrap();
    let response = request.join().unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    assert_eq!(
        String::from_utf8(result.stdout).unwrap(),
        "/file\nserved /file\n"
    );
    assert!(response.ends_with("response body"));
}

#[test]
fn streaming_file_callbacks_close_on_success_failure_and_missing_input() {
    let dir = scratch("streaming");
    let file = dir.join("lines.txt");
    std::fs::write(&file, "a\nb\nc\n").unwrap();
    let p = library(
        "std.files",
        "StdFiles",
        &format!(
            r#"
    let action=effect_fn(FnEffect(Stdout,Text,Nat),fun(line:Text,let log=print_line(join(words(line),"")) in 1))
    print_decimal(unwrap_or(each_line("{}",action),999))
    let fallible=effect_fn(FnEffect(Stdout,Text,Result(Nat,Text)),fun(line:Text,let text=join(words(line),"") in let log=print_line(text) in if_then_else(text_eq(text,"b"),err("stop",Nat),ok(1,Text))))
    print_line(match try_each_line("{}",fallible) {{err(e)=>e,ok(n)=>"unexpected"}})
    print_line(if_then_else(is_err(each_line("{}/missing",action)),"missing","unexpected"))
    "#,
            file.display(),
            file.display(),
            dir.display()
        ),
        &dir,
    );
    assert_eq!(
        run_both(&p, &dir),
        ["a", "b", "c", "3", "a", "b", "stop", "missing"]
    );
}

#[test]
fn large_traversals_use_iteration_and_support_sequences() {
    let dir = scratch("large");
    let p = library(
        "std.effects",
        "StdEffects",
        r#"
    let xs=nv_to_list(unwrap_or(nv_new(50000,1),nv_empty()))
    let action=effect_fn(FnEffect(Pure,Nat,Nat),fun(n:Nat,n+1))
    print_decimal(effect_each(xs,action))
    print_decimal(length(effect_map(xs,action)))
    print_decimal(length(effect_filter(xs,fun(n:Nat,true))))
    print_decimal(effect_fold(xs,0,fun(a:Nat,n:Nat,a+n)))
    print_decimal(length(map_effect(sequence(1,2),action)))
    "#,
        &dir,
    );
    assert_eq!(
        run_both(&p, &dir),
        ["50000", "50000", "50000", "50000", "2"]
    );
}
