# DLN-07 / DLM 0.140 validation

Executed on Linux on 2026-11-21. Native Windows/FreeBSD/macOS execution is a
separate acceptance gate; cross-compilation is reported separately.

| Gate | Executed result |
| --- | --- |
| Rust workspace/all targets, release/locked/offline | 1039 passed, 0 failed, 1 pre-existing GUI test ignored |
| Final transport/audit/corpus Rust rerun after quota changes | Passed |
| New independent message/policy/owner suite | 22 passed, no skips |
| Old foundation/reactor/async/crypto/QUIC peer suites | 152 passed, no skips |
| Applied patch over 0.139 | 13 DLM examples and 4 C hosts passed |
| ASan + UBSan | New C fault host and compiled DLM three-profile QUIC sample passed; detect_leaks=0 |
| Windows x86_64 GNU PE | Strict C provider/host compile and link passed; not executed |
| Archive compatibility | 24 changed/new files; applying over original or loader-fixed 0.139 reproduces all 1135 intended source/SDK-library/builder files |

## Message and policy checks

The new `crates/dlm_core/tests/native_hosts/dln_transport.py` suite runs 22 tests
with no skips. Its compiled DLM selector is compared to an independent Python
set/sort oracle for 49,152 combinations of required/forbidden/preferred flags
and availability masks, plus payload/quota boundary cases. Contradictions are
rejected, hard constraints precede preferences/cost, unsupported whole-process
memory requirements fail, and the pure policy example agrees in VM/native code.

Independent Python socket/SSL and aioquic 1.8.0 peers exercise all six profiles:

- TCP one-byte fragmentation, empty/binary and coalesced frames, retained IDs,
  offset copies, short destination preservation, 2 MiB partial-write queue ownership under a 4 KiB send-buffer limit,
  bounded storage/count backpressure and cancellation during an active wait.
- EOF between frames versus EOF inside a payload; invalid magic, reserved bytes,
  unsupported channel IDs and declared oversize rejection; sticky error status.
- UDP pinned-peer filtering, empty/binary datagrams and independent DLT1 decoding.
- TLS numeric connect route with verified DNS identity, raw message exchange,
  disabled HTTP ALPN, wrong identity/untrusted CA/wrong pin/TLS 1.2 rejection.
- Both QUIC roles, ordered/unordered/DATAGRAM message exchange, empty/binary
  payloads, independent channels and replies, partial stream isolation,
  unordered FIN publication and multi-message stream rejection.
- Large 16 KiB coalesced ordered frames resumed after retained receive queue
  pressure; stream RESET/truncated FIN/unexpected bidirectional stream failure;
  bounded retained QUIC queues and lifetime channel limits; explicit limit failure
  when partial frames exhaust receive storage without a consumable message.
- Common QUIC client establishment with a wrong identity fails the selected
  profile without fallback. The compiled DLM QUIC sample separately executes
  common client establishment and server adoption for all three valid profiles.
- Failed adoption consumes the old owner; compiled DLM assertion unwinding
  frees partially moved sessions and copied queued payloads (24 repetitions).
  An invalid QUIC profile consumes its typed endpoint safely (16 repetitions).

The new C host `dln_transport_host.c` executes 24 cycles of TCP framing and
retention, active-wait cancellation/timeout, normal EOF, five malformed/truncated
wire cases, storage quota pressure and failed-adoption ownership consumption.
Each cycle checks the native live-owner counter returns to zero. It also passes
ASan + UBSan with `ASAN_OPTIONS=detect_leaks=0`; this is not a LeakSanitizer result.

Five new Rust tests check pure/no-provider VM/native execution, compiled TCP/UDP
exchange, QUIC compilation, move/kind/borrow/fabrication rejection and effect
contracts. `network_transport_quic.dlm` also runs all three profiles with the
pinned ngtcp2 provider, empty/binary messages and logical-channel echo.

## Compatibility and old regressions

The existing Python suites pass: foundation 31, reactor 33, async 51, crypto 18,
and QUIC interoperability/owner tests 19. Existing 0.134..0.138 compatibility
snapshots pass. The new 0.139 snapshot protects 148 source/stdlib/provider files
and 101 old public C ABI declarations/bodies; it admits 13 new transport symbols.
The previous one-file Windows FARPROC repair is carried into this source patch.
No old public C function body, proof checker, parser or lowering semantics changes.

## Reproduction

From DLM-main, with the project Rust toolchain, GNU C compiler, GnuTLS dev
linker and the existing peer-test dependencies installed:

```sh
cargo build --workspace --release --locked --offline
cargo test --workspace --all-targets --release --locked --offline
python3 native/quic/build.py /absolute/path/libdlm-quic-1.so
export DLM_LIBQUIC=/absolute/path/libdlm-quic-1.so
export DLM_QUIC_PREBUILT="$DLM_LIBQUIC"
python3 crates/dlm_core/tests/native_hosts/dln_transport.py
python3 crates/dlm_core/tests/native_hosts/dln_quic.py
python3 crates/dlm_core/tests/native_hosts/dln_foundation.py
python3 crates/dlm_core/tests/native_hosts/dln_reactor.py
python3 crates/dlm_core/tests/native_hosts/dln_async.py
python3 crates/dlm_core/tests/native_hosts/dln_crypto.py
python3 tools/check_dln_compatibility.py --snapshot docs/DLN_BASELINE_0.139.0.json
cc -std=gnu11 -O1 -Wall -Wextra -Werror -Wno-misleading-indentation \
  crates/dlm_core/tests/native_hosts/dln_transport_host.c -ldl -o transport-host
./transport-host
target/release/dlm build examples/native/network_transport_quic.dlm --lib lib --allow-unsafe -o transport-quic
./transport-quic
```

## Windows boundary

The complete C provider and new host compile and link to x86_64 Windows GNU PE
with Zig/Clang and `-Wall -Wextra -Werror`; assertions remain enabled with
`-UNDEBUG`. This checks the prior FARPROC cast failure without relaxing warnings.
It is not native MinGW runtime evidence. The updated `Build-Network.ps1` requires
0.140 and runs all 13 DLM examples plus four C hosts with strict GCC warnings.
Use a full rebuild without `-SkipCompilerBuild` and PowerShell `-NoExit`.

The user's earlier log confirms Windows 0.139 compiler/provider/SDK construction
but stops at the old crypto-host FARPROC compilation failure. Its loader repair
is included; a successful full native 0.140 run remains to be collected. The
older 0.137 IOCP acceptance is preserved as its own evidence, not relabeled.

These tests are engineering evidence for the exercised profiles. They do not
prove complete dependency security, network liveness under arbitrary faults,
whole-process memory bounds, optimal latency or full RFC 9622 conformance.
