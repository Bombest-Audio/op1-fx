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
# manifest; this script adds the plugin.json MPC OS 3.10.1 needs (add_plugin_json). A suite adds install-all.sh and
# uninstall-all.sh, which install every plugin between one stop and one start of MPC, either into the drive's
# "VST2 Plugins" folder (3.10.1) or through the per-plugin install.sh (install.sh -n, the Synths folder and MPC.settings).
# Nothing in dist/ changes unless every check passes.
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

# add_plugin_json <zip> <arch>: puts the plugin.json MPC OS 3.10.1 reads (a plugin folder in a drive's "VST2 Plugins" needs
# one) into the package's portable/<skin>/ folder, and its checksum into the package's SHA256SUMS. The zip is rebuilt entry
# by entry with release.py's dates and modes kept. Name, manufacturer, version and the .so come from mpc-plugin.json;
# category, instrument or effect, and channel counts from plugin-meta.xml (the entry the Synths route writes). The uid is
# written as "0x" + hex: mpc-plugin.json keeps bare hex digits, which 3.10.1 would read as a decimal number.
add_plugin_json() {
    python3 - "$1" "$2" <<'EOF'
import hashlib, json, os, re, sys, zipfile
path, arch = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(path) as src:
    infos = src.infolist()
    data = {i.filename: src.read(i) for i in infos}
top = infos[0].filename.split("/")[0]
m = json.loads(data[f"{top}/mpc-plugin.json"])
folder, so, uid = m["folder"], m["so"], m["uid"]
if not re.fullmatch(r"[0-9a-f]{8}", uid):
    sys.exit(f"{path}: uid {uid!r} is not 8 hex digits")
if os.path.basename(folder) != f'{m["manufacturer"]} - VST - {m["name"]}':
    sys.exit(f"{path}: folder {folder!r} is not '<manufacturer> - VST - <name>', which 3.10.1 expects")
meta = data[f"{top}/{folder}/plugin-meta.xml"].decode()
def attr(k):
    v = re.search(r'\b%s="([^"]*)"' % k, meta)
    if not v:
        sys.exit(f"{path}: plugin-meta.xml has no {k}")
    return v.group(1)
inst = attr("isInstrument") == "1"
pj = {
    "name": m["name"], "manufacturer": m["manufacturer"], "version": m["version"], "category": attr("category"),
    "isInstrument": inst, "kind": "instrument" if inst else "effect",
    "numInputs": int(attr("numInputs")), "numOutputs": int(attr("numOutputs")), "uid": "0x" + uid,
    {"armv7": "file32", "aarch64": "file64"}[arch]: so, "file": so,
}
body = (json.dumps(pj, indent=2) + "\n").encode()
name = f"{top}/{folder}/plugin.json"
if name in data:
    sys.exit(f"{path}: already has {name}")
sums = data[f"{top}/SHA256SUMS"].decode().splitlines()
sums.append(f"{hashlib.sha256(body).hexdigest()}  {folder}/plugin.json")
sums.sort(key=lambda line: line.split("  ", 1)[1])
data[f"{top}/SHA256SUMS"] = ("\n".join(sums) + "\n").encode()
like = next(i for i in infos if i.filename == f"{top}/{folder}/plugin-meta.xml")
tmp = path + ".tmp"
with zipfile.ZipFile(tmp, "w") as out:
    for i in infos:
        out.writestr(i, data[i.filename])
        if i is like:
            new = zipfile.ZipInfo(name, date_time=like.date_time)
            new.compress_type, new.external_attr, new.create_system = like.compress_type, like.external_attr, like.create_system
            out.writestr(new, body)
os.replace(tmp, path)
EOF
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
    add_plugin_json "${z[0]}" "$arch"
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
    vendor="$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['vendor'])" "$m/vst.json")"
    skin="$m/build/skin/$vendor - VST - $name"
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
    check_plugin_json "$arch" "$root/packages"
    os_line "$arch" >/dev/null
}

