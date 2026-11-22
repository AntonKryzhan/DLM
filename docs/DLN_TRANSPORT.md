# DLN-07 / DLM 0.140: transport requirements and message sessions

`std.net_transport_policy` selects a protocol profile from declared properties.
It has no foreign calls, allocation or I/O and works in the VM and compiled code.
`std.net_transport` opens or adopts an owning session, then supplies the same
send/poll/peek/copy/ack/receive interface over all six profiles. The compiler,
proof kernel, old socket/reactor/async/crypto/QUIC interfaces and their contracts
are unchanged. This is the DLN-07 implementation profile; full platform release
acceptance and formal NetworkSpec certificates remain later milestones.

## Profiles and requirements

| Profile | Implementation | Mandatory properties it can satisfy | Availability bit |
| --- | --- | --- | --- |
| 1 | TCP + DLT1 framing | Reliable, ordered messages, congestion control, bounded adapter storage | 1 |
| 2 | Verified TLS 1.3 + DLT1 framing | Profile 1 + secure transport | 2 |
| 3 | Peer-pinned UDP + one DLT1 frame per datagram | Unordered messages, bounded adapter storage | 4 |
| 4 | QUIC + one unidirectional output stream per logical channel/direction | Reliable, secure, ordered messages, independent channels, congestion control, bounded controlled storage | 8 |
| 5 | QUIC + one unidirectional stream with FIN per message | Reliable, secure, unordered messages, independent channels, congestion control, bounded controlled storage | 16 |
| 6 | QUIC DATAGRAM + one DLT1 frame | Secure, unordered messages, congestion control, bounded controlled storage | 32 |

| Requirement flag | Value | Meaning in this profile |
| --- | --- | --- |
| `transport_reliable()` | 1 | Uses TCP or reliable QUIC stream recovery; connection failure remains observable |
| `transport_ordered()` | 2 | Preserves message order within each logical channel, independently in each direction |
| `transport_secure()` | 4 | Verified TLS 1.3/QUIC with server certificate identity checking; not mutual application identity |
| `transport_streams()` | 8 | Independent logical channels mapped to native QUIC streams |
| `transport_messages()` | 16 | Preserves complete binary message boundaries, including empty messages |
| `transport_congestion_control()` | 32 | OS TCP or upstream ngtcp2 congestion control |
| `transport_unordered()` | 64 | API permits arbitrary message delivery order; FIFO arrival storage does not promise reordering |
| `transport_bounded_storage()` | 128 | Explicit quota on adapter heap and, for QUIC, its controlled allocator |
| `transport_total_memory_bound()` | 256 | Whole-process/dependency/OS bound; unsupported by every current profile |

The profile masks are respectively 179, 183, 208, 191, 253, 244. A requirement
can contain `required`, `forbidden` and `preferred` flags. Unknown flags,
required/forbidden or preferred/forbidden overlap, and simultaneously required
ordered/unordered delivery are invalid (status 4). Missing required properties
or forbidden capabilities reject a candidate (status 6); preferences never
relax these tests. Preferences are the number of missing requested flags; then
lower caller-supplied cost wins; profile ID breaks a final tie. Selection examines
six candidates with a bounded loop. Default costs are TCP 10, TLS 30, UDP 1,
QUIC streams 20 and QUIC DATAGRAM 5. They are hints, not measured RTT or a claim
that one transport is faster. Measurement/adaptation belongs to DLN-15.

`transport_available()` reports local backend support only. Intersect it with
profiles explicitly offered by the remote application before constructing
`transport_candidates(mask)`. This API does not discover a server's protocol,
probe unrelated services or automatically replace a failed secure connection
with an insecure one. The C opener repeats mandatory validation before network
I/O, so a modified `TransportChoice` cannot bypass hard requirements. Changing
the requirement object itself changes the caller's policy.

Example of selection without I/O:

```dlm
module example.transport.policy
import std.net_transport_policy
native {
    fn main() -> Unit guarantees(NoAlloc,NoBlocking,NoIO) {
        let bounds: TransportBounds=TransportBounds(1024,8,8,8388608);
        let req: TransportRequirements=TransportRequirements(23,0,8);
        let choice: TransportChoice=transport_select(req,bounds,transport_candidates(63));
        assert(choice.status==0 && choice.profile==4);
    }
}
```

