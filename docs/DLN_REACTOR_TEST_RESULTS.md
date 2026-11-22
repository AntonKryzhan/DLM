# DLN reactor 0.136.0 validation record

Date: 2026-11-19. Executed on Linux x86-64, Rust 1.99.1, GNU C and Python 3.

The full release workspace regression completed: 1021 passed, 0 failed,
1 GUI test ignored; documentation tests completed (0). The final rebuilt
compiler also passed the focused reactor harness and independent acceptance.

The focused Rust reactor harness passed 4 tests: compiled eight-client TCP
operation group, move/close/adopt ownership rejection, distinct channel kinds,
and explicit FFI/effect admission. Existing compiler/checker/kernel sources
remain unchanged; the provider receives additive reactor functions.

Independent reactor acceptance passed 33 tests without skips: the same 16
scenarios on epoll and forced POSIX poll, plus an exported DLM ownership fixture.
They include a 2 MiB copied partial write checked against an independent Python
receiver, fragmented exact reads, partial timeout/EOF/cancellation, a concurrent
atomic cancel signal, zero-timeout progress, signals after deadline, numeric asynchronous
connect/accept/refusal, UDP empty and
truncated datagrams, reserved-byte and operation backpressure, terminal FIFO
ordering, release/close, stale/cross-reactor identities, and 1200 randomized
lifecycle transitions per backend. DLM error-boundary cleanup was repeated eight
times with pending provider buffers and a partially moved owning record; native
allocation counters returned to zero and provider shutdown succeeded.

The previous DLN foundation acceptance also passed all 31 tests, with required
libcurl backend and no skips, covering old TLS/HTTP2/WebSocket APIs and codecs.

The standalone owned-UDP host compiled with warnings treated as errors and
passed ASan/UBSan on both epoll and forced poll, 300 request/result/ack cycles
per backend followed by cancellation and group destruction. LeakSanitizer was
disabled; no leak-sanitizer result is claimed.
The compiled eight-client DLM TCP example also passed ASan/UBSan.

Both compatibility snapshots pass: v0.134 protects 136 source files and 39
public C functions; v0.135 protects 140 files and 46 functions. The latter
reports 15 additive reactor C entry points. These are engineering checks,
not protocol proof certificates.

Windows WSAPoll and BSD/macOS kqueue source branches have not been compiled or
executed on their native platforms here. IOCP, general tasks/async syntax,
cross-thread reactor wake and asynchronous TLS are not implemented by this patch.