# check_plugin_json <arch> <packages dir>: every package's plugin.json passes the checks MPC OS 3.10.1 makes
# (firmware/notes/3.10.1-plugin-json.md in the mpc workspace), and every package still matches its SHA256SUMS
check_plugin_json() {
    python3 - "$1" "$2" <<'EOF'
import hashlib, json, os, sys
arch, packages = sys.argv[1], sys.argv[2]
key = {"armv7": "file32", "aarch64": "file64"}[arch]
bad = []
for pkg in sorted(os.listdir(packages)):
    p = os.path.join(packages, pkg)
    m = json.load(open(os.path.join(p, "mpc-plugin.json")))
    folder = os.path.join(p, m["folder"])
    try:
        pj = json.load(open(os.path.join(folder, "plugin.json")))
    except (OSError, ValueError) as e:
        bad.append(f"{pkg}: no usable plugin.json ({e})"); continue
    want = {"name": m["name"], "manufacturer": m["manufacturer"], "uid": "0x" + m["uid"], key: m["so"], "file": m["so"]}
    bad += [f"{pkg}: plugin.json {k} is {pj.get(k)!r}, expected {v!r}" for k, v in want.items() if pj.get(k) != v]
    if os.path.basename(folder) != f'{pj.get("manufacturer")} - VST - {pj.get("name")}':
        bad.append(f"{pkg}: folder {os.path.basename(folder)!r} doesn't match plugin.json's manufacturer and name")
    lib = os.path.join(folder, str(pj.get(key)))
    if not os.path.isfile(lib) or os.path.islink(lib) or os.path.dirname(os.path.realpath(lib)) != os.path.realpath(folder):
        bad.append(f"{pkg}: {key} {pj.get(key)!r} is not a plain file directly in the plugin folder")
    for line in open(os.path.join(p, "SHA256SUMS")):
        digest, rel = line.rstrip("\n").split("  ", 1)
        if hashlib.sha256(open(os.path.join(p, rel), "rb").read()).hexdigest() != digest:
            bad.append(f"{pkg}: {rel} doesn't match its SHA256SUMS")
    if not any(l.endswith(m["folder"] + "/plugin.json\n") for l in open(os.path.join(p, "SHA256SUMS"))):
        bad.append(f"{pkg}: plugin.json is not in SHA256SUMS")
if bad:
    sys.exit("\n".join(bad))
EOF
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
YES=0; SYNTHS=""; VST2=""; ROUTE=auto; CLEAN=0
die() { echo "error: $*" >&2; exit 1; }
while [ $# -gt 0 ]; do
    case "$1" in
        -y) YES=1; shift ;;
        -c) CLEAN=1; shift ;;
        -r) [ -n "${2:-}" ] || die "-r needs auto, vst2 or synths"; ROUTE="$2"; shift 2 ;;
        -t) [ -n "${2:-}" ] || die "-t needs a folder"; SYNTHS="$2"; shift 2 ;;
        -v) [ -n "${2:-}" ] || die "-v needs a folder"; VST2="$2"; shift 2 ;;
        *) die "usage: sh @SCRIPT@ @OPTS@" ;;
    esac
done
case "$ROUTE" in auto|vst2|synths) ;; *) die "-r must be auto, vst2 or synths" ;; esac
if [ -z "$SYNTHS" ]; then if [ -d /nvme/Synths ]; then SYNTHS=/nvme/Synths; else SYNTHS=/sdcard/Synths; fi; fi
# MPC OS 3.10.1 lists plugins from a "VST2 Plugins" folder at a drive's root: the internal drive, /nvme on Gen2 and
# /media/acvs-content on Gen1 (firmware/notes/3.10.1-device-installs.md in the mpc workspace)
VST2_DEFAULT=0
if [ -z "$VST2" ]; then
    VST2_DEFAULT=1
    case "@ARCH@" in aarch64) VST2="/nvme/VST2 Plugins" ;; *) VST2="/media/acvs-content/VST2 Plugins" ;; esac
