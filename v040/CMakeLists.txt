cmake_minimum_required(VERSION 3.20)
project(nopen_v040 C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(NOPEN_OPENCL "Build real OpenCL AMD discrete GPU backend" ON)
# Inherit v0.3 and v0.12 regression tests and their statically linked libraries.
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../v030" "${CMAKE_CURRENT_BINARY_DIR}/v030")
add_executable(nopen-economics src/nopen_economics.c)
target_compile_options(nopen-economics PRIVATE -Wall -Wextra -Werror -Wpedantic)
target_link_libraries(nopen-economics PRIVATE nopen_vram_memory)
add_executable(nopen-import-test tests/test_import.c)
target_compile_options(nopen-import-test PRIVATE -Wall -Wextra -Werror -Wpedantic)
target_link_libraries(nopen-import-test PRIVATE nopen_vram_memory)
enable_testing()
add_test(NAME v040_bulk_zero_import_integrity COMMAND nopen-import-test)
add_test(NAME v040_cpu_baseline_4mib COMMAND nopen-economics
  --backend cpu --logical-mib 4 --ram-pages 32 --prefetch-pages 16)
add_test(NAME v040_mock_gpu_4mib COMMAND nopen-economics
  --backend mock --logical-mib 4 --ram-pages 32 --prefetch-pages 16)
set_tests_properties(v040_bulk_zero_import_integrity PROPERTIES
  PASS_REGULAR_EXPRESSION "IMPORT_TEST_PASS")
set_tests_properties(v040_cpu_baseline_4mib v040_mock_gpu_4mib PROPERTIES
  PASS_REGULAR_EXPRESSION "ECONOMICS_PASS")
