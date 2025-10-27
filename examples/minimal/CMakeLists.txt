cmake_minimum_required(VERSION 3.15)
project(inlined-vector-minimal-example LANGUAGES CXX)

# Option 1: Use installed inlined-vector (via find_package)
# Uncomment this if you've installed the library:
# find_package(inlined-vector REQUIRED)

# Option 2: Use in-tree build (development workflow)
# This assumes the example is in the source tree
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/inlined-vector)

add_executable(minimal_example main.cpp)
target_link_libraries(minimal_example PRIVATE inlined-vector::inlined-vector)
set_target_properties(minimal_example PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)
