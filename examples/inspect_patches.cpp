/// Inspect ColorChecker detection without running the solver.
///
/// Usage: inspect_patches <image.jpg>
///
/// Prints per-patch statistics: mean RGB, pixel count, and normality
/// test results. Useful for debugging detection quality.

#include <chromacal/chromacal.h>

#include <iomanip>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <image.jpg>" << std::endl;
        return 1;
    }

    cv::Mat image = cv::imread(argv[1]);
    if (image.empty()) {
        std::cerr << "Failed to load: " << argv[1] << std::endl;
        return 1;
    }

    auto patches = chromacal::detect(image);
    if (patches.empty()) {
        std::cerr << "No ColorChecker detected." << std::endl;
        return 1;
    }

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "Detected " << patches.size() << " patches\n" << std::endl;
    std::cout << std::setw(5) << "Patch"
              << std::setw(10) << "R"
              << std::setw(10) << "G"
              << std::setw(10) << "B"
              << std::setw(8) << "Pixels"
              << std::setw(12) << "Reliab."
              << std::setw(10) << "Normal?"
              << std::setw(12) << "SW p-val"
              << std::setw(12) << "HZ p-val"
              << std::endl;
    std::cout << std::string(89, '-') << std::endl;

    int normal_count = 0;
    for (size_t i = 0; i < patches.size(); ++i) {
        const auto& p = patches[i];
        const auto& nt = p.normality_tests;

        std::cout << std::setw(5) << i
                  << std::setw(10) << p.mean[0]
                  << std::setw(10) << p.mean[1]
                  << std::setw(10) << p.mean[2]
                  << std::setw(8) << p.pixel_count
                  << std::setw(12) << p.reliability
                  << std::setw(10) << (nt.overall_passes ? "PASS" : "FAIL");

        if (!nt.shapiro_pvalues.empty()) {
            double min_sw = *std::min_element(nt.shapiro_pvalues.begin(), nt.shapiro_pvalues.end());
            std::cout << std::setw(12) << min_sw;
        } else {
            std::cout << std::setw(12) << "-";
        }

        std::cout << std::setw(12) << nt.henze_zirkler_pvalue
                  << std::endl;

        if (nt.overall_passes) ++normal_count;
    }

    std::cout << "\n" << normal_count << " / " << patches.size()
              << " patches pass normality tests" << std::endl;

    return 0;
}
