use dlm_core::{CheckPolicy, load_program_with_paths, lower_program, native};
use std::fs;
use std::io::{BufRead, BufReader, ErrorKind, Read, Write};
use std::net::{Shutdown, SocketAddr, TcpListener, TcpStream};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::thread;
use std::time::{Duration, Instant};

fn temporary() -> PathBuf {
    static SERIAL: AtomicUsize = AtomicUsize::new(0);
    let serial = SERIAL.fetch_add(1, Ordering::Relaxed);
    let path = std::env::temp_dir().join(format!("dlm-s2-{}-{serial}", std::process::id()));
    fs::create_dir_all(&path).unwrap();
    path
}

fn lower(directory: &Path, source: &str, policy: CheckPolicy) -> native::Mir {
    let file = directory.join("s2.dlm");
    let lib = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../lib");
    fs::write(&file, source).unwrap();
    let program = load_program_with_paths(&file, &[lib]).unwrap();
    lower_program(&program, policy).unwrap().native
}

fn compile(mir: &native::Mir, directory: &Path) -> PathBuf {
    let executable = directory.join(if cfg!(windows) { "test.exe" } else { "test" });
    native::build::build(mir, &executable, &Default::default(), false).unwrap()
}

fn run(executable: &Path, expected: &str) {
    let output = Command::new(executable).output().unwrap();
    assert!(
        output.status.success(),
        "{}\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(String::from_utf8_lossy(&output.stdout).trim(), expected);
}

const JSON: &str = r###"
module test.s2_json
import std.json
native {
    fn reject(input: Utf8, code: U32) -> Unit {
        let doc: JsonDocument = json_parse(utf8_bytes(input),JsonLimits(512,32,8));
        assert(doc.error.code == code);
    }
    fn main() -> Unit {
        let data: Slice<U8> = utf8_bytes("{\"n\":18446744073709551615,\"i\":-9223372036854775808,\"s\":\"é\\uD83D\\uDE03\",\"f\":5e-324,\"nullable\":null,\"xs\":[true,false,\"\\u0000\"]}");
        let mut input: Buffer<U8> = buffer<U8>(len(data));
        for i in 0..len(data) { input[i]=data[i]; }
        let mut parser: JsonParser = json_parser(JsonLimits(512,32,8));
        for i in 0..len(&input) {
            let part: JsonError = json_feed(&mut parser,slice(&input,i,1));
            assert(part.code == 0);
        }
        let doc: JsonDocument = json_finish(move parser);
        assert(doc.error.code == 0);
        let mut fields: Buffer<JsonFieldSpec> = buffer<JsonFieldSpec>(0);
        push(&mut fields,json_field(utf8_bytes("n"),8,false,false));
        push(&mut fields,json_field(utf8_bytes("i"),7,false,false));
        push(&mut fields,json_field(utf8_bytes("s"),4,false,false));
        push(&mut fields,json_field(utf8_bytes("f"),9,false,false));
        push(&mut fields,json_field(utf8_bytes("nullable"),4,false,true));
        push(&mut fields,json_field(utf8_bytes("missing"),2,true,true));
        push(&mut fields,json_field(utf8_bytes("xs"),5,false,false));
        let values: JsonRecord = json_decode_record(&doc,doc.root,slice(&fields,0,len(&fields)),true);
        assert(values.error.code == 0);
        assert(values.fields[0].unsigned == U64(18446744073709551615));
        assert(values.fields[1].signed == I64(-9223372036854775808));
        let decoded: Slice<U8> = json_text(&doc,values.fields[2].node);
        assert(len(decoded) == 6 && decoded[0] == 195 && decoded[5] == 131);
        assert(bits(values.fields[3].real) == U64(1));
        assert(values.fields[4].kind == 1 && values.fields[5].node == 0);
        let encoded: JsonEncoded = json_encode(&doc,512);
        assert(encoded.error.code == 0);
        let again: JsonDocument = json_parse(slice(&encoded.bytes,0,len(&encoded.bytes)),JsonLimits(512,32,8));
        let second: JsonEncoded = json_encode(&again,512);
        assert(len(&second.bytes) == len(&encoded.bytes));
        for i in 0..len(&second.bytes) { assert(second.bytes[i] == encoded.bytes[i]); }
        let too_small: JsonEncoded = json_encode(&doc,5);
        assert(too_small.error.code == 3);
        let huge: JsonDocument = json_parse(utf8_bytes("1e999"),JsonLimits(512,32,8));
        assert(huge.error.code == 0);
        let overflow: JsonFloat = json_float(&huge,huge.root);
        assert(!overflow.valid);
        let small: JsonDocument = json_parse(utf8_bytes("0.00000000000000000000001e23"),JsonLimits(512,32,8));
        let exact: JsonInteger = json_integer(&small,small.root);
        assert(exact.valid && exact.magnitude == 1);
        let minus_zero: JsonDocument = json_parse(utf8_bytes("-0e99"),JsonLimits(512,32,8));
        let fzero: JsonFloat = json_float(&minus_zero,minus_zero.root);
        assert(fzero.valid && sign_bit(fzero.value));
        let binary: Array<U8,4> = array<Array<U8,4>>(U8(34),U8(192),U8(128),U8(34));
        let invalid: JsonDocument = json_parse(slice(&binary,0,4),JsonLimits(512,32,8));
        assert(invalid.error.code == 2);
        reject("{\"a\":1,\"\\u0061\":2}",4);
        reject("[1,]",1);
        reject("1.e2",1);
        reject("01",1);
        reject("\"\\uD800\"",2);
        let deep: JsonDocument = json_parse(utf8_bytes("[[0]]"),JsonLimits(12,8,1));
        assert(deep.error.code == 3);
        let count: JsonDocument = json_parse(utf8_bytes("[1,2]"),JsonLimits(12,2,4));
        assert(count.error.code == 3);
        let truncated: JsonDocument = json_parse(utf8_bytes("\"abcdef\""),JsonLimits(6,8,4));
        assert(truncated.error.code == 3);
        print("JSON parity OK");
    }
}
"###;

#[test]
fn json_streaming_and_typed_conversion_match_native_and_vm() {
    let directory = temporary();
    let mir = lower(&directory, JSON, CheckPolicy::default());
    let mut vm = native::vm::Vm::new(&mir);
    vm.run().unwrap();
    assert_eq!(vm.output, ["JSON parity OK"]);
    run(&compile(&mir, &directory), "JSON parity OK");
    fs::remove_dir_all(&directory).unwrap();
}

fn literal(value: &str) -> String {
    format!(
        "\"{}\"",
        value
            .replace('\\', "\\\\")
            .replace('"', "\\\"")
            .replace('\n', "\\n")
            .replace('\r', "\\r")
    )
}

const SQL: &str = r###"
module test.s2_sql
import std.sql
native {
    fn bytes(s: Utf8) -> Buffer<U8> {
        let data: Slice<U8> = utf8_bytes(s);
        let mut out: Buffer<U8> = buffer<U8>(len(data));
        for i in 0..len(data) { out[i]=data[i]; }
        return move out;
    }
    fn migrations(fail: Bool) -> Buffer<DbMigration> {
        let mut changes: Buffer<DbMigration> = buffer<DbMigration>(0);
        push(&mut changes,DbMigration(1,bytes("CREATE TABLE dlm_s2_items (id BIGINT PRIMARY KEY, message TEXT, amount SQL_DECIMAL_TYPE)")));
        push(&mut changes,DbMigration(2,bytes("CREATE INDEX dlm_s2_index ON dlm_s2_items (message)")));
        if fail {
            push(&mut changes,DbMigration(3,bytes("CREATE TABLE dlm_s2_rolled_back (id BIGINT)")));
            push(&mut changes,DbMigration(4,bytes("INSERT INTO dlm_s2_missing VALUES (1)")));
        }
        return move changes;
    }
    fn insert(tx: &mut DbTransaction) -> Unit {
        let mut statement: DbStatement = db_prepare_tx(tx,utf8_bytes("INSERT INTO dlm_s2_items VALUES (P1,P2,P3)"),3,1000);
        assert(db_statement_error(&statement).kind == 0);
        let id: DbValue = db_int(42);
        let message: DbValue = db_null(3);
        let fraction: DbValue = db_decimal(bytes("9007199254740993.123456789012345"));
        assert(db_bind(&mut statement,1,&id).kind == 0);
        assert(db_bind(&mut statement,2,&message).kind == 0);
        assert(db_bind(&mut statement,3,&fraction).kind == 0);
        assert(db_execute(move statement).kind == 0);
    }
    async fn rows(pool: DbPool) -> Unit {
        let tx: Sender<DbRow> = channel<DbRow>(1);
        let rx: Receiver<DbRow> = channel_receiver(&tx);
        let stream: Task<DbError> = spawn_child(db_stream(db_pool_clone(&pool),bytes("SELECT id, message, amount FROM dlm_s2_items"),buffer<DbValue>(0),1500,2048,move tx));
        let row: DbRow = await receive_async(receiver_clone(&rx));
        assert(row.error.kind == 0 && !row.done);
        assert(len(&row.values) == 3 && row.values[0].integer == 42);
        assert(row.values[1].kind == 0 && row.values[1].ty == 3);
        {
            let value: Buffer<U8> = move row.values[2].bytes;
            let expected: Slice<U8> = utf8_bytes("9007199254740993.123456789012345");
            assert(len(&value) == len(expected));
            for i in 0..len(expected) { assert(value[i] == expected[i]); }
        }
        let end: DbRow = await receive_async(move rx);
        assert(end.done && end.error.kind == 0);
        let done: DbError = await move stream;
        assert(done.kind == 0);
    }
    fn setup(pool: &DbPool) -> Unit {
        let mut connection: DbConnection = db_acquire(pool,1000);
        assert(db_connection_error(&connection).kind == 0);
        {
            let blocked: DbConnection = db_acquire(pool,20);
            assert(db_connection_error(&blocked).kind == 6);
        }
        {
            let raw: DbStatement = db_prepare(&mut connection,utf8_bytes("/* control */ BEGIN"),0,1000);
            assert(db_statement_error(&raw).kind == 9);
        }
        let mut txn: DbTransaction = db_begin(&mut connection,1000);
        assert(db_transaction_error(&txn).kind == 0);
        assert(db_savepoint(&mut txn,utf8_bytes("stamp"),1000).kind == 0);
        insert(&mut txn);
        assert(db_rollback_to(&mut txn,utf8_bytes("stamp"),1000).kind == 0);
        assert(db_release_savepoint(&mut txn,utf8_bytes("stamp"),1000).kind == 0);
        insert(&mut txn);
        assert(db_commit(&mut txn,1000).kind == 0);
        db_transaction_close(move txn);
        db_release(move connection);
    }
    async fn cancelled_query() -> DbRows {
        await sleep(30);
        return DbRows(buffer<DbRow>(0),DbError(7,0,0));
    }
    async fn stream_guard() -> DbError {
        await sleep(3000);
        return DbError(7,0,0);
    }
    async fn main() -> Unit {
        let pool: DbPool = POOL;
        let first: DbMigrationResult = await db_migrate(db_pool_clone(&pool),migrations(false),2000);
        if first.error.kind != 0 { print(first.error.kind); print(first.error.vendor); print(first.error.state); }
        assert(first.error.kind == 0 && first.applied == 2 && first.version == 2);
        let repeat: DbMigrationResult = await db_migrate(db_pool_clone(&pool),migrations(false),2000);
        assert(repeat.error.kind == 0 && repeat.applied == 0 && repeat.version == 2);
        let mut changed: Buffer<DbMigration> = migrations(false);
        changed[1] = DbMigration(2,bytes("CREATE INDEX other_index ON dlm_s2_items (message)"));
        let mismatch: DbMigrationResult = await db_migrate(db_pool_clone(&pool),move changed,2000);
        assert(mismatch.error.kind == 11);
        let failed: DbMigrationResult = await db_migrate(db_pool_clone(&pool),migrations(true),2000);
        assert(failed.error.kind == 4 && failed.version == 4);
        let missing: DbRows = await db_query(db_pool_clone(&pool),bytes("SELECT id FROM dlm_s2_rolled_back"),buffer<DbValue>(0),1000,2,4096);
        assert(missing.error.kind == 4);
        let setup_done: Task<Unit> = spawn_blocking(setup_work,db_pool_clone(&pool));
        await move setup_done;
        await rows(db_pool_clone(&pool));
        let limit: DbRows = await db_query(db_pool_clone(&pool),bytes("SELECT id FROM dlm_s2_items"),buffer<DbValue>(0),1000,0,4096);
        assert(limit.error.kind == 5);
        let slow: DbRows = await db_query(db_pool_clone(&pool),bytes("SLOW_QUERY"),buffer<DbValue>(0),25,1,4096);
        assert(slow.error.kind == 6);
        let cancelled: DbRows = await select(db_query(db_pool_clone(&pool),bytes("SLOW_QUERY"),buffer<DbValue>(0),5000,1,4096),cancelled_query());
        assert(cancelled.error.kind == 7);
        let healthy: DbRows = await db_query(db_pool_clone(&pool),bytes("SELECT id FROM dlm_s2_items"),buffer<DbValue>(0),2000,2,4096);
        assert(healthy.error.kind == 0 && len(&healthy.rows) == 1);
        let stream: Sender<DbRow> = channel<DbRow>(1);
        let reader: Receiver<DbRow> = channel_receiver(&stream);
        let backpressure: DbError = await select(db_stream(move pool,bytes("SELECT 1 UNION ALL SELECT 2"),buffer<DbValue>(0),1000,512,move stream),stream_guard());
        assert(backpressure.kind == 6);
        let buffered: DbRow = await receive_async(move reader);
        assert(!buffered.done && buffered.values[0].integer == 1);
        print("SQL integration OK");
    }
    fn setup_work(pool: DbPool) -> Unit guarantees(ThreadSafe) { setup(&pool); }
}
"###;

fn sql(backend: &str, address: &str, prefixes: [&str; 3], decimal: &str, slow: &str) {
    let source = SQL
        .replace(
            "POOL",
            &format!("{backend}_pool(utf8_bytes({}),1,4096)", literal(address)),
        )
        .replace("P1", prefixes[0])
        .replace("P2", prefixes[1])
        .replace("P3", prefixes[2])
        .replace("SQL_DECIMAL_TYPE", decimal)
        .replace("SLOW_QUERY", slow);
    let directory = temporary();
    let mir = lower(&directory, &source, CheckPolicy::allow_unsafe());
    run(&compile(&mir, &directory), "SQL integration OK");
    fs::remove_dir_all(&directory).unwrap();
}

#[test]
fn sqlite_migrations_transactions_and_streaming() {
    if cfg!(windows) && std::env::var_os("DLM_LIBSQLITE").is_none() {
        return;
    }
    sql(
        "sqlite",
        ":memory:",
        ["?1", "?2", "?3"],
        "TEXT",
        "WITH RECURSIVE work(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM work WHERE x<100000000) SELECT sum(x) FROM work",
    );
}

#[test]
fn postgres_migrations_transactions_and_streaming() {
    let Ok(dsn) = std::env::var("DLM_TEST_POSTGRES_DSN") else {
        return;
    };
    sql(
        "postgres",
        &dsn,
        ["$1", "$2", "$3"],
        "NUMERIC",
        "SELECT pg_sleep(2)",
    );
}

const SQLITE_RECOVERY: &str = r###"
module test.s2_recovery
import std.sql
native {
    trust "dlm.test.exit";
    extern "C" fn _Exit(status: I32) -> Unit from "dlm.test.exit" guarantees(NoUnwind);
    fn main() -> Unit {
        let pool: DbPool = sqlite_pool(utf8_bytes(DATABASE_FILE),2,4096);
        let mut writer: DbConnection = db_acquire(&pool,1000);
        assert(db_connection_error(&writer).kind == 0);
        if CRASH_PROCESS {
            assert(db_execute(db_prepare(&mut writer,utf8_bytes("CREATE TABLE recovery (id BIGINT PRIMARY KEY)"),0,1000)).kind == 0);
            assert(db_execute(db_prepare(&mut writer,utf8_bytes("INSERT INTO recovery VALUES (1)"),0,1000)).kind == 0);
            let mut tx: DbTransaction = db_begin(&mut writer,1000);
            assert(db_transaction_error(&tx).kind == 0);
            assert(db_execute(db_prepare_tx(&mut tx,utf8_bytes("INSERT INTO recovery VALUES (2)"),0,1000)).kind == 0);
            let mut concurrent: DbConnection = db_acquire(&pool,1000);
            let locked: DbTransaction = db_begin(&mut concurrent,25);
            assert(db_transaction_error(&locked).kind == 6);
            unsafe { _Exit(73); }
        }
        {
            let mut check: DbStatement = db_prepare(&mut writer,utf8_bytes("SELECT id FROM recovery ORDER BY id"),0,1000);
            let committed: DbRow = db_fetch(&mut check,512);
            assert(committed.error.kind == 0 && !committed.done && committed.values[0].integer == 1);
            let end: DbRow = db_fetch(&mut check,512);
            assert(end.error.kind == 0 && end.done);
        }
        let mut tx: DbTransaction = db_begin(&mut writer,1000);
        assert(db_execute(db_prepare_tx(&mut tx,utf8_bytes("INSERT INTO recovery VALUES (3)"),0,1000)).kind == 0);
        assert(db_commit(&mut tx,1000).kind == 0);
        print("SQLite recovery OK");
    }
}
"###;

#[test]
fn sqlite_writer_lock_and_abrupt_exit_recover_without_losing_committed_data() {
    if cfg!(windows) && std::env::var_os("DLM_LIBSQLITE").is_none() {
        return;
    }
    let directory = temporary();
    let source = SQLITE_RECOVERY.replace(
        "DATABASE_FILE",
        &literal(directory.join("recovery.sqlite").to_str().unwrap()),
    );
    let crash = lower(
        &directory,
        &source.replace("CRASH_PROCESS", "true"),
        CheckPolicy::allow_unsafe(),
    );
    let output = Command::new(compile(&crash, &directory)).output().unwrap();
    assert_eq!(
        output.status.code(),
        Some(73),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let recover = lower(
        &directory,
        &source.replace("CRASH_PROCESS", "false"),
        CheckPolicy::allow_unsafe(),
    );
    run(&compile(&recover, &directory), "SQLite recovery OK");
    fs::remove_dir_all(&directory).unwrap();
}

const HTTP: &str = r###"
module test.s2_http
import std.http_tasks
import std.sql
native {
    fn write_row(pool: DbPool, id: I64) -> DbError guarantees(ThreadSafe) {
        let mut lease: DbConnection = db_acquire(&pool,1000);
        let mut tx: DbTransaction = db_begin(&mut lease,1000);
        let mut statement: DbStatement = db_prepare_tx(&mut tx,utf8_bytes("INSERT INTO items VALUES (PARAMETER)"),1,1000);
        let key: DbValue = db_int(id);
        let bound: DbError = db_bind(&mut statement,1,&key);
        if bound.kind != 0 { return bound; }
        let executed: DbError = db_execute(move statement);
        if executed.kind != 0 { return executed; }
        return db_commit(&mut tx,1000);
    }
    async fn store_request(input: Receiver<HttpBody>, output: Sender<HttpEvent>, pool: DbPool) -> Bool {
        let doc: JsonDocument = await http_read_json(move input,JsonLimits(8192,32,8));
        let mut spec: Buffer<JsonFieldSpec> = buffer<JsonFieldSpec>(0);
        push(&mut spec,json_field(utf8_bytes("id"),7,false,false));
        let checked: JsonRecord = json_decode_record(&doc,doc.root,slice(&spec,0,1),true);
        if checked.error.code != 0 { return await http_send_response(move output,http_response(400,utf8_bytes(""),buffer<U8>(0),true),false); }
        if USE_DB {
            let task: Task<DbError> = spawn_blocking(write_row,move pool,checked.fields[0].signed);
            let committed: DbError = await move task;
            if committed.kind != 0 { return await http_send_response(move output,http_response(409,utf8_bytes(""),buffer<U8>(0),false),false); }
        }
        return await http_send_response(move output,http_json_response(201,&doc,8192),false);
    }
    async fn stream_request(output: Sender<HttpEvent>) -> Bool {
        let started: Bool = await http_stream_start(sender_clone(&output),200,utf8_empty(),false);
        if !started { return false; }
        let mut bytes: Buffer<U8> = buffer<U8>(24576);
        for i in 0..len(&bytes) { bytes[i]=U8(65)+cast<U8>(i%USize(26)); }
        let sent: Bool = await http_stream_write(sender_clone(&output),move bytes);
        if !sent { return false; }
        return await http_stream_end(move output);
    }
    fn utf8_empty() -> Buffer<U8> { return buffer<U8>(0); }
    async fn socket_request(input: Receiver<HttpBody>, output: Sender<HttpEvent>) -> Bool {
        let upgraded: Bool = await http_upgrade(sender_clone(&output));
        if !upgraded { return false; }
        while true {
            let message: HttpSocketMessage = await http_socket_receive(receiver_clone(&input));
            if message.status != 0 { return message.status == 2; }
            let mut opcode: U32 = 0;
            if message.first { opcode=message.opcode; }
            let wrote: Bool = await http_socket_send(sender_clone(&output),opcode,message.final,move message.bytes);
            if !wrote { return false; }
        }
    }
    async fn route(request: HttpRequest, input: Receiver<HttpBody>, output: Sender<HttpEvent>, pool: DbPool, stop: Sender<Bool>) -> Bool {
        let mut routes: Buffer<HttpRoute> = buffer<HttpRoute>(0);
        push(&mut routes,HttpRoute(http_copy(utf8_bytes("POST")),http_copy(utf8_bytes("/data")),1));
        push(&mut routes,HttpRoute(http_copy(utf8_bytes("GET")),http_copy(utf8_bytes("/stream")),2));
        push(&mut routes,HttpRoute(http_copy(utf8_bytes("GET")),http_copy(utf8_bytes("/ws")),3));
        push(&mut routes,HttpRoute(http_copy(utf8_bytes("GET")),http_copy(utf8_bytes("/stop")),4));
        let id: U32 = http_route(slice(&routes,0,len(&routes)),&request);
        if id == 1 { return await store_request(move input,move output,move pool); }
        if id == 2 && !http_is_head(&request) { return await stream_request(move output); }
        if id == 2 { return await http_send_response(move output,http_response(200,utf8_bytes(""),buffer<U8>(24576),false),true); }
        if id == 3 && request.ticket.websocket { return await socket_request(move input,move output); }
        if id == 4 {
            let sent: Bool = try_send(&stop,true);
            return await http_send_response(move output,http_response(204,utf8_bytes(""),buffer<U8>(0),true),false);
        }
        return await http_send_response(move output,http_response(404,utf8_bytes(""),buffer<U8>(0),false),http_is_head(&request));
    }
    async fn work(inbox: Receiver<HttpExchange>, pool: DbPool, stop: Sender<Bool>) -> Unit {
        while true {
            let exchange: HttpExchange = await receive_async(receiver_clone(&inbox));
            let result: Bool = await select(
                route(move exchange.request,move exchange.input,move exchange.output,db_pool_clone(&pool),sender_clone(&stop)),
                receive_async(move exchange.completion));
        }
    }
    fn main() -> Unit {
        let pool: DbPool = OPEN_POOL;
        if USE_DB {
            let mut db: DbConnection = db_acquire(&pool,1000);
            assert(db_execute(db_prepare(&mut db,utf8_bytes("CREATE TABLE items(id BIGINT PRIMARY KEY)"),0,1000)).kind == 0);
        }
        let address: SocketAddress = socket_address_v4(127,0,0,1,PORT);
        let limits: HttpLimits = HttpLimits(2048,32768,256,1024,8,24,1500,1500);
        let mut server: HttpServer = OPEN_SERVER;
        assert(http_server_error(&server).kind == 0);
        let executor: Executor = executor(1,12);
        let requests: Sender<HttpExchange> = channel<HttpExchange>(4);
        let inbox: Receiver<HttpExchange> = channel_receiver(&requests);
        let stop: Sender<Bool> = channel<Bool>(1);
        let shutdown: Receiver<Bool> = channel_receiver(&stop);
        let cancel: Resource<OpaqueHandle,net_cancel_dispose> = network_cancel();
        let worker1: Task<Unit> = spawn(&executor,work(receiver_clone(&inbox),db_pool_clone(&pool),sender_clone(&stop)));
        let worker2: Task<Unit> = spawn(&executor,work(move inbox,db_pool_clone(&pool),move stop));
        let mut dispatcher: HttpDispatch = http_dispatch(move requests,8,4096);
        let mut stopped: Bool = false;
        for i in 0..10000 {
            let tick: NetIo = http_dispatch_poll(&mut server,&mut dispatcher,&cancel,5);
            assert(tick.status == 0);
            if try_receive(&shutdown,&mut stopped) { break; }
        }
        assert(stopped);
        let drained: NetIo = http_dispatch_shutdown(&mut server,&mut dispatcher,1000);
        assert(drained.status == 0 && drained.count == 0);
        print("HTTP integration OK");
    }
}
"###;

struct Server(Child);

impl Drop for Server {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}

impl Server {
    fn connect(&mut self, address: SocketAddr) -> BufReader<TcpStream> {
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            assert!(
                self.0.try_wait().unwrap().is_none(),
                "server exited before accepting connections"
            );
            match TcpStream::connect_timeout(&address, Duration::from_millis(200)) {
                Ok(stream) => {
                    stream
                        .set_read_timeout(Some(Duration::from_secs(5)))
                        .unwrap();
                    stream
                        .set_write_timeout(Some(Duration::from_secs(5)))
                        .unwrap();
                    return BufReader::new(stream);
                }
                Err(e) => assert!(Instant::now() < deadline, "{e}"),
            }
            thread::sleep(Duration::from_millis(20));
        }
    }

    fn finish(&mut self) {
        let deadline = Instant::now() + Duration::from_secs(5);
        let status = loop {
            if let Some(status) = self.0.try_wait().unwrap() {
                break status;
            }
            assert!(
                Instant::now() < deadline,
                "server shutdown exceeded its deadline"
            );
            thread::sleep(Duration::from_millis(10));
        };
        assert!(status.success());
        let mut output = String::new();
        self.0
            .stdout
            .take()
            .unwrap()
            .read_to_string(&mut output)
            .unwrap();
        assert_eq!(output.trim(), "HTTP integration OK");
    }
}

fn server(directory: &Path, tls: bool, postgres: Option<&str>) -> (Server, SocketAddr) {
    let reservation = TcpListener::bind(("127.0.0.1", 0)).unwrap();
    let address = reservation.local_addr().unwrap();
    let has_db =
        postgres.is_some() || !cfg!(windows) || std::env::var_os("DLM_LIBSQLITE").is_some();
    let pool = match postgres {
        Some(dsn) => format!("postgres_pool(utf8_bytes({}),1,4096)", literal(dsn)),
        None => "sqlite_pool(utf8_bytes(\":memory:\"),1,4096)".into(),
    };
    let open = if tls {
        format!(
            "http_server_tls(address,limits,utf8_bytes({}),utf8_bytes({}))",
            literal(directory.join("cert.pem").to_str().unwrap()),
            literal(directory.join("key.pem").to_str().unwrap())
        )
    } else {
        "http_server_open(address,limits)".into()
    };
    let source = HTTP
        .replace("OPEN_SERVER", &open)
        .replace("OPEN_POOL", &pool)
        .replace("PARAMETER", if postgres.is_some() { "$1" } else { "?1" })
        .replace("USE_DB", if has_db { "true" } else { "false" })
        .replace("PORT", &address.port().to_string());
    let mir = lower(directory, &source, CheckPolicy::allow_unsafe());
    let executable = compile(&mir, directory);
    drop(reservation);
    let child = Command::new(executable)
        .stdout(Stdio::piped())
        .stderr(Stdio::inherit())
        .spawn()
        .unwrap();
    (Server(child), address)
}

fn write_request(socket: &mut BufReader<TcpStream>, bytes: &[u8]) {
    socket.get_mut().write_all(bytes).unwrap();
}

fn headers(socket: &mut impl BufRead, code: u16) -> String {
    let mut first = String::new();
    socket.read_line(&mut first).unwrap();
    assert!(first.starts_with(&format!("HTTP/1.1 {code} ")), "{first:?}");
    let mut fields = String::new();
    loop {
        let mut line = String::new();
        assert!(socket.read_line(&mut line).unwrap() != 0);
        if line == "\r\n" {
            return fields;
        }
        fields.push_str(&line);
    }
}

fn body(socket: &mut impl BufRead, size: usize) -> Vec<u8> {
    let mut bytes = vec![0; size];
    socket.read_exact(&mut bytes).unwrap();
    bytes
}

fn websocket_frame(socket: &mut BufReader<TcpStream>, opcode: u8, payload: &[u8]) {
    assert!(payload.len() <= 125);
    let mask = [1, 3, 5, 7];
    let mut frame = vec![opcode, 128 | payload.len() as u8];
    frame.extend(mask);
    frame.extend(
        payload
            .iter()
            .enumerate()
            .map(|(i, byte)| byte ^ mask[i % 4]),
    );
    write_request(socket, &frame);
}

fn pending_body(server: &mut Server, address: SocketAddr) -> BufReader<TcpStream> {
    let mut socket = server.connect(address);
    write_request(&mut socket,b"POST /data HTTP/1.1\r\nHost: localhost\r\nContent-Length: 9\r\nExpect: 100-continue\r\n\r\n");
    socket
}

fn saturated_queue_releases_when_client_disconnects(server: &mut Server, address: SocketAddr) {
    let mut active = Vec::new();
    for _ in 0..6 {
        let mut socket = pending_body(server, address);
        headers(&mut socket, 100);
        active.push(socket);
    }
    let mut waiting = pending_body(server, address);
    waiting
        .get_ref()
        .set_read_timeout(Some(Duration::from_millis(100)))
        .unwrap();
    let error = waiting.read(&mut [0; 1]).unwrap_err();
    assert!(matches!(
        error.kind(),
        ErrorKind::WouldBlock | ErrorKind::TimedOut
    ));
    active.remove(0).get_ref().shutdown(Shutdown::Both).unwrap();
    waiting
        .get_ref()
        .set_read_timeout(Some(Duration::from_secs(5)))
        .unwrap();
    headers(&mut waiting, 100);
}

#[test]
fn http_async_database_keep_alive_streaming_websocket_and_graceful_shutdown() {
    let directory = temporary();
    let (mut server, address) = server(&directory, false, None);
    let mut slow = pending_body(&mut server, address);
    headers(&mut slow, 100);

    let mut stream = server.connect(address);
    write_request(
        &mut stream,
        b"GET /stream HTTP/1.1\r\nHost: localhost\r\n\r\n",
    );
    assert!(headers(&mut stream, 200).contains("Transfer-Encoding: chunked\r\n"));
    let mut offset = 0;
    loop {
        let mut length = String::new();
        stream.read_line(&mut length).unwrap();
        let count = usize::from_str_radix(length.trim(), 16).unwrap();
        for (i, byte) in body(&mut stream, count).into_iter().enumerate() {
            assert_eq!(byte, b'A' + ((offset + i) % 26) as u8);
        }
        assert_eq!(body(&mut stream, 2), b"\r\n");
        if count == 0 {
            break;
        }
        offset += count;
    }
    assert_eq!(offset, 24576);

    write_request(
        &mut slow,
        b"{\"id\":42}HEAD /stream HTTP/1.1\r\nHost: localhost\r\n\r\n",
    );
    assert!(headers(&mut slow, 201).contains("Content-Type: application/json"));
    assert_eq!(body(&mut slow, 9), b"{\"id\":42}");
    assert!(headers(&mut slow, 200).contains("Content-Length: 24576\r\n"));

    write_request(&mut slow,b"POST /data?with=query HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n5\r\n{\"id\"\r\n4\r\n:43}\r\n0\r\nX-Trace: done\r\n\r\n");
    headers(&mut slow, 201);
    assert_eq!(body(&mut slow, 9), b"{\"id\":43}");

    if !cfg!(windows) || std::env::var_os("DLM_LIBSQLITE").is_some() {
        write_request(
            &mut slow,
            b"POST /data HTTP/1.1\r\nHost: localhost\r\nContent-Length: 9\r\n\r\n{\"id\":42}",
        );
        headers(&mut slow, 409);
    }

    let mut invalid = server.connect(address);
    write_request(&mut invalid,b"POST /data HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nxZ\r\n0\r\n\r\n");
    headers(&mut invalid, 400);

    let mut ws = server.connect(address);
    write_request(&mut ws,b"GET /ws HTTP/1.1\r\nHost: localhost\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n");
    assert!(headers(&mut ws, 101).contains("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
    websocket_frame(&mut ws, 0x01, b"ab");
    assert_eq!(body(&mut ws, 4), b"\x01\x02ab");
    websocket_frame(&mut ws, 0x89, b"q");
    assert_eq!(body(&mut ws, 3), b"\x8a\x01q");
    websocket_frame(&mut ws, 0x80, b"cd");
    assert_eq!(body(&mut ws, 4), b"\x80\x02cd");
    websocket_frame(&mut ws, 0x88, b"\x03\xe8");
    assert_eq!(body(&mut ws, 4), b"\x88\x02\x03\xe8");

    drop((slow, stream, invalid, ws));
    saturated_queue_releases_when_client_disconnects(&mut server, address);
    let mut stop = server.connect(address);
    write_request(&mut stop, b"GET /stop HTTP/1.1\r\nHost: localhost\r\n\r\n");
    headers(&mut stop, 204);
    server.finish();
    fs::remove_dir_all(&directory).unwrap();
}

#[test]
fn http_json_reaches_postgres_commit_via_blocking_executor() {
    let Ok(dsn) = std::env::var("DLM_TEST_POSTGRES_DSN") else {
        return;
    };
    let directory = temporary();
    let (mut server, address) = server(&directory, false, Some(&dsn));
    let mut client = server.connect(address);
    let request = b"POST /data HTTP/1.1\r\nHost: localhost\r\nContent-Length: 9\r\n\r\n{\"id\":51}";
    write_request(&mut client, request);
    headers(&mut client, 201);
    assert_eq!(body(&mut client, 9), b"{\"id\":51}");
    write_request(&mut client, request);
    headers(&mut client, 409);
    write_request(
        &mut client,
        b"GET /stop HTTP/1.1\r\nHost: localhost\r\n\r\n",
    );
    headers(&mut client, 204);
    server.finish();
    fs::remove_dir_all(&directory).unwrap();
}

#[test]
fn https_uses_real_tls_and_drains_on_shutdown() {
    if std::env::var_os("DLM_TEST_HTTP_TLS").is_none() {
        return;
    }
    let directory = temporary();
    let cert = directory.join("cert.pem");
    let key = directory.join("key.pem");
    let generated = Command::new("openssl")
        .args([
            "req",
            "-x509",
            "-newkey",
            "rsa:2048",
            "-nodes",
            "-days",
            "1",
            "-subj",
            "/CN=localhost",
            "-addext",
            "subjectAltName=DNS:localhost",
            "-keyout",
        ])
        .arg(&key)
        .arg("-out")
        .arg(&cert)
        .output()
        .unwrap();
    assert!(
        generated.status.success(),
        "{}",
        String::from_utf8_lossy(&generated.stderr)
    );
    let (mut server, address) = server(&directory, true, None);
    drop(server.connect(address));
    let mut client = Server(
        Command::new("openssl")
            .args([
                "s_client",
                "-quiet",
                "-tls1_2",
                "-verify_return_error",
                "-verify_hostname",
                "localhost",
                "-servername",
                "localhost",
                "-CAfile",
            ])
            .arg(&cert)
            .arg("-connect")
            .arg(address.to_string())
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .unwrap(),
    );
    client.0.stdin.take().unwrap().write_all(b"POST /data HTTP/1.1\r\nHost: localhost\r\nContent-Length: 9\r\n\r\n{\"id\":66}GET /stop HTTP/1.1\r\nHost: localhost\r\n\r\n").unwrap();
    server.finish();
    let deadline = Instant::now() + Duration::from_secs(5);
    let result = loop {
        if let Some(status) = client.0.try_wait().unwrap() {
            break status;
        }
        assert!(
            Instant::now() < deadline,
            "TLS close_notify was not received"
        );
        thread::sleep(Duration::from_millis(10));
    };
    let mut error = String::new();
    client
        .0
        .stderr
        .take()
        .unwrap()
        .read_to_string(&mut error)
        .unwrap();
    assert!(result.success(), "{error}");
    let mut wire = BufReader::new(client.0.stdout.take().unwrap());
    headers(&mut wire, 201);
    assert_eq!(body(&mut wire, 9), b"{\"id\":66}");
    headers(&mut wire, 204);
    fs::remove_dir_all(&directory).unwrap();
}
