cmake_minimum_required(VERSION 3.20)
project(nopen_v084 C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(NOPEN_OPENCL "Build real OpenCL backend (OFF for mock-only diagnostic)" ON)
enable_testing()
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../v083" "${CMAKE_CURRENT_BINARY_DIR}/v083")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(nopen-v084-mock-reclaim-stall
  tests/test_mock_reclaim_stall.c)
target_link_libraries(nopen-v084-mock-reclaim-stall PRIVATE nopen_isolated_io)
target_compile_options(nopen-v084-mock-reclaim-stall PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
add_test(NAME v084_mock_alive_worker_stalled_read_write_timeout
  COMMAND nopen-v084-mock-reclaim-stall
    $<TARGET_FILE:nopen-isolated-worker>)
set_tests_properties(v084_mock_alive_worker_stalled_read_write_timeout PROPERTIES
  PASS_REGULAR_EXPRESSION "V084_MOCK_RECLAIM_STALL_PASS"
  TIMEOUT 12)

add_test(NAME v084_hypothetical_reclaim_cycle_and_safety_gate
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/tests/test_reclaim_cycle.py")
set_tests_properties(v084_hypothetical_reclaim_cycle_and_safety_gate PROPERTIES
  PASS_REGULAR_EXPRESSION "V084_RECLAIM_CYCLE_TEST_PASS"
  TIMEOUT 10)
