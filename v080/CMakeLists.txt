cmake_minimum_required(VERSION 3.20)
project(nopen_v080_alpha1 C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(NOPEN_OPENCL "Build GPU backend with AMD OpenCL" ON)
enable_testing()
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../v070" "${CMAKE_CURRENT_BINARY_DIR}/v070")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(nopen-v080-scale src/nopen_scale_sweep.c)
target_link_libraries(nopen-v080-scale PRIVATE nopen_shadow)
target_compile_options(nopen-v080-scale PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME v080_capacity_1_2_4gib_feasibility_and_no_swap
 COMMAND "${Python3_EXECUTABLE}"
         "${CMAKE_CURRENT_LIST_DIR}/tests/test_capacity_plan.py")
set_tests_properties(v080_capacity_1_2_4gib_feasibility_and_no_swap PROPERTIES
 PASS_REGULAR_EXPRESSION "V080_CAPACITY_POLICY_PASS" TIMEOUT 15)
add_test(NAME v080_opencl_profiling_is_opt_in
 COMMAND "${Python3_EXECUTABLE}"
         "${CMAKE_CURRENT_LIST_DIR}/tests/test_opencl_optimization.py")
set_tests_properties(v080_opencl_profiling_is_opt_in PROPERTIES
 PASS_REGULAR_EXPRESSION "V080_PROFILING_POLICY_PASS" TIMEOUT 15)

add_executable(nopen-v080-test-zero tests/test_zero_fastpath.c)
target_link_libraries(nopen-v080-test-zero PRIVATE nopen_shadow)
target_compile_options(nopen-v080-test-zero PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME v080_zero_page_fastpath_preserves_acked_writes
 COMMAND nopen-v080-test-zero $<TARGET_FILE:nopen-isolated-worker>
         "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(v080_zero_page_fastpath_preserves_acked_writes PROPERTIES
 PASS_REGULAR_EXPRESSION "V080_ZERO_FASTPATH_PASS" TIMEOUT 30)
