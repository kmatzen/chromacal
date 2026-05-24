# Third-party software

The self-contained chromacal plugin bundle (`Contents/Frameworks/`) redistributes
the following open-source libraries. Their licenses permit redistribution with
attribution; include this notice with any distributed build.

| Component | License | Use |
|---|---|---|
| OpenCV (core, imgproc, imgcodecs, mcc, calib3d, dnn, features2d, flann) | Apache-2.0 | ColorChecker detection + image I/O |
| Ceres Solver | BSD-3-Clause | calibration solver |
| Eigen | MPL-2.0 (headers) | linear algebra (used by Ceres; header-only) |
| Abseil | Apache-2.0 | Ceres dependency |
| glog, gflags | BSD-3-Clause | Ceres dependencies |
| SuiteSparse (cholmod, etc.) | LGPL-2.1+ / BSD (per module) | Ceres sparse linear algebra |
| OpenColorIO | BSD-3-Clause | LUT helpers (CLI/tests only — **not** bundled in the effect) |

chromacal itself is licensed under the repository's top-level `LICENSE`.

Full license texts ship with each upstream project; this table is a summary for
attribution. Verify the exact versions/licenses of the libraries your build links
(they come from your package manager, e.g. Homebrew/vcpkg) before distributing.
