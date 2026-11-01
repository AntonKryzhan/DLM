# Complete cumulative source distribution 0.100.1

This distribution contains the complete project tree. Extract it to a new directory;
no earlier source archive or patch is required. It includes the Rust workspace,
standard DLM libraries, examples, fixtures, tools and documentation. Build output
and the Rust toolchain are installed separately.

## Included changes

The source base is the original project plus the accepted cumulative changes
through 0.100.0: scalar and matrix libraries, 16384-bit source precision,
exact Q(i), physical source identity, and checked declaration handoff by DeclId.
The separately supplied Q(i) patch is integrated additively in 0.100.1:

* Existing `math::complex_q::ComplexQ`, source `ComplexQ` / `complexq_*`, text,
  examples and tests retain their contracts.
* `math::complex::ComplexRational` and source `BigComplex` / `bigc_*` add the
  supplied arithmetic and strict `<re>+<im>i` / `<re>-<im>i` text API.
* The 1944 independent Fraction fixtures, 118 interpreter/native vectors,
  CLI example and tests are included alongside existing acceptance cases.
* The new source parser scans in bounded blocks for the current 16384-bit
  profile. Compatibility tests exercise both APIs, the maximum text length,
  precision boundaries and the distinction between precision and work limits.

The release retains N01–N06 closed and N07–N16 open. It does not advance C-min
or full A/B/C. The next engineering gate is acceptance of all six C-min
conditions before D/E under [ROADMAP.md](../ROADMAP.md).

[RECURSIVE_DISCOVERY_RESEARCH_PROGRAM.md](RECURSIVE_DISCOVERY_RESEARCH_PROGRAM.md)
is included as a design reference. R0–R6 remains deferred; this distribution
implements no research workflow.

## Build and run

Use the Rust toolchain and a C compiler for native DLM builds. Current acceptance
uses Rust 1.99.1 and GCC on Linux x86-64. Run from the extracted `DLM-main`:

```sh
cargo build --release --workspace --locked
cargo test --workspace --locked
cargo run --release -p dlm_cli -- run --trusted-only --lib lib examples/valid/complex_q_demo.dlm
cargo run --release -p dlm_cli -- run --trusted-only --lib lib examples/valid/exact_complex_q.dlm
```

Only workspace-local Rust packages are required. Add `--offline` to Cargo
commands when the toolchain is already installed and network access is unwanted.
Native target support and open platform obligations are described in
[LAW_STATUS.md](LAW_STATUS.md). Complete current verification results and their
configuration are recorded in [UPDATE.md](../UPDATE.md).

## Current acceptance

All 746 workspace tests pass on Linux x86-64 (0 failed, 0 ignored). Clippy
completes without errors, with existing warnings. The extracted distribution
builds the release compiler; both complex examples pass interpreter/native
run/build checks. The archive contains 631 project files, verified by CRC,
complete byte comparison and extraction. UPDATE.md records the exact commands,
configuration and platform scope.
