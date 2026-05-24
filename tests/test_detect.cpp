#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "chromacal/detect.h"
#include "chromacal/solver.h"

#include <random>

using namespace chromacal;
using Catch::Matchers::WithinAbs;

// ---------------------------------------------------------------------------
// Normality testing
// ---------------------------------------------------------------------------

static std::vector<cv::Vec3d> make_gaussian_pixels(int n, cv::Vec3d mean, double stddev,
                                                    unsigned seed = 42) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> dist(0.0, stddev);
    std::vector<cv::Vec3d> pixels(n);
    for (auto& p : pixels)
        p = cv::Vec3d(mean[0] + dist(rng), mean[1] + dist(rng), mean[2] + dist(rng));
    return pixels;
}

TEST_CASE("test_normality accepts Gaussian data", "[detect]") {
    auto pixels = make_gaussian_pixels(200, {0.5, 0.4, 0.3}, 0.02);
    auto result = test_normality(pixels);
    CHECK(result.overall_passes);
}

// Regression guard: each individual test must actually accept clean Gaussian
// data. Previously Shapiro-Francia and Henze-Zirkler returned ~0 even for
// perfect Gaussians (they only "passed" overall via Mardia), which silently
// made filter_normal reject every real patch. Use n=250 with a non-zero mean
// (the mean is what broke the old Shapiro W' normalization).
TEST_CASE("individual normality tests accept Gaussian data", "[detect]") {
    auto pixels = make_gaussian_pixels(250, {0.5, 0.4, 0.3}, 0.02);
    auto result = test_normality(pixels, 0.01);

    for (double p : result.shapiro_pvalues)
        CHECK(p > 0.01); // Shapiro-Francia per channel
    CHECK(result.henze_zirkler_pvalue > 0.01);
    CHECK(result.mardia_skewness_pvalue > 0.01);
    CHECK(result.mardia_kurtosis_pvalue > 0.01);
}

// Each individual test must reject clearly non-Gaussian (uniform) data.
TEST_CASE("individual normality tests reject uniform data", "[detect]") {
    std::mt19937 rng(123);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    std::vector<cv::Vec3d> pixels(500);
    for (auto& p : pixels)
        p = cv::Vec3d(dist(rng), dist(rng), dist(rng));
    auto result = test_normality(pixels, 0.01);

    CHECK(result.henze_zirkler_pvalue < 0.01);
    int rejected_channels = 0;
    for (double p : result.shapiro_pvalues)
        if (p < 0.01) ++rejected_channels;
    CHECK(rejected_channels >= 2);
}

TEST_CASE("test_normality rejects uniform data", "[detect]") {
    // Uniform distribution should fail normality tests
    std::mt19937 rng(123);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    std::vector<cv::Vec3d> pixels(500);
    for (auto& p : pixels)
        p = cv::Vec3d(dist(rng), dist(rng), dist(rng));
    auto result = test_normality(pixels);
    CHECK_FALSE(result.overall_passes);
}

TEST_CASE("test_normality handles tiny sample", "[detect]") {
    std::vector<cv::Vec3d> pixels = {{0.5, 0.5, 0.5}, {0.6, 0.6, 0.6}};
    auto result = test_normality(pixels);
    CHECK_FALSE(result.overall_passes); // Too few samples
}

TEST_CASE("test_normality returns p-values", "[detect]") {
    auto pixels = make_gaussian_pixels(100, {0.5, 0.5, 0.5}, 0.01);
    auto result = test_normality(pixels);
    CHECK(result.shapiro_pvalues.size() == 3);
    for (double p : result.shapiro_pvalues) {
        CHECK(p >= 0.0);
        CHECK(p <= 1.0);
    }
    CHECK(result.mardia_skewness_pvalue >= 0.0);
    CHECK(result.mardia_kurtosis_pvalue >= 0.0);
    CHECK(result.henze_zirkler_pvalue >= 0.0);
}

// ---------------------------------------------------------------------------
// Reliability weighting
// ---------------------------------------------------------------------------

