# Scoped asynchronous networking — DLM 0.137.0

This is the second DLN-04 block. `std.net_async` adds an owning NetworkScope,
bounded producer notifications, explicit join, and a Windows IOCP driver.
Source checking, native ownership, C ABI admission and Resource finalization
are reused. No syntax, proof-kernel rule, implicit thread or general-purpose
language task scheduler is added. The layer is runtime-control; socket bytes
and buffers remain ordinary hardware-execution storage.

## Install and execute

This cumulative networking source patch applies after 0.134.0, 0.135.0 or
0.136.0. It includes the required foundation/reactor files and their examples.
Run `DLM-pre/Build-Network.cmd`. It checks required files before building,
rebuilds the compiler and SDK, runs the five previous examples plus two scope
examples, then compiles and executes the native thread/lifecycle host.
The runner requires compiler >=0.137.0; `-SkipCompilerBuild` is only for a rebuilt
SDK. Detailed network output is saved to `%LOCALAPPDATA%/DLM/BuildCache/network-*.log`.
The CMD launcher pauses after success or error.

```bash
cargo build --workspace --release --locked --offline
target/release/dlm build examples/native/network_scope.dlm --lib lib --allow-unsafe -o network_scope
./network_scope
target/release/dlm build examples/native/network_scope_control.dlm --lib lib --allow-unsafe -o network_scope_control
./network_scope_control
```

## Scope ownership and results

`scope_open(channels,operations,byte_limit,wake_limit)` allocates the fixed control
tables. Limits are 1..4096 channels and operations, 0..64 MiB of reserved payload,
and 1..4096 queued producer commands. Invalid limits return an invalid opened
owner; check `scope_error` before use. All calls on one scope are serialized by
its owner. Only notification through an independently owned waker is ThreadSafe.

`scope_adopt_tcp`, `scope_adopt_listener` and `scope_adopt_udp` consume socket
owners even on admission failure. Scope operations use the existing distinct
ReactorTcp/ReactorListener/ReactorUdp keys and NetworkOperation identifiers.
Generation checks reject stale keys, operation IDs and keys from another group.
The new scope type is distinct from NetworkReactor: high-level APIs cannot
accidentally mix their owners. Public native record representations are still
unsealed; forged raw FFI handles remain outside the high-level guarantee.

The scope owns sockets, operation buffers, terminal results and kernel requests.
Writes copy their input before returning; reads use provider-owned storage.
No pending OS operation retains a pointer or borrow into DLM user memory.
Submission success does not imply completion. Poll, then inspect results using
`scope_result` or FIFO `scope_next`; `scope_copy` retrieves received bytes and
`scope_ack` releases terminal operation capacity and payload reservations.
The operation kinds and status codes match DLN_REACTOR.md.

`scope_join(timeout,cancel)` advances all pending operations until they are
terminal, the join budget expires, or the external cancel token is observed.
It progresses even with retained terminal results; it does not acknowledge or
discard them. Its successful count is the retained terminal count, not the
number of successful I/O operations: inspect each completion's error.
Join timeout leaves unfinished operations pending. Explicit `scope_cancel`,
`scope_cancel_all` or scope close cancels them. Already published terminal
results keep their original status and partial byte count.

Closing/finalizing the scope cuts off producers, cancels unfinished operations,
retires kernel requests, closes sockets and frees all buffers/results. This also
runs on a DLM runtime error and on partial moves from owning records. A scope
cannot export a socket back out: Windows associates a socket with its completion
port until that socket closes. The legacy reactor release API is preserved for
legacy owners, with its original readiness behavior.

## Cross-thread notification

The following owners retain a reference to a small control block, not a pointer
to the scope's operations or buffers:

| Owner factory | ThreadSafe request | Consumer action |
| --- | --- | --- |
| scope_event_waker(scope,event_id) | event_wake | Complete that event |
| scope_operation_canceller(scope,id) | operation_cancel | Cancel that operation |
| scope_canceller(scope) | scope_request_cancel | Cancel pending group work |

