#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "chromacal/apply.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace chromacal;

// Synthetic patches (same as test_solver.cpp)
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

static std::vector<PatchStatistics> make_patches() {
    std::vector<PatchStatistics> patches;
    for (size_t i = 0; i < kRefLab.size(); ++i) {
        PatchStatistics ps;
        double L = kRefLab[i][0] / 100.0;
        ps.mean = cv::Vec3d(std::clamp(L + 0.05, 0.01, 0.99),
                            std::clamp(L, 0.01, 0.99),
                            std::clamp(L - 0.05, 0.01, 0.99));
        ps.covariance = cv::Matx33d(1e-4, 0, 0, 0, 1e-4, 0, 0, 0, 1e-4);
        ps.reference_lab = kRefLab[i];
        ps.exposure = 1.0;
        ps.pixel_count = 100;
        patches.push_back(ps);
    }
    return patches;
}

static Solver make_fitted_solver() {
    Solver solver;
    solver.solve(make_patches());
    return solver;
}

TEST_CASE("create_lut returns valid processor", "[apply]") {
    auto solver = make_fitted_solver();
    // Use small LUT for speed in tests
    auto lut = create_lut(solver, 17);
    REQUIRE(lut != nullptr);
}

TEST_CASE("apply_lut handles CV_8UC3 input", "[apply]") {
    auto solver = make_fitted_solver();
    auto lut = create_lut(solver, 17);

    cv::Mat input(4, 4, CV_8UC3, cv::Scalar(128, 100, 80));
    cv::Mat output = apply_lut(input, lut);

    CHECK(output.rows == 4);
    CHECK(output.cols == 4);
    CHECK(output.type() == CV_32FC3);
}

TEST_CASE("apply_lut handles CV_32FC3 input", "[apply]") {
    auto solver = make_fitted_solver();
    auto lut = create_lut(solver, 17);

    cv::Mat input(4, 4, CV_32FC3, cv::Scalar(0.5f, 0.4f, 0.3f));
    cv::Mat output = apply_lut(input, lut);

    CHECK(output.rows == 4);
    CHECK(output.cols == 4);
    CHECK(output.type() == CV_32FC3);
}

TEST_CASE("apply_lut handles CV_64FC3 input", "[apply]") {
    auto solver = make_fitted_solver();
    auto lut = create_lut(solver, 17);

    cv::Mat input(4, 4, CV_64FC3, cv::Scalar(0.5, 0.4, 0.3));
    cv::Mat output = apply_lut(input, lut);

    CHECK(output.type() == CV_32FC3);
}

TEST_CASE("apply_lut rejects single-channel input", "[apply]") {
    auto solver = make_fitted_solver();
    auto lut = create_lut(solver, 17);

    cv::Mat gray(4, 4, CV_8UC1, cv::Scalar(128));
    CHECK_THROWS(apply_lut(gray, lut));
}

TEST_CASE("apply_lut produces different output from input", "[apply]") {
    auto solver = make_fitted_solver();
    auto lut = create_lut(solver, 17);

    cv::Mat input(4, 4, CV_32FC3, cv::Scalar(0.5f, 0.4f, 0.3f));
    cv::Mat output = apply_lut(input, lut);

    // The calibration should change the values
    auto in_px = input.at<cv::Vec3f>(0, 0);
    auto out_px = output.at<cv::Vec3f>(0, 0);
    bool changed = (std::abs(in_px[0] - out_px[0]) > 1e-6 ||
                    std::abs(in_px[1] - out_px[1]) > 1e-6 ||
                    std::abs(in_px[2] - out_px[2]) > 1e-6);
    CHECK(changed);
}

TEST_CASE("LUT size affects precision", "[apply]") {
    auto solver = make_fitted_solver();

    // Small vs larger LUT
    auto lut_small = create_lut(solver, 9);
    auto lut_large = create_lut(solver, 33);

    cv::Mat input(1, 1, CV_32FC3, cv::Scalar(0.5f, 0.4f, 0.3f));
    cv::Mat out_small = apply_lut(input, lut_small);
    cv::Mat out_large = apply_lut(input, lut_large);

    // Both should produce roughly similar results for mid-range values
    auto ps = out_small.at<cv::Vec3f>(0, 0);
    auto pl = out_large.at<cv::Vec3f>(0, 0);
    for (int c = 0; c < 3; ++c) {
        CHECK(std::abs(ps[c] - pl[c]) < 0.05);
    }
}

TEST_CASE("write_cube writes a valid .cube file", "[apply]") {
    auto solver = make_fitted_solver();
    const int N = 9;
    auto path = (std::filesystem::temp_directory_path() / "chromacal_test.cube").string();

    write_cube(solver, path, N, "test");

    std::ifstream in(path);
    REQUIRE(in.is_open());

    bool saw_size = false;
    bool saw_domain_max = false;
    int data_lines = 0;
    bool all_three_floats = true;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("LUT_3D_SIZE", 0) == 0) {
            saw_size = true;
            CHECK(line == "LUT_3D_SIZE 9");
        } else if (line.rfind("DOMAIN_MAX", 0) == 0) {
            saw_domain_max = true;
        } else if (!line.empty() &&
                   (std::isdigit(static_cast<unsigned char>(line[0])) || line[0] == '-')) {
            ++data_lines;
            // Each data row must be exactly three parseable floats.
            std::istringstream ss(line);
            double r, g, b;
            if (!(ss >> r >> g >> b)) all_three_floats = false;
        }
    }

    CHECK(saw_size);
    CHECK(saw_domain_max);
    CHECK(all_three_floats);
    CHECK(data_lines == N * N * N); // red x green x blue grid points

    std::filesystem::remove(path);
}

TEST_CASE("write_cube rejects degenerate grid sizes", "[apply]") {
    auto solver = make_fitted_solver();
    auto path = (std::filesystem::temp_directory_path() / "chromacal_bad.cube").string();
    CHECK_THROWS(write_cube(solver, path, 1, "bad"));
}