static void mean_cov(const std::vector<cv::Vec3d>& px, cv::Vec3d& mean, cv::Matx33d& cov) {
    mean = cv::Vec3d(0, 0, 0);
    for (const auto& p : px) mean += p;
    mean /= static_cast<double>(px.size());
    cov = cv::Matx33d::zeros();
    for (const auto& p : px) {
        cv::Vec3d d = p - mean;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                cov(i, j) += d[i] * d[j];
    }
    cov *= 1.0 / (px.size() - 1);
}

TEST_CASE("patch_reliability is ~1 for a clean Gaussian patch", "[detect]") {
    auto px = make_gaussian_pixels(250, {0.5, 0.4, 0.3}, 0.02);
    cv::Vec3d mean;
    cv::Matx33d cov;
    mean_cov(px, mean, cov);
    CHECK(patch_reliability(px, mean, cov) > 0.9);
}

TEST_CASE("patch_reliability drops when specular outliers contaminate a patch", "[detect]") {
    auto px = make_gaussian_pixels(250, {0.5, 0.4, 0.3}, 0.02);
    cv::Vec3d clean_mean;
    cv::Matx33d clean_cov;
    mean_cov(px, clean_mean, clean_cov);
    double clean = patch_reliability(px, clean_mean, clean_cov);

    // Replace ~8% of pixels with blown-out specular highlights.
    for (size_t i = 0; i < px.size() * 8 / 100; ++i)
        px[i] = cv::Vec3d(0.99, 0.99, 0.99);
    cv::Vec3d dirty_mean;
    cv::Matx33d dirty_cov;
    mean_cov(px, dirty_mean, dirty_cov);
    double dirty = patch_reliability(px, dirty_mean, dirty_cov);

    CHECK(dirty < clean);
    CHECK(dirty < 0.9); // meaningfully down-weighted
    CHECK(dirty > 0.0); // but not discarded
}

// ---------------------------------------------------------------------------
// Patch filtering
// ---------------------------------------------------------------------------

TEST_CASE("filter_normal removes failing patches", "[detect]") {
    PatchStatistics good;
    good.normality_tests.overall_passes = true;

    PatchStatistics bad;
    bad.normality_tests.overall_passes = false;

    auto filtered = filter_normal({good, bad, good, bad, bad});
    CHECK(filtered.size() == 2);
    for (const auto& p : filtered)
        CHECK(p.normality_tests.overall_passes);
}

TEST_CASE("filter_normal returns empty for all-bad input", "[detect]") {
    PatchStatistics bad;
    bad.normality_tests.overall_passes = false;
    auto filtered = filter_normal({bad, bad});
    CHECK(filtered.empty());
}

// ---------------------------------------------------------------------------
// detect() on a synthetic image
// ---------------------------------------------------------------------------

TEST_CASE("detect returns empty for image without ColorChecker", "[detect]") {
    // Solid gray image — no chart to detect
    cv::Mat gray(480, 640, CV_8UC3, cv::Scalar(128, 128, 128));
    auto patches = detect(gray);
    CHECK(patches.empty());
}

TEST_CASE("detect returns empty for tiny image", "[detect]") {
    cv::Mat tiny(2, 2, CV_8UC3, cv::Scalar(100, 100, 100));
    auto patches = detect(tiny);
    CHECK(patches.empty());
}

// End-to-end guard on the real example image shipped in the repo: the chart
// must be found and the solver must produce a sane, non-degenerate calibration.
// (filter_normal is intentionally not applied here -- this is a downscaled,
// 8-bit web image whose patches do not pass the normality gate.)
#ifdef CHROMACAL_SOURCE_DIR
TEST_CASE("end-to-end calibration on the bundled example image", "[detect]") {
    cv::Mat img = cv::imread(std::string(CHROMACAL_SOURCE_DIR) + "/docs/before.png");
    REQUIRE(!img.empty());

    auto patches = detect(img);
    REQUIRE(patches.size() >= 18); // MCC detector locates (most of) the chart

    Solver solver;
    solver.solve(patches);

    cv::Mat ccm = solver.get_ccm();
    REQUIRE(ccm.rows == 3);
    REQUIRE(ccm.cols == 3);
    double det = cv::determinant(ccm);
    CHECK(std::isfinite(det));
    CHECK(det > 0.1); // non-degenerate color correction
    CHECK(std::isfinite(solver.get_final_error()));
}
#endif
