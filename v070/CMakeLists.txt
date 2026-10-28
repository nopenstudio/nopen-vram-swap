cmake_minimum_required(VERSION 3.20)
project(nopen_v070 C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(NOPEN_OPENCL "Require real AMD OpenCL support for the GPU SWAP backend" ON)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../v060" "${CMAKE_CURRENT_BINARY_DIR}/v060")
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME v070_swap_controller_command_gate
 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/tests/test_swap_policy.py"
 "${CMAKE_CURRENT_LIST_DIR}/tools/vram_swap_lab.sh")
set_tests_properties(v070_swap_controller_command_gate PROPERTIES
 PASS_REGULAR_EXPRESSION "V070_GATE_PASS" TIMEOUT 15)
