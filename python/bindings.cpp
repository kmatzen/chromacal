#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "chromacal/chromacal.h"

namespace py = pybind11;

// Convert numpy (H, W, 3) uint8 BGR array to cv::Mat
static cv::Mat numpy_to_mat_bgr(py::array_t<uint8_t> arr) {
    auto buf = arr.request();
    if (buf.ndim != 3 || buf.shape[2] != 3)
        throw std::runtime_error("Expected (H, W, 3) uint8 array");
    return cv::Mat(buf.shape[0], buf.shape[1], CV_8UC3, buf.ptr).clone();
}

// Convert numpy (H, W, 3) float64 RGB array to cv::Mat CV_64FC3
static cv::Mat numpy_to_mat_f64(py::array_t<double> arr) {
    auto buf = arr.request();
    if (buf.ndim != 3 || buf.shape[2] != 3)
        throw std::runtime_error("Expected (H, W, 3) float64 array");
    return cv::Mat(buf.shape[0], buf.shape[1], CV_64FC3, buf.ptr).clone();
}

// Convert cv::Mat CV_32FC3 to numpy (H, W, 3) float32
static py::array_t<float> mat_to_numpy_f32(const cv::Mat& mat) {
    cv::Mat f32;
    if (mat.type() != CV_32FC3) mat.convertTo(f32, CV_32F);
    else f32 = mat;

    auto result = py::array_t<float>({f32.rows, f32.cols, 3});
    auto buf = result.request();
    std::memcpy(buf.ptr, f32.data, f32.total() * f32.elemSize());
    return result;
}

// Convert cv::Mat CV_64FC3 to numpy (H, W, 3) float64
static py::array_t<double> mat_to_numpy_f64(const cv::Mat& mat) {
    auto result = py::array_t<double>({mat.rows, mat.cols, 3});
    auto buf = result.request();
    std::memcpy(buf.ptr, mat.data, mat.total() * mat.elemSize());
    return result;
}

