#pragma once

#include <array>
#include <string>
#include <vector>

namespace chromacal_ppro {

/// Result of a single calibration run, in a plain-data form that is easy to
/// marshal across the JS <-> C++ boundary (no OpenCV/Ceres types leak out).
struct SolveResult {
    bool ok = false;
    std::string error;          ///< Human-readable failure reason when !ok.
    int patches_detected = 0;   ///< Number of ColorChecker patches found.
    double min_reliability = 1.0; ///< Lowest per-patch reliability (0..1].
    double final_error = 0.0;   ///< Final solver cost.
    std::array<double, 9> ccm{};  ///< Row-major 3x3 color correction matrix.
    std::array<double, 4> luma{}; ///< Log-polynomial tone curve coefficients.
    int overlay_count = 0;        ///< Number of detected patch centers below.
    float overlay_xy[48] = {};    ///< Patch centers, normalized [0,1] (x,y interleaved).
};

/// The plugin's pure core, shared by the native effect and the headless CLI.
///
/// Loads `image_path` (a frame grab containing a ColorChecker), detects the
/// chart, solves a calibration (the solver down-weights unreliable patches
/// internally via their reliability score), and writes an Iridas/Resolve
/// `.cube` 3D LUT to `cube_path`.
///
/// This translation unit depends only on chromacal + OpenCV (no Adobe SDK), so
/// it compiles and is verifiable independently via the CLI / smoke test.
SolveResult solve_from_image(const std::string& image_path,
                             const std::string& cube_path,
                             int lut_size = 33);

/// Detect + solve directly from an in-memory, packed 32-bit-float BGRA frame
/// (the layout Premiere hands a video effect as PrPixelFormat_BGRA_4444_32f),
/// returning the calibration parameters without writing any file.
///
/// Deliberately OpenCV-free in its signature so the native effect can call it
/// without pulling OpenCV into the same translation unit as the After Effects
/// SDK headers (which clash). Channel order per pixel is B, G, R, A; values are
/// nominally [0, 1]. `row_stride_floats` is the number of floats per row
/// (rowbytes / 4), which may exceed width*4 due to padding.
/// `chart`: 0 = ColorChecker Classic (24, default), 1 = ColorChecker SG (140).
/// `reference_path`: for SG, a whitespace/tab `Patch L a b` reference file (the
/// SG reference isn't bundled — see read_reference_lab); ignored for Classic.
SolveResult solve_from_bgra_f32(const float* bgra, int width, int height,
                                int row_stride_floats, int chart = 0,
                                const std::string& reference_path = std::string());

/// Bake a calibration (4 log-poly tone-curve coeffs + row-major 3x3 CCM) into a
/// display-referred Iridas/Resolve `.cube` for use as a Lumetri Input LUT.
/// Reproduces the native effect's output: encode(CCM * toneCurve(input)).
/// `gamma > 0` => pure power encode (SDR, e.g. 2.4); `gamma <= 0` => sRGB OETF.
/// Creates parent directories. Returns true on success.
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

/// Apply the native effect's exact transform — encode( CCM * toneCurve(input) )
/// with a working-gamma encode (`gamma > 0` => pow(c, 1/gamma); `gamma <= 0` =>
/// sRGB) — to an image file and write the result. This is the canonical headless
/// apply the Premiere effect must match; use it to generate a reference "after"
/// frame for the headless↔Premiere parity check. Returns false on I/O failure.
bool apply_calibration_to_image(const std::string& in_path, const std::string& out_path,
                                const double* luma, const double* ccm, double gamma);

/// Per-channel difference between two same-size images, in [0,1] units. Writes
/// the mean and max absolute channel difference. Returns false if the images
/// can't be read or differ in size (the parity check's comparator).
bool image_diff(const std::string& a_path, const std::string& b_path,
                double* mean_out, double* max_out);

} // namespace chromacal_ppro
