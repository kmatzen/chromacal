# chromacal — user guide

DaVinci Resolve–style **Color Match** for Premiere Pro / After Effects: shoot an
X-Rite/Calibrite **ColorChecker**, and chromacal solves a camera calibration (tone
curve + color matrix) and applies it to your footage.

## Install (macOS)

Double-click **`chromacal-<version>.pkg`** and follow the prompts. It installs the
effect into Premiere/After Effects' shared plug-in folder; **restart** the app.

> Building from source instead? See `plugin/effect/README.md`, then
> `plugin/effect/make_installer.sh` to produce the `.pkg`.

You'll then find **chromacal** under **Effects ▸ Video Effects ▸ chromacal**.

## Calibrate a clip

1. Put a clip that contains the ColorChecker on the timeline and drop the
   **chromacal** effect on it.
2. Set the **Program Monitor playback resolution to Full** (bottom-right of the
   monitor). This matters: Analyze reads the frame at the *preview* resolution, and
   a small/soft chart detects poorly. If the chart is small in frame, chromacal
   warns you.
3. Park the playhead on a frame where the chart is **square-on and evenly lit**, and
   click **Analyze current frame**. It reports the patch count and reliability.
4. Tick **Apply calibration**. The clip is now graded.

**Tip — best accuracy:** use the **chromacal panel** (Window ▸ Extensions) →
*Calibrate from current frame*. It analyzes at the *full sequence resolution*
regardless of preview quality, then **Save calibration preset…**; in the effect,
**Load calibration** → that file → Apply.

## Controls (Effect Controls)

| Control | What it does |
|---|---|
| **Analyze current frame** | Detect the chart + solve a calibration on the playhead frame |
| **Apply calibration** | Toggle the live correction on/off |
| **Output** | SDR (default) / HDR HLG / HDR PQ (HDR is experimental) |
| **Save / Load calibration** | Reuse one calibration across clips/projects (`.cmcal`) |
| **Show detected patches** | Overlay markers on the detected patches (to verify detection) |
| **Chart** | ColorChecker Classic (24) or SG (140 — needs a reference file) |
| **Export .cube for Lumetri** | Bake the calibration to a LUT for Lumetri ▸ Basic Correction ▸ Input LUT (matches the effect ~0.5%) |

## Troubleshooting

- **"no ColorChecker found"** — make the chart fill more of the frame, light it
  evenly, avoid glare; analyze at **Full** resolution. It keeps retrying while the
  clip's media is still loading.
- **Grade looks off** — re-Analyze on a flatter, better-lit frame; check **Show
  detected patches** lands the markers on the patches.
- **Effect not in the list** — confirm the install (restart Premiere); it's under
  the **chromacal** category.

## Reuse the calibration elsewhere

The exported **`.cube`** works in any app with a LUT slot (Resolve, etc.). The
**`.cmcal`** preset is chromacal's own format, loaded by the effect.
