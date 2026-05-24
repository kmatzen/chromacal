#!/usr/bin/env bash
# Code-sign (Developer ID, hardened runtime) + notarize + staple chromacal.plugin
# for distribution outside your own machine.
#
# Prereqs (one-time):
#   * a "Developer ID Application" certificate in your login keychain
#   * a notarytool credential profile:
#       xcrun notarytool store-credentials chromacal-notary \
#         --apple-id you@example.com --team-id TEAMID --password <app-specific-pw>
#
# Usage:
#   CODESIGN_ID="Developer ID Application: Your Name (TEAMID)" \
#   NOTARY_PROFILE="chromacal-notary" \
#   plugin/effect/notarize.sh build/plugin/chromacal.plugin
#
# Best run on a CHROMACAL_SELF_CONTAINED=1 bundle so the dylibs ship inside it.
set -euo pipefail

bundle="${1:?usage: notarize.sh <path/to/chromacal.plugin>}"
: "${CODESIGN_ID:?set CODESIGN_ID to your \"Developer ID Application: …\" identity}"
# NOTARY_PROFILE is optional: unset => sign + verify only (skip notarization).

# 1. Sign inside-out: every bundled dylib first, then the bundle itself, with the
#    hardened runtime (required for notarization).
if [ -d "$bundle/Contents/Frameworks" ]; then
    find "$bundle/Contents/Frameworks" -name '*.dylib' -print0 | while IFS= read -r -d '' f; do
        codesign --force --timestamp --options runtime -s "$CODESIGN_ID" "$f"
    done
fi
codesign --force --timestamp --options runtime -s "$CODESIGN_ID" "$bundle"
codesign --verify --deep --strict --verbose=2 "$bundle"
echo "Signed (hardened runtime) with: $CODESIGN_ID"

# 2. Notarize + staple if credentials are configured; otherwise stop at signed.
if [ -z "${NOTARY_PROFILE:-}" ]; then
    echo "NOTARY_PROFILE unset — signed but NOT notarized. For distribution, run:"
    echo "  xcrun notarytool store-credentials <profile> --apple-id … --team-id … --password <app-pw>"
    echo "  NOTARY_PROFILE=<profile> CODESIGN_ID=\"$CODESIGN_ID\" $0 \"$bundle\""
    exit 0
fi
zip="${bundle%.plugin}.zip"
ditto -c -k --keepParent "$bundle" "$zip"
xcrun notarytool submit "$zip" --keychain-profile "$NOTARY_PROFILE" --wait --timeout 30m
xcrun stapler staple "$bundle"
rm -f "$zip"
codesign --verify --deep --strict --verbose=2 "$bundle"
spctl --assess --type install --verbose=4 "$bundle" || true  # informational
echo "Notarized + stapled: $bundle"
