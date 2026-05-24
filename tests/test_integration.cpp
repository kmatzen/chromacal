// Integration / fixed-point tests for the full calibration pipeline.
//
// The pipeline is detect -> solve -> apply. A correct, self-consistent pipeline
// has a *fixed point*: if you calibrate from a chart frame and apply the result,
// the corrected frame's patches already match the reference — so re-calibrating
// it yields a near-identity calibration, and re-applying that is a no-op. This
// catches the whole class of bugs we hit by hand (patch-sampling bias, encoding
// mismatches, resolution issues): any of them break idempotency here.
//
// `apply_effect` mirrors the native effect's transform exactly — solver.infer()
// is the effect's log-poly tone curve + 3x3 CCM (-> linear), and we add the
// effect's working-gamma encode (pow(c, 1/gamma)) — so this test validates the
// shipped effect's math, not a parallel reimplementation.

#include <catch2/catch_test_macros.hpp>

#include "chromacal/detect.h"
#include "chromacal/solver.h"

#include <opencv2/opencv.hpp>

#include <cmath>
#include <string>

using namespace chromacal;

namespace {

// Effect's ENC_GAMMA encode, guarded like EncodeChannel in the effect.
double enc(double c, double gamma) {
    if (!(c > 0.0)) return 0.0;
    double o = std::pow(c, 1.0 / gamma);
    return (o == o) ? o : 0.0;
}

// Apply the effect's transform to a gamma-encoded RGB f64 image in [0,1]:
// encode( CCM * toneCurve(input) ).
cv::Mat apply_effect(const Solver& s, const cv::Mat& rgb01, double gamma) {
    cv::Mat lin = s.infer(rgb01); // CCM * tone -> linear (the effect's tone+CCM)
    cv::Mat out(rgb01.size(), CV_64FC3);
    for (int y = 0; y < lin.rows; ++y)
        for (int x = 0; x < lin.cols; ++x) {
            cv::Vec3d v = lin.at<cv::Vec3d>(y, x);
            out.at<cv::Vec3d>(y, x) =
                cv::Vec3d(enc(v[0], gamma), enc(v[1], gamma), enc(v[2], gamma));
        }
    return out;
}

cv::Mat toRgb01(const cv::Mat& bgr8) {
    cv::Mat rgb;
    cv::cvtColor(bgr8, rgb, cv::COLOR_BGR2RGB);
    rgb.convertTo(rgb, CV_64FC3, 1.0 / 255.0);
    return rgb;
}

cv::Mat toBgr8(const cv::Mat& rgb01) {
    cv::Mat tmp, bgr;
    rgb01.convertTo(tmp, CV_8UC3, 255.0);
    cv::cvtColor(tmp, bgr, cv::COLOR_RGB2BGR);
    return bgr;
}

} // namespace

const std::string kBefore = std::string(CHROMACAL_SOURCE_DIR) + "/docs/before.png";

TEST_CASE("pipeline reaches a fixed point (before -> after -> after')", "[integration]") {
    const double kGamma = 2.4;

    cv::Mat before = cv::imread(kBefore);
    REQUIRE(!before.empty());

    // 1) Calibrate from the original frame.
    auto p1 = detect(before);
    REQUIRE(p1.size() >= 18);
    Solver s1;
    s1.solve(p1);

    // 2) Apply -> the corrected ("after") frame.
    cv::Mat after = apply_effect(s1, toRgb01(before), kGamma);
    cv::Mat after8 = toBgr8(after);

    // 3) Re-detect + re-calibrate the corrected frame.
    auto p2 = detect(after8);
    REQUIRE(p2.size() >= 18);
    Solver s2;
    s2.solve(p2);

    // 4) Apply the second calibration. On a self-consistent pipeline this is a
    //    near no-op, because `after` already matches the reference.
    cv::Mat after2 = apply_effect(s2, after, kGamma);

    // Idempotency at the patch centers — compare `after` vs `after2` at the SAME
    // pixel coordinates (order-free; no re-detection matching needed).
    double maxc = 0.0, meanc = 0.0;
    int np = 0;
    for (const auto& p : p2) {
        int px = static_cast<int>(p.center[0] * after.cols);
        int py = static_cast<int>(p.center[1] * after.rows);
        if (px < 0 || py < 0 || px >= after.cols || py >= after.rows) continue;
        cv::Vec3d a = after.at<cv::Vec3d>(py, px);
        cv::Vec3d b = after2.at<cv::Vec3d>(py, px);
        for (int c = 0; c < 3; ++c) {
            double e = std::abs(a[c] - b[c]);
            maxc = std::max(maxc, e);
            meanc += e;
        }
        ++np;
    }
    if (np) meanc /= (3.0 * np);
    // Aggregate idempotency: re-applying the re-fit calibration barely moves the
    // calibrated patches. A single saturated/out-of-gamut patch can drift more (a
    // 3x3 CCM can't reach every color), so assert the mean and surface the max.
    INFO("patch-center idempotency drift: mean=" << meanc << " max=" << maxc);
    CHECK(meanc < 0.03);
    CHECK(maxc < 0.35); // catches gross non-idempotency; tolerates one OOG patch

    // Neutral idempotency: re-applying the 2nd calibration to a gray ramp is a
    // no-op (white balance / tone reached a fixed point).
    cv::Mat ramp(1, 256, CV_64FC3);
    for (int i = 0; i < 256; ++i) {
        double v = i / 255.0;
        ramp.at<cv::Vec3d>(0, i) = cv::Vec3d(v, v, v);
    }
    cv::Mat rout = apply_effect(s2, ramp, kGamma);
    double rmax = 0.0;
    for (int i = 0; i < 256; ++i) {
        cv::Vec3d a = ramp.at<cv::Vec3d>(0, i), b = rout.at<cv::Vec3d>(0, i);
        for (int c = 0; c < 3; ++c) rmax = std::max(rmax, std::abs(a[c] - b[c]));
    }
    INFO("gray-ramp idempotency max drift=" << rmax);
    CHECK(rmax < 0.03);
}

TEST_CASE("calibration drives detected patches toward the reference", "[integration]") {
    cv::Mat before = cv::imread(kBefore);
    REQUIRE(!before.empty());

    auto p0 = detect(before);
    REQUIRE(p0.size() >= 18);
    Solver s;
    s.solve(p0);

    cv::Mat after8 = toBgr8(apply_effect(s, toRgb01(before), 2.4));
    auto p1 = detect(after8);
    REQUIRE(p1.size() >= 18);

    // Re-solving the corrected frame should leave little residual work: the
    // second solve's final error should be far below the first.
    Solver s2;
    s2.solve(p1);
    INFO("final_error before=" << s.get_final_error() << " after=" << s2.get_final_error());
    CHECK(s2.get_final_error() < s.get_final_error());
}
