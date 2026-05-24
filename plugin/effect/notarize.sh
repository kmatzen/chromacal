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
#    hardened runtime (required for notarization). Each `codesign --timestamp` call
#    contacts Apple's timestamp server (TSA); a stuck TSA call otherwise hangs the
#    whole release with no error. Wrap each in a timeout + retry (uses coreutils'
#    gtimeout/timeout when present; falls back to plain codesign otherwise).
TIMEOUT_BIN="$(command -v gtimeout || command -v timeout || true)"
cc_sign() {
    local f="$1" i
    for i in 1 2 3 4 5; do
        if [ -n "$TIMEOUT_BIN" ]; then
            "$TIMEOUT_BIN" 90 codesign --force --timestamp --options runtime -s "$CODESIGN_ID" "$f" && return 0
        else
            codesign --force --timestamp --options runtime -s "$CODESIGN_ID" "$f" && return 0
        fi
        echo "  [retry $i/5] codesign --timestamp stalled for $(basename "$f"); retrying..."
        sleep $((i * 3))
    done
    echo "::error::codesign failed after 5 retries for $f"
    return 1
}

if [ -d "$bundle/Contents/Frameworks" ]; then
    n=$(find "$bundle/Contents/Frameworks" -name '*.dylib' | wc -l | tr -d ' ')
    echo "==> [$(date +%T)] signing $n bundled dylibs (hardened runtime, timestamped)..."
    while IFS= read -r -d '' f; do
        cc_sign "$f"
    done < <(find "$bundle/Contents/Frameworks" -name '*.dylib' -print0)
fi
echo "==> [$(date +%T)] signing the bundle..."
cc_sign "$bundle"
codesign --verify --deep --strict --verbose=2 "$bundle"
echo "==> [$(date +%T)] Signed (hardened runtime) with: $CODESIGN_ID"

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
