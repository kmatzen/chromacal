# Building the chromacal effect on Windows

The effect source is cross-platform — `_WIN32` branches, `save_dialog_win.cpp`
(GetSaveFileName/GetOpenFileName), and `comdlg32` linkage are already in place. It
has **not yet been built or tested in Premiere on Windows**; this is the recipe to
do so. (macOS is built via `plugin/effect/build_bundle.sh`; that script is
mac-only — Rez/bundle. Windows produces a `.aex`, which is a DLL.)

## Prerequisites

- Visual Studio 2022 (Desktop C++), CMake.
- The **Premiere Pro C++ SDK** and the **After Effects SDK** (same as macOS).
- Dependencies for the core (OpenCV, Ceres, Eigen, OpenColorIO) — easiest via
  **vcpkg**: `vcpkg install opencv[contrib] ceres eigen3 opencolorio`
  (`opencv[contrib]` provides the `mcc` ColorChecker module).

## Build the core + CLIs (no Adobe SDK needed)

```powershell
cmake -B build -DCHROMACAL_BUILD_PPRO=ON -DCHROMACAL_BUILD_TESTS=ON `
      -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
ctest --test-dir build -C Release          # detection + fixed-point + cube parity
```

## Build the effect + stage a self-contained plugin (needs the Adobe SDKs)

The PiPL resource, the `.aex` naming, and dependency bundling are now **automated**
(they were the four manual steps in earlier revisions). One command does it all,
from a *"x64 Native Tools Command Prompt for VS"* (so `cl.exe`, `rc.exe`,
`dumpbin.exe` are on PATH):

```powershell
pwsh plugin\effect\build_windows.ps1 `
  -Vcpkg C:\vcpkg `
  -PrSdk "C:\Premiere Pro 26.0 C++ SDK\Examples\Headers" `
  -AeSdk "C:\After Effects SDK\Examples\Headers"
```

This:

1. **Configures + builds** `chromacal.aex` with CMake/Ninja (Release).
2. **Generates + links the PiPL** automatically — `plugin/CMakeLists.txt` runs the
   AE SDK's `cl /EP` → `PiPLtool` → `cl /D MSWindows /EP` flow via
   `plugin/effect/win_pipl.cmake`, then MSVC's `rc.exe` compiles the `.rc` and the
   linker embeds it. (On macOS the PiPL is a flat `.rsrc`; on Windows it's a linked
   resource. `PiPLtool` ships in the AE SDK's `Examples/Resources`.)
3. **Names** the output `chromacal.aex` (an AE/Premiere effect is a DLL with that
   extension).
4. **Stages** `dist\chromacal\` — the `.aex` plus its *transitive* dependency DLLs
   (OpenCV/Ceres/OpenColorIO/…), resolved with `dumpbin` against the vcpkg `bin`,
   so it loads without vcpkg on PATH (the analog of the macOS self-contained bundle).

## Package the installer

```powershell
iscc /DAppVersion=0.1.0 plugin\effect\chromacal.iss   # -> dist\chromacal-0.1.0-win-x64.exe
```

`chromacal.iss` (Inno Setup) installs the staged folder into
`C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore\chromacal`, where Premiere
and After Effects both load it. Sign the resulting `.exe` with your Windows
code-signing cert to avoid SmartScreen warnings.

## Still requires a Windows box to verify

The above is written from Adobe's own SDK Windows example and is **build-ready, but
has not yet been compiled or run on Windows** (the macOS build is the verified one).
The first time you build, expect to shake out small issues. After it builds:

- **Install + test in Premiere** (Analyze / Apply / Export .cube). Confirm parity
  with the headless core via `tests/ppro_parity.sh` (WSL/Git-Bash).
- The render path is plain legacy `PF_Cmd_RENDER` (no GPU/SmartRender) — the same
  verified path as macOS, so behavior should match once it builds.
