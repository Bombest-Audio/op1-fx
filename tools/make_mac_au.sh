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

# .aupreset files give Logic folders in its preset menu; the AU factory list is flat. The generator reads the AUs registered
# on this Mac, so first install the exact build artifacts here and check every installed binary is byte-identical to the
# build. Then the presets come from the build being packaged, not from whatever suite was installed before.
user_components="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$user_components"
for c in "$BUILD"/plugins/*/*_artefacts/Release/AU/*.component; do
    name="$(basename "$c" .component)"
    rm -rf "$user_components/$name.component"
    ditto "$c" "$user_components/$name.component"
    cmp -s "$c/Contents/MacOS/$name" "$user_components/$name.component/Contents/MacOS/$name" \
        || { echo "installed $name differs from the build" >&2; exit 1; }
done
rm -rf "$HOME/Library/Caches/com.apple.audio.AudioUnitCache"
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 2
# Preset counts are checked against the source presets.json files as a second guard.
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
# Manifest of the factory files in this release: "sha256  path". install.sh uses it to retire files a later release drops
# or renames, and to leave alone any file you edited.
(cd "$STAGE/$TOP/Presets/Bombest" && find . -name '*.aupreset' | sort | while IFS= read -r f; do shasum -a 256 "$f"; done) \
    > "$STAGE/$TOP/Presets/manifest.txt"

cat > "$STAGE/$TOP/install.sh" <<'EOF'
#!/bin/sh
# Copies the AUs and their grouped presets into your user Library and makes Logic rescan them on next launch.
# Each plugin is copied and verified next to the installed one, then swapped in, so a failed copy never leaves a plugin
# missing. Presets you saved or edited are kept.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
dest="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$dest"
for c in "$here"/Components/*.component; do
    name="$(basename "$c")"
    new="$dest/.$name.new"
    old="$dest/.$name.old"
    rm -rf "$new" "$old"
    ditto "$c" "$new"
    codesign --verify --deep --strict "$new" || { rm -rf "$new"; echo "$name failed signature verification, not installed" >&2; exit 1; }
    if [ -e "$dest/$name" ]; then mv "$dest/$name" "$old"; fi
    if mv "$new" "$dest/$name"; then
        rm -rf "$old"
    else
        rm -rf "$dest/$name"
        if [ -e "$old" ]; then mv "$old" "$dest/$name"; fi
        echo "could not install $name, kept the previous copy" >&2
        exit 1
    fi
    echo "installed $name"
done

presets="$HOME/Library/Audio/Presets/Bombest"
manifest="$presets/.op1-suite-manifest"
stamp="$(date +%Y%m%d-%H%M%S)"
mkdir -p "$presets"
hash() { shasum -a 256 "$1" | cut -d' ' -f1; }
# Retire factory files from the previous release only if they are still byte-identical to what it shipped.
if [ -f "$manifest" ]; then
    while read -r sum f; do
        [ -f "$presets/$f" ] && [ "$(hash "$presets/$f")" = "$sum" ] && rm -f "$presets/$f"
    done < "$manifest"
    find "$presets" -type d -empty -delete 2>/dev/null || true
    mkdir -p "$presets"
fi
# Copy this release's presets. A different file already at the same path is yours: move it aside, never overwrite it.
while read -r sum f; do
    target="$presets/$f"
    if [ -f "$target" ] && [ "$(hash "$target")" != "$sum" ]; then
        mv "$target" "$target.user-backup-$stamp"
        echo "kept your $f as $(basename "$target").user-backup-$stamp"
    fi
    mkdir -p "$(dirname "$target")"
    cp "$here/Presets/Bombest/$f" "$target"
done < "$here/Presets/manifest.txt"
cp "$here/Presets/manifest.txt" "$manifest"
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
