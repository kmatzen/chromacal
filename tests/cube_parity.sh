#!/usr/bin/env bash
# Headless cube<->effect parity (CI): the exported Lumetri cube must reproduce the
# native effect's transform. solve a chart frame -> bake the effect-exact cube ->
# apply it via trilinear interpolation -> apply the effect's exact math -> compare.
# Passes if the MEAN per-channel difference is within tolerance (the max is higher
# by design: 33^3 LUT interpolation on saturated extremes). No Premiere needed.
#
# Usage: cube_parity.sh <bindir> <chart_image> [mean_tolerance=0.02]
set -euo pipefail

BIN=${1:?bin dir (where chromacal_solve etc. live)}
IMG=${2:?chart image}
MEANTOL=${3:-0.02}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

"$BIN/chromacal_solve"    "$IMG" "$T/c.cube" 33 "$T/p.cmcal" >/dev/null
"$BIN/chromacal_cube"     "$T/p.cmcal" "$T/lut.cube" 2.4 >/dev/null
"$BIN/chromacal_lutapply" "$T/lut.cube" "$IMG" "$T/cube_out.png" >/dev/null
"$BIN/chromacal_apply"    "$IMG" "$T/p.cmcal" "$T/eff_out.png" 2.4 >/dev/null

OUT=$("$BIN/chromacal_diff" "$T/cube_out.png" "$T/eff_out.png")
echo "$OUT"
MEAN=$(printf '%s\n' "$OUT" | sed -n 's/.*mean=\([0-9.eE+-]*\).*/\1/p')
awk -v m="$MEAN" -v t="$MEANTOL" 'BEGIN{
    if (m+0 <= t+0) { print "CUBE OK: mean " m " <= " t; exit 0 }
    print "CUBE FAIL: cube does not reproduce the effect (mean " m " > " t ")"; exit 1
}'
