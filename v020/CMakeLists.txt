cmake_minimum_required(VERSION 3.20)
project(nopen_vram_v020 C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(NOPEN_OPENCL "Build the discrete AMD GPU transfer backend" ON)
add_executable(nopen-link-bench src/nopen_link_bench.c)
target_compile_options(nopen-link-bench PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(NOPEN_OPENCL)
  find_package(OpenCL REQUIRED)
  target_compile_definitions(nopen-link-bench PRIVATE NOPEN_OPENCL=1)
  target_link_libraries(nopen-link-bench PRIVATE OpenCL::OpenCL)
endif()
enable_testing()
add_test(NAME v020_mock_4k_integrity COMMAND nopen-link-bench
  --backend mock --pool-mib 4 --chunk-kib 4 --passes 2)
add_test(NAME v020_mock_1m_integrity COMMAND nopen-link-bench
  --backend mock --pool-mib 4 --chunk-kib 1024 --passes 2)
set_tests_properties(v020_mock_4k_integrity v020_mock_1m_integrity PROPERTIES
  PASS_REGULAR_EXPRESSION "BENCH_PASS backend=mock verified_bytes=4194304")
