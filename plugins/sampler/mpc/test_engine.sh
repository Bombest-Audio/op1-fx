#!/usr/bin/env bash
# Offline test of the MPC engine's sample loading, under ASan and UBSan.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
MV="${MPC_VST:-$HOME/bombest-audio/mpc-re/mpc-vst-plugins}"
out="$here/../build/tests"
mkdir -p "$out"
clang -std=gnu11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra \
  -I"$here/../core" -I"$here" -I"$MV/wrapper" \
  "$here/test_engine.c" "$here/engine.c" "$here/sampler_wav.c" "$here/../core/sampler_core.c" -lm -lpthread -o "$out/test_engine"
"$out/test_engine" "$here/samples"
