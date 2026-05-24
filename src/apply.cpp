// NOTE: create_lut / apply_lut (the OpenColorIO-backed helpers) live in
// src/lut_ocio.cpp so that binaries needing only write_cube (the Premiere effect
// and CLIs) don't link OpenColorIO + its transitive deps.

#include "chromacal/apply.h"

#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <vector>

namespace chromacal {

void write_cube(const Solver& solver, const std::string& path,
                int lut_size, const std::string& title) {
    if (lut_size < 2)
        throw std::invalid_argument("lut_size must be >= 2");

    const int N = lut_size;
    // Flattened grid in .cube write order: red varies fastest, blue slowest,
    // so the value at (r, g, b) lives at index ((b * N) + g) * N + r.
    std::vector<cv::Vec3f> grid(static_cast<size_t>(N) * N * N);

#pragma omp parallel for
    for (int b = 0; b < N; ++b) {
        cv::Mat slice(N, N, CV_64FC3); // rows = green, cols = red

        for (int g = 0; g < N; ++g) {
            for (int r = 0; r < N; ++r) {
                slice.at<cv::Vec3d>(g, r) = cv::Vec3d(
                    static_cast<double>(r) / (N - 1),
                    static_cast<double>(g) / (N - 1),
                    static_cast<double>(b) / (N - 1));
            }
        }

        cv::Mat calibrated = solver.infer(slice);

        for (int g = 0; g < N; ++g) {
            for (int r = 0; r < N; ++r) {
                const auto& px = calibrated.at<cv::Vec3d>(g, r);
                grid[(static_cast<size_t>(b) * N + g) * N + r] =
                    cv::Vec3f(static_cast<float>(px[0]),
                              static_cast<float>(px[1]),
                              static_cast<float>(px[2]));
            }
        }
    }

    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("Failed to open file for writing: " + path);

    if (!title.empty())
        out << "TITLE \"" << title << "\"\n";
    out << "LUT_3D_SIZE " << N << "\n";
    out << "DOMAIN_MIN 0.0 0.0 0.0\n";
    out << "DOMAIN_MAX 1.0 1.0 1.0\n";

    out << std::fixed << std::setprecision(6);
    for (const auto& px : grid)
        out << px[0] << ' ' << px[1] << ' ' << px[2] << '\n';

    if (!out)
        throw std::runtime_error("Failed while writing file: " + path);
}

} // namespace chromacal
