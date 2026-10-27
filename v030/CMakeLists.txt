cmake_minimum_required(VERSION 3.20)
project(nopen_v030 C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(NOPEN_OPENCL "Build real AMD discrete OpenCL backend; never silently fallback" ON)
# Inherited, previously verified GPU store and cooperative pager with all regressions.
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../v012" "${CMAKE_CURRENT_BINARY_DIR}/v012")
add_executable(nopen-pager-bench src/nopen_pager_bench.c)
target_compile_options(nopen-pager-bench PRIVATE -Wall -Wextra -Werror)
target_link_libraries(nopen-pager-bench PRIVATE nopen_vram_memory)
add_executable(nopen-prefetch-test tests/test_prefetch.c)
target_compile_options(nopen-prefetch-test PRIVATE -Wall -Wextra -Werror)
target_link_libraries(nopen-prefetch-test PRIVATE nopen_vram_memory)
enable_testing()
add_test(NAME v030_prefetch_integrity_fail_closed COMMAND nopen-prefetch-test)
add_test(NAME v030_coop_demand_mock COMMAND nopen-pager-bench
    --backend mock --pool-mib 4 --logical-pages 512 --ram-pages 32
    --prefetch-pages 0 --passes 2)
add_test(NAME v030_coop_prefetch4_mock COMMAND nopen-pager-bench
    --backend mock --pool-mib 4 --logical-pages 512 --ram-pages 32
    --prefetch-pages 4 --passes 2)
add_test(NAME v030_coop_prefetch16_mock COMMAND nopen-pager-bench
    --backend mock --pool-mib 4 --logical-pages 512 --ram-pages 32
    --prefetch-pages 16 --passes 2)
set_tests_properties(v030_prefetch_integrity_fail_closed PROPERTIES
    PASS_REGULAR_EXPRESSION "PREFETCH_TEST_PASS")
set_tests_properties(v030_coop_demand_mock v030_coop_prefetch4_mock
    v030_coop_prefetch16_mock PROPERTIES PASS_REGULAR_EXPRESSION "PAGER_PASS")
