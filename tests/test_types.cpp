#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "chromacal/types.h"

using namespace chromacal;
using Catch::Matchers::WithinAbs;

TEST_CASE("PatchStatistics default construction", "[types]") {
    PatchStatistics ps;
    CHECK(ps.exposure == 1.0);
    CHECK(ps.pixel_count == 0);
    CHECK(ps.raw_pixels.empty());
}

TEST_CASE("CalibrationResult default construction", "[types]") {
    CalibrationResult cr;
    CHECK(cr.final_error == 0.0);
    CHECK(cr.reference_exposure == 1.0);
    CHECK(cr.luma_params.empty());
}

TEST_CASE("NormalityTestResults default construction", "[types]") {
    NormalityTestResults nr;
    CHECK_FALSE(nr.overall_passes);
    CHECK_FALSE(nr.passes_mardia_skewness);
    CHECK_FALSE(nr.passes_henze_zirkler);
}
