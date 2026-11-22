# DLN-06 / DLM 0.139.0 validation

Executed in the restored Linux workspace on 2026-11-20. No native Windows,
FreeBSD or macOS runtime result is implied by cross-compilation.

| Gate | Actual result |
| --- | --- |
| Rust workspace/all targets, release, locked, offline | 1034 passed, 0 failed, 1 pre-existing GUI ignored |
| Independent aioquic 1.8.0 peers | 19 passed, no skips |
| Existing scoped operations | 51 passed |
| Existing reactor | 33 passed |
| Existing foundation/codecs | 31 passed |
| Existing crypto RFC/OpenSSL peers and owner cleanup | 18 passed |
| Native C QUIC lifecycle host | Passed 12 full connect/stream/DATAGRAM/close/forget cycles |
| ASan + UBSan provider and C host | Passed; detect_leaks=0 (ptrace environment; no LeakSanitizer claim) |
| Compiled DLM loopback sample | Passed verified TLS, bidi/uni, binary/empty FIN, DATAGRAM and graceful close |
| Windows x86_64 PE provider + C host | Compiled/linked with Zig/Clang GNU target and original SDK GnuTLS exports; not executed |
| Old compatibility snapshots 0.134..0.138 | Passed; latest protects 143 source files and 83 C ABI declarations/bodies |
| Cumulative ZIP applied to 0.137 and 0.138 | 164 changed/new files; 1118 source/SDK-library/builder files byte-identical to intended tree |
| Execution from the applied 0.137 tree | Provider rebuilt, all 10 DLM examples and all 3 native C hosts passed |

The 19 peer/ownership tests cover both DLM-client/aioquic-server and
DLM-server/aioquic-client, IPv4/IPv6, independent uni/bidi streams, binary payloads,
empty FIN, DATAGRAM negotiation/exchange, retained receive credit, bounded queues,
13% deterministic packet dropping in both roles, reordering and duplicates,
connection-ID migration to a new UDP port, stream reset isolation, reset final-size
connection credit after deliberately dropped data, QUIC v2, stateless version
negotiation, certificate identity/untrusted CA/ALPN rejection, cross-endpoint token
rejection, graceful application close/retirement/forget, invalid configuration,
explicit loader override failure, cross-thread atomic cancellation and 16 partial
move/assertion-failure cleanup cycles in a compiled DLM shared library.

Reno/CUBIC implementation is upstream ngtcp2. Tests demonstrate interoperability
and recovery in the exercised profiles; they are not a fairness/throughput or
exhaustive adversarial-network benchmark. GnuTLS handles certificate validity
and cryptography; this suite does not prove the dependency or protocol secure.
The scope of each allocator excludes GnuTLS and OS internals.

## Reproduction on POSIX

From DLM-main, using the project's Rust toolchain and installed GnuTLS dev linker:

```sh
cargo test --workspace --all-targets --release --locked --offline
python3 native/quic/build.py /absolute/path/libdlm-quic-1.so
export DLM_LIBQUIC=/absolute/path/libdlm-quic-1.so
export DLM_QUIC_PREBUILT="$DLM_LIBQUIC"
python3 crates/dlm_core/tests/native_hosts/dln_quic.py
python3 crates/dlm_core/tests/native_hosts/dln_async.py
python3 crates/dlm_core/tests/native_hosts/dln_reactor.py
python3 crates/dlm_core/tests/native_hosts/dln_foundation.py
python3 crates/dlm_core/tests/native_hosts/dln_crypto.py
dlm build examples/native/network_quic.dlm --lib lib --allow-unsafe -o quic
./quic
python3 tools/check_dln_compatibility.py --snapshot docs/DLN_BASELINE_0.138.0.json
```

The independent QUIC peer requires aioquic==1.8.0; the crypto peer requires Python
cryptography. Neither is a production runtime dependency. The native C host:

```sh
cc -std=gnu11 -O1 -Wall -Wextra -Werror -Wno-misleading-indentation \
  crates/dlm_core/tests/native_hosts/dln_quic_host.c -ldl -o quic-host
./quic-host examples/native/fixtures/quic-test-ca.pem \
  examples/native/fixtures/quic-test-server.pem examples/native/fixtures/quic-test-key.pem
```

For sanitizers, build both provider and host with address/undefined sanitizers and
-fno-omit-frame-pointer; set DLM_LIBQUIC to that provider. The executed host used
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1. It checked lifecycle and bounds rather
than claiming a separate leak-detector result.

## Windows acceptance

Build-Network.ps1 requires 0.139, builds the pinned provider in the staged SDK,
then executes crypto known-answer checks, all 10 DLM networking examples, the
prior IOCP/thread/lifecycle host and the new QUIC lifecycle host. It preserves
its transcript and fails on a missing dependency, sample failure or C-host error.
Run without -SkipCompilerBuild on first upgrade and keep -NoExit on PowerShell.

The older user-confirmed 0.137 IOCP result remains recorded in
DLN_WINDOWS_0.137.0.json. It is not a new QUIC or 0.138 crypto Windows result.
Full platform, impairment/fault/fuzz, multi-interface and performance acceptance
remains open, as does the independent formal certificate stage of the roadmap.
