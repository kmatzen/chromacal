# Changelog

All notable changes to chromacal. Versions follow the plugin's
`CFBundleShortVersionString` / PiPL `AE_Effect_Version`.

## [0.2.0] - 2026-11-26

Python package, C++ core, and the macOS plugin, 16 commits past 0.1.1. The solver
and detector changed, so calibration results on difficult frames differ from 0.1.1;
presets saved by 0.1.1 load unchanged.

### Fixed
- **Detection: chi-square tail** — `chi_square_sf` truncated its power series at 200
  terms, so large statistics returned ~1.0 instead of decaying to ~0, and the Mardia
  skewness test passed the contaminated patches it should reject hardest
  (specular highlights, occlusion). Replaced with the standard two-regime
  incomplete-gamma evaluation, verified against `scipy.stats.chi2.sf`. (#21)
- **Detection: input dtype** — `detect()` assumed 8-bit BGR and scaled by 1/255
  whatever arrived, so a 16-bit or float frame produced a garbage calibration with
  no error. It now raises `std::invalid_argument`. (#21)
- **White-balance patch selection**, a Shapiro–Wilk off-by-one, and a tone-curve log
  clamp that blew up to 1e23 at black. (#21, `90e67b6`)
- **Preset save/load** — the `color_matrix` type check rejected valid presets,
  because OpenCV's `FileStorage` does not preserve `CV_64FC3`. (`844b74b`)
- **OpenCV 5.x** compatibility, including the `CCheckerDetector` API branch, plus an
  AppleClang OpenMP fallback. (`624606e`)
- Eleven audit findings (6 medium, 4 low), and in the plugin: `write_effect_cube`,
  gamma 2.4, a NaN guard, and the Windows LUT path. (`b01a14b`, `90e67b6`)

### Changed
- **Solver** — Mardia skewness is O(n), was O(n²). Covariance is now per-mean with
  residuals scaled by √pixel_count; reliability covariance excludes outliers
  iteratively; the solve propagates the Jacobian in two passes (input-space, then
  output-space weighting). (`90e67b6`)
- **Detection cost** — per-patch sampling no longer allocates and zeroes a
  full-frame mask for every patch. (#21)

### Added
- `solve_core` tests, and support for local and cloud build/test runs. (#22)

### Packaging
- macOS wheels build on `macos-15` with target 15.8, the Linux dependency prefix
  builds a pinned yaml-cpp before OpenColorIO, CI pins the interpreter, and the
  dependency cache key is content-based. All seven CI jobs are green on main.

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