23 requests reliable, ordered, secure messages; preferred flag 8 favors channels.
In real use, 63 must come from the local/remote intersection. See executable
`examples/native/network_transport*.dlm` for establishment and data exchange.

## Ownership and establishment

A `TransportSession` owns one consumed backend and all message storage. It is
moved, borrowed or closed using the existing `Resource` rules. Adoption consumes
the input owner on success **and on failure**; it never leaves two owners of one
socket. `transport_adopt_tcp`, `transport_adopt_tls`, `transport_adopt_udp` take
their corresponding typed socket; `transport_adopt_quic` consumes a ready QUIC
endpoint containing exactly one connection, plus its connection ID and role.
Its backend quota must be at most half of the session memory budget. Use
`transport_quic_config(bounds)` to construct a matching server endpoint.

The high-level QUIC opener and `transport_alpn(profile)` use distinct protocol
identifiers: `dlm-transport/1/ordered`, `/unordered`, `/datagram`. A manually
adopted QUIC endpoint must already use the matching ALPN/profile and truthful
client/server role. An arbitrary raw endpoint cannot be upgraded into a verified
application protocol by relabeling it. Native record representations remain
unsealed, as in DLN-02; raw FFI, forged handles/ownership and forged foreign
admission are outside these high-level guarantees.

`transport_open` actively opens the selected profile. TCP/TLS establishment is
synchronous with its explicit timeout. TCP checks cancellation before and after
connect; TLS also uses libcurl's progress callback during establishment. QUIC
returns a connecting session; `transport_poll` drives its verified handshake.
`transport_error` describes construction; `transport_statistics` describes later
state: 0 connecting, 1 ready, 2 failed, 3 EOF. Send while connecting returns
would-block. No 0-RTT application data is accepted.

`TransportTarget` separates a numeric peer from certificate identity. TLS pins
the numeric route with libcurl CONNECT_TO while checking the URL hostname/CA/pin;
HTTP ALPN is disabled because the connection carries DLT1. It does not make an
HTTP request. QUIC verifies the identity and CA using the existing GnuTLS
provider; a nonempty public-key pin on any profile except TLS is rejected as
unsupported before opening. It is never silently ignored. TLS hostname
input is ASCII DNS or numeric IPv4/IPv6; IDNs must be supplied as ASCII labels.

A zero local family selects wildcard bind for UDP. For QUIC it selects the
concrete route with a temporary connected UDP socket and getsockname, without
sending an application datagram. An explicit concrete local address can be
supplied instead. The current TCP/TLS active opener returns unsupported for an
explicit local bind; it does not silently ignore that requirement. IPv4/IPv6
addresses and IPv6 scope use existing `SocketAddress` conventions.

Servers use existing typed listen/accept or `quic_server` establishment followed
by adoption. This patch does not introduce a TLS server or a common listener
abstraction. Authentication/authorization beyond server-certificate transport
verification remains application policy and the later identity milestone.

## Message lifecycle and backpressure

`transport_send` copies one complete payload into the bounded outgoing queue.
Its count is the payload size accepted by this adapter; it is neither a peer
acknowledgment nor a delivery receipt. Empty payloads return success/count 0.
`transport_poll` flushes outgoing frames and receives incoming messages. TCP/TLS
partial writes retain their unsent suffix. Work per pass is bounded; waits check
cancellation at most every 10 ms between adapter passes. Dependencies and the OS
may take time inside individual calls; this is not a hard real-time guarantee.
Timeout/cancellation ends that wait, preserving partial messages and queues.

`transport_peek` returns the oldest complete message ID, logical channel and size.
`transport_copy` reads a bounded suffix by ID and offset without consuming it.
`transport_ack` releases that oldest message. `transport_receive` combines copy
and acknowledgment only if the destination is large enough. A short buffer
returns status 10, copied 0, required payload size, and leaves both the buffer
and queued message unchanged. Use a larger buffer or the retained copy API.

