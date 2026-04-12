#include "chromacal/detect.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>

namespace chromacal {

// ---------------------------------------------------------------------------
// ColorChecker D50 reference Lab values (standard 24-patch Macbeth chart)
// ---------------------------------------------------------------------------

static const std::vector<cv::Vec3d> kReferenceLab = {
    {37.986, 13.555, 14.059},  {65.711, 18.13, 17.81},    {49.927, -4.88, -21.925},
    {43.139, -13.095, 21.905}, {55.112, 8.844, -25.399},  {70.719, -33.397, -0.199},
    {62.661, 36.067, 57.096},  {40.02, 10.41, -45.964},   {51.124, 48.239, 16.248},
    {30.325, 22.976, -21.587}, {72.532, -23.709, 57.255}, {71.941, 19.363, 67.857},
    {28.778, 14.179, -50.297}, {55.261, -38.342, 31.37},  {42.101, 53.378, 28.19},
    {81.733, 4.039, 79.819},   {51.935, 49.986, -14.574}, {51.038, -28.631, -28.638},
    {96.539, -0.425, 1.186},   {81.257, -0.638, -0.335},  {66.766, -0.734, -0.504},
    {50.867, -0.153, -0.27},   {35.656, -0.421, -1.231},  {20.461, -0.079, -0.973},
};

// ---------------------------------------------------------------------------
// Statistical tests
// ---------------------------------------------------------------------------

static double chi_square_sf(double x, double df) {
    // Regularized upper incomplete gamma function via series expansion
    if (x <= 0.0) return 1.0;
    double a = df / 2.0;
    double sum = 0.0;
    double term = 1.0 / a;
    sum = term;
    for (int k = 1; k < 200; ++k) {
        term *= (x / 2.0) / (a + k);
        sum += term;
        if (std::abs(term) < 1e-12 * std::abs(sum)) break;
    }
    double p = sum * std::exp(-x / 2.0 + a * std::log(x / 2.0) - std::lgamma(a));
    return std::max(0.0, std::min(1.0, 1.0 - p));
}

static double normal_sf(double x) {
    return 0.5 * std::erfc(x / std::sqrt(2.0));
}

double shapiro_wilk_test(const std::vector<double>& data) {
    int n = static_cast<int>(data.size());
    if (n < 3 || n > 5000) return 0.0;

    std::vector<double> x = data;
    std::sort(x.begin(), x.end());

    double mean = 0.0;
    for (double val : x) mean += val;
    mean /= n;

    double ssq = 0.0;
    for (double val : x) { double d = val - mean; ssq += d * d; }
    if (ssq < 1e-12) return 1.0;

    // Approximate expected normal order statistics
    std::vector<double> m(n);
    for (int i = 0; i < n; ++i) {
        double p = (i + 0.375) / (n + 0.25);
        // Rational approximation of the normal quantile
        double t = std::sqrt(-2.0 * std::log(p < 0.5 ? p : 1.0 - p));
        double c0 = 2.515517, c1 = 0.802853, c2 = 0.010328;
        double d1 = 1.432788, d2 = 0.189269, d3 = 0.001308;
        double q = t - (c0 + c1 * t + c2 * t * t) / (1.0 + d1 * t + d2 * t * t + d3 * t * t * t);
        m[i] = (p < 0.5) ? -q : q;
    }

    double m_ssq = 0.0;
    for (double val : m) m_ssq += val * val;

    std::vector<double> a(n);
    double sum_m2 = std::sqrt(m_ssq);
    for (int i = 0; i < n; ++i) a[i] = m[i] / sum_m2;

    double b = 0.0;
    for (int i = 0; i < n; ++i) b += a[i] * x[i];

    double W = (b * b) / ssq;
    double log_W = std::log(1.0 - W);
    double mu = -1.2725 + 1.0521 * std::log(static_cast<double>(n));
    double sigma = 1.0308 - 0.26758 * std::log(static_cast<double>(n));
    double z = (log_W - mu) / sigma;

    return normal_sf(z);
}

double mardia_skewness_test(const std::vector<cv::Vec3d>& pixels, const cv::Vec3d& mean,
                            const cv::Matx33d& covariance) {
    int n = static_cast<int>(pixels.size());
    if (n < 4) return 0.0;

    cv::Matx33d cov_inv;
    cv::invert(covariance, cov_inv);

    double b1p = 0.0;
    for (int i = 0; i < n; ++i) {
        cv::Vec3d di = pixels[i] - mean;
        for (int j = 0; j < n; ++j) {
            cv::Vec3d dj = pixels[j] - mean;
            double gij = 0.0;
            for (int a = 0; a < 3; ++a)
                for (int b = 0; b < 3; ++b)
                    gij += di[a] * cov_inv(a, b) * dj[b];
            b1p += gij * gij * gij;
        }
    }
    b1p /= (static_cast<double>(n) * n);

    double stat = n * b1p / 6.0;
    double df = 10.0; // p*(p+1)*(p+2)/6 where p=3
    return chi_square_sf(stat, df);
}

double mardia_kurtosis_test(const std::vector<cv::Vec3d>& pixels, const cv::Vec3d& mean,
                            const cv::Matx33d& covariance) {
    int n = static_cast<int>(pixels.size());
    if (n < 4) return 0.0;

    cv::Matx33d cov_inv;
    cv::invert(covariance, cov_inv);

    double b2p = 0.0;
    for (int i = 0; i < n; ++i) {
        cv::Vec3d di = pixels[i] - mean;
        double gii = 0.0;
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b)
                gii += di[a] * cov_inv(a, b) * di[b];
        b2p += gii * gii;
    }
    b2p /= n;

