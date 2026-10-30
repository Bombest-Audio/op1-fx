#!/usr/bin/env bash
# Build the core tests with sanitizers and run them.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${here}/../../build/tests"
mkdir -p "$out"
CC="${CC:-clang}"
"$CC" -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    "$here/test_core.c" "$here/../pulse_core.c" -lm -o "$out/test_core"
"$out/test_core"
# Optional: PULSE_TIMING=1 also builds an optimized, sanitizer-free binary and prints render cost.
if [ "${PULSE_TIMING:-0}" = "1" ]; then
    "$CC" -std=c99 -O2 -DPULSE_TIMING -Wall -Wextra "$here/test_core.c" "$here/../pulse_core.c" -lm -o "$out/test_timing"
    "$out/test_timing" --timing
fi
