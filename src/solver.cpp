#include "chromacal/solver.h"

#include <ceres/ceres.h>
#include <cmath>
#include <iostream>

namespace chromacal {

// ---------------------------------------------------------------------------
// Ceres cost functions (see solver.h for algorithm description)
// ---------------------------------------------------------------------------

struct ColorCalibrationCost {
    ColorCalibrationCost(const cv::Vec3d& input_mean, const cv::Matx33d& input_covariance,
                         const cv::Vec3d& reference, double exposure, double reference_exposure)
        : input_mean(input_mean), reference_color(reference), exposure(exposure),
          reference_exposure(reference_exposure) {
        // Compute Sigma^{-1/2} via SVD for numerically stable Mahalanobis weighting
        cv::Matx33d U, Vt;
        cv::Vec3d sv;
        cv::SVD::compute(input_covariance, sv, U, Vt);
        cv::Matx33d D_inv_sqrt = cv::Matx33d::zeros();
        for (int i = 0; i < 3; ++i)
            D_inv_sqrt(i, i) = 1.0 / std::sqrt(std::max(sv[i], 1e-12));
        sqrt_inv_cov = U * D_inv_sqrt * U.t();
    }

    template <typename T> static T lab_finv(const T& t) {
        T delta = T(6.0 / 29.0);
        T cubic = t * t * t;
        T linear = T(3.0) * delta * delta * (t - T(4.0 / 29.0));
        T blend = (ceres::tanh((t - delta) * T(100.0)) + T(1.0)) * T(0.5);
        return blend * cubic + (T(1.0) - blend) * linear;
    }

    template <typename T> static void lab_to_rgb(const T& l, const T& a, const T& b, T* rgb) {
        T fy = (l + T(16.0)) / T(116.0);
        T fx = a / T(500.0) + fy;
        T fz = fy - b / T(200.0);

        T x50 = lab_finv(fx) * T(0.96422);
        T y50 = lab_finv(fy) * T(1.00000);
        T z50 = lab_finv(fz) * T(0.82521);

        // Bradford D50 -> D65
        T x65 = x50 * T(0.9555766) + y50 * T(-0.0230393) + z50 * T(0.0631636);
        T y65 = x50 * T(-0.0282895) + y50 * T(1.0099416) + z50 * T(0.0210077);
        T z65 = x50 * T(0.0122982) + y50 * T(-0.0204830) + z50 * T(1.3299098);

        // XYZ D65 -> sRGB
        rgb[0] = x65 * T(3.2404542) - y65 * T(1.5371385) - z65 * T(0.4985314);
        rgb[1] = -x65 * T(0.9692660) + y65 * T(1.8760108) + z65 * T(0.0415560);
        rgb[2] = x65 * T(0.0556434) - y65 * T(0.2040259) + z65 * T(1.0572252);
    }

    template <typename T>
    bool operator()(const T* const luma, const T* const ccm, T* residuals) const {
        // Log-polynomial luma curve
        T log_r = ceres::log(T(input_mean[0]) + T(1e-6));
        T log_g = ceres::log(T(input_mean[1]) + T(1e-6));
        T log_b = ceres::log(T(input_mean[2]) + T(1e-6));

        T adj_r = ceres::exp(luma[0] + luma[1]*log_r + luma[2]*log_r*log_r + luma[3]*log_r*log_r*log_r);
        T adj_g = ceres::exp(luma[0] + luma[1]*log_g + luma[2]*log_g*log_g + luma[3]*log_g*log_g*log_g);
        T adj_b = ceres::exp(luma[0] + luma[1]*log_b + luma[2]*log_b*log_b + luma[3]*log_b*log_b*log_b);

        // 3x3 color matrix
        T pr = ccm[0]*adj_r + ccm[1]*adj_g + ccm[2]*adj_b;
        T pg = ccm[3]*adj_r + ccm[4]*adj_g + ccm[5]*adj_b;
        T pb = ccm[6]*adj_r + ccm[7]*adj_g + ccm[8]*adj_b;

        // Exposure normalization
        T ratio = T(exposure) / T(reference_exposure);
        pr /= ratio; pg /= ratio; pb /= ratio;

        // Reference Lab -> RGB
        T ref_rgb[3];
        lab_to_rgb(T(reference_color[0]), T(reference_color[1]), T(reference_color[2]), ref_rgb);

        T dr = pr - ref_rgb[0];
        T dg = pg - ref_rgb[1];
        T db = pb - ref_rgb[2];

        // Perceptual weight
        T lum = T(0.2126729)*ref_rgb[0] + T(0.7151522)*ref_rgb[1] + T(0.0721750)*ref_rgb[2];
        T chroma = ceres::sqrt(T(reference_color[1])*T(reference_color[1]) +
                               T(reference_color[2])*T(reference_color[2]));
        T w = (T(1.0) + chroma / T(100.0)) / (lum + T(0.01));

        // Mahalanobis-weighted residuals
        residuals[0] = w * (T(sqrt_inv_cov(0,0))*dr + T(sqrt_inv_cov(0,1))*dg + T(sqrt_inv_cov(0,2))*db);
        residuals[1] = w * (T(sqrt_inv_cov(1,0))*dr + T(sqrt_inv_cov(1,1))*dg + T(sqrt_inv_cov(1,2))*db);
        residuals[2] = w * (T(sqrt_inv_cov(2,0))*dr + T(sqrt_inv_cov(2,1))*dg + T(sqrt_inv_cov(2,2))*db);
        return true;
    }