fi
# the per-plugin scripts refuse these too, but only after MPC is stopped: check here so a typo costs no restart
for d in "$SYNTHS" "$VST2"; do
    case "$d" in /*) ;; *) die "-t and -v need an absolute path, not $d" ;; esac
    case "$d" in *"&"*|*"|"*|*"\\"*) die "a plugin folder path may not contain & | or backslash: $d" ;; esac
done
[ "${SYNTHS%/}" != "${VST2%/}" ] || die "-t and -v name the same folder: $SYNTHS"
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
# 3.10.1 lists plugins only from "VST2 Plugins", and loads them only with Enhancements and External Plugin Support both
# on (Preferences > Enhancements). MPC.settings carries the Enhancements.VST2Support key from 3.10.1 on (3.9.x has no such
# setting), so its presence picks the vst2 route even while the switch is off: the Synths route does nothing on 3.10.1.
vst2_on() {
    grep -q 'name="Enhancements.Enabled" val="1"' "$SETTINGS" && grep -q 'name="Enhancements.VST2Support" val="1"' "$SETTINGS"
}
if [ "$ROUTE" = auto ]; then
    if grep -q 'name="Enhancements.VST2Support"' "$SETTINGS"; then ROUTE=vst2; else ROUTE=synths; fi
fi

# pkg_val <package> <key>: a string from the package's mpc-plugin.json (the device has no python)
pkg_val() { sed -n "s/.*\"$2\": *\"\([^\"]*\)\".*/\1/p" "$1/mpc-plugin.json" | head -n 1; }
# user_data <package>: the plugin's own files that survive a reinstall (its install.sh's USER_DATA)
user_data() { sed -n "s/.*USER_DATA='\([^']*\)'.*/\1/p" "$1/install.sh" | head -n 1; }
# ours <folder> <uid>: the folder holds the plugin with this uid (its plugin.json or mpc-plugin.json says so)
ours() { grep -qi "\"uid\": *\"0x$2\"" "$1/plugin.json" 2>/dev/null || grep -qi "\"uid\": *\"$2\"" "$1/mpc-plugin.json" 2>/dev/null; }
# The maker name was "bombest" up to 0.5.0, so a device can hold this plugin as "bombest - VST - <Name>" too
LEGACY_MAKERS="bombest"
# names <package>: every folder name this plugin has had, the current one first, one per line
names() { pkg_val "$1" skin; for m in $LEGACY_MAKERS; do echo "$m - VST - $(pkg_val "$1" name)"; done; }
# is_ours <package> <folder>: the folder holds this plugin: its manifest names the uid, or (an install from before the
# manifests) MPC.settings registers the uid at that exact path
is_ours() {
    [ -d "$2" ] || return 1
    ours "$2" "$(pkg_val "$1" uid)" && return 0
    grep " uid=\"$(pkg_val "$1" uid)\"" "$SETTINGS" | grep -qF "file=\"$2/$(pkg_val "$1" so)\""
}
# copies <package> <parent>: this plugin's folders in <parent>, under any of its names, one per line
copies() { names "$1" | while IFS= read -r n; do if is_ours "$1" "$2/$n"; then echo "$2/$n"; fi; done; }
# each <package> <parent> <function>: runs <function> <package> <folder> for every copy of the plugin in <parent>
# (a here-document, not a pipe, so a failure returns from the caller and the loop runs in this shell)
each() {
    list=$(copies "$1" "$2")
    while IFS= read -r c; do
        [ -n "$c" ] || continue
        "$3" "$1" "$c" || return 1
    done <<LIST
$list
LIST
}
# synths_entry <package>: MPC.settings lists the plugin's uid (its folder may be in another Synths folder)
synths_entry() { grep -q " uid=\"$(pkg_val "$1" uid)\"" "$SETTINGS"; }
# noexec <folder>: the folder's drive is mounted noexec, so MPC lists a plugin there but may not load it
noexec() {
    mp=$(df -kP "$1" 2>/dev/null | awk 'NR==2 {m = $6; for (i = 7; i <= NF; i++) m = m " " $i; print m}' | sed 's/ /\\040/g')
    [ -n "$mp" ] && MP="$mp" awk '$2 == ENVIRON["MP"] {print $4}' /proc/mounts 2>/dev/null | grep -q '\(^\|,\)noexec\(,\|$\)'
}

