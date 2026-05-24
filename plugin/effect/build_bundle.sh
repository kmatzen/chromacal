#!/usr/bin/env bash
# Build the chromacal native Premiere/AE effect into a loadable .plugin bundle.
#
# Requires BOTH SDKs:
#   PRSDK_DIR : Premiere Pro C++ SDK   .../Examples/Headers
#   AESDK_DIR : After Effects SDK      .../Examples/Headers
# (the AE SDK also supplies Examples/Util and Examples/Resources alongside.)
#
# Usage:
#   PRSDK_DIR=".../Premiere Pro 26.0 C++ SDK/Examples/Headers" \
#   AESDK_DIR=".../AfterEffects SDK/Examples/Headers" \
#   plugin/effect/build_bundle.sh
#
# Output: build/plugin/chromacal.plugin  (install to
#   /Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/)
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
: "${PRSDK_DIR:?set PRSDK_DIR to the Premiere C++ SDK Examples/Headers}"
: "${AESDK_DIR:?set AESDK_DIR to the After Effects SDK Examples/Headers}"
AEU="${AESDK_DIR}/../Util"
AER="${AESDK_DIR}/../Resources"

# 1. Compile + link the Mach-O against both SDKs.
#    CHROMACAL_UNIVERSAL=1 builds an arm64+x86_64 fat binary — but this only links
#    if your OpenCV/Ceres/OCIO (and their deps) are ALSO universal; Homebrew's are
#    arm64-only, so you need universal builds of those (e.g. via vcpkg or a CI).
archflag=""
[ "${CHROMACAL_UNIVERSAL:-0}" = "1" ] && archflag="-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
gpuflag=""
[ "${CHROMACAL_GPU:-0}" = "1" ] && gpuflag="-DCHROMACAL_GPU=ON"
cmake -B "$repo/build" -DCHROMACAL_BUILD_PPRO=ON ${archflag:+"$archflag"} ${gpuflag:+"$gpuflag"} \
      -DCHROMACAL_PRSDK_DIR="$PRSDK_DIR" -DCHROMACAL_AESDK_DIR="$AESDK_DIR" >/dev/null
cmake --build "$repo/build" --target chromacal_effect

macho="$repo/build/plugin/chromacal.bundle"
bundle="$repo/build/plugin/chromacal.plugin"

# 2. Assemble the CFBundle layout.
rm -rf "$bundle"
mkdir -p "$bundle/Contents/MacOS"
cp "$here/Info.plist" "$bundle/Contents/Info.plist"
printf 'eFKTFXTC' > "$bundle/Contents/PkgInfo"
cp "$macho" "$bundle/Contents/MacOS/chromacal"
mkdir -p "$bundle/Contents/Resources"

# 3. Compile the PiPL to a FLAT resource file at Contents/Resources/<exe>.rsrc.
#    Premiere/AE read the PiPL from this file (named after CFBundleExecutable),
#    NOT from the executable's resource fork. -useDF writes to the data fork.
arch="$(uname -m)"; [ "$arch" = "x86_64" ] && rezarch=x86_64 || rezarch=arm64
xcrun Rez -arch "$rezarch" -d AE_OS_MAC \
    -i "$AESDK_DIR" -i "$AER" -i "$PRSDK_DIR" \
    "$here/chromacal_effect.r" -o "$bundle/Contents/Resources/chromacal.rsrc" -useDF

# 3b. (optional) Compile + bundle the Metal kernel for the GPU render path.
if [ "${CHROMACAL_GPU:-0}" = "1" ]; then
    xcrun -sdk macosx metal -c "$here/chromacal_kernel.metal" -o "$repo/build/chromacal_kernel.air"
    xcrun -sdk macosx metallib "$repo/build/chromacal_kernel.air" \
        -o "$bundle/Contents/Resources/chromacal.metallib"
    echo "GPU: bundled chromacal.metallib"
fi

# 4. (optional) Self-contained bundle so the plugin runs on machines without
#    Homebrew. Set CHROMACAL_SELF_CONTAINED=1. The PiPL lives in
#    Contents/Resources/chromacal.rsrc (a flat file), so re-signing the dylibs
#    and the executable can't strip it — unlike the old resource-fork approach.
if [ "${CHROMACAL_SELF_CONTAINED:-0}" = "1" ]; then
    command -v dylibbundler >/dev/null \
        || { echo "need dylibbundler (brew install dylibbundler)"; exit 1; }
    mkdir -p "$bundle/Contents/Frameworks"
    # Give dylibbundler explicit search paths for our deps — a from-source prefix
    # (e.g. the lean OpenCV via CMAKE_PREFIX_PATH) and Homebrew — and feed it
    # /dev/null on stdin so an unresolved dependency FAILS fast instead of blocking
    # forever on dylibbundler's interactive "Where is <lib>?" prompt (no TTY in CI).
    dbsearch=()
    if [ -n "${CMAKE_PREFIX_PATH:-}" ]; then
        IFS=':' read -ra _pfx <<< "$CMAKE_PREFIX_PATH"
        for p in "${_pfx[@]}"; do [ -d "$p/lib" ] && dbsearch+=(-s "$p/lib"); done
    fi
    if brewlib="$(brew --prefix 2>/dev/null)/lib" && [ -d "$brewlib" ]; then
        dbsearch+=(-s "$brewlib")
    fi
    dylibbundler -of -b -x "$bundle/Contents/MacOS/chromacal" \
        -d "$bundle/Contents/Frameworks" -p "@loader_path/../Frameworks/" \
        ${dbsearch[@]+"${dbsearch[@]}"} </dev/null >/dev/null

    # Newer dyld refuses to load a dylib that has duplicate LC_RPATH commands
    # (dylibbundler can add @loader_path/../Frameworks/ to a lib that already has
    # it). Collapse any duplicates to one — this was the silent load failure.
    for lib in "$bundle/Contents/Frameworks/"*.dylib; do
        while [ "$(otool -l "$lib" | grep -c 'path @loader_path/../Frameworks')" -gt 1 ]; do
            install_name_tool -delete_rpath "@loader_path/../Frameworks/" "$lib" 2>/dev/null || break
        done
    done

    # install_name_tool invalidates arm64 code signatures; re-sign ad-hoc.
    while IFS= read -r f; do codesign --force -s - "$f" 2>/dev/null || true; done < <(
        find "$bundle/Contents/Frameworks" -name '*.dylib'
        echo "$bundle/Contents/MacOS/chromacal")
    echo "Self-contained: bundled $(find "$bundle/Contents/Frameworks" -name '*.dylib' | wc -l | tr -d ' ') libs into Contents/Frameworks"
fi

echo "Built: $bundle"
echo "Install: cp -R '$bundle' '/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/'"
