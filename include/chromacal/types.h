#pragma once

#include <opencv2/opencv.hpp>
#include <vector>

namespace chromacal {

/// Results of multivariate normality tests on a color patch.
struct NormalityTestResults {
    bool passes_mardia_skewness = false;
    bool passes_mardia_kurtosis = false;
    bool passes_henze_zirkler = false;
    std::vector<bool> passes_shapiro_per_channel;
    double mardia_skewness_pvalue = 0.0;
    double mardia_kurtosis_pvalue = 0.0;
    double henze_zirkler_pvalue = 0.0;
    std::vector<double> shapiro_pvalues;
    bool overall_passes = false;
};

/// Statistics for a single ColorChecker patch.
struct PatchStatistics {
    cv::Vec3d mean;                    ///< Mean RGB color (gamma-encoded, 0-1).
    cv::Matx33d covariance;            ///< 3x3 RGB covariance matrix.
    cv::Vec3d reference_lab;           ///< Reference CIE Lab color (D50).
    double exposure = 1.0;             ///< Relative exposure value.
    int pixel_count = 0;               ///< Number of valid pixels.
    std::vector<cv::Vec3d> raw_pixels; ///< Raw pixel values for normality testing.
    NormalityTestResults normality_tests;

    /// Robust reliability weight in (0, 1], based on the fraction of pixels
    /// that are multivariate outliers (Mahalanobis distance beyond the
    /// chi-square expectation). 1.0 for a clean, flat patch; lower for patches
    /// contaminated by specular highlights, occlusions, or shadow edges. The
    /// solver uses it to down-weight unreliable patches without discarding
    /// them — a graceful alternative to the binary normality gate, and one
    /// that does not suffer the large-sample hypersensitivity of the
    /// normality tests.
    double reliability = 1.0;
};

/// Calibration result from the solver.
struct CalibrationResult {
    std::vector<double> luma_params; ///< Log-polynomial coefficients [p0, p1, p2, p3].
    cv::Mat color_matrix;            ///< 3x3 color correction matrix.
    double final_error = 0.0;        ///< Final optimization cost.
    double reference_exposure = 1.0; ///< Reference exposure for normalization.
};

} // namespace chromacal
