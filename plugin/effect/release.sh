#!/usr/bin/env bash
# Cut a chromacal release: build a self-contained bundle, (optionally) sign +
# notarize it, and produce the .pkg installer — in one command.
#
# Required (build): the Adobe SDKs
#   PRSDK_DIR=".../Premiere Pro <ver> C++ SDK/Examples/Headers"
#   AESDK_DIR=".../After Effects SDK/Examples/Headers"
# Optional (universal): CHROMACAL_UNIVERSAL=1   (needs arm64+x86_64 dependencies)
# Optional (signed, notarized — for distribution):
#   CODESIGN_ID="Developer ID Application: You (TEAMID)"        # signs the .plugin
#   CHROMACAL_INSTALLER_ID="Developer ID Installer: You (TEAMID)"  # signs the .pkg
#   NOTARY_PROFILE=chromacal   # `xcrun notarytool store-credentials chromacal …` first
#
# Run from the repo root: plugin/effect/release.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

: "${PRSDK_DIR:?set PRSDK_DIR to the Premiere C++ SDK Examples/Headers}"
: "${AESDK_DIR:?set AESDK_DIR to the After Effects SDK Examples/Headers}"

echo "==> Building self-contained bundle..."
CHROMACAL_SELF_CONTAINED=1 "$HERE/build_bundle.sh"
BUNDLE="$(cd "$HERE/../.." && pwd)/build/plugin/chromacal.plugin"

if [ -n "${CODESIGN_ID:-}" ]; then
    if [ -n "${CHROMACAL_INSTALLER_ID:-}" ]; then
        # We ship a .pkg, and notarizing the .pkg already covers the signed bundle
        # inside it (pkg-installed files aren't quarantined). So just SIGN the bundle
        # here (hardened runtime) and let make_installer.sh do the single notarization
        # of the .pkg — avoids a redundant second notarytool --wait.
        echo "==> Signing the bundle (the .pkg is what gets notarized)..."
        NOTARY_PROFILE='' "$HERE/notarize.sh" "$BUNDLE"
    else
        # No installer cert -> no .pkg notarization, so notarize the bundle itself.
        echo "==> Signing + notarizing the bundle..."
        "$HERE/notarize.sh" "$BUNDLE"
    fi
else
    echo "==> Skipping bundle signing (set CODESIGN_ID to sign/notarize for distribution)."
fi

echo "==> Building the .pkg installer..."
"$HERE/make_installer.sh" "$BUNDLE"

echo "==> Done. Artifact: $(ls -1 chromacal-*.pkg 2>/dev/null | tail -1)"
echo "    Distribution build needs CODESIGN_ID + CHROMACAL_INSTALLER_ID + NOTARY_PROFILE;"
echo "    otherwise the .pkg is unsigned (fine for local testing)."
