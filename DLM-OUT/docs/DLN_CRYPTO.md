# Cryptographic network foundation — DLM 0.138.0

`std.net_crypto` is the DLN-05 primitive/record foundation. It reuses native
ownership, Resource finalizers and the explicit `dlm.net.v1` foreign boundary.
GnuTLS >=3.6.13 supplies SHA-256, HMAC-SHA-256, HKDF-SHA-256 and AES-256-GCM;
Nettle/Hogweed supplies X25519. No cipher, hash, MAC or curve arithmetic is
implemented in DLM's adapter. Existing verified TLS 1.3 clients remain available
through std.net_socket; their certificate/hostname/pin and no-downgrade behavior
is unchanged. Crypto is a synchronous operation with explicit foreign effects;
it is not a new proof rule or an asynchronous TLS implementation.

## Install and dependencies

Apply the source delta after 0.137.0 and run DLM-pre/Build-Network.cmd. The runner
rebuilds the compiler/SDK, executes nine DLM network examples, the IOCP/thread
host and a crypto known-answer host. Do not use SkipCompilerBuild until 0.138 is
built. The PowerShell -NoExit command in the root Russian README preserves its
window. Detailed validation output is in BuildCache/network-*.log.

The fixed WinLibs SDK already contains libgnutls-30.dll and libhogweed-6.dll,
plus their libnettle/GMP and other dependencies. The builder reuses these files;
this source delta contains no third-party binaries or new download step.
The runner and development launchers set DLM_LIBGNUTLS/DLM_LIBHOGWEED to absolute
SDK paths unless the user explicitly supplied overrides. A standalone application
needs these runtime dependencies beside its executable or explicit absolute
overrides. An explicit bad override fails; it never falls back to another library.

Linux defaults are libgnutls.so.30 and libhogweed.so.6. The macOS source branch
uses the corresponding versioned dylib names, with explicit overrides available.
Windows library lookup uses safe default DLL directories, and the dependency
directory of an explicitly supplied absolute library. Relative overrides are
rejected. All expected symbols and the minimum GnuTLS version are checked before
success is advertised. Failure is unsupported (6); no substitute algorithm or
plaintext fallback is attempted. crypto_available reports the complete backend.

## Owners and key agreement

| Type | Creation | Consumption/cleanup |
| --- | --- | --- |
| CryptoPrivate | crypto_private_generate: 32 OS-CSPRNG bytes | crypto_agree consumes the private owner, or crypto_private_close |
| CryptoSecret | crypto_secret_generate, crypto_agree, crypto_hkdf | HKDF/channel construction consumes the input, or crypto_secret_close |
| CryptoChannel | crypto_channel_open / crypto_channel_limited | Owns both directional AEAD contexts; crypto_channel_close |

Check the corresponding *_error after each owner factory. Factories that consume
an input consume it on failure as well. Native move/use-after-move and borrow
checks apply. Private, secret and channel owners are distinct nominal types.
Raw imports are explicit unsafe functions. They cannot establish caller entropy
or uniqueness, and importing the same handshake secret/context twice can reuse
keys/nonces. Do not do that in a protocol. Genuine high-level owners cannot be
copied or reset; no safe secret-export API is provided. Native record layouts
are still unsealed, as in previous networking patches: forged raw FFI owners and
arbitrary foreign pointer manipulation are outside these high-level guarantees.

Safe random secret generation accepts 32..65536 bytes. HKDF returns owned secret
material of 32..8160 bytes; it consumes its parent, uses HKDF extract-and-expand
with SHA-256, and never exposes the intermediate PRK. Explicit raw secret imports
may contain 0..65536 bytes for HMAC/known-answer/foreign integrations. Channel
construction requires at least 32 bytes of input material. Byte length alone
does not prove entropy of an imported key. Salt and context/info are each bounded
to 65536 bytes. crypto_sha256/crypto_hmac write exactly 32 bytes on success and
leave output unchanged on error. The keyed HMAC entry point rejects a missing
or failed secret owner; it never substitutes an unkeyed hash for an absent key.

crypto_public exports only a 32-byte X25519 public key. crypto_agree accepts
exactly 32 peer bytes, applies the RFC 7748 high-bit rule and rejects an all-zero
shared result. Its private input is wiped/consumed on both success and error.
Raw X25519 does not authenticate a peer or prevent a man-in-the-middle attack.
Use an authenticated peer key/transcript (for example, a verified TLS channel)
when building a protocol. Identity records and authenticated protocol state
machines remain separate DLN stages; the sample exchanges public keys locally.

## Directional record profile v1

Channel construction uses separate HKDF info labels:

