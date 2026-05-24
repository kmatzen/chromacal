// chromacal native effect — implementation. See chromacal_effect.h.
//
// Pixel format: requests PrPixelFormat_BGRA_4444_32f so we work in float RGB.
// The Analyze button is serviced inside Render (which already has the input
// frame), keeping us off the parameter-checkout path. Detection/solving is
// delegated to solve_core (OpenCV/chromacal) so this TU never includes OpenCV
// alongside the After Effects headers.

#include "chromacal_effect.h"
#include "chromacal_encode.h" // shared, unit-tested transfer functions
#include "solve_core.h" // chromacal_ppro::solve_from_bgra_f32 / SolveResult

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// Native file dialogs (macOS: save_dialog_mac.mm, Windows: save_dialog_win.cpp).
// Returns 1 = chosen (path in outBuf), 0 = cancelled, -1 = unavailable.
#if defined(__APPLE__) || defined(_WIN32)
extern "C" int chromacal_choose_save_path(const char* suggestedName, const char* ext,
                                          char* outBuf, unsigned long outSize);
extern "C" int chromacal_choose_open_path(char* outBuf, unsigned long outSize);
#else
static int chromacal_choose_save_path(const char*, const char*, char*, unsigned long) { return -1; }
static int chromacal_choose_open_path(char*, unsigned long) { return -1; }
#endif

namespace {

ChromacalSeqData* LockSeq(PF_InData* in_data, PF_OutData* out_data) {
    if (!in_data->sequence_data) return nullptr;
    AEFX_SuiteScoper<PF_HandleSuite1> hs(in_data, kPFHandleSuite,
                                         kPFHandleSuiteVersion1, out_data);
    return reinterpret_cast<ChromacalSeqData*>(
        hs->host_lock_handle(in_data->sequence_data));
}

void UnlockSeq(PF_InData* in_data, PF_OutData* out_data) {
    if (!in_data->sequence_data) return;
    AEFX_SuiteScoper<PF_HandleSuite1> hs(in_data, kPFHandleSuite,
                                         kPFHandleSuiteVersion1, out_data);
    hs->host_unlock_handle(in_data->sequence_data);
}

PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef*[],
                   PF_LayerDef*) {
    out_data->my_version =
        PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);

    if (in_data->appl_id == 'PrMr') {
        AEFX_SuiteScoper<PF_PixelFormatSuite1> pfs(
            in_data, kPFPixelFormatSuite, kPFPixelFormatSuiteVersion1, out_data);
        (*pfs->ClearSupportedPixelFormats)(in_data->effect_ref);
        (*pfs->AddSupportedPixelFormat)(in_data->effect_ref,
                                        PrPixelFormat_BGRA_4444_32f);
    }

    out_data->out_flags |= PF_OutFlag_USE_OUTPUT_EXTENT | PF_OutFlag_DISPLAY_ERROR_MESSAGE;
    out_data->out_flags2 |= PF_OutFlag2_PRESERVES_FULLY_OPAQUE_PIXELS;
    return PF_Err_NONE;
}

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef*[],
                   PF_LayerDef*) {
    PF_ParamDef def;

    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON("ColorChecker", "Analyze current frame",
                  0, PF_ParamFlag_SUPERVISE, CC_ANALYZE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX("Apply calibration", "", TRUE, 0, CC_APPLY);

    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON("LUT", "Export .cube for Lumetri", 0, PF_ParamFlag_SUPERVISE, CC_EXPORT);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Output", 3 /*choices*/, 1 /*default = SDR*/,
                 "SDR (Rec.709)|HDR HLG (Rec.2100)|HDR PQ (Rec.2100)", CC_HDR);

    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON("Calibration", "Save calibration\xE2\x80\xA6", 0, PF_ParamFlag_SUPERVISE, CC_SAVE_CAL);

    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON("Calibration", "Load calibration\xE2\x80\xA6", 0, PF_ParamFlag_SUPERVISE, CC_LOAD_CAL);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX("Show detected patches", "", FALSE, 0, CC_OVERLAY);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Chart", 2 /*choices*/, 1 /*default = Classic*/,
                 "ColorChecker Classic (24)|ColorChecker SG (140)", CC_CHART);

    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON("SG reference", "Load SG reference\xE2\x80\xA6", 0, PF_ParamFlag_SUPERVISE, CC_LOADREF);

    out_data->num_params = CC_NUM_PARAMS;
    return PF_Err_NONE;
}

