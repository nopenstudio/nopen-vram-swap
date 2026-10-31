cmake_minimum_required(VERSION 3.20)
project(nopen_v083 C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(NOPEN_OPENCL "Build OpenCL GPU backend; disabled for mock-only CI" ON)
enable_testing()
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../v080" "${CMAKE_CURRENT_BINARY_DIR}/v080")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# No ublk module, /dev devices, swapon, or GPU context is required by this
# test. Parent runs mock IPC through a separate userspace worker process.
add_executable(nopen-v083-test-worker-failure
  tests/test_v083_worker_failure.c)
target_link_libraries(nopen-v083-test-worker-failure PRIVATE nopen_isolated_io)
target_compile_options(nopen-v083-test-worker-failure
  PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME v083_mock_worker_timeout_crash_bad_reply_failclosed
  COMMAND nopen-v083-test-worker-failure
          $<TARGET_FILE:nopen-isolated-worker>)
set_tests_properties(v083_mock_worker_timeout_crash_bad_reply_failclosed
  PROPERTIES PASS_REGULAR_EXPRESSION "V083_WORKER_FAILURE_MATRIX_PASS"
  TIMEOUT 15)

add_test(NAME v083_reclaim_forward_progress_blocker_audit
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_LIST_DIR}/tests/test_reclaim_audit.py")
set_tests_properties(v083_reclaim_forward_progress_blocker_audit
  PROPERTIES PASS_REGULAR_EXPRESSION "V083_RECLAIM_AUDIT_TEST_PASS"
  TIMEOUT 10)
