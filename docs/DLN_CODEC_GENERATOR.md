# DLN-09 — generated application codecs (DLM 0.142.0)

`dlm network-codegen` turns the existing checked NetworkSpec into usable typed
DLM application parsers and serializers. This is an engineering implementation
of stage 09. Protocol/machine proof obligations remain pending; proof-required
schemas are refused, never silently downgraded. The proof kernel, core compiler,
existing transport/provider implementations and public network C ABI are unchanged.

## Generate, reproduce and independently check

From DLM-main with the rebuilt compiler and standard library:

```sh
target/release/dlm network-codegen examples/native/network_codec_schema.dlm \
  --lib lib --module generated.codec_echo --prefix Echo --minor 1 \
  -o examples/native/generated/codec_echo.dlm

target/release/dlm network-codegen examples/native/network_codec_schema.dlm \
  --lib lib --module generated.codec_echo --prefix Echo --minor 1 \
  -o examples/native/generated/codec_echo.dlm --check

python3 tools/check_network_codec.py examples/native/generated/codec_echo.dlm \
  --lib lib --report /absolute/output/codec-check.json
```

The input is either a `.dlm` file with a safe no-argument `network_schema()`
function returning NetworkSpec, or a canonical DNS1 `.dns` descriptor. A different
factory can be selected with `--factory`. The factory must declare and pass
`guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork)`. It is evaluated in the existing
reference VM, not executed as a host binary. The generator calls the actual
standard-library schema validator, descriptor encoder and plan validator. A
50-million-step VM limit bounds schema evaluation; it is not a wall-clock quota.
Imported standard modules must come from the selected library, including when
the input is a binary descriptor. Unsafe factories are not admitted.

`--minor` defaults to the current minor and must lie in the accepted window. Only
messages and fields active in that minor receive typed bindings. Their original
introduction/removal intervals remain in the plan. The complete original
specification is retained by `Echo_spec()` and the descriptor. A connection must
negotiate a compatible minor before choosing the matching generated module;
minor-version negotiation is not performed by the payload decoder itself.

`--storage BYTES` selects a per-value logical storage budget (default 8 MiB,
allowed range 256 bytes through 64 MiB). A plan unable to hold its minimum count
of value rows is rejected. Even a valid plan may reject particular messages
whose bytes/rows exceed this budget. Storage refusal is explicit, never data
truncation. Existing schema wire, repetition, node and unknown-field bounds also
remain enforced.

The three sibling outputs are:

| File | Contents |
| --- | --- |
| `codec_echo.dlm` | Full-spec factory, checked constant plans, owned message wrappers, typed builders/getters, encoder/decoder/equality and message dispatcher |
| `codec_echo.dlm.dns` | Canonical full NetworkSpec descriptor, byte-for-byte DNS1 |
| `codec_echo.dlm.codec` | Versioned engineering plan with active field tables, min/max wire sizes, value counts, dependency/source/descriptor bindings and pending proof mask |

Generation is deterministic and path-independent with LF output on every OS.
The source is parsed, linked, type/effect/ownership checked and each emitted plan
is validated in the VM before any output is installed. Existing different files
require `--force`. `--check` reproduces all three files and only compares bytes;
it neither regenerates nor repairs them. It cannot be combined with `--force`.
Input files, runtime dependencies and non-regular/symlink outputs are protected
from replacement. An exclusive sibling `.codec_echo.dlm.codegen-lock` excludes
another cooperating writer. Files are staged together and replaced with
backups/rollback on reported errors. This is not a power-failure-safe multi-file
transaction. After a killed process, inspect the bundle/backups before removing
its stale lock and intentionally regenerating.

## Typed API and ownership

For message ID 1, prefix Echo, the generator supplies:

```dlm
let mut message: EchoMessage1=EchoMessage1_new();
let added: CodecError=EchoMessage1_field_1_add(&mut message,42);
let measured: CodecIo=EchoMessage1_measure(&message);
// Complete the other required fields before measure/encode can succeed.
```

