# DLN-08 / DLM 0.141 validation

Executed on Linux on 2026-11-21. Native Windows, FreeBSD and macOS acceptance
are separate; a Windows cross-build is not a runtime result.

| Gate | Actual result |
| --- | --- |
| Rust workspace/all targets, release/locked/offline | 1044 passed, 0 failed, 1 pre-existing GUI test ignored |
| New independent NetworkSpec suite | 23 passed, no skips |
| Existing foundation/reactor/async/crypto/QUIC/transport suites | 174 passed, no skips |
| Applied archive over 0.140 | 16 DLM examples, 5 C hosts and 3 kernel artifacts passed |
| Ordinary mathematical laws | 6 accepted by the current checker; 3 Nat equation artifacts independently replayed |
| Instrumented descriptor model/ABI host | 5000 mutations plus boundary/proof gates passed ASan/UBSan; detect_leaks=0 |
| Windows x86_64 GNU PE | Generated fixture and strict host compile/link passed; not executed |
| Compatibility | 151 protected files and 114 existing C ABI bodies unchanged; all old snapshots passed |
| Patch application | 28 changed/new files; all 1157 intended source/SDK-library/builder files reproduced |

## New checks

`crates/dlm_core/tests/native_hosts/dln_spec.py` uses the real compiled DLM model,
metadata codec and compatibility functions through an explicit unsafe test ABI.
The production modules have no foreign imports or network dependencies. Python
uses only its standard library and independently constructs DNS1 metadata.

The suite checks every field kind under both wire profiles, scalar/optional/
repeated counts and boundaries; all truncated descriptor prefixes; malformed
magic, boolean, count and memory words; exact maximal 16904-byte metadata;
canonical re-encoding, semantic tail exclusion and short-buffer preservation.
An independent interval-change-point oracle checks 400 deterministic valid
schemas, including older-version peak sizes. A further 1200 mutated/truncated
metadata inputs must never trap or leave live allocations.

Compatibility cases cover highest shared minor, disjoint windows/major/protocol,
field introduction versions, directional cardinality/kind constraints,
role/phase mismatches, positional count-prefix mismatch, unknown-field budgets
and opaque/known-field collisions. Security tests cover mandatory encryption for
sensitive fields, unsupported mutual identity/cross-session replay, unsupported
whole-process memory, mandatory-property contradictions and optional preferences.
Pending proof masks cannot be cleared; proof-required mode refuses admission.
Session deadline/in-flight/message limits are intersected without widening.

Five Rust tests execute pure schema/metadata and version examples in both VM and
compiled native mode, check the preflight functions' NoAlloc/NoBlocking/NoIO/
BoundedWork contracts, run a real admitted TCP session with the unchanged DLMB
payload, and independently replay all three supplied arithmetic proof artifacts.
The six ordinary arithmetic/interval theorems also pass the existing checker.
These arithmetic facts do not discharge native machine/protocol obligations.

The C ABI host exercises the actual compiler-emitted fixture: independent DNS1
vector, all short prefixes, canonical re-encoding, proof/security gates,
negotiation, 5000 deterministic byte mutations and bad in-memory models. It
checks no recorded runtime error and zero live allocations. Host code compiles
with `-Wall -Wextra -Werror`; generated DLM C uses the standard compiler path.
Existing generated-C const/volatile qualifier diagnostics are not suppressed;
this is not a claim that the entire generated C file is warning-free under
-Werror. The host plus generated model passes ASan/UBSan with
`ASAN_OPTIONS=detect_leaks=0`; no LeakSanitizer result is claimed.

Windows x86_64 GNU PE host and generated fixture compile/link with Zig/Clang.
The handwritten host remains strict with -Werror and assertions enabled. They
are not executed in this Linux workspace. Build-Network.ps1 now includes all
16 DLM examples, five C hosts, six arithmetic laws and three kernel artifacts; the new host links the actual DLM static ABI
fixture and has no Python dependency.

## Compatibility

All prior 0.134..0.139 snapshots are retained. The new 0.140 snapshot protects
151 source/stdlib/provider files, including the entire old network.c, and all
114 old public C ABI bodies/declarations. The patch introduces zero new native
network C symbols. Existing compiler, proof kernel, socket/crypto/QUIC and
transport implementation source is byte-identical to 0.140.

## Reproduction

From DLM-main with the project toolchain and a native C compiler:

```sh
cargo build --workspace --release --locked --offline
cargo test --workspace --all-targets --release --locked --offline
python3 crates/dlm_core/tests/native_hosts/dln_spec.py
python3 tools/check_dln_compatibility.py --snapshot docs/DLN_BASELINE_0.140.0.json

target/release/dlm build crates/dlm_core/tests/native_hosts/dln_spec.dlm \
  --lib lib --allow-unsafe --target staticlib -o /absolute/test-dir/dln_spec.a
cc -std=gnu11 -O1 -Wall -Wextra -Werror \
  crates/dlm_core/tests/native_hosts/dln_spec_host.c -I/absolute/test-dir \
  /absolute/test-dir/dln_spec.a -pthread -lm -ldl -o /absolute/test-dir/spec-host
/absolute/test-dir/spec-host

target/release/dlm proof-check examples/native/fixtures/network-spec-repeated.kproof
target/release/dlm proof-check examples/native/fixtures/network-spec-zero.kproof
target/release/dlm proof-check examples/native/fixtures/network-spec-padding.kproof
```

For sanitizers, compile the emitted `dln_spec.a.c` and C host together with
`-fsanitize=address,undefined -fno-omit-frame-pointer`. This instruments the actual
DLM model and metadata implementation without loading an instrumented module
into Python. Network/transport peer tests retain their existing dependency setup.

For Windows, apply over 0.140 and run the full Build-Network.ps1 command with
PowerShell -NoExit. The new semantic model does not replace unexecuted native
platform gates or certify a protocol implementation.