# recover <dir>: an earlier run cut off mid-swap (a power cut) can leave the only copy of a plugin folder, or the user's
# own files, in a hidden staging folder next to it (this script's and install.sh's use the same names): put it back,
# then clear what's left
recover() {
    up=$(dirname "$1"); b=$(basename "$1")
    for h in "$up/.$b.old" "$up/.$b.keep"; do
        if [ -d "$h" ] && [ ! -e "$1" ]; then mv "$h" "$1" || return 1; fi
    done
    rm -rf "$up/.$b.new" "$up/.$b.old" "$up/.$b.keep"
}
# recover_all <package>: recover under every name, in both places
recover_all() { names "$1" | while IFS= read -r n; do recover "$VST2/$n" && recover "$SYNTHS/$n" || exit 1; done; }

# drop <package> <dir>: removes a folder of this plugin, keeping its USER_DATA files there
drop() {
    is_ours "$1" "$2" || return 0
    keep="$(dirname "$2")/.$(basename "$2").keep"
    for d in $(user_data "$1"); do
        if [ -e "$2/$d" ]; then mkdir -p "$(dirname "$keep/$d")" && mv "$2/$d" "$keep/$d" || return 1; fi
    done
    rm -rf "$2" || return 1
    if [ -d "$keep" ]; then mv "$keep" "$2" || return 1; echo "kept your own files in $2"; fi
    return 0
}

