#pragma once

#include "chromacal/types.h"

#include <opencv2/mcc.hpp>
#include <opencv2/opencv.hpp>
#include <vector>

namespace chromacal {

/// Which ColorChecker layout to detect.
enum class ChartType { Classic24, SG140 };

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
/// @param chart       Chart layout (Classic 24-patch by default, or SG 140-patch).
/// @param reference_lab  Optional custom reference Lab (D50) in patch order; required
///                    for SG140 (whose reference data isn't bundled). When null,
///                    the built-in 24-patch reference is used (Classic only).
/// @return Patch statistics for each detected patch, or empty if detection fails.
std::vector<PatchStatistics> detect(const cv::Mat& image, double exposure = 1.0,
                                    float lower_threshold = 0.01f,
                                    float upper_threshold = 0.99f,
                                    ChartType chart = ChartType::Classic24,
                                    const std::vector<cv::Vec3d>* reference_lab = nullptr);

/// Run multivariate normality tests on a set of 3D pixel samples.
///
/// Runs Shapiro-Wilk (per channel), Mardia skewness/kurtosis, and
/// Henze-Zirkler tests. Passes overall if any test category passes.
NormalityTestResults test_normality(const std::vector<cv::Vec3d>& pixels, double alpha = 0.01);

/// Robust reliability weight for a patch, in (0, 1].
///
/// Computes the fraction of pixels whose squared Mahalanobis distance exceeds
/// the chi-square(3) 99th percentile (~1% expected for clean Gaussian data),
/// then maps the excess to a weight: 1.0 for a clean patch, decreasing
/// smoothly toward a small floor as gross outliers (specular highlights,
/// occlusions, shadow edges) accumulate. Unlike a normality hypothesis test,
/// this is stable with respect to sample size, so it does not reject good
/// patches simply because they contain many pixels.
double patch_reliability(const std::vector<cv::Vec3d>& pixels, const cv::Vec3d& mean,
                         const cv::Matx33d& covariance);

/// Filter patches to only those passing normality tests.
std::vector<PatchStatistics> filter_normal(const std::vector<PatchStatistics>& patches);

} // namespace chromacal
