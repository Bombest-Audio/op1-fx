#!/usr/bin/env bash
# Package all 20 AUs for Logic Pro on the Mac as one signed, notarized zip.
#   tools/make_mac_au.sh <version> [build dir]     build dir defaults to build/all; writes dist/mac/OP1-Suite-<version>-mac-au.zip
# Needs the AUs built first (cmake --build build/all), the Developer ID Application identity in the login keychain and a
# notarytool keychain profile (OP1_NOTARY_PROFILE, default notarytool). AU bundles get the plain Developer ID recipe:
# no Hardened Runtime, no entitlements, since macOS drops entitlements from MH_BUNDLE binaries anyway.
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

cat > "$STAGE/$TOP/install.sh" <<'EOF'
#!/bin/sh
# Copies the AUs into your user Components folder and makes Logic rescan them on next launch.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
dest="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$dest"
for c in "$here"/Components/*.component; do
    rm -rf "$dest/$(basename "$c")"
    ditto "$c" "$dest/$(basename "$c")"
    echo "installed $(basename "$c")"
done
rm -rf "$HOME/Library/Caches/com.apple.audio.AudioUnitCache"
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Done. Quit and reopen Logic Pro to see the plugins."
EOF
chmod +x "$STAGE/$TOP/install.sh"
cat > "$STAGE/$TOP/README.txt" <<EOF
OP1 plugins $VERSION, Audio Units for Logic Pro (macOS 11.0+, Apple Silicon and Intel).

Install: open Terminal, drag install.sh into it, press Return.
Or copy everything in Components/ to ~/Library/Audio/Plug-Ins/Components yourself.
Effects show up under Audio Units > Bombest, instruments under the instrument slot > AU Instruments > Bombest.

GPL-3.0-only. Not affiliated with or endorsed by Teenage Engineering.
EOF
cp "$FX/LICENSE" "$FX/NOTICE" "$STAGE/$TOP/"

rm -f "$OUT/$TOP.zip"
(cd "$STAGE" && ditto -c -k --keepParent "$TOP" "$OUT/$TOP.zip")
echo "wrote $OUT/$TOP.zip ($count AUs)"
