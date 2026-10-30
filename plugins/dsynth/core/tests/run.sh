#!/usr/bin/env bash
# Build the core tests with sanitizers and run them.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${here}/../../build/tests"
mkdir -p "$out"
CC="${CC:-clang}"
"$CC" -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    "$here/test_core.c" "$here/../dsynth_core.c" -lm -o "$out/test_core"
"$out/test_core"
# The same tests without sanitizers at -O2, for the timing line (sanitizers make that number meaningless).
"$CC" -std=c99 -O2 -DDSYNTH_TEST_BENCH_ONLY -Wall -Wextra \
    "$here/test_core.c" "$here/../dsynth_core.c" -lm -o "$out/bench_core"
"$out/bench_core"