# place <package> <dir>: puts the package's plugin folder at <dir>, staged next to it and then swapped in, so nothing
# old goes before the new folder is in place. The user's own files (USER_DATA) come from <dir>, else from the first other
# copy of the plugin that has them. MPC.settings is not touched.
place() {
    p=$1; dir=$2
    skin=$(pkg_val "$p" skin); src="$p/portable/$skin"
    up=$(dirname "$dir"); stage="$up/.$(basename "$dir").new"; old="$up/.$(basename "$dir").old"
    [ -n "$skin" ] && [ -f "$src/plugin.json" ] || { echo "error: $p has no plugin.json for MPC OS 3.10.1"; return 1; }
    if [ -d "$dir" ] && ! is_ours "$p" "$dir"; then echo "error: $dir holds another plugin, not replaced"; return 1; fi
    mkdir -p "$up" || return 1
    rm -rf "$stage" "$old"
    cp -a "$src" "$stage" || return 1
    if [ -f "$p/MODES" ]; then   # a zip unpacked on Windows or copied file by file loses exec bits and symlinks: put them back
        tab=$(printf '\t')
        while IFS=$tab read -r kind rel target; do
            case "$rel" in ""|/*|..|../*|*/..|*/../*) continue ;; esac
            if [ -f "$stage/$rel" ] && [ ! -L "$stage/$rel" ]; then
                if [ "$kind" = x ]; then chmod 755 "$stage/$rel"
                elif [ "$kind" = l ]; then rm -f "$stage/$rel"; ln -s "$target" "$stage/$rel"; fi
            fi
        done < "$p/MODES"
    fi
    vsed=$(printf '%s' "$up" | sed 's/[|&\\]/\\&/g')
    for f in "$stage"/Presets/*.xpl; do   # shipped presets name the plugin's folder with a placeholder
        [ -f "$f" ] || continue
        sed "s|%payload-path%|$vsed|g" "$f" > "$f.new" && mv "$f.new" "$f" || return 1
    done
    for d in $(user_data "$p"); do
        { echo "$dir"; copies "$p" "$VST2"; copies "$p" "$SYNTHS"; } | while IFS= read -r from; do
            if [ -e "$from/$d" ]; then
                rm -rf "$stage/$d"; mkdir -p "$(dirname "$stage/$d")" && cp -a "$from/$d" "$stage/$d" || exit 1; break
            fi
        done || return 1
    done
    if [ -e "$dir" ]; then mv "$dir" "$old" || return 1; fi
    if ! mv "$stage" "$dir"; then [ ! -e "$old" ] || mv "$old" "$dir"; return 1; fi
    rm -rf "$old"
    sync
}

# not_current <package> <dir>: true for a copy under an old name
not_current() { [ "$(basename "$2")" != "$(pkg_val "$1" skin)" ]; }
drop_old() { if not_current "$1" "$2"; then drop "$1" "$2"; fi; }
place_old() { if not_current "$1" "$2"; then place "$1" "$2"; fi; }

# vst2_install <package>: the plugin into VST2 Plugins under its current name. A copy there under an old name goes once
# the new folder is in place: MPC lists every folder in VST2 Plugins, so two names would show the plugin twice.
vst2_install() { place "$1" "$VST2/$(pkg_val "$1" skin)" && each "$1" "$VST2" drop_old; }
# vst2_remove <package>: the plugin out of VST2 Plugins under every name, keeping the user's own files
vst2_remove() { each "$1" "$VST2" drop; }
# synths_remove <package>: the plugin out of the Synths folder under every name, with its MPC.settings entry. A folder of
# its current name that holds another plugin is left alone, and so is the entry (install.sh would remove that folder).
synths_remove() {
    each "$1" "$SYNTHS" drop_old || return 1
    cur="$SYNTHS/$(pkg_val "$1" skin)"
    if [ -d "$cur" ] && ! is_ours "$1" "$cur"; then echo "warning: $cur holds another plugin, left alone"; return 0; fi
    if [ -d "$cur" ] || synths_entry "$1"; then (cd "$1" && sh uninstall.sh -y -n -t "$SYNTHS") || return 1; fi
}

# install_one <package>: installs the plugin the $ROUTE way, then deals with its copies elsewhere.
# A project saved on 3.9.x stores the absolute path of each plugin's .so (for example /nvme/Synths/bombest - VST - Punch/
# punch.so), and whether 3.10.1 opens it from that path or by uid from VST2 Plugins is not known. So by default every
# other copy, under any of the plugin's names, is updated in place to this build and keeps its folder name: no saved
# path goes missing and every copy runs the same code. That doesn't touch MPC.settings. 3.10.1 lists only VST2 Plugins,
# where a copy under an old name is always removed; 3.9.x lists only MPC.settings' plugin list, where install.sh keeps
# one entry per uid. With -c the other copies are removed instead. A folder that holds another plugin is never touched.
install_one() {
    recover_all "$1" || return 1
    if [ "$ROUTE" = vst2 ]; then
        vst2_install "$1" || return 1
        if [ "$CLEAN" = 1 ]; then synths_remove "$1"; else each "$1" "$SYNTHS" place; fi
    else
        (cd "$1" && sh install.sh -y -n -t "$SYNTHS") || return 1   # its entry replaces any entry with this uid
        if [ "$CLEAN" = 1 ]; then
            each "$1" "$SYNTHS" drop_old && vst2_remove "$1"
        else
            each "$1" "$SYNTHS" place_old || return 1
            if [ -n "$(copies "$1" "$VST2")" ]; then vst2_install "$1"; fi
        fi
    fi
}
# uninstall_one <package>: the plugin out of both places, under every name
uninstall_one() { recover_all "$1" && vst2_remove "$1" && synths_remove "$1"; }

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
    trap 'exit 130' INT HUP TERM PIPE   # some shells skip the EXIT trap on a signal; exiting from here runs it (the
                                        # plugin loop replaces this with a trap that finishes the plugin in progress first).
                                        # PIPE: output piped to a reader that dies (tee, a dropped ssh) must not skip it
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
#   sh install-all.sh [-y] [-c] [-r auto|vst2|synths] [-t <synths-dir>] [-v <vst2-plugins-dir>]
# Two routes. vst2 (MPC OS 3.10.1): each plugin folder goes into the internal drive's "VST2 Plugins" folder
# (/nvme/VST2 Plugins on Gen2, /media/acvs-content/VST2 Plugins on Gen1). synths (3.9.x): each plugin folder goes into the
# Synths folder (/nvme/Synths when it exists, else /sdcard/Synths) with an entry in MPC.settings' plugin list.
# auto (the default) takes vst2 on MPC OS 3.10.1, else synths.
# A copy of a plugin installed the other way is updated too (projects saved on 3.9.x point at its path); -c removes it
# instead. Save your project first: MPC is stopped and restarted once.
EOF
      sed -e 's/@SCRIPT@/install-all.sh/' -e 's#@OPTS@#[-y] [-c] [-r auto|vst2|synths] [-t <synths-dir>] [-v <vst2-plugins-dir>]#' "$STAGE/common.sh"
      cat <<'EOF'
sha256sum -c SHA256SUMS >/dev/null 2>&1 || die "files damaged (SHA256SUMS mismatch): copy the folder again"
N=$(ls -d packages/*/ | wc -l | tr -d " ")
if [ "$ROUTE" = vst2 ]; then
    DRIVE=$(dirname "$VST2")
    [ -d "$DRIVE" ] || die "$DRIVE not found: is this MPC OS 3.10.1? Name the VST2 Plugins folder with -v, or use -r synths"
    if [ $VST2_DEFAULT = 1 ] && [ -z "${MPC_INSTALL_TEST:-}" ]; then   # an unmounted mount point is part of the system image
        awk -v d="$DRIVE" '$2 == d {f = 1} END {exit !f}' /proc/mounts ||
            die "$DRIVE is not mounted, so MPC won't scan it: name the VST2 Plugins folder with -v"
    fi
    mkdir -p "$VST2" && [ -w "$VST2" ] || die "cannot write to $VST2"

    vst2_on || echo "warning: External Plugin Support is off: turn on Menu > Preferences > Enhancements > Enable Enhancements and External Plugin Support, or MPC won't list the plugins"
    ! noexec "$DRIVE" || echo "warning: $DRIVE is mounted noexec: MPC may list the plugins there but not load them"
    echo "Installing $N plugins into $VST2 (MPC OS 3.10.1 VST2 Plugins)"
