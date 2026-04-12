#pragma once

#include "chromacal/solver.h"

#include <OpenColorIO/OpenColorIO.h>
#include <opencv2/opencv.hpp>

namespace OCIO = OCIO_NAMESPACE;

namespace chromacal {

/// Build an OpenColorIO 3D LUT from a calibration solver.
///
/// The LUT encodes the full calibration (luma curve + color matrix) as a
/// 3D lookup table for GPU-accelerated or batch application.
///
/// @param solver    A solver with a completed calibration.
/// @param lut_size  Grid resolution (default 129 for high quality).
/// @return An OCIO CPU processor that can be applied to images.
OCIO::ConstCPUProcessorRcPtr create_lut(const Solver& solver, int lut_size = 129);

/// Apply an OCIO processor (3D LUT) to an image.
///
/// @param image      Input image (CV_8UC3, CV_32FC3, or CV_64FC3, RGB channel order).
/// @param processor  OCIO processor from create_lut().
/// @return Corrected image (CV_32FC3, RGB).
cv::Mat apply_lut(const cv::Mat& image, OCIO::ConstCPUProcessorRcPtr processor);

} // namespace chromacal
