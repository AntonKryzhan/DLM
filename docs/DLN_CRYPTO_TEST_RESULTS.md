# DLN cryptographic foundation validation — 0.138.0

Development host: Linux x86-64, Rust 1.99.1, GNU-compatible native C backend.

| Executed suite | Result |
| --- | --- |
| cargo test --workspace --all-targets --release --locked --offline | 1031 passed, 0 failed, 1 pre-existing GUI test ignored |
| dln_crypto.py | 18 passed, 0 skipped |
| dln_async.py, epoll and forced poll | 51 passed, 0 skipped |
| dln_reactor.py | 33 passed, 0 skipped |
| dln_foundation.py, including TLS/HTTP/WebSocket | 31 passed, 0 skipped |
| dln_crypto_host.c with official-header ABI checks | Passed |
| Allocator-interception check for wiping before free | Passed |
| AddressSanitizer + UndefinedBehaviorSanitizer with wipe audit | Passed, no reported memory/UB error |

The five Rust crypto tests are included in the workspace total. They execute
both new native examples, reject copied/moved owners and wrong key kinds,
require explicit unsafe raw imports and reject false NoAlloc/NoIO claims.

## Independent primitive and record checks

The native host checks RFC 5869 HKDF, RFC 4231 HMAC, RFC 7748 public/shared X25519
values and a known AES-256-GCM vector. Compiling with DLM_CRYPTO_CHECK_HEADERS
compares the locally declared GnuTLS datum size/alignment/offsets, algorithm IDs
and the decryption-failure constant against official GnuTLS/Nettle headers.
Windows default host builds need only the runtime DLLs, not these headers.

The Python suite uses cryptography/OpenSSL as a peer to GnuTLS/Nettle. Checks
include 24 deterministic independent X25519 pairs, high-bit normalization,
low-order rejection, byte-exact derived AES-GCM records, reverse directions,
empty/binary/large messages, reordered records and both 63/64 replay distances.
It verifies malformed headers/lengths, wrong AAD/context/role, reflection,
no plaintext before authentication, no replay-window advance after bad tags,
record/byte/expiry/authentication budgets and capacity failures without spending
nonce or receive state. A missing HMAC key fails instead of selecting SHA-256.
An explicit invalid library override has no system-library fallback.

The DLM shared-library fixture repeatedly fails after a partial owning-record
move. All provider owners and native DLM allocations return to zero, and crypto
and network teardown succeed after each of 16 calls. The C wipe-audit host
intercepts free and verifies zeroed owned-secret/channel memory before release.
This does not assert wiping of backend internals, caller copies or CPU registers.
ASan/UBSan uses ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 because LeakSanitizer
cannot run under tracing in this environment; no leak-scanner result is claimed.

## Compatibility and application

The 0.137 engineering snapshot preserves all 142 protected compiler/stdlib files
and all 70 prior public network C function declarations/bodies. Thirteen crypto
entry points are added. Existing 0.134/0.135/0.136 snapshots remain available.
No parser, checker, IR, proof kernel or native code generator was modified.

The source delta is validated by applying it to a clean 0.137 source copy and
comparing the resulting source, SDK-library and builder files with the intended
0.138 tree. Nine native examples and both native hosts are built/run from the
applied tree. ZIP CRC/member bytes, paths, SDK mirrors, Windows launcher CRLF
and documentation inventory are checked before delivery.

## Windows evidence and remaining platform gates

The user's 0.137 Windows console log confirmed seven DLM examples and the native
thread/lifecycle host with backend=5; see DLN_WINDOWS_0.137.0.json. The 0.138
crypto path is newly implemented and has not been compiled/executed on Windows
in this Linux environment. FreeBSD/macOS crypto execution also remains open.

Build-Network.cmd rebuilds 0.138, checks the SDK DLL paths, runs the crypto
known-answer host, nine DLM examples and the IOCP thread host. Output and failures
are recorded in the network transcript. No Windows binary or third-party runtime
library is included in the source delta. These are engineering checks, not a
formal certificate for primitives or for an application protocol composed of them.
