// Apply a chromacal calibration preset to an image, using the native effect's
// exact transform. Generates the headless reference "after" frame for the
// headless<->Premiere parity check:
//   chromacal_apply <frame.(png|tif|jpg)> <preset.cmcal> <out.png> [gamma=2.4]

#include "solve_core.h"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <frame.(png|tif|jpg)> <preset.cmcal> <out.png> [gamma=2.4]\n";
        return 2;
    }
    double luma[4], ccm[9];
    if (!chromacal_ppro::read_calibration(argv[2], luma, ccm)) {
        std::cerr << "ERROR: could not read calibration preset: " << argv[2] << "\n";
        return 1;
    }
    double gamma = (argc > 4) ? std::atof(argv[4]) : 2.4;
    if (!chromacal_ppro::apply_calibration_to_image(argv[1], argv[3], luma, ccm, gamma)) {
        std::cerr << "ERROR: apply failed (read " << argv[1] << " / write " << argv[3] << ")\n";
        return 1;
    }
    std::cout << "wrote " << argv[3] << " (gamma " << gamma << ")\n";
    return 0;
}
