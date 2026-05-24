# chromacal for Premiere Pro / After Effects

A native video effect that brings DaVinci Resolve–style *Color Match* to
Premiere: drop **chromacal** on a clip that contains an X-Rite/Calibrite
ColorChecker, click **Analyze**, and it solves a camera calibration (log-polynomial
tone curve + 3×3 CCM) with chromacal and applies it live on the clip. It can also
export the calibration as a `.cube` for Lumetri's Input LUT.

> Premiere has no built-in ColorChecker auto-calibration; this fills that gap.

## Docs

- **[USER-GUIDE.md](USER-GUIDE.md)** — install + calibrate + apply (end users).
- **[RELEASING.md](RELEASING.md)** — cut a release; what needs your hardware/creds.
- **[TESTING-PROTOCOL.md](TESTING-PROTOCOL.md)** — the test suite + parity checks.
- **[WINDOWS.md](WINDOWS.md)** — building on Windows.
- **[THIRD-PARTY.md](THIRD-PARTY.md)** — bundled open-source attributions.
- **[effect/README.md](effect/README.md)** — effect build details.

## Layout

```
plugin/
├── effect/                       native After Effects–style effect (Premiere + AE)
│   ├── chromacal_effect.{h,cpp}  params, Analyze, live apply, .cube export
│   ├── save_dialog_mac.mm        native Save-As panel for Export .cube (macOS)
│   ├── chromacal_effect.r        PiPL resource
│   ├── Info.plist, build_bundle.sh
│   └── README.md                 build details
├── index.html, manifest.json     UXP analysis panel (full-resolution Analyze)
├── src/main.js                   panel logic (export full-res frame → solve → preset)
└── native/
    ├── solve_core.{h,cpp}        SDK-free engine wrapper (detect → solve → write cube/preset)
    ├── chromacal_addon.cpp       UXP Hybrid native addon (require("chromacal.uxpaddon"))
    └── solve_cli.cpp             headless tool / smoke-test harness
```

## The native effect

Build details are in `effect/README.md` (needs the Premiere C++ SDK + the After
Effects SDK); installing and using it is in `TESTING.md`. In short:

- **Effects ▸ Video Effects ▸ chromacal** → drop on a charted clip.
- **Analyze current frame** → detect + solve on the playhead frame (reports the
  patch count; retries while the source is still "Media pending").
- **Apply calibration** → grade the clip live, correct in the working space.
- **Export .cube for Lumetri** → Save-As dialog; load the file in Lumetri ▸ Basic
  Correction ▸ Input LUT. The cube is the effect's exact transform — Premiere
  applies an Input LUT directly (no sRGB management around it), so it reproduces the
  effect (measured ~0.5% mean over the chart).

## Full-resolution analysis panel (UXP)

The effect's own **Analyze** runs during a normal render, so it only sees the frame
at the **Program Monitor's playback resolution** — at 1/2 or 1/4 the chart can be
too small (<15 px/patch) for the detector to localize accurately. The UXP panel
fixes this: it exports the current frame at the **full sequence resolution** via
`Exporter.exportSequenceFrame(...)`, runs the identical detect+solve natively, and
writes a **calibration preset** the effect loads. Analysis at full res, application
in the effect.

**Workflow:** open the **chromacal** panel (Window ▸ Extensions) → *Calibrate from
current frame* → *Save calibration preset…* → in the clip's **chromacal** effect,
**Load calibration** → pick that `.cmcal` → tick **Apply**. (It also still writes a
`.cube` for Lumetri.)

**Build the native addon** — needs the **Premiere UXP Hybrid SDK** (a gated Adobe
Developer Console download; `UxpAddon.h` etc.). Point `CHROMACAL_UXP_SDK_DIR` at the
SDK's `utilities/` dir and pass its support sources:

```bash
cmake -B build -DCHROMACAL_BUILD_PPRO=ON \
      -DCHROMACAL_UXP_SDK_DIR=/path/to/uxp-hybrid-sdk/src/utilities \
      -DCHROMACAL_UXP_SDK_SRC="/path/.../UxpAddon.cpp;/path/.../UxpValue.cpp"
cmake --build build --target chromacal_uxpaddon   # -> chromacal.uxpaddon
```

Place the built `chromacal.uxpaddon` where the manifest expects it (the platform
path, e.g. `plugin/mac/arm64/chromacal.uxpaddon`). The addon links OpenCV/Ceres via
`chromacal_ppro_core`, so those dylibs must be resolvable from the addon (bundle/
sign as needed for Premiere's sandbox).

`manifest.json` ships **without** the addon declaration so the panel loads (and
its full-res frame export works) even before the addon is built — UXP refuses to
load a panel whose declared addon binary is missing. Once `chromacal.uxpaddon` is
built and placed, re-add this to `manifest.json` to enable the in-panel solve
(append `"enableAddon": true` inside `requiredPermissions`):

```json
    "addon": { "name": "chromacal.uxpaddon" }
```

Then **load the panel** with the **UXP Developer Tool** (Add Plugin →
`plugin/manifest.json` → Load) with Premiere running.

## Headless core / CLI (builds without the Adobe SDKs)

```bash
cmake -B build -DCHROMACAL_BUILD_PPRO=ON
cmake --build build --target chromacal_solve
./build/plugin/chromacal_solve docs/before.png out.cube 33   # frame image -> .cube
./build/plugin/chromacal_solve frame.png out.cube 33 cal.cmcal  # also a preset
```

The optional 4th argument writes a `.cmcal` **calibration preset** — the
no-SDK route to the native effect: export a full-resolution chart frame (the panel,
or Premiere's Export Frame button), run the CLI on it, then **Load calibration** →
`cal.cmcal` in the effect. Same result as the panel, without the UXP Hybrid SDK.

## Color-management notes (hard-won)

- Calibrate in an **SDR Rec. 709** sequence. A plain sRGB PNG/still is fine.
- The effect uses the sequence's **SDR gamma** for its output and deliberately
  does **not** trust `GetGraphicsWhiteLuminance` to detect HDR — it returns the
  HLG reference (203) even on SDR sequences, which would wrongly trigger an HDR
  path. HDR (PQ/HLG) output is not currently supported.
- Lumetri's Input LUT is **sRGB-managed**: Premiere tags a loaded `.cube` as sRGB
  and transcodes `working → sRGB → LUT → sRGB → working`. The exported cube undoes
  that transcode so it reproduces the effect; see `write_display_cube` in
  `native/solve_core.cpp`.
