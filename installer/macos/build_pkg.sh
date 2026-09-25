#!/usr/bin/env bash
# Builds the macOS installer package (SAMS-Ambience-Control-<version>-macOS.pkg).
#
#   installer/macos/build_pkg.sh <version> <artefacts dir> <output dir>
#   e.g. installer/macos/build_pkg.sh 1.0.0 build/Ambience_artefacts/Release dist
#
# Components (user-selectable in "Customize"): VST3, AU, Standalone.
# Destination (user-selectable on "Destination Select"):
#   - all users:     /Library/Audio/Plug-Ins/{VST3,Components}, /Applications
#   - current user:  ~/Library/Audio/Plug-Ins/{VST3,Components}, ~/Applications
#
# Optional signing / notarisation (all env vars must be set for each step):
#   MACOS_APP_IDENTITY        "Developer ID Application: Name (TEAMID)"  -> signs the bundles
#   MACOS_INSTALLER_IDENTITY  "Developer ID Installer: Name (TEAMID)"    -> signs the .pkg
#   APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD                           -> notarise + staple
# Without them the bundles are ad-hoc signed and the package is unsigned.
set -euo pipefail

VERSION="${1:?version}"
ART="${2:?artefacts dir}"
OUT="${3:?output dir}"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
ID_BASE="com.ambienceaudio.ambience"
NAME="SAMS AMBIENCE CONTROL"
mkdir -p "$OUT"

sign_bundle() {
    local bundle="$1"
    local ent=()
    if [[ "$bundle" == *.app ]]; then ent=(--entitlements "$HERE/Standalone.entitlements"); fi
    if [[ -n "${MACOS_APP_IDENTITY:-}" ]]; then
        codesign --force --deep --options runtime --timestamp ${ent[@]+"${ent[@]}"} --sign "$MACOS_APP_IDENTITY" "$bundle"
    else
        codesign --force --deep ${ent[@]+"${ent[@]}"} --sign - "$bundle"   # ad-hoc (required for arm64, no Gatekeeper trust)
    fi
    codesign --verify --deep --strict --verbose=2 "$bundle"
}

# $1 component id, $2 bundle path, $3 install location, $4 name of a legacy bundle to remove
make_component() {
    local id="$1" bundle="$2" location="$3" legacy="$4"
    local root="$WORK/root-$id"
    # preinstall: remove the bundle published as "Ambience" in 1.0.0 (same plug-in ID, would appear twice).
    local scripts="$WORK/scripts-$id"
    mkdir -p "$scripts"
    printf '#!/bin/sh\n# $2 = chosen install location\nrm -rf "$2/%s"\nexit 0\n' "$legacy" > "$scripts/preinstall"
    chmod +x "$scripts/preinstall"
    mkdir -p "$root"
    ditto "$bundle" "$root/$(basename "$bundle")"
    sign_bundle "$root/$(basename "$bundle")"
    # Non-relocatable: always install to the chosen location, even if a copy exists elsewhere.
    pkgbuild --analyze --root "$root" "$WORK/$id.plist"
    plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/$id.plist"
    plutil -replace 0.BundleOverwriteAction -string upgrade "$WORK/$id.plist"
    pkgbuild --root "$root" \
             --component-plist "$WORK/$id.plist" \
             --scripts "$scripts" \
             --identifier "$ID_BASE.$id" \
             --version "$VERSION" \
             --install-location "$location" \
             "$WORK/pkgs/Ambience-$id.pkg"
}

mkdir -p "$WORK/pkgs"
make_component vst3 "$ART/VST3/$NAME.vst3" "/Library/Audio/Plug-Ins/VST3" "Ambience.vst3"
make_component au "$ART/AU/$NAME.component" "/Library/Audio/Plug-Ins/Components" "Ambience.component"
make_component standalone "$ART/Standalone/$NAME.app" "/Applications" "Ambience.app"

sed "s/@VERSION@/$VERSION/g" "$HERE/distribution.xml" > "$WORK/distribution.xml"

UNSIGNED="$WORK/Ambience-unsigned.pkg"
FINAL="$OUT/SAMS-Ambience-Control-$VERSION-macOS.pkg"
productbuild --distribution "$WORK/distribution.xml" \
             --package-path "$WORK/pkgs" \
             --resources "$HERE/resources" \
             "$UNSIGNED"

if [[ -n "${MACOS_INSTALLER_IDENTITY:-}" ]]; then
    productsign --timestamp --sign "$MACOS_INSTALLER_IDENTITY" "$UNSIGNED" "$FINAL"
    pkgutil --check-signature "$FINAL"
else
    cp "$UNSIGNED" "$FINAL"
    echo "::warning::macOS package is NOT signed (MACOS_INSTALLER_IDENTITY not set) - Gatekeeper will warn."
fi

if [[ -n "${MACOS_INSTALLER_IDENTITY:-}" && -n "${APPLE_ID:-}" && -n "${APPLE_TEAM_ID:-}" && -n "${APPLE_APP_PASSWORD:-}" ]]; then
    xcrun notarytool submit "$FINAL" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
          --password "$APPLE_APP_PASSWORD" --wait --timeout 30m
    xcrun stapler staple "$FINAL"
    xcrun stapler validate "$FINAL"
    spctl --assess --type install --verbose "$FINAL"
else
    echo "::warning::macOS package is NOT notarised (Apple credentials not set)."
fi

rm -rf "$WORK"
echo "Created $FINAL"
