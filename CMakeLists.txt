cmake_minimum_required(VERSION 3.10)
project(neuro_os_patching VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Patching library
add_library(neuro_os_patching STATIC
    neuro_os/patching/verification.hpp
    neuro_os/patching/code_loader.hpp
    neuro_os/patching/patch_types.hpp
    neuro_os/patching/patch_manager.hpp
    neuro_os/patching/patch_manager.cpp
    neuro_os/patching/advanced_verification.hpp
    neuro_os/patching/runtime_patching.hpp
)

# Test executable
add_executable(test_patching test_patching.cpp)
target_link_libraries(test_patching neuro_os_patching)

# Enable testing
enable_testing()
add_test(NAME test_patching COMMAND test_patching)

# Installation
install(TARGETS neuro_os_patching
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY neuro_os/patching/
    DESTINATION include/neuro_os/patching
    FILES_MATCHING PATTERN "*.hpp"
)