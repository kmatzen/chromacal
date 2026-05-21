#include "chromacal/apply.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace chromacal {

OCIO::ConstCPUProcessorRcPtr create_lut(const Solver& solver, int lut_size) {
    auto lut = OCIO::Lut3DTransform::Create();
    lut->setGridSize(lut_size);
    lut->setInterpolation(OCIO::INTERP_LINEAR);

#pragma omp parallel for
    for (int r = 0; r < lut_size; ++r) {
        cv::Mat slice(lut_size, lut_size, CV_64FC3);

        for (int g = 0; g < lut_size; ++g) {
            for (int b = 0; b < lut_size; ++b) {
                double R = static_cast<double>(r) / (lut_size - 1);
                double G = static_cast<double>(g) / (lut_size - 1);
                double B = static_cast<double>(b) / (lut_size - 1);
                slice.at<cv::Vec3d>(g, b) = cv::Vec3d(R, G, B);
            }
        }

        cv::Mat calibrated = solver.infer(slice);
        calibrated.convertTo(calibrated, CV_32F);

        for (int g = 0; g < lut_size; ++g) {
            for (int b = 0; b < lut_size; ++b) {
                const auto& px = calibrated.at<cv::Vec3f>(g, b);
                lut->setValue(r, g, b, px[0], px[1], px[2]);
            }
        }
    }

    OCIO::ConstConfigRcPtr config = OCIO::Config::CreateRaw();
    OCIO::ConstProcessorRcPtr processor = config->getProcessor(lut);
    return processor->getDefaultCPUProcessor();
}

cv::Mat apply_lut(const cv::Mat& image, OCIO::ConstCPUProcessorRcPtr processor) {
    if (image.channels() != 3)
        throw std::runtime_error("Input image must have 3 channels (RGB)");

    cv::Mat output(image.rows, image.cols, CV_32FC3);
    int width = image.cols;
    int height = image.rows;

    OCIO::BitDepth in_depth;
    void* in_data = nullptr;
    cv::Mat temp;

    if (image.type() == CV_8UC3) {
        image.convertTo(temp, CV_32F, 1.0 / 255.0);
        in_depth = OCIO::BIT_DEPTH_F32;
        in_data = temp.ptr<float>();
    } else if (image.type() == CV_32FC3) {
        in_depth = OCIO::BIT_DEPTH_F32;
        in_data = const_cast<float*>(image.ptr<float>());
    } else if (image.type() == CV_64FC3) {
        image.convertTo(temp, CV_32F);
        in_depth = OCIO::BIT_DEPTH_F32;
        in_data = temp.ptr<float>();
    } else {
        throw std::runtime_error("Unsupported image type (must be CV_8UC3, CV_32FC3, or CV_64FC3)");
    }

    OCIO::PackedImageDesc src(in_data, width, height, 3, in_depth,
                              OCIO::AutoStride, OCIO::AutoStride, OCIO::AutoStride);
    OCIO::PackedImageDesc dst(output.ptr<float>(), width, height, 3,
                              OCIO::BIT_DEPTH_F32,
                              OCIO::AutoStride, OCIO::AutoStride, OCIO::AutoStride);
    processor->apply(src, dst);

    return output;
}

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
