#!/usr/bin/env bash
# Build the core tests with sanitizers and run them.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${here}/../../build/tests"
mkdir -p "$out"
CC="${CC:-clang}"
srcs=("$here/test_core.c" "$here/../drumsampler_core.c" "$here/../drumsampler_kit.c")
"$CC" -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    "${srcs[@]}" -lm -o "$out/test_core"
"$out/test_core"
# Timing runs without sanitizers.
"$CC" -std=c99 -O2 -Wall -Wextra -Wno-unused-function -DDS_TIMING_ONLY "${srcs[@]}" -lm -o "$out/bench_core"
"$out/bench_core"
