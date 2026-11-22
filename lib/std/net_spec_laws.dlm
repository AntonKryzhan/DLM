module std.net_spec_laws

// Generic arithmetic/interval laws for NetworkSpec proof obligations. These
// prove statements about natural values, not an unverified native codec.
theory StdNetworkSpecLaws {
    fn schema_limit(header: Nat, fields: Nat, unknown: Nat) -> Nat = header + fields + unknown
    fn repeated_limit(count: Nat, prefix: Nat, payload: Nat) -> Nat = mul(count, prefix + payload)
    fn version_contains(minimum: Nat, current: Nat, chosen: Nat) -> Bool = and(not(minimum > chosen), not(chosen > current))

    theorem payload_fits_its_envelope(header: Nat, payload: Nat): not(payload > header + payload) by normalize
    theorem opaque_retention_does_not_reduce_bound(header: Nat, fields: Nat, unknown: Nat): not(header + fields > schema_limit(header, fields, unknown)) by normalize
    theorem repeated_field_sum(count: Nat, prefix: Nat, payload: Nat): nat_eq(mul(count, prefix + payload), mul(count, prefix) + mul(count, payload)) by normalize
    theorem zero_occurrences_have_zero_payload(prefix: Nat, payload: Nat): nat_eq(mul(0, prefix + payload), 0) by normalize
    theorem exact_version_is_accepted(version: Nat): version_contains(version, version, version) by normalize
    theorem no_padding_preserves_payload(payload: Nat): nat_eq(0 + payload + 0, payload) by normalize
}
