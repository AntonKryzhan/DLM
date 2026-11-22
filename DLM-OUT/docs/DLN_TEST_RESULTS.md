# DLN 0.135.0 validation record

Date: 2026-11-19. Executed on Linux x86-64, Rust 1.99.1.

The restored 0.134.0 baseline completed its full workspace regression:
1011 passed, 0 failed, 1 GUI test ignored; documentation tests completed (0).
The 0.135.0 workspace regression completed with 1017 passed, 0 failed and
the same 1 GUI test ignored; documentation tests completed (0).
The compatibility gate confirms 136 protected source files and 39 old public
network C functions unchanged, with 7 additive C functions.

Focused new Rust harness: 6 tests passed. It verifies real typed TCP/UDP/options,
streaming VM/C output parity, rejection of wrong socket kinds, copying, zero
fabrication, second close/use after close, escaping/conflicting payload borrows
and false NoAlloc/NoIO declarations.

Independent Python acceptance: 31 tests passed, without skips. These include
2000 random integer cases in both byte orders, 3007 ULEB128 values and every
incomplete prefix, 4005 ZigZag boundary/random values, a fragmented/coalesced
1 MiB frame stream without native heap allocations, malformed/oversized/forged
state, compatibility-gate tampering, numeric IPv4/IPv6 TCP, IPv6-only rejection,
IPv4 clients on dual-stack TCP, dual-stack UDP replies with mapped peers,
DNS family/scope, buffer/keepalive/nodelay options and actual typed TLS echo.
The inherited suite checks previous TLS/HTTP2/WebSocket/binary/cancellation APIs.
The native error-boundary fixture closes one moved socket and then aborts;
the remaining owner is finalized, native allocation counters return to zero
and provider shutdown succeeds.

The compiled typed TCP/UDP example also passed AddressSanitizer and
UndefinedBehaviorSanitizer. LeakSanitizer was disabled in this environment;
leak evidence here comes from explicit runtime counters and owner cleanup.
The separate libcurl ABI host compiled with warnings treated as errors and ran
successfully. Both streaming and typed network examples built and ran; the
pure streaming example also ran in the VM.

Windows and FreeBSD have not been executed here. Build-Network.cmd is updated
for four examples and requires rebuilding the >=0.135 compiler. HTTP/3/ECH
successful handshakes, general QUIC, async reactors, protocol proof certificates
and TUN are not part of the reported evidence.
