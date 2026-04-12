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
    py::class_<chromacal::NormalityTestResults>(m, "NormalityTestResults")
        .def_readonly("passes_mardia_skewness", &chromacal::NormalityTestResults::passes_mardia_skewness)
        .def_readonly("passes_mardia_kurtosis", &chromacal::NormalityTestResults::passes_mardia_kurtosis)
        .def_readonly("passes_henze_zirkler", &chromacal::NormalityTestResults::passes_henze_zirkler)
        .def_readonly("passes_shapiro_per_channel", &chromacal::NormalityTestResults::passes_shapiro_per_channel)
        .def_readonly("overall_passes", &chromacal::NormalityTestResults::overall_passes);

    py::class_<chromacal::PatchStatistics>(m, "PatchStatistics")
        .def_readonly("pixel_count", &chromacal::PatchStatistics::pixel_count)
        .def_readonly("exposure", &chromacal::PatchStatistics::exposure)
        .def_readonly("normality_tests", &chromacal::PatchStatistics::normality_tests)
        .def_property_readonly("mean", [](const chromacal::PatchStatistics& p) {
            return std::vector<double>{p.mean[0], p.mean[1], p.mean[2]};
        })
        .def_property_readonly("reference_lab", [](const chromacal::PatchStatistics& p) {
            return std::vector<double>{p.reference_lab[0], p.reference_lab[1], p.reference_lab[2]};
        });

    // -----------------------------------------------------------------------
    // Detect
    // -----------------------------------------------------------------------
    m.def("detect", [](py::array_t<uint8_t> image, double exposure,
                       float lower, float upper) {
        cv::Mat mat = numpy_to_mat_bgr(image);
        return chromacal::detect(mat, exposure, lower, upper);
    }, py::arg("image"), py::arg("exposure") = 1.0,
       py::arg("lower_threshold") = 0.01f, py::arg("upper_threshold") = 0.99f,
       "Detect a ColorChecker and extract patch statistics from a BGR image.");

    m.def("filter_normal", &chromacal::filter_normal,
          "Filter patches to only those passing normality tests.");

    // -----------------------------------------------------------------------
    // Solver
    // -----------------------------------------------------------------------
    py::class_<chromacal::Solver>(m, "Solver")
        .def(py::init<>())
        .def("solve", &chromacal::Solver::solve,
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

    m.def("apply_lut", [](py::array_t<uint8_t> image, const OCIO::ConstCPUProcessorRcPtr& proc) {
        cv::Mat mat = numpy_to_mat_bgr(image);
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        cv::Mat result = chromacal::apply_lut(rgb, proc);
        return mat_to_numpy_f32(result);
    }, py::arg("image"), py::arg("lut"),
       "Apply a 3D LUT to a BGR uint8 image. Returns float32 RGB.");
}
