#pragma once

#include "chromacal/types.h"

#include <memory>
#include <string>
#include <vector>

namespace chromacal {

/// Solves for a log-polynomial luma curve + 3x3 color correction matrix
/// that maps camera RGB to reference Lab colors using Ceres optimization.
///
/// The model jointly fits:
/// - A 3rd-order polynomial in log-space (tone curve)
/// - A 3x3 linear color matrix (white balance + cross-talk correction)
///
/// Residuals are weighted by per-patch covariance (Mahalanobis distance)
/// and perceptual importance (luminance + chroma).
class Solver {
  public:
    Solver();
    ~Solver();

    /// Fit the model to detected ColorChecker patches.
    ///
    /// @param patches  Patch statistics from detect() or filter_normal().
    ///                 Must contain at least one patch.
    /// @throws std::invalid_argument if patches is empty.
    void solve(const std::vector<PatchStatistics>& patches);

    /// Apply the calibration to an image.
    ///
    /// @param image  Input image (CV_64FC3, RGB, gamma-encoded, 0-1 range).
    /// @return Corrected image (CV_64FC3, linear RGB).
    cv::Mat infer(const cv::Mat& image) const;

    /// Get the 3x3 color correction matrix.
    cv::Mat get_ccm() const;

    /// Get the 4 log-polynomial luma curve coefficients.
    std::vector<double> get_luma_params() const;

    /// Get the reference exposure used during solve().
    double get_reference_exposure() const;

    /// Get the final optimization cost.
    double get_final_error() const;

    /// Save calibration to a YAML file.
    void save(const std::string& filename) const;

    /// Load calibration from a YAML file.
    void load(const std::string& filename);

  private:
    std::unique_ptr<CalibrationResult> result_;
};

} // namespace chromacal
