// Tests for solve_format.{h,cpp} — deliberately dependency-free (no OpenCV,
// Ceres, or OpenColorIO): this file + solve_format.cpp build and run with
// just a C++17 compiler, so they're the fast path for verifying the
// calibration-preset / .cube-format logic without the full native stack.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "solve_format.h"

#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace chromacal_ppro;
using Catch::Matchers::WithinAbs;

namespace {

std::string temp_path(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

// Deterministic, non-identity calibration used across the file/format tests
// below — values don't need to be physically meaningful, just fixed and
// distinct so round-trips and format checks are exact.
const std::array<double, 4> kLuma = {0.05, 1.02, -0.01, 0.003};
const std::array<double, 9> kCcm = {1.05, -0.02, -0.01, 0.0, 1.03, -0.02, 0.01, -0.03, 1.04};

} // namespace

TEST_CASE("write_calibration / read_calibration round-trip", "[solve_format]") {
    auto path = temp_path("chromacal_calib_test.txt");
    REQUIRE(write_calibration(kLuma.data(), kCcm.data(), path));

    double luma[4] = {};
    double ccm[9] = {};
    REQUIRE(read_calibration(path, luma, ccm));
    for (int i = 0; i < 4; ++i) CHECK_THAT(luma[i], WithinAbs(kLuma[i], 1e-12));
    for (int i = 0; i < 9; ++i) CHECK_THAT(ccm[i], WithinAbs(kCcm[i], 1e-12));

    std::filesystem::remove(path);
}

TEST_CASE("write_calibration rejects null pointers", "[solve_format]") {
    auto path = temp_path("chromacal_calib_null.txt");
    CHECK_FALSE(write_calibration(nullptr, kCcm.data(), path));
    CHECK_FALSE(write_calibration(kLuma.data(), nullptr, path));
    CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("read_calibration rejects a missing file", "[solve_format]") {
    double luma[4] = {};
    double ccm[9] = {};
    CHECK_FALSE(read_calibration(temp_path("chromacal_calib_does_not_exist.txt"), luma, ccm));
}

TEST_CASE("read_calibration rejects a malformed file", "[solve_format]") {
    auto path = temp_path("chromacal_calib_bad.txt");
    {
        std::ofstream out(path);
        out << "not-a-chromacal-calibration 1\nluma 1 2 3 4\nccm 1 2 3 4 5 6 7 8 9\n";
    }
    double luma[4] = {};
    double ccm[9] = {};
    CHECK_FALSE(read_calibration(path, luma, ccm));
    std::filesystem::remove(path);
}

TEST_CASE("read_calibration rejects a file missing the ccm section", "[solve_format]") {
    auto path = temp_path("chromacal_calib_incomplete.txt");
    {
        std::ofstream out(path);
        out << "chromacal-calibration 1\nluma 1 2 3 4\n";
    }
    double luma[4] = {};
    double ccm[9] = {};
    CHECK_FALSE(read_calibration(path, luma, ccm));
    std::filesystem::remove(path);
}

TEST_CASE("read_reference_lab parses a well-formed reference file", "[solve_format]") {
    auto path = temp_path("chromacal_ref_ok.txt");
    {
        std::ofstream out(path);
        out << "Patch\tL\ta\tb\n"; // header row, should be skipped
        out << "A1\t37.986\t13.555\t14.059\n";
        out << "\n"; // blank line, should be skipped
        out << "A2\t65.711\t18.13\t17.81\n";
    }
    auto refs = read_reference_lab(path);
    REQUIRE(refs.size() == 2);
    CHECK_THAT(refs[0][0], WithinAbs(37.986, 1e-9));
    CHECK_THAT(refs[0][1], WithinAbs(13.555, 1e-9));
    CHECK_THAT(refs[0][2], WithinAbs(14.059, 1e-9));
    CHECK_THAT(refs[1][0], WithinAbs(65.711, 1e-9));
    std::filesystem::remove(path);
}

TEST_CASE("read_reference_lab returns empty for a missing file", "[solve_format]") {
    auto refs = read_reference_lab(temp_path("chromacal_ref_does_not_exist.txt"));
    CHECK(refs.empty());
}

TEST_CASE("write_display_cube writes a well-formed .cube file", "[solve_format]") {
    auto path = temp_path("chromacal_display.cube");
    const int N = 5;
    REQUIRE(write_display_cube(kLuma.data(), kCcm.data(), path, N, 2.4));

    std::ifstream in(path);
    REQUIRE(in.is_open());
    bool saw_size = false;
    int data_lines = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("LUT_3D_SIZE", 0) == 0) {
            saw_size = true;
            CHECK(line == "LUT_3D_SIZE 5");
        } else if (!line.empty() &&
                   (std::isdigit(static_cast<unsigned char>(line[0])) || line[0] == '-')) {
            ++data_lines;
            std::istringstream ss(line);
            double r, g, b;
            CHECK(static_cast<bool>(ss >> r >> g >> b));
            // write_display_cube's cube_encode clamps to [0, 1].
            CHECK(r >= 0.0); CHECK(r <= 1.0);
            CHECK(g >= 0.0); CHECK(g <= 1.0);
            CHECK(b >= 0.0); CHECK(b <= 1.0);
        }
    }
    CHECK(saw_size);
    CHECK(data_lines == N * N * N);

    in.close();
    std::filesystem::remove(path);
}

TEST_CASE("write_effect_cube writes a well-formed .cube file", "[solve_format]") {
    auto path = temp_path("chromacal_effect.cube");
    const int N = 5;
    REQUIRE(write_effect_cube(kLuma.data(), kCcm.data(), path, N, 2.4));

    std::ifstream in(path);
    REQUIRE(in.is_open());
    bool saw_size = false;
    int data_lines = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("LUT_3D_SIZE", 0) == 0) {
            saw_size = true;
        } else if (!line.empty() &&
                   (std::isdigit(static_cast<unsigned char>(line[0])) || line[0] == '-')) {
            ++data_lines;
        }
    }
    CHECK(saw_size);
    CHECK(data_lines == N * N * N);
    in.close();  // Windows won't remove a file with an open handle.
    std::filesystem::remove(path);
}

TEST_CASE("write_display_cube and write_effect_cube reject degenerate inputs", "[solve_format]") {
    auto path = temp_path("chromacal_bad.cube");
    CHECK_FALSE(write_display_cube(kLuma.data(), kCcm.data(), path, 1, 2.4));
    CHECK_FALSE(write_display_cube(nullptr, kCcm.data(), path, 9, 2.4));
    CHECK_FALSE(write_display_cube(kLuma.data(), nullptr, path, 9, 2.4));
    CHECK_FALSE(write_effect_cube(kLuma.data(), kCcm.data(), path, 1, 2.4));
    CHECK_FALSE(write_effect_cube(nullptr, kCcm.data(), path, 9, 2.4));
    CHECK_FALSE(write_effect_cube(kLuma.data(), nullptr, path, 9, 2.4));
}

TEST_CASE("write_display_cube creates missing parent directories", "[solve_format]") {
    auto dir = std::filesystem::temp_directory_path() / "chromacal_nested_test_dir";
    std::filesystem::remove_all(dir);
    auto path = (dir / "sub" / "out.cube").string();

    REQUIRE(write_display_cube(kLuma.data(), kCcm.data(), path, 3, 2.4));
    CHECK(std::filesystem::exists(path));

    std::filesystem::remove_all(dir);
}
