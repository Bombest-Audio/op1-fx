#!/usr/bin/env bash
# Package all 20 OP-1 plugins as ONE zip per MPC generation, each installing every plugin with a single MPC stop and start:
#   Gen2 (MPC Live III and kin, aarch64):                       dist/mpc/OP1-Suite-<version>-mpc-aarch64.zip
#   Gen1 (MPC Live / Live II, One, X, Key 61, Force; armv7):    dist/mpc/OP1-Suite-<version>-mpc-armv7.zip
#
#   tools/make_suite.sh [version]        default 0.4.0; also writes the per-plugin zips (both architectures) to
#                                        dist/mpc/release-<version>/ (OP1_REPO, OP1_LICENSE override the source repo and
#                                        SPDX license written into each manifest; OP1_ID_PREFIX sets the catalog id prefix,
#                                        default op1fx-, so the id of Delay is op1fx-delay; OP1_OUT replaces dist/mpc)
# Needs each plugin's MPC port built first, via mpc-vst-plugins tools/build_port.sh <vst.json> all:
#   plugins/<id>/mpc/build/aarch64/<id>.so (Gen2) and plugins/<id>/mpc/build/<id>.so (Gen1, 32-bit ARM).
# Every plugin needs its aarch64 build. The Gen1 suite is made only when every plugin also has its armv7 build; when none
# has one it is skipped with a warning, and when only some have one the script stops, so a suite never ships short.
# Each plugin is packaged by mpc-vst-plugins tools/release.py, so every package keeps its own install.sh, checksums and
# manifest. A suite adds install-all.sh and uninstall-all.sh, which run those per-plugin scripts between one stop and
# one start of MPC (install.sh -n).
set -euo pipefail
VERSION="${1:-0.4.0}"
FX="$(cd "$(dirname "$0")/.." && pwd)"
MV="${MPC_VST_PLUGINS:-$HOME/bombest-audio/mpc/mpc-vst-plugins}"
[ -f "$MV/tools/release.py" ] || { echo "mpc-vst-plugins not found at $MV: set MPC_VST_PLUGINS" >&2; exit 1; }
OUT="${OP1_OUT:-$FX/dist/mpc}"   # where the zips go; OP1_OUT points a test run somewhere else
PREV="$FX/dist/mpc"               # earlier zips, read for each plugin's one-line description
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
REPO="${OP1_REPO:-Bombest-Audio/op1-fx}"   # source repo and license go into every package's catalog manifest
LICENSE_ID="${OP1_LICENSE:-GPL-3.0-only}"
ID_PREFIX="${OP1_ID_PREFIX-op1fx-}"          # catalog ids never change once listed, so a generic id like "delay" would be locked forever
mkdir -p "$STAGE/rel"

# suite_top <arch>: the folder each suite zip unpacks to. The arch is in the name so the two suites never merge into one
# folder when both are unzipped in the same place (their packages share file names, so the checksums would break).
suite_top() { echo "OP1-Suite-$VERSION-mpc-$1"; }
mkdir -p "$STAGE/aarch64/$(suite_top aarch64)/packages" "$STAGE/armv7/$(suite_top armv7)/packages"

count=0; n32=0; missing32=""
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
    prev="$(ls -t "$PREV/$name"-*-mpc-aarch64.zip 2>/dev/null | head -n 1 || true)"
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
        --version "$VERSION" --about "$about" --id "$ID_PREFIX$id" --repo "$REPO" --license "$LICENSE_ID" -o "$STAGE/rel" >/dev/null
    zip64="$STAGE/rel/$name-$VERSION-mpc-aarch64.zip"
    [ -f "$zip64" ] || { echo "release.py did not write $zip64" >&2; exit 1; }
    unzip -q "$zip64" -d "$STAGE/aarch64/$(suite_top aarch64)/packages"
    # Gen1 and Force (armv7): the catalog's main download per plugin, and the Gen1 suite
    so32="$m/build/$id.so"
    if [ -f "$so32" ] && file -b "$so32" | grep -q "32-bit.*ARM"; then
        python3 "$MV/tools/release.py" --so "$so32" --skin "$skin" --entry "$m/build/pluginlist-entry.xml" \
            --version "$VERSION" --about "$about" --id "$ID_PREFIX$id" --repo "$REPO" --license "$LICENSE_ID" -o "$STAGE/rel" >/dev/null
        zip32="$STAGE/rel/$name-$VERSION-mpc-armv7.zip"
        [ -f "$zip32" ] || { echo "release.py did not write $zip32" >&2; exit 1; }
        unzip -q "$zip32" -d "$STAGE/armv7/$(suite_top armv7)/packages"
        n32=$((n32 + 1))
    else
        missing32="$missing32 $id"
    fi
    count=$((count + 1))
done
[ "$count" -gt 0 ] || { echo "no plugins found" >&2; exit 1; }

ARCHES="aarch64"
if [ "$n32" -eq "$count" ]; then
    ARCHES="aarch64 armv7"
elif [ "$n32" -eq 0 ]; then
    echo "warning: no plugin has an armv7 build (plugins/<id>/mpc/build/<id>.so), so there is no Gen1 suite" >&2
