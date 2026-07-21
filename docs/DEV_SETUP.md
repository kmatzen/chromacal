# Local dev environment setup

This is the dependency list `.github/workflows/ci.yml` installs, written down
in one place instead of only living inside the workflow YAML.

## Dependencies

- C++17 compiler, CMake >= 3.14
- Eigen3
- [Ceres Solver](http://ceres-solver.org/) 2.2.0
- [OpenColorIO](https://opencolorio.org/) v2.5.1
- OpenCV 4.x **with the `mcc` contrib module** — see the note below on why
  this usually needs a from-source build on Linux.
- Catch2 (fetched automatically by CMake via `FetchContent` when
  `-DCHROMACAL_BUILD_TESTS=ON`; nothing to install for it)

### Why OpenCV needs to be built from source on Linux

`chromacal::detect()` uses `cv::mcc::CChecker::getColorCharts()`, which was
added in OpenCV **4.7**. Ubuntu's packaged `libopencv-dev` is 4.6 (as of the
versions CI targets), so it's too old — and the `mcc` module isn't part of
core OpenCV anyway (it lives in `opencv_contrib`). CI builds a trimmed
OpenCV+contrib (`-DBUILD_LIST=mcc,imgcodecs`, everything else off) from
source, pinned to a version that has `mcc` and matches what the macOS/
Homebrew build in `ci.yml` gets. **Don't "fix" this back to
`apt install libopencv-dev`** — it will silently build without ColorChecker
detection support.

### macOS

```bash
brew install eigen ceres-solver opencolorio opencv
```

Homebrew's OpenCV is recent enough to include `mcc`, so no from-source build
is needed there.

### Linux — fast path (prebuilt dependency prefix)

Building Ceres + OpenColorIO + OpenCV(+contrib) from source takes roughly
20 minutes. `.github/workflows/publish-devdeps.yml` builds that same prefix
(via `scripts/build_linux_devdeps.sh`) and publishes it as a downloadable
Release asset whenever the build script changes, so you can skip the build:

```bash
sudo apt-get update
sudo apt-get install -y cmake libeigen3-dev libgoogle-glog-dev libgflags-dev \
  libsuitesparse-dev libpng-dev libjpeg-dev libtiff-dev zlib1g-dev

curl -fsSL -o /tmp/chromacal-devdeps.tar.gz \
  https://github.com/kmatzen/chromacal/releases/download/devdeps-linux/chromacal-devdeps-linux-x86_64-ceres2.2.0-ocio2.5.1-opencv4.13.0.tar.gz
tar -C "$HOME" -xzf /tmp/chromacal-devdeps.tar.gz   # extracts to $HOME/cvdeps

export CMAKE_PREFIX_PATH="$HOME/cvdeps"
export LD_LIBRARY_PATH="$HOME/cvdeps/lib:$HOME/cvdeps/lib64:$LD_LIBRARY_PATH"
```

If that asset filename 404s (e.g. the pinned dependency versions moved and
the workflow hasn't republished yet), fall back to the slow path below, or
check the [Releases page](https://github.com/kmatzen/chromacal/releases) for
the current `devdeps-linux` asset name.

A Claude Code `SessionStart` hook (`.claude/hooks/session-start.sh`) does
this automatically — with a from-source fallback — for Claude Code on the
web sessions opened against this repo.

### Linux — slow path (build from source)

```bash
sudo apt-get update
sudo apt-get install -y cmake libeigen3-dev libgoogle-glog-dev libgflags-dev \
  libsuitesparse-dev libpng-dev libjpeg-dev libtiff-dev zlib1g-dev

./scripts/build_linux_devdeps.sh "$HOME/cvdeps"   # ~20 minutes

export CMAKE_PREFIX_PATH="$HOME/cvdeps"
export LD_LIBRARY_PATH="$HOME/cvdeps/lib:$HOME/cvdeps/lib64:$LD_LIBRARY_PATH"
```

## Configure, build, test

```bash
cmake -B build -DCHROMACAL_BUILD_TESTS=ON -DCHROMACAL_BUILD_EXAMPLES=ON -DCHROMACAL_BUILD_PPRO=ON
cmake --build build -j"$(nproc)"
cd build && ctest --output-on-failure
```

`-DCHROMACAL_BUILD_PPRO=ON` additionally builds the Premiere/AE plugin's
SDK-free core, CLIs, and its own test binaries (`chromacal_ppro_tests`,
`chromacal_ppro_format_tests`) — the Adobe-SDK-dependent native effect itself
only builds when `CHROMACAL_PRSDK_DIR`/`CHROMACAL_AESDK_DIR` are also set
(gated Adobe downloads; see `plugin/effect/README.md`), so it's skipped by
default.

### If you only need the plugin's calibration-preset / `.cube` format logic

`plugin/native/solve_format.{h,cpp}` (calibration presets, `.cube` baking,
reference-Lab parsing) has **no OpenCV/Ceres/OpenColorIO dependency at all**
— see the `chromacal_ppro_format_tests` target in `plugin/CMakeLists.txt`.
That target links only the standard library + Catch2, so it builds and runs
with just `cmake` and a C++17 compiler — no native dependency setup above
required:

```bash
cmake -B build -DCHROMACAL_BUILD_TESTS=ON -DCHROMACAL_BUILD_PPRO=ON
cmake --build build --target chromacal_ppro_format_tests
./build/plugin/chromacal_ppro_format_tests
```

(This still needs `-DCHROMACAL_BUILD_TESTS=ON -DCHROMACAL_BUILD_PPRO=ON` to
configure, since the top-level `CMakeLists.txt` only defines these targets
under those options — but the *dependencies* it actually links are free.)