An outgoing queue at its message limit, or insufficient outgoing storage,
returns would-block. Adapter storage after fixed metadata is divided into
separate send and receive pools, so queued sends cannot consume receive capacity.
If receive allocation is blocked by already complete messages, consume them to
resume. If incomplete frames alone exhaust the receive pool and no complete
message can be freed, the session fails with a limit error instead of waiting
indefinitely. It does not silently discard a reliable partial message. Receive storage is retained until the application consumes
it. For QUIC, a full adapter receive queue pauses backend polling to avoid an
unbounded retained event backlog; the application must drain it for network
and outgoing progress to resume. Independent streams avoid transport-level
ordering dependencies; a deliberately retained application FIFO can still stall
the session. Unreliable UDP/QUIC datagrams can be dropped by lower layers.

A reliable-stream EOF at a message boundary returns EOF; mid-frame EOF/FIN is a
sticky truncated-message failure. Bad magic/reserved bytes/channel mapping or
extra bytes after a reliable-unordered message produce protocol failure. Oversize
messages produce limit errors. Native QUIC RESET fails this message-session
profile instead of silently claiming complete reliable delivery. Already complete
queued messages remain readable after a later failure. There is no implicit
retry on a different profile. Closing/dropping a session synchronously releases
storage and aborts its backend; there is no new graceful delivery barrier.
Use the raw QUIC interface when explicit QUIC disconnect/retirement is required.

## Channels, wire format and limits

Channel 0 exists on every profile; TCP/TLS/UDP/QUIC DATAGRAM support only that
channel. QUIC stream profiles can open more with `transport_open_channel`; its
successful count is the logical ID. Client-originated IDs are odd, server IDs
even; peers may reply on an established channel. Channel metadata is retained
for the session lifetime. `bounds.channels` includes channel 0 and counts all
logical channels ever introduced; IDs are not reclaimed or reused. Reuse an
existing logical channel for subsequent messages. Ordered profiles bind one
native unidirectional input stream per logical channel; unordered profiles use a
fresh FIN-terminated stream per message and do not publish before FIN.

Every message uses this 16-byte prefix followed by its binary payload:

| Offset | Size | Encoding |
| --- | --- | --- |
| 0 | 4 | ASCII `DLT1` |
| 4 | 4 | Payload length, unsigned big endian |
| 8 | 4 | Logical channel ID, unsigned big endian |
| 12 | 4 | Reserved zero |

This is a new opt-in application wire protocol. Both peers must implement it;
existing arbitrary TCP/UDP applications are not automatically compatible.
There is no proof/identity/schema metadata hidden in the payload.

`TransportBounds` declares maximum payload (0..16 MiB), queued messages per
outgoing/incoming queue (1..4096), lifetime channels (1..256) and controlled
memory (up to 64 MiB). Minimum storage is conservatively checked as
`1024 + channels*256 + 2*(max_message+128)`. This reserves fixed metadata and at
least one maximum-size message in each direction, not the whole requested queue
capacity. Each direction receives half of the remaining adapter quota. Allocator pressure
can therefore return would-block before the count limit; irrecoverable partial
receive pressure is an explicit limit failure, as described above.
UDP payload maximum is 65491 after the DLT1 prefix; QUIC DATAGRAM maximum is
1084 under the existing 1100-byte provider DATAGRAM cap.

QUIC requires at least 2 MiB total controlled budget, half reserved to its
provider, and `max_message+16 <= memory/8`. The adapter's conservative reserve
must fit in the other half. This bounds new adapter allocations plus the QUIC
custom allocator. It excludes caller Buffers, C stacks, TLS/GnuTLS/libcurl
internals, runtime module loading and kernel/socket memory. The unsupported
whole-memory flag makes this distinction testable; no profile claims a hard
whole-process RAM cap. Queued send data may already have moved to the backend;
`send_queued` is the adapter queue count, not outstanding peer delivery.

## Validation and continuation

See `DLN_TRANSPORT_TEST_RESULTS.md` for executed tests and platform boundaries.
`Build-Network.ps1` now checks DLM 0.140, all 13 networking examples and four C
hosts with warnings treated as errors. It includes the previous FARPROC loader
repair; no warning suppression or ABI downgrade is added. Rebuild the compiler
because network provider C is embedded at compiler build time.

DLN-08 is next: NetworkSpec schemas, contracts, bounds and explicit proof
obligations using existing DLM structures/theories before syntax changes. The
current requirement filter and tests are engineering checks, not a network
proof certificate or a claim of full RFC 9622 conformance.