else
    echo "armv7 build missing for:$missing32. Build every plugin for armv7, or none, so the Gen1 suite is complete" >&2
    exit 1
fi

# write_suite <arch>: install-all.sh, uninstall-all.sh, README.md and SHA256SUMS for one suite, then its zip
write_suite() {
    local arch="$1" top root gen devices other zip
    top="$(suite_top "$arch")"
    root="$STAGE/$arch/$top"
    case "$arch" in
        aarch64) gen="Gen2"; devices="MPC Live III and other Gen2 models"; other=armv7 ;;
        armv7)   gen="Gen1"; devices="Force, MPC Live / Live II, MPC One, MPC X, MPC Key 61"; other=aarch64 ;;
        *) echo "write_suite: unknown arch $arch" >&2; exit 1 ;;
    esac

    # every package must name a different uid, or the installer replaces one with the other
    local dups
    dups="$(cat "$root"/packages/*/mpc-plugin.json | sed -n 's/.*"uid": *"\([0-9a-f]*\)".*/\1/p' | sort | uniq -d)"
    [ -z "$dups" ] || { echo "duplicate MPC uid(s) across $arch plugins: $dups" >&2; exit 1; }
    # and every package must be built for this suite's CPU
    local wrong
    wrong="$(grep -L "\"arch\": *\"$arch\"" "$root"/packages/*/mpc-plugin.json || true)"
    [ -z "$wrong" ] || { echo "not $arch packages in the $arch suite: $wrong" >&2; exit 1; }

    cat > "$root/install-all.sh" <<'EOF'
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
    case "@ARCH@:$(uname -m)" in
        armv7:armv7*|aarch64:aarch64) ;;
        armv7:aarch64|aarch64:armv7*)
            die "this suite is for @GEN@ (@ARCH@) devices; this one is $(uname -m): download OP1-Suite-@VERSION@-mpc-@OTHER@.zip" ;;
        *) die "this suite is for @GEN@ (@ARCH@) MPC devices; this one is $(uname -m)" ;;
    esac
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

    cat > "$root/uninstall-all.sh" <<'EOF'
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
    pidof MPC >/dev/null && die "MPC did not stop"
fi
FAIL=""
for p in packages/*/; do
    n=$(basename "$p")
    if [ -f "$p/uninstall.sh" ]; then
        if (cd "$p" && sh uninstall.sh -y -n -t "$SYNTHS" >/dev/null 2>&1); then echo "  removed $n"
        else FAIL="$FAIL $n"; echo "  FAIL    $n"; fi
    fi
done
[ -z "$FAIL" ] || { echo "failed:$FAIL" >&2; exit 1; }
EOF

    # the arch, generation and version go into the scripts after the quoted heredocs, so nothing else in them expands
    local f
    for f in install-all.sh uninstall-all.sh; do
        sed -e "s|@ARCH@|$arch|g" -e "s|@GEN@|$gen|g" -e "s|@OTHER@|$other|g" -e "s|@VERSION@|$VERSION|g" \
            "$root/$f" > "$root/$f.new"
        mv "$root/$f.new" "$root/$f"
    done
    if grep -q '@[A-Z]*@' "$root/install-all.sh" "$root/uninstall-all.sh"; then
        echo "unreplaced placeholder in the $arch suite scripts" >&2; exit 1
    fi

    cat > "$root/README.md" <<EOF
# OP-1 Suite $VERSION for MPC ($gen, $arch)

$count plugins in one package, for $devices. The other generation needs \`OP1-Suite-$VERSION-mpc-$other.zip\`.
Copy this folder to the MPC, then on the device as root:

    sh install-all.sh -y

MPC stops once, every plugin installs, MPC starts once. Save your project first. Safe to run again: each plugin is
replaced by uid, and files you added inside a plugin folder are kept. \`uninstall-all.sh\` removes them all.

Synths folder: /nvme/Synths when it exists (Live III), else /sdcard/Synths. Override with \`-t <folder>\`. On a Force,
keep the default: MPC can't load a plugin from a noexec drive such as the Force's SSD.
Each plugin also installs alone: \`packages/<name>/install.sh\` (see its INSTALL.md).
EOF

    ( cd "$root" && find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 shasum -a 256 | sed 's|  \./|  |' > SHA256SUMS )
    chmod 755 "$root/install-all.sh" "$root/uninstall-all.sh"
    mkdir -p "$OUT"
    zip="$OUT/OP1-Suite-$VERSION-mpc-$arch.zip"
    rm -f "$zip"
    ( cd "$STAGE/$arch" && zip -qr -X "$zip" "$top" )
    echo "$zip ($count plugins, $(du -h "$zip" | cut -f1))"
}

for arch in $ARCHES; do write_suite "$arch"; done

# the per-plugin zips are what the community catalog lists; keep them next to the suites
rel_out="$OUT/release-$VERSION"
rm -rf "$rel_out"; mkdir -p "$rel_out"
cp "$STAGE"/rel/*.zip "$rel_out"/
