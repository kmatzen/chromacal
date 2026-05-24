# Releasing chromacal

## What CI guarantees (automatic, every push)

`ci.yml` (ubuntu + macOS) builds the core + plugin (`CHROMACAL_BUILD_PPRO=ON`) and
runs `ctest`: detection, calibration fixed-point/idempotency, and **cube↔effect
parity**. Green CI = the math and the cube are intact.

## Cut a macOS release (one command)

```bash
export PRSDK_DIR=".../Premiere Pro 26.0 C++ SDK/Examples/Headers"
export AESDK_DIR=".../After Effects SDK/Examples/Headers"
# For a distributable, signed+notarized build also export:
#   CODESIGN_ID, CHROMACAL_INSTALLER_ID, NOTARY_PROFILE   (see release.sh)
plugin/effect/release.sh        # -> self-contained bundle + chromacal-<ver>.pkg
```

This builds the self-contained bundle (verified: ~89 MB, no host paths, loads
without dev tools), optionally signs/notarizes it, and emits the `.pkg` installer.

## Or: automate it in GitHub Actions (`release-macos.yml`)

Pushing a `v*` tag can build, **sign, notarize, package, and attach the `.pkg`** to
the GitHub Release automatically. CI does everything *except* obtain the Adobe SDKs
(not redistributable). Set these up once:

- **Adobe SDKs** (not redistributable — can't live in this public repo) — pick one:
  (a) a **self-hosted macOS runner** that has them (`ADOBE_SDK_DIR` variable + change
  `runs-on`); (b) a **private GitHub repo** holding `adobe-sdks.tar.gz` as a release
  asset — set the `ADOBE_SDK_REPO` + `ADOBE_SDK_TAG` variables and an `ADOBE_SDK_PAT`
  secret (a fine-grained PAT with **Contents: read** on that repo); or (c) any
  private direct-download URL via the `ADOBE_SDK_TARBALL_URL` secret. The tarball is
  just the two SDKs' `Examples/Headers` (+ the AE `Headers/SP` and `Examples/Util`)
  laid out so a path like `*Premiere*/Examples/Headers` and a non-Premiere
  `Examples/Headers` both resolve (~0.5 MB).
- **Repo secrets:** `DEVELOPER_ID_APP_P12`, `DEVELOPER_ID_INSTALLER_P12` (base64 of
  the `.p12` exports), `DEVELOPER_ID_P12_PASSWORD`, `KEYCHAIN_PASSWORD`,
  `CODESIGN_ID`, `INSTALLER_ID`, and `NOTARY_APPLE_ID` / `NOTARY_TEAM_ID` /
  `NOTARY_PASSWORD` (app-specific password). Export a `.p12` and base64 it with
  `base64 -i cert.p12 | pbcopy`.

With those in place, `git tag v0.1.0 && git push --tags` produces a notarized,
double-click `.pkg` on the release — no local steps. `ci.yml` runs the test gate on
the same tag.

## Pre-release checklist

- [ ] `ctest` green locally and in CI.
- [ ] Bump `AE_Effect_Version` in `plugin/effect/chromacal_effect.r` and the
      version macros in `chromacal_effect.h`; update `CHANGELOG.md`.
- [ ] `release.sh` produces the `.pkg`; install it on a **clean Mac** (no
      Homebrew) and confirm the effect loads + Analyze/Apply work.
- [ ] Run the Premiere parity checks once on real footage:
      `tests/ppro_parity.sh` (live effect) and Layer 3 in `TESTING-PROTOCOL.md`
      (Lumetri cube).
- [ ] Tag the release and attach the signed `.pkg`.

## v0.1 release scope (macOS, SDR, ColorChecker Classic) — releasable

Everything v0.1 ships is **implemented, tested, and verified here**:

- Effect (Analyze / Apply / Save·Load presets / overlay / Export .cube) — parity
  to the math at 1/255; cube reproduces the effect ~0.5%.
- Detection — `ctest` (smoke + fixed-point + cube parity); transfer-function math
  unit-tested (`test_encode`, SMPTE/ARIB references).
- Self-contained signed bundle — built with `release.sh`, **signed with your
  Developer ID** and verified (`valid on disk, satisfies its Designated
  Requirement`), packaged as a `.pkg`.

**The one remaining step to ship v0.1 is notarization**, which needs a secret only
you have — your Apple ID app-specific password:

```bash
xcrun notarytool store-credentials chromacal \
  --apple-id you@example.com --team-id R5326Y7EZ4 --password <app-specific-pw>
CODESIGN_ID="Developer ID Application: Kevin Blackburn-Matzen (R5326Y7EZ4)" \
CHROMACAL_INSTALLER_ID="Developer ID Installer: Kevin Blackburn-Matzen (R5326Y7EZ4)" \
NOTARY_PROFILE=chromacal PRSDK_DIR=… AESDK_DIR=… plugin/effect/release.sh
```

(You'll also need a **Developer ID Installer** cert for the `.pkg` — you currently
have the Application cert; add the Installer one in the Apple Developer portal.)

## Post-v0.1 / experimental — need external resources

Each has an irreducible part that requires hardware/accounts/licenses an assistant
can't supply; they're wired + documented, not v0.1 blockers:

1. **Windows** — code supports it (`save_dialog_win.cpp`, `_WIN32`, `comdlg32`);
   build per `WINDOWS.md` (Adobe SDKs + `PiPLtool` for the PiPL) and verify in
   Premiere on Windows.
2. **HDR output (PQ/HLG)** — math verified (`test_encode`); confirm on an HDR
   monitor in an HDR sequence before un-flagging it as experimental.
3. **GPU render path** (`CHROMACAL_GPU`) — off by default. The Metal *kernel* is
   verified on a real GPU (`plugin/effect/gpu_kernel_test.mm`, ~5e-7 vs the CPU
   math); only Premiere's SmartRender *integration* (device setup + GPU world
   buffers) is unverified — enable only after testing in Premiere on a GPU (it
   previously broke the preview).
4. **UXP panel native solve** — needs the gated **Premiere UXP Hybrid SDK**
   (`CHROMACAL_UXP_SDK_DIR`). Until then, panel exports the full-res frame + the
   CLI solves (see `README.md`).
5. **SG-140 chart** — needs your reference Lab file (X-Rite data is licensed).
