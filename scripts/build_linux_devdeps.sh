#!/usr/bin/env bash
# Build chromacal's from-source Linux native dependencies (Ceres, OpenColorIO,
# OpenCV+contrib with mcc) into a single install prefix.
#
# Extracted out of .github/workflows/ci.yml so the exact same build is
# reusable from:
#   - ci.yml itself (still cached per-run via actions/cache)
#   - .github/workflows/publish-devdeps.yml, which tars up the result and
#     publishes it as a GitHub Release asset, so local/cloud dev sessions
#     can download a prebuilt prefix instead of paying this ~20 minute
#     from-source build (see docs/DEV_SETUP.md).
#
# Usage: build_linux_devdeps.sh [install-prefix]   (default: $HOME/cvdeps)
#
# Ubuntu's packaged libopencv-dev is 4.6 — too old for
# cv::mcc::CChecker::getColorCharts() (added in 4.7) — so OpenCV is built
# from source here. Keep the pinned versions below in sync with the cache
# key in ci.yml and the tag published by publish-devdeps.yml.

set -euo pipefail

CERES_VERSION="2.2.0"
OCIO_VERSION="v2.5.1"
OPENCV_VERSION="4.13.0"

PREFIX="${1:-$HOME/cvdeps}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

if [ -e "$PREFIX/.complete" ]; then
    echo "build_linux_devdeps: $PREFIX already complete, skipping."
    exit 0
fi

# Ceres Solver
git clone --depth 1 --branch "$CERES_VERSION" https://github.com/ceres-solver/ceres-solver.git "$WORK/ceres"
cmake -S "$WORK/ceres" -B "$WORK/ceres/build" -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$WORK/ceres/build" -j"$(nproc)" && cmake --install "$WORK/ceres/build"

# OpenColorIO
git clone --depth 1 --branch "$OCIO_VERSION" https://github.com/AcademySoftwareFoundation/OpenColorIO.git "$WORK/ocio"
cmake -S "$WORK/ocio" -B "$WORK/ocio/build" -DCMAKE_BUILD_TYPE=Release \
    -DOCIO_BUILD_APPS=OFF -DOCIO_BUILD_TESTS=OFF -DOCIO_BUILD_GPU_TESTS=OFF \
    -DOCIO_BUILD_PYTHON=OFF -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$WORK/ocio/build" -j"$(nproc)" && cmake --install "$WORK/ocio/build"

# OpenCV + contrib (mcc) — pinned to match the macOS/Homebrew build, so
# getColorCharts() is present. Only the modules chromacal actually uses.
git clone --depth 1 --branch "$OPENCV_VERSION" https://github.com/opencv/opencv.git "$WORK/opencv"
git clone --depth 1 --branch "$OPENCV_VERSION" https://github.com/opencv/opencv_contrib.git "$WORK/opencv_contrib"
cmake -S "$WORK/opencv" -B "$WORK/opencv/build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DOPENCV_EXTRA_MODULES_PATH="$WORK/opencv_contrib/modules" \
    -DBUILD_LIST=mcc,imgcodecs \
    -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF \
    -DBUILD_opencv_apps=OFF -DBUILD_DOCS=OFF \
    -DWITH_FFMPEG=OFF -DWITH_GSTREAMER=OFF -DWITH_GTK=OFF -DWITH_V4L=OFF \
    -DWITH_1394=OFF -DWITH_OPENEXR=OFF -DWITH_TBB=OFF
cmake --build "$WORK/opencv/build" -j"$(nproc)" && cmake --install "$WORK/opencv/build"

touch "$PREFIX/.complete"
echo "build_linux_devdeps: installed to $PREFIX"
