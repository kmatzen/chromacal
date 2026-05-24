# Changelog

All notable changes to chromacal. Versions follow the plugin's
`CFBundleShortVersionString` / PiPL `AE_Effect_Version`.

## [0.1.1] - 2026-05-24

First release of the native Premiere/After Effects plugin. Builds (macOS + Windows)
and the signed + notarized macOS `.pkg` are produced by GitHub Actions.

### Changed
- **Leaner bundle** — the macOS build now links a lean OpenCV built without OpenVINO
  (`WITH_OPENVINO=OFF`), dropping ~114 MB of unused inference dylibs that `dnn`
  otherwise drags in.
- **Robust release signing** — bounded `codesign --timestamp`, `productsign`, and
  `notarytool` against a stuck Apple timestamp/notary service (timeouts + retries),
  and sign the `.pkg` with `productsign` (not `productbuild --sign`).

### Added
- **Native Premiere/After Effects effect** — Analyze a ColorChecker on the current
  frame and apply the calibration (log-poly tone curve + 3×3 CCM) live in the
  render pipeline (legacy CPU render; parity-verified to the math at 1/255).
- **Calibration presets** — Save/Load the fitted calibration (`.cmcal`) to reuse
  across clips and projects.
- **Detection overlay** — "Show detected patches" markers to verify detection.
- **Output modes** — SDR (default); HDR HLG/PQ (experimental, unverified hardware).
- **Chart types** — ColorChecker Classic (24) and SG (140, with a user-supplied
  reference file).
- **Lumetri `.cube` export** — bakes the effect's exact transform; reproduces the
  effect in Lumetri's Input LUT (~0.5% mean over the chart).
- **UXP analysis panel** — exports the current frame at full sequence resolution
  (so detection isn't limited by the Program Monitor's preview resolution) and
  feeds the effect a preset.
- **CLI toolset** — `chromacal_solve` (calibrate), `chromacal_apply` (apply a
  preset with the effect's exact transform), `chromacal_cube` (Lumetri cube),
  `chromacal_lutapply` (apply a `.cube`), `chromacal_diff`, and `inspect_patches`
  (annotated "what was detected" image).
- **Test suite** — `ctest` covers detection, calibration fixed-point/idempotency,
  and cube↔effect parity; `tests/ppro_parity.sh` checks the live effect vs the
  headless math in Premiere.
- **Distribution** — self-contained bundle (`CHROMACAL_SELF_CONTAINED`), universal
  binary (`CHROMACAL_UNIVERSAL`), Developer-ID sign + notarize (`notarize.sh`), and
  a signed/notarizable `.pkg` installer (`make_installer.sh`).

### v0.1 scope
macOS, SDR Rec.709, ColorChecker Classic — fully implemented, tested, and
signed (self-contained bundle + `.pkg`). The only step needing your credentials
is **notarization** (Apple ID app-password); see `plugin/RELEASING.md`.

### Experimental / post-v0.1 (need external hardware, accounts, or licenses)
- **HDR (PQ/HLG)** — transfer-function math unit-tested (SMPTE/ARIB); needs an HDR
  monitor for visual sign-off.
- **GPU render path** (`CHROMACAL_GPU`) — off by default; needs a GPU + Premiere.
- **Windows** — source supports it; needs a Windows build + Premiere test.
- **UXP panel native solve** — needs the gated Premiere UXP Hybrid SDK (the panel
  exports the full-res frame + the CLI solves meanwhile).
- **SG-140 chart** — needs a user-supplied reference Lab file (X-Rite data is
  licensed).
