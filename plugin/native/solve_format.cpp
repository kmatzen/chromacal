#include "solve_format.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace chromacal_ppro {

namespace {
double cube_tone(double c, const double* p) {
    if (!(c > 0.0)) c = 0.0; // guard NaN / negative
    double l = std::log(c + 1e-6);
    double v = std::exp(p[0] + p[1] * l + p[2] * l * l + p[3] * l * l * l);
    return (v == v && v < 1e12) ? v : 0.0;
}
double cube_encode(double c, double gamma) {
    if (!(c > 0.0)) return 0.0;
    double out = gamma > 0.0
                     ? std::pow(c, 1.0 / gamma) // gamma == 1.0 => linear identity
                     : (c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055);
    if (!(out == out)) return 0.0; // NaN
    return out > 1.0 ? 1.0 : out;  // keep Input-LUT output in [0,1]
}
// sRGB EOTF (decode sRGB-encoded value -> linear).
double srgb_decode(double c) {
    if (!(c > 0.0)) return 0.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
} // namespace

bool write_display_cube(const double* luma, const double* ccm,
                        const std::string& path, int lut_size, double gamma) {
    if (lut_size < 2 || !luma || !ccm) return false;
    try {
        std::filesystem::path p(path);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    } catch (...) { /* best effort */ }

    std::ofstream out(path);
    if (!out) return false;
    const int N = lut_size;
    out << "TITLE \"chromacal\"\n";
    out << "LUT_3D_SIZE " << N << "\n";
    out << "DOMAIN_MIN 0.0 0.0 0.0\nDOMAIN_MAX 1.0 1.0 1.0\n";
    out << std::fixed << std::setprecision(6);
    for (int b = 0; b < N; ++b) {           // blue slowest, red fastest (.cube order)
        for (int g = 0; g < N; ++g) {
            for (int r = 0; r < N; ++r) {
                // Premiere tags this .cube as sRGB and transcodes working<->sRGB
                // around it (working -> sRGB -> LUT -> sRGB -> working). The
                // calibration was fit in the working (Rec.709, `gamma`) space, so:
                //   sRGB grid in -> linear -> working-gamma  (undo Lumetri's input
                //   transcode), tone curve + CCM, then sRGB-encode the output
                //   (Lumetri converts sRGB -> working). Nets to the effect's result.
                double R = std::pow(srgb_decode(static_cast<double>(r) / (N - 1)), 1.0 / gamma);
                double G = std::pow(srgb_decode(static_cast<double>(g) / (N - 1)), 1.0 / gamma);
                double B = std::pow(srgb_decode(static_cast<double>(b) / (N - 1)), 1.0 / gamma);
                double aR = cube_tone(R, luma), aG = cube_tone(G, luma), aB = cube_tone(B, luma);
                double linR = ccm[0] * aR + ccm[1] * aG + ccm[2] * aB;
                double linG = ccm[3] * aR + ccm[4] * aG + ccm[5] * aB;
                double linB = ccm[6] * aR + ccm[7] * aG + ccm[8] * aB;
                out << cube_encode(linR, -1.0) << ' ' << cube_encode(linG, -1.0) << ' '
                    << cube_encode(linB, -1.0) << '\n'; // sRGB-encoded output
            }
        }
    }
    return static_cast<bool>(out);
}

bool write_effect_cube(const double* luma, const double* ccm,
                       const std::string& path, int lut_size, double gamma) {
    if (lut_size < 2 || !luma || !ccm) return false;
    try {
        std::filesystem::path p(path);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    } catch (...) { /* best effort */ }

    std::ofstream out(path);
    if (!out) return false;
    const int N = lut_size;
    out << "TITLE \"chromacal\"\n";
    out << "LUT_3D_SIZE " << N << "\n";
    out << "DOMAIN_MIN 0.0 0.0 0.0\nDOMAIN_MAX 1.0 1.0 1.0\n";
    out << std::fixed << std::setprecision(6);
    for (int b = 0; b < N; ++b) {
        for (int g = 0; g < N; ++g) {
            for (int r = 0; r < N; ++r) {
                // The effect's exact transform, no color-management compensation:
                // input grid is the working-gamma value, tone + CCM, working-gamma
                // encode. Matches chromacal_apply / the live effect 1:1.
                double R = static_cast<double>(r) / (N - 1);
                double G = static_cast<double>(g) / (N - 1);
                double B = static_cast<double>(b) / (N - 1);
                double aR = cube_tone(R, luma), aG = cube_tone(G, luma), aB = cube_tone(B, luma);
                double linR = ccm[0] * aR + ccm[1] * aG + ccm[2] * aB;
                double linG = ccm[3] * aR + ccm[4] * aG + ccm[5] * aB;
                double linB = ccm[6] * aR + ccm[7] * aG + ccm[8] * aB;
                out << cube_encode(linR, gamma) << ' ' << cube_encode(linG, gamma) << ' '
                    << cube_encode(linB, gamma) << '\n';
            }
        }
    }
    return static_cast<bool>(out);
}

bool write_calibration(const double* luma, const double* ccm, const std::string& path) {
    if (!luma || !ccm) return false;
    try {
        std::filesystem::path p(path);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    } catch (...) { /* best effort */ }
    std::ofstream out(path);
    if (!out) return false;
    out << std::setprecision(17);
    out << "chromacal-calibration 1\n";
    out << "luma";
    for (int i = 0; i < 4; ++i) out << ' ' << luma[i];
    out << "\nccm";
    for (int i = 0; i < 9; ++i) out << ' ' << ccm[i];
    out << '\n';
    return static_cast<bool>(out);
}

std::vector<std::array<double, 3>> read_reference_lab(const std::string& path) {
    std::vector<std::array<double, 3>> out;
    std::ifstream in(path);
    if (!in) return out;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string name;
        double L, a, b;
        if (!(ss >> name)) continue;          // blank line
        if (!(ss >> L >> a >> b)) continue;   // header or non-numeric row
        out.push_back({L, a, b});
    }
    return out;
}

bool read_calibration(const std::string& path, double* luma, double* ccm) {
    if (!luma || !ccm) return false;
    std::ifstream in(path);
    if (!in) return false;
    std::string tag;
    int ver = 0;
    if (!(in >> tag >> ver) || tag != "chromacal-calibration") return false;
    bool gotLuma = false, gotCcm = false;
    std::string key;
    while (in >> key) {
        if (key == "luma") {
            for (int i = 0; i < 4; ++i)
                if (!(in >> luma[i])) return false;
            gotLuma = true;
        } else if (key == "ccm") {
            for (int i = 0; i < 9; ++i)
                if (!(in >> ccm[i])) return false;
            gotCcm = true;
        }
    }
    return gotLuma && gotCcm;
}

} // namespace chromacal_ppro