PF_Err SequenceSetup(PF_InData* in_data, PF_OutData* out_data) {
    AEFX_SuiteScoper<PF_HandleSuite1> hs(in_data, kPFHandleSuite,
                                         kPFHandleSuiteVersion1, out_data);
    PF_Handle h = hs->host_new_handle(sizeof(ChromacalSeqData));
    if (!h) return PF_Err_OUT_OF_MEMORY;
    ChromacalSeqData* d = reinterpret_cast<ChromacalSeqData*>(hs->host_lock_handle(h));
    if (d) {
        memset(d, 0, sizeof(*d));
        d->magic = CHROMACAL_MAGIC;
    }
    hs->host_unlock_handle(h);
    out_data->sequence_data = h;
    return PF_Err_NONE;
}

PF_Err SequenceResetup(PF_InData* in_data, PF_OutData* out_data) {
    // Flat POD: a flattened handle reloaded from a project is already valid,
    // unless it predates a struct change (smaller than the current size) — in
    // that case reallocate fresh so newer fields don't read/write out of bounds.
    if (in_data->sequence_data) {
        AEFX_SuiteScoper<PF_HandleSuite1> hs(in_data, kPFHandleSuite,
                                             kPFHandleSuiteVersion1, out_data);
        A_HandleSize sz = hs->host_get_handle_size(in_data->sequence_data);
        if (sz >= static_cast<A_HandleSize>(sizeof(ChromacalSeqData))) {
            out_data->sequence_data = in_data->sequence_data;
            return PF_Err_NONE;
        }
        hs->host_dispose_handle(in_data->sequence_data);
    }
    return SequenceSetup(in_data, out_data);
}

PF_Err SequenceSetdown(PF_InData* in_data, PF_OutData* out_data) {
    if (in_data->sequence_data) {
        AEFX_SuiteScoper<PF_HandleSuite1> hs(in_data, kPFHandleSuite,
                                             kPFHandleSuiteVersion1, out_data);
        hs->host_dispose_handle(in_data->sequence_data);
    }
    out_data->sequence_data = nullptr;
    return PF_Err_NONE;
}

