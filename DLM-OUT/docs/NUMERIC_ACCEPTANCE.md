# Acceptance of the numeric section (N16), 0.108.0

`math::acceptance::CRITERIA` maps N01..N16 to their tests, fixtures, source modules and documents.
`tests/numeric_acceptance.rs`:

* evidence exists; every fixture is written by a generator in `tools/` (independent oracle) and read by a test;
  every committed fixture belongs to a criterion;
* one regression program over all numeric source modules (`std.dense` now sits on top of the numeric chain;
  `std.complex_q` runs separately) agrees with the host: values, or error categories (`math::contract`) where
  host and source word refusals differently; all six categories are exercised;
* native C equals the interpreter; dense and ownership traces (no buffer released at exit);
* ASan/UBSan/LSan build when the toolchain supports it; MinGW-w64 cross build under Wine when installed.
  `DLM_REQUIRE_SANITIZERS=1` / `DLM_REQUIRE_WINDOWS=1` turn a missing toolchain into a failure.

Full run: `tools/numeric_acceptance.sh` (Linux: all numeric tests in release with both switches) or
`tools/numeric_acceptance.ps1` (Windows: the same tests with the native MinGW-w64 toolchain).
`build_c_executable_with_flags` passes extra compiler flags.
