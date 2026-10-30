cmake_minimum_required(VERSION 3.20)
project(nopen_v080_alpha4 C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(NOPEN_OPENCL "Build GPU backend with AMD OpenCL" ON)
enable_testing()
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../v070" "${CMAKE_CURRENT_BINARY_DIR}/v070")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME v081_game_detection_vram_guard_and_reclaim_gate
 COMMAND "${Python3_EXECUTABLE}"
         "${CMAKE_CURRENT_LIST_DIR}/tests/test_v081_bannerlord_probe.py")
set_tests_properties(v081_game_detection_vram_guard_and_reclaim_gate
 PROPERTIES PASS_REGULAR_EXPRESSION "V081_BANNERLORD_GATES_PASS" TIMEOUT 10)
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
add_executable(nopen-v080-async-shadow-bench src/nopen_async_shadow_bench.c)
target_link_libraries(nopen-v080-async-shadow-bench PRIVATE nopen_shadow)
target_compile_options(nopen-v080-async-shadow-bench PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME v080_alpha6_mock_async_benchmark_and_worker_loss
 COMMAND nopen-v080-async-shadow-bench --backend mock --capacity-mib 4
         --operations 750
         --worker $<TARGET_FILE:nopen-isolated-worker>
         --shadow-file "${CMAKE_CURRENT_BINARY_DIR}/v080-async-mock-shadow.bin")
set_tests_properties(v080_alpha6_mock_async_benchmark_and_worker_loss PROPERTIES
 PASS_REGULAR_EXPRESSION "V080_ASYNC_BENCH_PASS" TIMEOUT 45)
add_test(NAME v080_alpha9_disk_only_shadow_baseline_keeps_data
 COMMAND nopen-v080-async-shadow-bench --backend mock --capacity-mib 4
         --mode disk-only --operations 750
         --worker $<TARGET_FILE:nopen-isolated-worker>
         --shadow-file "${CMAKE_CURRENT_BINARY_DIR}/v080-alpha9-ssd-only-shadow.bin")
set_tests_properties(v080_alpha9_disk_only_shadow_baseline_keeps_data
 PROPERTIES PASS_REGULAR_EXPRESSION "V080_ASYNC_BENCH_PASS" TIMEOUT 45)
# GPU-only data path (no SSD shadow, no kernel swap activation).
add_executable(nopen-v080-vram-direct-bench src/nopen_vram_direct_bench.c)
target_link_libraries(nopen-v080-vram-direct-bench PRIVATE nopen_isolated_io)
target_compile_options(nopen-v080-vram-direct-bench PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME v080_alpha10_gpu_only_4k_page_integrity
 COMMAND nopen-v080-vram-direct-bench --backend mock
         --capacity-mib 4 --io-kib 4 --operations 500
         --worker $<TARGET_FILE:nopen-isolated-worker>)
set_tests_properties(v080_alpha10_gpu_only_4k_page_integrity
 PROPERTIES PASS_REGULAR_EXPRESSION "NOPEN_VRAM_DIRECT_PASS" TIMEOUT 30)
add_test(NAME v080_alpha10_gpu_only_64k_batch_integrity
 COMMAND nopen-v080-vram-direct-bench --backend mock
         --capacity-mib 4 --io-kib 64 --operations 250
         --worker $<TARGET_FILE:nopen-isolated-worker>)
set_tests_properties(v080_alpha10_gpu_only_64k_batch_integrity
 PROPERTIES PASS_REGULAR_EXPRESSION "NOPEN_VRAM_DIRECT_PASS" TIMEOUT 30)
add_test(NAME v081_vram_direct_spans_entire_address_space_4k
 COMMAND nopen-v080-vram-direct-bench --backend mock
         --capacity-mib 4 --io-kib 4 --operations 500
         --spread-across-vram
         --worker $<TARGET_FILE:nopen-isolated-worker>)
set_tests_properties(v081_vram_direct_spans_entire_address_space_4k
 PROPERTIES PASS_REGULAR_EXPRESSION "spread_across_vram=YES" TIMEOUT 30)
add_test(NAME v081_vram_direct_segment_boundary_64k
 COMMAND nopen-v080-vram-direct-bench --backend mock
         --capacity-mib 4 --io-kib 64 --operations 250
         --spread-across-vram
         --worker $<TARGET_FILE:nopen-isolated-worker>)
set_tests_properties(v081_vram_direct_segment_boundary_64k
 PROPERTIES PASS_REGULAR_EXPRESSION "segment_edge_checks=3" TIMEOUT 30)
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
add_test(NAME v080_alpha3_mock_tagged_pages_two_passes
 COMMAND nopen-v080-gpu-bench --backend mock --mode direct
         --pool-mib 16 --full-passes 2 --operations 32)
set_tests_properties(v080_alpha3_mock_tagged_pages_two_passes PROPERTIES
 PASS_REGULAR_EXPRESSION "tagged_page_ids=YES verified_pass=2" TIMEOUT 45)
add_test(NAME v080_alpha3_reject_4g_and_weak_guard
 COMMAND "${Python3_EXECUTABLE}"
         "${CMAKE_CURRENT_LIST_DIR}/tests/test_alpha3_guard.py"
         $<TARGET_FILE:nopen-v080-gpu-bench>)
set_tests_properties(v080_alpha3_reject_4g_and_weak_guard PROPERTIES
 PASS_REGULAR_EXPRESSION "V080_ALPHA3_CLI_GUARD_PASS" TIMEOUT 15)
add_test(NAME v080_alpha4_reject_bad_capacity_or_approval
 COMMAND "${Python3_EXECUTABLE}"
         "${CMAKE_CURRENT_LIST_DIR}/tests/test_alpha4_guard.py"
         $<TARGET_FILE:nopen-v080-gpu-bench>)
set_tests_properties(v080_alpha4_reject_bad_capacity_or_approval PROPERTIES
 PASS_REGULAR_EXPRESSION "V080_ALPHA4_GUARD_PASS" TIMEOUT 15)
add_test(NAME v080_alpha4_mock_game_hold_and_strict_bounds
 COMMAND "${Python3_EXECUTABLE}"
         "${CMAKE_CURRENT_LIST_DIR}/tests/test_alpha4_hold.py"
         $<TARGET_FILE:nopen-v080-gpu-bench>)
set_tests_properties(v080_alpha4_mock_game_hold_and_strict_bounds PROPERTIES
 PASS_REGULAR_EXPRESSION "V080_ALPHA4_HOLD_GUARD_PASS" TIMEOUT 15)
