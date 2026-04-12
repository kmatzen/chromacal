#pragma once

#include "chromacal/types.h"

#include <opencv2/mcc.hpp>
#include <opencv2/opencv.hpp>
#include <vector>

namespace chromacal {

/// Detect a ColorChecker in an image and extract patch statistics.
///
/// Uses OpenCV's MCC (Macbeth ColorChecker) detector to locate the chart,
/// then extracts per-patch mean, covariance, and normality test results.
///
/// @param image       BGR input image (CV_8UC3).
/// @param exposure    Relative exposure value (e.g. ISO * shutter_speed / 32).
///                    Set to 1.0 if all images share the same exposure.
/// @param lower_threshold  Minimum pixel value (0-1) to include (rejects blacks).
/// @param upper_threshold  Maximum pixel value (0-1) to include (rejects clipped whites).
/// @return Patch statistics for each detected patch, or empty if detection fails.
std::vector<PatchStatistics> detect(const cv::Mat& image, double exposure = 1.0,
                                    float lower_threshold = 0.01f,
                                    float upper_threshold = 0.99f);

/// Run multivariate normality tests on a set of 3D pixel samples.
///
/// Runs Shapiro-Wilk (per channel), Mardia skewness/kurtosis, and
/// Henze-Zirkler tests. Passes overall if any test category passes.
NormalityTestResults test_normality(const std::vector<cv::Vec3d>& pixels, double alpha = 0.01);

/// Filter patches to only those passing normality tests.
std::vector<PatchStatistics> filter_normal(const std::vector<PatchStatistics>& patches);

} // namespace chromacal