    double p = 3.0;
    double expected = p * (p + 2.0);
    double variance = 8.0 * p * (p + 2.0) / n;
    double z = (b2p - expected) / std::sqrt(variance);

    return 2.0 * normal_sf(std::abs(z));
}

double henze_zirkler_test(const std::vector<cv::Vec3d>& pixels, const cv::Vec3d& mean,
                          const cv::Matx33d& covariance) {
    int n = static_cast<int>(pixels.size());
    if (n < 4) return 0.0;

    double p = 3.0;
    double beta = 1.0 / std::sqrt(2.0) *
                  std::pow((2.0 * p + 1.0) / 4.0 * std::pow(n, 1.0 / (p + 4.0)),
                           1.0 / (p + 4.0));

    cv::Matx33d cov_inv;
    cv::invert(covariance, cov_inv);

    // Compute Mahalanobis distances
    std::vector<double> D(n);
    for (int i = 0; i < n; ++i) {
        cv::Vec3d di = pixels[i] - mean;
        double d = 0.0;
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b)
                d += di[a] * cov_inv(a, b) * di[b];
        D[i] = d;
    }

    // Term 1: double sum
    double S1 = 0.0;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            cv::Vec3d dij = pixels[i] - pixels[j];
            double d2 = 0.0;
            for (int a = 0; a < 3; ++a)
                for (int b = 0; b < 3; ++b)
                    d2 += dij[a] * cov_inv(a, b) * dij[b];
            S1 += std::exp(-beta * beta * d2 / 2.0);
        }
    }
    S1 /= (static_cast<double>(n) * n);

    // Term 2
    double S2 = 0.0;
    for (int i = 0; i < n; ++i) {
        S2 += std::exp(-beta * beta * D[i] / 2.0);
    }
    S2 *= 2.0 / n;

    double b2 = beta * beta;
    double term3 = std::pow(1.0 + 2.0 * b2, -p / 2.0);
    double HZ = S1 - S2 * std::pow(1.0 + b2, -p / 2.0) + term3;

    // Approximate log-normal parameters
    double omega2 = 1.0 + 2.0 * b2;
    double E_HZ = std::pow(1.0 + 2.0 * b2, -p / 2.0) *
                      (1.0 + p * b2 / (1.0 + 2.0 * b2) +
                       p * (p + 2.0) * b2 * b2 / (2.0 * omega2 * omega2)) -
                  2.0 * std::pow(1.0 + b2, -p / 2.0) *
                      (1.0 + p * b2 / (2.0 * (1.0 + b2)) +
                       p * (p + 2.0) * b2 * b2 / (8.0 * (1.0 + b2) * (1.0 + b2))) +
                  std::pow(1.0 + 2.0 * b2, -p / 2.0);

    double ln_HZ = std::log(std::max(HZ, 1e-12));
    double ln_E = std::log(std::max(E_HZ, 1e-12));
    double sigma2_ln = std::log(1.0 + 1.0 / n);
    double z = (ln_HZ - ln_E) / std::sqrt(sigma2_ln);

    return normal_sf(z);
}