    const cv::Vec3d input_mean;
    const cv::Vec3d reference_color;
    const double exposure;
    const double reference_exposure;
    cv::Matx33d sqrt_inv_cov;
};

struct WhiteBalanceCost {
    WhiteBalanceCost(const cv::Vec3d& input, double exp, double ref_exp)
        : input_mean(input), exposure(exp), reference_exposure(ref_exp) {}

    template <typename T>
    bool operator()(const T* const luma, const T* const ccm, T* residuals) const {
        T log_r = ceres::log(T(input_mean[0]) + T(1e-6));
        T log_g = ceres::log(T(input_mean[1]) + T(1e-6));
        T log_b = ceres::log(T(input_mean[2]) + T(1e-6));

        T adj_r = ceres::exp(luma[0] + luma[1]*log_r + luma[2]*log_r*log_r + luma[3]*log_r*log_r*log_r);
        T adj_g = ceres::exp(luma[0] + luma[1]*log_g + luma[2]*log_g*log_g + luma[3]*log_g*log_g*log_g);
        T adj_b = ceres::exp(luma[0] + luma[1]*log_b + luma[2]*log_b*log_b + luma[3]*log_b*log_b*log_b);

        T pr = ccm[0]*adj_r + ccm[1]*adj_g + ccm[2]*adj_b;
        T pg = ccm[3]*adj_r + ccm[4]*adj_g + ccm[5]*adj_b;
        T pb = ccm[6]*adj_r + ccm[7]*adj_g + ccm[8]*adj_b;

        T ratio = T(exposure) / T(reference_exposure);
        pr /= ratio; pg /= ratio; pb /= ratio;

        residuals[0] = pr - pg;
        residuals[1] = pg - pb;
        return true;
    }

