// Tests for solve_core.{h,cpp} — the OpenCV/chromacal-dependent detect+solve
// entry points. See test_solve_format.cpp for the dependency-free calibration
// preset / .cube-format helpers, tested separately so they don't need OpenCV.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "solve_core.h"

#include <array>
#include <filesystem>
#include <string>

using namespace chromacal_ppro;
using Catch::Matchers::WithinAbs;

namespace {

std::string temp_path(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

const std::array<double, 4> kLuma = {0.05, 1.02, -0.01, 0.003};
const std::array<double, 9> kCcm = {1.05, -0.02, -0.01, 0.0, 1.03, -0.02, 0.01, -0.03, 1.04};

} // namespace

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
