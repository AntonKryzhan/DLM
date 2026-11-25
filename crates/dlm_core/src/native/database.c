#include <limits.h>
#include <errno.h>
#if defined(_WIN32)
#include <winsock2.h>
#else
#include <poll.h>
#include <dlfcn.h>
#endif

typedef struct dnd_sqlite dnd_sqlite;
typedef struct dnd_sqlite_stmt dnd_sqlite_stmt;
typedef struct dnd_pg dnd_pg;
typedef struct dnd_result dnd_result;
typedef struct dnd_pg_cancel dnd_pg_cancel;
typedef struct { uint32_t kind; int32_t vendor; uint64_t state; } dnd_error;
typedef struct { void *handle; dnd_error error; } dnd_open;
typedef struct { size_t columns; bool row, done; dnd_error error; } dnd_step;
typedef struct { uint32_t kind, type; int64_t integer; double real; size_t length; dnd_error error; } dnd_cell;
typedef struct { uint32_t kind, type; int64_t integer; double real; const uint8_t *bytes; size_t length; } dnd_param;
enum { DND_OK, DND_INVALID, DND_DRIVER, DND_CONNECT, DND_SQL, DND_LIMIT,
       DND_TIMEOUT, DND_CANCEL, DND_BUSY, DND_STATE, DND_COMMIT_UNKNOWN };
enum { DND_NULL, DND_I64, DND_F64, DND_TEXT, DND_BYTES, DND_DECIMAL, DND_BOOL };
enum { DND_SQLITE = 1, DND_POSTGRES };
#define DND_PARAMS 128
#define DND_BYTES_LIMIT ((size_t)16777216)

static dnd_error dnd_err(uint32_t kind,int vendor) { return (dnd_error){kind,vendor,0}; }
static dnd_open dnd_open_err(dnd_error error) { return (dnd_open){NULL,error}; }

