#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")"
mkdir -p dist/linux-x86_64
dlm check src/live_core.dlm --allow-unsafe
dlm build src/live_core.dlm --allow-unsafe --target sharedlib --cpu auto -o dist/linux-x86_64/libchordlive.so
printf '%s\n' 'Built DLM LIVE core. This is a shared library, not a VST3 plugin.'
