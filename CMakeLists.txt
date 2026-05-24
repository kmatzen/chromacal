cmake_minimum_required(VERSION 3.14)
project(chromacal VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------
option(CHROMACAL_BUILD_PYTHON "Build Python bindings" OFF)
option(CHROMACAL_BUILD_EXAMPLES "Build examples" OFF)
option(CHROMACAL_BUILD_TESTS "Build tests" OFF)
option(CHROMACAL_BUILD_PPRO "Build the Premiere Pro plugin (native core + CLI)" OFF)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------
find_package(OpenCV 4.0 REQUIRED)
find_package(Ceres REQUIRED)
find_package(OpenColorIO REQUIRED)
find_package(Eigen3 REQUIRED)

# OpenMP (optional)
find_package(OpenMP)

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

target_include_directories(chromacal
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(chromacal
    PUBLIC
        ${OpenCV_LIBS}
        Ceres::ceres
        OpenColorIO::OpenColorIO
        Eigen3::Eigen
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(chromacal PRIVATE OpenMP::OpenMP_CXX)
endif()

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
# Premiere Pro plugin
# ------------------------------------------------------------------------------
if(CHROMACAL_BUILD_PPRO)
    add_subdirectory(plugin)
endif()

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------
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