Use `network_codec.dlm` for a complete all-kind example and
`network_codec_session.dlm` for generated request/reply over an admitted TCP
session. Their required fields are filled in ID order before encoding.

| Schema kind | Typed scalar argument/result or byte view |
| --- | --- |
| 1 / U8 | U8 |
| 2 / U16 | U16 |
| 3 / U32 | U32 |
| 4 / U64 | U64 |
| 5 / Bool | Bool, canonical wire 0 or 1 |
| 6 / bytes | Slice<U8>, owned copied payload; span/view getter |
| 7 / UTF-8 | Slice<U8>, strict valid UTF-8; span/view getter |
| 8 / ULEB128 | U64, shortest canonical encoding only |
| 9 / ZigZag | I64, ZigZag followed by canonical ULEB128 |

Scalar fields have `_add`, `_add_many` and `_get`; byte/text fields have `_add`,
`_span` and `_view`. Getters select an occurrence and return an explicit error
when it is absent. A view borrows its message. Obtain `_span` first if the caller
must distinguish a missing field from a present empty value: `_view` returns an
empty slice on a failed span. Wrong-message and scalar types are checked by the
existing compiler. Messages own buffers, require explicit moves, and cannot be
mutated while a returned view remains borrowed.

Builders append known fields in nondecreasing field-ID order and retain repeat
order. They may be incomplete while being built. Invalid values, malformed UTF-8,
wrong order, too many occurrences and exceeded budgets fail before mutation.
The batch scalar operation validates before reserving once; 4096 occurrences
are supported without repeatedly rescanning an ever-growing message. A runtime
allocation failure uses normal DLM unwinding; it is not a successful status-return
transaction with the old allocation restored.

`EchoMessage1_measure`, `_encode`, `_decode` and `_equal` operate on complete
valid messages. `Echo_decode` dispatches every active message ID into the common
owned CodecMessage result. The `_spec` factory makes the same full description
available to `network_spec_negotiate`; no handwritten payload codec is needed.

The native record representation is not sealed. Public `.inner` and plan records
can be forged in source; every high-level operation checks their IDs, kinds,
counts, arena spans, canonical ordering and budgets before use. The arbitrary
pointer ABI used by tests is explicitly unsafe and is outside safe-slice claims.

## Payload layout and canonical form

Every payload starts with a four-byte big-endian message ID. It is followed by:

| WireFormat | Body |
| --- | --- |
| Positional / endian 1 or 2 | Active fields in ID order; count prefix U32 in the selected endian unless cardinality is exactly 1; byte/text values additionally have a U32 length prefix; fixed integers use that endian; varints remain byte-oriented |
| DLMB / envelope 1 or 2 | Existing `DLMB` magic, U16LE envelope version, U16LE flags zero; each occurrence has U32LE field ID, U32LE byte length and its value; fixed scalar integers are little endian |

DLMB envelopes are reused unchanged. This does not replace existing native DLMB
helpers or the common transport's framing. Field introduction/retirement changes
which IDs are known at the selected minor. Inactive IDs are handled as unknown
only when that profile explicitly preserves unknown fields.

The parser accepts arbitrary ordering of known DLMB fields, preserves repeat
order within each field, and normalizes known fields into ascending ID order.
Preserved unknown TLVs retain their raw bytes and their relative order and are
appended after known fields on encoding. Their original interleaving with known
fields is not retained. Unknown tags and payload bytes are opaque; tags colliding
with an active known field cannot be added through `_add_unknown`. Both the
number and complete raw TLV bytes (including headers) consume explicit budgets.
Positional profiles reject unknown extensions.

The inverse property tested here is equality of valid canonical logical values:
`decode(encode(value)) == value`. Arbitrarily ordered DLMB input is normalized,
so `encode(decode(input))` need not equal the input bytes. Encoding the resulting
canonical value again is deterministic. No formal theorem of the generic
implementation or generated machine code is claimed.

