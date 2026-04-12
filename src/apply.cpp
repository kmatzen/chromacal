#include "chromacal/apply.h"

#include <iostream>
#include <stdexcept>

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
        in_depth = OCIO::BIT_DEPTH_UINT8;
        in_data = const_cast<uchar*>(image.ptr<uchar>());
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

} // namespace chromacal
