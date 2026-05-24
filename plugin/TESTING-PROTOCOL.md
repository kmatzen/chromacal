# chromacal integration testing protocol

The pipeline is **detect → solve → apply**, in three places that must agree: the
headless core (CLI/tests), the native Premiere/AE effect, and the UXP panel. This
protocol pins them down so a change can't silently break the picture again.

## Fixtures

- `docs/before.png` — 1280×720 frame with a ColorChecker (detects 24/24).
- Add your own frames under `tests/fixtures/` covering the cases that bite:
  square-on/well-lit, oblique, under/over-exposed, small-in-frame. Each is a
  "before" the protocol runs end-to-end.

## See the detected target

To eyeball what the pipeline detected on any frame (the same centers the effect
overlays), write an annotated image + an auto-zoomed crop:

```bash
cmake --build build --target inspect_patches
./build/inspect_patches frame.png /tmp/detected.png
# -> /tmp/detected.png (full) + /tmp/detected_zoom.png (cropped to the chart):
#    crosshair + index at each patch center, a disc of the sampled color, and a
#    ring colored by reliability (green clean -> red unreliable). Also prints the
#    chart width in px — < ~120px (≈20px/patch) means analyze at higher resolution.
```

## Layer 1 — Automated headless tests (CI, no Premiere)

Run everything:

```bash
cmake -S . -B build -DCHROMACAL_BUILD_PPRO=ON -DCHROMACAL_BUILD_TESTS=ON
cmake --build build --target chromacal_tests
ctest --test-dir build --output-on-failure        # smoke + unit + integration
./build/chromacal_tests "[integration]"            # just the pipeline tests
```

`tests/test_integration.cpp` encodes the **fixed-point** idea: a correct pipeline,
applied to a chart, produces a frame whose patches already match the reference —
so re-calibrating it is a near no-op. `apply_effect` there mirrors the native
effect exactly (`solver.infer()` = the effect's tone curve + CCM; plus the
working-gamma encode), so these test the *shipped* math.

| Test | Asserts | Catches |
|---|---|---|
| `ppro_core_smoke` (CTest) | `docs/before.png` → 24 patches | detection / build regressions |
| `cube_reproduces_effect` (CTest) | exported cube ≈ effect's apply, mean < 2% (via `chromacal_lutapply`, trilinear — no Premiere) | a cube-baking regression (e.g. the sRGB bake, ~6%) |
| fixed point — patch centers | re-applying the re-fit calibration moves patches < 0.03 mean (sampled at the same pixels, order-free) | sampling bias, encode mismatches |
| fixed point — gray ramp | re-applying to a neutral ramp is identity (< 0.03) | white-balance / tone drift |
| convergence | 2nd-pass `final_error` < 1st-pass | the calibration actually reduces error |

A single saturated/out-of-gamut patch can drift more than the mean (a 3×3 CCM
can't reach every color); the patch-center test tolerates that (`max < 0.35`) while
asserting the aggregate.

## Layer 2 — Headless ↔ Premiere parity (panel + one script)

Verifies the **effect renders the same pixels** as the headless reference — the
check that would have caught the GPU/linear-space and encoding bugs. Premiere has
no headless render for AE-style effects, so the frames are captured *inside*
Premiere by the UXP panel (full-resolution `Exporter.exportSequenceFrame`), and
one script does the rest.

1. In Premiere, on a clip with the **chromacal** effect: **Load calibration** →
   `p.cmcal` (from `chromacal_solve … p.cmcal` or the panel's *Save preset*).
2. Set the effect's **Apply OFF** → panel **Export current frame** → note the
   `/tmp/cc_export_*.png` path (this is the **raw** frame).
3. Set **Apply ON** → panel **Export current frame** again → that path is the
   **Premiere-applied** frame.
4. Run the parity check:

```bash
tests/ppro_parity.sh RAW.png p.cmcal PPRO.png 0.02   # tolerance in [0,1] (~5/255)
# -> applies p.cmcal to RAW with the effect's exact transform and diffs vs PPRO
# -> "PARITY OK" / non-zero exit + saved core reference on "PARITY FAIL"
```

A failure means the effect's render diverged from the core (color management,
encode, GPU path) — investigate the effect, not the core. Keep the sequence **SDR
Rec.709, Full playback resolution** so the comparison is apples-to-apples.

> The math (apply/diff) can't run inside UXP (sandbox: no subprocess), and the
> native addon would need the gated UXP Hybrid SDK — so the script is the external
> half. The two manual clicks (toggle Apply, export) are the irreducible part:
> Premiere won't apply an effect + export a frame from a headless CLI.

## Layer 3 — Lumetri cube parity

The exported `.cube` for Lumetri's **Input LUT** is the effect's **exact** transform
(`write_effect_cube`) — Premiere applies an Input LUT *directly*, with no sRGB
management around it (measured: this direct cube matches the effect ~0.5% mean over
the chart; an sRGB-compensated bake runs ~6% hot — see the color-management memory).

```bash
# Generate the Lumetri cube from the same preset (== the effect's Export .cube).
# gamma MUST match the sequence's SDR gamma (2.4 Rec.709; 2.2/1.96 if set).
./build/plugin/chromacal_cube /tmp/p.cmcal /tmp/lumetri.cube 2.4
#   --srgb : legacy sRGB-managed bake, only if your Premiere color-manages the LUT
```

1. On the **raw** clip with the **chromacal effect removed (or Apply OFF)** — don't
   double-correct — add **Lumetri Color → Basic Correction → Input LUT → Browse →
   `/tmp/lumetri.cube`**.
2. Export that frame → `LUMETRI.png`; export the untouched frame → `RAW.png`.
3. Compare to the effect's math (a little slack for the 33³ LUT interpolation in the
   steep nonlinear region — neutrals match to ~0, saturated channels to ~5%):

```bash
tests/ppro_parity.sh RAW.png /tmp/p.cmcal LUMETRI.png 0.06
```

`PARITY OK` means the Lumetri Input LUT reproduces the chromacal effect. If it runs
**hot** (≈6%, lifted shadows), the cube was the `--srgb` variant or your Premiere
*does* color-manage the LUT; if it's a **gamma mismatch**, the cube's gamma ≠ the
sequence's SDR gamma.

> The cube↔effect *math* is now covered headlessly by the `cube_reproduces_effect`
> CTest (Layer 1) — so this Premiere step is only needed to re-confirm Premiere
> applies the Input LUT directly (established 2026-05-23, Premiere 26.2.2). Day to
> day, Layer 1 catches cube regressions without opening Premiere.
