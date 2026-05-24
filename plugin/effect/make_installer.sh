#!/usr/bin/env bash
# Build a macOS .pkg installer that drops chromacal.plugin into Premiere/AE's
# MediaCore plug-in folder — so end users double-click instead of running sudo cp.
#
# Input: a built bundle (default build/plugin/chromacal.plugin). For distribution
# build it self-contained + signed first:
#   CHROMACAL_SELF_CONTAINED=1 PRSDK_DIR=… AESDK_DIR=… plugin/effect/build_bundle.sh
#   plugin/effect/notarize.sh build/plugin/chromacal.plugin     # sign the .plugin
#   plugin/effect/make_installer.sh                              # -> chromacal-<ver>.pkg
#
# Optional signing/notarization of the .pkg itself (Developer ID Installer):
#   CHROMACAL_INSTALLER_ID="Developer ID Installer: You (TEAMID)" \
#   NOTARY_PROFILE=chromacal plugin/effect/make_installer.sh
set -euo pipefail

BUNDLE=${1:-build/plugin/chromacal.plugin}
[ -d "$BUNDLE" ] || { echo "error: bundle not found: $BUNDLE (build it first)"; exit 1; }

# Version from the bundle's Info.plist (fallback 0.1.0).
PLIST="$BUNDLE/Contents/Info.plist"
VER=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$PLIST" 2>/dev/null || echo 0.1.0)
IDENT="com.chromacal.effect"
DEST="/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
OUT="chromacal-${VER}.pkg"

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
cp -R "$BUNDLE" "$STAGE/"
find "$STAGE" -name '._*' -delete           # strip AppleDouble metadata
COMPONENT=$(mktemp -t cc_component_XXXX).pkg

echo "Building $OUT (installs $(basename "$BUNDLE") -> $DEST) ..."
pkgbuild --root "$STAGE" --install-location "$DEST" \
         --identifier "$IDENT" --version "$VER" "$COMPONENT"

# Wrap in a product archive (so it's a normal double-click installer); sign it if
# a Developer ID Installer identity is provided.
if [ -n "${CHROMACAL_INSTALLER_ID:-}" ]; then
    # Build the product archive UNSIGNED, then sign with `productsign`. We don't use
    # `productbuild --sign`: in CI it silently blocked on keychain access to the
    # Installer key (codesign with the App cert worked), because the keychain import
    # authorizes `/usr/bin/productsign` (via -T) but not `productbuild`. No pkg-sig
    # timestamp either — the embedded code is already timestamped, which is what
    # notarization requires — so this also avoids a TSA dependency for the package.
    # Still bounded with a timeout + retry (gtimeout from coreutils; plain otherwise).
    TIMEOUT_BIN="$(command -v gtimeout || command -v timeout || true)"
    UNSIGNED="${OUT%.pkg}-unsigned.pkg"
    productbuild --package "$COMPONENT" "$UNSIGNED"
    echo "==> [$(date +%T)] signing the .pkg (productsign)..."
    _signed=0
    for i in 1 2 3 4 5; do
        rm -f "$OUT"
        if [ -n "$TIMEOUT_BIN" ]; then
            "$TIMEOUT_BIN" 120 productsign --sign "$CHROMACAL_INSTALLER_ID" "$UNSIGNED" "$OUT" && { _signed=1; break; }
        else
            productsign --sign "$CHROMACAL_INSTALLER_ID" "$UNSIGNED" "$OUT" && { _signed=1; break; }
        fi
        echo "  [retry $i/5] productsign stalled; retrying..."
        sleep $((i * 5))
    done
    rm -f "$UNSIGNED"
    [ "$_signed" = 1 ] || { echo "::error::productsign failed after 5 retries"; exit 1; }
    echo "==> [$(date +%T)] .pkg signed."
    if [ -n "${NOTARY_PROFILE:-}" ]; then
        echo "==> [$(date +%T)] notarizing $OUT (notarytool --wait --timeout 30m)..."
        xcrun notarytool submit "$OUT" --keychain-profile "$NOTARY_PROFILE" --wait --timeout 30m
        echo "==> [$(date +%T)] stapling $OUT..."
        xcrun stapler staple "$OUT"
        echo "==> [$(date +%T)] notarized + stapled $OUT"
    fi
else
    productbuild --package "$COMPONENT" "$OUT"
    echo "NOTE: unsigned .pkg (set CHROMACAL_INSTALLER_ID to sign for distribution)."
fi

echo "Done: $OUT"
echo "Verify: installer -pkginfo -pkg \"$OUT\"  (or double-click to install)."
