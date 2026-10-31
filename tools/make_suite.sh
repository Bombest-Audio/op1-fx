#!/usr/bin/env bash
# Package all 20 OP-1 plugins as ONE zip for an MPC (Gen2, aarch64) that installs them with a single MPC stop and start.
#   tools/make_suite.sh [version]        default 0.4.0; writes dist/mpc/OP1-Suite-<version>-mpc-aarch64.zip and the
#                                        per-plugin zips in dist/mpc/release-<version>/ (OP1_REPO, OP1_LICENSE override
#                                        the source repo and SPDX license written into each manifest)
# Needs each plugin's MPC port built first (plugins/<id>/mpc/build/aarch64/<id>.so, via mpc-vst-plugins tools/build_port.sh).
# Each plugin is packaged by mpc-vst-plugins tools/release.py, so every package keeps its own install.sh, checksums and
# manifest. The suite adds install-all.sh and uninstall-all.sh, which run those per-plugin scripts between one stop and
# one start of MPC (install.sh -n).
set -euo pipefail
VERSION="${1:-0.4.0}"
FX="$(cd "$(dirname "$0")/.." && pwd)"
MV="${MPC_VST_PLUGINS:-$HOME/bombest-audio/mpc-re/mpc-vst-plugins}"
OUT="$FX/dist/mpc"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
REPO="${OP1_REPO:-Bombest-Audio/op1-fx}"   # source repo and license go into every package's catalog manifest
LICENSE_ID="${OP1_LICENSE:-GPL-3.0-only}"
TOP="OP1-Suite-$VERSION"
mkdir -p "$STAGE/$TOP/packages" "$STAGE/rel"

count=0
for dir in "$FX"/plugins/*/; do
    id="$(basename "$dir")"
    m="$dir/mpc"
    [ -f "$m/vst.json" ] || continue
    so="$m/build/aarch64/$id.so"
    [ -f "$so" ] || { echo "missing $so: build the MPC port first" >&2; exit 1; }
    name="$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['name'])" "$m/params.json")"
    skin="$m/build/skin/bombest - VST - $name"
    [ -d "$skin" ] || { echo "missing skin $skin" >&2; exit 1; }
    # the one-line description comes from the newest earlier zip of this plugin, else a default
    prev="$(ls -t "$OUT/$name"-*-mpc-aarch64.zip 2>/dev/null | head -n 1 || true)"
    about=""
    if [ -n "$prev" ]; then
        about="$(python3 - "$prev" <<'EOF'
import json, sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
names = sorted((n for n in z.namelist() if n.endswith("mpc-plugin.json")), key=len)
print(json.loads(z.read(names[0]))["about"])
EOF
)"
    fi
    [ -n "$about" ] || about="New take on the OP-1 $id for MPC."
    python3 "$MV/tools/release.py" --so "$so" --skin "$skin" --entry "$m/build/pluginlist-entry.xml" \
        --version "$VERSION" --about "$about" --id "$id" --repo "$REPO" --license "$LICENSE_ID" -o "$STAGE/rel" >/dev/null
    unzip -q "$STAGE/rel/$name-$VERSION-mpc-aarch64.zip" -d "$STAGE/$TOP/packages"
    # Gen1 and Force (armv7) zip for the catalog, whose main download is the armv7 asset; not part of the Gen2 suite
    so32="$m/build/$id.so"
    if [ -f "$so32" ] && file -b "$so32" | grep -q "32-bit.*ARM"; then
        python3 "$MV/tools/release.py" --so "$so32" --skin "$skin" --entry "$m/build/pluginlist-entry.xml" \
            --version "$VERSION" --about "$about" --id "$id" --repo "$REPO" --license "$LICENSE_ID" -o "$STAGE/rel" >/dev/null
    fi
    count=$((count + 1))
done
[ "$count" -gt 0 ] || { echo "no plugins found" >&2; exit 1; }

# every package must name a different uid, or the installer replaces one with the other
dups="$(cat "$STAGE/$TOP"/packages/*/mpc-plugin.json | sed -n 's/.*"uid": *"\([0-9a-f]*\)".*/\1/p' | sort | uniq -d)"
[ -z "$dups" ] || { echo "duplicate MPC uid(s) across plugins: $dups" >&2; exit 1; }

