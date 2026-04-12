/// Apply a saved calibration to one or more images.
///
/// Usage: apply_calibration <calibration.yml> <image1.jpg> [image2.jpg ...]
///
/// Loads a pre-computed calibration, builds a 3D LUT once, then applies
/// it to each input image. Corrected images are saved as *_corrected.jpg.

#include <chromacal/chromacal.h>

#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <calibration.yml> <image1.jpg> [image2.jpg ...]"
                  << std::endl;
        return 1;
    }

    // Load calibration
    chromacal::Solver solver;
    solver.load(argv[1]);
    std::cout << "Loaded calibration from " << argv[1] << std::endl;

    // Build LUT once (this is the expensive step)
    std::cout << "Building 3D LUT (129^3)..." << std::endl;
    auto lut = chromacal::create_lut(solver);

    // Apply to each image
    for (int i = 2; i < argc; ++i) {
        std::string path = argv[i];
        cv::Mat image = cv::imread(path);
        if (image.empty()) {
            std::cerr << "Skipping (cannot load): " << path << std::endl;
            continue;
        }

        cv::Mat rgb;
        cv::cvtColor(image, rgb, cv::COLOR_BGR2RGB);
        cv::Mat corrected = chromacal::apply_lut(rgb, lut);

        corrected = cv::min(corrected, 1.0f);
        corrected = cv::max(corrected, 0.0f);
        corrected.convertTo(corrected, CV_8U, 255.0);
        cv::cvtColor(corrected, corrected, cv::COLOR_RGB2BGR);

        std::string out = path.substr(0, path.rfind('.')) + "_corrected.jpg";
        cv::imwrite(out, corrected);
        std::cout << path << " -> " << out << std::endl;
    }

    return 0;
}
