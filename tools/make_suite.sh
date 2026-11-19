#!/usr/bin/env bash
# Package all 20 OP-1 plugins as ONE zip per MPC generation, each installing every plugin with a single MPC stop and start:
#   Gen2 (MPC Live III and kin, aarch64):                       dist/mpc/OP1-Suite-<version>-mpc-aarch64.zip
#   Gen1 (MPC Live / Live II, One, X, Key 61, Force; armv7):    dist/mpc/OP1-Suite-<version>-mpc-armv7.zip
#
#   tools/make_suite.sh <version>        also writes the per-plugin zips (both architectures) to dist/mpc/release-<version>/
#     OP1_REPO, OP1_LICENSE    the source repo and SPDX license written into each manifest
#     OP1_ID_PREFIX            catalog id prefix, default op1fx-, so the id of Delay is op1fx-delay
#     OP1_OUT                  where the zips go instead of dist/mpc (a test run)
#     OP1_ALLOW_STALE=1        package builds that are older than their sources (see below)
#     MPC_VST_PLUGINS          the mpc-vst-plugins checkout, default ~/bombest-audio/mpc/mpc-vst-plugins
# Needs each plugin's MPC port built first, via mpc-vst-plugins tools/build_port.sh <vst.json> all:
#   plugins/<id>/mpc/build/aarch64/<id>.so (Gen2) and plugins/<id>/mpc/build/<id>.so (Gen1, 32-bit ARM).
# Every plugin needs its aarch64 build. The Gen1 suite is made only when every plugin also has its armv7 build, so it never
# ships short. When none has one, the Gen1 suite is skipped with a warning. When only some have one, the Gen2 suite and the
# per-plugin zips are still written, then the script exits non-zero naming the plugins that need an armv7 build.
# A build older than any file in its plugin's core/ or mpc/ stops the script, so the two generations of a release can't
# silently come from different sources. Rebuild with `build_port.sh ... all`.
# Each plugin is packaged by mpc-vst-plugins tools/release.py, so every package keeps its own install.sh, checksums and
# manifest. A suite adds install-all.sh and uninstall-all.sh, which run those per-plugin scripts between one stop and
# one start of MPC (install.sh -n). Nothing in dist/ changes unless every check passes.
set -euo pipefail
[ $# -eq 1 ] || { echo "usage: tools/make_suite.sh <version>   (for example 0.4.2)" >&2; exit 2; }
VERSION="$1"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+([-+][0-9A-Za-z.-]+)?$ ]] || { echo "version must look like 1.2.3, not $VERSION" >&2; exit 2; }
FX="$(cd "$(dirname "$0")/.." && pwd)"
MV="${MPC_VST_PLUGINS:-$HOME/bombest-audio/mpc/mpc-vst-plugins}"
[ -f "$MV/tools/release.py" ] || { echo "mpc-vst-plugins not found at $MV: set MPC_VST_PLUGINS" >&2; exit 1; }
OUT="${OP1_OUT:-$FX/dist/mpc}"
PREV="$FX/dist/mpc"               # earlier zips, read for each plugin's one-line description
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
REPO="${OP1_REPO:-Bombest-Audio/op1-fx}"   # source repo and license go into every package's catalog manifest
LICENSE_ID="${OP1_LICENSE:-GPL-3.0-only}"
ID_PREFIX="${OP1_ID_PREFIX-op1fx-}"          # catalog ids never change once listed, so a generic id like "delay" would be locked forever
mkdir -p "$STAGE/rel" "$STAGE/zips"

# suite_top <arch>: the folder each suite zip unpacks to. The arch is in the name so the two suites never merge into one
# folder when both are unzipped in the same place (their packages share file names, so the checksums would break).
suite_top() { echo "OP1-Suite-$VERSION-mpc-$1"; }
mkdir -p "$STAGE/aarch64/$(suite_top aarch64)/packages" "$STAGE/armv7/$(suite_top armv7)/packages"

# stale <so> <id>: true when a file in the plugin's core/ or mpc/ (its MPC build inputs) changed after the build. Git only
# rewrites a file on checkout when its content changes, so a newer file means the source differs from what was built.
stale() {
    [ -n "$(find "$FX/plugins/$2/core" "$FX/plugins/$2/mpc" -type f -newer "$1" \
        ! -path '*/build/*' ! -path '*/core/tests/*' ! -name .DS_Store -print 2>/dev/null | head -n 1)" ]
}

