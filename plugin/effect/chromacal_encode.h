// Pure transfer-function math shared by the effect and its tests. No Adobe/OpenCV
// deps, so it's unit-testable headlessly (the HDR OETFs can't be checked on an SDR
// machine visually, but their *math* can be verified against SMPTE/ARIB references).
#ifndef CHROMACAL_ENCODE_H
#define CHROMACAL_ENCODE_H

#include <cmath>

namespace chromacal_enc {

// sRGB OETF (linear -> sRGB-encoded), IEC 61966-2-1.
inline double SrgbEncode(double c) {
    if (c <= 0.0) return 0.0;
    return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

// SMPTE ST.2084 (PQ) OETF. `c` is scene-linear with 1.0 == graphics white;
// `white` is that white's luminance in nits (PQ is absolute to 10000 nits).
inline double PqEncode(double c, double white) {
    if (c <= 0.0) return 0.0;
    double Y = c * white / 10000.0;
    if (Y > 1.0) Y = 1.0;
    const double m1 = 0.1593017578125, m2 = 78.84375;
    const double c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    double ym1 = std::pow(Y, m1);
    return std::pow((c1 + c2 * ym1) / (1.0 + c3 * ym1), m2);
}

// ARIB STD-B67 (HLG) OETF, scene-linear in [0,1].
inline double HlgEncode(double c) {
    if (c <= 0.0) return 0.0;
    if (c > 1.0) c = 1.0;
    const double a = 0.17883277, b = 0.28466892, cc = 0.55991073;
    return c <= 1.0 / 12.0 ? std::sqrt(3.0 * c) : a * std::log(12.0 * c - b) + cc;
}

// Linear Rec.709/sRGB primaries -> linear Rec.2020 primaries (D65, BT.2087).
inline void ToRec2020(double& r, double& g, double& b) {
    double R = 0.6274 * r + 0.3293 * g + 0.0433 * b;
    double G = 0.0691 * r + 0.9195 * g + 0.0114 * b;
    double B = 0.0164 * r + 0.0880 * g + 0.8956 * b;
    r = R; g = G; b = B;
}

} // namespace chromacal_enc

#endif // CHROMACAL_ENCODE_H
