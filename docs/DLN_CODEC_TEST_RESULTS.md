# DLN-09 / DLM 0.142 validation

Executed on Linux on 2026-11-21. Windows PE compile/link is cross-compilation;
no native Windows, FreeBSD or macOS runtime acceptance is claimed.

| Gate | Actual result |
| --- | --- |
| Rust workspace/all targets, release/locked/offline | 1057 passed, 0 failed, 1 pre-existing GUI test ignored |
| New compiler/codec Rust coverage | 8 CLI integration tests and 5 core integration tests passed within that total |
| Existing foundation/reactor/async/crypto/QUIC/transport/NetworkSpec Python suites | 197 passed, no skips |
| New independent payload suite | 20 passed, no skips; 660 random valid values and 2100 mutated packets included |
| Independent wrapper/plan checker adversarial suite | 10 passed |
| Applied delta over the complete 0.141 source tree | 19 DLM examples, 6 C hosts, 3 generated bundles and independent checks, six arithmetic laws and 3 existing kernel artifacts passed |
| Maximal NetworkSpec metadata | Exact 16904-byte descriptor, 16 messages/512 fields generated, independently checked and message 16 roundtripped natively |
| Maximum repetition | Generated typed batch of 4096 U32 values passed native encode/decode/equality and last-value check |
| Instrumented generated DLM implementation plus handwritten C host | All three profiles, 15000 mutations, limits and owner-abort cleanup passed ASan/UBSan; detect_leaks=0 |
| Windows x86_64 GNU PE | Actual generated DLM fixture and strict C host compiled/linked; not executed |
| Compatibility | All 0.134..0.141 snapshots passed; 155 protected 0.141 files and all 114 existing C ABI declarations/bodies preserved |
| Format and packaging | Source/SDK runtime mirrors match; modified pre-existing text retains BOM/newline style; patch contains changed/new files only |

## What the new tests execute

`crates/dlm_core/tests/native_hosts/dln_codegen.py` builds the actual DLM fixture
and calls it through an explicitly unsafe pointer ABI. The test source is kept
separate from the unchanged foundation fixture `dln_codec.dlm`. Python's standard
library supplies an independent encoder/parser, integer/UTF-8 validation and
canonical-value oracle; it does not copy expected packets from DLM output.

The suite checks every supported kind under DLMB2 and positional big/little
endian, both optional absence and present empty values, repeated scalar/byte
fields, signed extremes, ULEB transitions/overflow/overlong forms, binary and
multibyte text, strict booleans and malformed UTF-8. It checks every truncated
prefix and every short output capacity for the supplied vectors, known-field
reordering with exact relative unknown preservation, duplicate singular fields,
missing required fields, unknown count/byte limits, oversized claimed lengths
before allocation/waiting, exact logical storage admission and one-byte-below
refusal. Failed output calls must leave every sentinel byte untouched. Valid
canonical values must survive native encode/decode and equal the independent
canonical wire representation. All calls must leave no recorded runtime error
and zero tracked live allocations. Intentional abort tests clear their expected
error after checking cleanup of partially moved owners.

`tools/test_network_codec_checker.py` tests all three checked bundles and rejects
changed source bodies even with recomputed plan checksums, changed size tables,
forged proof masks/assurance, malformed descriptor binding, runtime/dependency
edits, duplicate/missing dependency entries, executable changes hidden beside
comments, invalid namespace/version, excess input sizes and unterminated comments.
This checker does not invoke the generator or DLM VM. Its positive result is a
structural/binding result against pinned runtime files, not a correctness theorem.

Eight CLI tests check factory/DNS1 deterministic equivalence, version-activated
and retired typed fields, reproduction and intentional regeneration, preservation
of edited outputs on refusal, missing bundle siblings, invalid/truncated/oversized
schemas, unproved required assurance, factory effect contracts, invalid
version/storage/names, symlink/directory/input/runtime-dependency collisions,
cooperating-writer locks and selected-library shadowing. Native cases exercise
4096 repeated values in one generated batch and explicit DLMB1 envelope selection
with rejection of the wrong envelope version.

