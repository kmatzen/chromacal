#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "solve_core.h"

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

TEST_CASE("write_calibration / read_calibration round-trip", "[solve_core]") {
    auto path = temp_path("chromacal_calib_test.txt");
    REQUIRE(write_calibration(kLuma.data(), kCcm.data(), path));

    double luma[4] = {};
    double ccm[9] = {};
    REQUIRE(read_calibration(path, luma, ccm));
    for (int i = 0; i < 4; ++i) CHECK_THAT(luma[i], WithinAbs(kLuma[i], 1e-12));
    for (int i = 0; i < 9; ++i) CHECK_THAT(ccm[i], WithinAbs(kCcm[i], 1e-12));

    std::filesystem::remove(path);
}

TEST_CASE("write_calibration rejects null pointers", "[solve_core]") {
    auto path = temp_path("chromacal_calib_null.txt");
    CHECK_FALSE(write_calibration(nullptr, kCcm.data(), path));
    CHECK_FALSE(write_calibration(kLuma.data(), nullptr, path));
    CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("read_calibration rejects a missing file", "[solve_core]") {
    double luma[4] = {};
    double ccm[9] = {};
    CHECK_FALSE(read_calibration(temp_path("chromacal_calib_does_not_exist.txt"), luma, ccm));
}

TEST_CASE("read_calibration rejects a malformed file", "[solve_core]") {
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

TEST_CASE("read_calibration rejects a file missing the ccm section", "[solve_core]") {
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

TEST_CASE("read_reference_lab parses a well-formed reference file", "[solve_core]") {
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

TEST_CASE("read_reference_lab returns empty for a missing file", "[solve_core]") {
    auto refs = read_reference_lab(temp_path("chromacal_ref_does_not_exist.txt"));
    CHECK(refs.empty());
}

TEST_CASE("write_display_cube writes a well-formed .cube file", "[solve_core]") {
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

TEST_CASE("write_effect_cube writes a well-formed .cube file", "[solve_core]") {
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
    std::filesystem::remove(path);
}

TEST_CASE("write_display_cube and write_effect_cube reject degenerate inputs", "[solve_core]") {
    auto path = temp_path("chromacal_bad.cube");
    CHECK_FALSE(write_display_cube(kLuma.data(), kCcm.data(), path, 1, 2.4));
    CHECK_FALSE(write_display_cube(nullptr, kCcm.data(), path, 9, 2.4));
    CHECK_FALSE(write_display_cube(kLuma.data(), nullptr, path, 9, 2.4));
    CHECK_FALSE(write_effect_cube(kLuma.data(), kCcm.data(), path, 1, 2.4));
    CHECK_FALSE(write_effect_cube(nullptr, kCcm.data(), path, 9, 2.4));
    CHECK_FALSE(write_effect_cube(kLuma.data(), nullptr, path, 9, 2.4));
}

TEST_CASE("write_display_cube creates missing parent directories", "[solve_core]") {
    auto dir = std::filesystem::temp_directory_path() / "chromacal_nested_test_dir";
    std::filesystem::remove_all(dir);
    auto path = (dir / "sub" / "out.cube").string();

    REQUIRE(write_display_cube(kLuma.data(), kCcm.data(), path, 3, 2.4));
    CHECK(std::filesystem::exists(path));

    std::filesystem::remove_all(dir);
}

TEST_CASE("solve_from_bgra_f32 rejects an invalid frame buffer", "[solve_core]") {
    auto r1 = solve_from_bgra_f32(nullptr, 4, 4, 16);
    CHECK_FALSE(r1.ok);
    CHECK_FALSE(r1.error.empty());

    float px[4] = {0.1f, 0.2f, 0.3f, 1.0f};
    auto r2 = solve_from_bgra_f32(px, 0, 4, 16);
    CHECK_FALSE(r2.ok);

    auto r3 = solve_from_bgra_f32(px, 4, 0, 16);
    CHECK_FALSE(r3.ok);
}

TEST_CASE("solve_from_image rejects an unreadable path", "[solve_core]") {
    auto result = solve_from_image(temp_path("chromacal_no_such_image.png"),
                                   temp_path("chromacal_no_such_image_out.cube"), 9);
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("apply_calibration_to_image rejects an unreadable input path", "[solve_core]") {
    CHECK_FALSE(apply_calibration_to_image(temp_path("chromacal_no_such_image.png"),
                                           temp_path("chromacal_no_such_image_out.png"),
                                           kLuma.data(), kCcm.data(), 2.4));
}

TEST_CASE("apply_calibration_to_image round-trips the bundled sample frame", "[solve_core]") {
    std::string in_path = std::string(CHROMACAL_SOURCE_DIR) + "/docs/before.png";
    if (!std::filesystem::exists(in_path)) return; // sample not present in this checkout

    auto out_path = temp_path("chromacal_applied.png");
    REQUIRE(apply_calibration_to_image(in_path, out_path, kLuma.data(), kCcm.data(), 2.4));
    CHECK(std::filesystem::exists(out_path));

    double mean = -1.0, max = -1.0;
    REQUIRE(image_diff(in_path, out_path, &mean, &max));
    CHECK(mean >= 0.0);
    CHECK(max >= mean);

    std::filesystem::remove(out_path);
}

TEST_CASE("image_diff reports zero difference between identical images", "[solve_core]") {
    std::string in_path = std::string(CHROMACAL_SOURCE_DIR) + "/docs/before.png";
    if (!std::filesystem::exists(in_path)) return;

    double mean = -1.0, max = -1.0;
    REQUIRE(image_diff(in_path, in_path, &mean, &max));
    CHECK_THAT(mean, WithinAbs(0.0, 1e-9));
    CHECK_THAT(max, WithinAbs(0.0, 1e-9));
}

TEST_CASE("image_diff rejects missing or mismatched-size images", "[solve_core]") {
    std::string a_path = std::string(CHROMACAL_SOURCE_DIR) + "/docs/before.png";
    if (!std::filesystem::exists(a_path)) return;

    double mean = 0.0, max = 0.0;
    CHECK_FALSE(image_diff(a_path, temp_path("chromacal_no_such_image.png"), &mean, &max));
    CHECK_FALSE(image_diff(temp_path("chromacal_no_such_image.png"), a_path, &mean, &max));
}
