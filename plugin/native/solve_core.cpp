#include "solve_core.h"

#include <chromacal/chromacal.h>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <vector>

namespace chromacal_ppro {

// Shared core: detect + solve on an in-memory BGR image. If cube_path is
// non-null, also bake the calibration into a .cube LUT.
static SolveResult run_solve(const cv::Mat& image, const std::string* cube_path,
                             int lut_size, int chart = 0,
                             const std::string& reference_path = std::string()) {
    SolveResult r;

    if (image.empty()) {
        r.error = "Empty image.";
        return r;
    }

    // SG (chart==1) needs a user-supplied reference (X-Rite's data isn't bundled).
    std::vector<cv::Vec3d> ref;
    if (chart == 1) {
        for (const auto& lab : read_reference_lab(reference_path))
            ref.emplace_back(lab[0], lab[1], lab[2]);
        if (ref.size() < 140) {
            r.error = "ColorChecker SG needs a 140-patch reference file (Load reference…).";
            return r;
        }
    }

    std::vector<chromacal::PatchStatistics> patches;
    try {
        patches = chromacal::detect(image, 1.0, 0.01f, 0.99f,
                                    chart == 1 ? chromacal::ChartType::SG140
                                               : chromacal::ChartType::Classic24,
                                    chart == 1 ? &ref : nullptr);
    } catch (const std::exception& e) {
        r.error = std::string("detect() failed: ") + e.what();
        return r;
    }
    if (patches.empty()) {
        r.error = "No ColorChecker detected in the frame.";
        return r;
    }

    r.patches_detected = static_cast<int>(patches.size());
    r.min_reliability = 1.0;
    for (const auto& p : patches)
        r.min_reliability = std::min(r.min_reliability, p.reliability);

    // Patch centers (normalized) for the effect's detection overlay.
    for (const auto& p : patches) {
        if (r.overlay_count >= 24 || p.center[0] < 0) continue;
        r.overlay_xy[r.overlay_count * 2] = static_cast<float>(p.center[0]);
        r.overlay_xy[r.overlay_count * 2 + 1] = static_cast<float>(p.center[1]);
        ++r.overlay_count;
    }

    // No hard normality cull: the solver down-weights unreliable patches via
    // their reliability score, which is the right behavior for a single,
    // possibly-imperfect on-set frame grab.
    chromacal::Solver solver;
    try {
        solver.solve(patches);
        if (cube_path)
            chromacal::write_cube(solver, *cube_path, lut_size, "chromacal");
    } catch (const std::exception& e) {
        r.error = std::string("solve()/write_cube() failed: ") + e.what();
        return r;
    }

    cv::Mat ccm = solver.get_ccm();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.ccm[i * 3 + j] = ccm.at<double>(i, j);

    auto luma = solver.get_luma_params();
    for (int i = 0; i < 4 && i < static_cast<int>(luma.size()); ++i)
        r.luma[i] = luma[i];

    r.final_error = solver.get_final_error();
    r.ok = true;
    return r;
}

SolveResult solve_from_image(const std::string& image_path,
                             const std::string& cube_path, int lut_size) {
    cv::Mat image = cv::imread(image_path, cv::IMREAD_COLOR);
    if (image.empty()) {
        SolveResult r;
        r.error = "Failed to load image: " + image_path;
        return r;
    }
    return run_solve(image, &cube_path, lut_size);
}

SolveResult solve_from_bgra_f32(const float* bgra, int width, int height,
                                int row_stride_floats, int chart,
                                const std::string& reference_path) {
    if (!bgra || width <= 0 || height <= 0) {
        SolveResult r;
        r.error = "Invalid frame buffer.";
        return r;
    }

    // Pack the float BGRA frame into an 8-bit BGR image for the MCC detector.
    // row_stride_floats may be negative (bottom-up frames), so use signed
    // pointer arithmetic.
    cv::Mat bgr(height, width, CV_8UC3);
    for (int y = 0; y < height; ++y) {
        const float* row = bgra + static_cast<ptrdiff_t>(y) * row_stride_floats;
        auto* dst = bgr.ptr<cv::Vec3b>(y);
        for (int x = 0; x < width; ++x) {
            const float* px = row + static_cast<ptrdiff_t>(x) * 4;
            auto to8 = [](float v) -> unsigned char {
                int i = static_cast<int>(v * 255.0f + 0.5f);
                return static_cast<unsigned char>(i < 0 ? 0 : (i > 255 ? 255 : i));
            };
            dst[x] = cv::Vec3b(to8(px[0]), to8(px[1]), to8(px[2])); // B, G, R
        }
    }
    return run_solve(bgr, nullptr, 0, chart, reference_path);
}

namespace {
// Mirror the native effect's per-channel math exactly (chromacal_effect.cpp).
double tone_curve(double c, const double* p) {
    if (!(c > 0.0)) c = 0.0;
    double l = std::log(c + 1e-6);
    double v = std::exp(p[0] + p[1] * l + p[2] * l * l + p[3] * l * l * l);
    return (v == v && v < 1e12) ? v : 0.0;
}
double encode_channel(double c, double gamma) {
    if (!(c > 0.0)) return 0.0;
    double o = (gamma > 0.0)
                   ? std::pow(c, 1.0 / gamma)
                   : (c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055);
    return (o == o) ? o : 0.0;
}
} // namespace

bool apply_calibration_to_image(const std::string& in_path, const std::string& out_path,
                                const double* luma, const double* ccm, double gamma) {
    if (!luma || !ccm) return false;
    cv::Mat bgr = cv::imread(in_path, cv::IMREAD_COLOR);
    if (bgr.empty()) return false;
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    rgb.convertTo(rgb, CV_64FC3, 1.0 / 255.0);

    for (int y = 0; y < rgb.rows; ++y) {
        auto* row = rgb.ptr<cv::Vec3d>(y);
        for (int x = 0; x < rgb.cols; ++x) {
            double aR = tone_curve(row[x][0], luma);
            double aG = tone_curve(row[x][1], luma);
            double aB = tone_curve(row[x][2], luma);
            double lR = ccm[0] * aR + ccm[1] * aG + ccm[2] * aB;
            double lG = ccm[3] * aR + ccm[4] * aG + ccm[5] * aB;
            double lB = ccm[6] * aR + ccm[7] * aG + ccm[8] * aB;
            row[x] = cv::Vec3d(encode_channel(lR, gamma), encode_channel(lG, gamma),
                               encode_channel(lB, gamma));
        }
    }
    cv::Mat out8, outbgr;
    rgb.convertTo(out8, CV_8UC3, 255.0);
    cv::cvtColor(out8, outbgr, cv::COLOR_RGB2BGR);
    return cv::imwrite(out_path, outbgr);
}

bool image_diff(const std::string& a_path, const std::string& b_path,
                double* mean_out, double* max_out) {
    cv::Mat a = cv::imread(a_path, cv::IMREAD_COLOR);
    cv::Mat b = cv::imread(b_path, cv::IMREAD_COLOR);
    if (a.empty() || b.empty() || a.size() != b.size()) return false;
    cv::Mat da, db, diff;
    a.convertTo(da, CV_64FC3, 1.0 / 255.0);
    b.convertTo(db, CV_64FC3, 1.0 / 255.0);
    cv::absdiff(da, db, diff);
    if (mean_out) {
        cv::Scalar m = cv::mean(diff);
        *mean_out = (m[0] + m[1] + m[2]) / 3.0;
    }
    if (max_out) {
        double mn, mx;
        cv::minMaxLoc(diff.reshape(1), &mn, &mx);
        *max_out = mx;
    }
    return true;
}

} // namespace chromacal_ppro