Five core tests compare all-kind and limit examples in VM/native mode, check
NoAlloc/NoBlocking/NoIO/BoundedWork contracts without a network provider, exercise
a real admitted generated TCP request/reply, and reject wrong scalar/message
types, copying, use after move, mutation under a live view and escaping borrows.

The handwritten C host links a static library and header produced from
`dln_codegen.dlm` by the actual compiler. Independently constructed all-kind wire
vectors cover all three checked formats; the host runs 5000 deterministic byte
mutations per format, short capacities, quotas, dispatch and abort cleanup.
Its source compiles under `-Wall -Wextra -Werror -UNDEBUG`. The complete emitted
DLM C plus this host also passes ASan/UBSan. LeakSanitizer is disabled; zero tracked
DLM allocations and ASan/UBSan checks are reported separately.

Existing generated-C const/volatile qualifier warnings remain visible on the
standard build path and are not suppressed. The whole generated C translation
unit is not claimed to be warning-free under -Werror. The core emitter/runtime
and old ABI files are byte-identical; strict warning gates apply to the handwritten
host. Windows cross-compilation uses Zig/Clang with the same strict host and
assertions enabled, and produces an x86-64 PE console executable. It is not run
in this Linux workspace. The full PowerShell script still needs native execution.

## Compatibility and limits

All previous network suites passed after the original foundation ABI fixture was
restored byte-for-byte and the new generator fixture received its own filename.
No old tests or core source were weakened to accommodate this addition. Every
prior compatibility snapshot passed. The new 0.141 snapshot adds its four pure
NetworkSpec modules to the protected set and preserves 155 files and 114 C symbols.
The patch adds no native network C symbol.

All pending proof bits remain pending. Proof-required generation refuses. There
is no generic proof of codec inverse/bounds/termination, no certificate of the
emitted machine code and no new crypto or session state machine. Safe parsing
claims concern valid slices; forged foreign pointers are outside the test ABI's
contract. Logical storage charges exclude allocator capacity, input/output,
transport/provider and OS memory. Unknown TLVs retain bytes and relative unknown
order, not their original positions among known fields. See DLN_CODEC_GENERATOR.md.

## Reproduction

From DLM-main with the project Rust toolchain and native C compiler:

```sh
cargo build --workspace --release --locked --offline
cargo test --workspace --all-targets --release --locked --offline
python3 crates/dlm_core/tests/native_hosts/dln_codegen.py
python3 tools/test_network_codec_checker.py
python3 tools/check_network_codec.py examples/native/generated/codec_echo.dlm --lib lib
python3 tools/check_network_codec.py examples/native/generated/codec_pos.dlm --lib lib
python3 tools/check_network_codec.py examples/native/generated/codec_le.dlm --lib lib
python3 tools/check_dln_compatibility.py --snapshot docs/DLN_BASELINE_0.141.0.json

target/release/dlm build crates/dlm_core/tests/native_hosts/dln_codegen.dlm \
  --lib lib --lib examples/native --allow-unsafe --target staticlib \
  -o /absolute/test-dir/dln_codegen.a
cc -std=gnu11 -O1 -Wall -Wextra -Werror -UNDEBUG \
  crates/dlm_core/tests/native_hosts/dln_codegen_host.c -I/absolute/test-dir \
  /absolute/test-dir/dln_codegen.a -pthread -lm -ldl -o /absolute/test-dir/codec-host
/absolute/test-dir/codec-host
```

For ASan/UBSan, compile the emitted `dln_codegen.a.c` and handwritten host together
with `-fsanitize=address,undefined -fno-omit-frame-pointer`, then execute with
`ASAN_OPTIONS=detect_leaks=0`. This instruments actual DLM code instead of loading
an instrumented module into the Python process. The existing network suites
retain their earlier dependency/provider setup.

On Windows apply over 0.141 and perform the full Build-Network.ps1 command with
PowerShell -NoExit. It requires compiler 0.142, checks all three saved generated
bundles before running 19 DLM examples, six C hosts and the existing arithmetic
gates. The Windows build/host path does not require Python. Bundle reproduction
in this script and the standalone independent structural checker are distinct
gates. Native platform and full impairment/performance/proof acceptance remain
open; a successful engineering test run does not discharge those obligations.