cat > "$STAGE/$TOP/install-all.sh" <<'EOF'
#!/bin/sh
# Installs every plugin in packages/ with ONE stop and ONE start of MPC. Run on the device as root:
#   sh install-all.sh [-y] [-t <synths-dir>]
# The Synths folder defaults to /nvme/Synths when it exists (Live III), else /sdcard/Synths.
# Save your project first: MPC is stopped and restarted.
set -u
cd "$(dirname "$0")"
YES=0; SYNTHS=""
die() { echo "error: $*" >&2; exit 1; }
while [ $# -gt 0 ]; do
    case "$1" in
        -y) YES=1; shift ;;
        -t) [ -n "${2:-}" ] || die "-t needs a folder"; SYNTHS="$2"; shift 2 ;;
        *) die "usage: sh install-all.sh [-y] [-t <synths-dir>]" ;;
    esac
done
if [ -z "$SYNTHS" ]; then if [ -d /nvme/Synths ]; then SYNTHS=/nvme/Synths; else SYNTHS=/sdcard/Synths; fi; fi
if [ -z "${MPC_INSTALL_TEST:-}" ]; then
    [ "$(id -u)" = 0 ] || die "run as root"
    [ "$(uname -m)" = aarch64 ] || die "this suite is for Gen2 (aarch64) devices; this one is $(uname -m)"
    command -v systemctl >/dev/null || die "systemctl not found"
fi
sha256sum -c SHA256SUMS >/dev/null 2>&1 || die "files damaged (SHA256SUMS mismatch): copy the folder again"
N=$(ls -d packages/*/ | wc -l | tr -d " ")
echo "Installing $N plugins into $SYNTHS"
if [ $YES = 0 ]; then
    printf "MPC will be stopped and restarted once. Save your project first. Continue? [y/N] "
    read -r ok; case "$ok" in y|Y|yes) ;; *) echo "cancelled"; exit 1 ;; esac
fi
mpc_service() {
    if systemctl cat acvs >/dev/null 2>&1; then echo acvs
    elif systemctl cat inmusic-mpc >/dev/null 2>&1; then echo inmusic-mpc
    else echo acvs; fi
}
mpc_ctl() {   # a test run logs the call to $MPC_TEST_LOG instead of touching MPC
    if [ -n "${MPC_INSTALL_TEST:-}" ]; then [ -z "${MPC_TEST_LOG:-}" ] || echo "$1" >> "$MPC_TEST_LOG"; return 0; fi
    systemctl "$1" "$(mpc_service)"
}
mpc_ctl stop
trap 'mpc_ctl start' EXIT
if [ -z "${MPC_INSTALL_TEST:-}" ]; then
    i=0; while pidof MPC >/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
    pidof MPC >/dev/null && die "MPC did not stop"
fi
OK=0; FAIL=""
LOG=$(mktemp) || die "cannot create a temp file"
trap 'rm -f "$LOG"; mpc_ctl start' EXIT
for p in packages/*/; do
    n=$(basename "$p")
    if (cd "$p" && sh install.sh -y -n -t "$SYNTHS" >"$LOG" 2>&1); then
        OK=$((OK + 1)); echo "  ok   $n"
    else
        FAIL="$FAIL $n"; echo "  FAIL $n"; tail -n 3 "$LOG" | sed 's/^/       /'
    fi
done
echo "$OK of $N installed."
[ -z "$FAIL" ] || { echo "failed:$FAIL" >&2; exit 1; }
echo "Starting MPC. Add the plugins from the plugin browser."
EOF

cat > "$STAGE/$TOP/uninstall-all.sh" <<'EOF'
#!/bin/sh
# Removes every plugin in packages/ with ONE stop and ONE start of MPC. Run on the device as root:
#   sh uninstall-all.sh [-y] [-t <synths-dir>]
set -u
cd "$(dirname "$0")"
YES=0; SYNTHS=""
die() { echo "error: $*" >&2; exit 1; }
while [ $# -gt 0 ]; do
    case "$1" in
        -y) YES=1; shift ;;
        -t) [ -n "${2:-}" ] || die "-t needs a folder"; SYNTHS="$2"; shift 2 ;;
        *) die "usage: sh uninstall-all.sh [-y] [-t <synths-dir>]" ;;
    esac
