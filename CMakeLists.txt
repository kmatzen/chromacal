cmake_minimum_required(VERSION 3.14)
project(chromacal VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------
option(CHROMACAL_BUILD_PYTHON "Build Python bindings" OFF)
option(CHROMACAL_BUILD_EXAMPLES "Build examples" OFF)
option(CHROMACAL_BUILD_TESTS "Build tests" OFF)
option(CHROMACAL_BUILD_PPRO "Build the Premiere Pro plugin (native core + CLI)" OFF)

# Optional explicit override for the AppleClang/OpenMP fallback below.
set(CHROMACAL_LIBOMP_INCLUDE "" CACHE PATH "Explicit libomp include dir (fallback when find_package(OpenMP) fails)")
set(CHROMACAL_LIBOMP "" CACHE FILEPATH "Explicit libomp library (fallback when find_package(OpenMP) fails)")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------
# Deliberately unversioned. OpenCV's config treats major versions as
# incompatible, so find_package(OpenCV 4.0) rejects 5.x outright -- which is
# what broke macOS builds when Homebrew moved the opencv formula to 5.0. The
# floor is enforced below instead.
find_package(OpenCV REQUIRED)

if(OpenCV_VERSION VERSION_LESS 4.0)
    message(FATAL_ERROR
        "chromacal requires OpenCV 4.0 or newer, found ${OpenCV_VERSION}")
endif()
find_package(Ceres REQUIRED)
find_package(OpenColorIO REQUIRED)
find_package(Eigen3 REQUIRED)

# OpenMP (optional) -- resolved per-target below, after add_library().
#
# AppleClang (bundled LLVM, no -fopenmp of its own) only supports OpenMP via
# Apple's libomp, and CMake's OpenMP module is flaky about finding it -- it
# sometimes reports OpenMP_CXX_FOUND with -Xclang -fopenmp but no libomp on the
# include path, so <omp.h> (pulled in by Eigen when EIGEN_DONT_INCLUDE_PTHREAD
# is absent) fails to resolve. The brew libomp keg is not even symlinked into
# /opt/homebrew/lib. So on AppleClang skip the module entirely and wire libomp
# up manually -- that is all this project's OpenMP is used for (-fopenmp plus
# the libomp include + library).
if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    find_path(_chromacal_libomp_include
        NAMES omp.h
        HINTS ${CHROMACAL_LIBOMP_INCLUDE}
        PATH_SUFFIXES libomp/include libomp
    )
    find_library(_chromacal_libomp HINTS ${CHROMACAL_LIBOMP} NAME_SUFFIXES .dylib .a NAMES omp)
    if(NOT _chromacal_libomp)
        file(GLOB _chromacal_libomp_kegs /opt/homebrew/Cellar/libomp/*/lib/libomp.dylib)
        if(_chromacal_libomp_kegs)
            list(GET _chromacal_libomp_kegs 0 _chromacal_libomp)
        endif()
    endif()
    if(NOT _chromacal_libomp_include)
        file(GLOB _chromacal_libomp_inc_kegs /opt/homebrew/Cellar/libomp/*/include)
        if(_chromacal_libomp_inc_kegs)
            list(GET _chromacal_libomp_inc_kegs 0 _chromacal_libomp_include)
        endif()
    endif()
    if(NOT _chromacal_libomp_include OR NOT _chromacal_libomp)
        message(WARNING "chromacal: libomp not found; #pragma omp parallel for will be serial")
    endif()
elseif(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    find_package(OpenMP)
endif()

# ------------------------------------------------------------------------------
# Library
# ------------------------------------------------------------------------------
add_library(chromacal
    src/detect.cpp
    src/solver.cpp
    src/apply.cpp
    src/lut_ocio.cpp
)

set_target_properties(chromacal PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(chromacal::chromacal ALIAS chromacal)

target_compile_options(chromacal PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

target_include_directories(chromacal
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# OpenMP wiring (deferred until the target exists -- the AppleClang manual
# fallback runs after add_library()).
if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    if(_chromacal_libomp_include AND _chromacal_libomp)
        message(STATUS "chromacal: manual OpenMP (libomp at ${_chromacal_libomp})")
        target_compile_options(chromacal PRIVATE -Xpreprocessor -fopenmp -I${_chromacal_libomp_include})
        # INTERFACE (not PRIVATE) so downstream targets that link chromacal
        # (examples, tests, consumers) also get the libomp link.
        target_link_options(chromacal INTERFACE ${_chromacal_libomp})
    endif()
elseif(OpenMP_CXX_FOUND)
    target_link_libraries(chromacal PRIVATE OpenMP::OpenMP_CXX)
endif()

target_link_libraries(chromacal
    PUBLIC
        ${OpenCV_LIBS}
        Ceres::ceres
        OpenColorIO::OpenColorIO
        Eigen3::Eigen
)

set_target_properties(chromacal PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# ------------------------------------------------------------------------------
# Python bindings
# ------------------------------------------------------------------------------
if(CHROMACAL_BUILD_PYTHON)
    find_package(pybind11 CONFIG REQUIRED)

    pybind11_add_module(_chromacal python/bindings.cpp)
    target_link_libraries(_chromacal PRIVATE chromacal)
    install(TARGETS _chromacal DESTINATION .)
endif()

# ------------------------------------------------------------------------------
# Examples
# ------------------------------------------------------------------------------
if(CHROMACAL_BUILD_EXAMPLES)
    add_executable(calibrate examples/calibrate.cpp)
    target_link_libraries(calibrate PRIVATE chromacal)

    add_executable(apply_calibration examples/apply_calibration.cpp)
    target_link_libraries(apply_calibration PRIVATE chromacal)

    add_executable(inspect_patches examples/inspect_patches.cpp)
    target_link_libraries(inspect_patches PRIVATE chromacal)
endif()

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------
# Fetched before the Premiere Pro plugin subdirectory below so that
# Catch2::Catch2WithMain is available to plugin/CMakeLists.txt's own test
# executable (chromacal_ppro_tests).
if(CHROMACAL_BUILD_TESTS)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.8.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

    add_executable(chromacal_tests
        tests/test_types.cpp
        tests/test_detect.cpp
        tests/test_solver.cpp
        tests/test_apply.cpp
        tests/test_integration.cpp
        tests/test_encode.cpp
    )
    # test_encode.cpp pulls the effect's shared transfer-function header.
    target_include_directories(chromacal_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/plugin/effect)

    target_link_libraries(chromacal_tests PRIVATE chromacal Catch2::Catch2WithMain)
    target_compile_definitions(chromacal_tests PRIVATE
        CHROMACAL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

    include(CTest)
    include(Catch)
    catch_discover_tests(chromacal_tests)
endif()

# ------------------------------------------------------------------------------
# Premiere Pro plugin
# ------------------------------------------------------------------------------
if(CHROMACAL_BUILD_PPRO)
    add_subdirectory(plugin)
endif()

# ------------------------------------------------------------------------------
# Install
# ------------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS chromacal
    EXPORT chromacalTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/chromacal
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT chromacalTargets
    FILE chromacalTargets.cmake
    NAMESPACE chromacal::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/chromacal
)

configure_package_config_file(
    cmake/chromacalConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/chromacalConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/chromacal
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/chromacalConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/chromacalConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/chromacalConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/chromacal
)