// Both buttons just raise a flag; the work happens in Render, where the
// sequence data carries the live calibration. Mutations made in Render don't
// propagate back to this (button) selector's copy of the sequence data, so the
// export must read `calibrated` from the render side, not here.
PF_Err UserChangedParam(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef*[],
                        const PF_UserChangedParamExtra* extra) {
    if (!extra) return PF_Err_NONE;
    if (extra->param_index == CC_ANALYZE) {
        ChromacalSeqData* d = LockSeq(in_data, out_data);
        if (d) { d->analyze_requested = 1; d->analyze_attempts = 0; }
        UnlockSeq(in_data, out_data);
        out_data->out_flags |= PF_OutFlag_FORCE_RERENDER;

    } else if (extra->param_index == CC_EXPORT) {
        // Ask where to save first (this selector is on the UI thread). The actual
        // write happens in Render, where the calibration is visible.
        char chosen[1024] = {0};
        int pick = chromacal_choose_save_path("chromacal.cube", "cube", chosen, sizeof(chosen));
        if (pick == 0) return PF_Err_NONE; // user cancelled
        ChromacalSeqData* d = LockSeq(in_data, out_data);
        if (d) {
            d->export_requested = 1;
            if (pick == 1) {
                strncpy(d->export_path, chosen, sizeof(d->export_path) - 1);
                d->export_path[sizeof(d->export_path) - 1] = '\0';
            } else {
                d->export_path[0] = '\0'; // no dialog -> default LUT folder
            }
        }
        UnlockSeq(in_data, out_data);
        out_data->out_flags |= PF_OutFlag_FORCE_RERENDER;

    } else if (extra->param_index == CC_SAVE_CAL) {
        char chosen[1024] = {0};
        int pick = chromacal_choose_save_path("chromacal.cmcal", "cmcal", chosen, sizeof(chosen));
        if (pick != 1) return PF_Err_NONE; // cancelled or unavailable
        ChromacalSeqData* d = LockSeq(in_data, out_data);
        if (d) {
            d->save_cal_requested = 1;
            strncpy(d->cal_path, chosen, sizeof(d->cal_path) - 1);
            d->cal_path[sizeof(d->cal_path) - 1] = '\0';
        }
        UnlockSeq(in_data, out_data);
        out_data->out_flags |= PF_OutFlag_FORCE_RERENDER;

    } else if (extra->param_index == CC_LOAD_CAL) {
        char chosen[1024] = {0};
        int pick = chromacal_choose_open_path(chosen, sizeof(chosen));
        if (pick != 1) return PF_Err_NONE; // cancelled or unavailable
        ChromacalSeqData* d = LockSeq(in_data, out_data);
        if (d) {
            d->load_cal_requested = 1;
            strncpy(d->cal_path, chosen, sizeof(d->cal_path) - 1);
            d->cal_path[sizeof(d->cal_path) - 1] = '\0';
        }
        UnlockSeq(in_data, out_data);
        out_data->out_flags |= PF_OutFlag_FORCE_RERENDER;

    } else if (extra->param_index == CC_LOADREF) {
        // Store the SG reference Lab file; the next Analyze (in Render) uses it.
        char chosen[1024] = {0};
        int pick = chromacal_choose_open_path(chosen, sizeof(chosen));
        if (pick != 1) return PF_Err_NONE; // cancelled or unavailable
        ChromacalSeqData* d = LockSeq(in_data, out_data);
        if (d) {
            strncpy(d->ref_path, chosen, sizeof(d->ref_path) - 1);
            d->ref_path[sizeof(d->ref_path) - 1] = '\0';
        }
        UnlockSeq(in_data, out_data);
    }
    return PF_Err_NONE;
}

// chromacal model: per-channel log-polynomial tone curve, then 3x3 CCM.
// Float footage can contain negative (super-black) or NaN/Inf samples; log() of
// those is NaN and would propagate to black blocks, so clamp into the log domain
// and guard the result.
inline double ToneCurve(double c, const double* p) {
    if (!(c > 0.0)) c = 0.0; // NaN or <= 0 -> 0 (NaN fails the comparison)
    double l = log(c + 1e-6);
    double v = exp(p[0] + p[1] * l + p[2] * l * l + p[3] * l * l * l);
    return (v == v && v < 1e12) ? v : 0.0; // reject NaN/Inf
}

// chromacal produces *linear* RGB in Rec.709/sRGB primaries. We must re-encode
// it into the sequence's working color space, queried from Premiere — it is NOT
// always sRGB (Rec.709 SDR uses a pure ~2.4 gamma; HDR uses PQ/HLG with Rec.2020
// primaries). The input side needs no decode: Analyze fit the calibration on
// these same working-space pixels, so the model already maps them to linear.

enum EncodeMode { ENC_SRGB, ENC_GAMMA, ENC_HLG, ENC_PQ };

struct OutputEncoding {
    EncodeMode mode = ENC_SRGB; // safe fallback when the suites are unavailable
    double gamma = 2.4;         // for ENC_GAMMA, from GetSDRGamma
    double white = 100.0;       // graphics white luminance (nits)
};

// Transfer functions (sRGB / PQ / HLG OETFs, Rec.709->Rec.2020) live in the
// shared, unit-tested chromacal_encode.h.
using chromacal_enc::HlgEncode;
using chromacal_enc::PqEncode;
using chromacal_enc::SrgbEncode;
using chromacal_enc::ToRec2020;

inline double EncodeChannel(double c, const OutputEncoding& e) {
    if (!(c > 0.0)) return 0.0; // NaN or <= 0 -> black
    double out;
    switch (e.mode) {
    case ENC_GAMMA: out = pow(c, 1.0 / e.gamma); break;
    case ENC_PQ:    out = PqEncode(c, e.white); break;
    case ENC_HLG:   out = HlgEncode(c); break;
    case ENC_SRGB:
    default:        out = SrgbEncode(c); break;
    }
    return (out == out) ? out : 0.0; // reject NaN
}

// Choose how to encode the output. We use the sequence's SDR gamma and always
// take the SDR path: GetGraphicsWhiteLuminance proved unreliable as an HDR
// signal (it returns the HLG reference 203 even for SDR "Direct" Rec.709
// sequences), and the HDR (PQ/HLG + Rec.2020) encode is unverified. So default
// to SDR gamma; real HDR support needs a reliable working-color-space check.
OutputEncoding DetermineEncoding(PF_InData* in_data) {
    OutputEncoding enc;
    enc.mode = ENC_GAMMA;
    enc.gamma = 2.4;
    if (in_data->appl_id != 'PrMr' || !in_data->pica_basicP) return enc;
    SPBasicSuite* basic = in_data->pica_basicP;

    PF_UtilitySuite13* util = nullptr;
    if (basic->AcquireSuite(kPFUtilitySuite, kPFUtilitySuiteVersion13,
                            (const void**)&util) != 0 || !util)
        return enc;
    PrTimelineID timeline = 0;
    PF_Err e = util->GetContainingTimelineID(in_data->effect_ref, &timeline);
    basic->ReleaseSuite(kPFUtilitySuite, kPFUtilitySuiteVersion13);
    if (e != PF_Err_NONE || timeline == 0) return enc;

    PrSDKSequenceInfoSuite* seq = nullptr;
    if (basic->AcquireSuite(kPrSDKSequenceInfoSuite, kPrSDKSequenceInfoSuiteVersion,
                            (const void**)&seq) != 0 || !seq)
        return enc;
    csSDK_uint32 sdrGamma = 240;
    seq->GetSDRGamma(timeline, &sdrGamma); // 240=2.4, 220=2.2, 196=1.96
    basic->ReleaseSuite(kPrSDKSequenceInfoSuite, kPrSDKSequenceInfoSuiteVersion);

    enc.gamma = sdrGamma > 0 ? sdrGamma / 100.0 : 2.4;
    return enc;
}

PF_Err Render(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
              PF_LayerDef* output) {
    if (in_data->appl_id != 'PrMr') return PF_Err_NONE;

    PF_LayerDef* src = &params[CC_INPUT]->u.ld;
    PF_LayerDef* dest = output;
    const int width = output->width;
    const int height = output->height;

    ChromacalSeqData* d = LockSeq(in_data, out_data);

    // Service a pending Analyze using the frame we already have. Detection can
    // fail transiently because Premiere serves a "Media pending" placeholder
    // while the source decodes; rather than consume the request, keep it pending
    // and retry on subsequent renders (Premiere re-renders when media is ready),
    // up to a bounded number of attempts.
    static const int kMaxAnalyzeAttempts = 48;
    if (d && d->analyze_requested) {
        d->analyze_attempts++;
        // rowbytes is SIGNED (Premiere may hand frames bottom-up, i.e. negative
        // rowbytes); keep the division signed so the stride stays correct.
        const int strideFloats =
            static_cast<int>(src->rowbytes) / static_cast<int>(sizeof(float));
        // Chart popup: 1 = Classic (chart 0), 2 = SG (chart 1, needs ref_path).
        const int chart = (params[CC_CHART]->u.pd.value == 2) ? 1 : 0;
        chromacal_ppro::SolveResult r = chromacal_ppro::solve_from_bgra_f32(
            reinterpret_cast<const float*>(src->data), width, height, strideFloats,
            chart, d->ref_path);
        if (r.ok) {
            for (int i = 0; i < 4; ++i) d->luma[i] = r.luma[i];
            for (int i = 0; i < 9; ++i) d->ccm[i] = r.ccm[i];
            d->patches = r.patches_detected;
            d->overlay_count = r.overlay_count;
            for (int i = 0; i < r.overlay_count * 2 && i < 48; ++i)
                d->overlay_xy[i] = r.overlay_xy[i];
            d->calibrated = 1;
            d->analyze_requested = 0;
            // Warn when the chart is small in the analyzed frame: below ~15 px per
            // patch the MCC detector localizes the grid imprecisely (the markers
            // drift off the patches and the sampled colors are slightly biased).
            // This is common when analyzing at a reduced Program Monitor resolution.
            float mnx = 1.0f, mxx = 0.0f;
            for (int i = 0; i < d->overlay_count; ++i) {
                float x = d->overlay_xy[i * 2];
                if (x < mnx) mnx = x;
                if (x > mxx) mxx = x;
            }
            const double per_patch_px =
                d->overlay_count > 1 ? (mxx - mnx) * width / 5.0 : 0.0; // 6 cols, 5 gaps
            char hint[176] = {0};
            if (per_patch_px > 0.0 && per_patch_px < 15.0)
                snprintf(hint, sizeof(hint),
                         " NOTE: the chart is small (~%.0f px/patch), so detection is "
                         "imprecise — set the Program Monitor to Full resolution and frame "
                         "the chart larger, then Analyze again.",
                         per_patch_px);
            snprintf(out_data->return_msg, PF_MAX_EFFECT_MSG_LEN,
                     "chromacal: calibrated from %d ColorChecker patches "
                     "(min patch reliability %.2f, fit error %.4f).%s",
                     r.patches_detected, r.min_reliability, r.final_error, hint);
            out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
        } else {
            // Tell the user on the first miss, then keep retrying quietly in case
            // the frame was still a "Media pending" placeholder.
            if (d->analyze_attempts == 1) {
                snprintf(out_data->return_msg, PF_MAX_EFFECT_MSG_LEN,
                         "chromacal: no ColorChecker found in this frame. Make sure the "
                         "chart is visible at the playhead and the clip's media is loaded "
                         "(not the \"Media pending\" placeholder). It will keep checking "
                         "while the media loads; click Analyze again to retry.");
                out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
            }
            if (d->analyze_attempts >= kMaxAnalyzeAttempts) {
                d->analyze_requested = 0; // give up; user already notified
            } else {
                out_data->out_flags |= PF_OutFlag_FORCE_RERENDER; // retry next render
            }
        }
    }

    // Service a pending Export. Done here (not in the button handler) because the
    // calibration set during Render isn't visible to the UserChangedParam copy of
    // the sequence data.
    if (d && d->export_requested) {
        d->export_requested = 0;
        if (d->calibrated) {
            // Bake the effect's exact transform (tone curve + CCM + working-gamma
            // encode). Premiere applies an Input LUT directly (no sRGB management
            // around it — measured: this direct cube matches the effect to ~0.5%
            // mean over the chart, vs ~6% hot for an sRGB-compensated bake), so the
            // cube reproduces the effect in Lumetri ▸ Basic Correction ▸ Input LUT.
            const double gamma = DetermineEncoding(in_data).gamma;
            std::string path;
            if (d->export_path[0]) {
                path = d->export_path; // user-chosen location
            } else {
                const char* home = getenv("HOME");
                path = std::string(home ? home : "/tmp") +
                    "/Library/Application Support/Adobe/Common/LUTs/Technical/chromacal.cube";
            }
            bool ok = chromacal_ppro::write_effect_cube(d->luma, d->ccm, path, 33, gamma);
            snprintf(out_data->return_msg, PF_MAX_EFFECT_MSG_LEN,
                     ok ? "chromacal: saved .cube to %s — load it in Lumetri > Basic "
                          "Correction > Input LUT > Browse."
                        : "chromacal: could not write %s",
                     path.c_str());
        } else {
            snprintf(out_data->return_msg, PF_MAX_EFFECT_MSG_LEN,
                     "chromacal: click Analyze first, then Export .cube.");
        }
        out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
    }

    // Service a pending Save calibration (write the live calibration to a preset).
    if (d && d->save_cal_requested) {
        d->save_cal_requested = 0;
        bool ok = d->calibrated &&
                  chromacal_ppro::write_calibration(d->luma, d->ccm, d->cal_path);
        snprintf(out_data->return_msg, PF_MAX_EFFECT_MSG_LEN,
                 ok ? "chromacal: saved calibration to %s"
                    : (d->calibrated ? "chromacal: could not write %s"
                                     : "chromacal: click Analyze first, then Save calibration."),
                 d->cal_path);
        out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
    }

    // Service a pending Load calibration (read a preset into the live calibration).
    if (d && d->load_cal_requested) {
        d->load_cal_requested = 0;
        if (chromacal_ppro::read_calibration(d->cal_path, d->luma, d->ccm)) {
            d->calibrated = 1;
            snprintf(out_data->return_msg, PF_MAX_EFFECT_MSG_LEN,
                     "chromacal: loaded calibration from %s. Toggle Apply to use it.",
                     d->cal_path);
        } else {
            snprintf(out_data->return_msg, PF_MAX_EFFECT_MSG_LEN,
                     "chromacal: could not read a calibration from %s", d->cal_path);
        }
        out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
    }

    const bool apply = params[CC_APPLY]->u.bd.value && d && d->calibrated;
    OutputEncoding enc = apply ? DetermineEncoding(in_data) : OutputEncoding();
    // Opt-in HDR output (popup: 1 SDR, 2 HLG, 3 PQ). SDR is the default and leaves
    // the verified path untouched; HDR re-encodes to Rec.2020 + PQ/HLG (unverified
    // on HDR hardware, but inert unless explicitly selected).
    if (apply) {
        const A_long hdr = params[CC_HDR]->u.pd.value;
        if (hdr == 2) { enc.mode = ENC_HLG; enc.white = 203.0; }
        else if (hdr == 3) { enc.mode = ENC_PQ; enc.white = 300.0; }
    }
    const bool wideGamut = (enc.mode == ENC_PQ || enc.mode == ENC_HLG);

    // Precompute the per-channel tone curve as a 1D LUT (the curve is identical
    // for R/G/B), so the pixel loop avoids three log/exp evaluations per pixel.
    // Cover [0,1]; out-of-range samples (super-black/white) take the exact path.
    static const int kToneLUT = 1024;
    std::vector<float> toneLUT;
    if (apply) {
        toneLUT.resize(kToneLUT + 1);
        for (int i = 0; i <= kToneLUT; ++i)
            toneLUT[i] = static_cast<float>(
                ToneCurve(static_cast<double>(i) / kToneLUT, d->luma));
    }
    auto tone = [&](double c) -> double {
        if (c <= 0.0) return toneLUT[0];
        if (c >= 1.0) return ToneCurve(c, d->luma); // exact for super-white
        double x = c * kToneLUT;
        int i = static_cast<int>(x);
        double f = x - i;
        return toneLUT[i] * (1.0 - f) + toneLUT[i + 1] * f;
    };

    const char* srcRow = reinterpret_cast<const char*>(src->data);
    char* dstRow = reinterpret_cast<char*>(dest->data);
    for (int y = 0; y < height;
         ++y, srcRow += src->rowbytes, dstRow += dest->rowbytes) {
        const float* s = reinterpret_cast<const float*>(srcRow);
        float* o = reinterpret_cast<float*>(dstRow);
        for (int x = 0; x < width; ++x) {
            const float B = s[x * 4 + 0], G = s[x * 4 + 1], R = s[x * 4 + 2],
                        A = s[x * 4 + 3];
            if (apply) {
                const double aR = tone(R);
                const double aG = tone(G);
                const double aB = tone(B);
                const double* m = d->ccm;
                double linR = m[0] * aR + m[1] * aG + m[2] * aB;
                double linG = m[3] * aR + m[4] * aG + m[5] * aB;
                double linB = m[6] * aR + m[7] * aG + m[8] * aB;
                if (wideGamut) ToRec2020(linR, linG, linB); // Rec.2020 for HDR
                o[x * 4 + 2] = static_cast<float>(EncodeChannel(linR, enc)); // R
                o[x * 4 + 1] = static_cast<float>(EncodeChannel(linG, enc)); // G
                o[x * 4 + 0] = static_cast<float>(EncodeChannel(linB, enc)); // B
                o[x * 4 + 3] = A;
            } else {
                o[x * 4 + 0] = B;
                o[x * 4 + 1] = G;
                o[x * 4 + 2] = R;
                o[x * 4 + 3] = A;
            }
        }
    }

    // Optional diagnostic overlay: a magenta cross on each detected patch center
    // (uses the same row addressing as the loop, so it's correct for bottom-up
    // frames too). Drawn after the apply so it sits on top.
    if (params[CC_OVERLAY]->u.bd.value && d && d->overlay_count > 0) {
        char* base = reinterpret_cast<char*>(dest->data);
        auto setpx = [&](int px, int py) {
            if (px < 0 || py < 0 || px >= width || py >= height) return;
            float* row = reinterpret_cast<float*>(
                base + static_cast<ptrdiff_t>(py) * dest->rowbytes);
            row[px * 4 + 0] = 1.0f; // B
            row[px * 4 + 1] = 0.0f; // G
            row[px * 4 + 2] = 1.0f; // R  (magenta marker)
        };
        const int arm = 6;
        for (int i = 0; i < d->overlay_count && i < 24; ++i) {
            int cx = static_cast<int>(d->overlay_xy[i * 2] * width);
            int cy = static_cast<int>(d->overlay_xy[i * 2 + 1] * height);
            for (int t = -arm; t <= arm; ++t) { setpx(cx + t, cy); setpx(cx, cy + t); }
        }
    }

    UnlockSeq(in_data, out_data);
    return PF_Err_NONE;
}

} // namespace

#ifdef MSWindows
#define DllExport __declspec(dllexport)
#else
#define DllExport __attribute__((visibility("default")))
#endif

extern "C" DllExport PF_Err EffectMain(PF_Cmd inCmd, PF_InData* in_data,
                                       PF_OutData* out_data, PF_ParamDef* params[],
                                       PF_LayerDef* inOutput, void* extra) {
    PF_Err err = PF_Err_NONE;
    switch (inCmd) {
    case PF_Cmd_GLOBAL_SETUP:
        err = GlobalSetup(in_data, out_data, params, inOutput);
        break;
    case PF_Cmd_PARAMS_SETUP:
        err = ParamsSetup(in_data, out_data, params, inOutput);
        break;
    case PF_Cmd_SEQUENCE_SETUP:
        err = SequenceSetup(in_data, out_data);
        break;
    case PF_Cmd_SEQUENCE_RESETUP:
        err = SequenceResetup(in_data, out_data);
        break;
    case PF_Cmd_SEQUENCE_SETDOWN:
        err = SequenceSetdown(in_data, out_data);
        break;
    case PF_Cmd_USER_CHANGED_PARAM:
        err = UserChangedParam(in_data, out_data, params,
                               reinterpret_cast<const PF_UserChangedParamExtra*>(extra));
        break;
    case PF_Cmd_RENDER:
        err = Render(in_data, out_data, params, inOutput);
        break;
    }
    return err;
}
