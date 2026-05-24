/// Inspect ColorChecker detection without running the solver.
///
/// Usage: inspect_patches <image.(png|jpg|tif)> [annotated_out.png]
///
/// Prints per-patch statistics (mean RGB, pixel count, normality, reliability),
/// and — if an output path is given — writes an annotated image showing exactly
/// where the pipeline detected each patch (the same centers the effect overlays):
/// a crosshair + index at each patch center, a filled disc of the *sampled* color,
/// and a ring colored by reliability (green = clean, red = unreliable). Use it to
/// SEE the detected target and verify the grid lands on the chart.

#include <chromacal/chromacal.h>

#include <algorithm>
#include <iomanip>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <image.(png|jpg|tif)> [annotated_out.png]"
                  << std::endl;
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

    // Optional: write an annotated image of the detected target.
    if (argc >= 3) {
        cv::Mat vis = image.clone();
        for (size_t i = 0; i < patches.size(); ++i) {
            const auto& p = patches[i];
            if (p.center[0] < 0 || p.center[1] < 0) continue; // unknown center
            int cx = static_cast<int>(p.center[0] * image.cols);
            int cy = static_cast<int>(p.center[1] * image.rows);
            // Sampled patch color (mean is RGB [0,1]) drawn as a filled disc.
            cv::Scalar bgr(p.mean[2] * 255.0, p.mean[1] * 255.0, p.mean[0] * 255.0);
            cv::circle(vis, {cx, cy}, 9, bgr, cv::FILLED);
            // Ring colored by reliability: green (clean) -> red (unreliable).
            double rel = std::max(0.0, std::min(1.0, p.reliability));
            cv::Scalar ring(0.0, 255.0 * rel, 255.0 * (1.0 - rel)); // BGR
            cv::circle(vis, {cx, cy}, 9, ring, 2);
            cv::drawMarker(vis, {cx, cy}, {255, 0, 255}, cv::MARKER_CROSS, 14, 1);
            cv::putText(vis, std::to_string(i), {cx + 11, cy - 6},
                        cv::FONT_HERSHEY_SIMPLEX, 0.4, {0, 0, 0}, 3);
            cv::putText(vis, std::to_string(i), {cx + 11, cy - 6},
                        cv::FONT_HERSHEY_SIMPLEX, 0.4, {255, 255, 255}, 1);
        }
        if (cv::imwrite(argv[2], vis))
            std::cout << "wrote annotated detection -> " << argv[2] << std::endl;
        else
            std::cerr << "WARNING: could not write " << argv[2] << std::endl;

        // Also write a zoomed crop to the chart region — the chart is often small
        // in frame, so a crop makes the detection actually visible.
        double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
        for (const auto& p : patches) {
            if (p.center[0] < 0) continue;
            minx = std::min(minx, p.center[0] * image.cols);
            maxx = std::max(maxx, p.center[0] * image.cols);
            miny = std::min(miny, p.center[1] * image.rows);
            maxy = std::max(maxy, p.center[1] * image.rows);
        }
        if (maxx > minx && maxy > miny) {
            int pad = 30;
            int x0 = std::max(0, static_cast<int>(minx) - pad);
            int y0 = std::max(0, static_cast<int>(miny) - pad);
            int x1 = std::min(image.cols, static_cast<int>(maxx) + pad);
            int y1 = std::min(image.rows, static_cast<int>(maxy) + pad);
            cv::Mat crop = vis(cv::Rect(x0, y0, x1 - x0, y1 - y0)).clone();
            double scale = std::max(1.0, 800.0 / std::max(1, crop.cols)); // upscale small charts
            cv::Mat big;
            cv::resize(crop, big, {}, scale, scale, cv::INTER_NEAREST);
            std::string zp = argv[2];
            auto dot = zp.find_last_of('.');
            zp = (dot == std::string::npos ? zp : zp.substr(0, dot)) + "_zoom.png";
            if (cv::imwrite(zp, big))
                std::cout << "wrote zoomed crop      -> " << zp
                          << " (chart ~" << static_cast<int>(maxx - minx) << "px wide)" << std::endl;
        }
    }

    return 0;
}