- DLM-CRYPTO-v1/c2s| followed by the caller's context;
- DLM-CRYPTO-v1/s2c| followed by the same context.

Each expansion yields 32 key bytes and a 12-byte base IV. Initiator and responder
select opposite send/receive directions. Bind a canonical authenticated transcript,
protocol/service and version into the context. The profile is an experimental
DLM record format, not TLS, QUIC or an authenticated handshake by itself.

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII DLC1, fixed profile/version |
| 4 | 8 | Unsigned sequence, big-endian |
| 12 | 4 | Plaintext length, big-endian |
| 16 | N | AES-256-GCM ciphertext |
| 16+N | 16 | Authentication tag |

AAD is the complete 16-byte header followed by caller AAD (0..65536 bytes).
The nonce is base_iv XOR (four zero bytes followed by the big-endian sequence).
Sequence starts at zero and advances for each encryption invocation. Insufficient
output capacity or preflight rejection does not spend a sequence. Once encryption
starts, an unsuccessful provider call burns that sequence and permanently faults
the sender; it is never retried under the same nonce. The record overhead is
32 bytes; the complete record is bounded to 64 MiB. Empty plaintext is valid.

crypto_seal writes the record only after successful completion. crypto_open
decrypts into private temporary storage, verifies the tag, then copies plaintext
to caller storage. No unauthenticated plaintext is returned, including on bad
AAD, tags, contexts or roles. The temporary plaintext is wiped on every exit.
Malformed headers/lengths are rejected before allocating payload storage.

Replay protection is a 64-record sliding bitmap. Out-of-order records within the
window may succeed once; duplicates and older records fail. The receive window
and counters advance only after authentication. A forged high sequence cannot
slide the window. The check is per channel/key epoch, not global persistent
anti-replay storage across application restarts.

## Lifetime and resource limits

Defaults are 1,048,576 records, 1 GiB of plaintext in each direction, and one hour
from channel creation. crypto_channel_limited can lower these limits; records and
bytes must be positive and cannot exceed those maxima. Lifetime is a positive
U32 number of milliseconds. Expiry is checked before starting an operation using
the local monotonic clock; it is not a synchronized peer timestamp or an
interrupt/deadline for an already running primitive. At most 64 failed tag
verifications are allowed per receiver/key, without resetting after successes.
After exhaustion use a freshly agreed key epoch, not a reset of the same secret.

CryptoStatistics exposes per-direction counters, the configured limits, local
deadline and failed-auth count. Its error reports invalid/expired/provider-fault
state; individual operations report exhausted budgets. Cipher scratch space,
fixed owner metadata and caller-owned buffers are separate from the cumulative
per-key plaintext budget. There is no hidden thread or unbounded replay set.

Owned secret bytes, PRKs, derived material and temporary plaintext are explicitly
wiped before release; backend contexts are destroyed by GnuTLS. Cleanup also
runs on DLM runtime errors and partial moves. This does not claim to erase caller
copies, CPU registers, paging/swap or OS crash dumps. Stop all foreign calls and
close all network/crypto owners before crypto_provider_shutdown; this unsafe host
operation refuses teardown while provider owners remain alive. It releases only
this adapter's library references. Same-owner calls are serialized; no ThreadSafe
guarantee is declared for channel operations or teardown.

Additional crypto error kinds, preserving previous network statuses 0..11:

| Kind | Meaning |
| --- | --- |
| 12 | Authentication or all-zero X25519 agreement failure |
| 13 | Replayed or too-old sequence |
| 14 | Local key lifetime expired |
| 15 | Record, byte, nonce/fault or authentication-attempt budget exhausted |

Use crypto_error_name and the crypto_* predicates. Backend error codes are
retained in NetworkError.system. These are runtime observations, not static
proof evidence or a certification of a composed protocol.

## Verification and platform gates

See DLN_CRYPTO_TEST_RESULTS.md. The independent Python suite uses Python's
cryptography/OpenSSL backend as a peer to the production GnuTLS/Nettle adapter.
The C known-answer host can additionally compile with DLM_CRYPTO_CHECK_HEADERS
against official headers. Windows runner uses no development-header dependency.
The user confirmed 0.137 IOCP examples and the thread host (backend 5); the newly
added Windows crypto path still requires its own native run. FreeBSD/macOS crypto
have not been executed in the development environment.

Primary contracts: https://www.gnutls.org/reference/gnutls-crypto.html,
https://www.lysator.liu.se/~nisse/nettle/nettle.html,
https://www.rfc-editor.org/rfc/rfc5869.html,
https://www.rfc-editor.org/rfc/rfc7748.html.
