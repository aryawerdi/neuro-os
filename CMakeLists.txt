cmake_minimum_required(VERSION 3.20)
project(neuro_os_jit_ir VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(neuro_os_ir STATIC
    neuro_os/compiler/ir_builder.hpp
    neuro_os/compiler/ir_printer.hpp
    neuro_os/compiler/ir_visitor.hpp
    neuro_os/compiler/ir_ssa.hpp
    neuro_os/compiler/ir_analysis.hpp
    neuro_os/compiler/ir_optimization.hpp
    neuro_os/compiler/ir_serialization.hpp
)

target_include_directories(neuro_os_ir PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(neuro_os_ir PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    -Wshadow -Wnon-virtual-dtor -Wold-style-cast
    -Wcast-align -Wunused -Woverloaded-virtual
    -Wconversion -Wsign-conversion
)

add_executable(test_ir_system test_ir_system.cpp)
target_link_libraries(test_ir_system PRIVATE neuro_os_ir)

add_executable(example_ir example_ir.cpp)
target_link_libraries(example_ir PRIVATE neuro_os_ir)

enable_testing()
add_test(NAME test_ir_system COMMAND test_ir_system)