NormalityTestResults test_normality(const std::vector<cv::Vec3d>& pixels, double alpha) {
    NormalityTestResults results;
    results.shapiro_pvalues.resize(3);
    results.passes_shapiro_per_channel.resize(3);

    if (pixels.size() < 3) {
        results.overall_passes = false;
        return results;
    }

    cv::Vec3d mean(0, 0, 0);
    for (const auto& p : pixels) mean += p;
    mean /= static_cast<double>(pixels.size());

    cv::Matx33d cov = cv::Matx33d::zeros();
    for (const auto& p : pixels) {
        cv::Vec3d d = p - mean;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                cov(i, j) += d[i] * d[j];
    }
    cov /= static_cast<double>(pixels.size() - 1);

    for (int ch = 0; ch < 3; ++ch) {
        std::vector<double> channel_data;
        channel_data.reserve(pixels.size());
        for (const auto& p : pixels) channel_data.push_back(p[ch]);
        results.shapiro_pvalues[ch] = shapiro_wilk_test(channel_data);
        results.passes_shapiro_per_channel[ch] = results.shapiro_pvalues[ch] > alpha;
    }

    results.mardia_skewness_pvalue = mardia_skewness_test(pixels, mean, cov);
    results.mardia_kurtosis_pvalue = mardia_kurtosis_test(pixels, mean, cov);
    results.passes_mardia_skewness = results.mardia_skewness_pvalue > alpha;
    results.passes_mardia_kurtosis = results.mardia_kurtosis_pvalue > alpha;

    results.henze_zirkler_pvalue = henze_zirkler_test(pixels, mean, cov);
    results.passes_henze_zirkler = results.henze_zirkler_pvalue > alpha;

    int passed_tests = 0;
    if (results.passes_mardia_skewness && results.passes_mardia_kurtosis) passed_tests++;
    if (results.passes_henze_zirkler) passed_tests++;
    int passed_channels = 0;
    for (bool p : results.passes_shapiro_per_channel)
        if (p) passed_channels++;
    if (passed_channels >= 2) passed_tests++;

    results.overall_passes = (passed_tests >= 1);
    return results;
}

// ---------------------------------------------------------------------------
// Detection
// ---------------------------------------------------------------------------

std::vector<PatchStatistics> detect(const cv::Mat& image, double exposure,
                                    float lower_threshold, float upper_threshold) {
    std::vector<PatchStatistics> result;

    // Detect ColorChecker
    auto detector = cv::mcc::CCheckerDetector::create();
    if (!detector->process(image, cv::mcc::MCC24)) {
        return result; // No chart found
    }

    auto checkers = detector->getListColorChecker();
    if (checkers.empty()) return result;

    auto checker = checkers[0];
    std::vector<cv::Point2f> charts = checker->getColorCharts();
    size_t num_patches = charts.size() / 4;

    // Convert to RGB float [0, 1]
    cv::Mat rgb_float;
    cv::cvtColor(image, rgb_float, cv::COLOR_BGR2RGB);
    rgb_float.convertTo(rgb_float, CV_64F, 1.0 / 255.0);

    for (size_t idx = 0; idx < num_patches && idx < kReferenceLab.size(); ++idx) {
        size_t corner_idx = idx * 4;
        std::vector<cv::Point> corners;
        for (size_t j = 0; j < 4; ++j) {
            const auto& pt = charts[corner_idx + j];
            corners.push_back(cv::Point(static_cast<int>(pt.x), static_cast<int>(pt.y)));
        }

        cv::Mat mask = cv::Mat::zeros(image.size(), CV_8UC1);
        cv::fillConvexPoly(mask, corners, cv::Scalar(255));

        std::vector<cv::Vec3d> pixels;
        cv::Rect bbox = cv::boundingRect(corners);
        int min_y = std::max(0, bbox.y);
        int max_y = std::min(rgb_float.rows - 1, bbox.y + bbox.height);
        int min_x = std::max(0, bbox.x);
        int max_x = std::min(rgb_float.cols - 1, bbox.x + bbox.width);

        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                if (mask.at<uchar>(y, x) > 0) {
                    cv::Vec3d px = rgb_float.at<cv::Vec3d>(y, x);
                    if (px[0] > lower_threshold && px[0] < upper_threshold &&
                        px[1] > lower_threshold && px[1] < upper_threshold &&
                        px[2] > lower_threshold && px[2] < upper_threshold) {
                        pixels.push_back(px);
                    }
                }
            }
        }

        if (pixels.size() < 10) continue;

        // Compute mean
        cv::Vec3d mean(0, 0, 0);
        for (const auto& p : pixels) mean += p;
        mean /= static_cast<double>(pixels.size());

        // Compute covariance
        cv::Matx33d cov = cv::Matx33d::zeros();
        for (const auto& p : pixels) {
            cv::Vec3d d = p - mean;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    cov(i, j) += d[i] * d[j];
        }
        cov /= static_cast<double>(pixels.size() - 1);
        // Regularization
        cov(0, 0) += 1e-6;
        cov(1, 1) += 1e-6;
        cov(2, 2) += 1e-6;

        PatchStatistics stats;
        stats.mean = mean;
        stats.covariance = cov;
        stats.reference_lab = kReferenceLab[idx];
        stats.exposure = exposure;
        stats.pixel_count = static_cast<int>(pixels.size());
        stats.raw_pixels = pixels;
        stats.normality_tests = test_normality(pixels);

        result.push_back(stats);
    }

    return result;
}

std::vector<PatchStatistics> filter_normal(const std::vector<PatchStatistics>& patches) {
    std::vector<PatchStatistics> filtered;
    for (const auto& p : patches) {
        if (p.normality_tests.overall_passes) {
            filtered.push_back(p);
        }
    }
    return filtered;
}

} // namespace chromacal
