# DLN foundation — DLM 0.135.0

This patch extends 0.134.0 without replacing its network APIs. It implements
substantial library work from DLN-00..03. DLN-04 reactors, NetworkSpec, protocol
synthesis, general QUIC, identity/P2P and TUN are separate outstanding stages.
The current native language/checker/VM/proof kernel are unchanged.

## Install and build

Apply after DLM_v0.134.0_NETWORK_SOURCE_patch.zip. Preserve DLM-main, DLM-pre
and DLM-OUT paths. On Windows run DLM-pre/Build-Network.cmd: it rebuilds the
compiler, refreshes the SDK and compiles/runs all four network examples.
SkipCompilerBuild requires an already rebuilt compiler >=0.135.0. This is a
source patch; a Windows executable is not included or claimed tested here.

From DLM-main on Linux:

```bash
cargo build --release --workspace --locked --offline
python3 tools/check_dln_compatibility.py
target/release/dlm build examples/native/network_typed.dlm --lib lib --allow-unsafe -o network_typed
./network_typed
target/release/dlm run examples/native/network_streaming.dlm --lib lib
```

## Module and contract map

| Module | Purpose | Execution |
| --- | --- | --- |
| std.net_bytes | Checked little/big endian, ULEB128, ZigZag | Native VM and compiled native |
| std.net_codec | Constant-state U32 big-endian framing | Native VM and compiled native |
| std.net_error | NetworkError, NetworkTransfer and predicates | Native VM and compiled native |
| std.net_socket | Distinct owning socket types, addresses/options | Compiled native, explicit FFI admission |
| std.native_net / std.native_protocol | Existing 0.134 APIs | Preserved |

The project manifest explicitly admits std.native_net and std.net_socket roots.
A strict trust policy still rejects FFI even when a root is listed. None of these
libraries adds mathematical proof certificates or treats a successful test as
proof evidence. C remains the named dlm.net.v1 trust boundary.

## Distinct owners and structured errors

TcpSocket, TcpListener, UdpSocket and TlsSocket are different native record types.
Each owns the existing Resource control through its inner field. Transfers
require move; ordinary copying, zero initialization, use after close and a
second close are rejected. tcp_read cannot accept UdpSocket, tcp_accept cannot
accept TcpSocket and TCP half-close cannot accept TlsSocket.

Construct with tcp_connect/tcp_connect_address, tcp_listen, udp_bind or
tls_connect. Inspect tcp_error/listener_error/udp_error/tls_error before I/O.
A failed constructor returns a safely droppable owner with an error, not a live
connection. All remaining owners finalize at lexical exit and native error
boundaries. Explicit close takes and consumes the whole owner.

Native record representations are not sealed. Deliberately rebuilding wrappers
from inner or using raw FFI is outside the high-level type contract; it still
requires the admitted unsafe boundary. This release adds distinct API types,
not a new module-privacy or unforgeable-capability system.

NetworkError contains kind, system and operation. kind retains provider v1
statuses from NATIVE_NETWORKING.md. system retains errno/WSA/DNS/CURLcode;
operation identifies the API domain. Use network_ok, network_would_block,
network_eof, network_timed_out, network_was_cancelled, network_truncated and
network_error_name. NetworkTransfer adds the real processed count, including
partial progress on error. NetworkReady instead contains readiness events.

| Operation tag | API |
| --- | --- |
| 1 | TCP connect |
| 2 | listen |
| 3 | accept |
| 4 | UDP bind |
| 5 | TLS connect |
| 6 / 7 | read / write |
| 8 / 9 | wait / TCP shutdown-write |
| 10 / 11 / 12 | local address / peer address / DNS |
| 13 / 14 | configure / query options |
| 15 / 16 / 17 | UDP send / UDP receive / format address |

## Address families and options

SocketAddress wraps a family 4/6, port, IPv6 scope and network-order address
bytes. socket_address_v4 takes four octets and a port; socket_address_v6 takes
16 octets, port and scope. socket_address_resolve takes UTF-8/ASCII or Punycode
host bytes, port, family 0/4/6 and result index. An exhausted list is kind 2.
DNS is synchronous and has no new asynchronous cancellation guarantee.

Numeric tcp_connect_address does not call DNS. Its timeout applies to the
connection operation. Host-name tcp_connect retains the old sequential address
fallback and DNS-before-connect-budget behavior. Happy Eyeballs is not added.

