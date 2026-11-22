# DLN scoped async validation — 0.137.0

Development host: Linux x86-64, Rust 1.99.1, GCC-compatible native C backend.

| Executed suite | Result |
| --- | --- |
| cargo test --workspace --all-targets --release --locked --offline | 1026 passed, 0 failed, 1 pre-existing GUI test ignored |
| dln_async.py, production epoll and forced poll | 51 passed, 0 skipped |
| dln_reactor.py, previous reactor and native cleanup | 33 passed, 0 skipped |
| dln_foundation.py, binary/typed sockets, TLS, HTTP and WebSocket | 31 passed, 0 skipped |
| dln_async_host.c, epoll and forced poll | Both passed |
| AddressSanitizer + UndefinedBehaviorSanitizer, both native hosts | Passed, no reported memory/UB error |
| ThreadSanitizer, epoll native thread host | Passed, no reported data race |

The five Rust scope tests are included in the full workspace total. They execute
both new DLM examples and reject owner duplication/use-after-move, invalid key
and token types, implicit foreign admission and false effect guarantees.
The independent Python suite uses real OS TCP/UDP peers and producer threads;
it also executes the DLM error boundary sixteen times with pending buffers,
a partially moved owning record and a retained cancellation token.

Each native host exercises 100 close/signal races with 10,000 producer requests
per race, another 50,000 requests while draining/rearming notifications, bounded
queue admission/coalescing, blocking poll wake, cancellation retirement, join
with retained results and UDP truncation including zero receive capacity.
Provider owner counters and DLM live-allocation counters return to zero where
expected. LeakSanitizer cannot execute under this environment's tracing, so the
ASan/UBSan runs explicitly use ASAN_OPTIONS=detect_leaks=0:abort_on_error=1.
No leak-scanner result is claimed.

## Compatibility and package application

All three versioned engineering snapshots pass:

| Baseline | Protected compiler/stdlib files | Preserved public C declarations and bodies |
| --- | --- | --- |
| 0.134.0 | 136 | 39 |
| 0.135.0 | 140 | 46 |
| 0.136.0 | 141 | 61 |

Nine provider entry points are added. The existing std.net_reactor module is
byte-identical. The independent snapshot checker is included in the cumulative
archive together with all foundation/reactor prerequisites.

The cumulative patch was applied to a clean 0.134 source copy. All 887 files
under its source/SDK-library/builder trees matched the intended 0.137 tree.
All seven native network examples were built and run from that applied tree;
both C thread hosts and all three compatibility snapshots passed. ZIP member
CRC/bytes, duplicate/path checks, mirrored SDK modules, CRLF in the Windows
runner, and the documentation inventory were checked.

## Native platform gates

The user subsequently confirmed successful Windows 0.137 compilation/execution
of all seven DLM network examples and the native thread/lifecycle host, reporting
backend=5. This is native IOCP smoke/lifecycle evidence; the full platform matrix
and FreeBSD/kqueue still need native execution. The structured record is
DLN_WINDOWS_0.137.0.json. It does not validate new 0.138 crypto operations.

Run DLM-pre/Build-Network.cmd on Windows. It rebuilds 0.137, executes seven DLM
examples and compiles/runs dln_async_host.c. The native host requires backend=5
on Windows and exercises actual CreateThread/IOCP interaction. A failure prevents
the final success message; a separate transcript records network validation.
The source patch contains no prebuilt Windows executable.

Contracts and build commands are in DLN_ASYNC.md. These engineering checks are
not formal network certificates and do not establish arbitrary protocol safety.
