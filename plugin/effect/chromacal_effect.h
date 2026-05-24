// chromacal — native Premiere Pro / After Effects video effect.
//
// Adds a "chromacal" effect to the Effect Controls: an Analyze button detects
// a ColorChecker in the current frame and solves a calibration; the effect
// then applies the fitted tone curve + 3x3 CCM live in the render pipeline.
//
// Built on the AE effect API (PF_*) that Premiere hosts, plus the PrSDK pixel
// suites — so it needs BOTH the After Effects SDK and the Premiere Pro C++ SDK
// headers (see plugin/effect/README.md).

#ifndef CHROMACAL_EFFECT_H
#define CHROMACAL_EFFECT_H

#include "AEConfig.h"
#include "PrSDKTypes.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "A.h"
#include "AE_Macros.h"
#include "AEFX_SuiteHandlerTemplate.h"
#include "Param_Utils.h"
#include "PrSDKAESupport.h"
#include "PrSDKPixelFormat.h"
#include "PrSDKSequenceInfoSuite.h"

#include <math.h>

// Parameter indices.
enum {
    CC_INPUT = 0,
    CC_ANALYZE,   // button: detect + solve on the current frame
    CC_APPLY,     // checkbox: apply the calibration
    CC_EXPORT,    // button: bake a .cube for Lumetri's Input LUT
    CC_HDR,       // popup: output encoding (1 SDR / 2 HLG / 3 PQ)
    CC_SAVE_CAL,  // button: save the calibration to a preset file
    CC_LOAD_CAL,  // button: load a calibration from a preset file
    CC_OVERLAY,   // checkbox: draw markers on the detected patches
    CC_CHART,     // popup: chart type (1 Classic 24 / 2 SG 140)
    CC_LOADREF,   // button: load an SG reference Lab file (SG isn't bundled)
    CC_NUM_PARAMS
};

// Persistent per-instance calibration, stored in the effect's sequence data
// (flat POD so flatten/unflatten are trivial and it round-trips with projects).
struct ChromacalSeqData {
    A_long  magic;             // sentinel for validity across resetup
    A_long  calibrated;        // 0 until a successful Analyze
    A_long  analyze_requested; // set by the Analyze button, serviced in Render
    A_long  export_requested;  // set by the Export button, serviced in Render
    A_long  patches;           // patches found in the last Analyze
    A_long  analyze_attempts;  // render attempts since Analyze (for media-pending retry)
    A_long  save_cal_requested; // set by Save calibration, serviced in Render
    A_long  load_cal_requested; // set by Load calibration, serviced in Render
    double  luma[4];           // log-polynomial tone curve coefficients
    double  ccm[9];            // row-major 3x3 color correction matrix
    char    export_path[1024]; // chosen .cube path (empty => default LUT folder)
    char    cal_path[1024];    // chosen preset path for save/load calibration
    A_long  overlay_count;     // detected patch centers stored below
    float   overlay_xy[48];    // patch centers, normalized [0,1] (x,y interleaved)
    char    ref_path[1024];    // chosen SG reference Lab file (empty => Classic)
};

#define CHROMACAL_MAGIC 0x43484D43 // 'CHMC'

#define MAJOR_VERSION 0
#define MINOR_VERSION 1
#define BUG_VERSION   1
#define STAGE_VERSION PF_Stage_RELEASE
#define BUILD_VERSION 1

#endif // CHROMACAL_EFFECT_H
