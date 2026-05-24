// Generate the *Lumetri* .cube from a calibration preset — the same
// display-referred cube the effect's "Export .cube for Lumetri" writes
// (write_display_cube: bakes the sRGB<->working transcode that Premiere's
// sRGB-managed Input LUT wraps around the LUT). Use it to test the Lumetri path
// reproducibly without the effect's save dialog.
//
//   chromacal_cube <preset.cmcal> <out.cube> [gamma=2.4]
//
// gamma must match the sequence's SDR gamma (2.4 for Rec.709; 2.2/1.96 if set).

#include "solve_core.h"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <preset.cmcal> <out.cube> [gamma=2.4] [--srgb]\n"
                  << "  default: the effect's exact transform (matches Lumetri's Input LUT,\n"
                  << "           which applies directly — measured ~0.5% to the effect)\n"
                  << "  --srgb : legacy sRGB-managed bake (only if your Premiere color-manages\n"
                  << "           the Input LUT; otherwise it runs ~6% hot)\n";
        return 2;
    }
    double luma[4], ccm[9];
    if (!chromacal_ppro::read_calibration(argv[1], luma, ccm)) {
        std::cerr << "ERROR: could not read calibration preset: " << argv[1] << "\n";
        return 1;
    }
    double gamma = 2.4;
    bool srgb = false;
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--srgb") srgb = true;
        else gamma = std::atof(a.c_str());
    }
    bool ok = srgb ? chromacal_ppro::write_display_cube(luma, ccm, argv[2], 33, gamma)
                   : chromacal_ppro::write_effect_cube(luma, ccm, argv[2], 33, gamma);
    if (!ok) {
        std::cerr << "ERROR: could not write cube: " << argv[2] << "\n";
        return 1;
    }
    std::cout << "wrote " << (srgb ? "sRGB-managed (legacy)" : "effect-exact") << " .cube "
              << argv[2] << " (gamma " << gamma << ")\n";
    return 0;
}
