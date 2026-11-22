# DLN roadmap status after DLM 0.142.0

Scope follows the supplied DLN-00..17 plan. DLM version numbers and DLN milestone
numbers are separate. A library feature does not close unexecuted platform gates.

| Stage | State after 0.142 | Remaining acceptance/work |
| --- | --- | --- |
| 00 Baseline and compatibility | 0.134 baseline rerun; API/source snapshot; dependency map; new regression matrix | Windows 0.137 seven-example/IOCP-thread smoke confirmed by user; execute 0.138 crypto and full Windows/FreeBSD matrix; engineering evidence is not formal proof |
| 01 Network binary foundation | Implemented library surface: endian integers, two varint formats, ZigZag, bounds, zero-copy spans and constant-memory streaming | Maintain independent corpus/fuzz tests; formal codec certificates belong to 09/11 |
| 02 Typed resources | Distinct owning TCP/UDP/listener/TLS API, structured errors/options, compile-time kind/move/borrow checks | Native representation sealing is not implemented; keep raw FFI outside high-level guarantees |
| 03 IPv4/IPv6 | Numeric connect, explicit IPv6-only/dual-stack bind, family filtering, scope/peer/address formatting | Windows/FreeBSD, IPv6-disabled and full IPv6-only environment acceptance; Happy Eyeballs remains optional future work |
| 04 Async runtime | Scoped TCP/UDP/connect/accept, bounded producer queues, cross-thread wakers, cancellation/retirement, retained-result join; epoll/poll and Windows IOCP smoke executed; kqueue source | Windows IOCP smoke confirmed by user; full fault/performance matrix and native FreeBSD acceptance; general CPU tasks, language suspend/resume and async TLS |
| 05 Crypto foundation | Owned X25519/HKDF secrets, SHA-256/HMAC, AES-256-GCM record profile, replay/nonce/key budgets; verified TLS 1.3 and OS CSPRNG reused | Native Windows/FreeBSD/macOS crypto acceptance; authenticated application handshake/identity composition remains separate; no formal crypto/protocol certificate |
| 06 General QUIC | General v1/v2 adapter: verified TLS, independent streams and DATAGRAMs, bounded ownership, Reno/CUBIC, retransmission, graceful close and explicit client migration; aioquic interoperability both roles | Windows 0.139 compiler/provider/SDK build observed, strict C-host loader issue repaired; native full Windows/FreeBSD/macOS runtime, full impairment/fuzz/performance matrix, multi-connection/cross-family migration; no formal certificate |
| 07 Transport requirements | Pure hard-property selection and six common message profiles over TCP/TLS/UDP/QUIC; bounded owned queues, retained receive and independent QUIC channels | Native platform acceptance, whole-process quotas and TLS server/common listener remain open; cost hints are not measured latency; no formal certificate |
| 08 NetworkSpec | Pure bounded schema/version/wire/session/security/resource objects, descriptor metadata, directional compatibility and explicit pending-proof admission; ordinary Nat laws reuse the current checker | Generated payload codecs added in 09; executed state machine (10), independent native protocol certificates (11) and native platform acceptance remain separate |
| 09 Codec generator | Engineering implementation: typed all-kind codecs from checked NetworkSpec, bounded pure preflight/encode, versioned plan and independent wrapper/runtime-binding check, VM/native and TCP tests | Generic runtime/machine proofs remain pending and proof-required generation refuses; native platform/fault/performance gates remain open |
| 10 State machines | Not implemented | Formal transitions, auth, timeout, cleanup and termination checking |
| 11 Network proof checker | Not implemented | Small independent certificate checker and source/code identity binding |
| 12 Identity | Not implemented | Keys, signed locator records, freshness/replay/key rotation |
| 13 Peer discovery | Not implemented | Private interoperable P2P/DHT first, bounded abuse/privacy model |
| 14 Content addressing | Not implemented | Hash/Merkle integrity, identifiers and bounded caching |
| 15 Optimizer | Not implemented | Mandatory-property filtering, measured cost choice and valid state migration |
| 16 Virtual interfaces | Not implemented | Explicit TUN/TAP/Wintun privileges and routing lifecycle |
| 17 Release acceptance | Foundation regression/interoperability expanded | Full platform/fault/fuzz/performance/network-impairment matrix |

DLN-04 now has a scoped operation lifecycle, cross-thread notification and join.
New Windows scopes identify IOCP as backend 5; legacy NetworkReactor remains
WSAPoll (3). The user confirmed seven 0.137 Windows examples and the IOCP/thread
host (DLN_WINDOWS_0.137.0.json). Full platform/fault gates remain open. CPU tasks and
language async/await remain open. HTTP/3 is not general QUIC.
New network proof certificates must be added at stages 08..11; none is claimed
by the current codec tests or FFI audit.

DLN-05 has a primitive/owned-record implementation. Raw X25519 alone does not
authenticate a peer; verified TLS or another authenticated transcript is required
by a composed protocol. The new Windows crypto path still needs native validation.

DLN-07 adds std.net_transport_policy (pure/VM) and std.net_transport (owned I/O).
Mandatory properties are checked before ranking and opening; authentication
failures cannot trigger a hidden downgrade. Memory bounds cover controlled
storage only; total dependency/OS memory is explicitly unsupported. See
DLN_TRANSPORT.md for the wire protocol and remaining establishment limitations.

DLN-08 implements the description model without new syntax or compiler/proof
rules. Structural validation, unsupported security and pending proof obligations
are distinct. Proof-required admission refuses an unproved implementation. Three
generic Nat equation artifacts replay independently; they do not certify native
protocol execution. See DLN_NETWORK_SPEC.md.

DLN-09 adds an actual generator to the CLI and a safe DLM runtime without changing
core compiler/proof rules. Typed API and all three checked wire profiles are
exercised against an independent parser/wire oracle. The standalone plan checker
checks descriptor/size/wrapper consistency against pinned dependencies; it is not
a formal parser correctness certificate. Proof-required generation refuses.
Logical value storage budgets do not measure allocator capacity or whole-process
RAM. Session transition execution is DLN-10; proof discharge is DLN-11. See
DLN_CODEC_GENERATOR.md and DLN_CODEC_TEST_RESULTS.md.
