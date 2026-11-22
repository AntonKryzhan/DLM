# DLN dependency and trust map — 0.139.0

| Surface | Checked path | Interpreter/runtime path | Native output |
| --- | --- | --- | --- |
| Existing std.net | checker/ordinary_surface.rs, compile.rs, ir_verify/builtins.rs | runtime/ordinary_surface.rs → os.rs | backend_c.rs → backend_c_runtime.c |
| std.native_protocol, net_bytes, net_codec, net_error | native syntax → native/check.rs → TypedFacts.native/MIR | native/vm.rs for supported pure operations | native/emit.rs → native/runtime.c |
| std.native_net, net_socket | Same native checker, explicit dlm.net.v1 trust admission | Foreign calls rejected by reference VM | native/emit.rs embeds native/network.c; system sockets and optional libcurl |
| std.net_async | Existing native checker and Resource ownership; explicit dlm.net.v1 admission | Foreign OS operations are not emulated by the reference VM | native/network.c scopes, IOCP on Windows, epoll/kqueue/poll and bounded producer notifications on POSIX |
| std.net_crypto | Native move/borrow checks, distinct owners, unsafe raw imports and dlm.net.v1 admission | Foreign primitives are not emulated by the reference VM | network.c crypto adapter, GnuTLS/Hogweed dynamic ABI, OS random, owned keys and runtime replay/budget checks |
| Socket owners | Existing record ownership, Resource and partial-move checks | Native VM cannot emulate OS acquisition | Existing native runtime finalizers call the provider on all exits |
| Project policy | project.rs reads exact unsafe_roots | Checker trust policy remains authoritative | Strict mode still rejects foreign programs |
| Audit | audit.rs reads checked native facts | FFI libraries appear as Unsafe | No network certificate is generated |

Only native/network.c grows at the existing runtime boundary in this patch.
The 136 protected compiler/stdlib files and old public network C bodies remain
byte-for-byte unchanged, checked by the versioned compatibility snapshot.
New library types reuse existing nominal records and ownership; no parser,
checker, generic IR or proof-kernel rule is added.

Networking effects are not mathematical evidence. Native NoAlloc/NoIO/borrow
checks apply within their documented machine model; declarations at C ABI remain
trusted foreign contracts. The later NetworkSpec/Proof IR stages must preserve
this distinction and independently bind certificates to their generated code.

0.137 adds the independent NetworkScope owner without changing the legacy reactor
module or its public C bodies. The 0.136 snapshot protects 141 source files and
61 existing provider functions. Producer tokens retain only a reference-counted
control block; cancellation retires OS requests before freeing payload storage.
These checks bind engineering sources and ABI, not formal network certificates.

0.138 leaves all 142 protected 0.137 compiler/stdlib files and 70 prior public C
function bodies/declarations unchanged. GnuTLS/Nettle are explicit trusted native
dependencies; status/replay/key-budget observations are not static proof evidence.

0.139 adds std.net_quic and a separately built pinned source provider: ngtcp2
1.26.0 (MIT) + dynamically linked GnuTLS. The old compiler/parser/checker/IR and
83 pre-existing networking C bodies/ABI declarations are unchanged. The custom
allocator bounds app/ngtcp2 memory; TLS/OS allocations remain outside that budget.
No generated network proof certificate is claimed. Windows PE cross-compilation
does not close native platform runtime acceptance.
