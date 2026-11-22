# General QUIC — DLM 0.139.0 / DLN-06

`std.net_quic` exposes arbitrary QUIC streams and RFC 9221 DATAGRAMs. The adapter
uses pinned ngtcp2 1.26.0 source and dynamically linked GnuTLS >=3.7.5. It does
not interpret streams as HTTP/3 and does not implement new cryptographic math.
The native provider remains an explicitly admitted foreign trust boundary.

## Build and installation

This source patch is cumulative from DLM 0.137.0; it includes the 0.138 crypto
prerequisites. Extract over the directory containing DLM-main and DLM-pre. Run:

```powershell
powershell.exe -NoLogo -NoProfile -NoExit -ExecutionPolicy Bypass -File "M:\users\Ark\Desktop\DLM\DLM-pre\Build-Network.ps1"
```

Do not use `-SkipCompilerBuild` for the first upgrade. Build-DLM invokes
Build-Quic inside the staged SDK directory before publishing it. That script
checks hashes in vendor/DLN_QUIC_SOURCES.json, builds the C sources with the
existing SDK GCC, and links the existing libgnutls-30.dll. Python, CMake, Rust
packages and another QUIC DLL download are not needed for the Windows build.
Existing SDK output/projects retain the previous builder's backup behavior.

The SDK puts the provider at bin/dlm-quic-1.dll. Launchers set DLM_LIBQUIC to its
absolute path. For an independently deployed executable, ship that DLL and its
GnuTLS runtime dependencies beside the executable, or set DLM_LIBQUIC to an
absolute provider path. An invalid explicit override fails; it does not fall back.
Only ABI 1 with the pinned ngtcp2 version is accepted. The module is cached until
explicit unsafe, quiescent provider shutdown with no live network resources.

On POSIX, a GNU-compatible C compiler and GnuTLS development linker are needed:

```sh
python3 native/quic/build.py /absolute/path/libdlm-quic-1.so
export DLM_LIBQUIC=/absolute/path/libdlm-quic-1.so
dlm build examples/native/network_quic.dlm --lib lib --allow-unsafe -o quic
./quic
```

Run the sample from DLM-main (or an SDK root containing examples). Its public
TEST ONLY PEM credentials live in examples/native/fixtures; neither the module
nor provider uses them by default. They are unsuitable for deployed services.

## Security and identity

Every connection uses TLS 1.3. A client supplies the expected hostname or IP
identity separately from the numeric destination address. GnuTLS automatically
verifies certificates during the handshake; the completion callback checks the
chain/identity again and checks the exact negotiated ALPN. No application API
can disable this verification. Empty trust-file selects the system trust store;
an explicit file selects that trust bundle. Server certificate and key are
mandatory explicit PEM files. Files are capped at 4 MiB; UTF-8 Windows paths use
wide file opens. No PEM key password callback is provided.

There is no 0-RTT, ticket persistence or resumption API. Endpoint recovery after
loss/path change retains the established QUIC connection; reconnecting after a
lost connection requires a new authenticated handshake. The server does not
authenticate a client certificate in this stage; application identity is DLN-12.
No fallback to plaintext, certificate-bypass switch or key logging is present.

## Ownership and progress

QuicEndpoint owns its UDP socket(s), credentials, connections, stream queues and
retained events. It is linear and is driven by one thread/caller. There are no
hidden worker threads. All calls on the endpoint, including statistics, must be
serialized. The cancel token alone may be signalled concurrently through the
existing atomic network_cancel_signal operation.

quic_poll drives input, output, retransmission, expiry, handshake deadlines and
path validation. A zero timeout is nonblocking. Cancellation interrupts the
poll without closing the endpoint; waits check the flag at most every 10 ms
plus OS scheduling. No progress occurs while the application stops polling.
A successful poll can report zero received packets when a retained event exists.

QuicConnection and QuicStream are value tokens checked against their endpoint.
Connection IDs are process-unique until the provider is quiescently unloaded,
never wrap/reuse during a provider lifetime, and prevent another endpoint's
connection token from aliasing an owned connection. These public record
representations are not sealed; arbitrary unsafe raw C handles are outside the
high-level contracts, as in the earlier networking modules.

quic_open_stream selects bidi or local uni streams. quic_write copies the whole
accepted message into owned storage; count means queued application bytes, not
peer delivery. Queue saturation returns would_block; allocator exhaustion returns
limit. Bytes remain stable until ngtcp2 reports acknowledgment or stream closure.
FIN is explicit, including an empty FIN. Round-robin scheduling lets a blocked
stream yield to other streams. Both receive credit and sending congestion/loss
recovery are managed through ngtcp2; Reno (1) and CUBIC (2) are selectable.

