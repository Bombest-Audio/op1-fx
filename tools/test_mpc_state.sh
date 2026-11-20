#!/usr/bin/env bash
# Preset state round trip for the MPC ports, under ASan and UBSan: tools/test_mpc_state.sh [plugin id ...]
# (all plugins when none are named). Needs the toolchain for gen_vst.py and engine.h, at $MPC_VST_PLUGINS
# (default ~/bombest-audio/mpc/mpc-vst-plugins). The wrapper-level check is the toolchain's tools/test_port.sh.
set -euo pipefail
FX="$(cd "$(dirname "$0")/.." && pwd)"
MV="${MPC_VST_PLUGINS:-$HOME/bombest-audio/mpc/mpc-vst-plugins}"
[ -f "$MV/tools/gen_vst.py" ] || { echo "no toolchain at $MV (set MPC_VST_PLUGINS)" >&2; exit 1; }
[ $# -gt 0 ] || set -- $(cd "$FX/plugins" && ls)
failed=0
for id in "$@"; do
    CFG="$FX/plugins/$id/mpc/vst.json"
    eval "$(python3 "$MV/tools/gen_vst.py" "$CFG" --shell)"
    python3 "$MV/tools/gen_vst.py" "$CFG" --params-h >/dev/null
    OUT="$ROOT/$PORT/build/tests"
    mkdir -p "$OUT"
    (cd "$ROOT" && clang -std=gnu11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined \
        -Wall -Wextra -Wno-unused-parameter -Werror $CFLAGS -I"$PORT/build" -I"$MV/wrapper" \
        "$FX/tools/test_mpc_state.c" $SOURCES $LIBS -o "$OUT/test_mpc_state")
    DIR=""
    [ -d "$ROOT/$PORT/samples" ] && DIR="$ROOT/$PORT/samples"
    if r=$("$OUT/test_mpc_state" $DIR 2>&1); then
        echo "$id: ok"
    else
        echo "$id:"; echo "$r"; failed=1
    fi
done
exit $failed
