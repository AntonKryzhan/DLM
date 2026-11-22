# Owned network reactor — DLM 0.136.0

This is the first DLN-04 implementation block. `std.net_reactor` supplies an
explicitly driven group of owned operations, using the existing native language,
Resource finalizers and `dlm.net.v1` foreign boundary. It adds no parser syntax,
proof rule, implicit thread, general task scheduler or `async/await` construct.

## Installation

Apply this source delta after DLM 0.135.0. Keep DLM-main, DLM-pre and DLM-OUT
paths. Run DLM-pre/Build-Network.cmd to rebuild the compiler and SDK and execute
all five network examples. SkipCompilerBuild requires a compiler >=0.136.0.
The older binary cannot embed the newly added provider entry points.

On Linux, from DLM-main:

```bash
cargo build --workspace --release --locked --offline
target/release/dlm build examples/native/network_reactor.dlm --lib lib --allow-unsafe -o network_reactor
./network_reactor
```

## Ownership and operation lifecycle

Create a NetworkReactor with `reactor_open(channels,operations,byte_limit)` and
check `reactor_error`. Adopt connected TcpSocket, TcpListener or UdpSocket with
the corresponding `reactor_adopt_*` call, passing an explicit move. Adoption
consumes the input even on failure and closes a rejected socket. TLS/WebSocket
owners cannot be adopted. Asynchronous TLS setup and encrypted-buffer readiness
need a separate design; this API does not silently treat them as plain TCP.

The reactor owns the socket after adoption. ReactorTcp, ReactorListener and
ReactorUdp are copyable keys into that owner, with distinct API types. They do
not extend the reactor lifetime or independently close a socket. Generation
identities are unique within a loaded provider and checked against the target
reactor, including after slot reuse. Stale, forged or foreign-reactor keys
return status 4. Public native record representations remain unsealed.

Submit operations, check `NetworkOperation.error`, and retain the operation id.
Submission acceptance is separate from completion success. A single immediate
nonblocking attempt may already complete I/O. Otherwise the group owns the
pending state until a later `reactor_poll`, cancellation or close. It retains
neither a user pointer nor a DLM borrow between calls.

Read/receive allocate a bounded provider buffer. Write/send copy the input
before returning; the caller may immediately change or drop its source buffer.
This release chooses explicit copying rather than an untracked asynchronous
borrow. It does not advertise zero-copy asynchronous writes.

`reactor_result(group,id)` returns status 1 while pending, with current partial
count. `reactor_next` peeks at the oldest terminal completion. Successful I/O,
EOF, timeout, cancellation and system errors all produce terminal results.
`reactor_copy` copies received bytes after termination, including partial bytes
before an error. It supports a source offset, checks bounds and leaves bytes
beyond its returned count unchanged. It refuses pending operations and writes.

`reactor_ack` explicitly frees a terminal operation and its provider buffer.
Acknowledgement can occur out of completion order; the remaining FIFO order is
preserved. Acknowledging a pending or stale id is an error. A failed submission
has id 0 and owns no operation. `reactor_cancel` rejects id 0; use the explicitly
named `reactor_cancel_all` for group cancellation.

An accepted connection belongs to the group as `completion.accepted`, not to
the completion buffer. Acknowledging accept does not close its connection.
`reactor_*_close` invalidates a channel and cancels its pending operations, while
retaining their terminal counts/buffers until acknowledgement. `reactor_release_*`
restores an ordinary typed socket owner only after all operations for that
channel, including completed ones, have been acknowledged. Failure to release
does not remove the channel; inspect the returned typed owner error.

Closing or finalizing the NetworkReactor closes the kernel queue, sockets and
all operation buffers, including unacknowledged results. No background task or
overlapped OS request survives it. The DLM error boundary uses the same finalizer,
also when another field of an owning record has already been moved out.

## Operations

| Kind | API | Completion |
| --- | --- | --- |
| 1 | reactor_read(Tcp key,count,exact,timeout) | One nonempty portion, or exact count with partial progress on error |
| 2 | reactor_write(Tcp key,slice,timeout) | All bytes, with partial count on failure |
| 3 | reactor_accept(Listener key,timeout) | New group-owned TCP key |
| 4 | reactor_timer(delay) | Status 0 at its deadline |
| 5 | reactor_receive(Udp key,capacity,timeout) | One datagram, peer and truncation status |
| 6 | reactor_send(Udp key,peer,slice,timeout) | One datagram, preserving the empty case |
| 7 | reactor_event(timeout) / reactor_signal(id) | Status 0 on signal, 3 if its deadline wins |
| 8 | reactor_connect(numeric address,timeout) | TCP key usable after successful completion |

Numeric connect performs no DNS and checks SO_ERROR after readiness. A failed,
timed-out or cancelled connect closes and invalidates its new channel. Its
completion still identifies the failed channel; that key is not a live socket.
Hostname resolution remains a separate synchronous call.

