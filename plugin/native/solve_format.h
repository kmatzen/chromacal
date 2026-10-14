#pragma once

// Calibration-preset / .cube-format helpers with NO OpenCV, Ceres, or
// OpenColorIO dependency — plain standard-library file I/O and math only.
// Kept in their own translation unit (separate from solve_core.cpp, which
// pulls in the full native detect+solve stack) so they can be built and
// unit-tested with just a C++17 compiler, no native image/optimization
// libraries required. See plugin/tests/test_solve_format.cpp.

#include <array>
#include <string>
#include <vector>

namespace chromacal_ppro {

/// Bake a calibration (4 log-poly tone-curve coeffs + row-major 3x3 CCM) into a
/// display-referred Iridas/Resolve `.cube` for use as a Lumetri Input LUT, with
/// the sRGB<->working transcode baked in. Use this only if your Premiere
/// color-manages the Input LUT; otherwise it runs ~6% hot — for the effect's
/// exact transform (no sRGB compensation) use write_effect_cube. `gamma > 0` =>
/// pure power encode (SDR, e.g. 2.4); `gamma <= 0` => sRGB OETF. Creates parent
/// directories. Returns true on success.
bool write_display_cube(const double* luma, const double* ccm,
                        const std::string& path, int lut_size, double gamma);

/// Bake the effect's *exact* transform as a .cube — encode(CCM*toneCurve(in)) with
/// a pure working-gamma encode, no sRGB compensation. For a LUT slot Premiere
/// applies directly (no color management around it), this reproduces the effect;
/// it's the A/B alternative to write_display_cube's sRGB-managed baking.
bool write_effect_cube(const double* luma, const double* ccm,
                       const std::string& path, int lut_size, double gamma);

/// Save / load a calibration (4 tone-curve coeffs + row-major 3x3 CCM) as a
/// small text preset, so one chart read can be reused across clips/projects.
bool write_calibration(const double* luma, const double* ccm, const std::string& path);
bool read_calibration(const std::string& path, double* luma, double* ccm);

/// Load reference CIE L*a*b* (D50) values from a whitespace/tab-delimited file
/// (`Patch  L  a  b` per row; a header row or any non-numeric row is skipped),
/// in file order. For custom charts (e.g. ColorChecker SG) whose reference data
/// you supply — X-Rite's SG data is licensed personal/educational, so it is NOT
/// embedded here; point this at your own reference file. Empty vector on failure.
std::vector<std::array<double, 3>> read_reference_lab(const std::string& path);

} // namespace chromacal_ppro
