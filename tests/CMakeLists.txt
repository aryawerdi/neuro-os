cmake_minimum_required(VERSION 3.16)
project(NeuroOSTests)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.3
)
FetchContent_MakeAvailable(googlebenchmark)

include_directories(
    ${CMAKE_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/../neuro_os
)

enable_testing()

file(GLOB UNIT_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp")
file(GLOB INTEGRATION_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/*.cpp")

foreach(test ${UNIT_TESTS})
    get_filename_component(test_name ${test} NAME_WE)
    add_executable(${test_name} ${test})
    target_link_libraries(${test_name} 
        gtest_main 
        pthread
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

foreach(test ${INTEGRATION_TESTS})
    get_filename_component(test_name ${test} NAME_WE)
    add_executable(${test_name} ${test})
    target_link_libraries(${test_name} 
        gtest_main 
        pthread
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

file(GLOB BENCHMARKS "${CMAKE_CURRENT_SOURCE_DIR}/../benchmarks/*.cpp")

foreach(benchmark ${BENCHMARKS})
    get_filename_component(benchmark_name ${benchmark} NAME_WE)
    add_executable(${benchmark_name} ${benchmark})
    target_link_libraries(${benchmark_name} 
        benchmark 
        benchmark_main
        pthread
    )
endforeach()