else
    echo "Installing $N plugins into $SYNTHS"
fi
# a folder of a plugin's current name that holds another plugin is never replaced: say so before MPC stops
if [ "$ROUTE" = vst2 ]; then TARGET=$VST2; else TARGET=$SYNTHS; fi
for p in packages/*/; do
    d="$TARGET/$(pkg_val "$p" skin)"
    if [ -d "$d" ] && ! is_ours "$p" "$d"; then die "$d holds another plugin, not this suite's: move it first"; fi
done
if [ $YES = 0 ]; then
    printf "MPC will be stopped and restarted once. Save your project first. Continue? [y/N] "
    read -r ok; case "$ok" in y|Y|yes) ;; *) echo "cancelled"; exit 1 ;; esac
fi
LOG=$(mktemp) || die "cannot create a temp file"
stop_mpc 'rm -f "$LOG"'
OK=0; FAIL=""; WARN=""; STOP=""
# a signal from here on waits for the plugin in progress: install.sh cut off between its folder swap and its MPC.settings
# write would leave the two out of step. The plugin's install runs with the signals ignored (inherited through exec),
# this loop notes the signal and stops after that plugin, and the EXIT trap starts MPC.
trap 'STOP=1' INT HUP TERM PIPE
for p in packages/*/; do
    [ -z "$STOP" ] || break
    n=$(basename "$p")
    if (trap '' INT HUP TERM PIPE; install_one "$p") >"$LOG" 2>&1; then
        OK=$((OK + 1)); echo "  ok   $n"
        # the same warning (noexec drive, Synths folder not in MPC's content locations) comes from every plugin: show it once
        w=$(grep '^warning:' "$LOG" || true)
        if [ -n "$w" ] && [ "$w" != "$WARN" ]; then echo "$w" | sed 's/^/       /'; WARN="$w"; fi
    else
        FAIL="$FAIL $n"; echo "  FAIL $n"; tail -n 3 "$LOG" | sed 's/^/       /'
    fi
