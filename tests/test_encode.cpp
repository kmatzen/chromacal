// Unit tests for the transfer functions (chromacal_encode.h). The HDR OETFs can't
// be checked visually on an SDR machine, but their math is verifiable against the
// SMPTE ST.2084 (PQ) / ARIB STD-B67 (HLG) / IEC sRGB references.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "chromacal_encode.h"

using Catch::Matchers::WithinAbs;
using namespace chromacal_enc;

TEST_CASE("sRGB OETF", "[encode]") {
    CHECK(SrgbEncode(0.0) == 0.0);
    CHECK_THAT(SrgbEncode(1.0), WithinAbs(1.0, 1e-9));
    CHECK_THAT(SrgbEncode(0.0031308), WithinAbs(0.04045, 1e-4)); // toe boundary
    CHECK_THAT(SrgbEncode(0.5), WithinAbs(0.735357, 1e-4));
    CHECK(SrgbEncode(-0.1) == 0.0); // guarded
}

TEST_CASE("PQ OETF (SMPTE ST.2084)", "[encode]") {
    CHECK(PqEncode(0.0, 100.0) == 0.0);
    CHECK_THAT(PqEncode(1.0, 10000.0), WithinAbs(1.0, 1e-6));   // Y = 1.0 (10k nits)
    CHECK_THAT(PqEncode(1.0, 100.0), WithinAbs(0.508078, 1e-4)); // 100 nits -> Y=0.01
    CHECK_THAT(PqEncode(1.0, 1000.0), WithinAbs(0.751827, 1e-4)); // 1000 nits -> Y=0.1
    CHECK(PqEncode(2.0, 10000.0) <= 1.0); // clamps above peak
}

TEST_CASE("HLG OETF (ARIB STD-B67)", "[encode]") {
    CHECK(HlgEncode(0.0) == 0.0);
    CHECK_THAT(HlgEncode(1.0 / 12.0), WithinAbs(0.5, 1e-6)); // sqrt(3*1/12)=0.5
    CHECK_THAT(HlgEncode(1.0), WithinAbs(1.0, 1e-6));        // a·ln(12-b)+c = 1
    CHECK_THAT(HlgEncode(0.25), WithinAbs(0.738586, 1e-4));
}

TEST_CASE("Rec.709 -> Rec.2020 primaries", "[encode]") {
    // White (D65) is preserved — each row sums to 1.
    double r = 1, g = 1, b = 1;
    ToRec2020(r, g, b);
    CHECK_THAT(r, WithinAbs(1.0, 1e-6));
    CHECK_THAT(g, WithinAbs(1.0, 1e-6));
    CHECK_THAT(b, WithinAbs(1.0, 1e-6));
    // Pure Rec.709 red maps mostly to Rec.2020 red, slightly desaturated.
    r = 1; g = 0; b = 0;
    ToRec2020(r, g, b);
    CHECK_THAT(r, WithinAbs(0.6274, 1e-4));
    CHECK_THAT(g, WithinAbs(0.0691, 1e-4));
    CHECK_THAT(b, WithinAbs(0.0164, 1e-4));
    CHECK(r > g); CHECK(r > b); // still red-dominant
}