PYBIND11_MODULE(_chromacal, m) {
    m.doc() = "chromacal — ColorChecker camera calibration";

    // -----------------------------------------------------------------------
    // Types
    // -----------------------------------------------------------------------
    py::enum_<chromacal::ChartType>(m, "ChartType")
        .value("Classic24", chromacal::ChartType::Classic24, "24-patch ColorChecker Classic (built-in reference).")
        .value("SG140", chromacal::ChartType::SG140, "140-patch ColorChecker SG (needs a user-supplied reference_lab).");

    py::class_<chromacal::NormalityTestResults>(m, "NormalityTestResults")
        .def_readonly("passes_mardia_skewness", &chromacal::NormalityTestResults::passes_mardia_skewness)
        .def_readonly("passes_mardia_kurtosis", &chromacal::NormalityTestResults::passes_mardia_kurtosis)
        .def_readonly("passes_henze_zirkler", &chromacal::NormalityTestResults::passes_henze_zirkler)
        .def_readonly("passes_shapiro_per_channel", &chromacal::NormalityTestResults::passes_shapiro_per_channel)
        .def_readonly("overall_passes", &chromacal::NormalityTestResults::overall_passes)
        .def_readonly("mardia_skewness_pvalue", &chromacal::NormalityTestResults::mardia_skewness_pvalue)
        .def_readonly("mardia_kurtosis_pvalue", &chromacal::NormalityTestResults::mardia_kurtosis_pvalue)
        .def_readonly("henze_zirkler_pvalue", &chromacal::NormalityTestResults::henze_zirkler_pvalue)
        .def_readonly("shapiro_pvalues", &chromacal::NormalityTestResults::shapiro_pvalues);

    py::class_<chromacal::PatchStatistics>(m, "PatchStatistics")
        .def_readonly("pixel_count", &chromacal::PatchStatistics::pixel_count)
        .def_readonly("exposure", &chromacal::PatchStatistics::exposure)
        .def_readonly("reliability", &chromacal::PatchStatistics::reliability)
        .def_readonly("normality_tests", &chromacal::PatchStatistics::normality_tests)
        .def_property_readonly("mean", [](const chromacal::PatchStatistics& p) {
            return std::vector<double>{p.mean[0], p.mean[1], p.mean[2]};
        })
        .def_property_readonly("reference_lab", [](const chromacal::PatchStatistics& p) {
            return std::vector<double>{p.reference_lab[0], p.reference_lab[1], p.reference_lab[2]};
        })
        .def_property_readonly("covariance", [](const chromacal::PatchStatistics& p) {
            auto result = py::array_t<double>({3, 3});
            auto buf = result.request();
            auto* out = static_cast<double*>(buf.ptr);
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    out[i * 3 + j] = p.covariance(i, j);
            return result;
        }, "3x3 RGB covariance matrix, as a numpy array.")
        .def_property_readonly("center", [](const chromacal::PatchStatistics& p) {
            return std::vector<double>{p.center[0], p.center[1]};
        }, "Patch center in normalized image coordinates [0,1], or [-1,-1] if unknown.");

    // -----------------------------------------------------------------------
    // Detect
    // -----------------------------------------------------------------------
    m.def("detect", [](py::array_t<uint8_t> image, double exposure,
                       float lower, float upper, chromacal::ChartType chart,
                       py::object reference_lab) {
        cv::Mat mat = numpy_to_mat_bgr(image);

        std::vector<cv::Vec3d> ref;
        const std::vector<cv::Vec3d>* ref_ptr = nullptr;
        if (!reference_lab.is_none()) {
            py::array_t<double, py::array::c_style> arr =
                py::array_t<double, py::array::c_style>::ensure(reference_lab);
            if (!arr || arr.ndim() != 2 || arr.shape(1) != 3)
                throw std::invalid_argument(
                    "reference_lab must be an (N, 3) array of D50 Lab triples, in patch order");
            auto r = arr.unchecked<2>();
            ref.reserve(static_cast<size_t>(arr.shape(0)));
            for (py::ssize_t i = 0; i < arr.shape(0); ++i)
                ref.emplace_back(r(i, 0), r(i, 1), r(i, 2));
            ref_ptr = &ref;
        }

        return chromacal::detect(mat, exposure, lower, upper, chart, ref_ptr);
    }, py::arg("image"), py::arg("exposure") = 1.0,
       py::arg("lower_threshold") = 0.01f, py::arg("upper_threshold") = 0.99f,
       py::arg("chart") = chromacal::ChartType::Classic24,
       py::arg("reference_lab") = py::none(),
       "Detect a ColorChecker and extract patch statistics from a BGR image.\n\n"
       "exposure is a relative exposure value (e.g. ISO * shutter_speed / 32) used "
       "to normalize patches captured at different exposures during solve(); leave "
       "at 1.0 if all images share the same exposure.\n\n"
       "chart selects ChartType.Classic24 (default, built-in 24-patch reference) or "
       "ChartType.SG140, which requires reference_lab -- an (N, 3) array of D50 Lab "
       "values in patch order, since X-Rite's SG reference data is licensed and not "
       "bundled.");

    m.def("filter_normal", &chromacal::filter_normal,
          "Filter patches to only those passing normality tests.");

    // -----------------------------------------------------------------------
    // Solver
    // -----------------------------------------------------------------------
    py::class_<chromacal::Solver>(m, "Solver")
        .def(py::init<>())
        .def("solve", &chromacal::Solver::solve,
             py::arg("patches"), py::arg("verbose") = false,
             "Fit the calibration model to detected patches.")
        .def("infer", [](const chromacal::Solver& s, py::array_t<double> image) {
            cv::Mat mat = numpy_to_mat_f64(image);
            cv::Mat result = s.infer(mat);
            return mat_to_numpy_f64(result);
        }, "Apply calibration to a float64 RGB image (H, W, 3).")
        .def("get_ccm", [](const chromacal::Solver& s) {
            cv::Mat ccm = s.get_ccm();
            auto result = py::array_t<double>({3, 3});
            auto buf = result.request();
            std::memcpy(buf.ptr, ccm.data, 9 * sizeof(double));
            return result;
        }, "Get the 3x3 color correction matrix as numpy array.")
        .def("get_luma_params", &chromacal::Solver::get_luma_params)
        .def("get_reference_exposure", &chromacal::Solver::get_reference_exposure)
        .def("get_final_error", &chromacal::Solver::get_final_error)
        .def("get_report", &chromacal::Solver::get_report,
             "Brief human-readable summary of the last solve() (no per-iteration noise).")
        .def("converged", &chromacal::Solver::converged,
             "Whether the last solve() converged to a usable solution.")
        .def("save", &chromacal::Solver::save)
        .def("load", &chromacal::Solver::load);

    // -----------------------------------------------------------------------
    // Apply (LUT)
    // -----------------------------------------------------------------------
    // We wrap the OCIO processor as an opaque Python object
    py::class_<OCIO::ConstCPUProcessorRcPtr>(m, "LUT")
        .def("__repr__", [](const OCIO::ConstCPUProcessorRcPtr&) {
            return "<chromacal.LUT>";
        });

    m.def("create_lut", [](const chromacal::Solver& solver, int size) {
        return chromacal::create_lut(solver, size);
    }, py::arg("solver"), py::arg("lut_size") = 129,
       "Build an OpenColorIO 3D LUT from a calibration.");

    m.def("write_cube", &chromacal::write_cube,
          py::arg("solver"), py::arg("path"), py::arg("lut_size") = 33,
          py::arg("title") = "chromacal",
          "Write the calibration as a Resolve/Iridas .cube 3D LUT file.");

    m.def("apply_lut", [](py::array_t<uint8_t> image, const OCIO::ConstCPUProcessorRcPtr& proc) {
        cv::Mat mat = numpy_to_mat_bgr(image);
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        cv::Mat result = chromacal::apply_lut(rgb, proc);
        return mat_to_numpy_f32(result);
    }, py::arg("image"), py::arg("lut"),
       "Apply a 3D LUT to a BGR uint8 image. Returns float32 RGB.");
}
