#!/usr/bin/env bash
# Numeric section acceptance (N16): every numeric test in release, sanitizers and MinGW-w64/Wine required.
set -euo pipefail
cd "$(dirname "$0")/.."
export DLM_REQUIRE_SANITIZERS="${DLM_REQUIRE_SANITIZERS:-1}" DLM_REQUIRE_WINDOWS="${DLM_REQUIRE_WINDOWS:-1}"
tests=(integers machine_arithmetic exact_numbers source_exact rational source_wide_exact exact_ops source_exact_ops
       exact_complex_q complex_q source_complex_q interval_q creal ccomplex elementary convert numeric_contract
       dense_profile scalar_witness ir_verify example_corpus numeric_acceptance)
args=()
for t in "${tests[@]}"; do args+=(--test "$t"); done
cargo test -p dlm_core --release --locked --offline --lib "${args[@]}" -- --test-threads="${DLM_TEST_THREADS:-2}"
cargo test -p dlm_cli --release --locked --offline --test cli_creal_ownership
echo "numeric acceptance: passed"