done
if [ -z "$SYNTHS" ]; then if [ -d /nvme/Synths ]; then SYNTHS=/nvme/Synths; else SYNTHS=/sdcard/Synths; fi; fi
[ "$(id -u)" = 0 ] || [ -n "${MPC_INSTALL_TEST:-}" ] || die "run as root"
if [ $YES = 0 ]; then
    printf "Remove all plugins in this suite from %s? MPC will be stopped and restarted once. [y/N] " "$SYNTHS"
    read -r ok; case "$ok" in y|Y|yes) ;; *) echo "cancelled"; exit 1 ;; esac
fi
mpc_service() {
    if systemctl cat acvs >/dev/null 2>&1; then echo acvs
    elif systemctl cat inmusic-mpc >/dev/null 2>&1; then echo inmusic-mpc
    else echo acvs; fi
}
mpc_ctl() {
    if [ -n "${MPC_INSTALL_TEST:-}" ]; then [ -z "${MPC_TEST_LOG:-}" ] || echo "$1" >> "$MPC_TEST_LOG"; return 0; fi
    systemctl "$1" "$(mpc_service)"
}
mpc_ctl stop
trap 'mpc_ctl start' EXIT
if [ -z "${MPC_INSTALL_TEST:-}" ]; then
    i=0; while pidof MPC >/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
fi
for p in packages/*/; do
    n=$(basename "$p")
    if [ -f "$p/uninstall.sh" ]; then
        (cd "$p" && sh uninstall.sh -y -n -t "$SYNTHS" >/dev/null 2>&1) && echo "  removed $n" || echo "  FAIL    $n"
    fi
done
EOF

cat > "$STAGE/$TOP/README.md" <<EOF
# OP-1 Suite $VERSION for MPC (Gen2, aarch64)

$count plugins in one package. Copy this folder to the MPC, then on the device as root:

    sh install-all.sh -y

MPC stops once, every plugin installs, MPC starts once. Save your project first. Safe to run again: each plugin is
replaced by uid, and files you added inside a plugin folder are kept. \`uninstall-all.sh\` removes them all.

Synths folder: /nvme/Synths on a Live III, else /sdcard/Synths. Override with \`-t <folder>\`.
Each plugin also installs alone: \`packages/<name>/install.sh\` (see its INSTALL.md).
EOF

( cd "$STAGE/$TOP" && find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 shasum -a 256 | sed 's|  \./|  |' > SHA256SUMS )
chmod 755 "$STAGE/$TOP/install-all.sh" "$STAGE/$TOP/uninstall-all.sh"
mkdir -p "$OUT"
zip="$OUT/$TOP-mpc-aarch64.zip"
rm -f "$zip"
( cd "$STAGE" && zip -qr -X "$zip" "$TOP" )
# the per-plugin zips are what the community catalog lists; keep them next to the suite
rel_out="$OUT/release-$VERSION"
rm -rf "$rel_out"; mkdir -p "$rel_out"
cp "$STAGE"/rel/*.zip "$rel_out"/
echo "$zip ($count plugins, $(du -h "$zip" | cut -f1))"
