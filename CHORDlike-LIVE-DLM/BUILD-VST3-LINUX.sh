#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if [[ ! -f dist/linux-x86_64/libchordlive.so ]]; then ./build-linux.sh; fi
out='dist/vst3-linux/CHORDlike LIVE.vst3/Contents/x86_64-linux'
mkdir -p "$out"
gcc -std=c11 -O2 -Wall -Wextra -Werror -fPIC -shared -Iinclude -o "$out/CHORDlike LIVE.so" vst3/bridge.c -Ldist/linux-x86_64 -lchordlive -Wl,-rpath,'$ORIGIN' -lm
cp dist/linux-x86_64/libchordlive.so "$out/"
python3 tests/vst3/test_host.py "$out/CHORDlike LIVE.so"
echo "VST3 Linux bundle: $out"
