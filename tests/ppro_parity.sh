#!/usr/bin/env bash
# Headless<->Premiere parity check (Layer 2 of TESTING-PROTOCOL.md), automated
# down to a single command. You provide:
#   RAW   : the uncorrected chart frame (panel "Calibrate from current frame", or
#           Premiere Export Frame with the chromacal effect's Apply OFF)
#   PRESET: the calibration loaded into the effect (.cmcal)
#   PPRO  : the frame Premiere exported with the effect's Apply ON
#
# It applies PRESET to RAW with the effect's *exact* transform (chromacal_apply)
# and diffs that against Premiere's output (chromacal_diff). Non-zero exit = the
# effect's render diverged from the core (color management / encode / GPU path).
#
# Usage: tests/ppro_parity.sh RAW.png PRESET.cmcal PPRO.png [tolerance=0.02] [gamma=2.4]
set -euo pipefail

RAW=${1:?raw frame}; PRESET=${2:?preset .cmcal}; PPRO=${3:?premiere-applied frame}
TOL=${4:-0.02}; GAMMA=${5:-2.4}
BIN=${CHROMACAL_BIN:-build/plugin}
CORE=$(mktemp -t cc_core_XXXX).png

echo "== applying $PRESET to $RAW with the effect's transform (gamma $GAMMA) =="
"$BIN/chromacal_apply" "$RAW" "$PRESET" "$CORE" "$GAMMA"

echo "== comparing core vs Premiere (tolerance $TOL) =="
if "$BIN/chromacal_diff" "$CORE" "$PPRO" "$TOL"; then
    echo "PARITY OK — Premiere matches the headless core."
    rm -f "$CORE"; exit 0
else
    echo "PARITY FAIL — saved core reference at $CORE for inspection."
    exit 1
fi
