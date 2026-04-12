#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "chromacal/detect.h"

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
