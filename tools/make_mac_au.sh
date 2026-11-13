#!/usr/bin/env bash
# Package all 20 AUs for Logic Pro on the Mac as one signed, notarized zip.
#   tools/make_mac_au.sh <version> [build dir]     build dir defaults to build/all; writes dist/mac/OP1-Suite-<version>-mac-au.zip
# Needs the AUs built first (cmake --build build/all), the Developer ID Application identity in the login keychain and a
# notarytool keychain profile (OP1_NOTARY_PROFILE, default notarytool). AU bundles get the plain Developer ID recipe:
# no Hardened Runtime, no entitlements, since macOS drops entitlements from MH_BUNDLE binaries anyway.
# The factory presets ship as grouped .aupreset files (tools/make_aupresets.swift), read from the AUs installed on this
# Mac, so run install.sh from a fresh build before packaging.
# There is no Developer ID Installer identity on this machine, so this ships a zip with an install.sh, not a .pkg.
set -euo pipefail
VERSION="${1:?usage: tools/make_mac_au.sh <version> [build dir]}"
FX="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${2:-$FX/build/all}"
IDENTITY="${OP1_CODESIGN_IDENTITY:-Developer ID Application: Bombest, LLC (8C4A8V568P)}"
PROFILE="${OP1_NOTARY_PROFILE:-notarytool}"
OUT="$FX/dist/mac"
TOP="OP1-Suite-$VERSION-mac-au"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/$TOP/Components" "$OUT"

count=0
for c in "$BUILD"/plugins/*/*_artefacts/Release/AU/*.component; do
    name="$(basename "$c" .component)"
    bin="$c/Contents/MacOS/$name"
    archs="$(lipo -archs "$bin")"
    [[ "$archs" == *x86_64* && "$archs" == *arm64* ]] || { echo "$name is not universal ($archs)" >&2; exit 1; }
    minos="$(vtool -show-build "$bin" | awk '/minos/{print $2; exit}')"
    [ "$minos" = "11.0" ] || { echo "$name has minos $minos, expected 11.0" >&2; exit 1; }
    ditto "$c" "$STAGE/$TOP/Components/$name.component"
    count=$((count + 1))
done
[ "$count" -eq 20 ] || { echo "found $count AUs in $BUILD, expected 20" >&2; exit 1; }

for c in "$STAGE/$TOP/Components"/*.component; do
    xattr -rc "$c"
    codesign --sign "$IDENTITY" --timestamp --deep --force "$c"
    codesign --verify --deep --strict "$c"
done

# Apple notarizes the zip as a whole; the ticket is then stapled to each component so they verify offline.
ditto -c -k --keepParent "$STAGE/$TOP/Components" "$STAGE/notarize.zip"
xcrun notarytool submit "$STAGE/notarize.zip" --keychain-profile "$PROFILE" --wait | tee "$STAGE/notary.log"
grep -q "status: Accepted" "$STAGE/notary.log" || { echo "notarization was not accepted" >&2; exit 1; }
for c in "$STAGE/$TOP/Components"/*.component; do
    xcrun stapler staple -q "$c"
    spctl --assess --type install "$c"
done

# .aupreset files give Logic folders in its preset menu; the AU factory list is flat. The generator reads the AUs that are
# registered on this Mac, not the staged copies, so the output is checked against the source presets.json files: a stale or
# missing install changes the counts and fails the package.
swiftc -O "$FX/tools/make_aupresets.swift" -o "$STAGE/make_aupresets"
"$STAGE/make_aupresets" "$STAGE/$TOP/Presets"
want_plugins=0
want_presets=0
for j in "$FX"/plugins/*/mpc/presets.json; do
    n="$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["presets"]))' "$j")"
    if [ "$n" -gt 1 ]; then
        want_plugins=$((want_plugins + 1))
        want_presets=$((want_presets + n))
    fi
done
got_plugins="$(find "$STAGE/$TOP/Presets/Bombest" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')"
got_presets="$(find "$STAGE/$TOP/Presets" -name '*.aupreset' | wc -l | tr -d ' ')"
if [ "$got_plugins" != "$want_plugins" ] || [ "$got_presets" != "$want_presets" ]; then
    echo "presets do not match the source: $got_plugins plugins / $got_presets presets, expected $want_plugins / $want_presets; reinstall the AUs from this build" >&2
    exit 1
fi
# Manifest of the factory files in this release; install.sh uses it to retire files a later release drops or renames.
(cd "$STAGE/$TOP/Presets/Bombest" && find . -name '*.aupreset' | sort) > "$STAGE/$TOP/Presets/manifest.txt"

cat > "$STAGE/$TOP/install.sh" <<'EOF'
#!/bin/sh
# Copies the AUs and their grouped presets into your user Library and makes Logic rescan them on next launch.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
dest="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$dest"
for c in "$here"/Components/*.component; do
    rm -rf "$dest/$(basename "$c")"
    ditto "$c" "$dest/$(basename "$c")"
    echo "installed $(basename "$c")"
done
presets="$HOME/Library/Audio/Presets/Bombest"
mkdir -p "$presets"
# Remove only the factory files an earlier release installed (listed in its manifest), so presets you saved stay.
if [ -f "$presets/.op1-suite-manifest" ]; then
    while IFS= read -r f; do
        rm -f "$presets/$f"
    done < "$presets/.op1-suite-manifest"
    find "$presets" -type d -empty -delete 2>/dev/null || true
    mkdir -p "$presets"
fi
ditto "$here/Presets/Bombest" "$presets"
cp "$here/Presets/manifest.txt" "$presets/.op1-suite-manifest"
echo "installed presets"
rm -rf "$HOME/Library/Caches/com.apple.audio.AudioUnitCache"
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Done. Quit and reopen Logic Pro to see the plugins."
EOF
chmod +x "$STAGE/$TOP/install.sh"
cat > "$STAGE/$TOP/README.txt" <<EOF
OP1 plugins $VERSION, Audio Units for Logic Pro (macOS 11.0+, Apple Silicon and Intel).

Install: open Terminal, drag install.sh into it, press Return.
Or copy everything in Components/ to ~/Library/Audio/Plug-Ins/Components yourself.
Presets are grouped in folders (01 Vocals, 02 Drums, 03 Bass and Keys, 04 FX) in the plugin's preset menu.
Effects show up under Audio Units > Bombest, instruments under the instrument slot > AU Instruments > Bombest.

GPL-3.0-only. Not affiliated with or endorsed by Teenage Engineering.
EOF
cp "$FX/LICENSE" "$FX/NOTICE" "$STAGE/$TOP/"

rm -f "$OUT/$TOP.zip"
(cd "$STAGE" && ditto -c -k --keepParent "$TOP" "$OUT/$TOP.zip")
echo "wrote $OUT/$TOP.zip ($count AUs)"
