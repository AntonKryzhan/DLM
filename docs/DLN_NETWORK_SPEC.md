# DLN-08 / DLM 0.141: NetworkSpec

NetworkSpec is an executable, bounded protocol description built from existing
DLM records, arrays, functions and theories. No syntax, parser, proof-kernel,
compiler semantics, old network C code or existing DLMB format changes.

Four new modules separate the model, canonical descriptor metadata, directional
compatibility, and ordinary arithmetic laws:

| Module | Responsibility |
| --- | --- |
| `std.net_spec` | Objects, builders, consistency checks, size bounds, transport admission and pending obligations |
| `std.net_spec_wire` | Canonical DNS1 descriptor metadata, bounded decoding, atomic writing and semantic equality |
| `std.net_spec_compat` | Shared version, directional schema checks, intersected session limits and common transport choice |
| `std.net_spec_laws` | Six ordinary mathematical laws; three independently replayable Nat equation artifacts |

These are control-plane descriptions. They do not generate application codecs
(DLN-09), execute session state machines (DLN-10), or certify native network
implementations (DLN-11). Schema validation and mathematical proof have separate
results. A valid model can still contain unsupported security or pending proofs.

## The eight objects

| Object | Data |
| --- | --- |
| `MessageSchema` / `MessageField` | Stable IDs, phase, origin roles, minor-version interval, field kinds, occurrence/byte limits and sensitive-field flags |
| `WireFormat` | Positional or existing DLMB body, integer endianness, DLMB envelope version and unknown-field policy |
| `ProtocolVersion` | Positive major, current minor and minimum accepted minor |
| `SessionContract` | Handshake/idle/duration deadlines, declared in-flight limit and authentication-before-data obligation |
| `TransportRequirements` | Existing DLN-07 required/forbidden/preferred properties |
| `SecurityRequirements` | Server or mutual authentication, confidentiality, integrity and replay scope |
| `ResourceBounds` | Existing controlled transport storage, unknown-field budgets, session-message and schema-node limits |
| `ProofObligations` | Requested obligation bits and engineering/proof-required admission mode; no caller-supplied proven bits |

`network_spec(protocol,major,minor,minimum)` constructs an empty builder with
DLMB v2, default transport budgets and engineering mode. Add fields/messages in
strictly ascending nonzero ID order using `message_add_field` and
`network_add_message`. Their insertion errors leave the model unchanged. A
builder is not admitted until the complete `network_spec_check` succeeds.
Directly constructed/mutated records pass through the same complete validation.

There are at most 16 messages and 32 fields per message, stored inline. This is
an explicit v1 profile limit, not unbounded recursion or a second type system.
Uncounted array tails are semantically inactive and omitted from canonical
metadata/equality. Equality compares active descriptor data; it is not a proof
of behavioral equivalence between arbitrary protocols. Counts above the fixed
capacities are rejected before access.

## Fields and wire bounds

| Kind | Field value | Payload bytes per occurrence |
| --- | --- | --- |
| 1 | U8 | 1 |
| 2 | U16 | 2 |
| 3 | U32 | 4 |
| 4 | U64 | 8 |
| 5 | Bool, canonical 0/1 | 1 |
| 6 | Bounded bytes | 0..maximum_length |
| 7 | Bounded UTF-8 | 0..maximum_length bytes; UTF-8 validation remains a codec obligation |
| 8 | Canonical ULEB128 U64 | 1..10 |
| 9 | Canonical ZigZag I64 in ULEB128 | 1..10 |

Occurrences have `minimum_count <= maximum_count`, maximum 1..4096; 0..1 means
optional. `maximum_length` applies only to bytes/UTF-8 and is at most 16 MiB;
it must be zero for scalar/varint kinds. Field flag 1 means sensitive transport
data; other flag bits are rejected. Sensitive fields require encrypted transport
through the effective mandatory properties. This flag does not promise storage
secrecy, memory wiping or authenticated application identity.

Each declared application message begins with a four-byte big-endian message ID.
The body uses one of two opt-in profiles:

- Kind 1: positional fields in ascending ID order, endian 1 big / 2 little for
  fixed-width values. Non-scalar cardinality (anything except exactly one)
  carries a U32 count prefix. Each bytes/UTF-8 occurrence carries a U32 length
  prefix. These prefixes use the declared integer endianness. There is no DLMB
  envelope and unknown fields are rejected.
