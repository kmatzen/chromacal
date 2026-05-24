# Testing the chromacal Premiere/AE effect

A ready-made test image with a ColorChecker is in the repo: `docs/before.png`
(detects 24/24 patches).

## Build + install (macOS)

```bash
PRSDK_DIR=".../Premiere Pro 26.0 C++ SDK/Examples/Headers" \
AESDK_DIR=".../AfterEffects SDK/Examples/Headers" \
plugin/effect/build_bundle.sh
# → build/plugin/chromacal.plugin

# Premiere scans this folder on launch (system path needs sudo):
sudo cp -R "build/plugin/chromacal.plugin" \
  "/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/"
```

The bundle links Homebrew OpenCV/Ceres/OCIO by absolute path — fine for local
testing on a machine that has them under `/opt/homebrew`. **Restart Premiere**
after installing.

For a **self-contained** bundle that runs on machines without Homebrew (bundles
the dylibs into `Contents/Frameworks`), set `CHROMACAL_SELF_CONTAINED=1` (needs
`dylibbundler`):

```bash
CHROMACAL_SELF_CONTAINED=1 PRSDK_DIR=… AESDK_DIR=… plugin/effect/build_bundle.sh
```

## Use

1. Use an **SDR Rec. 709** sequence (Sequence ▸ Sequence Settings… → Color
   Management / Working Color Space → Rec. 709). A plain sRGB PNG is fine as the
   source.
2. Effects panel → search **chromacal** → drag onto a clip that contains a
   ColorChecker; park the playhead on the chart.
3. **Effect Controls ▸ chromacal**:
   - **Analyze current frame** → detects + solves; a dialog reports the patch
     count (or "no ColorChecker found"). It keeps retrying while the source is
     still the "Media pending" placeholder.
   - **Apply calibration** → see the tone-curve + CCM correction, live. Params
     persist with the project.
   - **Export .cube for Lumetri** → Save-As dialog; then load the file in
     Lumetri ▸ Basic Correction ▸ Input LUT (Browse). It matches the effect.

## Verify detection independently

```bash
./build/plugin/chromacal_solve docs/before.png /tmp/x.cube   # expects 24 patches
```

## If it doesn't load

- Confirm the bundle is in the MediaCore folder and Premiere was **fully
  restarted**.
- The PiPL must be a flat `Contents/Resources/chromacal.rsrc` (build_bundle.sh
  writes it there) — Premiere reads the PiPL from that file, **not** from the
  executable's resource fork.
- The bundle is ad-hoc-signed; that's fine for local testing (Premiere ships with
  `com.apple.security.cs.disable-library-validation`).
- If results look wrong rather than absent, it's color management, not detection
  — see the notes in `README.md` (use an SDR Rec. 709 sequence).
