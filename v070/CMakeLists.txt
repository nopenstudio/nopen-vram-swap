cmake_minimum_required(VERSION 3.20)
project(nopen_v070 C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(NOPEN_OPENCL "Require real AMD OpenCL support for the GPU SWAP backend" ON)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../v060" "${CMAKE_CURRENT_BINARY_DIR}/v060")
find_package(Threads REQUIRED)
add_library(nopen_shadow STATIC src/nopen_shadow.c)
target_include_directories(nopen_shadow PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(nopen_shadow PUBLIC nopen_isolated_io nopen_page_core Threads::Threads)
target_compile_options(nopen_shadow PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(nopen-test-shadow tests/test_shadow.c)
target_link_libraries(nopen-test-shadow PRIVATE nopen_shadow)
target_compile_options(nopen-test-shadow PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME v070_durable_disk_shadow_survives_gpu_worker_timeout
  COMMAND nopen-test-shadow $<TARGET_FILE:nopen-isolated-worker>)
set_tests_properties(v070_durable_disk_shadow_survives_gpu_worker_timeout
  PROPERTIES PASS_REGULAR_EXPRESSION "V070_SHADOW_TEST_PASS" TIMEOUT 30)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME v070_swap_controller_command_gate
 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/tests/test_swap_policy.py"
 "${CMAKE_CURRENT_LIST_DIR}/tools/vram_swap_lab.sh")
set_tests_properties(v070_swap_controller_command_gate PROPERTIES
 PASS_REGULAR_EXPRESSION "V070_GATE_PASS" TIMEOUT 15)

add_test(NAME v070_bannerlord_swap_proof_and_comparison
 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/tests/test_game_comparison.py")
set_tests_properties(v070_bannerlord_swap_proof_and_comparison PROPERTIES
 PASS_REGULAR_EXPRESSION "GAME_COMPARE_TEST_PASS" TIMEOUT 10)

add_test(NAME v070_fatal_gpu_ipc_forensic_log
 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/tests/test_fatal_telemetry.py"
 $<TARGET_FILE:nopen-test-isolated-io> $<TARGET_FILE:nopen-isolated-worker>)
set_tests_properties(v070_fatal_gpu_ipc_forensic_log PROPERTIES
 PASS_REGULAR_EXPRESSION "V070_FATAL_TELEMETRY_PASS" TIMEOUT 20)