done
echo "$OK of $N installed."
[ -z "$STOP" ] || { echo "interrupted: stopped after the plugin in progress, the rest are not installed" >&2; exit 130; }
[ -z "$FAIL" ] || { echo "failed:$FAIL" >&2; exit 1; }
echo "Starting MPC. Add the plugins from the plugin browser."
EOF
    } > "$root/install-all.sh"

    { cat <<'EOF'
#!/bin/sh
# Removes every plugin in packages/ with ONE stop and ONE start of MPC, from both the VST2 Plugins folder and the Synths
# folder (with its MPC.settings entry), whichever way each was installed. Run on the device as root:
#   sh uninstall-all.sh [-y] [-t <synths-dir>] [-v <vst2-plugins-dir>]
EOF
      sed -e 's/@SCRIPT@/uninstall-all.sh/' -e 's#@OPTS@#[-y] [-t <synths-dir>] [-v <vst2-plugins-dir>]#' "$STAGE/common.sh"
      cat <<'EOF'
if [ $YES = 0 ]; then
    printf "Remove all plugins in this suite from %s and %s? MPC will be stopped and restarted once. [y/N] " "$VST2" "$SYNTHS"
    read -r ok; case "$ok" in y|Y|yes) ;; *) echo "cancelled"; exit 1 ;; esac
fi
LOG=$(mktemp) || die "cannot create a temp file"
stop_mpc 'rm -f "$LOG"'
FAIL=""; STOP=""
trap 'STOP=1' INT HUP TERM PIPE   # as in install-all.sh: a signal waits for the plugin in progress, then stops
for p in packages/*/; do
    [ -z "$STOP" ] || break
    n=$(basename "$p")
    if [ -f "$p/uninstall.sh" ]; then
        if (trap '' INT HUP TERM PIPE; uninstall_one "$p") >"$LOG" 2>&1; then
            echo "  removed $n"
            grep '^warning:' "$LOG" | sed 's/^/       /'
        else FAIL="$FAIL $n"; echo "  FAIL    $n"; tail -n 3 "$LOG" | sed 's/^/       /'; fi
    fi
done
[ -z "$STOP" ] || { echo "interrupted: stopped after the plugin in progress, the rest are still installed" >&2; exit 130; }
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
replaced by uid. \`uninstall-all.sh\` removes them all, whichever way they were installed.

Where the plugins go:

- **MPC OS 3.10.1:** each plugin folder goes into the internal drive's \`VST2 Plugins\` folder, \`/nvme/VST2 Plugins\` on
  Gen2 and \`/media/acvs-content/VST2 Plugins\` on Gen1. Override the folder with \`-v <folder>\`. MPC lists them only with
  Menu > Preferences > Enhancements > Enable Enhancements and External Plugin Support both ticked.
- **MPC OS 3.9.x:** each plugin folder goes into the Synths folder, \`/nvme/Synths\` when it exists, else
  \`/sdcard/Synths\`, with an entry in MPC.settings. Override with \`-t <folder>\`. On a Force, keep the default: MPC
  can't load a plugin from a noexec drive such as the Force's SSD.

\`-r vst2\` or \`-r synths\` picks the route yourself. A copy of a plugin already installed the other way is updated
to this version too, because projects saved on 3.9.x store the path of the plugin they used (often \`/nvme/Synths\`).
Add \`-c\` to remove the other copy instead, once your old projects open with the new one. Up to 0.5.0 the plugin
folders were named \`bombest - VST - <Name>\`; those count as the same plugin (matched by uid). In \`VST2 Plugins\` the old
name is replaced, so MPC never lists a plugin twice; elsewhere an old-name copy is updated like any other copy. Each plugin also installs alone the
Synths way: \`packages/<name>/install.sh\` (see its INSTALL.md).
Its \`portable/<folder>/\` has the \`plugin.json\` 3.10.1 needs, so that folder can also be copied into \`VST2 Plugins\` by
hand.
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
