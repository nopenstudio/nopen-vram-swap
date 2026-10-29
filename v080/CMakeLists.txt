cmake_minimum_required(VERSION 3.20)
project(nopen_v080_alpha2 C)
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

# Alpha.2 real AMD OpenCL device benchmark: no kernel block device or SWAP.
add_executable(nopen-v080-gpu-bench src/nopen_gpu_bench.c)
target_link_libraries(nopen-v080-gpu-bench PRIVATE nopen_vram_store)
target_compile_options(nopen-v080-gpu-bench PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME v080_alpha2_mock_direct_transfer_integrity
 COMMAND nopen-v080-gpu-bench --backend mock --mode direct
         --pool-mib 16 --operations 32)
set_tests_properties(v080_alpha2_mock_direct_transfer_integrity PROPERTIES
 PASS_REGULAR_EXPRESSION "V080_GPU_BENCH_PASS" TIMEOUT 45)
add_test(NAME v080_alpha2_mock_pinned_cli_integrity
 COMMAND nopen-v080-gpu-bench --backend mock --mode pinned
         --pool-mib 16 --operations 32)
set_tests_properties(v080_alpha2_mock_pinned_cli_integrity PROPERTIES
 PASS_REGULAR_EXPRESSION "V080_GPU_BENCH_PASS" TIMEOUT 45)

add_test(NAME v080_alpha2_strict_gpu_benchmark_report
 COMMAND "${Python3_EXECUTABLE}"
         "${CMAKE_CURRENT_LIST_DIR}/tests/test_gpu_compare.py")
set_tests_properties(v080_alpha2_strict_gpu_benchmark_report PROPERTIES
 PASS_REGULAR_EXPRESSION "V080_ALPHA2_GPU_COMPARE_TEST_PASS" TIMEOUT 10)

add_executable(nopen-v080-shadow-bench src/nopen_shadow_bench.c)
target_link_libraries(nopen-v080-shadow-bench PRIVATE nopen_shadow)
target_compile_options(nopen-v080-shadow-bench PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME v080_alpha2_real_ssd_shadow_write_through_and_worker_failover_mock
 COMMAND nopen-v080-shadow-bench --backend mock --mode direct
         --operations 32
         --worker $<TARGET_FILE:nopen-isolated-worker>
         --shadow-file "${CMAKE_CURRENT_BINARY_DIR}/v080-alpha2-shadow-mock.bin")
set_tests_properties(v080_alpha2_real_ssd_shadow_write_through_and_worker_failover_mock PROPERTIES
 PASS_REGULAR_EXPRESSION "V080_SHADOW_BENCH_PASS" TIMEOUT 40)