## Bounds, errors and storage

`codec_scan` checks a complete safe input slice without allocation or I/O before
`codec_decode` allocates the output rows, byte arena and preserved unknown bytes.
It checks framing, exact integer widths, canonical booleans/varints, UTF-8,
required/optional/repeated counts, claimed lengths, trailing bytes and all quotas.
Oversized declared byte fields/unknown TLVs fail on their header without waiting
for the declared payload. Failed decode returns an empty owned message with ID 0.
It does not expose a partially accepted value. Partial payloads are not retained;
transport framing/stream accumulation must supply the complete message.

`codec_measure`, `codec_encode`, equality, getters and scan retain
NoAlloc/NoBlocking/NoIO/BoundedWork contracts. They perform bounded loops over
validated counts/sizes. This is not a latency/performance certificate. The
serializer validates the complete model and destination span before writing;
status-return validation/capacity errors leave the entire output unchanged.
Successful encoding preserves bytes outside the requested span.

| CodecError.kind | Meaning |
| --- | --- |
| 0 | Success |
| 1 | Incomplete input or insufficient output capacity |
| 2 | Requested occurrence is absent |
| 4 | Invalid plan/value/encoding/offset or missing required field |
| 7 | Count/length/wire/storage/unknown budget exceeded |
| 11 | Message ID mismatch or no active dispatcher case |

`field` and `offset` locate the failing field and input/value/output position where
available. A failed CodecIo has count 0; successful count is bytes written and
next is the absolute output end. Error classification may reject a header before
an incomplete body; callers must not treat every nonzero status as retryable.

The requested logical value storage accounting is:

`256 + 32 * known_value_rows + known_byte_arena + raw_unknown_bytes <= --storage`.

The fixed 32-byte row charge is a portable accounting unit. It is not the native
ABI sizeof guarantee. This accounting excludes input/output buffers, allocator
reserved capacity/metadata, stacks, compiler/VM, transport/provider and OS memory.
Buffer growth can reserve more physical memory. Transport quotas retain their
separate controlled-allocation scope; no whole-process hard RAM quota is claimed.
A known value may still fit the wire bound while exceeding the chosen value
storage budget, and is then refused.

## Independent check and remaining obligations

`tools/check_network_codec.py` uses only Python's standard library. It does not
invoke/import the Rust generator or DLM VM. It independently validates DNS1,
derives active size/count/version plans, checks the exact permitted typed-wrapper
token rules and binds the bundle to a pinned SHA-256 runtime dependency closure.
A source-body edit is rejected even if its plan checksum is updated. Runtime
edits require explicit checker pin review and regeneration. FNV64 entries in the
plan detect accidental mismatches; they are not signatures. Reports include
SHA-256 source/descriptor/dependency digests, assurance=engineering,
formal_protocol_certificate=false and the unchanged nonzero pending proof mask.

The independent checker is a structural/source-binding check against the pinned
runtime, not a generic parser correctness proof, compiler validation or trusted
code signing service. Its own source/pins and the compiler/runtime remain trusted
engineering inputs. Tests use a separate wire/parser oracle, malformed input and
mutation corpora, VM/native comparison, ownership/effect rejections and an
instrumented C ABI host. All pending proof bits are retained. Mutual identity and
cross-session replay declarations may be represented in a generated description;
a bytes-only codec does not implement them and transport admission still refuses
unsupported security. A proof-required input cannot be passed off as engineering.

DLN-10 must implement session transitions, timeouts, authentication gates and
termination/cleanup behavior. DLN-11 must supply the independent native protocol
certificate checker and proof discharge. Current arithmetic Nat artifacts are
retained, but do not prove codec or network execution. Native Windows/FreeBSD/
macOS runtime acceptance and the larger fault/performance matrix remain separate.
See DLN_CODEC_TEST_RESULTS.md for the checks actually executed for this release.
