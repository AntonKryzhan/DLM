#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export RUST_MIN_STACK=33554432
cargo test --workspace --release
if [[ -n "${DISPLAY:-}" ]]; then
    cargo test -p dlm_core --release --test native_platform editor_embeds -- --ignored --nocapture
else
    LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x800x24 +extension GLX +render' \
        cargo test -p dlm_core --release --test native_platform editor_embeds -- --ignored --nocapture
fi
cargo build --workspace --release
dlm_validation_dir=$(mktemp -d)
trap 'rm -rf "$dlm_validation_dir"' EXIT
target/release/dlm build examples/native/xml.dlm --lib lib --allow-unsafe --target sharedlib -o "$dlm_validation_dir/xml.so"
target/release/dlm build examples/native/versioned_state.dlm --lib lib --allow-unsafe --target sharedlib -o "$dlm_validation_dir/settings.so"
python3 crates/dlm_core/tests/native_hosts/interop.py "$dlm_validation_dir/xml.so" "$dlm_validation_dir/settings.so"
