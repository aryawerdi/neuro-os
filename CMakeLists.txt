cmake_minimum_required(VERSION 3.16)
project(NeuroOSUtils VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_VERSION VERSION_LESS "3.20")
    set(CMAKE_CXX_STANDARD 17)
endif()

add_library(neuro_os_utils INTERFACE)
target_include_directories(neuro_os_utils INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_sources(neuro_os_utils INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/neuro_os/utils/logging.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/neuro_os/utils/metrics.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/neuro_os/utils/thread_pool.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/neuro_os/utils/config.hpp
)

include(CTest)
if(BUILD_TESTING)
    enable_testing()
    
    add_executable(test_utils 
        tests/test_utils.cpp
    )
    target_link_libraries(test_utils 
        PRIVATE 
        neuro_os_utils
    )
    
    add_test(NAME UtilsTests COMMAND test_utils)
endif()
