# chromacal — native Premiere Pro / After Effects effect

A native video effect (the After Effects effect API that Premiere hosts) that
calibrates from a ColorChecker and applies the correction live in the render
pipeline — no LUT round-trip required (though it can also export a `.cube`).

## Controls (Effect Controls ▸ chromacal)

- **Analyze current frame** — detect the chart on the playhead frame and solve a
  chromacal calibration (log-polynomial tone curve + 3×3 CCM), down-weighting
  unreliable patches. Reports patch count / reliability / fit error. Retries
  while the source is still the "Media pending" placeholder.
- **Apply calibration** — apply the fitted tone curve + CCM live, encoded to the
  sequence working space (a 1D-LUT'd tone curve keeps it fast).
- **Export .cube for Lumetri** — bake a LUT (Save As…) for Lumetri ▸ Basic
  Correction ▸ Input LUT. It's the effect's exact transform; Premiere applies an
  Input LUT directly (no sRGB management), so it matches the effect (~0.5%).
- **Save / Load calibration** — write/read the calibration as a small `.chromacal`
  preset to reuse one chart read across clips/projects.
- **Show detected patches** — overlay a magenta cross at each detected patch
  center to confirm the chart was located.
- **Output** — `SDR (Rec.709)` (default), or opt-in `HDR HLG` / `HDR PQ`
  (Rec.2100, Rec.2020 gamut). HDR is unverified — see caveats.

The calibration (13 params + patch centers) persists in the effect's sequence
data, so it round-trips with the project.

## Files

```
chromacal_effect.{h,cpp}  params, entry points, Render (analyze/apply/export/save/load/overlay)
save_dialog_mac.mm        NSSavePanel/NSOpenPanel (macOS file dialogs)
save_dialog_win.cpp       GetSaveFileName/GetOpenFileName (Windows file dialogs)
chromacal_effect.r        PiPL resource (effect name, entry point, out-flags)
Info.plist                CFBundle metadata
build_bundle.sh           compile + Rez the PiPL + assemble chromacal.plugin
notarize.sh               Developer ID sign + notarize + staple (distribution)
```

The detect+solve runs in `../native/solve_core` (OpenCV/chromacal) via the
OpenCV-free `solve_from_bgra_f32`, so this effect TU never includes OpenCV
alongside the After Effects headers (they clash).

## Requirements & build

Needs **both** SDKs (a Premiere effect is an AE-style plugin):
- **Premiere Pro C++ SDK** — `.../Examples/Headers` (`PrSDK*`, `PrSDKAESupport.h`)
- **After Effects SDK** — `.../Examples/Headers` (+ sibling `Util/`, `Resources/`)

```bash
PRSDK_DIR=".../Premiere Pro 26.0 C++ SDK/Examples/Headers" \
AESDK_DIR=".../AfterEffects SDK/Examples/Headers" \
plugin/effect/build_bundle.sh                 # -> build/plugin/chromacal.plugin

# self-contained (bundles the dylibs; runs without Homebrew):
CHROMACAL_SELF_CONTAINED=1 PRSDK_DIR=… AESDK_DIR=… plugin/effect/build_bundle.sh
```

`build_bundle.sh` links the Mach-O, compiles the PiPL with `Rez` to a **flat
`Contents/Resources/chromacal.rsrc`** (Premiere reads the PiPL there, *not* from
the executable's resource fork), and assembles the `.plugin`. Install to
`/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/` and restart
Premiere; the effect appears under **Video Effects ▸ chromacal**.

## Status & caveats

- **Verified here:** builds against the real Premiere + AE SDK headers; PiPL
  compiles; bundle assembles, `dlopen`s, and exports `EffectMain`; the
  self-contained bundle loads with zero Homebrew references; the core detects
  24/24 on `docs/before.png`.
- **Verified in Premiere (by the user):** loads, analyzes, applies correctly in
  an SDR Rec.709 sequence; the exported `.cube` matches the effect in Lumetri.
- **Color management:** use an SDR Rec.709 sequence. The effect uses the
  sequence SDR gamma; it does *not* trust `GetGraphicsWhiteLuminance` for HDR
  detection (it returns 203 even on SDR). See `../README.md`.
- **HDR (HLG/PQ):** implemented to spec but **unverified** — needs an HDR
  sequence/monitor to validate the OOTF/nit mapping.
- **Windows:** the code is cross-platform but is built/tested on macOS only here;
  build with the Windows toolchain + PiPLtool.
- **Distribution:** `notarize.sh` signs (Developer ID, hardened runtime) +
  notarizes + staples a (self-contained) bundle.

## GPU (Metal) render path — opt-in, runtime-verify on a GPU

Implemented and **compile-verified**, gated behind `-DCHROMACAL_GPU=ON` so the
default build is the verified CPU effect:

```bash
CHROMACAL_GPU=1 PRSDK_DIR=… AESDK_DIR=… plugin/effect/build_bundle.sh
```

This advertises `SUPPORTS_SMART_RENDER` + `SUPPORTS_GPU_RENDER_F32`; builds a
Metal compute pipeline from the bundled `chromacal.metallib`
(`chromacal_gpu_mac.mm`, via `PF_GPUDeviceSuite`); and dispatches
`chromacal_kernel.metal` (same math as the CPU path) over the GPU-world buffers
in `PF_Cmd_SMART_RENDER_GPU`. Analyze/export/save/load fall back to the CPU
SmartRender; legacy Render and SmartRender share the factored `RenderBody`.

It compiles, links, dlopens, and bundles the metallib here. **GPU *rendering*
itself still needs a GPU + Premiere to verify** (I can't run it here) — so treat
the GPU build as experimental until you confirm it on your hardware. The CPU
path is 1D-LUT-accelerated meanwhile.

## Not yet implemented (need data not available here)
- **SG-140 / more chart types.** Passport (24 patches) already works (same layout
  as the Classic). SG-140 needs the MCC SG140 detector type **and** the 140-patch
  reference Lab table. The authoritative data is X-Rite's, in a tab-delimited
  `Patch  L*  a*  b*` file (D50/2°), e.g.
  `my-classic.xrite.com/documents/apps/public/digital_colorchecker_sg_l_a_b.txt`,
  with **two editions** (pre/post the Nov-2014 colorant change). It's licensed
  for *personal/educational, not commercial* use — so it must **not** be embedded
  in this MIT repo. The right design is to *load the user's own reference file* at
  calibration time (a tiny CGATS/tab parser) and select the SG140 detector. Not
  implemented yet; needs an SG chart to verify detection.
- **D50 white-point target.** The pipeline already adapts D50→D65 (Bradford) and
  outputs sRGB/Rec.709 (D65) — correct for SDR video; a D50 target is niche.
