#!/usr/bin/env bash
# Build the core tests with sanitizers and run them.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${here}/../../build/tests"
mkdir -p "$out"
CC="${CC:-clang}"
"$CC" -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    "$here/test_core.c" "$here/../nitro_core.c" -lm -o "$out/test_core"
"$out/test_core"
