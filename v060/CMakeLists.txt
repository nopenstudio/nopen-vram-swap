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
