cmake_minimum_required(VERSION 3.20)
project(nopen_v060 C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(NOPEN_OPENCL "Compile real AMD OpenCL code; no silent GPU fallback" ON)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../v050" "${CMAKE_CURRENT_BINARY_DIR}/v050")
find_package(Threads REQUIRED)
add_library(nopen_recovery STATIC src/nopen_recovery.c)
target_include_directories(nopen_recovery PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(nopen_recovery PUBLIC nopen_blk_backend nopen_page_core Threads::Threads)
target_compile_options(nopen_recovery PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(nopen-test-recovery tests/test_recovery.c)
target_link_libraries(nopen-test-recovery PRIVATE nopen_recovery)
target_compile_options(nopen-test-recovery PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(nopen-recovery-lab src/nopen_recovery_lab.c)
target_link_libraries(nopen-recovery-lab PRIVATE nopen_recovery)
target_compile_options(nopen-recovery-lab PRIVATE -Wall -Wextra -Wpedantic -Werror)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(nopen-watchdog-fixture tests/watchdog_fixture.c)
target_compile_options(nopen-watchdog-fixture PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(nopen-test-rebuild tests/test_rebuild.c)
target_link_libraries(nopen-test-rebuild PRIVATE nopen_recovery Threads::Threads)
target_compile_options(nopen-test-rebuild PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_library(nopen_isolated_io STATIC src/nopen_isolated_io.c)
target_include_directories(nopen_isolated_io PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(nopen_isolated_io PUBLIC nopen_page_core nopen_blk_backend Threads::Threads)
target_compile_options(nopen_isolated_io PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(nopen-isolated-worker src/nopen_isolated_worker.c)
target_link_libraries(nopen-isolated-worker PRIVATE nopen_blk_backend nopen_page_core)
target_compile_options(nopen-isolated-worker PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(nopen-test-isolated-io tests/test_isolated_io.c)
target_link_libraries(nopen-test-isolated-io PRIVATE nopen_isolated_io)
target_compile_options(nopen-test-isolated-io PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(nopen-isolated-smoke src/nopen_isolated_smoke.c)
target_link_libraries(nopen-isolated-smoke PRIVATE nopen_isolated_io)
target_compile_options(nopen-isolated-smoke PRIVATE -Wall -Wextra -Wpedantic -Werror)
enable_testing()
add_test(NAME v060_gpu_loss_fail_closed_and_cpu_mirror COMMAND nopen-test-recovery)
set_tests_properties(v060_gpu_loss_fail_closed_and_cpu_mirror PROPERTIES
  PASS_REGULAR_EXPRESSION "RECOVERY_TEST_PASS")
add_test(NAME v060_mock_mirror_recovery_lab COMMAND nopen-recovery-lab
  --backend mock --mode mirror --operations 1000 --inject-loss-after 500)
add_test(NAME v060_mock_failclosed_recovery_lab COMMAND nopen-recovery-lab
  --backend mock --mode failclosed --operations 1000 --inject-loss-after 500)
set_tests_properties(v060_mock_mirror_recovery_lab v060_mock_failclosed_recovery_lab
  PROPERTIES PASS_REGULAR_EXPRESSION "RECOVERY_LAB_PASS" TIMEOUT 15)
add_test(NAME v060_userspace_watchdog_timeout_and_proof
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/tests/test_watchdog.py"
    "${CMAKE_CURRENT_LIST_DIR}/tools/watchdog.py" $<TARGET_FILE:nopen-watchdog-fixture>)
set_tests_properties(v060_userspace_watchdog_timeout_and_proof PROPERTIES
    PASS_REGULAR_EXPRESSION "WATCHDOG_TEST_PASS" TIMEOUT 12)

add_test(NAME v060_readonly_game_baseline_samples_and_preserves_csv
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/tests/test_baseline.py"
    "${CMAKE_CURRENT_LIST_DIR}/tools/game_baseline.py")
set_tests_properties(v060_readonly_game_baseline_samples_and_preserves_csv PROPERTIES
    PASS_REGULAR_EXPRESSION "BASELINE_TEST_PASS" TIMEOUT 10)

add_test(NAME v060_two_phase_gpu_rebuild_and_concurrent_io COMMAND nopen-test-rebuild)
set_tests_properties(v060_two_phase_gpu_rebuild_and_concurrent_io PROPERTIES
  PASS_REGULAR_EXPRESSION "REBUILD_TEST_PASS" TIMEOUT 30)

add_test(NAME v060_mock_gpu_rehydration_full_verify COMMAND nopen-recovery-lab
 --backend mock --mode mirror --operations 1000 --inject-loss-after 500
 --rebuild-after-loss)
set_tests_properties(v060_mock_gpu_rehydration_full_verify PROPERTIES
 PASS_REGULAR_EXPRESSION "RECOVERY_LAB_PASS" TIMEOUT 15)

add_test(NAME v060_isolated_gpu_process_deadline_and_ipc_checksum
    COMMAND nopen-test-isolated-io $<TARGET_FILE:nopen-isolated-worker>)
set_tests_properties(v060_isolated_gpu_process_deadline_and_ipc_checksum
    PROPERTIES PASS_REGULAR_EXPRESSION "ISOLATED_TEST_PASS" TIMEOUT 15)

add_test(NAME v060_mock_independent_GPU_worker_integration
    COMMAND nopen-isolated-smoke --worker $<TARGET_FILE:nopen-isolated-worker>
            --backend mock --operations 400 --timeout-ms 1000)
set_tests_properties(v060_mock_independent_GPU_worker_integration PROPERTIES
    PASS_REGULAR_EXPRESSION "ISOLATED_GPU_PASS" TIMEOUT 15)