- Kind 2: the existing eight-byte DLMB v1/v2 envelope, little-endian field ID and
  payload length, eight-byte TLV header per occurrence. Numeric payloads use
  little endian; bytes/UTF-8 use the TLV length. Repeated occurrences retain
  their per-field order. Unknown policy 0 rejects; 1 preserves unknown TLVs and
  their order. The original `std.native_binary` functions and format are intact.

Thus the base application overhead is 4 or 12 bytes, before field encodings.
`network_message_measure` returns mathematical U64 lower/upper byte bounds at
one minor version. The complete checker evaluates every relevant interval
boundary, so a large field present only in an older accepted version still
counts against the resource cap. Multiplication limits are checked before
bounded arithmetic; the largest intermediate fits U64, even when the final
message exceeds the allowed 16 MiB and must be rejected.

Unknown-field byte budgets include their TLV headers. Count and byte budgets
both apply. Preserving unknowns requires at least one unknown-field slot and
at least eight bytes; rejecting them requires both budgets to be zero. Size
admission adds the complete unknown-byte budget to the peak known-field bound.
Schema-node units are exactly `1 + messages + fields`, at most 529. They are
model-size accounting units, not CPU cycles or a wall-clock timing guarantee.

## Version and interaction rules

Majors are 1..65535, minors 0..65535, and `minimum_minor <= minor`. Message/field
`first_minor..last_minor` intervals are inclusive; U32 MAX is the non-expiring
last value. An interval must intersect the accepted window; fields must lie
inside their parent message interval. The message set must cover every accepted
minor, including gaps between message lifetimes. Reusing an ID in the same
schema is rejected even across non-overlapping lifetimes.

A message phase is 1 handshake, 2 application, 3 close. `senders` is a bit mask:
1 client, 2 server, 3 both. These declarations are checked during compatibility;
state transition execution and authentication gating still require DLN-10.

Session deadlines satisfy `0 < handshake <= idle <= duration <= I32 MAX`.
Maximum in-flight is positive and no greater than the transport queue limit.
The session-message limit is positive U64. Current admission returns these
limits as data; the old transport queue is not a delivery acknowledgment counter.
A complete future state machine must enforce the session-level limits.

## Security and transport admission

Authentication 0 is unrequested, 1 verified server certificate, 2 mutual
application identity. Replay scope 0 is unrequested, 1 transport-packet replay
protection within a connection, 2 logical-message replay across sessions.
The current transport profiles implement only authentication 0/1 and replay
scope 0/1. Mutual identity or cross-session logical replay is a valid description
but current admission returns unsupported with reason bits 1 / 2 respectively.
Transport-packet replay protection does not detect an authenticated application
sending the same logical message twice.

Confidentiality, integrity, any authentication/replay request or a sensitive
field adds mandatory secure transport. Every spec requires message boundaries.
Contradictions with explicitly forbidden properties reject the model; optional
cost/preference ranking cannot weaken security. A mandatory independent-channel
request also needs at least two configured logical channels, including channel 0.

`network_spec_select(spec,candidates)` validates the complete model and returns
`NetworkAdmission`: effective requirements, narrowed transport message cap,
choice, session contract, message limit, unsupported security and pending proofs.
The message cap is the computed schema maximum, within the user's declared
resource ceiling. This allows a small schema to use UDP/QUIC DATAGRAM even if
the generic resource ceiling was larger. It bounds bytes; it is not runtime
validation of message field contents.

The candidate mask must still be the intersection of locally available and
remotely offered profiles. Availability is not network discovery. The result
feeds the existing `transport_open`/typed adoption API; this module performs no
network I/O. See `network_spec_session.dlm` for a real TCP exchange using a
handwritten unchanged DLMB payload after specification admission.

## Directional compatibility

`network_spec_negotiate(local,remote,initiator,candidates)` validates both models,
requires equal protocol/major/wire encoding and chooses the highest shared minor.
It checks the local send domain for its role against remote acceptance, then the
remote send domain against local acceptance. A receiving declaration need not
send every message it permits receiving. Matching IDs must agree on phase and
origin role. Sender occurrence and length ranges must fit receiver ranges;
value kinds must agree. Positional scalar/count-prefix layouts must agree.

An absent receiver field is allowed only for preserved TLV extensions whose
worst-case counts/bytes fit the receiver's unknown budgets. Required receiver
fields must be emitted by the sender. Optional receiver-only fields are allowed
for TLV only when the sender has no opaque unknown fields that could collide
with those newly known IDs. This conservative rejection avoids claiming a safe
interpretation of arbitrary old opaque payloads as new typed fields. Use proper
minor introduction points rather than backdating an incompatible change.