Group cancellation is one-shot: it cancels work pending when the command is
consumed. It neither closes the group nor latches cancellation for later work.

Creation validates operation identity and kind. Check the corresponding
`*_error` result. Each owner needs an explicit move on close, and must outlive
every concurrent call using it. Closing the same token while another thread is
using that token is outside the contract. Tokens may safely outlive their scope:
after scope close a request returns cancelled (9), without accessing freed scope
storage or waking a reused kernel object.

Request success means admission to the bounded queue. `count=1` is a new queued
command; `count=0` coalesces an identical queued command. Queue exhaustion returns
limit (7) without replacing earlier commands. The caller chooses retry, shedding
or a larger explicit queue budget. Stale commands are discarded by the consumer;
they cannot signal a reused operation slot. An event command keeps its first
admission timestamp, so a signal admitted before the event deadline succeeds even
if its owner runs later. Admission at or after the deadline yields timeout when
consumed. Already terminal events are unchanged.

POSIX uses a nonblocking close-on-exec pipe registered with epoll/kqueue/poll.
Windows posts a completion notification with PostQueuedCompletionStatus. At most
one unread kernel wake notification is retained per scope, even when the owner
drives commands without entering a kernel wait. Partial command drains rearm
notification so that remaining commands cannot lose their wakeup.
The queue uses a short atomic critical section; no wait-free/lock-free or
NoBlocking guarantee is claimed. The owner drains at most wake_limit commands
per turn. Control tables, kernel scratch space and explicitly created token
owners are separate from the payload byte budget. There is no hidden producer
thread, background queue or implicit socket retry.

## Windows IOCP lifetime

New scopes report backend 5 on Windows. Their socket operations use WSARecv,
WSASend, WSARecvFrom, WSASendTo, AcceptEx and ConnectEx with owned OVERLAPPED
storage. AcceptEx requests no initial payload. ConnectEx binds an ephemeral
local endpoint and updates its connect context. Accepted sockets update their
accept context before entering the group. Extension pointers are obtained from
the socket provider with WSAIoctl; no process-global provider pointer is assumed.

Synchronous I/O success still has one IOCP completion; the driver consumes it
exactly once. Partial TCP transfers resubmit only the remaining owned buffer.
Zero UDP datagrams and truncated receives retain their explicit status/count.
Completion results remain reserved until acknowledgement.

Timeout/cancellation requests CancelIoEx, then dequeues the affected completion
before publishing a terminal result or reusing/freeing its buffer/OVERLAPPED.
Bytes already transferred are retained; cancellation never retracts sent bytes.
This retirement may wait for the OS/driver: no bounded-duration close or cancel
is promised. Closing the port before retiring its requests is forbidden.
ScopeStatistics.kernel_pending exposes outstanding OS requests separately from
logical pending work. No asynchronous TLS or DNS is silently routed through
plain socket operations.

Legacy NetworkReactor still identifies Windows WSAPoll as backend 3. Its old C
entry points, DLM module and release behavior remain available unchanged.

## Validation and remaining gates

The Linux acceptance corpus covers epoll and forced poll, independent TCP/UDP
peers, copied writes, partial timeout/EOF/cancel, queue limits, stale producer
tokens, many producers, late signals and retained join results. A portable native
thread host tests actual blocking wake, cancellation, signal/close races and
kernel retirement. Sanitizer runs exercise both Linux drivers. DLM tests cover
owner moves, key/token distinctions, effect admission and error-boundary cleanup.

The user subsequently confirmed seven 0.137 Windows examples and the native
thread/lifecycle host reporting backend=5. This validates the supplied IOCP smoke
scenarios. Full Windows fault/performance acceptance and FreeBSD/kqueue execution
remain open; see DLN_WINDOWS_0.137.0.json.
General CPU tasks, language async/await and asynchronous TLS remain open.
These engineering checks are not formal network proof certificates.

Primary API contracts used for the IOCP implementation:

- https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatus
- https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex
- https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nf-mswsock-acceptex
- https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nc-mswsock-lpfn_connectex
