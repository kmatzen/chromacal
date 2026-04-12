#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "chromacal/solver.h"

#include <cstdio>
#include <filesystem>
#include <random>

using namespace chromacal;
using Catch::Matchers::WithinAbs;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// ColorChecker D50 reference Lab values (same as in detect.cpp)
static const std::vector<cv::Vec3d> kRefLab = {
    {37.986, 13.555, 14.059},  {65.711, 18.13, 17.81},    {49.927, -4.88, -21.925},
    {43.139, -13.095, 21.905}, {55.112, 8.844, -25.399},  {70.719, -33.397, -0.199},
    {62.661, 36.067, 57.096},  {40.02, 10.41, -45.964},   {51.124, 48.239, 16.248},
    {30.325, 22.976, -21.587}, {72.532, -23.709, 57.255}, {71.941, 19.363, 67.857},
    {28.778, 14.179, -50.297}, {55.261, -38.342, 31.37},  {42.101, 53.378, 28.19},
    {81.733, 4.039, 79.819},   {51.935, 49.986, -14.574}, {51.038, -28.631, -28.638},
    {96.539, -0.425, 1.186},   {81.257, -0.638, -0.335},  {66.766, -0.734, -0.504},
    {50.867, -0.153, -0.27},   {35.656, -0.421, -1.231},  {20.461, -0.079, -0.973},
};

// Create synthetic patches with identity transform + small noise
static std::vector<PatchStatistics> make_synthetic_patches(double exposure = 1.0) {
    std::mt19937 rng(42);
    std::normal_distribution<double> noise(0.0, 0.001);

    std::vector<PatchStatistics> patches;
    for (size_t i = 0; i < kRefLab.size(); ++i) {
        PatchStatistics ps;
        // Simulate gamma-encoded RGB in [0, 1] range (approximate)
        double L = kRefLab[i][0] / 100.0;
        ps.mean = cv::Vec3d(
            std::clamp(L + 0.05 * kRefLab[i][1] / 128.0 + noise(rng), 0.01, 0.99),
            std::clamp(L + noise(rng), 0.01, 0.99),
            std::clamp(L - 0.05 * kRefLab[i][2] / 128.0 + noise(rng), 0.01, 0.99));
        ps.covariance = cv::Matx33d(1e-4, 0, 0, 0, 1e-4, 0, 0, 0, 1e-4);
        ps.reference_lab = kRefLab[i];
        ps.exposure = exposure;
        ps.pixel_count = 100;
        patches.push_back(ps);
    }
    return patches;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("Solver throws on empty patches", "[solver]") {
    Solver solver;
    CHECK_THROWS_AS(solver.solve({}), std::invalid_argument);
}

TEST_CASE("Solver throws when no calibration loaded", "[solver]") {
    Solver solver;
    CHECK_THROWS(solver.get_ccm());
    CHECK_THROWS(solver.get_luma_params());
    CHECK_THROWS(solver.get_reference_exposure());
    CHECK_THROWS(solver.get_final_error());

    cv::Mat img(1, 1, CV_64FC3, cv::Scalar(0.5, 0.5, 0.5));
    CHECK_THROWS(solver.infer(img));
}

TEST_CASE("Solver fits synthetic data", "[solver]") {
    auto patches = make_synthetic_patches();

    Solver solver;
    solver.solve(patches);

    // Should have a valid result
    auto ccm = solver.get_ccm();
    CHECK(ccm.rows == 3);
    CHECK(ccm.cols == 3);
    CHECK(ccm.type() == CV_64F);

    auto luma = solver.get_luma_params();
    CHECK(luma.size() == 4);

    CHECK(solver.get_reference_exposure() > 0.0);
    CHECK(solver.get_final_error() >= 0.0);
}

TEST_CASE("Solver CCM diagonal elements are positive", "[solver]") {
    auto patches = make_synthetic_patches();
    Solver solver;
    solver.solve(patches);

    auto ccm = solver.get_ccm();
    CHECK(ccm.at<double>(0, 0) > 0.0);
    CHECK(ccm.at<double>(1, 1) > 0.0);
    CHECK(ccm.at<double>(2, 2) > 0.0);
}

TEST_CASE("Solver infer accepts CV_64FC3", "[solver]") {
    auto patches = make_synthetic_patches();
    Solver solver;
    solver.solve(patches);

    cv::Mat input(10, 10, CV_64FC3, cv::Scalar(0.5, 0.4, 0.3));
    cv::Mat output = solver.infer(input);
    CHECK(output.rows == 10);
    CHECK(output.cols == 10);
    CHECK(output.type() == CV_64FC3);
}

TEST_CASE("Solver infer rejects wrong type", "[solver]") {
    auto patches = make_synthetic_patches();
    Solver solver;
    solver.solve(patches);

    cv::Mat input(10, 10, CV_8UC3, cv::Scalar(128, 128, 128));
    CHECK_THROWS_AS(solver.infer(input), std::invalid_argument);
}

TEST_CASE("Solver infer rejects empty image", "[solver]") {
    auto patches = make_synthetic_patches();
    Solver solver;
    solver.solve(patches);
    CHECK_THROWS_AS(solver.infer(cv::Mat()), std::invalid_argument);
}

TEST_CASE("Solver save and load round-trip", "[solver]") {
    auto patches = make_synthetic_patches();
    Solver solver;
    solver.solve(patches);

    auto tmp = std::filesystem::temp_directory_path() / "chromacal_test.yml";
    solver.save(tmp.string());

    Solver loaded;
    loaded.load(tmp.string());

    // Compare CCM
    auto ccm1 = solver.get_ccm();
    auto ccm2 = loaded.get_ccm();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            CHECK_THAT(ccm1.at<double>(i, j), WithinAbs(ccm2.at<double>(i, j), 1e-10));

    // Compare luma params
    auto lp1 = solver.get_luma_params();
    auto lp2 = loaded.get_luma_params();
    REQUIRE(lp1.size() == lp2.size());
    for (size_t i = 0; i < lp1.size(); ++i)
        CHECK_THAT(lp1[i], WithinAbs(lp2[i], 1e-10));

    CHECK_THAT(solver.get_reference_exposure(),
               WithinAbs(loaded.get_reference_exposure(), 1e-10));

    std::filesystem::remove(tmp);
}

TEST_CASE("Solver load rejects missing file", "[solver]") {
    Solver solver;
    CHECK_THROWS(solver.load("/nonexistent/path.yml"));
}

TEST_CASE("Solver handles varying exposures", "[solver]") {
    // Patches at different exposure levels
    auto patches1 = make_synthetic_patches(1.0);
    auto patches2 = make_synthetic_patches(2.0);

    std::vector<PatchStatistics> combined;
    combined.insert(combined.end(), patches1.begin(), patches1.end());
    combined.insert(combined.end(), patches2.begin(), patches2.end());

    Solver solver;
    solver.solve(combined);

    // Should converge to roughly the mean exposure
    CHECK_THAT(solver.get_reference_exposure(), WithinAbs(1.5, 0.01));
}