# package <so> <arch> <id> <name> <skin> <about> <m>: one plugin zip via release.py, unpacked into that arch's suite.
# release.py names the zip from a slug of the plugin name, so take whatever single zip it wrote rather than guess the name.
package() {
    local so="$1" arch="$2" id="$3" name="$4" skin="$5" about="$6" m="$7" d z
    d="$STAGE/rel/$arch-$id"
    python3 "$MV/tools/release.py" --so "$so" --skin "$skin" --entry "$m/build/pluginlist-entry.xml" \
        --version "$VERSION" --about "$about" --id "$ID_PREFIX$id" --repo "$REPO" --license "$LICENSE_ID" -o "$d" >/dev/null
    z=("$d"/*-mpc-"$arch".zip)
    [ ${#z[@]} -eq 1 ] && [ -f "${z[0]}" ] || { echo "release.py did not write one $arch zip for $name in $d" >&2; exit 1; }
    unzip -q "${z[0]}" -d "$STAGE/$arch/$(suite_top "$arch")/packages"
    cp "${z[0]}" "$STAGE/zips/"
}

count=0; n32=0; missing32=""; stale_list=""
for dir in "$FX"/plugins/*/; do
    id="$(basename "$dir")"
    m="${dir%/}/mpc"
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
    if stale "$so" "$id"; then stale_list="$stale_list $id(aarch64)"; fi
    package "$so" aarch64 "$id" "$name" "$skin" "$about" "$m"
    # Gen1 and Force (armv7): the catalog's main download per plugin, and the Gen1 suite
    so32="$m/build/$id.so"
    if [ -f "$so32" ] && file -b "$so32" | grep -q "32-bit.*ARM"; then
        if stale "$so32" "$id"; then stale_list="$stale_list $id(armv7)"; fi
        package "$so32" armv7 "$id" "$name" "$skin" "$about" "$m"
        n32=$((n32 + 1))
    else
        missing32="$missing32 $id"
    fi
    count=$((count + 1))
done
[ "$count" -gt 0 ] || { echo "no plugins found" >&2; exit 1; }
if [ -n "$stale_list" ]; then
    if [ "${OP1_ALLOW_STALE:-}" = 1 ]; then
        echo "warning: packaging builds older than their sources (OP1_ALLOW_STALE=1):$stale_list" >&2
    else
        echo "builds older than their sources (a file in plugins/<id>/core or mpc changed after the build):$stale_list" >&2
        echo "rebuild them (mpc-vst-plugins tools/build_port.sh plugins/<id>/mpc/vst.json all), or set OP1_ALLOW_STALE=1" >&2
        exit 1
    fi
fi

ARCHES="aarch64"; GEN1_ERR=""
if [ "$n32" -eq "$count" ]; then
    ARCHES="aarch64 armv7"
elif [ "$n32" -eq 0 ]; then
    echo "warning: no plugin has an armv7 build (plugins/<id>/mpc/build/<id>.so), so there is no Gen1 suite" >&2
else
    GEN1_ERR="no Gen1 suite: armv7 build missing for:$missing32. Build them (build_port.sh ... all) and run this again"
fi

# check_suite <arch>: everything that can fail for a suite, run for every suite before any zip is written
check_suite() {
    local arch="$1" root dups wrong n
    root="$STAGE/$arch/$(suite_top "$arch")"
    n="$(ls -d "$root"/packages/*/ | wc -l | tr -d ' ')"
    [ "$n" -eq "$count" ] || { echo "the $arch suite has $n packages, expected $count" >&2; exit 1; }
    # every package must name a different uid, or the installer replaces one with the other
    dups="$(cat "$root"/packages/*/mpc-plugin.json | sed -n 's/.*"uid": *"\([0-9a-f]*\)".*/\1/p' | sort | uniq -d)"
    [ -z "$dups" ] || { echo "duplicate MPC uid(s) across $arch plugins: $dups" >&2; exit 1; }
    # and every package must be built for this suite's CPU
    wrong="$(grep -L "\"arch\": *\"$arch\"" "$root"/packages/*/mpc-plugin.json || true)"
    [ -z "$wrong" ] || { echo "not $arch packages in the $arch suite: $wrong" >&2; exit 1; }
    os_line "$arch" >/dev/null
}

# os_line <arch>: the MPC OS versions every package in the suite runs on ("MPC OS 3.x", "MPC OS 2.x and 3.x")
os_line() {
    python3 - "$STAGE/$1/$(suite_top "$1")/packages" <<'EOF'
import glob, json, os, sys
sets = [set(json.load(open(p)).get("os_compat") or []) for p in glob.glob(os.path.join(sys.argv[1], "*", "mpc-plugin.json"))]
common = sorted(set.intersection(*sets)) if sets else []
if not common:
    sys.exit("no MPC OS version that every package in %s supports" % sys.argv[1])
print("MPC OS " + " and ".join(common))
EOF
}

# write_suite <arch>: install-all.sh, uninstall-all.sh, README.md and SHA256SUMS for one suite, then its zip (in $STAGE)
write_suite() {
    local arch="$1" top root gen devices other osl f
    top="$(suite_top "$arch")"
    root="$STAGE/$arch/$top"
    case "$arch" in
        aarch64) gen="Gen2"; devices="MPC Live III and other Gen2 models"; other=armv7 ;;
        armv7)   gen="Gen1"; devices="Force, MPC Live / Live II, MPC One, MPC X, MPC Key 61"; other=aarch64 ;;
        *) echo "write_suite: unknown arch $arch" >&2; exit 1 ;;
    esac
    osl="$(os_line "$arch")"

    # the part both scripts share: options, device checks and MPC control, all before MPC is stopped
    cat > "$STAGE/common.sh" <<'EOF'
set -u
cd "$(dirname "$0")"
YES=0; SYNTHS=""
die() { echo "error: $*" >&2; exit 1; }
while [ $# -gt 0 ]; do
    case "$1" in
        -y) YES=1; shift ;;
        -t) [ -n "${2:-}" ] || die "-t needs a folder"; SYNTHS="$2"; shift 2 ;;
        *) die "usage: sh @SCRIPT@ [-y] [-t <synths-dir>]" ;;
    esac
done
if [ -z "$SYNTHS" ]; then if [ -d /nvme/Synths ]; then SYNTHS=/nvme/Synths; else SYNTHS=/sdcard/Synths; fi; fi
# the per-plugin scripts refuse these too, but only after MPC is stopped: check here so a typo costs no restart
case "$SYNTHS" in /*) ;; *) die "-t must be an absolute path" ;; esac
case "$SYNTHS" in *"&"*|*"|"*|*"\\"*) die "the Synths path may not contain & | or backslash" ;; esac
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
SETTINGS="${MPC_SETTINGS:-$(ls /media/az01-internal/Settings/*/MPC.settings /data/Settings/*/MPC.settings 2>/dev/null | head -n 1)}"
[ -n "$SETTINGS" ] && [ -f "$SETTINGS" ] || die "MPC.settings not found (not an MPC OS device?)"
mpc_service() {
    if systemctl cat acvs >/dev/null 2>&1; then echo acvs
    elif systemctl cat inmusic-mpc >/dev/null 2>&1; then echo inmusic-mpc
    else echo acvs; fi
}
mpc_ctl() {   # a test run logs the call to $MPC_TEST_LOG instead of touching MPC
    if [ -n "${MPC_INSTALL_TEST:-}" ]; then [ -z "${MPC_TEST_LOG:-}" ] || echo "$1" >> "$MPC_TEST_LOG"; return 0; fi
    systemctl "$1" "$(mpc_service)"
}
# stop_mpc <cleanup>: stops MPC; on any exit, Ctrl-C or a dropped SSH session, runs <cleanup> and starts MPC again
stop_mpc() {
    trap "$1; mpc_ctl start" EXIT
    trap 'exit 130' INT HUP TERM   # some shells skip the EXIT trap on a signal; exiting from here runs it
    mpc_ctl stop
    if [ -z "${MPC_INSTALL_TEST:-}" ]; then
        i=0; while pidof MPC >/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
        ! pidof MPC >/dev/null || die "MPC did not stop"
    fi
}
EOF

    { cat <<'EOF'
#!/bin/sh
# Installs every plugin in packages/ with ONE stop and ONE start of MPC. Run on the device as root:
#   sh install-all.sh [-y] [-t <synths-dir>]
# The Synths folder defaults to /nvme/Synths when it exists (Live III), else /sdcard/Synths.
# Save your project first: MPC is stopped and restarted.
EOF
      sed 's/@SCRIPT@/install-all.sh/' "$STAGE/common.sh"
      cat <<'EOF'
sha256sum -c SHA256SUMS >/dev/null 2>&1 || die "files damaged (SHA256SUMS mismatch): copy the folder again"
N=$(ls -d packages/*/ | wc -l | tr -d " ")
echo "Installing $N plugins into $SYNTHS"
if [ $YES = 0 ]; then
    printf "MPC will be stopped and restarted once. Save your project first. Continue? [y/N] "
    read -r ok; case "$ok" in y|Y|yes) ;; *) echo "cancelled"; exit 1 ;; esac
