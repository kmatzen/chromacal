# chromacal native Premiere/After Effects video effect, plus a shared SDK-free
# core (solve_core) and a headless CLI (chromacal_solve) used for the smoke test.
#
# Enabled from the top-level build with -DCHROMACAL_BUILD_PPRO=ON.

# --- SDK-free core ----------------------------------------------------------
add_library(chromacal_ppro_core STATIC native/solve_core.cpp)
target_include_directories(chromacal_ppro_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/native)
target_link_libraries(chromacal_ppro_core PUBLIC chromacal)
set_target_properties(chromacal_ppro_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Headless CLI / verification harness.
add_executable(chromacal_solve native/solve_cli.cpp)
target_link_libraries(chromacal_solve PRIVATE chromacal_ppro_core)

# Parity-check tools: apply a preset with the effect's exact transform, and diff
# two images (headless apply vs the frame Premiere exports after applying it).
add_executable(chromacal_apply native/apply_cli.cpp)
target_link_libraries(chromacal_apply PRIVATE chromacal_ppro_core)
add_executable(chromacal_diff native/diff_cli.cpp)
target_link_libraries(chromacal_diff PRIVATE chromacal_ppro_core)
# Generate the Lumetri (display-referred) cube from a preset — the effect's
# Export .cube, reproducible from the CLI for testing the Lumetri path.
add_executable(chromacal_cube native/cube_cli.cpp)
target_link_libraries(chromacal_cube PRIVATE chromacal_ppro_core)
# Apply a .cube via trilinear interpolation (NLE-style) — lets the cube<->effect
# match be tested headlessly (no Premiere).
add_executable(chromacal_lutapply native/lutapply_cli.cpp)
target_link_libraries(chromacal_lutapply PRIVATE chromacal_ppro_core)

# Self-verifying smoke test: calibrate the bundled example frame end-to-end.
if(CHROMACAL_BUILD_TESTS)
    enable_testing()
    add_test(NAME ppro_core_smoke
        COMMAND chromacal_solve
                ${CMAKE_CURRENT_SOURCE_DIR}/../docs/before.png
                ${CMAKE_CURRENT_BINARY_DIR}/smoke.cube 9)
    set_tests_properties(ppro_core_smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "patches_detected=24")

    # The exported Lumetri cube must reproduce the effect's transform: solve ->
    # cube -> trilinear-apply vs the effect's exact apply, mean diff within tol.
    # Guards against a cube-baking regression (e.g. reverting to the sRGB bake).
    add_test(NAME cube_reproduces_effect
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/../tests/cube_parity.sh
                $<TARGET_FILE_DIR:chromacal_solve>
                ${CMAKE_CURRENT_SOURCE_DIR}/../docs/before.png 0.02)
endif()

# --- Native Premiere/AE video effect (optional) ----------------------------
# A Premiere video effect is an After Effects-style plugin, so it needs BOTH:
#   CHROMACAL_PRSDK_DIR : Premiere C++ SDK   .../Examples/Headers
#   CHROMACAL_AESDK_DIR : After Effects SDK  .../Examples/Headers
# This target compiles + links the effect; producing a *loadable* plugin also
# requires compiling the PiPL (effect/chromacal_effect.r) via Rez/PiPLtool and
# bundling it — see plugin/effect/README.md (using the SDK's Xcode project is
# the path of least resistance).
if(CHROMACAL_PRSDK_DIR AND CHROMACAL_AESDK_DIR)
    set(_cc_effect_src effect/chromacal_effect.cpp)
    if(APPLE)
        # Native NSSavePanel/NSOpenPanel for the file dialogs.
        list(APPEND _cc_effect_src effect/save_dialog_mac.mm)
    elseif(WIN32)
        # Win32 GetSaveFileName/GetOpenFileName for the file dialogs.
        list(APPEND _cc_effect_src effect/save_dialog_win.cpp)
    endif()
    # Opt-in GPU (Metal) render path (-DCHROMACAL_GPU=ON). Adds the Metal host;
    # the kernel is compiled to a .metallib + bundled by build_bundle.sh.
    if(CHROMACAL_GPU AND APPLE)
        list(APPEND _cc_effect_src effect/chromacal_gpu_mac.mm)
    endif()
    add_library(chromacal_effect MODULE ${_cc_effect_src})
    if(CHROMACAL_GPU)
        target_compile_definitions(chromacal_effect PRIVATE CHROMACAL_GPU)
        if(APPLE)
            target_link_libraries(chromacal_effect PRIVATE "-framework Metal" "-framework Foundation")
        endif()
    endif()
    # AE SDK is primary (Headers + SP + Util); the Premiere SDK is added last
    # for the PrSDK*-only headers. Both ship colliding SP*/AEConfig headers, so
    # order matters.
    # PrSDK dir FIRST: it's the authoritative (Premiere 26) source for the
    # PrSDK* headers, which the AE SDK mirror also ships in older versions
    # (e.g. PrSDKAESupport.h without PF_UtilitySuite13/GetContainingTimelineID).
    # The AE dirs follow for the pure-AE headers (AE_Effect.h, Param_Utils.h, …).
    target_include_directories(chromacal_effect PRIVATE
        ${CHROMACAL_PRSDK_DIR}
        ${CHROMACAL_AESDK_DIR}
        ${CHROMACAL_AESDK_DIR}/SP
        ${CHROMACAL_AESDK_DIR}/../Util)
    target_link_libraries(chromacal_effect PRIVATE chromacal_ppro_core)
    if(APPLE)
        # Drop dylibs the effect doesn't actually use (OpenColorIO + abseil, unused
        # OpenCV modules) from the binary's load commands, so the self-contained
        # bundle stays small. (create_lut/apply_lut live in their own TU, so the
        # effect never references OCIO symbols.)
        target_link_options(chromacal_effect PRIVATE "LINKER:-dead_strip_dylibs")
        # The SP headers reference Carbon/CoreFoundation types under MAC_ENV.
        target_compile_definitions(chromacal_effect PRIVATE MAC_ENV)
        # SHELL: keeps each "-include <hdr>" pair together (CMake otherwise
        # de-duplicates the repeated -include flag).
        target_compile_options(chromacal_effect PRIVATE
            "SHELL:-include CoreServices/CoreServices.h"
            "SHELL:-include CoreFoundation/CoreFoundation.h")
        target_link_libraries(chromacal_effect PRIVATE
            "-framework CoreServices" "-framework CoreFoundation" "-framework AppKit")
    elseif(WIN32)
        target_link_libraries(chromacal_effect PRIVATE comdlg32)
        # --- PiPL resource (Windows, now automated) ----------------------
        # The mac build bakes the PiPL into a flat .rsrc (build_bundle.sh); on
        # Windows it's a linked resource. Generate chromacal_effect.rc via the AE
        # SDK's cl/PiPLtool flow (win_pipl.cmake), then let MSVC's rc.exe compile
        # it and the linker embed it — so the .aex is directly loadable, no manual
        # steps. PiPLtool ships in the AE SDK's Resources dir.
        find_program(CHROMACAL_PIPLTOOL
            NAMES PiPLtool PiPLtool.exe PiPLTool PiPLTool.exe
            PATHS "${CHROMACAL_AESDK_DIR}/../Resources" NO_DEFAULT_PATH)
        if(NOT CHROMACAL_PIPLTOOL)
            message(FATAL_ERROR
                "PiPLtool not found under ${CHROMACAL_AESDK_DIR}/../Resources — "
                "needed to build the Windows PiPL resource.")
        endif()
        set(_pipl_r   "${CMAKE_CURRENT_SOURCE_DIR}/effect/chromacal_effect.r")
        set(_pipl_rr  "${CMAKE_CURRENT_BINARY_DIR}/chromacal_effect.rr")
        set(_pipl_rrc "${CMAKE_CURRENT_BINARY_DIR}/chromacal_effect.rrc")
        set(_pipl_rc  "${CMAKE_CURRENT_BINARY_DIR}/chromacal_effect.rc")
        add_custom_command(
            OUTPUT "${_pipl_rc}"
            COMMAND ${CMAKE_COMMAND}
                "-DCL=${CMAKE_CXX_COMPILER}"          # cl.exe (MSVC) preprocesses the .r/.rrc
                "-DPIPLTOOL=${CHROMACAL_PIPLTOOL}"
                "-DR=${_pipl_r}" "-DRR=${_pipl_rr}" "-DRRC=${_pipl_rrc}" "-DRC=${_pipl_rc}"
                "-DINC1=${CHROMACAL_AESDK_DIR}"
                "-DINC2=${CHROMACAL_AESDK_DIR}/../Resources"
                "-DINC3=${CHROMACAL_PRSDK_DIR}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/effect/win_pipl.cmake"
            DEPENDS "${_pipl_r}" "${CMAKE_CURRENT_SOURCE_DIR}/effect/win_pipl.cmake"
            COMMENT "Generating PiPL resource (chromacal_effect.rc)"
            VERBATIM)
        target_sources(chromacal_effect PRIVATE "${_pipl_rc}")
    endif()
    if(APPLE)
        set_target_properties(chromacal_effect PROPERTIES
            PREFIX "" OUTPUT_NAME "chromacal" SUFFIX ".bundle")
        message(STATUS "Building native Premiere/AE effect: chromacal.bundle (PiPL via build_bundle.sh)")
    elseif(WIN32)
        # An AE/Premiere effect on Windows is a DLL with the .aex extension.
        set_target_properties(chromacal_effect PROPERTIES
            PREFIX "" OUTPUT_NAME "chromacal" SUFFIX ".aex")
        message(STATUS "Building native Premiere/AE effect: chromacal.aex (PiPL linked in)")
    endif()
else()
    message(STATUS
        "CHROMACAL_PRSDK_DIR / CHROMACAL_AESDK_DIR not both set — native effect "
        "skipped. See plugin/effect/README.md.")
endif()

# --- UXP native addon (optional) -------------------------------------------
# Full-resolution analysis panel's native side (chromacal.uxpaddon). Built only
# when the Premiere UXP Hybrid SDK is supplied (a gated Adobe download):
#   CHROMACAL_UXP_SDK_DIR : directory containing UxpAddon.h (the SDK's utilities/
#                           dir; UxpAddon.h pulls in ../api/* itself).
#   CHROMACAL_UXP_SDK_SRC : the SDK's support .cpp files defining the runtime glue
#                           (CreateErrorFromException, etc.), or link a prebuilt
#                           CHROMACAL_UXP_SDK_LIB instead.
if(CHROMACAL_UXP_SDK_DIR)
    if(NOT EXISTS "${CHROMACAL_UXP_SDK_DIR}/UxpAddon.h")
        message(WARNING
            "CHROMACAL_UXP_SDK_DIR set but UxpAddon.h not found there: "
            "${CHROMACAL_UXP_SDK_DIR} — skipping chromacal.uxpaddon.")
    else()
        add_library(chromacal_uxpaddon MODULE
            native/chromacal_addon.cpp
            ${CHROMACAL_UXP_SDK_SRC})
        target_include_directories(chromacal_uxpaddon PRIVATE ${CHROMACAL_UXP_SDK_DIR})
        target_link_libraries(chromacal_uxpaddon PRIVATE chromacal_ppro_core)
        if(CHROMACAL_UXP_SDK_LIB)
            target_link_libraries(chromacal_uxpaddon PRIVATE ${CHROMACAL_UXP_SDK_LIB})
        endif()
        # The UXP runtime requires the literal extension ".uxpaddon".
        set_target_properties(chromacal_uxpaddon PROPERTIES
            PREFIX "" OUTPUT_NAME "chromacal" SUFFIX ".uxpaddon")
        message(STATUS "Building Premiere UXP addon: chromacal.uxpaddon")
    endif()
else()
    message(STATUS
        "CHROMACAL_UXP_SDK_DIR not set — UXP analysis addon skipped "
        "(native effect still builds). See plugin/README.md.")
endif()
