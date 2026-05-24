// Apply an Iridas/Resolve .cube 3D LUT to an image via trilinear interpolation —
// the same way an NLE applies an Input LUT. Lets the cube<->effect match be tested
// fully headlessly (apply the cube, compare to chromacal_apply): no Premiere.
//
//   chromacal_lutapply <cube> <in.(png|tif|jpg)> <out.png>

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "Usage: %s <cube> <in.(png|tif|jpg)> <out.png>\n", argv[0]);
        return 2;
    }
    std::ifstream f(argv[1]);
    if (!f) { std::fprintf(stderr, "ERROR: cannot open cube: %s\n", argv[1]); return 1; }

    int N = 0;
    std::vector<cv::Vec3f> lut;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("LUT_3D_SIZE", 0) == 0) {
            std::istringstream s(line);
            std::string tok; s >> tok >> N;
            continue;
        }
        double r, g, b;
        std::istringstream s(line);
        if (s >> r >> g >> b) lut.push_back(cv::Vec3f((float)r, (float)g, (float)b));
    }
    if (N < 2 || static_cast<int>(lut.size()) != N * N * N) {
        std::fprintf(stderr, "ERROR: bad cube (N=%d, entries=%zu)\n", N, lut.size());
        return 1;
    }
    auto at = [&](int r, int g, int b) { return lut[(size_t)b * N * N + (size_t)g * N + r]; };

    cv::Mat img = cv::imread(argv[2]); // BGR 8-bit
    if (img.empty()) { std::fprintf(stderr, "ERROR: cannot read image: %s\n", argv[2]); return 1; }

    cv::Mat out(img.size(), CV_8UC3);
    for (int y = 0; y < img.rows; ++y)
        for (int x = 0; x < img.cols; ++x) {
            cv::Vec3b p = img.at<cv::Vec3b>(y, x);
            double in[3] = {p[2] / 255.0, p[1] / 255.0, p[0] / 255.0}; // R, G, B
            int i0[3]; double w[3];
            for (int c = 0; c < 3; ++c) {
                double t = std::min(1.0, std::max(0.0, in[c])) * (N - 1);
                i0[c] = std::min(N - 2, static_cast<int>(t));
                w[c] = t - i0[c];
            }
            cv::Vec3f acc(0, 0, 0);
            for (int dr = 0; dr < 2; ++dr)
                for (int dg = 0; dg < 2; ++dg)
                    for (int db = 0; db < 2; ++db) {
                        double wt = (dr ? w[0] : 1 - w[0]) * (dg ? w[1] : 1 - w[1]) *
                                    (db ? w[2] : 1 - w[2]);
                        acc += static_cast<float>(wt) * at(i0[0] + dr, i0[1] + dg, i0[2] + db);
                    }
            auto q = [](float c) {
                int i = static_cast<int>(c * 255 + 0.5f);
                return static_cast<uchar>(i < 0 ? 0 : (i > 255 ? 255 : i));
            };
            out.at<cv::Vec3b>(y, x) = cv::Vec3b(q(acc[2]), q(acc[1]), q(acc[0])); // BGR
        }
    if (!cv::imwrite(argv[3], out)) { std::fprintf(stderr, "ERROR: cannot write %s\n", argv[3]); return 1; }
    std::printf("applied %d^3 cube -> %s\n", N, argv[3]);
    return 0;
}