    const cv::Vec3d input_mean;
    const double exposure;
    const double reference_exposure;
};

// ---------------------------------------------------------------------------
// Solver implementation
// ---------------------------------------------------------------------------

Solver::Solver() = default;
Solver::~Solver() = default;
Solver::Solver(Solver&&) noexcept = default;
Solver& Solver::operator=(Solver&&) noexcept = default;

void Solver::solve(const std::vector<PatchStatistics>& patches, bool verbose) {
    if (patches.empty())
        throw std::invalid_argument("Patch statistics vector is empty.");

    double sum_exp = 0.0;
    for (const auto& p : patches) sum_exp += p.exposure;
    double ref_exp = sum_exp / patches.size();

    std::vector<double> luma = {0.0, 1.0, 0.0, 0.0};
    double ccm[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

    ceres::Problem problem;

    for (const auto& p : patches)
        if (p.exposure <= 0.0)
            throw std::invalid_argument("Patch exposure must be positive (got <= 0): the "
                                        "exposure normalization divides by it, so a zero "
                                        "exposure would produce divide-by-zero residuals.");

    // Neutral (grayscale) patches get an extra white-balance residual pinning
    // R=G=B. Identified by reference chroma rather than position in `patches`:
    // detect() can drop a patch for insufficient valid pixels, filter_normal()
    // (a documented pre-solve step) can remove patches, and SG140 doesn't share
    // the Classic chart's layout at all -- so a fixed index (e.g. "patches
    // 18-23 of every 24") silently drifts onto chromatic patches under any of
    // those. Reference chroma has a large margin here: the Classic chart's
    // grayscale row tops out at chroma ~1.3, while its most neutral chromatic
    // patch is ~19.5.
    const double kNeutralChromaThreshold = 5.0;

    for (const auto& p : patches) {
        // Down-weight unreliable patches (contaminated by specular highlights,
        // occlusions, etc.) by scaling the loss by reliability^2 -- equivalent
        // to scaling the residual by the reliability. reliability == 1 leaves
        // the plain Huber loss untouched, so clean patches are unaffected.
        double rw = p.reliability;
        ceres::LossFunction* loss = new ceres::HuberLoss(3.0);
        if (p.reliability < 1.0)
            loss = new ceres::ScaledLoss(loss, rw, ceres::TAKE_OWNERSHIP);

        problem.AddResidualBlock(
            new ceres::AutoDiffCostFunction<ColorCalibrationCost, 3, 4, 9>(
                new ColorCalibrationCost(p.mean, p.covariance, p.reference_lab, p.exposure, ref_exp)),
            loss, luma.data(), ccm);

        double chroma = std::hypot(p.reference_lab[1], p.reference_lab[2]);
        if (chroma < kNeutralChromaThreshold) {
            problem.AddResidualBlock(
                new ceres::AutoDiffCostFunction<WhiteBalanceCost, 2, 4, 9>(
                    new WhiteBalanceCost(p.mean, p.exposure, ref_exp)),
                new ceres::ScaledLoss(nullptr, 0.1 * rw, ceres::TAKE_OWNERSHIP), luma.data(), ccm);
        }
    }

    // Bounds on CCM
    for (int i = 0; i < 9; ++i) {
        if (i == 0 || i == 4 || i == 8) {
            problem.SetParameterLowerBound(ccm, i, 0.1);
            problem.SetParameterUpperBound(ccm, i, 3.0);
        } else {
            problem.SetParameterLowerBound(ccm, i, -1.5);
            problem.SetParameterUpperBound(ccm, i, 1.5);
        }
    }

    ceres::Solver::Options options;
    options.linear_solver_type = ceres::DENSE_QR;
    options.minimizer_progress_to_stdout = verbose;
    options.max_num_iterations = 100;
    options.function_tolerance = 1e-8;
    options.gradient_tolerance = 1e-10;
    options.parameter_tolerance = 1e-10;

    ceres::Solver::Summary summary;
    ceres::Solve(options, &problem, &summary);
    if (verbose) std::cout << summary.FullReport() << std::endl;

    result_ = std::make_unique<CalibrationResult>();
    result_->luma_params = luma;
    result_->color_matrix = cv::Mat(3, 3, CV_64F, ccm).clone();
    result_->final_error = summary.final_cost;
    result_->report = summary.BriefReport();
    result_->converged = summary.IsSolutionUsable();
    result_->reference_exposure = ref_exp;
}

cv::Mat Solver::infer(const cv::Mat& image) const {
    if (!result_) throw std::runtime_error("No calibration loaded.");
    if (image.empty()) throw std::invalid_argument("Input image is empty.");
    if (image.type() != CV_64FC3) throw std::invalid_argument("Image must be CV_64FC3.");

    cv::Mat out(image.size(), CV_64FC3);
    const auto& lp = result_->luma_params;
    const double l0 = lp[0], l1 = lp[1], l2 = lp[2], l3 = lp[3];
    // Hoist the CCM into locals: this is the inner loop behind both LUT-baking
    // paths (write_cube / create_lut sample it per grid cell), and 9 bounds-
    // checked cv::Mat::at<double>() lookups per pixel were pure overhead over a
    // 3x3 that never changes during the loop.
    const cv::Mat& cm = result_->color_matrix;
    const double m00 = cm.at<double>(0,0), m01 = cm.at<double>(0,1), m02 = cm.at<double>(0,2);
    const double m10 = cm.at<double>(1,0), m11 = cm.at<double>(1,1), m12 = cm.at<double>(1,2);
    const double m20 = cm.at<double>(2,0), m21 = cm.at<double>(2,1), m22 = cm.at<double>(2,2);

    for (int y = 0; y < image.rows; ++y) {
        const cv::Vec3d* in_row = image.ptr<cv::Vec3d>(y);
        cv::Vec3d* out_row = out.ptr<cv::Vec3d>(y);
        for (int x = 0; x < image.cols; ++x) {
            const cv::Vec3d& in = in_row[x];

            double log_r = std::log(in[0] + 1e-6);
            double log_g = std::log(in[1] + 1e-6);
            double log_b = std::log(in[2] + 1e-6);

            double ar = std::exp(l0 + l1*log_r + l2*log_r*log_r + l3*log_r*log_r*log_r);
            double ag = std::exp(l0 + l1*log_g + l2*log_g*log_g + l3*log_g*log_g*log_g);
            double ab = std::exp(l0 + l1*log_b + l2*log_b*log_b + l3*log_b*log_b*log_b);

            double r = m00*ar + m01*ag + m02*ab;
            double g = m10*ar + m11*ag + m12*ab;
            double b = m20*ar + m21*ag + m22*ab;

            out_row[x] = cv::Vec3d(r, g, b);
        }
    }
    return out;
}

cv::Mat Solver::get_ccm() const {
    if (!result_) throw std::runtime_error("No calibration loaded.");
    return result_->color_matrix.clone();
}

std::vector<double> Solver::get_luma_params() const {
    if (!result_) throw std::runtime_error("No calibration loaded.");
    return result_->luma_params;
}

double Solver::get_reference_exposure() const {
    if (!result_) throw std::runtime_error("No calibration loaded.");
    return result_->reference_exposure;
}

double Solver::get_final_error() const {
    if (!result_) throw std::runtime_error("No calibration loaded.");
    return result_->final_error;
}

std::string Solver::get_report() const {
    if (!result_) throw std::runtime_error("No calibration loaded.");
    return result_->report;
}

bool Solver::converged() const {
    if (!result_) throw std::runtime_error("No calibration loaded.");
    return result_->converged;
}

void Solver::save(const std::string& filename) const {
    if (!result_) throw std::runtime_error("No calibration to save.");
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);
    if (!fs.isOpened()) throw std::runtime_error("Cannot open for writing: " + filename);
    fs << "luma_params" << result_->luma_params;
    fs << "color_matrix" << result_->color_matrix;
    fs << "final_error" << result_->final_error;
    fs << "reference_exposure" << result_->reference_exposure;
}

void Solver::load(const std::string& filename) {
    cv::FileStorage fs(filename, cv::FileStorage::READ);
    if (!fs.isOpened()) throw std::runtime_error("Cannot open: " + filename);
    auto luma_params = std::vector<double>();
    fs["luma_params"] >> luma_params;
    if (luma_params.size() != 4)
        throw std::runtime_error("Invalid calibration file " + filename +
                                 ": luma_params must have 4 entries, got " +
                                 std::to_string(luma_params.size()));
    cv::Mat color_matrix;
    fs["color_matrix"] >> color_matrix;
    if (color_matrix.empty() || color_matrix.rows != 3 || color_matrix.cols != 3)
        throw std::runtime_error("Invalid calibration file " + filename +
                                 ": color_matrix must be a 3x3 matrix");
    color_matrix.convertTo(color_matrix, CV_64FC3);
    if (fs["final_error"].type() != cv::FileNode::REAL ||
        fs["reference_exposure"].type() != cv::FileNode::REAL)
        throw std::runtime_error("Invalid calibration file " + filename +
                                 ": missing final_error or reference_exposure");

    result_ = std::make_unique<CalibrationResult>();
    result_->luma_params = luma_params;
    result_->color_matrix = color_matrix.clone();
    fs["final_error"] >> result_->final_error;
    fs["reference_exposure"] >> result_->reference_exposure;
}

} // namespace chromacal