The result intersects controlled memory, queue/channel counts, all session
upper limits and the session-message limit. It derives a common message cap
from the actual two send domains at the negotiated version. Mandatory properties
are combined; forbidden properties prevail over optional preferences; a hard
required/forbidden contradiction rejects negotiation. Both sides' proof/security
requirements survive. The returned session is an agreed declaration, not an
executed state machine or measured liveness/performance result.

This function proposes compatibility of supplied descriptions. It does not
exchange offers, authenticate descriptors or bind them into a handshake
transcript. Applications must authenticate remote declarations where needed;
identity/transcript composition remains explicit later work. The checks are
conservative structural checks, not a proof about arbitrary codec implementations.

## Proof obligations and assurance

| Bit | Obligation |
| --- | --- |
| 1 | Codec roundtrip |
| 2 | Wire/buffer bounds |
| 4 | Machine overflow/definedness |
| 8 | Deterministic encoding |
| 16 | Unknown-field preservation |
| 32 | Session transitions |
| 64 | Authentication before application data |
| 128 | Resource cleanup |
| 256 | Termination |
| 512 | Application invariants |
| 1024 | External security assumptions |

The checker derives base mask 431, adds applicable requirements, and unions the
requested bits. All remain pending for a network implementation. There is no
`proven=true` field to forge, and metadata carries no admitted proof result.
Assurance 1 permits engineering admission while retaining pending obligations.
Assurance 2 requires proof-backed admission and returns unsupported until an
independent network certificate checker can discharge the obligations. Reducing
a requested mask cannot remove derived obligations; a peer cannot relax the
other peer's proof-required mode.

`StdNetworkSpecLaws` proves six generic Nat arithmetic/interval facts with the
existing symbolic checker. Three equation artifacts in `examples/native/fixtures`
replay with the existing independent kernel: `network-spec-repeated.kproof`,
`network-spec-zero.kproof`, `network-spec-padding.kproof`. They contain no axioms.
They prove their stated mathematical equations, not native overflow safety,
codec roundtrip or this NetworkSpec validator. Machine/protocol obligations
therefore remain pending. No new trusted proof rule is introduced.

## Canonical descriptor metadata

`network_spec_encode` writes DNS1 metadata into a caller-owned buffer after full
validation and capacity preflight. A short buffer returns status 1/count 0 and
leaves the destination unchanged; invalid models also write nothing. Prefix and
suffix outside the specified output span are preserved. `network_spec_decode`
requires an exact descriptor, rejects trailing bytes/noncanonical booleans,
checks counts before indexing, and returns an empty invalid model/count 0 on
failure. Both operations use fixed storage and no foreign calls or allocation.

All words are U32 big endian. Header: ASCII `DNS1`, then 33 words:

| Word indices | Data |
| --- | --- |
| 0 | Protocol ID |
| 1..3 | Major, minor, minimum minor |
| 4..7 | Wire kind, endian, envelope version, unknown policy |
| 8..10 | Required, forbidden, preferred transport properties |
| 11..13 | Transport maximum message, queue count, channels |
| 14..15 | Controlled memory, high/low U32 of U64 |
| 16..17 | Unknown-field count/byte budgets |
| 18..19 | Session message limit, high/low U32 of U64 |
| 20 | Maximum schema nodes |
| 21..25 | Session handshake, idle, duration, in-flight, auth-before-data |
| 26..29 | Security authentication, confidentiality, integrity, replay scope |
| 30..31 | Requested obligations, assurance mode |
| 32 | Message count |

Each message follows with six words: ID, phase, senders, first minor, last minor,
field count; then eight words per field in `MessageField` declaration order.
Minimum header size is 136 bytes; maximum valid descriptor size is 16904 bytes.
Memory above 64 MiB is rejected before conversion to platform USize; the U64
session-message limit is preserved exactly. Host padding, pointers and inactive
array tails never enter metadata. Equal valid semantic descriptions encode to
identical bytes, suitable for hashing by the existing crypto layer. DNS1 itself
is neither a signature nor a source/code-bound proof certificate.

## Validation

See `DLN_NETWORK_SPEC_TEST_RESULTS.md`. The next step is DLN-09: generate actual
application encoders/decoders from these stable objects, with independently
checkable obligations. Existing payloads, transport APIs and proof rules remain
available unchanged.