New address-based tcp_listen and udp_bind explicitly set IPV6_V6ONLY: false
for dual_stack=true, true for dual_stack=false. IPv4 with dual_stack=true is
invalid. A dual-stack wildcard receiver can return IPv4-mapped IPv6 peers;
those addresses are preserved and can be used for replies. Numeric scope is
retained; socket_address_write formats it without resolving interface names.

SocketOptions uses no_delay/keep_alive as 0/1 and send/receive buffer requests
as bytes (0 leaves buffer size unchanged). tcp_configure validates every value
before changing OS options. Later OS failures may leave earlier options applied;
configuration is not transactional. tcp_options returns actual OS values, which
may be clamped or doubled. udp_configure exposes only buffer requests.

Single read/write calls remain nonblocking and may return partial progress.
Exact/all calls share one deadline and accept the existing cancellation owner.
Wait returns READ=1, WRITE=2, ERROR=4, HUP=8. No pending operation retains a
borrowed user buffer after return. IOCP/epoll/kqueue operation objects and
structured asynchronous tasks are not part of this release.

## Byte codecs

net_read_be/net_read_le and their write counterparts accept widths 1..8.
Offsets, incomplete inputs and value overflow are checked. A rejected write
leaves the destination unchanged. They retain ProtocolValue status meanings:
0 success, 1 incomplete, 4 invalid and 7 limit.

ULEB128 covers every U64 in 1..10 bytes. A tenth byte may contain only the
single remaining value bit and no continuation flag. Canonical reading rejects
unnecessary trailing zero groups; noncanonical reading is an explicit choice.
Writes always use the shortest encoding. QUIC varint remains the distinct
existing 1/2/4/8-byte format. ZigZag covers the complete I64 range, including MIN,
without signed overflow. It does not reinterpret ULEB128 as signed LEB128.

The pure byte/framing functions also declare compiler-checked BoundedWork.
ULEB128 scanning uses at most ten iterations and framing consumes at most four
header octets per call. This is the existing native operation-count contract,
not a wall-clock or external-network latency guarantee and not a network proof
certificate.

## Streaming frames without whole-message buffering

Frame32Decoder is 20 bytes of persistent state, with a caller-chosen maximum.
It stores a four-byte big-endian header, remaining length and a sticky fault.
It does not own an input buffer or allocate payload storage.

Call frame32_feed(state,input_slice,offset). Frame32Step gives consumed bytes,
a payload_offset/payload_count span into that exact input slice, remaining
payload length and a complete flag. It stops at one frame boundary; advance by
consumed to process another frame in the same chunk. An incomplete header or
an empty chunk is not EOF. Call frame32_finish only at transport EOF; it rejects
a partial header or body. Zero-length frames are supported. Invalid/forged
state is rejected, and an oversized header poisons the decoder until reset.
Reset discards state; it does not discover a safe resynchronization boundary.

To borrow payload from its actual owning Buffer, call
frame32_view(buffer,base_offset,step), where base_offset is the start of the
slice previously passed to feed. The returned view borrows that owner and
cannot outlive it or coexist with a conflicting mutation. Never retain a view
while reusing its receive buffer. Offsets do not certify that a caller-supplied
step came from this input; runtime slice bounds still apply.

The streaming example consumes a 1 MiB message through a fixed 256-byte buffer.
Applications decide where each payload portion goes and must apply their own
consumer backpressure. The codec does not implement an unbounded output queue.

## Compatibility and evidence

DLN_BASELINE_0.134.0.json freezes 136 compiler/stdlib source files and the
signatures/bodies of 39 old public network C functions. The checker permits
additive files/symbols and rejects changed baseline entries. This release adds
seven numeric-address/options/formatting C entry points. The old DLMB/TLV wire
format, raw network APIs, native checker and proof kernel are preserved.

```bash
cargo test --workspace --release --locked --offline
DLM_NETWORK_REQUIRE_CURL=1 python3 crates/dlm_core/tests/native_hosts/dln_foundation.py
python3 tools/check_dln_compatibility.py
```

The Python acceptance suite needs Python 3, a GNU-compatible C compiler and
OpenSSL CLI; the HTTP/2 case additionally needs Node. Rust tests always cover
kind/lifetime/ownership rejection and compiled loopback. See DLN_TEST_RESULTS.md
for actual platforms, skipped work and measured results.
