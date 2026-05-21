#pragma once

#include "chromacal/solver.h"

#include <OpenColorIO/OpenColorIO.h>
#include <opencv2/opencv.hpp>

#include <string>

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

/// Write the calibration as an Iridas/Resolve `.cube` 3D LUT file.
///
/// Samples the solver on a regular grid and serializes directly to the Adobe
/// Cube LUT format (red channel varies fastest, blue slowest), independent of
/// OpenColorIO. Use this to hand the calibration to another application — e.g.
/// a DaVinci Resolve / Premiere Lumetri LUT slot.
///
/// Input domain is gamma-encoded RGB in [0, 1]; output is linear RGB at the
/// solver's reference exposure and may fall outside [0, 1] (values are written
/// unclamped to preserve the calibration).
///
/// @param solver    A solver with a completed calibration.
/// @param path      Output file path (`.cube`).
/// @param lut_size  Grid resolution (default 33, the conventional `.cube` size;
///                  raise to 65 for more precision in nonlinear regions).
/// @param title     Optional TITLE field written into the file header.
/// @throws std::invalid_argument if lut_size < 2.
/// @throws std::runtime_error    if the file cannot be written.
void write_cube(const Solver& solver, const std::string& path,
                int lut_size = 33, const std::string& title = "chromacal");

} // namespace chromacal
