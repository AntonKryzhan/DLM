module std.net_error

native {
    // Distinct native error value, not an exception and not proof evidence.
    // kind follows provider ABI v1; system retains the original OS/CURLcode.
    // operation identifies the failing API even when system domains differ.
    record NetworkError repr(C, version=1) { kind: I32; system: I32; operation: U32; }
    record NetworkTransfer repr(C, version=1) { count: USize; error: NetworkError; }
    fn network_ok(error: &NetworkError) -> Bool guarantees(NoAlloc,NoBlocking,NoIO) { return error.kind == I32(0); }
    fn network_would_block(error: &NetworkError) -> Bool guarantees(NoAlloc,NoBlocking,NoIO) { return error.kind == I32(1); }
    fn network_eof(error: &NetworkError) -> Bool guarantees(NoAlloc,NoBlocking,NoIO) { return error.kind == I32(2); }
    fn network_timed_out(error: &NetworkError) -> Bool guarantees(NoAlloc,NoBlocking,NoIO) { return error.kind == I32(3); }
    fn network_was_cancelled(error: &NetworkError) -> Bool guarantees(NoAlloc,NoBlocking,NoIO) { return error.kind == I32(9); }
    fn network_truncated(error: &NetworkError) -> Bool guarantees(NoAlloc,NoBlocking,NoIO) { return error.kind == I32(10); }
    fn network_error_name(error: &NetworkError) -> Utf8 guarantees(NoAlloc,NoBlocking,NoIO) {
        if error.kind == I32(0) { return "ok"; }
        if error.kind == I32(1) { return "would_block"; }
        if error.kind == I32(2) { return "end_of_stream"; }
        if error.kind == I32(3) { return "timeout"; }
        if error.kind == I32(4) { return "invalid_argument_or_state"; }
        if error.kind == I32(5) { return "system_or_provider_error"; }
        if error.kind == I32(6) { return "unsupported"; }
        if error.kind == I32(7) { return "resource_or_length_limit"; }
        if error.kind == I32(8) { return "tls_error"; }
        if error.kind == I32(9) { return "cancelled"; }
        if error.kind == I32(10) { return "truncated_datagram"; }
        if error.kind == I32(11) { return "protocol_mismatch"; }
        return "unknown_network_error";
    }
}
