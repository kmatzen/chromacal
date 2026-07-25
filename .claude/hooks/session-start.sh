#!/bin/bash
# Claude Code SessionStart hook: provisions chromacal's native build
# dependencies (Ceres, OpenColorIO, OpenCV+contrib/mcc, Eigen) so
# `cmake -DCHROMACAL_BUILD_TESTS=ON` and `ctest` work inside a Claude Code on
# the web session, without every session paying the ~20 minute from-source
# build cost ci.yml pays on a cache miss.
#
# Fast path: download the prebuilt prefix published by
# .github/workflows/publish-devdeps.yml (see docs/DEV_SETUP.md). Falls back
# to building from source (scripts/build_linux_devdeps.sh) if the asset
# isn't published yet or the download fails for any reason. Linux only —
# no-ops elsewhere (this hook targets the remote/web container).
#
# Async: the from-source fallback can take ~20 minutes, so this reports
# async immediately and does its work in the background rather than
# blocking session start on a slow build.

set -uo pipefail

echo '{"async": true, "asyncTimeout": 1500000}'

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
    exit 0
fi

if ! command -v apt-get >/dev/null 2>&1; then
    exit 0 # not a Debian/Ubuntu-based container; nothing this hook knows how to do
fi

REPO_DIR="${CLAUDE_PROJECT_DIR:-$(pwd)}"
PREFIX="$HOME/cvdeps"
ENV_FILE="${CLAUDE_ENV_FILE:-/dev/null}"

write_env() {
    {
        echo "export CMAKE_PREFIX_PATH=\"$PREFIX\${CMAKE_PREFIX_PATH:+:\$CMAKE_PREFIX_PATH}\""
        echo "export LD_LIBRARY_PATH=\"$PREFIX/lib:$PREFIX/lib64\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}\""
    } >>"$ENV_FILE"
}

# Already provisioned (e.g. a resumed session on a cached container).
if [ -e "$PREFIX/.complete" ]; then
    write_env
    exit 0
fi

# apt packages chromacal's CMake configure needs regardless of how
# Ceres/OpenColorIO/OpenCV get installed (mirrors ci.yml's Linux step).
sudo apt-get update -y || true
sudo apt-get install -y cmake libeigen3-dev libgoogle-glog-dev libgflags-dev \
    libsuitesparse-dev libpng-dev libjpeg-dev libtiff-dev zlib1g-dev || true

# Fast path: the prebuilt tarball publish-devdeps.yml publishes.
ASSET="chromacal-devdeps-linux-x86_64-ceres2.2.0-ocio2.5.1-opencv4.13.0.tar.gz"
URL="https://github.com/kmatzen/chromacal/releases/download/devdeps-linux/$ASSET"
TARBALL="$(mktemp -u /tmp/chromacal-devdeps-XXXXXX.tar.gz)"

if curl -fsSL "$URL" -o "$TARBALL" 2>/dev/null && [ -s "$TARBALL" ]; then
    mkdir -p "$PREFIX"
    tar -C "$HOME" -xzf "$TARBALL"
    rm -f "$TARBALL"
elif [ -x "$REPO_DIR/scripts/build_linux_devdeps.sh" ]; then
    # No published asset yet (e.g. before the first publish-devdeps.yml run
    # on this repo) — fall back to the from-source build it also uses.
    "$REPO_DIR/scripts/build_linux_devdeps.sh" "$PREFIX" || true
fi

if [ -e "$PREFIX/.complete" ]; then
    write_env
fi