There is at most one pending reader and one pending writer per channel; accept
uses the reader position and connect the writer position. A conflicting submit
returns would-block (1). Completed operations may coexist with later work on
the same channel, subject to the global operation and memory limits.

Zero-length TCP read is invalid; zero-length TCP write completes successfully.
UDP receive capacity 0 still consumes one datagram: empty data is status 0,
nonempty data is truncated (10). UDP send allows empty data and rejects lengths
above 65507. Exact TCP EOF is status 2 with partial count, not success.

## Deadlines, cancellation and fairness

Each submission records one monotonic deadline, not a new budget per retry.
Timeout 0 permits one immediate nonblocking I/O attempt; unfinished work then
terminates with status 3. A timer with delay 0 completes immediately. Polling
also has its own maximum wait; a poll timeout does not cancel pending operations.
Every resumed I/O step rechecks its operation deadline. Signalling an event
after its deadline records timeout even if no poll has yet observed expiry.

`reactor_poll(group,timeout,cancel)` drives all group operations on its calling
thread. It returns the number of currently retained terminal completions, with
status 0; an empty group returns 1. Existing unacknowledged completions make it
return immediately. Drain/acknowledge results before waiting for more progress.
Operation deadlines limit OS waits even when the poll budget is larger.

Each readiness batch advances at most one nonblocking step per direction per
channel. Channel traversal rotates between batches. Exact reads/writes therefore
do not drain an unbounded amount in one submission or readiness step. Deadlines
are scanned in bounded-capacity tables; this is not a timer heap or a throughput
benchmark. All runtime methods retain their conservative FFI effect admission.

Explicit cancellation synchronously changes a pending operation to terminal 9.
Already completed work wins and remains unchanged. A signalled legacy atomic
cancel token passed to poll cancels every pending operation. Poll checks it
before progress and after each OS wait, slicing waits to at most 50 ms when a
token is supplied. Scheduler delay is additional. A cancelled partial write may
already have sent bytes; the API never retransmits them automatically.

All calls on one group, including event signalling, are serialized by its
owner. Event signalling is not a cross-thread wake API. Only the existing cancel
token signal is thread-safe; its owner must outlive every polling thread.

## Backpressure and platform boundary

Channel and operation limits are 1..4096 each; byte_limit is 0..64 MiB.
Fixed control tables and kernel-event scratch space are allocated at creation.
Payload reservations count the full requested buffer capacity, both pending and
terminal, until ack. Byte/operation exhaustion rejects the submission with
status 7 before taking ownership of user data. There is no hidden growing queue.
Fixed table overhead and OS socket buffers are separate from byte_limit.

`reactor_statistics` reports live channels, pending/completed operations, reserved
payload bytes and the selected backend. Backend identity is factual:

| Value | Branch | Execution evidence |
| --- | --- | --- |
| 1 | Linux epoll, level-triggered | Linux acceptance and sanitizer hosts |
| 2 | BSD/macOS kqueue, level-triggered | Source implementation; no native execution here |
| 3 | Windows WSAPoll | Source implementation; no native execution here |
| 4 | Other POSIX poll, or explicit test build | Same Linux acceptance corpus as epoll |

Read/write interest exists only while the corresponding operation is pending.
Kernel queue/control failure becomes a sticky reactor fault; polling terminates
remaining operations with the system error, and callers close the group.
The Windows branch is WSAPoll, not IOCP. IOCP, native Windows/FreeBSD validation,
cross-thread wake queues, task scheduling and suspend/resume language integration
remain outstanding DLN-04 work.

## Verification

```bash
cargo test --workspace --release --locked --offline
python3 crates/dlm_core/tests/native_hosts/dln_reactor.py
python3 tools/check_dln_compatibility.py
python3 tools/check_dln_compatibility.py --snapshot docs/DLN_BASELINE_0.135.0.json
cc -std=gnu11 -g -fsanitize=address,undefined -fno-omit-frame-pointer crates/dlm_core/tests/native_hosts/dln_reactor_host.c -ldl -o reactor_host
ASAN_OPTIONS=detect_leaks=0 ./reactor_host
```

The independent Python suite builds the production backend and forced poll
variant with warnings treated as errors. It tests OS peers, copied partial
writes, partial read timeout/EOF/cancel, bounded queues, stale identities, UDP,
FIFO ordering, random lifecycle transitions and exported DLM failure cleanup.
It is Linux/GNU-host validation tooling; Windows/FreeBSD results require native
jobs. Compatibility snapshots protect existing sources and C functions, not
formal network proof certificates. See DLN_REACTOR_TEST_RESULTS.md for results.

Primary backend API references:

- https://man7.org/linux/man-pages/man7/epoll.7.html
- https://man.freebsd.org/cgi/man.cgi?query=kqueue&sektion=2&format=html
- https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsapoll
