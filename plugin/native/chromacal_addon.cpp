// chromacal.uxpaddon — Premiere UXP Hybrid native addon.
//
// Exposes one function to the JS panel:
//
//     const addon = require("chromacal.uxpaddon");
//     const r = addon.solveFromPng(framePath, cubePath, lutSize);
//     // r = { ok, error, patches, minReliability, finalError, ccm[9], luma[4] }
//
// All real work lives in solve_core (SDK-free, compiled and tested in the main
// build). This file is only the JS<->C++ marshalling, written against the
// Premiere UXP Hybrid SDK (UxpAddon.h / UxpAddonShared.h / UxpAddonTypes.h).
// Build it by pointing CHROMACAL_UXP_SDK_DIR at the SDK's header directory
// (the one containing UxpAddon.h); see plugin/README.md.

#include "UxpAddon.h" // Premiere UXP Hybrid SDK
#include "solve_core.h"

#include <array>
#include <string>
#include <vector>

namespace {

// --- marshalling helpers (use the global UxpAddonApis dispatch table) -------

addon_value MakeBool(addon_env env, bool v) {
    addon_value out = nullptr;
    Check(UxpAddonApis.uxp_addon_get_boolean(env, v, &out));
    return out;
}

addon_value MakeNumber(addon_env env, double v) {
    addon_value out = nullptr;
    Check(UxpAddonApis.uxp_addon_create_double(env, v, &out));
    return out;
}

addon_value MakeString(addon_env env, const std::string& s) {
    addon_value out = nullptr;
    Check(UxpAddonApis.uxp_addon_create_string_utf8(env, s.c_str(), s.size(), &out));
    return out;
}

void SetProp(addon_env env, addon_value obj, const char* key, addon_value v) {
    Check(UxpAddonApis.uxp_addon_set_named_property(env, obj, key, v));
}

std::string GetString(addon_env env, addon_value v) {
    size_t len = 0;
    Check(UxpAddonApis.uxp_addon_get_value_string_utf8(env, v, nullptr, 0, &len));
    std::vector<char> buf(len + 1, '\0');
    size_t copied = 0;
    Check(UxpAddonApis.uxp_addon_get_value_string_utf8(env, v, buf.data(), len + 1, &copied));
    return std::string(buf.data(), copied);
}

int GetInt(addon_env env, addon_value v, int fallback) {
    int32_t out = fallback;
    if (UxpAddonApis.uxp_addon_get_value_int32(env, v, &out) != addon_ok) return fallback;
    return out;
}

template <size_t N>
void SetNumberArray(addon_env env, addon_value obj, const char* key,
                    const std::array<double, N>& a) {
    addon_value arr = nullptr;
    Check(UxpAddonApis.uxp_addon_create_array_with_length(env, N, &arr));
    for (size_t i = 0; i < N; ++i)
        Check(UxpAddonApis.uxp_addon_set_element(env, arr, static_cast<uint32_t>(i),
                                                 MakeNumber(env, a[i])));
    SetProp(env, obj, key, arr);
}

// --- exported function ------------------------------------------------------
// solveFromPng(framePath: string, cubePath: string, lutSize?: number) -> object
// No entry point may throw across the boundary: catch and return an error value.
addon_value SolveFromPng(addon_env env, addon_callback_info info) {
    try {
        size_t argc = 4;
        addon_value argv[4] = {nullptr, nullptr, nullptr, nullptr};
        Check(UxpAddonApis.uxp_addon_get_cb_info(env, info, &argc, argv, nullptr, nullptr));

        addon_value result = nullptr;
        Check(UxpAddonApis.uxp_addon_create_object(env, &result));

        if (argc < 3) {
            SetProp(env, result, "ok", MakeBool(env, false));
            SetProp(env, result, "error",
                    MakeString(env, "solveFromPng(framePath, cubePath, presetPath, [lutSize])"));
            return result;
        }

        const std::string frame_path = GetString(env, argv[0]);
        const std::string cube_path = GetString(env, argv[1]);
        const std::string preset_path = GetString(env, argv[2]); // .cmcal for the native effect
        const int lut_size = (argc >= 4) ? GetInt(env, argv[3], 33) : 33;

        chromacal_ppro::SolveResult r =
            chromacal_ppro::solve_from_image(frame_path, cube_path, lut_size);

        // Also write a calibration preset the native chromacal effect loads (the
        // full-resolution analysis lives here; the effect just applies it). This
        // is the primary output — the .cube is a bonus for Lumetri users.
        bool preset_ok = false;
        if (r.ok && !preset_path.empty())
            preset_ok = chromacal_ppro::write_calibration(r.luma.data(), r.ccm.data(), preset_path);

        SetProp(env, result, "ok", MakeBool(env, r.ok));
        SetProp(env, result, "error", MakeString(env, r.error));
        SetProp(env, result, "patches", MakeNumber(env, r.patches_detected));
        SetProp(env, result, "minReliability", MakeNumber(env, r.min_reliability));
        SetProp(env, result, "finalError", MakeNumber(env, r.final_error));
        SetProp(env, result, "presetWritten", MakeBool(env, preset_ok));
        SetNumberArray(env, result, "ccm", r.ccm);
        SetNumberArray(env, result, "luma", r.luma);
        return result;
    } catch (...) {
        return CreateErrorFromException(env);
    }
}

// Called by the UXP runtime when the addon is require()'d.
addon_value Init(addon_env env, addon_value exports, const addon_apis& addonAPIs) {
    addon_value fn = nullptr;
    // Pass NULL/0 for the function name (named via set_named_property below),
    // matching the Adobe sample — an explicit name+length can make
    // create_function fail and leave the export unregistered.
    if (addonAPIs.uxp_addon_create_function(env, nullptr, 0, SolveFromPng,
                                            nullptr, &fn) != addon_ok)
        addonAPIs.uxp_addon_throw_error(env, nullptr, "Unable to wrap solveFromPng");
    if (addonAPIs.uxp_addon_set_named_property(env, exports, "solveFromPng", fn) != addon_ok)
        addonAPIs.uxp_addon_throw_error(env, nullptr, "Unable to populate exports");
    return exports;
}

} // namespace

// Required entry points (defined at file scope; the SDK declares them extern "C").
UXP_ADDON_INIT(Init)

void Terminate(addon_env /*env*/) {}
UXP_ADDON_TERMINATE(Terminate)