fi
LOG=$(mktemp) || die "cannot create a temp file"
stop_mpc 'rm -f "$LOG"'
OK=0; FAIL=""; WARN=""
for p in packages/*/; do
    n=$(basename "$p")
    if (cd "$p" && sh install.sh -y -n -t "$SYNTHS" >"$LOG" 2>&1); then
        OK=$((OK + 1)); echo "  ok   $n"
        # the same warning (noexec drive, Synths folder not in MPC's content locations) comes from every plugin: show it once
        w=$(grep '^warning:' "$LOG" || true)
        if [ -n "$w" ] && [ "$w" != "$WARN" ]; then echo "$w" | sed 's/^/       /'; WARN="$w"; fi
    else
        FAIL="$FAIL $n"; echo "  FAIL $n"; tail -n 3 "$LOG" | sed 's/^/       /'
    fi
done
echo "$OK of $N installed."
[ -z "$FAIL" ] || { echo "failed:$FAIL" >&2; exit 1; }
echo "Starting MPC. Add the plugins from the plugin browser."
EOF
    } > "$root/install-all.sh"

    { cat <<'EOF'
#!/bin/sh
# Removes every plugin in packages/ with ONE stop and ONE start of MPC. Run on the device as root:
#   sh uninstall-all.sh [-y] [-t <synths-dir>]
EOF
      sed 's/@SCRIPT@/uninstall-all.sh/' "$STAGE/common.sh"
      cat <<'EOF'
if [ $YES = 0 ]; then
    printf "Remove all plugins in this suite from %s? MPC will be stopped and restarted once. [y/N] " "$SYNTHS"
    read -r ok; case "$ok" in y|Y|yes) ;; *) echo "cancelled"; exit 1 ;; esac
fi
stop_mpc :
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
    } > "$root/uninstall-all.sh"

    # the arch, generation and version go into the scripts after the quoted heredocs, so nothing else in them expands
    for f in install-all.sh uninstall-all.sh; do
        sed -e "s|@ARCH@|$arch|g" -e "s|@GEN@|$gen|g" -e "s|@OTHER@|$other|g" -e "s|@VERSION@|$VERSION|g" \
            "$root/$f" > "$root/$f.new"
        mv "$root/$f.new" "$root/$f"
        sh -n "$root/$f" || { echo "generated $arch $f is not valid sh" >&2; exit 1; }
    done
    if grep -q '@[A-Z]*@' "$root/install-all.sh" "$root/uninstall-all.sh"; then
        echo "unreplaced placeholder in the $arch suite scripts" >&2; exit 1
    fi

    cat > "$root/README.md" <<EOF
# OP-1 Suite $VERSION for MPC ($gen, $arch)

$count plugins in one package, for $devices running $osl. The other generation needs
\`OP1-Suite-$VERSION-mpc-$other.zip\`. Copy this folder to the MPC, then on the device as root:

    sh install-all.sh -y

MPC stops once, every plugin installs, MPC starts once. Save your project first. Safe to run again: each plugin is
replaced by uid, and files you added inside a plugin folder are kept. \`uninstall-all.sh\` removes them all.

Synths folder: /nvme/Synths when it exists (Live III), else /sdcard/Synths. Override with \`-t <folder>\`. On a Force,
keep the default: MPC can't load a plugin from a noexec drive such as the Force's SSD.
Each plugin also installs alone: \`packages/<name>/install.sh\` (see its INSTALL.md).
EOF

    # the suite's own list; each package's SHA256SUMS is a file like any other and is covered too
    ( cd "$root" && find . -type f ! -path ./SHA256SUMS -print0 | sort -z | xargs -0 shasum -a 256 | sed 's|  \./|  |' > SHA256SUMS )
    chmod 755 "$root/install-all.sh" "$root/uninstall-all.sh"
    ( cd "$STAGE/$arch" && zip -qr -X "$STAGE/OP1-Suite-$VERSION-mpc-$arch.zip" "$top" )
    echo "OP1-Suite-$VERSION-mpc-$arch.zip: $count plugins, $gen, $osl"
}

for arch in $ARCHES; do check_suite "$arch"; done
for arch in $ARCHES; do write_suite "$arch"; done

# every check passed: replace the release in $OUT. A Gen1 suite from an earlier run goes when this run has none, so it
# can't sit next to a release it doesn't match.
mkdir -p "$OUT"
case " $ARCHES " in *" armv7 "*) ;; *) rm -f "$OUT/OP1-Suite-$VERSION-mpc-armv7.zip" ;; esac
for arch in $ARCHES; do
    mv -f "$STAGE/OP1-Suite-$VERSION-mpc-$arch.zip" "$OUT/"
    echo "$OUT/OP1-Suite-$VERSION-mpc-$arch.zip ($(du -h "$OUT/OP1-Suite-$VERSION-mpc-$arch.zip" | cut -f1))"
done
# the per-plugin zips are what the community catalog lists; keep them next to the suites
rel_out="$OUT/release-$VERSION"
rm -rf "$rel_out"; mkdir -p "$rel_out"
cp "$STAGE"/zips/*.zip "$rel_out"/

if [ -n "$GEN1_ERR" ]; then echo "$GEN1_ERR" >&2; exit 1; fi
