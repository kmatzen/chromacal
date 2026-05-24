// Headless driver for the plugin core. Doubles as (a) a verification harness
// for solve_core (the smoke test), and (b) a standalone tool you can call
// from any pipeline: `chromacal_solve frame.png out.cube [lut_size]`.

#include "solve_core.h"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <frame.(png|tif|jpg)> <out.cube> [lut_size=33] [out.cmcal]\n";
        return 2;
    }
    int lut_size = (argc > 3) ? std::atoi(argv[3]) : 33;

    auto r = chromacal_ppro::solve_from_image(argv[1], argv[2], lut_size);
    if (!r.ok) {
        std::cerr << "ERROR: " << r.error << "\n";
        return 1;
    }

    std::cout << "patches_detected=" << r.patches_detected
              << " min_reliability=" << r.min_reliability
              << " final_error=" << r.final_error << "\n";
    std::cout << "ccm=[";
    for (int i = 0; i < 9; ++i) std::cout << r.ccm[i] << (i < 8 ? ", " : "");
    std::cout << "]\nluma=[";
    for (int i = 0; i < 4; ++i) std::cout << r.luma[i] << (i < 3 ? ", " : "");
    std::cout << "]\nwrote LUT: " << argv[2] << "\n";

    // Optional: also write a calibration preset the native effect's "Load
    // calibration" reads (the no-SDK path: full-res frame -> CLI -> Load).
    if (argc > 4) {
        if (chromacal_ppro::write_calibration(r.luma.data(), r.ccm.data(), argv[4]))
            std::cout << "wrote calibration preset: " << argv[4] << "\n";
        else
            std::cerr << "WARNING: could not write preset: " << argv[4] << "\n";
    }
    return 0;
}
