// Compare two images channel-wise — the comparator for the headless<->Premiere
// parity check (headless apply vs the frame exported after applying the effect):
//   chromacal_diff <a.png> <b.png> [max_tolerance]
// Prints "mean=… max=…" (in [0,1]); exits non-zero if a tolerance is given and
// the max difference exceeds it.

#include "solve_core.h"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <a.png> <b.png> [max_tolerance]\n";
        return 2;
    }
    double mean = 0, mx = 0;
    if (!chromacal_ppro::image_diff(argv[1], argv[2], &mean, &mx)) {
        std::cerr << "ERROR: could not read images or sizes differ.\n";
        return 1;
    }
    std::cout << "mean=" << mean << " max=" << mx << "\n";
    if (argc > 3) {
        double tol = std::atof(argv[3]);
        if (mx > tol) {
            std::cerr << "FAIL: max " << mx << " > tolerance " << tol << "\n";
            return 1;
        }
        std::cout << "OK: within tolerance " << tol << "\n";
    }
    return 0;
}
