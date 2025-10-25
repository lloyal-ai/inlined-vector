cmake_minimum_required(VERSION 3.14)
project(inlined-vector VERSION 5.7.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(INLINED_VECTOR_BUILD_TESTS "Build tests" OFF)
option(INLINED_VECTOR_BUILD_FUZZ_TESTS "Build fuzz tests (requires FuzzTest)" OFF)
option(INLINED_VECTOR_BUILD_BENCHMARKS "Build benchmarks (requires Google Benchmark and system Boost)" OFF)

# Header-only library
add_library(inlined-vector INTERFACE)
add_library(inlined-vector::inlined-vector ALIAS inlined-vector)

target_include_directories(inlined-vector INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(inlined-vector INTERFACE cxx_std_20)

# Tests
if(INLINED_VECTOR_BUILD_TESTS)
    enable_testing()

    # Unit tests with sanitizers
    add_executable(test_inlined_vector tests/test_inlined_vector.cpp)
    target_link_libraries(test_inlined_vector PRIVATE inlined-vector)
    target_compile_options(test_inlined_vector PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -g
    )
    target_link_options(test_inlined_vector PRIVATE
        -fsanitize=address,undefined
    )
    add_test(NAME inlined_vector_tests COMMAND test_inlined_vector)
endif()

# Fuzz tests
if(INLINED_VECTOR_BUILD_FUZZ_TESTS)
    # Set C++ standard before fetching FuzzTest
    set(CMAKE_CXX_STANDARD 20 CACHE STRING "" FORCE)
    set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)

    include(FetchContent)
    FetchContent_Declare(
        fuzztest
        GIT_REPOSITORY https://github.com/google/fuzztest.git
        GIT_TAG main
    )
    set(FUZZTEST_FUZZING_MODE OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(fuzztest)

    add_executable(fuzz_inlined_vector tests/fuzz_inlined_vector.cpp)
    target_link_libraries(fuzz_inlined_vector PRIVATE
        inlined-vector
        GTest::gtest
        fuzztest::fuzztest_gtest_main
    )
    add_test(NAME fuzz_inlined_vector COMMAND fuzz_inlined_vector)
endif()

# Benchmarks
if(INLINED_VECTOR_BUILD_BENCHMARKS)
    message(STATUS "Building benchmarks (requires system Boost installation)...")
    include(FetchContent)

    # 1. Fetch Google Benchmark
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.8.3
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)

    # 2. Fetch Abseil (for absl::InlinedVector)
    set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
    set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        abseil-cpp
        GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
        GIT_TAG 20240116.2
    )
    FetchContent_MakeAvailable(abseil-cpp)

    # 3. Find system Boost (for boost::container::small_vector)
    find_package(Boost 1.70 REQUIRED)

    # 4. Define the benchmark executable
    add_executable(bench_inlined_vector bench/bench_inlined_vector.cpp)

    # 5. Link libraries
    target_link_libraries(bench_inlined_vector PRIVATE
        inlined-vector::inlined-vector
        benchmark::benchmark
        benchmark::benchmark_main
        absl::inlined_vector
        Boost::boost
    )

    # 6. Set optimization flags for accurate benchmarking
    target_compile_options(bench_inlined_vector PRIVATE
        -O3
        -DNDEBUG
        -march=native
    )

    # 7. Enable LTO for maximum performance
    set_target_properties(bench_inlined_vector PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION TRUE
    )
endif()

# Installation
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(TARGETS inlined-vector
    EXPORT inlined-vector-targets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT inlined-vector-targets
    FILE inlined-vector-targets.cmake
    NAMESPACE inlined-vector::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/inlined-vector
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/inlined-vector-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/inlined-vector-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/inlined-vector
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/inlined-vector-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/inlined-vector-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/inlined-vector-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/inlined-vector
)
