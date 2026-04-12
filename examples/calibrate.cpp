/// Calibrate a camera from a single image containing a ColorChecker.
///
/// Usage: calibrate <image> [output.yml]
///
/// Detects the chart, filters patches, solves for tone curve + CCM,
/// saves calibration to YAML, and writes a corrected preview image.

#include <chromacal/chromacal.h>

#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <image.jpg> [calibration.yml]" << std::endl;
        return 1;
    }

    std::string image_path = argv[1];
    std::string output_path = (argc > 2) ? argv[2] : "calibration.yml";

    // Load image
    cv::Mat image = cv::imread(image_path);
    if (image.empty()) {
        std::cerr << "Failed to load: " << image_path << std::endl;
        return 1;
    }

    // 1. Detect
    std::cout << "Detecting ColorChecker..." << std::endl;
    auto patches = chromacal::detect(image);
    if (patches.empty()) {
        std::cerr << "No ColorChecker detected in image." << std::endl;
        return 1;
    }
    std::cout << "  Found " << patches.size() << " patches" << std::endl;

    // Filter by normality
    auto filtered = chromacal::filter_normal(patches);
    std::cout << "  " << filtered.size() << " pass normality tests" << std::endl;
    if (filtered.size() < 10) {
        std::cerr << "Too few valid patches (need >= 10)." << std::endl;
        return 1;
    }

    // 2. Solve
    std::cout << "Solving calibration..." << std::endl;
    chromacal::Solver solver;
    solver.solve(filtered);

    auto ccm = solver.get_ccm();
    auto luma = solver.get_luma_params();
    std::cout << "  CCM diagonal: ["
              << ccm.at<double>(0, 0) << ", "
              << ccm.at<double>(1, 1) << ", "
              << ccm.at<double>(2, 2) << "]" << std::endl;
    std::cout << "  Luma params: [" << luma[0] << ", " << luma[1]
              << ", " << luma[2] << ", " << luma[3] << "]" << std::endl;
    std::cout << "  Final error: " << solver.get_final_error() << std::endl;

    solver.save(output_path);
    std::cout << "  Saved to " << output_path << std::endl;

    // 3. Apply — generate a preview
    std::cout << "Generating corrected preview..." << std::endl;
    auto lut = chromacal::create_lut(solver, 65); // smaller for preview speed

    cv::Mat rgb;
    cv::cvtColor(image, rgb, cv::COLOR_BGR2RGB);
    cv::Mat corrected = chromacal::apply_lut(rgb, lut);

    // Convert back to 8-bit BGR for saving
    corrected = cv::min(corrected, 1.0f);
    corrected = cv::max(corrected, 0.0f);
    corrected.convertTo(corrected, CV_8U, 255.0);
    cv::cvtColor(corrected, corrected, cv::COLOR_RGB2BGR);

    std::string preview_path = image_path.substr(0, image_path.rfind('.')) + "_corrected.jpg";
    cv::imwrite(preview_path, corrected);
    std::cout << "  Preview saved to " << preview_path << std::endl;

    return 0;
}
