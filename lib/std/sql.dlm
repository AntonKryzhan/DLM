module std.sql
import std.json
import std.native_net

native {
    trust "dlm.db.v1";
    record DbError repr(C, version=1) { kind: U32; vendor: I32; state: U64; }
    record DbNativeOpen repr(C, version=1) { handle: OpaqueHandle; error: DbError; }
    record DbNativeStep repr(C, version=1) { columns: USize; row: Bool; done: Bool; error: DbError; }
    record DbNativeCell repr(C, version=1) { kind: U32; ty: U32; integer: I64; real: F64; length: USize; error: DbError; }
    record DbNativeParam repr(C, version=1) { kind: U32; ty: U32; integer: I64; real: F64; bytes: Ptr<U8>; length: USize; }
    record DbConnection repr(C) { inner: Resource<DbNativeOpen,db_dispose_connection>; }
    record DbStatement repr(C) { inner: Resource<DbNativeOpen,db_dispose_statement>; }
    record DbTransaction repr(C) { inner: Resource<DbNativeOpen,db_dispose_transaction>; }
    record DbValue repr(C) { kind: U32; ty: U32; integer: I64; real: F64; bytes: Buffer<U8>; }
    record DbRow repr(C) { values: Buffer<DbValue>; bytes: USize; done: Bool; error: DbError; }
    record DbRows repr(C) { rows: Buffer<DbRow>; error: DbError; }
    record DbMigration repr(C) { version: I64; sql: Buffer<U8>; }
    record DbMigrationResult repr(C) { applied: USize; version: I64; error: DbError; }

    extern "C" fn dlm_db_acquire(pool: OpaqueHandle, timeout: U32) -> DbNativeOpen from "dlm.db.v1" guarantees(ThreadSafe,NoUnwind);
    extern "C" fn dlm_db_release(connection: OpaqueHandle) -> Unit from "dlm.db.v1" guarantees(ThreadSafe,NoUnwind);
    extern "C" fn dlm_db_driver(connection: OpaqueHandle) -> U32 from "dlm.db.v1" guarantees(ThreadSafe,NoUnwind);
    extern "C" fn dlm_db_prepare(connection: OpaqueHandle, transaction: OpaqueHandle, sql: Ptr<U8>, length: USize, count: USize, timeout: U32) -> DbNativeOpen from "dlm.db.v1" guarantees(ThreadSafe,NoUnwind);
    extern "C" fn dlm_db_bind(statement: OpaqueHandle, index: USize, value: DbNativeParam) -> DbError from "dlm.db.v1" guarantees(ThreadSafe,NoUnwind);
    extern "C" fn dlm_db_step(statement: OpaqueHandle) -> DbNativeStep from "dlm.db.v1" guarantees(ThreadSafe,NoUnwind);
    extern "C" fn dlm_db_remaining(statement: OpaqueHandle) -> U32 from "dlm.db.v1" guarantees(ThreadSafe,NoUnwind);
    extern "C" fn dlm_db_cell(statement: OpaqueHandle, column: USize, out: Ptr<U8>, cap: USize) -> DbNativeCell from "dlm.db.v1" guarantees(ThreadSafe,NoUnwind);
    extern "C" fn dlm_db_stmt_close(statement: OpaqueHandle) -> Unit from "dlm.db.v1" guarantees(ThreadSafe,NoUnwind);
    extern "C" fn dlm_db_begin(connection: OpaqueHandle, timeout: U32) -> DbNativeOpen from "dlm.db.v1" guarantees(ThreadSafe,NoUnwind);
    extern "C" fn dlm_db_transaction(transaction: OpaqueHandle, action: U32, name: Ptr<U8>, length: USize, timeout: U32) -> DbError from "dlm.db.v1" guarantees(ThreadSafe,NoUnwind);
    extern "C" fn dlm_db_tx_close(transaction: OpaqueHandle) -> Unit from "dlm.db.v1" guarantees(ThreadSafe,NoUnwind);

    fn db_dispose_connection(value: &mut DbNativeOpen) -> Unit guarantees(ThreadSafe,NoUnwind) { unsafe { dlm_db_release(value.handle); } }
    fn db_dispose_statement(value: &mut DbNativeOpen) -> Unit guarantees(ThreadSafe,NoUnwind) { unsafe { dlm_db_stmt_close(value.handle); } }
    fn db_dispose_transaction(value: &mut DbNativeOpen) -> Unit guarantees(ThreadSafe,NoUnwind) { unsafe { dlm_db_tx_close(value.handle); } }

    fn sqlite_pool(path: Slice<U8>, connections: USize, row_bytes: USize) -> DbPool {
        unsafe { return db_native_pool(1,path,connections,row_bytes); }
    }
    fn postgres_pool(address: Slice<U8>, connections: USize, row_bytes: USize) -> DbPool {
        unsafe { return db_native_pool(2,address,connections,row_bytes); }
    }
    fn db_pool_clone(pool: &DbPool) -> DbPool { return db_native_pool_clone(pool); }
    fn db_acquire(pool: &DbPool, timeout_ms: U32) -> DbConnection guarantees(ThreadSafe) {
        unsafe { return DbConnection(resource(dlm_db_acquire(db_native_pool_handle(pool),timeout_ms))); }
    }
    fn db_connection_error(connection: &DbConnection) -> DbError { return resource_get(&connection.inner).error; }
    fn db_statement_error(statement: &DbStatement) -> DbError { return resource_get(&statement.inner).error; }
    fn db_transaction_error(transaction: &DbTransaction) -> DbError { return resource_get(&transaction.inner).error; }
    fn db_release(connection: DbConnection) -> Unit guarantees(ThreadSafe) { resource_close(move connection.inner); }
    fn db_statement_close(statement: DbStatement) -> Unit guarantees(ThreadSafe) { resource_close(move statement.inner); }
    fn db_transaction_close(transaction: DbTransaction) -> Unit guarantees(ThreadSafe) { resource_close(move transaction.inner); }
    fn db_prepare(connection: &mut DbConnection, sql: Slice<U8>, params: USize, timeout_ms: U32) -> DbStatement guarantees(ThreadSafe) {
        let owner: &DbNativeOpen = resource_get(&connection.inner);
        if owner.error.kind != 0 { return DbStatement(resource(DbNativeOpen(zero<OpaqueHandle>(),owner.error))); }
        unsafe { return DbStatement(resource(dlm_db_prepare(owner.handle,zero<OpaqueHandle>(),net_input(sql),len(sql),params,timeout_ms))); }
    }
    fn db_prepare_tx(transaction: &mut DbTransaction, sql: Slice<U8>, params: USize, timeout_ms: U32) -> DbStatement guarantees(ThreadSafe) {
        let owner: &DbNativeOpen = resource_get(&transaction.inner);
        if owner.error.kind != 0 { return DbStatement(resource(DbNativeOpen(zero<OpaqueHandle>(),owner.error))); }
        unsafe { return DbStatement(resource(dlm_db_prepare(zero<OpaqueHandle>(),owner.handle,net_input(sql),len(sql),params,timeout_ms))); }
    }
    fn db_null(ty: U32) -> DbValue { return DbValue(0,ty,0,F64(0),buffer<U8>(0)); }
    fn db_int(value: I64) -> DbValue { return DbValue(1,1,value,F64(0),buffer<U8>(0)); }
    fn db_float(value: F64) -> DbValue { return DbValue(2,2,0,value,buffer<U8>(0)); }
    fn db_bytes(value: Buffer<U8>) -> DbValue { return DbValue(4,4,0,F64(0),move value); }
    fn db_text(value: Buffer<U8>) -> DbValue { return DbValue(3,3,0,F64(0),move value); }
    fn db_decimal(value: Buffer<U8>) -> DbValue { return DbValue(5,5,0,F64(0),move value); }
    fn db_bool(value: Bool) -> DbValue {
        let mut i: I64 = 0; if value { i = 1; }
        return DbValue(6,6,i,F64(0),buffer<U8>(0));
    }
    fn db_bind(statement: &mut DbStatement, index: USize, value: &DbValue) -> DbError guarantees(ThreadSafe) {
        let owner: &DbNativeOpen = resource_get(&statement.inner);
        if owner.error.kind != 0 { return owner.error; }
        let input: Slice<U8> = slice(&value.bytes,0,len(&value.bytes));
        if value.kind == 5 && !json_number_valid(input) { return DbError(1,0,0); }
        if value.kind == 3 {
            let mut cursor: USize = 0;
            while cursor < len(input) {
                let step: Utf8Step = utf8_next(input,cursor);
                if !step.valid || step.codepoint == 0 { return DbError(1,0,0); }
                cursor = step.next;
            }
        }
        unsafe { return dlm_db_bind(owner.handle,index,DbNativeParam(value.kind,value.ty,value.integer,value.real,net_input(input),len(input))); }
    }
    fn db_fetch(statement: &mut DbStatement, maximum: USize) -> DbRow guarantees(ThreadSafe) {
        let owner: &DbNativeOpen = resource_get(&statement.inner);
        if owner.error.kind != 0 { return DbRow(buffer<DbValue>(0),0,true,owner.error); }
        unsafe {
            let step: DbNativeStep = dlm_db_step(owner.handle);
            let mut result: DbRow = DbRow(buffer<DbValue>(0),0,step.done || step.error.kind != U32(0),step.error);
            if !step.row { return move result; }
            for i in 0..step.columns {
                let cell: DbNativeCell = dlm_db_cell(owner.handle,i,zero<Ptr<U8>>(),0);
                if cell.error.kind != 0 { return DbRow(buffer<DbValue>(0),0,true,cell.error); }
                if result.bytes > maximum || cell.length > maximum-result.bytes || maximum-result.bytes-cell.length < size_of<DbValue>() {
                    return DbRow(buffer<DbValue>(0),0,true,DbError(5,0,0));
                }
                let mut bytes: Buffer<U8> = buffer<U8>(cell.length);
                if cell.length != 0 {
                    let copied: DbNativeCell = dlm_db_cell(owner.handle,i,net_output(&mut bytes),cell.length);
                    if copied.error.kind != 0 { return DbRow(buffer<DbValue>(0),0,true,copied.error); }
                }
                result.bytes = result.bytes+cell.length+size_of<DbValue>();
                push(&mut result.values,DbValue(cell.kind,cell.ty,cell.integer,cell.real,move bytes));
            }
            return move result;
        }
    }
    fn db_begin(connection: &mut DbConnection, timeout_ms: U32) -> DbTransaction guarantees(ThreadSafe) {
        let owner: &DbNativeOpen = resource_get(&connection.inner);
        if owner.error.kind != 0 { return DbTransaction(resource(DbNativeOpen(zero<OpaqueHandle>(),owner.error))); }
        unsafe { return DbTransaction(resource(dlm_db_begin(owner.handle,timeout_ms))); }
    }
    fn db_tx_action(transaction: &mut DbTransaction, action: U32, name: Slice<U8>, timeout_ms: U32) -> DbError guarantees(ThreadSafe) {
        let owner: &DbNativeOpen = resource_get(&transaction.inner);
        if owner.error.kind != 0 { return owner.error; }
        unsafe { return dlm_db_transaction(owner.handle,action,net_input(name),len(name),timeout_ms); }
    }
    fn db_commit(transaction: &mut DbTransaction, timeout_ms: U32) -> DbError guarantees(ThreadSafe) {
        return db_tx_action(transaction,1,utf8_bytes(""),timeout_ms);
    }
    fn db_rollback(transaction: &mut DbTransaction, timeout_ms: U32) -> DbError guarantees(ThreadSafe) {
        return db_tx_action(transaction,2,utf8_bytes(""),timeout_ms);
    }
    fn db_savepoint(transaction: &mut DbTransaction, name: Slice<U8>, timeout_ms: U32) -> DbError guarantees(ThreadSafe) {
        return db_tx_action(transaction,3,name,timeout_ms);
    }
    fn db_rollback_to(transaction: &mut DbTransaction, name: Slice<U8>, timeout_ms: U32) -> DbError guarantees(ThreadSafe) {
        return db_tx_action(transaction,4,name,timeout_ms);
    }
    fn db_release_savepoint(transaction: &mut DbTransaction, name: Slice<U8>, timeout_ms: U32) -> DbError guarantees(ThreadSafe) {
        return db_tx_action(transaction,5,name,timeout_ms);
    }
    fn db_execute(statement: DbStatement) -> DbError guarantees(ThreadSafe) {
        let owner: &DbNativeOpen = resource_get(&statement.inner);
        if owner.error.kind != 0 { return owner.error; }
        unsafe {
            while true {
                let step: DbNativeStep = dlm_db_step(owner.handle);
                if step.done || step.error.kind != U32(0) { return step.error; }
            }
        }
    }
    fn db_migration_checksum(sql: &Buffer<U8>) -> I64 {
        let mut hash: U64 = U64(14695981039346656037);
        for i in 0..len(sql) { hash = wrapping_mul(hash ^ cast<U64>(sql[i]),U64(1099511628211)); }
        return cast<I64>(hash & U64(9223372036854775807));
    }
    fn db_migration_history(transaction: &mut DbTransaction, migrations: Slice<DbMigration>, timeout_ms: U32) -> DbMigrationResult guarantees(ThreadSafe) {
        let mut history: DbStatement = db_prepare_tx(transaction,utf8_bytes("SELECT version, checksum FROM dlm_schema_version ORDER BY version"),0,timeout_ms);
        let mut migrated: USize = 0;
        let mut version: I64 = 0;
        while true {
            let entry: DbRow = db_fetch(&mut history,256);
            if entry.error.kind != 0 { return DbMigrationResult(0,version,entry.error); }
            if entry.done { return DbMigrationResult(migrated,version,zero<DbError>()); }
            if len(&entry.values) != 2 || entry.values[0].kind != 1 || entry.values[1].kind != 1 || migrated == len(migrations)
                || entry.values[0].integer != migrations[migrated].version || entry.values[1].integer != db_migration_checksum(&migrations[migrated].sql) {
                return DbMigrationResult(0,version,DbError(11,0,0));
            }
            version = entry.values[0].integer;
            migrated = migrated+1;
        }
    }
    fn db_migrate_worker(pool: DbPool, migrations: Buffer<DbMigration>, timeout_ms: U32) -> DbMigrationResult guarantees(ThreadSafe) {
        let mut previous: I64 = 0;
        if len(&migrations) > 4096 { return DbMigrationResult(0,0,DbError(5,0,0)); }
        for i in 0..len(&migrations) {
            if migrations[i].version <= previous || len(&migrations[i].sql) == 0 { return DbMigrationResult(0,migrations[i].version,DbError(1,0,0)); }
            previous = migrations[i].version;
        }
        let mut connection: DbConnection = db_acquire(&pool,timeout_ms);
        let mut driver: U32 = 0;
        unsafe { driver = dlm_db_driver(resource_get(&connection.inner).handle); }
        let mut transaction: DbTransaction = db_begin(&mut connection,timeout_ms);
        let opened: DbError = db_transaction_error(&transaction);
        if opened.kind != 0 { return DbMigrationResult(0,0,opened); }
        if driver == 2 {
            let lock: DbError = db_execute(db_prepare_tx(&mut transaction,utf8_bytes("SELECT pg_advisory_xact_lock(6583077873879466516)"),0,timeout_ms));
            if lock.kind != 0 { return DbMigrationResult(0,0,lock); }
        }
        let created: DbError = db_execute(db_prepare_tx(&mut transaction,utf8_bytes("CREATE TABLE IF NOT EXISTS dlm_schema_version (version BIGINT PRIMARY KEY NOT NULL, checksum BIGINT NOT NULL)"),0,timeout_ms));
        if created.kind != 0 { return DbMigrationResult(0,0,created); }
        let history: DbMigrationResult = db_migration_history(&mut transaction,slice(&migrations,0,len(&migrations)),timeout_ms);
        if history.error.kind != 0 { return history; }
        let mut insert: Utf8 = "INSERT INTO dlm_schema_version (version, checksum) VALUES (?1, ?2)";
        if driver == 2 { insert = "INSERT INTO dlm_schema_version (version, checksum) VALUES ($1, $2)"; }
        for i in history.applied..len(&migrations) {
            let migration: &DbMigration = &migrations[i];
            let changed: DbError = db_execute(db_prepare_tx(&mut transaction,slice(&migration.sql,0,len(&migration.sql)),0,timeout_ms));
            if changed.kind != 0 { return DbMigrationResult(0,migration.version,changed); }
            let mut stamp: DbStatement = db_prepare_tx(&mut transaction,utf8_bytes(insert),2,timeout_ms);
            let value: DbValue = db_int(migration.version);
            let bound: DbError = db_bind(&mut stamp,1,&value);
            if bound.kind != 0 { return DbMigrationResult(0,migration.version,bound); }
            let hash: DbValue = db_int(db_migration_checksum(&migration.sql));
            let hashed: DbError = db_bind(&mut stamp,2,&hash);
            if hashed.kind != 0 { return DbMigrationResult(0,migration.version,hashed); }
            let recorded: DbError = db_execute(move stamp);
            if recorded.kind != 0 { return DbMigrationResult(0,migration.version,recorded); }
        }
        let committed: DbError = db_commit(&mut transaction,timeout_ms);
        if committed.kind != 0 { return DbMigrationResult(0,previous,committed); }
        return DbMigrationResult(len(&migrations)-history.applied,previous,committed);
    }
    async fn db_migrate(pool: DbPool, migrations: Buffer<DbMigration>, timeout_ms: U32) -> DbMigrationResult {
        let task: Task<DbMigrationResult> = spawn_blocking(db_migrate_worker,move pool,move migrations,timeout_ms);
        return await move task;
    }
    fn db_query_worker(pool: DbPool, sql: Buffer<U8>, params: Buffer<DbValue>, timeout_ms: U32, rows: USize, bytes: USize) -> DbRows guarantees(ThreadSafe) {
        let mut connection: DbConnection = db_acquire(&pool,timeout_ms);
        let mut statement: DbStatement = db_prepare(&mut connection,slice(&sql,0,len(&sql)),len(&params),timeout_ms);
        let mut output: DbRows = DbRows(buffer<DbRow>(0),db_statement_error(&statement));
        if rows > 1048576 || bytes > 67108864 { return DbRows(buffer<DbRow>(0),DbError(5,0,0)); }
        if output.error.kind != 0 { return move output; }
        for i in 0..len(&params) {
            let bound: DbError = db_bind(&mut statement,i+1,&params[i]);
            if bound.kind != 0 { return DbRows(buffer<DbRow>(0),bound); }
        }
        let mut left: USize = bytes;
        while true {
            let row: DbRow = db_fetch(&mut statement,left);
            if row.error.kind != 0 { return DbRows(buffer<DbRow>(0),row.error); }
            if row.done { return move output; }
            if len(&output.rows) == rows || left-row.bytes < size_of<DbRow>() { return DbRows(buffer<DbRow>(0),DbError(5,0,0)); }
            left = left-row.bytes-size_of<DbRow>();
            push(&mut output.rows,move row);
        }
    }
    async fn db_query(pool: DbPool, sql: Buffer<U8>, params: Buffer<DbValue>, timeout_ms: U32, rows: USize, bytes: USize) -> DbRows {
        let task: Task<DbRows> = spawn_blocking(db_query_worker,move pool,move sql,move params,timeout_ms,rows,bytes);
        return await move task;
    }
    fn db_stream_worker(pool: DbPool, sql: Buffer<U8>, params: Buffer<DbValue>, timeout_ms: U32, row_bytes: USize, output: Sender<DbRow>) -> DbError guarantees(ThreadSafe) {
        let mut connection: DbConnection = db_acquire(&pool,timeout_ms);
        let mut statement: DbStatement = db_prepare(&mut connection,slice(&sql,0,len(&sql)),len(&params),timeout_ms);
        let opened: DbError = db_statement_error(&statement);
        if opened.kind != 0 {
            let delivered: Bool = try_send(&output,DbRow(buffer<DbValue>(0),0,true,opened));
            return opened;
        }
        for i in 0..len(&params) {
            let bound: DbError = db_bind(&mut statement,i+1,&params[i]);
            if bound.kind != 0 {
                let delivered: Bool = try_send(&output,DbRow(buffer<DbValue>(0),0,true,bound));
                return bound;
            }
        }
        while true {
            let row: DbRow = db_fetch(&mut statement,row_bytes);
            let error: DbError = row.error;
            let done: Bool = row.done || error.kind != U32(0);
            let mut remaining: U32 = 0;
            unsafe { remaining = dlm_db_remaining(resource_get(&statement.inner).handle); }
            if !send_timeout(&output,move row,remaining) {
                if task_cancelled() || !sender_connected(&output) { return DbError(7,0,0); }
                if error.kind != 0 { return error; }
                return DbError(6,0,0);
            }
            if done { return error; }
        }
    }
    async fn db_stream(pool: DbPool, sql: Buffer<U8>, params: Buffer<DbValue>, timeout_ms: U32, row_bytes: USize, output: Sender<DbRow>) -> DbError {
        let task: Task<DbError> = spawn_blocking(db_stream_worker,move pool,move sql,move params,timeout_ms,row_bytes,move output);
        return await move task;
    }
}
