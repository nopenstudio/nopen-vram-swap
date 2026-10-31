cmake_minimum_required(VERSION 3.20)
project(nopen_v085 C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(NOPEN_OPENCL "Build GPU backend; OFF for read-only v0.8.5 CI" ON)
enable_testing()
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../v084" "${CMAKE_CURRENT_BINARY_DIR}/v084")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(NAME v085_reserved_vram_boot_preflight_policy
 COMMAND "${Python3_EXECUTABLE}"
         "${CMAKE_CURRENT_LIST_DIR}/tests/test_reserved_vram_preflight.py")
set_tests_properties(v085_reserved_vram_boot_preflight_policy
 PROPERTIES PASS_REGULAR_EXPRESSION "V085_PREFLIGHT_FIXTURE_PASS" TIMEOUT 10)

add_test(NAME v085_reserved_vram_os_counter_observer
 COMMAND "${Python3_EXECUTABLE}"
         "${CMAKE_CURRENT_LIST_DIR}/tests/test_reserved_vram_observe.py")
set_tests_properties(v085_reserved_vram_os_counter_observer
 PROPERTIES PASS_REGULAR_EXPRESSION "V085_OBSERVER_FIXTURE_PASS" TIMEOUT 10)