quic_next retains the oldest event until quic_ack. quic_copy supports partial
reads by offset, writes only its returned count, and preserves output on invalid
arguments. Acknowledging a data event frees its storage and returns receive
credit; failing to consume it applies transport backpressure. RESET's missing
final-size bytes return connection credit exactly once when that reset is acked.

| Event kind | Meaning | Payload/flags |
| --- | --- | --- |
| 1 | Authenticated handshake completed | stream.id=-1, connection identifies the peer |
| 2 | Peer stream opened | stream token |
| 3 | Ordered stream bytes | offset, count, flags bit 0 = FIN; count may be zero |
| 4 | Peer reset | offset = final size, code = application error |
| 5 | Unreliable DATAGRAM | count bytes; no retransmission guarantee |
| 6 | Connection retired | code = QUIC/application close code; inspect statistics error |
| 7 | Path validation outcome | flags bit 0 = success |
| 8 | Stream closed | stream/code; low-level closure flags |

## Bounds and failure behavior

| Config field | Accepted values / meaning |
| --- | --- |
| connections | 1..128, includes handshaking and retained closed connections |
| streams | 4..1024 active records per connection; half reserved for local streams, each peer direction advertises floor(streams/4) |
| events | 4..65536 retained events; also each connection's outgoing DATAGRAM count bound |
| idle_ms | Positive U32 timeout |
| handshake_ms | Positive U32, at most idle_ms |
| congestion | 1 Reno / 2 CUBIC |
| memory | 1..256 MiB application + ngtcp2 allocation budget, including allocation headers |
| window | 1 KiB..16 MiB, at most memory/4; connection/stream receive window and per-stream queued send-byte cap |

GnuTLS, OS socket storage and stacks are outside the explicit allocator budget.
Reallocation conservatively reserves both old and new storage before freeing.
An exhausted reliable event queue faults/closes the connection visibly instead
of discarding acknowledged stream bytes. An exhausted DATAGRAM receive queue
can drop that unreliable message. Statistics keep the terminal error even when
the event queue has no room; connection-retired events are retried as space opens.

DATAGRAM payloads are capped at 1100 bytes, and require peer negotiation. Packets
are capped at 1200 transmitted UDP payload bytes; PMTU discovery is disabled in
this profile. Bind a specific IPv4/IPv6 interface address (wildcard bind is
rejected). IPv6 sockets are IPv6-only; use separate endpoints for each family.
Numeric destinations do not trigger implicit DNS. Existing socket resolution
can supply addresses. Automatic Happy Eyeballs is not implemented.

quic_disconnect starts graceful application close. Keep polling and consuming
events through closing/draining; state 5 means the three-PTO retirement interval
has elapsed. quic_forget then releases connection storage after all its events
have been acknowledged. Dropping an endpoint aborts all work synchronously and
closes both old/new migration sockets; no kernel asynchronous buffer is retained.
quic_reset stops both stream directions and rejects later writes on that stream.

| Connection state | Meaning |
| --- | --- |
| 1 | Handshaking |
| 2 | Ready |
| 3 | Closing |
| 4 | Draining after peer close |
| 5 | Retired, eligible for forget after event acknowledgment |

Client migration is explicit: one active connection on the endpoint, a new
specific bind address and quic_migrate. The old socket remains alive while
ngtcp2 validates the new path, and is closed only on successful validation;
failure discards the candidate socket. A peer can disable migration or exhaust
spare connection IDs. Multi-connection endpoint migration, cross-family migration
and server-initiated migration are outside this profile. A server accepts peer
path rebinding using normal QUIC connection-ID routing and path validation.

## Standards, dependencies and open acceptance

The provider supports QUIC v1 and v2, including stateless version negotiation,
TLS integration, stream flow control, congestion control and loss recovery via
upstream ngtcp2. No new proof-kernel/parser/checker/IR rule is introduced.
Engineering tests and a foreign API audit do not constitute a formal certificate.
Native Windows and FreeBSD/macOS runtime/fault/performance acceptance remains
open. The Windows PE cross-build is compilation evidence only. See
DLN_QUIC_TEST_RESULTS.md for executed tests and DLN_STATUS.md for later stages.

Primary references:
- https://www.rfc-editor.org/rfc/rfc9000
- https://www.rfc-editor.org/rfc/rfc9001
- https://www.rfc-editor.org/rfc/rfc9002
- https://www.rfc-editor.org/rfc/rfc9221
- https://www.rfc-editor.org/rfc/rfc9369
- https://nghttp2.org/ngtcp2/programmers-guide.html
- https://github.com/ngtcp2/ngtcp2/tree/v1.26.0
- https://www.gnutls.org/manual/gnutls.html
- https://sourceware.org/binutils/docs/ld/WIN32.html