static void *dnd_library(const char *variable,const char *name) {
    const char *path=getenv(variable);
#if defined(_WIN32)
    if (path && *path) {
        if (!(path[0] && path[1]==':' && (path[2]=='\\' || path[2]=='/')) && !(path[0]=='\\' && path[1]=='\\')) return NULL;
        return LoadLibraryExA(path,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    }
    return LoadLibraryExA(name,NULL,LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
    if (path && *path && path[0]!='/') return NULL;
    return dlopen(path && *path ? path : name,RTLD_NOW | RTLD_LOCAL);
#endif
}
static void *dnd_symbol(void *library,const char *name) {
#if defined(_WIN32)
    return (void *)GetProcAddress((HMODULE)library,name);
#else
    return dlsym(library,name);
#endif
}
static void dnd_unload(void *library) {
#if defined(_WIN32)
    if (library) FreeLibrary((HMODULE)library);
#else
    if (library) dlclose(library);
#endif
}
#define DND_SQLITE_API(X) \
 X(int,threadsafe,(void)) \
 X(int,open_v2,(const char*,dnd_sqlite**,int,const char*)) \
 X(int,close_v2,(dnd_sqlite*)) \
 X(int,extended_result_codes,(dnd_sqlite*,int)) \
 X(int,limit,(dnd_sqlite*,int,int)) \
 X(int,get_autocommit,(dnd_sqlite*)) \
 X(void,progress_handler,(dnd_sqlite*,int,int(*)(void*),void*)) \
 X(int,busy_handler,(dnd_sqlite*,int(*)(void*,int),void*)) \
 X(int,prepare_v2,(dnd_sqlite*,const char*,int,dnd_sqlite_stmt**,const char**)) \
 X(int,finalize,(dnd_sqlite_stmt*)) \
 X(int,bind_parameter_count,(dnd_sqlite_stmt*)) \
 X(int,bind_null,(dnd_sqlite_stmt*,int)) \
 X(int,bind_int64,(dnd_sqlite_stmt*,int,int64_t)) \
 X(int,bind_double,(dnd_sqlite_stmt*,int,double)) \
 X(int,bind_text,(dnd_sqlite_stmt*,int,const char*,int,void(*)(void*))) \
 X(int,bind_blob,(dnd_sqlite_stmt*,int,const void*,int,void(*)(void*))) \
 X(int,step,(dnd_sqlite_stmt*)) \
 X(int,column_count,(dnd_sqlite_stmt*)) \
 X(int,column_type,(dnd_sqlite_stmt*,int)) \
 X(int64_t,column_int64,(dnd_sqlite_stmt*,int)) \
 X(double,column_double,(dnd_sqlite_stmt*,int)) \
 X(const uint8_t*,column_text,(dnd_sqlite_stmt*,int)) \
 X(const void*,column_blob,(dnd_sqlite_stmt*,int)) \
 X(int,column_bytes,(dnd_sqlite_stmt*,int)) \
 X(const char*,column_decltype,(dnd_sqlite_stmt*,int))
#define DND_PG_API(X) \
 X(int,isthreadsafe,(void)) \
 X(dnd_pg*,connectStart,(const char*)) \
 X(int,connectPoll,(dnd_pg*)) \
 X(int,status,(const dnd_pg*)) \
 X(int,setnonblocking,(dnd_pg*,int)) \
 X(int,socket,(const dnd_pg*)) \
 X(void,finish,(dnd_pg*)) \
 X(int,transactionStatus,(const dnd_pg*)) \
 X(int,sendPrepare,(dnd_pg*,const char*,const char*,int,const unsigned*)) \
 X(int,sendQueryPrepared,(dnd_pg*,const char*,int,const char *const*,const int*,const int*,int)) \
 X(int,setSingleRowMode,(dnd_pg*)) \
 X(int,flush,(dnd_pg*)) \
 X(int,consumeInput,(dnd_pg*)) \
 X(int,isBusy,(dnd_pg*)) \
 X(dnd_result*,getResult,(dnd_pg*)) \
 X(void,clear,(dnd_result*)) \
 X(int,resultStatus,(const dnd_result*)) \
 X(char*,resultErrorField,(const dnd_result*,int)) \
 X(char*,cmdStatus,(dnd_result*)) \
 X(int,nfields,(const dnd_result*)) \
 X(int,getisnull,(const dnd_result*,int,int)) \
 X(int,getlength,(const dnd_result*,int,int)) \
 X(char*,getvalue,(const dnd_result*,int,int)) \
 X(unsigned,ftype,(const dnd_result*,int))
#define DND_PG_CANCEL_API(X) \
 X(dnd_pg_cancel*,cancelCreate,(dnd_pg*)) \
 X(int,cancelStart,(dnd_pg_cancel*)) \
 X(int,cancelPoll,(dnd_pg_cancel*)) \
 X(int,cancelSocket,(const dnd_pg_cancel*)) \
 X(void,cancelFinish,(dnd_pg_cancel*))
#define DND_DECLARE(ret,name,args) ret (*name) args;
typedef struct { DND_SQLITE_API(DND_DECLARE) } dnd_sqlite_api;
typedef struct { DND_PG_API(DND_DECLARE) DND_PG_CANCEL_API(DND_DECLARE) } dnd_pg_api;
#undef DND_DECLARE

typedef struct {
    union { dnd_sqlite *sqlite; dnd_pg *pg; } raw;
    uint64_t deadline;
    bool broken, cleanup;
} dnd_db;
typedef struct dn_db_pool {
    dnc_sync lock;
    _Atomic size_t refs;
    uint32_t driver;
    size_t limit, active, idle, row_limit;
    dnd_db **available;
    char *address;
    void *library;
    dnd_error error;
    dnd_sqlite_api sq;
    dnd_pg_api pg;
    bool pg_cancel;
} dn_db_pool;
typedef struct dn_db_handle { dn_db_pool *pool; } dn_db_handle;
typedef struct {
    dn_db_pool *pool;
    dnd_db *db;
    uint64_t owner;
    size_t refs;
    bool closed, statement, transaction;
} dnd_lease;
typedef struct { dnd_lease *owner; bool active; } dnd_transaction;
typedef struct {
    dnd_lease *owner;
    dnd_sqlite_stmt *sqlite;
    dnd_result *row;
    char *sql;
    uint64_t deadline;
    size_t count, bound_bytes;
    dnd_param params[DND_PARAMS];
    bool bound[DND_PARAMS], started, done, on_row, pending;
} dnd_statement;

static char *dnd_string(const uint8_t *bytes,size_t length,size_t limit) {
    if (length>limit || (length && (!bytes || memchr(bytes,0,length)))) return NULL;
    char *text=malloc(length+1);
    if (text) { if (length) memcpy(text,bytes,length); text[length]=0; }
    return text;
}
static void dnd_load(dn_db_pool *pool) {
    bool complete=true;
    if (pool->driver==DND_SQLITE) {
#if defined(_WIN32)
        const char *library="sqlite3.dll";
#elif defined(__APPLE__)
        const char *library="libsqlite3.dylib";
#else
        const char *library="libsqlite3.so.0";
#endif
        pool->library=dnd_library("DLM_LIBSQLITE",library);
        if (!pool->library) { pool->error=dnd_err(DND_DRIVER,0); return; }
#define DND_LOAD(ret,name,args) do { *(void **)(&pool->sq.name)=dnd_symbol(pool->library,"sqlite3_" #name); if (!pool->sq.name) complete=false; } while (0);
        DND_SQLITE_API(DND_LOAD)
#undef DND_LOAD
        if (!complete || !pool->sq.threadsafe()) pool->error=dnd_err(DND_DRIVER,0);
    } else {
#if defined(_WIN32)
        const char *library="libpq.dll";
#elif defined(__APPLE__)
        const char *library="libpq.5.dylib";
#else
        const char *library="libpq.so.5";
#endif
        pool->library=dnd_library("DLM_LIBPQ",library);
        if (!pool->library) { pool->error=dnd_err(DND_DRIVER,0); return; }
#define DND_LOAD(ret,name,args) do { *(void **)(&pool->pg.name)=dnd_symbol(pool->library,"PQ" #name); if (!pool->pg.name) complete=false; } while (0);
        DND_PG_API(DND_LOAD)
        if (!complete || !pool->pg.isthreadsafe()) pool->error=dnd_err(DND_DRIVER,0);
        complete=true;
        DND_PG_CANCEL_API(DND_LOAD)
        pool->pg_cancel=complete;
#undef DND_LOAD
    }
}
static void dnd_close(dn_db_pool *pool,dnd_db *db) {
    if (pool->driver==DND_SQLITE) { if (db->raw.sqlite) pool->sq.close_v2(db->raw.sqlite); }
    else if (db->raw.pg) pool->pg.finish(db->raw.pg);
    free(db);
}
static void dnd_pool_release(dn_db_pool *pool) {
    if (atomic_fetch_sub_explicit(&pool->refs,1,memory_order_acq_rel)!=1) return;
    for (size_t i=0;i<pool->idle;i++) dnd_close(pool,pool->available[i]);
    dnd_unload(pool->library);
    if (pool->address) { volatile char *p=pool->address; while (*p) *p++=0; free(pool->address); }
    free(pool->available);
    dnc_destroy(&pool->lock);
    free(pool);
}
static void dn_db_finalize(void *raw) {
    dn_db_handle *handle=raw;
    if (handle->pool) dnd_pool_release(handle->pool);
}
static dn_db_handle *dn_db_open(uint32_t driver,const uint8_t *address,size_t length,size_t size,size_t row_limit) {
    dn_db_handle *handle=dn_allocate(sizeof *handle);
    ((dn_mem*)handle-1)->finalize=dn_db_finalize;
    dn_db_pool *pool=calloc(1,sizeof *pool);
    if (!pool) dn_fail("database pool allocation failed");
    if (!dnc_init(&pool->lock)) { free(pool); dn_fail("database pool initialization failed"); }
    atomic_init(&pool->refs,1);
    handle->pool=pool;
    pool->driver=driver;
    pool->row_limit=row_limit;
    if ((driver!=DND_SQLITE && driver!=DND_POSTGRES) || !size || size>64 || !row_limit || row_limit>DND_BYTES_LIMIT || !length || length>8192) {
        pool->error=dnd_err(DND_INVALID,0); return handle;
    }
    pool->address=dnd_string(address,length,8192);
    if (!pool->address || (driver==DND_SQLITE && size>1 && !strcmp(pool->address,":memory:"))) {
        pool->error=dnd_err(DND_INVALID,0); return handle;
    }
    pool->available=calloc(size,sizeof *pool->available);
    if (!pool->available) { pool->error=dnd_err(DND_LIMIT,0); return handle; }
    pool->limit=size;
    dnd_load(pool);
    return handle;
}
static dn_db_handle *dn_db_clone(dn_db_handle *source) {
    dn_db_pool *pool=((dn_db_handle *)dn_nonnull(source))->pool;
    dn_db_handle *handle=dn_allocate(sizeof *handle);
    handle->pool=pool;
    atomic_fetch_add_explicit(&pool->refs,1,memory_order_relaxed);
    ((dn_mem*)handle-1)->finalize=dn_db_finalize;
    return handle;
}

static uint32_t dnd_interrupted(dnd_db *db) {
    if (!db->cleanup && dn_task_cancelled()) return DND_CANCEL;
    return dnc_now()>=db->deadline?DND_TIMEOUT:DND_OK;
}
static int dnd_progress(void *raw) { return dnd_interrupted(raw)!=DND_OK; }
static int dnd_busy(void *raw,int tries) {
    (void)tries;
    if (dnd_progress(raw)) return 0;
    dnc_pause(2);
    return !dnd_progress(raw);
}
static dnd_error dnd_sqlite_error(dnd_lease *lease,int code) {
    uint32_t stop=dnd_interrupted(lease->db);
    if (stop) return dnd_err(stop,code);
    int kind=code & 255;
    if (kind==5 || kind==6) return dnd_err(DND_BUSY,code);
    return dnd_err(kind==7 || kind==18?DND_LIMIT:DND_SQL,code);
}
static int dnd_socket_poll(int fd,unsigned events) {
    if (fd<0) return -1;
    short requested=(events&1?POLLIN:0)|(events&2?POLLOUT:0);
#if defined(_WIN32)
    WSAPOLLFD poller={(SOCKET)(unsigned)fd,requested,0};
    int rc=WSAPoll(&poller,1,10);
    if (rc<0 && WSAGetLastError()==WSAEINTR) return 0;
#else
    struct pollfd poller={fd,requested,0};
    int rc=poll(&poller,1,10);
    if (rc<0 && errno==EINTR) return 0;
#endif
    return rc>0 && (poller.revents & POLLNVAL)?-1:rc;
}
static bool dnd_pg_wait(dnd_lease *lease,unsigned events) {
    int fd=lease->pool->pg.socket(lease->db->raw.pg);
    while (!dnd_interrupted(lease->db)) { int rc=dnd_socket_poll(fd,events); if (rc) return rc>0; }
    return false;
}
static void dnd_pg_try_cancel(dnd_lease *lease) {
    if (!lease->pool->pg_cancel) return;
    dnd_pg_api *pg=&lease->pool->pg;
    dnd_pg_cancel *cancel=pg->cancelCreate(lease->db->raw.pg);
    if (!cancel) return;
    if (pg->cancelStart(cancel)) {
        uint64_t deadline=dnc_now()+250;
        while (dnc_now()<deadline) {
            int state=pg->cancelPoll(cancel);
            if (state==0 || state==3) break;
            if ((state==1 || state==2) && dnd_socket_poll(pg->cancelSocket(cancel),(unsigned)state)<0) break;
        }
    }
    pg->cancelFinish(cancel);
}
static dnd_error dnd_connect(dnd_lease *lease) {
    dn_db_pool *pool=lease->pool;
    dnd_db *db=lease->db;
    if (pool->driver==DND_SQLITE) {
        int rc=pool->sq.open_v2(pool->address,&db->raw.sqlite,2 | 4 | 0x10000,NULL);
        if (rc) return dnd_err(DND_CONNECT,rc);
        pool->sq.extended_result_codes(db->raw.sqlite,1);
        pool->sq.limit(db->raw.sqlite,0,(int)pool->row_limit);
        pool->sq.limit(db->raw.sqlite,1,1048576);
        pool->sq.limit(db->raw.sqlite,2,256);
        pool->sq.limit(db->raw.sqlite,9,DND_PARAMS);
        pool->sq.progress_handler(db->raw.sqlite,1000,dnd_progress,db);
        pool->sq.busy_handler(db->raw.sqlite,dnd_busy,db);
        return dnd_err(DND_OK,0);
    }
    db->raw.pg=pool->pg.connectStart(pool->address);
    if (!db->raw.pg || pool->pg.status(db->raw.pg)==1) return dnd_err(DND_CONNECT,0);
    while (!dnd_interrupted(db)) {
        int state=pool->pg.connectPoll(db->raw.pg);
        if (state==3) return dnd_err(pool->pg.setnonblocking(db->raw.pg,1)==0?DND_OK:DND_CONNECT,0);
        if (state==0 || ((state==1 || state==2) && !dnd_pg_wait(lease,(unsigned)state))) break;
    }
    uint32_t stop=dnd_interrupted(db);
    return dnd_err(stop?stop:DND_CONNECT,0);
}
static bool dnd_owner(dnd_lease *lease) { return lease && lease->owner==dn_thread_id(); }
static void dnd_return(dnd_lease *lease) {
    if (--lease->refs) return;
    dn_db_pool *pool=lease->pool;
    dnd_db *db=lease->db;
    bool reusable=!db->broken && (pool->driver==DND_SQLITE ? pool->sq.get_autocommit(db->raw.sqlite) : (pool->pg.status(db->raw.pg)==0 && pool->pg.transactionStatus(db->raw.pg)==0));
    if (!reusable) dnd_close(pool,db);
    dnc_lock(&pool->lock);
    pool->active--;
    if (reusable) pool->available[pool->idle++]=db;
    dnc_signal(&pool->lock);
    dnc_unlock(&pool->lock);
    free(lease);
    dnd_pool_release(pool);
}
dnd_open dlm_db_acquire(void *raw,uint32_t timeout) {
    dn_db_pool *pool=raw;
    if (!pool || !timeout || timeout>3600000) return dnd_open_err(dnd_err(DND_INVALID,0));
    if (pool->error.kind) return dnd_open_err(pool->error);
    uint64_t deadline=dnc_now()+timeout;
    dnc_lock(&pool->lock);
    while (pool->active==pool->limit && !dn_task_cancelled() && dnc_now()<deadline) dnc_wait(&pool->lock,10);
    uint32_t stop=dn_task_cancelled()?DND_CANCEL:dnc_now()>=deadline?DND_TIMEOUT:DND_OK;
    if (stop) { dnc_unlock(&pool->lock); return dnd_open_err(dnd_err(stop,0)); }
    dnd_db *db=pool->idle?pool->available[--pool->idle]:NULL;
    pool->active++;
    dnc_unlock(&pool->lock);
    dnd_lease *lease=calloc(1,sizeof *lease);
    bool fresh=!db;
    if (fresh) db=calloc(1,sizeof *db);
    dnd_error error=dnd_err(DND_LIMIT,0);
    if (lease && db) {
        *lease=(dnd_lease){pool,db,dn_thread_id(),1,false,false,false};
        db->deadline=deadline;
        error=fresh?dnd_connect(lease):dnd_err(DND_OK,0);
        if (!error.kind) {
            atomic_fetch_add_explicit(&pool->refs,1,memory_order_relaxed);
            return (dnd_open){lease,error};
        }
    }
    if (db) dnd_close(pool,db);
    free(lease);
    dnc_lock(&pool->lock); pool->active--; dnc_signal(&pool->lock); dnc_unlock(&pool->lock);
    return dnd_open_err(error);
}
void dlm_db_release(void *raw) {
    dnd_lease *lease=raw;
    if (dnd_owner(lease) && !lease->closed) { lease->closed=true; dnd_return(lease); }
}
uint32_t dlm_db_driver(void *raw) {
    dnd_lease *lease=raw;
    return dnd_owner(lease) && !lease->closed ? lease->pool->driver : 0;
}

static dnd_error dnd_pg_error(dnd_lease *lease,dnd_result *result) {
    const char *code=result?lease->pool->pg.resultErrorField(result,'C'):NULL;
    if (!code || strlen(code)!=5) {
        uint32_t stop=dnd_interrupted(lease->db);
        return dnd_err(stop?stop:DND_CONNECT,0);
    }
    dnd_error error=dnd_err(DND_SQL,0);
    for (size_t i=0;i<5;i++) error.state|=(uint64_t)(uint8_t)code[i]<<(8*i);
    if (!strcmp(code,"55P03") || !strcmp(code,"40001") || !strcmp(code,"40P01")) error.kind=DND_BUSY;
    if (!strcmp(code,"57014")) error.kind=DND_CANCEL;
    return error;
}
static bool dnd_flush(dnd_lease *lease) {
    dnd_pg_api *pg=&lease->pool->pg;
    while (!dnd_interrupted(lease->db)) {
        int rc=pg->flush(lease->db->raw.pg);
        if (!rc) return true;
        if (rc<0 || !dnd_pg_wait(lease,3) || !pg->consumeInput(lease->db->raw.pg)) break;
    }
    lease->db->broken=true;
    return false;
}
static dnd_result *dnd_next(dnd_lease *lease) {
    dnd_pg_api *pg=&lease->pool->pg;
    dnd_pg *db=lease->db->raw.pg;
    while (!dnd_interrupted(lease->db)) {
        if (!pg->isBusy(db)) return pg->getResult(db);
        if (!dnd_pg_wait(lease,1) || !pg->consumeInput(db)) break;
    }
    lease->db->broken=true;
    return NULL;
}
static dnd_error dnd_pg_command(dnd_lease *lease,bool commit) {
    if (!dnd_flush(lease)) return dnd_pg_error(lease,NULL);
    dnd_pg_api *pg=&lease->pool->pg;
    dnd_result *result=dnd_next(lease);
    if (!result) { lease->db->broken=true; return dnd_pg_error(lease,NULL); }
    dnd_error error=pg->resultStatus(result)==1?dnd_err(DND_OK,0):dnd_pg_error(lease,result);
    if (commit && !error.kind && strcmp(pg->cmdStatus(result),"COMMIT")) error=dnd_err(DND_SQL,0);
    pg->clear(result);
    if (error.kind==DND_CONNECT) lease->db->broken=true;
    if (!lease->db->broken && (result=dnd_next(lease))) { pg->clear(result); lease->db->broken=true; }
    return error;
}
static bool dnd_lease_ready(dnd_lease *lease,void *transaction) {
    if (!dnd_owner(lease) || lease->closed || lease->statement || lease->db->broken) return false;
    if (!transaction) return !lease->transaction;
    dnd_transaction *txn=transaction;
    return txn->active && txn->owner==lease && lease->transaction;
}
static bool dnd_transaction_sql(const char *sql,bool nested) {
    if ((uint8_t)sql[0]==0xef && (uint8_t)sql[1]==0xbb && (uint8_t)sql[2]==0xbf) sql+=3;
    for (;;) {
        while (*sql && strchr(" \t\r\n\f\v;",*sql)) sql++;
        if (sql[0]=='-' && sql[1]=='-') { while (*sql && *sql!='\n' && *sql!='\r') sql++; }
        else if (sql[0]=='/' && sql[1]=='*') {
            size_t depth=1;
            sql+=2;
            while (*sql && depth) {
                if (sql[0]=='*' && sql[1]=='/') { depth--; sql+=2; }
                else if (nested && sql[0]=='/' && sql[1]=='*') { depth++; sql+=2; }
                else sql++;
            }
        } else break;
    }
    char keyword[16];size_t n=0;
    while (n+1<sizeof keyword && ((sql[n]>='A'&&sql[n]<='Z') || (sql[n]>='a'&&sql[n]<='z'))) {
        keyword[n]=sql[n]>='A'&&sql[n]<='Z'?sql[n]+32:sql[n];n++;
    }
    keyword[n]=0;
    if ((sql[n]>='0'&&sql[n]<='9') || sql[n]=='_' || sql[n]=='$' || (uint8_t)sql[n]>=128) return false;
    const char *control[]={"begin","start","commit","end","rollback","abort","savepoint","release","prepare"};
    for (size_t i=0;i<sizeof control/sizeof *control;i++) if (!strcmp(control[i],keyword)) return true;
    return false;
}
dnd_open dlm_db_prepare(void *raw,void *transaction,const uint8_t *sql,size_t length,size_t count,uint32_t timeout) {
    dnd_lease *lease=transaction?((dnd_transaction *)transaction)->owner:raw;
    if (!dnd_lease_ready(lease,transaction)) return dnd_open_err(dnd_err(DND_STATE,0));
    if (!length || count>DND_PARAMS || !timeout || timeout>3600000) return dnd_open_err(dnd_err(DND_INVALID,0));
    char *text=dnd_string(sql,length,1048576);
    if (!text) return dnd_open_err(dnd_err(DND_INVALID,0));
    if (dnd_transaction_sql(text,lease->pool->driver==DND_POSTGRES)) { free(text); return dnd_open_err(dnd_err(DND_STATE,0)); }
    dnd_statement *stmt=calloc(1,sizeof *stmt);
    if (!stmt) { free(text); return dnd_open_err(dnd_err(DND_LIMIT,0)); }
    stmt->owner=lease; stmt->count=count; stmt->deadline=dnc_now()+timeout; lease->db->deadline=stmt->deadline;
    dnd_error error=dnd_err(dnd_interrupted(lease->db),0);
    if (!error.kind && lease->pool->driver==DND_SQLITE) {
        dnd_sqlite_api *sq=&lease->pool->sq;
        const char *tail=NULL;
        int rc=sq->prepare_v2(lease->db->raw.sqlite,text,(int)length,&stmt->sqlite,&tail);
        if (rc) error=dnd_sqlite_error(lease,rc);
        else {
            while (tail && (*tail==' ' || *tail=='\r' || *tail=='\n' || *tail=='\t')) tail++;
            if (!stmt->sqlite || !tail || *tail || (size_t)sq->bind_parameter_count(stmt->sqlite)!=count) error=dnd_err(DND_INVALID,0);
        }
    }
    if (error.kind || stmt->sqlite) free(text); else stmt->sql=text;
    if (error.kind) {
        if (stmt->sqlite) lease->pool->sq.finalize(stmt->sqlite);
        free(stmt); return dnd_open_err(error);
    }
    lease->statement=true; lease->refs++;
    return (dnd_open){stmt,error};
}
dnd_error dlm_db_bind(void *raw,size_t index,dnd_param value) {
    dnd_statement *stmt=raw;
    if (!stmt || !dnd_owner(stmt->owner) || stmt->owner->closed || stmt->started || !index || index>stmt->count)
        return dnd_err(DND_STATE,0);
    if (value.kind>DND_BOOL || value.type>DND_BOOL || (value.length && !value.bytes)
        || (value.kind==DND_BOOL && value.integer!=0 && value.integer!=1)
        || (value.kind==DND_F64 && !isfinite(value.real))) return dnd_err(DND_INVALID,0);
    if (value.kind!=DND_TEXT && value.kind!=DND_BYTES && value.kind!=DND_DECIMAL) value.length=0;
    size_t slot=index-1;
    size_t current=stmt->bound_bytes-stmt->params[slot].length;
    if (value.length>stmt->owner->pool->row_limit-current) return dnd_err(DND_LIMIT,0);
    if (value.kind!=DND_BYTES && value.length && memchr(value.bytes,0,value.length)) return dnd_err(DND_INVALID,0);
    if (value.kind==DND_TEXT || value.kind==DND_BYTES || value.kind==DND_DECIMAL) {
        uint8_t *copy=malloc(value.length+1);
        if (!copy) return dnd_err(DND_LIMIT,0);
        if (value.length) memcpy(copy,value.bytes,value.length);
        copy[value.length]=0; value.bytes=copy;
    } else value.bytes=NULL;
    free((void*)stmt->params[slot].bytes);
    stmt->params[slot]=value; stmt->bound[slot]=true;
    stmt->bound_bytes=current+value.length;
    return dnd_err(DND_OK,0);
}
static dnd_error dnd_start(dnd_statement *stmt) {
    dnd_lease *lease=stmt->owner;
    for (size_t i=0;i<stmt->count;i++) if (!stmt->bound[i]) return dnd_err(DND_INVALID,0);
    if (lease->pool->driver==DND_SQLITE) {
        dnd_sqlite_api *sq=&lease->pool->sq;
        for (size_t i=0;i<stmt->count;i++) {
            dnd_param p=stmt->params[i];
            int index=(int)i+1,rc;
            switch (p.kind) {
                case DND_NULL: rc=sq->bind_null(stmt->sqlite,index); break;
                case DND_I64: case DND_BOOL: rc=sq->bind_int64(stmt->sqlite,index,p.integer); break;
                case DND_F64: rc=sq->bind_double(stmt->sqlite,index,p.real); break;
                case DND_BYTES: rc=sq->bind_blob(stmt->sqlite,index,p.bytes,(int)p.length,NULL); break;
                default: rc=sq->bind_text(stmt->sqlite,index,(const char*)p.bytes,(int)p.length,NULL); break;
            }
            if (rc) return dnd_sqlite_error(lease,rc);
        }
    } else {
        static const unsigned oid[]={0,20,701,25,17,1700,16};
        unsigned types[DND_PARAMS];
        const char *values[DND_PARAMS]; int lengths[DND_PARAMS],formats[DND_PARAMS];
        uint8_t binary[DND_PARAMS][8]; char integers[DND_PARAMS][24];
        for (size_t i=0;i<stmt->count;i++) {
            dnd_param p=stmt->params[i];
            types[i]=oid[p.kind==DND_NULL?p.type:p.kind];
            values[i]=(const char*)p.bytes; lengths[i]=(int)p.length; formats[i]=0;
            if (p.kind==DND_BYTES) formats[i]=1;
            else if (p.kind==DND_I64) { snprintf(integers[i],sizeof integers[i],"%" PRId64,p.integer); values[i]=integers[i]; }
            else if (p.kind==DND_BOOL) values[i]=p.integer?"true":"false";
            else if (p.kind==DND_F64) {
                uint64_t bits=dn_f64_bits(p.real);
                for (int j=0;j<8;j++) binary[i][j]=(uint8_t)(bits>>(56-j*8));
                values[i]=(const char*)binary[i]; lengths[i]=8; formats[i]=1;
            }
        }
        dnd_pg_api *pg=&lease->pool->pg;
        dnd_pg *db=lease->db->raw.pg;
        if (!pg->sendPrepare(db,"",stmt->sql,(int)stmt->count,types)) return dnd_pg_error(lease,NULL);
        dnd_error err=dnd_pg_command(lease,false);
        if (err.kind || lease->db->broken) return err.kind?err:dnd_pg_error(lease,NULL);
        if (!pg->sendQueryPrepared(db,"",(int)stmt->count,values,lengths,formats,1)) return dnd_pg_error(lease,NULL);
        stmt->pending=true;
        if (!pg->setSingleRowMode(db) || !dnd_flush(lease)) { lease->db->broken=true; return dnd_pg_error(lease,NULL); }
    }
    stmt->started=true;
    return dnd_err(DND_OK,0);
}

dnd_step dlm_db_step(void *raw) {
    dnd_statement *stmt=raw;
    dnd_step output={0};
    if (!stmt || !dnd_owner(stmt->owner) || stmt->owner->closed || stmt->owner->db->broken) { output.error=dnd_err(DND_STATE,0); return output; }
    if (stmt->done) { output.done=true; return output; }
    dnd_lease *lease=stmt->owner;
    lease->db->deadline=stmt->deadline;
    output.error=dnd_err(dnd_interrupted(lease->db),0);
    if (!output.error.kind && !stmt->started) output.error=dnd_start(stmt);
    stmt->on_row=false;
    if (output.error.kind) return output;
    if (stmt->sqlite) {
        int rc=lease->pool->sq.step(stmt->sqlite);
        if (rc==100) { output.row=true; output.columns=(size_t)lease->pool->sq.column_count(stmt->sqlite); }
        else if (rc==101) output.done=true;
        else output.error=dnd_sqlite_error(lease,rc);
    } else {
        dnd_pg_api *pg=&lease->pool->pg;
        if (stmt->row) { pg->clear(stmt->row); stmt->row=NULL; }
        dnd_result *result=dnd_next(lease);
        if (!result) { lease->db->broken=true; output.error=dnd_pg_error(lease,NULL); return output; }
        int kind=pg->resultStatus(result);
        if (kind==9) { stmt->row=result; output.row=true; output.columns=(size_t)pg->nfields(result); }
        else {
            if (kind==1 || kind==2) output.done=true;
            else output.error=dnd_pg_error(lease,result);
            pg->clear(result);
            if (kind==3 || kind==4 || kind==8 || output.error.kind==DND_CONNECT) lease->db->broken=true;
            else if ((result=dnd_next(lease))) { pg->clear(result); lease->db->broken=true; }
            stmt->pending=false;
        }
    }
    if (output.row && output.columns>256) { output.row=false; output.error=dnd_err(DND_LIMIT,0); }
    stmt->on_row=output.row;
    stmt->done=output.done || output.error.kind;
    return output;
}
uint32_t dlm_db_remaining(void *raw) {
    dnd_statement *stmt=raw;
    if (!stmt || !dnd_owner(stmt->owner)) return 0;
    uint64_t now=dnc_now();
    return stmt->deadline>now?(uint32_t)(stmt->deadline-now):0;
}
static uint32_t dnd_pg_kind(unsigned oid) {
    switch (oid) {
        case 16: return DND_BOOL;
        case 20: case 21: case 23: return DND_I64;
        case 700: case 701: return DND_F64;
        case 17: return DND_BYTES;
        case 1700: return DND_DECIMAL;
        case 18: case 19: case 25: case 1042: case 1043: return DND_TEXT;
        default: return DND_BYTES;
    }
}
static uint32_t dnd_declared(const char *name) {
    if (!name) return DND_NULL;
    char type[32]; size_t i=0;
    while (i<sizeof type-1 && name[i] && name[i]!='(' && name[i]!=' ') {
        char c=name[i]; type[i++]=c>='a'&&c<='z'?c-32:c;
    }
    type[i]=0;
    if (strstr(type,"INT")) return DND_I64;
    if (strstr(type,"REAL") || strstr(type,"FLOA") || strstr(type,"DOUB")) return DND_F64;
    if (strstr(type,"BOOL")) return DND_BOOL;
    if (strstr(type,"BLOB")) return DND_BYTES;
    if (strstr(type,"CHAR") || strstr(type,"CLOB") || strstr(type,"TEXT")) return DND_TEXT;
    if (strstr(type,"DEC") || strstr(type,"NUMERIC")) return DND_DECIMAL;
    return DND_NULL;
}
static size_t dnd_numeric_text(const uint8_t *bytes,size_t n,uint8_t *out,size_t cap) {
    if (n<8 || n%2) return SIZE_MAX;
    size_t digits=dn_binary_read(bytes,n,0,2,1);
    int weight=(int16_t)dn_binary_read(bytes,n,2,2,1);
    unsigned sign=(unsigned)dn_binary_read(bytes,n,4,2,1);
    size_t scale=dn_binary_read(bytes,n,6,2,1);
    if (digits!=(n-8)/2 || (sign!=0 && sign!=0x4000)) return SIZE_MAX;
    size_t len=0;
#define DND_APPEND(c) do { if (out) { if (len>=cap) return SIZE_MAX; out[len]=(uint8_t)(c); } len++; } while(0)
    if (sign) DND_APPEND('-');
    if (weight<0) DND_APPEND('0');
    else for (int i=0;i<=weight;i++) {
        unsigned digit=(size_t)i<digits?(unsigned)dn_binary_read(bytes,n,8+2*(size_t)i,2,1):0;
        if (digit>9999) return SIZE_MAX;
        unsigned divisor=1000;
        while (!i && divisor>1 && digit<divisor) divisor/=10;
        for (;divisor;divisor/=10) DND_APPEND('0'+digit/divisor%10);
    }
    if (scale) DND_APPEND('.');
    for (size_t i=0;i<scale;i++) {
        int idx=weight+1+(int)(i/4);
        unsigned digit=idx>=0 && (size_t)idx<digits?(unsigned)dn_binary_read(bytes,n,8+2*(size_t)idx,2,1):0;
        if (digit>9999) return SIZE_MAX;
        static const unsigned d[]={1000,100,10,1};
        DND_APPEND('0'+digit/d[i%4]%10);
    }
#undef DND_APPEND
    return len;
}

dnd_cell dlm_db_cell(void *raw,size_t col,uint8_t *out,size_t cap) {
    dnd_statement *stmt=raw;
    dnd_cell cell={0};
    if (!stmt || !dnd_owner(stmt->owner) || !stmt->on_row) { cell.error=dnd_err(DND_STATE,0); return cell; }
    const uint8_t *bytes=NULL;
    if (stmt->sqlite) {
        dnd_sqlite_api *sq=&stmt->owner->pool->sq;
        if (col>=(size_t)sq->column_count(stmt->sqlite)) { cell.error=dnd_err(DND_INVALID,0); return cell; }
        int type=sq->column_type(stmt->sqlite,(int)col);
        cell.type=dnd_declared(sq->column_decltype(stmt->sqlite,(int)col));
        if (type==1) {
            cell.kind=cell.type==DND_BOOL?DND_BOOL:DND_I64;
            cell.integer=sq->column_int64(stmt->sqlite,(int)col);
            if (cell.kind==DND_BOOL) cell.integer=cell.integer!=0;
        } else if (type==2) { cell.kind=DND_F64; cell.real=sq->column_double(stmt->sqlite,(int)col); }
        else if (type==3 || type==4) {
            cell.kind=type==3?DND_TEXT:DND_BYTES;
            bytes=type==3?sq->column_text(stmt->sqlite,(int)col):sq->column_blob(stmt->sqlite,(int)col);
            cell.length=(size_t)sq->column_bytes(stmt->sqlite,(int)col);
            if (type==3 && !bytes) cell.error=dnd_err(DND_LIMIT,0);
        }
    } else {
        dnd_pg_api *pg=&stmt->owner->pool->pg;
        if (!stmt->row || col>=(size_t)pg->nfields(stmt->row)) { cell.error=dnd_err(DND_INVALID,0); return cell; }
        cell.type=dnd_pg_kind(pg->ftype(stmt->row,(int)col));
        if (pg->getisnull(stmt->row,0,(int)col)) return cell;
        cell.kind=cell.type;
        bytes=(const uint8_t*)pg->getvalue(stmt->row,0,(int)col);
        size_t n=(size_t)pg->getlength(stmt->row,0,(int)col);
        if (cell.kind==DND_I64 && (n==2 || n==4 || n==8)) {
            uint64_t bits=dn_binary_read(bytes,n,0,n,1);
            if (n<8 && bytes[0]&128) bits|=UINT64_MAX<<(8*n);
            memcpy(&cell.integer,&bits,8);
        } else if (cell.kind==DND_BOOL && n==1) cell.integer=bytes[0]!=0;
        else if (cell.kind==DND_F64 && n==8) cell.real=dn_f64_from_bits(dn_binary_read(bytes,n,0,8,1));
        else if (cell.kind==DND_F64 && n==4) cell.real=dn_f32_from_bits((uint32_t)dn_binary_read(bytes,n,0,4,1));
        else if (cell.kind==DND_DECIMAL) {
            cell.length=dnd_numeric_text(bytes,n,NULL,0);
            if (cell.length>stmt->owner->pool->row_limit || (out && dnd_numeric_text(bytes,n,out,cap)==SIZE_MAX)) cell.error=dnd_err(DND_LIMIT,0);
            return cell;
        } else if (cell.kind==DND_TEXT || cell.kind==DND_BYTES) cell.length=n;
        else cell.error=dnd_err(DND_STATE,0);
    }
    if (cell.length>stmt->owner->pool->row_limit || (out && cell.length>cap)) cell.error=dnd_err(DND_LIMIT,0);
    if (!cell.error.kind && out && cell.length) memcpy(out,bytes,cell.length);
    return cell;
}
void dlm_db_stmt_close(void *raw) {
    dnd_statement *stmt=raw;
    if (!stmt || !dnd_owner(stmt->owner)) return;
    dnd_lease *lease=stmt->owner;
    if (stmt->sqlite) lease->pool->sq.finalize(stmt->sqlite);
    if (stmt->row) lease->pool->pg.clear(stmt->row);
    if (stmt->pending) { dnd_pg_try_cancel(lease); lease->db->broken=true; }
    for (size_t i=0;i<stmt->count;i++) free((void*)stmt->params[i].bytes);
    free(stmt->sql); free(stmt);
    lease->statement=false;
    dnd_return(lease);
}
static dnd_error dnd_control(dnd_lease *lease,const char *command,uint32_t timeout) {
    lease->db->deadline=dnc_now()+timeout;
    uint32_t stop=dnd_interrupted(lease->db);
    if (stop) return dnd_err(stop,0);
    if (lease->pool->driver==DND_SQLITE) {
        dnd_sqlite_api *sq=&lease->pool->sq;
        dnd_sqlite_stmt *stmt=NULL;
        int rc=sq->prepare_v2(lease->db->raw.sqlite,command,-1,&stmt,NULL);
        if (!rc) rc=sq->step(stmt);
        if (stmt) sq->finalize(stmt);
        return rc==101?dnd_err(DND_OK,0):dnd_sqlite_error(lease,rc);
    }
    dnd_pg_api *pg=&lease->pool->pg;
    if (!pg->sendPrepare(lease->db->raw.pg,"",command,0,NULL)) return dnd_pg_error(lease,NULL);
    dnd_error err=dnd_pg_command(lease,false);
    if (err.kind || lease->db->broken) return err.kind?err:dnd_pg_error(lease,NULL);
    if (!pg->sendQueryPrepared(lease->db->raw.pg,"",0,NULL,NULL,NULL,0)) return dnd_pg_error(lease,NULL);
    bool commit=!strcmp(command,"COMMIT");
    err=dnd_pg_command(lease,commit);
    if (err.kind && lease->db->broken && commit) return dnd_err(DND_COMMIT_UNKNOWN,0);
    return err;
}
dnd_open dlm_db_begin(void *raw,uint32_t timeout) {
    dnd_lease *lease=raw;
    if (!dnd_lease_ready(lease,NULL)) return dnd_open_err(dnd_err(DND_STATE,0));
    if (!timeout || timeout>3600000) return dnd_open_err(dnd_err(DND_INVALID,0));
    dnd_transaction *txn=calloc(1,sizeof *txn);
    if (!txn) return dnd_open_err(dnd_err(DND_LIMIT,0));
    dnd_error error=dnd_control(lease,lease->pool->driver==DND_SQLITE?"BEGIN IMMEDIATE":"BEGIN",timeout);
    if (error.kind) { free(txn); return dnd_open_err(error); }
    *txn=(dnd_transaction){lease,true}; lease->transaction=true; lease->refs++;
    return (dnd_open){txn,error};
}
dnd_error dlm_db_transaction(void *raw,uint32_t action,const uint8_t *name,size_t n,uint32_t timeout) {
    dnd_transaction *txn=raw;
    if (!txn || !dnd_lease_ready(txn->owner,txn)) return dnd_err(DND_STATE,0);
    if (!timeout || timeout>3600000) return dnd_err(DND_INVALID,0);
    const char *op=action==1?"COMMIT":action==2?"ROLLBACK":action==3?"SAVEPOINT":action==4?"ROLLBACK TO SAVEPOINT":action==5?"RELEASE SAVEPOINT":NULL;
    if (!op) return dnd_err(DND_INVALID,0);
    char sql[64];
    if (action>=3) {
        if (!name || !n || n>32) return dnd_err(DND_INVALID,0);
        for (size_t i=0;i<n;i++) {
            uint8_t c=name[i];
            if (!(c=='_' || (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (i && c>='0'&&c<='9'))) return dnd_err(DND_INVALID,0);
        }
        snprintf(sql,sizeof sql,"%s \"%.*s\"",op,(int)n,(const char*)name); op=sql;
    }
    dnd_error err=dnd_control(txn->owner,op,timeout);
    if (!err.kind && action<=2) { txn->active=false; txn->owner->transaction=false; }
    return err;
}
void dlm_db_tx_close(void *raw) {
    dnd_transaction *txn=raw;
    if (!txn || !dnd_owner(txn->owner)) return;
    dnd_lease *lease=txn->owner;
    if (txn->active) {
        if (!lease->statement && !lease->db->broken) {
            lease->db->cleanup=true;
            dnd_error err=dnd_control(lease,"ROLLBACK",250);
            lease->db->cleanup=false;
            if (err.kind) lease->db->broken=true;
        } else lease->db->broken=true;
        lease->transaction=false;
    }
    free(txn);
    dnd_return(lease);
}
